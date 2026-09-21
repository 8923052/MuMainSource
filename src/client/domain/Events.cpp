#include "domain/Events.h"
#include "network/generated/PacketFunctions_ClientToServer.h"
#include "support/CoreMath.h"
#include "session/SessionGameplay.h"
#include "domain/ItemsSkills.h"
#include "render/Textures.h"
#include "domain/EffectsUpdate.h"
#include "domain/MovementAI.h"
#include "domain/CharacterSystem.h"
#include "app/ApplicationAudio.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/session/UiSessionLogic.h"
#include "session/SessionKeeper.h"
#include "data/GameData.h"
#include "data/WorldData.h"
#include "render/Terrain.h"
#include "session/SessionNetwork.h"
#include "render/ModelGeometry.h"
#include "domain/CharacterPresentation.h"
#include "data/Localization.h"
#include "render/ModelResources.h"
#include "domain/WorldSimulation.h"
#include "render/World.h"
#include "ui/runtime/UiControls.h"
#include "I18N/All.h"
#include "session/SessionRender.h"
#include "app/AppWindow.h"
#include "domain/ChatSocial.h"
#include "data/ItemData.h"
#include "ui/features/Items/ItemsRender.h"
#include "render/Text.h"
#include "support/Scenes.h"
#include "domain/Shop.h"
#include "domain/Quests.h"
#include "ui/features/World/WorldLogic.h"
#include "domain/MapSimulation.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "app/ApplicationNetwork.h"
#include "ui/features/Hud/HudLogic.h"
#include "app/ApplicationLoopFrame.h"
#include "session/SessionUi.h"
#include "session/SessionWorkspace.h"
#include "domain/Automation.h"
#include "support/Camera.h"

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kRadToDeg = 180.0f / kPi;

float UnwindDegrees360(float degrees)
{
    degrees = std::fmod(degrees, 360.0f);
    if (degrees < 0.0f)
        degrees += 360.0f;
    return degrees;
}
} // namespace

CHARACTER *CDirection::FindLiveCharacterByKey(int key)
{
    for (CHARACTER *character : CharactersClient.Pointers())
    {
        if (character != nullptr && character->Object.Live && character->Key == key)
        {
            return character;
        }
    }
    return nullptr;
}

CDirection::CDirection(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), g_bTimeCheck(keeper.DirectionTimeCheckFlag()),
      g_iBackupTime(keeper.DirectionBackupTime()), m_CKanturu(*this), m_CMVP(keeper, *this)
{
    Init();
}

CDirection::~CDirection()
{
}

void CDirection::Init()
{
    directionSequenceEnded_ = true;
    Vector(0.0f, 0.0f, 0.0f, m_vCameraPosition);
    Vector(0.0f, 0.0f, 0.0f, m_v1stPosition);
    Vector(0.0f, 0.0f, 0.0f, m_v2ndPosition);
    Vector(0.0f, 0.0f, 0.0f, m_vResult);

    m_bStateCheck = true;
    m_bCameraCheck = false;
    m_bAction = true;
    m_bTimeCheck = true;
    m_bOrderExit = false;

    m_bDownHero = false;

    m_fCount = 0.0f;
    m_fLength = 0.0f;
    m_fCameraSpeed = 100.0f;
    m_fCameraViewFar = 2000.0f;

    m_iCheckTime = 0;
    m_iTimeSchedule = 0;
    m_iBackupTime = 0;
    m_CameraLevel = 0;

    m_AngleY = 0.0f;

    m_CMVP.Init();
    m_CKanturu.Init();
}

void CDirection::CloseAllWindows()
{
    sessionKeeper_.Gameplay()->RestorePickedItem();
    g_pNewUISystem->HideAll();
}

bool CDirection::IsDirection(int world) const
{
    if (world == WD_34CRYWOLF_1ST)
        return m_CMVP.IsCryWolfDirection();
    else if (world == WD_39KANTURU_3RD)
        return m_CKanturu.IsKanturuDirection();

    return false;
}

void CDirection::CheckDirection(int world, double worldTime)
{
    directionFramesRemaining_ = sessionKeeper_.FrameAnimationFactor();
    if (directionFramesRemaining_ <= 0.f)
        return;
    directionSequenceEnded_ = false;
    directionTime_ =
        worldTime - directionFramesRemaining_ *
                        (1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps);
    m_CMVP.CryWolfDirection(world, worldTime);
    m_CKanturu.KanturuAllDirection(world, worldTime);
}

void CDirection::SetCameraPosition()
{
    if (m_bStateCheck)
    {
        if (m_iTimeSchedule == 0)
        {
            VectorCopy(Hero->Object.Position, m_v1stPosition);
            m_iTimeSchedule++;
        }

        VectorSubtract(m_v2ndPosition, m_v1stPosition, m_vResult);
        m_fLength = VectorLength(m_vResult);
    }
}

int CDirection::GetCameraPosition(vec3_t GetPosition)
{
    VectorCopy(m_vCameraPosition, GetPosition);
    return m_CameraLevel;
}

void CDirection::SpendDirectionFrames(float frames)
{
    directionFramesRemaining_ = std::max(0.f, directionFramesRemaining_ - frames);
    directionTime_ += frames * (1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps);
}

bool CDirection::DirectionCameraMove()
{
    if (!m_bCameraCheck)
        return false;
    SetCameraPosition();
    m_bStateCheck = false;
    if (m_fLength > 0.f)
        VectorNormalize(m_vResult);
    const float frames = m_fCameraSpeed > 0.f
                             ? std::min(directionFramesRemaining_,
                                        std::max(0.f, m_fLength - m_fCount) / m_fCameraSpeed)
                             : 0.f;
    m_fCount = std::min(m_fLength, m_fCount + m_fCameraSpeed * frames);
    SpendDirectionFrames(frames);
    vec3_t offset;
    VectorScale(m_vResult, m_fCount, offset);
    VectorAdd(m_v1stPosition, offset, m_vCameraPosition);
    if (m_fCount < m_fLength)
        return true;
    VectorCopy(m_v2ndPosition, m_vCameraPosition);
    VectorCopy(m_v2ndPosition, m_v1stPosition);
    m_bAction = true;
    m_bCameraCheck = false;
    m_fCount = 0.f;
    return false;
}

void CDirection::DeleteMonster()
{
    const int count = static_cast<int>(stl_Monster.size());
    if (count == 0)
        return;

    for (int i = 0; i < count; ++i)
        DeleteCharacter(i + NUMOFMON);

    stl_Monster.clear();
}

float CDirection::CalculateAngle(CHARACTER *c, int x, int y, float Angle)
{
    vec3_t vTemp, vTemp2, vResult;
    float fx = (float)(x * TERRAIN_SCALE) + 0.5f * TERRAIN_SCALE;
    float fy = (float)(y * TERRAIN_SCALE) + 0.5f * TERRAIN_SCALE;

    Vector(fx, fy, 0.0f, vTemp);
    Vector(c->Object.Position[0], c->Object.Position[1], 0.0f, vTemp2);

    VectorSubtract(vTemp2, vTemp, vResult);
    Vector(0.0f, 1.0f, 0.0f, vTemp2);

    VectorNormalize(vResult);

    const float yawFromPositiveY = std::atan2(vResult[0], vResult[1]) * kRadToDeg;
    return UnwindDegrees360(yawFromPositiveY);
}

void CDirection::SummonCreateMonster(EMonsterType Type, int x, int y, float Angle, bool NextCheck,
                                     bool SummonAni, float AniSpeed)
{
    auto birth = sessionKeeper_.Gameplay()->EmissionTime(directionFramesRemaining_);
    CHARACTER *c = nullptr;
    DirectionMonster DMonster = {
        0,
    };

    DMonster.m_Index = stl_Monster.size();
    DMonster.m_bAngleCheck = true;
    DMonster.m_iActionCheck = 0;

    stl_Monster.push_back(DMonster);

    c = CreateMonster(Type, x, y, DMonster.m_Index + NUMOFMON);
    c->Key = NUMOFMON + DMonster.m_Index++;
    c->Object.SetAngleZ(Angle);
    c->Weapon[0].Type = -1;
    c->Weapon[1].Type = -1;
    c->Object.Alpha = 0.0f;

    int Index = 0;

    switch (Type)
    {
    case 344:
        Index = MODEL_BALRAM;
        break;
    case 341:
        Index = MODEL_SORAM;
        break;
    case 440:
    case 340:
        Index = MODEL_DARK_ELF_1;
        break;
    case 345:
        Index = MODEL_DEATH_SPIRIT;
        break;
    case 348:
        Index = MODEL_BALLISTA;
        break;
    case 349:
        Index = MODEL_BALGASS;
        break;
    case 361:
        Index = MODEL_DARK_SKULL_SOLDIER_5;
        break;
    }

    BMD *b = &Models[Index];

    if (AniSpeed >= 0.0f)
        b->Actions[MONSTER01_WALK].PlaySpeed = AniSpeed;

    if (SummonAni)
    {
        if (Type == 361)
        {
            vec3_t Light, Angle;

            Vector(0.5f, 0.8f, 1.0f, Light);
            Vector(0.0f, 0.0f, 0.0f, Angle);
            CreateEffect(MODEL_STORM2, c->Object.Position, Angle, Light, 1, nullptr, -1, 0, 0, 0,
                         1.6f);
            CreateEffect(MODEL_STORM2, c->Object.Position, Angle, Light, 1, nullptr, -1, 0, 0, 0,
                         1.3f);
            CreateEffect(MODEL_STORM2, c->Object.Position, Angle, Light, 1, nullptr, -1, 0, 0, 0,
                         0.7f);

            PlayBuffer(SOUND_KANTURU_3RD_MAYA_END);
        }
        else
        {
            PlayBuffer(SOUND_CRY1ST_SUMMON);

            vec3_t vPos;
            Vector(c->Object.Position[0] + 20.0f, c->Object.Position[1] + 20.0f,
                   c->Object.Position[2], vPos);

            CreateJoint(BITMAP_JOINT_THUNDER + 1, vPos, vPos, c->Object.Angle, 7, nullptr,
                        60.f + WorldRandom() % 10);
            CreateJoint(BITMAP_JOINT_THUNDER + 1, vPos, vPos, c->Object.Angle, 7, nullptr,
                        50.f + WorldRandom() % 10);
            CreateJoint(BITMAP_JOINT_THUNDER + 1, vPos, vPos, c->Object.Angle, 7, nullptr,
                        50.f + WorldRandom() % 10);
            CreateJoint(BITMAP_JOINT_THUNDER + 1, vPos, vPos, c->Object.Angle, 7, nullptr,
                        60.f + WorldRandom() % 10);

            CreateParticle(BITMAP_SMOKE + 4, c->Object.Position, c->Object.Angle, c->Object.Light,
                           1, 5.0f);
            CreateParticle(BITMAP_SMOKE + 4, c->Object.Position, c->Object.Angle, c->Object.Light,
                           1, 5.0f);
            CreateParticle(BITMAP_SMOKE + 4, c->Object.Position, c->Object.Angle, c->Object.Light,
                           1, 5.0f);

            Vector(c->Object.Position[0], c->Object.Position[1], c->Object.Position[2] + 120.0f,
                   vPos);
            CreateJoint(BITMAP_JOINT_THUNDER, c->Object.Position, vPos, c->Object.Angle, 17);
            CreateJoint(BITMAP_JOINT_THUNDER, c->Object.Position, vPos, c->Object.Angle, 17);
            CreateJoint(BITMAP_JOINT_THUNDER, c->Object.Position, vPos, c->Object.Angle, 17);
            CreateJoint(BITMAP_JOINT_THUNDER, c->Object.Position, vPos, c->Object.Angle, 17);
        }
    }

    if (NextCheck)
        m_iCheckTime++;
}

bool CDirection::MoveCreatedMonster(int Index, int x, int y, float Angle, int Speed)
{
    CHARACTER *character = FindLiveCharacterByKey(Index + NUMOFMON);
    if (!character)
        return false;
    auto &object = character->Object;
    const float frames = directionFramesRemaining_;
    float remaining = frames;
    const float targetX = (x + 0.5f) * TERRAIN_SCALE;
    const float targetY = (y + 0.5f) * TERRAIN_SCALE;
    if (object.Position[0] == targetX && object.Position[1] == targetY)
    {
        stl_Monster[Index].m_bAngleCheck = true;
        SetAction(&object, MONSTER01_STOP1);
        return true;
    }
    const float startFraction = 1.f - frames / sessionKeeper_.FrameAnimationFactor();
    object.MotionTrace.BeginDrivenMotion(sessionKeeper_.FrameWorldTime(), frames, object.Position,
                                         true, startFraction);
    const float targetYaw = CalculateAngle(character, x, y, Angle);
    if (stl_Monster[Index].m_bAngleCheck)
    {
        const float difference = std::remainder(targetYaw - object.Angle[2], 360.f);
        const float turnFrames = std::max(0.f, std::abs(difference) - 3.f) / 3.f;
        const float step = std::min(remaining, turnFrames);
        const float from = object.Angle[2];
        object.SetAngleZ(from + std::copysign(3.f * step, difference));
        object.MotionTrace.TurnYaw(step, from, object.Angle[2], 0.f);
        object.MotionTrace.Advance(step, object.Position);
        remaining -= step;
        if (step < turnFrames)
        {
            directionActorFrames_ = std::max(directionActorFrames_, frames);
            SetAction(&object, MONSTER01_STOP1);
            return false;
        }
        object.SetAngleZ(targetYaw);
        stl_Monster[Index].m_bAngleCheck = false;
        character->Blood = true;
    }
    character->MoveSpeed = Speed;
    SetAction(&object, MONSTER01_WALK);
    const float dx = targetX - object.Position[0], dy = targetY - object.Position[1];
    const float distance = std::hypot(dx, dy);
    const float speed = CharacterMoveSpeed(character);
    const float moveFrames = speed > 0.f ? distance / speed : 0.f;
    const float step = std::min(remaining, moveFrames);
    const bool arrived = distance == 0.f || (speed > 0.f && step == moveFrames);
    if (distance > 0.f)
    {
        const float amount = speed * step / distance;
        object.Position[0] += dx * amount;
        object.Position[1] += dy * amount;
    }
    directionActorFrames_ = std::max(directionActorFrames_, frames - remaining + step);
    sessionKeeper_.Gameplay()->FinishCharacterMovement(character, step);
    object.MotionTrace.Advance(step, object.Position);
    if (!arrived)
        return false;
    object.Position[0] = targetX;
    object.Position[1] = targetY;
    stl_Monster[Index].m_bAngleCheck = true;
    SetAction(&object, MONSTER01_STOP1);
    return true;
}

CHARACTER *SessionLegacyCalls::FindLiveCharacterByKey(int key)
{
    return sessionKeeper_.DirectionObject().FindLiveCharacterByKey(key);
} // OMF-00707

bool CDirection::ActionCreatedMonster(int Index, int Action, int Count, bool TankerAttack,
                                      bool NextCheck)
{
    // Actor animations advance in the character owner; this script waits for that completion.
    directionWaitsForActor_ = true;
    CHARACTER *c = nullptr;
    bool bNext = false;

    c = FindLiveCharacterByKey(Index + NUMOFMON);
    if (c == nullptr)
        return false;

    if (stl_Monster[Index].m_iActionCheck == Count)
        bNext = true;

    if (!bNext)
    {
        if (c->Object.CurrentAction != Action)
        {
            c->Object.EnableBoneMatrix = false;
            c->Object.CurrentAction = Action;
            c->Object.SetAnimationFrame(0.0f);
            stl_Monster[Index].m_iActionCheck++;

            if (TankerAttack)
                CreateEffect(MODEL_ARROW_TANKER, c->Object.Position, c->Object.Angle,
                             c->Object.Light, 1, &c->Object, c->Object.PKKey);
        }
    }
    else
    {
        if (NextCheck)
        {
            stl_Monster[Index].m_iActionCheck = 0;
            m_iCheckTime++;
        }

        if (c->Object.AnimationFrame >= 8.0f)
            return true;
    }

    return false;
}

void CDirection::HeroFallingDownDirection()
{
    if (!m_bDownHero)
        return;

    constexpr float Acceleration = 1.5f, TurnPerTick = 2.f;
    if (Hero->Object.Gravity == 0.f)
        m_AngleY = Hero->Object.Angle[2];
    Hero->Object.EnableBoneMatrix = false;
    MoveFallingObject(&Hero->Object, Acceleration, TurnPerTick,
                      sessionKeeper_.FrameAnimationFactor());
    Hero->Object.SetAngleZ(m_AngleY);

    FaillingEffect();
}

void CDirection::FaillingEffect()
{
    vec3_t Pos, Light;
    float Scale = 1.3f + WorldRandom() % 10 / 30.0f;
    Vector(0.05f, 0.05f, 0.08f, Light);

    Pos[0] = Hero->Object.Position[0] + (float)(WorldRandom() % 20 - 10) * 70.0f;
    Pos[1] = Hero->Object.Position[1] + (float)(WorldRandom() % 20 - 10) * 70.0f;
    Pos[2] = Hero->Object.Position[2] - WorldRandom() % 200 - 500.0f;

    CreateParticleFpsChecked(BITMAP_CLOUD, Pos, Hero->Object.Angle, Light, 13, Scale);

    Pos[0] = Hero->Object.Position[0] + (float)(WorldRandom() % 20 - 10) * 70.0f;
    Pos[1] = Hero->Object.Position[1] + (float)(WorldRandom() % 20 - 10) * 70.0f;
    Pos[2] = Hero->Object.Position[2] - WorldRandom() % 200 - 500.0f;

    Vector(0.05f, 0.05f, 0.05f, Light);
    CreateParticleFpsChecked(BITMAP_CLOUD, Pos, Hero->Object.Angle, Light, 13, Scale);
}

void CDirection::HeroFallingDownInit()
{
    if (!m_bDownHero)
    {
        Hero->Object.m_bActionStart = false;
        Hero->Object.Gravity = 0.0f;
        if (Hero->Object.Angle[0] != 0.0f)
        {
            Hero->Object.Angle[0] = 0.0f;
            Hero->Object.EnableBoneMatrix = false;
        }
    }
}

void CDirection::CameraLevelUp()
{
    if (m_CameraLevel < 4)
        m_CameraLevel++;
}

void CDirection::SetNextDirectionPosition(int x, int y, int z, float Speed)
{
    m_iCheckTime = 0;
    m_bCameraCheck = true;
    m_bStateCheck = true;
    m_fCameraSpeed = Speed;
    m_fCount = 0.f;

    float fx = (float)(x * TERRAIN_SCALE) + 0.5f * TERRAIN_SCALE;
    float fy = (float)(y * TERRAIN_SCALE) + 0.5f * TERRAIN_SCALE;
    float fz = (float)(z * TERRAIN_SCALE) + 0.5f * TERRAIN_SCALE;

    Vector(fx, fy, fz, m_v2ndPosition);
    m_iTimeSchedule++;
}

bool CDirection::GetTimeCheck(int DelayTime, double worldTime)
{
    if (m_bTimeCheck)
    {
        m_iBackupTime = directionTime_;
        m_bTimeCheck = false;
    }
    const double deadline = m_iBackupTime + DelayTime;
    if (deadline > worldTime)
    {
        SpendDirectionFrames(directionFramesRemaining_);
        return false;
    }
    const float frames =
        static_cast<float>(std::max(0.0, deadline - directionTime_) *
                           sessionKeeper_.ApplicationConfig().legacyReferenceFps / 1000.0);
    SpendDirectionFrames(frames);
    m_bTimeCheck = true;
    ++m_iCheckTime;
    return true;
}

CKanturuDirection::CKanturuDirection(CDirection &direction) : g_Direction(direction)
{
    m_iKanturuState = 0;
    m_iMayaState = 0;
    m_iNightmareState = 0;
    Init();
}

CKanturuDirection::~CKanturuDirection()
{
}

void CKanturuDirection::Init()
{
    m_bDirectionEnd = false;
    m_bKanturuDirection = false;
    m_bMayaDie = false;
    m_bMayaAppear = false;
}

bool CKanturuDirection::IsKanturuDirection() const
{
    return m_bKanturuDirection;
}

bool CKanturuDirection::IsMayaScene(int world) const
{
    if (!IsKanturuWorldActive(world))
        return false;

    switch (m_iKanturuState)
    {
    case KANTURU_STATE_NIGHTMARE_BATTLE:
    case KANTURU_STATE_TOWER:
    case KANTURU_STATE_END:
        return false;
    default:
        return true;
    }
}

bool CKanturuDirection::IsKanturu3rdTimer(int world) const
{
    if (!IsKanturuWorldActive(world))
        return false;

    switch (m_iMayaState)
    {
    case KANTURU_MAYA_DIRECTION_MONSTER1:
    case KANTURU_MAYA_DIRECTION_MAYA1:
    case KANTURU_MAYA_DIRECTION_MONSTER2:
    case KANTURU_MAYA_DIRECTION_MAYA2:
    case KANTURU_MAYA_DIRECTION_MONSTER3:
    case KANTURU_MAYA_DIRECTION_MAYA3:
        return true;
    default:
        break;
    }

    return (m_iNightmareState == KANTURU_NIGHTMARE_DIRECTION_BATTLE);
}

void CKanturuDirection::GetKanturuAllState(int world, std::uint8_t State, std::uint8_t DetailState)
{
    m_iKanturuState = State;

    if (m_iKanturuState == KANTURU_STATE_STANDBY)
    {
        g_Direction.Init();
        Init();
        m_iMayaState = 0;
        m_iNightmareState = 0;
    }
    else if (m_iKanturuState == KANTURU_STATE_MAYA_BATTLE)
    {
        m_iNightmareState = 0;
        GetKanturuMayaState(world, DetailState);
    }
    else if (m_iKanturuState == KANTURU_STATE_NIGHTMARE_BATTLE)
    {
        m_iMayaState = 0;
        GetKanturuNightmareState(world, DetailState);
    }
}

void CKanturuDirection::GetKanturuMayaState(int world, std::uint8_t DetailState)
{
    m_iMayaState = DetailState;

    if (!IsKanturuWorldActive(world))
        return;

    switch (m_iMayaState)
    {
    case KANTURU_MAYA_DIRECTION_NOTIFY:
        PrepareCameraFocus();
        ActivateDirectionSequence();
        break;
    case KANTURU_MAYA_DIRECTION_ENDCYCLE_MAYA3:
        PrepareCameraFocus();
        ActivateDirectionSequence();
        break;
    default:
        ResetDirectionState();
        break;
    }
}

void CKanturuDirection::GetKanturuNightmareState(int world, std::uint8_t DetailState)
{
    m_iNightmareState = DetailState;

    if (!IsKanturuWorldActive(world))
        return;

    switch (m_iNightmareState)
    {
    case KANTURU_NIGHTMARE_DIRECTION_NIGHTMARE:
        PrepareCameraFocus(false);
        ActivateDirectionSequence();
        break;
    case KANTURU_NIGHTMARE_DIRECTION_END:
        g_Direction.m_CameraLevel = 5;
        ActivateDirectionSequence();
        g_Direction.DeleteMonster();
        ResetDirectionState();
        break;
    default:
        g_Direction.DeleteMonster();
        ResetDirectionState();
        break;
    }
}

void CKanturuDirection::KanturuAllDirection(int world, double worldTime)
{
    if (m_bDirectionEnd || !IsKanturuWorldActive(world))
        return;

    if (m_iMayaState == KANTURU_MAYA_DIRECTION_END)
        g_Direction.m_bDownHero = true;

    g_Direction.HeroFallingDownDirection();
    g_Direction.HeroFallingDownInit();

    if (m_iKanturuState == KANTURU_STATE_MAYA_BATTLE)
        KanturuMayaDirection(worldTime);
    else if (m_iKanturuState == KANTURU_STATE_NIGHTMARE_BATTLE)
        KanturuNightmareDirection(worldTime);
    else if (m_iKanturuState == KANTURU_STATE_STANDBY)
    {
        g_Direction.HeroFallingDownInit();
        g_Direction.Init();
        Init();
    }
}

void CKanturuDirection::KanturuMayaDirection(double worldTime)
{
    switch (m_iMayaState)
    {
    case KANTURU_MAYA_DIRECTION_NOTIFY:
        Move1stDirection(worldTime);
        break;
    case KANTURU_MAYA_DIRECTION_ENDCYCLE_MAYA3:
        Move2ndDirection();
        break;
    }
}

void CKanturuDirection::Move1stDirection(double worldTime)
{
    g_Direction.AdvanceSequence([&] {
        if (g_Direction.DirectionCameraMove())
            return;

        switch (g_Direction.m_iTimeSchedule)
        {
        case 0:
            Direction1st0();
            break;
        case 1:
            Direction1st1(worldTime);
            break;
        }
    });
}

void CKanturuDirection::Direction1st0()
{
    ApplyDirectionTarget(DirectionTarget{196, 85, 0, 100.0f});
}

void CKanturuDirection::Direction1st1(double worldTime)
{
    if (g_Direction.m_bAction)
    {
        if (g_Direction.m_iCheckTime == 0)
        {
            m_bMayaAppear = true;
            g_Direction.m_iCheckTime++;
        }
        else if (g_Direction.m_iCheckTime == 1)
        {
            if (!m_bMayaAppear)
                g_Direction.m_iCheckTime++;
        }
        else if (g_Direction.m_iCheckTime == 2)
        {
            g_Direction.GetTimeCheck(3000, worldTime);
        }
        else if (g_Direction.m_iCheckTime == 3)
        {
            g_Direction.Init();
            Init();
            m_bDirectionEnd = true;
        }
    }
}

void CKanturuDirection::Move2ndDirection()
{
    g_Direction.AdvanceSequence([&] {
        if (g_Direction.DirectionCameraMove())
            return;

        switch (g_Direction.m_iTimeSchedule)
        {
        case 0:
            Direction2nd0();
            break;
        case 1:
            Direction2nd1();
            break;
        case 2:
            Direction2nd2();
            break;
        }
    });
}

void CKanturuDirection::Direction2nd0()
{
    ApplyDirectionTarget(DirectionTarget{196, 85, 0, 100.0f});
}

void CKanturuDirection::Direction2nd1()
{
    if (g_Direction.m_bAction)
    {
        if (g_Direction.m_iCheckTime == 0)
        {
            m_bMayaDie = true;
            g_Direction.m_iCheckTime++;
        }
        else if (g_Direction.m_iCheckTime == 1)
        {
            if (!m_bMayaDie)
                g_Direction.m_iCheckTime++;
        }
        else if (g_Direction.m_iCheckTime == 2)
        {
            Init();
            g_Direction.Init();
            g_Direction.m_bAction = true;
            g_Direction.m_iTimeSchedule = 2;
        }
    }
}

void CKanturuDirection::Direction2nd2()
{
    if (g_Direction.m_bAction)
    {
        if (g_Direction.m_iCheckTime == 0)
            g_Direction.m_bDownHero = true;
    }
}

bool CKanturuDirection::GetMayaExplotion() const
{
    return m_bMayaDie;
}

void CKanturuDirection::SetMayaExplotion(bool MayaDie)
{
    m_bMayaDie = MayaDie;
}

bool CKanturuDirection::GetMayaAppear() const
{
    return m_bMayaAppear;
}

void CKanturuDirection::SetMayaAppear(bool MayaAppear)
{
    m_bMayaAppear = MayaAppear;
}

void CKanturuDirection::KanturuNightmareDirection(double worldTime)
{
    switch (m_iNightmareState)
    {
    case KANTURU_NIGHTMARE_DIRECTION_NIGHTMARE:
        Move3rdDirection(worldTime);
        break;
    case KANTURU_NIGHTMARE_DIRECTION_END:
        Move4thDirection();
        break;
    }
}

void CKanturuDirection::Move3rdDirection(double worldTime)
{
    g_Direction.AdvanceSequence([&] {
        if (g_Direction.DirectionCameraMove())
            return;

        switch (g_Direction.m_iTimeSchedule)
        {
        case 0:
            Direction3rd0();
            break;
        case 1:
            Direction3rd1(worldTime);
            break;
        }
    });
}

void CKanturuDirection::Direction3rd0()
{
    ApplyDirectionTarget(DirectionTarget{80, 142, 0, 100.0f});
}

void CKanturuDirection::Direction3rd1(double worldTime)
{
    if (g_Direction.m_bAction)
    {
        if (g_Direction.m_iCheckTime == 0)
            g_Direction.GetTimeCheck(1000, worldTime);
        else if (g_Direction.m_iCheckTime == 1)
            g_Direction.SummonCreateMonster(MONSTER_NIGHTMARE, 79, 142, 0, true, true, 0.25f);
        else if (g_Direction.m_iCheckTime == 2)
            g_Direction.GetTimeCheck(5000, worldTime);
        else if (g_Direction.m_iCheckTime == 3)
        {
            Init();
            g_Direction.Init();
            m_bDirectionEnd = true;
            g_Direction.m_bOrderExit = true;
        }
    }
}

void CKanturuDirection::Move4thDirection()
{
    g_Direction.AdvanceSequence([&] {
        if (g_Direction.DirectionCameraMove())
            return;

        switch (g_Direction.m_iTimeSchedule)
        {
        case 0:
            Direction4th0();
            break;
        case 1:
            Direction4th1();
            break;
        }
    });
}

void CKanturuDirection::Direction4th0()
{
    ApplyDirectionTarget(DirectionTarget{113, 232, 0, 300.0f});
}

void CKanturuDirection::Direction4th1()
{
}

bool CKanturuDirection::IsKanturuWorldActive(int world) const
{
    return world == WD_39KANTURU_3RD;
}

void CKanturuDirection::PrepareCameraFocus(bool adjustViewDistance)
{
    g_Direction.CloseAllWindows();
    g_Direction.m_CameraLevel = 5;
    if (adjustViewDistance && g_Direction.m_fCameraViewFar <= 1200.0f)
        g_Direction.m_fCameraViewFar += 10.0f;
}

void CKanturuDirection::ActivateDirectionSequence()
{
    if (!m_bDirectionEnd && !m_bKanturuDirection)
        m_bKanturuDirection = true;
}

void CKanturuDirection::ResetDirectionState()
{
    g_Direction.Init();
    Init();
}

void CKanturuDirection::ApplyDirectionTarget(const DirectionTarget &target)
{
    g_Direction.SetNextDirectionPosition(target.x, target.y, target.z, target.distance);
    g_Direction.m_iTimeSchedule--;
}

namespace
{
constexpr int kCrywolfNotifyDelayMs = 5000;
constexpr int kCrywolfBeginCameraSpeedFast = 300;
constexpr int kCrywolfBeginCameraSpeedSlow = 40;
constexpr int kCrywolfSecondPhaseCameraSpeed = 300;
constexpr int kCrywolfThirdPhaseCameraSpeed = 300;
constexpr int kCrywolfFourthPhaseCameraSpeed = 40;

struct CameraTarget
{
    int x;
    int y;
    int z;
    float speed;
};

struct MonsterSpawnCommand
{
    EMonsterType type;
    int x;
    int y;
    float angle;
    bool nextCheck;
    bool summonAnimation;
    float animationSpeed;
};

struct MonsterMoveCommand
{
    int index;
    int x;
    int y;
    float angle;
    int speed;
};

constexpr CameraTarget kBeginDirection0Target{113, 232, 0,
                                              static_cast<float>(kCrywolfBeginCameraSpeedFast)};
constexpr CameraTarget kBeginDirection1Fallback{114, 220, 0,
                                                static_cast<float>(kCrywolfBeginCameraSpeedSlow)};
constexpr CameraTarget kBeginDirection2Fallback{114, 160, 0,
                                                static_cast<float>(kCrywolfSecondPhaseCameraSpeed)};
constexpr CameraTarget kBeginDirection3Fallback{121, 75, 0,
                                                static_cast<float>(kCrywolfThirdPhaseCameraSpeed)};
constexpr CameraTarget kBeginDirection4Fallback{121, 87, 0,
                                                static_cast<float>(kCrywolfFourthPhaseCameraSpeed)};

constexpr std::array<MonsterSpawnCommand, 13> kBeginDirection2WaveOne = {
    MonsterSpawnCommand{MONSTER_BALLISTA, 110, 240, 0.0f, false, false, -1.0f},
    {MONSTER_BALLISTA, 114, 240, 0.0f, false, false, -1.0f},
    {MONSTER_BALLISTA, 118, 240, 0.0f, false, false, -1.0f},
    {MONSTER_SORAM, 110, 242, 0.0f, false, false, -1.0f},
    {MONSTER_SORAM, 112, 242, 0.0f, false, false, -1.0f},
    {MONSTER_SORAM, 114, 242, 0.0f, false, false, -1.0f},
    {MONSTER_SORAM, 116, 242, 0.0f, false, false, -1.0f},
    {MONSTER_SORAM, 118, 242, 0.0f, false, false, -1.0f},
    {MONSTER_SORAM, 110, 244, 0.0f, false, false, -1.0f},
    {MONSTER_SORAM, 112, 244, 0.0f, false, false, -1.0f},
    {MONSTER_SORAM, 114, 244, 0.0f, false, false, -1.0f},
    {MONSTER_SORAM, 116, 244, 0.0f, false, false, -1.0f},
    {MONSTER_SORAM, 118, 244, 0.0f, true, true, -1.0f},
};

constexpr std::array<MonsterMoveCommand, 28> kBeginDirection2MoveCommands = {
    MonsterMoveCommand{0, 114, 222, 0.0f, 9},
    {1, 110, 222, 0.0f, 12},
    {2, 118, 222, 0.0f, 12},
    {3, 107, 219, 0.0f, 12},
    {4, 108, 220, 0.0f, 12},
    {5, 110, 220, 0.0f, 12},
    {6, 111, 219, 0.0f, 12},
    {7, 116, 219, 0.0f, 12},
    {8, 117, 220, 0.0f, 12},
    {9, 119, 220, 0.0f, 12},
    {10, 120, 219, 0.0f, 12},
    {11, 110, 217, 0.0f, 12},
    {12, 112, 217, 0.0f, 12},
    {13, 114, 217, 0.0f, 12},
    {14, 116, 217, 0.0f, 12},
    {15, 110, 227, 0.0f, 12},
    {16, 114, 227, 0.0f, 12},
    {17, 118, 227, 0.0f, 12},
    {18, 110, 229, 0.0f, 12},
    {19, 112, 229, 0.0f, 12},
    {20, 114, 229, 0.0f, 12},
    {21, 116, 229, 0.0f, 12},
    {22, 118, 229, 0.0f, 12},
    {23, 110, 231, 0.0f, 12},
    {24, 112, 231, 0.0f, 12},
    {25, 114, 231, 0.0f, 12},
    {26, 116, 231, 0.0f, 12},
    {27, 118, 231, 0.0f, 12},
};
} // namespace

CMVP1STDirection::CMVP1STDirection(SessionKeeper &keeper, CDirection &direction)
    : SessionLegacyCalls(keeper), g_Direction(direction)
{
    Init();
}

CMVP1STDirection::~CMVP1STDirection()
{
}

void CMVP1STDirection::Init()
{
    m_bTimerCheck = true;
    m_iCryWolfState = 0;
}

void CMVP1STDirection::GetCryWolfState(std::uint8_t CryWolfState)
{
    m_iCryWolfState = CryWolfState;
}

bool CMVP1STDirection::IsCryWolfDirection() const
{
    return IsSequenceReady() && !m_bTimerCheck && m_iCryWolfState == CRYWOLF_STATE_NOTIFY_2;
}

void CMVP1STDirection::IsCryWolfDirectionTimer()
{
    if (m_iCryWolfState == CRYWOLF_STATE_NOTIFY_2 && m_bTimerCheck &&
        GetTimeCheck(kCrywolfNotifyDelayMs))
    {
        m_bTimerCheck = false;
    }

    if (m_iCryWolfState == CRYWOLF_STATE_READY)
    {
        ResetSequence();
    }
}

