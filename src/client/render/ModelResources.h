#pragma once
#include "data/CharacterData.h"
#include "render/FrameTape.h"
#include "render/ModelGeometry.h"
#include "session/SessionRuntime.h"
#include "support/CoreMath.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#define NODE_MAX 200
#define TIME_MAX 100
#define TRIANGLE_MAX 15000
#define VERTEX_MAX 10000
#define TEXTURE_MAX 100
#define MESH_MAX 100
#define REFERENCE_FRAME 0
#define SKELETAL_ANIMATION 1
#define MODEL_TYPE_CHARM_MIXWING MODEL_HELPER
#define MAX_MODEL_MONSTER 400
#define MODEL_ITEM_COMMON_NUM 2
#define MODEL_ITEM_COMMONCNT_RAGEFIGHTER 4

class TextureScript;
class CMapManager;
class SharedAllocationCounter;
class SessionKeeper;
struct BmdSharedAsset;
struct RigidObjectPose;
struct CharacterClothVisual;

#define RENDER_COLOR 0x00000001
#define RENDER_TEXTURE 0x00000002
#define RENDER_CHROME 0x00000004
#define RENDER_METAL 0x00000008
#define RENDER_LIGHTMAP 0x00000010
#define RENDER_SHADOWMAP 0x00000020
#define RENDER_BRIGHT 0x00000040
#define RENDER_DARK 0x00000080
#define RENDER_EXTRA 0x00000100
#define RENDER_CHROME2 0x00000200
#define RENDER_WAVE 0x00000400
#define RENDER_CHROME3 0x00000800
#define RENDER_CHROME4 0x00001000
#define RENDER_NODEPTH 0x00002000
#define RENDER_CHROME5 0x00004000
#define RENDER_OIL 0x00008000
#define RENDER_CHROME6 0x00010000
#define RENDER_CHROME7 0x00020000
#define RENDER_DOPPELGANGER 0x00040000
#define RENDER_WAVE_EXT 0x10000000
#define RENDER_BYSCRIPT 0x80000000
#define RNDEXT_WAVE 1
#define RNDEXT_OIL 2
#define RNDEXT_RISE 4

#define MAX_MONSTER_SOUND 10

typedef struct
{
    vec3_t Position;
    vec3_t Color;
    float Range;
} Light_t;

typedef struct
{
    vec3_t *Position;
    vec3_t *Rotation;
    vec4_t *Quaternion;
} BoneMatrix_t;

typedef struct
{
    char Name[32];
    short Parent;
    char Dummy;
    BoneMatrix_t *BoneMatrixes;
    char BoundingBox;
    vec3_t BoundingVertices[8];
} Bone_t;

typedef struct
{
    char FileName[32];
} Texture_t;

typedef struct
{
    unsigned char Width;
    unsigned char Height;
    unsigned char *Buffer;
} Bitmap_t;

typedef struct
{
    short Node;
    vec3_t Position;
} Vertex_t;

typedef struct
{
    short Node;
    vec3_t Normal;
    short BindVertex;
} Normal_t;

typedef struct
{
    float TexCoordU;
    float TexCoordV;
} TexCoord_t;

typedef struct
{
    BYTE m_Colors[3]; //0~255 RGB
} VertexColor_t;

typedef struct Triangle_t
{
    char Polygon;
    short VertexIndex[4];
    short NormalIndex[4];
    short TexCoordIndex[4];
    short EdgeTriangleIndex[4];
    bool Front;
} Triangle_t;

struct BmdRenderTapeVertexSource final
{
    short vertexIndex;
    short normalIndex;
    short texCoordIndex;
};

typedef struct
{
    char Polygon;
    short VertexIndex[4];
    short NormalIndex[4];
    short TexCoordIndex[4];
    TexCoord_t LightMapCoord[4]; //ver1.2
    short LightMapIndexes;       //ver1.2
} Triangle_t2;

typedef struct
{
    bool Loop;
    float PlaySpeed;
    short NumAnimationKeys;
    bool LockPositions;
    vec3_t *Positions;
} Action_t;

typedef struct _Triangle_t3 : public Triangle_t
{
    short m_ivIndexAdditional[4];
} Triangle_t3;

typedef struct _Mesh_t
{
    bool NoneBlendMesh;
    short Texture;
    short NumVertices;
    short NumNormals;
    short NumTexCoords;
    short NumVertexColors; //ver1.3
    short NumTriangles;
    int NumCommandBytes; //ver1.1
    Vertex_t *Vertices;
    Normal_t *Normals;
    TexCoord_t *TexCoords;
    VertexColor_t *VertexColors; //ver1.3
    Triangle_t *Triangles;
    unsigned char *Commands; //ver1.1

    // The portable tape uses indexed vertices. This topology is derived once
    // from immutable mesh corners, while animated attributes remain per-frame.
    std::shared_ptr<const std::vector<BmdRenderTapeVertexSource>> RenderTapeVertexSources;
    std::shared_ptr<const std::vector<std::uint32_t>> RenderTapeIndices;
    std::uint32_t RenderTapeVertexOffset = 0;
    std::uint32_t RenderTapeIndexOffset = 0;

    TextureScript *m_csTScript;

    _Mesh_t()
    {
        Vertices = NULL;
        TexCoords = NULL;
        VertexColors = NULL;
        Normals = NULL;
        Triangles = NULL;
        Commands = NULL;
        m_csTScript = NULL;

        NumVertices = NumNormals = NumTexCoords = NumVertexColors = NumTriangles = 0;
    }
} Mesh_t;

class BMD : protected SessionLegacyCalls
{
  public:
    char Name[64];
    char Version;
    short NumBones;
    short NumMeshs;
    short NumActions;
    Mesh_t *Meshs;
    Bone_t *Bones;
    Action_t *Actions;
    Texture_t *Textures;
    unsigned int *IndexTexture;

    short NumLightMaps;  //ver1.2
    short IndexLightMap; //ver1.2
    Bitmap_t *LightMaps; //ver1.2

    bool LightEnable;
    bool ContrastEnable;
    vec3_t BodyLight;
    int BoneHead;

    int BoneFoot[4];
    float BodyScale;
    vec3_t BodyOrigin;
    vec3_t BodyAngle;
    float BodyHeight;
    char StreamMesh;
    char Skin;
    bool HideSkin;
    float Velocity;
    unsigned short CurrentAction;
    unsigned short PriorAction;
    float CurrentAnimation;
    short CurrentAnimationFrame;
    short Sounds[MAX_MONSTER_SOUND];
    float fTransformedSize;

    unsigned int m_iBMDSeqID;
    bool bLightMap;
    bool bOffLight;
    char iBillType;

    bool m_bCompletedAlloc;

    explicit BMD(SessionKeeper &sessionKeeper) noexcept;

    ~BMD();
    std::uint64_t AnimationEvaluationCount() const noexcept
    {
        return animationEvaluations_;
    }

  private:
    std::uint64_t animationEvaluations_ = 0;

  public:
    //utility
    void Init(bool Dummy);
    bool Open2(const wchar_t *DirName, const wchar_t *FileName, bool bReAlloc = true);
    // Owner-side preparation retains immutable data without replacing this model.
    std::shared_ptr<BmdSharedAsset> PrepareSharedAsset(
        const std::filesystem::path &path, const wchar_t *&reason, int requiredActions = 0,
        int requiredBones = 0, int requiredMeshes = 0, bool rigidGeometry = false) noexcept;
    std::uint64_t PoseAssetIdentity() const noexcept;
    bool BindSharedAsset(const std::shared_ptr<BmdSharedAsset> &asset) noexcept;
    void PrepareRigidInstanceMeshes();
    void RenderRigidInstances(std::span<const RenderTapeRigidInstance> instances,
                              bool bakedGeometry = true);
    bool Save2(wchar_t *DirName, wchar_t *FileName);
    void Release();
    void CreateBoundingBox();

    bool PlayAnimation(float *AnimationFrame, float *PriorAnimationFrame,
                       unsigned short *PriorAction, float Speed, vec3_t Origin, vec3_t Angle);
    bool PlayAnimation(float *AnimationFrame, float *PriorAnimationFrame,
                       unsigned short *PriorAction, float Speed, vec3_t Origin, vec3_t Angle,
                       float referenceFrames);
    void Animation(float (*BoneTransform)[3][4], float AnimationFrame, float PriorAnimationFrame,
                   unsigned short PriorAction, const vec3_t Angle, const vec3_t HeadAngle,
                   bool Parent = false, bool Translate = true,
                   const float (*ExtParentMatrix)[4] = nullptr, short CurrentAction = -1);
    // Sample a historical phase with adjacent keys; preserve explicit cross-action blending.
    void AnimationAtFrame(float (*BoneTransform)[3][4], float AnimationFrame,
                          float PriorAnimationFrame, unsigned short PriorAction, const vec3_t Angle,
                          const vec3_t HeadAngle, bool Parent = false, bool Translate = true,
                          const float (*ExtParentMatrix)[4] = nullptr, short CurrentAction = -1);
    void InterpolationTrans(float (*Mat1)[4], float (*TransMat2)[4], float _Scale);
    void Transform(const float (*BoneMatrix)[3][4], const vec3_t BoundingBoxMin,
                   const vec3_t BoundingBoxMax, OBB_t *OBB, bool Translate = false,
                   float _Scale = 0.0f, bool stablePose = false,
                   const RigidObjectPose *rigidPose = nullptr);
    void EnsureCpuTransforms() noexcept;
    void TransformByObjectBone(vec3_t vResultPosition, const ObjectDrawInput &draw, int iBoneNumber,
                               const vec3_t vRelativePosition = NULL);
    void TransformByBoneMatrix(vec3_t vResultPosition, const float (*BoneMatrix)[4],
                               const vec3_t vWorldPosition = NULL,
                               const vec3_t vRelativePosition = NULL);
    void TransformPosition(const float (*Matrix)[4], const vec3_t Position, vec3_t WorldPosition,
                           bool Translate = false);
    void RotationPosition(float (*Matrix)[4], vec3_t Position, vec3_t WorldPosition);

  public:
    void AnimationTransformWithAttachHighModel(OBJECT *oHighHierarchyModel,
                                               BMD *bmdHighHierarchyModel,
                                               int iBoneNumberHighHierarchyModel,
                                               vec3_t &vOutPosHighHiearachyModelBone,
                                               vec3_t *arrOutSetfAllBonePositions);

    void AnimationTransformOnlySelf(vec3_t *arrOutSetfAllBonePositions, const vec3_t &v3Angle,
                                    const vec3_t &v3Position, const float &fScale,
                                    OBJECT *oRefAnimation = NULL, const float fFrameArea = -1.0f,
                                    const float fWeight = -1.0f);

    void Lighting(float *, Light_t *, vec3_t, vec3_t);
    void Chrome(float *, int, vec3_t);

    // A retained cloth mesh supplies its own topology while using the model's materials.
    class MeshDrawScope final
    {
      public:
        MeshDrawScope(BMD &model, int index, const Mesh_t *mesh) noexcept;
        ~MeshDrawScope();
        MeshDrawScope(const MeshDrawScope &) = delete;
        MeshDrawScope &operator=(const MeshDrawScope &) = delete;

      private:
        BMD &model_;
        int previousIndex_;
        const Mesh_t *previousMesh_;
    };

    //render
    void RenderBone(float (*BoneTransform)[3][4]);
    void RenderObjectBoundingBox();
    void BeginRender(float);
    void EndRender();

    void RenderMesh(int meshIndex, int renderFlags, float alpha = 1.f, int blendMeshIndex = -1,
                    float blendMeshAlpha = 1.f, float blendMeshTextureCoordU = 0.f,
                    float blendMeshTextureCoordV = 0.f, int textureIndex = -1);
    void BeginRenderCoinHeap();
    int AddToCoinHeap(int coinIndex, int target_vertex_index);
    void EndRenderCoinHeap(int coinCount);

    void RenderMeshAlternative(int iRndExtFlag, int iParam, int i, int RenderFlag,
                               float Alpha = 1.f, int BlendMesh = -1, float BlendMeshLight = 1.f,
                               float BlendMeshTexCoordU = 0.f, float BlendMeshTexCoordV = 0.f,
                               int Texture = -1);
    void RenderBody(int RenderFlag, float Alpha = 1.f, int BlendMesh = -1,
                    float BlendMeshLight = 1.f, float BlendMeshTexCoordU = 0.f,
                    float BlendMeshTexCoordV = 0.f, int HiddenMesh = -1, int Texture = -1);
    void RenderBodyAlternative(int iRndExtFlag, int iParam, int RenderFlag, float Alpha = 1.f,
                               int BlendMesh = -1, float BlendMeshLight = 1.f,
                               float BlendMeshTexCoordU = 0.f, float BlendMeshTexCoordV = 0.f,
                               int HiddenMesh = -1, int Texture = -1);
    void RenderMeshTranslate(int i, int RenderFlag, float Alpha = 1.f, int BlendMesh = -1,
                             float BlendMeshLight = 1.f, float BlendMeshTexCoordU = 0.f,
                             float BlendMeshTexCoordV = 0.f, int Texture = -1);
    void RenderBodyTranslate(int RenderFlag, float Alpha = 1.f, int BlendMesh = -1,
                             float BlendMeshLight = 1.f, float BlendMeshTexCoordU = 0.f,
                             float BlendMeshTexCoordV = 0.f, int HiddenMesh = -1, int Texture = -1);
    void RenderBodyShadow(int blendMesh = -1, int hiddenMesh = -1, int startMeshNumber = -1,
                          int endMeshNumber = -1, const CharacterClothVisual *clothes = nullptr);

    void SetBodyLight(vec3_t right)
    {
        VectorCopy(right, BodyLight);
    }

    bool LightMapEnable;
    bool CollisionDetectLineToMesh(vec3_t, vec3_t, bool Collision = true, int Mesh = -1,
                                   int Triangle = -1);
    void CreateLightMapSurface(Light_t *, Mesh_t *, int, int, int, int, int, int, vec3_t, vec3_t,
                               int);
    void CreateLightMaps();
    void ReleaseLightMaps();
    std::size_t RenderTapeStorageBytes(SharedAllocationCounter &allocations) const noexcept;
    std::size_t MutableOverlayStorageBytes() const noexcept
    {
        const auto preparedBytes = rigidInstanceMeshes_.capacity() * sizeof(PreparedRigidMesh);
        if (sharedAsset_ == nullptr)
            return preparedBytes;
        const std::size_t meshCount = NumMeshs > 0 ? NumMeshs : 1;
        const std::size_t actionCount = NumActions > 0 ? NumActions : 1;
        return meshCount * (sizeof(Mesh_t) + sizeof(unsigned int)) +
               actionCount * sizeof(Action_t) + preparedBytes;
    }

    //#ifdef USE_SHADOWVOLUME
    void FindNearTriangle(void);

    void FindTriangleForEdge(int iMesh, int iTri, int iIndex11);
    //#endif //USE_SHADOWVOLUME
  private:
    void AnimationImpl(float (*BoneTransform)[3][4], float AnimationFrame,
                       float PriorAnimationFrame, unsigned short PriorAction, const vec3_t Angle,
                       const vec3_t HeadAngle, bool Parent, bool Translate,
                       const float (*ExtParentMatrix)[4], short CurrentAction, bool adjacentKeys);
    void ReleaseTextures();
    friend class SessionRenderUnit;
    friend class SessionVisualUnit;

    CMapManager &gMapManager;
    float &FPS_ANIMATION_FACTOR;
    double &WorldTime;
    float parentMatrix_[3][4]{};
    LogicalGeometryAssetLease renderTapeGeometry_;
    LogicalGeometryAssetLease renderTapeRigidGeometry_;
    std::vector<PreparedRigidMesh> rigidInstanceMeshes_;
    bool batchRigidPlacements_ = true;
    std::uint64_t renderTapeRigidGeometryRevision_ = 0;
    const LogicalGeometryAssetLease &GeometryForDraw() const noexcept
    {
        return renderTapeTransform_.rigid ? renderTapeRigidGeometry_ : renderTapeGeometry_;
    }
    void InvalidateCharacterPoses() const noexcept;
    std::shared_ptr<BmdSharedAsset> sharedAsset_;
    RenderTapeBmdConstants renderTapeTransform_;
    std::uint64_t renderTapeGeometryRevision_ = 0;
    bool renderTapePaletteReady_ = false;
    const float (*cpuBoneMatrix_)[3][4] = nullptr;
    std::array<float, 3> cpuLightPosition_{};
    std::array<float, 3> cpuBodyOrigin_{};
    float cpuPositionScale_ = 0.0F;
    float cpuBoneScale_ = 1.0F;
    float cpuBodyScale_ = 1.0F;
    bool cpuTranslate_ = false;
    bool cpuLightEnabled_ = false;
    bool cpuTransformsReady_ = false;

    BMD(const BMD &b);

    bool BuildRenderTapeGeometry(bool decoded = false) noexcept;
    std::shared_ptr<BmdSharedAsset> BuildSharedAsset(const std::filesystem::path &path,
                                                     const wchar_t *&reason, int requiredActions,
                                                     int requiredBones, int requiredMeshes,
                                                     bool rigidGeometry);
    bool PrepareRenderTapeGeometry(bool rigid = false) noexcept;
    void BakeRigidGeometry(BmdSharedAsset &asset);
    bool PrepareRenderTapePalette(const float (*boneMatrix)[3][4], bool stablePose) noexcept;
    RenderBmdUvMode SelectRenderTapeUvMode(int renderFlags) const noexcept;
    int drawMeshIndex_ = -1;
    const Mesh_t *drawMesh_ = nullptr;
    const Mesh_t *MeshForDraw(int index) const noexcept
    {
        return drawMesh_ && index == drawMeshIndex_ ? drawMesh_ : &Meshs[index];
    }
    void TransformCpuMeshes(float *boundingMin, float *boundingMax) noexcept;
    void PrepareTransformLighting(vec3_t lightPosition);

    void AddClothesShadowTriangles(const CharacterClothVisual &clothes, float sx, float sy) const;
    RenderTapeBmdConstants ShadowConstants(float sx, float sy,
                                           LogicalGeometryAssetRef terrain) const;
    void AddMeshShadowTriangles(int blendMesh, int hiddenMesh, int startMesh, int endMesh, float sx,
                                float sy);
};

struct BmdSharedAsset final
{
    char name[64]{};
    char version = 0;
    short boneCount = 0;
    short meshCount = 0;
    short actionCount = 0;
    Mesh_t *meshes = nullptr;
    Bone_t *bones = nullptr;
    Action_t *actions = nullptr;
    Texture_t *textures = nullptr;
    std::shared_ptr<const std::vector<RenderTapeVertex>> vertices;
    std::shared_ptr<const std::vector<RenderTapeVertex>> rigidVertices;
    std::shared_ptr<const std::vector<std::uint32_t>> indices;
    // Empty for animated assets. Prepared once from all authored actions/keys.
    std::vector<RenderTapeBoneMatrix> invariantLocalPose;

    std::size_t droppedExportTriangles = 0;

