#pragma once
#include "support/CoreMath.h"
#include "session/SessionRuntime.h"
#include "data/WorldData.h"
#include "domain/WorldSimulation.h"
#include "domain/EffectsUpdate.h"
#include "domain/CharacterSystem.h"
#include "render/ModelResources.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationConfigScheduling.h"
#include <optional>
#include <queue>
#include <memory>
#include <vector>
#include <array>
#include <cstdint>

class CMapManager;
class SessionKeeper;
struct WorldCharacterVisualState;

#define g_DoppelGanger1 TheWorld<CGMDoppelGanger1>(WD_65DOPPLEGANGER1)
#define g_DoppelGanger2 TheWorld<CGMDoppelGanger2>(WD_66DOPPLEGANGER2)
#define g_DoppelGanger3 TheWorld<CGMDoppelGanger3>(WD_67DOPPLEGANGER3)
#define g_DoppelGanger4 TheWorld<CGMDoppelGanger4>(WD_68DOPPLEGANGER4)
#define g_DuelArena TheWorld<CGMDuelArena>(WD_64DUELARENA)
#define g_EmpireGuardian1 TheWorld<GMEmpireGuardian1>(WD_69EMPIREGUARDIAN1)
#define g_EmpireGuardian2 TheWorld<GMEmpireGuardian2>(WD_70EMPIREGUARDIAN2)
#define g_EmpireGuardian3 TheWorld<GMEmpireGuardian3>(WD_71EMPIREGUARDIAN3)
#define g_EmpireGuardian4 TheWorld<GMEmpireGuardian4>(WD_72EMPIREGUARDIAN4)
#define g_PKField TheWorld<CGM_PK_Field>(WD_63PK_FIELD)
#define g_Raklion TheWorld<SEASON4A::CGM_Raklion>(WD_57ICECITY)
#define g_SantaTown TheWorld<CGMSantaTown>(WD_62SANTA_TOWN)
#define g_UnitedMarketPlace TheWorld<GMUnitedMarketPlace>(WD_79UNITEDMARKETPLACE)
#define MONSTERNUM 50

struct MapObjectInteraction final
{
    enum class Action
    {
        None,
        Pose,
        Sit,
        Heal
    };
    Action action = Action::None;
    bool faceObject = false;
};

class CGMBattleCastle;
class CGMCrywolf1st;
class CErrorReport;
class CDirection;
class CCameraMove;
class MapProcess;
class WorldResources;

class CMapManager final : protected SessionLegacyCalls
{
  public:
    explicit CMapManager(SessionKeeper &keeper) noexcept;
    virtual ~CMapManager();
    bool IsPreviewContext() const noexcept
    {
        return previewActive_;
    }
    int ContextMap() const noexcept
    {
        return previewActive_ ? WD_0LORENCIA
                              : (binding_.definition ? binding_.definition->BehaviorMap() : -1);
    }
    void DeleteObjects();
    bool InChaosCastle(int iMap = -1);
    bool InBloodCastle(int iMap = -1);
    bool InDevilSquare();
    bool InHellas(int iMap = -1);
    bool InHiddenHellas(int iMap = -1);
    bool IsPKField();
    bool IsCursedTemple();
    bool IsEmpireGuardian1();
    bool IsEmpireGuardian2();
    bool IsEmpireGuardian3();
    bool IsEmpireGuardian4();
    bool IsEmpireGuardian();
    bool InBattleCastle(int iMap = -1);
    const wchar_t *GetMapName(int iMap);

  public:
  private:
    const MapDefinition *DefinitionFor(int rawMap = -1) const noexcept;
    friend class MapProcess;
    friend class World;
    bool InstallWorldAssets(const WorldResources &resources);
    bool ActivateWorld(const MapDefinition &definition, WorldResources &resources);
    void InstallWorldLayout(WorldResources &resources);

    void ConnectBattleCastleForConstruction(CGMBattleCastle &battleCastle) noexcept;
    void ConnectCrywolf1stForConstruction(CGMCrywolf1st &crywolf1st) noexcept;
    void ClearMapOwnersForDestruction(CGMBattleCastle *battleCastle,
                                      CGMCrywolf1st *crywolf1st) noexcept;
    CGMBattleCastle &BattleCastleForUse() const noexcept;
    CGMCrywolf1st &Crywolf1stForUse() const noexcept;
    MapDefinition::Variant EntryTerrainVariant(int rawMap) noexcept;

    const WorldBinding &binding_;
    const bool &previewActive_;
    CGMBattleCastle *battleCastleForConstruction_ = nullptr;
    CGMCrywolf1st *crywolf1stForConstruction_ = nullptr;
    OBJECT (&Boids)[MAX_BOIDS];
    OBJECT (&Fishs)[MAX_FISHS];
    OPERATE (&Operates)[MAX_OPERATES];
    OBJECT_BLOCK (&ObjectBlock)[256];
    CCameraMove &cameraMove_;
    CMapManager &gMapManager;
    CDirection &g_Direction;
    CErrorReport &g_ErrorReport;
    HWND &g_hWnd;
};

class BaseMap
{
  public:
    BaseMap(float &animationFactor, double &worldTime) noexcept
        : FPS_ANIMATION_FACTOR(animationFactor), WorldTime(worldTime)
    {
    }
    virtual ~BaseMap()
    {
        clear();
    }

  public:
    virtual void InstallBehavior()
    {
    }
    virtual bool TerrainCutscene() const
    {
        return false;
    }

  public: // Object
    virtual MapObjectInteraction ObjectInteraction(int, CHARACTER &)
    {
        return {};
    }
    virtual void RenderAtmosphere()
    {
    }
    virtual void RenderMapInterface()
    {
    }
    virtual void BeginSceneRender()
    {
    }
    virtual void EndSceneRender()
    {
    }
    virtual void FinishObjectAction()
    {
    }

    virtual bool ActionObject(OBJECT *)
    {
        return false;
    }
    virtual void PrepareObjectEffects(int &, int)
    {
    }
    virtual void MoveObjectEffects(OBJECT *, int &, int &)
    {
    }
    virtual void PrepareObjectLight(const ObjectDrawInput &, BMD &)
    {
    }

    virtual std::optional<bool> ObjectVisibility(const OBJECT &, bool)
    {
        return std::nullopt;
    }
    virtual bool ObjectEffectsVisible(const OBJECT &)
    {
        return true;
    }
    virtual void AdvanceObjectVisibility(OBJECT &)
    {
    }
    virtual void AdvanceObjectFade(OBJECT &)
    {
    }
    virtual void RenderEarlyAfterCharacterObjects(OBJECT *)
    {
    }
    virtual bool IsEarlyAfterCharacterObject(const OBJECT &) const
    {
        return false;
    }
    virtual void AdvanceEnvironment()
    {
    }
    virtual void AdvanceTerrainEffects()
    {
    }

    virtual void PrepareObjectUpdate(OBJECT *)
    {
    }
    virtual float ObjectAnimationSpeed(const OBJECT &, const BMD &, float speed) const
    {
        return speed;
    }
    virtual bool CreateObject(OBJECT *o)
    {
        return false;
    }
    virtual bool MoveObject(OBJECT *o)
    {
        return false;
    }
    virtual bool MoveObject(OBJECT *o, CTimer2::StartTickTime &timer2StartTickTime)
    {
        (void)timer2StartTickTime;
        return MoveObject(o);
    }
    virtual bool RenderObjectVisual(const ObjectDrawInput &input, BMD *)
    {
        return false;
    }
    virtual bool AdvanceObjectVisual(OBJECT *, BMD *, float = 0.f)
    {
        return false;
    }
    // True replaces the complete common object pass, including its decorations.
    virtual bool RenderWholeObject(const ObjectDrawInput &input, BMD *, bool)
    {
        return false;
    }
    virtual bool RenderObjectMeshBeforeShared(const ObjectDrawInput &input, BMD *, bool)
    {
        return false;
    }
    // True replaces only the body; common decorations still follow.
    virtual bool RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0)
    {
        const auto &draw = input;
        const auto *o = input.source;
        return false;
    }
    virtual void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0)
    {
        const auto &draw = input;
        const auto *o = input.source;
    }

    virtual void RenderFrontSideVisual()
    {
    }

  public: // Character
    virtual bool CanObserveCharacter(const CHARACTER &)
    {
        return true;
    }
    virtual void AdvancePlayerVisual(CHARACTER *, OBJECT *)
    {
    }
    virtual void BeginCharacterTick()
    {
    }
    virtual void ObserveCharacterTick(const CHARACTER &)
    {
    }
    virtual void FinishCharacterTick()
    {
    }

    virtual bool PushCharacter(CHARACTER *, OBJECT *, float)
    {
        return false;
    }
    virtual bool AdvanceCharacterDeath(CHARACTER *, OBJECT *)
    {
        return false;
    }
    virtual void AdvanceCharacterStopTime()
    {
    }

    virtual bool StopMonster(CHARACTER *, OBJECT *)
    {
        return false;
    }
    virtual void MoveCharacterState(CHARACTER *, OBJECT *)
    {
    }
    virtual bool SetMonsterDeathAction(CHARACTER *, OBJECT *)
    {
        return false;
    }
    virtual bool PlayMonsterDeathSound(OBJECT *)
    {
        return false;
    }
    virtual float MonsterDeathRotationRate(const OBJECT &, float rate)
    {
        return rate;
    }
    virtual float ItemDrawHeight(const OBJECT &object, int)
    {
        return object.Position[2];
    }

    virtual void AdvanceMonsterState(CHARACTER &, BMD &)
    {
    }
    virtual void CaptureMonsterAttackState(CHARACTER &, BMD &)
    {
    }
    virtual CHARACTER *CreateMonster(int iType, int PosX, int PosY, int Key)
    {
        return NULL;
    }
    virtual bool MoveMonsterVisual(CHARACTER *, OBJECT *o, BMD *b,
                                   WorldCharacterVisualState &visual)
    {
        return false;
    }
    virtual void MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b)
    {
    }
    virtual bool RenderMonsterVisual(const CHARACTER *, const ObjectDrawInput &input, BMD *)
    {
        return false;
    }
    virtual bool AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                      WorldCharacterVisualState &visual)
    {
        return false;
    }
    virtual bool AttackEffectBeforeShared(CHARACTER *, OBJECT *, BMD *)
    {
        return false;
    }
    virtual bool AttackEffectMonster(CHARACTER *c, OBJECT *o, BMD *b)
    {
        return false;
    }
    virtual bool SetCurrentActionMonster(CHARACTER *c, OBJECT *o)
    {
        return false;
    }

    virtual bool PrepareAmbientBoidSlot(int index, bool &)
    {
        return index < 5;
    }
    virtual bool CanCreateAmbientBoid(int, int)
    {
        return false;
    }
    virtual bool ConfigureAmbientBoid(OBJECT *, int)
    {
        return false;
    }
    virtual bool CanCreateAmbientFish(int)
    {
        return false;
    }
    virtual void ConfigureAmbientFish(OBJECT *)
    {
    }

  public: // Weather
    virtual int PrepareWeather()
    {
        return 80;
    }
    virtual bool CreateWeather(PARTICLE *, int)
    {
        return false;
    }
    virtual bool MoveWeather(PARTICLE *)
    {
        return false;
    }

  public: // Sound
    virtual ESound WalkingSound(int, bool) const
    {
        return SOUND_HUMAN_WALK_GROUND;
    }
    virtual int PlayerNpcText(bool)
    {
        return 0;
    }

    virtual void PlayAmbientSounds()
    {
    }
    virtual bool AllowsAmbientSound(ESound) const
    {
        return false;
    }
    virtual void UpdateMusic()
    {
    }
    virtual bool AllowsMusic(const char *) const
    {
        return false;
    }

    virtual bool PlayMonsterSound(OBJECT *o)
    {
        return false;
    }
    virtual void PlayObjectSound(OBJECT *o)
    {
    }

  public: // Server Message
    virtual bool ReceiveMapMessage(BYTE code, BYTE subcode, BYTE *ReceiveBuffer)
    {
        return false;
    }

  public:
    void clear()
    {
        m_MapTypes.clear();
    }

  public:
    bool isMapIndex(ENUM_WORLD type)
    {
        for (int i = 0; i < (int)m_MapTypes.size(); ++i)
        {
            if (type == m_MapTypes[i])
            {
                return true;
            }
        }
        return false;
    }

    ENUM_WORLD FindMapIndex(int index = 0)
    {
        if (m_MapTypes.size() == 0 || index >= (int)m_MapTypes.size())
            return NUM_WD;
        else
            return m_MapTypes[index];
    }

  public:
    const bool IsCurrentMap(int type)
    {
        return isMapIndex(static_cast<ENUM_WORLD>(type));
    }

  public:
    void AddMapIndex(ENUM_WORLD type)
    {
        if (!isMapIndex(type))
            m_MapTypes.push_back(type);
    }

  public:
    typedef std::vector<ENUM_WORLD> WorldVector;

  protected:
    float &FPS_ANIMATION_FACTOR;
    double &WorldTime;

  private:
    WorldVector m_MapTypes;
};

class CGMAida;

SmartPointer(CGMAida);

class CGMAida final : public BaseMap, protected SessionLegacyCalls
{
  public:
    bool CanCreateAmbientFish(int index) override;

    void ConfigureAmbientFish(OBJECT *object) override;

    bool CreateWeather(PARTICLE *particle, int index) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    void InstallBehavior() override;

    bool AttackEffectBeforeShared(CHARACTER *character, OBJECT *object, BMD *model) override;

    bool SetCurrentActionMonster(CHARACTER *character, OBJECT *object) override;

    bool RenderObjectVisual(const ObjectDrawInput &input, BMD *model) override;

    bool RenderObjectMesh(const ObjectDrawInput &input, BMD *model,
                          bool extraMonster = false) override;

    bool CreateObject(OBJECT *object) override;
    bool MoveObject(OBJECT *object) override;
    bool AdvanceObjectVisual(OBJECT *object, BMD *model, float = 0.f) override;

    static CGMAidaPtr Make(SessionKeeper &keeper);
    ~CGMAida() override;