void CMVP1STDirection::CryWolfDirection(int world, double worldTime)
{
    if (!IsCrywolfWorldActive(world))
        return;

    IsCryWolfDirectionTimer();

    if (!IsCryWolfDirection())
        return;

    switch (m_iCryWolfState)
    {
    case CRYWOLF_STATE_NOTIFY_2:
        g_Direction.CloseAllWindows();
        MoveBeginDirection(worldTime);
        break;
    default:
        ResetSequence();
        break;
    }
}

void CMVP1STDirection::MoveBeginDirection(double worldTime)
{
    g_Direction.AdvanceSequence([&] {
        if (g_Direction.DirectionCameraMove())
            return;

        switch (g_Direction.m_iTimeSchedule)
        {
        case 0:
            BeginDirection0();
            break;
        case 1:
            BeginDirection1(worldTime);
            break;
        case 2:
            BeginDirection2(worldTime);
            break;
        case 3:
            BeginDirection3();
            break;
        case 4:
            BeginDirection4(worldTime);
            break;
        case 5:
            BeginDirection5(worldTime);
            break;
        }
    });
}

void CMVP1STDirection::BeginDirection0()
{
    QueueCameraMove(kBeginDirection0Target.x, kBeginDirection0Target.y, kBeginDirection0Target.z,
                    kBeginDirection0Target.speed);
    g_Direction.m_iTimeSchedule--;
}

void CMVP1STDirection::BeginDirection1(double worldTime)
{
    if (g_Direction.m_bAction)
    {
        if (g_Direction.m_iCheckTime == 0)
            g_Direction.SummonCreateMonster(MONSTER_BALGASS, 114, 238, 0, true, true, 0.18f);
        else if (g_Direction.m_iCheckTime == 1)
            g_Direction.SummonCreateMonster(MONSTER_DARKELF, 113, 238, 0, true, true, 0.227f);
        else if (g_Direction.m_iCheckTime == 2)
            g_Direction.SummonCreateMonster(MONSTER_DARKELF, 115, 238, 0, true, true, 0.227f);
        else if (g_Direction.m_iCheckTime == 3)
            g_Direction.GetTimeCheck(1000, worldTime);
        else if (g_Direction.m_iCheckTime == 4)
        {
            bool bSuccess[3];
            bSuccess[0] = g_Direction.MoveCreatedMonster(0, 114, 234, 0, 9);
            bSuccess[1] = g_Direction.MoveCreatedMonster(1, 112, 232, 0, 12);
            bSuccess[2] = g_Direction.MoveCreatedMonster(2, 116, 232, 0, 12);

            if (bSuccess[0] && bSuccess[1] && bSuccess[2])
                g_Direction.m_iCheckTime++;
        }
        else if (g_Direction.m_iCheckTime == 5)
            g_Direction.GetTimeCheck(1000, worldTime);
        else if (g_Direction.m_iCheckTime == 6)
        {
            if (g_Direction.ActionCreatedMonster(1, MONSTER01_ATTACK3, 1))
                g_Direction.SummonCreateMonster(MONSTER_DEATH_SPIRIT, 109, 229, 0);
        }
        else if (g_Direction.m_iCheckTime == 7)
            g_Direction.GetTimeCheck(660, worldTime);
        else if (g_Direction.m_iCheckTime == 8)
            g_Direction.SummonCreateMonster(MONSTER_DEATH_SPIRIT, 110, 230, 0);
        else if (g_Direction.m_iCheckTime == 9)
            g_Direction.GetTimeCheck(660, worldTime);
        else if (g_Direction.m_iCheckTime == 10)
            g_Direction.SummonCreateMonster(MONSTER_DEATH_SPIRIT, 112, 230, 0);
        else if (g_Direction.m_iCheckTime == 11)
            g_Direction.GetTimeCheck(660, worldTime);
        else if (g_Direction.m_iCheckTime == 12)
            g_Direction.SummonCreateMonster(MONSTER_DEATH_SPIRIT, 113, 229, 0);
        else if (g_Direction.m_iCheckTime == 13)
            g_Direction.GetTimeCheck(1000, worldTime);
        else if (g_Direction.m_iCheckTime == 14)
        {
            if (g_Direction.ActionCreatedMonster(2, MONSTER01_ATTACK3, 1))
                g_Direction.SummonCreateMonster(MONSTER_BALRAM, 114, 229, 0);
        }
        else if (g_Direction.m_iCheckTime == 15)
            g_Direction.GetTimeCheck(660, worldTime);
        else if (g_Direction.m_iCheckTime == 16)
            g_Direction.SummonCreateMonster(MONSTER_BALRAM, 115, 230, 0);
        else if (g_Direction.m_iCheckTime == 17)
            g_Direction.GetTimeCheck(660, worldTime);
        else if (g_Direction.m_iCheckTime == 18)
            g_Direction.SummonCreateMonster(MONSTER_BALRAM, 117, 230, 0);
        else if (g_Direction.m_iCheckTime == 19)
            g_Direction.GetTimeCheck(660, worldTime);
        else if (g_Direction.m_iCheckTime == 20)
            g_Direction.SummonCreateMonster(MONSTER_BALRAM, 118, 229, 0);
        else if (g_Direction.m_iCheckTime == 21)
            g_Direction.GetTimeCheck(1000, worldTime);
        else if (g_Direction.m_iCheckTime == 22)
        {
            g_Direction.SummonCreateMonster(MONSTER_SORAM, 110, 227, 0);
            g_Direction.CameraLevelUp();
        }
        else if (g_Direction.m_iCheckTime == 23)
            g_Direction.GetTimeCheck(660, worldTime);
        else if (g_Direction.m_iCheckTime == 24)
        {
            g_Direction.SummonCreateMonster(MONSTER_SORAM, 112, 227, 0);
            g_Direction.CameraLevelUp();
        }
        else if (g_Direction.m_iCheckTime == 25)
            g_Direction.GetTimeCheck(660, worldTime);
        else if (g_Direction.m_iCheckTime == 26)
        {
            g_Direction.SummonCreateMonster(MONSTER_SORAM, 114, 227, 0);
            g_Direction.CameraLevelUp();
        }
        else if (g_Direction.m_iCheckTime == 27)
            g_Direction.GetTimeCheck(660, worldTime);
        else if (g_Direction.m_iCheckTime == 28)
        {
            g_Direction.SummonCreateMonster(MONSTER_SORAM, 116, 227, 0);
            g_Direction.CameraLevelUp();
        }
        else if (g_Direction.m_iCheckTime == 29)
            g_Direction.GetTimeCheck(1000, worldTime);
        else if (g_Direction.m_iCheckTime == 30)
            g_Direction.ActionCreatedMonster(0, MONSTER01_ATTACK3, 1, false, true);
        else if (g_Direction.m_iCheckTime == 31)
            g_Direction.GetTimeCheck(2000, worldTime);
        else
            g_Direction.m_bAction = false;
    }
    else
    {
        QueueCameraMove(kBeginDirection1Fallback.x, kBeginDirection1Fallback.y,
                        kBeginDirection1Fallback.z, kBeginDirection1Fallback.speed);
    }
}

void CMVP1STDirection::BeginDirection2(double worldTime)
{
    if (g_Direction.m_bAction)
    {
        if (g_Direction.m_iCheckTime == 0)
        {
            for (const auto &command : kBeginDirection2WaveOne)
            {
                g_Direction.SummonCreateMonster(command.type, command.x, command.y, command.angle,
                                                command.nextCheck, command.summonAnimation,
                                                command.animationSpeed);
            }
        }
        else if (g_Direction.m_iCheckTime == 1)
        {
            g_Direction.GetTimeCheck(1000, worldTime);
        }
        else if (g_Direction.m_iCheckTime == 2)
        {
            bool allMovesSucceeded = true;
            for (const auto &command : kBeginDirection2MoveCommands)
            {
                if (!g_Direction.MoveCreatedMonster(command.index, command.x, command.y,
                                                    command.angle, command.speed))
                {
                    allMovesSucceeded = false;
                }
            }

            if (allMovesSucceeded)
            {
                g_Direction.m_iCheckTime++;
            }
        }
        else if (g_Direction.m_iCheckTime == 3)
            g_Direction.GetTimeCheck(1000, worldTime);
        else if (g_Direction.m_iCheckTime == 4)
            g_Direction.ActionCreatedMonster(0, MONSTER01_ATTACK3, 1, false, true);
        else if (g_Direction.m_iCheckTime == 5)
            g_Direction.GetTimeCheck(2700, worldTime);
        else if (g_Direction.m_iCheckTime == 6)
            g_Direction.ActionCreatedMonster(0, MONSTER01_ATTACK4, 1, false, true);
        else if (g_Direction.m_iCheckTime == 7)
            g_Direction.GetTimeCheck(3000, worldTime);
        else
            g_Direction.m_bAction = false;
    }
    else
        QueueCameraMove(kBeginDirection2Fallback.x, kBeginDirection2Fallback.y,
                        kBeginDirection2Fallback.z, kBeginDirection2Fallback.speed);
}

void CMVP1STDirection::BeginDirection3()
{
    if (g_Direction.m_bAction)
    {
        if (g_Direction.m_iCheckTime == 0)
        {
            g_Direction.DeleteMonster();
            g_Direction.m_iCheckTime++;
        }
        else if (g_Direction.m_iCheckTime == 1)
        {
            g_Direction.SummonCreateMonster(MONSTER_DARKELF, 110, 77, 0, false, false);
            g_Direction.SummonCreateMonster(MONSTER_DARKELF, 125, 77, 0, false, false);

            g_Direction.SummonCreateMonster(MONSTER_SORAM, 90, 37, 0, false, false);
            g_Direction.SummonCreateMonster(MONSTER_SORAM, 108, 73, 0, false, false);
            g_Direction.SummonCreateMonster(MONSTER_SORAM, 109, 75, 0, false, false);
            g_Direction.SummonCreateMonster(MONSTER_SORAM, 110, 73, 0, false, false);
            g_Direction.SummonCreateMonster(MONSTER_SORAM, 111, 75, 0, false, false);
            g_Direction.SummonCreateMonster(MONSTER_SORAM, 112, 73, 0, false, false);
            g_Direction.SummonCreateMonster(MONSTER_SORAM, 123, 73, 0, false, false);
            g_Direction.SummonCreateMonster(MONSTER_SORAM, 124, 75, 0, false, false);
            g_Direction.SummonCreateMonster(MONSTER_SORAM, 125, 73, 0, false, false);
            g_Direction.SummonCreateMonster(MONSTER_SORAM, 126, 75, 0, false, false);
            g_Direction.SummonCreateMonster(MONSTER_SORAM, 127, 73, 0, false, false);
            g_Direction.SummonCreateMonster(MONSTER_SORAM, 176, 20, 0, false, false);

            g_Direction.SummonCreateMonster(MONSTER_SORAM, 117, 77, 0, false, false);
            g_Direction.SummonCreateMonster(MONSTER_SORAM, 119, 77, 0, false, false);
            g_Direction.SummonCreateMonster(MONSTER_SORAM, 121, 77, 0, true, false);

            g_Direction.SummonCreateMonster(MONSTER_DARKELF, 119, 83, 0, false, false);

            g_Direction.SummonCreateMonster(MONSTER_BALRAM, 118, 79, 0, false, false);
            g_Direction.SummonCreateMonster(MONSTER_BALRAM, 120, 79, 0, false, false);

            g_Direction.SummonCreateMonster(MONSTER_BALRAM, 119, 90, 0, false, false);

            g_Direction.SummonCreateMonster(MONSTER_DEATH_SPIRIT, 116, 81, 0, false, false);
            g_Direction.SummonCreateMonster(MONSTER_DEATH_SPIRIT, 119, 81, 0, false, false);
            g_Direction.SummonCreateMonster(MONSTER_DEATH_SPIRIT, 122, 81, 0, false, false);

            g_Direction.SummonCreateMonster(MONSTER_DARKELF, 119, 87, 0, false, false);

            g_Direction.SummonCreateMonster(MONSTER_BALLISTA, 116, 90, 0, false, false);
            g_Direction.SummonCreateMonster(MONSTER_BALLISTA, 122, 90, 0, true, false);
        }
        else
            g_Direction.m_bAction = false;
    }
    else
        QueueCameraMove(kBeginDirection3Fallback.x, kBeginDirection3Fallback.y,
                        kBeginDirection3Fallback.z, kBeginDirection3Fallback.speed);
}

void CMVP1STDirection::BeginDirection4(double worldTime)
{
    if (g_Direction.m_bAction)
    {
        if (g_Direction.m_iCheckTime == 0)
            g_Direction.GetTimeCheck(3000, worldTime);
        else
            g_Direction.m_bAction = false;
    }
    else
        QueueCameraMove(kBeginDirection4Fallback.x, kBeginDirection4Fallback.y,
                        kBeginDirection4Fallback.z, kBeginDirection4Fallback.speed);
}

void CMVP1STDirection::BeginDirection5(double worldTime)
{
    if (g_Direction.m_bAction)
    {
        if (g_Direction.m_iCheckTime == 0)
            g_Direction.GetTimeCheck(2000, worldTime);
        else if (g_Direction.m_iCheckTime == 1)
        {
            g_Direction.ActionCreatedMonster(25, MONSTER01_ATTACK1, 1, true);
            g_Direction.ActionCreatedMonster(26, MONSTER01_ATTACK1, 1, true, true);
        }
        else if (g_Direction.m_iCheckTime == 2)
            g_Direction.GetTimeCheck(4000, worldTime);
        else
            g_Direction.m_bAction = false;
    }
    else
    {
        g_Direction.DeleteMonster();
        g_Direction.m_bOrderExit = true;
    }
}

bool CMVP1STDirection::IsCrywolfWorldActive(int world) const
{
    return world == WD_34CRYWOLF_1ST;
}

bool CMVP1STDirection::IsSequenceReady() const
{
    return !g_Direction.m_bOrderExit;
}

void CMVP1STDirection::ResetSequence()
{
    g_Direction.DeleteMonster();
    g_Direction.m_bOrderExit = false;
    Init();
}

void CMVP1STDirection::QueueCameraMove(int x, int y, int z, float speed)
{
    g_Direction.SetNextDirectionPosition(x, y, z, speed);
}

SEASON3A::CursedTemple::SkillResult SEASON3A::CursedTemple::TryUseSkill(CHARACTER &caster,
                                                                        DWORD selectedCharacter)
{
    if (Hero->m_CursedTempleCurSkillPacket)
        return SkillResult::Ignored;
    const int skill = caster.m_CursedTempleCurSkill;
    if (skill < AT_SKILL_CURSED_TEMPLE_PRODECTION || skill > AT_SKILL_CURSED_TEMPLE_SUBLIMATION)
        return SkillResult::Ignored;
    if (skillPoints_ < SkillAttribute[skill].KillCount)
        return SkillResult::InsufficientPoints;
    if (skill == AT_SKILL_CURSED_TEMPLE_TELEPORT && m_HolyItemPlayerIndex == 0xffff)
        return SkillResult::Ignored;
    const float distance = sessionKeeper_.SkillManagerObject().GetSkillDistance(skill, &caster);
    CHARACTER *target = Hero;
    if (skill == AT_SKILL_CURSED_TEMPLE_RESTRAINT || skill == AT_SKILL_CURSED_TEMPLE_SUBLIMATION)
    {
        if (!CharactersClient.IsValidIndex(selectedCharacter))
            return SkillResult::Ignored;
        target = &CharactersClient[selectedCharacter];
        for (int i = 0; i < teamCount_; ++i)
            if (teamKeys_[i] == target->Key)
                return SkillResult::Ignored;
        if (getTargetCharacterKey(&caster, selectedCharacter) == -1 ||
            !CheckTile(&caster, &caster.Object, distance))
            return SkillResult::Ignored;
    }
    SocketClient->ToGameServer()->SendIllusionTempleSkillRequest(
        skill, static_cast<BYTE>(target->Key), distance);
    Hero->m_CursedTempleCurSkillPacket = true;
    SetAction(&caster.Object, PLAYER_ATTACK_REMOVAL);
    return SkillResult::Sent;
}

bool SEASON3A::CursedTemple::CheckInventoryHolyItem(CHARACTER *character)
{
    auto &data = *sessionKeeper_.GameData();
    const auto *picked = data.GetPickedItem();
    if (picked && picked->item->Type == ITEM_POTION + 64)
        return true;
    if (character == Hero)
        return data.HasInventoryItem(ITEM_POTION + 64);
    return m_HolyItemPlayerIndex != 0xffff && character && character->Key == m_HolyItemPlayerIndex;
}

void SessionGameplayUnit::ReceiveCursedTempRegisterSkill(const BYTE *ReceiveBuffer)
{
    auto data = (LPPMSG_CURSED_TEMPLE_USE_MAGIC_RESULT)ReceiveBuffer;

    WORD magNumber = ((WORD)(data->MagicH) << 8) + data->MagicL;

    WORD sourceobjkey = data->wSourceObjIndex;
    WORD targetobjkey = data->wTargetObjIndex;

    WORD sourceobjindex = FindCharacterIndex(sourceobjkey);
    WORD targetobjindex = FindCharacterIndex(targetobjkey);

    if (!CharactersClient.IsValidIndex(sourceobjindex) ||
        !CharactersClient.IsValidIndex(targetobjindex))
        return;

    CHARACTER *sc = &CharactersClient[sourceobjindex];
    OBJECT *sco = &sc->Object;

    CHARACTER *tc = &CharactersClient[targetobjindex];
    OBJECT *tco = &tc->Object;

    if (data->MagicResult == 0)
    {
        if (sc == Hero)
            Hero->m_CursedTempleCurSkillPacket = false;
        return;
    }

    if (sc != Hero && magNumber != AT_SKILL_TELEPORT && magNumber != AT_SKILL_TELEPORT_ALLY)
    {
        sco->Angle[2] = CreateAngle2D(sco->Position, tco->Position);
    }

    if (sc == Hero)
    {
        Hero->m_CursedTempleCurSkillPacket = false;
    }

    bool effectresult = false;

    switch (magNumber)
    {
    case AT_SKILL_CURSED_TEMPLE_PRODECTION: {
        // _buffwani_
        g_CharacterRegisterBuff(tco, eBuff_CursedTempleProdection);

        if (sc != Hero)
            SetAction(sco, PLAYER_ATTACK_REMOVAL);

        effectresult = CreateCursedTempleSkillEffect(tc, AT_SKILL_CURSED_TEMPLE_PRODECTION, 0);
    }
    break;
    case AT_SKILL_CURSED_TEMPLE_RESTRAINT: {
        tc->Movement = false;

        SetPlayerStop(tc);

        g_CharacterRegisterBuff(tco, eDeBuff_CursedTempleRestraint);

        if (sc != Hero)
            SetAction(sco, PLAYER_ATTACK_REMOVAL);

        sc->AttackTime = 1;
        SetCharacterTarget(*sc, targetobjindex);
        sc->SkillSuccess = true;
        sc->Skill = magNumber;

        effectresult = CreateCursedTempleSkillEffect(tc, AT_SKILL_CURSED_TEMPLE_RESTRAINT, 0);
    }
    break;
    case AT_SKILL_CURSED_TEMPLE_TELEPORT: {
        if (sc != Hero)
            SetAction(sco, PLAYER_ATTACK_REMOVAL);
    }
    break;
    case AT_SKILL_CURSED_TEMPLE_SUBLIMATION: {
        SetAction(tco, PLAYER_SHOCK);
        effectresult = CreateCursedTempleSkillEffect(tc, AT_SKILL_CURSED_TEMPLE_SUBLIMATION, 0);

        if (sc != Hero)
            SetAction(sco, PLAYER_ATTACK_REMOVAL);
        effectresult = CreateCursedTempleSkillEffect(sc, AT_SKILL_CURSED_TEMPLE_SUBLIMATION, 1);
    }
    break;
    }
}

void SessionGameplayUnit::ReceiveCursedTempUnRegisterSkill(const BYTE *ReceiveBuffer)
{
    auto data = (LPPMSG_CURSED_TEMPLE_SKILL_END)ReceiveBuffer;

    WORD magNumber = ((WORD)(data->MagicH) << 8) + data->MagicL;

    WORD targetobjkey = data->wObjIndex;
    WORD targetobjindex = FindCharacterIndex(targetobjkey);

    if (!CharactersClient.IsValidIndex(targetobjindex))
        return;

    CHARACTER *tc = &CharactersClient[targetobjindex];
    OBJECT *tco = &tc->Object;

    switch (magNumber)
    {
    case AT_SKILL_CURSED_TEMPLE_PRODECTION: {
        if (g_isCharacterBuff(tco, eBuff_CursedTempleProdection))
        {
            g_CharacterUnRegisterBuff(tco, eBuff_CursedTempleProdection);

            DeleteEffect(MODEL_CURSEDTEMPLE_PRODECTION_SKILL, tco);
        }
    }
    break;
    case AT_SKILL_CURSED_TEMPLE_RESTRAINT: {
        if (g_isCharacterBuff(tco, eDeBuff_CursedTempleRestraint))
        {
            g_CharacterUnRegisterBuff(tco, eDeBuff_CursedTempleRestraint);

            DeleteEffect(MODEL_CURSEDTEMPLE_RESTRAINT_SKILL, tco);
        }
    }
    break;
    }
}

// common

namespace
{
using Clock = std::chrono::steady_clock;

constexpr std::int32_t kChristmasEffectIdBase =
    static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max()) - 60;
constexpr std::int32_t kChristmasEffectIdLimit =
    static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max()) - 10;

template <std::size_t N> void CopyWideString(wchar_t (&destination)[N], const wchar_t *source)
{
    if constexpr (N == 0)
    {
        return;
    }

    if (source == nullptr)
    {
        destination[0] = L'\0';
        return;
    }

    std::wcsncpy(destination, source, N - 1);
    destination[N - 1] = L'\0';
}

} // namespace

// 크리스마스 이벤트

CXmasEvent::CXmasEvent(SessionKeeper &keeper) noexcept : SessionLegacyCalls(keeper)
{
    m_iEffectID = kChristmasEffectIdBase;
}

CXmasEvent::~CXmasEvent()
{
}

void CXmasEvent::LoadXmasEvent()
{
    gLoadData.AccessModel(MODEL_XMAS_EVENT_CHA_SSANTA, L"Data\\Skill\\", L"xmassanta");
    gLoadData.OpenTexture(MODEL_XMAS_EVENT_CHA_SSANTA, L"Skill\\");

    gLoadData.AccessModel(MODEL_XMAS_EVENT_CHA_SNOWMAN, L"Data\\Skill\\", L"xmassnowman");
    gLoadData.OpenTexture(MODEL_XMAS_EVENT_CHA_SNOWMAN, L"Skill\\");

    gLoadData.AccessModel(MODEL_XMAS_EVENT_CHA_DEER, L"Data\\Skill\\", L"xmassaum");
    gLoadData.OpenTexture(MODEL_XMAS_EVENT_CHA_DEER, L"Skill\\");

    gLoadData.AccessModel(MODEL_XMAS_EVENT_CHANGE_GIRL, L"Data\\Skill\\", L"santa");
    gLoadData.OpenTexture(MODEL_XMAS_EVENT_CHANGE_GIRL, L"Skill\\");

    gLoadData.AccessModel(MODEL_XMAS_EVENT_EARRING, L"Data\\Skill\\", L"ring");
    gLoadData.OpenTexture(MODEL_XMAS_EVENT_EARRING, L"Skill\\");

    gLoadData.AccessModel(MODEL_XMAS_EVENT_ICEHEART, L"Data\\Skill\\", L"xmaseicehart");
    gLoadData.OpenTexture(MODEL_XMAS_EVENT_ICEHEART, L"Skill\\");

    gLoadData.AccessModel(MODEL_XMAS_EVENT_BOX, L"Data\\Skill\\", L"xmasebox");
    gLoadData.OpenTexture(MODEL_XMAS_EVENT_BOX, L"Skill\\");

    gLoadData.AccessModel(MODEL_XMAS_EVENT_CANDY, L"Data\\Skill\\", L"xmasecandy");
    gLoadData.OpenTexture(MODEL_XMAS_EVENT_CANDY, L"Skill\\");

    gLoadData.AccessModel(MODEL_XMAS_EVENT_TREE, L"Data\\Skill\\", L"xmasetree");
    gLoadData.OpenTexture(MODEL_XMAS_EVENT_TREE, L"Skill\\");

    gLoadData.AccessModel(MODEL_XMAS_EVENT_SOCKS, L"Data\\Skill\\", L"xmaseyangbal");
    gLoadData.OpenTexture(MODEL_XMAS_EVENT_SOCKS, L"Skill\\");
}

void CXmasEvent::LoadXmasEventEffect()
{
    LoadBitmapW(L"Effect\\snowseff01.jpg", BITMAP_SNOW_EFFECT_1, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Clamp);
    LoadBitmapW(L"Effect\\snowseff02.jpg", BITMAP_SNOW_EFFECT_2, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Clamp);
}

void CXmasEvent::LoadXmasEventItem()
{
    gLoadData.AccessModel(MODEL_CHRISTMAS_STAR, L"Data\\Item\\", L"MagicBox", 2);
    gLoadData.OpenTexture(MODEL_CHRISTMAS_STAR, L"Item\\");
}

void CXmasEvent::LoadXmasEventSound()
{
    LoadWaveFile(SOUND_XMAS_JUMP_SNOWMAN, L"Data\\Sound\\xmasjumpsnowman.wav", 1);
    LoadWaveFile(SOUND_XMAS_JUMP_DEER, L"Data\\Sound\\xmasjumpsasum.wav", 1);
    LoadWaveFile(SOUND_XMAS_JUMP_SANTA, L"Data\\Sound\\xmasjumpsanta.wav", 1);
    LoadWaveFile(SOUND_XMAS_TURN, L"Data\\Sound\\xmasturn.wav", 1);
}

void CXmasEvent::CreateXmasEventEffect(CHARACTER *pCha, OBJECT *pObj, int iType)
{
    if (pCha->m_iTempKey >= 0)
    {
        DeleteCharacter(pCha->m_iTempKey);
    }

    GenID();

    CHARACTER *c =
        CreateCharacter(m_iEffectID, MODEL_PLAYER, pCha->PositionX, pCha->PositionY, pCha->Rot);

    pCha->m_iTempKey = m_iEffectID;
    c->Object.Scale = 0.30f;
    c->Object.SubType = iType + MODEL_XMAS_EVENT_CHA_SSANTA;

    switch (c->Object.SubType)
    {
    case MODEL_XMAS_EVENT_CHA_SSANTA:
        CopyWideString(c->ID, I18N::Game::SantaClause);
        break;
    case MODEL_XMAS_EVENT_CHA_DEER:
        CopyWideString(c->ID, I18N::Game::Rudolf);
        break;
    case MODEL_XMAS_EVENT_CHA_SNOWMAN:
        CopyWideString(c->ID, I18N::Game::Snowman);
        break;
    default:
        CopyWideString(c->ID, L"");
        break;
    }

    c->Object.m_bRenderShadow = false;
    c->Object.Owner = pObj;

    c->Object.m_dwTime = GetMillisecondsTimestamp();

    OBJECT *o = &c->Object;

    VectorCopy(pObj->Position, o->Position);
    VectorCopy(pObj->Angle, o->Angle);
    o->PriorAction = pObj->PriorAction;
    o->PriorAnimationFrame = pObj->PriorAnimationFrame;
    o->CurrentAction = pObj->CurrentAction;
    o->AnimationFrame = pObj->AnimationFrame;

    vec3_t vLight;
    Vector(0.6f, 0.6f, 0.6f, vLight);

    CreateSnowBursts(*o, vLight);

    if (o->CurrentAction == PLAYER_SANTA_2)
    {
        vec3_t vPos, vAngle, vLight;
        VectorCopy(o->Position, vPos);
        vPos[2] += 230.f;
        Vector(1.f, 1.f, 1.f, vLight);
        Vector(0.f, 0.f, 40.f, vAngle);
        CreateEffect(MODEL_XMAS_EVENT_ICEHEART, vPos, vAngle, vLight, 0, o);
        CreateParticle(BITMAP_DS_EFFECT, vPos, o->Angle, vLight, 0, 3.f, o);
        Vector(1.f, 0.f, 0.f, vLight);
        CreateParticle(BITMAP_LIGHT, vPos, o->Angle, vLight, 10, 3.f, o);
    }
}

void CXmasEvent::GenID()
{
    if (m_iEffectID >= kChristmasEffectIdLimit)
    {
        m_iEffectID = kChristmasEffectIdBase;
    }

    ++m_iEffectID;
}

CNewYearsDayEvent::CNewYearsDayEvent(SessionKeeper &keeper) noexcept
    : SessionLegacyCalls(keeper), Random(keeper.RandomForConstruction())
{
}

CNewYearsDayEvent::~CNewYearsDayEvent()
{
}

void CNewYearsDayEvent::LoadModel()
{
    gLoadData.AccessModel(MODEL_NEWYEARSDAY_EVENT_BEKSULKI, L"Data\\Monster\\", L"sulbeksulki");
    gLoadData.OpenTexture(MODEL_NEWYEARSDAY_EVENT_BEKSULKI, L"Monster\\");

    gLoadData.AccessModel(MODEL_NEWYEARSDAY_EVENT_CANDY, L"Data\\Monster\\", L"sulcandy");
    gLoadData.OpenTexture(MODEL_NEWYEARSDAY_EVENT_CANDY, L"Monster\\");

    gLoadData.AccessModel(MODEL_NEWYEARSDAY_EVENT_MONEY, L"Data\\Monster\\", L"sulgold");
    gLoadData.OpenTexture(MODEL_NEWYEARSDAY_EVENT_MONEY, L"Monster\\");

    gLoadData.AccessModel(MODEL_NEWYEARSDAY_EVENT_HOTPEPPER_GREEN, L"Data\\Monster\\",
                          L"sulgreengochu");
    gLoadData.OpenTexture(MODEL_NEWYEARSDAY_EVENT_HOTPEPPER_GREEN, L"Monster\\");

    gLoadData.AccessModel(MODEL_NEWYEARSDAY_EVENT_HOTPEPPER_RED, L"Data\\Monster\\",
                          L"sulredgochu");
    gLoadData.OpenTexture(MODEL_NEWYEARSDAY_EVENT_HOTPEPPER_RED, L"Monster\\");

    gLoadData.AccessModel(MODEL_NEWYEARSDAY_EVENT_PIG, L"Data\\Monster\\", L"sulpeg");
    gLoadData.OpenTexture(MODEL_NEWYEARSDAY_EVENT_PIG, L"Monster\\");

    gLoadData.AccessModel(MODEL_NEWYEARSDAY_EVENT_YUT, L"Data\\Monster\\", L"sulyutnulre");
    gLoadData.OpenTexture(MODEL_NEWYEARSDAY_EVENT_YUT, L"Monster\\");
}

void CNewYearsDayEvent::LoadSound()
{
    LoadWaveFile(SOUND_NEWYEARSDAY_DIE, L"Data\\Sound\\newyeardie.wav", 1);
}

CHARACTER *CNewYearsDayEvent::CreateMonster(int iType, int iPosX, int iPosY, int iKey)
{
    CHARACTER *pCharacter = nullptr;

    switch (iType)
    {
    case MONSTER_POUCH_OF_BLESSING: {
        OpenMonsterModel(MONSTER_MODEL_POUCH_OF_BLESSING);
        pCharacter = CreateCharacter(iKey, MODEL_POUCH_OF_BLESSING, iPosX, iPosY);
        if (pCharacter != nullptr)
        {
            CopyWideString(pCharacter->ID, L"Fortune Pouch");
            pCharacter->Object.Scale = 1.5f;
        }
    }
    break;
    }

    return pCharacter;
}

bool CNewYearsDayEvent::MoveMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                          WorldCharacterVisualState &visual)
{
    if (!c || !o || !b || o->Type != MODEL_POUCH_OF_BLESSING)
        return false;
    const float frames = sessionKeeper_.FrameAnimationFactor();
    const double worldTime = sessionKeeper_.FrameWorldTime();
    ObjectDrawInput presentation(o);
    visual.movement.Apply(presentation);
    AnimationPoseSample pose(presentation, b->BoneHead, b->BodyHeight, false,
                             b->PoseAssetIdentity());
    vec3_t offset{}, position, light;
    const auto sample = [&](float fraction) {
        pose.SampleBonePosition(*b, *o, 81, offset, worldTime, fraction, position);
    };
    const bool dying = visual.action == MONSTER01_DIE;
    const bool reacting = dying || visual.action == MONSTER01_SHOCK;
    if (!dying)
        visual.newYearRewardChosen = false;
    if (reacting || (visual.action >= MONSTER01_STOP1 && visual.action <= MONSTER01_ATTACK2))
    {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(frames / 3.f))
        {
            sample(birth.FrameFraction());
            Vector(1.f, 0.4f, 0.f, light);
            const float scale = dying ? 0.5f : reacting ? 0.4f : 0.28f;
            CreateParticle(BITMAP_LIGHT + 2, position, o->Angle, light, 0, scale);
            for (int i = 0; i < (reacting ? 5 : 1); ++i)
            {
                if (Random.FpsCheck(2, 1.f))
                {
                    Vector(0.8f, 0.6f, 0.1f, light);
                }
                else
                {
                    Vector(Random.Unit(), Random.Unit(), Random.Unit(), light);
                }
                CreateParticle(BITMAP_SHINY, position, o->Angle, light, 4, 0.8f);
            }
        }
    }
    if (reacting)
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(frames / 2.f))
        {
            sample(birth.FrameFraction());
            Vector(0.3f, 0.3f, 0.8f, light);
            CreateParticle(BITMAP_LIGHT + 2, position, o->Angle, light, 1, 1.f);
        }
    if (!dying || frames <= 0.f)
        return false;
    if (!visual.newYearRewardChosen)
    {
        visual.newYearRewardChosen = true;
        visual.movement.animation = Random.RangeInt(0, 5) + MODEL_NEWYEARSDAY_EVENT_BEKSULKI;
        if (Random.FpsCheck(4, 1.f))
            visual.movement.animation = MODEL_NEWYEARSDAY_EVENT_PIG;
        const float fraction =
            o->MotionTrace.FirstAnimationCrossing(worldTime, MONSTER01_DIE, 0.f).value_or(1.f);
        auto birth = sessionKeeper_.Gameplay()->EmissionTime(frames * (1.f - fraction));
        sample(fraction);
        Vector(1.f, 1.f, 1.f, light);
        CreateParticle(BITMAP_EXPLOTION, position, o->Angle, light, 0, 0.5f);
        PlayBuffer(SOUND_NEWYEARSDAY_DIE);
    }
    if (visual.animationFrame < 4.5f)
        return false;
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(frames))
    {
        sample(birth.FrameFraction());
        Vector(1.f, 1.f, 1.f, light);
        CreateEffect(MODEL_NEWYEARSDAY_EVENT_MONEY, position, o->Angle, light);
    }
    if (visual.movement.animation != 0)
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(frames / 3.f))
        {
            sample(birth.FrameFraction());
            Vector(1.f, 1.f, 1.f, light);
            CreateEffect(visual.movement.animation, position, o->Angle, light);
        }
    return false;
}

// 행운의 파란가방 이벤트

#ifdef CSK_FIX_BLUELUCKYBAG_MOVECOMMAND

const DWORD CBlueLuckyBagEvent::m_dwBlueLuckyBagCheckTime = 600000;

