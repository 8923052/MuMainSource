#include "domain/WorldPhysics.h"
#include "support/CoreMath.h"
#include "render/ModelGeometry.h"
#include "app/ApplicationLoopFrame.h"
#include "session/SessionRender.h"
#include "domain/WorldSimulation.h"
#include "session/SessionGameplay.h"
#include "render/Textures.h"
#include "domain/CharacterSystem.h"
#include "domain/EffectsUpdate.h"
#include "domain/MapSimulation.h"
#include "session/SessionKeeper.h"
#include "session/SessionPresentation.h"
#include "data/WorldData.h"
#include "render/ModelResources.h"
#include "support/Camera.h"
#include "data/Localization.h"
#include "domain/ItemsSkills.h"
#include "domain/CharacterPresentation.h"
#include "render/World.h"
#include "render/Terrain.h"
#include "domain/MovementAI.h"
#include "data/ItemData.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "render/Text.h"
#include "support/Scenes.h"
#include "ui/runtime/UiControls.h"
#include "data/GameData.h"
#include "app/ApplicationAudio.h"
#include "domain/Events.h"
#include "ui/session/UiSessionLogic.h"

#define RENDER_CLOTH
#define ADD_COLLISION

#define RATE_SHORT_SHOULDER (0.6f)

CPhysicsVertex::CPhysicsVertex()
{
    Clear();
}

CPhysicsVertex::~CPhysicsVertex()
{
}

void CPhysicsVertex::Clear(void)
{
    m_iCountOneTimeMove = 0;
    for (int i = 0; i < 3; ++i)
    {
        m_vForce[i] = m_vVel[i] = m_vPos[i] = m_vPreviousPos[i] = m_vOneTimeMove[i] = 0.0f;
    }
    m_byState = PVS_NORMAL;
}

void CPhysicsVertex::Init(float fXPos, float fYPos, float fZPos, BOOL bFixed)
{
    m_vPos[0] = fXPos;
    m_vPos[1] = fYPos;
    m_vPos[2] = fZPos;
    VectorCopy(m_vPos, m_vPreviousPos);
    if (bFixed)
    {
        m_byState |= PVS_FIXEDPOS;
    }
}

void CPhysicsVertex::UpdateForce(unsigned int iKey, double WorldTime, const vec3_t &wind,
                                 DWORD dwType, float fWind)
{
    if (PVS_FIXEDPOS & m_byState)
    {
        m_vForce[0] = m_vForce[1] = m_vForce[2] = 0.0f;
        return;
    }

    float fGravityRate = 1.0f;

    int iTemp = std::min<int>(std::max<int>(0, 5 - iKey), 4);
    auto fRand = (float)(iTemp == 0 ? 0 : iTemp + 2);
#ifndef DISABLE_WIND
    for (int i = 0; i < 3; ++i)
    {
        m_vForce[i] = fRand * wind[i] - m_vVel[i] * 0.01f;
    }
#else
    for (int i = 0; i < 3; ++i)
    {
        m_vForce[i] = -m_vVel[i] * 0.01f;
    }
#endif

    switch (PCT_MASK_ELASTIC & dwType) // m_dwType
    {
    case PCT_RUBBER:
        m_vForce[2] += fRand * (fWind + 0.1f) * 1.f;
        break;
    case PCT_RUBBER2:
        m_vForce[2] += fRand * (fWind);
        break;
    }

    switch (PCT_MASK_ELASTIC_EXT & dwType)
    {
    case PCT_ELASTIC_HALLOWEEN:
        m_vForce[0] += -(fRand * fWind * 0.5f);
        m_vForce[2] += fRand * (fWind + 0.1f) * 0.5f * (float)sinf(WorldTime * 0.003f) * 5.0f;
        m_vForce[2] -= s_Gravity * fGravityRate * s_fMass * 50.0f;
        break;
    case PCT_ELASTIC_RAGE_L:
        m_vForce[0] += -(fRand * fWind * 0.8f);
        break;
    case PCT_ELASTIC_RAGE_R:
        m_vForce[0] -= -(fRand * fWind * 0.8f);
        break;
    }

    switch (PCT_MASK_WEIGHT & dwType) // m_dwType
    {
    case PCT_HEAVY:
        m_vForce[2] -= s_Gravity * fGravityRate * s_fMass * 180.0f;
        break;
    default:
        m_vForce[2] -= s_Gravity * fGravityRate * s_fMass * 100.0f;
        break;
    }
}

void CPhysicsVertex::AddToForce(float fXForce, float fYForce, float fZForce)
{
    m_vForce[0] += fXForce;
    m_vForce[1] += fYForce;
    m_vForce[2] += fZForce;
}

void CPhysicsVertex::Move(float fTime)
{
    if (PVS_FIXEDPOS & m_byState)
    {
        return;
    }

    for (int i = 0; i < 3; ++i)
    {
        m_vVel[i] += m_vForce[i] * s_fInvOfMass * fTime;
        m_vPos[i] += m_vVel[i] * fTime;
    }
}

void CPhysicsVertex::BeginStep()
{
    VectorCopy(m_vPos, m_vPreviousPos);
}

void CPhysicsVertex::GetInterpolatedPosition(float fraction, vec3_t *position) const
{
    for (int axis = 0; axis < 3; ++axis)
        (*position)[axis] =
            (m_byState & PVS_FIXEDPOS)
                ? m_vPos[axis]
                : m_vPreviousPos[axis] + (m_vPos[axis] - m_vPreviousPos[axis]) * fraction;
}

void CPhysicsVertex::GetPosition(vec3_t *pPos) const
{
    for (int i = 0; i < 3; ++i)
    {
        (*pPos)[i] = m_vPos[i];
    }
}

float CPhysicsVertex::GetDistance(CPhysicsVertex *pVertex2, vec3_t *pDistance)
{
    for (int i = 0; i < 3; ++i)
    {
        (*pDistance)[i] = m_vPos[i] - pVertex2->m_vPos[i];
    }

    return (VectorLength((*pDistance)));
}

