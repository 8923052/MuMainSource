#pragma once
#include "render/Assets.h"
#include "render/FrameTape.h"
#include "render/World.h"
#include "support/CoreMath.h"
#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct CharacterSocketBinding;
struct Triangle_t;
#define RENDER_FACE_ONE 0x01
#define RENDER_FACE_TWO 0x02

class CHARACTER;
class OBJECT;

// Exact-session admission only. Socket names/indices are character-owned facts.
class CBoneManager
{
  public:
    ~CBoneManager()
    {
        UnregisterAll();
    }
    void Admit(OBJECT *object, CHARACTER *character)
    {
        characters_.try_emplace(object, Admission{character, {}});
    }
    void Forget(OBJECT *object);
    void RegisterBone(CHARACTER *character, const std::wstring &name, int bone);
    void UnregisterBone(CHARACTER *character);
    void UnregisterAll();
    const CHARACTER *FindCharacter(const OBJECT *object) const noexcept
    {
        const auto found = characters_.find(object);
        return found == characters_.end() ? nullptr : found->second.character;
    }
    CHARACTER *GetOwnCharacter(OBJECT *object, const std::wstring &name);
    int GetBoneNumber(OBJECT *object, const std::wstring &name);
    bool GetBonePosition(const OBJECT *object, int bone, vec3_t position) const;
    bool GetBonePosition(const OBJECT *object, int bone, const vec3_t relative,
                         vec3_t position) const;
    bool GetBonePosition(OBJECT *object, const std::wstring &name, vec3_t position);
    bool GetBonePosition(OBJECT *object, const std::wstring &name, vec3_t relative,
                         vec3_t position);
    std::shared_ptr<const CharacterSocketBinding> BindSocket(OBJECT *object, std::wstring_view name,
                                                             int &bone);

  private:
    struct Admission
    {
        CHARACTER *character;
        std::shared_ptr<CharacterSocketBinding> binding;
    };
    std::unordered_map<const OBJECT *, Admission> characters_;
};

struct _Mesh_t;
namespace Render::Models
{
// Call at decode or cloth admission with the authored grid dimensions.
void PrepareClothGridTopology(_Mesh_t &mesh, int columns, int rows);
} // namespace Render::Models

struct PreparedRigidMesh final
{
    std::uint32_t vertexOffset = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexOffset = 0;
    std::uint32_t indexCount = 0;
    LogicalRenderAssetRef texture;
    bool alphaTest = false;
};

// Owned by one world placement. Draws borrow it until recording completes.
// CPU bones preserve picking/effect inputs; GPU draws need only the transform.
struct RigidObjectPose final
{
    RenderTapeBoneMatrix transform{};
    std::vector<RenderTapeBoneMatrix> bones;
};

class CShadowVolume;

struct SessionShadowVolumeStorage final
{
    CQueue<CShadowVolume *> m_qSV;
};

class CGlobalBitmap;
class SessionKeeper;

typedef struct
{
    short m_nVertexIndex[2];
    short m_nMesh;
    short m_nNormalIndex[2];
} St_Edges;

class CShadowVolume : protected SessionLegacyCalls
{
  public:
    explicit CShadowVolume(SessionKeeper &keeper);
    virtual ~CShadowVolume();

    void Clear(void);

    // a) ���� ������ ������ ���� ����
  protected:
    CGlobalBitmap &Bitmaps;
    short m_nNumVertices; // �� ����
    vec3_t *m_pVertices;  // ����
  protected:
    BOOL GetReadyToCreate(vec3_t ppVertexTransformed[MAX_MESH][MAX_VERTICES], BMD *b, OBJECT *o,
                          bool SkipTga = true); // ����
  public:
    virtual void Create(vec3_t ppVertexTransformed[MAX_MESH][MAX_VERTICES], BMD *b, OBJECT *o,
                        bool SkipTga = true); // ����
    virtual void Destroy(void);               // ����
    void RenderAsFrame(
        void);        // ������ ������ frame ���� �׸���
    void Shade(void); // ���ۿ� �׸��� �׸���

    // b) �߰� ����
  protected:
    vec3_t m_vLight; // ��
    int m_iNumEdge;  // �����ڸ� ����
    std::vector<std::uint8_t> facing_;
    St_Edges *m_pEdges; // �����ڸ�
    void DeterminateSilhouette(short nMesh, vec3_t ppVertexTransformed[MAX_MESH][MAX_VERTICES],
                               short nNumTriangles, const Triangle_t *pTriangles,
                               bool Tga);            // Mesh �� �����ڸ� ����
    void AddEdge(short nV1, short nV2, short nMesh); // �����ڸ� �߰�
    void AddEdgeFast(short nV1, short nV2, short nMesh, int iTriangle, int Edge,
                     const Triangle_t *pTriangles); // �����ڸ� �߰�
    void GenerateSidePolygon(
        vec3_t ppVertexTransformed[MAX_MESH][MAX_VERTICES]); // �����ڸ��� �̿��� ������ ����

    // c) ǥ��
  protected:
    void RenderShadowVolume(
        void); // ������ ������ ������ ������� �׸���
};

//#endif //USE_SHADOWVOLUME

class CameraState;

class CSideHair : CShadowVolume
{
  public:
    explicit CSideHair(SessionKeeper &keeper);
    virtual ~CSideHair();

    void Create(vec3_t ppVertexTransformed[MAX_MESH][MAX_VERTICES], BMD *b,
                const ObjectDrawInput &draw, bool SkipTga = true);

