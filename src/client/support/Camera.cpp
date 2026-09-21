// Camera mode management and switching.

#include "support/Camera.h"
#include "support/CoreMath.h"
#include "session/SessionKeeper.h"
#include "app/ApplicationLoopFrame.h"
#include "data/WorldData.h"
#include "render/Terrain.h"
#include "domain/MovementAI.h"
#include "domain/MapSimulation.h"
#include "session/SessionPresentation.h"
#include "render/Text.h"
#include "session/SessionGameplay.h"
#include "session/SessionRender.h"
#include "support/Scenes.h"
#include "ui/runtime/UiControls.h"
#include "domain/ItemsSkills.h"
#include "render/Textures.h"
#include "domain/CharacterPresentation.h"
#include "domain/WorldSimulation.h"
#include "render/World.h"
#include "domain/CharacterSystem.h"
#include "ui/session/UiSessionLogic.h"
#include "domain/Events.h"
#include "app/AppWindow.h"
#include "app/ApplicationDiagnostics.h"
#include "data/CharacterData.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "domain/Automation.h"
#include "domain/ChatSocial.h"
#include "domain/EffectsUpdate.h"
#include "domain/Guild.h"
#include "domain/Quests.h"
#include "domain/WorldPresentation.h"
#include "render/ModelGeometry.h"
#include "render/ModelResources.h"
#include "render/Sprites.h"
#include "session/SessionAudio.h"
#include "ui/features/Activities/ActivitiesLogic.h"
#include "ui/features/Hud/HudLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/QuestNpc/QuestNpcLogic.h"
#include "ui/features/Social/SocialLogic.h"
#include "render/FrameTape.h"
#include "session/SessionNetwork.h"
#include "session/SessionWorkspace.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationNetwork.h"
#include "I18N/All.h"
#include "data/GameData.h"
#include "ui/features/Dialogs/DialogsLogic.h"

CameraManager::CameraManager(SessionKeeper &keeper) noexcept
    : SessionLegacyCalls(keeper), sessionKeeper_(keeper), g_Camera(keeper.CameraStateObject()),
      m_CurrentMode(CameraMode::Default), m_pActiveCamera(nullptr)
{
}

CameraManager::~CameraManager()
{
    Shutdown();
}

void CameraManager::Initialize(float animationFactor)
{
    // Create camera instances
    m_pDefaultCamera = std::make_unique<DefaultCamera>(sessionKeeper_);
    m_pOrbitalCamera = std::make_unique<OrbitalCamera>(sessionKeeper_);

    // Start with default camera
    m_pActiveCamera = m_pDefaultCamera.get();
    m_CurrentMode = CameraMode::Default;
    m_pActiveCamera->OnActivate(g_Camera, animationFactor);
}

void CameraManager::Shutdown()
{
    if (m_pActiveCamera)
    {
        m_pActiveCamera->OnDeactivate();
        m_pActiveCamera = nullptr;
    }

    m_pDefaultCamera.reset();
    m_pOrbitalCamera.reset();
}

bool CameraManager::Update(float animationFactor)
{
    // Lazy initialization on first use
    if (!m_pActiveCamera && !m_pDefaultCamera)
        Initialize(animationFactor);

    if (!m_pActiveCamera)
        return false;

    // Auto-reset to DefaultCamera when leaving MainScene
    // Orbital camera is MainScene-only; if the scene changed, switch back
    if (SceneFlag != MAIN_SCENE && m_CurrentMode != CameraMode::Default)
        SetCameraMode(CameraMode::Default, animationFactor);

    return m_pActiveCamera->Update(animationFactor);
}

bool CameraManager::SetCameraMode(CameraMode mode, float animationFactor)
{
    if (mode == m_CurrentMode)
        return false;

    // FIX: Only allow OrbitalCamera in MainScene
    if (mode == CameraMode::Orbital && SceneFlag != MAIN_SCENE)
    {
        // Silently ignore orbital camera request in non-MainScene
        return false;
    }

    ICamera *pNewCamera = nullptr;

    switch (mode)
    {
    case CameraMode::Default:
        pNewCamera = m_pDefaultCamera.get();
        break;
    case CameraMode::Orbital:
        pNewCamera = m_pOrbitalCamera.get();
        break;
    default:
        return false;
    }

    if (!pNewCamera)
        return false;

    TransitionToCamera(pNewCamera, animationFactor);
    m_CurrentMode = mode;
    return true;
}

void CameraManager::CycleToNextMode(float animationFactor)
{
    CameraMode nextMode = GetNextCameraMode(m_CurrentMode);
    SetCameraMode(nextMode, animationFactor);
}

void CameraManager::TransitionToCamera(ICamera *pNewCamera, float animationFactor)
{
    bool skipActivate = false;

    // Deactivate old camera
    if (m_pActiveCamera)
        m_pActiveCamera->OnDeactivate();

    // Activate new camera with current state for smooth transition
    if (!skipActivate)
        pNewCamera->OnActivate(g_Camera, animationFactor);

    m_pActiveCamera = pNewCamera;
}

CameraManager &CameraManager::CameraManager_Instance()
{
    return *this;
}

OrbitalCamera *CameraManager::GetOrbitalCameraInstance()
{
    if (GetCurrentMode() == CameraMode::Orbital)
    {
        return static_cast<OrbitalCamera *>(GetActiveCamera());
    }
    return nullptr;
}

void CameraManager::GetOrbitalCameraAngles(float *outYaw, float *outPitch)
{
    auto *orbital = GetOrbitalCameraInstance();
    if (orbital)
    {
        if (outYaw)
            *outYaw = orbital->GetTotalYaw();
        if (outPitch)
            *outPitch = orbital->GetTotalPitch();
    }
    else
    {
        if (outYaw)
            *outYaw = 0.0f;
        if (outPitch)
            *outPitch = 0.0f;
    }
}

// Get active camera's config
void CameraManager::GetActiveCameraConfig(float *outFOV, float *outNearPlane, float *outFarPlane,
                                          float *outTerrainCullRange)
{
    ICamera *camera = GetActiveCamera();
    if (camera)
    {
        const CameraConfig &config = camera->GetConfig();
        if (outFOV)
            *outFOV = config.hFov;
        if (outNearPlane)
            *outNearPlane = config.nearPlane;
        if (outFarPlane)
            *outFarPlane = config.farPlane;
        if (outTerrainCullRange)
            *outTerrainCullRange = config.terrainCullRange;
    }
}

CameraManager &SessionLegacyCalls::CameraManager_Instance()
{
    return sessionKeeper_.CameraManagerUnit()->CameraManager_Instance();
}
OrbitalCamera *SessionLegacyCalls::GetOrbitalCameraInstance()
{
    return sessionKeeper_.CameraManagerUnit()->GetOrbitalCameraInstance();
}
void SessionLegacyCalls::GetOrbitalCameraAngles(float *outYaw, float *outPitch)
{
    sessionKeeper_.CameraManagerUnit()->GetOrbitalCameraAngles(outYaw, outPitch);
}
void SessionLegacyCalls::GetActiveCameraConfig(float *outFOV, float *outNearPlane,
                                               float *outFarPlane, float *outTerrainCullRange)
{
    sessionKeeper_.CameraManagerUnit()->GetActiveCameraConfig(outFOV, outNearPlane, outFarPlane,
                                                              outTerrainCullRange);
}

CCameraMove::CCameraMove(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), mapManager_(keeper.MapManagerObject())
{
    Init();
}
CCameraMove::~CCameraMove()
{
    UnLoadCameraWalkScript();
}

namespace
{
constexpr float kDefaultStartDistance = 10.0f;
constexpr float kMinMoveAccel = 0.1f;
constexpr float kMaxMoveAccel = 40.0f;
constexpr float kMinDistanceLevel = 5.0f;
constexpr float kMaxDistanceLevel = 20.0f;
constexpr float kReturnSpeed = 10.0f;
constexpr float kDistanceAdjustFactor = 0.005f;
constexpr float kDistanceSnapEpsilon = 0.2f;
constexpr float kTourBlendDistance = 300.0f;
constexpr float kTourMaxRotateSpeed = 1.0f;
constexpr float kMinTourAccel = 0.1f;
constexpr float kMaxTourAccel = 100.0f;
constexpr float kFullCircleDegrees = 360.0f;
constexpr float kWaypointRenderOffset = 50.0f;
constexpr float kWaypointRenderHalfSize = 10.0f;
constexpr float kTourCameraZPosition = -300.0f;

template <typename T> constexpr const T &Clamp(const T &value, const T &minValue, const T &maxValue)
{
    return std::max<T>(minValue, std::min<T>(value, maxValue));
}

template <std::size_t Size> void CopyArray(const float (&source)[Size], float (&destination)[Size])
{
    std::copy(source, source + Size, destination);
}

float NormalizeAngleDegrees(float angle)
{
    angle = std::fmod(angle, kFullCircleDegrees);
    if (angle < 0.0f)
    {
        angle += kFullCircleDegrees;
    }
    return angle;
}

float SignedAngleDelta(float fromDegrees, float toDegrees)
{
    const float delta = NormalizeAngleDegrees(toDegrees) - NormalizeAngleDegrees(fromDegrees);
    if (delta > 180.0f)
    {
        return delta - kFullCircleDegrees;
    }
    if (delta < -180.0f)
    {
        return delta + kFullCircleDegrees;
    }
    return delta;
}

struct CameraVector2
{
    float x{0.0f};
    float y{0.0f};

    float Length() const
    {
        return std::sqrt(x * x + y * y);
    }

    CameraVector2 Normalized() const
    {
        const float length = Length();
        if (length <= std::numeric_limits<float>::epsilon())
        {
            return {};
        }
        return {x / length, y / length};
    }

    CameraVector2 operator+(const CameraVector2 &other) const
    {
        return {x + other.x, y + other.y};
    }

    CameraVector2 operator-(const CameraVector2 &other) const
    {
        return {x - other.x, y - other.y};
    }

    CameraVector2 operator*(float scalar) const
    {
        return {x * scalar, y * scalar};
    }
};

CameraVector2 BlendVectors(const CameraVector2 &a, const CameraVector2 &b, float alpha)
{
    return {(a.x * (1.0f - alpha)) + (b.x * alpha), (a.y * (1.0f - alpha)) + (b.y * alpha)};
}
} // namespace

// Applies the LoginScene waypoint correction to a world-space position in-place.
// Only active on the LoginScene map; other worlds pass through unchanged.
void CCameraMove::ApplyLoginSceneOffset(float &x, float &y, float &z)
{
    if (mapManager_.ContextMap() != WD_73NEW_LOGIN_SCENE)
        return;

    x += g_LoginSceneOffsetX;
    y += g_LoginSceneOffsetY;
    z += g_LoginSceneOffsetZ;
}

void SessionLegacyCalls::ApplyLoginSceneOffset(float &x, float &y, float &z)
{
    sessionKeeper_.CameraMoveObject().ApplyLoginSceneOffset(x, y, z);
}

void CCameraMove::Init()
{
    tourMotionFrames_ = tourMotionElapsed_ = 0.0;
    m_CameraStartPos[0] = m_CameraStartPos[1] = m_CameraStartPos[2] = 0.0f;
    m_fCameraStartDistanceLevel = kDefaultStartDistance;
    m_iDelayCount = 0;
    m_dwCameraWalkState = CAMERAWALK_STATE_READY;

    m_CurrentCameraPos[0] = m_CurrentCameraPos[1] = m_CurrentCameraPos[2] = 0.0f;
    m_fCurrentDistanceLevel = 0.0f;

    m_dwCurrentIndex = 0;
    m_iSelectedTile = -1;

    m_bTourMode = FALSE;
    m_bTourPause = FALSE;
    m_fForceSpeed = 0.0f;
    m_vTourCameraPos[0] = m_vTourCameraPos[1] = m_vTourCameraPos[2] = 0.0f;
    m_fTourCameraAngle = 0.0f;
    m_fTargetTourCameraAngle = 0.0f;

    m_fCameraAngle = 0.0f;
    m_fFrustumAngle = 0.0f;
}

void CCameraMove::InstallCameraWalkScript(WorldCameraData &&data) noexcept
{
    m_listWayPoint = std::move(data).TakeWaypoints();
    Init();
}
void CCameraMove::UnLoadCameraWalkScript()
{
    m_listWayPoint.clear();

    Init();
}
bool CCameraMove::SaveCameraWalkScript(const std::wstring &filename)
{
    if (m_listWayPoint.empty())
    {
        return false;
    }

    std::unique_ptr<FILE, decltype(&fclose)> fileHandle(_wfopen(filename.c_str(), L"wb"), &fclose);
    if (!fileHandle)
    {
        return false;
    }

    const DWORD signature = WorldCameraData::Signature;
    // Keep the count a 32-bit field on disk to match the original tools (see
    // the read path); sizeof(size_t) would emit 8 bytes on LP64.
    const std::uint32_t waypointCount = static_cast<std::uint32_t>(m_listWayPoint.size());

    if (fwrite(&signature, sizeof(DWORD), 1, fileHandle.get()) != 1 ||
        fwrite(&waypointCount, sizeof(waypointCount), 1, fileHandle.get()) != 1)
    {
        return false;
    }

    for (const auto &waypoint : m_listWayPoint)
    {
        if (fwrite(&waypoint, sizeof(WAYPOINT), 1, fileHandle.get()) != 1)
        {
            return false;
        }
    }

    return true;
}

void CCameraMove::AddWayPoint(int iGridX, int iGridY, float fCameraMoveAccel,
                              float fCameraDistanceLevel, int iDelay)
{
    tourMotionFrames_ = tourMotionElapsed_ = 0.0;
    WAYPOINT waypoint{};
    waypoint.iIndex = TERRAIN_INDEX_REPEAT(iGridX, iGridY);
    waypoint.fCameraX = iGridX * TERRAIN_SCALE;
    waypoint.fCameraY = iGridY * TERRAIN_SCALE;
    waypoint.fCameraZ = RequestTerrainHeight(waypoint.fCameraX, waypoint.fCameraY);

    waypoint.fCameraMoveAccel = Clamp(fCameraMoveAccel, kMinMoveAccel, kMaxMoveAccel);
    waypoint.fCameraDistanceLevel =
        Clamp(fCameraDistanceLevel, kMinDistanceLevel, kMaxDistanceLevel);
    waypoint.iDelay = std::max<int>(iDelay, 0);

    m_listWayPoint.push_back(std::move(waypoint));
}
void CCameraMove::RemoveWayPoint(int iGridX, int iGridY)
{
    tourMotionFrames_ = tourMotionElapsed_ = 0.0;
    const int tileIndex = TERRAIN_INDEX_REPEAT(iGridX, iGridY);
    const auto originalSize = m_listWayPoint.size();
    const bool wasSelectedTile = GetSelectedTile() == static_cast<DWORD>(tileIndex);

    auto removalPredicate = [tileIndex](const WAYPOINT &waypoint) {
        return waypoint.iIndex == tileIndex;
    };

    m_listWayPoint.erase(
        std::remove_if(m_listWayPoint.begin(), m_listWayPoint.end(), removalPredicate),
        m_listWayPoint.end());

    if (m_dwCurrentIndex >= m_listWayPoint.size())
        m_dwCurrentIndex = 0;

    if (wasSelectedTile && m_listWayPoint.size() != originalSize)
    {
        SetSelectedTile(-1);
    }
}

void CCameraMove::SetCameraMoveAccel(int iTileIndex, float fCameraMoveAccel)
{
    tourMotionFrames_ = tourMotionElapsed_ = 0.0;
    const float clampedAccel = Clamp(fCameraMoveAccel, kMinMoveAccel, kMaxMoveAccel);
    for (auto &waypoint : m_listWayPoint)
    {
        if (waypoint.iIndex == iTileIndex)
        {
            waypoint.fCameraMoveAccel = clampedAccel;
        }
    }
}
void CCameraMove::SetCameraDistanceLevel(int iTileIndex, float fCameraDistanceLevel)
{
    tourMotionFrames_ = tourMotionElapsed_ = 0.0;
    const float clampedDistance = Clamp(fCameraDistanceLevel, kMinDistanceLevel, kMaxDistanceLevel);
    for (auto &waypoint : m_listWayPoint)
    {
        if (waypoint.iIndex == iTileIndex)
        {
            waypoint.fCameraDistanceLevel = clampedDistance;
        }
    }
}
void CCameraMove::SetDelay(int iTileIndex, int iDelay)
{
    tourMotionFrames_ = tourMotionElapsed_ = 0.0;
    const int clampedDelay = std::max<int>(iDelay, 0);
    for (auto &waypoint : m_listWayPoint)
    {
        if (waypoint.iIndex == iTileIndex)
        {
            waypoint.iDelay = clampedDelay;
        }
    }
}
float CCameraMove::GetCameraMoveAccel(int iTileIndex)
{
    if (const auto *waypoint = FindWayPointByTile(iTileIndex))
    {
        return waypoint->fCameraMoveAccel;
    }
    return 0.0f;
}
float CCameraMove::GetCameraDistanceLevel(int iTileIndex)
{
    if (const auto *waypoint = FindWayPointByTile(iTileIndex))
    {
        return waypoint->fCameraDistanceLevel;
    }
    return 0.0f;
}
int CCameraMove::GetDelay(int iTileIndex)
{
    if (const auto *waypoint = FindWayPointByTile(iTileIndex))
    {
        return waypoint->iDelay;
    }
    return -1;
}

bool CCameraMove::IsCameraMove() const
{
    return (m_dwCameraWalkState == CAMERAWALK_STATE_MOVE);
}

