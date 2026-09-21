#include "domain/CharacterPresentation.h"
#include "session/SessionPresentation.h"
#include "support/CoreMath.h"
#include "session/SessionKeeper.h"
#include "session/SessionGameplay.h"
#include "render/Character.h"
#include "render/ModelGeometry.h"
#include "render/ModelResources.h"
#include "domain/EffectsUpdate.h"
#include "app/ApplicationDiagnostics.h"
#include "domain/CharacterSystem.h"
#include "render/FrameTape.h"
#include "domain/MapSimulation.h"
#include "domain/WorldSimulation.h"
#include "render/World.h"
#include "domain/WorldPhysics.h"
#include "domain/ItemsSkills.h"
#include "ui/features/Hud/HudLogic.h"
#include "session/SessionWorkspace.h"
#include "session/SessionRender.h"
#include "app/ApplicationLoopFrame.h"
#include "data/WorldData.h"
#include "render/Terrain.h"
#include "domain/MovementAI.h"
#include "support/Camera.h"
#include "domain/Events.h"
#include "domain/Guild.h"
#include "app/ApplicationNetwork.h"
#include "ui/session/UiSessionLogic.h"
#include "render/Textures.h"
#include "app/ApplicationAudio.h"
#include "session/SessionNetwork.h"
#include "session/SessionUi.h"
#include "ui/features/Social/SocialLogic.h"
#include "I18N/All.h"
#include "domain/ChatSocial.h"
#include "data/CharacterData.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "domain/Quests.h"
#include "session/SessionAudio.h"
#include "data/Localization.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "render/Text.h"
#include "support/Scenes.h"
#include "ui/runtime/UiControls.h"
#include "app/AppWindow.h"
#include "domain/Shop.h"
#include "ui/features/World/WorldLogic.h"
#include "ui/features/Activities/ActivitiesLogic.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "domain/Automation.h"

void SessionVisualUnit::AdvanceEquipmentSetEnergy(CHARACTER &character, BMD &model)
{
    auto &object = character.Object;
    auto draw = CharacterPresentationInput(character).object;
    AnimationPoseSample pose(draw, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    const float phase = std::sin(float(WorldTime * 0.002));
    for (int side = 0; side < 2; ++side)
    {
        vec3_t offset{13.f, 10.f, side == 0 ? 3.f : -3.f}, position, light{0.2f, 0.4f, 0.8f};
        model.TransformByObjectBone(position, draw, 20, offset);
        CreateSprite(BITMAP_SHINY + 6, position, 0.5f * phase, light, &object);
        Vector(0.1f, 0.15f, 1.f, light);
        CreateSprite(BITMAP_PIN_LIGHT, position, 1.3f * phase + 0.5f, light, &object,
                     side == 0 ? 100.f : 80.f);
    }
    std::array<vec34_t, MAX_BONES> bones;
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
    {
        object.MotionTrace.Sample(WorldTime, birth.FrameFraction(), object.Position, draw.position);
        draw.bones =
            pose.EvaluateAtTime(model, object, WorldTime, birth.FrameFraction(), bones.data());
        for (int side = 0; side < 2; ++side)
        {
            vec3_t offset{13.f, 10.f, side == 0 ? 3.f : -3.f}, position, light{0.09f, 0.09f, 0.8f};
            model.TransformByObjectBone(position, draw, 20, offset);
            CreateJoint(BITMAP_JOINT_ENERGY, position, draw.position, draw.angle, 55 + side,
                        &object, 6.f, -1, 0, 0, -1, light);
        }
    }
}

void SessionVisualUnit::AdvanceLinkedItemPlayback(CHARACTER &character,
                                                  CharacterLinkedItemVisual &entry, BMD &model,
                                                  bool advance, int slot, bool link)
{
    auto &sample = entry.playback;
    auto &item = entry.item;
    const bool phoenix = slot == CharacterLinkedItemVisual::RightPhoenix ||
                         slot == CharacterLinkedItemVisual::LeftPhoenix;
    if (!advance)
    {
        if (phoenix)
            sample.AnimationFrame = std::min(sample.AnimationFrame, 2.f);
        return;
    }
    item.MotionTrace.Begin(WorldTime, FPS_ANIMATION_FACTOR, item.Position);
    const ObjectMotionTrace::AnimationPhase phase{sample.AnimationFrame, sample.PriorAnimationFrame,
                                                  sample.CurrentAction, sample.PriorAction};
    const bool enabled =
        sample.PlaySpeed != 0.f &&
        (!link || item.Type < MODEL_BOW || item.Type >= MODEL_BOW + MAX_ITEM_INDEX ||
         item.Type == MODEL_STINGER_BOW) &&
        !g_isCharacterBuff(&character.Object, eDeBuff_Stun) &&
        !g_isCharacterBuff(&character.Object, eDeBuff_Sleep);
    const float speed = enabled ? sample.PlaySpeed : 0.f;
    const float moving =
        phoenix && speed > 0.f
            ? std::min(FPS_ANIMATION_FACTOR, std::max(0.f, (2.f - sample.AnimationFrame) / speed))
            : FPS_ANIMATION_FACTOR;
    item.MotionTrace.AdvanceAnimation(moving, phase, speed * moving);
    if (enabled && moving > 0.f)
        model.PlayAnimation(&sample.AnimationFrame, &sample.PriorAnimationFrame,
                            &sample.PriorAction, speed * moving, item.Position, item.Angle, 1.f);
    if (phoenix)
        sample.AnimationFrame = std::min(sample.AnimationFrame, 2.f);
    if (moving < FPS_ANIMATION_FACTOR)
    {
        const ObjectMotionTrace::AnimationPhase stopped{sample.AnimationFrame,
                                                        sample.PriorAnimationFrame,
                                                        sample.CurrentAction, sample.PriorAction};
        item.MotionTrace.AdvanceAnimation(FPS_ANIMATION_FACTOR - moving, stopped, 0.f);
    }
}

void SessionVisualUnit::AdvanceMountEmissions(OBJECT &object, bool moving)
{
    if (!object.Visible || object.Alpha < 0.01f)
        return;
    const bool ownerVisible = object.Owner && !g_isCharacterBuff(object.Owner, eBuff_Cloaking);
    const auto &policy = TheMapProcess().CharacterPolicy();
    float dustRate = 0.f;
    switch (object.Type)
    {
    case MODEL_FENRIR_BLACK:
    case MODEL_FENRIR_BLUE:
    case MODEL_FENRIR_RED:
    case MODEL_FENRIR_GOLD:
        if (!gMapManager.InHellas())
            dustRate = 1.f / 3.f;
        break;
    case MODEL_DARK_HORSE:
        if (!gMapManager.InHellas())
            dustRate = 0.5f;
        if (ownerVisible)
        {
            auto &model = Models[object.Type];
            AnimationPoseSample pose(&object, model.BoneHead, model.BodyHeight, false,
                                     model.PoseAssetIdentity());
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
            {
                vec3_t offset{50.f, -4.f, 0.f}, position;
                const float fraction = birth.FrameFraction();
                pose.SampleBonePosition(model, object, 27, offset, WorldTime, fraction, position);
                CreateParticle(TheMapProcess().Presentation().waterFish ? BITMAP_BUBBLE
                                                                        : BITMAP_SMOKE,
                               position, object.Angle, object.Light);
            }
        }
        break;
    case MODEL_PEGASUS:
    case MODEL_UNICON:
        dustRate = 0.5f;
        break;
    case MODEL_BUTTERFLY01:
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
        {
            vec3_t position, light{0.4f, 0.6f, 1.f};
            const float fraction = birth.FrameFraction();
            object.MotionTrace.Sample(WorldTime, fraction, object.Position, position);
            CreateParticle(BITMAP_SMOKE, position, object.Angle, light, 1);
        }
        break;
    case MODEL_HELPER:
        if (!ownerVisible)
            break;
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            vec3_t origin, light{0.4f, 0.4f, 0.4f};
            const float fraction = birth.FrameFraction();
            object.MotionTrace.Sample(WorldTime, fraction, object.Position, origin);
            for (int i = 0; i < 4; ++i)
            {
                vec3_t position;
                for (int axis = 0; axis < 3; ++axis)
                    position[axis] = origin[axis] + WorldRandom() % 16 - 8;
                CreateParticle(BITMAP_SPARK, position, object.Angle, light, 1);
            }
        }
        break;
    }
    if (!moving || !ownerVisible || dustRate == 0.f || policy.skyTerrain ||
        TheMapProcess().TerrainCutscene())
        return;
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR * dustRate))
    {
        vec3_t position, light{1.f, 1.f, 1.f};
        const float fraction = birth.FrameFraction();
        object.MotionTrace.Sample(WorldTime, fraction, object.Position, position);
        position[0] += WorldRandom() % 64 - 32;
        position[1] += WorldRandom() % 64 - 32;
        position[2] += WorldRandom() % 32 - 16;
        CreateParticle(policy.snowFootsteps ? BITMAP_SMOKE : BITMAP_SMOKE + 1, position,
                       object.Angle, light);
    }
}

void SessionVisualUnit::EmitQueenRainerLightning(OBJECT &object, BMD &model,
                                                 const AnimationPoseSample &pose)
{
    constexpr std::pair<int, int> links[]{{2, 3},   {3, 4},   {4, 5},   {2, 10},  {10, 11},
                                          {2, 18},  {18, 22}, {22, 23}, {23, 24}, {24, 25},
                                          {18, 31}, {31, 32}, {32, 33}, {33, 34}};
    std::array<vec34_t, MAX_BONES> bones;
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
    {
        ObjectDrawInput draw(&object);
        object.MotionTrace.Sample(WorldTime, birth.FrameFraction(), object.Position, draw.position);
        draw.bones =
            pose.EvaluateAtTime(model, object, WorldTime, birth.FrameFraction(), bones.data());
        for (const auto [firstBone, secondBone] : links)
        {
            vec3_t first, second;
            model.TransformByObjectBone(first, draw, firstBone);
            model.TransformByObjectBone(second, draw, secondBone);
            CreateJoint(BITMAP_JOINT_THUNDER, first, second, object.Angle, 7, nullptr, 14.f);
        }
    }
}

CharacterClothVisual::CharacterClothVisual(SessionKeeper &keeper, Kind kind, std::size_t allocated,
                                           bool mesh)
    : kind(kind), pieces(mesh ? CPhysicsClothMesh::AllocateSingle(keeper)
                              : CPhysicsCloth::AllocateArray(keeper, allocated)),
      count(allocated), allocated_(allocated)
{
}

CharacterClothVisual::~CharacterClothVisual()
{
    CPhysicsCloth::DestroyArray(pieces, allocated_);
}

WorldCharacterVisualState::WorldCharacterVisualState() = default;
WorldCharacterVisualState::~WorldCharacterVisualState() = default;
WorldCharacterVisualState::WorldCharacterVisualState(WorldCharacterVisualState &&) noexcept =
    default;
WorldCharacterVisualState &WorldCharacterVisualState::operator=(
    WorldCharacterVisualState &&) noexcept = default;

void WorldCharacterVisualState::ResetCloth() noexcept
{
    bodyCloth.reset();
    capeCloth.reset();
    partCloth.reset();
}

void WorldCharacterVisualState::Reset() noexcept
{
    auto retainedSprites = std::move(sprites);
    auto retainedAttachmentSprites = std::move(attachmentSprites);
    auto retainedDarksidePoses = std::move(darksidePoses);
    *this = WorldCharacterVisualState{};
    sprites = std::move(retainedSprites);
    attachmentSprites = std::move(retainedAttachmentSprites);
    darksidePoses = std::move(retainedDarksidePoses);
    darksidePoses.clear();
    if (sprites)
        sprites->Clear();
    if (attachmentSprites)
        attachmentSprites->Clear();
}

void WorldCharacterVisualState::BindAppearance(const CHARACTER &character) noexcept
{
    if (generation != character.WorldVisualGeneration)
    {
        Reset();
        generation = character.WorldVisualGeneration;
        appearanceRevision = character.WorldVisualAppearanceRevision;
        return;
    }
    if (appearanceRevision == character.WorldVisualAppearanceRevision)
        return;
    appearanceRevision = character.WorldVisualAppearanceRevision;
    if (character.Object.Type != MODEL_CURSED_SANTA)
        santa.reset();
    // A changed skin must not retain decoration from the old appearance. Event
    // cursors and death latches survive equipment changes within this lifetime.
    if (sprites)
        sprites->Clear();
    if (attachmentSprites)
        attachmentSprites->Clear();
}

void WorldCharacterVisualState::CaptureSample(const CHARACTER &character)
{
    if (!initialized)
    {
        soundSubType = character.Object.SubType;
        emissionLifeTime = character.WorldVisualPriorLifeTime;
        initialized = true;
    }
    consumedTick = character.WorldVisualTick;
    darksidePoses.clear();
    poseRevision = character.WorldVisualPoseRevision;
    intervalStartFrame = action == character.WorldVisualAction &&
                                 animationFrame <= character.WorldVisualAnimationFrame
                             ? animationFrame
                             : 0.f;
    if (character.WorldVisualAction != PLAYER_SKILL_DARKSIDE_READY &&
        character.WorldVisualAction != PLAYER_SKILL_DARKSIDE_ATTACK)
        darkside.reset();
    action = character.WorldVisualAction;
    animationFrame = character.WorldVisualAnimationFrame;
    priorAnimationFrame = character.WorldVisualPriorAnimationFrame;
    priorAction = character.WorldVisualPriorAction;
    attackTime = character.WorldVisualAttackTime;
    priorAI = character.WorldVisualPriorAI;
}

void CHARACTER::ResetPresentationIdentity()
{
    ++WorldVisualGeneration;
    WorldVisualPoseRevision = 0;
    WorldVisualPoseSample = {};
    TargetBinding = {};
    MarkAppearanceChanged();
    NamedBones.clear();
    if (SocketSource)
        SocketSource->object = nullptr;
    SocketSource = std::make_shared<CharacterSocketSource>(&Object);
    PetCommands = {};
    HelperPetState = {};
    MountState = {};
    WorldVisualMountBurst = false;
    WorldVisualSnowmanDeath = false;
    WorldVisualStructureDeath = false;
    Darkside = {};
    WorldVisualTick = 0;
    WorldVisualAnimationFactor = 1.f;
    WorldVisualHalloweenProgress = 0.f;
    WorldVisualHalloweenBurst = false;
    WorldVisualAppearing = false;
    WorldVisualHalloweenSparks = 0;
    WorldVisualPriorLifeTime = 100.f;
    WorldVisualAction = WorldVisualPriorAction = WorldVisualAttackTime = WorldVisualPriorAI = 0;
    WorldVisualAnimationFrame = WorldVisualPriorAnimationFrame = 0.f;
    WorldVisualAttackFrameTime = -1.0;
    WorldVisualAttackStart = WorldVisualAttackFrames = 0.f;
    WorldVisualAttackAction = 0;
}

void CHARACTER::MarkAppearanceChanged() noexcept
{
    ++WorldVisualAppearanceRevision;
}

void PetObject::PreparePresentation(bool forceRender)
{
    if (!m_obj || !m_obj->Live)
        return;
    auto &model = Models[m_obj->Type];
    model.BodyScale = m_obj->Scale;
    model.BodyHeight = 0.f;
    model.CurrentAction = m_obj->CurrentAction;
    VectorCopy(m_obj->Position, model.BodyOrigin);
    AnimationPoseSample sample(m_obj, model.BoneHead, 0.f, true, model.PoseAssetIdentity());
    if (poseSample_ != sample)
    {
        sample.Evaluate(model, m_obj->BoneTransform);
        poseSample_ = sample;
    }
    m_obj->Visible = forceRender || (sessionKeeper_.Display()->IsVisible() &&
                                     TestFrustrum2D(m_obj->Position[0] * 0.01f,
                                                    m_obj->Position[1] * 0.01f, -20.f));
}

void SessionVisualUnit::AdvanceCharacterHelperPet(CHARACTER &character,
                                                  WorldCharacterVisualState &visual, bool advance,
                                                  bool forceRender)
{
    const auto &state = character.HelperPetState;
    if (visual.helperPetGeneration != state.generation)
    {
        visual.helperPet.reset();
        if (!advance && visual.attachmentSprites)
            visual.attachmentSprites->Clear();
        visual.helperPetGeneration = state.generation;
        visual.helperPetCommandRevision = 0;
    }
    if (state.itemType == -1)
        return;
    SessionRandom::PresentationScope presentation(sessionKeeper_.RandomForConstruction());
    const bool admitted = !visual.helperPet;
    if (admitted)
        visual.helperPet = g_petProcess.CreatePetObject(character);
    if (!visual.helperPet)
        return;
    if (visual.helperPetCommandRevision != state.commandRevision)
    {
        visual.helperPet->SetCommand(state.targetKey,
                                     static_cast<PetObject::ActionType>(state.action));
        visual.helperPetCommandRevision = state.commandRevision;
    }
    if (advance)
        visual.helperPet->Update(forceRender);
    else if (admitted && forceRender)
        visual.helperPet->PreparePresentation(true);
}

void SessionVisualUnit::UpdateItemObjectMaterial(OBJECT &object)
{
    ObjectDrawInput draw(&object);
    UpdateItemDrawMaterial(draw);
    object.SetBlendMesh(draw.blendMesh);
    object.SetHiddenMesh(draw.hiddenMesh);
    object.BlendMeshLight = draw.blendLight;
    object.BlendMeshTexCoordU = draw.blendU;
    object.BlendMeshTexCoordV = draw.blendV;
    object.MaterialJitterU = draw.materialJitterU;
    object.MaterialJitterV = draw.materialJitterV;
    object.Scale = draw.scale;
}

void SessionVisualUnit::UpdateItemDrawMaterial(ObjectDrawInput &draw)
{
    switch (draw.type)
    {
    case MODEL_SAINT_CROSSBOW:
        draw.blendMesh = -2;
        draw.blendLight = sinf(WorldTime * 0.004f) * 0.2f + 0.9f;
        break;
    case MODEL_STAFF_OF_DESTRUCTION:
        draw.blendMesh = -2;
        draw.blendLight = sinf(WorldTime * 0.004f) * 0.2f + 0.9f;
        //draw.blendU = (float)((int)(WorldTime)%2000)*0.0005f;
        break;
    case MODEL_CHAOS_LIGHTNING_STAFF:
        draw.blendMesh = 1;
        draw.blendLight = (float)(WorldRandom() % 11) * 0.1f;
        break;
    case MODEL_STAFF_OF_RESURRECTION:
        draw.blendMesh = -2;
        draw.blendLight = sinf(WorldTime * 0.004f) * 0.3f + 0.7f;
        break;
    case MODEL_CHAOS_NATURE_BOW:
    case MODEL_BLUEWING_CROSSBOW:
    case MODEL_AQUAGOLD_CROSSBOW:
        draw.blendMesh = -2;
        draw.blendLight = sinf(WorldTime * 0.004f) * 0.3f + 0.7f;
        break;
    case MODEL_RED_SPIRIT_PANTS:
    case MODEL_RED_SPIRIT_HELM:
        draw.blendLight = sinf(WorldTime * 0.001f) * 0.4f + 0.6f;
        break;
    case MODEL_STAFF_OF_KUNDUN:
        draw.blendMesh = 2;
        draw.blendLight = sinf(WorldTime * 0.004f) * 0.3f + 0.7f;
        break;
    case MODEL_CRYSTAL_MORNING_STAR:
        draw.blendMesh = 1;
        draw.blendLight = sinf(WorldTime * 0.004f) * 0.2f + 0.8f;
        break;
    case MODEL_CRYSTAL_SWORD:
        draw.blendMesh = 0;
        break;
    case MODEL_CHAOS_DRAGON_AXE:
        draw.blendMesh = 1;
        draw.blendLight = sinf(WorldTime * 0.004f) * 0.3f + 0.7f;
        break;
    case MODEL_BILL_OF_BALROG:
        draw.blendV = -(float)((int)(WorldTime) % 2000) * 0.0005f;
        break;
    case MODEL_LEGENDARY_SHIELD:
        draw.blendMesh = 1;
        draw.blendU = (float)(WorldRandom() % 10) * 0.1f;
        draw.blendV = (float)(WorldRandom() % 10) * 0.1f;
        break;
    case MODEL_ELEMENTAL_SHIELD:
        draw.hiddenMesh = 2;
        [[fallthrough]];
    case MODEL_GRAND_SOUL_SHIELD:
        draw.materialJitterU = static_cast<float>(WorldRandom() % 10) * 0.1f;
        draw.materialJitterV = static_cast<float>(WorldRandom() % 10) * 0.1f;
        break;
    case MODEL_ELEMENTAL_MACE:
        draw.hiddenMesh = 2;
        break;
    case MODEL_RUNE_BLADE:
        draw.hiddenMesh = 2;
        break;
    case MODEL_DRAGON_SPEAR:
        draw.hiddenMesh = 1;
        break;
    case MODEL_DRAGON_SOUL_STAFF:
        draw.blendMesh = 1;
        break;
    case MODEL_LEGENDARY_STAFF:
        draw.blendMesh = 2;
        draw.blendU = (float)(WorldRandom() % 10) * 0.1f;
        draw.blendV = (float)(WorldRandom() % 10) * 0.1f;
        break;
    case MODEL_ETC:
        draw.scale = 0.7f;
        break;
    case MODEL_SMALL_HEALING_POTION:
        draw.scale = 1.f;
        break;
    case MODEL_POTION + 21:
        draw.scale = 0.5f;
        break;
    case MODEL_LIGHTING_SWORD:
        draw.blendMesh = 1;
        draw.blendU = (float)(WorldRandom() % 10) * 0.1f;
        draw.blendLight = sinf(WorldTime * 0.004f) * 0.3f + 0.7f;
        break;
    case MODEL_LIGHT_SABER:
    case MODEL_SPEAR:
        draw.blendMesh = 1;
        draw.blendLight = sinf(WorldTime * 0.004f) * 0.3f + 0.7f;
        break;
    case MODEL_BLADE:
    case MODEL_DOUBLE_BLADE:
        draw.blendMesh = 1;
        break;
    case MODEL_STAFF:
        draw.blendMesh = 2;
        break;
    case MODEL_HELPER:
        draw.blendMesh = 1;
        break;
    case MODEL_WINGS_OF_SPIRITS:
        draw.scale = 0.5f;
        draw.blendMesh = 0;
        break;
    case MODEL_WINGS_OF_SOUL:
    case MODEL_WINGS_OF_DRAGON:
    case MODEL_WINGS_OF_DARKNESS:
        break;
    case MODEL_WING:
    case MODEL_ORB_OF_HEALING:
    case MODEL_ORB_OF_GREATER_DEFENSE:
    case MODEL_ORB_OF_GREATER_DAMAGE:
    case MODEL_ORB_OF_SUMMONING:
    case MODEL_WING + 20:
    case MODEL_WING + 132:
        draw.blendMesh = 0;
        break;
    case MODEL_SERPENT_SHIELD:
    case MODEL_BRONZE_SHIELD:
    case MODEL_DRAGON_SHIELD:
        draw.blendMesh = 1;
        draw.blendLight = sinf(WorldTime * 0.004f) * 0.3f + 0.7f;
        break;
    }
}

ObjectDrawInput SessionVisualUnit::PrepareDroppedItemDraw(const OBJECT &object)
{
    ObjectDrawInput draw(&object);
    if (cameraManager_.GetCurrentMode() != CameraMode::Orbital)
        return draw;
    float groundHeight = RequestTerrainHeight(object.Position[0], object.Position[1]) + 30.f;
    if (object.Type >= MODEL_SWORD && object.Type < MODEL_STAFF + MAX_ITEM_INDEX)
        groundHeight += 40.f;
    if (object.Position[2] <= groundHeight)
        draw.angle[2] +=
            static_cast<OrbitalCamera *>(cameraManager_.GetActiveCamera())->GetTotalYaw();
    return draw;
}

bool SessionVisualUnit::CharacterCapeAppearance(CHARACTER &character, vec3_t CloakLight)
{
    auto *c = &character;
    auto *o = &character.Object;
    bool bCloak = false;

    if ((c->Class == CLASS_DARK || gCharacterManager.GetBaseClass(c->Class) == CLASS_DARK_LORD ||
         gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER) &&
        o->Type == MODEL_PLAYER)
    {
        if (c->Change == false || (c->Change == true && c->Object.Type == MODEL_PLAYER))
        {
            bCloak = true;
        }
    }

    if (c->MonsterIndex == MONSTER_DEATH_KING || c->MonsterIndex == MONSTER_NIGHTMARE)
    {
        bCloak = true;
    }

    if (c->MonsterIndex >= MONSTER_TERRIBLE_BUTCHER && c->MonsterIndex <= MONSTER_DOPPELGANGER_SUM)
    {
        bCloak = false;
    }

    if (gMapManager.InChaosCastle() == true)
    {
        bCloak = false;
    }

    Vector(1.f, 1.f, 1.f, CloakLight);
    if (c->GuildMarkIndex != -1)
    {
        if (EnableSoccer)
        {
            if (wcscmp(GuildMark[Hero->GuildMarkIndex].GuildName,
                       GuildMark[c->GuildMarkIndex].GuildName) == 0)
            {
                bCloak = true;
                if (HeroSoccerTeam == 0)
                {
                    Vector(1.f, 0.2f, 0.f, CloakLight);
                }
                else
                {
                    Vector(0.f, 0.2f, 1.f, CloakLight);
                }
            }
            if (wcscmp(GuildWarName, GuildMark[c->GuildMarkIndex].GuildName) == 0)
            {
                bCloak = true;
                if (HeroSoccerTeam == 0)
                {
                    Vector(0.f, 0.2f, 1.f, CloakLight);
                }
                else
                {
                    Vector(1.f, 0.2f, 0.f, CloakLight);
                }
            }
        }
        if (SoccerObserver)
        {
            if (wcscmp(SoccerTeamName[0], GuildMark[c->GuildMarkIndex].GuildName) == 0)
            {
                bCloak = true;
                Vector(1.f, 0.2f, 0.f, CloakLight);
            }
            if (wcscmp(SoccerTeamName[1], GuildMark[c->GuildMarkIndex].GuildName) == 0)
            {
                bCloak = true;
                Vector(0.f, 0.2f, 1.f, CloakLight);
            }
        }
    }

    if (g_DuelMgr.IsDuelEnabled())
    {
        if (g_DuelMgr.IsDuelPlayer(c, DUEL_ENEMY, FALSE))
        {
            bCloak = true;
            Vector(1.f, 0.2f, 0.f, CloakLight);
        }
        else if (g_DuelMgr.IsDuelPlayer(c, DUEL_HERO, FALSE))
        {
            bCloak = true;
            Vector(0.f, 0.2f, 1.f, CloakLight);
        }
    }

    return bCloak;
}

void SessionVisualUnit::CreateCharacterCape(CHARACTER &character, WorldCharacterVisualState &visual)
{
    auto *c = &character;
    auto *o = &character.Object;
    if (gCharacterManager.GetBaseClass(c->Class) == CLASS_DARK_LORD)
    {
        int numCloth = 4;
        if (c->Wing.Type == MODEL_CAPE_OF_EMPEROR)
        {
            numCloth = 6;
        }
        else
        {
            numCloth = 4;
        }

        visual.capeCloth = std::make_unique<CharacterClothVisual>(
            sessionKeeper_, CharacterClothVisual::Kind::Cape, numCloth);
        auto *pCloth = visual.capeCloth->pieces;
        const int armor = c->BodyPart[BODYPART_ARMOR].Type;
        visual.capeCloth->hasSkirt =
            armor == static_cast<int>(MODEL_BODY_ARMOR) + SKIN_CLASS_DARK_LORD ||
            armor == static_cast<int>(MODEL_BODY_ARMOR) + SKIN_CLASS_LORDEMPEROR;

        pCloth[0].Create(o, 20, 0.0f, 0.0f, 20.0f, 6, 5, 30.0f, 70.0f, BITMAP_ROBE + 6,
                         BITMAP_ROBE + 6,
                         PCT_CURVED | PCT_RUBBER2 | PCT_MASK_LIGHT | PLS_STRICTDISTANCE |
                             PCT_SHORT_SHOULDER | PCT_NORMAL_THICKNESS | PCT_OPT_HAIR);
        pCloth[0].SetWindMinMax(10, 50);
        pCloth[0].AddCollisionSphere(-10.f, 20.0f, 20.0f, 27.0f, 17);
        pCloth[0].AddCollisionSphere(10.f, 20.0f, 20.0f, 27.0f, 17);

        pCloth[1].Create(o, 20, 0.0f, 5.0f, 18.0f, 5, 5, 30.0f, 70.0f, BITMAP_ROBE + 6,
                         BITMAP_ROBE + 6,
                         PCT_CURVED | PCT_RUBBER2 | PCT_MASK_BLEND | PLS_STRICTDISTANCE |
                             PCT_SHORT_SHOULDER | PCT_NORMAL_THICKNESS | PCT_OPT_HAIR);
        pCloth[1].SetWindMinMax(8, 40);
        pCloth[1].AddCollisionSphere(-10.f, 20.0f, 20.0f, 27.0f, 17);
        pCloth[1].AddCollisionSphere(10.f, 20.0f, 20.0f, 27.0f, 17);

        if (c->Wing.Type == MODEL_CAPE_OF_EMPEROR)
        {
            pCloth[2].Create(o, 19, 0.0f, 8.0f, 10.0f, 10, 10, 180.0f, 180.0f, BITMAP_ROBE + 9,
                             BITMAP_ROBE + 9,
                             PCT_CURVED | PCT_SHORT_SHOULDER | PCT_HEAVY | PCT_MASK_ALPHA);
            pCloth[2].AddCollisionSphere(-10.f, -10.0f, -10.0f, 25.0f, 17);
            pCloth[2].AddCollisionSphere(10.f, -10.0f, -10.0f, 25.0f, 17);
            pCloth[2].AddCollisionSphere(-10.f, -10.0f, 20.0f, 27.0f, 17);
            pCloth[2].AddCollisionSphere(10.f, -10.0f, 20.0f, 27.0f, 17);
        }
        else if (c->Wing.Type == MODEL_WING + 130)
        {
            pCloth[2].Create(o, 19, 0.0f, 8.0f, 10.0f, 10, 10, 100.0f, 100.0f, BITMAP_ROBE + 7,
                             BITMAP_ROBE + 7, PCT_CURVED | PCT_SHORT_SHOULDER | PCT_MASK_ALPHA);
            pCloth[2].AddCollisionSphere(-10.f, -10.0f, -10.0f, 25.0f, 17);
            pCloth[2].AddCollisionSphere(10.f, -10.0f, -10.0f, 25.0f, 17);
            pCloth[2].AddCollisionSphere(-10.f, -10.0f, 20.0f, 27.0f, 17);
            pCloth[2].AddCollisionSphere(10.f, -10.0f, 20.0f, 27.0f, 17);
        }
        else
        {
            pCloth[2].Create(o, 19, 0.0f, 8.0f, 10.0f, 10, 10, 180.0f, 180.0f, BITMAP_ROBE + 7,
                             BITMAP_ROBE + 7, PCT_CURVED | PCT_SHORT_SHOULDER | PCT_MASK_ALPHA);
            pCloth[2].AddCollisionSphere(-10.f, -10.0f, -10.0f, 25.0f, 17);
            pCloth[2].AddCollisionSphere(10.f, -10.0f, -10.0f, 25.0f, 17);
            pCloth[2].AddCollisionSphere(-10.f, -10.0f, 20.0f, 27.0f, 17);
            pCloth[2].AddCollisionSphere(10.f, -10.0f, 20.0f, 27.0f, 17);
        }

        if (c->Wing.Type == MODEL_CAPE_OF_EMPEROR)
        {
            numCloth = 6;
        }
        else
        {
            numCloth = 3;
        }

        if (c->BodyPart[BODYPART_ARMOR].Type ==
            static_cast<int>(MODEL_BODY_ARMOR) + SKIN_CLASS_DARK_LORD)
        {
            numCloth = (std::max)(numCloth, 4);
            pCloth[3].Create(o, 18, 0.0f, 10.0f, -5.0f, 5, 5, 50.0f, 90.0f, BITMAP_DARK_LOAD_SKIRT,
                             BITMAP_DARK_LOAD_SKIRT,
                             PCT_MASK_ALPHA | PCT_HEAVY | PCT_STICKED | PCT_SHORT_SHOULDER);
            pCloth[3].AddCollisionSphere(0.0f, -15.0f, -20.0f, 30.0f, 2);
        }
        else if (c->BodyPart[BODYPART_ARMOR].Type ==
                 static_cast<int>(MODEL_BODY_ARMOR) + SKIN_CLASS_LORDEMPEROR)
        {
            numCloth = (std::max)(numCloth, 4);
            pCloth[3].Create(o, 18, 0.0f, 10.0f, -5.0f, 5, 5, 50.0f, 90.0f,
                             BITMAP_DARKLOAD_SKIRT_3RD, BITMAP_DARKLOAD_SKIRT_3RD,
                             PCT_MASK_ALPHA | PCT_HEAVY | PCT_STICKED | PCT_SHORT_SHOULDER);
            pCloth[3].AddCollisionSphere(0.0f, -15.0f, -20.0f, 30.0f, 2);
        }

        if (c->Wing.Type == MODEL_CAPE_OF_EMPEROR)
        {
            pCloth[4].Create(o, 19, 30.0f, 15.0f, 10.0f, 2, 5, 12.0f, 200.0f, BITMAP_ROBE + 10,
                             BITMAP_ROBE + 10,
                             PCT_FLAT | PCT_SHAPE_NORMAL | PCT_COTTON | PCT_MASK_ALPHA);
            pCloth[4].AddCollisionSphere(0.0f, -15.0f, -20.0f, 30.0f, 2);
            pCloth[4].AddCollisionSphere(0.f, 0.0f, 0.0f, 35.0f, 17);

            pCloth[5].Create(o, 19, -30.0f, 20.0f, 10.0f, 2, 5, 12.0f, 200.0f, BITMAP_ROBE + 10,
                             BITMAP_ROBE + 10,
                             PCT_FLAT | PCT_SHAPE_NORMAL | PCT_COTTON | PCT_MASK_ALPHA);
            pCloth[5].AddCollisionSphere(0.0f, -15.0f, -20.0f, 30.0f, 2);
            pCloth[5].AddCollisionSphere(0.f, 0.0f, 0.0f, 35.0f, 17);
        }

        visual.capeCloth->count = numCloth;
    }
    else if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER)
    {
        int numCloth = (c->Wing.Type == MODEL_CAPE_OF_OVERRULE) ? 3 : 1;

        visual.capeCloth = std::make_unique<CharacterClothVisual>(
            sessionKeeper_, CharacterClothVisual::Kind::Cape, numCloth);
        auto *pCloth = visual.capeCloth->pieces;

        if (c->Wing.Type == MODEL_CAPE_OF_OVERRULE)
        {
            pCloth[0].Create(o, 19, 0.0f, 15.0f, 5.0f, 10, 10, 180.0f, 170.0f, BITMAP_MANTOE,
                             BITMAP_MANTOE,
                             PCT_CURVED | PCT_SHORT_SHOULDER | PCT_HEAVY | PCT_MASK_ALPHA);
            pCloth[0].AddCollisionSphere(-10.f, -10.0f, -10.0f, 35.0f, 17);
            pCloth[0].AddCollisionSphere(10.f, -10.0f, -10.0f, 35.0f, 17);
            pCloth[0].AddCollisionSphere(-10.f, -10.0f, 20.0f, 37.0f, 17);
            pCloth[0].AddCollisionSphere(10.f, -10.0f, 20.0f, 37.0f, 17);
        }
        else if (c->Wing.Type == MODEL_WING + 135)
        {
            pCloth[0].Create(o, 19, 0.0f, 15.0f, 5.0f, 10, 10, 150.0f, 130.0f, BITMAP_NCCAPE,
                             BITMAP_NCCAPE,
                             PCT_CURVED | PCT_SHORT_SHOULDER | PCT_HEAVY | PCT_MASK_ALPHA);
            pCloth[0].AddCollisionSphere(-10.f, -10.0f, -10.0f, 35.0f, 17);
            pCloth[0].AddCollisionSphere(10.f, -10.0f, -10.0f, 35.0f, 17);
            pCloth[0].AddCollisionSphere(-10.f, -10.0f, 20.0f, 37.0f, 17);
            pCloth[0].AddCollisionSphere(10.f, -10.0f, 20.0f, 37.0f, 17);
        }
        else
        {
            pCloth[0].Create(o, 19, 0.0f, 15.0f, 5.0f, 10, 10, 180.0f, 170.0f, BITMAP_NCCAPE,
                             BITMAP_NCCAPE,
                             PCT_CURVED | PCT_SHORT_SHOULDER | PCT_HEAVY | PCT_MASK_ALPHA);
            pCloth[0].AddCollisionSphere(-10.f, -10.0f, -10.0f, 35.0f, 17);
            pCloth[0].AddCollisionSphere(10.f, -10.0f, -10.0f, 35.0f, 17);
            pCloth[0].AddCollisionSphere(-10.f, -10.0f, 20.0f, 37.0f, 17);
            pCloth[0].AddCollisionSphere(10.f, -10.0f, 20.0f, 37.0f, 17);
        }

        if (c->Wing.Type == MODEL_CAPE_OF_OVERRULE)
        {
            pCloth[1].Create(
                o, 19, 25.0f, 15.0f, 2.0f, 2, 5, 12.0f, 180.0f, BITMAP_MANTO01, BITMAP_MANTO01,
                PCT_FLAT | PCT_SHAPE_NORMAL | PCT_COTTON | PCT_ELASTIC_RAGE_L | PCT_MASK_ALPHA);
            pCloth[1].AddCollisionSphere(0.0f, -15.0f, -20.0f, 35.0f, 2);
            pCloth[1].AddCollisionSphere(0.0f, 0.0f, 0.0f, 45.0f, 17);

            pCloth[2].Create(
                o, 19, -25.0f, 15.0f, 2.0f, 2, 5, 12.0f, 180.0f, BITMAP_MANTO01, BITMAP_MANTO01,
                PCT_FLAT | PCT_SHAPE_NORMAL | PCT_COTTON | PCT_ELASTIC_RAGE_R | PCT_MASK_ALPHA);
            pCloth[2].AddCollisionSphere(0.0f, -15.0f, -20.0f, 35.0f, 2);
            pCloth[2].AddCollisionSphere(0.0f, 0.0f, 0.0f, 50.0f, 17);
        }

        visual.capeCloth->count = numCloth;
    }
    else
    {
        visual.capeCloth = std::make_unique<CharacterClothVisual>(
            sessionKeeper_, CharacterClothVisual::Kind::Cape, 1);
        auto *pCloth = visual.capeCloth->pieces;

        if (c->MonsterIndex == MONSTER_DEATH_KING)
        {
            pCloth[0].Create(o, 19, 0.0f, 10.0f, 0.0f, 10, 10, 55.0f, 140.0f, BITMAP_ROBE + 2,
                             BITMAP_ROBE + 2, PCT_CURVED | PCT_MASK_ALPHA);
            pCloth[0].AddCollisionSphere(-10.f, -10.0f, -10.0f, 35.0f, 17);
            pCloth[0].AddCollisionSphere(10.f, -10.0f, -10.0f, 35.0f, 17);
            pCloth[0].AddCollisionSphere(0.f, -10.0f, -40.0f, 50.0f, 19);
        }
        else if (c->MonsterIndex == MONSTER_NIGHTMARE)
        {
            pCloth[0].Create(o, 2, 0.0f, 0.0f, 0.0f, 6, 6, 180.0f, 100.0f, BITMAP_NIGHTMARE_ROBE,
                             BITMAP_NIGHTMARE_ROBE, PCT_CYLINDER | PCT_MASK_ALPHA);
        }
        else
        {
            if (c->Wing.Type == MODEL_WING_OF_RUIN)
            {
                pCloth[0].Create(o, 19, 0.0f, 15.0f, 0.0f, 10, 10, 120.0f, 120.0f, BITMAP_ROBE + 8,
                                 BITMAP_ROBE + 8,
                                 PCT_CURVED | PCT_SHORT_SHOULDER | PCT_HEAVY | PCT_MASK_ALPHA);
            }
            else
            {
                pCloth[0].Create(o, 19, 0.0f, 10.0f, 0.0f, 10, 10, 75.0f, 120.0f, BITMAP_ROBE,
                                 BITMAP_ROBE + 1, PCT_CURVED | PCT_SHORT_SHOULDER | PCT_HEAVY);
            }
        }
        visual.capeCloth->count = 1;
    }
}

void SessionVisualUnit::AdvanceCharacterCape(CHARACTER &character,
                                             WorldCharacterVisualState &visual, bool advance)
{
    auto *c = &character;
    auto *o = &character.Object;
    vec3_t CloakLight;
    if (!CharacterCapeAppearance(character, CloakLight))
    {
        visual.capeCloth.reset();
        return;
    }
    const std::array shape{static_cast<int>(o->Type), static_cast<int>(c->MonsterIndex),
                           static_cast<int>(gCharacterManager.GetBaseClass(c->Class)),
                           static_cast<int>(c->Wing.Type),
                           static_cast<int>(c->BodyPart[BODYPART_ARMOR].Type)};
    if (visual.capeCloth && visual.capeCloth->shape != shape)
        visual.capeCloth.reset();
    PrepareCharacterModel(character);
    if (!visual.capeCloth)
    {
        CreateCharacterCape(character, visual);
        visual.capeCloth->shape = shape;
        visual.capeCloth->visiblePieces.reserve(visual.capeCloth->count);
    }
    auto &cape = *visual.capeCloth;
    cape.visiblePieces.clear();
    VectorCopy(CloakLight, cape.light);
    if (g_isCharacterBuff(o, eBuff_Cloaking))
        return;
    const float step =
        g_isCharacterBuff(o, eDeBuff_Stun) || g_isCharacterBuff(o, eDeBuff_Sleep) ? 0.f : 0.005f;
    for (std::size_t i = 0; i < cape.count; ++i)
    {
        if (gCharacterManager.GetBaseClass(c->Class) == CLASS_DARK_LORD)
        {
            if (i == 2 &&
                ((c->Wing.Type != MODEL_CAPE_OF_LORD && c->Wing.Type != MODEL_CAPE_OF_EMPEROR &&
                  c->Wing.Type != MODEL_WING + 130) &&
                 (CloakLight[0] == 1.f && CloakLight[1] == 1.f && CloakLight[2] == 1.f)))
            {
                continue;
            }
            if ((i <= 1) && g_ChangeRingMgr->CheckDarkLordHair(c->Object.SubType) == true)
            {
                continue;
            }
            if (i == 3 && g_ChangeRingMgr->CheckDarkLordHair(c->Object.SubType) == true)
            {
                continue;
            }
        }
        if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER)
        {
            if (i == 0 &&
                ((c->Wing.Type != MODEL_CAPE_OF_FIGHTER && c->Wing.Type != MODEL_CAPE_OF_OVERRULE &&
                  c->Wing.Type != MODEL_WING + 135) &&
                 (CloakLight[0] == 1.f && CloakLight[1] == 1.f && CloakLight[2] == 1.f)))
            {
                continue;
            }
        }
        if (i == 0 && g_ChangeRingMgr->CheckDarkCloak(c->Class, c->Object.SubType) == true)
        {
            continue;
        }

        if (c->Object.SubType == MODEL_CURSEDTEMPLE_ALLIED_PLAYER ||
            c->Object.SubType == MODEL_CURSEDTEMPLE_ILLUSION_PLAYER)
        {
            continue;
        }

        // The optional Dark Lord skirt occupies slot 3 only for its authored armor.
        if (i == 3 && !cape.hasSkirt)
            continue;
        cape.pieces[i].SetPose(CharacterPresentationInput(*c).object.bones,
                               &c->WorldVisualPoseSample, &c->Object);
        if (advance && !cape.pieces[i].Move2(step, 5, FPS_ANIMATION_FACTOR, WorldTime))
        {
            visual.capeCloth.reset();
            return;
        }
        cape.visiblePieces.push_back(i);
    }
}

void SessionVisualUnit::AdvanceMapCharacterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                                  WorldCharacterVisualState &visual)
{
    // Match the generic presentation cases: these keep their existing character pipeline.
    switch (o->Type)
    {
    case MODEL_PLAYER:
        if (CharacterVisibleToObserver(*c))
            TheMapProcess().AdvancePlayerVisual(c, o);
        return;
    case MODEL_BULL_FIGHTER:
    case MODEL_DEATH_COW:
    case MODEL_CRUST:
    case MODEL_HYDRA:
    case MODEL_VEPAR:
    case MODEL_LIZARD:
    case MODEL_BAHAMUT:
    case MODEL_MIX_NPC:
    case MODEL_NPC_SEVINA:
    case MODEL_NPC_DEVILSQUARE:
    case MODEL_NPC_CASTEL_GATE:
    case MODEL_SHADOW:
    case MODEL_SEED_MASTER:
    case MODEL_SEED_INVESTIGATOR:
        return;
    default:
        break;
    }
    b->BodyHeight = 0.f;
    b->BodyScale = o->Scale;
    b->CurrentAction = visual.action;
    VectorCopy(o->Position, b->BodyOrigin);
    BodyLight(o, b);
    TheMapProcess().AdvanceMonsterVisual(c, o, b, visual);
}

void SessionVisualUnit::PrepareCharacterModel(const CHARACTER &character)
{
    auto &model = Models[character.Object.Type];
    model.BodyScale = character.Object.Scale;
    model.BodyHeight = 0.f;
    model.CurrentAction = character.WorldVisualAction;
    VectorCopy(character.Object.Position, model.BodyOrigin);
}

void SessionVisualUnit::AdvanceGenericCharacterVisual(CHARACTER &character,
                                                      WorldCharacterVisualState &visual)
{
    if (!character.Object.Live)
    {
        if (character.WorldVisualStructureDeath)
        {
            SessionRandom::PresentationScope presentation(sessionKeeper_.RandomForConstruction());
            PrepareCharacterModel(character);
            AdvanceNpcCharacterVisual(character, visual);
        }
        return;
    }
    if (g_isCharacterBuff(&character.Object, eBuff_CrywolfNPCHide) ||
        Models[character.Object.Type].NumActions == 0)
        return;
    const bool removeAbsent = visual.passiveReconcilePending;
    visual.passiveReconcilePending = false;
    SessionRandom::PresentationScope presentation(sessionKeeper_.RandomForConstruction());
    auto &model = Models[character.Object.Type];
    PrepareCharacterModel(character);
    AdvanceMovementCharacterVisual(character, visual);
    AdvanceParts(character, visual);
    AdvanceCharacterCloth(character, visual);
    AdvanceCharacterCape(character, visual);
    AdvanceCharacterPartCloth(character, visual);
    AdvanceSpecialMonsterVisual(character);
    AdvanceNpcCharacterVisual(character, visual);
    if (!g_isCharacterBuff(&character.Object, eBuff_Cloaking))
    {
        EmitCharacterFormVisual(character.Object);
        if (character.Object.Type == MODEL_PLAYER && character.Object.Kind == KIND_PLAYER &&
            character.Object.SubType == MODEL_XMAS_EVENT_CHANGE_GIRL)
            EmitChristmasFormVisual(character.Object, character.Object.m_iAnimation >= 1,
                                    visual.intervalStartFrame);
        if (character.Object.Type == MODEL_PLAYER && character.Object.Kind == KIND_PLAYER &&
            character.Object.SubType == MODEL_HALLOWEEN)
            EmitHalloweenFormVisual(character.Object, character.WorldVisualHalloweenSparks,
                                    character.WorldVisualHalloweenBurst);
    }
    AdvanceRabbitVisual(character, visual);
    AdvanceCherryBlossomVisual(character);
    AdvanceProtectGuildMark(character, visual);
    AdvanceDoppelgangerVisual(character);
    AdvanceCriticalDamageVisual(character, visual);
    AdvanceCharacterLinkedItems(character, visual);
    AdvanceWeaponVisual(character, visual);
    AdvanceWeaponSounds(character, visual);
    AdvanceTopGradeWeaponVisual(character);
    ReconcileCharacterStunVisual(character, removeAbsent);
    // Linked models use their own origins/scales; subsequent body effects use the character model.
    model.BodyScale = character.Object.Scale;
    model.CurrentAction = visual.action;
    VectorCopy(character.Object.Position, model.BodyOrigin);
    if ((visual.action == PLAYER_SKILL_DARKSIDE_READY ||
         visual.action == PLAYER_SKILL_DARKSIDE_ATTACK) &&
        character.JumpTime <= 0)
        g_CMonkSystem.AdvanceDarksideVisual(character, visual);
    if (gCharacterManager.GetBaseClass(character.Class) == CLASS_SUMMONER)
    {
        const auto &weapon = character.Weapon[1];
        g_SummonSystem.MoveEquipEffect(&character, weapon.Type, weapon.Level, weapon.ExcellentFlags,
                                       WorldTime, removeAbsent);
    }
    if (character.Object.Type != MODEL_PLAYER)
        return;
    if (!AdvancePlayerSpellVisual(character, visual))
    {
        visual.passiveReconcilePending |= removeAbsent;
        return;
    }
    AdvancePlayerBuffVisual(character);
    ReconcilePlayerBuffVisual(character, removeAbsent);
    AdvanceExtendedStateVisual(character, visual);
    AdvanceEquipmentSetVisual(character, visual);
}

bool SessionVisualUnit::PrepareCharacterVisualAppearance(CHARACTER &character,
                                                         WorldCharacterVisualState &visual)
{
    if (visual.generation != 0 && visual.generation != character.WorldVisualGeneration)
        RetireCharacterVisualLifetime(character, visual);
    else if (visual.appearanceRevision != character.WorldVisualAppearanceRevision)
    {
        RetireLinkedItemVisuals(visual);
        g_SummonSystem.RemoveEquipEffects(&character);
        visual.passiveReconcilePending = true;
    }
    visual.BindAppearance(character);
    if (visual.targetRevision != character.TargetBinding.revision)
    {
        visual.target = {};
        visual.targetRevision = character.TargetBinding.revision;
    }
    if (!character.TargetBinding.source || !character.TargetBinding.source->object)
        visual.target = {};
    else if (!visual.target.source)
    {
        const int target = CharactersClient.FindIndexByKey(character.TargetBinding.key);
        if (CharactersClient.IsValidIndex(target))
        {
            visual.target.character = &CharactersClient[target];
            visual.target.key = character.TargetBinding.key;
            visual.target.source = visual.target.character->SocketSource;
        }
    }
    if (visual.equipmentAppearanceRevision != character.WorldVisualAppearanceRevision &&
        character.Object.Type == MODEL_PLAYER)
    {
        visual.equipmentSet = sessionKeeper_.Gameplay()->EvaluateCharacterEquipmentSet(character);
        visual.equipmentAppearanceRevision = character.WorldVisualAppearanceRevision;
    }
    const auto buffRevision = character.Object.m_BuffMap.Revision();
    const bool buffsChanged = visual.buffRevision != buffRevision;
    if (buffsChanged)
    {
        visual.passiveReconcilePending = true;
        if (g_isCharacterBuff(&character.Object, eBuff_Cloaking))
            RetireLinkedItemVisuals(visual);
        for (auto &[slot, item] : visual.linkedItems)
        {
            if (!item)
                continue;
            g_CharacterCopyBuff(&item->target, &character.Object);
            g_CharacterCopyBuff(&item->item, &character.Object);
        }
        visual.buffRevision = buffRevision;
    }
    return buffsChanged;
}

void SessionVisualUnit::AdvanceCharacterAttachments(CHARACTER &character,
                                                    WorldCharacterVisualState &visual, bool advance,
                                                    bool forceRender, OBJECT *localMount)
{
    SessionRandom::PresentationScope presentation(sessionKeeper_.RandomForConstruction());
    struct CaptureScope final
    {
        std::unique_ptr<SessionSpriteStorage> *&output;
        std::unique_ptr<SessionSpriteStorage> *previous;
        ~CaptureScope()
        {
            output = previous;
        }
    } scope{mapSpriteOutput_, mapSpriteOutput_};
    mapSpriteOutput_ = &visual.attachmentSprites;
    if (advance && visual.attachmentSprites)
        visual.attachmentSprites->BeginTick();
    AdvanceCharacterPet(character, visual, advance, forceRender);
    if (forceRender || TheMapProcess().CharacterPolicy().mounts)
    {
        AdvanceCharacterHelperPet(character, visual, advance, forceRender);
        AdvanceCharacterMount(character, visual,
                              advance && (forceRender || sessionKeeper_.Display()->IsVisible()));
        if (localMount)
        {
            if (advance)
                sessionKeeper_.Gameplay()->MoveMount(localMount, TRUE, &character,
                                                     &visual.localMountPose);
            else
            {
                sessionKeeper_.Gameplay()->SynchronizeMount(*localMount);
                sessionKeeper_.Gameplay()->PrepareMountPose(*localMount, &visual.localMountPose);
            }
        }
    }
    if (advance && visual.attachmentSprites)
        visual.attachmentSprites->EndTick();
}

void SessionVisualUnit::AdvanceMapObservers()
{
    const bool sessionVisible = sessionKeeper_.Display()->IsVisible();
    const double simulationTime = WorldSimulationTime();
    if (attachmentSimulationTime_ != simulationTime)
    {
        attachmentSimulationTime_ = simulationTime;
        struct FactorScope final
        {
            float &factor;
            float previous;
            ~FactorScope()
            {
                factor = previous;
            }
        } scope{FPS_ANIMATION_FACTOR, FPS_ANIMATION_FACTOR};
        FPS_ANIMATION_FACTOR = sessionKeeper_.WorldUnit()->SimulationAnimationFactor();
        SessionRandom::PresentationScope presentation(sessionKeeper_.RandomForConstruction());
        if (sessionVisible)
            sessionKeeper_.Gameplay()->MoveMounts();
        sessionKeeper_.SpritesStorage().EndTick();
    }

    for (int index = 0; index < CharactersClient.Size(); ++index)
    {
        if (!CharactersClient.IsValidIndex(index))
            continue;
        auto lease = CharactersClient.AcquireSharedAccess(index);
        auto &character = CharactersClient[index];
        if (!gameplay_.PrepareCharacterObservation(character))
        {
            const int key = character.Key;
            lease.Release();
            DeleteCharacter(key);
            continue;
        }
        sessionKeeper_.Gameplay()->RefreshPendingCharacterPose(character);
        auto &visual = CharactersClient.WorldVisuals(index);
        const bool buffsChanged = PrepareCharacterVisualAppearance(character, visual);
        const bool cosmeticVisible = sessionVisible && CharactersClient.IsVisible(index) &&
                                     CharacterVisibleToObserver(character);
        if (visual.cosmeticVisible && !cosmeticVisible)
            visual.ResetCloth();
        visual.cosmeticVisible = cosmeticVisible;
        if (character.WorldVisualTick == 0 || visual.consumedTick == character.WorldVisualTick)
        {
            if (cosmeticVisible && character.WorldVisualPoseRevision != 0)
                PrepareMovementCharacterPose(character, visual, false);
            if (character.WorldVisualPoseRevision != 0 && CharacterVisibleToObserver(character))
            {
                if (visual.poseRevision != character.WorldVisualPoseRevision)
                {
                    visual.ResetCloth();
                    visual.poseRevision = character.WorldVisualPoseRevision;
                }
                AdvanceCharacterAttachments(character, visual, false);
                if (cosmeticVisible)
                {
                    AdvanceParts(character, visual, false);
                    AdvanceCharacterCloth(character, visual, false);
                    AdvanceCharacterCape(character, visual, false);
                    AdvanceCharacterPartCloth(character, visual, false);
                    AdvanceCharacterLinkedItems(character, visual, false);
                }
            }
            if (buffsChanged && character.WorldVisualPoseRevision != 0 &&
                CharacterVisibleToObserver(character))
            {
                SessionRandom::PresentationScope presentation(
                    sessionKeeper_.RandomForConstruction());
                ReconcileCharacterStunVisual(character, true);
                visual.passiveReconcilePending = false;
                if (character.Object.Type == MODEL_PLAYER)
                    ReconcilePlayerBuffVisual(character, true);
                if (visual.sprites)
                    visual.sprites->Clear();
                if (visual.attachmentSprites)
                    visual.attachmentSprites->Clear();
            }
            continue;
        }
        visual.CaptureSample(character);
        if (visual.sprites)
            visual.sprites->BeginTick();
        struct CaptureScope final
        {
            std::unique_ptr<SessionSpriteStorage> *&target;
            std::unique_ptr<SessionSpriteStorage> *previous;
            float &factor;
            float previousFactor;
            ~CaptureScope()
            {
                target = previous;
                factor = previousFactor;
            }
        } scope{mapSpriteOutput_, mapSpriteOutput_, FPS_ANIMATION_FACTOR, FPS_ANIMATION_FACTOR};
        mapSpriteOutput_ = &visual.sprites;
        FPS_ANIMATION_FACTOR = character.WorldVisualAnimationFactor;
        if (cosmeticVisible)
            PrepareMovementCharacterPose(character, visual, true);
        if (CharacterVisibleToObserver(character))
            AdvanceCharacterAttachments(character, visual, true);
        if (!sessionVisible || !CharactersClient.IsVisible(index))
        {
            if (visual.sprites)
                visual.sprites->EndTick();
            continue;
        }

        if (CharacterVisibleToObserver(character) || character.WorldVisualStructureDeath)
            AdvanceGenericCharacterVisual(character, visual);
        AdvanceMapCharacterVisual(&character, &character.Object, &Models[character.Object.Type],
                                  visual);
        if (visual.sprites)
            visual.sprites->EndTick();
    }
}

void SessionVisualUnit::AdvanceLocalCharacterVisual(CHARACTER &character,
                                                    WorldCharacterVisualState &visual,
                                                    OBJECT *localMount)
{
    SessionRandom::PresentationScope presentation(sessionKeeper_.RandomForConstruction());
    struct PreparationScope final
    {
        const CHARACTER *&character;
        const CHARACTER *previousCharacter;
        const WorldCharacterVisualState *&visual;
        const WorldCharacterVisualState *previousVisual;
        ~PreparationScope()
        {
            character = previousCharacter;
            visual = previousVisual;
        }
    } preparation{preparingCharacter_, preparingCharacter_, preparingCharacterVisual_,
                  preparingCharacterVisual_};
    preparingCharacter_ = &character;
    preparingCharacterVisual_ = &visual;
    const bool buffsChanged = PrepareCharacterVisualAppearance(character, visual);
    if (character.WorldVisualPoseRevision != 0)
        PrepareMovementCharacterPose(character, visual,
                                     character.WorldVisualTick != 0 &&
                                         visual.consumedTick != character.WorldVisualTick);
    if (character.WorldVisualTick == 0 || visual.consumedTick == character.WorldVisualTick)
    {
        if (character.WorldVisualPoseRevision != 0)
        {
            if (visual.poseRevision != character.WorldVisualPoseRevision)
            {
                // Preview rotation/zoom refreshes the pose without stepping physics.
                visual.ResetCloth();
                visual.CaptureSample(character);
            }
            AdvanceParts(character, visual, false);
            AdvanceCharacterAttachments(character, visual, false, true, localMount);
            AdvanceCharacterCloth(character, visual, false);
            AdvanceCharacterCape(character, visual, false);
            AdvanceCharacterPartCloth(character, visual, false);
            AdvanceCharacterLinkedItems(character, visual, false);
        }
        if (buffsChanged && character.WorldVisualPoseRevision != 0)
        {
            SessionRandom::PresentationScope presentation(sessionKeeper_.RandomForConstruction());
            ReconcileCharacterStunVisual(character, true);
            visual.passiveReconcilePending = false;
            if (character.Object.Type == MODEL_PLAYER)
                ReconcilePlayerBuffVisual(character, true);
            if (visual.sprites)
                visual.sprites->Clear();
            if (visual.attachmentSprites)
                visual.attachmentSprites->Clear();
        }
        return;
    }
    visual.CaptureSample(character);
    if (visual.sprites)
        visual.sprites->BeginTick();
    struct CaptureScope final
    {
        std::unique_ptr<SessionSpriteStorage> *&target;
        std::unique_ptr<SessionSpriteStorage> *previous;
        ~CaptureScope()
        {
            target = previous;
        }
    } scope{mapSpriteOutput_, mapSpriteOutput_};
    mapSpriteOutput_ = &visual.sprites;
    AdvanceCharacterAttachments(character, visual, true, true, localMount);
    AdvanceGenericCharacterVisual(character, visual);
    if (visual.sprites)
        visual.sprites->EndTick();
}

void RenderColor(float x, float y, float Width, float Height, float Alpha, int Flag);

extern void MoveCharacter(CHARACTER *c, OBJECT *o);

void CUIPhotoViewer::UpdatePhotoPose()
{
    CHARACTER *c = &m_PhotoChar;
    OBJECT *o = &c->Object;

    if (o->AnimationFrame < m_iCurrentFrame)
    {
        if (m_bActionRepeatCheck == FALSE && rand_fps_check(4))
        {
            m_bActionRepeatCheck = TRUE;
            SetPhotoPose(m_iSettingAnimation);
        }
        else
        {
            if (m_iSettingAnimation >= AT_STAND1 && m_iSettingAnimation <= AT_HEALING1)
                ;
            else
            {
                m_bActionRepeatCheck = FALSE;
                SetPhotoPose(AT_STAND1);
            }
        }
        m_iCurrentFrame = 0;
    }
    else
        m_iCurrentFrame = o->AnimationFrame;

    if (c->EtcPart < PARTS_LION)
    {
        DeleteParts(&m_PhotoChar);
    }
}

namespace CharacterPresentationDetail
{
// Character selection screen: generous axis-aligned pick box dimensions.
constexpr float CHARSCENE_PICK_MIN_HEIGHT = 300.0f;
constexpr float CHARSCENE_PICK_HALF_WIDTH = 72.0f;

} // namespace CharacterPresentationDetail

void SessionInteractionUnit::BuildCharacterScenePickOBB(const OBJECT *o, OBB_t &outOBB)
{
    const float extent = (std::max)({o->BoundingBoxMax[0] - o->BoundingBoxMin[0],
                                     o->BoundingBoxMax[1] - o->BoundingBoxMin[1],
                                     o->BoundingBoxMax[2] - o->BoundingBoxMin[2]});
    float charHeight = extent * 2.0f;
    if (charHeight < CharacterPresentationDetail::CHARSCENE_PICK_MIN_HEIGHT)
        charHeight = CharacterPresentationDetail::CHARSCENE_PICK_MIN_HEIGHT;

    outOBB.StartPos[0] = o->Position[0] - CharacterPresentationDetail::CHARSCENE_PICK_HALF_WIDTH;
    outOBB.StartPos[1] = o->Position[1] - CharacterPresentationDetail::CHARSCENE_PICK_HALF_WIDTH;
    outOBB.StartPos[2] = o->Position[2];
    outOBB.XAxis[0] = CharacterPresentationDetail::CHARSCENE_PICK_HALF_WIDTH * 2.0f;
    outOBB.XAxis[1] = 0.0f;
    outOBB.XAxis[2] = 0.0f;
    outOBB.YAxis[0] = 0.0f;
    outOBB.YAxis[1] = CharacterPresentationDetail::CHARSCENE_PICK_HALF_WIDTH * 2.0f;
    outOBB.YAxis[2] = 0.0f;
    outOBB.ZAxis[0] = 0.0f;
    outOBB.ZAxis[1] = 0.0f;
    outOBB.ZAxis[2] = charHeight;
}

static constexpr wchar_t vec_list[35] = {5,  6,  33, 53, 35, 49, 50, 45, 46, 41, 42, 37,
                                         38, 11, 31, 13, 27, 28, 23, 24, 19, 20, 15, 16,
                                         54, 55, 62, 69, 70, 77, 2,  79, 81, 84, 86};
static constexpr wchar_t wingLeft[15][2] = {{0, 2},   {2, 3}, {2, 4}, {4, 5},  {5, 6},
                                            {4, 7},   {7, 8}, {4, 9}, {9, 10}, {4, 11},
                                            {11, 12}, {6, 5}, {8, 7}, {10, 9}, {12, 11}};
static constexpr wchar_t wingRight[15][2] = {{0, 13},  {13, 14}, {13, 15}, {15, 16}, {16, 17},
                                             {15, 18}, {18, 19}, {15, 20}, {20, 21}, {15, 22},
                                             {22, 23}, {17, 16}, {19, 18}, {21, 20}, {23, 22}};
static constexpr wchar_t arm_leg_Left[4][2] = {
    {29, 28},
    {28, 27},
    {34, 33},
    {33, 30},
};
static constexpr wchar_t arm_leg_Right[4][2] = {
    {26, 25},
    {25, 24},
    {32, 31},
    {31, 30},
};

static constexpr wchar_t g_chStar[10] = {10, 18, 37, 38, 51, 52, 58, 59, 66, 24};

//extern bool EnableEdit;
#define RGZ_FIX_ATTACK_SPEED

void SessionVisualUnit::MonsterDieSandSmoke(OBJECT *o, WorldCharacterVisualState &visual)
{
    if (o->CurrentAction != MONSTER01_DIE)
    {
        visual.sandSmokeEmitted = false;
        return;
    }
    if (visual.sandSmokeEmitted || o->AnimationFrame < 8.f || FPS_ANIMATION_FACTOR <= 0.f)
        return;
    visual.sandSmokeEmitted = true;
    const float fraction =
        o->MotionTrace.FirstAnimationCrossing(WorldTime, MONSTER01_DIE, 8.f).value_or(1.f);
    auto birthTime =
        sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR * (1.f - fraction));
    vec3_t origin;
    o->MotionTrace.Sample(WorldTime, fraction, o->Position, origin);
    for (int i = 0; i < 20; ++i)
    {
        vec3_t light{1.f, 1.f, 1.f}, position;
        Vector(origin[0] + float(WorldRandom() % 64 - 32),
               origin[1] + float(WorldRandom() % 64 - 32),
               origin[2] + float(WorldRandom() % 32 - 16), position);
        CreateParticle(BITMAP_SMOKE + 1, position, o->Angle, light, 1);
    }
}

void SessionVisualUnit::PrepareMovementCharacterPose(CHARACTER &character,
                                                     WorldCharacterVisualState &visual,
                                                     bool advance)
{
    auto &state = visual.movement;
    const auto &object = character.Object;
    auto &model = Models[object.Type];
    if (!state.initialized || state.type != object.Type)
    {
        state.initialized = true;
        state.type = object.Type;
        state.materialFields = 0;
        state.actionStarted = false;
        state.foot[0] = state.foot[1] = false;
        state.footAction = -1;
        state.footFrame = 0.f;
        VectorCopy(object.HeadAngle, state.head);
        VectorCopy(object.HeadTargetAngle, state.headTarget);
        VectorCopy(object.Light, state.light);
        VectorCopy(object.Light, state.sourceLight);
        state.blendMesh = object.BlendMesh;
        state.hiddenMesh = object.HiddenMesh;
        state.blendLight = object.BlendMeshLight;
        state.blendU = object.BlendMeshTexCoordU;
        state.blendV = object.BlendMeshTexCoordV;
        state.animation = object.m_iAnimation;
        state.subType = object.SubType;
        state.weaponLevel = object.WeaponLevel;
        state.lifeTime = character.WorldVisualPriorLifeTime;
        state.renderShadow = object.m_bRenderShadow;
    }
    if (state.sourceLight[0] != object.Light[0] || state.sourceLight[1] != object.Light[1] ||
        state.sourceLight[2] != object.Light[2])
    {
        VectorCopy(object.Light, state.sourceLight);
        VectorCopy(object.Light, state.light);
    }
    if (&character == Hero)
    {
        VectorCopy(object.HeadAngle, state.head);
    }
    else if (advance && model.BoneHead >= 0 && !g_isCharacterBuff(&object, eDeBuff_Harden) &&
             !g_isCharacterBuff(&object, eDeBuff_Stun) &&
             !g_isCharacterBuff(&object, eDeBuff_Sleep) &&
             !g_isCharacterBuff(&object, eBuff_Att_up_Ourforces) &&
             !g_isCharacterBuff(&object, eBuff_Hp_up_Ourforces) &&
             !g_isCharacterBuff(&object, eBuff_Def_up_Ourforces))
    {
        SessionRandom::PresentationScope presentation(sessionKeeper_.RandomForConstruction());
        if (object.Type != MODEL_PLAYER && object.CurrentAction != MONSTER01_DIE)
        {
            if (object.CurrentAction == MONSTER01_STOP1)
            {
                if (rand_fps_check(32))
                {
                    state.headTarget[0] = Random.RangeFloat(-64, 63);
                    state.headTarget[1] = Random.RangeFloat(-16, 31);
                }
            }
            else if (object.CurrentAction == MONSTER01_WALK && visual.target.Resolve() != nullptr)
            {
                const auto &target = visual.target.Resolve()->Object;
                const float angle = CreateAngle2D(object.Position, target.Position);
                state.headTarget[0] =
                    FarAngle(object.Angle[2], angle) < 90.f ? object.Angle[2] - angle : 0.f;
                state.headTarget[1] = FarAngle(object.Angle[2], angle) < 90.f
                                          ? (target.Position[2] - object.Position[2] - 50.f) * 0.2f
                                          : 0.f;
            }
            else
                state.headTarget[0] = state.headTarget[1] = 0.f;
        }
        if (character.Dead == 0 && rand_fps_check(32))
        {
            state.headTarget[0] = static_cast<float>(WorldRandom() % 128 - 64);
            state.headTarget[1] = static_cast<float>(WorldRandom() % 32 - 16);
        }
        for (int axis = 0; axis < 2; ++axis)
        {
            if (state.headTarget[axis] < 0.f)
                state.headTarget[axis] += 360.f;
            state.head[axis] = TurnAngle2(state.head[axis], state.headTarget[axis],
                                          FarAngle(state.head[axis], state.headTarget[axis]) *
                                              Core::Time::Blend(0.2f, FPS_ANIMATION_FACTOR));
        }
    }
    auto sample = character.WorldVisualPoseSample;
    sample.asset = model.PoseAssetIdentity();
    sample.boneHead = model.BoneHead;
    VectorCopy(state.head, sample.headAngle.data());
    state.hasPose = sample != character.WorldVisualPoseSample;
    if (!state.hasPose || sample == state.pose)
        return;
    if (state.boneCapacity < model.NumBones)
    {
        state.bones = std::make_unique<vec34_t[]>(model.NumBones);
        state.boneCapacity = model.NumBones;
    }
    sample.Evaluate(model, state.bones.get());
    state.pose = sample;
}

void SessionVisualUnit::AdvanceMovementCharacterVisual(CHARACTER &character,
                                                       WorldCharacterVisualState &visual)
{
    auto *c = &character;
    auto *o = &character.Object;
    auto *b = &Models[o->Type];
    const auto *bones = visual.movement.hasPose ? visual.movement.bones.get() : o->BoneTransform;

    std::array<vec34_t, MAX_BONES> emissionBones;
    const auto sampleEmitter = [&](float fraction, bool skeletal) {
        ObjectDrawInput draw(o);
        o->MotionTrace.Sample(WorldTime, fraction, o->Position, draw.position);
        draw.angle[2] = o->MotionTrace.SampleYaw(WorldTime, fraction, draw.angle[2]);
        if (skeletal)
        {
            const auto &pose =
                visual.movement.hasPose ? visual.movement.pose : character.WorldVisualPoseSample;
            draw.bones = pose.EvaluateAtTime(*b, *o, WorldTime, fraction, emissionBones.data());
        }
        return draw;
    };

    visual.movement.lightChanged = true;

    if (visual.movement.footAction != visual.action ||
        visual.animationFrame < visual.movement.footFrame)
        visual.movement.foot[0] = visual.movement.foot[1] = false;
    visual.movement.footFrame = visual.animationFrame;
    visual.movement.footAction = visual.action;
    vec3_t p, Position;
    vec3_t Light;
    float Luminosity = 0.8f;
    if (c->WorldVisualAppearing)
    {

        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            auto draw = sampleEmitter(birthTime.FrameFraction(), false);
            for (int i = 0; i < 20; i++)
            {
                Vector(1.f, 1.f, 1.f, visual.movement.light);
                Vector(draw.position[0] + (float)(WorldRandom() % 64 - 32),
                       draw.position[1] + (float)(WorldRandom() % 64 - 32),
                       draw.position[2] + (float)(WorldRandom() % 32 - 16), Position);
                if (Random.FpsCheck(10, 1.f))
                    CreateParticle(BITMAP_SMOKE + 1, Position, draw.angle, visual.movement.light,
                                   1);
                if (Random.FpsCheck(10, 1.f))
                    CreateEffect(MODEL_STONE1 + WorldRandom() % 2, draw.position, draw.angle,
                                 visual.movement.light);
            }
        }
    }
    if (c->PK < PVP_MURDERER2)
    {
        for (int j = 0; j < 2; j++)
        {
            if (c->Weapon[j].Type == MODEL_HELIACAL_SWORD)
            {
                Vector(Luminosity, Luminosity * 0.8f, Luminosity * 0.5f, Light);
                AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
            }
            else if (c->Weapon[j].Type == MODEL_DIVINE_SWORD_OF_ARCHANGEL ||
                     c->Weapon[j].Type == MODEL_DIVINE_CB_OF_ARCHANGEL ||
                     c->Weapon[j].Type == MODEL_DIVINE_STAFF_OF_ARCHANGEL ||
                     c->Weapon[j].Type == MODEL_DIVINE_SCEPTER_OF_ARCHANGEL)
            {
                Vector(Luminosity * 0.8f, Luminosity * 0.5f, Luminosity * 0.3f, Light);
                AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
            }
        }
    }
    if (c->Freeze > 0.f)
    {
        if (c->FreezeType == BITMAP_ICE && rand_fps_check(4))
        {
            Vector(o->Position[0] + (float)(WorldRandom() % 100 - 50),
                   o->Position[1] + (float)(WorldRandom() % 100 - 50),
                   o->Position[2] + (float)(WorldRandom() % 180), Position);
            //CreateParticle(BITMAP_SHINY,Position,o->Angle,visual.movement.light);
            //CreateParticle(BITMAP_SHINY,Position,o->Angle,visual.movement.light,1);
        }
        if (c->FreezeType == BITMAP_BURN)
        {
            Vector(0.f, 0.f, -20.f, Position);
            for (int i = 1; i < b->NumBones;)
            {
                if (!b->Bones[i].Dummy)
                {
                    b->TransformPosition(bones[i], Position, p);
                    //CreateParticle(BITMAP_FIRE+1,p,o->Angle,visual.movement.light,WorldRandom()%4);
                }
                int d = (int)(4.f / c->Freeze);
                if (d < 1)
                    d = 1;
                i += d;
            }
        }
    }
    Vector(1.f, 1.f, 1.f, Light);
    Vector(0.f, 0.f, 0.f, p);
    Luminosity = (float)(WorldRandom() % 8 + 2) * 0.1f;
    bool Smoke = false;
    switch (o->Type)
    {
    case MODEL_PLAYER:
        if (SceneFlag == MAIN_SCENE && (TheMapProcess().CharacterPolicy().ocean) &&
            (int)WorldTime % 10000 < 1000)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                Vector(0.f, 20.f, -10.f, p);
                b->TransformByObjectBone(Position, draw, b->BoneHead, p);
                CreateParticle(BITMAP_BUBBLE, Position, draw.angle, Light);
            }
        }
        Vector(1.f, 1.f, 1.f, Light);
        Vector(-15.f, 0.f, 0.f, p);
        if (gMapManager.InDevilSquare() == true)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                b->TransformByObjectBone(Position, draw, 26, p);
                CreateParticle(BITMAP_RAIN_CIRCLE + 1, Position, draw.angle, Light);
            }
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                b->TransformByObjectBone(Position, draw, 35, p);
                CreateParticle(BITMAP_RAIN_CIRCLE + 1, Position, draw.angle, Light);
            }
        }
        if (visual.action == PLAYER_SKILL_HELL_BEGIN || visual.action == PLAYER_SKILL_HELL_START)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                if (bones != NULL)
                {
                    Vector(0.3f, 0.3f, 1.f, Light);

                    for (int i = 0; i < 40; i += 2)
                    {
                        if (i < b->NumBones && !b->Bones[i].Dummy)
                        {
                            b->TransformByObjectBone(Position, draw, i, p);

                            for (int j = 0; j < o->m_bySkillCount + 1; ++j)
                            {
                                CreateParticle(BITMAP_LIGHT, Position, draw.angle, Light, 6,
                                               1.3f + (o->m_bySkillCount * 0.08f));
                            }
                        }
                    }
                }
                VectorCopy(draw.position, Position);
                CreateForce(o, Position);
            }
        }

        if (visual.action == PLAYER_SKILL_HELL)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                for (int i = 0; i < 10; i++)
                {
                    b->TransformByObjectBone(Position, draw, WorldRandom() % b->NumBones, p);
                    CreateParticle(BITMAP_FIRE, Position, draw.angle, Light);
                }
            }
        }

        Vector(0.f, -30.f, 0.f, p);
        b->TransformPosition(bones[c->Weapon[0].LinkBone], p, Position, true);
        break;

    case MODEL_MOLT:
        break;
    case MODEL_ALQUAMOS:
        break;
    case MODEL_QUEEN_RAINER:
        EmitQueenRainerLightning(*o, *b,
                                 visual.movement.hasPose ? visual.movement.pose
                                                         : character.WorldVisualPoseSample);
        break;
    case MODEL_CRUST: {
        visual.movement.materialFields |= CharacterMovementVisual::U;
        visual.movement.blendU = -(float)((int)(WorldTime) % 10000) * 0.0004f;
    }
    break;
    case MODEL_DARK_PHEONIX_SHIELD: {
        visual.movement.materialFields |= CharacterMovementVisual::V;
        visual.movement.blendV = (float)((int)(WorldTime) % 10000) * 0.0001f;
    }
    break;
    case MODEL_DRAKAN:
        break;
    case MODEL_CURSED_KING:
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 5.f))
        {
            auto draw = sampleEmitter(birth.FrameFraction(), false);
            VectorCopy(draw.position, Position);
            Position[0] =
                draw.position[0] + ((WorldRandom() % 21) - 10) * ((float)TERRAIN_SIZE / 70);
            Position[1] =
                draw.position[1] + ((WorldRandom() % 21) - 10) * ((float)TERRAIN_SIZE / 70);
            CreatePointer(BITMAP_BLOOD, Position, draw.angle[0], visual.movement.light, 0.65f);
        }
        break;
    case MODEL_FRED:
        break;
    case MODEL_RED_SKELETON_KNIGHT_1:
    case MODEL_MUTANT:
        MonsterMoveSandSmoke(o);
        //MonsterDieSandSmoke(o, visual);
        break;
    case MODEL_BEAM_KNIGHT: //
        if (c->MonsterIndex == MONSTER_DEATH_BEAM_KNIGHT)
        {
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                wchar_t body[2] = {30, 0};
                char head = 1;
                vec3_t vec[35];
                vec3_t angle;
                vec3_t dist;
                vec3_t p;

                Vector(0.f, 0.f, 0.f, angle);
                Vector(0.f, 0.f, 0.f, p);

                const float fraction = birthTime.FrameFraction();
                AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false,
                                         b->PoseAssetIdentity());
                std::array<vec34_t, MAX_BONES> sampledBones;
                pose.EvaluateAtTime(*b, *o, WorldTime, fraction, sampledBones.data());
                vec3_t origin;
                o->MotionTrace.Sample(WorldTime, fraction, o->Position, origin);
                for (int i = 0; i < 35; ++i)
                {
                    VectorTransform(p, sampledBones[vec_list[i]], vec[i]);
                    VectorScale(vec[i], o->Scale, vec[i]);
                    VectorAdd(vec[i], origin, vec[i]);
                }

                char start, end;
                float scale = 1.0f;

                for (int i = 0; i < 15; ++i)
                {
                    if (i >= 11)
                    {
                        scale = 0.5f;
                    }

                    start = wingLeft[i][0];
                    end = wingLeft[i][1];

                    dist[0] = ::MoveHumming(vec[end], angle, vec[start], 360.0f);
                    CreateParticle(BITMAP_FLAME, vec[start], angle, dist, 2, scale);

                    start = wingRight[i][0];
                    end = wingRight[i][1];

                    dist[0] = ::MoveHumming(vec[end], angle, vec[start], 360.0f);
                    CreateParticle(BITMAP_FLAME, vec[start], angle, dist, 2, scale);
                }

                for (int i = 0; i < 4; ++i)
                {
                    start = arm_leg_Left[i][0];
                    end = arm_leg_Left[i][1];

                    dist[0] = ::MoveHumming(vec[end], angle, vec[start], 360.0f);
                    CreateParticle(BITMAP_FLAME, vec[start], angle, dist, 2, 0.6f);

                    start = arm_leg_Right[i][0];
                    end = arm_leg_Right[i][1];

                    dist[0] = ::MoveHumming(vec[end], angle, vec[start], 360.0f);
                    CreateParticle(BITMAP_FLAME, vec[start], angle, dist, 2, 0.6f);
                }

                if (visual.alternateSide == 0)
                {
                    start = body[0];
                    end = body[1];

                    dist[0] = ::MoveHumming(vec[end], angle, vec[start], 360.0f);
                    CreateParticle(BITMAP_FLAME, vec[start], angle, dist, 2, 1.3f);
                    CreateParticle(BITMAP_FLAME, vec[head], angle, dist, 3, 0.5f);
                }

                visual.alternateSide ^= 1;
            }
            Vector(-1.3f, -1.3f, -1.3f, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
        }
        else
        {
            vec3_t pos, angle;
            Vector(0.f, 0.f, 0.f, angle);

            Luminosity = (float)sinf(WorldTime * 0.002f) * 0.3f + 0.7f;

            Vector(Luminosity, Luminosity * 0.5f, Luminosity * 0.5f, Light);

            b->TransformPosition(bones[55], p, pos, true);
            b->TransformPosition(bones[62], p, Position, true);
            ::MoveHumming(pos, angle, Position, 360.0f);
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                b->TransformByObjectBone(pos, draw, 55, p);
                b->TransformByObjectBone(Position, draw, 62, p);
                ::MoveHumming(pos, angle, Position, 360.f);
                CreateParticle(BITMAP_FLAME, Position, angle, Light, 1, 0.2f);
            }

            b->TransformPosition(bones[70], p, pos, true);
            b->TransformPosition(bones[77], p, Position, true);
            ::MoveHumming(pos, angle, Position, 360.0f);
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                b->TransformByObjectBone(pos, draw, 70, p);
                b->TransformByObjectBone(Position, draw, 77, p);
                ::MoveHumming(pos, angle, Position, 360.f);
                CreateParticle(BITMAP_FLAME, Position, angle, Light, 1, 0.2f);
            }

            MonsterMoveSandSmoke(o);
            MonsterDieSandSmoke(o, visual);
        }
        break;
    case MODEL_BLOODY_WOLF:
        MonsterMoveSandSmoke(o);
        //MonsterDieSandSmoke(o, visual);
        break;
    case MODEL_TANTALLOS: //
        if (o->SubType == 1)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                b->TransformByObjectBone(Position, draw, 6, p);
                CreateParticle(BITMAP_FIRE, Position, draw.angle, Light);
                b->TransformByObjectBone(Position, draw, 13, p);
                CreateParticle(BITMAP_FIRE, Position, draw.angle, Light);
            }

            Vector(-1.3f, -1.3f, -1.3f, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
        }
        else
        {
            MonsterMoveSandSmoke(o);
            MonsterDieSandSmoke(o, visual);
        }
        break;
    case MODEL_GOLDEN_WHEEL:
        MonsterMoveSandSmoke(o);
        MonsterDieSandSmoke(o, visual);
        break;
    case MODEL_TITAN:
        break;
    case MODEL_HYDRA:
        if (visual.action >= MONSTER01_ATTACK1 && visual.action <= MONSTER01_ATTACK2)
        {
            {
                visual.movement.materialFields |= CharacterMovementVisual::Brightness;
                visual.movement.blendLight += (0.1f) * FPS_ANIMATION_FACTOR;
            }
            if (visual.movement.blendLight > 1.f)
            {
                visual.movement.materialFields |= CharacterMovementVisual::Brightness;
                visual.movement.blendLight = 1.f;
            }
        }
        else
        {
            {
                visual.movement.materialFields |= CharacterMovementVisual::Brightness;
                visual.movement.blendLight -= (0.1f) * FPS_ANIMATION_FACTOR;
            }
            if (visual.movement.blendLight < 0.f)
            {
                visual.movement.materialFields |= CharacterMovementVisual::Brightness;
                visual.movement.blendLight = 0.f;
            }
        }
        break;
    case MODEL_DEATH_KNIGHT: {
        visual.movement.materialFields |= CharacterMovementVisual::Mesh;
        visual.movement.blendMesh = 3;
    }
        {
            visual.movement.materialFields |= CharacterMovementVisual::V;
            visual.movement.blendV = -(float)((int)(WorldTime) % 1000) * 0.001f;
        }
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
        {
            auto draw = sampleEmitter(birth.FrameFraction(), true);
            Vector(0.f, 0.f, 0.f, p);
            b->TransformByObjectBone(Position, draw, 2, p);
            CreateParticle(BITMAP_FIRE, Position, draw.angle, Light);
        }
        break;
    case MODEL_BALROG: {
        visual.movement.materialFields |= CharacterMovementVisual::V;
        visual.movement.blendV = -(float)((int)(WorldTime) % 1000) * 0.001f;
    }
    break;
    case MODEL_DEVIL: {
        visual.movement.materialFields |= CharacterMovementVisual::U;
        visual.movement.blendU = -(float)((int)(WorldTime) % 10000) * 0.0001f;
    }
    break;
    case MODEL_BALI:
        Vector(0.f, 0.f, 0.f, p);
        Vector(0.6f, 1.f, 0.8f, Light);
        if (visual.action == MONSTER01_ATTACK1)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                Vector(0.6f, 1.f, 0.8f, Light);
                b->TransformByObjectBone(Position, draw, 33, p);
                CreateParticle(BITMAP_ENERGY, Position, draw.angle, Light);
                Vector(1.f, 0.6f, 1.f, Light);
                CreateParticle(BITMAP_FIRE, Position, draw.angle, Light);
            }
        }
        if (visual.action == MONSTER01_ATTACK2)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                Vector(0.6f, 1.f, 0.8f, Light);
                b->TransformByObjectBone(Position, draw, 20, p);
                CreateParticle(BITMAP_ENERGY, Position, draw.angle, Light);
                Vector(1.f, 0.6f, 1.f, Light);
                CreateParticle(BITMAP_FIRE, Position, draw.angle, Light);
            }
        }
        if (visual.action == MONSTER01_ATTACK3)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                Vector(0.6f, 1.f, 0.8f, Light);
                b->TransformByObjectBone(Position, draw, 41, p);
                CreateParticle(BITMAP_ENERGY, Position, draw.angle, Light);
                Vector(1.f, 0.6f, 1.f, Light);
                CreateParticle(BITMAP_FIRE, Position, draw.angle, Light);
            }
        }
        if (visual.action == MONSTER01_ATTACK4)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                Vector(0.6f, 1.f, 0.8f, Light);
                b->TransformByObjectBone(Position, draw, 49, p);
                CreateParticle(BITMAP_ENERGY, Position, draw.angle, Light);
                Vector(1.f, 0.6f, 1.f, Light);
                CreateParticle(BITMAP_FIRE, Position, draw.angle, Light);
            }
        }
        if (visual.action == MONSTER01_DIE && visual.animationFrame < 12.f)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                Vector(0.1f, 0.8f, 0.6f, Light);

                for (int i = 0; i < 20; i++)
                {
                    b->TransformByObjectBone(Position, draw, WorldRandom() % b->NumBones, p);
                    CreateParticle(BITMAP_FIRE, Position, draw.angle, Light);
                }
            }
        }
        break;
    case MODEL_GORGON: {
        visual.movement.materialFields |= CharacterMovementVisual::Brightness;
        visual.movement.blendLight = (float)(WorldRandom() % 10) * 0.1f;
    }
        if (c->Level == 2)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                for (int i = 0; i < 10; i++)
                {
                    b->TransformByObjectBone(Position, draw, WorldRandom() % b->NumBones, p);
                    CreateParticle(BITMAP_FIRE, Position, draw.angle, Light);
                }
            }

            Vector(Luminosity * 1.f, Luminosity * 0.2f, Luminosity * 0.f, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
        }
        break;
    case MODEL_ICE_MONSTER: {
        visual.movement.materialFields |= CharacterMovementVisual::V;
        visual.movement.blendV = -(float)((int)(WorldTime) % 2000) * 0.0005f;
    }
    break;
    case MODEL_MIX_NPC:
        if (rand_fps_check(64))
            PlayBuffer(SOUND_NPC_MIX);
        break;
    case MODEL_ELF_WIZARD:
        if (rand_fps_check(256))
            PlayBuffer(SOUND_NPC_HARP);
        break;
    case MODEL_SMITH:
        if (g_isCharacterBuff(o, eBuff_CrywolfNPCHide))
            break;
        if (visual.action == 0 && visual.animationFrame >= 5.f && visual.animationFrame <= 10.f)
            PlayBuffer(SOUND_NPC_BLACK_SMITH);
        {
            visual.movement.materialFields |= CharacterMovementVisual::Mesh;
            visual.movement.blendMesh = 4;
        }
        {
            visual.movement.materialFields |= CharacterMovementVisual::Brightness;
            visual.movement.blendLight = Luminosity;
        }
        Vector(Luminosity * 1.f, Luminosity * 0.4f, Luminosity * 0.f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
        Vector(1.f, 1.f, 1.f, Light);
        Vector(0.f, 0.f, 0.f, p);
        if (visual.action == 0 && visual.animationFrame >= 5.f && visual.animationFrame <= 6.f)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                b->TransformByObjectBone(Position, draw, 17, p);
                vec3_t Angle;
                for (int i = 0; i < 4; i++)
                {
                    Vector((float)(WorldRandom() % 60 + 60 + 90), 0.f, (float)(WorldRandom() % 30),
                           Angle);
                    CreateJoint(BITMAP_JOINT_SPARK, Position, Position, Angle);
                    CreateParticle(BITMAP_SPARK, Position, Angle, Light);
                }
            }
        }
        break;
    case MODEL_DEVIAS_TRADER:
        Vector(1.f, 1.f, 1.f, Light);
        Vector(0.f, 5.f, 10.f, p);
        if (visual.action == 0)
        {
            Vector(Luminosity * 0.5f, Luminosity * 0.3f, Luminosity * 0.f, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
            b->TransformPosition(bones[37], p, Position, true);
            vec3_t Angle;
            for (int i = 0; i < 4; i++)
            {
                for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
                {
                    auto draw = sampleEmitter(birth.FrameFraction(), true);
                    b->TransformByObjectBone(Position, draw, 37, p);
                    Vector((float)(WorldRandom() % 60 + 60 + 30), 0.f, (float)(WorldRandom() % 30),
                           Angle);
                    CreateJoint(BITMAP_JOINT_SPARK, Position, Position, Angle);
                    if (WorldRandom() % 2)
                        CreateParticle(BITMAP_SPARK, Position, Angle, Light);
                }
            }
        }
        break;
    case MODEL_WEDDING_NPC:
        if (visual.action == 1 && (visual.animationFrame > 4.5f && visual.animationFrame <= 4.8f))
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), false);
                CreateEffect(BITMAP_FIRECRACKER0001, draw.position, draw.angle,
                             visual.movement.light, 0);
            }
        }
        break;
    case MODEL_BUDGE_DRAGON:
        if (visual.action == MONSTER01_ATTACK1 && visual.animationFrame <= 4.f)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                vec3_t Light;
                Vector(1.f, 1.f, 1.f, Light);
                Vector(0.f, (float)(WorldRandom() % 32 + 32), 0.f, p);
                b->TransformByObjectBone(Position, draw, 7, p);
                CreateParticle(BITMAP_FIRE, Position, draw.angle, Light, 1);
            }
        }
    case MODEL_DARK_KNIGHT:
    case MODEL_LARVA:
    case MODEL_CHAIN_SCORPION:
        if (o->Type == MODEL_CHAIN_SCORPION)
        {
            Vector(0.f, 0.f, 0.f, p);
            b->TransformPosition(bones[7], p, Position, true);
            Vector(Luminosity * 1.f, Luminosity * 0.4f, Luminosity * 0.2f, Light);
            CreateSprite(BITMAP_LIGHT, Position, 1.f, Light, o);
        }
        if (c->Dead == 0)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), false);
                Vector(draw.position[0] + (float)(WorldRandom() % 64 - 32),
                       draw.position[1] + (float)(WorldRandom() % 64 - 32),
                       draw.position[2] + (float)(WorldRandom() % 32 - 16), Position);
                if (TheMapProcess().CharacterPolicy().snowFootsteps)
                    CreateParticle(BITMAP_SMOKE, Position, draw.angle, Light);
                else
                    CreateParticle(BITMAP_SMOKE + 1, Position, draw.angle, Light);
            }
        }
        break;
    case MODEL_BAHAMUT:
        if (c->Dead == 0 && c->Level == 1)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), false);
                Vector(draw.position[0] + (float)(WorldRandom() % 64 - 32),
                       draw.position[1] + (float)(WorldRandom() % 64 - 32),
                       draw.position[2] + (float)(WorldRandom() % 32 - 16), Position);
                CreateParticle(BITMAP_SMOKE + 1, Position, draw.angle, Light);
            }
        }
        break;
    case MODEL_GIANT:
        MonsterDieSandSmoke(o, visual);
        break;
    case MODEL_YETI:
    case MODEL_ELITE_YETI:
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.f))
        {
            auto draw = sampleEmitter(birth.FrameFraction(), true);
            Vector(0.f, 0.f, 0.f, p);
            b->TransformByObjectBone(Position, draw, 22, p);
            CreateParticle(BITMAP_SMOKE, Position, draw.angle, visual.movement.light);
        }
        break;
    case MODEL_BULL_FIGHTER:
        if (visual.action == MONSTER01_STOP1 &&
            (visual.animationFrame >= 15.f && visual.animationFrame <= 20.f))
            Smoke = true;
        if (visual.action == MONSTER01_STOP2 &&
            (visual.animationFrame >= 20.f && visual.animationFrame <= 25.f))
            Smoke = true;
        if (visual.action == MONSTER01_WALK &&
            ((visual.animationFrame >= 2.f && visual.animationFrame <= 3.f) ||
             (visual.animationFrame >= 5.f && visual.animationFrame <= 6.f)))
            Smoke = true;
        if (Smoke)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                Vector(0.f, -4.f, 0.f, p);
                b->TransformByObjectBone(Position, draw, 24, p);
                CreateParticle(BITMAP_SMOKE, Position, draw.angle, visual.movement.light);
            }
        }
        break;
    default: {
        TheMapProcess().MoveMonsterVisual(c, o, b, visual);
    }
    break;
    }

    if (o->Type == MODEL_PLAYER && c == Hero)
    {
        if ((visual.action >= PLAYER_WALK_MALE && visual.action <= PLAYER_RUN_RIDE_WEAPON) ||
            (visual.action == PLAYER_WALK_TWO_HAND_SWORD_TWO ||
             visual.action == PLAYER_RUN_TWO_HAND_SWORD_TWO) ||
            (visual.action == PLAYER_RUN_RIDE_HORSE) ||
            (visual.action == PLAYER_RAGE_UNI_RUN ||
             visual.action == PLAYER_RAGE_UNI_RUN_ONE_RIGHT))
        {
            Vector(0.f, 0.f, 0.f, p);
            if (visual.animationFrame >= 1.5f && !visual.movement.foot[0])
            {
                visual.movement.foot[0] = true;
                PlayWalkSound();
            }
            if (visual.animationFrame >= 4.5f && !visual.movement.foot[1])
            {
                visual.movement.foot[1] = true;
                PlayWalkSound();
            }
        }
    }

    if ((visual.action == PLAYER_RUN_RIDE || visual.action == PLAYER_RAGE_UNI_RUN ||
         visual.action == PLAYER_RAGE_UNI_RUN_ONE_RIGHT ||
         visual.action == PLAYER_RUN_RIDE_WEAPON || visual.action == PLAYER_RUN_SWIM ||
         visual.action == PLAYER_WALK_SWIM || visual.action == PLAYER_FLY ||
         visual.action == PLAYER_FLY_CROSSBOW || visual.action == PLAYER_RUN_RIDE_HORSE) &&
        o->Type == MODEL_PLAYER && gMapManager.InHellas())
    {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
        {
            auto draw = sampleEmitter(birth.FrameFraction(), false);
            vec3_t Light = {0.3f, 0.3f, 0.3f};
            VectorCopy(draw.position, Position);

            float Matrix[3][4];

            Vector(0.f, -40.f, 0.f, p);

            AngleMatrix(draw.angle, Matrix);
            VectorRotate(p, Matrix, Position);
            VectorAdd(draw.position, Position, Position);

            Position[0] += WorldRandom() % 64 - 32.f;
            Position[1] += WorldRandom() % 64 - 32.f;
            Position[2] += 50.f;

            CreateParticle(BITMAP_WATERFALL_5, Position, draw.angle, Light, 1);
        }
    }
}

void SessionGameplayUnit::AdvanceCharacterPresentationState(CHARACTER &character)
{
    auto *c = &character;
    auto *o = &character.Object;
    if ((o->Type == MODEL_DOPPELGANGER_NPC_BOX || o->Type == MODEL_DOPPELGANGER_NPC_GOLDENBOX) &&
        o->CurrentAction == MONSTER01_DIE)
        o->AnimationFrame = (std::min)(o->AnimationFrame, 9.f);
    const bool structureDeath =
        (o->Type == MODEL_CASTLE_GATE || o->Type == MODEL_STATUE_OF_SAINT) &&
        o->CurrentAction == MONSTER01_DIE;
    character.WorldVisualStructureDeath = structureDeath;
    if (o->Type == MODEL_FIRE_FLAME_GHOST && o->CurrentAction == MONSTER01_DIE)
        o->m_bRenderShadow = false;
    character.WorldVisualSnowmanDeath = o->Type == MODEL_XMAS2008_SNOWMAN &&
                                        o->CurrentAction == MONSTER01_DIE &&
                                        static_cast<int>(o->LifeTime) == 100;
    if (character.WorldVisualSnowmanDeath)
    {
        o->LifeTime = 90.f;
        o->m_bRenderShadow = false;
    }
    const bool darkHorse = character.MountState.type == MODEL_DARK_HORSE ||
                           character.Helper.Type == MODEL_DARK_HORSE_ITEM;
    character.WorldVisualMountBurst = darkHorse && o->ExtState == 1;
    if (darkHorse && o->ExtState != 0)
        o->ExtState = 0;

    if (o->SubType != MODEL_HALLOWEEN)
    {
        character.WorldVisualHalloweenProgress = 0.f;
        character.WorldVisualHalloweenBurst = false;
        character.WorldVisualHalloweenSparks = 0;
    }
    CreatePartsFactory(c);
    c->PostMoveProcess_Process();
    if (!sessionKeeper_.Visual()->CharacterWeaponsOnBack(character) &&
        !(o->CurrentAction == PLAYER_ATTACK_SKILL_FURY_STRIKE && o->AnimationFrame <= 4.f) &&
        c->PostMoveProcess_IsProcessing())
        c->Skill = 0;

    if (o->Type == MODEL_PLAYER)
    {
        switch (o->SubType)
        {
        case MODEL_HALLOWEEN:
            AdvanceHalloweenFormState(character);
            break;
        case MODEL_XMAS_EVENT_CHA_SSANTA:
        case MODEL_XMAS_EVENT_CHA_SNOWMAN:
        case MODEL_XMAS_EVENT_CHA_DEER: {
            const auto &owner = *o->Owner;
            o->Live = owner.Live;
            VectorCopy(owner.Position, o->Position);
            VectorCopy(owner.Angle, o->Angle);
            o->PriorAction = owner.PriorAction;
            o->PriorAnimationFrame = owner.PriorAnimationFrame;
            o->CurrentAction = owner.CurrentAction;
            o->AnimationFrame = owner.AnimationFrame;
            break;
        }
        case MODEL_XMAS_EVENT_CHANGE_GIRL:
            if (o->m_iAnimation >= 1)
            {
                o->m_iAnimation = 0;
                SetPlayerStop(c);
            }
            else if ((o->CurrentAction == PLAYER_SANTA_1 && o->AnimationFrame >= 19.f) ||
                     (o->CurrentAction == PLAYER_SANTA_2 && o->AnimationFrame >= 14.f))
            {
                // Publish the final Santa sample; stop on the next source tick.
                o->m_iAnimation = 1;
            }
            break;
        case MODEL_PANDA:
        case MODEL_SKELETON_CHANGED:
            if (o->m_iAnimation >= 1)
            {
                o->m_iAnimation = 0;
                SetPlayerStop(c);
            }
            break;
        }
    }

    if (gMapManager.InBloodCastle() && o->m_bActionStart && c->Dead > 0 &&
        (o->Type == MODEL_PLAYER ||
         (o->Alpha >= 0.3f && CharacterPresentationDetail::CharacterUsesGroundShadow(
                                  character, TheMapProcess().CharacterPolicy().skyTerrain))))
        o->Position[2] =
            (std::min)(o->Position[2], RequestTerrainHeight(o->Position[0], o->Position[1]));

    if (c->MonsterIndex < MONSTER_TERRIBLE_BUTCHER || c->MonsterIndex > MONSTER_DOPPELGANGER_SUM)
        return;
    if (!TheMapProcess().CharacterPolicy().cloneAppearances)
    {
        constexpr float deathFadePerTick = 0.07f;
        o->Alpha = o->CurrentAction == PLAYER_DIE1
                       ? (std::max)(0.f, o->Alpha - deathFadePerTick * FPS_ANIMATION_FACTOR)
                       : 1.f;
    }
    if (g_isCharacterBuff(o, eBuff_Doppelganger_Ascension))
    {
        constexpr float ascensionHeightPerTick = 2.f;
        o->Position[2] += ascensionHeightPerTick * FPS_ANIMATION_FACTOR;
    }
}

void SessionGameplayUnit::RefreshPendingCharacterPose(CHARACTER &character)
{
    if (character.Object.EnableBoneMatrix && character.WorldVisualPoseRevision != 0)
        return;
    auto &model = Models[character.Object.Type];
    if (!character.Object.Live || model.NumBones == 0 || model.NumActions == 0)
        return;
    BindCharacterTarget(character);
    CharacterPresentationDetail::CaptureCharacterPoseInputs(character);
    CharacterPresentationDetail::PublishCharacterPose(character, model);
}

void SessionGameplayUnit::RefreshLocalCharacterPose(CHARACTER &character)
{
    auto &model = Models[character.Object.Type];
    if (!character.Object.Live || model.NumBones == 0 || model.NumActions == 0)
        return;
    BindCharacterTarget(character);
    CharacterPresentationDetail::CaptureCharacterPoseInputs(character);
    CharacterPresentationDetail::PublishCharacterPose(character, model);
}

void SessionGameplayUnit::PrepareLocalCharacterPose(CHARACTER &character)
{
    auto &model = Models[character.Object.Type];
    if (!character.Object.Live || model.NumBones == 0 || model.NumActions == 0)
        return;
    AdvanceCharacterPresentationState(character);
    if (!character.Object.Live)
        return;
    BindCharacterTarget(character);
    CharacterPresentationDetail::CaptureCharacterPoseInputs(character);
    CharacterPresentationDetail::PublishCharacterPose(character, model);
    if (character.WorldVisualStructureDeath)
        character.Object.Live = false;
    character.WorldVisualAnimationFactor = FPS_ANIMATION_FACTOR;
    ++character.WorldVisualTick;
}

void SessionGameplayUnit::UpdateCharactersAnimationParallel(std::span<CHARACTER *const> characters)
{
    CharactersClient.mapHitTargets.clear();
    for (std::size_t index = 0; index < characters.size(); ++index)
    {
        CHARACTER *c = characters[index];
        if (c == nullptr)
            continue;

        const int characterIndex = static_cast<int>(index);
        auto sharedCharacterAccess = CharactersClient.AcquireSharedAccess(characterIndex);
        if (!CharactersClient.IsSource(characterIndex))
            continue;

        c->Object.EnableBoneMatrix = false;
        if (!c->Object.Live)
            continue;
        if (SceneFlag == CHARACTER_SCENE && characterIndex < MAX_CHARACTERS_PER_ACCOUNT)
            c->Object.Position[2] = 163.f;
        AdvanceCharacterPresentationState(*c);
        if (!c->Object.Live)
            continue;

        OBJECT *o = &c->Object;
        BMD *model = &Models[o->Type];
        if (model->NumBones <= 0)
            continue;

        BindCharacterTarget(*c);
        CharacterPresentationDetail::CaptureCharacterPoseInputs(*c);
        sessionKeeper_.Gameplay()->AdvanceMapCharacterTransitions(*c, *model);
        CharacterPresentationDetail::PublishCharacterPose(*c, *model);
        TheMapProcess().CaptureMonsterAttackState(*c, *model);
        TheMapProcess().AdvanceObjectFade(*o);
        c->WorldVisualAnimationFactor = FPS_ANIMATION_FACTOR;
        ++c->WorldVisualTick;
    }
    // Never hold an attacker lease while acquiring a target lease.
    for (const int target : CharactersClient.mapHitTargets)
    {
        if (!CharactersClient.IsValidIndex(target))
            continue;
        auto targetLease = CharactersClient.AcquireSharedAccess(target);
        auto &object = CharactersClient[target].Object;
        if (object.Live)
            object.m_byHurtByDeathstab = 35;
    }
}

void SessionVisualUnit::RenderBrightEffect(BMD *b, int Bitmap, int Link, float Scale, vec3_t Light,
                                           OBJECT *o)
{
    vec3_t p, Position;
    Vector(0.f, 0.f, 0.f, p);
    b->TransformPosition(BoneTransform[Link], p, Position, true);
    CreateSprite(Bitmap, Position, Scale, Light, o);
}

bool SessionVisualUnit::PrepareCharacterItemVisualPose(CHARACTER &character, const PART_t &part,
                                                       const CharacterLinkedItemVisual *prepared)
{
    auto &model = Models[part.Type];
    if (model.NumBones == 0 || model.NumActions == 0)
        return false;
    vec3_t angle;
    BuildCharacterItemParent(0.f, 0.f, 0.f, CharacterPresentationInput(character), part, part.Type,
                             false, false, ParentMatrix, model.BodyOrigin, angle, model.BodyScale);
    model.CurrentAction = part.CurrentAction;
    model.BodyHeight = 0.f;
    ObjectDrawInput draw(&character.Object);
    draw.type = part.Type;
    draw.action = part.CurrentAction;
    draw.animationFrame = part.AnimationFrame;
    draw.priorAnimationFrame = part.PriorAnimationFrame;
    draw.priorAction = part.PriorAction;
    VectorCopy(angle, draw.angle);
    VectorCopy(angle, draw.headAngle);
    AnimationPoseSample sample(draw, model.BoneHead, 0.f, true, model.PoseAssetIdentity());
    sample.SetParent(ParentMatrix);
    if (prepared && prepared->poseSample == sample)
        memcpy(BoneTransform, prepared->item.BoneTransform, model.NumBones * sizeof(vec34_t));
    else
        sample.Evaluate(model, BoneTransform);
    if (part.Type == MODEL_FLAIL &&
        g_CMonkSystem.IsRagefighterCommonWeapon(character.Class, part.Type))
        model.InterpolationTrans(BoneTransform[0], BoneTransform[2], 0.9f);
    return true;
}

void SessionVisualUnit::AdvanceWeaponSounds(CHARACTER &character,
                                            const WorldCharacterVisualState &visual)
{
    const bool onBack = CharacterWeaponsOnBack(character);
    for (int slot = 0; slot < 2; ++slot)
    {
        const auto &weapon = character.Weapon[slot];
        if (weapon.Type != MODEL_PHOENIX_SOUL_STAR)
            continue;
        if (!onBack && slot == 0 &&
            ((character.Object.CurrentAction == PLAYER_ATTACK_SKILL_FURY_STRIKE &&
              character.Object.AnimationFrame <= 4.f) ||
             character.PostMoveProcess_IsProcessing()))
            continue;
        const auto found = visual.linkedItems.find(CharacterLinkedItemVisual::RightPhoenix + slot);
        const float frame =
            found != visual.linkedItems.end() ? found->second->playback.PriorAnimationFrame : 0.f;
        if (onBack || frame < 2.f)
            PlayBuffer(SOUND_EMPIREGUARDIAN_DEFENDER_ATTACK02);
    }
}

void SessionVisualUnit::AdvanceWeaponVisual(CHARACTER &character,
                                            const WorldCharacterVisualState &visual)
{
    if (CharacterWeaponsOnBack(character))
        return;
    auto *c = &character;
    auto *o = &character.Object;
    auto *b = &Models[o->Type];
    vec3_t p{}, Position{}, Light{};
    float Luminosity = static_cast<float>(WorldRandom() % 30 + 70) * 0.01f;
    for (int i = 0; i < 2; ++i)
    {
        if (i == 0 &&
            ((o->CurrentAction == PLAYER_ATTACK_SKILL_FURY_STRIKE && o->AnimationFrame <= 4.f) ||
             c->PostMoveProcess_IsProcessing()))
            continue;
        const auto found = visual.linkedItems.find(i);
        const auto *w =
            found != visual.linkedItems.end() ? &found->second->playback : &c->Weapon[i];
        if (w->Type == -1 || w->Type == MODEL_BOLT || w->Type == MODEL_ARROWS ||
            w->Type == MODEL_DARK_RAVEN_ITEM)
            continue;
        if (!PrepareCharacterItemVisualPose(
                character, *w, found != visual.linkedItems.end() ? found->second.get() : nullptr))
            continue;
        if (w->Level >= 7)
        {
            Vector(Luminosity * 0.5f, Luminosity * 0.4f, Luminosity * 0.3f, Light);
        }
        else if (w->Level >= 5)
        {
            Vector(Luminosity * 0.3f, Luminosity * 0.3f, Luminosity * 0.5f, Light);
        }
        else if (w->Level >= 3)
        {
            Vector(Luminosity * 0.5f, Luminosity * 0.3f, Luminosity * 0.3f, Light);
        }
        else
        {
            Vector(Luminosity * 0.3f, Luminosity * 0.3f, Luminosity * 0.3f, Light);
        }
        float Scale;
        if (c->PK < PVP_MURDERER2 && c->Level != 4)
        {
            bool Success = true;

            switch (w->Type)
            {
            case MODEL_SWORD_OF_ASSASSIN:
            case MODEL_BLADE:
            case MODEL_DOUBLE_BLADE:
                Vector(0.f, -110.f, 5.f, Position);
                break;
            case MODEL_SERPENT_SWORD:
            case MODEL_SWORD_OF_SALAMANDER:
                Vector(0.f, -110.f, -5.f, Position);
                break;
            case MODEL_FALCHION:
            case MODEL_LIGHT_SABER:
                Vector(0.f, -110.f, 0.f, Position);
                break;
            case MODEL_LIGHTING_SWORD:
            case MODEL_LEGENDARY_SWORD:
                Vector(0.f, -150.f, 0.f, Position);
                break;
            case MODEL_HELIACAL_SWORD:
                Vector(Luminosity, Luminosity, Luminosity, Light);
                Vector(0.f, -160.f, 0.f, Position);
                break;
            case MODEL_DARK_BREAKER:
                Success = false;
                Scale = sinf(WorldTime * 0.004f) * 10.f + 20.f;
                {
                    vec3_t pos1, pos2;

                    Vector(0.f, -20.f, -40.f, Position);
                    Models[o->Type].TransformPosition(o->BoneTransform[w->LinkBone], Position, pos1,
                                                      true);
                    Vector(0.f, -160.f, -10.f, Position);
                    Models[o->Type].TransformPosition(o->BoneTransform[w->LinkBone], Position, pos2,
                                                      true);
                    CreateJoint(BITMAP_FLARE + 1, pos1, pos2, o->Angle, 4, o, Scale);

                    Vector(0.f, -10.f, 28.f, Position);
                    Models[o->Type].TransformPosition(o->BoneTransform[w->LinkBone], Position, pos1,
                                                      true);
                    Vector(0.f, -145.f, 18.f, Position);
                    Models[o->Type].TransformPosition(o->BoneTransform[w->LinkBone], Position, pos2,
                                                      true);
                    CreateJoint(BITMAP_FLARE + 1, pos1, pos2, o->Angle, 4, o, Scale);
                }
                break;

            case MODEL_THUNDER_BLADE: {
                Success = false;

                Vector(0.f, -20.f, 15.f, Position);
                Models[o->Type].TransformPosition(o->BoneTransform[w->LinkBone], Position, p, true);

                Scale = sinf(WorldTime * 0.004f) * 0.3f + 0.3f;
                Vector(Scale * 0.2f, Scale * 0.2f, Scale * 1.f, Light);
                CreateSprite(BITMAP_SHINY + 1, p, Scale + 1.f, Light, o, 0);

                Vector(0.f, -133.f, 7.f, Position);
                Models[o->Type].TransformPosition(o->BoneTransform[w->LinkBone], Position, Position,
                                                  true);

                Scale = (Scale * 20.f) + 20.f;
                CreateJoint(BITMAP_JOINT_THUNDER, p, Position, o->Angle, 10, NULL, Scale);
                CreateJoint(BITMAP_JOINT_THUNDER, p, Position, o->Angle, 10, NULL, Scale);
                CreateJoint(BITMAP_JOINT_THUNDER, p, Position, o->Angle, 10, NULL, Scale);
            }
            break;
            case MODEL_DRAGON_SOUL_STAFF:
                Success = false;
                Vector(0.f, -120.f, 5.f, Position);
                Vector(Luminosity * 0.6f, Luminosity * 0.6f, Luminosity * 2.f, Light);
                Models[o->Type].TransformPosition(o->BoneTransform[w->LinkBone], Position, p, true);
                CreateSprite(BITMAP_SHINY + 1, p, 1.5f, Light, o);
                CreateSprite(BITMAP_LIGHT, p, Luminosity + 1.f, Light, o);

                Vector(0.f, 100.f, 10.f, Position);
                Models[o->Type].TransformPosition(o->BoneTransform[w->LinkBone], Position, p, true);
                CreateSprite(BITMAP_LIGHT, p, Luminosity + 1.f, Light, o);
                break;
            case MODEL_GORGON_STAFF:
                Success = false;
                Vector(0.f, -90.f, 0.f, Position);
                Vector(Luminosity * 0.4f, Luminosity * 0.8f, Luminosity * 0.6f, Light);
                Models[o->Type].TransformPosition(o->BoneTransform[w->LinkBone], Position, p, true);
                CreateSprite(BITMAP_SHINY + 1, p, 2.f, Light, o);
                break;
            case MODEL_LEGENDARY_SHIELD:
                Success = false;
                Vector(20.f, 0.f, 0.f, Position);
                Vector(Luminosity * 0.4f, Luminosity * 0.6f, Luminosity * 1.5f, Light);
                Models[o->Type].TransformPosition(o->BoneTransform[w->LinkBone], Position, p, true);
                CreateSprite(BITMAP_SHINY + 1, p, 1.5f, Light, o);
                break;
            case MODEL_LEGENDARY_STAFF:
                Success = false;
                Vector(0.f, -145.f, 0.f, Position);
                Vector(Luminosity * 0.4f, Luminosity * 0.6f, Luminosity * 1.f, Light);
                Models[o->Type].TransformPosition(o->BoneTransform[w->LinkBone], Position, p, true);
                CreateSprite(BITMAP_SHINY + 1, p, 1.5f, Light, o);
                CreateSprite(BITMAP_LIGHTNING + 1, p, 0.3f, Light, o);
                break;
            case MODEL_CHAOS_LIGHTNING_STAFF:
                Success = false;
                Vector(Luminosity * 0.4f, Luminosity * 0.6f, Luminosity * 1.f, Light);
                RenderBrightEffect(b, BITMAP_SHINY + 1, 27, 2.f, Light, o);

                for (int j = 28; j <= 37; j++)
                {
                    RenderBrightEffect(b, BITMAP_LIGHT, j, 1.5f, Light, o);
                }
                break;
            case MODEL_STAFF_OF_RESURRECTION:
                Success = false;
                Vector(0.f, -145.f, 0.f, Position);
                Models[o->Type].TransformPosition(o->BoneTransform[w->LinkBone], Position, p, true);
                Vector(Luminosity * 1.f, Luminosity * 0.6f, Luminosity * 0.4f, Light);
                CreateSprite(BITMAP_SPARK, p, 3.f, Light, o);
                CreateSprite(BITMAP_SHINY + 2, p, 1.5f, Light, o);

                for (int j = 0; j < 4; j++)
                {
                    Vector((float)(WorldRandom() % 20 - 10),
                           (float)(WorldRandom() % 20 - 10 - 90.f),
                           (float)(WorldRandom() % 20 - 10), Position);
                    Models[o->Type].TransformPosition(o->BoneTransform[w->LinkBone], Position, p,
                                                      true);
                    if (rand_fps_check(1))
                    {
                        CreateParticle(BITMAP_SPARK, p, o->Angle, Light, 1);
                    }
                }
                Vector(Luminosity * 1.f, Luminosity * 0.2f, Luminosity * 0.1f, Light);

                for (int j = 0; j < 10; j++)
                {
                    if (WorldRandom() % 4 < 3)
                    {
                        Vector(0.f, -j * 20 + 60.f, 0.f, Position);
                        Models[o->Type].TransformPosition(o->BoneTransform[w->LinkBone], Position,
                                                          p, true);
                        CreateSprite(BITMAP_LIGHT, p, 1.f, Light, o);
                    }
                }
                break;
            case MODEL_VIOLENT_WIND_STICK:
                Success = false;
                Vector(Luminosity * 0.2f, Luminosity * 0.3f, Luminosity * 1.4f, Light);
                Vector(0.f, 0.f, 0.f, Position);
                b->TransformPosition(BoneTransform[1], Position, p, true);
                CreateSprite(BITMAP_SHINY + 1, p, 1.f, Light, o, -(int)WorldTime * 0.1f);
                b->TransformPosition(BoneTransform[2], Position, p, true);
                CreateSprite(BITMAP_SHINY + 1, p, 0.5f, Light, o, (int)WorldTime * 0.1f);
                break;
            case MODEL_RED_WING_STICK:
                Success = false;
                Vector(Luminosity * 1.0f, Luminosity * 0.3f, Luminosity * 0.4f, Light);
                Vector(0.f, 0.f, 0.f, Position);
                b->TransformPosition(BoneTransform[1], Position, p, true);
                CreateSprite(BITMAP_SHINY + 1, p, 1.f, Light, o, -(int)WorldTime * 0.1f);
                CreateSprite(BITMAP_SHINY + 1, p, 1.f, Light, o, -(int)WorldTime * 0.13f);
                break;
            case MODEL_ANCIENT_STICK:
                Success = false;
                Scale = absf(sinf(WorldTime * 0.002f)) * 0.2f;
                Luminosity = absf(sinf(WorldTime * 0.002f)) * 0.4f;
                Vector(0.5f + Luminosity, 0.2f + Luminosity, 0.9f + Luminosity, Light);

                for (int j = 1; j <= 4; ++j)
                    RenderBrightEffect(b, BITMAP_LIGHT, j, Scale + 1.0f, Light, o);
                break;
            case MODEL_DEMONIC_STICK:
                Success = false;
                Scale = absf(sinf(WorldTime * 0.002f)) * 0.2f;
                Luminosity = absf(sinf(WorldTime * 0.002f)) * 0.4f;
                Vector(0.5f + Luminosity, 0.2f + Luminosity, 0.9f + Luminosity, Light);

                for (int j = 1; j <= 2; ++j)
                {
                    RenderBrightEffect(b, BITMAP_SHINY + 1, j, Scale + 1.0f, Light, o);
                    RenderBrightEffect(b, BITMAP_LIGHT, j, Scale + 1.0f, Light, o);
                }
                Vector(0.8f + Luminosity, 0.6f + Luminosity, 0.3f + Luminosity, Light);
                RenderBrightEffect(b, BITMAP_LIGHT, 3, Scale + 1.0f, Light, o);
                break;
            case MODEL_STORM_BLITZ_STICK:
                Success = false;
                Scale = absf(sinf(WorldTime * 0.002f)) * 0.2f;
                Luminosity = absf(sinf(WorldTime * 0.002f)) * 0.4f;
                Vector(0.5f + Luminosity, 0.2f + Luminosity, 0.9f + Luminosity, Light);

                for (int j = 2; j <= 3; ++j)
                    RenderBrightEffect(b, BITMAP_LIGHT, j, Scale + 1.0f, Light, o);

                Vector(0.8f + Luminosity, 0.6f + Luminosity, 0.3f + Luminosity, Light);
                RenderBrightEffect(b, BITMAP_SHINY + 2, 2, Scale + 1.0f, Light, o);
                break;
            case MODEL_ETERNAL_WING_STICK:
                Success = false;
                Vector(1.0f, 0.2f, 0.1f, Light);
                Vector(0.f, 0.f, 0.f, Position);
                b->TransformPosition(BoneTransform[2], Position, p, true);
                CreateSprite(BITMAP_EVENT_CLOUD, p, 0.25f, Light, o, -(int)WorldTime * 0.1f);
                CreateSprite(BITMAP_EVENT_CLOUD, p, 0.25f, Light, o, -(int)WorldTime * 0.2f);
                Vector(1.0f, 0.4f, 0.3f, Light);
                RenderBrightEffect(b, BITMAP_SHINY + 1, 2, 1.0f, Light, o);
                Vector(1.0f, 0.2f, 0.0f, Light);
                if (rand_fps_check(1))
                {
                    CreateParticle(BITMAP_SPARK + 1, p, o->Angle, Light, 16, 1.0f);
                    CreateParticle(BITMAP_SPARK + 1, p, o->Angle, Light, 23, 1.0f);
                }

#ifdef ASG_ADD_ETERNALWING_STICK_EFFECT
                if (rand_fps_check(20))
                    CreateParticle(BITMAP_SPARK + 1, p, o->Angle, Light, 20, 1.0f);
                Vector(1.0f, 0.0f, 0.0f, Light);
                RenderBrightEffect(b, BITMAP_LIGHT, 2, 3.0f, Light, o);
#endif // ASG_ADD_ETERNALWING_STICK_EFFECT
                break;
            case MODEL_SAINT_CROSSBOW:
                Success = false;
                Vector(Luminosity * 0.4f, Luminosity * 0.6f, Luminosity * 1.f, Light);

                for (int j = 0; j < 6; j++)
                {
                    Vector(0.f, -10.f, -j * 20.f, Position);
                    Models[o->Type].TransformPosition(o->BoneTransform[w->LinkBone], Position, p,
                                                      true);
                    CreateSprite(BITMAP_LIGHT, p, 2.f, Light, o);
                }
                break;
            case MODEL_CRYSTAL_SWORD:
                Success = false;
                Vector(Luminosity * 1.f, Luminosity * 0.6f, Luminosity * 0.4f, Light);

                for (int j = 0; j < 8; j++)
                {
                    if (WorldRandom() % 4 < 3)
                    {
                        Vector(0.f, -j * 20 - 30.f, 0.f, Position);
                        Models[o->Type].TransformPosition(o->BoneTransform[w->LinkBone], Position,
                                                          p, true);
                        CreateSprite(BITMAP_LIGHT, p, 1.f, Light, o);
                    }
                }
                break;
            case MODEL_CHAOS_DRAGON_AXE:
                Success = false;
                Vector(0.f, -84.f, 0.f, Position);
                Models[o->Type].TransformPosition(o->BoneTransform[w->LinkBone], Position, p, true);
                Scale = sinf(WorldTime * 0.004f) * 0.3f + 0.7f;
                Vector(Scale * 1.f, Scale * 0.2f, Scale * 0.1f, Light);
                CreateSprite(BITMAP_SHINY + 1, p, Scale + 1.5f, Light, o);

                for (int j = 0; j < 5; j++)
                {
                    Vector(0.f, -j * 20.f - 10.f, 0.f, Position);
                    Models[o->Type].TransformPosition(o->BoneTransform[w->LinkBone], Position, p,
                                                      true);
                    CreateSprite(BITMAP_SHINY + 1, p, 1.f, Light, o);
                }
                Vector(Scale * 0.5f, Scale * 0.1f, Scale * 0.05f, Light);
                RenderBrightEffect(b, BITMAP_SHINY + 1, 2, 1.f, Light, o);
                RenderBrightEffect(b, BITMAP_SHINY + 1, 6, 1.f, Light, o);
                break;
            case MODEL_BLUEWING_CROSSBOW:
            case MODEL_AQUAGOLD_CROSSBOW:
                Success = false;
                if (w->Type == MODEL_BLUEWING_CROSSBOW)
                {
                    Vector(Luminosity * 0.2f, Luminosity * 0.4f, Luminosity * 0.6f, Light);
                }
                else
                {
                    Vector(Luminosity * 0.6f, Luminosity * 0.4f, Luminosity * 0.2f, Light);
                }

                for (int j = 0; j < 6; j++)
                {
                    Vector(0.f, -20.f, (float)(-j * 20), Position);
                    Models[o->Type].TransformPosition(o->BoneTransform[w->LinkBone], Position, p,
                                                      true);
                    CreateSprite(BITMAP_LIGHT, p, 2.f, Light, o);
                }
                break;
            default:
                Success = false;
            }
            if (Success)
            {
                Models[o->Type].TransformPosition(o->BoneTransform[w->LinkBone], Position, p, true);
                CreateSprite(BITMAP_LIGHT, p, 1.4f, Light, o);
            }
        }
    }
}

void SessionVisualUnit::BuildCharacterItemParent(float x, float y, float z,
                                                 const CharacterDrawInput &character,
                                                 const PART_t &part, int Type, bool Link,
                                                 bool bRightHandItem, float (&parent)[3][4],
                                                 vec3_t origin, vec3_t angle, float &scale,
                                                 float requestedScale)
{
    const auto &preparedCharacter = character;
    const auto *c = character.source;
    const auto *o = preparedCharacter.object.source;
    const auto *f = &part;
    scale = requestedScale > 0.f ? requestedScale : preparedCharacter.object.scale;
    vec3_t p, Position;
    if (Link)
    {
        vec3_t Angle;
        float Matrix[3][4];

        if (c->MonsterIndex >= MONSTER_STATUE_OF_SAINT_1 &&
            c->MonsterIndex <= MONSTER_STATUE_OF_SAINT_3)
        {
            if (Type == MODEL_DIVINE_STAFF_OF_ARCHANGEL || Type == MODEL_DIVINE_SWORD_OF_ARCHANGEL)
            {
                Vector(90.f, 0.f, 90.f, Angle);
                AngleMatrix(Angle, Matrix);
                Matrix[0][3] = 0.f;
                Matrix[1][3] = 80.f;
                Matrix[2][3] = 120.f;
            }
            else if (Type == MODEL_DIVINE_CB_OF_ARCHANGEL)
            {
                Vector(10.f, 0.f, 0.f, Angle);
                AngleMatrix(Angle, Matrix);
                Matrix[0][3] = 0.f;
                Matrix[1][3] = 110.f;
                Matrix[2][3] = 80.f;
            }
        }
        else if (Type == MODEL_SWORD_35_WING)
        {
            Vector(x, y, z, Angle);
            AngleMatrix(Angle, Matrix);
            Matrix[0][3] = 15.f;
            Matrix[1][3] = -5.f;
            Matrix[2][3] = 0.f;
        }
        else if (Type == MODEL_CAPE_OF_OVERRULE)
        {
            Vector(0.f, 90.f, 0.f, Angle);
            AngleMatrix(Angle, Matrix);
            Matrix[0][3] = 10.f;
            Matrix[1][3] = -15.f;
            Matrix[2][3] = 0.f;
        }
        else if (Type >= MODEL_CAPE_OF_EMPEROR)
        {
            Vector(0.f, 90.f, 0.f, Angle);
            AngleMatrix(Angle, Matrix);
            Matrix[0][3] = -47.f;
            Matrix[1][3] = -7.f;
            Matrix[2][3] = 0.f;
        }

        else if ((Type >= MODEL_CROSSBOW && Type < MODEL_ARROWS) ||
                 (Type >= MODEL_SAINT_CROSSBOW && Type < MODEL_CELESTIAL_BOW) ||
                 (Type >= MODEL_DIVINE_CB_OF_ARCHANGEL && Type < MODEL_ARROW_VIPER_BOW))
        {
            Vector(0.f, 20.f, 180.f, Angle);
            AngleMatrix(Angle, Matrix);
            Matrix[0][3] = -10.f;
            Matrix[1][3] = 8.f;
            Matrix[2][3] = 40.f;
        }
        else if (Type == MODEL_15GRADE_ARMOR_OBJ_ARMLEFT ||
                 Type == MODEL_15GRADE_ARMOR_OBJ_ARMRIGHT ||
                 Type == MODEL_15GRADE_ARMOR_OBJ_BODYLEFT ||
                 Type == MODEL_15GRADE_ARMOR_OBJ_BODYRIGHT ||
                 Type == MODEL_15GRADE_ARMOR_OBJ_BOOTLEFT ||
                 Type == MODEL_15GRADE_ARMOR_OBJ_BOOTRIGHT ||
                 Type == MODEL_15GRADE_ARMOR_OBJ_HEAD || Type == MODEL_15GRADE_ARMOR_OBJ_PANTLEFT ||
                 Type == MODEL_15GRADE_ARMOR_OBJ_PANTRIGHT)
        {
            switch (Type)
            {
            case MODEL_15GRADE_ARMOR_OBJ_ARMLEFT: {
                Vector(0.f, -90.f, 0.f, Angle);
                AngleMatrix(Angle, Matrix);
                Matrix[0][3] = 30.f;
                Matrix[1][3] = 0.f;
                Matrix[2][3] = 20.f;
            }
            break;
            case MODEL_15GRADE_ARMOR_OBJ_ARMRIGHT: {
                Vector(0.f, -90.f, 0.f, Angle);
                AngleMatrix(Angle, Matrix);
                Matrix[0][3] = 30.f;
                Matrix[1][3] = 0.f;
                Matrix[2][3] = -20.f;
            }
            break;
            case MODEL_15GRADE_ARMOR_OBJ_BODYLEFT: {
                Vector(0.f, -90.f, 0.f, Angle);
                AngleMatrix(Angle, Matrix);
                Matrix[0][3] = 5.f;
                Matrix[1][3] = -20.f;
                Matrix[2][3] = 0.f;
            }
            break;
            case MODEL_15GRADE_ARMOR_OBJ_BODYRIGHT: {
                Vector(0.f, -90.f, 0.f, Angle);
                AngleMatrix(Angle, Matrix);
                Matrix[0][3] = 5.f;
                Matrix[1][3] = -20.f;
                Matrix[2][3] = 0.f;
            }
            break;
            case MODEL_15GRADE_ARMOR_OBJ_BOOTLEFT: {
                Vector(0.f, 90.f, 180.f, Angle);
                AngleMatrix(Angle, Matrix);
                Matrix[0][3] = 20.f;
                Matrix[1][3] = 15.f;
                Matrix[2][3] = -10.f;
            }
            break;
            case MODEL_15GRADE_ARMOR_OBJ_BOOTRIGHT: {
                Vector(0.f, 90.f, 180.f, Angle);
                AngleMatrix(Angle, Matrix);
                Matrix[0][3] = 20.f;
                Matrix[1][3] = 15.f;
                Matrix[2][3] = 10.f;
            }
            break;
            case MODEL_15GRADE_ARMOR_OBJ_HEAD: {
                Vector(180.f, -90.f, 0.f, Angle); //y,x,z
                AngleMatrix(Angle, Matrix);
                Matrix[0][3] = 28.f; //y
                Matrix[1][3] = 20.f; //x
                Matrix[2][3] = 0.f;
            }
            break;
            case MODEL_15GRADE_ARMOR_OBJ_PANTLEFT: {
                Vector(0.f, 90.f, 180.f, Angle);
                AngleMatrix(Angle, Matrix);
                Matrix[0][3] = 25.f;
                Matrix[1][3] = 5.f;
                Matrix[2][3] = -5.f;
            }
            break;
            case MODEL_15GRADE_ARMOR_OBJ_PANTRIGHT: {
                Vector(0.f, 90.f, 180.f, Angle);
                AngleMatrix(Angle, Matrix);
                Matrix[0][3] = 25.f;
                Matrix[1][3] = 5.f;
                Matrix[2][3] = 5.f;
            }
            break;
            }
        }
        else
        {
            if (Type == MODEL_DRAGON_SOUL_STAFF)
            {
                Vector(90.f + 20.f, 180.f, 90.f, Angle);
                AngleMatrix(Angle, Matrix);
            }
            else
            {
                Vector(90.f - 20.f, 0.f, 90.f, Angle);
                AngleMatrix(Angle, Matrix);
            }

            if (Type == MODEL_ARROW_VIPER_BOW)
            {
                Vector(-60.f, 0.f, -80.f, Angle);
                AngleMatrix(Angle, Matrix);
                Matrix[0][3] = -5.f;
                Matrix[1][3] = 20.f;
                Matrix[2][3] = 0.f;
            }
            else if (Type == MODEL_STINGER_BOW)
            {
                Vector(-60.f, 0.f, -80.f, Angle);
                AngleMatrix(Angle, Matrix);
                Matrix[0][3] = -5.f;
                Matrix[1][3] = 20.f;
                Matrix[2][3] = -5.f;
            }
            else if (Type == MODEL_AIR_LYN_BOW)
            {
                Vector(90.f, 0.f, -80.f, Angle);
                AngleMatrix(Angle, Matrix);
                Matrix[0][3] = 10.f;
                Matrix[1][3] = 20.f;
                Matrix[2][3] = -5.f;
            }
            else if (Type >= MODEL_BOW && Type < MODEL_BOW + MAX_ITEM_INDEX)
            {
                Matrix[0][3] = -10.f;
                Matrix[1][3] = 5.f;
                Matrix[2][3] = 10.f;
            }
            else if (Type == MODEL_DRAGON_SOUL_STAFF)
            {
                Matrix[0][3] = -10.f;
                Matrix[1][3] = 5.f;
                Matrix[2][3] = -10.f;
            }
            else if (Type == MODEL_STAFF_OF_DESTRUCTION)
            {
                Matrix[0][3] = -10.f;
                Matrix[1][3] = 5.f;
                Matrix[2][3] = 10.f;
            }
            else if (Type >= MODEL_SHIELD && Type < MODEL_SHIELD + MAX_ITEM_INDEX)
            {
                if (Type == MODEL_ELEMENTAL_SHIELD)
                {
                    Vector(30.f, 0.f, 90.f, Angle);
                    AngleMatrix(Angle, Matrix);
                    Matrix[0][3] = -20.f;
                    Matrix[1][3] = 0.f;
                    Matrix[2][3] = -20.f;
                }
                else if (Type == MODEL_LEGENDARY_SHIELD || Type == MODEL_GRAND_SOUL_SHIELD)
                {
                    Vector(50.f, 0.f, 90.f, Angle);
                    AngleMatrix(Angle, Matrix);
                    Matrix[0][3] = -28.f;
                    Matrix[1][3] = 0.f;
                    Matrix[2][3] = -25.f;
                }
                else if (Type == MODEL_SKULL_SHIELD)
                {
                    Vector(30.f, 0.f, 90.f, Angle);
                    AngleMatrix(Angle, Matrix);
                    Matrix[0][3] = -15.f;
                    Matrix[1][3] = 0.f;
                    Matrix[2][3] = -25.f;
                }
                else
                {
                    Matrix[0][3] = -10.f;
                    Matrix[1][3] = 0.f;
                    Matrix[2][3] = 0.f;
                }
            }
            else
            {
                Matrix[0][3] = -20.f;
                Matrix[1][3] = 5.f;
                Matrix[2][3] = 40.f;
            }
        }

        if (g_CMonkSystem.IsRagefighterCommonWeapon(c->Class, Type))
        {
            Matrix[1][3] += 10.0f;
            Matrix[2][3] += 25.0f;
            scale = 0.9f;
        }

        if (bRightHandItem == false &&
            !(Type >= MODEL_SHIELD && Type < MODEL_SHIELD + MAX_ITEM_INDEX) &&
            Type != MODEL_ARROW_VIPER_BOW)
        {
            vec3_t vNewAngle;
            float mNewRot[3][4];
            float _Angle = 275.0f;
            if (g_CMonkSystem.IsRagefighterCommonWeapon(c->Class, Type))
            {
                _Angle = 265.0f;
                Matrix[1][3] += 7.0f;
            }
            Vector(145.f, 0.f, _Angle, vNewAngle);
            AngleMatrix(vNewAngle, mNewRot);
            mNewRot[0][3] = 0.f;
            mNewRot[1][3] = 10.f;
            mNewRot[2][3] = -30.f;

            R_ConcatTransforms(Matrix, mNewRot, Matrix);
        }

        R_ConcatTransforms(preparedCharacter.object.bones[f->LinkBone], Matrix, parent);
        VectorCopy(preparedCharacter.object.position, origin);
        Vector(0.f, 0.f, 0.f, angle);
    }
    else
    {
        Vector(x, y, z, p);
        VectorRotate(p, preparedCharacter.object.bones[f->LinkBone], Position);
        VectorScale(Position, scale, Position);
        VectorAdd(preparedCharacter.object.position, Position, origin);
        Vector(0.f, 0.f, 0.f, angle);
        // For unlinked items (e.g. Wings), explicitly populate main-thread parent
        // with the character's link bone transform to prevent uninitialized thread_local garbage.
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c)
                parent[r][c] = preparedCharacter.object.bones[f->LinkBone][r][c];
    }
}

void SessionVisualUnit::EmitLinkedItemSprite(BMD *model, vec34_t *bones, int bitmap, int bone,
                                             float scale, vec3_t light, OBJECT *owner)
{
    vec3_t offset{}, position;
    model->TransformPosition(bones[bone], offset, position, true);
    CreateSprite(bitmap, position, scale, light, owner);
}

bool SessionVisualUnit::CanPresentLinkedItem(const CHARACTER &character, int Type)
{
    const auto *o = &character.Object;
    if (o->SubType == MODEL_CURSEDTEMPLE_ALLIED_PLAYER ||
        o->SubType == MODEL_CURSEDTEMPLE_ILLUSION_PLAYER)
    {
        if (Type >= MODEL_WING && Type <= MODEL_WINGS_OF_DARKNESS)
            return false; // 1st and 2nd Wings
        else if (Type >= MODEL_WING_OF_STORM && Type <= MODEL_WING_OF_DIMENSION) // 3rd Wings
            return false;
        else if (Type >= MODEL_WING + 130 && Type <= MODEL_WING + 135)
            return false; // Small Wings and Capes
        else if (Type >= MODEL_CAPE_OF_FIGHTER && Type <= MODEL_CAPE_OF_OVERRULE)
            return false; // Capes
    }

    if (Type >= MODEL_BOOK_OF_SAHAMUTT && Type <= MODEL_STAFF + 29)
    {
        return false;
    }

    if (g_isCharacterBuff(o, eBuff_Cloaking))
        return false;

    return true;
}

void SessionVisualUnit::PrepareCharacterClothPose(CHARACTER &character, CharacterClothVisual &cloth)
{
    if (!cloth.poseOwner)
        return;
    auto &owner = *cloth.poseOwner;
    const auto &source = character.Object;
    owner.Live = source.Live;
    owner.Scale = source.Scale;
    VectorCopy(source.Position, owner.Position);
    const CharacterDrawInput presentation(character);
    VectorCopy(presentation.object.angle, owner.Angle);
    VectorCopy(CharacterPresentationInput(character).object.headAngle, owner.HeadAngle);
    auto &model = Models[owner.Type];
    model.BodyScale = source.Scale;
    model.BodyHeight = 0.f;
    model.CurrentAction = character.WorldVisualAction;
    VectorCopy(source.Position, model.BodyOrigin);
    AnimationPoseSample sample(&owner, model.BoneHead, 0.f, false, model.PoseAssetIdentity());
    sample.frame = character.WorldVisualAnimationFrame;
    sample.priorFrame = character.WorldVisualPriorAnimationFrame;
    sample.priorAction = character.WorldVisualPriorAction;
    sample.action = character.WorldVisualAction;
    if (cloth.poseSample != sample)
    {
        sample.Evaluate(model, owner.BoneTransform);
        cloth.poseSample = sample;
    }
    cloth.poseRevision = character.WorldVisualPoseRevision;
}

void SessionVisualUnit::AdvanceCharacterCloth(CHARACTER &character,
                                              WorldCharacterVisualState &visual, bool advance)
{
    auto &object = character.Object;
    const bool mesh = character.MonsterIndex == MONSTER_MAGIC_SKELETON_1 ||
                      character.MonsterIndex == MONSTER_MAGIC_SKELETON_2 ||
                      character.MonsterIndex == MONSTER_MAGIC_SKELETON_3 ||
                      character.MonsterIndex == MONSTER_MAGIC_SKELETON_4 ||
                      character.MonsterIndex == MONSTER_MAGIC_SKELETON_5 ||
                      character.MonsterIndex == MONSTER_MAGIC_SKELETON_6 ||
                      character.MonsterIndex == MONSTER_MAGIC_SKELETON_7;
    const bool robe = character.MonsterIndex == MONSTER_MEGA_CRUST ||
                      character.MonsterIndex == MONSTER_ALPHA_CRUST ||
                      character.MonsterIndex == MONSTER_OMEGA_WING;
    using Kind = CharacterClothVisual::Kind;
    Kind kind;
    if (mesh)
        kind = Kind::MagicSkeleton;
    else if (robe)
        kind = Kind::Crust;
    else if (object.Type == MODEL_FRED)
        kind = Kind::Fred;
    else if (character.MonsterIndex == MONSTER_DARK_PHOENIX)
        kind = Kind::PhoenixHair;
    else if (object.Type == MODEL_PLAYER && object.Kind == KIND_PLAYER &&
             object.SubType == MODEL_GM_CHARACTER)
        kind = Kind::GmHair;
    else if (object.Type == MODEL_PLAYER && object.Kind == KIND_PLAYER &&
             object.SubType == MODEL_CURSEDTEMPLE_ALLIED_PLAYER)
        kind = Kind::CursedAllied;
    else if (object.Type == MODEL_PLAYER && object.Kind == KIND_PLAYER &&
             object.SubType == MODEL_CURSEDTEMPLE_ILLUSION_PLAYER)
        kind = Kind::CursedIllusion;
    else if (object.Type == MODEL_PLAYER && object.Kind == KIND_PLAYER &&
             object.SubType == MODEL_HALLOWEEN)
        kind = Kind::Halloween;
    else
    {
        visual.bodyCloth.reset();
        return;
    }
    const std::array<int, 5> shape{object.Type, object.SubType, character.MonsterIndex, 0, 0};
    if (visual.bodyCloth && (visual.bodyCloth->kind != kind || visual.bodyCloth->shape != shape))
        visual.bodyCloth.reset();
    if (!advance && visual.bodyCloth &&
        visual.bodyCloth->poseRevision == character.WorldVisualPoseRevision)
        return;
    auto &model = Models[object.Type];
    model.BodyScale = object.Scale;
    model.BodyHeight = 0.f;
    model.CurrentAction = object.CurrentAction;
    VectorCopy(object.Position, model.BodyOrigin);
    if (mesh)
    {
        // The mesh solver's fixed vertices need this tick's CPU-transformed pose.
        vec3_t minimum{}, maximum{};
        OBB_t bounds;
        model.Transform(CharacterPresentationInput(character).object.bones, minimum, maximum,
                        &bounds, true);
    }
    if (!visual.bodyCloth)
    {
        if (kind == Kind::PhoenixHair &&
            (Models[object.Type + 1].NumBones == 0 || Models[object.Type + 1].NumActions == 0))
            return;
        visual.bodyCloth = std::make_unique<CharacterClothVisual>(
            sessionKeeper_, kind, kind == Kind::GmHair ? 2 : 1, mesh);
        visual.bodyCloth->shape = shape;
        if (kind == Kind::PhoenixHair)
        {
            const auto &hairModel = Models[object.Type + 1];
            visual.bodyCloth->poseOwner = std::make_unique<OBJECT>();
            visual.bodyCloth->poseOwner->Type = object.Type + 1;
            visual.bodyCloth->poseBones = std::make_unique<vec34_t[]>(hairModel.NumBones);
            visual.bodyCloth->poseOwner->BoneTransform = visual.bodyCloth->poseBones.get();
            PrepareCharacterClothPose(character, *visual.bodyCloth);
        }
        auto *cloth = visual.bodyCloth->pieces;
        if (mesh)
        {
            static_cast<CPhysicsClothMesh *>(cloth)->Create(&object, 2, 18, PCT_HEAVY);
            cloth->AddCollisionSphere(0.f, 0.f, 0.f, 50.f, 18);
            cloth->AddCollisionSphere(0.f, -20.f, 0.f, 30.f, 18);
        }
        else if (robe)
        {
            const int texture =
                character.MonsterIndex == MONSTER_MEGA_CRUST ? BITMAP_ROBE + 3 : BITMAP_ROBE + 5;
            cloth->Create(&object, 19, 0.f, 10.f, 0.f, 5, 15, 30.f, 300.f, texture, texture,
                          PCT_RUBBER | PCT_MASK_ALPHA);
        }
        else if (kind == Kind::PhoenixHair)
        {
            cloth->Create(visual.bodyCloth->poseOwner.get(), 10, 0.f, -10.f, 0.f, 5, 12, 15.f,
                          240.f, BITMAP_PHO_R_HAIR, BITMAP_PHO_R_HAIR, PCT_RUBBER | PCT_MASK_ALPHA);
            cloth->AddCollisionSphere(0.f, 0.f, 40.f, 30.f, 10);
        }
        else if (kind == Kind::GmHair)
        {
            cloth[0].Create(&object, 20, 0.f, 5.f, 10.f, 6, 5, 30.f, 70.f, BITMAP_GM_HAIR_1,
                            BITMAP_GM_HAIR_1, PCT_COTTON | PCT_SHORT_SHOULDER | PCT_MASK_ALPHA);
            cloth[0].SetWindMinMax(10, 20);
            cloth[1].Create(&object, 20, 0.f, 5.f, 25.f, 4, 4, 30.f, 40.f, BITMAP_GM_HAIR_3,
                            BITMAP_GM_HAIR_3, PCT_COTTON | PCT_SHORT_SHOULDER | PCT_MASK_ALPHA);
            cloth[1].SetWindMinMax(8, 15);
            for (int index = 0; index < 2; ++index)
            {
                cloth[index].AddCollisionSphere(-10.f, 20.f, 20.f, 27.f, 17);
                cloth[index].AddCollisionSphere(10.f, 20.f, 20.f, 27.f, 17);
            }
        }
        else if (kind == Kind::Fred)
        {
            cloth->Create(&object, 10, 0.f, -10.f, 0.f, 5, 12, 150.f, 190.f, BITMAP_DEASULER_CLOTH,
                          BITMAP_DEASULER_CLOTH,
                          PCT_MASK_ALPHA | PCT_HEAVY | PCT_STICKED | PCT_SHORT_SHOULDER);
            cloth->AddCollisionSphere(50.f, -140.f, -20.f, 30.f, 2);
        }
        else if (kind == Kind::CursedAllied)
        {
            cloth->Create(&object, 19, 0.f, 8.f, 0.f, 10, 10, 140.f, 140.f,
                          BITMAP_CURSEDTEMPLE_ALLIED_PHYSICSCLOTH,
                          BITMAP_CURSEDTEMPLE_ALLIED_PHYSICSCLOTH,
                          PCT_CURVED | PCT_SHORT_SHOULDER | PCT_MASK_ALPHA);
        }
        else if (kind == Kind::CursedIllusion)
        {
            cloth->Create(&object, 20, -4.f, 5.f, 0.f, 10, 20, 17.f, 100.f,
                          BITMAP_CURSEDTEMPLE_ILLUSION_PHYSICSCLOTH,
                          BITMAP_CURSEDTEMPLE_ILLUSION_PHYSICSCLOTH,
                          PCT_SHAPE_HALLOWEEN | PCT_ELASTIC_HALLOWEEN | PCT_MASK_ALPHA);
        }
        else if (kind == Kind::Halloween)
        {
            cloth->Create(&object, 19, 0.f, 10.f, 0.f, 10, 20, 30.f, 200.f, BITMAP_ROBE + 3,
                          BITMAP_ROBE + 3,
                          PCT_SHAPE_HALLOWEEN | PCT_ELASTIC_HALLOWEEN | PCT_MASK_ALPHA);
        }
    }
    if (visual.bodyCloth->poseRevision != character.WorldVisualPoseRevision)
        PrepareCharacterClothPose(character, *visual.bodyCloth);
    visual.bodyCloth->poseRevision = character.WorldVisualPoseRevision;
    if (kind == Kind::GmHair && g_isCharacterBuff(&object, eBuff_Cloaking))
        return;
    if (!advance)
        return;
    const bool frozen =
        (kind == Kind::CursedAllied || kind == Kind::CursedIllusion) &&
        (g_isCharacterBuff(&object, eDeBuff_Stun) || g_isCharacterBuff(&object, eDeBuff_Sleep));
    const float step = frozen ? 0.f : 0.005f;
    for (std::size_t index = 0; index < visual.bodyCloth->count; ++index)
    {
        visual.bodyCloth->pieces[index].SetPose(
            visual.bodyCloth->poseOwner ? visual.bodyCloth->poseBones.get()
                                        : CharacterPresentationInput(character).object.bones,
            visual.bodyCloth->poseOwner ? &visual.bodyCloth->poseSample
                                        : &character.WorldVisualPoseSample,
            &character.Object);
        if (visual.bodyCloth->pieces[index].Move2(step, 5, FPS_ANIMATION_FACTOR, WorldTime))
            continue;
        visual.bodyCloth.reset();
        break;
    }
}

void SessionVisualUnit::ConfigureCharacterWeaponPlayback(CHARACTER &character, PART_t &part)
{
    auto *o = &character.Object;
    auto *w = &part;
    if (o->CurrentAction == PLAYER_ATTACK_BOW || o->CurrentAction == PLAYER_ATTACK_CROSSBOW ||
        o->CurrentAction == PLAYER_ATTACK_FLY_BOW || o->CurrentAction == PLAYER_ATTACK_FLY_CROSSBOW)
    {
        if (w->Type == MODEL_STINGER_BOW)
        {
            w->CurrentAction = 1;
        }
        else
        {
            w->CurrentAction = 0;
        }
        w->PlaySpeed = Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_BOW].PlaySpeed;
    }
    else if (w->Type == MODEL_FLAIL)
    {
        if (o->CurrentAction >= PLAYER_ATTACK_SWORD_RIGHT1 &&
            o->CurrentAction <= PLAYER_ATTACK_SWORD_RIGHT2)
        {
            w->CurrentAction = 2;
            w->PlaySpeed = Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_SWORD_RIGHT1].PlaySpeed;
        }
        else
        {
            w->CurrentAction = 1;
            w->PlaySpeed = Models[MODEL_PLAYER].Actions[PLAYER_STOP_MALE].PlaySpeed;
        }
    }
    else if (w->Type == MODEL_CRYSTAL_SWORD)
    {
        w->CurrentAction = 0;
        w->PlaySpeed = Models[MODEL_PLAYER].Actions[PLAYER_STOP_MALE].PlaySpeed * 2.f;
    }
    else if (w->Type == MODEL_STAFF_OF_RESURRECTION)
    {
        w->CurrentAction = 0;
        w->PlaySpeed = Models[MODEL_PLAYER].Actions[PLAYER_STOP_MALE].PlaySpeed * 15.f;
    }
    else if (w->Type == MODEL_PHOENIX_SOUL_STAR)
    {
        if (w->AnimationFrame < 2.f)
        {
            w->CurrentAction = 0;
            w->PlaySpeed = Models[MODEL_PLAYER].Actions[PLAYER_STOP_RAGEFIGHTER].PlaySpeed * 1.5f;
        }
        else
        {
            w->AnimationFrame = w->PriorAnimationFrame = 2.f;
            w->PlaySpeed = 0.f;
        }
    }
    else if (w->Type >= MODEL_SWORD && w->Type < MODEL_SWORD + MAX_ITEM_INDEX)
    {
        w->CurrentAction = 0;
        w->PlaySpeed = Models[MODEL_PLAYER].Actions[PLAYER_STOP_MALE].PlaySpeed;
    }
    else if (w->Type == MODEL_STINGER_BOW)
    {
        w->CurrentAction = 0;
        w->PlaySpeed = Models[MODEL_PLAYER].Actions[PLAYER_STOP_MALE].PlaySpeed;
    }
    else if ((w->Type == MODEL_FROST_MACE) || (w->Type == MODEL_ABSOLUTE_SCEPTER))
    {
        w->CurrentAction = 0;
        w->PlaySpeed = Models[MODEL_PLAYER].Actions[PLAYER_STOP_MALE].PlaySpeed;
    }
    else if (w->Type == MODEL_RAVEN_STICK)
    {
        w->CurrentAction = 0;
        w->PlaySpeed = Models[MODEL_PLAYER].Actions[PLAYER_STOP_MALE].PlaySpeed;
    }
    else
    {
        w->CurrentAction = 0;
        w->PlaySpeed = 0.f;
        w->AnimationFrame = 0.001f;
        w->PriorAnimationFrame = 0.f;
    }
}

void SessionVisualUnit::AdvanceLinkedItemVisual(CHARACTER &character,
                                                WorldCharacterVisualState &visual, bool advance,
                                                int slot, const PART_t &part, int type, bool link,
                                                bool rightHand, float x, float y, float z,
                                                float scaleOverride)
{
    if (!CanPresentLinkedItem(character, type))
        return;
    auto &model = Models[type];
    if (model.NumBones == 0 || model.NumActions == 0)
        return;
    auto &entry = visual.linkedItems[slot];
    if (entry && entry->item.Type != type)
    {
        OBJECT *targets[]{&entry->target, &entry->item};
        gameplay_.RetireCharacterEffectTargets(targets);
        entry.reset();
    }
    const bool newlyCreated = !entry;
    if (!entry)
    {
        entry = std::make_unique<CharacterLinkedItemVisual>(type, model.NumBones);
        entry->playback = part;
        ItemObjectAttribute(&entry->item);
        g_CharacterCopyBuff(&entry->item, &character.Object);
        g_CharacterCopyBuff(&entry->target, &character.Object);
    }
    visual.linkedItemSlots |= 1u << slot;
    auto &sample = entry->playback;
    if (entry->linked != link && slot <= CharacterLinkedItemVisual::LeftWeapon)
        sample = part;
    entry->linked = link;
    entry->rightHand = rightHand;
    Vector(x, y, z, entry->parentOffset);
    entry->scaleOverride = scaleOverride;
    sample.Type = type;
    sample.LinkBone = part.LinkBone;
    sample.Level = part.Level;
    sample.ExcellentFlags = part.ExcellentFlags;
    sample.AncientDiscriminator = part.AncientDiscriminator;
    sample.CurrentAction = part.CurrentAction;
    sample.PlaySpeed = part.PlaySpeed;
    if (slot <= CharacterLinkedItemVisual::LeftWeapon && !link)
        ConfigureCharacterWeaponPlayback(character, sample);
    if (slot == CharacterLinkedItemVisual::RightPhoenix ||
        slot == CharacterLinkedItemVisual::LeftPhoenix)
    {
        sample.Type = MODEL_PHOENIX_SOUL_STAR;
        ConfigureCharacterWeaponPlayback(character, sample);
        sample.Type = type;
        if (CharacterWeaponsOnBack(character))
        {
            sample.CurrentAction = sample.PriorAction = 0;
            sample.AnimationFrame = sample.PriorAnimationFrame = sample.PlaySpeed = 0.f;
        }
    }
    if (type == MODEL_WING_OF_RUIN)
        sample.PlaySpeed = 0.15f;
    if (type == MODEL_WINGS_OF_DARKNESS && character.SafeZone)
        sample.CurrentAction = 1;
    if ((character.Skill == AT_SKILL_PENETRATION || character.Skill == AT_SKILL_PENETRATION_STR) &&
        character.Object.Type == MODEL_PLAYER &&
        Engine::Object::IsAttackAction(character.Object.CurrentAction) &&
        character.Object.AnimationFrame >= 5.f && character.Object.AnimationFrame <= 10.f)
    {
        sample.PriorAnimationFrame = 2.f;
        sample.AnimationFrame = 3.2f;
    }
    auto &target = entry->target;
    auto &item = entry->item;
    if (advance && !newlyCreated)
        UpdateItemObjectMaterial(item);
    const auto &source = character.Object;
    target.Live = item.Live = source.Live;
    target.Type = source.Type;
    target.Kind = source.Kind;
    target.CurrentAction = source.CurrentAction;
    target.AnimationFrame = source.AnimationFrame;
    target.Alpha = source.Alpha;
    target.Scale = source.Scale;
    VectorCopy(source.Position, target.Position);
    VectorCopy(source.Angle, target.Angle);
    VectorCopy(source.Light, target.Light);
    target.MotionTrace = source.MotionTrace;
    BuildCharacterItemParent(x, y, z, CharacterPresentationInput(character), sample, type, link,
                             rightHand, ParentMatrix, model.BodyOrigin, item.Angle, model.BodyScale,
                             scaleOverride);
    if (type == MODEL_BOSS_HEAD)
    {
        model.BoneHead = 0;
        item.Angle[2] = WorldTime;
    }
    model.CurrentAction = sample.CurrentAction;
    AdvanceLinkedItemPlayback(character, *entry, model, advance, slot, link);
    model.BodyHeight = 0.f;
    item.CurrentAction = sample.CurrentAction;
    item.AnimationFrame = sample.AnimationFrame;
    item.PriorAnimationFrame = sample.PriorAnimationFrame;
    item.PriorAction = sample.PriorAction;
    item.Scale = model.BodyScale;
    item.Alpha = source.Alpha;
    VectorCopy(model.BodyOrigin, item.Position);
    AnimationPoseSample poseSample(&item, model.BoneHead, 0.f, true, model.PoseAssetIdentity());
    poseSample.SetParent(ParentMatrix);
    VectorCopy(item.Angle, poseSample.headAngle.data());

    if (entry->poseSample != poseSample)
    {
        poseSample.Evaluate(model, item.BoneTransform);
        entry->poseSample = poseSample;
    }
    if (advance)
    {
        const auto parentDraw = CharacterPresentationInput(character);
        sessionKeeper_.Gameplay()->EmitWingItemVisual(item, &item, type, item.BoneTransform,
                                                      entry.get(), &parentDraw);
        EmitLinkedItemVisual(character, visual, *entry, parentDraw);
    }
}

void SessionVisualUnit::AdvanceCharacterLinkedItems(CHARACTER &character,
                                                    WorldCharacterVisualState &visual, bool advance)
{
    SessionRandom::PresentationScope presentation(sessionKeeper_.RandomForConstruction());
    using enum CharacterLinkedItemVisual::Slot;
    const bool onBack = CharacterWeaponsOnBack(character);
    if (!advance && visual.linkedPoseRevision == character.WorldVisualPoseRevision &&
        visual.linkedAppearanceRevision == character.WorldVisualAppearanceRevision &&
        visual.linkedBuffRevision == character.Object.m_BuffMap.Revision() &&
        visual.linkedOnBack == onBack)
        return;
    visual.linkedPoseRevision = character.WorldVisualPoseRevision;
    visual.linkedAppearanceRevision = character.WorldVisualAppearanceRevision;
    visual.linkedBuffRevision = character.Object.m_BuffMap.Revision();
    visual.linkedOnBack = onBack;
    const auto previousSlots = visual.linkedItemSlots;
    visual.linkedItemSlots = 0;
    auto &object = character.Object;
    for (int hand = 0; hand < 2; ++hand)
    {
        const auto &weapon = character.Weapon[hand];
        if (g_CMonkSystem.EqualItemModelType(weapon.Type) == MODEL_PHOENIX_SOUL_STAR)
        {
            auto part = weapon;
            part.LinkBone = hand ? 37 : 28;
            AdvanceLinkedItemVisual(character, visual, advance, RightPhoenix + hand, part,
                                    MODEL_SWORD_35_WING, true, true, hand ? 100.f : 80.f, 10.f,
                                    -75.f);
        }
        if (weapon.Type < 0)
            continue;
        int backType = weapon.Type;
        if (hand == 0 && object.Kind == KIND_NPC &&
            TheMapProcess().CharacterPolicy().festiveUniform && object.SubType >= MODEL_SKELETON1 &&
            object.SubType <= MODEL_SKELETON3)
            backType = MODEL_GOLDEN_CROSSBOW;
        const bool ammunition = weapon.Type == MODEL_BOLT || weapon.Type == MODEL_ARROWS;
        if (object.Type == MODEL_PLAYER &&
            (ammunition || (onBack && IsBackItem(&character, backType))))
        {
            if (g_CMonkSystem.IsSwordformGloves(backType))
                continue;
            auto part = character.Wing;
            part.Type = backType;
            part.LinkBone = 47;
            part.CurrentAction = part.PriorAction = 0;
            part.AnimationFrame = part.PriorAnimationFrame = part.PlaySpeed = 0.f;
            if (backType == MODEL_STINGER_BOW)
            {
                part = character.Weapon[1];
                part.LinkBone = 47;
                part.CurrentAction = 2;
                part.PlaySpeed = 0.25f;
            }
            AdvanceLinkedItemVisual(character, visual, advance, hand, part, backType, true,
                                    backType != MODEL_STINGER_BOW && hand == 0, 0.f, 0.f, 15.f);
        }
        else if (!onBack && !ammunition && weapon.Type != MODEL_DARK_RAVEN_ITEM &&
                 !g_CMonkSystem.IsSwordformGloves(weapon.Type))
        {
            if (hand == 0 && ((object.CurrentAction == PLAYER_ATTACK_SKILL_FURY_STRIKE &&
                               object.AnimationFrame <= 4.f) ||
                              character.PostMoveProcess_IsProcessing()))
                continue;
            AdvanceLinkedItemVisual(character, visual, advance, hand, weapon, weapon.Type, false,
                                    false);
        }
    }
    if (object.Type == MODEL_PLAYER)
    {
        if (gMapManager.InBloodCastle() && character.EtcPart >= 1 && character.EtcPart <= 3)
        {
            constexpr int questTypes[]{MODEL_DIVINE_STAFF_OF_ARCHANGEL,
                                       MODEL_DIVINE_SWORD_OF_ARCHANGEL,
                                       MODEL_DIVINE_CB_OF_ARCHANGEL};
            auto part = character.Wing;
            part.LinkBone = 47;
            part.PlaySpeed =
                object.CurrentAction == PLAYER_FLY || object.CurrentAction == PLAYER_FLY_CROSSBOW
                    ? 1.f
                    : 0.25f;
            AdvanceLinkedItemVisual(character, visual, advance, Quest, part,
                                    questTypes[character.EtcPart - 1], true, false, 0.f, 0.f, 15.f);
        }
        if (!gMapManager.InChaosCastle() && character.Wing.Type != -1)
        {
            auto part = character.Wing;
            part.PlaySpeed =
                object.CurrentAction == PLAYER_FLY || object.CurrentAction == PLAYER_FLY_CROSSBOW
                    ? (part.Type == MODEL_WING_OF_STORM ? 0.5f : 1.f)
                    : 0.25f;
            const bool cape =
                part.Type == MODEL_CAPE_OF_EMPEROR || part.Type == MODEL_CAPE_OF_OVERRULE;
            part.LinkBone = cape ? 19 : 47;
            AdvanceLinkedItemVisual(character, visual, advance, Wing, part, part.Type, cape, false,
                                    0.f, 0.f, 15.f);
        }
        if (!gMapManager.InChaosCastle() && character.Helper.Type == MODEL_IMP)
        {
            auto part = character.Helper;
            part.LinkBone = 34;
            part.PlaySpeed = 0.5f;
            const auto armor = character.BodyPart[BODYPART_ARMOR].Type;
            const bool rageArmor =
                gCharacterManager.GetBaseClass(character.Class) == CLASS_RAGEFIGHTER &&
                (armor == MODEL_SACRED_ARMOR || armor == MODEL_STORM_HARD_ARMOR ||
                 armor == MODEL_PIERCING_ARMOR || armor == MODEL_PHOENIX_SOUL_ARMOR);
            AdvanceLinkedItemVisual(character, visual, advance, Helper, part, part.Type, false,
                                    false, 20.f, rageArmor ? -5.f : 0.f, rageArmor ? 20.f : 0.f);
            if (advance)
            {
                PrepareCharacterModel(character);
                const vec3_t offset{20.f, rageArmor ? -5.f : 0.f, rageArmor ? 35.f : 15.f};
                vec3_t position, light;
                Models[object.Type].TransformPosition(object.BoneTransform[part.LinkBone], offset,
                                                      position, true);
                const float luminosity = static_cast<float>(WorldRandom() % 30 + 70) * 0.01f;
                Vector(luminosity * 0.5f, 0.f, 0.f, light);
                CreateSprite(BITMAP_LIGHT, position, 1.5f, light, &object);
            }
        }
    }
    if (character.MonsterIndex >= MONSTER_STATUE_OF_SAINT_1 &&
        character.MonsterIndex <= MONSTER_STATUE_OF_SAINT_3)
    {
        constexpr int statueTypes[]{MODEL_DIVINE_STAFF_OF_ARCHANGEL,
                                    MODEL_DIVINE_SWORD_OF_ARCHANGEL, MODEL_DIVINE_CB_OF_ARCHANGEL};
        auto part = character.Wing;
        part.LinkBone = 1;
        part.CurrentAction = part.PriorAction = 1;
        part.PlaySpeed = 0.2f;
        AdvanceLinkedItemVisual(character, visual, advance, Statue, part,
                                statueTypes[character.MonsterIndex - MONSTER_STATUE_OF_SAINT_1],
                                true, false, 0.f, 0.f, 0.f,
                                character.MonsterIndex == MONSTER_STATUE_OF_SAINT_3 ? 0.9f : 0.7f);
    }
    if (character.MonsterIndex == MONSTER_RED_DRAGON)
    {
        auto part = character.Wing;
        part.LinkBone = 9;
        part.CurrentAction = part.PriorAction = 1;
        part.PlaySpeed = 0.2f;
        AdvanceLinkedItemVisual(character, visual, advance, DragonHead, part, MODEL_BOSS_HEAD,
                                false, false, 0.f, 0.f, -40.f);
        part.LinkBone = 61;
        AdvanceLinkedItemVisual(character, visual, advance, Princess, part, MODEL_PRINCESS, false,
                                false, 0.f, -40.f, 45.f, 0.9f);
    }
    AdvanceCharacterGradeItems(character, visual, advance);
    auto removedSlots = previousSlots & ~visual.linkedItemSlots;
    if (removedSlots == 0)
        return;
    if (!advance && visual.sprites)
        visual.sprites->Clear();
    // Only an attachment admission/retirement change traverses effect owners.
    std::vector<OBJECT *> targets;
    std::vector<std::unique_ptr<CharacterLinkedItemVisual>> retired;
    while (removedSlots != 0)
    {
        const auto slot = std::countr_zero(removedSlots);
        removedSlots &= removedSlots - 1;
        auto entry = visual.linkedItems.find(slot);
        targets.push_back(&entry->second->target);
        targets.push_back(&entry->second->item);
        retired.push_back(std::move(entry->second));
        visual.linkedItems.erase(entry);
    }
    gameplay_.RetireCharacterEffectTargets(targets);
}

void SessionVisualUnit::RetireLinkedItemVisuals(WorldCharacterVisualState &visual)
{
    if (visual.linkedItems.empty())
        return;
    std::vector<OBJECT *> targets;
    std::vector<std::unique_ptr<CharacterLinkedItemVisual>> retired;
    CharacterPresentationDetail::CollectLinkedItemRetirement(visual, targets, retired);
    gameplay_.RetireCharacterEffectTargets(targets);
}

void SessionVisualUnit::RetireCharacterVisualLifetime(CHARACTER &character,
                                                      WorldCharacterVisualState &visual)
{
    std::vector<OBJECT *> targets{&character.Object};
    std::vector<std::unique_ptr<CSIPartsMDL>> retiredParts;
    std::vector<std::unique_ptr<CSPetSystem>> retiredPets;
    std::vector<std::unique_ptr<CharacterMountVisual>> retiredMounts;
    std::vector<std::shared_ptr<PetObject>> retiredHelperPets;
    std::vector<std::unique_ptr<CharacterLinkedItemVisual>> retired;
    CharacterPresentationDetail::CollectLinkedItemRetirement(visual, targets, retired);
    CharacterPresentationDetail::CollectPartsRetirement(visual, targets, retiredParts);
    CharacterPresentationDetail::RetireClothAndAfterImages(visual);
    CharacterPresentationDetail::CollectPetRetirement(visual, targets, retiredPets);
    CharacterPresentationDetail::CollectHelperPetRetirement(visual, targets, retiredHelperPets);
    CharacterPresentationDetail::CollectMountRetirement(visual, targets, retiredMounts);
    gameplay_.RetireCharacterEffectTargets(targets);
    for (auto &pet : retiredPets)
        pet->EffectsRetired();
    for (auto &pet : retiredHelperPets)
        pet->EffectsRetired();
    g_SummonSystem.ForgetCharacterPhase(&character.Object);
}

void SessionVisualUnit::AdvanceScepterVisual(CharacterLinkedItemVisual &linked,
                                             const CharacterDrawInput &parentDraw)
{
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    auto &item = linked.item;
    if (item.Type == MODEL_ABSOLUTE_SCEPTER)
    {
        constexpr double RefreshMilliseconds = 1000.0;
        if (linked.nextScepterRefreshMilliseconds < 0.0)
            linked.nextScepterRefreshMilliseconds =
                WorldTime - FPS_ANIMATION_FACTOR *
                                (1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps);
        while (linked.nextScepterRefreshMilliseconds <= WorldTime)
        {
            item.m_iAnimation = WorldRandom() % 100;
            for (int axis = 0; axis < 3; ++axis)
                item.EyeRight[axis] = WorldRandom() % 10 - 5;
            for (int axis = 0; axis < 3; ++axis)
                item.EyeRight2[axis] = (WorldRandom() % 10 - 5) * 1.2f;
            linked.nextScepterRefreshMilliseconds += RefreshMilliseconds;
        }
        return;
    }
    // Owner-selected cadence: aligned 40 ms updates met the old %14 gate every seven ticks.
    constexpr float EmissionInterval = 7.f;
    float offset = EmissionInterval - linked.scepterEmissionFrames;
    linked.scepterEmissionFrames += FPS_ANIMATION_FACTOR;
    const int count =
        int(Core::Time::ReferenceSample(linked.scepterEmissionFrames / EmissionInterval));
    linked.scepterEmissionFrames =
        std::max(0.f, linked.scepterEmissionFrames - count * EmissionInterval);
    auto &model = Models[item.Type];
    for (int event = 0; event < count; ++event, offset += EmissionInterval)
    {
        auto birth = gameplay_.EmissionTime(std::max(0.f, FPS_ANIMATION_FACTOR - offset));
        ItemBirthPose pose(sessionKeeper_, *this, parentDraw, linked, birth);
        vec3_t local{}, position, angle, light{0.4f, 0.6f, 0.9f};
        model.TransformPosition(pose.Bones()[5], local, position, true);
        VectorCopy(linked.target.Angle, angle);
        angle[2] = linked.target.MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), angle[2]);
        CreateParticle(BITMAP_SHINY + 6, position, angle, light, 1);
    }
}

void SessionVisualUnit::EmitLinkedItemVisual(const CHARACTER &character,
                                             const WorldCharacterVisualState &visual,
                                             CharacterLinkedItemVisual &linkedItem,
                                             const CharacterDrawInput &parentDraw)
{
    auto &target = linkedItem.target;
    auto &item = linkedItem.item;
    const int Type = item.Type;
    auto *bones = item.BoneTransform;
    auto *c = &character;
    auto *o = &target;
    auto *Object = &item;
    auto *b = &Models[Type];
    vec3_t p{}, Position{};
    float Luminosity;
    vec3_t Light;
    Luminosity = (float)(WorldRandom() % 30 + 70) * 0.005f;
    switch (Type)
    {
    case MODEL_TIGER_BOW:
    case MODEL_SILVER_BOW:
    case MODEL_CHAOS_NATURE_BOW:
    case MODEL_CELESTIAL_BOW:
        if (Type == MODEL_CHAOS_NATURE_BOW)
        {
            Vector(Luminosity * 0.6f, Luminosity * 1.f, Luminosity * 0.8f, Light);

            for (int i = 13; i <= 18; i++)
                EmitLinkedItemSprite(b, bones, BITMAP_SHINY + 1, i, 1.f, Light, o);
        }
        else if (Type == MODEL_CELESTIAL_BOW)
        {
            Vector(Luminosity * 0.5f, Luminosity * 0.5f, Luminosity * 0.8f, Light);

            for (int i = 13; i <= 18; i++)
                EmitLinkedItemSprite(b, bones, BITMAP_SHINY + 1, i, 1.f, Light, o);

            for (int i = 5; i <= 8; i++)
                EmitLinkedItemSprite(b, bones, BITMAP_SHINY + 1, i, 1.f, Light, o);
        }
        else
        {
            Vector(Luminosity * 1.f, Luminosity * 0.6f, Luminosity * 0.2f, Light);
            EmitLinkedItemSprite(b, bones, BITMAP_SHINY + 1, 2, 1.f, Light, o);
            EmitLinkedItemSprite(b, bones, BITMAP_SHINY + 1, 6, 1.f, Light, o);
        }
        break;
    case MODEL_DIVINE_STAFF_OF_ARCHANGEL:
        Vector(Luminosity * 1.f, Luminosity * 0.3f, Luminosity * 0.1f, Light);

        for (int i = 0; i < 10; ++i)
        {
            vec3_t Light2;
            Vector(0.4f, 0.4f, 0.4f, Light2);
            Vector(i * 30.f - 180.f, -40.f, 0.f, p);
            b->TransformPosition(bones[0], p, Position, true);

            if (rand_fps_check(3))
            {
                CreateSprite(BITMAP_SHINY + 1, Position, 0.6f, Light2, o,
                             (float)(WorldRandom() % 360));
            }
            CreateSprite(BITMAP_LIGHT, Position, 2.f, Light, o);
        }
        break;
    case MODEL_SYLPH_WIND_BOW: {
        Vector(0.8f, 0.8f, 0.2f, Light);
        Vector(0.f, 0.f, 0.f, p);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
        {
            ItemBirthPose birthPose(sessionKeeper_, *this, parentDraw, linkedItem, birth);
            vec3_t emissionAngle;
            VectorCopy(o->Angle, emissionAngle);
            emissionAngle[2] =
                o->MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), emissionAngle[2]);
            auto *bones = birthPose.Bones();
            b->TransformPosition(bones[4], p, Position, true);
            CreateParticle(BITMAP_SPARK + 1, Position, emissionAngle, Light, 11, 0.8f);

            b->TransformPosition(bones[12], p, Position, true);
            CreateParticle(BITMAP_SPARK + 1, Position, emissionAngle, Light, 11, 0.8f);
        }
        Vector(0.5f, 0.5f, 0.1f, Light);
        b->TransformPosition(bones[7], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 2.0f, Light, o);

        b->TransformPosition(bones[15], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 2.0f, Light, o);

        Vector(20.f, 0.f, 0.f, p);
        Vector(1.0f, 1.0f, 0.4f, Light);
        b->TransformPosition(bones[0], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 0.8f, Light, o);
        CreateSprite(BITMAP_LIGHT, Position, 1.3f, Light, o);
        CreateSprite(BITMAP_LIGHT, Position, 2.0f, Light, o);
    }
    break;
    case MODEL_SOLEIL_SCEPTER: {
        Vector(1.0f, 0.3f, 0.0f, Light);

        float fRendomPos = (float)(WorldRandom() % 60) / 20.0f - 1.5f;
        float fRendomScale = (float)(WorldRandom() % 10) / 20.0f + 1.4f;
        Vector(0.f, -100.f + fRendomPos, fRendomPos, p);
        b->TransformPosition(bones[0], p, Position, true);
        CreateSprite(BITMAP_SHINY + 1, Position, fRendomScale, Light, o);
        CreateSprite(BITMAP_SHINY + 1, Position, fRendomScale - 0.3f, Light, o, 20.0f);

        fRendomPos = (float)(WorldRandom() % 60) / 20.0f - 1.5f;
        fRendomScale = (float)(WorldRandom() % 10) / 20.0f + 1.0f;
        Vector(0.f, -100.f + fRendomPos, fRendomPos, p);
        b->TransformPosition(bones[0], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, fRendomScale + 0.3f, Light, o);

        fRendomPos = (float)(WorldRandom() % 40) / 20.0f - 1.0f;
        fRendomScale = (float)(WorldRandom() % 8) / 20.0f + 0.4f;
        Vector(0.f, 100.f + fRendomPos, fRendomPos, p);
        b->TransformPosition(bones[0], p, Position, true);
        CreateSprite(BITMAP_SHINY + 1, Position, fRendomScale, Light, o);
        CreateSprite(BITMAP_SHINY + 1, Position, fRendomScale - 0.2f, Light, o, 90.0f);
    }
    break;
    case MODEL_BONE_BLADE: {
        float fLight = (float)sinf((WorldTime) * 0.4f) * 0.25f + 0.7f;
        Vector(fLight, fLight - 0.5f, fLight - 0.5f, Light);

        Vector(5.f, -22.f, -10.f, p);
        b->TransformPosition(bones[0], p, Position, true);
        CreateSprite(BITMAP_LIGHT + 1, Position, 0.75f, Light, o);

        Vector(-5.f, -22.f, -10.f, p);
        b->TransformPosition(bones[0], p, Position, true);
        CreateSprite(BITMAP_LIGHT + 1, Position, 0.75f, Light, o);
    }
    break;
    case MODEL_EXPLOSION_BLADE: {
        float fRendomPos = (float)(WorldRandom() % 60) / 20.0f - 1.5f;
        float fRendomScale = (float)(WorldRandom() % 30) / 20.0f + 1.5f;
        float fLight = (float)sinf((WorldTime) * 0.7f) * 0.2f + 0.5f;

        float fRotation = (WorldTime * 0.0006f) * 360.0f;
        float fRotation2 = (WorldTime * 0.0006f) * 360.0f;

        Vector(0.2f, 0.2f, fLight, Light);

        Vector(0.f, fRendomPos, fRendomPos, p);
        b->TransformPosition(bones[4], p, Position, true);
        CreateSprite(BITMAP_SHINY + 1, Position, fRendomScale, Light, o, fRotation);
        CreateSprite(BITMAP_SHINY + 1, Position, fRendomScale - 0.4f, Light, o, 90.f + fRotation2);
        Vector(0.0f, 0.0f, 0.0f, p);
        CreateSprite(BITMAP_LIGHT, Position, 2.3f, Light, o);

        Vector(30.f, 0.f, 0.f, p);
        b->TransformPosition(bones[4], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 2.0f, Light, o);

        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(bones[6], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 2.0f, Light, o);

        b->TransformPosition(bones[7], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 2.0f, Light, o);

        b->TransformPosition(bones[8], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 1.0f, Light, o);
    }
    break;
    case MODEL_GRAND_VIPER_STAFF: {
        Vector(0.4f, 0.4f, 0.4f, Light);
        float fRendomPos = (float)(WorldRandom() % 60) / 20.0f - 1.5f;
        float fRendomScale = (float)(WorldRandom() % 15) / 20.0f + 1.8f;
        Vector(0.f, -170.f + fRendomPos, 0.f + fRendomPos, p);
        b->TransformPosition(bones[0], p, Position, true);
        CreateSprite(BITMAP_SPARK + 1, Position, fRendomScale, Light, o);

        VectorCopy(Position, o->EyeLeft);

        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
        {
            ItemBirthPose birthPose(sessionKeeper_, *this, parentDraw, linkedItem, birth);
            vec3_t emissionAngle;
            VectorCopy(o->Angle, emissionAngle);
            emissionAngle[2] =
                o->MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), emissionAngle[2]);
            const float jitter = (WorldRandom() % 60) / 20.f - 1.5f;
            vec3_t offset{0.f, -170.f + jitter, jitter}, Position;
            b->TransformPosition(birthPose.Bones()[0], offset, Position, true);
            CreateJoint(BITMAP_JOINT_ENERGY, Position, Position, emissionAngle, 17, o, 30.f);
        }

        fRendomPos = (float)(WorldRandom() % 60) / 20.0f - 1.5f;
        fRendomScale = (float)(WorldRandom() % 15) / 20.0f + 1.0f;
        Vector(0.f, -170.f + fRendomPos, 0.f + fRendomPos, p);
        Vector(1.0f, 0.4f, 1.0f, Light);
        CreateSprite(BITMAP_LIGHT, Position, fRendomScale, Light, o);
        CreateSprite(BITMAP_SHINY + 1, Position, fRendomScale, Light, o);
        CreateSprite(BITMAP_SHINY + 1, Position, fRendomScale - 0.3f, Light, o, 90.0f);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
        {
            ItemBirthPose birthPose(sessionKeeper_, *this, parentDraw, linkedItem, birth);
            vec3_t emissionAngle;
            VectorCopy(o->Angle, emissionAngle);
            emissionAngle[2] =
                o->MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), emissionAngle[2]);
            const float jitter = (WorldRandom() % 60) / 20.f - 1.5f;
            vec3_t offset{0.f, -170.f + jitter, jitter}, Position;
            b->TransformPosition(birthPose.Bones()[0], offset, Position, true);
            CreateParticle(BITMAP_SPARK + 1, Position, emissionAngle, Light, 11, 2.0f);
        }

        float fLight = (float)sinf((WorldTime) * 0.7f) * 0.2f + 0.5f;
        float fRotation = (WorldTime * 0.0006f) * 360.0f;

        Vector(fLight - 0.1f, 0.1f, fLight - 0.1f, Light);
        Vector(0.f, 10.0f, 0.0f, p);
        b->TransformPosition(bones[2], p, Position, true);
        CreateSprite(BITMAP_SHINY + 1, Position, 1.5f, Light, o, fRotation);
        CreateSprite(BITMAP_SHINY + 1, Position, 1.2f, Light, o, 90.0f + fRotation);

        Vector(-40.f, -10.0f, 0.0f, p);
        b->TransformPosition(bones[1], p, Position, true);
        CreateSprite(BITMAP_SHINY + 1, Position, 1.0f, Light, o, fRotation);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.7f, Light, o, 90.0f + fRotation);

        Vector(-160.f, -5.0f, 0.0f, p);
        b->TransformPosition(bones[1], p, Position, true);
        CreateSprite(BITMAP_SHINY + 1, Position, 1.2f, Light, o, fRotation);
        CreateSprite(BITMAP_SHINY + 1, Position, 1.0f, Light, o, 90.0f + fRotation);
    }
    break;
    case MODEL_DAYBREAK: {
        // Light
        Vector(0.6f, 0.6f, 0.6f, Light);

        Vector(0.f, 0.f, 3.f, p);
        b->TransformPosition(bones[2], p, Position, true);
        CreateSprite(BITMAP_LIGHT + 1, Position, 0.45f, Light, o);

        Vector(0.f, 0.f, -3.f, p);
        b->TransformPosition(bones[2], p, Position, true);
        CreateSprite(BITMAP_LIGHT + 1, Position, 0.45f, Light, o);

        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(bones[2], p, Position, true);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.7f, Light, o);

        float fScale = 0.5f + (float)(WorldRandom() % 100) / 180;
        float fRotation = (WorldTime * 0.0006f) * 360.0f;
        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(bones[2], p, Position, true);
        CreateSprite(BITMAP_SHINY + 1, Position, fScale, Light, o, fRotation);

        // Flare01.jpg
        float fLight = (float)sinf((WorldTime) * 0.4f) * 0.25f + 0.6f;
        Vector(fLight, fLight, fLight, Light);
        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(bones[2], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 3.f, Light, o, 0.f);
    }
    break;
    case MODEL_SWORD_DANCER: {
        float fLight, fScale, fRotation;
        Vector(1.0f, 0.1f, 0.0f, Light);
        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(bones[1], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 1.0f, Light, o); // flare01.jpg

        fScale = (float)(WorldRandom() % 30) / 60.0f + 0.2f;
        fLight = (float)sinf((WorldTime) * 0.7f) * 0.2f + 0.3f;
        fRotation = (WorldTime * 0.0006f) * 360.0f;
        Vector(0.1f + fLight, 0.2f, 0.0f, Light);
        CreateSprite(BITMAP_LIGHT + 3, Position, fScale, Light, o, fRotation); // impact01.jpg

        Vector(1.0f - fLight, 0.0f, 0.0f, Light);
        if (swordDancerPosition >= 20.0f)
        {
            swordDancerRandom = WorldRandom() % 5 + 2; // 2 ~ 6
            swordDancerPosition = 0.0f;
            //Vector(1.0f, 0.0f, 0.0f, Light);
        }
        else
            swordDancerPosition += 1.5f;
        Vector(0.f, swordDancerPosition, 0.f, p);
        b->TransformPosition(bones[swordDancerRandom], p, Position, true);
        CreateSprite(BITMAP_WATERFALL_4, Position, 0.7f, Light, o);
        Vector(0.1f, 0.1f, 0.1f, Light);
        CreateSprite(BITMAP_WATERFALL_2, Position, 0.3f, Light, o);

        // Flare01
        fLight = (float)sinf((WorldTime) * 0.4f) * 0.25f + 0.2f;
        Vector(0.8f + fLight, 0.1f, 0.f, Light);
        Vector(0.f, 0.f, 0.f, p);
        for (int i = 0; i < 5; ++i)
        {
            b->TransformPosition(bones[2 + i], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 1.1f, Light, o);
        }

        if (o->CurrentAction == PLAYER_RUN_TWO_HAND_SWORD_TWO &&
            !TheMapProcess().CharacterPolicy().skyTerrain)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
            {
                ItemBirthPose birthPose(sessionKeeper_, *this, parentDraw, linkedItem, birth);
                vec3_t emissionAngle;
                VectorCopy(o->Angle, emissionAngle);
                emissionAngle[2] =
                    o->MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), emissionAngle[2]);
                auto *bones = birthPose.Bones();
                if (!TheMapProcess().TerrainCutscene())
                {
                    Vector(1.f, 1.f, 1.f, Light);
                    Vector(0.f, 0.f, 0.f, p);
                    b->TransformPosition(bones[6], p, Position, true);
                    Position[0] += WorldRandom() % 30 - 15.f;
                    Position[1] += WorldRandom() % 30 - 15.f;
                    Position[2] += 20.f;

                    vec3_t Angle;
                    for (int i = 0; i < 4; i++)
                    {
                        Vector((float)(WorldRandom() % 60 + 60 + 90), 0.f, emissionAngle[2],
                               Angle); //(float)(WorldRandom()%30),Angle);
                        CreateJoint(BITMAP_JOINT_SPARK, Position, Position, Angle);
                        CreateParticle(BITMAP_SPARK, Position, Angle, Light);
                    }
                }
            }
        }
    }
    break;
    case MODEL_SHINING_SCEPTER: {
        float fScale;
        Luminosity = (float)sinf((WorldTime) * 0.002f) * 0.3f + 0.1f;
        fScale = (float)(WorldRandom() % 10) / 30.0f + 1.7f;

        Vector(0.f, 0.f, 0.f, p);

        Vector(0.8f + Luminosity, 0.5f + Luminosity, 0.1f + Luminosity, Light);
        b->TransformPosition(bones[1], p, Position, true);
        CreateSprite(BITMAP_SHINY + 1, Position, fScale, Light, o);

        Vector(0.7f, 0.5f, 0.3f, Light);
        b->TransformPosition(bones[1], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 3.f, Light, o);

        Vector(0.8f, 0.6f, 0.4f, Light);
        b->TransformPosition(bones[2], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 1.5f, Light, o);

        Vector(0.8f, 0.6f, 0.4f, Light);
        b->TransformPosition(bones[3], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 1.2f, Light, o);
    }
    break;
    case MODEL_ALBATROSS_BOW: {
        float fLight;
        fLight = (float)sinf((WorldTime) * 0.4f) * 0.25f;
        Vector(0.0f, 0.7f, 0.0f, Light);
        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(bones[10], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 1.2f, Light, o);

        b->TransformPosition(bones[28], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 1.2f, Light, o);

        b->TransformPosition(bones[34], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 1.2f, Light, o);

        b->TransformPosition(bones[16], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 1.2f, Light, o);

        Vector(0.f, 0.f, 0.f, p);
        Vector(0.3f, 0.9f, 0.2f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
        {
            ItemBirthPose birthPose(sessionKeeper_, *this, parentDraw, linkedItem, birth);
            vec3_t emissionAngle;
            VectorCopy(o->Angle, emissionAngle);
            emissionAngle[2] =
                o->MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), emissionAngle[2]);
            auto *bones = birthPose.Bones();
            b->TransformPosition(bones[10], p, Position, true);
            CreateParticle(BITMAP_SPARK + 1, Position, emissionAngle, Light, 14, 0.05f);
            b->TransformPosition(bones[28], p, Position, true);
            CreateParticle(BITMAP_SPARK + 1, Position, emissionAngle, Light, 14, 0.05f);
            b->TransformPosition(bones[34], p, Position, true);
            CreateParticle(BITMAP_SPARK + 1, Position, emissionAngle, Light, 14, 0.05f);
            b->TransformPosition(bones[16], p, Position, true);
            CreateParticle(BITMAP_SPARK + 1, Position, emissionAngle, Light, 14, 0.05f);
        }
    }
    break;
    case MODEL_PLATINA_STAFF: {
        float fLight, fScale, fRotation;
        fLight = (float)sinf((WorldTime) * 0.7f) * 0.2f + 0.3f;
        Vector(0.f, 0.f, 0.f, p);

        // flare01
        Vector(0.7f, 0.7f, 0.7f, Light);
        fScale = (float)(WorldRandom() % 10) / 500.0f;
        b->TransformPosition(bones[9], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 0.3f + fScale, Light, o);
        //			Vector(0.1f, 0.5f, 0.1f, Light);
        //			CreateSprite(BITMAP_LIGHT, Position, 1.5f, Light, o);

        Vector(0.2f, 0.6f + fLight, 0.2f, Light);
        fScale = (float)(WorldRandom() % 10) / 500.0f;
        b->TransformPosition(bones[9], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 4.0f + fScale, Light, o);

        // shiny02
        Vector(0.4f, 0.5f + fLight, 0.4f, Light);
        fScale = (float)(WorldRandom() % 30) / 60.0f;
        fRotation = (WorldTime * 0.0004f) * 360.0f;
        b->TransformPosition(bones[9], p, Position, true);
        CreateSprite(BITMAP_SHINY + 1, Position, 1.2f + fScale, Light, o, -fRotation);

        // magic_ground1
        Vector(0.6f, 1.f, 0.6f, Light);
        fScale = (float)(WorldRandom() % 10) / 500.0f;
        fRotation = (WorldTime * 0.0006f) * 360.0f;
        b->TransformPosition(bones[9], p, Position, true);
        CreateSprite(BITMAP_MAGIC, Position, 0.25f + fScale, Light, o, fRotation);

        // 5, 6, 7, 8 flare01 , 10, 11, 12, 13
        Vector(0.f, 0.f, 0.f, p);

        for (int i = 0; i < 4; ++i)
        {
            Vector(0.1f, 0.8f, 0.1f, Light);
            b->TransformPosition(bones[5 + i], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 0.2f, Light, o);
            Vector(0.5f, 0.5f, 0.5f, Light);
            b->TransformPosition(bones[10 + i], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 0.3f, Light, o);
        }

        Vector(0.1f, 0.8f, 0.1f, Light);
        b->TransformPosition(bones[4], p, Position, true);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.6f + fScale, Light, o, fRotation);

        Vector(0.6f, 1.f, 0.6f, Light);
        fScale = (float)(WorldRandom() % 10) / 500.0f;
        fRotation = (WorldTime * 0.0006f) * 360.0f;
        b->TransformPosition(bones[2], p, Position, true);
        CreateSprite(BITMAP_MAGIC, Position, 0.15f + fScale, Light, o, fRotation);
        Vector(0.8f, 0.8f, 0.8f, Light);
        b->TransformPosition(bones[2], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 0.7f + fScale, Light, o, -fRotation);
        Vector(0.1f, 1.0f, 0.1f, Light);
        b->TransformPosition(bones[2], p, Position, true);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f + fScale, Light, o, fRotation);

        Vector(0.2f, 0.6f + fLight, 0.2f, Light);
        b->TransformPosition(bones[2], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 2.0f + fScale, Light, o);
    }
    break;
    case MODEL_STAFF_OF_KUNDUN: {
        Luminosity = (float)sinf((WorldTime) * 0.002f) * 0.3f + 0.7f;

        Vector(0.f, 0.f, 0.f, p);
        Vector(Luminosity * 0.f, Luminosity * 0.5f, Luminosity * 1.f, Light);

        auto Rotation = (float)(WorldRandom() % 360);
        b->TransformPosition(bones[5], p, Position, true);
        Vector(Luminosity * 0.f, Luminosity * 0.5f, Luminosity * 1.f, Light);
        CreateSprite(BITMAP_LIGHT, Position, 1.f, Light, o);
        Vector(0.5f, 0.5f, 0.5f, Light);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f, Light, o, Rotation);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f, Light, o, 360.f - Rotation);

        b->TransformPosition(bones[6], p, Position, true);
        Vector(Luminosity * 0.f, Luminosity * 0.5f, Luminosity * 1.f, Light);
        CreateSprite(BITMAP_LIGHT, Position, 1.f, Light, o);
        Vector(0.5f, 0.5f, 0.5f, Light);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f, Light, o, Rotation);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f, Light, o, 360.f - Rotation);

        b->TransformPosition(bones[8], p, Position, true);
        Vector(Luminosity * 0.f, Luminosity * 0.5f, Luminosity * 1.f, Light);
        CreateSprite(BITMAP_LIGHT, Position, 2.f, Light, o);
        Vector(0.5f, 0.5f, 0.5f, Light);
        CreateSprite(BITMAP_SHINY + 1, Position, 1.f, Light, o, Rotation);
        CreateSprite(BITMAP_SHINY + 1, Position, 1.f, Light, o, 360.f - Rotation);
    }
    break;
    case MODEL_GREAT_LORD_SCEPTER: {
        auto Rotation = (float)(WorldRandom() % 360);
        Luminosity = (float)sinf((WorldTime) * 0.002f) * 0.3f + 0.7f;

        Vector(0.f, 0.f, 0.f, p);

        b->TransformPosition(bones[1], p, Position, true);
        Vector(Luminosity * 0.f, Luminosity * 0.5f, Luminosity * 1.f, Light);
        CreateSprite(BITMAP_LIGHT, Position, 2.f, Light, o);
        Vector(0.5f, 0.5f, 0.5f, Light);
        CreateSprite(BITMAP_SHINY + 1, Position, 1.f, Light, o, Rotation);
        CreateSprite(BITMAP_SHINY + 1, Position, 1.f, Light, o, 360.f - Rotation);
    }
    break;
    case MODEL_DIVINE_SWORD_OF_ARCHANGEL:
        Vector(Luminosity * 1.f, Luminosity * 0.3f, Luminosity * 0.1f, Light);

        Vector(0.f, 0.f, 0.f, p);

        for (int i = 0; i < 7; ++i)
        {
            vec3_t Light2;
            Vector(0.4f, 0.4f, 0.4f, Light2);
            b->TransformPosition(bones[i + 2], p, Position, true);

            if (rand_fps_check(3))
            {
                CreateSprite(BITMAP_SHINY + 1, Position, 0.6f, Light2, o,
                             (float)(WorldRandom() % 360));
            }
            CreateSprite(BITMAP_LIGHT, Position, 2.f, Light, o);
        }

        if (((o->CurrentAction < PLAYER_WALK_MALE || o->CurrentAction > PLAYER_RUN_RIDE_WEAPON) &&
             (o->CurrentAction < PLAYER_ATTACK_SKILL_SWORD1 ||
              o->CurrentAction > PLAYER_ATTACK_SKILL_SWORD5)))
        {
            vec3_t pos, delta, angle;

            Vector(0.f, 0.f, 0.f, p);
            Vector(-90.f, (float)(WorldRandom() % 360), o->Angle[2] - 45, angle);
            b->TransformPosition(bones[3], p, pos, true);
            b->TransformPosition(bones[2], p, Position, true);

            VectorSubtract(pos, Position, delta);
        }
        break;
    case MODEL_DIVINE_CB_OF_ARCHANGEL:
        Vector(Luminosity * 1.f, Luminosity * 0.3f, Luminosity * 0.1f, Light);

        break;
    case MODEL_GREAT_REIGN_CROSSBOW:
        Vector(0.f, 0.f, 10.f, p);

        for (int i = 1; i < 6; ++i)
        {
            Vector(Luminosity * 0.5f, Luminosity * 0.5f, Luminosity * 0.8f, Light);
            b->TransformPosition(bones[i], p, Position, true);

            CreateSprite(BITMAP_SHINY + 1, Position, 1.f, Light, o);

            if (i == 5)
            {
                Vector(1.f, 1.f, 1.f, Light);
                CreateSprite(BITMAP_LIGHT, Position, 2.f, Light, o);
            }
            else
                CreateSprite(BITMAP_LIGHT, Position, 2.f, Light, o);
        }
        break;
    case MODEL_GRAND_SOUL_SHIELD:
        Vector(Luminosity * 0.6f, Luminosity * 0.6f, Luminosity * 2.f, Light);

        Vector(15.f, -15.f, 0.f, p);
        b->TransformPosition(bones[1], p, Position, true);
        CreateSprite(BITMAP_SHINY + 1, Position, 1.5f, Light, o);
        CreateSprite(BITMAP_LIGHT, Position, Luminosity + 1.5f, Light, o);
        break;
    case MODEL_ELEMENTAL_MACE:
        Vector(Luminosity * 1.f, Luminosity * 0.9f, Luminosity * 0.f, Light);

        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(bones[1], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 2.f, Light, o);

        Vector(0.5f, 0.5f, 0.5f, Light);
        CreateSprite(BITMAP_LIGHT, Position, sinf(WorldTime * 0.002f) + 0.5f, Light, o);
        break;
    case MODEL_BATTLE_SCEPTER: {
        float Scale = sinf(WorldTime * 0.001f) + 1.f;
        Vector(Luminosity * 0.2f, Luminosity * 0.1f, Luminosity * 3.f, Light);
        Vector(-15.f, 0.f, 0.f, p);
        b->TransformPosition(bones[1], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, Scale, Light, o);
        Vector(10.f, 0.f, 0.f, p);
        b->TransformPosition(bones[1], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, Scale, Light, o);

        Scale = sinf(WorldTime * 0.01f) * 360;
        Luminosity = sinf(WorldTime * 0.001f) * 0.5f + 0.5f;
        Vector(Luminosity, Luminosity, Luminosity, Light);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.6f, Light, o, 360 - Scale);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.6f, Light, o, Scale);
    }
    break;
    case MODEL_MASTER_SCEPTER: {
        for (int i = 1; i < 5; i++)
        {
            Luminosity = (float)sinf((WorldTime) * 0.002f) * 0.35f + 0.65f;
            Vector(Luminosity * 0.6f, Luminosity * 0.8f, Luminosity * 1.f, Light);
            Vector(-10.f, 0.f, 0.f, p);
            b->TransformPosition(bones[i + 1], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 1.f, Light, o);

            if (i == 3)
            {
                Vector(0.5f, 0.5f, 0.5f, Light);
                CreateSprite(BITMAP_SHINY + 1, Position, 0.6f, Light, o, WorldRandom() % 360);
            }
        }
    }
    break;
    case MODEL_GREAT_SCEPTER: {
        Luminosity = sinf(WorldTime * 0.001f) * 0.5f + 0.7f;
        Vector(Luminosity * 1.f, Luminosity * 0.8f, Luminosity * 0.6f, Light);
        Vector(0.f, 0.f, 0.f, p);

        b->TransformPosition(bones[2], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 1.5f, Light, o);
        CreateSprite(BITMAP_SHINY + 1, Position, Luminosity * 0.5f, Light, o);

        Vector(Luminosity * -10.f, 0.f, 0.f, p);
        Vector(0.6f, 0.8f, 1.f, Light);
        Luminosity = WorldRandom() % 360;
        b->TransformPosition(bones[1], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 0.7f, Light, o);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f, Light, o, Luminosity);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.4f, Light, o, 360 - Luminosity);
    }
    break;
    case MODEL_LORD_SCEPTER: {
        Vector(1.f, 0.6f, 0.3f, Light);
        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(bones[1], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 1.3f, Light, o);

        for (int i = 0; i < 3; ++i)
        {
            Vector(i * 15.f - 10.f, 0.f, 0.f, p);
            b->TransformPosition(bones[2], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 1.3f, Light, o);
            CreateSprite(BITMAP_SHINY + 1, Position, 0.4f, Light, o, WorldRandom() % 360);
        }
    }
    break;
    case MODEL_DIVINE_SCEPTER_OF_ARCHANGEL: {
        Vector(Luminosity * 1.f, Luminosity * 0.3f, Luminosity * 0.1f, Light);
        Vector(0.f, 0.f, 0.f, p);
        vec3_t Light2;
        Vector(0.4f, 0.4f, 0.4f, Light2);
        b->TransformPosition(bones[0], p, Position, true);
        if (rand_fps_check(3))
        {
            CreateSprite(BITMAP_SHINY + 1, Position, 0.6f, Light2, o, (float)(WorldRandom() % 360));
        }
        CreateSprite(BITMAP_LIGHT, Position, 2.f, Light, o);
    }
    break;
    case MODEL_KNIGHT_BLADE: {
        for (int i = 0; i < 2; i++)
        {
            Vector(0.f, 0.f, 0.f, p);
            b->TransformPosition(bones[i + 1], p, Position, true);
            Luminosity = (float)sinf((WorldTime) * 0.002f) * 0.35f + 0.65f;
            Vector(Luminosity * 0.43f, Luminosity * 0.14f, Luminosity * 0.6f, Light);
            CreateSprite(BITMAP_LIGHT, Position, Luminosity * 0.9f, Light, o);
            Vector(0.3f, 0.3f, 0.3f, Light);
            CreateSprite(BITMAP_LIGHT, Position, float(sinf(WorldTime * 0.002f) * 0.5f) + 0.4f,
                         Light, o);
        }
    }
    break;
    case MODEL_ARROW_VIPER_BOW: {
        float Scale = sinf(WorldTime * 0.001f) + 1.f;
        Vector(Luminosity * 3.f, Luminosity, Luminosity, Light);
        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(bones[6], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, Scale * 0.8f, Light, o);
        b->TransformPosition(bones[2], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, Scale * 0.8f, Light, o);

        float Rotation = sinf(WorldTime * 0.01f) * 360;
        Luminosity = sinf(WorldTime * 0.001f) * 0.3 + 0.3f;
        Vector(Luminosity, Luminosity, Luminosity, Light);
        if (!c->SafeZone)
        {
            Vector(10.f, 0.f, 0.f, p);
            b->TransformPosition(bones[9], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, Scale * 0.8f, Light, o);
            Vector(Luminosity * 3.0f, Luminosity, Luminosity, Light);
            CreateSprite(BITMAP_SHINY + 1, Position, 0.8f, Light, o, 360 - Rotation);
            CreateSprite(BITMAP_SHINY + 1, Position, 0.8f, Light, o, Rotation);
        }
    }
    break;
    case MODEL_DARK_REIGN_BLADE:
    case MODEL_RUNE_BLADE:
        if (o->CurrentAction == PLAYER_RUN_TWO_HAND_SWORD_TWO &&
            !TheMapProcess().CharacterPolicy().skyTerrain)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
            {
                ItemBirthPose birthPose(sessionKeeper_, *this, parentDraw, linkedItem, birth);
                vec3_t emissionAngle;
                VectorCopy(o->Angle, emissionAngle);
                emissionAngle[2] =
                    o->MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), emissionAngle[2]);
                auto *bones = birthPose.Bones();
                if (!TheMapProcess().TerrainCutscene())
                {
                    Vector(1.f, 1.f, 1.f, Light);
                    Vector(0.f, 0.f, 0.f, p);
                    b->TransformPosition(bones[1], p, Position, true);
                    Position[0] += WorldRandom() % 30 - 15.f;
                    Position[1] += WorldRandom() % 30 - 15.f;
                    Position[2] += 20.f;

                    vec3_t Angle;
                    for (int i = 0; i < 4; i++)
                    {
                        Vector((float)(WorldRandom() % 60 + 60 + 90), 0.f, emissionAngle[2],
                               Angle); //(float)(WorldRandom()%30),Angle);
                        CreateJoint(BITMAP_JOINT_SPARK, Position, Position, Angle);
                        CreateParticle(BITMAP_SPARK, Position, Angle, Light);
                    }
                }
            }
        }
        break;
    case MODEL_DRAGON_SPEAR:
        Vector(Luminosity * 0.2f, Luminosity * 0.1f, Luminosity * 0.8f, Light);
        Vector(0.f, 0.f, 0.f, p);

        for (int i = 1; i < 9; i++)
        {
            b->TransformPosition(bones[i], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 1.3f, Light, o);
        }
        break;
    case MODEL_FLAMBERGE: {
        Vector(0.8f, 0.6f, 0.2f, Light);
        b->TransformByObjectBone(Position, Object, 11); // Gold01
        CreateSprite(BITMAP_LIGHT, Position, 0.6f, Light, o);
        CreateSprite(BITMAP_LIGHT, Position, 0.6f, Light, o);

        Vector(0.3f, 0.8f, 0.7f, Light);
        b->TransformByObjectBone(Position, Object, 12); // b01
        CreateSprite(BITMAP_LIGHT, Position, 0.5f, Light, o);
        b->TransformByObjectBone(Position, Object, 13); // n02
        CreateSprite(BITMAP_LIGHT, Position, 0.5f, Light, o);

        b->TransformByObjectBone(Position, Object, 12); // n04
        CreateSprite(BITMAP_LIGHT, Position, 0.3f, Light, o);
        b->TransformByObjectBone(Position, Object, 13); // b03
        CreateSprite(BITMAP_LIGHT, Position, 0.3f, Light, o);

        Vector(0.9f, 0.1f, 0.1f, Light);
        b->TransformByObjectBone(Position, Object, 1); // Zx01
        CreateSprite(BITMAP_LIGHTMARKS, Position, 0.8f, Light, o);
        b->TransformByObjectBone(Position, Object, 2); // Zx02
        CreateSprite(BITMAP_LIGHTMARKS, Position, 0.7f, Light, o);
        b->TransformByObjectBone(Position, Object, 3); // Zx03
        CreateSprite(BITMAP_LIGHTMARKS, Position, 0.7f, Light, o);
        b->TransformByObjectBone(Position, Object, 4); // Zx04
        CreateSprite(BITMAP_LIGHTMARKS, Position, 0.7f, Light, o);
        b->TransformByObjectBone(Position, Object, 5); // Zx05
        CreateSprite(BITMAP_LIGHTMARKS, Position, 0.6f, Light, o);
        b->TransformByObjectBone(Position, Object, 6); // Zx06
        CreateSprite(BITMAP_LIGHTMARKS, Position, 0.5f, Light, o);
        b->TransformByObjectBone(Position, Object, 7); // Zx07
        CreateSprite(BITMAP_LIGHTMARKS, Position, 0.5f, Light, o);
    }
    break;
    case MODEL_SWORD_BREAKER: {
        Vector(0.1f, 0.9f, 0.1f, Light);

        b->TransformByObjectBone(Position, Object, 1); // Zx01
        CreateSprite(BITMAP_LIGHTMARKS, Position, 0.6f, Light, o);
        b->TransformByObjectBone(Position, Object, 2); // Zx02
        CreateSprite(BITMAP_LIGHTMARKS, Position, 0.5f, Light, o);
        b->TransformByObjectBone(Position, Object, 3); // Zx03
        CreateSprite(BITMAP_LIGHTMARKS, Position, 0.5f, Light, o);
        b->TransformByObjectBone(Position, Object, 4); // Zx04
        CreateSprite(BITMAP_LIGHTMARKS, Position, 0.5f, Light, o);
        b->TransformByObjectBone(Position, Object, 5); // Zx05
        CreateSprite(BITMAP_LIGHTMARKS, Position, 0.4f, Light, o);
        b->TransformByObjectBone(Position, Object, 6); // Zx06
        CreateSprite(BITMAP_LIGHTMARKS, Position, 0.3f, Light, o);
        b->TransformByObjectBone(Position, Object, 7); // Zx07
        CreateSprite(BITMAP_LIGHTMARKS, Position, 0.3f, Light, o);

        for (int i = 1; i <= 7; i++)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.f))
            {
                ItemBirthPose birthPose(sessionKeeper_, *this, parentDraw, linkedItem, birth);
                auto birthDraw = birthPose.Draw();
                auto birthItem = birthPose.Draw();
                Vector(0.f, 0.f, 0.f, Position);
                b->TransformByObjectBone(Position, birthItem, i);
                CreateParticle(BITMAP_WATERFALL_4, Position, birthDraw.angle, Light, 12, 0.5f,
                               Object);
            }
        }
    }
    break;
    case MODEL_IMPERIAL_SWORD: {
        float fRendomScale = (float)((WorldRandom() % 15) / 30.0f) + 0.5f;
        Vector(0.f, 0.f, 0.f, Position);
        Vector(0.1f, 0.4f, 0.9f, Light);
        b->TransformPosition(bones[8], Position, p, true); // Zx01
        CreateSprite(BITMAP_FLARE_BLUE, p, 0.4f, o->Light, o);
        CreateSprite(BITMAP_SHINY + 6, p, fRendomScale, Light, o);

        // 잔상 Zx01
        vec3_t vColor;
        VectorCopy(p, o->EyeLeft);
        Vector(0.f, 0.f, 0.9f, vColor);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
        {
            ItemBirthPose birthPose(sessionKeeper_, *this, parentDraw, linkedItem, birth);
            vec3_t emissionAngle;
            VectorCopy(o->Angle, emissionAngle);
            emissionAngle[2] =
                o->MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), emissionAngle[2]);
            vec3_t offset{}, p;
            b->TransformPosition(birthPose.Bones()[8], offset, p, true);
            CreateJoint(BITMAP_JOINT_ENERGY, p, p, emissionAngle, 17, o, 25.f);
            //CreateEffect(MODEL_EFFECT_TRACE, p, emissionAngle, vColor, 0, NULL, -1, 0, 0, 0, 25.f);
        }

        b->TransformPosition(bones[9], Position, p, true); // Zx02
        CreateSprite(BITMAP_FLARE_BLUE, p, 0.4f, o->Light, o);
        CreateSprite(BITMAP_SHINY + 6, p, fRendomScale, Light, o);
        b->TransformPosition(bones[10], Position, p, true); // Zx03
        CreateSprite(BITMAP_FLARE_BLUE, p, 0.4f, o->Light, o);
        CreateSprite(BITMAP_SHINY + 6, p, 0.4f, Light, o);
        b->TransformPosition(bones[11], Position, p, true); // Zx04
        CreateSprite(BITMAP_FLARE_BLUE, p, 0.4f, o->Light, o);
        CreateSprite(BITMAP_SHINY + 6, p, 0.4f, Light, o);

        // 칼주변
        Vector(0.0f, 0.3f, 0.7f, Light);
        b->TransformPosition(bones[2], Position, p, true); // rx01
        CreateSprite(BITMAP_LIGHTMARKS, p, 1.0f, Light, o);
        b->TransformPosition(bones[3], Position, p, true); // rx02
        CreateSprite(BITMAP_LIGHTMARKS, p, 0.8f, Light, o);
        b->TransformPosition(bones[4], Position, p, true); // rx03
        CreateSprite(BITMAP_LIGHTMARKS, p, 0.6f, Light, o);
        b->TransformPosition(bones[5], Position, p, true); // rx04
        CreateSprite(BITMAP_LIGHTMARKS, p, 0.4f, Light, o);
        b->TransformPosition(bones[6], Position, p, true); // Zx05
        CreateSprite(BITMAP_LIGHTMARKS, p, 0.2f, Light, o);
        b->TransformPosition(bones[7], Position, p, true); // Zx06
        CreateSprite(BITMAP_LIGHTMARKS, p, 0.1f, Light, o);
    }
    break;
    case MODEL_FROST_MACE: {
        vec3_t vDPos;
        Vector(0.5f, 0.8f, 0.5f, Light);
        // Zx04
        b->TransformByObjectBone(Position, Object, 11);
        CreateSprite(BITMAP_SHINY + 6, Position, 0.2f, Light, o);
        // Zx05
        b->TransformByObjectBone(Position, Object, 10);
        CreateSprite(BITMAP_SHINY + 6, Position, 0.2f, Light, o);

        Vector(0.9f, 0.1f, 0.3f, Light);
        // Zx02
        b->TransformByObjectBone(Position, Object, 9);
        CreateSprite(BITMAP_SHINY + 6, Position, 0.2f, Light, o);
        // Zx03
        b->TransformByObjectBone(Position, Object, 8);
        CreateSprite(BITMAP_SHINY + 6, Position, 0.2f, Light, o);
        // Zx01
        b->TransformByObjectBone(Position, Object, 1);
        CreateSprite(BITMAP_SHINY + 6, Position, 0.2f, Light, o);
        CreateSprite(BITMAP_LIGHT, Position, 0.8f, Light, o);

        Vector(0.5f, 0.8f, 0.6f, Light);
        // Zx001
        b->TransformByObjectBone(Position, Object, 24);
        CreateSprite(BITMAP_SHINY + 6, Position, 0.3f, Light, o);
        CreateSprite(BITMAP_LIGHT, Position, 2.0f, Light, o);
        CreateSprite(BITMAP_PIN_LIGHT, Position, 0.5f, Light, o, ((int)(WorldTime * 0.04f) % 360));
        CreateSprite(BITMAP_PIN_LIGHT, Position, 0.7f, Light, o, -((int)(WorldTime * 0.03f) % 360));
        CreateSprite(BITMAP_PIN_LIGHT, Position, 0.9f, Light, o, ((int)(WorldTime * 0.02f) % 360));

        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            ItemBirthPose birthPose(sessionKeeper_, *this, parentDraw, linkedItem, birth);
            vec3_t emissionAngle;
            VectorCopy(o->Angle, emissionAngle);
            emissionAngle[2] =
                o->MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), emissionAngle[2]);
            vec3_t Position;
            b->TransformByObjectBone(Position, birthPose.Draw(), 24);
            float fTemp = Position[2];
            Position[2] -= 15.f;
            Vector(0.5f, 0.8f, 0.8f, Light);
            CreateParticle(BITMAP_CLUD64, Position, emissionAngle, Light, 9, 0.4f);
            Position[2] = fTemp;
        }

        Vector(0.5f, 0.8f, 0.6f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.f))
        {
            ItemBirthPose birthPose(sessionKeeper_, *this, parentDraw, linkedItem, birth);
            vec3_t emissionAngle;
            VectorCopy(o->Angle, emissionAngle);
            emissionAngle[2] =
                o->MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), emissionAngle[2]);
            vec3_t Position;
            b->TransformByObjectBone(Position, birthPose.Draw(), 24);
            vDPos[0] = Position[0] + (WorldRandom() % 20 - 10) * 3.f;
            vDPos[1] = Position[1] + (WorldRandom() % 20 - 10) * 3.f;
            vDPos[2] = Position[2] + (WorldRandom() % 20 - 10) * 3.f;
            CreateEffect(MODEL_STAR_SHINE, vDPos, emissionAngle, Light, 0, Object, -1, 0, 0, 0,
                         0.22f);
        }

        // Zx06
        b->TransformByObjectBone(Position, Object, 12);
        CreateSprite(BITMAP_LIGHT, Position, 0.2f, Light, o);
        // Zx07
        b->TransformByObjectBone(Position, Object, 13);
        CreateSprite(BITMAP_LIGHT, Position, 0.2f, Light, o);
        // Zx08
        b->TransformByObjectBone(Position, Object, 14);
        CreateSprite(BITMAP_LIGHT, Position, 0.2f, Light, o);
        // Zx09
        b->TransformByObjectBone(Position, Object, 15);
        CreateSprite(BITMAP_LIGHT, Position, 0.2f, Light, o);
        // Zx10
        b->TransformByObjectBone(Position, Object, 16);
        CreateSprite(BITMAP_LIGHT, Position, 0.3f, Light, o);
        // Zx11
        b->TransformByObjectBone(Position, Object, 17);
        CreateSprite(BITMAP_LIGHT, Position, 0.3f, Light, o);
        // Zx12
        b->TransformByObjectBone(Position, Object, 18);
        CreateSprite(BITMAP_LIGHT, Position, 0.3f, Light, o);
        // Zx13
        b->TransformByObjectBone(Position, Object, 19);
        CreateSprite(BITMAP_LIGHT, Position, 0.3f, Light, o);
        // Zx14
        b->TransformByObjectBone(Position, Object, 20);
        CreateSprite(BITMAP_LIGHT, Position, 0.3f, Light, o);
        // Zx15
        b->TransformByObjectBone(Position, Object, 21);
        CreateSprite(BITMAP_LIGHT, Position, 0.4f, Light, o);
        // Zx16
        b->TransformByObjectBone(Position, Object, 22);
        CreateSprite(BITMAP_LIGHT, Position, 0.4f, Light, o);
        // Zx17
        b->TransformByObjectBone(Position, Object, 23);
        CreateSprite(BITMAP_LIGHT, Position, 0.4f, Light, o);
    }
    break;
    case MODEL_ABSOLUTE_SCEPTER: {
        float fRandomScale;
        vec3_t vPosZx01, vPosZx02, vLight1, vLight2, vDLight;

        float fLumi = absf((sinf(WorldTime * 0.0008f))) * 0.8 + 0.2f;
        Vector(fLumi * 0.6f, fLumi * 0.5f, fLumi * 0.8f, vDLight);

        Vector(0.6f, 0.5f, 0.8f, vLight1);
        Vector(0.8f, 0.8f, 0.8f, vLight2);
        b->TransformByObjectBone(vPosZx01, Object, 3); // Zx01
        b->TransformByObjectBone(vPosZx02, Object, 4); // Zx02

        AdvanceScepterVisual(linkedItem, parentDraw);
        // Object->m_iAnimation Random Texture
        int iRandomTexure1, iRandomTexure2;
        iRandomTexure1 = (Object->m_iAnimation / 10) % 3; // 3개
        iRandomTexure2 = (Object->m_iAnimation) % 3;      // 3개

        // Zx01
        fRandomScale = (float)(WorldRandom() % 10) / 10.0f + 1.0f; //(1.0~2.0)
        CreateSprite(BITMAP_LIGHT, vPosZx01, fRandomScale, vLight1, o);
        CreateSprite(BITMAP_SHINY + 1, vPosZx01, 0.5f, vDLight, o);
        VectorAdd(vPosZx01, Object->EyeRight, vPosZx01);
        CreateSprite(BITMAP_LIGHTNING_MEGA1 + iRandomTexure1, vPosZx01,
                     (((WorldRandom() % 11) - 20) / 100.f) + 0.5f, vLight2, o, WorldRandom() % 380);

        // Zx02
        fRandomScale = (float)((WorldRandom() % 10) / 5.0f) + 1.5f; //(2.0~3.25)
        CreateSprite(BITMAP_LIGHT, vPosZx02, fRandomScale, vLight1, o);
        CreateSprite(BITMAP_SHINY + 1, vPosZx02, 1.0f, vDLight, o);
        VectorAdd(vPosZx02, Object->EyeRight2, vPosZx02);
        CreateSprite(BITMAP_LIGHTNING_MEGA1 + iRandomTexure1, vPosZx02,
                     (((WorldRandom() % 11) - 20) / 50.f) + 0.8f, vLight2, o, WorldRandom() % 380);
    }
    break;
    case MODEL_STINGER_BOW: {
        // Bow_24.bmd authored feather sockets: zx03 and zx04.
        constexpr int featherSocketLeft = 24;
        constexpr int featherSocketRight = 42;

        Vector(0.2f, 0.25f, 0.3f, Light);

        for (int i = 0; i <= 43; i++)
        {
            if (i == 1)
            {
                continue;
            }
            b->TransformByObjectBone(Position, Object, i); //
            CreateSprite(BITMAP_LIGHT, Position, 0.8f, Light, Object);
        }

        if (visual.animationFrame >= 4.5f &&
            (visual.animationFrame <= 5.f || visual.intervalStartFrame < 5.f))
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
            {
                ItemBirthPose birthPose(sessionKeeper_, *this, parentDraw, linkedItem, birth);
                vec3_t emissionAngle;
                VectorCopy(o->Angle, emissionAngle);
                emissionAngle[2] =
                    o->MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), emissionAngle[2]);
                vec3_t vZX03, vZx04;
                auto birthItem = birthPose.Draw();
                b->TransformByObjectBone(vZX03, birthItem, featherSocketLeft);
                b->TransformByObjectBone(vZx04, birthItem, featherSocketRight);
                const int iNumCreateFeather = WorldRandom() % 3;
                for (int i = 0; i < iNumCreateFeather; i++)
                {
                    CreateEffect(MODEL_FEATHER, vZX03, emissionAngle, Light, 0, NULL, -1, 0, 0, 0,
                                 0.6f);
                    CreateEffect(MODEL_FEATHER, vZX03, emissionAngle, Light, 1, NULL, -1, 0, 0, 0,
                                 0.6f);
                    CreateEffect(MODEL_FEATHER, vZx04, emissionAngle, Light, 0, NULL, -1, 0, 0, 0,
                                 0.6f);
                    CreateEffect(MODEL_FEATHER, vZx04, emissionAngle, Light, 1, NULL, -1, 0, 0, 0,
                                 0.6f);
                }
            }
        }
    }
    break;
    case MODEL_DEADLY_STAFF: {
        Vector(0.f, 0.f, 0.f, Position);
        Vector(0.8f, 0.3f, 0.1f, Light);
        b->TransformPosition(bones[4], Position, p, true); // Rx01
        CreateSprite(BITMAP_LIGHT, p, 0.8f, Light, o);
        b->TransformPosition(bones[5], Position, p, true); // Rx02
        CreateSprite(BITMAP_LIGHT, p, 0.4f, Light, o);
        b->TransformPosition(bones[6], Position, p, true); // Zx02
        CreateSprite(BITMAP_LIGHT, p, 0.8f, Light, o);
        b->TransformPosition(bones[7], Position, p, true); // Zx03
        CreateSprite(BITMAP_LIGHT, p, 0.8f, Light, o);
        b->TransformPosition(bones[8], Position, p, true); // Zx04
        CreateSprite(BITMAP_LIGHT, p, 2.0f, Light, o);
        b->TransformPosition(bones[9], Position, p, true); // Zx05
        CreateSprite(BITMAP_LIGHT, p, 0.8f, Light, o);

        float fLumi = absf((sinf(WorldTime * 0.001f))) * 0.8f + 0.2f;
        vec3_t vDLight;
        Vector(fLumi * 0.8f, fLumi * 0.1f, fLumi * 0.3f, vDLight);
        float fRendomScale = (float)(WorldRandom() % 10) / 20.0f + 0.8f; // (0.5~1.25)
        Vector(0.8f, 0.2f, 0.4f, Light);
        b->TransformPosition(bones[2], Position, p, true); // Red01
        CreateSprite(BITMAP_LIGHT, p, 2.0f, vDLight, o);
        CreateSprite(BITMAP_SHINY + 6, p, fRendomScale, Light, o);
        b->TransformPosition(bones[3], Position, p, true); // Red02
        CreateSprite(BITMAP_LIGHT, p, 2.0f, vDLight, o);
        CreateSprite(BITMAP_SHINY + 6, p, fRendomScale, Light, o);

        vec3_t vColor;
        VectorCopy(p, o->EyeRight);
        Vector(0.9f, 0.f, 0.f, vColor);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
        {
            ItemBirthPose birthPose(sessionKeeper_, *this, parentDraw, linkedItem, birth);
            vec3_t emissionAngle;
            VectorCopy(o->Angle, emissionAngle);
            emissionAngle[2] =
                o->MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), emissionAngle[2]);
            vec3_t offset{}, p;
            b->TransformPosition(birthPose.Bones()[3], offset, p, true);
            CreateJoint(BITMAP_JOINT_ENERGY, p, p, emissionAngle, 47, o, 25.f);
        }
    }
    break;
    case MODEL_IMPERIAL_STAFF: {
        Vector(0.f, 0.f, 0.f, Position);
        Vector(0.3f, 0.3f, 0.9f, Light);
        b->TransformPosition(bones[2], Position, p, true); // Zx01
        CreateSprite(BITMAP_LIGHT, p, 2.f, Light, o);
        CreateSprite(BITMAP_LIGHT, p, 2.f, Light, o);
        b->TransformPosition(bones[3], Position, p, true); // Zx02
        CreateSprite(BITMAP_LIGHT, p, 2.5f, Light, o);
        CreateSprite(BITMAP_LIGHT, p, 2.5f, Light, o);
        b->TransformPosition(bones[4], Position, p, true); // Zx03
        CreateSprite(BITMAP_LIGHT, p, 3.f, Light, o);
        Vector(0.7f, 0.1f, 0.2f, Light);
        b->TransformPosition(bones[5], Position, p, true); // Zx04
        CreateSprite(BITMAP_LIGHT, p, 2.f, Light, o);
        Vector(0.9f, 0.3f, 0.5f, Light);
        CreateSprite(BITMAP_SHINY + 6, p, 0.8f, Light, o);

        float fRendomScale = (float)(WorldRandom() % 15) / 20.0f + 1.0f;
        CreateSprite(BITMAP_SHINY + 1, p, fRendomScale, Light, o);
        CreateSprite(BITMAP_SHINY + 1, p, fRendomScale - 0.3f, Light, o, 90.0f);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
        {
            ItemBirthPose birthPose(sessionKeeper_, *this, parentDraw, linkedItem, birth);
            vec3_t emissionAngle;
            VectorCopy(o->Angle, emissionAngle);
            emissionAngle[2] =
                o->MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), emissionAngle[2]);
            vec3_t offset{}, p;
            b->TransformPosition(birthPose.Bones()[5], offset, p, true);
            CreateParticle(BITMAP_SPARK + 1, p, emissionAngle, Light, 11, 2.0f);
            CreateJoint(BITMAP_JOINT_ENERGY, p, p, emissionAngle, 17, o, 30.f);
            //vec3_t vColor;
            //Vector(0.f, 0.f, 0.9f, vColor);
            //CreateEffect(MODEL_EFFECT_TRACE, p, emissionAngle, vColor, 0, NULL, -1, 0, 0, 0, 30.f);
        }

        // 잔상

        VectorCopy(p, o->EyeLeft);

        Vector(0.7f, 0.7f, 0.7f, Light);
        CreateSprite(BITMAP_SHINY + 2, p, 2.f, Light, o);
    }
    break;
    case MODEL_STAFF + 32: {
        float fRandomScale;
        vec3_t vLight1, vLight2;
        Vector(0.9f, 0.7f, 0.4f, vLight1);
        Vector(0.9f, 0.1f, 0.3f, vLight2);

        b->TransformByObjectBone(Position, Object, 1); // Zx01
        Vector(1.0f, 0.1f, 0.2f, Light);
        CreateSprite(BITMAP_SHINY + 6, Position, 0.7f, Light, o);
        CreateSprite(BITMAP_LIGHTMARKS, Position, 0.6f, Light, o);
        CreateSprite(BITMAP_LIGHT, Position, 2.f, Light, o);

        Vector(0.7f, 0.1f, 0.6f, Light);
        CreateSprite(BITMAP_SHOCK_WAVE, Position, 0.65f, Light, o,
                     -((int)(WorldTime * 0.05f) % 360));
        // Object->Timer
        // Object->EyeRight
        Object->Timer += 0.01f * FPS_ANIMATION_FACTOR;
        if (Object->Timer <= 0.1f || Object->Timer > 0.9f)
        {
            Object->Timer = 0.15f;
            Vector(0.7f, 0.1f, 0.6f, Object->EyeRight);
        }
        if (Object->Timer > 0.5f)
        {
            const float decay = powf(0.95f, FPS_ANIMATION_FACTOR);
            VectorScale(Object->EyeRight, decay, Object->EyeRight);
        }

        CreateSprite(BITMAP_SHOCK_WAVE, Position, Object->Timer, Object->EyeRight, o);

        Vector(0.9f, 0.5f, 0.2f, Light);
        fRandomScale = (float)(WorldRandom() % 5) / 25.0f + 0.3f; // (0.3~0.4)
        CreateSprite(BITMAP_LIGHTMARKS, Position, fRandomScale, Light, o);

        b->TransformByObjectBone(Position, Object, 2); // Zx02
        CreateSprite(BITMAP_SHINY + 6, Position, 0.3f, vLight1, o);
        CreateSprite(BITMAP_LIGHTMARKS, Position, 0.4f, vLight2, o);

        b->TransformByObjectBone(Position, Object, 3); // Zx03
        CreateSprite(BITMAP_SHINY + 6, Position, 0.2f, vLight1, o);
        CreateSprite(BITMAP_LIGHTMARKS, Position, 0.3f, vLight2, o);
    }
    break;
    case MODEL_CRIMSONGLORY: {
        vec3_t vDLight;
        Vector(0.8f, 0.6f, 0.2f, Light);
        b->TransformByObjectBone(Position, Object, 4); // Zx03
        CreateSprite(BITMAP_LIGHT, Position, 1.5f, Light, o);
        b->TransformByObjectBone(Position, Object, 5); // Zx04
        CreateSprite(BITMAP_LIGHT, Position, 1.5f, Light, o);
        b->TransformByObjectBone(Position, Object, 6); // Zx05
        CreateSprite(BITMAP_LIGHT, Position, 1.5f, Light, o);
        b->TransformByObjectBone(Position, Object, 7); // Zx06
        CreateSprite(BITMAP_LIGHT, Position, 1.5f, Light, o);
        b->TransformByObjectBone(Position, Object, 8); // Zx07
        CreateSprite(BITMAP_LIGHT, Position, 1.5f, Light, o);
        b->TransformByObjectBone(Position, Object, 9); // Zx08
        CreateSprite(BITMAP_LIGHT, Position, 1.5f, Light, o);
        b->TransformByObjectBone(Position, Object, 10); // Zx09
        CreateSprite(BITMAP_LIGHT, Position, 1.5f, Light, o);

        float fLumi = absf((sinf(WorldTime * 0.0005f)));
        Vector(fLumi * 1.f, fLumi * 1.f, fLumi * 1.f, vDLight);
        b->TransformByObjectBone(Position, Object, 1); // Zx01
        CreateSprite(BITMAP_FLARE_RED, Position, 0.5f, vDLight, o);
        b->TransformByObjectBone(Position, Object, 2); // Zx02
        CreateSprite(BITMAP_FLARE_RED, Position, 0.3f, vDLight, o);
        b->TransformByObjectBone(Position, Object, 3); // Zx002
        CreateSprite(BITMAP_FLARE_RED, Position, 0.3f, vDLight, o);
    }
    break;
    case MODEL_SALAMANDER_SHIELD: {
        Vector(0.9f, 0.f, 0.2f, Light);
        b->TransformByObjectBone(Position, Object, 1); // Zx01
        CreateSprite(BITMAP_LIGHT, Position, 2.0f, Light, o);

        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
        {
            ItemBirthPose birthPose(sessionKeeper_, *this, parentDraw, linkedItem, birth);
            auto birthDraw = birthPose.Draw();
            vec3_t Position;
            b->TransformByObjectBone(Position, birthPose.Draw(), 1);
            Vector(1.f, 1.f, 1.f, Light);
            switch (WorldRandom() % 3)
            {
            case 0:
                CreateParticle(BITMAP_FIRE_HIK1, Position, birthDraw.angle, Light, 0, 0.7f);
                break;
            case 1:
                CreateParticle(BITMAP_FIRE_CURSEDLICH, Position, birthDraw.angle, Light, 4, 0.7f);
                break;
            case 2:
                CreateParticle(BITMAP_FIRE_HIK3, Position, birthDraw.angle, Light, 0, 0.7);
                break;
            }
        }
    }
    break;
    case MODEL_GUARDIAN_SHILED: {
        Vector(0.f, 0.f, 0.f, Position);
        float fLumi = fabs(sinf(WorldTime * 0.001f)) + 0.1f;
        Vector(0.8f * fLumi, 0.3f * fLumi, 0.8f * fLumi, Light);
        b->TransformPosition(bones[1], Position, p, true); // b01
        CreateSprite(BITMAP_LIGHT, p, 1.5f, Light, o);
        b->TransformPosition(bones[2], Position, p, true); // Zx01
        CreateSprite(BITMAP_LIGHT, p, 1.5f, Light, o);
        b->TransformPosition(bones[3], Position, p, true); // Zx05
        CreateSprite(BITMAP_LIGHT, p, 1.5f, Light, o);
        b->TransformPosition(bones[4], Position, p, true); // Zx04
        CreateSprite(BITMAP_LIGHT, p, 1.5f, Light, o);
        b->TransformPosition(bones[5], Position, p, true); // Object04
        CreateSprite(BITMAP_LIGHT, p, 1.5f, Light, o);
        b->TransformPosition(bones[6], Position, p, true); // Object05
        CreateSprite(BITMAP_LIGHT, p, 1.5f, Light, o);
        b->TransformPosition(bones[7], Position, p, true); // Zx02
        CreateSprite(BITMAP_LIGHT, p, 1.5f, Light, o);
        b->TransformPosition(bones[8], Position, p, true); // Object01
        CreateSprite(BITMAP_LIGHT, p, 1.5f, Light, o);
        b->TransformPosition(bones[9], Position, p, true); // Object03
        CreateSprite(BITMAP_LIGHT, p, 1.5f, Light, o);
    }
    break;
    case MODEL_CROSS_SHIELD: {
        vec3_t vPos, vLight;
        Vector(0.f, 0.f, 0.f, vPos);
        float fLumi = fabs(sinf(WorldTime * 0.001f)) + 0.1f;
        Vector(0.2f * fLumi, 0.2f * fLumi, 0.8f * fLumi, vLight);
        b->TransformByObjectBone(vPos, Object, 4);
        CreateSprite(BITMAP_LIGHT, vPos, 1.2f, vLight, Object);
        b->TransformByObjectBone(vPos, Object, 5);
        CreateSprite(BITMAP_LIGHT, vPos, 1.2f, vLight, Object);
        b->TransformByObjectBone(vPos, Object, 6);
        CreateSprite(BITMAP_LIGHT, vPos, 1.2f, vLight, Object);
        b->TransformByObjectBone(vPos, Object, 7);
        CreateSprite(BITMAP_LIGHT, vPos, 1.2f, vLight, Object);

        Vector(0.2f * fLumi, 0.6f * fLumi, 0.6f * fLumi, vLight);
        b->TransformByObjectBone(vPos, Object, 8);
        CreateSprite(BITMAP_LIGHT, vPos, 1.4f, vLight, Object);
        b->TransformByObjectBone(vPos, Object, 9);
        CreateSprite(BITMAP_LIGHT, vPos, 1.4f, vLight, Object);
        b->TransformByObjectBone(vPos, Object, 10);
        CreateSprite(BITMAP_LIGHT, vPos, 1.4f, vLight, Object);
        b->TransformByObjectBone(vPos, Object, 11);
        CreateSprite(BITMAP_LIGHT, vPos, 1.4f, vLight, Object);
        b->TransformByObjectBone(vPos, Object, 12);
        CreateSprite(BITMAP_LIGHT, vPos, 1.4f, vLight, Object);
        b->TransformByObjectBone(vPos, Object, 13);
        CreateSprite(BITMAP_LIGHT, vPos, 1.4f, vLight, Object);
    }
    break;
    case MODEL_CHROMATIC_STAFF: {
        vec3_t vPos, vLight;

        b->TransformByObjectBone(vPos, Object, 8);
        Vector(0.9f, 0.1f, 0.4f, vLight);
        CreateSprite(BITMAP_LIGHT, vPos, 3.5f, vLight, Object);
        vLight[0] = 0.1f + 0.8f * absf(sinf(WorldTime * 0.0008f));
        vLight[1] = 0.1f * absf(sinf(WorldTime * 0.0008f));
        vLight[2] = 0.1f + 0.4f * absf(sinf(WorldTime * 0.0008f));
        CreateSprite(BITMAP_MAGIC, vPos, 0.3f, vLight, Object);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
        {
            ItemBirthPose birthPose(sessionKeeper_, *this, parentDraw, linkedItem, birth);
            vec3_t emissionAngle;
            VectorCopy(o->Angle, emissionAngle);
            emissionAngle[2] =
                o->MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), emissionAngle[2]);
            vec3_t vPos, vLight;
            b->TransformByObjectBone(vPos, birthPose.Draw(), 8);
            const double birthTime =
                WorldTime - birth.SceneRemainingFrames() * 1000.0 /
                                sessionKeeper_.ApplicationConfig().legacyReferenceFps;
            const float pulse = absf(sinf(birthTime * 0.0008f));
            Vector(0.1f + 0.8f * pulse, 0.1f * pulse, 0.1f + 0.4f * pulse, vLight);
            CreateEffect(MODEL_MOONHARVEST_MOON, vPos, emissionAngle, vLight, 2, NULL, -1, 0, 0, 0,
                         0.12f);
        }

        Vector(0.8f, 0.8f, 0.2f, vLight);
        CreateSprite(BITMAP_SHINY + 1, vPos, 1.0f, vLight, Object);

        //작은 구슬
        for (int i = 1; i < 8; i++)
        {
            b->TransformByObjectBone(vPos, Object, i);
            Vector(0.8f, 0.1f, 0.4f, vLight);
            CreateSprite(BITMAP_LIGHT, vPos, 0.6f, vLight, Object);
            CreateSprite(BITMAP_MAGIC, vPos, 0.12f, vLight, Object);
            Vector(0.8f, 0.8f, 0.2f, vLight);
            CreateSprite(BITMAP_SHINY + 1, vPos, 0.3f, vLight, Object);
        }
    }
    break;
    case MODEL_RAVEN_STICK: {
        vec3_t vPos, vLight;

        //zx01
        b->TransformByObjectBone(vPos, Object, 1);
        Vector(0.4f, 0.2f, 1.0f, vLight);
        CreateSprite(BITMAP_SHINY + 6, vPos, 1.4f, vLight, Object);
        Vector(0.7f, 0.1f, 0.9f, vLight);
        CreateSprite(BITMAP_LIGHT, vPos, 1.5f, vLight, Object);
        Vector(0.0f, 0.0f, 1.0f, vLight);
        CreateSprite(BITMAP_SHINY + 2, vPos, 1.5f, vLight, Object);
        Vector(0.4f, 0.4f, 1.0f, vLight);
        CreateSprite(BITMAP_PIN_LIGHT, vPos, 0.7f, vLight, Object,
                     -((int)(WorldTime * 0.04f) % 360));
        CreateSprite(BITMAP_PIN_LIGHT, vPos, 0.8f, vLight, Object,
                     -((int)(WorldTime * 0.03f) % 360));

        float quarterAngle, theta, fSize;
        quarterAngle = Q_PI / 180.0f * (int(WorldTime * 0.02f) % 90);
        theta = absf(sinf(quarterAngle + Q_PI / 2));
        fSize = absf(sinf(quarterAngle)) * 0.5f;
        Vector(0.7f * theta, 0.1f * theta, 0.9f * theta, vLight);
        CreateSprite(BITMAP_MAGIC, vPos, fSize, vLight, Object);
        quarterAngle = Q_PI / 180.0f * (int(WorldTime * 0.05f) % 60 + 30);
        theta = absf(sinf(quarterAngle + Q_PI / 2));
        fSize = absf(sinf(quarterAngle)) * 0.5f;
        Vector(0.1f + 0.7f * theta, 0.1f * theta, 0.1f + 0.3f * theta, vLight);
        CreateSprite(BITMAP_MAGIC, vPos, fSize, vLight, Object);

        //zx02
        Vector(0.9f, 0.0f, 0.1f, vLight);
        b->TransformByObjectBone(vPos, Object, 2);
        CreateSprite(BITMAP_SHINY + 6, vPos, 0.8f, vLight, Object);

        //zx03
        Vector(0.9f, 0.4f, 0.7f, vLight);
        b->TransformByObjectBone(vPos, Object, 3);
        CreateSprite(BITMAP_SHINY + 6, vPos, 0.6f, vLight, Object);
    }
    break;
    case MODEL_BEUROBA: {
        vec3_t vPos, vLight;

        //zx01, zx02
        Vector(0.9f, 0.2f, 0.7f, vLight);
        b->TransformByObjectBone(vPos, Object, 3);
        CreateSprite(BITMAP_SHINY + 6, vPos, 0.8f, vLight, Object);
        CreateSprite(BITMAP_SHINY + 6, vPos, 0.3f, vLight, Object);
        b->TransformByObjectBone(vPos, Object, 4);
        CreateSprite(BITMAP_SHINY + 6, vPos, 0.8f, vLight, Object);
        CreateSprite(BITMAP_SHINY + 6, vPos, 0.3f, vLight, Object);

        //bone02
        Vector(0.2f, 0.9f, 0.1f, vLight);
        b->TransformByObjectBone(vPos, Object, 5);
        CreateSprite(BITMAP_LIGHT, vPos, 1.5f, vLight, Object);
    }
    break;
    case MODEL_STRYKER_SCEPTER: {
        vec3_t vPos, vLight;
        float fSize = 0.0f;

        //rx01
        b->TransformByObjectBone(vPos, Object, 5);
        Vector(0.5f, 0.8f, 0.9f, vLight);
        CreateSprite(BITMAP_SHINY + 1, vPos, 0.5f, vLight, Object);
        Vector(0.4f, 0.7f, 0.9f, vLight);
        CreateSprite(BITMAP_LIGHTMARKS, vPos, 1.0f, vLight, Object);

        AdvanceScepterVisual(linkedItem, parentDraw);

        //rx02
        Vector(0.5f, 0.6f, 0.9f, vLight);
        b->TransformByObjectBone(vPos, Object, 6);
        CreateSprite(BITMAP_SHINY + 1, vPos, 1.1f, vLight, Object);

        //zx 01, 02
        Vector(0.8f, 0.5f, 0.2f, vLight);
        b->TransformByObjectBone(vPos, Object, 2);
        CreateSprite(BITMAP_SHINY + 1, vPos, 0.5f, vLight, Object);
        b->TransformByObjectBone(vPos, Object, 3);
        CreateSprite(BITMAP_SHINY + 1, vPos, 0.5f, vLight, Object);

        //zx03
        Vector(1.0f, 1.0f, 1.0f, vLight);
        b->TransformByObjectBone(vPos, Object, 4);
        fSize = 0.5f * absf((sinf(WorldTime * 0.0015f)));
        CreateSprite(BITMAP_FLARE, vPos, fSize, vLight, Object);
    }
    break;
    case MODEL_AIR_LYN_BOW: {
        vec3_t vPos, vLight;
        float quarterAngle, theta, fSize;

        //zx02, 03
        Vector(0.2f, 0.8f, 0.5f, vLight);
        int iBoneNum[2] = {7, 11};

        for (int i = 0; i < 2; i++)
        {
            b->TransformByObjectBone(vPos, Object, iBoneNum[i]);

            CreateSprite(BITMAP_LIGHT, vPos, 0.8f, vLight, Object);
            CreateSprite(BITMAP_LIGHT, vPos, 0.8f, vLight, Object);

            quarterAngle = Q_PI / 180.0f * (int(WorldTime * 0.02f) % 90);
            theta = absf(sinf(quarterAngle + Q_PI / 2));
            fSize = absf(sinf(quarterAngle)) * 0.3f;
            Vector(0.2f * theta, 0.8f * theta, 0.5f * theta, vLight);
            CreateSprite(BITMAP_SHOCK_WAVE, vPos, fSize, vLight, Object);

            quarterAngle = Q_PI / 180.0f * (int(WorldTime * 0.05f) % 60 + 30);
            theta = absf(sinf(quarterAngle + Q_PI / 2));
            fSize = absf(sinf(quarterAngle)) * 0.3f;
            Vector(0.2f * theta, 0.8f * theta, 0.5f * theta, vLight);
            CreateSprite(BITMAP_SHOCK_WAVE, vPos, fSize, vLight, Object);
        }

        //model_bow action, frame
        if (Engine::Object::IsAttackAction(o->CurrentAction))
        {
            Vector(0.2f, 0.8f, 0.5f, vLight);
            for (int i = 0; i < 8; i++)
            {
                b->TransformByObjectBone(vPos, Object, 22 + i);
                CreateSprite(BITMAP_FLARE, vPos, 0.4f, vLight, Object);
            }
        }
    }
    break;
    case MODEL_SWORD_35_WING:
        vec3_t vLight;
        float fSize = (double)(WorldRandom() % 30) / 300.0;
        Vector(0.18f, 0.45f, 0.22f, vLight);
        Vector(0.f, 0.f, 0.f, p);
        for (int i = 0; i < 8; ++i)
        {
            b->TransformPosition(bones[3 + i], p, Position, true);
            CreateSprite(BITMAP_LIGHT_MARKS, Position, fSize + 0.3f, vLight, Object,
                         WorldRandom() % 360);
        }
        for (int i = 0; i < 8; ++i)
        {
            b->TransformPosition(bones[13 + i], p, Position, true);
            CreateSprite(BITMAP_LIGHT_MARKS, Position, fSize + 0.3f, vLight, Object,
                         WorldRandom() % 360);
        }
        break;
    }
}

void SessionVisualUnit::AdvanceSpecialMonsterVisual(CHARACTER &character)
{
    auto *c = &character;
    auto *o = &character.Object;
    auto *b = &Models[o->Type];
    vec3_t p{}, Position{}, Light{1.f, 1.f, 1.f};
    std::array<vec34_t, MAX_BONES> emissionBones;
    AnimationPoseSample pose(CharacterPresentationInput(character).object, b->BoneHead,
                             b->BodyHeight, false, b->PoseAssetIdentity());
    const auto sample = [&](float fraction) {
        ObjectDrawInput draw(o);
        o->MotionTrace.Sample(WorldTime, fraction, o->Position, draw.position);
        draw.bones = pose.EvaluateAtTime(*b, *o, WorldTime, fraction, emissionBones.data());
        return draw;
    };

    switch (c->MonsterIndex)
    {
    case MONSTER_GOLDEN_GREAT_DRAGON: {
        float fEffectScale = o->Scale * 1.6f;
        vec3_t v3EffectLightColor, v3EffectPosition;

        Vector(1.0f, 0.6f, 0.1f, v3EffectLightColor);

        b->TransformPosition(o->BoneTransform[0], p, v3EffectPosition, true);
        CreateSprite(BITMAP_LIGHTMARKS, v3EffectPosition, 2.5f, v3EffectLightColor, o);

        b->TransformPosition(o->BoneTransform[4], p, v3EffectPosition, true);
        CreateSprite(BITMAP_LIGHTMARKS, v3EffectPosition, 2.5f, v3EffectLightColor, o);

        b->TransformPosition(o->BoneTransform[57], p, v3EffectPosition, true);
        CreateSprite(BITMAP_LIGHTMARKS, v3EffectPosition, 2.5f, v3EffectLightColor, o);

        b->TransformPosition(o->BoneTransform[60], p, v3EffectPosition, true);
        CreateSprite(BITMAP_LIGHTMARKS, v3EffectPosition, 2.5f, v3EffectLightColor, o);

        b->TransformPosition(o->BoneTransform[87], p, v3EffectPosition, true);
        CreateSprite(BITMAP_LIGHTMARKS, v3EffectPosition, 2.5f, v3EffectLightColor, o);

        Vector(1.0f, 0.8f, 0.1f, v3EffectLightColor);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            auto draw = sample(birth.FrameFraction());
            b->TransformByObjectBone(v3EffectPosition, draw, 57, p);
            CreateEffect(MODEL_EFFECT_FIRE_HIK3_MONO, v3EffectPosition, o->Angle,
                         v3EffectLightColor, 1, NULL, -1, 0, 0, 0, fEffectScale);

            b->TransformByObjectBone(v3EffectPosition, draw, 60, p);
            CreateEffect(MODEL_EFFECT_FIRE_HIK3_MONO, v3EffectPosition, o->Angle,
                         v3EffectLightColor, 1, NULL, -1, 0, 0, 0, fEffectScale);

            b->TransformByObjectBone(v3EffectPosition, draw, 66, p);
            CreateEffect(MODEL_EFFECT_FIRE_HIK3_MONO, v3EffectPosition, o->Angle,
                         v3EffectLightColor, 1, NULL, -1, 0, 0, 0, fEffectScale);

            b->TransformByObjectBone(v3EffectPosition, draw, 78, p);
            CreateEffect(MODEL_EFFECT_FIRE_HIK3_MONO, v3EffectPosition, o->Angle,
                         v3EffectLightColor, 1, NULL, -1, 0, 0, 0, fEffectScale);

            b->TransformByObjectBone(v3EffectPosition, draw, 91, p);
            CreateEffect(MODEL_EFFECT_FIRE_HIK3_MONO, v3EffectPosition, o->Angle,
                         v3EffectLightColor, 1, NULL, -1, 0, 0, 0, fEffectScale);
        }
        break;
    }
    case MONSTER_ALQUAMOS: {

        float Luminosity = (float)(WorldRandom() % 30 + 70) * 0.01f;
        Vector(Luminosity * 0.8f, Luminosity * 0.9f, Luminosity * 1.f, Light);

        for (int i = 0; i < 9; ++i)
        {
            b->TransformPosition(o->BoneTransform[g_chStar[i]], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 0.6f, Light, o);
        }

        Vector(Luminosity * 0.6f, Luminosity * 0.7f, Luminosity * 0.8f, Light);

        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            auto draw = sample(birth.FrameFraction());
            for (int i = 0; i < 3; i++)
            {
                Vector((float)(WorldRandom() % 20 - 10), (float)(WorldRandom() % 20 - 10),
                       (float)(WorldRandom() % 20 - 10), p);
                b->TransformByObjectBone(Position, draw, WorldRandom() % b->NumBones, p);
                CreateParticle(BITMAP_SPARK + 1, Position, o->Angle, Light, 3);
            }
        }
        break;
    }
    case MONSTER_DRAKAN:
    case MONSTER_GREAT_DRAKAN: {
        vec3_t pos1, pos2;
        switch (c->MonsterIndex)
        {
        case MONSTER_DRAKAN:
            Vector(0.1f, 0.1f, 1.f, Light);

            for (int i = 13; i < 27; ++i)
            {
                b->TransformPosition(o->BoneTransform[i], p, Position, true);
                CreateSprite(BITMAP_LIGHT, Position, 0.8f, Light, o);

                VectorCopy(Position, pos2);
                VectorCopy(Position, pos1);
            }

            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                auto draw = sample(birth.FrameFraction());
                for (const int end : {14, 15, 16, 23})
                {
                    b->TransformByObjectBone(pos1, draw, end - 1);
                    b->TransformByObjectBone(pos2, draw, end);
                    CreateJoint(BITMAP_JOINT_THUNDER, pos1, pos2, o->Angle, 7, nullptr, 20.f);
                }
            }

            for (int i = 52; i < 59; ++i)
            {
                b->TransformPosition(o->BoneTransform[i], p, Position, true);
                CreateSprite(BITMAP_LIGHT, Position, 0.8f, Light, o);
            }
            break;
        case MONSTER_GREAT_DRAKAN:
            Vector(1.f, 1.f, 1.0f, Light);

            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                auto draw = sample(birth.FrameFraction());
                for (int i = 18; i < 19; ++i)
                {
                    Vector(0.f, 0.f, 0.f, p);
                    b->TransformByObjectBone(Position, draw, i, p);
                    CreateParticle(BITMAP_FIRE, Position, o->Angle, Light, 0, 0.3f);
                }
            }
            break;
        }

        break;
    }
    }
}

void SessionVisualUnit::EmitCursedSantaBurst(OBJECT &object, vec3_t position, vec3_t angle)
{
    auto *o = &object;

    vec3_t vPos, Position;
    Vector(position[0], position[1], position[2] + 250, vPos);

    vec3_t vLight;
    Vector(0.7f, 0.3f, 0.0f, vLight);
    for (int i = 0; i < 30; i++)
    {
        CreateParticle(BITMAP_SPARK + 1, vPos, angle, vLight, 27);
    }

    for (int i = 0; i < 20; i++)
    {
        CreateParticle(BITMAP_SPARK + 1, vPos, angle, vLight, 28);
    }

    Vector(vPos[0] + (WorldRandom() % 100 - 50), vPos[1] + (WorldRandom() % 100 - 50), vPos[2],
           Position);
    CreateSprite(BITMAP_DS_SHOCK, Position, WorldRandom() % 10 * 0.1f + 1.5f, o->Light, o);

    for (int i = 0; i < 60; i++)
    {
        Vector(0.3f + (WorldRandom() % 700) * 0.001f, 0.3f + (WorldRandom() % 700) * 0.001f,
               0.3f + (WorldRandom() % 700) * 0.001f, vLight);
        CreateParticle(BITMAP_SHINY, vPos, angle, vLight, 9);
    }

    vPos[2] += 50;

    for (int i = 0; i < 3; ++i)
    {
        CreateEffect(MODEL_HALLOWEEN_CANDY_STAR, vPos, angle, vLight, 1);
        CreateEffect(WorldRandom() % 4 + MODEL_XMAS_EVENT_BOX, vPos, angle, vLight, 0, o);
        CreateEffect(WorldRandom() % 4 + MODEL_XMAS_EVENT_BOX, vPos, angle, vLight, 0, o);
    }
}

void SessionVisualUnit::EmitCursedSantaOpening(vec3_t position)
{

    vec3_t Angle = {0.0f, 0.0f, 0.0f};
    vec3_t Position;
    for (int i = 0; i < 36; ++i)
    {
        Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);
        Position[0] = position[0] + WorldRandom() % 200 - 100;
        Position[1] = position[1] + WorldRandom() % 200 - 100;
        Position[2] = position[2];

        CreateJoint(BITMAP_FLARE, Position, Position, Angle, 2, NULL, 40);
    }
}

void SessionVisualUnit::AdvanceCursedSantaVisual(CHARACTER &character,
                                                 WorldCharacterVisualState &visual)
{
    auto &object = character.Object;
    if (object.CurrentAction != MONSTER01_DIE)
    {
        visual.santa.reset();
        return;
    }
    if (!visual.santa)
    {
        visual.santa = std::make_unique<CharacterSantaVisual>();
        VectorCopy(object.Position, visual.santa->origin);
        VectorCopy(object.Position, visual.santa->position);
        visual.santa->angle = object.Angle[2];
    }
    auto &state = *visual.santa;
    if (state.openingBurst && state.remainingBursts == 0)
    {
        state.position[2] = object.Position[2];
        return;
    }
    constexpr float openingFrame = 3.f, transitionFrame = 9.f, burstFrame = 13.f;
    const float remaining = -float(state.elapsedFrames);
    for (float sample = Core::Time::ReferenceSample(remaining);
         Core::Time::Reaches(remaining, FPS_ANIMATION_FACTOR, sample); sample -= 1.f)
    {
        const float offset = (std::max)(0.f, remaining - sample);
        const float fraction = offset / FPS_ANIMATION_FACTOR;
        const auto phase =
            object.MotionTrace.SampleAnimation(WorldTime, fraction,
                                               {object.AnimationFrame, object.PriorAnimationFrame,
                                                object.CurrentAction, object.PriorAction});
        if (phase.action != MONSTER01_DIE)
            continue;
        auto birthTime = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR - offset);
        vec3_t angle, position;
        VectorCopy(object.Angle, angle);
        object.MotionTrace.Sample(WorldTime, fraction, object.Position, position);
        state.position[2] = position[2];
        if (!state.openingBurst && phase.frame >= openingFrame)
        {
            state.openingBurst = true;
            EmitCursedSantaOpening(state.position);
        }
        if (phase.frame >= burstFrame)
        {
            state.angle = angle[2] = 45.f;
            if (state.remainingBursts > 0)
            {
                --state.remainingBursts;
                EmitCursedSantaBurst(object, state.position, angle);
                if (state.remainingBursts == 0)
                    break;
            }
            continue;
        }
        const bool transitioning = phase.frame >= transitionFrame;
        if (transitioning)
            state.angle += 40.f;
        const int range = transitioning ? 20 : 40;
        state.rightSide = !state.rightSide;
        const float side = state.rightSide ? 0.1f : -0.1f;
        state.position[0] = state.origin[0] + (WorldRandom() % range + range) * side;
        state.position[1] = state.origin[1] + (WorldRandom() % range + range) * side;
    }
    state.elapsedFrames += FPS_ANIMATION_FACTOR;
    state.position[2] = object.Position[2];
}

void SessionVisualUnit::AdvanceTitusVisual(CHARACTER &character)
{
    auto *o = &character.Object;
    auto &model = Models[o->Type];
    constexpr int bones[]{9, 45, 23, 11};
    vec3_t fireLight{1.f, 0.2f, 0.f}, light{1.f, 1.f, 1.f};
    for (int index = 0; index < 4; ++index)
    {
        vec3_t position;
        model.TransformByObjectBone(position, o, bones[index]);
        CreateSprite(BITMAP_LIGHT, position, index == 0 ? 3.f : 2.f, fireLight, o);
    }
    for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
    {
        const float fraction = birthTime.FrameFraction();
        AnimationPoseSample pose(o, model.BoneHead, model.BodyHeight, false,
                                 model.PoseAssetIdentity());
        std::array<vec34_t, MAX_BONES> sampledBones;
        pose.EvaluateAtTime(model, *o, WorldTime, fraction, sampledBones.data());
        vec3_t origin;
        o->MotionTrace.Sample(WorldTime, fraction, o->Position, origin);
        for (int index = 0; index < 4; ++index)
        {
            vec3_t relative, position;
            Vector(float(WorldRandom() % 10 - 5), float(WorldRandom() % 10 - 5),
                   float(WorldRandom() % 10 - 5), relative);
            VectorTransform(relative, sampledBones[bones[index]], position);
            VectorScale(position, o->Scale, position);
            VectorAdd(position, origin, position);
            const float scale = index == 0 ? 0.8f : 0.6f;
            switch (WorldRandom() % 3)
            {
            case 0:
                CreateParticle(BITMAP_FIRE_HIK1, position, o->Angle, light, 3, scale);
                break;
            case 1:
                CreateParticle(BITMAP_FIRE_CURSEDLICH, position, o->Angle, light, 7, scale);
                break;
            case 2:
                CreateParticle(BITMAP_FIRE_HIK3, position, o->Angle, light, 3, scale);
                break;
            }
        }
    }
}

void SessionVisualUnit::AdvanceDoppelgangerBoxVisual(CHARACTER &character)
{
    auto *o = &character.Object;
    auto *b = &Models[o->Type];
    if (o->CurrentAction == MONSTER01_DIE && o->AnimationFrame > 1 && o->AnimationFrame < 9)
    {
        vec3_t vPos, vLight;
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            vec3_t origin;
            o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, origin);
            for (int i = 0; i < 1; ++i)
            {
                vPos[0] = origin[0] + WorldRandom() % 120 - 60;
                vPos[1] = origin[1] + WorldRandom() % 120 - 60;
                vPos[2] = origin[2];
                Vector(0.3f, 0.3f, 0.3f, vLight);
                CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, 54, 2.0f);
                Vector(0.3f, 0.3f, 0.3f, vLight);
                CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, 66, 2.0f, o);
            }

            if (WorldRandom() % 2 == 0)
            {
                vPos[0] = origin[0] + WorldRandom() % 80 - 40;
                vPos[1] = origin[1] + WorldRandom() % 80 - 40;
                vPos[2] = origin[2] + WorldRandom() % 50 + 10;
                Vector(0.2f, 0.5f, 1.0f, vLight);
                CreateParticle(BITMAP_PIN_LIGHT, vPos, o->Angle, vLight, 2, 1.0f, o);
                vPos[2] = origin[2] + WorldRandom() % 50 + 10;
                Vector(1.0f, 0.8f, 0.5f, vLight);
                CreateParticle(BITMAP_PIN_LIGHT, vPos, o->Angle, vLight, 3, 0.2f, o);
            }

            Vector(0.9f, 0.7f, 0.0f, vLight);
            CreateParticle(BITMAP_SHINY, vPos, o->Angle, vLight, 3, 0.5f, o);
            CreateParticle(BITMAP_SHINY, vPos, o->Angle, vLight, 3, 0.5f, o);
        }

        vPos[0] = o->Position[0];
        vPos[1] = o->Position[1];
        if (o->Type == MODEL_DOPPELGANGER_NPC_GOLDENBOX)
            vPos[2] = o->Position[2] + 80;
        else
            vPos[2] = o->Position[2] + 50;
        Vector(1.0f, 1.0f, 1.0f, vLight);
        float fRot = (WorldTime * 0.0006f) * 360.0f;
        CreateSprite(BITMAP_DS_EFFECT, vPos, 2.5f, vLight, o, fRot);
        Vector(0.9f, 0.7f, 0.0f, vLight);
        CreateSprite(BITMAP_SHINY + 5, vPos, 0.8f, vLight, o, -fRot);
        float fLight = (sinf(WorldTime * 0.01f) + 1.0f) * 0.5f * 0.9f + 0.1f;
        Vector(1.0f * fLight, 1.0f * fLight, 1.0f * fLight, vLight);
        CreateSprite(BITMAP_FLARE, vPos, 1.5f, vLight, o);
    }
}

void SessionVisualUnit::AdvanceJuliaVisual(CHARACTER &character)
{
    auto *o = &character.Object;
    auto *b = &Models[o->Type];
    vec3_t vRelativePos{}, vWorldPos{}, Light{};
    float fScale = 0.f;
    for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(
             o->CurrentAction == 0 ? FPS_ANIMATION_FACTOR / 5.f : 0.f))
    {
        Vector(0.f, 0.f, 0.f, vWorldPos);
        Vector((WorldRandom() % 90 + 10) * 0.01f, (WorldRandom() % 90 + 10) * 0.01f,
               (WorldRandom() % 90 + 10) * 0.01f, Light);
        fScale = (WorldRandom() % 5 + 5) * 0.1f;
        Vector(0.f, 0.f, 0.f, vRelativePos);
        vRelativePos[0] = -5 + (WorldRandom() % 1000 - 500) * 0.01f;
        vRelativePos[1] = (WorldRandom() % 300 - 150) * 0.01f;
        const float fraction = birthTime.FrameFraction();
        AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false, b->PoseAssetIdentity());
        pose.SampleBonePosition(*b, *o, 127, vRelativePos, WorldTime, fraction, vWorldPos);
        CreateParticle(BITMAP_LIGHT, vWorldPos, o->Angle, Light, 13, fScale, o);
        CreateParticle(BITMAP_LIGHT, vWorldPos, o->Angle, Light, 12, fScale * 0.1f, o);
    }
}

void SessionVisualUnit::AdvanceSantaNpcVisual(CHARACTER &character,
                                              const WorldCharacterVisualState &visual)
{
    auto *o = &character.Object;
    auto *b = &Models[o->Type];
    vec3_t vRelativePos{}, vWorldPos{}, Light{};
    float fScale = 0.0f;
    AnimationPoseSample pose(CharacterPresentationInput(character).object, b->BoneHead,
                             b->BodyHeight, false, b->PoseAssetIdentity());
    switch (o->CurrentAction)
    {
    case 1:
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            vRelativePos[0] = 20 + (WorldRandom() % 500 - 250) * 0.1f;
            vRelativePos[1] = (WorldRandom() % 300 - 150) * 0.1f;
            pose.SampleBonePosition(*b, *o, 16, vRelativePos, WorldTime, birth.FrameFraction(),
                                    vWorldPos);

            Vector((WorldRandom() % 90 + 10) * 0.01f, (WorldRandom() % 90 + 10) * 0.01f,
                   (WorldRandom() % 90 + 10) * 0.01f, Light);
            fScale = (WorldRandom() % 5 + 5) * 0.1f;
            CreateParticle(BITMAP_LIGHT, vWorldPos, o->Angle, Light, 13, fScale, o);
            CreateParticle(BITMAP_LIGHT, vWorldPos, o->Angle, Light, 13, fScale, o);
            CreateParticle(BITMAP_LIGHT, vWorldPos, o->Angle, Light, 13, fScale, o);
            CreateParticle(BITMAP_SHINY, vWorldPos, o->Angle, Light, 7);
        }

        Vector(1.0f, 0.8f, 0.2f, Light);
        Vector(0.f, 0.f, 0.f, vRelativePos);
        b->TransformPosition(o->BoneTransform[16], vRelativePos, vWorldPos, true);
        CreateSprite(BITMAP_LIGHT, vWorldPos, 2.5f, Light, o);
        break;
    case 2:
        Vector(150.0f, 0.0f, 0.0f, vRelativePos);
        b->TransformPosition(o->BoneTransform[6], vRelativePos, vWorldPos, true);
        Vector(0.8f, 0.8f, 0.9f, Light);

        if (visual.animationFrame > 1.f && visual.intervalStartFrame <= 1.f)
        {
            CreateSprite(BITMAP_LIGHT, vWorldPos, 0.5f, Light, o);
            CreateSprite(BITMAP_DS_SHOCK, vWorldPos, 0.15f, Light, o);
            const float fraction =
                o->MotionTrace.FirstAnimationCrossing(WorldTime, visual.action, 1.f).value_or(1.f);
            auto birth =
                sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR * (1.f - fraction));
            pose.SampleBonePosition(*b, *o, 6, vRelativePos, WorldTime, fraction, vWorldPos);
            CreateEffect(BITMAP_FIRECRACKER0002, vWorldPos, o->Angle, Light, o->Skill);
        }
        break;
    }
}

void SessionVisualUnit::AdvanceNpcCharacterVisual(CHARACTER &character,
                                                  WorldCharacterVisualState &visual)
{
    auto *c = &character;
    auto *o = &character.Object;
    auto *b = &Models[o->Type];
    vec3_t p{}, Position{}, Light{1.f, 1.f, 1.f};
    float Luminosity = 1.f;
    AnimationPoseSample pose(CharacterPresentationInput(character).object, b->BoneHead,
                             b->BodyHeight, false, b->PoseAssetIdentity());
    switch (o->Type)
    {
    case MODEL_CASTLE_GATE:
    case MODEL_STATUE_OF_SAINT:
        if (character.WorldVisualStructureDeath ||
            (o->Type == MODEL_STATUE_OF_SAINT && WorldTime - o->InitialSceneTime < 1000))
        {
            const auto emit = [&](float fraction) {
                PrepareWorldObjectPose(*o, fraction);
                OBB_t bounds{};
                b->Transform(BoneTransform, o->BoundingBoxMin, o->BoundingBoxMax, &bounds, false);
                PlayBuffer(SOUND_HIT_GATE2);
                EmitMeshEffects(*b, 0,
                                o->Type == MODEL_CASTLE_GATE ? MODEL_GATE : MODEL_STONE_COFFIN);
            };
            if (character.WorldVisualStructureDeath)
                emit(1.f);
            else
                for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
                    emit(birthTime.FrameFraction());
        }
        break;
    case MODEL_DOPPELGANGER_NPC_BOX:
    case MODEL_DOPPELGANGER_NPC_GOLDENBOX:
        AdvanceDoppelgangerBoxVisual(character);
        break;
    case MODEL_UNITEDMARKETPLACE_JULIA:
        AdvanceJuliaVisual(character);
        break;
    case MODEL_DUEL_NPC_TITUS:
        AdvanceTitusVisual(character);
        break;
    case MODEL_GAMBLE_NPC_MOSS:
#ifdef ASG_ADD_TIME_LIMIT_QUEST_NPC
    case MODEL_TIME_LIMIT_QUEST_NPC_ZAIRO:
#endif
        Vector(0.8f, 0.8f, 0.8f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            pose.SampleBonePosition(*b, *o, 41, p, WorldTime, birth.FrameFraction(), Position);
            CreateParticle(BITMAP_SMOKELINE1 + WorldRandom() % 3, Position, o->Angle, Light, 1,
                           0.6f, o);
            CreateParticle(BITMAP_CLUD64, Position, o->Angle, Light, 6, 0.6f, o);
        }
        break;
    case MODEL_LITTLESANTA:
    case MODEL_LITTLESANTA + 1:
    case MODEL_LITTLESANTA + 2:
    case MODEL_LITTLESANTA + 3:
    case MODEL_LITTLESANTA + 4:
    case MODEL_LITTLESANTA + 5:
    case MODEL_LITTLESANTA + 6:
    case MODEL_LITTLESANTA + 7:
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR * 6.f / 50.f))
        {
            LittleSantaLight(o->Type, Light);
            o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, Position);
            CreateParticle(BITMAP_LIGHT + 3, Position, o->Angle, Light, 0, 1.f);
        }
        break;
    case MODEL_CURSED_SANTA:
        AdvanceCursedSantaVisual(character, visual);
        break;
    case MODEL_XMAS2008_SANTA_NPC:
        AdvanceSantaNpcVisual(character, visual);
        break;
    case MODEL_XMAS2008_SNOWMAN:
    case MODEL_XMAS2008_SNOWMAN_NPC: {
        if (o->Type == MODEL_XMAS2008_SNOWMAN && o->CurrentAction == MONSTER01_DIE)
        {
            if (character.WorldVisualSnowmanDeath)
            {
                b->TransformPosition(o->BoneTransform[7], p, Position, true);
                CreateEffect(MODEL_XMAS2008_SNOWMAN_HEAD, Position, o->Angle, Light, 0, o, 0, 0);
                CreateEffect(MODEL_XMAS2008_SNOWMAN_BODY, o->Position, o->Angle, Light, 0, o, 0, 0);
            }
            break;
        }
        Vector(0.8f, 0.8f, 0.9f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            pose.SampleBonePosition(*b, *o, 34, p, WorldTime, birth.FrameFraction(), Position);
            CreateParticle(BITMAP_SHINY, Position, o->Angle, Light, 7);
        }
        if (o->Type == MODEL_XMAS2008_SNOWMAN && o->CurrentAction == MONSTER01_WALK)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                vec3_t origin;
                o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, origin);
                Position[0] = origin[0] + sinf((WorldRandom() % 360) * 6.12f) * 40.f;
                Position[1] = origin[1] + sinf((WorldRandom() % 360) * 6.12f) * 40.f;
                Position[2] = origin[2];
                CreateParticle(BITMAP_SMOKE, Position, o->Angle, o->Light);
            }
        }
        break;
    }
    case MODEL_KALIMA_SHOP: {
        vec3_t angle{}, light{1.f, 1.f, 1.f};
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            for (const float side : {-1.f, 1.f})
            {
                vec3_t position;
                o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, position);
                position[0] += side * 40.f;
                position[1] += side * 30.f;
                position[2] -= 90.f;
                CreateParticle(BITMAP_SMOKE, position, angle, light, 11, 0.4f);
            }
        break;
    }
    case MODEL_NPC_DEVILSQUARE: {
        Luminosity = (float)sinf((WorldTime) * 0.002f) * 0.35f + 0.65f;

        float Scale = 0.3f;
        Vector(Luminosity, Luminosity, Luminosity, Light);

        Vector(3.5f, -12.f, 10.f, p);
        b->TransformPosition(o->BoneTransform[20], p, Position, 1);
        CreateSprite(BITMAP_LIGHTNING + 1, Position, Scale, Light, o, (WorldTime / 50.0f));
        CreateSprite(BITMAP_LIGHTNING + 1, Position, Scale, Light, o, ((-WorldTime) / 50.0f));

        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 30.f))
        {
            vec3_t offset{3.5f, -12.f, 10.f};
            pose.SampleBonePosition(*b, *o, 20, offset, WorldTime, birth.FrameFraction(), Position);
            p[0] = Position[0] + WorldRandom() % 100 - 50;
            p[1] = Position[1] + WorldRandom() % 100 - 50;
            p[2] = Position[2] + WorldRandom() % 100 - 50;

            CreateJoint(BITMAP_JOINT_ENERGY, p, Position, o->Angle, 6, NULL, 20.f);
        }
    }
    break;
    case MODEL_NPC_CASTEL_GATE: {
        vec3_t vPos, vRelative;
        float fLumi, fScale;

        fLumi = (sinf(WorldTime * 0.002f) + 2.0f) * 0.5f;
        Vector(fLumi * 1.0f, fLumi * 0.5f, fLumi * 0.3f, Light);
        fScale = fLumi / 2.0f;

        Vector(4.0f, 3.0f, -4.0f, vRelative);
        b->TransformPosition(o->BoneTransform[2], vRelative, vPos, true);

        CreateSprite(BITMAP_LIGHT, vPos, fScale, Light, o);
        CreateSprite(BITMAP_KANTURU_2ND_EFFECT1, vPos, fScale, Light, o);

        Vector(5.0f, 3.0f, 2.0f, vRelative);
        b->TransformPosition(o->BoneTransform[4], vRelative, vPos, true);

        CreateSprite(BITMAP_LIGHT, vPos, fScale, Light, o);
        CreateSprite(BITMAP_KANTURU_2ND_EFFECT1, vPos, fScale, Light, o);

        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.f))
        {
            Vector(-20.0f, 10.0f, 0.0f, vRelative);
            pose.SampleBonePosition(*b, *o, 3, vRelative, WorldTime, birth.FrameFraction(), vPos);

            vec3_t Angle;
            VectorCopy(o->Angle, Angle);
            Angle[0] += 120.0f;
            Angle[2] -= 60.0f;

            Vector(1.0f, 1.0f, 1.0f, Light);

            CreateParticle(BITMAP_FLAME, vPos, Angle, Light, 10, o->Scale / 2);
        }
    }
    break;
    case MODEL_SHADOW:
        Vector(0.f, 0.f, 0.f, p);
        Luminosity = 1.f;
        if (c->Level == 0)
        {
            Vector(Luminosity * 1.f, Luminosity * 1.f, Luminosity * 1.f, Light);
        }
        else
        {
            Vector(Luminosity * 1.f, Luminosity * 0.4f, Luminosity * 0.f, Light);
            Vector(Luminosity * 0.2f, Luminosity * 0.7f, Luminosity * 0.1f, Light);
        }

        for (int i = 0; i < b->NumBones; i++)
        {
            if (!b->Bones[i].Dummy)
            {
                if ((i >= 15 && i <= 20) || (i >= 27 && i <= 32))
                {
                }
                else
                {
                    b->TransformPosition(o->BoneTransform[i], p, Position, true);
                    if (c->Level == 0)
                        CreateSprite(BITMAP_SHINY + 1, Position, 2.5f, Light, o, 0.f, 1);
                    else
                        CreateSprite(BITMAP_MAGIC + 1, Position, 0.8f, Light, o, 0.f);
                    //CreateParticle(BITMAP_SMOKE,Position,o->Angle,Light,1);
                }
            }
        }
        if (o->CurrentAction >= MONSTER01_ATTACK1 && o->CurrentAction <= MONSTER01_ATTACK2)
        {
            std::array<vec34_t, MAX_BONES> sampledBones;
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                ObjectDrawInput draw(o);
                o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, draw.position);
                draw.bones = pose.EvaluateAtTime(*b, *o, WorldTime, birth.FrameFraction(),
                                                 sampledBones.data());
                for (int bone = 0; bone < b->NumBones; ++bone)
                {
                    if (b->Bones[bone].Dummy || (bone >= 15 && bone <= 20) ||
                        (bone >= 27 && bone <= 32))
                        continue;
                    if (WorldRandom() % 4 != 0)
                        continue;
                    b->TransformByObjectBone(Position, draw, bone, p);
                    CreateParticle(BITMAP_ENERGY, Position, o->Angle, Light);
                }
            }
        }
        break;
    }
}

void SessionVisualUnit::AdvanceCherryBlossomVisual(CHARACTER &character)
{
    auto &object = character.Object;
    if (object.Type != MODEL_NPC_CHERRYBLOSSOM)
        return;
    BMD &model = Models[object.Type];
    const auto presentation = CharacterPresentationInput(character).object;
    AnimationPoseSample pose(presentation, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    vec3_t position, pink{1.f, 0.6f, 0.8f}, grey{0.3f, 0.3f, 0.3f}, white{1.f, 1.f, 1.f};
    const auto sample = [&](int bone, float fraction, vec3_t output) {
        vec3_t zero{};
        if (fraction == 1.f)
            model.TransformByObjectBone(output, presentation, bone, zero);
        else
            pose.SampleBonePosition(model, object, bone, zero, WorldTime, fraction, output);
        if (bone == 43)
            output[2] += 20.f;
    };
    const auto scaleAt = [&](const EffectEmissionScope &birth) {
        const double time = WorldTime - birth.SceneRemainingFrames() * 1000.0 /
                                            sessionKeeper_.ApplicationConfig().legacyReferenceFps;
        return std::abs(std::sin(float(time * 0.002))) * 0.2f;
    };
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
    {
        sample(43, birth.FrameFraction(), position);
        CreateParticle(BITMAP_CHERRYBLOSSOM_EVENT_PETAL, position, object.Angle,
                       WorldRandom() % 3 == 0 ? pink : grey, 1, 0.3f);
    }
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
    {
        sample(43, birth.FrameFraction(), position);
        const float scale = scaleAt(birth);
        CreateParticle(BITMAP_SPARK + 1, position, object.Angle,
                       WorldRandom() % 3 == 0 ? white : grey, 25, scale + 0.2f);
        CreateParticle(BITMAP_SPARK + 1, position, object.Angle,
                       WorldRandom() % 2 == 0 ? white : grey, 25, scale + 0.3f);
    }
    vec3_t light{0.7f, 0.5f, 0.2f};
    sample(43, 1.f, position);
    CreateSprite(BITMAP_LIGHT, position, 2.f, light, &object);
    Vector(0.7f, 0.2f, 0.6f, light);
    sample(3, 1.f, position);
    CreateSprite(BITMAP_LIGHT, position, 5.f, light, &object);
    for (const int bone : {53, 56, 59, 62})
    {
        sample(bone, 1.f, position);
        Vector(0.9f, 0.4f, 0.8f, light);
        CreateSprite(BITMAP_LIGHT, position, 1.5f, light, &object);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            sample(bone, birth.FrameFraction(), position);
            const float offset = float(WorldRandom() % 30 + 5);
            position[0] += WorldRandom() % 2 == 0 ? offset : -offset;
            Vector(1.f, 0.8f, 0.4f, light);
            CreateParticle(BITMAP_SPARK + 1, position, object.Angle, light, 15,
                           scaleAt(birth) + 0.4f);
        }
    }
}

void SessionVisualUnit::AdvanceEquipmentSetVisual(CHARACTER &character,
                                                  WorldCharacterVisualState &visual)
{
    if (character.HideShadow || (character.MonsterIndex >= MONSTER_TERRIBLE_BUTCHER &&
                                 character.MonsterIndex <= MONSTER_DOPPELGANGER_SUM))
        return;
    auto *c = &character;
    auto *o = &character.Object;
    auto *b = &Models[o->Type];
    vec3_t p{}, Position{}, Light{}, effectLight;
    VectorCopy(o->Light, effectLight);
    if (gMapManager.InChaosCastle() == false)
    {
        if (visual.equipmentSet.complete && g_pOption->GetRenderLevel() >= 2)
        {
            PartObjectColor(c->BodyPart[5].Type, o->Alpha, 0.5f, Light);

            if (!g_isCharacterBuff(o, eBuff_Cloaking))
            {
                for (int i = 0; i < 2; i++)
                {
                    b->TransformPosition(o->BoneTransform[c->Weapon[i].LinkBone], p, Position,
                                         true);
                    CreateSprite(BITMAP_LIGHT, Position, 1.3f, Light, o);
                    b->TransformPosition(o->BoneTransform[c->Weapon[i].LinkBone - 6], p, Position,
                                         true);
                    CreateSprite(BITMAP_LIGHT, Position, 1.3f, Light, o);
                    b->TransformPosition(o->BoneTransform[c->Weapon[i].LinkBone - 7], p, Position,
                                         true);
                    CreateSprite(BITMAP_LIGHT, Position, 1.3f, Light, o);
                }
            }
            if ((c->BodyPart[BODYPART_BOOTS].Type >= MODEL_DRAGON_KNIGHT_BOOTS &&
                 c->BodyPart[BODYPART_BOOTS].Type <= MODEL_SUNLIGHT_BOOTS) ||
                c->BodyPart[BODYPART_BOOTS].Type == MODEL_AURA_BOOTS)
            {
                if (visual.equipmentSet.level > 9)
                {
                    VectorCopy(effectLight, Light);

                    if (c->BodyPart[BODYPART_BOOTS].Type == MODEL_DRAGON_KNIGHT_BOOTS)
                        Vector(0.65f, 0.3f, 0.1f, effectLight);
                    if (c->BodyPart[BODYPART_BOOTS].Type == MODEL_VENOM_MIST_BOOTS)
                        Vector(0.1f, 0.1f, 0.9f, effectLight);
                    if (c->BodyPart[BODYPART_BOOTS].Type == MODEL_SYLPHID_RAY_BOOTS)
                        Vector(0.0f, 0.32f, 0.24f, effectLight);
                    if (c->BodyPart[BODYPART_BOOTS].Type == MODEL_VOLCANO_BOOTS)
                        Vector(0.5f, 0.24f, 0.8f, effectLight);
                    if (c->BodyPart[BODYPART_BOOTS].Type == MODEL_SUNLIGHT_BOOTS)
                        Vector(0.6f, 0.4f, 0.0f, effectLight);
                    if (c->BodyPart[BODYPART_BOOTS].Type == MODEL_AURA_BOOTS)
                        Vector(0.6f, 0.3f, 0.4f, effectLight);
                    const int framesBetweenBursts = (std::max)(1, 14 - visual.equipmentSet.level);
                    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR /
                                                                           framesBetweenBursts))
                    {
                        AnimationPoseSample pose(CharacterPresentationInput(character).object,
                                                 b->BoneHead, b->BodyHeight, false,
                                                 b->PoseAssetIdentity());
                        const float spread = visual.equipmentSet.level <= 12 ? 18.f : 20.f;
                        vec3_t offsets[]{
                            {0.f, -spread, 50.f}, {0.f, 0.f, 70.f}, {0.f, spread, 50.f}};
                        for (auto &offset : offsets)
                        {
                            pose.SampleBonePosition(*b, *o, 0, offset, WorldTime,
                                                    birth.FrameFraction(), Position);
                            CreateParticle(BITMAP_WATERFALL_2, Position, o->Angle, effectLight, 3);
                        }
                    }

                    VectorCopy(Light, effectLight);
                }
            }

            if (visual.equipmentSet.level > 9)
            {
                for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 20.f))
                {
                    vec3_t origin, white{1.f, 1.f, 1.f};
                    o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, origin);
                    const int level = visual.equipmentSet.level;
                    if (level == 10)
                        CreateParticle(BITMAP_FLARE, origin, o->Angle, white, 0, 0.19f, o);
                    else if (level == 11)
                    {
                        if (WorldRandom() % 8 == 0)
                            CreateJoint(BITMAP_FLARE, origin, origin, o->Angle, 0, o);
                        else
                            CreateParticle(BITMAP_FLARE, origin, o->Angle, white, 0, 0.19f, o);
                    }
                    else if (level >= 12 && level <= 15)
                    {
                        const bool paired = WorldRandom() % 6 == 0;
                        if (paired)
                        {
                            CreateJoint(BITMAP_FLARE, origin, origin, o->Angle, 18, o, 20, -1, 0);
                            CreateJoint(BITMAP_FLARE, origin, origin, o->Angle, 18, o, 20, -1, 1);
                        }
                        if (level == 12)
                        {
                            if (!paired && WorldRandom() % 3 == 0)
                                CreateParticle(BITMAP_FLARE, origin, o->Angle, white, 0, 0.19f, o);
                        }
                        else if (WorldRandom() % 4 == 0)
                        {
                            CreateParticle(BITMAP_FLARE, origin, o->Angle, white, 0, 0.19f, o);
                            CreateJoint(BITMAP_FLARE + 1, origin, origin, o->Angle, 7, o, 20, 40,
                                        1);
                        }
                    }
                }

                if (visual.equipmentSet.level == 15 && g_pOption->GetRenderLevel() >= 4)
                    AdvanceEquipmentSetEnergy(character, *b);
            }
        }
    }
}

void SessionVisualUnit::AdvanceExtendedStateVisual(CHARACTER &character,
                                                   WorldCharacterVisualState &visual)
{
    if (gMapManager.InChaosCastle())
        return;
    auto *c = &character;
    auto *o = &character.Object;
    vec3_t Light;
    if (c->ExtendState)
    {
        if ((o->CurrentAction < PLAYER_WALK_MALE || o->CurrentAction == PLAYER_DARKLORD_STAND ||
             o->CurrentAction == PLAYER_STOP_RIDE_HORSE ||
             o->CurrentAction == PLAYER_STOP_TWO_HAND_SWORD_TWO) &&
            visual.extendedStateTicks >= 100)
        {
            Vector(0.2f, 0.7f, 0.9f, Light);
            CreateEffect(BITMAP_LIGHT, o->Position, o->Angle, Light, 3, o);

            visual.extendedStateTicks = 0;
        }
        visual.extendedStateTicks += FPS_ANIMATION_FACTOR;
    }
}

bool SessionVisualUnit::AdvancePlayerSpellVisual(CHARACTER &character,
                                                 WorldCharacterVisualState &visual)
{
    auto *c = &character;
    auto *o = &character.Object;
    auto *b = &Models[o->Type];
    vec3_t p{}, Position{}, Light{1.f, 1.f, 1.f};
    AnimationPoseSample emissionPose(CharacterPresentationInput(character).object, b->BoneHead,
                                     b->BodyHeight, false, b->PoseAssetIdentity());
    if (visual.action == PLAYER_SKILL_FLASH || visual.action == PLAYER_ATTACK_RIDE_ATTACK_FLASH ||
        visual.action == PLAYER_FENRIR_ATTACK_DARKLORD_FLASH)
    {
        if (gCharacterManager.GetBaseClass(c->Class) == CLASS_DARK_LORD ||
            visual.action == PLAYER_ATTACK_RIDE_ATTACK_FLASH ||
            visual.action == PLAYER_FENRIR_ATTACK_DARKLORD_FLASH)
        {
            if (visual.CrossesAnimationWindow(1.2f, 1.6f))
            {
                const float fraction =
                    o->MotionTrace.FirstAnimationCrossing(WorldTime, visual.action, 1.2f)
                        .value_or(1.f);
                auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR *
                                                                     (1.f - fraction));
                vec3_t Angle;
                Vector(1.f, 0.f, 0.f, Angle);
                o->MotionTrace.Sample(WorldTime, fraction, o->Position, Position);
                CreateEffect(BITMAP_GATHERING, Position, o->Angle, o->Light, 2, o);

                PlayBuffer(SOUND_ELEC_STRIKE_READY);
            }

            if (visual.animationFrame < 2.f)
            {
                if (PartyNumber > 0)
                {
                    if (g_pPartyManager->IsPartyMemberChar(c) == false)
                        return false;

                    for (int i = 0; i < PartyNumber; ++i)
                    {
                        PARTY_t *p = &Party[i];
                        if (p->index < 0)
                            continue;

                        CHARACTER *tc = &CharactersClient[p->index];
                        if (tc != c)
                        {
                            for (auto birth :
                                 sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
                            {
                                tc->Object.MotionTrace.Sample(WorldTime, birth.FrameFraction(),
                                                              tc->Object.Position, Position);
                                Position[2] += 150.f;
                                CreateJoint(BITMAP_JOINT_ENERGY, Position, Position, o->Angle, 12,
                                            o, 20.f);
                                CreateJoint(BITMAP_JOINT_ENERGY, Position, Position, o->Angle, 13,
                                            o, 20.f);
                            }
                        }
                    }
                }
            }

            if (visual.CrossesAnimationWindow(7.f, 8.f))
            {
                const float fraction =
                    o->MotionTrace.FirstAnimationCrossing(WorldTime, visual.action, 7.f)
                        .value_or(1.f);
                auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR *
                                                                     (1.f - fraction));
                vec3_t Angle, Light;

                emissionPose.SampleBonePosition(*b, *o, c->Weapon[0].LinkBone, p, WorldTime,
                                                fraction, Position);

                Vector(0.8f, 0.5f, 1.f, Light);
                Vector(180.f, 45.f, 0.f, Angle);
                CreateEffect(MODEL_DARKLORD_SKILL, Position, Angle, Light, 2);

                Vector(0.f, 0.f, o->Angle[2], Angle);
                CreateEffect(MODEL_DARKLORD_SKILL, Position, Angle, Light, 2);
            }
        }
        else
        {
            if (g_isCharacterBuff(o, eBuff_Cloaking))
            {
                b->TransformPosition(o->BoneTransform[c->Weapon[0].LinkBone], p, Position, true);
                Vector(0.1f, 0.1f, 1.f, Light);
                float Scale = visual.animationFrame * 0.1f;
                CreateSprite(BITMAP_LIGHTNING + 1, Position, Scale * 0.3f, Light, o);
                CreateSprite(BITMAP_LIGHTNING + 1, Position, Scale * 1.f, o->Light, o,
                             -(int)WorldTime * 0.1f);
                CreateSprite(BITMAP_LIGHTNING + 1, Position, Scale * 2.5f, o->Light, o,
                             (int)WorldTime * 0.1f);
            }
        }
    }
    else if (visual.action == PLAYER_SKILL_LIGHTNING_ORB ||
             visual.action == PLAYER_SKILL_LIGHTNING_ORB_UNI ||
             visual.action == PLAYER_SKILL_LIGHTNING_ORB_DINO ||
             visual.action == PLAYER_SKILL_LIGHTNING_ORB_FENRIR)
    {
        vec3_t vLight, vRelativePos, vWorldPos;
        Vector(1.0f, 1.0f, 1.0f, vLight);
        Vector(0.f, 0.f, 0.f, vRelativePos);
        Vector(0.f, 0.f, 0.f, vWorldPos);
        // 27 "Bip01 R Forearm"
        b->TransformPosition(o->BoneTransform[27], vRelativePos, vWorldPos, true);

        Vector(0.2f, 0.2f, 1.0f, vLight);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            emissionPose.SampleBonePosition(*b, *o, 27, vRelativePos, WorldTime,
                                            birth.FrameFraction(), vWorldPos);
            CreateEffect(MODEL_FENRIR_THUNDER, vWorldPos, o->Angle, vLight, 2, o);
            CreateEffect(MODEL_FENRIR_THUNDER, vWorldPos, o->Angle, vLight, 2, o);
        }
    }
    else if (visual.action >= PLAYER_SKILL_SLEEP && visual.action <= PLAYER_SKILL_SLEEP_FENRIR)
    {
        auto iSkillType = CharacterAttribute->Skill[Hero->CurrentSkill];

        vec3_t vLight, vRelativePos, vWorldPos;
        Vector(1.0f, 1.0f, 1.0f, vLight);
        Vector(0.f, 0.f, 0.f, vRelativePos);
        Vector(0.f, 0.f, 0.f, vWorldPos);
        b->TransformPosition(o->BoneTransform[37], vRelativePos, vWorldPos, true); // "Bip01 L Hand"

        float fRot = (WorldTime * 0.0006f) * 360.0f;

        // shiny
        if (iSkillType == AT_SKILL_ALICE_SLEEP || iSkillType == AT_SKILL_ALICE_SLEEP_STR)
        {
            Vector(0.5f, 0.2f, 0.8f, vLight);
        }
        else if (iSkillType == AT_SKILL_ALICE_BLIND)
        {
            Vector(1.0f, 1.0f, 1.0f, vLight);
        }
        else if (iSkillType == AT_SKILL_ALICE_THORNS)
        {
            Vector(0.8f, 0.5f, 0.2f, vLight);
        }
        else if (iSkillType == AT_SKILL_ALICE_BERSERKER ||
                 iSkillType == AT_SKILL_ALICE_BERSERKER_STR)
        {
            Vector(1.0f, 0.1f, 0.2f, vLight);
        }
        else if (iSkillType == AT_SKILL_ALICE_WEAKNESS)
        {
            Vector(0.8f, 0.1f, 0.1f, vLight);
        }
        else if (iSkillType == AT_SKILL_ALICE_ENERVATION)
        {
            Vector(0.25f, 1.0f, 0.7f, Light);
        }

        if (iSkillType == AT_SKILL_ALICE_SLEEP || iSkillType == AT_SKILL_ALICE_SLEEP_STR ||
            iSkillType == AT_SKILL_ALICE_THORNS || iSkillType == AT_SKILL_ALICE_BERSERKER ||
            iSkillType == AT_SKILL_ALICE_BERSERKER_STR || iSkillType == AT_SKILL_ALICE_WEAKNESS ||
            iSkillType == AT_SKILL_ALICE_ENERVATION)
        {
            CreateSprite(BITMAP_SHINY + 5, vWorldPos, 1.0f, vLight, o, fRot);
            CreateSprite(BITMAP_SHINY + 5, vWorldPos, 0.7f, vLight, o, -fRot);
        }
        else if (iSkillType == AT_SKILL_ALICE_BLIND)
        {
            CreateSprite(BITMAP_SHINY + 5, vWorldPos, 1.0f, vLight, o, fRot, 1);
            CreateSprite(BITMAP_SHINY + 5, vWorldPos, 0.7f, vLight, o, -fRot, 1);
        }

        // pin_light
        if (iSkillType == AT_SKILL_ALICE_SLEEP || iSkillType == AT_SKILL_ALICE_SLEEP_STR)
        {
            Vector(0.7f, 0.0f, 0.8f, vLight);
        }
        else if (iSkillType == AT_SKILL_ALICE_BLIND)
        {
            Vector(1.0f, 1.0f, 1.0f, vLight);
        }
        else if (iSkillType == AT_SKILL_ALICE_THORNS)
        {
            Vector(0.8f, 0.5f, 0.2f, vLight);
        }
        else if (iSkillType == AT_SKILL_ALICE_BERSERKER ||
                 iSkillType == AT_SKILL_ALICE_BERSERKER_STR)
        {
            Vector(1.0f, 0.1f, 0.2f, vLight);
        }
        else if (iSkillType == AT_SKILL_ALICE_THORNS)
        {
            Vector(0.8f, 0.1f, 0.1f, vLight);
        }
        else if (iSkillType == AT_SKILL_ALICE_ENERVATION)
        {
            Vector(0.25f, 1.f, 0.7f, vLight);
        }

        if (iSkillType == AT_SKILL_ALICE_SLEEP || iSkillType == AT_SKILL_ALICE_SLEEP_STR ||
            iSkillType == AT_SKILL_ALICE_THORNS || iSkillType == AT_SKILL_ALICE_BERSERKER ||
            iSkillType == AT_SKILL_ALICE_BERSERKER_STR || iSkillType == AT_SKILL_ALICE_WEAKNESS ||
            iSkillType == AT_SKILL_ALICE_ENERVATION)
        {
            CreateSprite(BITMAP_PIN_LIGHT, vWorldPos, 1.7f, vLight, o,
                         (float)(WorldRandom() % 360));
            CreateSprite(BITMAP_PIN_LIGHT, vWorldPos, 1.5f, vLight, o,
                         (float)(WorldRandom() % 360));
        }
        else if (iSkillType == AT_SKILL_ALICE_BLIND)
        {
            CreateSprite(BITMAP_PIN_LIGHT, vWorldPos, 1.7f, vLight, o, (float)(WorldRandom() % 360),
                         1);
            CreateSprite(BITMAP_PIN_LIGHT, vWorldPos, 1.5f, vLight, o, (float)(WorldRandom() % 360),
                         1);
        }

        // cra04, clud64
        if (iSkillType == AT_SKILL_ALICE_SLEEP || iSkillType == AT_SKILL_ALICE_SLEEP_STR)
        {
            Vector(0.6f, 0.1f, 0.8f, vLight);
        }
        else if (iSkillType == AT_SKILL_ALICE_BLIND)
        {
            Vector(1.0f, 1.0f, 1.0f, vLight);
        }
        else if (iSkillType == AT_SKILL_ALICE_THORNS)
        {
            Vector(0.8f, 0.5f, 0.2f, vLight);
        }
        else if (iSkillType == AT_SKILL_ALICE_BERSERKER ||
                 iSkillType == AT_SKILL_ALICE_BERSERKER_STR)
        {
            Vector(1.0f, 0.1f, 0.2f, vLight);
        }
        else if (iSkillType == AT_SKILL_ALICE_THORNS)
        {
            Vector(0.8f, 0.1f, 0.1f, vLight);
        }
        else if (iSkillType == AT_SKILL_ALICE_ENERVATION)
        {
            Vector(0.25f, 1.f, 0.7f, vLight);
        }
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            vec3_t birthPosition;
            emissionPose.SampleBonePosition(*b, *o, 37, vRelativePos, WorldTime,
                                            birth.FrameFraction(), birthPosition);
            if (iSkillType == AT_SKILL_ALICE_SLEEP || iSkillType == AT_SKILL_ALICE_SLEEP_STR ||
                iSkillType == AT_SKILL_ALICE_THORNS || iSkillType == AT_SKILL_ALICE_BERSERKER ||
                iSkillType == AT_SKILL_ALICE_BERSERKER_STR ||
                iSkillType == AT_SKILL_ALICE_WEAKNESS || iSkillType == AT_SKILL_ALICE_ENERVATION)
            {
                CreateParticle(BITMAP_LIGHT + 2, birthPosition, o->Angle, vLight, 0, 1.0f);
                CreateParticle(BITMAP_CLUD64, birthPosition, o->Angle, vLight, 3, 0.5f);
            }
            else if (iSkillType == AT_SKILL_ALICE_BLIND)
            {
                CreateParticle(BITMAP_LIGHT + 2, birthPosition, o->Angle, vLight, 4, 1.0f);
                CreateParticle(BITMAP_CLUD64, birthPosition, o->Angle, vLight, 5, 0.5f);
            }
        }
    }
    // ChainLighting
    else if (visual.action == PLAYER_SKILL_CHAIN_LIGHTNING)
    {
        vec3_t vRelativePos, vWorldPos, vLight;
        Vector(0.f, 0.f, 0.f, vRelativePos);
        Vector(0.4f, 0.4f, 0.8f, vLight);

        b->TransformPosition(o->BoneTransform[37], vRelativePos, vWorldPos, true); // "Bip01 L Hand"

        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            vec3_t birthPosition;
            emissionPose.SampleBonePosition(*b, *o, 37, vRelativePos, WorldTime,
                                            birth.FrameFraction(), birthPosition);
            CreateEffect(MODEL_FENRIR_THUNDER, birthPosition, o->Angle, vLight, 2, o);
            CreateEffect(MODEL_FENRIR_THUNDER, birthPosition, o->Angle, vLight, 2, o);
        }

        CreateSprite(BITMAP_LIGHT, vWorldPos, 1.5f, vLight, o, 0.f);

        b->TransformPosition(o->BoneTransform[28], vRelativePos, vWorldPos, true); // "Bip01 R Hand"
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            vec3_t birthPosition;
            emissionPose.SampleBonePosition(*b, *o, 28, vRelativePos, WorldTime,
                                            birth.FrameFraction(), birthPosition);
            CreateEffect(MODEL_FENRIR_THUNDER, birthPosition, o->Angle, vLight, 2, o);
            CreateEffect(MODEL_FENRIR_THUNDER, birthPosition, o->Angle, vLight, 2, o);
        }

        CreateSprite(BITMAP_LIGHT, vWorldPos, 1.5f, vLight, o, 0.f);
    }

    return true;
}

void SessionVisualUnit::AdvancePlayerBuffVisual(CHARACTER &character)
{
    auto *c = &character;
    auto *o = &character.Object;
    if (!(g_isCharacterBuff(o, eBuff_Attack) || g_isCharacterBuff(o, eBuff_HelpNpc)) ||
        g_isCharacterBuff(o, eBuff_Cloaking))
        return;
    auto *b = &Models[o->Type];
    vec3_t p{}, Position{}, Light{};
    float Luminosity;
    for (int i = 0; i < 2; i++)
    {
        Luminosity = (float)(WorldRandom() % 30 + 70) * 0.01f;
        Vector(Luminosity * 1.f, Luminosity * 0.3f, Luminosity * 0.2f, Light);
        b->TransformPosition(o->BoneTransform[c->Weapon[i].LinkBone], p, Position, true);
        CreateSprite(BITMAP_SHINY + 1, Position, 1.5f, Light, o);
        b->TransformPosition(o->BoneTransform[c->Weapon[i].LinkBone - 6], p, Position, true);
        CreateSprite(BITMAP_SHINY + 1, Position, 1.5f, Light, o);
        b->TransformPosition(o->BoneTransform[c->Weapon[i].LinkBone - 7], p, Position, true);
        CreateSprite(BITMAP_SHINY + 1, Position, 1.5f, Light, o);
    }
}

void SessionVisualUnit::ReconcileCharacterStunVisual(CHARACTER &character, bool removeAbsent)
{
    auto *object = &character.Object;
    const bool visibleStun = g_isCharacterBuff(object, eDeBuff_Stun) &&
                             !g_isCharacterBuff(object, eBuff_Cloaking) &&
                             !g_isCharacterBuff(object, eDeBuff_Poison) &&
                             !g_isCharacterBuff(object, eDeBuff_BlowOfDestruction);
    if (!visibleStun)
    {
        if (removeAbsent)
            DeleteEffect(BITMAP_SKULL, object, 5);
        return;
    }
    if (!SearchEffect(BITMAP_SKULL, object, 5))
        CreateEffect(BITMAP_SKULL, object->Position, object->Angle, object->Light, 5, object);
}

void SessionVisualUnit::ReconcilePlayerBuffVisual(CHARACTER &character, bool removeAbsent)
{
    auto *c = &character;
    auto *o = &character.Object;
    vec3_t Light{1.f, 1.f, 1.f};
    const bool protectiveJoint = g_isCharacterBuff(o, eBuff_Attack) ||
                                 g_isCharacterBuff(o, eBuff_HelpNpc) ||
                                 g_isCharacterBuff(o, eBuff_Defense);
    if (protectiveJoint && !g_isCharacterBuff(o, eBuff_Cloaking))
    {
        if (!SearchJoint(MODEL_SPEARSKILL, o, 4) && !SearchJoint(MODEL_SPEARSKILL, o, 9))
            for (int index = 0; index < 5; ++index)
                CreateJoint(MODEL_SPEARSKILL, o->Position, o->Position, o->Angle, 4, o, 20.f, -1, 0,
                            0, c->TargetCharacter);
    }
    else
    {
        if (removeAbsent)
            DeleteJoint(MODEL_SPEARSKILL, o, 4);
        if (removeAbsent)
            DeleteJoint(MODEL_SPEARSKILL, o, 9);
    }

    if (g_isCharacterBuff((&c->Object), eBuff_PcRoomSeal1) ||
        g_isCharacterBuff((&c->Object), eBuff_PcRoomSeal2) ||
        g_isCharacterBuff((&c->Object), eBuff_PcRoomSeal3) ||
        g_isCharacterBuff((&c->Object), eBuff_Seal1) ||
        g_isCharacterBuff((&c->Object), eBuff_Seal2) ||
        g_isCharacterBuff((&c->Object), eBuff_Seal3) ||
        g_isCharacterBuff((&c->Object), eBuff_Seal4))
    {
        if (SearchJoint(MODEL_SPEARSKILL, o, 10) == false &&
            SearchJoint(MODEL_SPEARSKILL, o, 11) == false)
        {
            for (int i = 0; i < 3; ++i)
            {
                CreateJoint(MODEL_SPEARSKILL, o->Position, o->Position, o->Angle, 10, o, 12.0f, -1,
                            0, 0, c->Key);
            }
        }
    }
    else
    {
        if (removeAbsent)
            DeleteJoint(MODEL_SPEARSKILL, o, 10);
        if (removeAbsent)
            DeleteJoint(MODEL_SPEARSKILL, o, 11);
    }
    if (g_isCharacterBuff((&c->Object), eBuff_Thorns))
    {
        if (SearchJoint(BITMAP_FLARE, o, 43) == false)
        {
            vec3_t vLight;
            Vector(0.9f, 0.6f, 0.1f, vLight);
            CreateJoint(BITMAP_FLARE, o->Position, o->Position, o->Angle, 43, o, 50.f, 0, 0, 0, 0,
                        vLight);
        }
    }
    else
    {
        if (removeAbsent)
            DeleteJoint(BITMAP_FLARE, o, 43);
    }

    if (g_isCharacterBuff((&c->Object), eBuff_Berserker))
    {
        if (!SearchEffect(BITMAP_ORORA, o, 0))
        {
            vec3_t vLight[2];
            Vector(0.9f, 0.0f, 0.1f, vLight[0]);
            Vector(1.0f, 1.0f, 1.0f, vLight[1]);
            for (int i = 0; i < 4; ++i)
            {
                CreateEffect(BITMAP_ORORA, o->Position, o->Angle, vLight[0], i, o);
                if (i == 2 || i == 3)
                    CreateEffect(BITMAP_SPARK + 2, o->Position, o->Angle, vLight[1], i, o);
            }
            CreateEffect(BITMAP_LIGHT_MARKS, o->Position, o->Angle, vLight[0], 0, o);
        }
    }
    else
    {
        for (int i = 0; i < 4; ++i)
        {
            if (removeAbsent)
                DeleteEffect(BITMAP_ORORA, o, i);
            if (i == 2 || i == 3)
                if (removeAbsent)
                    DeleteEffect(BITMAP_SPARK + 2, o, i);
        }
        if (removeAbsent)
            DeleteEffect(BITMAP_LIGHT_MARKS, o);
    }

    if (g_isCharacterBuff((&c->Object), eDeBuff_Blind))
    {
        if (SearchEffect(MODEL_ALICE_BUFFSKILL_EFFECT, o, 3) == false)
        {
            vec3_t vLight;
            Vector(1.0f, 1.f, 1.f, vLight);
            CreateEffect(MODEL_ALICE_BUFFSKILL_EFFECT, o->Position, o->Angle, vLight, 3, o);
        }
    }
    else
    {
        if (removeAbsent)
            DeleteEffect(MODEL_ALICE_BUFFSKILL_EFFECT, o, 3);
    }

    if (g_isCharacterBuff((&c->Object), eDeBuff_Sleep))
    {
        if (SearchEffect(MODEL_ALICE_BUFFSKILL_EFFECT, o, 4) == false)
        {
            vec3_t vLight;
            Vector(0.8f, 0.3f, 0.9f, vLight);
            CreateEffect(MODEL_ALICE_BUFFSKILL_EFFECT, o->Position, o->Angle, vLight, 4, o);
        }
    }
    else
    {
        if (removeAbsent)
            DeleteEffect(MODEL_ALICE_BUFFSKILL_EFFECT, o, 4);
    }

    if (g_isCharacterBuff((&c->Object), eDeBuff_AttackDown))
    {
        if (!SearchEffect(BITMAP_SHINY + 6, o, 1))
        {
            vec3_t vLight;
            Vector(1.4f, 0.2f, 0.2f, vLight);
            CreateEffect(BITMAP_SHINY + 6, o->Position, o->Angle, vLight, 1, o, -1, 0, 0, 0, 0.5f);
            CreateEffect(BITMAP_PIN_LIGHT, o->Position, o->Angle, vLight, 1, o, -1, 0, 0, 0, 1.f);
        }
    }
    else
    {
        if (removeAbsent)
            DeleteEffect(BITMAP_SHINY + 6, o, 1);
        if (removeAbsent)
            DeleteEffect(BITMAP_PIN_LIGHT, o, 1);
    }

    if (g_isCharacterBuff((&c->Object), eDeBuff_DefenseDown))
    {
        if (!SearchEffect(BITMAP_SHINY + 6, o, 2))
        {
            vec3_t vLight;
            Vector(0.25f, 1.0f, 0.7f, vLight);
            CreateEffect(BITMAP_SHINY + 6, o->Position, o->Angle, vLight, 2, o, -1, 0, 0, 0, 0.5f);
            CreateEffect(BITMAP_PIN_LIGHT, o->Position, o->Angle, vLight, 2, o, -1, 0, 0, 0, 1.f);
        }
    }
    else
    {
        if (removeAbsent)
            DeleteEffect(BITMAP_SHINY + 6, o, 2);
        if (removeAbsent)
            DeleteEffect(BITMAP_PIN_LIGHT, o, 2);
    }

    if (g_isCharacterBuff((&c->Object), eDeBuff_SahamuttDOT))
    {
        g_SummonSystem.CreateDamageOfTimeEffect(AT_SKILL_SUMMON_EXPLOSION, &c->Object);
    }
    else
    {
        if (removeAbsent)
            g_SummonSystem.RemoveDamageOfTimeEffect(AT_SKILL_SUMMON_EXPLOSION, &c->Object);
    }

    if (g_isCharacterBuff((&c->Object), eDeBuff_NeilDOT))
    {
        g_SummonSystem.CreateDamageOfTimeEffect(AT_SKILL_SUMMON_REQUIEM, &c->Object);
    }
    else
    {
        if (removeAbsent)
            g_SummonSystem.RemoveDamageOfTimeEffect(AT_SKILL_SUMMON_REQUIEM, &c->Object);
    }

    if (g_isCharacterBuff((&c->Object), eBuff_SwellOfMagicPower) &&
        !g_isCharacterBuff((&c->Object), eBuff_Cloaking))
    {
        if (!SearchEffect(MODEL_SWELL_OF_MAGICPOWER_BUFF_EFF, o, 0))
        {
            vec3_t vLight;
            Vector(0.7f, 0.2f, 0.9f, vLight);
            CreateEffect(MODEL_SWELL_OF_MAGICPOWER_BUFF_EFF, o->Position, o->Angle, vLight, 0, o);
        }
    }
    else
    {
        if (removeAbsent)
            DeleteEffect(MODEL_SWELL_OF_MAGICPOWER_BUFF_EFF, o, 0);
    }
}

void SessionVisualUnit::AdvanceCriticalDamageVisual(CHARACTER &character,
                                                    WorldCharacterVisualState &visual)
{
    if (CharacterWeaponsOnBack(character))
        return;
    auto *c = &character;
    auto *o = &character.Object;
    auto *b = &Models[o->Type];
    vec3_t p, Position, Light;
    if (!g_isCharacterBuff(o, eBuff_Cloaking))
    {
        constexpr float CritDamageEffectInterval = 1200.0f;
        if (g_isCharacterBuff(o, eBuff_AddCriticalDamage) && o->Kind == KIND_PLAYER &&
            o->Type == MODEL_PLAYER &&
            (visual.lastCriticalDamageTime < WorldTime - CritDamageEffectInterval))
        {
            visual.lastCriticalDamageTime = WorldTime;
            bool renderSkillWave = (WorldRandom() % 20) ? true : false;
            short weaponType = -1;
            Vector(0.f, 0.f, 0.f, p);
            Vector(1.f, 0.6f, 0.3f, Light);
            if (c->Weapon[0].Type != -1 &&
                c->Weapon[0].Type !=
                    MODEL_ARROWS) //&& ( c->Weapon[0].Type<MODEL_SHIELD || c->Weapon[0].Type>=MODEL_SHIELD+MAX_ITEM_INDEX ) )
            {
                b->TransformByObjectBone(Position, o, c->Weapon[0].LinkBone);
                if (c->Weapon[0].Type >= MODEL_BOW &&
                    c->Weapon[0].Type < MODEL_BOW + MAX_ITEM_INDEX)
                {
                    weaponType = 1;
                }
                CreateEffect(BITMAP_FLARE_FORCE, Position, o->Angle, o->Light, 1, o, weaponType,
                             c->Weapon[0].LinkBone);
                if (renderSkillWave == false)
                {
                    CreateEffect(MODEL_DARKLORD_SKILL, Position, o->Angle, Light, 0);
                }
            }
            if (c->Weapon[1].Type != -1 && c->Weapon[1].Type != MODEL_BOLT &&
                (c->Weapon[1].Type < MODEL_SHIELD ||
                 c->Weapon[1].Type >= MODEL_SHIELD + MAX_ITEM_INDEX))
            {
                b->TransformByObjectBone(Position, o, c->Weapon[1].LinkBone);
                if (c->Weapon[1].Type >= MODEL_BOW &&
                    c->Weapon[1].Type < MODEL_BOW + MAX_ITEM_INDEX)
                {
                    weaponType = 1;
                }
                CreateEffect(BITMAP_FLARE_FORCE, Position, o->Angle, o->Light, 1, o, weaponType,
                             c->Weapon[1].LinkBone);
                if (renderSkillWave == false)
                {
                    CreateEffect(MODEL_DARKLORD_SKILL, Position, o->Angle, Light, 1);
                }
            }

            PlayBuffer(SOUND_CRITICAL, o);
        }
    }
}

void SessionVisualUnit::AdvanceDoppelgangerVisual(CHARACTER &character)
{
    auto &object = character.Object;
    if (character.MonsterIndex < MONSTER_TERRIBLE_BUTCHER ||
        character.MonsterIndex > MONSTER_DOPPELGANGER_SUM ||
        TheMapProcess().CharacterPolicy().cloneAppearances || object.CurrentAction != PLAYER_DIE1)
        return;
    auto &model = Models[object.Type];
    AnimationPoseSample pose(&object, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
    {
        vec3_t position, relative{};
        const float fraction = birthTime.FrameFraction();
        pose.SampleBonePosition(model, object, WorldRandom() % model.NumBones, relative, WorldTime,
                                fraction, position);
        vec3_t light{object.Alpha, 0.3f * object.Alpha, 0.1f * object.Alpha};
        CreateParticle(BITMAP_TWINTAIL_WATER, position, object.Angle, light, 2, 0.5f);
    }
}

void SessionVisualUnit::AdvanceRabbitVisual(CHARACTER &character, WorldCharacterVisualState &visual)
{
    auto *o = &character.Object;
    if (o->Type == MODEL_LUNAR_RABBIT)
    {
        vec3_t vLight;
        vec3_t vPos, vRelatedPos;
        Vector(0.3f, 0.3f, 0.0f, vLight);
        Vector(0.f, 0.f, 0.f, vRelatedPos);

        if (visual.action == MONSTER01_WALK)
        {
            if (rand_fps_check(20))
                PlayBuffer(SOUND_MOONRABBIT_WALK);
        }

        if (visual.action == MONSTER01_SHOCK)
        {
            if (visual.CrossesAnimationWindow(2.f, 3.f))
                PlayBuffer(SOUND_MOONRABBIT_DAMAGE);
        }

        if (visual.action == MONSTER01_DIE)
        {
            if (visual.CrossesAnimationWindow(1.f, 2.f))
                PlayBuffer(SOUND_MOONRABBIT_DEAD);

            if (visual.animationFrame > 9.f)
            {
                if (!visual.rabbitDeathEmitted)
                {
                    const float fraction =
                        o->MotionTrace.FirstAnimationCrossing(WorldTime, visual.action, 9.f)
                            .value_or(1.f);
                    auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR *
                                                                         (1.f - fraction));
                    Vector(0.5f, 0.5f, 0.0f, vLight);
                    BMD &model = Models[o->Type];
                    AnimationPoseSample pose(CharacterPresentationInput(character).object,
                                             model.BoneHead, model.BodyHeight, false,
                                             model.PoseAssetIdentity());
                    pose.SampleBonePosition(model, *o, CharacterSocket::Rabbit_1, vRelatedPos,
                                            WorldTime, fraction, vPos);

                    Vector(0.7f, 1.0f, 0.6f, vLight);
                    vec3_t vMoonPos;
                    VectorCopy(vPos, vMoonPos);
                    vMoonPos[2] += 28.f;
                    CreateEffect(MODEL_MOONHARVEST_MOON, vMoonPos, o->Angle, vLight, 0, NULL, -1, 0,
                                 0, 0, 0.5f);

                    Vector(0.4f, 0.4f, 0.8f, vLight);
                    CreateParticle(BITMAP_EXPLOTION_MONO, vPos, o->Angle, vLight, 10, 1.0f);

                    Vector(1.0f, 1.0f, 1.0f, vLight);
                    for (int i = 0; i < 200; i++)
                    {
                        CreateParticle(BITMAP_SPARK + 1, vPos, o->Angle, vLight, 21);
                    }

                    for (int i = 0; i < 150; i++)
                    {
                        CreateParticle(BITMAP_SPARK + 1, vPos, o->Angle, vLight, 22);
                    }
                    Vector(1.0f, 1.0f, 1.0f, vLight);
                    switch (o->SubType)
                    {
                    case 0: {
                        for (int i = 0; i < 10; i++)
                        {
                            CreateEffect(MODEL_MOONHARVEST_GAM, vPos, o->Angle, vLight);
                        }
                    }
                    break;
                    case 1: {
                        for (int i = 0; i < 5; i++)
                        {
                            CreateEffect(MODEL_MOONHARVEST_SONGPUEN1, vPos, o->Angle, vLight);
                            CreateEffect(MODEL_MOONHARVEST_SONGPUEN2, vPos, o->Angle, vLight);
                        }
                    }
                    break;
                    case 2: {
                        for (int i = 0; i < 10; i++)
                        {
                            CreateEffect(MODEL_NEWYEARSDAY_EVENT_BEKSULKI, vPos, o->Angle, vLight);
                        }
                    }
                    break;
                    }

                    visual.rabbitDeathEmitted = true;

                    if (visual.animationFrame <= 10.f)
                    {
                        // flare
                        Vector(1.0f, 0.0f, 0.0f, vLight);
                        CreateSprite(BITMAP_LIGHT, vPos, 8.0f, vLight, o);
                    }
                }
            }
        }
        else
        {
            boneManager_.GetBonePosition(o, CharacterSocket::Rabbit_1, vPos); // Bip01 Spine
            Vector(0.4f, 0.4f, 0.9f, vLight);
            CreateSprite(BITMAP_LIGHT, vPos, 3.0f, vLight, o); // flare01.jpg

            boneManager_.GetBonePosition(o, CharacterSocket::Rabbit_2, vPos); // Bip01 Head
            Vector(0.4f, 0.4f, 0.9f, vLight);
            CreateSprite(BITMAP_LIGHT, vPos, 2.0f, vLight, o); // flare01.jpg

            boneManager_.GetBonePosition(o, CharacterSocket::Rabbit_4, vPos); // Bip01 Pelvis
            Vector(0.4f, 0.4f, 0.9f, vLight);
            CreateSprite(BITMAP_LIGHT, vPos, 1.0f, vLight, o); // flare01.jpg
        }
    }
}

bool SessionGameplayUnit::PrepareCharacterObservation(CHARACTER &character)
{
    auto &object = character.Object;
    if (CharacterPresentationDetail::IsExpiredXmasCharacter(object))
        return false;
    if (!TheMapProcess().CanObserveCharacter(character) && Hero->TargetCharacter == character.Key)
        SetCharacterTarget(*Hero, -1);
    if (&character == Hero && (0x04 & Hero->CtlCode) && SceneFlag == MAIN_SCENE)
    {
        object.OBB.StartPos[0] = 1000.f;
        object.OBB.XAxis[0] = object.OBB.YAxis[1] = object.OBB.ZAxis[2] = 1.f;
    }
    return true;
}

void SessionVisualUnit::AdvanceProtectGuildMark(CHARACTER &character,
                                                WorldCharacterVisualState &visual)
{
    auto &object = character.Object;
    if (SceneFlag != MAIN_SCENE || character.GuildType != GT_ANGEL)
        return;
    if (object.Kind == KIND_PLAYER && !gMapManager.InBloodCastle() &&
        !gMapManager.InChaosCastle() && gMapManager.InBattleCastle())
        return;
    constexpr double guildMarkIntervalMilliseconds = 5000.0;
    if (visual.protectGuildMarkTime != 0.0 &&
        WorldTime - visual.protectGuildMarkTime <= guildMarkIntervalMilliseconds)
        return;
    CreateEffect(MODEL_PROTECTGUILD, object.Position, object.Angle, object.Light, 0, &object);
    visual.protectGuildMarkTime = WorldTime;
}

bool SessionVisualUnit::IsBackItem(const CHARACTER *c, int iType)
{
    int iBowType = gCharacterManager.GetEquipedBowType(c);
    if (iBowType != BOWTYPE_NONE)
    {
        return true;
    }

    if (iType >= MODEL_SWORD && iType < MODEL_SHIELD + MAX_ITEM_INDEX &&
        !(iType >= MODEL_BOOK_OF_SAHAMUTT && iType <= MODEL_STAFF + 29))
    {
        return true;
    }

    return false;
}

bool SessionVisualUnit::CharacterWeaponsOnBack(const CHARACTER &character)
{
    if (gMapManager.InBloodCastle() || gMapManager.InChaosCastle())
        return false;
    const auto action = character.Object.CurrentAction;
    return character.SafeZone || (action >= PLAYER_GREETING1 && action <= PLAYER_SALUTE1) ||
           (TheMapProcess().CharacterPolicy().swimming &&
            (action == PLAYER_WALK_SWIM || action == PLAYER_RUN_SWIM));
}

void CMonkSystem::AdvanceDarksideVisual(CHARACTER &character, WorldCharacterVisualState &visual)
{
    const auto &facts = character.Darkside;
    if (facts.targetCount == 0 && facts.attackCount == 0)
    {
        visual.darkside.reset();
        return;
    }
    if (!visual.darkside || visual.darkside->revision != facts.revision)
    {
        visual.darkside = std::make_unique<CharacterDarksideVisual>();
        visual.darkside->revision = facts.revision;
    }
    auto &state = *visual.darkside;
    auto &poses = visual.darksidePoses;
    float dummyAnimationFrame = 0.f;
    auto *pCha = &character;
    auto *pObj = &character.Object;
    auto pose = MonkPresentationDetail::CaptureDarksidePose(*pObj);
    const float fAnimationFrame = pObj->AnimationFrame;
    vec3_t vPos, vOrgPos;
    VectorCopy(pObj->Position, vPos);
    VectorCopy(pObj->Position, vOrgPos);
    if (facts.targetCount > state.attackCount)
    {
        const auto &target = facts.targets[state.attackCount];
        const int _TargetIndex = CharactersClient.FindIndexByKey(target.key);

        state.otherAnimationFrame = (float)(pCha->AttackTime + 1) * 0.1f * 2.0f;
        state.distanceFrame += sessionKeeper_.FrameAnimationFactor();
        const float nextDistanceFrame = state.distanceFrame + sessionKeeper_.FrameAnimationFactor();
        if (state.otherAnimationFrame >= 2)
        {
            state.otherAnimationFrame = 1.9f;
        }

        if (_TargetIndex >= 0 && CharactersClient[_TargetIndex].SocketSource == target.source)
        {
            VectorCopy(CharactersClient[_TargetIndex].Object.Position, vPos);
        }
        VectorCopy(vOrgPos, pose.startPosition);
        const bool reachedTarget =
            CalculateDarksideTrans(pose, vPos, state.distanceFrame, nextDistanceFrame);

        vec3_t vDisLen;
        VectorSubtract(pose.position, pose.startPosition, vDisLen);
        int nLen = VectorLength(vDisLen);

        vec3_t vLight, vAngle;
        Vector(1.0f, 1.0f, 1.0f, vLight);
        if (nLen > 30 && !state.shockwaveEmitted)
        {
            VectorCopy(pose.angle, vAngle);
            Vector(90.0f, 0.0f, vAngle[2] - 180.0f, vAngle);
            CreateEffect(MODEL_SHOCKWAVE01, pose.position, vAngle, vLight, 3, pObj, -1, 0, 0, 0,
                         0.8f);
            state.shockwaveEmitted = true;
        }
        if (reachedTarget)
        {
            CreateParticleFpsChecked(BITMAP_DAMAGE2, vPos, pose.angle, vLight, 0, 1.0f);
        }

        if (facts.attackCount > state.attackCount)
        {
            state.attackCount++;
            state.otherAnimationFrame = 0.1f;
            state.distanceFrame = 0;
            state.shockwaveEmitted = false;
        }

        pose.animationFrame = state.otherAnimationFrame;
        poses.push_back(pose);
    }

    if (facts.attackCount > 0)
    {
        if (fAnimationFrame > (state.dummyCount * 2) &&
            state.dummyCount < CharacterDarksideVisual::DummyCount)
        {
            const int primaryIndex = CharactersClient.FindIndexByKey(facts.primaryTarget.key);
            if (primaryIndex >= 0 &&
                CharactersClient[primaryIndex].SocketSource == facts.primaryTarget.source)
                VectorCopy(CharactersClient[primaryIndex].Object.Position, vPos);
            if (state.attackCount < facts.targetCount)
            {
                const auto &target = facts.targets[state.attackCount];
                const int index = CharactersClient.FindIndexByKey(target.key);
                if (index >= 0 && CharactersClient[index].SocketSource == target.source)
                    VectorCopy(CharactersClient[index].Object.Position, vPos);
            }

            VectorCopy(vPos, pose.position);
            SetDummy(state, pose.position, vPos);
            dummyAnimationFrame = 0.0f;
        }

        for (int index = 0; index < state.dummyCount; ++index)
        {
            bool bChange = false;
            if (pObj->Kind == KIND_PLAYER && pObj->Type == MODEL_PLAYER &&
                (pObj->SubType == MODEL_SKELETON_CHANGED || pObj->SubType == MODEL_PANDA ||
                 pObj->SubType == MODEL_SKELETON_PCBANG || pObj->SubType == MODEL_HALLOWEEN ||
                 pObj->SubType == MODEL_XMAS_EVENT_CHANGE_GIRL || pObj->SubType == MODEL_SKELETON1))
            {
                bChange = true;
            }

            auto *pDummy = &*state.dummies[index];
            if (!pDummy->IsDistance(sessionKeeper_.FrameAnimationFactor()))
                continue;
            const float priorFrame = pDummy->GetAniFrame(bChange);
            pDummy->CalDummyPosition(pose.position, dummyAnimationFrame, bChange,
                                     sessionKeeper_.FrameAnimationFactor());
            {
                VectorCopy(pDummy->GetStartPosition(), pose.startPosition);

                pose.alpha = pDummy->GetAlpha();

                vec3_t vAngle;
                VectorCopy(pose.angle, vAngle);
                vAngle[2] = CreateAngle2D(pose.startPosition, pose.position);
                VectorCopy(vAngle, pose.angle);

                vec3_t vLight;
                constexpr float shockwaveFrame = 1.5f;
                if (priorFrame < shockwaveFrame && dummyAnimationFrame >= shockwaveFrame)
                {
                    Vector(1.0f, 1.0f, 1.0f, vLight);
                    VectorScale(vLight, 0.2f, vLight);
                    Vector(90.0f, 0.0f, vAngle[2] - 180.0f, vAngle);
                    CreateEffect(MODEL_SHOCKWAVE01, pose.position, vAngle, vLight, 4, pObj, -1, 0,
                                 0, 0, 0.8f);
                }

                constexpr float impactFrame = 1.f;
                if (priorFrame < impactFrame && dummyAnimationFrame >= impactFrame &&
                    facts.targetCount > state.attackCount)
                {
                    Vector(1.0f, 1.0f, 1.0f, vLight);
                    CreateParticleFpsChecked(BITMAP_DAMAGE2, vPos, pose.angle, vLight, 0, 1.0f);
                }

                pose.animationFrame = dummyAnimationFrame;
                poses.push_back(pose);
            }
        }
    }
}

namespace
{

bool ShouldAssignWebzenPart(CHARACTER &character)
{
    const bool hasGmBuff = g_isCharacterBuff((&character.Object), eBuff_GMEffect) != FALSE;
    const bool isOperator =
        (character.CtlCode == CTLCODE_20OPERATOR) || (character.CtlCode == CTLCODE_08OPERATOR);
    return hasGmBuff || isOperator;
}

std::unique_ptr<CSIPartsMDL> CreatePartsByType(SessionKeeper &keeper, int etcPart)
{
    switch (etcPart)
    {
    case PARTS_WEBZEN:
        return std::make_unique<CSParts>(keeper, MODEL_WEBZEN_MARK,
                                         CharacterPartsDetail::kDefaultBoneIndex, false, 70.f, -5.f,
                                         0.f, 0.f, 0.f, 45.f);
    case PARTS_ATTACK_TEAM_MARK:
        return std::make_unique<CSParts2D>(keeper, BITMAP_FORMATION_MARK, 0,
                                           CharacterPartsDetail::kDefaultBoneIndex, 120.f, 0.f,
                                           0.f);
    case PARTS_ATTACK_TEAM_MARK2:
        return std::make_unique<CSParts2D>(keeper, BITMAP_FORMATION_MARK, 1,
                                           CharacterPartsDetail::kDefaultBoneIndex, 120.f, 0.f,
                                           0.f);
    case PARTS_ATTACK_TEAM_MARK3:
        return std::make_unique<CSParts2D>(keeper, BITMAP_FORMATION_MARK, 2,
                                           CharacterPartsDetail::kDefaultBoneIndex, 120.f, 0.f,
                                           0.f);
    case PARTS_ATTACK_KING_TEAM_MARK:
        return std::make_unique<CSParts2D>(keeper, BITMAP_FORMATION_MARK, 3,
                                           CharacterPartsDetail::kDefaultBoneIndex, 120.f, 0.f,
                                           0.f);
    case PARTS_ATTACK_KING_TEAM_MARK2:
        return std::make_unique<CSParts2D>(keeper, BITMAP_FORMATION_MARK, 4,
                                           CharacterPartsDetail::kDefaultBoneIndex, 120.f, 0.f,
                                           0.f);
    case PARTS_ATTACK_KING_TEAM_MARK3:
        return std::make_unique<CSParts2D>(keeper, BITMAP_FORMATION_MARK, 5,
                                           CharacterPartsDetail::kDefaultBoneIndex, 120.f, 0.f,
                                           0.f);
    case PARTS_DEFENSE_TEAM_MARK:
        return std::make_unique<CSParts2D>(keeper, BITMAP_FORMATION_MARK, 6,
                                           CharacterPartsDetail::kDefaultBoneIndex, 120.f, 0.f,
                                           0.f);
    case PARTS_DEFENSE_KING_TEAM_MARK:
        return std::make_unique<CSParts2D>(keeper, BITMAP_FORMATION_MARK, 7,
                                           CharacterPartsDetail::kDefaultBoneIndex, 120.f, 0.f,
                                           0.f);
    default:
        return nullptr;
    }
}

void InitializeCommonObjectState(OBJECT &object, int type, bool billboard)
{
    object.Type = type;
    object.Live = true;
    object.Visible = false;
    object.LightEnable = true;
    object.ContrastEnable = false;
    object.AlphaEnable = false;
    object.EnableBoneMatrix = false;
    object.Owner = nullptr;
    object.SubType = 0;
    object.HiddenMesh = -1;
    object.BlendMesh = -1;
    object.BlendMeshLight = 1.f;
    object.Scale = 1.f;
    object.LifeTime = CharacterPartsDetail::kDefaultLifetimeTicks;
    object.Alpha = 1.f;
    object.AlphaTarget = 1.f;
    object.EnableShadow = false;
    object.CurrentAction = 0;
    object.PriorAction = 0;
    object.PriorAnimationFrame = 0.f;
    object.AnimationFrame = 0.f;
    object.Velocity = CharacterPartsDetail::kDefaultVelocity;
    object.bBillBoard = billboard;

    g_CharacterClearBuff((&object));
    Vector(1.f, 1.f, 1.f, object.Light);
    Vector(0.f, 0.f, 0.f, object.HeadAngle);
}
} // namespace

void SessionGameplayUnit::CreatePartsFactory(CHARACTER *character)
{
    if (character == nullptr || !ShouldAssignWebzenPart(*character) ||
        character->EtcPart == PARTS_WEBZEN)
        return;
    character->EtcPart = PARTS_WEBZEN;
    character->MarkAppearanceChanged();
}

void SessionVisualUnit::AdvanceParts(CHARACTER &character, WorldCharacterVisualState &visual,
                                     bool advanceAnimation)
{
    const int type = ShouldAssignWebzenPart(character) ? PARTS_WEBZEN : character.EtcPart;
    const int temporaryType = character.Object.Type == MODEL_PLAYER &&
                                      character.Object.Kind == KIND_PLAYER &&
                                      character.Object.SubType == MODEL_XMAS_EVENT_CHANGE_GIRL
                                  ? MODEL_XMAS_EVENT_EARRING
                                  : -1;
    if (!advanceAnimation && visual.partsType == type &&
        visual.temporaryPartsType == temporaryType &&
        visual.partsAppearanceRevision == character.WorldVisualAppearanceRevision &&
        visual.partsBuffRevision == character.Object.m_BuffMap.Revision() &&
        visual.partsPoseRevision == character.WorldVisualPoseRevision)
        return;
    const bool replaceParts = visual.partsType != type;
    const bool replaceTemporary = visual.temporaryPartsType != temporaryType;
    if (replaceParts || replaceTemporary)
    {
        auto oldParts = replaceParts ? std::move(visual.parts) : nullptr;
        auto oldTemporary = replaceTemporary ? std::move(visual.temporaryParts) : nullptr;
        OBJECT *targets[2];
        std::size_t count = 0;
        if (oldParts)
            targets[count++] = oldParts->GetObject();
        if (oldTemporary)
            targets[count++] = oldTemporary->GetObject();
        gameplay_.RetireCharacterEffectTargets(std::span(targets, count));
        if (replaceParts)
        {
            visual.parts = CreatePartsByType(sessionKeeper_, type);
            visual.partsType = type;
        }
        if (replaceTemporary)
        {
            if (temporaryType != -1)
            {
                visual.temporaryParts = std::make_unique<CSParts>(
                    sessionKeeper_, temporaryType, CharacterPartsDetail::kDefaultBoneIndex);
                auto &object = *visual.temporaryParts->GetObject();
                object.Velocity = 0.25f;
                object.Owner = &character.Object;
            }
            visual.temporaryPartsType = temporaryType;
        }
    }
    visual.partsAppearanceRevision = character.WorldVisualAppearanceRevision;
    visual.partsBuffRevision = character.Object.m_BuffMap.Revision();
    visual.partsPoseRevision = character.WorldVisualPoseRevision;
    if (g_isCharacterBuff(&character.Object, eBuff_Cloaking))
        return;
    if (type >= PARTS_ATTACK_TEAM_MARK && type <= PARTS_DEFENSE_KING_TEAM_MARK &&
        !IsBattleCastleStart())
        return;
    PrepareCharacterModel(character);
    if (visual.temporaryParts)
        visual.temporaryParts->IAdvance(&character, advanceAnimation);
    if (visual.parts)
        visual.parts->IAdvance(&character, advanceAnimation);
}

const WorldCharacterVisualState *SessionVisualUnit::FindCharacterVisual(const CHARACTER &character)
{
    if (preparingCharacter_ == &character)
        return preparingCharacterVisual_;
    const int index = CharactersClient.FindIndexByKey(character.Key);
    return index >= 0 && &CharactersClient[index] == &character
               ? &CharactersClient.WorldVisuals(index)
               : nullptr;
}

void SessionGameplayUnit::DeleteParts(CHARACTER *character)
{
    if (character == nullptr)
        return;
    if (character->EtcPart >= PARTS_WEBZEN && character->EtcPart <= PARTS_DEFENSE_KING_TEAM_MARK)
    {
        character->EtcPart = 0;
        character->MarkAppearanceChanged();
    }
}

bool CSIPartsMDL::PreparePose()
{
    auto &model = Models[m_pObj.Type];
    if (!pose_)
    {
        if (model.NumBones == 0 || model.NumActions == 0)
            return false;
        pose_ = std::make_unique<vec34_t[]>(model.NumBones);
    }
    m_pObj.BoneTransform = pose_.get();
    model.BodyScale = m_pObj.Scale;
    model.BodyHeight = 0.f;
    VectorCopy(m_pObj.Position, model.BodyOrigin);
    const AnimationPoseSample sample(&m_pObj, model.BoneHead, 0.f, false,
                                     model.PoseAssetIdentity());
    if (poseSample_ != sample)
    {
        sample.Evaluate(model, m_pObj.BoneTransform);
        poseSample_ = sample;
    }
    m_pObj.EnableBoneMatrix = true;
    return true;
}

CSParts::CSParts(SessionKeeper &keeper, int Type, int BoneNumber, bool bBillBoard, float x, float y,
                 float z, float ax, float ay, float az)
    : CSIPartsMDL(keeper)
{
    m_iBoneNumber = BoneNumber;

    m_vOffset[0] = x;
    m_vOffset[1] = y;
    m_vOffset[2] = z;

    InitializeCommonObjectState(m_pObj, Type, bBillBoard);
    Vector(ax, ay, az, m_pObj.Angle);
}

void CSParts::IAdvance(CHARACTER *c, bool advanceAnimation)
{
    if (c == nullptr)
    {
        return;
    }

    if (m_pObj.Alpha < CharacterPartsDetail::kRenderableAlphaThreshold)
    {
        return;
    }

    OBJECT *o = &c->Object;
    BMD *b = &Models[o->Type];
    vec3_t Position;

    b->TransformByObjectBone(Position, CharacterPresentationInput(*c).object, m_iBoneNumber,
                             m_vOffset);
    VectorCopy(Position, m_pObj.Position);
    if (!m_pObj.bBillBoard)
    {
        const CharacterDrawInput presentation(c);
        VectorCopy(presentation.object.angle, m_pObj.Angle);
    }

    if (m_pObj.Type == MODEL_XMAS_EVENT_EARRING)
    {
        m_pObj.Angle[2] -= CharacterPresentationInput(*c).object.headAngle[0];
    }

    b = &Models[m_pObj.Type];
    b->CurrentAction = m_pObj.CurrentAction;

    const float speed = m_pObj.Velocity;
    if (advanceAnimation)
        b->PlayAnimation(&m_pObj.AnimationFrame, &m_pObj.PriorAnimationFrame, &m_pObj.PriorAction,
                         speed, m_pObj.Position, m_pObj.Angle);

    if (!PreparePose() || !advanceAnimation || m_pObj.Type != MODEL_XMAS_EVENT_EARRING)
        return;
    vec3_t light, white{1.f, 1.f, 1.f};
    const float luminosity =
        (sinf(static_cast<float>(sessionKeeper_.FrameWorldTime()) * 0.004f) + 1.f) * 0.05f;
    Vector(0.8f + luminosity, 0.8f + luminosity, 0.3f + luminosity, light);
    VectorCopy(light, m_pObj.Light);
    for (float side : {18.f, -18.f})
    {
        vec3_t offset{side, 0.f, 6.f}, position;
        b->TransformByObjectBone(position, &m_pObj, 0, offset);
        CreateSprite(BITMAP_LIGHT, position, 0.4f, light, &m_pObj, 0.5f);
        for (auto birth :
             sessionKeeper_.Gameplay()->Emissions(sessionKeeper_.FrameAnimationFactor() / 15.f))
        {
            BMD &parentModel = Models[o->Type];
            AnimationPoseSample parentPose(CharacterPresentationInput(*c).object,
                                           parentModel.BoneHead, parentModel.BodyHeight, false,
                                           parentModel.PoseAssetIdentity());
            vec3_t anchor, birthPosition;
            parentPose.SampleBonePosition(parentModel, *o, m_iBoneNumber, m_vOffset,
                                          sessionKeeper_.FrameWorldTime(), birth.FrameFraction(),
                                          anchor);
            VectorSubtract(position, m_pObj.Position, birthPosition);
            VectorAdd(birthPosition, anchor, birthPosition);
            CreateParticle(BITMAP_SHINY, birthPosition, m_pObj.Angle, white, 5, 0.5f);
        }
    }
}

CSAnimationParts::CSAnimationParts(SessionKeeper &keeper, int Type, int BoneNumber, bool bBillBoard,
                                   float x, float y, float z, float ax, float ay, float az)
    : CSIPartsMDL(keeper)
{
    m_iBoneNumber = BoneNumber;

    m_vOffset[0] = x;
    m_vOffset[1] = y;
    m_vOffset[2] = z;

    InitializeCommonObjectState(m_pObj, Type, bBillBoard);
    Vector(ax, ay, az, m_pObj.Angle);
}

void CSAnimationParts::Animation(CHARACTER *c)
{
    if (c == nullptr)
    {
        return;
    }

    BMD *b = &Models[m_pObj.Type];
    b->CurrentAction = m_pObj.CurrentAction;

    float fSpeed = m_pObj.Velocity;
    b->PlayAnimation(&m_pObj.AnimationFrame, &m_pObj.PriorAnimationFrame, &m_pObj.PriorAction,
                     fSpeed, m_pObj.Position, m_pObj.Angle);
}

void CSAnimationParts::IAdvance(CHARACTER *c, bool advanceAnimation)
{
    if (c == nullptr)
    {
        return;
    }

    if (m_pObj.Alpha < CharacterPartsDetail::kRenderableAlphaThreshold)
    {
        return;
    }

    OBJECT *o = &c->Object;
    BMD *b = &Models[o->Type];
    vec3_t Position;

    b->TransformByObjectBone(Position, CharacterPresentationInput(*c).object, m_iBoneNumber,
                             m_vOffset);
    VectorCopy(Position, m_pObj.Position);
    if (!m_pObj.bBillBoard)
    {
        const CharacterDrawInput presentation(c);
        VectorCopy(presentation.object.angle, m_pObj.Angle);
    }

    if (advanceAnimation)
        Animation(c);
    PreparePose();
}

CSParts2D::CSParts2D(SessionKeeper &keeper, int Type, int SubType, int BoneNumber, float x, float y,
                     float z)
    : CSIPartsMDL(keeper)
{
    m_iBoneNumber = BoneNumber;

    m_vOffset[0] = x;
    m_vOffset[1] = y;
    m_vOffset[2] = z;

    InitializeCommonObjectState(m_pObj, Type, true);
    m_pObj.SubType = SubType;
}

void CSParts2D::IAdvance(CHARACTER *c, bool)
{
    if (c == nullptr)
    {
        return;
    }

    OBJECT *o = &c->Object;
    BMD *b = &Models[o->Type];
    vec3_t Position;
    BYTE bSubType = m_pObj.SubType;

    b->TransformByObjectBone(Position, CharacterPresentationInput(*c).object, m_iBoneNumber,
                             m_vOffset);
    VectorCopy(o->Position, m_pObj.Position);
    m_pObj.Position[2] = Position[2];

    const bool isMarkType =
        (c->EtcPart == PARTS_ATTACK_TEAM_MARK || c->EtcPart == PARTS_ATTACK_TEAM_MARK2 ||
         c->EtcPart == PARTS_ATTACK_TEAM_MARK3 || c->EtcPart == PARTS_DEFENSE_TEAM_MARK);

    if (isMarkType && c->GuildStatus == G_MASTER)
    {
        const bool sameUnionAndGuild = (wcscmp(GuildMark[c->GuildMarkIndex].UnionName,
                                               GuildMark[c->GuildMarkIndex].GuildName) == 0);
        const bool emptyUnionName = (wcscmp(GuildMark[c->GuildMarkIndex].UnionName, L"") == 0);
        if (sameUnionAndGuild || emptyUnionName)
        {
            if (c->EtcPart == PARTS_DEFENSE_TEAM_MARK)
            {
                bSubType += 1;
            }
            else
            {
                bSubType += 3;
            }
        }
    }

    preparedSubType_ = bSubType;
}

//int   World = -1;

void SessionVisualUnit::BodyLight(const ObjectDrawInput &draw, BMD *b)
{
    if (draw.type == MODEL_DARK_PHEONIX_SHIELD)
    {
        Vector(.6f, .6f, .6f, b->BodyLight);
        return;
    }
    if (draw.type == MODEL_PROTECT)
    {
        float Luminosity = sinf(WorldTime * 0.003f) * 0.5f + 0.5f;
        Vector(Luminosity, Luminosity, Luminosity, b->BodyLight);
        return;
    }

    b->LightEnable = draw.lightEnable;
    if (draw.lightEnable)
    {
        vec3_t Light;
        RequestTerrainLight(draw.position[0], draw.position[1], Light);
        VectorAdd(Light, draw.light, b->BodyLight);
    }
    else
    {
        vec3_t Light;
        RequestTerrainLight(draw.position[0], draw.position[1], Light);
        VectorScale(Light, 0.1f, Light);
        VectorAdd(Light, draw.light, b->BodyLight);
    }
}

void SessionVisualUnit::PrepareRigidObjectPose(OBJECT &object)
{
    ObjectBlock[object.Block].DrawGroupsDirty = true;
    object.RigidPoseDirty = false;
    object.RigidPose.reset();
    // Lorencia's placement updates change lighting/materials, not local poses.
    // Other map families keep their authored animation/state paths for now.
    if (!WorldPrimaryModel::UsesRigidGeometry(gMapManager.ContextMap(), object.Type))
        return;
    auto &model = Models[object.Type];
    if (!model.renderTapeRigidGeometry_.vertices || model.BoneHead >= 0)
        return;
    auto pose = std::make_shared<RigidObjectPose>();
    pose->bones.resize(model.NumBones);
    auto *matrices = reinterpret_cast<vec34_t *>(pose->bones.data());
    auto *transform = reinterpret_cast<float(*)[4]>(&pose->transform);
    AngleMatrix(object.Angle, transform);
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
            transform[row][column] *= object.Scale;
        transform[row][3] = object.Position[row];
    }
    model.BodyHeight = 0.f;
    model.BodyScale = object.Scale;
    VectorCopy(object.Position, model.BodyOrigin);
    model.Animation(matrices, 0.f, 0.f, 0, object.Angle, object.HeadAngle, false, true, nullptr, 0);
    object.RigidPose = std::move(pose);
}

void SessionVisualUnit::PartObjectColor(int Type, float Alpha, float Bright, vec3_t Light,
                                        bool ExtraMon)
{
    int Color = 0;

    if (ExtraMon && (Type == MODEL_BALROG || Type == MODEL_BILL_OF_BALROG))
    {
        Color = 8;
    }
    else if (Type == MODEL_BALROG || Type == MODEL_BILL_OF_BALROG)
    {
        Color = 1;
    }
    else if (Type == MODEL_VALKYRIE || Type == MODEL_SILVER_BOW || Type == MODEL_BLUEWING_CROSSBOW)
    {
        Color = 5;
    }
    else if (Type == MODEL_LIGHTING_SWORD || Type == MODEL_LEGENDARY_STAFF ||
             (Type >= MODEL_TEAR_OF_ELF && Type < MODEL_POTION + 27))
    {
        Color = 2;
    }
    else if (Type == MODEL_CELESTIAL_BOW || Type == MODEL_GREAT_REIGN_CROSSBOW)
    {
        Color = 9;
    }
    else if (Type == MODEL_ORB_OF_GREATER_FORTITUDE)
    {
        Color = 2;
    }
    else if (Type == MODEL_DIVINE_CB_OF_ARCHANGEL)
    {
        Color = 10;
    }
    else if (Type == MODEL_DRAGON_SOUL_STAFF)
    {
        Color = 5;
    }
    else if (Type == MODEL_RUNE_BLADE)
    {
        Color = 10;
    }
    else if (Type == MODEL_ELEMENTAL_SHIELD)
    {
        Color = 6;
    }
    else if (Type == MODEL_DRAGON_SPEAR)
    {
        Color = 9;
    }
    else if (Type == MODEL_BATTLE_SCEPTER)
    {
        Color = 9;
    }
    else if (Type == MODEL_MASTER_SCEPTER)
    {
        Color = 10;
    }
    else if (Type == MODEL_GREAT_SCEPTER)
    {
        Color = 12;
    }
    else if (Type == MODEL_KNIGHT_BLADE)
    {
        Color = 10;
    }
    else if (Type == MODEL_DARK_REIGN_BLADE)
    {
        Color = 5;
    }
    else if (Type == MODEL_ARROW_VIPER_BOW)
    {
        Color = 16;
    }
    else if (Type == MODEL_STAFF_OF_KUNDUN)
    {
        Color = 17;
    }
    else if (Type == MODEL_GREAT_LORD_SCEPTER)
    {
        Color = 16;
    }
    else if (Type == MODEL_BONE_BLADE)
    {
        Color = 18;
    }
    else if (Type == MODEL_GRAND_VIPER_STAFF)
    {
        Color = 19;
    }
    else if (Type == MODEL_SYLPH_WIND_BOW)
    {
        Color = 20;
    }
    else if (Type == MODEL_EXPLOSION_BLADE)
    {
        Color = 23;
    }
    else if (Type == MODEL_SOLEIL_SCEPTER)
    {
        Color = 22;
    }
    else if (Type == MODEL_DAYBREAK)
    {
        Color = 24;
    }
    else if (Type == MODEL_PLATINA_STAFF)
    {
        Color = 25;
    }
    else if (Type == MODEL_ALBATROSS_BOW)
    {
        Color = 26;
    }
    else if (Type == MODEL_SHINING_SCEPTER)
    {
        Color = 28;
    }
    else if (Type == MODEL_SWORD_DANCER)
    {
        Color = 27;
    }
    else if (Type == MODEL_MISTERY_STICK)
        Color = 24;
    else if (Type == MODEL_VIOLENT_WIND_STICK)
        Color = 15;
    else if (Type == MODEL_RED_WING_STICK)
        Color = 1;
    else if (Type == MODEL_ANCIENT_STICK)
        Color = 3;
    else if (Type == MODEL_DEMONIC_STICK)
        Color = 30;
    else if (Type == MODEL_STORM_BLITZ_STICK)
        Color = 21;
    else if (Type == MODEL_ETERNAL_WING_STICK)
        Color = 5;
    else if (Type == MODEL_BOOK_OF_NEIL)
        Color = 1;
    else if (Type == MODEL_IMPERIAL_SWORD)
        Color = 8;
    else if (Type == MODEL_DEADLY_STAFF)
        Color = 1;
    else if (Type == MODEL_IMPERIAL_STAFF)
        Color = 19;
    else if (Type == MODEL_ABSOLUTE_SCEPTER)
        Color = 40;
    else if (Type == MODEL_FROST_BARRIER)
        Color = 29;
    else if (Type == MODEL_STINGER_BOW)
        Color = 35;
    else if (Type == MODEL_GUARDIAN_SHILED)
        Color = 36;
    else if (Type == MODEL_CROSS_SHIELD)
        Color = 30;
    else if (Type == MODEL_BEUROBA)
        Color = 20;
    else if (Type == MODEL_CHROMATIC_STAFF)
        Color = 43;
    else if (Type == MODEL_RAVEN_STICK)
        Color = 5;
    else if (Type == MODEL_STRYKER_SCEPTER)
        Color = 5;
    else if (Type == MODEL_AIR_LYN_BOW)
        Color = 36;
    else if (g_CMonkSystem.EqualItemModelType(Type) == MODEL_SACRED_GLOVE)
        Color = 16;
    else if (g_CMonkSystem.EqualItemModelType(Type) == MODEL_STORM_HARD_GLOVE)
        Color = 42;
    else if (g_CMonkSystem.EqualItemModelType(Type) == MODEL_PIERCING_BLADE_GLOVE)
        Color = 18;
    else if (g_CMonkSystem.EqualItemModelType(Type) == MODEL_PHOENIX_SOUL_STAR)
        Color = 45;
    else if (Type == MODEL_ARMORINVEN_60)
        Color = 16;
    else if (Type == MODEL_ARMORINVEN_61)
        Color = 42;
    else if (Type == MODEL_ARMORINVEN_62)
        Color = 18;
    else if (Type == MODEL_ARMORINVEN_74)
        Color = 45;
    else //  if ( Type<MODEL_WING )
    {
        int ItemType = Type - MODEL_ITEM;
        if (ItemType / MAX_ITEM_INDEX >= 7 && ItemType / MAX_ITEM_INDEX <= 11)
        {
            switch (ItemType % MAX_ITEM_INDEX)
            {
            case 1:
                Color = 1;
                break;
            case 9:
                Color = 2;
                break;
            case 12:
                Color = 2;
                break;
            case 3:
                Color = 3;
                break;
            case 13:
                Color = 4;
                break;
            case 4:
                Color = 5;
                break;
            case 14:
                Color = 5;
                break;
            case 6:
                Color = 6;
                break;
            case 15:
                Color = 7;
                break;
            case 16:
                Color = 10;
                break;
            case 17:
                Color = 9;
                break;
            case 18:
                Color = 5;
                break;
            case 19:
                Color = 9;
                break;
            case 20:
                Color = 9;
                break;
            case 21:
                Color = 16;
                break;
            case 22:
                Color = 17;
                break;
            case 23:
                Color = 11;
                break;
            case 24:
                Color = 16;
                break;
            case 25:
                Color = 11;
                break;
            case 26:
                Color = 12;
                break;
            case 27:
                Color = 10;
                break;
            case 28:
                Color = 15;
                break;
            case 29:
                Color = 18;
                break;
            case 30:
                Color = 19;
                break;
            case 31:
                Color = 20;
                break;
            case 32:
                Color = 21;
                break;
            case 33:
                Color = 22;
                break;
            case 34:
                Color = 24;
                break;
            case 35:
                Color = 25;
                break;
            case 36:
                Color = 26;
                break;
            case 37:
                Color = 27;
                break;
            case 38:
                Color = 28;
                break;
            case 39:
                Color = 29;
                break;
            case 40:
                Color = 30;
                break;
            case 41:
                Color = 31;
                break;
            case 42:
                Color = 32;
                break;
            case 43:
                Color = 33;
                break;
            case 44:
                Color = 34;
                break;
            case 45:
                Color = 36;
                break;
            case 46:
                Color = 42;
                break;
            case 47:
                Color = 37;
                break;
            case 48:
                Color = 1;
                break;
            case 49:
                Color = 35;
                break;
            case 50:
                Color = 39;
                break;
            case 51:
                Color = 40;
                break;
            case 52:
                Color = 36;
                break;
            case 53:
                Color = 41;
                break;
            case 59:
                Color = 16;
                break;
            case 60:
                Color = 42;
                break;
            case 61:
                Color = 18;
                break;
            case 73:
                Color = 45;
                break;
            }
        }
    }
    WorldObjectDetail::ApplyPartPalette(Color, Alpha, Bright, Light);
}

void SessionVisualUnit::EmitHalloweenFormVisual(OBJECT &object, unsigned int sparks, bool burst)
{
    auto *o = &object;
    auto *b = &Models[o->Type];
    vec3_t vLight;
    Vector(1.f, 1.f, 1.f, vLight);
    vec3_t vPos, vRelativePos;
    Vector(6.f, 6.f, 0.f, vRelativePos);
    b->TransformByObjectBone(vPos, o, 20, vRelativePos);
    vPos[2] += 36.f;
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
    {
        const float fraction = birth.FrameFraction();
        vec3_t position;
        AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false, b->PoseAssetIdentity());
        pose.SampleBonePosition(*b, *o, 20, vRelativePos, WorldTime, fraction, position);
        position[2] += 36.f;
        if (sessionKeeper_.Random()->FpsCheck(2, 1.f))
            CreateParticle(BITMAP_TRUE_FIRE, position, o->Angle, vLight, 8, 0.5f, o);
        CreateParticle(BITMAP_TRUE_FIRE, position, o->Angle, vLight, 9, 0.5f, o);
    }

    vPos[2] -= 5.0f;
    float fLumi;
    fLumi = (float)(WorldRandom() % 5 + 10) * 0.1f;
    Vector(fLumi + 0.5f, fLumi * 0.3f + 0.1f, 0.f, vLight);
    CreateSprite(BITMAP_LIGHT, vPos, 1.0f + fLumi * 0.1f, vLight, o);

    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 8.f))
    {
        const float fraction = birth.FrameFraction();
        AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false, b->PoseAssetIdentity());
        pose.SampleBonePosition(*b, *o, 20, vRelativePos, WorldTime, fraction, vPos);
        vPos[2] += 21.f;
        fLumi = (float)(WorldRandom() % 100) * 0.01f;
        vLight[0] = fLumi;
        fLumi = (float)(WorldRandom() % 100) * 0.01f;
        vLight[1] = fLumi;
        fLumi = (float)(WorldRandom() % 100) * 0.01f;
        vLight[2] = fLumi;
        CreateParticle(BITMAP_SHINY, vPos, o->Angle, vLight, 4, 0.8f);
    }

    for (auto birth : sessionKeeper_.Gameplay()->Emissions(sparks * FPS_ANIMATION_FACTOR))
    {
        Vector(1.f, 1.f, 1.f, vLight);
        const float fraction = birth.FrameFraction();
        AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false, b->PoseAssetIdentity());
        pose.SampleBonePosition(*b, *o, 20, vRelativePos, WorldTime, fraction, vPos);
        vPos[2] += 11.f;
        CreateParticle(BITMAP_SPARK + 2, vPos, o->Angle, vLight, 0, 0.6f);
        CreateParticle(BITMAP_SPARK + 2, vPos, o->Angle, vLight, 0, 0.6f);
        for (int i = 0; i < 20; ++i)
        {
            fLumi = (float)(WorldRandom() % 100) * 0.01f;
            vLight[0] = fLumi;
            fLumi = (float)(WorldRandom() % 100) * 0.01f;
            vLight[1] = fLumi;
            fLumi = (float)(WorldRandom() % 100) * 0.01f;
            vLight[2] = fLumi;
            CreateParticle(BITMAP_SHINY, vPos, o->Angle, vLight, 4, 0.8f);
        }
    }
    if (burst)
    {
        b->TransformByObjectBone(vPos, o, 20, vRelativePos);
        vPos[2] += 11.f;
        Vector(1.f, 1.f, 1.f, vLight);
        CreateEffect(MODEL_HALLOWEEN_EX, vPos, o->Angle, vLight, 0);
        vPos[2] -= 40.f;
        CreateParticle(BITMAP_SPARK + 2, vPos, o->Angle, vLight, 1);
    }
}

void SessionVisualUnit::EmitChristmasFormVisual(OBJECT &object, bool finished, float priorFrame)
{
    auto *o = &object;

    if (o->CurrentAction == PLAYER_SANTA_1)
    {
        for (float frame : {4.f, 8.f, 12.f})
        {
            if (o->AnimationFrame < frame ||
                (o->AnimationFrame >= frame + 1.f && priorFrame >= frame + 1.f))
                continue;
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                int iEffectType = WorldRandom() % 4 + MODEL_XMAS_EVENT_BOX;
                vec3_t vPos;
                vec3_t vLight;
                Vector(1.f, 1.f, 1.f, vLight);
                const float fraction = birth.FrameFraction();
                o->MotionTrace.Sample(WorldTime, fraction, o->Position, vPos);
                vPos[2] += 230.f;
                for (int i = 0; i < 2; ++i)
                {
                    CreateEffect(iEffectType, vPos, o->Angle, o->Light);
                    CreateParticle(BITMAP_SPARK + 1, vPos, o->Angle, o->Light, 20, 1.f);
                }
                for (int i = 0; i < 5; ++i)
                {
                    CreateParticle(BITMAP_SPARK + 1, vPos, o->Angle, vLight, 20, 1.f);
                }
            }
        }
    }
    else if (o->CurrentAction == PLAYER_SANTA_2)
    {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            vec3_t position, light{1.f, 1.f, 1.f};
            const float fraction = birth.FrameFraction();
            o->MotionTrace.Sample(WorldTime, fraction, o->Position, position);
            position[2] += 230.f;
            CreateParticle(BITMAP_SPARK + 1, position, o->Angle, light, 20, 1.f);
            CreateParticle(BITMAP_SMOKE, position, o->Angle, light, 4, 2.f);
            if (sessionKeeper_.Random()->FpsCheck(6, 1.f))
                CreateParticle(BITMAP_SNOW_EFFECT_1, position, o->Angle, light, 0, 0.3f);
            if (sessionKeeper_.Random()->FpsCheck(3, 1.f))
                CreateParticle(BITMAP_SNOW_EFFECT_2, position, o->Angle, light, 0, 0.5f);
        }

        if (finished)
        {
            DeleteEffect(MODEL_XMAS_EVENT_ICEHEART, NULL);
            DeleteParticle(BITMAP_DS_EFFECT);
            DeleteParticle(BITMAP_LIGHT);
        }
    }
}

void SessionVisualUnit::EmitCharacterFormVisual(OBJECT &object)
{
    auto *o = &object;
    if (o->Kind != KIND_PLAYER || o->Type != MODEL_PLAYER)
        return;
    auto *b = &Models[o->Type];
    switch (o->SubType)
    {
    case MODEL_CURSEDTEMPLE_ALLIED_PLAYER:
    case MODEL_CURSEDTEMPLE_ILLUSION_PLAYER: {
        if (!g_isCharacterBuff(o, eDeBuff_CursedTempleRestraint) &&
            !g_isCharacterBuff(o, eBuff_CursedTempleProdection))
            break;
        vec3_t vRelativePos, vtaWorldPos, Light;
        Vector(0.4f, 0.4f, 0.8f, Light);
        Vector(0.f, 0.f, 0.f, vRelativePos);
        const int boneindex[15] = {1, 2, 17, 18, 19, 20, 44, 25, 27, 34, 37, 3, 5, 10, 12};

        for (int bone : boneindex)
        {
            b->TransformByObjectBone(vtaWorldPos, o, bone, vRelativePos);
            CreateSprite(BITMAP_LIGHT, vtaWorldPos, 1.3f, Light, o);
        }
        break;
    }
    case MODEL_PANDA: {
        vec3_t Light;
        vec3_t vPos;
        VectorCopy(o->Position, vPos);

        Vector(1.f, 0.6f, 0.2f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 100.f))
        {
            const float fraction = birth.FrameFraction();
            o->MotionTrace.Sample(WorldTime, fraction, o->Position, vPos);
            CreateEffect(MODEL_ARROWSRE06, vPos, o->Angle, Light, 2, o, 0, 0, 0, 0, 1.f);
            CreateEffect(MODEL_ARROWSRE06, vPos, o->Angle, Light, 2, o, 1, 0, 0, 0, 1.f);
        }

        break;
    }
    case MODEL_GM_CHARACTER: {
        vec3_t vRelativePos, vPos, vLight, vLight2;
        float fLight;
        fLight = sinf(WorldTime * 0.08f) * 0.2f + 0.2f;

        Vector(0.f, 0.3f, 1.2f, vLight);
        Vector(0.3f + fLight, 0.f + fLight, 0.1f + fLight, vLight2);

        Vector(0.f, 0.f, -7.5f, vRelativePos);
        b->TransformByObjectBone(vPos, o, 26, vRelativePos);
        CreateSprite(BITMAP_SHINY + 1, vPos, 0.9f, vLight, o, -WorldTime * 0.08f);
        CreateSprite(BITMAP_LIGHT + 3, vPos, 0.8f, vLight2, o, WorldTime * 0.3f);

        Vector(0.f, 0.f, 7.5f, vRelativePos);
        b->TransformByObjectBone(vPos, o, 35, vRelativePos);
        CreateSprite(BITMAP_SHINY + 1, vPos, 0.9f, vLight, o, WorldTime * 0.08f);
        CreateSprite(BITMAP_LIGHT + 3, vPos, 0.8f, vLight2, o, -WorldTime * 0.3f);

        Vector(-5.f, 2.5f, 0.f, vRelativePos);
        b->TransformByObjectBone(vPos, o, 5, vRelativePos);
        CreateSprite(BITMAP_SHINY + 1, vPos, 0.9f, vLight, o, -WorldTime * 0.08f);
        CreateSprite(BITMAP_LIGHT + 3, vPos, 0.8f, vLight2, o, WorldTime * 0.3f);

        b->TransformByObjectBone(vPos, o, 12, vRelativePos);
        CreateSprite(BITMAP_SHINY + 1, vPos, 0.9f, vLight, o, -WorldTime * 0.08f);
        CreateSprite(BITMAP_LIGHT + 3, vPos, 0.8f, vLight2, o, WorldTime * 0.3f);
        break;
    }
    default:
        break;
    }
}

void SessionVisualUnit::AdvanceCharacterGradeItems(CHARACTER &character,
                                                   WorldCharacterVisualState &visual, bool advance)
{
    for (int k = 1; k < MAX_BODYPART; ++k)
    {
        auto part = character.BodyPart[k];
        if (part.Type == -1 || part.Level != 15)
            continue;
        int bornIndex[2]{}, gradeType[2]{};
        switch (k)
        {
        case 1: {
            bornIndex[0] = 20;
            bornIndex[1] = -1;
            gradeType[0] = MODEL_15GRADE_ARMOR_OBJ_HEAD;
            gradeType[1] = -1;
        }
        break;
        case 2: {
            bornIndex[0] = 35;
            bornIndex[1] = 26;
            gradeType[0] = MODEL_15GRADE_ARMOR_OBJ_BODYLEFT;
            gradeType[1] = MODEL_15GRADE_ARMOR_OBJ_BODYRIGHT;
        }
        break;
        case 3: {
            bornIndex[0] = 3;
            bornIndex[1] = 10;
            gradeType[0] = MODEL_15GRADE_ARMOR_OBJ_PANTLEFT;
            gradeType[1] = MODEL_15GRADE_ARMOR_OBJ_PANTRIGHT;
        }
        break;
        case 4: {
            bornIndex[0] = 36;
            bornIndex[1] = 27;
            gradeType[0] = MODEL_15GRADE_ARMOR_OBJ_ARMLEFT;
            gradeType[1] = MODEL_15GRADE_ARMOR_OBJ_ARMRIGHT;
        }
        break;
        case 5: {
            bornIndex[0] = 4;
            bornIndex[1] = 11;
            gradeType[0] = MODEL_15GRADE_ARMOR_OBJ_BOOTLEFT;
            gradeType[1] = MODEL_15GRADE_ARMOR_OBJ_BOOTRIGHT;
        }
        break;
        default:
            bornIndex[0] = -1;
            bornIndex[1] = -1;
            gradeType[0] = -1;
            gradeType[1] = -1;
            break;
        }

        for (int side = 0; side < 2; ++side)
        {
            if (gradeType[side] == -1)
                continue;
            part.LinkBone = bornIndex[side];
            AdvanceLinkedItemVisual(character, visual, advance,
                                    CharacterLinkedItemVisual::GradeHead + (k - 1) * 2 + side, part,
                                    gradeType[side], true, true);
        }
    }
}

void SessionVisualUnit::ConfigureCharacterPartCloth(OBJECT &object, int Type,
                                                    CPhysicsClothMesh &cloth)
{
    auto *b = &Models[Type];
    switch (Type)
    {
    case MODEL_GRAND_SOUL_PANTS:
        cloth.Create(&object, 2, 17, 0.0f, 9.0f, 7.0f, 5, 8, 45.0f, 85.0f, BITMAP_PANTS_G_SOUL,
                     BITMAP_PANTS_G_SOUL,
                     PCT_MASK_ALPHA | PCT_HEAVY | PCT_STICKED | PCT_SHORT_SHOULDER, Type);
        cloth.AddCollisionSphere(0.0f, -15.0f, -20.0f, 30.0f, 2);
        break;
    case MODEL_DIVINE_PANTS:
        cloth.Create(&object, 3, 2, PCT_OPT_CORRECTEDFORCE | PCT_HEAVY, Type);
        cloth.AddCollisionSphere(0.0f, 0.0f, -15.0f, 22.0f, 2);
        cloth.AddCollisionSphere(0.0f, 0.0f, -27.0f, 23.0f, 2);
        cloth.AddCollisionSphere(0.0f, 0.0f, -40.0f, 24.0f, 2);
        cloth.AddCollisionSphere(0.0f, 0.0f, -54.0f, 25.0f, 2);
        cloth.AddCollisionSphere(0.0f, 0.0f, -69.0f, 26.0f, 2);
        break;
    case MODEL_DARK_SOUL_PANTS:
        cloth.Create(&object, 2, 17, 0.0f, 9.0f, 7.0f, 7, 5, 50.0f, 100.0f,
                     b->IndexTexture[b->Meshs[2].Texture], b->IndexTexture[b->Meshs[2].Texture],
                     PCT_MASK_ALPHA | PCT_HEAVY | PCT_STICKED | PCT_SHORT_SHOULDER, Type);
        cloth.AddCollisionSphere(0.0f, -15.0f, -20.0f, 30.0f, 2);
        break;
    }
}

void SessionVisualUnit::AdvanceCharacterPartCloth(CHARACTER &character,
                                                  WorldCharacterVisualState &visual, bool advance)
{
    const int type = character.BodyPart[BODYPART_PANTS].Type;
    if (!WorldObjectDetail::HasCharacterPartCloth(type))
    {
        visual.partCloth.reset();
        return;
    }
    if (visual.partCloth && visual.partCloth->shape[0] != type)
        visual.partCloth.reset();
    if (!advance && visual.partCloth)
        return;
    auto &model = Models[type];
    if (model.NumBones == 0 || model.NumActions == 0)
        return;
    PrepareCharacterModel(character);
    auto &object = character.Object;
    model.BodyScale = object.Scale;
    model.BodyHeight = 0.f;
    model.CurrentAction = character.WorldVisualAction;
    VectorCopy(object.Position, model.BodyOrigin);
    vec3_t minimum{}, maximum{};
    OBB_t bounds;
    model.Transform(CharacterPresentationInput(character).object.bones, minimum, maximum, &bounds,
                    true);
    if (!visual.partCloth)
    {
        visual.partCloth = std::make_unique<CharacterClothVisual>(
            sessionKeeper_, CharacterClothVisual::Kind::Equipment, 1, true);
        visual.partCloth->shape[0] = type;
        ConfigureCharacterPartCloth(object, type,
                                    *static_cast<CPhysicsClothMesh *>(visual.partCloth->pieces));
    }
    visual.partCloth->pieces->SetPose(CharacterPresentationInput(character).object.bones,
                                      &character.WorldVisualPoseSample, &character.Object);
    if (advance && !visual.partCloth->pieces->Move2(0.005f, 5, FPS_ANIMATION_FACTOR, WorldTime))
        visual.partCloth.reset();
}

void CSPetDarkSpirit::AdvanceMotion(float frames)
{
    vec3_t standTarget{};
    if (m_PetCharacter.Object.AI == PET_STAND_START)
    {
        vec3_t offset{};
        PetSystemDetail::OwnerBonePosition(m_PetOwner->Object, 42, offset, standTarget);
    }
    if (frames <= 0.f)
    {
        AdvanceMotionStep(0.f, standTarget, 1.f);
        return;
    }
    constexpr float MaximumSteeringStep = 1.f / 16.f, AttackEndFrame = 22.f;
    auto &object = m_PetCharacter.Object;
    const float totalFrames = frames;
    object.MotionTrace.Begin(WorldTime, totalFrames, object.Position);

    while (frames > 0.f)
    {
        const int previousAi = object.AI;
        const bool steering = object.AI != PET_STAND && object.AI != PET_ATTACK_MAGIC;
        float step = steering ? (std::min)(frames, MaximumSteeringStep) : frames;
        if (object.AI == PET_ATTACK)
            step = (std::min)(step, (std::max)(0.f, AttackEndFrame - object.LifeTime));
        if (object.AI == PET_ESCAPE)
            step = (std::min)(step, (std::max)(0.f, object.Velocity));
        if ((object.AI == PET_FLY || object.AI == PET_FLYING) && flightNoiseFrames_ > 0.f)
            step = (std::min)(step, flightNoiseFrames_);
        if (object.AI == PET_ATTACK)
        {
            vec3_t target;
            const OBJECT &source = object.m_bActionStart ? m_PetTarget->Object : m_PetOwner->Object;
            source.MotionTrace.Sample(WorldTime, (totalFrames - frames) / totalFrames,
                                      source.Position, target);
            target[2] += 50.f;
            step = PetSystemDetail::PetTargetStep(object, target, 20.f, step, [&](float duration) {
                return PetSystemDetail::PetRushDistance(object, duration);
            });
        }
        else if (object.AI == PET_STAND_START)
        {
            vec3_t offset{};
            PetSystemDetail::SampleOwnerBonePosition(
                *m_PetOwner, Models[m_PetOwner->Object.Type], 42, offset, WorldTime,
                (totalFrames - frames) / totalFrames, standTarget);
            step = PetSystemDetail::PetTargetStep(
                object, standTarget, 50.f, step, [&](float duration) {
                    return duration * (object.Velocity + (duration - 1.f) * 0.5f);
                });
        }
        const float previousYaw = object.Angle[2];
        AdvanceMotionStep(step, standTarget, (totalFrames - frames + step) / totalFrames);
        object.MotionTrace.TurnYaw(step, previousYaw, object.Angle[2], 0.f);
        object.MotionTrace.Advance(step, object.Position);
        if (previousAi != object.AI)
        {
            const int action = object.AI == PET_STAND                                    ? 2
                               : object.AI == PET_FLYING || object.AI == PET_STAND_START ? 1
                               : object.AI == PET_ATTACK || object.AI == PET_ESCAPE      ? 3
                                                                                         : 0;
            SetAction(&object, action);
        }
        frames -= step;
    }
}

void CSPetDarkSpirit::AdvanceMotionStep(float frames, const vec3_t standTarget, float frameFraction)
{
    bool Play;
    CHARACTER *c = &m_PetCharacter;
    OBJECT *o = &c->Object;
    OBJECT *Owner = &m_PetOwner->Object;
    vec3_t ownerPosition;
    Owner->MotionTrace.Sample(WorldTime, frameFraction, Owner->Position, ownerPosition);
    Play = PlayAnimation(o, frames);
    if (Play == false)
    {
        switch (o->AI)
        {
        case PET_FLY:
            SetAction(o, 0);
            break;
        case PET_FLYING:
            SetAction(o, 1);
            break;
        case PET_STAND:
            SetAction(o, 2);
            break;
        case PET_STAND_START:
            SetAction(o, 1);
            break;
        case PET_ATTACK:
        case PET_ESCAPE:
            SetAction(o, 3);
            break;
        default:
            SetAction(o, 0);
            break;
        }
    }

    vec3_t Range, TargetPosition;
    float FlyRange = 150.f;

    if (o->m_bActionStart == true)
    {
        OBJECT *to = &m_PetTarget->Object;

        to->MotionTrace.Sample(WorldTime, frameFraction, to->Position, TargetPosition);
        VectorSubtract(TargetPosition, o->Position, Range);
    }
    else
    {
        VectorCopy(ownerPosition, TargetPosition);
        VectorSubtract(TargetPosition, o->Position, Range);
    }

    if (o->AI == PET_FLY || o->AI == PET_FLYING)
    {
        o->m_bActionStart = false;

        float Distance = Range[0] * Range[0] + Range[1] * Range[1];
        if (frames > 0.f && flightNoiseFrames_ <= 0.f)
        {
            flightNoiseFrames_ = 1.f;
            flightTurnRate_ =
                Distance >= FlyRange * FlyRange ? Random.RangeFloat(0, 14) + 5.f : 0.f;
        }

        if (Distance >= FlyRange * FlyRange)
        {
            float Angle = CreateAngle2D(o->Position, TargetPosition);
            o->Angle[2] = TurnAngle2(o->Angle[2], Angle, flightTurnRate_ * frames);
        }
        AngleMatrix(o->Angle, o->Matrix);

        vec3_t Direction;
        VectorRotate(o->Direction, o->Matrix, Direction);
        VectorAddScaled(o->Position, Direction, o->Position, frames);

        int speedRandom = 28;
        int CharacterHeight = 250;

        if (m_PetOwner->Helper.Type == ITEM_DARK_HORSE_ITEM ||
            gMapManager.ContextMap() == WD_55LOGINSCENE)
        {
            CharacterHeight = 350;
        }

        float Height = TargetPosition[2] + CharacterHeight;
        if (o->Position[2] < Height)
        {
            speedRandom = 10;
            o->Angle[0] -= (2.f) * frames;
            if (o->Angle[0] < -15.f)
                o->Angle[0] = -15.f;
        }
        else if (o->Position[2] > Height + 100)
        {
            speedRandom = 20;
            o->Angle[0] += (2.f) * frames;
            if (o->Angle[0] > 15.f)
                o->Angle[0] = 15.f;
        }

        float Speed = 0;
        flightNoiseFrames_ -= frames;
        if (frames > 0.f && flightNoiseFrames_ <= 0.f && Random.FpsCheck(speedRandom, 1.f))
        {
            if (Distance >= FlyRange * FlyRange)
            {
                Speed = -(Random.RangeFloat(0, 63) + 128.f) * 0.1f;
            }
            else
            {
                Speed = -(Random.RangeFloat(0, 7) + 32.f) * 0.1f;
                o->Angle[2] += Random.RangeFloat(0, 59);
            }

            Speed += o->Direction[1];
            Speed = Speed / 2.f;

            o->Direction[0] = 0.f;
            o->Direction[1] = Speed;
            o->Direction[2] = Random.RangeFloat(-32, 31) * 0.1f;
        }

        if (o->Direction[1] < -12.f)
        {
            if (o->AI != PET_FLYING)
            {
                SetAI(PET_FLYING);
            }
        }
        else if (o->AI != PET_FLY)
        {
            SetAI(PET_FLY);
        }
    }
    else if (o->AI == PET_ATTACK || o->AI == PET_ESCAPE)
    {
        const bool attacking = o->AI == PET_ATTACK;
        const float travel = PetSystemDetail::AdvancePetRush(*o, frames);
        vec3_t local, movement;
        AngleMatrix(o->Angle, o->Matrix);
        Vector(0.f, -travel, 0.f, local);
        VectorRotate(local, o->Matrix, movement);
        VectorAdd(o->Position, movement, o->Position);
        if (attacking)
        {
            TargetPosition[2] += 50.f;
            const float distance =
                ::MoveHumming(o->Position, o->Angle, TargetPosition, (std::max)(0.f, travel));
            o->LifeTime += frames;
            if (distance <= 20.f + PetSystemDetail::PetContactTolerance || o->LifeTime >= 22.f)
            {
                SetAI(PET_ESCAPE);
                o->Angle[0] = -45.f;
                if (m_byCommand != PET_CMD_TARGET)
                    o->m_bActionStart = false;
            }
        }
        else
        {
            VectorSubtract(TargetPosition, o->Position, Range);
            const float distance = Range[0] * Range[0] + Range[1] * Range[1];
            if (distance >= (FlyRange + 100.f) * (FlyRange + 100.f) || o->Velocity <= 0.f)
                SetAI(PET_FLYING);
        }
        SetAction(o, 3);
    }
    else if (o->AI == PET_ATTACK_MAGIC)
    {
        if (c->TargetCharacter != -1)
        {
            CHARACTER *tc = &CharactersClient[c->TargetCharacter];
            OBJECT *to = &tc->Object;

            vec3_t targetPosition;
            to->MotionTrace.Sample(WorldTime, frameFraction, to->Position, targetPosition);
            float Angle = CreateAngle2D(o->Position, targetPosition);
            o->Angle[2] = TurnAngle2(o->Angle[2], Angle, 40.f * frames);
        }
    }
    else if (o->AI == PET_STAND)
    {
        vec3_t p;

        Vector(-10.f, 0.f, 10.f, p);
        PetSystemDetail::SampleOwnerBonePosition(*m_PetOwner, Models[Owner->Type], 37, p, WorldTime,
                                                 frameFraction, o->Position);
        VectorCopy(Owner->Angle, o->Angle);
        o->Angle[2] = Owner->MotionTrace.SampleYaw(WorldTime, frameFraction, Owner->Angle[2]);
        o->Angle[2] -= 120.f;
    }
    else if (o->AI == PET_STAND_START)
    {
        vec3_t p, Pos;

        const float travel = frames * (o->Velocity + (frames - 1.f) * 0.5f);
        AngleMatrix(o->Angle, o->Matrix);
        Vector(0.f, -travel, 0.f, p);
        VectorRotate(p, o->Matrix, Pos);
        VectorAdd(o->Position, Pos, o->Position);
        VectorCopy(standTarget, Pos);

        float Distance = ::MoveHumming(o->Position, o->Angle, Pos, (std::max)(0.f, travel));
        o->Velocity += frames;
        if (Distance <= 50.f + PetSystemDetail::PetContactTolerance)
        {
            SetAI(PET_STAND);
        }
    }
    if (o->AI >= PET_ATTACK && o->AI <= PET_ATTACK_MAGIC)
    {
        c->AttackTime += frames;
        if (c->AttackTime >= 15)
        {
            c->AttackTime = 15;
        }
    }
    PetSystemDetail::RescueDistantPet(*o, ownerPosition, frames);
}

void CSPetDarkSpirit::AdvancePresentation(bool emit, bool forceRender)
{
    auto *c = &m_PetCharacter;
    auto *o = &c->Object;
    if (!o->Live)
        return;
    o->WeaponLevel = static_cast<std::uint8_t>(petLevel_ & 0xFF);
    const bool inFrustum =
        forceRender || TestFrustrum2D(o->Position[0] * 0.01f, o->Position[1] * 0.01f, -20.f);
    o->Visible = inFrustum && (forceRender || sessionKeeper_.Display()->IsVisible());
    if (!o->Visible)
        return;
    auto &model = Models[o->Type];
    model.BodyScale = o->Scale;
    model.BodyHeight = 0.f;
    model.CurrentAction = o->CurrentAction;
    VectorCopy(o->Position, model.BodyOrigin);
    AnimationPoseSample sample(o, model.BoneHead, 0.f, true, model.PoseAssetIdentity());
    if (poseSample_ != sample)
    {
        sample.Evaluate(model, o->BoneTransform);
        poseSample_ = sample;
    }
    vec3_t minimum{}, maximum{};
    OBB_t bounds;
    model.Transform(o->BoneTransform, minimum, maximum, &bounds, false);
    SessionRandom::PresentationScope presentation(Random);
    if (emit)
        AttackEffect(c, o);
}

CSPetSystem *SessionVisualUnit::FindPetSystem(CHARACTER &character)
{
    const auto *visual = FindCharacterVisual(character);
    return visual ? visual->darkSpirit.get() : nullptr;
}

void SessionVisualUnit::AdmitCharacterPet(CHARACTER &character)
{
    const int index = CharactersClient.FindIndexByKey(character.Key);
    if (index >= 0 && &CharactersClient[index] == &character)
        AdvanceCharacterPet(character, CharactersClient.WorldVisuals(index), false);
}

void SessionVisualUnit::AdvanceCharacterPet(CHARACTER &character, WorldCharacterVisualState &visual,
                                            bool advance, bool forceRender)
{
    const auto &state = character.PetCommands;
    if (!state.present || gMapManager.InChaosCastle())
    {
        if (visual.darkSpirit && visual.attachmentSprites)
            visual.attachmentSprites->Clear();
        visual.darkSpirit.reset();
        visual.petGeneration = state.generation;
        return;
    }
    SessionRandom::PresentationScope presentation(sessionKeeper_.RandomForConstruction());
    if (visual.petGeneration != state.generation)
    {
        if (visual.darkSpirit && visual.attachmentSprites)
            visual.attachmentSprites->Clear();
        visual.darkSpirit.reset();
        visual.petGeneration = state.generation;
        visual.petCommandRevision = visual.petAttackRevision = 0;
    }
    const bool admitted = !visual.darkSpirit;
    if (admitted)
    {
        if (Models[MODEL_DARK_SPIRIT].NumBones == 0 || Models[MODEL_DARK_SPIRIT].NumActions == 0)
            return;
        visual.darkSpirit = std::make_unique<CSPetDarkSpirit>(sessionKeeper_, &character);
    }
    auto &pet = *visual.darkSpirit;
    pet.SetPetLevel(state.level);
    ApplyCharacterPetCommands(character, visual);
    if (advance)
        pet.MovePet(forceRender);
    else if (admitted)
        pet.AdvancePresentation(false, forceRender);
}

void SessionVisualUnit::ApplyCharacterPetCommands(CHARACTER &character,
                                                  WorldCharacterVisualState &visual)
{
    const auto &state = character.PetCommands;
    auto &pet = *visual.darkSpirit;
    enum class TargetStatus
    {
        Waiting,
        Ready,
        Expired
    };
    const auto targetStatus = [&](int key,
                                  const std::shared_ptr<const CharacterSocketSource> &source) {
        if (!source || !source->object)
            return TargetStatus::Expired;
        const int index = CharactersClient.FindIndexByKey(key);
        if (index < 0)
            return TargetStatus::Waiting;
        return CharactersClient[index].SocketSource == source ? TargetStatus::Ready
                                                              : TargetStatus::Expired;
    };
    const auto command = [&] {
        if (visual.petCommandRevision == state.commandRevision)
            return;
        const bool superseded = state.commandRevision < visual.petAttackRevision;
        const auto target = state.command == PET_CMD_TARGET
                                ? targetStatus(state.commandTarget, state.commandTargetSource)
                                : TargetStatus::Ready;
        if (!superseded && target == TargetStatus::Waiting)
            return;
        if (!superseded && target == TargetStatus::Ready)
            pet.SetCommand(state.commandTarget, state.command);
        visual.petCommandRevision = state.commandRevision;
    };
    const auto attack = [&] {
        if (visual.petAttackRevision == state.attackRevision)
            return;
        const bool superseded = state.attackRevision < visual.petCommandRevision;
        const auto target = targetStatus(state.attackTarget, state.attackTargetSource);
        if (!superseded && target == TargetStatus::Waiting)
            return;
        if (!superseded && target == TargetStatus::Ready)
            pet.SetAttack(state.attackTarget, state.attackType);
        visual.petAttackRevision = state.attackRevision;
    };
    if (state.commandRevision < state.attackRevision)
    {
        command();
        attack();
    }
    else
    {
        attack();
        command();
    }
}
#ifdef _PVP_ADD_MOVE_SCROLL
extern CMurdererMove g_MurdererMove;
#endif // _PVP_ADD_MOVE_SCROLL

BOOL Util_CheckOption(std::wstring lpszCommandLine, wchar_t cOption, std::wstring &lpszString);

extern void StopMusic();

void SessionGameplayUnit::SetPlayerBow(CHARACTER *c)
{
    OBJECT *o = &c->Object;

    if (o->Type != MODEL_PLAYER || gCharacterManager.GetBaseClass(c->Class) != CLASS_ELF ||
        c->SafeZone)
        return;

    SetAttackSpeed();

    switch (gCharacterManager.GetEquipedBowType(c))
    {
    case BOWTYPE_BOW: {
        if (c->Helper.Type == MODEL_HORN_OF_FENRIR)
        {
            SetAction(&c->Object, PLAYER_FENRIR_ATTACK_BOW);
        }
        else if ((c->Helper.Type == MODEL_HORN_OF_UNIRIA) ||
                 (c->Helper.Type == MODEL_HORN_OF_DINORANT))
        {
            SetAction(&c->Object, PLAYER_ATTACK_RIDE_BOW);
        }
        else if (c->Wing.Type != -1)
        {
            SetAction(&c->Object, PLAYER_ATTACK_FLY_BOW);
        }
        else
        {
            SetAction(&c->Object, PLAYER_ATTACK_BOW);
        }
    }
    break;
    case BOWTYPE_CROSSBOW: {
        if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone)
        {
            SetAction(&c->Object, PLAYER_FENRIR_ATTACK_CROSSBOW);
        }
        else if ((c->Helper.Type == MODEL_HORN_OF_UNIRIA) ||
                 (c->Helper.Type == MODEL_HORN_OF_DINORANT))
        {
            SetAction(&c->Object, PLAYER_ATTACK_RIDE_CROSSBOW);
        }
        else if (c->Wing.Type != -1)
        {
            SetAction(&c->Object, PLAYER_ATTACK_FLY_CROSSBOW);
        }
        else
        {
            SetAction(&c->Object, PLAYER_ATTACK_CROSSBOW);
        }
    }
    break;
    }
}

void SessionLegacyCalls::SetPlayerBow(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->SetPlayerBow(character);
}

void SessionGameplayUnit::SetPlayerHighBow(CHARACTER *c)
{
    switch (gCharacterManager.GetEquipedBowType(c))
    {
    case BOWTYPE_BOW: {
        if (c->Helper.Type == MODEL_HORN_OF_FENRIR)
        {
            SetAction(&c->Object, PLAYER_ATTACK_RIDE_BOW_UP);
        }
        else if ((c->Helper.Type == MODEL_HORN_OF_UNIRIA) ||
                 (c->Helper.Type == MODEL_HORN_OF_DINORANT))
        {
            SetAction(&c->Object, PLAYER_ATTACK_RIDE_BOW_UP);
        }
        else if (c->Wing.Type != -1)
        {
            SetAction(&c->Object, PLAYER_ATTACK_FLY_BOW_UP);
        }
        else
        {
            SetAction(&c->Object, PLAYER_ATTACK_BOW_UP);
        }
    }
    break;
    case BOWTYPE_CROSSBOW: {
        if (c->Helper.Type == MODEL_HORN_OF_FENRIR && !c->SafeZone)
        {
            SetAction(&c->Object, PLAYER_ATTACK_RIDE_CROSSBOW_UP);
        }
        else if ((c->Helper.Type == MODEL_HORN_OF_UNIRIA) ||
                 (c->Helper.Type == MODEL_HORN_OF_DINORANT))
        {
            SetAction(&c->Object, PLAYER_ATTACK_RIDE_CROSSBOW_UP);
        }
        else if (c->Wing.Type != -1)
        {
            SetAction(&c->Object, PLAYER_ATTACK_FLY_CROSSBOW_UP);
        }
        else
        {
            SetAction(&c->Object, PLAYER_ATTACK_CROSSBOW_UP);
        }
    }
    break;
    }
}

void SessionLegacyCalls::SetPlayerHighBow(CHARACTER *character)
{
    sessionKeeper_.Gameplay()->SetPlayerHighBow(character);
}

void SessionVisualUnit::AdvanceCharacterMount(CHARACTER &character,
                                              WorldCharacterVisualState &visual, bool advance)
{
    const auto &state = character.MountState;
    if (visual.mount &&
        (!TheMapProcess().CharacterPolicy().mounts || state.type != visual.mount->object.Type ||
         state.generation != visual.mount->generation))
    {
        OBJECT *targets[]{&visual.mount->object};
        gameplay_.RetireCharacterEffectTargets(targets);
        visual.mount.reset();
        if (!advance && visual.attachmentSprites)
            visual.attachmentSprites->Clear();
    }
    if (state.type == -1 || !TheMapProcess().CharacterPolicy().mounts)
        return;
    SessionRandom::PresentationScope presentation(sessionKeeper_.RandomForConstruction());
    if (!visual.mount)
    {
        const auto &model = Models[state.type];
        if (model.NumBones == 0 || model.NumActions == 0)
            return;
        visual.mount = std::make_unique<CharacterMountVisual>(model.NumBones);
        visual.mount->generation = state.generation;
        vec3_t position;
        VectorCopy(state.position, position);
        sessionKeeper_.Gameplay()->CreateMountSub(state.type, position, &character.Object,
                                                  &visual.mount->object, state.subType,
                                                  state.linkBone);
        visual.mount->object.EnableBoneMatrix = true;
    }
    if (advance)
        sessionKeeper_.Gameplay()->MoveMount(&visual.mount->object, false, &character,
                                             &visual.mount->poseSample_);
    else if (visual.mount->poseRevision != character.WorldVisualPoseRevision)
    {
        sessionKeeper_.Gameplay()->SynchronizeMount(visual.mount->object);
        sessionKeeper_.Gameplay()->PrepareMountPose(visual.mount->object,
                                                    &visual.mount->poseSample_);
    }
    visual.mount->poseRevision = character.WorldVisualPoseRevision;
}

void SessionVisualUnit::AdvanceFenrirVisual(OBJECT &object, float previousFrame)
{
    vec3_t light;
    int subType;
    switch (object.Type)
    {
    case MODEL_FENRIR_RED:
        Vector(0.8f, 0.f, 0.f, light);
        subType = 1;
        break;
    case MODEL_FENRIR_BLUE:
        Vector(0.1f, 0.1f, 0.8f, light);
        subType = 2;
        break;
    case MODEL_FENRIR_BLACK:
        Vector(1.f, 1.f, 0.2f, light);
        subType = 3;
        break;
    case MODEL_FENRIR_GOLD:
        Vector(0.8f, 0.8f, 0.1f, light);
        subType = 4;
        break;
    default:
        return;
    }
    auto &model = Models[object.Type];
    const auto *bones = object.BoneTransform ? object.BoneTransform : BoneTransform;
    if (object.CurrentAction == FENRIR_ATTACK_SKILL)
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            vec3_t offset, position, sparkLight{1.f, 0.f, 0.f};
            Vector(static_cast<float>(WorldRandom() % 10 - 10) * 0.5f, 0.f,
                   static_cast<float>(WorldRandom() % 40 - 20) * 0.5f, offset);
            constexpr int jawBone = 14;
            const float fraction = birth.FrameFraction();
            AnimationPoseSample pose(&object, model.BoneHead, model.BodyHeight, false,
                                     model.PoseAssetIdentity());
            pose.SampleBonePosition(model, object, jawBone, offset, WorldTime, fraction, position);
            const float luminosity = sinf(WorldTime * 0.002f) * 0.2f;
            CreateParticle(BITMAP_SPARK + 1, position, object.Angle, sparkLight, 15,
                           0.7f + luminosity * 0.05f);
        }
    for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
    {
        const float fraction = birthTime.FrameFraction();
        vec3_t position, angle;
        object.MotionTrace.Sample(WorldTime, fraction, object.Position, position);
        VectorCopy(object.Angle, angle);
        angle[2] = object.MotionTrace.SampleYaw(WorldTime, fraction, angle[2]);
        CreateEffect(MODEL_FENRIR_THUNDER, position, angle, light, 0, &object);
        CreateEffect(MODEL_FENRIR_THUNDER, position, angle, light, 0, &object);
    }
    if (object.AnimationFrame < previousFrame)
        previousFrame = 0.f;
    const auto crosses = [&](float lower, float upper) {
        return object.AnimationFrame > lower &&
               (object.AnimationFrame <= upper || previousFrame < upper);
    };
    const bool walking =
        object.CurrentAction == FENRIR_WALK && (object.AnimationFrame == 0.f || crosses(0.f, 1.5f));
    const bool running = object.CurrentAction == FENRIR_RUN;
    constexpr int footBones[]{22, 28, 36, 44};
    for (int index = 0; index < 4; ++index)
    {
        if (!walking && !(running && (index < 2 ? crosses(1.f, 1.4f) : crosses(4.8f, 5.2f))))
            continue;
        vec3_t position, origin{}, white{1.f, 1.f, 1.f};
        model.TransformPosition(bones[footBones[index]], origin, position, false);
        CreateEffect(MODEL_FENRIR_FOOT_THUNDER, position, object.Angle, white, subType, &object);
    }
}

void SessionVisualUnit::AdvanceDarkHorseSkill(OBJECT *o, BMD *b, bool emit)
{
    if (!o || !b)
        return;
    constexpr float waveInterval = 10.f; // 400 ms in legacy 25 Hz ticks.
    constexpr float firstImpact = 19.f;
    constexpr float impactInterval = 22.f; // Original 19-frame cycle plus 3-frame pause.
    const float previous = o->HorseSkillTicks;
    const float current = previous + FPS_ANIMATION_FACTOR;
    o->HorseSkillTicks = current;
    if (!emit || FPS_ANIMATION_FACTOR <= 0.f)
        return;
    const int firstWave =
        previous == 0.f ? 0 : static_cast<int>(floorf(previous / waveInterval)) + 1;
    const int lastWave = static_cast<int>(floorf(current / waveInterval));
    for (int wave = firstWave; wave <= lastWave; ++wave)
    {
        const float remaining = current - wave * waveInterval;
        auto birth = sessionKeeper_.Gameplay()->EmissionTime(remaining);
        vec3_t position;
        o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, position);
        CreateEffect(BITMAP_SHOCK_WAVE, position, o->Angle, o->Light);
    }

    if (o->AnimationFrame >= 8.f && o->AnimationFrame <= 9.5f)
    {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
        {
            float matrix[3][4];
            vec3_t angle, offset, position;
            const float birthAge = current - birth.RemainingFrames();
            const float fraction = birth.FrameFraction();
            vec3_t origin;
            o->MotionTrace.Sample(WorldTime, fraction, o->Position, origin);
            const float cycle = fmodf(birthAge + 3.f, impactInterval) - 3.f;
            const float radius = 150.f * (std::max)(0.f, floorf(cycle / 2.f));
            Vector(0.f, radius, 0.f, offset);
            Vector(0.f, 0.f, static_cast<float>(WorldRandom() % 360), angle);
            for (int i = 0; i < 6; ++i)
            {
                angle[2] += 60.f;
                AngleMatrix(angle, matrix);
                VectorRotate(offset, matrix, position);
                VectorAdd(origin, position, position);
                CreateEffect(MODEL_GROUND_STONE + WorldRandom() % 2, position, o->Angle, o->Light);
            }
        }
        EarthQuake = (WorldRandom() % 3 - 3) * 0.7f;
    }
    else
    {
        const auto impactsThrough = [](float ticks) {
            return ticks < firstImpact
                       ? 0
                       : 1 + static_cast<int>(floorf((ticks - firstImpact) / impactInterval));
        };
        for (int impact = impactsThrough(previous); impact < impactsThrough(current); ++impact)
        {
            const float remaining = current - (firstImpact + impact * impactInterval);
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(remaining);
            vec3_t position;
            o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, position);
            CreateEffect(MODEL_SKILL_FURY_STRIKE, position, o->Angle, o->Light, 0, o, -1, 0, 2);
        }
    }
}

void SessionVisualUnit::AdvanceSkillEarthQuake(CHARACTER *c, OBJECT *o, BMD *b,
                                               WorldCharacterVisualState &visual, int iMaxSkill)
{
    if (c == NULL)
        return;
    if (o == NULL)
        return;
    if (b == NULL || FPS_ANIMATION_FACTOR <= 0.f)
        return;

    float Matrix[3][4];
    vec3_t Angle, p, Position;
    auto *target = visual.target.Resolve();
    if (!target)
        return;
    OBJECT &TargetO = target->Object;

    const float previous = visual.movement.weaponLevel;
    const float current = previous + FPS_ANIMATION_FACTOR;
    const float cycle = static_cast<float>(iMaxSkill + 1);
    constexpr float CrossingTolerance = 0.0001f;
    const int firstCycle = static_cast<int>(floorf(previous / cycle));
    const int lastCycle = static_cast<int>(floorf((current + CrossingTolerance) / cycle));
    for (int period = firstCycle; period <= lastCycle; ++period)
    {
        for (int stage = iMaxSkill - 2; stage <= iMaxSkill; ++stage)
        {
            const float eventTime = period * cycle + stage;
            if (eventTime <= previous + CrossingTolerance ||
                eventTime > current + CrossingTolerance)
                continue;
            const float remaining = (std::max)(0.f, current - eventTime);
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(remaining);
            vec3_t origin;
            TargetO.MotionTrace.Sample(WorldTime, birth.FrameFraction(), TargetO.Position, origin);
            Vector(0.f, 40.f * (stage / 2), 0.f, p);
            Vector(0.f, 0.f, static_cast<float>(WorldRandom() % 360), Angle);
            for (int i = 0; i < 6; ++i)
            {
                Angle[2] += 60.f;
                AngleMatrix(Angle, Matrix);
                VectorRotate(p, Matrix, Position);
                VectorAdd(origin, Position, Position);
                CreateEffect(MODEL_GROUND_STONE + 1, Position, TargetO.Angle, TargetO.Light);
            }
            if (stage == iMaxSkill - 1)
            {
                EarthQuake = (WorldRandom() % 3 - 3) * 0.7f;
                CreateEffect(MODEL_SKILL_FURY_STRIKE, origin, TargetO.Angle, TargetO.Light, 0, o,
                             -1, 0, 2);
            }
        }
    }
    visual.movement.weaponLevel = fmodf(current, cycle);
}

// Afterimage motion belongs to character presentation.
#pragma pack(push)
#pragma pack()
CDummyUnit::CDummyUnit(SessionKeeper &keeper) : gMapManager(keeper.MapManagerObject())
{
    Init();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
CDummyUnit::~CDummyUnit()
{
    Destroy();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CDummyUnit::Init(vec3_t Pos, vec3_t Target)
{
    Vector(0.0f, 0.0f, 0.0f, m_vPosition);
    Vector(0.0f, 0.0f, 0.0f, m_vStartPosition);
    Vector(0.0f, 0.0f, 0.0f, m_vTargetPosition);
    Vector(0.0f, 0.0f, 0.0f, m_vDirection);

    if (Pos != NULL)
    {
        VectorCopy(Pos, m_vPosition);
        VectorCopy(m_vPosition, m_vStartPosition);
    }
    if (Target != NULL)
    {
        VectorCopy(Target, m_vTargetPosition);
    }

    VectorSubtract(m_vTargetPosition, m_vStartPosition, m_vDirection);
    VectorNormalize(m_vDirection);

    m_fAniFrame = 0.0f;
    m_fDisFrame = 0.0f;
    m_fAlpha = 0.2f;

    m_fAniFrameSpeed = 30.0f;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CDummyUnit::Destroy()
{
    m_fAniFrame = 0.0f;
    m_fDisFrame = 0.0f;
    m_fAlpha = 0.2f;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool CDummyUnit::IsDistance(float animationFactor)
{
    vec3_t vDisPos;
    float fDis;
    VectorSubtract(m_vStartPosition, m_vPosition, vDisPos);
    fDis = VectorLength(vDisPos);

    if (fDis > 300)
    {
        if (m_fAlpha < 0)
        {
            m_fAlpha = 0.0f;
        }
        else
        {
            m_fAlpha -= 0.07f * animationFactor;
        }
    }
    else
    {
        if (m_fAlpha > 0.6f)
        {
            m_fAlpha = 0.6f;
        }
        else
        {
            m_fAlpha += 0.02f * animationFactor;
        }
    }

    if (fDis > MAX_DUMMYDISTANCE)
        return false;

    return true;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
vec_t *CDummyUnit::GetPosition()
{
    return m_vPosition;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
vec_t *CDummyUnit::GetStartPosition()
{
    return m_vStartPosition;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
float CDummyUnit::GetAniFrame(bool changed)
{
    return (gMapManager.InChaosCastle() || changed) ? m_fAniFrame * m_fAniFrameSpeed : m_fAniFrame;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
float CDummyUnit::GetAlpha()
{
    return m_fAlpha;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CDummyUnit::CalDummyPosition(vec3_t vOutPos, float &fAni, bool bChange, float animationFactor)
{
    m_fDisFrame += 0.7f * animationFactor;
    m_fAniFrame += 0.05f * animationFactor;
    if (m_fAniFrame >= 2.0f)
    {
        m_fAniFrame = 1.9f;
    }
    fAni = GetAniFrame(bChange);
    float _fDisFrame = m_fDisFrame * 0.7f;
    if (gMapManager.InChaosCastle() || bChange)
    {
        _fDisFrame *= m_fAniFrameSpeed;
    }
    vec3_t step;
    VectorScale(m_vDirection, _fDisFrame, step);
    VectorAdd(m_vPosition, step, m_vPosition);
    VectorCopy(m_vPosition, vOutPos);
}
#pragma pack(pop)

// Common character-preview control.
#pragma pack(push)
#pragma pack()
void CUIPhotoViewer::RetirePhotoMount()
{
    if (!m_PhotoHelper.Live && !m_photoHelperBones)
        return;
    OBJECT *target = &m_PhotoHelper;
    sessionKeeper_.Gameplay()->RetireCharacterEffectTargets({&target, 1});
    m_PhotoHelper.BoneTransform = nullptr;
    m_photoHelperBones.reset();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUIPhotoViewer::PreparePhotoMount()
{
    if (!m_PhotoHelper.Live)
        return;
    const auto &model = Models[m_PhotoHelper.Type];
    if (model.NumBones == 0 || model.NumActions == 0)
        return;
    m_PhotoVisual.localMountPose = {};
    m_photoHelperBones = std::make_unique<vec34_t[]>(model.NumBones);
    m_PhotoHelper.BoneTransform = m_photoHelperBones.get();
    m_PhotoHelper.EnableBoneMatrix = true;
    gameplay_.SynchronizeMount(m_PhotoHelper);
    gameplay_.PrepareMountPose(m_PhotoHelper, &m_PhotoVisual.localMountPose);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUIPhotoViewer::AdvancePhotoCharacter()
{
    if (!m_bIsInitialized)
        return;
    const WorldPreviewContext preview(sessionKeeper_);
    SessionRandom::PresentationScope presentation(*sessionKeeper_.Random());
    if (m_PhotoHelper.Live && !m_photoHelperBones)
        PreparePhotoMount();
    auto *c = &m_PhotoChar;
    auto *o = &c->Object;
    const float scale = 0.7f * m_fCurrentZoom;
    const bool poseChanged = o->Angle[0] != 0.f || o->Angle[1] != 0.f ||
                             o->Angle[2] != m_fCurrentAngle || o->Scale != scale ||
                             o->CurrentAction != c->WorldVisualAction ||
                             o->AnimationFrame != c->WorldVisualAnimationFrame ||
                             m_PhotoVisual.appearanceRevision != c->WorldVisualAppearanceRevision;
    Vector(0.f, 0.f, m_fCurrentAngle, o->Angle);
    o->Scale = scale;
    m_PhotoHelper.Scale = m_fPhotoHelperScale * m_fCurrentZoom;
    Vector(1, 1, 1, o->Light);
    Vector(1, 1, 1, m_PhotoHelper.Light);

    c->HideShadow = true;
    if (c->Wing.Type != -1 && m_iSettingAnimation > AT_HEALING1)
        c->SafeZone = true;
    else
        c->SafeZone = false;
    const double elapsedMilliseconds = WorldTime - m_photoUpdateTime;
    m_photoUpdateTime = WorldTime;
    const bool advance = elapsedMilliseconds > 0.0;
    struct FactorScope final
    {
        float &factor;
        float previous;
        ~FactorScope()
        {
            factor = previous;
        }
    } scope{FPS_ANIMATION_FACTOR, FPS_ANIMATION_FACTOR};
    FPS_ANIMATION_FACTOR = ApplicationFrameUnit::CalculateAnimationFactor(
        elapsedMilliseconds, sessionKeeper_.ApplicationConfig().legacyReferenceFps);
    if (advance)
        AdvancePhotoAnimation();
    if (!advance && (poseChanged || c->WorldVisualPoseRevision == 0))
    {
        gameplay_.RefreshLocalCharacterPose(*c);
        if (m_photoHelperBones)
        {
            gameplay_.SynchronizeMount(m_PhotoHelper);
            gameplay_.PrepareMountPose(m_PhotoHelper, &m_PhotoVisual.localMountPose);
        }
    }
    if (!advance)
        sessionKeeper_.Visual()->AdvanceLocalCharacterVisual(
            *c, m_PhotoVisual, m_photoHelperBones ? &m_PhotoHelper : nullptr);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUIPhotoViewer::AdvancePhotoAnimation()
{
    UpdatePhotoPose();
    MoveCharacter(&m_PhotoChar, &m_PhotoChar.Object);
    AdvanceCharacterEnvironmentState(m_PhotoChar);
    gameplay_.PrepareLocalCharacterPose(m_PhotoChar);
    sessionKeeper_.Visual()->AdvanceLocalCharacterVisual(
        m_PhotoChar, m_PhotoVisual, m_photoHelperBones ? &m_PhotoHelper : nullptr);
}
#pragma pack(pop)