CBlueLuckyBagEvent::CBlueLuckyBagEvent()
{
    m_bBlueLuckyBag = false;
    m_dwBlueLuckyBagTime = 0;
}

CBlueLuckyBagEvent::~CBlueLuckyBagEvent()
{
}

void CBlueLuckyBagEvent::StartBlueLuckyBag()
{
    m_bBlueLuckyBag = true;
    m_dwBlueLuckyBagTime = timeGetTime();
}

void CBlueLuckyBagEvent::CheckTime()
{
    if (m_bBlueLuckyBag == true)
    {
        if (timeGetTime() - m_dwBlueLuckyBagTime > m_dwBlueLuckyBagCheckTime)
        {
            m_bBlueLuckyBag = false;
            m_dwBlueLuckyBagTime = 0;
        }
    }
}

bool CBlueLuckyBagEvent::IsEnableBlueLuckyBag()
{
    return m_bBlueLuckyBag;
}
#endif // CSK_FIX_BLUELUCKYBAG_MOVECOMMAND

C09SummerEvent::C09SummerEvent(SessionKeeper &keeper) noexcept : SessionLegacyCalls(keeper)
{
}

C09SummerEvent::~C09SummerEvent()
{
}

void C09SummerEvent::LoadModel()
{
}

void C09SummerEvent::LoadSound()
{
    LoadWaveFile(SOUND_UMBRELLA_MONSTER_WALK1, L"Data\\Sound\\UmbMon_Walk01.wav", 1);
    LoadWaveFile(SOUND_UMBRELLA_MONSTER_WALK2, L"Data\\Sound\\UmbMon_Walk02.wav", 1);
    LoadWaveFile(SOUND_UMBRELLA_MONSTER_DAMAGE, L"Data\\Sound\\UmbMon_Damage01.wav", 1);
    LoadWaveFile(SOUND_UMBRELLA_MONSTER_DEAD, L"Data\\Sound\\UmbMon_Dead.wav", 1);
}

CHARACTER *C09SummerEvent::CreateMonster(int iType, int iPosX, int iPosY, int iKey)
{
    CHARACTER *pCharacter = nullptr;

    if (iType == MONSTER_FIRE_FLAME_GHOST)
    {
        OpenMonsterModel(MONSTER_MODEL_FIRE_FLAME_GHOST);
        pCharacter = CreateCharacter(iKey, MODEL_FIRE_FLAME_GHOST, iPosX, iPosY);
        if (pCharacter != nullptr)
        {
            CopyWideString(pCharacter->ID, L"Initial Helper");
            pCharacter->Object.Scale = 0.8f;
            pCharacter->Object.HiddenMesh = 2;
            pCharacter->Object.m_iAnimation = 0;
        }
    }

    return pCharacter;
}

void C09SummerEvent::EmitAnimationSounds(OBJECT &object, double worldTime,
                                         WorldCharacterVisualState &visual)
{
    if (sessionKeeper_.FrameAnimationFactor() <= 0.f)
        return;
    constexpr std::array<std::pair<int, float>, 3> markers{
        {{MONSTER01_WALK, 1.f}, {MONSTER01_SHOCK, 0.5f}, {MONSTER01_DIE, 0.5f}}};
    object.MotionTrace.VisitAnimationEvents(worldTime, markers, [&](std::size_t event, float) {
        if (event == 0)
        {
            PlayBuffer(visual.movement.animation == 0 ? SOUND_UMBRELLA_MONSTER_WALK1
                                                      : SOUND_UMBRELLA_MONSTER_WALK2);
            visual.movement.animation ^= 1;
        }
        else
            PlayBuffer(event == 1 ? SOUND_UMBRELLA_MONSTER_DAMAGE : SOUND_UMBRELLA_MONSTER_DEAD);
    });
}

bool C09SummerEvent::MoveMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b, double worldTime,
                                       WorldCharacterVisualState &visual)
{
    if (o == nullptr || o->Type != MODEL_FIRE_FLAME_GHOST)
    {
        return false;
    }
    EmitAnimationSounds(*o, worldTime, visual);
    ObjectDrawInput presentation(o);
    visual.movement.Apply(presentation);

    vec3_t vRelativePos, vWorldPos;
    Vector(0.f, 0.f, 0.f, vRelativePos);
    b->TransformPosition(presentation.bones[39], vRelativePos, vWorldPos, true);

    vec3_t vLight;
    const float fLumi = (std::sin(worldTime * 0.0015f) + 1.0f) * 0.4f + 0.2f;

    Vector(0.8f, 0.4f, 0.2f, vLight);

    CreateSprite(BITMAP_LIGHT, vWorldPos, 2.0f, vLight, o);
    Vector(fLumi * 0.8f, fLumi * 0.4f, fLumi * 0.2f, vLight);
    CreateSprite(BITMAP_LIGHT, vWorldPos, 3.0f, vLight, o);

    if (visual.action != MONSTER01_DIE)
        visual.summerDeathEmitted = false;
    switch (visual.action)
    {
    case MONSTER01_DIE: {
        if (!visual.summerDeathEmitted && visual.animationFrame > 0.f &&
            sessionKeeper_.FrameAnimationFactor() > 0.f)
        {
            visual.summerDeathEmitted = true;
            const float fraction =
                o->MotionTrace.FirstAnimationCrossing(worldTime, MONSTER01_DIE, 0.f).value_or(1.f);
            auto birthTime = sessionKeeper_.Gameplay()->EmissionTime(
                sessionKeeper_.FrameAnimationFactor() * (1.f - fraction));
            vec3_t deathPosition, deathAngle;
            o->MotionTrace.Sample(worldTime, fraction, o->Position, deathPosition);
            VectorCopy(o->Angle, deathAngle);
            deathAngle[2] = o->MotionTrace.SampleYaw(worldTime, fraction, deathAngle[2]);
            CreateEffect(MODEL_EFFECT_SKURA_ITEM, deathPosition, deathAngle, visual.movement.light,
                         1, o);
            vec3_t Position, Angle, Light;
            Position[0] = deathPosition[0];
            Position[1] = deathPosition[1];
            Position[2] = RequestTerrainHeight(Position[0], Position[1]);
            Vector(0.f, 0.f, 0.f, Angle);
            Vector(1.0f, 1.0f, 1.0f, Light);
            CreateEffect(BITMAP_FIRECRACKER0001, Position, Angle, Light, 0);
            CreateEffect(MODEL_EFFECT_UMBRELLA_DIE, deathPosition, deathAngle,
                         visual.movement.light, 0, o);
            for (int i = 0; i < 40; ++i)
            {
                CreateEffect(MODEL_EFFECT_UMBRELLA_GOLD, deathPosition, deathAngle,
                             visual.movement.light, 0, o);
            }
        }
    }
    break;
    }

    return true;
}

CDuelMgr::CDuelMgr(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), g_ConsoleDebug(keeper.ConsoleDebug())
{
    Reset();
}

CDuelMgr::~CDuelMgr()
{
    Reset();
}

void CDuelMgr::Reset()
{
    m_bIsDuelEnabled = FALSE;
    m_bIsPetDuelEnabled = FALSE;
    m_DuelPlayer = {};
    m_DuelChannels = {};
    m_iCurrentChannel = -1;
    m_bRegenerated = FALSE;
    RemoveAllDuelWatchUser();
}

void CDuelMgr::EnableDuel(BOOL bEnable)
{
    m_bIsDuelEnabled = bEnable;

    if (bEnable == FALSE)
    {
        Reset();
    }
}

BOOL CDuelMgr::IsDuelEnabled()
{
    return m_bIsDuelEnabled;
}

void CDuelMgr::EnablePetDuel(BOOL bEnable)
{
    m_bIsPetDuelEnabled = bEnable;
}

BOOL CDuelMgr::IsPetDuelEnabled()
{
    return m_bIsPetDuelEnabled;
}

void CDuelMgr::SetDuelPlayer(int iPlayerNum, short sIndex, const wchar_t *pszID)
{
    m_DuelPlayer[iPlayerNum].m_sIndex = sIndex;
    if (pszID != nullptr)
    {
        wcsncpy_s(m_DuelPlayer[iPlayerNum].m_szID, pszID, MAX_USERNAME_SIZE);
    }
    else
    {
        m_DuelPlayer[iPlayerNum].m_szID[0] = L'\0';
    }
    g_ConsoleDebug.Write(MCD_NORMAL, L"[SetDuelPlayer] %d, %ls", sIndex, pszID ? pszID : L"(null)");
}

void CDuelMgr::SetHeroAsDuelPlayer(int iPlayerNum)
{
    m_DuelPlayer[iPlayerNum].m_sIndex = Hero->Key;
    wcsncpy_s(m_DuelPlayer[iPlayerNum].m_szID, Hero->ID, MAX_USERNAME_SIZE);
}

void CDuelMgr::SetScore(int iPlayerNum, int iScore)
{
    m_DuelPlayer[iPlayerNum].m_iScore = iScore;
}

void CDuelMgr::SetHP(int iPlayerNum, int iRate)
{
    m_DuelPlayer[iPlayerNum].m_fHPRate = iRate * 0.01f;
}

void CDuelMgr::SetSD(int iPlayerNum, int iRate)
{
    m_DuelPlayer[iPlayerNum].m_fSDRate = iRate * 0.01f;
}

const wchar_t *CDuelMgr::GetDuelPlayerID(int iPlayerNum) const
{
    return m_DuelPlayer[iPlayerNum].m_szID;
}

int CDuelMgr::GetScore(int iPlayerNum)
{
    return m_DuelPlayer[iPlayerNum].m_iScore;
}

float CDuelMgr::GetHP(int iPlayerNum)
{
    return m_DuelPlayer[iPlayerNum].m_fHPRate;
}

float CDuelMgr::GetSD(int iPlayerNum)
{
    return m_DuelPlayer[iPlayerNum].m_fSDRate;
}

BOOL CDuelMgr::IsDuelPlayer(CHARACTER *pCharacter, int iPlayerNum, BOOL bIncludeSummon)
{
    if (pCharacter->Key == m_DuelPlayer[iPlayerNum].m_sIndex &&
        wcsncmp(pCharacter->ID, m_DuelPlayer[iPlayerNum].m_szID, MAX_USERNAME_SIZE) == 0)
    {
        return TRUE;
    }
    else if (bIncludeSummon == TRUE && gCharacterManager.GetBaseClass(pCharacter->Class) == 0 &&
             wcsncmp(pCharacter->OwnerID, m_DuelPlayer[iPlayerNum].m_szID, MAX_USERNAME_SIZE) == 0)
    {
        return TRUE;
    }

    return FALSE;
}

BOOL CDuelMgr::IsDuelPlayer(WORD wIndex, int iPlayerNum)
{
    return (m_DuelPlayer[iPlayerNum].m_sIndex == wIndex);
}

void CDuelMgr::SetDuelChannel(int iChannelIndex, BOOL bEnable, BOOL bJoinable,
                              const wchar_t *pszID1, const wchar_t *pszID2)
{
    auto &channel = m_DuelChannels[iChannelIndex];
    channel.m_bEnable = bEnable;
    channel.m_bJoinable = bJoinable;
    if (pszID1 != nullptr)
    {
        wcsncpy_s(channel.m_szID1, pszID1, MAX_USERNAME_SIZE);
    }
    else
    {
        channel.m_szID1[0] = L'\0';
    }
    if (pszID2 != nullptr)
    {
        wcsncpy_s(channel.m_szID2, pszID2, MAX_USERNAME_SIZE);
    }
    else
    {
        channel.m_szID2[0] = L'\0';
    }
}

void CDuelMgr::RemoveAllDuelWatchUser()
{
    m_DuelWatchUserList.clear();
}

void CDuelMgr::AddDuelWatchUser(const wchar_t *pszUserID)
{
    if (pszUserID == nullptr)
    {
        return;
    }

    m_DuelWatchUserList.emplace_back(pszUserID, wcsnlen(pszUserID, MAX_USERNAME_SIZE));
}

void CDuelMgr::RemoveDuelWatchUser(const wchar_t *pszUserID)
{
    if (pszUserID == nullptr)
    {
        return;
    }

    const auto matcher = [pszUserID](const std::wstring &name) {
        return wcsncmp(name.c_str(), pszUserID, MAX_USERNAME_SIZE) == 0;
    };

    const auto iter =
        std::remove_if(m_DuelWatchUserList.begin(), m_DuelWatchUserList.end(), matcher);
    if (iter != m_DuelWatchUserList.end())
    {
        m_DuelWatchUserList.erase(iter, m_DuelWatchUserList.end());
    }
    else
    {
        assert(!L"RemoveDuelWatchUser!");
    }
}

const wchar_t *CDuelMgr::GetDuelWatchUser(int iIndex) const
{
    if (iIndex < 0 || static_cast<std::size_t>(iIndex) >= m_DuelWatchUserList.size())
    {
        return nullptr;
    }

    return m_DuelWatchUserList[static_cast<std::size_t>(iIndex)].c_str();
}

namespace MUHelper::Combat
{
namespace
{
void AddDensity(std::vector<int> &density, int width, int x, int y)
{
    for (int dy = -ConcentrationRadius; dy <= ConcentrationRadius; ++dy)
    {
        for (int dx = -ConcentrationRadius; dx <= ConcentrationRadius; ++dx)
        {
            if (dx * dx + dy * dy <= ConcentrationRadius * ConcentrationRadius)
                ++density[(y + dy) * width + x + dx];
        }
    }
}

int SelectConcentrated(std::span<const HuntingTarget> targets, std::vector<int> &density)
{
    int minX = targets.front().x, maxX = minX;
    int minY = targets.front().y, maxY = minY;
    for (const auto &target : targets)
    {
        minX = std::min(minX, target.x);
        maxX = std::max(maxX, target.x);
        minY = std::min(minY, target.y);
        maxY = std::max(maxY, target.y);
    }
    const int width = maxX - minX + 1 + 2 * ConcentrationRadius;
    const int height = maxY - minY + 1 + 2 * ConcentrationRadius;
    // Bounded-radius stamps avoid comparing every monster with every other one.
    // The session-owned vectors retain capacity between target acquisitions.
    density.assign(static_cast<std::size_t>(width) * height, 0);
    const int originX = minX - ConcentrationRadius;
    const int originY = minY - ConcentrationRadius;
    for (const auto &target : targets)
        AddDensity(density, width, target.x - originX, target.y - originY);

    int selected = -1, largestGroup = 0;
    int nearest = std::numeric_limits<int>::max();
    for (const auto &target : targets)
    {
        const int group = density[(target.y - originY) * width + target.x - originX];
        if (group > largestGroup || (group == largestGroup && target.distance <= nearest))
        {
            selected = target.id;
            largestGroup = group;
            nearest = target.distance;
        }
    }
    return selected;
}
} // namespace

int SelectTarget(std::span<const HuntingTarget> targets, bool concentrated,
                 std::vector<int> &density)
{
    if (targets.empty())
        return -1;
    if (concentrated)
        return SelectConcentrated(targets, density);
    int selected = -1;
    int nearest = std::numeric_limits<int>::max();
    for (const auto &target : targets)
    {
        if (target.distance <= nearest)
        {
            selected = target.id;
            nearest = target.distance;
        }
    }
    return selected;
}

} // namespace MUHelper::Combat

// Includes mirror ZzzInterface.cpp, the unit this was extracted from.

bool SessionGameplayUnit::CheckTarget(CHARACTER *c)
{
    if (CharactersClient.IsValidIndex(SelectedCharacter))
    {
        TargetX = (int)(CharactersClient[SelectedCharacter].Object.Position[0] / TERRAIN_SCALE);
        TargetY = (int)(CharactersClient[SelectedCharacter].Object.Position[1] / TERRAIN_SCALE);
        VectorCopy(CharactersClient[SelectedCharacter].Object.Position, c->TargetPosition);
        return true;
    }
    else
    {
        RenderTerrain(true);
        if (SelectFlag)
        {
            VectorCopy(CollisionPosition, c->TargetPosition);
            TargetX = (int)(c->TargetPosition[0] / TERRAIN_SCALE);
            TargetY = (int)(c->TargetPosition[1] / TERRAIN_SCALE);
            return true;
        }
    }
    return false;
}

bool SessionGameplayUnit::CheckMovementSkillTarget(CHARACTER *c)
{
    const int targetIndex = g_MovementSkill.m_iTarget;
    if (targetIndex == -1)
    {
        if (CheckTarget(c))
            return true;
    }
    else if (CharactersClient.IsValidIndex(targetIndex))
    {
        CHARACTER *target = &CharactersClient[targetIndex];
        if (target != c && target->Dead == 0)
        {
            TargetX = (int)(target->Object.Position[0] / TERRAIN_SCALE);
            TargetY = (int)(target->Object.Position[1] / TERRAIN_SCALE);
            VectorCopy(target->Object.Position, c->TargetPosition);
            return true;
        }
    }

    TargetX = c->PositionX;
    TargetY = c->PositionY;
    VectorCopy(c->Object.Position, c->TargetPosition);
    return false;
}

using namespace GameLogic::Combat;

bool SessionGameplayUnit::CastWarriorSkill(CHARACTER *c, OBJECT *o, ITEM *p, ActionSkillType iSkill)
{
    if (c == NULL)
        return false;
    if (o == NULL)
        return false;
    //if (p == NULL)	return false;
    bool Success = false;

    if (!CharactersClient.IsValidIndex(SelectedCharacter))
    {
        return false;
    }

    TargetX = (int)(CharactersClient[SelectedCharacter].Object.Position[0] / TERRAIN_SCALE);
    TargetY = (int)(CharactersClient[SelectedCharacter].Object.Position[1] / TERRAIN_SCALE);

    g_MovementSkill.m_bMagic = FALSE;
    g_MovementSkill.m_iSkill = iSkill;
    g_MovementSkill.m_iTarget = SelectedCharacter;
    float Distance = gSkillManager.GetSkillDistance(iSkill, c) * 1.2f;

    if ((gMapManager.InBloodCastle() == true) &&
        ((iSkill >= AT_SKILL_FALLING_SLASH && iSkill <= AT_SKILL_SLASH) ||
         iSkill == AT_SKILL_FALLING_SLASH_STR || iSkill == AT_SKILL_LUNGE_STR ||
         iSkill == AT_SKILL_CYCLONE_STR || iSkill == AT_SKILL_CYCLONE_STR_MG ||
         iSkill == AT_SKILL_SLASH_STR))
    {
        Distance = 1.8f;
    }

    if (CheckTile(c, o, Distance))
    {
        UseSkillWarrior(c, o);
        Success = true;
    }
    else if (PathFinding2(c->PositionX, c->PositionY, TargetX, TargetY, &c->Path, Distance))
    {
        c->Movement = true;
        c->MovementType = MOVEMENT_SKILL;
        SendMove(c, o);
    }

    return (Success);
}

bool SessionGameplayUnit::SkillWarrior(CHARACTER *c, ITEM *p)
{
    OBJECT *o = &c->Object;
    if (o->Type == MODEL_PLAYER)
    {
        if (o->CurrentAction == PLAYER_DEFENSE1)
            return false;
        if (o->CurrentAction >= PLAYER_ATTACK_SKILL_SWORD1 &&
                o->CurrentAction <= PLAYER_ATTACK_SKILL_SWORD4 ||
            o->CurrentAction == PLAYER_ATTACK_TWO_HAND_SWORD_TWO)
            return false;
    }
    else
    {
        if (o->CurrentAction >= MONSTER01_ATTACK1 && o->CurrentAction <= MONSTER01_ATTACK2)
            return false;
    }
    auto Skill = CharacterAttribute->Skill[g_MovementSkill.m_iSkill];
    if (Skill == AT_SKILL_RIDER || Skill == AT_SKILL_FIRE_SLASH ||
        Skill == AT_SKILL_FIRE_SLASH_STR || Skill == AT_SKILL_TWISTING_SLASH ||
        Skill == AT_SKILL_TWISTING_SLASH_STR || Skill == AT_SKILL_TWISTING_SLASH_STR_MG ||
        Skill == AT_SKILL_TWISTING_SLASH_MASTERY || Skill == AT_SKILL_DEATHSTAB ||
        Skill == AT_SKILL_DEATHSTAB_STR ||
        (Skill == AT_SKILL_IMPALE && (Hero->Helper.Type == MODEL_HORN_OF_UNIRIA ||
                                      Hero->Helper.Type == MODEL_HORN_OF_DINORANT ||
                                      Hero->Helper.Type == MODEL_DARK_HORSE_ITEM ||
                                      Hero->Helper.Type == MODEL_HORN_OF_FENRIR)) ||
        Skill == AT_SKILL_FORCE || Skill == AT_SKILL_FORCE_WAVE ||
        Skill == AT_SKILL_FORCE_WAVE_STR || Skill == AT_SKILL_FIREBURST ||
        Skill == AT_SKILL_FIREBURST_STR || Skill == AT_SKILL_FIREBURST_MASTERY ||
        Skill == AT_SKILL_RUSH || Skill == AT_SKILL_SPIRAL_SLASH || Skill == AT_SKILL_SPACE_SPLIT)
    {
        switch (Skill)
        {
        case AT_SKILL_IMPALE:
            if (!(o->Type == MODEL_PLAYER && Hero->Weapon[0].Type != -1 &&
                  (Hero->Weapon[0].Type >= MODEL_SPEAR &&
                   Hero->Weapon[0].Type < MODEL_SPEAR + MAX_ITEM_INDEX)))
            {
                return false;
            }
            break;
        case AT_SKILL_DEATHSTAB:
        case AT_SKILL_DEATHSTAB_STR:
            if (!(Hero->Weapon[0].Type != -1 &&
                  (Hero->Weapon[0].Type < MODEL_STAFF ||
                   Hero->Weapon[0].Type >= MODEL_STAFF + MAX_ITEM_INDEX)))
            {
                return false;
            }
            break;
        case AT_SKILL_SPIRAL_SLASH:
            if (Hero->Weapon[0].Type < MODEL_SWORD ||
                Hero->Weapon[0].Type >= MODEL_SWORD + MAX_ITEM_INDEX)
            {
                return false;
            }
            break;
        }
        int iMana, iSkillMana;
        gSkillManager.GetSkillInformation(Skill, 1, NULL, &iMana, NULL, &iSkillMana);
        if (CharacterAttribute->Mana < iMana)
        {
            int Index = sessionKeeper_.GameData()->FindManaItemIndex();

            if (Index != -1)
            {
                SendRequestUse(Index, 0);
            }
            return false;
        }
        if (iSkillMana > CharacterAttribute->SkillMana)
        {
            return false;
        }

        if (!gSkillManager.CheckSkillDelay(g_MovementSkill.m_iSkill))
        {
            return false;
        }
        if (CheckAttack())
        {
            return (CastWarriorSkill(c, o, p, Skill));
        }
    }

    auto baseSkill = gSkillManager.MasterSkillToBaseSkillIndex(Skill);
    bool Success = false;
    for (int i = 0; i < p->SpecialNum; i++)
    {
        if (baseSkill == p->Special[i]) // current skill is available as weapon skill?
        {
            int iMana;
            gSkillManager.GetSkillInformation(Skill, 1, NULL, &iMana, NULL);
            if (CharacterAttribute->Mana < iMana)
            {
                int Index = sessionKeeper_.GameData()->FindManaItemIndex();

                if (Index != -1)
                {
                    SendRequestUse(Index, 0);
                }
                continue;
            }

            if (!gSkillManager.CheckSkillDelay(Hero->CurrentSkill))
            {
                continue;
            }

            switch (Skill)
            {
            case AT_SKILL_BLOCKING:
                c->Movement = false;
                if (o->Type == MODEL_PLAYER)
                    SetAction(o, PLAYER_DEFENSE1);
                else
                    SetPlayerAttack(c);
                SendRequestMagic(Skill, Hero->Key);
                Success = true;
                break;
            case AT_SKILL_FALLING_SLASH:
            case AT_SKILL_FALLING_SLASH_STR:
            case AT_SKILL_LUNGE:
            case AT_SKILL_LUNGE_STR:
            case AT_SKILL_UPPERCUT:
            case AT_SKILL_CYCLONE:
            case AT_SKILL_CYCLONE_STR:
            case AT_SKILL_CYCLONE_STR_MG:
            case AT_SKILL_SLASH:
            case AT_SKILL_SLASH_STR:
            case AT_SKILL_RIDER:
                if (CheckAttack())
                    Success = CastWarriorSkill(c, o, p, Skill);
                break;
            }
        }
    }

    if (Skill == AT_SKILL_FIRE_SCREAM || Skill == AT_SKILL_FIRE_SCREAM_STR ||
        Skill == AT_SKILL_CHAOTIC_DISEIER)
    {
        int iMana;
        gSkillManager.GetSkillInformation(Skill, 1, nullptr, &iMana, nullptr);
        if (CharacterAttribute->Mana < iMana)
        {
            int Index = sessionKeeper_.GameData()->FindManaItemIndex();

            if (Index != -1)
            {
                SendRequestUse(Index, 0);
            }
            return Success;
        }

        float distance = gSkillManager.GetSkillDistance(Skill, c);
        if (CheckTile(c, o, distance))
        {
            o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));
            WORD TKey = 0xffff;

            if (g_MovementSkill.m_iTarget != -1)
            {
                TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
            }
        }
    }

    return Success;
}

void SessionGameplayUnit::UseSkillWarrior(CHARACTER *c, OBJECT *o)
{
    auto Skill = g_MovementSkill.m_bMagic ? CharacterAttribute->Skill[g_MovementSkill.m_iSkill]
                                          : static_cast<ActionSkillType>(g_MovementSkill.m_iSkill);
    LetHeroStop();
    c->Movement = false;
    if (o->Type == MODEL_PLAYER)
    {
        SetAttackSpeed();

        switch (Skill)
        {
        case AT_SKILL_IMPALE:
            if (c->Helper.Type == MODEL_HORN_OF_FENRIR)
                SetAction(o, PLAYER_FENRIR_ATTACK_SPEAR);
            else
                SetAction(o, PLAYER_ATTACK_SKILL_SPEAR);
            break;
        case AT_SKILL_DEATHSTAB:
        case AT_SKILL_DEATHSTAB_STR:
            SetAction(o, PLAYER_ATTACK_DEATHSTAB);
            break;
        case AT_SKILL_TWISTING_SLASH:
        case AT_SKILL_TWISTING_SLASH_STR:
        case AT_SKILL_TWISTING_SLASH_STR_MG:
        case AT_SKILL_TWISTING_SLASH_MASTERY:
        case AT_SKILL_FIRE_SLASH:
        case AT_SKILL_FIRE_SLASH_STR:
            SetAction(o, PLAYER_ATTACK_SKILL_WHEEL);
            break;
        case AT_SKILL_RIDER:
            if (gMapManager.ContextMap() == WD_8TARKAN || gMapManager.ContextMap() == WD_10HEAVEN ||
                g_Direction.m_CKanturu.IsMayaScene(gMapManager.ContextMap()))
                SetAction(o, PLAYER_SKILL_RIDER_FLY);
            else
                SetAction(o, PLAYER_SKILL_RIDER);
            break;
        case AT_SKILL_FIRE_SCREAM:
        case AT_SKILL_FIRE_SCREAM_STR:
            break;
        case AT_SKILL_CHAOTIC_DISEIER:
            break;
        case AT_SKILL_FORCE:
        case AT_SKILL_FORCE_WAVE:
        case AT_SKILL_FORCE_WAVE_STR:
        case AT_SKILL_FIREBURST:
        case AT_SKILL_FIREBURST_STR:
        case AT_SKILL_FIREBURST_MASTERY:
        case AT_SKILL_SPACE_SPLIT:
            break;
        case AT_SKILL_RUSH:
            SetAction(o, PLAYER_ATTACK_RUSH);
            break;
        default:
            if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone)
            {
                SetAction(o, PLAYER_FENRIR_ATTACK_MAGIC);
            }
            else
            {
                auto baseSkill = gSkillManager.MasterSkillToBaseSkillIndex(Skill);
                SetAction(o, static_cast<int>(PLAYER_ATTACK_SKILL_SWORD1) + baseSkill -
                                 AT_SKILL_FALLING_SLASH);
            }
            break;
        }
    }
    else
    {
        SetPlayerAttack(c);
    }

    vec3_t Light;
    Vector(1.f, 1.f, 1.f, Light);

    if (Skill != AT_SKILL_FORCE && Skill != AT_SKILL_FORCE_WAVE &&
        Skill != AT_SKILL_FORCE_WAVE_STR && Skill != AT_SKILL_FIREBURST &&
        Skill != AT_SKILL_FIREBURST_STR && Skill != AT_SKILL_FIREBURST_MASTERY &&
        Skill != AT_SKILL_SPACE_SPLIT && Skill != AT_SKILL_FIRE_SCREAM &&
        Skill != AT_SKILL_FIRE_SCREAM_STR && Skill != AT_SKILL_KILLING_BLOW &&
        Skill != AT_SKILL_KILLING_BLOW_STR && Skill != AT_SKILL_KILLING_BLOW_MASTERY &&
        Skill != AT_SKILL_BEAST_UPPERCUT && Skill != AT_SKILL_BEAST_UPPERCUT_STR &&
        Skill != AT_SKILL_BEAST_UPPERCUT_MASTERY)
    {
        CreateParticle(BITMAP_SHINY + 2, o->Position, o->Angle, Light, 0, 0.f, o);
        PlayBuffer(static_cast<ESound>(SOUND_BRANDISH_SWORD01 + rand() % 2));
    }

    if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget))
    {
        VectorCopy(CharactersClient[g_MovementSkill.m_iTarget].Object.Position, c->TargetPosition);
        o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));
    }

    if (Skill != AT_SKILL_CHAOTIC_DISEIER)
    {
        WORD TKey = 0xffff;
        if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget))
        {
            TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
        }

        if (Skill == AT_SKILL_TWISTING_SLASH || Skill == AT_SKILL_TWISTING_SLASH_STR ||
            Skill == AT_SKILL_TWISTING_SLASH_STR_MG || Skill == AT_SKILL_TWISTING_SLASH_MASTERY ||
            Skill == AT_SKILL_FIRE_SLASH || Skill == AT_SKILL_FIRE_SLASH_STR)
        {
            SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                     (BYTE)(o->Angle[2] / 360.f * 256.f), 0, 0, TKey, 0);
        }
        else
        {
            SendRequestMagic(Skill, TKey);
        }
    }

    if (((!g_isCharacterBuff(o, eDeBuff_Harden)) && c->Helper.Type != MODEL_DARK_HORSE_ITEM) &&
        Skill != AT_SKILL_FIRE_SCREAM && Skill != AT_SKILL_FIRE_SCREAM_STR)
    {
        BYTE positionX = (BYTE)(c->TargetPosition[0] / TERRAIN_SCALE);
        BYTE positionY = (BYTE)(c->TargetPosition[1] / TERRAIN_SCALE);

        if ((gMapManager.InBloodCastle() == true) || Skill == AT_SKILL_FORCE ||
            Skill == AT_SKILL_FORCE_WAVE || Skill == AT_SKILL_FORCE_WAVE_STR ||
            Skill == AT_SKILL_SPIRAL_SLASH || Skill == AT_SKILL_RUSH ||
            CharactersClient[g_MovementSkill.m_iTarget].MonsterIndex == MONSTER_CASTLE_GATE1 ||
            CharactersClient[g_MovementSkill.m_iTarget].MonsterIndex == MONSTER_GUARDIAN_STATUE ||
            CharactersClient[g_MovementSkill.m_iTarget].MonsterIndex == MONSTER_LIFE_STONE ||
            CharactersClient[g_MovementSkill.m_iTarget].MonsterIndex == MONSTER_CANON_TOWER)
        {
            int angle = abs((int)(o->Angle[2] / 45.f));
            switch (angle)
            {
            case 0:
                positionY++;
                break;
            case 1:
                positionX--;
                positionY++;
                break;
            case 2:
                positionX--;
                break;
            case 3:
                positionX--;
                positionY--;
                break;
            case 4:
                positionY--;
                break;
            case 5:
                positionX++;
                positionY--;
                break;
            case 6:
                positionX++;
                break;
            case 7:
                positionX++;
                positionY++;
                break;
            }
        }

#ifdef SEND_POSITION_TO_SERVER
        int TargetIndex = TERRAIN_INDEX(positionX, positionY);

        if ((TerrainWall[TargetIndex] & TW_NOMOVE) != TW_NOMOVE &&
            (TerrainWall[TargetIndex] & TW_NOGROUND) != TW_NOGROUND)
        {
            if (Skill != AT_SKILL_IMPALE && Skill != AT_SKILL_DEATHSTAB &&
                Skill != AT_SKILL_DEATHSTAB_STR && Skill != AT_SKILL_RIDER &&
                Skill != AT_SKILL_FORCE && Skill != AT_SKILL_FORCE_WAVE &&
                Skill != AT_SKILL_FORCE_WAVE_STR && Skill != AT_SKILL_FIREBURST &&
                Skill != AT_SKILL_FIREBURST_STR && Skill != AT_SKILL_FIREBURST_MASTERY &&
                Skill != AT_SKILL_SPACE_SPLIT)
            {
                SocketClient->ToGameServer()->SendInstantMoveRequest(positionX, positionY);
            }
        }
#endif
    }

    c->SkillSuccess = true;
}

void SessionGameplayUnit::UseSkillWizard(CHARACTER *c, OBJECT *o)
{
    auto Skill = CharacterAttribute->Skill[g_MovementSkill.m_iSkill];

    switch (Skill)
    {
    case AT_SKILL_IMPALE:
    case AT_SKILL_DEATHSTAB:
    case AT_SKILL_DEATHSTAB_STR:
        return;
    }

    if (Skill == AT_SKILL_DEATH_CANNON)
    {
        if (Hero->Weapon[0].Type < MODEL_STAFF ||
            Hero->Weapon[0].Type >= MODEL_STAFF + MAX_ITEM_INDEX)
            return;
    }

    if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget))
    {
        VectorCopy(CharactersClient[g_MovementSkill.m_iTarget].Object.Position, c->TargetPosition);
        o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));
    }

    WORD TKey = 0xffff;
    if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget))
    {
        TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
    }

    switch (Skill)
    {
    case AT_SKILL_POISON:
    case AT_SKILL_POISON_STR:
    case AT_SKILL_METEO:
    case AT_SKILL_LIGHTNING:
    case AT_SKILL_LIGHTNING_STR:
    case AT_SKILL_LIGHTNING_STR_MG:
    case AT_SKILL_ENERGYBALL:
    case AT_SKILL_POWERWAVE:
    case AT_SKILL_ICE:
    case AT_SKILL_ICE_STR:
    case AT_SKILL_ICE_STR_MG:
    case AT_SKILL_FIREBALL:
    case AT_SKILL_JAVELIN:
        SendRequestMagic(Skill, TKey);
        SetPlayerMagic(c);
        LetHeroStop();
        break;
    case AT_SKILL_DEATH_CANNON:
        SendRequestMagic(Skill, TKey);
        SetAction(o, PLAYER_ATTACK_DEATH_CANNON);
        SetAttackSpeed();
        LetHeroStop();
        break;
    case AT_SKILL_BLAST:
    case AT_SKILL_BLAST_STR:
    case AT_SKILL_BLAST_STR_MG: {
        SendRequestMagicContinue(Skill, (int)(c->TargetPosition[0] / 100.f),
                                 (int)(c->TargetPosition[1] / 100.f),
                                 (BYTE)(o->Angle[2] / 360.f * 256.f), 0, 0, TKey, 0);
    }
        SetPlayerMagic(c);
        LetHeroStop();
        break;
    }

    c->SkillSuccess = true;
}