void CCameraMove::UpdateWayPoint(float animationFactor)
{
    while (true)
    {
        if (m_dwCameraWalkState != CAMERAWALK_STATE_MOVE || m_listWayPoint.empty())
        {
            m_dwCameraWalkState = CAMERAWALK_STATE_DONE;
            m_iDelayCount = 0;
            m_dwCurrentIndex = 0;
            return;
        }

        if (m_dwCurrentIndex < m_listWayPoint.size())
        {
            const WAYPOINT *targetWaypoint = GetWayPointByIndex(m_dwCurrentIndex);
            if (!targetWaypoint)
            {
                m_dwCameraWalkState = CAMERAWALK_STATE_DONE;
                return;
            }

            const float cameraMoveAccel = targetWaypoint->fCameraMoveAccel * animationFactor;
            if (m_iDelayCount >= targetWaypoint->iDelay)
            {
                CameraVector2 toTarget{targetWaypoint->fCameraX - m_CurrentCameraPos[0],
                                       targetWaypoint->fCameraY - m_CurrentCameraPos[1]};
                const float remainingDistance = toTarget.Length();

                if (remainingDistance <= cameraMoveAccel)
                {
                    ++m_dwCurrentIndex;
                    m_iDelayCount = 0;
                    continue;
                }

                if (remainingDistance > std::numeric_limits<float>::epsilon())
                {
                    const CameraVector2 direction = toTarget * (1.0f / remainingDistance);
                    m_CurrentCameraPos[0] += direction.x * cameraMoveAccel;
                    m_CurrentCameraPos[1] += direction.y * cameraMoveAccel;
                }

                m_CurrentCameraPos[2] =
                    RequestTerrainHeight(m_CurrentCameraPos[0], m_CurrentCameraPos[1]);

                const float distanceDelta = cameraMoveAccel * kDistanceAdjustFactor;
                const float targetDistance = targetWaypoint->fCameraDistanceLevel;
                if (std::abs(m_fCurrentDistanceLevel - targetDistance) <= distanceDelta)
                {
                    m_fCurrentDistanceLevel = targetDistance;
                }
                else if (m_fCurrentDistanceLevel < targetDistance)
                {
                    m_fCurrentDistanceLevel += distanceDelta;
                }
                else
                {
                    m_fCurrentDistanceLevel -= distanceDelta;
                }
            }

            m_iDelayCount += animationFactor;
            return;
        }

        if (m_dwCurrentIndex == m_listWayPoint.size())
        {
            CameraVector2 toStart{m_CameraStartPos[0] - m_CurrentCameraPos[0],
                                  m_CameraStartPos[1] - m_CurrentCameraPos[1]};
            const float remainingDistance = toStart.Length();

            const float returnStep = kReturnSpeed * animationFactor;
            if (remainingDistance <= returnStep)
            {
                ++m_dwCurrentIndex;
                continue;
            }

            if (remainingDistance > std::numeric_limits<float>::epsilon())
            {
                const CameraVector2 direction = toStart * (1.0f / remainingDistance);
                m_CurrentCameraPos[0] += direction.x * returnStep;
                m_CurrentCameraPos[1] += direction.y * returnStep;
            }

            m_CurrentCameraPos[2] =
                RequestTerrainHeight(m_CurrentCameraPos[0], m_CurrentCameraPos[1]);

            if (std::abs(m_fCameraStartDistanceLevel - m_fCurrentDistanceLevel) >
                kDistanceSnapEpsilon)
            {
                m_fCurrentDistanceLevel += (m_fCameraStartDistanceLevel - m_fCurrentDistanceLevel) *
                                           Core::Time::Blend(0.2f, animationFactor);
            }
            else
            {
                m_fCurrentDistanceLevel = m_fCameraStartDistanceLevel;
            }

            return;
        }

        m_dwCameraWalkState = CAMERAWALK_STATE_DONE;
        m_iDelayCount = 0;
        m_dwCurrentIndex = 0;
        return;
    }
}
void CCameraMove::GetCurrentCameraPos(float CameraPos[3])
{
    if (IsTourMode())
    {
        CameraPos[0] = m_vTourCameraPos[0];
        CameraPos[1] = m_vTourCameraPos[1];
        CameraPos[2] = m_vTourCameraPos[2];

        // FIX: Apply LoginScene position offset
        ApplyLoginSceneOffset(CameraPos[0], CameraPos[1], CameraPos[2]);
    }
    else
    {
        CameraPos[0] = m_CurrentCameraPos[0];
        CameraPos[1] = m_CurrentCameraPos[1];
        CameraPos[2] = m_CurrentCameraPos[2];
    }
}
float CCameraMove::GetCurrentCameraDistanceLevel() const
{
    return m_fCurrentDistanceLevel;
}

void CCameraMove::PlayCameraWalk(float StartPos[3], float fStartDistanceLevel)
{
    tourMotionFrames_ = tourMotionElapsed_ = 0.0;
    if (m_dwCameraWalkState == CAMERAWALK_STATE_READY)
    {
        m_dwCameraWalkState = CAMERAWALK_STATE_MOVE;
        m_CameraStartPos[0] = m_CurrentCameraPos[0] = StartPos[0];
        m_CameraStartPos[1] = m_CurrentCameraPos[1] = StartPos[1];
        m_CameraStartPos[2] = m_CurrentCameraPos[2] = StartPos[2];
        m_fCameraStartDistanceLevel = m_fCurrentDistanceLevel = fStartDistanceLevel;
        m_iDelayCount = 0;
        m_dwCurrentIndex = 0;
    }
}
void CCameraMove::StopCameraWalk(bool bDone)
{
    tourMotionFrames_ = tourMotionElapsed_ = 0.0;
    if (m_dwCameraWalkState == CAMERAWALK_STATE_MOVE)
    {
        if (bDone)
        {
            m_dwCameraWalkState = CAMERAWALK_STATE_DONE;
        }
        else
        {
            m_dwCameraWalkState = CAMERAWALK_STATE_READY;
        }
        m_iDelayCount = 0;
        m_dwCurrentIndex = 0;
    }
}
void CCameraMove::UpdateCameraStartPos(float StartPos[3])
{
    m_CameraStartPos[0] = StartPos[0];
    m_CameraStartPos[1] = StartPos[1];
    m_CameraStartPos[2] = StartPos[2];
}

void CCameraMove::SetCameraWalkState(DWORD dwCameraWalkState)
{
    m_dwCameraWalkState = dwCameraWalkState;
}

DWORD CCameraMove::GetCameraWalkState() const
{
    return m_dwCameraWalkState;
}

void CCameraMove::RenderWayPoint()
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_TEXTURE_2D);

    glBegin(GL_QUADS);
    glColor4f(1.0f, 0.0f, 0.0f, 0.8f);
    for (const auto &waypoint : m_listWayPoint)
    {
        glNormal3f(0.0f, 0.0f, 1.0f);
        const float minX = waypoint.fCameraX + kWaypointRenderOffset - kWaypointRenderHalfSize;
        const float maxX = waypoint.fCameraX + kWaypointRenderOffset + kWaypointRenderHalfSize;
        const float minY = waypoint.fCameraY + kWaypointRenderOffset - kWaypointRenderHalfSize;
        const float maxY = waypoint.fCameraY + kWaypointRenderOffset + kWaypointRenderHalfSize;

        glVertex3f(minX, minY, waypoint.fCameraZ);
        glVertex3f(maxX, minY, waypoint.fCameraZ);
        glVertex3f(maxX, maxY, waypoint.fCameraZ);
        glVertex3f(minX, maxY, waypoint.fCameraZ);
    }
    glEnd();

    RenderWayPointLine();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_ALPHA_TEST);
    glEnable(GL_TEXTURE_2D);
}
void CCameraMove::RenderWayPointLine()
{
    glBegin(GL_LINE_STRIP);

    glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
    for (const auto &waypoint : m_listWayPoint)
    {
        glVertex3f(waypoint.fCameraX + 50.0f, waypoint.fCameraY + 50.0f, waypoint.fCameraZ);
    }

    glEnd();
}
void CCameraMove::SetSelectedTile(int iTileIndex)
{
    m_iSelectedTile = -1;
    for (const auto &waypoint : m_listWayPoint)
    {
        if (waypoint.iIndex == iTileIndex)
        {
            m_iSelectedTile = iTileIndex;
            return;
        }
    }
}
DWORD CCameraMove::GetSelectedTile() const
{
    return m_iSelectedTile;
}

CCameraMove::WAYPOINT *CCameraMove::GetWayPointByIndex(std::size_t index)
{
    if (index >= m_listWayPoint.size())
    {
        return nullptr;
    }
    return &m_listWayPoint[index];
}

const CCameraMove::WAYPOINT *CCameraMove::GetWayPointByIndex(std::size_t index) const
{
    if (index >= m_listWayPoint.size())
    {
        return nullptr;
    }
    return &m_listWayPoint[index];
}

CCameraMove::WAYPOINT *CCameraMove::FindWayPointByTile(int tileIndex)
{
    auto iter = std::find_if(
        m_listWayPoint.begin(), m_listWayPoint.end(),
        [tileIndex](const WAYPOINT &waypoint) { return waypoint.iIndex == tileIndex; });

    return (iter != m_listWayPoint.end()) ? &*iter : nullptr;
}

const CCameraMove::WAYPOINT *CCameraMove::FindWayPointByTile(int tileIndex) const
{
    auto iter = std::find_if(
        m_listWayPoint.begin(), m_listWayPoint.end(),
        [tileIndex](const WAYPOINT &waypoint) { return waypoint.iIndex == tileIndex; });

    return (iter != m_listWayPoint.end()) ? &*iter : nullptr;
}

BOOL CCameraMove::SetTourMode(BOOL bFlag, BOOL bRandomStart, int index)
{
    const std::size_t waypointCount = m_listWayPoint.size();
    if (waypointCount <= 1)
    {
        m_bTourMode = FALSE;
        return FALSE;
    }

    m_bTourMode = bFlag;

    if (bFlag == FALSE)
    {
        return TRUE;
    }

    if (bRandomStart)
    {
        m_dwCurrentIndex = static_cast<DWORD>(WorldRandom() % waypointCount);
    }
    else
    {
        if (index < 0)
        {
            index = 0;
        }
        m_dwCurrentIndex = static_cast<DWORD>(static_cast<std::size_t>(index) % waypointCount);
    }

    const std::size_t targetIndex = (m_dwCurrentIndex < waypointCount) ? m_dwCurrentIndex : 0;
    const std::size_t startIndex = (targetIndex > 0) ? targetIndex - 1 : waypointCount - 1;

    const WAYPOINT *startWaypoint = GetWayPointByIndex(startIndex);
    const WAYPOINT *targetWaypoint = GetWayPointByIndex(targetIndex);
    if (!startWaypoint || !targetWaypoint)
    {
        return FALSE;
    }

    m_CameraStartPos[0] = m_CurrentCameraPos[0] = m_vTourCameraPos[0] = startWaypoint->fCameraX;
    m_CameraStartPos[1] = m_CurrentCameraPos[1] = m_vTourCameraPos[1] = startWaypoint->fCameraY;
    m_CameraStartPos[2] = m_CurrentCameraPos[2] = m_vTourCameraPos[2] = startWaypoint->fCameraZ;

    // Route coordinates stay authored; GetCurrentCameraPos applies the login offset once.
    tourMotionFrames_ = tourMotionElapsed_ = 0.0;
    m_iDelayCount = 0.0;
    m_bTourPause = FALSE;
    m_fForceSpeed = 0.f;
    m_fCurrentDistanceLevel = startWaypoint->fCameraDistanceLevel;

    CameraVector2 toTarget{targetWaypoint->fCameraX - startWaypoint->fCameraX,
                           targetWaypoint->fCameraY - startWaypoint->fCameraY};
    const CameraVector2 forwardDir = toTarget.Normalized();
    m_fTargetTourCameraAngle = m_fTourCameraAngle = CreateAngle(0, 0, forwardDir.x, -forwardDir.y);

    return TRUE;
}

void CCameraMove::PauseTour(BOOL bFlag)
{
    if (m_bTourPause != bFlag || m_fForceSpeed != 0.f)
        tourMotionFrames_ = tourMotionElapsed_ = 0.0;
    m_bTourPause = bFlag;
    m_fForceSpeed = 0.0f;
}

void CCameraMove::ForwardTour(float fSpeed)
{
    if (m_fForceSpeed != fSpeed)
        tourMotionFrames_ = tourMotionElapsed_ = 0.0;
    m_fForceSpeed = fSpeed;
}

void CCameraMove::BackwardTour(float fSpeed)
{
    if (m_fForceSpeed != -fSpeed)
        tourMotionFrames_ = tourMotionElapsed_ = 0.0;
    m_fForceSpeed = -fSpeed;
}

void CCameraMove::UpdateTourWayPoint(float animationFactor)
{
    const std::size_t waypointCount = m_listWayPoint.size();
    if (m_dwCameraWalkState != CAMERAWALK_STATE_MOVE || waypointCount <= 1)
    {
        m_dwCameraWalkState = CAMERAWALK_STATE_DONE;
        m_iDelayCount = 0;
        return;
    }
    if (animationFactor <= 0.f || (IsTourPaused() && m_fForceSpeed == 0.f))
        return;

    double remaining = animationFactor;
    std::size_t coincidentPoints = 0;
    while (remaining > 0.0)
    {
        const auto &target = m_listWayPoint[m_dwCurrentIndex];
        const auto &origin = m_listWayPoint[(m_dwCurrentIndex + waypointCount - 1) % waypointCount];
        const double delay = std::min(remaining, std::max(0.0, target.iDelay - m_iDelayCount));
        m_iDelayCount += delay;
        remaining -= delay;
        if (delay > 0.0)
            coincidentPoints = 0;
        if (remaining <= 0.0)
            break;

        if (tourMotionFrames_ == 0.0)
            PrepareTourMotion(origin, target);
        const double step = std::min(remaining, tourMotionFrames_ - tourMotionElapsed_);
        AdvanceTourMotion(step);
        remaining -= step;
        if (step > 0.0)
            coincidentPoints = 0;
        if (tourMotionElapsed_ >= tourMotionFrames_)
        {
            if (tourArrives_)
                FinishTourSegment();
            tourMotionFrames_ = tourMotionElapsed_ = 0.0;
            // A route made entirely of coincident points with no delays is stationary.
            if (step == 0.0 && ++coincidentPoints >= waypointCount)
                break;
        }
    }
}

void CCameraMove::PrepareTourMotion(const WAYPOINT &origin, const WAYPOINT &target)
{
    const std::size_t count = m_listWayPoint.size();
    const CameraVector2 toTarget{target.fCameraX - m_CurrentCameraPos[0],
                                 target.fCameraY - m_CurrentCameraPos[1]};
    const CameraVector2 toOrigin{origin.fCameraX - m_CurrentCameraPos[0],
                                 origin.fCameraY - m_CurrentCameraPos[1]};
    const float distanceToTarget = toTarget.Length();
    const float distanceToOrigin = toOrigin.Length();
    const CameraVector2 forward = toTarget.Normalized();
    CameraVector2 tourDirection = forward;
    if (distanceToTarget <= kTourBlendDistance)
    {
        const auto &next = m_listWayPoint[(m_dwCurrentIndex + 1) % count];
        const CameraVector2 segment{next.fCameraX - target.fCameraX,
                                    next.fCameraY - target.fCameraY};
        if (segment.Length() > std::numeric_limits<float>::epsilon())
            tourDirection = BlendVectors(segment.Normalized(), forward,
                                         distanceToTarget / kTourBlendDistance * 0.5f + 0.5f)
                                .Normalized();
    }
    else if (distanceToOrigin <= kTourBlendDistance)
    {
        const auto &previous = m_listWayPoint[(m_dwCurrentIndex + count - 2) % count];
        const CameraVector2 segment{origin.fCameraX - previous.fCameraX,
                                    origin.fCameraY - previous.fCameraY};
        if (segment.Length() > std::numeric_limits<float>::epsilon())
            tourDirection = BlendVectors(segment.Normalized(), forward,
                                         distanceToOrigin / kTourBlendDistance * 0.5f + 0.5f)
                                .Normalized();
    }

    const bool reverse = m_fForceSpeed < 0.f;
    const float multiplier = m_fForceSpeed == 0.f ? 1.f : std::abs(m_fForceSpeed);
    const float speed = Clamp(reverse ? origin.fCameraMoveAccel : target.fCameraMoveAccel,
                              kMinTourAccel, kMaxTourAccel) *
                        multiplier;
    const double arrivalFrames = (reverse ? distanceToOrigin : distanceToTarget) / speed;
    // Keep the authored direction/angle sample for a full reference frame, including
    // fractional callbacks. Arrival cuts the sample short and spends its remainder.
    tourMotionFrames_ = std::min(1.0, arrivalFrames);
    tourMotionElapsed_ = 0.0;
    tourArrives_ = arrivalFrames <= 1.0;
    const CameraVector2 routeDirection = reverse ? toOrigin.Normalized() : forward;
    for (int axis = 0; axis < 2; ++axis)
        tourRouteStart_[axis] = m_CurrentCameraPos[axis];
    tourRouteVelocity_ = {routeDirection.x * speed, routeDirection.y * speed};
    constexpr float angleHoldDistance = 5.f;
    if (distanceToTarget > angleHoldDistance && distanceToOrigin > angleHoldDistance)
        m_fTargetTourCameraAngle = CreateAngle(0, 0, tourDirection.x, -tourDirection.y);
    tourAngleStart_ = m_fTourCameraAngle;
}

void CCameraMove::AdvanceTourMotion(double frames)
{
    tourMotionElapsed_ += frames;
    const float elapsed = static_cast<float>(tourMotionElapsed_);
    for (int axis = 0; axis < 2; ++axis)
    {
        m_CurrentCameraPos[axis] = tourRouteStart_[axis] + tourRouteVelocity_[axis] * elapsed;
        m_vTourCameraPos[axis] = m_CurrentCameraPos[axis];
    }
    const float angleDelta = SignedAngleDelta(tourAngleStart_, m_fTargetTourCameraAngle);
    constexpr float angleBlend = 1.f / 30.f;
    const float rotation = std::min(std::abs(angleDelta) * Core::Time::Blend(angleBlend, elapsed),
                                    kTourMaxRotateSpeed * elapsed);
    m_fTourCameraAngle =
        NormalizeAngleDegrees(tourAngleStart_ + std::copysign(rotation, angleDelta));
    m_CurrentCameraPos[2] = RequestTerrainHeight(m_CurrentCameraPos[0], m_CurrentCameraPos[1]);
    m_vTourCameraPos[2] = kTourCameraZPosition;
    const auto &target = m_listWayPoint[m_dwCurrentIndex];
    const auto &origin =
        m_listWayPoint[(m_dwCurrentIndex + m_listWayPoint.size() - 1) % m_listWayPoint.size()];
    const CameraVector2 toTarget{target.fCameraX - m_CurrentCameraPos[0],
                                 target.fCameraY - m_CurrentCameraPos[1]};
    const CameraVector2 toOrigin{origin.fCameraX - m_CurrentCameraPos[0],
                                 origin.fCameraY - m_CurrentCameraPos[1]};
    UpdateTourDistance(origin, target, toOrigin.Length(), toTarget.Length());
}

void CCameraMove::FinishTourSegment()
{
    const auto count = m_listWayPoint.size();
    const bool reverse = m_fForceSpeed < 0.f;
    const auto arrived = reverse ? (m_dwCurrentIndex + count - 1) % count : m_dwCurrentIndex;
    m_CurrentCameraPos[0] = m_vTourCameraPos[0] = m_listWayPoint[arrived].fCameraX;
    m_CurrentCameraPos[1] = m_vTourCameraPos[1] = m_listWayPoint[arrived].fCameraY;
    m_dwCurrentIndex = static_cast<DWORD>(reverse ? arrived : (arrived + 1) % count);
    m_iDelayCount = 0.0;
}

void CCameraMove::UpdateTourDistance(const WAYPOINT &origin, const WAYPOINT &target,
                                     float distanceToOrigin, float distanceToTarget)
{
    const float totalBlendDistance = distanceToOrigin + distanceToTarget;
    if (totalBlendDistance > std::numeric_limits<float>::epsilon())
    {
        m_fCurrentDistanceLevel =
            origin.fCameraDistanceLevel * distanceToTarget / totalBlendDistance +
            target.fCameraDistanceLevel * distanceToOrigin / totalBlendDistance;
    }
    else
    {
        m_fCurrentDistanceLevel = target.fCameraDistanceLevel;
    }
}

void CCameraMove::SetAngleFrustum(float _Value)
{
    m_fCameraAngle = _Value;
}
void CCameraMove::SetFrustumAngle(float _Value)
{
    m_fFrustumAngle = _Value;
}
float CCameraMove::GetFrustumAngle()
{
    return (m_fCameraAngle - m_fFrustumAngle);
}

// Note: stdafx.h includes:
// - _types.h which has IdentityVector3D() for clearing vec3_t
// - ZzzMathLib.h which has AngleMatrix()

// Global camera state instance
CameraState::CameraState()
{
    Reset();
}

