#pragma once
#include "support/CoreMath.h"
#include "domain/WorldSimulation.h"
#include "app/ApplicationLoopFrame.h"
#include "render/ModelGeometry.h"
#include "domain/CharacterSystem.h"
#include "session/SessionRuntime.h"
#include "render/Sprites.h"
#include "data/CharacterData.h"
#include "render/ModelResources.h"
#include "data/Localization.h"
#include "render/World.h"
#include <array>
#include <vector>
#include <optional>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <cstdint>
#include <map>

class CMapManager;
class SessionKeeper;

class CDummyUnit
{
  private:
    static constexpr float MAX_DUMMYDISTANCE = 500.0f;
    vec3_t m_vPosition;
    vec3_t m_vStartPosition;
    vec3_t m_vTargetPosition;
    vec3_t m_vDirection;
    float m_fDisFrame;
    float m_fAniFrame;
    float m_fAlpha;
    float m_fAniFrameSpeed;

  public:
    explicit CDummyUnit(SessionKeeper &keeper);
    ~CDummyUnit();

    void Init(vec3_t Pos = NULL, vec3_t Target = NULL);
    void Destroy();
    bool IsDistance(float animationFactor);
    vec_t *GetPosition();
    vec_t *GetStartPosition();
    float GetAniFrame(bool changed = false);
    float GetAlpha();
    void CalDummyPosition(vec3_t vOutPos, float &fAni, bool bChange, float animationFactor);

  private:
    CMapManager &gMapManager;
};
#define MONSTER01_STOP1 0
#define MONSTER01_STOP2 1
#define MONSTER01_WALK 2
#define MONSTER01_ATTACK1 3
#define MONSTER01_ATTACK2 4
#define MONSTER01_SHOCK 5
#define MONSTER01_DIE 6
#define MONSTER01_APEAR 7
#define MONSTER01_ATTACK3 8
#define MONSTER01_ATTACK4 9
#define MONSTER01_RUN 10
#define MONSTER01_ATTACK5 11
#define FENRIR_STAND 0
#define FENRIR_WALK 1
#define FENRIR_RUN 2
#define FENRIR_ATTACK 3
#define FENRIR_ATTACK_SKILL 4
#define FENRIR_DAMAGE 5
#define FENRIR_RUN_DELAY 20
#define DREADFEAR_ATTACK2_MOTION_COUNT 2
#define KANTURU2ND_NPC_ANI_STOP 0
#define KANTURU2ND_NPC_ANI_ROT 1

void AdvanceHoveringPet(OBJECT &pet, float frames, double worldTime, float speedMultiplier,
                        float speedOffset, float rescueHeight);