BOOL CPhysicsVertex::KeepLength(CPhysicsVertex *pVertex2, float *pfLength)
{
    if (PVS_FIXEDPOS & m_byState)
    {
        return (TRUE);
    }

    vec3_t vDistance;
    float fDistance = std::max<float>(0.001f, GetDistance(pVertex2, &vDistance));

    if (fDistance > pfLength[1] * 20.0f)
    {
        return (FALSE);
    }

    if (fDistance > pfLength[1])
    {
        VectorScale(vDistance, (fDistance - pfLength[1]) / fDistance, vDistance);
        VectorSubtract(m_vPos, vDistance, m_vPos);
    }
    else if (fDistance < pfLength[0])
    {
        VectorScale(vDistance, (fDistance - pfLength[0]) / fDistance, vDistance);
        VectorSubtract(m_vPos, vDistance, m_vPos);
    }

    return (TRUE);
}

void CPhysicsVertex::AddOneTimeMoveToKeepLength(CPhysicsVertex *pVertex2, float fLength)
{
    vec3_t vDistance;
    float fDistance = std::max<float>(0.001f, GetDistance(pVertex2, &vDistance));
    VectorScale(vDistance, (fDistance - fLength) * 0.5f / fDistance, vDistance);
    VectorSubtract(m_vOneTimeMove, vDistance, m_vOneTimeMove);
    VectorAdd(pVertex2->m_vOneTimeMove, vDistance, pVertex2->m_vOneTimeMove);
    m_iCountOneTimeMove++;
    pVertex2->m_iCountOneTimeMove++;
}

void CPhysicsVertex::DoOneTimeMove(void)
{
    if (PVS_FIXEDPOS & m_byState)
    {
        m_vOneTimeMove[0] = m_vOneTimeMove[1] = m_vOneTimeMove[2] = 0.0f;
        return;
    }

    if (m_iCountOneTimeMove > 0)
    {
        for (int i = 0; i < 3; ++i)
        {
            m_vOneTimeMove[i] /= (float)(m_iCountOneTimeMove);
        }
        VectorAdd(m_vPos, m_vOneTimeMove, m_vPos);
        m_vOneTimeMove[0] = m_vOneTimeMove[1] = m_vOneTimeMove[2] = 0.0f;
        m_iCountOneTimeMove = 0;
    }
}

void CPhysicsVertex::AddOneTimeMove(vec3_t vMove)
{
    VectorAdd(m_vOneTimeMove, vMove, m_vOneTimeMove);
    m_iCountOneTimeMove++;
}

CPhysicsCollision::CPhysicsCollision()
{
    Clear();
}

CPhysicsCollision::~CPhysicsCollision()
{
}

void CPhysicsCollision::Clear(void)
{
    Vector(0.0f, 0.0f, 0.0f, m_vCenterBeforeTransform);
    m_iBone = 0;
    Vector(0.0f, 0.0f, 0.0f, m_vCenter);
}

void CPhysicsCollision::SetPosition(float fXPos, float fYPos, float fZPos)
{
    m_vCenter[0] = fXPos;
    m_vCenter[1] = fYPos;
    m_vCenter[2] = fZPos;
}

void CPhysicsCollision::GetCenter(vec3_t vCenter)
{
    memcpy(vCenter, m_vCenter, sizeof(vec3_t));
}

void CPhysicsCollision::GetCenterBeforeTransform(vec3_t vCenter)
{
    memcpy(vCenter, m_vCenterBeforeTransform, sizeof(vec3_t));
}

void CPhysicsCollision::ProcessCollision(CPhysicsVertex *pVertex)
{
}

CPhysicsColSphere::CPhysicsColSphere()
{
    Clear();
}

CPhysicsColSphere::~CPhysicsColSphere()
{
}

void CPhysicsColSphere::Clear(void)
{
    CPhysicsCollision::Clear();
    m_fRadius = 0.0f;
}

void CPhysicsColSphere::Init(float fXPos, float fYPos, float fZPos, float fRadius, int iBone)
{
    m_vCenterBeforeTransform[0] = fXPos;
    m_vCenterBeforeTransform[1] = fYPos;
    m_vCenterBeforeTransform[2] = fZPos;
    m_fRadius = fRadius;
    m_iBone = iBone;
}

void CPhysicsColSphere::ProcessCollision(CPhysicsVertex *pVertex)
{
    vec3_t vPos;
    pVertex->GetPosition(&vPos);
    VectorSubtract(vPos, m_vCenter, vPos);
    //float fLength = max( 0.001f, VectorLength( vPos));
    float fLength = VectorLength(vPos);
    if (fLength < 0.01f)
    {
        fLength = 0.01f;
        Vector(fLength, 0.0f, 0.0f, vPos);
    }
    if (fLength < m_fRadius)
    {
        VectorScale(vPos, (m_fRadius - fLength) / fLength, vPos);
        pVertex->AddOneTimeMove(vPos);
    }
}

CPhysicsCloth::CPhysicsCloth(SessionKeeper &keeper) noexcept
    : SessionLegacyCalls(keeper), gMapManager(keeper.MapManagerObject()),
      Random(keeper.RandomForConstruction())
{
    Clear();
}

CPhysicsCloth::~CPhysicsCloth()
{
    Destroy();
}

CPhysicsCloth *CPhysicsCloth::AllocateArray(SessionKeeper &keeper, std::size_t count)
{
    auto *cloth = static_cast<CPhysicsCloth *>(::operator new[](sizeof(CPhysicsCloth) * count));
    for (std::size_t i = 0; i < count; ++i)
    {
        new (&cloth[i]) CPhysicsCloth(keeper);
    }
    return cloth;
}

void CPhysicsCloth::DestroyArray(CPhysicsCloth *cloth, std::size_t count)
{
    for (std::size_t i = count; i > 0; --i)
    {
        cloth[i - 1].~CPhysicsCloth();
    }
    ::operator delete[](cloth);
}