    bool IsInAida();
    bool IsInAidaSection2(const vec3_t position);
    bool CreateAidaObject(OBJECT *object);
    bool MoveAidaObject(OBJECT *object);
    bool RenderAidaObjectVisual(const ObjectDrawInput &input, BMD *model);
    bool AdvanceAidaObjectVisual(OBJECT *object, BMD *model);
    bool RenderAidaObjectMesh(const ObjectDrawInput &input, BMD *model, bool extraMonster = false);
    CHARACTER *CreateAidaMonster(int type, int positionX, int positionY, int key);
    bool MoveAidaMonsterVisual(OBJECT *object, BMD *model, WorldCharacterVisualState &visual);
    void MoveAidaBlurEffect(CHARACTER *character, OBJECT *object, BMD *model);
    bool RenderAidaMonsterObjectMesh(const ObjectDrawInput &input, BMD *model, bool extraMonster);
    bool AdvanceAidaMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                                  WorldCharacterVisualState &visual);
    void EmitMonsterActionSounds(OBJECT &object);
    bool AttackEffectAidaMonster(CHARACTER *character, OBJECT *object, BMD *model);
    bool SetCurrentActionAidaMonster(CHARACTER *character, OBJECT *object);
    bool CreateMist(PARTICLE *particle);

  private:
    explicit CGMAida(SessionKeeper &keeper) noexcept;

    CMapManager &gMapManager;
};

class GMAtlans final : public BaseMap, protected SessionLegacyCalls
{
  public:
    MapObjectInteraction ObjectInteraction(int type, CHARACTER &actor) override;
    float MonsterDeathRotationRate(const OBJECT &object, float rate) override;

    ESound WalkingSound(int tile, bool safe) const override;

    bool PrepareAmbientBoidSlot(int index, bool &allowCreate) override;

    bool CanCreateAmbientBoid(int slot, int terrainIndex) override;

    bool ConfigureAmbientBoid(OBJECT *object, int index) override;

    bool CanCreateAmbientFish(int index) override;

    void ConfigureAmbientFish(OBJECT *o) override;

    bool CreateWeather(PARTICLE *o, int) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    bool AllowsAmbientSound(ESound sound) const override;

    void PlayAmbientSounds() override;

    explicit GMAtlans(SessionKeeper &keeper) noexcept;
    bool CreateObject(OBJECT *o) override;
    bool MoveObject(OBJECT *o) override;

  private:
    SessionRandom &Random;
};

#ifndef __GMBATTLECASTLE_H__
#define __GMBATTLECASTLE_H__

class CameraProjection;
class CameraState;

SmartPointer(CGMBattleCastle);

class CGMBattleCastle final : public BaseMap, protected SessionLegacyCalls
{
  public:
    void RenderMapInterface() override;
    void RenderAtmosphere() override;
    void BeginSceneRender() override;
    void EndSceneRender() override;
    MapObjectInteraction ObjectInteraction(int type, CHARACTER &actor) override;
    void MoveCharacterState(CHARACTER *character, OBJECT *object) override;

    bool StopMonster(CHARACTER *character, OBJECT *object) override;

    void MoveObjectEffects(OBJECT *object, int &count, int &visible) override;

    void PrepareObjectEffects(int &count, int previousVisible) override;

    int PrepareWeather() override;

    bool CreateWeather(PARTICLE *particle, int index) override;

    void InstallBehavior() override;

    bool RenderMonsterVisual(const CHARACTER *character, const ObjectDrawInput &input,
                             BMD *model) override;
    void AdvanceMonsterState(CHARACTER &character, BMD &model) override;
    bool AdvanceMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                              WorldCharacterVisualState &visual) override;

    bool MoveMonsterVisual(CHARACTER *, OBJECT *object, BMD *model,
                           WorldCharacterVisualState &visual) override;
    bool AttackEffectBeforeShared(CHARACTER *character, OBJECT *object, BMD *model) override;

    bool RenderObjectMesh(const ObjectDrawInput &input, BMD *model,
                          bool extraMonster = false) override;

    bool CreateObject(OBJECT *object) override;
    bool MoveObject(OBJECT *object) override;
    bool AdvanceObjectVisual(OBJECT *object, BMD *model, float = 0.f) override;

    void AdvanceStructurePresentationState(CHARACTER &character);
    bool RenderBattleCastleMonsterVisual(const CHARACTER *c, const ObjectDrawInput &input, BMD *b);
    static CGMBattleCastlePtr Make(SessionKeeper &keeper);
    ~CGMBattleCastle() override = default;

    bool IsBattleCastleStart();
    bool InBattleCastle2(vec3_t position);
    bool InBattleCastle3(vec3_t position);
    void SetBattleCastleStart(bool result);
    bool InArea(float x, float y, vec3_t position, float range);
    void CollisionHeroCharacter(vec3_t position, float range, int animationType);
    void CollisionTempCharacter(vec3_t position, float range, int animationType);
    bool CollisionEffectToObject(OBJECT *effect, float range, float rangeZ, bool collisionGround,
                                 bool realCollision = false);
    bool CalcDistanceChrToChr(OBJECT *object, BYTE type, float range);
    void SetCastleGate_Attribute(int x, int y, BYTE operation, bool allClear = false);
    void RenderAurora(int type, int renderType, float x, float y, float sizeX, float sizeY,
                      const vec3_t light);
    void SetBuildTimeLocation(OBJECT *object);
    void RenderBuildTimes();
    void Init();
    bool SettingBattleFormation(CHARACTER *character, eBuffState state);
    bool GetGuildMaster(CHARACTER *character);
    void SettingBattleKing(CHARACTER *character);
    void DeleteBattleFormation(CHARACTER *character, eBuffState state);
    void ChangeBattleFormation(wchar_t *guildName, bool effect = false);
    bool CanObserveCharacter(const CHARACTER &character) override;
    void AdvancePlayerVisual(CHARACTER *character, OBJECT *object) override;
    void DeleteTmpCharacter();
    void StartFog(vec3_t color);
    void EndFog();
    void RenderBaseSmoke();
    bool CreateFireSnuff(PARTICLE *particle);
    void SetAttackDefenseObjectType(OBJECT *object);
    bool MoveBattleCastleObjectSetting(int &objectCount, int object);
    bool MoveBattleCastleObject(OBJECT *object, int &objectIndex, int &visibleObjectIndex);
    bool CreateBattleCastleObject(OBJECT *object);
    bool MoveBattleCastleVisual(OBJECT *object);
    bool AdvanceBattleCastleVisual(OBJECT *object, BMD *model);
    bool RenderBattleCastleObjectMesh(const ObjectDrawInput &input, BMD *model);
    void MoveFlyBigStone(OBJECT *object);
    CHARACTER *CreateBattleCastleMonster(EMonsterType type, int positionX, int positionY, int key);
    bool SettingBattleCastleMonsterLinkBone(CHARACTER *character, int type);
    bool StopBattleCastleMonster(CHARACTER *character, OBJECT *object);
    void InitGateAttribute();
    void BeginCharacterTick() override;
    void ObserveCharacterTick(const CHARACTER &character) override;
    void FinishCharacterTick() override;
    bool MoveBattleCastleMonster(CHARACTER *character, OBJECT *object);
    bool SetCurrentAction_BattleCastleMonster(CHARACTER *character, OBJECT *object);
    bool AttackEffect_BattleCastleMonster(CHARACTER *character, OBJECT *object, BMD *model);
    void CreateGuardStoneHealingVisual(CHARACTER *character, float range);
    void EmitStructureDeath(OBJECT *object, BMD *model, WorldCharacterVisualState &visual);
    void AdvanceStructureState(OBJECT *object, BMD *model);
    bool MoveBattleCastleMonsterVisual(OBJECT *object, BMD *model,
                                       WorldCharacterVisualState &visual);
    bool AdvanceBattleCastleMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                                          WorldCharacterVisualState &visual);
    bool RenderBattleCastleMonsterObjectMesh(const ObjectDrawInput &input, BMD *model);
    void EmitMonsterHitEffect(OBJECT *object);

  private:
    void ApplyBattleFormation(CHARACTER &character, const wchar_t *guildName);
    using SessionLegacyCalls::CreateObject;

    struct BuildTime
    {
        vec3_t m_vPosition;
        BYTE m_byBuildTime;
    };

    explicit CGMBattleCastle(SessionKeeper &keeper) noexcept;

    static constexpr BYTE g_byGateLocation[6][2] = {{67, 114}, {93, 114},  {119, 114},
                                                    {81, 161}, {107, 161}, {93, 204}};
    static constexpr float g_fGuardStoneLocation[4][2] = {
        {8200.0f, 13000.0f}, {10700.0f, 13000.0f}, {9400.0f, 18200.0f}, {9400.0f, 22700.0f}};
    static constexpr float HealingParticleInterval = 400.0f;
    static constexpr float StoneFlyEffectInterval = 800.0f;
    static constexpr float ArrowEffectOnBattlefieldInterval = 2400.0f;
    static constexpr float ArrowEffectInStampRoomInterval = 400.0f;

    OBJECT (&Mounts)[MAX_MOUNTS];
    OBJECT_BLOCK (&ObjectBlock)[256];
    CMapManager &gMapManager;
    CameraState &g_Camera;
    CameraProjection &cameraProjection_;
    BYTE g_byGuardAI = 0;
    bool g_bBeGate = false;
    float g_fLifeStoneLocation[2] = {0.0f, 0.0f};
    bool g_isCrownState = false;
    DWORD g_dwCrownBackState = 0;
    bool g_bBattleCastleStart = false;
    bool g_bBattleCastleStartBackup = false;
    float g_iMp3PlayTime = 0.0f;
    float LastHealingParticle = 0.0f;
    float LastStoneFlyEffect = 0.0f;
    float LastArrowEffectOnBattlefield = 0.0f;
    float LastArrowEffectInStampRoom = 0.0f;
    std::queue<BuildTime> g_qBuildTimeLocation;
};

#endif // __GMBATTLECASTLE_H__

class GMBloodCastle final : public BaseMap, protected SessionLegacyCalls
{
  public:
    bool AdvanceCharacterDeath(CHARACTER *, OBJECT *) override;
    void PrepareObjectEffects(int &objCount, int previousVisible) override;

    bool CanCreateAmbientBoid(int slot, int terrainIndex) override;

    bool ConfigureAmbientBoid(OBJECT *object, int index) override;

    void InstallBehavior() override;

    bool RenderWholeObject(const ObjectDrawInput &input, BMD *model, bool extraMonster) override;
    explicit GMBloodCastle(SessionKeeper &keeper) noexcept;
    bool MoveObject(OBJECT *o) override;
    bool AdvanceObjectVisual(OBJECT *o, BMD *b, float Luminosity) override;
};

class SessionRandom;

class GMChaosCastle final : public BaseMap, protected SessionLegacyCalls
{
  public:
    bool PushCharacter(CHARACTER *, OBJECT *, float) override;
    bool AdvanceCharacterDeath(CHARACTER *, OBJECT *) override;
    void AdvanceCharacterStopTime() override;
    bool PlayMonsterDeathSound(OBJECT *object) override;

    bool ActionObject(OBJECT *object) override;
    void FinishObjectAction() override;

    void MoveObjectEffects(OBJECT *object, int &count, int &visible) override;

    void PrepareObjectEffects(int &count, int previousVisible) override;

    int PrepareWeather() override;

    bool MoveWeather(PARTICLE *o) override;

    bool CreateWeather(PARTICLE *o, int Index) override;

    void InstallBehavior() override;

    void AdvanceEnvironment() override;

    void AdvanceObjectFade(OBJECT &object) override;

    explicit GMChaosCastle(SessionKeeper &keeper) noexcept;
    bool CreateObject(OBJECT *object) override;
    bool AdvanceObjectVisual(OBJECT *object, BMD *model, float = 0.f) override;

    void ChangeChaosCastleUnit(CHARACTER *character);
    bool MoveChaosCastleObjectSetting(int &objectCount, int object);
    bool MoveChaosCastleObject(OBJECT *object, int &objectCount, int &visibleObjectCount);
    bool MoveChaosCastleAllObject(OBJECT *object);
    bool CreateChaosCastleObject(OBJECT *object);
    bool AdvanceChaosCastleVisual(OBJECT *object, BMD *model);
    void AdvanceChaosTerrain();

  private:
    void AdvanceChaosTerrainCell(int x, int y);
    CMapManager &gMapManager;
    SessionRandom &Random;
    float &actionTime_;
    float &actionObjectVelocity_;
};

SmartPointer(CGMCryingWolf2nd);

class CGMCryingWolf2nd final : public BaseMap, protected SessionLegacyCalls
{
  public:
    bool MoveMonsterVisual(CHARACTER *, OBJECT *object, BMD *model,
                           WorldCharacterVisualState &visual) override;
    bool AttackEffectBeforeShared(CHARACTER *character, OBJECT *object, BMD *model) override;

    bool RenderObjectMesh(const ObjectDrawInput &input, BMD *model,
                          bool extraMonster = false) override;

    bool CreateObject(OBJECT *object) override;
    bool MoveObject(OBJECT *object) override;
    bool AdvanceObjectVisual(OBJECT *object, BMD *model, float = 0.f) override;

    static CGMCryingWolf2ndPtr Make(SessionKeeper &keeper);
    ~CGMCryingWolf2nd() override;

    bool IsCyringWolf2nd();
    bool CreateCryingWolf2ndObject(OBJECT *object);
    bool MoveCryingWolf2ndObject(OBJECT *object);
    bool AdvanceCryingWolf2ndObjectVisual(OBJECT *object, BMD *model);
    bool RenderCryingWolf2ndObjectMesh(const ObjectDrawInput &input, BMD *model);
    CHARACTER *CreateCryingWolf2ndMonster(int type, int positionX, int positionY, int key);
    bool MoveCryingWolf2ndMonsterVisual(OBJECT *object, BMD *model,
                                        WorldCharacterVisualState &visual);
    bool RenderCryingWolf2ndMonsterObjectMesh(const ObjectDrawInput &input, BMD *model);
    bool AdvanceCryingWolf2ndMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                                           WorldCharacterVisualState &visual);
    void MoveCryingWolf2ndBlurEffect(CHARACTER *character, OBJECT *object, BMD *model);
    bool AttackEffectCryingWolf2ndMonster(CHARACTER *character, OBJECT *object, BMD *model);

  private:
    explicit CGMCryingWolf2nd(SessionKeeper &keeper) noexcept;

    CMapManager &gMapManager;
};