    enum class Error
    {
        None,
        Open,
        Read,
        Size,
        Header,
        Values,
        Allocation
    };
    static std::shared_ptr<BmdSharedAsset> Load(const std::filesystem::path &path,
                                                Error &error) noexcept;
    static const wchar_t *ErrorText(Error error) noexcept;
    ~BmdSharedAsset();
    BmdSharedAsset();
    const std::uint64_t poseIdentity;
    BmdSharedAsset(const BmdSharedAsset &) = delete;
    BmdSharedAsset &operator=(const BmdSharedAsset &) = delete;
};

// Minimum extents consumed by model setup and named map effects. Checked at load,
// before these trusted arrays are published to gameplay or rendering.
struct ModelResourceRequirements final
{
    static int Actions(int slot) noexcept;
    static int Bones(int slot) noexcept;
    static int Meshes(int slot) noexcept;
    static int HeadBone(int slot) noexcept;
    static int RequiredBones(int slot, bool geometry) noexcept;
};

typedef struct
{
    char Name[32];
    short Parent;
} Node_t;

typedef struct
{
    vec3_t Position[NODE_MAX];
    vec3_t Rotation[NODE_MAX];
} Skeleton_t;

typedef struct
{
    short NodeNum;
    Node_t Node[NODE_MAX];
    Skeleton_t Skeleton;
} NodeGroup_t;

typedef struct
{
    short TimeNum;
    Skeleton_t Skeleton[TIME_MAX];
} SkeletonGroup_t;

typedef struct
{
    short Node;
    vec3_t Position;
    vec3_t Normal;
    float TexCoordU;
    float TexCoordV;
} SMDVertex_t;

typedef struct
{
    short TriangleNum;
    char TextureName[TRIANGLE_MAX][32];
    SMDVertex_t Vertex[TRIANGLE_MAX][3];
} TriangleGroup_t;

typedef struct
{
    short Texture;
    short VertexNum;
    short NormalNum;
    short TexCoordNum;
    short TriangleNum;
    Vertex_t Vertex[VERTEX_MAX];
    Normal_t Normal[VERTEX_MAX];
    TexCoord_t TexCoord[VERTEX_MAX];

    char Polygon[TRIANGLE_MAX];
    short VertexList[TRIANGLE_MAX][4];
    short NormalList[TRIANGLE_MAX][4];
    short TexCoordList[TRIANGLE_MAX][4];
} SMDMesh_t;

typedef struct
{
    short MeshNum;
    Texture_t Texture[TEXTURE_MAX];
    SMDMesh_t Mesh[MESH_MAX];
} SMDMeshGroup_t;

void OpenBMD(int ID, wchar_t *FileName);
void SaveBMD(int ID, wchar_t *FileName);

class BMD;
class CErrorReport;
class SessionKeeper;

class SessionModelLoader final : protected SessionLegacyCalls
{
  public:
    explicit SessionModelLoader(SessionKeeper &keeper) noexcept;

    SessionModelLoader(const SessionModelLoader &) = delete;
    SessionModelLoader &operator=(const SessionModelLoader &) = delete;
    SessionModelLoader(SessionModelLoader &&) = delete;
    SessionModelLoader &operator=(SessionModelLoader &&) = delete;

    bool AccessModel(int type, const wchar_t *directory, const wchar_t *fileName, int index = -1);
    bool OpenTexture(int model, const wchar_t *subFolder,
                     LegacyTextureWrap wrap = LegacyTextureWrap::Repeat,
                     LegacyTextureFilter type = LegacyTextureFilter::Nearest, bool check = true);

  private:
    std::uint32_t LoadTexture(const char *filename, const wchar_t *subFolder,
                              LegacyTextureWrap wrap, LegacyTextureFilter filter);
    SessionModelPool &Models;
    CErrorReport &g_ErrorReport;
};

class BMD;
class SharedAllocationCounter;
class SessionKeeper;

class SessionModelPool final
{
  public:
    explicit SessionModelPool(SessionKeeper &keeper) noexcept;
    ~SessionModelPool();

    SessionModelPool(const SessionModelPool &) = delete;
    SessionModelPool &operator=(const SessionModelPool &) = delete;
    SessionModelPool(SessionModelPool &&) = delete;
    SessionModelPool &operator=(SessionModelPool &&) = delete;

    bool Allocate();
    BMD &operator[](int model);
    BMD *Find(int model) noexcept;
    bool IsAllocated() const noexcept;
    std::size_t LoadedModelCount() const noexcept;
    std::size_t StorageBytes() const noexcept;
    std::size_t RenderTapeStorageBytes(SharedAllocationCounter &allocations) const noexcept;

  private:
    void Reset() noexcept;

    SessionKeeper &sessionKeeper_;
    std::vector<std::unique_ptr<BMD>> models_;
    std::size_t loadedModelCount_ = 0;
};

struct SessionBmdStorage final
{
    short BoundingVertices[MAX_BONES]{};
    vec3_t BoundingMin[MAX_BONES]{};
    vec3_t BoundingMax[MAX_BONES]{};
    float BoneTransform[MAX_BONES][3][4]{};
    vec3_t VertexTransform[MAX_MESH][MAX_VERTICES]{};
    vec3_t NormalTransform[MAX_MESH][MAX_VERTICES]{};
    float IntensityTransform[MAX_MESH][MAX_VERTICES]{};
    vec3_t LightTransform[MAX_MESH][MAX_VERTICES]{};
    vec3_t RenderArrayVertices[MAX_VERTICES * 3]{};
    vec4_t RenderArrayColors[MAX_VERTICES * 3]{};
    vec2_t RenderArrayTexCoords[MAX_VERTICES * 3]{};
    float ParentMatrix[3][4]{};
    vec3_t LightVector{0.f, -0.1f, -0.8f};
    vec3_t LightVector2{0.f, -0.5f, -0.8f};
    bool HighLight = true;
    float BoneScale = 1.f;
    vec3_t g_vright{};
    int g_smodels_total = 1;
    float g_chrome[MAX_VERTICES][2]{};
    int g_chromeage[MAX_BONES]{};
    vec3_t g_chromeup[MAX_BONES]{};
    vec3_t g_chromeright[MAX_BONES]{};
    float SubPixel = 16.f;
};

enum SMDToken : int
{
    NAME,
    NUMBER,
    END,
    COMMAND = '#',
    LBRACKET = '{',
    RBRACKET = '}',
    COMMA = ',',
    SEMICOLON = ';',
    SMD_ERROR
};

struct SessionSmdStorage
{
    struct BoneFixupValue
    {
        float m[3][4];
        float im[3][4];
        vec3_t WorldOrg;
    };

    SessionSmdStorage() noexcept
    {
        std::memset(this, 0, sizeof(*this));
    }

    NodeGroup_t NodeGroup;
    SkeletonGroup_t SkeletonGroup;
    TriangleGroup_t TriangleGroup;
    SMDMeshGroup_t MeshGroup;
    FILE *SMDFile;
    float TokenNumber;
    char TokenString[256];
    SMDToken CurrentToken;
    BoneFixupValue BoneFixup[NODE_MAX];
};

// Explicit models used by concrete map behavior, independent of placement slots.
struct WorldModelDependency final
{
    enum class Geometry
    {
        Drawable,
        PoseOnly
    };
    enum class Lifetime
    {
        WorldVisit,
        SessionBasic
    };
    int slot;
    const wchar_t *path; // Relative to Data.
    int requiredActions = 0;
    const wchar_t *textureDirectory = nullptr; // Relative to Data; otherwise the model directory.
    Lifetime lifetime = Lifetime::WorldVisit;
    Geometry geometry = Geometry::Drawable;
    static std::filesystem::path SharedTexturePath(const std::filesystem::path &model,
                                                   const wchar_t *texture);
    static std::span<const WorldModelDependency> For(int behaviorMap) noexcept;
    static std::span<const int> ForWorldSpawns(int behaviorMap) noexcept;
    static std::span<const WorldModelDependency> ForMonster(int monsterModel) noexcept;
};

// Authored primary slot names and proven placement markers; global slots are separate.
struct WorldPrimaryModel final
{
    static std::filesystem::path Path(const MapDefinition &map, int slot);
    static int RequiredBones(int behaviorMap, int slot) noexcept;
    static int RequiredActions(int behaviorMap, int slot) noexcept;
    static bool RequiresGeometry(int behaviorMap, int slot) noexcept;
    static bool UsesRigidGeometry(int behaviorMap, int slot) noexcept;
    static bool AllowsMissingGeometry(int assetSet, int slot) noexcept;
    static bool IsLegacyUnboundPlacement(int assetSet, int slot) noexcept;
};

enum
{
    //nature
    MODEL_WORLD_OBJECT = 0,
    MODEL_TREE01 = 0,
    MODEL_GRASS01 = 20,
    MODEL_STONE01 = 30,
    //outside
    MODEL_STONE_STATUE01 = 40,
    MODEL_STONE_STATUE02,
    MODEL_STONE_STATUE03,
    MODEL_STEEL_STATUE,
    MODEL_TOMB01,
    MODEL_TOMB02,
    MODEL_TOMB03,
    MODEL_FIRE_LIGHT01 = 50,
    MODEL_FIRE_LIGHT02,
    MODEL_BONFIRE,
    MODEL_DUNGEON_GATE = 55,
    MODEL_MERCHANT_ANIMAL01,
    MODEL_MERCHANT_ANIMAL02,
    MODEL_TREASURE_DRUM,
    MODEL_TREASURE_CHEST,
    MODEL_SHIP,
    //wall
    MODEL_STEEL_WALL01 = 65,
    MODEL_STEEL_WALL02,
    MODEL_STEEL_WALL03,
    MODEL_STEEL_DOOR,
    MODEL_STONE_WALL01,
    MODEL_STONE_WALL02,
    MODEL_STONE_WALL03,
    MODEL_STONE_WALL04,
    MODEL_STONE_WALL05,
    MODEL_STONE_WALL06,
    MODEL_MU_WALL01,
    MODEL_MU_WALL02,
    MODEL_MU_WALL03,
    MODEL_MU_WALL04,
    MODEL_BRIDGE = 80,
    MODEL_FENCE01,
    MODEL_FENCE02,
    MODEL_FENCE03,
    MODEL_FENCE04,
    MODEL_BRIDGE_STONE,
    //town
    MODEL_STREET_LIGHT = 90,
    MODEL_CANNON01,
    MODEL_CANNON02,
    MODEL_CANNON03,
    MODEL_CURTAIN = 95,
    MODEL_SIGN01,
    MODEL_SIGN02,
    MODEL_CARRIAGE01,
    MODEL_CARRIAGE02,
    MODEL_CARRIAGE03,
    MODEL_CARRIAGE04,
    MODEL_STRAW01,
    MODEL_STRAW02,
    MODEL_WATERSPOUT = 105,
    MODEL_WELL01,
    MODEL_WELL02,
    MODEL_WELL03,
    MODEL_WELL04,
    MODEL_HANGING,
    MODEL_STAIR,
    //house
    MODEL_HOUSE01 = 115,
    MODEL_HOUSE02,
    MODEL_HOUSE03,
    MODEL_HOUSE04,
    MODEL_HOUSE05,
    MODEL_TENT,
    MODEL_HOUSE_WALL01,
    MODEL_HOUSE_WALL02,
    MODEL_HOUSE_WALL03,
    MODEL_HOUSE_WALL04,
    MODEL_HOUSE_WALL05,
    MODEL_HOUSE_WALL06,
    MODEL_HOUSE_ETC01,
    MODEL_HOUSE_ETC02,
    MODEL_HOUSE_ETC03,
    MODEL_LIGHT01 = 130,
    MODEL_LIGHT02,
    MODEL_LIGHT03,
    MODEL_POSE_BOX,
    //bar
    MODEL_FURNITURE01 = 140,
    MODEL_FURNITURE02,
    MODEL_FURNITURE03,
    MODEL_FURNITURE04,
    MODEL_FURNITURE05,
    MODEL_FURNITURE06,
    MODEL_FURNITURE07,
    MODEL_CANDLE = 150,
    MODEL_BEER01,
    MODEL_BEER02,
    MODEL_BEER03,
    MODEL_HERO_CHAIR,
    MODEL_HERO_GUARD,
    MODEL_MURDERER_DOG,
    MAX_WORLD_OBJECTS = 160,
    //logo
    MODEL_LOGO = MAX_WORLD_OBJECTS,
    MODEL_WAVEBYSHIP,
    MODEL_MUGAME,
    MODEL_LOGOSUN,
    MODEL_CARD,
    MODEL_FACE = MODEL_CARD + 4,
    //animal
    MODEL_BIRD01 = MODEL_FACE + MAX_CLASS,

    MODEL_BUTTERFLY01,
    MODEL_BAT01,
    MODEL_RAT01,
    MODEL_BUG01,
    MODEL_FISH01,
    MODEL_GHOST,
    MODEL_DRAGON,
    MODEL_LOGIN_WARP,
    MODEL_CLOUD = MODEL_FISH01 + 9,
    MODEL_CROW,
    MODEL_SHINE,
    MODEL_EAGLE,
    MODEL_MAP_TORNADO,
    MODEL_GM_CHARACTER,
    MODEL_XMAS_EVENT_CHA_SSANTA,
    MODEL_XMAS_EVENT_CHA_SNOWMAN,
    MODEL_XMAS_EVENT_CHA_DEER,
    MODEL_XMAS_EVENT_CHANGE_GIRL,
    MODEL_XMAS_EVENT_EARRING,
    MODEL_XMAS_EVENT_ICEHEART,
    MODEL_XMAS_EVENT_BOX,
    MODEL_XMAS_EVENT_CANDY,
    MODEL_XMAS_EVENT_TREE,
    MODEL_XMAS_EVENT_SOCKS,
    MODEL_NEWYEARSDAY_EVENT_BEKSULKI,
    MODEL_NEWYEARSDAY_EVENT_CANDY,
    MODEL_NEWYEARSDAY_EVENT_HOTPEPPER_GREEN,
    MODEL_NEWYEARSDAY_EVENT_HOTPEPPER_RED,
    MODEL_NEWYEARSDAY_EVENT_PIG,
    MODEL_NEWYEARSDAY_EVENT_YUT,
    MODEL_NEWYEARSDAY_EVENT_MONEY,
    MODEL_MOONHARVEST_GAM,
    MODEL_MOONHARVEST_SONGPUEN1,
    MODEL_MOONHARVEST_SONGPUEN2,
    MODEL_MOONHARVEST_MOON,
    MODEL_LIGHTNING_ORB,
    MODEL_CHAIN_LIGHTNING,
    MODEL_ALICE_BUFFSKILL_EFFECT,
    MODEL_ALICE_BUFFSKILL_EFFECT2,
    MODEL_ALICE_DRAIN_LIFE,
    MODEL_RAKLION_BOSS_CRACKEFFECT,
    MODEL_RAKLION_BOSS_MAGIC,
    MODEL_LIGHTNING_SHOCK,
    MODEL_MOVE_TARGETPOSITION_EFFECT,

