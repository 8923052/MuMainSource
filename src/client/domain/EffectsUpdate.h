#pragma once
#include "support/CoreMath.h"
#include "session/SessionRuntime.h"
#include <cstdint>
#include <optional>
#include <array>
#include <utility>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <numeric>
#include <type_traits>
#include <iterator>

template <class T> class SessionEffectPool;
struct CharacterSocketBinding;
class CHARACTER;
#define MAX_OPERATES 200
#define MAX_MAGIC 64

namespace Core::Time
{
struct BounceResult final
{
    float damping = 1.f;
    float dampedFrames = 0.f;
    int impacts = 0;
    float restingFrames = 0.f;
};
} // namespace Core::Time

// Only a newborn has a shortened first update. Ordinary updates retain the
// session's elapsed duration; pool position does not own the birth time.
struct EffectBirthTiming
{
    std::uint64_t serial = 0;
    float remainingFrames = -1.f;
    float pendingStartFraction = 0.f;
    float activeStartFraction = 0.f;
    float FrameFraction(float local) const;
};

class EffectBirthStep final
{
  public:
    EffectBirthStep(float &frames, EffectBirthTiming &birth);
    ~EffectBirthStep();
    EffectBirthStep(const EffectBirthStep &) = delete;
    EffectBirthStep &operator=(const EffectBirthStep &) = delete;

  private:
    float &frames_;
    float previous_;
    EffectBirthTiming &birth_;
    float previousStart_;
    std::uint64_t serial_;
};

class EffectEmissionScope final
{
  public:
    EffectEmissionScope(std::optional<float> &time, float remainingFrames, float sceneFrames,
                        float intervalTail);
    ~EffectEmissionScope();
    // Local remainder drives an emitter's own age; scene time samples other owners.
    float RemainingFrames() const;
    float SceneRemainingFrames() const;
    float FrameFraction() const;
    EffectEmissionScope(const EffectEmissionScope &) = delete;
    EffectEmissionScope &operator=(const EffectEmissionScope &) = delete;

  private:
    std::optional<float> &time_;
    std::optional<float> previous_;
    float remainingFrames_, sceneFrames_, intervalTail_;
};

// Each iteration installs one birth time, then restores its enclosing emitter.
// Samples include the authored interval's starting point.
class EffectEmissionSequence final
{
  public:
    struct Iterator
    {
        std::optional<float> *time;
        int index, count;
        float frames, remainingAtStart, sceneFrames, intervalTail;
        EffectEmissionScope operator*() const;
        Iterator &operator++();
        bool operator!=(const Iterator &other) const;
    };
    EffectEmissionSequence(std::optional<float> &time, int count, float frames,
                           float remainingAtStart, float sceneFrames, float intervalTail);
    Iterator begin();
    Iterator end();

  private:
    std::optional<float> &time_;
    int count_;
    float frames_, remainingAtStart_, sceneFrames_, intervalTail_;
};

// A subinterval of the current update. Births retain the time left after it.
class EffectUpdateInterval final
{
  public:
    EffectUpdateInterval(float &frames, float &intervalTail, std::optional<float> &emissionTime,
                         float duration, float offset);
    ~EffectUpdateInterval();
    EffectUpdateInterval(const EffectUpdateInterval &) = delete;
    EffectUpdateInterval &operator=(const EffectUpdateInterval &) = delete;

  private:
    float &frames_;
    float &intervalTail_;
    std::optional<float> &emissionTime_;
    float previousFrames_, previousTail_;
    std::optional<float> previousEmissionTime_;
};

class OBJECT;

namespace GameLogic::Effects::Behaviors
{
class MoveBehavior;
}

// Data-driven description of a single effect type.
// Historically every effect was hand-coded as a `case` in three giant switch
// statements (CreateEffect / MoveEffect / RenderEffects in ZzzEffect.cpp). The
// vast majority of those cases only assigned a handful of scalar fields at
// creation and rendered with a plain RenderObject(). EffectDescriptor captures
// that as data: the parameters live in a table (see EffectRegistry), and only
// effects with genuine per-frame behaviour carry a handler function.
namespace GameLogic::Effects
{
inline constexpr float MoonHarvestLifetime = 70.f;
// Creation parameters applied on top of the common initialisation that
// CreateEffect performs for every effect. Every field is optional: an unset
// field means "keep whatever the common initialisation chose", so a table
// row only states what actually differs from the default for that effect.
// These cover effects whose creation is plain data. Randomised creation in
// this codebase is almost always fused with angle/direction/matrix setup
// (e.g. a stone that picks a random spin, then rotates its launch vector by
// that angle), which isn't expressible as independent scalar parameters --
// those effects use an onCreate hook instead (see EffectDescriptor).
struct CreateParams
{
    std::optional<float> lifeTime;
    std::optional<float> scale;
    std::optional<float> velocity;
    std::optional<float> gravity;
    std::optional<int> hiddenMesh;
    std::optional<int> blendMesh;
    std::optional<float> blendMeshLight;
    std::optional<float> alpha;

    // When set, overrides o->Light (the colour the effect renders with).
    std::optional<std::array<float, 3>> light;

    // Many legacy cases finish with `VectorCopy(o->Light, o->Direction)`,
    // stashing the colour so MoveEffect can fade it back in. Opt in here.
    bool copyLightToDirection = false;
};

// Spawns sub-effects / joints or runs other one-shot setup that can't be
// expressed as plain parameters. Runs once, right after CreateParams are
// applied.
using CreateHook = void (Behaviors::MoveBehavior::*)(OBJECT *o);

// Per-frame update. `luminosity` is the per-frame flicker value MoveEffect
// computes once for every effect (so handlers don't draw an extra rand()).
// Returns true to run MoveEffect's shared tail (lifetime decrement, particle
// trail, destruction); false to skip it, mirroring the handful of legacy
// cases that `return` early out of the move switch.
using MoveHandler = bool (Behaviors::MoveBehavior::*)(OBJECT *o, int index, float luminosity);

// Creation and movement migrate independently. An unset handler retains
// the corresponding CPU switch; drawing has no lifecycle callback.
struct EffectDescriptor
{
    std::optional<CreateParams> create;
    CreateHook onCreate = nullptr;
    MoveHandler move = nullptr;
};

// Applies the optional parameters to an already common-initialised effect.
void ApplyCreateParams(OBJECT *o, const CreateParams &params);
} // namespace GameLogic::Effects

// Per-frame behaviour handlers extracted from the MoveEffect / RenderEffects
// switches in ZzzEffect.cpp. Each handler owns the logic for one effect type and
// is wired into the registry (see EffectRegistry.cpp). Move handlers return true
// to run MoveEffect's shared tail (lifetime decrement / particle trail /
// destruction) and false to skip it, matching the original cases.

class BMD;
class CMapManager;
class SessionKeeper;

// Move handlers extracted from MoveEffect. False skips the shared tail; true retains
// animation/lifetime processing. Integrated motion owners are excluded from the
// generic movement tail in ModelEffectAdvance.cpp. See EffectRegistry.cpp for dispatch.
namespace GameLogic::Effects::Behaviors
{
class MoveBehavior;

class MoveBehaviorLegacyCalls : protected SessionLegacyCalls
{
  protected:
    MoveBehaviorLegacyCalls(SessionKeeper &keeper, MoveBehavior &owner) noexcept;