void CPhysicsCloth::Clear(void)
{
    m_oOwner = NULL;
    m_pose = nullptr;
    m_poseSample = nullptr;
    m_motionSource = nullptr;
    m_iBone = 0;
    m_iTexFront = m_iTexBack = 0;
    m_dwType = 0;
    m_fxPos = m_fyPos = m_fzPos = 0.0f;
    m_fWidth = m_fHeight = 1.0f;
    m_fUnitWidth = m_fUnitHeight = 1.0f;

    m_stepRemaining = 0.f;
    m_iNumHor = 0;
    m_iNumVer = 0;
    m_iNumVertices = 0;
    m_pVertices = NULL;
    m_iNumLink = 0;
    m_pLink = NULL;

    m_byWindMax = 1;
    m_byWindMin = 1;
}

BOOL CPhysicsCloth::Create(OBJECT *o, int iBone, float fxPos, float fyPos, float fzPos, int iNumHor,
                           int iNumVer, float fWidth, float fHeight, int iTexFront, int iTexBack,
                           DWORD dwType)
{
    assert(iNumHor > 1 && iNumVer > 1);

    m_oOwner = o;
    SetPose(o->BoneTransform);
    VectorCopy(o->Position, m_anchorOrigin);
    m_iBone = iBone;
    m_iTexFront = iTexFront;
    m_iTexBack = iTexBack;
    m_dwType |= dwType;
    m_fxPos = fxPos;
    m_fyPos = fyPos;
    m_fzPos = fzPos;

    if (m_pVertices)
    {
        delete[] m_pVertices;
        m_pVertices = NULL;
    }

    if (m_pLink)
    {
        delete[] m_pLink;
        m_pLink = NULL;
    }

    m_fWidth = fWidth;
    m_fHeight = fHeight;
    m_iNumHor = iNumHor;
    m_iNumVer = iNumVer;
    m_iNumVertices = m_iNumHor * m_iNumVer;
    m_pVertices = new CPhysicsVertex[m_iNumVertices];
    m_stepRemaining = 0.f;

    m_iNumLink = 2 * ((m_iNumHor - 1) * m_iNumVer + m_iNumHor * (m_iNumVer - 1));
    m_pLink = new St_PhysicsLink[m_iNumLink];

    m_fUnitWidth = m_fWidth / (float)(m_iNumHor - 1);
    m_fUnitHeight = m_fHeight / (float)(m_iNumVer - 1);
    int iLink = 0;
    float Matrix[3][4];
    AngleMatrix(m_oOwner->Angle, Matrix);
    if (m_pose)
    {
        for (int i = 0; i < 3; ++i)
        {
            Matrix[i][3] = m_pose[m_iBone][i][3];
        }
    }

    bool bCylinder = false;

    for (int j = 0; j < m_iNumVer; ++j)
    {
        float fWidth = m_fWidth;
        float fUnitWidth = m_fUnitWidth;
        float fUnitHeight = m_fUnitHeight;

        switch (m_dwType & PCT_MASK_SHAPE_EXT)
        {
        case PCT_SHORT_SHOULDER:
            fWidth *= RATE_SHORT_SHOULDER +
                      (1.0f - RATE_SHORT_SHOULDER) * (float)j / (float)(m_iNumVer - 1);
            fUnitWidth = fWidth / (float)(m_iNumHor - 1);
            break;
        case PCT_CYLINDER:
            bCylinder = true;
            break;
        }

        for (int i = 0; i < m_iNumHor; ++i)
        {
            int iVertex = m_iNumHor * j + i;

            vec3_t vPos;
            if (bCylinder)
            {
                float fPosY = sinf((float)i / (float)(m_iNumHor - 1) * Q_PI) * 100.0f;

                vPos[0] = (fUnitWidth * (float)i);
                vPos[1] = fPosY - 30.0f;
                vPos[2] = (fUnitHeight * (float)j);
            }
            else
            {
                vPos[0] = (fUnitWidth * (float)i) - 0.5f * fWidth;
                vPos[1] = 20.0f;
                vPos[2] = -fUnitHeight * (float)j;
            }
            switch (PCT_MASK_SHAPE & m_dwType)
            {
            case PCT_CURVED: {
                float fMove = 2.0f * (float)fabs((float)i / (float)(m_iNumHor - 1) - 0.5f);
                vPos[1] -= 10.0f * fMove * fMove;
            }
            break;
            case PCT_STICKED:
                vPos[1] = 0.0f;
                break;
            }
            NotifyVertexPos(iVertex, vPos);

            vec3_t vPos2;
            if (m_pose)
            {
                TransformAnchor(Matrix, vPos, vPos2);
            }
            else
            {
                VectorCopy(vPos, vPos2);
            }
            m_pVertices[iVertex].Init(vPos2[0], vPos2[1], vPos2[2]);
        }
    }
    BYTE byVerLinkStyle = 0;
    BYTE byCrossLinkStyle = 0;
    switch (PCT_MASK_ELASTIC & m_dwType)
    {
    case PCT_RUBBER:
        break;
    default:
        byVerLinkStyle |= PLS_STRICTDISTANCE;
        byCrossLinkStyle |= PLS_LOOSEDISTANCE;
        break;
    }

    for (int j = 0; j < m_iNumVer; ++j)
    {
        for (int i = 0; i < m_iNumHor; ++i)
        {
            vec3_t vTemp;
            int iVertex = m_iNumHor * j + i;
            if (j < m_iNumVer - 1)
            {
                float fDist =
                    m_pVertices[iVertex].GetDistance(&m_pVertices[iVertex + m_iNumHor], &vTemp);
                SetLink(iLink++, iVertex, iVertex + m_iNumHor, fDist * 0.8f, fDist,
                        PLS_SPRING | byVerLinkStyle);
            }
            if (i < m_iNumHor - 1)
            {
                float fDist = m_pVertices[iVertex].GetDistance(&m_pVertices[iVertex + 1], &vTemp);
                SetLink(iLink++, iVertex, iVertex + 1, fDist * 0.8f, fDist,
                        PLS_SPRING | PLS_LOOSEDISTANCE);

                if (j < m_iNumVer - 1)
                {
                    float fDist = m_pVertices[iVertex].GetDistance(
                        &m_pVertices[iVertex + 1 + m_iNumHor], &vTemp);
                    SetLink(iLink++, iVertex, iVertex + 1 + m_iNumHor, fDist * 0.8f, fDist,
                            byCrossLinkStyle);
                }
                if (j > 1)
                {
                    float fDist = m_pVertices[iVertex].GetDistance(
                        &m_pVertices[iVertex + 1 - m_iNumHor], &vTemp);
                    SetLink(iLink++, iVertex, iVertex + 1 - m_iNumHor, fDist * 0.8f, fDist,
                            byCrossLinkStyle);
                }
            }
        }
    }
    m_iNumLink = iLink;
    SetFixedVertices(Matrix);

    return (TRUE);
}

