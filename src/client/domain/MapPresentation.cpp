#include "domain/MapPresentation.h"
#include "render/Character.h"
#include "support/CoreMath.h"
#include "session/SessionGameplay.h"
#include "domain/MapSimulation.h"
#include "domain/Quests.h"
#include "data/WorldData.h"
#include "render/Terrain.h"
#include "domain/WorldSimulation.h"
#include "session/SessionKeeper.h"
#include "domain/CharacterPresentation.h"
#include "domain/Events.h"
#include "render/Textures.h"
#include "render/ModelResources.h"
#include "render/World.h"
#include "domain/CharacterSystem.h"
#include "domain/EffectsUpdate.h"
#include "ui/session/UiSessionLogic.h"
#include "app/ApplicationAudio.h"
#include "data/GameData.h"
#include "ui/runtime/UiControls.h"
#include "ui/features/Items/ItemsLogic.h"
#include "I18N/All.h"
#include "data/Localization.h"
#include "domain/ItemsSkills.h"
#include "domain/MovementAI.h"
#include "render/ModelGeometry.h"
#include "session/SessionPresentation.h"
#include "support/Camera.h"
#include "session/SessionRender.h"
#include "render/Text.h"
#include "support/Scenes.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationKeeper.h"
#include "data/CharacterData.h"
#include "data/ItemData.h"
#include "domain/ChatSocial.h"
#include "session/SessionAudio.h"
#include "session/SessionWorkspace.h"
#include "domain/WorldPhysics.h"
#include "app/ApplicationConfigScheduling.h"
#include "app/ApplicationNetwork.h"
#include "domain/Guild.h"
#include "ui/features/Dialogs/DialogsLogic.h"

void MapProcess::MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b)
{
    // Shared model trails retain their original order, including overlapping
    // handlers. Only the active map contributes map-local trails afterward.
    switch (o->Type)
    {
    case MODEL_AXE_HERO:
        huntingGround_->MoveHuntingGroundBlurEffect(c, o, b);
        break;
    case MODEL_WEREWOLF_HERO:
    case MODEL_SOLAM:
        cryingWolf2nd_->MoveCryingWolf2ndBlurEffect(c, o, b);
        crywolf1st_->MoveCryWolf1stBlurEffect(c, o, b);
        break;
    case MODEL_SORAM:
    case MODEL_DEATH_SPIRIT:
        crywolf1st_->MoveCryWolf1stBlurEffect(c, o, b);
        thirdChange_->MoveBalgasBarrackBlurEffect(c, o, b);
        break;
    case MODEL_SCOUT:
    case MODEL_BALGASS:
    case MODEL_DARK_ELF_1:
        crywolf1st_->MoveCryWolf1stBlurEffect(c, o, b);
        break;
    case MODEL_DEATH_RIDER:
    case MODEL_FOREST_ORC:
    case MODEL_DEATH_TREE:
    case MODEL_BLOODY_ORC:
    case MODEL_BLOODY_DEATH_RIDER:
        aida_->MoveAidaBlurEffect(c, o, b);
        break;
    case MODEL_SATYROS:
    case MODEL_BERSERK:
    case MODEL_BLADE_HUNTER:
    case MODEL_GIGANTIS:
    case MODEL_GENOCIDER:
    case MODEL_BERSERKER_WARRIOR:
    case MODEL_KENTAUROS_WARRIOR:
    case MODEL_GIGANTIS_WARRIOR:
        kanturu1st_->MoveKanturu1stBlurEffect(c, o, b);
        break;
    case MODEL_PERSONA:
    case MODEL_TWIN_TAIL:
    case MODEL_DREADFEAR:
        kanturu2nd_->Move_Kanturu2nd_BlurEffect(c, o, b);
        break;
    case MODEL_DARK_ELF:
        thirdChange_->MoveBalgasBarrackBlurEffect(c, o, b);
        break;
    case MODEL_HIDEOUS_RABBIT:
        newTown_->MoveSharedMonsterBlur(c, o, b);
        break;
    case MODEL_SAPIUNUS:
    case MODEL_SAPIDUO:
    case MODEL_SAPITRES:
    case MODEL_NAPIN:
    case MODEL_SAPI_QUEEN:
    case MODEL_WOLF_STATUS:
        swampOfQuiet_->MoveSharedMonsterBlur(c, o, b);
        break;
    }
    if (BaseMap *const map = ContextBehavior())
        map->MoveBlurEffect(c, o, b);
}

bool MapProcess::PrepareAmbientBoidSlot(int index, bool &allowCreate)
{
    allowCreate = true;
    BaseMap *const map = ContextBehavior();
    return map ? map->PrepareAmbientBoidSlot(index, allowCreate) : index < 5;
}

bool MapProcess::CreateAmbientBoid(OBJECT *object, int slot, int terrainIndex)
{
    BaseMap *const map = ContextBehavior();
    if (!map || !map->CanCreateAmbientBoid(slot, terrainIndex))
        return false;
    object->Initialize();
    object->Live = true;
    object->Velocity = 1.f;
    object->LightEnable = true;
    object->LifeTime = 0;
    object->SubType = 0;
    Vector(0.5f, 0.5f, 0.5f, object->Light);
    object->Alpha = 0.f;
    object->AlphaTarget = 1.f;
    object->Gravity = 13;
    object->AI = 0;
    object->CurrentAction = 0;
    if (!map->ConfigureAmbientBoid(object, slot))
        PlaceAmbientBoid(*object);
    return true;
}

void MapProcess::PlaceAmbientBoid(OBJECT &object)
{
    object.AlphaEnable = true;
    object.Scale = 0.8f;
    object.ShadowScale = 10.f;
    object.HiddenMesh = -1;
    object.BlendMesh = -1;
    object.Timer = (float)(WorldRandom() % 314) * 0.01f;
    Vector(Hero->Object.Position[0] + (float)(WorldRandom() % 1024 - 512),
           Hero->Object.Position[1] + (float)(WorldRandom() % 1024 - 512), Hero->Object.Position[2],
           object.Position);
    object.Position[2] = RequestTerrainHeight(object.Position[0], object.Position[1]) +
                         (float)(WorldRandom() % 200 + 150);
    Vector(0.f, 0.f, 0.f, object.Angle);
}

bool MapProcess::CreateAmbientFish(OBJECT *object, int terrainIndex)
{
    BaseMap *const map = ContextBehavior();
    if (!map || !map->CanCreateAmbientFish(terrainIndex))
        return false;
    object->Live = true;
    object->Alpha = 0.f;
    object->AlphaTarget = 1.f;
    object->BlendMesh = -1;
    Vector(0.5f, 0.5f, 0.5f, object->Light);
    Vector(0.f, 0.f, 0.f, object->Angle);
    object->LightEnable = true;
    object->AlphaEnable = true;
    object->SubType = 0;
    object->HiddenMesh = -1;
    object->LifeTime = WorldRandom() % 128;
    object->Scale = (float)(WorldRandom() % 4 + 4) * 0.1f;
    object->Gravity = 13;
    object->bBillBoard = true;
    map->ConfigureAmbientFish(object);
    return true;
}

void MapProcess::MoveAmbientFishTerrain(OBJECT *object, int index)
{
    const auto *definition = sessionKeeper_.WorldContextDefinition();
    const MapPresentationPolicy fallback;
    const auto &policy = definition ? definition->presentation : fallback;
    const bool wall = TerrainWall[index] == 1 || TerrainWall[index] >= TW_NOGROUND;
    const bool turn =
        policy.fishWallObstacles
            ? wall
            : (object->Type == MODEL_FISH01 && TerrainMappingLayer1[index] != 5) ||
                  (object->Type == MODEL_RAT01 && TerrainWall[index] >= TW_NOGROUND) ||
                  (policy.waterFish && wall);
    if (turn)
    {
        object->Angle[2] += 180.f;
        if (object->Angle[2] >= 360.f)
            object->Angle[2] -= 360.f;
        ++object->SubType;
    }
    else if (object->SubType > 0)
        --object->SubType;
    if (policy.finiteAmbientFish && object->LifeTime <= 1)
        object->Live = false;
}

bool MapProcess::MoveMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                   WorldCharacterVisualState &visual)
{
    // Crying Wolf's local effects precede the shared wolf effects. Other local
    // movement types are disjoint from the shared model routes below.
    if (BaseMap *const map = ContextBehavior(); map && map->MoveMonsterVisual(c, o, b, visual))
        return true;
    switch (o->Type)
    {
    case MODEL_WARCRAFT:
    case MODEL_DEATH_ANGEL:
    case MODEL_ILLUSION_OF_KUNDUN:
    case MODEL_BLOOD_SOLDIER:
    case MODEL_AEGIS:
    case MODEL_DEATH_CENTURION:
    case MODEL_NECRON:
    case MODEL_SHRIKER:
        return hellas_->MoveHellasMonsterVisual(o, b, visual);
    case MODEL_FIRE_GOLEM:
        return huntingGround_->MoveHuntingGroundMonsterVisual(o, b, visual);
    case MODEL_SCOUT:
    case MODEL_BALGASS:
    case MODEL_DARK_ELF_1:
    case MODEL_WEREWOLF_HERO:
    case MODEL_SOLAM:
    case MODEL_VALAM:
    case MODEL_BALLISTA:
        return crywolf1st_->MoveCryWolf1stMonsterVisual(c, o, b, visual);
    case MODEL_SORAM:
    case MODEL_BALRAM:
    case MODEL_DEATH_SPIRIT:
        if (crywolf1st_->MoveCryWolf1stMonsterVisual(c, o, b, visual))
            return true;
        return thirdChange_->MoveBalgasBarrackMonsterVisual(c, o, b, visual);
    case MODEL_WITCH_QUEEN:
    case MODEL_GOLDEN_STONE_GOLEM:
    case MODEL_HELL_MAINE:
    case MODEL_BLOODY_GOLEM:
    case MODEL_BLOODY_WITCH_QUEEN:
        return aida_->MoveAidaMonsterVisual(o, b, visual);
    case MODEL_PERSONA:
    case MODEL_TWIN_TAIL:
    case MODEL_DREADFEAR:
    case MODEL_KANTURU2ND_ENTER_NPC:
        return kanturu2nd_->Move_Kanturu2nd_MonsterVisual(c, o, b, visual);
    case MODEL_DARK_ELF:
        return thirdChange_->MoveBalgasBarrackMonsterVisual(c, o, b, visual);
    case MODEL_DUAL_BERSERKER:
    case MODEL_BANSHEE:
    case MODEL_HEAD_MOUNTER:
        return empireGuardian3_->MoveSharedMonsterVisual(c, o, b, visual);
    case MODEL_POUCH_OF_BLESSING:
        return sessionKeeper_.Gameplay()->NewYearsDayEvent().MoveMonsterVisual(c, o, b, visual);
    case MODEL_FIRE_FLAME_GHOST:
        return sessionKeeper_.Gameplay()->SummerEvent().MoveMonsterVisual(
            c, o, b, sessionKeeper_.FrameWorldTime(), visual);
    default:
        return false;
    }
}

bool MapProcess::AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                      WorldCharacterVisualState &visual)
{
    // Shared models emit in the observer's captured tick, even outside their
    // original map. A false result may still have emitted an effect.
    switch (o->Type)
    {
    case MODEL_WARCRAFT:
    case MODEL_DEATH_ANGEL:
    case MODEL_ILLUSION_OF_KUNDUN:
    case MODEL_BLOOD_SOLDIER:
    case MODEL_AEGIS:
    case MODEL_DEATH_CENTURION:
    case MODEL_NECRON:
    case MODEL_SHRIKER:
        if (hellas_->AdvanceHellasMonsterVisual(c, o, b, visual))
            return true;
        break;
    case MODEL_LIZARD_WARRIOR:
    case MODEL_FIRE_GOLEM:
    case MODEL_QUEEN_BEE:
    case MODEL_POISON_GOLEM:
    case MODEL_AXE_HERO:
    case MODEL_EROHIM:
        if (huntingGround_->AdvanceHuntingGroundMonsterVisual(c, o, b, visual))
            return true;
        break;
    case MODEL_WITCH_QUEEN:
    case MODEL_GOLDEN_STONE_GOLEM:
    case MODEL_DEATH_RIDER:
    case MODEL_FOREST_ORC:
    case MODEL_DEATH_TREE:
    case MODEL_HELL_MAINE:
    case MODEL_BLOODY_ORC:
    case MODEL_BLOODY_DEATH_RIDER:
    case MODEL_BLOODY_GOLEM:
    case MODEL_BLOODY_WITCH_QUEEN:
        if (aida_->AdvanceAidaMonsterVisual(c, o, b, visual))
            return true;
        break;
    case MODEL_BERSERK:
    case MODEL_GIGANTIS:
    case MODEL_GENOCIDER:
    case MODEL_SPLINTER_WOLF:
    case MODEL_IRON_RIDER:
    case MODEL_SATYROS:
    case MODEL_BLADE_HUNTER:
    case MODEL_KENTAUROS:
    case MODEL_BERSERKER_WARRIOR:
    case MODEL_KENTAUROS_WARRIOR:
    case MODEL_GIGANTIS_WARRIOR:
    case MODEL_SOCCERBALL:
        if (kanturu1st_->AdvanceKanturu1stMonsterVisual(c, o, b, visual))
            return true;
        break;
    case MODEL_PERSONA:
    case MODEL_TWIN_TAIL:
    case MODEL_DREADFEAR:
    case MODEL_KANTURU2ND_ENTER_NPC:
    case MODEL_TRAP_CANON:
        if (kanturu2nd_->Advance_Kanturu2nd_MonsterVisual(c, o, b, visual))
            return true;
        break;
    case MODEL_CURSEDTEMPLE_ILLUSION_NPC:
    case MODEL_CURSEDTEMPLE_ENTER_NPC:
    case MODEL_ILLUSION_SORCERER_SPIRIT_POISON:
    case MODEL_ILLUSION_SORCERER_SPIRIT_ICE:
    case MODEL_ILLUSION_SORCERER_SPIRIT_LIGHTNING:
    case MODEL_CURSEDTEMPLE_STATUE:
    case MODEL_CURSEDTEMPLE_ALLIED_BASKET:
    case MODEL_CURSEDTEMPLE_ILLUSION__BASKET:
        if (cursedTemple_->AdvanceSharedMonsterVisual(c, o, b, visual))
            return true;
        break;
    }
    BaseMap *const map = ContextBehavior();
    return map && map->AdvanceMonsterVisual(c, o, b, visual);
}

using namespace SEASON3A;

void CursedTemple::MoveMonsterSoundVisual(OBJECT *object, BMD *)
{
    if (!gMapManager.IsCursedTemple() || FPS_ANIMATION_FACTOR <= 0.f)
        return;
    const bool firstFamily = object->Type == MODEL_ILLUSION_SORCERER_SPIRIT_LIGHTNING ||
                             object->Type == MODEL_ILLUSION_SORCERER_SPIRIT_ICE;
    if (!firstFamily && object->Type != MODEL_ILLUSION_SORCERER_SPIRIT_POISON)
        return;
    constexpr std::array<std::pair<int, float>, 3> markers{
        {{MONSTER01_STOP1, 0.5f}, {MONSTER01_WALK, 0.5f}, {MONSTER01_DIE, 0.5f}}};
    object->MotionTrace.VisitAnimationEvents(WorldTime, markers, [&](std::size_t event, float) {
        if (event == 0 && firstFamily)
            PlayBuffer(SOUND_CURSEDTEMPLE_MONSTER1_IDLE);
        else if (event == 1)
            PlayBuffer(SOUND_CURSEDTEMPLE_MONSTER_MOVE);
        else if (event == 2)
            PlayBuffer(firstFamily ? SOUND_CURSEDTEMPLE_MONSTER1_DEATH
                                   : SOUND_CURSEDTEMPLE_MONSTER2_DEATH);
    });
}

bool CursedTemple::MoveMonsterVisual(CHARACTER *, OBJECT *o, BMD *b,
                                     WorldCharacterVisualState &visual)
{
    if (!gMapManager.IsCursedTemple())
        return false;

    switch (o->Type)
    {
    case MODEL_CURSEDTEMPLE_ALLIED_NPC: {
    }
        return true;
    case MODEL_CURSEDTEMPLE_ILLUSION_NPC: {
    }
        return true;
    }
    return false;
}

void CursedTemple::MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b)
{
    if (!gMapManager.IsCursedTemple())
        return;

    switch (o->Type)
    {
    case MODEL_ILLUSION_SORCERER_SPIRIT_POISON: {
        if (o->CurrentAction == MONSTER01_ATTACK1 || o->CurrentAction == MONSTER01_ATTACK2)
        {
            vec3_t Light;
            Vector(1.0f, 0.2f, 0.5f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);

                b->TransformPosition(BoneTransform[19], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[21], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 1);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);

                b->TransformPosition(BoneTransform[25], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[27], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 1);

                fAnimationFrame += fSpeedPerFrame;
            }

            for (int j = 0; j < 10; j++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);

                b->TransformPosition(BoneTransform[25], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[27], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 1);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    }
}

bool CursedTemple::AdvanceObjectVisual(OBJECT *o, BMD *b, float)
{
    if (!gMapManager.IsCursedTemple())
        return false;

    vec3_t Light;

    switch (gMapManager.ContextMap())
    {
    case WD_45CURSEDTEMPLE_LV1:
    case WD_45CURSEDTEMPLE_LV2:
    case WD_45CURSEDTEMPLE_LV3:
    case WD_45CURSEDTEMPLE_LV4:
    case WD_45CURSEDTEMPLE_LV5:
    case WD_45CURSEDTEMPLE_LV6: {
        switch (o->Type)
        {
        case 62: {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 5.f))
            {
                vec3_t relative{}, position;
                PrepareWorldObjectPose(*o, birth.FrameFraction());
                b->TransformPosition(BoneTransform[20], relative, position);
                Vector(1.f, 1.f, 1.f, Light);
                CreateParticle(BITMAP_SMOKE, position, o->Angle, Light);
            }
            break;
        }
        case 54: {
            vec3_t position;
            VectorCopy(o->Position, position);
            position[2] -= 100.f;

            float Rotation = (int)WorldTime % 3600 / (float)10.f;

            Vector(0.15f, 0.15f, 0.15f, o->Light);
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                CreateParticle(BITMAP_EFFECT, position, o->Angle, o->Light);
                CreateParticle(BITMAP_EFFECT, position, o->Angle, o->Light, 3);
            }
        }
        break;
        case 70: {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
            {
                float fLumi = (WorldRandom() % 10) * 0.007f + 0.03f;
                Vector(54.f / 256.f * fLumi, 177.f / 256.f * fLumi, 150.f / 256.f * fLumi, Light);
                CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 15, o->Scale, o);
            }
        }
            return true;
        case 71: {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
            {
                float fLumi = (WorldRandom() % 10) * 0.007f + 0.03f;
                Vector(221.f / 256.f * fLumi, 121.f / 256.f * fLumi, 201.f / 256.f * fLumi, Light);
                CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 15, o->Scale, o);
            }
        }
            return true;
        case 72: {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
            {
                float fLumi = (WorldRandom() % 10) * 0.007f + 0.03f;
                Vector(54.f / 256.f * fLumi, 177.f / 256.f * fLumi, 150.f / 256.f * fLumi, Light);
                CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 15, o->Scale, o);
                Vector(221.f / 256.f * fLumi, 121.f / 256.f * fLumi, 201.f / 256.f * fLumi, Light);
                CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 15, o->Scale, o);
            }
        }
            return true;
        case 73: {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
            {
                float fLumi = (WorldRandom() % 10) * 0.002f + 0.03f;
                Vector(1.2f * fLumi, 1.2f * fLumi, 1.2f * fLumi, Light);
                CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 15, o->Scale, o);
            }
        }
            return true;
        case 74: {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
            {
                Vector(0.f, 0.f, 0.f, Light);
                CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 16, o->Scale, o);
            }
        }
            return true;
        case 75: {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 35.f))
            {
                float fLumi = (WorldRandom() % 10) * 0.05f + 0.03f;
                Vector(256.f / 256.f * fLumi, 256.f / 256.f * fLumi, 256.f / 256.f * fLumi, Light);
                CreateParticle(BITMAP_GHOST_CLOUD1, o->Position, o->Angle, Light, 0, o->Scale, o);
            }
        }
            return true;
        case 76: {
            float fLumi = (WorldRandom() % 100) * 0.01;
            //Vector(180.f/255.f+fLumi, 71.f/255.f, 55.f/255.f, Light);
            Vector(180.f / 255.f + fLumi, 71.f / 255.f, 55.f / 255.f, Light);
            vec3_t vPos;
            VectorCopy(o->Position, vPos);
            for (int i = 0; i < 1; ++i)
            {
                CreateParticleFpsChecked(BITMAP_TORCH_FIRE, vPos, o->Angle, Light, 0, o->Scale, o);
            }
            VectorCopy(o->Position, vPos);
            vPos[2] += 20.f;
            CreateSprite(BITMAP_LIGHT, vPos, o->Scale * 6.f, Light, o);
        }
            return true;
        case 77: {
            float fLumi = (WorldRandom() % 100) * 0.005;
            Vector(55.f / 256.f, 71.f / 256.f, 180.f / 256.f + fLumi, Light);
            //Vector(54.f/256.f, 177.f/256.f+fLumi, 150.f/256.f, Light);
            vec3_t vPos;
            VectorCopy(o->Position, vPos);
            for (int i = 0; i < 1; ++i)
            {
                CreateParticleFpsChecked(BITMAP_TORCH_FIRE, vPos, o->Angle, Light, 0, o->Scale, o);
            }
            VectorCopy(o->Position, vPos);
            vPos[2] += 20.f;
            CreateSprite(BITMAP_LIGHT, vPos, o->Scale * 6.f, Light, o);
        }
            return true;
        case 78: {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                const double birthTime = WorldSimulationTime() -
                                         birth.SceneRemainingFrames() * 1000.0 /
                                             sessionKeeper_.ApplicationConfig().legacyReferenceFps;
                int iTime = static_cast<DWORD>(birthTime) % 500;
                int iRand = WorldRandom() % 485;
                if (iTime >= iRand + (WorldRandom() % 5) &&
                    iTime < iRand + (WorldRandom() % 10 + 5))
                {
                    Vector(1.f, 0.8f, 0.8f, Light);
                    for (int i = 0; i < 4; ++i)
                    {
                        CreateEffect(MODEL_FALL_STONE_EFFECT, o->Position, o->Angle, Light);
                    }
                    CreateEffect(MODEL_FALL_STONE_EFFECT, o->Position, o->Angle, Light, 1);
                    Vector(0.7f, 0.7f, 0.8f, Light);
                    vec3_t vPos;
                    VectorCopy(o->Position, vPos);
                    vPos[0] += (float)(WorldRandom() % 80 - 40);
                    vPos[1] += (float)(WorldRandom() % 80 - 40);
                    CreateParticle(BITMAP_WATERFALL_3 + (WorldRandom() % 2), vPos, o->Angle, Light,
                                   2);
                    Vector(0.9f, 0.0f, 0.0f, Light);
                    CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 0, 1.5f);
                }
            }
        }
            return true;
        case 79: {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                for (int i = 0; i < 5; ++i)
                {
                    float fLumi = (WorldRandom() % 10) * 0.03f + 0.008f;
                    Vector(100.f / 256.f * fLumi, 110.f / 256.f * fLumi, 160.f / 256.f * fLumi,
                           Light);
                    CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 17, o->Scale, o);
                }
            }
        }
            return true;
        }
    }
        return true;
    }
    return false;
}

bool CursedTemple::AdvanceSharedMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                              WorldCharacterVisualState &visual)
{
    vec3_t Light;

    MoveMonsterSoundVisual(o, b);

    switch (o->Type)
    {
    case MODEL_CURSEDTEMPLE_ILLUSION_NPC: {
        vec3_t vRelativePos, vWorldPos;
        int boneindex[11] = {34, 48, 49, 20, 45, 19, 44, 22, 21, 47, 46};
        for (int i = 0; i < 11; ++i)
        {
            Vector(0.f, 0.f, 0.f, vRelativePos);
            b->TransformPosition(o->BoneTransform[boneindex[i]], vRelativePos, vWorldPos, true);
            Vector(0.4f, 0.4f, 1.0f, Light);
            CreateSprite(BITMAP_LIGHT, vWorldPos, 0.7f, Light, o); // flare01.jpg
        }

        Vector(0.f, 0.f, 0.f, Light);

        for (int j = 0; j < 3; ++j)
        {
            int randtemp = WorldRandom() % 4;
            if (randtemp == 0)
            {
                Vector(static_cast<float>(WorldRandom() % 120),
                       static_cast<float>(WorldRandom() % 70),
                       static_cast<float>(WorldRandom() % 50), vRelativePos);
            }
            else if (randtemp == 1)
            {
                Vector(-static_cast<float>(WorldRandom() % 120),
                       -static_cast<float>(WorldRandom() % 70),
                       static_cast<float>(WorldRandom() % 50), vRelativePos);
            }
            else if (randtemp == 2)
            {
                Vector(static_cast<float>(WorldRandom() % 120),
                       -static_cast<float>(WorldRandom() % 70),
                       static_cast<float>(WorldRandom() % 50), vRelativePos);
            }
            else if (randtemp == 3)
            {
                Vector(-static_cast<float>(WorldRandom() % 120),
                       static_cast<float>(WorldRandom() % 70),
                       static_cast<float>(WorldRandom() % 50), vRelativePos);
            }

            b->TransformPosition(o->BoneTransform[1], vRelativePos, vWorldPos, true);
            CreateParticleFpsChecked(BITMAP_CLUD64, vWorldPos, o->Angle, Light, 0, 0.7f);
        }
    }
    break;
    case MODEL_CURSEDTEMPLE_ENTER_NPC: {
        vec3_t vRelativePos, vWorldPos;

        if (visual.action == MONSTER01_STOP2)
        {
            float fActionSpeed =
                b->Actions[visual.action].PlaySpeed * static_cast<float>(FPS_ANIMATION_FACTOR);

            if (visual.animationFrame > 0.5f && visual.animationFrame < (8.5f + fActionSpeed))
            {
                Vector(0.f, 0.f, 0.f, vRelativePos);
                b->TransformPosition(o->BoneTransform[22], vRelativePos, vWorldPos, true);

                Vector(0.8f, 0.8f, 1.0f, Light);
                CreateParticleFpsChecked(BITMAP_LIGHT + 2, vWorldPos, o->Angle, Light, 2, 0.7f);

                Light[0] = static_cast<float>((WorldRandom() % 100) * 0.01f);
                Light[1] = static_cast<float>((WorldRandom() % 100) * 0.01f);
                Light[2] = static_cast<float>((WorldRandom() % 100) * 0.01f);

                CreateParticleFpsChecked(BITMAP_SHINY, vWorldPos, o->Angle, Light, 3, 0.8f);
            }

            if (visual.animationFrame > 7.3f && visual.animationFrame < (7.5f + fActionSpeed))
            {
                Vector(0.7f, 0.7f, 1.0f, Light);
                CreateParticleFpsChecked(BITMAP_GM_AURORA, vWorldPos, o->Angle, Light, 3, 0.7f);
            }
        }
    }
        return true;

    case MODEL_ILLUSION_SORCERER_SPIRIT_POISON: {
        vec3_t vRelativePos, vWorldPos;
        Vector(0.f, 0.f, 0.f, vRelativePos);
        if (visual.action == MONSTER01_DIE)
        {
            int boneindex[6] = {6, 2, 19, 25, 35, 40};

            for (int i = 0; i < 6; ++i)
            {
                b->TransformPosition(o->BoneTransform[boneindex[i]], vRelativePos, vWorldPos, true);
                CreateParticleFpsChecked(BITMAP_SMOKE + 3, vWorldPos, o->Angle, Light, 3, 0.5f);
            }
        }
    }
        return true;
    case MODEL_ILLUSION_SORCERER_SPIRIT_ICE: {
        vec3_t vRelativePos, vWorldPos;
        float fLuminosity = (float)sinf((WorldTime) * 0.002f) * 0.2f;

        Vector(0.4f, 0.4f, 1.0f, Light);
        Vector(-2.f, 14.f, 0.f, vRelativePos);
        b->TransformPosition(o->BoneTransform[8], vRelativePos, vWorldPos, true);
        CreateSprite(BITMAP_SHINY + 1, vWorldPos, 0.9f, Light, o, -WorldTime * 0.08f);
        CreateSprite(BITMAP_LIGHT + 3, vWorldPos, 0.8f, Light, o, WorldTime * 0.3f);

        Vector(0.f, 0.f, 0.f, vRelativePos);

        if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
        {
            Vector(0.4f, 0.4f, 1.0f, Light);
            Vector(0.f, 0.f, 0.f, vRelativePos);
            b->TransformPosition(o->BoneTransform[17], vRelativePos, vWorldPos, true);
            CreateSprite(BITMAP_LIGHT, vWorldPos, 3.f, Light, o, 0.f);
            CreateParticleFpsChecked(BITMAP_CLUD64, vWorldPos, o->Angle, Light, 4, 0.8f);
            CreateParticleFpsChecked(BITMAP_SPARK + 1, vWorldPos, o->Angle, Light, 15,
                                     0.7f + (fLuminosity * 0.05f));

            Vector(0.f, 0.f, 0.f, vRelativePos);
            b->TransformPosition(o->BoneTransform[41], vRelativePos, vWorldPos, true);
            CreateSprite(BITMAP_LIGHT, vWorldPos, 3.f, Light, o, 0.f);
            CreateParticleFpsChecked(BITMAP_CLUD64, vWorldPos, o->Angle, Light, 4, 0.8f);
            CreateParticleFpsChecked(BITMAP_SPARK + 1, vWorldPos, o->Angle, Light, 15,
                                     0.7f + (fLuminosity * 0.05f));
        }
        else if (visual.action == MONSTER01_DIE)
        {
            int boneindex[6] = {7, 2, 14, 38, 73, 78};

            for (int i = 0; i < 4; ++i)
            {
                b->TransformPosition(o->BoneTransform[boneindex[i]], vRelativePos, vWorldPos, true);
                CreateParticleFpsChecked(BITMAP_SMOKE + 3, vWorldPos, o->Angle, Light, 3, 0.5f);
            }
        }
    }
        return true;
    case MODEL_ILLUSION_SORCERER_SPIRIT_LIGHTNING: {
        vec3_t vRelativePos, vWorldPos;

        Vector(0.6f, 0.0f, 0.0f, Light);
        Vector(-2.f, 14.f, 0.f, vRelativePos);
        b->TransformPosition(o->BoneTransform[8], vRelativePos, vWorldPos, true);
        CreateSprite(BITMAP_SHINY + 1, vWorldPos, 0.5f, Light, o, -WorldTime * 0.08f);
        CreateSprite(BITMAP_LIGHT + 3, vWorldPos, 0.6f, Light, o, WorldTime * 0.3f);

        Vector(0.f, 0.f, 0.f, vRelativePos);

        if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
        {
            Vector(0.6f, 0.0f, 0.0f, Light);
            b->TransformPosition(o->BoneTransform[17], vRelativePos, vWorldPos, true);
            CreateEffectFpsChecked(MODEL_FENRIR_THUNDER, vWorldPos, o->Angle, Light, 2, o);
            CreateEffectFpsChecked(MODEL_FENRIR_THUNDER, vWorldPos, o->Angle, Light, 2, o);
            CreateEffectFpsChecked(MODEL_FENRIR_THUNDER, vWorldPos, o->Angle, Light, 2, o);
            CreateParticleFpsChecked(BITMAP_CLUD64, vWorldPos, o->Angle, Light, 4, 1.f);
            CreateSprite(BITMAP_LIGHT, vWorldPos, 3.f, Light, o, 0.f);

            b->TransformPosition(o->BoneTransform[41], vRelativePos, vWorldPos, true);
            CreateEffectFpsChecked(MODEL_FENRIR_THUNDER, vWorldPos, o->Angle, Light, 2, o);
            CreateEffectFpsChecked(MODEL_FENRIR_THUNDER, vWorldPos, o->Angle, Light, 2, o);
            CreateEffectFpsChecked(MODEL_FENRIR_THUNDER, vWorldPos, o->Angle, Light, 2, o);
            CreateEffectFpsChecked(MODEL_FENRIR_THUNDER, vWorldPos, o->Angle, Light, 2, o);
            CreateParticleFpsChecked(BITMAP_CLUD64, vWorldPos, o->Angle, Light, 4, 1.f);
            CreateSprite(BITMAP_LIGHT, vWorldPos, 3.f, Light, o, 0.f);
        }
        else if (visual.action == MONSTER01_DIE)
        {
            int boneindex[6] = {7, 2, 14, 38, 73, 78};

            for (int i = 0; i < 6; ++i)
            {
                b->TransformPosition(o->BoneTransform[boneindex[i]], vRelativePos, vWorldPos, true);
                CreateParticleFpsChecked(BITMAP_SMOKE + 3, vWorldPos, o->Angle, Light, 3, 0.5f);
            }
        }
    }
        return true;
    case MODEL_CURSEDTEMPLE_STATUE:
        if (visual.action == MONSTER01_DIE)
        {
            vec3_t vRelativePos, vWorldPos, Light;
            Vector(1.0f, 1.0f, 1.0f, Light);
            Vector(0.f, 0.f, 0.f, vRelativePos);
            b->TransformPosition(o->BoneTransform[13], vRelativePos, vWorldPos, true);
            vRelativePos[0] = vWorldPos[0];
            vRelativePos[1] = vWorldPos[1];
            vRelativePos[2] = vWorldPos[2];
            if (!visual.deathEmitted)
            {
                visual.deathEmitted = true;
                EarthQuake = (float)(WorldRandom() % 16 - 8) * 0.1f;
                vWorldPos[2] = vRelativePos[2] + 250;
                CreateEffectFpsChecked(MODEL_CURSEDTEMPLE_STATUE_PART2, vWorldPos, o->Angle, Light,
                                       0, o, 0, 0);
                for (int i = 0; i < 60; ++i)
                {
                    vWorldPos[0] = vRelativePos[0] + WorldRandom() % 80 - 40;
                    vWorldPos[1] = vRelativePos[1] + WorldRandom() % 80 - 40;
                    vWorldPos[2] = vRelativePos[2] + (WorldRandom() % 250);
                    CreateEffectFpsChecked(MODEL_CURSEDTEMPLE_STATUE_PART1, vWorldPos, o->Angle,
                                           Light, 0, o, 0, 0);
                }
                Vector(0.5f, 0.5f, 0.5f, Light);

                for (int i = 0; i < 160; ++i)
                {
                    vWorldPos[0] = vRelativePos[0] + WorldRandom() % 140 - 70;
                    vWorldPos[1] = vRelativePos[1] + WorldRandom() % 140 - 70;
                    vWorldPos[2] = vRelativePos[2] + (WorldRandom() % 400) - 100;
                    CreateParticleFpsChecked(BITMAP_SMOKE, vWorldPos, o->Angle, Light, 48, 1.0f);
                }
            }
        }
        else
        {
            vec3_t vRelativePos, vWorldPos, Light;
            Vector(0.2f, 0.3f, 0.4f + (WorldRandom() % 3) * 0.1f, Light);
            Vector(0.f, 0.f, 0.f, vRelativePos);
            b->TransformPosition(o->BoneTransform[13], vRelativePos, vWorldPos, true);
            CreateParticleFpsChecked(BITMAP_FLARE + 1, vWorldPos, o->Angle, Light, 0, 0.15f);
            vWorldPos[2] += 30;
            CreateParticleFpsChecked(BITMAP_LIGHT, vWorldPos, o->Angle, Light, 1, 8.0f);

            vWorldPos[2] += 160;
            Vector(0.2f, 0.1f, 0.0f, Light);
            b->TransformPosition(o->BoneTransform[14], vRelativePos, vWorldPos, true);
            CreateParticleFpsChecked(BITMAP_LIGHT, vWorldPos, o->Angle, Light, 1, 4.0f);
        }
        return true;
    case MODEL_CURSEDTEMPLE_ALLIED_BASKET: {
        vec3_t vRelativePos, vWorldPos, Light;
        if (m_ShowAlliedPointEffect)
        {
            Vector(1.0f, 1.0f, 0.5f, Light);
            Vector(0.f, 0.f, 0.f, vRelativePos);
            b->TransformPosition(o->BoneTransform[5], vRelativePos, vWorldPos, true);
            CreateEffectFpsChecked(BITMAP_FLARE, vWorldPos, o->Angle, Light, 1);

            Vector(0.9f, 0.7f, 0.4f, Light);
            CreateEffectFpsChecked(BITMAP_MAGIC, o->Position, o->Angle, Light, 8);

            m_ShowAlliedPointEffect = false;
        }
    }
        return true;
    case MODEL_CURSEDTEMPLE_ILLUSION__BASKET: {
        vec3_t vRelativePos, vWorldPos, Light;
        if (m_ShowIllusionPointEffect)
        {
            Vector(0.5f, 1.0f, 1.0f, Light);
            Vector(0.f, 0.f, 0.f, vRelativePos);
            b->TransformPosition(o->BoneTransform[3], vRelativePos, vWorldPos, true);
            CreateEffectFpsChecked(BITMAP_FLARE, vWorldPos, o->Angle, Light, 2);

            Vector(0.7f, 0.8f, 0.9f, Light);
            CreateEffectFpsChecked(BITMAP_MAGIC, o->Position, o->Angle, Light, 8);

            m_ShowIllusionPointEffect = false;
        }
    }
        return true;
    }
    return false;
}

bool SEASON3A::CGM3rdChangeUp::AdvanceObjectVisual(OBJECT *pObject, BMD *pModel, float)
{
    if (!(IsBalgasBarrackMap() || IsBalgasRefugeMap()))
        return false;

    vec3_t Light;

    switch (pObject->Type)
    {
    case 2:
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, pObject->Position, pObject->Angle, Light, 13,
                           pObject->Scale);
        }
        break;
    case 3:
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_TRUE_FIRE, pObject->Position, pObject->Angle, Light, 0,
                           pObject->Scale);
        }
        break;
    case 5:
        sessionKeeper_.Visual()->EmitAlternatingSmoke(*pObject, AlternatingSmokeStyle::Plain);
        break;
    case 6: {
        Vector(1.f, 1.f, 1.f, Light);
        Vector(0.2f, 0.2f, 0.2f, Light);

        if (pObject->HiddenMesh != -2)
        {
            CreateParticle(BITMAP_CLOUD, pObject->Position, pObject->Angle, Light, 1,
                           pObject->Scale, pObject);
            CreateParticle(BITMAP_CLOUD, pObject->Position, pObject->Angle, Light, 2,
                           pObject->Scale, pObject);
            CreateParticle(BITMAP_CLOUD, pObject->Position, pObject->Angle, Light, 3,
                           pObject->Scale, pObject);
            CreateParticle(BITMAP_CLOUD, pObject->Position, pObject->Angle, Light, 4,
                           pObject->Scale, pObject);
        }
        pObject->HiddenMesh = -2;
    }
    break;
    case 58:
        Vector(1.f, 1.f, 1.f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
            CreateParticle(BITMAP_WATERFALL_1, pObject->Position, pObject->Angle, Light, 2,
                           pObject->Scale);
        break;
    case 59:
        Vector(1.f, 1.f, 1.f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
            CreateParticle(BITMAP_WATERFALL_2, pObject->Position, pObject->Angle, Light, 1,
                           pObject->Scale);
        break;
    case 60:
        Vector(1.f, 1.f, 1.f, Light);
        CreateParticleFpsChecked(BITMAP_WATERFALL_3, pObject->Position, pObject->Angle, Light, 3,
                                 pObject->Scale);
        break;
    case 85:
        sessionKeeper_.Visual()->EmitAlternatingSmoke(*pObject, AlternatingSmokeStyle::Barracks);
        break;
    case 88:
        Vector(1.f, 1.f, 1.f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.f))
        {
            CreateParticle(BITMAP_CLOUD, pObject->Position, pObject->Angle, Light, 10,
                           pObject->Scale, pObject);
        }
        break;
    case 89:
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.f))
        {
            float fRed = (WorldRandom() % 3) * 0.01f + 0.015f;
            Vector(fRed, 0.0f, 0.0f, Light);
            CreateParticle(BITMAP_CLOUD, pObject->Position, pObject->Angle, Light, 11,
                           pObject->Scale, pObject);
        }
        break;
    case 90: {
        Vector(1.0f, 0.4f, 0.4f, Light);
        vec3_t vAngle;
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector((float)(WorldRandom() % 40 + 120), 0.f, (float)(WorldRandom() % 30), vAngle);
            VectorAdd(vAngle, pObject->Angle, vAngle);
            CreateJoint(BITMAP_JOINT_SPARK, pObject->Position, pObject->Position, vAngle, 4,
                        pObject, pObject->Scale);
            CreateParticle(BITMAP_SPARK, pObject->Position, vAngle, Light, 9, pObject->Scale);
        }
    }
    break;
    case 92: {
        Vector(1.0f, 0.4f, 0.4f, Light);
        float fSin = (sinf(WorldTime * 0.0005f) + 1.f) * 0.5f;
        if (fSin > 0.9f)
        {
            for (int i = 0; i < 2; ++i)
            {
                CreateEffectFpsChecked(BITMAP_FIRE_RED, pObject->Position, pObject->Angle, Light, 0,
                                       NULL, -1, 0, pObject->Scale);
            }
        }
    }
    break;
    }

    return true;
}

bool SEASON3A::CGM3rdChangeUp::CreateFireSnuff(PARTICLE *o)
{
    if (IsBalgasRefugeMap() == true)
    {
        o->Type = BITMAP_FIRE_SNUFF;
        o->Scale = WorldRandom() % 50 / 100.f + 0.4f;
        vec3_t Position;
        Vector(Hero->Object.Position[0] + (float)(WorldRandom() % 1600 - 800),
               Hero->Object.Position[1] + (float)(WorldRandom() % 1400 - 500),
               Hero->Object.Position[2] + (float)(WorldRandom() % 300 + 50), Position);

        VectorCopy(Position, o->Position);
        VectorCopy(Position, o->StartPosition);
        o->Velocity[0] = -(float)(WorldRandom() % 64 + 64) * 0.1f;
        if (Position[1] < g_Camera.Position[1] + 400.f)
        {
            o->Velocity[0] = -o->Velocity[0] + 2.2f;
        }
        o->Velocity[1] = (float)(WorldRandom() % 32 - 16) * 0.1f;
        o->Velocity[2] = (float)(WorldRandom() % 32 - 16) * 0.1f;
        o->TurningForce[0] = (float)(WorldRandom() % 16 - 8) * 0.1f;
        o->TurningForce[1] = (float)(WorldRandom() % 64 - 32) * 0.1f;
        o->TurningForce[2] = (float)(WorldRandom() % 16 - 8) * 0.1f;
        return true;
    }
    return false;
}

bool SEASON3A::CGM3rdChangeUp::MoveBalgasBarrackMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                                              WorldCharacterVisualState &visual)
{
    ObjectDrawInput presentation(o);
    visual.movement.Apply(presentation);
    switch (o->Type)
    {
    case MODEL_BALRAM: {
        vec3_t Light;
        Vector(0.9f, 0.2f, 0.1f, Light);
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(10))
            {
                CreateParticle(BITMAP_SMOKE + 1, o->Position, o->Angle, Light);
            }
        }
    }
    break;
    case MODEL_DEATH_SPIRIT: {
        vec3_t Light;
        Vector(0.9f, 0.2f, 0.1f, Light);
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(10))
            {
                CreateParticle(BITMAP_SMOKE + 1, o->Position, o->Angle, Light);
            }
        }
    }
    break;
    case MODEL_SORAM: {
        float fActionSpeed =
            b->Actions[visual.action].PlaySpeed * static_cast<float>(FPS_ANIMATION_FACTOR);
        vec3_t Light;
        vec3_t EndPos, EndRelative;
        Vector(1.f, 1.f, 1.f, Light);

        if (visual.action == MONSTER01_ATTACK1)
        {
            if (visual.animationFrame >= 6.5f && visual.animationFrame < (6.5f + fActionSpeed) &&
                rand_fps_check(1))
            {
                Vector(0.0f, 0.0f, 0.0f, EndRelative);
                b->TransformPosition(presentation.bones[27], EndRelative, EndPos, true);
                CreateEffect(BITMAP_CRATER, EndPos, o->Angle, visual.movement.light, 2);
                CreateParticle(BITMAP_BLUE_BLUR, EndPos, o->Angle, Light, 1);
                CreateParticle(BITMAP_BLUE_BLUR, EndPos, o->Angle, Light, 1);
                Vector(1.0f, 0.6f, 0.4f, Light);
                CreateEffect(BITMAP_SHOCK_WAVE, EndPos, o->Angle, Light, 8);
                CreateEffect(BITMAP_SHOCK_WAVE, EndPos, o->Angle, Light, 8);
                CreateEffect(BITMAP_SHOCK_WAVE, EndPos, o->Angle, Light, 8);
                CreateEffect(BITMAP_SHOCK_WAVE, EndPos, o->Angle, Light, 8);
                CreateEffect(BITMAP_SHOCK_WAVE, EndPos, o->Angle, Light, 8);

                for (int iu = 0; iu < 4; iu++)
                {
                    CreateEffect(MODEL_BIG_STONE1, EndPos, o->Angle, visual.movement.light, 10);
                    CreateEffect(MODEL_STONE2, EndPos, o->Angle, visual.movement.light);
                }
            }
        }
        else if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(10))
            {
                CreateParticle(BITMAP_SMOKE + 1, o->Position, o->Angle, Light);
            }
        }
    }
    break;
    case MODEL_DARK_ELF:
        if (m_nDarkElfAppearance)
        {
            m_nDarkElfAppearance = false;

            vec3_t Light;
            vec3_t EndPos, EndRelative;

            Vector(1.f, 0.2f, 0.2f, Light);
            Vector(0.0f, 0.0f, 0.0f, EndRelative);
            b->TransformPosition(presentation.bones[27], EndRelative, EndPos, true);
            CreateEffect(BITMAP_CRATER, EndPos, o->Angle, visual.movement.light, 2);
            CreateParticle(BITMAP_BLUE_BLUR, EndPos, o->Angle, Light, 1);
            CreateParticle(BITMAP_BLUE_BLUR, EndPos, o->Angle, Light, 1);
            Vector(1.0f, 0.2f, 0.2f, Light);
            CreateEffect(BITMAP_SHOCK_WAVE, EndPos, o->Angle, Light, 8);
            CreateEffect(BITMAP_SHOCK_WAVE, EndPos, o->Angle, Light, 8);
            CreateEffect(BITMAP_SHOCK_WAVE, EndPos, o->Angle, Light, 8);
            CreateEffect(BITMAP_SHOCK_WAVE, EndPos, o->Angle, Light, 8);
            CreateEffect(BITMAP_SHOCK_WAVE, EndPos, o->Angle, Light, 8);
        }
        break;
    }
    return false;
}

void SEASON3A::CGM3rdChangeUp::MoveBalgasBarrackBlurEffect(CHARACTER *c, OBJECT *o, BMD *b)
{
    vec3_t Angle, Position;
    float Matrix[3][4];
    vec3_t p, p2, EndPos;
    vec3_t TempAngle;

    switch (o->Type)
    {
    case MODEL_DEATH_SPIRIT: {
        if ((o->CurrentAction == MONSTER01_ATTACK1 || o->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t Light;
            Vector(0.2f, 1.f, 0.4f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;

            VectorCopy(o->Angle, TempAngle);
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 250.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);
                b->TransformPosition(BoneTransform[27], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[27], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 3, true, 80);

                fAnimationFrame += fSpeedPerFrame;
            }
            VectorCopy(TempAngle, o->Angle);

            if (o->AnimationFrame > 4.5f && o->AnimationFrame < 5.0f)
            {
                CHARACTER *tc = &CharactersClient[c->TargetCharacter];
                OBJECT *to = &tc->Object;
                vec3_t Angle = {0.f, 0.f, o->Angle[2]};
                vec3_t Pos = {0.f, 0.f, (to->BoundingBoxMax[2] / 1.f)};

                Vector(80.f, 0.f, 20.f, p);
                b->TransformPosition(o->BoneTransform[0], p, Position, true);
                Position[2] += 50.0f;
                Angle[2] = o->Angle[2] + 90;
                CreateEffect(MODEL_DEATH_SPI_SKILL, Position, Angle, Pos, 0, to);
                CreateEffect(MODEL_DEATH_SPI_SKILL, Position, Angle, Pos, 1, to);
                Angle[2] = o->Angle[2] - 90;
                CreateEffect(MODEL_DEATH_SPI_SKILL, Position, Angle, Pos, 0, to);
                CreateEffect(MODEL_DEATH_SPI_SKILL, Position, Angle, Pos, 1, to);
            }
        }
    }
    break;
    case MODEL_SORAM: {
        vec3_t Light;
        Vector(1.0f, 1.0f, 1.0f, Light);

        if (o->CurrentAction == MONSTER01_ATTACK2)
        {
            vec3_t EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            VectorCopy(o->Angle, TempAngle);
            for (int i = 0; i < 10; ++i)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 100.f, -150.f, EndRelative);
                b->TransformPosition(BoneTransform[16], EndRelative, EndPos, false);

                if (o->AnimationFrame > 5.0f && o->AnimationFrame < 7.0f && rand_fps_check(1))
                {
                    CreateParticle(BITMAP_FIRE, EndPos, o->Angle, Light);
                }

                Vector(0.f, -150.f, 0.f, p);
                AngleMatrix(o->Angle, Matrix);
                VectorRotate(p, Matrix, Position);
                VectorAdd(Position, o->Position, p2);
                o->Angle[2] -= 18;
                Vector((float)(WorldRandom() % 60 + 60 - 90), 0.f, (float)(WorldRandom() % 30 + 90),
                       Angle);
                VectorAdd(Angle, o->Angle, Angle);
                VectorCopy(p2, Position);

                fAnimationFrame += fSpeedPerFrame;
            }
            VectorCopy(TempAngle, o->Angle);
        }
    }
    break;
    case MODEL_DARK_ELF: {
        if ((o->CurrentAction == MONSTER01_ATTACK1 || o->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t Light;
            Vector(1.f, 1.f, 1.f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;

            if (o->CurrentAction == MONSTER01_ATTACK2 &&
                (o->AnimationFrame > 4.5f && o->AnimationFrame < 5.0f))
                CreateEffectFpsChecked(MODEL_DARK_ELF_SKILL, o->Position, o->Angle, o->Light, 2, o);

            VectorCopy(o->Angle, TempAngle);
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 0.f, -60.f, StartRelative);
                Vector(0.f, 0.f, -150.f, EndRelative);
                b->TransformPosition(BoneTransform[c->Weapon[0].LinkBone], StartRelative, StartPos,
                                     false);
                b->TransformPosition(BoneTransform[c->Weapon[0].LinkBone], EndRelative, EndPos,
                                     false);
                CreateBlur(c, StartPos, EndPos, Light, 3, true, 80);

                fAnimationFrame += fSpeedPerFrame;
            }
            VectorCopy(TempAngle, o->Angle);
        }
        else if (o->CurrentAction == MONSTER01_ATTACK3 && rand_fps_check(1))
        {
            vec3_t Position, Light;
            boneManager_.GetBonePosition(o, CharacterSocket::Left_Hand, Position);

            Vector(0.2f, 0.2f, 0.7f, Light);
            CreateParticle(BITMAP_SMOKE, Position, o->Angle, Light, 27, 1.0f);
            Vector(0.3f, 0.3f, 0.4f, Light);
            CreateParticle(BITMAP_LIGHT + 1, Position, o->Angle, Light, 2, 0.8f);
            CreateParticle(BITMAP_LIGHT + 1, Position, o->Angle, Light, 2, 0.6f);
        }
    }
    break;
    }
}

bool SEASON3A::CGM3rdChangeUp::AdvanceBalgasBarrackMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                                                 WorldCharacterVisualState &visual)
{
    if (!(IsBalgasBarrackMap() || IsBalgasRefugeMap()))
        return false;

    switch (o->Type)
    {
    case MODEL_BALRAM:
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_CRY1ST_BALRAM_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_BALRAM_ATTACK1);
            }
        }
        else if (visual.action == MONSTER01_ATTACK2)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_BALRAM_ATTACK2);
            }
        }
        else if (visual.action == MONSTER01_DIE)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_BALRAM_DIE);
            }
        }
        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;

        return true;
    case MODEL_DEATH_SPIRIT: {
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_CRY1ST_DEATHSPIRIT_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_DEATHSPIRIT_ATTACK1);
            }
        }
        else if (visual.action == MONSTER01_ATTACK2)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_DEATHSPIRIT_ATTACK2);
            }
        }
        else if (visual.action == MONSTER01_DIE)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_DEATHSPIRIT_DIE);
            }
        }

        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;

        vec3_t Position, Light;
        int dummy = WorldRandom() % 14;
        auto Data = (float)((float)dummy / (float)100);
        auto Rot = (float)(WorldRandom() % 360);
        Vector(1.0f, 1.0f, 1.0f, Light);
        boneManager_.GetBonePosition(o, CharacterSocket::Monster94_zx, Position);
        CreateSprite(BITMAP_DS_EFFECT, Position, 1.5f, Light, o);
        Vector(0.3f, 0.3f, 0.7f, Light);
        CreateSprite(BITMAP_LIGHT, Position, 3.5f + (Data * 5), Light, o);
        CreateSprite(BITMAP_LIGHT, Position, 3.5f + (Data * 5), Light, o);
        CreateSprite(BITMAP_LIGHT, Position, 3.5f + (Data * 5), Light, o);
        CreateSprite(BITMAP_DS_SHOCK, Position, 0.8f + Data, Light, o, Rot);

        boneManager_.GetBonePosition(o, CharacterSocket::Monster94_zx01, Position);
        Vector(0.1f, 0.0f, 0.6f, Light);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.8f, Light, o, Rot);
        if (rand_fps_check(2))
        {
            Vector(0.7f, 0.7f, 1.0f, Light);
            CreateSprite(BITMAP_SHINY + 1, Position, 0.8f, Light, o, 360.f - Rot);
        }
        Vector(0.3f, 0.3f, 0.7f, Light);
        CreateSprite(BITMAP_LIGHT, Position, 1.5f, Light, o);
    }
        return true;
    case MODEL_SORAM:
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_CRY1ST_SORAM_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_SORAM_ATTACK1);
            }
        }
        else if (visual.action == MONSTER01_ATTACK2)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_SORAM_ATTACK2);
            }
        }
        else if (visual.action == MONSTER01_DIE)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_SORAM_DIE);
            }
        }
        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;
        return true;
    case MODEL_DARK_ELF: {
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_CRY1ST_DARKELF_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_DARKELF_ATTACK1);
            }
        }
        else if (visual.action == MONSTER01_ATTACK2)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_DARKELF_ATTACK2);
            }
        }
        else if (visual.action == MONSTER01_ATTACK3)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_DARKELF_SKILL1);
            }
        }
        else if (visual.action == MONSTER01_ATTACK4)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_DARKELF_SKILL2);
            }
        }
        else if (visual.action == MONSTER01_DIE)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_DARKELF_DIE);
            }
        }

        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;

        vec3_t vRelativePos, vPos, vLight;
        Vector(0.f, 0.f, 0.f, vRelativePos);
        Vector(0.f, 0.f, 0.f, vPos);
        Vector(0.6f, 0.6f, 0.9f, vLight);
        int iBoneThunder[] = {6, 15, 27, 17, 29, 3, 34, 44, 45, 40};
        if (rand_fps_check(2))
        {
            for (int i = 0; i < 10; ++i)
            {
                b->TransformPosition(o->BoneTransform[iBoneThunder[i]], vRelativePos, vPos, true);
                if (rand_fps_check(2))
                {
                    CreateEffect(MODEL_FENRIR_THUNDER, vPos, o->Angle, vLight, 1, o);
                }
            }
        }
    }
        return true;
    }
    return false;
}

bool SEASON3A::CGM3rdChangeUp::AdvanceMonsterVisual(CHARACTER *character, OBJECT *object,
                                                    BMD *model, WorldCharacterVisualState &visual)
{
    return AdvanceBalgasBarrackMonsterVisual(character, object, model, visual);
}

bool SEASON3A::CGM3rdChangeUp::CreateWeather(PARTICLE *particle, int)
{
    return CreateFireSnuff(particle);
}

void GMAtlans::PlayAmbientSounds()
{
    PlayBuffer(SOUND_WATER01, NULL, true);
}

bool GMAtlans::AllowsAmbientSound(ESound sound) const
{
    return (sound == SOUND_WATER01);
}

void GMAtlans::UpdateMusic()
{
    PlayMp3(MUSIC_ATLANS);
}

bool GMAtlans::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_ATLANS) == 0;
}

bool GMAtlans::CreateWeather(PARTICLE *o, int)
{
    o->Type = BITMAP_LEAF1;
    vec3_t Position;
    Vector(Hero->Object.Position[0] + Random.RangeFloat(-800, 799),
           Hero->Object.Position[1] + Random.RangeFloat(-500, 899),
           Hero->Object.Position[2] + Random.RangeFloat(50, 349), Position);
    VectorCopy(Position, o->Position);
    VectorCopy(Position, o->StartPosition);
    o->Velocity[0] = -Random.RangeFloat(64, 127) * 0.1f;
    if (Position[1] < sessionKeeper_.CameraStateObject().Position[1] + 400.f)
        o->Velocity[0] = -o->Velocity[0] + 3.2f;
    o->Velocity[1] = Random.RangeFloat(-16, 15) * 0.1f;
    o->Velocity[2] = Random.RangeFloat(-16, 15) * 0.1f;
    o->TurningForce[0] = Random.RangeFloat(-8, 7) * 0.1f;
    o->TurningForce[1] = Random.RangeFloat(-32, 31) * 0.1f;
    o->TurningForce[2] = Random.RangeFloat(-8, 7) * 0.1f;

    return true;
}

void GMAtlans::ConfigureAmbientFish(OBJECT *o)
{
    o->Scale = (float)(WorldRandom() % 2 + 8) * 0.1f;
    if (Hero->Object.Position[1] * 0.01f < 128)
    {
        o->Type = MODEL_FISH01 + 1 + 2 + WorldRandom() % 4;
        o->Velocity = 1.f / o->Scale;
        if (Random.FpsCheck(2, 1.f))
            o->Gravity = 2;
        else
            o->Gravity = 3;
    }
    else
    {
        o->Type = MODEL_FISH01 + 1 + 6 + WorldRandom() % 2;
        if (o->Type == MODEL_FISH01 + 7 || o->Type == MODEL_FISH01 + 8)
        {
            o->BlendMesh = 0;
            o->BlendMeshLight = 1.f;
        }
        o->Velocity = 0.5f / o->Scale;
        if (Random.FpsCheck(2, 1.f))
            o->Gravity = 1;
        else
            o->Gravity = 2;
    }
    o->Timer = (float)(WorldRandom() % 32) * 0.1f;
    o->Position[2] =
        RequestTerrainHeight(o->Position[0], o->Position[1]) + (float)(WorldRandom() % 150 + 50);
}

bool GMAtlans::CanCreateAmbientFish(int index)
{
    return TerrainWall[index] == 0 || TerrainWall[index] == TW_CHARACTER;
}

bool GMAtlans::ConfigureAmbientBoid(OBJECT *o, int)
{
    if (Hero->Object.Position[1] * 0.01f < 128)
    {
        o->Type = MODEL_FISH01 + 1 + WorldRandom() % 2;
        o->Gravity = 15;
        o->AlphaEnable = true;
        o->Scale = 0.8f;
        o->ShadowScale = 10.f;
        o->HiddenMesh = -1;
        o->BlendMesh = -1;
        o->Timer = (float)(WorldRandom() % 314) * 0.01f;
        if (WorldRandom() % 100 < 90)
        {
            o->Velocity = 0.3f;
        }
        else
        {
            o->Velocity = 0.25f;
        }
        o->LightEnable = false;
        Vector(0.f, 0.f, 0.f, o->Angle);
        Vector(1.f, 1.f, 1.f, o->Light);
        Vector(Hero->Object.Position[0] + (float)(WorldRandom() % 1024 - 512),
               Hero->Object.Position[1] + (float)(WorldRandom() % 1024 - 512),
               Hero->Object.Position[2], o->Position);

        o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) +
                         (float)(WorldRandom() % 200 + 150);
    }
    else
    {
        o->Live = false;
    }
    return true;
}

bool GMAtlans::CanCreateAmbientBoid(int slot, int index)
{
    return TerrainWall[index] == 0 || TerrainWall[index] == TW_CHARACTER;
}

bool GMAtlans::PrepareAmbientBoidSlot(int index, bool &allowCreate)
{
    return true;
}

ESound GMAtlans::WalkingSound(int tile, bool safe) const
{
    return safe ? SOUND_HUMAN_WALK_GROUND : SOUND_HUMAN_WALK_SWIM;
}

bool GMBloodCastle::AdvanceObjectVisual(OBJECT *o, BMD *b, float Luminosity)
{
    vec3_t p{}, Position{}, Light{};
    int Bitmap;
    float Scale, Rotation;
    switch (o->Type)
    {
    case 11: {
        PrepareWorldObjectPose(*o);
        wchar_t indexLight[7] = {1, 2, 4, 6, 9, 10, 11};

        Luminosity = sinf((o->Angle[2] * 20 + WorldTime) * 0.001f) * 0.5f + 0.5f;
        Vector(Luminosity * 1.f, Luminosity * 0.5f, 0.f, Light);

        for (int i = 0; i < 7; ++i)
        {
            Vector(0.f, 0.f, 2.f, p);
            b->TransformPosition(BoneTransform[indexLight[i]], p, Position);
            CreateSprite(BITMAP_LIGHT, Position, 0.5f, Light, o);
        }
    }
    break;
    case 13:
        PrepareWorldObjectPose(*o);
        Luminosity = sinf(WorldTime * 0.001f) * 0.3f + 0.7f;
        Vector(Luminosity, Luminosity, Luminosity, Light);
        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(BoneTransform[3], p, Position);
        CreateSprite(BITMAP_FLARE, Position, Luminosity + 0.5f, Light, o);
        break;
    case 37:
        sessionKeeper_.Visual()->EmitAlternatingSmoke(*o, AlternatingSmokeStyle::Flare);
    }
    return true;
}

bool GMBloodCastle::ConfigureAmbientBoid(OBJECT *object, int)
{
    object->Type = MODEL_CROW;
    return false;
}

bool GMBloodCastle::CanCreateAmbientBoid(int slot, int index)
{
    return true;
}

void GMBloodCastle::PrepareObjectEffects(int &objCount, int previousVisible)
{
    vec3_t Position, Angle;
    vec3_t Light;

    Vector(0.f, 0.f, 0.f, Angle);
    Vector(1.f, 1.f, 1.f, Light);

    for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.f))
    {
        const float fraction = emission.FrameFraction();
        Hero->Object.MotionTrace.Sample(WorldTime, fraction, Hero->Object.Position, Position);
        Position[0] += WorldRandom() % 900 - 300;
        Position[1] += WorldRandom() % 900 - 300;
        Position[2] += WorldRandom() % 50 + 250.f;
        CreateParticle(BITMAP_FLARE, Position, Angle, Light, 3, 0.19f, NULL);
    }
    return;
}

void CGMCrywolf1st::AdvanceNotices()
{
    constexpr int NoticeIntervalMilliseconds = 10000;
    constexpr int NoticeCount = 4;
    if (m_CrywolfState == CRYWOLF_STATE_READY && GetTimeCheck(NoticeIntervalMilliseconds))
        iNextNotice = (iNextNotice + 1) % NoticeCount;
}

void CGMCrywolf1st::ChangeBackGroundMusic(int World)
{
    if (World == WD_34CRYWOLF_1ST)
    {
        if (m_CrywolfState == CRYWOLF_STATE_NOTIFY_2)
        {
            StopMp3(MUSIC_BC_CRYWOLF_1ST);
            PlayMp3(MUSIC_CRYWOLF_BEFORE);

            if (IsEndMp3())
                StopMp3(MUSIC_CRYWOLF_BEFORE);
        }
        else if (m_CrywolfState == CRYWOLF_STATE_READY)
        {
            StopMp3(MUSIC_CRYWOLF_BEFORE);
            PlayMp3(MUSIC_CRYWOLF_READY);

            if (IsEndMp3())
                StopMp3(MUSIC_CRYWOLF_READY);
        }
        else if (m_CrywolfState == CRYWOLF_STATE_START)
        {
            StopMp3(MUSIC_CRYWOLF_READY);
            PlayMp3(MUSIC_CRYWOLF_BACK);

            if (IsEndMp3())
                StopMp3(MUSIC_CRYWOLF_BACK);
        }
        else if (m_CrywolfState == CRYWOLF_STATE_END)
        {
            StopMp3(MUSIC_CRYWOLF_READY);
            StopMp3(MUSIC_CRYWOLF_BACK);

            if (Add_Num == 10)
                PlayBuffer(SOUND_CRY1ST_FAILED);
            else
                PlayBuffer(SOUND_CRY1ST_SUCCESS);
        }
        else
        {
            PlayMp3(MUSIC_BC_CRYWOLF_1ST);

            if (IsEndMp3())
                StopMp3(MUSIC_BC_CRYWOLF_1ST);
        }
    }
    else
    {
        StopMp3(MUSIC_BC_CRYWOLF_1ST);
    }
}

bool CGMCrywolf1st::AdvanceCryWolf1stObjectVisual(OBJECT *o, BMD *b)
{
    if (!IsCyrWolf1st())
        return false;

    vec3_t Light, p;
    float Scale, Luminosity;
    Vector(0.f, 0.f, 0.f, p);
    switch (o->Type)
    {
    case 56: {
        if (m_OccupationState == CRYWOLF_OCCUPATION_STATE_PEACE)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
            {
                const float color = WorldRandom() % 10000 * 0.0001f;
                Vector(color, color, color, Light);
                CreateEffect(BITMAP_MAGIC, o->Position, o->Angle, Light, 3, o, 4.0f);
            }
        }
    }
    break;
    case 57: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_TRUE_FIRE, o->Position, o->Angle, Light, 5, o->Scale);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 21, o->Scale);
        }
    }
    break;
    case 71: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_TRUE_FIRE, o->Position, o->Angle, Light, 5, o->Scale);
        }
    }
    break;
    case 72: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.f))
        {
            vec3_t Position;
            VectorCopy(o->Position, Position);
            Position[2] += 50.0f;
            Luminosity = (float)(WorldRandom() % 4 + 3) * 0.1f;
            Vector(Luminosity, Luminosity * 0.6f, Luminosity * 0.2f, Light);
            CreateParticle(BITMAP_SMOKE, Position, o->Angle, Light, 33, o->Scale);
        }
    }
    break;
    case 73: {
        if (weather == 0)
            ashies = true;
    }
    break;
    case 74: {
        if (m_OccupationState == CRYWOLF_OCCUPATION_STATE_OCCUPIED)
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
            {
                Vector(1.f, 1.f, 1.f, Light);
                CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 21, o->Scale);
            }
    }
    break;
    case 77: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 6.f))
        {
            //	Vector(0.02f,0.02f,0.03f,Light);
            Vector(0.02f, 0.03f, 0.02f, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 9, o->Scale, o);
        }
    }
    break;
    case 78: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 6.f))
        {
            Vector(0.03f, 0.06f, 0.05f, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 9, o->Scale, o);
        }
    }
    break;

    case 41:
        AdvanceFlare(*o);
        break;
    case 37: {
    }
    break;
    case 84:
        if (m_OccupationState == CRYWOLF_OCCUPATION_STATE_OCCUPIED)
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
            {
                Vector(1.f, 1.f, 1.f, Light);
                CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 21, o->Scale);
            }
        break;
    }

    return true;
}

void CGMCrywolf1st::AdvanceBallistaState(OBJECT &object)
{
    if (m_CrywolfState == CRYWOLF_STATE_START && rand_fps_check(100))
        SetAction(&object, MONSTER01_ATTACK1);
}

bool CGMCrywolf1st::MoveCryWolf1stMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                                WorldCharacterVisualState &visual)
{
    ObjectDrawInput presentation(o);
    visual.movement.Apply(presentation);
    switch (o->Type)
    {
    case MODEL_SCOUT: {
        float fActionSpeed =
            b->Actions[visual.action].PlaySpeed * static_cast<float>(FPS_ANIMATION_FACTOR);
        float fAnimationFrame = visual.animationFrame - fActionSpeed;
        b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame, o->PriorAction,
                            o->Angle, presentation.headAngle);
        vec3_t Light{1.f, 1.f, 1.f};
        if (visual.action == MONSTER01_ATTACK2)
        {
            vec3_t EndPos, EndPos1, EndRelative;
            if (visual.animationFrame >= 5.5f && visual.animationFrame < (5.5f + fActionSpeed))
            {
                Vector(0.f, 0.f, 0.f, EndRelative);
                b->TransformPosition(BoneTransform[24], EndRelative, EndPos, false);
                b->TransformPosition(BoneTransform[16], EndRelative, EndPos1, false);
                Vector(1.f, 1.f, 1.f, Light);

                for (int iu = 0; iu < 6; iu++)
                {
                    CreateEffectFpsChecked(MODEL_STONE2, EndPos, o->Angle, visual.movement.light);
                    CreateEffectFpsChecked(MODEL_STONE2, EndPos1, o->Angle, visual.movement.light);
                }
            }
        }
        else if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.f))
            {
                vec3_t position;
                o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, position);
                CreateParticle(BITMAP_SMOKE + 1, position, o->Angle, Light);
            }
        }
    }
    break;
    case MODEL_SORAM: {
        float fActionSpeed =
            b->Actions[visual.action].PlaySpeed * static_cast<float>(FPS_ANIMATION_FACTOR);
        vec3_t Light{1.f, 1.f, 1.f};
        vec3_t EndPos, EndRelative;
        Vector(1.f, 1.f, 1.f, Light);

        if (visual.action == MONSTER01_ATTACK1)
        {
            if (visual.animationFrame >= 6.5f && visual.animationFrame < (6.5f + fActionSpeed) &&
                rand_fps_check(1))
            {
                Vector(0.0f, 0.0f, 0.0f, EndRelative);
                b->TransformPosition(presentation.bones[27], EndRelative, EndPos, true);
                CreateEffect(BITMAP_CRATER, EndPos, o->Angle, visual.movement.light, 2);
                CreateParticle(BITMAP_BLUE_BLUR, EndPos, o->Angle, Light, 1);
                CreateParticle(BITMAP_BLUE_BLUR, EndPos, o->Angle, Light, 1);
                Vector(1.0f, 0.6f, 0.4f, Light);
                CreateEffect(BITMAP_SHOCK_WAVE, EndPos, o->Angle, Light, 8);
                CreateEffect(BITMAP_SHOCK_WAVE, EndPos, o->Angle, Light, 8);
                CreateEffect(BITMAP_SHOCK_WAVE, EndPos, o->Angle, Light, 8);
                CreateEffect(BITMAP_SHOCK_WAVE, EndPos, o->Angle, Light, 8);
                CreateEffect(BITMAP_SHOCK_WAVE, EndPos, o->Angle, Light, 8);

                for (int iu = 0; iu < 4; iu++)
                {
                    CreateEffect(MODEL_BIG_STONE1, EndPos, o->Angle, visual.movement.light, 10);
                    CreateEffect(MODEL_STONE2, EndPos, o->Angle, visual.movement.light);
                }
            }
        }
        else if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.f))
            {
                vec3_t position;
                o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, position);
                CreateParticle(BITMAP_SMOKE + 1, position, o->Angle, Light);
            }
        }
    }
    break;
    case MODEL_BALGASS: {
        float fActionSpeed =
            b->Actions[visual.action].PlaySpeed * static_cast<float>(FPS_ANIMATION_FACTOR);
        float fAnimationFrame = visual.animationFrame - fActionSpeed;
        b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame, o->PriorAction,
                            o->Angle, presentation.headAngle);
        vec3_t Light{1.f, 1.f, 1.f};

        if (visual.action == MONSTER01_ATTACK2)
        {
            vec3_t EndPos, EndRelative;
            if (visual.animationFrame >= 7.5f && visual.animationFrame < (7.5f + fActionSpeed) &&
                rand_fps_check(1))
            {
                Vector(0.f, 0.f, 0.f, EndRelative);
                b->TransformPosition(BoneTransform[33], EndRelative, EndPos, false);
                Vector(1.f, 1.f, 1.f, Light);
                CreateEffect(BITMAP_CRATER, EndPos, o->Angle, visual.movement.light, 2);
                CreateParticle(BITMAP_EXPLOTION, EndPos, o->Angle, Light, 2);
                for (int iu = 0; iu < 6; iu++)
                    CreateEffect(MODEL_BIG_STONE1, EndPos, o->Angle, visual.movement.light, 10);
                //						CreateEffect ( MODEL_STONE2,EndPos,o->Angle,visual.movement.light);
            }
        }
        else if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.f))
            {
                vec3_t position;
                o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, position);
                CreateParticle(BITMAP_SMOKE + 1, position, o->Angle, Light);
            }
        }
    }
    break;
    case MODEL_BALRAM: {
        vec3_t Light{1.f, 1.f, 1.f};
        Vector(0.9f, 0.2f, 0.1f, Light);
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.f))
            {
                vec3_t position;
                o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, position);
                CreateParticle(BITMAP_SMOKE + 1, position, o->Angle, Light);
            }
        }
    }
    break;
    case MODEL_DARK_ELF_1: {
        if (m_CrywolfState == CRYWOLF_STATE_START && Dark_Elf_Check)
        {
            Dark_Elf_Check = false;

            vec3_t Light{1.f, 1.f, 1.f};
            vec3_t EndPos, EndRelative;

            Vector(1.f, 0.2f, 0.2f, Light);
            Vector(0.0f, 0.0f, 0.0f, EndRelative);
            b->TransformPosition(presentation.bones[27], EndRelative, EndPos, true);
            CreateEffect(BITMAP_CRATER, EndPos, o->Angle, visual.movement.light, 2);
            CreateParticle(BITMAP_BLUE_BLUR, EndPos, o->Angle, Light, 1);
            CreateParticle(BITMAP_BLUE_BLUR, EndPos, o->Angle, Light, 1);
            Vector(1.0f, 0.2f, 0.2f, Light);
            CreateEffect(BITMAP_SHOCK_WAVE, EndPos, o->Angle, Light, 8);
            CreateEffect(BITMAP_SHOCK_WAVE, EndPos, o->Angle, Light, 8);
            CreateEffect(BITMAP_SHOCK_WAVE, EndPos, o->Angle, Light, 8);
            CreateEffect(BITMAP_SHOCK_WAVE, EndPos, o->Angle, Light, 8);
            CreateEffect(BITMAP_SHOCK_WAVE, EndPos, o->Angle, Light, 8);
        }
    }
    break;
    case MODEL_DEATH_SPIRIT: {
        vec3_t Light{1.f, 1.f, 1.f};
        Vector(0.9f, 0.2f, 0.1f, Light);
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.f))
            {
                vec3_t position;
                o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, position);
                CreateParticle(BITMAP_SMOKE + 1, position, o->Angle, Light);
            }
        }
    }
    break;
    case MODEL_WEREWOLF_HERO: {
        //CreateEffect ( MODEL_SKILL_FURY_STRIKE, o->Position, o->Angle, visual.movement.light, 1, o, -1, 0, 1 );
        vec3_t Position, Light;

        if (visual.action != MONSTER01_DIE)
        {
            Vector(0.9f, 0.2f, 0.1f, Light);
            GetBonePosition(presentation, CharacterSocket::Monster95_Head, Position);
            CreateSprite(BITMAP_LIGHT, Position, 3.5f, Light, o);
        }

        float fActionSpeed =
            b->Actions[visual.action].PlaySpeed * static_cast<float>(FPS_ANIMATION_FACTOR);
        vec3_t EndPos, EndRelative;
        Vector(1.f, 1.f, 1.f, Light);

        //			CreateParticle(BITMAP_FIRE,EndPos,o->Angle,Light);
        if (visual.action == MONSTER01_ATTACK1)
        {
            if (visual.animationFrame >= 2.5f && visual.animationFrame < (2.5f + fActionSpeed) &&
                rand_fps_check(1))
            {
                Vector(0.0f, 0.0f, 100.0f, EndRelative);
                b->TransformPosition(presentation.bones[20], EndRelative, EndPos, true);
                if (o->Scale > 1.25f)
                {
                    //						Vector ( 0.f, 0.f, 1.f, Light );
                    CreateEffect(MODEL_SKILL_FURY_STRIKE, EndPos, o->Angle, Light, 1, o, -1, 0, 1);
                }
            }
        }
        else
            //			Vector ( 0.9f, 0.2f, 0.1f, Light );
            //. Walking & Running Scene Processing
            if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
            {
                for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.f))
                {
                    vec3_t position;
                    o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, position);
                    CreateParticle(BITMAP_SMOKE + 1, position, o->Angle, Light);
                }
            }
    }
    break;
    case MODEL_SOLAM: {
        vec3_t Position, Light;

        if (visual.action == MONSTER01_ATTACK2 && visual.animationFrame >= 4.0f &&
            visual.animationFrame <= 6.0f)
        {
            float Matrix[3][4];
            AngleMatrix(o->Angle, Matrix);
            VectorRotate(o->Direction, Matrix, Position);

            Vector(1.f, 0.0f, 0.5f, Light);
            CreateEffectFpsChecked(MODEL_PIERCING2, o->Position, o->Angle, Light, 1);
        }
    }
    break;
    case MODEL_VALAM: {
        vec3_t Position, Light;

        auto Rotation = (float)(WorldRandom() % 360);
        float Luminosity = sinf(WorldTime * 0.0012f) * 0.8f + 1.3f;

        float fScalePercent = 1.f;
        if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
            fScalePercent = .5f;

        GetBonePosition(presentation, CharacterSocket::Monster96_Center, Position);
        Vector(Luminosity * 0.f, Luminosity * 0.5f, Luminosity * 0.1f, Light);
        CreateSprite(BITMAP_LIGHT, Position, fScalePercent, Light, o);

        Vector(0.5f, 0.5f, 0.5f, Light);

        GetBonePosition(presentation, CharacterSocket::Monster96_Top, Position);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f * fScalePercent, Light, o, Rotation);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f * fScalePercent, Light, o, 360.f - Rotation);

        GetBonePosition(presentation, CharacterSocket::Monster96_Bottom, Position);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f * fScalePercent, Light, o, Rotation);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f * fScalePercent, Light, o, 360.f - Rotation);
    }
    break;
    case MODEL_BALLISTA: {
        if (m_CrywolfState == CRYWOLF_STATE_START)
        {
            if (visual.action == MONSTER01_ATTACK1 && visual.animationFrame >= 5.f &&
                visual.intervalStartFrame < 5.f)
            {
                CreateEffect(MODEL_ARROW_TANKER, o->Position, o->Angle, visual.movement.light, 1, o,
                             o->PKKey);

                return true;
            }
        }
    }
    break;
    }
    return false;
}

bool CGMCrywolf1st::AttackEffectCryWolf1stMonster(CHARACTER *c, OBJECT *o, BMD *b)
{
    //CreateJoint ( BITMAP_FLARE+1, o->Position, o->Position, o->Angle, 6, o, 20.f, 40 );
    OBJECT *to = NULL;

    if (!IsCyrWolf1st() && !(gMapManager.InDevilSquare()))
        return false;

    switch (o->Type)
    {
    case MODEL_VALAM: {
        if (c->CheckAttackTime(14))
        {
            CreateEffect(MODEL_ARROW_NATURE, o->Position, o->Angle, o->Light, 1, o, o->PKKey);
            c->SetLastAttackEffectTime();
            return true;
        }
    }
    break;
    case MODEL_BALRAM: {
        if (c->CheckAttackTime(14))
        {
            CreateEffect(MODEL_ARROW_HOLY, o->Position, o->Angle, o->Light, 1, o, o->PKKey);
            c->SetLastAttackEffectTime();
            return true;
        }
    }
    break;
    case MODEL_TANTALLOS: {
        vec3_t Angle;
        if (c->CheckAttackTime(1))
        {
            CreateInferno(o->Position);
            c->SetLastAttackEffectTime();
        }
        if (c->CheckAttackTime(14))
        {
            if (c->MonsterIndex == MONSTER_ZAIKAN)
            {
                if ((c->Skill) == AT_SKILL_BOSS)
                {
                    for (int i = 0; i < 18; i++)
                    {
                        VectorCopy(o->Angle, Angle);
                        Angle[2] += i * 20.f;
                        CreateEffect(MODEL_STAFF_OF_DESTRUCTION, o->Position, Angle, o->Light);
                    }
                }
            }

            c->SetLastAttackEffectTime();
        }
    }
    break;
    case MODEL_BEAM_KNIGHT: {
        vec3_t p, Position, Angle;
        for (int i = 0; i < 6; i++)
        {
            int Hand = 0;
            if (i >= 3)
                Hand = 1;
            b->TransformPosition(o->BoneTransform[c->Weapon[Hand].LinkBone], p, Position, true);
            Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);

            if (to != NULL)
            {
                CreateJointFpsChecked(BITMAP_JOINT_THUNDER, Position, to->Position, Angle, 2, to,
                                      50.f);
                CreateJointFpsChecked(BITMAP_JOINT_THUNDER, Position, to->Position, Angle, 2, to,
                                      10.f);
            }
        }
        if (c->CheckAttackTime(1))
        {
            PlayBuffer(SOUND_EVIL);
            c->SetLastAttackEffectTime();
        }

        for (int i = 0; i < 4; i++)
        {
            int Hand = 0;
            if (i >= 2)
                Hand = 1;
            b->TransformPosition(o->BoneTransform[c->Weapon[Hand].LinkBone], p, Position, true);
            Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);
            if (to != NULL)
                CreateJointFpsChecked(BITMAP_JOINT_LASER + 1, Position, to->Position, Angle, 0, to,
                                      50.f);
            CreateParticleFpsChecked(BITMAP_FIRE, Position, o->Angle, o->Light);
        }
    }
    break;
    case MODEL_BALLISTA: {
        if (c->CheckAttackTime(15))
        {
            CreateEffect(MODEL_ARROW_TANKER, o->Position, o->Angle, o->Light, 1, o, o->PKKey);
            c->SetLastAttackEffectTime();
            return true;
        }
    }
    break;
    }
    return false;
}

void CGMCrywolf1st::MoveCryWolf1stBlurEffect(CHARACTER *c, OBJECT *o, BMD *b)
{
    //CreateEffect ( MODEL_STONE2,o->Position,o->Angle,o->Light);
    vec3_t Angle, Position;
    float Matrix[3][4];
    vec3_t p, p2, EndPos;
    vec3_t TempAngle;
    switch (o->Type)
    {
    case MODEL_SORAM: {
        vec3_t Light;
        Vector(1.0f, 1.0f, 1.0f, Light);

        if (o->CurrentAction == MONSTER01_ATTACK2)
        {
            //				vec3_t StartPos;//, StartRelative;
            vec3_t EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            VectorCopy(o->Angle, TempAngle);
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 100.f, -150.f, EndRelative);

                b->TransformPosition(BoneTransform[16], EndRelative, EndPos, false);

                if (o->AnimationFrame > 5.0f && o->AnimationFrame < 7.0f)
                {
                    CreateParticle(BITMAP_FIRE, EndPos, o->Angle, Light);
                    //						CreateParticle(BITMAP_BLUE_BLUR,EndPos, o->Angle, Light,1);
                }

                Vector(0.f, -150.f, 0.f, p);
                AngleMatrix(o->Angle, Matrix);
                VectorRotate(p, Matrix, Position);
                VectorAdd(Position, o->Position, p2);

                o->Angle[2] -= 18; //

                Vector((float)(WorldRandom() % 60 + 60 - 90), 0.f, (float)(WorldRandom() % 30 + 90),
                       Angle);
                VectorAdd(Angle, o->Angle, Angle);
                VectorCopy(p2, Position);
                //					Position[0] += WorldRandom()%20-10;
                //					Position[1] += WorldRandom()%20-10;

                fAnimationFrame += fSpeedPerFrame;
            }
            VectorCopy(TempAngle, o->Angle);
        }
    }
    break;
    case MODEL_SCOUT: {
        if (o->CurrentAction == MONSTER01_ATTACK1)
        {
            vec3_t Light;
            Vector(1.0f, 1.0f, 1.0f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                //					Vector(0.f, 0.f, -60.f, StartRelative);
                //					Vector(0.f, 0.f, -150.f, EndRelative);
                Vector(20.f, 0.f, 0.f, StartRelative);
                Vector(60.f, 0.f, 0.f, EndRelative);
                //					Vector(20.f, 0.f, 0.f, StartRelative);
                //					Vector(60.f, 0.f, 0.f, EndRelative);

                b->TransformPosition(BoneTransform[12], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[16], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 3, true, 23);

                b->TransformPosition(BoneTransform[20], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[24], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 3, true, 24);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_BALGASS: {
        if (o->CurrentAction == MONSTER01_ATTACK1)
        {
            vec3_t Light;
            Vector(1.f, 1.f, 1.f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;

            VectorCopy(o->Angle, TempAngle);
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 0.f, -60.f, StartRelative);
                Vector(0.f, 0.f, -150.f, EndRelative);
                b->TransformPosition(BoneTransform[c->Weapon[0].LinkBone], StartRelative, StartPos,
                                     false);
                b->TransformPosition(BoneTransform[c->Weapon[0].LinkBone], EndRelative, EndPos,
                                     false);

                if (o->AnimationFrame > 3.5f && o->AnimationFrame < 4.5f)
                {
                    CreateParticle(BITMAP_BLUE_BLUR, EndPos, o->Angle, Light, 0);
                }
                Vector(0.f, 0.f, 5.f, Light);
                CreateBlur(c, StartPos, EndPos, Light, 3, true, 80);

                fAnimationFrame += fSpeedPerFrame;
            }
            VectorCopy(TempAngle, o->Angle);
        }
    }
    break;

    case MODEL_DARK_ELF_1: {
        if ((o->CurrentAction == MONSTER01_ATTACK1 || o->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t Light;
            Vector(1.f, 1.f, 1.f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;

            if (o->CurrentAction == MONSTER01_ATTACK2 &&
                (o->AnimationFrame > 4.5f && o->AnimationFrame < 5.0f))
                CreateEffect(MODEL_DARK_ELF_SKILL, o->Position, o->Angle, o->Light, 2, o);

            VectorCopy(o->Angle, TempAngle);
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 0.f, -60.f, StartRelative);
                Vector(0.f, 0.f, -150.f, EndRelative);
                b->TransformPosition(BoneTransform[c->Weapon[0].LinkBone], StartRelative, StartPos,
                                     false);
                b->TransformPosition(BoneTransform[c->Weapon[0].LinkBone], EndRelative, EndPos,
                                     false);
                CreateBlur(c, StartPos, EndPos, Light, 3, true, 80);

                fAnimationFrame += fSpeedPerFrame;
            }
            VectorCopy(TempAngle, o->Angle);
        }
        else if (o->CurrentAction == MONSTER01_ATTACK3)
        {
            vec3_t Position, Light;
            GetBonePosition(o, CharacterSocket::Left_Hand, Position);

            Vector(0.2f, 0.2f, 0.7f, Light);
            CreateParticle(BITMAP_SMOKE, Position, o->Angle, Light, 27, 1.0f);
            Vector(0.3f, 0.3f, 0.4f, Light);
            CreateParticle(BITMAP_LIGHT + 1, Position, o->Angle, Light, 2, 0.8f);
            CreateParticle(BITMAP_LIGHT + 1, Position, o->Angle, Light, 2, 0.6f);
        }
    }
    break;
    case MODEL_DEATH_SPIRIT: {
        if ((o->CurrentAction == MONSTER01_ATTACK1 || o->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t Light;
            Vector(0.2f, 1.f, 0.4f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;

            VectorCopy(o->Angle, TempAngle);
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 250.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);
                b->TransformPosition(BoneTransform[27], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[27], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 3, true, 80);

                fAnimationFrame += fSpeedPerFrame;
            }
            VectorCopy(TempAngle, o->Angle);

            if (o->AnimationFrame > 4.5f && o->AnimationFrame < 5.0f)
            {
                CHARACTER *tc = &CharactersClient[c->TargetCharacter];
                OBJECT *to = &tc->Object;
                vec3_t Angle = {0.f, 0.f, o->Angle[2]};
                vec3_t Pos = {0.f, 0.f, (to->BoundingBoxMax[2] / 1.f)};

                Vector(80.f, 0.f, 20.f, p);
                b->TransformPosition(o->BoneTransform[0], p, Position, true);
                Position[2] += 50.0f;
                Angle[2] = o->Angle[2] + 90;
                CreateEffect(MODEL_DEATH_SPI_SKILL, Position, Angle, Pos, 0, to);
                CreateEffect(MODEL_DEATH_SPI_SKILL, Position, Angle, Pos, 1, to);
                Angle[2] = o->Angle[2] - 90;
                CreateEffect(MODEL_DEATH_SPI_SKILL, Position, Angle, Pos, 0, to);
                CreateEffect(MODEL_DEATH_SPI_SKILL, Position, Angle, Pos, 1, to);
            }
        }
    }
    break;
    case MODEL_WEREWOLF_HERO: {
        if (o->CurrentAction == MONSTER01_ATTACK2)
        {
            vec3_t Light;
            Vector(1.f, 1.f, 1.f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 0.f, -90.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);
                b->TransformPosition(BoneTransform[80], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[80], EndRelative, EndPos, false);

                CreateBlur(c, StartPos, EndPos, Light, 3, true, 80);

                Vector(0.f, 0.f, 90.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);
                b->TransformPosition(BoneTransform[82], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[82], EndRelative, EndPos, false);

                CreateBlur(c, StartPos, EndPos, Light, 3, true, 84);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_SOLAM: {
        if (o->CurrentAction == MONSTER01_ATTACK1)
        {
            vec3_t Light;
            Vector(1.f, 1.f, 1.f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 0.f, 120.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);
                b->TransformPosition(BoneTransform[25], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[25], EndRelative, EndPos, false);

                CreateBlur(c, StartPos, EndPos, Light, 3, true, 25);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    }
}

bool CGMCrywolf1st::AdvanceCryWolf1stMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                                   WorldCharacterVisualState &visual)
{
    vec3_t emissionLight;
    VectorCopy(o->Light, emissionLight);

    if (!IsCyrWolf1st() && !(gMapManager.InDevilSquare()))
        return false;

    switch (o->Type)
    {
    case MODEL_CRYWOLF_ALTAR1:
    case MODEL_CRYWOLF_ALTAR2:
    case MODEL_CRYWOLF_ALTAR3:
    case MODEL_CRYWOLF_ALTAR4:
    case MODEL_CRYWOLF_ALTAR5:
        if (g_isCharacterBuff(o, eBuff_CrywolfAltarContracted))
        {
            Vector(0.09f, 0.09f, 0.04f, emissionLight);
            CreateParticleFpsChecked(BITMAP_EFFECT, o->Position, o->Angle, emissionLight);
            CreateParticleFpsChecked(BITMAP_EFFECT, o->Position, o->Angle, emissionLight, 1);
        }
        break;
    case MODEL_BALGASS: {
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_CRY1ST_BALGAS_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_BALGAS_ATTACK1);
            }
        }
        else if (visual.action == MONSTER01_ATTACK2)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_BALGAS_ATTACK2);
            }
        }
        else if (visual.action == MONSTER01_ATTACK3)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_BALGAS_SKILL1);
            }
        }
        else if (visual.action == MONSTER01_ATTACK4)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_BALGAS_SKILL2);
            }
        }
        else if (visual.action == MONSTER01_DIE)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_BALGAS_DIE);
            }
        }

        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;
    }
    break;
    case MODEL_DARK_ELF_1: {
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_CRY1ST_DARKELF_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_DARKELF_ATTACK1);
            }
        }
        else if (visual.action == MONSTER01_ATTACK2)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_DARKELF_ATTACK2);
            }
        }
        else if (visual.action == MONSTER01_ATTACK3)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_DARKELF_SKILL1);
            }
        }
        else if (visual.action == MONSTER01_ATTACK4)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_DARKELF_SKILL2);
            }
        }
        else if (visual.action == MONSTER01_DIE)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_DARKELF_DIE);
            }
        }

        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;
    }
    break;
    case MODEL_BALLISTA: {
        if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_TANKER_ATTACK1);
            }
        }
        else if (visual.action == MONSTER01_DIE)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_TANKER_DIE);
            }
        }

        if (!(visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_DIE))
            visual.soundSubType = FALSE;
    }
    break;
    case MODEL_DEATH_SPIRIT: {
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_CRY1ST_DEATHSPIRIT_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_DEATHSPIRIT_ATTACK1);
            }
        }
        else if (visual.action == MONSTER01_ATTACK2)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_DEATHSPIRIT_ATTACK2);
            }
        }
        else if (visual.action == MONSTER01_DIE)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_DEATHSPIRIT_DIE);
            }
        }

        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;

        vec3_t Position, Light;
        int dummy = WorldRandom() % 14;
        auto Data = (float)((float)dummy / (float)100);
        auto Rot = (float)(WorldRandom() % 360);
        Vector(1.0f, 1.0f, 1.0f, Light);
        GetBonePosition(o, CharacterSocket::Monster94_zx, Position);
        CreateSprite(BITMAP_DS_EFFECT, Position, 1.5f, Light, o);
        Vector(0.3f, 0.3f, 0.7f, Light);
        CreateSprite(BITMAP_LIGHT, Position, 3.5f + (Data * 5), Light, o);
        CreateSprite(BITMAP_LIGHT, Position, 3.5f + (Data * 5), Light, o);
        CreateSprite(BITMAP_LIGHT, Position, 3.5f + (Data * 5), Light, o);
        CreateSprite(BITMAP_DS_SHOCK, Position, 0.8f + Data, Light, o, Rot);

        GetBonePosition(o, CharacterSocket::Monster94_zx01, Position);
        Vector(0.1f, 0.0f, 0.6f, Light);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.8f, Light, o, Rot);
        if (rand_fps_check(2))
        {
            Vector(0.7f, 0.7f, 1.0f, Light);
            CreateSprite(BITMAP_SHINY + 1, Position, 0.8f, Light, o, 360.f - Rot);
        }
        Vector(0.3f, 0.3f, 0.7f, Light);
        CreateSprite(BITMAP_LIGHT, Position, 1.5f, Light, o);
    }
        return true;
    case MODEL_BALRAM: {
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_CRY1ST_BALRAM_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_BALRAM_ATTACK1);
            }
        }
        else if (visual.action == MONSTER01_ATTACK2)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_BALRAM_ATTACK2);
            }
        }
        else if (visual.action == MONSTER01_DIE)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_BALRAM_DIE);
            }
        }
        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;
    }
        return true;
    case MODEL_SORAM: {
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_CRY1ST_SORAM_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_SORAM_ATTACK1);
            }
        }
        else if (visual.action == MONSTER01_ATTACK2)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_SORAM_ATTACK2);
            }
        }
        else if (visual.action == MONSTER01_DIE)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_SORAM_DIE);
            }
        }
        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;
    }
        return true;
    case MODEL_WEREWOLF_HERO: {
        vec3_t Position, Light;

        if (visual.action != MONSTER01_DIE)
        {
            Vector(0.9f, 0.2f, 0.1f, Light);
            GetBonePosition(o, CharacterSocket::Monster95_Head, Position);
            CreateSprite(BITMAP_LIGHT, Position, 3.5f, Light, o);
        }

        Vector(0.9f, 0.2f, 0.1f, Light);
        //. Walking & Running Scene Processing
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.f))
            {
                vec3_t position;
                o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, position);
                CreateParticle(BITMAP_SMOKE + 1, position, o->Angle, Light);
            }
        }

        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_CRY1ST_WWOLF_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_WWOLF_ATTACK1);
            }
        }
        else if (visual.action == MONSTER01_ATTACK2)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_WWOLF_ATTACK2);
            }
        }
        else if (visual.action == MONSTER01_DIE)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_WWOLF_DIE);
            }
        }
        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;
    }
        return true;
    case MODEL_SOLAM: {
        vec3_t Position, Light;

        if (visual.action == MONSTER01_ATTACK2 && visual.animationFrame >= 4.0f &&
            visual.animationFrame <= 6.0f)
        {
            float Matrix[3][4];
            AngleMatrix(o->Angle, Matrix);
            VectorRotate(o->Direction, Matrix, Position);

            Vector(1.f, 0.0f, 0.5f, Light);
            CreateEffect(MODEL_PIERCING2, o->Position, o->Angle, Light, 1);
        }
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_CRY1ST_SCOUT2_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_SCOUT2_ATTACK1);
            }
        }
        else if (visual.action == MONSTER01_ATTACK2)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_SCOUT2_ATTACK2);
            }
        }
        else if (visual.action == MONSTER01_DIE)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_SCOUT2_DIE);
            }
        }
        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;
    }
        return true;
    case MODEL_VALAM: {
        vec3_t Position, Light;

        auto Rotation = (float)(WorldRandom() % 360);
        float Luminosity = sinf(WorldTime * 0.0012f) * 0.8f + 1.3f;

        float fScalePercent = 1.f;
        if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
            fScalePercent = .5f;

        GetBonePosition(o, CharacterSocket::Monster96_Center, Position);
        Vector(Luminosity * 0.f, Luminosity * 0.5f, Luminosity * 0.1f, Light);
        CreateSprite(BITMAP_LIGHT, Position, fScalePercent, Light, o);

        Vector(0.5f, 0.5f, 0.5f, Light);

        GetBonePosition(o, CharacterSocket::Monster96_Top, Position);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f * fScalePercent, Light, o, Rotation);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f * fScalePercent, Light, o, 360.f - Rotation);

        GetBonePosition(o, CharacterSocket::Monster96_Bottom, Position);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f * fScalePercent, Light, o, Rotation);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f * fScalePercent, Light, o, 360.f - Rotation);

        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_CRY1ST_SCOUT3_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_SCOUT3_ATTACK1);
            }
        }
        else if (visual.action == MONSTER01_ATTACK2)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_SCOUT3_ATTACK2);
            }
        }
        else if (visual.action == MONSTER01_DIE)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_SCOUT3_DIE);
            }
        }

        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;
    }
        return true;
    case MODEL_SCOUT: {
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_CRY1ST_SCOUT1_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_SCOUT1_ATTACK1);
            }
        }
        else if (visual.action == MONSTER01_ATTACK2)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_SCOUT1_ATTACK2);
            }
        }
        else if (visual.action == MONSTER01_DIE)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(SOUND_CRY1ST_SCOUT1_DIE);
            }
        }
        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;
    }
        return true;
    }
    return false;
}

bool CGMCrywolf1st::CreateMist(PARTICLE *pParticleObj)
{
    if (!IsCyrWolf1st())
        return false;

    pParticleObj->Live = false;
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 30.f))
    {
        vec3_t origin, angle;
        Hero->Object.MotionTrace.Sample(WorldTime, birth.FrameFraction(), Hero->Object.Position,
                                        origin);
        VectorCopy(Hero->Object.Angle, angle);
        angle[2] = Hero->Object.MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), angle[2]);
        vec3_t Light;
        Vector(0.07f, 0.07f, 0.07f, Light);
        int ff = 200.0f;

        vec3_t TargetPosition = {0.f, 0.f, 400.f}, TargetAngle = {0.f, 0.f, 0.f};
        switch (WorldRandom() % 8)
        {
        case 0:
            TargetPosition[0] = origin[0] + (300 - ff + WorldRandom() % 250);
            TargetPosition[1] = origin[1] + (300 - ff + WorldRandom() % 250);
            break;
        case 1:
            TargetPosition[0] = origin[0] + (250 - ff + WorldRandom() % 250);
            TargetPosition[1] = origin[1] - (250 - ff + WorldRandom() % 250);
            break;
        case 2:
            TargetPosition[0] = origin[0] - (200 - ff + WorldRandom() % 250);
            TargetPosition[1] = origin[1] + (200 - ff + WorldRandom() % 250);
            break;
        case 3:
            TargetPosition[0] = origin[0] - (300 - ff + WorldRandom() % 250);
            TargetPosition[1] = origin[1] - (300 - ff + WorldRandom() % 250);
            break;
        case 4:
            TargetPosition[0] = origin[0] + (400 - ff + WorldRandom() % 250);
            TargetPosition[1] = origin[1];
            break;
        case 5:
            TargetPosition[0] = origin[0] - (400 - ff + WorldRandom() % 250);
            TargetPosition[1] = origin[1];
            break;
        case 6:
            TargetPosition[0] = origin[0];
            TargetPosition[1] = origin[1] + (400 - ff + WorldRandom() % 250);
            break;
        case 7:
            TargetPosition[0] = origin[0];
            TargetPosition[1] = origin[1] - (400 - ff + WorldRandom() % 250);
            break;
        }

        if (Hero->Movement)
        {
            float Matrix[3][4];
            AngleMatrix(angle, Matrix);
            vec3_t Velocity, Direction;
            Vector(0.f, -45.f * CharacterMoveSpeed(Hero), 0.f, Velocity);
            VectorRotate(Velocity, Matrix, Direction);
            VectorAdd(TargetPosition, Direction, TargetPosition);
        }
        if (Hero->Movement || (WorldRandom() % 2 == 0))
        {
            CreateParticle(BITMAP_CLOUD, TargetPosition, TargetAngle, Light, 8, 0.4f);
        }
    }

    return true;
}
bool CGMCrywolf1st::AdvanceObjectVisual(OBJECT *object, BMD *model, float)
{
    return AdvanceCryWolf1stObjectVisual(object, model);
}

bool CGMCrywolf1st::AttackEffectMonster(CHARACTER *character, OBJECT *object, BMD *model)
{
    return AttackEffectCryWolf1stMonster(character, object, model);
}

bool CGMCrywolf1st::AdvanceMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                                         WorldCharacterVisualState &visual)
{
    return AdvanceCryWolf1stMonsterVisual(character, object, model, visual);
}

void CGMCrywolf1st::UpdateMusic()
{
    ChangeBackGroundMusic(gMapManager.ContextMap());
}

bool CGMCrywolf1st::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_BC_CRYWOLF_1ST) == 0 ||
           std::strcmp(track, MUSIC_CRYWOLF_BEFORE) == 0 ||
           std::strcmp(track, MUSIC_CRYWOLF_READY) == 0 ||
           std::strcmp(track, MUSIC_CRYWOLF_BACK) == 0;
}

bool CGMCrywolf1st::CreateWeather(PARTICLE *particle, int index)
{
    if (weather == 1)
        return TheMapProcess().DevilSquare().CreateWeather(particle, index);
    return weather == 2 && CreateMist(particle);
}

bool CGMCrywolf1st::MoveWeather(PARTICLE *particle)
{
    return weather == 1 && TheMapProcess().DevilSquare().MoveWeather(particle);
}

int CGMCrywolf1st::PrepareWeather()
{
    return weather == 1 ? 60 : weather == 2 ? 50 : 80;
}

void CGMCrywolf1st::ConfigureAmbientFish(OBJECT *o)
{
    if (Hero->SafeZone != true)
    {
        o->Type = MODEL_SCOLPION;
        o->Scale = (float)(WorldRandom() % 4 + 8) * 0.1f;
        o->Velocity = (((WorldRandom() % 8 + 1)) * 0.1f) / o->Scale;
        VectorCopy(o->Position, o->EyeLeft);
        o->Gravity = 1;
        o->LifeTime = 100;
        CreateJointFpsChecked(BITMAP_SCOLPION_TAIL, o->Position, o->Position, o->Angle, 0, o, 30.f);
    }
    else
        o->Live = false;
}

bool CGMCrywolf1st::CanCreateAmbientFish(int index)
{
    return TerrainWall[index] == 0 || TerrainWall[index] == TW_CHARACTER;
}

void CGMCrywolf1st::PrepareObjectEffects(int &, int)
{
    ashies = false;
    constexpr float FullTurn = 360.f;
    flareRotation_ = std::fmod(flareRotation_ + FPS_ANIMATION_FACTOR, FullTurn);
    constexpr float WeatherInterval = 70000.f;
    weatherChangeCounter_ += FPS_ANIMATION_FACTOR;
    while (weatherChangeCounter_ > WeatherInterval)
    {
        weather = WorldRandom() % 3;
        weatherChangeCounter_ -= WeatherInterval;
    }
}

void CGMCrywolf1st::AdvanceFlare(OBJECT &object)
{
    constexpr float HalfCycle = 20.f, Height = 350.f, Scale = 1.3f;
    const float bob = HalfCycle - std::abs(Time_Effect - HalfCycle);
    vec3_t position, light{1.f, 1.f, 1.f};
    VectorCopy(object.Position, position);
    position[2] += Height + bob;
    CreateSprite(BITMAP_FLARE, position, Scale, light, &object);
    CreateSprite(BITMAP_FLARE, position, Scale, light, &object, flareRotation_);
}

void CGMCrywolf1st::AdvanceTerrainEffects()
{
    constexpr int BlockSize = 4;
    for (int y = FrustrumBoundMinY; y <= FrustrumBoundMaxY; y += BlockSize)
        for (int x = FrustrumBoundMinX; x <= FrustrumBoundMaxX; x += BlockSize)
        {
            if (!TestFrustrum2D(x + 2.f, y + 2.f, g_fFrustumRange))
                continue;
            for (int row = 0; row < BlockSize; ++row)
                for (int column = 0; column < BlockSize; ++column)
                    AdvanceWaterTile(x + column, y + row);
        }
}

void CGMCrywolf1st::AdvanceWaterTile(int x, int y)
{
    constexpr int WaterTile = 5;
    const int index = TERRAIN_INDEX_REPEAT(x, y);
    if (TerrainMappingLayer1[index] != WaterTile || (TerrainWall[index] & TW_NOGROUND))
        return;
    if (!TestFrustrum2D(x + 0.5f, y + 0.5f, 0.f))
        return;
    const float alpha[] = {TerrainMappingAlpha[index],
                           TerrainMappingAlpha[TERRAIN_INDEX_REPEAT(x + 1, y)],
                           TerrainMappingAlpha[TERRAIN_INDEX_REPEAT(x + 1, y + 1)],
                           TerrainMappingAlpha[TERRAIN_INDEX_REPEAT(x, y + 1)]};
    if (alpha[0] >= 1.f && alpha[1] >= 1.f && alpha[2] >= 1.f && alpha[3] >= 1.f)
        return;
    const bool overlay = TerrainMappingLayer2[index] == WaterTile &&
                         (alpha[0] > 0.f || alpha[1] > 0.f || alpha[2] > 0.f || alpha[3] > 0.f);
    // Keep base/overlay emission order, but consume it once per simulation tick.
    for (int pass = 0; pass < (overlay ? 2 : 1); ++pass)
    {
        if (!rand_fps_check(50))
            continue;
        vec3_t position{x * TERRAIN_SCALE + float(WorldRandom() % 100 + 1),
                        y * TERRAIN_SCALE + float(WorldRandom() % 100 + 1),
                        Hero->Object.Position[2] + 10.f};
        vec3_t light{0.30f, 0.40f, 0.20f};
        CreateParticle(BITMAP_SPOT_WATER, position, Hero->Object.Angle, light, 0);
    }
}

bool GMDevias::AdvanceObjectVisual(OBJECT *o, BMD *b, float Luminosity)
{
    vec3_t p{}, Position{}, Light{};
    int Bitmap;
    float Scale, Rotation;
    switch (o->Type)
    {
    case 100:
        PrepareWorldObjectPose(*o);
        Vector(Luminosity * 1.f, Luminosity * 1.f, Luminosity * 1.f, Light);
        Rotation = (float)((int)(WorldTime * 0.1f) % 360);
        Vector(0.f, 0.f, 150.f, p);
        b->TransformPosition(BoneTransform[0], p, Position);
        CreateSprite(BITMAP_LIGHTNING + 1, Position, 2.5f, Light, o, Rotation);
        CreateSprite(BITMAP_LIGHTNING + 1, Position, 2.5f, Light, o, -Rotation);
        break;
    case 103: //. Sleddog
        if (b->CurrentAnimationFrame == b->Actions[o->CurrentAction].NumAnimationKeys - 1)
        {
            if (rand_fps_check(32))
                SetAction(o, 1);
            else
                SetAction(o, 0);
        }
    }
    return true;
}

void GMDevias::PrepareObjectUpdate(OBJECT *o)
{
    if (o->Type == 81 || o->Type == 82 || o->Type == 96 || o->Type == 98 || o->Type == 99)
    {
        if (HeroTile == 3 || HeroTile >= 10)
            o->AlphaTarget = 0.f;
        else
            o->AlphaTarget = 1.f;
    }
}

bool GMDevias::ObjectEffectsVisible(const OBJECT &object)
{
    if (object.Type != 100)
        return true;
    const int base = gCharacterManager.GetBaseClass(Hero->Class);
    const int level = base == CLASS_DARK || base == CLASS_DARK_LORD || base == CLASS_RAGEFIGHTER
                          ? 50 * 2 / 3
                          : 50;
    return CharacterAttribute->Level >= level;
}

void GMDevias::PlayAmbientSounds()
{
    if (HeroTile == 3 || HeroTile >= 10)
        StopBuffer(SOUND_WIND01, true);
    else
        PlayBuffer(SOUND_WIND01, NULL, true);
}

bool GMDevias::AllowsAmbientSound(ESound sound) const
{
    return (sound == SOUND_WIND01);
}

void GMDevias::UpdateMusic()
{
    if (Hero->SafeZone)
    {
        const bool church = Hero->PositionX >= 205 && Hero->PositionX <= 214 &&
                            Hero->PositionY >= 13 && Hero->PositionY <= 31;
        PlayMp3(church ? MUSIC_CHURCH : MUSIC_DEVIAS);
    }
}

bool GMDevias::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_CHURCH) == 0 || std::strcmp(track, MUSIC_DEVIAS) == 0;
}

bool GMDevias::CreateWeather(PARTICLE *o, int)
{
    o->Type = BITMAP_LEAF1;
    o->Scale = 5.f;
    if (Random.FpsCheck(10, 1.f))
    {
        o->Type = BITMAP_LEAF2;
        o->Scale = 10.f;
    }
    Vector(Hero->Object.Position[0] + Random.RangeFloat(-800, 799),
           Hero->Object.Position[1] + Random.RangeFloat(-500, 899),
           Hero->Object.Position[2] + Random.RangeFloat(200, 399), o->Position);
    Vector(-30.f, 0.f, 0.f, o->Angle);
    vec3_t Velocity;
    Vector(0.f, 0.f, -Random.RangeFloat(8, 23), Velocity);
    float Matrix[3][4];
    AngleMatrix(o->Angle, Matrix);
    VectorRotate(Velocity, Matrix, o->Velocity);

    return true;
}

ESound GMDevias::WalkingSound(int tile, bool safe) const
{
    return tile != 3 && tile < 10 ? SOUND_HUMAN_WALK_SNOW : SOUND_HUMAN_WALK_GROUND;
}

int GMDevias::PlayerNpcText(bool actionChanged)
{
    constexpr int textIds[] = {904, 905};
    if (actionChanged)
        playerNpcDeviasTextIndex = textIds[WorldRandom() % 2];
    return playerNpcDeviasTextIndex;
}

bool GMDevilSquare::AdvanceObjectVisual(OBJECT *o, BMD *b, float Luminosity)
{
    vec3_t p{}, Position{}, Light{};
    int Bitmap;
    float Scale, Rotation;
    switch (o->Type)
    {
    case 2: {
        constexpr int Bones[] = {23, 31, 23};
        constexpr float Rates[] = {0.25f, 0.25f, 1.f};
        Vector(1.f, 1.f, 1.f, Light);
        Vector(-15.f, 0.f, 0.f, p);
        for (int stream = 0; stream < 3; ++stream)
            for (auto birth :
                 sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR * Rates[stream]))
            {
                const float fraction = birth.FrameFraction();
                PrepareWorldObjectPose(*o, fraction);
                b->TransformPosition(BoneTransform[Bones[stream]], p, Position);
                CreateParticle(BITMAP_RAIN_CIRCLE + 1, Position, o->Angle, Light);
            }
    }
    break;
    }
    return true;
}

void GMDevilSquare::PrepareObjectUpdate(OBJECT *o)
{
    if ((int)WorldTime % 4000 < 1000)
        if (rand_fps_check(100))
        {
            float Luminosity = (float)(WorldRandom() % 12 + 4) * 0.1f;
            vec3_t Light;
            Vector(Luminosity * 0.2f, Luminosity * 0.3f, Luminosity * 0.5f, Light);
            AddTerrainLight(Hero->Object.Position[0] + WorldRandom() % 1200 - 600,
                            Hero->Object.Position[1] + WorldRandom() % 1200 - 600, Light, 12,
                            PrimaryTerrainLight);
            // PlayBuffer(SOUND_THUNDER01);
        }
    PlayBuffer(SOUND_RAIN01, NULL, true);
}

bool GMDevilSquare::AttackEffectMonster(CHARACTER *character, OBJECT *object, BMD *model)
{
    return TheMapProcess().Crywolf1st().AttackEffectCryWolf1stMonster(character, object, model);
}

bool GMDevilSquare::AdvanceMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                                         WorldCharacterVisualState &visual)
{
    return TheMapProcess().Crywolf1st().AdvanceCryWolf1stMonsterVisual(character, object, model,
                                                                       visual);
}

bool GMDevilSquare::AllowsAmbientSound(ESound sound) const
{
    return (sound == SOUND_RAIN01);
}

bool GMDevilSquare::CreateWeather(PARTICLE *o, int Index)
{
    o->Type = BITMAP_RAIN;
    if (Index < 300)
    {
        const float randomX = Random.RangeFloat(-800, 799);
        const float randomY = Random.RangeFloat(-500, 899);
        const float randomZ = Random.RangeFloat(300, 499);
        Vector(Hero->Object.Position[0] + randomX, Hero->Object.Position[1] + randomY,
               Hero->Object.Position[2] + randomZ, o->Position);
    }
    else
    {
        const float randomX = Random.RangeFloat(-800, 799);
        const float randomY = Random.RangeFloat(1000, 1299) - RainPosition;
        const float randomZ = Random.RangeFloat(300, 499);
        Vector(Hero->Object.Position[0] + randomX, Hero->Object.Position[1] + randomY,
               Hero->Object.Position[2] + randomZ, o->Position);
    }

    if (Random.FpsCheck(2, 1.0))
    {
        Vector(-Random.RangeFloat(20, 39), 0.f, 0.f, o->Angle);
    }
    else
    {
        Vector(-Random.RangeFloat(30, 49) - RainAngle, 0.f, 0.f, o->Angle);
    }

    vec3_t Velocity;
    Vector(0.f, 0.f, -((Random.RangeFloat(0, 39) + RainSpeed)), Velocity);
    float Matrix[3][4];
    AngleMatrix(o->Angle, Matrix);
    VectorRotate(Velocity, Matrix, o->Velocity);

    return true;
}

bool GMDevilSquare::MoveWeather(PARTICLE *o)
{
    VectorAddScaled(o->Position, o->Velocity, o->Position, FPS_ANIMATION_FACTOR);
    float Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
    if (o->Position[2] < Height)
    {
        o->Live = false;
        o->Position[2] = Height + 10.f;
        if (Random.FpsCheck(4, 1.f))
            CreateParticle(BITMAP_RAIN_CIRCLE, o->Position, o->Angle, o->Light);
        else
            CreateParticle(BITMAP_RAIN_CIRCLE + 1, o->Position, o->Angle, o->Light);
    }
    return true;
}

int GMDevilSquare::PrepareWeather()
{
    return MAX_LEAVES;
}

void GMDevilSquare::PrepareObjectLight(const ObjectDrawInput &object, BMD &model)
{
    if (object.type == MODEL_ICE_QUEEN)
        Vector(0.0f, 0.3f, 1.0f, model.BodyLight);
}

bool CGMDoppelGanger2::MoveMonsterVisual(CHARACTER *, OBJECT *object, BMD *model,
                                         WorldCharacterVisualState &visual)
{
    return IsDoppelGanger2() && g_DoppelGanger1.MoveSharedMonsterVisual(*object, *model, visual);
}

void CGMDoppelGanger2::MoveBlurEffect(CHARACTER *pCharacter, OBJECT *pObject, BMD *pModel)
{
    switch (pObject->Type)
    {
    case MODEL_MAD_BUTCHER:
    case MODEL_TERRIBLE_BUTCHER: {
        if (!(pObject->CurrentAction == MONSTER01_WALK ||
              pObject->CurrentAction == MONSTER01_ATTACK1 ||
              pObject->CurrentAction == MONSTER01_ATTACK2))
            break;

        vec3_t vLight;
        Vector(0.6f, 0.4f, 0.2f, vLight);

        vec3_t StartPos, StartRelative;
        vec3_t EndPos, EndRelative;

        float fActionSpeed = pModel->Actions[pObject->CurrentAction].PlaySpeed;
        float fSpeedPerFrame = fActionSpeed / 10.f;
        float fAnimationFrame = pObject->AnimationFrame - fActionSpeed;
        for (int i = 0; i < 10; i++)
        {
            pModel->AnimationAtFrame(BoneTransform, fAnimationFrame, pObject->PriorAnimationFrame,
                                     pObject->PriorAction, pObject->Angle, pObject->HeadAngle);

            Vector(0.f, 0.f, 0.f, StartRelative);
            Vector(0.f, 0.f, 0.f, EndRelative);
            pModel->TransformPosition(BoneTransform[33], StartRelative, StartPos, false);
            pModel->TransformPosition(BoneTransform[34], EndRelative, EndPos, false);
            CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 0);

            fAnimationFrame += fSpeedPerFrame;
        }
    }
    break;
    }
}

bool CGMDoppelGanger2::AdvanceObjectVisual(OBJECT *o, BMD *b, float)
{
    if (IsDoppelGanger2() == false)
        return false;

    vec3_t Light;

    switch (o->Type)
    {
    case 67: {
        PrepareWorldObjectPose(*o);
        vec3_t vLightFire, Position, vPos;
        Vector(1.0f, 0.0f, 0.0f, vLightFire);
        Vector(0.0f, 0.0f, 0.0f, vPos);

        b->TransformPosition(BoneTransform[6], vPos, Position, false);
        CreateSprite(BITMAP_LIGHT, Position, o->Scale * 5.0f, vLightFire, o);

        Vector(0.0f, 0.0f, -350.0f, vPos);
        b->TransformPosition(BoneTransform[6], vPos, Position, false);
        CreateSprite(BITMAP_LIGHT, Position, o->Scale * 5.0f, vLightFire, o);

        if (o->AnimationFrame >= 35 && o->AnimationFrame <= 37)
        {
            o->PKKey = -1;
        }

        if (o->AnimationFrame >= 1 && o->AnimationFrame <= 2 && o->PKKey != 1)
        {
            o->AnimationFrame = 1;

            int test = WorldRandom() % 1000;
            if (test >= 0 && test < 2)
            {
                o->PKKey = 1;
            }
            else
            {
                o->PKKey = -1;
            }
        }
        vec3_t p, Pos, Light;
        Vector(0.4f, 0.1f, 0.1f, Light);
        //Vector(WorldRandom()%20-30.0f, WorldRandom()%20-30.0f, 0.0f, p);
        Vector(-150.0f, 0.0f, 0.0f, p);
        b->TransformPosition(BoneTransform[4], p, Pos, false);
        if (o->AnimationFrame >= 35.0f && o->AnimationFrame < 50.0f)
            CreateParticleFpsChecked(BITMAP_SMOKE, Pos, o->Angle, Light, 24, o->Scale * 1.5f);
        return true;
    }

    case 68: {
        PrepareWorldObjectPose(*o);
        vec3_t vLightFire, Position, vPos;
        Vector(1.0f, 0.0f, 0.0f, vLightFire);
        Vector(0.0f, 0.0f, 0.0f, vPos);

        b->TransformPosition(BoneTransform[6], vPos, Position, false);
        CreateSprite(BITMAP_LIGHT, Position, o->Scale * 5.0f, vLightFire, o);

        Vector(0.0f, 0.0f, -350.0f, vPos);
        b->TransformPosition(BoneTransform[6], vPos, Position, false);
        CreateSprite(BITMAP_LIGHT, Position, o->Scale * 5.0f, vLightFire, o);

        vec3_t p, Pos, Light;
        //Vector(0.08f, 0.08f, 0.08f, Light);
        Vector(0.3f, 0.1f, 0.1f, Light);
        Vector(WorldRandom() % 20 - 30.0f, WorldRandom() % 20 - 30.0f, 0.0f, p);
        b->TransformPosition(BoneTransform[4], p, Pos, false);
        if (o->AnimationFrame >= 7.0f && o->AnimationFrame < 13.0f)
            CreateParticleFpsChecked(BITMAP_SMOKE, Pos, o->Angle, Light, 18, o->Scale * 1.5f);
        return true;
    }

    case 0: {
        o->HiddenMesh = -2;
        float fLumi = ((sinf(WorldTime * 0.001f) + 1.f) * 0.5f) * 100.0f;

        int nRanDelay = o->Position[0];
        nRanDelay = nRanDelay % 3 + 1;
        int nRanTemp = 30;
        nRanTemp = nRanTemp * nRanDelay;
        int nRanGap = 10;
        if (nRanTemp != 90.0f)
        {
            nRanGap = 40;
        }

        if (fLumi >= nRanTemp && fLumi <= nRanTemp + nRanGap)
        {
            Vector(1.0f, 1.0f, 1.0f, Light);
            for (int i = 0; i < 20; ++i)
            {
                CreateParticleFpsChecked(BITMAP_WATERFALL_2, o->Position, o->Angle, Light, 6,
                                         o->Scale, o);
            }
        }
    }
        return true;
    case 1: {
        o->HiddenMesh = -2;
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.0f, 1.0f, 1.0f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 60, o->Scale, o);
        }
    }
        return true;
    case 2: {
        o->HiddenMesh = -2;
        vec3_t Light;
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(0.f, 0.f, 0.f, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 16, o->Scale, o);
        }
    }
        return true;
    case 3: {
        o->HiddenMesh = -2;
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.f))
        {
            float fRed = (WorldRandom() % 3) * 0.01f + 0.015f;
            Vector(fRed, 0.0f, 0.0f, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 11, o->Scale, o);
        }
    }
        return true;
    case 4: {
        o->HiddenMesh = -2;
        Vector(1.0f, 0.4f, 0.4f, Light);
        vec3_t vAngle;
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector((float)(WorldRandom() % 40 + 120), 0.f, (float)(WorldRandom() % 30), vAngle);
            VectorAdd(vAngle, o->Angle, vAngle);
            CreateJoint(BITMAP_JOINT_SPARK, o->Position, o->Position, vAngle, 4, o, o->Scale);
            CreateParticle(BITMAP_SPARK, o->Position, vAngle, Light, 9, o->Scale);
        }
    }
        return true;
    case 5: {
        o->HiddenMesh = -2;
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(0.3f, 0.3f, 0.3f, o->Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, o->Light, 21, o->Scale);
        }
    }
        return true;
    case 6: {
        o->HiddenMesh = -2;

        vec3_t vLightFire;
        Vector(1.0f, 0.2f, 0.0f, vLightFire);
        CreateSprite(BITMAP_LIGHT, o->Position, 2.0f * o->Scale, vLightFire, o);

        vec3_t vLight;
        Vector(1.0f, 1.0f, 1.0f, vLight);

        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            switch (WorldRandom() % 3)
            {
            case 0:
                CreateParticle(BITMAP_FIRE_HIK1, o->Position, o->Angle, vLight, 0, o->Scale);
                break;
            case 1:
                CreateParticle(BITMAP_FIRE_CURSEDLICH, o->Position, o->Angle, vLight, 4, o->Scale);
                break;
            case 2:
                CreateParticle(BITMAP_FIRE_HIK3, o->Position, o->Angle, vLight, 0, o->Scale);
                break;
            }
        }
    }
        return true;
    case 47: {
        vec3_t vLight;
        Vector(0.1f, 0.4f, 1.0f, vLight);
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            switch (WorldRandom() % 3)
            {
            case 0:
                CreateParticle(BITMAP_FIRE_HIK1_MONO, o->Position, o->Angle, vLight, 2, o->Scale);
                break;
            case 1:
                CreateParticle(BITMAP_FIRE_HIK2_MONO, o->Position, o->Angle, vLight, 6, o->Scale);
                break;
            case 2:
                CreateParticle(BITMAP_FIRE_HIK3_MONO, o->Position, o->Angle, vLight, 2, o->Scale);
                break;
            }
            CreateParticle(BITMAP_FIRE_HIK1_MONO, o->Position, o->Angle, vLight, 2, o->Scale);
        }
    }
        return true;
    case 48:
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            vec3_t Light, vPos;
            Vector(0.6f, 0.8f, 1.0f, Light);
            VectorCopy(o->Position, vPos);
            int iScale = o->Scale * 60;
            vPos[0] += WorldRandom() % iScale - iScale / 2;
            vPos[1] += WorldRandom() % iScale - iScale / 2;
            CreateParticle(BITMAP_LIGHT, vPos, o->Angle, Light, 15, o->Scale, o);
        }
        return true;
    }

    return false;
}

bool CGMDoppelGanger2::AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                            WorldCharacterVisualState &visual)
{
    switch (o->Type)
    {
    case MODEL_MAD_BUTCHER:
    case MODEL_TERRIBLE_BUTCHER:
        sessionKeeper_.Visual()->AdvanceButcherVisual(*o, *b, c->Dead == 0);
        return true;
    case MODEL_DOPPELGANGER:
        return true;
        break;
    }

    return false;
}

bool CGMDoppelGanger2::CreateFireSpark(PARTICLE *o)
{
    if (!IsDoppelGanger2())
    {
        return false;
    }
    o->Type = BITMAP_FIRE_SNUFF;
    o->Scale = WorldRandom() % 50 / 100.f + 0.4f;
    vec3_t Position;
    Vector(Hero->Object.Position[0] + (float)(WorldRandom() % 1600 - 800),
           Hero->Object.Position[1] + (float)(WorldRandom() % 1400 - 500),
           Hero->Object.Position[2] + (float)(WorldRandom() % 300 + 50), Position);

    VectorCopy(Position, o->Position);
    VectorCopy(Position, o->StartPosition);
    o->Velocity[0] = -(float)(WorldRandom() % 64 + 64) * 0.1f;
    if (Position[1] < g_Camera.Position[1] + 400.f)
    {
        o->Velocity[0] = -o->Velocity[0] + 2.2f;
    }
    o->Velocity[1] = (float)(WorldRandom() % 32 - 16) * 0.1f;
    o->Velocity[2] = (float)(WorldRandom() % 32 - 16) * 0.1f;
    o->TurningForce[0] = (float)(WorldRandom() % 16 - 8) * 0.1f;
    o->TurningForce[1] = (float)(WorldRandom() % 64 - 32) * 0.1f;
    o->TurningForce[2] = (float)(WorldRandom() % 16 - 8) * 0.1f;

    Vector(1.f, 0.f, 0.f, o->Light);

    return true;
}

bool CGMDoppelGanger2::PlayMonsterSound(OBJECT *o)
{
    if (IsDoppelGanger2() == false)
        return false;

    return g_DoppelGanger1.PlayMonsterSound(o);

    return false;
}

std::optional<bool> CGMDoppelGanger2::ObjectVisibility(const OBJECT &object, bool)
{
    if (object.Type == 16 || object.Type == 67 || object.Type == 68)
        return TestFrustrum2D(object.Position[0] * 0.01f, object.Position[1] * 0.01f, -600.f);
    return std::nullopt;
}

void CGMDoppelGanger2::UpdateMusic()
{
    TheMapProcess().DoppelGanger1().PlayBGM();
}

bool CGMDoppelGanger2::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_DOPPELGANGER) == 0;
}

bool CGMDoppelGanger2::CreateWeather(PARTICLE *particle, int)
{
    return CreateFireSpark(particle);
}

bool CGMDoppelGanger3::MoveMonsterVisual(CHARACTER *, OBJECT *object, BMD *model,
                                         WorldCharacterVisualState &visual)
{
    return IsDoppelGanger3() && g_DoppelGanger1.MoveSharedMonsterVisual(*object, *model, visual);
}

void CGMDoppelGanger3::MoveBlurEffect(CHARACTER *pCharacter, OBJECT *pObject, BMD *pModel)
{
    switch (pObject->Type)
    {
    case MODEL_MAD_BUTCHER:
    case MODEL_TERRIBLE_BUTCHER: {
        if (!(pObject->CurrentAction == MONSTER01_WALK ||
              pObject->CurrentAction == MONSTER01_ATTACK1 ||
              pObject->CurrentAction == MONSTER01_ATTACK2))
            break;

        vec3_t vLight;
        Vector(0.6f, 0.4f, 0.2f, vLight);

        vec3_t StartPos, StartRelative;
        vec3_t EndPos, EndRelative;

        float fActionSpeed = pModel->Actions[pObject->CurrentAction].PlaySpeed;
        float fSpeedPerFrame = fActionSpeed / 10.f;
        float fAnimationFrame = pObject->AnimationFrame - fActionSpeed;
        for (int i = 0; i < 10; i++)
        {
            pModel->AnimationAtFrame(BoneTransform, fAnimationFrame, pObject->PriorAnimationFrame,
                                     pObject->PriorAction, pObject->Angle, pObject->HeadAngle);

            Vector(0.f, 0.f, 0.f, StartRelative);
            Vector(0.f, 0.f, 0.f, EndRelative);
            pModel->TransformPosition(BoneTransform[33], StartRelative, StartPos, false);
            pModel->TransformPosition(BoneTransform[34], EndRelative, EndPos, false);
            CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 0);

            fAnimationFrame += fSpeedPerFrame;
        }
    }
    break;
    }
}

bool CGMDoppelGanger3::AdvanceObjectVisual(OBJECT *o, BMD *b, float)
{
    switch (o->Type)
    {
    case 47: {
        vec3_t vLight;
        Vector(0.1f, 0.4f, 1.0f, vLight);

        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            switch (WorldRandom() % 3)
            {
            case 0:
                CreateParticle(BITMAP_FIRE_HIK1_MONO, o->Position, o->Angle, vLight, 2, o->Scale);
                break;
            case 1:
                CreateParticle(BITMAP_FIRE_HIK2_MONO, o->Position, o->Angle, vLight, 6, o->Scale);
                break;
            case 2:
                CreateParticle(BITMAP_FIRE_HIK3_MONO, o->Position, o->Angle, vLight, 2, o->Scale);
                break;
            }
            CreateParticle(BITMAP_FIRE_HIK1_MONO, o->Position, o->Angle, vLight, 2, o->Scale);
        }
    }
        return true;
    case 48:
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            vec3_t Light, vPos;
            Vector(0.6f, 0.8f, 1.0f, Light);
            VectorCopy(o->Position, vPos);
            int iScale = o->Scale * 60;
            vPos[0] += WorldRandom() % iScale - iScale / 2;
            vPos[1] += WorldRandom() % iScale - iScale / 2;
            CreateParticle(BITMAP_LIGHT, vPos, o->Angle, Light, 15, o->Scale, o);
        }
        return true;
    }
    return false;
}

bool CGMDoppelGanger3::AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                            WorldCharacterVisualState &visual)
{
    switch (o->Type)
    {
    case MODEL_MAD_BUTCHER:
    case MODEL_TERRIBLE_BUTCHER:
        sessionKeeper_.Visual()->AdvanceButcherVisual(*o, *b, c->Dead == 0);
        return true;
    case MODEL_DOPPELGANGER:
        return true;
        break;
    }

    return true;
}

bool CGMDoppelGanger3::PlayMonsterSound(OBJECT *o)
{
    if (IsDoppelGanger3() == false)
        return false;

    return g_DoppelGanger1.PlayMonsterSound(o);

    return false;
}

void CGMDoppelGanger3::UpdateMusic()
{
    TheMapProcess().DoppelGanger1().PlayBGM();
}

bool CGMDoppelGanger3::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_DOPPELGANGER) == 0;
}

void CGMDoppelGanger3::ConfigureAmbientFish(OBJECT *object)
{
    TheMapProcess().Atlans().ConfigureAmbientFish(object);
}

bool CGMDoppelGanger3::CanCreateAmbientFish(int index)
{
    return TerrainWall[index] == 0 || TerrainWall[index] == TW_CHARACTER;
}

bool CGMDoppelGanger3::ConfigureAmbientBoid(OBJECT *object, int index)
{
    return TheMapProcess().Atlans().ConfigureAmbientBoid(object, index);
}

bool CGMDoppelGanger3::CanCreateAmbientBoid(int slot, int index)
{
    return TerrainWall[index] == 0 || TerrainWall[index] == TW_CHARACTER;
}

bool CGMDoppelGanger3::PrepareAmbientBoidSlot(int index, bool &allowCreate)
{
    return true;
}

ESound CGMDoppelGanger3::WalkingSound(int tile, bool safe) const
{
    return safe ? SOUND_HUMAN_WALK_GROUND : SOUND_HUMAN_WALK_SWIM;
}

bool CGMDoppelGanger4::MoveMonsterVisual(CHARACTER *, OBJECT *object, BMD *model,
                                         WorldCharacterVisualState &visual)
{
    return IsDoppelGanger4() && g_DoppelGanger1.MoveSharedMonsterVisual(*object, *model, visual);
}

void CGMDoppelGanger4::MoveBlurEffect(CHARACTER *pCharacter, OBJECT *pObject, BMD *pModel)
{
    switch (pObject->Type)
    {
    case MODEL_MAD_BUTCHER:
    case MODEL_TERRIBLE_BUTCHER: {
        if (!(pObject->CurrentAction == MONSTER01_WALK ||
              pObject->CurrentAction == MONSTER01_ATTACK1 ||
              pObject->CurrentAction == MONSTER01_ATTACK2))
            break;

        vec3_t vLight;
        Vector(0.6f, 0.4f, 0.2f, vLight);

        vec3_t StartPos, StartRelative;
        vec3_t EndPos, EndRelative;

        float fActionSpeed = pModel->Actions[pObject->CurrentAction].PlaySpeed;
        float fSpeedPerFrame = fActionSpeed / 10.f;
        float fAnimationFrame = pObject->AnimationFrame - fActionSpeed;
        for (int i = 0; i < 10; i++)
        {
            pModel->AnimationAtFrame(BoneTransform, fAnimationFrame, pObject->PriorAnimationFrame,
                                     pObject->PriorAction, pObject->Angle, pObject->HeadAngle);

            Vector(0.f, 0.f, 0.f, StartRelative);
            Vector(0.f, 0.f, 0.f, EndRelative);
            pModel->TransformPosition(BoneTransform[33], StartRelative, StartPos, false);
            pModel->TransformPosition(BoneTransform[34], EndRelative, EndPos, false);
            CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 0);

            fAnimationFrame += fSpeedPerFrame;
        }
    }
    break;
    }
}

bool CGMDoppelGanger4::AdvanceObjectVisual(OBJECT *o, BMD *b, float)
{
    if (IsDoppelGanger4() == false)
        return false;

    vec3_t Light, p, Position;
    float Luminosity;

    switch (o->Type)
    {
    case 37: {
        int time = static_cast<DWORD>(WorldSimulationTime()) % 1024;
        if (time >= 0 && time < 10)
        {
            vec3_t Light;
            Vector(1.f, 1.f, 1.f, Light);
            CreateEffect(MODEL_BUTTERFLY01, o->Position, o->Angle, Light, 3, o);
        }
        o->HiddenMesh = -2;
    }
        return true;
    case 47: {
        vec3_t vLight;
        Vector(0.1f, 0.4f, 1.0f, vLight);
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            switch (WorldRandom() % 3)
            {
            case 0:
                CreateParticle(BITMAP_FIRE_HIK1_MONO, o->Position, o->Angle, vLight, 2, o->Scale);
                break;
            case 1:
                CreateParticle(BITMAP_FIRE_HIK2_MONO, o->Position, o->Angle, vLight, 6, o->Scale);
                break;
            case 2:
                CreateParticle(BITMAP_FIRE_HIK3_MONO, o->Position, o->Angle, vLight, 2, o->Scale);
                break;
            }
            CreateParticle(BITMAP_FIRE_HIK1_MONO, o->Position, o->Angle, vLight, 2, o->Scale);
        }
    }
        return true;
    case 48:
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            vec3_t Light, vPos;
            Vector(0.6f, 0.8f, 1.0f, Light);
            VectorCopy(o->Position, vPos);
            int iScale = o->Scale * 60;
            vPos[0] += WorldRandom() % iScale - iScale / 2;
            vPos[1] += WorldRandom() % iScale - iScale / 2;
            CreateParticle(BITMAP_LIGHT, vPos, o->Angle, Light, 15, o->Scale, o);
        }
        return true;
    case 59:
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 13, o->Scale);
        }
        return true;
    case 61:
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_TRUE_FIRE, o->Position, o->Angle, Light, 0, o->Scale);
        }
        return true;
    case 62:
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.04f, 0.04f, 0.04f, Light);

            for (int i = 0; i < 20; ++i)
                CreateParticleFpsChecked(BITMAP_CLOUD, o->Position, o->Angle, Light, 0, o->Scale,
                                         o);
        }
        return true;
    case 70:
        PrepareWorldObjectPose(*o);
        Vector(0.0f, 0.0f, 0.0f, p);
        b->TransformPosition(BoneTransform[6], p, Position, false);
        Luminosity = (float)sinf(WorldTime * 0.002f) + 1.8f;
        Vector(0.8f, 0.4f, 0.2f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, Luminosity * 7.0f, Light, o);
        Vector(0.65f, 0.65f, 0.65f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, Luminosity * 4.0f, Light, o);
        return true;
    case 81:
        Vector(1.f, 1.f, 1.f, Light);
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
            CreateParticle(BITMAP_WATERFALL_1, o->Position, o->Angle, Light, 2, o->Scale);
        return true;
    case 82:
        Vector(1.f, 1.f, 1.f, Light);
        CreateParticleFpsChecked(BITMAP_WATERFALL_3, o->Position, o->Angle, Light, 3, o->Scale);
        return true;
    case 83:
        Vector(1.f, 1.f, 1.f, Light);
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
            CreateParticle(BITMAP_WATERFALL_2, o->Position, o->Angle, Light, 1, o->Scale);
        return true;
    case 92:
        sessionKeeper_.Visual()->EmitBoneLightning(*o, *b, 8);
        return true;
    case 98: {
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            const float fraction = emission.FrameFraction();
            PrepareWorldObjectPose(*o, fraction);
            vec3_t vPos;
            Vector(0.0f, 0.0f, 0.0f, vPos);
            b->TransformPosition(BoneTransform[1], vPos, Position, false);
            Vector(0.5f, 0.6f, 0.1f, Light);
            CreateParticle(BITMAP_TWINTAIL_WATER, Position, o->Angle, Light, 2);
        }
    }
        return true;
    case 105:
        sessionKeeper_.Visual()->AdvanceEnergyNode(*o, *b);
        return true;
    case 107:
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.06f, 0.06f, 0.06f, Light);
            for (int i = 0; i < 20; ++i)
                CreateParticleFpsChecked(BITMAP_CLOUD, o->Position, o->Angle, Light, 2, o->Scale,
                                         o);
        }
        return true;
    case 108:
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.2f, 0.2f, 0.2f, Light);
            for (int i = 0; i < 20; ++i)
                CreateParticleFpsChecked(BITMAP_CLOUD, o->Position, o->Angle, Light, 7, o->Scale,
                                         o);
        }
        return true;
    case 110: {
        PrepareWorldObjectPose(*o);
        float fLumi = (sinf(WorldTime * 0.002f) + 1.5f) * 0.5f;
        Vector(fLumi * 0.6f, fLumi * 1.0f, fLumi * 0.8f, Light);
        vec3_t vPos;
        Vector(0.0f, 0.0f, 0.0f, vPos);
        b->TransformPosition(BoneTransform[1], vPos, Position, false);
        CreateSprite(BITMAP_LIGHT, Position, 1.1f, Light, o);
    }
        return true;
    }

    return false;
}

bool CGMDoppelGanger4::AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                            WorldCharacterVisualState &visual)
{
    switch (o->Type)
    {
    case MODEL_MAD_BUTCHER:
    case MODEL_TERRIBLE_BUTCHER:
        sessionKeeper_.Visual()->AdvanceButcherVisual(*o, *b, c->Dead == 0);
        return true;
    case MODEL_DOPPELGANGER:
        return true;
        break;
    }

    return true;
}

// 몬스터 사운드
bool CGMDoppelGanger4::PlayMonsterSound(OBJECT *o)
{
    if (IsDoppelGanger4() == false)
        return false;

    return g_DoppelGanger1.PlayMonsterSound(o);

    // 	float fDis_x, fDis_y;
    // 	fDis_x = o->Position[0] - Hero->Object.Position[0];
    // 	fDis_y = o->Position[1] - Hero->Object.Position[1];
    // 	float fDistance = sqrtf(fDis_x*fDis_x+fDis_y*fDis_y);
    // 	if (fDistance > 500.0f)
    // 		return true;
    // 	switch(o->Type)
    // 	{
    // 	}

    return false;
}

void CGMDoppelGanger4::UpdateMusic()
{
    TheMapProcess().DoppelGanger1().PlayBGM();
}

bool CGMDoppelGanger4::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_DOPPELGANGER) == 0;
}

bool CGMDuelArena::AdvanceObjectVisual(OBJECT *o, BMD *b, float)
{
    switch (o->Type)
    {
    case 34:
        break;
    case 35: {
        vec3_t vLight;
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, vLight);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, vLight, 14, o->Scale, o);
        }
    }
    break;
    case 36: {
        vec3_t vLightFire;
        Vector(1.0f, 0.2f, 0.0f, vLightFire);
        CreateSprite(BITMAP_LIGHT, o->Position, 2.0f * o->Scale, vLightFire, o);

        vec3_t vLight;
        Vector(1.0f, 1.0f, 1.0f, vLight);

        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            switch (WorldRandom() % 3)
            {
            case 0:
                CreateParticle(BITMAP_FIRE_HIK1, o->Position, o->Angle, vLight, 0, o->Scale);
                break;
            case 1:
                CreateParticle(BITMAP_FIRE_CURSEDLICH, o->Position, o->Angle, vLight, 4, o->Scale);
                break;
            case 2:
                CreateParticle(BITMAP_FIRE_HIK3, o->Position, o->Angle, vLight, 0, o->Scale);
                break;
            }
        }
    }
    break;
    }

    return false;
}

bool CGMDuelArena::AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                        WorldCharacterVisualState &visual)
{
    // 	vec3_t vPos, vRelative, vLight;
    // 	switch(o->Type)
    // 	{
    // 	}

    return true;
}

void CGMDuelArena::PlayBGM()
{
    if (IsDuelArena())
    {
        PlayMp3(MUSIC_DUEL_ARENA);
    }
}

void GMDungeon::PlayAmbientSounds()
{
    PlayBuffer(SOUND_DUNGEON01, NULL, true);
}

bool GMDungeon::AllowsAmbientSound(ESound sound) const
{
    return (sound == SOUND_DUNGEON01);
}

void GMDungeon::UpdateMusic()
{
    PlayMp3(MUSIC_DUNGEON);
}

bool GMDungeon::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_DUNGEON) == 0;
}

void GMDungeon::ConfigureAmbientFish(OBJECT *o)
{
    o->Type = MODEL_RAT01;
    o->Velocity = 0.6f / o->Scale;
}

bool GMDungeon::CanCreateAmbientFish(int index)
{
    return TerrainWall[index] < TW_NOGROUND;
}

bool GMDungeon::ConfigureAmbientBoid(OBJECT *object, int)
{
    object->Type = MODEL_BAT01;
    return false;
}

bool GMDungeon::CanCreateAmbientBoid(int slot, int index)
{
    return true;
}

bool GMEmpireGuardian2::MoveMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                          WorldCharacterVisualState &visual)
{
    ObjectDrawInput presentation(o);
    visual.movement.Apply(presentation);
    if (gMapManager.IsEmpireGuardian2() == false)
        return false;

    if (true == g_EmpireGuardian1.MoveMonsterVisual(c, o, b, visual))
    {
        return true;
    }

    switch (o->Type)
    {
    case MODEL_HAMMERIZE: {
        vec3_t vPos;
        switch (visual.action)
        {
        case MONSTER01_STOP1:
        case MONSTER01_STOP2:
            break;
        case MONSTER01_WALK: {
            int iTypeSubType = 0;

            Vector(0.4f, 0.4f, 0.4f, visual.movement.light);

            if (6.8f <= visual.animationFrame && visual.animationFrame < 7.5f)
            {
                b->TransformByObjectBone(vPos, presentation, 42);
                vPos[2] += 25.0f;
                vPos[1] += 0.0f;
                //vPos[0] += 100.0f;
                CreateParticleFpsChecked(BITMAP_SMOKE, vPos, o->Angle, visual.movement.light,
                                         iTypeSubType, 1.0f);
            }
            if (0.8f <= visual.animationFrame && visual.animationFrame < 1.5f)
            {
                b->TransformByObjectBone(vPos, presentation, 49);
                vPos[2] += 25.0f;
                vPos[1] += 0.0f;
                //vPos[0] += 100.0f;
                CreateParticleFpsChecked(BITMAP_SMOKE, vPos, o->Angle, visual.movement.light,
                                         iTypeSubType, 1.0f);
            }
        }
        break;
        case MONSTER01_DIE: {
        }
        break;
        case MONSTER01_ATTACK1: {
            if (6.0f <= visual.animationFrame && visual.animationFrame < 12.0f)
            {
                vec3_t Light;
                Vector(1.0f, 1.0f, 1.0f, Light);

                vec3_t StartPos, StartRelative;
                vec3_t EndPos, EndRelative;

                float fActionSpeed = b->Actions[visual.action].PlaySpeed;
                float fSpeedPerFrame = fActionSpeed / 10.f;
                float fAnimationFrame = visual.animationFrame - fActionSpeed;
                for (int i = 0; i < 10; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, presentation.headAngle);

                    Vector(0.0f, 0.0f, 0.0f, StartRelative);
                    Vector(0.0f, 0.0f, 0.0f, EndRelative);

                    b->TransformPosition(BoneTransform[34], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[35], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 0);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }
        }
        break;
        case MONSTER01_ATTACK2: {
            if (visual.animationFrame >= 5.4f && visual.animationFrame <= 11.0f)
            {
                AdvanceSkillEarthQuake(c, o, b, visual, 14);
            }

            if (6.0f <= visual.animationFrame && visual.animationFrame < 10.0f)
            {
                vec3_t Light;
                Vector(1.0f, 1.0f, 1.0f, Light);

                vec3_t StartPos, StartRelative;
                vec3_t EndPos, EndRelative;

                float fActionSpeed = b->Actions[visual.action].PlaySpeed;
                float fSpeedPerFrame = fActionSpeed / 10.f;
                float fAnimationFrame = visual.animationFrame - fActionSpeed;
                for (int i = 0; i < 10; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, presentation.headAngle);

                    Vector(0.0f, 0.0f, 0.0f, StartRelative);
                    Vector(0.0f, 0.0f, 0.0f, EndRelative);

                    b->TransformPosition(BoneTransform[34], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[35], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 0);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }
        }
        break;
        case MONSTER01_ATTACK3: {


            if (6.0f <= visual.animationFrame && visual.animationFrame < 12.0f)
            {
                vec3_t Light;
                Vector(1.0f, 1.0f, 1.0f, Light);

                vec3_t StartPos, StartRelative;
                vec3_t EndPos, EndRelative;

                float fActionSpeed = b->Actions[visual.action].PlaySpeed;
                float fSpeedPerFrame = fActionSpeed / 10.f;
                float fAnimationFrame = visual.animationFrame - fActionSpeed;
                for (int i = 0; i < 10; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, presentation.headAngle);

                    Vector(0.0f, 0.0f, 0.0f, StartRelative);
                    Vector(0.0f, 0.0f, 0.0f, EndRelative);

                    b->TransformPosition(BoneTransform[34], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[35], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 0);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }
        }
        break;
        case MONSTER01_APEAR: {
            vec3_t Light;

            if (m_bCurrentIsRage_Bermont == true)
            {
                Vector(1.0f, 1.0f, 1.0f, Light);
                CreateInferno(o->Position);

                CreateEffect(MODEL_CIRCLE, o->Position, o->Angle, Light, 4, o);
                CreateEffect(MODEL_CIRCLE, o->Position, o->Angle, Light, 4, o);
                m_bCurrentIsRage_Bermont = false;
            }
        }
        break;
        }
    }
        return true;
    case MODEL_ATICLES_HEAD: {
        vec3_t vPos, vRelative, vLight, v3Temp;

        float fLumi1 = (sinf(WorldTime * 0.004f) + 1.f) * 0.25f;

        Vector(0.7f + fLumi1, 0.7f + fLumi1, 0.7f + fLumi1, vLight);
        Vector(0.0f, 0.0f, 0.0f, vRelative);
        b->TransformByObjectBone(vPos, presentation, 21, vRelative);
        CreateSprite(BITMAP_LIGHT, vPos, 2.0f + fLumi1, vLight, o);

        b->TransformByObjectBone(vPos, presentation, 11, vRelative);
        CreateSprite(BITMAP_LIGHT, vPos, 2.0f + fLumi1, vLight, o);

        Vector(0.4f, 0.5f, 0.7f, vLight);
        b->TransformPosition(presentation.bones[8], v3Temp, vPos, true);
        CreateSprite(BITMAP_LIGHTMARKS, vPos, 2.0f, vLight, o);

        switch (visual.action)
        {
        case MONSTER01_STOP1:
        case MONSTER01_STOP2: {
        }
        break;
        case MONSTER01_WALK: {
            int iTypeSubType = 0;

            Vector(0.7f, 0.7f, 0.7f, visual.movement.light);

            if (7.0f <= visual.animationFrame && visual.animationFrame < 8.0f)
            {
                b->TransformByObjectBone(vPos, presentation, 42);
                vPos[2] += 25.0f;
                vPos[1] += 0.0f;
                //vPos[0] += 100.0f;
                CreateParticleFpsChecked(BITMAP_SMOKE, vPos, o->Angle, visual.movement.light,
                                         iTypeSubType, 2.0f);
            }
            if (1.0f <= visual.animationFrame && visual.animationFrame < 2.0f)
            {
                b->TransformByObjectBone(vPos, presentation, 47);
                vPos[2] += 25.0f;
                vPos[1] += 0.0f;
                //vPos[0] += 100.0f;
                CreateParticleFpsChecked(BITMAP_SMOKE, vPos, o->Angle, visual.movement.light,
                                         iTypeSubType, 2.0f);
            }
        }
        break;
        case MONSTER01_DIE: {
            // 					float Scale = 0.3f;
            // 					b->TransformByObjectBone( vPos, presentation, 30 );
            // 					CreateParticle(BITMAP_SMOKE+1, vPos, o->Angle, visual.movement.light, 1, Scale);
            // 					b->TransformByObjectBone( vPos, presentation, 17 );
            // 					CreateParticle(BITMAP_SMOKE+1, vPos, o->Angle, visual.movement.light, 1, Scale);
        }
        break;
        case MONSTER01_ATTACK1: {
            // 						if( visual.animationFrame >= 6.6f && visual.animationFrame <= 7.4f )
            // 						{
            // 							CreateEffect ( MODEL_WAVES, o->Position, o->Angle, visual.movement.light, 1 );
            // 							CreateEffect ( MODEL_WAVES, o->Position, o->Angle, visual.movement.light, 1 );
            // 							CreateEffect ( MODEL_PIERCING2, o->Position, o->Angle, visual.movement.light );
            // 							PlayBuffer ( SOUND_ATTACK_SPEAR );
            // 						}
        }
        break;
        case MONSTER01_ATTACK2: {
            vec3_t v3PosPiercing, v3AnglePiercing;


        }
        break;
        case MONSTER01_APEAR: {
            vec3_t v3PosPiercing, v3AnglePiercing;

        }
        break;
        }
    }
        return true;
    }
    return false;
}

void GMEmpireGuardian2::MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b)
{
    switch (o->Type)
    {
    case MODEL_LUCAS:
    case MODEL_DEFENDER:
    case MODEL_FORSAKER:
    case MODEL_OCELOT:
    case MODEL_ERIC: {
        g_EmpireGuardian1.MoveBlurEffect(c, o, b);
    }
    break;
    }
}

bool GMEmpireGuardian2::AdvanceObjectVisual(OBJECT *o, BMD *b, float)
{
    if (gMapManager.IsEmpireGuardian2() == false)
        return false;

    vec3_t p, Position, Light;
    Vector(0.f, 30.f, 0.f, Position);
    Vector(0.f, 0.f, 0.f, p);

    switch (o->Type)
    {
    case 115:
    case 117:
        g_EmpireGuardian1.AdvanceGateProjectile(o, b);
        return true;
    case 12: {
        PrepareWorldObjectPose(*o);
        vec3_t vPos, vRelativePos, vLight1, vLight2;
        float flumi = absf(sinf(WorldTime * 0.0008)) * 0.9f + 0.1f;
        float fScale = o->Scale * 0.3f * flumi;
        Vector(0.f, 0.f, 0.f, vPos);
        Vector(8.f, -3.f, -3.f, vRelativePos);
        Vector(flumi, flumi, flumi, vLight1);
        Vector(0.9f, 0.1f, 0.1f, vLight2);
        b->TransformPosition(BoneTransform[2], vRelativePos, vPos);
#ifdef LDS_FIX_ACCESS_INDEXNUMBER_ALREADY_LOADTEXTURE
        CreateSprite(BITMAP_SHINY + 6, vPos, 0.5f, vLight2, o);
        CreateSprite(BITMAP_SHINY + 6, vPos, fScale, vLight1, o);
        Vector(3.f, -3.f, -3.5f, vRelativePos);
        b->TransformPosition(BoneTransform[3], vRelativePos, vPos);
        CreateSprite(BITMAP_SHINY + 6, vPos, 0.5f, vLight2, o);
        CreateSprite(BITMAP_SHINY + 6, vPos, fScale, vLight1, o);
#else  // LDS_FIX_ACCESS_INDEXNUMBER_ALREADY_LOADTEXTURE
        CreateSprite(BITMAP_SHINY + 5, vPos, 0.5f, vLight2, o);
        CreateSprite(BITMAP_SHINY + 5, vPos, fScale, vLight1, o);
        Vector(3.f, -3.f, -3.5f, vRelativePos);
        b->TransformPosition(BoneTransform[3], vRelativePos, vPos);
        CreateSprite(BITMAP_SHINY + 5, vPos, 0.5f, vLight2, o);
        CreateSprite(BITMAP_SHINY + 5, vPos, fScale, vLight1, o);
#endif // LDS_FIX_ACCESS_INDEXNUMBER_ALREADY_LOADTEXTURE
    }
        return true;

    case 20: {
        if (o->AnimationFrame > 5.4f && o->AnimationFrame < 6.5f)
        {
            vec3_t Angle;
            for (int i = 0; i < 4; ++i)
            {
                Vector((float)(WorldRandom() % 60 + 60 + 90), 0.f, o->Angle[2] + 180, Angle);
                CreateJointFpsChecked(BITMAP_JOINT_SPARK, o->Position, o->Position, Angle, 5, o);
            }
            CreateParticleFpsChecked(BITMAP_SPARK, o->Position, Angle, o->Light, 11);
        }
        else if (o->AnimationFrame > 15.4f && o->AnimationFrame < 16.5f)
        {
            vec3_t Angle;
            for (int i = 0; i < 4; ++i)
            {
                Vector((float)(WorldRandom() % 60 + 60 + 90), 0.f, o->Angle[2], Angle);
                CreateJointFpsChecked(BITMAP_JOINT_SPARK, o->Position, o->Position, Angle, 5, o);
            }
            CreateParticleFpsChecked(BITMAP_SPARK, o->Position, Angle, o->Light, 11);
        }
    }
        return true;

    case 37: {
        PrepareWorldObjectPose(*o);
        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(BoneTransform[1], p, Position);

        float fLumi;
        fLumi = (sinf(WorldTime * 0.039f) + 1.0f) * 0.2f + 0.6f;
        vec3_t vLightFire;
        Vector(fLumi * 0.7f, fLumi * 0.7f, fLumi * 0.7f, vLightFire);
        CreateSprite(BITMAP_FLARE, Position, 4.0f * o->Scale, vLightFire, o);
    }
        return true;

    case 50: {
        PrepareWorldObjectPose(*o);
        vec3_t vPos, vRelativePos, vLight1, vLight2, vAngle;
        Vector(0.f, 0.f, 0.f, vPos);
        Vector(0.f, 0.f, 5.f, vRelativePos);
        Vector(0.0f, -1.0f, 0.0f, vAngle);
        Vector(0.05f, 0.1f, 0.3f, vLight1);
        Vector(1.f, 1.f, 1.f, vLight2);

        for (int i = 2; i <= 7; i++)
        {
            b->TransformPosition(BoneTransform[i], vRelativePos, vPos);
            CreateParticleFpsChecked(BITMAP_FIRE_HIK3_MONO, vPos, vAngle, vLight1, 4,
                                     o->Scale * 0.6f);
            CreateParticleFpsChecked(BITMAP_FIRE_HIK3_MONO, vPos, vAngle, vLight2, 4,
                                     o->Scale * 0.3f);
        }
    }
        return true;

    case 51: {
        if (o->AnimationFrame > 5.4f && o->AnimationFrame < 6.5f)
        {
            vec3_t Angle;
            for (int i = 0; i < 4; ++i)
            {
                Vector((float)(WorldRandom() % 60 + 60 + 90), 0.f, o->Angle[2] + 180, Angle);
                CreateJointFpsChecked(BITMAP_JOINT_SPARK, o->Position, o->Position, Angle, 5, o);
            }
            CreateParticleFpsChecked(BITMAP_SPARK, o->Position, Angle, o->Light, 11);
        }
    }
        return true;

    case 64: {
        if ((o->AnimationFrame > 9.5f && o->AnimationFrame < 11.5f) ||
            (o->AnimationFrame > 23.5f && o->AnimationFrame < 25.5f))
        {
            float Matrix[3][4];
            vec3_t vAngle, vDirection, vPosition;
            Vector(0.f, 0.f, o->Angle[2] + 90, vAngle);
            AngleMatrix(vAngle, Matrix);
            Vector(0.f, 30.0f, 0.f, vDirection);
            VectorRotate(vDirection, Matrix, vPosition);
            VectorAdd(vPosition, o->Position, Position);

            Vector(0.04f, 0.03f, 0.02f, Light);
            for (int i = 0; i < 3; ++i)
            {
                CreateParticleFpsChecked(BITMAP_CLOUD, Position, o->Angle, Light, 22, o->Scale, o);
            }
        }
    }
        return true;

    case 79: {
        vec3_t vLightFire;
        Vector(1.0f, 0.2f, 0.0f, vLightFire);
        CreateSprite(BITMAP_LIGHT, o->Position, 2.0f * o->Scale, vLightFire, o);

        vec3_t vLight;
        Vector(1.0f, 1.0f, 1.0f, vLight);

        switch (WorldRandom() % 3)
        {
        case 0:
            CreateParticleFpsChecked(BITMAP_FIRE_HIK1, o->Position, o->Angle, vLight, 0, o->Scale);
            break;
        case 1:
            CreateParticleFpsChecked(BITMAP_FIRE_CURSEDLICH, o->Position, o->Angle, vLight, 4,
                                     o->Scale);
            break;
        case 2:
            CreateParticleFpsChecked(BITMAP_FIRE_HIK3, o->Position, o->Angle, vLight, 0, o->Scale);
            break;
        }
    }
        return true;
    case 80: {
        float fLumi;
        fLumi = (sinf(WorldTime * 0.04f) + 1.0f) * 0.3f + 0.4f;
        vec3_t vLightFire;
        Vector(fLumi * 0.1f, fLumi * 0.1f, fLumi * 0.5f, vLightFire);
        CreateSprite(BITMAP_LIGHT, o->Position, 8.0f * o->Scale, vLightFire, o);
    }
        return true;
    case 82: {
        Vector(1.f, 1.f, 1.f, Light);
        CreateParticleFpsChecked(BITMAP_WATERFALL_5, o->Position, o->Angle, Light, 9, o->Scale);
    }
        return true;

    case 83: {
        Vector(1.f, 1.f, 1.f, Light);
        CreateParticleFpsChecked(BITMAP_WATERFALL_3, o->Position, o->Angle, Light, 14, o->Scale);
    }
        return true;

    case 84: {
        Vector(1.f, 1.f, 1.f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 8.f))
        {
            CreateParticle(BITMAP_WATERFALL_2, o->Position, o->Angle, Light, 4, o->Scale);
        }
    }
        return true;

    case 85:
        sessionKeeper_.Visual()->EmitPeriodicFlames(*o);
        return true;

    case 86: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 6.f))
        {
            Vector(0.05f, 0.02f, 0.01f, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 21, o->Scale, o);
        }
    }
        return true;

    case 129: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 6.f))
        {
            Vector(0.01f, 0.02f, 0.05f, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 21, o->Scale, o);
        }
    }
        return true;

    case 130: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 6.f))
        {
            Vector(0.01f, 0.05f, 0.02f, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 21, o->Scale, o);
        }
    }
        return true;

    case 131: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 22, o->Scale);

            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 21, o->Scale * 2.0f, o);
        }
    }
        return true;

    case 132: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 60, o->Scale, o);

            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 60, o->Scale, o);

            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 21, o->Scale * 2.0f, o);
        }
    }
        return true;
    }

    return false;
}

bool GMEmpireGuardian2::AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                             WorldCharacterVisualState &visual)
{
    if (g_EmpireGuardian1.AdvanceMonsterVisual(c, o, b, visual))
    {
        return true;
    }

    vec3_t vPos, vLight;

    switch (o->Type)
    {
    case MODEL_HAMMERIZE: {
        vec3_t vPos, vRelative, vLight;
        float fLumi1 = (sinf(WorldTime * 0.004f) + 1.f) * 0.25f;

        Vector(0.1f + fLumi1, 0.4f + fLumi1, 0.8f + fLumi1, vLight);
        Vector(0.0f, 0.0f, 0.0f, vRelative);
        b->TransformByObjectBone(vPos, o, 21, vRelative);
        CreateSprite(BITMAP_LIGHT, vPos, 1.5f + fLumi1, vLight, o);

        b->TransformByObjectBone(vPos, o, 38, vRelative);
        CreateSprite(BITMAP_LIGHT, vPos, 1.5f + fLumi1, vLight, o);

        if (g_isNotCharacterBuff(o) == true && g_isCharacterBuff(o, eBuff_Berserker) == true)
        {
            sessionKeeper_.Visual()->EmitBerserkerSmoke(*o, *b, 6);
        }
    }
        return true;
    case MODEL_DARK_GHOST: {
        int i;
        float fLumi = (sinf(WorldTime * 0.08f) + 1.0f) * 0.5f * 0.3f + 0.7f;
        Vector(0.1f * fLumi, 0.6f * fLumi, 1.0f * fLumi, vLight);

        int iBlueLights[] = {93, 100, 47, 54};
        for (i = 0; i < 4; ++i)
        {
            b->TransformByObjectBone(vPos, o, iBlueLights[i]);
            CreateSprite(BITMAP_LIGHT, vPos, 3.0f, vLight, o);
        }

        Vector(0.1f * fLumi, 1.0f * fLumi, 0.2f * fLumi, vLight);
        int iGreenLights[] = {92, 99, 46, 53, 95, 88, 42, 49};
        for (i = 0; i < 8; ++i)
        {
            b->TransformByObjectBone(vPos, o, iGreenLights[i]);
            CreateSprite(BITMAP_LIGHT, vPos, 1.0f, vLight, o);
        }

        int iBigGreenLights[] = {80, 34};
        for (i = 0; i < 2; ++i)
        {
            b->TransformByObjectBone(vPos, o, iBigGreenLights[i]);
            Vector(0.1f * fLumi, 1.0f * fLumi, 0.3f * fLumi, vLight);
            CreateSprite(BITMAP_LIGHT, vPos, 3.0f, vLight, o);
            Vector(0.5f, 0.5f, 0.5f, vLight);
            CreateParticleFpsChecked(BITMAP_CHROME2, vPos, o->Angle, vLight, 0, 0.9f, o);
        }

        MoveEye(o, b, 80, 34);

        if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
        {
            for (i = 0; i < 2; ++i)
            {
                b->TransformByObjectBone(vPos, o, iBigGreenLights[i]);
                Vector(0.3f, 1.0f, 0.8f, vLight);
                CreateParticleFpsChecked(BITMAP_WATERFALL_4, vPos, o->Angle, vLight, 15, 2.0f);
                Vector(0.0f, 0.4f, 0.0f, vLight);
                CreateParticleFpsChecked(BITMAP_SPARK + 1, vPos, o->Angle, vLight, 13, 1.0f, o);
                CreateParticleFpsChecked(BITMAP_SPARK + 1, vPos, o->Angle, vLight, 13, 1.0f, o);
            }
        }
        else if (visual.action == MONSTER01_DIE)
        {

            if (visual.animationFrame <= 3.0f)
            {
                Vector(0.1f, 1.0f, 0.2f, vLight);
                for (int i = 0; i < 5; i++)
                {
                    CreateParticleFpsChecked(BITMAP_SMOKE, o->Position, o->Angle, vLight, 39);
                }

                CreateEffectFpsChecked(MODEL_SKILL_INFERNO, o->Position, o->Angle, o->Light, 9, o);

                if (visual.animationFrame <= 0.2f)
                {
                    Vector(0.4f, 1.0f, 0.6f, vLight);
                    CreateEffectFpsChecked(MODEL_TWINTAIL_EFFECT, o->Position, o->Angle, vLight, 1,
                                           o);
                    CreateEffectFpsChecked(MODEL_TWINTAIL_EFFECT, o->Position, o->Angle, vLight, 2,
                                           o);
                }
            }
        }
    }
        return true;
    default: {
    }
        return true;
    }
    return true;
}

bool GMEmpireGuardian2::CreateRain(PARTICLE *o)
{
    return g_EmpireGuardian1.CreateRain(o);
}

bool GMEmpireGuardian2::AttackEffectMonster(CHARACTER *c, OBJECT *o, BMD *b)
{
    if (gMapManager.IsEmpireGuardian2() == false)
        return false;

    switch (c->MonsterIndex)
    {
    case MONSTER_DARK_GHOST: {
        vec3_t vPos;

        if (o->CurrentAction == MONSTER01_ATTACK1)
        {
            if (c->TargetCharacter != -1)
            {
                CHARACTER *pTargetCharacter = &CharactersClient[c->TargetCharacter];
                CreateEffect(MODEL_FIRE, pTargetCharacter->Object.Position, o->Angle, o->Light);
                PlayBuffer(SOUND_METEORITE01);
                c->TargetCharacter = -1;
                c->MonsterSkill = -1;
            }
        }
        else if (c->MonsterSkill == 51)
        {
            if (c->TargetCharacter != -1)
            {
                CHARACTER *pTargetCharacter = &CharactersClient[c->TargetCharacter];
                OBJECT *pTargetObject = &pTargetCharacter->Object;
                CreateEffect(BITMAP_FLAME, pTargetObject->Position, pTargetObject->Angle,
                             pTargetObject->Light, 0, pTargetObject);
                PlayBuffer(SOUND_FLAME);
                c->MonsterSkill = -1;
            }
        }
        else if (c->MonsterSkill == 52)
        {
            Vector(o->Position[0] + WorldRandom() % 1024 - 512,
                   o->Position[1] + WorldRandom() % 1024 - 512, o->Position[2], vPos);
            CreateEffect(MODEL_FIRE, vPos, o->Angle, o->Light);
            PlayBuffer(SOUND_METEORITE01);
        }
    }
        return true;
    }
    return false;
}

bool GMEmpireGuardian2::PlayMonsterSound(OBJECT *o)
{
    if (gMapManager.IsEmpireGuardian2() == false)
        return false;

    if (true == g_EmpireGuardian1.PlayMonsterSound(o))
    {
        return true;
    }

    float fDis_x, fDis_y;
    fDis_x = o->Position[0] - Hero->Object.Position[0];
    fDis_y = o->Position[1] - Hero->Object.Position[1];
    float fDistance = sqrtf(fDis_x * fDis_x + fDis_y * fDis_y);

    if (fDistance > 500.0f)
        return true;

    switch (o->Type)
    {
    case MODEL_HAMMERIZE: {
        switch (o->CurrentAction)
        {
        case MONSTER01_WALK: {
            if (rand_fps_check(2))
            {
                PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_MOVE01);
            }
            else
            {
                PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_MOVE02);
            }
        }
        break;
        case MONSTER01_ATTACK1: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_2CORP_VERMONT_MONSTER_ATTACK01);
        }
        break;
        case MONSTER01_ATTACK2: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_2CORP_VERMONT_MONSTER_ATTACK02);
        }
        break;
        case MONSTER01_ATTACK3: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_2CORP_VERMONT_MONSTER_ATTACK01);
        }
        break;
        case MONSTER01_APEAR: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_RAGE);
        }
        break;
        case MONSTER01_DIE: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_2CORP_VERMONT_MONSTER_DEATH);
        }
        break;
        }
    }
        return true;
    case MODEL_ATICLES_HEAD: {
        switch (o->CurrentAction)
        {
        case MONSTER01_WALK: {
            if (rand_fps_check(2))
            {
                PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_MOVE01);
            }
            else
            {
                PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_MOVE02);
            }
        }
        break;
        case MONSTER01_ATTACK1: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_COMBATMASTER_ATTACK03);
        }
        break;
        case MONSTER01_ATTACK2: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_COMBATMASTER_ATTACK03);
        }
        break;
        case MONSTER01_ATTACK3: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_COMBATMASTER_ATTACK03);
        }
        break;
        case MONSTER01_DIE: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_DEATH);
        }
        break;
        }
    }
        return true;
    case MODEL_DARK_GHOST: {
        switch (o->CurrentAction)
        {
        case MONSTER01_WALK: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_BOSS_GAION_MONSTER_MOVE);
        }
        break;
        case MONSTER01_ATTACK1: {
            PlayBuffer(SOUND_METEORITE01);
            PlayBuffer(SOUND_EXPLOTION01);
        }
        break;
        case MONSTER01_ATTACK2: {
            PlayBuffer(SOUND_3RD_CHANGE_UP_BG_FIREPILLAR);
        }
        break;
        case MONSTER01_ATTACK3: {
            PlayBuffer(SOUND_EXPLOTION01);
        }
        break;
        case MONSTER01_DIE: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_GRANDWIZARD_DEATH);
        }
        break;
        }
    }
        return true;
    }

    return false;
}

void GMEmpireGuardian2::PlayObjectSound(OBJECT *o)
{
    g_EmpireGuardian1.PlayObjectSound(o);
}

void GMEmpireGuardian2::PlayBGM()
{
    if (gMapManager.IsEmpireGuardian2())
    {
        PlayMp3(MUSIC_EMPIREGUARDIAN2);
    }
    else
    {
        StopMp3(MUSIC_EMPIREGUARDIAN2);
    }
}

void GMEmpireGuardian2::AdvanceEnvironment()
{
    g_EmpireGuardian1.AdvanceWeather();
}

void GMEmpireGuardian2::UpdateMusic()
{
    PlayBGM();
}

bool GMEmpireGuardian2::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_EMPIREGUARDIAN2) == 0;
}

bool GMEmpireGuardian2::CreateWeather(PARTICLE *particle, int)
{
    return CreateRain(particle);
}

void GMEmpireGuardian3::EmitBansheeEvents(CHARACTER &character, BMD &model)
{
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    OBJECT *o = &character.Object;
    CHARACTER *c = &character;
    BMD *b = &model;
    constexpr std::array<std::pair<int, float>, 2> markers{
        {{MONSTER01_ATTACK2, 4.5f}, {MONSTER01_ATTACK3, 4.5f}}};
    o->MotionTrace.VisitAnimationEvents(WorldTime, markers, [&](std::size_t event, float fraction) {
        auto birth =
            sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR * (1.f - fraction));
        ObjectDrawInput draw(o);
        o->MotionTrace.Sample(WorldTime, fraction, o->Position, draw.position);
        draw.angle[2] = o->MotionTrace.SampleYaw(WorldTime, fraction, draw.angle[2]);
        vec3_t angle;
        VectorCopy(draw.angle, angle);
        AnimationPoseSample pose(draw, b->BoneHead, b->BodyHeight, true, b->PoseAssetIdentity());
        const auto phase = o->MotionTrace.SampleAnimation(
            WorldTime, fraction,
            {o->AnimationFrame, o->PriorAnimationFrame, o->CurrentAction, o->PriorAction});
        pose.action = phase.action;
        pose.priorAction = phase.priorAction;
        pose.priorFrame = phase.priorFrame;

        vec3_t Light;
        Vector(1.0f, 0.5f, 0.2f, Light);

        vec3_t vPosBlur01, vPosBlurRelative01;
        vec3_t vPosBlur02, vPosBlurRelative02;

        float fActionSpeed = b->Actions[markers[event].first].PlaySpeed;
        float fSpeedPerFrame = fActionSpeed / 6.f;
        float fAnimationFrame = 4.5f - fActionSpeed;
        for (int i = 0; i < 8; i++)
        {
            pose.frame = fAnimationFrame;
            pose.EvaluateAtFrame(*b, BoneTransform);

            Vector(0.0f, 0.0f, 0.0f, vPosBlurRelative01);
            Vector(0.0f, 0.0f, 0.0f, vPosBlurRelative02);

            b->TransformPosition(BoneTransform[49], vPosBlurRelative01, vPosBlur01, false);
            b->TransformPosition(BoneTransform[51], vPosBlurRelative02, vPosBlur02, false);

            CreateBlur(c, vPosBlur01, vPosBlur02, Light, 2);

            fAnimationFrame += fSpeedPerFrame;
        }
        if (event == 0)
        {
            Vector(0.1f, 0.6f, 0.3f, Light);
        }
        else
        {
            Vector(1.f, 1.f, 1.f, Light);
        }
        CreateJoint(BITMAP_JOINT_FORCE, vPosBlur01, vPosBlur02, angle, 10, o, 150.f, o->PKKey,
                    o->Skill, 1, -1, Light);

        for (int iLoop = 0; iLoop < 3; iLoop++)
        {
            vPosBlur01[2] -= 30.f;
            vPosBlur02[2] -= 30.f;
            CreateJoint(BITMAP_JOINT_FORCE, vPosBlur01, vPosBlur02, angle, 10, o, 150.f, o->PKKey,
                        o->Skill, 1, -1, Light);
        }

        //						c->AttackTime = 15;
    });
}

bool GMEmpireGuardian3::MoveSharedMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                                WorldCharacterVisualState &visual)
{
    ObjectDrawInput presentation(o);
    visual.movement.Apply(presentation);
    //  	if(IsEmpireGuardian3() == false)
    //		return false;

    vec3_t vPos;

    switch (o->Type)
    {
    case MODEL_DUAL_BERSERKER: {
        switch (visual.action)
        {
        case MONSTER01_WALK:
        case MONSTER01_ATTACK1:
        case MONSTER01_ATTACK2:
        case MONSTER01_ATTACK3:
        case MONSTER01_DIE:
            break;
        case MONSTER01_APEAR: {
            vec3_t Light;

            if (m_bCurrentIsRage_Kato == true)
            {
                Vector(1.0f, 1.0f, 1.0f, Light);
                CreateInferno(o->Position);

                CreateEffect(MODEL_CIRCLE, o->Position, o->Angle, Light, 4, o);
                CreateEffect(MODEL_CIRCLE, o->Position, o->Angle, Light, 4, o);
                m_bCurrentIsRage_Kato = false;
            }
        }
        break;
        }
    }
        return true;
    case MODEL_BANSHEE: {
        EmitBansheeEvents(*c, *b);
        switch (visual.action)
        {
        case MONSTER01_WALK:
            break;
        case MONSTER01_ATTACK1: {
            if (2.0f <= visual.animationFrame && visual.animationFrame < 15.0f)
            {
                vec3_t Light;
                //						Vector(0.3f, 0.3f, 0.3f, Light);
                Vector(0.3f, 0.8f, 0.4f, Light);

                vec3_t StartPos, StartRelative;
                vec3_t EndPos, EndRelative;

                float fActionSpeed = b->Actions[visual.action].PlaySpeed;
                float fSpeedPerFrame = fActionSpeed / 10.f;
                float fAnimationFrame = visual.animationFrame - fActionSpeed;
                for (int i = 0; i < 10; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, presentation.headAngle);

                    Vector(0.0f, 0.0f, 0.0f, StartRelative);
                    Vector(0.0f, 0.0f, 0.0f, EndRelative);

                    b->TransformPosition(BoneTransform[49], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[51], EndRelative, EndPos, false);
                    CreateObjectBlur(o, StartPos, EndPos, Light, 3, false,
                                     visual.movement.animation + 1);
                    //							CreateObjectBlur(o, StartPos, EndPos, Light, 2, false, visual.movement.animation + 1);
                    //							CreateObjectBlur(o, StartPos, EndPos, Light, 3, false, visual.movement.animation + 1);
                    //							CreateObjectBlur(o, StartPos, EndPos, Light, 2);
                    //							CreateObjectBlur(o, StartPos, EndPos, Light, 3);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }
        }
        break;
        case MONSTER01_ATTACK2: {
            if (c->AttackTime >= 1 && c->AttackTime <= 2)
            {
                vec3_t Angle;
                Vector(1.f, 0.f, 0.f, Angle);
                CreateEffect(BITMAP_GATHERING, o->Position, o->Angle, visual.movement.light, 1, o);
            }


        }
        break;
        case MONSTER01_ATTACK3: {
            if (c->AttackTime >= 1 && c->AttackTime <= 2)
            {
                vec3_t Angle;
                Vector(1.f, 0.f, 0.f, Angle);
                CreateEffect(BITMAP_GATHERING, o->Position, o->Angle, visual.movement.light, 1, o);
            }


        }
        break;
        case MONSTER01_DIE:
            break;
        case MONSTER01_APEAR: {
        }
        break;
        }
    }
        return true;
    case MODEL_HEAD_MOUNTER: {
        switch (visual.action)
        {
        case MONSTER01_STOP1:
        case MONSTER01_STOP2: {
            if (4.5f <= visual.animationFrame && visual.animationFrame < 10.0f)
            {
                Vector(0.8f, 0.4f, 0.1f, visual.movement.light);

                b->TransformByObjectBone(vPos, presentation, 15);
                CreateParticleFpsChecked(BITMAP_CLUD64, vPos, o->Angle, visual.movement.light, 3,
                                         0.2f);
                CreateParticleFpsChecked(BITMAP_WATERFALL_3, vPos, o->Angle, visual.movement.light,
                                         13, 1.0f);
            }
        }
        break;
        case MONSTER01_WALK: {
            Vector(0.5f, 0.2f, 0.1f, visual.movement.light);

            if (7.0f <= visual.animationFrame && visual.animationFrame < 8.0f)
            {
                b->TransformByObjectBone(vPos, presentation, 32);
                vPos[2] += 25.0f;
                CreateParticleFpsChecked(BITMAP_ADV_SMOKE, vPos, o->Angle, visual.movement.light, 3,
                                         2.0f);
            }
            if (1.0f <= visual.animationFrame && visual.animationFrame < 2.0f)
            {
                b->TransformByObjectBone(vPos, presentation, 19);
                vPos[2] += 25.0f;
                CreateParticleFpsChecked(BITMAP_ADV_SMOKE, vPos, o->Angle, visual.movement.light, 3,
                                         2.0f);
            }
        }
        break;
        case MONSTER01_ATTACK1: {
        }
        break;
        case MONSTER01_ATTACK2: {
            if (visual.animationFrame >= 3.8f && visual.animationFrame <= 9.4f)
            {
                AdvanceSkillEarthQuake(c, o, b, visual, 14);
            }
            else
            {
                visual.movement.weaponLevel = 0;
            }
        }
        break;
        case MONSTER01_APEAR: {
        }
        break;
        case MONSTER01_ATTACK3: {
        }
        break;
        case MONSTER01_DIE: {
            float Scale = 0.3f;
            b->TransformByObjectBone(vPos, presentation, 30);
            CreateParticleFpsChecked(BITMAP_SMOKE + 1, vPos, o->Angle, visual.movement.light, 1,
                                     Scale);

            b->TransformByObjectBone(vPos, presentation, 17);
            CreateParticleFpsChecked(BITMAP_SMOKE + 1, vPos, o->Angle, visual.movement.light, 1,
                                     Scale);

            b->TransformByObjectBone(vPos, presentation, 2);
            CreateParticleFpsChecked(BITMAP_SMOKE + 1, vPos, o->Angle, visual.movement.light, 1,
                                     Scale);

            b->TransformByObjectBone(vPos, presentation, 6);
            CreateParticleFpsChecked(BITMAP_SMOKE + 1, vPos, o->Angle, visual.movement.light, 1,
                                     Scale);

            b->TransformByObjectBone(vPos, presentation, 98);
            CreateParticleFpsChecked(BITMAP_SMOKE + 1, vPos, o->Angle, visual.movement.light, 1,
                                     Scale);

            b->TransformByObjectBone(vPos, presentation, 91);
            CreateParticleFpsChecked(BITMAP_SMOKE + 1, vPos, o->Angle, visual.movement.light, 1,
                                     Scale);

            b->TransformByObjectBone(vPos, presentation, 2);
            CreateParticleFpsChecked(BITMAP_SMOKE + 3, vPos, o->Angle, visual.movement.light, 3,
                                     Scale);

            b->TransformByObjectBone(vPos, presentation, 3);
            CreateParticleFpsChecked(BITMAP_SMOKE + 3, vPos, o->Angle, visual.movement.light, 3,
                                     Scale);
        }
        break;
        } //switch end
    }
        return true;
    } //switch end

    return false;
}

void GMEmpireGuardian3::MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b)
{
    switch (o->Type)
    {
    case MODEL_RAYMOND:
    case MODEL_DEFENDER:
    case MODEL_FORSAKER:
    case MODEL_OCELOT:
    case MODEL_ERIC: {
        g_EmpireGuardian1.MoveBlurEffect(c, o, b);
    }
    break;
    case MODEL_DUAL_BERSERKER: {
        vec3_t Light;
        vec3_t StartPos, StartRelative;
        vec3_t EndPos, EndRelative;
        float fActionSpeed, fSpeedPerFrame, fAnimationFrame;

        if (o->CurrentAction == MONSTER01_ATTACK1)
        {
            if (4.2f <= o->AnimationFrame && o->AnimationFrame < 6.6f)
            {
                Vector(0.9f, 0.5f, 0.4f, Light);

                fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed *
                               static_cast<float>(FPS_ANIMATION_FACTOR);
                fSpeedPerFrame = fActionSpeed / 5.f;
                fAnimationFrame = o->AnimationFrame - fActionSpeed;

                for (int i = 0; i < 3; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, o->HeadAngle);

                    Vector(0.0f, 0.0f, 0.0f, StartRelative);
                    Vector(0.0f, 0.0f, 0.0f, EndRelative);

                    b->TransformPosition(BoneTransform[24], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[25], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 2, false, 1);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }
            else if (10.2f <= o->AnimationFrame && o->AnimationFrame < 12.6f)
            {
                Vector(0.9f, 0.5f, 0.4f, Light);

                fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed *
                               static_cast<float>(FPS_ANIMATION_FACTOR);
                ;
                fSpeedPerFrame = fActionSpeed / 5.f;
                fAnimationFrame = o->AnimationFrame - fActionSpeed;

                for (int i = 0; i < 3; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, o->HeadAngle);

                    Vector(0.0f, 0.0f, 0.0f, StartRelative);
                    Vector(0.0f, 0.0f, 0.0f, EndRelative);

                    b->TransformPosition(BoneTransform[43], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[44], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 2, false, 0);
                    //b->TransformPosition(BoneTransform[24], StartRelative, StartPos, false);
                    //b->TransformPosition(BoneTransform[25], EndRelative, EndPos, false);
                    //CreateBlur(c, StartPos, EndPos, Light, 2, false, 1);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }
        }
        else if (o->CurrentAction == MONSTER01_ATTACK2)
        {
            if (6.2f <= o->AnimationFrame && o->AnimationFrame < 8.2f)
            {
                Vector(0.9f, 0.5f, 0.4f, Light);

                fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed *
                               static_cast<float>(FPS_ANIMATION_FACTOR);
                ;
                fSpeedPerFrame = fActionSpeed / 5.f;
                fAnimationFrame = o->AnimationFrame - fActionSpeed;
                for (int i = 0; i < 3; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, o->HeadAngle);

                    Vector(0.0f, 0.0f, 0.0f, StartRelative);
                    Vector(0.0f, 0.0f, 0.0f, EndRelative);

                    b->TransformPosition(BoneTransform[43], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[44], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 2, false, 0);
                    b->TransformPosition(BoneTransform[24], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[25], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 2, false, 1);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }
        }
        else if (o->CurrentAction == MONSTER01_ATTACK3)
        {
            vec3_t vRelative, vRelative2, vPosition;
            Vector(0.0f, 0.0f, 0.0f, vRelative);
            Vector(0.0f, 0.0f, 0.0f, vRelative2);

            if (6.2f <= o->AnimationFrame && o->AnimationFrame < 8.2f)
            {
                Vector(0.9f, 0.5f, 0.4f, Light);

                fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed *
                               static_cast<float>(FPS_ANIMATION_FACTOR);
                ;
                fSpeedPerFrame = fActionSpeed / 10.f;
                fAnimationFrame = o->AnimationFrame - fActionSpeed;
                for (int i = 0; i < 16; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, o->HeadAngle);

                    Vector(0.0f, 0.0f, 0.0f, StartRelative);
                    Vector(0.0f, 0.0f, 0.0f, EndRelative);

                    b->TransformPosition(BoneTransform[43], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[44], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 2, false, 10);
                    b->TransformPosition(BoneTransform[24], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[25], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 2, false, 21);

                    fAnimationFrame += fSpeedPerFrame;

                    if (7.4f <= o->AnimationFrame && o->AnimationFrame < 8.0f)
                    {
                        int iOffset = 80;

                        for (int i_ = 0; i_ < 8; i_++)
                        {
                            vRelative[0] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            vRelative[1] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            vRelative[2] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            b->TransformPosition(BoneTransform[24], vRelative, vPosition, false);
                            CreateParticleFpsChecked(BITMAP_FIRE, vPosition, o->Angle, o->Light, 0);

                            vRelative[0] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            vRelative[1] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            vRelative[2] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            b->TransformPosition(BoneTransform[25], vRelative, vPosition, false);
                            CreateParticleFpsChecked(BITMAP_FIRE, vPosition, o->Angle, o->Light, 0);

                            vRelative[0] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            vRelative[1] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            vRelative[2] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            b->TransformPosition(BoneTransform[43], vRelative, vPosition, false);
                            CreateParticleFpsChecked(BITMAP_FIRE, vPosition, o->Angle, o->Light, 0);

                            vRelative[0] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            vRelative[1] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            vRelative[2] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            b->TransformPosition(BoneTransform[44], vRelative, vPosition, false);
                            CreateParticleFpsChecked(BITMAP_FIRE, vPosition, o->Angle, o->Light, 0);

                            vec3_t vLight__;
                            Vector(0.9f, 0.9f, 0.9f, vLight__);

                            vRelative[0] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            vRelative[1] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            vRelative[2] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            b->TransformPosition(BoneTransform[24], vRelative, vPosition, false);
                            CreateParticleFpsChecked(BITMAP_SMOKELINE2, vPosition, o->Angle,
                                                     vLight__, 3);

                            vRelative[0] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            vRelative[1] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            vRelative[2] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            b->TransformPosition(BoneTransform[25], vRelative, vPosition, false);
                            CreateParticleFpsChecked(BITMAP_SMOKELINE2, vPosition, o->Angle,
                                                     vLight__, 3);

                            vRelative[0] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            vRelative[1] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            vRelative[2] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            b->TransformPosition(BoneTransform[43], vRelative, vPosition, false);
                            CreateParticleFpsChecked(BITMAP_SMOKELINE2, vPosition, o->Angle,
                                                     vLight__, 3);

                            vRelative[0] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            vRelative[1] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            vRelative[2] = ((WorldRandom() % iOffset) - iOffset * 0.5f);
                            b->TransformPosition(BoneTransform[44], vRelative, vPosition, false);
                            CreateParticleFpsChecked(BITMAP_SMOKELINE2, vPosition, o->Angle,
                                                     vLight__, 3);
                        }
                    } // ??
                }
            }
        }
    }
    break;
    case MODEL_HEAD_MOUNTER: {
        switch (o->CurrentAction)
        {
        case MONSTER01_ATTACK1: {
            if (4.2f <= o->AnimationFrame && o->AnimationFrame < 8.9f)
            {
                vec3_t Light;
                Vector(1.0f, 1.0f, 1.0f, Light);

                vec3_t StartPos, StartRelative;
                vec3_t EndPos, EndRelative;

                float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
                float fSpeedPerFrame = fActionSpeed / 5.f;
                float fAnimationFrame = o->AnimationFrame - fActionSpeed;
                for (int i = 0; i < 5; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, o->HeadAngle);

                    Vector(0.0f, 0.0f, 0.0f, StartRelative);
                    Vector(0.0f, 0.0f, 0.0f, EndRelative);

                    b->TransformPosition(BoneTransform[35], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[36], EndRelative, EndPos, false);
                    //CreateBlur(c, StartPos, EndPos, Light, 6);
                    CreateBlur(c, EndPos, StartPos, Light, 6);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }
        }
        break;
        } //switch end
    }
    break;
    } //switch end
}

bool GMEmpireGuardian3::AdvanceObjectVisual(OBJECT *o, BMD *b, float)
{
    if (gMapManager.IsEmpireGuardian3() == false)
        return false;

    vec3_t p, Position, Light;
    Vector(0.f, 30.f, 0.f, Position);
    Vector(0.f, 0.f, 0.f, p);

    switch (o->Type)
    {
    case 115:
    case 117:
        g_EmpireGuardian1.AdvanceGateProjectile(o, b);
        return true;
    case 12: {
        PrepareWorldObjectPose(*o);
        vec3_t vPos, vRelativePos, vLight1, vLight2;
        float flumi = absf(sinf(WorldTime * 0.0008)) * 0.9f + 0.1f;
        float fScale = o->Scale * 0.3f * flumi;
        Vector(0.f, 0.f, 0.f, vPos);
        Vector(8.f, -3.f, -3.f, vRelativePos);
        Vector(flumi, flumi, flumi, vLight1);
        Vector(0.9f, 0.1f, 0.1f, vLight2);
        b->TransformPosition(BoneTransform[2], vRelativePos, vPos);
#ifdef LDS_FIX_ACCESS_INDEXNUMBER_ALREADY_LOADTEXTURE
        CreateSprite(BITMAP_SHINY + 6, vPos, 0.5f, vLight2, o);
        CreateSprite(BITMAP_SHINY + 6, vPos, fScale, vLight1, o);
        Vector(3.f, -3.f, -3.5f, vRelativePos);
        b->TransformPosition(BoneTransform[3], vRelativePos, vPos);
        CreateSprite(BITMAP_SHINY + 6, vPos, 0.5f, vLight2, o);
        CreateSprite(BITMAP_SHINY + 6, vPos, fScale, vLight1, o);
#else  // LDS_FIX_ACCESS_INDEXNUMBER_ALREADY_LOADTEXTURE
        CreateSprite(BITMAP_SHINY + 5, vPos, 0.5f, vLight2, o);
        CreateSprite(BITMAP_SHINY + 5, vPos, fScale, vLight1, o);
        Vector(3.f, -3.f, -3.5f, vRelativePos);
        b->TransformPosition(BoneTransform[3], vRelativePos, vPos);
        CreateSprite(BITMAP_SHINY + 5, vPos, 0.5f, vLight2, o);
        CreateSprite(BITMAP_SHINY + 5, vPos, fScale, vLight1, o);
#endif // LDS_FIX_ACCESS_INDEXNUMBER_ALREADY_LOADTEXTURE
    }
        return true;
    case 20: {
        if (o->AnimationFrame > 5.4f && o->AnimationFrame < 6.5f)
        {
            vec3_t Angle;
            for (int i = 0; i < 4; ++i)
            {
                Vector((float)(WorldRandom() % 60 + 60 + 90), 0.f, o->Angle[2] + 180, Angle);
                CreateJointFpsChecked(BITMAP_JOINT_SPARK, o->Position, o->Position, Angle, 5, o);
            }
            CreateParticleFpsChecked(BITMAP_SPARK, o->Position, Angle, o->Light, 11);
        }
        else if (o->AnimationFrame > 15.4f && o->AnimationFrame < 16.5f)
        {
            vec3_t Angle;
            for (int i = 0; i < 4; ++i)
            {
                Vector((float)(WorldRandom() % 60 + 60 + 90), 0.f, o->Angle[2], Angle);
                CreateJointFpsChecked(BITMAP_JOINT_SPARK, o->Position, o->Position, Angle, 5, o);
            }
            CreateParticleFpsChecked(BITMAP_SPARK, o->Position, Angle, o->Light, 11);
        }
    }
        return true;

    case 37: {
        PrepareWorldObjectPose(*o);
        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(BoneTransform[1], p, Position);

        float fLumi;
        fLumi = (sinf(WorldTime * 0.039f) + 1.0f) * 0.2f + 0.6f;
        vec3_t vLightFire;
        Vector(fLumi * 0.7f, fLumi * 0.7f, fLumi * 0.7f, vLightFire);
        CreateSprite(BITMAP_FLARE, Position, 4.0f * o->Scale, vLightFire, o);
    }
        return true;

    case 50: {
        PrepareWorldObjectPose(*o);
        vec3_t vPos, vRelativePos, vLight1, vLight2, vAngle;
        Vector(0.f, 0.f, 0.f, vPos);
        Vector(0.f, 0.f, 5.f, vRelativePos);
        Vector(0.0f, -1.0f, 0.0f, vAngle);
        Vector(0.05f, 0.1f, 0.3f, vLight1);
        Vector(1.f, 1.f, 1.f, vLight2);

        for (int i = 2; i <= 7; i++)
        {
            b->TransformPosition(BoneTransform[i], vRelativePos, vPos);
            CreateParticleFpsChecked(BITMAP_FIRE_HIK3_MONO, vPos, vAngle, vLight1, 4,
                                     o->Scale * 0.6f);
            CreateParticleFpsChecked(BITMAP_FIRE_HIK3_MONO, vPos, vAngle, vLight2, 4,
                                     o->Scale * 0.3f);
        }
    }
        return true;

    case 51: {
        if (o->AnimationFrame > 5.4f && o->AnimationFrame < 6.5f)
        {
            vec3_t Angle;
            for (int i = 0; i < 4; ++i)
            {
                Vector((float)(WorldRandom() % 60 + 60 + 90), 0.f, o->Angle[2] + 180, Angle);
                CreateJointFpsChecked(BITMAP_JOINT_SPARK, o->Position, o->Position, Angle, 5, o);
            }
            CreateParticleFpsChecked(BITMAP_SPARK, o->Position, Angle, o->Light, 11);
        }
    }
        return true;

    case 64: {
        if ((o->AnimationFrame > 9.5f && o->AnimationFrame < 11.5f) ||
            (o->AnimationFrame > 23.5f && o->AnimationFrame < 25.5f))
        {
            float Matrix[3][4];
            vec3_t vAngle, vDirection, vPosition;
            Vector(0.f, 0.f, o->Angle[2] + 90, vAngle);
            AngleMatrix(vAngle, Matrix);
            Vector(0.f, 30.0f, 0.f, vDirection);
            VectorRotate(vDirection, Matrix, vPosition);
            VectorAdd(vPosition, o->Position, Position);

            Vector(0.04f, 0.03f, 0.02f, Light);
            for (int i = 0; i < 3; ++i)
            {
                CreateParticleFpsChecked(BITMAP_CLOUD, Position, o->Angle, Light, 22, o->Scale, o);
            }
        }
    }
        return true;

    case 79: {
        vec3_t vLightFire;
        Vector(1.0f, 0.2f, 0.0f, vLightFire);
        CreateSprite(BITMAP_LIGHT, o->Position, 2.0f * o->Scale, vLightFire, o);

        vec3_t vLight;
        Vector(1.0f, 1.0f, 1.0f, vLight);

        switch (WorldRandom() % 3)
        {
        case 0:
            CreateParticleFpsChecked(BITMAP_FIRE_HIK1, o->Position, o->Angle, vLight, 0, o->Scale);
            break;
        case 1:
            CreateParticleFpsChecked(BITMAP_FIRE_CURSEDLICH, o->Position, o->Angle, vLight, 4,
                                     o->Scale);
            break;
        case 2:
            CreateParticleFpsChecked(BITMAP_FIRE_HIK3, o->Position, o->Angle, vLight, 0, o->Scale);
            break;
        }
    }
        return true;
    case 80: {
        float fLumi;
        fLumi = (sinf(WorldTime * 0.04f) + 1.0f) * 0.3f + 0.4f;
        vec3_t vLightFire;
        Vector(fLumi * 0.1f, fLumi * 0.1f, fLumi * 0.5f, vLightFire);
        CreateSprite(BITMAP_LIGHT, o->Position, 8.0f * o->Scale, vLightFire, o);
    }
        return true;
    case 82: {
        Vector(1.f, 1.f, 1.f, Light);
        CreateParticleFpsChecked(BITMAP_WATERFALL_5, o->Position, o->Angle, Light, 9, o->Scale);
    }
        return true;

    case 83: {
        Vector(1.f, 1.f, 1.f, Light);
        CreateParticleFpsChecked(BITMAP_WATERFALL_3, o->Position, o->Angle, Light, 14, o->Scale);
    }
        return true;

    case 84: {
        Vector(1.f, 1.f, 1.f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 8.f))
        {
            CreateParticle(BITMAP_WATERFALL_2, o->Position, o->Angle, Light, 4, o->Scale);
        }
    }
        return true;

    case 85:
        sessionKeeper_.Visual()->EmitPeriodicFlames(*o);
        return true;

    case 86: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 6.f))
        {
            Vector(0.05f, 0.02f, 0.01f, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 21, o->Scale, o);
        }
    }
        return true;
    case 129: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 6.f))
        {
            Vector(0.01f, 0.02f, 0.05f, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 21, o->Scale, o);
        }
    }
        return true;
    case 130: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 6.f))
        {
            Vector(0.01f, 0.05f, 0.02f, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 21, o->Scale, o);
        }
    }
        return true;
    case 131: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 22, o->Scale);

            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 21, o->Scale * 2.0f, o);
        }
    }
        return true;
    case 132: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 60, o->Scale, o);

            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 60, o->Scale, o);

            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 21, o->Scale * 2.0f, o);
        }
    }
        return true;
    }
    return false;
}

bool GMEmpireGuardian3::AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                             WorldCharacterVisualState &visual)
{
    if (g_EmpireGuardian1.AdvanceMonsterVisual(c, o, b, visual))
    {
        return true;
    }

    switch (o->Type)
    {
    case MODEL_DUAL_BERSERKER: {
        vec3_t Light, Position;

        Vector(0.4f, 0.4f, 0.4f, Light);
        b->TransformByObjectBone(Position, o, 9);
        CreateSprite(BITMAP_LIGHT_RED, Position, 0.4f, Light, o);
        CreateSprite(BITMAP_LIGHT_RED, Position, 1.2f, Light, o);
        b->TransformByObjectBone(Position, o, 10);
        CreateSprite(BITMAP_LIGHT_RED, Position, 0.4f, Light, o);
        CreateSprite(BITMAP_LIGHT_RED, Position, 1.2f, Light, o);
        b->TransformByObjectBone(Position, o, 28);
        CreateSprite(BITMAP_LIGHT_RED, Position, 0.4f, Light, o);
        CreateSprite(BITMAP_LIGHT_RED, Position, 1.2f, Light, o);
        b->TransformByObjectBone(Position, o, 29);
        CreateSprite(BITMAP_LIGHT_RED, Position, 0.4f, Light, o);
        CreateSprite(BITMAP_LIGHT_RED, Position, 1.2f, Light, o);
        b->TransformByObjectBone(Position, o, 47);
        CreateSprite(BITMAP_LIGHT_RED, Position, 0.4f, Light, o);
        CreateSprite(BITMAP_LIGHT_RED, Position, 1.2f, Light, o);
        b->TransformByObjectBone(Position, o, 48);
        CreateSprite(BITMAP_LIGHT_RED, Position, 0.4f, Light, o);
        CreateSprite(BITMAP_LIGHT_RED, Position, 1.2f, Light, o);
        b->TransformByObjectBone(Position, o, 123);
        CreateSprite(BITMAP_LIGHT_RED, Position, 0.4f, Light, o);
        CreateSprite(BITMAP_LIGHT_RED, Position, 1.2f, Light, o);
        b->TransformByObjectBone(Position, o, 124);
        CreateSprite(BITMAP_LIGHT_RED, Position, 0.4f, Light, o);
        CreateSprite(BITMAP_LIGHT_RED, Position, 1.2f, Light, o);
        b->TransformByObjectBone(Position, o, 119);
        CreateSprite(BITMAP_LIGHT_RED, Position, 0.4f, Light, o);
        CreateSprite(BITMAP_LIGHT_RED, Position, 1.2f, Light, o);
        b->TransformByObjectBone(Position, o, 120);
        CreateSprite(BITMAP_LIGHT_RED, Position, 0.4f, Light, o);
        CreateSprite(BITMAP_LIGHT_RED, Position, 1.2f, Light, o);
        b->TransformByObjectBone(Position, o, 125);
        CreateSprite(BITMAP_LIGHT_RED, Position, 0.4f, Light, o);
        CreateSprite(BITMAP_LIGHT_RED, Position, 1.2f, Light, o);
        b->TransformByObjectBone(Position, o, 126);
        CreateSprite(BITMAP_LIGHT_RED, Position, 0.4f, Light, o);
        CreateSprite(BITMAP_LIGHT_RED, Position, 1.2f, Light, o);

        Vector(0.9f, 0.9f, 0.9f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            const float fraction = birth.FrameFraction();
            ObjectDrawInput sampled(o);
            o->MotionTrace.Sample(WorldTime, fraction, o->Position, sampled.position);
            AnimationPoseSample pose(sampled, b->BoneHead, b->BodyHeight, false,
                                     b->PoseAssetIdentity());
            std::array<vec34_t, MAX_BONES> bones;
            sampled.bones = pose.EvaluateAtTime(*b, *o, WorldTime, fraction, bones.data());
            b->TransformByObjectBone(Position, sampled, 54);
            CreateParticle(BITMAP_SMOKELINE2, Position, o->Angle, Light, 3, 0.2f);
            b->TransformByObjectBone(Position, sampled, 58);
            CreateParticle(BITMAP_SMOKELINE2, Position, o->Angle, Light, 3, 0.2f);
            b->TransformByObjectBone(Position, sampled, 64);
            CreateParticle(BITMAP_SMOKELINE2, Position, o->Angle, Light, 3, 0.2f);
            b->TransformByObjectBone(Position, sampled, 65);
            CreateParticle(BITMAP_SMOKELINE2, Position, o->Angle, Light, 3, 0.2f);
            b->TransformByObjectBone(Position, sampled, 69);
            CreateParticle(BITMAP_SMOKELINE2, Position, o->Angle, Light, 3, 0.2f);
            b->TransformByObjectBone(Position, sampled, 70);
            CreateParticle(BITMAP_SMOKELINE2, Position, o->Angle, Light, 3, 0.2f);
            b->TransformByObjectBone(Position, sampled, 77);
            CreateParticle(BITMAP_SMOKELINE2, Position, o->Angle, Light, 3, 0.2f);
            b->TransformByObjectBone(Position, sampled, 78);
            CreateParticle(BITMAP_SMOKELINE2, Position, o->Angle, Light, 3, 0.2f);
            b->TransformByObjectBone(Position, sampled, 83);
            CreateParticle(BITMAP_SMOKELINE2, Position, o->Angle, Light, 3, 0.2f);
            b->TransformByObjectBone(Position, sampled, 87);
            CreateParticle(BITMAP_SMOKELINE2, Position, o->Angle, Light, 3, 0.2f);
            b->TransformByObjectBone(Position, sampled, 91);
            CreateParticle(BITMAP_SMOKELINE2, Position, o->Angle, Light, 3, 0.2f);
            b->TransformByObjectBone(Position, sampled, 92);
            CreateParticle(BITMAP_SMOKELINE2, Position, o->Angle, Light, 3, 0.2f);
        }

        if (g_isNotCharacterBuff(o) == true && g_isCharacterBuff(o, eBuff_Berserker) == true)
        {
            sessionKeeper_.Visual()->EmitBerserkerSmoke(*o, *b, 6);
        }
    }
        return true;
    case MODEL_BANSHEE: {
    }
        return true;
    }
    return false;
}

bool GMEmpireGuardian3::CreateRain(PARTICLE *o)
{
    return g_EmpireGuardian1.CreateRain(o);
}

bool GMEmpireGuardian3::AttackEffectMonster(CHARACTER *c, OBJECT *o, BMD *b)
{
    if (gMapManager.IsEmpireGuardian3() == false)
        return false;
    return false;
}

bool GMEmpireGuardian3::PlayMonsterSound(OBJECT *o)
{
    if (gMapManager.IsEmpireGuardian3() == false)
        return false;

    if (true == g_EmpireGuardian1.PlayMonsterSound(o))
    {
        return true;
    }

    float fDis_x, fDis_y;
    fDis_x = o->Position[0] - Hero->Object.Position[0];
    fDis_y = o->Position[1] - Hero->Object.Position[1];
    float fDistance = sqrtf(fDis_x * fDis_x + fDis_y * fDis_y);

    if (fDistance > 500.0f)
        return true;

    switch (o->Type)
    {
    case MODEL_DUAL_BERSERKER: {
        switch (o->CurrentAction)
        {
        case MONSTER01_WALK: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_3CORP_CATO_MOVE);
        }
        break;
        case MONSTER01_ATTACK1: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_COMBATMASTER_ATTACK01);
        }
        break;
        case MONSTER01_ATTACK2:
        case MONSTER01_ATTACK3: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_3CORP_CATO_ATTACK02);
        }
        break;
        case MONSTER01_APEAR: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_RAYMOND_MONSTER_RAGE);
        }
        break;
        case MONSTER01_DIE: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_2CORP_VERMONT_MONSTER_DEATH);
        }
        break;
        }
    }
        return true;

    case MODEL_BANSHEE: {
        switch (o->CurrentAction)
        {
        case MONSTER01_WALK: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_BOSS_GAION_MONSTER_MOVE);
        }
        break;
        case MONSTER01_ATTACK1: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_COMBATMASTER_ATTACK01);
        }
        break;
        case MONSTER01_ATTACK2:
        case MONSTER01_ATTACK3: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_COMBATMASTER_ATTACK02);
        }
        break;
        case MONSTER01_DIE: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_ASSASSINMASTER_DEATH);
        }
        break;
        }
    }
        return true;

    case MODEL_HEAD_MOUNTER: {
        switch (o->CurrentAction)
        {
        case MONSTER01_WALK: {
            if (7.0f <= o->AnimationFrame && o->AnimationFrame < 8.0f)
            {
                PlayBuffer(SOUND_EMPIREGUARDIAN_CAVALRYLEADER_MOVE01);
            }
            if (1.0f <= o->AnimationFrame && o->AnimationFrame < 2.0f)
            {
                PlayBuffer(SOUND_EMPIREGUARDIAN_CAVALRYLEADER_MOVE02);
            }
        }
        break;
        case MONSTER01_ATTACK1:
        case MONSTER01_ATTACK3: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_CAVALRYLEADER_ATTACK01);
        }
        break;
        case MONSTER01_ATTACK2:
        case MONSTER01_ATTACK4: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_CAVALRYLEADER_ATTACK02);
        }
        break;
        case MONSTER01_DIE: {
            PlayBuffer(SOUND_RAKLION_GIANT_MAMUD_DEATH);
        }
        break;
        }
    }
        return true;
    }

    return false;
}

void GMEmpireGuardian3::PlayObjectSound(OBJECT *o)
{
    g_EmpireGuardian1.PlayObjectSound(o);
}

void GMEmpireGuardian3::PlayBGM()
{
    if (gMapManager.IsEmpireGuardian3())
    {
        PlayMp3(MUSIC_EMPIREGUARDIAN3);
    }
    else
    {
        StopMp3(MUSIC_EMPIREGUARDIAN3);
    }
}

bool GMEmpireGuardian3::MoveMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                          WorldCharacterVisualState &visual)
{
    return g_EmpireGuardian1.MoveMonsterVisual(c, o, b, visual);
}

void GMEmpireGuardian3::AdvanceEnvironment()
{
    g_EmpireGuardian1.AdvanceWeather();
}

void GMEmpireGuardian3::UpdateMusic()
{
    PlayBGM();
}

bool GMEmpireGuardian3::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_EMPIREGUARDIAN3) == 0;
}

bool GMEmpireGuardian3::CreateWeather(PARTICLE *particle, int)
{
    return CreateRain(particle);
}

bool GMEmpireGuardian4::MoveStructureVisual(OBJECT *o, BMD *b)
{
    if (gMapManager.IsEmpireGuardian4() == false)
        return false;

    if (true == g_EmpireGuardian1.MoveStructureVisual(o, b))
    {
        return true;
    }

    switch (o->Type)
    {
    case MODEL_RUSH_GATE: {
        if (o->CurrentAction == MONSTER01_DIE)
        {
            if ((int)o->LifeTime == 100)
            {
                o->LifeTime = 90;

                vec3_t vPos, vRelativePos;
                Vector(200.0f, 0.0f, 0.0f, vRelativePos);
                b->TransformPosition(o->BoneTransform[0], vRelativePos, vPos, true);
                CreateEffect(MODEL_DOOR_CRUSH_EFFECT, vPos, o->Angle, o->Light, 1, o, 0, 0);
            }
        }
    }
        return true;
    }

    return false;
}

bool GMEmpireGuardian4::MoveMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                          WorldCharacterVisualState &visual)
{
    ObjectDrawInput presentation(o);
    visual.movement.Apply(presentation);
    if (gMapManager.IsEmpireGuardian4() == false)
        return false;

    if (true == g_EmpireGuardian1.MoveMonsterVisual(c, o, b, visual))
    {
        return true;
    }

    switch (o->Type)
    {
    case MODEL_GAYION: {
        switch (visual.action)
        {
        case MONSTER01_ATTACK1: {
            CreateEffect(MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_, o->Position, o->Angle,
                         visual.movement.light, 0, o, -1, 0, 0, 0, o->Scale);
            CreateEffect(MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_, o->Position, o->Angle,
                         visual.movement.light, 0, o, -1, 0, 0, 0, o->Scale);
            CreateEffect(MODEL_SWORDMAIN01_EMPIREGUARDIAN_BOSS_GAION_, o->Position, o->Angle,
                         visual.movement.light, 0, o, -1, 0, 0, 0, o->Scale);


        }
        break;
        case MONSTER01_ATTACK2: {
            CreateEffect(MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_, o->Position, o->Angle,
                         visual.movement.light, 0, o, -1, 0, 0, 0, o->Scale);
            CreateEffect(MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_, o->Position, o->Angle,
                         visual.movement.light, 0, o, -1, 0, 0, 0, o->Scale);
            CreateEffect(MODEL_SWORDMAIN01_EMPIREGUARDIAN_BOSS_GAION_, o->Position, o->Angle,
                         visual.movement.light, 0, o, -1, 0, 0, 0, o->Scale);


        }
        break;
        case MONSTER01_ATTACK3: {

        }
        break;
        case MONSTER01_ATTACK4: {
            CreateEffect(MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_, o->Position, o->Angle,
                         visual.movement.light, 0, o, -1, 0, 0, 0, o->Scale);
            CreateEffect(MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_, o->Position, o->Angle,
                         visual.movement.light, 0, o, -1, 0, 0, 0, o->Scale);
            CreateEffect(MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_, o->Position, o->Angle,
                         visual.movement.light, 0, o, -1, 0, 0, 0, o->Scale);
            CreateEffect(MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_, o->Position, o->Angle,
                         visual.movement.light, 0, o, -1, 0, 0, 0, o->Scale);


        }
        break;
        case MONSTER01_APEAR: {
            CreateEffect(MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_, o->Position, o->Angle,
                         visual.movement.light, 0, o, -1, 0, 0, 0, o->Scale);
            CreateEffect(MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_, o->Position, o->Angle,
                         visual.movement.light, 0, o, -1, 0, 0, 0, o->Scale);
            CreateEffect(MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_, o->Position, o->Angle,
                         visual.movement.light, 0, o, -1, 0, 0, 0, o->Scale);
            CreateEffect(MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_, o->Position, o->Angle,
                         visual.movement.light, 0, o, -1, 0, 0, 0, o->Scale);
            CreateEffect(MODEL_SWORDMAIN01_EMPIREGUARDIAN_BOSS_GAION_, o->Position, o->Angle,
                         visual.movement.light, 0, o, -1, 0, 0, 0, o->Scale);
            {
                vec3_t Light;

                if (m_bCurrentIsRage_BossGaion == true)
                {
                    Vector(1.0f, 1.0f, 1.0f, Light);
                    CreateInferno(o->Position);
                    CreateEffect(MODEL_CIRCLE, o->Position, o->Angle, Light, 4, o);
                    CreateEffect(MODEL_CIRCLE, o->Position, o->Angle, Light, 4, o);
                    m_bCurrentIsRage_BossGaion = false;
                }
            }
        }
        break;
        default: {
            CreateEffect(MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_, o->Position, o->Angle,
                         visual.movement.light, 0, o, -1, 0, 0, 0, o->Scale);
            CreateEffect(MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_, o->Position, o->Angle,
                         visual.movement.light, 0, o, -1, 0, 0, 0, o->Scale);
            CreateEffect(MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_, o->Position, o->Angle,
                         visual.movement.light, 0, o, -1, 0, 0, 0, o->Scale);
            CreateEffect(MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_, o->Position, o->Angle,
                         visual.movement.light, 0, o, -1, 0, 0, 0, o->Scale);
            CreateEffect(MODEL_SWORDMAIN01_EMPIREGUARDIAN_BOSS_GAION_, o->Position, o->Angle,
                         visual.movement.light, 0, o, -1, 0, 0, 0, o->Scale);
        }
        break;
        }
    }
        return true;
    case MODEL_DEATH_ANGEL_3:
    case MODEL_JERRY: {
        switch (visual.action)
        {
        case MONSTER01_STOP1:
        case MONSTER01_STOP2:
            break;
        case MONSTER01_WALK:
            break;
        case MONSTER01_DIE: {
        }
        break;
        case MONSTER01_ATTACK1: {
            if (7.5f <= visual.animationFrame && visual.animationFrame < 10.8f)
            {
                vec3_t Light;
                Vector(1.0f, 0.5f, 0.2f, Light);

                vec3_t vPosBlur01, vPosBlurRelative01;
                vec3_t vPosBlur02, vPosBlurRelative02;

                float fActionSpeed = b->Actions[visual.action].PlaySpeed;
                float fSpeedPerFrame = fActionSpeed / 10.f;
                float fAnimationFrame = visual.animationFrame - fActionSpeed;

                int iSwordForceType = 0;

                for (int i = 0; i < 14; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, presentation.headAngle);

                    Vector(0.0f, 0.0f, 0.0f, vPosBlurRelative01);
                    Vector(0.0f, 0.0f, 0.0f, vPosBlurRelative02);

                    b->TransformPosition(BoneTransform[61], vPosBlurRelative01, vPosBlur01, false);
                    b->TransformPosition(BoneTransform[51], vPosBlurRelative02, vPosBlur02, false);

                    CreateObjectBlur(o, vPosBlur01, vPosBlur02, Light, iSwordForceType, true, 1,
                                     30);
                    CreateObjectBlur(o, vPosBlur01, vPosBlur02, Light, iSwordForceType, true, 2,
                                     30);

                    b->TransformPosition(BoneTransform[52], vPosBlurRelative01, vPosBlur01, false);
                    b->TransformPosition(BoneTransform[60], vPosBlurRelative02, vPosBlur02, false);

                    CreateObjectBlur(o, vPosBlur01, vPosBlur02, Light, iSwordForceType, true, 11,
                                     30);
                    CreateObjectBlur(o, vPosBlur01, vPosBlur02, Light, iSwordForceType, true, 12,
                                     30);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }
        }
        break;
        case MONSTER01_ATTACK2: {
            if (visual.animationFrame >= 3.0f && visual.animationFrame <= 5.0f)
            {
                CreateEffect(BITMAP_GATHERING, o->Position, o->Angle, visual.movement.light, 1, o);
            }

            if (8.0f <= visual.animationFrame && visual.animationFrame < 10.1f)
            {
                float fActionSpeed = b->Actions[visual.action].PlaySpeed;
                float fSpeedPerFrame = fActionSpeed / 10.f;
                float fAnimationFrame = visual.animationFrame - fActionSpeed;

                vec3_t vRelative, vPosition, vRelative2;

                vec3_t vAngle, vRandomDir, vRandomDirPosition, vResultRandomPosition;
                vec34_t matRandomRotation;
                Vector(0.0f, 0.0f, 0.0f, vAngle);

                Vector(0.0f, 0.0f, 0.0f, vRandomDirPosition);

                Vector(0.0f, 0.0f, 0.0f, vRelative);
                Vector(0.0f, 0.0f, 0.0f, vRelative2);
                for (int i = 0; i < 100; i++)
                {
                    float fRandDistance = (float)(WorldRandom() % 100) + 100;
                    Vector(0.0f, fRandDistance, 0.0f, vRandomDir);

                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, presentation.headAngle);

                    b->TransformPosition(BoneTransform[61], vRelative, vPosition, false);
                    CreateParticleFpsChecked(BITMAP_FIRE, vPosition, o->Angle,
                                             visual.movement.light, 0);

                    Vector((float)(WorldRandom() % 360), 0.f, (float)(WorldRandom() % 360), vAngle);
                    AngleMatrix(vAngle, matRandomRotation);
                    VectorRotate(vRandomDir, matRandomRotation, vRandomDirPosition);
                    VectorAdd(vPosition, vRandomDirPosition, vResultRandomPosition);
                    CreateJointFpsChecked(BITMAP_JOINT_THUNDER, vResultRandomPosition, vPosition,
                                          vAngle, 3, NULL, 10.f, 10, 10);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }

            if (6.0f <= visual.animationFrame && visual.animationFrame < 10.1f)
            {
                vec3_t Light;
                Vector(1.0f, 1.0f, 1.0f, Light);

                vec3_t vPosBlur01, vPosBlurRelative01;
                vec3_t vPosBlur02, vPosBlurRelative02;

                float fActionSpeed = b->Actions[visual.action].PlaySpeed;
                float fSpeedPerFrame = fActionSpeed / 10.f;
                float fAnimationFrame = visual.animationFrame - fActionSpeed;

                for (int i = 0; i < 40; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, presentation.headAngle);

                    Vector(0.0f, 0.0f, 0.0f, vPosBlurRelative01);
                    Vector(0.0f, 0.0f, 0.0f, vPosBlurRelative02);

                    b->TransformPosition(BoneTransform[61], vPosBlurRelative01, vPosBlur01, false);
                    b->TransformPosition(BoneTransform[51], vPosBlurRelative02, vPosBlur02, false);

                    CreateBlur(c, vPosBlur01, vPosBlur02, Light, 2);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }
        }
        break;
        case MONSTER01_ATTACK3: {


            if (9.8f <= visual.animationFrame && visual.animationFrame < 14.0f)
            {
                vec3_t Light;
                Vector(1.0f, 1.0f, 1.0f, Light);

                vec3_t vPosBlur03, vPosBlurRelative03;
                vec3_t vPosBlur04, vPosBlurRelative04;

                float fActionSpeed = b->Actions[visual.action].PlaySpeed;
                float fSpeedPerFrame = fActionSpeed / 10.f;

                float fAnimationFrame = visual.animationFrame - fActionSpeed;
                for (int i = 0; i < 10; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, presentation.headAngle);

                    Vector(0.0f, 0.0f, 0.0f, vPosBlurRelative03);
                    Vector(0.0f, 0.0f, 0.0f, vPosBlurRelative04);

                    b->TransformPosition(BoneTransform[52], vPosBlurRelative04, vPosBlur04, false);
                    b->TransformPosition(BoneTransform[58], vPosBlurRelative04, vPosBlur03, false);

                    CreateObjectBlur(o, vPosBlur03, vPosBlur04, Light, 10, false, 0);
                    CreateObjectBlur(o, vPosBlur03, vPosBlur04, Light, 10, false, 1);
                    CreateObjectBlur(o, vPosBlur03, vPosBlur04, Light, 10, false, 2);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }
        }
        break;
        case MONSTER01_APEAR: {
            {
                vec3_t Light;

                if (m_bCurrentIsRage_Jerint == true)
                {
                    Vector(1.0f, 1.0f, 1.0f, Light);
                    CreateInferno(o->Position);

                    CreateEffect(MODEL_CIRCLE, o->Position, o->Angle, Light, 4, o);
                    CreateEffect(MODEL_CIRCLE, o->Position, o->Angle, Light, 4, o);
                    m_bCurrentIsRage_Jerint = false;
                }
            }
        }
        break;
        }
    }
        return true;
    }
    return MoveStructureVisual(o, b);
}

void GMEmpireGuardian4::MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b)
{
    switch (o->Type)
    {
    case MODEL_RAYMOND:
    case MODEL_DEFENDER:
    case MODEL_FORSAKER:
    case MODEL_OCELOT:
    case MODEL_ERIC: {
        g_EmpireGuardian1.MoveBlurEffect(c, o, b);
    }
    break;
    }
}

bool GMEmpireGuardian4::AdvanceObjectVisual(OBJECT *o, BMD *b, float)
{
    if (gMapManager.IsEmpireGuardian4() == false)
        return false;

    vec3_t p, Position, Light;
    Vector(0.f, 30.f, 0.f, Position);
    Vector(0.f, 0.f, 0.f, p);

    switch (o->Type)
    {
    case 115:
    case 117:
        g_EmpireGuardian1.AdvanceGateProjectile(o, b);
        return true;
    case 12: {
        PrepareWorldObjectPose(*o);
        vec3_t vPos, vRelativePos, vLight1, vLight2;
        float flumi = absf(sinf(WorldTime * 0.0008)) * 0.9f + 0.1f;
        float fScale = o->Scale * 0.3f * flumi;
        Vector(0.f, 0.f, 0.f, vPos);
        Vector(8.f, -3.f, -3.f, vRelativePos);
        Vector(flumi, flumi, flumi, vLight1);
        Vector(0.9f, 0.1f, 0.1f, vLight2);
        b->TransformPosition(BoneTransform[2], vRelativePos, vPos);
#ifdef LDS_FIX_ACCESS_INDEXNUMBER_ALREADY_LOADTEXTURE
        CreateSprite(BITMAP_SHINY + 6, vPos, 0.5f, vLight2, o);
        CreateSprite(BITMAP_SHINY + 6, vPos, fScale, vLight1, o);
        Vector(3.f, -3.f, -3.5f, vRelativePos);
        b->TransformPosition(BoneTransform[3], vRelativePos, vPos);
        CreateSprite(BITMAP_SHINY + 6, vPos, 0.5f, vLight2, o);
        CreateSprite(BITMAP_SHINY + 6, vPos, fScale, vLight1, o);
#else  // LDS_FIX_ACCESS_INDEXNUMBER_ALREADY_LOADTEXTURE
        CreateSprite(BITMAP_SHINY + 5, vPos, 0.5f, vLight2, o);
        CreateSprite(BITMAP_SHINY + 5, vPos, fScale, vLight1, o);
        Vector(3.f, -3.f, -3.5f, vRelativePos);
        b->TransformPosition(BoneTransform[3], vRelativePos, vPos);
        CreateSprite(BITMAP_SHINY + 5, vPos, 0.5f, vLight2, o);
        CreateSprite(BITMAP_SHINY + 5, vPos, fScale, vLight1, o);
#endif // LDS_FIX_ACCESS_INDEXNUMBER_ALREADY_LOADTEXTURE
    }
        return true;

    case 20: {
        if (o->AnimationFrame > 5.4f && o->AnimationFrame < 6.5f)
        {
            vec3_t Angle;
            for (int i = 0; i < 4; ++i)
            {
                Vector((float)(WorldRandom() % 60 + 60 + 90), 0.f, o->Angle[2] + 180, Angle);
                CreateJointFpsChecked(BITMAP_JOINT_SPARK, o->Position, o->Position, Angle, 5, o);
            }
            CreateParticleFpsChecked(BITMAP_SPARK, o->Position, Angle, o->Light, 11);
        }
        else if (o->AnimationFrame > 15.4f && o->AnimationFrame < 16.5f)
        {
            vec3_t Angle;
            for (int i = 0; i < 4; ++i)
            {
                Vector((float)(WorldRandom() % 60 + 60 + 90), 0.f, o->Angle[2], Angle);
                CreateJointFpsChecked(BITMAP_JOINT_SPARK, o->Position, o->Position, Angle, 5, o);
            }
            CreateParticleFpsChecked(BITMAP_SPARK, o->Position, Angle, o->Light, 11);
        }
    }
        return true;

    case 37: {
        PrepareWorldObjectPose(*o);
        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(BoneTransform[1], p, Position);

        float fLumi;
        fLumi = (sinf(WorldTime * 0.039f) + 1.0f) * 0.2f + 0.6f;
        vec3_t vLightFire;
        Vector(fLumi * 0.7f, fLumi * 0.7f, fLumi * 0.7f, vLightFire);
        CreateSprite(BITMAP_FLARE, Position, 4.0f * o->Scale, vLightFire, o);
    }
        return true;

    case 50: {
        // The shipped Object73/Object51 rig has no fire anchors. Keep its geometry;
        // only local-scene versions of this slot provide the six emitter bones.
        PrepareWorldObjectPose(*o);
        if (gMapManager.ContextMap() == WD_72EMPIREGUARDIAN4)
            return true;

        vec3_t vPos, vRelativePos, vLight1, vLight2, vAngle;
        Vector(0.f, 0.f, 0.f, vPos);
        Vector(0.f, 0.f, 5.f, vRelativePos);
        Vector(0.0f, -1.0f, 0.0f, vAngle);
        Vector(0.05f, 0.1f, 0.3f, vLight1);
        Vector(1.f, 1.f, 1.f, vLight2);

        for (int i = 2; i <= 7; i++)
        {
            b->TransformPosition(BoneTransform[i], vRelativePos, vPos);
            CreateParticleFpsChecked(BITMAP_FIRE_HIK3_MONO, vPos, vAngle, vLight1, 4,
                                     o->Scale * 0.6f);
            CreateParticleFpsChecked(BITMAP_FIRE_HIK3_MONO, vPos, vAngle, vLight2, 4,
                                     o->Scale * 0.3f);
        }
    }
        return true;

    case 64: {
        if ((o->AnimationFrame > 9.5f && o->AnimationFrame < 11.5f) ||
            (o->AnimationFrame > 23.5f && o->AnimationFrame < 25.5f))
        {
            float Matrix[3][4];
            vec3_t vAngle, vDirection, vPosition;
            Vector(0.f, 0.f, o->Angle[2] + 90, vAngle);
            AngleMatrix(vAngle, Matrix);
            Vector(0.f, 30.0f, 0.f, vDirection);
            VectorRotate(vDirection, Matrix, vPosition);
            VectorAdd(vPosition, o->Position, Position);

            Vector(0.04f, 0.03f, 0.02f, Light);
            for (int i = 0; i < 3; ++i)
            {
                CreateParticleFpsChecked(BITMAP_CLOUD, Position, o->Angle, Light, 22, o->Scale, o);
            }
        }
    }
        return true;

    case 79: {
        vec3_t vLightFire;
        Vector(1.0f, 0.2f, 0.0f, vLightFire);
        CreateSprite(BITMAP_LIGHT, o->Position, 2.0f * o->Scale, vLightFire, o);

        vec3_t vLight;
        Vector(1.0f, 1.0f, 1.0f, vLight);
        const float animationFrames = FPS_ANIMATION_FACTOR;
        const float remaining = -static_cast<float>(o->EffectEmissionAge);
        for (float sample = Core::Time::ReferenceSample(remaining);
             Core::Time::Reaches(remaining, animationFrames, sample); sample -= 1.f)
        {
            const float offset = (std::max)(0.f, remaining - sample);
            auto birth = sessionKeeper_.Gameplay()->EmissionTime(animationFrames - offset);
            SessionRandom::PresentationScope presentation(*sessionKeeper_.Random());
            int particleIndex = 0;
            switch (WorldRandom() % 3)
            {
            case 0:
                particleIndex =
                    CreateParticle(BITMAP_FIRE_HIK1, o->Position, o->Angle, vLight, 0, o->Scale);
                break;
            case 1:
                particleIndex = CreateParticle(BITMAP_FIRE_CURSEDLICH, o->Position, o->Angle,
                                               vLight, 4, o->Scale);
                break;
            case 2:
                particleIndex =
                    CreateParticle(BITMAP_FIRE_HIK3, o->Position, o->Angle, vLight, 0, o->Scale);
                break;
            }
            auto &particle = sessionKeeper_.ParticlesStorage()[particleIndex];
            particle.MotionRandomSeed = static_cast<std::uint64_t>(WorldRandom()) + 1;
            particle.AlphaRandomState = particle.MotionRandomSeed;
            particle.ScaleRandomState = particle.MotionRandomSeed ^ 0x9e3779b97f4a7c15ULL;
        }
        o->EffectEmissionAge += animationFrames;
    }
        return true;

    case 80: {
        float fLumi;
        fLumi = (sinf(WorldTime * 0.04f) + 1.0f) * 0.3f + 0.4f;
        vec3_t vLightFire;
        Vector(fLumi * 0.1f, fLumi * 0.1f, fLumi * 0.5f, vLightFire);
        CreateSprite(BITMAP_LIGHT, o->Position, 8.0f * o->Scale, vLightFire, o);
    }
        return true;

    case 82: {
        Vector(1.f, 1.f, 1.f, Light);
        CreateParticleFpsChecked(BITMAP_WATERFALL_5, o->Position, o->Angle, Light, 9, o->Scale);
    }
        return true;

    case 83: {
        Vector(1.f, 1.f, 1.f, Light);
        CreateParticleFpsChecked(BITMAP_WATERFALL_3, o->Position, o->Angle, Light, 14, o->Scale);
    }
        return true;

    case 84: {
        Vector(1.f, 1.f, 1.f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 8.f))
        {
            CreateParticle(BITMAP_WATERFALL_2, o->Position, o->Angle, Light, 4, o->Scale);
        }
    }
        return true;
    case 85:
        sessionKeeper_.Visual()->EmitPeriodicFlames(*o);
        return true;

    case 86: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 6.f))
        {
            Vector(0.05f, 0.02f, 0.01f, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 21, o->Scale, o);
        }
    }
        return true;

    case 129: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 6.f))
        {
            Vector(0.01f, 0.02f, 0.05f, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 21, o->Scale, o);
        }
    }
        return true;

    case 130: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 6.f))
        {
            Vector(0.01f, 0.05f, 0.02f, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 21, o->Scale, o);
        }
    }
        return true;

    case 131: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 22, o->Scale);

            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 21, o->Scale * 2.0f, o);
        }
    }
        return true;

    case 132: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 60, o->Scale, o);

            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 60, o->Scale, o);

            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 21, o->Scale * 2.0f, o);
        }
    }
        return true;

    case 157: {
        PrepareWorldObjectPose(*o);
        if (gMapManager.ContextMap() == WD_73NEW_LOGIN_SCENE ||
            gMapManager.ContextMap() == WD_74NEW_CHARACTER_SCENE)
            return true;

        vec3_t vPos, vRelativePos, vLightFire01, vLightFire02, vLightFlareFire, vLightSmoke, vAngle;
        Vector(0.f, 0.f, 0.f, vPos);

        Vector(4.f, 0.f, 0.0f, vRelativePos);
        Vector(0.0f, 0.0f, 0.0f, vAngle);
        Vector(0.9f, 0.5f, 0.0f, vLightFire01);
        Vector(0.75f, 0.3f, 0.0f, vLightFire02);

        int arriCandleFire[] = {22, 23, 25};
        int arriCandleSmoke[] = {26, 24, 21};

        Vector(0.65f, 0.45f, 0.02f, vLightFlareFire);

        {
            for (int i = 0; i < 3; ++i)
            {
                int iCurBoneIdx = arriCandleFire[i];
                b->TransformPosition(BoneTransform[iCurBoneIdx], vRelativePos, vPos);

                for (int j = 0; j < 5; ++j)
                {
                    CreateParticleFpsChecked(BITMAP_FIRE_HIK3_MONO, vPos, vAngle, vLightFire01, 3,
                                             o->Scale * 0.2f);
                    CreateParticleFpsChecked(BITMAP_FIRE_HIK3_MONO, vPos, vAngle, vLightFire02, 3,
                                             o->Scale * 0.1f);
                }
            }
        }

        Vector(4.f, 0.f, 0.0f, vRelativePos);
        Vector(1.f, 1.f, 1.f, vLightSmoke);

        {
            for (int i = 0; i < 3; i++)
            {
                int iCurBoneIdx = arriCandleSmoke[i];
                b->TransformPosition(BoneTransform[iCurBoneIdx], vRelativePos, vPos);

                for (int j = 0; j < 4; ++j)
                {
                    CreateParticleFpsChecked(BITMAP_SMOKE, vPos, o->Angle, vLightSmoke, 65,
                                             o->Scale * 0.1, o);
                }
            }
        }
    }
        return true;
    case 158: {
        for (int i_ = 0; i_ < 1; ++i_)
        {
            CreateParticleFpsChecked(BITMAP_SMOKE, o->Position, o->Angle, Light, 64, o->Scale, o);
        }
    }
        return true;
    }

    return false;
}

bool GMEmpireGuardian4::AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                             WorldCharacterVisualState &visual)
{
    if (g_EmpireGuardian1.AdvanceMonsterVisual(c, o, b, visual))
    {
        return true;
    }

    vec3_t vPos, vRelative, vLight;

    switch (o->Type)
    {
    case MODEL_GAYION: {
        VectorCopy(o->Position, b->BodyOrigin);
        Vector(0.0f, 0.0f, 0.0f, vRelative);

        float fLumi1 = (sinf(WorldTime * 0.004f) + 1.f) * 0.25f;
        float fLumi2 = (sinf(WorldTime * 0.004f) + 1.f) * 0.2f;

        Vector(0.9f + fLumi1, 0.3f + fLumi1, 0.2f + fLumi1, vLight);
        Vector(0.0f, 5.0f, 0.0f, vRelative);
        b->TransformByObjectBone(vPos, o, 57, vRelative);

        CreateSprite(BITMAP_LIGHT_RED, vPos, 1.5f + fLumi2, vLight, o);
        CreateSprite(BITMAP_LIGHT_RED, vPos, 2.0f + fLumi2, vLight, o);

        Vector(0.0f, -10.0f, 0.0f, vRelative);
        b->TransformByObjectBone(vPos, o, 57, vRelative);
        Vector(1.0f, 0.0f, 0.0f, vLight);
        CreateParticleFpsChecked(BITMAP_SPARK + 1, vPos, o->Angle, vLight, 10, 2.0f);

        if (g_isNotCharacterBuff(o) == true && g_isCharacterBuff(o, eBuff_Berserker) == true)
        {
            sessionKeeper_.Visual()->EmitBerserkerSmoke(*o, *b, 6);
        }
    }
    break;
    case MODEL_JERRY:
    case MODEL_DEATH_ANGEL_3: {
        VectorCopy(o->Position, b->BodyOrigin);
        Vector(0.0f, 0.0f, 0.0f, vRelative);
        float fLumi1 = (sinf(WorldTime * 0.004f) + 1.f) * 0.05f;
        float fLumi2 = (sinf(WorldTime * 0.004f) + 1.f) * 0.2f;

        float fSize = 1.6f;
        Vector(0.6f + fLumi1, 0.2f + fLumi1, 0.1f + fLumi1, vLight);
        Vector(0.0f, 0.0f, 0.0f, vRelative);

        b->TransformByObjectBone(vPos, o, 50, vRelative);
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi2, vLight, o);

        b->TransformByObjectBone(vPos, o, 54, vRelative);
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi2, vLight, o);

        b->TransformByObjectBone(vPos, o, 59, vRelative);
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi2, vLight, o);

        b->TransformByObjectBone(vPos, o, 60, vRelative);
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi2, vLight, o);

        b->TransformByObjectBone(vPos, o, 53, vRelative);
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi2, vLight, o);

        b->TransformByObjectBone(vPos, o, 55, vRelative);
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi2, vLight, o);

        b->TransformByObjectBone(vPos, o, 56, vRelative);
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi2, vLight, o);

        b->TransformByObjectBone(vPos, o, 57, vRelative);
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi2, vLight, o);

        float fLumi3 = (cosf(WorldTime * 0.004f) + 1.f) * 0.1f;
        float fLumi4 = (cosf(WorldTime * 0.004f) + 1.f) * 0.2f;

        fSize = 1.6f;

        Vector(0.9f + fLumi3, 0.1f + fLumi3, 0.6f + fLumi3, vLight);
        Vector(0.0f, 0.0f, 0.0f, vRelative);

        b->TransformByObjectBone(vPos, o, 29, vRelative); // node_body01
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi4, vLight, o);

        b->TransformByObjectBone(vPos, o, 30, vRelative); // node_body02
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi4, vLight, o);

        b->TransformByObjectBone(vPos, o, 31, vRelative); // node_body03
        CreateSprite(BITMAP_LIGHT, vPos, (fSize * 0.6f) + fLumi4, vLight, o);

        b->TransformByObjectBone(vPos, o, 67, vRelative); // node_body04
        CreateSprite(BITMAP_LIGHT, vPos, (fSize * 0.6f) + fLumi4, vLight, o);

        b->TransformByObjectBone(vPos, o, 66, vRelative); // node_body05
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi4, vLight, o);

        b->TransformByObjectBone(vPos, o, 65, vRelative); // node_body06
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi4, vLight, o);

        b->TransformByObjectBone(vPos, o, 9, vRelative); // node_body07
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi4, vLight, o);

        b->TransformByObjectBone(vPos, o, 28, vRelative); // node_body08
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi4, vLight, o);

        b->TransformByObjectBone(vPos, o, 64, vRelative); // node_body09
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi4, vLight, o);

        fLumi3 = (cosf(WorldTime * 0.004f) + 1.f) * 0.25f;
        fLumi4 = (cosf(WorldTime * 0.004f) + 1.f) * 0.2f;

        fSize = 1.3f;

        Vector(0.9f + fLumi3, 0.1f + fLumi3, 0.4f + fLumi3, vLight);
        Vector(0.0f, 0.0f, 0.0f, vRelative);

        b->TransformByObjectBone(vPos, o, 7, vRelative); // Bip01 Head
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi4, vLight, o);

        b->TransformByObjectBone(vPos, o, 11, vRelative); // Bip01 L UpperArm
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi4, vLight, o);

        b->TransformByObjectBone(vPos, o, 33, vRelative); // Bip01 R UpperArm
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi4, vLight, o);

        fSize = 1.3f;

        Vector(0.9f + fLumi3, 0.1f + fLumi3, 0.4f + fLumi3, vLight);
        Vector(0.0f, 0.0f, 0.0f, vRelative);

        b->TransformByObjectBone(vPos, o, 7, vRelative); // Bip01 Head
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi4, vLight, o);

        b->TransformByObjectBone(vPos, o, 11, vRelative); // Bip01 L UpperArm
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi4, vLight, o);

        b->TransformByObjectBone(vPos, o, 33, vRelative); // Bip01 R UpperArm
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi4, vLight, o);

        fSize = 2.3f;

        Vector(0.9f + fLumi3, 0.1f + fLumi3, 0.4f + fLumi3, vLight);
        Vector(0.0f, 0.0f, 0.0f, vRelative);

        b->TransformByObjectBone(vPos, o, 5, vRelative); // Bip01 Spine2
        CreateSprite(BITMAP_LIGHT, vPos, fSize + fLumi4, vLight, o);

        if (g_isNotCharacterBuff(o) == true && g_isCharacterBuff(o, eBuff_Berserker) == true)
        {
            sessionKeeper_.Visual()->EmitBerserkerSmoke(*o, *b, 5);
        }
    }
        return true;
    }
    return false;
}

bool GMEmpireGuardian4::AttackEffectMonster(CHARACTER *c, OBJECT *o, BMD *b)
{
    if (gMapManager.IsEmpireGuardian4() == false)
        return false;

    return false;
}

bool GMEmpireGuardian4::PlayMonsterSound(OBJECT *o)
{
    if (gMapManager.IsEmpireGuardian4() == false)
        return false;

    if (true == g_EmpireGuardian1.PlayMonsterSound(o))
    {
        return true;
    }

    float fDis_x, fDis_y;
    fDis_x = o->Position[0] - Hero->Object.Position[0];
    fDis_y = o->Position[1] - Hero->Object.Position[1];
    float fDistance = sqrtf(fDis_x * fDis_x + fDis_y * fDis_y);

    if (fDistance > 500.0f)
        return true;

    switch (o->Type)
    {
    case MODEL_GAYION: {
        switch (o->CurrentAction)
        {
        case MONSTER01_WALK: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_BOSS_GAION_MONSTER_MOVE);
        }
            return true;
        case MONSTER01_DIE: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_BOSS_GAION_MONSTER_DEATH);
        }
            return true;
        case MONSTER01_APEAR: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_BOSS_GAION_MONSTER_RAGE);
        }
            return true;
        }
    }
        return true;
    case MODEL_JERRY: {
        switch (o->CurrentAction)
        {
        case MONSTER01_WALK: {
            if (rand_fps_check(2))
            {
                PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_MOVE01);
            }
            else
            {
                PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_MOVE02);
            }
        }
            return true;
        case MONSTER01_ATTACK1: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_ATTACK01);
        }
            return true;
        case MONSTER01_ATTACK2: {
            PlayBuffer(SOUND_BLOODATTACK);
        }
            return true;
        case MONSTER01_ATTACK3: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_ATTACK03);
            PlayBuffer(SOUND_SKILL_BLOWOFDESTRUCTION);
        }
            return true;
        case MONSTER01_DIE: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_DEATH);
        }
            return true;
        case MONSTER01_APEAR: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_RAGE);
        }
            return true;
        }
    }
        return true;
    }

    return false;
}

void GMEmpireGuardian4::PlayObjectSound(OBJECT *o)
{
    g_EmpireGuardian1.PlayObjectSound(o);
}

void GMEmpireGuardian4::PlayBGM()
{
    if (gMapManager.IsEmpireGuardian4())
    {
        PlayMp3(MUSIC_EMPIREGUARDIAN4);
    }
    else
    {
        StopMp3(MUSIC_EMPIREGUARDIAN4);
    }
}

void GMEmpireGuardian4::AdvanceGatePlacement(OBJECT *o)
{
    if (!gMapManager.IsEmpireGuardian4())
        return;
    switch (o->Type)
    {
    case MODEL_STAR_GATE: {
        int tileX = int(o->Position[0] / 100);
        int tileY = int(o->Position[1] / 100);

        if ((49 <= tileX && tileX <= 51) && (68 <= tileY && tileY <= 70))
        {
            o->Scale = 1.0f;
            o->Position[0] = 5080;
            o->Position[1] = 6920;
        }
        else if ((51 <= tileX && tileX <= 53) && (190 <= tileY && tileY <= 192))
        {
            o->Scale = 1.0f;
            o->Position[0] = 5270;
            o->Position[1] = 19120;
        }
        else if ((196 <= tileX && tileX <= 198) && (131 <= tileY && tileY <= 133))
        {
            o->Scale = 1.0f;
            o->Position[0] = 19750;
            o->Position[1] = 13220;
        }

        break;
    }
    case MODEL_RUSH_GATE: {
        if (o->CurrentAction == MONSTER01_DIE)
            return;
        int tileX = int(o->Position[0] / 100);
        int tileY = int(o->Position[1] / 100);

        if ((80 <= tileX && tileX <= 82) && (68 <= tileY && tileY <= 70))
        {
            o->Scale = 0.8f;
            o->Position[0] = 8115;
            o->Position[1] = 6880;
        }
        else if ((31 <= tileX && tileX <= 33) && (89 <= tileY && tileY <= 91))
        {
            o->Scale = 0.9f;
            o->Position[0] = 3250;
            o->Position[1] = 9000;
        }
        else if ((33 <= tileX && tileX <= 35) && (175 <= tileY && tileY <= 177))
        {
            o->Scale = 0.8f;
            o->Position[0] = 3470;
            o->Position[1] = 17600;
        }
        else if ((68 <= tileX && tileX <= 70) && (165 <= tileY && tileY <= 167))
        {
            o->Scale = 0.9f;
            o->Position[0] = 6915;
            o->Position[1] = 16650;
        }
        else if ((155 <= tileX && tileX <= 157) && (131 <= tileY && tileY <= 133))
        {
            o->Scale = 0.9f;
            o->Position[0] = 15710;
            o->Position[1] = 13250;
        }
        else if ((223 <= tileX && tileX <= 225) && (158 <= tileY && tileY <= 160))
        {
            o->Scale = 0.8f;
            o->Position[0] = 22500;
            o->Position[1] = 16000;
        }
        else if ((213 <= tileX && tileX <= 215) && (23 <= tileY && tileY <= 25))
        {
            o->Scale = 0.9f;
            o->Position[0] = 21480;
            o->Position[1] = 2430;
        }

        break;
    }
    }
}

void GMEmpireGuardian4::AdvanceEnvironment()
{
    g_EmpireGuardian1.AdvanceWeather();
}

void GMEmpireGuardian4::UpdateMusic()
{
    PlayBGM();
}

bool GMEmpireGuardian4::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_EMPIREGUARDIAN4) == 0;
}

bool CGMGmArea::AdvanceObjectVisual(OBJECT *object, BMD *model, float)
{
    return TheMapProcess().Kanturu1st().AdvanceKanturu1stObjectVisual(object, model);
}

bool GMIcarus::AdvanceObjectVisual(OBJECT *o, BMD *b, float Luminosity)
{
    vec3_t p{}, Position{}, Light{};
    int Bitmap;
    float Scale, Rotation;
    switch (o->Type)
    {
    case 0:
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.1f, 0.1f, 0.1f, Light);
            for (int i = 0; i < 20; ++i)
            {
                CreateParticleFpsChecked(BITMAP_CLOUD, o->Position, o->Angle, Light, 0, o->Scale,
                                         o);
            }
        }
        o->HiddenMesh = -2;
        break;

    case 1:
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.1f, 0.1f, 0.1f, Light);
            for (int i = 0; i < 20; ++i)
            {
                CreateParticleFpsChecked(BITMAP_CLOUD, o->Position, o->Angle, Light, 1, o->Scale,
                                         o);
            }
        }
        o->HiddenMesh = -2;
        break;

    case 2:
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.1f, 0.1f, 0.1f, Light);
            for (int i = 0; i < 20; ++i)
            {
                CreateParticleFpsChecked(BITMAP_CLOUD, o->Position, o->Angle, Light, 2, o->Scale,
                                         o);
            }
        }
        o->HiddenMesh = -2;
        break;

    case 3:
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.1f, 0.1f, 0.1f, Light);
            for (int i = 0; i < 10; ++i)
            {
                CreateParticleFpsChecked(BITMAP_CLOUD, o->Position, o->Angle, Light, 3, o->Scale,
                                         o);
            }
        }
        o->HiddenMesh = -2;
        break;

    case 4:
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.1f, 0.1f, 0.1f, Light);
            for (int i = 0; i < 10; ++i)
            {
                CreateParticleFpsChecked(BITMAP_CLOUD, o->Position, o->Angle, Light, 4, o->Scale,
                                         o);
            }
        }
        o->HiddenMesh = -2;
        break;

    case 5:
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.1f, 0.1f, 0.1f, Light);
            for (int i = 0; i < 10; ++i)
            {
                CreateParticleFpsChecked(BITMAP_CLOUD, o->Position, o->Angle, Light, 5, o->Scale,
                                         o);
            }
        }
        o->HiddenMesh = -2;
        break;

    case 10: {
        PrepareWorldObjectPose(*o);
        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(BoneTransform[3], p, Position);

        Vector(1.f, 1.f, 1.f, Light);
        CreateParticleFpsChecked(BITMAP_LIGHT, Position, o->Angle, Light, 0, 1.f);
    }
    case 6:
    case 7:
    case 8:
    case 9:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
        //            o->BlendMeshLight = sinf(WorldTime*0.002f)*1.f;
        //            o->BlendMesh = -2;
        //            o->HiddenMesh= -99;
        break;
    }
    return true;
}

std::optional<bool> GMIcarus::ObjectVisibility(const OBJECT &object, bool blockVisible)
{
    return blockVisible && TestFrustrum2D(object.Position[0] * 0.01f, object.Position[1] * 0.01f,
                                          object.CollisionRange - 10.f);
}

void GMIcarus::PlayAmbientSounds()
{
    PlayBuffer(SOUND_HEAVEN01, nullptr, true);
    // Preserve the existing tick RNG cadence; these optional sounds are disabled.
    if (!rand_fps_check(100))
        (void)rand_fps_check(10);
}

bool GMIcarus::AllowsAmbientSound(ESound sound) const
{
    return (sound == SOUND_HEAVEN01);
}

void GMIcarus::UpdateMusic()
{
    PlayMp3(MUSIC_ICARUS);
}

bool GMIcarus::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_ICARUS) == 0;
}

bool GMIcarus::CreateWeather(PARTICLE *o, int index)
{
    int Rainly = RainCurrent * MAX_LEAVES / 100;
    if (index < Rainly)
    {
        o->Type = BITMAP_RAIN;
        Vector(Hero->Object.Position[0] + Random.RangeFloat(-800, 799),
               Hero->Object.Position[1] + Random.RangeFloat(-500, 899),
               Hero->Object.Position[2] + Random.RangeFloat(200, 399), o->Position);
        Vector(-30.f, 0.f, 0.f, o->Angle);
        vec3_t Velocity;
        Vector(0.f, 0.f, -Random.RangeFloat(20, 43), Velocity);
        float Matrix[3][4];
        AngleMatrix(o->Angle, Matrix);
        VectorRotate(Velocity, Matrix, o->Velocity);
    }
    return index < Rainly;
}

bool GMIcarus::MoveWeather(PARTICLE *particle)
{
    return TheMapProcess().Lorencia().MoveAirWeather(particle, false);
}

int GMIcarus::PrepareWeather()
{
    RainTarget = MAX_LEAVES / 2;
    return 80;
}

bool GMIcarus::ConfigureAmbientBoid(OBJECT *o, int index)
{
    if (index < 3)
    {
        if (!OpenMonsterModel(MONSTER_MODEL_DRAGON))
        {
            return false;
        }
        o->Live = true;
        o->Type = MODEL_DRAGON_;
        o->Scale = (float)(WorldRandom() % 3 + 6) * 0.05f;
        o->Alpha = 1.f;
        o->AlphaTarget = o->Alpha;
        o->Velocity = (float)(WorldRandom() % 10 + 10) * 0.02f;
        o->Gravity = (float)(WorldRandom() % 10 + 10) * 0.05f;
        o->LightEnable = true;
        o->AlphaEnable = false;
        o->SubType = 0;
        o->HiddenMesh = -1;
        o->BlendMesh = -1;
        // o->LifeTime    = 128+WorldRandom()%128;
        // o->Timer       = (float)(WorldRandom()%10)*0.1f;
        o->CurrentAction = MONSTER01_DIE + 1;
        SetAction(o, o->CurrentAction);
        Vector(0.f, 0.f, (float)(WorldRandom() % 360), o->Angle);
        Vector(Hero->Object.Position[0] + (float)(WorldRandom() % 4000 - 2000),
               Hero->Object.Position[1] + (float)(WorldRandom() % 4000 - 2000),
               Hero->Object.Position[2] - 600.f, o->Position);
    }
    else
    {
        o->Type = MODEL_SPEARSKILL;
        o->Velocity = 2.2f;
        o->LightEnable = false;
        o->LifeTime = 240 * 40;
        constexpr BYTE GroundBoid = 2;
        o->AI = GroundBoid;
        Vector(1.f, 1.f, 1.f, o->Light);

        o->AlphaEnable = true;
        o->Scale = 0.8f;
        o->ShadowScale = 10.f;
        o->HiddenMesh = -1;
        o->BlendMesh = -1;
        o->Timer = (float)(WorldRandom() % 314) * 0.01f;
        Vector(Hero->Object.Position[0] + (float)(WorldRandom() % 1024 - 512),
               Hero->Object.Position[1] + (float)(WorldRandom() % 1024 - 512),
               Hero->Object.Position[2], o->Position);

        o->Position[2] = Hero->Object.Position[2];
        CreateJoint(MODEL_SPEARSKILL, o->Position, o->Position, o->Angle, 1, o, 25.0f);
        Vector(0.f, 0.f, 0.f, o->Angle);
        o->Angle[2] = (float)(WorldRandom() % 360);
    }
    return true;
}

bool GMIcarus::CanCreateAmbientBoid(int slot, int index)
{
    return true;
}

bool GMIcarus::PrepareAmbientBoidSlot(int index, bool &allowCreate)
{
    return index < 13;
}

void GMIcarus::EmitSkyLightning(const vec3_t origin)
{
    float Matrix1[3][4];
    float Matrix2[3][4];
    vec3_t pos, position, position2, angle;
    vec3_t vTempPos;

    Vector(0.f, 0.f, -45.f, angle);
    AngleMatrix(angle, Matrix1);

    switch (WorldRandom() % 4)
    {
    case 0:
        Vector(-400.f, -1000.f, 0.f, position);
        VectorCopy(position, vTempPos);
        VectorRotate(vTempPos, Matrix1, position);
        VectorAdd(origin, position, pos);
        Vector(0.f, 0.f, 240.f, angle);
        AngleMatrix(angle, Matrix2);
        Vector(-200.f, -1000.f, 0.f, position);
        break;
    case 1:
        Vector(-300.f, -400.f, 0.f, position);
        VectorCopy(position, vTempPos);
        VectorRotate(vTempPos, Matrix1, position);
        VectorSubtract(origin, position, pos);
        Vector(0.f, 0.f, 210.f, angle);
        AngleMatrix(angle, Matrix2);
        Vector(-500.f, -1000.f, 0.f, position);
        break;
    case 2:
        Vector(-200.f, -400.f, 0.f, position);
        VectorCopy(position, vTempPos);
        VectorRotate(vTempPos, Matrix1, position);
        VectorAdd(origin, position, pos);
        Vector(0.f, 0.f, 235.f, angle);
        AngleMatrix(angle, Matrix2);
        Vector(-1000.f, -1500.f, 0.f, position);
        break;
    case 3:
        Vector(-200.f, 400.f, 0.f, position);
        VectorCopy(position, vTempPos);
        VectorRotate(vTempPos, Matrix1, position);
        VectorAdd(origin, position, pos);
        Vector(0.f, 0.f, 200.f, angle);
        AngleMatrix(angle, Matrix2);
        Vector(-600.f, -1200.f, 0.f, position);
        break;
    }
    VectorCopy(position, vTempPos);
    VectorRotate(vTempPos, Matrix2, position);
    VectorSubtract(pos, position, position2);
    VectorAdd(pos, position, position);
    position[2] -= 300.f;
    position2[2] -= 300.f;
    angle[2] = 0;
    CreateJoint(BITMAP_JOINT_THUNDER + 1, position, position2, angle, 0, NULL,
                40.f + WorldRandom() % 10);
    CreateJoint(BITMAP_JOINT_THUNDER + 1, position, position2, angle, 0, NULL,
                40.f + WorldRandom() % 10);
}

void GMIcarus::PrepareThunder(int previousVisible)
{
    thunderBirthTimes_.clear();
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 50.f))
    {
        const float fraction = birth.FrameFraction();
        vec3_t origin, position, angle{}, light;
        Hero->Object.MotionTrace.Sample(WorldTime, fraction, Hero->Object.Position, origin);
        VectorCopy(origin, position);
        position[0] += WorldRandom() % 300 - 150.f;
        position[2] += WorldRandom() % 300 - 150.f;
        const float luminosity = (WorldRandom() % 4 + 4) * 0.05f;
        Vector(luminosity * 0.3f, luminosity * 0.3f, luminosity * 0.081f, light);
        AddTerrainLight(position[0], position[1], light, 2, PrimaryTerrainLight);
        CreateEffect(MODEL_CLOUD, origin, angle, light);
        if (previousVisible > 0 && WorldRandom() % previousVisible != 0)
            thunderBirthTimes_.push_back(birth.RemainingFrames());
        if (Random.FpsCheck(5, 1.f))
            EmitSkyLightning(origin);
    }
}

void GMIcarus::PrepareObjectEffects(int &, int previousVisible)
{
    PrepareThunder(previousVisible);

    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.f))
    {
        vec3_t Position;
        const float fraction = birth.FrameFraction();
        Hero->Object.MotionTrace.Sample(WorldTime, fraction, Hero->Object.Position, Position);
        Position[0] += WorldRandom() % 5000 - 2500;
        Position[1] += WorldRandom() % 5000 - 2500;
        Position[2] -= 1000.f;
        vec3_t Light = {1.f, 1.f, 1.f};
        vec3_t Angle = {0.f, 0.f, 0.f};
        CreateEffect(BITMAP_LIGHT, Position, Angle, Light);
    }
    return;
}

void GMIcarus::MoveObjectEffects(OBJECT *o, int &, int &visObject)
{
    ++visObject;
    if (o->Type < 0 || o->Type > 5)
        return;
    for (float remaining : thunderBirthTimes_)
    {
        if (!Random.FpsCheck(10, 1.f))
            continue;
        auto birth = sessionKeeper_.Gameplay()->EmissionTime(remaining);
        vec3_t light;
        Vector(WorldRandom() % 10 / 50.f, WorldRandom() % 10 / 50.f, WorldRandom() % 10 / 50.f,
               light);
        CreateSprite(BITMAP_CLOUD + 1, o->Position, 0.5f, light, &Hero->Object);
        CreateJoint(BITMAP_JOINT_THUNDER, o->Position, o->Position, o->Angle, 6, o,
                    WorldRandom() % 20 + 10.f);
        CreateJoint(BITMAP_JOINT_THUNDER, o->Position, o->Position, o->Angle, 6, o,
                    WorldRandom() % 20 + 10.f);
    }
}

void GMIcarus::PrepareObjectLight(const ObjectDrawInput &object, BMD &model)
{
    if (object.type == MODEL_DRAGON_)
        Vector(0.02f, 0.05f, 0.15f, model.BodyLight);
}

#ifdef ASG_ADD_MAP_KARUTAN

bool CGMKarutan1::AdvanceObjectVisual(OBJECT *o, BMD *b, float)
{
    if (!IsKarutanMap())
        return false;

    switch (o->Type)
    {
    case 66: {
        PrepareWorldObjectPose(*o);
        vec3_t vPos, vRelativePos, vLight0;
        Vector(0.f, 0.f, 0.f, vPos);
        float flumi = absf(sinf(WorldTime * 0.0004)) * 0.4f; //+0.1f;
        Vector(flumi * 1.9f, flumi * 1.1f, flumi * 1.1f, vLight0);

        Vector(10.f, 0.f, -6.f, vRelativePos);
        b->TransformPosition(BoneTransform[13], vRelativePos, vPos);
        CreateSprite(BITMAP_SHINY + 5, vPos, 1.2f, vLight0, o);
        Vector(10.f, 0.f, 6.f, vRelativePos);
        b->TransformPosition(BoneTransform[14], vRelativePos, vPos);
        CreateSprite(BITMAP_SHINY + 5, vPos, 1.2f, vLight0, o);
    }
        return true;
    case 72: {
        PrepareWorldObjectPose(*o);
        vec3_t vPos, vRelativePos, vLight0, vLight1;
        Vector(0.f, 0.f, 0.f, vPos);
        Vector(20.f, 0.f, 0.f, vRelativePos);
        float flumi = absf(sinf(WorldTime * 0.0004)) * 0.4f; //+0.1f;
        Vector(flumi * 1.3f, flumi * 1.3f, flumi * 1.9f, vLight0);
        Vector(0.15f, 0.15f, 0.15f, vLight1);

        b->TransformPosition(BoneTransform[11], vRelativePos, vPos);
        CreateSprite(BITMAP_LIGHT, vPos, 2.0f, vLight0, o);
        CreateSprite(BITMAP_SPARK + 1, vPos, 1.5f, vLight1, o);
        b->TransformPosition(BoneTransform[7], vRelativePos, vPos);
        CreateSprite(BITMAP_SPARK + 1, vPos, 4.0f, vLight0, o);
        CreateSprite(BITMAP_SPARK + 1, vPos, 1.5f, vLight1, o);
    }
        return true;
    case 113: {
        vec3_t vLightFire;
        Vector(1.0f, 0.2f, 0.0f, vLightFire);
        CreateSprite(BITMAP_LIGHT, o->Position, 2.0f * o->Scale, vLightFire, o);

        vec3_t vLight;
        Vector(1.0f, 1.0f, 1.0f, vLight);

        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            switch (WorldRandom() % 3)
            {
            case 0:
                CreateParticle(BITMAP_FIRE_HIK1, o->Position, o->Angle, vLight, 0, o->Scale);
                break;
            case 1:
                CreateParticle(BITMAP_FIRE_CURSEDLICH, o->Position, o->Angle, vLight, 4, o->Scale);
                break;
            case 2:
                CreateParticle(BITMAP_FIRE_HIK3, o->Position, o->Angle, vLight, 0, o->Scale);
                break;
            }
        }
    }
        return true;
    case 114:
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            vec3_t vLight;
            Vector(0.2f, 0.2f, 0.2f, vLight);
            CreateParticle(BITMAP_WATERFALL_3, o->Position, o->Angle, vLight, 16, o->Scale);
        }
        return true;
    case 115:
        if (o->HiddenMesh != -2)
        {
            vec3_t vLight;
            Vector(0.04f, 0.04f, 0.04f, vLight);
            for (int i = 0; i < 20; ++i)
                CreateParticleFpsChecked(BITMAP_CLOUD, o->Position, o->Angle, vLight, 0, o->Scale,
                                         o);
        }
        return true;
    case 116:
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 8.f))
        {
            vec3_t vLight;
            Vector(0.5f, 0.9f, 0.5f, vLight);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, vLight, 69, o->Scale);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, o->Light, 13, o->Scale * 2.0f, o);
        }
        return true;
    case 118:
        if (o->HiddenMesh != -2)
        {
            vec3_t vLight;
            Vector(0.27f, 0.2f, 0.1f, vLight);
            for (int i = 0; i < 4; ++i)
                CreateParticleFpsChecked(BITMAP_CLOUD, o->Position, o->Angle, vLight, 0, o->Scale,
                                         o);
        }
        return true;
    }

    return false;
}

void CGMKarutan1::PlayObjectSound(OBJECT *o)
{
    switch (o->Type)
    {
    case 58:
    case 66:
        PlayBuffer(SOUND_KARUTAN_INSECT_ENV, o, false);
        break;
    }
}

#ifdef ASG_ADD_KARUTAN_MONSTERS

void CGMKarutan1::EmitCondraDeath(OBJECT &object, BMD &model, float fraction)
{
    constexpr std::array<std::pair<int, int>, 10> condraParts{{{12, MODEL_CONDRA_ARM_L},
                                                               {13, MODEL_CONDRA_ARM_L2},
                                                               {33, MODEL_CONDRA_SHOULDER},
                                                               {33, MODEL_CONDRA_ARM_R},
                                                               {34, MODEL_CONDRA_ARM_R2},
                                                               {11, MODEL_CONDRA_CONE_L},
                                                               {32, MODEL_CONDRA_CONE_R},
                                                               {0, MODEL_CONDRA_PELVIS},
                                                               {4, MODEL_CONDRA_STOMACH},
                                                               {6, MODEL_CONDRA_NECK}}};
    constexpr std::array<std::pair<int, int>, 16> nacondraParts{{{12, MODEL_NARCONDRA_ARM_L},
                                                                 {12, MODEL_NARCONDRA_ARM_L2},
                                                                 {11, MODEL_NARCONDRA_SHOULDER_L},
                                                                 {34, MODEL_NARCONDRA_SHOULDER_R},
                                                                 {35, MODEL_NARCONDRA_ARM_R},
                                                                 {35, MODEL_NARCONDRA_ARM_R2},
                                                                 {36, MODEL_NARCONDRA_ARM_R3},
                                                                 {82, MODEL_NARCONDRA_CONE_1},
                                                                 {80, MODEL_NARCONDRA_CONE_2},
                                                                 {78, MODEL_NARCONDRA_CONE_3},
                                                                 {76, MODEL_NARCONDRA_CONE_4},
                                                                 {74, MODEL_NARCONDRA_CONE_5},
                                                                 {72, MODEL_NARCONDRA_CONE_6},
                                                                 {0, MODEL_NARCONDRA_PELVIS},
                                                                 {4, MODEL_NARCONDRA_STOMACH},
                                                                 {6, MODEL_NARCONDRA_NECK}}};
    const bool condra = object.Type == MODEL_CONDRA;
    const auto parts = condra ? std::span<const std::pair<int, int>>(condraParts)
                              : std::span<const std::pair<int, int>>(nacondraParts);
    AnimationPoseSample pose(&object, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    std::array<vec34_t, MAX_BONES> bones;
    pose.EvaluateAtTime(model, object, WorldTime, fraction, bones.data());
    vec3_t sampledOrigin;
    object.MotionTrace.Sample(WorldTime, fraction, object.Position, sampledOrigin);
    const auto bonePosition = [&](int bone, vec3_t result) {
        for (int axis = 0; axis < 3; ++axis)
            result[axis] = bones[bone][axis][3] * object.Scale + sampledOrigin[axis];
    };
    vec3_t position, angle, light{1.f, 1.f, 1.f};
    VectorCopy(object.Angle, angle);
    angle[2] = object.MotionTrace.SampleYaw(WorldTime, fraction, angle[2]);
    bonePosition(6, position);
    const int stoneCount = condra ? 6 : 4;
    const int stoneType = condra ? MODEL_CONDRA_STONE : MODEL_NARCONDRA_STONE;
    for (int child = 0; child < stoneCount; ++child)
        CreateEffect(stoneType + WorldRandom() % stoneCount, position, angle, light);
    for (const auto &[bone, type] : parts)
    {
        bonePosition(bone, position);
        CreateEffect(type, position, angle, light, 0, &object, 0, 0);
    }
    vec3_t origin;
    bonePosition(5, origin);
    Vector(0.5f, 0.5f, 0.5f, light);
    for (int child = 0; child < 20; ++child)
    {
        Vector(origin[0] + WorldRandom() % 160 - 80, origin[1] + WorldRandom() % 160 - 80,
               origin[2] + WorldRandom() % 150 - 50, position);
        CreateParticle(BITMAP_SMOKE, position, angle, light, 48, 1.f);
    }
}

void CGMKarutan1::EmitCondraEvents(OBJECT &object, BMD &model, WorldCharacterVisualState &visual)
{
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    constexpr std::array<std::pair<int, float>, 3> markers{
        {{MONSTER01_ATTACK1, 12.5f}, {MONSTER01_ATTACK2, 12.5f}, {MONSTER01_DIE, 0.f}}};
    object.MotionTrace.VisitAnimationEvents(
        WorldTime, markers, [&](std::size_t event, float fraction) {
            auto birth =
                sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR * (1.f - fraction));
            if (event == 2)
            {
                if (visual.emissionLifeTime != 100.f)
                    return;
                visual.emissionLifeTime = visual.movement.lifeTime = 90.f;
                visual.movement.materialFields |= CharacterMovementVisual::Shadow;
                visual.movement.renderShadow = false;
                EmitCondraDeath(object, model, fraction);
                return;
            }
            if (object.Type != MODEL_CONDRA)
                return;
            AnimationPoseSample pose(&object, model.BoneHead, model.BodyHeight, false,
                                     model.PoseAssetIdentity());
            vec3_t local{}, position, angle, light{1.f, 0.6f, 0.2f};
            pose.SampleBonePosition(model, object, 14, local, WorldTime, fraction, position);
            VectorCopy(object.Angle, angle);
            angle[2] = object.MotionTrace.SampleYaw(WorldTime, fraction, angle[2]);
            CreateEffect(BITMAP_CRATER, position, angle, visual.movement.light, 2);
            CreateParticle(BITMAP_FIRE, position, angle, light, 2, 2.f);
            CreateParticle(BITMAP_ADV_SMOKE + 1, position, angle, visual.movement.light);
            CreateParticle(BITMAP_ADV_SMOKE + 1, position, angle, visual.movement.light, 1, 1.8f);
            for (int child = 0; child < 10; ++child)
                CreateEffect(MODEL_STONE2, position, angle, visual.movement.light);
        });
}

bool CGMKarutan1::MoveMonsterVisual(CHARACTER *, OBJECT *o, BMD *b,
                                    WorldCharacterVisualState &visual)
{
    ObjectDrawInput presentation(o);
    visual.movement.Apply(presentation);
    if (!IsKarutanMap())
        return false;

    if (o->Type == MODEL_CRYPOS && FPS_ANIMATION_FACTOR > 0.f)
    {
        constexpr std::array<std::pair<int, float>, 1> markers{{{MONSTER01_ATTACK2, 3.5f}}};
        o->MotionTrace.VisitAnimationEvents(WorldTime, markers, [&](std::size_t, float fraction) {
            CHARACTER *target = visual.target.Resolve();
            if (!target)
                return;
            auto birth =
                sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR * (1.f - fraction));
            vec3_t position, angle, light{0.4f, 0.9f, 0.6f};
            target->Object.MotionTrace.Sample(WorldTime, fraction, target->Object.Position,
                                              position);
            VectorCopy(target->Object.Angle, angle);
            angle[2] = target->Object.MotionTrace.SampleYaw(WorldTime, fraction, angle[2]);
            for (int child = 0; child < 5; ++child)
                CreateParticle(BITMAP_SMOKE, position, angle, light, 1);
            Vector(0.4f, 1.f, 0.6f, light);
            for (int child = 0; child < 2; ++child)
                CreateParticle(BITMAP_TWINTAIL_WATER, position, angle, light, 0);
        });
    }
    if (o->Type == MODEL_CONDRA || o->Type == MODEL_NACONDRA)
        EmitCondraEvents(*o, *b, visual);

    vec3_t p, Position;
    vec3_t Light;
    float Luminosity = (float)(WorldRandom() % 8 + 2) * 0.1f;
    ;

    switch (o->Type)
    {
    case MODEL_VENOMOUS_CHAIN_SCORPION:
        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(presentation.bones[15], p, Position, true);
        Vector(Luminosity * 1.f, Luminosity * 0.4f, Luminosity * 0.2f, Light);
        CreateSprite(BITMAP_LIGHT, Position, 1.5f, Light, o);
        return true;
    case MODEL_BONE_SCORPION:
        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(presentation.bones[8], p, Position, true);
        Vector(Luminosity * 1.f, Luminosity * 0.4f, Luminosity * 0.2f, Light);
        CreateSprite(BITMAP_LIGHT, Position, 2.0f, Light, o);
        return true;
    case MODEL_GOLLOCK:
        if (visual.action == MONSTER01_WALK)
        {
            vec3_t Position;
            Vector(o->Position[0] + WorldRandom() % 200 - 100,
                   o->Position[1] + WorldRandom() % 200 - 100, o->Position[2], Position);
            CreateParticleFpsChecked(BITMAP_SMOKE + 1, Position, o->Angle, visual.movement.light);
        }
        return true;
    case MODEL_CONDRA:
        return true;
    case MODEL_NACONDRA:
        if (visual.action != MONSTER01_DIE)
        {
            Vector(0.f, 0.f, 0.f, p);
            Vector(Luminosity * 1.0f, Luminosity * 0.4f, Luminosity * 1.0f, Light);
            b->TransformPosition(presentation.bones[9], p, Position, true);
            CreateSprite(BITMAP_SHINY + 5, Position, 0.7f, Light, o);
            b->TransformPosition(presentation.bones[33], p, Position, true);
            CreateSprite(BITMAP_SHINY + 5, Position, 1.0f, Light, o);
            b->TransformPosition(presentation.bones[34], p, Position, true);
            CreateSprite(BITMAP_SHINY + 5, Position, 1.0f, Light, o);
            b->TransformPosition(presentation.bones[58], p, Position, true);
            CreateSprite(BITMAP_SHINY + 5, Position, 1.0f, Light, o);
            b->TransformPosition(presentation.bones[59], p, Position, true);
            CreateSprite(BITMAP_SHINY + 5, Position, 1.0f, Light, o);
            b->TransformPosition(presentation.bones[88], p, Position, true);
            CreateSprite(BITMAP_SHINY + 5, Position, 0.7f, Light, o);
            b->TransformPosition(presentation.bones[89], p, Position, true);
            CreateSprite(BITMAP_SHINY + 5, Position, 0.7f, Light, o);
        }
        return true;
    }

    return false;
}

void CGMKarutan1::MoveBlurEffect(CHARACTER *pCharacter, OBJECT *pObject, BMD *pModel)
{
    switch (pObject->Type)
    {
    case MODEL_ORCUS: {
        float Start_Frame = 0.f;
        float End_Frame = 9.0f;
        if ((pObject->AnimationFrame >= Start_Frame && pObject->AnimationFrame <= End_Frame &&
             pObject->CurrentAction == MONSTER01_ATTACK1) ||
            (pObject->AnimationFrame >= Start_Frame && pObject->AnimationFrame <= End_Frame &&
             pObject->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t Light;
            Vector(1.0f, 1.2f, 1.2f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = pModel->Actions[pObject->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = pObject->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; ++i)
            {
                pModel->AnimationAtFrame(BoneTransform, fAnimationFrame,
                                         pObject->PriorAnimationFrame, pObject->PriorAction,
                                         pObject->Angle, pObject->HeadAngle);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);

                pModel->TransformPosition(BoneTransform[56], StartRelative, StartPos, false);
                pModel->TransformPosition(BoneTransform[57], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, Light, 0);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_CRYPOS: {
        float Start_Frame = 0.f; //3.5f;
        float End_Frame = 6.0f;  //6.7f;
        if ((pObject->AnimationFrame >= Start_Frame && pObject->AnimationFrame <= End_Frame &&
             pObject->CurrentAction == MONSTER01_ATTACK1))
        {
            vec3_t Light;
            Vector(1.f, 0.05f, 0.0f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = pModel->Actions[pObject->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = pObject->AnimationFrame - fActionSpeed;
            int i;
            for (i = 0; i < 10; ++i)
            {
                pModel->AnimationAtFrame(BoneTransform, fAnimationFrame,
                                         pObject->PriorAnimationFrame, pObject->PriorAction,
                                         pObject->Angle, pObject->HeadAngle);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);

                Vector(2.f, 2.f, 2.f, pModel->BodyLight);

                pModel->TransformPosition(BoneTransform[156], StartRelative, StartPos, false);
                pModel->TransformPosition(BoneTransform[153], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, Light, 1, false, 0);
                CreateBlur(pCharacter, StartPos, EndPos, Light, 1, false, 2);

                pModel->TransformPosition(BoneTransform[149], StartRelative, StartPos, false);
                pModel->TransformPosition(BoneTransform[146], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, Light, 1, false, 1);
                CreateBlur(pCharacter, StartPos, EndPos, Light, 1, false, 3);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    }
}

bool CGMKarutan1::AttackEffectMonster(CHARACTER *, OBJECT *object, BMD *)
{
    return IsKarutanMap() && object->Type == MODEL_CRYPOS;
}

bool CGMKarutan1::PlayMonsterSound(OBJECT *o)
{
    if (!IsKarutanMap())
        return false;

    float fDis_x, fDis_y;
    fDis_x = o->Position[0] - Hero->Object.Position[0];
    fDis_y = o->Position[1] - Hero->Object.Position[1];
    float fDistance = sqrtf(fDis_x * fDis_x + fDis_y * fDis_y);

    if (fDistance > 600.0f)
        return true;

    switch (o->Type)
    {
    case MODEL_VENOMOUS_CHAIN_SCORPION:
        if (MONSTER01_ATTACK1 == o->CurrentAction || MONSTER01_ATTACK2 == o->CurrentAction)
            PlayBuffer(SOUND_KARUTAN_TCSCORPION_ATTACK);
        else if (MONSTER01_DIE == o->CurrentAction)
            PlayBuffer(SOUND_KARUTAN_TCSCORPION_DEATH);
        else if (MONSTER01_SHOCK == o->CurrentAction)
            PlayBuffer(SOUND_KARUTAN_TCSCORPION_HIT);
        return true;

    case MODEL_BONE_SCORPION:
        if (MONSTER01_ATTACK1 == o->CurrentAction || MONSTER01_ATTACK2 == o->CurrentAction)
            PlayBuffer(SOUND_KARUTAN_BONESCORPION_ATTACK);
        else if (MONSTER01_DIE == o->CurrentAction)
            PlayBuffer(SOUND_KARUTAN_BONESCORPION_DEATH);
        else if (MONSTER01_SHOCK == o->CurrentAction)
            PlayBuffer(SOUND_KARUTAN_BONESCORPION_HIT);
        return true;

    case MODEL_ORCUS:
        if (MONSTER01_WALK == o->CurrentAction)
        {
            if (1.0f <= o->AnimationFrame && o->AnimationFrame < 2.0f)
                PlayBuffer(SOUND_KARUTAN_ORCUS_MOVE1);
            if (7.0f <= o->AnimationFrame && o->AnimationFrame < 8.0f)
                PlayBuffer(SOUND_KARUTAN_ORCUS_MOVE2);
        }
        else if (MONSTER01_ATTACK1 == o->CurrentAction)
            PlayBuffer(SOUND_KARUTAN_ORCUS_ATTACK1);
        else if (MONSTER01_ATTACK2 == o->CurrentAction)
            PlayBuffer(SOUND_KARUTAN_ORCUS_ATTACK2);
        else if (MONSTER01_DIE == o->CurrentAction)
            PlayBuffer(SOUND_KARUTAN_ORCUS_DEATH);
        return true;

    case MODEL_GOLLOCK:
        if (MONSTER01_WALK == o->CurrentAction)
        {
            if (1.0f <= o->AnimationFrame && o->AnimationFrame < 2.0f)
                PlayBuffer(SOUND_KARUTAN_GOLOCH_MOVE1);
            if (7.0f <= o->AnimationFrame && o->AnimationFrame < 8.0f)
                PlayBuffer(SOUND_KARUTAN_GOLOCH_MOVE2);
        }
        else if (MONSTER01_ATTACK1 == o->CurrentAction || MONSTER01_ATTACK2 == o->CurrentAction)
            PlayBuffer(SOUND_KARUTAN_GOLOCH_ATTACK);
        else if (MONSTER01_DIE == o->CurrentAction)
            PlayBuffer(SOUND_KARUTAN_GOLOCH_DEATH);
        return true;

    case MODEL_CRYPTA:
        if (MONSTER01_WALK == o->CurrentAction)
        {
            if (1.0f <= o->AnimationFrame && o->AnimationFrame < 2.0f)
                PlayBuffer(SOUND_KARUTAN_CRYPTA_MOVE1);
            if (7.0f <= o->AnimationFrame && o->AnimationFrame < 8.0f)
                PlayBuffer(SOUND_KARUTAN_CRYPTA_MOVE2);
        }
        else if (MONSTER01_ATTACK1 == o->CurrentAction || MONSTER01_ATTACK2 == o->CurrentAction)
            PlayBuffer(SOUND_KARUTAN_CRYPTA_ATTACK);
        else if (MONSTER01_DIE == o->CurrentAction)
            PlayBuffer(SOUND_KARUTAN_CRYPTA_DEATH);
        return true;

    case MODEL_CRYPOS:
        if (MONSTER01_WALK == o->CurrentAction)
        {
            if (1.0f <= o->AnimationFrame && o->AnimationFrame < 2.0f)
                PlayBuffer(SOUND_KARUTAN_CRYPOS_MOVE1);
            if (7.0f <= o->AnimationFrame && o->AnimationFrame < 8.0f)
                PlayBuffer(SOUND_KARUTAN_CRYPOS_MOVE2);
        }
        else if (MONSTER01_ATTACK1 == o->CurrentAction)
            PlayBuffer(SOUND_KARUTAN_CRYPOS_ATTACK1);
        else if (MONSTER01_ATTACK2 == o->CurrentAction)
            PlayBuffer(SOUND_KARUTAN_CRYPOS_ATTACK2);
        else if (MONSTER01_DIE == o->CurrentAction)
            PlayBuffer(SOUND_KARUTAN_CRYPTA_DEATH);
        return true;

    case MODEL_CONDRA:
        if (MONSTER01_WALK == o->CurrentAction)
        {
            if (1.0f <= o->AnimationFrame && o->AnimationFrame < 2.0f)
                PlayBuffer(SOUND_KARUTAN_CONDRA_MOVE1);
            if (7.0f <= o->AnimationFrame && o->AnimationFrame < 8.0f)
                PlayBuffer(SOUND_KARUTAN_CONDRA_MOVE2);
        }
        else if (MONSTER01_ATTACK1 == o->CurrentAction || MONSTER01_ATTACK2 == o->CurrentAction)
            PlayBuffer(SOUND_KARUTAN_CONDRA_ATTACK);
        else if (MONSTER01_DIE == o->CurrentAction)
            PlayBuffer(SOUND_KARUTAN_CONDRA_DEATH);
        return true;

    case MODEL_NACONDRA:
        if (MONSTER01_WALK == o->CurrentAction)
        {
            if (1.0f <= o->AnimationFrame && o->AnimationFrame < 2.0f)
                PlayBuffer(SOUND_KARUTAN_CONDRA_MOVE1);
            if (7.0f <= o->AnimationFrame && o->AnimationFrame < 8.0f)
                PlayBuffer(SOUND_KARUTAN_CONDRA_MOVE2);
        }
        else if (MONSTER01_ATTACK1 == o->CurrentAction || MONSTER01_ATTACK2 == o->CurrentAction)
            PlayBuffer(SOUND_KARUTAN_NARCONDRA_ATTACK);
        else if (MONSTER01_DIE == o->CurrentAction)
            PlayBuffer(SOUND_KARUTAN_CONDRA_DEATH);
        return true;
    }

    return false;
}
#endif // ASG_ADD_KARUTAN_MONSTERS

void CGMKarutan1::PlayBGM()
{
    if (gMapManager.ContextMap() == WD_80KARUTAN1)
        PlayMp3(MUSIC_KARUTAN1);
    else
        StopMp3(MUSIC_KARUTAN1);

    if (gMapManager.ContextMap() == WD_81KARUTAN2)
        PlayMp3(MUSIC_KARUTAN2);
    else
        StopMp3(MUSIC_KARUTAN2);
}

float CGMKarutan1::ObjectAnimationSpeed(const OBJECT &object, const BMD &model, float speed) const
{
    return (object.Type == 66 || object.Type == 107) ? model.Actions[object.CurrentAction].PlaySpeed
                                                     : speed;
}

void CGMKarutan1::PlayAmbientSounds()
{
    if (gMapManager.ContextMap() == WD_80KARUTAN1)
    {
        PlayBuffer(SOUND_KARUTAN_DESERT_ENV, NULL, true);
    }
    else
    {
        if (HeroTile == 12)
        {
            StopBuffer(SOUND_KARUTAN_DESERT_ENV, true);
            PlayBuffer(SOUND_KARUTAN_KARDAMAHAL_ENV, NULL, true);
        }
        else
        {
            StopBuffer(SOUND_KARUTAN_KARDAMAHAL_ENV, true);
            PlayBuffer(SOUND_KARUTAN_DESERT_ENV, NULL, true);
        }
    }
}

bool CGMKarutan1::AllowsAmbientSound(ESound sound) const
{
    return sound == SOUND_KARUTAN_DESERT_ENV ||
           (gMapManager.ContextMap() == WD_80KARUTAN1 && sound == SOUND_KARUTAN_INSECT_ENV) ||
           (gMapManager.ContextMap() == WD_81KARUTAN2 && sound == SOUND_KARUTAN_KARDAMAHAL_ENV);
}

void CGMKarutan1::UpdateMusic()
{
    PlayBGM();
}

bool CGMKarutan1::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_KARUTAN1) == 0 || std::strcmp(track, MUSIC_KARUTAN2) == 0;
}

#endif // ASG_ADD_MAP_KARUTAN

bool GMLegacyLogin::AdvanceObjectVisual(OBJECT *o, BMD *b, float Luminosity)
{
    vec3_t p{}, Position{}, Light{};
    int Bitmap;
    float Scale, Rotation;
    {
        switch (o->Type)
        {
        case 84:
            if (rand_fps_check(5))
                CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 10, o->Scale, o);
            break;
        case 90:
        case 86:
            break;
        case 89: {
            int iTimeCheck = (int)WorldTime % 8000;

            if (iTimeCheck >= 7950)
            {
                Vector(o->Position[0] + (WorldRandom() % 2048 - 1024),
                       o->Position[1] + (WorldRandom() % 2048 - 1024),
                       o->Position[2] + (3000 + WorldRandom() % 600), Position);
                CreateEffectFpsChecked(MODEL_FIRE, Position, o->Angle, o->Light, 9);
            }
        }
        break;
        }
    }
    return true;
}

bool GMLegacyLogin::CreateWeather(PARTICLE *particle, int)
{
    return TheMapProcess().BattleCastle().CreateFireSnuff(particle);
}

bool GMLorencia::AdvanceObjectVisual(OBJECT *o, BMD *b, float Luminosity)
{
    vec3_t p{}, Position{}, Light{};
    int Bitmap;
    float Scale, Rotation;
    switch (o->Type)
    {
    case MODEL_WATERSPOUT:
        o->BlendMeshLight = 1.f;
        o->BlendMeshTexCoordV = -(int)WorldTime % 1000 * 0.001f;
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
        {
            const float fraction = emission.FrameFraction();
            PrepareWorldObjectPose(*o, fraction);
            Vector((float)(WorldRandom() % 32 - 16), -20.f, (float)(WorldRandom() % 32 - 16), p);
            b->TransformPosition(BoneTransform[1], p, Position);
            CreateParticle(BITMAP_SMOKE, Position, o->Angle, o->Light);
            Vector((float)(WorldRandom() % 32 - 16), -80.f, (float)(WorldRandom() % 32 - 16), p);
            b->TransformPosition(BoneTransform[4], p, Position);
            CreateParticle(BITMAP_SMOKE, Position, o->Angle, o->Light);
        }
        break;
    case MODEL_MERCHANT_ANIMAL01:
        PrepareWorldObjectPose(*o);
        Scale = Luminosity * 5.f;
        Vector(Luminosity * 0.6f, Luminosity * 0.3f, Luminosity * 0.1f, Light);
        b->TransformPosition(BoneTransform[48], Position, p);
        CreateSprite(BITMAP_LIGHT, p, Scale, Light, o);
        b->TransformPosition(BoneTransform[57], Position, p);
        CreateSprite(BITMAP_LIGHT, p, Scale, Light, o);

        break;
    }
    return true;
}

void GMLorencia::PrepareObjectUpdate(OBJECT *o)
{
    if (o->Type == MODEL_HOUSE_WALL01 + 4 || o->Type == MODEL_HOUSE_WALL01 + 5)
    {
        if (HeroTile == 4)
            o->AlphaTarget = 0.f;
        else
            o->AlphaTarget = 1.f;
    }
#ifdef _PVP_MURDERER_HERO_ITEM
    else if (o->Type == MODEL_MURDERER_DOG)
    {
        if (o->AnimationCycleEnded)
        {
            if (Random.FpsCheck(10, 1.0) && o->CurrentAction != MONSTER01_STOP2)
                o->CurrentAction = MONSTER01_STOP2;
            else
                o->CurrentAction = MONSTER01_STOP1;
        }
    }
#endif // _PVP_MURDERER_HERO_ITEM
}

void GMLorencia::PlayAmbientSounds()
{
    if (HeroTile == 4)
    {
        StopBuffer(SOUND_WIND01, true);
        StopBuffer(SOUND_RAIN01, true);
    }
    else
    {
        PlayBuffer(SOUND_WIND01, NULL, true);
        if (RainCurrent > 0)
            PlayBuffer(SOUND_RAIN01, NULL, true);
    }
}

bool GMLorencia::AllowsAmbientSound(ESound sound) const
{
    return (sound == SOUND_WIND01 || sound == SOUND_RAIN01);
}

void GMLorencia::UpdateMusic()
{
    if (Hero->SafeZone)
        PlayMp3(HeroTile == 4 ? MUSIC_PUB : MUSIC_MAIN_THEME);
}

bool GMLorencia::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_PUB) == 0 || std::strcmp(track, MUSIC_MAIN_THEME) == 0;
}

bool GMLorencia::CreateWeather(PARTICLE *o, int)
{
    o->Type = BITMAP_LEAF1;
    vec3_t Position;
    Vector(Hero->Object.Position[0] + (Random.RangeFloat(-800, 799)),
           Hero->Object.Position[1] + (Random.RangeFloat(-500, 899)),
           Hero->Object.Position[2] + (Random.RangeFloat(50, 349)), Position);
    VectorCopy(Position, o->Position);
    VectorCopy(Position, o->StartPosition);
    o->Velocity[0] = -Random.RangeFloat(64, 127) * 0.1f;
    if (Position[1] < sessionKeeper_.CameraStateObject().Position[1] + 400.f)
    {
        o->Velocity[0] = -o->Velocity[0] + 3.2f;
    }

    o->Velocity[1] = Random.RangeFloat(-16, 15) * 0.1f;
    o->Velocity[2] = Random.RangeFloat(-16, 15) * 0.1f;
    o->TurningForce[0] = Random.RangeFloat(-8, 7) * 0.1f;
    o->TurningForce[1] = Random.RangeFloat(-32, 31) * 0.1f;
    o->TurningForce[2] = Random.RangeFloat(-8, 7) * 0.1f;

    return true;
}

bool GMLorencia::MoveAirWeather(PARTICLE *o, bool splash)
{
    if (o->Type == BITMAP_RAIN)
    {
        VectorAddScaled(o->Position, o->Velocity, o->Position, FPS_ANIMATION_FACTOR);
        float Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
        if (o->Position[2] < Height)
        {
            o->Live = false;
            o->Position[2] = Height + 10.f;
            if (splash)
                CreateParticle(BITMAP_RAIN_CIRCLE, o->Position, o->Angle, o->Light);
        }
    }
    else
    {
        MoveAirLeaf(o, true, false);

        vec3_t Range;
        VectorSubtract(o->StartPosition, o->Position, Range);
        float Length = Range[0] * Range[0] + Range[1] * Range[1] + Range[2] * Range[2];
        if (Length >= 200000.f)
            o->Live = false;
    }

    return true;
}

bool GMLorencia::MoveWeather(PARTICLE *particle)
{
    return MoveAirWeather(particle, true);
}

void GMLorencia::ConfigureAmbientFish(OBJECT *o)
{
    o->Type = MODEL_FISH01;
    o->AlphaTarget = (float)(WorldRandom() % 2 + 2) * 0.1f;
    o->Velocity = 0.6f / o->Scale;
}

bool GMLorencia::CanCreateAmbientFish(int index)
{
    return TerrainMappingLayer1[index] == 5;
}

bool GMLorencia::ConfigureAmbientBoid(OBJECT *object, int)
{
    object->Type = MODEL_BIRD01;
    return false;
}

bool GMLorencia::CanCreateAmbientBoid(int slot, int index)
{
    return true;
}

ESound GMLorencia::WalkingSound(int tile, bool safe) const
{
    return tile == 0 ? SOUND_HUMAN_WALK_GRASS : SOUND_HUMAN_WALK_GROUND;
}

int GMLorencia::PlayerNpcText(bool actionChanged)
{
    constexpr int textIds[] = {823};
    if (actionChanged)
        playerNpcLorenciaTextIndex = textIds[WorldRandom() % 1];
    return playerNpcLorenciaTextIndex;
}

bool GMLostTower::AdvanceObjectVisual(OBJECT *o, BMD *b, float Luminosity)
{
    vec3_t p{}, Position{}, Light{};
    int Bitmap;
    float Scale, Rotation;
    switch (o->Type)
    {
    case 19:
    case 20:
        PrepareWorldObjectPose(*o);
        if (o->Type == 19)
        {
            Bitmap = BITMAP_MAGIC + 1;
            Vector(Luminosity * 1.f, Luminosity * 0.2f, Luminosity * 0.f, Light);
        }
        else
        {
            Bitmap = BITMAP_LIGHTNING + 1;
            Vector(Luminosity * 0.4f, Luminosity * 0.8f, Luminosity * 1.f, Light);
        }
        Rotation = (float)((int)(WorldTime * 0.1f) % 360);
        b->TransformPosition(BoneTransform[15], p, Position);
        CreateSprite(Bitmap, Position, 0.3f, Light, o, Rotation);
        CreateSprite(Bitmap, Position, 0.3f, Light, o, -Rotation);
        b->TransformPosition(BoneTransform[19], p, Position);
        CreateSprite(Bitmap, Position, 0.3f, Light, o, Rotation);
        CreateSprite(Bitmap, Position, 0.3f, Light, o, -Rotation);
        b->TransformPosition(BoneTransform[21], p, Position);
        CreateSprite(Bitmap, Position, 1.5f, Light, o, Rotation);
        CreateSprite(Bitmap, Position, 1.5f, Light, o, -Rotation);
        break;
    case 40:
        Vector(Luminosity * 1.f, Luminosity * 1.f, Luminosity * 1.f, Light);
        Rotation = (float)((int)(WorldTime * 0.1f) % 360);
        VectorCopy(o->Position, Position);
        Vector(0.f, 0.f, 260.f, p);
        VectorAdd(Position, p, Position);
        CreateSprite(BITMAP_LIGHTNING + 1, Position, 2.5f, Light, o, Rotation);
        CreateSprite(BITMAP_LIGHTNING + 1, Position, 2.5f, Light, o, -Rotation);

        break;
    }
    return true;
}

void GMLostTower::PlayAmbientSounds()
{
    PlayBuffer(SOUND_TOWER01, NULL, true);
}

bool GMLostTower::AllowsAmbientSound(ESound sound) const
{
    return (sound == SOUND_TOWER01);
}

void GMLostTower::UpdateMusic()
{
    PlayMp3(MUSIC_LOSTTOWER_A);
}

bool GMLostTower::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_LOSTTOWER_A) == 0;
}

bool GMLostTower::ConfigureAmbientBoid(OBJECT *object, int)
{
    object->Type = MODEL_BAT01;
    return false;
}

bool GMLostTower::CanCreateAmbientBoid(int slot, int index)
{
    return true;
}

using namespace SEASON3B;

void GMNewTown::PlayObjectSound(OBJECT *pObject)
{
    if (IsNewMap73_74())
        return;

    float fDis_x, fDis_y;
    fDis_x = pObject->Position[0] - Hero->Object.Position[0];
    fDis_y = pObject->Position[1] - Hero->Object.Position[1];

    int Index = TERRAIN_INDEX_REPEAT((Hero->PositionX), (Hero->PositionY));
    BOOL bSafeZone = FALSE;
    if ((TerrainWall[Index] & TW_SAFEZONE) == TW_SAFEZONE)
        bSafeZone = TRUE;

    switch (pObject->Type)
    {
    case 2:
        if (!bSafeZone)
            PlayBuffer(SOUND_ELBELAND_WATERSMALL01, pObject, false);
        break;
    case 53:
        if (!bSafeZone)
            PlayBuffer(SOUND_ELBELAND_RAVINE01, pObject, false);
        break;
    case 56:
        PlayBuffer(SOUND_ELBELAND_ENTERATLANCE01, pObject, false);
        break;
    case 59:
        if (!bSafeZone)
            PlayBuffer(SOUND_ELBELAND_WATERFALLSMALL01, pObject, false);
        break;
    case 85:
        PlayBuffer(SOUND_ELBELAND_ENTERDEVIAS01, pObject, false);
        break;
    case 89:
        if (!bSafeZone)
            PlayBuffer(SOUND_ELBELAND_WATERWAY01, pObject, false);
        break;
    case 110:
        PlayBuffer(SOUND_ELBELAND_VILLAGEPROTECTION01, pObject, false);
        break;
    }
}

bool GMNewTown::AdvanceObjectVisual(OBJECT *pObject, BMD *pModel, float)
{
    if (!IsCurrentMap())
        return false;

    if (IsNewMap73_74())
    {
        empireGuardian4_.AdvanceObjectVisual(pObject, pModel);
        return true;
    }

    vec3_t p, Position, Light;

    switch (pObject->Type)
    {
    case 0:
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_TRUE_FIRE, pObject->Position, pObject->Angle, Light, 0,
                           pObject->Scale);
        }
        break;
    case 54:
        Vector(1.f, 1.f, 1.f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.f))
        {
            CreateParticle(BITMAP_WATERFALL_2, pObject->Position, pObject->Angle, Light, 4,
                           pObject->Scale);
        }
        break;
    case 58:
        Vector(1.f, 1.f, 1.f, Light);
        CreateParticleFpsChecked(BITMAP_WATERFALL_5, pObject->Position, pObject->Angle, Light, 0);
        break;
    case 59:
        Vector(1.f, 1.f, 1.f, Light);
        CreateParticleFpsChecked(BITMAP_WATERFALL_3, pObject->Position, pObject->Angle, Light, 8,
                                 pObject->Scale);
        break;
    case 60:
        if (pObject->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.06f, 0.07f, 0.08f, Light);
            for (int i = 0; i < 10; ++i)
            {
                CreateParticle(BITMAP_CLOUD, pObject->Position, pObject->Angle, Light, 3,
                               pObject->Scale, pObject);
            }
            pObject->HiddenMesh = -2;
        }
        break;
    case 61:
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_TRUE_BLUE, pObject->Position, pObject->Angle, Light, 0,
                           pObject->Scale);
        }
        break;
    case 63: {
        PrepareWorldObjectPose(*pObject);
        vec3_t vPos, vRelative;
        float fLumi;
        fLumi = (sinf(WorldTime * 0.001f) + 1.0f) * 0.3f + 0.4f;
        Vector(fLumi * 1.0f, fLumi * 1.0f, fLumi * 0.6f, Light);

        Vector(-40.0f, -10.0f, 0.0f, vRelative);
        pModel->TransformPosition(BoneTransform[5], vRelative, vPos, false);
        CreateSprite(BITMAP_LIGHT, vPos, pObject->Scale * 6.f, Light, pObject);
    }
    break;
    case 110:
        Vector(1.0f, 1.0f, 1.0f, Light);
        Vector(0.f, 0.f, 70.f, p);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
        {
            const float fraction = birth.FrameFraction();
            PrepareWorldObjectPose(*pObject, fraction);
            const double birthTime =
                WorldTime - birth.SceneRemainingFrames() * 1000.0 /
                                sessionKeeper_.ApplicationConfig().legacyReferenceFps;
            p[0] = (float)cosf(birthTime * 0.03f) * (30.f + WorldRandom() % 5);
            p[1] = (float)sinf(birthTime * 0.03f) * (30.f + WorldRandom() % 5);
            pModel->TransformPosition(BoneTransform[0], p, Position, false);
            CreateParticle(BITMAP_LIGHT, Position, pObject->Angle, Light, 11, 0.6f, pObject);
        }
        break;
    case 121: {
        PrepareWorldObjectPose(*pObject);
        vec3_t vPos, vRelative;
        float fLumi, fScale;

        fLumi = (sinf(WorldTime * 0.002f) + 1.0f) * 0.5f + 0.5f;
        Vector(fLumi * 1.0f, fLumi * 0.5f, fLumi * 0.3f, Light);
        fScale = fLumi / 1.0f;

        Vector(5.0f, -4.0f, -1.0f, vRelative);
        for (int i = 3; i <= 8; ++i)
        {
            pModel->TransformPosition(BoneTransform[i], vRelative, vPos, false);
            CreateSprite(BITMAP_LIGHT, vPos, fScale, Light, pObject);
        }
    }
    break;
    case 133:
    case 134:
    case 135:
    case 136:
    case 137:
    case 138:
    case 139:
    case 140:
    case 141:
    case 142:
    case 143:
    case 144:
    case 145:
    case 146:
    case 147:
        if (pObject->HiddenMesh != -2)
        {
            pObject->HiddenMesh = -2;

            int icntIndex = 0;

            for (int i = 0; i < CharactersClient.Size(); i++)
            {
                if (!CharactersClient.IsValidIndex(i))
                    continue;
                icntIndex = i;
                if (!CharactersClient[i].Object.Live)
                    break;
            }

            CHARACTER *pCharacter = &CharactersClient[icntIndex];
            OBJECT *pNewObject = &CharactersClient[icntIndex].Object;
            CreateCharacterPointer(pCharacter, MODEL_PLAYER, 0, 0, 0);
            Vector(0.3f, 0.3f, 0.3f, pNewObject->Light);
            pCharacter->Key = icntIndex;

            int Level = 0;
            switch (pObject->Type)
            {
            case 133:
                pCharacter->Class = CLASS_KNIGHT;
                pCharacter->Skin = 0;
                Level = 9;
                pCharacter->BodyPart[BODYPART_HELM].Type = MODEL_PLATE_HELM;
                pCharacter->BodyPart[BODYPART_HELM].Level = Level;
                pCharacter->BodyPart[BODYPART_ARMOR].Type = MODEL_PLATE_ARMOR;
                pCharacter->BodyPart[BODYPART_ARMOR].Level = Level;
                pCharacter->BodyPart[BODYPART_PANTS].Type = MODEL_PLATE_PANTS;
                pCharacter->BodyPart[BODYPART_PANTS].Level = Level;
                pCharacter->BodyPart[BODYPART_GLOVES].Type = MODEL_PLATE_GLOVES;
                pCharacter->BodyPart[BODYPART_GLOVES].Level = Level;
                pCharacter->BodyPart[BODYPART_BOOTS].Type = MODEL_PLATE_BOOTS;
                pCharacter->BodyPart[BODYPART_BOOTS].Level = Level;
                pCharacter->Weapon[0].Type = MODEL_LIGHTING_SWORD;
                pCharacter->Weapon[0].Level = 9;
                pCharacter->Weapon[1].Type = MODEL_PLATE_SHIELD;
                pCharacter->Weapon[1].Level = 9;
                pCharacter->Wing.Type = -1;
                pCharacter->Helper.Type = -1;
                break;
            case 134:
                pCharacter->Class = CLASS_KNIGHT;
                pCharacter->Skin = 0;
                Level = 10;
                pCharacter->BodyPart[BODYPART_HELM].Type = MODEL_DRAGON_HELM;
                pCharacter->BodyPart[BODYPART_HELM].Level = Level;
                pCharacter->BodyPart[BODYPART_ARMOR].Type = MODEL_DRAGON_ARMOR;
                pCharacter->BodyPart[BODYPART_ARMOR].Level = Level;
                pCharacter->BodyPart[BODYPART_PANTS].Type = MODEL_DRAGON_PANTS;
                pCharacter->BodyPart[BODYPART_PANTS].Level = Level;
                pCharacter->BodyPart[BODYPART_GLOVES].Type = MODEL_DRAGON_GLOVES;
                pCharacter->BodyPart[BODYPART_GLOVES].Level = Level;
                pCharacter->BodyPart[BODYPART_BOOTS].Type = MODEL_DRAGON_BOOTS;
                pCharacter->BodyPart[BODYPART_BOOTS].Level = Level;
                pCharacter->Weapon[0].Type = MODEL_SWORD_OF_DESTRUCTION;
                pCharacter->Weapon[0].Level = 10;
                pCharacter->Weapon[1].Type = MODEL_SWORD_OF_DESTRUCTION;
                pCharacter->Weapon[1].Level = 10;
                pCharacter->Wing.Type = MODEL_WINGS_OF_DRAGON;
                pCharacter->Helper.Type = -1;
                break;
                // 			case 135:
            case 136:
                pCharacter->Class = CLASS_KNIGHT;
                pCharacter->Skin = 0;
                Level = 13;
                pCharacter->BodyPart[BODYPART_HELM].Type = MODEL_DRAGON_KNIGHT_HELM;
                pCharacter->BodyPart[BODYPART_HELM].Level = Level;
                pCharacter->BodyPart[BODYPART_ARMOR].Type = MODEL_DRAGON_KNIGHT_ARMOR;
                pCharacter->BodyPart[BODYPART_ARMOR].Level = Level;
                pCharacter->BodyPart[BODYPART_PANTS].Type = MODEL_DRAGON_KNIGHT_PANTS;
                pCharacter->BodyPart[BODYPART_PANTS].Level = Level;
                pCharacter->BodyPart[BODYPART_GLOVES].Type = MODEL_DRAGON_KNIGHT_GLOVES;
                pCharacter->BodyPart[BODYPART_GLOVES].Level = Level;
                pCharacter->BodyPart[BODYPART_BOOTS].Type = MODEL_DRAGON_KNIGHT_BOOTS;
                pCharacter->BodyPart[BODYPART_BOOTS].Level = Level;
                pCharacter->Weapon[0].Type = MODEL_DARK_BREAKER;
                pCharacter->Weapon[0].Level = 13;
                pCharacter->Weapon[1].Type = -1;
                pCharacter->Weapon[1].Level = 0;
                pCharacter->Wing.Type = MODEL_WINGS_OF_DRAGON;
                pCharacter->Helper.Type = MODEL_HORN_OF_FENRIR;
                CreateMount(MODEL_FENRIR_BLUE, pNewObject->Position, pNewObject);
                break;
                // 			case 137:
            case 138:
                pCharacter->Class = CLASS_WIZARD;
                pCharacter->Skin = 0;
                Level = 10;
                pCharacter->BodyPart[BODYPART_HELM].Type = MODEL_LEGENDARY_HELM;
                pCharacter->BodyPart[BODYPART_HELM].Level = Level;
                pCharacter->BodyPart[BODYPART_ARMOR].Type = MODEL_LEGENDARY_ARMOR;
                pCharacter->BodyPart[BODYPART_ARMOR].Level = Level;
                pCharacter->BodyPart[BODYPART_PANTS].Type = MODEL_LEGENDARY_PANTS;
                pCharacter->BodyPart[BODYPART_PANTS].Level = Level;
                pCharacter->BodyPart[BODYPART_GLOVES].Type = MODEL_LEGENDARY_GLOVES;
                pCharacter->BodyPart[BODYPART_GLOVES].Level = Level;
                pCharacter->BodyPart[BODYPART_BOOTS].Type = MODEL_LEGENDARY_BOOTS;
                pCharacter->BodyPart[BODYPART_BOOTS].Level = Level;
                pCharacter->Weapon[0].Type = MODEL_LEGENDARY_STAFF;
                pCharacter->Weapon[0].Level = 10;
                pCharacter->Weapon[1].Type = MODEL_LEGENDARY_SHIELD;
                pCharacter->Weapon[1].Level = 10;
                pCharacter->Wing.Type = MODEL_WINGS_OF_SOUL;
                pCharacter->Helper.Type = -1;
                break;
            case 139:
                pCharacter->Class = CLASS_WIZARD;
                pCharacter->Skin = 0;
                Level = 13;
                pCharacter->BodyPart[BODYPART_HELM].Type = MODEL_VENOM_MIST_HELM;
                pCharacter->BodyPart[BODYPART_HELM].Level = Level;
                pCharacter->BodyPart[BODYPART_ARMOR].Type = MODEL_VENOM_MIST_ARMOR;
                pCharacter->BodyPart[BODYPART_ARMOR].Level = Level;
                pCharacter->BodyPart[BODYPART_PANTS].Type = MODEL_VENOM_MIST_PANTS;
                pCharacter->BodyPart[BODYPART_PANTS].Level = Level;
                pCharacter->BodyPart[BODYPART_GLOVES].Type = MODEL_VENOM_MIST_GLOVES;
                pCharacter->BodyPart[BODYPART_GLOVES].Level = Level;
                pCharacter->BodyPart[BODYPART_BOOTS].Type = MODEL_VENOM_MIST_BOOTS;
                pCharacter->BodyPart[BODYPART_BOOTS].Level = Level;
                pCharacter->Weapon[0].Type = MODEL_PLATINA_STAFF;
                pCharacter->Weapon[0].Level = 13;
                pCharacter->Weapon[1].Type = -1;
                pCharacter->Weapon[1].Level = 0;
                pCharacter->Wing.Type = MODEL_WING_OF_ETERNAL;
                pCharacter->Helper.Type = -1;
                break;
                // 			case 140:
                // 			case 141:
            case 142:
                pCharacter->Class = CLASS_ELF;
                pCharacter->Skin = 0;
                Level = 13;
                pCharacter->BodyPart[BODYPART_HELM].Type = MODEL_SYLPHID_RAY_HELM;
                pCharacter->BodyPart[BODYPART_HELM].Level = Level;
                pCharacter->BodyPart[BODYPART_ARMOR].Type = MODEL_SYLPHID_RAY_ARMOR;
                pCharacter->BodyPart[BODYPART_ARMOR].Level = Level;
                pCharacter->BodyPart[BODYPART_PANTS].Type = MODEL_SYLPHID_RAY_PANTS;
                pCharacter->BodyPart[BODYPART_PANTS].Level = Level;
                pCharacter->BodyPart[BODYPART_GLOVES].Type = MODEL_SYLPHID_RAY_GLOVES;
                pCharacter->BodyPart[BODYPART_GLOVES].Level = Level;
                pCharacter->BodyPart[BODYPART_BOOTS].Type = MODEL_SYLPHID_RAY_BOOTS;
                pCharacter->BodyPart[BODYPART_BOOTS].Level = Level;
                pCharacter->Weapon[0].Type = -1;
                pCharacter->Weapon[0].Level = 0;
                pCharacter->Weapon[1].Type = MODEL_ALBATROSS_BOW;
                pCharacter->Weapon[1].Level = 13;
                pCharacter->Wing.Type = MODEL_WING_OF_ILLUSION;
                pCharacter->Helper.Type = -1;
                break;
                // 			case 143:
            case 144:
                pCharacter->Class = CLASS_DARK;
                pCharacter->Skin = 0;
                Level = 13;
                pCharacter->BodyPart[BODYPART_HELM].Type = MODEL_HELM + 32;
                pCharacter->BodyPart[BODYPART_HELM].Level = Level;
                pCharacter->BodyPart[BODYPART_ARMOR].Type = MODEL_VOLCANO_ARMOR;
                pCharacter->BodyPart[BODYPART_ARMOR].Level = Level;
                pCharacter->BodyPart[BODYPART_PANTS].Type = MODEL_VOLCANO_PANTS;
                pCharacter->BodyPart[BODYPART_PANTS].Level = Level;
                pCharacter->BodyPart[BODYPART_GLOVES].Type = MODEL_VOLCANO_GLOVES;
                pCharacter->BodyPart[BODYPART_GLOVES].Level = Level;
                pCharacter->BodyPart[BODYPART_BOOTS].Type = MODEL_VOLCANO_BOOTS;
                pCharacter->BodyPart[BODYPART_BOOTS].Level = Level;
                pCharacter->Weapon[0].Type = MODEL_EXPLOSION_BLADE;
                pCharacter->Weapon[0].Level = 13;
                pCharacter->Weapon[1].Type = -1;
                pCharacter->Weapon[1].Level = 0;
                pCharacter->Wing.Type = MODEL_WING_OF_RUIN;
                pCharacter->Helper.Type = -1;
                break;
                // 			case 145:
            case 146:
                pCharacter->Class = CLASS_DARK_LORD;
                pCharacter->Skin = 0;
                Level = 13;
                pCharacter->BodyPart[BODYPART_HELM].Type = MODEL_DARK_MASTER_MASK;
                pCharacter->BodyPart[BODYPART_HELM].Level = Level;
                pCharacter->BodyPart[BODYPART_ARMOR].Type = MODEL_DARK_MASTER_ARMOR;
                pCharacter->BodyPart[BODYPART_ARMOR].Level = Level;
                pCharacter->BodyPart[BODYPART_PANTS].Type = MODEL_DARK_MASTER_PANTS;
                pCharacter->BodyPart[BODYPART_PANTS].Level = Level;
                pCharacter->BodyPart[BODYPART_GLOVES].Type = MODEL_DARK_MASTER_GLOVES;
                pCharacter->BodyPart[BODYPART_GLOVES].Level = Level;
                pCharacter->BodyPart[BODYPART_BOOTS].Type = MODEL_DARK_MASTER_BOOTS;
                pCharacter->BodyPart[BODYPART_BOOTS].Level = Level;
                pCharacter->Weapon[0].Type = MODEL_SHINING_SCEPTER;
                pCharacter->Weapon[0].Level = 10;
                pCharacter->Weapon[1].Type = -1;
                pCharacter->Weapon[1].Level = 0;
                pCharacter->Wing.Type = MODEL_CAPE_OF_LORD;
                pCharacter->Helper.Type = MODEL_DARK_HORSE_ITEM;
                CreateMount(MODEL_DARK_HORSE, pNewObject->Position, pNewObject);
                CreatePetDarkSpirit_Now(pCharacter);
                break;
            case 147:
                pCharacter->Class = CLASS_DARK_LORD;
                pCharacter->Skin = 0;
                Level = 13;
                pCharacter->BodyPart[BODYPART_HELM].Type = MODEL_SUNLIGHT_MASK;
                pCharacter->BodyPart[BODYPART_HELM].Level = Level;
                pCharacter->BodyPart[BODYPART_ARMOR].Type = MODEL_SUNLIGHT_ARMOR;
                pCharacter->BodyPart[BODYPART_ARMOR].Level = Level;
                pCharacter->BodyPart[BODYPART_PANTS].Type = MODEL_SUNLIGHT_PANTS;
                pCharacter->BodyPart[BODYPART_PANTS].Level = Level;
                pCharacter->BodyPart[BODYPART_GLOVES].Type = MODEL_SUNLIGHT_GLOVES;
                pCharacter->BodyPart[BODYPART_GLOVES].Level = Level;
                pCharacter->BodyPart[BODYPART_BOOTS].Type = MODEL_SUNLIGHT_BOOTS;
                pCharacter->BodyPart[BODYPART_BOOTS].Level = Level;
                pCharacter->Weapon[0].Type = MODEL_SOLEIL_SCEPTER;
                pCharacter->Weapon[0].Level = 13;
                pCharacter->Weapon[1].Type = -1;
                pCharacter->Weapon[1].Level = 0;
                pCharacter->Wing.Type = MODEL_CAPE_OF_EMPEROR;
                pCharacter->Helper.Type = -1;
                break;
            }
            VectorCopy(pObject->Position, pNewObject->Position);
            VectorCopy(pObject->Angle, pNewObject->Angle);
            SetCharacterScale(pCharacter);

            SetPlayerAttack(pCharacter);
            pNewObject->PriorAnimationFrame = pNewObject->AnimationFrame =
                WorldRandom() %
                Models[pNewObject->Type].Actions[pNewObject->CurrentAction].NumAnimationKeys;
            pObject->Owner = pNewObject;
            pObject->SubType = icntIndex;
        }
        break;
    case 149:
    case 150:
    case 151:
    case 152:
    case 153:
    case 154:
    case 155:
        if (pObject->HiddenMesh != -2)
        {
            pObject->HiddenMesh = -2;

            int icntIndex = 0;

            for (int i = 0; i < CharactersClient.Size(); i++)
            {
                if (!CharactersClient.IsValidIndex(i))
                    continue;
                icntIndex = i;
                if (!CharactersClient[i].Object.Live)
                {
                    break;
                }
            }

            OBJECT *pNewObject = &CharactersClient[icntIndex].Object;

            switch (pObject->Type)
            {
            case 149:
                CreateMonster(EMonsterType::MONSTER_POLLUTED_BUTTERFLY, 0, 0, icntIndex);
                break;
            case 150:
                CreateMonster(MONSTER_HIDEOUS_RABBIT, 0, 0, icntIndex);
                break;
            case 151:
                CreateMonster(MONSTER_WEREWOLF2, 0, 0, icntIndex);
                break;
            case 152:
                CreateMonster(MONSTER_CURSED_LICH, 0, 0, icntIndex);
                break;
            case 153:
                CreateMonster(MONSTER_TOTEM_GOLEM, 0, 0, icntIndex);
                break;
            case 154:
                CreateMonster(MONSTER_GRIZZLY, 0, 0, icntIndex);
                break;
            case 155:
                CreateMonster(MONSTER_CAPTAIN_GRIZZLY, 0, 0, icntIndex);
                break;
            }

            VectorCopy(pObject->Position, pNewObject->Position);
            VectorCopy(pObject->Angle, pNewObject->Angle);
            SetAction(pNewObject, MONSTER01_ATTACK1);
            if (Models[pNewObject->Type].Actions[MONSTER01_ATTACK1].NumAnimationKeys > 0)
            {
                pNewObject->PriorAnimationFrame = pNewObject->AnimationFrame =
                    WorldRandom() %
                    Models[pNewObject->Type].Actions[MONSTER01_ATTACK1].NumAnimationKeys;
            }
            else
            {
                assert(!"Attack action is missing.");
            }
            pObject->Owner = pNewObject;
        }
        break;
    }

    if (pObject->Type >= 133 && pObject->Type <= 147)
    {
        if (pObject->Owner != NULL)
        {
            CHARACTER *pCharacter = &CharactersClient[pObject->SubType];
            if (pObject->Owner->CurrentAction < PLAYER_WALK_MALE)
            {
                if (pObject->Type == 133)
                {
                    if (rand_fps_check(2))
                    {
                        pCharacter->Helper.Type = -1;
                        SetPlayerAttack(pCharacter);
                    }
                    else
                    {
                        pCharacter->Helper.Type = -1;
                        SetAction(pObject->Owner, PLAYER_ATTACK_SKILL_WHEEL);
                        pCharacter->Skill = AT_SKILL_TWISTING_SLASH;
                        pCharacter->AttackTime = 1;
                        SetCharacterTarget(*pCharacter, -1);
                        pCharacter->AttackFlag = ATTACK_FAIL;
                        pCharacter->SkillX = pCharacter->PositionX;
                        pCharacter->SkillY = pCharacter->PositionY;
                    }
                }
                else if (pObject->Type == 134)
                {
                    if (rand_fps_check(2))
                    {
                        SetAction(pObject->Owner, PLAYER_ATTACK_SKILL_WHEEL);
                        pCharacter->Skill = AT_SKILL_TWISTING_SLASH;
                        pCharacter->AttackTime = 1;
                        SetCharacterTarget(*pCharacter, -1);
                        pCharacter->AttackFlag = ATTACK_FAIL;
                        pCharacter->SkillX = pCharacter->PositionX;
                        pCharacter->SkillY = pCharacter->PositionY;
                    }
                    else
                    {
                        SetAction(pObject->Owner, PLAYER_ATTACK_DEATHSTAB);
                        pCharacter->Skill = AT_SKILL_DEATHSTAB;
                        pCharacter->AttackTime = 1;
                        SetCharacterTarget(*pCharacter, -1);
                        pCharacter->AttackFlag = ATTACK_FAIL;
                        pCharacter->SkillX = pCharacter->PositionX;
                        pCharacter->SkillY = pCharacter->PositionY;
                    }
                }
                else if (pObject->Type == 138)
                {
                    if (rand_fps_check(2))
                    {
                        SetPlayerAttack(pCharacter);
                    }
                    else
                    {
                        SetPlayerMagic(pCharacter);
                        pCharacter->Skill = AT_SKILL_INFERNO;
                        pCharacter->AttackTime = 1;
                        SetCharacterTarget(*pCharacter, -1);
                        pCharacter->AttackFlag = ATTACK_FAIL;
                        pCharacter->SkillX = pCharacter->PositionX;
                        pCharacter->SkillY = pCharacter->PositionY;
                    }
                }
                else if (pObject->Type == 139)
                {
                    SetPlayerStop(pCharacter);
                }
                else if (pObject->Type == 142)
                {
                    SetPlayerStop(pCharacter);
                }
                else if (pObject->Type == 144)
                {
                    SetPlayerStop(pCharacter);
                }
                else if (pObject->Type == 147)
                {
                    SetPlayerStop(pCharacter);
                }
            }
        }
    }
    else if (pObject->Type >= 149 && pObject->Type <= 155)
    {
        if (pObject->Owner != NULL)
        {
            if (pObject->Owner->CurrentAction < MONSTER01_WALK)
            {
                if (pObject->Type == 153)
                {
                    int iRand = WorldRandom() % 3;
                    if (iRand == 0)
                        SetAction(pObject->Owner, MONSTER01_ATTACK1);
                    else if (iRand == 1)
                        SetAction(pObject->Owner, MONSTER01_ATTACK2);
                    else
                        SetAction(pObject->Owner, MONSTER01_SHOCK);
                }
                else if ((pObject->Type >= 152 && pObject->Type <= 155))
                {
                    if (rand_fps_check(2))
                        SetAction(pObject->Owner, MONSTER01_ATTACK1);
                    else
                        SetAction(pObject->Owner, MONSTER01_SHOCK);
                }
                else if ((pObject->Type >= 149 && pObject->Type <= 151))
                {
                    SetAction(pObject->Owner, MONSTER01_STOP1);
                }
            }
        }
    }

    return true;
}

void GMNewTown::MoveSharedMonsterBlur(CHARACTER *pCharacter, OBJECT *pObject, BMD *pModel)
{
    switch (pObject->Type)
    {
    case MODEL_HIDEOUS_RABBIT: {
        float Start_Frame = 0.f;
        float End_Frame = 6.0f;
        if ((pObject->AnimationFrame >= Start_Frame && pObject->AnimationFrame <= End_Frame &&
             pObject->CurrentAction == MONSTER01_ATTACK1) ||
            (pObject->AnimationFrame >= Start_Frame && pObject->AnimationFrame <= End_Frame &&
             pObject->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t Light;
            Vector(1.0f, 1.2f, 2.f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = pModel->Actions[pObject->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = pObject->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                pModel->AnimationAtFrame(BoneTransform, fAnimationFrame,
                                         pObject->PriorAnimationFrame, pObject->PriorAction,
                                         pObject->Angle, pObject->HeadAngle);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);

                pModel->TransformPosition(BoneTransform[60], StartRelative, StartPos, false);
                pModel->TransformPosition(BoneTransform[61], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, Light, 0);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    }
}

bool GMNewTown::AdvanceMonsterVisual(CHARACTER *pCharacter, OBJECT *pObject, BMD *pModel,
                                     WorldCharacterVisualState &visual)
{
    if (!IsCurrentMap())
        return false;

    vec3_t vPos, vRelative, Light;
    float fLumi, fScale;

    switch (pObject->Type)
    {
    case MODEL_ELBELAND_RHEA:
        if (visual.action == 0)
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 5.f))
            {
                Vector((WorldRandom() % 90 + 10) * 0.01f, (WorldRandom() % 90 + 10) * 0.01f,
                       (WorldRandom() % 90 + 10) * 0.01f, Light);
                fScale = (WorldRandom() % 5 + 5) * 0.1f;
                Vector(0.f, 0.f, 0.f, vRelative);
                vRelative[0] = 20 + (WorldRandom() % 1000 - 500) * 0.1f;
                vRelative[1] = (WorldRandom() % 300 - 150) * 0.1f;
                const float fraction = birth.FrameFraction();
                AnimationPoseSample pose(pObject, pModel->BoneHead, pModel->BodyHeight, false,
                                         pModel->PoseAssetIdentity());
                pose.SampleBonePosition(*pModel, *pObject, 35, vRelative, WorldTime, fraction,
                                        vPos);
                CreateParticle(BITMAP_LIGHT, vPos, pObject->Angle, Light, 13, fScale, pObject);
                CreateParticle(BITMAP_LIGHT, vPos, pObject->Angle, Light, 12, fScale, pObject);
            }
        break;
    case MODEL_ELBELAND_MARCE: {
        Vector(10.0f, 0.0f, 0.0f, vRelative);
        pModel->TransformPosition(pObject->BoneTransform[81], vRelative, vPos, true);
        fScale = 1.5f;

        fLumi = (sinf(WorldTime * 0.001f) + 1.0f) * 0.3f + 0.4f;
        Vector(fLumi * 0.5f, fLumi * 0.5f, fLumi * 0.5f, Light);
        CreateSprite(BITMAP_FLARE_BLUE, vPos, fScale, Light, pObject);

        Vector(0.5f, 0.5f, 0.5f, Light);
        CreateSprite(BITMAP_FLARE_BLUE, vPos, fScale * 0.8f, Light, pObject, -WorldTime * 0.1f);
    }
    break;
    case MODEL_CURSED_LICH:
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            const float fraction = birth.FrameFraction();
            ObjectDrawInput draw(pObject);
            pObject->MotionTrace.Sample(WorldTime, fraction, pObject->Position, draw.position);
            AnimationPoseSample pose(draw, pModel->BoneHead, pModel->BodyHeight, false,
                                     pModel->PoseAssetIdentity());
            std::array<vec34_t, MAX_BONES> bones;
            draw.bones = pose.EvaluateAtTime(*pModel, *pObject, WorldTime, fraction, bones.data());
            for (int i = 0; i < 4; ++i)
            {
                Vector(0, (WorldRandom() % 300 - 150) * 0.1f, (WorldRandom() % 200 - 100) * 0.1f,
                       vRelative);
                pModel->TransformByObjectBone(vPos, draw, 30, vRelative);
                CreateParticle(BITMAP_FIRE_CURSEDLICH, vPos, pObject->Angle, pObject->Light, 0, 1,
                               pObject);
            }
        }
        break;
    case MODEL_TOTEM_GOLEM:
        MonsterMoveSandSmoke(pObject);
        if (visual.action == MONSTER01_DIE)
        {
            if (visual.emissionLifeTime == 100)
            {
                visual.emissionLifeTime = 90;

                vec3_t vRelativePos, vWorldPos, Light;
                Vector(1.0f, 1.0f, 1.0f, Light);

                Vector(0.f, 0.f, 0.f, vRelativePos);

                pModel->TransformPosition(pObject->BoneTransform[7], vRelativePos, vWorldPos, true);
                CreateEffect(MODEL_TOTEMGOLEM_PART1, vWorldPos, pObject->Angle, Light, 0, pObject,
                             0, 0);

                pModel->TransformPosition(pObject->BoneTransform[5], vRelativePos, vWorldPos, true);
                CreateEffect(MODEL_TOTEMGOLEM_PART2, vWorldPos, pObject->Angle, Light, 0, pObject,
                             0, 0);

                pModel->TransformPosition(pObject->BoneTransform[29], vRelativePos, vWorldPos,
                                          true);
                CreateEffect(MODEL_TOTEMGOLEM_PART3, vWorldPos, pObject->Angle, Light, 0, pObject,
                             0, 0);

                pModel->TransformPosition(pObject->BoneTransform[64], vRelativePos, vWorldPos,
                                          true);
                CreateEffect(MODEL_TOTEMGOLEM_PART4, vWorldPos, pObject->Angle, Light, 0, pObject,
                             0, 0);

                pModel->TransformPosition(pObject->BoneTransform[93], vRelativePos, vWorldPos,
                                          true);
                CreateEffect(MODEL_TOTEMGOLEM_PART5, vWorldPos, pObject->Angle, Light, 0, pObject,
                             0, 0);

                pModel->TransformPosition(pObject->BoneTransform[98], vRelativePos, vWorldPos,
                                          true);
                CreateEffect(MODEL_TOTEMGOLEM_PART6, vWorldPos, pObject->Angle, Light, 0, pObject,
                             0, 0);

                pModel->TransformPosition(pObject->BoneTransform[5], vRelativePos, vWorldPos, true);
                VectorCopy(vWorldPos, vRelativePos);

                for (int i = 0; i < 6; ++i)
                {
                    vWorldPos[0] = vRelativePos[0] + WorldRandom() % 160 - 80;
                    vWorldPos[1] = vRelativePos[1] + WorldRandom() % 160 - 80;
                    vWorldPos[2] = vRelativePos[2];
                    CreateParticle(BITMAP_LEAF_TOTEMGOLEM, vWorldPos, pObject->Angle, Light, 0,
                                   1.0f);
                }

                Vector(0.5f, 0.5f, 0.5f, Light);

                for (int i = 0; i < 20; ++i)
                {
                    vWorldPos[0] = vRelativePos[0] + WorldRandom() % 160 - 80;
                    vWorldPos[1] = vRelativePos[1] + WorldRandom() % 160 - 80;
                    vWorldPos[2] = vRelativePos[2] + (WorldRandom() % 150) - 50;
                    CreateParticle(BITMAP_SMOKE, vWorldPos, pObject->Angle, Light, 48, 1.0f);
                }
            }
        }
        break;
    }

    return false;
}

bool GMNewTown::PlayMonsterSound(OBJECT *pObject)
{
    if (!IsCurrentMap())
        return false;

    float fDis_x, fDis_y;
    fDis_x = pObject->Position[0] - Hero->Object.Position[0];
    fDis_y = pObject->Position[1] - Hero->Object.Position[1];
    float fDistance = sqrtf(fDis_x * fDis_x + fDis_y * fDis_y);

    if (fDistance > 500.0f)
        return true;

    switch (pObject->Type)
    {
    case MODEL_RABBIT:
        if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
            pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            PlayBuffer(SOUND_ELBELAND_RABBITSTRANGE_ATTACK01);
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_ELBELAND_RABBITSTRANGE_DEATH01);
        }
        return true;
    case MODEL_BUTTERFLY:
        if (pObject->CurrentAction == MONSTER01_WALK)
        {
            if (rand_fps_check(100))
            {
                PlayBuffer(SOUND_ELBELAND_RABBITUGLY_BREATH01);
            }
        }
        else if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
                 pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            PlayBuffer(SOUND_ELBELAND_RABBITUGLY_ATTACK01);
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_ELBELAND_RABBITUGLY_DEATH01);
        }
        return true;
    case MODEL_HIDEOUS_RABBIT:
        if (pObject->CurrentAction == MONSTER01_STOP1 ||
            pObject->CurrentAction == MONSTER01_STOP2 || pObject->CurrentAction == MONSTER01_WALK)
        {
            if (rand_fps_check(30))
            {
                PlayBuffer(SOUND_ELBELAND_WOLFHUMAN_MOVE02);
            }
        }
        else if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
                 pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            PlayBuffer(SOUND_ELBELAND_WOLFHUMAN_ATTACK01);
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_ELBELAND_WOLFHUMAN_DEATH01);
        }
        return true;
    case MODEL_WEREWOLF2:
        if (pObject->CurrentAction == MONSTER01_WALK)
        {
            if (rand_fps_check(100))
            {
                PlayBuffer(SOUND_ELBELAND_BUTTERFLYPOLLUTION_MOVE01);
            }
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_ELBELAND_BUTTERFLYPOLLUTION_DEATH01);
        }
        return true;
    case MODEL_CURSED_LICH:
        if (pObject->CurrentAction == MONSTER01_WALK)
        {
            if (rand_fps_check(100))
            {
                PlayBuffer(SOUND_ELBELAND_CURSERICH_MOVE01);
            }
        }
        else if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
                 pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            PlayBuffer(SOUND_ELBELAND_CURSERICH_ATTACK01);
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_ELBELAND_CURSERICH_DEATH01);
        }
        return true;
    case MODEL_TOTEM_GOLEM:
        if (pObject->CurrentAction == MONSTER01_WALK)
        {
            if (rand_fps_check(2))
                PlayBuffer(SOUND_ELBELAND_TOTEMGOLEM_MOVE01);
            else
                PlayBuffer(SOUND_ELBELAND_TOTEMGOLEM_MOVE02);
        }
        else if (pObject->CurrentAction == MONSTER01_ATTACK1)
        {
            PlayBuffer(SOUND_ELBELAND_TOTEMGOLEM_ATTACK01);
        }
        else if (pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            PlayBuffer(SOUND_ELBELAND_TOTEMGOLEM_ATTACK02);
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_ELBELAND_TOTEMGOLEM_DEATH01);
        }
        return true;
    case MODEL_GRIZZLY:
        if (pObject->CurrentAction == MONSTER01_WALK)
        {
            if (rand_fps_check(100))
            {
                PlayBuffer(SOUND_ELBELAND_BEASTWOO_MOVE01);
            }
        }
        else if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
                 pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            PlayBuffer(SOUND_ELBELAND_BEASTWOO_ATTACK01);
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_ELBELAND_BEASTWOO_DEATH01);
        }
        return true;
    case MODEL_CAPTAIN_GRIZZLY:
        if (pObject->CurrentAction == MONSTER01_WALK)
        {
            if (rand_fps_check(100))
            {
                PlayBuffer(SOUND_ELBELAND_BEASTWOOLEADER_MOVE01);
            }
        }
        else if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
                 pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            PlayBuffer(SOUND_ELBELAND_BEASTWOOLEADER_ATTACK01);
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_ELBELAND_BEASTWOO_DEATH01);
        }
        return true;
    }

    return true;
}

std::optional<bool> GMNewTown::ObjectVisibility(const OBJECT &object, bool)
{
    const int world = gMapManager.ContextMap();
    const int type = object.Type;
    if (world == WD_74NEW_CHARACTER_SCENE && (type == 129 || type == 98))
        return true;
    const float x = object.Position[0] * 0.01f, y = object.Position[1] * 0.01f;
    if (world == WD_73NEW_LOGIN_SCENE)
    {
        const auto &camera = sessionKeeper_.CameraStateObject();
        const float dx = camera.Position[0] - object.Position[0];
        const float dy = camera.Position[1] - object.Position[1];
        const bool extended = (type >= 122 && type <= 124) || type == 159 || type == 126 ||
                              type == 129 || type == 127;
        const float range = LoginSceneCameraDefaults::RENDER_OBJECT_DIST * (extended ? 2.f : 1.f);
        return (extended || type <= MAX_WORLD_OBJECTS) && dx * dx + dy * dy < range * range &&
               TestFrustrum2D(x, y, -500.f);
    }
    if (world == WD_51HOME_6TH_CHAR &&
        ((type >= 5 && type <= 14) || type == 87 || type == 88 || type == 4 || type == 129))
        return TestFrustrum2D(x, y, -400.f);
    return std::nullopt;
}

void GMNewTown::AdvanceObjectVisibility(OBJECT &object)
{
    if (gMapManager.ContextMap() != WD_73NEW_LOGIN_SCENE)
        return;
    if (object.Visible)
    {
        object.AlphaTarget = std::min(1.f, object.AlphaTarget + 0.03f * FPS_ANIMATION_FACTOR);
        const int type = object.Type;
        if ((type >= 122 && type <= 124) || type == 159 || type == 126 || type == 129 ||
            type == 127)
            object.BlendMeshLight =
                std::min(1.f, object.BlendMeshLight + 0.03f * FPS_ANIMATION_FACTOR);
        return;
    }
    const float dx = sessionKeeper_.CameraStateObject().Position[0] - object.Position[0];
    const float dy = sessionKeeper_.CameraStateObject().Position[1] - object.Position[1];
    const float range = LoginSceneCameraDefaults::RENDER_OBJECT_DIST;
    if (dx * dx + dy * dy > range * range)
        object.BlendMeshLight = object.Alpha = object.AlphaTarget = 0.f;
}

bool GMNewTown::AllowsAmbientSound(ESound sound) const
{
    return gMapManager.ContextMap() == WD_51HOME_6TH_CHAR &&
           (sound == SOUND_ELBELAND_VILLAGEPROTECTION01 ||
            sound == SOUND_ELBELAND_WATERFALLSMALL01 || sound == SOUND_ELBELAND_WATERWAY01 ||
            sound == SOUND_ELBELAND_ENTERDEVIAS01 || sound == SOUND_ELBELAND_WATERSMALL01 ||
            sound == SOUND_ELBELAND_RAVINE01 || sound == SOUND_ELBELAND_ENTERATLANCE01);
}

void GMNewTown::UpdateMusic()
{
    if (gMapManager.ContextMap() == WD_51HOME_6TH_CHAR)
        PlayMp3(MUSIC_ELBELAND);
    else
        StopMp3(MUSIC_ELBELAND);
}

bool GMNewTown::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_ELBELAND) == 0;
}

bool GMNewTown::ConfigureAmbientBoid(OBJECT *object, int)
{
    object->Type = MODEL_MAP_TORNADO;
    object->Velocity = 0.f;
    object->BlendMeshLight = 0.f;
    object->HeadAngle[0] = (WorldRandom() % 314) / 100.f;
    object->Position[1] += 5.f;
    object->m_bRenderAfterCharacter = true;
    return false;
}

bool GMNewTown::CanCreateAmbientBoid(int slot, int index)
{
    return gMapManager.ContextMap() == WD_51HOME_6TH_CHAR && slot < 1 && rand_fps_check(500) &&
           !Hero->SafeZone;
}

bool GMNewTown::PrepareAmbientBoidSlot(int index, bool &allowCreate)
{
    return gMapManager.ContextMap() == WD_51HOME_6TH_CHAR ? index < 2 : index < 5;
}

bool GMNoria::AdvanceObjectVisual(OBJECT *o, BMD *b, float Luminosity)
{
    vec3_t p{}, Position{}, Light{};
    int Bitmap;
    float Scale, Rotation;
    switch (o->Type)
    {
    case 9:
        PrepareWorldObjectPose(*o);
        Vector(Luminosity * 0.4f, Luminosity * 0.7f, Luminosity * 1.f, Light);
        b->TransformPosition(BoneTransform[1], p, Position);
        CreateSprite(BITMAP_LIGHT, Position, 1.5f, Light, o);
        break;
    case 35:
        PrepareWorldObjectPose(*o);
        Vector(Luminosity * 0.4f, Luminosity * 0.7f, Luminosity * 1.f, Light);
        b->TransformPosition(BoneTransform[3], p, Position);
        CreateSprite(BITMAP_LIGHT, Position, 1.5f, Light, o);
        break;
    case 1:
        PrepareWorldObjectPose(*o);
        Vector(Luminosity * 0.4f, Luminosity * 0.7f, Luminosity * 1.f, Light);
        b->TransformPosition(BoneTransform[2], p, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.5f, Light, o);
        b->TransformPosition(BoneTransform[4], p, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.5f, Light, o);
        b->TransformPosition(BoneTransform[6], p, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.5f, Light, o);
        break;
    case 17:
        PrepareWorldObjectPose(*o);
        Vector(Luminosity * 0.4f, Luminosity * 0.7f, Luminosity * 1.f, Light);
        b->TransformPosition(BoneTransform[4], p, Position);
        CreateSprite(BITMAP_LIGHT, Position, 1.f, Light, o);
        b->TransformPosition(BoneTransform[7], p, Position);
        CreateSprite(BITMAP_LIGHT, Position, 1.f, Light, o);
        b->TransformPosition(BoneTransform[10], p, Position);
        CreateSprite(BITMAP_LIGHT, Position, 1.f, Light, o);
        b->TransformPosition(BoneTransform[13], p, Position);
        CreateSprite(BITMAP_LIGHT, Position, 1.f, Light, o);
        break;
    case 39:
        PrepareWorldObjectPose(*o);
        Vector(Luminosity * 0.4f, Luminosity * 0.8f, Luminosity * 1.f, Light);
        Rotation = (float)((int)(WorldTime * 0.1f) % 360);
        b->TransformPosition(BoneTransform[57], p, Position);
        CreateSprite(BITMAP_LIGHTNING + 1, Position, 1.f, Light, o, Rotation);
        CreateSprite(BITMAP_LIGHTNING + 1, Position, 1.f, Light, o, -Rotation);

        Vector(1.f, 1.f, 1.f, Light);

        for (int i = 61; i <= 65; i++)
        {
            b->TransformPosition(BoneTransform[i], p, Position);
            CreateSprite(BITMAP_LIGHT, Position, 1.f, Light, o);
        }
        for (int i = 61; i <= 65; i++)
        {
            for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 32.f))
            {
                const float fraction = emission.FrameFraction();
                PrepareWorldObjectPose(*o, fraction);
                b->TransformPosition(BoneTransform[i], p, Position);
                CreateParticle(BITMAP_SHINY, Position, o->Angle, Light);
                CreateParticle(BITMAP_SHINY, Position, o->Angle, Light, 1);
            }
        }
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 8.f))
        {
            const float fraction = emission.FrameFraction();
            PrepareWorldObjectPose(*o, fraction);
            b->TransformPosition(BoneTransform[58], p, Position);
            vec3_t Angle;

            for (int i = 0; i < 8; i++)
            {
                Vector((float)(WorldRandom() % 60 + 60), 90.f + 50.f, (float)(WorldRandom() % 30),
                       Angle);
                CreateJoint(BITMAP_JOINT_SPARK, Position, Position, Angle);
                CreateParticle(BITMAP_SPARK, Position, Angle, Light);
            }
        }
    }
    return true;
}

void GMNoria::PlayAmbientSounds()
{
    PlayBuffer(SOUND_WIND01, NULL, true);
    if (rand_fps_check(512))
        PlayBuffer(SOUND_FOREST01);
}

bool GMNoria::AllowsAmbientSound(ESound sound) const
{
    return (sound == SOUND_WIND01 || sound == SOUND_FOREST01);
}

void GMNoria::UpdateMusic()
{
    if (Hero->SafeZone)
        PlayMp3(MUSIC_NORIA);
}

bool GMNoria::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_NORIA) == 0;
}

bool GMNoria::CreateWeather(PARTICLE *particle, int index)
{
    return TheMapProcess().Atlans().CreateWeather(particle, index);
}

bool GMNoria::ConfigureAmbientBoid(OBJECT *object, int)
{
    object->Type = MODEL_BUTTERFLY01;
    object->Velocity = 0.3f;
    object->LightEnable = false;
    Vector(1.f, 1.f, 1.f, object->Light);
    return false;
}

bool GMNoria::CanCreateAmbientBoid(int slot, int index)
{
    return true;
}

ESound GMNoria::WalkingSound(int tile, bool safe) const
{
    return tile == 0 ? SOUND_HUMAN_WALK_GRASS : SOUND_HUMAN_WALK_GROUND;
}

bool CGMSantaTown::AdvanceObjectVisual(OBJECT *o, BMD *b, float)
{
    switch (o->Type)
    {
    case 26: {
        float fLumi;
        fLumi = (sinf(WorldTime * 0.005f) + 1.0f) * 0.1f + 0.9f;
        vec3_t vLightFire;
        Vector(fLumi * 0.8f, fLumi * 0.2f, fLumi * 0.0f, vLightFire);
        CreateSprite(BITMAP_LIGHT, o->Position, 8.0f * o->Scale, vLightFire, o);
        return true;
    }
    break;
    case 27: {
        float fLumi;
        fLumi = (sinf(WorldTime * 0.005f) + 1.0f) * 0.1f + 0.9f;
        vec3_t vLightFire;
        Vector(fLumi * 0.0f, fLumi * 0.2f, fLumi * 0.8f, vLightFire);
        CreateSprite(BITMAP_LIGHT, o->Position, 8.0f * o->Scale, vLightFire, o);
        return true;
    }
    break;
    case 28: {
        float fLumi;
        fLumi = (sinf(WorldTime * 0.005f) + 1.0f) * 0.1f + 0.9f;
        vec3_t vLightFire;
        Vector(fLumi * 0.0f, fLumi * 0.6f, fLumi * 0.6f, vLightFire);
        CreateSprite(BITMAP_LIGHT, o->Position, 8.0f * o->Scale, vLightFire, o);
        return true;
    }
    break;
    }

    return false;
}

bool CGMSantaTown::AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                        WorldCharacterVisualState &visual)
{
    // 	vec3_t vPos, vRelative, vLight;
    // 	switch(o->Type)
    // 	{
    // 	}

    return true;
}

bool CGMSantaTown::CreateSnow(PARTICLE *o)
{
    if (IsSantaTown() == false)
        return false;

    o->Type = BITMAP_LEAF1;
    o->Scale = (float)(WorldRandom() % 10 + 5);
    if (rand_fps_check(10))
    {
        o->Type = BITMAP_LEAF2;
        o->Scale = 12.f;
    }
    Vector(Hero->Object.Position[0] + (float)(WorldRandom() % 1600 - 800),
           Hero->Object.Position[1] + (float)(WorldRandom() % 1400 - 500),
           Hero->Object.Position[2] + (float)(WorldRandom() % 200 + 200), o->Position);
    Vector(-20.f, 0.f, 0.f, o->Angle);
    vec3_t Velocity;
    Vector(0.f, 0.f, -(float)(WorldRandom() % 8 + 4), Velocity);
    float Matrix[3][4];
    AngleMatrix(o->Angle, Matrix);
    VectorRotate(Velocity, Matrix, o->Velocity);

    return true;
}

void CGMSantaTown::PlayBGM()
{
    if (IsSantaTown())
    {
        PlayMp3(MUSIC_SANTA_TOWN);
    }
}

void CGMSantaTown::UpdateMusic()
{
    PlayBGM();
}

bool CGMSantaTown::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_SANTA_TOWN) == 0;
}

bool CGMSantaTown::CreateWeather(PARTICLE *particle, int)
{
    return CreateSnow(particle);
}

ESound CGMSantaTown::WalkingSound(int tile, bool safe) const
{
    return SOUND_HUMAN_WALK_SNOW;
}

bool GMStadium::AdvanceObjectVisual(OBJECT *o, BMD *b, float Luminosity)
{
    vec3_t p{}, Position{}, Light{};
    int Bitmap;
    float Scale, Rotation;
    constexpr int StadiumLightObject = 9;
    constexpr int LightAnchorBone = 1;
    if (o->Type == StadiumLightObject)
    {
        PrepareWorldObjectPose(*o);
        Vector(Luminosity * 0.6f, Luminosity * 0.3f, Luminosity * 0.1f, Light);
        b->TransformPosition(BoneTransform[LightAnchorBone], p, Position);
        CreateSprite(BITMAP_LIGHT, Position, Luminosity * 5.f, Light, o);
    }
    return true;
}

void GMStadium::ConfigureAmbientFish(OBJECT *o)
{
    o->Type = MODEL_BUG01;
    o->Velocity = 0.6f / o->Scale;
}

bool GMStadium::CanCreateAmbientFish(int index)
{
    return TerrainWall[index] < TW_NOGROUND;
}

bool GMTarkan::AdvanceObjectVisual(OBJECT *o, BMD *b, float Luminosity)
{
    vec3_t p{}, Position{}, Light{};
    int Bitmap;
    float Scale, Rotation;
    switch (o->Type)
    {
    case 60:
        if (o->HiddenMesh != -2)
        {
            for (int i = 0; i < 20; ++i)
            {
                CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, o->Light, 6, o->Scale);
            }
        }
        o->HiddenMesh = -2;
        break;

    case 63:
        PrepareWorldObjectPose(*o);
        Luminosity = (float)sinf((WorldTime + (o->Angle[2] * 5)) * 0.002f) * 0.3f + 0.7f;

        Scale = Luminosity * 1.5f;
        Vector(Luminosity / 1.7f, Luminosity, Luminosity, Light);
        b->TransformPosition(BoneTransform[2], p, Position);
        CreateSprite(BITMAP_IMPACT, Position, Scale, Light, o);
        break;

    case 64:
        PrepareWorldObjectPose(*o);
        Luminosity = (float)sinf((WorldTime + (o->Angle[2] * 5)) * 0.002f) * 0.3f + 0.7f;

        Scale = Luminosity * 1.5f;
        Vector(Luminosity, Luminosity * 0.32f, Luminosity * 0.32f, Light);
        b->TransformPosition(BoneTransform[2], p, Position);
        CreateSprite(BITMAP_IMPACT, Position, Scale, Light, o);
        break;

    case 70:
        o->HiddenMesh = -2;
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 5.f))
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, o->Light, 7, o->Scale);
        break;

    case 76:
        o->HiddenMesh = -2;
        {
            bool Smoke = false;

            if (((int)WorldTime % 5000) > 4500)
                Smoke = true;
            if (Smoke)
                CreateParticleFpsChecked(BITMAP_SMOKE, o->Position, o->Angle, o->Light, 4,
                                         o->Scale);
        }
        break;
    case 83:
        o->HiddenMesh = -2;
        {
            bool Smoke = false;
            int inter = (int)o->Angle[2] * 10;
            int timing = (int)WorldTime % 10000;

            if (timing > 3500 + inter && timing < 4000 + inter)
                Smoke = true;
            if (Smoke)
            {
                Vector(1.f, 1.f, 1.f, Light);

                CreateParticleFpsChecked(BITMAP_SMOKE, o->Position, o->Angle, o->Light, 8,
                                         o->Scale);
                for (auto emission :
                     sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
                {
                    Position[0] = o->Position[0] + (WorldRandom() % 128 - 64);
                    Position[1] = o->Position[1] + (WorldRandom() % 128 - 64);
                    Position[2] = o->Position[2];
                    CreateParticle(BITMAP_SMOKE, Position, o->Angle, o->Light, 4, o->Scale * 0.5f);
                    CreateEffect(MODEL_STONE1 + WorldRandom() % 2, o->Position, o->Angle, o->Light);
                }
            }
        }
    }
    return true;
}

float GMTarkan::ObjectAnimationSpeed(const OBJECT &object, const BMD &, float speed) const
{
    constexpr int FastAnimatedObject = 8;
    constexpr float FastAnimationMultiplier = 4.f;
    return object.Type == FastAnimatedObject ? speed * FastAnimationMultiplier : speed;
}

void GMTarkan::PlayAmbientSounds()
{
    PlayBuffer(SOUND_DESERT01, NULL, true);
}

bool GMTarkan::AllowsAmbientSound(ESound sound) const
{
    return (sound == SOUND_DESERT01);
}

void GMTarkan::UpdateMusic()
{
    PlayMp3(MUSIC_TARKAN);
}

bool GMTarkan::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_TARKAN) == 0;
}

void GMTarkan::ConfigureAmbientFish(OBJECT *o)
{
    o->Type = MODEL_BUG01 + 1;
    o->Scale = (float)(WorldRandom() % 4 + 8) * 0.1f;
    o->Velocity = 2.5f / o->Scale;
    o->Gravity = 9;
    o->LifeTime = 100;
    VectorCopy(o->Position, o->EyeLeft);
    CreateJointFpsChecked(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 4, o, 30.f);
}

bool GMTarkan::CanCreateAmbientFish(int index)
{
    return TerrainWall[index] == 0 || TerrainWall[index] == TW_CHARACTER;
}

bool GMUnitedMarketPlace::MoveMonsterVisual(CHARACTER *, OBJECT *o, BMD *b,
                                            WorldCharacterVisualState &visual)
{
    if (o == NULL)
        return false;
    if (b == NULL)
        return false;

    if (IsUnitedMarketPlace() == false)
    {
        return false;
    }

    switch (o->Type)
    {
    case MODEL_UNITEDMARKETPLACE_JULIA: {
    }
        return true;
    }

    return false;
}

bool GMUnitedMarketPlace::AdvanceObjectVisual(OBJECT *o, BMD *b, float)
{
    if (IsUnitedMarketPlace() == false)
        return false;

    vec3_t p, Position, Light;
    Vector(0.f, 30.f, 0.f, Position);
    Vector(0.f, 0.f, 0.f, p);
    Vector(0.f, 0.f, 0.f, Light);

    switch (o->Type)
    {
    case 30: {
        PrepareWorldObjectPose(*o);
        for (int i = 1; i <= 3; ++i)
        {
            vec3_t vLightPosition, vRelativePos;
            Vector(0.0f, 0.0f, 0.0f, vRelativePos);
            b->TransformPosition(BoneTransform[i], vRelativePos, vLightPosition, false);

            float fLumi = (sinf(WorldTime * 0.002f) + 1.0f) * 0.2f + 0.6f;
            vec3_t vLightFire;
            Vector(fLumi * 0.5f, fLumi * 0.5f, fLumi * 0.5f, vLightFire);
            CreateSprite(BITMAP_FLARE, vLightPosition, 1.5f * o->Scale, vLightFire, o);
            //CreateSprite(BITMAP_LIGHT, vLightPosition, fLumi/2, vLightFire, o);
        }
    }
        return true;
    case 35: {
        PrepareWorldObjectPose(*o);
        vec3_t vLightPosition, vRelativePos;
        Vector(0.0f, 0.0f, 0.0f, vRelativePos);
        b->TransformPosition(BoneTransform[2], vRelativePos, vLightPosition, false);

        float fLumi = (sinf(WorldTime * 0.039f) + 1.0f) * 0.2f + 0.6f;
        vec3_t vLightFire;
        Vector(fLumi * 0.7f, fLumi * 0.7f, fLumi * 0.7f, vLightFire);
        CreateSprite(BITMAP_FLARE, vLightPosition, 1.5f * o->Scale, vLightFire, o);
    }
        return true;
    case 54: {
        CreateParticleFpsChecked(BITMAP_WATERFALL_5, o->Position, o->Angle, Light, 0);
    }
        return true;
    case 55: {
        Vector(1.f, 1.f, 1.f, Light);
        CreateParticleFpsChecked(BITMAP_WATERFALL_3, o->Position, o->Angle, Light, 8, o->Scale);
    }
        return true;
    case 56: {
        Vector(1.f, 1.f, 1.f, Light);
        if (rand_fps_check(8))
        {
            CreateParticle(BITMAP_WATERFALL_2, o->Position, o->Angle, Light, 4, o->Scale);
        }
    }
        return true;
    case 57: {
        vec3_t vLightFire;
        Vector(1.0f, 0.2f, 0.0f, vLightFire);
        CreateSprite(BITMAP_LIGHT, o->Position, 2.0f * o->Scale, vLightFire, o);

        vec3_t vLight;
        Vector(1.0f, 1.0f, 1.0f, vLight);

        switch (WorldRandom() % 3)
        {
        case 0:
            CreateParticleFpsChecked(BITMAP_FIRE_HIK1, o->Position, o->Angle, vLight, 0, o->Scale);
            break;
        case 1:
            CreateParticleFpsChecked(BITMAP_FIRE_CURSEDLICH, o->Position, o->Angle, vLight, 4,
                                     o->Scale);
            break;
        case 2:
            CreateParticleFpsChecked(BITMAP_FIRE_HIK3, o->Position, o->Angle, vLight, 0, o->Scale);
            break;
        }
    }
        return true;
    case 58: {
        float fLumi;
        fLumi = (sinf(WorldTime * 0.005f) + 1.0f) * 0.3f + 0.5f;
        vec3_t vLightFire;
        Vector(fLumi * 0.4f, fLumi * 0.4f, fLumi * 0.2f, vLightFire);
        CreateSprite(BITMAP_LIGHT, o->Position, 20.0f * o->Scale, vLightFire, o);
    }
        return true;
    }

    return false;
}

bool GMUnitedMarketPlace::AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                               WorldCharacterVisualState &visual)
{
    switch (o->Type)
    {
    case MODEL_UNITEDMARKETPLACE_CHRISTIN: {
    }
        return true;
    case MODEL_UNITEDMARKETPLACE_RAUL: {
        vec3_t vRelativePos, vWorldPos, Light, vLightPosition;
        Vector(0.f, 0.f, 0.f, vRelativePos);

        Vector(0.8f, 0.8f, 0.8f, Light);
        vec3_t vAngle;
        Vector(10.0f, 0.0f, 0.0f, vAngle);
        b->TransformPosition(o->BoneTransform[43], vRelativePos, vWorldPos, true);
        CreateParticleFpsChecked(BITMAP_SMOKELINE1 + WorldRandom() % 3, vWorldPos, o->Angle, Light,
                                 1, 0.6f, o);
        CreateParticleFpsChecked(BITMAP_CLUD64, vWorldPos, o->Angle, Light, 6, 0.6f, o);

        Vector(1.0f, 1.0f, 1.0f, Light);
        Vector(0.0f, 0.0f, 0.0f, vRelativePos);
        b->TransformPosition(o->BoneTransform[43], vRelativePos, vLightPosition, true);
        CreateSprite(BITMAP_FLARE_RED, vLightPosition, 0.24f, Light, o);

        vec3_t v3Angle;
        Vector(0.0f, 0.0f, 0.0f, v3Angle);
        Vector(0.f, 0.f, 0.f, vRelativePos);
        Vector(1.0f, 0.7f, 0.3f, Light);
        b->TransformPosition(o->BoneTransform[79], vRelativePos, vWorldPos, true);
        CreateSprite(BITMAP_LIGHT, vWorldPos, 0.6f, Light, o);
        CreateSprite(BITMAP_LIGHT, vWorldPos, 0.6f, Light, o);

        Vector(0.0f, 0.0f, 0.0f, vRelativePos);
        b->TransformPosition(o->BoneTransform[78], vRelativePos, vLightPosition, true);

        float fLumi = (sinf(WorldTime * 0.030f) + 1.0f) * 0.4f + 0.6f;
        vec3_t vLightFire;
        Vector(fLumi * 1.0f, fLumi * 0.7f, fLumi * 0.3f, vLightFire);
        CreateSprite(BITMAP_FLARE, vLightPosition, 0.5f * o->Scale, vLightFire, o);

        Vector(1.0f, 1.0f, 1.0f, Light);
        Vector(0.0f, 0.0f, 0.0f, vRelativePos);
        b->TransformPosition(o->BoneTransform[77], vRelativePos, vLightPosition, true);
        CreateSprite(BITMAP_FLARE_RED, vLightPosition, 0.5f, Light, o);

        Vector(1.0f, 0.7f, 0.3f, Light);
        Vector(0.0f, 0.0f, 0.0f, vRelativePos);
        b->TransformPosition(o->BoneTransform[76], vRelativePos, vLightPosition, true);
        CreateSprite(BITMAP_FLARE, vLightPosition, 0.4f, Light, o);
    }
        return true;
    }

    return false;
}

bool GMUnitedMarketPlace::PlayMonsterSound(OBJECT *o)
{
    if (IsUnitedMarketPlace() == false)
    {
        return false;
    }

    float fDis_x, fDis_y;
    fDis_x = o->Position[0] - Hero->Object.Position[0];
    fDis_y = o->Position[1] - Hero->Object.Position[1];
    float fDistance = sqrtf(fDis_x * fDis_x + fDis_y * fDis_y);

    if (fDistance > 500.0f)
        return true;

    return false;
}

bool GMUnitedMarketPlace::CreateRain(PARTICLE *o)
{
    if (IsUnitedMarketPlace() == false)
    {
        return false;
    }

    o->Type = BITMAP_RAIN;
    Vector(Hero->Object.Position[0] + (float)(WorldRandom() % 1600 - 800),
           Hero->Object.Position[1] + (float)(WorldRandom() % 1400 - 500),
           Hero->Object.Position[2] + (float)(WorldRandom() % 200 + 200), o->Position);
    Vector(-30.f, 0.f, 0.f, o->Angle);
    vec3_t Velocity;
    Vector(0.f, 0.f, -(float)(WorldRandom() % 24 + 20), Velocity);
    float Matrix[3][4];
    AngleMatrix(o->Angle, Matrix);
    VectorRotate(Velocity, Matrix, o->Velocity);

    //o->Scale = o->Scale * 1.5f;

    return true;
}

bool GMUnitedMarketPlace::MoveRain(PARTICLE *o)
{
    if (IsUnitedMarketPlace() == false)
    {
        return false;
    }

    if (o->Type == BITMAP_RAIN)
    {
        VectorAddScaled(o->Position, o->Velocity, o->Position, FPS_ANIMATION_FACTOR);
        float Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
        if (o->Position[2] < Height)
        {
            o->Live = false;
            o->Position[2] = Height + 10.f;
            if (WorldRandom() % 4 == 0)
                CreateParticle(BITMAP_RAIN_CIRCLE, o->Position, o->Angle, o->Light, 2);
            else
                CreateParticle(BITMAP_RAIN_CIRCLE + 1, o->Position, o->Angle, o->Light, 2);
        }
    }
    else
    {
        MoveAirLeaf(o, true, true);

        vec3_t Range;
        VectorSubtract(o->StartPosition, o->Position, Range);
        float Length = Range[0] * Range[0] + Range[1] * Range[1] + Range[2] * Range[2];
        if (Length >= 200000.f)
            o->Live = false;
    }

    return true;
}

void GMUnitedMarketPlace::PlayAmbientSounds()
{
    {
        PlayBuffer(SOUND_WIND01, NULL, true);
        PlayBuffer(SOUND_RAIN01, NULL, true);
    }
}

bool GMUnitedMarketPlace::AllowsAmbientSound(ESound sound) const
{
    return (sound == SOUND_WIND01 || sound == SOUND_RAIN01);
}

bool GMUnitedMarketPlace::CreateWeather(PARTICLE *particle, int)
{
    return CreateRain(particle);
}

bool GMUnitedMarketPlace::MoveWeather(PARTICLE *particle)
{
    return MoveRain(particle);
}

void GMUnknownWorld::UpdateMusic()
{
    PlayMp3(MUSIC_DUNGEON);
}

bool GMUnknownWorld::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_DUNGEON) == 0;
}

bool GMKanturu1st::AdvanceKanturu1stObjectVisual(OBJECT *pObject, BMD *pModel)
{
    if (!(IsKanturu1st() || gmArea_.IsGmArea()))
        return false;

    vec3_t Light, p, Position;

    switch (pObject->Type)
    {
    case 37: {
        int time = static_cast<DWORD>(WorldSimulationTime()) % 1024;
        if (time >= 0 && time < 10)
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateEffect(MODEL_BUTTERFLY01, pObject->Position, pObject->Angle, Light, 3, pObject);
        }
        pObject->HiddenMesh = -2; // Hide Object
    }
    break;
    case 59:
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, pObject->Position, pObject->Angle, Light, 21,
                           pObject->Scale);
        }
        break;
    case 61:
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_TRUE_FIRE, pObject->Position, pObject->Angle, Light, 0,
                           pObject->Scale);
        }
        break;
    case 62:
        if (pObject->HiddenMesh != -2)
        {
            Vector(0.04f, 0.04f, 0.04f, Light);
            for (int i = 0; i < 20; ++i)
                CreateParticleFpsChecked(BITMAP_CLOUD, pObject->Position, pObject->Angle, Light, 20,
                                         pObject->Scale, pObject);
        }
        break;
    case 70:
        PrepareWorldObjectPose(*pObject);
        float Luminosity;
        Vector(0.0f, 0.0f, 0.0f, p);
        pModel->TransformPosition(BoneTransform[6], p, Position, false);
        Luminosity = (float)sinf(WorldTime * 0.002f) + 1.8f;
        Vector(0.8f, 0.4f, 0.2f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, Luminosity * 7.0f, Light, pObject);
        Vector(0.65f, 0.65f, 0.65f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, Luminosity * 4.0f, Light, pObject);
        break;
    case 81:
        Vector(1.f, 1.f, 1.f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
            CreateParticle(BITMAP_WATERFALL_1, pObject->Position, pObject->Angle, Light, 2,
                           pObject->Scale);
        break;
    case 82:
        Vector(1.f, 1.f, 1.f, Light);
        CreateParticleFpsChecked(BITMAP_WATERFALL_3, pObject->Position, pObject->Angle, Light, 4,
                                 pObject->Scale);
        break;
    case 83:
        Vector(1.f, 1.f, 1.f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
            CreateParticle(BITMAP_WATERFALL_2, pObject->Position, pObject->Angle, Light, 2,
                           pObject->Scale);
        break;
    case 92:
        sessionKeeper_.Visual()->EmitBoneLightning(*pObject, *pModel, 18);
        break;
    case 98:
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            const float fraction = birth.FrameFraction();
            PrepareWorldObjectPose(*pObject, fraction);
            vec3_t vPos;
            Vector(0.0f, 0.0f, 0.0f, vPos);
            pModel->TransformPosition(BoneTransform[1], vPos, Position, false);
            Vector(0.5f, 0.6f, 0.1f, Light);
            CreateParticle(BITMAP_TWINTAIL_WATER, Position, pObject->Angle, Light, 2);
        }
        break;
    case 105:
        sessionKeeper_.Visual()->AdvanceEnergyNode(*pObject, *pModel);
        break;
    case 107:
        if (pObject->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.06f, 0.06f, 0.06f, Light);
            for (int i = 0; i < 20; ++i)
                CreateParticleFpsChecked(BITMAP_CLOUD, pObject->Position, pObject->Angle, Light, 2,
                                         pObject->Scale, pObject);
        }
        break;
    case 108:
        if (pObject->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.2f, 0.2f, 0.2f, Light);
            for (int i = 0; i < 20; ++i)
                CreateParticleFpsChecked(BITMAP_CLOUD, pObject->Position, pObject->Angle, Light, 7,
                                         pObject->Scale, pObject);
        }
        break;
    case 110: {
        PrepareWorldObjectPose(*pObject);
        float fLumi = (sinf(WorldTime * 0.002f) + 1.5f) * 0.5f;
        Vector(fLumi * 0.6f, fLumi * 1.0f, fLumi * 0.8f, Light);
        vec3_t vPos;
        Vector(0.0f, 0.0f, 0.0f, vPos);
        pModel->TransformPosition(BoneTransform[1], vPos, Position, false);
        CreateSprite(BITMAP_LIGHT, Position, 1.1f, Light, pObject);
    }
    break;
    }

    return true;
}

bool GMKanturu1st::AttackEffectKanturu1stMonster(CHARACTER *c, OBJECT *o, BMD *b)
{
    if (!IsKanturu1st())
        return false;

    switch (o->Type)
    {
    case MODEL_BERSERK: {
        return true;
    }
    break;
    case MODEL_GIGANTIS: {
        return true;
    }
    break;
    case MODEL_GENOCIDER: {
        return true;
    }
    break;
    case MODEL_SPLINTER_WOLF: {
        return true;
    }
    break;
    case MODEL_IRON_RIDER: {
        if (o->CurrentAction == MONSTER01_ATTACK1 || o->CurrentAction == MONSTER01_ATTACK2)
        {
            vec3_t vPos, vRelative;
            vec3_t vLight = {0.8f, 1.0f, 0.8f};
            Vector(0.f, 0.f, 0.f, vRelative);
            vRelative[0] = (float)(4 - WorldRandom() % 5);
            vRelative[1] = (float)(4 - WorldRandom() % 5);
            vRelative[2] = (float)(4 - WorldRandom() % 5);
            GetBonePosition(o, CharacterSocket::IRON_RIDER_BOW_6, vRelative, vPos);
            CreateParticleFpsChecked(BITMAP_SPARK + 1, vPos, o->Angle, vLight, 10, 4.0f);
        }

        if (c->CheckAttackTime(10))
        {
            vec3_t vPos, vRelative;
            Vector(0.f, 0.f, 0.f, vRelative);
            GetBonePosition(o, CharacterSocket::IRON_RIDER_BOW_6, vRelative, vPos);
            CreateEffect(MODEL_IRON_RIDER_ARROW, vPos, o->Angle, o->Light, 0);
            c->SetLastAttackEffectTime();
        }

        return true;
    }
    break;
    case MODEL_BLADE_HUNTER: {
        if (o->CurrentAction == MONSTER01_ATTACK1 || o->CurrentAction == MONSTER01_ATTACK2)
        {
            if (c->CheckAttackTime(14))
            //				if(o->AnimationFrame >= StartAction && o->AnimationFrame < (StartAction + fActionSpeed))
            {
                vec3_t vPos, vRelative, Light;
                Vector(140.f, 0.f, -30.f, vRelative);
                Vector(0.2f, 0.2f, 1.f, Light);
                GetBonePosition(o, CharacterSocket::BLADE_L_HAND, vRelative, vPos);

                CreateEffect(MODEL_BLADE_SKILL, vPos, o->Angle, Light, 0);

                CreateParticle(BITMAP_EXPLOTION + 1, vPos, o->Angle, o->Light, 0, 1.3f);
                CreateParticle(BITMAP_EXPLOTION + 1, vPos, o->Angle, o->Light, 0, 2.3f);
                CreateParticle(BITMAP_EXPLOTION + 1, vPos, o->Angle, o->Light, 0, 1.8f);
                CreateParticle(BITMAP_EXPLOTION + 1, vPos, o->Angle, o->Light, 0, 1.3f);
                CreateParticle(BITMAP_EXPLOTION + 1, vPos, o->Angle, o->Light, 0, 2.3f);
                CreateParticle(BITMAP_EXPLOTION + 1, vPos, o->Angle, o->Light, 0, 1.8f);
                c->SetLastAttackEffectTime();
            }
        }

        return true;
    }
    break;
    case MODEL_KENTAUROS: {
        if (o->CurrentAction == MONSTER01_ATTACK1 || o->CurrentAction == MONSTER01_ATTACK2)
        {
            vec3_t vLight = {0.3f, 0.5f, 0.7f};
            vec3_t vPos, vRelative;
            Vector(0.f, 0.f, 0.f, vRelative);
            int index = 31;
            float Width = 2.f;
            GetBonePosition(o, CharacterSocket::KENTAUROS_BIP_23, vRelative, vPos);
            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, index, Width);
            GetBonePosition(o, CharacterSocket::KENTAUROS_BIP_24, vRelative, vPos);
            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, index, Width);
            GetBonePosition(o, CharacterSocket::KENTAUROS_BIP_25, vRelative, vPos);
            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, index, Width);
            GetBonePosition(o, CharacterSocket::KENTAUROS_BIP_26, vRelative, vPos);
            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, index, Width);
            GetBonePosition(o, CharacterSocket::KENTAUROS_BIP_18, vRelative, vPos);
            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, index, Width);
            GetBonePosition(o, CharacterSocket::KENTAUROS_BIP_19, vRelative, vPos);
            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, index, Width);
            GetBonePosition(o, CharacterSocket::KENTAUROS_BIP_20, vRelative, vPos);
            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, index, Width);
            GetBonePosition(o, CharacterSocket::KENTAUROS_BIP_21, vRelative, vPos);
            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, index, Width);
        }
        if (c->CheckAttackTime(14))
        {
            vec3_t vPos, vRelative;

            if (o->CurrentAction == MONSTER01_ATTACK1)
            {
                Vector(30.f, -30.f, 0.f, vRelative);
                GetBonePosition(o, CharacterSocket::KENTAUROS_BIP_25, vRelative, vPos);
                CreateEffect(MODEL_KENTAUROS_ARROW, vPos, o->Angle, o->Light, 0);
            }
            else
            {
                Vector(60.f, -30.f, 50.f, vRelative);
                GetBonePosition(o, CharacterSocket::KENTAUROS_BIP_23, vRelative, vPos);
                CreateEffect(MODEL_KENTAUROS_ARROW, vPos, o->Angle, o->Light, 0);
                o->Angle[2] += 15.f;
                CreateEffect(MODEL_KENTAUROS_ARROW, vPos, o->Angle, o->Light, 0);
                o->Angle[2] -= 30.f;
                CreateEffect(MODEL_KENTAUROS_ARROW, vPos, o->Angle, o->Light, 0);
                o->Angle[2] += 15.f;
            }

            c->SetLastAttackEffectTime();
        }

        return true;
    }
    break;
    case MODEL_BERSERKER_WARRIOR: {
        return true;
    }
    break;
    case MODEL_KENTAUROS_WARRIOR: {
        if (o->CurrentAction == MONSTER01_ATTACK1 || o->CurrentAction == MONSTER01_ATTACK2)
        {
            vec3_t vLight = {0.3f, 0.5f, 0.7f};
            vec3_t vPos, vRelative;
            Vector(0.f, 0.f, 0.f, vRelative);
            int index = 31;
            float Width = 2.f;
            GetBonePosition(o, CharacterSocket::KENTAUROS_BIP_23, vRelative, vPos);
            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, index, Width);
            GetBonePosition(o, CharacterSocket::KENTAUROS_BIP_24, vRelative, vPos);
            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, index, Width);
            GetBonePosition(o, CharacterSocket::KENTAUROS_BIP_25, vRelative, vPos);
            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, index, Width);
            GetBonePosition(o, CharacterSocket::KENTAUROS_BIP_26, vRelative, vPos);
            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, index, Width);
            GetBonePosition(o, CharacterSocket::KENTAUROS_BIP_18, vRelative, vPos);
            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, index, Width);
            GetBonePosition(o, CharacterSocket::KENTAUROS_BIP_19, vRelative, vPos);
            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, index, Width);
            GetBonePosition(o, CharacterSocket::KENTAUROS_BIP_20, vRelative, vPos);
            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, index, Width);
            GetBonePosition(o, CharacterSocket::KENTAUROS_BIP_21, vRelative, vPos);
            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, index, Width);
        }
        if (c->CheckAttackTime(14))
        {
            vec3_t vPos, vRelative;

            if (o->CurrentAction == MONSTER01_ATTACK1)
            {
                Vector(30.f, -30.f, 0.f, vRelative);
                GetBonePosition(o, CharacterSocket::KENTAUROS_BIP_25, vRelative, vPos);
                CreateEffect(MODEL_KENTAUROS_ARROW, vPos, o->Angle, o->Light, 0);
            }
            else
            {
                Vector(60.f, -30.f, 50.f, vRelative);
                GetBonePosition(o, CharacterSocket::KENTAUROS_BIP_23, vRelative, vPos);
                CreateEffect(MODEL_KENTAUROS_ARROW, vPos, o->Angle, o->Light, 0);
                o->Angle[2] += 15.f;
                CreateEffect(MODEL_KENTAUROS_ARROW, vPos, o->Angle, o->Light, 0);
                o->Angle[2] -= 30.f;
                CreateEffect(MODEL_KENTAUROS_ARROW, vPos, o->Angle, o->Light, 0);
                o->Angle[2] += 15.f;
            }

            c->SetLastAttackEffectTime();
        }

        return true;
    }
    break;
    case MODEL_GIGANTIS_WARRIOR: {
        return true;
    }
    break;
    case MODEL_SOCCERBALL: {
        return true;
    }
    break;
    }

    return false;
}

bool GMKanturu1st::MoveKanturu1stMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                               WorldCharacterVisualState &visual)
{
    switch (o->Type)
    {
    case MODEL_BERSERK:
        break;
    case MODEL_GIGANTIS:
        break;
    case MODEL_GENOCIDER:
        break;
    case MODEL_KENTAUROS:
        break;
    case MODEL_BERSERKER_WARRIOR: {
    }
    break;
    case MODEL_KENTAUROS_WARRIOR: {
    }
    break;
    case MODEL_GIGANTIS_WARRIOR: {
    }
    break;
    case MODEL_SOCCERBALL: {
    }
    break;
    }

    return false;
}

void GMKanturu1st::EmitMonsterActionSounds(OBJECT &object)
{
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    int attack, death;
    switch (object.Type)
    {
    case MODEL_BERSERK:
    case MODEL_BERSERKER_WARRIOR:
        attack = SOUND_KANTURU_1ST_BER_ATTACK1;
        death = SOUND_KANTURU_1ST_BER_DIE;
        break;
    case MODEL_GIGANTIS:
    case MODEL_GIGANTIS_WARRIOR:
        attack = SOUND_KANTURU_1ST_GIGAN_ATTACK1;
        death = SOUND_KANTURU_1ST_GIGAN_DIE;
        break;
    case MODEL_GENOCIDER:
    case MODEL_SOCCERBALL:
        attack = SOUND_KANTURU_1ST_GENO_ATTACK1;
        death = SOUND_KANTURU_1ST_GENO_DIE;
        break;
    case MODEL_SPLINTER_WOLF:
        attack = SOUND_KANTURU_1ST_SWOLF_ATTACK1;
        death = SOUND_KANTURU_1ST_SWOLF_DIE;
        break;
    case MODEL_IRON_RIDER:
        attack = SOUND_KANTURU_1ST_IR_ATTACK1;
        death = SOUND_KANTURU_1ST_IR_DIE;
        break;
    case MODEL_SATYROS:
        attack = SOUND_KANTURU_1ST_SATI_ATTACK1;
        death = SOUND_KANTURU_1ST_SATI_DIE;
        break;
    case MODEL_BLADE_HUNTER:
        attack = SOUND_KANTURU_1ST_BLADE_ATTACK1;
        death = SOUND_KANTURU_1ST_BLADE_DIE;
        break;
    case MODEL_KENTAUROS:
    case MODEL_KENTAUROS_WARRIOR:
        attack = SOUND_KANTURU_1ST_KENTA_ATTACK1;
        death = SOUND_KANTURU_1ST_KENTA_DIE;
        break;
    default:
        return;
    }
    constexpr std::array<std::pair<int, float>, 3> markers{
        {{MONSTER01_ATTACK1, 0.f}, {MONSTER01_ATTACK2, 0.f}, {MONSTER01_DIE, 0.f}}};
    object.MotionTrace.VisitAnimationEvents(WorldTime, markers, [&](std::size_t event, float) {
        PlayBuffer(static_cast<ESound>(event == 2 ? death : attack + WorldRandom() % 2));
    });
}

bool GMKanturu1st::AdvanceKanturu1stMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                                  WorldCharacterVisualState &visual)
{
    vec3_t emissionLight;
    VectorCopy(o->Light, emissionLight);

    EmitMonsterActionSounds(*o);

    switch (o->Type)
    {
    case MODEL_BERSERK: {
        if (visual.action == MONSTER01_WALK)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_KANTURU_1ST_BER_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
        {
        }
        else if (visual.action == MONSTER01_DIE)
        {
        }

        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;

        if (visual.action == MONSTER01_DIE || visual.action == MONSTER01_ATTACK1 ||
            visual.action == MONSTER01_ATTACK2)
            return true;

        vec3_t vPos, vRelative;
        Vector(0.f, 0.f, 0.f, vRelative);
        GetBonePosition(o, CharacterSocket::BERSERK_MOUTH, vRelative, vPos);
        CreateParticle(BITMAP_SMOKE, vPos, o->Angle, emissionLight, 42, o->Scale);

        return true;
    }
    break;

    case MODEL_GIGANTIS: {
        if (visual.action == MONSTER01_WALK)
        {
            if (rand_fps_check(15))
                PlayBuffer(SOUND_KANTURU_1ST_GIGAN_MOVE1);
        }
        else if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
        {
        }
        else if (visual.action == MONSTER01_DIE)
        {
        }

        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;

        if (visual.action == MONSTER01_DIE)
            return true;

        vec3_t EndRelative, EndPos;
        Vector(19.f, -2.f, 0.f, EndRelative);

        b->TransformPosition(o->BoneTransform[7], EndRelative, EndPos, true);

        Vector(0.4f, 0.6f, 0.8f, emissionLight);
        CreateSprite(BITMAP_LIGHT, EndPos, 3.0f, emissionLight, o, 0.5f);

        float Luminosity;
        Luminosity = sinf(WorldTime * 0.05f) * 0.4f + 0.9f;
        Vector(Luminosity * 0.3f, Luminosity * 0.5f, Luminosity * 0.8f, emissionLight);
        CreateSprite(BITMAP_LIGHT, EndPos, 1.0f, emissionLight, o);

        return true;
    }
    break;

    case MODEL_GENOCIDER: {
        if (visual.action == MONSTER01_WALK)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_KANTURU_1ST_GENO_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
        {
        }
        else if (visual.action == MONSTER01_DIE)
        {
        }

        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;

        if (visual.action == MONSTER01_WALK)
        {
            sessionKeeper_.Visual()->EmitGenociderDust(*o, *b, emissionLight);
        }
        return true;
    }
    break;
    case MODEL_SPLINTER_WOLF: {
        MoveEye(o, b, 16, 17);

        if (visual.action == MONSTER01_WALK)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_KANTURU_1ST_SWOLF_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
        {
        }
        else if (visual.action == MONSTER01_DIE)
        {
        }
        return true;
    }
    break;
    case MODEL_IRON_RIDER: {
        if (visual.action != MONSTER01_DIE)
        {
            vec3_t vPos, vRelative;
            vec3_t vLight = {0.8f, 1.0f, 0.8f};
            Vector(0.f, 0.f, 0.f, vRelative);
            vRelative[0] = (float)(4 - WorldRandom() % 5);
            vRelative[1] = (float)(4 - WorldRandom() % 5);
            vRelative[2] = (float)(4 - WorldRandom() % 5);
            GetBonePosition(o, CharacterSocket::IRON_RIDER_BOW_15, vRelative, vPos);
            CreateParticleFpsChecked(BITMAP_SPARK + 1, vPos, o->Angle, vLight, 10, 4.0f);
            GetBonePosition(o, CharacterSocket::IRON_RIDER_BOW_16, vRelative, vPos);
            CreateParticleFpsChecked(BITMAP_SPARK + 1, vPos, o->Angle, vLight, 10, 4.0f);

            if (visual.action == MONSTER01_WALK)
            {
                if (rand_fps_check(15))
                    PlayBuffer(static_cast<ESound>(SOUND_KANTURU_1ST_IR_MOVE1 + WorldRandom() % 2));
            }
            else if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
            {
            }
        }
        else
        {
            vec3_t vLight = {1.0f, 1.0f, 1.0f};
            vec3_t vPos, vRelative;
            Vector(0.f, 0.f, 0.f, vRelative);
            GetBonePosition(o, CharacterSocket::IRON_RIDER_BIP01, vRelative, vPos);
            CreateParticleFpsChecked(BITMAP_SMOKE + 3, vPos, o->Angle, vLight, 3, 2.0f);
            CreateParticleFpsChecked(BITMAP_SMOKE + 3, vPos, o->Angle, vLight, 4, 1.0f);
        }
        return true;
    }
    break;
    case MODEL_SATYROS: {
        if (visual.action == MONSTER01_WALK)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_KANTURU_1ST_SATI_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
        {
        }
        else if (visual.action == MONSTER01_DIE)
        {
        }
    }
    break;
    case MODEL_BLADE_HUNTER: {

        if (visual.action == MONSTER01_WALK)
        {
            if (rand_fps_check(10))
            {
                if (gMapManager.ContextMap() != WD_39KANTURU_3RD)
                    CreateParticle(BITMAP_SMOKE + 1, o->Position, o->Angle, emissionLight);

                PlayBuffer(static_cast<ESound>(SOUND_KANTURU_1ST_BLADE_MOVE1 + WorldRandom() % 2));
            }
        }
        else if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
        {
        }
        else if (visual.action == MONSTER01_DIE)
        {
        }
    }
    break;
    case MODEL_KENTAUROS: {
        if (visual.action == MONSTER01_WALK)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_KANTURU_1ST_KENTA_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
        {
        }
        else if (visual.action == MONSTER01_DIE)
        {
        }
        vec3_t vPos, vRelative;
        Vector(0.f, 0.f, 0.f, vRelative);
        if (visual.action == MONSTER01_DIE)
        {
            sessionKeeper_.Visual()->EmitKentaurosDeathSmoke(*o, *b, emissionLight);
        }
        else if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.f))
            {
                vec3_t position;
                o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, position);
                CreateParticle(BITMAP_SMOKE + 1, position, o->Angle, emissionLight);
            }
        }
    }
    break;
    case MODEL_BERSERKER_WARRIOR: {
        if (visual.action == MONSTER01_WALK)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_KANTURU_1ST_BER_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
        {
        }
        else if (visual.action == MONSTER01_DIE)
        {
        }

        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;

        if (visual.action == MONSTER01_DIE || visual.action == MONSTER01_ATTACK1 ||
            visual.action == MONSTER01_ATTACK2)
            return true;

        vec3_t vPos, vRelative;
        Vector(0.f, 0.f, 0.f, vRelative);
        GetBonePosition(o, CharacterSocket::BERSERK_MOUTH, vRelative, vPos);
        CreateParticleFpsChecked(BITMAP_SMOKE, vPos, o->Angle, emissionLight, 42, o->Scale);
    }
        return true;
    case MODEL_KENTAUROS_WARRIOR: {
        if (visual.action == MONSTER01_WALK)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_KANTURU_1ST_KENTA_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
        {
        }
        else if (visual.action == MONSTER01_DIE)
        {
        }
        vec3_t vPos, vRelative;
        Vector(0.f, 0.f, 0.f, vRelative);
        if (visual.action == MONSTER01_DIE)
        {
            sessionKeeper_.Visual()->EmitKentaurosDeathSmoke(*o, *b, emissionLight);
        }
        else
        {
            if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
            {
                for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.f))
                {
                    vec3_t position;
                    o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, position);
                    CreateParticle(BITMAP_SMOKE + 1, position, o->Angle, emissionLight);
                }
            }
        }
    }
        return true;
    case MODEL_GIGANTIS_WARRIOR: {
        if (visual.action == MONSTER01_WALK)
        {
            if (rand_fps_check(15))
                PlayBuffer(SOUND_KANTURU_1ST_GIGAN_MOVE1);
        }
        else if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
        {
        }
        else if (visual.action == MONSTER01_DIE)
        {
        }

        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;

        if (visual.action == MONSTER01_DIE)
            return true;

        vec3_t EndRelative, EndPos;
        Vector(19.f, -2.f, 0.f, EndRelative);

        b->TransformPosition(o->BoneTransform[7], EndRelative, EndPos, true);

        Vector(0.4f, 0.6f, 0.8f, emissionLight);
        CreateSprite(BITMAP_LIGHT, EndPos, 3.0f, emissionLight, o, 0.5f);

        float Luminosity;
        Luminosity = sinf(WorldTime * 0.05f) * 0.4f + 0.9f;
        Vector(Luminosity * 0.3f, Luminosity * 0.5f, Luminosity * 0.8f, emissionLight);
        CreateSprite(BITMAP_LIGHT, EndPos, 1.0f, emissionLight, o);
    }
        return true;
    case MODEL_SOCCERBALL: {
        if (visual.action == MONSTER01_WALK)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_KANTURU_1ST_GENO_MOVE1 + WorldRandom() % 2));
        }
        else if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
        {
        }
        else if (visual.action == MONSTER01_DIE)
        {
        }

        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;

        if (visual.action == MONSTER01_WALK)
        {
            sessionKeeper_.Visual()->EmitGenociderDust(*o, *b, emissionLight);
        }
    }
        return true;
    }
    return false;
}

void GMKanturu1st::MoveKanturu1stBlurEffect(CHARACTER *c, OBJECT *o, BMD *b)
{
    switch (o->Type)
    {
    case MODEL_SATYROS: {
        if ((o->AnimationFrame >= 3.6f && o->AnimationFrame <= 6.0f &&
             o->CurrentAction == MONSTER01_ATTACK1) ||
            (o->AnimationFrame >= 3.6f && o->AnimationFrame <= 6.0f &&
             o->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t Light;
            Vector(0.3f, 1.0f, 3.5f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, -200.f, EndRelative);

                b->TransformPosition(BoneTransform[44], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[44], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 1);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_BERSERK: {
        if ((o->AnimationFrame >= 3.5f && o->AnimationFrame <= 6.7f &&
             o->CurrentAction == MONSTER01_ATTACK1) ||
            (o->AnimationFrame >= 3.5f && o->AnimationFrame <= 6.7f &&
             o->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t Light;
            Vector(0.3f, 2.0f, 0.5f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(-20.f, 0.f, 0.f, StartRelative);
                Vector(100.f, 0.f, 0.f, EndRelative);

                b->TransformPosition(BoneTransform[33], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[34], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 1);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_BLADE_HUNTER: {
        float Start_Frame = 5.9f;
        float End_Frame = 7.55f;
        if ((o->AnimationFrame >= Start_Frame && o->AnimationFrame <= End_Frame &&
             o->CurrentAction == MONSTER01_ATTACK1) ||
            (o->AnimationFrame >= Start_Frame && o->AnimationFrame <= End_Frame &&
             o->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t Light;
            Vector(1.0f, 1.2f, 2.f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);

                b->TransformPosition(BoneTransform[55], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[54], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 0);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_GIGANTIS: {
        if ((o->AnimationFrame >= 3.5f && o->AnimationFrame <= 5.9f &&
             o->CurrentAction == MONSTER01_ATTACK1) ||
            (o->AnimationFrame >= 3.5f && o->AnimationFrame <= 5.9f &&
             o->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t Light;
            Vector(1.0f, 1.2f, 2.f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);

                b->TransformPosition(BoneTransform[34], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[33], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 0);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_GENOCIDER: {
        if ((o->AnimationFrame >= 5.5f && o->AnimationFrame <= 6.9f &&
             o->CurrentAction == MONSTER01_ATTACK1) ||
            (o->AnimationFrame >= 5.5f && o->AnimationFrame <= 6.9f &&
             o->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t Light;
            Vector(1.0f, 0.2f, 0.0f, Light);

            //				vec3_t vPos, vRelative;
            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 18; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(-40.f, 0.f, 0.f, StartRelative);
                Vector(10.f, 0.f, 0.f, EndRelative);

                b->TransformPosition(BoneTransform[49], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[49], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 0, true, 0);

                b->TransformPosition(BoneTransform[51], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[51], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 0, true, 1);

                b->TransformPosition(BoneTransform[50], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[50], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 0, true, 2);

                b->TransformPosition(BoneTransform[52], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[52], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 0, true, 3);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_BERSERKER_WARRIOR: {
        if ((o->AnimationFrame >= 3.5f && o->AnimationFrame <= 6.7f &&
             o->CurrentAction == MONSTER01_ATTACK1) ||
            (o->AnimationFrame >= 3.5f && o->AnimationFrame <= 6.7f &&
             o->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t Light;
            Vector(0.3f, 2.0f, 0.5f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(-20.f, 0.f, 0.f, StartRelative);
                Vector(100.f, 0.f, 0.f, EndRelative);

                b->TransformPosition(BoneTransform[33], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[34], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 1);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_KENTAUROS_WARRIOR: {
        if ((o->AnimationFrame >= 3.5f && o->AnimationFrame <= 5.9f &&
             o->CurrentAction == MONSTER01_ATTACK1) ||
            (o->AnimationFrame >= 3.5f && o->AnimationFrame <= 5.9f &&
             o->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t Light;
            Vector(1.0f, 1.2f, 2.f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);

                b->TransformPosition(BoneTransform[34], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[33], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 0);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_GIGANTIS_WARRIOR: {
        if ((o->AnimationFrame >= 5.5f && o->AnimationFrame <= 6.9f &&
             o->CurrentAction == MONSTER01_ATTACK1) ||
            (o->AnimationFrame >= 5.5f && o->AnimationFrame <= 6.9f &&
             o->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t Light;
            Vector(1.0f, 0.2f, 0.0f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 18; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(-40.f, 0.f, 0.f, StartRelative);
                Vector(10.f, 0.f, 0.f, EndRelative);

                b->TransformPosition(BoneTransform[49], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[49], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 0, true, 0);

                b->TransformPosition(BoneTransform[51], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[51], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 0, true, 1);

                b->TransformPosition(BoneTransform[50], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[50], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 0, true, 2);

                b->TransformPosition(BoneTransform[52], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[52], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 0, true, 3);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    }
}

bool GMKanturu1st::AdvanceObjectVisual(OBJECT *object, BMD *model, float)
{
    return AdvanceKanturu1stObjectVisual(object, model);
}

bool GMKanturu1st::AttackEffectBeforeShared(CHARACTER *character, OBJECT *object, BMD *model)
{
    return AttackEffectKanturu1stMonster(character, object, model);
}

void GMKanturu1st::UpdateMusic()
{
    PlayMp3(MUSIC_KANTURU_1ST);
}

bool GMKanturu1st::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_KANTURU_1ST) == 0;
}

bool GMKanturu2nd::AttackEffect_Kanturu2nd_Monster(CHARACTER *c, OBJECT *o, BMD *b)
{
    switch (o->Type)
    {
    case MODEL_PERSONA: {
        return true;
    }
    break;
    case MODEL_TWIN_TAIL: {
        return true;
    }
    break;
    case MODEL_DREADFEAR: {
        return true;
    }
    break;
    case MODEL_TRAP_CANON: {
        trapCanon_.EmitAttackEffect(c, o, b);
        return true;
    }
    break;
    }

    return false;
}

void CTrapCanon::EmitAttackEffect(CHARACTER *c, OBJECT *o, BMD *b)
{
    if (c->CheckAttackTime(1))
    {
        CHARACTER *tc = &CharactersClient[c->TargetCharacter];
        OBJECT *to = &tc->Object;
        vec3_t vPos, vPos2;
        VectorCopy(o->Position, vPos);
        VectorCopy(to->Position, vPos2);
        vPos[2] += 85.f;
        CreateJoint(BITMAP_JOINT_ENERGY, vPos, vPos2, to->Angle, 43, to, 30.0f);
        c->SetLastAttackEffectTime();
    }
}

bool GMKanturu2nd::Move_Kanturu2nd_Object(OBJECT *o)
{
    if (!Is_Kanturu2nd())
        return false;

    Sound_Kanturu2nd_Object(o);

    switch (o->Type)
    {
    case 10: {
        o->Velocity = 0.04f;
        o->BlendMeshLight = (float)sinf(WorldTime * 0.0015f) + 1.0f;

        if (o->BlendMeshLight <= 0.1f)
        {
            o->BlendMeshLight = 0.1f;
        }
        else if (o->BlendMeshLight >= 0.9f)
        {
            o->BlendMeshLight = 0.9f;
        }
    }
    break;
    case 38: {
        o->BlendMeshLight = (float)sinf(WorldTime * 0.0010f) + 1.0f;
    }
    break;
    case 42: {
        o->BlendMeshTexCoordU = -(int)WorldTime % 10000 * 0.0002f;
        o->BlendMeshTexCoordV = -(int)WorldTime % 10000 * 0.0002f;
    }
    break;
    case 44: {
        o->Velocity = 0.02f;
    }
    break;
    case 45:
    case 46:
    case 47:
    case 48:
    case 49:
    case 50:
    case 51:
    case 52:
    case 53:
    case 54:
    case 55:
    case 56:
    case 65: {
        o->HiddenMesh = -2;
    }
    break;
    }

    return true;
}

void GMKanturu2nd::EmitMonsterEvents(OBJECT &object, BMD &model, WorldCharacterVisualState &visual)
{
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    constexpr std::array<std::pair<int, float>, 4> markers{{{MONSTER01_ATTACK1, 0.f},
                                                            {MONSTER01_ATTACK2, 0.f},
                                                            {MONSTER01_ATTACK2, 5.5f},
                                                            {MONSTER01_DIE, 0.f}}};
    constexpr std::array<std::pair<int, float>, 2> npc{
        {{KANTURU2ND_NPC_ANI_ROT, 42.f}, {KANTURU2ND_NPC_ANI_ROT, 50.f}}};
    if (object.Type == MODEL_KANTURU2ND_ENTER_NPC)
    {
        object.MotionTrace.VisitAnimationEvents(WorldTime, npc, [&](std::size_t event, float) {
            if (event == 0 && !g_pKanturu2ndEnterNpc->IsEnterRequest() && Hero->Dead == 0)
                g_pKanturu2ndEnterNpc->SendRequestKanturu3rdEnter();
            if (event == 1)
            {
                g_pKanturu2ndEnterNpc->SetNpcAnimation(false);
                g_pKanturu2ndEnterNpc->SetEnterRequest(false);
            }
        });
        return;
    }
    std::array<ESound, 3> sounds;
    switch (object.Type)
    {
    case MODEL_PERSONA:
        sounds = {SOUND_KANTURU_2ND_PERSO_ATTACK1, SOUND_KANTURU_2ND_PERSO_ATTACK2,
                  SOUND_KANTURU_2ND_PERSO_DIE};
        break;
    case MODEL_TWIN_TAIL:
        sounds = {SOUND_KANTURU_2ND_TWIN_ATTACK1, SOUND_KANTURU_2ND_TWIN_ATTACK2,
                  SOUND_KANTURU_2ND_TWIN_DIE};
        break;
    case MODEL_DREADFEAR:
        sounds = {SOUND_KANTURU_2ND_DRED_ATTACK1, SOUND_KANTURU_2ND_DRED_ATTACK2,
                  SOUND_KANTURU_2ND_DRED_DIE};
        break;
    default:
        return;
    }
    object.MotionTrace.VisitAnimationEvents(
        WorldTime, markers, [&](std::size_t event, float fraction) {
            auto birth =
                sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR * (1.f - fraction));
            if (event != 2)
                PlayBuffer(sounds[event == 3 ? 2 : event]);
            if (object.Type != MODEL_TWIN_TAIL)
                return;
            vec3_t position, angle, light{0.4f, 1.f, 0.6f};
            VectorCopy(object.Angle, angle);
            angle[2] = object.MotionTrace.SampleYaw(WorldTime, fraction, angle[2]);
            if (event == 3)
            {
                object.MotionTrace.Sample(WorldTime, fraction, object.Position, position);
                CreateEffect(MODEL_TWINTAIL_EFFECT, position, angle, light, 1, &object);
                CreateEffect(MODEL_TWINTAIL_EFFECT, position, angle, light, 2, &object);
            }
            if (event != 2)
                return;
            CHARACTER *target = visual.target.Resolve();
            if (!target)
                return;
            target->Object.MotionTrace.Sample(WorldTime, fraction, target->Object.Position,
                                              position);
            VectorCopy(target->Object.Angle, angle);
            angle[2] = target->Object.MotionTrace.SampleYaw(WorldTime, fraction, angle[2]);
            const bool battle = gMapManager.ContextMap() == WD_39KANTURU_3RD;
            Vector(0.4f, 0.9f, 0.6f, light);
            for (int child = 0; child < (battle ? 2 : 5); ++child)
                CreateParticle(BITMAP_SMOKE, position, angle, light, 1);
            Vector(0.4f, 1.f, 0.6f, light);
            for (int child = 0; child < (battle ? 1 : 2); ++child)
                CreateParticle(BITMAP_TWINTAIL_WATER, position, angle, light, 0);
        });
}

bool GMKanturu2nd::Move_Kanturu2nd_MonsterVisual(CHARACTER *, OBJECT *object, BMD *model,
                                                 WorldCharacterVisualState &visual)
{
    EmitMonsterEvents(*object, *model, visual);
    return false;
}

bool GMKanturu2nd::Advance_Kanturu2nd_ObjectVisual(OBJECT *o, BMD *b)
{
    if (Is_Kanturu2nd() == false)
        return false;

    vec3_t Position, Light;

    switch (o->Type)
    {
    case 4: {
        PrepareWorldObjectPose(*o);
        vec3_t vPos, Light;
        float fLumi = (sinf(WorldTime * 0.002f) + 2.0f) * 0.5f;
        Vector(fLumi * 0.3f, fLumi * 0.5f, fLumi * 1.0f, Light);
        Vector(-1.0f, 0.0f, 0.0f, vPos);
        b->TransformPosition(BoneTransform[1], vPos, Position, false);
        CreateSprite(BITMAP_LIGHT, Position, fLumi / 3.2, Light, o);
        CreateSprite(BITMAP_KANTURU_2ND_EFFECT1, Position, fLumi / 3.2, Light, o);
        CreateSprite(BITMAP_KANTURU_2ND_EFFECT1, Position, fLumi / 3.2, Light, o);
    }
    break;
    case 8:
        sessionKeeper_.Visual()->AdvanceEnergyNode(*o, *b, 18);
        break;
    case 10: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 6.f))
        {
            vec3_t vPos;
            VectorCopy(o->Position, vPos);
            vPos[2] = 410.f;
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_WATERFALL_3, vPos, o->Angle, Light, 7, o->Scale);
        }
    }
    break;
    case 45: {
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.06f, 0.06f, 0.06f, Light);
            for (int i = 0; i < 20; ++i)
            {
                CreateParticleFpsChecked(BITMAP_CLOUD, o->Position, o->Angle, Light, 1, o->Scale,
                                         o);
            }
        }
    }
    break;
    case 46: {
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.06f, 0.06f, 0.06f, Light);
            for (int i = 0; i < 20; ++i)
            {
                CreateParticleFpsChecked(BITMAP_CLOUD, o->Position, o->Angle, Light, 2, o->Scale,
                                         o);
            }
        }
    }
    break;
    case 47: {
        if (o->HiddenMesh != -2)
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                vec3_t Light;
                Vector(0.2f, 0.2f, 0.2f, Light);
                for (int i = 0; i < 20; ++i)
                {
                    CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 7, o->Scale, o);
                }
            }
    }
    break;
    case 48: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            vec3_t Light;
            Vector(0.2f, 0.2f, 0.2f, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 10, o->Scale, o);
        }
    }
    break;
    case 49: {
        if (o->HiddenMesh != -2)
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                vec3_t Light;
                Vector(0.0f, 0.01f, 0.03f, Light);
                for (int i = 0; i < 5; ++i)
                {
                    CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 1, o->Scale, o);
                }
            }
    }
    break;
    case 50: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            float fBlue = (WorldRandom() % 3) * 0.01f + 0.02f;
            vec3_t Light;
            Vector(0.0f, 0.01f, fBlue, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 11, o->Scale, o);
        }
    }
    break;
    case 51: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            float fRed = (WorldRandom() % 3) * 0.01f + 0.01f;
            vec3_t Light;
            Vector(fRed, 0.00f, 0.0f, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 11, o->Scale, o);
        }
    }
    break;
    case 52: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 6.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_WATERFALL_3, o->Position, o->Angle, Light, 7, o->Scale);
        }
    }
    break;
    case 53: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(0.4f, 0.6f, 0.7f, Light);
            CreateParticle(BITMAP_TWINTAIL_WATER, o->Position, o->Angle, Light, 1);
        }
    }
    break;
    case 54: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 20.f))
        {
            CreateJoint(BITMAP_JOINT_THUNDER + 1, o->Position, o->Position, o->Angle, 8, NULL,
                        30.f + WorldRandom() % 10);
        }
    }
    break;
    case 55: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 5.f))
        {
            Vector(0.8f, 0.8f, 1.0f, Light);
            CreateEffect(MODEL_FENRIR_THUNDER, o->Position, o->Angle, Light, 1, o);
        }
    }
    break;
    case 56: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.f))
        {
            CreateJoint(BITMAP_JOINT_THUNDER + 1, o->Position, o->Position, o->Angle, 9, NULL,
                        30.f + WorldRandom() % 10);
        }
    }
    break;
    case 65: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.f))
        {
            CreateJoint(BITMAP_JOINT_THUNDER + 1, o->Position, o->Position, o->Angle, 10, NULL,
                        30.f + WorldRandom() % 10);
        }
    }
    break;
    }

    return true;
}

bool GMKanturu2nd::Advance_Kanturu2nd_MonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                                    WorldCharacterVisualState &visual)
{
    std::array<vec34_t, MAX_BONES> emissionBones;
    AnimationPoseSample emissionPose(CharacterPresentationInput(*c).object, b->BoneHead,
                                     b->BodyHeight, false, b->PoseAssetIdentity());
    const auto sampleEmitter = [&](float fraction, bool skeletal) {
        ObjectDrawInput draw(o);
        o->MotionTrace.Sample(WorldTime, fraction, o->Position, draw.position);
        draw.angle[2] = o->MotionTrace.SampleYaw(WorldTime, fraction, draw.angle[2]);
        if (skeletal)
            draw.bones =
                emissionPose.EvaluateAtTime(*b, *o, WorldTime, fraction, emissionBones.data());
        return draw;
    };

    switch (o->Type)
    {
    case MODEL_PERSONA: {
        if (visual.action == MONSTER01_WALK)
        {
            if (rand_fps_check(15))
            {
                PlayBuffer(static_cast<ESound>(SOUND_KANTURU_2ND_PERSO_MOVE1 + WorldRandom() % 2));
            }
        }
        else if (visual.action == MONSTER01_ATTACK1)
        {
        }
        else if (visual.action == MONSTER01_ATTACK2)
        {
        }
        else if (visual.action == MONSTER01_DIE)
        {
            visual.soundSubType = TRUE;
        }

        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2 ||
            visual.action == MONSTER01_WALK)
        {
            visual.soundSubType = FALSE;
        }

        vec3_t vPos;
        vec3_t vLight = {1.0f, 1.0f, 1.0f};
        float Luminosity = (float)(WorldRandom() % 30 + 70) * 0.01f;
        Vector(Luminosity * 0.5f, Luminosity * 0.6f, Luminosity * 1.0f, vLight);

        GetBonePosition(o, CharacterSocket::PRSona_A1, vPos);
        CreateSprite(BITMAP_LIGHT, vPos, 2.0f, vLight, o);

        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 5.f))
        {
            auto draw = sampleEmitter(birth.FrameFraction(), false);
            CreateParticle(BITMAP_SMOKE, draw.position, draw.angle, vLight, 40);
        }

        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            auto draw = sampleEmitter(birth.FrameFraction(), true);
            Vector(0.5f, 0.5f, 0.5f, vLight);
            b->TransformByObjectBone(vPos, draw, CharacterSocket::PRSona_Tail);
            CreateParticle(BITMAP_WATERFALL_3, vPos, draw.angle, vLight, 5, 0.8f, o);
            b->TransformByObjectBone(vPos, draw, CharacterSocket::PRSona_Tail1);
            CreateParticle(BITMAP_WATERFALL_3, vPos, draw.angle, vLight, 5, 0.8f, o);
        }

        if (visual.action == MONSTER01_ATTACK1 &&
            (visual.animationFrame >= 3.f && visual.animationFrame <= 4.f))
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), false);
                Vector(0.6f, 0.6f, 1.0f, vLight);
                CreateEffect(MODEL_STORM, draw.position, draw.angle, vLight, 0);
            }
        }
        else if (visual.action == MONSTER01_ATTACK2 &&
                 (visual.animationFrame >= 3.f && visual.animationFrame <= 4.f))
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), false);
                vec3_t vPos;
                VectorCopy(draw.position, vPos);
                vPos[2] += 100.f;

                int i;
                for (int j = 0; j < 2; j++)
                {
                    i = WorldRandom() % 4;
                    vec3_t vAngle;
                    Vector(0.f, 0.f, i * 90.f, vAngle);

                    CreateJoint(BITMAP_JOINT_SPIRIT, vPos, draw.position, vAngle, 0, o, 80.f);
                    CreateJoint(BITMAP_JOINT_SPIRIT, vPos, draw.position, vAngle, 0, o, 20.f);
                }
            }
        }

        else if (visual.action == MONSTER01_DIE)
        {

            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                Vector(0.5f, 0.5f, 0.5f, vLight);
                b->TransformByObjectBone(vPos, draw, CharacterSocket::PRSona_Tail);
                CreateParticle(BITMAP_WATERFALL_3, vPos, draw.angle, vLight, 6, 0.8f, o);
                b->TransformByObjectBone(vPos, draw, CharacterSocket::PRSona_Tail1);
                CreateParticle(BITMAP_WATERFALL_3, vPos, draw.angle, vLight, 6, 0.8f, o);

                for (int i = 0; i < 5; i++)
                {
                    int j = WorldRandom() % 90;
                    vec3_t p;
                    Vector(0.f, 0.f, 0.f, p);
                    Vector(0.5f, 0.5f, 0.5f, vLight);
                    b->TransformByObjectBone(vPos, draw, j, p);
                    CreateParticle(BITMAP_WATERFALL_3, vPos, draw.angle, vLight, 6, 0.8f, o);
                }
            }
        }

        return true;
    }
    break;
    case MODEL_TWIN_TAIL: {

        if (visual.action == MONSTER01_ATTACK1)
        {
        }
        else if (visual.action == MONSTER01_ATTACK2)
        {
        }
        else if (visual.action == MONSTER01_DIE)
        {
            visual.soundSubType = TRUE;
        }

        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2 ||
            visual.action == MONSTER01_WALK)
        {
            visual.soundSubType = FALSE;
        }

        vec3_t vRelative, vPos;
        vec3_t vLight = {0.6f, 1.0f, 0.4f};

        if (visual.action == MONSTER01_WALK)
        {
            const double millisecondsPerFrame =
                1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
            visual.emissionMilliseconds += FPS_ANIMATION_FACTOR * millisecondsPerFrame;
            constexpr double alternateEmissionPeriod = 500.0;
            while (visual.emissionMilliseconds + 0.0001 >= alternateEmissionPeriod)
            {
                visual.emissionMilliseconds =
                    (std::max)(0.0, visual.emissionMilliseconds - alternateEmissionPeriod);
                const float remaining = float(visual.emissionMilliseconds / millisecondsPerFrame);
                auto birthTime = sessionKeeper_.Gameplay()->EmissionTime(remaining);
                const float fraction = birthTime.FrameFraction();
                const int bone = visual.alternateSide ? CharacterSocket::Twintail_Hair32
                                                      : CharacterSocket::Twintail_Hair24;
                AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false,
                                         b->PoseAssetIdentity());
                Vector(0.f, 0.f, 0.f, vRelative);
                pose.SampleBonePosition(*b, *o, bone, vRelative, WorldTime, fraction, vPos);
                CreateEffect(MODEL_TWINTAIL_EFFECT, vPos, o->Angle, vLight, 0, o);
                visual.alternateSide ^= 1;
                PlayBuffer(static_cast<ESound>(SOUND_KANTURU_2ND_TWIN_MOVE1 + WorldRandom() % 2));
            }
        }
        else if (visual.action == MONSTER01_DIE)
        {

            if (visual.animationFrame <= 3.0f)
            {
                for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
                {
                    auto draw = sampleEmitter(birth.FrameFraction(), false);
                    Vector(0.1f, 1.0f, 0.2f, vLight);
                    for (int i = 0; i < 5; i++)
                    {
                        CreateParticle(BITMAP_SMOKE, draw.position, draw.angle, vLight, 39);
                    }
                    Vector(0.4f, 1.0f, 0.6f, vLight);
                    CreateParticle(BITMAP_TWINTAIL_WATER, draw.position, draw.angle, vLight, 0);

                    CreateEffect(MODEL_SKILL_INFERNO, draw.position, draw.angle, o->Light, 9, o);
                }
            }
        }
        return true;
    }
    break;
    case MODEL_DREADFEAR: {

        if (visual.action == MONSTER01_WALK)
        {
            if (rand_fps_check(15))
            {
                PlayBuffer(static_cast<ESound>(SOUND_KANTURU_2ND_DRED_MOVE1 + WorldRandom() % 2));
            }
        }
        else if (visual.action == MONSTER01_ATTACK1)
        {
        }
        else if (visual.action == MONSTER01_ATTACK2 &&
                 (visual.animationFrame >= 0 && visual.animationFrame <= 2))
        {
            visual.soundSubType = TRUE;
        }
        else if (visual.action == MONSTER01_DIE)
        {
            visual.soundSubType = TRUE;
        }

        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2 ||
            visual.action == MONSTER01_WALK)
        {
            visual.soundSubType = FALSE;
        }

        vec3_t vPos, vLight;
        float fScale;
        Vector(1.f, 1.f, 1.f, vLight);

        if (visual.action != MONSTER01_STOP1 && visual.action != MONSTER01_DIE &&
            gMapManager.ContextMap() != WD_39KANTURU_3RD)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                b->TransformByObjectBone(vPos, draw, CharacterSocket::Dreadfear_Wing32);
                CreateParticle(BITMAP_CLUD64, vPos, draw.angle, vLight, 0);
                b->TransformByObjectBone(vPos, draw, CharacterSocket::Dreadfear_Wing34);
                CreateParticle(BITMAP_CLUD64, vPos, draw.angle, vLight, 0);
            }
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                b->TransformByObjectBone(vPos, draw, CharacterSocket::Dreadfear_Wing51);
                CreateParticle(BITMAP_CLUD64, vPos, draw.angle, vLight, 0);
                b->TransformByObjectBone(vPos, draw, CharacterSocket::Dreadfear_Wing53);
                CreateParticle(BITMAP_CLUD64, vPos, draw.angle, vLight, 0);
            }
        }
        else if (visual.action == MONSTER01_DIE)
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 1.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                vec3_t vRelative;
                Vector(0.f, 0.f, 0.f, vRelative);
                b->TransformByObjectBone(vPos, draw, CharacterSocket::Dreadfear_Wing32, vRelative);
                CreateParticle(BITMAP_SMOKE + 3, vPos, draw.angle, vLight, 3, 1.0f);
                b->TransformByObjectBone(vPos, draw, CharacterSocket::Dreadfear_Wing34, vRelative);
                CreateParticle(BITMAP_SMOKE + 3, vPos, draw.angle, vLight, 3, 1.0f);
                b->TransformByObjectBone(vPos, draw, CharacterSocket::Dreadfear_Wing51, vRelative);
                CreateParticle(BITMAP_SMOKE + 3, vPos, draw.angle, vLight, 3, 1.0f);
                b->TransformByObjectBone(vPos, draw, CharacterSocket::Dreadfear_Wing53, vRelative);
                CreateParticle(BITMAP_SMOKE + 3, vPos, draw.angle, vLight, 3, 1.0f);
            }
        }

        Vector(0.25f, 0.7f, 0.6f, vLight);
        fScale = (WorldRandom() % 10 - 5) * 0.01f;
        GetBonePosition(o, CharacterSocket::Dreadfear_Eye52, vPos);
        CreateSprite(BITMAP_LIGHT + 1, vPos, 0.5f + fScale, vLight, o);
        GetBonePosition(o, CharacterSocket::Dreadfear_Eye54, vPos);
        CreateSprite(BITMAP_LIGHT + 1, vPos, 0.5f + fScale, vLight, o);

        return true;
    }
    break;
    case MODEL_KANTURU2ND_ENTER_NPC: {
        if (visual.action == KANTURU2ND_NPC_ANI_ROT)
        {
            PlayBuffer(SOUND_KANTURU_2ND_MAPSOUND_HOLE);

            int iAnimationFrame = (int)visual.animationFrame;

            vec3_t vPos, vLight;
            Vector(0.f, 0.f, 0.f, vPos);
            Vector(1.0f, 1.0f, 2.0f, vLight);
            if (iAnimationFrame < 40)
            {
                for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.f))
                {
                    auto draw = sampleEmitter(birth.FrameFraction(), true);
                    b->TransformByObjectBone(vPos, draw, CharacterSocket::KANTURU2ND_ENTER_NPC_1);
                    CreateParticle(BITMAP_SPARK + 1, vPos, draw.angle, vLight, 18, 1.0f);
                    b->TransformByObjectBone(vPos, draw, CharacterSocket::KANTURU2ND_ENTER_NPC_2);
                    CreateParticle(BITMAP_SPARK + 1, vPos, draw.angle, vLight, 18, 1.0f);
                    b->TransformByObjectBone(vPos, draw, CharacterSocket::KANTURU2ND_ENTER_NPC_3);
                    CreateParticle(BITMAP_SPARK + 1, vPos, draw.angle, vLight, 18, 1.0f);
                    b->TransformByObjectBone(vPos, draw, CharacterSocket::KANTURU2ND_ENTER_NPC_4);
                    CreateParticle(BITMAP_SPARK + 1, vPos, draw.angle, vLight, 18, 1.0f);
                    b->TransformByObjectBone(vPos, draw, CharacterSocket::KANTURU2ND_ENTER_NPC_5);
                    CreateParticle(BITMAP_SPARK + 1, vPos, draw.angle, vLight, 18, 1.0f);
                    b->TransformByObjectBone(vPos, draw, CharacterSocket::KANTURU2ND_ENTER_NPC_6);
                    CreateParticle(BITMAP_SPARK + 1, vPos, draw.angle, vLight, 18, 1.0f);
                    b->TransformByObjectBone(vPos, draw, CharacterSocket::KANTURU2ND_ENTER_NPC_7);
                    CreateParticle(BITMAP_SPARK + 1, vPos, draw.angle, vLight, 18, 1.0f);
                }
            }

            Vector(1.0f, 1.0f, 2.f, vLight);
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.f))
            {
                auto draw = sampleEmitter(birth.FrameFraction(), true);
                b->TransformByObjectBone(vPos, draw, CharacterSocket::KANTURU2ND_ENTER_NPC_8);
                CreateParticle(BITMAP_CLUD64, vPos, draw.angle, vLight, 2, 2.f);
                b->TransformByObjectBone(vPos, draw, CharacterSocket::KANTURU2ND_ENTER_NPC_9);
                CreateParticle(BITMAP_CLUD64, vPos, draw.angle, vLight, 2, 2.f);
                b->TransformByObjectBone(vPos, draw, CharacterSocket::KANTURU2ND_ENTER_NPC_10);
                CreateParticle(BITMAP_CLUD64, vPos, draw.angle, vLight, 2, 2.f);
                b->TransformByObjectBone(vPos, draw, CharacterSocket::KANTURU2ND_ENTER_NPC_11);
                CreateParticle(BITMAP_CLUD64, vPos, draw.angle, vLight, 2, 2.f);
                b->TransformByObjectBone(vPos, draw, CharacterSocket::KANTURU2ND_ENTER_NPC_12);
                CreateParticle(BITMAP_CLUD64, vPos, draw.angle, vLight, 2, 2.f);
                b->TransformByObjectBone(vPos, draw, CharacterSocket::KANTURU2ND_ENTER_NPC_13);
                CreateParticle(BITMAP_CLUD64, vPos, draw.angle, vLight, 2, 2.f);
            }

            if (iAnimationFrame <= 10)
            {
                Vector(0.0f, 0.0f, 0.0f, vLight);
            }
            else if (iAnimationFrame == 10)
            {
                Vector(0.1f, 0.1f, 0.1f, vLight);
            }
            else if (iAnimationFrame >= 11 && iAnimationFrame <= 28)
            {
                vLight[0] = (iAnimationFrame - 11) * 0.06f;
                vLight[1] = (iAnimationFrame - 11) * 0.06f;
                vLight[2] = (iAnimationFrame - 11) * 0.06f;
            }
            else if (iAnimationFrame >= 29)
            {
                Vector(1.0f, 1.0f, 1.0f, vLight);
            }
            VectorCopy(o->Position, vPos);
            vPos[2] += 310; // Y
            vPos[1] -= 100; // X
            vPos[0] += 40;  // Z
            CreateSprite(BITMAP_LIGHTNING + 1, vPos, 1.0, vLight, o, (WorldTime / 10.0f));

            if (iAnimationFrame >= 12 && iAnimationFrame <= 25)
            {
                vec3_t vPos, vPos2;
                VectorCopy(o->Position, vPos2);
                vPos2[2] += 290.f;
                VectorCopy(o->Position, vPos);
                vPos[0] += (WorldRandom() % 600 - 300.f);
                vPos[1] -= (WorldRandom() % 200 + 350.f);
                vPos[2] += (WorldRandom() % 600 - 100.f);
                CreateJoint(BITMAP_JOINT_ENERGY, vPos, vPos2, o->Angle, 42, o, 10.f);
                CreateJoint(BITMAP_JOINT_ENERGY, vPos, vPos2, o->Angle, 42, o, 10.f);
            }

            VectorCopy(o->Position, vPos);
            vPos[0] += 260.0f;
            vPos[1] -= 115.0f;
            vPos[2] += 50.f;
            if (iAnimationFrame >= 1 && iAnimationFrame <= 9)
            {
                vPos[0] += iAnimationFrame * 4;
            }
            else if (iAnimationFrame >= 10 && iAnimationFrame <= 18)
            {
                vPos[0] += 36.0f;
                vPos[0] -= (iAnimationFrame - 9) * 4;
            }

            if (iAnimationFrame >= 1 && iAnimationFrame <= 20)
            {
                vec3_t vAngle;
                Vector((float)(WorldRandom() % 60 + 290), 0.f, (float)(WorldRandom() % 30 + 90),
                       vAngle);
                CreateJoint(BITMAP_JOINT_SPARK, vPos, vPos, vAngle, 0);
            }
        }
    }
    break;
    case MODEL_TRAP_CANON: {
        AdvanceTrapCanonObjectVisual(c, o, b);
    }
    break;
    }
    return false;
}

void GMKanturu2nd::Move_Kanturu2nd_BlurEffect(CHARACTER *c, OBJECT *o, BMD *b)
{
    switch (o->Type)
    {
    case MODEL_PERSONA: {
    }
    break;
    case MODEL_TWIN_TAIL: {
        if ((o->AnimationFrame <= 4.12f && o->CurrentAction == MONSTER01_ATTACK1))
        {
            vec3_t Light;
            Vector(1.0f, 0.2f, 0.5f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(-150.f, 50.f, 0.f, StartRelative);
                Vector(150.f, -200.f, 0.f, EndRelative);

                b->TransformPosition(BoneTransform[58], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[59], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 1);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
        else if (o->CurrentAction == MONSTER01_ATTACK2)
        {
            CHARACTER *tc = &CharactersClient[c->TargetCharacter];
            OBJECT *to = &tc->Object;

            vec3_t vPos, vRelative;
            Vector(0.f, 0.f, 0.f, vRelative);
            if (gMapManager.ContextMap() == WD_39KANTURU_3RD)
            {
                for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.f))
                {
                    AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false,
                                             b->PoseAssetIdentity());
                    pose.SampleBonePosition(*b, *o, CharacterSocket::Twintail_Hair24, vRelative,
                                            WorldTime, birth.FrameFraction(), vPos);
                    vec3_t targetPosition;
                    to->MotionTrace.Sample(WorldTime, birth.FrameFraction(), to->Position,
                                           targetPosition);
                    CreateJoint(BITMAP_JOINT_ENERGY, vPos, targetPosition, o->Angle, 0, to, 30.f);
                }

                for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.f))
                {
                    AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false,
                                             b->PoseAssetIdentity());
                    pose.SampleBonePosition(*b, *o, CharacterSocket::Twintail_Hair32, vRelative,
                                            WorldTime, birth.FrameFraction(), vPos);
                    vec3_t targetPosition;
                    to->MotionTrace.Sample(WorldTime, birth.FrameFraction(), to->Position,
                                           targetPosition);
                    CreateJoint(BITMAP_JOINT_ENERGY, vPos, targetPosition, o->Angle, 0, to, 30.f);
                }
            }
            else
            {
                for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
                {
                    AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false,
                                             b->PoseAssetIdentity());
                    pose.SampleBonePosition(*b, *o, CharacterSocket::Twintail_Hair24, vRelative,
                                            WorldTime, birth.FrameFraction(), vPos);
                    vec3_t targetPosition;
                    to->MotionTrace.Sample(WorldTime, birth.FrameFraction(), to->Position,
                                           targetPosition);
                    CreateJoint(BITMAP_JOINT_ENERGY, vPos, targetPosition, o->Angle, 0, to, 30.f);
                }

                for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
                {
                    AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false,
                                             b->PoseAssetIdentity());
                    pose.SampleBonePosition(*b, *o, CharacterSocket::Twintail_Hair32, vRelative,
                                            WorldTime, birth.FrameFraction(), vPos);
                    vec3_t targetPosition;
                    to->MotionTrace.Sample(WorldTime, birth.FrameFraction(), to->Position,
                                           targetPosition);
                    CreateJoint(BITMAP_JOINT_ENERGY, vPos, targetPosition, o->Angle, 0, to, 30.f);
                }
            }
        }
    }
    break;
    case MODEL_DREADFEAR: {
        if ((o->AnimationFrame <= 5.0f && o->CurrentAction == MONSTER01_ATTACK1) ||
            (o->AnimationFrame <= 9.0f && o->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t Light;
            Vector(1.0f, 1.0f, 1.0f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(100.f, 120.f, 0.f, EndRelative);

                b->TransformPosition(BoneTransform[33], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[33], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 4);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    }
}

void GMKanturu2nd::PlayBGM()
{
    if (gMapManager.ContextMap() == WD_38KANTURU_2ND)
    {
        PlayMp3(MUSIC_KANTURU_2ND);
    }
    else
    {
        StopMp3(MUSIC_KANTURU_2ND);
    }
}

void GMKanturu2nd::AdvanceTrapCanonObjectVisual(CHARACTER *c, OBJECT *o, BMD *b)
{
    if (c->AttackTime < 1)
    {
        float fLumi;
        vec3_t vPos, vLight;
        VectorCopy(o->Position, vPos);
        fLumi = (sinf(WorldTime * 0.003f) + 1.f);
        vPos[1] -= 5.0f;
        vPos[2] += 80.f + fLumi * 5.f;
        fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.1f;
        Vector(2.0f + fLumi, 1.0f + fLumi, 1.0f + fLumi, vLight);
        CreateSprite(BITMAP_POUNDING_BALL, vPos, 0.7f + fLumi, vLight, o, (WorldTime / 10.0f));
        fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 1.0f;
        Vector(2.0f + (WorldRandom() % 10) * 0.03f, 0.4f + (WorldRandom() % 10) * 0.03f,
               0.4f + (WorldRandom() % 10) * 0.03f, vLight);
        CreateSprite(BITMAP_LIGHT, vPos, 2.0f, vLight, o, -(WorldTime * 0.1f));
        CreateSprite(BITMAP_LIGHT, vPos, 2.0f, vLight, o, (WorldTime * 0.12f));
    }
}

bool GMKanturu2nd::AdvanceObjectVisual(OBJECT *object, BMD *model, float)
{
    return Advance_Kanturu2nd_ObjectVisual(object, model);
}

void GMKanturu2nd::UpdateMusic()
{
    PlayBGM();
}

bool GMKanturu2nd::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_KANTURU_2ND) == 0;
}

bool GMKanturu3rd::AdvanceKanturu3rdObjectVisual(OBJECT *o, BMD *b)
{
    if (!IsInKanturu3rd())
        return false;

    vec3_t Position, Light;

    switch (o->Type)
    {
    case 0: {
        PrepareWorldObjectPose(*o);
        AdvanceMayaLighting(*o);
        vec3_t p, Pos, Light;

        if (!g_Direction.m_CKanturu.GetMayaExplotion())
            o->HiddenMesh = 0;

        if (g_Direction.m_CKanturu.m_iMayaState >= KANTURU_MAYA_DIRECTION_MONSTER1 &&
            g_Direction.m_CKanturu.m_iMayaState <= KANTURU_MAYA_DIRECTION_ENDCYCLE)
            o->Position[2] = -749.5f;

        if (g_Direction.m_CKanturu.GetMayaAppear())
        {
            o->Position[2] = std::min(-749.5f, o->Position[2] + 13.f * FPS_ANIMATION_FACTOR);
            if (o->Position[2] == -749.5f)
                g_Direction.m_CKanturu.SetMayaAppear(false);
            EarthQuake = (float)(WorldRandom() % 6 - 3) * 2.0f;

            PlayBuffer(SOUND_KANTURU_3RD_MAYA_INTRO);
        }
        else
        {
            Vector(0.08f, 0.08f, 0.08f, Light);
            Vector(WorldRandom() % 20 - 30.0f, WorldRandom() % 20 - 30.0f, 0.0f, p);
            b->TransformPosition(BoneTransform[34], p, Pos, false);
            if (o->AnimationFrame >= 5.0f && o->AnimationFrame < 12.5f)
                CreateParticleFpsChecked(BITMAP_SMOKE, Pos, o->Angle, Light, 43, 1.5f);
        }

        if (g_Direction.m_CKanturu.m_iMayaState == KANTURU_MAYA_DIRECTION_ENDCYCLE ||
            g_Direction.m_CKanturu.m_iKanturuState == KANTURU_STATE_STANDBY)
        {
            //KanturuDirection.SetMayaAppear(false);
            o->Position[2] = -2749.5f;
        }

        MayaAction(o, b);
    }
    break;
    case 1: {
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.0f, 0.03f, 0.04f, Light);
            for (int i = 0; i < 10; ++i)
            {
                CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 1, o->Scale, o);
            }
        }
        o->HiddenMesh = -2;
    }
    break;
    case 5: {
        PrepareWorldObjectPose(*o);
        float fLumi = (sinf(WorldTime * 0.002f) + 2.0f) * 0.5f;
        Vector(fLumi * 0.3f, fLumi * 0.5f, fLumi * 1.0f, Light);
        vec3_t vPos;
        Vector(-1.0f, 0.0f, 0.0f, vPos);
        b->TransformPosition(BoneTransform[1], vPos, Position, false);
        CreateSprite(BITMAP_LIGHT, Position, fLumi / 2.4, Light, o);
        CreateSprite(BITMAP_SHINY + 1, Position, fLumi / 2.4, Light, o,
                     (float)((int)(WorldTime / 2) % 360));
        CreateSprite(BITMAP_SHINY + 1, Position, fLumi / 2.4, Light, o,
                     (float)((int)(WorldTime / 4) % 360));
    }
    break;
    case 11: {
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.1f, 0.1f, 0.1f, Light);
            for (int i = 0; i < 10; ++i)
            {
                CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 3, o->Scale, o);
            }
        }
        o->HiddenMesh = -2;
    }
    break;
    case 32: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(0.4f, 0.6f, 0.7f, Light);
            CreateParticle(BITMAP_TWINTAIL_WATER, o->Position, o->Angle, Light, 1);
        }
    }
    break;
    case 46: {
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.25f, 0.25f, 0.25f, Light);
            for (int i = 0; i < 20; ++i)
            {
                CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 7, o->Scale, o);
            }
        }
        o->HiddenMesh = -2;
    }
    break;
    case 74: {
        if (KanturuSuccessMap)
            o->HiddenMesh = -2;
        else
        {
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 5.f))
                CreateJoint(BITMAP_JOINT_THUNDER + 1, o->Position, o->Position, o->Angle, 11, NULL,
                            o->Scale * 15.0f);
        }
    }
    break;
    case 47: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 5.f))
            CreateJoint(BITMAP_JOINT_THUNDER + 1, o->Position, o->Position, o->Angle, 11, NULL,
                        o->Scale * 15.0f);
    }
    break;
    case 48: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, o->Light, 10, o->Scale, o);
        }
    }
    break;
    case 49: {
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.0f, 0.06f, 0.10f, Light);
            for (int i = 0; i < 10; ++i)
            {
                CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 1, o->Scale, o);
            }
        }
        o->HiddenMesh = -2;
    }
    break;
    case 50: {
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.1f, 0.1f, 0.1f, Light);
            for (int i = 0; i < 10; ++i)
            {
                CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 4, o->Scale, o);
            }
        }
        o->HiddenMesh = -2;
    }
    break;
    case 52: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(0.5f, 1.f, 0.7f, o->Light);
            CreateParticle(BITMAP_TRUE_BLUE, o->Position, o->Angle, o->Light, 1, o->Scale);
        }
    }
    break;
    case 53: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(0.3f, 0.3f, 0.3f, o->Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, o->Light, 46, o->Scale);
        }
    }
    break;
    case 54: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            o->HiddenMesh = -1;
            CreateJoint(BITMAP_JOINT_THUNDER, o->Position, o->Position, o->Angle, 19, NULL,
                        o->Scale * 10.0f);
        }
    }
    break;
    }
    return true;
}

void GMKanturu3rd::EmitNightmareEvents(OBJECT &object, BMD &model)
{
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    constexpr std::array<std::pair<int, float>, 6> markers{{{MONSTER01_ATTACK1, 0.f},
                                                            {MONSTER01_ATTACK2, 0.f},
                                                            {MONSTER01_ATTACK2, 5.2f},
                                                            {MONSTER01_ATTACK3, 0.f},
                                                            {MONSTER01_ATTACK4, 0.f},
                                                            {MONSTER01_DIE, 0.f}}};
    constexpr std::array<ESound, 5> sounds{
        SOUND_KANTURU_3RD_NIGHTMARE_ATT1, SOUND_KANTURU_3RD_NIGHTMARE_ATT2,
        SOUND_KANTURU_3RD_NIGHTMARE_ATT3, SOUND_KANTURU_3RD_NIGHTMARE_ATT4,
        SOUND_KANTURU_3RD_NIGHTMARE_DIE};
    object.MotionTrace.VisitAnimationEvents(
        WorldTime, markers, [&](std::size_t event, float fraction) {
            if (event != 2)
            {
                PlayBuffer(sounds[event < 2 ? event : event - 1]);
                return;
            }
            auto birth =
                sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR * (1.f - fraction));
            ObjectDrawInput draw(&object);
            object.MotionTrace.Sample(WorldTime, fraction, object.Position, draw.position);
            draw.angle[2] = object.MotionTrace.SampleYaw(WorldTime, fraction, draw.angle[2]);
            AnimationPoseSample pose(draw, model.BoneHead, model.BodyHeight, false,
                                     model.PoseAssetIdentity());
            std::array<vec34_t, MAX_BONES> bones;
            draw.bones = pose.EvaluateAtTime(model, object, WorldTime, fraction, bones.data());
            model.BodyScale = object.Scale;
            vec3_t offset{0.f, -float(WorldRandom() % 20) - 40.f, 0.f}, position,
                light{0.5f, 0.7f, 1.f};
            GetBonePosition(draw, CharacterSocket::LHand_Bone, offset, position);
            CreateEffect(MODEL_STORM2, position, draw.angle, light, 0);
            CreateEffect(BITMAP_BOSS_LASER, position, draw.angle, light, 2);
        });
}

bool GMKanturu3rd::MoveKanturu3rdMonsterVisual(OBJECT *o, BMD *b, WorldCharacterVisualState &visual)
{
    ObjectDrawInput presentation(o);
    visual.movement.Apply(presentation);
    if (!IsInKanturu3rd())
        return false;

    vec3_t Position, Direction, Angle, Light;

    switch (o->Type)
    {
    case MODEL_DARK_SKULL_SOLDIER_5: {
        EmitNightmareEvents(*o, *b);
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(
                    static_cast<ESound>(SOUND_KANTURU_3RD_NIGHTMARE_IDLE1 + WorldRandom() % 2));
        }

        if (visual.action == MONSTER01_ATTACK3)
        {
            Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);

            for (int i = 0; i < 2; i++)
            {
                Position[0] = o->StartPosition[0] + WorldRandom() % 440 - 220;
                Position[1] = o->StartPosition[1] + WorldRandom() % 440 - 220;
                Position[2] = o->StartPosition[2];
                CreateJoint(BITMAP_FLARE, Position, Position, Angle, 2, NULL, 40);
            }
        }

        else if (visual.action == MONSTER01_DIE && visual.animationFrame >= 3.0f &&
                 rand_fps_check(1))
        {
            vec3_t Position;
            GetBonePosition(presentation, CharacterSocket::Body_Bone13, Position);
            CreateParticle(BITMAP_SMOKE + 3, Position, o->Angle, Light, 3, 1.5f);
            CreateParticle(BITMAP_SMOKE + 3, Position, o->Angle, Light, 3, 1.5f);

            vec3_t Angle;
            Vector((float)(WorldRandom() % 100) - 50.0f, (float)(WorldRandom() % 100) - 50.0f,
                   (float)(WorldRandom() % 360), Angle);
            float Scale = (float)(WorldRandom() % 40) + 30.0f;
            CreateJoint(BITMAP_JOINT_SPIRIT2, Position, Position, Angle, 8, NULL, Scale, 0, 0);
        }
        else if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.movement.subType = FALSE;
    }
    break;
    case MODEL_MAYA_HAND_LEFT:
    case MODEL_MAYA_HAND_RIGHT: {
        const bool crumbling = visual.action == MONSTER01_DIE &&
                               g_Direction.m_CKanturu.m_iMayaState < KANTURU_MAYA_DIRECTION_MAYA3;
        visual.movement.materialFields |= CharacterMovementVisual::Mesh;
        visual.movement.blendMesh = crumbling ? -2 : -1;
        AnimationPoseSample pose =
            presentation.preparedPose
                ? *presentation.preparedPose
                : AnimationPoseSample(presentation, b->BoneHead, b->BodyHeight, false,
                                      b->PoseAssetIdentity());
        std::array<vec34_t, MAX_BONES> bones;
        if (crumbling)
        {
            Vector(0.4f, 0.6f, 1.f, Light);
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                auto draw = presentation;
                o->MotionTrace.Sample(WorldTime, birth.FrameFraction(), o->Position, draw.position);
                draw.bones =
                    pose.EvaluateAtTime(*b, *o, WorldTime, birth.FrameFraction(), bones.data());
                for (int i = 0; i < 5; ++i)
                {
                    vec3_t position;
                    b->TransformByObjectBone(position, draw, WorldRandom() % 45);
                    CreateParticle(BITMAP_WATERFALL_3, position, o->Angle, Light, 6, 2.f, o);
                }
            }
        }
        if (visual.action == MONSTER01_STOP2 &&
            g_Direction.m_CKanturu.m_iMayaState >= KANTURU_MAYA_DIRECTION_MAYA3)
        {
            const bool left = o->Type == MODEL_MAYA_HAND_LEFT;
            vec3_t offset{left ? 0.f : 50.f, left ? -50.f : 0.f, 0.f};
            Vector(1.f, 1.f, 1.f, Light);
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                vec3_t position;
                pose.SampleBonePosition(*b, *o, left ? 24 : 12, offset, WorldTime,
                                        birth.FrameFraction(), position);
                CreateParticle(BITMAP_SMOKE + 3, position, o->Angle, Light, 3, 1.5f);
                CreateParticle(BITMAP_SMOKE + 3, position, o->Angle, Light, 4, 0.8f);
            }
        }
    }
    break;
    case MODEL_MAYA: {
    }
    break;
    case MODEL_SMELTING_NPC: {
    }
    break;
    }

    return false;
}

void GMKanturu3rd::MoveKanturu3rdBlurEffect(CHARACTER *c, OBJECT *o, BMD *b)
{
    if (!IsInKanturu3rd())
        return;

    switch (o->Type)
    {
    case MODEL_DARK_SKULL_SOLDIER_5: {
        if ((o->CurrentAction == MONSTER01_ATTACK1 || o->CurrentAction == MONSTER01_ATTACK4) &&
            o->AnimationFrame <= 5.9f)
        {
            vec3_t Light;
            Vector(1.0f, 1.0f, 1.0f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                fAnimationFrame += fSpeedPerFrame;

                Vector(10.f, 0.f, 0.f, StartRelative);
                Vector(10.f, -10.f, 0.f, EndRelative);

                b->TransformPosition(BoneTransform[32], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[38], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 3, true, 80);
            }
        }
    }
    break;
    case MODEL_MAYA_HAND_LEFT: {
    }
    break;
    case MODEL_MAYA_HAND_RIGHT: {
    }
    break;
    case MODEL_MAYA: {
    }
    break;
    }
}

bool GMKanturu3rd::AdvanceKanturu3rdMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                                  WorldCharacterVisualState &visual)
{
    if (!IsInKanturu3rd())
        return false;

    vec3_t Position, Light;

    switch (o->Type)
    {
    case MODEL_DARK_SKULL_SOLDIER_5: {
        if (visual.action == MONSTER01_DIE)
            return true;

        MoveEye(o, b, 9, 10, 39, 40);

        Vector(1.0f, 1.0f, 1.0f, Light);
        Vector(0.0f, 0.0f, 0.0f, Position);

        GetBonePosition(o, CharacterSocket::Body_Bone1, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.3f, Light, o);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.5f, Light, o);
        GetBonePosition(o, CharacterSocket::Body_Bone2, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.3f, Light, o);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.5f, Light, o);
        GetBonePosition(o, CharacterSocket::Body_Bone3, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.3f, Light, o);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.5f, Light, o);
        GetBonePosition(o, CharacterSocket::Body_Bone4, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.3f, Light, o);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.5f, Light, o);
        Vector(5.0f, 0.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::Body_Bone5, Position, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.3f, Light, o);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.5f, Light, o);
        Vector(9.0f, 0.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::Body_Bone6, Position, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.3f, Light, o);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.5f, Light, o);
        Vector(6.0f, 0.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::Body_Bone7, Position, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.4f, Light, o);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.7f, Light, o);
        GetBonePosition(o, CharacterSocket::Body_Bone8, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.4f, Light, o);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.7f, Light, o);
        GetBonePosition(o, CharacterSocket::Body_Bone9, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.4f, Light, o);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.7f, Light, o);
        GetBonePosition(o, CharacterSocket::Body_Bone10, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.4f, Light, o);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.7f, Light, o);
        Vector(10.0f, 0.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::Body_Bone11, Position, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.4f, Light, o);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.7f, Light, o);
        GetBonePosition(o, CharacterSocket::Body_Bone12, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.4f, Light, o);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.7f, Light, o);

        GetBonePosition(o, CharacterSocket::Body_Bone13, Position);
        CreateParticleFpsChecked(BITMAP_FIRE + 1, Position, o->Angle, Light, 3, 1.7f);

        Vector(3.0f, 0.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::Sword_Bone1, Position, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.3f, Light, o);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.6f, Light, o);
        Vector(3.0f, 0.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::Sword_Bone2, Position, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.3f, Light, o);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.6f, Light, o);

        Vector(0.0f, -2.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::Eye_Bone1, Position, Position);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.3f, Light, o);
        Vector(4.0f, -4.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::Eye_Bone2, Position, Position);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.3f, Light, o);

        GetBonePosition(o, CharacterSocket::Windmill_Bone1, Position);
        if (rand_fps_check(2))
            CreateParticle(BITMAP_SPARK + 1, Position, o->Angle, Light, 16, 1.0f);
    }
        return true;
    case MODEL_MAYA_HAND_LEFT: {

        if (visual.action == MONSTER01_DIE)
            return true;

        MoveEye(o, b, 5, 11, 17, 29, 23);

        Vector(0.5f, 0.5f, 0.8f, Light);

        Vector(0.0f, 0.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::L_Hand01, Position, Position);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 1.3f, Light, o);
        Vector(-10.0f, 0.0f, 3.0f, Position);
        GetBonePosition(o, CharacterSocket::L_Hand01, Position, Position);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.7f, Light, o);

        Vector(0.0f, 0.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::L_Hand02, Position, Position);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 1.3f, Light, o);
        Vector(-12.0f, 5.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::L_Hand02, Position, Position);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.7f, Light, o);

        Vector(0.0f, 0.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::L_Hand03, Position, Position);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 1.3f, Light, o);
        Vector(-12.0f, 5.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::L_Hand03, Position, Position);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.7f, Light, o);

        Vector(0.0f, 0.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::L_Hand04, Position, Position);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 1.3f, Light, o);
        Vector(-10.0f, 5.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::L_Hand04, Position, Position);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.7f, Light, o);

        Vector(0.0f, 0.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::L_Hand05, Position, Position);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 1.3f, Light, o);
        Vector(-8.0f, 9.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::L_Hand05, Position, Position);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 0.7f, Light, o);

        Vector(0.0f, 0.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::L_Hand21, Position, Position);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 1.0f, Light, o);
        Vector(0.0f, 0.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::L_Hand22, Position, Position);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 1.0f, Light, o);
        Vector(0.0f, 0.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::L_Hand23, Position, Position);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 1.0f, Light, o);
        Vector(0.0f, 0.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::L_Hand24, Position, Position);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 1.0f, Light, o);
        Vector(0.0f, 0.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::L_Hand25, Position, Position);
        CreateSprite(BITMAP_FLARE_BLUE, Position, 1.0f, Light, o);
    }
        return true;
    case MODEL_MAYA_HAND_RIGHT: {

        if (visual.action == MONSTER01_DIE)
            return true;

        MoveEye(o, b, 5, 20, 31, 42, 53);

        Vector(0.5f, 0.3f, 0.3f, Light);

        Vector(-2.0f, 0.0f, 4.0f, Position);
        GetBonePosition(o, CharacterSocket::R_Hand01, Position, Position);
        CreateSprite(BITMAP_FLARE_RED, Position, 1.5f, Light, o);
        Vector(-10.0f, -3.0f, 3.0f, Position);
        GetBonePosition(o, CharacterSocket::R_Hand01, Position, Position);
        CreateSprite(BITMAP_FLARE_RED, Position, 0.7f, Light, o);

        Vector(-1.0f, -2.0f, 4.0f, Position);
        GetBonePosition(o, CharacterSocket::R_Hand02, Position, Position);
        CreateSprite(BITMAP_FLARE_RED, Position, 1.5f, Light, o);
        Vector(-10.0f, -3.0f, 5.0f, Position);
        GetBonePosition(o, CharacterSocket::R_Hand02, Position, Position);
        CreateSprite(BITMAP_FLARE_RED, Position, 0.7f, Light, o);

        Vector(0.0f, 0.0f, 4.0f, Position);
        GetBonePosition(o, CharacterSocket::R_Hand03, Position, Position);
        CreateSprite(BITMAP_FLARE_RED, Position, 1.3f, Light, o);
        Vector(-10.0f, 0.0f, 4.0f, Position);
        GetBonePosition(o, CharacterSocket::R_Hand03, Position, Position);
        CreateSprite(BITMAP_FLARE_RED, Position, 0.7f, Light, o);

        Vector(0.0f, 0.0f, 4.0f, Position);
        GetBonePosition(o, CharacterSocket::R_Hand04, Position, Position);
        CreateSprite(BITMAP_FLARE_RED, Position, 1.3f, Light, o);
        Vector(-10.0f, 1.0f, 4.0f, Position);
        GetBonePosition(o, CharacterSocket::R_Hand04, Position, Position);
        CreateSprite(BITMAP_FLARE_RED, Position, 0.7f, Light, o);

        Vector(6.0f, 3.0f, 3.0f, Position);
        GetBonePosition(o, CharacterSocket::R_Hand05, Position, Position);
        CreateSprite(BITMAP_FLARE_RED, Position, 1.3f, Light, o);
        Vector(-5.0f, 10.0f, 5.0f, Position);
        GetBonePosition(o, CharacterSocket::R_Hand05, Position, Position);
        CreateSprite(BITMAP_FLARE_RED, Position, 0.7f, Light, o);

        Vector(0.0f, 0.0f, -5.0f, Position);
        GetBonePosition(o, CharacterSocket::R_Hand11, Position, Position);
        CreateSprite(BITMAP_FLARE_RED, Position, 1.0f, Light, o);
        Vector(0.0f, 0.0f, -2.0f, Position);
        GetBonePosition(o, CharacterSocket::R_Hand12, Position, Position);
        CreateSprite(BITMAP_FLARE_RED, Position, 1.0f, Light, o);
        Vector(0.0f, 0.0f, -5.0f, Position);
        GetBonePosition(o, CharacterSocket::R_Hand13, Position, Position);
        CreateSprite(BITMAP_FLARE_RED, Position, 1.0f, Light, o);
        Vector(0.0f, 0.0f, -5.0f, Position);
        GetBonePosition(o, CharacterSocket::R_Hand14, Position, Position);
        CreateSprite(BITMAP_FLARE_RED, Position, 1.0f, Light, o);
        Vector(10.0f, -4.0f, 0.0f, Position);
        GetBonePosition(o, CharacterSocket::R_Hand15, Position, Position);
        CreateSprite(BITMAP_FLARE_RED, Position, 1.0f, Light, o);
    }
        return true;
    case MODEL_MAYA: {
    }
        return true;
    case MODEL_SMELTING_NPC: {

        Position[0] = o->Position[0] + (float)(WorldRandom() % 250 - 125);
        Position[1] = o->Position[1] + (float)(WorldRandom() % 250 - 125);
        Position[2] = o->Position[2] - (float)(WorldRandom() % 100) + 50.0f;

        if (rand_fps_check(10))
        {
            Vector(0.5f, 1.0f, 0.8f, Light);
            CreateParticle(BITMAP_SPARK + 1, Position, o->Angle, Light, 17, 0.6f);
        }
    }
        return true;
    }

    return false;
}

bool GMKanturu3rd::AttackEffectKanturu3rdMonster(CHARACTER *c, OBJECT *o, BMD *b)
{
    if (!IsInKanturu3rd())
        return false;

    vec3_t Light, Position, Direction, Angle;

    switch (c->MonsterIndex)
    {
    case MONSTER_DEATH_SPIRIT: {
        if (o->CurrentAction == MONSTER01_ATTACK1 || o->CurrentAction == MONSTER01_ATTACK2)
        {
            if (c->CheckAttackTime(14))
            {
                vec3_t vPos, vRelative, Light;
                Vector(140.f, 0.f, -30.f, vRelative);
                Vector(0.2f, 0.2f, 1.f, Light);
                GetBonePosition(o, CharacterSocket::BLADE_L_HAND, vRelative, vPos);

                CreateEffect(MODEL_BLADE_SKILL, vPos, o->Angle, Light, 0);

                CreateParticle(BITMAP_EXPLOTION + 1, vPos, o->Angle, o->Light, 0, 1.3f);
                CreateParticle(BITMAP_EXPLOTION + 1, vPos, o->Angle, o->Light, 0, 2.3f);
                CreateParticle(BITMAP_EXPLOTION + 1, vPos, o->Angle, o->Light, 0, 1.8f);
                CreateParticle(BITMAP_EXPLOTION + 1, vPos, o->Angle, o->Light, 0, 1.3f);
                CreateParticle(BITMAP_EXPLOTION + 1, vPos, o->Angle, o->Light, 0, 2.3f);
                CreateParticle(BITMAP_EXPLOTION + 1, vPos, o->Angle, o->Light, 0, 1.8f);
                c->SetLastAttackEffectTime();
            }
        }
    }
        return true;
    case MONSTER_NIGHTMARE: {
        if (o->CurrentAction == MONSTER01_ATTACK3 && c->AttackTime >= 14 && rand_fps_check(1))
        {
            Vector(0.3f, 0.2f, 0.1f, Light);
            CreateEffect(MODEL_SUMMON, o->Position, o->Angle, Light, 3);
        }
        else if (o->CurrentAction == MONSTER01_ATTACK4 && c->AttackTime >= 10 && rand_fps_check(1))
        {
            Vector(1.0f, 1.0f, 1.0f, Light);
            CreateInferno(o->Position);
            if (c->CheckAttackTime(10))
            {
                CreateEffect(MODEL_CIRCLE, o->Position, o->Angle, Light, 4, o);
                c->SetLastAttackEffectTime();
            }
            else if (c->CheckAttackTime(14))
            {
                CreateEffect(MODEL_CIRCLE, o->Position, o->Angle, Light, 4, o);
                c->SetLastAttackEffectTime();
            }
        }
    }
        return true;
    case MONSTER_MAYA_HAND_LEFT: {
        if (o->CurrentAction == MONSTER01_ATTACK1 && c->CheckAttackTime(14))
        {
            CreateInferno(o->Position, 2);
            Vector(0.0f, 0.5f, 1.0f, Light);
            CreateEffect(BITMAP_SHOCK_WAVE, o->Position, o->Angle, Light, 7);

            PlayBuffer(SOUND_KANTURU_3RD_MAYAHAND_ATTACK1);
            c->SetLastAttackEffectTime();
        }
        else if (o->CurrentAction == MONSTER01_ATTACK2 && c->CheckAttackTime(14))
        {
            float Matrix[3][4];
            Vector(0.0f, 0.0f, 0.0f, Angle);
            Vector(0.0f, -160.0f, 0.0f, Direction);
            AngleMatrix(Angle, Matrix);
            VectorRotate(Direction, Matrix, Position);
            VectorAdd(o->Position, Position, Position);

            Vector(0.3f, 0.5f, 1.0f, Light);
            CreateEffect(MODEL_MAYAHANDSKILL, Position, o->Angle, Light, 0, NULL, -1, 0, 0, 0,
                         1.0f);

            PlayBuffer(SOUND_KANTURU_3RD_MAYAHAND_ATTACK2);
            c->SetLastAttackEffectTime();
        }
    }
        return true;
    case MONSTER_MAYA_HAND_RIGHT: {
        if (o->CurrentAction == MONSTER01_ATTACK1 && c->CheckAttackTime(14))
        {
            CreateInferno(o->Position, 3);
            Vector(1.0f, 0.5f, 0.0f, Light);
            CreateEffect(BITMAP_SHOCK_WAVE, o->Position, o->Angle, Light, 7);

            PlayBuffer(SOUND_KANTURU_3RD_MAYAHAND_ATTACK1);
            c->SetLastAttackEffectTime();
        }
        else if (o->CurrentAction == MONSTER01_ATTACK2 && c->CheckAttackTime(14))
        {
            float Matrix[3][4];
            Vector(0.0f, 0.0f, 0.0f, Angle);
            Vector(0.0f, -160.0f, 0.0f, Direction);
            AngleMatrix(Angle, Matrix);
            VectorRotate(Direction, Matrix, Position);
            VectorAdd(o->Position, Position, Position);

            Vector(1.0f, 0.6f, 0.4f, Light);
            CreateEffect(MODEL_MAYAHANDSKILL, Position, o->Angle, Light, 0, NULL, -1, 0, 0, 0,
                         1.0f);

            PlayBuffer(SOUND_KANTURU_3RD_MAYAHAND_ATTACK2);
            c->SetLastAttackEffectTime();
        }
    }
        return true;
    case MONSTER_MAYA:
        return true;
    }

    return false;
}

void GMKanturu3rd::ChangeBackGroundMusic(int World)
{
    if (World == WD_39KANTURU_3RD)
    {
        if (g_Direction.m_CKanturu.m_iKanturuState == KANTURU_STATE_MAYA_BATTLE ||
            g_Direction.m_CKanturu.m_iKanturuState == KANTURU_STATE_STANDBY)
        {
            StopMp3(MUSIC_KANTURU_TOWER);
            PlayMp3(MUSIC_KANTURU_MAYA_BATTLE);
            if (IsEndMp3())
                StopMp3(MUSIC_KANTURU_MAYA_BATTLE);
        }
        else if (g_Direction.m_CKanturu.m_iKanturuState == KANTURU_STATE_NIGHTMARE_BATTLE)
        {
            StopMp3(MUSIC_KANTURU_TOWER);
            StopMp3(MUSIC_KANTURU_MAYA_BATTLE);
            PlayMp3(MUSIC_KANTURU_NIGHTMARE_BATTLE);
            if (IsEndMp3())
                StopMp3(MUSIC_KANTURU_NIGHTMARE_BATTLE);
        }
        else
        {
            StopMp3(MUSIC_KANTURU_MAYA_BATTLE);
            StopMp3(MUSIC_KANTURU_NIGHTMARE_BATTLE);
            PlayMp3(MUSIC_KANTURU_TOWER);
            if (IsEndMp3())
                StopMp3(MUSIC_KANTURU_TOWER);
        }
    }
    else
    {
        StopMp3(MUSIC_KANTURU_MAYA_BATTLE);
        StopMp3(MUSIC_KANTURU_NIGHTMARE_BATTLE);
        StopMp3(MUSIC_KANTURU_TOWER);
    }
}

void GMKanturu3rd::AdvanceMayaLighting(OBJECT &object)
{
    if (!g_Direction.m_CKanturu.GetMayaExplotion())
    {
        Vector(1.f, 1.f, 1.f, object.Light);
        const float phase = sinf(WorldTime * 0.001f) * 0.12f;
        Vector(phase + 0.05f, phase + 0.3f, phase + 0.21f, object.StartPosition);
        return;
    }
    const float decay = powf(1.f / 1.02f, FPS_ANIMATION_FACTOR);
    VectorScale(object.Light, decay, object.Light);
    VectorScale(object.StartPosition, decay, object.StartPosition);
    if (object.Light[0] <= 0.05f)
    {
        Vector(0.05f, 0.05f, 0.05f, object.Light);
        object.HiddenMesh = -2;
    }
}

void GMKanturu3rd::AdvanceBattleRing(OBJECT &object)
{
    if (!IsInKanturu3rd() || g_Direction.m_CKanturu.m_iKanturuState != KANTURU_STATE_MAYA_BATTLE)
        return;
    if (object.Type != MODEL_BLADE_HUNTER && object.Type != MODEL_TWIN_TAIL &&
        object.Type != MODEL_DREADFEAR)
        return;
    constexpr float ringStep = 0.05f;
    constexpr float ringLimit = 3.f;
    constexpr float ringRestart = 0.1f;
    object.Distance += ringStep * FPS_ANIMATION_FACTOR;
    if (object.Distance >= ringLimit)
        object.Distance =
            ringRestart + std::fmod(object.Distance - ringLimit, ringLimit - ringRestart);
}

void GMKanturu3rd::AdvanceResultPresentation()
{
    if (!IsInKanturu3rd() || iKanturuResult == -1)
        return;
    constexpr float fadeStep = 0.01f;
    constexpr double holdMilliseconds = 5000.0;
    if (iKanturuResult != 0 && iKanturuResult != 1)
    {
        iKanturuResult = -1;
        fAlpha = 0.1f;
        return;
    }
    const float fadeFrames = std::min(FPS_ANIMATION_FACTOR, std::max(0.f, 1.f - fAlpha) / fadeStep);
    fAlpha = std::min(1.f, fAlpha + fadeStep * fadeFrames);
    resultDisplayMilliseconds_ += (FPS_ANIMATION_FACTOR - fadeFrames) *
                                  (1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps);
    if (resultDisplayMilliseconds_ >= holdMilliseconds)
    {
        fAlpha = 0.1f;
        iKanturuResult = -1;
    }
}

bool GMKanturu3rd::AdvanceObjectVisual(OBJECT *object, BMD *model, float)
{
    return AdvanceKanturu3rdObjectVisual(object, model);
}

bool GMKanturu3rd::AttackEffectMonster(CHARACTER *character, OBJECT *object, BMD *model)
{
    return AttackEffectKanturu3rdMonster(character, object, model);
}

void GMKanturu3rd::MoveBlurEffect(CHARACTER *character, OBJECT *object, BMD *model)
{
    MoveKanturu3rdBlurEffect(character, object, model);
}

bool GMKanturu3rd::MoveMonsterVisual(CHARACTER *, OBJECT *object, BMD *model,
                                     WorldCharacterVisualState &visual)
{
    return MoveKanturu3rdMonsterVisual(object, model, visual);
}

bool GMKanturu3rd::AdvanceMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                                        WorldCharacterVisualState &visual)
{
    return AdvanceKanturu3rdMonsterVisual(character, object, model, visual);
}

std::optional<bool> GMKanturu3rd::ObjectVisibility(const OBJECT &object, bool)
{
    if (g_Direction.m_CKanturu.IsMayaScene(gMapManager.ContextMap()) &&
        (object.Type == 0 || object.Type == MODEL_STORM2))
        return true;
    return std::nullopt;
}

void GMKanturu3rd::AdvanceEnvironment()
{
    AdvanceResultPresentation();
}

void GMKanturu3rd::UpdateMusic()
{
    ChangeBackGroundMusic(gMapManager.ContextMap());
}

bool GMKanturu3rd::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_KANTURU_TOWER) == 0 ||
           std::strcmp(track, MUSIC_KANTURU_MAYA_BATTLE) == 0 ||
           std::strcmp(track, MUSIC_KANTURU_NIGHTMARE_BATTLE) == 0;
}

void MapProcess::AdvancePlayerVisual(CHARACTER *character, OBJECT *object)
{
    if (BaseMap *const map = ContextBehavior())
        map->AdvancePlayerVisual(character, object);
}

bool MapProcess::CanObserveCharacter(const CHARACTER &character)
{
    BaseMap *const map = ContextBehavior();
    return !map || map->CanObserveCharacter(character);
}

void MapProcess::AdvanceTerrainEffects()
{
    if (BaseMap *const map = ContextBehavior())
        map->AdvanceTerrainEffects();
}

bool MapProcess::ActionObject(OBJECT *object)
{
    BaseMap *const map = ContextBehavior();
    return map && map->ActionObject(object);
}

void MapProcess::FinishObjectAction()
{
    if (BaseMap *const map = ContextBehavior())
        map->FinishObjectAction();
}

void MapProcess::PrepareObjectEffects(int &count, int previousVisible)
{
    if (BaseMap *const map = ContextBehavior())
        map->PrepareObjectEffects(count, previousVisible);
}

void MapProcess::MoveObjectEffects(OBJECT *object, int &count, int &visible)
{
    if (BaseMap *const map = ContextBehavior())
        map->MoveObjectEffects(object, count, visible);
}

void MapProcess::PrepareObjectLight(const ObjectDrawInput &object, BMD &model)
{
    if (BaseMap *const map = ContextBehavior())
        map->PrepareObjectLight(object, model);
}

ESound MapProcess::WalkingSound(int tile, bool safe) const
{
    BaseMap *const map = ContextBehavior();
    return map ? map->WalkingSound(tile, safe) : SOUND_HUMAN_WALK_GROUND;
}

int MapProcess::PlayerNpcText(bool actionChanged)
{
    BaseMap *const map = ContextBehavior();
    return map ? map->PlayerNpcText(actionChanged) : 0;
}

MapObjectInteraction MapProcess::ObjectInteraction(int type, CHARACTER &actor)
{
    BaseMap *const map = ContextBehavior();
    return map ? map->ObjectInteraction(type, actor) : MapObjectInteraction{};
}

bool MapProcess::ObjectVisible(const OBJECT &object, bool blockVisible)
{
    if (BaseMap *const map = ContextBehavior())
        if (const auto visible = map->ObjectVisibility(object, blockVisible))
            return *visible;
    return blockVisible && TestFrustrum2D(object.Position[0] * 0.01f, object.Position[1] * 0.01f,
                                          object.CollisionRange);
}

bool MapProcess::ObjectEffectsVisible(const OBJECT &object)
{
    BaseMap *const map = ContextBehavior();
    return object.Visible && (!map || map->ObjectEffectsVisible(object));
}

void MapProcess::AdvanceObjectVisibility(OBJECT &object)
{
    if (BaseMap *const map = ContextBehavior())
        map->AdvanceObjectVisibility(object);
}

void MapProcess::AdvanceObjectFade(OBJECT &object)
{
    if (BaseMap *const map = ContextBehavior())
        map->AdvanceObjectFade(object);
}

void MapProcess::AdvanceEnvironment()
{
    if (BaseMap *const map = ContextBehavior())
        map->AdvanceEnvironment();
}

int MapProcess::PrepareWeather()
{
    BaseMap *const map = ContextBehavior();
    return map ? map->PrepareWeather() : 80;
}

bool MapProcess::CreateWeather(PARTICLE *particle, int index)
{
    BaseMap *const map = ContextBehavior();
    return map && map->CreateWeather(particle, index);
}

bool MapProcess::MoveWeather(PARTICLE *particle)
{
    BaseMap *const map = ContextBehavior();
    return map && map->MoveWeather(particle);
}

bool MapProcess::WeatherEnabled() const
{
    switch (Presentation().weather)
    {
    case MapPresentationPolicy::WeatherAdmission::Enabled:
        return true;
    case MapPresentationPolicy::WeatherAdmission::OutsideTavern:
        return HeroTile != 4;
    case MapPresentationPolicy::WeatherAdmission::OutsideChurch:
        return HeroTile != 3 && HeroTile < 10;
    default:
        return false;
    }
}
void MapProcess::PrepareObjectUpdate(OBJECT *o)
{
    if (BaseMap *const map = ContextBehavior())
        map->PrepareObjectUpdate(o);
}

float MapProcess::ObjectAnimationSpeed(const OBJECT *object, const BMD &model, float speed) const
{
    const BaseMap *const map = ContextBehavior();
    return map ? map->ObjectAnimationSpeed(*object, model, speed) : speed;
}

bool MapProcess::AdvanceObjectVisual(OBJECT *o, BMD *b, float luminosity)
{
    BaseMap *const map = ContextBehavior();
    return map != nullptr && map->AdvanceObjectVisual(o, b, luminosity);
}

bool MapProcess::AttackEffectMonster(CHARACTER *c, OBJECT *o, BMD *b)
{
    BaseMap *const map = ContextBehavior();
    if (map && map->AttackEffectBeforeShared(c, o, b))
        return true;
    if (kanturu2nd_->AttackEffect_Kanturu2nd_Monster(c, o, b))
        return true;
    return map != nullptr && map->AttackEffectMonster(c, o, b);
}

bool MapProcess::PlayMonsterSound(OBJECT *o)
{
    BaseMap *const map = ContextBehavior();
    return map != nullptr && map->PlayMonsterSound(o);
}

bool CGMCryingWolf2nd::AdvanceCryingWolf2ndObjectVisual(OBJECT *pObject, BMD *pModel)
{
    if (!IsCyringWolf2nd())
        return false;

    vec3_t Light;

    switch (pObject->Type)
    {
    case 2:
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, pObject->Position, pObject->Angle, Light, 21,
                           pObject->Scale);
        }
        break;
    case 3:
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_TRUE_FIRE, pObject->Position, pObject->Angle, Light, 5,
                           pObject->Scale);
        }
        break;
    case 5:
        sessionKeeper_.Visual()->EmitAlternatingSmoke(*pObject, AlternatingSmokeStyle::Plain);
        break;
    case 6: {
        Vector(1.f, 1.f, 1.f, Light);
        Vector(0.2f, 0.2f, 0.2f, Light);

        //CreateParticle ( BITMAP_CLOUD, o->Position, o->Angle, Light, 8, o->Scale);
        if (pObject->HiddenMesh != -2)
        {
            CreateParticle(BITMAP_CLOUD, pObject->Position, pObject->Angle, Light, 1,
                           pObject->Scale, pObject);
            CreateParticle(BITMAP_CLOUD, pObject->Position, pObject->Angle, Light, 2,
                           pObject->Scale, pObject);
            CreateParticle(BITMAP_CLOUD, pObject->Position, pObject->Angle, Light, 3,
                           pObject->Scale, pObject);
            CreateParticle(BITMAP_CLOUD, pObject->Position, pObject->Angle, Light, 4,
                           pObject->Scale, pObject);
        }
        pObject->HiddenMesh = -2;
        //}
    }
    break;
    }

    return true;
}

bool CGMCryingWolf2nd::MoveCryingWolf2ndMonsterVisual(OBJECT *pObject, BMD *pModel,
                                                      WorldCharacterVisualState &visual)
{
    ObjectDrawInput presentation(pObject);
    visual.movement.Apply(presentation);
    if (!IsCyringWolf2nd())
        return false;
    switch (pObject->Type)
    {
    case MODEL_WEREWOLF_HERO: {
        vec3_t Position, Light;

        if (visual.action != MONSTER01_DIE)
        {
            Vector(0.9f, 0.2f, 0.1f, Light);
            GetBonePosition(presentation, CharacterSocket::Monster95_Head, Position);
            CreateSprite(BITMAP_LIGHT, Position, 3.5f, Light, pObject);
        }

        Vector(0.9f, 0.2f, 0.1f, Light);
        //. Walking & Running Scene Processing
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(10))
            {
                CreateParticle(BITMAP_SMOKE + 1, pObject->Position, pObject->Angle, Light);
            }
        }
    }
    break;
    case MODEL_SOLAM: {
        if (FPS_ANIMATION_FACTOR <= 0.f)
            break;
        constexpr std::array<std::pair<int, float>, 1> markers{{{MONSTER01_ATTACK2, 4.f}}};
        pObject->MotionTrace.VisitAnimationEvents(
            WorldTime, markers, [&](std::size_t, float fraction) {
                auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR *
                                                                     (1.f - fraction));
                vec3_t position, angle, light{1.f, 0.f, 0.5f};
                pObject->MotionTrace.Sample(WorldTime, fraction, pObject->Position, position);
                VectorCopy(pObject->Angle, angle);
                angle[2] = pObject->MotionTrace.SampleYaw(WorldTime, fraction, angle[2]);
                CreateEffect(MODEL_PIERCING2, position, angle, light, 1);
            });
    }
    break;
    case MODEL_VALAM: {
        vec3_t Position, Light;

        auto Rotation = (float)(WorldRandom() % 360);
        float Luminosity = sinf(WorldTime * 0.0012f) * 0.8f + 1.3f;

        float fScalePercent = 1.f;
        if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
            fScalePercent = .5f;

        GetBonePosition(presentation, CharacterSocket::Monster96_Center, Position);
        Vector(Luminosity * 0.f, Luminosity * 0.5f, Luminosity * 0.1f, Light);
        CreateSprite(BITMAP_LIGHT, Position, fScalePercent, Light, pObject);

        Vector(0.5f, 0.5f, 0.5f, Light);

        GetBonePosition(presentation, CharacterSocket::Monster96_Top, Position);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f * fScalePercent, Light, pObject, Rotation);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f * fScalePercent, Light, pObject,
                     360.f - Rotation);

        GetBonePosition(presentation, CharacterSocket::Monster96_Bottom, Position);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f * fScalePercent, Light, pObject, Rotation);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f * fScalePercent, Light, pObject,
                     360.f - Rotation);
    }
    break;
    }
    return false;
}

bool CGMCryingWolf2nd::AdvanceCryingWolf2ndMonsterVisual(CHARACTER *pCharacter, OBJECT *pObject,
                                                         BMD *pModel,
                                                         WorldCharacterVisualState &visual)
{
    return false;
}

void CGMCryingWolf2nd::MoveCryingWolf2ndBlurEffect(CHARACTER *pCharacter, OBJECT *pObject,
                                                   BMD *pModel)
{
    switch (pObject->Type)
    {
    case MODEL_WEREWOLF_HERO: {
        if (pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            vec3_t Light;
            Vector(1.f, 1.f, 1.f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = pModel->Actions[pObject->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = pObject->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                pModel->AnimationAtFrame(BoneTransform, fAnimationFrame,
                                         pObject->PriorAnimationFrame, pObject->PriorAction,
                                         pObject->Angle, pObject->HeadAngle);

                Vector(0.f, 0.f, -90.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);
                pModel->TransformPosition(BoneTransform[80], StartRelative, StartPos, false);
                pModel->TransformPosition(BoneTransform[80], EndRelative, EndPos, false);

                CreateBlur(pCharacter, StartPos, EndPos, Light, 3, true, 80);

                Vector(0.f, 0.f, 90.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);
                pModel->TransformPosition(BoneTransform[82], StartRelative, StartPos, false);
                pModel->TransformPosition(BoneTransform[82], EndRelative, EndPos, false);

                CreateBlur(pCharacter, StartPos, EndPos, Light, 3, true, 84);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_SOLAM: {
        if (pObject->CurrentAction == MONSTER01_ATTACK1)
        {
            vec3_t Light;
            Vector(1.f, 1.f, 1.f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = pModel->Actions[pObject->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = pObject->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                pModel->AnimationAtFrame(BoneTransform, fAnimationFrame,
                                         pObject->PriorAnimationFrame, pObject->PriorAction,
                                         pObject->Angle, pObject->HeadAngle);

                Vector(0.f, 0.f, 120.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);
                pModel->TransformPosition(BoneTransform[25], StartRelative, StartPos, false);
                pModel->TransformPosition(BoneTransform[25], EndRelative, EndPos, false);

                CreateBlur(pCharacter, StartPos, EndPos, Light, 3, true, 25);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    }
}

bool CGMCryingWolf2nd::AttackEffectCryingWolf2ndMonster(CHARACTER *pCharacter, OBJECT *pObject,
                                                        BMD *pModel)
{
    if (!IsCyringWolf2nd())
        return false;

    switch (pObject->Type)
    {
    case MODEL_VALAM: {
        if (pCharacter->CheckAttackTime(14))
        {
            CreateEffect(MODEL_ARROW_NATURE, pObject->Position, pObject->Angle, pObject->Light, 1,
                         pObject, pObject->PKKey);
            pCharacter->SetLastAttackEffectTime();
            return true;
        }
    }
    break;
    case MODEL_BALRAM: {
        if (pCharacter->CheckAttackTime(14))
        {
            CreateEffect(MODEL_ARROW_HOLY, pObject->Position, pObject->Angle, pObject->Light, 1,
                         pObject, pObject->PKKey);
            pCharacter->SetLastAttackEffectTime();
            return true;
        }
    }
    break;
    }
    return false;
}

bool CGMCryingWolf2nd::AdvanceObjectVisual(OBJECT *object, BMD *model, float)
{
    return AdvanceCryingWolf2ndObjectVisual(object, model);
}

bool CGMCryingWolf2nd::AttackEffectBeforeShared(CHARACTER *character, OBJECT *object, BMD *model)
{
    return AttackEffectCryingWolf2ndMonster(character, object, model);
}

bool CGMCryingWolf2nd::MoveMonsterVisual(CHARACTER *, OBJECT *object, BMD *model,
                                         WorldCharacterVisualState &visual)
{
    return MoveCryingWolf2ndMonsterVisual(object, model, visual);
}

bool CGMHuntingGround::AdvanceHuntingGroundObjectVisual(OBJECT *pObject, BMD *pModel)
{
    if (!IsInHuntingGround())
        return false;

    vec3_t Light;

    switch (pObject->Type)
    {
    case 3:
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_WATERFALL_3, pObject->Position, pObject->Angle, Light, 3,
                           pObject->Scale);
        }
        break;
    case 53:
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, pObject->Position, pObject->Angle, Light, 22,
                           pObject->Scale);
        }
        break;
    case 49: {
        PrepareWorldObjectPose(*pObject);
        vec3_t Relative, Position;
        Vector(0.f, -10.f, 0.f, Relative);
        pModel->TransformPosition(BoneTransform[3], Relative, Position, false);

        Vector(1.f, 0.f, 0.f, Light);
        float Luminosity = (float)sinf((WorldTime) * 0.002f) * 0.35f + 0.65f;
        CreateSprite(BITMAP_LIGHT, Position, Luminosity * 1.2f + 0.3f, Light, pObject);
        CreateSprite(BITMAP_LIGHT, Position, 0.5f, Light, pObject);
        Vector(0.3f, 0.3f, 0.3f, Light);
        CreateSprite(BITMAP_SHINY + 1, Position, 1.2f, Light, NULL, WorldRandom() % 360);
    }
    break;
    }

    return true;
}

bool CGMHuntingGround::MoveHuntingGroundMonsterVisual(OBJECT *pObject, BMD *pModel,
                                                      WorldCharacterVisualState &visual)
{
    switch (pObject->Type)
    {
    case MODEL_FIRE_GOLEM: {
        vec3_t Light;
        Vector(1.f, 0.2f, 0.1f, Light);
        AddTerrainLight(pObject->Position[0], pObject->Position[1], Light, 3, PrimaryTerrainLight);
    }
    break;
    }
    return false;
}
void CGMHuntingGround::MoveHuntingGroundBlurEffect(CHARACTER *pCharacter, OBJECT *pObject,
                                                   BMD *pModel)
{
    switch (pObject->Type)
    {
    case MODEL_AXE_HERO: {
        if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
            pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            vec3_t Light;
            Vector(1.f, 1.f, 1.f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = pModel->Actions[pObject->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = pObject->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                pModel->AnimationAtFrame(BoneTransform, fAnimationFrame,
                                         pObject->PriorAnimationFrame, pObject->PriorAction,
                                         pObject->Angle, pObject->HeadAngle);

                Vector(0.f, -10.f, -80.f, StartRelative);
                Vector(30.f, -30.f, -230.f, EndRelative);
                pModel->TransformPosition(BoneTransform[23], StartRelative, StartPos, false);
                pModel->TransformPosition(BoneTransform[23], EndRelative, EndPos, false);

                CreateBlur(pCharacter, StartPos, EndPos, Light, 3, true, 23);

                Vector(30.f, 10.f, 80.f, StartRelative);
                Vector(30.f, -65.f, 230, EndRelative);
                pModel->TransformPosition(BoneTransform[34], StartRelative, StartPos, false);
                pModel->TransformPosition(BoneTransform[34], EndRelative, EndPos, false);

                CreateBlur(pCharacter, StartPos, EndPos, Light, 3, true, 34);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    }
}
void CGMHuntingGround::EmitFireGolemEvents(OBJECT &object, BMD &model)
{
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    constexpr std::array<std::pair<int, float>, 3> markers{
        {{MONSTER01_ATTACK1, 8.4f}, {MONSTER01_ATTACK2, 5.f}, {MONSTER01_DIE, 0.f}}};
    AnimationPoseSample pose(&object, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    object.MotionTrace.VisitAnimationEvents(
        WorldTime, markers, [&](std::size_t event, float fraction) {
            auto birth =
                sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR * (1.f - fraction));
            if (event == 2)
            {
                PlayBuffer(SOUND_BC_FIREGOLEM_DIE);
                return;
            }
            vec3_t position, relative{}, origin, angle, sourceAngle, light{1.f, 1.f, 1.f};
            if (event == 1)
                Vector(80.f, 40.f, -25.f, relative);
            const int bone =
                event == 0 ? CharacterSocket::Monster82_RHand : CharacterSocket::Monster82_LHand;
            pose.SampleBonePosition(model, object, bone, relative, WorldTime, fraction, position);
            if (event == 1)
            {
                CreateBomb(position, true);
                PlayBuffer(SOUND_BC_FIREGOLEM_ATTACK2);
                return;
            }
            object.MotionTrace.Sample(WorldTime, fraction, object.Position, origin);
            position[2] = origin[2];
            VectorCopy(object.Angle, sourceAngle);
            sourceAngle[2] = object.MotionTrace.SampleYaw(WorldTime, fraction, sourceAngle[2]);
            Vector(-90.f, 0.f, sourceAngle[2], angle);
            CreateEffect(BITMAP_CRATER, position, angle, light, 2);
            CreateJoint(BITMAP_JOINT_FORCE, position, position, angle, 6, nullptr, 200.f);
            CreateEffect(BITMAP_FLAME, position, sourceAngle, object.Light, 3);
            for (int index = 0; index < CharactersClient.Size(); ++index)
            {
                if (!CharactersClient.IsValidIndex(index))
                    continue;
                auto &target = CharactersClient[index].Object;
                if (target.Kind != KIND_PLAYER || target.Type != MODEL_PLAYER || !target.Visible ||
                    !target.Live)
                    continue;
                vec3_t targetPosition;
                target.MotionTrace.Sample(WorldTime, fraction, target.Position, targetPosition);
                const float distance = VectorDistance2D(targetPosition, position);
                if (distance <= 200.f || distance >= 1100.f)
                    continue;
                CreateEffect(BITMAP_CRATER, targetPosition, angle, light, 2);
                CreateJoint(BITMAP_JOINT_FORCE, targetPosition, targetPosition, angle, 6, nullptr,
                            200.f);
                CreateEffect(BITMAP_FLAME, targetPosition, target.Angle, light, 3);
            }
            PlayBuffer(SOUND_BC_FIREGOLEM_ATTACK1);
        });
}

void CGMHuntingGround::EmitMonsterActionSounds(OBJECT &object)
{
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    std::array<ESound, 3> sounds;
    switch (object.Type)
    {
    case MODEL_LIZARD_WARRIOR:
        sounds = {SOUND_BC_LIZARDWARRIOR_ATTACK1, SOUND_BC_LIZARDWARRIOR_ATTACK1,
                  SOUND_BC_LIZARDWARRIOR_DIE};
        break;
    case MODEL_QUEEN_BEE:
        sounds = {SOUND_BC_QUEENBEE_ATTACK1, SOUND_BC_QUEENBEE_ATTACK1, SOUND_BC_AXEWARRIOR_DIE};
        break;
    case MODEL_POISON_GOLEM:
        sounds = {SOUND_BC_POISONGOLEM_ATTACK3, SOUND_BC_POISONGOLEM_ATTACK1,
                  SOUND_BC_POISONGOLEM_DIE};
        break;
    case MODEL_AXE_HERO:
        sounds = {SOUND_BC_AXEWARRIOR_ATTACK1, SOUND_BC_AXEWARRIOR_ATTACK1,
                  SOUND_BC_AXEWARRIOR_DIE};
        break;
    case MODEL_EROHIM:
        sounds = {SOUND_BC_EROHIM_ATTACK1, SOUND_BC_EROHIM_ATTACK3, SOUND_BC_EROHIM_DIE};
        break;
    default:
        return;
    }
    const std::array<std::pair<int, float>, 3> markers{
        {{MONSTER01_ATTACK1, 0.f},
         {MONSTER01_ATTACK2, object.Type == MODEL_POISON_GOLEM ? 3.5f : 0.f},
         {MONSTER01_DIE, 0.f}}};
    object.MotionTrace.VisitAnimationEvents(
        WorldTime, markers, [&](std::size_t event, float fraction) {
            const bool randomVariant = event < 2 &&
                                       !(object.Type == MODEL_POISON_GOLEM && event == 0) &&
                                       !(object.Type == MODEL_EROHIM && event == 1);
            PlayBuffer(
                static_cast<ESound>(sounds[event] + (randomVariant ? WorldRandom() % 2 : 0)));
            if (object.Type == MODEL_EROHIM && event == 1)
            {
                auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR *
                                                                     (1.f - fraction));
                vec3_t position, angle;
                object.MotionTrace.Sample(WorldTime, fraction, object.Position, position);
                VectorCopy(object.Angle, angle);
                angle[2] = object.MotionTrace.SampleYaw(WorldTime, fraction, angle[2]);
                CreateEffect(MODEL_SKILL_FISSURE, position, angle, object.Light, 0, &object);
            }
        });
}

bool CGMHuntingGround::AdvanceHuntingGroundMonsterVisual(CHARACTER *pCharacter, OBJECT *pObject,
                                                         BMD *pModel,
                                                         WorldCharacterVisualState &visual)
{
    EmitMonsterActionSounds(*pObject);
    switch (pObject->Type)
    {
    case MODEL_LIZARD_WARRIOR: {
        vec3_t Position, Light;
        Vector(0.9f, 0.2f, 0.1f, Light);
        GetBonePosition(pObject, CharacterSocket::Monster81_EyeRight, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.5f, Light, pObject);
        GetBonePosition(pObject, CharacterSocket::Monster81_EyeLeft, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.5f, Light, pObject);

        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
            {
                PlayBuffer(static_cast<ESound>(SOUND_BC_LIZARDWARRIOR_MOVE1 + WorldRandom() % 2));
            }
        }
        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;
    }
    break;
    case MODEL_FIRE_GOLEM: {
        EmitFireGolemEvents(*pObject, *pModel);
        vec3_t Light;
        Vector(1.f, 1.f, 1.f, Light);

        vec3_t Position, Relative;

        //. Dying Scene Processing
        if (visual.action == MONSTER01_DIE)
        {
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                const float fraction = birthTime.FrameFraction();
                PrepareWorldObjectPose(*pObject, fraction);
                OBB_t bounds{};
                pModel->Transform(BoneTransform, pObject->BoundingBoxMin, pObject->BoundingBoxMax,
                                  &bounds, false);
                EmitMeshEffects(*pModel, 0, MODEL_GOLEM_STONE);
            }

        }
        else
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                const float fraction = birthTime.FrameFraction();
                ObjectDrawInput draw(pObject);
                pObject->MotionTrace.Sample(WorldTime, fraction, pObject->Position, draw.position);
                draw.angle[2] = pObject->MotionTrace.SampleYaw(WorldTime, fraction, draw.angle[2]);
                AnimationPoseSample pose(draw, pModel->BoneHead, pModel->BodyHeight, false,
                                         pModel->PoseAssetIdentity());
                std::array<vec34_t, MAX_BONES> sampledBones;
                pose.EvaluateAtTime(*pModel, *pObject, WorldTime, fraction, sampledBones.data());
                draw.bones = sampledBones.data();
                Vector(5.f, 10.f, 5.f, Relative);
                GetBonePosition(draw, CharacterSocket::Monster82_LHand, Relative, Position);
                CreateParticle(BITMAP_TRUE_FIRE, Position, draw.angle, Light, 3, 3.4f, pObject);
                CreateParticle(BITMAP_SMOKE, Position, draw.angle, Light, 21, 2.5f);

                GetBonePosition(draw, CharacterSocket::Monster82_RHand, Position);
                CreateParticle(BITMAP_TRUE_FIRE, Position, draw.angle, Light, 4, 3.4f, pObject);
                CreateParticle(BITMAP_SMOKE, Position, draw.angle, Light, 21, 2.5f);

                Vector(-20.f, -2.f, -10.f, Relative);
                GetBonePosition(draw, CharacterSocket::Monster82_Eye, Relative, Position);
                CreateParticle(BITMAP_TRUE_FIRE, Position, draw.angle, Light, 0, 0.8f);
                CreateParticle(BITMAP_SMOKE, Position, draw.angle, Light, 21, 0.8f);

                GetBonePosition(draw, CharacterSocket::Monster82_Back, Position);
                CreateParticle(BITMAP_SMOKE, Position, draw.angle, Light, 21, 0.8f);

                if (sessionKeeper_.Random()->FpsCheck(20, 1.f))
                {
                    PlayBuffer(static_cast<ESound>(SOUND_BC_FIREGOLEM_MOVE1 + WorldRandom() % 2));
                }
            }

    }
    break;
    case MODEL_QUEEN_BEE: {
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(static_cast<ESound>(SOUND_BC_QUEENBEE_MOVE1 + WorldRandom() % 2));
            }
        }
        if (visual.action == MONSTER01_ATTACK2)
        {
            vec3_t Position, Relative;
            Vector(WorldRandom() % 12 - 6, WorldRandom() % 12 - 6, WorldRandom() % 12 - 6,
                   Relative);
            GetBonePosition(pObject, CharacterSocket::Monster83_Tail, Relative, Position);

            vec3_t Light;
            Vector(1.f, 0.3f, 0.f, Light);

            CreateSprite(BITMAP_SHINY + 1, Position, 2.5f, pObject->Light, pObject, 0.f, 1);
            CreateSprite(BITMAP_MAGIC + 1, Position, 0.8f, pObject->Light, pObject, 0.f);
            CreateParticleFpsChecked(BITMAP_ENERGY, Position, pObject->Angle, Light);
            CreateParticleFpsChecked(BITMAP_ENERGY, Position, pObject->Angle, Light);
        }
        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;
    }
    break;

    case MODEL_POISON_GOLEM: {
        vec3_t Light, Position;

        Vector(0.4f, 1.0f, 0.7f, Light);

        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (visual.soundSubType == FALSE)
            {
                visual.soundSubType = TRUE;
                PlayBuffer(static_cast<ESound>(SOUND_BC_POISONGOLEM_MOVE1 + WorldRandom() % 2));
            }
        }

        //. Dying Scene Processing
        if (visual.action == MONSTER01_DIE)
        {
            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                const float fraction = birthTime.FrameFraction();
                PrepareWorldObjectPose(*pObject, fraction);
                OBB_t bounds{};
                pModel->Transform(BoneTransform, pObject->BoundingBoxMin, pObject->BoundingBoxMax,
                                  &bounds, false);
                EmitMeshEffects(*pModel, 0, MODEL_GOLEM_STONE, 1);
            }

        }

        //. Attack Scene Processing
        if (visual.action == MONSTER01_ATTACK1)
        {

            for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            {
                const float fraction = birthTime.FrameFraction();
                const double time =
                    WorldTime -
                    birthTime.SceneRemainingFrames() *
                        (1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps);
                vec3_t angle, relative{};
                Vector(-55.f, sinf(float(time * 0.03)) * 45.f,
                       pObject->MotionTrace.SampleYaw(WorldTime, fraction, pObject->Angle[2]),
                       angle);
                const int bone = sessionKeeper_.Random()->FpsCheck(2, 1.f)
                                     ? CharacterSocket::Monster84_PoisonRight
                                     : CharacterSocket::Monster84_PoisonLeft;
                AnimationPoseSample pose(pObject, pModel->BoneHead, pModel->BodyHeight, false,
                                         pModel->PoseAssetIdentity());
                pose.SampleBonePosition(*pModel, *pObject, bone, relative, WorldTime, fraction,
                                        Position);
                if (sessionKeeper_.Random()->FpsCheck(2, 1.f))
                    CreateParticle(BITMAP_SMOKE, Position, pObject->Angle, Light, 11,
                                   (float)(WorldRandom() % 32 + 50) * 0.025f);
                CreateEffect(MODEL_BIG_STONE_PART2, Position, angle, Light, 3);
            }
        }
        if (visual.action == MONSTER01_ATTACK2 && visual.animationFrame >= 3.5f &&
            visual.animationFrame <= 4.2f)
        {

            GetBonePosition(pObject, CharacterSocket::Monster84_RightHand, Position);
            Position[2] = pObject->Position[2];
            CreateParticleFpsChecked(BITMAP_SMOKE, Position, pObject->Angle, Light, 11,
                                     (float)(WorldRandom() % 32 + 50) * 0.05f);

            GetBonePosition(pObject, CharacterSocket::Monster84_LeftHand, Position);
            Position[2] = pObject->Position[2];
            CreateParticleFpsChecked(BITMAP_SMOKE, Position, pObject->Angle, Light, 11,
                                     (float)(WorldRandom() % 32 + 50) * 0.05f);
        }

        for (const auto bone :
             {CharacterSocket::Monster84_PoisonTop, CharacterSocket::Monster84_PoisonRight,
              CharacterSocket::Monster84_PoisonLeft})
        {
            for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.f))
            {
                const float fraction = emission.FrameFraction();
                vec3_t relative{};
                AnimationPoseSample pose(pObject, pModel->BoneHead, pModel->BodyHeight, false,
                                         pModel->PoseAssetIdentity());
                pose.SampleBonePosition(*pModel, *pObject, bone, relative, WorldTime, fraction,
                                        Position);
                CreateParticle(BITMAP_SMOKE, Position, pObject->Angle, Light, 23);
            }
        }
        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;
    }
    break;

    case MODEL_AXE_HERO: {
        vec3_t Position, Light;
        Vector(0.9f, 0.2f, 0.1f, Light);
        GetBonePosition(pObject, CharacterSocket::Monster85_LeftEye, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.7f, Light, pObject);
        GetBonePosition(pObject, CharacterSocket::Monster85_RightEye, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.7f, Light, pObject);

        //. Walking & Running Scene Processing
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 10.f))
            {
                const float fraction = emission.FrameFraction();
                pObject->MotionTrace.Sample(WorldTime, fraction, pObject->Position, Position);
                CreateParticle(BITMAP_SMOKE + 1, Position, pObject->Angle, Light);
                PlayBuffer(static_cast<ESound>(SOUND_BC_AXEWARRIOR_MOVE1 + WorldRandom() % 2));
            }
        }
        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;
    }
    break;
    case MODEL_EROHIM: {
        vec3_t Position, Relative, Light;
        Vector(0.9f, 0.2f, 0.1f, Light);
        Vector(0.f, -2.f, 0.f, Relative);
        GetBonePosition(pObject, CharacterSocket::Monster87_LeftEye, Relative, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.5f, Light, pObject);
        CreateSprite(BITMAP_LIGHT, Position, 0.5f, Light, pObject);
        GetBonePosition(pObject, CharacterSocket::Monster87_RightEye, Relative, Position);
        CreateSprite(BITMAP_LIGHT, Position, 0.5f, Light, pObject);
        CreateSprite(BITMAP_LIGHT, Position, 0.5f, Light, pObject);


        if (visual.action == MONSTER01_ATTACK2)
        {
            Vector(0.f, 0.f, -10.f, Relative);
            GetBonePosition(pObject, CharacterSocket::Monster87_LeftHand, Relative, Position);
            CreateParticleFpsChecked(BITMAP_TRUE_FIRE, Position, pObject->Angle, Light, 3, 3.f,
                                     pObject);
            CreateParticleFpsChecked(BITMAP_SMOKE, Position, pObject->Angle, Light, 21, 2.f);
        }
        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2)
            visual.soundSubType = FALSE;
    }
    break;
    }
    return false;
}

bool CGMHuntingGround::AttackEffectHuntingGroundMonster(CHARACTER *pCharacter, OBJECT *pObject,
                                                        BMD *pModel)
{
    if (!IsInHuntingGround())
        return false;

    switch (pCharacter->MonsterIndex)
    {
    case MONSTER_LIZARD_WARRIOR:
        break;
    case MONSTER_QUEEN_BEE:
    case MONSTER_GIGAS_GOLEM:
    case MONSTER_POISON_GOLEM:
    case MONSTER_FIRE_GOLEM:
    case MONSTER_EROHIM:
        return true;
    case MONSTER_AXE_HERO:
    case MONSTER_AXE_WARRIOR:
        break;
    }
    return false;
}

bool CGMHuntingGround::CreateMist(PARTICLE *pParticleObj)
{
    if (!IsInHuntingGround())
        return false;
    if (!IsInHuntingGroundSection2(Hero->Object.Position))
        return false;

    pParticleObj->Live = false;
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 30.f))
    {
        vec3_t origin, angle;
        Hero->Object.MotionTrace.Sample(WorldTime, birth.FrameFraction(), Hero->Object.Position,
                                        origin);
        VectorCopy(Hero->Object.Angle, angle);
        angle[2] = Hero->Object.MotionTrace.SampleYaw(WorldTime, birth.FrameFraction(), angle[2]);
        vec3_t Light;
        Vector(0.05f, 0.05f, 0.1f, Light);

        vec3_t TargetPosition = {0.f, 0.f, 0.f}, TargetAngle = {0.f, 0.f, 0.f};
        switch (WorldRandom() % 8)
        {
        case 0:
            TargetPosition[0] = origin[0] + (300 + WorldRandom() % 250);
            TargetPosition[1] = origin[1] + (300 + WorldRandom() % 250);
            break;
        case 1:
            TargetPosition[0] = origin[0] + (250 + WorldRandom() % 250);
            TargetPosition[1] = origin[1] - (250 + WorldRandom() % 250);
            break;
        case 2:
            TargetPosition[0] = origin[0] - (200 + WorldRandom() % 250);
            TargetPosition[1] = origin[1] + (200 + WorldRandom() % 250);
            break;
        case 3:
            TargetPosition[0] = origin[0] - (300 + WorldRandom() % 250);
            TargetPosition[1] = origin[1] - (300 + WorldRandom() % 250);
            break;
        case 4:
            TargetPosition[0] = origin[0] + (400 + WorldRandom() % 250);
            TargetPosition[1] = origin[1];
            break;
        case 5:
            TargetPosition[0] = origin[0] - (400 + WorldRandom() % 250);
            TargetPosition[1] = origin[1];
            break;
        case 6:
            TargetPosition[0] = origin[0];
            TargetPosition[1] = origin[1] + (400 + WorldRandom() % 250);
            break;
        case 7:
            TargetPosition[0] = origin[0];
            TargetPosition[1] = origin[1] - (400 + WorldRandom() % 250);
            break;
        }

        if (Hero->Movement)
        {
            float Matrix[3][4];
            AngleMatrix(angle, Matrix);
            vec3_t Velocity, Direction;
            Vector(0.f, -45.f * CharacterMoveSpeed(Hero), 0.f, Velocity);
            VectorRotate(Velocity, Matrix, Direction);
            VectorAdd(TargetPosition, Direction, TargetPosition);
        }
        if (Hero->Movement || (WorldRandom() % 2 == 0))
        {
            TargetPosition[2] =
                (WorldRandom() % 20) + RequestTerrainHeight(TargetPosition[0], TargetPosition[1]);
            CreateParticle(BITMAP_CLOUD, TargetPosition, TargetAngle, Light, 8, 0.4f);
        }
    }

    return true;
}

bool CGMHuntingGround::AdvanceObjectVisual(OBJECT *object, BMD *model, float)
{
    return AdvanceHuntingGroundObjectVisual(object, model);
}

bool CGMHuntingGround::AttackEffectBeforeShared(CHARACTER *character, OBJECT *object, BMD *model)
{
    return AttackEffectHuntingGroundMonster(character, object, model);
}

void CGMHuntingGround::UpdateMusic()
{
    PlayMp3(MUSIC_BC_HUNTINGGROUND);
}

bool CGMHuntingGround::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_BC_HUNTINGGROUND) == 0;
}

bool CGMHuntingGround::CreateWeather(PARTICLE *particle, int)
{
    return CreateMist(particle);
}

using namespace SEASON3C;

bool GMSwampOfQuiet::AdvanceObjectVisual(OBJECT *pObject, BMD *pModel, float)
{
    if (!IsCurrentMap())
        return false;

    vec3_t Light;

    switch (pObject->Type)
    {
    case 57: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_TRUE_FIRE, pObject->Position, pObject->Angle, Light, 5,
                           pObject->Scale);
            CreateParticle(BITMAP_SMOKE, pObject->Position, pObject->Angle, Light, 21,
                           pObject->Scale);
        }
    }
    break;
    case 71: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_TRUE_FIRE, pObject->Position, pObject->Angle, Light, 5,
                           pObject->Scale);
        }
    }
    break;
    case 72: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 6.f))
        {
            vec3_t Position;
            VectorCopy(pObject->Position, Position);
            Position[2] += 50.0f;

            Vector(0.03f, 0.03f, 0.03f, Light);
            CreateParticle(BITMAP_SMOKE, Position, pObject->Angle, Light, 49, pObject->Scale);
        }
    }
    break;
    case 73:
        break;
    case 74: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, pObject->Position, pObject->Angle, Light, 21,
                           pObject->Scale * 2.0f);
        }
    }
    break;
    case 77: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 6.f))
        {
            Vector(0.04f, 0.06f, 0.03f, Light);
            CreateParticle(BITMAP_CLOUD, pObject->Position, pObject->Angle, Light, 20,
                           pObject->Scale, pObject);
        }
    }
    break;
    case 78: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 6.f))
        {
            Vector(0.03f, 0.03f, 0.05f, Light);
            CreateParticle(BITMAP_CLOUD, pObject->Position, pObject->Angle, Light, 20,
                           pObject->Scale, pObject);
        }
    }
    break;
    }
    return true;
}

bool GMSwampOfQuiet::MoveMonsterVisual(CHARACTER *, OBJECT *pObject, BMD *pModel,
                                       WorldCharacterVisualState &visual)
{
    ObjectDrawInput presentation(pObject);
    visual.movement.Apply(presentation);
    if (!IsCurrentMap())
        return false;

    switch (pObject->Type)
    {
    case MODEL_NAPIN:
        if (visual.action == MONSTER01_ATTACK2)
        {
            BMD *pModel = &Models[pObject->Type];
            float fActionSpeed =
                pModel->Actions[visual.action].PlaySpeed * static_cast<float>(FPS_ANIMATION_FACTOR);
            float Start_Frame = 7.2f;
            float End_Frame = Start_Frame + fActionSpeed;
            if (visual.animationFrame >= Start_Frame && visual.animationFrame < End_Frame)
            {
                vec3_t vLook, vPosition, vLight;
                float matRotate[3][4];
                Vector(1.f, 1.f, 1.f, vLight);
                Vector(0.0f, -250.0f, 0.0f, vPosition);
                AngleMatrix(pObject->Angle, matRotate);
                VectorRotate(vPosition, matRotate, vLook);
                VectorAdd(pObject->Position, vLook, vPosition);

                CreateEffectFpsChecked(BITMAP_CRATER, vPosition, pObject->Angle, vLight, 2, NULL,
                                       -1, 0, 0, 0, 1.5f);
                for (int iu = 0; iu < 20; iu++)
                {
                    //CreateEffect ( MODEL_BIG_STONE1, vPosition,pObject->Angle,visual.movement.light,10);
                    CreateEffectFpsChecked(MODEL_STONE2, vPosition, pObject->Angle,
                                           visual.movement.light);
                }
                Vector(0.7f, 0.7f, 1.f, vLight);
                CreateParticleFpsChecked(BITMAP_CLUD64, vPosition, pObject->Angle, vLight, 7, 2.0f);
                CreateParticleFpsChecked(BITMAP_CLUD64, vPosition, pObject->Angle, vLight, 7, 2.0f);

                Vector(0.3f, 0.2f, 1.f, vLight);
                CreateEffectFpsChecked(BITMAP_SHOCK_WAVE, vPosition, pObject->Angle, vLight, 11);
                CreateEffectFpsChecked(BITMAP_SHOCK_WAVE, vPosition, pObject->Angle, vLight, 11);

                vPosition[2] += 100.0f;
                Vector(0.0f, 0.2f, 1.0f, vLight);
                CreateEffectFpsChecked(MODEL_EFFECT_THUNDER_NAPIN_ATTACK_1, vPosition,
                                       pObject->Angle, vLight, 0);
            }
        }
        break;
    case MODEL_GHOST_NAPIN:
        if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
        {
            float Start_Frame = 7.0f;
            float End_Frame = Start_Frame + 0.8f;
            if (visual.animationFrame >= Start_Frame)
            {
                vec3_t vLook, vPosition, vLight;
                float matRotate[3][4];
                Vector(0.4f, 1.0f, 0.4f, vLight);
                Vector(0.0f, -150.0f, 100.0f, vPosition);
                AngleMatrix(pObject->Angle, matRotate);
                VectorRotate(vPosition, matRotate, vLook);
                VectorAdd(pObject->Position, vLook, vPosition);

                vec3_t vSmokePosition;
                for (int i = 0; i < 2; ++i)
                {
                    Vector(vPosition[0] + (WorldRandom() % 20 - 10) * 1.0f,
                           vPosition[1] + (WorldRandom() % 20 - 10) * 1.0f,
                           vPosition[2] + (WorldRandom() % 20 - 10) * 1.0f, vSmokePosition);
                    CreateParticleFpsChecked(BITMAP_SMOKE, vSmokePosition, pObject->Angle, vLight,
                                             51);
                }
                if (visual.animationFrame < End_Frame)
                {
                    Vector(4.0f, 10.0f, 4.0f, vLight);
                    CreateParticleFpsChecked(BITMAP_SHOCK_WAVE, vPosition, pObject->Angle, vLight,
                                             3, 0.5f);
                    CreateParticleFpsChecked(BITMAP_SHOCK_WAVE, vPosition, pObject->Angle, vLight,
                                             3, 0.8f);
                }

                Vector(0.0f, -100.0f, 100.0f, vPosition);
                AngleMatrix(pObject->Angle, matRotate);
                VectorRotate(vPosition, matRotate, vLook);
                VectorAdd(pObject->Position, vLook, vPosition);
                CreateJointFpsChecked(BITMAP_JOINT_ENERGY, vPosition, pObject->Position,
                                      pObject->Angle, 46, pObject, 20.0f);
            }
        }
        break;
    case MODEL_BLAZE_NAPIN:
        if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
        {
            BMD *pModel = &Models[pObject->Type];
            float fActionSpeed =
                pModel->Actions[visual.action].PlaySpeed * static_cast<float>(FPS_ANIMATION_FACTOR);
            float Start_Frame = 5.5f;
            float End_Frame = Start_Frame + fActionSpeed;
            if (visual.animationFrame >= Start_Frame && visual.animationFrame < End_Frame)
            {
                vec3_t vLook, vPosition, vLight, vLightFire;
                float matRotate[3][4];
                Vector(1.f, 1.f, 1.f, vLight);
                Vector(1.0f, 0.2f, 0.0f, vLightFire);
                Vector(0.0f, -150.0f, 0.0f, vPosition);
                AngleMatrix(pObject->Angle, matRotate);
                VectorRotate(vPosition, matRotate, vLook);
                VectorAdd(pObject->Position, vLook, vPosition);

                CreateEffectFpsChecked(BITMAP_FIRE_CURSEDLICH, vPosition, pObject->Angle, vLight, 1,
                                       pObject);

                CreateEffectFpsChecked(BITMAP_CRATER, vPosition, pObject->Angle, vLight, 2, NULL,
                                       -1, 0, 0, 0, 1.5f);
                for (int iu = 0; iu < 20; iu++)
                {
                    CreateEffectFpsChecked(MODEL_STONE2, vPosition, pObject->Angle,
                                           visual.movement.light);
                }

                Vector(0.5f, 0.1f, 0.0f, vLight);
                CreateEffectFpsChecked(BITMAP_SHOCK_WAVE, vPosition, pObject->Angle, vLight, 12,
                                       NULL, -1, 0, 0, 0, 0.1f);
            }
        }
        break;
    case MODEL_MEDUSA: {
        vec3_t vPos, vColor;

        switch (visual.action)
        {
        case MONSTER01_WALK:
            Vector(pObject->Position[0] + WorldRandom() % 200 - 100,
                   pObject->Position[1] + WorldRandom() % 200 - 100, pObject->Position[2], vPos);
            CreateParticleFpsChecked(BITMAP_SMOKE + 1, vPos, pObject->Angle, visual.movement.light);
            break;

        case MONSTER01_ATTACK1:
        case MONSTER01_ATTACK2:
            if (2 < pModel->CurrentAnimationFrame)
            {
                vec3_t vAngle, vRel;
                VectorCopy(pObject->Angle, vAngle);
                Vector(10.0f, 5.0f, 0.0f, vRel);

                int temp[] = {19, 31};
                for (int i = 0; i < 2; i++)
                {
                    pModel->TransformByObjectBone(vPos, presentation, temp[i], vRel);

                    Vector(0.0f, 1.0f, 0.5f, vColor);
                    for (int i = 0; i < 2; ++i)
                    {
                        if (i == 1 && rand_fps_check(2))
                            continue;

                        switch (WorldRandom() % 3)
                        {
                        case 0:
                            CreateParticleFpsChecked(BITMAP_FIRE_HIK1_MONO, vPos, vAngle, vColor, 4,
                                                     pObject->Scale, pObject);
                            break;
                        case 1:
                            CreateParticleFpsChecked(BITMAP_FIRE_HIK2_MONO, vPos, vAngle, vColor, 8,
                                                     pObject->Scale, pObject);
                            break;
                        case 2:
                            CreateParticleFpsChecked(BITMAP_FIRE_HIK3_MONO, vPos, vAngle, vColor, 5,
                                                     pObject->Scale, pObject);
                            break;
                        }
                    }

                    Vector(1.0f, 1.0f, 1.0f, vColor);
                    CreateSprite(BITMAP_HOLE, vPos, (sinf(WorldTime * 0.005f) + 1.0f) * 0.1f + 0.1f,
                                 vColor, pObject);
                }
            }
            break;
        }
    }
    break;
    case MODEL_ICE_NAPIN:
        if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
        {
            BMD *pModel = &Models[pObject->Type];
            float fActionSpeed =
                pModel->Actions[visual.action].PlaySpeed * static_cast<float>(FPS_ANIMATION_FACTOR);
            float Start_Frame = 5.5f;
            float End_Frame = Start_Frame + fActionSpeed;
            if (visual.animationFrame >= Start_Frame && visual.animationFrame < End_Frame)
            {
                vec3_t vLook, vPosition, vLight, vLightFire;
                float matRotate[3][4];
                Vector(0.25f, 0.6f, 0.7f, vLight);
                Vector(1.0f, 1.0f, 1.0f, vLightFire);
                Vector(0.0f, -150.0f, 0.0f, vPosition);
                AngleMatrix(pObject->Angle, matRotate);
                VectorRotate(vPosition, matRotate, vLook);
                VectorAdd(pObject->Position, vLook, vPosition);

                CreateEffectFpsChecked(BITMAP_FIRE_CURSEDLICH, vPosition, pObject->Angle, vLight, 1,
                                       pObject);

                CreateEffectFpsChecked(BITMAP_CRATER, vPosition, pObject->Angle, vLight, 2, NULL,
                                       -1, 0, 0, 0, 1.5f);
                for (int iu = 0; iu < 20; iu++)
                {
                    CreateEffectFpsChecked(MODEL_STONE2, vPosition, pObject->Angle,
                                           visual.movement.light);
                }

                Vector(0.5f, 0.1f, 0.0f, vLight);
                CreateEffectFpsChecked(BITMAP_SHOCK_WAVE, vPosition, pObject->Angle, vLight, 12,
                                       NULL, -1, 0, 0, 0, 0.1f);
            }
        }
        break;
    }
    return false;
}

void GMSwampOfQuiet::MoveSharedMonsterBlur(CHARACTER *pCharacter, OBJECT *pObject, BMD *pModel)
{
    switch (pObject->Type)
    {
    case MODEL_SAPIUNUS: {
        float Start_Frame = 6.f;
        float End_Frame = 7.6f;
        if ((pObject->AnimationFrame >= Start_Frame && pObject->AnimationFrame <= End_Frame &&
             pObject->CurrentAction == MONSTER01_ATTACK1) ||
            (pObject->AnimationFrame >= Start_Frame && pObject->AnimationFrame <= End_Frame &&
             pObject->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t vLight;
            Vector(1.0f, 0.6f, 0.1f, vLight);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = pModel->Actions[pObject->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = pObject->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                pModel->AnimationAtFrame(BoneTransform, fAnimationFrame,
                                         pObject->PriorAnimationFrame, pObject->PriorAction,
                                         pObject->Angle, pObject->HeadAngle);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);
                pModel->TransformPosition(BoneTransform[42], StartRelative, StartPos, false);

                pModel->TransformPosition(BoneTransform[34], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 0);
                pModel->TransformPosition(BoneTransform[35], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 1);
                pModel->TransformPosition(BoneTransform[38], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 2);
                pModel->TransformPosition(BoneTransform[39], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 3);
                pModel->TransformPosition(BoneTransform[43], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 4);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_SAPIDUO: {
        float Start_Frame = 6.f;
        float End_Frame = 7.f;
        if ((pObject->AnimationFrame >= Start_Frame && pObject->AnimationFrame <= End_Frame &&
             pObject->CurrentAction == MONSTER01_ATTACK1) ||
            (pObject->AnimationFrame >= Start_Frame && pObject->AnimationFrame <= End_Frame &&
             pObject->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t vLight;
            Vector(2.0f, 0.0f, 0.0f, vLight);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = pModel->Actions[pObject->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = pObject->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                pModel->AnimationAtFrame(BoneTransform, fAnimationFrame,
                                         pObject->PriorAnimationFrame, pObject->PriorAction,
                                         pObject->Angle, pObject->HeadAngle);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);
                pModel->TransformPosition(BoneTransform[42], StartRelative, StartPos, false);

                pModel->TransformPosition(BoneTransform[34], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 0);
                pModel->TransformPosition(BoneTransform[35], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 1);
                pModel->TransformPosition(BoneTransform[38], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 2);
                pModel->TransformPosition(BoneTransform[39], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 3);
                pModel->TransformPosition(BoneTransform[43], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 4);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_SAPITRES: {
        float Start_Frame = 5.f;
        float End_Frame = 10.f;
        if ((pObject->AnimationFrame >= Start_Frame && pObject->AnimationFrame <= End_Frame &&
             pObject->CurrentAction == MONSTER01_ATTACK1) ||
            (pObject->AnimationFrame >= Start_Frame && pObject->AnimationFrame <= End_Frame &&
             pObject->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t vLight;
            Vector(0.3f, 0.7f, 1.0f, vLight);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = pModel->Actions[pObject->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = pObject->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                pModel->AnimationAtFrame(BoneTransform, fAnimationFrame,
                                         pObject->PriorAnimationFrame, pObject->PriorAction,
                                         pObject->Angle, pObject->HeadAngle);
                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);

                // 왼손
                pModel->TransformPosition(BoneTransform[42], StartRelative, StartPos, false);

                pModel->TransformByBoneMatrix(EndPos, BoneTransform[34]);
                //					pModel->TransformPosition(BoneTransform[34], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 0);
                pModel->TransformPosition(BoneTransform[35], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 1);
                pModel->TransformPosition(BoneTransform[38], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 2);
                pModel->TransformPosition(BoneTransform[39], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 3);
                pModel->TransformPosition(BoneTransform[43], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 4);

                pModel->TransformPosition(BoneTransform[75], StartRelative, StartPos, false);

                pModel->TransformPosition(BoneTransform[76], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 5);
                pModel->TransformPosition(BoneTransform[79], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 6);
                pModel->TransformPosition(BoneTransform[80], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 7);
                pModel->TransformPosition(BoneTransform[83], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 8);
                pModel->TransformPosition(BoneTransform[84], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 9);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_NAPIN:
        break;
    case MODEL_SAPI_QUEEN:
    case MODEL_WOLF_STATUS: {
        float Start_Frame = 6.f;
        float End_Frame = 7.6f;
        if ((pObject->AnimationFrame >= Start_Frame && pObject->AnimationFrame <= End_Frame &&
             pObject->CurrentAction == MONSTER01_ATTACK1) ||
            (pObject->AnimationFrame >= Start_Frame && pObject->AnimationFrame <= End_Frame &&
             pObject->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t vLight;
            Vector(1.0f, 0.6f, 0.1f, vLight);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = pModel->Actions[pObject->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = pObject->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                pModel->AnimationAtFrame(BoneTransform, fAnimationFrame,
                                         pObject->PriorAnimationFrame, pObject->PriorAction,
                                         pObject->Angle, pObject->HeadAngle);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);
                pModel->TransformPosition(BoneTransform[42], StartRelative, StartPos, false);

                pModel->TransformPosition(BoneTransform[34], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 0);
                pModel->TransformPosition(BoneTransform[35], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 1);
                pModel->TransformPosition(BoneTransform[38], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 2);
                pModel->TransformPosition(BoneTransform[39], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 3);
                pModel->TransformPosition(BoneTransform[43], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 4);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    }
}

bool GMSwampOfQuiet::AdvanceMonsterVisual(CHARACTER *pCharacter, OBJECT *pObject, BMD *pModel,
                                          WorldCharacterVisualState &visual)
{
    if (!IsCurrentMap())
        return false;

    vec3_t vPos, vRelative, vLight;

    switch (pObject->Type)
    {
    case MODEL_SAPIUNUS: {
        Vector(1.0f, 0.6f, 0.1f, vLight);
        Vector(0.0f, 0.0f, 0.0f, vRelative);
        pModel->TransformPosition(pObject->BoneTransform[15], vRelative, vPos, true);

        CreateSprite(BITMAP_LIGHT, vPos, 2.0f, vLight, pObject);
        CreateSprite(BITMAP_LIGHT, vPos, 2.0f, vLight, pObject);
    }
    break;
    case MODEL_SAPIDUO: {
        Vector(1.0f, 0.0f, 0.0f, vLight);
        Vector(0.0f, 0.0f, 0.0f, vRelative);
        pModel->TransformPosition(pObject->BoneTransform[15], vRelative, vPos, true);

        CreateSprite(BITMAP_LIGHT, vPos, 2.0f, vLight, pObject);
        CreateSprite(BITMAP_LIGHT, vPos, 2.0f, vLight, pObject);
    }
    break;
    case MODEL_SAPITRES: {
        Vector(0.2f, 0.7f, 1.0f, vLight);
        Vector(0.0f, 0.0f, 0.0f, vRelative);
        pModel->TransformPosition(pObject->BoneTransform[15], vRelative, vPos, true);

        CreateSprite(BITMAP_LIGHT, vPos, 2.0f, vLight, pObject);
        CreateSprite(BITMAP_LIGHT, vPos, 2.0f, vLight, pObject);
    }
    break;
    case MODEL_SHADOW_PAWN: {
        int iBones[] = {11, 15, 34, 21, 25, 39};
        Vector(1.0f, 0.1f, 0.1f, vLight);
        for (int i = 0; i < 6; ++i)
        {
            if (WorldRandom() % 6 > 0)
                continue;
            pModel->TransformByObjectBone(vPos, pObject, iBones[i]);
            CreateParticleFpsChecked(BITMAP_SMOKE, vPos, pObject->Angle, vLight, 50, 1.0f);
            CreateParticleFpsChecked(BITMAP_SMOKELINE1 + WorldRandom() % 3, vPos, pObject->Angle,
                                     vLight, 0, 1.0f);
        }
    }
        if (visual.action == MONSTER01_DIE)
        {
            if (visual.emissionLifeTime == 100)
            {
                visual.emissionLifeTime = 90;

                vec3_t vWorldPos, Light;
                Vector(1.0f, 1.0f, 1.0f, Light);

                pModel->TransformByObjectBone(vWorldPos, pObject, 34);
                CreateEffect(MODEL_SHADOW_PAWN_ANKLE_LEFT, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 39);
                CreateEffect(MODEL_SHADOW_PAWN_ANKLE_RIGHT, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 0);
                CreateEffect(MODEL_SHADOW_PAWN_BELT, vWorldPos, pObject->Angle, Light, 0, pObject,
                             0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 4);
                CreateEffect(MODEL_SHADOW_PAWN_CHEST, vWorldPos, pObject->Angle, Light, 0, pObject,
                             0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 6);
                CreateEffect(MODEL_SHADOW_PAWN_HELMET, vWorldPos, pObject->Angle, Light, 0, pObject,
                             0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 33);
                CreateEffect(MODEL_SHADOW_PAWN_KNEE_LEFT, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 38);
                CreateEffect(MODEL_SHADOW_PAWN_KNEE_RIGHT, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 13);
                CreateEffect(MODEL_SHADOW_PAWN_WRIST_LEFT, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 23);
                CreateEffect(MODEL_SHADOW_PAWN_WRIST_RIGHT, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
            }
        }
        break;
    case MODEL_SHADOW_KNIGHT: {
        int iBones[] = {11, 15, 34, 21, 25, 39};
        Vector(0.3f, 0.6f, 1.0f, vLight);
        for (int i = 0; i < 6; ++i)
        {
            if (WorldRandom() % 6 > 0)
                continue;
            pModel->TransformByObjectBone(vPos, pObject, iBones[i]);
            CreateParticleFpsChecked(BITMAP_SMOKE, vPos, pObject->Angle, vLight, 50, 1.0f);
            CreateParticleFpsChecked(BITMAP_SMOKELINE1 + WorldRandom() % 3, vPos, pObject->Angle,
                                     vLight, 0, 1.0f);
        }
    }
        if (visual.action == MONSTER01_DIE)
        {
            if (visual.emissionLifeTime == 100)
            {
                visual.emissionLifeTime = 90;

                vec3_t vWorldPos, Light;
                Vector(1.0f, 1.0f, 1.0f, Light);

                pModel->TransformByObjectBone(vWorldPos, pObject, 34);
                CreateEffect(MODEL_SHADOW_KNIGHT_ANKLE_LEFT, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 39);
                CreateEffect(MODEL_SHADOW_KNIGHT_ANKLE_RIGHT, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 0);
                CreateEffect(MODEL_SHADOW_KNIGHT_BELT, vWorldPos, pObject->Angle, Light, 0, pObject,
                             0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 4);
                CreateEffect(MODEL_SHADOW_KNIGHT_CHEST, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 6);
                CreateEffect(MODEL_SHADOW_KNIGHT_HELMET, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 33);
                CreateEffect(MODEL_SHADOW_KNIGHT_KNEE_LEFT, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 38);
                CreateEffect(MODEL_SHADOW_KNIGHT_KNEE_RIGHT, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 13);
                CreateEffect(MODEL_SHADOW_KNIGHT_WRIST_LEFT, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 23);
                CreateEffect(MODEL_SHADOW_KNIGHT_WRIST_RIGHT, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
            }
        }
        break;
    case MODEL_SHADOW_LOOK: {
        int iBones[] = {11, 15, 34, 21, 25, 39};
        Vector(0.5f, 1.0f, 0.5f, vLight);
        for (int i = 0; i < 6; ++i)
        {
            if (WorldRandom() % 6 > 0)
                continue;
            pModel->TransformByObjectBone(vPos, pObject, iBones[i]);
            CreateParticleFpsChecked(BITMAP_SMOKE, vPos, pObject->Angle, vLight, 50, 1.5f);
            CreateParticleFpsChecked(BITMAP_SMOKELINE1 + WorldRandom() % 3, vPos, pObject->Angle,
                                     vLight, 0, 1.1f);
        }
    }
        if (visual.action == MONSTER01_DIE)
        {
            if (visual.emissionLifeTime == 100)
            {
                visual.emissionLifeTime = 90;

                vec3_t vWorldPos, Light;
                Vector(1.0f, 1.0f, 1.0f, Light);

                pModel->TransformByObjectBone(vWorldPos, pObject, 34);
                CreateEffect(MODEL_SHADOW_ROOK_ANKLE_LEFT, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 39);
                CreateEffect(MODEL_SHADOW_ROOK_ANKLE_RIGHT, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 0);
                CreateEffect(MODEL_SHADOW_ROOK_BELT, vWorldPos, pObject->Angle, Light, 0, pObject,
                             0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 4);
                CreateEffect(MODEL_SHADOW_ROOK_CHEST, vWorldPos, pObject->Angle, Light, 0, pObject,
                             0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 6);
                CreateEffect(MODEL_SHADOW_ROOK_HELMET, vWorldPos, pObject->Angle, Light, 0, pObject,
                             0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 33);
                CreateEffect(MODEL_SHADOW_ROOK_KNEE_LEFT, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 38);
                CreateEffect(MODEL_SHADOW_ROOK_KNEE_RIGHT, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 12);
                CreateEffect(MODEL_SHADOW_ROOK_WRIST_LEFT, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 23);
                CreateEffect(MODEL_SHADOW_ROOK_WRIST_RIGHT, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
            }
        }
        break;
    case MODEL_NAPIN: {
        int iBones[] = {7, 4, 5, 10, 22, 11, 23, 12, 24, 34, 39};
        vec3_t vLightFlare;
        float fScale;
        Vector(0.4f, 0.7f, 1.0f, vLight);
        Vector(0.1f, 0.2f, 1.0f, vLightFlare);
        for (int bone : iBones)
        {
            pModel->TransformByObjectBone(vPos, pObject, bone);
            CreateSprite(BITMAP_LIGHT, vPos, 2.2f, vLightFlare, pObject);
        }
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            const float fraction = birth.FrameFraction();
            ObjectDrawInput draw(pObject);
            pObject->MotionTrace.Sample(WorldTime, fraction, pObject->Position, draw.position);
            AnimationPoseSample pose(draw, pModel->BoneHead, pModel->BodyHeight, false,
                                     pModel->PoseAssetIdentity());
            std::array<vec34_t, MAX_BONES> bones;
            draw.bones = pose.EvaluateAtTime(*pModel, *pObject, WorldTime, fraction, bones.data());
            for (int i = 0; i < 11; ++i)
            {
                if (WorldRandom() % 3 > 0)
                    continue;
                Vector((WorldRandom() % 30 - 15) * 1.0f, (WorldRandom() % 30 - 15) * 1.0f,
                       (WorldRandom() % 30 - 15) * 1.0f, vRelative);
                pModel->TransformByObjectBone(vPos, draw, iBones[i], vRelative);
                fScale = (float)(WorldRandom() % 80 + 32) * 0.01f * 1.0f;
                CreateParticle(BITMAP_LIGHTNING_MEGA1 + WorldRandom() % 3, vPos, pObject->Angle,
                               vLight, 0, fScale);
            }
        }
    }
    break;
    case MODEL_GHOST_NAPIN: {
        int iBones[] = {21, 37, 65, 66, 77, 78, 79};
        Vector(0.4f, 1.0f, 0.4f, vLight);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            const float fraction = birth.FrameFraction();
            ObjectDrawInput draw(pObject);
            pObject->MotionTrace.Sample(WorldTime, fraction, pObject->Position, draw.position);
            AnimationPoseSample pose(draw, pModel->BoneHead, pModel->BodyHeight, false,
                                     pModel->PoseAssetIdentity());
            std::array<vec34_t, MAX_BONES> bones;
            draw.bones = pose.EvaluateAtTime(*pModel, *pObject, WorldTime, fraction, bones.data());
            for (int i = 0; i < 7; ++i)
            {
                pModel->TransformByObjectBone(vPos, draw, iBones[i]);
                CreateParticle(BITMAP_SMOKELINE1 + WorldRandom() % 3, vPos, pObject->Angle, vLight,
                               1, 1.0f, pObject);
                CreateParticle(BITMAP_CLUD64, vPos, pObject->Angle, vLight, 6, 1.0f, pObject);
            }
        }
    }
    break;
    case MODEL_BLAZE_NAPIN:
        sessionKeeper_.Visual()->AdvanceBurningNapin(*pObject, *pModel, false);
        break;
    case MODEL_MEDUSA: {
        vec3_t vColor;
        Vector(1.0f, 1.0f, 1.0f, vColor);

        pModel->TransformByObjectBone(vPos, pObject, 5);
        Vector(0.9f, 0.8f, 0.3f, vColor);
        CreateSprite(BITMAP_LIGHTMARKS, vPos, 2.0f, vColor, pObject);
        Vector(0.1f, 1.0f, 0.0f, vColor);
        CreateSprite(BITMAP_LIGHT, vPos, 2.4f, vColor, pObject);

        pModel->TransformByObjectBone(vPos, pObject, 33);
        Vector(0.9f, 0.8f, 0.3f, vColor);
        CreateSprite(BITMAP_LIGHTMARKS, vPos, 1.2f, vColor, pObject);
        Vector(0.1f, 1.0f, 0.0f, vColor);
        CreateSprite(BITMAP_LIGHT, vPos, 1.4f, vColor, pObject);

        MoveEye(pObject, pModel, 34, 35);
        Vector(1.0f, 0.0f, 0.0f, vColor);
        Vector(1.0f, 1.0f, 1.0f, vColor);
        pModel->TransformByObjectBone(vPos, pObject, 34);
        vPos[1] -= 0.8f;
        CreateSprite(BITMAP_LIGHT_RED, vPos, 0.4f, vColor, pObject);
        pModel->TransformByObjectBone(vPos, pObject, 35);
        vPos[1] -= 0.8f;
        CreateSprite(BITMAP_LIGHT_RED, vPos, 0.4f, vColor, pObject);

        pModel->TransformByObjectBone(vPos, pObject, 68);
        Vector(0.1f, 1.0f, 0.0f, vColor);
        CreateSprite(BITMAP_LIGHT, vPos, 4.0f, vColor, pObject);
        Vector(0.9f, 1.0f, 0.1f, vColor);
        CreateSprite(BITMAP_SHOCK_WAVE, vPos, 0.35f, vColor, pObject);
        Vector(0.9f, 1.0f, 0.1f, vColor);
        CreateSprite(BITMAP_SHOCK_WAVE, vPos, 0.22f, vColor, pObject);
        Vector(0.9f, 1.0f, 0.9f, vColor);
        CreateSprite(BITMAP_SHINY + 1, vPos, 1.2f, vColor, pObject);
        Vector(1.0f, 1.0f, 1.0f, vColor);
        pModel->TransformByObjectBone(vPos, pObject, 66);
        CreateSprite(BITMAP_LIGHT_RED, vPos, 0.4f, vColor, pObject);
        pModel->TransformByObjectBone(vPos, pObject, 67);
        CreateSprite(BITMAP_LIGHT_RED, vPos, 0.4f, vColor, pObject);
        pModel->TransformByObjectBone(vPos, pObject, 69);
        CreateSprite(BITMAP_LIGHT_RED, vPos, 0.4f, vColor, pObject);
        pModel->TransformByObjectBone(vPos, pObject, 70);
        CreateSprite(BITMAP_LIGHT_RED, vPos, 0.4f, vColor, pObject);

        sessionKeeper_.Visual()->EmitMedusaParticles(*pObject, *pModel,
                                                     visual.action == MONSTER01_DIE);
    }
    break;
    case MODEL_SAPI_QUEEN:
    case MODEL_WOLF_STATUS: {
        Vector(1.0f, 0.6f, 0.1f, vLight);
        Vector(0.0f, 0.0f, 0.0f, vRelative);
        pModel->TransformPosition(pObject->BoneTransform[15], vRelative, vPos, true);

        CreateSprite(BITMAP_LIGHT, vPos, 2.0f, vLight, pObject);
        CreateSprite(BITMAP_LIGHT, vPos, 2.0f, vLight, pObject);
    }
    break;
    case MODEL_ICE_NAPIN:
        sessionKeeper_.Visual()->AdvanceBurningNapin(*pObject, *pModel, true);
        break;
    case MODEL_SHADOW_MASTER: {
        int iBones[] = {11, 15, 34, 21, 25, 39};
        Vector(1.0f, 1.0f, 1.f, vLight);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            const float fraction = birth.FrameFraction();
            ObjectDrawInput draw(pObject);
            pObject->MotionTrace.Sample(WorldTime, fraction, pObject->Position, draw.position);
            AnimationPoseSample pose(draw, pModel->BoneHead, pModel->BodyHeight, false,
                                     pModel->PoseAssetIdentity());
            std::array<vec34_t, MAX_BONES> bones;
            draw.bones = pose.EvaluateAtTime(*pModel, *pObject, WorldTime, fraction, bones.data());
            for (int i = 0; i < 6; ++i)
            {
                if (WorldRandom() % 6 > 0)
                    continue;
                pModel->TransformByObjectBone(vPos, draw, iBones[i]);
                CreateParticle(BITMAP_SMOKE, vPos, pObject->Angle, vLight, 50, 1.5f);
                CreateParticle(BITMAP_SMOKELINE1 + WorldRandom() % 3, vPos, pObject->Angle, vLight,
                               0, 1.1f);
            }
        }
    }
        if (visual.action == MONSTER01_DIE)
        {
            if (visual.emissionLifeTime == 100)
            {
                visual.emissionLifeTime = 90;

                vec3_t vWorldPos, Light;
                Vector(1.0f, 1.0f, 1.0f, Light);

                pModel->TransformByObjectBone(vWorldPos, pObject, 34);
                CreateEffect(MODEL_EX01_SHADOW_MASTER_ANKLE_LEFT, vWorldPos, pObject->Angle, Light,
                             0, pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 39);
                CreateEffect(MODEL_EX01_SHADOW_MASTER_ANKLE_RIGHT, vWorldPos, pObject->Angle, Light,
                             0, pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 0);
                CreateEffect(MODEL_EX01_SHADOW_MASTER_BELT, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 4);
                CreateEffect(MODEL_EX01_SHADOW_MASTER_CHEST, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 6);
                CreateEffect(MODEL_EX01_SHADOW_MASTER_HELMET, vWorldPos, pObject->Angle, Light, 0,
                             pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 33);
                CreateEffect(MODEL_EX01_SHADOW_MASTER_KNEE_LEFT, vWorldPos, pObject->Angle, Light,
                             0, pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 38);
                CreateEffect(MODEL_EX01_SHADOW_MASTER_KNEE_RIGHT, vWorldPos, pObject->Angle, Light,
                             0, pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 12);
                CreateEffect(MODEL_EX01_SHADOW_MASTER_WRIST_LEFT, vWorldPos, pObject->Angle, Light,
                             0, pObject, 0, 0);
                pModel->TransformByObjectBone(vWorldPos, pObject, 23);
                CreateEffect(MODEL_EX01_SHADOW_MASTER_WRIST_RIGHT, vWorldPos, pObject->Angle, Light,
                             0, pObject, 0, 0);
            }
        }
        break;
    }

    return false;
}

bool GMSwampOfQuiet::PlayMonsterSound(OBJECT *pObject)
{
    if (!IsCurrentMap())
        return false;

    float fDis_x, fDis_y;
    fDis_x = pObject->Position[0] - Hero->Object.Position[0];
    fDis_y = pObject->Position[1] - Hero->Object.Position[1];
    float fDistance = sqrtf(fDis_x * fDis_x + fDis_y * fDis_y);

    if (fDistance > 500.0f)
        return true;

    switch (pObject->Type)
    {
    case MODEL_SAPIUNUS:
    case MODEL_SAPIDUO:
        if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
            pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            if (rand_fps_check(3))
            {
                PlayBuffer(SOUND_SWAMPOFQUIET_SAPI_UNUS_ATTACK01);
            }
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            if (rand_fps_check(3))
            {
                PlayBuffer(SOUND_SWAMPOFQUIET_SAPI_DEATH01);
            }
        }
        return true;
    case MODEL_SAPITRES:
        if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
            pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            if (rand_fps_check(3))
            {
                PlayBuffer(SOUND_SWAMPOFQUIET_SAPI_TRES_ATTACK01);
            }
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            if (rand_fps_check(3))
            {
                PlayBuffer(SOUND_SWAMPOFQUIET_SAPI_DEATH01);
            }
        }
        return true;
    case MODEL_SHADOW_PAWN:
        if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
            pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            PlayBuffer(SOUND_SWAMPOFQUIET_SHADOW_PAWN_ATTACK01);
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_SWAMPOFQUIET_SHADOW_DEATH01);
        }
        return true;
    case MODEL_SHADOW_KNIGHT:
        if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
            pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            PlayBuffer(SOUND_SWAMPOFQUIET_SHADOW_KNIGHT_ATTACK01);
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_SWAMPOFQUIET_SHADOW_DEATH01);
        }
        return true;
    case MODEL_SHADOW_LOOK:
        if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
            pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            PlayBuffer(SOUND_SWAMPOFQUIET_SHADOW_ROOK_ATTACK01);
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_SWAMPOFQUIET_SHADOW_DEATH01);
        }
        return true;
    case MODEL_NAPIN:
        if (pObject->CurrentAction == MONSTER01_WALK)
        {
            if (rand_fps_check(100))
            {
                PlayBuffer(SOUND_SWAMPOFQUIET_THUNDER_NAIPIN_BREATH01);
            }
        }
        else if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
                 pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            PlayBuffer(SOUND_SWAMPOFQUIET_NAIPIN_ATTACK01);
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_SWAMPOFQUIET_NAIPIN_DEATH01);
        }
        return true;
    case MODEL_GHOST_NAPIN:
        if (pObject->CurrentAction == MONSTER01_WALK)
        {
            if (rand_fps_check(100))
            {
                PlayBuffer(SOUND_SWAMPOFQUIET_GHOST_NAIPIN_BREATH01);
            }
        }
        else if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
                 pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            PlayBuffer(SOUND_SWAMPOFQUIET_NAIPIN_ATTACK01);
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_SWAMPOFQUIET_NAIPIN_DEATH01);
        }
        return true;
    case MODEL_BLAZE_NAPIN:
        if (pObject->CurrentAction == MONSTER01_WALK)
        {
            if (rand_fps_check(100))
            {
                PlayBuffer(SOUND_SWAMPOFQUIET_BLAZE_NAIPIN_BREATH01);
            }
        }
        else if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
                 pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            PlayBuffer(SOUND_SWAMPOFQUIET_NAIPIN_ATTACK01);
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_SWAMPOFQUIET_NAIPIN_DEATH01);
        }
        return true;
    case MODEL_MEDUSA: {
    }
        return true;
    case MODEL_SAPI_QUEEN:
    case MODEL_WOLF_STATUS:
        if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
            pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            if (rand_fps_check(3))
            {
                PlayBuffer(SOUND_SWAMPOFQUIET_SAPI_UNUS_ATTACK01);
            }
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            if (rand_fps_check(3))
            {
                PlayBuffer(SOUND_SWAMPOFQUIET_SAPI_DEATH01);
            }
        }
        return true;
    case MODEL_ICE_NAPIN:
        if (pObject->CurrentAction == MONSTER01_WALK)
        {
            if (rand_fps_check(100))
            {
                PlayBuffer(SOUND_SWAMPOFQUIET_BLAZE_NAIPIN_BREATH01);
            }
        }
        else if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
                 pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            PlayBuffer(SOUND_SWAMPOFQUIET_NAIPIN_ATTACK01);
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_SWAMPOFQUIET_NAIPIN_DEATH01);
        }
        return true;
    case MODEL_SHADOW_MASTER:
        if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
            pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            PlayBuffer(SOUND_SWAMPOFQUIET_SHADOW_ROOK_ATTACK01);
        }
        else if (pObject->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_SWAMPOFQUIET_SHADOW_DEATH01);
        }
        return true;
    }

    return true;
}

bool GMSwampOfQuiet::AttackEffectMonster(CHARACTER *pCharacter, OBJECT *pObject, BMD *pModel)
{
    if (!IsCurrentMap())
        return false;

    return false;
}

void GMSwampOfQuiet::UpdateMusic()
{
    PlayMp3(MUSIC_SWAMP_OF_QUIET);
}

bool GMSwampOfQuiet::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_SWAMP_OF_QUIET) == 0;
}

extern int GetMp3PlayPosition();

bool CGMDoppelGanger1::MoveSharedMonsterVisual(OBJECT &object, BMD &model,
                                               WorldCharacterVisualState &visual)
{
    if (object.Type == MODEL_ICE_WALKER)
    {
        constexpr std::array<std::pair<int, float>, 1> markers{{{MONSTER01_ATTACK2, 4.4f}}};
        AnimationPoseSample pose(&object, model.BoneHead, model.BodyHeight, false,
                                 model.PoseAssetIdentity());
        if (FPS_ANIMATION_FACTOR > 0.f)
            object.MotionTrace.VisitAnimationEvents(
                WorldTime, markers, [&](std::size_t, float fraction) {
                    auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR *
                                                                         (1.f - fraction));
                    vec3_t position, local{}, angle;
                    pose.SampleBonePosition(model, object, 8, local, WorldTime, fraction, position);
                    VectorCopy(object.Angle, angle);
                    angle[2] = object.MotionTrace.SampleYaw(WorldTime, fraction, angle[2]);
                    CreateEffect(MODEL_STREAMOFICEBREATH, position, angle, visual.movement.light, 0,
                                 nullptr, -1, 0, 0, 0, 0.2f);
                });
        if (visual.action == MONSTER01_DIE)
        {
            ObjectDrawInput presentation(&object);
            visual.movement.Apply(presentation);
            vec3_t position;
            Vector(1.f, 1.f, 1.f, visual.movement.light);
            model.TransformByObjectBone(position, presentation, 6);
            CreateParticleFpsChecked(BITMAP_SMOKE, position, object.Angle, visual.movement.light, 3,
                                     3.5f);
            model.TransformByObjectBone(position, presentation, 79);
            CreateParticleFpsChecked(BITMAP_SMOKE, position, object.Angle, visual.movement.light,
                                     53, 3.5f);
        }
        return true;
    }
    if (object.Type == MODEL_MAD_BUTCHER || object.Type == MODEL_TERRIBLE_BUTCHER)
    {
        if (visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2 ||
            visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
            visual.action = MONSTER01_WALK;
        return true;
    }
    if (object.Type != MODEL_DOPPELGANGER)
        return false;
    constexpr std::array<std::pair<int, float>, 1> markers{{{MONSTER01_APEAR, AppearanceEndFrame}}};
    if (FPS_ANIMATION_FACTOR > 0.f)
        object.MotionTrace.VisitAnimationEvents(WorldTime, markers,
                                                [&](std::size_t, float fraction) {
                                                    visual.movement.actionStarted = TRUE;
                                                    EmitAppearanceBurst(object, fraction);
                                                });
    return true;
}

void CGMDoppelGanger1::EmitAppearanceBurst(OBJECT &object, float fraction)
{
    auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR * (1.f - fraction));
    vec3_t origin, angle, position, light;
    object.MotionTrace.Sample(WorldTime, fraction, object.Position, origin);
    VectorCopy(object.Angle, angle);
    angle[2] = object.MotionTrace.SampleYaw(WorldTime, fraction, angle[2]);
    Vector(0.2f, 1.f, 0.3f, light);
    for (int child = 0; child < 6; ++child)
    {
        Vector(origin[0] + WorldRandom() % 140 - 70, origin[1] + WorldRandom() % 140 - 70,
               origin[2], position);
        CreateEffect(BITMAP_CLOUD, position, angle, light, 0, nullptr, -1, 0, 0, 0, 2.f);
    }
    Vector(0.4f, 1.f, 0.6f, light);
    for (int child = 0; child < 3; ++child)
    {
        Vector(origin[0] + WorldRandom() % 100 - 50, origin[1] + WorldRandom() % 100 - 50,
               origin[2] + 10.f + (WorldRandom() % 20) * 10.f, position);
        CreateParticle(BITMAP_EXPLOTION_MONO, position, angle, light, 0,
                       (WorldRandom() % 8 + 7) * 0.1f);
    }
    Vector(0.f, 0.5f, 0.f, light);
    for (int child = 0; child < 15; ++child)
    {
        Vector(origin[0] + WorldRandom() % 200 - 100, origin[1] + WorldRandom() % 200 - 100,
               origin[2] + (WorldRandom() % 10) * 10.f, position);
        CreateEffect(MODEL_DOPPELGANGER_SLIME_CHIP, position, angle, light, 0, &object, 0, 0);
    }
    Vector(0.2f, 0.9f, 0.3f, light);
    for (int child = 0; child < 30; ++child)
    {
        Vector(origin[0] + WorldRandom() % 300 - 150, origin[1] + WorldRandom() % 300 - 150,
               origin[2] + 20.f + (WorldRandom() % 10) * 10.f, position);
        CreateParticle(BITMAP_SPARK + 1, position, angle, light, 31);
    }
    Vector(0.8f, 1.f, 0.8f, light);
    CreateParticle(BITMAP_SMOKE, origin, angle, light, 54, 2.8f);
}

bool CGMDoppelGanger1::MoveMonsterVisual(CHARACTER *, OBJECT *object, BMD *model,
                                         WorldCharacterVisualState &visual)
{
    return IsDoppelGanger1() && MoveSharedMonsterVisual(*object, *model, visual);
}

void CGMDoppelGanger1::MoveBlurEffect(CHARACTER *pCharacter, OBJECT *pObject, BMD *pModel)
{
    switch (pObject->Type)
    {
    case MODEL_MAD_BUTCHER:
    case MODEL_TERRIBLE_BUTCHER: {
        if (!(pObject->CurrentAction == MONSTER01_WALK ||
              pObject->CurrentAction == MONSTER01_ATTACK1 ||
              pObject->CurrentAction == MONSTER01_ATTACK2))
            break;

        vec3_t vLight;
        Vector(0.6f, 0.4f, 0.2f, vLight);

        vec3_t StartPos, StartRelative;
        vec3_t EndPos, EndRelative;

        float fActionSpeed = pModel->Actions[pObject->CurrentAction].PlaySpeed;
        float fSpeedPerFrame = fActionSpeed / 10.f;
        float fAnimationFrame = pObject->AnimationFrame - fActionSpeed;
        for (int i = 0; i < 10; i++)
        {
            pModel->AnimationAtFrame(BoneTransform, fAnimationFrame, pObject->PriorAnimationFrame,
                                     pObject->PriorAction, pObject->Angle, pObject->HeadAngle);

            Vector(0.f, 0.f, 0.f, StartRelative);
            Vector(0.f, 0.f, 0.f, EndRelative);
            pModel->TransformPosition(BoneTransform[33], StartRelative, StartPos, false);
            pModel->TransformPosition(BoneTransform[34], EndRelative, EndPos, false);
            CreateBlur(pCharacter, StartPos, EndPos, vLight, 0, false, 0);

            fAnimationFrame += fSpeedPerFrame;
        }
    }
    break;
    }
}

void CGMDoppelGanger1::EmitMist(OBJECT *o)
{
    for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
    {
        vec3_t Light, vPos;
        Vector(0.6f, 0.8f, 1.0f, Light);
        VectorCopy(o->Position, vPos);
        int iScale = o->Scale * 60;
        vPos[0] += WorldRandom() % iScale - iScale / 2;
        vPos[1] += WorldRandom() % iScale - iScale / 2;
        CreateParticle(BITMAP_LIGHT, vPos, o->Angle, Light, 15, o->Scale, o);
    }
}

bool CGMDoppelGanger1::AdvanceObjectVisual(OBJECT *o, BMD *b, float)
{
    if (IsDoppelGanger1() == false)
        return false;

    switch (o->Type)
    {
    case 70: {
        vec3_t vLight;
        Vector(0.1f, 0.4f, 1.0f, vLight);

        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            switch (WorldRandom() % 3)
            {
            case 0:
                CreateParticle(BITMAP_FIRE_HIK1_MONO, o->Position, o->Angle, vLight, 2, o->Scale);
                break;
            case 1:
                CreateParticle(BITMAP_FIRE_HIK2_MONO, o->Position, o->Angle, vLight, 6, o->Scale);
                break;
            case 2:
                CreateParticle(BITMAP_FIRE_HIK3_MONO, o->Position, o->Angle, vLight, 2, o->Scale);
                break;
            }

            CreateParticle(BITMAP_FIRE_HIK1_MONO, o->Position, o->Angle, vLight, 2, o->Scale);
        }
    }
        return true;
    case 80: {
        vec3_t vLight;
        Vector(0.7f, 0.2f, 0.1f, vLight);

        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            switch (WorldRandom() % 3)
            {
            case 0:
                CreateParticle(BITMAP_FIRE_HIK1_MONO, o->Position, o->Angle, vLight, 2, o->Scale);
                break;
            case 1:
                CreateParticle(BITMAP_FIRE_HIK2_MONO, o->Position, o->Angle, vLight, 6, o->Scale);
                break;
            case 2:
                CreateParticle(BITMAP_FIRE_HIK3_MONO, o->Position, o->Angle, vLight, 2, o->Scale);
                break;
            }
            CreateParticle(BITMAP_FIRE_HIK1_MONO, o->Position, o->Angle, vLight, 2, o->Scale);
        }
    }
        return true;
    case 99:
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            //Vector(0.02f, 0.03f, 0.04f, Light);
            Vector(0.04f, 0.06f, 0.08f, Light);
            for (int i = 0; i < 10; ++i)
                CreateParticleFpsChecked(BITMAP_CLOUD, o->Position, o->Angle, Light, 3, o->Scale,
                                         o);
            //CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 0, o->Scale, o);
        }
        return true;
    case 101:
        EmitMist(o);
        return true;
    }

    return false;
}

bool CGMDoppelGanger1::AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                            WorldCharacterVisualState &visual)
{
    switch (o->Type)
    {
    case MODEL_MAD_BUTCHER:
    case MODEL_TERRIBLE_BUTCHER:
        sessionKeeper_.Visual()->AdvanceButcherVisual(*o, *b, c->Dead == 0);
        return true;
    case MODEL_DOPPELGANGER:
        return true;
        break;
    }

    return false;
}

bool CGMDoppelGanger1::PlayMonsterSound(OBJECT *o)
{
    float fDis_x, fDis_y;
    fDis_x = o->Position[0] - Hero->Object.Position[0];
    fDis_y = o->Position[1] - Hero->Object.Position[1];
    float fDistance = sqrtf(fDis_x * fDis_x + fDis_y * fDis_y);

    if (fDistance > 500.0f)
        return true;

    switch (o->Type)
    {
    case MODEL_ICE_WALKER: // Ice Walker
        if (MONSTER01_ATTACK1 == o->CurrentAction || MONSTER01_ATTACK2 == o->CurrentAction)
        {
            PlayBuffer(SOUND_RAKLION_ICEWALKER_ATTACK);
        }
        else if (MONSTER01_WALK == o->CurrentAction)
        {
            if (rand_fps_check(20))
            {
                PlayBuffer(SOUND_RAKLION_ICEWALKER_MOVE);
            }
        }
        else if (MONSTER01_DIE == o->CurrentAction)
        {
            {
                PlayBuffer(SOUND_ELBELAND_WOLFHUMAN_DEATH01);
            }
        }
        return true;
    case MODEL_TERRIBLE_BUTCHER: {
        if (MONSTER01_ATTACK1 == o->CurrentAction || MONSTER01_ATTACK2 == o->CurrentAction)
        {
            PlayBuffer(SOUND_DOPPELGANGER_RED_BUGBEAR_ATTACK);
        }
        else if (MONSTER01_DIE == o->CurrentAction)
        {
            PlayBuffer(SOUND_DOPPELGANGER_RED_BUGBEAR_DEATH);
        }
    }
        return true;
    case MODEL_MAD_BUTCHER: {
        if (MONSTER01_ATTACK1 == o->CurrentAction || MONSTER01_ATTACK2 == o->CurrentAction)
        {
            PlayBuffer(SOUND_DOPPELGANGER_BUGBEAR_ATTACK);
        }
        else if (MONSTER01_DIE == o->CurrentAction)
        {
            PlayBuffer(SOUND_DOPPELGANGER_BUGBEAR_DEATH);
        }
    }
        return true;
    case MODEL_DOPPELGANGER: {
        if (MONSTER01_APEAR == o->CurrentAction)
        {
            PlayBuffer(SOUND_DOPPELGANGER_SLIME_ATTACK);
        }
        else if (MONSTER01_DIE == o->CurrentAction)
        {
            PlayBuffer(SOUND_DOPPELGANGER_SLIME_DEATH);
        }
    }
        return true;
    }

    return false;
}

void CGMDoppelGanger1::PlayBGM()
{
    if (IsDoppelGanger1() || IsDoppelGanger2() || IsDoppelGanger3() || IsDoppelGanger4())
    {
        if (!g_pDoppelGangerFrame->IsDoppelGangerEnabled())
        {
            StopMp3(MUSIC_DOPPELGANGER);
            m_bIsMP3Playing = FALSE;
        }
        else
        {
            if (m_bIsMP3Playing == TRUE && GetMp3PlayPosition() == 0)
            {
                StopMp3(MUSIC_DOPPELGANGER);
                m_bIsMP3Playing = FALSE;
            }
            if (m_bIsMP3Playing == FALSE)
            {
                PlayMp3(MUSIC_DOPPELGANGER);

                if (GetMp3PlayPosition() > 0)
                {
                    m_bIsMP3Playing = TRUE;
                }
            }
        }
    }
}

void CGMDoppelGanger1::UpdateMusic()
{
    PlayBGM();
}

bool CGMDoppelGanger1::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_DOPPELGANGER) == 0;
}

bool CGMDoppelGanger1::CreateWeather(PARTICLE *particle, int)
{
    return TheMapProcess().Raklion().CreateSnow(particle);
}

ESound CGMDoppelGanger1::WalkingSound(int, bool) const
{
    return SOUND_HUMAN_WALK_SNOW;
}

bool CGMAida::MoveAidaObject(OBJECT *pObject)
{
    if (!IsInAida())
        return false;

    float Luminosity;
    vec3_t Light;

    switch (pObject->Type)
    {
    case 25: {
        pObject->BlendMeshTexCoordV -= (0.015f) * FPS_ANIMATION_FACTOR;
    }
    break;
    case 28: {
        pObject->BlendMeshTexCoordV -= (0.015f) * FPS_ANIMATION_FACTOR;
    }
    break;
    case 30: {
        Luminosity = (float)(WorldRandom() % 5) * 0.01f;
        Vector(Luminosity + 0.4f, Luminosity + 0.6f, Luminosity + 0.4f, Light);
        AddTerrainLight(pObject->Position[0], pObject->Position[1], Light, 2, PrimaryTerrainLight);
    }
    break;
    case 71: {
        Luminosity = (float)(WorldRandom() % 5) * 0.01f;
        Vector(Luminosity + 0.9f, Luminosity + 0.2f, Luminosity + 0.2f, Light);
        AddTerrainLight(pObject->Position[0], pObject->Position[1], Light, 2, PrimaryTerrainLight);
    }
    break;
    case 41: {
        pObject->Alpha = 0.5f;
    }
    break;
    case 56:
    case 57:
    case 58:
    case 59:
    case 62:
    case 63:
    case 67:
    case 70:
        pObject->HiddenMesh = -2;
        break;
    case 64:
        pObject->Velocity = 0.05f;
        break;
    }

    PlayBuffer(SOUND_AIDA_AMBIENT);

    return true;
}

bool CGMAida::AdvanceAidaObjectVisual(OBJECT *pObject, BMD *pModel)
{
    if (!IsInAida())
        return false;

    vec3_t p, Position, Light;

    switch (pObject->Type)
    {
    case 30: // Ǯ
    {
        PrepareWorldObjectPose(*pObject);
        Vector(0.0f, -3.0f, 1.0f, p);
        pModel->TransformPosition(BoneTransform[6], p, Position, false);
        Vector(0.1f, 0.1f, 0.3f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 5.5f, Light, pObject);
        Vector(0.15f, 0.15f, 0.15f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 3.0f, Light, pObject);

        Vector(0.0f, -3.0f, 0.0f, p);
        pModel->TransformPosition(BoneTransform[7], p, Position, false);
        Vector(0.1f, 0.1f, 0.3f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 4.5f, Light, pObject);
        Vector(0.15f, 0.15f, 0.15f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 3.0f, Light, pObject);

        Vector(0.0f, -3.0f, 0.0f, p);
        pModel->TransformPosition(BoneTransform[8], p, Position, false);
        Vector(0.1f, 0.1f, 0.3f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 4.0f, Light, pObject);
        Vector(0.15f, 0.15f, 0.15f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 3.0f, Light, pObject);

        Vector(-3.0f, -1.0f, 0.0f, p);
        pModel->TransformPosition(BoneTransform[12], p, Position, false);
        Vector(0.1f, 0.1f, 0.3f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 4.5f, Light, pObject);
        Vector(0.15f, 0.15f, 0.15f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 3.0f, Light, pObject);

        Vector(-3.0f, -3.0f, 0.0f, p);
        pModel->TransformPosition(BoneTransform[13], p, Position, false);
        Vector(0.1f, 0.1f, 0.3f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 4.0f, Light, pObject);
        Vector(0.15f, 0.15f, 0.15f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 3.0f, Light, pObject);

        Vector(0.0f, 2.0f, -8.0f, p);
        pModel->TransformPosition(BoneTransform[17], p, Position, false);
        Vector(0.1f, 0.1f, 0.3f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 4.0f, Light, pObject);
        Vector(0.15f, 0.15f, 0.15f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 3.0f, Light, pObject);
    }
    break;
    case 56:
        Vector(1.f, 1.f, 1.f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
        {
            CreateParticle(BITMAP_WATERFALL_1, pObject->Position, pObject->Angle, Light, 2,
                           pObject->Scale);
        }
        break;
    case 57:
        Vector(1.f, 1.f, 1.f, Light);
        CreateParticleFpsChecked(BITMAP_WATERFALL_3, pObject->Position, pObject->Angle, Light, 4,
                                 pObject->Scale);
        break;
    case 58:
        Vector(1.f, 1.f, 1.f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            CreateParticle(BITMAP_WATERFALL_2, pObject->Position, pObject->Angle, Light, 2,
                           pObject->Scale);
        }
        break;
    case 59:
        if (pObject->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.01f, 0.03f, 0.05f, Light);
            for (int i = 0; i < 20; ++i)
            {
                CreateParticleFpsChecked(BITMAP_CLOUD, pObject->Position, pObject->Angle, Light, 1,
                                         pObject->Scale, pObject);
            }
        }
        break;
    case 60: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 25.f))
        {
            vec3_t Light;
            Vector(1.f, 1.f, 1.f, Light);
            CreateEffect(MODEL_BUTTERFLY01, pObject->Position, pObject->Angle, Light, 3, pObject);
        }
        pObject->HiddenMesh = -2; //. Hide Object
    }
    break;
    case 62:
        if (pObject->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.05f, 0.05f, 0.05f, Light);
            for (int i = 0; i < 20; ++i)
            {
                CreateParticleFpsChecked(BITMAP_CLOUD, pObject->Position, pObject->Angle, Light, 1,
                                         pObject->Scale, pObject);
            }
        }
        break;
    case 63:
        if (pObject->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.05f, 0.02f, 0.02f, Light);
            for (int i = 0; i < 20; ++i)
            {
                CreateParticleFpsChecked(BITMAP_CLOUD, pObject->Position, pObject->Angle, Light, 1,
                                         pObject->Scale, pObject);
            }
        }
        break;
    case 67:
        Vector(0.3f, 0.3f, 0.3f, pObject->Light);
        CreateParticleFpsChecked(BITMAP_FLAME, pObject->Position, pObject->Angle, pObject->Light, 7,
                                 pObject->Scale);
        break;
    case 70: {
        int time = static_cast<DWORD>(WorldSimulationTime()) % 1024;
        if (rand_fps_check(5) && (time >= 0 && time < 30))
        {
            Vector(0.1f, 0.1f, 0.1f, Light);
            CreateEffect(MODEL_GHOST, pObject->Position, pObject->Angle, Light, 0, pObject, -1, 0,
                         pObject->Scale);
        }
    }
    break;
    case 71: {
        PrepareWorldObjectPose(*pObject);
        Vector(0.0f, -3.0f, 1.0f, p);
        pModel->TransformPosition(BoneTransform[6], p, Position, false);
        Vector(0.5f, 0.0f, 0.0f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 6.0f, Light, pObject);
        Vector(0.15f, 0.0f, 0.0f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 3.0f, Light, pObject);

        Vector(0.0f, -3.0f, 0.0f, p);
        pModel->TransformPosition(BoneTransform[7], p, Position, false);
        Vector(0.25f, 0.0f, 0.0f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 5.5f, Light, pObject);
        Vector(0.15f, 0.0f, 0.0f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 3.0f, Light, pObject);

        Vector(0.0f, -3.0f, 0.0f, p);
        pModel->TransformPosition(BoneTransform[8], p, Position, false);
        Vector(0.25f, 0.0f, 0.0f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 5.0f, Light, pObject);
        Vector(0.15f, 0.0f, 0.0f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 3.0f, Light, pObject);

        Vector(-3.0f, -1.0f, 0.0f, p);
        pModel->TransformPosition(BoneTransform[12], p, Position, false);
        Vector(0.25f, 0.0f, 0.0f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 5.5f, Light, pObject);
        Vector(0.15f, 0.0f, 0.0f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 3.0f, Light, pObject);

        Vector(-3.0f, -3.0f, 0.0f, p);
        pModel->TransformPosition(BoneTransform[13], p, Position, false);
        Vector(0.25f, 0.0f, 0.0f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 5.0f, Light, pObject);
        Vector(0.15f, 0.0f, 0.0f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 3.0f, Light, pObject);

        Vector(0.0f, 2.0f, -8.0f, p);
        pModel->TransformPosition(BoneTransform[17], p, Position, false);
        Vector(0.25f, 0.0f, 0.0f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 5.0f, Light, pObject);
        Vector(0.15f, 0.0f, 0.0f, Light);
        CreateSprite(BITMAP_SPARK + 1, Position, 3.0f, Light, pObject);
    }
    break;
    case 75: {
        PrepareWorldObjectPose(*pObject);
        Vector(0.f, 0.f, 0.f, p);
        pModel->TransformPosition(BoneTransform[4], p, Position, false);
        float fLumi = (sinf(WorldTime * 0.002f) + 1.0f) * 0.5f;
        Vector(fLumi, fLumi, fLumi, Light);
        Vector(1.0f, 1.0f, 1.f, Light);
        CreateSprite(BITMAP_FLARE, Position, 3.0f, Light, pObject, (WorldTime / 10.0f));
    }
    break;
    }

    return true;
}

bool CGMAida::MoveAidaMonsterVisual(OBJECT *pObject, BMD *pModel, WorldCharacterVisualState &visual)
{
    switch (pObject->Type)
    {
    case MODEL_WITCH_QUEEN: {
        vec3_t Light;
        Vector(0.7f, 0.1f, 0.1f, Light);
        AddTerrainLight(pObject->Position[0], pObject->Position[1], Light, 3, PrimaryTerrainLight);
    }
    break;
    case MODEL_GOLDEN_STONE_GOLEM: {
        vec3_t Light;
        Vector(0.f, 0.0f, 0.7f, Light);
        AddTerrainLight(pObject->Position[0], pObject->Position[1], Light, 3, PrimaryTerrainLight);
    }
    break;
    case MODEL_HELL_MAINE: {
        vec3_t Light;
        Vector(1.f, 0.0f, 0.0f, Light);
        AddTerrainLight(pObject->Position[0], pObject->Position[1], Light, 3, PrimaryTerrainLight);
    }
    break;
    case MODEL_BLOODY_GOLEM: {
        vec3_t Light;
        Vector(0.f, 0.0f, 0.7f, Light);
        AddTerrainLight(pObject->Position[0], pObject->Position[1], Light, 3, PrimaryTerrainLight);
    }
    break;
    case MODEL_BLOODY_WITCH_QUEEN: {
        vec3_t Light;
        Vector(0.7f, 0.1f, 0.1f, Light);
        AddTerrainLight(pObject->Position[0], pObject->Position[1], Light, 3, PrimaryTerrainLight);
    }
    break;
    }
    return false;
}

void CGMAida::MoveAidaBlurEffect(CHARACTER *pCharacter, OBJECT *pObject, BMD *pModel)
{
    switch (pObject->Type)
    {
    case MODEL_DEATH_RIDER: {
        if (pObject->AnimationFrame <= 5.06f && (pObject->CurrentAction == MONSTER01_ATTACK1 ||
                                                 pObject->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t Light;
            Vector(1.0f, 1.0f, 1.0f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = pModel->Actions[pObject->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = pObject->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                pModel->AnimationAtFrame(BoneTransform, fAnimationFrame,
                                         pObject->PriorAnimationFrame, pObject->PriorAction,
                                         pObject->Angle, pObject->HeadAngle);

                Vector(20.f, 0.f, 0.f, StartRelative);
                Vector(100.f, 0.f, 0.f, EndRelative);

                pModel->TransformPosition(BoneTransform[18], StartRelative, StartPos, false);
                pModel->TransformPosition(BoneTransform[20], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, Light, 3, true, 23);

                pModel->TransformPosition(BoneTransform[25], StartRelative, StartPos, false);
                pModel->TransformPosition(BoneTransform[26], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, Light, 3, true, 24);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_FOREST_ORC: {
        if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
            pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            vec3_t Light;
            Vector(0.5f, 0.5f, 0.0f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = pModel->Actions[pObject->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = pObject->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                pModel->AnimationAtFrame(BoneTransform, fAnimationFrame,
                                         pObject->PriorAnimationFrame, pObject->PriorAction,
                                         pObject->Angle, pObject->HeadAngle);

                Vector(20.f, 0.f, 0.f, StartRelative);
                Vector(60.f, 0.f, 0.f, EndRelative);

                pModel->TransformPosition(BoneTransform[35], StartRelative, StartPos, false);
                pModel->TransformPosition(BoneTransform[37], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, Light, 3, true, 23);

                pModel->TransformPosition(BoneTransform[39], StartRelative, StartPos, false);
                pModel->TransformPosition(BoneTransform[41], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, Light, 3, true, 24);

                pModel->TransformPosition(BoneTransform[43], StartRelative, StartPos, false);
                pModel->TransformPosition(BoneTransform[45], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, Light, 3, true, 25);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_DEATH_TREE: {
        if (pObject->CurrentAction == MONSTER01_ATTACK1)
        {
            vec3_t Light;
            Vector(0.3f, 0.3f, 0.3f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = pModel->Actions[pObject->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = pObject->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                pModel->AnimationAtFrame(BoneTransform, fAnimationFrame,
                                         pObject->PriorAnimationFrame, pObject->PriorAction,
                                         pObject->Angle, pObject->HeadAngle);

                Vector(0.f, 0.f, 20.f, StartRelative);
                Vector(0.f, 0.f, 70.f, EndRelative);

                pModel->TransformPosition(BoneTransform[26], StartRelative, StartPos, false);
                pModel->TransformPosition(BoneTransform[27], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, Light, 3, true, 24);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_BLOODY_ORC: {
        if (pObject->CurrentAction == MONSTER01_ATTACK1 ||
            pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            vec3_t Light;
            Vector(0.5f, 0.5f, 0.0f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = pModel->Actions[pObject->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = pObject->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                pModel->AnimationAtFrame(BoneTransform, fAnimationFrame,
                                         pObject->PriorAnimationFrame, pObject->PriorAction,
                                         pObject->Angle, pObject->HeadAngle);

                Vector(20.f, 0.f, 0.f, StartRelative);
                Vector(60.f, 0.f, 0.f, EndRelative);

                pModel->TransformPosition(BoneTransform[35], StartRelative, StartPos, false);
                pModel->TransformPosition(BoneTransform[37], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, Light, 3, true, 23);

                pModel->TransformPosition(BoneTransform[39], StartRelative, StartPos, false);
                pModel->TransformPosition(BoneTransform[41], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, Light, 3, true, 24);

                pModel->TransformPosition(BoneTransform[43], StartRelative, StartPos, false);
                pModel->TransformPosition(BoneTransform[45], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, Light, 3, true, 25);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_BLOODY_DEATH_RIDER: {
        if (pObject->AnimationFrame <= 5.06f && (pObject->CurrentAction == MONSTER01_ATTACK1 ||
                                                 pObject->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t Light;
            Vector(1.0f, 1.0f, 1.0f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = pModel->Actions[pObject->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = pObject->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                pModel->AnimationAtFrame(BoneTransform, fAnimationFrame,
                                         pObject->PriorAnimationFrame, pObject->PriorAction,
                                         pObject->Angle, pObject->HeadAngle);

                Vector(20.f, 0.f, 0.f, StartRelative);
                Vector(100.f, 0.f, 0.f, EndRelative);

                pModel->TransformPosition(BoneTransform[18], StartRelative, StartPos, false);
                pModel->TransformPosition(BoneTransform[20], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, Light, 3, true, 23);

                pModel->TransformPosition(BoneTransform[25], StartRelative, StartPos, false);
                pModel->TransformPosition(BoneTransform[26], EndRelative, EndPos, false);
                CreateBlur(pCharacter, StartPos, EndPos, Light, 3, true, 24);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    }
}

void CGMAida::EmitMonsterActionSounds(OBJECT &object)
{
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    std::array<ESound, 3> sounds;
    switch (object.Type)
    {
    case MODEL_WITCH_QUEEN:
    case MODEL_BLOODY_WITCH_QUEEN:
        sounds = {SOUND_AIDA_WITCHQUEEN_ATTACK2, SOUND_AIDA_WITCHQUEEN_ATTACK1,
                  SOUND_AIDA_WITCHQUEEN_DIE};
        break;
    case MODEL_GOLDEN_STONE_GOLEM:
    case MODEL_BLOODY_GOLEM:
        sounds = {SOUND_AIDA_BLUEGOLEM_ATTACK1, SOUND_AIDA_BLUEGOLEM_ATTACK2,
                  SOUND_AIDA_BLUEGOLEM_DIE};
        break;
    case MODEL_DEATH_RIDER:
    case MODEL_BLOODY_DEATH_RIDER:
        sounds = {SOUND_AIDA_DEATHRAIDER_ATTACK1, SOUND_AIDA_DEATHRAIDER_ATTACK2,
                  SOUND_AIDA_DEATHRAIDER_DIE};
        break;
    case MODEL_FOREST_ORC:
    case MODEL_BLOODY_ORC:
        sounds = {SOUND_AIDA_FORESTORC_ATTACK1, SOUND_AIDA_FORESTORC_ATTACK2,
                  SOUND_AIDA_FORESTORC_DIE};
        break;
    case MODEL_DEATH_TREE:
        sounds = {SOUND_AIDA_DEATHTREE_ATTACK1, SOUND_AIDA_DEATHTREE_ATTACK2,
                  SOUND_AIDA_DEATHTREE_DIE};
        break;
    case MODEL_HELL_MAINE:
        sounds = {SOUND_AIDA_HELL_ATTACK3, SOUND_AIDA_HELL_ATTACK2, SOUND_AIDA_HELL_DIE};
        break;
    default:
        return;
    }
    constexpr std::array<std::pair<int, float>, 4> markers{{{MONSTER01_ATTACK1, 0.f},
                                                            {MONSTER01_ATTACK2, 0.f},
                                                            {MONSTER01_DIE, 0.f},
                                                            {MONSTER01_ATTACK3, 0.f}}};
    object.MotionTrace.VisitAnimationEvents(WorldTime, markers, [&](std::size_t event, float) {
        if (event == 3)
        {
            if (object.Type == MODEL_HELL_MAINE)
                PlayBuffer(SOUND_AIDA_HELL_ATTACK1);
            return;
        }
        if (event == 1 &&
            (object.Type == MODEL_WITCH_QUEEN || object.Type == MODEL_BLOODY_WITCH_QUEEN))
            PlayBuffer(static_cast<ESound>(SOUND_CHAOS_THUNDER01 + WorldRandom() % 2));
        PlayBuffer(sounds[event]);
    });
}

bool CGMAida::AdvanceAidaMonsterVisual(CHARACTER *pCharacter, OBJECT *pObject, BMD *pModel,
                                       WorldCharacterVisualState &visual)
{
    EmitMonsterActionSounds(*pObject);
    switch (pObject->Type)
    {
    case MODEL_WITCH_QUEEN: {
        vec3_t Position, Light, Angle;
        float Random_Light = (float)(WorldRandom() % 30) / 100.0f + 0.6f;
        Vector(Random_Light + 0.5f, Random_Light - 0.05f, Random_Light + 0.5f, Light);

        GetBonePosition(pObject, CharacterSocket::Monster100_L_Hand, Position);
        CreateSprite(BITMAP_LIGHT + 1, Position, 0.8f, Light, pObject);
        GetBonePosition(pObject, CharacterSocket::Monster100_z02, Position);
        CreateSprite(BITMAP_LIGHT + 1, Position, 1.1f, Light, pObject);
        GetBonePosition(pObject, CharacterSocket::Monster100_z03, Position);
        CreateSprite(BITMAP_LIGHT + 1, Position, 1.0f, Light, pObject);
        GetBonePosition(pObject, CharacterSocket::Monster100_z04, Position);
        CreateSprite(BITMAP_LIGHT + 1, Position, 1.7f, Light, pObject);
        GetBonePosition(pObject, CharacterSocket::Monster100_z05, Position);
        CreateSprite(BITMAP_LIGHT + 1, Position, 0.8f, Light, pObject);

        Vector(0.7f, 0.5f, 0.7f, Light);
        vec3_t Relative = {0.0f, 0.0f, -65.0f};
        GetBonePosition(pObject, CharacterSocket::Monster100_Footstepst, Relative, Position);
        CreateParticleFpsChecked(BITMAP_LIGHT + 1, Position, Angle, Light, 4, 4.0f);

        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_AIDA_WITCHQUEEN_MOVE1 + WorldRandom() % 2));
        }

        if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
        {
            if (visual.animationFrame >= 4.0f)
            {
                vec3_t Relative = {70.0f, 0.0f, 0.0f};
                GetBonePosition(pObject, CharacterSocket::Monster100_Footstepst, Relative,
                                Position);

                Vector(0.5f, 0.2f, 0.5f, Light);
                CreateParticleFpsChecked(BITMAP_SMOKE, Position, Angle, Light, 27, 2.0f);
                Vector(1.0f, 1.0f, 1.0f, Light);
                CreateParticleFpsChecked(BITMAP_LIGHT + 1, Position, Angle, Light, 2, 1.0f);
            }
            if (visual.animationFrame >= 6.0f && visual.action == MONSTER01_ATTACK1)
            {
                vec3_t Position, Light, Angle = {0.0f, 0.0f, 0.0f};
                vec3_t Relative = {70.0f, 0.0f, 0.0f};
                GetBonePosition(pObject, CharacterSocket::Monster100_Footstepst, Relative,
                                Position);
                Vector(1.0f, 1.0f, 1.0f, Light);
                CreateParticleFpsChecked(BITMAP_LIGHT + 1, Position, Angle, Light, 3, 1.3f);
            }
            if (CharactersClient.IsValidIndex(pCharacter->TargetCharacter))
            {
                CHARACTER *tc = &CharactersClient[pCharacter->TargetCharacter];
                OBJECT *to = &tc->Object;

                vec3_t vTemp;
                VectorCopy(to->Position, vTemp);
                vTemp[2] += 100.0f;

                if (visual.animationFrame >= 6.0f && visual.action == MONSTER01_ATTACK1)
                {
                    CreateParticleFpsChecked(BITMAP_LIGHT + 1, vTemp, Angle, Light, 3, 1.3f);
                }
                if (visual.animationFrame >= 6.0f && visual.action == MONSTER01_ATTACK2)
                {
                    CreateJointFpsChecked(BITMAP_JOINT_THUNDER, to->Position, vTemp, pObject->Angle,
                                          16);
                }
            }
        }

    }
    break;
    case MODEL_GOLDEN_STONE_GOLEM: {
        vec3_t Position, Light, Angle = {0.0f, 0.0f, 0.0f};
        Vector(1.0f, 1.0f, 1.0f, Light);

        GetBonePosition(pObject, CharacterSocket::Monster101_L_Arm, Position);
        CreateParticleFpsChecked(BITMAP_WATERFALL_2, Position, Angle, Light, 1);
        GetBonePosition(pObject, CharacterSocket::Monster101_R_Arm, Position);
        CreateParticleFpsChecked(BITMAP_WATERFALL_2, Position, Angle, Light, 1);
        GetBonePosition(pObject, CharacterSocket::Monster101_Head, Position);
        CreateParticleFpsChecked(BITMAP_WATERFALL_2, Position, Angle, Light, 1);

        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_AIDA_BLUEGOLEM_MOVE1 + WorldRandom() % 2));
        }

        if (visual.action == MONSTER01_ATTACK1)
        {
            vec3_t Position, Light, Angle = {0.0f, 0.0f, 0.0f};
            Vector(1.0f, 1.0f, 1.0f, Light);
            if (visual.animationFrame >= 4.0f)
            {
                Vector(0.0f, 45.0f, 45.0f, Angle);
                vec3_t Relative = {30.0f, 0.0f, 0.0f};
                GetBonePosition(pObject, CharacterSocket::Monster101_L_Arm, Relative, Position);
                CreateParticleFpsChecked(BITMAP_SMOKE, Position, Angle, Light, 25);

                if (visual.animationFrame >= 5.0f && visual.animationFrame <= 5.5f)
                {
                    Vector(5.0f, 5.0f, 5.0f, Light);
                    GetBonePosition(pObject, CharacterSocket::Monster101_L_Arm, Relative, Position);
                    CreateParticleFpsChecked(BITMAP_SHOCK_WAVE, Position, Angle, Light, 3, 0.5f);
                }
            }

            if (CharactersClient.IsValidIndex(pCharacter->TargetCharacter) &&
                visual.animationFrame >= 6.9f)
            {
                CHARACTER *tc = &CharactersClient[pCharacter->TargetCharacter];
                OBJECT *to = &tc->Object;

                vec3_t vTemp;
                VectorCopy(to->Position, vTemp);
                vTemp[2] += 100.0f;

                CreateJointFpsChecked(BITMAP_JOINT_ENERGY, vTemp, pObject->Position, to->Angle, 16,
                                      pObject, 20.0f);
            }
        }

    }
    break;
    case MODEL_DEATH_RIDER: {
        vec3_t Relative, Position, Light, Angle = {0.0f, 0.0f, 0.0f};

        float Random_Light = (float)(WorldRandom() % 10) / 100.0f + 0.2f;
        Vector(60.0f, -30.0f, 0.0f, Relative);
        Vector(0.0f, Random_Light, 0.0f, Light);

        GetBonePosition(pObject, CharacterSocket::Monster102_Head, Relative, Position);
        CreateSprite(BITMAP_LIGHT + 1, Position, 3.0f, Light, pObject);
        CreateParticleFpsChecked(BITMAP_SPARK + 1, Position, Angle, Light, 7);

        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_AIDA_DEATHRAIDER_MOVE1 + WorldRandom() % 2));
        }

    }
    break;

    case MODEL_FOREST_ORC: {
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_AIDA_FORESTORC_MOVE1 + WorldRandom() % 2));
        }

    }
    break;
    case MODEL_DEATH_TREE: {
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_AIDA_DEATHTREE_MOVE1 + WorldRandom() % 2));
        }

        vec3_t Position, Light, Angle = {0.0f, 0.0f, 0.0f};
        float Random_Light;

        for (int i = 0; i < 6; i++)
        {
            constexpr int horns[]{
                CharacterSocket::Monster104_Horn0, CharacterSocket::Monster104_Horn1,
                CharacterSocket::Monster104_Horn2, CharacterSocket::Monster104_Horn3,
                CharacterSocket::Monster104_Horn4, CharacterSocket::Monster104_Horn5};
            Random_Light = (float)(WorldRandom() % 10) / 100.0f + 0.8f;
            Vector(0.4f, Random_Light, 0.5f, Light);
            GetBonePosition(pObject, horns[i], Position);
            CreateSprite(BITMAP_LIGHT + 1, Position, 0.4f, Light, pObject);
            CreateParticleFpsChecked(BITMAP_SPARK + 1, Position, Angle, Light, 6);
        }
        if (visual.action == MONSTER01_ATTACK2)
        {
            vec3_t Position, Relative = {0.0f, -100.0f, 20.0f}, Light = {0.5f, 0.7f, 0.5f};
            GetBonePosition(pObject, CharacterSocket::Monster104_Footsteps, Relative, Position);
            CreateParticleFpsChecked(BITMAP_SMOKE, Position, pObject->Angle, Light, 26);
        }

    }
    break;
    case MODEL_HELL_MAINE: {
        vec3_t Relative, Position, Light, Angle = {0.0f, 0.0f, 0.0f};
        float Random_Light;

        Random_Light = (float)(WorldRandom() % 8) / 10.0f + 0.6f;
        Vector(Random_Light, 0.0f, 0.0f, Light);

        GetBonePosition(pObject, CharacterSocket::Monster105_R_Eye, Position);
        CreateSprite(BITMAP_LIGHT + 1, Position, 0.3f, Light, pObject);

        GetBonePosition(pObject, CharacterSocket::Monster105_L_Eye, Position);
        CreateSprite(BITMAP_LIGHT + 1, Position, 0.3f, Light, pObject);

        GetBonePosition(pObject, CharacterSocket::Monster105_L_Arm00, Position);
        CreateSprite(BITMAP_LIGHT + 1, Position, 1.0f, Light, pObject);

        GetBonePosition(pObject, CharacterSocket::Monster105_L_Arm01, Position);
        CreateSprite(BITMAP_LIGHT + 1, Position, 1.0f, Light, pObject);

        GetBonePosition(pObject, CharacterSocket::Monster105_L_Arm02, Position);
        CreateSprite(BITMAP_LIGHT + 1, Position, 1.0f, Light, pObject);

        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_AIDA_HELL_MOVE1 + WorldRandom() % 2));
        }

        if (visual.action == MONSTER01_ATTACK2)
        {
            GetBonePosition(pObject, CharacterSocket::Monster105_L_Hand, Position);
            CreateParticleFpsChecked(BITMAP_TRUE_FIRE, Position, Angle, Light, 6, 5.0f);

            GetBonePosition(pObject, CharacterSocket::Monster105_R_Hand, Position);
            CreateParticleFpsChecked(BITMAP_TRUE_FIRE, Position, Angle, Light, 6, 5.0f);
        }
        else
        {
            GetBonePosition(pObject, CharacterSocket::Monster105_L_Hand, Position);
            CreateParticleFpsChecked(BITMAP_TRUE_FIRE, Position, Angle, Light, 6, 2.5f);

            Vector(0.0f, 0.0f, -10.0f, Relative);
            GetBonePosition(pObject, CharacterSocket::Monster105_R_Hand, Relative, Position);
            CreateParticleFpsChecked(BITMAP_TRUE_FIRE, Position, Angle, Light, 6, 2.1f);
        }
        if ((visual.animationFrame >= 9.0f && visual.animationFrame <= 10.0f) &&
            visual.action == MONSTER01_ATTACK2)
        {
            CreateEffectFpsChecked(BITMAP_BOSS_LASER, Position, pObject->Angle, Light, 1);
        }

    }
    break;
    case MODEL_BLOODY_ORC: {
        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_AIDA_FORESTORC_MOVE1 + WorldRandom() % 2));
        }

    }
    break;
    case MODEL_BLOODY_DEATH_RIDER: {
        vec3_t Relative, Position, Light, Angle = {0.0f, 0.0f, 0.0f};

        float Random_Light = (float)(WorldRandom() % 10) / 100.0f + 0.2f;
        Vector(60.0f, -30.0f, 0.0f, Relative);
        Vector(0.0f, Random_Light, 0.0f, Light);

        GetBonePosition(pObject, CharacterSocket::Monster102_Head, Relative, Position);
        CreateSprite(BITMAP_LIGHT + 1, Position, 3.0f, Light, pObject);
        CreateParticleFpsChecked(BITMAP_SPARK + 1, Position, Angle, Light, 7);

        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_AIDA_DEATHRAIDER_MOVE1 + WorldRandom() % 2));
        }

    }
    break;

    case MODEL_BLOODY_GOLEM: {
        vec3_t Position, Light, Angle = {0.0f, 0.0f, 0.0f};
        Vector(1.0f, 1.0f, 1.0f, Light);

        GetBonePosition(pObject, CharacterSocket::Monster101_L_Arm, Position);
        CreateParticleFpsChecked(BITMAP_WATERFALL_2, Position, Angle, Light, 1);
        GetBonePosition(pObject, CharacterSocket::Monster101_R_Arm, Position);
        CreateParticleFpsChecked(BITMAP_WATERFALL_2, Position, Angle, Light, 1);
        GetBonePosition(pObject, CharacterSocket::Monster101_Head, Position);
        CreateParticleFpsChecked(BITMAP_WATERFALL_2, Position, Angle, Light, 1);

        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_AIDA_BLUEGOLEM_MOVE1 + WorldRandom() % 2));
        }

        if (visual.action == MONSTER01_ATTACK1)
        {
            vec3_t Position, Light, Angle = {0.0f, 0.0f, 0.0f};
            Vector(1.0f, 1.0f, 1.0f, Light);
            if (visual.animationFrame >= 4.0f)
            {
                Vector(0.0f, 45.0f, 45.0f, Angle);
                vec3_t Relative = {30.0f, 0.0f, 0.0f};
                GetBonePosition(pObject, CharacterSocket::Monster101_L_Arm, Relative, Position);
                CreateParticleFpsChecked(BITMAP_SMOKE, Position, Angle, Light, 25);

                if (visual.animationFrame >= 5.0f && visual.animationFrame <= 5.5f)
                {
                    Vector(5.0f, 5.0f, 5.0f, Light);
                    GetBonePosition(pObject, CharacterSocket::Monster101_L_Arm, Relative, Position);
                    CreateParticleFpsChecked(BITMAP_SHOCK_WAVE, Position, Angle, Light, 3, 0.5f);
                }
            }

            if (CharactersClient.IsValidIndex(pCharacter->TargetCharacter) &&
                visual.animationFrame >= 6.9f)
            {
                CHARACTER *tc = &CharactersClient[pCharacter->TargetCharacter];
                OBJECT *to = &tc->Object;

                vec3_t vTemp;
                VectorCopy(to->Position, vTemp);
                vTemp[2] += 100.0f;

                CreateJointFpsChecked(BITMAP_JOINT_ENERGY, vTemp, pObject->Position, to->Angle, 16,
                                      pObject, 20.0f);
            }
        }

    }
    break;
    case MODEL_BLOODY_WITCH_QUEEN: {
        vec3_t Position, Light, Angle;
        float Random_Light = (float)(WorldRandom() % 30) / 100.0f + 0.6f;
        Vector(Random_Light + 0.5f, Random_Light - 0.05f, Random_Light + 0.5f, Light);

        GetBonePosition(pObject, CharacterSocket::Monster100_L_Hand, Position);
        CreateSprite(BITMAP_LIGHT + 1, Position, 0.8f, Light, pObject);
        GetBonePosition(pObject, CharacterSocket::Monster100_z02, Position);
        CreateSprite(BITMAP_LIGHT + 1, Position, 1.1f, Light, pObject);
        GetBonePosition(pObject, CharacterSocket::Monster100_z03, Position);
        CreateSprite(BITMAP_LIGHT + 1, Position, 1.0f, Light, pObject);
        GetBonePosition(pObject, CharacterSocket::Monster100_z04, Position);
        CreateSprite(BITMAP_LIGHT + 1, Position, 1.7f, Light, pObject);
        GetBonePosition(pObject, CharacterSocket::Monster100_z05, Position);
        CreateSprite(BITMAP_LIGHT + 1, Position, 0.8f, Light, pObject);

        Vector(0.7f, 0.5f, 0.7f, Light);
        vec3_t Relative = {0.0f, 0.0f, -65.0f};
        GetBonePosition(pObject, CharacterSocket::Monster100_Footstepst, Relative, Position);
        CreateParticleFpsChecked(BITMAP_LIGHT + 1, Position, Angle, Light, 4, 4.0f);

        if (visual.action == MONSTER01_WALK || visual.action == MONSTER01_RUN)
        {
            if (rand_fps_check(15))
                PlayBuffer(static_cast<ESound>(SOUND_AIDA_WITCHQUEEN_MOVE1 + WorldRandom() % 2));
        }

        if (visual.action == MONSTER01_ATTACK1 || visual.action == MONSTER01_ATTACK2)
        {
            if (visual.animationFrame >= 4.0f)
            {
                vec3_t Relative = {70.0f, 0.0f, 0.0f};
                GetBonePosition(pObject, CharacterSocket::Monster100_Footstepst, Relative,
                                Position);

                Vector(0.5f, 0.2f, 0.5f, Light);
                CreateParticleFpsChecked(BITMAP_SMOKE, Position, Angle, Light, 27, 2.0f);
                Vector(1.0f, 1.0f, 1.0f, Light);
                CreateParticleFpsChecked(BITMAP_LIGHT + 1, Position, Angle, Light, 2, 1.0f);
            }
            if (visual.animationFrame >= 6.0f && visual.action == MONSTER01_ATTACK1)
            {
                vec3_t Position, Light, Angle = {0.0f, 0.0f, 0.0f};
                vec3_t Relative = {70.0f, 0.0f, 0.0f};
                GetBonePosition(pObject, CharacterSocket::Monster100_Footstepst, Relative,
                                Position);
                Vector(1.0f, 1.0f, 1.0f, Light);
                CreateParticleFpsChecked(BITMAP_LIGHT + 1, Position, Angle, Light, 3, 1.3f);
            }
            if (CharactersClient.IsValidIndex(pCharacter->TargetCharacter))
            {
                CHARACTER *tc = &CharactersClient[pCharacter->TargetCharacter];
                OBJECT *to = &tc->Object;

                vec3_t vTemp;
                VectorCopy(to->Position, vTemp);
                vTemp[2] += 100.0f;

                if (visual.animationFrame >= 6.0f && visual.action == MONSTER01_ATTACK1)
                {
                    CreateParticleFpsChecked(BITMAP_LIGHT + 1, vTemp, Angle, Light, 3, 1.3f);
                }
                if (visual.animationFrame >= 6.0f && visual.action == MONSTER01_ATTACK2)
                {
                    CreateJointFpsChecked(BITMAP_JOINT_THUNDER, to->Position, vTemp, pObject->Angle,
                                          16);
                }
            }
        }

    }
    break;
    }
    return false;
}

bool CGMAida::AttackEffectAidaMonster(CHARACTER *pCharacter, OBJECT *pObject, BMD *pModel)
{
    if (!IsInAida())
        return false;

    switch (pCharacter->MonsterIndex)
    {
    case MONSTER_WITCH_QUEEN: {
        if (pCharacter->CheckAttackTime(10) && pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            vec3_t Light;
            Vector(1.f, 1.f, 1.f, Light);
            CreateEffect(BITMAP_JOINT_FORCE, pObject->Position, pObject->Angle, Light, 1);
            pCharacter->SetLastAttackEffectTime();
        }
    }
        return true;
    case MONSTER_DEATH_TREE: {
        if (pCharacter->CheckAttackTime(10) && pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            vec3_t Light;
            Vector(1.f, 1.f, 1.f, Light);
            CreateEffect(MODEL_TREE_ATTACK, pObject->Position, pObject->Angle, Light);
            pCharacter->SetLastAttackEffectTime();
        }
    }
        return true;
    case MONSTER_HELL_MAINE: {
        if (pCharacter->CheckAttackTime(10) && pObject->CurrentAction == MONSTER01_ATTACK1)
        {
            vec3_t light{1.f, 1.f, 1.f};
            constexpr int stormCount = 5;
            for (int storm = 0; storm < stormCount; ++storm)
                CreateEffect(MODEL_STORM, pObject->Position, pObject->Angle, light, 3 + storm);
            pCharacter->SetLastAttackEffectTime();
        }
    }
        return true;
    case MONSTER_BLOODY_WITCH_QUEEN: {
        if (pCharacter->CheckAttackTime(10) && pObject->CurrentAction == MONSTER01_ATTACK2)
        {
            vec3_t Light;
            Vector(1.f, 1.f, 1.f, Light);
            CreateEffect(BITMAP_JOINT_FORCE, pObject->Position, pObject->Angle, Light, 1);
            pCharacter->SetLastAttackEffectTime();
        }
    }
        return true;
    }
    return false;
}

bool CGMAida::CreateMist(PARTICLE *pParticleObj)
{
    if (!IsInAida())
        return false;
    if (!IsInAidaSection2(Hero->Object.Position))
        return false;

    return false;
}

bool CGMAida::MoveObject(OBJECT *object)
{
    return MoveAidaObject(object);
}

bool CGMAida::AdvanceObjectVisual(OBJECT *object, BMD *model, float)
{
    return AdvanceAidaObjectVisual(object, model);
}

bool CGMAida::AttackEffectBeforeShared(CHARACTER *character, OBJECT *object, BMD *model)
{
    return AttackEffectAidaMonster(character, object, model);
}

bool CGMAida::CreateWeather(PARTICLE *particle, int)
{
    return CreateMist(particle);
}

void CGMAida::ConfigureAmbientFish(OBJECT *object)
{
    if (gMapManager.ContextMap() == WD_33AIDA)
        TheMapProcess().Tarkan().ConfigureAmbientFish(object);
}

bool CGMAida::CanCreateAmbientFish(int index)
{
    return TerrainWall[index] == 0 || TerrainWall[index] == TW_CHARACTER;
}

bool GMEmpireGuardian1::MoveStructureVisual(OBJECT *o, BMD *b)
{
    if (gMapManager.IsEmpireGuardian1() == false && gMapManager.IsEmpireGuardian2() == false &&
        gMapManager.IsEmpireGuardian3() == false && gMapManager.IsEmpireGuardian4() == false)
    {
        return false;
    }

    vec3_t vPos, vRelativePos, Light;

    switch (o->Type)
    {
    case MODEL_LION_GATE: {
        if (o->CurrentAction == MONSTER01_DIE)
        {
            if ((int)o->LifeTime == 100)
            {
                o->LifeTime = 90;

                Vector(0, 0, 200.0f, vRelativePos);
                b->TransformPosition(o->BoneTransform[0], vRelativePos, vPos, true);
                CreateEffect(MODEL_DOOR_CRUSH_EFFECT, vPos, o->Angle, o->Light, 0, o, 0, 0);
            }
        }
    }
        return true;
    case MODEL_STATUE: {
        if (o->CurrentAction == MONSTER01_DIE)
        {
            if ((int)o->LifeTime == 100)
            {
                o->LifeTime = 90;

                Vector(0, 0, 0.0f, vRelativePos);
                b->TransformPosition(o->BoneTransform[1], vRelativePos, vPos, true);
                CreateEffect(MODEL_STATUE_CRUSH_EFFECT, vPos, o->Angle, o->Light, 0, o, 0, 0);

                CreateEffect(MODEL_STATUE_CRUSH_EFFECT_PIECE04, o->Position, o->Angle, o->Light, 0);

                vec3_t Angle;
                Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);

                CreateEffect(MODEL_STONE1 + WorldRandom() % 2, vPos, Angle, o->Light, 0);
                CreateEffect(MODEL_STONE1 + WorldRandom() % 2, vPos, Angle, o->Light, 0);
                CreateEffect(MODEL_STONE1 + WorldRandom() % 2, vPos, Angle, o->Light, 0);
            }
        }
        else
        {
            float Luminosity = sinf(WorldTime * 0.003f) * 0.2f + 0.6f;
            Vector(1.0f * Luminosity, 0.8f * Luminosity, 0.2f * Luminosity, Light);

            b->TransformByObjectBone(vPos, o, 3);
            CreateSprite(BITMAP_LIGHT_RED, vPos, 1.0f, Light, o);
            b->TransformByObjectBone(vPos, o, 4);
            CreateSprite(BITMAP_LIGHT_RED, vPos, 1.0f, Light, o);
        }
    }
        return true;
    }

    return false;
}

void GMEmpireGuardian1::EmitMonsterEvents(OBJECT &object, BMD &model,
                                          WorldCharacterVisualState &visual)
{
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    constexpr std::array<std::pair<int, float>, 1> lucas{{{MONSTER01_ATTACK2, 4.f}}};
    constexpr std::array<std::pair<int, float>, 2> fred{
        {{MONSTER01_ATTACK2, 4.5f}, {MONSTER01_ATTACK3, 4.5f}}};
    constexpr std::array<std::pair<int, float>, 1> devil{{{MONSTER01_ATTACK2, 2.5f}}};
    constexpr std::array<std::pair<int, float>, 1> instructor{{{MONSTER01_APEAR, 5.f}}};
    constexpr std::array<std::pair<int, float>, 1> forsaker{{{MONSTER01_ATTACK2, 4.5f}}};
    constexpr std::array<std::pair<int, float>, 1> hammer{{{MONSTER01_ATTACK3, 1.7f}}};
    constexpr std::array<std::pair<int, float>, 1> jerry{{{MONSTER01_ATTACK3, 0.2f}}};
    constexpr std::array<std::pair<int, float>, 2> aticles{
        {{MONSTER01_ATTACK2, 6.6f}, {MONSTER01_APEAR, 6.6f}}};
    constexpr std::array<std::pair<int, float>, 4> gaion{{{MONSTER01_ATTACK1, 0.f},
                                                          {MONSTER01_ATTACK2, 0.f},
                                                          {MONSTER01_ATTACK3, 0.f},
                                                          {MONSTER01_ATTACK4, 0.f}}};
    std::span<const std::pair<int, float>> markers;
    switch (object.Type)
    {
    case MODEL_LUCAS:
    case MODEL_DEFENDER:
        markers = lucas;
        break;
    case MODEL_FRED:
        markers = fred;
        break;
    case MODEL_DEVIL_LORD:
        markers = devil;
        break;
    case MODEL_COMBAT_INSTRUCTOR:
        markers = instructor;
        break;
    case MODEL_FORSAKER:
        markers = forsaker;
        break;
    case MODEL_HAMMERIZE:
        markers = hammer;
        break;
    case MODEL_DEATH_ANGEL_3:
    case MODEL_JERRY:
        markers = jerry;
        break;
    case MODEL_ATICLES_HEAD:
        markers = aticles;
        break;
    case MODEL_GAYION:
        markers = gaion;
        break;
    default:
        return;
    }
    object.MotionTrace.VisitAnimationEvents(
        WorldTime, markers, [&](std::size_t event, float fraction) {
            auto birth =
                sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR * (1.f - fraction));
            vec3_t origin, angle, light, position;
            object.MotionTrace.Sample(WorldTime, fraction, object.Position, origin);
            VectorCopy(object.Angle, angle);
            angle[2] = object.MotionTrace.SampleYaw(WorldTime, fraction, angle[2]);
            VectorCopy(visual.movement.light, light);
            AnimationPoseSample pose(&object, model.BoneHead, model.BodyHeight, false,
                                     model.PoseAssetIdentity());
            if (object.Type == MODEL_LUCAS)
            {
                for (int side : {0, 1, -1})
                {
                    vec3_t rotation, offset{80.f * side, 0.f, 0.f}, rotated;
                    VectorCopy(angle, rotation);
                    rotation[2] += 10.f * side;
                    vec34_t matrix;
                    AngleMatrix(rotation, matrix);
                    VectorRotate(offset, matrix, rotated);
                    VectorAdd(origin, rotated, position);
                    CreateEffect(MODEL_DARK_SCREAM, position, rotation, light, 1, &object,
                                 object.PKKey, 9);
                    CreateEffect(MODEL_DARK_SCREAM_FIRE, position, rotation, light, 1, &object,
                                 object.PKKey, 9);
                }
            }
            else if (object.Type == MODEL_DEFENDER)
            {
                vec3_t offset{0.f, -100.f, 100.f}, rotated;
                vec34_t matrix;
                AngleMatrix(angle, matrix);
                VectorRotate(offset, matrix, rotated);
                VectorAdd(origin, rotated, position);
                Vector(0.8f, 0.8f, 1.f, light);
                CreateEffect(MODEL_EFFECT_EG_GUARDIANDEFENDER_ATTACK2, position, angle, light, 0,
                             &object);
            }
            else if (object.Type == MODEL_COMBAT_INSTRUCTOR)
            {
                CreateEffect(MODEL_WAVES, origin, angle, light, 1);
                CreateEffect(MODEL_WAVES, origin, angle, light, 1);
                CreateEffect(MODEL_PIERCING2, origin, angle, light);
                PlayBuffer(SOUND_ATTACK_SPEAR);
            }
            else if (object.Type == MODEL_ATICLES_HEAD)
            {
                vec3_t offset{};
                pose.SampleBonePosition(model, object, 23, offset, WorldTime, fraction, position);
                position[2] -= 150.f;
                angle[2] += 5.f;
                if (event == 0)
                    CreateEffect(MODEL_PIERCING2, position, angle, light, 0);
                else
                {
                    for (int child = 0; child < 6; ++child)
                        CreateEffect(MODEL_WAVES, position, angle, light, 1);
                    for (int child = 0; child < 5; ++child)
                        CreateEffect(MODEL_PIERCING2, position, angle, light);
                }
                PlayBuffer(SOUND_ATTACK_SPEAR);
            }
            else if (object.Type == MODEL_DEVIL_LORD || (object.Type == MODEL_FRED && event == 1))
            {
                const bool devilLord = object.Type == MODEL_DEVIL_LORD;
                vec3_t offset{0.f, 0.f, devilLord ? 100.f : 0.f};
                pose.SampleBonePosition(model, object, 11, offset, WorldTime, fraction, position);
                if (devilLord)
                {
                    const float eventTime =
                        WorldTime -
                        FPS_ANIMATION_FACTOR * (1.f - fraction) *
                            (1000.f / sessionKeeper_.ApplicationConfig().legacyReferenceFps);
                    const float luminosity = sinf(eventTime * 0.003f) * 0.2f + 0.6f;
                    Vector(luminosity, 0.5f * luminosity, 0.4f * luminosity, light);
                }
                else
                    Vector(1.f, 0.1f, 0.f, light);
                CreateEffect(MODEL_SKILL_FURY_STRIKE, position, angle, light, 1, &object, -1, 0, 0);
            }
            else
            {
                CHARACTER *targetCharacter = visual.target.Resolve();
                if (!targetCharacter)
                    return;
                OBJECT &target = targetCharacter->Object;
                vec3_t targetPosition, targetAngle;
                target.MotionTrace.Sample(WorldTime, fraction, target.Position, targetPosition);
                VectorCopy(target.Angle, targetAngle);
                targetAngle[2] = target.MotionTrace.SampleYaw(WorldTime, fraction, targetAngle[2]);
                if (object.Type == MODEL_FORSAKER)
                    CreateEffect(BITMAP_MAGIC + 1, targetPosition, targetAngle, target.Light, 1,
                                 &target);
                else if (object.Type == MODEL_HAMMERIZE || object.Type == MODEL_DEATH_ANGEL_3 ||
                         object.Type == MODEL_JERRY)
                    CreateEffect(MODEL_EMPIREGUARDIAN_BLOW_OF_DESTRUCTION, origin, angle,
                                 targetPosition, 0, &object, -1, 0, 0, 0, 1.f);
                else if (object.Type == MODEL_FRED)
                {
                    vec3_t offset{};
                    pose.SampleBonePosition(model, object, 20, offset, WorldTime, fraction,
                                            position);
                    CreateEffect(MODEL_DEASULER, position, angle, targetPosition, 0, &object, -1, 0,
                                 0, 0, 1.8f);
                }
                else // Gaion's thrown swords; attached sword refresh stays with presentation.
                {
                    constexpr std::array<std::pair<int, int>, 5> swords{
                        {{4, MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_},
                         {8, MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_},
                         {2, MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_},
                         {10, MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_},
                         {6, MODEL_SWORDMAIN01_EMPIREGUARDIAN_BOSS_GAION_}}};
                    std::array<vec34_t, MAX_BONES> bones;
                    pose.EvaluateAtTime(model, object, WorldTime, fraction, bones.data());
                    const int first = event == 0 ? 2 : event == 3 ? 4 : 0;
                    const int end = event == 1 ? 2 : event == 0 ? 4 : 5;
                    for (int sword = first; sword < end; ++sword)
                    {
                        const auto [bone, type] = swords[sword];
                        for (int axis = 0; axis < 3; ++axis)
                            position[axis] = bones[bone][axis][3] * object.Scale + origin[axis];
                        CreateEffect(type, position, angle, targetPosition, event == 2 ? 3 : 1,
                                     &object, -1, 0, 0, 0, object.Scale);
                    }
                }
            }
        });
}

void GMEmpireGuardian1::EmitRaymondEvents(OBJECT &object, BMD &model, const vec3_t light)
{
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    constexpr std::array<std::pair<int, float>, 2> markers{
        {{MONSTER01_ATTACK2, 2.5f}, {MONSTER01_ATTACK3, 7.f}}};
    AnimationPoseSample pose(&object, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    object.MotionTrace.VisitAnimationEvents(
        WorldTime, markers, [&](std::size_t event, float fraction) {
            auto birth =
                sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR * (1.f - fraction));
            vec3_t position, angle;
            VectorCopy(object.Angle, angle);
            angle[2] = object.MotionTrace.SampleYaw(WorldTime, fraction, angle[2]);
            if (event == 0)
            {
                vec3_t relative{0.f, 0.f, 100.f}, white{1.f, 1.f, 1.f};
                pose.SampleBonePosition(model, object, 31, relative, WorldTime, fraction, position);
                CreateEffect(MODEL_SKILL_FURY_STRIKE, position, angle, white, 1, &object);
            }
            else
            {
                vec3_t color;
                VectorCopy(light, color);
                object.MotionTrace.Sample(WorldTime, fraction, object.Position, position);
                CreateEffect(MODEL_CIRCLE, position, angle, color);
                CreateEffect(MODEL_CIRCLE_LIGHT, position, angle, color);
            }
        });
}

bool GMEmpireGuardian1::MoveMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                          WorldCharacterVisualState &visual)
{
    ObjectDrawInput presentation(o);
    visual.movement.Apply(presentation);
    if (gMapManager.IsEmpireGuardian1() == false && gMapManager.IsEmpireGuardian2() == false &&
        gMapManager.IsEmpireGuardian3() == false && gMapManager.IsEmpireGuardian4() == false)
    {
        return false;
    }

    EmitMonsterEvents(*o, *b, visual);

    vec3_t vPos, Light;

    switch (o->Type)
    {
    case MODEL_RAYMOND: {
        EmitRaymondEvents(*o, *b, visual.movement.light);
        vec3_t EndPos, EndRelative, Light, vPos;
        Vector(1.0f, 1.0f, 1.0f, Light);

        switch (visual.action)
        {
        case MONSTER01_STOP1:
        case MONSTER01_STOP2:
        case MONSTER01_ATTACK1:
        case MONSTER01_APEAR: {
            {
                vec3_t Light;

                if (m_bCurrentIsRage_Raymond == true)
                {
                    Vector(1.0f, 1.0f, 1.0f, Light);
                    CreateInferno(o->Position);

                    CreateEffect(MODEL_CIRCLE, o->Position, o->Angle, Light, 4, o);
                    CreateEffect(MODEL_CIRCLE, o->Position, o->Angle, Light, 4, o);
                    m_bCurrentIsRage_Raymond = false;
                }
            }
        }
        break;
        case MONSTER01_DIE:
            break;
        case MONSTER01_WALK: {
            Vector(0.9f, 0.2f, 0.1f, Light);
            if (7.5f <= visual.animationFrame && visual.animationFrame < 8.0f)
            {
                b->TransformByObjectBone(vPos, presentation, 54);
                CreateParticleFpsChecked(BITMAP_SMOKE + 1, vPos, o->Angle, Light, 0, 0.1f);
            }
            if (0.0f <= visual.animationFrame && visual.animationFrame < 1.0f)
            {
                b->TransformByObjectBone(vPos, presentation, 59);
                CreateParticleFpsChecked(BITMAP_SMOKE + 1, vPos, o->Angle, Light, 0, 0.1f);
            }
        }
        break;
        case MONSTER01_ATTACK3: {
            Vector(o->Position[0] + WorldRandom() % 1024 - 512,
                   o->Position[1] + WorldRandom() % 1024 - 512, o->Position[2], EndPos);
            CreateEffectFpsChecked(MODEL_FIRE, EndPos, o->Angle, visual.movement.light);
        }
        break;
        }
    }
        return true;
    case MODEL_LUCAS: {
        switch (visual.action)
        {
        case MONSTER01_STOP1:
        case MONSTER01_STOP2:
            break;
        case MONSTER01_WALK:
            break;
        case MONSTER01_DIE: {
            // 					float Scale = 0.3f;
            // 					b->TransformByObjectBone( vPos, presentation, 30 );
            // 					CreateParticle(BITMAP_SMOKE+1, vPos, o->Angle, visual.movement.light, 1, Scale);
            // 					b->TransformByObjectBone( vPos, presentation, 17 );
            // 					CreateParticle(BITMAP_SMOKE+1, vPos, o->Angle, visual.movement.light, 1, Scale);
        }
        break;
        case MONSTER01_ATTACK1: {
            if (2.0f <= visual.animationFrame && visual.animationFrame < 15.0f)
            {
                vec3_t Light;
                //Vector(0.3f, 0.3f, 0.3f, Light);
                Vector(0.3f, 0.8f, 0.4f, Light);

                vec3_t StartPos, StartRelative;
                vec3_t EndPos, EndRelative;

                float fActionSpeed = b->Actions[visual.action].PlaySpeed;
                float fSpeedPerFrame = fActionSpeed / 10.f;
                float fAnimationFrame = visual.animationFrame - fActionSpeed;
                for (int i = 0; i < 25; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, presentation.headAngle);

                    Vector(0.0f, 0.0f, 0.0f, StartRelative);
                    Vector(0.0f, 0.0f, 0.0f, EndRelative);

                    b->TransformPosition(BoneTransform[39], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[40], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 0);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }
        }
        break;
        case MONSTER01_ATTACK2: {

            if (2.0f <= visual.animationFrame && visual.animationFrame < 15.0f)
            {
                vec3_t Light;
                //Vector(0.3f, 0.3f, 0.3f, Light);
                Vector(0.3f, 0.8f, 0.4f, Light);

                vec3_t StartPos, StartRelative;
                vec3_t EndPos, EndRelative;

                float fActionSpeed = b->Actions[visual.action].PlaySpeed;
                float fSpeedPerFrame = fActionSpeed / 10.f;
                float fAnimationFrame = visual.animationFrame - fActionSpeed;
                for (int i = 0; i < 25; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, presentation.headAngle);

                    Vector(0.0f, 0.0f, 0.0f, StartRelative);
                    Vector(0.0f, 0.0f, 0.0f, EndRelative);

                    b->TransformPosition(BoneTransform[39], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[40], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 0);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }
        }
        break;
        case MONSTER01_ATTACK3: {
            if (visual.animationFrame >= 3.4f && visual.animationFrame <= 9.0f)
            {
                AdvanceSkillEarthQuake(c, o, b, visual, 12);
            }

            if (2.0f <= visual.animationFrame && visual.animationFrame < 15.0f)
            {
                vec3_t Light;
                //Vector(0.3f, 0.3f, 0.3f, Light);
                Vector(0.3f, 0.8f, 0.4f, Light);

                vec3_t StartPos, StartRelative;
                vec3_t EndPos, EndRelative;

                float fActionSpeed = b->Actions[visual.action].PlaySpeed;
                float fSpeedPerFrame = fActionSpeed / 10.f;
                float fAnimationFrame = visual.animationFrame - fActionSpeed;
                for (int i = 0; i < 25; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, presentation.headAngle);

                    Vector(0.0f, 0.0f, 0.0f, StartRelative);
                    Vector(0.0f, 0.0f, 0.0f, EndRelative);

                    b->TransformPosition(BoneTransform[39], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[40], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 0);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }
        }
        break;
        case MONSTER01_APEAR: {
            {
                vec3_t Light;

                if (m_bCurrentIsRage_Ercanne == true)
                {
                    Vector(1.0f, 1.0f, 1.0f, Light);
                    CreateInferno(o->Position);

                    CreateEffectFpsChecked(MODEL_CIRCLE, o->Position, o->Angle, Light, 4, o);
                    CreateEffectFpsChecked(MODEL_CIRCLE, o->Position, o->Angle, Light, 4, o);
                    m_bCurrentIsRage_Ercanne = false;
                }
            }
        }
        break;
        }
    }
        return true;
    case MODEL_FRED: {
        switch (visual.action)
        {
        case MONSTER01_STOP1:
        case MONSTER01_STOP2:
            break;
        case MONSTER01_WALK:
            break;
        case MONSTER01_DIE: {
        }
        break;
        case MONSTER01_ATTACK1: {
        }
        break;
        case MONSTER01_ATTACK2: {
            float _fActSpdTemp = b->Actions[visual.action].PlaySpeed;

        }
        break;
        case MONSTER01_ATTACK3: {
            vec3_t Angle, p, Position;
            float Matrix[3][4];
            Vector(0.f, -500.f, 0.f, p);
            for (int j = 0; j < 3; j++)
            {
                Vector((float)(WorldRandom() % 90), 0.f, (float)(WorldRandom() % 360), Angle);
                AngleMatrix(Angle, Matrix);
                VectorRotate(p, Matrix, Position);
                VectorSubtract(o->Position, Position, Position);
                Position[2] += 120.f;
                CreateJointFpsChecked(BITMAP_JOINT_HEALING, Position, o->Position, Angle, 17, o,
                                      10.f);
            }
            vec3_t Light;
            Vector(1.f, 0.1f, 0.f, Light);
            Vector(0.f, 0.f, 0.f, p);
            for (int i = 0; i < 10; i++)
            {
                b->TransformPosition(presentation.bones[WorldRandom() % 62], p, Position, true);
                CreateParticleFpsChecked(BITMAP_LIGHT, Position, o->Angle, Light, 5,
                                         0.5f + (WorldRandom() % 100) / 50.f);

                b->TransformPosition(presentation.bones[50], p, Position, true);
                CreateParticleFpsChecked(BITMAP_LIGHT, Position, o->Angle, Light, 5,
                                         0.5f + (WorldRandom() % 100) / 50.f);
            }

            float _fActSpdTemp = b->Actions[visual.action].PlaySpeed;


        }
        break;
        case MONSTER01_APEAR: {
            {
                vec3_t Light;

                if (m_bCurrentIsRage_Daesuler == true)
                {
                    Vector(1.0f, 1.0f, 1.0f, Light);
                    CreateInferno(o->Position);

                    CreateEffectFpsChecked(MODEL_CIRCLE, o->Position, o->Angle, Light, 4, o);
                    CreateEffectFpsChecked(MODEL_CIRCLE, o->Position, o->Angle, Light, 4, o);
                    m_bCurrentIsRage_Daesuler = false;
                }
            }
        }
        break;
        }
    }
    break;
    case MODEL_DEVIL_LORD: {
        //wing node 1,2,3
        Vector(1.0f, 0.8f, 0.2f, Light);
        b->TransformByObjectBone(vPos, presentation, 81);
        CreateSprite(BITMAP_LIGHT_RED, vPos, 0.5f, Light, o);
        b->TransformByObjectBone(vPos, presentation, 82);
        CreateSprite(BITMAP_LIGHT_RED, vPos, 1.3f, Light, o);
        b->TransformByObjectBone(vPos, presentation, 83);
        CreateSprite(BITMAP_LIGHT_RED, vPos, 2.0f, Light, o);

        //wing node 4,5,6
        Vector(1.0f, 0.8f, 0.2f, Light);
        b->TransformByObjectBone(vPos, presentation, 105);
        CreateSprite(BITMAP_LIGHT_RED, vPos, 0.5f, Light, o);
        b->TransformByObjectBone(vPos, presentation, 104);
        CreateSprite(BITMAP_LIGHT_RED, vPos, 1.3f, Light, o);
        b->TransformByObjectBone(vPos, presentation, 103);
        CreateSprite(BITMAP_LIGHT_RED, vPos, 2.0f, Light, o);

        //head
        Vector(1.0f, 0.8f, 0.2f, Light);
        b->TransformByObjectBone(vPos, presentation, 10);
        CreateSprite(BITMAP_LIGHT_RED, vPos, 1.5f, Light, o);

        //neak
        Vector(1.0f, 0.8f, 0.2f, Light);
        b->TransformByObjectBone(vPos, presentation, 9);
        CreateSprite(BITMAP_LIGHT_RED, vPos, 2.0f, Light, o);

        //spine
        Vector(1.0f, 0.8f, 0.2f, Light);
        b->TransformByObjectBone(vPos, presentation, 6);
        CreateSprite(BITMAP_LIGHT_RED, vPos, 4.0f, Light, o);

        //L arm
        Vector(1.0f, 0.8f, 0.2f, Light);
        b->TransformByObjectBone(vPos, presentation, 13);
        CreateSprite(BITMAP_LIGHT_RED, vPos, 2.0f, Light, o);
        b->TransformByObjectBone(vPos, presentation, 30);
        CreateSprite(BITMAP_LIGHT_RED, vPos, 2.0f, Light, o);

        //R arm
        Vector(1.0f, 0.8f, 0.2f, Light);
        b->TransformByObjectBone(vPos, presentation, 32);
        CreateSprite(BITMAP_LIGHT_RED, vPos, 2.0f, Light, o);
        b->TransformByObjectBone(vPos, presentation, 75);
        CreateSprite(BITMAP_LIGHT_RED, vPos, 2.0f, Light, o);

        //sword
        float Luminosity = sinf(WorldTime * 0.003f) * 0.2f + 0.6f;
        Vector(1.0f * Luminosity, 0.5f * Luminosity, 0.4f * Luminosity, Light);

        int temp[] = {49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
                      60, 61, 62, 63, 64, 65, 66, 67, 71, 72};

        for (int i = 0; i < 21; i++)
        {
            b->TransformByObjectBone(vPos, presentation, temp[i]);
            CreateSprite(BITMAP_LIGHTMARKS, vPos, 0.6f, Light, o);
        }
        b->TransformByObjectBone(vPos, presentation, 68);
        CreateSprite(BITMAP_LIGHTMARKS, vPos, 0.3f, Light, o);
        b->TransformByObjectBone(vPos, presentation, 69);
        CreateSprite(BITMAP_LIGHTMARKS, vPos, 0.2f, Light, o);
        b->TransformByObjectBone(vPos, presentation, 70);
        CreateSprite(BITMAP_LIGHTMARKS, vPos, 0.3f, Light, o);

        // Action 정의
        float _fActSpdTemp = b->Actions[visual.action].PlaySpeed;

        switch (visual.action)
        {
        case MONSTER01_STOP1:
        case MONSTER01_STOP2:
            break;
        case MONSTER01_WALK:
            break;
        case MONSTER01_DIE: {
        }
        break;
        case MONSTER01_ATTACK1: {
        }
        break;
        case MONSTER01_ATTACK2: {

        }
        break;
        case MONSTER01_ATTACK3: {
            if (visual.animationFrame >= 3.4f && visual.animationFrame <= 9.0f)
            {
                AdvanceSkillEarthQuake(c, o, b, visual, 12);
            }
        }
        break;
        case MONSTER01_APEAR: {
            {
                vec3_t Light;

                if (m_bCurrentIsRage_Gallia == true)
                {
                    Vector(1.0f, 1.0f, 1.0f, Light);
                    CreateInferno(o->Position);

                    CreateEffectFpsChecked(MODEL_CIRCLE, o->Position, o->Angle, Light, 4, o);
                    CreateEffectFpsChecked(MODEL_CIRCLE, o->Position, o->Angle, Light, 4, o);
                    m_bCurrentIsRage_Gallia = false;
                }
            }
        }
        break;
        }
    }
        return true;

    case MODEL_COMBAT_INSTRUCTOR: {
        switch (visual.action)
        {
        case MONSTER01_WALK: {
            Vector(0.5f, 0.2f, 0.1f, visual.movement.light);

            if (7.0f <= visual.animationFrame && visual.animationFrame < 8.0f)
            {
                b->TransformByObjectBone(vPos, presentation, 53);
                vPos[2] += 25.0f;
                CreateParticleFpsChecked(BITMAP_SMOKE + 1, vPos, o->Angle, visual.movement.light,
                                         1);
            }
            if (1.0f <= visual.animationFrame && visual.animationFrame < 2.0f)
            {
                b->TransformByObjectBone(vPos, presentation, 48);
                vPos[2] += 25.0f;
                CreateParticleFpsChecked(BITMAP_SMOKE + 1, vPos, o->Angle, visual.movement.light,
                                         1);
            }
        }
        break;
        case MONSTER01_ATTACK1: {
        }
        break;
        case MONSTER01_ATTACK2: {
        }
        break;
        case MONSTER01_APEAR: {
        }
        break;
        }
    }
        return true;
    case MODEL_DEFENDER: {
        // HeadTargetAngle
        switch (visual.action)
        {
        case MONSTER01_ATTACK1: {
        }
        break;
        case MONSTER01_ATTACK2: {

        }
        break;
        case MONSTER01_APEAR: {
        }
        break;
        };
    }
        return true;
    case MODEL_FORSAKER: {
        vec3_t vRelative, vLight, vPos;
        switch (visual.action)
        {
        case MONSTER01_STOP1:
        case MONSTER01_STOP2:
        case MONSTER01_WALK: {
            Vector(10.0f, 0.0f, 0.0f, vRelative);
            b->TransformPosition(presentation.bones[55], vRelative, vPos, true);
            float fLumi = (sinf(WorldTime * 0.001f) + 1.0f) * 0.3f + 0.4f;
            Vector(fLumi * 0.5f, fLumi * 0.5f, fLumi * 0.5f, vLight);
            CreateSprite(BITMAP_FLARE_BLUE, vPos, 1.5f, vLight, o);
            Vector(0.5f, 0.5f, 0.5f, vLight);

            CreateSprite(BITMAP_FLARE_BLUE, vPos, 1.2f, vLight, o, -WorldTime * 0.1f);
        }
        break;
        case MONSTER01_DIE:
            break;
        case MONSTER01_ATTACK1: {
            Vector(10.0f, 0.0f, 0.0f, vRelative);
            b->TransformPosition(presentation.bones[55], vRelative, vPos, true);
            float fLumi = (sinf(WorldTime * 0.001f) + 1.0f) * 0.3f + 0.4f;
            float fScale = (sinf(WorldTime * 0.001f) + 1.0f) * 0.5f + 1.0f;
            Vector(fLumi * 0.5f, fLumi * 0.5f, fLumi * 0.5f, vLight);
            CreateSprite(BITMAP_FLARE_RED, vPos, 1.5f, vLight, o);
            Vector(0.5f, 0.5f, 0.5f, vLight);
            CreateSprite(BITMAP_FLARE_RED, vPos, 1.2f * fScale * visual.animationFrame * 0.15f,
                         vLight, o, -WorldTime * 0.1f);

            CHARACTER *tc = visual.target.Resolve();
            if (!tc)
                break;
            OBJECT *to = &tc->Object;

            if (visual.animationFrame > 10)
            {
                vec3_t p, Angle;
                Vector(0.0f, 0.0f, 0.0f, p);
                Vector(0.88f, 0.12f, 0.08f, vLight);
                b->TransformPosition(presentation.bones[55], p, vPos, true);
                Vector(-60.0f, 0.0f, o->Angle[2], Angle);
                Vector(-60.0f, 0.0f, o->Angle[2], Angle);
                CreateJointFpsChecked(BITMAP_JOINT_THUNDER, vPos, to->Position, Angle, 27, to,
                                      50.0f, -1, 0, 0, 0, vLight);
                CreateJointFpsChecked(BITMAP_JOINT_THUNDER, vPos, to->Position, Angle, 27, to,
                                      10.0f, -1, 0, 0, 0, vLight);
                CreateParticleFpsChecked(BITMAP_ENERGY, vPos, o->Angle, vLight);
            }
        }
        break;
        case MONSTER01_ATTACK2: {
            Vector(10.0f, 0.0f, 0.0f, vRelative);
            b->TransformPosition(presentation.bones[55], vRelative, vPos, true);
            float fLumi = (sinf(WorldTime * 0.001f) + 1.0f) * 0.3f + 0.4f;
            float fScale = (sinf(WorldTime * 0.001f) + 1.0f) * 0.5f + 1.0f;
            Vector(fLumi * 0.5f, fLumi * 0.5f, fLumi * 0.5f, vLight);
            CreateSprite(BITMAP_FLARE_BLUE, vPos, 1.5f, vLight, o);
            Vector(0.5f, 0.5f, 0.5f, vLight);
            //CreateSprite(BITMAP_FLARE_BLUE, vPos, 1.2f, vLight, o, -WorldTime*0.1f);
            CreateSprite(BITMAP_FLARE_BLUE, vPos, 1.2f * fScale * visual.animationFrame * 0.15f,
                         vLight, o, -WorldTime * 0.1f);

            CHARACTER *tc = visual.target.Resolve();
            if (!tc)
                break;
            OBJECT *to = &tc->Object;
        }
        break;
        }
    }
        return true;
    } //switch end

    return MoveStructureVisual(o, b);
}

void GMEmpireGuardian1::MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b)
{
    vec3_t Light;
    vec3_t StartPos, StartRelative;
    vec3_t EndPos, EndRelative;
    Vector(1.0f, 1.0f, 1.0f, Light);
    Vector(0.0f, 0.0f, 0.0f, StartRelative);
    Vector(0.0f, 0.0f, 0.0f, EndRelative);

    float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
    float fSpeedPerFrame = fActionSpeed / 10.f;
    float fAnimationFrame = o->AnimationFrame - fActionSpeed;

    switch (o->Type)
    {
    case MODEL_RAYMOND: {
        float Start_Frame = 0.0f;
        float End_Frame = 0.0f;

        switch (o->CurrentAction)
        {
        case MONSTER01_STOP1:
        case MONSTER01_STOP2:
        case MONSTER01_WALK:
        case MONSTER01_DIE:
            break;
        case MONSTER01_ATTACK1: {
            Start_Frame = 4.0f;
            End_Frame = 11.0f;
        }
        break;
        case MONSTER01_ATTACK2:
        case MONSTER01_ATTACK3: {
            Start_Frame = 5.0f;
            End_Frame = 10.0f;
        }
        break;
        }

        if ((o->AnimationFrame >= Start_Frame && o->AnimationFrame <= End_Frame) &&
            ((o->CurrentAction == MONSTER01_ATTACK1) || (o->CurrentAction == MONSTER01_ATTACK2) ||
             (o->CurrentAction == MONSTER01_ATTACK3)))
        {
            BMD *b = &Models[o->Type];
            vec3_t Light;

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fDelay = 5.0f;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / fDelay;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < fDelay; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);

                Vector(0.9f, 0.2f, 0.1f, Light);
                b->TransformPosition(BoneTransform[32], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[33], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 0);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_DEVIL_LORD: {
        switch (o->CurrentAction)
        {
        case MONSTER01_ATTACK1: {
            if (4.0f <= o->AnimationFrame && o->AnimationFrame < 6.0f)
            {
                Vector(1.0f, 0.2f, 0.0f, Light);
                Vector(0.0f, 0.0f, 0.0f, StartRelative);
                Vector(0.0f, 0.0f, 0.0f, EndRelative);

                for (int i = 0; i < 28; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, o->HeadAngle);

                    b->TransformPosition(BoneTransform[69], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[49], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 7);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }
        }
        break;
        case MONSTER01_ATTACK2: {
            if (4.0f <= o->AnimationFrame && o->AnimationFrame < 7.5f)
            {
                Vector(1.0f, 1.0f, 1.0f, Light);
                Vector(0.0f, 0.0f, 0.0f, StartRelative);
                Vector(0.0f, 0.0f, 0.0f, EndRelative);

                for (int i = 0; i < 28; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, o->HeadAngle);

                    b->TransformPosition(BoneTransform[69], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[49], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 2);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }
        }
        break;
        case MONSTER01_ATTACK3: {
            if (4.5f <= o->AnimationFrame && o->AnimationFrame < 7.8f)
            {
                Vector(1.0f, 0.4f, 0.0f, Light);
                Vector(0.0f, 0.0f, 0.0f, StartRelative);
                Vector(0.0f, 0.0f, 0.0f, EndRelative);

                for (int i = 0; i < 28; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, o->HeadAngle);

                    b->TransformPosition(BoneTransform[69], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[49], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 8);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }
        }
        break;
        } //attack switch end
    }
    break;

    case MODEL_QUARTER_MASTER: {
        if (o->CurrentAction == MONSTER01_ATTACK1 || o->CurrentAction == MONSTER01_ATTACK2)
        {
            vec3_t vLight;
            Vector(0.5f, 0.5f, 0.7f, vLight);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);
                b->TransformPosition(BoneTransform[33], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[34], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, vLight, 0, false, 0);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;

    case MODEL_COMBAT_INSTRUCTOR: {
        switch (o->CurrentAction)
        {
        case MONSTER01_ATTACK1: {
            if (4.0f <= o->AnimationFrame && o->AnimationFrame < 11.7f)
            {
                Vector(1.0f, 0.2f, 0.0f, Light);
                Vector(0.0f, 0.0f, 0.0f, StartRelative);
                Vector(0.0f, 0.0f, 0.0f, EndRelative);

                for (int i = 0; i < 18; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, o->HeadAngle);

                    b->TransformPosition(BoneTransform[36], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[37], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 7);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }
        }
        break;
        case MONSTER01_ATTACK2: {
            if (4.0f <= o->AnimationFrame && o->AnimationFrame < 6.840f)
            {
                Vector(1.0f, 1.0f, 1.0f, Light);
                Vector(0.0f, 0.0f, 0.0f, StartRelative);
                Vector(0.0f, 0.0f, 0.0f, EndRelative);

                for (int i = 0; i < 16; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, o->HeadAngle);

                    b->TransformPosition(BoneTransform[36], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[37], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 1);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }
        }
        break;
        case MONSTER01_APEAR: {
            if (4.0f <= o->AnimationFrame && o->AnimationFrame < 6.0f)
            {
                Vector(1.0f, 0.2f, 0.0f, Light);
                Vector(0.0f, 0.0f, 0.0f, StartRelative);
                Vector(0.0f, 0.0f, 0.0f, EndRelative);

                for (int i = 0; i < 28; i++)
                {
                    b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                        o->PriorAction, o->Angle, o->HeadAngle);

                    b->TransformPosition(BoneTransform[36], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[37], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 7);

                    fAnimationFrame += fSpeedPerFrame;
                }
            }
        }
        break;
        } // attack switch end
    }
    break;
    case MODEL_OCELOT: {
        if (o->CurrentAction == MONSTER01_ATTACK1 || o->CurrentAction == MONSTER01_ATTACK2)
        {
            vec3_t vLight;
            Vector(0.5f, 0.5f, 0.5f, vLight);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);
                b->TransformPosition(BoneTransform[28], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[29], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, vLight, 0, false, 0);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_ERIC: {
        float Start_Frame = 0.f;
        float End_Frame = 10.f;
        if ((o->AnimationFrame >= Start_Frame && o->AnimationFrame <= End_Frame &&
             o->CurrentAction == MONSTER01_ATTACK1) ||
            (o->AnimationFrame >= Start_Frame && o->AnimationFrame <= End_Frame &&
             o->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t Light;
            Vector(0.5f, 0.5f, 0.5f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 20.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 20; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);

                b->TransformPosition(BoneTransform[37], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[38], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 0);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    } //switch end
}

bool GMEmpireGuardian1::AdvanceObjectVisual(OBJECT *o, BMD *b, float)
{
    if (gMapManager.IsEmpireGuardian1() == false)
        return false;

    vec3_t p, Position, Light;
    Vector(0.f, 30.f, 0.f, Position);
    Vector(0.f, 0.f, 0.f, p);

    switch (o->Type)
    {
    case 115:
    case 117:
        AdvanceGateProjectile(o, b);
        return true;
    case 12: {
        PrepareWorldObjectPose(*o);
        vec3_t vPos, vRelativePos, vLight1, vLight2;
        float flumi = absf(sinf(WorldTime * 0.0008)) * 0.9f + 0.1f;
        float fScale = o->Scale * 0.3f * flumi;
        Vector(0.f, 0.f, 0.f, vPos);
        Vector(8.f, -3.f, -3.f, vRelativePos);
        Vector(flumi, flumi, flumi, vLight1);
        Vector(0.9f, 0.1f, 0.1f, vLight2);
        b->TransformPosition(BoneTransform[2], vRelativePos, vPos);
        CreateSprite(BITMAP_SHINY + 5, vPos, 0.5f, vLight2, o);
        CreateSprite(BITMAP_SHINY + 5, vPos, fScale, vLight1, o);
        Vector(3.f, -3.f, -3.5f, vRelativePos);
        b->TransformPosition(BoneTransform[3], vRelativePos, vPos);
        CreateSprite(BITMAP_SHINY + 5, vPos, 0.5f, vLight2, o);
        CreateSprite(BITMAP_SHINY + 5, vPos, fScale, vLight1, o);
    }
        return true;

    case 20: {
        if (o->AnimationFrame > 5.4f && o->AnimationFrame < 6.5f && rand_fps_check(1))
        {
            vec3_t Angle;
            for (int i = 0; i < 4; ++i)
            {
                Vector((float)(WorldRandom() % 60 + 60 + 90), 0.f, o->Angle[2] + 180, Angle);
                CreateJoint(BITMAP_JOINT_SPARK, o->Position, o->Position, Angle, 5, o);
            }
            CreateParticle(BITMAP_SPARK, o->Position, Angle, o->Light, 11);
        }
        else if (o->AnimationFrame > 15.4f && o->AnimationFrame < 16.5f && rand_fps_check(1))
        {
            vec3_t Angle;
            for (int i = 0; i < 4; ++i)
            {
                Vector((float)(WorldRandom() % 60 + 60 + 90), 0.f, o->Angle[2], Angle);
                CreateJoint(BITMAP_JOINT_SPARK, o->Position, o->Position, Angle, 5, o);
            }
            CreateParticle(BITMAP_SPARK, o->Position, Angle, o->Light, 11);
        }
    }
        return true;

    case 37: {
        PrepareWorldObjectPose(*o);
        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(BoneTransform[1], p, Position);

        float fLumi;
        fLumi = (sinf(WorldTime * 0.039f) + 1.0f) * 0.2f + 0.6f;
        vec3_t vLightFire;
        Vector(fLumi * 0.7f, fLumi * 0.7f, fLumi * 0.7f, vLightFire);
        CreateSprite(BITMAP_FLARE, Position, 4.0f * o->Scale, vLightFire, o);
    }
        return true;

    case 50: {
        PrepareWorldObjectPose(*o);
        vec3_t vPos, vRelativePos, vLight1, vLight2, vAngle;
        Vector(0.f, 0.f, 0.f, vPos);
        Vector(0.f, 0.f, 5.f, vRelativePos);
        Vector(0.0f, -1.0f, 0.0f, vAngle);
        Vector(0.05f, 0.1f, 0.3f, vLight1);
        Vector(1.f, 1.f, 1.f, vLight2);

        for (int i = 2; i <= 7; i++)
        {
            b->TransformPosition(BoneTransform[i], vRelativePos, vPos);
            CreateParticleFpsChecked(BITMAP_FIRE_HIK3_MONO, vPos, vAngle, vLight1, 4,
                                     o->Scale * 0.6f);
            CreateParticleFpsChecked(BITMAP_FIRE_HIK3_MONO, vPos, vAngle, vLight2, 4,
                                     o->Scale * 0.3f);
        }
    }
        return true;

    case 51: {
        if (o->AnimationFrame > 5.4f && o->AnimationFrame < 6.5f)
        {
            vec3_t Angle;
            for (int i = 0; i < 4; ++i)
            {
                Vector((float)(WorldRandom() % 60 + 60 + 90), 0.f, o->Angle[2] + 180, Angle);
                CreateJointFpsChecked(BITMAP_JOINT_SPARK, o->Position, o->Position, Angle, 5, o);
            }
            CreateParticleFpsChecked(BITMAP_SPARK, o->Position, Angle, o->Light, 11);
        }
    }
        return true;

    case 64: {
        if ((o->AnimationFrame > 9.5f && o->AnimationFrame < 11.5f) ||
            (o->AnimationFrame > 23.5f && o->AnimationFrame < 25.5f))
        {
            float Matrix[3][4];
            vec3_t vAngle, vDirection, vPosition;
            Vector(0.f, 0.f, o->Angle[2] + 90, vAngle);
            AngleMatrix(vAngle, Matrix);
            Vector(0.f, 30.0f, 0.f, vDirection);
            VectorRotate(vDirection, Matrix, vPosition);
            VectorAdd(vPosition, o->Position, Position);

            Vector(0.04f, 0.03f, 0.02f, Light);
            for (int i = 0; i < 3; ++i)
            {
                CreateParticleFpsChecked(BITMAP_CLOUD, Position, o->Angle, Light, 22, o->Scale, o);
            }
        }
    }
        return true;

    case 79: {
        vec3_t vLightFire;
        Vector(1.0f, 0.2f, 0.0f, vLightFire);
        CreateSprite(BITMAP_LIGHT, o->Position, 2.0f * o->Scale, vLightFire, o);

        vec3_t vLight;
        Vector(1.0f, 1.0f, 1.0f, vLight);

        switch (WorldRandom() % 3)
        {
        case 0:
            CreateParticleFpsChecked(BITMAP_FIRE_HIK1, o->Position, o->Angle, vLight, 0, o->Scale);
            break;
        case 1:
            CreateParticleFpsChecked(BITMAP_FIRE_CURSEDLICH, o->Position, o->Angle, vLight, 4,
                                     o->Scale);
            break;
        case 2:
            CreateParticleFpsChecked(BITMAP_FIRE_HIK3, o->Position, o->Angle, vLight, 0, o->Scale);
            break;
        }
    }
        return true;

    case 80: {
        float fLumi;
        fLumi = (sinf(WorldTime * 0.04f) + 1.0f) * 0.3f + 0.4f;
        vec3_t vLightFire;
        Vector(fLumi * 0.1f, fLumi * 0.1f, fLumi * 0.5f, vLightFire);
        CreateSprite(BITMAP_LIGHT, o->Position, 8.0f * o->Scale, vLightFire, o);
    }
        return true;

    case 82: {
        Vector(1.f, 1.f, 1.f, Light);
        CreateParticleFpsChecked(BITMAP_WATERFALL_5, o->Position, o->Angle, Light, 9, o->Scale);
    }
        return true;

    case 83: {
        Vector(1.f, 1.f, 1.f, Light);
        CreateParticleFpsChecked(BITMAP_WATERFALL_3, o->Position, o->Angle, Light, 14, o->Scale);
    }
        return true;

    case 84: {
        Vector(1.f, 1.f, 1.f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 8.f))
        {
            CreateParticle(BITMAP_WATERFALL_2, o->Position, o->Angle, Light, 4, o->Scale);
        }
    }
        return true;

    case 85:
        sessionKeeper_.Visual()->EmitPeriodicFlames(*o);
        return true;

    case 86: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 6.f))
        {
            Vector(0.05f, 0.02f, 0.01f, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 21, o->Scale, o);
        }
    }
        return true;

    case 129: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 6.f))
        {
            Vector(0.01f, 0.02f, 0.05f, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 21, o->Scale, o);
        }
    }
        return true;

    case 130: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 6.f))
        {
            Vector(0.01f, 0.05f, 0.02f, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 21, o->Scale, o);
        }
    }
        return true;

    case 131: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 22, o->Scale);

            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 21, o->Scale * 2.0f, o);
        }
    }
        return true;

    case 132: {
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 60, o->Scale, o);

            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 60, o->Scale, o);

            Vector(1.f, 1.f, 1.f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 21, o->Scale * 2.0f, o);
        }
    }
        return true;
    }

    return false;
}

bool GMEmpireGuardian1::AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                             WorldCharacterVisualState &visual)
{
    vec3_t vLight, vPos;

    switch (o->Type)
    {
    case MODEL_RAYMOND: {
        if (g_isNotCharacterBuff(o) == true && g_isCharacterBuff(o, eBuff_Berserker) == true)
        {
            sessionKeeper_.Visual()->EmitBerserkerSmoke(*o, *b, 6);
        }
    }
        return true;
    case MODEL_LUCAS: {
        vec3_t vRelative;
        Vector(0.0f, 0.0f, 0.0f, vRelative) float fLumi1 =
            (sinf(WorldTime * 0.004f) + 0.9f) * 0.25f;

        Vector(0.05f + fLumi1, 0.75f + fLumi1, 0.35f + fLumi1, vLight);

        Vector(0.0f, 0.0f, -10.0f, vRelative);
        b->TransformByObjectBone(vPos, o, 41, vRelative);
        CreateSprite(BITMAP_LIGHT, vPos, 1.0f + fLumi1, vLight, o);

        Vector(0.0f, 0.0f, 10.0f, vRelative);
        b->TransformByObjectBone(vPos, o, 41, vRelative);
        CreateSprite(BITMAP_LIGHT, vPos, 1.0f + fLumi1, vLight, o);

        Vector(0.0f, 0.0f, -10.0f, vRelative);
        b->TransformByObjectBone(vPos, o, 42, vRelative);
        CreateSprite(BITMAP_LIGHT, vPos, 1.0f + fLumi1, vLight, o);

        Vector(0.0f, 0.0f, 10.0f, vRelative);
        b->TransformByObjectBone(vPos, o, 42, vRelative);
        CreateSprite(BITMAP_LIGHT, vPos, 1.0f + fLumi1, vLight, o);

        float fScale = 1.2f;
        for (int i = 50; i <= 55; ++i)
        {
            int iCurrentBoneIndex = i;

            Vector(0.0f, 6.0f, 0.0f, vRelative);
            b->TransformByObjectBone(vPos, o, iCurrentBoneIndex, vRelative);
            CreateSprite(BITMAP_LIGHT, vPos, fScale + fLumi1, vLight, o);

            fScale = fScale - 0.15f;
        }

        if (g_isNotCharacterBuff(o) == true && g_isCharacterBuff(o, eBuff_Berserker) == true)
        {
            sessionKeeper_.Visual()->EmitBerserkerSmoke(*o, *b, 6);
        }
    }
        return true;
    case MODEL_FRED: {
        if (g_isNotCharacterBuff(o) == true && g_isCharacterBuff(o, eBuff_Berserker) == true)
        {
            sessionKeeper_.Visual()->EmitBerserkerSmoke(*o, *b, 6);
        }
    }
        return true;
    case MODEL_DEVIL_LORD: {
        if (g_isNotCharacterBuff(o) == true && g_isCharacterBuff(o, eBuff_Berserker) == true)
        {
            sessionKeeper_.Visual()->EmitBerserkerSmoke(*o, *b, 6);
        }
    }
        return true;
    case MODEL_QUARTER_MASTER: {
        int i;
        Vector(0.5f, 0.5f, 0.8f, vLight);
        int iBoneNumbers[] = {10, 8, 12, 25};
        for (i = 1; i < 4; ++i)
        {
            b->TransformByObjectBone(vPos, o, iBoneNumbers[i]);
            CreateSprite(BITMAP_LIGHT, vPos, 3.1f, vLight, o);
        }

        if (visual.animationFrame > 2 && visual.animationFrame < 3)
        {

            b->TransformByObjectBone(vPos, o, 11);

            Vector(1.0f, 1.0f, 1.0f, vLight);
            CreateParticleFpsChecked(BITMAP_SMOKE, vPos, o->Angle, vLight, 4, 1.0f);
        }
    }
        return true;
    case MODEL_DEFENDER: {
    }
        return true;
    case MODEL_FORSAKER: {
        if (visual.action != MONSTER01_ATTACK1 && visual.action != MONSTER01_ATTACK2)
        {
            vec3_t vRelative;
            Vector(10.0f, 0.0f, 0.0f, vRelative);
            b->TransformPosition(o->BoneTransform[55], vRelative, vPos, true);
            float fLumi = (sinf(WorldTime * 0.001f) + 1.0f) * 0.3f + 0.4f;
            Vector(fLumi * 0.5f, fLumi * 0.5f, fLumi * 0.5f, vLight);
            CreateSprite(BITMAP_FLARE_BLUE, vPos, 1.5f, vLight, o);
            Vector(0.5f, 0.5f, 0.5f, vLight);
            CreateSprite(BITMAP_FLARE_BLUE, vPos, 1.2f, vLight, o, -WorldTime * 0.1f);
        }
    }
        return true;
    }

    return false;
}

bool GMEmpireGuardian1::CreateRain(PARTICLE *o)
{
    if (!gMapManager.IsEmpireGuardian1() && !gMapManager.IsEmpireGuardian2() &&
        !gMapManager.IsEmpireGuardian3() && !gMapManager.IsEmpireGuardian4())
        return false;
    if (m_iWeather != WEATHER_RAIN && m_iWeather != WEATHER_STORM)
        return false;
    o->Type = BITMAP_RAIN;
    Vector(Hero->Object.Position[0] + float(WorldRandom() % 800 - 300),
           Hero->Object.Position[1] + float(WorldRandom() % 800 - 400),
           Hero->Object.Position[2] + float(WorldRandom() % 200 + 400), o->Position);
    Vector(-30.f, 0.f, 0.f, o->Angle);
    vec3_t velocity{0.f, 0.f, -float(WorldRandom() % 24 + 30)};
    float matrix[3][4];
    AngleMatrix(o->Angle, matrix);
    VectorRotate(velocity, matrix, o->Velocity);
    // Authored birth offset, independent of the duration of the receiving frame.
    VectorAdd(o->Position, o->Velocity, o->Position);
    if (WorldRandom() % 2 == 0)
    {
        o->Live = false;
        o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) + 10.f;
        const int type = WorldRandom() % 4 == 0 ? BITMAP_RAIN_CIRCLE : BITMAP_RAIN_CIRCLE + 1;
        CreateParticle(type, o->Position, o->Angle, o->Light);
    }
    return true;
}

bool GMEmpireGuardian1::AttackEffectMonster(CHARACTER *c, OBJECT *o, BMD *b)
{
    if (gMapManager.IsEmpireGuardian1() == false)
        return false;

    //  switch(c->MonsterIndex)
    //  {
    //  }

    return false;
}

bool GMEmpireGuardian1::PlayMonsterSound(OBJECT *o)
{
    if (gMapManager.IsEmpireGuardian1() == false && gMapManager.IsEmpireGuardian2() == false &&
        gMapManager.IsEmpireGuardian3() == false && gMapManager.IsEmpireGuardian4() == false)
    {
        return false;
    }

    float fDis_x, fDis_y;
    fDis_x = o->Position[0] - Hero->Object.Position[0];
    fDis_y = o->Position[1] - Hero->Object.Position[1];
    float fDistance = sqrtf(fDis_x * fDis_x + fDis_y * fDis_y);

    if (fDistance > 500.0f)
        return true;

    switch (o->Type)
    {
    case MODEL_RAYMOND: {
        switch (o->CurrentAction)
        {
        case MONSTER01_WALK: {
            if (7.0f <= o->AnimationFrame && o->AnimationFrame < 8.0f)
            {
                PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_MOVE01);
            }
            if (1.0f <= o->AnimationFrame && o->AnimationFrame < 2.0f)
            {
                PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_MOVE02);
            }
        }
        break;
        case MONSTER01_ATTACK1: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_ATTACK01);
        }
        break;
        case MONSTER01_ATTACK2: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_RAYMOND_MONSTER_ATTACK02); // Raymond_attack2.wav
            // 					PlayBuffer(SOUND_SKILL_BLOWOFDESTRUCTION);
        }
        break;
        case MONSTER01_ATTACK3: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_RAYMOND_MONSTER_ATTACK02); // Raymond_attack2.wav
            // 					PlayBuffer(SOUND_METEORITE01);
            // 					PlayBuffer(SOUND_EXPLOTION01);
        }
        break;
        case MONSTER01_APEAR: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_RAYMOND_MONSTER_RAGE); // Raymond_rage.wav
        }
        break;
        case MONSTER01_DIE: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_DEATH);
        }
        break;
        }
    }
        return true;
    case MODEL_LUCAS: {
        switch (o->CurrentAction)
        {
        case MONSTER01_WALK: {
            if (rand_fps_check(2))
            {
                PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_MOVE01);
            }
            else
            {
                PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_MOVE02);
            }
        }
        break;
        case MONSTER01_ATTACK1: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_ATTACK01); // 공격1 사운드
        }
        break;
        case MONSTER01_ATTACK2: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_ATTACK03); // 공격2 사운드
        }
        break;
        case MONSTER01_ATTACK3: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_ERCANNE_MONSTER_ATTACK03); // 공격3 사운드
        }
        break;
        case MONSTER01_APEAR: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_RAYMOND_MONSTER_RAGE); // 광폭화 사운드
        }
        break;
        case MONSTER01_DIE: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_DEATH); // 죽음 사운드
        }
        break;
        }
    }
        return true;
    case MODEL_FRED: {
        switch (o->CurrentAction)
        {
        case MONSTER01_WALK: {
            if (7.0f <= o->AnimationFrame && o->AnimationFrame < 8.0f)
            {
                PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_MOVE01);
            }
            if (1.0f <= o->AnimationFrame && o->AnimationFrame < 2.0f)
            {
                PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_MOVE02);
            }
        }
        break;
        case MONSTER01_ATTACK1: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_COMBATMASTER_ATTACK01);
        }
        break;
        case MONSTER01_ATTACK2: {
            PlayBuffer(
                SOUND_EMPIREGUARDIAN_1CORP_DEASULER_MONSTER_ATTACK02); // 1Deasuler_attack2.wav
        }
        break;
        case MONSTER01_ATTACK3: {
            PlayBuffer(
                SOUND_EMPIREGUARDIAN_1CORP_DEASULER_MONSTER_ATTACK03); // 1Deasuler_attack3.wav
        }
        break;
        case MONSTER01_APEAR: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_RAGE);
        }
        break;
        case MONSTER01_DIE: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_DEATH);
        }
        break;
        }
    }
        return true;

    case MODEL_DEVIL_LORD: {
        switch (o->CurrentAction)
        {
        case MONSTER01_WALK: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_3CORP_CATO_MOVE);
        }
        break;
        case MONSTER01_ATTACK1: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_ATTACK01);
        }
        break;
        case MONSTER01_ATTACK2:
        case MONSTER01_ATTACK3: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_4CORP_GALLIA_ATTACK02); // 4Gallia_attack2.wav
        }
        break;
        case MONSTER01_APEAR: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_RAGE);
        }
        break;
        case MONSTER01_DIE: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_2CORP_VERMONT_MONSTER_DEATH);
        }
        break;
        }
    }
        return true;

    case MODEL_QUARTER_MASTER: {
        switch (o->CurrentAction)
        {
        case MONSTER01_WALK: {
            if (7.0f <= o->AnimationFrame && o->AnimationFrame < 8.0f)
            {
                PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_MOVE01);
            }
            if (1.0f <= o->AnimationFrame && o->AnimationFrame < 2.0f)
            {
                PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_MOVE02);
            }
        }
        break;
        case MONSTER01_ATTACK1: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_ATTACK01);
        }
        break;
        case MONSTER01_ATTACK2:
        case MONSTER01_ATTACK3: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_QUATERMASTER_ATTACK02); // QuaterMaster_attack2.wav
        }
        break;
        case MONSTER01_DIE: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_DEATH);
        }
        break;
        }
    }
        return true;

    case MODEL_COMBAT_INSTRUCTOR: {
        switch (o->CurrentAction)
        {
        case MONSTER01_WALK: {
            if (7.0f <= o->AnimationFrame && o->AnimationFrame < 8.0f)
            {
                PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_MOVE01);
            }
            if (1.0f <= o->AnimationFrame && o->AnimationFrame < 2.0f)
            {
                PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_MOVE02);
            }
        }
        break;
        case MONSTER01_ATTACK1: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_COMBATMASTER_ATTACK01); // CombatMaster_attack1.wav
        }
        break;

        case MONSTER01_ATTACK2: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_COMBATMASTER_ATTACK02); // CombatMaster_attack2.wav
        }
        break;

        case MONSTER01_ATTACK3: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_COMBATMASTER_ATTACK03); // CombatMaster_attack3.wav
        }
        break;
        case MONSTER01_DIE: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_DEATH);
        }
        break;
        }
    }
        return true;
    case MODEL_DEFENDER: {
        switch (o->CurrentAction)
        {
        case MONSTER01_WALK: {
            if (7.0f <= o->AnimationFrame && o->AnimationFrame < 8.0f)
            {
                PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_MOVE01);
            }
            if (1.0f <= o->AnimationFrame && o->AnimationFrame < 2.0f)
            {
                PlayBuffer(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_MOVE02);
            }
        }
        break;
        case MONSTER01_ATTACK1: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_COMBATMASTER_ATTACK01); // CombatMaster_attack1.wav
        }
        break;

        case MONSTER01_ATTACK2: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_COMBATMASTER_ATTACK02); // CombatMaster_attack2.wav
        }
        break;
        case MONSTER01_DIE: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_DEFENDER_ATTACK02);
        }
        break;
        }
    }
        return true;

    case MODEL_FORSAKER: {
        switch (o->CurrentAction)
        {
        case MONSTER01_STOP1:
        case MONSTER01_STOP2: {
            PlayBuffer(SOUND_EMPIREGUARDIAN_PRIEST_STOP);
        }
        break;
        case MONSTER01_ATTACK1: {
            PlayBuffer(SOUND_THUNDERS01);
        }
        break;

        case MONSTER01_ATTACK2: {
            PlayBuffer(SOUND_RAKLION_SERUFAN_CURE);
        }
        break;
        case MONSTER01_DIE: {
            PlayBuffer(SOUND_DARKLORD_DEAD);
        }
        break;
        }
    }
        return true;
    }

    return false;
}

void GMEmpireGuardian1::PlayObjectSound(OBJECT *o)
{
    if (gMapManager.IsEmpireGuardian1() == false && gMapManager.IsEmpireGuardian2() == false &&
        gMapManager.IsEmpireGuardian3() == false && gMapManager.IsEmpireGuardian4() == false)
    {
        return;
    }

    if (gMapManager.IsEmpireGuardian4())
    {
        PlayBuffer(SOUND_EMPIREGUARDIAN_INDOOR_SOUND, NULL, false);
        return;
    }

    switch (m_iWeather)
    {
    case WEATHER_SUN:
        break;
    case WEATHER_RAIN:
        PlayBuffer(SOUND_EMPIREGUARDIAN_WEATHER_RAIN, NULL, false);
        break;
    case WEATHER_FOG:
        PlayBuffer(SOUND_EMPIREGUARDIAN_WEATHER_FOG, NULL, false);
        break;
    case WEATHER_STORM:
        PlayBuffer(SOUND_EMPIREGUARDIAN_WEATHER_STORM, NULL, false);
        break;
    }
}

void GMEmpireGuardian1::PlayBGM()
{
    if (gMapManager.IsEmpireGuardian1())
    {
        PlayMp3(MUSIC_EMPIREGUARDIAN1);
    }
    else
    {
        StopMp3(MUSIC_EMPIREGUARDIAN1);
    }
}

void GMEmpireGuardian1::AdvanceGateProjectile(OBJECT *o, BMD *b)
{
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    const std::array<std::pair<int, float>, 2> markers{
        {{o->CurrentAction, 0.f}, {o->CurrentAction, 7.f}}};
    o->MotionTrace.VisitAnimationEvents(WorldTime, markers, [&](std::size_t event, float fraction) {
        if (event == 1 && o->SubType == 101)
            o->SubType = 100;
        if (event != 0 || o->SubType != 100)
            return;
        auto birth =
            sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR * (1.f - fraction));
        AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false, b->PoseAssetIdentity());
        vec3_t offset{}, position, light{1.f, 1.f, 1.f}, angle{};
        pose.SampleBonePosition(*b, *o, 9, offset, WorldTime, fraction, position);
        CreateEffect(MODEL_PROJECTILE, position, angle, light, 0, o);
        o->SubType = 101;
    });
}

void GMEmpireGuardian1::AdvanceWeather()
{
    stormFlash_ = m_iWeather == WEATHER_STORM && rand_fps_check(20);
}

void GMEmpireGuardian1::AdvanceGatePlacement(OBJECT *o)
{
    if (!gMapManager.IsEmpireGuardian1() && !gMapManager.IsEmpireGuardian2() &&
        !gMapManager.IsEmpireGuardian3())
        return;
    switch (o->Type)
    {
    case MODEL_EVIL_GATE: {
        int tileX = int(o->Position[0] / 100);
        int tileY = int(o->Position[1] / 100);

        switch (gMapManager.ContextMap())
        {
        case WD_69EMPIREGUARDIAN1: {
            if (tileX == 233 && tileY == 55)
            {
                o->Position[0] = 23350;
                o->Position[1] = 5520;
            }
            else if ((165 <= tileX && tileX <= 167) && (25 <= tileY && tileY <= 27))
            {
                o->Position[0] = 16710;
                o->Position[1] = 2620;
            }
        }
        break;
        case WD_70EMPIREGUARDIAN2: {
            if ((49 <= tileX && tileX <= 51) && (64 <= tileY && tileY <= 66))
            {
                o->Position[0] = 5075;
                o->Position[1] = 6490;
            }
            else if ((40 <= tileX && tileX <= 42) && (116 <= tileY && tileY <= 118))
            {
                o->Position[0] = 4200;
                o->Position[1] = 11680;
            }
        }
        break;
        case WD_71EMPIREGUARDIAN3: {
            if ((118 <= tileX && tileX <= 120) && (191 <= tileY && tileY <= 193))
            {
                o->Scale = 0.9f;
                o->Position[0] = 11985;
                o->Position[1] = 19250;
            }
            else if ((221 <= tileX && tileX <= 223) && (159 <= tileY && tileY <= 161))
            {
                o->Scale = 1.08f;
                o->Position[0] = 22300;
                o->Position[1] = 16000;
            }
        }
        break;
        }

        break;
    }
    case MODEL_LION_GATE: {
        int tileX = int(o->Position[0] / 100);
        int tileY = int(o->Position[1] / 100);
        switch (gMapManager.ContextMap())
        {
        case WD_69EMPIREGUARDIAN1: {
            if (tileX == 234 && tileY == 28)
            {
                o->Position[0] = 23450;
                o->Position[1] = 2820;
            }
            else if (tileX == 216 && tileY == 80)
            {
                o->Position[0] = 21650;
                o->Position[1] = 8000;
            }
            else if (tileX == 194 && tileY == 25)
            {
                o->Position[0] = 19450;
                o->Position[1] = 2530;
            }
            else if ((153 <= tileX && tileX <= 155) && (52 <= tileY && tileY <= 54))
            {
                o->Scale = 1.15f;
                o->Position[0] = 15510;
                o->Position[1] = 5360;
            }
            else if (tileX == 180 && tileY == 79)
            {
                o->Position[0] = 18070;
                o->Position[1] = 7950;
            }
        }
        break;
        case WD_70EMPIREGUARDIAN2: {
            if ((74 <= tileX && tileX <= 76) && (66 <= tileY && tileY <= 68))
            {
                o->Scale = 1.17f;
                o->Position[0] = 7620;
                o->Position[1] = 6740;
            }
            else if ((18 <= tileX && tileX <= 20) && (64 <= tileY && tileY <= 66))
            {
                o->Position[0] = 1950;
                o->Position[1] = 6500;
            }
            else if ((36 <= tileX && tileX <= 38) && (92 <= tileY && tileY <= 94))
            {
                o->Scale = 1.1f;
                o->Position[0] = 3770;
                o->Position[1] = 9250;
            }
            else if ((54 <= tileX && tileX <= 56) && (153 <= tileY && tileY <= 155))
            {
                o->Scale = 1.15f;
                o->Position[0] = 5515;
                o->Position[1] = 15350;
            }
            else if ((106 <= tileX && tileX <= 108) && (111 <= tileY && tileY <= 113))
            {
                o->Scale = 1.05f;
                o->Position[0] = 10830;
                o->Position[1] = 11180;
            }
        }
        break;
        case WD_71EMPIREGUARDIAN3: {
            if ((145 <= tileX && tileX <= 147) && (190 <= tileY && tileY <= 192))
            {
                o->Scale = 1.28f;
                o->Position[0] = 14700;
                o->Position[1] = 19140;
            }
            else if ((88 <= tileX && tileX <= 90) && (194 <= tileY && tileY <= 196))
            {
                o->Scale = 1.10f;
                o->Position[0] = 9010;
                o->Position[1] = 19580;
            }
            else if ((221 <= tileX && tileX <= 223) && (133 <= tileY && tileY <= 135))
            {
                o->Scale = 1.1f;
                o->Position[0] = 22300;
                o->Position[1] = 13360;
            }
            else if ((222 <= tileX && tileX <= 224) && (192 <= tileY && tileY <= 194))
            {
                o->Scale = 1.1f;
                o->Position[0] = 22305;
                o->Position[1] = 19280;
            }
            else if ((166 <= tileX && tileX <= 168) && (216 <= tileY && tileY <= 218))
            {
                o->Scale = 1.23f;
                o->Position[0] = 16720;
                o->Position[1] = 21750;
            }
        }
        break;
        }

        break;
    }
    }
}

void GMEmpireGuardian1::AdvanceEnvironment()
{
    AdvanceWeather();
}

void GMEmpireGuardian1::UpdateMusic()
{
    PlayBGM();
}

bool GMEmpireGuardian1::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_EMPIREGUARDIAN1) == 0;
}

bool GMEmpireGuardian1::CreateWeather(PARTICLE *particle, int)
{
    return CreateRain(particle);
}

using namespace SEASON4A;

void CGM_Raklion::EmitSelupanEvent(OBJECT &object, std::size_t event, float fraction)
{
    vec3_t origin, angle, light, position;
    object.MotionTrace.Sample(WorldTime, fraction, object.Position, origin);
    VectorCopy(object.Angle, angle);
    angle[2] = object.MotionTrace.SampleYaw(WorldTime, fraction, angle[2]);
    if (event == 0)
    {
        Vector(0.2f, 0.4f, 1.f, light);
        VectorCopy(origin, position);
        position[1] += 30.f;
        CreateEffect(MODEL_RAKLION_BOSS_MAGIC, position, angle, light, 0, &object, -1, 0, 0, 0,
                     1.5f);
        vec3_t target;
        Hero->Object.MotionTrace.Sample(WorldTime, fraction, Hero->Object.Position, target);
        for (int child = 0; child < 20; ++child)
        {
            position[0] = target[0] + (WorldRandom() % 20 - 10) * 80.f + 500.f;
            position[1] = target[1] + (WorldRandom() % 20 - 10) * 80.f + 200.f;
            position[2] = target[2] + 300.f + (WorldRandom() % 10) * 100.f;
            const float scale = 1.f + (WorldRandom() % 10) / 5.f;
            const int type =
                WorldRandom() % 2 == 0 ? MODEL_EFFECT_BROKEN_ICE3 : MODEL_EFFECT_BROKEN_ICE1;
            CreateEffect(type, position, angle, light, 1, nullptr, -1, 0, 0, 0, scale);
        }
        return;
    }
    if (event == 1)
    {
        Vector(0.f, 0.9f, 0.1f, light);
        origin[2] += 100.f;
        for (int child = 0; child < 5; ++child)
        {
            vec3_t rotation{0.f, 0.f, angle[2] + 150.f + child * 20.f};
            vec3_t direction{0.f, 20.f, 0.f}, velocity;
            float matrix[3][4];
            AngleMatrix(rotation, matrix);
            VectorRotate(direction, matrix, velocity);
            CreateEffect(MODEL_MOONHARVEST_MOON, origin, velocity, light, 1, nullptr, -1, 0, 0, 0,
                         0.4f);
        }
        return;
    }
    Vector(0.3f, 0.5f, 1.f, light);
    CreateEffect(BITMAP_DAMAGE_01_MONO, origin, angle, light, 0);
    for (int child = 0; child < 3; ++child)
    {
        vec3_t offset{0.f, 200.f, 0.f}, rotation{0.f, 0.f, child * 120.f};
        float matrix[3][4];
        AngleMatrix(rotation, matrix);
        VectorRotate(offset, matrix, position);
        VectorAdd(position, origin, position);
        CreateEffect(BITMAP_FIRE_HIK2_MONO, position, angle, light, 0, &object);
    }
    Vector(0.f, 0.f, 1.f, light);
    CreateEffect(BITMAP_FIRE_HIK2_MONO, origin, angle, light, 1, &object);
    Vector(0.1f, 0.2f, 1.f, light);
    CreateEffect(MODEL_RAKLION_BOSS_CRACKEFFECT, origin, angle, light, 0, &object, -1, 0, 0, 0,
                 2.f);
    if (event == 3)
        PlayBuffer(SOUND_KANTURU_3RD_MAYAHAND_ATTACK2);
}

void CGM_Raklion::EmitMonsterEvents(OBJECT &object, BMD &model, WorldCharacterVisualState &visual)
{
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    constexpr std::array<std::pair<int, float>, 1> walker{{{MONSTER01_ATTACK2, 4.4f}}};
    constexpr std::array<std::pair<int, float>, 1> mammoth{{{MONSTER01_ATTACK2, 8.f}}};
    constexpr std::array<std::pair<int, float>, 2> coolutin{
        {{MONSTER01_ATTACK1, 1.7f}, {MONSTER01_ATTACK2, 1.7f}}};
    constexpr std::array<std::pair<int, float>, 2> giant{
        {{MONSTER01_ATTACK2, 7.4f}, {MONSTER01_DIE, 0.f}}};
    constexpr std::array<std::pair<int, float>, 1> knight{{{MONSTER01_DIE, 0.f}}};
    constexpr std::array<std::pair<int, float>, 4> selupan{{{MONSTER01_ATTACK1, 3.7f},
                                                            {MONSTER01_ATTACK2, 6.8f},
                                                            {MONSTER01_ATTACK3, 6.6f},
                                                            {MONSTER01_APEAR, 5.6f}}};
    std::span<const std::pair<int, float>> markers;
    switch (object.Type)
    {
    case MODEL_ICE_WALKER:
        markers = walker;
        break;
    case MODEL_GIANT_MAMMOTH:
    case MODEL_DARK_MAMMOTH:
        markers = mammoth;
        break;
    case MODEL_COOLUTIN:
    case MODEL_DARK_COOLUTIN:
        markers = coolutin;
        break;
    case MODEL_ICE_GIANT:
    case MODEL_DARK_GIANT:
        markers = giant;
        break;
    case MODEL_IRON_KNIGHT:
    case MODEL_DARK_IRON_KNIGHT:
        markers = knight;
        break;
    case MODEL_SELUPAN:
        markers = selupan;
        break;
    default:
        return;
    }
    object.MotionTrace.VisitAnimationEvents(
        WorldTime, markers, [&](std::size_t event, float fraction) {
            auto birth =
                sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR * (1.f - fraction));
            if (object.Type == MODEL_SELUPAN)
            {
                EmitSelupanEvent(object, event, fraction);
                return;
            }
            vec3_t origin, position, angle, light;
            object.MotionTrace.Sample(WorldTime, fraction, object.Position, origin);
            VectorCopy(object.Angle, angle);
            angle[2] = object.MotionTrace.SampleYaw(WorldTime, fraction, angle[2]);
            const bool isGiant = object.Type == MODEL_ICE_GIANT || object.Type == MODEL_DARK_GIANT;
            if (isGiant && event == 0)
            {
                CreateInferno(origin, 5);
                return;
            }
            if (isGiant)
            {
                if (visual.emissionLifeTime != 100.f)
                    return;
                visual.emissionLifeTime = 90.f;
            }
            AnimationPoseSample pose(&object, model.BoneHead, model.BodyHeight, false,
                                     model.PoseAssetIdentity());
            std::array<vec34_t, MAX_BONES> bones;
            pose.EvaluateAtTime(model, object, WorldTime, fraction, bones.data());
            const auto bonePosition = [&](int bone) {
                for (int axis = 0; axis < 3; ++axis)
                    position[axis] = bones[bone][axis][3] * object.Scale + origin[axis];
            };
            if (object.Type == MODEL_ICE_WALKER)
            {
                bonePosition(8);
                CreateEffect(MODEL_STREAMOFICEBREATH, position, angle, visual.movement.light, 0,
                             nullptr, -1, 0, 0, 0, 0.2f, -1);
            }
            else if (object.Type == MODEL_GIANT_MAMMOTH || object.Type == MODEL_DARK_MAMMOTH)
            {
                bonePosition(28);
                Vector(0.7f, 0.7f, 1.f, light);
                for (int child = 0; child < 2; ++child)
                    CreateParticle(BITMAP_CLUD64, position, angle, light, 7, 2.f);
                Vector(0.9f, 0.9f, 0.9f, light);
                for (int subtype : {8, 13, 13})
                    CreateEffect(BITMAP_SHOCK_WAVE, position, angle, light, subtype);
                Vector(0.4f, 0.8f, 0.9f, light);
                for (int child = 0; child < 60; ++child)
                    CreateEffect(MODEL_EFFECT_BROKEN_ICE0 + WorldRandom() % 3, position, angle,
                                 light, 0);
            }
            else if (object.Type == MODEL_COOLUTIN || object.Type == MODEL_DARK_COOLUTIN)
            {
                bonePosition(19);
                if (event == 0)
                {
                    Vector(0.4f, 0.6f, 1.f, light);
                }
                else
                {
                    Vector(0.f, 0.9f, 0.1f, light);
                }
                CreateEffect(MODEL_1_STREAMBREATHFIRE, position, angle, light, 0, nullptr, -1, 0, 0,
                             0, 1.f, -1);
            }
            else
            {
                constexpr std::array<int, 8> giantBones{4, 7, 10, 22, 39, 44, 12, 24};
                constexpr std::array<int, 4> knightBones{20, 37, 45, 51};
                const auto emitBones =
                    isGiant ? std::span<const int>(giantBones) : std::span<const int>(knightBones);
                Vector(0.3f, isGiant ? 0.6f : 0.5f, 1.f, light);
                for (int bone : emitBones)
                {
                    bonePosition(bone);
                    for (int child = 0; child < (isGiant ? 12 : 6); ++child)
                        CreateEffect(MODEL_EFFECT_BROKEN_ICE0 + WorldRandom() % 3, position, angle,
                                     light, 0);
                }
                if (isGiant)
                {
                    constexpr std::array<std::pair<int, int>, 6> parts{
                        {{5, MODEL_ICE_GIANT_PART1},
                         {3, MODEL_ICE_GIANT_PART2},
                         {11, MODEL_ICE_GIANT_PART3},
                         {23, MODEL_ICE_GIANT_PART4},
                         {39, MODEL_ICE_GIANT_PART5},
                         {45, MODEL_ICE_GIANT_PART6}}};
                    Vector(1.f, 1.f, 1.f, light);
                    for (const auto &[bone, type] : parts)
                    {
                        bonePosition(bone);
                        CreateEffect(type, position, angle, light, 0, &object, 0, 0);
                    }
                }
            }
        });
}

bool CGM_Raklion::MoveMonsterVisual(CHARACTER *, OBJECT *o, BMD *b,
                                    WorldCharacterVisualState &visual)
{
    ObjectDrawInput presentation(o);
    visual.movement.Apply(presentation);
    if (IsIceCity() == false)
        return false;

    EmitMonsterEvents(*o, *b, visual);

    switch (o->Type)
    {
    case MODEL_ICE_WALKER: {
        switch (visual.action)
        {
        case MONSTER01_ATTACK2: {

        }
        break;
        case MONSTER01_DIE: {
            vec3_t vPos, vRelative;
            Vector(0.f, 0.f, 0.f, vRelative);

            float Scale = 3.5f;
            Vector(1.f, 1.f, 1.f, visual.movement.light);
            b->TransformByObjectBone(vPos, presentation, 6);
            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, visual.movement.light, 3, Scale);
            b->TransformByObjectBone(vPos, presentation, 79);
            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, visual.movement.light, 53, Scale);
        }
        break;
        default: {
        }
        break;
        } // switch( visual.action )
    }
    break;
    case MODEL_GIANT_MAMMOTH: {
        vec3_t Light;
        vec3_t EndPos, EndRelative;
        Vector(1.f, 1.f, 1.f, Light);

        if (visual.action == MONSTER01_ATTACK2)
        {

        }
        else
        {
            vec3_t vPos, vRelative;
            Vector(0.f, 0.f, 0.f, vRelative);
            if (visual.action == MONSTER01_DIE)
            {
                float Scale = 3.5f;
                Vector(1.f, 1.f, 1.f, visual.movement.light);
                GetBonePosition(presentation, CharacterSocket::GIANT_MAMUD_BIP_TAIL, vRelative,
                                vPos);
                CreateParticle(BITMAP_SMOKE, vPos, o->Angle, visual.movement.light, 53, Scale);
                GetBonePosition(presentation, CharacterSocket::GIANT_MAMUD_BIP_TAIL_1, vRelative,
                                vPos);
                CreateParticle(BITMAP_SMOKE, vPos, o->Angle, visual.movement.light, 3, Scale);
                GetBonePosition(presentation, CharacterSocket::GIANT_MAMUD_BIP_TAIL_2, vRelative,
                                vPos);
                CreateParticle(BITMAP_SMOKE, vPos, o->Angle, visual.movement.light, 53, Scale);
                GetBonePosition(presentation, CharacterSocket::GIANT_MAMUD_BIP_SPAIN_1, vRelative,
                                vPos);
                CreateParticle(BITMAP_SMOKE, vPos, o->Angle, visual.movement.light, 3, Scale);
                GetBonePosition(presentation, CharacterSocket::GIANT_MAMUD_BIP_SPAIN_2, vRelative,
                                vPos);
                CreateParticle(BITMAP_SMOKE, vPos, o->Angle, visual.movement.light, 53, Scale);
                GetBonePosition(presentation, CharacterSocket::GIANT_MAMUD_BIP_SPAIN_3, vRelative,
                                vPos);
                CreateParticle(BITMAP_SMOKE, vPos, o->Angle, visual.movement.light, 3, Scale);
            }
        }
    }
    break;
    case MODEL_ICE_GIANT: {
    }
    break;
    case MODEL_COOLUTIN: {
        // if( visual.animationFrame >= 1.7f && visual.animationFrame <= 2.0f )

        if (visual.animationFrame <= 8.0f)
        {
            switch (visual.action)
            {
            case MONSTER01_DIE: {
                vec3_t vPos, vLight;

                if (WorldRandom() % 3 != 0)
                {
                    for (int i = 0; i < b->NumBones; ++i)
                    {
                        b->TransformByObjectBone(vPos, presentation, i);
                        if (rand_fps_check(5))
                        {
                            Vector(0.0f, 1.f, 0.2f, vLight);
                            CreateParticle(BITMAP_WATERFALL_5, vPos, o->Angle, vLight, 8, 2.0f);
                        }
                        if (rand_fps_check(5))
                        {
                            Vector(0.1f, 1.f, 0.1f, vLight);
                            CreateParticle(BITMAP_WATERFALL_3, vPos, o->Angle, vLight, 8, 2.5f);
                        }
                    }
                }

                if (rand_fps_check(3))
                {
                    VectorCopy(o->Position, vPos);
                    vPos[0] += (float)(WorldRandom() % 200 - 100);
                    vPos[1] += (float)(WorldRandom() % 200 - 100);
                    Vector(1.0f, 1.f, 1.f, vLight);
                    CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, 1, 0.5f);
                    CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, 24, 1.25f);

                    VectorCopy(o->Position, vPos);
                    vPos[0] += (float)(WorldRandom() % 250 - 125);
                    vPos[1] += (float)(WorldRandom() % 250 - 125);
                    Vector(0.1f, 1.0f, 0.1f, vLight);
                    CreateEffect(BITMAP_CLOUD, vPos, o->Angle, vLight, 0, NULL, -1, 0, 0, 0, 1.0f);

                    Vector(1.0f, 1.0f, 1.0f, vLight);
                    vPos[2] += 50.f;
                    CreateParticle(BITMAP_SPARK + 1, vPos, o->Angle, vLight, 5, 0.75f);
                }
            } // case MONSTER01_DIE:
            break;
            } // switch(visual.action)
        }
    }
    break;
    case MODEL_IRON_KNIGHT: {
    }
    break;
    case MODEL_SELUPAN: {
        if (visual.action == MONSTER01_ATTACK1)
        {
            vec3_t vLight, vPos;


        }
        else if (visual.action == MONSTER01_ATTACK2)
        {
            vec3_t vLight, vPos, vAngle;

            Vector(0.0f, 0.9f, 0.1f, vLight);
            b->TransformByObjectBone(vPos, presentation, 0);
            CreateParticle(BITMAP_WATERFALL_3, vPos, o->Angle, vLight, 11, 2.f);

            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, 52, 2.f);
        }
        else if (visual.action == MONSTER01_ATTACK3)
        {
            vec3_t vLight, vPos, vAngle, vPos2;
            float Matrix[3][4];

            Vector(0.3f, 0.5f, 1.f, vLight);


        }
        else if (visual.action == MONSTER01_APEAR)
        {
            vec3_t vLight, vPos, vAngle, vPos2;
            float Matrix[3][4];
            float fScale = 1.f;

            if (visual.animationFrame <= 3.f)
            {
                Vector(0.3f, 0.5f, 1.f, vLight);

                for (int i = 0; i < 1; ++i)
                {
                    VectorCopy(o->Position, vPos);
                    vPos[0] += (WorldRandom() % 2000 - 1000.f);
                    vPos[1] += (WorldRandom() % 2000 - 1000.f);
                    vPos[2] = 500.f + WorldRandom() % 100;

                    fScale = 2.0f + (WorldRandom() % 20) / 5.0f;
                    int iIndex = MODEL_EFFECT_BROKEN_ICE1;

                    CreateEffect(iIndex, vPos, o->Angle, vLight, 2, NULL, -1, 0, 0, 0, fScale);

                    VectorCopy(o->Position, vPos);
                    vPos[0] += (WorldRandom() % 2000 - 1000.f);
                    vPos[1] += (WorldRandom() % 2000 - 1000.f);
                    vPos[2] = 500.f + WorldRandom() % 100;

                    fScale = 0.5f + (WorldRandom() % 10) / 5.0f;
                    iIndex = MODEL_EFFECT_BROKEN_ICE3;

                    CreateEffect(iIndex, vPos, o->Angle, vLight, 2, NULL, -1, 0, 0, 0, fScale);

                    VectorCopy(o->Position, vPos);
                    vPos[0] += (WorldRandom() % 2000 - 1000.f);
                    vPos[1] += (WorldRandom() % 2000 - 1000.f);
                    vPos[2] = 500.f + WorldRandom() % 100;

                    Vector(1.f, 0.8f, 0.8f, vLight);
                    fScale = 0.05f + (WorldRandom() % 10) / 20.0f;
                    CreateEffect(MODEL_FALL_STONE_EFFECT, vPos, o->Angle, vLight, 2, NULL, -1, 0, 0,
                                 0, fScale);

                    Vector(0.7f, 0.7f, 0.8f, vLight);
                    vPos[0] += (float)(WorldRandom() % 80 - 40);
                    vPos[1] += (float)(WorldRandom() % 80 - 40);
                    CreateParticle(BITMAP_WATERFALL_3 + (WorldRandom() % 2), vPos, o->Angle, vLight,
                                   2);
                }
            }

            if (visual.animationFrame >= 5.f && visual.animationFrame <= 7.f)
            {
                EarthQuake = (float)(WorldRandom() % 4 - 2) * 1.0f;


            }
            else
            {
                EarthQuake = 0.f;
            }
        }
        else if (visual.action == MONSTER01_DIE && visual.animationFrame <= 8.f)
        {
            vec3_t vPos, vLight;

            if (WorldRandom() % 3 != 0)
            {
                for (int i = 0; i < b->NumBones; ++i)
                {
                    b->TransformByObjectBone(vPos, presentation, i);
                    if (rand_fps_check(5))
                    {
                        Vector(0.0f, 1.f, 0.2f, vLight);
                        CreateParticle(BITMAP_WATERFALL_5, vPos, o->Angle, vLight, 8, 2.0f);
                    }
                    if (rand_fps_check(5))
                    {
                        Vector(0.1f, 1.f, 0.1f, vLight);
                        CreateParticle(BITMAP_WATERFALL_3, vPos, o->Angle, vLight, 8, 2.5f);
                    }
                }
            }

            if (rand_fps_check(3))
            {
                VectorCopy(o->Position, vPos);
                vPos[0] += (float)(WorldRandom() % 400 - 200);
                vPos[1] += (float)(WorldRandom() % 400 - 200);
                Vector(1.0f, 1.f, 1.f, vLight);
                CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, 1, 1.f);
                CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, 24, 2.5f);

                VectorCopy(o->Position, vPos);
                vPos[0] += (float)(WorldRandom() % 500 - 250);
                vPos[1] += (float)(WorldRandom() % 500 - 250);
                Vector(0.1f, 1.0f, 0.1f, vLight);
                CreateEffect(BITMAP_CLOUD, vPos, o->Angle, vLight, 0, NULL, -1, 0, 0, 0, 2.0f);

                Vector(1.0f, 1.0f, 1.0f, vLight);
                vPos[2] += 50.f;
                CreateParticle(BITMAP_SPARK + 1, vPos, o->Angle, vLight, 5, 1.5f);
            }
        }

        if (m_byDetailState >= BATTLE_OF_SELUPAN_PATTERN_3 &&
            m_byDetailState <= BATTLE_OF_SELUPAN_PATTERN_7)
        {
            vec3_t vLight;
            float fScale = 1.0f;
            if (m_byDetailState == BATTLE_OF_SELUPAN_PATTERN_3 ||
                m_byDetailState == BATTLE_OF_SELUPAN_PATTERN_4)
            {
                fScale = 0.5f;
            }
            else if (m_byDetailState == BATTLE_OF_SELUPAN_PATTERN_5)
            {
                fScale = 1.0f;
            }
            else if (m_byDetailState == BATTLE_OF_SELUPAN_PATTERN_6 ||
                     m_byDetailState == BATTLE_OF_SELUPAN_PATTERN_7)
            {
                fScale = 1.5f;
            }

            Vector(1.0f, 0.1f, 0.1f, vLight);
            for (int k = 0; k < 1; ++k)
            {
                if (rand_fps_check(2))
                {
                    vec3_t vPos1, vPos2;
                    b->TransformByObjectBone(vPos1, presentation, 34);
                    CreateParticle(BITMAP_SMOKE, vPos1, o->Angle, vLight, 50, fScale);
                    CreateParticle(BITMAP_SMOKELINE1 + WorldRandom() % 3, vPos1, o->Angle, vLight,
                                   0, fScale);

                    b->TransformByObjectBone(vPos2, presentation, 52);
                    CreateParticle(BITMAP_SMOKE, vPos2, o->Angle, vLight, 50, fScale);
                    CreateParticle(BITMAP_SMOKELINE1 + WorldRandom() % 3, vPos2, o->Angle, vLight,
                                   0, fScale);

                    if (m_byDetailState == BATTLE_OF_SELUPAN_PATTERN_6 ||
                        m_byDetailState == BATTLE_OF_SELUPAN_PATTERN_7)
                    {
                        CreateParticle(BITMAP_FIRE_HIK1_MONO, vPos1, o->Angle, vLight, 0, o->Scale);
                        CreateParticle(BITMAP_FIRE_HIK1_MONO, vPos2, o->Angle, vLight, 0, o->Scale);
                    }
                }
            }
        }
    }
    break;
    case MODEL_SPIDER_EGGS_1:
    case MODEL_SPIDER_EGGS_2:
    case MODEL_SPIDER_EGGS_3: {
        if (visual.action == MONSTER01_DIE && visual.animationFrame <= 12.f)
        {
            vec3_t vPos, vLight;

            for (int i = 0; i < 1; ++i)
            {
                VectorCopy(o->Position, vPos);

                if (i == 0)
                {
                    vPos[0] += 100.f;
                }
                else if (i == 1)
                {
                    vPos[0] += 100.f;
                    vPos[1] += 100.f;
                }

                vPos[0] += WorldRandom() % 60 - 30.f;
                vPos[1] += WorldRandom() % 60 - 30.f;
                vPos[2] += WorldRandom() % 20 - 10.f;
                Vector(0.2f, 0.4f, 1.0f, vLight);
                CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, 11, 1.2f);

                if (rand_fps_check(2))
                {
                    VectorCopy(o->Position, vPos);
                    vPos[0] += (float)(WorldRandom() % 200 - 100);
                    vPos[1] += (float)(WorldRandom() % 200 - 100);
                    Vector(0.2f, 0.4f, 1.0f, vLight);
                    CreateEffect(BITMAP_CLOUD, vPos, o->Angle, vLight, 0, NULL, -1, 0, 0, 0, 1.0f);
                }
            }
        }
    }
    break;
    case MODEL_DARK_MAMMOTH: {
        vec3_t Light;
        vec3_t EndPos, EndRelative;
        Vector(1.f, 1.f, 1.f, Light);

        if (visual.action == MONSTER01_ATTACK2)
        {

        }
        else
        {
            vec3_t vPos, vRelative;
            Vector(0.f, 0.f, 0.f, vRelative);
            if (visual.action == MONSTER01_DIE)
            {
                float Scale = 3.5f;
                Vector(1.f, 1.f, 1.f, visual.movement.light);
                GetBonePosition(presentation, CharacterSocket::GIANT_MAMUD_BIP_TAIL, vRelative,
                                vPos);
                CreateParticle(BITMAP_SMOKE, vPos, o->Angle, visual.movement.light, 53, Scale);
                GetBonePosition(presentation, CharacterSocket::GIANT_MAMUD_BIP_TAIL_1, vRelative,
                                vPos);
                CreateParticle(BITMAP_SMOKE, vPos, o->Angle, visual.movement.light, 3, Scale);
                GetBonePosition(presentation, CharacterSocket::GIANT_MAMUD_BIP_TAIL_2, vRelative,
                                vPos);
                CreateParticle(BITMAP_SMOKE, vPos, o->Angle, visual.movement.light, 53, Scale);
                GetBonePosition(presentation, CharacterSocket::GIANT_MAMUD_BIP_SPAIN_1, vRelative,
                                vPos);
                CreateParticle(BITMAP_SMOKE, vPos, o->Angle, visual.movement.light, 3, Scale);
                GetBonePosition(presentation, CharacterSocket::GIANT_MAMUD_BIP_SPAIN_2, vRelative,
                                vPos);
                CreateParticle(BITMAP_SMOKE, vPos, o->Angle, visual.movement.light, 53, Scale);
                GetBonePosition(presentation, CharacterSocket::GIANT_MAMUD_BIP_SPAIN_3, vRelative,
                                vPos);
                CreateParticle(BITMAP_SMOKE, vPos, o->Angle, visual.movement.light, 3, Scale);
            }
        }
    }
    break;
    case MODEL_DARK_GIANT: {
    }
    break;
    case MODEL_DARK_COOLUTIN: {
        // if( visual.animationFrame >= 1.7f && visual.animationFrame <= 2.0f )

        if (visual.animationFrame <= 8.0f)
        {
            switch (visual.action)
            {
            case MONSTER01_DIE: {
                vec3_t vPos, vLight;

                if (WorldRandom() % 3 != 0)
                {
                    for (int i = 0; i < b->NumBones; ++i)
                    {
                        b->TransformByObjectBone(vPos, presentation, i);
                        if (rand_fps_check(5))
                        {
                            Vector(0.0f, 1.f, 0.2f, vLight);
                            CreateParticle(BITMAP_WATERFALL_5, vPos, o->Angle, vLight, 8, 2.0f);
                        }
                        if (rand_fps_check(5))
                        {
                            Vector(0.1f, 1.f, 0.1f, vLight);
                            CreateParticle(BITMAP_WATERFALL_3, vPos, o->Angle, vLight, 8, 2.5f);
                        }
                    }
                }

                if (rand_fps_check(3))
                {
                    VectorCopy(o->Position, vPos);
                    vPos[0] += (float)(WorldRandom() % 200 - 100);
                    vPos[1] += (float)(WorldRandom() % 200 - 100);
                    Vector(1.0f, 1.f, 1.f, vLight);
                    CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, 1, 0.5f);
                    CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, 24, 1.25f);

                    VectorCopy(o->Position, vPos);
                    vPos[0] += (float)(WorldRandom() % 250 - 125);
                    vPos[1] += (float)(WorldRandom() % 250 - 125);
                    Vector(0.1f, 1.0f, 0.1f, vLight);
                    CreateEffect(BITMAP_CLOUD, vPos, o->Angle, vLight, 0, NULL, -1, 0, 0, 0, 1.0f);

                    Vector(1.0f, 1.0f, 1.0f, vLight);
                    vPos[2] += 50.f;
                    CreateParticle(BITMAP_SPARK + 1, vPos, o->Angle, vLight, 5, 0.75f);
                }
            } // case MONSTER01_DIE:
            break;
            } // switch(visual.action)
        }
    }
    break;
    case MODEL_DARK_IRON_KNIGHT: {
    }
    break;
    }

    return false;
}

void CGM_Raklion::MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b)
{
    //int iType = MODEL_MONSTER01+455;

    switch (o->Type)
    {
    case MODEL_ICE_WALKER: {
    }
    break;
    case MODEL_GIANT_MAMMOTH: {
    }
    break;
    case MODEL_ICE_GIANT: {
        float Start_Frame = 3.f;
        float End_Frame = 8.0f;
        if ((o->AnimationFrame >= Start_Frame && o->AnimationFrame <= End_Frame &&
             o->CurrentAction == MONSTER01_ATTACK1) ||
            (o->AnimationFrame >= Start_Frame && o->AnimationFrame <= End_Frame &&
             o->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t Light;
            Vector(1.0f, 1.2f, 2.f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);

                b->TransformPosition(BoneTransform[35], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[36], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 0);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
    break;
    case MODEL_COOLUTIN: {
    }
    break;
    case MODEL_IRON_KNIGHT: {
        // AdvanceMonsterVisual(, visual)
    }
    break;

    case MODEL_SELUPAN: {
        // AdvanceMonsterVisual(, visual)
    }
    break;
    case MODEL_SPIDER_EGGS_1:
    case MODEL_SPIDER_EGGS_2:
    case MODEL_SPIDER_EGGS_3: {
    }
    break;

    case MODEL_DARK_MAMMOTH: {
    }
    break;
    case MODEL_DARK_GIANT: {
        float Start_Frame = 3.f;
        float End_Frame = 8.0f;
        if ((o->AnimationFrame >= Start_Frame && o->AnimationFrame <= End_Frame &&
             o->CurrentAction == MONSTER01_ATTACK1) ||
            (o->AnimationFrame >= Start_Frame && o->AnimationFrame <= End_Frame &&
             o->CurrentAction == MONSTER01_ATTACK2))
        {
            vec3_t Light;
            Vector(1.0f, 1.2f, 2.f, Light);

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / 10.f;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < 10; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);

                b->TransformPosition(BoneTransform[35], StartRelative, StartPos, false);
                b->TransformPosition(BoneTransform[36], EndRelative, EndPos, false);
                CreateBlur(c, StartPos, EndPos, Light, 0);

                fAnimationFrame += fSpeedPerFrame;
            }
        }
    } // case MODEL_MONSTER01+456:
    break;
    case MODEL_DARK_IRON_KNIGHT: {
    }
    break;
    case MODEL_DARK_COOLUTIN: {
    }
    break;
    }
}

bool CGM_Raklion::AdvanceObjectVisual(OBJECT *o, BMD *b, float)
{
    switch (o->Type)
    {
    case 16: {

        if (o->AnimationFrame >= 19)
        {
            SetAction(o, WorldRandom() % 2);
        }

        return true;
    }

    case 17: {

        if (o->CurrentAction >= 0 && o->CurrentAction <= 1 && o->AnimationFrame >= 19)
        {
            int iAniIndex = WorldRandom() % 100;

            if (iAniIndex < 90)
            {
                if (iAniIndex % 2 == 0)
                    SetAction(o, 0);
                else
                    SetAction(o, 1);
            }
            else
            {
                if (iAniIndex % 2 == 0)
                    SetAction(o, 2);
                else
                    SetAction(o, 3);
            }
        }
        else if (o->CurrentAction == 2 && o->AnimationFrame >= 97)
        {
            SetAction(o, WorldRandom() % 2);
        }
        else if (o->CurrentAction == 3 && o->AnimationFrame >= 98)
        {
            SetAction(o, WorldRandom() % 2);
        }

        return true;
    }

    case 19: {

        PrepareWorldObjectPose(*o);
        if (o->CurrentAction == 0)
        {
            vec3_t vRelativePos, vWorldPos, vLight;
            Vector(0, 0, 0, vRelativePos);
            Vector(1.f, 1.f, 1.f, vLight);

            if ((o->AnimationFrame >= 2 && o->AnimationFrame <= 5))
            {
                for (int i = 0; i < b->NumBones; ++i)
                {
                    b->TransformPosition(BoneTransform[i], vRelativePos, vWorldPos, false);
                    CreateParticle(BITMAP_WATERFALL_5, vWorldPos, o->Angle, vLight, 7);
                }
            }
            else if (o->AnimationFrame >= 11 && o->AnimationFrame <= 12)
            {
                b->TransformPosition(BoneTransform[6], vRelativePos, vWorldPos, false);
                CreateParticle(BITMAP_WATERFALL_5, vWorldPos, o->Angle, vLight, 7);
                b->TransformPosition(BoneTransform[7], vRelativePos, vWorldPos, false);
                CreateParticle(BITMAP_WATERFALL_5, vWorldPos, o->Angle, vLight, 7);
                b->TransformPosition(BoneTransform[8], vRelativePos, vWorldPos, false);
                CreateParticle(BITMAP_WATERFALL_5, vWorldPos, o->Angle, vLight, 7);
            }
            else if (o->AnimationFrame >= 13 && o->AnimationFrame <= 14)
            {
                b->TransformPosition(BoneTransform[9], vRelativePos, vWorldPos, false);
                CreateParticle(BITMAP_WATERFALL_5, vWorldPos, o->Angle, vLight, 7);
                b->TransformPosition(BoneTransform[10], vRelativePos, vWorldPos, false);
                CreateParticle(BITMAP_WATERFALL_5, vWorldPos, o->Angle, vLight, 7);
                b->TransformPosition(BoneTransform[11], vRelativePos, vWorldPos, false);
                CreateParticle(BITMAP_WATERFALL_5, vWorldPos, o->Angle, vLight, 7);
            }
            else if (o->AnimationFrame >= 15 && o->AnimationFrame <= 17)
            {
                for (int i = 12; i < 20; ++i)
                {
                    b->TransformPosition(BoneTransform[i], vRelativePos, vWorldPos, false);
                    CreateParticle(BITMAP_WATERFALL_5, vWorldPos, o->Angle, vLight, 7);
                }
            }
            else if (o->AnimationFrame >= 19.5f)
            {
                o->CurrentAction = 1;
            }

            for (int i = 0; i < b->NumBones; ++i)
            {
                b->TransformPosition(BoneTransform[i], vRelativePos, vWorldPos, false);
                CreateParticle(BITMAP_WATERFALL_3, vWorldPos, o->Angle, vLight, 8, 1.5f);
            }
        }
        else if (o->CurrentAction == 1)
        {
            if (rand_fps_check(40))
            {
                o->CurrentAction = 0;
            }
        }

        return true;
    }

    case 20: {

        PrepareWorldObjectPose(*o);
        if (o->CurrentAction == 0)
        {
            vec3_t vRelativePos, vWorldPos, vLight;
            Vector(0, 0, 0, vRelativePos);
            Vector(1.f, 1.f, 1.f, vLight);

            // 머리
            b->TransformPosition(BoneTransform[5], vRelativePos, vWorldPos, false);
            CreateParticle(BITMAP_WATERFALL_3, vWorldPos, o->Angle, vLight, 8, 2.f);

            if (o->AnimationFrame <= 8)
            {
                // 머리
                b->TransformPosition(BoneTransform[6], vRelativePos, vWorldPos, false);
                CreateParticle(BITMAP_WATERFALL_3, vWorldPos, o->Angle, vLight, 8, 1.5f);
                CreateParticle(BITMAP_WATERFALL_5, vWorldPos, o->Angle, vLight, 7, 0.1f);
            }

            if (o->AnimationFrame >= 12)
            {
                // 입가
                b->TransformPosition(BoneTransform[6], vRelativePos, vWorldPos, false);
                CreateParticle(BITMAP_WATERFALL_3, vWorldPos, o->Angle, vLight, 8, 2.f);
                CreateParticle(BITMAP_WATERFALL_5, vWorldPos, o->Angle, vLight, 7, 0.1f);
            }

            if (o->AnimationFrame <= 15)
            {
                // 날개
                b->TransformPosition(BoneTransform[8], vRelativePos, vWorldPos, false);
                CreateParticle(BITMAP_WATERFALL_3, vWorldPos, o->Angle, vLight, 9, 2.f);
                CreateParticle(BITMAP_WATERFALL_5, vWorldPos, o->Angle, vLight, 7, 0.1f);

                b->TransformPosition(BoneTransform[11], vRelativePos, vWorldPos, false);
                CreateParticle(BITMAP_WATERFALL_3, vWorldPos, o->Angle, vLight, 9, 2.f);
                CreateParticle(BITMAP_WATERFALL_5, vWorldPos, o->Angle, vLight, 7, 0.1f);
            }

            if (o->AnimationFrame >= 19)
            {
                o->CurrentAction = 1;
            }
        }
        else if (o->CurrentAction == 1)
        {
            if (rand_fps_check(40))
            {
                o->CurrentAction = 0;
            }
        }

        return true;
    }

    case 21: {

        // 얼음 깨는 에니메이션 동작이고
        PrepareWorldObjectPose(*o);
        if (o->CurrentAction == 0)
        {
            if (o->AnimationFrame >= 4 && o->AnimationFrame <= 8)
            {
                vec3_t vRelativePos, vWorldPos, vLight;
                Vector(0, 0, 0, vRelativePos);
                Vector(1.f, 1.f, 1.f, vLight);
                // 입앞 본
                b->TransformPosition(BoneTransform[7], vRelativePos, vWorldPos, false);
                // 물 이펙트
                CreateParticle(BITMAP_WATERFALL_3, vWorldPos, o->Angle, vLight, 9, 0.5f);
                CreateParticle(BITMAP_WATERFALL_5, vWorldPos, o->Angle, vLight, 7);

                // 7, 16, 17, 21, 22
                // 연기 이펙트
                b->TransformPosition(BoneTransform[7], vRelativePos, vWorldPos, false);
                CreateParticle(BITMAP_WATERFALL_2, vWorldPos, o->Angle, vLight, 5, 1.f);
                b->TransformPosition(BoneTransform[16], vRelativePos, vWorldPos, false);
                CreateParticle(BITMAP_WATERFALL_2, vWorldPos, o->Angle, vLight, 5, 0.5f);
                b->TransformPosition(BoneTransform[17], vRelativePos, vWorldPos, false);
                CreateParticle(BITMAP_WATERFALL_2, vWorldPos, o->Angle, vLight, 5, 0.7f);
                b->TransformPosition(BoneTransform[20], vRelativePos, vWorldPos, false);
                CreateParticle(BITMAP_WATERFALL_2, vWorldPos, o->Angle, vLight, 5, 0.3f);
                b->TransformPosition(BoneTransform[21], vRelativePos, vWorldPos, false);
                CreateParticle(BITMAP_WATERFALL_2, vWorldPos, o->Angle, vLight, 5, 0.3f);
                b->TransformPosition(BoneTransform[22], vRelativePos, vWorldPos, false);
                CreateParticle(BITMAP_WATERFALL_2, vWorldPos, o->Angle, vLight, 5, 0.5f);
                b->TransformPosition(BoneTransform[23], vRelativePos, vWorldPos, false);
                CreateParticle(BITMAP_WATERFALL_2, vWorldPos, o->Angle, vLight, 5, 0.3f);
                b->TransformPosition(BoneTransform[24], vRelativePos, vWorldPos, false);
                CreateParticle(BITMAP_WATERFALL_2, vWorldPos, o->Angle, vLight, 5, 0.3f);
            }

            if (o->AnimationFrame >= 19.f)
            {
                o->CurrentAction = 1;
            }
        }
        else if (o->CurrentAction == 1)
        {
            if (rand_fps_check(40))
            {
                o->CurrentAction = 0;
            }
        }

        return true;
    }

    case 70: {
        vec3_t vLight;
        Vector(0.1f, 0.4f, 1.0f, vLight);

        switch (WorldRandom() % 3)
        {
        case 0:
            CreateParticle(BITMAP_FIRE_HIK1_MONO, o->Position, o->Angle, vLight, 2, o->Scale);
            break;
        case 1:
            CreateParticle(BITMAP_FIRE_HIK2_MONO, o->Position, o->Angle, vLight, 6, o->Scale);
            break;
        case 2:
            CreateParticle(BITMAP_FIRE_HIK3_MONO, o->Position, o->Angle, vLight, 2, o->Scale);
            break;
        }
        CreateParticle(BITMAP_FIRE_HIK1_MONO, o->Position, o->Angle, vLight, 2, o->Scale);

        return true;
    }
    break;
    case 80: {
        vec3_t vLight;
        Vector(0.7f, 0.2f, 0.1f, vLight);

        switch (WorldRandom() % 3)
        {
        case 0:
            CreateParticle(BITMAP_FIRE_HIK1_MONO, o->Position, o->Angle, vLight, 2, o->Scale);
            break;
        case 1:
            CreateParticle(BITMAP_FIRE_HIK2_MONO, o->Position, o->Angle, vLight, 6, o->Scale);
            break;
        case 2:
            CreateParticle(BITMAP_FIRE_HIK3_MONO, o->Position, o->Angle, vLight, 2, o->Scale);
            break;
        }
        CreateParticle(BITMAP_FIRE_HIK1_MONO, o->Position, o->Angle, vLight, 2, o->Scale);

        return true;
    }
    break;
    }

    return false;
}

void CGM_Raklion::EmitKnightAttack(CHARACTER &character, BMD &model)
{
    if (FPS_ANIMATION_FACTOR <= 0.f || character.WorldVisualAttackFrameTime != WorldTime ||
        character.WorldVisualAttackAction != MONSTER01_ATTACK2)
        return;
    OBJECT &object = character.Object;
    const float start = character.WorldVisualAttackStart;
    const float end = start + character.WorldVisualAttackFrames;
    constexpr float tickTolerance = 0.0001f;
    AnimationPoseSample pose(&object, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    for (float tick = std::ceil(start - tickTolerance); tick < end - tickTolerance; tick += 1.f)
    {
        const float elapsed = std::max(0.f, tick - start);
        auto birth = sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR - elapsed);
        const float fraction = birth.FrameFraction();
        vec3_t origin, angle;
        object.MotionTrace.Sample(WorldTime, fraction, object.Position, origin);
        VectorCopy(object.Angle, angle);
        angle[2] = object.MotionTrace.SampleYaw(WorldTime, fraction, angle[2]);
        if (tick >= 2.f && tick <= 8.f)
        {
            for (int child = 0; child < 3; ++child)
            {
                vec3_t position, center;
                VectorCopy(origin, center);
                center[2] += 120.f;
                GetNearRandomPos(center, 300, position);
                constexpr float spearReach = 1400.f;
                position[0] -= spearReach * sinf(angle[2] * Q_PI / 180.f);
                position[1] += spearReach * cosf(angle[2] * Q_PI / 180.f);
                CreateJoint(MODEL_SPEARSKILL, position, position, angle, 2, &object, 40.f);
            }
        }
        if (tick >= 6.f && tick <= 12.f && WorldRandom() % 2 == 0)
        {
            vec3_t position, offset{}, light{1.f, 1.f, 1.f};
            pose.SampleBonePosition(model, object, 26, offset, WorldTime, fraction, position);
            const float distance = 100.f + (tick - 8.f) * 10.f;
            position[0] += distance * sinf(angle[2] * Q_PI / 180.f);
            position[1] -= distance * cosf(angle[2] * Q_PI / 180.f);
            CreateEffect(MODEL_SPEAR, position, angle, light, 1, &object);
        }
        if (tick == 12.f)
            CreateEffect(MODEL_COMBO, origin, angle, object.Light);
    }
}

bool CGM_Raklion::AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                       WorldCharacterVisualState &visual)
{
    AdvanceEggSmoke(o, b);

    vec3_t vPos, vRelative, vLight;

    switch (o->Type)
    {
    case MODEL_ICE_WALKER: {
    }
    break;
    case MODEL_GIANT_MAMMOTH: {
    }
    break;
    case MODEL_ICE_GIANT:


        break;
    case MODEL_COOLUTIN: {
    }
    break;
    case MODEL_IRON_KNIGHT: {
        EmitKnightAttack(*c, *b);
        vec3_t Light;
        Vector(1.0f, 1.2f, 2.f, Light);

        vec3_t StartPos, StartRelative;
        vec3_t EndPos, EndRelative;

        float fActionSpeed = b->Actions[visual.action].PlaySpeed;
        float fSpeedPerFrame = fActionSpeed / 10.f;
        float fAnimationFrame = visual.animationFrame - fActionSpeed;
        for (int i = 0; i < 10; i++)
        {
            b->AnimationAtFrame(BoneTransform, fAnimationFrame, visual.priorAnimationFrame,
                                visual.priorAction, o->Angle, o->HeadAngle);

            Vector(0.f, 0.f, 0.f, StartRelative);
            Vector(0.f, 0.f, 0.f, EndRelative);

            b->TransformPosition(BoneTransform[35], StartRelative, StartPos, false);
            b->TransformPosition(BoneTransform[36], EndRelative, EndPos, false);
            CreateBlur(c, StartPos, EndPos, Light, 3);

            fAnimationFrame += fSpeedPerFrame;
        }

        int iBones[] = {20, 37, 45, 51};

        for (int i = 0; i < 4; ++i)
        {
            if (WorldRandom() % 6 > 0)
                continue;

            if (WorldRandom() % 3 > 0)
            {
                Vector(0.3f, 0.6f, 1.0f, vLight);
            }
            else
            {
                Vector(0.8f, 0.8f, 0.8f, vLight);
            }

            b->TransformByObjectBone(vPos, o, iBones[i]);
            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, 50, 1.0f);
            CreateParticle(BITMAP_SMOKELINE1 + WorldRandom() % 3, vPos, o->Angle, vLight, 2, 0.8f);
        }



    }
    break;
    case MODEL_SELUPAN: {
        vec3_t Light;
        Vector(1.0f, 1.2f, 2.f, Light);

        vec3_t StartPos, StartRelative;
        vec3_t EndPos, EndRelative;

        float fActionSpeed = b->Actions[visual.action].PlaySpeed;
        float fSpeedPerFrame = fActionSpeed / 10.f;
        float fAnimationFrame = visual.animationFrame - fActionSpeed;
        for (int i = 0; i < 10; i++)
        {
            b->AnimationAtFrame(BoneTransform, fAnimationFrame, visual.priorAnimationFrame,
                                visual.priorAction, o->Angle, o->HeadAngle);

            Vector(0.f, 0.f, 0.f, StartRelative);
            Vector(0.f, 0.f, 0.f, EndRelative);

            b->TransformPosition(BoneTransform[69], StartRelative, StartPos, false);
            b->TransformPosition(BoneTransform[73], EndRelative, EndPos, false);
            CreateBlur(c, StartPos, EndPos, Light, 0);

            fAnimationFrame += fSpeedPerFrame;
        }

        VectorCopy(o->Position, b->BodyOrigin);
        Vector(0.0f, 0.0f, 0.0f, vRelative);

        float fLumi1 = (sinf(WorldTime * 0.004f) + 1.f) * 0.25f;
        float fLumi2 = (sinf(WorldTime * 0.004f) + 1.f) * 0.2f;

        Vector(0.3f + fLumi1, 0.6f + fLumi1, 1.0f + fLumi1, vLight);
        Vector(0.0f, 10.0f, 0.0f, vRelative);
        b->TransformByObjectBone(vPos, o, 9, vRelative);
        // flare01.jpg
        CreateSprite(BITMAP_LIGHT, vPos, 2.0f + fLumi2, vLight, o);
        CreateSprite(BITMAP_LIGHT, vPos, 2.0f + fLumi2, vLight, o);
        CreateSprite(BITMAP_LIGHT, vPos, 3.0f + fLumi2, vLight, o);

        Vector(0.0f, 0.0f, 0.0f, vRelative);
        fLumi1 = (sinf(WorldTime * 0.004f) + 1.f) * 0.1f;
        Vector(0.1f + fLumi1, 0.2f + fLumi1, 1.0f + fLumi1, vLight);
        Vector(0.0f, 0.0f, 0.0f, vRelative);
        b->TransformByObjectBone(vPos, o, 70);
        // flare01.jpg
        CreateSprite(BITMAP_LIGHT, vPos, 2.1f + fLumi2, vLight, o, (int)WorldTime * 0.08f);
        // shiny05.jpg
        CreateSprite(BITMAP_SHINY + 6, vPos, 1.8f + fLumi2, vLight, o);

        Vector(0.0f, 0.0f, 0.0f, vRelative);
        b->TransformByObjectBone(vPos, o, 74);
        // flare01.jpg
        CreateSprite(BITMAP_LIGHT, vPos, 2.1f + fLumi2, vLight, o, (int)WorldTime * 0.08f);
        // shiny05.jpg
        CreateSprite(BITMAP_SHINY + 6, vPos, 1.8f + fLumi2, vLight, o);

        b->TransformByObjectBone(vPos, o, 71);
        // clouds2.jpg
        CreateSprite(BITMAP_EVENT_CLOUD, vPos, 0.6f, vLight, o, -(int)WorldTime * 0.1f);
        CreateSprite(BITMAP_EVENT_CLOUD, vPos, 0.40f, vLight, o, -(int)WorldTime * 0.2f);
        // flare01.jpg
        CreateSprite(BITMAP_LIGHT, vPos, 2.3f + fLumi2, vLight, o);
        // Shiny02.jpg
        CreateSprite(BITMAP_SHINY + 1, vPos, 2.3f + fLumi2, vLight, o, (int)WorldTime * 0.08f);
        // shiny05.jpg
        CreateSprite(BITMAP_SHINY + 6, vPos, 2.3f + fLumi2, vLight, o, (int)WorldTime * 0.15f);
        CreateParticle(BITMAP_SPARK + 1, vPos, o->Angle, vLight, 11, 1.5f);

        b->TransformByObjectBone(vPos, o, 72);
        // flare01.jpg
        CreateSprite(BITMAP_LIGHT, vPos, 1.8f + fLumi2, vLight, o);
        // shiny05.jpg
        CreateSprite(BITMAP_SHINY + 6, vPos, 1.3f + fLumi2, vLight, o);

        return true;
    }
    break;
    case MODEL_SPIDER_EGGS_1:
    case MODEL_SPIDER_EGGS_2:
    case MODEL_SPIDER_EGGS_3: {
        return true;
    }
    break;
    case MODEL_DARK_MAMMOTH: {
    }
    break;
    case MODEL_DARK_GIANT: {


    }
    break;
    case MODEL_DARK_IRON_KNIGHT: {
        EmitKnightAttack(*c, *b);
        vec3_t Light;
        Vector(1.0f, 1.2f, 2.f, Light);

        vec3_t StartPos, StartRelative;
        vec3_t EndPos, EndRelative;

        float fActionSpeed = b->Actions[visual.action].PlaySpeed;
        float fSpeedPerFrame = fActionSpeed / 10.f;
        float fAnimationFrame = visual.animationFrame - fActionSpeed;
        for (int i = 0; i < 10; i++)
        {
            b->AnimationAtFrame(BoneTransform, fAnimationFrame, visual.priorAnimationFrame,
                                visual.priorAction, o->Angle, o->HeadAngle);

            Vector(0.f, 0.f, 0.f, StartRelative);
            Vector(0.f, 0.f, 0.f, EndRelative);

            b->TransformPosition(BoneTransform[35], StartRelative, StartPos, false);
            b->TransformPosition(BoneTransform[36], EndRelative, EndPos, false);
            CreateBlur(c, StartPos, EndPos, Light, 3);

            fAnimationFrame += fSpeedPerFrame;
        }

        int iBones[] = {20, 37, 45, 51};

        for (int i = 0; i < 4; ++i)
        {
            if (WorldRandom() % 6 > 0)
                continue;

            if (WorldRandom() % 3 > 0)
            {
                Vector(0.3f, 0.6f, 1.0f, vLight);
            }
            else
            {
                Vector(0.8f, 0.8f, 0.8f, vLight);
            }

            b->TransformByObjectBone(vPos, o, iBones[i]);
            CreateParticle(BITMAP_SMOKE, vPos, o->Angle, vLight, 50, 1.0f);
            CreateParticle(BITMAP_SMOKELINE1 + WorldRandom() % 3, vPos, o->Angle, vLight, 2, 0.8f);
        }



    }
    break;
    case MODEL_DARK_COOLUTIN: {
    }
    break;
    }

    return true;
}

bool CGM_Raklion::CreateSnow(PARTICLE *o)
{
    if (IsIceCity() == false)
        return false;

    o->Type = BITMAP_LEAF1;
    o->Scale = (float)(WorldRandom() % 10 + 3);
    if (sessionKeeper_.Random()->FpsCheck(10, 1.f))
    {
        o->Scale = (float)(WorldRandom() % 3 + 10);
    }

    Vector(Hero->Object.Position[0] + (float)(WorldRandom() % 1600 - 800),
           Hero->Object.Position[1] + (float)(WorldRandom() % 1400 - 500),
           Hero->Object.Position[2] + (float)(WorldRandom() % 200 + 200), o->Position);
    Vector(-(float)(WorldRandom() % 30 + 50), 0.f, 0.f, o->Angle);
    vec3_t Velocity;
    Vector(0.f, 0.f, -(float)(WorldRandom() % 20 + 30), Velocity);
    float Matrix[3][4];
    AngleMatrix(o->Angle, Matrix);
    VectorRotate(Velocity, Matrix, o->Velocity);

    return true;
}

bool CGM_Raklion::AttackEffectMonster(CHARACTER *c, OBJECT *o, BMD *b)
{
    if (IsIceCity() == false)
        return false;

    return false;
}

bool CGM_Raklion::PlayMonsterSound(OBJECT *o)
{
    if (IsIceCity() == false)
        return false;

    float fDis_x, fDis_y;
    fDis_x = o->Position[0] - Hero->Object.Position[0];
    fDis_y = o->Position[1] - Hero->Object.Position[1];
    float fDistance = sqrtf(fDis_x * fDis_x + fDis_y * fDis_y);

    if (fDistance > 500.0f)
        return true;

    switch (o->Type)
    {
    case MODEL_ICE_WALKER: // Ice Walker
        if (MONSTER01_ATTACK1 == o->CurrentAction || MONSTER01_ATTACK2 == o->CurrentAction)
        {
            PlayBuffer(SOUND_RAKLION_ICEWALKER_ATTACK);
        }
        else if (MONSTER01_WALK == o->CurrentAction)
        {
            if (rand_fps_check(20))
            {
                PlayBuffer(SOUND_RAKLION_ICEWALKER_MOVE);
            }
        }
        else if (MONSTER01_DIE == o->CurrentAction)
        {
            {
                PlayBuffer(SOUND_ELBELAND_WOLFHUMAN_DEATH01);
            }
        }
        return true;
    case MODEL_GIANT_MAMMOTH:
        if (MONSTER01_ATTACK1 == o->CurrentAction || MONSTER01_ATTACK2 == o->CurrentAction)
        {
            PlayBuffer(SOUND_RAKLION_GIANT_MAMUD_ATTACK);
        }
        else if (o->CurrentAction == MONSTER01_WALK)
        {
            if (rand_fps_check(100))
            {
                PlayBuffer(SOUND_RAKLION_GIANT_MAMUD_MOVE);
            }
        }
        else if (o->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_RAKLION_GIANT_MAMUD_DEATH);
        }
        return true;
    case MODEL_ICE_GIANT:
        if (o->CurrentAction == MONSTER01_WALK)
        {
            if (rand_fps_check(100))
            {
                PlayBuffer(SOUND_RAKLION_ICEGIANT_MOVE);
            }
        }
        else if (o->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_RAKLION_ICEGIANT_DEATH);
        }
        return true;
    case MODEL_COOLUTIN:
        if (MONSTER01_ATTACK1 == o->CurrentAction || MONSTER01_ATTACK2 == o->CurrentAction)
        {
            PlayBuffer(SOUND_RAKLION_COOLERTIN_ATTACK);
        }
        else if (MONSTER01_WALK == o->CurrentAction)
        {
            if (rand_fps_check(20))
            {
                PlayBuffer(SOUND_RAKLION_COOLERTIN_MOVE);
            }
        }
        else if (MONSTER01_DIE == o->CurrentAction)
        {
            {
                PlayBuffer(SOUND_MONSTER_HELLSPIDERDIE);
            }
        }

        return true;
    case MODEL_IRON_KNIGHT:
        if (o->CurrentAction == MONSTER01_ATTACK1 || o->CurrentAction == MONSTER01_ATTACK2)
        {
            PlayBuffer(SOUND_RAKLION_IRON_KNIGHT_ATTACK);
        }
        else if (o->CurrentAction == MONSTER01_STOP1 || o->CurrentAction == MONSTER01_STOP2 ||
                 o->CurrentAction == MONSTER01_WALK)
        {
            PlayBuffer(SOUND_RAKLION_IRON_KNIGHT_MOVE);
        }
        else if (o->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_MONSTER_DEATH1);
        }
        return true;

    case MODEL_SELUPAN:
        if (o->CurrentAction == MONSTER01_ATTACK1 || o->CurrentAction == MONSTER01_ATTACK2)
        {
            PlayBuffer(SOUND_RAKLION_SERUFAN_ATTACK1);
        }
        else if (o->CurrentAction == MONSTER01_ATTACK3)
        {
            PlayBuffer(SOUND_RAKLION_SERUFAN_ATTACK2);
        }
        else if (o->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_RAKLION_SERUFAN_WORD2);
        }
        return true;

    case MODEL_DARK_MAMMOTH:
        if (MONSTER01_ATTACK1 == o->CurrentAction || MONSTER01_ATTACK2 == o->CurrentAction)
        {
            PlayBuffer(SOUND_RAKLION_GIANT_MAMUD_ATTACK);
        }
        else if (o->CurrentAction == MONSTER01_WALK)
        {
            if (rand_fps_check(100))
            {
                PlayBuffer(SOUND_RAKLION_GIANT_MAMUD_MOVE);
            }
        }
        else if (o->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_RAKLION_GIANT_MAMUD_DEATH);
        }
        return true;
    case MODEL_DARK_GIANT:
        if (o->CurrentAction == MONSTER01_WALK)
        {
            if (rand_fps_check(100))
            {
                PlayBuffer(SOUND_RAKLION_ICEGIANT_MOVE);
            }
        }
        else if (o->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_RAKLION_ICEGIANT_DEATH);
        }
        return true;
    case MODEL_DARK_IRON_KNIGHT:
        if (o->CurrentAction == MONSTER01_ATTACK1 || o->CurrentAction == MONSTER01_ATTACK2)
        {
            PlayBuffer(SOUND_RAKLION_IRON_KNIGHT_ATTACK);
        }
        else if (o->CurrentAction == MONSTER01_STOP1 || o->CurrentAction == MONSTER01_STOP2 ||
                 o->CurrentAction == MONSTER01_WALK)
        {
            PlayBuffer(SOUND_RAKLION_IRON_KNIGHT_MOVE);
        }
        else if (o->CurrentAction == MONSTER01_DIE)
        {
            PlayBuffer(SOUND_MONSTER_DEATH1);
        }
        return true;
    case MODEL_DARK_COOLUTIN:
        if (MONSTER01_ATTACK1 == o->CurrentAction || MONSTER01_ATTACK2 == o->CurrentAction)
        {
            PlayBuffer(SOUND_RAKLION_COOLERTIN_ATTACK);
        }
        else if (MONSTER01_WALK == o->CurrentAction)
        {
            if (rand_fps_check(20))
            {
                PlayBuffer(SOUND_RAKLION_COOLERTIN_MOVE);
            }
        }
        else if (MONSTER01_DIE == o->CurrentAction)
        {
            {
                PlayBuffer(SOUND_MONSTER_HELLSPIDERDIE);
            }
        }

        return true;
    }

    return false;
}

void CGM_Raklion::MoveEffect(CTimer2::StartTickTime &timer2StartTickTime)
{
    if (m_bVisualEffect == false)
    {
        return;
    }

    if (m_byState <= RAKLION_STATE_STANDBY)
    {
        m_Timer.UpdateTime(timer2StartTickTime);

        if (m_Timer.IsTime() == true)
        {
            m_Timer.ResetTimer();
            m_bVisualEffect = false;

            if (m_byState == RAKLION_STATE_STANDBY)
            {
                EarthQuake = 0.f;
            }
        }
        else
        {
            if (rand_fps_check(2))
            {
                CreateMapEffect();
            }
        }
    }
    else
    {
        m_Timer.ResetTimer();
        m_bVisualEffect = false;
        EarthQuake = 0.f;
    }
}

void CGM_Raklion::CreateMapEffect()
{
    if (m_byState <= RAKLION_STATE_NOTIFY_1)
    {
        float fScale = 1.f;
        vec3_t vPos, vLight;
        OBJECT *pObject = &Hero->Object;

        for (int i = 0; i < 5; ++i)
        {
            Vector(1.f, 1.0f, 1.0f, vLight);
            VectorCopy(pObject->Position, vPos);
            vPos[0] += (WorldRandom() % 1600 - 800.f);
            vPos[1] += (WorldRandom() % 1600 - 800.f);
            vPos[2] = 600.f + WorldRandom() % 100;

            fScale = 2.0f + (WorldRandom() % 20) / 5.0f;
            int iIndex = MODEL_EFFECT_BROKEN_ICE0;

            CreateEffect(iIndex, vPos, pObject->Angle, vLight, 2, NULL, -1, 0, 0, 0, fScale);

            VectorCopy(pObject->Position, vPos);
            vPos[0] += (WorldRandom() % 1600 - 800.f);
            vPos[1] += (WorldRandom() % 1600 - 800.f);
            vPos[2] = 600.f + WorldRandom() % 100;

            fScale = 0.5f + (WorldRandom() % 10) / 5.0f;
            iIndex = MODEL_EFFECT_BROKEN_ICE2;

            CreateEffect(iIndex, vPos, pObject->Angle, vLight, 2, NULL, -1, 0, 0, 0, fScale);

            VectorCopy(pObject->Position, vPos);
            vPos[0] += (WorldRandom() % 1600 - 800.f);
            vPos[1] += (WorldRandom() % 1600 - 800.f);
            vPos[2] = 600.f + WorldRandom() % 100;

            Vector(1.f, 0.8f, 0.8f, vLight);
            fScale = 0.005f + (WorldRandom() % 10) / 200.0f;
            CreateEffect(MODEL_FALL_STONE_EFFECT, vPos, pObject->Angle, vLight, 2, NULL, -1, 0, 0,
                         0, fScale);

            Vector(0.4f, 0.4f, 0.5f, vLight);
            fScale = 0.5f + (WorldRandom() % 10) / 20.0f;
            for (int k = 0; k < 3; ++k)
            {
                VectorCopy(pObject->Position, vPos);
                vPos[0] += (WorldRandom() % 1000 - 500.f);
                vPos[1] += (WorldRandom() % 1000 - 500.f);
                vPos[2] = 760.f + WorldRandom() % 150;
                fScale = 0.2f + (WorldRandom() % 20) / 40.0f;
                CreateParticle(BITMAP_WATERFALL_3 + (WorldRandom() % 2), vPos, pObject->Angle,
                               vLight, 13, fScale);
            }
        }
    }
    else if (m_byState == RAKLION_STATE_STANDBY)
    {
        vec3_t vPos, vLight;
        float fScale = 1.f;
        OBJECT *pObject = &Hero->Object;
        Vector(0.3f, 0.5f, 1.f, vLight);

        EarthQuake = (float)(WorldRandom() % 2 - 2) * 0.5f;

        VectorCopy(pObject->Position, vPos);
        vPos[0] += (WorldRandom() % 2000 - 1000.f);
        vPos[1] += (WorldRandom() % 2000 - 1000.f);
        vPos[2] = 500.f + WorldRandom() % 100;

        fScale = 2.0f + (WorldRandom() % 20) / 5.0f;
        int iIndex = MODEL_EFFECT_BROKEN_ICE1;

        CreateEffect(iIndex, vPos, pObject->Angle, vLight, 2, NULL, -1, 0, 0, 0, fScale);

        VectorCopy(pObject->Position, vPos);
        vPos[0] += (WorldRandom() % 2000 - 1000.f);
        vPos[1] += (WorldRandom() % 2000 - 1000.f);
        vPos[2] = 500.f + WorldRandom() % 100;

        fScale = 0.5f + (WorldRandom() % 10) / 5.0f;
        iIndex = MODEL_EFFECT_BROKEN_ICE3;

        CreateEffect(iIndex, vPos, pObject->Angle, vLight, 2, NULL, -1, 0, 0, 0, fScale);

        VectorCopy(pObject->Position, vPos);
        vPos[0] += (WorldRandom() % 2000 - 1000.f);
        vPos[1] += (WorldRandom() % 2000 - 1000.f);
        vPos[2] = 500.f + WorldRandom() % 100;

        Vector(1.f, 0.8f, 0.8f, vLight);
        fScale = 0.05f + (WorldRandom() % 10) / 20.0f;
        CreateEffect(MODEL_FALL_STONE_EFFECT, vPos, pObject->Angle, vLight, 2, NULL, -1, 0, 0, 0,
                     fScale);
    }
}

void CGM_Raklion::PlayBGM()
{
    if (gMapManager.ContextMap() == WD_57ICECITY)
    {
        PlayMp3(MUSIC_RAKLION);
    }
    else
    {
        StopMp3(MUSIC_RAKLION);
    }

    if (gMapManager.ContextMap() == WD_58ICECITY_BOSS && m_bMusicBossMap == true)
    {
        PlayMp3(MUSIC_RAKLION_BOSS);
    }
    else
    {
        StopMp3(MUSIC_RAKLION_BOSS);
    }
}

void CGM_Raklion::AdvanceEggSmoke(OBJECT *object, BMD *model)
{
    if (object->Type < MODEL_SPIDER_EGGS_1 || object->Type > MODEL_SPIDER_EGGS_3 ||
        object->CurrentAction == MONSTER01_DIE)
        return;
    const int bones[] = {object->Type == MODEL_SPIDER_EGGS_1 ? 57 : 95, 115, 173};
    const int boneCount = object->Type - MODEL_SPIDER_EGGS_1 + 1;
    const float spacing = object->Type == MODEL_SPIDER_EGGS_1 ? 60.f : 100.f;
    const float luminosity = (sinf(WorldTime * 0.004f) + 1.2f) * 0.5f + 0.1f;
    vec3_t light = {0.1f * luminosity, 0.6f * luminosity, 0.7f * luminosity};
    for (int instance = 0; instance < 3; ++instance)
        for (int bone = 0; bone < boneCount; ++bone)
        {
            if (!rand_fps_check(100))
                continue;
            vec3_t position;
            model->TransformByObjectBone(position, object, bones[bone]);
            if (instance < 2)
                position[0] += spacing;
            if (instance == 1)
                position[1] += spacing;
            CreateParticle(BITMAP_SMOKE, position, object->Angle, light, 33, 1.f);
        }
}

std::optional<bool> SEASON4A::CGM_Raklion::ObjectVisibility(const OBJECT &object, bool)
{
    if (object.Type == 30 || object.Type == 31)
        return TestFrustrum2D(object.Position[0] * 0.01f, object.Position[1] * 0.01f, -600.f);
    if (object.Type == 76)
        return false;
    return std::nullopt;
}

float SEASON4A::CGM_Raklion::ObjectAnimationSpeed(const OBJECT &object, const BMD &model,
                                                  float speed) const
{
    return (object.Type == 16 || object.Type == 17 || object.Type == 68)
               ? model.Actions[object.CurrentAction].PlaySpeed
               : speed;
}

void SEASON4A::CGM_Raklion::PlayAmbientSounds()
{
    if (gMapManager.ContextMap() == WD_58ICECITY_BOSS)
    {
        PlayBuffer(SOUND_WIND01, NULL, true);
    }
}

bool SEASON4A::CGM_Raklion::AllowsAmbientSound(ESound sound) const
{
    return gMapManager.ContextMap() == WD_58ICECITY_BOSS && (sound == SOUND_WIND01);
}

void SEASON4A::CGM_Raklion::UpdateMusic()
{
    PlayBGM();
}

bool SEASON4A::CGM_Raklion::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_RAKLION) == 0 || std::strcmp(track, MUSIC_RAKLION_BOSS) == 0;
}

bool SEASON4A::CGM_Raklion::CreateWeather(PARTICLE *particle, int)
{
    return CreateSnow(particle);
}

ESound SEASON4A::CGM_Raklion::WalkingSound(int tile, bool safe) const
{
    return SOUND_HUMAN_WALK_SNOW;
}

void CGMBattleCastle::AdvancePlayerVisual(CHARACTER *c, OBJECT *o)
{
    if (gMapManager.InBattleCastle() == false)
        return;
    if (IsBattleCastleStart() == false)
        return;

    CreateGuardStoneHealingVisual(c, 380.f);
}

bool CGMBattleCastle::CreateFireSnuff(PARTICLE *o)
{
    if (gMapManager.ContextMap() != WD_55LOGINSCENE)
    {
        if (gMapManager.InBattleCastle() == false)
            return false;
        if (IsBattleCastleStart() == false)
            return false;
        if (InBattleCastle3(Hero->Object.Position) == false)
            return false;
    }

    o->Type = BITMAP_FIRE_SNUFF;
    o->Scale = WorldRandom() % 50 / 100.f + 0.5f;
    vec3_t Position;

    if (gMapManager.ContextMap() == WD_55LOGINSCENE)
    {
        Vector(Hero->Object.Position[0] + (float)(WorldRandom() % 1600 - 800),
               Hero->Object.Position[1] + (float)(WorldRandom() % 1400 - 500),
               Hero->Object.Position[2] + (float)(WorldRandom() % 1000 + 50), Position);
    }
    else
    {
        Vector(Hero->Object.Position[0] + (float)(WorldRandom() % 1600 - 800),
               Hero->Object.Position[1] + (float)(WorldRandom() % 1400 - 500),
               Hero->Object.Position[2] + (float)(WorldRandom() % 300 + 50), Position);
    }

    VectorCopy(Position, o->Position);
    VectorCopy(Position, o->StartPosition);
    o->Velocity[0] = -(float)(WorldRandom() % 64 + 64) * 0.1f;
    if (Position[1] < g_Camera.Position[1] + 400.f)
    {
        o->Velocity[0] = -o->Velocity[0] + 3.2f;
    }
    o->Velocity[1] = (float)(WorldRandom() % 32 - 16) * 0.1f;
    o->Velocity[2] = (float)(WorldRandom() % 32 - 16) * 0.1f;
    o->TurningForce[0] = (float)(WorldRandom() % 16 - 8) * 0.1f;
    o->TurningForce[1] = (float)(WorldRandom() % 64 - 32) * 0.1f;
    o->TurningForce[2] = (float)(WorldRandom() % 16 - 8) * 0.1f;

    Vector(1.f, 0.f, 0.f, o->Light);

    return true;
}

bool CGMBattleCastle::MoveBattleCastleVisual(OBJECT *o)
{
    if (gMapManager.InBattleCastle() == false)
        return false;

    float Luminosity;
    vec3_t Light;

    switch (o->Type)
    {
    case 4:
        o->HiddenMesh = -1;
        o->BlendMeshTexCoordU = WorldTime * 0.0001f;
        break;

    case 19:
        break;

    case 42:
    case 52:
    case 54:
        o->HiddenMesh = -2;
        break;

    case 0:
        if (IsBattleCastleStart() == false)
        {
            Luminosity = (float)(WorldRandom() % 4 + 3) * 0.1f;
            Vector(Luminosity, Luminosity * 0.6f, Luminosity * 0.2f, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
        }
        o->HiddenMesh = -2;
        break;

    case 53:
        if (IsBattleCastleStart())
        {
            Luminosity = (float)(WorldRandom() % 4 + 3) * 0.1f;
            Vector(Luminosity, Luminosity * 0.6f, Luminosity * 0.2f, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
        }
        o->HiddenMesh = -2;
        break;

    case 66:
        o->HiddenMesh = -2;
        if (IsBattleCastleStart())
        {
            if (g_isCrownState == false)
            {
                o->HiddenMesh = -1;
            }
            o->BlendMeshTexCoordV = WorldTime * 0.001f;
        }
        break;

    case BATTLE_CASTLE_WALL1:
    case BATTLE_CASTLE_WALL2:
    case BATTLE_CASTLE_WALL3:
    case BATTLE_CASTLE_WALL4:
        break;

    case MODEL_BATTLE_GUARD2:
        if (IsBattleCastleStart())
        {
            o->HiddenMesh = -2;
        }
        else
        {
            o->HiddenMesh = -1;
        }
        break;
    }
    return true;
}

bool CGMBattleCastle::AdvanceBattleCastleVisual(OBJECT *o, BMD *b)
{
    if (gMapManager.InBattleCastle() && o->Type >= BATTLE_CASTLE_WALL1 &&
        o->Type <= BATTLE_CASTLE_WALL4 && o->SubType != 0)
    {
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            PrepareWorldObjectPose(*o, birthTime.FrameFraction());
            OBB_t bounds{};
            b->Transform(BoneTransform, o->BoundingBoxMin, o->BoundingBoxMax, &bounds, false);
            EmitMeshEffects(*b, 0, MODEL_WALL_PART1);
        }
    }

    if (gMapManager.InBattleCastle() == false)
        return false;

    vec3_t Light;

    switch (o->Type)
    {
    case 0:
        if (IsBattleCastleStart() == false)
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
            {
                Vector(1.f, 1.f, 1.f, Light);
                CreateParticle(BITMAP_TRUE_FIRE, o->Position, o->Angle, Light, 0, o->Scale);
            }
        break;

    case 41:
        o->BlendMeshLight = sinf(WorldTime * 0.002f) * 0.3f + 0.7f;
        break;

    case 42:
        if (IsBattleCastleStart())
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
            {
                Vector(1.f, 1.f, 1.f, Light);
                CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 21, o->Scale);
            }
        break;
    case 52:
        if (IsBattleCastleStart() == false)
        {
            Vector(1.f, 1.f, 1.f, Light);
            CreateParticleFpsChecked(BITMAP_WATERFALL_5, o->Position, o->Angle, Light, 6, o->Scale);
        }
        break;
    case 53:
        if (IsBattleCastleStart())
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
            {
                Vector(1.f, 1.f, 1.f, Light);
                CreateParticle(BITMAP_TRUE_FIRE, o->Position, o->Angle, Light, 0, o->Scale);
            }
        break;
    case 54:
        if (IsBattleCastleStart() == false)
        {
            Vector(1.f, 1.f, 1.f, Light);
            for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
                CreateParticle(BITMAP_WATERFALL_3 + (WorldRandom() % 2), o->Position, o->Angle,
                               Light, 0);
        }
        break;
    }

    return true;
}

void CGMBattleCastle::MoveFlyBigStone(OBJECT *o)
{
}

bool CGMBattleCastle::AttackEffect_BattleCastleMonster(CHARACTER *c, OBJECT *o, BMD *b)
{
    if (gMapManager.InBattleCastle() == false)
        return false;

    CHARACTER *tc = NULL;
    OBJECT *to = NULL;
    vec3_t Light;
    vec3_t p, Position;

    Vector(0.f, 0.f, 0.f, p);
    Vector(1.f, 1.f, 1.f, Light);
    if (CharactersClient.IsValidIndex(c->TargetCharacter))
    {
        tc = &CharactersClient[c->TargetCharacter];
        to = &tc->Object;
    }

    switch (c->MonsterIndex)
    {
    case MONSTER_TRAP:
        if (c->CheckAttackTime(5))
        {
            VectorCopy(o->Position, Position);
            Position[2] += 500.f;
            CreateEffect(MODEL_BATTLE_GUARD2, Position, o->Angle, o->Light, 0);
            c->SetLastAttackEffectTime();
        }
        return true;

    case MONSTER_CANON_TOWER:
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            CreateEffect(BITMAP_JOINT_FORCE, o->Position, o->Angle, o->Light);
        }
        return true;
    }
    return false;
}

void CGMBattleCastle::CreateGuardStoneHealingVisual(CHARACTER *c, float Range)
{
    OBJECT *o = &c->Object;
    bool bHealing = false;

    if (g_isCharacterBuff(o, eBuff_CastleRegimentDefense))
    {
        for (int i = 0; i < 4; ++i)
        {
            if (InArea(g_fGuardStoneLocation[i][0], g_fGuardStoneLocation[i][1], o->Position,
                       Range))
            {
                bHealing = true;
                break;
            }
        }
    }
    else //if ( (o->State&STATE_REGIMENT_ATTACK)==STATE_REGIMENT_ATTACK )
    {
        if (g_fLifeStoneLocation[0] != 0.f && g_fLifeStoneLocation[1] != 0.f &&
            Hero->GuildMarkIndex == c->GuildMarkIndex)
        {
            if (InArea(g_fLifeStoneLocation[0], g_fLifeStoneLocation[1], o->Position, Range))
            {
                bHealing = true;
            }
        }
    }

    if (bHealing && LastHealingParticle < WorldTime - HealingParticleInterval)
    {
        LastHealingParticle = WorldTime;
        CreateParticle(BITMAP_PLUS, o->Position, o->Angle, o->Light);
    }
}

bool CGMBattleCastle::MoveBattleCastleMonsterVisual(OBJECT *o, BMD *b,
                                                    WorldCharacterVisualState &visual)
{
    if (gMapManager.InBattleCastle() == false)
        return false;

    float Luminosity;
    vec3_t Light;

    switch (o->Type)
    {
    case 11:
        //            { visual.movement.materialFields |= CharacterMovementVisual::HiddenMesh; visual.movement.hiddenMesh = -2; }
        break;

    case MODEL_NPC_CROWN: {
        visual.movement.materialFields |= CharacterMovementVisual::HiddenMesh;
        visual.movement.hiddenMesh = -1;
    }
        if (IsBattleCastleStart())
        {
            if (g_isCrownState == false)
            {
                {
                    visual.movement.materialFields |= CharacterMovementVisual::HiddenMesh;
                    visual.movement.hiddenMesh = -2;
                }
            }
        }
        break;

    case MODEL_CASTLE_GATE1:

        return true;

    case MODEL_GUARDIAN_STATUE:

        Luminosity = sinf(WorldTime * 0.0005) * 0.58f + 0.42f;
        Vector(Luminosity * 0.2f, Luminosity * 0.7f, Luminosity * 1.f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
        return true;

    case MODEL_GREAT_DRAKAN:

        return true;

    case MODEL_BATTLE_GUARD1:

        return true;

    case MODEL_BATTLE_GUARD2:

        return true;

    case MODEL_GOLDEN_GOBLIN:

        return true;

    case MODEL_CANON_TOWER:

        return true;
    }

    return false;
}

bool CGMBattleCastle::AdvanceBattleCastleMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                                       WorldCharacterVisualState &visual)
{
    EmitStructureDeath(o, b, visual);

    if (gMapManager.InBattleCastle() == false)
        return false;

    vec3_t Position, Light, p;

    switch (o->Type)
    {
    case MODEL_CASTLE_GATE1:
        break;

    case MODEL_GUARDIAN_STATUE:
        if (IsBattleCastleStart())
        {
            Vector(0.3f, 0.2f, 0.f, Light);
        }
        else
        {
        }
        return true;

    case MODEL_LIFE_STONE:
        if (o->m_byBuildTime >= 5)
        {

            Vector(0.3f, 0.2f, 0.f, Light);
        }
        else
        {
            Vector(0.8f, 0.6f, 0.3f, Light);
            VectorCopy(o->Position, Position);
            Position[2] += 50.f;
            CreateSprite(BITMAP_LIGHT, Position, 4.f, Light, NULL);
            CreateSprite(BITMAP_SHINY + 1, Position, 2.5f, Light, NULL, WorldRandom() % 360);
        }
        return true;

    case MODEL_CANON_TOWER:
        if (IsBattleCastleStart())
        {

            Vector(0.f, 0.f, 0.f, p);
            Vector(0.8f, 0.6f, 0.3f, Light);
            b->TransformPosition(o->BoneTransform[1], p, Position, true);

            Position[2] += sinf(WorldTime * 0.001f) * 60.f + 60.f;

            CreateSprite(BITMAP_LIGHT, Position, 3.f, Light, NULL);
            CreateSprite(BITMAP_SHINY + 1, Position, 1.5f, Light, NULL, WorldRandom() % 360);

            b->TransformPosition(o->BoneTransform[2], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 4.f, Light, NULL);
        }
        else
        {
        }
        return true;

    case MODEL_NPC_CHECK_FLOOR:
        if (IsBattleCastleStart())
        {
        }
        else
        {
        }

        return true;
    }

    return false;
}

void CGMBattleCastle::EmitMonsterHitEffect(OBJECT *o)
{
    if (gMapManager.InBattleCastle() == false)
        return;

    switch (o->Type)
    {
    case MODEL_CASTLE_GATE1: {
        vec3_t Position;
        VectorCopy(o->Position, Position);

        Position[0] += 60.f;
        Position[1] -= 100.f;
        Position[2] += 150.f;

        if (!g_isCharacterBuff(o, eBuff_CastleGateIsOpen))
        {
            if (!g_isCharacterBuff((&Hero->Object), eBuff_BlessPotion) &&
                !g_isCharacterBuff((&Hero->Object), eBuff_SoulPotion))
            {
                CreateEffect(MODEL_WAVES, Position, o->Angle, o->Light, 6, NULL, 0);
            }

            for (int i = 0; i < 5; ++i)
            {
                Position[0] = o->Position[0];
                Position[1] = o->Position[1];
                Position[2] = o->Position[2] + 200 + WorldRandom() % 30;

                CreateEffect(MODEL_GATE_PART1 + WorldRandom() % 3, Position, o->Angle, o->Light,
                             WorldRandom() % 2 + 1);
            }
        }
    }
    break;

    case MODEL_GUARDIAN_STATUE: {
        vec3_t Position;
        for (int i = 0; i < 5; i++)
        {
            if (sessionKeeper_.Random()->FpsCheck(2, 1.f))
            {
                Position[0] = o->Position[0];
                Position[1] = o->Position[1];
                Position[2] = o->Position[2] + 200 + WorldRandom() % 30;

                CreateEffect(MODEL_STONE_COFFIN + 1, Position, o->Angle, o->Light);
            }
        }
    }
    break;
    }
}
void CGMBattleCastle::AdvanceStructurePresentationState(CHARACTER &character)
{
    if (!gMapManager.InBattleCastle())
        return;
    auto &object = character.Object;
    if (object.Type == MODEL_LIFE_STONE && object.m_byBuildTime >= 5)
        object.BlendMeshLight = sinf(WorldTime * 0.001f) * 0.3f + 0.3f;
    if (object.Type == MODEL_CANON_TOWER)
    {
        character.m_bIsSelected = IsBattleCastleStart();
        if (!character.m_bIsSelected)
            object.HiddenMesh = -2;
    }
    if (object.Type == MODEL_NPC_CHECK_FLOOR)
    {
        character.m_bIsSelected = IsBattleCastleStart();
        object.Position[2] = object.Velocity - (character.m_bIsSelected ? 0.f : 100.f);
        object.BlendMeshLight = sinf(WorldTime * 0.001f) * 0.3f + 0.5f;
    }
}

void CGMBattleCastle::EmitStructureDeath(OBJECT *object, BMD *model,
                                         WorldCharacterVisualState &visual)
{
    if (!gMapManager.InBattleCastle() || visual.action != MONSTER01_DIE || visual.deathEmitted)
        return;
    const bool gate = object->Type == MODEL_CASTLE_GATE1;
    const bool statue = object->Type == MODEL_GUARDIAN_STATUE && IsBattleCastleStart();
    if (!gate && !statue)
        return;
    visual.deathEmitted = true;
    PrepareWorldObjectPose(*object);
    model->Transform(BoneTransform, object->BoundingBoxMin, object->BoundingBoxMax, &object->OBB,
                     false);
    PlayBuffer(gate ? SOUND_HIT_GATE2 : SOUND_BC_GUARD_STONE_DIS);
    EmitMeshEffects(*model, 0, gate ? MODEL_GATE_PART1 : MODEL_STONE_COFFIN);
}

bool CGMBattleCastle::AdvanceObjectVisual(OBJECT *object, BMD *model, float)
{
    return AdvanceBattleCastleVisual(object, model);
}

bool CGMBattleCastle::AttackEffectBeforeShared(CHARACTER *character, OBJECT *object, BMD *model)
{
    return AttackEffect_BattleCastleMonster(character, object, model);
}

bool CGMBattleCastle::MoveMonsterVisual(CHARACTER *, OBJECT *object, BMD *model,
                                        WorldCharacterVisualState &visual)
{
    return MoveBattleCastleMonsterVisual(object, model, visual);
}

bool CGMBattleCastle::AdvanceMonsterVisual(CHARACTER *character, OBJECT *object, BMD *model,
                                           WorldCharacterVisualState &visual)
{
    return AdvanceBattleCastleMonsterVisual(character, object, model, visual);
}

bool CGMBattleCastle::CreateWeather(PARTICLE *particle, int)
{
    return CreateFireSnuff(particle);
}

int CGMBattleCastle::PrepareWeather()
{
    return 40;
}

void CGMBattleCastle::PrepareObjectEffects(int &count, int previousVisible)
{
    MoveBattleCastleObjectSetting(count, previousVisible);
}

void CGMBattleCastle::MoveObjectEffects(OBJECT *object, int &count, int &visible)
{
    MoveBattleCastleObject(object, count, visible);
}

void CGMBattleCastle::MoveCharacterState(CHARACTER *character, OBJECT *object)
{
    MoveBattleCastleMonster(character, object);
    switch (object->Type)
    {
    case MODEL_CASTLE_GATE1:
    case MODEL_GUARDIAN_STATUE:
    case MODEL_GREAT_DRAKAN:
    case MODEL_BATTLE_GUARD1:
    case MODEL_BATTLE_GUARD2:
    case MODEL_GOLDEN_GOBLIN:
        object->Angle[2] = 0.f;
        break;
    case MODEL_CANON_TOWER:
        object->Angle[2] = 45.f;
        break;
    }
}

#define NUM_HELLAS 7

#define KUNDUN_ZONE NUM_HELLAS

void CGMHellas::MoveWaterTerrain(void)
{
    if (g_pCSWaterTerrain != nullptr)
    {
        g_pCSWaterTerrain->Update();
    }
}

// every 4 seconds.

// every 2 seconds.

bool CGMHellas::MoveHellasVisual(OBJECT *o)
{
    if (gMapManager.InHellas() == false)
        return false;

    switch (o->Type)
    {
    case 37:
    case 38:
    case 39:
    case 40:
        o->HiddenMesh = -2;
        break;
    }
    return true;
}

bool CGMHellas::AdvanceHellasVisual(OBJECT *o, BMD *b)
{
    if (gMapManager.InHellas() == false)
        return false;

    vec3_t p, Position;
    vec3_t Light;
    float Luminosity = 0.f;

    switch (o->Type)
    {
    case 12:
        PrepareWorldObjectPose(*o);
        Luminosity = sinf(WorldTime * 0.001f) * 0.3f + 0.7f;

        Vector(0.6f, 0.6f, 1.f, Light);
        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(BoneTransform[5], p, Position);
        CreateSprite(BITMAP_LIGHT, Position, Luminosity + 0.2f, Light, o);
        break;
    case 15:
    case 29:
        CheckGrass(o);
        o->Position[2] = GetWaterTerrain(o->Position[0], o->Position[1]) + 180;
        break;
    case 32:
        PrepareWorldObjectPose(*o);
        CheckGrass(o);
        Luminosity = sinf(WorldTime * 0.001f) * 0.3f + 0.7f;

        Vector(0.6f, 0.6f, 1.f, Light);
        Vector(0.f, 0.f, 0.f, p);
        b->TransformPosition(BoneTransform[5], p, Position);
        CreateSprite(BITMAP_LIGHT, Position, Luminosity + 0.2f, Light, o);
        o->Position[2] = GetWaterTerrain(o->Position[0], o->Position[1]) + 180;
        break;
    case 35:
        Vector(0.3f, 0.6f, 1.f, Light);
        CreateParticleFpsChecked(BITMAP_LIGHT, o->Position, o->Angle, Light, 6, 1.f, o);
        o->HiddenMesh = -2;
        break;
    case 36:
        Vector(1.f, 1.f, 1.f, Light);
        CreateParticleFpsChecked(BITMAP_TRUE_BLUE, o->Position, o->Angle, Light, 0);

        o->Scale = 0.5f;
        o->HiddenMesh = -2;
        break;

    case 37:
        Vector(1.f, 1.f, 1.f, Light);
        CreateParticleFpsChecked(BITMAP_WATERFALL_5, o->Position, o->Angle, Light, 0);
        o->Scale = 0.5f;
        PlayBuffer(SOUND_KALIMA_WATER_FALL);
        break;
    case 38:
        Vector(1.f, 1.f, 1.f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
        {
            CreateParticle(BITMAP_WATERFALL_1, o->Position, o->Angle, Light, 0);
        }
        PlayBuffer(SOUND_KALIMA_WATER_FALL);
        o->Scale = 0.5f;
        break;
    case 39:
        Vector(1.f, 1.f, 1.f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            CreateParticle(BITMAP_WATERFALL_3 + (WorldRandom() % 2), o->Position, o->Angle, Light,
                           0);
        o->Scale = 0.5f;
        break;
    case 40:
        Vector(1.f, 1.f, 1.f, Light);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.f))
        {
            CreateParticle(BITMAP_WATERFALL_2, o->Position, o->Angle, Light, 0);
        }
        o->Scale = 0.5f;
        break;
    }

    return true;
}

// every 4 seconds.

float CGMHellas::MoveBigMon(OBJECT *o, float frames, bool refresh, float startingLife)
{
    if (frames <= 0.f)
        return o->Velocity;
    if (refresh)
    {
        o->AmbientTurnRate = o->Gravity;
        if (WorldRandom() % 5 == 0)
            o->Gravity *= -1;
    }
    o->Angle[2] += o->AmbientTurnRate * frames;

    // The authored <20 test first accelerates the tick starting at integer age 19.
    constexpr float AcceleratingLife = 19.f, Acceleration = 0.5f;
    const float lateFrames = std::clamp(AcceleratingLife - startingLife + frames, 0.f, frames);
    const float meanVelocity =
        o->Velocity + Acceleration * lateFrames * (lateFrames + 1.f) / (2.f * frames);
    o->Alpha *= std::pow(1.f / 1.2f, lateFrames);
    o->Velocity += Acceleration * lateFrames;
    o->Angle[0] += 2.f * lateFrames;
    if (startingLife < 0.f)
        o->Live = false;
    return meanVelocity;
}

bool CGMHellas::AttackEffect_HellasMonster(CHARACTER *c, CHARACTER *tc, OBJECT *o, OBJECT *to,
                                           BMD *b)
{
    vec3_t Light;
    vec3_t p, Position;
    Vector(0.f, 0.f, 0.f, p);
    Vector(1.f, 1.f, 1.f, Light);
    switch (c->MonsterIndex)
    {
    case MONSTER_DEATH_ANGEL_1:
    case MONSTER_DEATH_ANGEL_2:
    case MONSTER_DEATH_ANGEL_3:
    case MONSTER_DEATH_ANGEL_4:
    case MONSTER_DEATH_ANGEL_5:
    case MONSTER_DEATH_ANGEL_6:
    case MONSTER_DEATH_ANGEL_7:
        if (c->CheckAttackTime(14))
        {
            Vector(1.f, 1.f, 1.f, Light);

            if (to != NULL)
            {
                VectorCopy(to->Position, Position);
            }
            else
            {
                VectorCopy(Hero->Object.Position, Position);
            }
            Position[2] += 150.f;
            CreateParticle(BITMAP_SHINY + 4, Position, o->Angle, Light, 1, 1.f);
            c->SetLastAttackEffectTime();
        }
        return true;

    case MONSTER_DEATH_CENTURION_1:
    case MONSTER_DEATH_CENTURION_2:
    case MONSTER_DEATH_CENTURION_3:
    case MONSTER_DEATH_CENTURION_4:
    case MONSTER_DEATH_CENTURION_5:
    case MONSTER_DEATH_CENTURION_6:
        switch ((c->Skill))
        {
        case AT_SKILL_FIRE_SLASH:
        case AT_SKILL_FIRE_SLASH_STR:
            CreateMonsterSkill_ReduceDef(o, c->AttackTime, 13, 200.f);
            break;

        case AT_SKILL_POISON:
        case AT_SKILL_POISON_STR:
            CreateMonsterSkill_Poison(o, c->AttackTime, 13);
            break;

        case AT_SKILL_MONSTER_SUMMON:
            CreateMonsterSkill_Summon(o, c->AttackTime, 13);
            break;

        case AT_SKILL_MONSTER_MAGIC_DEF:
            if ((int)c->AttackTime >= 13)
            {
                g_CharacterRegisterBuff(o, eBuff_WizDefense);
                c->AttackTime = 15;
                PlayBuffer(SOUND_GREAT_SHIELD);
            }
            break;

        case AT_SKILL_MONSTER_PHY_DEF:
            if ((int)c->AttackTime >= 13)
            {
                g_CharacterRegisterBuff(o, eBuff_Defense);
                c->AttackTime = 15;
                PlayBuffer(SOUND_GREAT_SHIELD);
            }
            break;

        default:
            break;
        }
        if (o->CurrentAction == MONSTER01_ATTACK2 && c->AttackTime == 14)
        {
            CreateEffect(MODEL_SKILL_FURY_STRIKE, o->Position, o->Angle, o->Light, 0, o, -1, 0, 1);
            CreateEffect(BITMAP_JOINT_THUNDER + 1, o->Position, o->Angle, o->Light, 0, o, -1, 0, 1);
            c->AttackTime = 15;
        }
        return true;

    case MONSTER_BLOOD_SOLDIER_1:
    case MONSTER_BLOOD_SOLDIER_2:
    case MONSTER_BLOOD_SOLDIER_3:
    case MONSTER_BLOOD_SOLDIER_4:
    case MONSTER_BLOOD_SOLDIER_5:
    case MONSTER_BLOOD_SOLDIER_6:
    case MONSTER_BLOOD_SOLDIER_7:
        if (c->CheckAttackTime(14))
        {
            Vector(1.f, 1.f, 1.f, Light);

            if (to != NULL)
            {
                VectorCopy(to->Position, Position);
            }
            else
            {
                VectorCopy(Hero->Object.Position, Position);
            }
            Position[2] += 150.f;
            CreateParticle(BITMAP_SHINY + 4, Position, o->Angle, Light, 1, 1.f);
            c->SetLastAttackEffectTime();
        }
        return true;

    case MONSTER_AEGIS_1:
    case MONSTER_AEGIS_2:
    case MONSTER_AEGIS_3:
    case MONSTER_AEGIS_4:
    case MONSTER_AEGIS_5:
    case MONSTER_AEGIS_6:
    case MONSTER_AEGIS_7:
        if (o->CurrentAction == MONSTER01_ATTACK2 && c->AttackTime == 14)
        {
            CreateEffect(MODEL_WATER_WAVE, o->Position, o->Angle, o->Light);
        }
        return true;

    case MONSTER_ROGUE_CENTURION_1:
    case MONSTER_ROGUE_CENTURION_2:
    case MONSTER_ROGUE_CENTURION_3:
    case MONSTER_ROGUE_CENTURION_4:
    case MONSTER_ROGUE_CENTURION_5:
    case MONSTER_ROGUE_CENTURION_6:
    case MONSTER_ROGUE_CENTURION_7:
        switch ((c->Skill))
        {
        case AT_SKILL_ENERGYBALL:
            if (c->CheckAttackTime(14))
            {
                CreateEffect(MODEL_SKILL_FURY_STRIKE, o->Position, o->Angle, o->Light, 1, o, -1, 0,
                             1);
                c->SetLastAttackEffectTime();
            }
            break;

        default:
            break;
        }
        return true;

    case MONSTER_NECRON_1:
    case MONSTER_NECRON_2:
    case MONSTER_NECRON_3:
    case MONSTER_NECRON_4:
    case MONSTER_NECRON_5:
    case MONSTER_NECRON_6:
    case MONSTER_NECRON_7:
        switch ((c->Skill))
        {
        case AT_SKILL_POISON:
        case AT_SKILL_POISON_STR:
            if (c->CheckAttackTime(14))
            {
                vec3_t Light, Position;

                Vector(0.8f, 0.5f, 0.1f, Light);

                for (int i = 0; i < 3; ++i)
                {
                    Position[0] = Hero->Object.Position[0] + (WorldRandom() % 200 - 100);
                    Position[1] = Hero->Object.Position[1] + (WorldRandom() % 200 - 100);
                    Position[2] = Hero->Object.Position[2];

                    CreateEffect(MODEL_FIRE, Position, o->Angle, Light, 7, NULL, 0);
                }
                c->SetLastAttackEffectTime();
            }
            break;

        case AT_SKILL_ENERGYBALL:
            if (c->CheckAttackTime(14))
            {
                if (CharactersClient.IsValidIndex(c->TargetCharacter))
                {
                    CHARACTER *tc = &CharactersClient[c->TargetCharacter];
                    OBJECT *to = &tc->Object;

                    vec3_t Angle;
                    Vector(0.f, 0.f, 0.f, p);
                    VectorCopy(o->Angle, Angle);
                    b->TransformPosition(o->BoneTransform[60], p, Position, true);

                    CreateJoint(BITMAP_FLARE + 1, Position, to->Position, Angle, 6, to, 30.f, 50);
                    Angle[2] -= 30.f;
                    CreateJoint(BITMAP_FLARE + 1, Position, to->Position, Angle, 6, to, 30.f, 50);
                    Angle[2] += 15.f;
                    CreateJoint(BITMAP_FLARE + 1, Position, to->Position, Angle, 6, to, 30.f, 50);
                }

                c->SetLastAttackEffectTime();
            }
            break;
        }
        return true;

    case MONSTER_SCHRIKER_1:
    case MONSTER_SCHRIKER_2:
    case MONSTER_SCHRIKER_3:
    case MONSTER_SCHRIKER_4:
    case MONSTER_SCHRIKER_5:
    case MONSTER_SCHRIKER_6:
        if (o->CurrentAction == MONSTER01_ATTACK1 && c->AttackTime >= 13)
        {
            CreateEffect(MODEL_SKILL_INFERNO, o->Position, o->Angle, o->Light, 5, o);
            CreateEffect(BITMAP_FLAME, o->Position, o->Angle, o->Light, 2, o);
            c->AttackTime = 15;
        }
        return true;

    case MONSTER_ILLUSION_OF_KUNDUN_1:
    case MONSTER_ILLUSION_OF_KUNDUN_2:
    case MONSTER_ILLUSION_OF_KUNDUN_3:
    case MONSTER_ILLUSION_OF_KUNDUN_4:
    case MONSTER_ILLUSION_OF_KUNDUN_5:
        switch ((c->Skill))
        {
        case AT_SKILL_FIRE_SLASH:
        case AT_SKILL_FIRE_SLASH_STR:
            CreateMonsterSkill_ReduceDef(o, c->AttackTime, 13, 200.f);
            break;

        case AT_SKILL_POISON:
        case AT_SKILL_POISON_STR:
            CreateMonsterSkill_Poison(o, c->AttackTime, 13);
            break;

        case AT_SKILL_MONSTER_SUMMON:
            CreateMonsterSkill_Summon(o, c->AttackTime, 13);
            break;

        case AT_SKILL_MONSTER_MAGIC_DEF:
            if ((int)c->AttackTime >= 13)
            {
                g_CharacterRegisterBuff(o, eBuff_WizDefense);
                c->AttackTime = 15;
                PlayBuffer(SOUND_GREAT_SHIELD);
            }
            break;

        case AT_SKILL_MONSTER_PHY_DEF:
            if ((int)c->AttackTime >= 13)
            {
                g_CharacterRegisterBuff(o, eBuff_Defense);
                c->AttackTime = 15;
                PlayBuffer(SOUND_GREAT_SHIELD);
            }
            break;

        default:
            break;
        }

        if (o->CurrentAction == MONSTER01_ATTACK1)
        {
            if (c->CheckAttackTime(7))
            {
                CreateEffect(MODEL_SKILL_FURY_STRIKE, o->Position, o->Angle, o->Light, 0, o, -1, 0,
                             0);
                c->SetLastAttackEffectTime();
            }
            else if (c->CheckAttackTime(13))
            {
                CreateEffect(MODEL_SKILL_INFERNO, o->Position, o->Angle, o->Light, 0, o);
                CreateEffect(BITMAP_FLAME, o->Position, o->Angle, o->Light, 1, o);
                c->SetLastAttackEffectTime();
            }
        }
        return true;
    case MONSTER_ILLUSION_OF_KUNDUN_7:
        switch ((c->Skill))
        {
        case AT_SKILL_FIRE_SLASH:
        case AT_SKILL_FIRE_SLASH_STR:
            CreateMonsterSkill_ReduceDef(o, c->AttackTime, 13, 200.f);
            break;

        case AT_SKILL_POISON:
        case AT_SKILL_POISON_STR:
            CreateMonsterSkill_Poison(o, c->AttackTime, 13);
            break;

        case AT_SKILL_MONSTER_SUMMON:
            CreateMonsterSkill_Summon(o, c->AttackTime, 13);
            break;

        case AT_SKILL_MONSTER_MAGIC_DEF:
            if ((int)c->AttackTime >= 13)
            {
                g_CharacterRegisterBuff(o, eBuff_WizDefense);
                c->AttackTime = 15;

                PlayBuffer(SOUND_GREAT_SHIELD);
            }
            break;

        case AT_SKILL_MONSTER_PHY_DEF:
            if ((int)c->AttackTime >= 13)
            {
                g_CharacterRegisterBuff(o, eBuff_Defense);
                c->AttackTime = 15;

                PlayBuffer(SOUND_GREAT_SHIELD);
            }
            break;

        default:
            break;
        }
        return true;
    }
    return false;
}

bool CGMHellas::MoveHellasMonsterVisual(OBJECT *o, BMD *b, WorldCharacterVisualState &visual)
{
    ObjectDrawInput presentation(o);
    visual.movement.Apply(presentation);
    vec3_t Position, p;
    vec3_t Light;

    switch (o->Type)
    {
    case MODEL_WARCRAFT:
        if (visual.action == 0)
        {
            Position[0] = o->Position[0] + WorldRandom() % 200 - 100;
            Position[1] = o->Position[1] + WorldRandom() % 100 - 50;
            Position[2] = o->Position[2];

            CreateParticleFpsChecked(BITMAP_SMOKE + 1, Position, o->Angle, visual.movement.light);
            CreateParticleFpsChecked(BITMAP_SMOKE + 1, Position, o->Angle, visual.movement.light);
            CreateEffectFpsChecked(MODEL_STONE1, o->Position, o->Angle, visual.movement.light);
            CreateEffectFpsChecked(MODEL_STONE2, o->Position, o->Angle, visual.movement.light);
        }
        {
            visual.movement.materialFields |= CharacterMovementVisual::Mesh;
            visual.movement.blendMesh = 1;
        }
        {
            visual.movement.materialFields |= CharacterMovementVisual::Brightness;
            visual.movement.blendLight = sinf(WorldTime * 0.001f) * 0.5f + 0.5f;
        }
        return true;

    case MODEL_DEATH_ANGEL: {
        visual.movement.materialFields |= CharacterMovementVisual::Brightness;
        visual.movement.blendLight = sinf(WorldTime * 0.001f) * 0.7f + 0.3f;
    }
        return true;

    case MODEL_ILLUSION_OF_KUNDUN:
        for (int births = sessionKeeper_.Random()->EmissionCount(FPS_ANIMATION_FACTOR / 2.0);
             births > 0; --births)
        {
            Vector(2.f, 30.f, 0.f, p);
            b->TransformPosition(presentation.bones[6], p, Position, true);
            Vector(1.0f, 0.2f, 0.0f, Light);
            CreateParticle(BITMAP_SMOKE, Position, o->Angle, Light, 17);
        }
        return true;

    case MODEL_BLOOD_SOLDIER:
        return true;

    case MODEL_AEGIS:
        return true;

    case MODEL_DEATH_CENTURION:
        return true;

    case MODEL_NECRON: {
        visual.movement.materialFields |= CharacterMovementVisual::Brightness;
        visual.movement.blendLight = sinf(WorldTime * 0.001f) * 0.7f + 0.3f;
    }
        return true;

    case MODEL_SHRIKER:
        if (visual.action != MONSTER01_DIE)
        {
            for (int births = sessionKeeper_.Random()->EmissionCount(FPS_ANIMATION_FACTOR / 2.0);
                 births > 0; --births)
            {
                Vector(2.f, 30.f, 0.f, p);
                b->TransformPosition(presentation.bones[31], p, Position, true);
                if (visual.movement.subType == 9)
                {
                    Vector(1.0f, 0.0f, 0.0f, Light);
                }
                else
                {
                    Vector(0.0f, 0.3f, 1.0f, Light);
                }
                CreateParticle(BITMAP_SMOKE, Position, o->Angle, Light, 17);
            }
        }
        return true;
    }

    return false;
}

void CGMHellas::EmitKundunEvents(OBJECT &object, BMD &model)
{
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    constexpr std::array<std::pair<int, float>, 6> markers{{{MONSTER01_SHOCK, 0.f},
                                                            {MONSTER01_SHOCK, 4.f},
                                                            {MONSTER01_ATTACK1, 3.f},
                                                            {MONSTER01_ATTACK2, 5.f},
                                                            {MONSTER01_ATTACK2, 6.f},
                                                            {MONSTER01_ATTACK2, 9.f}}};
    AnimationPoseSample pose(&object, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    object.MotionTrace.VisitAnimationEvents(
        WorldTime, markers, [&](std::size_t event, float fraction) {
            auto birth =
                sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR * (1.f - fraction));
            if (event == 0)
            {
                PlayBuffer(SOUND_KUNDUN_ROAR);
                return;
            }
            vec3_t position, local{}, angle;
            object.MotionTrace.Sample(WorldTime, fraction, object.Position, position);
            if ((event == 1 || event == 4 || event == 5) && gMapManager.InHellas())
                AddWaterWave(static_cast<int>(position[0] / TERRAIN_SCALE),
                             static_cast<int>(position[1] / TERRAIN_SCALE), 2, 2000);
            if (event == 1 || event == 5)
                return;
            vec3_t bone;
            pose.SampleBonePosition(model, object, 49, local, WorldTime, fraction, bone);
            VectorCopy(object.Angle, angle);
            angle[2] = object.MotionTrace.SampleYaw(WorldTime, fraction, angle[2]);
            if (event == 2)
            {
                position[2] = bone[2];
                CreateEffect(MODEL_CUNDUN_SKILL, position, angle, object.Light, 0);
            }
            else if (event == 3)
            {
                bone[2] = 400.f;
                CreateEffect(MODEL_CUNDUN_SKILL, bone, angle, object.Light, 1);
            }
            else
            {
                bone[2] = 400.f;
                Vector(0.f, 0.f, 0.f, angle);
                for (int child = 0; child < 24; ++child)
                {
                    angle[2] = child * 30.f;
                    CreateJoint(BITMAP_JOINT_SPIRIT2, bone, bone, angle, 14, nullptr, 100.f, 0, 0);
                }
            }
        });
}

bool CGMHellas::AdvanceHellasMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                           WorldCharacterVisualState &visual)
{
    EmitMonsterMeshEffects(o, b, visual);
    if (o->Type == MODEL_ILLUSION_OF_KUNDUN)
        EmitKundunDeath(o, b, visual);

    if (o->Type == MODEL_ILLUSION_OF_KUNDUN)
        EmitKundunEvents(*o, *b);

    vec3_t Position, Light, p;
    float Luminosity;
    int i;

    switch (o->Type)
    {
    case MODEL_WARCRAFT:
        if (visual.action == 1)
        {
            float remaining = FPS_ANIMATION_FACTOR;
            while (remaining > 0.f)
            {
                const float step =
                    Core::Time::ReferenceStep(remaining, visual.warcraftEmissionFrames);
                visual.warcraftEmissionFrames -= step;
                remaining -= step;
                if (visual.warcraftEmissionFrames > 0.f)
                    continue;
                visual.warcraftEmissionFrames = 5.f;
                auto birthTime = sessionKeeper_.Gameplay()->EmissionTime(remaining);
                const float fraction = birthTime.FrameFraction();
                AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false,
                                         b->PoseAssetIdentity());
                Vector(0.f, 0.f, 0.f, p);
                pose.SampleBonePosition(*b, *o, 8, p, WorldTime, fraction, Position);
                Vector(1.f, 0.1f, 0.1f, Light);
                CreateParticle(BITMAP_HOLE, Position, o->Angle, Light, 0, 3.f);
            }
        }
        return true;

    case MODEL_DEATH_ANGEL:
        Vector(0.f, 0.f, 0.f, p);

        if (visual.action != MONSTER01_DIE)
        {
            Luminosity = 0.f;
            for (i = 0; i < 5; ++i)
            {
                Luminosity += 1 / 4.f;
                Vector(Luminosity * 0.1f, Luminosity * 0.4f, Luminosity, Light);

                b->TransformPosition(o->BoneTransform[i + 74], p, Position, true);
                CreateSprite(BITMAP_LIGHT, Position, 1.5f - (0.2f * i), Light, o, WorldTime);
                CreateSprite(BITMAP_LIGHT, Position, 1.5f - (0.2f * i), Light, o, WorldTime);
                for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
                {
                    vec3_t birthPosition;
                    AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false,
                                             b->PoseAssetIdentity());
                    pose.SampleBonePosition(*b, *o, i + 74, p, WorldTime, birth.FrameFraction(),
                                            birthPosition);
                    CreateParticle(BITMAP_BUBBLE, birthPosition, o->Angle, Light, 1);
                }

                b->TransformPosition(o->BoneTransform[i + 62], p, Position, true);
                CreateSprite(BITMAP_LIGHT, Position, 1.5f - (0.2f * i), Light, o, WorldTime);
                CreateSprite(BITMAP_LIGHT, Position, 1.5f - (0.2f * i), Light, o, WorldTime);
                for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
                {
                    vec3_t birthPosition;
                    AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false,
                                             b->PoseAssetIdentity());
                    pose.SampleBonePosition(*b, *o, i + 62, p, WorldTime, birth.FrameFraction(),
                                            birthPosition);
                    CreateParticle(BITMAP_BUBBLE, birthPosition, o->Angle, Light, 1);
                }
            }

            Vector(2.f, 2.f, 0.f, p);
            Vector(o->BlendMeshLight, o->BlendMeshLight, o->BlendMeshLight, Light);
            b->TransformPosition(o->BoneTransform[8], p, Position, true);
            CreateSprite(BITMAP_ENERGY, Position, 0.2f, Light, o, 0.f);
            Vector(o->BlendMeshLight * 0.1f, o->BlendMeshLight * 0.1f, o->BlendMeshLight, Light);
            CreateSprite(BITMAP_SHINY + 1, Position, 1.f, Light, o, 0.f);

            Vector(0.f, 0.f, 0.f, p);
            b->TransformPosition(o->BoneTransform[22], p, Position, true);
            CreateSprite(BITMAP_ENERGY, Position, 0.2f, Light, o, 0.f);
            Vector(o->BlendMeshLight * 0.1f, o->BlendMeshLight * 0.1f, o->BlendMeshLight, Light);
            CreateSprite(BITMAP_SHINY + 1, Position, 1.f, Light, o, 0.f);

            b->TransformPosition(o->BoneTransform[15], p, Position, true);
            CreateSprite(BITMAP_ENERGY, Position, 0.2f, Light, o, 0.f);
            Vector(o->BlendMeshLight * 0.1f, o->BlendMeshLight * 0.1f, o->BlendMeshLight, Light);
            CreateSprite(BITMAP_SHINY + 1, Position, 1.f, Light, o, 0.f);
        }

        Luminosity = sinf(WorldTime * 0.001f) * 0.2f + 0.4f;
        for (i = 0; i < 5; ++i)
        {
            Vector(Luminosity * 0.1f, Luminosity * 0.4f, Luminosity, Light);

            b->TransformPosition(o->BoneTransform[i + 51], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 2.f, Light, o, WorldTime);

            b->TransformPosition(o->BoneTransform[i + 29], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 1.5f, Light, o, WorldTime);
        }
        return true;

    case MODEL_ILLUSION_OF_KUNDUN:

        if (visual.action == MONSTER01_DIE)
        {
        }
        else
        {
            // 눈
            Luminosity = (float)sin(WorldTime * 0.003f) * 0.2f + 0.8f;
            Vector(0, 0, 0, p);
            Vector(Luminosity * 1.0f, Luminosity * 0.0f, Luminosity * 0.0f, Light);
            b->TransformPosition(o->BoneTransform[8], p, Position, true);
            CreateSprite(BITMAP_ENERGY, Position, 0.4f, Light, o, 0.f);

            Vector(0, 0, 0, p);
            Vector(Luminosity * 1.0f, Luminosity * 0.0f, Luminosity * 0.0f, Light);
            b->TransformPosition(o->BoneTransform[9], p, Position, true);
            CreateSprite(BITMAP_ENERGY, Position, 0.4f, Light, o, 0.f);

            Vector(0, 0, 0, p);
            {
                Vector(1.f, 1.f, 1.f, Light);
                b->TransformPosition(o->BoneTransform[100], p, Position, true);
                float fRoar = 1.0f;
                if (visual.action == MONSTER01_SHOCK)
                {

                    fRoar = o->PKKey * 0.15f;
                    if (visual.animationFrame > 4.0f && visual.animationFrame < 6.0f)
                    {
                        fRoar += o->Timer * 0.15f;
                    }
                    else if (visual.animationFrame > 8.0f && visual.animationFrame < 10.0f)
                    {
                        fRoar += o->Timer * 0.15f;
                    }
                    else
                    {
                    }
                    CreateSprite(BITMAP_FLARE_BLUE, Position,
                                 (1.2f + (sinf(WorldTime * 0.001f) * 0.3f)), Light, o, 0.f);
                    CreateSprite(BITMAP_FLARE_RED, Position,
                                 (1.2f + (sinf(WorldTime * 0.001f) * 0.3f)) * fRoar, Light, o, 0.f);
                }
                else
                {

                    fRoar = 1.0f;
                    CreateSprite(BITMAP_FLARE_BLUE, Position,
                                 (1.2f + (sinf(WorldTime * 0.001f) * 0.3f)), Light, o, 0.f);
                }
                for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
                {
                    vec3_t birthPosition;
                    AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false,
                                             b->PoseAssetIdentity());
                    pose.SampleBonePosition(*b, *o, 100, p, WorldTime, birth.FrameFraction(),
                                            birthPosition);
                    CreateParticle(BITMAP_SMOKE, birthPosition, o->Angle, Light, 13, fRoar);
                }
            }

            if (visual.action == MONSTER01_SHOCK)
            {
                if (visual.animationFrame > 3.0f)
                {
                    EarthQuake = (float)(WorldRandom() % 8 - 8) * 0.2f;
                }
                if (gMapManager.InHellas() && visual.animationFrame > 3.0f &&
                    visual.animationFrame < 14.0f)
                {
                    vec3_t Position, Light;
                    Vector(0.3f, 0.8f, 1.f, Light);
                    vec3_t Angle = {0.f, 0.f, 0.f};
                    for (int i = 0; i < 2; ++i)
                    {
                        auto fAngle = float(WorldRandom() % 360);
                        auto fDistance = float(WorldRandom() % 600 + 200);
                        Position[0] = o->Position[0] + sinf(fAngle) * fDistance;
                        Position[1] = o->Position[1] + cosf(fAngle) * fDistance;
                        Position[2] = o->Position[2] + 800.f;
                        CreateEffectFpsChecked(9, Position, Angle, Light);
                    }
                }
            }
            if (visual.action == MONSTER01_ATTACK2)
            {
                if (visual.animationFrame > 6.0f)
                {
                    EarthQuake = (float)(WorldRandom() % 16 - 16) * 0.1f;
                }
            }
        }
        return true;

    case MODEL_BLOOD_SOLDIER:
        if (visual.action != MONSTER01_DIE)
        {
            Luminosity = (float)sin(WorldTime * 0.003f) * 0.2f + 0.8f;
            Vector(20.f, 30.f, -7.f, p);
            Vector(Luminosity * 1.0f, Luminosity * 0.9f, Luminosity * 0.9f, Light);
            b->TransformPosition(o->BoneTransform[6], p, Position, true);
            CreateSprite(BITMAP_ENERGY, Position, 0.2f, Light, o, 0.f);
            Vector(Luminosity * 1.0f, Luminosity * 0.3f, Luminosity * 0.1f, Light);
            CreateSprite(BITMAP_SHINY + 1, Position, 1.f, Light, o, 0.f);

            Vector(20.f, 30.f, 7.f, p);
            Vector(Luminosity * 1.0f, Luminosity * 0.9f, Luminosity * 0.9f, Light);
            b->TransformPosition(o->BoneTransform[6], p, Position, true);
            CreateSprite(BITMAP_ENERGY, Position, 0.2f, Light, o, 0.f);
            Vector(Luminosity * 1.0f, Luminosity * 0.3f, Luminosity * 0.1f, Light);
            CreateSprite(BITMAP_SHINY + 1, Position, 1.f, Light, o, 0.f);

            Vector(0.f, 0.f, 7.f, p);
            Vector(Luminosity * 1.0f, Luminosity * 0.9f, Luminosity * 0.9f, Light);
            b->TransformPosition(o->BoneTransform[13], p, Position, true);
            CreateSprite(BITMAP_ENERGY, Position, 0.1f, Light, o, 0.f);
            Vector(Luminosity * 1.0f, Luminosity * 0.5f, Luminosity * 0.1f, Light);
            CreateSprite(BITMAP_SHINY + 1, Position, 0.7f, Light, o, 0.f);

            Vector(0.f, 0.f, -7.f, p);
            Vector(Luminosity * 1.0f, Luminosity * 0.9f, Luminosity * 0.9f, Light);
            b->TransformPosition(o->BoneTransform[19], p, Position, true);
            CreateSprite(BITMAP_ENERGY, Position, 0.1f, Light, o, 0.f);
            Vector(Luminosity * 1.0f, Luminosity * 0.5f, Luminosity * 0.1f, Light);
            CreateSprite(BITMAP_SHINY + 1, Position, 0.7f, Light, o, 0.f);

            Vector(Luminosity * 1.0f, Luminosity * 0.8f, Luminosity * 0.8f, Light);
            Vector(0, 0, 0, p);
            b->TransformPosition(o->BoneTransform[23], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 3, Light, o, 0.f);
            Vector(-30, -25, 0, p);
            b->TransformPosition(o->BoneTransform[25], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 2.5f, Light, o, 0.f);
            Vector(0, 0, 0, p);
            b->TransformPosition(o->BoneTransform[31], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 3, Light, o, 0.f);
            Vector(30, 25, 0, p);
            b->TransformPosition(o->BoneTransform[33], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 2.5f, Light, o, 0.f);
        }
        return true;

    case MODEL_AEGIS:
        if (visual.action != MONSTER01_DIE)
        {
            Luminosity = (float)sin(WorldTime * 0.005f) * 0.15f + 0.85f;
            Vector(4.f, 0.f, 5.f, p);
            Vector(Luminosity * 1.0f, Luminosity * 1.0f, Luminosity * 1.0f, Light);
            b->TransformPosition(o->BoneTransform[9], p, Position, true);
            CreateSprite(BITMAP_ENERGY, Position, 0.1f, Light, o, 0.f);
            Vector(Luminosity * 0.3f, Luminosity * 0.6f, Luminosity * 1.f, Light);
            CreateSprite(BITMAP_SHINY + 1, Position, 1.f, Light, o, 0.f);

            Vector(4.f, 0.f, 5.f, p);
            Vector(Luminosity * 1.0f, Luminosity * 1.0f, Luminosity * 1.0f, Light);
            b->TransformPosition(o->BoneTransform[10], p, Position, true);
            CreateSprite(BITMAP_ENERGY, Position, 0.1f, Light, o, 0.f);
            Vector(Luminosity * 0.3f, Luminosity * 0.6f, Luminosity * 1.f, Light);
            CreateSprite(BITMAP_SHINY + 1, Position, 1.f, Light, o, 0.f);

            Vector(0, 0, 0, p);
            Vector(0.7f, 0.6f, 1, Light);
            b->TransformPosition(o->BoneTransform[3], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 1.7f, Light, o, 0.f);
            CreateSprite(BITMAP_SHINY + 1, Position, 1.f, Light, o, (float)(WorldRandom() % 360));
            b->TransformPosition(o->BoneTransform[4], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 1.7f, Light, o, 0.f);
            CreateSprite(BITMAP_SHINY + 1, Position, 1.f, Light, o, (float)(WorldRandom() % 360));

            vec3_t pos1, pos2;
            Vector(0, 0, 0, p);
            if (visual.action == MONSTER01_ATTACK1)
            {
                for (i = 10; i < 16; ++i)
                {
                    b->TransformPosition(o->BoneTransform[i], p, pos1, true);
                    b->TransformPosition(o->BoneTransform[i + 1], p, pos2, true);
                    CreateJointFpsChecked(BITMAP_JOINT_THUNDER, pos1, pos2, o->Angle, 7, NULL,
                                          30.f);
                }
                for (i = 31; i < 37; ++i)
                {
                    b->TransformPosition(o->BoneTransform[i], p, pos1, true);
                    b->TransformPosition(o->BoneTransform[i + 1], p, pos2, true);
                    CreateJointFpsChecked(BITMAP_JOINT_THUNDER, pos1, pos2, o->Angle, 7, NULL,
                                          30.f);
                }
            }
        }
        return true;

    case MODEL_DEATH_CENTURION:
        if (visual.action != MONSTER01_DIE)
        {
            Vector(0.f, 0.f, 30.f, p);

            if (c->MonsterIndex == MONSTER_DEATH_CENTURION_1)
            {
                Vector(1.f, 0.f, 0.f, Light);
                b->TransformPosition(o->BoneTransform[0], p, Position, true);
                CreateSprite(BITMAP_FLARE_BLUE, Position, 2.f + (sinf(WorldTime * 0.001f) * 0.3f),
                             Light, o, 0.f);

                for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 2.f))
                {
                    vec3_t birthPosition;
                    AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false,
                                             b->PoseAssetIdentity());
                    pose.SampleBonePosition(*b, *o, 0, p, WorldTime, birth.FrameFraction(),
                                            birthPosition);
                    CreateParticle(BITMAP_SMOKE, birthPosition, o->Angle, Light, 13);
                }
            }
            else
            {
                Vector(1.f, 1.f, 1.f, Light);
                b->TransformPosition(o->BoneTransform[0], p, Position, true);
                CreateSprite(BITMAP_FLARE_BLUE, Position, 1.2f + (sinf(WorldTime * 0.001f) * 0.3f),
                             Light, o, 0.f);
            }

            Vector(5.f, 0.f, 0.f, p);
            Vector(1.f, 1.f, 1.f, Light);
            b->TransformPosition(o->BoneTransform[28], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 0.3f + (sinf(WorldTime * 0.001f) * 0.2f), Light, o,
                         0.f);
            b->TransformPosition(o->BoneTransform[29], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 0.3f + (sinf(WorldTime * 0.001f) * 0.2f), Light, o,
                         0.f);
        }
        else
        {
        }
        return true;

    case MODEL_NECRON:
        if (visual.action != MONSTER01_DIE)
        {
            vec3_t pos;
            vec3_t Angle;

            Vector(5.f, 0.f, 0.f, p);
            Vector(1.f, 1.f, 1.f, Light);
            VectorCopy(o->Angle, Angle);
            b->TransformPosition(o->BoneTransform[9], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 0.3f + (sinf(WorldTime * 0.001f) * 0.2f), Light, o,
                         0.f);
            b->TransformPosition(o->BoneTransform[10], p, Position, true);
            CreateSprite(BITMAP_LIGHT, Position, 0.3f + (sinf(WorldTime * 0.001f) * 0.2f), Light, o,
                         0.f);

            Vector(0.f, 0.f, 0.f, p);
            Luminosity = (float)sin(WorldTime * 0.002f) * 0.3f + 0.6f;

            if (visual.action == MONSTER01_ATTACK2)
            {
                Vector(Luminosity * 0.1f, Luminosity, Luminosity * 0.1f, Light);
            }
            else
            {
                Vector(Luminosity, Luminosity, Luminosity, Light);
            }
            b->TransformPosition(o->BoneTransform[60], p, pos, true);
            CreateSprite(BITMAP_ENERGY, pos, 0.5f + (Luminosity * 0.2f), Light, o,
                         WorldTime * 0.1f);
            CreateSprite(BITMAP_ENERGY, pos, 0.5f + (Luminosity * 0.2f), Light, o,
                         -WorldTime * 0.1f);
            CreateParticleFpsChecked(BITMAP_LIGHT, pos, o->Angle, Light, 0, 1.1f);

            Vector(0.1f, 0.4f, 1.f, Light);
            CreateSprite(BITMAP_LIGHT, Position, 1.f, Light, o, 0.f);

            Vector(0.1f, 0.3f, 1.f, Light);
            for (i = 0; i < 5; ++i)
            {
                b->TransformPosition(o->BoneTransform[63 + i], p, Position, true);
                CreateSprite(BITMAP_LIGHT, Position, 0.5f + (sinf(WorldTime * 0.001f) * 0.2f),
                             Light, o, 0.f);

                if ((visual.action == MONSTER01_STOP1 || visual.action == MONSTER01_STOP2) &&
                    rand_fps_check(50))
                {
                    Angle[0] = (float)(WorldRandom() % 360);
                    Angle[2] = (float)(WorldRandom() % 360);
                    CreateJoint(BITMAP_FLARE + 1, Position, pos, Angle, 5, NULL, 20.f);
                }
            }
        }
        return true;

    case MODEL_SHRIKER:
        if (visual.action != MONSTER01_DIE)
        {
            vec3_t Pos1, Pos2;
            Luminosity = (float)sin(WorldTime * 0.003f) * 0.2f + 0.8f;
            Vector(0, 0, 0, p);
            b->TransformPosition(o->BoneTransform[33], p, Pos1, true);
            b->TransformPosition(o->BoneTransform[34], p, Pos2, true);
            if (o->SubType == 9)
            {
                Vector(Luminosity * 1.0f, Luminosity * 0.9f, Luminosity * 0.9f, Light);
            }
            else
            {
                Vector(Luminosity * 0.0f, Luminosity * 0.9f, Luminosity * 1.f, Light);
            }
            CreateSprite(BITMAP_ENERGY, Pos1, 0.1f, Light, o, 0.f);
            CreateSprite(BITMAP_ENERGY, Pos2, 0.1f, Light, o, 0.f);
            if (o->SubType == 9)
            {
                Vector(Luminosity * 1.0f, Luminosity * 0.3f, Luminosity * 0.1f, Light);
            }
            else
            {
                Vector(Luminosity * 0.1f, Luminosity * 0.3f, Luminosity * 1.f, Light);
            }
            CreateSprite(BITMAP_SHINY + 1, Pos1, 0.7f, Light, o, 0.f);
            CreateSprite(BITMAP_SHINY + 1, Pos2, 0.7f, Light, o, 0.f);

            if (o->SubType == 9)
            {
                Vector(Luminosity * 1.0f, Luminosity * 0.9f, Luminosity * 0.9f, Light);
            }
            else
            {
                Vector(Luminosity * 0.9f, Luminosity * 0.9f, Luminosity * 1.f, Light);
            }
            for (i = 0; i < 6; ++i)
            {
                Vector(0.f, -40.f - i * 24.f, 0.f, p);
                b->TransformPosition(o->BoneTransform[41], p, Pos1, true);
                b->TransformPosition(o->BoneTransform[51], p, Pos2, true);
                if (o->SubType == 9)
                {
                    CreateParticleFpsChecked(BITMAP_FIRE + 1, Pos1, o->Angle, Light, 1,
                                             7.2f / (i / 2 + 6));
                    CreateParticleFpsChecked(BITMAP_FIRE + 1, Pos2, o->Angle, Light, 1,
                                             7.2f / (i / 2 + 6));
                }
                else
                {
                    CreateParticleFpsChecked(BITMAP_FIRE + 3, Pos1, o->Angle, Light, 12,
                                             7.2f / (i / 2 + 6) * 0.5f);
                    CreateParticleFpsChecked(BITMAP_FIRE + 3, Pos2, o->Angle, Light, 12,
                                             7.2f / (i / 2 + 6) * 0.5f);
                }
            }
        }
        else
        {
        }
        return true;
    }
    return false;
}

void CGMHellas::EmitKundunDeath(OBJECT *o, BMD *b, WorldCharacterVisualState &visual)
{
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    constexpr std::array<std::pair<int, float>, 2> markers{
        {{MONSTER01_DIE, 8.f}, {MONSTER01_DIE, 14.8f}}};
    o->MotionTrace.VisitAnimationEvents(WorldTime, markers, [&](std::size_t event, float fraction) {
        auto birth =
            sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR * (1.f - fraction));
        if (event == 0 && visual.emissionLifeTime >= 100.f)
        {
            visual.emissionLifeTime = 90.f;
            PlayBuffer(SOUND_KUNDUN_DESTROY);
            vec3_t origin;
            o->MotionTrace.Sample(WorldTime, fraction, o->Position, origin);
            vec3_t Angle = {0.0f, 0.0f, 0.0f};
            int iCount = 86;
            for (int i = 0; i < iCount; ++i)
            {
                Angle[0] = -10.f;
                Angle[1] = 0.f;
                Angle[2] = i * (10.f + WorldRandom() % 10);

                vec3_t Position;
                VectorCopy(origin, Position);
                Position[2] += 200.f;
                CreateJoint(BITMAP_JOINT_SPIRIT, Position, Position, Angle, 3, NULL, 50.f, 0, 0);
                CreateJoint(BITMAP_JOINT_SPIRIT2, Position, Position, Angle, 3, NULL, 50.f, 0, 0);
            }
        }
        else if (event == 1 && visual.emissionLifeTime == 90.f)
        {
            visual.emissionLifeTime = 10.f;
            PlayBuffer(SOUND_KUNDUN_SHUDDER);
            ObjectDrawInput draw(o);
            o->MotionTrace.Sample(WorldTime, fraction, o->Position, draw.position);
            AnimationPoseSample pose(draw, b->BoneHead, b->BodyHeight, false,
                                     b->PoseAssetIdentity());
            std::array<vec34_t, MAX_BONES> bones;
            draw.bones = pose.EvaluateAtTime(*b, *o, WorldTime, fraction, bones.data());
            b->BodyScale = o->Scale;
            vec3_t angle;
            VectorCopy(o->Angle, angle);
            angle[2] = o->MotionTrace.SampleYaw(WorldTime, fraction, angle[2]);
            vec3_t p, Position;
            Vector(39.0f, -7.5f, -0.5, p);
            b->TransformByObjectBone(Position, draw, 4, p);
            CreateEffect(MODEL_CUNDUN_PART8, Position, angle, o->Light, 3, o, -130, 3);
            Vector(24.0f, -7.5f, 32.5f, p);
            b->TransformByObjectBone(Position, draw, 4, p);
            CreateEffect(MODEL_CUNDUN_PART1, Position, angle, o->Light, 3, o, -130, 4);
            Vector(24.0f, -8.5f, -32.5f, p);
            b->TransformByObjectBone(Position, draw, 4, p);
            CreateEffect(MODEL_CUNDUN_PART2, Position, angle, o->Light, 3, o, -130, 5);
            Vector(-0.5f, 4.0f, 0.5f, p);
            b->TransformByObjectBone(Position, draw, 4, p);
            CreateEffect(MODEL_CUNDUN_PART3, Position, angle, o->Light, 2, o, -130, 6);
            Vector(-2.5f, -22.0f, 54.0f, p);
            b->TransformByObjectBone(Position, draw, 4, p);
            CreateEffect(MODEL_CUNDUN_PART4, Position, angle, o->Light, 3, o, -130, 1);
            Vector(-4.5f, -24.5f, -53, p);
            b->TransformByObjectBone(Position, draw, 4, p);
            CreateEffect(MODEL_CUNDUN_PART5, Position, angle, o->Light, 3, o, -130, 2);
            Vector(-136.0f, -153.5f, 0, p);
            b->TransformByObjectBone(Position, draw, 4, p);
            CreateEffect(MODEL_CUNDUN_PART6, Position, angle, o->Light, 4, o, -10, 2);
            Vector(-135.0f, -153.0f, 0.0f, p);
            b->TransformByObjectBone(Position, draw, 4, p);
            CreateEffect(MODEL_CUNDUN_PART7, Position, angle, o->Light, 5, o, -130, 2);

            CreateEffect(MODEL_CUNDUN_SKILL, draw.position, angle, o->Light, 2);
        }
        EarthQuake = (float)(WorldRandom() % 8 - 8) * 0.1f;
    });
}

void CGMHellas::AdvanceKundunState(OBJECT &object)
{
    const auto frame = object.AnimationFrame;
    if (object.CurrentAction == MONSTER01_DIE)
    {
        if (frame >= 8.f && object.LifeTime >= 100.f)
            object.LifeTime = 90.f;
        if (frame >= 14.8f && object.LifeTime == 90.f)
            object.LifeTime = 10.f;
        return;
    }
    if (object.CurrentAction == MONSTER01_SHOCK)
    {
        const float speed = Models[object.Type].Actions[MONSTER01_SHOCK].PlaySpeed;
        const float rise = std::clamp(frame - 4.f, 0.f, 6.f);
        const float fall = std::max(0.f, frame - 10.f);
        object.PKKey = 10.f + std::max(0.f, rise - fall) / speed;
        object.Timer = 0.f;
        if ((frame > 4.f && frame < 6.f) || (frame > 8.f && frame < 10.f))
        {
            const float start = frame < 6.f ? 4.f : 8.f;
            const float phase = frame - start;
            object.Timer =
                phase <= 1.f ? 3.f * phase / speed : std::max(3.f, 3.f * (2.f - phase) / speed);
        }
        object.LifeTime = frame > 4.f ? 101.f : 100.f;
        return;
    }
    object.PKKey = 0.f;
    if (object.CurrentAction == MONSTER01_ATTACK1)
        object.LifeTime = frame > 3.f ? 101.f : 100.f;
    else if (object.CurrentAction == MONSTER01_ATTACK2)
        object.LifeTime = frame > 9.f ? 103.f : frame > 6.f ? 102.f : frame > 5.f ? 101.f : 100.f;
}

void CGMHellas::EmitMonsterMeshEffects(OBJECT *object, BMD *model,
                                       WorldCharacterVisualState &visual)
{
    const bool bubble = (object->Type == MODEL_DEATH_ANGEL || object->Type == MODEL_BLOOD_SOLDIER ||
                         object->Type == MODEL_AEGIS || object->Type == MODEL_DEATH_CENTURION ||
                         object->Type == MODEL_NECRON || object->Type == MODEL_SHRIKER) &&
                        visual.action == MONSTER01_DIE && object->Alpha < 0.5f;
    const bool defense = ((object->Type == MODEL_DEATH_CENTURION && object->SubType == 9) ||
                          object->Type == MODEL_SHRIKER) &&
                         (visual.priorAI == HellasDetail::ACTION_DESTROY_WIZ_DEF ||
                          visual.priorAI == HellasDetail::ACTION_DESTROY_DEF);
    if (!bubble && !defense)
        return;
    const auto emit = [&](int mesh, int type, int subtype, float fraction) {
        PrepareWorldObjectPose(*object, fraction);
        OBB_t bounds{};
        model->Transform(BoneTransform, object->BoundingBoxMin, object->BoundingBoxMax, &bounds,
                         false);
        EmitMeshEffects(*model, mesh, type, subtype);
    };
    if (bubble)
        for (auto birthTime : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
            emit(0, BITMAP_BUBBLE, 0, birthTime.FrameFraction());
    if (defense)
    {
        emit(6, MODEL_STONE_COFFIN, visual.priorAI == HellasDetail::ACTION_DESTROY_WIZ_DEF ? 1 : 2,
             1.f);
        PlayBuffer(SOUND_HIT_CRISTAL);
    }
}

bool CGMHellas::AdvanceObjectVisual(OBJECT *object, BMD *model, float)
{
    return AdvanceHellasVisual(object, model);
}

bool CGMHellas::AttackEffectBeforeShared(CHARACTER *character, OBJECT *object, BMD *model)
{
    CHARACTER *target = nullptr;
    if (CharactersClient.IsValidIndex(character->TargetCharacter))
        target = &CharactersClient[character->TargetCharacter];
    return AttackEffect_HellasMonster(character, target, object, target ? &target->Object : nullptr,
                                      model);
}

void CGMHellas::UpdateMusic()
{
    PlayMp3(MUSIC_KALIMA);
}

bool CGMHellas::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_KALIMA) == 0;
}

void CGMHellas::ConfigureAmbientFish(OBJECT *o)
{
    o->Type = -1;
    o->Scale = (float)(WorldRandom() % 4 + 8) * 0.1f;
    o->Velocity = 2.5f / o->Scale;
    o->Gravity = 9;
    o->LifeTime = 70;
    CreateJointFpsChecked(BITMAP_FLARE + 1, o->Position, o->Position, o->Angle, 8, o, 50.f);
}

bool CGMHellas::CanCreateAmbientFish(int index)
{
    return TerrainWall[index] == 0 || TerrainWall[index] == TW_CHARACTER;
}

bool CGMHellas::ConfigureAmbientBoid(OBJECT *object, int)
{
    return CreateBigMon(object) == 1;
}

bool CGMHellas::CanCreateAmbientBoid(int slot, int index)
{
    return true;
}

bool CGMHellas::PrepareAmbientBoidSlot(int index, bool &allowCreate)
{
    allowCreate = WorldRandom() % 10 == 0;
    return index <= 1;
}

void CGMHellas::PrepareObjectEffects(int &count, int previousVisible)
{
    MoveHellasObjectSetting(count, previousVisible);
}

ESound CGMHellas::WalkingSound(int tile, bool safe) const
{
    return safe ? SOUND_HUMAN_WALK_GROUND : SOUND_HUMAN_WALK_SWIM;
}

bool GMChaosCastle::AdvanceObjectVisual(OBJECT *object, BMD *model, float)
{
    return AdvanceChaosCastleVisual(object, model);
}
// namespace

bool GMChaosCastle::AdvanceChaosCastleVisual(OBJECT *o, BMD *b)
{
    if (gMapManager.InChaosCastle() == false)
        return false;

    vec3_t p, Position;

    switch (o->Type)
    {
    case 6:
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.05f, 0.05f, 0.1f, Light);
            for (int i = 0; i < 10; ++i)
            {
                CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 0, o->Scale, o);
            }
        }
        o->HiddenMesh = -2;
        break;

    case 7:
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.05f, 0.05f, 0.1f, Light);
            for (int i = 0; i < 10; ++i)
            {
                CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 1, o->Scale, o);
            }
        }
        o->HiddenMesh = -2;
        break;

    case 8:
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.05f, 0.05f, 0.1f, Light);
            for (int i = 0; i < 10; ++i)
            {
                CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 2, o->Scale, o);
            }
        }
        o->HiddenMesh = -2;
        break;

    case 9:
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.05f, 0.05f, 0.1f, Light);
            for (int i = 0; i < 5; ++i)
            {
                CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 3, o->Scale, o);
            }
        }
        o->HiddenMesh = -2;
        break;

    case 10:
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.05f, 0.05f, 0.1f, Light);
            for (int i = 0; i < 5; ++i)
            {
                CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 4, o->Scale, o);
            }
        }
        o->HiddenMesh = -2;
        break;

    case 11:
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.05f, 0.05f, 0.1f, Light);
            for (int i = 0; i < 5; ++i)
            {
                CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 5, o->Scale, o);
            }
        }
        o->HiddenMesh = -2;
        break;

    case 12:
        if (o->HiddenMesh != -2)
        {
            vec3_t Light;
            Vector(0.3f, 0.3f, 0.3f, Light);
            for (int i = 0; i < 7; ++i)
            {
                CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 7, o->Scale, o);
            }
        }
        o->HiddenMesh = -2;
        break;

    case 18:
    case 19:
    case 20:
    case 21:
        if (g_currentCastleLevel == CastleLevel::Seven ||
            g_currentCastleLevel == CastleLevel::Eight)
        {
            o->HiddenMesh = -1;
        }
        else
        {
            o->HiddenMesh = -2;
        }
        break;

    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
        if (g_currentCastleLevel == CastleLevel::Four || g_currentCastleLevel == CastleLevel::Five)
        {
            o->HiddenMesh = -1;
        }
        else if (ChaosCastleDetail::LevelValue(g_currentCastleLevel) >=
                 ChaosCastleDetail::LevelValue(CastleLevel::Eight))
        {
            o->HiddenMesh = -2;
        }
        break;

    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
        if (g_currentCastleLevel == CastleLevel::One || g_currentCastleLevel == CastleLevel::Two)
        {
            o->HiddenMesh = -1;
        }
        else if (ChaosCastleDetail::LevelValue(g_currentCastleLevel) >=
                 ChaosCastleDetail::LevelValue(CastleLevel::Five))
        {
            o->HiddenMesh = -2;
        }
        break;

    case 0:
    case 1:
    case 2:
    case 3:
        if (o->PKKey && o->HiddenMesh != -2)
        {
            PrepareWorldObjectPose(*o);
            Vector(0.f, 0.f, 0.f, p);
            b->TransformPosition(BoneTransform[1], p, Position);
            if ((int)o->LifeTime == 10)
            {
                CreateJoint(BITMAP_JOINT_THUNDER + 1, Position, Position, o->Angle, 2, NULL,
                            60.f + Random.RangeFloat(0, 9));

                int randValue = Random.RangeInt(0, 1);
                PlayBuffer(static_cast<ESound>(SOUND_CHAOS_THUNDER01 + randValue));
                o->LifeTime = 9.9f;
            }

            if (o->LifeTime < 5)
            {
                o->PKKey = 0;
            }
            else
            {
                o->LifeTime -= FPS_ANIMATION_FACTOR;
            }
        }
    case 4:
    case 5:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        if (ChaosCastleDetail::LevelValue(g_currentCastleLevel) >=
                ChaosCastleDetail::LevelValue(CastleLevel::Two) &&
            g_currentCastleLevel != CastleLevel::Invalid)
        {
            o->HiddenMesh = -2;
        }
        break;
    }

    return true;
}

void GMChaosCastle::AdvanceChaosTerrainCell(int xi, int yi)
{
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 8.f))
    {
        vec3_t light{1.f, 1.f, 1.f}, angle{}, position;
        position[0] = xi * TERRAIN_SCALE + Random.RangeFloat(-15, 14);
        position[1] = yi * TERRAIN_SCALE + Random.RangeFloat(-15, 14);
        position[2] = Hero->Object.Position[2];
        CreateParticle(BITMAP_SMOKE + 4, position, angle, light, 0, 1.5f);
        if (Random.FpsCheck(5, 1.f))
            EarthQuake = Random.RangeFloat(-3, -1) * 0.1f;
    }
}

void GMChaosCastle::AdvanceChaosTerrain()
{
    if (!gMapManager.InChaosCastle())
        return;
    const auto *areas = ChaosCastleDetail::SelectLimitArea(g_currentCastleLevel);
    if (!areas)
        return;
    for (const auto &area : *areas)
        for (int y = area[1]; y <= area[3]; ++y)
            for (int x = area[0]; x <= area[2]; ++x)
                AdvanceChaosTerrainCell(x, y);
}

void GMChaosCastle::AdvanceObjectFade(OBJECT &object)
{
    if (Hero->Object.m_bActionStart)
        object.Alpha -= 0.15f * FPS_ANIMATION_FACTOR;
}

void GMChaosCastle::AdvanceEnvironment()
{
    AdvanceChaosTerrain();
}

bool GMChaosCastle::CreateWeather(PARTICLE *o, int Index)
{
    o->Type = BITMAP_RAIN;
    o->TurningForce[0] = 1.f;
    o->TurningForce[1] = 30.f + Random.RangeFloat(0, 9);

    if (Index < 300)
    {
        const float randomX = Random.RangeFloat(-800, 799);
        const float randomY = Random.RangeFloat(-500, 899);
        const float randomZ = Random.RangeFloat(300, 499);
        Vector(Hero->Object.Position[0] + randomX, Hero->Object.Position[1] + randomY,
               Hero->Object.Position[2] + randomZ, o->Position);
    }
    else
    {
        const float randomX = Random.RangeFloat(-800, 799);
        const float randomY = Random.RangeFloat(1000, 1299) - RainPosition;
        const float randomZ = Random.RangeFloat(300, 499);
        Vector(Hero->Object.Position[0] + randomX, Hero->Object.Position[1] + randomY,
               Hero->Object.Position[2] + randomZ, o->Position);
    }
    if (Random.FpsCheck(2, 1.0))
    {
        Vector(-Random.RangeFloat(20, 39), 0.f, 0.f, o->Angle);
    }
    else
    {
        Vector(-(Random.RangeFloat(30, 49) + RainAngle), 0.f, 0.f, o->Angle);
    }
    vec3_t Velocity;
    Vector(0.f, 0.f, -((Random.RangeFloat(0, 39) + RainSpeed + 20)), Velocity);
    float Matrix[3][4];
    AngleMatrix(o->Angle, Matrix);
    VectorRotate(Velocity, Matrix, o->Velocity);

    return true;
}

bool GMChaosCastle::MoveWeather(PARTICLE *o)
{
    VectorAddScaled(o->Position, o->Velocity, o->Position, FPS_ANIMATION_FACTOR);
    float Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
    if (o->Position[2] < Height &&
        (TERRAIN_ATTRIBUTE(o->Position[0], o->Position[1]) & TW_NOGROUND) != TW_NOGROUND)
    {
        o->Live = false;
        o->Position[2] = Height + 10.f;
        if (Random.FpsCheck(4, 1.f))
            CreateParticle(BITMAP_RAIN_CIRCLE, o->Position, o->Angle, o->Light);
        else
            CreateParticle(BITMAP_RAIN_CIRCLE + 1, o->Position, o->Angle, o->Light);
    }
    return true;
}

int GMChaosCastle::PrepareWeather()
{
    RainTarget = MAX_LEAVES / 2;
    return 80;
}

void GMChaosCastle::PrepareObjectEffects(int &count, int previousVisible)
{
    MoveChaosCastleObjectSetting(count, previousVisible);
}

void GMChaosCastle::MoveObjectEffects(OBJECT *object, int &count, int &visible)
{
    MoveChaosCastleObject(object, count, visible);
}

bool GMChaosCastle::PlayMonsterDeathSound(OBJECT *object)
{
    PlayBuffer(static_cast<ESound>(SOUND_CHAOS_MOB_BOOM01 + WorldRandom() % 2), object);
    return true;
}

void CGM_PK_Field::MoveBlurEffect(CHARACTER *c, OBJECT *o, BMD *b)
{
    if (!gMapManager.IsPKField())
        return;

    if (o->Type >= MODEL_GLADIATOR && o->Type <= MODEL_CRUEL_BLOOD_ASSASSIN)
    {
        float Start_Frame = 0.0f;
        float End_Frame = 0.0f;

        switch (o->Type)
        {
        case MODEL_SLAUGHTERER: {
            Start_Frame = 4.0f;
            End_Frame = 10.0f;
        }
        break;
        case MODEL_GLADIATOR: {
            Start_Frame = 3.0f;
            End_Frame = 7.0f;
        }
        break;
        case MODEL_BLOOD_ASSASSIN:
        case MODEL_CRUEL_BLOOD_ASSASSIN: {
            Start_Frame = 3.0f;
            End_Frame = 8.0f;
        }
        break;
        default:
            return;
        }

        if ((o->AnimationFrame >= Start_Frame && o->AnimationFrame <= End_Frame &&
             o->CurrentAction == MONSTER01_ATTACK1) ||
            (o->AnimationFrame >= Start_Frame && o->AnimationFrame <= End_Frame &&
             o->CurrentAction == MONSTER01_ATTACK2))
        {
            BMD *b = &Models[o->Type];
            vec3_t Light;

            vec3_t StartPos, StartRelative;
            vec3_t EndPos, EndRelative;

            float fDelay = 5.0f;

            float fActionSpeed = b->Actions[o->CurrentAction].PlaySpeed;
            float fSpeedPerFrame = fActionSpeed / fDelay;
            float fAnimationFrame = o->AnimationFrame - fActionSpeed;
            for (int i = 0; i < fDelay; i++)
            {
                b->AnimationAtFrame(BoneTransform, fAnimationFrame, o->PriorAnimationFrame,
                                    o->PriorAction, o->Angle, o->HeadAngle);

                Vector(0.f, 0.f, 0.f, StartRelative);
                Vector(0.f, 0.f, 0.f, EndRelative);

                switch (o->Type)
                {
                case MODEL_SLAUGHTERER: {
                    Vector(0.3f, 0.3f, 0.3f, Light);
                    b->TransformPosition(BoneTransform[33], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[34], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 0);
                }
                break;
                case MODEL_GLADIATOR: {
                    Vector(0.0f, 0.3f, 0.2f, Light);
                    b->TransformPosition(BoneTransform[39], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[40], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 0);
                }
                break;
                case MODEL_BLOOD_ASSASSIN:
                case MODEL_CRUEL_BLOOD_ASSASSIN: {
                    if (o->Type == MODEL_BLOOD_ASSASSIN)
                    {
                        Vector(0.9f, 0.2f, 0.1f, Light);
                    }
                    else //o->Type == MODEL_MONSTER01+161
                    {
                        Vector(0.2f, 0.9f, 0.1f, Light);
                    }
                    b->TransformPosition(BoneTransform[40], StartRelative, StartPos, false);

                    b->TransformByBoneMatrix(EndPos, BoneTransform[40]);
                    CreateBlur(c, StartPos, EndPos, Light, 5, false, 0);

                    b->TransformPosition(BoneTransform[40], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 5, false, 1);
                    b->TransformPosition(BoneTransform[53], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 5, false, 2);
                    b->TransformPosition(BoneTransform[14], StartRelative, StartPos, false);
                    b->TransformPosition(BoneTransform[14], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 5, false, 3);
                    b->TransformPosition(BoneTransform[27], EndRelative, EndPos, false);
                    CreateBlur(c, StartPos, EndPos, Light, 5, false, 4);
                }
                break;

                default:
                    break;
                }
                fAnimationFrame += fSpeedPerFrame;
            }
        }
    }
}

bool CGM_PK_Field::AdvanceObjectVisual(OBJECT *o, BMD *b, float)
{
    if (!gMapManager.IsPKField())
    {
        return false;
    }

    vec3_t Light;

    switch (o->Type)
    {
    case 67: {
        PrepareWorldObjectPose(*o);
        vec3_t vLightFire, Position, vPos;
        Vector(1.0f, 0.0f, 0.0f, vLightFire);
        Vector(0.0f, 0.0f, 0.0f, vPos);

        b->TransformPosition(BoneTransform[6], vPos, Position, false);
        CreateSprite(BITMAP_LIGHT, Position, o->Scale * 5.0f, vLightFire, o);

        Vector(0.0f, 0.0f, -350.0f, vPos);
        b->TransformPosition(BoneTransform[6], vPos, Position, false);
        CreateSprite(BITMAP_LIGHT, Position, o->Scale * 5.0f, vLightFire, o);

        if (o->AnimationFrame >= 35 && o->AnimationFrame <= 37)
        {
            o->PKKey = -1;
        }

        if (o->AnimationFrame >= 1 && o->AnimationFrame <= 2 && o->PKKey != 1)
        {
            o->AnimationFrame = 1;

            int test = Random.RangeInt(0, 999);
            if (test >= 0 && test < 2)
            {
                o->PKKey = 1;
            }
            else
            {
                o->PKKey = -1;
            }
        }
        vec3_t p, Pos, Light;
        Vector(0.4f, 0.1f, 0.1f, Light);
        //Vector(Random.RangeFloat(-30, -11), Random.RangeFloat(-30, -11), 0.0f, p);
        Vector(-150.0f, 0.0f, 0.0f, p);
        b->TransformPosition(BoneTransform[4], p, Pos, false);
        if (o->AnimationFrame >= 35.0f && o->AnimationFrame < 50.0f)
            CreateParticleFpsChecked(BITMAP_SMOKE, Pos, o->Angle, Light, 63, o->Scale * 1.5f);

        return true;
    }

    case 68: {
        PrepareWorldObjectPose(*o);
        vec3_t vLightFire, Position, vPos;
        Vector(1.0f, 0.0f, 0.0f, vLightFire);
        Vector(0.0f, 0.0f, 0.0f, vPos);

        b->TransformPosition(BoneTransform[6], vPos, Position, false);
        CreateSprite(BITMAP_LIGHT, Position, o->Scale * 5.0f, vLightFire, o);

        Vector(0.0f, 0.0f, -350.0f, vPos);
        b->TransformPosition(BoneTransform[6], vPos, Position, false);
        CreateSprite(BITMAP_LIGHT, Position, o->Scale * 5.0f, vLightFire, o);

        vec3_t p, Pos, Light;
        //Vector(0.08f, 0.08f, 0.08f, Light);
        Vector(0.3f, 0.1f, 0.1f, Light);
        Vector(Random.RangeFloat(-30, -11), Random.RangeFloat(-30, -11), 0.0f, p);
        b->TransformPosition(BoneTransform[4], p, Pos, false);
        if (o->AnimationFrame >= 7.0f && o->AnimationFrame < 13.0f)
            CreateParticleFpsChecked(BITMAP_SMOKE, Pos, o->Angle, Light, 18, o->Scale * 1.5f);

        return true;
    }

    case 0: {
        o->HiddenMesh = -2;
        float fLumi = ((sinf(WorldTime * 0.001f) + 1.f) * 0.5f) * 100.0f;

        int nRanDelay = o->Position[0];
        nRanDelay = nRanDelay % 3 + 1;
        int nRanTemp = 30;
        nRanTemp = nRanTemp * nRanDelay;
        int nRanGap = 10;
        if (nRanTemp != 90.0f)
        {
            nRanGap = 40;
        }

        if (fLumi >= nRanTemp && fLumi <= nRanTemp + nRanGap)
        {
            Vector(1.0f, 1.0f, 1.0f, Light);
            for (int i = 0; i < 20; ++i)
            {
                CreateParticleFpsChecked(BITMAP_WATERFALL_2, o->Position, o->Angle, Light, 6,
                                         o->Scale, o);
            }
        }
    }
        return true;
    case 1: {
        o->HiddenMesh = -2;
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(1.0f, 1.0f, 1.0f, Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, Light, 60, o->Scale, o);
        }
    }
        return true;
    case 2: {
        o->HiddenMesh = -2;
        vec3_t Light;
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(0.f, 0.f, 0.f, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 16, o->Scale, o);
        }
    }
        return true;
    case 3: {
        o->HiddenMesh = -2;
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.f))
        {
            float fRed = Random.RangeFloat(0, 2) * 0.01f + 0.015f;
            Vector(fRed, 0.0f, 0.0f, Light);
            CreateParticle(BITMAP_CLOUD, o->Position, o->Angle, Light, 11, o->Scale, o);
        }
    }
        return true;
    case 4: {
        o->HiddenMesh = -2;
        Vector(1.0f, 0.4f, 0.4f, Light);
        vec3_t vAngle;
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(Random.RangeFloat(120, 159), 0.f, Random.RangeFloat(0, 29), vAngle);
            VectorAdd(vAngle, o->Angle, vAngle);
            CreateJoint(BITMAP_JOINT_SPARK, o->Position, o->Position, vAngle, 4, o, o->Scale);
            CreateParticle(BITMAP_SPARK, o->Position, vAngle, Light, 9, o->Scale);
        }
    }
        return true;
    case 5: {
        o->HiddenMesh = -2;
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
        {
            Vector(0.3f, 0.3f, 0.3f, o->Light);
            CreateParticle(BITMAP_SMOKE, o->Position, o->Angle, o->Light, 21, o->Scale);
        }
    }
        return true;
    case 6: {
        o->HiddenMesh = -2;

        vec3_t vLightFire;
        Vector(1.0f, 0.2f, 0.0f, vLightFire);
        CreateSprite(BITMAP_LIGHT, o->Position, 2.0f * o->Scale, vLightFire, o);

        vec3_t vLight;
        Vector(1.0f, 1.0f, 1.0f, vLight);
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            switch (Random.RangeInt(0, 2))
            {
            case 0:
                CreateParticle(BITMAP_FIRE_HIK1, o->Position, o->Angle, vLight, 0, o->Scale);
                break;
            case 1:
                CreateParticle(BITMAP_FIRE_CURSEDLICH, o->Position, o->Angle, vLight, 4, o->Scale);
                break;
            case 2:
                CreateParticle(BITMAP_FIRE_HIK3, o->Position, o->Angle, vLight, 0, o->Scale);
                break;
            }
        }
    }
        return true;
    }
    return false;
}

bool CGM_PK_Field::MoveMonsterVisual(CHARACTER *, OBJECT *o, BMD *b,
                                     WorldCharacterVisualState &visual)
{
    ObjectDrawInput presentation(o);
    visual.movement.Apply(presentation);
    if (!gMapManager.IsPKField())
        return false;

    switch (o->Type)
    {
    case MODEL_BLOOD_ASSASSIN:
    case MODEL_CRUEL_BLOOD_ASSASSIN: {
        if (visual.action == MONSTER01_DIE)
        {
            int iBones[] = {5, 6, 7}; // Neck/ Head/ HeadNub
            vec3_t vLight, vPos, vRelative;
            Vector(1.0f, 1.0f, 1.0f, vLight);
            vec3_t vLightFire;
            if (o->Type == MODEL_BLOOD_ASSASSIN)
            {
                Vector(1.0f, 0.2f, 0.0f, vLightFire);
            }
            else
            {
                Vector(0.2f, 1.0f, 0.0f, vLightFire);
            }
            for (int i = 0; i < 3; ++i)
            {
                float fScale = 1.2f;
                if (i >= 1)
                {
                    b->TransformByObjectBone(vPos, presentation, iBones[i]);
                    CreateSprite(BITMAP_LIGHT, vPos, 1.0f, vLightFire, o);

                    fScale = 0.7f;
                    Vector(Random.RangeFloat(-5, 4), Random.RangeFloat(-5, 4),
                           Random.RangeFloat(-5, 4), vRelative);
                    b->TransformByObjectBone(vPos, presentation, iBones[i], vRelative);
                }
                else
                {
                    b->TransformByObjectBone(vPos, presentation, iBones[i]);
                    vPos[2] += 50.0f;
                    CreateSprite(BITMAP_LIGHT, vPos, 2.5f, vLightFire, o);

                    Vector(Random.RangeFloat(-10, 9), Random.RangeFloat(-10, 9),
                           Random.RangeFloat(-10, 9), vRelative);
                    b->TransformByObjectBone(vPos, presentation, iBones[i], vRelative);
                }
                if (o->Type == MODEL_BLOOD_ASSASSIN)
                {
                    for (int i = 0; i < 2; ++i)
                    {
                        float fScale = Random.RangeFloat(18, 22) * 0.03f;
                        switch (Random.RangeInt(0, 2))
                        {
                        case 0:
                            CreateParticleFpsChecked(BITMAP_FIRE_HIK1, vPos, o->Angle, vLight, 0,
                                                     fScale);
                            break;
                        case 1:
                            CreateParticleFpsChecked(BITMAP_FIRE_CURSEDLICH, vPos, o->Angle, vLight,
                                                     4, fScale);
                            break;
                        case 2:
                            CreateParticleFpsChecked(BITMAP_FIRE_HIK3, vPos, o->Angle, vLight, 0,
                                                     fScale);
                            break;
                        }
                    }
                }
                else //o->Type == MODEL_MONSTER01+161
                {
                    for (int i = 0; i < 2; ++i)
                    {
                        float fScale = Random.RangeFloat(18, 22) * 0.03f;
                        Vector(0.6f, 0.9f, 0.1f, visual.movement.light);
                        switch (Random.RangeInt(0, 2))
                        {
                        case 0:
                            CreateParticleFpsChecked(BITMAP_FIRE_HIK1_MONO, vPos, o->Angle,
                                                     visual.movement.light, 0, fScale);
                            break;
                        case 1:
                            CreateParticleFpsChecked(BITMAP_FIRE_HIK2_MONO, vPos, o->Angle,
                                                     visual.movement.light, 4, fScale);
                            break;
                        case 2:
                            CreateParticleFpsChecked(BITMAP_FIRE_HIK3_MONO, vPos, o->Angle,
                                                     visual.movement.light, 0, fScale);
                            break;
                        }
                    }
                }
            }
        }
    }
        return true;
    }
    return false;
}

void CGM_PK_Field::AdvanceLavaFootsteps(OBJECT &object, BMD &model)
{
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    constexpr std::array<std::pair<int, float>, 3> Footsteps{
        {{MONSTER01_WALK, 2.f}, {MONSTER01_WALK, 7.f}, {MONSTER01_ATTACK2, 6.f}}};
    const int type = object.Type == MODEL_BURNING_LAVA_GIANT ? MODEL_LAVAGIANT_FOOTPRINT_R
                                                             : MODEL_LAVAGIANT_FOOTPRINT_V;
    AnimationPoseSample pose(&object, model.BoneHead, model.BodyHeight, false,
                             model.PoseAssetIdentity());
    object.MotionTrace.VisitAnimationEvents(
        WorldTime, Footsteps, [&](std::size_t event, float fraction) {
            auto birth =
                sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR * (1.f - fraction));
            const bool attack = event == 2;
            vec3_t local{}, position, angle, light{1.f, 1.f, 1.f};
            pose.SampleBonePosition(model, object, event == 0 ? 36 : 42, local, WorldTime, fraction,
                                    position);
            VectorCopy(object.Angle, angle);
            angle[2] = object.MotionTrace.SampleYaw(WorldTime, fraction, angle[2]);
            CreateEffect(type, position, angle, light, 0, &object, -1, 0, 0, 0,
                         attack ? 1.6f : 1.3f);
            CreateParticle(BITMAP_SMOKE, position, angle, light, 62, attack ? 4.f : 1.f);
        });
}

bool CGM_PK_Field::AdvanceMonsterVisual(CHARACTER *c, OBJECT *o, BMD *b,
                                        WorldCharacterVisualState &visual)
{
    vec3_t emissionLight;
    VectorCopy(o->Light, emissionLight);

    if (!gMapManager.IsPKField())
    {
        return false;
    }

    vec3_t vPos, vLight;

    switch (o->Type)
    {
    case MODEL_ZOMBIE_FIGHTER: {
    }
        return true;
    case MODEL_GLADIATOR: {
    }
        return true;
    case MODEL_SLAUGHTERER: {
        vec3_t p, Position;
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.f))
        {
            const float fraction = birth.FrameFraction();
            AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false, b->PoseAssetIdentity());
            Vector(0.0f, 50.0f, 0.0f, p);
            pose.SampleBonePosition(*b, *o, 6, p, WorldTime, fraction, Position);

            Vector(1.0f, 1.0f, 1.0f, emissionLight);
            CreateParticle(BITMAP_SMOKE, Position, o->Angle, emissionLight, 61);
        }
    }
        return true;
    case MODEL_BLOOD_ASSASSIN:
    case MODEL_CRUEL_BLOOD_ASSASSIN: {
        int iBones[] = {37, 11, 70, 65, 6};

        switch (o->Type)
        {
        case MODEL_BLOOD_ASSASSIN:
            Vector(0.9f, 0.2f, 0.1f, vLight); //red
            break;
        case MODEL_CRUEL_BLOOD_ASSASSIN:
            Vector(0.3f, 0.9f, 0.2f, vLight); //green
            break;
        }
        if (visual.action != MONSTER01_DIE)
        {
            for (int i = 0; i < 4; ++i)
            {
                if (Random.RangeInt(0, 3) > 0)
                    continue;

                b->TransformByObjectBone(vPos, o, iBones[i]);
                CreateParticleFpsChecked(BITMAP_SMOKE, vPos, o->Angle, vLight, 50, 1.0f);
                CreateParticleFpsChecked(BITMAP_SMOKELINE1 + Random.RangeInt(0, 2), vPos, o->Angle,
                                         vLight, 0, 0.01f);
            }

            if (visual.action == MONSTER01_ATTACK1)
            {
                vec3_t TempPos;
                Vector(0.0f, 0.0f, 0.0f, TempPos);

                b->TransformByObjectBone(vPos, o, iBones[4]);

                TempPos[1] = (Hero->Object.Position[1] - vPos[1]) * 0.5f;
                TempPos[0] = (Hero->Object.Position[0] - vPos[0]) * 0.5f;
                TempPos[2] = 0.0f;

                VectorNormalize(TempPos);
                VectorScale(TempPos, 50.0f, TempPos);

                VectorAdd(vPos, TempPos, vPos);

                CreateParticleFpsChecked(BITMAP_LIGHT + 2, vPos, o->Angle, vLight, 7, 0.5f);

                switch (o->Type)
                {
                case MODEL_BLOOD_ASSASSIN:
                    Vector(0.9f, 0.4f, 0.1f, vLight); //red
                    break;
                case MODEL_CRUEL_BLOOD_ASSASSIN:
                    Vector(0.6f, 0.9f, 0.2f, vLight); //green
                    break;
                }
                CreateParticleFpsChecked(BITMAP_SPARK + 1, vPos, o->Angle, vLight, 29, 1.0f);
            }
        }
        else //visual.action == MONSTER01_DIE
        {
            if ((int)visual.emissionLifeTime == 100)
            {
                const int bodyType = o->Type == MODEL_BLOOD_ASSASSIN
                                         ? MODEL_PKFIELD_ASSASSIN_EFFECT_RED_BODY
                                         : MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_BODY;
                CreateEffect(bodyType, o->Position, o->Angle, emissionLight, 0, o, 0, 0);
                visual.emissionLifeTime = 90;

                vec3_t vRelativePos, vWorldPos, Light;
                Vector(1.0f, 1.0f, 1.0f, Light);

                Vector(0.f, 0.f, 0.f, vRelativePos);

                b->TransformPosition(o->BoneTransform[5], vRelativePos, vWorldPos, true);
                switch (o->Type)
                {
                case MODEL_BLOOD_ASSASSIN:
                    CreateEffect(MODEL_PKFIELD_ASSASSIN_EFFECT_RED_HEAD, vWorldPos, o->Angle, Light,
                                 0, o, 0, 0);
                    break;

                case MODEL_CRUEL_BLOOD_ASSASSIN:
                    CreateEffect(MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_HEAD, vWorldPos, o->Angle,
                                 Light, 0, o, 0, 0);
                    break;
                }
            }
        }
    }
        return true;
    case MODEL_LAVA_GIANT:
    case MODEL_BURNING_LAVA_GIANT: {
        auto fRotation = (float)((int)(WorldTime * 0.1f) % 360);
        float fAngle = (sinf(WorldTime * 0.003f) + 1.0f) * 0.4f + 1.5f;
        vec3_t vWorldPos, vLight;
        switch (o->Type)
        {
        case MODEL_LAVA_GIANT: {
            Vector(0.5f, 0.1f, 0.9f, vLight);
        }
        break;
        case MODEL_BURNING_LAVA_GIANT: {
            Vector(0.9f, 0.4f, 0.1f, vLight);
        }
        break;
        }

        b->TransformByObjectBone(vWorldPos, o, 3);
        CreateSprite(BITMAP_LIGHTMARKS, vWorldPos, fAngle, vLight, o, fRotation);
        fAngle = (sinf(WorldTime * 0.003f) + 1.0f) * 0.4f + 0.5f;
        b->TransformByObjectBone(vWorldPos, o, 37);
        CreateSprite(BITMAP_LIGHTMARKS, vWorldPos, fAngle, vLight, o, fRotation);
        b->TransformByObjectBone(vWorldPos, o, 43);
        CreateSprite(BITMAP_LIGHTMARKS, vWorldPos, fAngle, vLight, o, fRotation);

        AdvanceLavaFootsteps(*o, *b);

        vec3_t p, Position;
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 4.f))
        {
            const float fraction = birth.FrameFraction();
            AnimationPoseSample pose(o, b->BoneHead, b->BodyHeight, false, b->PoseAssetIdentity());
            Vector(0.0f, 50.0f, 0.0f, p);
            pose.SampleBonePosition(*b, *o, 7, p, WorldTime, fraction, Position);
            switch (o->Type)
            {
            case MODEL_LAVA_GIANT:
                Vector(0.5f, 0.1f, 0.9f, emissionLight);
                break;
            case MODEL_BURNING_LAVA_GIANT:
                Vector(0.9f, 0.4f, 0.1f, emissionLight);
                break;
            }
            CreateParticle(BITMAP_SMOKE, Position, o->Angle, emissionLight, 61);
        }
    }
        return true;
    }
    return false;
}

bool CGM_PK_Field::CreateFireSpark(PARTICLE *o)
{
    if (!gMapManager.IsPKField())
    {
        return false;
    }

    o->Type = BITMAP_FIRE_SNUFF;
    o->Scale = Random.RangeFloat(0, 49) / 100.f + 0.4f;
    vec3_t Position;
    Vector(Hero->Object.Position[0] + Random.RangeFloat(-800, 799),
           Hero->Object.Position[1] + Random.RangeFloat(-500, 899),
           Hero->Object.Position[2] + Random.RangeFloat(50, 349), Position);

    VectorCopy(Position, o->Position);
    VectorCopy(Position, o->StartPosition);
    o->Velocity[0] = -Random.RangeFloat(64, 127) * 0.1f;
    if (Position[1] < g_Camera.Position[1] + 400.f)
    {
        o->Velocity[0] = -o->Velocity[0] + 2.2f;
    }
    o->Velocity[1] = Random.RangeFloat(-16, 15) * 0.1f;
    o->Velocity[2] = Random.RangeFloat(-16, 15) * 0.1f;
    o->TurningForce[0] = Random.RangeFloat(-8, 7) * 0.1f;
    o->TurningForce[1] = Random.RangeFloat(-32, 31) * 0.1f;
    o->TurningForce[2] = Random.RangeFloat(-8, 7) * 0.1f;

    Vector(1.f, 0.f, 0.f, o->Light);

    return true;
}

bool CGM_PK_Field::PlayMonsterSound(OBJECT *o)
{
    if (!gMapManager.IsPKField())
        return false;

    float fDis_x, fDis_y;
    fDis_x = o->Position[0] - Hero->Object.Position[0];
    fDis_y = o->Position[1] - Hero->Object.Position[1];
    float fDistance = sqrtf(fDis_x * fDis_x + fDis_y * fDis_y);

    if (fDistance > 500.0f)
        return true;

    switch (o->Type)
    {
    case MODEL_ZOMBIE_FIGHTER: {
        if (MONSTER01_ATTACK1 == o->CurrentAction || MONSTER01_ATTACK2 == o->CurrentAction)
        {
            PlayBuffer(SOUND_PKFIELD_ZOMBIEWARRIOR_ATTACK);
        }
        else if (MONSTER01_SHOCK == o->CurrentAction)
        {
            PlayBuffer(SOUND_PKFIELD_ZOMBIEWARRIOR_DAMAGE01);
        }
        else if (MONSTER01_WALK == o->CurrentAction)
        {
            if (rand_fps_check(20))
            {
                PlayBuffer(SOUND_PKFIELD_ZOMBIEWARRIOR_MOVE01);
            }
        }
        else if (MONSTER01_DIE == o->CurrentAction)
        {
            PlayBuffer(SOUND_PKFIELD_ZOMBIEWARRIOR_DEATH);
        }

        //	SOUND_PKFIELD_ZOMBIEWARRIOR_DAMAGE02,
        //	SOUND_PKFIELD_ZOMBIEWARRIOR_MOVE02,
    }
        return true;
    case MODEL_GLADIATOR: {
        if (MONSTER01_ATTACK1 == o->CurrentAction || MONSTER01_ATTACK2 == o->CurrentAction)
        {
            PlayBuffer(SOUND_PKFIELD_RAISEDGLADIATOR_ATTACK);
        }
        else if (MONSTER01_SHOCK == o->CurrentAction)
        {
            PlayBuffer(SOUND_PKFIELD_RAISEDGLADIATOR_DAMAGE01);
        }
        else if (MONSTER01_WALK == o->CurrentAction)
        {
            if (rand_fps_check(20))
            {
                PlayBuffer(SOUND_PKFIELD_RAISEDGLADIATOR_MOVE01);
            }
        }
        else if (MONSTER01_DIE == o->CurrentAction)
        {
            PlayBuffer(SOUND_PKFIELD_RAISEDGLADIATOR_DEATH);
        }

        //	SOUND_PKFIELD_RAISEDGLADIATOR_DAMAGE02,
        //	SOUND_PKFIELD_RAISEDGLADIATOR_MOVE02,
    }
        return true;
    case MODEL_SLAUGHTERER: {
        if (MONSTER01_ATTACK1 == o->CurrentAction || MONSTER01_ATTACK2 == o->CurrentAction)
        {
            PlayBuffer(SOUND_PKFIELD_ASHESBUTCHER_ATTACK);
        }
        else if (MONSTER01_SHOCK == o->CurrentAction)
        {
            PlayBuffer(SOUND_PKFIELD_ASHESBUTCHER_DAMAGE01);
        }
        else if (MONSTER01_WALK == o->CurrentAction)
        {
            if (rand_fps_check(20))
            {
                PlayBuffer(SOUND_PKFIELD_ASHESBUTCHER_MOVE01);
            }
        }
        else if (MONSTER01_DIE == o->CurrentAction)
        {
            PlayBuffer(SOUND_PKFIELD_ASHESBUTCHER_DEATH);
        }

        //SOUND_PKFIELD_ASHESBUTCHER_DAMAGE02,
        //SOUND_PKFIELD_ASHESBUTCHER_MOVE02,
    }
        return true;
    case MODEL_BLOOD_ASSASSIN:
    case MODEL_CRUEL_BLOOD_ASSASSIN: {
        if (MONSTER01_ATTACK1 == o->CurrentAction || MONSTER01_ATTACK2 == o->CurrentAction)
        {
            PlayBuffer(SOUND_PKFIELD_BLOODASSASSIN_ATTACK);
        }
        else if (MONSTER01_SHOCK == o->CurrentAction)
        {
            PlayBuffer(SOUND_PKFIELD_BLOODASSASSIN_DAMAGE01);
        }
        else if (MONSTER01_WALK == o->CurrentAction)
        {
            if (rand_fps_check(20))
            {
                PlayBuffer(SOUND_PKFIELD_BLOODASSASSIN_MOVE01);
            }
        }
        else if (MONSTER01_DIE == o->CurrentAction)
        {
            PlayBuffer(SOUND_PKFIELD_BLOODASSASSIN_DEDTH);
        }

        //	SOUND_PKFIELD_BLOODASSASSIN_DAMAGE02,
        //	SOUND_PKFIELD_BLOODASSASSIN_MOVE01,
    }
        return true;
    case MODEL_LAVA_GIANT:
    case MODEL_BURNING_LAVA_GIANT: {
        if (MONSTER01_ATTACK1 == o->CurrentAction)
        {
            PlayBuffer(SOUND_PKFIELD_BURNINGLAVAGOLEM_ATTACK01);
        }
        else if (MONSTER01_ATTACK2 == o->CurrentAction)
        {
            PlayBuffer(SOUND_PKFIELD_BURNINGLAVAGOLEM_ATTACK02);
        }
        else if (MONSTER01_SHOCK == o->CurrentAction)
        {
            PlayBuffer(SOUND_PKFIELD_BURNINGLAVAGOLEM_DAMAGE01);
        }
        else if (MONSTER01_WALK == o->CurrentAction)
        {
            if (rand_fps_check(20))
            {
                PlayBuffer(SOUND_PKFIELD_BURNINGLAVAGOLEM_MOVE01);
            }
        }
        else if (MONSTER01_DIE == o->CurrentAction)
        {
            PlayBuffer(SOUND_PKFIELD_BURNINGLAVAGOLEM_DEATH);
        }

        //SOUND_PKFIELD_BURNINGLAVAGOLEM_DAMAGE02,
        //SOUND_PKFIELD_BURNINGLAVAGOLEM_MOVE02,
    }
        return true;
    }

    return false;
}

void CGM_PK_Field::PlayBGM()
{
    if (gMapManager.IsPKField())
    {
        PlayMp3(MUSIC_PKFIELD);
    }
    else
    {
        StopMp3(MUSIC_PKFIELD);
    }
}

std::optional<bool> CGM_PK_Field::ObjectVisibility(const OBJECT &object, bool)
{
    if (object.Type == 16 || object.Type == 67 || object.Type == 68)
        return TestFrustrum2D(object.Position[0] * 0.01f, object.Position[1] * 0.01f, -600.f);
    return std::nullopt;
}

void CGM_PK_Field::UpdateMusic()
{
    PlayBGM();
}

bool CGM_PK_Field::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_PKFIELD) == 0;
}

bool CGM_PK_Field::CreateWeather(PARTICLE *particle, int)
{
    return CreateFireSpark(particle);
}

namespace
{

float ReferenceWave(double endTime, float frames, double millisecondsPerFrame, double frequency,
                    bool cosine)
{
    const double step = frequency * millisecondsPerFrame;
    const double phase = frequency * (endTime - (frames - 1.0) * millisecondsPerFrame * 0.5);
    const double count = std::sin(step * frames * 0.5) / std::sin(step * 0.5);
    return float((cosine ? std::cos(phase) : std::sin(phase)) * count);
}

float HeavenBugTurn(double phase, int index, float frames)
{
    constexpr double PhaseSpeed = 0.0003;
    const double yaw = (34571 + double(index) * 41273) * PhaseSpeed;
    const double pitch = (17732 + double(index) * 5161) * PhaseSpeed;
    const double midpoint = phase - (frames - 1.0) * 0.5;
    const double oscillation = std::sin(yaw + pitch + 2.0 * PhaseSpeed * midpoint) *
                               std::sin(PhaseSpeed * frames) / std::sin(PhaseSpeed);
    return float(0.005 * (oscillation + frames * std::sin(pitch - yaw)));
}

} // namespace

bool IsMount(ITEM *pItem)
{
    if (pItem == NULL)
    {
        return false;
    }

    if (pItem->Type == ITEM_GUARDIAN_ANGEL || pItem->Type == ITEM_IMP ||
        pItem->Type == ITEM_HORN_OF_UNIRIA || pItem->Type == ITEM_HORN_OF_DINORANT ||
        pItem->Type == ITEM_DARK_HORSE_ITEM || pItem->Type == ITEM_DARK_RAVEN_ITEM ||
        pItem->Type == ITEM_HORN_OF_FENRIR)
    {
        return true;
    }

    return false;
}

void SessionGameplayUnit::DeleteBoids()
{
    boidSteeringFrames_ = fishSteeringFrames_ = 0.f;
    for (int i = 0; i < MAX_BOIDS; i++)
    {
        OBJECT *o = &Boids[i];
        o->Live = false;
    }
}

void SessionGameplayUnit::MoveBat(OBJECT *o, float frames)
{
    o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]);
    o->Position[2] += (-absf(sinf(o->Timer + 0.2f * (frames - 1.f))) * 150.f + 350.f);
    o->Timer += 0.2f * frames;
}

void SessionGameplayUnit::MoveButterFly(OBJECT *o, float frames, bool refresh)
{
    if (refresh && Random.FpsCheck(32, 1.f))
    {
        o->Angle[2] = (float)(WorldRandom() % 360);
        o->Direction[2] = (float)(WorldRandom() % 15 - 7) * 1.f;
    }
    if (refresh)
    {
        o->Direction[2] += (float)(WorldRandom() % 15 - 7) * 0.2f;
        float Height = RequestTerrainHeight(o->Position[0], o->Position[1]);
        if (o->Position[2] < Height + 50.f)
        {
            o->Direction[2] *= 0.8f;
            o->Direction[2] += 1.f;
        }
        if (o->Position[2] > Height + 300.f)
        {
            o->Direction[2] *= 0.8f;
            o->Direction[2] -= 1.f;
        }
        o->AmbientVerticalNoise = (float)(WorldRandom() % 15 - 7) * 0.3f;
    }
    o->Position[2] += o->AmbientVerticalNoise * frames;
}

namespace
{
void StartBirdRise(OBJECT &bird)
{
    bird.AI = CharacterMotionDetail::BOID_UP;
    bird.Velocity = 1.1f;
    bird.Direction[2] = 20.f;
    bird.CurrentAction = 0;
}

float AdvanceDescendingBird(OBJECT &bird, float frames, float ground, double &travel)
{
    constexpr float Descent = 20.f;
    bird.Direction[2] = -Descent;
    const float contact = (std::max)(0.f, (bird.Position[2] - ground) / Descent);
    const float step = (std::min)(frames, contact);
    bird.Position[2] -= Descent * step;
    travel += bird.Velocity * step;
    const float contactTolerance =
        4.f * std::numeric_limits<float>::epsilon() * (std::max)({1.f, std::abs(ground), Descent});
    if ((contact - step) * Descent <= contactTolerance)
    {
        bird.Position[2] = ground;
        StartBirdRise(bird);
    }
    return step;
}

float AdvanceRisingBird(OBJECT &bird, float frames, double &travel)
{
    constexpr float CruiseSpeed = 1.f, Deceleration = 0.005f;
    const float cruise = (std::max)(0.f, (bird.Velocity - CruiseSpeed) / Deceleration);
    const float step = (std::min)(frames, cruise);
    travel += step * (bird.Velocity - Deceleration * (step + 1.f) * 0.5f);
    bird.Position[2] += (bird.Direction[2] + bird.AmbientVerticalNoise) * step;
    bird.Velocity -= Deceleration * step;
    if (step == cruise)
    {
        bird.Velocity = CruiseSpeed;
        bird.AI = CharacterMotionDetail::BOID_FLY;
    }
    return step;
}

float AdvanceCruisingBird(OBJECT &bird, float frames, double &travel)
{
    constexpr float FlightFloor = 200.f, FlightCeiling = 600.f, Correction = 10.f;
    bird.Velocity = 1.f;
    if (bird.Position[2] <= FlightFloor)
        bird.Direction[2] = Correction;
    else if (bird.Position[2] >= FlightCeiling)
        bird.Direction[2] = -Correction;
    const float vertical = bird.Direction[2] + bird.AmbientVerticalNoise;
    const float bound = vertical > 0.f ? FlightCeiling : FlightFloor;
    const float contact =
        vertical == 0.f ? frames : (std::max)(0.f, (bound - bird.Position[2]) / vertical);
    const float step = (std::min)(frames, contact);
    bird.Position[2] += vertical * step;
    travel += bird.Velocity * step;
    if (step == contact && vertical != 0.f)
    {
        bird.Position[2] = bound;
        bird.Direction[2] = vertical > 0.f ? -Correction : Correction;
    }
    return step;
}

float AdvanceBirdVertical(OBJECT &bird, float frames, float ground)
{
    double travel = 0.0;
    float remaining = frames;
    while (remaining > 0.f && bird.AI != CharacterMotionDetail::BOID_GROUND)
    {
        if (bird.AI == CharacterMotionDetail::BOID_DOWN)
            remaining -= AdvanceDescendingBird(bird, remaining, ground, travel);
        else if (bird.AI == CharacterMotionDetail::BOID_UP)
            remaining -= AdvanceRisingBird(bird, remaining, travel);
        else
            remaining -= AdvanceCruisingBird(bird, remaining, travel);
    }
    return float(travel / frames);
}
} // namespace

float SessionGameplayUnit::MoveBird(OBJECT *o, float frames, double time, bool refresh)
{
    if (frames <= 0.f)
        return o->Velocity;
    vec3_t heroPosition;
    const double milliseconds = 1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    const float fraction =
        FPS_ANIMATION_FACTOR > 0.f
            ? 1.f - (static_cast<float>((WorldTime - time) / milliseconds) + frames) /
                        FPS_ANIMATION_FACTOR
            : 1.f;
    Hero->Object.MotionTrace.Sample(WorldTime, fraction, Hero->Object.Position, heroPosition);

    if (refresh)
        o->AmbientVerticalNoise = float(WorldRandom() % 16 - 8);
    if (o->AI == CharacterMotionDetail::BOID_FLY && (int)time % 8192 < 2048)
    {
        vec3_t range;
        VectorSubtract(o->Position, heroPosition, range);
        const float distance = std::sqrt(range[0] * range[0] + range[1] * range[1]);
        if (distance >= 200.f && distance <= 400.f)
            o->AI = CharacterMotionDetail::BOID_DOWN;
    }
    if (o->AI == CharacterMotionDetail::BOID_GROUND &&
        (Hero->Object.CurrentAction >= PLAYER_WALK_MALE || (refresh && Random.FpsCheck(256, 1.f))))
        StartBirdRise(*o);
    return AdvanceBirdVertical(*o, frames, RequestTerrainHeight(o->Position[0], o->Position[1]));
}

void SessionGameplayUnit::MoveHeavenBug(OBJECT *o, int index, float frames, double time)
{
    vec3_t heroPosition;
    const double milliseconds = 1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    const float fraction =
        FPS_ANIMATION_FACTOR > 0.f
            ? 1.f - (static_cast<float>((WorldTime - time) / milliseconds)) / FPS_ANIMATION_FACTOR
            : 1.f;
    Hero->Object.MotionTrace.Sample(WorldTime, fraction, Hero->Object.Position, heroPosition);

    const double phase =
        Core::Time::ReferenceFrames(time, sessionKeeper_.ApplicationConfig().legacyReferenceFps);

    if (frames > 0.f)
    {
        const double turn = HeavenBugTurn(phase, index, frames);
        const double rate = turn / frames;
        const double distance =
            std::abs(rate) < 1e-8 ? frames : std::sin(turn * 0.5) / std::sin(rate * 0.5);
        const double heading = o->Angle[2] + (turn - rate) * 0.5;
        o->Position[0] += float(o->Velocity * std::sin(heading) * distance);
        o->Position[1] -= float(o->Velocity * std::cos(heading) * distance);
        o->Angle[2] += float(turn);
    }

    float dx = o->Position[0] - heroPosition[0];
    float dy = o->Position[1] - heroPosition[1];
    float Range = sqrtf(dx * dx + dy * dy);
    if (Range >= 1500.f)
        o->Live = false;
    if (frames > 0.f && Random.FpsCheck(5120, frames))
        o->Live = false;

    if (TheMapProcess().Presentation().finiteAmbientBoids)
    {
        if (o->LifeTime <= 0)
        {
            o->Live = false;
        }
    }
}

void SessionGameplayUnit::AdvanceEagleAnimation(OBJECT &object, BMD &model, float frames,
                                                float speed, bool refresh)
{
    if (frames <= 0.f)
        return;
    if (object.SubType == 0 && refresh && Random.FpsCheck(120, 1.f))
    {
        object.SubType = 1;
        object.AnimationFrame = 0.f;
    }
    const auto phase = [&] {
        return ObjectMotionTrace::AnimationPhase{object.AnimationFrame, object.PriorAnimationFrame,
                                                 object.CurrentAction, object.PriorAction};
    };
    if (model.NumActions == 0 || model.Actions[model.CurrentAction].NumAnimationKeys <= 1 ||
        speed <= 0.f)
    {
        object.MotionTrace.AdvanceAnimation(frames, phase(), 0.f);
        return;
    }
    constexpr float WingTransitionFrame = 24.f;
    const auto &action = model.Actions[model.CurrentAction];
    const float cycle = float(action.NumAnimationKeys - (action.LockPositions ? 1 : 0));
    while (frames > 0.f && object.SubType != 0)
    {
        const float boundary = object.SubType == 1 ? std::min(WingTransitionFrame, cycle) : cycle;
        const float untilBoundary = std::max(0.f, boundary - object.AnimationFrame) / speed;
        const float step = std::min(frames, untilBoundary);
        const auto start = phase();
        model.PlayAnimation(&object.AnimationFrame, &object.PriorAnimationFrame,
                            &object.PriorAction, speed, object.Position, object.Angle, step);
        object.MotionTrace.AdvanceAnimation(step, start, speed * step);
        frames -= step;
        if (step < untilBoundary)
            break;
        if (boundary == cycle)
        {
            object.SubType = 0;
            object.AnimationFrame = 0.f;
        }
        else
        {
            object.SubType = 2;
            object.AnimationFrame = boundary;
        }
    }
    if (object.SubType == 0)
    {
        object.AnimationFrame = 0.f;
        object.MotionTrace.AdvanceAnimation(frames, phase(), 0.f);
    }
    model.PlayAnimation(&object.AnimationFrame, &object.PriorAnimationFrame, &object.PriorAction,
                        0.f, object.Position, object.Angle, 0.f);
}

void SessionGameplayUnit::MoveEagle(OBJECT *o, float frames, double time, bool)
{
    float fSeedAngle = time * 0.001f;
    float fFlyRange = o->Gravity;
    float fAngle = 0;
    if (o->AI == CharacterMotionDetail::BOID_FLY)
    {
        o->HeadAngle[0] = cosf(fSeedAngle) * fFlyRange;
        o->HeadAngle[1] = sinf(fSeedAngle) * fFlyRange;
        fAngle = CreateAngle(o->Position[0], o->Position[1], o->Position[0] + o->HeadAngle[0],
                             o->Position[1] + o->HeadAngle[1]);

        if (o->HeadAngle[2] == 0 && o->HeadAngle[0] > o->HeadAngle[1])
        {
            o->HeadAngle[2] = 1;
        }
        else if (o->HeadAngle[2] == 1 && o->HeadAngle[0] < o->HeadAngle[1])
        {
            o->HeadAngle[2] = 2;
        }
        else if (o->HeadAngle[2] == 2 && o->HeadAngle[0] > o->HeadAngle[1])
        {
            o->AI = CharacterMotionDetail::BOID_GROUND;
            o->HeadAngle[2] = 0;

            o->HeadAngle[0] = sinf(fSeedAngle) * fFlyRange;
            o->HeadAngle[1] = cosf(fSeedAngle) * fFlyRange;
            fAngle = CreateAngle(o->Position[0], o->Position[1], o->Position[0] + o->HeadAngle[0],
                                 o->Position[1] + o->HeadAngle[1]);
        }
    }
    else if (o->AI == CharacterMotionDetail::BOID_GROUND)
    {
        o->HeadAngle[0] = sinf(fSeedAngle) * fFlyRange;
        o->HeadAngle[1] = cosf(fSeedAngle) * fFlyRange;
        fAngle = CreateAngle(o->Position[0], o->Position[1], o->Position[0] + o->HeadAngle[0],
                             o->Position[1] + o->HeadAngle[1]);

        if (o->HeadAngle[2] == 0 && o->HeadAngle[0] < o->HeadAngle[1])
        {
            o->HeadAngle[2] = 1;
        }
        else if (o->HeadAngle[2] == 1 && o->HeadAngle[0] > o->HeadAngle[1])
        {
            o->HeadAngle[2] = 2;
        }
        else if (o->HeadAngle[2] == 2 && o->HeadAngle[0] < o->HeadAngle[1])
        {
            o->AI = CharacterMotionDetail::BOID_FLY;
            o->HeadAngle[2] = 0;

            o->HeadAngle[0] = cosf(fSeedAngle) * fFlyRange;
            o->HeadAngle[1] = sinf(fSeedAngle) * fFlyRange;
            fAngle = CreateAngle(o->Position[0], o->Position[1], o->Position[0] + o->HeadAngle[0],
                                 o->Position[1] + o->HeadAngle[1]);
        }
    }

    const double milliseconds = 1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    const bool flying = o->AI == CharacterMotionDetail::BOID_FLY;
    o->Position[0] += o->Gravity * ReferenceWave(time, frames, milliseconds, 0.001, flying);
    o->Position[1] += o->Gravity * ReferenceWave(time, frames, milliseconds, 0.001, !flying);
    o->Position[2] += ReferenceWave(time, frames, milliseconds, 0.0005, false);
    o->Angle[1] += 0.4f * ReferenceWave(time, frames, milliseconds, 0.001, false);
    o->Angle[2] = fAngle + 270;
}

void SessionGameplayUnit::MoveTornado(OBJECT *o, float frames, bool refresh)
{
    o->Scale = 1.0f;
    if ((refresh && Random.FpsCheck(500, 1.f)))
    {
        o->HeadAngle[0] = (WorldRandom() % 314) / 100.0f;
    }
    o->Position[0] += sinf(o->HeadAngle[0]) * 2.0f * frames;
    o->Position[1] += cosf(o->HeadAngle[0]) * 2.0f * frames;
    o->Angle[2] = 0;
    if (o->BlendMeshLight < 1.0f)
        o->BlendMeshLight = (std::min)(1.f, o->BlendMeshLight + 0.1f * frames);
}

void SessionGameplayUnit::MoveBoidGroup(OBJECT *o, int index, float frames, bool refresh,
                                        float movementVelocity, bool preparedSteering,
                                        double endTime)
{
    if (endTime < 0.0)
        endTime = WorldTime;
    vec3_t heroPosition;
    const double milliseconds = 1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    const float fraction =
        FPS_ANIMATION_FACTOR > 0.f
            ? 1.f - static_cast<float>((WorldTime - endTime) / milliseconds) / FPS_ANIMATION_FACTOR
            : 1.f;
    Hero->Object.MotionTrace.Sample(WorldTime, fraction, Hero->Object.Position, heroPosition);

    if (!g_pOption->GetRenderAllEffects())
    {
        return;
    }

    if (o->AI != CharacterMotionDetail::BOID_GROUND)
    {
        if (refresh && o->Type == MODEL_BUTTERFLY01)
            o->AmbientSteering = Random.FpsCheck(4, 1.f);
        const float previousYaw = o->Angle[2];
        const float turnRate = o->Gravity;
        if (o->Type != MODEL_BUTTERFLY01 || o->AmbientSteering)
        {
            MoveBoid(o, index, Boids, MAX_BOIDS, frames, preparedSteering);
        }

        vec3_t p, Direction;
        if (TheMapProcess().Presentation().waterFish)
        {
            if (o->Timer < 5.f)
            {
                if (index < 35)
                {
                    if (refresh)
                        o->AmbientSpeedNoise = (float)(WorldRandom() % 16 + 8);
                    Vector(movementVelocity * o->AmbientSpeedNoise, 0.f, o->Direction[2],
                           Direction);
                }
                else
                {
                    if (refresh)
                        o->AmbientSpeedNoise = (float)(WorldRandom() % 16 + 16);
                    Vector(movementVelocity * o->AmbientSpeedNoise, 0.f, o->Direction[2],
                           Direction);
                }
                o->Gravity = 15;
            }
            else
            {
                if (refresh)
                    o->AmbientSpeedNoise = (float)(WorldRandom() % 32 + 32);
                Vector(movementVelocity * o->AmbientSpeedNoise, 0.f, o->Direction[2], Direction);
                o->Gravity = 5;
            }
            o->Timer += 0.1f * frames;
            o->Timer = std::fmod(o->Timer, 10.f);
        }
        else
        {
            Vector(movementVelocity * 25.f, 0.f, o->Direction[2], Direction);
        }
        if (o->Type == MODEL_BIRD01 || o->Type == MODEL_CROW)
            Direction[2] = 0.f;
        MoveTurningObject(*o, Direction, previousYaw, turnRate, frames, p);
        o->Direction[0] = o->Position[0] + 3.f * p[0];
        o->Direction[1] = o->Position[1] + 3.f * p[1];

        float dx = o->Position[0] - heroPosition[0];
        float dy = o->Position[1] - heroPosition[1];
        float Range = sqrtf(dx * dx + dy * dy);
        float FlyDistance = 1500.f;
        if (o->Type == MODEL_DRAGON_)
        {
            FlyDistance = 4000.f;
        }
        else if (o->Type == MODEL_BAHAMUT)
        {
            FlyDistance = 3000.f;
        }
        else if (TheMapProcess().Presentation().persistentAmbientBoids)
            ;
        else
        {
            if (Random.FpsCheck(512, frames))
                o->Live = false;
        }
        if (Range >= FlyDistance)
            o->Live = false;
    }
}

void SessionGameplayUnit::AdvanceAmbientBoid(OBJECT *o, int index, float frames,
                                             float animationSpeed, double endTime,
                                             bool preparedSteering)
{
    o->MotionTrace.Begin(WorldTime, frames, o->Position);
    if (endTime < 0.0)
        endTime = WorldTime;
    const double millisecondsPerFrame =
        1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    CharacterMotionDetail::AdvanceAmbientIntervals(
        *o, frames, [&](float step, bool refresh, float remaining) {
            const double time = endTime - remaining * millisecondsPerFrame;
            BMD &model = Models[o->Type];
            model.CurrentAction = o->CurrentAction;
            const ObjectMotionTrace::AnimationPhase phase{o->AnimationFrame, o->PriorAnimationFrame,
                                                          o->CurrentAction, o->PriorAction};
            if (o->Type == MODEL_EAGLE)
                AdvanceEagleAnimation(*o, model, step, animationSpeed, refresh);
            else
            {
                model.PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame, &o->PriorAction,
                                    animationSpeed, o->Position, o->Angle, step);
                o->MotionTrace.AdvanceAnimation(step, phase, animationSpeed * step);
            }
            float movementVelocity = o->Velocity;
            const float startingLife = o->LifeTime - (frames - step - remaining);
            switch (o->Type)
            {
            case MODEL_BAT01:
                MoveBat(o, step);
                break;
            case MODEL_BUTTERFLY01:
                MoveButterFly(o, step, refresh);
                break;
            case MODEL_BIRD01:
            case MODEL_CROW:
                movementVelocity = MoveBird(o, step, time, refresh);
                break;
            case MODEL_SPEARSKILL:
                MoveHeavenBug(o, index, step, time);
                break;
            case MODEL_BAHAMUT:
                movementVelocity =
                    TheMapProcess().Hellas().MoveBigMon(o, step, refresh, startingLife);
                break;
            case MODEL_EAGLE:
                MoveEagle(o, step, time, refresh);
                break;
            case MODEL_MAP_TORNADO:
                MoveTornado(o, step, refresh);
                break;
            }
            if (o->Type != MODEL_BAHAMUT && o->Type != MODEL_BIRD01 && o->Type != MODEL_CROW)
                movementVelocity = o->Velocity;
            MoveBoidGroup(o, index, step, refresh, movementVelocity, preparedSteering, time);
            o->MotionTrace.Advance(step, o->Position);
        });
}

void SessionGameplayUnit::AdvanceFishMotion(OBJECT *o, int index, float frames,
                                            bool preparedSteering)
{
    o->MotionTrace.Begin(WorldTime, frames, o->Position);
    CharacterMotionDetail::AdvanceAmbientIntervals(
        *o, frames, [&](float step, bool refresh, float) {
            if (refresh)
                o->AmbientSpeedNoise = float(WorldRandom() % 4 + 6);
            const float previousYaw = o->Angle[2];
            MoveBoid(o, index, Fishs, MAX_FISHS, step, preparedSteering);
            vec3_t local{o->Velocity * o->AmbientSpeedNoise, 0.f, 0.f}, direction;
            MoveTurningObject(*o, local, previousYaw, o->Gravity, step, direction);
            o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]);
            o->Direction[0] = o->Position[0] + 3.f * direction[0];
            o->Direction[1] = o->Position[1] + 3.f * direction[1];
            // Obstacle attempts are authored once per reference tick, including stalls.
            // Fractional presentation updates must not add extra turns or failure counts.
            if (step == o->AmbientNoiseFrames)
            {
                const int tile = TERRAIN_INDEX_REPEAT(int(o->Position[0] / TERRAIN_SCALE),
                                                      int(o->Position[1] / TERRAIN_SCALE));
                TheMapProcess().MoveAmbientFishTerrain(o, tile);
                if (o->SubType >= 2)
                    o->Live = false;
            }
            o->MotionTrace.TurnYaw(step, previousYaw, o->Angle[2], 0.f);
            o->MotionTrace.Advance(step, o->Position);
        });
}

void SessionGameplayUnit::EmitEventMeteor(float fraction)
{
    vec3_t Position, Angle, Light;
    Hero->Object.MotionTrace.Sample(WorldTime, fraction, Hero->Object.Position, Position);
    Position[0] += WorldRandom() % 600 - 200;
    Position[1] += WorldRandom() % 400 + 200;
    Position[2] += 300.f;
    Vector(0.f, 0.f, 0.f, Angle);
    Vector(1.f, 1.f, 1.f, Light);
    CreateEffect(MODEL_FIRE, Position, Angle, Light, 3);
    PlayBuffer(SOUND_METEORITE01);
}

void SessionGameplayUnit::AdvanceBoidStep(OBJECT *o, int i, int Index, float frames, double endTime)
{
    vec3_t heroPosition;
    const double milliseconds = 1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    const float fraction =
        1.f - static_cast<float>((WorldTime - endTime) / milliseconds) / FPS_ANIMATION_FACTOR;
    Hero->Object.MotionTrace.Sample(WorldTime, fraction, Hero->Object.Position, heroPosition);
    Index = TERRAIN_INDEX_REPEAT(static_cast<int>(heroPosition[0] / TERRAIN_SCALE),
                                 static_cast<int>(heroPosition[1] / TERRAIN_SCALE));

    BMD *b = &Models[o->Type];
    float PlaySpeed = 1.f;
    if (o->Type == MODEL_DRAGON_ || o->Type == MODEL_BAHAMUT)
    {
        PlaySpeed = 0.5f;
    }

    if (EnableEvent != 0 && o->Type == MODEL_DRAGON_)
    {
        SetAction(o, MONSTER01_DIE + 1);
        b->CurrentAction = o->CurrentAction;
        const ObjectMotionTrace::AnimationPhase phase{o->AnimationFrame, o->PriorAnimationFrame,
                                                      o->CurrentAction, o->PriorAction};
        b->PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame, &o->PriorAction, PlaySpeed,
                         o->Position, o->Angle, frames);
        o->MotionTrace.AdvanceAnimation(frames, phase, PlaySpeed * frames);
        AngleMatrix(o->Angle, o->Matrix);
        vec3_t Position, Direction;
        Vector(o->Scale * 40.f, 0.f, 0.f, Position);
        VectorRotate(Position, o->Matrix, Direction);
        VectorAddScaled(o->Position, Direction, o->Position, frames);
        o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) + 300.f;
        o->Position[2] += -absf(sinf(o->Timer + o->Scale * 0.05f * (frames - 1.f))) * 100.f + 100.f;
        o->Timer += o->Scale * 0.05f * frames;
        o->MotionTrace.Advance(frames, o->Position);
        o->LifeTime -= frames;
        if (o->LifeTime <= 0)
            o->Live = false;
        if (Random.FpsCheck(128, frames))
            PlayBuffer(SOUND_MONSTER_BULLATTACK1);
    }
    else
    {
        b->CurrentAction = o->CurrentAction;

        if (TheMapProcess().Presentation().authoredBoidAnimation)
        {
            PlaySpeed = b->Actions[b->CurrentAction].PlaySpeed;
        }
        AdvanceAmbientBoid(o, i, frames, PlaySpeed, endTime, true);

        if (o->LifeTime <= 0 && TheMapProcess().Presentation().waterFish &&
            TerrainWall[Index] == TW_SAFEZONE)
        {
            o->Angle[2] += 180.f;
            if (o->Angle[2] >= 360.f)
                o->Angle[2] -= 360.f;
            o->LifeTime = 10;
            o->SubType++;
        }

        if (o->Type == MODEL_EAGLE || o->Type == MODEL_MAP_TORNADO)
        {
            // do nothing?
        }
        else if (o->SubType >= 2)
        {
            o->Live = false;
        }

        o->LifeTime -= frames;

        float dx = o->Position[0] - heroPosition[0];
        float dy = o->Position[1] - heroPosition[1];
        float Range = sqrtf(dx * dx + dy * dy);
        if (Range < 600)
        {
            if (o->Type == MODEL_BIRD01)
            {
                if (Random.FpsCheck(512, frames))
                    PlayBuffer(SOUND_BIRD01, o);
                if (Random.FpsCheck(512, frames))
                    PlayBuffer(SOUND_BIRD02, o);
            }
            else if (o->Type == MODEL_BAT01)
            {
                if (Random.FpsCheck(256, frames))
                    PlayBuffer(SOUND_BAT01, o);
            }
            else if (o->Type == MODEL_CROW)
            {
                if (TerrainWall[Index] == TW_SAFEZONE)
                {
                    if (Random.FpsCheck(128, frames))
                        PlayBuffer(SOUND_CROW, o);
                }
            }
        }
    }
    Alpha(o, frames);
}

void SessionGameplayUnit::MoveBoids()
{
    if (!g_pOption->GetRenderAllEffects())
    {
        return;
    }

    if (EnableEvent != 0)
    {
        OBJECT *o = &Hero->Object;
        vec3_t Position, Angle, Light;
        for (auto birth : Emissions(FPS_ANIMATION_FACTOR / 40.f))
            EmitEventMeteor(birth.FrameFraction());
        Vector(-0.3f, -0.3f, -0.2f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 16, PrimaryTerrainLight);
    }

    int Index = TERRAIN_INDEX_REPEAT((int)(Hero->Object.Position[0] / TERRAIN_SCALE),
                                     (int)(Hero->Object.Position[1] / TERRAIN_SCALE));
    int boidCount = 0;
    for (int i = 0; i < MAX_BOIDS; i++)
    {
        bool bCreate;
        if (!TheMapProcess().PrepareAmbientBoidSlot(i, bCreate))
            break;
        boidCount = i + 1;
        OBJECT *o = &Boids[i];
        if (!o->Live && bCreate)
        {
            o->MotionTrace.Reset();
            o->AmbientNoiseFrames = 0.f;
            if (EnableEvent != 0)
            {
                if (rand_fps_check(300))
                {
                    if (!OpenMonsterModel(MONSTER_MODEL_DRAGON))
                    {
                        continue;
                    }
                    o->Live = true;
                    o->Type = MODEL_DRAGON_;
                    o->Scale = (float)(WorldRandom() % 3 + 6) * 0.1f;
                    o->Alpha = 1.f;
                    o->AlphaTarget = 1.f;
                    o->Velocity = 0.5f;
                    o->LightEnable = true;
                    o->AlphaEnable = false;
                    o->SubType = 0;
                    o->HiddenMesh = -1;
                    o->BlendMesh = -1;
                    o->LifeTime = 128 + WorldRandom() % 128;
                    o->Timer = (float)(WorldRandom() % 10) * 0.1f;
                    Vector(0.f, 0.f, -90.f, o->Angle);
                    if (EnableEvent == 3)
                    {
                        o->SubType = 1;
                    }
                    Vector(Hero->Object.Position[0] + (float)(WorldRandom() % 600 - 100),
                           Hero->Object.Position[1] + (float)(WorldRandom() % 400 + 200),
                           Hero->Object.Position[2] + 300.f, o->Position);
                }
            }
            else
            {
                TheMapProcess().CreateAmbientBoid(o, i, Index);
            }
            if (o->Live)
                o->AmbientFlockHeading = o->Angle[2];
        }

        if (o->Live)
            o->MotionTrace.Begin(WorldTime, FPS_ANIMATION_FACTOR, o->Position);
    }
    float remaining = FPS_ANIMATION_FACTOR;
    const double milliseconds = 1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    while (remaining > 0.f)
    {
        if (boidSteeringFrames_ <= 0.f)
        {
            RefreshFlockSteering(Boids, boidCount);
            boidSteeringFrames_ = 1.f;
        }
        const float step = (std::min)(remaining, boidSteeringFrames_);
        const double endTime = WorldTime - (remaining - step) * milliseconds;
        for (int index = 0; index < boidCount; ++index)
            if (Boids[index].Live)
                AdvanceBoidStep(&Boids[index], index, Index, step, endTime);
        remaining -= step;
        boidSteeringFrames_ -= step;
    }
    for (int index = 0; index < boidCount; ++index)
        if (Boids[index].Live)
            AdvanceBoidVisual(Boids[index]);
}

void SessionVisualUnit::AdvanceBoidVisual(OBJECT &object)
{
    if (!object.Live ||
        !TestFrustrum2D(object.Position[0] * 0.01f, object.Position[1] * 0.01f, -20.f))
        return;
    if (object.Type != MODEL_MAP_TORNADO && (object.Type != MODEL_DRAGON_ || EnableEvent == 0))
        return;
    const CharacterMotionDetail::BoidDrawingHeading drawingHeading(object);
    if (object.Type == MODEL_MAP_TORNADO)
    {
        CreateParticleFpsChecked(BITMAP_CLOUD, object.Position, object.Angle, object.Light, 18,
                                 object.Scale, &object);
        return;
    }
    PrepareWorldObjectPose(object);
    vec3_t offset{0.f, -50.f, 0.f}, position, light{1.f, 1.f, 1.f};
    Models[object.Type].TransformPosition(BoneTransform[11], offset, position);
    CreateParticleFpsChecked(BITMAP_FIRE, position, object.Angle, light);
}

bool SessionGameplayUnit::AdvanceFishAnimation(OBJECT &object)
{
    return AdvanceFishAnimation(object, FPS_ANIMATION_FACTOR);
}

bool SessionGameplayUnit::AdvanceFishAnimation(OBJECT &object, float frames)
{
    const bool moving =
        (object.Type >= MODEL_FISH01 && object.Type <= MODEL_FISH01 + 10) || object.LifeTime > 0;
    return AdvanceFishAnimation(object, frames, moving);
}

bool SessionGameplayUnit::AdvanceFishAnimation(OBJECT &object, float frames, bool moving)
{
    // Hellas uses model-less fish as moving joint anchors.
    if (object.Type == -1)
        return moving;
    const bool walking = moving && (object.Type == MODEL_BUG01 || object.Type == MODEL_SCOLPION);
    SetAction(&object, walking ? 1 : 0);
    BMD &model = Models[object.Type];
    model.CurrentAction = object.CurrentAction;
    const ObjectMotionTrace::AnimationPhase phase{object.AnimationFrame, object.PriorAnimationFrame,
                                                  object.CurrentAction, object.PriorAction};
    model.PlayAnimation(&object.AnimationFrame, &object.PriorAnimationFrame, &object.PriorAction,
                        object.Velocity * 0.5f, object.Position, object.Angle, frames);
    object.MotionTrace.AdvanceAnimation(frames, phase, object.Velocity * 0.5f * frames);
    if (object.Type == MODEL_FISH01 + 7 || object.Type == MODEL_FISH01 + 8)
    {
        object.BlendMeshLight = sinf(object.Timer + 0.1f * (frames - 1.f)) * 0.4f + 0.5f;
        object.Timer += 0.1f * frames;
    }
    return moving;
}

void SessionGameplayUnit::AdvanceFishStep(OBJECT *o, int i, float frames, double endTime)
{
    vec3_t heroPosition;
    const double milliseconds = 1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    const float fraction =
        1.f - static_cast<float>((WorldTime - endTime) / milliseconds) / FPS_ANIMATION_FACTOR;
    Hero->Object.MotionTrace.Sample(WorldTime, fraction, Hero->Object.Position, heroPosition);
    const bool swimming = o->Type >= MODEL_FISH01 && o->Type <= MODEL_FISH01 + 10;
    const float movingFrames = swimming ? frames : (std::min)(frames, (std::max)(0.f, o->LifeTime));
    if (movingFrames > 0.f)
    {
        AdvanceFishAnimation(*o, movingFrames, true);
        AdvanceFishMotion(o, i, movingFrames, true);

        if (o->Type == MODEL_BUG01 + 1 || o->Type == MODEL_SCOLPION)
        {
            VectorCopy(o->Position, o->EyeLeft);
        }

        float dx = o->Position[0] - heroPosition[0];
        float dy = o->Position[1] - heroPosition[1];
        float Range = sqrtf(dx * dx + dy * dy);
        if (Range >= 1500.f)
            o->Live = false;

        if (Range < 600.f)
            if (o->Type == MODEL_RAT01 && Random.FpsCheck(256, frames))
                PlayBuffer(SOUND_RAT01, o);
    }

    if (movingFrames < frames)
    {
        AdvanceFishAnimation(*o, frames - movingFrames, false);
        o->MotionTrace.Advance(frames - movingFrames, o->Position);
    }
    o->LifeTime -= frames;
    if (o->LifeTime <= 0)
    {
        if (o->Type == MODEL_BUG01)
        {
        }
        else
        {
            if (Random.FpsCheck(64, frames))
                o->LifeTime = WorldRandom() % 128;
        }
    }
    Alpha(o, frames);
}

void SessionGameplayUnit::MoveFishs()
{
    if (!g_pOption->GetRenderAllEffects())
    {
        return;
    }

    const auto *definition = sessionKeeper_.WorldContextDefinition();
    const int fishCount =
        definition && definition->presentation.fullAmbientFishPool ? MAX_FISHS : 3;
    for (int i = 0; i < fishCount; i++)
    {
        OBJECT *o = &Fishs[i];
        if (!o->Live)
        {
            o->MotionTrace.Reset();
            o->AmbientNoiseFrames = 0.f;
            Vector(Hero->Object.Position[0] + (float)(WorldRandom() % 1024 - 512),
                   Hero->Object.Position[1] + (float)(WorldRandom() % 1024 - 512),
                   Hero->Object.Position[2], o->Position);
            int Index = TERRAIN_INDEX_REPEAT((int)(o->Position[0] / TERRAIN_SCALE),
                                             (int)(o->Position[1] / TERRAIN_SCALE));
            TheMapProcess().CreateAmbientFish(o, Index);
            if (o->Live)
                o->AmbientFlockHeading = o->Angle[2];
        }
        if (o->Live)
            o->MotionTrace.Begin(WorldTime, FPS_ANIMATION_FACTOR, o->Position);
    }
    float remaining = FPS_ANIMATION_FACTOR;
    const double milliseconds = 1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    while (remaining > 0.f)
    {
        if (fishSteeringFrames_ <= 0.f)
        {
            RefreshFlockSteering(Fishs, fishCount);
            fishSteeringFrames_ = 1.f;
        }
        const float step = (std::min)(remaining, fishSteeringFrames_);
        const double endTime = WorldTime - (remaining - step) * milliseconds;
        for (int index = 0; index < fishCount; ++index)
            if (Fishs[index].Live)
                AdvanceFishStep(&Fishs[index], index, step, endTime);
        remaining -= step;
        fishSteeringFrames_ -= step;
    }
}