// Collector decisions keep their authored reference cadence. Their produced
// movement is consumed continuously, with the owner's height sampled in time.
template <class Prepare>
void AdvanceCollectorMotion(OBJECT &pet, float frames, double tick, double worldTime,
                            double referenceMilliseconds, Prepare &&prepare)
{
    const float totalFrames = frames;
    while (frames > 0.f)
    {
        if (pet.EffectMotionFrames <= 0.f)
        {
            vec3_t ownerPosition, ownerAngle, start, angle;
            const float fraction = (totalFrames - frames) / totalFrames;
            pet.Owner->MotionTrace.Sample(worldTime, fraction, pet.Owner->Position, ownerPosition);
            VectorCopy(pet.Owner->Angle, ownerAngle);
            ownerAngle[2] = pet.Owner->MotionTrace.SampleYaw(worldTime, fraction, ownerAngle[2]);
            VectorCopy(pet.Position, start);
            VectorCopy(pet.Angle, angle);
            pet.AmbientVerticalNoise = pet.Velocity;
            prepare(tick - frames * referenceMilliseconds + referenceMilliseconds, ownerPosition,
                    ownerAngle);
            VectorSubtract(pet.Position, start, pet.EffectMotionVelocity);
            for (int axis = 0; axis < 3; ++axis)
                pet.EffectMotionAngleRate[axis] =
                    std::remainder(pet.Angle[axis] - angle[axis], 360.f);
            VectorCopy(start, pet.Position);
            VectorCopy(angle, pet.Angle);
            pet.AmbientSpeedNoise = ownerPosition[2];
            pet.EffectMotionFrames = 1.f;
        }
        const float step = Core::Time::ReferenceStep(frames, pet.EffectMotionFrames);
        const float priorTravel = pet.EffectAnimationAdvance.value_or(0.f);
        ObjectMotionTrace::AnimationPhase phase{pet.AnimationFrame + priorTravel,
                                                pet.PriorAnimationFrame, pet.CurrentAction,
                                                pet.PriorAction};
        if (std::floor(phase.frame) != std::floor(pet.AnimationFrame))
        {
            phase.priorAction = phase.action;
            phase.adjacentKeys = true;
        }
        const float travel = pet.AmbientVerticalNoise * step;
        pet.MotionTrace.AdvanceAnimation(step, phase, travel);
        pet.EffectAnimationAdvance = priorTravel + travel;
        pet.MotionTrace.TurnYaw(step, pet.Angle[2],
                                pet.Angle[2] + pet.EffectMotionAngleRate[2] * step, 0.f);
        VectorAddScaled(pet.Position, pet.EffectMotionVelocity, pet.Position, step);
        VectorAddScaled(pet.Angle, pet.EffectMotionAngleRate, pet.Angle, step);
        pet.EffectMotionFrames -= step;
        frames -= step;
        vec3_t ownerPosition;
        pet.Owner->MotionTrace.Sample(worldTime, (totalFrames - frames) / totalFrames,
                                      pet.Owner->Position, ownerPosition);
        pet.Position[2] += ownerPosition[2] - pet.AmbientSpeedNoise;
        pet.AmbientSpeedNoise = ownerPosition[2];
        pet.MotionTrace.Advance(step, pet.Position);
    }
    AngleMatrix(pet.Angle, pet.Matrix);
}

struct CharacterAfterImagePose final
{
    vec3_t position{};
    vec3_t startPosition{};
    vec3_t angle{};
    float animationFrame = 0.f;
    float alpha = 1.f;
};

class CPhysicsCloth;
class OBJECT;
class SessionKeeper;

// Owns the allocation count separately from the number of active pieces.
// A cape can disable its skirt without losing the allocation's destruction size.
struct CharacterClothVisual final
{
    enum class Kind
    {
        MagicSkeleton,
        Crust,
        Fred,
        CursedAllied,
        CursedIllusion,
        Halloween,
        GmHair,
        PhoenixHair,
        Cape,
        Equipment
    };
    CharacterClothVisual(SessionKeeper &keeper, Kind kind, std::size_t allocated,
                         bool mesh = false);
    ~CharacterClothVisual();
    CharacterClothVisual(const CharacterClothVisual &) = delete;
    CharacterClothVisual &operator=(const CharacterClothVisual &) = delete;

    std::unique_ptr<OBJECT> poseOwner;
    std::unique_ptr<vec34_t[]> poseBones;
    std::uint64_t poseRevision = 0;
    AnimationPoseSample poseSample;
    Kind kind;
    CPhysicsCloth *pieces;
    std::size_t count;
    bool hasSkirt = true;
    std::array<int, 5> shape{};
    float light[3]{1.f, 1.f, 1.f};
    std::vector<std::size_t> visiblePieces;

  private:
    std::size_t allocated_;
};

struct CharacterDarksideVisual
{
    static constexpr int DummyCount = 5; // Authored Darkside afterimages.
    std::array<std::optional<CDummyUnit>, DummyCount> dummies;
    std::uint64_t revision = 0;
    int dummyCount = 0;
    int attackCount = 0;
    float distanceFrame = 0.f;
    float otherAnimationFrame = 0.1f;
    bool shockwaveEmitted = false;
};

