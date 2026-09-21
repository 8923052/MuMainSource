#pragma once

#include "app/ApplicationLoopFrame.h"
#include "data/ResourceData.h"
#include "domain/ChatSocial.h"
#include "domain/EffectsUpdate.h"
#include "domain/WorldSimulation.h"
#include "render/Sprites.h"
#include "session/SessionRuntime.h"

#include <optional>

class SessionVisualView
{
  public:
    virtual ~SessionVisualView() = default;

    virtual bool UsesSessionOwnedLegacyBehavior() const noexcept
    {
        return false;
    }

    virtual bool UpdateVisualAnimation() noexcept = 0;
    virtual bool UpdateRenderScratch() noexcept = 0;
};

class LegacySessionVisualView final : public SessionVisualView
{
  public:
    bool UsesSessionOwnedLegacyBehavior() const noexcept override
    {
        return true;
    }
    bool UpdateVisualAnimation() noexcept override;
    bool UpdateRenderScratch() noexcept override;
};

class SessionRandom;

class SessionKeeper;
class SessionLifecycleObserver;
class GameSessionTestPeer;
class CmuConsoleDebug;
class CMapManager;
class CameraManager;
class CameraState;
class SessionGameplayUnit;
class PetProcess;
class CBoneManager;
class CSummonSystem;
class CMonkSystem;
class CPhysicsClothMesh;
struct CharacterClothVisual;
struct CharacterLinkedItemVisual;
struct SessionSpriteStorage;
struct AnimationPoseSample;

class SessionVisualUnit final : protected SessionLegacyCalls
{
  public:
    void CreateSnowBursts(OBJECT &object, vec3_t light);
    bool CreateCursedTempleSkillEffect(CHARACTER *character, int skillIndex, int subType);
    void MonsterDieSandSmoke(OBJECT *object, WorldCharacterVisualState &visual);
    void AdvanceButcherVisual(OBJECT &object, BMD &model, bool alive);
    void EmitButcherFire(OBJECT &object, BMD &model);
    void EmitAlternatingSmoke(OBJECT &object, AlternatingSmokeStyle style);
    void AdvanceEnergyNode(OBJECT &object, BMD &model, int lightningSubtype = 8);
    void EmitBoneLightning(OBJECT &object, BMD &model, int subtype);
    void EmitPeriodicFlames(OBJECT &object);
    void InitTerrainLight();
    void ApplySelectedCharacterLighting();
    void EmitMedusaParticles(OBJECT &object, BMD &model, bool dying);
    void EmitQueenRainerLightning(OBJECT &object, BMD &model, const AnimationPoseSample &pose);
    void AdvanceEquipmentSetEnergy(CHARACTER &character, BMD &model);
    void EmitKentaurosDeathSmoke(OBJECT &object, BMD &model, vec3_t light);
    void EmitGenociderDust(OBJECT &object, BMD &model, vec3_t light);
    void AdvanceBurningNapin(OBJECT &object, BMD &model, bool ice);
    void EmitBerserkerSmoke(OBJECT &object, BMD &model, int chance = 6);
    SessionVisualUnit(SessionKeeper &keeper, SessionLifecycleObserver *observer) noexcept;
    ~SessionVisualUnit();

    bool PrepareWorldVisuals(WorldResources &resources, const MapDefinition &definition,
                             WorldResources::Failure &failure);
    bool InstallWorldVisuals(const WorldResources &resources, WorldResources::Failure &failure);
    bool PrepareMonsterVisuals(int type, WorldResources &resources,
                               WorldResources::Failure &failure);
    bool UpdateCameraOnOwner() noexcept;
    bool UpdateAnimation() noexcept;
    bool UpdateRenderScratch() noexcept;
    std::optional<SessionVisualAnimationInput> CaptureAnimationInputOnOwner() const noexcept;
    static bool TryBuildAnimationResult(const SessionVisualAnimationInput &input,
                                        SessionVisualAnimationResult &result) noexcept;
    bool ApplyAnimationResultOnOwner(const SessionVisualAnimationResult &result) noexcept;

