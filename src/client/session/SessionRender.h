#pragma once
#include "support/CoreMath.h"
#include "app/ApplicationLoopFrame.h"
#include "data/GameData.h"
#include "data/WorldData.h"
#include "domain/ChatSocial.h"
#include "domain/EffectsUpdate.h"
#include "render/Effects.h"
#include "render/FrameTape.h"
#include "render/Map.h"
#include "render/ModelGeometry.h"
#include "render/Textures.h" // For vec3_t
#include "render/World.h"
#include "session/SessionNetwork.h"
#include "session/SessionRuntime.h"
#include "session/SessionUi.h"
#include "ui/features/Hud/HudRender.h"
#include "ui/features/Social/SocialRender.h"
#include "ui/features/World/WorldRender.h"
#include "ui/runtime/UiControls.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ItemRulesDetail
{
struct GroundItemLabelDescriptor;
}

namespace SplashSceneDetail
{
enum class BackgroundTheme;
}

// Character scene lifecycle
bool NewRenderCharacterScene();

// Login scene lifecycle
bool NewRenderLogInScene();

class SessionFrameView final
{
  public:
    using RenderFunction = bool (*)();

    explicit SessionFrameView(RenderFunction render) noexcept;

    bool HasRenderFunction() const noexcept;
    bool Render() const noexcept;

  private:
    RenderFunction render_;
};

struct WorldCharacterVisualState;
struct WorldObjectDrawGroup;
class CSPetSystem;
class CSideHair;
struct CharacterClothVisual;
struct CharacterLinkedItemVisual;
class CPhysicsClothMesh;
namespace UI::Items
{
struct ItemSlotTrs;
}

class SessionKeeper;
struct SessionSpriteStorage;
struct SessionSprite;
class SessionRandom;
namespace SEASON3A
{
class CMixRecipeMgr;
}
namespace SEASON4A
{
class CSocketItemMgr;
}
class SessionLifecycleObserver;
class SessionAdvanceUnit;
class GameSessionTestPeer;
class CharacterRetirementTestPeer;
class SessionGameplayUnit;
class MapProcess;
class CameraManager;
class CameraProjection;
class CameraState;
class CCameraMove;
class CErrorReport;
class SessionModelPool;
class CPhysicsCloth;
class CMapManager;
class CDirection;
class CSkillManager;
class CSItemOption;
class CSQuest;
class CQuestMng;
class CSkillEffectMgr;
class CSummonSystem;
class CMonkSystem;
class CPortalMgr;
class CmuConsoleDebug;
class PetProcess;
class CTimer;
class CPhysicsManager;
class CPhysicsCloth;
class CBoneManager;
class SessionRenderText;
class CUITextInputBox;
class CUIMapName;
class CUIMng;
class OBJECT;
namespace UI::Modern
{
class RmlUiRuntime;
}
namespace Core::Math
{
struct LineToFaceCollision;
}
namespace SEASON3B
{
class CNewUIInventoryCtrl;
class CNewUIMessageBoxMng;
class CNewUISystem;
} // namespace SEASON3B
namespace SEASON3A
{
class CursedTemple;
class CGM3rdChangeUp;
} // namespace SEASON3A
struct GroundItemLabelCacheEntry;
struct SessionGroundItemLabelStorage;
struct SessionSmdStorage;
enum SMDToken : int;
struct LogicPickResult;

class SessionRenderUnit final : protected SessionLegacyCalls
{
  public:
    SessionRenderUnit(SessionKeeper &keeper, SessionLifecycleObserver *observer = nullptr) noexcept;
    ~SessionRenderUnit();