void CPhysicsCloth::Destroy(void)
{
#ifdef ADD_COLLISION
    CNode<CPhysicsCollision *> *pHead = m_lstCollision.FindHead();
    for (; pHead; pHead = m_lstCollision.GetNext(pHead))
    {
        delete pHead->GetData();
    }
    m_lstCollision.RemoveAll();
#endif

    delete[] m_pLink;
    delete[] m_pVertices;
    Clear();
}

void CPhysicsCloth::TransformAnchor(const float matrix[3][4], const vec3_t local,
                                    vec3_t position) const
{
    VectorTransform(local, matrix, position);
    VectorScale(position, m_oOwner->Scale, position);
    VectorAdd(position, m_anchorOrigin, position);
}

void CPhysicsCloth::SetFixedVertices(const float Matrix[3][4])
{
    bool bCylinder = false;
    for (int iVertex = 0; iVertex < m_iNumHor; ++iVertex)
    {
        float fWidth = m_fWidth;
        float fUnitWidth = m_fUnitWidth;
        switch (PCT_MASK_SHAPE_EXT & m_dwType)
        {
        case PCT_SHORT_SHOULDER:
            fWidth *= RATE_SHORT_SHOULDER;
            fUnitWidth = fWidth / (float)(m_iNumHor - 1);
            break;
        case PCT_CYLINDER:
            bCylinder = true;
            break;
        }

        vec3_t vPos = {(fUnitWidth * (float)iVertex) - 0.5f * fWidth, 10.0f, 0.0f};

        vPos[0] += m_fxPos;
        if (bCylinder)
        {
            float fPosY = sinf((float)iVertex / (float)(m_iNumHor - 1) * Q_PI) * 130.0f;
            vPos[1] = fPosY - 110.0f;
        }
        else
        {
            vPos[1] = m_fyPos;
        }
        vPos[2] = m_fzPos;

        switch (PCT_MASK_SHAPE_EXT2 & m_dwType)
        {
        case PCT_SHAPE_HALLOWEEN:
            vPos[0] += 5.f;
            vPos[1] += 10.f;
            break;
        }

        switch (PCT_MASK_SHAPE & m_dwType)
        {
        case PCT_CURVED:
            float fMove = 2.0f * (float)fabs((float)iVertex / (float)(m_iNumHor - 1) - 0.5f);
            vPos[1] -= 10.0f * fMove * fMove;
            break;
        }

        vec3_t vTemp;
        memcpy(vTemp, vPos, sizeof(vec3_t));
        vPos[0] = vTemp[2];
        vPos[1] = -vTemp[1];
        vPos[2] = vTemp[0];

        vec3_t vPos2;
        TransformAnchor(Matrix, vPos, vPos2);
        m_pVertices[iVertex].Init(vPos2[0], vPos2[1], vPos2[2], TRUE);
    }
}

void CPhysicsCloth::SetLink(int iLink, int iVertex1, int iVertex2, float fDistanceSmall,
                            float fDistanceLarge, BYTE byStyle)
{
    auto &link = m_pLink[iLink];
    link.m_nVertices[0] = iVertex1;
    link.m_nVertices[1] = iVertex2;
    link.m_fDistance[0] = fDistanceSmall;
    link.m_fDistance[1] = fDistanceLarge;
    link.m_byStyle = byStyle;
}

BOOL CPhysicsCloth::Move2(float referenceStepSeconds, int stepsPerReference, float animationFactor,
                          double worldTime)
{
    return Move2(referenceStepSeconds, stepsPerReference, animationFactor, worldTime,
                 1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps);
}