class CGMCrywolf1st;
class SessionRenderText;

SmartPointer(CGMCrywolf1st);

class CGMCrywolf1st final : public BaseMap, protected SessionLegacyCalls
{
  public:
    void RenderAtmosphere() override;
    void AdvanceBallistaState(OBJECT &object);
    void PrepareObjectEffects(int &, int) override;
    void AdvanceTerrainEffects() override;
    bool CanCreateAmbientFish(int index) override;

    void ConfigureAmbientFish(OBJECT *o) override;

    int PrepareWeather() override;

    bool MoveWeather(PARTICLE *particle) override;

    bool CreateWeather(PARTICLE *particle, int index) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    void InstallBehavior() override;

    bool RenderMonsterVisual(const CHARACTER *character, const ObjectDrawInput &input,
                             BMD *model) override;
    bool AdvanceMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                              WorldCharacterVisualState &visual) override;

    bool AttackEffectMonster(CHARACTER *character, OBJECT *object, BMD *model) override;

    bool SetCurrentActionMonster(CHARACTER *character, OBJECT *object) override;

    bool RenderObjectMeshBeforeShared(const ObjectDrawInput &input, BMD *model,
                                      bool extraMonster = false) override;

    bool CreateObject(OBJECT *object) override;
    bool MoveObject(OBJECT *object) override;
    bool AdvanceObjectVisual(OBJECT *object, BMD *model, float = 0.f) override;

    bool RenderCryWolf1stMonsterVisual(const CHARACTER *c, const ObjectDrawInput &input, BMD *b);
    static CGMCrywolf1stPtr Make(SessionKeeper &keeper);
    ~CGMCrywolf1st() override = default;

    void CryWolfMVPInit();
    int IsCryWolf1stMVPStart();
    bool IsCryWolf1stMVPStatePeace();
    MapDefinition::Variant TerrainVariant() const noexcept;
    void CheckCryWolf1stMVP(BYTE occupationState, BYTE crywolfState);
    void CheckCryWolf1stMVPAltarfInfo(int statueHp, BYTE altarState1, BYTE altarState2,
                                      BYTE altarState3, BYTE altarState4, BYTE altarState5);
    void DoTankerFireFixStartPosition(int sourceX, int sourceY, int positionX, int positionY);
    void AdvanceNotices();
    void RenderNoticesCryWolf();
    void ChangeBackGroundMusic(int world);

    bool IsCyrWolf1st();
    bool CreateCryWolf1stObject(OBJECT *object);
    bool MoveCryWolf1stObject(OBJECT *object);
    bool AdvanceCryWolf1stObjectVisual(OBJECT *object, BMD *model);
    bool RenderCryWolf1stObjectMesh(const ObjectDrawInput &input, BMD *model, int extraMonster);

    CHARACTER *CreateCryWolf1stMonster(int type, int positionX, int positionY, int key);
    bool MoveCryWolf1stMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                                     WorldCharacterVisualState &visual);
    bool AttackEffectCryWolf1stMonster(CHARACTER *character, OBJECT *object, BMD *model);
    void MoveCryWolf1stBlurEffect(CHARACTER *character, OBJECT *object, BMD *model);
    bool RenderCryWolf1stMonsterObjectMesh(const ObjectDrawInput &input, BMD *model,
                                           int extraMonster);
    bool AdvanceCryWolf1stMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                                        WorldCharacterVisualState &visual);
    bool SetCurrentActionCrywolfMonster(CHARACTER *character, OBJECT *object);

    bool CreateMist(PARTICLE *particle);
    void RenderBaseSmoke();

    bool Get_State();
    bool Get_State_Only_Elf() const;
    void SetTime(BYTE hour, BYTE minute);
    void Set_BossMonster(int hp, int darkElfCount);
    void Check_AltarState(int number, int state);
    void Set_Message_Box(int stringId, int number, int key, int objectNumber = -1);
    void Set_Hp(int state);
    void Set_Val_Hp(int state);
    bool Get_AltarState_State(int number);
    void Set_MyRank(BYTE rank, int experience);
    void Set_WorldRank(BYTE rank, CLASS_TYPE characterClass, int score, wchar_t *heroName);

    // Exact per-session Crywolf event state. Legacy UI consumers reach these
    // fields only through their originating session's typed MapProcess route.
    BYTE m_AltarState[5] = {2, 2, 2, 2, 2};
    bool View_Bal = false;
    char Suc_Or_Fail = -1;
    char View_Suc_Or_Fail = -1;
    float Deco_Insert = 0.0f;
    char Message_Box = 0;
    wchar_t Box_String[2][200] = {};
    int Dark_elf_Num = 0;
    int Button_Down = 0;
    int BackUp_Key = 0;
    int Val_Hp = 100;
    int m_iHour = 0;
    int m_iMinute = 0;
    DWORD m_dwSyncTime = 0;
    int Delay = 1;
    int Add_Num = 10;
    bool Dark_Elf_Check = false;
    int iNextNotice = -1;
    BYTE Rank = 0;
    int Exp = 0;
    BYTE Ranking[5] = {};
    CLASS_TYPE HeroClass[5] = {};
    int HeroScore[5] = {-1, -1, -1, -1, -1};
    wchar_t HeroName[5][MAX_USERNAME_SIZE + 1] = {};
    int BackUpMin = 0;
    bool TimeStart = false;
    int Delay_Add_inter = 390;
    bool View_End_Result = false;
    int nPastTick = 0;
    int BackUpTick = 0;
    BYTE m_OccupationState = 0;
    BYTE m_CrywolfState = 0;
    int m_StatueHP = 0;
    double weatherChangeCounter_ = 0;
    float flareRotation_ = 0.f;
    void AdvanceFlare(OBJECT &object);

  private:
    void AdvanceWaterTile(int x, int y);
    explicit CGMCrywolf1st(SessionKeeper &keeper);
    void ReloadTerrainVariant();

    CMapManager &gMapManager;
    SessionRenderText &g_RenderText;
};

class GMDevias final : public BaseMap, protected SessionLegacyCalls
{
  public:
    MapObjectInteraction ObjectInteraction(int type, CHARACTER &actor) override;
    int PlayerNpcText(bool actionChanged) override;

    ESound WalkingSound(int tile, bool safe) const override;

    bool CreateWeather(PARTICLE *o, int) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    bool AllowsAmbientSound(ESound sound) const override;

    void PlayAmbientSounds() override;

    void InstallBehavior() override;

    bool ObjectEffectsVisible(const OBJECT &object) override;

    bool RenderObjectVisual(const ObjectDrawInput &input, BMD *b) override;
    void PrepareObjectUpdate(OBJECT *o) override;
    explicit GMDevias(SessionKeeper &keeper) noexcept;
    bool CreateObject(OBJECT *o) override;
    bool MoveObject(OBJECT *o) override;
    bool AdvanceObjectVisual(OBJECT *o, BMD *b, float Luminosity) override;

  private:
    SessionRandom &Random;
};

class GMDevilSquare final : public BaseMap, protected SessionLegacyCalls
{
  public:
    void PrepareObjectLight(const ObjectDrawInput &object, BMD &model) override;

    int PrepareWeather() override;

    bool MoveWeather(PARTICLE *o) override;

    bool CreateWeather(PARTICLE *o, int Index) override;

    bool AllowsAmbientSound(ESound sound) const override;

    bool RenderMonsterVisual(const CHARACTER *character, const ObjectDrawInput &input,
                             BMD *model) override;
    bool AdvanceMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                              WorldCharacterVisualState &visual) override;

    bool AttackEffectMonster(CHARACTER *character, OBJECT *object, BMD *model) override;
    bool SetCurrentActionMonster(CHARACTER *character, OBJECT *object) override;
    void PrepareObjectUpdate(OBJECT *o) override;
    explicit GMDevilSquare(SessionKeeper &keeper) noexcept;
    bool AdvanceObjectVisual(OBJECT *o, BMD *b, float Luminosity) override;

  private:
    SessionRandom &Random;
};

SmartPointer(CGMDoppelGanger1);

class CGMDoppelGanger1 : public BaseMap, protected SessionLegacyCalls
{
  public:
    static constexpr float AppearanceEndFrame = 18.f;
    bool MoveSharedMonsterVisual(OBJECT &object, BMD &model, WorldCharacterVisualState &visual);
    void EmitAppearanceBurst(OBJECT &object, float fraction);
    void RenderAtmosphere() override;
    bool CreateWeather(PARTICLE *, int) override;
    ESound WalkingSound(int, bool) const override;
    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    bool RenderMonsterVisual(const CHARACTER *c, const ObjectDrawInput &input, BMD *b);
    static CGMDoppelGanger1Ptr Make(SessionKeeper &keeper);
    virtual ~CGMDoppelGanger1();

    bool IsDoppelGanger1();

  public:
    virtual bool CreateObject(OBJECT *o);
    virtual bool MoveObject(OBJECT *o);
    virtual bool AdvanceObjectVisual(OBJECT *o, BMD *b, float = 0.f);
    virtual bool RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);
    virtual void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);

  public:
    virtual CHARACTER *CreateMonster(int iType, int PosX, int PosY, int Key);
    virtual bool MoveMonsterVisual(CHARACTER *, OBJECT *o, BMD *b,
                                   WorldCharacterVisualState &visual);
    virtual void MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b);
    virtual bool AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                      WorldCharacterVisualState &visual);

  public:
    virtual bool PlayMonsterSound(OBJECT *o);
    void PlayBGM();

  public:
    void Init();
    void Destroy();

  protected:
    void EmitMist(OBJECT *object);
    explicit CGMDoppelGanger1(SessionKeeper &keeper);
    CMapManager &gMapManager;

    BOOL m_bIsMP3Playing;
};

class CameraState;

SmartPointer(CGMDoppelGanger2);
class CGMDoppelGanger2 : public BaseMap, protected SessionLegacyCalls
{
  public:
    bool CreateWeather(PARTICLE *particle, int index) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    void RenderEarlyAfterCharacterObjects(OBJECT *head) override;

    bool IsEarlyAfterCharacterObject(const OBJECT &object) const override;

    std::optional<bool> ObjectVisibility(const OBJECT &object, bool blockVisible) override;

    bool RenderMonsterVisual(const CHARACTER *c, const ObjectDrawInput &input, BMD *b);
    static CGMDoppelGanger2Ptr Make(SessionKeeper &keeper);
    virtual ~CGMDoppelGanger2();

    bool IsDoppelGanger2();

  public:
    virtual bool CreateObject(OBJECT *o);
    virtual bool MoveObject(OBJECT *o);
    virtual bool AdvanceObjectVisual(OBJECT *o, BMD *b, float = 0.f);
    virtual bool RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);
    virtual void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);

  public:
    bool CreateFireSpark(PARTICLE *o);

  public:
    virtual bool MoveMonsterVisual(CHARACTER *, OBJECT *o, BMD *b,
                                   WorldCharacterVisualState &visual);
    virtual void MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b);
    virtual bool AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                      WorldCharacterVisualState &visual);

  public:
    virtual bool PlayMonsterSound(OBJECT *o);

  public:
    void Init();
    void Destroy();

  protected:
    explicit CGMDoppelGanger2(SessionKeeper &keeper);
    CMapManager &gMapManager;
    CameraState &g_Camera;
};

SmartPointer(CGMDoppelGanger3);
class CGMDoppelGanger3 : public BaseMap, protected SessionLegacyCalls
{
  public:
    ESound WalkingSound(int tile, bool safe) const override;

    bool PrepareAmbientBoidSlot(int index, bool &allowCreate) override;

    bool CanCreateAmbientBoid(int slot, int terrainIndex) override;

    bool ConfigureAmbientBoid(OBJECT *object, int index) override;

    bool CanCreateAmbientFish(int index) override;

    void ConfigureAmbientFish(OBJECT *object) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    bool RenderMonsterVisual(const CHARACTER *c, const ObjectDrawInput &input, BMD *b);
    static CGMDoppelGanger3Ptr Make(SessionKeeper &keeper);
    virtual ~CGMDoppelGanger3();

    bool IsDoppelGanger3();

  public: // Object
    virtual bool CreateObject(OBJECT *o);
    virtual bool MoveObject(OBJECT *o);
    virtual bool AdvanceObjectVisual(OBJECT *o, BMD *b, float = 0.f);
    virtual bool RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);
    virtual void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);

  public: // Character
    virtual bool MoveMonsterVisual(CHARACTER *, OBJECT *o, BMD *b,
                                   WorldCharacterVisualState &visual);
    virtual void MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b);
    virtual bool AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                      WorldCharacterVisualState &visual);

  public: // Sound
    virtual bool PlayMonsterSound(OBJECT *o);

  public:
    void Init();
    void Destroy();

  protected:
    explicit CGMDoppelGanger3(SessionKeeper &keeper);
    CMapManager &gMapManager;
};

SmartPointer(CGMDoppelGanger4);
class CGMDoppelGanger4 : public BaseMap, protected SessionLegacyCalls
{
  public:
    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    void InstallBehavior() override;

    bool RenderMonsterVisual(const CHARACTER *c, const ObjectDrawInput &input, BMD *b);
    static CGMDoppelGanger4Ptr Make(SessionKeeper &keeper);
    virtual ~CGMDoppelGanger4();

    bool IsDoppelGanger4();

  public:
    virtual bool CreateObject(OBJECT *o);
    virtual bool MoveObject(OBJECT *o);
    virtual bool RenderObjectVisual(const ObjectDrawInput &input, BMD *model) override;
    bool AdvanceObjectVisual(OBJECT *o, BMD *b, float = 0.f);
    virtual bool RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);
    virtual void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);

  public:
    virtual bool MoveMonsterVisual(CHARACTER *, OBJECT *o, BMD *b,
                                   WorldCharacterVisualState &visual);
    virtual void MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b);
    virtual bool AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                      WorldCharacterVisualState &visual);

  public:
    virtual bool PlayMonsterSound(OBJECT *o);

  public:
    void Init();
    void Destroy();

  protected:
    explicit CGMDoppelGanger4(SessionKeeper &keeper);
    CMapManager &gMapManager;
};