void SessionGameplayUnit::UseSkillElf(CHARACTER *c, OBJECT *o)
{
    LetHeroStop();
    int Skill = CharacterAttribute->Skill[g_MovementSkill.m_iSkill];

    if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget))
    {
        VectorCopy(CharactersClient[g_MovementSkill.m_iTarget].Object.Position, c->TargetPosition);
        o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));
    }

    WORD TKey = 0xffff;
    if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget))
    {
        TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
    }

    switch (Skill)
    {
    case AT_SKILL_HEALING:
    case AT_SKILL_HEALING_STR:
    case AT_SKILL_ATTACK:
    case AT_SKILL_ATTACK_STR:
    case AT_SKILL_RECOVER:
    case AT_SKILL_DEFENSE:
    case AT_SKILL_DEFENSE_STR:
    case AT_SKILL_DEFENSE_MASTERY:
        SendRequestMagic(Skill, TKey);
        SetPlayerMagic(c);
        break;

    case AT_SKILL_ICE_ARROW:
    case AT_SKILL_ICE_ARROW_STR: {
        WORD Dexterity;
        const WORD notDexterity = 646;
        Dexterity = CharacterAttribute->Dexterity + CharacterAttribute->AddDexterity;
        if (Dexterity < notDexterity)
        {
            break;
        }
        if (!CheckArrow())
            break;
        SendRequestMagic(Skill, TKey);
        SetPlayerAttack(c);
    }
    break;

    case AT_SKILL_DEEPIMPACT:
        if (!CheckArrow())
            break;
        SendRequestMagic(Skill, TKey);
        SetPlayerHighBowAttack(c);
        break;
    }

    c->SkillSuccess = true;
}

void SessionGameplayUnit::UseSkillSummon(CHARACTER *pCha, OBJECT *pObj)
{
    int iSkill = CharacterAttribute->Skill[g_MovementSkill.m_iSkill];

    switch (iSkill)
    {
    case AT_SKILL_ALICE_DRAINLIFE:
    case AT_SKILL_ALICE_DRAINLIFE_STR:
    case AT_SKILL_ALICE_LIGHTNINGORB: {
        LetHeroStop();
        if (iSkill == AT_SKILL_ALICE_DRAINLIFE || iSkill == AT_SKILL_ALICE_DRAINLIFE_STR)
        {
            switch (pCha->Helper.Type)
            {
            case MODEL_HORN_OF_UNIRIA:
                SetAction(pObj, PLAYER_SKILL_DRAIN_LIFE_UNI);
                break;
            case MODEL_HORN_OF_DINORANT:
                SetAction(pObj, PLAYER_SKILL_DRAIN_LIFE_DINO);
                break;
            case MODEL_HORN_OF_FENRIR:
                SetAction(pObj, PLAYER_SKILL_DRAIN_LIFE_FENRIR);
                break;
            default:
                SetAction(pObj, PLAYER_SKILL_DRAIN_LIFE);
                break;
            }
        }
        else if (iSkill == AT_SKILL_ALICE_LIGHTNINGORB)
        {
            switch (pCha->Helper.Type)
            {
            case MODEL_HORN_OF_UNIRIA:
                SetAction(pObj, PLAYER_SKILL_LIGHTNING_ORB_UNI);
                break;
            case MODEL_HORN_OF_DINORANT:
                SetAction(pObj, PLAYER_SKILL_LIGHTNING_ORB_DINO);
                break;
            case MODEL_HORN_OF_FENRIR:
                SetAction(pObj, PLAYER_SKILL_LIGHTNING_ORB_FENRIR);
                break;
            default:
                SetAction(pObj, PLAYER_SKILL_LIGHTNING_ORB);
                break;
            }
        }

        if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget))
        {
            WORD wTargetKey = CharactersClient[g_MovementSkill.m_iTarget].Key;
            SendRequestMagicContinue(iSkill, (int)(pCha->TargetPosition[0] / 100.f),
                                     (int)(pCha->TargetPosition[1] / 100.f),
                                     (BYTE)(pObj->Angle[2] / 360.f * 256.f), 0, 0, wTargetKey, 0);
        }
    }
    break;
    case AT_SKILL_ALICE_CHAINLIGHTNING:
    case AT_SKILL_ALICE_CHAINLIGHTNING_STR: {
        LetHeroStop();

        switch (pCha->Helper.Type)
        {
        case MODEL_HORN_OF_UNIRIA:
            SetAction(pObj, PLAYER_SKILL_CHAIN_LIGHTNING_UNI);
            break;
        case MODEL_HORN_OF_DINORANT:
            SetAction(pObj, PLAYER_SKILL_CHAIN_LIGHTNING_DINO);
            break;
        case MODEL_HORN_OF_FENRIR:
            SetAction(pObj, PLAYER_SKILL_CHAIN_LIGHTNING_FENRIR);
            break;
        default:
            SetAction(pObj, PLAYER_SKILL_CHAIN_LIGHTNING);
            break;
        }

        if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget))
        {
            WORD wTargetKey = CharactersClient[g_MovementSkill.m_iTarget].Key;

            SendRequestMagicContinue(iSkill, (int)(pCha->TargetPosition[0] / 100.f),
                                     (int)(pCha->TargetPosition[1] / 100.f),
                                     (BYTE)(pObj->Angle[2] / 360.f * 256.f), 0, 0, wTargetKey, 0);
        }
    }
    break;
    case AT_SKILL_ALICE_SLEEP:
    case AT_SKILL_ALICE_SLEEP_STR:
    case AT_SKILL_ALICE_BLIND:
    case AT_SKILL_ALICE_THORNS: {
        LetHeroStop();

        if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget))
        {
            VectorCopy(CharactersClient[g_MovementSkill.m_iTarget].Object.Position,
                       pCha->TargetPosition);
            pObj->SetAngleZ(CreateAngle2D(pObj->Position, pCha->TargetPosition));

            WORD wTargetKey = CharactersClient[g_MovementSkill.m_iTarget].Key;
            SendRequestMagic(iSkill, wTargetKey);
        }
    }
    break;
    case AT_SKILL_ALICE_BERSERKER:
    case AT_SKILL_ALICE_BERSERKER_STR:
        LetHeroStop();
        switch (pCha->Helper.Type)
        {
        case MODEL_HORN_OF_UNIRIA:
            SetAction(pObj, PLAYER_SKILL_SLEEP_UNI);
            break;
        case MODEL_HORN_OF_DINORANT:
            SetAction(pObj, PLAYER_SKILL_SLEEP_DINO);
            break;
        case MODEL_HORN_OF_FENRIR:
            SetAction(pObj, PLAYER_SKILL_SLEEP_FENRIR);
            break;
        default:
            SetAction(pObj, PLAYER_SKILL_SLEEP);
            break;
        }
        SendRequestMagic(iSkill, HeroKey);
        break;
    case AT_SKILL_ALICE_WEAKNESS:
    case AT_SKILL_ALICE_ENERVATION:
        LetHeroStop();
        SendRequestMagicContinue(iSkill, pCha->PositionX, pCha->PositionY,
                                 (BYTE)(pObj->Angle[2] / 360.f * 256.f), 0, 0, 0xffff, 0);
        switch (pCha->Helper.Type)
        {
        case MODEL_HORN_OF_UNIRIA:
            SetAction(pObj, PLAYER_SKILL_SLEEP_UNI);
            break;
        case MODEL_HORN_OF_DINORANT:
            SetAction(pObj, PLAYER_SKILL_SLEEP_DINO);
            break;
        case MODEL_HORN_OF_FENRIR:
            SetAction(pObj, PLAYER_SKILL_SLEEP_FENRIR);
            break;
        default:
            SetAction(pObj, PLAYER_SKILL_SLEEP);
            break;
        }
        break;
    }

    pCha->SkillSuccess = true;
}

void SessionGameplayUnit::UseSkillRagefighter(CHARACTER *pCha, OBJECT *pObj)
{
    int iSkill = g_MovementSkill.m_bMagic ? CharacterAttribute->Skill[g_MovementSkill.m_iSkill]
                                          : g_MovementSkill.m_iSkill;

    ITEM *pLeftRing = &CharacterMachine->Equipment[EQUIPMENT_RING_LEFT];
    ITEM *pRightRing = &CharacterMachine->Equipment[EQUIPMENT_RING_RIGHT];

    if (g_CMonkSystem.IsChangeringNotUseSkill(pLeftRing->Type, pRightRing->Type, pLeftRing->Level,
                                              pRightRing->Level))
        return;

    if (g_CMonkSystem.IsRideNotUseSkill(iSkill, pCha->Helper.Type))
        return;

    if (!g_CMonkSystem.IsSwordformGlovesUseSkill(iSkill))
        return;

    LetHeroStop();
    pCha->Movement = false;

    if (pObj->Type == MODEL_PLAYER)
    {
        g_CMonkSystem.SetRageSkillAni(iSkill, *pCha);
        SetAttackSpeed();
    }

    switch (iSkill)
    {
    case AT_SKILL_KILLING_BLOW:
    case AT_SKILL_KILLING_BLOW_STR:
    case AT_SKILL_KILLING_BLOW_MASTERY:
    case AT_SKILL_OCCUPY: {
        WORD wTargetKey = 0;
        if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget))
        {
            wTargetKey = CharactersClient[g_MovementSkill.m_iTarget].Key;
            VectorCopy(CharactersClient[g_MovementSkill.m_iTarget].Object.Position,
                       pCha->TargetPosition);
            pObj->SetAngleZ(CreateAngle2D(pObj->Position, pCha->TargetPosition));
        }
        SendRequestMagic(iSkill, wTargetKey);

        BYTE TargetPosX = (BYTE)(pCha->TargetPosition[0] / TERRAIN_SCALE);
        BYTE TargetPosY = (BYTE)(pCha->TargetPosition[1] / TERRAIN_SCALE);

        if ((gMapManager.InBloodCastle()) || iSkill == AT_SKILL_OCCUPY ||
            (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget) &&
             (CharactersClient[g_MovementSkill.m_iTarget].MonsterIndex == MONSTER_CASTLE_GATE1 ||
              CharactersClient[g_MovementSkill.m_iTarget].MonsterIndex == MONSTER_GUARDIAN_STATUE ||
              CharactersClient[g_MovementSkill.m_iTarget].MonsterIndex == MONSTER_LIFE_STONE ||
              CharactersClient[g_MovementSkill.m_iTarget].MonsterIndex == MONSTER_CANON_TOWER)))
        {
            int angle = abs((int)(pObj->Angle[2] / 45.f));
            switch (angle)
            {
            case 0:
                TargetPosY++;
                break;
            case 1:
                TargetPosX--;
                TargetPosY++;
                break;
            case 2:
                TargetPosX--;
                break;
            case 3:
                TargetPosX--;
                TargetPosY--;
                break;
            case 4:
                TargetPosY--;
                break;
            case 5:
                TargetPosX++;
                TargetPosY--;
                break;
            case 6:
                TargetPosX++;
                break;
            case 7:
                TargetPosX++;
                TargetPosY++;
                break;
            }
        }

        int TargetIndex = TERRAIN_INDEX(TargetPosX, TargetPosY);

        vec3_t vDis;
        Vector(0.0f, 0.0f, 0.0f, vDis);
        VectorSubtract(pCha->TargetPosition, pCha->Object.Position, vDis);
        VectorNormalize(vDis);
        VectorScale(vDis, TERRAIN_SCALE, vDis);
        VectorSubtract(pCha->TargetPosition, vDis, vDis);
        BYTE CharPosX = (BYTE)(vDis[0] / TERRAIN_SCALE);
        BYTE CharPosY = (BYTE)(vDis[1] / TERRAIN_SCALE);

#ifdef SEND_POSITION_TO_SERVER
        if ((TerrainWall[TargetIndex] & TW_NOMOVE) != TW_NOMOVE &&
            (TerrainWall[TargetIndex] & TW_NOGROUND) != TW_NOGROUND)
        {
            SocketClient->ToGameServer()->SendInstantMoveRequest(CharPosX, CharPosY);
        }
#endif

        pObj->m_sTargetIndex = g_MovementSkill.m_iTarget;
        g_CMonkSystem.RageCreateEffect(pObj, iSkill);
    }
    break;
    case AT_SKILL_BEAST_UPPERCUT:
    case AT_SKILL_BEAST_UPPERCUT_STR:
    case AT_SKILL_BEAST_UPPERCUT_MASTERY: {
        WORD wTargetKey = 0;
        if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget))
        {
            wTargetKey = CharactersClient[g_MovementSkill.m_iTarget].Key;
        }

        SocketClient->ToGameServer()->SendRageAttackRequest(iSkill, wTargetKey);

        g_CMonkSystem.InitConsecutiveState(3.0f, 7.0f);
    }
    break;
    case AT_SKILL_CHAIN_DRIVE:
    case AT_SKILL_CHAIN_DRIVE_STR: {
        WORD wTargetKey = 0;
        if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget))
        {
            wTargetKey = CharactersClient[g_MovementSkill.m_iTarget].Key;
        }
        SocketClient->ToGameServer()->SendRageAttackRequest(iSkill, wTargetKey);

        g_CMonkSystem.InitConsecutiveState(3.0f, 12.0f);

        pObj->m_sTargetIndex = g_MovementSkill.m_iTarget;
        g_CMonkSystem.RageCreateEffect(pObj, iSkill);
    }
    break;
    case AT_SKILL_DRAGON_KICK: {
        WORD wTargetKey = 0;
        if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget))
        {
            wTargetKey = CharactersClient[g_MovementSkill.m_iTarget].Key;
        }
        SocketClient->ToGameServer()->SendRageAttackRequest(iSkill, wTargetKey);

        g_CMonkSystem.InitConsecutiveState(3.0f);

        pObj->m_sTargetIndex = g_MovementSkill.m_iTarget;
        g_CMonkSystem.RageCreateEffect(pObj, iSkill);
    }
    break;
    case AT_SKILL_DARKSIDE:
    case AT_SKILL_DARKSIDE_STR: {
        WORD wTargetKey = 0;
        if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget))
        {
            wTargetKey = CharactersClient[g_MovementSkill.m_iTarget].Key;
        }

        SocketClient->ToGameServer()->SendRageAttackRangeRequest(iSkill, wTargetKey);
        SocketClient->ToGameServer()->SendRageAttackRequest(iSkill, wTargetKey);

        pObj->m_sTargetIndex = g_MovementSkill.m_iTarget;
        g_CMonkSystem.RageCreateEffect(pObj, iSkill);
    }
    break;
    case AT_SKILL_DRAGON_ROAR:
    case AT_SKILL_DRAGON_ROAR_STR: {
        BYTE angle = (BYTE)((((pObj->Angle[2] + 180.f) / 360.f) * 255.f));
        WORD TKey = 0xffff;
        if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget))
        {
            TKey = CharactersClient[g_MovementSkill.m_iTarget].Key;
        }
        BYTE byValue = GetDestValue((pCha->PositionX), (pCha->PositionY), TargetX, TargetY);
        SendRequestMagicContinue(iSkill, pCha->PositionX, pCha->PositionY,
                                 ((pObj->Angle[2] / 360.f) * 255), byValue, angle, TKey, 0);

        pObj->m_sTargetIndex = g_MovementSkill.m_iTarget;
        g_CMonkSystem.RageCreateEffect(pObj, iSkill);
    }
    break;
    case AT_SKILL_ATT_UP_OURFORCES:
    case AT_SKILL_HP_UP_OURFORCES:
    case AT_SKILL_HP_UP_OURFORCES_STR:
    case AT_SKILL_DEF_UP_OURFORCES:
    case AT_SKILL_DEF_UP_OURFORCES_STR:
    case AT_SKILL_DEF_UP_OURFORCES_MASTERY: {
        SendRequestMagic(iSkill, HeroKey);
        if (Random.FpsCheck(2, 1.f))
        {
            SetAction(pObj, PLAYER_SKILL_ATT_UP_OURFORCES);
            PlayBuffer(SOUND_RAGESKILL_BUFF_1);
        }
        else
        {
            SetAction(pObj, PLAYER_SKILL_HP_UP_OURFORCES);
            PlayBuffer(SOUND_RAGESKILL_BUFF_2);
        }
        g_CMonkSystem.RageCreateEffect(pObj, iSkill);
    }
    break;
    case AT_SKILL_PLASMA_STORM_FENRIR: {
        pObj->SetAngleZ(CreateAngle2D(pObj->Position, pCha->TargetPosition));

        gSkillManager.CheckSkillDelay(Hero->CurrentSkill);

        BYTE pos = CalcTargetPos(pObj->Position[0], pObj->Position[1], pCha->TargetPosition[0],
                                 pCha->TargetPosition[1]);
        WORD TKey = 0xffff;
        TKey = getTargetCharacterKey(pCha, g_MovementSkill.m_iTarget);
        pCha->m_iFenrirSkillTarget = g_MovementSkill.m_iTarget;
        SendRequestMagicContinue(iSkill, (pCha->PositionX), (pCha->PositionY),
                                 (BYTE)(pObj->Angle[2] / 360.f * 256.f), 0, pos, TKey,
                                 &pObj->m_bySkillSerialNum);
        pCha->Movement = 0;

        if (pObj->Type == MODEL_PLAYER)
        {
            SetAction_Fenrir_Skill(pCha, pObj);
        }
    }
    break;
    default:
        break;
    }

    pCha->SkillSuccess = true;
}

void SessionGameplayUnit::AttackRagefighter(CHARACTER *pCha, int nSkill, float fDistance)
{
    OBJECT *pObj = &pCha->Object;

    int iMana, iSkillMana;
    gSkillManager.GetSkillInformation(nSkill, 1, NULL, &iMana, NULL, &iSkillMana);

    g_ConsoleDebug.Write(MCD_RECEIVE, L"AttackRagefighter ID : %d, Dis : %.2f | %d %d / %d | %d",
                         nSkill, fDistance, iMana, iSkillMana, CharacterAttribute->Mana,
                         gSkillManager.CheckSkillDelay(Hero->CurrentSkill));

    if (CharacterAttribute->Mana < iMana)
    {
        int Index = sessionKeeper_.GameData()->FindManaItemIndex();

        if (Index != -1)
            SendRequestUse(Index, 0);

        return;
    }

    if (iSkillMana > CharacterAttribute->SkillMana)
        return;

    const int targetIndex = g_MovementSkill.m_iTarget;
    bool bSuccess = CheckMovementSkillTarget(pCha);
    bool bCheckAttack = CharactersClient.IsValidIndex(targetIndex);

    g_ConsoleDebug.Write(
        MCD_SEND, L"AttackRagefighter ID : %d, Success : %d, SelectedCharacter: %d %d | 5d", nSkill,
        bSuccess, SelectedCharacter,
        CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget)
            ? CharactersClient[g_MovementSkill.m_iTarget].Dead
            : -1,
        bCheckAttack);

    if (bSuccess)
    {
        switch (nSkill)
        {
        case AT_SKILL_KILLING_BLOW:
        case AT_SKILL_KILLING_BLOW_STR:
        case AT_SKILL_KILLING_BLOW_MASTERY:
        case AT_SKILL_BEAST_UPPERCUT:
        case AT_SKILL_BEAST_UPPERCUT_STR:
        case AT_SKILL_BEAST_UPPERCUT_MASTERY:
        case AT_SKILL_CHAIN_DRIVE:
        case AT_SKILL_CHAIN_DRIVE_STR:
        case AT_SKILL_DRAGON_KICK:
        case AT_SKILL_DRAGON_ROAR:
        case AT_SKILL_DRAGON_ROAR_STR:
        case AT_SKILL_OCCUPY:
        case AT_SKILL_PHOENIX_SHOT: {
            if (CharactersClient.IsValidIndex(targetIndex) &&
                CharactersClient[targetIndex].Dead == 0)
            {
                SetCharacterTarget(*pCha, targetIndex);

                TargetX =
                    (int)(CharactersClient[targetIndex].Object.Position[0] / TERRAIN_SCALE);
                TargetY =
                    (int)(CharactersClient[targetIndex].Object.Position[1] / TERRAIN_SCALE);

                if (bCheckAttack)
                {
                    fDistance = gSkillManager.GetSkillDistance(nSkill, pCha) * 1.2f;
                    if (CheckTile(pCha, pObj, fDistance) && pCha->SafeZone == false)
                    {
                        bool bNoneWall =
                            CheckWall((pCha->PositionX), (pCha->PositionY), TargetX, TargetY);
                        g_ConsoleDebug.Write(MCD_SEND, L"check wall %d", bNoneWall);
                        if (bNoneWall)
                            UseSkillRagefighter(pCha, pObj);
                    }
                    else
                    {
                        if (PathFinding2(pCha->PositionX, pCha->PositionY, TargetX, TargetY,
                                         &pCha->Path, fDistance))
                        {
                            pCha->Movement = true;
                            pCha->MovementType = MOVEMENT_SKILL;
                            SendMove(pCha, pObj);
                        }
                    }
                }
            }
        }
        break;
        case AT_SKILL_DARKSIDE:
        case AT_SKILL_DARKSIDE_STR:
            UseSkillRagefighter(pCha, pObj);
            break;
        case AT_SKILL_ATT_UP_OURFORCES:
        case AT_SKILL_HP_UP_OURFORCES:
        case AT_SKILL_HP_UP_OURFORCES_STR:
        case AT_SKILL_DEF_UP_OURFORCES:
        case AT_SKILL_DEF_UP_OURFORCES_STR:
        case AT_SKILL_DEF_UP_OURFORCES_MASTERY:
            UseSkillRagefighter(pCha, pObj);
            break;
        case AT_SKILL_PLASMA_STORM_FENRIR: {
            if (gMapManager.InChaosCastle())
                break;

            int nTargetX = (int)(pCha->TargetPosition[0] / TERRAIN_SCALE);
            int nTargetY = (int)(pCha->TargetPosition[1] / TERRAIN_SCALE);
            if (CheckTile(pCha, pObj, fDistance))
            {
                if (g_MovementSkill.m_iTarget != -1)
                {
                    UseSkillRagefighter(pCha, pObj);
                }
                else
                {
                    pCha->m_iFenrirSkillTarget = -1;
                }
            }
            else
            {
                if (g_MovementSkill.m_iTarget != -1)
                {
                    if (PathFinding2(pCha->PositionX, pCha->PositionY, nTargetX, nTargetY,
                                     &pCha->Path, fDistance * 1.2f))
                    {
                        pCha->Movement = true;
                    }
                }
                else
                {
                    Attacking = -1;
                }
            }
        }
        break;
        }
    }

    pCha->SkillSuccess = true;
}

bool SessionGameplayUnit::UseSkillRagePosition(CHARACTER *pCha)
{
    OBJECT *pObj = &pCha->Object;

    if (pObj->CurrentAction == PLAYER_SKILL_GIANTSWING ||
        pObj->CurrentAction == PLAYER_SKILL_STAMP || pObj->CurrentAction == PLAYER_SKILL_DRAGONKICK)
    {
        if (g_CMonkSystem.IsConsecutiveAtt(pObj->AnimationFrame))
        {
            int iSkill = g_MovementSkill.m_bMagic
                             ? CharacterAttribute->Skill[g_MovementSkill.m_iSkill]
                             : g_MovementSkill.m_iSkill;
            return SendAttackPacket(pCha, g_MovementSkill.m_iTarget, iSkill);
        }

        if (pObj->CurrentAction == PLAYER_SKILL_STAMP && pObj->AnimationFrame >= 2.0f)
        {
            pObj->m_sTargetIndex = g_MovementSkill.m_iTarget;
            g_CMonkSystem.RageCreateEffect(pObj, AT_SKILL_BEAST_UPPERCUT);
        }
    }
    else if (pObj->CurrentAction == PLAYER_SKILL_DARKSIDE_READY)
    {
        int AttTime =
            (int)(2.5f / Models[pObj->Type].Actions[PLAYER_SKILL_DARKSIDE_READY].PlaySpeed);
        if (pCha->AttackTime >= AttTime)
        {
            if (g_CMonkSystem.SendDarksideAtt(pObj))
            {
                pCha->AttackTime = 1;
                return true;
            }
        }
        return false;
    }
    else if (pObj->CurrentAction == PLAYER_SKILL_THRUST)
    {
        pObj->SetAngleZ(CreateAngle2D(pObj->Position, pCha->TargetPosition));
        pObj->m_sTargetIndex = g_MovementSkill.m_iTarget;
    }
    else
    {
        g_CMonkSystem.InitConsecutiveState();
        g_CMonkSystem.InitEffectOnce();
    }
    return false;
}

bool SessionGameplayUnit::SkillElf(CHARACTER *c, ITEM *p)
{
    OBJECT *o = &c->Object;
    bool Success = false;
    auto currentSkill = CharacterAttribute->Skill[Hero->CurrentSkill];
    for (int i = 0; i < p->SpecialNum; i++)
    {
        int Spe_Num = p->Special[i];
        if (Spe_Num == AT_SKILL_TRIPLE_SHOT && (AT_SKILL_TRIPLE_SHOT_STR == currentSkill ||
                                                AT_SKILL_TRIPLE_SHOT_MASTERY == currentSkill))
        {
            Spe_Num = currentSkill;
        }

        if (currentSkill == Spe_Num)
        {
            int iMana, iSkillMana;
            gSkillManager.GetSkillInformation(Spe_Num, 1, NULL, &iMana, NULL, &iSkillMana);

            if (g_isCharacterBuff(o, eBuff_InfinityArrow))
                iMana += CharacterMachine->InfinityArrowAdditionalMana;

            if (CharacterAttribute->Mana <= iMana)
            {
                int Index = sessionKeeper_.GameData()->FindManaItemIndex();

                if (Index != -1)
                {
                    SendRequestUse(Index, 0);
                }
                continue;
            }

            if (iSkillMana > CharacterAttribute->SkillMana)
            {
                return (FALSE);
            }
            if (!gSkillManager.CheckSkillDelay(Hero->CurrentSkill))
            {
                return false;
            }
            float Distance = gSkillManager.GetSkillDistance(Spe_Num, c);
            switch (Spe_Num)
            {
            case AT_SKILL_TRIPLE_SHOT:
            case AT_SKILL_TRIPLE_SHOT_STR:
            case AT_SKILL_TRIPLE_SHOT_MASTERY:
                if (!CheckArrow())
                {
                    i = p->SpecialNum;
                    continue;
                }
                if (CheckTile(c, o, Distance))
                {
                    o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));
                    WORD TKey = 0xffff;
                    if (g_MovementSkill.m_iTarget != -1)
                    {
                        TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
                    }
                    SendRequestMagicContinue(Spe_Num, (c->PositionX), (c->PositionY),
                                             (BYTE)(o->Angle[2] / 360.f * 256.f), 0, 0, TKey, 0);
                    SetPlayerAttack(c);
                    if (o->Type != MODEL_PLAYER && o->Kind != KIND_PLAYER)
                        CreateArrows(c, o, NULL, FindHotKey((c->Skill)), 1);
                    Success = true;
                }
                break;

            case AT_SKILL_BLAST_CROSSBOW4:
                if (!CheckArrow())
                {
                    i = p->SpecialNum;
                    continue;
                }
                if (CheckTile(c, o, Distance))
                {
                    o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));
                    BYTE byValue = GetDestValue((c->PositionX), (c->PositionY), TargetX, TargetY);

                    BYTE angle = (BYTE)((((o->Angle[2] + 180.f) / 360.f) * 255.f));
                    WORD TKey = 0xffff;
                    if (g_MovementSkill.m_iTarget != -1)
                    {
                        TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
                    }
                    SendRequestMagicContinue(Spe_Num, (c->PositionX), (c->PositionY),
                                             ((o->Angle[2] / 360.f) * 255), byValue, angle, TKey,
                                             0);
                    SetPlayerAttack(c);
                    if (o->Type != MODEL_PLAYER)
                        CreateArrows(c, o, NULL, FindHotKey((c->Skill)),
                                     Spe_Num - AT_SKILL_BLAST_CROSSBOW4 + 2);

                    Success = true;
                }
                break;
            }
        }
    }
    return Success;
}
// Includes mirror ZzzInterface.cpp, the unit this was extracted from.

bool SessionGameplayUnit::CanExecuteSkill(CHARACTER *c, ActionSkillType Skill, float Distance)
{
    OBJECT *o = &c->Object;

    if (c->Dead > 0)
    {
        return false;
    }

    if (c->SafeZone)
    {
        if ((gMapManager.InBloodCastle() == true) || gMapManager.InChaosCastle() == true)
        {
            if (Skill != AT_SKILL_HEALING && Skill != AT_SKILL_HEALING_STR &&
                Skill != AT_SKILL_DEFENSE && Skill != AT_SKILL_DEFENSE_STR &&
                Skill != AT_SKILL_DEFENSE_MASTERY && Skill != AT_SKILL_ATTACK &&
                Skill != AT_SKILL_ATTACK_STR && Skill != AT_SKILL_ATTACK_MASTERY &&
                Skill != AT_SKILL_SOUL_BARRIER && Skill != AT_SKILL_SOUL_BARRIER_STR &&
                Skill != AT_SKILL_SOUL_BARRIER_PROFICIENCY && Skill != AT_SKILL_SWELL_LIFE &&
                Skill != AT_SKILL_SWELL_LIFE_STR && Skill != AT_SKILL_SWELL_LIFE_PROFICIENCY &&
                Skill != AT_SKILL_INFINITY_ARROW && Skill != AT_SKILL_INFINITY_ARROW_STR &&
                Skill != AT_SKILL_EXPANSION_OF_WIZARDRY &&
                Skill != AT_SKILL_EXPANSION_OF_WIZARDRY_STR &&
                Skill != AT_SKILL_EXPANSION_OF_WIZARDRY_MASTERY && Skill != AT_SKILL_RECOVER &&
                Skill != AT_SKILL_ALICE_BERSERKER && Skill != AT_SKILL_ALICE_BERSERKER_STR &&
                Skill != AT_SKILL_IMPROVE_AG && Skill != AT_SKILL_ADD_CRITICAL &&
                Skill != AT_SKILL_ADD_CRITICAL_STR1 && Skill != AT_SKILL_ADD_CRITICAL_STR2 &&
                Skill != AT_SKILL_ADD_CRITICAL_STR3 && Skill != AT_SKILL_PARTY_TELEPORT &&
                (Skill < AT_SKILL_STUN || Skill > AT_SKILL_REMOVAL_BUFF) &&
                Skill != AT_SKILL_BRAND_OF_SKILL)
            {
                return false;
            }
        }
        return false;
    }

    if (gMapManager.IsCursedTemple())
    {
        if (!(!Hero->m_CursedTempleCurSkillPacket && IsRepeat(VK_SHIFT)))
        {
            if (cursedTemple_.IsPartyMember(SelectedCharacter) == true)
            {
                if (!IsCorrectSkillType_FrendlySkill(Skill) && !IsCorrectSkillType_Buff(Skill))
                {
                    return false;
                }
            }
            else
            {
                if (IsCorrectSkillType_FrendlySkill(Skill) || IsCorrectSkillType_Buff(Skill))
                {
                    if (-1 != SelectedCharacter)
                    {
                        return false;
                    }
                }
            }
        }
    }

    if (Skill == AT_SKILL_SUMMON_EXPLOSION || Skill == AT_SKILL_SUMMON_REQUIEM ||
        Skill == AT_SKILL_SUMMON_POLLUTION)
    {
        CheckTarget(c);
        if (!CheckTile(c, o, Distance))
        {
            return false;
        }
    }

    if (!gSkillManager.AreSkillAttributeRequirementsMet(Skill))
    {
        return false;
    }

    if (CheckSkillUseCondition(o, Skill) == false)
    {
        return false;
    }

    if (CheckMana(c, Skill) == false)
    {
        return false;
    }

    return true;
}

bool SessionGameplayUnit::CheckMana(CHARACTER *c, int Skill)
{
    int iMana, iSkillMana;
    gSkillManager.GetSkillInformation(Skill, 1, NULL, &iMana, NULL, &iSkillMana);
    if (CharacterAttribute->Mana < iMana)
    {
        int Index = sessionKeeper_.GameData()->FindManaItemIndex();

        if (Index != -1)
        {
            SendRequestUse(Index, 0);
        }
        return false;
    }
    if (iSkillMana > CharacterAttribute->SkillMana)
    {
        return false;
    }
    return true;
}