    //skill
    MODEL_SKILL_BEGIN,
    MODEL_DARK_HORSE,
    MODEL_DARK_SPIRIT,
    MODEL_ICE,
    MODEL_FIRE,
    MODEL_POISON,
    MODEL_WOLF,
    MODEL_WARCRAFT,
    MODEL_UNICON,
    MODEL_BLOOD,
    MODEL_STONE1,
    MODEL_STONE2,
    MODEL_ICE_SMALL,
    MODEL_CIRCLE,
    MODEL_CIRCLE_LIGHT,
    MODEL_MAGIC1,
    MODEL_MAGIC2,
    MODEL_STORM,
    MODEL_LASER,
    MODEL_SKELETON1,
    MODEL_SKELETON2,
    MODEL_SKELETON3,
    MODEL_SKELETON_PCBANG,
    MODEL_HALLOWEEN,
    MODEL_HALLOWEEN_EX,
    MODEL_HALLOWEEN_CANDY_BLUE,
    MODEL_HALLOWEEN_CANDY_ORANGE,
    MODEL_HALLOWEEN_CANDY_RED,
    MODEL_HALLOWEEN_CANDY_YELLOW,
    MODEL_HALLOWEEN_CANDY_HOBAK,
    MODEL_HALLOWEEN_CANDY_STAR,
    MODEL_CURSEDTEMPLE_ALLIED_PLAYER,
    MODEL_CURSEDTEMPLE_ILLUSION_PLAYER,
    MODEL_WOOSISTONE,
    MODEL_SAW,
    MODEL_BONE1,
    MODEL_BONE2,
    MODEL_SNOW1,
    MODEL_SNOW2,
    MODEL_SNOW3,
    MODEL_DUNGEON_STONE01,
    MODEL_ARROW,
    MODEL_ARROW_STEEL,
    MODEL_ARROW_THUNDER,
    MODEL_ARROW_LASER,
    MODEL_ARROW_V,
    MODEL_ARROW_SAW,
    MODEL_ARROW_NATURE,
    MODEL_ARROW_BOMB,
    MODEL_ARROW_WING,
    MODEL_PROTECT,
    MODEL_BIG_STONE1,
    MODEL_BIG_STONE2,
    MODEL_BIG_METEO1,
    MODEL_BIG_METEO2,
    MODEL_BIG_METEO3,
    MODEL_METEO1,
    MODEL_METEO2,
    MODEL_MAGIC_CIRCLE1,
    MODEL_ARROW_TANKER,
    MODEL_ARROW_TANKER_HIT,
    MODEL_BOSS_HEAD,
    MODEL_PRINCESS,
    MODEL_BALL,
    MODEL_BOSS_ATTACK,
    MODEL_SKILL_WHEEL1,
    MODEL_SKILL_WHEEL2,
    MODEL_SKILL_BLAST,
    MODEL_SKILL_INFERNO,
    MODEL_ARROW_DOUBLE,
    MODEL_ARROW_HOLY,
    MODEL_ARROW_SPARK,
    MODEL_ARROW_RING,
    MODEL_ARROW_DARKSTINGER,
    MODEL_FEATHER,
    MODEL_FEATHER_FOREIGN,
    MODEL_SKILL_FURY_STRIKE,
    MODEL_WAVE = MODEL_SKILL_FURY_STRIKE + 9,
    MODEL_TAIL,
    MODEL_PIERCING,
    MODEL_WAVE_FORCE = MODEL_PIERCING + 2,
    MODEL_BLIZZARD,
    MODEL_MAGIC_CAPSULE2,
    MODEL_STONE_COFFIN,
    MODEL_GATE = MODEL_STONE_COFFIN + 2,
    MODEL_ARROW_BEST_CROSSBOW = MODEL_GATE + 2,
    MODEL_SPEARSKILL,
    MODEL_PEGASUS,
    MODEL_ARROW_DRILL,
    MODEL_COMBO,
    MODEL_AIR_FORCE,
    MODEL_WAVES,
    MODEL_PIERCING2,
    MODEL_PIER_PART,
    MODEL_DARKLORD_SKILL,
    MODEL_GROUND_STONE,
    MODEL_GROUND_STONE2,
    MODEL_WATER_WAVE,
    MODEL_SKULL,
    MODEL_LACEARROW,
    MODEL_CUNDUN_PART1,
    MODEL_CUNDUN_PART2,
    MODEL_CUNDUN_PART3,
    MODEL_CUNDUN_PART4,
    MODEL_CUNDUN_PART5,
    MODEL_CUNDUN_PART6,
    MODEL_CUNDUN_PART7,
    MODEL_CUNDUN_PART8,
    MODEL_CUNDUN_DRAGON_HEAD,
    MODEL_CUNDUN_PHOENIX,
    MODEL_CUNDUN_GHOST,
    MODEL_CUNDUN_SKILL,
    MODEL_MANY_FLAG,
    MODEL_WEBZEN_MARK,
    MODEL_FLY_BIG_STONE1,
    MODEL_FLY_BIG_STONE2,
    MODEL_BIG_STONE_PART1,
    MODEL_BIG_STONE_PART2,
    MODEL_WALL_PART1,
    MODEL_WALL_PART2,
    MODEL_GATE_PART1,
    MODEL_GATE_PART2,
    MODEL_GATE_PART3,
    MODEL_AURORA,
    MODEL_TOWER_GATE_PLANE,
    MODEL_STUN_STONE,
    MODEL_SKIN_SHELL,
    MODEL_MANA_RUNE,
    MODEL_SKILL_JAVELIN,
    MODEL_ARROW_IMPACT,
    MODEL_SWORD_FORCE,
    MODEL_GOLEM_STONE,
    MODEL_FISSURE,
    MODEL_FISSURE_LIGHT,
    MODEL_SKILL_FISSURE,
    MODEL_PROTECTGUILD,
    MODEL_TREE_ATTACK,
    MODEL_WARP,
    MODEL_WARP2,
    MODEL_WARP3,
    MODEL_WARP4,
    MODEL_WARP5,
    MODEL_WARP6,
    MODEL_DARK_ELF_SKILL,
    MODEL_BALGAS_SKILL,
    MODEL_DEATH_SPI_SKILL,
    MODEL_SCOLPION,
    MODEL_BUG_CRY1ST,
    MODEL_FENRIR_BLACK,
    MODEL_FENRIR_RED,
    MODEL_FENRIR_BLUE,
    MODEL_FENRIR_GOLD,
    MODEL_FENRIR_THUNDER,
    MODEL_FENRIR_FOOT_THUNDER,
    MODEL_FENRIR_SKILL_THUNDER,
    MODEL_FENRIR_SKILL_DAMAGE,
    MODEL_ARROW_AUTOLOAD,
    MODEL_SHIELD_CRASH,
    MODEL_SHIELD_CRASH2,
    MODEL_INFINITY_ARROW,
    MODEL_INFINITY_ARROW1,
    MODEL_INFINITY_ARROW2,
    MODEL_INFINITY_ARROW3,
    MODEL_INFINITY_ARROW4,
    MODEL_IRON_RIDER_ARROW,
    MODEL_KENTAUROS_ARROW,
    MODEL_BLADE_SKILL,
    MODEL_TWINTAIL_EFFECT,
    MODEL_STORM2,
    MODEL_SUMMON,
    MODEL_STORM3,
    MODEL_MAYASTAR,
    MODEL_MAYASTONE1,
    MODEL_MAYASTONE2,
    MODEL_MAYASTONE3,
    MODEL_MAYASTONE4,
    MODEL_MAYASTONE5,
    MODEL_MAYASTONEFIRE,
    MODEL_MAYAHANDSKILL,
    MODEL_DARK_SCREAM,
    MODEL_DARK_SCREAM_FIRE,
    MODEL_CURSEDTEMPLE_HOLYITEM,
    MODEL_CURSEDTEMPLE_PRODECTION_SKILL,
    MODEL_CURSEDTEMPLE_RESTRAINT_SKILL,
    MODEL_FALL_STONE_EFFECT,
    MODEL_CURSEDTEMPLE_STATUE_PART1,
    MODEL_CURSEDTEMPLE_STATUE_PART2,
    MODEL_CHANGE_UP_EFF,
    MODEL_CHANGE_UP_NASA,
    MODEL_CHANGE_UP_CYLINDER,
    MODEL_TOTEMGOLEM_PART1,
    MODEL_TOTEMGOLEM_PART2,
    MODEL_TOTEMGOLEM_PART3,
    MODEL_TOTEMGOLEM_PART4,
    MODEL_TOTEMGOLEM_PART5,
    MODEL_TOTEMGOLEM_PART6,
    MODEL_SUMMONER_WRISTRING_EFFECT,
    MODEL_SUMMONER_EQUIP_HEAD_SAHAMUTT,
    MODEL_SUMMONER_EQUIP_HEAD_NEIL,
    MODEL_SUMMONER_EQUIP_HEAD_LAGUL,
    MODEL_SUMMONER_CASTING_EFFECT1,
    MODEL_SUMMONER_CASTING_EFFECT11,
    MODEL_SUMMONER_CASTING_EFFECT111,
    MODEL_SUMMONER_CASTING_EFFECT2,
    MODEL_SUMMONER_CASTING_EFFECT22,
    MODEL_SUMMONER_CASTING_EFFECT222,
    MODEL_SUMMONER_CASTING_EFFECT4,
    MODEL_SUMMONER_SUMMON_SAHAMUTT,
    MODEL_SUMMONER_SUMMON_NEIL,
    MODEL_SUMMONER_SUMMON_LAGUL,
    MODEL_SUMMONER_SUMMON_NEIL_NIFE1,
    MODEL_SUMMONER_SUMMON_NEIL_NIFE2,
    MODEL_SUMMONER_SUMMON_NEIL_NIFE3,
    MODEL_SUMMONER_SUMMON_NEIL_GROUND1,
    MODEL_SUMMONER_SUMMON_NEIL_GROUND2,
    MODEL_SUMMONER_SUMMON_NEIL_GROUND3,
    MODEL_SHADOW_PAWN_ANKLE_LEFT,
    MODEL_SHADOW_PAWN_ANKLE_RIGHT,
    MODEL_SHADOW_PAWN_BELT,
    MODEL_SHADOW_PAWN_CHEST,
    MODEL_SHADOW_PAWN_HELMET,
    MODEL_SHADOW_PAWN_KNEE_LEFT,
    MODEL_SHADOW_PAWN_KNEE_RIGHT,
    MODEL_SHADOW_PAWN_WRIST_LEFT,
    MODEL_SHADOW_PAWN_WRIST_RIGHT,

    MODEL_SHADOW_KNIGHT_ANKLE_LEFT,
    MODEL_SHADOW_KNIGHT_ANKLE_RIGHT,
    MODEL_SHADOW_KNIGHT_BELT,
    MODEL_SHADOW_KNIGHT_CHEST,
    MODEL_SHADOW_KNIGHT_HELMET,
    MODEL_SHADOW_KNIGHT_KNEE_LEFT,
    MODEL_SHADOW_KNIGHT_KNEE_RIGHT,
    MODEL_SHADOW_KNIGHT_WRIST_LEFT,
    MODEL_SHADOW_KNIGHT_WRIST_RIGHT,

    MODEL_SHADOW_ROOK_ANKLE_LEFT,
    MODEL_SHADOW_ROOK_ANKLE_RIGHT,
    MODEL_SHADOW_ROOK_BELT,
    MODEL_SHADOW_ROOK_CHEST,
    MODEL_SHADOW_ROOK_HELMET,
    MODEL_SHADOW_ROOK_KNEE_LEFT,
    MODEL_SHADOW_ROOK_KNEE_RIGHT,
    MODEL_SHADOW_ROOK_WRIST_LEFT,
    MODEL_SHADOW_ROOK_WRIST_RIGHT,

    MODEL_ICE_GIANT_PART1,
    MODEL_ICE_GIANT_PART2,
    MODEL_ICE_GIANT_PART3,
    MODEL_ICE_GIANT_PART4,
    MODEL_ICE_GIANT_PART5,
    MODEL_ICE_GIANT_PART6,

    MODEL_EFFECT_SAPITRES_ATTACK,
    MODEL_EFFECT_SAPITRES_ATTACK_1,
    MODEL_EFFECT_SAPITRES_ATTACK_2,
    MODEL_EFFECT_THUNDER_NAPIN_ATTACK_1,
    MODEL_EFFECT_SKURA_ITEM,
    MODEL_EFFECT_BROKEN_ICE0,
    MODEL_EFFECT_BROKEN_ICE1,
    MODEL_EFFECT_BROKEN_ICE2,
    MODEL_EFFECT_BROKEN_ICE3,
    MODEL_EFFECT_UMBRELLA_LIGHT,
    MODEL_DESAIR,
    MODEL_EFFECT_TRACE,
    MODEL_STAR_SHINE,
    MODEL_BLOW_OF_DESTRUCTION,
    MODEL_NIGHTWATER_01,
    MODEL_KNIGHT_PLANCRACK_A,
    MODEL_KNIGHT_PLANCRACK_B,
    MODEL_SWELL_OF_MAGICPOWER,
    MODEL_SWELL_OF_MAGICPOWER_GUIDE,
    MODEL_ARROWSRE06,
    MODEL_SWELL_OF_MAGICPOWER_BUFF_EFF,
    MODEL_EFFECT_FLAME_STRIKE,
    MODEL_STREAMOFICEBREATH,
    MODEL_1_STREAMBREATHFIRE,
    MODEL_ARROW_GAMBLE,
    MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_HEAD,
    MODEL_PKFIELD_ASSASSIN_EFFECT_RED_HEAD,
    MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_BODY,
    MODEL_PKFIELD_ASSASSIN_EFFECT_RED_BODY,
    MODEL_LAVAGIANT_FOOTPRINT_R,
    MODEL_LAVAGIANT_FOOTPRINT_V,
    MODEL_EFFECT_FIRE_HIK3_MONO,
    MODEL_PROJECTILE,
    MODEL_DOOR_CRUSH_EFFECT,
    MODEL_DOOR_CRUSH_EFFECT_PIECE01,
    MODEL_DOOR_CRUSH_EFFECT_PIECE02,
    MODEL_DOOR_CRUSH_EFFECT_PIECE03,
    MODEL_DOOR_CRUSH_EFFECT_PIECE04,
    MODEL_DOOR_CRUSH_EFFECT_PIECE05,
    MODEL_DOOR_CRUSH_EFFECT_PIECE06,
    MODEL_DOOR_CRUSH_EFFECT_PIECE07,
    MODEL_DOOR_CRUSH_EFFECT_PIECE08,
    MODEL_DOOR_CRUSH_EFFECT_PIECE09,
    MODEL_DOOR_CRUSH_EFFECT_PIECE10,
    MODEL_DOOR_CRUSH_EFFECT_PIECE11,
    MODEL_DOOR_CRUSH_EFFECT_PIECE12,
    MODEL_DOOR_CRUSH_EFFECT_PIECE13,
    MODEL_STATUE_CRUSH_EFFECT,
    MODEL_STATUE_CRUSH_EFFECT_PIECE01,
    MODEL_STATUE_CRUSH_EFFECT_PIECE02,
    MODEL_STATUE_CRUSH_EFFECT_PIECE03,
    MODEL_STATUE_CRUSH_EFFECT_PIECE04,
    MODEL_PANDA,

    MODEL_EMPIREGUARDIANBOSS_SKILLEFF01_GENERALATTACK,
    MODEL_EMPIREGUARDIANBOSS_SKILLEFF02_BLOODATTACK,
    MODEL_EMPIREGUARDIANBOSS_SKILLEFF03_GIGANTIC,
    MODEL_EMPIREGUARDIANBOSS_SKILLEFF04_FRAMESTRIKE,
    MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_,
    MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_,
    MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_,
    MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_,
    MODEL_SWORDMAIN01_EMPIREGUARDIAN_BOSS_GAION_,
    MODEL_EMPIREGUARDIAN_BLOW_OF_DESTRUCTION,
    MODEL_EFFECT_UMBRELLA_DIE,
    MODEL_EFFECT_UMBRELLA_GOLD,
    MODEL_DOPPELGANGER_SLIME_CHIP,
    MODEL_EMPIREGUARDIANBOSS_FRAMESTRIKE,
    MODEL_DEASULER,
    MODEL_EFFECT_EG_GUARDIANDEFENDER_ATTACK2,
    MODEL_EFFECT_SD_AURA,
    MODEL_SKELETON_CHANGED,
    MODEL_15GRADE_ARMOR_OBJ_ARMLEFT,
    MODEL_15GRADE_ARMOR_OBJ_ARMRIGHT,
    MODEL_15GRADE_ARMOR_OBJ_BODYLEFT,
    MODEL_15GRADE_ARMOR_OBJ_BODYRIGHT,
    MODEL_15GRADE_ARMOR_OBJ_BOOTLEFT,
    MODEL_15GRADE_ARMOR_OBJ_BOOTRIGHT,
    MODEL_15GRADE_ARMOR_OBJ_HEAD,
    MODEL_15GRADE_ARMOR_OBJ_PANTLEFT,
    MODEL_15GRADE_ARMOR_OBJ_PANTRIGHT,
    MODEL_EX01_SHADOW_MASTER_ANKLE_LEFT,
    MODEL_EX01_SHADOW_MASTER_ANKLE_RIGHT,
    MODEL_EX01_SHADOW_MASTER_BELT,
    MODEL_EX01_SHADOW_MASTER_CHEST,
    MODEL_EX01_SHADOW_MASTER_HELMET,
    MODEL_EX01_SHADOW_MASTER_KNEE_LEFT,
    MODEL_EX01_SHADOW_MASTER_KNEE_RIGHT,
    MODEL_EX01_SHADOW_MASTER_WRIST_LEFT,
    MODEL_EX01_SHADOW_MASTER_WRIST_RIGHT,

#ifdef ASG_ADD_KARUTAN_MONSTERS
    MODEL_CONDRA_ARM_L,
    MODEL_CONDRA_ARM_L2,
    MODEL_CONDRA_SHOULDER,
    MODEL_CONDRA_ARM_R,
    MODEL_CONDRA_ARM_R2,
    MODEL_CONDRA_CONE_L,
    MODEL_CONDRA_CONE_R,
    MODEL_CONDRA_PELVIS,
    MODEL_CONDRA_STOMACH,
    MODEL_CONDRA_NECK,
    MODEL_CONDRA_STONE,
    MODEL_CONDRA_STONE1,
    MODEL_CONDRA_STONE2,
    MODEL_CONDRA_STONE3,
    MODEL_CONDRA_STONE4,
    MODEL_CONDRA_STONE5,

    MODEL_NARCONDRA_ARM_L,
    MODEL_NARCONDRA_ARM_L2,
    MODEL_NARCONDRA_SHOULDER_L,
    MODEL_NARCONDRA_SHOULDER_R,
    MODEL_NARCONDRA_ARM_R,
    MODEL_NARCONDRA_ARM_R2,
    MODEL_NARCONDRA_ARM_R3,
    MODEL_NARCONDRA_CONE_1,
    MODEL_NARCONDRA_CONE_2,
    MODEL_NARCONDRA_CONE_3,
    MODEL_NARCONDRA_CONE_4,
    MODEL_NARCONDRA_CONE_5,
    MODEL_NARCONDRA_CONE_6,
    MODEL_NARCONDRA_PELVIS,
    MODEL_NARCONDRA_STOMACH,
    MODEL_NARCONDRA_NECK,
    MODEL_NARCONDRA_STONE,
    MODEL_NARCONDRA_STONE1,
    MODEL_NARCONDRA_STONE2,
    MODEL_NARCONDRA_STONE3,
#endif // ASG_ADD_KARUTAN_MONSTERS

    MODEL_SWORD_32_LEFT,
    MODEL_SWORD_32_RIGHT,
    MODEL_SWORD_33_LEFT,
    MODEL_SWORD_33_RIGHT,
    MODEL_SWORD_34_LEFT,
    MODEL_SWORD_34_RIGHT,
    MODEL_SWORD_35_LEFT,
    MODEL_SWORD_35_RIGHT,
    MODEL_SWORD_35_WING,
    MODEL_WOLF_HEAD_EFFECT,
    MODEL_ARMORINVEN_60,
    MODEL_ARMORINVEN_61,
    MODEL_ARMORINVEN_62,
    MODEL_DOWN_ATTACK_DUMMY_L,
    MODEL_DOWN_ATTACK_DUMMY_R,
    MODEL_SHOCKWAVE01,
    MODEL_SHOCKWAVE02,
    MODEL_SHOCKWAVE_SPIN01,
    MODEL_WINDFOCE,
    MODEL_WINDFOCE_MIRROR,
    MODEL_WOLF_HEAD_EFFECT2,
    MODEL_SHOCKWAVE_GROUND01,
    MODEL_DRAGON_KICK_DUMMY,
    MODEL_DRAGON_LOWER_DUMMY,
    MODEL_TARGETMON_EFFECT,
    MODEL_SHOCKWAVE03,
    MODEL_VOLCANO_OF_MONK,
    MODEL_VOLCANO_STONE,
    MODEL_ARMORINVEN_74,
    MODEL_PHOENIX_SHOT,
    MODEL_WINDSPIN01,
    MODEL_WINDSPIN02,
    MODEL_WINDSPIN03,

    MODEL_SKILL_END
};

enum
{
    // monsters
    MODEL_MONSTER01 = MODEL_SKILL_END + 1,
    MODEL_MONSTER_END = MODEL_MONSTER01 + MAX_MODEL_MONSTER,
};

enum
{
    //npc