BOOL CPhysicsCloth::Move2(float referenceStepSeconds, int stepsPerReference, float animationFactor,
                          double worldTime, double referenceMilliseconds)
{
    if (animationFactor <= 0.f)
        return TRUE;
    if (m_oOwner == nullptr)
        return FALSE;
    VectorCopy(m_oOwner->Position, m_anchorOrigin);
    if (referenceStepSeconds <= 0.f)
    {
        SetFixedVertices(m_pose[m_iBone]);
        return TRUE;
    }
    auto &poseModel = Models[m_oOwner->Type];
    const AnimationPoseSample poseSample =
        m_poseSample != nullptr
            ? *m_poseSample
            : AnimationPoseSample(m_oOwner, poseModel.BoneHead, poseModel.BodyHeight, false,
                                  poseModel.PoseAssetIdentity());
    const OBJECT &motionSource = m_motionSource != nullptr ? *m_motionSource : *m_oOwner;
    const auto &physics = sessionKeeper_.PhysicsStorage();
    const double startTime = worldTime - animationFactor * referenceMilliseconds;
    float remaining = stepsPerReference * animationFactor;
    float elapsed = 0.f;
    while (remaining > 0.f)
    {
        if (m_stepRemaining <= 0.f)
        {
            const float offset = elapsed / stepsPerReference;
            const double endpointTime =
                startTime + (offset + 1.0 / stepsPerReference) * referenceMilliseconds;
            const float wind = physics.ClothWindAt(sessionKeeper_.FrameWorldTime(), offset);
            // Prepare one fixed numerical endpoint, then expose continuous interpolation towards it.
            const double frameTime = sessionKeeper_.FrameWorldTime();
            const float fraction = offset / animationFactor;
            motionSource.MotionTrace.Sample(frameTime, fraction, motionSource.Position,
                                            m_anchorOrigin);
            const float yaw =
                motionSource.MotionTrace.SampleYaw(frameTime, fraction, poseSample.angle[2]);
            bool integrated;
            if (poseModel.NumActions > 0)
            {
                // A new numerical step can read its start pose at every render rate.
                // The future endpoint is not available during a fractional update.

                auto &model = Models[m_oOwner->Type];
                const float scale = model.BodyScale, height = model.BodyHeight;
                const int head = model.BoneHead;
                vec3_t origin;
                VectorCopy(model.BodyOrigin, origin);
                const auto *currentPose = m_pose;
                std::array<vec34_t, MAX_BONES> sampledBones;
                m_pose = poseSample.EvaluateAtTime(model, motionSource, frameTime, fraction,
                                                   sampledBones.data(), currentPose);

                integrated = IntegrateStep(referenceStepSeconds, endpointTime, wind, yaw);
                m_pose = currentPose;
                model.BodyScale = scale;
                model.BodyHeight = height;
                model.BoneHead = head;
                VectorCopy(origin, model.BodyOrigin);
            }
            else
                integrated = IntegrateStep(referenceStepSeconds, endpointTime, wind, yaw);
            if (!integrated)
                return FALSE;
            m_stepRemaining = 1.f;
        }
        const float step = (std::min)(remaining, m_stepRemaining);
        m_stepRemaining -= step;
        elapsed += step;
        remaining -= step;
    }
    VectorCopy(m_oOwner->Position, m_anchorOrigin);
    SetFixedVertices(m_pose[m_iBone]);
    return TRUE;
}

BOOL CPhysicsCloth::IntegrateStep(float fTime, double worldTime, float sharedWind, float ownerYaw)
{
    switch (PCT_MASK_ELASTIC & m_dwType)
    {
    case PCT_RUBBER2:
        m_fWind =
            Random.RangeInt(m_byWindMin, static_cast<int>(m_byWindMin) + m_byWindMax - 1) / 100.0f;
        break;
    default:
        if (gMapManager.ContextMap() == 55)
            m_fWind = Random.RangeInt(10, 49) / 50.0f;
        else
            m_fWind = sharedWind;
        break;
    }
    switch (PCT_MASK_SHAPE_EXT & m_dwType)
    {
    case PCT_CYLINDER:
        m_fWind = Random.RangeInt(25, 34) / 50.0f;
        break;
    }

    if (m_oOwner == NULL)
        return (FALSE);

    s_vWind[0] = m_fWind * sinf((180.0f + ownerYaw) * Q_PI / 180.0f);
    s_vWind[1] = -m_fWind * cosf((180.0f + ownerYaw) * Q_PI / 180.0f);

    SetFixedVertices(m_pose[m_iBone]);
    MoveVertices(fTime, worldTime);

    if (!PreventFromStretching())
    {
        return (FALSE);
    }

    return (TRUE);
}

void CPhysicsCloth::GetPosition(int index, vec3_t *pPos) const
{
    m_pVertices[index].GetInterpolatedPosition(1.f - m_stepRemaining, pPos);
}

void CPhysicsCloth::InitForces(double worldTime)
{
    const int iSeed = static_cast<int>(worldTime / 400.0) * 101 % m_iNumVertices;

    for (int iVertex = 0; iVertex < m_iNumVertices; ++iVertex)
    {
        m_pVertices[iVertex].BeginStep();
        m_pVertices[iVertex].UpdateForce(abs(iSeed % m_iNumHor - iVertex % m_iNumHor) +
                                             abs(iSeed / m_iNumHor - iVertex / m_iNumHor),
                                         worldTime, s_vWind, m_dwType, m_fWind);
    }
}

void CPhysicsCloth::MoveVertices(float fTime, double worldTime)
{
    InitForces(worldTime);

    for (int iLink = 0; iLink < m_iNumLink; ++iLink)
    {
        St_PhysicsLink *pLink = &m_pLink[iLink];
        if (pLink->m_byStyle & PLS_SPRING)
        {
            CPhysicsVertex *pVertex1 = &m_pVertices[pLink->m_nVertices[0]];
            CPhysicsVertex *pVertex2 = &m_pVertices[pLink->m_nVertices[1]];
            vec3_t vDistance;
            float fDistance = std::max<float>(0.001f, pVertex1->GetDistance(pVertex2, &vDistance));
            if (fDistance > pLink->m_fDistance[1] + 0.01f)
            {
                vec3_t vForce;
                for (int i = 0; i < 3; ++i)
                {
                    vForce[i] = (fDistance - pLink->m_fDistance[1]) * vDistance[i] / fDistance;
                    if (PCT_OPT_CORRECTEDFORCE & m_dwType)
                    {
                        vForce[i] *= (pLink->m_fDistance[1] / 32.0f);
                    }
                    switch (PCT_MASK_ELASTIC & m_dwType)
                    {
                    case PCT_RUBBER:
                        vForce[i] *= 3.0f;
                        break;
                    }

                    switch (PCT_MASK_ELASTIC_EXT & m_dwType)
                    {
                    case PCT_ELASTIC_HALLOWEEN:
                        vForce[i] *= 2.0f;
                        break;
                    case PCT_ELASTIC_RAGE_L:
                    case PCT_ELASTIC_RAGE_R: {
                        vForce[i] *= 1.0f;
                    }
                    break;
                    }
                }
                pVertex1->AddToForce(-vForce[0], -vForce[1], -vForce[2]);
                pVertex2->AddToForce(vForce[0], vForce[1], vForce[2]);
            }
        }
    }

    for (int iVertex = 0; iVertex < m_iNumVertices; ++iVertex)
    {
        m_pVertices[iVertex].Move(fTime);
    }
}