SmartPointer(CGMDuelArena);
class CGMDuelArena : public BaseMap, protected SessionLegacyCalls
{
  public:
    static CGMDuelArenaPtr Make(SessionKeeper &keeper);
    virtual ~CGMDuelArena();

    bool IsDuelArena();

  public:
    virtual bool CreateObject(OBJECT *o);
    virtual bool MoveObject(OBJECT *o);
    virtual bool AdvanceObjectVisual(OBJECT *o, BMD *b, float = 0.f);

  public:
    virtual bool AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                      WorldCharacterVisualState &visual);

  public:
    void PlayBGM();

  public:
    void Init();
    void Destroy();

  protected:
    explicit CGMDuelArena(SessionKeeper &keeper);
    CMapManager &gMapManager;
};

class GMDungeon final : public BaseMap, protected SessionLegacyCalls
{
  public:
    MapObjectInteraction ObjectInteraction(int type, CHARACTER &actor) override;
    bool StopMonster(CHARACTER *character, OBJECT *object) override;

    bool CanCreateAmbientBoid(int slot, int terrainIndex) override;

    bool ConfigureAmbientBoid(OBJECT *object, int index) override;

    bool CanCreateAmbientFish(int index) override;

    void ConfigureAmbientFish(OBJECT *o) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    bool AllowsAmbientSound(ESound sound) const override;

    void PlayAmbientSounds() override;

    void InstallBehavior() override;

    explicit GMDungeon(SessionKeeper &keeper) noexcept;
    bool CreateObject(OBJECT *o) override;
    bool MoveObject(OBJECT *o) override;
};

SmartPointer(GMEmpireGuardian1);

class GMEmpireGuardian1 : public BaseMap, protected SessionLegacyCalls
{
  public:
    void EmitMonsterEvents(OBJECT &object, BMD &model, WorldCharacterVisualState &visual);
    void EmitRaymondEvents(OBJECT &object, BMD &model, const vec3_t light);
    bool CreateWeather(PARTICLE *particle, int index) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    void InstallBehavior() override;

    void AdvanceEnvironment() override;

    void AdvanceMonsterState(CHARACTER &character, BMD &model) override;
    void AdvanceGatePlacement(OBJECT *object);
    static GMEmpireGuardian1Ptr Make(SessionKeeper &keeper);
    virtual ~GMEmpireGuardian1();

  public: // Object
    virtual bool CreateObject(OBJECT *o);
    virtual bool MoveObject(OBJECT *o);
    void AdvanceGateProjectile(OBJECT *object, BMD *model);
    virtual bool AdvanceObjectVisual(OBJECT *o, BMD *b, float = 0.f);
    virtual bool RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);
    virtual void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);
    virtual void RenderFrontSideVisual();
    bool RenderMonster(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);

  public: // Character
    virtual CHARACTER *CreateMonster(int iType, int PosX, int PosY, int Key);
    virtual void MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b);
    virtual bool AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                      WorldCharacterVisualState &visual);
    virtual bool AttackEffectMonster(CHARACTER *c, OBJECT *o, BMD *b);
    virtual bool SetCurrentActionMonster(CHARACTER *c, OBJECT *o);

    bool MoveStructureVisual(OBJECT *o, BMD *b);
    bool MoveMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                           WorldCharacterVisualState &visual) override;

  public: // Sound
    virtual bool PlayMonsterSound(OBJECT *o);
    virtual void PlayObjectSound(OBJECT *o);
    void PlayBGM();

  public:
    void Init();
    void Destroy();

  public: //Weather
    enum WEATHER_TYPE
    {
        WEATHER_SUN = 0,
        WEATHER_RAIN = 1,
        WEATHER_FOG = 2,
        WEATHER_STORM = 3,
    };

    bool CreateRain(PARTICLE *o);
    void AdvanceWeather();

    void SetWeather(int weather)
    {
        m_iWeather = weather;
    }
    int GetWeather()
    {
        return m_iWeather;
    }

  private:
    int m_iWeather;
    bool stormFlash_ = false;

  private:
    bool m_bCurrentIsRage_Raymond;
    bool m_bCurrentIsRage_Ercanne;
    bool m_bCurrentIsRage_Daesuler;
    bool m_bCurrentIsRage_Gallia;
    float objectAnimationFrame_ = 0.f;

  protected:
    explicit GMEmpireGuardian1(SessionKeeper &keeper);
    CMapManager &gMapManager;
};

SmartPointer(GMEmpireGuardian2);

class GMEmpireGuardian2 : public BaseMap, protected SessionLegacyCalls
{
  public:
    bool CreateWeather(PARTICLE *particle, int index) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    void InstallBehavior() override;

    void AdvanceEnvironment() override;

    void AdvanceMonsterState(CHARACTER &character, BMD &model) override;
    static GMEmpireGuardian2Ptr Make(SessionKeeper &keeper);
    virtual ~GMEmpireGuardian2();

  public:
    virtual bool CreateObject(OBJECT *o);
    virtual bool MoveObject(OBJECT *o);
    virtual bool AdvanceObjectVisual(OBJECT *o, BMD *b, float = 0.f);
    virtual bool RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);
    virtual void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);
    virtual void RenderFrontSideVisual();
    bool RenderMonster(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);

  public:
    virtual CHARACTER *CreateMonster(int iType, int PosX, int PosY, int Key);
    virtual void MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b);
    virtual bool AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                      WorldCharacterVisualState &visual);
    virtual bool AttackEffectMonster(CHARACTER *c, OBJECT *o, BMD *b);
    virtual bool SetCurrentActionMonster(CHARACTER *c, OBJECT *o);
    bool MoveMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                           WorldCharacterVisualState &visual) override;

  public:
    virtual bool PlayMonsterSound(OBJECT *o);
    virtual void PlayObjectSound(OBJECT *o);
    void PlayBGM();

  public:
    bool CreateRain(PARTICLE *o);
    void SetWeather(int weather);

  private:
    bool m_bCurrentIsRage_Bermont;
    float objectAnimationFrame_ = 0.f;

  public:
    void Init();
    void Destroy();

  protected:
    explicit GMEmpireGuardian2(SessionKeeper &keeper);
    CMapManager &gMapManager;
};

SmartPointer(GMEmpireGuardian3);

class GMEmpireGuardian3 : public BaseMap, protected SessionLegacyCalls
{
  public:
    bool CreateWeather(PARTICLE *particle, int index) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    void InstallBehavior() override;

    void AdvanceEnvironment() override;

    void AdvanceMonsterState(CHARACTER &character, BMD &model) override;
    bool RenderMonsterVisual(const CHARACTER *c, const ObjectDrawInput &input, BMD *b);
    static GMEmpireGuardian3Ptr Make(SessionKeeper &keeper);
    virtual ~GMEmpireGuardian3();

  public:
    virtual bool CreateObject(OBJECT *o);
    virtual bool MoveObject(OBJECT *o);
    virtual bool AdvanceObjectVisual(OBJECT *o, BMD *b, float = 0.f);
    virtual bool RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);
    virtual void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);
    virtual void RenderFrontSideVisual();
    bool RenderMonster(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);

  public:
    virtual CHARACTER *CreateMonster(int iType, int PosX, int PosY, int Key);
    virtual void MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b);
    virtual bool AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                      WorldCharacterVisualState &visual);
    virtual bool AttackEffectMonster(CHARACTER *c, OBJECT *o, BMD *b);
    virtual bool SetCurrentActionMonster(CHARACTER *c, OBJECT *o);
    void EmitBansheeEvents(CHARACTER &character, BMD &model);
    bool MoveSharedMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                 WorldCharacterVisualState &visual);
    bool MoveMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                           WorldCharacterVisualState &visual) override;

  public: // Sound
    virtual bool PlayMonsterSound(OBJECT *o);
    virtual void PlayObjectSound(OBJECT *o);
    void PlayBGM();

  public:
    void Init();
    void Destroy();

  public: //Weather
    bool CreateRain(PARTICLE *o);
    void SetWeather(int weather);

  private:
    bool m_bCurrentIsRage_Kato;
    float objectAnimationFrame_ = 0.f;

  protected:
    explicit GMEmpireGuardian3(SessionKeeper &keeper);
    CMapManager &gMapManager;
};

SmartPointer(GMEmpireGuardian4);

class GMEmpireGuardian4 final : public BaseMap, protected SessionLegacyCalls
{
  public:
    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    void InstallBehavior() override;

    void AdvanceEnvironment() override;

    void AdvanceMonsterState(CHARACTER &character, BMD &model) override;
    void AdvanceGatePlacement(OBJECT *object);
    static GMEmpireGuardian4Ptr Make(SessionKeeper &keeper);
    virtual ~GMEmpireGuardian4();

  public:
    virtual bool CreateObject(OBJECT *o);
    virtual bool MoveObject(OBJECT *o);
    virtual bool AdvanceObjectVisual(OBJECT *o, BMD *b, float = 0.f);
    virtual bool RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);
    virtual void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);
    virtual void RenderFrontSideVisual();
    bool RenderMonster(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);

  public:
    virtual CHARACTER *CreateMonster(int iType, int PosX, int PosY, int Key);
    virtual void MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b);
    virtual bool AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                      WorldCharacterVisualState &visual);
    virtual bool AttackEffectMonster(CHARACTER *c, OBJECT *o, BMD *b);
    virtual bool SetCurrentActionMonster(CHARACTER *c, OBJECT *o);
    bool MoveStructureVisual(OBJECT *o, BMD *b);
    bool MoveMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                           WorldCharacterVisualState &visual) override;

  public:
    virtual bool PlayMonsterSound(OBJECT *o);
    virtual void PlayObjectSound(OBJECT *o);
    void PlayBGM();

  public:
    void Init();
    void Destroy();

  public: //Weather
    void SetWeather(int weather);

  private:
    bool m_bCurrentIsRage_BossGaion;
    bool m_bCurrentIsRage_Jerint;
    float objectAnimationFrame_ = 0.f;

  protected:
    explicit GMEmpireGuardian4(SessionKeeper &keeper);
    CMapManager &gMapManager;
};

SmartPointer(CGMGmArea);

class CGMGmArea final : public BaseMap, protected SessionLegacyCalls
{
  public:
    bool CreateObject(OBJECT *object) override;
    bool MoveObject(OBJECT *object) override;
    bool AdvanceObjectVisual(OBJECT *object, BMD *model, float) override;
    bool RenderObjectVisual(const ObjectDrawInput &input, BMD *model) override;
    bool RenderObjectMesh(const ObjectDrawInput &input, BMD *model, bool) override;
    void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *model, bool) override;
    static CGMGmAreaPtr Make(SessionKeeper &keeper);

    bool IsGmArea();

  private:
    explicit CGMGmArea(SessionKeeper &keeper) noexcept;

    CMapManager &gMapManager;
};

class CameraProjection;
class CameraState;
class CSWaterTerrain;
class SessionRenderText;

SmartPointer(CGMHellas);

class CGMHellas final : public BaseMap, protected SessionLegacyCalls
{
  public:
    float ItemDrawHeight(const OBJECT &object, int index) override;

    ESound WalkingSound(int tile, bool safe) const override;

    void PrepareObjectEffects(int &count, int previousVisible) override;

    bool PrepareAmbientBoidSlot(int index, bool &allowCreate) override;

    bool CanCreateAmbientBoid(int slot, int terrainIndex) override;

    bool ConfigureAmbientBoid(OBJECT *object, int index) override;

    bool CanCreateAmbientFish(int index) override;

    void ConfigureAmbientFish(OBJECT *o) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    void InstallBehavior() override;

    bool AttackEffectBeforeShared(CHARACTER *character, OBJECT *object, BMD *model) override;

    bool RenderObjectMesh(const ObjectDrawInput &input, BMD *model,
                          bool extraMonster = false) override;

    bool CreateObject(OBJECT *object) override;
    bool MoveObject(OBJECT *object) override;
    bool AdvanceObjectVisual(OBJECT *object, BMD *model, float = 0.f) override;

    static CGMHellasPtr Make(SessionKeeper &keeper);
    ~CGMHellas() override;