    void AdmitCharacterPet(CHARACTER &character);
    void AdvanceBoidVisual(OBJECT &object);
    void AdvanceCharacterAttachments(CHARACTER &character, WorldCharacterVisualState &visual,
                                     bool advance, bool forceRender = false,
                                     OBJECT *localMount = nullptr);
    void AdvanceCharacterCape(CHARACTER &character, WorldCharacterVisualState &visual,
                              bool advance = true);
    void AdvanceCharacterCloth(CHARACTER &character, WorldCharacterVisualState &visual,
                               bool advance = true);
    void AdvanceCharacterGradeItems(CHARACTER &character, WorldCharacterVisualState &visual,
                                    bool advance);
    void AdvanceCharacterHelperPet(CHARACTER &character, WorldCharacterVisualState &visual,
                                   bool advance = true, bool forceRender = false);
    void AdvanceCharacterLinkedItems(CHARACTER &character, WorldCharacterVisualState &visual,
                                     bool advance = true);
    void AdvanceCharacterMount(CHARACTER &character, WorldCharacterVisualState &visual,
                               bool advance = true);
    void AdvanceCharacterPartCloth(CHARACTER &character, WorldCharacterVisualState &visual,
                                   bool advance = true);
    void AdvanceCharacterPet(CHARACTER &character, WorldCharacterVisualState &visual,
                             bool advance = true, bool forceRender = false);
    void AdvanceCherryBlossomVisual(CHARACTER &character);
    void AdvanceCriticalDamageVisual(CHARACTER &character, WorldCharacterVisualState &visual);
    void AdvanceCursedSantaVisual(CHARACTER &character, WorldCharacterVisualState &visual);
    void AdvanceDarkHorseSkill(OBJECT *object, BMD *model, bool emit = true);
    void AdvanceDoppelgangerBoxVisual(CHARACTER &character);
    void AdvanceDoppelgangerVisual(CHARACTER &character);
    void AdvanceEquipmentSetVisual(CHARACTER &character, WorldCharacterVisualState &visual);
    void AdvanceExtendedStateVisual(CHARACTER &character, WorldCharacterVisualState &visual);
    void AdvanceFenrirVisual(OBJECT &object, float previousFrame);
    void AdvanceMountEmissions(OBJECT &object, bool moving);
    void AdvanceGenericCharacterVisual(CHARACTER &character, WorldCharacterVisualState &visual);
    void AdvanceHiddenObjectBlock(OBJECT_BLOCK &block);
    void AdvanceJuliaVisual(CHARACTER &character);
    void AdvanceLinkedItemVisual(CHARACTER &character, WorldCharacterVisualState &visual,
                                 bool advance, int slot, const PART_t &part, int type, bool link,
                                 bool rightHand, float x = 0.f, float y = 0.f, float z = 0.f,
                                 float scaleOverride = 0.f);
    void AdvanceLocalCharacterVisual(CHARACTER &character, WorldCharacterVisualState &visual,
                                     OBJECT *localMount = nullptr);
    void AdvanceMapCharacterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                                   WorldCharacterVisualState &visual);
    void AdvanceMapObservers();
    void AdvanceMovementCharacterVisual(CHARACTER &character, WorldCharacterVisualState &visual);
    void AdvanceNpcCharacterVisual(CHARACTER &character, WorldCharacterVisualState &visual);
    void AdvanceObjectVisual(OBJECT *object);
    void AdvanceParts(CHARACTER &character, WorldCharacterVisualState &visual,
                      bool advanceAnimation = true);
    void AdvancePlayerBuffVisual(CHARACTER &character);
    bool AdvancePlayerSpellVisual(CHARACTER &character, WorldCharacterVisualState &visual);
    void AdvanceProtectGuildMark(CHARACTER &character, WorldCharacterVisualState &visual);
    void AdvanceRabbitVisual(CHARACTER &character, WorldCharacterVisualState &visual);
    void AdvanceSantaNpcVisual(CHARACTER &character, const WorldCharacterVisualState &visual);
    void AdvanceSelectedCharacterEffects();
    void AdvanceSkillEarthQuake(CHARACTER *character, OBJECT *object, BMD *model,
                                WorldCharacterVisualState &visual, int maxSkill);
    void AdvanceSpecialMonsterVisual(CHARACTER &character);
    void AdvanceTitusVisual(CHARACTER &character);
    void AdvanceTopGradeWeaponVisual(CHARACTER &character);
    void AdvanceWeaponSounds(CHARACTER &character, const WorldCharacterVisualState &visual);
    void AdvanceWeaponVisual(CHARACTER &character, const WorldCharacterVisualState &visual);
    void ApplyCharacterPetCommands(CHARACTER &character, WorldCharacterVisualState &visual);
    void BodyLight(const ObjectDrawInput &object, BMD *model);
    void BuildCharacterItemParent(float x, float y, float z, const CharacterDrawInput &character,
                                  const PART_t &part, int type, bool link, bool rightHandItem,
                                  float (&parent)[3][4], vec3_t origin, vec3_t angle, float &scale,
                                  float requestedScale = 0.f);
    void AdvanceLinkedItemPlayback(CHARACTER &character, CharacterLinkedItemVisual &entry,
                                   BMD &model, bool advance, int slot, bool link);
    bool CanPresentLinkedItem(const CHARACTER &character, int type);
    bool CharacterCapeAppearance(CHARACTER &character, vec3_t light);
    bool CharacterWeaponsOnBack(const CHARACTER &character);
    void ConfigureCharacterPartCloth(OBJECT &object, int type, CPhysicsClothMesh &cloth);
    void ConfigureCharacterWeaponPlayback(CHARACTER &character, PART_t &part);
    void CreateCharacterCape(CHARACTER &character, WorldCharacterVisualState &visual);
    int CreateSprite(int type, const vec3_t position, float scale, const vec3_t light,
                     const OBJECT *owner, float rotation = 0.f, int subType = 0);
    void EmitCharacterFormVisual(OBJECT &object);
    void EmitChristmasFormVisual(OBJECT &object, bool finished, float priorFrame);
    void EmitCursedSantaBurst(OBJECT &object, vec3_t position, vec3_t angle);
    void EmitCursedSantaOpening(vec3_t position);
    void EmitHalloweenFormVisual(OBJECT &object, unsigned int sparks, bool burst);
    void EmitLinkedItemSprite(BMD *model, vec34_t *bones, int bitmap, int bone, float scale,
                              vec3_t light, OBJECT *owner);
    void EmitLinkedItemVisual(const CHARACTER &character, const WorldCharacterVisualState &visual,
                              CharacterLinkedItemVisual &linkedItem,
                              const CharacterDrawInput &parentDraw);
    void AdvanceScepterVisual(CharacterLinkedItemVisual &item,
                              const CharacterDrawInput &parentDraw);
    CSPetSystem *FindPetSystem(CHARACTER &character);
    void MoveObject(OBJECT *object);
    void MoveObjects();
    void PrepareCharacterClothPose(CHARACTER &character, CharacterClothVisual &cloth);
    bool PrepareCharacterItemVisualPose(CHARACTER &character, const PART_t &part,
                                        const CharacterLinkedItemVisual *prepared);
    void PrepareCharacterModel(const CHARACTER &character);
    bool PrepareCharacterVisualAppearance(CHARACTER &character, WorldCharacterVisualState &visual);
    void PrepareMovementCharacterPose(CHARACTER &character, WorldCharacterVisualState &visual,
                                      bool advance);
    void PrepareRigidObjectPose(OBJECT &object);
    void PrepareWorldObjectPose(OBJECT &object, float fraction = 1.f);
    void ReconcileCharacterStunVisual(CHARACTER &character, bool removeAbsent);
    void ReconcilePlayerBuffVisual(CHARACTER &character, bool removeAbsent);
    void RetireCharacterVisualLifetime(CHARACTER &character, WorldCharacterVisualState &visual);
    void RetireLinkedItemVisuals(WorldCharacterVisualState &visual);
    const WorldCharacterVisualState *FindCharacterVisual(const CHARACTER &character);

    ObjectDrawInput PrepareDroppedItemDraw(const OBJECT &object);
    void UpdateItemObjectMaterial(OBJECT &object);

    void UpdateItemDrawMaterial(ObjectDrawInput &draw);

    void ComputeIterationBoundsFromHull();

    void BuildHull2DAndBounds(const float *pointsX, const float *pointsY, int pointCount);

    void ExpandHullOutward(float offset);

    void CreateFrustrum2D(vec_t *position);

    void CreateFrustrum(float xAspect, float yAspect, vec_t *position);

    void ResetFrustrumBoundsFullTerrain();

    void CacheActiveFrustum();

    bool TestFrustrum2D(float x, float y, float range);

    bool TestFrustrum(const vec3_t position, float range);

    bool IsBackItem(const CHARACTER *character, int type);

    void PartObjectColor(int type, float alpha, float bright, vec3_t light,
                         bool extraMonster = false);

    void RenderBrightEffect(BMD *model, int bitmap, int link, float scale, vec3_t light,
                            OBJECT *object);

    void AddChat(UI::Chat::CHAT *chat, const wchar_t *chatText, int flag);

    void AddGuildName(UI::Chat::CHAT *chat, CHARACTER *owner);

    int CreateChat(wchar_t *characterName, const wchar_t *chatText, OBJECT *owner, int flag = 0,
                   int setColor = -1);

    void CreateChat(wchar_t *characterName, const wchar_t *chatText, CHARACTER *owner, int flag = 0,
                    int setColor = -1);

    void DetachCharacterChats(CHARACTER *character) noexcept;

    void AssignChat(wchar_t *characterName, const wchar_t *chatText, int flag = 0);

    void MoveChat();

  private:
    friend class SessionLegacyCalls;
    friend class GameSession;
    friend class GameSessionTestPeer;

    bool UpdateSceneVisualAnimation();
    bool UpdateSceneRenderScratch();
    void UpdateWaterAnimation();
    void ManageRenderScratch();
    bool MoveMainCamera();

    FrameTimingState &g_frameTiming;
    double &lastWaterChange;
    std::chrono::steady_clock::time_point &g_timer2StartTickTime;
    float &FPS_ANIMATION_FACTOR;
    CmuConsoleDebug &g_ConsoleDebug;
    CMapManager &gMapManager;
    CameraManager &cameraManager_;
    bool &Destroy;
    std::unique_ptr<SessionSpriteStorage> *mapSpriteOutput_ = nullptr;
    double attachmentSimulationTime_ = 0.0;
    const CHARACTER *preparingCharacter_ = nullptr;
    const WorldCharacterVisualState *preparingCharacterVisual_ = nullptr;
    SessionGameplayUnit &gameplay_;
    PetProcess &g_petProcess;
    CBoneManager &boneManager_;
    OBJECT_BLOCK (&ObjectBlock)[256];
    int &g_iActionObjectType;
    int &g_iActionWorld;
    float &g_iActionTime;
    SessionSpriteStorage &Sprites;
    CSummonSystem &g_SummonSystem;
    CMonkSystem &g_CMonkSystem;
    CameraState &g_Camera;
    unsigned int &WindowWidth;
    unsigned int &WindowHeight;
    UI::Chat::CHAT (&Chat)[UI::Chat::MAX_CHAT];
    SessionRandom &Random;
    double &WorldTime;
    SessionLifecycleObserver *observer_;
};

