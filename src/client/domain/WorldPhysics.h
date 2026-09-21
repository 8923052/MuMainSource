#pragma once
#include "support/CoreMath.h"
#include "render/ModelResources.h"
#include "data/Localization.h"
#include "domain/CharacterSystem.h"
#include "domain/ItemsSkills.h"
#include "domain/CharacterPresentation.h"
#include "domain/WorldSimulation.h"
#include "render/World.h"
#include "session/SessionRuntime.h"
#include <cstddef>
#include <algorithm>
#include <vector>
#include <iterator>

#if !defined(AFX_PHYSICSMANAGER_H__11A9449A_CF75_4963_8F71_C8EB8EA7FE2D__INCLUDED_)
#define AFX_PHYSICSMANAGER_H__11A9449A_CF75_4963_8F71_C8EB8EA7FE2D__INCLUDED_

class CMapManager;
class CPhysicsManager;
class SessionRenderUnit;
class SessionKeeper;
class SessionRandom;

#define PVS_NORMAL (0x00)
#define PVS_FIXEDPOS (0x01)

class CPhysicsVertex
{
  public:
    CPhysicsVertex();
    virtual ~CPhysicsVertex();
    void Clear(void);

    inline static constexpr float s_Gravity = 9.8f;
    inline static constexpr float s_fMass = 0.0025f;
    inline static constexpr float s_fInvOfMass = 400.0f;

  protected:
    vec3_t m_vForce;
    vec3_t m_vVel;
    vec3_t m_vPos;
    vec3_t m_vPreviousPos;
    BYTE m_byState;

  public:
    void Init(float fXPos, float fYPos, float fZPos, BOOL bFixed = FALSE);
    void UpdateForce(unsigned int iKey, double worldTime, const vec3_t &wind, DWORD dwType = 0,
                     float fWind = 0.f);
    void AddToForce(float fXForce, float fYForce, float fZForce);
    void Move(float fTime);

  public:
    void GetPosition(vec3_t *pPos) const;
    void BeginStep();
    void GetInterpolatedPosition(float fraction, vec3_t *position) const;
    float GetDistance(CPhysicsVertex *pVertex2, vec3_t *pDistance);

  protected:
    int m_iCountOneTimeMove;
    vec3_t m_vOneTimeMove;

  public:
    BOOL KeepLength(CPhysicsVertex *pVertex2, float *pfLength);
    void AddOneTimeMoveToKeepLength(CPhysicsVertex *pVertex2, float fLength);
    void DoOneTimeMove(void);
    void AddOneTimeMove(vec3_t vMove);
};

enum ENUM_COLLISION_TYPE
{
    CLT_DEFAULT = 0,
    CLT_SPHERE,
    NUM_CLT
};

class CPhysicsCollision
{
  public:
    CPhysicsCollision();
    virtual ~CPhysicsCollision();
    void Clear(void);

  protected:
    vec3_t m_vCenterBeforeTransform;
    int m_iBone;
    vec3_t m_vCenter;

  public:
    virtual int GetType(void)
    {
        return (CLT_DEFAULT);
    }

    int GetBone(void)
    {
        return (m_iBone);
    }
    void SetPosition(float fXPos, float fYPos, float fZPos);
    void GetCenter(vec3_t vCenter);
    void GetCenterBeforeTransform(vec3_t vCenter);
    virtual void ProcessCollision(CPhysicsVertex *pVertex);
};

class CPhysicsColSphere : public CPhysicsCollision
{
  public:
    CPhysicsColSphere();
    virtual ~CPhysicsColSphere();
    void Clear(void);

  protected:
    float m_fRadius;

  public:
    virtual int GetType(void)
    {
        return (CLT_SPHERE);
    }

    void Init(float fXPos, float fYPos, float fZPos, float fRadius, int iBone);
    virtual void ProcessCollision(CPhysicsVertex *pVertex);
    float GetRadius(void)
    {
        return (m_fRadius);
    }
};

#define PCT_MASK_SHAPE (0x00000003)
#define PCT_FLAT (0x00000000)
#define PCT_CURVED (0x00000001)
#define PCT_STICKED (0x00000002)

#define PCT_MASK_SHAPE_EXT (0x0000000C)
#define PCT_SHAPE_NORMAL (0x00000000)
#define PCT_SHORT_SHOULDER (0x00000004)
#define PCT_CYLINDER (0x00000008)

#define PCT_MASK_SHAPE_EXT2 (0x00000030)
#define PCT_SHAPE_HALLOWEEN (0x00000010)

#define PCT_MASK_ELASTIC (0x00000300)
#define PCT_COTTON (0x00000000)
#define PCT_RUBBER (0x00000100)
#define PCT_RUBBER2 (0x00000200)