BOOL CPhysicsCloth::PreventFromStretching(void)
{
#ifdef ADD_COLLISION
    CNode<CPhysicsCollision *> *pHead = m_lstCollision.FindHead();
    for (; pHead; pHead = m_lstCollision.GetNext(pHead))
    {
        CPhysicsCollision *pCollision = pHead->GetData();

        vec3_t vPos;
        pCollision->GetCenterBeforeTransform(vPos);
        {
            vec3_t vTemp;
            memcpy(vTemp, vPos, sizeof(vec3_t));
            vPos[0] = vTemp[2];
            vPos[1] = -vTemp[1];
            vPos[2] = vTemp[0];
        }
        vec3_t vPos2;
        TransformAnchor(m_pose[pCollision->GetBone()], vPos, vPos2);
        pCollision->SetPosition(vPos2[0], vPos2[1], vPos2[2]);
    }
#endif
    ProcessCollision();

    for (int iLink = 0; iLink < m_iNumLink; ++iLink)
    {
        St_PhysicsLink *pLink = &m_pLink[iLink];
        if (pLink->m_byStyle & PLS_LOOSEDISTANCE)
        {
            CPhysicsVertex *pVertex1 = &m_pVertices[pLink->m_nVertices[0]];
            CPhysicsVertex *pVertex2 = &m_pVertices[pLink->m_nVertices[1]];

            pVertex1->AddOneTimeMoveToKeepLength(pVertex2, pLink->m_fDistance[1]);
        }
    }
    for (int j = 0; j < m_iNumVer; ++j)
    {
        for (int i = 0; i < m_iNumHor; ++i)
        {
            int iVertex = m_iNumHor * j + i;
            m_pVertices[iVertex].DoOneTimeMove();
        }
    }

    for (int iLink = 0; iLink < m_iNumLink; ++iLink)
    {
        St_PhysicsLink *pLink = &m_pLink[iLink];
        if (pLink->m_nVertices[1] >= m_iNumHor && (pLink->m_byStyle & PLS_STRICTDISTANCE))
        {
            CPhysicsVertex *pVertex1 = &m_pVertices[pLink->m_nVertices[0]];
            CPhysicsVertex *pVertex2 = &m_pVertices[pLink->m_nVertices[1]];

            if (!pVertex2->KeepLength(pVertex1, pLink->m_fDistance))
            {
                return (FALSE);
            }
        }
    }

    return (TRUE);
}

void CPhysicsCloth::AddCollisionSphere(float fXPos, float fYPos, float fZPos, float fRadius,
                                       int iBone)
{
#ifdef ADD_COLLISION
    auto *pCol = new CPhysicsColSphere;
    pCol->Init(fXPos, fYPos, fZPos, fRadius, iBone);
    m_lstCollision.AddTail(pCol);
#endif
}

void CPhysicsCloth::ProcessCollision(void)
{
#ifdef ADD_COLLISION
    if (m_lstCollision.GetCount() > 0)
    {
        CNode<CPhysicsCollision *> *pHead = m_lstCollision.FindHead();
        for (; pHead; pHead = m_lstCollision.GetNext(pHead))
        {
            CPhysicsCollision *pCollision = pHead->GetData();
            for (int i = 0; i < m_iNumVertices; ++i)
            {
                pCollision->ProcessCollision(&m_pVertices[i]);
            }
        }

        for (int i = 0; i < m_iNumVertices; ++i)
        {
            m_pVertices[i].DoOneTimeMove();
        }
    }
#endif
}

CPhysicsClothMesh::CPhysicsClothMesh(SessionKeeper &keeper) noexcept : CPhysicsCloth(keeper)
{
}

CPhysicsClothMesh::~CPhysicsClothMesh()
{
    Clear();
}

CPhysicsClothMesh *CPhysicsClothMesh::AllocateSingle(SessionKeeper &keeper)
{
    auto *cloth = static_cast<CPhysicsClothMesh *>(::operator new[](sizeof(CPhysicsClothMesh)));
    new (cloth) CPhysicsClothMesh(keeper);
    return cloth;
}

void CPhysicsClothMesh::Clear(void)
{
    CPhysicsCloth::Destroy();
}

BOOL CPhysicsClothMesh::Create(OBJECT *o, int iMesh, int iBone, DWORD dwType, int iBMDType)
{
    m_oOwner = o;
    SetPose(o->BoneTransform);
    VectorCopy(o->Position, m_anchorOrigin);
    m_iMesh = iMesh;
    m_iBone = iBone;
    m_dwType |= dwType;

    m_iBMDType = (iBMDType == -1) ? m_oOwner->Type : iBMDType;
    BMD *b = &Models[m_iBMDType];
    assert(iMesh < b->NumMeshs);
    Mesh_t *pMesh = &b->Meshs[m_iMesh];
    drawMesh_ = *pMesh;

    if (m_pVertices)
    {
        delete[] m_pVertices;
        m_pVertices = NULL;
    }

    if (m_pLink)
    {
        delete[] m_pLink;
        m_pLink = NULL;
    }

    m_iNumVertices = pMesh->NumVertices;
    m_pVertices = new CPhysicsVertex[m_iNumVertices];
    m_stepRemaining = 0.f;

    m_iNumLink = pMesh->NumTriangles * 3 * 2;
    m_pLink = new St_PhysicsLink[m_iNumLink];

    const auto *BoneMatrix = m_pose;

    for (int iVertex = 0; iVertex < m_iNumVertices; ++iVertex)
    {
        Vertex_t *v = &pMesh->Vertices[iVertex];
        vec3_t vPos;
        TransformAnchor(BoneMatrix[v->Node], v->Position, vPos);
        m_pVertices[iVertex].Init(vPos[0], vPos[1], vPos[2], (v->Node == m_iBone));
    }

    int iLink = 0;
    vec3_t vTemp;
    for (int iTri = 0; iTri < pMesh->NumTriangles; iTri++)
    {
        Triangle_t *tp = &pMesh->Triangles[iTri];
        for (int i = 0; i < 3; ++i)
        {
            int iV1 = tp->VertexIndex[i];
            int iV2 = tp->VertexIndex[(i + 1) % 3];
            float fDist = m_pVertices[iV1].GetDistance(&m_pVertices[iV2], &vTemp);

            BYTE byLinkType = PLS_STRICTDISTANCE;

            vec3_t vPos1, vPos2;
            m_pVertices[iV1].GetPosition(&vPos1);
            m_pVertices[iV2].GetPosition(&vPos2);
            if (fabs(vPos1[0] - vPos2[0]) > 10.0f)
            {
                byLinkType = PLS_LOOSEDISTANCE;
            }

            SetLink(iLink++, iV1, iV2, fDist * 0.5f, fDist, PLS_SPRING | byLinkType);

            int iV3 = tp->VertexIndex[(i + 2) % 3];
            int iMatch = FindMatchVertex(pMesh, iV1, iV2, iV3);
            if (iMatch > 0)
            {
                float fDist2 = m_pVertices[iV3].GetDistance(&m_pVertices[iMatch], &vTemp);
                if (fDist2 < fDist * 1.2f && !FindInLink(iLink, iV3, iMatch))
                {
                    SetLink(iLink++, iV3, iMatch, fDist2 * 0.5f, fDist2, PLS_SPRING | byLinkType);
                }
            }
        }
    }
    m_iNumLink = iLink;

    return (TRUE);
}