// An observer's item effect target and retained item pose. Neither object is a
// copy of CHARACTER; they carry only fields used by the effect consumers.
struct CharacterLinkedItemVisual final
{
    enum Slot
    {
        RightWeapon,
        LeftWeapon,
        Wing,
        Helper,
        Quest,
        RightPhoenix,
        LeftPhoenix,
        Statue,
        DragonHead,
        Princess,
        GradeHead
    };
    PART_t playback;
    AnimationPoseSample poseSample;
    bool linked = false;
    bool rightHand = false;
    vec3_t parentOffset{};
    float scaleOverride = 0.f;
    float scepterEmissionFrames = 0.f;
    double nextScepterRefreshMilliseconds = -1.0;
    explicit CharacterLinkedItemVisual(int type, int boneCount)
        : bones(std::make_unique<vec34_t[]>(boneCount))
    {
        item.Type = type;
        item.Live = target.Live = true;
        item.BoneTransform = bones.get();
    }

    OBJECT target;
    OBJECT item;
    std::unique_ptr<vec34_t[]> bones;
};

struct CharacterMountVisual final
{
    explicit CharacterMountVisual(int boneCount) : bones(std::make_unique<vec34_t[]>(boneCount))
    {
        object.BoneTransform = bones.get();
    }
    OBJECT object;
    std::unique_ptr<vec34_t[]> bones;
    std::uint64_t generation = 0;
    std::uint64_t poseRevision = 0;
    AnimationPoseSample poseSample_;
};

// State formerly mixed into source-only movement cosmetics.
struct CharacterMovementVisual final
{
    bool initialized = false;
    bool hasPose = false;
    int type = -1;
    vec3_t head{}, headTarget{}, light{}, sourceLight{};
    bool lightChanged = false;
    enum MaterialField : unsigned
    {
        Mesh = 1,
        Brightness = 2,
        U = 4,
        V = 8,
        HiddenMesh = 16,
        Shadow = 32
    };
    unsigned materialFields = 0;
    int blendMesh = -1, hiddenMesh = -1, animation = 0, subType = 0;
    float weaponLevel = 0.f;
    float blendLight = 1.f, blendU = 0.f, blendV = 0.f, lifeTime = 100.f;
    bool actionStarted = false, renderShadow = false;
    bool foot[2]{};
    float footFrame = 0.f;
    int footAction = -1;
    std::unique_ptr<vec34_t[]> bones;
    int boneCapacity = 0;
    AnimationPoseSample pose;
    void Apply(ObjectDrawInput &draw) const
    {
        if (!initialized)
            return;
        VectorCopy(head, draw.headAngle);
        if (hasPose && AnimationPoseSample(draw, pose.boneHead, pose.bodyHeight, pose.translated,
                                           pose.asset) == pose)
        {
            draw.bones = bones.get();
            draw.preparedPose = &pose;
            draw.stableBones = true;
        }
        if (lightChanged)
        {
            VectorCopy(light, draw.light);
        }
        if (materialFields & Mesh)
            draw.blendMesh = blendMesh;
        if (materialFields & Brightness)
            draw.blendLight = blendLight;
        if (materialFields & U)
            draw.blendU = blendU;
        if (materialFields & V)
            draw.blendV = blendV;
        if (materialFields & HiddenMesh)
            draw.hiddenMesh = hiddenMesh;
        if (materialFields & Shadow)
            draw.renderShadow = renderShadow;
    }
};

// Cursed Santa's death presentation belongs to each observer, not gameplay AI.
struct CharacterSantaVisual final
{
    vec3_t origin{};
    vec3_t position{};
    float angle = 0.f;
    int remainingBursts = 3;
    double elapsedFrames = 0.0;
    bool openingBurst = false;
    bool rightSide = false;
};

class OBJECT;

// Published lifetime anchor. Replacement/retirement clears the pointer once.

// One observing session's admission to one canonical lifetime.