void CameraState::Reset()
{
    // Transform
    IdentityVector3D(Position); // Clears vec3_t to zero (defined in _types.h)
    IdentityVector3D(Angle);
    memset(Matrix, 0, sizeof(Matrix));

    // View frustum
    ViewNear = 20.0f;
    ViewFar = 2000.0f;
    FOV = 55.0f;

    // Camera behavior
    Distance = 1000.0f;
    DistanceTarget = 1000.0f;
    ZoomLevel = 0;
    CustomDistance = 0.0f;

    // Projection cache
    PerspectiveX = 0.0f;
    PerspectiveY = 0.0f;
    ScreenCenterX = 0;
    ScreenCenterY = 0;
    ScreenCenterYFlip = 0;
}

void CameraState::UpdateMatrix()
{
    // Match the world-to-view rotation used by BeginOpengl: Ry * Rx * Rz.
    const float x = Angle[0] * Q_PI / 180.f;
    const float y = Angle[1] * Q_PI / 180.f;
    const float z = Angle[2] * Q_PI / 180.f;
    const float sx = sinf(x), cx = cosf(x), sy = sinf(y), cy = cosf(y);
    const float sz = sinf(z), cz = cosf(z);
    Matrix[0][0] = cy * cz + sy * sx * sz;
    Matrix[0][1] = -cy * sz + sy * sx * cz;
    Matrix[0][2] = sy * cx;
    Matrix[1][0] = cx * sz;
    Matrix[1][1] = cx * cz;
    Matrix[1][2] = -sx;
    Matrix[2][0] = -sy * cz + cy * sx * sz;
    Matrix[2][1] = sy * sz + cy * sx * cz;
    Matrix[2][2] = cy * cx;
    for (int row = 0; row < 3; ++row)
        Matrix[row][3] = -DotProduct(Matrix[row], Position);
}

bool SessionVisualUnit::UpdateCameraOnOwner() noexcept
{
    auto *visual = sessionKeeper_.VisualView();
    if (!visual)
        return false;
    if (!visual->UsesSessionOwnedLegacyBehavior())
        return true;
    if (SceneFlag == MAIN_SCENE && !MainSceneReady)
        return true;
    if (SceneFlag == MAIN_SCENE || SceneFlag == CHARACTER_SCENE || SceneFlag == LOG_IN_SCENE)
        MoveMainCamera();
    return true;
}

bool SessionVisualUnit::MoveMainCamera()
{
    const bool result = cameraManager_.Update(FPS_ANIMATION_FACTOR);
    sessionKeeper_.TerrainStorage().mainCameraLocked = result;
    CacheActiveFrustum();
    return result;
}

bool SessionLegacyCalls::MoveMainCamera()
{
    return sessionKeeper_.Visual()->MoveMainCamera();
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
// LoginScene fallback when no tour waypoints available
constexpr float LOGIN_SCENE_FALLBACK_Z = 500.0f;
constexpr float LOGIN_SCENE_FALLBACK_PITCH = -80.0f;

// Sentinel value used to invalidate the frustum cache (forces next-frame rebuild)
constexpr float CACHE_INVALIDATE_SENTINEL = -999999.0f;

// Camera position tuning constants. The values were inherited as magic
// numbers in the legacy code; they're kept identical here, just named.
constexpr float TOUR_VIEWFAR_PER_LEVEL = 390.f;        // ViewFar = 390 * tourLevel
constexpr float CAMERA_DISTANCE_HEIGHT_OFFSET = 150.f; // Subtracted from Distance for Z
constexpr float CUSTOM_DISTANCE_PITCH_DEG = -45.f;     // Pitch used for custom-distance offset

constexpr float TOUR_BASE_DISTANCE = 1100.f; // Base distance in tour & Battle Castle

// Player-controlled zoom ladder. Single source of truth for both the
// hero-camera distance and the far-plane multiplier per step. Add or
// remove entries here to change the ladder; the bounds and the
// F8/Ctrl+wheel handlers index into this table directly.
// viewFarMult holds at 1.00 through the default level (1300) — pulling
// closer doesn't need more far plane — and ramps up beyond it so the
// far clip keeps up with the camera as it pulls back. The 4..7
// multipliers are inherited from the legacy curve.
struct ZoomLevel
{
    float distance;    // distance from hero in world units
    float viewFarMult; // multiplier on m_Config.farPlane
};
constexpr ZoomLevel PLAYER_ZOOM_LADDER[] = {
    {1000.f, 1.00f}, // 0
    {1100.f, 1.00f}, // 1
    {1200.f, 1.00f}, // 2
    {1300.f, 1.00f}, // 3 — default (F11 reset target)
    {1400.f, 1.04f}, // 4
    {1500.f, 1.08f}, // 5
    {1600.f, 1.23f}, // 6
    {1700.f, 1.33f}, // 7
};
constexpr int PLAYER_ZOOM_LEVEL_DEFAULT = 3;
constexpr int PLAYER_ZOOM_LEVEL_COUNT = static_cast<int>(std::size(PLAYER_ZOOM_LADDER));
} // namespace

DefaultCamera::DefaultCamera(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), m_State(keeper.CameraStateObject()),
      g_Camera(keeper.CameraStateObject()), gMapManager(keeper.MapManagerObject()),
      cameraMove_(keeper.CameraMoveObject()), cameraManager_(keeper.CameraManagerObject()),
      cameraProjection_(keeper.CameraProjectionObject()), g_Direction(keeper.DirectionObject()),
      MouseWheel(keeper.MouseWheelState()), WindowWidth(keeper.PlatformWindowWidth()),
      WindowHeight(keeper.PlatformWindowHeight()),
      m_Config(
          CameraConfig::ForMainSceneOrbitalCamera()) // Phase 1: Initialize with gameplay config
{
    m_FrustumCache = {};
}

bool DefaultCamera::IsHeroValid() const
{
    return (Hero != nullptr && Hero->Object.Live);
}

void DefaultCamera::Reset()
{
    m_State.Reset();
    // Phase 5: Reset scene tracking to force config reload on next Update()
    m_LastSceneFlag = -1;
}

void DefaultCamera::ApplyConfigToState()
{
    float aspect = (float)WindowWidth / (float)WindowHeight;
    m_State.ViewFar = m_Config.farPlane;
    m_State.FOV = HFovToVFov(m_Config.hFov, aspect);
}

void DefaultCamera::InvalidateFrustumCache()
{
    m_FrustumCache.Position[0] = m_FrustumCache.Position[1] = m_FrustumCache.Position[2] =
        CACHE_INVALIDATE_SENTINEL;
    m_FrustumCache.Angle[0] = m_FrustumCache.Angle[1] = m_FrustumCache.Angle[2] =
        CACHE_INVALIDATE_SENTINEL;
    m_FrustumCache.ViewFar = CACHE_INVALIDATE_SENTINEL;
}

void DefaultCamera::InitCharacterScene()
{
    m_Config = CameraConfig::ForCharacterScene();
    ApplyConfigToState();

    m_State.Angle[0] = CharacterSceneCamera::ANGLE_PITCH;
    m_State.Angle[1] = 0.0f;
    m_State.Angle[2] = CharacterSceneCamera::ANGLE_ROLL;
    m_State.Position[0] = CharacterSceneCamera::POSITION_X;
    m_State.Position[1] = CharacterSceneCamera::POSITION_Y;
    m_State.Position[2] = CharacterSceneCamera::POSITION_Z;
}

void DefaultCamera::InitMainScene()
{
    m_Config = CameraConfig::ForMainSceneDefaultCamera();
    ApplyConfigToState();

    // Initial position will be recalculated from Hero on the next Update() frame
    if (IsHeroValid())
    {
        VectorCopy(Hero->Object.Position, m_State.Position);
    }
}

void DefaultCamera::InitLoginScene()
{
    m_Config = CameraConfig::ForLoginScene();
    ApplyConfigToState();

    // Initialize to LoginScene WALK_PATHS[0] starting position.
    // Tour mode will update this, but we need a valid initial position.
    if (!cameraMove_.IsTourMode())
        return;

    vec3_t tourPos;
    cameraMove_.GetCurrentCameraPos(tourPos);
    if (tourPos[0] != 0.0f || tourPos[1] != 0.0f || tourPos[2] != 0.0f)
    {
        VectorCopy(tourPos, m_State.Position);
        m_State.Angle[0] = cameraMove_.GetAngleFrustum();
        m_State.Angle[1] = 0.0f;
        m_State.Angle[2] = cameraMove_.GetCameraAngle();
        return;
    }

    // Fallback: Use WALK_PATHS[0] with transformation
    vec3_t startPos = {0.f, 0.f, LOGIN_SCENE_FALLBACK_Z};
    vec3_t startAngle = {LOGIN_SCENE_FALLBACK_PITCH, 0.f, 0.f};
    float tempAngle[3] = {0.f, 0.f, startAngle[2]};
    float Matrix[3][4];
    AngleMatrix(tempAngle, Matrix);
    VectorIRotate(startPos, Matrix, m_State.Position);
    VectorCopy(startAngle, m_State.Angle);
}

void DefaultCamera::ResetForScene(EGameScene scene)
{
    switch (scene)
    {
    case CHARACTER_SCENE:
        InitCharacterScene();
        break;
    case MAIN_SCENE:
        InitMainScene();
        break;
    case LOG_IN_SCENE:
        InitLoginScene();
        break;
    case SERVER_LIST_SCENE:
    case WEBZEN_SCENE:
    case LOADING_SCENE:
    default:
        m_Config = CameraConfig::ForMainSceneOrbitalCamera();
        break;
    }

    InvalidateFrustumCache();
}

void DefaultCamera::OnActivate(const CameraState &previousState, float animationFactor)
{
    // Phase 5: When activating, ensure camera is configured for current scene

    CAMERA_LOG(
        "[CAM] DefaultCamera::OnActivate - Scene=%d, PrevPos=(%.1f,%.1f,%.1f), PrevAngle=(%.1f,%.1f,%.1f), PrevDist=%.0f",
        (int)SceneFlag, previousState.Position[0], previousState.Position[1],
        previousState.Position[2], previousState.Angle[0], previousState.Angle[1],
        previousState.Angle[2], previousState.Distance);

    // FIX: Save previousState before ResetForScene overwrites m_State
    vec3_t savedPosition, savedAngle;
    float savedDistance = previousState.Distance;
    float savedDistanceTarget = previousState.DistanceTarget;
    VectorCopy(previousState.Position, savedPosition);
    VectorCopy(previousState.Angle, savedAngle);

    // ALWAYS call ResetForScene to ensure config is properly loaded
    // This guarantees correct config for each scene (MainScene uses ForMainScene, others use ForGameplay)
    // ResetForScene also handles position/angle initialization for each scene
    ResetForScene(SceneFlag);

    CAMERA_LOG("[CAM]   Config: Far=%.0f, hFOV=%.1f, TerrainCull=%.0f", m_Config.farPlane,
               m_Config.hFov, m_Config.terrainCullRange);

    // FIX: Inherit position and angles from previous camera for seamless transition
    VectorCopy(savedPosition, m_State.Position);
    VectorCopy(savedAngle, m_State.Angle);
    m_State.Distance = savedDistance;
    m_State.DistanceTarget = savedDistanceTarget;

    CAMERA_LOG("[CAM]   After inherit: Pos=(%.1f,%.1f,%.1f), Angle=(%.1f,%.1f,%.1f), Dist=%.0f",
               m_State.Position[0], m_State.Position[1], m_State.Position[2], m_State.Angle[0],
               m_State.Angle[1], m_State.Angle[2], m_State.Distance);

    // Pre-compute the target pose so frame 1 already renders at the
    // Default camera's natural position. Without this, the first frame
    // after the switch would render the inherited (previous-camera) pose
    // and snap to the new pose on frame 2 — visible as a one-frame
    // character offset on screen.
    // Mirror the normal Update() pipeline exactly: reset angles to the
    // canonical default-camera baseline (0, 0, -45) BEFORE
    // CalculateCameraPosition reads them, then let SetCameraAngle apply
    // the per-scene pitch override. CalculateCameraPosition rotates the
    // (0, -Distance, 0) body offset by m_State.Angle, so passing in the
    // inherited orbital yaw/pitch here would produce a position that
    // doesn't match the default camera's canonical pose.
    if (SceneFlag == MAIN_SCENE && IsHeroValid())
    {
        g_shCameraLevel = static_cast<short>(m_PlayerZoomLevel);
        m_State.Distance = PLAYER_ZOOM_LADDER[m_PlayerZoomLevel].distance;
        m_State.DistanceTarget = m_State.Distance;

        m_State.Angle[0] = 0.f;
        m_State.Angle[1] = 0.f;
        m_State.Angle[2] = -45.f;
        SetCameraFOV();

        UpdateMountOffset(animationFactor);
        CalculateCameraPosition(animationFactor);
        SetCameraAngle();
        m_State.UpdateMatrix();
    }

    // Update scene tracking to prevent redundant reset in Update()
    m_LastSceneFlag = (int)SceneFlag;

    // FIX: Mark as just activated to skip first frame position recalculation and disable smoothing
    m_bJustActivated = true;
    m_FramesSinceActivation = 0;
}

void DefaultCamera::OnDeactivate()
{
    // Nothing to clean up.
}

bool DefaultCamera::Update(float animationFactor)
{
    // Phase 5: Detect scene transitions and properly reset for new scene
    if (m_LastSceneFlag != (int)SceneFlag)
    {
        // Scene changed - use dedicated reset function
        m_LastSceneFlag = (int)SceneFlag;
        ResetForScene(SceneFlag);
    }

    // Player input: mouse wheel cycles the zoom ladder when CameraManager
    // is unlocked (F10 toggle). Only meaningful in MainScene;
    // CalculateCameraPosition() seeds g_shCameraLevel from m_PlayerZoomLevel
    // each frame outside cutscenes.
    if (SceneFlag == MAIN_SCENE)
        HandleWheelZoom();

    // FIX: Check if LoginScene is using WALK_PATHS animation (tour mode OFF)
    // In this case, MoveCamera() in LoginScene updates g_Camera directly
    // We should only update frustum and skip position/angle calculations
    if (SceneFlag == LOG_IN_SCENE && !cameraMove_.IsTourMode())
    {
        // LoginScene WALK_PATHS animation is active
        // g_Camera is updated by LoginScene::MoveCamera() -> MoveCharacterCamera()
        // Always update frustum since camera is moving every frame
        UpdateFrustum();
        return false; // Camera not locked
    }

    // Note: Tour mode is handled internally by CalculateCameraPosition() and SetCameraAngle()
    // No need to return early - let normal flow continue

    bool bLockCamera = false;

    // FIX: Skip position/angle recalculation on first frame to preserve inherited state
    if (!m_bJustActivated)
    {
        // Initialize default angles (SetCameraAngle will override these for specific scenes)
        m_State.Angle[0] = 0.f;
        m_State.Angle[1] = 0.f;
        m_State.Angle[2] = -45.f;

        SetCameraFOV();

#ifdef ENABLE_EDIT2
        HandleEditorMode(animationFactor);
#endif

        UpdateMountOffset(animationFactor);
        CalculateCameraPosition(animationFactor);
        SetCameraAngle();
        UpdateCustomCameraDistance(animationFactor);
    }
    else
    {
        // First frame after activation - preserve inherited position/angles
        // Still need to call SetCameraFOV for proper FOV setup
        SetCameraFOV();

        CAMERA_LOG("[CAM] DefaultCamera first frame: Pos=(%.1f,%.1f,%.1f), Angle=(%.1f,%.1f,%.1f)",
                   m_State.Position[0], m_State.Position[1], m_State.Position[2], m_State.Angle[0],
                   m_State.Angle[1], m_State.Angle[2]);

        m_bJustActivated = false;
    }

    // Increment frame counter for smoothing control
    if (m_FramesSinceActivation < 10) // Cap to prevent overflow
        m_FramesSinceActivation++;

    UpdateCameraDistance(animationFactor);

    // Phase 5 fix: Update frustum only when camera state actually changes
    // This avoids expensive frustum rebuild every frame (20-25% performance gain)
    if (NeedsFrustumUpdate())
    {
        UpdateFrustum();
    }

    // Phase 5: Sync camera state to legacy g_Camera global
    // This is needed because BeginOpengl() still uses g_Camera.FOV for perspective setup
    VectorCopy(m_State.Position, g_Camera.Position);
    VectorCopy(m_State.Angle, g_Camera.Angle);
    g_Camera.FOV = m_State.FOV;
    // g_Camera.ViewNear intentionally NOT set from m_Config.nearPlane — the
    // CameraConfig nearPlane is for frustum culling; gluPerspective uses the
    // legacy ViewNear (20.0) for depth precision. Syncing would clip geometry.

    // FIX Issue #1: Use m_Config.farPlane for rendering, not zoom-adjusted m_State.ViewFar
    float effectiveFarPlane = m_Config.farPlane;
    g_Camera.ViewFar = effectiveFarPlane;

    return bLockCamera;
}

void DefaultCamera::CalculateCameraViewFar()
{
    // Use config's farPlane as single source of truth.
    // Per-map ViewFar multipliers (BattleCastle/PK Field/6th-char-home/etc)
    // were removed so all gameplay maps share the same zoom-level scaling.
    float baseFarPlane = m_Config.farPlane;

    // Login/character scenes ignore the gameplay zoom ladder — they use
    // their own scene-specific far plane.
    if (SceneFlag == LOG_IN_SCENE || SceneFlag == CHARACTER_SCENE)
    {
        m_State.ViewFar = m_Config.farPlane;
        return;
    }
    if (g_shCameraLevel == 0 && g_Direction.m_CKanturu.IsMayaScene(gMapManager.ContextMap()))
    {
        m_State.ViewFar = baseFarPlane * 0.96f; // Slightly less for Kanturu Maya
        return;
    }

    if (g_shCameraLevel >= 0 && g_shCameraLevel < PLAYER_ZOOM_LEVEL_COUNT)
        m_State.ViewFar = baseFarPlane * PLAYER_ZOOM_LADDER[g_shCameraLevel].viewFarMult;
    else
        m_State.ViewFar = baseFarPlane; // cutscene / out-of-range fallback
}

namespace
{
// Per-mount Z offsets (world units above terrain).
// Camera lift for ground mounts (character stays on terrain, camera rises
// for a better vantage point). Dinorant gets 0 because MoveCharacterPosition
// already lifts the character model (+30 normal, +90 sky maps).
constexpr float MOUNT_OFFSET_GROUND = 30.0f;

// Time-based smooth transition for mount/dismount (frame-rate independent).
// Uses exponential decay: reaches ~95% of target in MOUNT_TRANSITION_MS.
constexpr float MOUNT_TRANSITION_MS = 500.0f;
constexpr float MOUNT_LERP_TAU = MOUNT_TRANSITION_MS / 3000.0f; // time constant (seconds)

// Snap threshold — when the offset is close enough, snap to target
// to avoid endless micro-oscillation.
constexpr float MOUNT_LERP_SNAP_THRESHOLD = 0.5f;
} // namespace

float DefaultCamera::GetTargetMountOffset() const
{
    if (!IsHeroValid() || Hero->SafeZone || gMapManager.ContextMap() == -1)
        return 0.0f;

    switch (Hero->Helper.Type)
    {
    case MODEL_HORN_OF_DINORANT:
        return 0.0f; // Character model already floats via MoveCharacterPosition
    case MODEL_HORN_OF_UNIRIA:
    case MODEL_DARK_HORSE_ITEM:
    case MODEL_HORN_OF_FENRIR:
        return MOUNT_OFFSET_GROUND;
    default:
        return 0.0f;
    }
}

void DefaultCamera::SyncMountOffset()
{
    m_CurrentMountOffset = GetTargetMountOffset();
    m_LastMountType = IsHeroValid() ? Hero->Helper.Type : -1;
}