#define PCT_MASK_WEIGHT (0x00000C00)
#define PCT_NORMAL_THICKNESS (0x00000000)
#define PCT_HEAVY (0x00000400)

#define PCT_MASK_DRAW (0x00003000)
#define PCT_MASK_BLIT (0x00000000)
#define PCT_MASK_ALPHA (0x00001000)
#define PCT_MASK_BLEND (0x00002000)

#define PCT_MASK_ELASTIC_EXT (0x0000E000)
#define PCT_ELASTIC_HALLOWEEN (0x00004000)
#define PCT_ELASTIC_RAGE_L (0x00008000)
#define PCT_ELASTIC_RAGE_R (0x0000C000)

#define PCT_OPT_MESHPROG (0x10000000)
#define PCT_OPT_CORRECTEDFORCE (0x20000000)
#define PCT_MASK_LIGHT (0x40000000)
#define PCT_OPT_HAIR (0x80000000)

#define PLS_NORMAL (0x00)
#define PLS_LOOSEDISTANCE (0x01)
#define PLS_SPRING (0x02)
#define PLS_STRICTDISTANCE (0x04)

typedef struct
{
    short m_nVertices[2];
    float m_fDistance[2];
    BYTE m_byStyle;
} St_PhysicsLink;

struct AnimationPoseSample;

class CPhysicsCloth : protected SessionLegacyCalls
{
    friend class SessionRenderUnit;
    friend class PhysicsClothTestPeer;

  public:
    explicit CPhysicsCloth(SessionKeeper &keeper) noexcept;
    virtual ~CPhysicsCloth();
    static CPhysicsCloth *AllocateArray(SessionKeeper &keeper, std::size_t count);
    static void DestroyArray(CPhysicsCloth *cloth, std::size_t count);
    void Clear(void);

  protected:
    CMapManager &gMapManager;
    SessionRandom &Random;
    OBJECT *m_oOwner;
    const vec34_t *m_pose = nullptr;
    const AnimationPoseSample *m_poseSample = nullptr;
    const OBJECT *m_motionSource = nullptr;
    vec3_t m_anchorOrigin{};
    int m_iBone;
    int m_iTexFront;
    int m_iTexBack;
    DWORD m_dwType;
    float m_fxPos, m_fyPos, m_fzPos;
    float m_fWidth, m_fHeight;
    int m_iNumHor, m_iNumVer;
    int m_iNumVertices;
    CPhysicsVertex *m_pVertices;
    int m_iNumLink;
    St_PhysicsLink *m_pLink;

    float m_fWind;
    float m_stepRemaining = 0.f;
    BYTE m_byWindMax;
    BYTE m_byWindMin;

  public:
    OBJECT *GetOwner(void)
    {
        return (m_oOwner);
    }
    void SetPose(const vec34_t *pose, const AnimationPoseSample *sample = nullptr,
                 const OBJECT *source = nullptr) noexcept
    {
        m_pose = pose;
        m_poseSample = sample;
        m_motionSource = source;
    }

  protected:
    float m_fUnitWidth, m_fUnitHeight;

  public:
    virtual BOOL Create(OBJECT *o, int iBone, float fxPos, float fyPos, float fzPos, int iNumHor,
                        int iNumVer, float fWidth, float fHeight, int iTexFront, int TexBack,
                        DWORD dwType = 0);
    virtual void Destroy(void);

  protected:
    void TransformAnchor(const float matrix[3][4], const vec3_t local, vec3_t position) const;
    virtual void SetFixedVertices(const float Matrix[3][4]);
    virtual void NotifyVertexPos(int iVertex, vec3_t vPos)
    {
    }
    void SetLink(int iLink, int iVertex1, int iVertex2, float fDistanceSmall, float fDistanceLarge,
                 BYTE byStyle);

  public:
    BOOL Move2(float fTime, int iCount, float animationFactor, double worldTime);
    BOOL Move2(float referenceStepSeconds, int stepsPerReference, float animationFactor,
               double worldTime, double referenceMilliseconds);
    void GetPosition(int index, vec3_t *pPos) const;
    int GetVerticesCount() const
    {
        return m_iNumVertices;
    }
    int GetHorizontalCount() const
    {
        return m_iNumHor;
    }
    int GetVerticalCount() const
    {
        return m_iNumVer;
    }

  protected:
    virtual void InitForces(double worldTime);
    BOOL IntegrateStep(float seconds, double worldTime, float sharedWind, float ownerYaw);
    void MoveVertices(float seconds, double worldTime);
    BOOL PreventFromStretching(void);

  public:
    virtual void Render(SessionRenderUnit &renderer, const vec3_t *pvColor = NULL,
                        int iLevel = 0) const;

  protected:
    void RenderCollisions(void);