    bool PrepareFrameOnOwner(SessionGeneration generation, std::uint64_t frameSequence,
                             std::uint64_t surfaceGeneration, std::uint32_t viewportWidth,
                             std::uint32_t viewportHeight,
                             SessionRenderTapeRecording recording) noexcept;
    bool SubmitModernUiPreparation(UI::Modern::RmlUiRuntime &runtime) noexcept;
    bool RecordPreparedFrameWorkerSafe() noexcept;
    std::optional<SessionRenderTape> CompletePreparedFrameOnOwner(bool accept) noexcept;
    bool CompleteRender(const SessionReplayCompletion &completion) noexcept;
    bool CompleteRenderRequest(const RenderOwnerRequestCompletion &completion,
                               std::span<const std::byte> payload) noexcept;
    const std::optional<SessionReplayCompletion> &LastCompletedTarget() const noexcept
    {
        return lastCompletedTarget_;
    }
    LegacyRenderFacade &LegacyRender() noexcept
    {
        return facade_;
    }
    const LogicalGeometryAssetLease &TextQuadGeometry() const noexcept
    {
        return effectQuadGeometry_;
    }
    SessionRenderText &Text() noexcept
    {
        return g_RenderText;
    }
    const SessionRenderText &Text() const noexcept
    {
        return g_RenderText;
    }
    std::uint64_t AllocateModernUiGeometryRevision() noexcept
    {
        return AllocateGeometryRevision();
    }
    LogicalGeometryAssetRef MakeModernUiGeometryAssetRef(std::uint64_t revision) const noexcept;
    std::uint32_t ModernUiPhysicalViewportWidth() const noexcept
    {
        return preparedViewportWidth_;
    }
    std::uint32_t ModernUiPhysicalViewportHeight() const noexcept
    {
        return preparedViewportHeight_;
    }
    std::uint32_t ModernUiLogicalViewportWidth() const noexcept
    {
        return preparedModernUiViewportWidth_;
    }
    std::uint32_t ModernUiLogicalViewportHeight() const noexcept
    {
        return preparedModernUiViewportHeight_;
    }
    void RenderItemInfo(int x, int y, ITEM *item, bool sell, int inventoryType = 0,
                        bool itemTextListBoxUse = false);
    void RenderTipTextList(const int x, const int y, int textCount, int tab, int sort = 3,
                           int renderPoint = 0, BOOL useBackground = TRUE);
    void RenderHelpLine(int columnType, const wchar_t *printStyle, int &tabSpace,
                        const wchar_t *gapText, int positionY, int type);
    void RenderRepairInfo(int x, int y, ITEM *item, bool sell);
    void RenderToolTipForSocketSetOption(int positionX, int positionY);
    void RenderItemName(int index, OBJECT *object, ITEM *item, bool sort);
    void StagePetFrame(const UI::Modern::RmlPetFrameRequest &request) noexcept
    {
        petFrameLayer_.Stage(request);
    }
    bool RecordPetFrame()
    {
        return petFrameLayer_.Record(facade_);
    }
    bool RecordTooltips();
    void StagePartyFrame(const UI::Modern::RmlPartyFrameRequest &request) noexcept
    {
        partyFrameLayer_.Stage(request);
    }
    bool RecordPartyFrame()
    {
        return partyFrameLayer_.Record(facade_);
    }
    void StageMainFrame(const UI::Modern::RmlMainFrameRequest &request) noexcept
    {
        mainFrameLayer_.Stage(request);
    }
    bool RecordMainFrame()
    {
        return mainFrameLayer_.Record(facade_);
    }
    void StageTopMenu(const UI::Modern::RmlTopMenuRequest &request) noexcept
    {
        topMenuLayer_.Stage(request);
    }
    bool RecordTopMenu()
    {
        return topMenuLayer_.Record(facade_);
    }
    void StageMoveCommand(const UI::Modern::RmlMoveCommandRequest &request) noexcept
    {
        moveCommandLayer_.Stage(request);
    }
    bool RecordMoveCommand()
    {
        return moveCommandLayer_.Record(facade_);
    }
    void StagePlayerNames(UI::Modern::RmlPlayerNameRequest &request);
    void SetPlayerNameDisplay(bool enabled) noexcept
    {
        playerNameDisplay_ = enabled;
    }
    bool PlayerNameDisplay() const noexcept
    {
        return playerNameDisplay_;
    }
    bool RecordPlayerNames()
    {
        return playerNameLayer_.Record(facade_);
    }
    SEASON3B::CNewUIInventoryCtrl *CreateInventoryControl();
    void RenderPhysicsCloths();
    void RenderCloth(const CPhysicsCloth &cloth, const vec3_t *color = nullptr, int level = 0);
    void RenderClothFace(const CPhysicsCloth &cloth, BOOL front, int texture);
    std::size_t TerrainGeometryStorageBytes(SharedAllocationCounter &allocations) const noexcept
    {
        return terrainGeometryCache_.StorageBytes(allocations);
    }

  private:
    bool recordingCharacter_ = false;
    struct PreparedCharacter final
    {
        const CHARACTER *character;
        const WorldCharacterVisualState *visual;
        int selection;
    };
    // Published after the simulation/observer joins; released after recording joins.
    std::vector<PreparedCharacter> preparedCharacters_;
    void PrepareCharacterReaders();
    SessionDrawPoseStorage drawPoses_;
    std::unique_ptr<CSideHair> sideHair_;
    std::vector<std::array<float, 3>> clothShadowVertices_;
    bool recordingParts_ = false;
    const CHARACTER *drawingCharacter_ = nullptr;
    const WorldCharacterVisualState *drawingCharacterVisual_ = nullptr;
    friend class CharacterRetirementTestPeer;
    friend class World;
    friend class WorldResources;
    friend class BMD;
    friend class SessionKeeper;
    friend class SessionUiUnit;
    friend class SessionLegacyCalls;
    friend class GameSession;
    friend class GameSessionTestPeer;
    friend class SessionGameplayUnit;
    friend class SessionAdvanceUnit;
    friend class CUIMng;
    friend class CCharMakeWin;
    friend class CUIPhotoViewer;
    friend class CSPetSystem;
    friend class PetObject;
    friend struct RootingItem;
    void TrackPendingTargetCopies(const SessionRenderTape &tape) noexcept;
    void CaptureRenderTapeDebugStats(const SessionRenderTape &tape) noexcept;
    bool CompletePendingTargetCopy(const RenderOwnerRequestCompletion &completion,
                                   std::span<const std::byte> payload) noexcept;
    bool BeginRenderTapePass(RenderTapePass pass) noexcept;
    bool EndRenderTapePass() noexcept;
    bool SwitchWorldRenderTapePass(RenderTapePass pass, int x, int y, int width,
                                   int height) noexcept;
    bool InitializeEffectQuadGeometry() noexcept;
    bool DrawEffectQuadInstance(const RenderTapeQuadInstance &instance,
                                std::uint64_t runId) noexcept
    {
        return facade_.DrawQuadInstance(effectQuadGeometry_, instance, runId);
    }
    bool DrawSpriteInstance(const RenderTapeQuadInstance &instance, std::uint64_t runId) noexcept
    {
        return facade_.DrawSpriteInstance(effectQuadGeometry_, instance, runId);
    }
    friend class SEASON3B::CNewUISystem;

    std::uint64_t AllocateGeometryRevision() noexcept
    {
        return nextGeometryRevision_.fetch_add(1, std::memory_order_relaxed);
    }

    CUIMng &LegacyUiManager();