int SessionGameplayUnit::ExecuteSkill(CHARACTER *c, ActionSkillType Skill, float Distance)
{
    OBJECT *o = &c->Object;

    int ClassIndex = gCharacterManager.GetBaseClass(c->Class);

    if (!CanExecuteSkill(c, Skill, Distance))
    {
        return 0;
    }

    int iSkillIndex = gSkillManager.GetSkillIndex(Skill);
    if (iSkillIndex == -1)
    {
        return 0;
    }

    Hero->CurrentSkill = iSkillIndex;

    if (gMapManager.IsCursedTemple() && (!Hero->m_CursedTempleCurSkillPacket && IsRepeat(VK_SHIFT)))
    {
        const auto result = cursedTemple_.TryUseSkill(*c, SelectedCharacter);
        MouseRButtonPush = false;
        if (result == SEASON3A::CursedTemple::SkillResult::InsufficientPoints && g_pSystemLogBox)
            g_pSystemLogBox->AddText(I18N::Game::KillPointIsnTSufficient,
                                     SEASON3B::TYPE_ERROR_MESSAGE);
        return 0;
    }

    if (c->Movement)
    {
        c->SkillSuccess = false;
    }

    if (g_pOption->IsAutoAttack() && gMapManager.ContextMap() != WD_6STADIUM &&
        gMapManager.InChaosCastle() == false)
    {
        if (ClassIndex == CLASS_ELF &&
            (Skill != AT_SKILL_TRIPLE_SHOT && Skill != AT_SKILL_TRIPLE_SHOT_STR &&
             Skill != AT_SKILL_TRIPLE_SHOT_MASTERY && Skill != AT_SKILL_MULTI_SHOT &&
             Skill != AT_SKILL_BOW && Skill != AT_SKILL_PENETRATION &&
             Skill != AT_SKILL_PENETRATION_STR && Skill != AT_SKILL_BLAST_CROSSBOW4 &&
             Skill != AT_SKILL_PLASMA_STORM_FENRIR))
        {
            Attacking = -1;
        }
        else if (ClassIndex == CLASS_KNIGHT &&
                 (Skill == AT_SKILL_SWELL_LIFE || Skill == AT_SKILL_SWELL_LIFE_STR ||
                  Skill == AT_SKILL_SWELL_LIFE_PROFICIENCY))
        {
            Attacking = -1;
        }
        else if (ClassIndex == CLASS_DARK_LORD &&
                 (Skill == AT_SKILL_ADD_CRITICAL || Skill == AT_SKILL_ADD_CRITICAL_STR1 ||
                  Skill == AT_SKILL_ADD_CRITICAL_STR2 || Skill == AT_SKILL_ADD_CRITICAL_STR3 ||
                  Skill == AT_SKILL_PARTY_TELEPORT))
        {
            Attacking = -1;
        }
        else if (ClassIndex == CLASS_WIZARD &&
                 (Skill == AT_SKILL_NOVA_BEGIN || Skill == AT_SKILL_NOVA))
        {
            Attacking = -1;
        }
        else if (Skill >= AT_SKILL_STUN && Skill <= AT_SKILL_REMOVAL_BUFF)
        {
            Attacking = -1;
        }
        else if (Skill == AT_SKILL_BRAND_OF_SKILL)
        {
            Attacking = -1;
        }
        else if (Skill == AT_SKILL_ALICE_THORNS || Skill == AT_SKILL_ALICE_BERSERKER ||
                 Skill == AT_SKILL_ALICE_BERSERKER_STR || Skill == AT_SKILL_ALICE_SLEEP ||
                 Skill == AT_SKILL_ALICE_SLEEP_STR || Skill == AT_SKILL_ALICE_BLIND ||
                 Skill == AT_SKILL_ALICE_WEAKNESS || Skill == AT_SKILL_ALICE_ENERVATION)
        {
            Attacking = -1;
        }
        else if (AT_SKILL_ATT_UP_OURFORCES == Skill || AT_SKILL_HP_UP_OURFORCES == Skill ||
                 AT_SKILL_HP_UP_OURFORCES_STR == Skill || AT_SKILL_DEF_UP_OURFORCES == Skill ||
                 AT_SKILL_DEF_UP_OURFORCES_STR == Skill ||
                 AT_SKILL_DEF_UP_OURFORCES_MASTERY == Skill)
        {
            Attacking = -1;
        }
        else
        {
            Attacking = 2;
        }
    }

    if (o->Type == MODEL_PLAYER)
    {
        if (o->CurrentAction < PLAYER_STOP_MALE ||
            o->CurrentAction > PLAYER_STOP_RIDE_WEAPON &&
                o->CurrentAction != PLAYER_STOP_TWO_HAND_SWORD_TWO &&
                o->CurrentAction != PLAYER_SKILL_HELL_BEGIN &&
                o->CurrentAction != PLAYER_DARKLORD_STAND &&
                o->CurrentAction != PLAYER_STOP_RIDE_HORSE &&
                o->CurrentAction != PLAYER_FENRIR_STAND &&
                o->CurrentAction != PLAYER_FENRIR_STAND_TWO_SWORD &&
                o->CurrentAction != PLAYER_FENRIR_STAND_ONE_RIGHT &&
                o->CurrentAction != PLAYER_FENRIR_STAND_ONE_LEFT &&
                !(o->CurrentAction >= PLAYER_RAGE_FENRIR_STAND &&
                  o->CurrentAction <= PLAYER_RAGE_FENRIR_STAND_ONE_LEFT) &&
                o->CurrentAction != PLAYER_RAGE_UNI_STOP_ONE_RIGHT &&
                o->CurrentAction != PLAYER_STOP_RAGEFIGHTER)
        {
            MouseRButtonPress = 0;
            return 0;
        }
    }
    else
    {
        if (o->CurrentAction < MONSTER01_STOP1 || o->CurrentAction > MONSTER01_STOP2)
        {
            return 0;
        }
    }

    const bool hasAttackTarget = CharactersClient.IsValidIndex(SelectedCharacter) &&
                                 &CharactersClient[SelectedCharacter] != c && CheckAttack();
    g_MovementSkill.m_bMagic = TRUE;
    g_MovementSkill.m_iSkill = Hero->CurrentSkill;
    g_MovementSkill.m_iTarget = hasAttackTarget ? SelectedCharacter : -1;

    if (ClassIndex != CLASS_WIZARD)
    {
        CheckMovementSkillTarget(c);

        const bool hasClearPath = CheckWall(c->PositionX, c->PositionY, TargetX, TargetY);
        if (hasClearPath)
        {
            for (int i = EQUIPMENT_WEAPON_RIGHT; i <= EQUIPMENT_WEAPON_LEFT; i++)
            {
                if (ClassIndex == CLASS_KNIGHT || ClassIndex == CLASS_DARK ||
                    ClassIndex == CLASS_DARK_LORD || ClassIndex == CLASS_RAGEFIGHTER)
                {
                    bool bOk = false;
                    if (c->Helper.Type != MODEL_HORN_OF_UNIRIA &&
                        c->Helper.Type != MODEL_HORN_OF_DINORANT &&
                        c->Helper.Type != MODEL_DARK_HORSE_ITEM &&
                        c->Helper.Type != MODEL_HORN_OF_FENRIR)
                    {
                        bOk = true;
                    }
                    else
                    {
                        switch (Skill)
                        {
                        case AT_SKILL_CHAOTIC_DISEIER:
                        case AT_SKILL_IMPALE:
                        case AT_SKILL_RIDER:
                        case AT_SKILL_DEATHSTAB:
                        case AT_SKILL_DEATHSTAB_STR:
                        case AT_SKILL_FORCE:
                        case AT_SKILL_FORCE_WAVE:
                        case AT_SKILL_FORCE_WAVE_STR:
                        case AT_SKILL_FIREBURST:
                        case AT_SKILL_FIREBURST_STR:
                        case AT_SKILL_FIREBURST_MASTERY:
                        case AT_SKILL_EARTHSHAKE:
                        case AT_SKILL_EARTHSHAKE_STR:
                        case AT_SKILL_EARTHSHAKE_MASTERY:
                        case AT_SKILL_THUNDER_STRIKE:
                        case AT_SKILL_SPACE_SPLIT:
                        case AT_SKILL_PLASMA_STORM_FENRIR:
                        case AT_SKILL_FIRE_SCREAM:
                        case AT_SKILL_FIRE_SCREAM_STR:
                            bOk = true;
                            break;
                        }
                    }

                    if (bOk)
                    {
                        if (SkillWarrior(c, &CharacterMachine->Equipment[i]))
                        {
                            return (int)ExecuteSkillComplete(c);
                        }
                    }
                }
                if (ClassIndex == CLASS_ELF)
                {
                    if (SkillElf(c, &CharacterMachine->Equipment[i]))
                    {
                        return (int)ExecuteSkillComplete(c);
                    }
                }
            }
        }
        else if (GameLogic::Combat::ShouldPathToSkillTarget(
                     CharactersClient.IsValidIndex(SelectedCharacter), hasClearPath))
        {
            if (PathFinding2((c->PositionX), (c->PositionY), TargetX, TargetY, &c->Path))
            {
                SendMove(c, o);
                return 0;
            }
            else
            {
                ZeroMemory(&g_MovementSkill, sizeof(g_MovementSkill));
                g_MovementSkill.m_iTarget = -1;
                return -1;
            }
        }
    }
    if (ClassIndex == CLASS_ELF)
    {
        AttackElf(c, Skill, Distance);
    }
    if (ClassIndex == CLASS_KNIGHT || ClassIndex == CLASS_DARK || ClassIndex == CLASS_DARK_LORD)
    {
        AttackKnight(c, Skill, Distance);
    }
    if (ClassIndex == CLASS_RAGEFIGHTER)
    {
        AttackRagefighter(c, Skill, Distance);
    }
    if (ClassIndex == CLASS_WIZARD || ClassIndex == CLASS_DARK || ClassIndex == CLASS_SUMMONER)
    {
        AttackWizard(c, Skill, Distance);
    }
    if ((Skill >= AT_SKILL_STUN && Skill <= AT_SKILL_REMOVAL_BUFF))
    {
        AttackCommon(c, Skill, Distance);
    }

    return (int)ExecuteSkillComplete(c);
}

namespace GameLogic::Combat
{
bool ExecuteSkillComplete(CHARACTER *c)
{
    return c->SkillSuccess && !c->Movement;
}
} // namespace GameLogic::Combat

using namespace SEASON3B;

CNewBloodCastleSystem::CNewBloodCastleSystem(SessionKeeper &keeper) : CSBaseMatch(keeper)
{
}

CNewBloodCastleSystem::~CNewBloodCastleSystem()
{
}

void CNewBloodCastleSystem::SetMatchResult(const int iNumDevilRank, const int iMyRank,
                                           const MatchResult *pMatchResult, const int Success)
{
    if (iNumDevilRank != 255)
    {
        return;
    }

    m_iNumResult = Success;
    memcpy(m_MatchResult, pMatchResult, sizeof(MatchResult));
    SEASON3B::CreateMessageBox(
        MSGBOX_LAYOUT_CLASS(SEASON3B::CBloodCastleResultMsgBoxLayout, SessionOrigin()));
}

void CNewBloodCastleSystem::SetMatchGameCommand(const LPPRECEIVE_MATCH_GAME_STATE data)
{
    switch (data->m_byPlayState)
    {
    case 0:
        SetAllAction(PLAYER_RUSH1);
        PlayBuffer(SOUND_BLOODCASTLE, NULL, true);

    case 1:
        SetMatchInfo(data->m_byPlayState + 1, 15 * 60, data->m_wRemainSec, data->m_wMaxKillMonster,
                     data->m_wCurKillMonster);

        if (data->m_wIndex != 65535 && data->m_byItemType != 255 && data->m_byItemType != 0)
        {
            WORD Key = data->m_wIndex;
            Key &= 0x7FFF;

            int index = HangerBloodCastleQuestItem(Key);
            CHARACTER *c = &CharactersClient[index];

            if (c != NULL)
            {
                c->EtcPart = data->m_byItemType;
            }
        }
        break;

    case 2:
        clearMatchInfo();
        StopBuffer(SOUND_BLOODCASTLE, true);

        break;

    case 3:
        SetActionObject(gMapManager.ContextMap(), 36, 20, 1.f);
        break;
    case 4:
        SetMatchInfo(data->m_byPlayState + 1, 15 * 60, data->m_wRemainSec, data->m_wMaxKillMonster,
                     data->m_wCurKillMonster);

        if (data->m_wIndex != 65535 && data->m_byItemType != 255 && data->m_byItemType != 0)
        {
            WORD Key = data->m_wIndex;
            Key &= 0x7FFF;

            int index = HangerBloodCastleQuestItem(Key);
            CHARACTER *c = &CharactersClient[index];

            if (c != NULL)
            {
                c->EtcPart = data->m_byItemType;
            }
        }
        break;
    }
}

void CNewBloodCastleSystem::UpdateMatchState(void)
{
    if (m_byMatchType > 0 && gMapManager.InBloodCastle() == true)
    {
        switch (m_byMatchType)
        {
        case 0:
        case 1:
        case 2:
        case 5:
            if (m_iMatchTime > 0)
            {
                if (!g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_BLOODCASTLE_TIME))
                {
                    g_pNewUISystem->Show(SEASON3B::INTERFACE_BLOODCASTLE_TIME);
                }

                g_pBloodCastle->SetTime(m_iMatchTime);
                g_pBloodCastle->SetKillMonsterStatue(m_iKillMonster, m_iMaxKillMonster);
            }
            break;

        default:
            break;
        }
    }
    else
    {
        if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_BLOODCASTLE_TIME))
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_BLOODCASTLE_TIME);
        }
    }
}

CNewChaosCastleSystem::CNewChaosCastleSystem(SessionKeeper &keeper) : CSBaseMatch(keeper)
{
    int iChaosCastleLimitArea1[16] = {23, 75,  44, 76,  43, 77, 44, 108,
                                      23, 107, 42, 108, 23, 77, 24, 106};
    int iChaosCastleLimitArea2[16] = {25, 77,  42, 78,  41, 79, 42, 106,
                                      25, 105, 40, 106, 25, 79, 26, 104};
    int iChaosCastleLimitArea3[16] = {27, 79,  40, 80,  39, 81, 40, 104,
                                      27, 103, 38, 104, 27, 81, 28, 102};
    memcpy(m_iChaosCastleLimitArea1, iChaosCastleLimitArea1, sizeof(int) * 16);
    memcpy(m_iChaosCastleLimitArea2, iChaosCastleLimitArea2, sizeof(int) * 16);
    memcpy(m_iChaosCastleLimitArea3, iChaosCastleLimitArea3, sizeof(int) * 16);

    m_byCurrCastleLevel = 255;
    m_bActionMatch = true;
}

CNewChaosCastleSystem::~CNewChaosCastleSystem()
{
}

void CNewChaosCastleSystem::SetMatchResult(const int iNumDevilRank, const int iMyRank,
                                           const MatchResult *pMatchResult, const int Success)
{
    if (iNumDevilRank != 254)
    {
        return;
    }

    m_iNumResult = Success;

    memcpy(m_MatchResult, pMatchResult, sizeof(MatchResult));

    SEASON3B::CreateMessageBox(
        MSGBOX_LAYOUT_CLASS(SEASON3B::CChaosCastleResultMsgBoxLayout, SessionOrigin()));
}

void CNewChaosCastleSystem::SetMatchGameCommand(const LPPRECEIVE_MATCH_GAME_STATE data)
{
    switch (data->m_byPlayState)
    {
    case 5:
        m_bActionMatch = true;
        m_byCurrCastleLevel = 255;

        SetAllAction(PLAYER_RUSH1);
        StopBuffer(SOUND_CHAOS_ENVIR, true);
        PlayBuffer(SOUND_CHAOSCASTLE, NULL, true);
        break;
    case 6:
        SetMatchInfo(data->m_byPlayState + 1, 15 * 60, data->m_wRemainSec, data->m_wMaxKillMonster,
                     data->m_wCurKillMonster);
        break;
    case 7:
        clearMatchInfo();
        StopBuffer(SOUND_CHAOSCASTLE, true);
        if (gMapManager.InChaosCastle() == true)
        {
            PlayBuffer(SOUND_CHAOS_ENVIR, NULL, true);
            PlayBuffer(SOUND_CHAOS_END);
        }
        break;
    case 8:
        if (m_byCurrCastleLevel == 0)
        {
            m_byCurrCastleLevel = 1;
            SetActionObject(gMapManager.ContextMap(), 1, 40, 1);
            AddTerrainAttributeRange(23, 75, 22, 2, TW_NOGROUND, 1);
            AddTerrainAttributeRange(43, 77, 2, 32, TW_NOGROUND, 1);
            AddTerrainAttributeRange(23, 107, 20, 2, TW_NOGROUND, 1);
            AddTerrainAttributeRange(23, 77, 2, 30, TW_NOGROUND, 1);

            PlayBuffer(SOUND_CHAOS_FALLING_STONE);
        }
        break;
    case 9:
        if (m_byCurrCastleLevel == 3)
        {
            m_byCurrCastleLevel = 4;
            SetActionObject(gMapManager.ContextMap(), 1, 40, 1);
            AddTerrainAttributeRange(25, 77, 18, 2, TW_NOGROUND, 1);
            AddTerrainAttributeRange(41, 79, 2, 28, TW_NOGROUND, 1);
            AddTerrainAttributeRange(25, 105, 16, 2, TW_NOGROUND, 1);
            AddTerrainAttributeRange(25, 79, 2, 26, TW_NOGROUND, 1);

            PlayBuffer(SOUND_CHAOS_FALLING_STONE);
        }
        break;
    case 10:
        if (m_byCurrCastleLevel == 6)
        {
            m_byCurrCastleLevel = 7;
            SetActionObject(gMapManager.ContextMap(), 1, 40, 1);
            AddTerrainAttributeRange(27, 79, 14, 2, TW_NOGROUND, 1);
            AddTerrainAttributeRange(39, 81, 2, 24, TW_NOGROUND, 1);
            AddTerrainAttributeRange(27, 103, 12, 2, TW_NOGROUND, 1);
            AddTerrainAttributeRange(27, 81, 2, 22, TW_NOGROUND, 1);

            PlayBuffer(SOUND_CHAOS_FALLING_STONE);
        }
        break;
    }
}

void CNewChaosCastleSystem::UpdateMatchState(void)
{
    if (m_byMatchType > 0 && gMapManager.InChaosCastle())
    {
        switch (m_byMatchType)
        {
        case 6:
        case 7:
        case 8:
            if (m_iMatchTime > 0)
            {
                if (!g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_CHAOSCASTLE_TIME))
                {
                    g_pNewUISystem->HideAll();
                    g_pNewUISystem->Show(SEASON3B::INTERFACE_CHAOSCASTLE_TIME);
                }

                g_pChaosCastleTime->SetTime(m_iMatchTime);
                g_pChaosCastleTime->SetKillMonsterStatue(m_iKillMonster, m_iMaxKillMonster);

                if (m_iKillMonster <= 46 && m_iKillMonster > 40)
                {
                    m_byCurrCastleLevel = 0;
                }
                else if (m_iKillMonster <= 36 && m_iKillMonster > 30)
                {
                    m_byCurrCastleLevel = 3;
                }
                else if (m_iKillMonster <= 26 && m_iKillMonster > 20)
                {
                    m_byCurrCastleLevel = 6;
                }
            }
            break;

        default:
            break;
        }
    }
    else
    {
        if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_CHAOSCASTLE_TIME))
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_CHAOSCASTLE_TIME);
        }
    }
}

using namespace SEASON3A;

CursedTemple::CursedTemple(SessionKeeper &keeper)
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject()), m_IsTalkEnterNpc(false), m_HolyItemPlayerIndex(0xffff)
{
    Initialize();
}

CursedTemple::~CursedTemple()
{
    Destroy();
}

void CursedTemple::Initialize()
{
    ResetCursedTemple();
}

void CursedTemple::Destroy()
{
    if (m_TerrainWaterIndex.size() != 0)
        m_TerrainWaterIndex.clear();
}

void CursedTemple::ResetCursedTemple()
{
    skillPoints_ = 0;
    teamCount_ = 0;
    m_HolyItemPlayerIndex = 0xffff;
    m_CursedTempleState = eCursedTempleState_None;
    m_InterfaceState = false;
    m_AlliedPoint = 0;
    m_IllusionPoint = 0;
    m_ShowAlliedPointEffect = false;
    m_ShowIllusionPointEffect = false;
    m_bGaugebarEnabled = false;
    m_fGaugebarCloseTimer = 0;
}

void CursedTemple::SetInterfaceState(bool state, int subtype)
{
    if (subtype == -1)
    {
        m_InterfaceState = state;
    }
    else if (subtype == 0)
    {
        if (m_CursedTempleState == eCursedTempleState_Wait ||
            m_CursedTempleState == eCursedTempleState_Ready)
        {
            m_InterfaceState = state;
        }
    }

    if (m_InterfaceState == false)
    {
        if (!g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_CURSEDTEMPLE_RESULT))
        {
            sessionKeeper_.Gameplay()->RestorePickedItem();
            g_pNewUISystem->HideAll();
        }
    }
}

bool CursedTemple::GetInterfaceState(int type, int subtype)
{
    if (!gMapManager.IsCursedTemple())
        return true;

    bool result = m_InterfaceState;

    if (type == SEASON3B::INTERFACE_COMMAND)
    {
        auto tempSubtype = (COMMAND_TYPE)(subtype);

        if (subtype == COMMAND_PARTY)
        {
            return false;
        }
        else if (tempSubtype == COMMAND_TRADE || tempSubtype == COMMAND_PURCHASE ||
                 tempSubtype == COMMAND_BATTLE || tempSubtype == COMMAND_END)
        {
            return result;
        }
        else
        {
            return true;
        }
    }

    return result;
}

bool CursedTemple::IsPartyMember(DWORD selectcharacterindex)
{
    if (PartyNumber == 0)
        return false;

    CHARACTER *c = &CharactersClient[selectcharacterindex];
    if (c == NULL)
        return false;

    for (int i = 0; i < PartyNumber; ++i)
    {
        PARTY_t *p = &Party[i];
        int length = std::max<int>(1, wcslen(c->ID));
        if (!wcsncmp(p->Name, c->ID, length))
            return true;
    }
    return false;
}

void CursedTemple::UpdateTempleSystemMsg(int _Value)
{
    wchar_t szText[256] = {
        0,
    };
    mu_swprintf(szText, I18N::Game::TheAdmissionAndScrollLevelsDoNotMatch);
    switch (_Value)
    {
    case 0:
        break;
    case 1:
        break;
    case 2:
        break;
    case 3:
        g_pSystemLogBox->AddText(I18N::Game::TheAdmissionAndScrollLevelsDoNotMatch,
                                 SEASON3B::TYPE_ERROR_MESSAGE);
        break;
    case 4:
        g_pSystemLogBox->AddText(
            I18N::Game::YouCannotEnterTheZoneWithTheNumberOfMembersExceedingTheLimit,
            SEASON3B::TYPE_ERROR_MESSAGE);
        break;
    case 5:
        mu_swprintf(szText, I18N::Game::YouMayEnterOnlyDTimesPerDay, 6);
        g_pSystemLogBox->AddText(szText, SEASON3B::TYPE_ERROR_MESSAGE);
        break;
    case 6:
        break;
    case 7:
        g_pSystemLogBox->AddText(I18N::Game::YouCannotEnterIfYouAreA1stStageOutlaw,
                                 SEASON3B::TYPE_ERROR_MESSAGE);
        break;
    case 8:
        g_pSystemLogBox->AddText(I18N::Game::YouCanTWarpWearingTheRingOfTransformation,
                                 SEASON3B::TYPE_ERROR_MESSAGE);
        break;
    }
}

void CursedTemple::SetGaugebarEnabled(bool bFlag)
{
    m_bGaugebarEnabled = bFlag;
    m_fGaugebarCloseTimer = WorldTime + 10000000.0f;
}

void CursedTemple::SetGaugebarCloseTimer()
{
    m_fGaugebarCloseTimer = WorldTime;
}

bool CursedTemple::IsGaugebarEnabled()
{
    return m_bGaugebarEnabled;
}

void CursedTemple::Process()
{
    if (!gMapManager.IsCursedTemple())
        return;
}

void CursedTemple::Draw()
{
    if (!gMapManager.IsCursedTemple())
        return;
}

bool CursedTemple::IsHolyItemPickState()
{
    if (!gMapManager.IsCursedTemple())
        return false;

    if (m_CursedTempleState != eCursedTempleState_Play)
    {
        return false;
    }

    return true;
}

void CursedTemple::ReceiveCursedTempleState(const eCursedTempleState state)
{
    if (state == eCursedTempleState_Ready)
    {
        skillPoints_ = 0;
        teamCount_ = 0;
        m_HolyItemPlayerIndex = 0xffff;
    }
    m_CursedTempleState = state;

    if (m_CursedTempleState == eCursedTempleState_Wait)
    {
        SetInterfaceState(false);
    }
    else if (m_CursedTempleState == eCursedTempleState_Ready)
    {
        SetInterfaceState(false);
    }
    else if (m_CursedTempleState == eCursedTempleState_Play)
    {
        m_TerrainWaterIndex.clear();
        SetTerrainWaterState(m_TerrainWaterIndex, 0);
        SetInterfaceState(false);
    }
    else if (m_CursedTempleState == eCursedTempleState_End)
    {
        SetTerrainWaterState(m_TerrainWaterIndex, 1);
        m_TerrainWaterIndex.clear();
        SetInterfaceState(false);
        sessionKeeper_.Gameplay()->RestorePickedItem();
    }
}

void CursedTemple::ReceiveCursedTempleInfo(const BYTE *ReceiveBuffer)
{
    UpdateTeam(ReceiveBuffer);
    auto data = (LPPMSG_CURSED_TAMPLE_STATE)ReceiveBuffer;

    if (m_AlliedPoint != data->btAlliedPoint)
    {
        m_ShowAlliedPointEffect = true;
    }
    else if (m_IllusionPoint != data->btIllusionPoint)
    {
        m_ShowIllusionPointEffect = true;
    }
    m_AlliedPoint = data->btAlliedPoint;
    m_IllusionPoint = data->btIllusionPoint;

    if (m_HolyItemPlayerIndex != 0xffff && data->btUserIndex == 0xffff)
    {
        DeleteEffect(MODEL_CURSEDTEMPLE_HOLYITEM);
    }

    if (data->btUserIndex != 0xffff)
    {
        WORD holyitemkey = data->btUserIndex;
        WORD holyitemcharacterindex = FindCharacterIndex(holyitemkey);

        if (CharactersClient.IsValidIndex(holyitemcharacterindex))
        {
            CHARACTER *c = &CharactersClient[holyitemcharacterindex];
            OBJECT *o = &c->Object;
            OBJECT *ho = &Hero->Object;

            if (o->Live && !SearchEffect(MODEL_CURSEDTEMPLE_HOLYITEM, o))
            {
                PlayBuffer(SOUND_CURSEDTEMPLE_GAMESYSTEM3);

                vec3_t tempPosition, p;
                Vector(70.f, 0.f, 0.f, p);
                BMD *b = &Models[o->Type];
                VectorCopy(o->Position, b->BodyOrigin);

                float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;

                float fSpeedPerFrame = fActionSpeed / 10.f;

                float fAnimationFrame = o->AnimationFrame - fActionSpeed;

                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                b->TransformPosition(BoneTransform[20], p, tempPosition, true);
                CreateEffect(MODEL_CURSEDTEMPLE_HOLYITEM, tempPosition, o->Angle, o->Light, 0, o);
            }
        }
    }

    m_HolyItemPlayerIndex = data->btUserIndex;
}

void CursedTemple::InstallBehavior()
{
    //EFFECT

    //game system sound
    LoadWaveFile(SOUND_CURSEDTEMPLE_GAMESYSTEM1, L"Data\\Sound\\w47\\cursedtemple_start01.wav", 1);
    LoadWaveFile(SOUND_CURSEDTEMPLE_GAMESYSTEM2, L"Data\\Sound\\w47\\cursedtemple_statue01.wav", 1);
    LoadWaveFile(SOUND_CURSEDTEMPLE_GAMESYSTEM3, L"Data\\Sound\\w47\\cursedtemple_holy01.wav", 1);
    LoadWaveFile(SOUND_CURSEDTEMPLE_GAMESYSTEM4, L"Data\\Sound\\w47\\cursedtemple_score01.wav", 1);
    LoadWaveFile(SOUND_CURSEDTEMPLE_GAMESYSTEM5, L"Data\\Sound\\w47\\cursedtemple_end01.wav", 1);
    //moster 1 - 2 effect sound
    LoadWaveFile(SOUND_CURSEDTEMPLE_MONSTER1_IDLE, L"Data\\Sound\\w47\\cursedtemple_idle01.wav", 1);
    LoadWaveFile(SOUND_CURSEDTEMPLE_MONSTER_MOVE, L"Data\\Sound\\w47\\cursedtemple_move01.wav", 1);
    LoadWaveFile(SOUND_CURSEDTEMPLE_MONSTER1_DAMAGE, L"Data\\Sound\\w47\\cursedtemple_damage01.wav",
                 1);
    LoadWaveFile(SOUND_CURSEDTEMPLE_MONSTER1_DEATH, L"Data\\Sound\\w47\\cursedtemple_death01.wav",
                 1);
    //moster 3 effect sound
    LoadWaveFile(SOUND_CURSEDTEMPLE_MONSTER2_IDLE, L"Data\\Sound\\w47\\cursedtemple_idle02.wav", 1);
    LoadWaveFile(SOUND_CURSEDTEMPLE_MONSTER2_ATTACK, L"Data\\Sound\\w47\\cursedtemple_Attack01.wav",
                 1);
    LoadWaveFile(SOUND_CURSEDTEMPLE_MONSTER2_DAMAGE, L"Data\\Sound\\w47\\cursedtemple_damage02.wav",
                 1);
    LoadWaveFile(SOUND_CURSEDTEMPLE_MONSTER2_DEATH, L"Data\\Sound\\w47\\cursedtemple_death02.wav",
                 1);
}

void CursedTemple::UpdateMusic()
{

    const eCursedTempleState state = m_CursedTempleState;
    if (state == eCursedTempleState_Wait)
    {
        PlayMp3(MUSIC_CURSEDTEMPLE_WAIT);

        if (IsEndMp3())
            StopMp3(MUSIC_CURSEDTEMPLE_WAIT);
    }
    else if (state == eCursedTempleState_Ready || state == eCursedTempleState_Play)
    {
        StopMp3(MUSIC_CURSEDTEMPLE_WAIT);
        PlayMp3(MUSIC_CURSEDTEMPLE_GAME);

        if (IsEndMp3())
            StopMp3(MUSIC_CURSEDTEMPLE_GAME);
    }
    else if (state == eCursedTempleState_None)
    {
        StopMp3(MUSIC_CURSEDTEMPLE_WAIT);
        StopMp3(MUSIC_CURSEDTEMPLE_GAME);
    }
}

bool CursedTemple::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_CURSEDTEMPLE_WAIT) == 0 ||
           std::strcmp(track, MUSIC_CURSEDTEMPLE_GAME) == 0;
}

void CursedTemple::UpdateTeam(const BYTE *packet)
{
    const auto *info = reinterpret_cast<const PMSG_CURSED_TAMPLE_STATE *>(packet);
    teamCount_ = std::min<int>(info->btPartyCount, MAX_PARTYS);
    const auto *members =
        reinterpret_cast<const PMSG_CURSED_TAMPLE_PARTY_POS *>(packet + sizeof(*info));
    for (int i = 0; i < teamCount_; ++i)
        teamKeys_[i] = members[i].wPartyUserIndex;
}

void SessionGameplayUnit::AttackElf(CHARACTER *c, int Skill, float Distance)
{
    OBJECT *o = &c->Object;
    int ClassIndex = gCharacterManager.GetBaseClass(c->Class);

    int iMana, iSkillMana;
    gSkillManager.GetSkillInformation(Skill, 1, NULL, &iMana, NULL, &iSkillMana);
    if (g_isCharacterBuff(o, eBuff_InfinityArrow))
        iMana += CharacterMachine->InfinityArrowAdditionalMana;
    if (iMana > CharacterAttribute->Mana)
    {
        int Index = sessionKeeper_.GameData()->FindManaItemIndex();

        if (Index != -1)
        {
            SendRequestUse(Index, 0);
        }
        return;
    }

    if (iSkillMana > CharacterAttribute->SkillMana)
    {
        return;
    }
    if (!gSkillManager.CheckSkillDelay(Hero->CurrentSkill))
    {
        return;
    }

    int iEnergy;
    gSkillManager.GetSkillInformation_Energy(Skill, &iEnergy);
    if (iEnergy > (CharacterAttribute->Energy + CharacterAttribute->AddEnergy))
    {
        return;
    }

    bool Success = CheckMovementSkillTarget(c);
    const int movementTargetIndex = g_MovementSkill.m_iTarget;
    const bool hasMovementTarget =
        CharactersClient.IsValidIndex(movementTargetIndex) &&
        CharactersClient[movementTargetIndex].Dead == 0;

    switch (Skill)
    {
    case AT_SKILL_SUMMON:
    case AT_SKILL_SUMMON + 1:
    case AT_SKILL_SUMMON + 2:
    case AT_SKILL_SUMMON + 3:
    case AT_SKILL_SUMMON + 4:
    case AT_SKILL_SUMMON + 5:
    case AT_SKILL_SUMMON + 6:
#ifdef ADD_ELF_SUMMON
    case AT_SKILL_SUMMON + 7:
#endif // ADD_ELF_SUMMON
        if (gMapManager.ContextMap() != WD_10HEAVEN && gMapManager.InChaosCastle() == false)
            if (!g_Direction.m_CKanturu.IsMayaScene(gMapManager.ContextMap()))
            {
                SendRequestMagic(Skill, HeroKey);
                SetPlayerMagic(c);
            }
        return;

    case AT_SKILL_PENETRATION:
    case AT_SKILL_PENETRATION_STR:
        if ((o->Type == MODEL_PLAYER) &&
            (gCharacterManager.GetEquipedBowType(Hero) != BOWTYPE_NONE))
        {
            if (!CheckArrow())
                break;
            if (CheckTile(c, o, Distance))
            {
                o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));

                WORD TKey = 0xffff;
                if (g_MovementSkill.m_iTarget != -1)
                {
                    TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
                }
                SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                         (BYTE)(o->Angle[2] / 360.f * 256.f), 0, 0, TKey, 0);
                SetPlayerAttack(c);
                if (o->Type != MODEL_PLAYER)
                {
                    CreateArrows(c, o, NULL, FindHotKey((c->Skill)), 0, (c->Skill));
                }
            }
        }
        break;

    case AT_SKILL_HEALING:
    case AT_SKILL_HEALING_STR:
    case AT_SKILL_ATTACK:
    case AT_SKILL_ATTACK_STR:
    case AT_SKILL_ATTACK_MASTERY:
    case AT_SKILL_DEFENSE:
    case AT_SKILL_DEFENSE_STR:
    case AT_SKILL_DEFENSE_MASTERY:
        if (CharactersClient.IsValidIndex(SelectedCharacter))
        {
            if (CharactersClient[SelectedCharacter].Object.Kind != KIND_PLAYER)
            {
                Attacking = -1;
                return;
            }

            if (c != Hero)
                return;

            //if (!g_pPartyManager->IsPartyMember(SelectedCharacter))
            //    return;

            SetCharacterTarget(*c, SelectedCharacter);

            //ZeroMemory(&g_MovementSkill, sizeof(g_MovementSkill));
            g_MovementSkill.m_bMagic = TRUE;
            g_MovementSkill.m_iSkill = Hero->CurrentSkill;
            g_MovementSkill.m_iTarget = SelectedCharacter;

            if (!CheckTile(c, o, Distance))
            {
                if (PathFinding2(c->PositionX, c->PositionY, TargetX, TargetY, &c->Path, Distance))
                {
                    c->Movement = true;
                    c->MovementType = MOVEMENT_SKILL;
                    SendMove(c, o);
                }
                return;
            }

            SendRequestMagic(Skill, CharactersClient[g_MovementSkill.m_iTarget].Key);
        }
        else
        {
            SendRequestMagic(Skill, HeroKey);
        }
        SetPlayerMagic(c);
        return;
    }
    if (!CheckTile(c, o, Distance))
    {
        const bool hasSelectedSupportTarget = CharactersClient.IsValidIndex(SelectedCharacter) &&
                                              (Skill == AT_SKILL_HEALING ||
                                               Skill == AT_SKILL_HEALING_STR ||
                                               Skill == AT_SKILL_ATTACK ||
                                               Skill == AT_SKILL_ATTACK_STR ||
                                               Skill == AT_SKILL_ATTACK_MASTERY ||
                                               Skill == AT_SKILL_DEFENSE ||
                                               Skill == AT_SKILL_DEFENSE_STR ||
                                               Skill == AT_SKILL_DEFENSE_MASTERY);
        const bool hasSelectedFireScreamTarget = CharactersClient.IsValidIndex(SelectedCharacter) &&
                                                 (Skill == AT_SKILL_FIRE_SCREAM ||
                                                  Skill == AT_SKILL_FIRE_SCREAM_STR);
        if (hasMovementTarget || hasSelectedSupportTarget || hasSelectedFireScreamTarget)
        {
            const int pathTargetIndex = hasMovementTarget ? movementTargetIndex : SelectedCharacter;
            if (CharactersClient.IsValidIndex(pathTargetIndex) &&
                CharactersClient[pathTargetIndex].Object.Kind == KIND_PLAYER)
            {
                if (PathFinding2(c->PositionX, c->PositionY, TargetX, TargetY, &c->Path, Distance))
                {
                    c->Movement = true;
                    c->MovementType = MOVEMENT_SKILL;
                    SendMove(c, o);
                }
            }
        }

        if (Skill != AT_SKILL_STUN && Skill != AT_SKILL_REMOVAL_STUN && Skill != AT_SKILL_MANA &&
            Skill != AT_SKILL_INVISIBLE && Skill != AT_SKILL_REMOVAL_INVISIBLE &&
            Skill != AT_SKILL_PLASMA_STORM_FENRIR)
            return;
    }
    bool Wall = CheckWall((c->PositionX), (c->PositionY), TargetX, TargetY);
    if (Wall)
    {
        if (CharactersClient.IsValidIndex(SelectedCharacter))
        {
            if (CharactersClient[SelectedCharacter].Object.Kind == KIND_PLAYER)
            {
                switch (Skill)
                {
                case AT_SKILL_HEALING:
                case AT_SKILL_HEALING_STR:
                case AT_SKILL_ATTACK:
                case AT_SKILL_ATTACK_STR:
                case AT_SKILL_ATTACK_MASTERY:
                case AT_SKILL_DEFENSE:
                case AT_SKILL_DEFENSE_STR:
                case AT_SKILL_DEFENSE_MASTERY:
                    UseSkillElf(c, o);
                    return;
                }
            }
            if (CheckAttack())
            {
                if (((Skill == AT_SKILL_ICE_ARROW) || (Skill == AT_SKILL_ICE_ARROW_STR) ||
                     (Skill == AT_SKILL_DEEPIMPACT)) &&
                    ((o->Type == MODEL_PLAYER) &&
                     (gCharacterManager.GetEquipedBowType(Hero) != BOWTYPE_NONE)))
                {
                    UseSkillElf(c, o);
                }
            }
        }
    }

    switch (Skill)
    {
    case AT_SKILL_INFINITY_ARROW:
    case AT_SKILL_INFINITY_ARROW_STR: {
        if (g_isCharacterBuff((&Hero->Object), eBuff_InfinityArrow) == false)
        {
            SendRequestMagic(Skill, HeroKey);
            if ((c->Helper.Type == MODEL_HORN_OF_FENRIR) ||
                (c->Helper.Type == MODEL_HORN_OF_UNIRIA) ||
                (c->Helper.Type == MODEL_HORN_OF_DINORANT) ||
                (c->Helper.Type == MODEL_DARK_HORSE_ITEM))
                SetPlayerMagic(c);
            else
                SetAction(o, PLAYER_RUSH1);

            c->Movement = 0;
        }
    }
    break;

    case AT_SKILL_HELLOWIN_EVENT_1:
    case AT_SKILL_HELLOWIN_EVENT_2:
    case AT_SKILL_HELLOWIN_EVENT_3:
    case AT_SKILL_HELLOWIN_EVENT_4:
    case AT_SKILL_HELLOWIN_EVENT_5:
        SendRequestMagic(Skill, HeroKey);
        //					SetPlayerMagic(c);
        c->Movement = 0;
        break;
    case AT_SKILL_RECOVER: {
        vec3_t Light, Position, P, dp;

        float Matrix[3][4];

        Vector(0.f, -220.f, 130.f, P);
        AngleMatrix(o->Angle, Matrix);
        VectorRotate(P, Matrix, dp);
        VectorAdd(dp, o->Position, Position);
        Vector(0.7f, 0.6f, 0.f, Light);
        CreateEffect(BITMAP_IMPACT, Position, o->Angle, Light, 0, o);
        SetAction(o, PLAYER_RECOVER_SKILL);

        if (CharactersClient.IsValidIndex(SelectedCharacter) &&
            CharactersClient[SelectedCharacter].Object.Kind == KIND_PLAYER)
        {
            SendRequestMagic(Skill, CharactersClient[SelectedCharacter].Key);
        }
        else
        {
            SendRequestMagic(Skill, HeroKey);
        }
        //			UseSkillElf( c, o);
    }
    break;
    case AT_SKILL_MULTI_SHOT: {
        if (!CheckArrow())
            break;

        if (GetEquipedBowType_Skill() == BOWTYPE_NONE)
            return;
        o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));

        if (CheckTile(c, o, Distance))
        {
            BYTE PathX[1];
            BYTE PathY[1];
            PathX[0] = (c->PositionX);
            PathY[0] = (c->PositionY);

            SendCharacterMove(c->Key, o->Angle[2], 1, &PathX[0], &PathY[0], TargetX, TargetY);

            BYTE byValue = GetDestValue((c->PositionX), (c->PositionY), TargetX, TargetY);

            BYTE angle = (BYTE)((((o->Angle[2] + 180.f) / 360.f) * 255.f));
            WORD TKey = 0xffff;
            if (g_MovementSkill.m_iTarget != -1)
            {
                TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
            }

            SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                     ((o->Angle[2] / 360.f) * 255), byValue, angle, TKey, 0);
            SetAttackSpeed();
            //									SetAction(o, PLAYER_SKILL_FLAMESTRIKE);
            c->Movement = 0;
            //c->AttackTime = 15;

            SetPlayerBow(c);
            vec3_t Light, Position, P, dp;

            float Matrix[3][4];
            Vector(0.f, 20.f, 0.f, P);
            AngleMatrix(o->Angle, Matrix);
            VectorRotate(P, Matrix, dp);
            VectorAdd(dp, o->Position, Position);
            Vector(0.8f, 0.9f, 1.6f, Light);
            CreateEffect(MODEL_MULTI_SHOT3, Position, o->Angle, Light, 0);
            CreateEffect(MODEL_MULTI_SHOT3, Position, o->Angle, Light, 0);

            Vector(0.f, -20.f, 0.f, P);
            Vector(0.f, 0.f, 0.f, P);
            AngleMatrix(o->Angle, Matrix);
            VectorRotate(P, Matrix, dp);
            VectorAdd(dp, o->Position, Position);

            CreateEffect(MODEL_MULTI_SHOT1, Position, o->Angle, Light, 0);
            CreateEffect(MODEL_MULTI_SHOT1, Position, o->Angle, Light, 0);
            CreateEffect(MODEL_MULTI_SHOT1, Position, o->Angle, Light, 0);

            Vector(0.f, 20.f, 0.f, P);
            AngleMatrix(o->Angle, Matrix);
            VectorRotate(P, Matrix, dp);
            VectorAdd(dp, o->Position, Position);
            CreateEffect(MODEL_MULTI_SHOT2, Position, o->Angle, Light, 0);
            CreateEffect(MODEL_MULTI_SHOT2, Position, o->Angle, Light, 0);

            Vector(0.f, -120.f, 145.f, P);
            AngleMatrix(o->Angle, Matrix);
            VectorRotate(P, Matrix, dp);
            VectorAdd(dp, o->Position, Position);

            short Key = -1;
            for (int i = 0; i < CharactersClient.Size(); i++)
            {
                if (!CharactersClient.IsValidIndex(i))
                    continue;
                CHARACTER *tc = &CharactersClient[i];
                if (tc == c)
                {
                    Key = i;
                    break;
                }
            }

            CreateEffect(MODEL_BLADE_SKILL, Position, o->Angle, Light, 1, o, Key);
            PlayBuffer(SOUND_SKILL_MULTI_SHOT);
        }
    }
    break;
    case AT_SKILL_IMPROVE_AG:
        SendRequestMagic(Skill, HeroKey);
        SetPlayerMagic(c);
        c->Movement = 0;
        break;
    case AT_SKILL_PLASMA_STORM_FENRIR: {
        int TargetX = (int)(c->TargetPosition[0] / TERRAIN_SCALE);
        int TargetY = (int)(c->TargetPosition[1] / TERRAIN_SCALE);
        if (CheckTile(c, o, Distance))
        {
            BYTE pos = CalcTargetPos(o->Position[0], o->Position[1], c->TargetPosition[0],
                                     c->TargetPosition[1]);
            WORD TKey = 0xffff;
            if (g_MovementSkill.m_iTarget != -1)
            {
                o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));

                gSkillManager.CheckSkillDelay(Hero->CurrentSkill);

                TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
                c->m_iFenrirSkillTarget = g_MovementSkill.m_iTarget;
                SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                         (BYTE)(o->Angle[2] / 360.f * 256.f), 0, pos, TKey,
                                         &o->m_bySkillSerialNum);
                c->Movement = 0;

                if (o->Type == MODEL_PLAYER)
                {
                    SetAction_Fenrir_Skill(c, o);
                }
            }
            else
            {
                c->m_iFenrirSkillTarget = -1;
            }
        }
        else
        {
            if (g_MovementSkill.m_iTarget != -1)
            {
                if (PathFinding2(c->PositionX, c->PositionY, TargetX, TargetY, &c->Path,
                                 Distance * 1.2f))
                {
                    c->Movement = true;
                }
            }
            else
            {
                Attacking = -1;
            }
        }
    }
    break;
    }

    c->SkillSuccess = true;
}