// Authored indices used by appearance admission and its corresponding effect recipes.
// Names are resolved only when binding an external named attachment.
namespace CharacterSocket
{
inline constexpr int BERSERK_MOUTH = 9;
inline constexpr int BLADE_L_HAND = 12;
inline constexpr int BOX1 = 54;
inline constexpr int BOX2 = 55;
inline constexpr int Body_Bone1 = 61;
inline constexpr int Body_Bone10 = 70;
inline constexpr int Body_Bone11 = 43;
inline constexpr int Body_Bone12 = 44;
inline constexpr int Body_Bone13 = 63;
inline constexpr int Body_Bone2 = 62;
inline constexpr int Body_Bone3 = 54;
inline constexpr int Body_Bone4 = 55;
inline constexpr int Body_Bone5 = 21;
inline constexpr int Body_Bone6 = 22;
inline constexpr int Body_Bone7 = 25;
inline constexpr int Body_Bone8 = 26;
inline constexpr int Body_Bone9 = 71;
inline constexpr int Dreadfear_Eye52 = 9;
inline constexpr int Dreadfear_Eye54 = 10;
inline constexpr int Dreadfear_Wing32 = 71;
inline constexpr int Dreadfear_Wing34 = 68;
inline constexpr int Dreadfear_Wing51 = 50;
inline constexpr int Dreadfear_Wing53 = 47;
inline constexpr int Eye_Bone1 = 9;
inline constexpr int Eye_Bone2 = 10;
inline constexpr int GENO_WP = 47;
inline constexpr int GIANT_MAMUD_BIP_SPAIN_1 = 3;
inline constexpr int GIANT_MAMUD_BIP_SPAIN_2 = 4;
inline constexpr int GIANT_MAMUD_BIP_SPAIN_3 = 5;
inline constexpr int GIANT_MAMUD_BIP_TAIL = 45;
inline constexpr int GIANT_MAMUD_BIP_TAIL_1 = 6;
inline constexpr int GIANT_MAMUD_BIP_TAIL_2 = 7;
inline constexpr int IRON_RIDER_BIP01 = 2;
inline constexpr int IRON_RIDER_BOW_15 = 52;
inline constexpr int IRON_RIDER_BOW_16 = 47;
inline constexpr int IRON_RIDER_BOW_6 = 42;
inline constexpr int KANTURU2ND_ENTER_NPC_1 = 37;
inline constexpr int KANTURU2ND_ENTER_NPC_10 = 8;
inline constexpr int KANTURU2ND_ENTER_NPC_11 = 15;
inline constexpr int KANTURU2ND_ENTER_NPC_12 = 16;
inline constexpr int KANTURU2ND_ENTER_NPC_13 = 17;
inline constexpr int KANTURU2ND_ENTER_NPC_14 = 10;
inline constexpr int KANTURU2ND_ENTER_NPC_2 = 38;
inline constexpr int KANTURU2ND_ENTER_NPC_3 = 39;
inline constexpr int KANTURU2ND_ENTER_NPC_4 = 40;
inline constexpr int KANTURU2ND_ENTER_NPC_5 = 41;
inline constexpr int KANTURU2ND_ENTER_NPC_6 = 42;
inline constexpr int KANTURU2ND_ENTER_NPC_7 = 43;
inline constexpr int KANTURU2ND_ENTER_NPC_8 = 6;
inline constexpr int KANTURU2ND_ENTER_NPC_9 = 7;
inline constexpr int KENTAUROS_BIP_18 = 34;
inline constexpr int KENTAUROS_BIP_19 = 35;
inline constexpr int KENTAUROS_BIP_20 = 36;
inline constexpr int KENTAUROS_BIP_21 = 37;
inline constexpr int KENTAUROS_BIP_23 = 27;
inline constexpr int KENTAUROS_BIP_24 = 28;
inline constexpr int KENTAUROS_BIP_25 = 29;
inline constexpr int KENTAUROS_BIP_26 = 30;
inline constexpr int KENTAUROS_BIP_SPAIN_1 = 4;
inline constexpr int KENTAUROS_BIP_SPAIN_2 = 5;
inline constexpr int KENTAUROS_BIP_SPAIN_3 = 6;
inline constexpr int KENTAUROS_BIP_TAIL = 81;
inline constexpr int KENTAUROS_BIP_TAIL_1 = 82;
inline constexpr int KENTAUROS_BIP_TAIL_2 = 83;
inline constexpr int LHand_Bone = 14;
inline constexpr int L_Hand01 = 7;
inline constexpr int L_Hand02 = 13;
inline constexpr int L_Hand03 = 19;
inline constexpr int L_Hand04 = 25;
inline constexpr int L_Hand05 = 31;
inline constexpr int L_Hand11 = 11;
inline constexpr int L_Hand12 = 17;
inline constexpr int L_Hand13 = 23;
inline constexpr int L_Hand14 = 29;
inline constexpr int L_Hand15 = 5;
inline constexpr int L_Hand21 = 12;
inline constexpr int L_Hand22 = 18;
inline constexpr int L_Hand23 = 24;
inline constexpr int L_Hand24 = 30;
inline constexpr int L_Hand25 = 6;
inline constexpr int Left_Hand = 17;
inline constexpr int Monster100_Footstepst = 0;
inline constexpr int Monster100_Head = 57;
inline constexpr int Monster100_L_Hand = 76;
inline constexpr int Monster100_Pelvis = 2;
inline constexpr int Monster100_R_Hand = 94;
inline constexpr int Monster100_z02 = 107;
inline constexpr int Monster100_z03 = 108;
inline constexpr int Monster100_z04 = 109;
inline constexpr int Monster100_z05 = 110;
inline constexpr int Monster101_Head = 6;
inline constexpr int Monster101_L_Arm = 12;
inline constexpr int Monster101_R_Arm = 20;
inline constexpr int Monster102_Footstepst = 0;
inline constexpr int Monster102_Head = 6;
inline constexpr int Monster104_Footsteps = 1;
inline constexpr int Monster104_Horn0 = 37;
inline constexpr int Monster104_Horn1 = 38;
inline constexpr int Monster104_Horn2 = 39;
inline constexpr int Monster104_Horn3 = 40;
inline constexpr int Monster104_Horn4 = 44;
inline constexpr int Monster104_Horn5 = 45;
inline constexpr int Monster105_Footsteps = 1;
inline constexpr int Monster105_L_Arm00 = 33;
inline constexpr int Monster105_L_Arm01 = 34;
inline constexpr int Monster105_L_Arm02 = 35;
inline constexpr int Monster105_L_Eye = 10;
inline constexpr int Monster105_L_Hand = 20;
inline constexpr int Monster105_R_Eye = 9;
inline constexpr int Monster105_R_Hand = 39;
inline constexpr int Monster81_EyeLeft = 30;
inline constexpr int Monster81_EyeRight = 29;
inline constexpr int Monster82_Back = 46;
inline constexpr int Monster82_Eye = 48;
inline constexpr int Monster82_LHand = 34;
inline constexpr int Monster82_RHand = 45;
inline constexpr int Monster83_Tail = 62;
inline constexpr int Monster84_LeftHand = 37;
inline constexpr int Monster84_PoisonLeft = 55;
inline constexpr int Monster84_PoisonRight = 54;
inline constexpr int Monster84_PoisonTop = 53;
inline constexpr int Monster84_RightHand = 50;
inline constexpr int Monster85_LeftEye = 18;
inline constexpr int Monster85_RightEye = 19;
inline constexpr int Monster87_LeftEye = 8;
inline constexpr int Monster87_LeftHand = 16;
inline constexpr int Monster87_RightEye = 9;
inline constexpr int Monster94_zx = 27;
inline constexpr int Monster94_zx01 = 28;
inline constexpr int Monster95_Head = 6;
inline constexpr int Monster96_Bottom = 29;
inline constexpr int Monster96_Center = 28;
inline constexpr int Monster96_Top = 27;
inline constexpr int PRSona_A1 = 73;
inline constexpr int PRSona_Tail = 76;
inline constexpr int PRSona_Tail1 = 77;
inline constexpr int R_Hand01 = 59;
inline constexpr int R_Hand02 = 48;
inline constexpr int R_Hand03 = 11;
inline constexpr int R_Hand04 = 37;
inline constexpr int R_Hand05 = 26;
inline constexpr int R_Hand11 = 54;
inline constexpr int R_Hand12 = 43;
inline constexpr int R_Hand13 = 32;
inline constexpr int R_Hand14 = 6;
inline constexpr int R_Hand15 = 21;
inline constexpr int R_Hand21 = 5;
inline constexpr int R_Hand22 = 53;
inline constexpr int R_Hand23 = 42;
inline constexpr int R_Hand24 = 20;
inline constexpr int R_Hand25 = 31;
inline constexpr int Rabbit_1 = 3;
inline constexpr int Rabbit_2 = 16;
inline constexpr int Rabbit_3 = 15;
inline constexpr int Rabbit_4 = 2;
inline constexpr int SPL_WOLF_EYE_25 = 17;
inline constexpr int SPL_WOLF_EYE_26 = 16;
inline constexpr int Sword_Bone1 = 39;
inline constexpr int Sword_Bone2 = 40;
inline constexpr int Twintail_Hair24 = 16;
inline constexpr int Twintail_Hair32 = 24;
inline constexpr int Windmill_Bone1 = 47;
inline constexpr int node_blade01 = 71;
inline constexpr int node_blade02 = 74;
inline constexpr int node_blade04 = 73;
inline constexpr int node_blade05 = 72;
inline constexpr int node_eyes01 = 14;
inline constexpr int node_eyes02 = 15;
} // namespace CharacterSocket