    void ComputeItemInfo(int helpItem);                     // OMF-00507
    bool CreateGuildMark(int markIndex, bool blend = true); // OMF-00588
    void CreateCastleMark(int type, BYTE *buffer = nullptr,
                          bool blend = true); // OMF-00589
    void HideKeyPad();                        // OMF-00578
    int CheckMouseOnKeyPad();                 // OMF-00579
    bool RenderScene();
    bool EnsureVisualDataOnOwner(bool keepTitleScene);
    bool PrepareWebzenSceneOnOwner();
    bool AdvanceWebzenLoadingOnOwner();
    bool HasPresentedFirstTitleFrame() const noexcept;
    bool HasPresentedFinalTitleFrame() const noexcept;
    bool HasPresentedTitleFrame(std::uint64_t frameSequence, SessionGeneration generation,
                                std::uint64_t surfaceGeneration) const noexcept;
    bool InitializeTitlePresentationOnOwner();
    void InitializeMainSceneInterfaceOnOwner();
    void ReleaseTitleSceneOnOwner() noexcept;
    void OpenBasicDataStep(std::uint32_t step);
    void WebzenScene();
    SplashSceneDetail::BackgroundTheme SelectBackgroundTheme();
    void LoadBackgroundTheme(SplashSceneDetail::BackgroundTheme theme);
    void LoadCommonTitleBitmaps();
    void UnloadTitleBitmaps();
    bool OpenFont();

    void OpenBasicData();
    void OpenLegacySystemWindowTextures();
    void OpenModel(int type, wchar_t *directory, wchar_t *modelFileName, ...);
    void OpenModel(int type, wchar_t *directory, wchar_t *modelFileName, va_list animationFiles);
    void OpenModels(int model, wchar_t *fileName, int index);
    void OpenPlayers();
    void RenderPet(CHARACTER *character);
    void LoadChangeRingItemModels();
    void LoadChangeRingItemTextures();
    void OpenPlayerTextures();
    void OpenItems();
    void OpenItemTextures();
    void OpenNpc(int type);
    void DeleteNpcs();

    void OpenSkills();
    void OpenSounds();
    void OpenImages();
    void BeginOpengl(int x = 0, int y = 0, int width = 640, int height = 480);
    void EndOpengl(); // OMF-01719
    void glViewport2(int x, int y, int width, int height);
    void gluPerspective2(float fov, float aspect, float zNear, float zFar);
    void SaveCameraPerspective();
    void RestoreCameraPerspective();
    void UpdateMousePositionn();
    void RenderSprite(int texture, const vec3_t position, float width, float height,
                      const vec3_t light, float rotation = 0.f, float u = 0.f, float v = 0.f,
                      float uWidth = 1.f, float vHeight = 1.f);
    std::uint64_t RenderSpriteInstance(int texture, const vec3_t position, float width,
                                       float height, const vec3_t light, float rotation, float u,
                                       float v, float uWidth, float vHeight, char textureComponents,
                                       std::uint64_t runId);
    void RenderSpriteUV(int texture, vec3_t position, float width, float height, float (*uv)[2],
                        vec3_t light[4], float alpha = 1.f);
    void RenderNumber(const vec3_t position, int number, const vec3_t color, float alpha = 1.f,
                      float scale = 15.f);
    void RenderNumberPoints(const vec3_t position, int number, const vec3_t color, float alpha,
                            float scale);
    float RenderNumber2D(float x, float y, int number, float width, float height);
    void BeginBitmap();
    void EndBitmap();                                                  // OMF-01735
    void BeginSprite();                                                // OMF-01728
    void EndSprite();                                                  // OMF-01729
    void RenderBox(float matrix[3][4]);                                // OMF-01726
    void RenderPlane3D(float width, float height, float matrix[3][4]); // OMF-01727
    void RenderPointRotate(int texture, float ix, float iy, float iWidth, float iHeight, float x,
                           float y, float width, float height, float rotate, float rotateLocation,
                           float uWidth, float vHeight,
                           int number); // OMF-01742
    void RenderDebugSphere(const vec_t *center, float radius, float red, float green,
                           float blue); // OMF-01669
    void RenderDebugBox(const vec_t *origin, float sizeX, float sizeY, float sizeZ, float red,
                        float green, float blue); // OMF-01670
    float RenderNumber(float x, float y, int number, float scale = 1.0f);
    void ConfigureCharacterSceneModels();
    void ReleaseCharacterSceneData();
    void ReleaseWorldRenderResources();

    void InitTerrainMappingLayer(); // OMF-01593
    void InitializeTerrainGrass();
    void CalculateTerrainNormal(int x, int y);
    void RefreshTerrainLighting(int x, int y, int width, int height);
    bool SaveTerrainMapping(wchar_t *fileName, int mapNumber); // OMF-01602
    void CreateTerrainNormal();                                // OMF-01603
    void CreateTerrainNormal_Part(int x, int y);               // OMF-01604
    void CreateTerrainLight_Part(int x, int y);                // OMF-01606
    void SaveTerrainLight(wchar_t *fileName);                  // OMF-01608
    void CalcShadowPosition(vec3_t *position, const vec3_t origin, float scaleX,
                            float scaleY); // OMF-01586
    void GetClothShadowPosition(vec3_t *target, const CPhysicsCloth *cloth, int index,
                                const vec3_t origin, float scaleX, float scaleY); // OMF-01587
    void Vertex0();                                                               // OMF-01628
    void Vertex1();                                                               // OMF-01629
    void Vertex2();                                                               // OMF-01630
    void Vertex3();                                                               // OMF-01631
    void Vertex01();                                                              // OMF-01632
    void Vertex12();                                                              // OMF-01633
    void Vertex23();                                                              // OMF-01634
    void Vertex30();                                                              // OMF-01635
    void Vertex02();                                                              // OMF-01636
    void VertexAlpha0();                                                          // OMF-01637
    void VertexAlpha1();                                                          // OMF-01638
    void VertexAlpha2();                                                          // OMF-01639
    void VertexAlpha3();                                                          // OMF-01640
    void VertexAlpha01();                                                         // OMF-01641
    void VertexAlpha12();                                                         // OMF-01642
    void VertexAlpha23();                                                         // OMF-01643
    void VertexAlpha30();                                                         // OMF-01644
    void VertexAlpha02();                                                         // OMF-01645
    void VertexBlend0();                                                          // OMF-01646
    void VertexBlend1();                                                          // OMF-01647
    void VertexBlend2();                                                          // OMF-01648
    void VertexBlend3();                                                          // OMF-01649
    void CreateSun();                                                             // OMF-01684
    bool SaveTerrainAttribute(wchar_t *fileName, int mapNumber);
    void CreateTerrain(const WorldImageData &data, bool extended);
    void InstallTerrainMapping(const WorldFileData &data);
    void InstallTerrainAttributes(const WorldFileData &data);
    void InstallTerrainLight(const WorldImageData &data);
    void InstallWorldPlacements(const WorldFileData &data);