BOOL CPhysicsClothMesh::Create(OBJECT *o, int iMesh, int iBone, float fxPos, float fyPos,
                               float fzPos, int iNumHor, int iNumVer, float fWidth, float fHeight,
                               int iTexFront, int TexBack, DWORD dwType, int iBMDType)
{
    m_iBMDType = (iBMDType == -1) ? o->Type : iBMDType;

    m_iMesh = iMesh;

    m_dwType |= PCT_OPT_MESHPROG;
    if (!CPhysicsCloth::Create(o, iBone, fxPos, fyPos, fzPos, iNumHor, iNumVer, fWidth, fHeight,
                               iTexFront, TexBack, dwType))
    {
        return (FALSE);
    }

    BMD *b = &Models[m_iBMDType];
    assert(iMesh < b->NumMeshs);
    drawMesh_ = b->Meshs[m_iMesh];
    clothTriangles_.assign(drawMesh_.Triangles, drawMesh_.Triangles + drawMesh_.NumTriangles);
    clothTexCoords_.assign(drawMesh_.TexCoords, drawMesh_.TexCoords + drawMesh_.NumTexCoords);
    drawMesh_.Triangles = clothTriangles_.data();
    drawMesh_.TexCoords = clothTexCoords_.data();
    drawMesh_.RenderTapeVertexSources.reset();
    drawMesh_.RenderTapeIndices.reset();
    Mesh_t *pMesh = &drawMesh_;

    Render::Models::PrepareClothGridTopology(*pMesh, m_iNumHor, m_iNumVer);

    return (TRUE);
}

int CPhysicsClothMesh::FindMatchVertex(Mesh_t *pMesh, int iV1, int iV2, int iV3)
{
    for (int iTri = 0; iTri < pMesh->NumTriangles; iTri++)
    {
        Triangle_t *tp = &pMesh->Triangles[iTri];
        for (int i = 0; i < 3; ++i)
        {
            int iVcompare1 = tp->VertexIndex[i];
            int iVcompare2 = tp->VertexIndex[(i + 1) % 3];
            int iVcompare3 = tp->VertexIndex[(i + 2) % 3];
            if (iV1 == iVcompare2 && iV2 == iVcompare1 && iV3 != iVcompare3)
            {
                return (iVcompare3);
            }
        }
    }

    return (-1);
}

BOOL CPhysicsClothMesh::FindInLink(int iCount, int iV1, int iV2)
{
    for (int iLink = 0; iLink < iCount; ++iLink)
    {
        if (m_pLink[iLink].m_nVertices[0] == iV1 && m_pLink[iLink].m_nVertices[1] == iV2)
        {
            return (TRUE);
        }
    }

    return (FALSE);
}

void CPhysicsClothMesh::SetFixedVertices(const float Matrix[3][4])
{
    if (m_dwType & PCT_OPT_MESHPROG)
    {
        CPhysicsCloth::SetFixedVertices(Matrix);
        return;
    }

    BMD *b = &Models[m_iBMDType];
    Mesh_t *pMesh = &b->Meshs[m_iMesh];

    for (int iVertex = 0; iVertex < m_iNumVertices; ++iVertex)
    {
        Vertex_t *v = &pMesh->Vertices[iVertex];
        if (v->Node == m_iBone)
        {
            vec3_t vPos;
            TransformAnchor(Matrix, v->Position, vPos);
            m_pVertices[iVertex].Init(vPos[0], vPos[1], vPos[2], TRUE);
        }
    }
}

void CPhysicsClothMesh::InitForces(double worldTime)
{
    if (m_dwType & PCT_OPT_MESHPROG)
    {
        CPhysicsCloth::InitForces(worldTime);
        return;
    }

    const int iSeed = static_cast<int>(worldTime / 400.0) * 101 % m_iNumVertices;

    for (int iVertex = 0; iVertex < m_iNumVertices; ++iVertex)
    {
        m_pVertices[iVertex].BeginStep();
        m_pVertices[iVertex].UpdateForce(abs(iSeed % 10), worldTime, s_vWind, m_dwType, m_fWind);
    }
}

CPhysicsManager::CPhysicsManager(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), Random(keeper.RandomForConstruction())
{
    Clear();
}

CPhysicsManager::~CPhysicsManager()
{
    RemoveAll();
}

void CPhysicsManager::Clear(void)
{
}