    void Render(vec3_t ppVertexTransformed[MAX_MESH][MAX_VERTICES],
                vec3_t ppLightTransformed[MAX_MESH][MAX_VERTICES]);

  protected:
    using CShadowVolume::Bitmaps;
    void RenderLine(const vec3_t v1, const vec3_t v2, float textureV);
    std::vector<St_Edges> edges_;
    std::uint32_t texturePhase_ = 0;
    CameraState &g_Camera;
};

class SessionKeeper;
class SessionVisualUnit;
class EffectEmissionScope;
struct CharacterDrawInput;
struct CharacterLinkedItemVisual;

// One birth borrows historical parent/item bones and restores the current model
// transform when its factories finish. The prepared display pose stays untouched.
class ItemBirthPose final
{
  public:
    ItemBirthPose(SessionKeeper &keeper, SessionVisualUnit &visual,
                  const CharacterDrawInput &character, const CharacterLinkedItemVisual &entry,
                  const EffectEmissionScope &birth);
    ItemBirthPose(SessionKeeper &keeper, const ObjectDrawInput &draw, int type,
                  const vec34_t *preparedBones, const EffectEmissionScope &birth);
    ~ItemBirthPose();
    ItemBirthPose(const ItemBirthPose &) = delete;
    ItemBirthPose &operator=(const ItemBirthPose &) = delete;
    const vec34_t *Bones() const
    {
        return view_;
    }
    ObjectDrawInput Draw() const;

  private:
    static CharacterDrawInput SampleParent(SessionKeeper &keeper,
                                           const CharacterDrawInput &character, double time,
                                           float fraction,
                                           std::array<vec34_t, MAX_BONES> &characterBones);
    BMD &model_;
    ObjectDrawInput draw_;
    vec3_t savedOrigin_;
    float savedScale_, savedHeight_;
    int savedHead_;
    std::array<vec34_t, MAX_BONES> bones_;
    const vec34_t *view_;
};

class BMD;

// Complete inputs to one skeletal sample. Root scale/position are included only
// when sampling translates them into the matrices; ordinary body skinning does not.
struct AnimationPoseSample final
{
    AnimationPoseSample() = default;
    AnimationPoseSample(const ObjectDrawInput &draw, int head, float height, bool translate,
                        std::uint64_t assetIdentity)
        : asset(assetIdentity), type(draw.type), boneHead(head), bodyHeight(height),
          frame(draw.animationFrame), priorFrame(draw.priorAnimationFrame), action(draw.action),
          priorAction(draw.priorAction), translated(translate), scale(translate ? draw.scale : 1.f)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            angle[axis] = draw.angle[axis];
            headAngle[axis] = draw.headAngle[axis];
            origin[axis] = translate ? draw.position[axis] : 0.f;
        }
    }

    std::uint64_t asset = 0;
    int type = -1;
    int boneHead = -1;
    float bodyHeight = 0.f;
    float frame = 0.f;
    float priorFrame = 0.f;
    unsigned short action = 0;
    unsigned short priorAction = 0;
    bool parent = false;
    bool translated = false;
    float scale = 1.f;
    std::array<float, 3> angle{}, headAngle{}, origin{};
    std::array<float, 12> parentMatrix{};
    int blendBone = -1;
    float blendWeight = 0.f;

    void SetParent(const float (&matrix)[3][4])
    {
        parent = translated = true;
        scale = 1.f;
        origin = {};
        std::copy_n(&matrix[0][0], 12, parentMatrix.begin());
    }

    auto operator<=>(const AnimationPoseSample &) const = default;
    void Evaluate(BMD &model, vec34_t *output) const;
    void EvaluateAtFrame(BMD &model, vec34_t *output) const;
    const vec34_t *EvaluateAtTime(BMD &model, const OBJECT &owner, double frameTime, float fraction,
                                  vec34_t *output, const vec34_t *prepared = nullptr) const;
    void SampleBonePosition(BMD &model, const OBJECT &owner, int bone, const vec3_t offset,
                            double frameTime, float fraction, vec3_t position) const;

  private:
    void Evaluate(BMD &model, vec34_t *output, bool adjacentKeys) const;
};

// A pose used by this recording cannot be overwritten until the next frame.
// Prepared character poses are borrowed directly by callers. Actual draw variants
// use these reusable arrays, without a general pose dictionary on every draw.
class SessionDrawPoseStorage final
{
  public:
    void BeginFrame() noexcept
    {
        used_ = 0;
    }
    const vec34_t *Sample(BMD &model, const AnimationPoseSample &sample);
    std::size_t AllocationCount() const noexcept
    {
        return allocations_;
    }
    std::size_t RetainedPoseCount() const noexcept
    {
        return poses_.size();
    }
    std::size_t RetainedBoneCount() const noexcept
    {
        return retainedBones_;
    }

    vec34_t *Allocate(std::size_t bones)
    {
        if (used_ == poses_.size())
            poses_.emplace_back();
        auto &pose = poses_[used_++];
        EnsureCapacity(pose, bones);
        return pose.matrices.get();
    }

  private:
    struct Pose
    {
        std::unique_ptr<vec34_t[]> matrices;
        std::size_t capacity = 0;
    };
    void EnsureCapacity(Pose &pose, std::size_t bones)
    {
        if (pose.capacity >= bones)
            return;
        pose.matrices = std::make_unique<vec34_t[]>(bones);
        retainedBones_ += bones - pose.capacity;
        pose.capacity = bones;
        ++allocations_;
    }
    std::vector<Pose> poses_;
    std::size_t used_ = 0;
    std::size_t allocations_ = 0;
    std::size_t retainedBones_ = 0;
};