class SessionKeeper;
class SessionLifecycleObserver;
class GameSessionTestPeer;
class SessionOrderedEffectBatch;

class SessionPresentationUnit final : protected SessionLegacyCalls
{
  public:
    SessionPresentationUnit(SessionKeeper &keeper, SessionLifecycleObserver *observer) noexcept;
    ~SessionPresentationUnit();

    bool BeginFrame() noexcept;
    GameplayInteractionFact PickForLogic() noexcept;
    bool FinishFrame() noexcept;
    bool UpdateVisualAnimation() noexcept;
    std::optional<SessionVisualAnimationInput> CaptureVisualAnimationInputOnOwner() const noexcept;
    bool TryBuildVisualAnimationResult(const SessionVisualAnimationInput &input,
                                       SessionVisualAnimationResult &result) noexcept;
    bool ApplyVisualAnimationResultOnOwner(const SessionVisualAnimationResult &result) noexcept;
    bool UpdateRenderScratch() noexcept;
    bool UpdateSpatialAudio() noexcept;
    bool BuildSpatialAudioEffect(SessionOrderedEffectBatch &effects,
                                 std::uint64_t stableSequence) noexcept;
    bool PrepareInteraction() noexcept;

  private:
    friend class GameSessionTestPeer;

    SessionLifecycleObserver *observer_;
};

#define LDS_BACK_MAX 4

class CUIMng;

class CLoadingScene
{
  protected:
    SessionBoundArray<CSprite, LDS_BACK_MAX> m_asprBack;
    CUIMng &legacyUiManager_;
    unsigned int &WindowWidth;
    unsigned int &WindowHeight;

  public:
    explicit CLoadingScene(SessionKeeper &keeper);
    virtual ~CLoadingScene();

    void Create();
    void Release();
    void Render();
};