    bool CreateWaterTerrain(int mapIndex);
    bool IsWaterTerrain();
    void AddWaterWave(int x, int y, int range, int height);
    void MoveWaterTerrain();
    bool RenderWaterTerrain();
    void DeleteWaterTerrain();
    float GetWaterTerrain(float x, float y);
    void RenderWaterTerrain(int texture, float x, float y, float sizeX, float sizeY,
                            const vec3_t light, float rotation = 0.0f, float alpha = 1.0f,
                            float height = 0.0f);
    void SettingHellasColor();
    BYTE GetHellasLevel(CLASS_TYPE characterClass, int level);
    bool EnableKalima(CLASS_TYPE characterClass, int level, int itemLevel);
    bool GetUseLostMap(bool drawAlert = false);
    int RenderHellasItemInfo(ITEM *item, int textNumber);
    void AddObjectDescription(wchar_t *text, vec3_t position);
    void RenderObjectDescription();
    bool MoveHellasObjectSetting(int &objectCount, int object);
    bool MoveHellasObject(OBJECT *object, int &objectIndex, int &visibleObjectIndex);
    bool MoveHellasAllObject(OBJECT *object);
    bool CreateHellasObject(OBJECT *object);
    bool MoveHellasVisual(OBJECT *object);
    void CheckGrass(OBJECT *object);
    bool AdvanceHellasVisual(OBJECT *object, BMD *model);
    bool RenderHellasObjectMesh(const ObjectDrawInput &input, BMD *model);
    int CreateBigMon(OBJECT *object);
    float MoveBigMon(OBJECT *object, float frames, bool refresh, float startingLife);
    void CreateMonsterSkill_ReduceDef(OBJECT *object, int attackTime, BYTE time, float height);
    void CreateMonsterSkill_Poison(OBJECT *object, int attackTime, BYTE time);
    void CreateMonsterSkill_Summon(OBJECT *object, int attackTime, BYTE time);
    void SetActionDestroy_Def(OBJECT *object);
    CHARACTER *CreateHellasMonster(EMonsterType type, int positionX, int positionY, int key);
    bool SettingHellasMonsterLinkBone(CHARACTER *character, int type);
    bool SetCurrentAction_HellasMonster(CHARACTER *character, OBJECT *object);
    bool AttackEffect_HellasMonster(CHARACTER *character, CHARACTER *targetCharacter,
                                    OBJECT *object, OBJECT *targetObject, BMD *model);
    void MonsterMoveWaterSmoke(OBJECT *object);
    void MonsterDieWaterSmoke(OBJECT *object);
    void AdvanceMonsterState(CHARACTER &character);
    void AdvanceKundunState(OBJECT &object);
    void EmitMonsterMeshEffects(OBJECT *object, BMD *model, WorldCharacterVisualState &visual);
    void EmitKundunDeath(OBJECT *object, BMD *model, WorldCharacterVisualState &visual);
    void EmitKundunEvents(OBJECT &object, BMD &model);
    bool MoveHellasMonsterVisual(OBJECT *object, BMD *model, WorldCharacterVisualState &visual);
    bool AdvanceHellasMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                                    WorldCharacterVisualState &visual);
    bool RenderHellasMonsterObjectMesh(const ObjectDrawInput &input, BMD *model);

  private:
    friend class GameSessionTestPeer;
    explicit CGMHellas(SessionKeeper &keeper) noexcept;

    CMapManager &gMapManager;
    CameraState &g_Camera;
    CameraProjection &cameraProjection_;
    SessionRenderText &g_RenderText;
    std::unique_ptr<CSWaterTerrain> g_pCSWaterTerrain;
    std::queue<ObjectDescript> g_qObjDes;
    float LastAmbientSoundPlay = 0.0f;
    float LastKundunSoundPlay = 0.0f;
    float LastBigMonCreation = 0.0f;
};

class CGMHuntingGround;

SmartPointer(CGMHuntingGround);

class CGMHuntingGround final : public BaseMap, protected SessionLegacyCalls
{
  public:
    bool CreateWeather(PARTICLE *particle, int index) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    void InstallBehavior() override;

    bool AttackEffectBeforeShared(CHARACTER *character, OBJECT *object, BMD *model) override;

    bool SetCurrentActionMonster(CHARACTER *character, OBJECT *object) override;

    bool RenderObjectMesh(const ObjectDrawInput &input, BMD *model,
                          bool extraMonster = false) override;

    bool CreateObject(OBJECT *object) override;
    bool MoveObject(OBJECT *object) override;
    bool AdvanceObjectVisual(OBJECT *object, BMD *model, float = 0.f) override;

    void AdvanceMonsterState(OBJECT &object);
    static CGMHuntingGroundPtr Make(SessionKeeper &keeper);
    ~CGMHuntingGround() override;

    bool IsInHuntingGround();
    bool IsInHuntingGroundSection2(const vec3_t position);
    bool CreateHuntingGroundObject(OBJECT *object);
    bool MoveHuntingGroundObject(OBJECT *object);
    bool AdvanceHuntingGroundObjectVisual(OBJECT *object, BMD *model);
    bool RenderHuntingGroundObjectMesh(const ObjectDrawInput &input, BMD *model,
                                       bool extraMonster = false);
    CHARACTER *CreateHuntingGroundMonster(int type, int positionX, int positionY, int key);
    bool MoveHuntingGroundMonsterVisual(OBJECT *object, BMD *model,
                                        WorldCharacterVisualState &visual);
    void MoveHuntingGroundBlurEffect(CHARACTER *character, OBJECT *object, BMD *model);
    bool RenderHuntingGroundMonsterObjectMesh(const ObjectDrawInput &input, BMD *model,
                                              bool extraMonster);
    bool AdvanceHuntingGroundMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                                           WorldCharacterVisualState &visual);
    void EmitFireGolemEvents(OBJECT &object, BMD &model);
    void EmitMonsterActionSounds(OBJECT &object);
    bool AttackEffectHuntingGroundMonster(CHARACTER *character, OBJECT *object, BMD *model);
    bool SetCurrentActionHuntingGroundMonster(CHARACTER *character, OBJECT *object);
    bool CreateMist(PARTICLE *particle);

  private:
    explicit CGMHuntingGround(SessionKeeper &keeper) noexcept;

    CMapManager &gMapManager;
    DWORD g_MusicStartStamp = 0;
};

class GMIcarus final : public BaseMap, protected SessionLegacyCalls
{
  public:
    float ItemDrawHeight(const OBJECT &object, int index) override;

    void PrepareObjectLight(const ObjectDrawInput &object, BMD &model) override;

    void MoveObjectEffects(OBJECT *object, int &count, int &visible) override;

    void PrepareObjectEffects(int &objCount, int previousVisible) override;

    void PrepareThunder(int previousVisible);

    bool PrepareAmbientBoidSlot(int index, bool &allowCreate) override;

    bool CanCreateAmbientBoid(int slot, int terrainIndex) override;

    bool ConfigureAmbientBoid(OBJECT *object, int index) override;

    int PrepareWeather() override;

    bool MoveWeather(PARTICLE *particle) override;

    bool CreateWeather(PARTICLE *o, int index) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    bool AllowsAmbientSound(ESound sound) const override;

    void PlayAmbientSounds() override;

    std::optional<bool> ObjectVisibility(const OBJECT &object, bool blockVisible) override;

    explicit GMIcarus(SessionKeeper &keeper) noexcept;
    bool AdvanceObjectVisual(OBJECT *o, BMD *b, float Luminosity) override;

  private:
    void EmitSkyLightning(const vec3_t origin);
    std::vector<float> thunderBirthTimes_;
    SessionRandom &Random;
};

#if !defined(AFX_GMKARUTAN1_H__A2F56C80_26D8_4474_AECE_63DA2DA511A9__INCLUDED_)
#define AFX_GMKARUTAN1_H__A2F56C80_26D8_4474_AECE_63DA2DA511A9__INCLUDED_

#ifdef ASG_ADD_MAP_KARUTAN

SmartPointer(CGMKarutan1);

class CGMKarutan1 : public BaseMap, protected SessionLegacyCalls
{
  public:
    void EmitCondraEvents(OBJECT &object, BMD &model, WorldCharacterVisualState &visual);
    void EmitCondraDeath(OBJECT &object, BMD &model, float fraction);

  protected:
    explicit CGMKarutan1(SessionKeeper &keeper);

  public:
    void RenderAtmosphere() override;
    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    bool AllowsAmbientSound(ESound sound) const override;

    void PlayAmbientSounds() override;

    void InstallBehavior() override;

    float ObjectAnimationSpeed(const OBJECT &object, const BMD &model, float speed) const override;

    virtual ~CGMKarutan1();

    static CGMKarutan1Ptr Make(SessionKeeper &keeper);

    bool IsKarutanMap();

    // Object
    virtual bool CreateObject(OBJECT *o);
    virtual bool MoveObject(OBJECT *o);
    virtual bool AdvanceObjectVisual(OBJECT *o, BMD *b, float = 0.f);
    virtual bool RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);
    virtual void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);

#ifdef ASG_ADD_KARUTAN_MONSTERS
    // Character
    virtual CHARACTER *CreateMonster(int iType, int PosX, int PosY, int Key);
    virtual bool MoveMonsterVisual(CHARACTER *, OBJECT *o, BMD *b,
                                   WorldCharacterVisualState &visual);
    virtual void MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b);
    virtual bool AttackEffectMonster(CHARACTER *c, OBJECT *o, BMD *b);

    // Sound
    virtual bool PlayMonsterSound(OBJECT *o);
#endif // ASG_ADD_KARUTAN_MONSTERS
    virtual void PlayObjectSound(OBJECT *o);
    void PlayBGM();

    CMapManager &gMapManager;
};

#endif // ASG_ADD_MAP_KARUTAN

#endif // !defined(AFX_GMKARUTAN1_H__A2F56C80_26D8_4474_AECE_63DA2DA511A9__INCLUDED_)

class GMLegacyLogin final : public BaseMap, protected SessionLegacyCalls
{
  public:
    bool CreateWeather(PARTICLE *particle, int index) override;

    void InstallBehavior() override;

    bool RenderWholeObject(const ObjectDrawInput &input, BMD *model, bool extraMonster) override;
    explicit GMLegacyLogin(SessionKeeper &keeper) noexcept;
    bool CreateObject(OBJECT *o) override;
    bool MoveObject(OBJECT *o) override;
    bool AdvanceObjectVisual(OBJECT *o, BMD *b, float Luminosity) override;
};

class GMLorencia final : public BaseMap, protected SessionLegacyCalls
{
  public:
    MapObjectInteraction ObjectInteraction(int type, CHARACTER &actor) override;
    int PlayerNpcText(bool actionChanged) override;

    ESound WalkingSound(int tile, bool safe) const override;

    bool CanCreateAmbientBoid(int slot, int terrainIndex) override;

    bool ConfigureAmbientBoid(OBJECT *object, int index) override;

    bool CanCreateAmbientFish(int index) override;

    void ConfigureAmbientFish(OBJECT *o) override;

    bool MoveWeather(PARTICLE *particle) override;

    bool MoveAirWeather(PARTICLE *o, bool splash);

    bool CreateWeather(PARTICLE *o, int) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    bool AllowsAmbientSound(ESound sound) const override;

    void PlayAmbientSounds() override;

    bool RenderWholeObject(const ObjectDrawInput &input, BMD *model, bool extraMonster) override;
    void PrepareObjectUpdate(OBJECT *o) override;
    explicit GMLorencia(SessionKeeper &keeper) noexcept;
    bool CreateObject(OBJECT *o) override;
    bool MoveObject(OBJECT *o) override;
    bool AdvanceObjectVisual(OBJECT *o, BMD *b, float Luminosity) override;

  private:
    SessionRandom &Random;
};

class GMLostTower final : public BaseMap, protected SessionLegacyCalls
{
  public:
    bool CanCreateAmbientBoid(int slot, int terrainIndex) override;

    bool ConfigureAmbientBoid(OBJECT *object, int index) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    bool AllowsAmbientSound(ESound sound) const override;

    void PlayAmbientSounds() override;

    bool RenderWholeObject(const ObjectDrawInput &input, BMD *model, bool extraMonster) override;
    explicit GMLostTower(SessionKeeper &keeper) noexcept;
    bool MoveObject(OBJECT *o) override;
    bool AdvanceObjectVisual(OBJECT *o, BMD *b, float Luminosity) override;
};

class GMEmpireGuardian4;

namespace SEASON3B
{
SmartPointer(GMNewTown);

class GMNewTown final : public BaseMap, protected SessionLegacyCalls
{
  public:
    MapObjectInteraction ObjectInteraction(int type, CHARACTER &actor) override;
    bool PrepareAmbientBoidSlot(int index, bool &allowCreate) override;

    bool CanCreateAmbientBoid(int slot, int terrainIndex) override;

    bool ConfigureAmbientBoid(OBJECT *object, int index) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    bool AllowsAmbientSound(ESound sound) const override;

    void InstallBehavior() override;

    void RenderEarlyAfterCharacterObjects(OBJECT *head) override;

    bool IsEarlyAfterCharacterObject(const OBJECT &object) const override;

    void AdvanceObjectVisibility(OBJECT &object) override;

    std::optional<bool> ObjectVisibility(const OBJECT &object, bool blockVisible) override;

    void AdvanceMonsterState(CHARACTER &character, BMD &model) override;
    static GMNewTownPtr Make(SessionKeeper &keeper);
    ~GMNewTown() override;

    bool IsCurrentMap() const;
    bool IsNewMap73_74() const;

    bool CreateObject(OBJECT *object) override;
    bool MoveObject(OBJECT *object) override;
    bool AdvanceObjectVisual(OBJECT *object, BMD *model, float = 0.f) override;
    bool RenderObjectMesh(const ObjectDrawInput &input, BMD *model,
                          bool extraMonster = false) override;
    void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *model,
                               bool extraMonster = false) override;

    CHARACTER *CreateMonster(int type, int positionX, int positionY, int key) override;
    void MoveSharedMonsterBlur(CHARACTER *character, OBJECT *object, BMD *model);
    bool AdvanceMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                              WorldCharacterVisualState &visual) override;

    bool PlayMonsterSound(OBJECT *object) override;
    void PlayObjectSound(OBJECT *object) override;

    bool IsCheckMouseIn() const noexcept;
    bool CharacterSceneCheckMouse(OBJECT *object);

  private:
    using SessionLegacyCalls::CreateMonster;

    explicit GMNewTown(SessionKeeper &keeper);

    OBJECT (&Boids)[MAX_BOIDS];
    CMapManager &gMapManager;
    GMEmpireGuardian4 &empireGuardian4_;
    bool m_bCharacterSceneCheckMouse = false;
};
} // namespace SEASON3B

class GMNoria final : public BaseMap, protected SessionLegacyCalls
{
  public:
    MapObjectInteraction ObjectInteraction(int type, CHARACTER &actor) override;
    ESound WalkingSound(int tile, bool safe) const override;

    bool CanCreateAmbientBoid(int slot, int terrainIndex) override;

    bool ConfigureAmbientBoid(OBJECT *object, int index) override;

    bool CreateWeather(PARTICLE *particle, int index) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    bool AllowsAmbientSound(ESound sound) const override;

    void PlayAmbientSounds() override;

    void InstallBehavior() override;

    bool RenderWholeObject(const ObjectDrawInput &input, BMD *model, bool extraMonster) override;
    explicit GMNoria(SessionKeeper &keeper) noexcept;
    bool CreateObject(OBJECT *o) override;
    bool MoveObject(OBJECT *o) override;
    bool AdvanceObjectVisual(OBJECT *o, BMD *b, float Luminosity) override;
};

SmartPointer(CGMSantaTown);