class CHARACTER;

class CHARACTER;
class CSIPartsMDL;
class CSPetSystem;
class PetObject;
struct CharacterClothVisual;
struct CharacterDarksideVisual;

// Exact observer state. These latches never overwrite the shared character.
struct WorldCharacterVisualState final
{
    WorldCharacterVisualState();
    ~WorldCharacterVisualState();
    WorldCharacterVisualState(WorldCharacterVisualState &&) noexcept;
    WorldCharacterVisualState &operator=(WorldCharacterVisualState &&) noexcept;

    std::uint64_t consumedTick = 0;
    std::uint64_t generation = 0;
    std::uint64_t appearanceRevision = 0;
    std::uint64_t poseRevision = 0;
    std::uint64_t buffRevision = 0;
    bool passiveReconcilePending = true;
    int action = 0;
    float animationFrame = 0.f;
    float intervalStartFrame = 0.f;
    float priorAnimationFrame = 0.f;
    int priorAction = 0;
    int attackTime = 0;
    int priorAI = 0;
    int soundSubType = 0;
    float emissionLifeTime = 100.f;
    double emissionMilliseconds = 0.0;
    float warcraftEmissionFrames = 1.f;
    int alternateSide = 0;
    bool initialized = false;
    bool cosmeticVisible = false;
    bool deathEmitted = false;
    bool sandSmokeEmitted = false;
    bool summerDeathEmitted = false;
    bool newYearRewardChosen = false;
    bool rabbitDeathEmitted = false;
    double protectGuildMarkTime = 0.0;
    double lastCriticalDamageTime = 0.0;
    float extendedStateTicks = 0.f;
    CharacterEquipmentSet equipmentSet;
    CharacterMovementVisual movement;
    CharacterTargetBinding target;
    std::uint64_t targetRevision = 0;
    std::uint64_t partsAppearanceRevision = 0;
    std::uint64_t partsBuffRevision = 0;
    std::uint64_t partsPoseRevision = 0;
    int partsType = 0;
    int temporaryPartsType = -1;
    std::unique_ptr<CharacterClothVisual> bodyCloth;
    std::unique_ptr<CharacterClothVisual> capeCloth;
    std::unique_ptr<CharacterClothVisual> partCloth;
    std::shared_ptr<PetObject> helperPet;
    std::uint64_t helperPetGeneration = 0;
    std::uint64_t helperPetCommandRevision = 0;
    std::unique_ptr<CharacterMountVisual> mount;
    AnimationPoseSample localMountPose;
    std::unique_ptr<CSPetSystem> darkSpirit;
    std::uint64_t petGeneration = 0;
    std::uint64_t petCommandRevision = 0;
    std::uint64_t petAttackRevision = 0;
    std::unique_ptr<CSIPartsMDL> parts;
    std::unique_ptr<CSIPartsMDL> temporaryParts;
    std::unique_ptr<CharacterDarksideVisual> darkside;
    std::unique_ptr<CharacterSantaVisual> santa;
    std::vector<CharacterAfterImagePose> darksidePoses;
    std::map<int, std::unique_ptr<CharacterLinkedItemVisual>> linkedItems;
    unsigned int linkedItemSlots = 0;
    std::uint64_t linkedPoseRevision = 0;
    std::uint64_t linkedAppearanceRevision = 0;
    std::uint64_t linkedBuffRevision = 0;
    bool linkedOnBack = false;
    std::uint64_t equipmentAppearanceRevision = 0;
    std::unique_ptr<SessionSpriteStorage> sprites;
    std::unique_ptr<SessionSpriteStorage> attachmentSprites;