void DefaultCamera::UpdateMountOffset(float animationFactor)
{
    if (!IsHeroValid())
        return;

    // Compute target mount offset and lerp for camera
    float targetOffset = GetTargetMountOffset();

    if (Hero->Helper.Type != m_LastMountType)
        m_LastMountType = Hero->Helper.Type;

    // Time-based exponential lerp (frame-rate independent).
    float dt = animationFactor / Core::Time::DefaultReferenceFps;
    float delta = targetOffset - m_CurrentMountOffset;
    if (fabsf(delta) < MOUNT_LERP_SNAP_THRESHOLD)
        m_CurrentMountOffset = targetOffset;
    else
        m_CurrentMountOffset += delta * (1.0f - expf(-dt / MOUNT_LERP_TAU));
}

void DefaultCamera::CalculateCameraPosition(float animationFactor)
{
    vec3_t Position, TransformPosition;
    float Matrix[3][4];

    CalculateCameraViewFar();

    Vector(0.f, -m_State.Distance, 0.f, Position);
    AngleMatrix(m_State.Angle, Matrix);
    VectorIRotate(Position, Matrix, TransformPosition);

    // Phase 5 fix: Handle tour mode (LoginScene) BEFORE Hero check
    if (cameraMove_.IsTourMode())
    {
        cameraMove_.UpdateTourWayPoint(animationFactor);
        cameraMove_.GetCurrentCameraPos(Position);
        m_State.ViewFar = TOUR_VIEWFAR_PER_LEVEL * cameraMove_.GetCurrentCameraDistanceLevel();

        // Tour mode handles camera completely - set position and return
        VectorAdd(Position, TransformPosition, m_State.Position);
        return;
    }

    // Phase 5: NULL check - if Hero invalid and not in tour mode, use fallback
    if (!IsHeroValid())
    {
        // Fallback: Use CharacterScene or LoginScene static positions
        // (SetCameraAngle() sets hardcoded positions for CHARACTER_SCENE)
        return;
    }

    int iIndex = TERRAIN_INDEX((Hero->PositionX), (Hero->PositionY));

    if (SceneFlag == MAIN_SCENE)
    {
        g_pCatapultWindow->GetCameraPos(Position);
    }

    if (g_Direction.IsDirection(gMapManager.ContextMap()) && !g_Direction.m_bDownHero)
    {
        g_shCameraLevel = g_Direction.GetCameraPosition(Position);
    }
    else if (SceneFlag == MAIN_SCENE)
    {
        // Re-seed the global from the player's persistent zoom level so the
        // wheel input handler isn't fighting a per-frame reset to 0.
        // Other scenes (login / character) keep g_shCameraLevel at 0 so
        // ZzzLodTerrain's CharacterScene Width formula and similar
        // level-keyed paths still hit their case-0 branch.
        g_shCameraLevel = static_cast<short>(m_PlayerZoomLevel);
    }
    else
    {
        g_shCameraLevel = 0;
    }

    if (cameraMove_.IsTourMode())
    {
        vec3_t temp = {0.0f, 0.0f, -100.0f};
        VectorAdd(TransformPosition, temp, TransformPosition);
    }

    VectorAdd(Position, TransformPosition, m_State.Position);

    if (!cameraMove_.IsTourMode())
    {
        m_State.Position[2] = Hero->Object.Position[2];
    }

    if ((TerrainWall[iIndex] & TW_HEIGHT) == TW_HEIGHT)
    {
        m_State.Position[2] = g_fSpecialHeight;
    }
    m_State.Position[2] += m_State.Distance - CAMERA_DISTANCE_HEIGHT_OFFSET;

    // Raise camera when mounted (smooth lerp computed in UpdateMountOffset).
    // Character stays on the ground; only the camera view lifts.
    m_State.Position[2] += m_CurrentMountOffset;

    // Apply custom camera distance for special terrain
    if (m_State.CustomDistance != 0.f)
    {
        vec3_t angle = {0.f, 0.f, CUSTOM_DISTANCE_PITCH_DEG};
        Vector(0.f, m_State.CustomDistance, 0.f, Position);
        AngleMatrix(angle, Matrix);
        VectorIRotate(Position, Matrix, TransformPosition);
        VectorAdd(m_State.Position, TransformPosition, m_State.Position);
    }
}

void DefaultCamera::SetCameraAngle()
{
    if (cameraMove_.IsTourMode())
    {
        // Apply angle offsets for LoginScene camera tuning
        cameraMove_.SetAngleFrustum(-112.5f);
        m_State.Angle[0] = cameraMove_.GetAngleFrustum();
        m_State.Angle[1] = 0.0f;
        m_State.Angle[2] = cameraMove_.GetCameraAngle();

        // Apply offsets only in LoginScene (WD_73NEW_LOGIN_SCENE)
        if (gMapManager.ContextMap() == 73)
        {
            m_State.Angle[0] += g_LoginSceneAnglePitch;
            m_State.Angle[2] += g_LoginSceneAngleYaw;
        }
    }
    else if (SceneFlag == CHARACTER_SCENE)
    {
        m_State.Angle[0] = -84.5f;
        m_State.Angle[1] = 0.0f;
        m_State.Angle[2] = -75.0f;
        m_State.Position[0] = 9758.93f;
        m_State.Position[1] = 18913.11f;
        m_State.Position[2] = 675.5f;
    }
    else
    {
        m_State.Angle[0] = -48.5f;
    }

    m_State.Angle[0] += EarthQuake;
}

void DefaultCamera::UpdateCustomCameraDistance(float animationFactor)
{
    // Phase 5: NULL check - custom distance only applies when Hero exists
    if (!IsHeroValid())
        return;

    int iIndex = TERRAIN_INDEX((Hero->PositionX), (Hero->PositionY));

    if ((TerrainWall[iIndex] & TW_CAMERA_UP) == TW_CAMERA_UP)
    {
        if (m_State.CustomDistance <= CUSTOM_CAMERA_DISTANCE1)
        {
            m_State.CustomDistance = (std::min)(static_cast<float>(CUSTOM_CAMERA_DISTANCE1),
                                                m_State.CustomDistance + 10.f * animationFactor);
        }
    }
    else
    {
        if (m_State.CustomDistance > 0)
        {
            m_State.CustomDistance =
                (std::max)(0.f, m_State.CustomDistance - 10.f * animationFactor);
        }
    }
}

void DefaultCamera::ResetView()
{
    m_PlayerZoomLevel = PLAYER_ZOOM_LEVEL_DEFAULT;
}

void DefaultCamera::HandleWheelZoom()
{
    if (MouseWheel == 0)
        return;

    // Always consume the wheel, even when locked. Otherwise a wheel tick
    // received while locked stays in MouseWheel and fires the moment the
    // player unlocks (looks like a phantom zoom on F10 release).
    const int wheel = MouseWheel;
    MouseWheel = 0;

    if (cameraManager_.IsZoomLocked())
        return;

    // Wheel up (positive ticks) zooms in -> lower index. MouseWheel is
    // already normalized to ticks in Winmain (HIWORD / WHEEL_DELTA), so a
    // fast scroll producing 2+ ticks per frame moves multiple ladder rungs.
    m_PlayerZoomLevel -= wheel;

    if (m_PlayerZoomLevel < 0)
        m_PlayerZoomLevel = 0;
    if (m_PlayerZoomLevel >= PLAYER_ZOOM_LEVEL_COUNT)
        m_PlayerZoomLevel = PLAYER_ZOOM_LEVEL_COUNT - 1;
}

void DefaultCamera::UpdateCameraDistance(float animationFactor)
{
    if (cameraMove_.IsTourMode())
    {
        m_State.DistanceTarget =
            TOUR_BASE_DISTANCE * cameraMove_.GetCurrentCameraDistanceLevel() * 0.1f;
        m_State.Distance = m_State.DistanceTarget;
        return;
    }

    if (g_shCameraLevel >= 0 && g_shCameraLevel < PLAYER_ZOOM_LEVEL_COUNT)
        m_State.DistanceTarget = PLAYER_ZOOM_LADDER[g_shCameraLevel].distance;
    else
        m_State.DistanceTarget = PLAYER_ZOOM_LADDER[PLAYER_ZOOM_LEVEL_DEFAULT].distance;

    // Disable distance smoothing for first 2 frames after activation to
    // prevent visible interpolation when switching from OrbitalCamera.
    if (m_FramesSinceActivation < 2)
        m_State.Distance = m_State.DistanceTarget;
    else
        m_State.Distance += (m_State.DistanceTarget - m_State.Distance) *
                            Core::Time::Blend(1.f / 3.f, animationFactor);
}

void DefaultCamera::SetCameraFOV()
{
    float aspect = (float)WindowWidth / (float)WindowHeight;

    if (gMapManager.ContextMap() == WD_73NEW_LOGIN_SCENE && cameraMove_.IsTourMode())
    {
        // Tour mode uses wider FOV for cinematic feel
        m_State.FOV = HFovToVFov(m_Config.hFov, aspect);
    }
    else
    {
        // Compute vertical FOV from horizontal FOV and current aspect ratio
        m_State.FOV = HFovToVFov(m_Config.hFov, aspect);
    }
}

#ifdef ENABLE_EDIT2
void DefaultCamera::HandleEditorMode(float animationFactor)
{
    if (g_pUIManager->IsInputEnable())
        return;
    constexpr float RotationStep = 15.f;
    if (IsKeyDown(VK_INSERT))
        m_State.Angle[2] += RotationStep;
    if (IsKeyDown(VK_DELETE))
        m_State.Angle[2] -= RotationStep;
    if (IsKeyDown(VK_HOME))
        m_State.Angle[2] = -45.f;
    m_State.Angle[2] = fmodf(m_State.Angle[2] + 360.f, 360.f) - 360.f;
}
#endif //ENABLE_EDIT2

// ========== Phase 1: Configuration & Frustum Management ==========

void DefaultCamera::SetConfig(const CameraConfig &config)
{
    m_Config = config;
    UpdateFrustum();
}

void DefaultCamera::UpdateFrustum()
{
    // Derive forward/up from camera angles matching GL rotation convention.
    // BeginOpengl applies: glRotatef(A1,0,1,0) * glRotatef(A0,1,0,0) * glRotatef(A2,0,0,1)
    // => R = Ry(A1) * Rx(A0) * Rz(A2).  Forward = R^T*(0,0,-1), Up = R^T*(0,1,0).
    float a0 = m_State.Angle[0] * (Q_PI / 180.0f);
    float a1 = m_State.Angle[1] * (Q_PI / 180.0f);
    float a2 = m_State.Angle[2] * (Q_PI / 180.0f);
    float s0 = sinf(a0), c0 = cosf(a0);
    float s1 = sinf(a1), c1 = cosf(a1);
    float s2 = sinf(a2), c2 = cosf(a2);

    vec3_t forward, up;
    forward[0] = s1 * c2 - c1 * s0 * s2;
    forward[1] = -(s1 * s2 + c1 * s0 * c2);
    forward[2] = -c1 * c0;
    VectorNormalize(forward);

    up[0] = c0 * s2;
    up[1] = c0 * c2;
    up[2] = -s0;
    VectorNormalize(up);

    // Build frustum from current configuration
    float aspectRatio = (float)WindowWidth / (float)WindowHeight;

    // Phase 5 FIX: ALWAYS use m_Config values for frustum culling
    // (Override was already applied at the top of this function.)
    float effectiveFarPlane = m_Config.farPlane;
    float effectiveTerrainCullRange = m_Config.terrainCullRange;

    // Convert horizontal FOV to vertical FOV for frustum building
    float vFov = HFovToVFov(m_Config.hFov, aspectRatio);

    m_Frustum.BuildFromCamera(m_State.Position, forward, up, vFov, aspectRatio, m_Config.nearPlane,
                              effectiveFarPlane, effectiveTerrainCullRange);

    // Phase 5: Cache current state for next frame's comparison
    VectorCopy(m_State.Position, m_FrustumCache.Position);
    VectorCopy(m_State.Angle, m_FrustumCache.Angle);
    m_FrustumCache.ViewFar = effectiveFarPlane;
    m_FrustumCache.AspectRatio = aspectRatio;
}

bool DefaultCamera::NeedsFrustumUpdate() const
{
    // Phase 5: Check if camera state changed since last frustum rebuild
    const float EPSILON = 0.01f; // Small threshold to avoid floating point comparison issues

    // Check position change
    if (fabs(m_State.Position[0] - m_FrustumCache.Position[0]) > EPSILON ||
        fabs(m_State.Position[1] - m_FrustumCache.Position[1]) > EPSILON ||
        fabs(m_State.Position[2] - m_FrustumCache.Position[2]) > EPSILON)
    {
        return true;
    }

    // Check angle change
    if (fabs(m_State.Angle[0] - m_FrustumCache.Angle[0]) > EPSILON ||
        fabs(m_State.Angle[1] - m_FrustumCache.Angle[1]) > EPSILON ||
        fabs(m_State.Angle[2] - m_FrustumCache.Angle[2]) > EPSILON)
    {
        return true;
    }

    // Check ViewFar change
    if (fabs(m_State.ViewFar - m_FrustumCache.ViewFar) > EPSILON)
    {
        return true;
    }

    // Check aspect ratio change (window resize / runtime resolution switch).
    // Frustum width depends on aspect; without this the cache would stay valid
    // through a resize and culling at the screen edges would go stale.
    const float aspectRatio = (float)WindowWidth / (float)WindowHeight;
    if (fabs(aspectRatio - m_FrustumCache.AspectRatio) > EPSILON)
    {
        return true;
    }

    return false; // No significant change, skip frustum rebuild
}

namespace
{
// Epsilon when comparing cull distances to decide whether the terrain 2D-hull
// extension is needed. Anything closer than this to farDist is treated as equal.
constexpr float TERRAIN_EXTENSION_EPSILON = 1.0f;

// Compute the 4 corners of a rectangular plane (top-left, top-right, bottom-right, bottom-left)
// given its center, half-dimensions, up, and right axes.
// NOTE: vec3_t params are non-const because the project's vector helpers don't take const.
inline void ComputePlaneCorners(vec3_t center, float halfHeight, float halfWidth, vec3_t up,
                                vec3_t right, vec3_t outTL, vec3_t outTR, vec3_t outBR,
                                vec3_t outBL)
{
    vec3_t temp;
    VectorMA(center, halfHeight, up, temp);
    VectorMA(temp, -halfWidth, right, outTL);
    VectorMA(center, halfHeight, up, temp);
    VectorMA(temp, halfWidth, right, outTR);
    VectorMA(center, -halfHeight, up, temp);
    VectorMA(temp, halfWidth, right, outBR);
    VectorMA(center, -halfHeight, up, temp);
    VectorMA(temp, -halfWidth, right, outBL);
}

} // namespace

Frustum::Frustum() : m_2DCount(0), m_bHasTerrainExtension(false)
{
    for (int i = 0; i < 6; i++)
    {
        Vector(0.f, 0.f, 0.f, m_Planes[i].normal);
        m_Planes[i].distance = 0.f;
    }

    for (int i = 0; i < 8; i++)
    {
        Vector(0.f, 0.f, 0.f, m_Vertices[i]);
    }

    for (int i = 0; i < 4; i++)
    {
        Vector(0.f, 0.f, 0.f, m_TerrainFarVertices[i]);
    }

    for (int i = 0; i < MAX_2D_HULL_POINTS; i++)
    {
        m_2DX[i] = 0.f;
        m_2DY[i] = 0.f;
    }
}

void Frustum::BuildFromCamera(const vec3_t position, const vec3_t forward, const vec3_t up,
                              float fovDegrees, float aspectRatio, float nearDist, float farDist,
                              float terrainCullDist)
{
    if (terrainCullDist < 0.0f)
        terrainCullDist = farDist;

    // Build orthonormal basis from camera vectors (CrossProduct needs non-const args)
    vec3_t right, forwardTemp, upTemp;
    VectorCopy(forward, forwardTemp);
    VectorCopy(up, upTemp);
    CrossProduct(forwardTemp, upTemp, right);
    VectorNormalize(right);

    vec3_t actualUp;
    CrossProduct(right, forwardTemp, actualUp);
    VectorNormalize(actualUp);

    float tanHalfFov = tanf((fovDegrees * Q_PI / 180.0f) * 0.5f);

    // Near/far plane centers, needed by CalculatePlanes
    vec3_t nearCenter, farCenter, posTemp;
    VectorCopy(position, posTemp);
    VectorMA(posTemp, nearDist, forwardTemp, nearCenter);
    VectorCopy(position, posTemp);
    VectorMA(posTemp, farDist, forwardTemp, farCenter);

    CalculateFrustumVertices(position, forward, actualUp, right, tanHalfFov, aspectRatio, nearDist,
                             farDist);

    m_bHasTerrainExtension = (terrainCullDist > farDist + TERRAIN_EXTENSION_EPSILON);
    if (m_bHasTerrainExtension)
        CalculateTerrainExtension(position, forward, actualUp, right, tanHalfFov, aspectRatio,
                                  farDist, terrainCullDist);

    CalculatePlanes(position, forward, nearCenter, farCenter);
    CalculateBoundingBox();
    Calculate2DProjection();
}

void Frustum::SetCustom2DHull(const float *xs, const float *ys, int count)
{
    if (!xs || !ys || count <= 0)
        return;
    if (count > MAX_2D_HULL_POINTS)
        count = MAX_2D_HULL_POINTS;

    // Run the points through SortPoints2D + ConvexHullCCW to produce a proper
    // convex hull in CCW order. Callers (e.g. OrbitalCamera) pass frustum corners
    // in view-space walking order, which is NOT the hull outline — rendering
    // would connect edges across the hull interior and look broken. The hull
    // computation fixes the winding for any input order.
    Point2D input[MAX_2D_HULL_POINTS];
    for (int i = 0; i < count; i++)
    {
        input[i].x = xs[i];
        input[i].y = ys[i];
    }

    SortPoints2D(input, count);

    // Andrew's monotone chain produces at most `count` hull points for `count`
    // inputs; bounding the output buffer to MAX_2D_HULL_POINTS makes the
    // capacity match m_2DX/m_2DY storage so no truncation can happen.
    Point2D hull[MAX_2D_HULL_POINTS];
    int k = ConvexHullCCW(input, count, hull, MAX_2D_HULL_POINTS);

    // Reverse to CW order — TestPoint2D's cross-product winding test expects
    // CW. Calculate2DProjection does the same flip; without it the cull test
    // inverts and tiles inside the custom hull get culled instead of those
    // outside (visible when DevEditor's orbital trapezoid override is active).
    m_2DCount = k;
    for (int i = 0; i < k; i++)
    {
        m_2DX[i] = hull[k - 1 - i].x;
        m_2DY[i] = hull[k - 1 - i].y;
    }
}