class CGMSantaTown : public BaseMap, protected SessionLegacyCalls
{
  public:
    ESound WalkingSound(int tile, bool safe) const override;

    bool CreateWeather(PARTICLE *particle, int index) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    static CGMSantaTownPtr Make(SessionKeeper &keeper);
    virtual ~CGMSantaTown();

    bool IsSantaTown();

  public: // Object
    virtual bool CreateObject(OBJECT *o);
    virtual bool MoveObject(OBJECT *o);
    virtual bool AdvanceObjectVisual(OBJECT *o, BMD *b, float = 0.f);
    virtual bool RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);

  public: // Character
    virtual CHARACTER *CreateMonster(int iType, int PosX, int PosY, int Key);
    virtual bool AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                      WorldCharacterVisualState &visual);

  public: // Sound
    void PlayBGM();

  public:
    void Init();
    void Destroy();
    bool CreateSnow(PARTICLE *o);

  protected:
    explicit CGMSantaTown(SessionKeeper &keeper);
    CMapManager &gMapManager;
};

class GMStadium final : public BaseMap, protected SessionLegacyCalls
{
  public:
    bool CanCreateAmbientFish(int index) override;

    void ConfigureAmbientFish(OBJECT *o) override;

    explicit GMStadium(SessionKeeper &keeper) noexcept;
    bool MoveObject(OBJECT *o) override;
    bool AdvanceObjectVisual(OBJECT *o, BMD *b, float Luminosity) override;
};

namespace SEASON3C
{
SmartPointer(GMSwampOfQuiet);

class GMSwampOfQuiet final : public BaseMap, protected SessionLegacyCalls
{
  public:
    void RenderAtmosphere() override;
    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    void AdvanceMonsterState(CHARACTER &character, BMD &model) override;
    static GMSwampOfQuietPtr Make(SessionKeeper &keeper);
    ~GMSwampOfQuiet() override;

    bool IsCurrentMap() const;
    void RenderBaseSmoke();

    bool CreateObject(OBJECT *object) override;
    bool MoveObject(OBJECT *object) override;
    bool AdvanceObjectVisual(OBJECT *object, BMD *model, float = 0.f) override;
    bool RenderSharedMonsterMesh(const ObjectDrawInput &input, BMD *model,
                                 bool extraMonster = false);
    void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *model,
                               bool extraMonster = false) override;

    CHARACTER *CreateMonster(int type, int positionX, int positionY, int key) override;
    bool MoveMonsterVisual(CHARACTER *, OBJECT *object, BMD *model,
                           WorldCharacterVisualState &visual) override;
    void MoveSharedMonsterBlur(CHARACTER *character, OBJECT *object, BMD *model);
    bool AdvanceMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                              WorldCharacterVisualState &visual) override;
    bool AttackEffectMonster(CHARACTER *character, OBJECT *object, BMD *model) override;
    bool SetCurrentActionMonster(CHARACTER *character, OBJECT *object) override;

    bool PlayMonsterSound(OBJECT *object) override;

  private:
    explicit GMSwampOfQuiet(SessionKeeper &keeper);

    CMapManager &gMapManager;
};
} // namespace SEASON3C

class GMTarkan final : public BaseMap, protected SessionLegacyCalls
{
  public:
    void RenderAtmosphere() override;
    MapObjectInteraction ObjectInteraction(int type, CHARACTER &actor) override;
    bool CanCreateAmbientFish(int index) override;

    void ConfigureAmbientFish(OBJECT *o) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    bool AllowsAmbientSound(ESound sound) const override;

    void PlayAmbientSounds() override;

    void InstallBehavior() override;

    bool RenderWholeObject(const ObjectDrawInput &input, BMD *model, bool extraMonster) override;
    float ObjectAnimationSpeed(const OBJECT &object, const BMD &model, float speed) const override;
    explicit GMTarkan(SessionKeeper &keeper) noexcept;
    bool CreateObject(OBJECT *o) override;
    bool MoveObject(OBJECT *o) override;
    bool AdvanceObjectVisual(OBJECT *o, BMD *b, float Luminosity) override;
};

SmartPointer(GMUnitedMarketPlace);

class GMUnitedMarketPlace : public BaseMap, protected SessionLegacyCalls
{
  public:
    MapObjectInteraction ObjectInteraction(int type, CHARACTER &actor) override;
    bool MoveWeather(PARTICLE *particle) override;

    bool CreateWeather(PARTICLE *particle, int index) override;

    bool AllowsAmbientSound(ESound sound) const override;

    void PlayAmbientSounds() override;

    static GMUnitedMarketPlacePtr Make(SessionKeeper &keeper);
    virtual ~GMUnitedMarketPlace();

    bool IsUnitedMarketPlace() const;

  public:
    virtual bool CreateObject(OBJECT *o);
    virtual bool MoveObject(OBJECT *o);
    virtual bool AdvanceObjectVisual(OBJECT *o, BMD *b, float = 0.f);
    virtual bool RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);
    virtual void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);
    bool RenderMonster(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);

  public:
    virtual bool MoveMonsterVisual(CHARACTER *, OBJECT *o, BMD *b,
                                   WorldCharacterVisualState &visual);
    virtual bool AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                      WorldCharacterVisualState &visual);
    virtual bool SetCurrentActionMonster(CHARACTER *c, OBJECT *o);

  public:
    virtual bool PlayMonsterSound(OBJECT *o);

  public:
    void Init();
    void Destroy();

  public:
    bool CreateRain(PARTICLE *o);
    bool MoveRain(PARTICLE *o);

  protected:
    explicit GMUnitedMarketPlace(SessionKeeper &keeper);
    CMapManager &gMapManager;
};

class GMUnknownWorld final : public BaseMap, protected SessionLegacyCalls
{
  public:
    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    explicit GMUnknownWorld(SessionKeeper &keeper) noexcept;
    bool MoveObject(OBJECT *o) override;
};

class CDirection;
class CGMGmArea;

SmartPointer(GMKanturu1st);

class GMKanturu1st final : public BaseMap, protected SessionLegacyCalls
{
  public:
    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    void InstallBehavior() override;

    bool AttackEffectBeforeShared(CHARACTER *character, OBJECT *object, BMD *model) override;

    bool SetCurrentActionMonster(CHARACTER *character, OBJECT *object) override;

    void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *model,
                               bool extraMonster = false) override;

    bool RenderObjectVisual(const ObjectDrawInput &input, BMD *model) override;

    bool RenderObjectMesh(const ObjectDrawInput &input, BMD *model,
                          bool extraMonster = false) override;

    bool CreateObject(OBJECT *object) override;
    bool MoveObject(OBJECT *object) override;
    bool AdvanceObjectVisual(OBJECT *object, BMD *model, float = 0.f) override;

    bool RenderKanturu1stMonsterVisual(const CHARACTER *c, const ObjectDrawInput &input, BMD *b);
    static GMKanturu1stPtr Make(SessionKeeper &keeper);
    ~GMKanturu1st() override = default;

    bool IsKanturu1st();
    bool CreateKanturu1stObject(OBJECT *object);
    bool MoveKanturu1stObject(OBJECT *object);
    bool RenderKanturu1stObjectVisual(const ObjectDrawInput &input, BMD *model);
    bool AdvanceKanturu1stObjectVisual(OBJECT *object, BMD *model);
    bool RenderKanturu1stObjectMesh(const ObjectDrawInput &input, BMD *model,
                                    bool extraMonster = false);
    void RenderKanturu1stAfterObjectMesh(const ObjectDrawInput &input, BMD *model);
    CHARACTER *CreateKanturu1stMonster(int type, int positionX, int positionY, int key);
    bool SetCurrentActionKanturu1stMonster(CHARACTER *character, OBJECT *object);
    bool AttackEffectKanturu1stMonster(CHARACTER *character, OBJECT *object, BMD *model);
    bool MoveKanturu1stMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                                     WorldCharacterVisualState &visual);
    bool RenderKanturu1stMonsterObjectMesh(const ObjectDrawInput &input, BMD *model,
                                           int extraMonster);
    void EmitMonsterActionSounds(OBJECT &object);
    bool AdvanceKanturu1stMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                                        WorldCharacterVisualState &visual);
    void MoveKanturu1stBlurEffect(CHARACTER *character, OBJECT *object, BMD *model);

  private:
    explicit GMKanturu1st(SessionKeeper &keeper) noexcept;

    CMapManager &gMapManager;
    CDirection &g_Direction;
    CGMGmArea &gmArea_;
};

class CDirection;

SmartPointer(GMKanturu2nd);

class CTrapCanon final : protected SessionLegacyCalls
{
  public:
    explicit CTrapCanon(SessionKeeper &keeper) noexcept : SessionLegacyCalls(keeper)
    {
    }

    CHARACTER *Create_TrapCanon(int positionX, int positionY, int key);
    void EmitAttackEffect(CHARACTER *character, OBJECT *object, BMD *model);
};

class GMKanturu2nd final : public BaseMap, protected SessionLegacyCalls
{
  public:
    MapObjectInteraction ObjectInteraction(int type, CHARACTER &actor) override;
    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    void InstallBehavior() override;

    bool SetCurrentActionMonster(CHARACTER *character, OBJECT *object) override;

    void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *model,
                               bool extraMonster = false) override;

    bool RenderObjectMesh(const ObjectDrawInput &input, BMD *model,
                          bool extraMonster = false) override;

    bool CreateObject(OBJECT *object) override;
    bool MoveObject(OBJECT *object) override;
    bool AdvanceObjectVisual(OBJECT *object, BMD *model, float = 0.f) override;

    void AdvanceMonsterState(CHARACTER &character);
    bool Render_Kanturu2nd_MonsterVisual(const CHARACTER *c, const ObjectDrawInput &input, BMD *b);
    static GMKanturu2ndPtr Make(SessionKeeper &keeper);
    ~GMKanturu2nd() override = default;

    bool Create_Kanturu2nd_Object(OBJECT *object);
    CHARACTER *Create_Kanturu2nd_Monster(int type, int positionX, int positionY, int key);
    bool Set_CurrentAction_Kanturu2nd_Monster(CHARACTER *character, OBJECT *object);
    bool AttackEffect_Kanturu2nd_Monster(CHARACTER *character, OBJECT *object, BMD *model);
    void Sound_Kanturu2nd_Object(OBJECT *object);
    bool Move_Kanturu2nd_Object(OBJECT *object);
    void EmitMonsterEvents(OBJECT &object, BMD &model, WorldCharacterVisualState &visual);
    bool Move_Kanturu2nd_MonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                                       WorldCharacterVisualState &visual);
    bool Advance_Kanturu2nd_ObjectVisual(OBJECT *object, BMD *model);
    bool Render_Kanturu2nd_ObjectMesh(const ObjectDrawInput &input, BMD *model,
                                      bool extraMonster = false);
    void Render_Kanturu2nd_AfterObjectMesh(const ObjectDrawInput &input, BMD *model);
    bool Render_Kanturu2nd_MonsterObjectMesh(const ObjectDrawInput &input, BMD *model,
                                             int extraMonster);
    bool Advance_Kanturu2nd_MonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                                          WorldCharacterVisualState &visual);
    bool Is_Kanturu2nd();
    bool Is_Kanturu2nd_3rd();
    void Move_Kanturu2nd_BlurEffect(CHARACTER *character, OBJECT *object, BMD *model);
    void PlayBGM();

  private:
    explicit GMKanturu2nd(SessionKeeper &keeper) noexcept;

    void RenderTrapCanonObject(const ObjectDrawInput &input, BMD *model);
    void AdvanceTrapCanonObjectVisual(CHARACTER *character, OBJECT *object, BMD *model);

    CTrapCanon trapCanon_;
    CMapManager &gMapManager;
    CDirection &g_Direction;
};

class CDirection;

SmartPointer(GMKanturu3rd);

class GMKanturu3rd final : public BaseMap, protected SessionLegacyCalls
{
  public:
    void RenderMapInterface() override;
    float ItemDrawHeight(const OBJECT &object, int index) override;

    bool SetMonsterDeathAction(CHARACTER *character, OBJECT *object) override;

    bool TerrainCutscene() const override;
    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    void InstallBehavior() override;

    bool RenderMonsterVisual(const CHARACTER *character, const ObjectDrawInput &input,
                             BMD *model) override;
    void AdvanceEnvironment() override;

    void RenderEarlyAfterCharacterObjects(OBJECT *head) override;

    std::optional<bool> ObjectVisibility(const OBJECT &object, bool blockVisible) override;