    void SetTerrainWaterState(std::list<int> &terrainIndex, int state);
    void CreateTerrainLight();
    void SaveTerrainHeight(wchar_t *fileName);
    void FaceTexture(int texture, float x, float y, bool water, bool scale);
    void InstallTerrainHeight(const WorldImageData &data, bool extended);
    void UpdateTerrainWaterUv();
    bool PrepareWorldTerrainOnOwner(SessionGeneration generation) noexcept;
    bool PrepareTerrainLightSnapshot() noexcept;
    bool PreparePersistentTerrainGeometry(SessionGeneration generation,
                                          float specialHeight) noexcept;
    bool ShouldRenderGameTerrain();
    bool CanRenderMainScene() const noexcept;
    bool CanRenderCharacterScene() const noexcept;
    bool PrepareTerrainGeometryMaterial(const TerrainGeometryDraw &draw);
    void RestoreTerrainVertexAttributes(const LogicalGeometryAssetLease &geometry,
                                        const TerrainGeometryDraw &draw, bool grass);
    bool DrawPersistentTerrainGeometry(const LogicalGeometryAssetLease &geometry,
                                       const TerrainGeometryDraw &draw);
    bool CollectVisibleTerrainBlocks() noexcept;
    void RenderPersistentTerrainFrustrum();
    void RenderPersistentTerrainBlock(int mapX, int mapY, std::uint16_t visibleTiles);
    void RenderPersistentTerrainAfterBlock(int mapX, int mapY, std::uint16_t visibleTiles);
    void RenderPersistentGrassFrustrum();
    bool DrawPersistentGrassGeometry(const LogicalGeometryAssetLease &geometry,
                                     const TerrainGeometryDraw &draw,
                                     const RenderTapeGrassConstants &grass, bool alphaBlend,
                                     int &activeTexture, std::uint64_t &activeRunId);
    void RenderPersistentGrassBlock(int mapX, int mapY, std::uint16_t visibleTiles,
                                    const RenderTapeGrassConstants &grass, int &activeTexture,
                                    std::uint64_t &activeRunId);
    bool PrepareTerrainBaseMaterial(int texture);
    bool ShouldRenderLeaves();
    void ParseNodes(); // OMF-01575
    SMDToken GetSmdToken();
    void ParseSkeleton();           // OMF-01576
    void ParseTriangles(bool flip); // OMF-01577
    void FixupSMD();                // OMF-01581
    bool OpenSMDFile(wchar_t *fileName, int type, bool flip);
    bool OpenSMDModel(int id, wchar_t *fileName, int actions = 1, bool flip = false);
    bool OpenSMDAnimation(int id, wchar_t *fileName, bool lockPosition = false);
    void SMD2BMDModel(int id, int actions);
    void SMD2BMDAnimation(int id, bool lockPosition);
    void MainScene();
    bool RenderCurrentScene();
    bool NewRenderLogInScene();
    bool NewRenderCharacterScene();
    bool RenderMainScene();
    void RenderDebugWindow();
    int RenderDebugText(int y);
    void RenderFrameGraph(float graphX, float graphY, float graphW, float graphH);
    void RenderDebugInfo();
    int RenderRuntimeDebugInfo(int y);
    int RenderTapeDebugInfo(int y);
    int RenderTapeFailureDebugInfo(int y);
    int RenderHotspotProfilerDebugInfo(int y);
    void RenderFpsCounter();
    void RenderMainSceneOverlays(bool sceneRendered);
    void LoadingScene();
    void RenderCharacterSceneUI();
    void RenderMainSceneUI();
    void RenderMainSceneWorldUi();
    void RenderCharacterScene3D();
    void RenderBoids(bool afterCharacter = false);
    void RenderFishs();

    bool RenderMount(const ObjectDrawInput &object, bool forceRender = false);

    void RenderCharacterAttachments();
    void RenderCharacterAttachments(const CHARACTER &character,
                                    const WorldCharacterVisualState &visual,
                                    bool forceRender = false);
    void RenderGameWorld(BYTE &waterMap, int width, int height);
    bool RenderMainTerrainAndObjects(int width, int height);
    void PrepareCharacterDrawLight(CharacterDrawInput &character, vec3_t ambientLight);
    void RenderCharacter(const CHARACTER *character, const OBJECT *object, int select = 0,
                         const WorldCharacterVisualState *localVisual = nullptr,
                         bool drawLocalAttachments = true);
    void RenderGuild(const ObjectDrawInput &object, int type = -1, vec3_t position = nullptr);
    const vec34_t *RenderLinkObject(float x, float y, float z, const CharacterDrawInput &character,
                                    const PART_t *part, int type, int level, int option1, bool link,
                                    bool translate, int renderType = 0, bool rightHandItem = true,
                                    int slot = -1);