    MODEL_NPC_BEGIN = MODEL_MONSTER_END,
    MODEL_MERCHANT_FEMALE,
    MODEL_MERCHANT_MAN,
    MODEL_MERCHANT_GIRL,
    MODEL_SMITH,
    MODEL_SCIENTIST,
    MODEL_SNOW_MERCHANT,
    MODEL_SNOW_SMITH,
    MODEL_SNOW_WIZARD,
    MODEL_ELF_WIZARD,
    MODEL_ELF_MERCHANT,
    MODEL_MASTER,
    MODEL_STORAGE,
    MODEL_TOURNAMENT,
    MODEL_MIX_NPC,
    MODEL_NPC_DEVILSQUARE,
    MODEL_MERCHANT_FEMALE_HEAD,
    MODEL_MERCHANT_FEMALE_UPPER = MODEL_MERCHANT_FEMALE_HEAD + 2,
    MODEL_MERCHANT_FEMALE_LOWER = MODEL_MERCHANT_FEMALE_UPPER + 2,
    MODEL_MERCHANT_FEMALE_GLOVES = MODEL_MERCHANT_FEMALE_LOWER + 2,
    MODEL_MERCHANT_FEMALE_BOOTS = MODEL_MERCHANT_FEMALE_GLOVES + 2,
    MODEL_MERCHANT_MAN_HEAD = MODEL_MERCHANT_FEMALE_BOOTS + 2,
    MODEL_MERCHANT_MAN_UPPER = MODEL_MERCHANT_MAN_HEAD + 2,
    MODEL_MERCHANT_MAN_GLOVES = MODEL_MERCHANT_MAN_UPPER + 2,
    MODEL_MERCHANT_MAN_BOOTS = MODEL_MERCHANT_MAN_GLOVES + 2,
    MODEL_MERCHANT_GIRL_HEAD = MODEL_MERCHANT_MAN_BOOTS + 2,
    MODEL_MERCHANT_GIRL_UPPER = MODEL_MERCHANT_GIRL_HEAD + 2,
    MODEL_MERCHANT_GIRL_LOWER = MODEL_MERCHANT_GIRL_UPPER + 2,
    MODEL_NPC_SEVINA = MODEL_MERCHANT_GIRL_LOWER + 2,
    MODEL_NPC_ARCHANGEL,
    MODEL_NPC_ARCHANGEL_MESSENGER,
    MODEL_DEVIAS_TRADER,
    MODEL_NPC_BREEDER,
    MODEL_ANGEL,
    MODEL_KALIMA_SHOP,
    MODEL_BC_NPC1,
    MODEL_BC_NPC2,
    MODEL_BC_BOX,
    MODEL_NPC_CAPATULT_ATT,
    MODEL_NPC_CAPATULT_DEF,
    MODEL_NPC_SENATUS,
    MODEL_NPC_GATE_SWITCH,
    MODEL_NPC_CROWN,
    MODEL_NPC_CHECK_FLOOR,
    MODEL_NPC_CLERK,
    MODEL_NPC_BARRIER,
    MODEL_NPC_SERBIS,
    MODEL_NPC_SERBIS_DONKEY,
    MODEL_NPC_SERBIS_FLAG,
    MODEL_CRYWOLF_STATUE,
    MODEL_CRYWOLF_ALTAR1,
    MODEL_CRYWOLF_ALTAR2,
    MODEL_CRYWOLF_ALTAR3,
    MODEL_CRYWOLF_ALTAR4,
    MODEL_CRYWOLF_ALTAR5,
    MODEL_KANTURU2ND_ENTER_NPC,
    MODEL_TRAP_CANON,
    MODEL_SMELTING_NPC,
    MODEL_REFINERY_NPC,
    MODEL_RECOVERY_NPC,
    MODEL_WEDDING_NPC,
    MODEL_NPC_DEVIN,
    MODEL_NPC_QUARREL,
    MODEL_NPC_CASTEL_GATE,
    MODEL_CURSEDTEMPLE_ENTER_NPC,
    MODEL_CURSEDTEMPLE_ALLIED_NPC,
    MODEL_CURSEDTEMPLE_ILLUSION_NPC,
    MODEL_CURSEDTEMPLE_STATUE,
    MODEL_CURSEDTEMPLE_ALLIED_BASKET,
    MODEL_CURSEDTEMPLE_ILLUSION__BASKET,
    MODEL_ELBELAND_SILVIA,
    MODEL_ELBELAND_RHEA,
    MODEL_ELBELAND_MARCE,
    MODEL_NPC_CHERRYBLOSSOM,
    MODEL_NPC_CHERRYBLOSSOMTREE,
    MODEL_SEED_MASTER,
    MODEL_SEED_INVESTIGATOR,
    MODEL_LITTLESANTA,
    MODEL_LITTLESANTA_END = MODEL_LITTLESANTA + 7,
    MODEL_XMAS2008_SNOWMAN,
    MODEL_XMAS2008_SNOWMAN_HEAD,
    MODEL_XMAS2008_SNOWMAN_BODY,
    MODEL_XMAS2008_SNOWMAN_NPC,
    MODEL_XMAS2008_SANTA_NPC,
    MODEL_DUEL_NPC_TITUS,
    MODEL_GAMBLE_NPC_MOSS,
    MODEL_DOPPELGANGER_NPC_LUGARD,
    MODEL_DOPPELGANGER_NPC_BOX,
    MODEL_DOPPELGANGER_NPC_GOLDENBOX,
    MODAL_GENS_NPC_DUPRIAN,
    MODAL_GENS_NPC_BARNERT,
    MODEL_UNITEDMARKETPLACE_CHRISTIN,
    MODEL_UNITEDMARKETPLACE_RAUL,
    MODEL_UNITEDMARKETPLACE_JULIA,
    MODEL_KARUTAN_NPC_REINA,
    MODEL_KARUTAN_NPC_VOLVO,
    MODEL_LUCKYITEM_NPC,
    MODEL_TERSIA,
    MODEL_BENA,
    MODEL_NPC_END,
    //player
    MODEL_PLAYER,
    MODEL_SHADOW_BODY,
    MODEL_SHADOW_SWORD,
    MODEL_SHADOW_AXE,
    MODEL_SHADOW_MACE,
    MODEL_SHADOW_SPEAR,
    MODEL_SHADOW_BOW,
    MODEL_SHADOW_STAFF,

    //item
    MODEL_ITEM, // (515)

    MODEL_SWORD = MODEL_ITEM,
    MODEL_AXE = (MODEL_ITEM + ITEM_AXE),
    MODEL_MACE = (MODEL_ITEM + ITEM_MACE),
    MODEL_SPEAR = (MODEL_ITEM + ITEM_SPEAR),
    MODEL_BOW = (MODEL_ITEM + ITEM_BOW),
    MODEL_STAFF = (MODEL_ITEM + ITEM_STAFF),
    MODEL_SHIELD = (MODEL_ITEM + ITEM_SHIELD),
    MODEL_HELM = (MODEL_ITEM + ITEM_HELM),
    MODEL_ARMOR = (MODEL_ITEM + ITEM_ARMOR),
    MODEL_PANTS = (MODEL_ITEM + ITEM_PANTS),
    MODEL_GLOVES = (MODEL_ITEM + ITEM_GLOVES),
    MODEL_BOOTS = (MODEL_ITEM + ITEM_BOOTS),
    MODEL_WING = (MODEL_ITEM + ITEM_WING),
    MODEL_HELPER = (MODEL_ITEM + ITEM_HELPER),
    MODEL_POTION = (MODEL_ITEM + ITEM_POTION),
    MODEL_ETC = (MODEL_ITEM + ITEM_ETC),

    MODEL_HELM2 = (MODEL_ITEM + MAX_ITEM),
    MODEL_ARMOR2 = (MODEL_HELM2 + MODEL_ITEM_COMMON_NUM),
    MODEL_PANTS2 = (MODEL_ARMOR2 + MODEL_ITEM_COMMON_NUM),
    MODEL_GLOVES2 = (MODEL_PANTS2 + MODEL_ITEM_COMMON_NUM),
    MODEL_BOOTS2 = (MODEL_GLOVES2 + MODEL_ITEM_COMMON_NUM),
    MODEL_HELM_MONK = (MODEL_BOOTS2 + MODEL_ITEM_COMMON_NUM),
    MODEL_ARMOR_MONK = (MODEL_HELM_MONK + MODEL_ITEM_COMMONCNT_RAGEFIGHTER),
    MODEL_PANTS_MONK = (MODEL_ARMOR_MONK + MODEL_ITEM_COMMONCNT_RAGEFIGHTER),
    MODEL_BOOTS_MONK = (MODEL_PANTS_MONK + MODEL_ITEM_COMMONCNT_RAGEFIGHTER),

    MODEL_BODY_HELM = (MODEL_BOOTS_MONK + MODEL_ITEM_COMMONCNT_RAGEFIGHTER),

    MODEL_BODY_ARMOR = (MODEL_BODY_HELM + MODEL_BODY_NUM),
    MODEL_BODY_PANTS = (MODEL_BODY_ARMOR + MODEL_BODY_NUM),
    MODEL_BODY_GLOVES = (MODEL_BODY_PANTS + MODEL_BODY_NUM),
    MODEL_BODY_BOOTS = (MODEL_BODY_GLOVES + MODEL_BODY_NUM),

    MODEL_EVENT = (MODEL_BODY_BOOTS + MODEL_BODY_NUM),
    MODEL_QUEST = (MODEL_EVENT + MAX_EVENT_ITEM),
    MODEL_MULTI_SHOT1,
    MODEL_MULTI_SHOT2,
    MODEL_MULTI_SHOT3,
    MODEL_MASK_HELM = (MODEL_MULTI_SHOT3 + MAX_QUEST_ITEM),
    MAX_MODELS = (MODEL_MASK_HELM + MAX_ITEM_INDEX),
};