    void CreateMayaStone45(OBJECT *o);                                            // OMF-01246
    bool MoveDesair(OBJECT *o, int index, float luminosity);                      // OMF-01247
    bool MoveInfinityArrow4(OBJECT *o, int index, float luminosity);              // OMF-01248
    bool MoveMagicCapsule2(OBJECT *o, int index, float luminosity);               // OMF-01249
    bool MoveSpear(OBJECT *o, int index, float luminosity);                       // OMF-01250
    bool MoveSummonerNeilNife(OBJECT *o, int index, float luminosity);            // OMF-01251
    bool MoveSummonerNeilGround(OBJECT *o, int index, float luminosity);          // OMF-01252
    bool MoveBitmapFire(OBJECT *o, int index, float luminosity);                  // OMF-01253
    bool MoveBitmapFireRed(OBJECT *o, int index, float luminosity);               // OMF-01254
    bool MoveBitmapLightMarks(OBJECT *o, int index, float luminosity);            // OMF-01255
    bool MoveMagic1(OBJECT *o, int index, float luminosity);                      // OMF-01256
    bool MoveMayaStar(OBJECT *o, int index, float luminosity);                    // OMF-01257
    bool Move_MODEL_DRAGON(OBJECT *o, int index, float Luminosity);               // OMF-01258
    bool Move_MODEL_ARROW_AUTOLOAD(OBJECT *o, int index, float Luminosity);       // OMF-01259
    bool Move_MODEL_INFINITY_ARROW(OBJECT *o, int index, float Luminosity);       // OMF-01260
    bool Move_MODEL_INFINITY_ARROW1(OBJECT *o, int index, float Luminosity);      // OMF-01261
    bool Move_MODEL_SHIELD_CRASH(OBJECT *o, int index, float Luminosity);         // OMF-01262
    bool Move_MODEL_SHIELD_CRASH2(OBJECT *o, int index, float Luminosity);        // OMF-01263
    bool Move_MODEL_IRON_RIDER_ARROW(OBJECT *o, int index, float Luminosity);     // OMF-01264
    bool Move_MODEL_MULTI_SHOT3(OBJECT *o, int index, float Luminosity);          // OMF-01265
    bool Move_MODEL_MULTI_SHOT1(OBJECT *o, int index, float Luminosity);          // OMF-01266
    bool Move_MODEL_MULTI_SHOT2(OBJECT *o, int index, float Luminosity);          // OMF-01267
    bool Move_MODEL_BLADE_SKILL(OBJECT *o, int index, float Luminosity);          // OMF-01268
    bool Move_MODEL_KENTAUROS_ARROW(OBJECT *o, int index, float Luminosity);      // OMF-01269
    bool Move_MODEL_WARP3(OBJECT *o, int index, float Luminosity);                // OMF-01270
    bool Move_MODEL_GHOST(OBJECT *o, int index, float Luminosity);                // OMF-01271
    bool Move_MODEL_TREE_ATTACK(OBJECT *o, int index, float Luminosity);          // OMF-01272
    bool Move_MODEL_BUTTERFLY01(OBJECT *o, int index, float Luminosity);          // OMF-01273
    bool Move_BITMAP_SKULL(OBJECT *o, int index, float Luminosity);               // OMF-01274
    bool Move_MODEL__SPEAR(OBJECT *o, int index, float Luminosity);               // OMF-01275
    bool Move_MODEL_HALLOWEEN_CANDY_BLUE(OBJECT *o, int index, float Luminosity); // OMF-01276
    bool Move_MODEL_HALLOWEEN_EX(OBJECT *o, int index, float Luminosity);         // OMF-01277
    bool Move_MODEL_XMAS_EVENT_BOX(OBJECT *o, int index, float Luminosity);       // OMF-01278
    bool Move_MODEL_XMAS_EVENT_ICEHEART(OBJECT *o, int index, float Luminosity);  // OMF-01279
    bool Move_MODEL_NEWYEARSDAY_EVENT_BEKSULKI(OBJECT *o, int index,
                                               float Luminosity);             // OMF-01280
    bool Move_MODEL_MOONHARVEST_MOON(OBJECT *o, int index, float Luminosity); // OMF-01281
    bool Move_MODEL_MOONHARVEST_GAM(OBJECT *o, int index, float Luminosity);  // OMF-01282
    bool Move_MODEL_SPEARSKILL(OBJECT *o, int index, float Luminosity);       // OMF-01283
    bool Move_BITMAP_FIRE_CURSEDLICH(OBJECT *o, int index, float Luminosity); // OMF-01284
    bool Move_MODEL_SUMMONER_WRISTRING_EFFECT(OBJECT *o, int index,
                                              float Luminosity); // OMF-01285
    bool Move_MODEL_SUMMONER_EQUIP_HEAD_SAHAMUTT(OBJECT *o, int index,
                                                 float Luminosity); // OMF-01286
    bool Move_MODEL_SUMMONER_EQUIP_HEAD_NEIL(OBJECT *o, int index,
                                             float Luminosity); // OMF-01287
    bool Move_MODEL_SUMMONER_CASTING_EFFECT1(OBJECT *o, int index,
                                             float Luminosity); // OMF-01288
    bool Move_MODEL_SUMMONER_SUMMON_SAHAMUTT(OBJECT *o, int index,
                                             float Luminosity);                    // OMF-01289
    bool Move_MODEL_SUMMONER_SUMMON_NEIL(OBJECT *o, int index, float Luminosity);  // OMF-01290
    bool Move_MODEL_SUMMONER_SUMMON_LAGUL(OBJECT *o, int index, float Luminosity); // OMF-01291
    bool Move_BITMAP_MAGIC(OBJECT *o, int index, float Luminosity);                // OMF-01292
    bool Move_BITMAP_OUR_INFLUENCE_GROUND(OBJECT *o, int index, float Luminosity); // OMF-01293
    bool Move_BITMAP_MAGIC_ZIN(OBJECT *o, int index, float Luminosity);            // OMF-01294
    bool Move_BITMAP_PIN_LIGHT(OBJECT *o, int index, float Luminosity);            // OMF-01295
    bool Move_BITMAP_ORORA(OBJECT *o, int index, float Luminosity);                // OMF-01296
    bool Move_BITMAP_GATHERING(OBJECT *o, int index, float Luminosity);            // OMF-01297
    bool Move_BITMAP_JOINT_THUNDER(OBJECT *o, int index, float Luminosity);        // OMF-01298
    bool Move_BITMAP_IMPACT(OBJECT *o, int index, float Luminosity);               // OMF-01299
    bool Move_BITMAP_FLAME(OBJECT *o, int index, float Luminosity);                // OMF-01300
    bool Move_MODEL_RAKLION_BOSS_CRACKEFFECT(OBJECT *o, int index,
                                             float Luminosity);                     // OMF-01301
    bool Move_MODEL_RAKLION_BOSS_MAGIC(OBJECT *o, int index, float Luminosity);     // OMF-01302
    bool Move_BITMAP_FIRE_HIK2_MONO(OBJECT *o, int index, float Luminosity);        // OMF-01303
    bool Move_BITMAP_CLOUD(OBJECT *o, int index, float Luminosity);                 // OMF-01304
    bool Move_MODEL_CHAIN_LIGHTNING(OBJECT *o, int index, float Luminosity);        // OMF-01305
    bool Move_MODEL_ALICE_DRAIN_LIFE(OBJECT *o, int index, float Luminosity);       // OMF-01306
    bool Move_MODEL_ALICE_BUFFSKILL_EFFECT(OBJECT *o, int index, float Luminosity); // OMF-01307
    bool Move_MODEL_LIGHTNING_SHOCK(OBJECT *o, int index, float Luminosity);        // OMF-01308
    bool Move_MODEL_SKILL_BLAST(OBJECT *o, int index, float Luminosity);            // OMF-01309
    bool Move_MODEL_WAVE(OBJECT *o, int index, float Luminosity);                   // OMF-01310
    bool Move_MODEL_TAIL(OBJECT *o, int index, float Luminosity);                   // OMF-01311
    bool Move_MODEL_WAVE_FORCE(OBJECT *o, int index, float Luminosity);             // OMF-01312
    bool Move_MODEL_SKILL_INFERNO(OBJECT *o, int index, float Luminosity);          // OMF-01313
    bool Move_MODEL_MAGIC_CIRCLE1(OBJECT *o, int index, float Luminosity);          // OMF-01314
    bool Move_MODEL_PROTECT(OBJECT *o, int index, float Luminosity);                // OMF-01315
    bool Move_MODEL_POISON(OBJECT *o, int index, float Luminosity);                 // OMF-01316
    bool Move_MODEL_SAW(OBJECT *o, int index, float Luminosity);                    // OMF-01317
    bool Move_MODEL_LASER(OBJECT *o, int index, float Luminosity);                  // OMF-01318
    bool Move_MODEL_SKILL_WHEEL1(OBJECT *o, int index, float Luminosity);           // OMF-01319
    bool Move_MODEL_SKILL_WHEEL2(OBJECT *o, int index, float Luminosity);           // OMF-01320
    bool Move_MODEL_SKILL_FISSURE(OBJECT *o, int index, float Luminosity);          // OMF-01321
    bool Move_MODEL_FISSURE(OBJECT *o, int index, float Luminosity);                // OMF-01322
    bool Move_MODEL_SKILL_FURY_STRIKE(OBJECT *o, int index, float Luminosity);      // OMF-01323
    bool Move_MODEL_BALGAS_SKILL(OBJECT *o, int index, float Luminosity);           // OMF-01324
    bool Move_MODEL_CHANGE_UP_EFF(OBJECT *o, int index, float Luminosity);          // OMF-01325
    bool Move_MODEL_CHANGE_UP_NASA(OBJECT *o, int index, float Luminosity);         // OMF-01326
    bool Move_MODEL_CHANGE_UP_CYLINDER(OBJECT *o, int index, float Luminosity);     // OMF-01327
    bool Move_MODEL_DARK_ELF_SKILL(OBJECT *o, int index, float Luminosity);         // OMF-01328
    bool Move_MODEL_MAGIC2(OBJECT *o, int index, float Luminosity);                 // OMF-01329
    bool Move_MODEL_STORM(OBJECT *o, int index, float Luminosity);                  // OMF-01330
    bool Move_MODEL_SUMMON(OBJECT *o, int index, float Luminosity);                 // OMF-01331
    bool Move_MODEL_STORM2(OBJECT *o, int index, float Luminosity);                 // OMF-01332
    bool Move_MODEL_STORM3(OBJECT *o, int index, float Luminosity);                 // OMF-01333
    bool Move_MODEL_MAYASTONE1(OBJECT *o, int index, float Luminosity);             // OMF-01334
    bool Move_MODEL_MAYASTONE4(OBJECT *o, int index, float Luminosity);             // OMF-01335
    bool Move_MODEL_MAYASTONEFIRE(OBJECT *o, int index, float Luminosity);          // OMF-01336
    bool Move_MODEL_MAYAHANDSKILL(OBJECT *o, int index, float Luminosity);          // OMF-01337
    bool Move_MODEL_CIRCLE(OBJECT *o, int index, float Luminosity);                 // OMF-01338
    bool Move_MODEL_CIRCLE_LIGHT(OBJECT *o, int index, float Luminosity);           // OMF-01339
    bool Move_MODEL_ICE_SMALL(OBJECT *o, int index, float Luminosity);              // OMF-01340
    bool Move_MODEL_SKULL(OBJECT *o, int index, float Luminosity);                  // OMF-01341
    bool Move_MODEL_CUNDUN_PART1(OBJECT *o, int index, float Luminosity);           // OMF-01342
    bool Move_MODEL_CURSEDTEMPLE_STATUE_PART1(OBJECT *o, int index,
                                              float Luminosity);                   // OMF-01343
    bool Move_MODEL_XMAS2008_SNOWMAN_HEAD(OBJECT *o, int index, float Luminosity); // OMF-01344
    bool Move_MODEL_XMAS2008_SNOWMAN_BODY(OBJECT *o, int index, float Luminosity); // OMF-01345
    bool Move_MODEL_DOPPELGANGER_SLIME_CHIP(OBJECT *o, int index,
                                            float Luminosity);                     // OMF-01346
    bool Move_MODEL_WATER_WAVE(OBJECT *o, int index, float Luminosity);            // OMF-01347
    bool Move_MODEL_STAFF_OF_DESTRUCTION(OBJECT *o, int index, float Luminosity);  // OMF-01348
    bool Move_MODEL_PIERCING(OBJECT *o, int index, float Luminosity);              // OMF-01349
    bool Move_MODEL_ARROW_BEST_CROSSBOW(OBJECT *o, int index, float Luminosity);   // OMF-01350
    bool Move_MODEL_ARROW_DOUBLE(OBJECT *o, int index, float Luminosity);          // OMF-01351
    bool Move_MODEL_ARROW_HOLY(OBJECT *o, int index, float Luminosity);            // OMF-01352
    bool Move_MODEL_ARROW(OBJECT *o, int index, float Luminosity);                 // OMF-01353
    bool Move_MODEL_ARROW_STEEL(OBJECT *o, int index, float Luminosity);           // OMF-01354
    bool Move_MODEL_DARK_SCREAM_FIRE(OBJECT *o, int index, float Luminosity);      // OMF-01355
    bool Move_MODEL_CURSEDTEMPLE_HOLYITEM(OBJECT *o, int index, float Luminosity); // OMF-01356
    bool Move_MODEL_CURSEDTEMPLE_PRODECTION_SKILL(OBJECT *o, int index,
                                                  float Luminosity); // OMF-01357
    bool Move_MODEL_CURSEDTEMPLE_RESTRAINT_SKILL(OBJECT *o, int index,
                                                 float Luminosity);              // OMF-01358
    bool Move_MODEL_ARROW_SPARK(OBJECT *o, int index, float Luminosity);         // OMF-01359
    bool Move_MODEL_ARROW_RING(OBJECT *o, int index, float Luminosity);          // OMF-01360
    bool Move_MODEL_ARROW_TANKER(OBJECT *o, int index, float Luminosity);        // OMF-01361
    bool Move_MODEL_ARROW_BOMB(OBJECT *o, int index, float Luminosity);          // OMF-01362
    bool Move_MODEL_ARROW_DARKSTINGER(OBJECT *o, int index, float Luminosity);   // OMF-01363
    bool Move_MODEL_DUNGEON_STONE01(OBJECT *o, int index, float Luminosity);     // OMF-01364
    bool Move_MODEL_WARCRAFT(OBJECT *o, int index, float Luminosity);            // OMF-01365
    bool Move_BITMAP_FIRECRACKERRISE(OBJECT *o, int index, float Luminosity);    // OMF-01366
    bool Move_BITMAP_FIRECRACKER(OBJECT *o, int index, float Luminosity);        // OMF-01367
    bool Move_BITMAP_FIRECRACKER0001(OBJECT *o, int index, float Luminosity);    // OMF-01368
    bool Move_BITMAP_FIRECRACKER0002(OBJECT *o, int index, float Luminosity);    // OMF-01369
    bool Move_BITMAP_FIRECRACKER0003(OBJECT *o, int index, float Luminosity);    // OMF-01370
    bool Move_BITMAP_SWORD_FORCE(OBJECT *o, int index, float Luminosity);        // OMF-01371
    bool Move_BITMAP_BLIZZARD(OBJECT *o, int index, float Luminosity);           // OMF-01372
    bool Move_BITMAP_SHOTGUN(OBJECT *o, int index, float Luminosity);            // OMF-01373
    bool Move_MODEL_SHINE(OBJECT *o, int index, float Luminosity);               // OMF-01374
    bool Move_MODEL_BLIZZARD(OBJECT *o, int index, float Luminosity);            // OMF-01375
    bool Move_MODEL_ARROW_DRILL(OBJECT *o, int index, float Luminosity);         // OMF-01376
    bool Move_MODEL_COMBO(OBJECT *o, int index, float Luminosity);               // OMF-01377
    bool Move_MODEL_WAVES(OBJECT *o, int index, float Luminosity);               // OMF-01378
    bool Move_MODEL_AIR_FORCE(OBJECT *o, int index, float Luminosity);           // OMF-01379
    bool Move_MODEL_PIERCING2(OBJECT *o, int index, float Luminosity);           // OMF-01380
    bool Move_MODEL_DEASULER(OBJECT *o, int index, float Luminosity);            // OMF-01381
    bool Move_MODEL_DEATH_SPI_SKILL(OBJECT *o, int index, float Luminosity);     // OMF-01382
    bool Move_MODEL_PIER_PART(OBJECT *o, int index, float Luminosity);           // OMF-01383
    bool Move_BITMAP_FLARE_FORCE(OBJECT *o, int index, float Luminosity);        // OMF-01384
    bool Move_MODEL_DARKLORD_SKILL(OBJECT *o, int index, float Luminosity);      // OMF-01385
    bool Move_MODEL_GROUND_STONE(OBJECT *o, int index, float Luminosity);        // OMF-01386
    bool Move_BITMAP_TWLIGHT(OBJECT *o, int index, float Luminosity);            // OMF-01387
    bool Move_BITMAP_SHOCK_WAVE(OBJECT *o, int index, float Luminosity);         // OMF-01388
    bool Move_BITMAP_DAMAGE_01_MONO(OBJECT *o, int index, float Luminosity);     // OMF-01389
    bool Move_BITMAP_FLARE(OBJECT *o, int index, float Luminosity);              // OMF-01390
    bool Move_MODEL_CUNDUN_DRAGON_HEAD(OBJECT *o, int index, float Luminosity);  // OMF-01391
    bool Move_MODEL_CUNDUN_PHOENIX(OBJECT *o, int index, float Luminosity);      // OMF-01392
    bool Move_MODEL_CUNDUN_GHOST(OBJECT *o, int index, float Luminosity);        // OMF-01393
    bool Move_MODEL_CUNDUN_SKILL(OBJECT *o, int index, float Luminosity);        // OMF-01394
    bool Move_MODEL_BATTLE_GUARD2(OBJECT *o, int index, float Luminosity);       // OMF-01395
    bool Move_MODEL_ARROW_TANKER_HIT(OBJECT *o, int index, float Luminosity);    // OMF-01396
    bool Move_MODEL_FLY_BIG_STONE1(OBJECT *o, int index, float Luminosity);      // OMF-01397
    bool Move_MODEL_FLY_BIG_STONE2(OBJECT *o, int index, float Luminosity);      // OMF-01398
    bool Move_MODEL_BIG_STONE_PART1(OBJECT *o, int index, float Luminosity);     // OMF-01399
    bool Move_MODEL_GATE_PART1(OBJECT *o, int index, float Luminosity);          // OMF-01400
    bool Move_MODEL_AURORA(OBJECT *o, int index, float Luminosity);              // OMF-01401
    bool Move_MODEL_FENRIR_THUNDER(OBJECT *o, int index, float Luminosity);      // OMF-01402
    bool Move_MODEL_FALL_STONE_EFFECT(OBJECT *o, int index, float Luminosity);   // OMF-01403
    bool Move_MODEL_FENRIR_FOOT_THUNDER(OBJECT *o, int index, float Luminosity); // OMF-01404
    bool Move_MODEL_TWINTAIL_EFFECT(OBJECT *o, int index, float Luminosity);     // OMF-01405
    bool Move_MODEL_TOWER_GATE_PLANE(OBJECT *o, int index, float Luminosity);    // OMF-01406
    bool Move_BITMAP_CRATER(OBJECT *o, int index, float Luminosity);             // OMF-01407
    bool Move_BITMAP_CHROME_ENERGY2(OBJECT *o, int index, float Luminosity);     // OMF-01408
    bool Move_MODEL_STUN_STONE(OBJECT *o, int index, float Luminosity);          // OMF-01409
    bool Move_MODEL_SKIN_SHELL(OBJECT *o, int index, float Luminosity);          // OMF-01410
    bool Move_MODEL_MANA_RUNE(OBJECT *o, int index, float Luminosity);           // OMF-01411
    bool Move_MODEL_SKILL_JAVELIN(OBJECT *o, int index, float Luminosity);       // OMF-01412
    bool Move_MODEL_ARROW_IMPACT(OBJECT *o, int index, float Luminosity);        // OMF-01413
    bool Move_MODEL_SWORD_FORCE(OBJECT *o, int index, float Luminosity);         // OMF-01414
    bool Move_MODEL_PROTECTGUILD(OBJECT *o, int index, float Luminosity);        // OMF-01415
    bool Move_MODEL_MOVE_TARGETPOSITION_EFFECT(OBJECT *o, int index,
                                               float Luminosity); // OMF-01416
    bool Move_BITMAP_TARGET_POSITION_EFFECT1(OBJECT *o, int index,
                                             float Luminosity); // OMF-01417
    bool Move_BITMAP_TARGET_POSITION_EFFECT2(OBJECT *o, int index,
                                             float Luminosity);                     // OMF-01418
    bool Move_MODEL_EFFECT_SAPITRES_ATTACK(OBJECT *o, int index, float Luminosity); // OMF-01419
    bool Move_MODEL_EFFECT_THUNDER_NAPIN_ATTACK_1(OBJECT *o, int index,
                                                  float Luminosity);             // OMF-01420
    bool Move_MODEL_EFFECT_SKURA_ITEM(OBJECT *o, int index, float Luminosity);   // OMF-01421
    bool Move_MODEL_BLOW_OF_DESTRUCTION(OBJECT *o, int index, float Luminosity); // OMF-01422
    bool Move_MODEL_NIGHTWATER_01(OBJECT *o, int index, float Luminosity);       // OMF-01423
    bool Move_MODEL_KNIGHT_PLANCRACK_A(OBJECT *o, int index, float Luminosity);  // OMF-01424
    bool Move_MODEL_KNIGHT_PLANCRACK_B(OBJECT *o, int index, float Luminosity);  // OMF-01425
    bool Move_MODEL_EFFECT_FLAME_STRIKE(OBJECT *o, int index, float Luminosity); // OMF-01426
    bool Move_MODEL_1_STREAMBREATHFIRE(OBJECT *o, int index, float Luminosity);  // OMF-01427
    bool Move_MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_HEAD(OBJECT *o, int index,
                                                       float Luminosity); // OMF-01428
    bool Move_MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_BODY(OBJECT *o, int index,
                                                       float Luminosity);          // OMF-01429
    bool Move_MODEL_LAVAGIANT_FOOTPRINT_R(OBJECT *o, int index, float Luminosity); // OMF-01430
    bool Move_MODEL_PROJECTILE(OBJECT *o, int index, float Luminosity);            // OMF-01431
    bool Move_MODEL_DOOR_CRUSH_EFFECT_PIECE01(OBJECT *o, int index,
                                              float Luminosity); // OMF-01432
    bool Move_MODEL_STATUE_CRUSH_EFFECT_PIECE04(OBJECT *o, int index,
                                                float Luminosity); // OMF-01433
    bool Move_MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_(OBJECT *o, int index,
                                                           float Luminosity); // OMF-01434
    bool Move_MODEL_EMPIREGUARDIAN_BLOW_OF_DESTRUCTION(OBJECT *o, int index,
                                                       float Luminosity);        // OMF-01435
    bool Move_MODEL_EFFECT_SD_AURA(OBJECT *o, int index, float Luminosity);      // OMF-01436
    bool Move_BITMAP_WATERFALL_4(OBJECT *o, int index, float Luminosity);        // OMF-01437
    bool Move_MODEL_WOLF_HEAD_EFFECT(OBJECT *o, int index, float Luminosity);    // OMF-01438
    bool Move_BITMAP_SBUMB(OBJECT *o, int index, float Luminosity);              // OMF-01439
    bool Move_MODEL_DOWN_ATTACK_DUMMY_L(OBJECT *o, int index, float Luminosity); // OMF-01440
    bool Move_MODEL_DOWN_ATTACK_DUMMY_R(OBJECT *o, int index, float Luminosity); // OMF-01441
    bool Move_BITMAP_SWORDEFF(OBJECT *o, int index, float Luminosity);           // OMF-01442
    bool Move_MODEL_SHOCKWAVE01(OBJECT *o, int index, float Luminosity);         // OMF-01443
    bool Move_MODEL_SHOCKWAVE02(OBJECT *o, int index, float Luminosity);         // OMF-01444
    bool Move_BITMAP_DAMAGE1(OBJECT *o, int index, float Luminosity);            // OMF-01445
    bool Move_MODEL_SHOCKWAVE_SPIN01(OBJECT *o, int index, float Luminosity);    // OMF-01446
    bool Move_BITMAP_EVENT_CLOUD(OBJECT *o, int index, float Luminosity);        // OMF-01447
    bool Move_MODEL_WINDFOCE(OBJECT *o, int index, float Luminosity);            // OMF-01448
    bool Move_BITMAP_LIGHT_RED(OBJECT *o, int index, float Luminosity);          // OMF-01449
    bool Move_MODEL_WINDFOCE_MIRROR(OBJECT *o, int index, float Luminosity);     // OMF-01450
    bool Move_BITMAP_SWORD_EFFECT_MONO(OBJECT *o, int index, float Luminosity);  // OMF-01451
    bool Move_MODEL_WOLF_HEAD_EFFECT2(OBJECT *o, int index, float Luminosity);   // OMF-01452
    bool Move_MODEL_SHOCKWAVE_GROUND01(OBJECT *o, int index, float Luminosity);  // OMF-01453
    bool Move_MODEL_DRAGON_KICK_DUMMY(OBJECT *o, int index, float Luminosity);   // OMF-01454
    bool Move_BITMAP_LAVA(OBJECT *o, int index, float Luminosity);               // OMF-01455
    bool Move_MODEL_DRAGON_LOWER_DUMMY(OBJECT *o, int index, float Luminosity);  // OMF-01456
    bool Move_MODEL_TARGETMON_EFFECT(OBJECT *o, int index, float Luminosity);    // OMF-01457
    bool Move_MODEL_VOLCANO_OF_MONK(OBJECT *o, int index, float Luminosity);     // OMF-01458
    bool Move_MODEL_VOLCANO_STONE(OBJECT *o, int index, float Luminosity);       // OMF-01459