    bool RenderCharacterBackItem(const CharacterDrawInput &character, bool translate);

    void RenderHelpCategory(int columnType, int positionX, int positionY);
    void RenderEqiupmentPart3D(int index, float x, float y, float width, float height);
    void RenderEqiupment3D();
    bool Calc_RenderObject(ObjectDrawInput &object, bool translate, int select, int extraMonster);
    bool Calc_ObjectAnimation(ObjectDrawInput &object, bool translate, int select);
    const vec34_t *PrepareDrawPose(const ObjectDrawInput &draw, bool translate,
                                   float animationFrame);
    void Draw_RenderObject(const ObjectDrawInput &object, bool translate, int select,
                           int extraMonster);
    const vec34_t *RenderObject(const ObjectDrawInput &object, bool translate = false,
                                int select = 0, int extraMonster = 0);

    void RenderCharacterCape(const CHARACTER &character);

    void RenderCharacterPartCloth(int type);

    void RenderCharacterCloth(const CHARACTER &character, const vec3_t *light = nullptr);

    const WorldCharacterVisualState *FindCharacterVisual(const CHARACTER &character);
    void RenderParts(const CHARACTER *character);

    const vec34_t *RenderObject_AfterImage(const ObjectDrawInput &object, bool translate = false,
                                           int select = 0, int extraMonster = 0);
    void RenderCharacter_AfterImage(const CharacterDrawInput &character, const PART_t *part,
                                    bool translate = false, int select = 0,
                                    float animationInterval1 = 1.4f,
                                    float animationInterval2 = 0.7f);
    void RenderCharacterPartPose(ObjectDrawInput &draw, const vec3_t light, const PART_t &part,
                                 bool translate, int select);
    void RenderCharacterDarkside(const CharacterDrawInput &character, const PART_t *part,
                                 bool translate, int select);
    void RenderObject_AfterCharacter(const ObjectDrawInput &object, bool translate = false,
                                     int select = 0, int extraMonster = 0);
    void Draw_RenderObject_AfterCharacter(const ObjectDrawInput &object, bool translate = false,
                                          int select = 0, int extraMonster = 0);
    void RenderObjects_AfterCharacter();

    ObjectDrawInput PrepareItemDraw(const OBJECT &object, int type);

    void RenderZen(int itemIndex, ITEM_t *item, vec3_t light);

    void SortInBlockByType();

    bool SaveObjects(wchar_t *fileName, int mapNumber);
    void CreateShadowAngle();

    void InsertShadowVolume(CShadowVolume *shadowVolume);
    void InitCollisionDetectLineToFace();
    bool CollisionDetectLineToFace(vec3_t position, vec3_t target, int polygon, float *vertex1,
                                   float *vertex2, float *vertex3, float *vertex4, vec3_t normal,
                                   bool collision = true);

    void SaveTrapObjects(wchar_t *fileName);
    void RenderCloudLowLevel(int index, int type);
    void RenderItems();
    void NextGradeObjectRender(const CharacterDrawInput &character);
    void RenderBoundingBox(const OBJECT *object);
    void RenderPartObjectEffect(const ObjectDrawInput &object, int type, const vec3_t light,
                                float alpha = 0.0f, int level = 0, int excellentFlags = 0,
                                int ancientDiscriminator = 0, int select = 0,
                                int renderType = 0x00000002);
    void RenderPotionGlow(BMD &model, const ObjectDrawInput &draw, int renderType, float alpha);
    void RenderPartObjectBody(BMD *model, const ObjectDrawInput &object, int type, float alpha,
                              int renderType);
    void RenderPartObjectBodyColor(BMD *model, const ObjectDrawInput &object, int type, float alpha,
                                   int renderType, float bright, int texture = -1,
                                   int monsterIndex = -1);
    void RenderPartObjectBodyColor2(BMD *model, const ObjectDrawInput &object, int type,
                                    float alpha, int renderType, float bright, int texture = -1);
    void GetSpecialOptionText(int type, wchar_t *text, WORD option, BYTE value, int mana);

    void PartObjectColor2(int type, float alpha, float bright, vec3_t light,
                          bool extraMonster = false);
    void RenderPartObjectEdgeLight(BMD *model, const ObjectDrawInput &object, int flags,
                                   bool translate, float scale);
    void RenderPartObjectEdge(BMD *model, const ObjectDrawInput &object, int flags, bool translate,
                              float scale); // OMF-00646
    void RenderPartObjectEdge2(BMD *model, const ObjectDrawInput &object, int flags, bool translate,
                               float scale, OBB_t *obb); // OMF-00647
    void RenderPartObject(const ObjectDrawInput &object, int type, const PART_t *data,
                          const vec3_t light, float alpha = 0.0f, int level = 0,
                          int excellentFlags = 0, int ancientDiscriminator = 0,
                          bool globalTransform = false, bool hideSkin = false,
                          bool translate = false, int select = 0, int renderType = 0x00000002);
    void RenderCharactersClient();
    void RenderLight(const ObjectDrawInput &object, int texture, float scale, int bone, float x,
                     float y, float z);
    void RenderEye(const ObjectDrawInput &object, int left, int right, float size = 1.0f);

    void RenderJoints(BYTE bRenderOneMore = 0);
    void RenderJoint(const JOINT &joint);
    void RenderParticles(BYTE byRenderOneMore = 0);
    using ParticleDrawRun = Render::Effects::ParticleDrawRun;
    template <class BlurType>
    void RenderBlurSegments(const BlurType &blur, bool fade, std::uint64_t &runId);
    void RenderParticle(const PARTICLE &particle, int index, ParticleDrawRun &run);
    void RenderCircle2D(int type, vec3_t screenPosition, float scaleBottom, float scaleTop,
                        float height, float rotation, float textureV, float textureVScale);
    void RenderDestructionHighlight(const OBJECT &effect);
    void RenderWheelWeapon(const OBJECT *object);
    void RenderFuryStrike(const OBJECT *object);
    bool EffectVisible(const OBJECT &object);