void Frustum::CalculateFrustumVertices(const vec3_t position, const vec3_t forward, const vec3_t up,
                                       const vec3_t right, float tanHalfFov, float aspectRatio,
                                       float nearDist, float farDist)
{
    float nearHeight = 2.0f * tanHalfFov * nearDist;
    float nearWidth = nearHeight * aspectRatio;
    float farHeight = 2.0f * tanHalfFov * farDist;
    float farWidth = farHeight * aspectRatio;

    vec3_t posMut, forwardMut, upMut, rightMut;
    VectorCopy(position, posMut);
    VectorCopy(forward, forwardMut);
    VectorCopy(up, upMut);
    VectorCopy(right, rightMut);

    vec3_t nearCenter, farCenter, posTemp;
    VectorCopy(posMut, posTemp);
    VectorMA(posTemp, nearDist, forwardMut, nearCenter);
    VectorCopy(posMut, posTemp);
    VectorMA(posTemp, farDist, forwardMut, farCenter);

    // Near plane: vertices 0..3 = TL, TR, BR, BL
    ComputePlaneCorners(nearCenter, nearHeight * 0.5f, nearWidth * 0.5f, upMut, rightMut,
                        m_Vertices[0], m_Vertices[1], m_Vertices[2], m_Vertices[3]);
    // Far plane: vertices 4..7 = TL, TR, BR, BL
    ComputePlaneCorners(farCenter, farHeight * 0.5f, farWidth * 0.5f, upMut, rightMut,
                        m_Vertices[4], m_Vertices[5], m_Vertices[6], m_Vertices[7]);
}

void Frustum::CalculateTerrainExtension(const vec3_t position, const vec3_t forward,
                                        const vec3_t up, const vec3_t right, float tanHalfFov,
                                        float aspectRatio, float /*farDist*/, float terrainCullDist)
{
    // When terrainCullDist > farDist, the 2D terrain tile culling range extends
    // beyond the 3D frustum far plane to match the rendering extent.
    float tcHeight = 2.0f * tanHalfFov * terrainCullDist;
    float tcWidth = tcHeight * aspectRatio;

    vec3_t posMut, forwardMut, upMut, rightMut;
    VectorCopy(position, posMut);
    VectorCopy(forward, forwardMut);
    VectorCopy(up, upMut);
    VectorCopy(right, rightMut);

    vec3_t tcCenter, posTemp;
    VectorCopy(posMut, posTemp);
    VectorMA(posTemp, terrainCullDist, forwardMut, tcCenter);

    ComputePlaneCorners(tcCenter, tcHeight * 0.5f, tcWidth * 0.5f, upMut, rightMut,
                        m_TerrainFarVertices[0], m_TerrainFarVertices[1], m_TerrainFarVertices[2],
                        m_TerrainFarVertices[3]);
}

void Frustum::CalculatePlanes(const vec3_t position, const vec3_t forward, const vec3_t nearCenter,
                              const vec3_t farCenter)
{
    // Phase 5: CRITICAL -- match old CreateFrustrum() plane indices!
    // Old winding: [0]=TOP, [1]=RIGHT, [2]=BOTTOM, [3]=LEFT
    // TestFrustrum() relies on this ordering.
    struct PlaneDef
    {
        int v2, v3;
    };
    const PlaneDef planeDefs[4] = {
        {4, 5}, // TOP:    apex → far-TL → far-TR
        {5, 6}, // RIGHT:  apex → far-TR → far-BR
        {6, 7}, // BOTTOM: apex → far-BR → far-BL
        {7, 4}, // LEFT:   apex → far-BL → far-TL
    };

    vec3_t v1, v2, v3;
    for (int i = 0; i < 4; i++)
    {
        VectorCopy(position, v1);
        VectorCopy(m_Vertices[planeDefs[i].v2], v2);
        VectorCopy(m_Vertices[planeDefs[i].v3], v3);
        FaceNormalize(v1, v2, v3, m_Planes[i].normal);
        m_Planes[i].distance = -DotProduct(position, m_Planes[i].normal);
    }

    // Near plane: forward direction
    VectorCopy(forward, m_Planes[4].normal);
    m_Planes[4].distance = -DotProduct(nearCenter, m_Planes[4].normal);

    // Far plane: negative forward direction (VectorScale needs non-const input)
    vec3_t forwardMut;
    VectorCopy(forward, forwardMut);
    VectorScale(forwardMut, -1.0f, m_Planes[5].normal);
    m_Planes[5].distance = -DotProduct(farCenter, m_Planes[5].normal);
}

bool Frustum::TestSphere(const vec3_t center, float radius) const
{
    // Test sphere against all 6 planes
    for (int i = 0; i < 6; i++)
    {
        float distance = DotProduct(center, m_Planes[i].normal) + m_Planes[i].distance;

        // If sphere is completely outside this plane, it's culled
        if (distance < -radius)
            return false;
    }

    return true;
}

bool Frustum::TestAABB(const AABB &box) const
{
    // Test AABB against all 6 planes using p-vertex method
    for (int i = 0; i < 6; i++)
    {
        // Find the most positive vertex along plane normal
        vec3_t pVertex;
        pVertex[0] = (m_Planes[i].normal[0] >= 0.0f) ? box.max[0] : box.min[0];
        pVertex[1] = (m_Planes[i].normal[1] >= 0.0f) ? box.max[1] : box.min[1];
        pVertex[2] = (m_Planes[i].normal[2] >= 0.0f) ? box.max[2] : box.min[2];

        // If p-vertex is outside, entire box is outside
        if (DotProduct(pVertex, m_Planes[i].normal) + m_Planes[i].distance < 0.0f)
            return false;
    }

    return true;
}

bool Frustum::TestPoint(const vec3_t point) const
{
    // Test point against all 6 planes
    for (int i = 0; i < 6; i++)
    {
        float distance = DotProduct(point, m_Planes[i].normal) + m_Planes[i].distance;

        // If point is outside this plane, it's culled
        if (distance < 0.0f)
            return false;
    }

    return true;
}

void Frustum::CalculateBoundingBox()
{
    // Initialize with first vertex
    VectorCopy(m_Vertices[0], m_BoundingBox.min);
    VectorCopy(m_Vertices[0], m_BoundingBox.max);

    // Expand to include all vertices
    for (int i = 1; i < 8; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            if (m_Vertices[i][j] < m_BoundingBox.min[j])
                m_BoundingBox.min[j] = m_Vertices[i][j];
            if (m_Vertices[i][j] > m_BoundingBox.max[j])
                m_BoundingBox.max[j] = m_Vertices[i][j];
        }
    }
}

void Frustum::Calculate2DProjection()
{
    // Project frustum vertices to XY ground plane in tile coordinates (world / TERRAIN_SCALE).
    // Include terrain-cull far vertices if present (extends 2D hull beyond 3D far plane).
    constexpr float WORLD_TO_TILE = 1.0f / TERRAIN_SCALE;

    int numPts = 8 + (m_bHasTerrainExtension ? 4 : 0);
    Point2D pts[MAX_2D_HULL_POINTS];

    for (int i = 0; i < 8; i++)
    {
        pts[i].x = m_Vertices[i][0] * WORLD_TO_TILE;
        pts[i].y = m_Vertices[i][1] * WORLD_TO_TILE;
    }
    if (m_bHasTerrainExtension)
    {
        for (int i = 0; i < 4; i++)
        {
            pts[8 + i].x = m_TerrainFarVertices[i][0] * WORLD_TO_TILE;
            pts[8 + i].y = m_TerrainFarVertices[i][1] * WORLD_TO_TILE;
        }
    }

    SortPoints2D(pts, numPts);

    Point2D hull[MAX_2D_HULL_POINTS];
    int k = ConvexHullCCW(pts, numPts, hull, MAX_2D_HULL_POINTS);

    // Reverse to CW order (the original TestFrustrum2D cross-product test expects CW winding)
    m_2DCount = k;
    for (int i = 0; i < k; i++)
    {
        m_2DX[i] = hull[k - 1 - i].x;
        m_2DY[i] = hull[k - 1 - i].y;
    }
}