void SessionGameplayUnit::AttackKnight(CHARACTER *c, ActionSkillType Skill, float Distance)
{
    OBJECT *o = &c->Object;
    if (g_MovementSkill.m_iTarget != -1)
        CheckMovementSkillTarget(c);
    int ClassIndex = gCharacterManager.GetBaseClass(c->Class);

    int iMana, iSkillMana;
    gSkillManager.GetSkillInformation(Skill, 1, NULL, &iMana, NULL, &iSkillMana);

    int iTypeL = CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT].Type;
    int iTypeR = CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT].Type;

    if (((iTypeR != -1 && (iTypeR < ITEM_STAFF || iTypeR >= ITEM_STAFF + MAX_ITEM_INDEX) &&
          (iTypeL < ITEM_STAFF || iTypeL >= ITEM_STAFF + MAX_ITEM_INDEX)) ||
         Skill == AT_SKILL_SWELL_LIFE || Skill == AT_SKILL_SWELL_LIFE_STR ||
         Skill == AT_SKILL_SWELL_LIFE_PROFICIENCY || Skill == AT_SKILL_ADD_CRITICAL ||
         Skill == AT_SKILL_ADD_CRITICAL_STR1 || Skill == AT_SKILL_ADD_CRITICAL_STR2 ||
         Skill == AT_SKILL_ADD_CRITICAL_STR3 || Skill == AT_SKILL_PARTY_TELEPORT ||
         Skill == AT_SKILL_THUNDER_STRIKE || Skill == AT_SKILL_EARTHSHAKE ||
         Skill == AT_SKILL_EARTHSHAKE_STR || Skill == AT_SKILL_EARTHSHAKE_MASTERY ||
         Skill == AT_SKILL_BRAND_OF_SKILL || Skill == AT_SKILL_PLASMA_STORM_FENRIR ||
         Skill == AT_SKILL_FIRE_SCREAM || Skill == AT_SKILL_FIRE_SCREAM_STR ||
         Skill == AT_SKILL_GIGANTIC_STORM || Skill == AT_SKILL_CHAOTIC_DISEIER ||
         Skill == AT_SKILL_TWISTING_SLASH || Skill == AT_SKILL_TWISTING_SLASH_STR ||
         Skill == AT_SKILL_TWISTING_SLASH_STR_MG || Skill == AT_SKILL_TWISTING_SLASH_MASTERY))
    {
        bool Success = true;

        if (Skill == AT_SKILL_PARTY_TELEPORT && PartyNumber <= 0)
        {
            Success = false;
        }

        if (!g_csItemOption.IsNonWeaponSkillOrIsSkillEquipped(Skill))
        {
            Success = false;
        }

        if (Skill == AT_SKILL_PARTY_TELEPORT && g_DuelMgr.IsDuelEnabled())
        {
            Success = false;
        }

        if (Skill == AT_SKILL_PARTY_TELEPORT &&
            (IsDoppelGanger1() || IsDoppelGanger2() || IsDoppelGanger3() || IsDoppelGanger4()))
        {
            Success = false;
        }

        if (Skill == AT_SKILL_EARTHSHAKE || Skill == AT_SKILL_EARTHSHAKE_STR ||
            Skill == AT_SKILL_EARTHSHAKE_MASTERY)
        {
            BYTE t_DarkLife = 0;
            t_DarkLife = CharacterMachine->Equipment[EQUIPMENT_HELPER].Durability;
            if (t_DarkLife == 0)
                Success = false;
        }

        if (gMapManager.InChaosCastle())
        {
            if (Skill == AT_SKILL_EARTHSHAKE || Skill == AT_SKILL_EARTHSHAKE_STR ||
                Skill == AT_SKILL_EARTHSHAKE_MASTERY || Skill == AT_SKILL_RIDER ||
                (static_cast<int>(Skill) >= static_cast<int>(AT_PET_COMMAND_DEFAULT) &&
                 static_cast<int>(Skill) <= static_cast<int>(AT_PET_COMMAND_TARGET)) ||
                Skill == AT_SKILL_PLASMA_STORM_FENRIR)
            {
                Success = false;
            }
        }
        else
        {
            if (Skill == AT_SKILL_EARTHSHAKE || Skill == AT_SKILL_EARTHSHAKE_STR ||
                Skill == AT_SKILL_EARTHSHAKE_MASTERY)
            {
                BYTE t_DarkLife = 0;
                t_DarkLife = CharacterMachine->Equipment[EQUIPMENT_HELPER].Durability;
                if (t_DarkLife == 0)
                    Success = false;
            }
        }

        if (iMana > CharacterAttribute->Mana)
        {
            int Index = sessionKeeper_.GameData()->FindManaItemIndex();
            if (Index != -1)
            {
                SendRequestUse(Index, 0);
            }
            Success = false;
        }
        if (Success && iSkillMana > CharacterAttribute->SkillMana)
        {
            Success = false;
        }
        if (Success && !gSkillManager.CheckSkillDelay(Hero->CurrentSkill))
        {
            Success = false;
        }

        int iEnergy;
        gSkillManager.GetSkillInformation_Energy(Skill, &iEnergy);
        if (iEnergy > (CharacterAttribute->Energy + CharacterAttribute->AddEnergy))
        {
            Success = false;
        }
        if (ClassIndex == CLASS_DARK_LORD)
        {
            int iCharisma;
            gSkillManager.GetSkillInformation_Charisma(Skill, &iCharisma);
            if (iCharisma > (CharacterAttribute->Charisma + CharacterAttribute->AddCharisma))
            {
                Success = false;
            }
        }

        if (Success)
        {
            switch (Skill)
            {
            case AT_SKILL_TWISTING_SLASH:
            case AT_SKILL_TWISTING_SLASH_STR:
            case AT_SKILL_TWISTING_SLASH_STR_MG:
            case AT_SKILL_TWISTING_SLASH_MASTERY:
                o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));
                {
                    BYTE PathX[1];
                    BYTE PathY[1];
                    PathX[0] = (c->PositionX);
                    PathY[0] = (c->PositionY);

                    SendCharacterMove(c->Key, o->Angle[2], 1, &PathX[0], &PathY[0], TargetX,
                                      TargetY);

                    WORD TKey = 0xffff;
                    if (g_MovementSkill.m_iTarget != -1)
                    {
                        TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
                    }
                    SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                             (BYTE)(o->Angle[2] / 360.f * 256.f), 0, 0, TKey, 0);
                    SetAttackSpeed();
                    SetAction(o, PLAYER_ATTACK_SKILL_WHEEL);

                    c->Movement = 0;
                }
                break;
            case AT_SKILL_FIRE_SLASH:
            case AT_SKILL_FIRE_SLASH_STR:
                o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));
                {
                    WORD Strength;
                    const WORD notStrength = 596;
                    Strength = CharacterAttribute->Strength + CharacterAttribute->AddStrength;
                    if (Strength < notStrength)
                    {
                        break;
                    }

                    if (CheckTile(c, o, Distance))
                    {
                        BYTE byValue =
                            GetDestValue((c->PositionX), (c->PositionY), TargetX, TargetY);

                        WORD TKey = 0xffff;
                        if (g_MovementSkill.m_iTarget != -1)
                        {
                            TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
                        }
                        SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                                 (BYTE)(o->Angle[2] / 360.f * 256.f), byValue, 0,
                                                 TKey, 0);
                        SetAttackSpeed();

                        SetAction(o, PLAYER_ATTACK_SKILL_WHEEL);
                        c->Movement = 0;
                    }
                }
                break;
            case AT_SKILL_POWER_SLASH:
            case AT_SKILL_POWER_SLASH_STR:
                if (c->Helper.Type < MODEL_HORN_OF_UNIRIA ||
                    c->Helper.Type > MODEL_DARK_HORSE_ITEM &&
                        c->Helper.Type != MODEL_HORN_OF_FENRIR)
                {
                    o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));

                    if (CheckTile(c, o, Distance))
                    {
                        BYTE PathX[1];
                        BYTE PathY[1];
                        PathX[0] = (c->PositionX);
                        PathY[0] = (c->PositionY);

                        SendCharacterMove(c->Key, o->Angle[2], 1, &PathX[0], &PathY[0], TargetX,
                                          TargetY);

                        BYTE byValue =
                            GetDestValue((c->PositionX), (c->PositionY), TargetX, TargetY);

                        BYTE angle = (BYTE)((((o->Angle[2] + 180.f) / 360.f) * 255.f));
                        WORD TKey = 0xffff;
                        if (g_MovementSkill.m_iTarget != -1)
                        {
                            TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
                        }
                        SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                                 ((o->Angle[2] / 360.f) * 255), byValue, angle,
                                                 TKey, 0);
                        SetAttackSpeed();
                        if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone)
                        {
                            SetAction(o, PLAYER_FENRIR_ATTACK_MAGIC);
                        }
                        else
                        {
                            SetAction(o, PLAYER_ATTACK_TWO_HAND_SWORD_TWO);
                        }
                        c->Movement = 0;
                    }
                }
                break;
            case AT_SKILL_CHAOTIC_DISEIER: {
                o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));

                if (CheckTile(c, o, Distance))
                {
                    BYTE PathX[1];
                    BYTE PathY[1];
                    PathX[0] = (c->PositionX);
                    PathY[0] = (c->PositionY);

                    SendCharacterMove(c->Key, o->Angle[2], 1, &PathX[0], &PathY[0], TargetX,
                                      TargetY);

                    BYTE byValue = GetDestValue((c->PositionX), (c->PositionY), TargetX, TargetY);

                    BYTE angle = (BYTE)((((o->Angle[2] + 180.f) / 360.f) * 255.f));
                    WORD TKey = 0xffff;
                    if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget))
                    {
                        TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
                        if (TKey == 0xffff)
                        {
                            CHARACTER *st = &CharactersClient[g_MovementSkill.m_iTarget];
                            TKey = st->Key;
                        }
                    }
                    SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                             ((o->Angle[2] / 360.f) * 255), byValue, angle, TKey,
                                             0);
                    SetAttackSpeed();
                    //									SetAction(o, PLAYER_SKILL_FLAMESTRIKE);
                    c->Movement = 0;

                    //									SetPlayerMagic(c);

                    if (c->Helper.Type == MODEL_HORN_OF_FENRIR)
                    {
                        SetAction(o, PLAYER_FENRIR_ATTACK_DARKLORD_STRIKE);
                    }
                    else if ((c->Helper.Type >= MODEL_HORN_OF_UNIRIA) &&
                             (c->Helper.Type <= MODEL_DARK_HORSE_ITEM))
                    {
                        SetAction(o, PLAYER_ATTACK_RIDE_STRIKE);
                    }
                    else
                    {
                        SetAction(o, PLAYER_ATTACK_STRIKE);
                    }

                    vec3_t Light, Position, P, dp;

                    float Matrix[3][4];
                    Vector(0.f, -20.f, 0.f, P);
                    Vector(0.f, 0.f, 0.f, P);
                    AngleMatrix(o->Angle, Matrix);
                    VectorRotate(P, Matrix, dp);
                    VectorAdd(dp, o->Position, Position);

                    Vector(0.5f, 0.5f, 0.5f, Light);
                    for (int i = 0; i < 5; ++i)
                    {
                        CreateEffect(BITMAP_SHINY + 6, Position, o->Angle, Light, 3, o, -1, 0, 0, 0,
                                     0.5f);
                    }

                    VectorCopy(o->Position, Position);

                    for (int i = 0; i < 8; i++)
                    {
                        Position[0] = (o->Position[0] - 119.f) + (float)(rand() % 240);
                        Position[2] = (o->Position[2] + 49.f) + (float)(rand() % 60);
                        CreateJoint(BITMAP_2LINE_GHOST, Position, o->Position, o->Angle, 0, o, 20.f,
                                    o->PKKey, 0, o->m_bySkillSerialNum);
                    }
                    if (c == Hero && CharactersClient.IsValidIndex(SelectedCharacter))
                    {
                        vec3_t Pos;
                        CHARACTER *sc = &CharactersClient[SelectedCharacter];
                        VectorCopy(sc->Object.Position, Pos);
                        CreateBomb(Pos, true, 6);
                    }
                    PlayBuffer(SOUND_SKILL_CAOTIC);
                }
                else
                {
                    if (PathFinding2(c->PositionX, c->PositionY, TargetX, TargetY, &c->Path,
                                     Distance))
                    {
                        c->Movement = true;
                    }
                }
            }
            break;
            case AT_SKILL_FLAME_STRIKE: {
                o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));

                if (CheckTile(c, o, Distance))
                {
                    BYTE PathX[1];
                    BYTE PathY[1];
                    PathX[0] = (c->PositionX);
                    PathY[0] = (c->PositionY);

                    SendCharacterMove(c->Key, o->Angle[2], 1, &PathX[0], &PathY[0], TargetX,
                                      TargetY);

                    BYTE byValue = GetDestValue((c->PositionX), (c->PositionY), TargetX, TargetY);

                    BYTE angle = (BYTE)((((o->Angle[2] + 180.f) / 360.f) * 255.f));
                    WORD TKey = 0xffff;
                    if (g_MovementSkill.m_iTarget != -1)
                    {
                        TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
                    }
                    SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                             ((o->Angle[2] / 360.f) * 255), byValue, angle, TKey,
                                             0);
                    SetAttackSpeed();
                    SetAction(o, PLAYER_SKILL_FLAMESTRIKE);
                    c->Movement = 0;
                    //c->AttackTime = 15;
                }
            }
            break;
            case AT_SKILL_GIGANTIC_STORM: {
                o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));

                if (CheckTile(c, o, Distance))
                {
                    BYTE PathX[1];
                    BYTE PathY[1];
                    PathX[0] = (c->PositionX);
                    PathY[0] = (c->PositionY);

                    SendCharacterMove(c->Key, o->Angle[2], 1, &PathX[0], &PathY[0], TargetX,
                                      TargetY);

                    BYTE byValue = GetDestValue((c->PositionX), (c->PositionY), TargetX, TargetY);

                    BYTE angle = (BYTE)((((o->Angle[2] + 180.f) / 360.f) * 255.f));
                    WORD TKey = 0xffff;
                    if (g_MovementSkill.m_iTarget != -1)
                    {
                        TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
                    }
                    SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                             ((o->Angle[2] / 360.f) * 255), byValue, angle, TKey,
                                             0);
                    SetAttackSpeed();
                    SetAction(o, PLAYER_SKILL_GIGANTICSTORM);
                    c->Movement = 0;
                }
            }
            break;
            case AT_SKILL_PARTY_TELEPORT:
                if (gMapManager.IsCursedTemple() &&
                    !sessionKeeper_.GameData()->HasInventoryItem(ITEM_POTION + 64, true))
                {
                    return;
                }
                SendRequestMagic(Skill, HeroKey);
                c->Movement = 0;
                break;

            case AT_SKILL_ADD_CRITICAL:
            case AT_SKILL_ADD_CRITICAL_STR1:
            case AT_SKILL_ADD_CRITICAL_STR2:
            case AT_SKILL_ADD_CRITICAL_STR3:
                SendRequestMagic(Skill, HeroKey);
                c->Movement = 0;
                break;
            case AT_SKILL_BRAND_OF_SKILL:
                SendRequestMagic(Skill, HeroKey);
                c->Movement = 0;
                break;
            case AT_SKILL_FIRE_SCREAM:
            case AT_SKILL_FIRE_SCREAM_STR:
                if (CheckTile(c, o, Distance))
                {
                    int TargetX = (int)(c->TargetPosition[0] / TERRAIN_SCALE);
                    int TargetY = (int)(c->TargetPosition[1] / TERRAIN_SCALE);
                    BYTE byValue = GetDestValue((c->PositionX), (c->PositionY), TargetX, TargetY);

                    BYTE pos = CalcTargetPos(o->Position[0], o->Position[1], c->TargetPosition[0],
                                             c->TargetPosition[1]);
                    WORD TKey = 0xffff;
                    if (g_MovementSkill.m_iTarget != -1)
                    {
                        TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
                    }
                    o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));
                    CheckClientArrow(o);
                    SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                             (BYTE)(o->Angle[2] / 360.f * 256.f), byValue, pos,
                                             TKey, 0);

                    SetAttackSpeed();
                    {
                        if ((c->Helper.Type >= MODEL_HORN_OF_UNIRIA &&
                             c->Helper.Type <= MODEL_DARK_HORSE_ITEM) &&
                            !c->SafeZone)
                        {
                            SetAction(o, PLAYER_ATTACK_RIDE_STRIKE);
                        }
                        else if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone)
                        {
                            SetAction(o, PLAYER_FENRIR_ATTACK_DARKLORD_STRIKE);
                        }
                        else
                        {
                            SetAction(o, PLAYER_ATTACK_STRIKE);
                        }
                    }
                    c->Movement = 0;
                }
                else
                {
                    Attacking = -1;
                }
                break;
            case AT_SKILL_EARTHSHAKE:
            case AT_SKILL_EARTHSHAKE_STR:
            case AT_SKILL_EARTHSHAKE_MASTERY:
                if (c->Helper.Type != MODEL_DARK_HORSE_ITEM || c->SafeZone)
                {
                    break;
                }

            case AT_SKILL_THUNDER_STRIKE:
                if (CheckTile(c, o, Distance))
                {
                    int TargetX = (int)(c->TargetPosition[0] / TERRAIN_SCALE);
                    int TargetY = (int)(c->TargetPosition[1] / TERRAIN_SCALE);
                    BYTE byValue = GetDestValue((c->PositionX), (c->PositionY), TargetX, TargetY);

                    BYTE pos = CalcTargetPos(o->Position[0], o->Position[1], c->TargetPosition[0],
                                             c->TargetPosition[1]);
                    WORD TKey = 0xffff;
                    if (g_MovementSkill.m_iTarget != -1)
                    {
                        TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
                    }
                    o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));
                    SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                             (BYTE)(o->Angle[2] / 360.f * 256.f), byValue, pos,
                                             TKey, 0);
                    SetAttackSpeed();

                    if (Skill == AT_SKILL_THUNDER_STRIKE)
                    {
                        if (c->Helper.Type == MODEL_DARK_HORSE_ITEM && !c->SafeZone)
                        {
                            SetAction(o, PLAYER_ATTACK_RIDE_ATTACK_FLASH);
                        }
                        else if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone)
                        {
                            SetAction(o, PLAYER_FENRIR_ATTACK_DARKLORD_FLASH);
                        }
                        else
                        {
                            SetAction(o, PLAYER_SKILL_FLASH);
                        }
                    }
                    else if (Skill == AT_SKILL_EARTHSHAKE || Skill == AT_SKILL_EARTHSHAKE_STR ||
                             Skill == AT_SKILL_EARTHSHAKE_MASTERY)
                    {
                        SetAction(o, PLAYER_ATTACK_DARKHORSE);
                        PlayBuffer(SOUND_EARTH_QUAKE);
                    }
                    c->Movement = 0;
                }
                else
                {
                    Attacking = -1;
                }
                break;
            case AT_SKILL_SWELL_LIFE:
            case AT_SKILL_SWELL_LIFE_STR:
            case AT_SKILL_SWELL_LIFE_PROFICIENCY:
                SendRequestMagic(Skill, HeroKey);
                SetAction(o, PLAYER_SKILL_VITALITY);
                c->Movement = 0;
                break;
            case AT_SKILL_RAGEFUL_BLOW:
            case AT_SKILL_RAGEFUL_BLOW_STR:
            case AT_SKILL_RAGEFUL_BLOW_MASTERY: {
                o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));

                int TargetX = (int)(c->TargetPosition[0] / TERRAIN_SCALE);
                int TargetY = (int)(c->TargetPosition[1] / TERRAIN_SCALE);

                if (CheckTile(c, o, Distance))
                {
                    BYTE pos = CalcTargetPos(o->Position[0], o->Position[1], c->TargetPosition[0],
                                             c->TargetPosition[1]);
                    WORD TKey = 0xffff;
                    if (g_MovementSkill.m_iTarget != -1)
                    {
                        TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
                    }
                    SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                             (BYTE)(o->Angle[2] / 360.f * 256.f), 0, pos, TKey, 0);
                    SetAction(o, PLAYER_ATTACK_SKILL_FURY_STRIKE);
                    c->Movement = 0;
                }
                else
                {
                    Attacking = -1;
                }
            }
            break;
            case AT_SKILL_STRIKE_OF_DESTRUCTION:
            case AT_SKILL_STRIKE_OF_DESTRUCTION_STR: {
                o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));

                int TargetX = (int)(c->TargetPosition[0] / TERRAIN_SCALE);
                int TargetY = (int)(c->TargetPosition[1] / TERRAIN_SCALE);
                if (CheckTile(c, o, Distance))
                {
                    BYTE pos = CalcTargetPos(o->Position[0], o->Position[1], c->TargetPosition[0],
                                             c->TargetPosition[1]);
                    WORD TKey = 0xffff;
                    if (g_MovementSkill.m_iTarget != -1)
                    {
                        TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
                    }
                    SendRequestMagicContinue(Skill, TargetX, TargetY,
                                             (BYTE)(o->Angle[2] / 360.f * 256.f), 0, pos, TKey, 0);
                    SetAction(o, PLAYER_SKILL_BLOW_OF_DESTRUCTION);
                    c->Movement = 0;
                }
                else
                {
                    Attacking = -1;
                }
            }
            break;
            case AT_SKILL_PLASMA_STORM_FENRIR: {
                int TargetX = (int)(c->TargetPosition[0] / TERRAIN_SCALE);
                int TargetY = (int)(c->TargetPosition[1] / TERRAIN_SCALE);
                if (CheckTile(c, o, Distance))
                {
                    BYTE pos = CalcTargetPos(o->Position[0], o->Position[1], c->TargetPosition[0],
                                             c->TargetPosition[1]);
                    WORD TKey = 0xffff;
                    if (g_MovementSkill.m_iTarget != -1)
                    {
                        o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));

                        gSkillManager.CheckSkillDelay(Hero->CurrentSkill);

                        TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
                        c->m_iFenrirSkillTarget = g_MovementSkill.m_iTarget;
                        SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                                 (BYTE)(o->Angle[2] / 360.f * 256.f), 0, pos, TKey,
                                                 &o->m_bySkillSerialNum);
                        c->Movement = 0;

                        if (o->Type == MODEL_PLAYER)
                        {
                            SetAction_Fenrir_Skill(c, o);
                        }
                    }
                    else
                    {
                        c->m_iFenrirSkillTarget = -1;
                    }
                }
                else
                {
                    if (g_MovementSkill.m_iTarget != -1)
                    {
                        if (PathFinding2(c->PositionX, c->PositionY, TargetX, TargetY, &c->Path,
                                         Distance * 1.2f))
                        {
                            c->Movement = true;
                        }
                    }
                    else
                    {
                        Attacking = -1;
                    }
                }
            }
            break;
            }
        }
    }

    c->SkillSuccess = true;
}