enum
{
    MODEL_KRIS = MODEL_SWORD + 0,
    MODEL_SHORT_SWORD = MODEL_SWORD + 1,
    MODEL_RAPIER = MODEL_SWORD + 2,
    MODEL_KATACHE = MODEL_SWORD + 3,
    MODEL_SWORD_OF_ASSASSIN = MODEL_SWORD + 4,
    MODEL_BLADE = MODEL_SWORD + 5,
    MODEL_GLADIUS = MODEL_SWORD + 6,
    MODEL_FALCHION = MODEL_SWORD + 7,
    MODEL_SERPENT_SWORD = MODEL_SWORD + 8,
    MODEL_SWORD_OF_SALAMANDER = MODEL_SWORD + 9,
    MODEL_LIGHT_SABER = MODEL_SWORD + 10,
    MODEL_LEGENDARY_SWORD = MODEL_SWORD + 11,
    MODEL_HELIACAL_SWORD = MODEL_SWORD + 12,
    MODEL_DOUBLE_BLADE = MODEL_SWORD + 13,
    MODEL_LIGHTING_SWORD = MODEL_SWORD + 14,
    MODEL_GIANT_SWORD = MODEL_SWORD + 15,
    MODEL_SWORD_OF_DESTRUCTION = MODEL_SWORD + 16,
    MODEL_DARK_BREAKER = MODEL_SWORD + 17,
    MODEL_THUNDER_BLADE = MODEL_SWORD + 18,
    MODEL_DIVINE_SWORD_OF_ARCHANGEL = MODEL_SWORD + 19,
    MODEL_KNIGHT_BLADE = MODEL_SWORD + 20,
    MODEL_DARK_REIGN_BLADE = MODEL_SWORD + 21,
    MODEL_BONE_BLADE = MODEL_SWORD + 22,
    MODEL_EXPLOSION_BLADE = MODEL_SWORD + 23,
    MODEL_DAYBREAK = MODEL_SWORD + 24,
    MODEL_SWORD_DANCER = MODEL_SWORD + 25,
    MODEL_FLAMBERGE = MODEL_SWORD + 26,
    MODEL_SWORD_BREAKER = MODEL_SWORD + 27,
    MODEL_IMPERIAL_SWORD = MODEL_SWORD + 28,
    MODEL_RUNE_BLADE = MODEL_SWORD + 31,
    MODEL_SACRED_GLOVE = MODEL_SWORD + 32,
    MODEL_STORM_HARD_GLOVE = MODEL_SWORD + 33,
    MODEL_PIERCING_BLADE_GLOVE = MODEL_SWORD + 34,
    MODEL_PHOENIX_SOUL_STAR = MODEL_SWORD + 35,
    MODEL_SMALL_AXE = MODEL_AXE + 0,
    MODEL_HAND_AXE = MODEL_AXE + 1,
    MODEL_DOUBLE_AXE = MODEL_AXE + 2,
    MODEL_TOMAHAWK = MODEL_AXE + 3,
    MODEL_ELVEN_AXE = MODEL_AXE + 4,
    MODEL_BATTLE_AXE = MODEL_AXE + 5,
    MODEL_NIKKEA_AXE = MODEL_AXE + 6,
    MODEL_LARKAN_AXE = MODEL_AXE + 7,
    MODEL_CRESCENT_AXE = MODEL_AXE + 8,
    MODEL_SMALLMACE = MODEL_MACE + 0,
    MODEL_MORNING_STAR = MODEL_MACE + 1,
    MODEL_FLAIL = MODEL_MACE + 2,
    MODEL_GREAT_HAMMER = MODEL_MACE + 3,
    MODEL_CRYSTAL_MORNING_STAR = MODEL_MACE + 4,
    MODEL_CRYSTAL_SWORD = MODEL_MACE + 5,
    MODEL_CHAOS_DRAGON_AXE = MODEL_MACE + 6,
    MODEL_ELEMENTAL_MACE = MODEL_MACE + 7,
    MODEL_BATTLE_SCEPTER = MODEL_MACE + 8,
    MODEL_MASTER_SCEPTER = MODEL_MACE + 9,
    MODEL_GREAT_SCEPTER = MODEL_MACE + 10,
    MODEL_LORD_SCEPTER = MODEL_MACE + 11,
    MODEL_GREAT_LORD_SCEPTER = MODEL_MACE + 12,
    MODEL_DIVINE_SCEPTER_OF_ARCHANGEL = MODEL_MACE + 13,
    MODEL_SOLEIL_SCEPTER = MODEL_MACE + 14,
    MODEL_SHINING_SCEPTER = MODEL_MACE + 15,
    MODEL_FROST_MACE = MODEL_MACE + 16,
    MODEL_ABSOLUTE_SCEPTER = MODEL_MACE + 17,
    MODEL_STRYKER_SCEPTER = MODEL_MACE + 18,
    MODEL_LIGHT_SPEAR = MODEL_SPEAR + 0,
    MODEL__SPEAR = MODEL_SPEAR + 1,
    MODEL_DRAGON_LANCE = MODEL_SPEAR + 2,
    MODEL_GIANT_TRIDENT = MODEL_SPEAR + 3,
    MODEL_SERPENT_SPEAR = MODEL_SPEAR + 4,
    MODEL_DOUBLE_POLEAXE = MODEL_SPEAR + 5,
    MODEL_HALBERD = MODEL_SPEAR + 6,
    MODEL_BERDYSH = MODEL_SPEAR + 7,
    MODEL_GREAT_SCYTHE = MODEL_SPEAR + 8,
    MODEL_BILL_OF_BALROG = MODEL_SPEAR + 9,
    MODEL_DRAGON_SPEAR = MODEL_SPEAR + 10,
    MODEL_BEUROBA = MODEL_SPEAR + 11,
    MODEL_SHORT_BOW = MODEL_BOW + 0,
    MODEL_SMALL_BOW = MODEL_BOW + 1,
    MODEL_ELVEN_BOW = MODEL_BOW + 2,
    MODEL_BATTLE_BOW = MODEL_BOW + 3,
    MODEL_TIGER_BOW = MODEL_BOW + 4,
    MODEL_SILVER_BOW = MODEL_BOW + 5,
    MODEL_CHAOS_NATURE_BOW = MODEL_BOW + 6,
    MODEL_BOLT = MODEL_BOW + 7,
    MODEL_CROSSBOW = MODEL_BOW + 8,
    MODEL_GOLDEN_CROSSBOW = MODEL_BOW + 9,
    MODEL_ARQUEBUS = MODEL_BOW + 10,
    MODEL_LIGHT_CROSSBOW = MODEL_BOW + 11,
    MODEL_SERPENT_CROSSBOW = MODEL_BOW + 12,
    MODEL_BLUEWING_CROSSBOW = MODEL_BOW + 13,
    MODEL_AQUAGOLD_CROSSBOW = MODEL_BOW + 14,
    MODEL_ARROWS = MODEL_BOW + 15,
    MODEL_SAINT_CROSSBOW = MODEL_BOW + 16,
    MODEL_CELESTIAL_BOW = MODEL_BOW + 17,
    MODEL_DIVINE_CB_OF_ARCHANGEL = MODEL_BOW + 18,
    MODEL_GREAT_REIGN_CROSSBOW = MODEL_BOW + 19,
    MODEL_ARROW_VIPER_BOW = MODEL_BOW + 20,
    MODEL_SYLPH_WIND_BOW = MODEL_BOW + 21,
    MODEL_ALBATROSS_BOW = MODEL_BOW + 22,
    MODEL_STINGER_BOW = MODEL_BOW + 23,
    MODEL_AIR_LYN_BOW = MODEL_BOW + 24,
    MODEL_SKULL_STAFF = MODEL_STAFF + 0,
    MODEL_ANGELIC_STAFF = MODEL_STAFF + 1,
    MODEL_SERPENT_STAFF = MODEL_STAFF + 2,
    MODEL_THUNDER_STAFF = MODEL_STAFF + 3,
    MODEL_GORGON_STAFF = MODEL_STAFF + 4,
    MODEL_LEGENDARY_STAFF = MODEL_STAFF + 5,
    MODEL_STAFF_OF_RESURRECTION = MODEL_STAFF + 6,
    MODEL_CHAOS_LIGHTNING_STAFF = MODEL_STAFF + 7,
    MODEL_STAFF_OF_DESTRUCTION = MODEL_STAFF + 8,
    MODEL_DRAGON_SOUL_STAFF = MODEL_STAFF + 9,
    MODEL_DIVINE_STAFF_OF_ARCHANGEL = MODEL_STAFF + 10,
    MODEL_STAFF_OF_KUNDUN = MODEL_STAFF + 11,
    MODEL_GRAND_VIPER_STAFF = MODEL_STAFF + 12,
    MODEL_PLATINA_STAFF = MODEL_STAFF + 13,
    MODEL_MISTERY_STICK = MODEL_STAFF + 14,
    MODEL_VIOLENT_WIND_STICK = MODEL_STAFF + 15,
    MODEL_RED_WING_STICK = MODEL_STAFF + 16,
    MODEL_ANCIENT_STICK = MODEL_STAFF + 17,
    MODEL_DEMONIC_STICK = MODEL_STAFF + 18,
    MODEL_STORM_BLITZ_STICK = MODEL_STAFF + 19,
    MODEL_ETERNAL_WING_STICK = MODEL_STAFF + 20,
    MODEL_BOOK_OF_SAHAMUTT = MODEL_STAFF + 21,
    MODEL_BOOK_OF_NEIL = MODEL_STAFF + 22,
    MODEL_BOOK_OF_LAGLE = MODEL_STAFF + 23,
    MODEL_DEADLY_STAFF = MODEL_STAFF + 30,
    MODEL_IMPERIAL_STAFF = MODEL_STAFF + 31,
    MODEL_CHROMATIC_STAFF = MODEL_STAFF + 33,
    MODEL_RAVEN_STICK = MODEL_STAFF + 34,
    MODEL_DIVINE_STICK_OF_ARCHANGEL = MODEL_STAFF + 36,
    MODEL_SMALL_SHIELD = MODEL_SHIELD + 0,
    MODEL_HORN_SHIELD = MODEL_SHIELD + 1,
    MODEL_KITE_SHIELD = MODEL_SHIELD + 2,
    MODEL_ELVEN_SHIELD = MODEL_SHIELD + 3,
    MODEL_BUCKLER = MODEL_SHIELD + 4,
    MODEL_DRAGON_SLAYER_SHIELD = MODEL_SHIELD + 5,
    MODEL_SKULL_SHIELD = MODEL_SHIELD + 6,
    MODEL_SPIKED_SHIELD = MODEL_SHIELD + 7,
    MODEL_TOWER_SHIELD = MODEL_SHIELD + 8,
    MODEL_PLATE_SHIELD = MODEL_SHIELD + 9,
    MODEL_BIG_ROUND_SHIELD = MODEL_SHIELD + 10,
    MODEL_SERPENT_SHIELD = MODEL_SHIELD + 11,
    MODEL_BRONZE_SHIELD = MODEL_SHIELD + 12,
    MODEL_DRAGON_SHIELD = MODEL_SHIELD + 13,
    MODEL_LEGENDARY_SHIELD = MODEL_SHIELD + 14,
    MODEL_GRAND_SOUL_SHIELD = MODEL_SHIELD + 15,
    MODEL_ELEMENTAL_SHIELD = MODEL_SHIELD + 16,
    MODEL_CRIMSONGLORY = MODEL_SHIELD + 17,
    MODEL_SALAMANDER_SHIELD = MODEL_SHIELD + 18,
    MODEL_FROST_BARRIER = MODEL_SHIELD + 19,
    MODEL_GUARDIAN_SHILED = MODEL_SHIELD + 20,
    MODEL_CROSS_SHIELD = MODEL_SHIELD + 21,
    MODEL_BRONZE_HELM = MODEL_HELM + 0,
    MODEL_DRAGON_HELM = MODEL_HELM + 1,
    MODEL_PAD_HELM = MODEL_HELM + 2,
    MODEL_LEGENDARY_HELM = MODEL_HELM + 3,
    MODEL_BONE_HELM = MODEL_HELM + 4,
    MODEL_LEATHER_HELM = MODEL_HELM + 5,
    MODEL_SCALE_HELM = MODEL_HELM + 6,
    MODEL_SPHINX_MASK = MODEL_HELM + 7,
    MODEL_BRASS_HELM = MODEL_HELM + 8,
    MODEL_PLATE_HELM = MODEL_HELM + 9,
    MODEL_VINE_HELM = MODEL_HELM + 10,
    MODEL_SILK_HELM = MODEL_HELM + 11,
    MODEL_WIND_HELM = MODEL_HELM + 12,
    MODEL_SPIRIT_HELM = MODEL_HELM + 13,
    MODEL_GUARDIAN_HELM = MODEL_HELM + 14,
    MODEL_BLACK_DRAGON_HELM = MODEL_HELM + 16,
    MODEL_DARK_PHOENIX_HELM = MODEL_HELM + 17,
    MODEL_GRAND_SOUL_HELM = MODEL_HELM + 18,
    MODEL_DIVINE_HELM = MODEL_HELM + 19,
    MODEL_GREAT_DRAGON_HELM = MODEL_HELM + 21,
    MODEL_DARK_SOUL_HELM = MODEL_HELM + 22,
    MODEL_RED_SPIRIT_HELM = MODEL_HELM + 24,
    MODEL_LIGHT_PLATE_MASK = MODEL_HELM + 25,
    MODEL_ADAMANTINE_MASK = MODEL_HELM + 26,
    MODEL_DARK_STEEL_MASK = MODEL_HELM + 27,
    MODEL_DARK_MASTER_MASK = MODEL_HELM + 28,
    MODEL_DRAGON_KNIGHT_HELM = MODEL_HELM + 29,
    MODEL_VENOM_MIST_HELM = MODEL_HELM + 30,
    MODEL_SYLPHID_RAY_HELM = MODEL_HELM + 31,
    MODEL_SUNLIGHT_MASK = MODEL_HELM + 33,
    MODEL_ASHCROW_HELM = MODEL_HELM + 34,
    MODEL_ECLIPSE_HELM = MODEL_HELM + 35,
    MODEL_IRIS_HELM = MODEL_HELM + 36,
    MODEL_GLORIOUS_MASK = MODEL_HELM + 38,
    MODEL_MISTERY_HELM = MODEL_HELM + 39,
    MODEL_RED_WING_HELM = MODEL_HELM + 40,
    MODEL_ANCIENT_HELM = MODEL_HELM + 41,
    MODEL_BLACK_ROSE_HELM = MODEL_HELM + 42,
    MODEL_AURA_HELM = MODEL_HELM + 43,
    MODEL_LILIUM_HELM = MODEL_HELM + 44,
    MODEL_TITAN_HELM = MODEL_HELM + 45,
    MODEL_BRAVE_HELM = MODEL_HELM + 46,
    MODEL_SERAPHIM_HELM = MODEL_HELM + 49,
    MODEL_FAITH_HELM = MODEL_HELM + 50,
    MODEL_PAEWANG_MASK = MODEL_HELM + 51,
    MODEL_HADES_HELM = MODEL_HELM + 52,
    MODEL_SACRED_HELM = MODEL_HELM + 59,
    MODEL_STORM_HARD_HELM = MODEL_HELM + 60,
    MODEL_PIERCING_HELM = MODEL_HELM + 61,
    MODEL_PHOENIX_SOUL_HELMET = MODEL_HELM + 73,
    MODEL_BRONZE_ARMOR = MODEL_ARMOR + 0,
    MODEL_DRAGON_ARMOR = MODEL_ARMOR + 1,
    MODEL_PAD_ARMOR = MODEL_ARMOR + 2,
    MODEL_LEGENDARY_ARMOR = MODEL_ARMOR + 3,
    MODEL_BONE_ARMOR = MODEL_ARMOR + 4,
    MODEL_LEATHER_ARMOR = MODEL_ARMOR + 5,
    MODEL_SCALE_ARMOR = MODEL_ARMOR + 6,
    MODEL_SPHINX_ARMOR = MODEL_ARMOR + 7,
    MODEL_BRASS_ARMOR = MODEL_ARMOR + 8,
    MODEL_PLATE_ARMOR = MODEL_ARMOR + 9,
    MODEL_VINE_ARMOR = MODEL_ARMOR + 10,
    MODEL_SILK_ARMOR = MODEL_ARMOR + 11,
    MODEL_WIND_ARMOR = MODEL_ARMOR + 12,
    MODEL_SPIRIT_ARMOR = MODEL_ARMOR + 13,
    MODEL_GUARDIAN_ARMOR = MODEL_ARMOR + 14,
    MODEL_STORM_CROW_ARMOR = MODEL_ARMOR + 15,
    MODEL_BLACK_DRAGON_ARMOR = MODEL_ARMOR + 16,
    MODEL_DARK_PHOENIX_ARMOR = MODEL_ARMOR + 17,
    MODEL_GRAND_SOUL_ARMOR = MODEL_ARMOR + 18,
    MODEL_DIVINE_ARMOR = MODEL_ARMOR + 19,
    MODEL_THUNDER_HAWK_ARMOR = MODEL_ARMOR + 20,
    MODEL_GREAT_DRAGON_ARMOR = MODEL_ARMOR + 21,
    MODEL_DARK_SOUL_ARMOR = MODEL_ARMOR + 22,
    MODEL_HURRICANE_ARMOR = MODEL_ARMOR + 23,
    MODEL_RED_SPRIT_ARMOR = MODEL_ARMOR + 24,
    MODEL_LIGHT_PLATE_ARMOR = MODEL_ARMOR + 25,
    MODEL_ADAMANTINE_ARMOR = MODEL_ARMOR + 26,
    MODEL_DARK_STEEL_ARMOR = MODEL_ARMOR + 27,
    MODEL_DARK_MASTER_ARMOR = MODEL_ARMOR + 28,
    MODEL_DRAGON_KNIGHT_ARMOR = MODEL_ARMOR + 29,
    MODEL_VENOM_MIST_ARMOR = MODEL_ARMOR + 30,
    MODEL_SYLPHID_RAY_ARMOR = MODEL_ARMOR + 31,
    MODEL_VOLCANO_ARMOR = MODEL_ARMOR + 32,
    MODEL_SUNLIGHT_ARMOR = MODEL_ARMOR + 33,
    MODEL_ASHCROW_ARMOR = MODEL_ARMOR + 34,
    MODEL_ECLIPSE_ARMOR = MODEL_ARMOR + 35,
    MODEL_IRIS_ARMOR = MODEL_ARMOR + 36,
    MODEL_VALIANT_ARMOR = MODEL_ARMOR + 37,
    MODEL_GLORIOUS_ARMOR = MODEL_ARMOR + 38,
    MODEL_MISTERY_ARMOR = MODEL_ARMOR + 39,
    MODEL_RED_WING_ARMOR = MODEL_ARMOR + 40,
    MODEL_ANCIENT_ARMOR = MODEL_ARMOR + 41,
    MODEL_BLACK_ROSE_ARMOR = MODEL_ARMOR + 42,
    MODEL_AURA_ARMOR = MODEL_ARMOR + 43,
    MODEL_LILIUM_ARMOR = MODEL_ARMOR + 44,
    MODEL_TITAN_ARMOR = MODEL_ARMOR + 45,
    MODEL_BRAVE_ARMOR = MODEL_ARMOR + 46,
    MODEL_DESTORY_ARMOR = MODEL_ARMOR + 47,
    MODEL_PHANTOM_ARMOR = MODEL_ARMOR + 48,
    MODEL_SERAPHIM_ARMOR = MODEL_ARMOR + 49,
    MODEL_FAITH_ARMOR = MODEL_ARMOR + 50,
    MODEL_PAEWANG_ARMOR = MODEL_ARMOR + 51,
    MODEL_HADES_ARMOR = MODEL_ARMOR + 52,
    MODEL_SACRED_ARMOR = MODEL_ARMOR + 59,
    MODEL_STORM_HARD_ARMOR = MODEL_ARMOR + 60,
    MODEL_PIERCING_ARMOR = MODEL_ARMOR + 61,
    MODEL_PHOENIX_SOUL_ARMOR = MODEL_ARMOR + 73,
    MODEL_BRONZE_PANTS = MODEL_PANTS + 0,
    MODEL_DRAGON_PANTS = MODEL_PANTS + 1,
    MODEL_PAD_PANTS = MODEL_PANTS + 2,
    MODEL_LEGENDARY_PANTS = MODEL_PANTS + 3,
    MODEL_BONE_PANTS = MODEL_PANTS + 4,
    MODEL_LEATHER_PANTS = MODEL_PANTS + 5,
    MODEL_SCALE_PANTS = MODEL_PANTS + 6,
    MODEL_SPHINX_PANTS = MODEL_PANTS + 7,
    MODEL_BRASS_PANTS = MODEL_PANTS + 8,
    MODEL_PLATE_PANTS = MODEL_PANTS + 9,
    MODEL_VINE_PANTS = MODEL_PANTS + 10,
    MODEL_SILK_PANTS = MODEL_PANTS + 11,
    MODEL_WIND_PANTS = MODEL_PANTS + 12,
    MODEL_SPIRIT_PANTS = MODEL_PANTS + 13,
    MODEL_GUARDIAN_PANTS = MODEL_PANTS + 14,
    MODEL_STORM_CROW_PANTS = MODEL_PANTS + 15,
    MODEL_BLACK_DRAGON_PANTS = MODEL_PANTS + 16,
    MODEL_DARK_PHOENIX_PANTS = MODEL_PANTS + 17,
    MODEL_GRAND_SOUL_PANTS = MODEL_PANTS + 18,
    MODEL_DIVINE_PANTS = MODEL_PANTS + 19,
    MODEL_THUNDER_HAWK_PANTS = MODEL_PANTS + 20,
    MODEL_GREAT_DRAGON_PANTS = MODEL_PANTS + 21,
    MODEL_DARK_SOUL_PANTS = MODEL_PANTS + 22,
    MODEL_HURRICANE_PANTS = MODEL_PANTS + 23,
    MODEL_RED_SPIRIT_PANTS = MODEL_PANTS + 24,
    MODEL_LIGHT_PLATE_PANTS = MODEL_PANTS + 25,
    MODEL_ADAMANTINE_PANTS = MODEL_PANTS + 26,
    MODEL_DARK_STEEL_PANTS = MODEL_PANTS + 27,
    MODEL_DARK_MASTER_PANTS = MODEL_PANTS + 28,
    MODEL_DRAGON_KNIGHT_PANTS = MODEL_PANTS + 29,
    MODEL_VENOM_MIST_PANTS = MODEL_PANTS + 30,
    MODEL_SYLPHID_RAY_PANTS = MODEL_PANTS + 31,
    MODEL_VOLCANO_PANTS = MODEL_PANTS + 32,
    MODEL_SUNLIGHT_PANTS = MODEL_PANTS + 33,
    MODEL_ASHCROW_PANTS = MODEL_PANTS + 34,
    MODEL_ECLIPSE_PANTS = MODEL_PANTS + 35,
    MODEL_IRIS_PANTS = MODEL_PANTS + 36,
    MODEL_VALIANT_PANTS = MODEL_PANTS + 37,
    MODEL_GLORIOUS_PANTS = MODEL_PANTS + 38,
    MODEL_MISTERY_PANTS = MODEL_PANTS + 39,
    MODEL_RED_WING_PANTS = MODEL_PANTS + 40,
    MODEL_ANCIENT_PANTS = MODEL_PANTS + 41,
    MODEL_BLACK_ROSE_PANTS = MODEL_PANTS + 42,
    MODEL_AURA_PANTS = MODEL_PANTS + 43,
    MODEL_LILIUM_PANTS = MODEL_PANTS + 44,
    MODEL_TITAN_PANTS = MODEL_PANTS + 45,
    MODEL_BRAVE_PANTS = MODEL_PANTS + 46,
    MODEL_DESTORY_PANTS = MODEL_PANTS + 47,
    MODEL_PHANTOM_PANTS = MODEL_PANTS + 48,
    MODEL_SERAPHIM_PANTS = MODEL_PANTS + 49,
    MODEL_FAITH_PANTS = MODEL_PANTS + 50,
    MODEL_PAEWANG_PANTS = MODEL_PANTS + 51,
    MODEL_HADES_PANTS = MODEL_PANTS + 52,
    MODEL_SACRED_PANTS = MODEL_PANTS + 59,
    MODEL_STORM_HARD_PANTS = MODEL_PANTS + 60,
    MODEL_PIERCING_PANTS = MODEL_PANTS + 61,
    MODEL_PHOENIX_SOUL_PANTS = MODEL_PANTS + 73,
    MODEL_BRONZE_GLOVES = MODEL_GLOVES + 0,
    MODEL_DRAGON_GLOVES = MODEL_GLOVES + 1,
    MODEL_PAD_GLOVES = MODEL_GLOVES + 2,
    MODEL_LEGENDARY_GLOVES = MODEL_GLOVES + 3,
    MODEL_BONE_GLOVES = MODEL_GLOVES + 4,
    MODEL_LEATHER_GLOVES = MODEL_GLOVES + 5,
    MODEL_SCALE_GLOVES = MODEL_GLOVES + 6,
    MODEL_SPHINX_GLOVES = MODEL_GLOVES + 7,
    MODEL_BRASS_GLOVES = MODEL_GLOVES + 8,
    MODEL_PLATE_GLOVES = MODEL_GLOVES + 9,
    MODEL_VINE_GLOVES = MODEL_GLOVES + 10,
    MODEL_SILK_GLOVES = MODEL_GLOVES + 11,
    MODEL_WIND_GLOVES = MODEL_GLOVES + 12,
    MODEL_SPIRIT_GLOVES = MODEL_GLOVES + 13,
    MODEL_GUARDIAN_GLOVES = MODEL_GLOVES + 14,
    MODEL_STORM_CROW_GLOVES = MODEL_GLOVES + 15,
    MODEL_BLACK_DRAGON_GLOVES = MODEL_GLOVES + 16,
    MODEL_DARK_PHOENIX_GLOVES = MODEL_GLOVES + 17,
    MODEL_GRAND_SOUL_GLOVES = MODEL_GLOVES + 18,
    MODEL_DIVINE_GLOVES = MODEL_GLOVES + 19,
    MODEL_THUNDER_HAWK_GLOVES = MODEL_GLOVES + 20,
    MODEL_GREAT_DRAGON_GLOVES = MODEL_GLOVES + 21,
    MODEL_DARK_SOUL_GLOVES = MODEL_GLOVES + 22,
    MODEL_HURRICANE_GLOVES = MODEL_GLOVES + 23,
    MODEL_RED_SPIRIT_GLOVES = MODEL_GLOVES + 24,
    MODEL_LIGHT_PLATE_GLOVES = MODEL_GLOVES + 25,
    MODEL_ADAMANTINE_GLOVES = MODEL_GLOVES + 26,
    MODEL_DARK_STEEL_GLOVES = MODEL_GLOVES + 27,
    MODEL_DARK_MASTER_GLOVES = MODEL_GLOVES + 28,
    MODEL_DRAGON_KNIGHT_GLOVES = MODEL_GLOVES + 29,
    MODEL_VENOM_MIST_GLOVES = MODEL_GLOVES + 30,
    MODEL_SYLPHID_RAY_GLOVES = MODEL_GLOVES + 31,
    MODEL_VOLCANO_GLOVES = MODEL_GLOVES + 32,
    MODEL_SUNLIGHT_GLOVES = MODEL_GLOVES + 33,
    MODEL_ASHCROW_GLOVES = MODEL_GLOVES + 34,
    MODEL_ECLIPSE_GLOVES = MODEL_GLOVES + 35,
    MODEL_IRIS_GLOVES = MODEL_GLOVES + 36,
    MODEL_VALIANT_GLOVES = MODEL_GLOVES + 37,
    MODEL_GLORIOUS_GLOVES = MODEL_GLOVES + 38,
    MODEL_MISTERY_GLOVES = MODEL_GLOVES + 39,
    MODEL_RED_WING_GLOVES = MODEL_GLOVES + 40,
    MODEL_ANCIENT_GLOVES = MODEL_GLOVES + 41,
    MODEL_BLACK_ROSE_GLOVES = MODEL_GLOVES + 42,
    MODEL_AURA_GLOVES = MODEL_GLOVES + 43,
    MODEL_LILIUM_GLOVES = MODEL_GLOVES + 44,
    MODEL_TITAN_GLOVES = MODEL_GLOVES + 45,
    MODEL_BRAVE_GLOVES = MODEL_GLOVES + 46,
    MODEL_DESTORY_GLOVES = MODEL_GLOVES + 47,
    MODEL_PHANTOM_GLOVES = MODEL_GLOVES + 48,
    MODEL_SERAPHIM_GLOVES = MODEL_GLOVES + 49,
    MODEL_FAITH_GLOVES = MODEL_GLOVES + 50,
    MODEL_PAEWANG_GLOVES = MODEL_GLOVES + 51,
    MODEL_HADES_GLOVES = MODEL_GLOVES + 52,
    MODEL_BRONZE_BOOTS = MODEL_BOOTS + 0,
    MODEL_DRAGON_BOOTS = MODEL_BOOTS + 1,
    MODEL_PAD_BOOTS = MODEL_BOOTS + 2,
    MODEL_LEGENDARY_BOOTS = MODEL_BOOTS + 3,
    MODEL_BONE_BOOTS = MODEL_BOOTS + 4,
    MODEL_LEATHER_BOOTS = MODEL_BOOTS + 5,
    MODEL_SCALE_BOOTS = MODEL_BOOTS + 6,
    MODEL_SPHINX_BOOTS = MODEL_BOOTS + 7,
    MODEL_BRASS_BOOTS = MODEL_BOOTS + 8,
    MODEL_PLATE_BOOTS = MODEL_BOOTS + 9,
    MODEL_VINE_BOOTS = MODEL_BOOTS + 10,
    MODEL_SILK_BOOTS = MODEL_BOOTS + 11,
    MODEL_WIND_BOOTS = MODEL_BOOTS + 12,
    MODEL_SPIRIT_BOOTS = MODEL_BOOTS + 13,
    MODEL_GUARDIAN_BOOTS = MODEL_BOOTS + 14,
    MODEL_STORM_CROW_BOOTS = MODEL_BOOTS + 15,
    MODEL_BLACK_DRAGON_BOOTS = MODEL_BOOTS + 16,
    MODEL_DARK_PHOENIX_BOOTS = MODEL_BOOTS + 17,
    MODEL_GRAND_SOUL_BOOTS = MODEL_BOOTS + 18,
    MODEL_DIVINE_BOOTS = MODEL_BOOTS + 19,
    MODEL_THUNDER_HAWK_BOOTS = MODEL_BOOTS + 20,
    MODEL_GREAT_DRAGON_BOOTS = MODEL_BOOTS + 21,
    MODEL_DARK_SOUL_BOOTS = MODEL_BOOTS + 22,
    MODEL_HURRICANE_BOOTS = MODEL_BOOTS + 23,
    MODEL_RED_SPIRIT_BOOTS = MODEL_BOOTS + 24,
    MODEL_LIGHT_PLATE_BOOTS = MODEL_BOOTS + 25,
    MODEL_ADAMANTINE_BOOTS = MODEL_BOOTS + 26,
    MODEL_DARK_STEEL_BOOTS = MODEL_BOOTS + 27,
    MODEL_DARK_MASTER_BOOTS = MODEL_BOOTS + 28,
    MODEL_DRAGON_KNIGHT_BOOTS = MODEL_BOOTS + 29,
    MODEL_VENOM_MIST_BOOTS = MODEL_BOOTS + 30,
    MODEL_SYLPHID_RAY_BOOTS = MODEL_BOOTS + 31,
    MODEL_VOLCANO_BOOTS = MODEL_BOOTS + 32,
    MODEL_SUNLIGHT_BOOTS = MODEL_BOOTS + 33,
    MODEL_ASHCROW_BOOTS = MODEL_BOOTS + 34,
    MODEL_ECLIPSE_BOOTS = MODEL_BOOTS + 35,
    MODEL_IRIS_BOOTS = MODEL_BOOTS + 36,
    MODEL_VALIANT_BOOTS = MODEL_BOOTS + 37,
    MODEL_GLORIOUS_BOOTS = MODEL_BOOTS + 38,
    MODEL_MISTERY_BOOTS = MODEL_BOOTS + 39,
    MODEL_RED_WING_BOOTS = MODEL_BOOTS + 40,
    MODEL_ANCIENT_BOOTS = MODEL_BOOTS + 41,
    MODEL_BLACK_ROSE_BOOTS = MODEL_BOOTS + 42,
    MODEL_AURA_BOOTS = MODEL_BOOTS + 43,
    MODEL_LILIUM_BOOTS = MODEL_BOOTS + 44,
    MODEL_TITAN_BOOTS = MODEL_BOOTS + 45,
    MODEL_BRAVE_BOOTS = MODEL_BOOTS + 46,
    MODEL_DESTORY_BOOTS = MODEL_BOOTS + 47,
    MODEL_PHANTOM_BOOTS = MODEL_BOOTS + 48,
    MODEL_SERAPHIM_BOOTS = MODEL_BOOTS + 49,
    MODEL_FAITH_BOOTS = MODEL_BOOTS + 50,
    MODEL_PHAEWANG_BOOTS = MODEL_BOOTS + 51,
    MODEL_HADES_BOOTS = MODEL_BOOTS + 52,
    MODEL_SACRED_BOOTS = MODEL_BOOTS + 59,
    MODEL_STORM_HARD_BOOTS = MODEL_BOOTS + 60,
    MODEL_PIERCING_BOOTS = MODEL_BOOTS + 61,
    MODEL_PHOENIX_SOUL_BOOTS = MODEL_BOOTS + 73,
    MODEL_WINGS_OF_ELF = MODEL_WING + 0,
    MODEL_WINGS_OF_HEAVEN = MODEL_WING + 1,
    MODEL_WINGS_OF_SATAN = MODEL_WING + 2,
    MODEL_WINGS_OF_SPIRITS = MODEL_WING + 3,
    MODEL_WINGS_OF_SOUL = MODEL_WING + 4,
    MODEL_WINGS_OF_DRAGON = MODEL_WING + 5,
    MODEL_WINGS_OF_DARKNESS = MODEL_WING + 6,
    MODEL_ORB_OF_TWISTING_SLASH = MODEL_WING + 7,
    MODEL_ORB_OF_HEALING = MODEL_WING + 8,
    MODEL_ORB_OF_GREATER_DEFENSE = MODEL_WING + 9,
    MODEL_ORB_OF_GREATER_DAMAGE = MODEL_WING + 10,
    MODEL_ORB_OF_SUMMONING = MODEL_WING + 11,
    MODEL_ORB_OF_RAGEFUL_BLOW = MODEL_WING + 12,
    MODEL_ORB_OF_IMPALE = MODEL_WING + 13,
    MODEL_ORB_OF_GREATER_FORTITUDE = MODEL_WING + 14,
    MODEL_JEWEL_OF_CHAOS = MODEL_WING + 15,
    MODEL_ORB_OF_FIRE_SLASH = MODEL_WING + 16,
    MODEL_ORB_OF_PENETRATION = MODEL_WING + 17,
    MODEL_ORB_OF_ICE_ARROW = MODEL_WING + 18,
    MODEL_ORB_OF_DEATH_STAB = MODEL_WING + 19,
    MODEL_SCROLL_OF_FIREBURST = MODEL_WING + 21,
    MODEL_SCROLL_OF_SUMMON = MODEL_WING + 22,
    MODEL_SCROLL_OF_CRITICAL_DAMAGE = MODEL_WING + 23,
    MODEL_SCROLL_OF_ELECTRIC_SPARK = MODEL_WING + 24,
    MODEL_PACKED_JEWEL_OF_BLESS = MODEL_WING + 30,
    MODEL_PACKED_JEWEL_OF_SOUL = MODEL_WING + 31,
    MODEL_RED_RIBBON_BOX = MODEL_WING + 32,
    MODEL_GREEN_RIBBON_BOX = MODEL_WING + 33,
    MODEL_BLUE_RIBBON_BOX = MODEL_WING + 34,
    MODEL_SCROLL_OF_FIRE_SCREAM = MODEL_WING + 35,
    MODEL_WING_OF_STORM = MODEL_WING + 36,
    MODEL_WING_OF_ETERNAL = MODEL_WING + 37,
    MODEL_WING_OF_ILLUSION = MODEL_WING + 38,
    MODEL_WING_OF_RUIN = MODEL_WING + 39,
    MODEL_CAPE_OF_EMPEROR = MODEL_WING + 40,
    MODEL_WING_OF_CURSE = MODEL_WING + 41,
    MODEL_WINGS_OF_DESPAIR = MODEL_WING + 42,
    MODEL_WING_OF_DIMENSION = MODEL_WING + 43,
    MODEL_CRYSTAL_OF_DESTRUCTION = MODEL_WING + 44,
    MODEL_CRYSTAL_OF_MULTI_SHOT = MODEL_WING + 45,
    MODEL_CRYSTAL_OF_RECOVERY = MODEL_WING + 46,
    MODEL_CRYSTAL_OF_FLAME_STRIKE = MODEL_WING + 47,
    MODEL_SCROLL_OF_CHAOTIC_DISEIER = MODEL_WING + 48,
    MODEL_CAPE_OF_FIGHTER = MODEL_WING + 49,
    MODEL_CAPE_OF_OVERRULE = MODEL_WING + 50,
    MODEL_SEED_FIRE = MODEL_WING + 60,
    MODEL_SEED_WATER = MODEL_WING + 61,
    MODEL_SEED_ICE = MODEL_WING + 62,
    MODEL_SEED_WIND = MODEL_WING + 63,
    MODEL_SEED_LIGHTNING = MODEL_WING + 64,
    MODEL_SEED_EARTH = MODEL_WING + 65,
    MODEL_SPHERE_MONO = MODEL_WING + 70,
    MODEL_SPHERE_DI = MODEL_WING + 71,
    MODEL_SPHERE_TRI = MODEL_WING + 72,
    MODEL_SPHERE_4 = MODEL_WING + 73,
    MODEL_SPHERE_5 = MODEL_WING + 74,
    MODEL_SEED_SPHERE_FIRE_1 = MODEL_WING + 100,
    MODEL_SEED_SPHERE_WATER_1 = MODEL_WING + 101,
    MODEL_SEED_SPHERE_ICE_1 = MODEL_WING + 102,
    MODEL_SEED_SPHERE_WIND_1 = MODEL_WING + 103,
    MODEL_SEED_SPHERE_LIGHTNING_1 = MODEL_WING + 104,
    MODEL_SEED_SPHERE_EARTH_1 = MODEL_WING + 105,
    MODEL_SEED_SPHERE_FIRE_2 = MODEL_WING + 106,
    MODEL_SEED_SPHERE_WATER_2 = MODEL_WING + 107,
    MODEL_SEED_SPHERE_ICE_2 = MODEL_WING + 108,
    MODEL_SEED_SPHERE_WIND_2 = MODEL_WING + 109,
    MODEL_SEED_SPHERE_LIGHTNING_2 = MODEL_WING + 110,
    MODEL_SEED_SPHERE_EARTH_2 = MODEL_WING + 111,
    MODEL_SEED_SPHERE_FIRE_3 = MODEL_WING + 112,
    MODEL_SEED_SPHERE_WATER_3 = MODEL_WING + 113,
    MODEL_SEED_SPHERE_ICE_3 = MODEL_WING + 114,
    MODEL_SEED_SPHERE_WIND_3 = MODEL_WING + 115,
    MODEL_SEED_SPHERE_LIGHTNING_3 = MODEL_WING + 116,
    MODEL_SEED_SPHERE_EARTH_3 = MODEL_WING + 117,
    MODEL_SEED_SPHERE_FIRE_4 = MODEL_WING + 118,
    MODEL_SEED_SPHERE_WATER_4 = MODEL_WING + 119,
    MODEL_SEED_SPHERE_ICE_4 = MODEL_WING + 120,
    MODEL_SEED_SPHERE_WIND_4 = MODEL_WING + 121,
    MODEL_SEED_SPHERE_LIGHTNING_4 = MODEL_WING + 122,
    MODEL_SEED_SPHERE_EARTH_4 = MODEL_WING + 123,
    MODEL_SEED_SPHERE_FIRE_5 = MODEL_WING + 124,
    MODEL_SEED_SPHERE_WATER_5 = MODEL_WING + 125,
    MODEL_SEED_SPHERE_ICE_5 = MODEL_WING + 126,
    MODEL_SEED_SPHERE_WIND_5 = MODEL_WING + 127,
    MODEL_SEED_SPHERE_LIGHTNING_5 = MODEL_WING + 128,
    MODEL_SEED_SPHERE_EARTH_5 = MODEL_WING + 129,
    MODEL_PACKED_JEWEL_OF_LIFE = MODEL_WING + 136,
    MODEL_PACKED_JEWEL_OF_CREATION = MODEL_WING + 137,
    MODEL_PACKED_JEWEL_OF_GUARDIAN = MODEL_WING + 138,
    MODEL_PACKED_GEMSTONE = MODEL_WING + 139,
    MODEL_PACKED_JEWEL_OF_HARMONY = MODEL_WING + 140,
    MODEL_PACKED_JEWEL_OF_CHAOS = MODEL_WING + 141,
    MODEL_PACKED_LOWER_REFINE_STONE = MODEL_WING + 142,
    MODEL_PACKED_HIGHER_REFINE_STONE = MODEL_WING + 143,
    MODEL_GUARDIAN_ANGEL = MODEL_HELPER + 0,
    MODEL_IMP = MODEL_HELPER + 1,
    MODEL_HORN_OF_UNIRIA = MODEL_HELPER + 2,
    MODEL_HORN_OF_DINORANT = MODEL_HELPER + 3,
    MODEL_DARK_HORSE_ITEM = MODEL_HELPER + 4,
    MODEL_DARK_RAVEN_ITEM = MODEL_HELPER + 5,
    MODEL_RING_OF_ICE = MODEL_HELPER + 8,
    MODEL_RING_OF_POISON = MODEL_HELPER + 9,
    MODEL_TRANSFORMATION_RING = MODEL_HELPER + 10,
    MODEL_LIFE_STONE_ITEM = MODEL_HELPER + 11,
    MODEL_PENDANT_OF_LIGHTING = MODEL_HELPER + 12,
    MODEL_PENDANT_OF_FIRE = MODEL_HELPER + 13,
    MODEL_LOCHS_FEATHER = MODEL_HELPER + 14,
    MODEL_FRUITS = MODEL_HELPER + 15,
    MODEL_SCROLL_OF_ARCHANGEL = MODEL_HELPER + 16,
    MODEL_BLOOD_BONE = MODEL_HELPER + 17,
    MODEL_INVISIBILITY_CLOAK = MODEL_HELPER + 18,
    MODEL_WEAPON_OF_ARCHANGEL = MODEL_HELPER + 19,
    MODEL_WIZARDS_RING = MODEL_HELPER + 20,
    MODEL_RING_OF_FIRE = MODEL_HELPER + 21,
    MODEL_RING_OF_EARTH = MODEL_HELPER + 22,
    MODEL_RING_OF_WIND = MODEL_HELPER + 23,
    MODEL_RING_OF_MAGIC = MODEL_HELPER + 24,
    MODEL_PENDANT_OF_ICE = MODEL_HELPER + 25,
    MODEL_PENDANT_OF_WIND = MODEL_HELPER + 26,
    MODEL_PENDANT_OF_WATER = MODEL_HELPER + 27,
    MODEL_PENDANT_OF_ABILITY = MODEL_HELPER + 28,
    MODEL_ARMOR_OF_GUARDSMAN = MODEL_HELPER + 29,
    MODEL_CAPE_OF_LORD = MODEL_HELPER + 30,
    MODEL_SPIRIT = MODEL_HELPER + 31,
    MODEL_SPLINTER_OF_ARMOR = MODEL_HELPER + 32,
    MODEL_BLESS_OF_GUARDIAN = MODEL_HELPER + 33,
    MODEL_CLAW_OF_BEAST = MODEL_HELPER + 34,
    MODEL_FRAGMENT_OF_HORN = MODEL_HELPER + 35,
    MODEL_BROKEN_HORN = MODEL_HELPER + 36,
    MODEL_HORN_OF_FENRIR = MODEL_HELPER + 37,
    MODEL_MOONSTONE_PENDANT = MODEL_HELPER + 38,
    MODEL_ELITE_TRANSFER_SKELETON_RING = MODEL_HELPER + 39,
    MODEL_JACK_OLANTERN_TRANSFORMATION_RING = MODEL_HELPER + 40,
    MODEL_CHRISTMAS_TRANSFORMATION_RING = MODEL_HELPER + 41,
    MODEL_GAME_MASTER_TRANSFORMATION_RING = MODEL_HELPER + 42,
    MODEL_OLD_SCROLL = MODEL_HELPER + 49,
    MODEL_ILLUSION_SORCERER_COVENANT = MODEL_HELPER + 50,
    MODEL_SCROLL_OF_BLOOD = MODEL_HELPER + 51,
    MODEL_FLAME_OF_CONDOR = MODEL_HELPER + 52,
    MODEL_FEATHER_OF_CONDOR = MODEL_HELPER + 53,
    MODEL_DEMON = MODEL_HELPER + 64,
    MODEL_SPIRIT_OF_GUARDIAN = MODEL_HELPER + 65,
    MODEL_PET_RUDOLF = MODEL_HELPER + 67,
    MODEL_SNOWMAN_TRANSFORMATION_RING = MODEL_HELPER + 68,
    MODEL_PANDA_TRANSFORMATION_RING = MODEL_HELPER + 76,
    MODEL_PET_PANDA = MODEL_HELPER + 80,
    MODEL_PET_UNICORN = MODEL_HELPER + 106,
    MODEL_SKELETON_TRANSFORMATION_RING = MODEL_HELPER + 122,
    MODEL_PET_SKELETON = MODEL_HELPER + 123,
    MODEL_TRANSFORMATION_RING1 = MODEL_HELPER + 163,
    MODEL_TRANSFORMATION_RING2 = MODEL_HELPER + 164,
    MODEL_TRANSFORMATION_RING3 = MODEL_HELPER + 165,
    MODEL_APPLE = MODEL_POTION + 0,
    MODEL_SMALL_HEALING_POTION = MODEL_POTION + 1,
    MODEL_MEDIUM_HEALING_POTION = MODEL_POTION + 2,
    MODEL_LARGE_HEALING_POTION = MODEL_POTION + 3,
    MODEL_SMALL_MANA_POTION = MODEL_POTION + 4,
    MODEL_MEDIUM_MANA_POTION = MODEL_POTION + 5,
    MODEL_LARGE_MANA_POTION = MODEL_POTION + 6,
    MODEL_SIEGE_POTION = MODEL_POTION + 7,
    MODEL_ANTIDOTE = MODEL_POTION + 8,
    MODEL_ALE = MODEL_POTION + 9,
    MODEL_TOWN_PORTAL_SCROLL = MODEL_POTION + 10,
    MODEL_BOX_OF_LUCK = MODEL_POTION + 11,
    MODEL_JEWEL_OF_BLESS = MODEL_POTION + 13,
    MODEL_JEWEL_OF_SOUL = MODEL_POTION + 14,
    MODEL_ZEN = MODEL_POTION + 15,
    MODEL_JEWEL_OF_LIFE = MODEL_POTION + 16,
    MODEL_DEVILS_EYE = MODEL_POTION + 17,
    MODEL_DEVILS_KEY = MODEL_POTION + 18,
    MODEL_DEVILS_INVITATION = MODEL_POTION + 19,
    MODEL_JEWEL_OF_CREATION = MODEL_POTION + 22,
    MODEL_SCROLL_OF_EMPEROR_RING_OF_HONOR = MODEL_POTION + 23,
    MODEL_BROKEN_SWORD_DARK_STONE = MODEL_POTION + 24,
    MODEL_TEAR_OF_ELF = MODEL_POTION + 25,
    MODEL_SOUL_SHARD_OF_WIZARD = MODEL_POTION + 26,
    MODEL_LOST_MAP = MODEL_POTION + 28,
    MODEL_SYMBOL_OF_KUNDUN = MODEL_POTION + 29,
    MODEL_JEWEL_OF_GUARDIAN = MODEL_POTION + 31,
    MODEL_PINK_CHOCOLATE_BOX = MODEL_POTION + 32,
    MODEL_RED_CHOCOLATE_BOX = MODEL_POTION + 33,
    MODEL_BLUE_CHOCOLATE_BOX = MODEL_POTION + 34,
    MODEL_SMALL_SHIELD_POTION = MODEL_POTION + 35,
    MODEL_MEDIUM_SHIELD_POTION = MODEL_POTION + 36,
    MODEL_LARGE_SHIELD_POTION = MODEL_POTION + 37,
    MODEL_SMALL_COMPLEX_POTION = MODEL_POTION + 38,
    MODEL_MEDIUM_COMPLEX_POTION = MODEL_POTION + 39,
    MODEL_LARGE_COMPLEX_POTION = MODEL_POTION + 40,
    MODEL_GEMSTONE = MODEL_POTION + 41,
    MODEL_JEWEL_OF_HARMONY = MODEL_POTION + 42,
    MODEL_LOWER_REFINE_STONE = MODEL_POTION + 43,
    MODEL_HIGHER_REFINE_STONE = MODEL_POTION + 44,
    MODEL_PUMPKIN_OF_LUCK = MODEL_POTION + 45,
    MODEL_JACK_OLANTERN_BLESSINGS = MODEL_POTION + 46,
    MODEL_JACK_OLANTERN_WRATH = MODEL_POTION + 47,
    MODEL_JACK_OLANTERN_CRY = MODEL_POTION + 48,
    MODEL_JACK_OLANTERN_FOOD = MODEL_POTION + 49,
    MODEL_JACK_OLANTERN_DRINK = MODEL_POTION + 50,
    MODEL_CHRISTMAS_STAR = MODEL_POTION + 51,
    MODEL_GM_GIFT = MODEL_POTION + 52,
    MODEL_FIRECRACKER = MODEL_POTION + 63,
    MODEL_FLAME_OF_DEATH_BEAM_KNIGHT = MODEL_POTION + 65,
    MODEL_HORN_OF_HELL_MAINE = MODEL_POTION + 66,
    MODEL_FEATHER_OF_DARK_PHOENIX = MODEL_POTION + 67,
    MODEL_EYE_OF_ABYSSAL = MODEL_POTION + 68,
    MODEL_CHERRY_BLOSSOM_PLAYBOX = MODEL_POTION + 84,
    MODEL_CHERRY_BLOSSOM_WINE = MODEL_POTION + 85,
    MODEL_CHERRY_BLOSSOM_RICE_CAKE = MODEL_POTION + 86,
    MODEL_CHERRY_BLOSSOM_FLOWER_PETAL = MODEL_POTION + 87,
    MODEL_GOLDEN_CHERRY_BLOSSOM_BRANCH = MODEL_POTION + 90,
    MODEL_CHRISTMAS_FIRECRACKER = MODEL_POTION + 99,
    MODEL_SUSPICIOUS_SCRAP_OF_PAPER = MODEL_POTION + 101,
    MODEL_GAIONS_ORDER = MODEL_POTION + 102,
    MODEL_FIRST_SECROMICON_FRAGMENT = MODEL_POTION + 103,
    MODEL_SECOND_SECROMICON_FRAGMENT = MODEL_POTION + 104,
    MODEL_THIRD_SECROMICON_FRAGMENT = MODEL_POTION + 105,
    MODEL_FOURTH_SECROMICON_FRAGMENT = MODEL_POTION + 106,
    MODEL_FIFTH_SECROMICON_FRAGMENT = MODEL_POTION + 107,
    MODEL_SIXTH_SECROMICON_FRAGMENT = MODEL_POTION + 108,
    MODEL_COMPLETE_SECROMICON = MODEL_POTION + 109,
    MODEL_SCROLL_OF_POISON = MODEL_ETC + 0,
    MODEL_SCROLL_OF_METEORITE = MODEL_ETC + 1,
    MODEL_SCROLL_OF_LIGHTING = MODEL_ETC + 2,
    MODEL_SCROLL_OF_FIRE_BALL = MODEL_ETC + 3,
    MODEL_SCROLL_OF_FLAME = MODEL_ETC + 4,
    MODEL_SCROLL_OF_TELEPORT = MODEL_ETC + 5,
    MODEL_SCROLL_OF_ICE = MODEL_ETC + 6,
    MODEL_SCROLL_OF_TWISTER = MODEL_ETC + 7,
    MODEL_SCROLL_OF_EVIL_SPIRIT = MODEL_ETC + 8,
    MODEL_SCROLL_OF_HELLFIRE = MODEL_ETC + 9,
    MODEL_SCROLL_OF_POWER_WAVE = MODEL_ETC + 10,
    MODEL_SCROLL_OF_AQUA_BEAM = MODEL_ETC + 11,
    MODEL_SCROLL_OF_COMETFALL = MODEL_ETC + 12,
    MODEL_SCROLL_OF_INFERNO = MODEL_ETC + 13,
    MODEL_SCROLL_OF_TELEPORT_ALLY = MODEL_ETC + 14,
    MODEL_SCROLL_OF_SOUL_BARRIER = MODEL_ETC + 15,
    MODEL_SCROLL_OF_DECAY = MODEL_ETC + 16,
    MODEL_SCROLL_OF_ICE_STORM = MODEL_ETC + 17,
    MODEL_SCROLL_OF_NOVA = MODEL_ETC + 18,
    MODEL_CHAIN_LIGHTNING_PARCHMENT = MODEL_ETC + 19,
    MODEL_DRAIN_LIFE_PARCHMENT = MODEL_ETC + 20,
    MODEL_LIGHTNING_SHOCK_PARCHMENT = MODEL_ETC + 21,
    MODEL_DAMAGE_REFLECTION_PARCHMENT = MODEL_ETC + 22,
    MODEL_BERSERKER_PARCHMENT = MODEL_ETC + 23,
    MODEL_SLEEP_PARCHMENT = MODEL_ETC + 24,
    MODEL_WEAKNESS_PARCHMENT = MODEL_ETC + 26,
    MODEL_INNOVATION_PARCHMENT = MODEL_ETC + 27,
    MODEL_SCROLL_OF_WIZARDRY_ENHANCE = MODEL_ETC + 28,
    MODEL_SCROLL_OF_GIGANTIC_STORM = MODEL_ETC + 29,
    MODEL_CHAIN_DRIVE_PARCHMENT = MODEL_ETC + 30,
    MODEL_DARK_SIDE_PARCHMENT = MODEL_ETC + 31,
    MODEL_DRAGON_ROAR_PARCHMENT = MODEL_ETC + 32,
    MODEL_DRAGON_SLASHER_PARCHMENT = MODEL_ETC + 33,
    MODEL_IGNORE_DEFENSE_PARCHMENT = MODEL_ETC + 34,
    MODEL_INCREASE_HEALTH_PARCHMENT = MODEL_ETC + 35,
    MODEL_INCREASE_BLOCK_PARCHMENT = MODEL_ETC + 36,
};