bool Frustum::TestPoint2D(float tileX, float tileY, float range) const
{
    if (m_2DCount < 3)
        return true; // No valid 2D projection, treat as visible

    // Same cross-product winding test as original TestFrustrum2D
    // For CW-ordered polygon: d > range means inside, d <= range means outside
    int j = m_2DCount - 1;
    for (int i = 0; i < m_2DCount; j = i, i++)
    {
        float d = (m_2DX[i] - tileX) * (m_2DY[j] - tileY) - (m_2DX[j] - tileX) * (m_2DY[i] - tileY);
        if (d <= range)
            return false;
    }
    return true;
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Hero is declared in ZzzCharacter.h as CHARACTER*

namespace
{
// Match the natural-pyramid near-edge width used by ZzzLodTerrain's Orbital
// path so FreeFly-spectator hull equals the active-render hull.
constexpr float NATURAL_NEAR_HALF_WIDTH = 400.0f;

// Fallback orbit-origin parameters used by non-MainScene activation.
// We synthesize a "static camera" and ray-cast from it to find where to
// place the orbit pivot at the character's Z height.
constexpr float NON_CHAR_STATIC_CAM_HEIGHT_OFFSET = 300.0f; // above the character
constexpr float NON_CHAR_STATIC_CAM_PITCH = -45.0f;

// Sanity-check ranges for ray-cast intersection t values (world units).
constexpr float ORBIT_ORIGIN_MAX_T = 5000.0f;
constexpr float LOOKAT_MAX_T = 2000.0f;
} // namespace

OrbitalCamera::OrbitalCamera(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), m_State(keeper.CameraStateObject()),
      g_Camera(keeper.CameraStateObject()), cameraManager_(keeper.CameraManagerObject()),
      cameraProjection_(keeper.CameraProjectionObject()), Hero(keeper.HeroStorage()),
      CharactersClient(keeper.CharactersClientStorage()), MouseWheel(keeper.MouseWheelState()),
      MouseMButton(keeper.InterfaceStorage().MouseMButton),
      MouseMButtonPush(keeper.InterfaceStorage().MouseMButtonPush),
      MouseX(keeper.InterfaceStorage().MouseX), MouseY(keeper.InterfaceStorage().MouseY),
      SceneFlag(keeper.InterfaceStorage().SceneFlag), WindowWidth(keeper.PlatformWindowWidth()),
      WindowHeight(keeper.PlatformWindowHeight()), m_CameraZoom(keeper.CameraZoom()),
      m_pDefaultCamera(std::make_unique<DefaultCamera>(keeper)),
      m_Config(CameraConfig::
                   ForMainSceneOrbitalCamera()) // OrbitalCamera defaults to extended visibility
      ,
      m_bInitialOffsetSet(false), m_BaseYaw(0.0f), m_BasePitch(0.0f), m_DeltaYaw(0.0f),
      m_DeltaPitch(0.0f), m_Radius((float)m_CameraZoom),
      m_LastSceneFlag(-1) // Phase 5: Initialize to invalid scene
      ,
      m_bJustActivated(false) // Initialize activation flag
{
    IdentityVector3D(m_Target);
    IdentityVector3D(m_InitialCameraOffset);

    // Sync ViewFar with loaded zoom level
    UpdateConfigForView();
}

void OrbitalCamera::Reset()
{
    m_BaseYaw = 0.0f;
    m_BasePitch = 0.0f;
    m_DeltaYaw = 0.0f;
    m_DeltaPitch = 0.0f;
    m_Radius = (float)m_CameraZoom;
    m_Input.Rotating = false;
    m_bInitialOffsetSet = false;
    // Phase 5: Reset scene tracking to force config reload
    m_LastSceneFlag = -1;
    m_pDefaultCamera->Reset();
}

void OrbitalCamera::ResetView()
{
    // F11: drop the player's accumulated rotation and zoom radius back to
    // defaults. Yaw/pitch base stays as-is so the camera's "anchor angle"
    // for this orbit isn't lost — only the user-driven deltas are zeroed.
    m_DeltaYaw = 0.0f;
    m_DeltaPitch = 0.0f;
    m_Radius = DEFAULT_RADIUS;
    UpdateConfigForView();
    UpdateFrustum();
}

void OrbitalCamera::ResetForScene(EGameScene scene)
{
    // Save current zoom before scene transition
    PersistZoom();

    // Phase 5: Proper scene-specific reset using switch statement
    switch (scene)
    {
    case CHARACTER_SCENE:
    case MAIN_SCENE:
        LoadConfigForScene(scene);
        ApplyConfigToState();

        // Reset orbital parameters for new scene
        m_DeltaYaw = 0.0f;
        m_DeltaPitch = 0.0f;
        m_Radius = (float)m_CameraZoom;
        m_bInitialOffsetSet = false;
        break;

    case LOG_IN_SCENE:
    case SERVER_LIST_SCENE:
    case WEBZEN_SCENE:
    case LOADING_SCENE:
    default:
        LoadConfigForScene(scene);
        ApplyConfigToState();
        break;
    }

    // Update target immediately with new scene context
    UpdateTarget();

    // Force DefaultCamera to recalculate as well
    m_pDefaultCamera->ResetForScene(scene);
}

void OrbitalCamera::LoadConfigForScene(EGameScene scene)
{
    switch (scene)
    {
    case CHARACTER_SCENE:
        m_Config = CameraConfig::ForCharacterScene();
        break;
    case MAIN_SCENE:
        m_Config = CameraConfig::ForMainSceneOrbitalCamera();
        break;
    default:
        m_Config = CameraConfig::ForMainSceneOrbitalCamera();
        break;
    }
}

void OrbitalCamera::ApplyConfigToState()
{
    float aspect = (float)WindowWidth / (float)WindowHeight;
    m_State.ViewFar = m_Config.farPlane;
    m_State.FOV = HFovToVFov(m_Config.hFov, aspect);
}

void OrbitalCamera::OnActivate(const CameraState &previousState, float)
{

    CAMERA_LOG(
        "[CAM] OrbitalCamera::OnActivate - Scene=%d, PrevPos=(%.1f,%.1f,%.1f), PrevAngle=(%.1f,%.1f,%.1f), PrevDist=%.0f",
        (int)SceneFlag, previousState.Position[0], previousState.Position[1],
        previousState.Position[2], previousState.Angle[0], previousState.Angle[1],
        previousState.Angle[2], previousState.Distance);

    // Step 1: Load config (don't use ResetForScene — it would call UpdateTarget with wrong pos)
    LoadConfigForScene(SceneFlag);
    ApplyConfigToState();
    CAMERA_LOG("[CAM]   Config: Far=%.0f, hFOV=%.1f, TerrainCull=%.0f", m_Config.farPlane,
               m_Config.hFov, m_Config.terrainCullRange);

    // Step 2: Update target to Hero/Character position
    UpdateTarget();

    // Step 3: Either inherit the previous camera pose (MainScene) or ray-cast from a static
    // camera pose to find a sensible orbit origin (CharacterScene/LoginScene).
    if (SceneFlag == MAIN_SCENE)
    {
        VectorCopy(previousState.Position, m_State.Position);
        VectorCopy(previousState.Angle, m_State.Angle);
        CAMERA_LOG("[CAM]   MainScene: Inherited Pos=(%.1f,%.1f,%.1f), Angle=(%.1f,%.1f,%.1f)",
                   m_State.Position[0], m_State.Position[1], m_State.Position[2], m_State.Angle[0],
                   m_State.Angle[1], m_State.Angle[2]);
    }
    else
    {
        CalculateOrbitOriginForStaticScene(SceneFlag, previousState);
    }

    // Step 4: Compute look-at point and initialize orbital parameters from it
    m_DeltaYaw = 0.0f;
    m_DeltaPitch = 0.0f;

    vec3_t lookAtPoint;
    CalculateLookAtPoint(SceneFlag, lookAtPoint);
    InitializeOrbitalFromCurrentState(lookAtPoint, previousState);

    // Apply zoom-scaled farPlane / cull ranges now, before the first render.
    UpdateConfigForView();

    // Step 5: Materialize the orbital pose synchronously so frame 1 already
    // renders at m_Target ± (m_InitialCameraOffset · zoomScale). Without
    // this call frame 1 would render the inherited (previous-camera)
    // position and the user would see a brief one-frame "animation" as
    // frame 2 snaps to the orbital pose.
    if (SceneFlag == MAIN_SCENE)
        ComputeCameraTransform();

    // Sync to g_Camera so the global state matches what we just produced.
    m_State.UpdateMatrix();
    SyncStateToGlobalCamera();

    // Step 6: Initialize frustum and scene tracking
    UpdateFrustum();
    m_LastSceneFlag = (int)SceneFlag;

    // Skip DefaultCamera's first-frame update so our inherited pose isn't overwritten
    m_bJustActivated = true;

    // Sync the internal DefaultCamera's mount state immediately so
    // GetMountCameraOffset() returns the correct value — without this,
    // on the first-ever activation the internal DefaultCamera hasn't run
    // Update() yet and would return 0 even though the inherited position
    // already has the offset baked in from the main DefaultCamera.
    m_pDefaultCamera->SyncMountOffset();
    m_ActivationMountOffset = m_pDefaultCamera->GetMountCameraOffset();
}

void OrbitalCamera::CalculateOrbitOriginForStaticScene(EGameScene scene,
                                                       const CameraState &previousState)
{
    // CharacterScene/LoginScene: DefaultCamera uses static hardcoded positions, not
    // character-relative. Inheriting them would place the camera far from the orbit pivot,
    // so instead we ray-cast from the static camera through its forward direction to
    // find where on the character's Z plane to place the pivot.

    vec3_t staticCameraPos, staticCameraAngle;
    if (scene == CHARACTER_SCENE)
    {
        staticCameraPos[0] = CharacterSceneCamera::POSITION_X;
        staticCameraPos[1] = CharacterSceneCamera::POSITION_Y;
        staticCameraPos[2] = CharacterSceneCamera::POSITION_Z;
        staticCameraAngle[0] = CharacterSceneCamera::ANGLE_PITCH;
        staticCameraAngle[1] = 0.0f;
        staticCameraAngle[2] = CharacterSceneCamera::ANGLE_ROLL;
    }
    else
    {
        // LoginScene/others: synthesize a camera above the character
        VectorCopy(m_Target, staticCameraPos);
        staticCameraPos[2] += NON_CHAR_STATIC_CAM_HEIGHT_OFFSET;
        staticCameraAngle[0] = NON_CHAR_STATIC_CAM_PITCH;
        staticCameraAngle[1] = 0.0f;
        staticCameraAngle[2] = 0.0f;
    }

    float Matrix[3][4];
    AngleMatrix(staticCameraAngle, Matrix);
    vec3_t forward = {Matrix[1][0], Matrix[1][1], Matrix[1][2]};
    VectorNormalize(forward);

    // Ray from static camera to the character's Z plane
    float targetZ = m_Target[2];
    float t = (targetZ - staticCameraPos[2]) / forward[2];

    vec3_t orbitOrigin;
    if (t > 0.0f && t < ORBIT_ORIGIN_MAX_T)
    {
        orbitOrigin[0] = staticCameraPos[0] + t * forward[0];
        orbitOrigin[1] = staticCameraPos[1] + t * forward[1];
        orbitOrigin[2] = targetZ;
    }
    else
    {
        // Fallback: character position
        VectorCopy(m_Target, orbitOrigin);
    }

    // Inherit the previous camera pose, but orbit around the calculated origin
    VectorCopy(previousState.Position, m_State.Position);
    VectorCopy(previousState.Angle, m_State.Angle);
    VectorCopy(orbitOrigin, m_Target);

    CAMERA_LOG(
        "[CAM]   CharScene: StaticCam=(%.1f,%.1f,%.1f), CalcOrigin=(%.1f,%.1f,%.1f), InheritedPos=(%.1f,%.1f,%.1f)",
        staticCameraPos[0], staticCameraPos[1], staticCameraPos[2], orbitOrigin[0], orbitOrigin[1],
        orbitOrigin[2], m_State.Position[0], m_State.Position[1], m_State.Position[2]);
}

void OrbitalCamera::CalculateLookAtPoint(EGameScene scene, vec3_t outLookAt) const
{
    // For CharacterScene/LoginScene, m_Target already IS the orbit pivot (set above).
    if (scene != MAIN_SCENE)
    {
        VectorCopy(m_Target, outLookAt);
        return;
    }

    // MainScene: project forward from the inherited camera pose onto the Hero's Z plane
    // to find what point the camera was actually looking at.
    float Matrix[3][4];
    AngleMatrix(m_State.Angle, Matrix);
    vec3_t forward = {Matrix[1][0], Matrix[1][1], Matrix[1][2]};
    VectorNormalize(forward);

    float targetZ = m_Target[2];
    float t = (targetZ - m_State.Position[2]) / forward[2];

    if (t > 0.0f && t < LOOKAT_MAX_T)
    {
        outLookAt[0] = m_State.Position[0] + t * forward[0];
        outLookAt[1] = m_State.Position[1] + t * forward[1];
        outLookAt[2] = targetZ;
    }
    else
    {
        // Fallback: Hero position
        VectorCopy(m_Target, outLookAt);
    }
}

void OrbitalCamera::InitializeOrbitalFromCurrentState(const vec3_t lookAtPoint,
                                                      const CameraState &previousState)
{
    // Capture the offset from the *current m_Target* (= Hero, as set by
    // UpdateTarget() before this call) — NOT from lookAtPoint. The
    // orbital camera's pivot is m_Target, and UpdateTarget() will keep
    // re-setting it to Hero every frame. If we anchored the offset to
    // lookAtPoint here and overwrote m_Target = lookAtPoint, the very
    // next frame UpdateTarget would snap m_Target back to Hero and the
    // camera would jump by (Hero − lookAtPoint) — visible as a one-frame
    // character teleport on F9 switch.
    // lookAtPoint is kept as a parameter for the static-scene path
    // (CalculateOrbitOriginForStaticScene already overwrote m_Target with
    // its own pivot before calling us), but for the MainScene path it is
    // only used by SyncMountOffset / logging.
    m_InitialCameraOffset[0] = m_State.Position[0] - m_Target[0];
    m_InitialCameraOffset[1] = m_State.Position[1] - m_Target[1];
    m_InitialCameraOffset[2] = m_State.Position[2] - m_Target[2];
    m_bInitialOffsetSet = true;

    // Diagnostic distances for logging (horizontal matches DefaultCamera's distance convention)
    float dx = m_InitialCameraOffset[0];
    float dy = m_InitialCameraOffset[1];
    float horizontalDistance = sqrtf(dx * dx + dy * dy);
    float fullDistance = sqrtf(m_InitialCameraOffset[0] * m_InitialCameraOffset[0] +
                               m_InitialCameraOffset[1] * m_InitialCameraOffset[1] +
                               m_InitialCameraOffset[2] * m_InitialCameraOffset[2]);

    // Load radius from config so zoom persists across activations
    m_Radius = (float)m_CameraZoom;
    m_Radius = std::clamp(m_Radius, MIN_RADIUS, MAX_RADIUS);

    CAMERA_LOG("[CAM]   LookAtPoint: (%.1f,%.1f,%.1f), Target (Hero): (%.1f,%.1f,%.1f)",
               lookAtPoint[0], lookAtPoint[1], lookAtPoint[2], m_Target[0], m_Target[1],
               m_Target[2]);
    CAMERA_LOG("[CAM]   InitialOffset (to Target): (%.1f,%.1f,%.1f)", m_InitialCameraOffset[0],
               m_InitialCameraOffset[1], m_InitialCameraOffset[2]);
    CAMERA_LOG("[CAM]   HorizontalDist=%.1f, FullDist=%.1f, m_Radius=%.1f, prevDist=%.1f",
               horizontalDistance, fullDistance, m_Radius, previousState.Distance);
}

void OrbitalCamera::SyncStateToGlobalCamera()
{
    VectorCopy(m_State.Position, g_Camera.Position);
    VectorCopy(m_State.Angle, g_Camera.Angle);
    g_Camera.FOV = m_State.FOV;
    g_Camera.ViewFar = m_Config.farPlane;
}

void OrbitalCamera::OnDeactivate()
{
    m_Input.Rotating = false;

    PersistZoom();
}

void OrbitalCamera::PersistZoom()
{
    m_CameraZoom = static_cast<int>(m_Radius);
}

bool OrbitalCamera::Update(float animationFactor)
{
    // Phase 5: Detect scene transitions and properly reset for new scene
    if (m_LastSceneFlag != (int)SceneFlag)
    {
        // Scene changed - use dedicated reset function
        m_LastSceneFlag = (int)SceneFlag;
        ResetForScene(SceneFlag);
    }

    HandleInput();

    UpdateTarget();

    // First, let DefaultCamera calculate the base camera position
    // EXCEPT on the first frame after activation - we want to preserve inherited position
    bool skipTransformThisFrame = m_bJustActivated;

    if (!m_bJustActivated)
    {
        m_pDefaultCamera->Update(animationFactor);
    }
    else
    {
        // First frame after activation - skip DefaultCamera update to preserve inherited state
        CAMERA_LOG(
            "[CAM] OrbitalCamera: Skipping DefaultCamera update on first frame. Current Pos=(%.1f,%.1f,%.1f), Angle=(%.1f,%.1f,%.1f)",
            m_State.Position[0], m_State.Position[1], m_State.Position[2], m_State.Angle[0],
            m_State.Angle[1], m_State.Angle[2]);
        m_bJustActivated = false; // Clear flag for next frame
    }

    // Then modify it with our orbital transformations.
    // BUT skip ComputeCameraTransform on first frame to preserve inherited position
    if (!skipTransformThisFrame)
    {
        ComputeCameraTransform();

        // Apply mount camera offset as a DELTA from a fixed activation baseline.
        // m_ActivationMountOffset is captured once at OnActivate and is intentionally
        // never updated while the orbital camera is active — it is a *reference
        // point*, not a "current" value. The reasoning:
        //   - At activation, the inherited pose already had m_ActivationMountOffset
        //     baked into Position[2] (DefaultCamera applied it before handoff). The
        //     spherical (BaseYaw, BasePitch, Radius) we derived from that pose
        //     therefore reproduces an inheritedZ that includes the activation offset.
        //   - GetMountCameraOffset() is the DefaultCamera's current (lerped) offset,
        //     updated by AdjustHeroHeight() each frame.
        //   - mountDelta = current - activation. While the player is in the same
        //     mount state as activation, delta == 0 and Z matches the inherited pose.
        //     When the player dismounts, current → 0 and delta → -activation, so the
        //     camera correctly drops by the lost mount lift. When they remount,
        //     delta returns to 0 and the camera returns to the activation height.
        // Updating m_ActivationMountOffset on mount changes (a reasonable-sounding
        // "fix" against staleness) would make delta always 0 and freeze the camera
        // height across mount/dismount transitions — the opposite of what we want.
        float mountDelta = m_pDefaultCamera->GetMountCameraOffset() - m_ActivationMountOffset;
        if (fabsf(mountDelta) > 0.5f)
            m_State.Position[2] += mountDelta;

        // Clamp m_DeltaPitch based on what was actually achieved
        // If we tried to pitch but didn't move much, we're stuck at a constraint
        // This prevents accumulation when hitting ground/ceiling
        const float tolerance = 0.1f;
        if (std::abs(m_DeltaPitch - m_Input.LastEffectivePitch) > tolerance)
        {
            // We tried to pitch more but hit a constraint, clamp to effective value
            m_DeltaPitch = m_Input.LastEffectivePitch;
        }
    }

    return false; // Camera not locked
}

void OrbitalCamera::HandleInput()
{
    // Mouse wheel zoom. Always consume the wheel even when locked so a
    // tick received while F10-locked doesn't leak through on unlock.
    if (MouseWheel != 0)
    {
        const int wheel = MouseWheel;
        MouseWheel = 0;

        if (!cameraManager_.IsZoomLocked())
        {
            // 150 units/tick is intentionally a touch faster than the
            // Default-camera ladder step (100 distance/rung) so continuous
            // orbital zoom doesn't feel sluggish over the wider radius
            // range. MouseWheel is already normalized to ticks in Winmain,
            // so multiplying by it preserves multi-tick scrolls.
            const float zoomSpeed = 150.0f;
            m_Radius -= wheel * zoomSpeed;
            m_Radius = std::clamp(m_Radius, MIN_RADIUS, MAX_RADIUS);

            // Update terrain culling range based on zoom level and pitch.
            UpdateConfigForView();
            UpdateFrustum();
        }
    }

    // Middle mouse drag rotation - only rotate when button is held AND mouse moves
    // Check if button is currently pressed (not just was pressed)
    bool buttonHeld = MouseMButton;

    if (buttonHeld)
    {
        if (!m_Input.Rotating)
        {
            // Button just pressed - record starting position
            m_Input.Rotating = true;
            m_Input.LastMouseX = MouseX;
            m_Input.LastMouseY = MouseY;
        }
        else
        {
            // Button held - only rotate if mouse actually moved
            int deltaX = MouseX - m_Input.LastMouseX;
            int deltaY = MouseY - m_Input.LastMouseY;

            // Only apply rotation if there's actual mouse movement
            if (deltaX != 0 || deltaY != 0)
            {
                const float sensitivity = 0.5f;
                m_DeltaYaw += deltaX * sensitivity;
                m_DeltaPitch -= deltaY * sensitivity; // Inverted Y

                // Clamp pitch delta to prevent extreme angles
                float totalPitch = m_BasePitch + m_DeltaPitch;
                if (totalPitch < MIN_PITCH)
                    m_DeltaPitch = MIN_PITCH - m_BasePitch;
                else if (totalPitch > MAX_PITCH)
                    m_DeltaPitch = MAX_PITCH - m_BasePitch;

                // Update last position
                m_Input.LastMouseX = MouseX;
                m_Input.LastMouseY = MouseY;
            }
        }
    }
    else
    {
        // Button released - reset rotation state
        m_Input.Rotating = false;
    }
}

bool OrbitalCamera::IsHeroValid() const
{
    return (Hero != nullptr && Hero->Object.Live);
}

void OrbitalCamera::GetTargetPosition(vec3_t outTarget) const
{
    if (IsHeroValid())
    {
        // Priority 3: Hero exists - use Hero position as target
        VectorCopy(Hero->Object.Position, outTarget);
    }
    else
    {
        // Priority 4: In CharacterScene, use CharactersClient[0] as target
        if (SceneFlag == CHARACTER_SCENE)
        {
            if (CharactersClient.IsValidIndex(0) && CharactersClient[0].Object.Live)
            {
                VectorCopy(CharactersClient[0].Object.Position, outTarget);
                return;
            }
        }

        // Priority 5 (Fallback): Use current camera position as pivot
        // This allows orbital camera to work in LoginScene when no character exists
        VectorCopy(m_State.Position, outTarget);
    }
}

void OrbitalCamera::UpdateTarget()
{
    // Phase 5: Use safe GetTargetPosition() instead of direct Hero access
    GetTargetPosition(m_Target);
}

void OrbitalCamera::ComputeCameraTransform()
{
    // At this point, DefaultCamera has calculated m_State.Position, m_State.Angle, and m_State.ViewFar
    // Save the ViewFar that DefaultCamera calculated (we'll multiply it later)
    float defaultCameraViewFar = m_State.ViewFar;

    // Calculate camera offset relative to character
    float relativeX = m_State.Position[0] - m_Target[0];
    float relativeY = m_State.Position[1] - m_Target[1];
    float relativeZ = m_State.Position[2] - m_Target[2];

    // Save initial offset on first frame
    if (!m_bInitialOffsetSet)
    {
        m_InitialCameraOffset[0] = relativeX;
        m_InitialCameraOffset[1] = relativeY;
        m_InitialCameraOffset[2] = relativeZ;
        m_bInitialOffsetSet = true;
    }

    // m_Radius is the absolute camera-to-target distance in world units.
    // Normalize the captured direction and scale to m_Radius so the camera
    // sits exactly that far from m_Target — no hidden ratio.
    float initialLen = sqrtf(m_InitialCameraOffset[0] * m_InitialCameraOffset[0] +
                             m_InitialCameraOffset[1] * m_InitialCameraOffset[1] +
                             m_InitialCameraOffset[2] * m_InitialCameraOffset[2]);
    float dirScale = (initialLen > 0.001f) ? (m_Radius / initialLen) : 1.0f;

    float scaledX = m_InitialCameraOffset[0] * dirScale;
    float scaledY = m_InitialCameraOffset[1] * dirScale;
    float scaledZ = m_InitialCameraOffset[2] * dirScale;

    // Apply horizontal rotation around Z axis
    float angleRad = m_DeltaYaw * (M_PI / 180.0f);
    float rotatedX = scaledX * cosf(angleRad) - scaledY * sinf(angleRad);
    float rotatedY = scaledX * sinf(angleRad) + scaledY * cosf(angleRad);
    float rotatedZ = scaledZ;

    // Apply vertical pitch rotation
    float horizontalDist = sqrtf(rotatedX * rotatedX + rotatedY * rotatedY);
    float totalDist = sqrtf(rotatedX * rotatedX + rotatedY * rotatedY + rotatedZ * rotatedZ);
    float currentElevation = atan2f(rotatedZ, horizontalDist);

    // Calculate additional height offset when zooming in close
    // This makes the camera focus on character's head/upper body instead of feet
    // Increased from 80 to 150 to center character on screen (was showing feet at center)
    const float CAMERA_HEIGHT_OFFSET = 150.0f;
    const float maxHeightOffset = 270.0f; // Increased proportionally

    float zoomFactor = 0.0f;
    if (m_Radius < DEFAULT_RADIUS) // Only add extra offset when zooming in from default
    {
        zoomFactor = (DEFAULT_RADIUS - m_Radius) / (DEFAULT_RADIUS - MIN_RADIUS);
        zoomFactor = std::max(0.0f, std::min(1.0f, zoomFactor)); // Clamp to 0-1
    }

    // Calculate the target offset for current zoom level
    float targetHeightOffset =
        CAMERA_HEIGHT_OFFSET + ((maxHeightOffset - CAMERA_HEIGHT_OFFSET) * zoomFactor);
    float additionalOffset = targetHeightOffset - CAMERA_HEIGHT_OFFSET;

    // Check if requested pitch would hit ground or ceiling constraints
    float pitchRad = m_DeltaPitch * (M_PI / 180.0f);
    float requestedElevation = currentElevation + pitchRad;
    float requestedVerticalDist = totalDist * sinf(requestedElevation);
    float requestedZ = m_Target[2] + requestedVerticalDist + additionalOffset;

    const float minCameraHeight = 50.0f;
    float finalElevation = requestedElevation;
    float effectivePitchDelta = m_DeltaPitch;

    // Clamp to prevent looking too far down (below minimum height)
    if (requestedZ < m_Target[2] + minCameraHeight)
    {
        // Calculate maximum downward elevation that keeps us above minimum height
        float maxRelativeZ = minCameraHeight - additionalOffset;
        finalElevation = asinf(std::clamp(maxRelativeZ / totalDist, -1.0f, 1.0f));

        // Calculate effective pitch delta
        effectivePitchDelta = (finalElevation - currentElevation) * (180.0f / M_PI);
    }

    // Clamp to prevent looking straight down at character (limit to ~80-90 degrees)
    // When camera is directly above, elevation approaches 90 degrees (PI/2)
    // We want to stop before reaching that to keep character visible
    const float maxElevationRad = 90.0f * (M_PI / 180.0f); // ~90 degrees maximum
    if (finalElevation > maxElevationRad)
    {
        finalElevation = maxElevationRad;
        effectivePitchDelta = (finalElevation - currentElevation) * (180.0f / M_PI);
    }

    // Apply the final (possibly clamped) elevation
    float newHorizontalDist = totalDist * cosf(finalElevation);
    float newVerticalDist = totalDist * sinf(finalElevation);

    float xyScale = (horizontalDist > 0.001f) ? (newHorizontalDist / horizontalDist) : 1.0f;
    rotatedX *= xyScale;
    rotatedY *= xyScale;
    rotatedZ = newVerticalDist;

    // Set final camera position with additional height offset
    m_State.Position[0] = m_Target[0] + rotatedX;
    m_State.Position[1] = m_Target[1] + rotatedY;
    m_State.Position[2] = m_Target[2] + rotatedZ + additionalOffset;

    // Modify camera angles - use effective pitch that accounts for ground collision
    m_State.Angle[0] = m_State.Angle[0] + effectivePitchDelta;
    m_State.Angle[1] = 0.0f;
    m_State.Angle[2] = m_State.Angle[2] - m_DeltaYaw; // Inverted like CustomCamera3D

    // Store the effective pitch for next frame's constraint checking
    m_Input.LastEffectivePitch = effectivePitchDelta;

    // Update transformation matrix
    m_State.UpdateMatrix();

    // Update distance properties
    m_State.Distance = m_Radius;
    m_State.DistanceTarget = m_Radius;

    // Sync ViewFar and fog distances with current zoom level
    UpdateConfigForView();
    UpdateFrustum();
}

// ========== Phase 1: Configuration & Frustum Management ==========

void OrbitalCamera::SetConfig(const CameraConfig &config)
{
    m_Config = config;
    UpdateFrustum();
}

void OrbitalCamera::UpdateConfigForView()
{
    float zoomRatio = m_Radius / DEFAULT_RADIUS;
    CameraConfig baseConfig = CameraConfig::ForMainSceneOrbitalCamera();

    float zoomScale;
    if (zoomRatio >= 1.0f)
    {
        // Zooming OUT: scale up gently
        zoomScale = 1.0f + (zoomRatio - 1.0f) * 0.33f;
    }
    else
    {
        // Zooming IN: reduce proportionally
        zoomScale = 0.5f + (zoomRatio * 0.5f);
    }

    // Scale far plane with zoom only (not pitch).
    // The 2D frustum projection already adapts its shape to the actual camera pitch.
    m_Config.farPlane = baseConfig.farPlane * zoomScale;

    // Sync g_Camera.ViewFar so BeginOpengl uses the updated far plane for
    // projection and percentage-based fog (fog = ViewFar * 80%/90%).
    m_State.ViewFar = m_Config.farPlane;
    g_Camera.ViewFar = m_Config.farPlane;

    // Restore orbital's FOV. The internal m_pDefaultCamera->Update() call in
    // OrbitalCamera::Update() writes m_State.FOV from its own (default-cam,
    // hFov=40°) config; without this re-assert the orbital frame would
    // render at the narrower default-cam FOV from frame 2 onward.
    if (WindowHeight > 0)
    {
        float aspect = (float)WindowWidth / (float)WindowHeight;
        m_State.FOV = HFovToVFov(m_Config.hFov, aspect);
    }

    // Keep cull ranges in lockstep with farPlane. Previously objectCullRange was
    // set once at config load and never updated, so zooming the camera widened
    // the terrain hull but left object culling at the static initial value.
    m_Config.terrainCullRange = m_Config.farPlane * RENDER_DISTANCE_MULTIPLIER;
    m_Config.objectCullRange = m_Config.farPlane;
}

void OrbitalCamera::UpdateFrustum()
{
    // Derive forward and up vectors from m_State.Angle to match the OpenGL view setup.
    // BeginOpengl() applies: glRotatef(A1, Y) * glRotatef(A0, X) * glRotatef(A2, Z)
    // The GL rotation R = Ry(A1) * Rx(A0) * Rz(A2) transforms world→view.
    // Forward (view -Z) in world space = R^T * (0,0,-1), Up (view +Y) = R^T * (0,1,0).
    vec3_t forward, up;

    float a0 = m_State.Angle[0] * (Q_PI / 180.0f); // X rotation (pitch)
    float a1 = m_State.Angle[1] * (Q_PI / 180.0f); // Y rotation (usually 0)
    float a2 = m_State.Angle[2] * (Q_PI / 180.0f); // Z rotation (yaw)
    float sa0 = sinf(a0), ca0 = cosf(a0);
    float sa1 = sinf(a1), ca1 = cosf(a1);
    float sa2 = sinf(a2), ca2 = cosf(a2);

    // forward = -(third row of R) = -R[2][*]
    forward[0] = sa1 * ca2 - ca1 * sa0 * sa2;
    forward[1] = -(sa1 * sa2 + ca1 * sa0 * ca2);
    forward[2] = -(ca1 * ca0);

    // up = second row of R = R[1][*]
    up[0] = ca0 * sa2;
    up[1] = ca0 * ca2;
    up[2] = -sa0;

    // Build frustum using the same viewport aspect ratio that BeginOpengl() will use.
    // BeginOpengl() scales reference coords (640×480) to actual window pixels.
    // The transparent S16 HUD overlays the world instead of reserving a
    // bottom strip.
    int refWidth = REFERENCE_WIDTH;
    int refHeight = REFERENCE_HEIGHT;
    if (SceneFlag == MAIN_SCENE)
    {
        refWidth = GetScreenWidth();
    }
    float viewportWidth = (float)(refWidth * WindowWidth) / (float)REFERENCE_WIDTH;
    float viewportHeight = (float)(refHeight * WindowHeight) / (float)REFERENCE_HEIGHT;
    float aspectRatio = viewportWidth / viewportHeight;

    // Override was already applied at the top of this function (if enabled).
    float effectiveFarPlane = m_Config.farPlane;
    float effectiveTerrainCullRange = m_Config.terrainCullRange;

    // Use g_Camera.FOV (= m_State.FOV) directly — same vFov that BeginOpengl passes
    // to gluPerspective. Combined with viewport aspect, this matches the GL projection exactly.
    float vFov = m_State.FOV;

    m_Frustum.BuildFromCamera(m_State.Position, forward, up, vFov, aspectRatio, m_Config.nearPlane,
                              effectiveFarPlane, effectiveTerrainCullRange);

    // Inject Orbital's actual 2D terrain-cull hull into m_Frustum so FreeFly
    // spectator (which reads Frustum::Get2DX/Y) sees exactly what Orbital's
    // active-render path uses. Matches ZzzLodTerrain::CreateFrustrum2D logic:
    //   - override on  → user's trapezoid (farDist/farWidth/nearDist/nearWidth)
    //   - override off → natural pyramid + 400 half-width at near edge
    {
        float hullFarDist, hullFarHalfW, hullNearDist, hullNearHalfW;
        {
            const float tanHalfV = tanf(vFov * 0.5f * Q_PI / 180.0f);
            hullFarDist = effectiveTerrainCullRange;
            hullFarHalfW = tanHalfV * effectiveTerrainCullRange * aspectRatio;
            hullNearDist = 0.0f;
            hullNearHalfW = NATURAL_NEAR_HALF_WIDTH;
        }
        const float hullFarHalfH = hullFarHalfW / aspectRatio;
        const float hullNearHalfH = hullNearHalfW / aspectRatio;

        // Build the SAME rotation matrix BeginOpengl produces (Ry(A1) * Rx(A0) * Rz(A2))
        // so the transform matches g_Camera.Matrix exactly. Using AngleMatrix directly
        // would apply a different convention and the hull would counter-rotate in spectator.
        float mat[3][4];
        {
            const float a0 = m_State.Angle[0] * (Q_PI / 180.0f);
            const float a1 = m_State.Angle[1] * (Q_PI / 180.0f);
            const float a2 = m_State.Angle[2] * (Q_PI / 180.0f);
            const float s0 = sinf(a0), c0 = cosf(a0);
            const float s1 = sinf(a1), c1 = cosf(a1);
            const float s2 = sinf(a2), c2 = cosf(a2);
            mat[0][0] = c1 * c2 + s1 * s0 * s2;
            mat[0][1] = -c1 * s2 + s1 * s0 * c2;
            mat[0][2] = s1 * c0;
            mat[1][0] = c0 * s2;
            mat[1][1] = c0 * c2;
            mat[1][2] = -s0;
            mat[2][0] = -s1 * c2 + c1 * s0 * s2;
            mat[2][1] = s1 * s2 + c1 * s0 * c2;
            mat[2][2] = c1 * c0;
            mat[0][3] = mat[1][3] = mat[2][3] = 0.0f;
        }

        vec3_t viewPts[8];
        Vector(-hullNearHalfW, hullNearHalfH, -hullNearDist, viewPts[0]);
        Vector(hullNearHalfW, hullNearHalfH, -hullNearDist, viewPts[1]);
        Vector(hullNearHalfW, -hullNearHalfH, -hullNearDist, viewPts[2]);
        Vector(-hullNearHalfW, -hullNearHalfH, -hullNearDist, viewPts[3]);
        Vector(-hullFarHalfW, hullFarHalfH, -hullFarDist, viewPts[4]);
        Vector(hullFarHalfW, hullFarHalfH, -hullFarDist, viewPts[5]);
        Vector(hullFarHalfW, -hullFarHalfH, -hullFarDist, viewPts[6]);
        Vector(-hullFarHalfW, -hullFarHalfH, -hullFarDist, viewPts[7]);

        float hx[8], hy[8];
        for (int i = 0; i < 8; i++)
        {
            vec3_t world;
            VectorIRotate(viewPts[i], mat, world);
            VectorAdd(world, m_State.Position, world);
            hx[i] = world[0] * 0.01f;
            hy[i] = world[1] * 0.01f;
        }
        m_Frustum.SetCustom2DHull(hx, hy, 8);
    }
}

// 2D frustum convex hull vertices (in tile coordinates, i.e. world * 0.01).
// Source hull is at most 12 (8 camera frustum corners + 4 terrain-cull extension
// corners; legacy trapezoid uses 4). Sutherland-Hodgman clipping against a
// single plane can grow an N-vertex convex polygon to N+1 vertices, so the
// post-clip storage needs to be at least 13. Use 16 for headroom and to
// accommodate any future multi-plane clipping extension.
static constexpr int MAX_HULL_VERTICES = 16;

namespace
{
// Iteration bounds are snapped to a tile grid of this size so we iterate whole LOD tiles.
constexpr int TERRAIN_ITERATION_TILE = 4;

// Insertion-sort threshold for degenerate-edge detection during hull expansion.
constexpr float EDGE_LENGTH_EPSILON = 0.001f;

// Bisector scale clamp when interior angle approaches 180° (cosHalf → 0).
constexpr float BISECTOR_COS_MIN = 0.1f;

} // namespace

void SessionVisualUnit::ComputeIterationBoundsFromHull()
{
    if (FrustrumCount <= 0)
        return;

    float minX = FrustrumX[0], minY = FrustrumY[0];
    float maxX = FrustrumX[0], maxY = FrustrumY[0];
    for (int i = 1; i < FrustrumCount; i++)
    {
        if (FrustrumX[i] < minX)
            minX = FrustrumX[i];
        if (FrustrumY[i] < minY)
            minY = FrustrumY[i];
        if (FrustrumX[i] > maxX)
            maxX = FrustrumX[i];
        if (FrustrumY[i] > maxY)
            maxY = FrustrumY[i];
    }

    constexpr int T = TERRAIN_ITERATION_TILE;
    FrustrumBoundMinX = (int)(minX) / T * T - T;
    FrustrumBoundMinY = (int)(minY) / T * T - T;
    FrustrumBoundMaxX = (int)(maxX) / T * T + T;
    FrustrumBoundMaxY = (int)(maxY) / T * T + T;
    FrustrumBoundMinX = std::max(FrustrumBoundMinX, 0);
    FrustrumBoundMinY = std::max(FrustrumBoundMinY, 0);
    // Clamp so the final 4×4 block fits in [TERRAIN_SIZE_MASK]. With T=4 this is
    // TERRAIN_SIZE - T = 252; clamping to TERRAIN_SIZE_MASK - T (= 251) would
    // leave tiles 252–255 at the high edge of the map permanently un-iterated.
    FrustrumBoundMaxX = std::min(FrustrumBoundMaxX, TERRAIN_SIZE - T);
    FrustrumBoundMaxY = std::min(FrustrumBoundMaxY, TERRAIN_SIZE - T);
}

void SessionVisualUnit::BuildHull2DAndBounds(const float *ptsX, const float *ptsY, int numPts)
{
    const int n = std::min(numPts, MAX_HULL_VERTICES);

    Point2D sorted[MAX_HULL_VERTICES];
    for (int i = 0; i < n; i++)
    {
        sorted[i].x = ptsX[i];
        sorted[i].y = ptsY[i];
    }

    SortPoints2D(sorted, n);

    Point2D hull[MAX_HULL_VERTICES];
    int k = ConvexHullCCW(sorted, n, hull, MAX_HULL_VERTICES);

    // Reverse to CW (TestFrustrum2D expects CW winding)
    FrustrumCount = k;
    for (int i = 0; i < k; i++)
    {
        FrustrumX[i] = hull[k - 1 - i].x;
        FrustrumY[i] = hull[k - 1 - i].y;
    }

    ComputeIterationBoundsFromHull();
}

void SessionVisualUnit::ExpandHullOutward(float offset)
{
    if (FrustrumCount < 3)
        return;

    float newX[MAX_HULL_VERTICES], newY[MAX_HULL_VERTICES];

    for (int i = 0; i < FrustrumCount; i++)
    {
        int prev = (i + FrustrumCount - 1) % FrustrumCount;
        int next = (i + 1) % FrustrumCount;

        // Edge directions
        float e1x = FrustrumX[i] - FrustrumX[prev];
        float e1y = FrustrumY[i] - FrustrumY[prev];
        float len1 = sqrtf(e1x * e1x + e1y * e1y);

        float e2x = FrustrumX[next] - FrustrumX[i];
        float e2y = FrustrumY[next] - FrustrumY[i];
        float len2 = sqrtf(e2x * e2x + e2y * e2y);

        if (len1 < EDGE_LENGTH_EPSILON || len2 < EDGE_LENGTH_EPSILON)
        {
            newX[i] = FrustrumX[i];
            newY[i] = FrustrumY[i];
            continue;
        }

        // Outward normals for CW winding: (-ey, ex) / len
        float n1x = -e1y / len1, n1y = e1x / len1;
        float n2x = -e2y / len2, n2y = e2x / len2;

        // Bisector direction
        float bx = n1x + n2x;
        float by = n1y + n2y;
        float blen = sqrtf(bx * bx + by * by);

        if (blen > EDGE_LENGTH_EPSILON)
        {
            bx /= blen;
            by /= blen;
            // Scale by 1/cos(halfAngle) to maintain perpendicular offset distance.
            // Clamp cosHalf at BISECTOR_COS_MIN so the expansion is continuous at
            // very flat corners (cap at offset / BISECTOR_COS_MIN) rather than
            // jumping to a fixed multiplier.
            float cosHalf = n1x * bx + n1y * by;
            float scale = offset / std::max(cosHalf, BISECTOR_COS_MIN);
            newX[i] = FrustrumX[i] + bx * scale;
            newY[i] = FrustrumY[i] + by * scale;
        }
        else
        {
            newX[i] = FrustrumX[i] + n1x * offset;
            newY[i] = FrustrumY[i] + n1y * offset;
        }
    }

    for (int i = 0; i < FrustrumCount; i++)
    {
        FrustrumX[i] = newX[i];
        FrustrumY[i] = newY[i];
    }

    ComputeIterationBoundsFromHull();
}

void SessionVisualUnit::CreateFrustrum2D(vec_t *Position)
{
    CameraMode currentMode = cameraManager_.GetCurrentMode();
    // Default joins the Orbital natural-hull path so the cull hull is built
    // from the actual GL modelview / FOV / aspect instead of the legacy
    // hardcoded trapezoid table (which was tuned for 4:3 and left the upper
    // screen corners uncovered on widescreen). The DevEditor near/far
    // trapezoid multipliers still apply, just on view-space halfW/nearHalfW.
    if (currentMode == CameraMode::Default || currentMode == CameraMode::Orbital)
    {
        // Orbital mode: build 2D hull from the ACTUAL GL modelview matrix,
        // then clip against the far-clip plane at ground level (Z=0).
        // The hull extends to fogEnd distance (= terrainCullRange) so terrain is only
        // rendered where it's at least partially visible through fog. The far-clip ground
        // line clipping is a safety net for steep camera angles where the ground at the
        // hull boundary could be deeper than the GL far clip.
        {
            // Use fog end distance as terrain cull extent (don't render fully-fogged terrain)
            const ICamera *cam = cameraManager_.GetActiveCamera();
            float terrainDist = g_Camera.ViewFar * RENDER_DISTANCE_MULTIPLIER; // fallback
            if (cam)
                terrainDist = cam->GetConfig().terrainCullRange;

            // Same vFov and viewport aspect that gluPerspective uses in BeginOpengl
            float vFovHalfRad = g_Camera.FOV * 0.5f * Q_PI / 180.0f;
            float tanHalf = tanf(vFovHalfRad);

            // Viewport aspect ratio (matching BeginOpengl's calculation)
            int refWidth = GetScreenWidth();
            int refHeight = REFERENCE_HEIGHT;
            float vpW = (float)(refWidth * WindowWidth) / (float)REFERENCE_WIDTH;
            float vpH = (float)(refHeight * WindowHeight) / (float)REFERENCE_HEIGHT;
            float aspect = vpW / vpH;

            float halfH = tanHalf * terrainDist;
            float halfW = halfH * aspect;

            // Natural Orbital hull: 8 view-space corners (near quad + far quad).
            // Near quad is centered on the camera (view-Z=0) with a non-zero half-width.
            // An apex pyramid (near half-width = 0) leaves a gap at the camera footprint
            // where tile-center-in-polygon tests miss the tiles directly under the camera,
            // making static 3D objects anchored to those tiles pop in/out of view.
            // 400 world units (= 800 total width) covers the footprint reliably.
            constexpr float NATURAL_NEAR_HALF_WIDTH = 400.0f;
            float nearHalfW = NATURAL_NEAR_HALF_WIDTH;

            const float nearHalfH = nearHalfW / aspect;

            vec3_t viewPts[8];
            Vector(-nearHalfW, nearHalfH, 0.0f, viewPts[0]);
            Vector(nearHalfW, nearHalfH, 0.0f, viewPts[1]);
            Vector(nearHalfW, -nearHalfH, 0.0f, viewPts[2]);
            Vector(-nearHalfW, -nearHalfH, 0.0f, viewPts[3]);
            Vector(-halfW, halfH, -terrainDist, viewPts[4]);
            Vector(halfW, halfH, -terrainDist, viewPts[5]);
            Vector(halfW, -halfH, -terrainDist, viewPts[6]);
            Vector(-halfW, -halfH, -terrainDist, viewPts[7]);

            // Transform to world space using the actual GL modelview matrix
            // (g_Camera.Matrix was set by CameraProjection::GetOpenGLMatrix in BeginOpengl)
            float px[8], py[8];
            for (int i = 0; i < 8; i++)
            {
                vec3_t world;
                VectorIRotate(viewPts[i], g_Camera.Matrix, world);
                VectorAdd(world, g_Camera.Position, world);
                px[i] = world[0] * 0.01f;
                py[i] = world[1] * 0.01f;
            }

            BuildHull2DAndBounds(px, py, 8);

            // --- Clip hull against the far-clip plane at ground level (Z=0) ---
            // The GL modelview matrix maps world→view. For a point at ground (z=0):
            //   viewZ = Matrix[2][0]*x + Matrix[2][1]*y + Matrix[2][3]
            // The point is within the far clip if viewZ >= -terrainDist.
            // In tile coords (world = tile * 100):
            //   A*xt + B*yt + D >= 0  means "visible"
            float A = g_Camera.Matrix[2][0] * 100.0f;
            float B = g_Camera.Matrix[2][1] * 100.0f;
            float D = g_Camera.Matrix[2][3] + terrainDist;

            // Sutherland-Hodgman: clip polygon against half-plane A*x + B*y + D >= 0.
            // Single-plane clip can at most double the vertex count temporarily.
            float clipX[2 * MAX_HULL_VERTICES], clipY[2 * MAX_HULL_VERTICES];
            int clipCount = 0;

            for (int i = 0; i < FrustrumCount; i++)
            {
                int j = (i + 1) % FrustrumCount;
                float di = A * FrustrumX[i] + B * FrustrumY[i] + D;
                float dj = A * FrustrumX[j] + B * FrustrumY[j] + D;

                if (di >= 0.f)
                {
                    clipX[clipCount] = FrustrumX[i];
                    clipY[clipCount] = FrustrumY[i];
                    clipCount++;
                }

                // If edge crosses the line, add intersection
                if ((di >= 0.f) != (dj >= 0.f))
                {
                    float t = di / (di - dj);
                    clipX[clipCount] = FrustrumX[i] + t * (FrustrumX[j] - FrustrumX[i]);
                    clipY[clipCount] = FrustrumY[i] + t * (FrustrumY[j] - FrustrumY[i]);
                    clipCount++;
                }
            }

            if (clipCount >= 3 && clipCount <= MAX_HULL_VERTICES)
            {
                FrustrumCount = clipCount;
                for (int i = 0; i < clipCount; i++)
                {
                    FrustrumX[i] = clipX[i];
                    FrustrumY[i] = clipY[i];
                }

                // Recompute bounds from clipped hull
                float minX = FrustrumX[0], minY = FrustrumY[0];
                float maxX = FrustrumX[0], maxY = FrustrumY[0];
                for (int i = 1; i < FrustrumCount; i++)
                {
                    if (FrustrumX[i] < minX)
                        minX = FrustrumX[i];
                    if (FrustrumY[i] < minY)
                        minY = FrustrumY[i];
                    if (FrustrumX[i] > maxX)
                        maxX = FrustrumX[i];
                    if (FrustrumY[i] > maxY)
                        maxY = FrustrumY[i];
                }
                int tileWidth = 4;
                FrustrumBoundMinX = (int)(minX) / tileWidth * tileWidth - tileWidth;
                FrustrumBoundMinY = (int)(minY) / tileWidth * tileWidth - tileWidth;
                FrustrumBoundMaxX = (int)(maxX) / tileWidth * tileWidth + tileWidth;
                FrustrumBoundMaxY = (int)(maxY) / tileWidth * tileWidth + tileWidth;
                FrustrumBoundMinX = std::max(FrustrumBoundMinX, 0);
                FrustrumBoundMinY = std::max(FrustrumBoundMinY, 0);
                FrustrumBoundMaxX = std::min(FrustrumBoundMaxX, TERRAIN_SIZE_MASK - tileWidth);
                FrustrumBoundMaxY = std::min(FrustrumBoundMaxY, TERRAIN_SIZE_MASK - tileWidth);
            }
            // else: keep the unclipped hull (camera looking up or edge case)

            ExpandHullOutward(1.0f);
            return;
        }
    }
}

void SessionVisualUnit::CreateFrustrum(float xAspect, float yAspect, vec_t *position)
{
    const auto fovv = tanf(g_Camera.FOV * Q_PI / 360.f);
    float Distance = g_Camera.ViewFar;
    float Width = fovv * Distance * xAspect + 100.f;
    float Height = fovv * Distance * yAspect + 100.f;

    vec3_t Temp[5];
    Vector(0.f, 0.f, 0.f, Temp[0]);
    Vector(-Width, Height, -Distance, Temp[1]);
    Vector(Width, Height, -Distance, Temp[2]);
    Vector(Width, -Height, -Distance, Temp[3]);
    Vector(-Width, -Height, -Distance, Temp[4]);

    float FrustrumMinX = (float)TERRAIN_SIZE * TERRAIN_SCALE;
    float FrustrumMinY = (float)TERRAIN_SIZE * TERRAIN_SCALE;
    float FrustrumMaxX = 0.f;
    float FrustrumMaxY = 0.f;

    for (int i = 0; i < 5; i++)
    {
        vec3_t t;
        VectorIRotate(Temp[i], g_Camera.Matrix, t);
        VectorAdd(t, g_Camera.Position, FrustrumVertex[i]);
        if (FrustrumMinX > FrustrumVertex[i][0])
            FrustrumMinX = FrustrumVertex[i][0];
        if (FrustrumMinY > FrustrumVertex[i][1])
            FrustrumMinY = FrustrumVertex[i][1];
        if (FrustrumMaxX < FrustrumVertex[i][0])
            FrustrumMaxX = FrustrumVertex[i][0];
        if (FrustrumMaxY < FrustrumVertex[i][1])
            FrustrumMaxY = FrustrumVertex[i][1];
    }

    int tileWidth = 4;
    FrustrumBoundMinX = (int)(FrustrumMinX / TERRAIN_SCALE) / tileWidth * tileWidth - tileWidth;
    FrustrumBoundMinY = (int)(FrustrumMinY / TERRAIN_SCALE) / tileWidth * tileWidth - tileWidth;
    FrustrumBoundMaxX = (int)(FrustrumMaxX / TERRAIN_SCALE) / tileWidth * tileWidth + tileWidth;
    FrustrumBoundMaxY = (int)(FrustrumMaxY / TERRAIN_SCALE) / tileWidth * tileWidth + tileWidth;
    FrustrumBoundMinX = FrustrumBoundMinX < 0 ? 0 : FrustrumBoundMinX;
    FrustrumBoundMinY = FrustrumBoundMinY < 0 ? 0 : FrustrumBoundMinY;
    FrustrumBoundMaxX = FrustrumBoundMaxX > TERRAIN_SIZE_MASK - tileWidth
                            ? TERRAIN_SIZE_MASK - tileWidth
                            : FrustrumBoundMaxX;
    FrustrumBoundMaxY = FrustrumBoundMaxY > TERRAIN_SIZE_MASK - tileWidth
                            ? TERRAIN_SIZE_MASK - tileWidth
                            : FrustrumBoundMaxY;

    FaceNormalize(FrustrumVertex[0], FrustrumVertex[1], FrustrumVertex[2], FrustrumFaceNormal[0]);
    FaceNormalize(FrustrumVertex[0], FrustrumVertex[2], FrustrumVertex[3], FrustrumFaceNormal[1]);
    FaceNormalize(FrustrumVertex[0], FrustrumVertex[3], FrustrumVertex[4], FrustrumFaceNormal[2]);
    FaceNormalize(FrustrumVertex[0], FrustrumVertex[4], FrustrumVertex[1], FrustrumFaceNormal[3]);
    FaceNormalize(FrustrumVertex[3], FrustrumVertex[2], FrustrumVertex[1], FrustrumFaceNormal[4]);
    FrustrumFaceD[0] = -DotProduct(FrustrumVertex[0], FrustrumFaceNormal[0]);
    FrustrumFaceD[1] = -DotProduct(FrustrumVertex[0], FrustrumFaceNormal[1]);
    FrustrumFaceD[2] = -DotProduct(FrustrumVertex[0], FrustrumFaceNormal[2]);
    FrustrumFaceD[3] = -DotProduct(FrustrumVertex[0], FrustrumFaceNormal[3]);
    FrustrumFaceD[4] = -DotProduct(FrustrumVertex[1], FrustrumFaceNormal[4]);

    CreateFrustrum2D(position);
}

void SessionVisualUnit::ResetFrustrumBoundsFullTerrain()
{
    FrustrumBoundMinX = 0;
    FrustrumBoundMinY = 0;
    FrustrumBoundMaxX = TERRAIN_SIZE_MASK - TERRAIN_ITERATION_TILE;
    FrustrumBoundMaxY = TERRAIN_SIZE_MASK - TERRAIN_ITERATION_TILE;
}

void SessionVisualUnit::CacheActiveFrustum()
{
    g_Camera.UpdateMatrix();
    CreateFrustrum(static_cast<float>(GetScreenWidth()) / REFERENCE_WIDTH, 1.f, g_Camera.Position);
}

bool SessionVisualUnit::TestFrustrum2D(float x, float y, float Range)
{
    if (SceneFlag == SERVER_LIST_SCENE || SceneFlag == WEBZEN_SCENE || SceneFlag == LOADING_SCENE)
        return true;

    // Fast path: unrolled 4-edge test for Legacy/Default cameras
    if (FrustrumCount == 4)
    {
        float d;
        d = (FrustrumX[0] - x) * (FrustrumY[3] - y) - (FrustrumX[3] - x) * (FrustrumY[0] - y);
        if (d <= Range)
            return false;
        d = (FrustrumX[1] - x) * (FrustrumY[0] - y) - (FrustrumX[0] - x) * (FrustrumY[1] - y);
        if (d <= Range)
            return false;
        d = (FrustrumX[2] - x) * (FrustrumY[1] - y) - (FrustrumX[1] - x) * (FrustrumY[2] - y);
        if (d <= Range)
            return false;
        d = (FrustrumX[3] - x) * (FrustrumY[2] - y) - (FrustrumX[2] - x) * (FrustrumY[3] - y);
        if (d <= Range)
            return false;
        return true;
    }

    // General path for N-vertex hulls (Orbital camera)
    int j = FrustrumCount - 1;
    for (int i = 0; i < FrustrumCount; j = i, i++)
    {
        float d = (FrustrumX[i] - x) * (FrustrumY[j] - y) - (FrustrumX[j] - x) * (FrustrumY[i] - y);
        if (d <= Range)
        {
            return false;
        }
    }
    return true;
}

bool SessionVisualUnit::TestFrustrum(const vec3_t Position, float Range)
{
    if (SceneFlag == LOG_IN_SCENE)
        return true;

    for (int i = 0; i < 5; i++)
    {
        if (DotProduct(Position, FrustrumFaceNormal[i]) + FrustrumFaceD[i] < -Range)
        {
            return false;
        }
    }
    return true;
}

CameraProjection::CameraProjection(SessionKeeper &keeper) noexcept
    : keeper_(keeper), OpenglWindowX(keeper.PlatformOpenglWindowX()),
      OpenglWindowY(keeper.PlatformOpenglWindowY()),
      OpenglWindowWidth(keeper.PlatformOpenglWindowWidth()),
      OpenglWindowHeight(keeper.PlatformOpenglWindowHeight()),
      WindowWidth(keeper.PlatformWindowWidth()), WindowHeight(keeper.PlatformWindowHeight()),
      MousePosition(keeper.InterfaceStorage().MousePosition)
{
}

LegacyRenderFacade *CameraProjection::RecordingFacade() const noexcept
{
    SessionRenderUnit *const renderer = keeper_.Renderer();
    if (renderer == nullptr || !renderer->LegacyRender().IsRecording())
    {
        return nullptr;
    }
    return &renderer->LegacyRender();
}

void CameraProjection::SetupPerspective(CameraState &state, float fov, float aspect, float zNear,
                                        float zFar)
{
    if (LegacyRenderFacade *const facade = RecordingFacade())
    {
        (void)facade->MatrixMode(LegacyMatrixMode::Projection);
        (void)facade->Perspective(fov, aspect, zNear, zFar);
    }

    // Use actual viewport dimensions (set by SetViewport) for screen center and
    // perspective. This accounts for the game viewport being narrower/shorter than
    // the full window when UI panels (inventory, NPC shop, etc.) are open.
    int vpWidth = viewportWidth_ > 0 ? viewportWidth_ : OpenglWindowWidth;
    int vpHeight = viewportHeight_ > 0 ? viewportHeight_ : OpenglWindowHeight;

    state.ScreenCenterX = OpenglWindowX + vpWidth / 2;
    state.ScreenCenterY = WindowHeight - OpenglWindowY - vpHeight / 2;
    state.ScreenCenterYFlip = WindowHeight - state.ScreenCenterY;

    float fovRad = fov * 0.5f * Q_PI / 180.0f;
    state.PerspectiveX = tanf(fovRad) / (float)(vpWidth / 2) * aspect;
    state.PerspectiveY = tanf(fovRad) / (float)(vpHeight / 2);
}

void CameraProjection::PrepareInteractionProjection(CameraState &state, int width, int height)
{
    state.UpdateMatrix();
    state.ScreenCenterX = width / 2;
    state.ScreenCenterY = height / 2;
    state.ScreenCenterYFlip = WindowHeight - state.ScreenCenterY;
    const float tangent = tanf(state.FOV * 0.5f * Q_PI / 180.f);
    state.PerspectiveX = tangent / static_cast<float>(width / 2) * width / height;
    state.PerspectiveY = tangent / static_cast<float>(height / 2);
}

void CameraProjection::SetViewport(int x, int y, int width, int height)
{
    OpenglWindowX = x;
    OpenglWindowY = y;
    viewportWidth_ = width;
    viewportHeight_ = height;

    if (LegacyRenderFacade *const facade = RecordingFacade())
    {
        (void)facade->SetViewport(
            {x, y, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)});
    }
}