    void RenderBossLaser(const OBJECT &object);
    void RenderSkillSpear(const OBJECT *object);

    int CreateSprite(int type, const vec3_t position, float scale, const vec3_t light,
                     const OBJECT *owner, float rotation = 0.f, int subType = 0);
    void RenderSprites(BYTE byRenderOneMore = 0);
    void RenderBlurs();
    void RenderObjectBlurs();
    void PrepareGaionSwordPose(const OBJECT &object, vec3_t *positions);
    using RigidEffectRun = Render::Effects::RigidEffectRun;
    bool QueueRigidEffect(const OBJECT &effect, RigidEffectRun &run);
    void FlushRigidEffects(RigidEffectRun &run);
    std::vector<RenderTapeRigidInstance> effectRigidInstances_;
    void RenderEffects(bool bRenderBlendMesh = false);
    void RenderAfterEffects(bool renderBlendMesh = false);
    void RenderEffectShadows();
    void RenderEffectShadow(const OBJECT &effect);
    void RenderCircle(int type, const vec3_t objectPosition, float scaleBottom, float scaleTop,
                      float height, float rotation = 0.f, float lightTop = 1.f,
                      float textureV = 0.f);
    void RenderLeaves();
    void RenderPoints(BYTE byRenderOneMore = 0);
    void RenderPointers();
    void RenderTerrain(bool editFlag);

    void RenderTerrainBitmapTile(float x, float y, float lodFactor, int lod, vec3_t coordinates[4],
                                 bool lightEnable, float alpha, float height = 0.f,
                                 std::uint64_t quadRunId = 0);
    void RenderTerrainBitmap(int texture, int mapX, int mapY, float rotation);
    void RenderTerrainAlphaBitmap(int texture, float x, float y, float sizeX, float sizeY,
                                  const vec3_t light, float rotation = 0.f, float alpha = 1.f,
                                  float height = 5.f);
    void SetupCharacterSceneViewport(int &outWidth, int &outHeight);
    void SetupMainSceneViewport(int &outWidth, int &outHeight, BYTE &outByWaterMap,
                                vec_t *cameraPos);
    void RenderSelectedCharacterEffects();

    void RenderInfomation3D();
    void RenderInfomation();
    void RenderInputText(int x, int y, int index, int gold = 0);
    void RenderBar(float x, float y, float width, float height, float bar, bool disabled = false,
                   bool clipping = true);
    void RenderSwichState();
    void RenderTournamentInterface();
    void RenderPartyHP();
    void BackSelectModel();
    void ForwardSelectModel();
    void RenderInterface(bool render);
    void RenderOutSides();
    void RenderTimes();
    void RenderCursor();
    void RenderTipText(int x, int y, const wchar_t *text);
    void RenderFace(int texture, int mapX, int mapY);
    void RenderFace_After(int texture, int mapX, int mapY);
    void RenderFaceAlpha(int texture, int mapX, int mapY);
    void RenderFaceBlend(int texture, int mapX, int mapY);
    void RenderTerrainFace(float x, float y, int mapX, int mapY, float lodFactor);
    void RenderTerrainFace_After(float x, float y, int mapX, int mapY, float lodFactor);
    bool RenderTerrainTile(float x, float y, int mapX, int mapY, float lodFactor, int lod,
                           bool flag);
    void RenderTerrainTile_After(float x, float y, int mapX, int mapY, float lodFactor, int lod,
                                 bool flag);
    void RenderTerrainBlockVisible(float x, float y, int mapX, int mapY, bool editFlag,
                                   std::uint16_t visibleTiles, bool visibilityKnown);
    void RenderTerrainBlock(float x, float y, int mapX, int mapY, bool editFlag);
    void RenderTerrainFrustrum(bool editFlag);
    void RenderTerrainBlock_After(float x, float y, int mapX, int mapY, bool editFlag);
    void RenderTerrainFrustrum_After(bool editFlag);
    void RenderTerrain_After(bool editFlag);
    void RenderSun();
    void RenderSky();
    void RenderShadowVolumesAsFrame();
    void ShadeWithShadowVolumes();
    void RenderShadowToScreen();
    void SetClearAndFogColor(float red, float green, float blue);
    void SetWorldClearColor();
    void RenderObjectVisual(const ObjectDrawInput &draw);

    void AppendCharacterSprites(const WorldCharacterVisualState &visual);
    void AppendPreparedCharacterSprites(const std::unique_ptr<SessionSpriteStorage> &sprites);

    void AppendMapCharacterSprites(const CHARACTER &character);

    void RenderObjects();
    void PrepareWorldObjectDrawGroups(OBJECT_BLOCK &block);
    void RenderWorldObject(const OBJECT &object);
    void RenderRigidObjectGroup(OBJECT_BLOCK &block, const WorldObjectDrawGroup &group);

    void RenderSprite(const OBJECT *object, const OBJECT *owner);
    std::uint64_t RenderSprite(const SessionSprite *object, std::uint64_t runId);
    void SetBooleanPosition(UI::Chat::CHAT *chat);
    void SetPlayerColor(BYTE playerKillLevel);
    void RenderBoolean(int x, int y, UI::Chat::CHAT *chat);