    void Reset() noexcept;
    void ResetCloth() noexcept;
    void BindAppearance(const CHARACTER &character) noexcept;
    void CaptureSample(const CHARACTER &character);
    bool CrossesAnimationWindow(float lower, float upper) const noexcept
    {
        return animationFrame > lower && (animationFrame <= upper || intervalStartFrame < upper);
    }
};

#ifndef __CSPARTS_H__
#define __CSPARTS_H__

class SessionKeeper;

class CSIPartsMDL : protected SessionLegacyCalls
{
  protected:
    OBJECT m_pObj;
    int m_iBoneNumber;
    vec3_t m_vOffset;
    std::unique_ptr<vec34_t[]> pose_;
    AnimationPoseSample poseSample_;
    bool PreparePose();

  public:
    explicit CSIPartsMDL(SessionKeeper &keeper) : SessionLegacyCalls(keeper), m_iBoneNumber(-1)
    {
    }
    virtual ~CSIPartsMDL() = default;
    virtual void IAdvance(CHARACTER *c, bool advanceAnimation = true) = 0;
    virtual void IRender(const CHARACTER *c) const = 0;
    inline OBJECT *GetObject()
    {
        return &m_pObj;
    }
    const OBJECT *GetObject() const
    {
        return &m_pObj;
    }
};