void CameraProjection::ScreenToWorldRay(const CameraState &state, int sx, int sy, vec3_t outTarget,
                                        bool bFixView)
{
    // Convert reference coordinates to actual pixels
    sx = sx * WindowWidth / REFERENCE_WIDTH;
    sy = sy * WindowHeight / REFERENCE_HEIGHT;

    vec3_t p1, p2;

    float farDist = bFixView ? state.ViewFar : RENDER_ITEMVIEW_FAR;

    p1[0] = (float)(sx - state.ScreenCenterX) * farDist * state.PerspectiveX;
    p1[1] = -(float)(sy - state.ScreenCenterY) * farDist * state.PerspectiveY;
    p1[2] = -farDist;

    p2[0] = -state.Matrix[0][3];
    p2[1] = -state.Matrix[1][3];
    p2[2] = -state.Matrix[2][3];
    VectorIRotate(p2, state.Matrix, MousePosition);
    VectorIRotate(p1, state.Matrix, p2);
    VectorAdd(MousePosition, p2, outTarget);
}

void CameraProjection::WorldToScreen(const CameraState &state, const vec3_t worldPos, int *outX,
                                     int *outY)
{
    vec3_t transformPos;
    VectorTransform(worldPos, state.Matrix, transformPos);

    // Project to screen
    *outX = -(int)(transformPos[0] / state.PerspectiveX / transformPos[2]) + state.ScreenCenterX;
    *outY = (int)(transformPos[1] / state.PerspectiveY / transformPos[2]) + state.ScreenCenterY;

    // Convert to 640×480 reference coordinates
    *outX = *outX * REFERENCE_WIDTH / (int)WindowWidth;
    *outY = *outY * REFERENCE_HEIGHT / (int)WindowHeight;
}