    void RenderBooleans();
    void RenderList();
    void BuildGroundItemLabelDescriptor(OBJECT *object, ITEM *item,
                                        ItemRulesDetail::GroundItemLabelDescriptor &descriptor);
    void ApplyGroundItemLabelDescriptor(
        const ItemRulesDetail::GroundItemLabelDescriptor &descriptor);
    bool CreateGroundItemLabelTexture(const ItemRulesDetail::GroundItemLabelDescriptor &descriptor,
                                      GroundItemLabelCacheEntry &cacheEntry);
    void RenderGroundItemLabelTexture(OBJECT *object, const GroundItemLabelCacheEntry &cacheEntry);
    bool RenderGroundItemLabelCached(OBJECT *object, ITEM *item);
    void PruneGroundItemLabelCache(DWORD currentTick);
    void SetGroundItemLabelBuildBudget(int buildBudget);
    void ReleaseGroundItemLabelCache() noexcept;
    void RenderObjectScreen(int type, int itemLevel, int excellentFlags, int ancientDiscriminator,
                            vec3_t target, int select, bool pickUp, float presentationScale = 1.0f,
                            const UI::Items::ItemSlotTrs *trs = nullptr);
    void RenderItem3D(float x, float y, float width, float height, int type, int level,
                      int excellentFlags, int ancientDiscriminator, bool pickUp = false,
                      float presentationScale = 1.0f, bool useSlotTrs = false);
    void InventoryColor(ITEM *item);
    void RenderEqiupmentBox();
    void RenderGuildList(int startX, int startY);
    void RenderServerDivision();
    void RenderInventoryInterface(int startX, int startY, int flag);
    void RenderGuildColor(float x, float y, int sizeX, int sizeY, int index);
    SessionRandom &Random;
    SessionGameplayUnit &gameplay_;
    MapProcess &g_MapProcess;
    CameraManager &cameraManager_;
    CameraProjection &cameraProjection_;
    CameraState &g_Camera;
    PetProcess &g_petProcess;
    SEASON3A::CursedTemple &cursedTemple_;
    SEASON3A::CGM3rdChangeUp &thirdChange_;
    HWND &g_hWnd;
    SessionRenderText &g_RenderText;
    CUIMapName &g_pUIMapName;
    UI::Chat::CHAT (&Chat)[UI::Chat::MAX_CHAT];
    wchar_t (&WhisperRegistID)[UI::Chat::WHISPER_ID_SLOTS][UI::Chat::WHISPER_ID_LENGTH];
    int &g_iNoMouseTime;
    bool &Destroy;
    int AlphaBlendType = 0;
    int &OpenglWindowX;
    int &OpenglWindowY;
    int &OpenglWindowWidth;
    int &OpenglWindowHeight;
    unsigned int &WindowWidth;
    unsigned int &WindowHeight;
    LogicalRenderAssetRef boundTexture_{};
    bool TextureEnable = true;
    bool DepthTestEnable = true;
    bool CullFaceEnable = true;
    bool DepthMaskEnable = true;
    bool AlphaTestEnable = false;
    bool FogEnable = true;
    float FogColor[4] = {30.0F / 256.0F, 20.0F / 256.0F, 10.0F / 256.0F, 0.0F};
    MONSTER_SCRIPT (&MonsterScript)[MAX_MONSTER];
    int &EditMonsterNumber;
    std::wstring &g_strSelectedML;
    CErrorReport &g_ErrorReport;
    SessionModelPool &modelPool_;
    CBoneManager &boneManager_;
    SessionEffectPool<OBJECT> &Effects;
    SessionEffectPool<PARTICLE> &Particles;
    SessionEffectPool<JOINT> &Joints;
    SessionEffectPool<PARTICLE> &Points;
    SessionEffectPool<PARTICLE> &Pointers;
    OBJECT (&Mounts)[MAX_MOUNTS];
    OBJECT (&Boids)[MAX_BOIDS];
    OBJECT (&Fishs)[MAX_FISHS];
    OPERATE (&Operates)[MAX_OPERATES];
    ITEM_t (&Items)[MAX_ITEMS];
    SessionGroundItemLabelStorage &groundItemLabels_;
    std::unique_ptr<SessionSmdStorage> smdStorage_;
    SEASON3A::CMixRecipeMgr &g_MixRecipeMgr;
    SEASON4A::CSocketItemMgr &g_SocketItemMgr;
    CSItemOption &g_csItemOption;
    CSQuest &g_csQuest;
    CQuestMng &g_QuestMng;
    OBJECT_BLOCK (&ObjectBlock)[256];
    int &g_iActionObjectType;
    int &g_iActionWorld;
    float &g_iActionTime;
    float &g_fActionObjectVelocity;