void SessionGameplayUnit::AttackWizard(CHARACTER *c, int Skill, float Distance)
{
    OBJECT *o = &c->Object;
    int ClassIndex = gCharacterManager.GetBaseClass(c->Class);

    int iMana, iSkillMana;
    if (Skill == AT_SKILL_NOVA_BEGIN || Skill == AT_SKILL_NOVA)
    {
        gSkillManager.GetSkillInformation(AT_SKILL_NOVA, 1, NULL, &iMana, NULL, &iSkillMana);

        if (Skill == AT_SKILL_NOVA)
        {
            iSkillMana = 0;
        }
    }
    else
    {
        gSkillManager.GetSkillInformation(Skill, 1, NULL, &iMana, NULL, &iSkillMana);
    }

    int iEnergy;
    gSkillManager.GetSkillInformation_Energy(Skill, &iEnergy);
    if (iEnergy > (CharacterAttribute->Energy + CharacterAttribute->AddEnergy))
    {
        return;
    }

    if (iMana > CharacterAttribute->Mana)
    {
        int Index = sessionKeeper_.GameData()->FindManaItemIndex();
        if (Index != -1)
        {
            SendRequestUse(Index, 0);
        }
        return;
    }

    if (iSkillMana > CharacterAttribute->SkillMana)
    {
        if (Skill == AT_SKILL_NOVA_BEGIN || Skill == AT_SKILL_NOVA)
        {
            MouseRButtonPop = false;
            MouseRButtonPush = false;
            MouseRButton = false;

            MouseRButtonPress = 0;
        }
        return;
    }

    if (gSkillManager.CheckSkillDelay(Hero->CurrentSkill) == false)
    {
        return;
    }
    bool Success = CheckMovementSkillTarget(c);

    switch (Skill)
    {
    case AT_SKILL_NOVA_BEGIN: {
        SendRequestMagic(Skill, HeroKey);

        SetAttackSpeed();
        SetAction(o, PLAYER_SKILL_HELL_BEGIN);
        c->Movement = 0;
    }
        return;
    case AT_SKILL_NOVA: {
        int iTargetKey = getTargetCharacterKey(c, SelectedCharacter);
        if (iTargetKey == -1)
        {
            iTargetKey = HeroKey;
        }
        SendRequestMagic(Skill, iTargetKey);

        SetAttackSpeed();
        SetAction(o, PLAYER_SKILL_HELL_START);
        c->Movement = 0;
    }
        return;
    case AT_SKILL_SOUL_BARRIER:
    case AT_SKILL_SOUL_BARRIER_STR:
    case AT_SKILL_SOUL_BARRIER_PROFICIENCY:
        if (CharactersClient.IsValidIndex(SelectedCharacter))
        {
            if (CharactersClient[SelectedCharacter].Object.Kind != KIND_PLAYER)
            {
                Attacking = -1;
                return;
            }
            else
            {
                if (c != Hero && !g_pPartyManager->IsPartyMember(SelectedCharacter))
                    return;

                SetCharacterTarget(*c, SelectedCharacter);

                if (CharactersClient.IsValidIndex(SelectedCharacter))
                {
                    ZeroMemory(&g_MovementSkill, sizeof(g_MovementSkill));
                    g_MovementSkill.m_bMagic = TRUE;
                    g_MovementSkill.m_iSkill = Hero->CurrentSkill;
                    g_MovementSkill.m_iTarget = SelectedCharacter;
                }

                if (!CheckTile(c, o, Distance))
                {
                    if (CharactersClient.IsValidIndex(SelectedCharacter))
                    {
                        if (PathFinding2(c->PositionX, c->PositionY, TargetX, TargetY, &c->Path,
                                         Distance))
                        {
                            c->Movement = true;
                            c->MovementType = MOVEMENT_SKILL;
                            SendMove(c, o);
                        }
                    }

                    return;
                }

                SendRequestMagic(Skill, CharactersClient[g_MovementSkill.m_iTarget].Key);
            }
        }
        else
        {
            SendRequestMagic(Skill, HeroKey);
        }
        SetPlayerMagic(c);
        break;
    case AT_SKILL_HELL_FIRE:
    case AT_SKILL_HELL_FIRE_STR: {
        WORD TKey = 0xffff;
        if (g_MovementSkill.m_iTarget != -1)
        {
            TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
        }
        SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                 (BYTE)(o->Angle[2] / 360.f * 256.f), 0, 0, TKey, 0);
        SetAttackSpeed();
        SetAction(o, PLAYER_SKILL_HELL);
        c->Movement = 0;
    }
        return;
    case AT_SKILL_INFERNO:
    case AT_SKILL_INFERNO_STR:
    case AT_SKILL_INFERNO_STR_MG: {
        WORD TKey = 0xffff;
        if (g_MovementSkill.m_iTarget != -1)
        {
            TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
        }
        SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                 (BYTE)(o->Angle[2] / 360.f * 256.f), 0, 0, TKey, 0);
        SetAttackSpeed();
        SetAction(o, PLAYER_SKILL_INFERNO);
        c->Movement = 0;
    }
        return;
    case AT_SKILL_PLASMA_STORM_FENRIR: {
        int TargetX = (int)(c->TargetPosition[0] / TERRAIN_SCALE);
        int TargetY = (int)(c->TargetPosition[1] / TERRAIN_SCALE);
        if (CheckTile(c, o, Distance)) //&& CheckAttack())
        {
            BYTE pos = CalcTargetPos(o->Position[0], o->Position[1], c->TargetPosition[0],
                                     c->TargetPosition[1]);
            WORD TKey = 0xffff;
            if (g_MovementSkill.m_iTarget != -1)
            {
                o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));

                gSkillManager.CheckSkillDelay(Hero->CurrentSkill);

                TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
                c->m_iFenrirSkillTarget = g_MovementSkill.m_iTarget;
                SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                         (BYTE)(o->Angle[2] / 360.f * 256.f), 0, pos, TKey,
                                         &o->m_bySkillSerialNum);
                c->Movement = 0;

                if (o->Type == MODEL_PLAYER)
                {
                    SetAction_Fenrir_Skill(c, o);
                }
            }
            else
            {
                c->m_iFenrirSkillTarget = -1;
            }
        }
        else
        {
            if (g_MovementSkill.m_iTarget != -1)
            {
                if (PathFinding2(c->PositionX, c->PositionY, TargetX, TargetY, &c->Path,
                                 Distance * 1.2f))
                {
                    c->Movement = true;
                }
            }
            else
            {
                Attacking = -1;
            }
        }
    }
        return;
    case AT_SKILL_BLOCKING:
    case AT_SKILL_FALLING_SLASH:
    case AT_SKILL_FALLING_SLASH_STR:
    case AT_SKILL_LUNGE:
    case AT_SKILL_LUNGE_STR:
    case AT_SKILL_UPPERCUT:
    case AT_SKILL_CYCLONE:
    case AT_SKILL_CYCLONE_STR:
    case AT_SKILL_CYCLONE_STR_MG:
    case AT_SKILL_SLASH:
    case AT_SKILL_SLASH_STR:
    case AT_SKILL_IMPALE:
        return;
    case AT_SKILL_EXPANSION_OF_WIZARDRY:
    case AT_SKILL_EXPANSION_OF_WIZARDRY_STR:
    case AT_SKILL_EXPANSION_OF_WIZARDRY_MASTERY: {
        if (g_isCharacterBuff((&Hero->Object), eBuff_SwellOfMagicPower) == false)
        {
            SendRequestMagic(Skill, HeroKey);

            c->Movement = 0;
        }
    }
    break;
    }

    const int movementTargetIndex = g_MovementSkill.m_iTarget;
    const bool hasLiveMovementTarget =
        CharactersClient.IsValidIndex(movementTargetIndex) &&
        CharactersClient[movementTargetIndex].Dead == 0;

    if (!CheckTile(c, o, Distance))
    {
        if (hasLiveMovementTarget)
        {
            if (PathFinding2(c->PositionX, c->PositionY, TargetX, TargetY, &c->Path, Distance))
            {
                c->Movement = true;
                c->MovementType = MOVEMENT_SKILL;
                SendMove(c, o);
            }
        }

        if (Skill != AT_SKILL_STUN && Skill != AT_SKILL_REMOVAL_STUN && Skill != AT_SKILL_MANA &&
            Skill != AT_SKILL_INVISIBLE && Skill != AT_SKILL_REMOVAL_INVISIBLE &&
            Skill != AT_SKILL_PLASMA_STORM_FENRIR && Skill != AT_SKILL_ALICE_BERSERKER &&
            Skill != AT_SKILL_ALICE_BERSERKER_STR && Skill != AT_SKILL_ALICE_WEAKNESS &&
            Skill != AT_SKILL_ALICE_ENERVATION)
            return;
    }

    bool Wall = CheckWall((c->PositionX), (c->PositionY), TargetX, TargetY);
    if (Wall)
    {
        if (hasLiveMovementTarget)
        {
            UseSkillWizard(c, o);
        }
        if (Success)
        {
            o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));

            WORD TKey = 0xffff;
            if (g_MovementSkill.m_iTarget != -1)
            {
                TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
            }

            switch (Skill)
            {
            case AT_SKILL_STORM:
            case AT_SKILL_EVIL_SPIRIT:
            case AT_SKILL_EVIL_SPIRIT_STR:
            case AT_SKILL_EVIL_SPIRIT_STR_MG: {
                SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                         (BYTE)(o->Angle[2] / 360.f * 256.f), 0, 0, TKey,
                                         &o->m_bySkillSerialNum);
                SetPlayerMagic(c);
            }
                return;
            case AT_SKILL_FLASH: {
                SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                         (BYTE)(o->Angle[2] / 360.f * 256.f), 0, 0, TKey, 0);
                SetAttackSpeed();

                if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone)
                    SetAction(o, PLAYER_SKILL_FLASH);
                else
                    SetAction(o, PLAYER_SKILL_FLASH);

                c->Movement = 0;
                StandTime = 0;
            }
                return;
            case AT_SKILL_FLAME:
            case AT_SKILL_FLAME_STR:
            case AT_SKILL_FLAME_STR_MG: {
                SendRequestMagicContinue(Skill, (BYTE)(c->TargetPosition[0] / TERRAIN_SCALE),
                    (BYTE)(c->TargetPosition[1] / TERRAIN_SCALE),
                    (BYTE)(o->Angle[2] / 360.f * 256.f), 0, 0, TKey, 0);
                SetPlayerMagic(c);
            }
                return;
            case AT_SKILL_DECAY:
            case AT_SKILL_DECAY_STR:
            case AT_SKILL_ICE_STORM: {
                SendRequestMagicContinue(Skill, (BYTE)(c->TargetPosition[0] / TERRAIN_SCALE),
                                         (BYTE)(c->TargetPosition[1] / TERRAIN_SCALE),
                                         (BYTE)(o->Angle[2] / 360.f * 256.f), 0, 0, TKey, 0);
                SetPlayerMagic(c);
                c->Movement = 0;
            }
                return;
            }
        }
    }
    if (ClassIndex == CLASS_WIZARD && Success)
    {
        switch (Skill)
        {
        case AT_SKILL_TELEPORT_ALLY:
            if (gMapManager.IsCursedTemple() &&
                !sessionKeeper_.GameData()->HasInventoryItem(ITEM_POTION + 64, true))
            {
                return;
            }
            if (CharactersClient.IsValidIndex(SelectedCharacter))
            {
                if (!g_pPartyManager->IsPartyMember(SelectedCharacter))
                    return;

                if (sessionKeeper_.GameData()->GetPickedItem())
                {
                    return;
                }

                CHARACTER *tc = &CharactersClient[SelectedCharacter];
                OBJECT *to = &tc->Object;
                bool Success = false;
                if (to->Type == MODEL_PLAYER)
                {
                    if (to->CurrentAction != PLAYER_SKILL_TELEPORT)
                        Success = true;
                }
                else
                {
                    if (to->CurrentAction != MONSTER01_SHOCK)
                        Success = true;
                }
                if (Success && to->Teleport != TELEPORT_BEGIN && to->Teleport != TELEPORT &&
                    to->Alpha >= 0.7f)
                {
                    int Wall, indexX, indexY, TargetIndex, count = 0;
                    int PositionX = (int)(c->Object.Position[0] / TERRAIN_SCALE);
                    int PositionY = (int)(c->Object.Position[1] / TERRAIN_SCALE);

                    while (1)
                    {
                        indexX = rand() % 3;
                        indexY = rand() % 3;

                        if (indexX != 1 || indexY != 1)
                        {
                            TargetX = (PositionX - 1) + indexX;
                            TargetY = (PositionY - 1) + indexY;

                            TargetIndex = TERRAIN_INDEX(TargetX, TargetY);

                            Wall = TerrainWall[TargetIndex];

                            if ((Wall & TW_ACTION) == TW_ACTION)
                            {
                                Wall -= TW_ACTION;
                            }
                            if (gMapManager.ContextMap() == WD_30BATTLECASTLE)
                            {
                                int ax = (Hero->PositionX);
                                int ay = (Hero->PositionY);
                                if ((ax >= 150 && ax <= 200) && (ay >= 180 && ay <= 230))
                                    Wall = 0;
                            }
                            if (Wall == 0)
                                break;

                            count++;
                        }

                        if (count > 10)
                            return;
                    }
                    to->SetAngleZ(CreateAngle2D(to->Position, tc->TargetPosition));

                    SocketClient->ToGameServer()->SendTeleportTarget(tc->Key, TargetX, TargetY);
                    SetPlayerTeleport(tc);
                }
            }
            return;

        case AT_SKILL_TELEPORT: {
            if (sessionKeeper_.GameData()->GetPickedItem() || g_isCharacterBuff(o, eDeBuff_Stun) ||
                g_isCharacterBuff(o, eDeBuff_Sleep))
            {
                return;
            }

            WORD byHeroPriorSkill = gSkillManager.GetHeroPriorSkill();
            if (c == Hero && byHeroPriorSkill == AT_SKILL_NOVA)
            {
                gSkillManager.SetHeroPriorSkill(AT_SKILL_TELEPORT);
                SendRequestMagic(byHeroPriorSkill, HeroKey);
                SetAttackSpeed();
                SetAction(&Hero->Object, PLAYER_SKILL_HELL_BEGIN);
                Hero->Movement = 0;
                return;
            }

            bool Success = false;
            if (o->Type == MODEL_PLAYER)
            {
                if (o->CurrentAction != PLAYER_SKILL_TELEPORT)
                    Success = true;
            }
            else
            {
                if (o->CurrentAction != MONSTER01_SHOCK)
                    Success = true;
            }
            if (Success && o->Teleport != TELEPORT_BEGIN && o->Teleport != TELEPORT &&
                o->Alpha >= 0.7f)
            {
                int TargetIndex = TERRAIN_INDEX_REPEAT(TargetX, TargetY);
                int Wall = TerrainWall[TargetIndex];
                if ((Wall & TW_ACTION) == TW_ACTION)
                    Wall -= TW_ACTION;
                if ((Wall & TW_HEIGHT) == TW_HEIGHT)
                    Wall -= TW_HEIGHT;
                if (Wall == 0)
                {
                    o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));
                    bool bResult;
                    //SendRequestMagicTeleport(&bResult, 0, TargetX, TargetY);
                    if (g_bWhileMovingZone || (GetTickCount() - g_dwLatestZoneMoving < 3000))
                    {
                        bResult = false;
                    }
                    else
                    {
                        SocketClient->ToGameServer()->SendEnterGateRequest(0, TargetX, TargetY);
                        bResult = true;
                    }

                    if (bResult)
                    {
                        if (g_isCharacterBuff(o, eDeBuff_Stun))
                        {
                            UnRegisterBuff(eDeBuff_Stun, o);
                        }

                        if (g_isCharacterBuff(o, eDeBuff_Sleep))
                        {
                            UnRegisterBuff(eDeBuff_Sleep, o);
                        }

                        SetPlayerTeleport(c);
                    }
                }
            }
        }
            return;
        }
    }
    else if (ClassIndex == CLASS_SUMMONER && Success)
    {
        if (SendRequestSummonSkill(Skill, c, o) == TRUE)
        {
            return;
        }

        int iEnergy;
        gSkillManager.GetSkillInformation_Energy(Skill, &iEnergy);
        if (iEnergy > (CharacterAttribute->Energy + CharacterAttribute->AddEnergy))
        {
            return;
        }

        switch (Skill)
        {
        case AT_SKILL_ALICE_THORNS: {
            if (SelectedCharacter == -1 ||
                CharactersClient[SelectedCharacter].Object.Kind != KIND_PLAYER)
            {
                LetHeroStop();

                switch (c->Helper.Type)
                {
                case MODEL_HORN_OF_UNIRIA:
                    SetAction(o, PLAYER_SKILL_SLEEP_UNI);
                    break;
                case MODEL_HORN_OF_DINORANT:
                    SetAction(o, PLAYER_SKILL_SLEEP_DINO);
                    break;
                case MODEL_HORN_OF_FENRIR:
                    SetAction(o, PLAYER_SKILL_SLEEP_FENRIR);
                    break;
                default:
                    SetAction(o, PLAYER_SKILL_SLEEP);
                    break;
                }
                SendRequestMagic(Skill, HeroKey);
            }

            else if (0 == CharactersClient[SelectedCharacter].Dead &&
                     CharactersClient[SelectedCharacter].Object.Kind == KIND_PLAYER)
            {
                TargetX = (int)(CharactersClient[g_MovementSkill.m_iTarget].Object.Position[0] /
                                TERRAIN_SCALE);
                TargetY = (int)(CharactersClient[g_MovementSkill.m_iTarget].Object.Position[1] /
                                TERRAIN_SCALE);

                if (CheckTile(c, o, Distance) && c->SafeZone == false)
                {
                    bool bNoneWall = CheckWall((c->PositionX), (c->PositionY), TargetX, TargetY);
                    if (bNoneWall)
                    {
                        UseSkillSummon(c, o);
                    }
                }
                else
                {
                    if (PathFinding2(c->PositionX, c->PositionY, TargetX, TargetY, &c->Path,
                                     Distance))
                    {
                        c->Movement = true;
                    }
                }
            }
        }
        break;
        case AT_SKILL_ALICE_BERSERKER:
        case AT_SKILL_ALICE_BERSERKER_STR:
        case AT_SKILL_ALICE_WEAKNESS:
        case AT_SKILL_ALICE_ENERVATION:
            UseSkillSummon(c, o);
            break;
        case AT_SKILL_LIGHTNING_SHOCK:
        case AT_SKILL_LIGHTNING_SHOCK_STR: {
            o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));

            if (CheckTile(c, o, Distance))
            {
                BYTE PathX[1];
                BYTE PathY[1];
                PathX[0] = (c->PositionX);
                PathY[0] = (c->PositionY);
                SendCharacterMove(c->Key, o->Angle[2], 1, &PathX[0], &PathY[0], TargetX, TargetY);

                BYTE byValue = GetDestValue((c->PositionX), (c->PositionY), TargetX, TargetY);

                BYTE angle = (BYTE)((((o->Angle[2] + 180.f) / 360.f) * 255.f));
                WORD TKey = 0xffff;
                if (g_MovementSkill.m_iTarget != -1)
                {
                    TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
                }
                SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                         ((o->Angle[2] / 360.f) * 255), byValue, angle, TKey, 0);
                SetAttackSpeed();
                SetAction(o, PLAYER_SKILL_LIGHTNING_SHOCK);
                c->Movement = 0;
            }
        }
        break;
        }

        switch (Skill)
        {
        case AT_SKILL_ALICE_DRAINLIFE:
        case AT_SKILL_ALICE_DRAINLIFE_STR:
        case AT_SKILL_ALICE_LIGHTNINGORB:
        case AT_SKILL_ALICE_CHAINLIGHTNING:
        case AT_SKILL_ALICE_CHAINLIGHTNING_STR: {
            SetCharacterTarget(*c, SelectedCharacter);
            if (CharactersClient.IsValidIndex(SelectedCharacter) &&
                CharactersClient[SelectedCharacter].Dead == 0)
            {
                TargetX =
                    (int)(CharactersClient[SelectedCharacter].Object.Position[0] / TERRAIN_SCALE);
                TargetY =
                    (int)(CharactersClient[SelectedCharacter].Object.Position[1] / TERRAIN_SCALE);

                if (CheckAttack() == true)
                {
                    if (CheckTile(c, o, Distance) && c->SafeZone == false)
                    {
                        bool bNoneWall =
                            CheckWall((c->PositionX), (c->PositionY), TargetX, TargetY);
                        if (bNoneWall)
                        {
                            UseSkillSummon(c, o);
                        }
                    }
                    else
                    {
                        if (PathFinding2(c->PositionX, c->PositionY, TargetX, TargetY, &c->Path,
                                         Distance))
                        {
                            c->Movement = true;
                        }
                    }
                }
            }
        }
        break;
        case AT_SKILL_ALICE_SLEEP:
        case AT_SKILL_ALICE_SLEEP_STR:
        case AT_SKILL_ALICE_BLIND: {
            if (CharactersClient.IsValidIndex(SelectedCharacter) &&
                CharactersClient[SelectedCharacter].Object.Kind == KIND_PLAYER)
            {
                if (gMapManager.InChaosCastle() == true || gMapManager.IsCursedTemple() == true ||
                    (gMapManager.InBattleCastle() == true && IsBattleCastleStart() == true) ||
                    g_DuelMgr.IsDuelEnabled())
                {
                }
                else
                {
                    break;
                }
            }
            if (CharactersClient.IsValidIndex(SelectedCharacter) &&
                CharactersClient[SelectedCharacter].Dead == 0)
            {
                TargetX =
                    (int)(CharactersClient[SelectedCharacter].Object.Position[0] / TERRAIN_SCALE);
                TargetY =
                    (int)(CharactersClient[SelectedCharacter].Object.Position[1] / TERRAIN_SCALE);

                if (CheckAttack() == true)
                {
                    if (CheckTile(c, o, Distance) && c->SafeZone == false)
                    {
                        bool bNoneWall =
                            CheckWall((c->PositionX), (c->PositionY), TargetX, TargetY);
                        if (bNoneWall)
                        {
                            UseSkillSummon(c, o);
                        }
                    }
                    else
                    {
                        if (PathFinding2(c->PositionX, c->PositionY, TargetX, TargetY, &c->Path,
                                         Distance))
                        {
                            c->Movement = true;
                        }
                    }
                }
            }
        }
        break;
        }
    }

    c->SkillSuccess = true;
}

void SessionGameplayUnit::AttackCommon(CHARACTER *c, int Skill, float Distance)
{
    OBJECT *o = &c->Object;

    int ClassIndex = gCharacterManager.GetBaseClass(c->Class);

    int iMana, iSkillMana;
    gSkillManager.GetSkillInformation(Skill, 1, NULL, &iMana, NULL, &iSkillMana);

    if (o->Type == MODEL_PLAYER)
    {
        bool Success = true;

        if (iMana > CharacterAttribute->Mana)
        {
            int Index = sessionKeeper_.GameData()->FindManaItemIndex();
            if (Index != -1)
            {
                SendRequestUse(Index, 0);
            }
            Success = false;
        }
        if (Success && iSkillMana > CharacterAttribute->SkillMana)
        {
            Success = false;
        }
        if (Success && !gSkillManager.CheckSkillDelay(Hero->CurrentSkill))
        {
            Success = false;
        }

        int iEnergy;
        gSkillManager.GetSkillInformation_Energy(Skill, &iEnergy);
        if (Success && iEnergy > (CharacterAttribute->Energy + CharacterAttribute->AddEnergy))
        {
            Success = false;
        }

        switch (Skill)
        {
        case AT_SKILL_STUN: {
            //	                        if ( CheckTile( c, o, Distance ) )
            {
                o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));

                int TargetX = (int)(c->TargetPosition[0] / TERRAIN_SCALE);
                int TargetY = (int)(c->TargetPosition[1] / TERRAIN_SCALE);
                BYTE byValue = GetDestValue((c->PositionX), (c->PositionY), TargetX, TargetY);

                BYTE pos = CalcTargetPos(o->Position[0], o->Position[1], c->TargetPosition[0],
                                         c->TargetPosition[1]);
                WORD TKey = 0xffff;
                if (g_MovementSkill.m_iTarget != -1)
                {
                    TKey = getTargetCharacterKey(c, g_MovementSkill.m_iTarget);
                }
                SendRequestMagicContinue(Skill, (c->PositionX), (c->PositionY),
                                         (BYTE)(o->Angle[2] / 360.f * 256.f), byValue, pos, TKey,
                                         0);
                SetAttackSpeed();

                if (c->Helper.Type == MODEL_DARK_HORSE_ITEM && !c->SafeZone)
                {
                    SetAction(o, PLAYER_ATTACK_RIDE_ATTACK_MAGIC);
                }
                else if (c->Helper.Type == MODEL_HORN_OF_UNIRIA && !c->SafeZone)
                {
                    SetAction(o, PLAYER_SKILL_RIDER);
                }
                else if (c->Helper.Type == MODEL_HORN_OF_DINORANT && !c->SafeZone)
                {
                    SetAction(o, PLAYER_SKILL_RIDER_FLY);
                }
                else if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone)
                {
                    SetAction(o, PLAYER_FENRIR_ATTACK_MAGIC);
                }
                else
                {
                    SetAction(o, PLAYER_SKILL_VITALITY);
                }
                c->Movement = 0;
            }
        }
        break;

        case AT_SKILL_REMOVAL_STUN: {
            if (SelectedCharacter == -1)
            {
                SendRequestMagic(Skill, HeroKey);
            }
            else
            {
                SendRequestMagic(Skill, CharactersClient[SelectedCharacter].Key);
            }

            if (c->Helper.Type == MODEL_DARK_HORSE_ITEM && !c->SafeZone)
            {
                SetAction(o, PLAYER_ATTACK_RIDE_ATTACK_MAGIC);
            }
            else if (c->Helper.Type == MODEL_HORN_OF_UNIRIA && !c->SafeZone)
            {
                SetAction(o, PLAYER_SKILL_RIDER);
            }
            else if (c->Helper.Type == MODEL_HORN_OF_DINORANT && !c->SafeZone)
            {
                SetAction(o, PLAYER_SKILL_RIDER_FLY);
            }
            else if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone)
            {
                SetAction(o, PLAYER_FENRIR_ATTACK_MAGIC);
            }
            else
            {
                SetAction(o, PLAYER_ATTACK_REMOVAL);
            }
            c->Movement = 0;
        }
        break;

        case AT_SKILL_MANA:
            if (SelectedCharacter == -1)
            {
                SendRequestMagic(Skill, HeroKey);
            }
            else
            {
                SendRequestMagic(Skill, CharactersClient[SelectedCharacter].Key);
            }

            if (c->Helper.Type == MODEL_DARK_HORSE_ITEM && !c->SafeZone)
            {
                SetAction(o, PLAYER_ATTACK_RIDE_ATTACK_MAGIC);
            }
            else if (c->Helper.Type == MODEL_HORN_OF_UNIRIA && !c->SafeZone)
            {
                SetAction(o, PLAYER_SKILL_RIDER);
            }
            else if (c->Helper.Type == MODEL_HORN_OF_DINORANT && !c->SafeZone)
            {
                SetAction(o, PLAYER_SKILL_RIDER_FLY);
            }
            else if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone)
            {
                SetAction(o, PLAYER_FENRIR_ATTACK_MAGIC);
            }
            else
                SetAction(o, PLAYER_SKILL_VITALITY);
            c->Movement = 0;
            break;

        case AT_SKILL_INVISIBLE:

            if (SelectedCharacter == -1)
            {
                SendRequestMagic(Skill, HeroKey);
            }
            else
            {
                if (CharactersClient[SelectedCharacter].Object.Kind == KIND_PLAYER)
                    SendRequestMagic(Skill, CharactersClient[SelectedCharacter].Key);
            }

            if (c->Helper.Type == MODEL_DARK_HORSE_ITEM && !c->SafeZone)
            {
                SetAction(o, PLAYER_ATTACK_RIDE_ATTACK_MAGIC);
            }
            else if (c->Helper.Type == MODEL_HORN_OF_UNIRIA && !c->SafeZone)
            {
                SetAction(o, PLAYER_SKILL_RIDER);
            }
            else if (c->Helper.Type == MODEL_HORN_OF_DINORANT && !c->SafeZone)
            {
                SetAction(o, PLAYER_SKILL_RIDER_FLY);
            }
            else if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone)
            {
                SetAction(o, PLAYER_FENRIR_ATTACK_MAGIC);
            }
            else
                SetAction(o, PLAYER_SKILL_VITALITY);
            c->Movement = 0;
            break;

        case AT_SKILL_REMOVAL_INVISIBLE:
            if (SelectedCharacter == -1)
            {
                SendRequestMagic(Skill, HeroKey);
            }
            else
            {
                SendRequestMagic(Skill, CharactersClient[SelectedCharacter].Key);
            }

            if (c->Helper.Type == MODEL_DARK_HORSE_ITEM && !c->SafeZone)
            {
                SetAction(o, PLAYER_ATTACK_RIDE_ATTACK_MAGIC);
            }
            else if (c->Helper.Type == MODEL_HORN_OF_UNIRIA && !c->SafeZone)
            {
                SetAction(o, PLAYER_SKILL_RIDER);
            }
            else if (c->Helper.Type == MODEL_HORN_OF_DINORANT && !c->SafeZone)
            {
                SetAction(o, PLAYER_SKILL_RIDER_FLY);
            }
            else if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone)
            {
                SetAction(o, PLAYER_FENRIR_ATTACK_MAGIC);
            }
            else
                SetAction(o, PLAYER_ATTACK_REMOVAL);
            c->Movement = 0;
            break;

        case AT_SKILL_REMOVAL_BUFF:

            if (SelectedCharacter == -1)
            {
                SendRequestMagic(Skill, HeroKey);
            }
            else
            {
                SendRequestMagic(Skill, CharactersClient[SelectedCharacter].Key);
            }

            if (c->Helper.Type == MODEL_DARK_HORSE_ITEM && !c->SafeZone)
            {
                SetAction(o, PLAYER_ATTACK_RIDE_ATTACK_MAGIC);
            }
            else if (c->Helper.Type == MODEL_HORN_OF_UNIRIA && !c->SafeZone)
            {
                SetAction(o, PLAYER_SKILL_RIDER);
            }
            else if (c->Helper.Type == MODEL_HORN_OF_DINORANT && !c->SafeZone)
            {
                SetAction(o, PLAYER_SKILL_RIDER_FLY);
            }
            else if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone)
            {
                SetAction(o, PLAYER_FENRIR_ATTACK_MAGIC);
            }
            else
                SetAction(o, PLAYER_SKILL_VITALITY);
            c->Movement = 0;
            break;
        }
    }
    c->SkillSuccess = true;
}

CSBaseMatch::CSBaseMatch(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), m_byMatchEventType(0),
      m_iMatchCountDownType(TYPE_MATCH_NONE), m_matchCountDownStart(MatchClock::time_point{}),
      m_byMatchType(0), m_iMatchTimeMax(-1), m_iMatchTime(-1), m_iMaxKillMonster(-1),
      m_iKillMonster(-1), m_iNumResult(0), m_iMyResult(0), m_MatchResult{}
{
}

void CSBaseMatch::clearMatchInfo(void)
{
    m_byMatchType = 0;
    m_iMatchTimeMax = -1;
    m_iMatchTime = -1;
    m_iMaxKillMonster = -1;
    m_iKillMonster = -1;
    SetPosition(REFERENCE_WIDTH - 230 / 2, 100);
}

bool CSBaseMatch::getEqualMonster(int addV)
{
    if (m_iKillMonster <= (m_iMaxKillMonster + addV))
        return true;

    return false;
}

void CSBaseMatch::StartMatchCountDown(int iType)
{
    UpdateCountdown();
    if (m_iMatchCountDownType >= TYPE_MATCH_DOPPELGANGER_ENTER_CLOSE &&
        m_iMatchCountDownType <= TYPE_MATCH_DOPPELGANGER_CLOSE)
    {
        if (!(iType >= TYPE_MATCH_DOPPELGANGER_ENTER_CLOSE &&
              iType <= TYPE_MATCH_DOPPELGANGER_CLOSE) &&
            iType != TYPE_MATCH_NONE)
        {
            return;
        }
    }
    m_iMatchCountDownType = static_cast<MATCH_TYPE>(iType);
    m_matchCountDownStart = MatchClock::now();
}
void CSBaseMatch::SetMatchInfo(const std::uint8_t byType, const int iMaxTime, const int iTime,
                               const int iMaxMonster, const int iKillMonster)
{
    m_byMatchType = byType;
    m_iMatchTimeMax = iMaxTime;
    m_iMatchTime = iTime;
    m_iMaxKillMonster = iMaxMonster;
    m_iKillMonster = iKillMonster;
    SetPosition(REFERENCE_WIDTH - 230 / 2, 100);
}

void CSBaseMatch::UpdateCountdown(MatchClock::time_point now)
{
    if (m_iMatchCountDownType > TYPE_MATCH_NONE && m_iMatchCountDownType < TYPE_MATCH_END &&
        now - m_matchCountDownStart >= EventMatchDetail::kMatchCountdownDuration)
        m_iMatchCountDownType = TYPE_MATCH_NONE;
}

void CSBaseMatch::Update()
{
    UpdateCountdown();
    UpdateMatchState();
}

void CSBaseMatch::SetPosition(int ix, int iy)
{
    m_PosResult.x = ix;
    m_PosResult.y = iy;
}

void CSDevilSquareMatch::SetMatchResult(const int iNumDevilRank, const int iMyRank,
                                        const MatchResult *pMatchResult, const int Success)
{
    if (iNumDevilRank >= 200)
    {
        return;
    }

    m_iNumResult = iNumDevilRank;
    m_iMyResult = iMyRank;

    memcpy(m_MatchResult, pMatchResult, m_iNumResult * sizeof(MatchResult));

    SEASON3B::TMsgBoxLayoutContainer<SEASON3B::CDevilSquareRankMsgBoxLayout> msgBoxLayout(
        SessionOrigin());
    SEASON3B::CreateMessageBox(msgBoxLayout);
}

void CSDevilSquareMatch::UpdateMatchState(void)
{
    return;
}

void CSDevilSquareMatch::SetMatchGameCommand(const LPPRECEIVE_MATCH_GAME_STATE data)
{
    return;
}

void CCursedTempleMatch::SetMatchGameCommand(const LPPRECEIVE_MATCH_GAME_STATE data)
{
    return;
}

void CCursedTempleMatch::SetMatchResult(const int iNumDevilRank, const int iMyRank,
                                        const MatchResult *pMatchResult, const int Success)
{
    return;
}

void CCursedTempleMatch::UpdateMatchState()
{
    return;
}

void CCursedTempleMatch::RenderMatchResult()
{
    return;
}
void SessionGameplayUnit::Damage(vec3_t soPosition, CHARACTER *tc, float AttackRange,
                                 int AttackPoint, bool Hit)
{
    return;
    if (tc == NULL)
        return;

    OBJECT *to = &tc->Object;

    vec3_t Position;
    if (AttackPoint > 0)
    {
        vec3_t Range;
        VectorSubtract(soPosition, to->Position, Range);
        VectorMA(to->Position, 0.3f, Range, Position);
        Position[2] += 80.f;
        vec3_t Light;
        Vector(1.f, 1.f, 1.f, Light);
        for (int i = 0; i < 40; i++)
        {
            CreateParticle(BITMAP_SPARK, Position, to->Angle, Light);
            vec3_t Angle;
            Vector(-Random.RangeFloat(30, 89), 0.f, Random.RangeFloat(0, 359), Angle);
            CreateJoint(BITMAP_JOINT_SPARK, Position, Position, Angle);
        }
        CreateParticle(BITMAP_SPARK + 1, Position, to->Angle, Light);
    }

    //point
    VectorCopy(to->Position, Position);
    Position[2] += 130.f + to->CollisionRange * 0.5f;
    vec3_t Color;
    Vector(1.f, 0.1f, 0.1f, Color);
    CreatePoint(Position, AttackPoint, Color);
}

#ifdef _PVP_ADD_MOVE_SCROLL
extern CMurdererMove g_MurdererMove;
#endif // _PVP_ADD_MOVE_SCROLL