class CSParts : public CSIPartsMDL
{
  public:
    CSParts(SessionKeeper &keeper, int Type, int BoneNumber, bool bBillBoard = false, float x = 0.f,
            float y = 0.f, float z = 0.f, float ax = 0.f, float ay = 0.f, float az = 0.f);
    void IAdvance(CHARACTER *c, bool advanceAnimation = true) override;
    void IRender(const CHARACTER *c) const override;
};

class CSAnimationParts : public CSIPartsMDL
{
  public:
    CSAnimationParts(SessionKeeper &keeper, int Type, int BoneNumber, bool bBillBoard = false,
                     float x = 0.f, float y = 0.f, float z = 0.f, float ax = 0.f, float ay = 0.f,
                     float az = 0.f);
    void Animation(CHARACTER *c);
    void IAdvance(CHARACTER *c, bool advanceAnimation = true) override;
    void IRender(const CHARACTER *c) const override;
};

class CSParts2D : public CSIPartsMDL
{
    BYTE preparedSubType_ = 0;

  public:
    CSParts2D(SessionKeeper &keeper, int Type, int SubType, int BoneNumber, float x = 0.f,
              float y = 0.f, float z = 0.f);
    void IAdvance(CHARACTER *c, bool advanceAnimation = true) override;
    void IRender(const CHARACTER *c) const override;
};

#endif // __CSPARTS_H__

enum
{
    CHARACTER_NONE = 0,
    CHARACTER_RENDER_OBJ,
    CHARACTER_ANIMATION
};

enum
{
    RENDER_TYPE_NONE = 0,
    RENDER_TYPE_ALPHA_BLEND,
    RENDER_TYPE_ALPHA_TEST,
    RENDER_TYPE_ALPHA_BLEND_MINUS,
    RENDER_TYPE_ALPHA_BLEND_OTHER,
};

enum
{
    PET_FLYING = 0,
    PET_FLY,
    PET_ESCAPE,
    PET_STAND,
    PET_STAND_START,
    PET_ATTACK,
    PET_ATTACK_MAGIC,
    PET_END
};

enum PET_COMMAND
{
    PET_CMD_DEFAULT = 0,
    PET_CMD_RANDOM,
    PET_CMD_OWNER,
    PET_CMD_TARGET,
    PET_CMD_END
};