    SessionEffectPool<Blur> &g_blurs;
    SessionEffectPool<ObjectBlur> &g_objectBlurs;
    SessionSpriteStorage &Sprites;
    SessionLeaves &Leaves;
    CQueue<CShadowVolume *> &m_qSV;
    float &Distance;
    vec3_t &CollisionPosition;
    float &SelectXF;
    float &SelectYF;
    MATCH_RESULT &g_wtMatchResult;
    PMSG_MATCH_TIMEVIEW &g_wtMatchTimeLeft;
    CMapManager &gMapManager;
    CCameraMove &cameraMove_;
    CDirection &g_Direction;
    CSkillManager &gSkillManager;
    CSkillEffectMgr &g_SkillEffects;
    CSummonSystem &g_SummonSystem;
    CMonkSystem &g_CMonkSystem;
    CUITextInputBox *&g_pSingleTextInputBox;
    CUITextInputBox *&g_pSinglePasswdInputBox;
    SEASON3B::CNewUIMessageBoxMng &g_MessageBox;
    CPortalMgr &g_PortalMgr;
    CmuConsoleDebug &g_ConsoleDebug;
    CTimer *&g_pTimer;
    std::chrono::steady_clock::time_point &g_timer2StartTickTime;
    struct SavedCameraPerspectiveState
    {
        float perspectiveX = 0.f;
        float perspectiveY = 0.f;
        int screenCenterX = 0;
        int screenCenterY = 0;
        int screenCenterYFlip = 0;
        float matrix[3][4] = {};
    } savedCameraPerspectiveState_;
    bool &g_bRenderBoundingBox;
    float &FPS_ANIMATION_FACTOR;
    double &WorldTime;
    wchar_t (&m_ExeVersion)[11];
    double &FPS_AVG;
    bool &g_bShowDebugInfo;
    bool &g_bShowFpsCounter;
    float (&s_frameTimesMs)[ApplicationFrameStorage::FrameHistorySize];
    int &s_frameIndex;
    int &s_frameCount;
    double &s_highestFps;
    float &s_avgFps;
    float &s_onePercentLow;
    float &s_slowestFrameFps;
    float (&g_frameProfilerAccumulatorMs)[ApplicationFrameStorage::ProfilerPassCount];
    // PLAN_P1R5.1.md section 5.7/8.4: one exact-session facade owned
    // directly here, no global/TLS/focus/slot lookup. Not yet reachable from
    // any legacy call site -- Task 8 routes recording through it.
    LegacyRenderFacade facade_;
    UI::Modern::RmlTooltipLayer tooltipLayer_;
    UI::Modern::RmlPetFrameLayer petFrameLayer_;
    UI::Modern::RmlPartyFrameLayer partyFrameLayer_;
    UI::Modern::RmlMainFrameLayer mainFrameLayer_;
    UI::Modern::RmlTopMenuLayer topMenuLayer_;
    UI::Modern::RmlMoveCommandLayer moveCommandLayer_;
    UI::Modern::RmlPlayerNameLayer playerNameLayer_;
    bool playerNameDisplay_ = true;
    LogicalGeometryAssetLease effectQuadGeometry_;
    TerrainGeometryCache terrainGeometryCache_;
    bool preparedPersistentTerrain_ = false;
    std::vector<TrustedGeometryDraw> trustedGeometryDrawPlan_;
    std::atomic<std::uint64_t> nextGeometryRevision_{1};
    std::optional<RenderTapePass> activeRenderTapePass_;
    bool mainSceneFramePresentable_;
    bool recordingSucceeded_ = true;
    struct RenderTapeDebugStats final
    {
        std::uint64_t frameSequence = 0;
        std::uint64_t generation = 0;
        std::uint64_t surfaceGeneration = 0;
        std::uint32_t viewportWidth = 0;
        std::uint32_t viewportHeight = 0;
        std::size_t entries = 0;
        std::size_t clears = 0;
        std::size_t draws = 0;
        std::size_t vertices = 0;
        std::size_t indices = 0;
        std::size_t quadInstances = 0;
        std::size_t particleInstances = 0;
        std::size_t trailSamples = 0;
        std::size_t trailSegments = 0;
        std::size_t rigidInstances = 0;
        std::size_t constants = 0;
        std::size_t assets = 0;
        std::size_t geometryAssets = 0;
        std::size_t geometryBytes = 0;
        std::size_t ownerRequests = 0;
        std::size_t payloadBytes = 0;
        std::size_t storageBytes = 0;
    } renderTapeDebugStats_;
    RenderTapeFailureDiagnostics lastRenderTapeFailureDiagnostics_{};
    RenderTapeFailure lastReportedFailure_ = RenderTapeFailure::None;
    SessionLifecycleObserver *observer_;
    std::optional<SessionReplayCompletion> lastCompletedTarget_;
    std::unordered_map<LogicalRenderAssetRef, RenderOwnerRequestCompletion,
                       LogicalRenderAssetRefHash>
        pendingTargetCopies_;
    std::optional<SessionGeneration> preparedGeneration_;
    std::uint64_t preparedFrameSequence_ = 0;
    std::uint64_t preparedSurfaceGeneration_ = 0;
    std::uint32_t preparedViewportWidth_ = 0;
    std::uint32_t preparedViewportHeight_ = 0;
    std::uint32_t preparedModernUiViewportWidth_ = 0;
    std::uint32_t preparedModernUiViewportHeight_ = 0;
    std::optional<SessionRenderTapeRecording> preparedRecording_;
    std::optional<SessionUiUnit::ScreenshotRequest> preparedScreenshot_;
    std::optional<SessionRenderTape> preparedTape_;
    RenderTapeFailureDiagnostics preparedDiagnostics_{};
    enum class ModernUiPreparationState : std::uint8_t
    {
        NotRequired,
        Queued,
        Succeeded,
        Failed,
    };
    bool PrepareModernUiOnWorker() noexcept;
    bool WaitForModernUiPreparation() noexcept;
    std::mutex modernUiPreparationMutex_;
    std::condition_variable modernUiPreparationReady_;
    ModernUiPreparationState modernUiPreparationState_ = ModernUiPreparationState::NotRequired;
    bool visualDataReady_ = false;
    bool preparedWebzenScene_ = false;
    std::uint32_t startupLoadStep_ = 0;
    std::uint64_t firstTitleFrameSequence_ = 0;
    std::optional<SessionGeneration> firstTitleFrameGeneration_;
    std::uint64_t firstTitleFrameSurfaceGeneration_ = 0;
    std::uint64_t finalTitleFrameSequence_ = 0;
    std::optional<SessionGeneration> finalTitleFrameGeneration_;
    std::uint64_t finalTitleFrameSurfaceGeneration_ = 0;
};

bool RenderMainScene();

// Frame Timing State

// Scene orchestration

bool RenderScene();