enum EMonsterModelType : int
{
    MONSTER_MODEL_UNDEFINED = -1,
    MONSTER_MODEL_BULL_FIGHTER = 0,
    MONSTER_MODEL_HOUND = 1,
    MONSTER_MODEL_BUDGE_DRAGON = 2,
    MONSTER_MODEL_DARK_KNIGHT = 3,
    MONSTER_MODEL_LICH = 4,
    MONSTER_MODEL_GIANT = 5,
    MONSTER_MODEL_LARVA = 6,
    MONSTER_MODEL_GHOST = 7,
    MONSTER_MODEL_HELL_SPIDER = 8,
    MONSTER_MODEL_SPIDER = 9,
    MONSTER_MODEL_CYCLOPS = 10,
    MONSTER_MODEL_GORGON = 11,
    MONSTER_MODEL_YETI = 12,
    MONSTER_MODEL_ELITE_YETI = 13,
    MONSTER_MODEL_ASSASSIN = 14,
    MONSTER_MODEL_ICE_MONSTER = 15,
    MONSTER_MODEL_HOMMERD = 16,
    MONSTER_MODEL_WORM = 17,
    MONSTER_MODEL_ICE_QUEEN = 18,
    MONSTER_MODEL_GOBLIN = 19,
    MONSTER_MODEL_CHAIN_SCORPION = 20,
    MONSTER_MODEL_BEETLE_MONSTER = 21,
    MONSTER_MODEL_HUNTER = 22,
    MONSTER_MODEL_FOREST_MONSTER = 23,
    MONSTER_MODEL_AGON = 24,
    MONSTER_MODEL_STONE_GOLEM = 25,
    MONSTER_MODEL_DEVIL = 26,
    MONSTER_MODEL_BALROG = 27,
    MONSTER_MODEL_SHADOW = 28,
    MONSTER_MODEL_DEATH_KNIGHT = 29,
    MONSTER_MODEL_DEATH_COW = 30,
    MONSTER_MODEL_DRAGON = 31,
    MONSTER_MODEL_BALI = 32,
    MONSTER_MODEL_BAHAMUT = 33,
    MONSTER_MODEL_VEPAR = 34,
    MONSTER_MODEL_VALKYRIE = 35,
    MONSTER_MODEL_LIZARD = 36,
    MONSTER_MODEL_HYDRA = 37,
    MONSTER_MODEL_SEA_WORM = 38,
    MONSTER_MODEL_TITAN = 39,
    MONSTER_MODEL_SOLDIER = 40,
    MONSTER_MODEL_GOLDEN_WHEEL = 41,
    MONSTER_MODEL_TANTALLOS = 42,
    MONSTER_MODEL_BLOODY_WOLF = 43,
    MONSTER_MODEL_BEAM_KNIGHT = 44,
    MONSTER_MODEL_MUTANT = 45,
    MONSTER_MODEL_ORC_ARCHER = 46,
    MONSTER_MODEL_ORC = 47,
    MONSTER_MODEL_CURSED_KING = 48,
    MONSTER_MODEL_MOLT = 49,
    MONSTER_MODEL_ALQUAMOS = 50,
    MONSTER_MODEL_QUEEN_RAINER = 51,
    MONSTER_MODEL_CRUST = 52,
    MONSTER_MODEL_PHANTOM_KNIGHT = 53,
    MONSTER_MODEL_DRAKAN = 54,
    MONSTER_MODEL_DARK_PHOENIX_SHIELD = 55,
    MONSTER_MODEL_DARK_PHOENIX = 56,
    MONSTER_MODEL_RED_SKELETON_KNIGHT = 57,
    MONSTER_MODEL_GIANT_OGRE = 58,
    MONSTER_MODEL_DARK_SKULL_SOLDIER = 59,
    MONSTER_MODEL_STATUE_OF_SAINT = 60,
    MONSTER_MODEL_CASTLE_GATE = 61,
    MONSTER_MODEL_MAGIC_SKELETON = 62,
    MONSTER_MODEL_DEATH_ANGEL = 63,
    MONSTER_MODEL_ILLUSION_OF_KUNDUN = 64,
    MONSTER_MODEL_BLOOD_SOLDIER = 65,
    MONSTER_MODEL_AEGIS = 66,
    MONSTER_MODEL_DEATH_CENTURION = 67,
    MONSTER_MODEL_NECRON = 68,
    MONSTER_MODEL_SHRIKER = 69,
    MONSTER_MODEL_CHAOSCASTLE_KNIGHT = 70,
    MONSTER_MODEL_CHAOSCASTLE_ELF = 71,
    MONSTER_MODEL_CHAOSCASTLE_WIZARD = 72,
    MONSTER_MODEL_CASTLE_GATE1 = 73,
    MONSTER_MODEL_GUARDIAN_STATUE = 74,
    MONSTER_MODEL_GREAT_DRAKAN = 75,
    MONSTER_MODEL_BATTLE_GUARD1 = 76,
    MONSTER_MODEL_BATTLE_GUARD2 = 77,
    MONSTER_MODEL_GOLDEN_GOBLIN = 78,
    MONSTER_MODEL_CANON_TOWER = 79,
    MONSTER_MODEL_GOLDEN_LIZARD_KING = 80,
    MONSTER_MODEL_LIZARD_WARRIOR = 81,
    MONSTER_MODEL_FIRE_GOLEM = 82,
    MONSTER_MODEL_QUEEN_BEE = 83,
    MONSTER_MODEL_POISON_GOLEM = 84,
    MONSTER_MODEL_AXE_HERO = 85,
    MONSTER_MODEL_LIFE_STONE = 86,
    MONSTER_MODEL_EROHIM = 87,
    MONSTER_MODEL_RED_SKELETON_KNIGHT_1 = 88,
    MONSTER_MODEL_BALGASS = 89,
    MONSTER_MODEL_CHIEF_SKELETON_WARRIOR_2 = 90,
    MONSTER_MODEL_BALRAM = 91,
    MONSTER_MODEL_DARK_ELF_1 = 92,
    MONSTER_MODEL_DEATH_SPIRIT = 93,
    MONSTER_MODEL_SORAM = 94,
    MONSTER_MODEL_WEREWOLF_HERO = 95,
    MONSTER_MODEL_VALAM = 96,
    MONSTER_MODEL_SOLAM = 97,
    MONSTER_MODEL_SCOUT = 98,
    MONSTER_MODEL_BALLISTA = 99,
    MONSTER_MODEL_WITCH_QUEEN = 100,
    MONSTER_MODEL_GOLDEN_STONE_GOLEM = 101,
    MONSTER_MODEL_DEATH_RIDER = 102,
    MONSTER_MODEL_FOREST_ORC = 103,
    MONSTER_MODEL_DEATH_TREE = 104,
    MONSTER_MODEL_HELL_MAINE = 105,
    MONSTER_MODEL_BERSERK = 106,
    MONSTER_MODEL_SPLINTER_WOLF = 107,
    MONSTER_MODEL_IRON_RIDER = 108,
    MONSTER_MODEL_SATYROS = 109,
    MONSTER_MODEL_BLADE_HUNTER = 110,
    MONSTER_MODEL_KENTAUROS = 111,
    MONSTER_MODEL_GIGANTIS = 112,
    MONSTER_MODEL_GENOCIDER = 113,
    MONSTER_MODEL_PERSONA = 114,
    MONSTER_MODEL_TWIN_TAIL = 115,
    MONSTER_MODEL_DREADFEAR = 116,
    MONSTER_MODEL_RED_SKELETON_KNIGHT_4 = 117,
    MONSTER_MODEL_MAYA_HAND_LEFT = 118,
    MONSTER_MODEL_MAYA_HAND_RIGHT = 119,
    MONSTER_MODEL_MAYA = 120,
    MONSTER_MODEL_DARK_SKULL_SOLDIER_5 = 121,
    MONSTER_MODEL_POUCH_OF_BLESSING = 122,
    MONSTER_MODEL_ILLUSION_SORCERER_SPIRIT_LIGHTNING = 123,
    MONSTER_MODEL_ILLUSION_SORCERER_SPIRIT_ICE = 124,
    MONSTER_MODEL_ILLUSION_SORCERER_SPIRIT_POISON = 125,
    MONSTER_MODEL_DARK_ELF = 126,
    MONSTER_MODEL_LUNAR_RABBIT = 127,
    MONSTER_MODEL_RABBIT = 128,
    MONSTER_MODEL_BUTTERFLY = 129,
    MONSTER_MODEL_HIDEOUS_RABBIT = 130,
    MONSTER_MODEL_WEREWOLF2 = 131,
    MONSTER_MODEL_CURSED_LICH = 132,
    MONSTER_MODEL_TOTEM_GOLEM = 133,
    MONSTER_MODEL_GRIZZLY = 134,
    MONSTER_MODEL_CAPTAIN_GRIZZLY = 135,
    MONSTER_MODEL_SAPIUNUS = 136,
    MONSTER_MODEL_SAPIDUO = 137,
    MONSTER_MODEL_SAPITRES = 138,
    MONSTER_MODEL_SHADOW_PAWN = 139,
    MONSTER_MODEL_SHADOW_KNIGHT = 140,
    MONSTER_MODEL_SHADOW_LOOK = 141,
    MONSTER_MODEL_NAPIN = 142,
    MONSTER_MODEL_GHOST_NAPIN = 143,
    MONSTER_MODEL_BLAZE_NAPIN = 144,
    MONSTER_MODEL_ICE_WALKER = 145,
    MONSTER_MODEL_GIANT_MAMMOTH = 146,
    MONSTER_MODEL_ICE_GIANT = 147,
    MONSTER_MODEL_COOLUTIN = 148,
    MONSTER_MODEL_IRON_KNIGHT = 149,
    MONSTER_MODEL_SELUPAN = 150,
    MONSTER_MODEL_SPIDER_EGGS_1 = 151,
    MONSTER_MODEL_SPIDER_EGGS_2 = 152,
    MONSTER_MODEL_SPIDER_EGGS_3 = 153,
    MONSTER_MODEL_FIRE_FLAME_GHOST = 154,
    MONSTER_MODEL_CURSED_SANTA = 155,
    MONSTER_MODEL_EVIL_GOBLIN = 156,
    MONSTER_MODEL_ZOMBIE_FIGHTER = 157,
    MONSTER_MODEL_GLADIATOR = 158,
    MONSTER_MODEL_SLAUGTHERER = 159,
    MONSTER_MODEL_BLOOD_ASSASSIN = 160,
    MONSTER_MODEL_CRUEL_BLOOD_ASSASSIN = 161,
    MONSTER_MODEL_LAVA_GIANT = 162,
    MONSTER_MODEL_BURNING_LAVA_GIANT = 163,
    MONSTER_MODEL_GAYION = 164,
    MONSTER_MODEL_JERRY = 165,
    MONSTER_MODEL_RAYMOND = 166,
    MONSTER_MODEL_LUCAS = 167,
    MONSTER_MODEL_FRED = 168,
    MONSTER_MODEL_HAMMERIZE = 169,
    MONSTER_MODEL_DUAL_BERSERKER = 170,
    MONSTER_MODEL_DEVIL_LORD = 171,
    MONSTER_MODEL_QUARTER_MASTER = 172,
    MONSTER_MODEL_COMBAT_INSTRUCTOR = 173,
    MONSTER_MODEL_ATICLES_HEAD = 174,
    MONSTER_MODEL_DARK_GHOST = 175,
    MONSTER_MODEL_BANSHEE = 176,
    MONSTER_MODEL_HEAD_MOUNTER = 177,
    MONSTER_MODEL_DEFENDER = 178,
    MONSTER_MODEL_FORSAKER = 179,
    MONSTER_MODEL_OCELOT = 180,
    MONSTER_MODEL_ERIC = 181,
    MONSTER_MODEL_DEATH_ANGEL_3 = 182,
    MONSTER_MODEL_EVIL_GATE = 183,
    MONSTER_MODEL_LION_GATE = 184,
    MONSTER_MODEL_STATUE = 185,
    MONSTER_MODEL_STAR_GATE = 186,
    MONSTER_MODEL_RUSH_GATE = 187,
    MONSTER_MODEL_SCHRIKER_3 = 188,
    MONSTER_MODEL_MAD_BUTCHER = 189,
    MONSTER_MODEL_TERRIBLE_BUTCHER = 190,
    MONSTER_MODEL_DOPPELGANGER = 191,
    MONSTER_MODEL_MEDUSA = 192,
    MONSTER_MODEL_BLOODY_ORC = 193,
    MONSTER_MODEL_BLOODY_DEATH_RIDER = 194,
    MONSTER_MODEL_BLOODY_GOLEM = 195,
    MONSTER_MODEL_BLOODY_WITCH_QUEEN = 196,
    MONSTER_MODEL_BERSERKER_WARRIOR = 197,
    MONSTER_MODEL_KENTAUROS_WARRIOR = 198,
    MONSTER_MODEL_GIGANTIS_WARRIOR = 199,
    MONSTER_MODEL_SOCCERBALL = 200,
    MONSTER_MODEL_SAPI_QUEEN = 201,
    MONSTER_MODEL_ICE_NAPIN = 202,
    MONSTER_MODEL_SHADOW_MASTER = 203,
    MONSTER_MODEL_WOLF_STATUS = 204,
    MONSTER_MODEL_DARK_MAMMOTH = 205,
    MONSTER_MODEL_DARK_GIANT = 206,
    MONSTER_MODEL_DARK_COOLUTIN = 207,
    MONSTER_MODEL_DARK_IRON_KNIGHT = 208,
    MONSTER_MODEL_VENOMOUS_CHAIN_SCORPION = 209,
    MONSTER_MODEL_BONE_SCORPION = 210,
    MONSTER_MODEL_ORCUS = 211,
    MONSTER_MODEL_GOLLOCK = 212,
    MONSTER_MODEL_CRYPTA = 213,
    MONSTER_MODEL_CRYPOS = 214,
    MONSTER_MODEL_CONDRA = 215,
    MONSTER_MODEL_NACONDRA = 216,
    MONSTER_MODEL_COUNT, // keep last: number of monster models
};