// Latched when a click opens an NPC conversation while the button is still held.
// The world click handler ignores the held button until it is physically released, so the
// same click can't fall through to ground movement and instantly close the NPC window.
// A fresh press (e.g. deliberately clicking the ground to walk away) still works normally.
// 4 Seconds
bool SessionGameplayUnit::CheckAttack_Fenrir(CHARACTER *c)
{
    if (sessionKeeper_.GameData()->GetPickedItem())
    {
        return false;
    }

    if (gMapManager.InChaosCastle() == true && c != Hero)
    {
        return true;
    }
    else if (IsStrifeMap(gMapManager.ContextMap()) && c != Hero &&
             c->m_byGensInfluence != Hero->m_byGensInfluence)
    {
        if (((wcscmp(GuildMark[Hero->GuildMarkIndex].GuildName,
                     GuildMark[c->GuildMarkIndex].GuildName) == 0) ||
             (g_pPartyManager->IsPartyMember(SelectedCharacter))) &&
            (IsKeyDown(VK_CONTROL)))
        {
            return true;
        }
        else if ((wcscmp(GuildMark[Hero->GuildMarkIndex].GuildName,
                         GuildMark[c->GuildMarkIndex].GuildName) != 0) &&
                 !g_pPartyManager->IsPartyMember(SelectedCharacter))
        {
            return true;
        }
    }
    if (c->Object.Kind == KIND_MONSTER)
    {
        if (EnableGuildWar && EnableSoccer)
        {
            return true;
        }
        else if (EnableGuildWar)
        {
            return false;
        }

        return true;
    }
    else if (c->Object.Kind == KIND_PLAYER)
    {
        if (sessionKeeper_.MuHelper()->IsSelfDefenseTarget(c->Key))
        {
            return true;
        }

        if (IsBattleCastleStart())
        {
            if ((Hero->EtcPart == PARTS_ATTACK_KING_TEAM_MARK ||
                 Hero->EtcPart == PARTS_ATTACK_TEAM_MARK ||
                 Hero->EtcPart == PARTS_ATTACK_KING_TEAM_MARK2 ||
                 Hero->EtcPart == PARTS_ATTACK_KING_TEAM_MARK3 ||
                 Hero->EtcPart == PARTS_ATTACK_TEAM_MARK2 ||
                 Hero->EtcPart == PARTS_ATTACK_TEAM_MARK3) &&
                (c->EtcPart == PARTS_DEFENSE_KING_TEAM_MARK ||
                 c->EtcPart == PARTS_DEFENSE_TEAM_MARK))
            {
                if (!g_isCharacterBuff((&c->Object), eBuff_Cloaking))
                {
                    return true;
                }
                else
                {
                    return false;
                }
            }
            else if ((Hero->EtcPart == PARTS_DEFENSE_KING_TEAM_MARK ||
                      Hero->EtcPart == PARTS_DEFENSE_TEAM_MARK) &&
                     (c->EtcPart == PARTS_ATTACK_KING_TEAM_MARK ||
                      c->EtcPart == PARTS_ATTACK_TEAM_MARK ||
                      c->EtcPart == PARTS_ATTACK_KING_TEAM_MARK2 ||
                      c->EtcPart == PARTS_ATTACK_KING_TEAM_MARK3 ||
                      c->EtcPart == PARTS_ATTACK_TEAM_MARK2 ||
                      c->EtcPart == PARTS_ATTACK_TEAM_MARK3))
            {
                if (!g_isCharacterBuff((&c->Object), eBuff_Cloaking))
                {
                    return true;
                }
                else
                {
                    return false;
                }
            }
            else if (g_isCharacterBuff((&Hero->Object), eBuff_CastleRegimentAttack1) ||
                     g_isCharacterBuff((&Hero->Object), eBuff_CastleRegimentAttack2) ||
                     g_isCharacterBuff((&Hero->Object), eBuff_CastleRegimentAttack3))
            {
                OBJECT *o = &c->Object;
                if (!g_isCharacterBuff(o, eBuff_CastleRegimentAttack1) &&
                    !g_isCharacterBuff(o, eBuff_CastleRegimentAttack2) &&
                    !g_isCharacterBuff(o, eBuff_CastleRegimentAttack3))
                {
                    if (!g_isCharacterBuff((&c->Object), eBuff_Cloaking))
                    {
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                }
            }
            else if (g_isCharacterBuff((&Hero->Object), eBuff_CastleRegimentDefense))
            {
                OBJECT *o = &c->Object;

                if (!g_isCharacterBuff(o, eBuff_CastleRegimentDefense))
                {
                    if (!g_isCharacterBuff((&c->Object), eBuff_Cloaking))
                    {
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                }
            }
        }

        if (c->GuildRelationShip == GR_RIVAL || c->GuildRelationShip == GR_RIVALUNION) //??? ??
        {
            return true;
        }

        if (EnableGuildWar && c->PK >= PVP_MURDERER2 && c->GuildMarkIndex != -1 &&
            wcscmp(GuildMark[Hero->GuildMarkIndex].GuildName,
                   GuildMark[c->GuildMarkIndex].GuildName) == 0)
        {
            return false;
        }

        else if (g_DuelMgr.IsDuelEnabled())
        {
            if (g_DuelMgr.IsDuelPlayer(c, DUEL_ENEMY))
            {
                return true;
            }
            else
            {
                return false;
            }
        }
        else if (EnableGuildWar)
        {
            if (c->GuildTeam == 2 && c != Hero)
            {
                return true;
            }
            else
            {
                return false;
            }
        }
        else if (c->PK >= PVP_MURDERER2 || (IsKeyDown(VK_CONTROL) && c != Hero))
        {
            return true;
        }
        else if (gMapManager.IsCursedTemple() && !cursedTemple_.IsPartyMember(SelectedCharacter))
        {
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }

    return false;
}

bool SessionGameplayUnit::CheckAttack()
{
    if (sessionKeeper_.GameData()->GetPickedItem())
    {
        return false;
    }

    if (!CharactersClient.IsValidIndex(SelectedCharacter))
    {
        return false;
    }

    if (IsGMCharacter() && IsNonAttackGM() == true)
    {
        return false;
    }

    CHARACTER *c = &CharactersClient[SelectedCharacter];

    if (c->Dead > 0)
    {
        return false;
    }

    if (gMapManager.InChaosCastle() == true && c != Hero)
    {
        return true;
    }
    else if (IsStrifeMap(gMapManager.ContextMap()) && c != Hero &&
             c->m_byGensInfluence != Hero->m_byGensInfluence)
    {
        if (g_pCommandWindow->GetMouseCursor() == CURSOR_IDSELECT)
        {
            return false;
        }
        if (IsKeyDown(VK_MENU))
        {
            return false;
        }

        if (IsKeyDown(VK_CONTROL))
        {
            if (EnableGuildWar)
            {
                if (c->GuildTeam == 2 && c != Hero)
                    return true;
                else
                    return false;
            }
            else
                return true;
        }
        else if (wcscmp(GuildMark[Hero->GuildMarkIndex].GuildName,
                        GuildMark[c->GuildMarkIndex].GuildName) == 0)
        {
            if (g_pPartyManager->IsPartyMember(SelectedCharacter))
            {
                return false;
            }
            if (EnableGuildWar)
            {
                if (c->GuildTeam == 2 && c != Hero)
                    return true;
                else
                    return false;
            }
            if (c->GuildRelationShip == GR_NONE)
                return true;
            else
                return false;
        }
        else if ((c->GuildRelationShip == GR_UNION) || (c->GuildRelationShip == GR_UNIONMASTER))
        {
            return false;
        }
        else if (EnableGuildWar)
        {
            if (c->GuildTeam == 2 && c != Hero)
                return true;
            else
                return false;
        }
        else if (g_pPartyManager->IsPartyMember(SelectedCharacter))
        {
            if ((c->GuildRelationShip == GR_RIVAL) || (c->GuildRelationShip == GR_RIVALUNION))
            {
                return true;
            }
            else
                return false;
        }
        else
        {
            return true;
        }
    }

    if (c->Object.Kind == KIND_MONSTER)
    {
        if (EnableGuildWar && EnableSoccer)
        {
            return true;
        }
        else if (EnableGuildWar)
        {
            return false;
        }
        else if (g_isCharacterBuff((&Hero->Object), eBuff_DuelWatch))
        {
            return false;
        }

        return true;
    }
    else if (c->Object.Kind == KIND_PLAYER)
    {
        if (sessionKeeper_.MuHelper()->IsSelfDefenseTarget(c->Key))
        {
            return true;
        }

        if (IsBattleCastleStart())
        {
            if ((Hero->EtcPart == PARTS_ATTACK_KING_TEAM_MARK ||
                 Hero->EtcPart == PARTS_ATTACK_TEAM_MARK ||
                 Hero->EtcPart == PARTS_ATTACK_KING_TEAM_MARK2 ||
                 Hero->EtcPart == PARTS_ATTACK_KING_TEAM_MARK3 ||
                 Hero->EtcPart == PARTS_ATTACK_TEAM_MARK2 ||
                 Hero->EtcPart == PARTS_ATTACK_TEAM_MARK3) &&
                (c->EtcPart == PARTS_DEFENSE_KING_TEAM_MARK ||
                 c->EtcPart == PARTS_DEFENSE_TEAM_MARK))
            {
                if (!g_isCharacterBuff((&c->Object), eBuff_Cloaking))
                {
                    return true;
                }
                else
                {
                    return false;
                }
            }
            else if ((Hero->EtcPart == PARTS_DEFENSE_KING_TEAM_MARK ||
                      Hero->EtcPart == PARTS_DEFENSE_TEAM_MARK) &&
                     (c->EtcPart == PARTS_ATTACK_KING_TEAM_MARK ||
                      c->EtcPart == PARTS_ATTACK_TEAM_MARK ||
                      c->EtcPart == PARTS_ATTACK_KING_TEAM_MARK2 ||
                      c->EtcPart == PARTS_ATTACK_KING_TEAM_MARK3 ||
                      c->EtcPart == PARTS_ATTACK_TEAM_MARK2 ||
                      c->EtcPart == PARTS_ATTACK_TEAM_MARK3))
            {
                if (!g_isCharacterBuff((&c->Object), eBuff_Cloaking))
                {
                    return true;
                }
                else
                {
                    return false;
                }
            }
            else if (g_isCharacterBuff((&Hero->Object), eBuff_CastleRegimentAttack1) ||
                     g_isCharacterBuff((&Hero->Object), eBuff_CastleRegimentAttack2) ||
                     g_isCharacterBuff((&Hero->Object), eBuff_CastleRegimentAttack3))
            {
                OBJECT *o = &c->Object;

                if (!g_isCharacterBuff(o, eBuff_CastleRegimentAttack1) &&
                    !g_isCharacterBuff(o, eBuff_CastleRegimentAttack2) &&
                    !g_isCharacterBuff(o, eBuff_CastleRegimentAttack3))
                {
                    if (!g_isCharacterBuff((&c->Object), eBuff_Cloaking))
                    {
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                }
            }
            else if (g_isCharacterBuff((&Hero->Object), eBuff_CastleRegimentDefense))
            {
                OBJECT *o = &c->Object;

                if (!g_isCharacterBuff(o, eBuff_CastleRegimentDefense))
                {
                    if (!g_isCharacterBuff((&c->Object), eBuff_Cloaking))
                    {
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                }
            }
        }

        if (c->GuildRelationShip == GR_RIVAL || c->GuildRelationShip == GR_RIVALUNION)
        {
            return true;
        }

        if (EnableGuildWar && c->PK >= PVP_MURDERER2 && c->GuildMarkIndex != -1 &&
            wcscmp(GuildMark[Hero->GuildMarkIndex].GuildName,
                   GuildMark[c->GuildMarkIndex].GuildName) == 0)
        {
            return false;
        }
        else if (g_DuelMgr.IsDuelEnabled())
        {
            if (g_DuelMgr.IsDuelPlayer(c, DUEL_ENEMY))
            {
                return true;
            }
            else
            {
                return false;
            }
        }
        else if (g_isCharacterBuff((&Hero->Object), eBuff_DuelWatch))
        {
            return false;
        }
        else if (EnableGuildWar)
        {
            if (c->GuildTeam == 2 && c != Hero)
            {
                return true;
            }
            else
            {
                return false;
            }
        }
        else if (c->PK >= PVP_MURDERER2 || (IsKeyDown(VK_CONTROL) && c != Hero))
        {
            return true;
        }
        else if (gMapManager.IsCursedTemple() && !cursedTemple_.IsPartyMember(SelectedCharacter))
        {
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }

    return false;
}

void SessionGameplayUnit::SendRequestAction(OBJECT &obj, BYTE action)
{
    BYTE rotation = (BYTE)((obj.Angle[2] + 22.5f) / 360.f * 8.f + 1.f) % 8;
    SocketClient->ToGameServer()->SendAnimationRequest(rotation, action);
}

void SessionGameplayUnit::Action(CHARACTER *c, OBJECT *o, bool Now)
{
    float Range = 1.8f;
    switch (c->MovementType)
    {
    case MOVEMENT_ATTACK: {
        int Right = CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT].Type;
        int Left = CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT].Type;

        if (Right >= ITEM_SPEAR && Right < ITEM_SPEAR + MAX_ITEM_INDEX)
        {
            Range = 2.2f;
        }

        if (GetEquipedBowType() != BOWTYPE_NONE)
        {
            Range = 6.f;
        }

        if (ActionTarget == -1)
            return;

        // To debounce repeat left-clicks while a swing animation is still
        // playing, gate on a small *include* list of the actual swing
        // animations -- never a `>= . <=` range, which would also sweep in
        // idle/locomotion frames and lock out attacks while mounted. e.g.:
        //   if (o->CurrentAction == PLAYER_ATTACK_FIST
        //    || o->CurrentAction == PLAYER_ATTACK_SWORD_RIGHT1
        //    || ... )
        //       break;

        if (ActionTarget <= -1)
            break;

        TargetX = (int)(CharactersClient[ActionTarget].Object.Position[0] / TERRAIN_SCALE);
        TargetY = (int)(CharactersClient[ActionTarget].Object.Position[1] / TERRAIN_SCALE);

        if (c->SafeZone)
        {
            break;
        }
        else if (!CheckTile(c, o, Range))
        {
            if (gCharacterManager.GetBaseClass(c->Class) == CLASS_ELF)
            {
                if (PathFinding2(c->PositionX, c->PositionY, TargetX, TargetY, &c->Path, Range))
                {
                    c->Movement = true;
                }
            }
            break;
        }

        MouseUpdateTime = MouseUpdateTimeMax;
        SetPlayerAttack(c);
        c->AttackTime = 1;
        VectorCopy(CharactersClient[ActionTarget].Object.Position, c->TargetPosition);
        o->SetAngleZ(CreateAngle2D(o->Position, c->TargetPosition));
        LetHeroStop();
        c->Movement = false;
        BYTE PathX[1];
        BYTE PathY[1];
        PathX[0] = (c->PositionX);
        PathY[0] = (c->PositionY);
        SetCharacterTarget(*c, ActionTarget);
        int Dir = ((BYTE)((Hero->Object.Angle[2] + 22.5f) / 360.f * 8.f + 1.f) % 8);
        c->Skill = 0;
        SocketClient->ToGameServer()->SendHitRequest(CharactersClient[ActionTarget].Key, AT_ATTACK1,
                                                     Dir);
    }
    break;
    case MOVEMENT_SKILL: {
        auto iSkill = static_cast<ActionSkillType>(
            (g_MovementSkill.m_bMagic) ? (CharacterAttribute->Skill[g_MovementSkill.m_iSkill])
                                       : g_MovementSkill.m_iSkill);

        float Distance = gSkillManager.GetSkillDistance(iSkill, c);
        switch (iSkill)
        {
        case AT_SKILL_IMPALE:
            if (Hero->Helper.Type != MODEL_HORN_OF_UNIRIA)
            {
                break;
            }
        case AT_SKILL_DEATHSTAB:
        case AT_SKILL_DEATHSTAB_STR:
        case AT_SKILL_RIDER:
        case AT_SKILL_FALLING_SLASH:
        case AT_SKILL_FALLING_SLASH_STR:
        case AT_SKILL_LUNGE:
        case AT_SKILL_LUNGE_STR:
        case AT_SKILL_UPPERCUT:
        case AT_SKILL_CYCLONE:
        case AT_SKILL_CYCLONE_STR:
        case AT_SKILL_CYCLONE_STR_MG:
        case AT_SKILL_SLASH:
        case AT_SKILL_SLASH_STR:
        case AT_SKILL_FORCE:
        case AT_SKILL_FORCE_WAVE:
        case AT_SKILL_FORCE_WAVE_STR:
        case AT_SKILL_FIREBURST:
        case AT_SKILL_FIREBURST_STR:
        case AT_SKILL_FIREBURST_MASTERY:
        case AT_SKILL_RUSH:
        case AT_SKILL_SPACE_SPLIT:
        case AT_SKILL_SPIRAL_SLASH:
            if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget) &&
                CharactersClient[g_MovementSkill.m_iTarget].Dead == 0)
            {
                TargetX = (int)(CharactersClient[g_MovementSkill.m_iTarget].Object.Position[0] /
                                TERRAIN_SCALE);
                TargetY = (int)(CharactersClient[g_MovementSkill.m_iTarget].Object.Position[1] /
                                TERRAIN_SCALE);
                if (CheckTile(c, o, Distance * 1.2f) && !c->SafeZone)
                {
                    UseSkillWarrior(c, o);
                }
                else
                {
                    if (PathFinding2(c->PositionX, c->PositionY, TargetX, TargetY, &c->Path,
                                     Distance * 1.2f))
                    {
                        c->Movement = true;
                    }
                }
            }
            break;
        case AT_SKILL_POISON:
        case AT_SKILL_POISON_STR:
        case AT_SKILL_METEO:
        case AT_SKILL_LIGHTNING:
        case AT_SKILL_LIGHTNING_STR:
        case AT_SKILL_LIGHTNING_STR_MG:
        case AT_SKILL_ENERGYBALL:
        case AT_SKILL_POWERWAVE:
        case AT_SKILL_ICE:
        case AT_SKILL_ICE_STR:
        case AT_SKILL_ICE_STR_MG:
        case AT_SKILL_FIREBALL:
        case AT_SKILL_JAVELIN:
        case AT_SKILL_DEATH_CANNON:
            if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget) &&
                CharactersClient[g_MovementSkill.m_iTarget].Dead == 0)
            {
                TargetX = (int)(CharactersClient[g_MovementSkill.m_iTarget].Object.Position[0] /
                                TERRAIN_SCALE);
                TargetY = (int)(CharactersClient[g_MovementSkill.m_iTarget].Object.Position[1] /
                                TERRAIN_SCALE);
                if (CheckTile(c, o, Distance) && !c->SafeZone)
                {
                    bool Wall = CheckWall((c->PositionX), (c->PositionY), TargetX, TargetY);
                    if (Wall)
                    {
                        UseSkillWizard(c, o);
                    }
                }
                else
                {
                    if (PathFinding2(c->PositionX, c->PositionY, TargetX, TargetY, &c->Path,
                                     Distance))
                    {
                        c->Movement = true;
                    }
                }
            }
            break;
        case AT_SKILL_DEEPIMPACT:
        case AT_SKILL_ICE_ARROW:
        case AT_SKILL_ICE_ARROW_STR:
            if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget) &&
                CharactersClient[g_MovementSkill.m_iTarget].Dead == 0)
            {
                TargetX = (int)(CharactersClient[g_MovementSkill.m_iTarget].Object.Position[0] /
                                TERRAIN_SCALE);
                TargetY = (int)(CharactersClient[g_MovementSkill.m_iTarget].Object.Position[1] /
                                TERRAIN_SCALE);
                if (CheckTile(c, o, Distance) && !c->SafeZone)
                {
                    bool Wall = CheckWall((c->PositionX), (c->PositionY), TargetX, TargetY);
                    if (Wall)
                    {
                        UseSkillElf(c, o);
                    }
                }
                else
                {
                    if (PathFinding2(c->PositionX, c->PositionY, TargetX, TargetY, &c->Path,
                                     Distance))
                    {
                        c->Movement = true;
                    }
                }
            }
            break;
        case AT_SKILL_HEALING:
        case AT_SKILL_HEALING_STR:
        case AT_SKILL_ATTACK:
        case AT_SKILL_ATTACK_STR:
        case AT_SKILL_ATTACK_MASTERY:
        case AT_SKILL_DEFENSE:
        case AT_SKILL_DEFENSE_STR:
        case AT_SKILL_DEFENSE_MASTERY:
            if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget) &&
                CharactersClient[g_MovementSkill.m_iTarget].Dead == 0)
            {
                TargetX = (int)(CharactersClient[g_MovementSkill.m_iTarget].Object.Position[0] /
                                TERRAIN_SCALE);
                TargetY = (int)(CharactersClient[g_MovementSkill.m_iTarget].Object.Position[1] /
                                TERRAIN_SCALE);
                if (CheckTile(c, o, Distance) && !c->SafeZone)
                {
                    bool Wall = CheckWall((c->PositionX), (c->PositionY), TargetX, TargetY);
                    if (Wall)
                    {
                        UseSkillElf(c, o);
                    }
                }
                else
                {
                    if (PathFinding2(c->PositionX, c->PositionY, TargetX, TargetY, &c->Path,
                                     Distance))
                    {
                        c->Movement = true;
                    }
                }
            }
            break;
        case AT_SKILL_TWISTING_SLASH:
        case AT_SKILL_TWISTING_SLASH_STR:
        case AT_SKILL_TWISTING_SLASH_MASTERY:
        case AT_SKILL_FIRE_SLASH:
        case AT_SKILL_FIRE_SLASH_STR: {
            AttackKnight(c, iSkill, Distance);
        }
        break;
        case AT_SKILL_KILLING_BLOW:
        case AT_SKILL_KILLING_BLOW_STR:
        case AT_SKILL_KILLING_BLOW_MASTERY:
        case AT_SKILL_BEAST_UPPERCUT:
        case AT_SKILL_BEAST_UPPERCUT_STR:
        case AT_SKILL_BEAST_UPPERCUT_MASTERY:
        case AT_SKILL_CHAIN_DRIVE:
        case AT_SKILL_CHAIN_DRIVE_STR:
        case AT_SKILL_DRAGON_KICK:
        case AT_SKILL_DRAGON_ROAR:
        case AT_SKILL_DRAGON_ROAR_STR:
        case AT_SKILL_OCCUPY:
        case AT_SKILL_PHOENIX_SHOT: {
            g_ConsoleDebug.Write(MCD_RECEIVE, L"Action ID : %d, %d | %d %d | %d %d", iSkill,
                                 Distance, CharactersClient[g_MovementSkill.m_iTarget].Dead,
                                 g_MovementSkill.m_iTarget, CheckTile(c, o, Distance * 1.2f),
                                 !c->SafeZone);
            if (CharactersClient.IsValidIndex(g_MovementSkill.m_iTarget) &&
                CharactersClient[g_MovementSkill.m_iTarget].Dead == 0)
            {
                TargetX = (int)(CharactersClient[g_MovementSkill.m_iTarget].Object.Position[0] /
                                TERRAIN_SCALE);
                TargetY = (int)(CharactersClient[g_MovementSkill.m_iTarget].Object.Position[1] /
                                TERRAIN_SCALE);
                if (CheckTile(c, o, Distance * 1.2f) && !c->SafeZone)
                {
                    UseSkillRagefighter(c, o);
                    g_ConsoleDebug.Write(MCD_RECEIVE, L"Success attack ID : %d, %d", iSkill,
                                         Distance);
                }
                else
                {
                    if (PathFinding2(c->PositionX, c->PositionY, TargetX, TargetY, &c->Path,
                                     Distance * 1.2f))
                        c->Movement = true;
                }
            }
        }
        break;
        }
    }
    break;
    case MOVEMENT_GET:
        if (!g_bAutoGetItem)
        {
            if (CheckTile(c, o, 1.5f) == false)
            {
                break;
            }
            MouseUpdateTimeMax = 6;
        }

        if (Items[ItemKey].Item.Type == ITEM_ZEN && SendGetItem == -1)
        {
            SendGetItem = ItemKey;
            SocketClient->ToGameServer()->SendPickupItemRequest(ItemKey);
        }
        else if (g_pMyInventory->FindEmptySlotIncludingExtensions(&Items[ItemKey].Item) == -1)
        {
            wchar_t Text[256];
            mu_swprintf(Text, I18N::Game::InventoryIsFull);

            g_pSystemLogBox->AddText(Text, SEASON3B::TYPE_SYSTEM_MESSAGE);

            OBJECT *pItem = &(Items[ItemKey].Object);
            pItem->Position[2] = RequestTerrainHeight(pItem->Position[0], pItem->Position[1]) + 3.f;
            pItem->Gravity = 50.f;
        }
        else if (SendGetItem == -1)
        {
            SendGetItem = ItemKey;
            SocketClient->ToGameServer()->SendPickupItemRequest(ItemKey);
        }
        break;
    case MOVEMENT_TALK: {
        MouseUpdateTimeMax = 12;

        const bool isCryWolfElf = TheMapProcess().Crywolf1st().Get_State_Only_Elf() &&
                                  TheMapProcess().Crywolf1st().IsCyrWolf1st();
        if (!isCryWolfElf)
        {
            SetPlayerStop(c);
            c->Movement = false;
        }

        if (TargetNpc == -1)
            break;

        // === Rozpoznanie napotkanego NPC ===
        const int npcIndex = 205;
        const int monsterIndex = CharactersClient[TargetNpc].MonsterIndex;

        if (monsterIndex >= npcIndex)
        {
            const int level = CharacterAttribute->Level;

            if (monsterIndex == MONSTER_CHAOS_GOBLIN && level < 10)
            {
                wchar_t text[100];
                mu_swprintf(text, I18N::Game::OnlyLevelAboveDCanDoTheChaosCombination,
                            CHAOS_MIX_LEVEL);
                g_pSystemLogBox->AddText(text, SEASON3B::TYPE_SYSTEM_MESSAGE);
                break;
            }

            // Sklepy
            bool isRepairNpc = monsterIndex == MONSTER_EO_THE_CRAFTSMAN ||
                               monsterIndex == MONSTER_ZIENNA_THE_WEAPONS_MERCHANT ||
                               monsterIndex == MONSTER_HANZO_THE_BLACKSMITH ||
                               monsterIndex == MONSTER_RHEA ||
                               monsterIndex == MONSTER_WEAPONS_MERCHANT_BOLO;

            g_pNPCShop->SetRepairShop(isRepairNpc);

            if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_MYQUEST))
                g_pNewUISystem->Hide(SEASON3B::INTERFACE_MYQUEST);

            if (g_csQuest.IsInit())
                SocketClient->ToGameServer()->SendLegacyQuestStateRequest();

            // === Specjalne rozmowy ===
            const int objectType = CharactersClient[TargetNpc].Object.Type;
            if (isCryWolfElf)
            {
                if (objectType >= MODEL_CRYWOLF_ALTAR1 && objectType <= MODEL_CRYWOLF_ALTAR5)
                {
                    const int altarNum = objectType - MODEL_CRYWOLF_ALTAR1;
                    const bool isElf = gCharacterManager.GetBaseClass(Hero->Class) == CLASS_ELF;
                    const bool altarActive =
                        TheMapProcess().Crywolf1st().Get_AltarState_State(altarNum);
                    const BYTE state = (TheMapProcess().Crywolf1st().m_AltarState[altarNum] & 0x0f);

                    if (isElf && !altarActive)
                    {
                        if (state > 0)
                            SEASON3B::CreateMessageBox(MSGBOX_LAYOUT_CLASS(
                                SEASON3B::CCry_Wolf_Get_Temple, sessionKeeper_));
                        else
                            SEASON3B::CreateMessageBox(MSGBOX_LAYOUT_CLASS(
                                SEASON3B::CCry_Wolf_Destroy_Set_Temple, sessionKeeper_));
                    }
                    else if (isElf && altarActive)
                    {
                        SEASON3B::CreateMessageBox(MSGBOX_LAYOUT_CLASS(
                            SEASON3B::CCry_Wolf_Ing_Set_Temple, sessionKeeper_));
                    }
                    else
                    {
                        SocketClient->ToGameServer()->SendTalkToNpcRequest(
                            CharactersClient[TargetNpc].Key);
                    }
                }
                else
                {
                    SocketClient->ToGameServer()->SendTalkToNpcRequest(
                        CharactersClient[TargetNpc].Key);
                }
            }
            else if (TheMapProcess().Crywolf1st().IsCyrWolf1st())
            {
                if (!(objectType >= MODEL_CRYWOLF_ALTAR1 && objectType <= MODEL_CRYWOLF_ALTAR5))
                {
                    if (objectType == MODEL_NPC_QUARREL)
                        SEASON3B::CreateMessageBox(MSGBOX_LAYOUT_CLASS(
                            SEASON3B::CMapEnterWerwolfMsgBoxLayout, sessionKeeper_));

                    SocketClient->ToGameServer()->SendTalkToNpcRequest(
                        CharactersClient[TargetNpc].Key);
                }
            }
            else if (thirdChange_.IsBalgasBarrackMap())
            {
                SocketClient->ToGameServer()->SendTalkToNpcRequest(CharactersClient[TargetNpc].Key);
                SEASON3B::CreateMessageBox(
                    MSGBOX_LAYOUT_CLASS(SEASON3B::CMapEnterGateKeeperMsgBoxLayout, sessionKeeper_));
            }
            else if (monsterIndex >= MONSTER_LITTLE_SANTA_YELLOW &&
                     monsterIndex <= MONSTER_LITTLE_SANTA_PINK)
            {
                SocketClient->ToGameServer()->SendTalkToNpcRequest(CharactersClient[TargetNpc].Key);

                wchar_t temp[32] = {0};
                if (monsterIndex == MONSTER_LITTLE_SANTA_RED)
                    mu_swprintf(temp, I18N::Game::HealthHasBeenRecoveredOf100, 100);
                else if (monsterIndex == MONSTER_LITTLE_SANTA_BLUE)
                    mu_swprintf(temp, I18N::Game::ManaHasBeenRecoveredOf100, 100);

                g_pSystemLogBox->AddText(temp, SEASON3B::TYPE_SYSTEM_MESSAGE);
            }
            else if (monsterIndex == MONSTER_DELGADO || monsterIndex == MONSTER_LUGARD ||
                     monsterIndex == MONSTER_MARKET_UNION_MEMBER_JULIA ||
                     monsterIndex == MONSTER_DAVID)
            {
                SocketClient->ToGameServer()->SendTalkToNpcRequest(CharactersClient[TargetNpc].Key);
            }
            else
            {
                if (Is_Kanturu2nd())
                {
                    if (!g_pKanturu2ndEnterNpc->IsNpcAnimation())
                        SocketClient->ToGameServer()->SendTalkToNpcRequest(
                            CharactersClient[TargetNpc].Key);
                }
                else if (gMapManager.IsCursedTemple())
                {
                    if (!cursedTemple_.IsGaugebarEnabled())
                    {
                        if (CharactersClient[TargetNpc].MonsterIndex == MONSTER_STONE_STATUE ||
                            (sessionKeeper_.CursedTempleObject().CheckInventoryHolyItem(Hero) &&
                             ((g_pCursedTempleWindow->GetMyTeam() == SEASON3A::eTeam_Allied &&
                               monsterIndex == MONSTER_ALLIANCE_ITEM_STORAGE) ||
                              (g_pCursedTempleWindow->GetMyTeam() == SEASON3A::eTeam_Illusion &&
                               monsterIndex == MONSTER_ILLUSION_ITEM_STORAGE))))
                        {
                            cursedTemple_.SetGaugebarEnabled(true);
                        }
                        g_pCursedTempleWindow->CheckTalkProgressNpc(
                            monsterIndex, CharactersClient[TargetNpc].Key);
                    }
                }
                else
                {
                    SocketClient->ToGameServer()->SendTalkToNpcRequest(
                        CharactersClient[TargetNpc].Key);
                }
            }

            bCheckNPC = (monsterIndex == MONSTER_MARLON);

            if (monsterIndex == MONSTER_PET_TRAINER)
            {
                ITEM *pItem = &CharacterMachine->Equipment[EQUIPMENT_HELPER];
                if (pItem->Type == ITEM_DARK_HORSE_ITEM)
                    SocketClient->ToGameServer()->SendPetInfoRequest(
                        PetType::DarkHorse, StorageType::Inventory, EQUIPMENT_HELPER);

                pItem = &CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT];
                if (pItem->Type == ITEM_DARK_RAVEN_ITEM)
                    SocketClient->ToGameServer()->SendPetInfoRequest(
                        PetType::DarkRaven, StorageType::Inventory, EQUIPMENT_WEAPON_LEFT);
            }
        }

        TargetNpc = -1;
        break;
    }
    case MOVEMENT_OPERATE:
        if (std::max<int>(abs((Hero->PositionX) - TargetX), abs((Hero->PositionY) - TargetY)) <= 1)
        {
            const auto interaction = TheMapProcess().ObjectInteraction(TargetType, *Hero);
            using Action = MapObjectInteraction::Action;
            if (interaction.faceObject)
                Hero->Object.SetAngleZ(TargetAngle);
            const bool Healing = interaction.action == Action::Heal;
            const bool Pose = interaction.action == Action::Pose;
            const bool Sit = interaction.action == Action::Sit;
            if (Healing)
            {
                if (!gCharacterManager.IsFemale(c->Class))
                    SetAction(o, PLAYER_HEALING1);
                else
                    SetAction(o, PLAYER_HEALING_FEMALE1);
                SendRequestAction(Hero->Object, AT_HEALING1);
            }
            else
            {
                BYTE PathX[1];
                BYTE PathY[1];
                PathX[0] = TargetX;
                PathY[0] = TargetY;

                SendCharacterMove(Hero->Key, Hero->Object.Angle[2], 1, PathX, PathY, TargetX,
                                  TargetY);

                c->Path.PathNum = 0;
                if (Pose)
                {
                    if (!gCharacterManager.IsFemale(c->Class))
                        SetAction(o, PLAYER_POSE1);
                    else
                        SetAction(o, PLAYER_POSE_FEMALE1);
                    SendRequestAction(Hero->Object, AT_POSE1);
                }
                if (Sit)
                {
                    if ((!c->SafeZone) && (c->Helper.Type == MODEL_HORN_OF_FENRIR ||
                                           c->Helper.Type == MODEL_HORN_OF_UNIRIA ||
                                           c->Helper.Type == MODEL_HORN_OF_DINORANT ||
                                           c->Helper.Type == MODEL_DARK_HORSE_ITEM))
                        return;

                    if (!gCharacterManager.IsFemale(c->Class))
                        SetAction(o, PLAYER_SIT1);
                    else
                        SetAction(o, PLAYER_SIT_FEMALE1);
                    SendRequestAction(Hero->Object, AT_SIT1);
                }
                PlayBuffer(SOUND_DROP_ITEM01, &Hero->Object);
            }
        }
        break;
    }
}

void SessionGameplayUnit::SendMove(CHARACTER *c, OBJECT *o)
{
    if (g_pNewUISystem->IsImpossibleSendMoveInterface() == true)
    {
        return;
    }

    if (g_isCharacterBuff(o, eDeBuff_Harden))
    {
        return;
    }
    if (g_isCharacterBuff(o, eDeBuff_CursedTempleRestraint))
    {
        return;
    }

    if (c->Path.PathNum <= 2)
    {
        MouseUpdateTimeMax = 0;
    }
    else if (c->Path.PathNum == 3)
    {
        MouseUpdateTimeMax = 5;
    }
    else
    {
        MouseUpdateTimeMax = 10 + (c->Path.PathNum - 2) * 3;
    }

    SendCharacterMove(Hero->Key, o->Angle[2], c->Path.PathNum, &c->Path.PathX[0], &c->Path.PathY[0],
                      TargetX, TargetY);

    c->Movement = true;

    g_pNewUISystem->UpdateSendMoveInterface();

    if (g_bEventChipDialogEnable)
    {
        SocketClient->ToGameServer()->SendEventChipExitDialog();

        if (g_bEventChipDialogEnable == EVENT_SCRATCH_TICKET)
        {
            ClearInput(FALSE);
            InputEnable = false;
            GoldInputEnable = false;
            InputGold = 0;
            StorageGoldFlag = 0;
            g_bScratchTicket = false;
        }
        g_bEventChipDialogEnable = EVENT_NONE;

#ifndef FOR_WORK
#ifdef WINDOWMODE
        if (g_bUseWindowMode == FALSE)
        {
#endif // WINDOWMODE
            int x = REFERENCE_WIDTH * MouseX / 260;
            SetCursorPos((x)*WindowWidth / REFERENCE_WIDTH,
                         (MouseY)*WindowHeight / REFERENCE_HEIGHT);
#ifdef WINDOWMODE
        }
#endif // WINDOWMODE
#endif // FOR_WORK
        MouseUpdateTimeMax = 6;
        MouseLButton = false;
    }
}

void SessionGameplayUnit::Attack(CHARACTER *c)
{
    if ((MouseOnWindow || !CheckMouseIn(0, 0, GetScreenWidth(), 429)) && MouseLButtonPush)
    {
        MouseRButtonPop = false;
        MouseRButtonPush = false;
        MouseRButton = false;
        MouseRButtonPress = 0;

        return;
    }

    if (g_isCharacterBuff((&c->Object), eDeBuff_Stun) ||
        g_isCharacterBuff((&c->Object), eDeBuff_Sleep) ||
        g_isCharacterBuff((&Hero->Object), eBuff_DuelWatch))
    {
        return;
    }

    OBJECT *o = &c->Object;

    GameplayInteractionDetail::AdvanceTeleportFade(o, FPS_ANIMATION_FACTOR);

    bool Success = false;
    bool manualSkillInput = false;

    auto Skill = CharacterAttribute->Skill[Hero->CurrentSkill];
    float Distance = gSkillManager.GetSkillDistance(Skill, c);

    if (!EnableFastInput)
    {
        if (MouseRButtonPress != 0)
        {
            if (MouseRButtonPop || SkillKeyPush(Skill))
            {
                MouseRButtonPop = false;
                MouseRButtonPush = false;
                MouseRButton = false;

                MouseRButtonPress = 0;
                Success = true;
                manualSkillInput = true;
            }
            else
            {
                MouseRButtonPress++;
            }
        }
        else if (GameLogic::Combat::ShouldStartRightClickSkillCast(MouseRButtonPush || MouseRButton,
                                                                   MouseLButtonPush || MouseLButton,
                                                                   c->Movement))
        {
            if (Skill == AT_SKILL_NOVA)
            {
                if (o->Teleport != TELEPORT_END && o->Teleport != TELEPORT_NONE)
                    return;
                int iReqEng = 0;
                gSkillManager.GetSkillInformation_Energy(Skill, &iReqEng);
                if (CharacterAttribute->Energy + CharacterAttribute->AddEnergy < iReqEng)
                    return;

                MouseRButtonPress = 1;
                Hero->Object.m_bySkillCount = 0;
                Skill = AT_SKILL_NOVA_BEGIN;
            }
            sessionKeeper_.Gameplay()->RestorePickedItem();
            MouseRButtonPush = false;
            Success = true;
            manualSkillInput = true;
        }
        if (g_pOption->IsAutoAttack() && gMapManager.ContextMap() != WD_6STADIUM &&
            gMapManager.InChaosCastle() == false && Attacking == 2 && SelectedCharacter != -1)
        {
            Success = true;
        }

        if (Success)
        {
            RButtonPressTime = ((WorldTime - RButtonPopTime) / CLOCKS_PER_SEC);

            if (RButtonPressTime >= GameplayInteractionDetail::AutoMouseLimitTime)
            {
                MouseRButtonPush = false;
                MouseRButton = false;
                Success = FALSE;
            }
        }
        else
        {
            RButtonPopTime = WorldTime;
            RButtonPressTime = 0.f;
        }
    }
    else
    {
        if (MouseLButtonPush || MouseLButton)
        {
            MouseLButtonPush = false;
            Success = true;
            manualSkillInput = true;
        }
    }

    if (!Success)
    {
        return;
    }

    if (manualSkillInput)
    {
        sessionKeeper_.MuHelper()->YieldToManualControl();
    }

    g_iFollowCharacter = -1;

    if (!g_isCharacterBuff((&Hero->Object), eBuff_Cloaking))
    {
        if (SendPetCommand(c, Hero->CurrentSkill) == true)
        {
            return;
        }
    }

    ExecuteSkill(c, Skill, Distance);
}