  protected:
    CList<CPhysicsCollision *> m_lstCollision;

  public:
    void SetWindMinMax(BYTE Min, BYTE Max)
    {
        m_byWindMin = Min;
        m_byWindMax = Max;
    }
    void AddCollisionSphere(float fXPos, float fYPos, float fZPos, float fRadius, int iBone);
    void ProcessCollision(void);
};

class CPhysicsClothMesh : public CPhysicsCloth
{
  public:
    explicit CPhysicsClothMesh(SessionKeeper &keeper) noexcept;
    virtual ~CPhysicsClothMesh();
    static CPhysicsClothMesh *AllocateSingle(SessionKeeper &keeper);
    virtual void Clear(void);

  protected:
    Mesh_t drawMesh_;
    std::vector<Triangle_t> clothTriangles_;
    std::vector<TexCoord_t> clothTexCoords_;
    int m_iMesh;
    int m_iBoneForFixed;
    int m_iBMDType;

  public:
    int MeshIndex() const noexcept
    {
        return m_iMesh;
    }
    const Mesh_t &DrawMesh() const noexcept
    {
        return drawMesh_;
    }
    virtual BOOL Create(OBJECT *o, int iMesh, int iBone, DWORD dwType = 0, int iBMDType = -1);
    virtual BOOL Create(OBJECT *o, int iMesh, int iBone, float fxPos, float fyPos, float fzPos,
                        int iNumHor, int iNumVer, float fWidth, float fHeight, int iTexFront,
                        int TexBack, DWORD dwType = 0, int iBMDType = -1);

  protected:
    int FindMatchVertex(Mesh_t *pMesh, int iV1, int iV2, int iV3);
    BOOL FindInLink(int iCount, int iV1, int iV2);
    virtual void SetFixedVertices(const float Matrix[3][4]);
    virtual void InitForces(double worldTime);

  public:
    virtual void Render(SessionRenderUnit &renderer, const vec3_t *pvColor = NULL,
                        int iLevel = 0) const;
};

class CPhysicsManager : protected SessionLegacyCalls
{
  public:
    explicit CPhysicsManager(SessionKeeper &keeper);
    virtual ~CPhysicsManager();
    void Clear(void);

  public:
    void PrepareWind(float animationFactor);
    void Move(float fTime, float animationFactor, double worldTime);
    friend class SessionRenderUnit;

  protected:
    CList<CPhysicsCloth *> m_lstCloth;
    SessionRandom &Random;

  public:
    void Add(CPhysicsCloth *pCloth);
    void Remove(OBJECT *oOwner);
    void RemoveAll(void);
};

#endif // !defined(AFX_PHYSICSMANAGER_H__11A9449A_CF75_4963_8F71_C8EB8EA7FE2D__INCLUDED_)

struct SessionPhysicsStorage final
{
    struct ClothWindSample
    {
        float offset;
        float value;
    };
    float clothWind = 0.f;
    float clothWindFrames = 0.f;
    double clothWindWorldTime = -1.0;
    std::vector<ClothWindSample> clothWindSamples;

    float ClothWindAt(double frameTime, float offset) const
    {
        if (clothWindWorldTime != frameTime || clothWindSamples.empty())
            return clothWind;
        const auto next = std::upper_bound(
            clothWindSamples.begin(), clothWindSamples.end(), offset,
            [](float time, const ClothWindSample &sample) { return time < sample.offset; });
        return next == clothWindSamples.begin() ? next->value : std::prev(next)->value;
    }

    vec3_t clothWindVector{};
    vec3_t particleWind{};
    vec3_t particleWindVelocity{};
    float particleWindFrames = 0.f;
    struct ParticleWindSample
    {
        float end;
        float x, y;
    };
    float particleWindUpdateFrames = 0.f;
    std::vector<ParticleWindSample> particleWindSamples;

    void ParticleWindTravel(float frames, vec3_t travel) const
    {
        travel[0] = travel[1] = travel[2] = 0.f;
        if (particleWindSamples.empty())
            return;
        const auto &last = particleWindSamples.back();
        if (frames == particleWindUpdateFrames)
        {
            travel[0] = last.x;
            travel[1] = last.y;
            return;
        }
        const float start = particleWindUpdateFrames - frames;
        const auto next = std::lower_bound(
            particleWindSamples.begin(), particleWindSamples.end(), start,
            [](const ParticleWindSample &sample, float time) { return sample.end < time; });
        const ParticleWindSample previous =
            next == particleWindSamples.begin() ? ParticleWindSample{} : *std::prev(next);
        const float fraction = (start - previous.end) / (next->end - previous.end);
        travel[0] = last.x - (previous.x + (next->x - previous.x) * fraction);
        travel[1] = last.y - (previous.y + (next->y - previous.y) * fraction);
    }
};