enum
{
    PARTS_LION = 4,
    PARTS_WEBZEN,
    PARTS_ATTACK_TEAM_MARK,
    PARTS_ATTACK_TEAM_MARK2,
    PARTS_ATTACK_TEAM_MARK3,
    PARTS_ATTACK_KING_TEAM_MARK,
    PARTS_ATTACK_KING_TEAM_MARK2,
    PARTS_ATTACK_KING_TEAM_MARK3,
    PARTS_DEFENSE_TEAM_MARK,
    PARTS_DEFENSE_KING_TEAM_MARK,
    PARTS_END
};

struct WorldCharacterVisualState;

void AdvanceDarkHorseSkill(OBJECT *o, BMD *b, bool emit = true);
void AdvanceSkillEarthQuake(CHARACTER *c, OBJECT *o, BMD *b, WorldCharacterVisualState &visual,
                            int iMaxSkill = 30);

void PartObjectColor(int Type, float Alpha, float Bright, vec3_t Light, bool ExtraMon = false);
void PartObjectColor2(int Type, float Alpha, float Bright, vec3_t Light, bool ExtraMon = false);

void BodyLight(OBJECT *o, BMD *b);

namespace CharacterPresentationDetail
{

void CaptureCharacterPoseInputs(CHARACTER &character);
void PublishCharacterPose(CHARACTER &character, BMD &model);
bool IsExpiredXmasCharacter(const OBJECT &object) noexcept;
bool CharacterUsesGroundShadow(const CHARACTER &character, bool skyTerrain);
void CollectHelperPetRetirement(WorldCharacterVisualState &visual, std::vector<OBJECT *> &targets,
                                std::vector<std::shared_ptr<PetObject>> &retired);
void CollectMountRetirement(WorldCharacterVisualState &visual, std::vector<OBJECT *> &targets,
                            std::vector<std::unique_ptr<CharacterMountVisual>> &retired);
void CollectPetRetirement(WorldCharacterVisualState &visual, std::vector<OBJECT *> &targets,
                          std::vector<std::unique_ptr<CSPetSystem>> &retired);
void CollectPartsRetirement(WorldCharacterVisualState &visual, std::vector<OBJECT *> &targets,
                            std::vector<std::unique_ptr<CSIPartsMDL>> &retired);
void CollectLinkedItemRetirement(WorldCharacterVisualState &visual, std::vector<OBJECT *> &targets,
                                 std::vector<std::unique_ptr<CharacterLinkedItemVisual>> &retired);
void RetireClothAndAfterImages(WorldCharacterVisualState &visual);
} // namespace CharacterPresentationDetail

namespace MonkPresentationDetail
{

CharacterAfterImagePose CaptureDarksidePose(const OBJECT &object);
}

namespace CharacterPartsDetail
{

#pragma pack(push)
#pragma pack()
inline constexpr float kRenderableAlphaThreshold = 0.01f;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kDefaultBoneIndex = 20;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int kDefaultLifetimeTicks = 30;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr float kDefaultVelocity = 0.5f;
#pragma pack(pop)

} // namespace CharacterPartsDetail

namespace CharacterMotionDetail
{
struct BoidDrawingHeading final
{
    OBJECT &object;
    const float heading;
    explicit BoidDrawingHeading(OBJECT &value) : object(value), heading(value.Angle[2])
    {
        object.Angle[2] += 90.f;
    }
    ~BoidDrawingHeading()
    {
        object.Angle[2] = heading;
    }
};
} // namespace CharacterMotionDetail

namespace CharacterMotionDetail
{

#pragma pack(push)
#pragma pack()
inline const BYTE BOID_FLY = 0;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const BYTE BOID_DOWN = 1;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const BYTE BOID_GROUND = 2;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline const BYTE BOID_UP = 3;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
template <class Step> void AdvanceAmbientIntervals(OBJECT &object, float frames, Step &&advance)
{
    while (frames > 0.f)
    {
        const bool refresh = object.AmbientNoiseFrames <= 0.f;
        if (refresh)
            object.AmbientNoiseFrames = 1.f;
        const float step = Core::Time::ReferenceStep(frames, object.AmbientNoiseFrames);
        advance(step, refresh, frames - step);
        object.AmbientNoiseFrames -= step;
        frames -= step;
        if (!object.Live)
            break;
    }
}
#pragma pack(pop)

} // namespace CharacterMotionDetail