enum
{
    MODEL_BULL_FIGHTER = MODEL_MONSTER01 + 0,
    MODEL_HOUND = MODEL_MONSTER01 + 1,
    MODEL_BUDGE_DRAGON = MODEL_MONSTER01 + 2,
    MODEL_DARK_KNIGHT = MODEL_MONSTER01 + 3,
    MODEL_LICH = MODEL_MONSTER01 + 4,
    MODEL_GIANT = MODEL_MONSTER01 + 5,
    MODEL_LARVA = MODEL_MONSTER01 + 6,
    MODEL_GHOST_MONSTER = MODEL_MONSTER01 + 7,
    MODEL_HELL_SPIDER = MODEL_MONSTER01 + 8,
    MODEL_SPIDER = MODEL_MONSTER01 + 9,
    MODEL_CYCLOPS = MODEL_MONSTER01 + 10,
    MODEL_GORGON = MODEL_MONSTER01 + 11,
    MODEL_YETI = MODEL_MONSTER01 + 12,
    MODEL_ELITE_YETI = MODEL_MONSTER01 + 13,
    MODEL_ASSASSIN = MODEL_MONSTER01 + 14,
    MODEL_ICE_MONSTER = MODEL_MONSTER01 + 15,
    MODEL_HOMMERD = MODEL_MONSTER01 + 16,
    MODEL_WORM = MODEL_MONSTER01 + 17,
    MODEL_ICE_QUEEN = MODEL_MONSTER01 + 18,
    MODEL_GOBLIN = MODEL_MONSTER01 + 19,
    MODEL_CHAIN_SCORPION = MODEL_MONSTER01 + 20,
    MODEL_BEETLE_MONSTER = MODEL_MONSTER01 + 21,
    MODEL_HUNTER = MODEL_MONSTER01 + 22,
    MODEL_FOREST_MONSTER = MODEL_MONSTER01 + 23,
    MODEL_AGON = MODEL_MONSTER01 + 24,
    MODEL_STONE_GOLEM = MODEL_MONSTER01 + 25,
    MODEL_DEVIL = MODEL_MONSTER01 + 26,
    MODEL_BALROG = MODEL_MONSTER01 + 27,
    MODEL_SHADOW = MODEL_MONSTER01 + 28,
    MODEL_DEATH_KNIGHT = MODEL_MONSTER01 + 29,
    MODEL_DEATH_COW = MODEL_MONSTER01 + 30,
    MODEL_DRAGON_ = MODEL_MONSTER01 + 31,
    MODEL_BALI = MODEL_MONSTER01 + 32,
    MODEL_BAHAMUT = MODEL_MONSTER01 + 33,
    MODEL_VEPAR = MODEL_MONSTER01 + 34,
    MODEL_VALKYRIE = MODEL_MONSTER01 + 35,
    MODEL_LIZARD = MODEL_MONSTER01 + 36,
    MODEL_HYDRA = MODEL_MONSTER01 + 37,
    MODEL_SEA_WORM = MODEL_MONSTER01 + 38,
    MODEL_TITAN = MODEL_MONSTER01 + 39,
    MODEL_SOLDIER = MODEL_MONSTER01 + 40,
    MODEL_GOLDEN_WHEEL = MODEL_MONSTER01 + 41,
    MODEL_TANTALLOS = MODEL_MONSTER01 + 42,
    MODEL_BLOODY_WOLF = MODEL_MONSTER01 + 43,
    MODEL_BEAM_KNIGHT = MODEL_MONSTER01 + 44,
    MODEL_MUTANT = MODEL_MONSTER01 + 45,
    MODEL_ORC_ARCHER = MODEL_MONSTER01 + 46,
    MODEL_ORC = MODEL_MONSTER01 + 47,
    MODEL_CURSED_KING = MODEL_MONSTER01 + 48,
    MODEL_MOLT = MODEL_MONSTER01 + 49,
    MODEL_ALQUAMOS = MODEL_MONSTER01 + 50,
    MODEL_QUEEN_RAINER = MODEL_MONSTER01 + 51,
    MODEL_CRUST = MODEL_MONSTER01 + 52,
    MODEL_PHANTOM_KNIGHT = MODEL_MONSTER01 + 53,
    MODEL_DRAKAN = MODEL_MONSTER01 + 54,
    MODEL_DARK_PHEONIX_SHIELD = MODEL_MONSTER01 + 55,
    MODEL_DARK_PHEONIX = MODEL_MONSTER01 + 56,
    MODEL_RED_SKELETON_KNIGHT = MODEL_MONSTER01 + 57,
    MODEL_GIANT_OGRE = MODEL_MONSTER01 + 58,
    MODEL_DARK_SKULL_SOLDIER = MODEL_MONSTER01 + 59,
    MODEL_STATUE_OF_SAINT = MODEL_MONSTER01 + 60,
    MODEL_CASTLE_GATE = MODEL_MONSTER01 + 61,
    MODEL_MAGIC_SKELETON = MODEL_MONSTER01 + 62,
    MODEL_DEATH_ANGEL = MODEL_MONSTER01 + 63,
    MODEL_ILLUSION_OF_KUNDUN = MODEL_MONSTER01 + 64,
    MODEL_BLOOD_SOLDIER = MODEL_MONSTER01 + 65,
    MODEL_AEGIS = MODEL_MONSTER01 + 66,
    MODEL_DEATH_CENTURION = MODEL_MONSTER01 + 67,
    MODEL_NECRON = MODEL_MONSTER01 + 68,
    MODEL_SHRIKER = MODEL_MONSTER01 + 69,
    MODEL_CHAOS_CASTLE_KNIGHT = MODEL_MONSTER01 + 70,
    MODEL_CHAOS_CASTLE_ELF = MODEL_MONSTER01 + 71,
    MODEL_CHAOS_CASTLE_WIZARD = MODEL_MONSTER01 + 72,
    MODEL_CASTLE_GATE1 = MODEL_MONSTER01 + 73,
    MODEL_GUARDIAN_STATUE = MODEL_MONSTER01 + 74,
    MODEL_GREAT_DRAKAN = MODEL_MONSTER01 + 75,
    MODEL_BATTLE_GUARD1 = MODEL_MONSTER01 + 76,
    MODEL_BATTLE_GUARD2 = MODEL_MONSTER01 + 77,
    MODEL_GOLDEN_GOBLIN = MODEL_MONSTER01 + 78,
    MODEL_CANON_TOWER = MODEL_MONSTER01 + 79,
    MODEL_GOLDEN_LIZARD_KING = MODEL_MONSTER01 + 80,
    MODEL_LIZARD_WARRIOR = MODEL_MONSTER01 + 81,
    MODEL_FIRE_GOLEM = MODEL_MONSTER01 + 82,
    MODEL_QUEEN_BEE = MODEL_MONSTER01 + 83,
    MODEL_POISON_GOLEM = MODEL_MONSTER01 + 84,
    MODEL_AXE_HERO = MODEL_MONSTER01 + 85,
    MODEL_LIFE_STONE = MODEL_MONSTER01 + 86,
    MODEL_EROHIM = MODEL_MONSTER01 + 87,
    MODEL_RED_SKELETON_KNIGHT_1 = MODEL_MONSTER01 + 88,
    MODEL_BALGASS = MODEL_MONSTER01 + 89,
    MODEL_CHIEF_SKELETON_WARRIOR_2 = MODEL_MONSTER01 + 90,
    MODEL_BALRAM = MODEL_MONSTER01 + 91,
    MODEL_DARK_ELF_1 = MODEL_MONSTER01 + 92,
    MODEL_DEATH_SPIRIT = MODEL_MONSTER01 + 93,
    MODEL_SORAM = MODEL_MONSTER01 + 94,
    MODEL_WEREWOLF_HERO = MODEL_MONSTER01 + 95,
    MODEL_VALAM = MODEL_MONSTER01 + 96,
    MODEL_SOLAM = MODEL_MONSTER01 + 97,
    MODEL_SCOUT = MODEL_MONSTER01 + 98,
    MODEL_BALLISTA = MODEL_MONSTER01 + 99,
    MODEL_WITCH_QUEEN = MODEL_MONSTER01 + 100,
    MODEL_GOLDEN_STONE_GOLEM = MODEL_MONSTER01 + 101,
    MODEL_DEATH_RIDER = MODEL_MONSTER01 + 102,
    MODEL_FOREST_ORC = MODEL_MONSTER01 + 103,
    MODEL_DEATH_TREE = MODEL_MONSTER01 + 104,
    MODEL_HELL_MAINE = MODEL_MONSTER01 + 105,
    MODEL_BERSERK = MODEL_MONSTER01 + 106,
    MODEL_SPLINTER_WOLF = MODEL_MONSTER01 + 107,
    MODEL_IRON_RIDER = MODEL_MONSTER01 + 108,
    MODEL_SATYROS = MODEL_MONSTER01 + 109,
    MODEL_BLADE_HUNTER = MODEL_MONSTER01 + 110,
    MODEL_KENTAUROS = MODEL_MONSTER01 + 111,
    MODEL_GIGANTIS = MODEL_MONSTER01 + 112,
    MODEL_GENOCIDER = MODEL_MONSTER01 + 113,
    MODEL_PERSONA = MODEL_MONSTER01 + 114,
    MODEL_TWIN_TAIL = MODEL_MONSTER01 + 115,
    MODEL_DREADFEAR = MODEL_MONSTER01 + 116,
    MODEL_RED_SKELETON_KNIGHT_4 = MODEL_MONSTER01 + 117,
    MODEL_MAYA_HAND_LEFT = MODEL_MONSTER01 + 118,
    MODEL_MAYA_HAND_RIGHT = MODEL_MONSTER01 + 119,
    MODEL_MAYA = MODEL_MONSTER01 + 120,
    MODEL_DARK_SKULL_SOLDIER_5 = MODEL_MONSTER01 + 121,
    MODEL_POUCH_OF_BLESSING = MODEL_MONSTER01 + 122,
    MODEL_ILLUSION_SORCERER_SPIRIT_LIGHTNING = MODEL_MONSTER01 + 123,
    MODEL_ILLUSION_SORCERER_SPIRIT_ICE = MODEL_MONSTER01 + 124,
    MODEL_ILLUSION_SORCERER_SPIRIT_POISON = MODEL_MONSTER01 + 125,
    MODEL_DARK_ELF = MODEL_MONSTER01 + 126,
    MODEL_LUNAR_RABBIT = MODEL_MONSTER01 + 127,
    MODEL_RABBIT = MODEL_MONSTER01 + 128,
    MODEL_BUTTERFLY = MODEL_MONSTER01 + 129,
    MODEL_HIDEOUS_RABBIT = MODEL_MONSTER01 + 130,
    MODEL_WEREWOLF2 = MODEL_MONSTER01 + 131,
    MODEL_CURSED_LICH = MODEL_MONSTER01 + 132,
    MODEL_TOTEM_GOLEM = MODEL_MONSTER01 + 133,
    MODEL_GRIZZLY = MODEL_MONSTER01 + 134,
    MODEL_CAPTAIN_GRIZZLY = MODEL_MONSTER01 + 135,
    MODEL_SAPIUNUS = MODEL_MONSTER01 + 136,
    MODEL_SAPIDUO = MODEL_MONSTER01 + 137,
    MODEL_SAPITRES = MODEL_MONSTER01 + 138,
    MODEL_SHADOW_PAWN = MODEL_MONSTER01 + 139,
    MODEL_SHADOW_KNIGHT = MODEL_MONSTER01 + 140,
    MODEL_SHADOW_LOOK = MODEL_MONSTER01 + 141,
    MODEL_NAPIN = MODEL_MONSTER01 + 142,
    MODEL_GHOST_NAPIN = MODEL_MONSTER01 + 143,
    MODEL_BLAZE_NAPIN = MODEL_MONSTER01 + 144,
    MODEL_ICE_WALKER = MODEL_MONSTER01 + 145,
    MODEL_GIANT_MAMMOTH = MODEL_MONSTER01 + 146,
    MODEL_ICE_GIANT = MODEL_MONSTER01 + 147,
    MODEL_COOLUTIN = MODEL_MONSTER01 + 148,
    MODEL_IRON_KNIGHT = MODEL_MONSTER01 + 149,
    MODEL_SELUPAN = MODEL_MONSTER01 + 150,
    MODEL_SPIDER_EGGS_1 = MODEL_MONSTER01 + 151,
    MODEL_SPIDER_EGGS_2 = MODEL_MONSTER01 + 152,
    MODEL_SPIDER_EGGS_3 = MODEL_MONSTER01 + 153,
    MODEL_FIRE_FLAME_GHOST = MODEL_MONSTER01 + 154,
    MODEL_CURSED_SANTA = MODEL_MONSTER01 + 155,
    MODEL_EVIL_GOBLIN = MODEL_MONSTER01 + 156,
    MODEL_ZOMBIE_FIGHTER = MODEL_MONSTER01 + 157,
    MODEL_GLADIATOR = MODEL_MONSTER01 + 158,
    MODEL_SLAUGHTERER = MODEL_MONSTER01 + 159,
    MODEL_BLOOD_ASSASSIN = MODEL_MONSTER01 + 160,
    MODEL_CRUEL_BLOOD_ASSASSIN = MODEL_MONSTER01 + 161,
    MODEL_LAVA_GIANT = MODEL_MONSTER01 + 162,
    MODEL_BURNING_LAVA_GIANT = MODEL_MONSTER01 + 163,
    MODEL_GAYION = MODEL_MONSTER01 + 164,
    MODEL_JERRY = MODEL_MONSTER01 + 165,
    MODEL_RAYMOND = MODEL_MONSTER01 + 166,
    MODEL_LUCAS = MODEL_MONSTER01 + 167,
    MODEL_FRED = MODEL_MONSTER01 + 168,
    MODEL_HAMMERIZE = MODEL_MONSTER01 + 169,
    MODEL_DUAL_BERSERKER = MODEL_MONSTER01 + 170,
    MODEL_DEVIL_LORD = MODEL_MONSTER01 + 171,
    MODEL_QUARTER_MASTER = MODEL_MONSTER01 + 172,
    MODEL_COMBAT_INSTRUCTOR = MODEL_MONSTER01 + 173,
    MODEL_ATICLES_HEAD = MODEL_MONSTER01 + 174,
    MODEL_DARK_GHOST = MODEL_MONSTER01 + 175,
    MODEL_BANSHEE = MODEL_MONSTER01 + 176,
    MODEL_HEAD_MOUNTER = MODEL_MONSTER01 + 177,
    MODEL_DEFENDER = MODEL_MONSTER01 + 178,
    MODEL_FORSAKER = MODEL_MONSTER01 + 179,
    MODEL_OCELOT = MODEL_MONSTER01 + 180,
    MODEL_ERIC = MODEL_MONSTER01 + 181,
    MODEL_DEATH_ANGEL_3 = MODEL_MONSTER01 + 182,
    MODEL_EVIL_GATE = MODEL_MONSTER01 + 183,
    MODEL_LION_GATE = MODEL_MONSTER01 + 184,
    MODEL_STATUE = MODEL_MONSTER01 + 185,
    MODEL_STAR_GATE = MODEL_MONSTER01 + 186,
    MODEL_RUSH_GATE = MODEL_MONSTER01 + 187,
    MODEL_SCHRIKER_3 = MODEL_MONSTER01 + 188,
    MODEL_MAD_BUTCHER = MODEL_MONSTER01 + 189,
    MODEL_TERRIBLE_BUTCHER = MODEL_MONSTER01 + 190,
    MODEL_DOPPELGANGER = MODEL_MONSTER01 + 191,
    MODEL_MEDUSA = MODEL_MONSTER01 + 192,
    MODEL_BLOODY_ORC = MODEL_MONSTER01 + 193,
    MODEL_BLOODY_DEATH_RIDER = MODEL_MONSTER01 + 194,
    MODEL_BLOODY_GOLEM = MODEL_MONSTER01 + 195,
    MODEL_BLOODY_WITCH_QUEEN = MODEL_MONSTER01 + 196,
    MODEL_BERSERKER_WARRIOR = MODEL_MONSTER01 + 197,
    MODEL_KENTAUROS_WARRIOR = MODEL_MONSTER01 + 198,
    MODEL_GIGANTIS_WARRIOR = MODEL_MONSTER01 + 199,
    MODEL_SOCCERBALL = MODEL_MONSTER01 + 200,
    MODEL_SAPI_QUEEN = MODEL_MONSTER01 + 201,
    MODEL_ICE_NAPIN = MODEL_MONSTER01 + 202,
    MODEL_SHADOW_MASTER = MODEL_MONSTER01 + 203,
    MODEL_WOLF_STATUS = MODEL_MONSTER01 + 204,
    MODEL_DARK_MAMMOTH = MODEL_MONSTER01 + 205,
    MODEL_DARK_GIANT = MODEL_MONSTER01 + 206,
    MODEL_DARK_COOLUTIN = MODEL_MONSTER01 + 207,
    MODEL_DARK_IRON_KNIGHT = MODEL_MONSTER01 + 208,
    MODEL_VENOMOUS_CHAIN_SCORPION = MODEL_MONSTER01 + 209,
    MODEL_BONE_SCORPION = MODEL_MONSTER01 + 210,
    MODEL_ORCUS = MODEL_MONSTER01 + 211,
    MODEL_GOLLOCK = MODEL_MONSTER01 + 212,
    MODEL_CRYPTA = MODEL_MONSTER01 + 213,
    MODEL_CRYPOS = MODEL_MONSTER01 + 214,
    MODEL_CONDRA = MODEL_MONSTER01 + 215,
    MODEL_NACONDRA = MODEL_MONSTER01 + 216,
};

namespace WorldModelLoadingDetail
{

const wchar_t *GetMonsterModelName(EMonsterModelType Type);
}

namespace ModelGeometryDetail
{

std::size_t BmdSharedAssetBytes(const BmdSharedAsset &asset) noexcept;
}

#define MODEL_COMPILED_CELE MODEL_WING + 30
#define MODEL_COMPILED_SOUL MODEL_WING + 31