    bool AdvanceMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                              WorldCharacterVisualState &visual) override;

    bool MoveMonsterVisual(CHARACTER *, OBJECT *object, BMD *model,
                           WorldCharacterVisualState &visual) override;
    void MoveBlurEffect(CHARACTER *character, OBJECT *object, BMD *model) override;
    bool AttackEffectMonster(CHARACTER *character, OBJECT *object, BMD *model) override;

    bool SetCurrentActionMonster(CHARACTER *character, OBJECT *object) override;

    void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *model,
                               bool extraMonster = false) override;

    bool RenderObjectMesh(const ObjectDrawInput &input, BMD *model,
                          bool extraMonster = false) override;

    bool CreateObject(OBJECT *object) override;
    bool MoveObject(OBJECT *object) override;
    bool AdvanceObjectVisual(OBJECT *object, BMD *model, float = 0.f) override;

    void AdvanceMonsterState(CHARACTER &character, BMD &model) override;
    static GMKanturu3rdPtr Make(SessionKeeper &keeper);
    ~GMKanturu3rd() override = default;

    bool IsInKanturu3rd();
    void Kanturu3rdInit();
    bool IsSuccessBattle();
    void CheckSuccessBattle(BYTE state, BYTE detailState);
    bool CreateKanturu3rdObject(OBJECT *object);
    bool MoveKanturu3rdObject(OBJECT *object);
    void AdvanceMayaLighting(OBJECT &object);
    void AdvanceResultPresentation();
    void AdvanceBattleRing(OBJECT &object);
    bool AdvanceKanturu3rdObjectVisual(OBJECT *object, BMD *model);
    bool RenderKanturu3rdObjectMesh(const ObjectDrawInput &input, BMD *model,
                                    bool extraMonster = false);
    void RenderKanturu3rdAfterObjectMesh(const ObjectDrawInput &input, BMD *model,
                                         bool extraMonster = false);
    CHARACTER *CreateKanturu3rdMonster(int type, int positionX, int positionY, int key);
    void EmitNightmareEvents(OBJECT &object, BMD &model);
    bool MoveKanturu3rdMonsterVisual(OBJECT *object, BMD *model, WorldCharacterVisualState &visual);
    void MoveKanturu3rdBlurEffect(CHARACTER *character, OBJECT *object, BMD *model);
    bool AdvanceKanturu3rdMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                                        WorldCharacterVisualState &visual);
    bool RenderKanturu3rdMonsterObjectMesh(const ObjectDrawInput &input, BMD *model,
                                           bool extraMonster);
    bool AttackEffectKanturu3rdMonster(CHARACTER *character, OBJECT *object, BMD *model);
    bool SetCurrentActionKanturu3rdMonster(CHARACTER *character, OBJECT *object);
    void MayaSceneMayaAction(BYTE skill);
    void MayaAction(OBJECT *object, BMD *model);
    void Kanturu3rdState(BYTE state, BYTE detailState);
    void Kanturu3rdResult(BYTE result);
    void Kanturu3rdUserandMonsterCount(int monsterCount, int userCount);
    void RenderKanturu3rdinterface();
    void RenderKanturu3rdResultInterface();
    void Kanturu3rdSuccess();
    void Kanturu3rdFailed();
    void ChangeBackGroundMusic(int state);

  private:
    double resultDisplayMilliseconds_ = 0.0;
    explicit GMKanturu3rd(SessionKeeper &keeper) noexcept;

    CMapManager &gMapManager;
    CDirection &g_Direction;
    bool &KanturuSuccessMap;
    bool &KanturuSuccessMapBackup;
    int &iMayaAction;
    bool &bMayaSkill2;
    int &iMayaSkill2_Counter;
    int &iMayaDie_Counter;
    int &iKanturuResult;
    float &fAlpha;
    int &UserCount;
    int &MonsterCount;
};

class SessionRandom;
class CameraState;

class BMD;
class OBJECT;
class CHARACTER;
namespace PkFieldDetail
{
struct MonsterDefinition;
}

class CGM_PK_Field;
using CGM_PK_FieldPtr = std::shared_ptr<CGM_PK_Field>;

class CGM_PK_Field final : public BaseMap, protected SessionLegacyCalls
{
  public:
    bool CreateWeather(PARTICLE *particle, int index) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    void RenderEarlyAfterCharacterObjects(OBJECT *head) override;

    bool IsEarlyAfterCharacterObject(const OBJECT &object) const override;

    std::optional<bool> ObjectVisibility(const OBJECT &object, bool blockVisible) override;

    void AdvanceMonsterState(CHARACTER &character, BMD &model) override;
    static CGM_PK_FieldPtr Make(SessionKeeper &keeper);
    ~CGM_PK_Field() override;

    bool CreateObject(OBJECT *object) override;
    bool MoveObject(OBJECT *object) override;
    bool AdvanceObjectVisual(OBJECT *object, BMD *bmd, float = 0.f) override;
    bool RenderObjectMesh(const ObjectDrawInput &input, BMD *bmd, bool extraMon = false) override;
    void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *bmd,
                               bool extraMon = false) override;

    CHARACTER *CreateMonster(int type, int positionX, int positionY, int key) override;
    const PkFieldDetail::MonsterDefinition *FindMonsterDefinition(int monsterType);
    bool MoveMonsterVisual(CHARACTER *, OBJECT *object, BMD *bmd,
                           WorldCharacterVisualState &visual) override;
    void MoveBlurEffect(CHARACTER *character, OBJECT *object, BMD *bmd) override;
    bool AdvanceMonsterVisual(CHARACTER *character, OBJECT *object, BMD *bmd,
                              WorldCharacterVisualState &visual) override;

    bool PlayMonsterSound(OBJECT *object) override;

    void PlayBGM();
    bool CreateFireSpark(PARTICLE *particle);

    void Init();
    void Destroy();

  private:
    explicit CGM_PK_Field(SessionKeeper &keeper);
    SessionRandom &Random;
    CMapManager &gMapManager;
    CameraState &g_Camera;

    bool RenderMonster(const ObjectDrawInput &input, BMD *bmd, bool extraMon);
    void AdvanceLavaFootsteps(OBJECT &object, BMD &model);
};

bool IsPKField();

class BMD;

namespace SEASON4A
{
SmartPointer(CGM_Raklion);
class CGM_Raklion : public BaseMap, protected SessionLegacyCalls
{
  public:
    void RenderAtmosphere() override;
    ESound WalkingSound(int tile, bool safe) const override;

    bool CreateWeather(PARTICLE *particle, int index) override;

    bool AllowsMusic(const char *track) const override;

    void UpdateMusic() override;

    bool AllowsAmbientSound(ESound sound) const override;

    void PlayAmbientSounds() override;

    void InstallBehavior() override;

    void RenderEarlyAfterCharacterObjects(OBJECT *head) override;

    bool IsEarlyAfterCharacterObject(const OBJECT &object) const override;

    float ObjectAnimationSpeed(const OBJECT &object, const BMD &model, float speed) const override;

    std::optional<bool> ObjectVisibility(const OBJECT &object, bool blockVisible) override;

    void CaptureMonsterAttackState(CHARACTER &character, BMD &model) override;
    enum RAKLION_STATE
    {
        RAKLION_STATE_IDLE = 0,
        RAKLION_STATE_NOTIFY_1 = 1,
        RAKLION_STATE_STANDBY = 2,
        RAKLION_STATE_NOTIFY_2 = 3,
        RAKLION_STATE_READY = 4,
        RAKLION_STATE_START_BATTLE = 5,
        RAKLION_STATE_NOTIFY_3 = 6,
        RAKLION_STATE_CLOSE_DOOR = 7,
        RAKLION_STATE_ALL_USER_DIE = 8,
        RAKLION_STATE_NOTIFY_4 = 9,
        RAKLION_STATE_END = 10,
        RAKLION_STATE_DETAIL_STATE = 11,
        RAKLION_STATE_MAX = 12,
    };

    enum RAKLION_BATTLE_OF_SELUPAN_PATTERN
    {
        BATTLE_OF_SELUPAN_NONE = 0,
        BATTLE_OF_SELUPAN_STANDBY = 1,
        BATTLE_OF_SELUPAN_PATTERN_1 = 2,
        BATTLE_OF_SELUPAN_PATTERN_2 = 3,
        BATTLE_OF_SELUPAN_PATTERN_3 = 4,
        BATTLE_OF_SELUPAN_PATTERN_4 = 5,
        BATTLE_OF_SELUPAN_PATTERN_5 = 6,
        BATTLE_OF_SELUPAN_PATTERN_6 = 7,
        BATTLE_OF_SELUPAN_PATTERN_7 = 8,
        BATTLE_OF_SELUPAN_DIE = 9,
        BATTLE_OF_SELUPAN_MAX = 10,
    };

  public:
    static CGM_RaklionPtr Make(SessionKeeper &keeper);
    virtual ~CGM_Raklion();

    bool IsIceCity();

  public: // Object
    virtual bool CreateObject(OBJECT *o);
    bool MoveObject(OBJECT *o, CTimer2::StartTickTime &timer2StartTickTime) override;
    virtual bool AdvanceObjectVisual(OBJECT *o, BMD *b, float = 0.f);
    virtual bool RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);
    virtual void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);

  public: // Character
    virtual CHARACTER *CreateMonster(int iType, int PosX, int PosY, int Key);
    virtual bool MoveMonsterVisual(CHARACTER *, OBJECT *o, BMD *b,
                                   WorldCharacterVisualState &visual);
    virtual void MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b);
    void EmitMonsterEvents(OBJECT &object, BMD &model, WorldCharacterVisualState &visual);
    void EmitKnightAttack(CHARACTER &character, BMD &model);
    void EmitSelupanEvent(OBJECT &object, std::size_t event, float fraction);
    void AdvanceEggSmoke(OBJECT *object, BMD *model);
    virtual bool AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                      WorldCharacterVisualState &visual);
    virtual bool AttackEffectMonster(CHARACTER *c, OBJECT *o, BMD *b);
    virtual bool SetCurrentActionMonster(CHARACTER *c, OBJECT *o);

  public: // Sound
    virtual bool PlayMonsterSound(OBJECT *o);
    void PlayBGM();

  public:
    void Init();
    void Destroy();

  private:
    explicit CGM_Raklion(SessionKeeper &keeper);
    bool RenderMonster(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);
    void SetBossMonsterAction(CHARACTER *c, OBJECT *o);

  public:
    bool CreateSnow(PARTICLE *o);
    void RenderBaseSmoke();
    void SetState(BYTE byState, BYTE byDetailState);
    bool CanGoBossMap();
    void SetCanGoBossMap();
    void SetEffect();
    void MoveEffect(CTimer2::StartTickTime &timer2StartTickTime);
    void CreateMapEffect();

  private:
    CMapManager &gMapManager;
    CTimer2 m_Timer;
    BYTE m_byState;
    BYTE m_byDetailState;
    bool m_bCanGoBossMap;
    bool m_bVisualEffect;
    bool m_bMusicBossMap;
    bool m_bBossHeightMove;
};
} // namespace SEASON4A

class WorldReadView;
namespace GameLogic::Interaction
{
LogicPickResult PickTerrain(const WorldReadView &world, const float *start,
                            const float *target) noexcept;
}

class CHARACTER;

class CPortalMgr
{
  public:
    explicit CPortalMgr(SessionKeeper &keeper);
    virtual ~CPortalMgr();

    void Reset();
    void ResetPortalPosition();
    void ResetRevivePosition();

    void SavePortalPosition();
    void SaveRevivePosition();

    BOOL IsPortalPositionSaved();
    BOOL IsRevivePositionSaved();

    void GetPortalPositionText(wchar_t *pszOut);
    void GetRevivePositionText(wchar_t *pszOut);

    BOOL IsPortalUsable();

  protected:
    CMapManager &gMapManager;
    CHARACTER *&Hero;
    int m_iPortalWorld;
    int m_iPortalPosition_x;
    int m_iPortalPosition_y;

    int m_iReviveWorld;
    int m_iRevivePosition_x;
    int m_iRevivePosition_y;
};

struct MapPresentationPolicy;
struct MapCharacterPolicy;
struct MapTerrainPolicy;

class GMChaosCastle;
class GMLorencia;
class GMAtlans;
class GMTarkan;
class GMDevilSquare;
class CGMAida;
class CGMBattleCastle;
class CGMDoppelGanger1;
class CGMDoppelGanger2;
class CGMDoppelGanger3;
class CGMDoppelGanger4;
class CGMDuelArena;
class CGMHellas;
class CGMGmArea;
class CGMHuntingGround;
class CGMCrywolf1st;
class CGMCryingWolf2nd;
class CGMKarutan1;
class CGM_PK_Field;
class CGMSantaTown;
class GMEmpireGuardian1;
class GMEmpireGuardian2;
class GMEmpireGuardian3;
namespace SEASON3A
{
class CursedTemple;
class CGM3rdChangeUp;
} // namespace SEASON3A
class GMEmpireGuardian4;
class GMUnitedMarketPlace;
class GMKanturu1st;
class GMKanturu2nd;
class GMKanturu3rd;
class SessionGameplayUnit;
class MapProcessTestPeer;
namespace SEASON3B
{
class GMNewTown;
}
namespace SEASON3C
{
class GMSwampOfQuiet;
}
namespace SEASON4A
{
class CGM_Raklion;
}

SmartPointer(MapProcess);
class MapProcess : protected SessionLegacyCalls
{
  public:
    static MapProcessPtr Make(SessionKeeper &keeper);
    virtual ~MapProcess();

  public:
    void InstallBehavior();