  private:
    MoveBehavior &owner_;
};

class MoveBehavior final : protected MoveBehaviorLegacyCalls
{
    friend class MoveBehaviorLegacyCalls;

  public:
    explicit MoveBehavior(SessionKeeper &keeper) noexcept;

    void CreateMayaStone45(OBJECT *o); // OMF-01246

    bool MoveDesair(OBJECT *o, int index, float luminosity);
    bool MoveInfinityArrow4(OBJECT *o, int index, float luminosity);
    bool MoveMagicCapsule2(OBJECT *o, int index, float luminosity);
    bool MoveSpear(OBJECT *o, int index, float luminosity);
    bool MoveSummonerNeilNife(OBJECT *o, int index, float luminosity);
    bool MoveSummonerNeilGround(OBJECT *o, int index, float luminosity);
    bool MoveBitmapFire(OBJECT *o, int index, float luminosity);
    bool MoveBitmapFireRed(OBJECT *o, int index, float luminosity);
    bool MoveBitmapLightMarks(OBJECT *o, int index, float luminosity);
    bool MoveMagic1(OBJECT *o, int index, float luminosity);
    bool MoveMayaStar(OBJECT *o, int index, float luminosity);

    bool Move_MODEL_DRAGON(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_ARROW_AUTOLOAD(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_INFINITY_ARROW(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_INFINITY_ARROW1(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SHIELD_CRASH(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SHIELD_CRASH2(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_IRON_RIDER_ARROW(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_MULTI_SHOT3(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_MULTI_SHOT1(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_MULTI_SHOT2(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_BLADE_SKILL(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_KENTAUROS_ARROW(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_WARP3(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_GHOST(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_TREE_ATTACK(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_BUTTERFLY01(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_SKULL(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL__SPEAR(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_HALLOWEEN_CANDY_BLUE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_HALLOWEEN_EX(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_XMAS_EVENT_BOX(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_XMAS_EVENT_ICEHEART(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_NEWYEARSDAY_EVENT_BEKSULKI(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_MOONHARVEST_MOON(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_MOONHARVEST_GAM(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SPEARSKILL(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_FIRE_CURSEDLICH(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SUMMONER_WRISTRING_EFFECT(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SUMMONER_EQUIP_HEAD_SAHAMUTT(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SUMMONER_EQUIP_HEAD_NEIL(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SUMMONER_CASTING_EFFECT1(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SUMMONER_SUMMON_SAHAMUTT(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SUMMONER_SUMMON_NEIL(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SUMMONER_SUMMON_LAGUL(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_MAGIC(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_OUR_INFLUENCE_GROUND(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_MAGIC_ZIN(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_PIN_LIGHT(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_ORORA(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_GATHERING(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_JOINT_THUNDER(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_IMPACT(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_FLAME(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_RAKLION_BOSS_CRACKEFFECT(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_RAKLION_BOSS_MAGIC(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_FIRE_HIK2_MONO(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_CLOUD(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_CHAIN_LIGHTNING(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_ALICE_DRAIN_LIFE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_ALICE_BUFFSKILL_EFFECT(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_LIGHTNING_SHOCK(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SKILL_BLAST(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_WAVE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_TAIL(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_WAVE_FORCE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SKILL_INFERNO(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_MAGIC_CIRCLE1(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_PROTECT(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_POISON(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SAW(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_LASER(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SKILL_WHEEL1(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SKILL_WHEEL2(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SKILL_FISSURE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_FISSURE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SKILL_FURY_STRIKE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_BALGAS_SKILL(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_CHANGE_UP_EFF(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_CHANGE_UP_NASA(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_CHANGE_UP_CYLINDER(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_DARK_ELF_SKILL(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_MAGIC2(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_STORM(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SUMMON(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_STORM2(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_STORM3(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_MAYASTONE1(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_MAYASTONE4(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_MAYASTONEFIRE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_MAYAHANDSKILL(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_CIRCLE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_CIRCLE_LIGHT(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_ICE_SMALL(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SKULL(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_CUNDUN_PART1(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_CURSEDTEMPLE_STATUE_PART1(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_XMAS2008_SNOWMAN_HEAD(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_XMAS2008_SNOWMAN_BODY(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_DOPPELGANGER_SLIME_CHIP(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_WATER_WAVE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_STAFF_OF_DESTRUCTION(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_PIERCING(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_ARROW_BEST_CROSSBOW(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_ARROW_DOUBLE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_ARROW_HOLY(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_ARROW(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_ARROW_STEEL(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_DARK_SCREAM_FIRE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_CURSEDTEMPLE_HOLYITEM(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_CURSEDTEMPLE_PRODECTION_SKILL(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_CURSEDTEMPLE_RESTRAINT_SKILL(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_ARROW_SPARK(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_ARROW_RING(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_ARROW_TANKER(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_ARROW_BOMB(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_ARROW_DARKSTINGER(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_DUNGEON_STONE01(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_WARCRAFT(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_FIRECRACKERRISE(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_FIRECRACKER(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_FIRECRACKER0001(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_FIRECRACKER0002(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_FIRECRACKER0003(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_SWORD_FORCE(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_BLIZZARD(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_SHOTGUN(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SHINE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_BLIZZARD(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_ARROW_DRILL(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_COMBO(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_WAVES(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_AIR_FORCE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_PIERCING2(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_DEASULER(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_DEATH_SPI_SKILL(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_PIER_PART(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_FLARE_FORCE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_DARKLORD_SKILL(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_GROUND_STONE(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_TWLIGHT(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_SHOCK_WAVE(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_DAMAGE_01_MONO(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_FLARE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_CUNDUN_DRAGON_HEAD(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_CUNDUN_PHOENIX(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_CUNDUN_GHOST(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_CUNDUN_SKILL(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_BATTLE_GUARD2(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_ARROW_TANKER_HIT(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_FLY_BIG_STONE1(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_FLY_BIG_STONE2(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_BIG_STONE_PART1(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_GATE_PART1(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_AURORA(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_FENRIR_THUNDER(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_FALL_STONE_EFFECT(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_FENRIR_FOOT_THUNDER(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_TWINTAIL_EFFECT(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_TOWER_GATE_PLANE(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_CRATER(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_CHROME_ENERGY2(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_STUN_STONE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SKIN_SHELL(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_MANA_RUNE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SKILL_JAVELIN(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_ARROW_IMPACT(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SWORD_FORCE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_PROTECTGUILD(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_MOVE_TARGETPOSITION_EFFECT(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_TARGET_POSITION_EFFECT1(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_TARGET_POSITION_EFFECT2(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_EFFECT_SAPITRES_ATTACK(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_EFFECT_THUNDER_NAPIN_ATTACK_1(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_EFFECT_SKURA_ITEM(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_BLOW_OF_DESTRUCTION(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_NIGHTWATER_01(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_KNIGHT_PLANCRACK_A(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_KNIGHT_PLANCRACK_B(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_EFFECT_FLAME_STRIKE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_1_STREAMBREATHFIRE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_HEAD(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_BODY(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_LAVAGIANT_FOOTPRINT_R(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_PROJECTILE(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_DOOR_CRUSH_EFFECT_PIECE01(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_STATUE_CRUSH_EFFECT_PIECE04(OBJECT *o, int index, float Luminosity);
    void AdvanceGaionEvents(OBJECT &object);
    void EmitWaterWaves(OBJECT &object, const OBJECT &source, float period, int height);
    void EmitGaionEvent(OBJECT &object, float life);
    void EmitGaionImpact(OBJECT &object, vec3_t angle);
    bool Move_MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_EMPIREGUARDIAN_BLOW_OF_DESTRUCTION(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_EFFECT_SD_AURA(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_WATERFALL_4(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_WOLF_HEAD_EFFECT(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_SBUMB(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_DOWN_ATTACK_DUMMY_L(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_DOWN_ATTACK_DUMMY_R(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_SWORDEFF(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SHOCKWAVE01(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SHOCKWAVE02(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_DAMAGE1(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SHOCKWAVE_SPIN01(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_EVENT_CLOUD(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_WINDFOCE(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_LIGHT_RED(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_WINDFOCE_MIRROR(OBJECT *o, int index, float Luminosity);
    bool Move_BITMAP_SWORD_EFFECT_MONO(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_WOLF_HEAD_EFFECT2(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_SHOCKWAVE_GROUND01(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_DRAGON_KICK_DUMMY(OBJECT *o, int index, float Luminosity);
    void EmitDragonLoreSamples(OBJECT *o, float fraction = 1.f);
    bool Move_BITMAP_LAVA(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_DRAGON_LOWER_DUMMY(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_TARGETMON_EFFECT(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_VOLCANO_OF_MONK(OBJECT *o, int index, float Luminosity);
    bool Move_MODEL_VOLCANO_STONE(OBJECT *o, int index, float Luminosity);

  private:
    void ApplyCatapultGroundImpact(OBJECT &object, bool collideWithObjects);
    void ApplyCatapultTerrainImpact(OBJECT &object, float height);
    void EmitTankerTrail(OBJECT &object);
    void EmitLightningShockAir(OBJECT &effect, float duration);
    void EmitDragonKickWind(OBJECT &effect, float fraction);
    void EmitSahamuttFlames(OBJECT &effect);
    void EmitBladeVolley(OBJECT &effect, float fraction);
    void EmitThunderCloud(OBJECT &effect);
    void EmitRotatingArrowTrail(OBJECT &effect);
    void EmitSakuraPetals(OBJECT &effect, BMD &model);
    void EmitGatheringBirth(OBJECT &effect, vec3_t origin, bool lightning);
    void EmitDarkStingerFeathers(OBJECT &effect, BMD &model, vec3_t velocity);
    SessionEffectPool<JOINT> &Joints;
    CMapManager &gMapManager;
    float &FPS_ANIMATION_FACTOR;
    double &WorldTime;
};

const std::vector<std::pair<int, MoveHandler>> &ExtractedMoveHandlers();
} // namespace GameLogic::Effects::Behaviors

class OBJECT;
class World;

namespace GameLogic::Effects
{
enum class DebrisMotion
{
    Heavy,
    MayaStone,
    IceStone,
    SkinShell,
    Wall,
    Candy,
    Christmas,
    NewYear,
    MoonHarvest,
    Snowman
};
void AdvanceBouncingDebris(OBJECT &effect, const World &world, float frames, DebrisMotion kind);
} // namespace GameLogic::Effects

// Registry mapping an effect type (a MODEL_* / BITMAP_* enum value) to its
// data-driven EffectDescriptor. The table is built once on first use and looked
// up by CreateEffect / MoveEffect. Types with no entry fall back
// to the legacy switch statements, so migration can proceed effect by effect.
namespace GameLogic::Effects
{
// Returns the descriptor for an effect type, or nullptr if the type has not
// been migrated to the registry yet. Lookup is O(1) (type-indexed table).
const EffectDescriptor *Lookup(int type);
} // namespace GameLogic::Effects

namespace GameLogic::Effects::Motion
{
Core::Time::BounceResult AdvanceJump(float position[3], float &gravity, float direction[3],
                                     const float angle[3], float groundHeight, float frames);
}

// Logical index zero is the newest authored sample. Capacity survives retirement;
// indexing and insertion never walk or shift the retained history.
template <class T> class CircularEffectHistory final
{
    static_assert(std::is_trivially_copyable_v<T>);

  public:
    void Reset(std::size_t size)
    {
        head_ = size_ = 0;
        Resize(size);
    }
    void Clear() noexcept
    {
        head_ = size_ = 0;
    }
    bool Empty() const noexcept
    {
        return size_ == 0;
    }
    std::size_t Capacity() const noexcept
    {
        return capacity_;
    }
    std::size_t Size() const noexcept
    {
        return size_;
    }

    void Resize(std::size_t size)
    {
        if (size == size_)
            return;
        Normalize();
        if (size > capacity_)
        {
            const auto capacity = (std::max)(size, capacity_ * 2);
            auto values = std::make_unique<T[]>(capacity);
            if (size_ != 0)
                std::memcpy(values.get(), values_.get(), size_ * sizeof(T));
            values_ = std::move(values);
            capacity_ = capacity;
        }
        else if (size > size_)
            std::memset(values_.get() + size_, 0, (size - size_) * sizeof(T));
        size_ = size;
    }

    void Advance() noexcept
    {
        head_ = head_ == 0 ? size_ - 1 : head_ - 1;
    }
    void RepeatFront() noexcept
    {
        T sample;
        std::memcpy(&sample, &(*this)[0], sizeof(T));
        Advance();
        std::memcpy(&(*this)[0], &sample, sizeof(T));
    }
    T &operator[](std::size_t index) noexcept
    {
        return values_[(head_ + index) % size_];
    }
    const T &operator[](std::size_t index) const noexcept
    {
        return values_[(head_ + index) % size_];
    }

  private:
    void Normalize() noexcept
    {
        if (head_ == 0)
            return;
        const auto cycles = std::gcd(head_, size_);
        for (std::size_t start = 0; start < cycles; ++start)
        {
            T sample;
            std::memcpy(&sample, &values_[start], sizeof(T));
            auto current = start;
            for (auto next = (current + head_) % size_; next != start;
                 next = (current + head_) % size_)
            {
                std::memcpy(&values_[current], &values_[next], sizeof(T));
                current = next;
            }
            std::memcpy(&values_[current], &sample, sizeof(T));
        }
        head_ = 0;
    }

    std::unique_ptr<T[]> values_;
    std::size_t capacity_ = 0;
    std::size_t size_ = 0;
    std::size_t head_ = 0;
};

// A hierarchy of non-empty words. Finding the next live/free slot skips empty
// pages instead of walking the retained capacity after a burst has retired.
class EffectSlotSet final
{
  public:
    static constexpr std::size_t End = static_cast<std::size_t>(-1);
    void Grow(std::size_t bits);
    bool Set(std::size_t bit, bool present) noexcept;
    std::size_t Next(std::size_t first) const noexcept;

  private:
    static constexpr std::size_t WordBits = 64;
    std::size_t NextAtLevel(std::size_t level, std::size_t first) const noexcept;
    std::vector<std::vector<std::uint64_t>> levels_;
};

enum class AlternatingSmokeStyle
{
    Plain,
    Flare,
    Barracks
};

enum eTypeSkill : int
{
    eTypeSkill_None = -1,
    eTypeSkill_CommonAttack,
    eTypeSkill_Buff,
    eTypeSkill_DeBuff,
    eTypeSkill_FrendlySkill,
    eTypeSkill_End,
};

enum eBuffClass
{
    eBuffClass_Buff = 0,
    eBuffClass_DeBuff,

    eBuffClass_Count,
};

enum eBuffState : int
{
    eBuffNone = 0,
    eBuff_Attack,
    eBuff_Defense,
    eBuff_HelpNpc,
    eBuff_WizDefense,
    eBuff_AddCriticalDamage,
    eBuff_InfinityArrow,
    eBuff_AddAG,
    eBuff_Life,
    eBuff_AddMana,
    eBuff_BlessPotion,
    eBuff_SoulPotion,
    eBuff_RemovalMagic,
    eBuff_CastleGateIsOpen,
    eBuff_CastleRegimentDefense,
    eBuff_CastleRegimentAttack1,
    eBuff_CastleRegimentAttack2,
    eBuff_CastleRegimentAttack3,
    eBuff_Cloaking,
    eBuff_AddSkill,
    eBuff_CastleCrown,
    eBuff_CrywolfAltarEnable,
    eBuff_CrywolfAltarDisable,
    eBuff_CrywolfAltarContracted,
    eBuff_CrywolfAltarAttempt,
    eBuff_CrywolfAltarOccufied,
    eBuff_CrywolfHeroContracted,
    eBuff_CrywolfNPCHide,
    eBuff_GMEffect,
    eBuff_PcRoomSeal1,
    eBuff_PcRoomSeal2,
    eBuff_PcRoomSeal3,
    eBuff_CursedTempleQuickness,
    eBuff_CursedTempleSublimation,
    eBuff_CursedTempleProdection,
    eBuff_Hellowin1,
    eBuff_Hellowin2,
    eBuff_Hellowin3,
    eBuff_Hellowin4,
    eBuff_Hellowin5,
    //	eBuff_LuckSeal,
    eBuff_Seal1,
    eBuff_Seal2,
    eBuff_Seal3,
    eBuff_Seal4,
    eBuff_EliteScroll1,
    eBuff_EliteScroll2,
    eBuff_EliteScroll3,
    eBuff_EliteScroll4,
    eBuff_EliteScroll5,
    eBuff_EliteScroll6,
    eBuff_SecretPotion1,
    eBuff_SecretPotion2,
    eBuff_SecretPotion3,
    eBuff_SecretPotion4,
    eBuff_SecretPotion5,

    // DeBuff
    eDeBuff_Poison,
    eDeBuff_Freeze,
    eDeBuff_Harden,
    eDeBuff_Defense,
    eDeBuff_Attack,
    eDeBuff_MagicPower,
    eDeBuff_Stun,
    eDeBuff_InvincibleMagic,
    eDeBuff_InvincibleMagicAttack,
    eDeBuff_InvinciblePhysAttack,
    eDeBuff_CursedTempleRestraint,

    eBuff_CrywolfProdection1,
    eBuff_CrywolfProdection2,
    eBuff_CrywolfProdection3,
    eBuff_CrywolfProdection4,
    eBuff_CrywolfProdection5,

    eBuff_Thorns = 71,
    eDeBuff_Sleep = 72,
    eDeBuff_Blind = 73,
    eDeBuff_NeilDOT = 74,
    eDeBuff_SahamuttDOT = 75,
    eDeBuff_AttackDown = 76,
    eDeBuff_DefenseDown = 77,
    eBuff_CherryBlossom_Liguor,
    eBuff_CherryBlossom_RiceCake,
    eBuff_CherryBlossom_Petal,
    eBuff_Berserker = 81,
    eBuff_SwellOfMagicPower = 82,
    eDeBuff_FlameStrikeDamage = 83,
    eDeBuff_GiganticStormDamage = 84,
    eDeBuff_LightningShockDamage = 85,
    eDeBuff_BlowOfDestruction = 86,
    eBuff_Seal_HpRecovery = 87,
    eBuff_Seal_MpRecovery = 88,
    eBuff_Scroll_Battle = 89,
    eBuff_Scroll_Strengthen = 90,
    eBuff_BlessingOfXmax = 91,
    eBuff_CureOfSanta,
    eBuff_SafeGuardOfSanta,
    eBuff_StrengthOfSanta,
    eBuff_DefenseOfSanta,
    eBuff_QuickOfSanta,
    eBuff_LuckOfSanta,
    eBuff_DuelWatch = 98,
    eBuff_GuardCharm = 99,
    eBuff_ItemGuardCharm = 100,
    eBuff_AscensionSealMaster = 101,
    eBuff_WealthSealMaster = 102,
    eBuff_HonorOfGladiator = 103,
    eBuff_Doppelganger_Ascension = 105,
    eBuff_PartyExpBonus = 112,
    eBuff_AG_Addition = 113,
    eBuff_SD_Addition = 114,
    eBuff_NewWealthSeal = 119,
    eDeBuff_Discharge_Stamina = 120,
    eBuff_Scroll_Healing = 121,
    EFFECT_HAWK_FIGURINE = 122,
    EFFECT_GOAT_FIGURINE = 123,
    EFFECT_OAK_CHARM = 124,
    EFFECT_MAPLE_CHARM = 125,
    EFFECT_GOLDEN_OAK_CHARM = 126,
    EFFECT_GOLDEN_MAPLE_CHARM = 127,
    EFFECT_WORN_HORSESHOE = 128,
    eBuff_Att_up_Ourforces = 129,
    eBuff_Hp_up_Ourforces = 130,
    eBuff_Def_up_Ourforces = 131,
    EFFECT_IRON_DEFENSE = 134,
    EFFECT_GREATER_LIFE_ENHANCED = 135,
    EFFECT_GREATER_LIFE_MASTERED = 136,
    EFFECT_DEATH_STAB_ENHANCED = 137,
    EFFECT_MAGIC_CIRCLE_IMPROVED = 138,
    EFFECT_MAGIC_CIRCLE_ENHANCED = 139,
    EFFECT_MANA_SHIELD_MASTERED = 140,
    EFFECT_FROZEN_STAB_MASTERED = 141,
    EFFECT_BLESS = 142,
    EFFECT_INFINITY_ARROW_IMPROVED = 143,
    EFFECT_BLIND_IMPROVED = 144,
    EFFECT_DRAIN_LIFE_ENHANCED = 145,
    EFFECT_ICE_STORM_ENHANCED = 146,
    EFFECT_EARTH_PRISON = 147,
    EFFECT_GREATER_CRITICAL_DAMAGE_MASTERED = 148,
    EFFECT_GREATER_CRITICAL_DAMAGE_EXTENDED = 149,
    EFFECT_SWORD_POWER_IMPROVED = 150,
    EFFECT_SWORD_POWER_ENHANCED = 151,
    EFFECT_SWORD_POWER_MASTERED = 152,
    EFFECT_GREATER_DEFENSE_SUCCESS_RATE_IMPROVED = 153,
    EFFECT_GREATER_DEFENSE_SUCCESS_RATE_ENHANCED = 154,
    EFFECT_FITNESS_IMPROVED = 155,
    EFFECT_DRAGON_ROAR_ENHANCED = 157,
    EFFECT_CHAIN_DRIVER_ENHANCED = 158,
    EFFECT_POISON_ARROW = 159,
    EFFECT_POISON_ARROW_IMPROVED = 160,
    EFFECT_BLESS_IMPROVED = 161,
    EFFECT_LESSER_DAMAGE_IMPROVED = 162,
    EFFECT_LESSER_DEFENSE_IMPROVED = 163,
    EFFECT_FIRE_SLASH_ENHANCED = 164,
    EFFECT_IRON_DEFENSE_IMPROVED = 165,
    EFFECT_BLOOD_HOWLING = 166,
    EFFECT_BLOOD_HOWLING_IMPROVED = 167,
    EFFECT_PENTAGRAM_JEWEL_HALF_SD = 174,
    EFFECT_PENTAGRAM_JEWEL_HALF_MP = 175,
    EFFECT_PENTAGRAM_JEWEL_HALF_SPEED = 176,
    EFFECT_PENTAGRAM_JEWEL_HALF_HP = 177,
    EFFECT_PENTAGRAM_JEWEL_STUN = 178,
    EFFECT_ARCA_FIRETOWER = 179,
    EFFECT_ARCA_WATERTOWER = 180,
    EFFECT_ARCA_EARTHTOWER = 181,
    EFFECT_ARCA_WINDTOWER = 182,
    EFFECT_ARCA_DARKNESSTOWER = 183,
    EFFECT_ARCA_DEATHPENALTY = 184,
    EFFECT_PENTAGRAM_JEWEL_SLOW = 186,
    EFFECT_ARCA_ARCHERONBUFF = 187,
    EFFECT_TALISMAN_OF_ASCENSION1 = 190,
    EFFECT_TALISMAN_OF_ASCENSION2 = 191,
    EFFECT_TALISMAN_OF_ASCENSION3 = 192,
    EFFECT_SEAL_OF_ASCENSION3 = 193,
    EFFECT_MASTER_SEAL_OF_ASCENSION2 = 194,
    EFFECT_BLESSING_OF_LIGHT = 195,
    EFFECT_MASTER_SCROLL_OF_DEFENSE = 196,
    EFFECT_MASTER_SCROLL_OF_MAGIC_DAMAGE = 197,
    EFFECT_MASTER_SCROLL_OF_LIFE = 198,
    EFFECT_MASTER_SCROLL_OF_MANA = 199,
    EFFECT_MASTER_SCROLL_OF_DAMAGE = 200,
    EFFECT_MASTER_SCROLL_OF_HEALING = 201,
    EFFECT_MASTER_SCROLL_OF_BATTLE = 202,
    EFFECT_MASTER_SCROLL_OF_STRENGTH = 203,
    EFFECT_MASTER_SCROLL_OF_QUICK = 204,
    EFFECT_TESTE_205 = 205,
    eBuff_Count,
};

enum eBuffTimeType
{
    eBuffTime_None = 0,
    eBuffTime_Hellowin = 1006,
    eBuffTime_PcRoomSeal,
    eBuffTime_Seal,
    eBuffTime_Scroll,
    eBuffTime_Secret,
    eBuffTime_CherryBlossom,
    eBuffTime_SwellOfMP,
    eBuffTime_Christmax,
    eBuffTime_HonorOfGladiator,
    eBuffTime_GuardCharm,
    eBuffTime_ItemGuardCharm,
    eBuffTime_AG_Addition,
    eBuffTime_SD_Addition,
    eBuffTime_PartyExpBonus,
    eBuffTime_Count,
};

enum eBuffValueLoadType
{
    eBuffValueLoad_None = 0,
    eBuffValueLoad_Skill,
    eBuffValueLoad_Item,
    eBuffValueLoad_Text,
    eBuffValueLoad_ItemAddOption,

    eBuffValueLoad_Count,
};

class OBJECT;

struct SessionTexturePropertiesSlot;
typedef struct PARTICLE
{
    EffectBirthTiming BirthTiming;
    const SessionTexturePropertiesSlot *RenderTexture = nullptr;
    const SessionTexturePropertiesSlot *TypeTexture = nullptr;
    const SessionTexturePropertiesSlot *AdditionalTexture = nullptr;
    std::shared_ptr<const CharacterSocketBinding> SocketBinding;
    bool PresentationRandom = false;
    bool Live;
    int Type;
    int TexType;
    int SubType;
    float Scale;
    vec3_t Position;
    vec3_t Angle;
    vec3_t Light;
    float Alpha;
    float LifeTime;
    OBJECT *Target;
    float Rotation;
    float Frame;

    float MotionIntervalFrames = 0.f;
    float RotationNoiseFrames = 0.f;
    float ScalarNoiseRate = 0.f;
    float ScaleNoiseFrames = 0.f;
    float ScaleNoiseRate = 0.f;
    float AlphaNoiseFrames = 0.f;
    float AlphaNoiseRate = 0.f;
    std::uint64_t MotionRandomSeed = 0;
    std::uint64_t AlphaRandomState = 0;
    std::uint64_t ScaleRandomState = 0;
    bool bEnableMove;
    float Gravity;
    vec3_t Velocity;
    vec3_t TurningForce;
    vec3_t StartPosition;
    int iNumBone;
    bool bRepeatedly;
    float fRepeatedlyHeight;
} PARTICLE;

typedef struct
{
    bool Live;
    int Type;
    OBJECT *Owner;
} OPERATE;

typedef struct JOINT
{
    EffectBirthTiming BirthTiming;
    bool PresentationRandom = false;
    bool Live;
    int Type;
    int TexType;
    int SubType;
    BYTE RenderType;
    BYTE RenderFace;
    float Scale;
    vec3_t Position;
    vec3_t StartPosition;
    vec3_t Angle;
    vec3_t HeadAngle;
    vec3_t Light;
    OBJECT *Target;
    vec3_t TargetPosition;
    BYTE byOnlyOneRender;
    float LifeTime;
    bool Collision;
    float Velocity;
    vec3_t Direction;
    short PKKey;
    WORD Skill;
    float Weapon;
    float MultiUse;
    bool bTileMapping;
    BYTE m_byReverseUV;
    int TargetIndex[5];
    BYTE m_bySkillSerialNum;
    int m_iChaIndex;
    short int m_sTargetIndex;

    // Fields about the "Tails" of effects:
    bool m_bCreateTails; // Flag, if tails are created.
    int NumTails; // The number of currently used tail entries. Usually this gets increased by one in every frame until the maximum is reached.
    int MaxTails; // The maximum number of tail entries to use.
    using Tail = vec3_t[4];
    std::array<float, 2> HeadSpriteAngles{};
    float SpiritPhase = 0.f;        // Kundun spirit animation; PKKey remains an identity.
    float SpiritMotionFrames = 0.f; // Remaining time in subtype 18 movement interval.
    float MotionFrames = 0.f;
    float MotionAcceleration = 0.f;
    float MotionScale = 0.f;
    bool MotionChecksTarget = false;
    vec3_t MotionVelocity{};
    vec3_t MotionAngleRate{};
    float TailSampleFrames = 0.f;
    Tail LastTailSample{};
    CircularEffectHistory<Tail> Tails; // Allocation survives ordinary retirement and reuse.
    void SetMaxTails(int count)
    {
        MaxTails = count;
        Tails.Resize(static_cast<std::size_t>((std::max)(count, 1)));
    }
} JOINT;

bool AttackCharacterRange(int Index, vec3_t Position, float Range, BYTE Serial, short PKKey = -1,
                          WORD SkillSerialNum = 0);

namespace GameLogic::Effects
{
void CreateTail(JOINT *o, float Matrix[3][4], bool Blur = false);
void CreateTimedTail(JOINT *joint, float matrix[3][4], float referenceFrames, bool blur = false,
                     BYTE axis = 0);
void CreateTailAxis(JOINT *o, float Matrix[3][4], BYTE axis = 0);
} // namespace GameLogic::Effects

// Stable-address effect storage shared by gameplay and visual advancement.
template <class T> class SessionEffectPool final
{
  public:
    static constexpr int End = -1;
    SessionEffectPool()
    {
        AddPage();
    }
    SessionEffectPool(const SessionEffectPool &) = delete;
    SessionEffectPool &operator=(const SessionEffectPool &) = delete;

    int Allocate()
    {
        auto slot = free_.Next(0);
        if (slot == EffectSlotSet::End)
        {
            AddPage();
            slot = free_.Next(0);
        }
        Activate(static_cast<int>(slot));
        return static_cast<int>(slot);
    }

    void Activate(int slot)
    {
        while (slot >= Capacity())
            AddPage();
        if (live_.Set(slot, true))
            ++used_;
        free_.Set(slot, false);
        (*this)[slot].Live = true;
    }

    void Retire(int slot) noexcept
    {
        if (live_.Set(slot, false))
            --used_;
        free_.Set(slot, true);
        (*this)[slot].Live = false;
    }

    void Retire(T &value) noexcept
    {
        Retire(*Find(&value));
    }
    void SetLive(int slot, bool live)
    {
        if (live)
            Activate(slot);
        else
            Retire(slot);
    }

    std::optional<int> Find(const T *value) const noexcept
    {
        const auto address = reinterpret_cast<std::uintptr_t>(value);
        auto page = std::upper_bound(
            addresses_.begin(), addresses_.end(), address,
            [](std::uintptr_t key, const PageAddress &candidate) { return key < candidate.start; });
        if (page == addresses_.begin())
            return std::nullopt;
        --page;
        const auto offset = address - page->start;
        if (offset >= sizeof(Page))
            return std::nullopt;
        return page->index * PageSize + static_cast<int>(offset / sizeof(T));
    }

    int Next(int first = 0) const noexcept
    {
        const auto slot = live_.Next(static_cast<std::size_t>(first));
        return slot == EffectSlotSet::End ? End : static_cast<int>(slot);
    }
    int Capacity() const noexcept
    {
        return static_cast<int>(pages_.size()) * PageSize;
    }
    std::size_t size() const noexcept
    {
        return used_;
    }
    bool empty() const noexcept
    {
        return used_ == 0;
    }
    T &operator[](int slot) noexcept
    {
        return (*pages_[slot / PageSize])[slot % PageSize];
    }
    const T &operator[](int slot) const noexcept
    {
        return (*pages_[slot / PageSize])[slot % PageSize];
    }

  private:
    static constexpr int PageSize = 64;
    using Page = std::array<T, PageSize>;
    struct PageAddress final
    {
        std::uintptr_t start;
        int index;
    };

    template <bool Constant> class Cursor final
    {
        using Pool = std::conditional_t<Constant, const SessionEffectPool, SessionEffectPool>;

      public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using reference = std::conditional_t<Constant, const T &, T &>;
        using pointer = std::conditional_t<Constant, const T *, T *>;
        Cursor() = default;
        Cursor(Pool *pool, int slot)
            : pool_(pool), slot_(slot), value_(slot == End ? nullptr : &(*pool)[slot])
        {
        }
        int Index() const noexcept
        {
            return slot_;
        }
        reference operator*() const noexcept
        {
            return *value_;
        }
        pointer operator->() const noexcept
        {
            return value_;
        }
        Cursor &operator++() noexcept
        {
            // Payloads within a page are contiguous. Read the current membership
            // of the adjacent slot after the callback; never cache future births.
            if (++slot_ % PageSize != 0 && (++value_)->Live)
                return *this;
            slot_ = pool_->Next(slot_);
            value_ = slot_ == End ? nullptr : &(*pool_)[slot_];
            return *this;
        }
        Cursor operator++(int) noexcept
        {
            auto previous = *this;
            ++*this;
            return previous;
        }
        friend bool operator==(const Cursor &first, const Cursor &second) noexcept
        {
            return first.pool_ == second.pool_ && first.slot_ == second.slot_;
        }

      private:
        Pool *pool_ = nullptr;
        int slot_ = End;
        pointer value_ = nullptr;
    };

  public:
    // Increment queries membership after the caller finishes. Higher-slot births
    // run this pass; lower/equal-slot births wait until the next pass.
    auto begin() noexcept
    {
        return Cursor<false>(this, Next());
    }
    auto end() noexcept
    {
        return Cursor<false>(this, End);
    }
    auto begin() const noexcept
    {
        return Cursor<true>(this, Next());
    }
    auto end() const noexcept
    {
        return Cursor<true>(this, End);
    }

  private:
    void AddPage()
    {
        // Slot identities are int throughout the existing effect interfaces.
        if (pages_.size() >= static_cast<std::size_t>((std::numeric_limits<int>::max)() / PageSize))
            throw std::bad_alloc();
        const int first = Capacity();
        auto page = std::make_unique<Page>();
        const PageAddress address{reinterpret_cast<std::uintptr_t>(page->data()), first / PageSize};
        if (pages_.size() == pages_.capacity())
            pages_.reserve((std::max)(pages_.capacity() * 2, std::size_t{1}));
        if (addresses_.size() == addresses_.capacity())
            addresses_.reserve((std::max)(addresses_.capacity() * 2, std::size_t{1}));
        live_.Grow(first + PageSize);
        free_.Grow(first + PageSize);
        pages_.push_back(std::move(page));
        addresses_.insert(std::lower_bound(addresses_.begin(), addresses_.end(), address.start,
                                           [](const PageAddress &candidate, std::uintptr_t key) {
                                               return candidate.start < key;
                                           }),
                          address);
        for (int slot = first; slot < first + PageSize; ++slot)
            free_.Set(slot, true);
    }

    std::vector<std::unique_ptr<Page>> pages_;
    std::vector<PageAddress> addresses_;
    EffectSlotSet live_;
    EffectSlotSet free_;
    std::size_t used_ = 0;
};

// Persistent blur and weather effect storage.
#define MAX_LEAVES 200
#define MAX_LEAVES_DOUBLE 400

#pragma pack(push)
#pragma pack()
inline constexpr int MAX_BLUR_TAILS = 30;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int MAX_OBJECT_BLUR_TAILS = 600;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
using BlurVec3 = float[3];
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
struct Blur final
{
    bool Live = false;
    int Type = 0;
    float LifeTime = 0;
    CHARACTER *Owner = nullptr;
    int Number = 0;
    BlurVec3 Light{};
    CircularEffectHistory<BlurVec3> P1;
    CircularEffectHistory<BlurVec3> P2;
    int SubType = 0;
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
struct ObjectBlur final
{
    bool Live = false;
    int Type = 0;
    float LifeTime = 0;
    OBJECT *Owner = nullptr;
    int Number = 0;
    BlurVec3 Light{};
    int LimitLifeTime = 0;
    CircularEffectHistory<BlurVec3> P1;
    CircularEffectHistory<BlurVec3> P2;
    CircularEffectHistory<std::uint8_t> BreakAfter;
    int SubType = 0;
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
struct SessionBlurStorage final
{
    SessionEffectPool<Blur> blurs;
    SessionEffectPool<ObjectBlur> objectBlurs;
};
#pragma pack(pop)

#ifdef DEVIAS_XMAS_EVENT
#pragma pack(push)
#pragma pack()
inline constexpr int SESSION_LEAF_CAPACITY = MAX_LEAVES_DOUBLE;
#pragma pack(pop)
#endif

#ifdef DEVIAS_XMAS_EVENT
#else
#pragma pack(push)
#pragma pack()
inline constexpr int SESSION_LEAF_CAPACITY = MAX_LEAVES;
#pragma pack(pop)
#endif

#pragma pack(push)
#pragma pack()
using SessionLeaves = PARTICLE[SESSION_LEAF_CAPACITY];
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
struct SessionLeafStorage final
{
    SessionLeaves Leaves{};
};
#pragma pack(pop)