void CameraProjection::TransformPosition(const CameraState &state, const vec3_t position,
                                         vec3_t outWorldPosition, int *outX, int *outY)
{
    vec3_t temp;
    VectorSubtract(position, state.Position, temp);
    VectorRotate(temp, state.Matrix, outWorldPosition);

    // Project to screen (pixel coordinates)
    *outX = (int)(outWorldPosition[0] / state.PerspectiveX / -outWorldPosition[2]) +
            state.ScreenCenterX;
    *outY = (int)(outWorldPosition[1] / state.PerspectiveY / -outWorldPosition[2]) +
            state.ScreenCenterYFlip;
}

void CameraProjection::GetModelViewMatrix(float outMatrix[3][4])
{
    std::array<float, 16> openglMatrix = RenderTapeIdentityMatrix4x4;
    if (LegacyRenderFacade *const facade = RecordingFacade())
    {
        openglMatrix = facade->CurrentMatrix(LegacyMatrixMode::ModelView);
    }

    // Convert from OpenGL 4×4 to our 3×4 format
    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            outMatrix[i][j] = openglMatrix[j * 4 + i];
        }
    }
}

int SessionGameplayUnit::GetLoginCameraCount()
{
    return g_loginCamera.currentCount;
}

int SessionGameplayUnit::GetLoginCameraWalkCut()
{
    return g_loginCamera.walkCut;
}

void SessionGameplayUnit::MoveCharacterCamera(vec_t *Origin, vec_t *Position, vec_t *Angle)
{
    vec3_t TransformPosition;
    g_Camera.Angle[0] = 0.f;
    g_Camera.Angle[1] = 0.f;
    g_Camera.Angle[2] = Angle[2];
    float Matrix[3][4];
    AngleMatrix(g_Camera.Angle, Matrix);
    VectorIRotate(Position, Matrix, TransformPosition);
    VectorAdd(Origin, TransformPosition, g_Camera.Position);
    g_Camera.Angle[0] = Angle[0];
}

/**
 * @brief Initializes login camera to starting position and angle.
 */
void SessionGameplayUnit::InitializeLoginCamera()
{
    for (int i = 0; i < 3; i++)
    {
        g_loginCamera.currentPosition[i] = LoginCameraState::WALK_PATHS[0][i];
        g_loginCamera.currentAngle[i] = LoginCameraState::WALK_PATHS[0][i + 3];
    }
    g_loginCamera.currentNumber = 1;
    g_loginCamera.currentWalkType = 1;

    for (int i = 0; i < 3; i++)
    {
        g_loginCamera.currentWalkDelta[i] =
            (LoginCameraState::WALK_PATHS[g_loginCamera.currentNumber][i] -
             g_loginCamera.currentPosition[i]) /
            128;
        g_loginCamera.currentWalkDelta[i + 3] =
            (LoginCameraState::WALK_PATHS[g_loginCamera.currentNumber][i + 3] -
             g_loginCamera.currentAngle[i]) /
            128;
    }
}

/**
 * @brief Calculates movement delta for transitioning to target waypoint.
 */
void SessionGameplayUnit::CalculateWalkDelta()
{
    for (int i = 0; i < 3; i++)
    {
        g_loginCamera.currentWalkDelta[i] =
            (LoginCameraState::WALK_PATHS[g_loginCamera.currentNumber][i] -
             g_loginCamera.currentPosition[i]) /
            128;
        g_loginCamera.currentWalkDelta[i + 3] =
            (LoginCameraState::WALK_PATHS[g_loginCamera.currentNumber][i + 3] -
             g_loginCamera.currentAngle[i]) /
            128;
    }
}

/**
 * @brief Selects next camera waypoint based on scene state.
 */
void SessionGameplayUnit::SelectNextWaypoint()
{
    if (SceneFlag == LOG_IN_SCENE)
    {
        g_loginCamera.currentNumber = rand() % 4 + 1;
        g_loginCamera.currentWalkType = rand() % 2;
    }
    else
    {
        g_loginCamera.currentNumber = 5;
        g_loginCamera.currentWalkType = 0;
    }
}

/**
 * @brief Updates camera waypoint transition timing and selection.
 */
void SessionGameplayUnit::UpdateCameraWaypoint()
{
    g_loginCamera.currentCount++;

    bool shouldTransition = (g_loginCamera.walkCut == 0 && g_loginCamera.currentCount >= 40) ||
                            (g_loginCamera.walkCut > 0 && g_loginCamera.currentCount >= 128);

    if (shouldTransition)
    {
        g_loginCamera.currentCount = 0;

        if (g_loginCamera.walkCut == 0)
        {
            g_loginCamera.walkCut = 1;
        }
        else
        {
            SelectNextWaypoint();
        }

        CalculateWalkDelta();
    }
}

/**
 * @brief Interpolates camera position/angle towards the current target waypoint.
 *
 * Two walk modes, selected per waypoint via WALK_PATHS[...][6]:
 *  - Type 0 (smooth): exponential ease toward target (divide by SMOOTH_EASING_FACTOR each frame).
 *  - Type 1 (linear): step by a precomputed per-frame delta (see CalculateWalkDelta).
 *
 * Both modes update all 3 position components AND all 3 angle components so camera
 * rotation tracks the waypoint pose, not just X/Y translation.
 */
void SessionGameplayUnit::InterpolateCameraMovement()
{
    constexpr int SMOOTH_EASING_FACTOR = 6;
    const int target = g_loginCamera.currentNumber;

    if (g_loginCamera.currentWalkType == 0)
    {
        for (int i = 0; i < 3; i++)
        {
            g_loginCamera.currentPosition[i] +=
                (LoginCameraState::WALK_PATHS[target][i] - g_loginCamera.currentPosition[i]) /
                SMOOTH_EASING_FACTOR;
            g_loginCamera.currentAngle[i] +=
                (LoginCameraState::WALK_PATHS[target][i + 3] - g_loginCamera.currentAngle[i]) /
                SMOOTH_EASING_FACTOR;
        }
    }
    else
    {
        for (int i = 0; i < 3; i++)
        {
            g_loginCamera.currentPosition[i] += g_loginCamera.currentWalkDelta[i];
            g_loginCamera.currentAngle[i] += g_loginCamera.currentWalkDelta[i + 3];
        }
    }
}

/**
 * @brief Updates login scene camera animation along predefined waypoint path.
 */
void SessionGameplayUnit::MoveCamera()
{
    if (cameraMove_.IsTourMode())
    {
        return;
    }

    if (g_loginCamera.currentCount == -1)
    {
        InitializeLoginCamera();
    }

    UpdateCameraWaypoint();
    InterpolateCameraMovement();

    g_Camera.FOV = 45.f;
    vec3_t Position;
    Vector(0.f, 0.f, 0.f, Position);
    MoveCharacterCamera(Position, g_loginCamera.currentPosition, g_loginCamera.currentAngle);
}

void SessionLegacyCalls::MoveCharacterCamera(vec_t *origin, vec_t *position, vec_t *angle)
{
    sessionKeeper_.Gameplay()->MoveCharacterCamera(origin, position, angle);
}
int SessionLegacyCalls::GetLoginCameraCount()
{
    return sessionKeeper_.Gameplay()->GetLoginCameraCount();
} // OMF-01772
int SessionLegacyCalls::GetLoginCameraWalkCut()
{
    return sessionKeeper_.Gameplay()->GetLoginCameraWalkCut();
} // OMF-01773
void SessionLegacyCalls::InitializeLoginCamera()
{
    sessionKeeper_.Gameplay()->InitializeLoginCamera();
} // OMF-01776
void SessionLegacyCalls::CalculateWalkDelta()
{
    sessionKeeper_.Gameplay()->CalculateWalkDelta();
} // OMF-01777
void SessionLegacyCalls::SelectNextWaypoint()
{
    sessionKeeper_.Gameplay()->SelectNextWaypoint();
} // OMF-01778
void SessionLegacyCalls::UpdateCameraWaypoint()
{
    sessionKeeper_.Gameplay()->UpdateCameraWaypoint();
} // OMF-01779
void SessionLegacyCalls::InterpolateCameraMovement()
{
    sessionKeeper_.Gameplay()->InterpolateCameraMovement();
} // OMF-01780
void SessionLegacyCalls::MoveCamera()
{
    sessionKeeper_.Gameplay()->MoveCamera();
}