  public:
    bool ObjectVisible(const OBJECT &object, bool blockVisible);
    bool ObjectEffectsVisible(const OBJECT &object);
    void AdvanceObjectVisibility(OBJECT &object);
    void AdvanceObjectFade(OBJECT &object);
    void RenderEarlyAfterCharacterObjects(OBJECT *head);
    bool IsEarlyAfterCharacterObject(const OBJECT &object) const;
    void AdvanceEnvironment();
    MapObjectInteraction ObjectInteraction(int type, CHARACTER &actor);
    void RenderAtmosphere();
    void RenderMapInterface();
    void BeginSceneRender();
    void EndSceneRender();
    bool ActionObject(OBJECT *object);
    void FinishObjectAction();
    void PrepareObjectEffects(int &count, int previousVisible);
    void MoveObjectEffects(OBJECT *object, int &count, int &visible);
    void PrepareObjectLight(const ObjectDrawInput &object, BMD &model);
    ESound WalkingSound(int tile, bool safe) const;
    int PlayerNpcText(bool actionChanged);
    bool CreateObject(OBJECT *o);
    void PrepareObjectUpdate(OBJECT *o);
    float ObjectAnimationSpeed(const OBJECT *object, const BMD &model, float speed) const;
    bool MoveObject(OBJECT *o);
    bool MoveObject(OBJECT *o, CTimer2::StartTickTime &timer2StartTickTime);
    bool RenderObjectVisual(const ObjectDrawInput &input, BMD *model);
    bool AdvanceObjectVisual(OBJECT *o, BMD *b, float luminosity);
    bool RenderWholeObject(const ObjectDrawInput &input, BMD *model, bool extraMonster);
    bool RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);
    void RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon = 0);
    void RenderFrontSideVisual();

  public:
    bool CanObserveCharacter(const CHARACTER &character);
    void AdvancePlayerVisual(CHARACTER *, OBJECT *);
    void BeginCharacterTick();
    void ObserveCharacterTick(const CHARACTER &character);
    void FinishCharacterTick();
    void AdvanceMonsterState(CHARACTER &character, BMD &model);
    void CaptureMonsterAttackState(CHARACTER &character, BMD &model);
    bool ConfigureMonsterLinks(CHARACTER *character, int type);
    bool PushCharacter(CHARACTER *, OBJECT *, float);
    bool AdvanceCharacterDeath(CHARACTER *, OBJECT *);
    void AdvanceCharacterStopTime();
    bool StopMonster(CHARACTER *character, OBJECT *object);
    void MoveCharacterState(CHARACTER *character, OBJECT *object);
    bool SetMonsterDeathAction(CHARACTER *character, OBJECT *object);
    bool PlayMonsterDeathSound(OBJECT *object);
    float MonsterDeathRotationRate(const OBJECT &object);
    float ItemDrawHeight(const OBJECT &object, int index);
    CHARACTER *CreateMonster(int iType, int PosX, int PosY, int Key);
    bool MoveMonsterVisual(CHARACTER *, OBJECT *o, BMD *b, WorldCharacterVisualState &visual);
    void MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b);
    bool RenderMonsterVisual(const CHARACTER *, const ObjectDrawInput &input, BMD *);
    bool AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b, WorldCharacterVisualState &visual);
    bool AttackEffectMonster(CHARACTER *c, OBJECT *o, BMD *b);
    bool SetCurrentActionMonster(CHARACTER *c, OBJECT *o);

  public:
    const MapPresentationPolicy &Presentation() const noexcept;
    const MapCharacterPolicy &CharacterPolicy() const noexcept;
    const MapTerrainPolicy &TerrainPolicy() const noexcept;
    void AdvanceTerrainEffects();
    bool TerrainCutscene() const;
    bool TerrainIsAirborne() const;
    bool MountsUseFlyingActions() const;
    bool GroundShadowsVisible() const;
    bool PrepareAmbientBoidSlot(int index, bool &allowCreate);
    bool CreateAmbientBoid(OBJECT *object, int slot, int terrainIndex);
    bool CreateAmbientFish(OBJECT *object, int terrainIndex);
    void MoveAmbientFishTerrain(OBJECT *object, int terrainIndex);
    bool WeatherEnabled() const;
    bool WeatherSpritePass() const;
    bool WeatherWaterPass() const;
    int PrepareWeather();
    bool CreateWeather(PARTICLE *particle, int index);
    bool MoveWeather(PARTICLE *particle);
    void UpdateWorldAudio();
    bool PlayMonsterSound(OBJECT *o);

  public:
    bool ReceiveMapMessage(BYTE code, BYTE subcode, BYTE *ReceiveBuffer);

  public:
    void Register(Smart_Ptr(BaseMap) pMap);
    void Register(BaseMap &map);

  public:
    BaseMap &GetMap(int type);
    template <typename T> T &TheWorld(int type);
    GMLorencia &Lorencia() noexcept;
    GMAtlans &Atlans() noexcept;
    GMTarkan &Tarkan() noexcept;
    GMDevilSquare &DevilSquare() noexcept;
    GMChaosCastle &ChaosCastle() noexcept;
    CGMAida &Aida() noexcept;
    CGMBattleCastle &BattleCastle() noexcept;
    CGMGmArea &GmArea() noexcept;
    CGMHuntingGround &HuntingGround() noexcept;
    CGMCrywolf1st &Crywolf1st() noexcept;
    CGMCryingWolf2nd &CryingWolf2nd() noexcept;
    CGMHellas &Hellas() noexcept;
    GMKanturu1st &Kanturu1st() noexcept;
    GMKanturu2nd &Kanturu2nd() noexcept;
    GMKanturu3rd &Kanturu3rd() noexcept;
    CGM_PK_Field &PKField() noexcept;
    SEASON4A::CGM_Raklion &Raklion() noexcept;
    CGMSantaTown &SantaTown() noexcept;
    CGMDuelArena &DuelArena() noexcept;
    CGMDoppelGanger1 &DoppelGanger1() noexcept;
    CGMDoppelGanger2 &DoppelGanger2() noexcept;
    CGMDoppelGanger3 &DoppelGanger3() noexcept;
    CGMDoppelGanger4 &DoppelGanger4() noexcept;
    GMEmpireGuardian4 &EmpireGuardian4() noexcept;
    SEASON3B::GMNewTown &NewTown() noexcept;
    SEASON3C::GMSwampOfQuiet &SwampOfQuiet() noexcept;
    GMUnitedMarketPlace &UnitedMarketPlace() noexcept;
#ifdef ASG_ADD_MAP_KARUTAN
    CGMKarutan1 &Karutan() noexcept;
#endif

  private:
    friend class SessionGameplayUnit;
    friend class World;
    friend class MapProcessTestPeer;

    bool FindMap(ENUM_WORLD type);
    BaseMap &FindBaseMap(ENUM_WORLD type);
    BaseMap *MapFor(ENUM_WORLD type) const noexcept;
    void BindCurrentMap() noexcept;
    void PlaceAmbientBoid(OBJECT &object);
    void PrepareAudio(BaseMap *map, int rawMap);
    void Initialize();
    void RegisterOrdinaryMaps();
    void Destroy();
    explicit MapProcess(SessionKeeper &keeper) noexcept;

  private:
    using MapList = std::vector<Smart_Ptr(BaseMap)>;

  private:
    CMapManager &gMapManager;
    GMLorencia *lorencia_ = nullptr;
    GMAtlans *atlans_ = nullptr;
    GMTarkan *tarkan_ = nullptr;
    GMDevilSquare *devilSquare_ = nullptr;
    GMChaosCastle *chaosCastle_ = nullptr;
    CGMAida *aida_ = nullptr;
    CGMBattleCastle *battleCastle_ = nullptr;
    CGMGmArea *gmArea_ = nullptr;
    CGMHuntingGround *huntingGround_ = nullptr;
    CGMCrywolf1st *crywolf1st_ = nullptr;
    CGMCryingWolf2nd *cryingWolf2nd_ = nullptr;
    CGMHellas *hellas_ = nullptr;
    GMKanturu1st *kanturu1st_ = nullptr;
    GMKanturu2nd *kanturu2nd_ = nullptr;
    GMKanturu3rd *kanturu3rd_ = nullptr;
    CGM_PK_Field *pkField_ = nullptr;
    SEASON4A::CGM_Raklion *raklion_ = nullptr;
    CGMSantaTown *santaTown_ = nullptr;
    CGMDuelArena *duelArena_ = nullptr;
    CGMDoppelGanger1 *doppelGanger1_ = nullptr;
    CGMDoppelGanger2 *doppelGanger2_ = nullptr;
    CGMDoppelGanger3 *doppelGanger3_ = nullptr;
    CGMDoppelGanger4 *doppelGanger4_ = nullptr;
    SEASON3A::CursedTemple *cursedTemple_ = nullptr;
    SEASON3A::CGM3rdChangeUp *thirdChange_ = nullptr;
    GMEmpireGuardian1 *empireGuardian1_ = nullptr;
    GMEmpireGuardian2 *empireGuardian2_ = nullptr;
    GMEmpireGuardian3 *empireGuardian3_ = nullptr;
    GMEmpireGuardian4 *empireGuardian4_ = nullptr;
    SEASON3B::GMNewTown *newTown_ = nullptr;
    SEASON3C::GMSwampOfQuiet *swampOfQuiet_ = nullptr;
    GMUnitedMarketPlace *unitedMarketPlace_ = nullptr;
#ifdef ASG_ADD_MAP_KARUTAN
    CGMKarutan1 *karutan_ = nullptr;
#endif
    MapList m_MapList;
    std::vector<BaseMap *> mapsByWorld_;
    BaseMap *audioMap_ = nullptr;
    int audioMapId_ = -2;
    std::vector<ESound> inactiveAmbient_;
    std::vector<const char *> inactiveMusic_;
    BaseMap *currentMap_ = nullptr;
    BaseMap *previewMap_ = nullptr;
    BaseMap *ContextBehavior() const noexcept
    {
        return gMapManager.IsPreviewContext() ? previewMap_ : currentMap_;
    }
};

template <typename T> T &MapProcess::TheWorld(int type)
{
    return dynamic_cast<T &>(TheMapProcess().GetMap(type));
}

template <typename T> T &SessionLegacyCalls::TheWorld(int type)
{
    return TheMapProcess().TheWorld<T>(type);
}

#ifdef ASG_ADD_MAP_KARUTAN
#define g_Karutan1 TheWorld<CGMKarutan1>(WD_80KARUTAN1)
#endif // ASG_ADD_MAP_KARUTAN

namespace RaklionDetail
{
using namespace SEASON4A;
float EggDeathBrightness(float animationFrame);
} // namespace RaklionDetail

namespace BattleCastleDetail
{
#pragma pack(push)
#pragma pack()
enum
{
    GUARD_STOP = 0,
    GUARD_READY,
    GUARD_ATTACK_READY,
    GUARD_ATTACK
};
#pragma pack(pop)

} // namespace BattleCastleDetail

namespace HellasDetail
{

#pragma pack(push)
#pragma pack()
inline const BYTE ACTION_DESTROY_WIZ_DEF = 33;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const BYTE ACTION_DESTROY_DEF = 34;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const int g_iKalimaLevel[14][2] = {
    {40, 999}, {131, 999}, {181, 999}, {231, 999}, {281, 999}, {331, 999}, {350, 999},
    {20, 999}, {111, 999}, {161, 999}, {211, 999}, {261, 999}, {311, 999}, {350, 999}};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const float AmbientSoundInterval = 4000.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const float KundunSoundInterval = 2000.0f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const float BigMonInterval = 4000.0f;
#pragma pack(pop)

} // namespace HellasDetail

namespace ChaosCastleDetail
{

#pragma pack(push)
#pragma pack()
using CastleArea = std::array<int, 4>;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
using CastleAreaSet = std::array<CastleArea, 4>;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr CastleAreaSet gChaosCastleLimitArea1{{
    CastleArea{23, 75, 44, 76},
    CastleArea{43, 77, 44, 108},
    CastleArea{23, 107, 42, 108},
    CastleArea{23, 77, 24, 106},
}};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr CastleAreaSet gChaosCastleLimitArea2{{
    CastleArea{25, 77, 42, 78},
    CastleArea{41, 79, 42, 106},
    CastleArea{25, 105, 40, 106},
    CastleArea{25, 79, 26, 104},
}};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr CastleAreaSet gChaosCastleLimitArea3{{
    CastleArea{27, 79, 40, 80},
    CastleArea{39, 81, 40, 104},
    CastleArea{27, 103, 38, 104},
    CastleArea{27, 81, 28, 102},
}};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr std::array<const CastleAreaSet *, 9> kCastleLimitAreas{{
    &gChaosCastleLimitArea1, // 0
    nullptr,                 // 1
    nullptr,                 // 2
    &gChaosCastleLimitArea2, // 3
    nullptr,                 // 4
    nullptr,                 // 5
    &gChaosCastleLimitArea3, // 6
    nullptr,                 // 7
    nullptr,                 // 8
}};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kActionTriggerTime = 30;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
constexpr std::uint8_t LevelValue(CastleLevel level)
{
    return static_cast<std::uint8_t>(level);
}
#pragma pack(pop)

} // namespace ChaosCastleDetail

namespace PkFieldDetail
{

#pragma pack(push)
#pragma pack()
struct MonsterDefinition
{
    int monsterType;
    EMonsterModelType monsterModelId;
    int objectModelId;
    float scale;
    bool assignLifetime;
    int lifetime;
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr std::array<MonsterDefinition, 12> kMonsterDefinitions{{
    {MONSTER_ZOMBIE_FIGHTER, EMonsterModelType::MONSTER_MODEL_ZOMBIE_FIGHTER, MODEL_ZOMBIE_FIGHTER,
     1.0f, false, 0},
    {MONSTER_ZOMBIER, EMonsterModelType::MONSTER_MODEL_ZOMBIE_FIGHTER, MODEL_ZOMBIE_FIGHTER, 1.0f,
     false, 0},
    {MONSTER_GLADIATOR, EMonsterModelType::MONSTER_MODEL_GLADIATOR, MODEL_GLADIATOR, 1.0f, false,
     0},
    {MONSTER_HELL_GLADIATOR, EMonsterModelType::MONSTER_MODEL_GLADIATOR, MODEL_GLADIATOR, 1.0f,
     false, 0},
    {MONSTER_SLAUGHTERER, EMonsterModelType::MONSTER_MODEL_SLAUGTHERER, MODEL_SLAUGHTERER, 0.7f,
     false, 0},
    {MONSTER_ASH_SLAUGHTERER, EMonsterModelType::MONSTER_MODEL_SLAUGTHERER, MODEL_SLAUGHTERER, 0.7f,
     false, 0},
    {MONSTER_BLOOD_ASSASSIN, EMonsterModelType::MONSTER_MODEL_BLOOD_ASSASSIN, MODEL_BLOOD_ASSASSIN,
     1.0f, true, 100},
    {MONSTER_CRUEL_BLOOD_ASSASSIN, EMonsterModelType::MONSTER_MODEL_CRUEL_BLOOD_ASSASSIN,
     MODEL_CRUEL_BLOOD_ASSASSIN, 1.0f, true, 100},
    {MONSTER_COLD_BLOODED_ASSASSIN, EMonsterModelType::MONSTER_MODEL_CRUEL_BLOOD_ASSASSIN,
     MODEL_CRUEL_BLOOD_ASSASSIN, 1.0f, true, 100},
    {MONSTER_BURNING_LAVA_GIANT, EMonsterModelType::MONSTER_MODEL_BURNING_LAVA_GIANT,
     MODEL_BURNING_LAVA_GIANT, 1.0f, false, 0},
    {MONSTER_LAVA_GIANT, EMonsterModelType::MONSTER_MODEL_LAVA_GIANT, MODEL_LAVA_GIANT, 1.0f, false,
     0},
    {MONSTER_RUTHLESS_LAVA_GIANT, EMonsterModelType::MONSTER_MODEL_LAVA_GIANT, MODEL_LAVA_GIANT,
     1.0f, false, 0},
}};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kBlurSampleCount = 5;
#pragma pack(pop)

} // namespace PkFieldDetail

namespace ChaosCastleDetail
{

const ChaosCastleDetail::CastleAreaSet *SelectLimitArea(CastleLevel level);
}