void CPhysicsManager::PrepareWind(float animationFactor)
{
    SessionRandom::PresentationScope presentation(Random);
    auto &physics = sessionKeeper_.PhysicsStorage();
    if (physics.clothWindWorldTime == sessionKeeper_.FrameWorldTime() &&
        !physics.clothWindSamples.empty())
        return;
    physics.clothWindWorldTime = sessionKeeper_.FrameWorldTime();
    physics.clothWindSamples.clear();
    float remaining = animationFactor, elapsed = 0.f;
    while (remaining > 0.f)
    {
        if (physics.clothWindFrames <= 0.f)
        {
            physics.clothWind =
                std::clamp(physics.clothWind + Random.RangeInt(-100, 99) * 0.001f, -0.2f, 1.f);
            physics.clothWindFrames = 1.f;
        }
        physics.clothWindSamples.push_back({elapsed, physics.clothWind});
        const float step = (std::min)(remaining, physics.clothWindFrames);
        physics.clothWindFrames -= step;
        elapsed += step;
        remaining -= step;
    }
}

void CPhysicsManager::Move(float referenceStepSeconds, float animationFactor, double worldTime)
{
    PrepareWind(animationFactor);
    SessionRandom::PresentationScope presentation(Random);
    for (auto *node = m_lstCloth.FindHead(); node; node = m_lstCloth.GetNext(node))
        node->GetData()->Move2(referenceStepSeconds, 1, animationFactor, worldTime);
}

void CPhysicsManager::Add(CPhysicsCloth *pCloth)
{
    m_lstCloth.AddTail(pCloth);
}

void CPhysicsManager::Remove(OBJECT *oOwner)
{
    CNode<CPhysicsCloth *> *pNode = m_lstCloth.FindHead();
    for (; pNode; pNode = m_lstCloth.GetNext(pNode))
    {
        if (oOwner == pNode->GetData()->GetOwner())
        {
            pNode->GetData()->Destroy();
            delete pNode->GetData();
            m_lstCloth.RemoveNode(pNode);
            break;
        }
    }
}

void CPhysicsManager::RemoveAll(void)
{
    CNode<CPhysicsCloth *> *pNode = m_lstCloth.FindHead();
    for (; pNode; pNode = m_lstCloth.GetNext(pNode))
    {
        pNode->GetData()->Destroy();
        delete pNode->GetData();
    }
    m_lstCloth.RemoveAll();
}
OBJECT *SessionInteractionUnit::CollisionDetectObjects(OBJECT *PickObject)
{
    auto &ObjectBlock = sessionKeeper_.ObjectBlocks();

    OBJECT *Object = NULL;
    InitCollisionDetectLineToFace();
    for (int i = 0; i < 16; i++)
    {
        for (int j = 0; j < 16; j++)
        {
            OBJECT_BLOCK *ob = &ObjectBlock[i * 16 + j];
            OBJECT *o = ob->Head;
            while (1)
            {
                if (o != NULL)
                {
                    if (o->Live && o->Visible && o->Alpha >= 0.01f)
                    {
                        //if(o != PickObject)
                        {
                            BMD *b = &Models[o->Type];
                            b->BodyScale = o->Scale;
                            b->CurrentAction = o->CurrentAction;
                            VectorCopy(o->Position, b->BodyOrigin);
                            b->Animation(BoneTransform, o->AnimationFrame, o->PriorAnimationFrame,
                                         o->PriorAction, o->Angle, o->HeadAngle, false, false);
                            b->Transform(BoneTransform, o->BoundingBoxMin, o->BoundingBoxMax,
                                         &o->OBB, true);
                            if (CollisionDetectLineToOBB(MousePosition, MouseTarget, o->OBB))
                            {
                                if (b->CollisionDetectLineToMesh(MousePosition, MouseTarget))
                                {
                                    Object = o;
                                    //return Object;
                                }
                            }
                        }
                    }
                    if (o->Next == NULL)
                        break;
                    o = o->Next;
                }
                else
                    break;
            }
        }
    }
    return Object;
}

namespace
{
bool ProjectLineBox(vec3_t ax, vec3_t p1, vec3_t p2, OBB_t obb)
{
    float P1 = DotProduct(ax, p1);
    float P2 = DotProduct(ax, p2);

    float mx1 = maxf(P1, P2);
    float mn1 = minf(P1, P2);

    float ST = DotProduct(ax, obb.StartPos);
    float Q1 = DotProduct(ax, obb.XAxis);
    float Q2 = DotProduct(ax, obb.YAxis);
    float Q3 = DotProduct(ax, obb.ZAxis);

    float mx2 = ST;
    float mn2 = ST;

    if (Q1 > 0)
        mx2 += Q1;
    else
        mn2 += Q1;
    if (Q2 > 0)
        mx2 += Q2;
    else
        mn2 += Q2;
    if (Q3 > 0)
        mx2 += Q3;
    else
        mn2 += Q3;

    if (mn1 > mx2)
        return false;
    if (mn2 > mx1)
        return false;

    return true;
}
} // namespace

bool CollisionDetectLineToOBB(vec3_t p1, vec3_t p2, OBB_t obb)
{
    vec3_t e1;
    vec3_t eq11, eq12, eq13;

    VectorSubtract(p2, p1, e1);

    CrossProduct(e1, obb.XAxis, eq11);
    CrossProduct(e1, obb.YAxis, eq12);
    CrossProduct(e1, obb.ZAxis, eq13);

    if (!ProjectLineBox(eq11, p1, p2, obb))
        return false;
    if (!ProjectLineBox(eq12, p1, p2, obb))
        return false;
    if (!ProjectLineBox(eq13, p1, p2, obb))
        return false;

    if (!ProjectLineBox(obb.XAxis, p1, p2, obb))
        return false;
    if (!ProjectLineBox(obb.YAxis, p1, p2, obb))
        return false;
    if (!ProjectLineBox(obb.ZAxis, p1, p2, obb))
        return false;

    return true;
}
