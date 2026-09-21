#include "domain/MapSimulation.h"
#include "network/generated/PacketFunctions_ClientToServer.h"
#include "support/CoreMath.h"
#include "data/WorldData.h"
#include "session/SessionGameplay.h"
#include "domain/Events.h"
#include "domain/Quests.h"
#include "session/SessionKeeper.h"
#include "render/ModelResources.h"
#include "domain/CharacterSystem.h"
#include "domain/CharacterPresentation.h"
#include "render/Textures.h"
#include "domain/WorldSimulation.h"
#include "render/World.h"
#include "render/Terrain.h"
#include "domain/EffectsUpdate.h"
#include "ui/session/UiSessionLogic.h"
#include "app/ApplicationAudio.h"
#include "data/GameData.h"
#include "ui/runtime/UiControls.h"
#include "ui/features/Items/ItemsLogic.h"
#include "I18N/All.h"
#include "support/Camera.h"
#include "render/ModelGeometry.h"
#include "session/SessionPresentation.h"
#include "session/SessionRender.h"
#include "render/Text.h"
#include "support/Scenes.h"
#include "domain/MovementAI.h"
#include "domain/ItemsSkills.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationKeeper.h"
#include "data/CharacterData.h"
#include "data/ItemData.h"
#include "domain/ChatSocial.h"
#include "session/SessionAudio.h"
#include "session/SessionWorkspace.h"
#include "domain/WorldPhysics.h"
#include "data/Localization.h"
#include "app/ApplicationConfigScheduling.h"
#include "data/ResourceData.h"
#include "render/Sprites.h"
#include "domain/Shop.h"
#include "session/SessionNetwork.h"
#include "app/ApplicationNetwork.h"
#include "domain/Guild.h"
#include "ui/features/Dialogs/DialogsLogic.h"

namespace
{
bool IntersectTriangle(const float *start, const float *target, const vec3_t *vertices, int third,
                       float &closest, vec3_t hit)
{
    vec3_t normal, direction, offset, edge1, edge2;
    VectorSubtract(vertices[third == 2 ? 1 : 2], vertices[0], edge1);
    VectorSubtract(vertices[third], vertices[0], edge2);
    CrossProduct(edge1, edge2, normal);
    VectorSubtract(target, start, direction);
    const float denominator = DotProduct(normal, direction);
    if (denominator >= 0.f)
        return false;
    VectorSubtract(vertices[0], start, offset);
    const float distance = DotProduct(normal, offset) / denominator;
    if (distance < 0.f || distance > closest)
        return false;
    vec3_t position;
    VectorAddScaled(start, direction, position, distance);
    const float x = position[0] - vertices[0][0], y = position[1] - vertices[0][1];
    if (x < 0.f || x > TERRAIN_SCALE || y < 0.f || y > TERRAIN_SCALE ||
        (third == 2 ? y > x : x > y))
        return false;
    closest = distance;
    VectorCopy(position, hit);
    return true;
}

bool PickTerrainTile(const WorldReadView &world, const float *start, const float *target, int tileX,
                     int tileY, LogicPickResult &result)
{
    const auto cell = TerrainCell::TryCreate(static_cast<TerrainCell::Coordinate>(tileX),
                                             static_cast<TerrainCell::Coordinate>(tileY));
    const auto surface = world.TerrainPickSurfaceAt(*cell);
    if (!surface)
        return false;
    const auto heights = surface->HeightsValue();
    const auto walls = surface->WallBitsValue();
    const float x = tileX * TERRAIN_SCALE, y = tileY * TERRAIN_SCALE;
    vec3_t vertices[4] = {{x, y, heights[0]},
                          {x + TERRAIN_SCALE, y, heights[1]},
                          {x + TERRAIN_SCALE, y + TERRAIN_SCALE, heights[2]},
                          {x, y + TERRAIN_SCALE, heights[3]}};
    for (int corner = 0; corner < 4; ++corner)
        if ((walls[corner] & TW_HEIGHT) != 0)
            vertices[corner][2] = surface->SpecialHeight();
    float closest = 1.f;
    vec3_t hit;
    const bool first = IntersectTriangle(start, target, vertices, 2, closest, hit);
    const bool second = IntersectTriangle(start, target, vertices, 3, closest, hit);
    if (!first && !second)
        return false;
    result = {true, hit[0], hit[1], hit[2], static_cast<float>(tileX), static_cast<float>(tileY)};
    return true;
}

bool ClipTerrainAxis(float start, float direction, float &enter, float &leave)
{
    constexpr float TerrainExtent = TERRAIN_SIZE * TERRAIN_SCALE;
    if (direction == 0.f)
        return start >= 0.f && start < TerrainExtent;
    float first = -start / direction, last = (TerrainExtent - start) / direction;
    if (first > last)
        std::swap(first, last);
    enter = (std::max)(enter, first);
    leave = (std::min)(leave, last);
    return enter <= leave;
}
} // namespace

LogicPickResult GameLogic::Interaction::PickTerrain(const WorldReadView &world, const float *start,
                                                    const float *target) noexcept
{
    const float dx = target[0] - start[0], dy = target[1] - start[1];
    float enter = 0.f, leave = 1.f;
    if (!ClipTerrainAxis(start[0], dx, enter, leave) ||
        !ClipTerrainAxis(start[1], dy, enter, leave))
        return {};
    int x =
        std::clamp(static_cast<int>((start[0] + dx * enter) / TERRAIN_SCALE), 0, TERRAIN_SIZE - 1);
    int y =
        std::clamp(static_cast<int>((start[1] + dy * enter) / TERRAIN_SCALE), 0, TERRAIN_SIZE - 1);
    const int stepX = dx >= 0.f ? 1 : -1, stepY = dy >= 0.f ? 1 : -1;
    constexpr float Infinity = std::numeric_limits<float>::infinity();
    const float deltaX = dx == 0.f ? Infinity : TERRAIN_SCALE / std::abs(dx);
    const float deltaY = dy == 0.f ? Infinity : TERRAIN_SCALE / std::abs(dy);
    float nextX = dx == 0.f ? Infinity : ((x + (stepX > 0)) * TERRAIN_SCALE - start[0]) / dx;
    float nextY = dy == 0.f ? Infinity : ((y + (stepY > 0)) * TERRAIN_SCALE - start[1]) / dy;
    LogicPickResult result;
    // Traverse only cells crossed by the ray, in near-to-far order.
    while (x >= 0 && x < TERRAIN_SIZE && y >= 0 && y < TERRAIN_SIZE)
    {
        if (PickTerrainTile(world, start, target, x, y, result))
            return result;
        if ((std::min)(nextX, nextY) > leave)
            break;
        if (nextX < nextY)
        {
            x += stepX;
            nextX += deltaX;
        }
        else
        {
            y += stepY;
            nextY += deltaY;
        }
    }
    return {};
}

// Model creators are shared across maps. Route by the requested monster type,
// retaining only the actual candidates in their established precedence. Map-local
// guards stay in their concrete creators (for example Crying Wolf and PK Field).
CHARACTER *MapProcess::CreateMonster(int type, int x, int y, int key)
{
    switch (type)
    {
    case MONSTER_STONE_STATUE:
    case MONSTER_MU_ALLIES_GENERAL:
    case MONSTER_ILLUSION_ELDER:
    case MONSTER_ALLIANCE_ITEM_STORAGE:
    case MONSTER_ILLUSION_ITEM_STORAGE:
    case MONSTER_MIRAGE:
    case MONSTER_ILLUSION_SORCERER_SPIRIT1_LIGHTNING:
    case MONSTER_ILLUSION_SORCERER_SPIRIT2_LIGHTNING:
    case MONSTER_ILLUSION_SORCERER_SPIRIT3_LIGHTNING:
    case MONSTER_ILLUSION_SORCERER_SPIRIT4_LIGHTNING:
    case MONSTER_ILLUSION_SORCERER_SPIRIT5_LIGHTNING:
    case MONSTER_ILLUSION_SORCERER_SPIRIT6_LIGHTNING:
    case MONSTER_ILLUSION_SORCERER_SPIRIT1_ICE:
    case MONSTER_ILLUSION_SORCERER_SPIRIT2_ICE:
    case MONSTER_ILLUSION_SORCERER_SPIRIT3_ICE:
    case MONSTER_ILLUSION_SORCERER_SPIRIT4_ICE:
    case MONSTER_ILLUSION_SORCERER_SPIRIT5_ICE:
    case MONSTER_ILLUSION_SORCERER_SPIRIT6_ICE:
    case MONSTER_ILLUSION_SORCERER_SPIRIT1_POISON:
    case MONSTER_ILLUSION_SORCERER_SPIRIT2_POISON:
    case MONSTER_ILLUSION_SORCERER_SPIRIT3_POISON:
    case MONSTER_ILLUSION_SORCERER_SPIRIT4_POISON:
    case MONSTER_ILLUSION_SORCERER_SPIRIT5_POISON:
    case MONSTER_ILLUSION_SORCERER_SPIRIT6_POISON:
    case MONSTER_MU_ALLIES:
    case MONSTER_ILLUSION_SORCERER: {
        return cursedTemple_->CreateCharacters(static_cast<EMonsterType>(type), x, y, key);
    }
    case MONSTER_DEATH_ANGEL_1:
    case MONSTER_DEATH_ANGEL_2:
    case MONSTER_DEATH_ANGEL_3:
    case MONSTER_DEATH_ANGEL_4:
    case MONSTER_DEATH_ANGEL_5:
    case MONSTER_DEATH_ANGEL_6:
    case MONSTER_DEATH_ANGEL_7:
    case MONSTER_DEATH_CENTURION_1:
    case MONSTER_DEATH_CENTURION_2:
    case MONSTER_DEATH_CENTURION_3:
    case MONSTER_DEATH_CENTURION_4:
    case MONSTER_DEATH_CENTURION_5:
    case MONSTER_DEATH_CENTURION_6:
    case MONSTER_DEATH_CENTURION_7:
    case MONSTER_BLOOD_SOLDIER_1:
    case MONSTER_BLOOD_SOLDIER_2:
    case MONSTER_BLOOD_SOLDIER_3:
    case MONSTER_BLOOD_SOLDIER_4:
    case MONSTER_BLOOD_SOLDIER_5:
    case MONSTER_BLOOD_SOLDIER_6:
    case MONSTER_BLOOD_SOLDIER_7:
    case MONSTER_AEGIS_1:
    case MONSTER_AEGIS_2:
    case MONSTER_AEGIS_3:
    case MONSTER_AEGIS_4:
    case MONSTER_AEGIS_5:
    case MONSTER_AEGIS_6:
    case MONSTER_AEGIS_7:
    case MONSTER_ROGUE_CENTURION_1:
    case MONSTER_ROGUE_CENTURION_2:
    case MONSTER_ROGUE_CENTURION_3:
    case MONSTER_ROGUE_CENTURION_4:
    case MONSTER_ROGUE_CENTURION_5:
    case MONSTER_ROGUE_CENTURION_6:
    case MONSTER_ROGUE_CENTURION_7:
    case MONSTER_NECRON_1:
    case MONSTER_NECRON_2:
    case MONSTER_NECRON_3:
    case MONSTER_NECRON_4:
    case MONSTER_NECRON_5:
    case MONSTER_NECRON_6:
    case MONSTER_NECRON_7:
    case MONSTER_SCHRIKER_1:
    case MONSTER_SCHRIKER_2:
    case MONSTER_SCHRIKER_3:
    case MONSTER_SCHRIKER_4:
    case MONSTER_SCHRIKER_5:
    case MONSTER_SCHRIKER_6:
    case MONSTER_SCHRIKER_7:
    case MONSTER_ILLUSION_OF_KUNDUN_1:
    case MONSTER_ILLUSION_OF_KUNDUN_2:
    case MONSTER_ILLUSION_OF_KUNDUN_3:
    case MONSTER_ILLUSION_OF_KUNDUN_4:
    case MONSTER_ILLUSION_OF_KUNDUN_5:
    case MONSTER_ILLUSION_OF_KUNDUN_6:
    case MONSTER_ILLUSION_OF_KUNDUN_7: {
        return hellas_->CreateHellasMonster(static_cast<EMonsterType>(type), x, y, key);
    }
    case MONSTER_TRAP:
    case MONSTER_SHIELD:
    case MONSTER_CROWN:
    case MONSTER_CROWN_SWITCH1:
    case MONSTER_CROWN_SWITCH2:
    case MONSTER_CASTLE_GATE_SWITCH:
    case MONSTER_GUARD:
    case MONSTER_SLINGSHOT_ATTACK:
    case MONSTER_SLINGSHOT_DEFENSE:
    case MONSTER_SENIOR:
    case MONSTER_GUARDSMAN:
    case MONSTER_CASTLE_GATE1:
    case MONSTER_LIFE_STONE:
    case MONSTER_GUARDIAN_STATUE:
    case MONSTER_GUARDIAN:
    case MONSTER_BATTLE_GUARD1:
    case MONSTER_BATTLE_GUARD2:
    case MONSTER_CANON_TOWER: {
        return battleCastle_->CreateBattleCastleMonster(static_cast<EMonsterType>(type), x, y, key);
    }
    case MONSTER_LIZARD_WARRIOR:
    case MONSTER_FIRE_GOLEM:
    case MONSTER_QUEEN_BEE:
    case MONSTER_GIGAS_GOLEM:
    case MONSTER_POISON_GOLEM:
    case MONSTER_AXE_HERO:
    case MONSTER_AXE_WARRIOR:
    case MONSTER_EROHIM:
    case MONSTER_PK_DARK_KNIGHT: {
        return huntingGround_->CreateHuntingGroundMonster(type, x, y, key);
    }
    case MONSTER_WEREWOLFHERO:
    case MONSTER_VALAM:
    case MONSTER_SOLAM:
    case MONSTER_HAMMER_SCOUT: {
        if (CHARACTER *character = cryingWolf2nd_->CreateCryingWolf2ndMonster(type, x, y, key))
            return character;
        return crywolf1st_->CreateCryWolf1stMonster(type, x, y, key);
    }
    case MONSTER_SCOUT: {
        return cryingWolf2nd_->CreateCryingWolf2ndMonster(type, x, y, key);
    }
    case MONSTER_LANCE_SCOUT:
    case MONSTER_BOW_SCOUT:
    case MONSTER_WEREWOLF:
    case MONSTER_SCOUTHERO:
    case MONSTER_BALRAM:
    case MONSTER_SORAM:
    case MONSTER_BALGASS:
    case MONSTER_DEATH_SPIRIT:
    case MONSTER_DARK_ELF:
    case MONSTER_DARKELF:
    case MONSTER_BALLISTA: {
        return crywolf1st_->CreateCryWolf1stMonster(type, x, y, key);
    }
    case MONSTER_WITCH_QUEEN:
    case MONSTER_BLUE_GOLEM:
    case MONSTER_DEATH_RIDER:
    case MONSTER_FOREST_ORC:
    case MONSTER_DEATH_TREE:
    case MONSTER_HELL_MAINE:
    case MONSTER_BLOODY_ORC:
    case MONSTER_BLOODY_DEATH_RIDER:
    case MONSTER_BLOODY_GOLEM:
    case MONSTER_BLOODY_WITCH_QUEEN: {
        return aida_->CreateAidaMonster(type, x, y, key);
    }
    case MONSTER_BERSERK:
    case MONSTER_BERSERKER:
    case MONSTER_GIGANTIS2:
    case MONSTER_GIGANTIS:
    case MONSTER_GENOCIDER:
    case MONSTER_SPLINTER_WOLF:
    case MONSTER_IRON_RIDER:
    case MONSTER_BLADE_HUNTER:
    case MONSTER_SATYROS:
    case MONSTER_KENTAUROS:
    case MONSTER_BERSERKER_WARRIOR:
    case MONSTER_KENTAUROS_WARRIOR:
    case MONSTER_GIGANTIS_WARRIOR:
    case MONSTER_GENOCIDER_WARRIOR: {
        return kanturu1st_->CreateKanturu1stMonster(type, x, y, key);
    }
    case MONSTER_PERSONA_DS7:
    case MONSTER_PERSONA:
    case MONSTER_TWIN_TALE:
    case MONSTER_DREADFEAR2:
    case MONSTER_DREADFEAR:
    case MONSTER_GATEWAY_MACHINE:
    case MONSTER_CANON_TRAP: {
        return kanturu2nd_->Create_Kanturu2nd_Monster(type, x, y, key);
    }
    case MONSTER_NIGHTMARE:
    case MONSTER_MAYA_HAND_LEFT:
    case MONSTER_MAYA_HAND_RIGHT:
    case MONSTER_MAYA: {
        return kanturu3rd_->CreateKanturu3rdMonster(type, x, y, key);
    }
    case MONSTER_BALRAM_TRAINEE:
    case MONSTER_BALRAM_TRAINEE_SOLDIER:
    case MONSTER_DEATH_SPIRIT_TRAINEE_SOLDIER:
    case MONSTER_SORAM_TRAINEE:
    case MONSTER_SORAM_TRAINEE_SOLDIER:
    case MONSTER_DARK_ELF_TRAINEE_SOLDIER: {
        return thirdChange_->CreateBalgasBarrackMonster(type, x, y, key);
    }
    case MONSTER_SILVIA:
    case MONSTER_RHEA:
    case MONSTER_MARCE:
    case MONSTER_STRANGE_RABBIT:
    case MONSTER_POLLUTED_BUTTERFLY:
    case MONSTER_HIDEOUS_RABBIT:
    case MONSTER_WEREWOLF2:
    case MONSTER_CURSED_LICH:
    case MONSTER_TOTEM_GOLEM:
    case MONSTER_GRIZZLY:
    case MONSTER_CAPTAIN_GRIZZLY: {
        return newTown_->CreateMonster(type, x, y, key);
    }
    case MONSTER_SAPIUNUS:
    case MONSTER_SAPIDUO:
    case MONSTER_SAPITRES:
    case MONSTER_SHADOW_PAWN:
    case MONSTER_SHADOW_KNIGHT:
    case MONSTER_SHADOW_LOOK:
    case MONSTER_THUNDER_NAPIN:
    case MONSTER_GHOST_NAPIN:
    case MONSTER_BLAZE_NAPIN:
    case MONSTER_MEDUSA:
    case MONSTER_SAPI_QUEEN:
    case MONSTER_SAPI_QUEEN2:
    case MONSTER_ICE_NAPIN:
    case MONSTER_SHADOW_MASTER: {
        return swampOfQuiet_->CreateMonster(type, x, y, key);
    }
    case MONSTER_ICE_WALKER:
    case MONSTER_GIANT_MAMMOTH:
    case MONSTER_ICE_GIANT:
    case MONSTER_COOLUTIN:
    case MONSTER_IRON_KNIGHT:
    case MONSTER_SELUPAN:
    case MONSTER_SPIDER_EGGS_1:
    case MONSTER_SPIDER_EGGS_2:
    case MONSTER_SPIDER_EGGS_3:
    case MONSTER_DARK_MAMMOTH:
    case MONSTER_DARK_GIANT:
    case MONSTER_DARK_COOLUTIN:
    case MONSTER_DARK_IRON_KNIGHT: {
        return raklion_->CreateMonster(type, x, y, key);
    }
    case 465:
    case 467: {
        return santaTown_->CreateMonster(type, x, y, key);
    }
    case MONSTER_ZOMBIE_FIGHTER:
    case MONSTER_ZOMBIER:
    case MONSTER_GLADIATOR:
    case MONSTER_HELL_GLADIATOR:
    case MONSTER_SLAUGHTERER:
    case MONSTER_ASH_SLAUGHTERER:
    case MONSTER_BLOOD_ASSASSIN:
    case MONSTER_CRUEL_BLOOD_ASSASSIN:
    case MONSTER_COLD_BLOODED_ASSASSIN:
    case MONSTER_BURNING_LAVA_GIANT:
    case MONSTER_LAVA_GIANT:
    case MONSTER_RUTHLESS_LAVA_GIANT: {
        return pkField_->CreateMonster(type, x, y, key);
    }
    case MONSTER_TERRIBLE_BUTCHER:
    case MONSTER_MAD_BUTCHER:
    case MONSTER_ICE_WALKER2:
    case MONSTER_LARVA2:
    case MONSTER_DOPPELGANGER:
    case MONSTER_DOPPELGANGER_ELF:
    case MONSTER_DOPPELGANGER_KNIGHT:
    case MONSTER_DOPPELGANGER_WIZARD:
    case MONSTER_DOPPELGANGER_MG:
    case MONSTER_DOPPELGANGER_DL:
    case MONSTER_DOPPELGANGER_SUM: {
        return doppelGanger1_->CreateMonster(type, x, y, key);
    }
    case MONSTER_RAYMOND:
    case MONSTER_LUCAS:
    case MONSTER_FRED:
    case MONSTER_DEVIL_LORD:
    case MONSTER_QUARTER_MASTER:
    case MONSTER_COMBAT_INSTRUCTOR:
    case MONSTER_DEFENDER:
    case MONSTER_FORSAKER:
    case MONSTER_OCELOT_THE_LORD:
    case MONSTER_ERIC_THE_GUARD:
    case MONSTER_EVIL_GATE:
    case MONSTER_LION_GATE:
    case MONSTER_STATUE: {
        return empireGuardian1_->CreateMonster(type, x, y, key);
    }
    case MONSTER_HAMMERIZE:
    case MONSTER_ATICLES_HEAD:
    case MONSTER_DARK_GHOST: {
        return empireGuardian2_->CreateMonster(type, x, y, key);
    }
    case MONSTER_DUAL_BERSERKER:
    case MONSTER_BANSHEE:
    case MONSTER_HEAD_MOUNTER: {
        return empireGuardian3_->CreateMonster(type, x, y, key);
    }
    case MONSTER_GAYION_THE_GLADIATOR:
    case MONSTER_JERRY:
    case MONSTER_ADVISER_JERINTEU:
    case MONSTER_STAR_GATE:
    case MONSTER_RUSH_GATE: {
        return empireGuardian4_->CreateMonster(type, x, y, key);
    }
    case MONSTER_VENOMOUS_CHAIN_SCORPION:
    case MONSTER_BONE_SCORPION:
    case MONSTER_ORCUS:
    case MONSTER_GOLLOCK:
    case MONSTER_CRYPTA:
    case MONSTER_CRYPOS:
    case MONSTER_CONDRA:
    case MONSTER_NARCONDRA: {
#ifdef ASG_ADD_MAP_KARUTAN
        if (CHARACTER *character = karutan_->CreateMonster(type, x, y, key))
            return character;
#endif
        return nullptr;
    }
    default:
        return nullptr;
    }
}

void MapProcess::AdvanceMonsterState(CHARACTER &character, BMD &model)
{
    auto &object = character.Object;
    switch (object.Type)
    {
    case MODEL_BALLISTA:
        crywolf1st_->AdvanceBallistaState(object);
        break;
    case MODEL_MAD_BUTCHER:
    case MODEL_TERRIBLE_BUTCHER:
        if (gMapManager.ContextMap() >= WD_65DOPPLEGANGER1 &&
            gMapManager.ContextMap() <= WD_68DOPPLEGANGER4 &&
            (object.CurrentAction == MONSTER01_STOP1 || object.CurrentAction == MONSTER01_STOP2 ||
             object.CurrentAction == MONSTER01_ATTACK1 ||
             object.CurrentAction == MONSTER01_ATTACK2))
            object.CurrentAction = MONSTER01_WALK;
        break;
    case MODEL_DREADFEAR:
        if (object.CurrentAction == MONSTER01_DIE)
            object.Alpha =
                std::max(0.f, object.Alpha - 0.1f * sessionKeeper_.FrameAnimationFactor());
        break;
    case MODEL_BLADE_HUNTER:
        if (object.CurrentAction == MONSTER01_DIE)
        {
            object.Alpha = std::max(0.f, object.Alpha);
            object.m_bRenderShadow = false;
        }
        break;
    case MODEL_FIRE_GOLEM:
    case MODEL_POISON_GOLEM:
        huntingGround_->AdvanceMonsterState(object);
        break;
    case MODEL_PERSONA:
    case MODEL_TWIN_TAIL:
    case MODEL_KANTURU2ND_ENTER_NPC:
    case MODEL_TRAP_CANON:
        kanturu2nd_->AdvanceMonsterState(character);
        break;
    case MODEL_ILLUSION_OF_KUNDUN:
    case MODEL_DEATH_CENTURION:
    case MODEL_SHRIKER:
    case MODEL_WARCRAFT:
        hellas_->AdvanceMonsterState(character);
        break;
    }
    if (BaseMap *const map = ContextBehavior())
        map->AdvanceMonsterState(character, model);
}

void MapProcess::CaptureMonsterAttackState(CHARACTER &character, BMD &model)
{
    if (BaseMap *const map = ContextBehavior())
        map->CaptureMonsterAttackState(character, model);
}

CPortalMgr::CPortalMgr(SessionKeeper &keeper)
    : gMapManager(keeper.MapManagerObject()), Hero(keeper.HeroStorage())
{
    Reset();
}

CPortalMgr::~CPortalMgr()
{
    Reset();
}

void CPortalMgr::Reset()
{
    ResetPortalPosition();
    ResetRevivePosition();
}

void CPortalMgr::ResetPortalPosition()
{
    m_iPortalWorld = -1;
    m_iPortalPosition_x = 0;
    m_iPortalPosition_y = 0;
}

void CPortalMgr::ResetRevivePosition()
{
    m_iReviveWorld = -1;
    m_iRevivePosition_x = 0;
    m_iRevivePosition_y = 0;
}

BOOL CPortalMgr::IsPortalUsable()
{
    switch (gMapManager.ContextMap())
    {
    case WD_6STADIUM:
    case WD_0LORENCIA:
    case WD_3NORIA:
    case WD_51HOME_6TH_CHAR:
    case WD_2DEVIAS:
    case WD_1DUNGEON:
    case WD_7ATLANSE:
    case WD_4LOSTTOWER:
    case WD_8TARKAN:
    case WD_33AIDA:
    case WD_10HEAVEN:
    case WD_37KANTURU_1ST:
    case WD_38KANTURU_2ND:
    case WD_57ICECITY:
    case WD_56MAP_SWAMP_OF_QUIET:
        return TRUE;
    }
    return FALSE;
}

void CPortalMgr::SavePortalPosition()
{
    m_iPortalWorld = gMapManager.ContextMap();
    m_iPortalPosition_x = Hero->PositionX;
    m_iPortalPosition_y = Hero->PositionY;
}

void CPortalMgr::SaveRevivePosition()
{
    m_iReviveWorld = gMapManager.ContextMap();
    m_iRevivePosition_x = Hero->PositionX;
    m_iRevivePosition_y = Hero->PositionY;
}

BOOL CPortalMgr::IsPortalPositionSaved()
{
    return (m_iPortalWorld != -1);
}

BOOL CPortalMgr::IsRevivePositionSaved()
{
    return (m_iReviveWorld != -1);
}

void CPortalMgr::GetPortalPositionText(wchar_t *pszOut)
{
    if (pszOut == NULL)
        return;

    if (m_iPortalWorld == -1)
    {
        assert(!"�̵� ��ġ�� �������� ���� ������");
    }
    else
    {
        mu_swprintf(pszOut, L"%ls (%d, %d)", gMapManager.GetMapName(m_iPortalWorld),
                    m_iPortalPosition_x, m_iPortalPosition_y);
    }
}

void CPortalMgr::GetRevivePositionText(wchar_t *pszOut)
{
    if (pszOut == NULL)
        return;

    if (m_iReviveWorld == -1)
    {
        assert(!"�̵� ��ġ�� �������� ���� ������");
    }
    else
    {
        mu_swprintf(pszOut, L"%ls (%d, %d)", gMapManager.GetMapName(m_iReviveWorld),
                    m_iRevivePosition_x, m_iRevivePosition_y);
    }
}

using namespace SEASON3A;

bool CursedTemple::SetCurrentActionMonster(CHARACTER *c, OBJECT *o)
{
    if (!gMapManager.IsCursedTemple())
        return false;

    switch (c->MonsterIndex)
    {
    case MONSTER_ILLUSION_SORCERER_SPIRIT1_POISON:
    case MONSTER_ILLUSION_SORCERER_SPIRIT2_POISON:
    case MONSTER_ILLUSION_SORCERER_SPIRIT3_POISON:
    case MONSTER_ILLUSION_SORCERER_SPIRIT4_POISON:
    case MONSTER_ILLUSION_SORCERER_SPIRIT5_POISON:
    case MONSTER_ILLUSION_SORCERER_SPIRIT6_POISON:
        return CheckMonsterSkill(c, o);
    }

    return false;
}

CHARACTER *CursedTemple::CreateCharacters(EMonsterType iType, int iPosX, int iPosY, int iKey)
{
    CHARACTER *pCharacter = NULL;

    switch (iType)
    {
    case MONSTER_STONE_STATUE: {
        OpenNpc(MODEL_CURSEDTEMPLE_STATUE);
        pCharacter = CreateCharacter(iKey, MODEL_CURSEDTEMPLE_STATUE, iPosX, iPosY);
        pCharacter->Object.EnableShadow = false;
        pCharacter->Object.m_bRenderShadow = false;
        pCharacter->Object.m_fEdgeScale = 1.03f;
        pCharacter->Object.PKKey = 0;
    }
    break;
    case MONSTER_MU_ALLIES_GENERAL: {
        OpenNpc(MODEL_CURSEDTEMPLE_ALLIED_NPC);
        pCharacter = CreateCharacter(iKey, MODEL_CURSEDTEMPLE_ALLIED_NPC, iPosX, iPosY);
        pCharacter->Object.Scale = 1.2f;
    }
    break;
    case MONSTER_ILLUSION_ELDER: {
        OpenNpc(MODEL_CURSEDTEMPLE_ILLUSION_NPC);
        pCharacter = CreateCharacter(iKey, MODEL_CURSEDTEMPLE_ILLUSION_NPC, iPosX, iPosY);
        pCharacter->Object.Scale = 1.2f;
    }
    break;
    case MONSTER_ALLIANCE_ITEM_STORAGE: {
        OpenNpc(MODEL_CURSEDTEMPLE_ALLIED_BASKET);
        pCharacter = CreateCharacter(iKey, MODEL_CURSEDTEMPLE_ALLIED_BASKET, iPosX, iPosY);
        pCharacter->Object.Scale = 1.8f;
        pCharacter->Object.m_fEdgeScale = 1.03f;
        m_ShowAlliedPointEffect = false;
    }
    break;
    case MONSTER_ILLUSION_ITEM_STORAGE: {
        OpenNpc(MODEL_CURSEDTEMPLE_ILLUSION__BASKET);
        pCharacter = CreateCharacter(iKey, MODEL_CURSEDTEMPLE_ILLUSION__BASKET, iPosX, iPosY);

        pCharacter->Object.Scale = 1.5f;
        pCharacter->Object.m_fEdgeScale = 1.03f;
        m_ShowIllusionPointEffect = false;
    }
    break;
    case MONSTER_MIRAGE: {
        OpenNpc(MODEL_CURSEDTEMPLE_ENTER_NPC);
        pCharacter = CreateCharacter(iKey, MODEL_CURSEDTEMPLE_ENTER_NPC, iPosX, iPosY);
        pCharacter->Object.Scale = 0.95f;
    }
    break;
    case MONSTER_ILLUSION_SORCERER_SPIRIT1_LIGHTNING:
    case MONSTER_ILLUSION_SORCERER_SPIRIT2_LIGHTNING:
    case MONSTER_ILLUSION_SORCERER_SPIRIT3_LIGHTNING:
    case MONSTER_ILLUSION_SORCERER_SPIRIT4_LIGHTNING:
    case MONSTER_ILLUSION_SORCERER_SPIRIT5_LIGHTNING:
    case MONSTER_ILLUSION_SORCERER_SPIRIT6_LIGHTNING: {
        OpenMonsterModel(MONSTER_MODEL_ILLUSION_SORCERER_SPIRIT_LIGHTNING);
        pCharacter = CreateCharacter(iKey, MODEL_ILLUSION_SORCERER_SPIRIT_LIGHTNING, iPosX, iPosY);
        pCharacter->Object.Scale = 1.0f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
    }
    break;
    case MONSTER_ILLUSION_SORCERER_SPIRIT1_ICE:
    case MONSTER_ILLUSION_SORCERER_SPIRIT2_ICE:
    case MONSTER_ILLUSION_SORCERER_SPIRIT3_ICE:
    case MONSTER_ILLUSION_SORCERER_SPIRIT4_ICE:
    case MONSTER_ILLUSION_SORCERER_SPIRIT5_ICE:
    case MONSTER_ILLUSION_SORCERER_SPIRIT6_ICE: {
        OpenMonsterModel(MONSTER_MODEL_ILLUSION_SORCERER_SPIRIT_ICE);
        pCharacter = CreateCharacter(iKey, MODEL_ILLUSION_SORCERER_SPIRIT_ICE, iPosX, iPosY);
        pCharacter->Object.Scale = 1.0f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
    }
    break;
    case MONSTER_ILLUSION_SORCERER_SPIRIT1_POISON:
    case MONSTER_ILLUSION_SORCERER_SPIRIT2_POISON:
    case MONSTER_ILLUSION_SORCERER_SPIRIT3_POISON:
    case MONSTER_ILLUSION_SORCERER_SPIRIT4_POISON:
    case MONSTER_ILLUSION_SORCERER_SPIRIT5_POISON:
    case MONSTER_ILLUSION_SORCERER_SPIRIT6_POISON: {
        OpenMonsterModel(MONSTER_MODEL_ILLUSION_SORCERER_SPIRIT_POISON);
        pCharacter = CreateCharacter(iKey, MODEL_ILLUSION_SORCERER_SPIRIT_POISON, iPosX, iPosY);
        pCharacter->Object.Scale = 1.0f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
    }
    break;
    case MONSTER_MU_ALLIES: {
        pCharacter = CreateCharacter(iKey, MODEL_PLAYER, iPosX, iPosY);
        pCharacter->Object.Scale = 1.f;
        pCharacter->Object.SubType = MODEL_CURSEDTEMPLE_ALLIED_PLAYER;
    }
    break;
    case MONSTER_ILLUSION_SORCERER: {
        pCharacter = CreateCharacter(iKey, MODEL_PLAYER, iPosX, iPosY);
        pCharacter->Object.Scale = 1.f;
        pCharacter->Object.SubType = MODEL_CURSEDTEMPLE_ILLUSION_PLAYER;
    }
    break;
    }

    return pCharacter;
}

bool CursedTemple::AttackEffectMonster(CHARACTER *c, OBJECT *o, BMD *b)
{
    if (!gMapManager.IsCursedTemple())
        return false;

    vec3_t p, Light;
    Vector(0.f, 0.f, 0.f, p);
    Vector(1.f, 1.f, 1.f, Light);
    switch (c->MonsterIndex)
    {
    case MONSTER_ILLUSION_SORCERER_SPIRIT1_POISON:
    case MONSTER_ILLUSION_SORCERER_SPIRIT2_POISON:
    case MONSTER_ILLUSION_SORCERER_SPIRIT3_POISON:
    case MONSTER_ILLUSION_SORCERER_SPIRIT4_POISON:
    case MONSTER_ILLUSION_SORCERER_SPIRIT5_POISON:
    case MONSTER_ILLUSION_SORCERER_SPIRIT6_POISON: {
        if (o->CurrentAction == MONSTER01_ATTACK1)
        {
            if (c->TargetCharacter != -1)
            {
                CHARACTER *tc = &CharactersClient[c->TargetCharacter];
                OBJECT *to = &tc->Object;

                Vector(0.4f, 0.6f, 1.f, Light);

                for (int i = 0; i < 5; i++)
                    CreateParticleFpsChecked(BITMAP_SMOKE, to->Position, o->Angle, Light, 1);

                PlayBuffer(SOUND_HEART);
            }
        }
    }
        return true;
    }

    return false;
}

bool CursedTemple::MoveObject(OBJECT *o)
{
    if (!gMapManager.IsCursedTemple())
        return false;

    if (m_bGaugebarEnabled == true && WorldTime - m_fGaugebarCloseTimer > 3000.0f)
    {
        SetGaugebarEnabled(false);
    }

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
        case 64:
        case 65:
        case 80:
            o->BlendMeshLight = (float)sinf(WorldTime * 0.0010f) * 0.5f + 0.5f;
            break;
        case 70:
        case 71:
        case 72:
        case 73:
        case 74:
        case 75:
        case 76:
        case 77:
        case 78:
        case 79:
            o->HiddenMesh = -2;
            break;
        }
    }
        return true;
    }

    return false;
}

void CursedTemple::AdvanceMonsterState(CHARACTER &character, BMD &model)
{
    auto &object = character.Object;

    if (gMapManager.IsCursedTemple())
    {
        if (object.Type == MODEL_CURSEDTEMPLE_ALLIED_NPC)
            object.Position[2] = 225.f;
        else if (object.Type == MODEL_CURSEDTEMPLE_ILLUSION_NPC)
            object.Position[2] = 250.f;
    }
    if (gMapManager.IsCursedTemple() && object.Type == MODEL_CURSEDTEMPLE_STATUE &&
        object.CurrentAction == MONSTER01_DIE)
        object.PKKey = 1;
}

GMAtlans::GMAtlans(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      Random(keeper.RandomForConstruction())
{
}

bool GMAtlans::CreateObject(OBJECT *o)
{
    const int Type = o->Type;
    switch (Type)
    {
    case 39:
        CreateOperate(o);
        o->HiddenMesh = -2;
        break;
    }
    return true;
}

bool GMAtlans::MoveObject(OBJECT *o)
{
    vec3_t p{}, Light{};
    float Luminosity;
    switch (o->Type)
    {
    case 22:
        o->HiddenMesh = -2;
        o->Timer += 0.1f * FPS_ANIMATION_FACTOR;
        if (o->Timer > 10.f)
            o->Timer = 0.f;
        if (o->Timer > 5.f)
            CreateParticleFpsChecked(BITMAP_BUBBLE, o->Position, o->Angle, o->Light);
        break;
    case 23:
        o->BlendMesh = 0;
        // o->BlendMeshLight = o->Light[1]+1.f;
        o->BlendMeshLight = sinf(WorldTime * 0.002f) * 0.3f + 0.5f;
        break;
    case 32:
    case 34:
        o->BlendMesh = 1;
        o->BlendMeshLight = sinf(WorldTime * 0.004f) * 0.5f + 0.5f;
        break;
    case 38:
        o->BlendMesh = 0;
        // o->BlendMeshLight = sinf(WorldTime*0.004f)*0.3f+0.7f;
        break;
    case 40:
        o->BlendMesh = 0;
        o->BlendMeshLight = sinf(WorldTime * 0.004f) * 0.3f + 0.5f;
        o->Velocity = 0.05f;
        break;
    }
    return true;
}

float GMAtlans::MonsterDeathRotationRate(const OBJECT &object, float rate)
{
    return object.Type == MODEL_BALI ? 0.05f : rate;
}

MapObjectInteraction GMAtlans::ObjectInteraction(int type, CHARACTER &actor)
{
    using Action = MapObjectInteraction::Action;
    return type == 39 ? MapObjectInteraction{Action::Pose, true} : MapObjectInteraction{};
}

GMBloodCastle::GMBloodCastle(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper)
{
}

bool GMBloodCastle::MoveObject(OBJECT *o)
{
    vec3_t p{}, Light{};
    float Luminosity;
    switch (o->Type)
    {
    case 9:
    case 10:
        if (o->PKKey != 4)
            o->HiddenMesh = -2;
        break;
    }
    return true;
}

void GMBloodCastle::InstallBehavior()
{
    LoadWaveFile(SOUND_BLOODCASTLE, L"Data\\Sound\\iBloodCastle.wav", 1);
}

bool GMBloodCastle::AdvanceCharacterDeath(CHARACTER *character, OBJECT *object)
{
    if (!object->m_bActionStart)
        return false;
    FallingCharacter(character, object);
    return true;
}

CGMCrywolf1stPtr CGMCrywolf1st::Make(SessionKeeper &keeper)
{
    return CGMCrywolf1stPtr(new CGMCrywolf1st(keeper));
}

CGMCrywolf1st::CGMCrywolf1st(SessionKeeper &keeper)
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject()), g_RenderText(keeper.SessionText())
{
}

void CGMCrywolf1st::CryWolfMVPInit()
{
    Deco_Insert = 0.f;
    memset(Box_String, 0, sizeof(Box_String));
    Button_Down = 0;
    BackUp_Key = 0;
    m_iHour = 0, m_iMinute = 0;
    m_dwSyncTime = -1;
    Add_Num = 10;
    TargetNpc = -1;
    Delay = 1;
    Dark_Elf_Check = false;

    iNextNotice = -1;
    View_Bal = false;
    Message_Box = 0;
    Dark_elf_Num = 0;
    Val_Hp = 100;

    m_OccupationState = 0;
    m_CrywolfState = 0;
    m_StatueHP = 0;
    SelectedNpc = -1;
}

int CGMCrywolf1st::IsCryWolf1stMVPStart()
{
    return m_OccupationState;
}

bool CGMCrywolf1st::IsCryWolf1stMVPStatePeace()
{
    if (gMapManager.ContextMap() == WD_34CRYWOLF_1ST &&
        m_OccupationState == CRYWOLF_OCCUPATION_STATE_PEACE)
        return true;

    return false;
}

void CGMCrywolf1st::CheckCryWolf1stMVP(BYTE btOccupationState, BYTE btCrywolfState)
{
    if (m_OccupationState == btOccupationState && m_CrywolfState == btCrywolfState)
        return;

    m_CrywolfState = btCrywolfState;

    if (m_CrywolfState >= CRYWOLF_STATE_READY && m_CrywolfState < CRYWOLF_STATE_ENDCYCLE)
    {
        g_pNewUISystem->Show(SEASON3B::INTERFACE_CRYWOLF);
    }

    if (m_CrywolfState == CRYWOLF_STATE_START)
    {
        Dark_Elf_Check = true;

        g_pCryWolfInterface->InitTime();
    }

    if (m_CrywolfState != CRYWOLF_STATE_START)
        View_Bal = false;

    if (m_CrywolfState == CRYWOLF_STATE_END)
    {
        for (int i = 0; i < 5; i++)
            HeroScore[i] = -1;

        if (btOccupationState != CRYWOLF_OCCUPATION_STATE_WAR)
        {
            Suc_Or_Fail = 1;
            //			View_Suc_Or_Fail = 1;
            if (btOccupationState == CRYWOLF_OCCUPATION_STATE_PEACE)
            {
                Add_Num = 11;
                View_Suc_Or_Fail = 1;
            }
            else
                View_Suc_Or_Fail = -1;
        }
    }

    if (m_OccupationState == btOccupationState)
        return;

    m_OccupationState = btOccupationState;

    ReloadTerrainVariant();
}

MapDefinition::Variant CGMCrywolf1st::TerrainVariant() const noexcept
{
    switch (m_OccupationState)
    {
    case CRYWOLF_OCCUPATION_STATE_OCCUPIED:
        return MapDefinition::Variant::Occupied;
    case CRYWOLF_OCCUPATION_STATE_WAR:
        return MapDefinition::Variant::War;
    default:
        return MapDefinition::Variant::Base;
    }
}

void CGMCrywolf1st::ReloadTerrainVariant()
{
    if (!IsCyrWolf1st())
        return;
    sessionKeeper_.WorldUnit()->ReloadTerrainVariant(TerrainVariant());
}

void CGMCrywolf1st::CheckCryWolf1stMVPAltarfInfo(int StatueHP, BYTE AltarState1, BYTE AltarState2,
                                                 BYTE AltarState3, BYTE AltarState4,
                                                 BYTE AltarState5)
{
    m_StatueHP = StatueHP;
    m_AltarState[0] = AltarState1;
    m_AltarState[1] = AltarState2;
    m_AltarState[2] = AltarState3;
    m_AltarState[3] = AltarState4;
    m_AltarState[4] = AltarState5;
}

void CGMCrywolf1st::DoTankerFireFixStartPosition(int SourceX, int SourceY, int PositionX,
                                                 int PositionY)
{
    vec3_t Position, TargetPosition;

    Vector(PositionX * TERRAIN_SCALE, PositionY * TERRAIN_SCALE, 100.f, TargetPosition);

    int Type = 0;
    if (SourceX == 122 || SourceX == 116)
        Type = 2;
    else if (SourceX == 62)
        Type = 0;
    else if (SourceX == 183)
        Type = 1;

    switch (Type)
    {
    case 0:
        Vector(TargetPosition[0] - 800.0f, TargetPosition[1], 800.0f, Position);
        CreateEffect(MODEL_ARROW_TANKER_HIT, Position, TargetPosition, Hero->Object.Light, 0,
                     &Hero->Object);
        break;
    case 1:
        Vector(TargetPosition[0] + 800.0f, TargetPosition[1], 800.0f, Position);
        CreateEffect(MODEL_ARROW_TANKER_HIT, Position, TargetPosition, Hero->Object.Light, 1,
                     &Hero->Object);
        break;
    case 2:
        Vector(TargetPosition[0], TargetPosition[1] + 800.0f, 800.0f, Position);
        CreateEffect(MODEL_ARROW_TANKER_HIT, Position, TargetPosition, Hero->Object.Light, 2,
                     &Hero->Object);
        break;
    }
}

bool CGMCrywolf1st::IsCyrWolf1st()
{
    return (gMapManager.ContextMap() == WD_34CRYWOLF_1ST) ? true : false;
}

bool CGMCrywolf1st::CreateCryWolf1stObject(OBJECT *o)
{
    if (!IsCyrWolf1st())
        return false;

    return true;
}

bool CGMCrywolf1st::MoveCryWolf1stObject(OBJECT *o)
{
    if (!IsCyrWolf1st())
        return false;

    float Luminosity;
    vec3_t Light;

    switch (o->Type)
    {
    case 36: {
    }
    break;
    case 70: {
    }
    break;
    case 41: {
        Vector(0.2f, 0.7f, 0.5f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
    }
    break;
    case 71:
    case 57: {
        Luminosity = (float)(WorldRandom() % 4 + 3) * 0.1f;
        Vector(Luminosity, Luminosity * 0.6f, Luminosity * 0.2f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
    }
    break;
    case 81:
        if (m_OccupationState == CRYWOLF_OCCUPATION_STATE_WAR)
            o->Scale = 1.03f;
        o->Alpha = 0.2f;
        break;
    case 82:
    case 83:
    case 84:
        o->HiddenMesh = -2;
        break;
    }
    return true;
}

CHARACTER *CGMCrywolf1st::CreateCryWolf1stMonster(int iType, int PosX, int PosY, int Key)
{
    if (!IsCyrWolf1st() && !(gMapManager.InDevilSquare()))
        return NULL;

    CHARACTER *c = NULL;

    switch (iType)
    {
    case MONSTER_HAMMER_SCOUT: {
        OpenMonsterModel(MONSTER_MODEL_SCOUT);
        c = CreateCharacter(Key, MODEL_SCOUT, PosX, PosY);
        c->Object.Scale = 1.2f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
    }
    break;
    case MONSTER_LANCE_SCOUT: {
        OpenMonsterModel(MONSTER_MODEL_SOLAM);
        c = CreateCharacter(Key, MODEL_SOLAM, PosX, PosY);
        c->Object.Scale = 1.2f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
    }
    break;
    case MONSTER_BOW_SCOUT: {
        OpenMonsterModel(MONSTER_MODEL_VALAM);
        c = CreateCharacter(Key, MODEL_VALAM, PosX, PosY);
        c->Object.Scale = 1.2f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;

        RegisterBone(c, L"Monster96_Top", CharacterSocket::Monster96_Top);
        RegisterBone(c, L"Monster96_Center", CharacterSocket::Monster96_Center);
        RegisterBone(c, L"Monster96_Bottom", CharacterSocket::Monster96_Bottom);
    }
    break;
    case MONSTER_WEREWOLF: {
        OpenMonsterModel(MONSTER_MODEL_WEREWOLF_HERO);
        c = CreateCharacter(Key, MODEL_WEREWOLF_HERO, PosX, PosY);
        c->Object.Scale = 1.25f;
        c->Object.SubType = 0;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;

        RegisterBone(c, L"Monster95_Head", CharacterSocket::Monster95_Head);
    }
    break;
    case MONSTER_SCOUTHERO: {
        OpenMonsterModel(MONSTER_MODEL_SCOUT);
        c = CreateCharacter(Key, MODEL_SCOUT, PosX, PosY);
        c->Object.Scale = 1.6f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
    }
    break;
    case MONSTER_WEREWOLFHERO: {
        OpenMonsterModel(MONSTER_MODEL_WEREWOLF_HERO);
        c = CreateCharacter(Key, MODEL_WEREWOLF_HERO, PosX, PosY);
        c->Object.Scale = 1.65f;
        c->Object.SubType = 1;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;

        RegisterBone(c, L"Monster95_Head", CharacterSocket::Monster95_Head);
    }
    break;
    case MONSTER_VALAM:
    case MONSTER_BALRAM: {
        OpenMonsterModel(MONSTER_MODEL_BALRAM);
        c = CreateCharacter(Key, MODEL_BALRAM, PosX, PosY);
        c->Object.Scale = 1.25f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
    }
    break;
    case MONSTER_SOLAM:
    case MONSTER_SORAM: {
        OpenMonsterModel(MONSTER_MODEL_SORAM);
        c = CreateCharacter(Key, MODEL_SORAM, PosX, PosY);
        c->Object.Scale = 1.3f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
    }
    break;
    case MONSTER_BALGASS: {
        OpenMonsterModel(MONSTER_MODEL_BALGASS);
        c = CreateCharacter(Key, MODEL_BALGASS, PosX, PosY);
        c->Object.Scale = 2.f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        CreateJoint(BITMAP_JOINT_ENERGY, c->Object.Position, c->Object.Position, c->Object.Angle, 2,
                    &c->Object, 30.f);
        CreateJoint(BITMAP_JOINT_ENERGY, c->Object.Position, c->Object.Position, c->Object.Angle, 3,
                    &c->Object, 30.f);
    }
    break;
    case MONSTER_DEATH_SPIRIT: {
        OpenMonsterModel(MONSTER_MODEL_DEATH_SPIRIT);
        c = CreateCharacter(Key, MODEL_DEATH_SPIRIT, PosX, PosY);
        c->Object.Scale = 1.25f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;

        RegisterBone(c, L"Monster94_zx", CharacterSocket::Monster94_zx);
        RegisterBone(c, L"Monster94_zx01", CharacterSocket::Monster94_zx01);
    }
    break;
    case MONSTER_DARK_ELF:
    case MONSTER_DARKELF: {
        OpenMonsterModel(MONSTER_MODEL_DARK_ELF_1);
        c = CreateCharacter(Key, MODEL_DARK_ELF_1, PosX, PosY);
        c->Object.Scale = 1.5f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        RegisterBone(c, L"Left_Hand", CharacterSocket::Left_Hand);
    }
    break;
    case MONSTER_BALLISTA: {
        OpenMonsterModel(MONSTER_MODEL_BALLISTA);
        c = CreateCharacter(Key, MODEL_BALLISTA, PosX, PosY, 180);
        c->Object.Scale = 1.0f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;

        if (PosY == 90)
        {
        }
        if (PosX == 62)
            c->Object.Angle[2] = 90.0f;
        if (PosX == 183)
            c->Object.Angle[2] = 90.0f;
    }
    break;
    }

    return c;
}

bool CGMCrywolf1st::Get_State()
{
    if (m_CrywolfState == CRYWOLF_STATE_START)
        return true;
    return false;
}

bool CGMCrywolf1st::Get_State_Only_Elf() const
{
    if (m_CrywolfState == CRYWOLF_STATE_START || m_CrywolfState == CRYWOLF_STATE_READY)
        return true;
    return false;
}

void CGMCrywolf1st::SetTime(BYTE byHour, BYTE byMinute)
{
    if (Get_State() == false || IsCyrWolf1st() == false)
        return;
    m_iHour = byHour;
    m_iMinute = byMinute;
    m_dwSyncTime = GetTickCount();

    if (TimeStart == false)
        TimeStart = true;
}

void CGMCrywolf1st::Set_BossMonster(int Val_Hp, int Dl_Num)
{
    if (Get_State_Only_Elf() == false || IsCyrWolf1st() == false)
        return;
    Dark_elf_Num = Dl_Num;

    if (Val_Hp > 0 && View_Bal == false)
        View_Bal = true;
    else if (Val_Hp <= 0 && View_Bal == true)
        View_Bal = false;

    Set_Val_Hp(Val_Hp);
}

void CGMCrywolf1st::Check_AltarState(int Num, int State)
{
    if (Get_State_Only_Elf() == false || IsCyrWolf1st() == false)
        return;
    m_AltarState[Num - 1] = State;
}

void CGMCrywolf1st::Set_Message_Box(int Str, int Num, int Key, int ObjNum)
{
    if (Get_State_Only_Elf() == false || IsCyrWolf1st() == false || LogOut)
        return;

    if (Str == 56)
    {
        BYTE State = (m_AltarState[ObjNum] & 0x0f);
        mu_swprintf(Box_String[Num], I18N::Game::Lookup(1950 + Str), State);
    }
    else
        wcscpy(Box_String[Num], I18N::Game::Lookup(1950 + Str));
    if (Str == 56 || Str == 57)
        Message_Box = 1;
    else
        Message_Box = 2;
    if (Num == 0)
        Box_String[Num + 1][0] = 0;

    BackUp_Key = Key;
}

void CGMCrywolf1st::Set_Hp(int State)
{
    m_StatueHP = State;
}

void CGMCrywolf1st::Set_Val_Hp(int State)
{
    Val_Hp = State;
}

bool CGMCrywolf1st::Get_AltarState_State(int Num)
{
    BYTE Use = (m_AltarState[Num] & 0xf0) >> 4;
    //	BYTE State = (m_AltarState[Num] & 0x0f);
    if (Use == CRYWOLF_ALTAR_STATE_CONTRACTED)
        return true;
    else
        return false;
}

bool CGMCrywolf1st::SetCurrentActionCrywolfMonster(CHARACTER *c, OBJECT *o)
{
    if (!IsCyrWolf1st() && !(gMapManager.InDevilSquare()))
        return false;

    switch (c->MonsterIndex)
    {
    case MONSTER_DARK_ELF:
    case MONSTER_DARKELF:
    case MONSTER_BALRAM:
    case MONSTER_DEATH_SPIRIT:
    case MONSTER_SORAM:
    case MONSTER_BALGASS:
        return CheckMonsterSkill(c, o);
    }
    return false;
}

void CGMCrywolf1st::Set_MyRank(BYTE MyRank, int GettingExp)
{
    Rank = MyRank;
    Exp = GettingExp;
}

void CGMCrywolf1st::Set_WorldRank(BYTE Rank, CLASS_TYPE Class, int Score, wchar_t *szHeroName)
{
    HeroScore[Rank] = Score;
    HeroClass[Rank] = Class;
    wcsncpy_s(HeroName[Rank], szHeroName, MAX_USERNAME_SIZE);
}
bool CGMCrywolf1st::CreateObject(OBJECT *object)
{
    return CreateCryWolf1stObject(object);
}

bool CGMCrywolf1st::MoveObject(OBJECT *object)
{
    return MoveCryWolf1stObject(object);
}

bool CGMCrywolf1st::SetCurrentActionMonster(CHARACTER *character, OBJECT *object)
{
    return SetCurrentActionCrywolfMonster(character, object);
}

void CGMCrywolf1st::InstallBehavior()
{
    LoadWaveFile(SOUND_CRY1ST_AMBIENT, L"Data\\Sound\\w35\\crywolf_ambi.wav", 1, true);
    LoadWaveFile(SOUND_CRY1ST_WWOLF_MOVE1, L"Data\\Sound\\w35\\ww_idle1.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_WWOLF_MOVE2, L"Data\\Sound\\w35\\ww_idle2.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_WWOLF_ATTACK1, L"Data\\Sound\\w35\\ww_attack1.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_WWOLF_ATTACK2, L"Data\\Sound\\w35\\ww_attack2.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_WWOLF_DIE, L"Data\\Sound\\w35\\ww_death.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SCOUT1_MOVE1, L"Data\\Sound\\w35\\ww_s1_idle1.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SCOUT1_MOVE2, L"Data\\Sound\\w35\\ww_s1_idle2.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SCOUT1_ATTACK1, L"Data\\Sound\\w35\\ww_s1_attack1.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SCOUT1_ATTACK2, L"Data\\Sound\\w35\\ww_s1_attack2.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SCOUT1_DIE, L"Data\\Sound\\w35\\ww_s1_death.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SCOUT2_MOVE1, L"Data\\Sound\\w35\\ww_s2_idle1.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SCOUT2_MOVE2, L"Data\\Sound\\w35\\ww_s2_idle2.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SCOUT2_ATTACK1, L"Data\\Sound\\w35\\ww_s2_attack1.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SCOUT2_ATTACK2, L"Data\\Sound\\w35\\ww_s2_attack2.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SCOUT2_DIE, L"Data\\Sound\\w35\\ww_s2_death.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SCOUT3_MOVE1, L"Data\\Sound\\w35\\ww_s3_idle1.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SCOUT3_MOVE2, L"Data\\Sound\\w35\\ww_s3_idle2.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SCOUT3_ATTACK1, L"Data\\Sound\\w35\\ww_s3_attack1.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SCOUT3_ATTACK2, L"Data\\Sound\\w35\\ww_s3_attack2.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SCOUT3_DIE, L"Data\\Sound\\w35\\ww_s3_death.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SORAM_MOVE1, L"Data\\Sound\\w35\\soram_idle1.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SORAM_MOVE2, L"Data\\Sound\\w35\\soram_idle2.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SORAM_ATTACK1, L"Data\\Sound\\w35\\soram_attack1.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SORAM_ATTACK2, L"Data\\Sound\\w35\\soram_attack2.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SORAM_DIE, L"Data\\Sound\\w35\\soram_death.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_BALRAM_MOVE1, L"Data\\Sound\\w35\\balram_idle1.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_BALRAM_MOVE2, L"Data\\Sound\\w35\\balram_idle2.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_BALRAM_ATTACK1, L"Data\\Sound\\w35\\balram_attack1.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_BALRAM_ATTACK2, L"Data\\Sound\\w35\\balram_attack2.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_BALRAM_DIE, L"Data\\Sound\\w35\\balram_death.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_BALGAS_MOVE1, L"Data\\Sound\\w35\\balga_idle1.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_BALGAS_MOVE2, L"Data\\Sound\\w35\\balga_idle2.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_BALGAS_ATTACK1, L"Data\\Sound\\w35\\balga_at1.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_BALGAS_ATTACK2, L"Data\\Sound\\w35\\balga_at2.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_BALGAS_DIE, L"Data\\Sound\\w35\\balga_death.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_BALGAS_SKILL1, L"Data\\Sound\\w35\\balga_skill1.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_BALGAS_SKILL2, L"Data\\Sound\\w35\\balga_skill2.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_DARKELF_MOVE1, L"Data\\Sound\\w35\\darkelf_idle1.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_DARKELF_MOVE2, L"Data\\Sound\\w35\\darkelf_idle2.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_DARKELF_ATTACK1, L"Data\\Sound\\w35\\darkelf_at1.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_DARKELF_ATTACK2, L"Data\\Sound\\w35\\darkelf_at2.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_DARKELF_DIE, L"Data\\Sound\\w35\\darkelf_death.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_DARKELF_SKILL1, L"Data\\Sound\\w35\\darkelf_skill1.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_DARKELF_SKILL2, L"Data\\Sound\\w35\\darkelf_skill2.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_DEATHSPIRIT_MOVE1, L"Data\\Sound\\w35\\dths_idle1.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_DEATHSPIRIT_MOVE2, L"Data\\Sound\\w35\\dths_idle2.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_DEATHSPIRIT_ATTACK1, L"Data\\Sound\\w35\\dths_at1.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_DEATHSPIRIT_ATTACK2, L"Data\\Sound\\w35\\dths_at2.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_DEATHSPIRIT_DIE, L"Data\\Sound\\w35\\dths_deat.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_TANKER_ATTACK1, L"Data\\Sound\\w35\\tanker_attack.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_TANKER_DIE, L"Data\\Sound\\w35\\tanker_death.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SUMMON, L"Data\\Sound\\w35\\spawn_single.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_SUCCESS, L"Data\\Sound\\w35\\CW_win.wav", 1);
    LoadWaveFile(SOUND_CRY1ST_FAILED, L"Data\\Sound\\w35\\CW_lose.wav", 1);
}

GMDevias::GMDevias(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      Random(keeper.RandomForConstruction())
{
}

bool GMDevias::CreateObject(OBJECT *o)
{
    const int Type = o->Type;
    switch (Type)
    {
    case MODEL_WARP: {
        vec3_t Position;
        Vector(o->Position[0], o->Position[1], o->Position[2] + 360.f, Position);
        CreateEffect(MODEL_WARP, Position, o->Angle, o->Light, 1);

        Vector(o->Position[0], o->Position[1] + 4.0f, o->Position[2] + 360.f, Position);
        CreateEffect(MODEL_WARP2, Position, o->Angle, o->Light, 1);

        Vector(o->Position[0], o->Position[1] + 20.0f, o->Position[2] + 360.f, Position);
        CreateEffect(MODEL_WARP3, Position, o->Angle, o->Light, 1);
    }
    break;
    case 22:
    case 25:
    case 40:
    case 45:
    case 55:
    case 73:
        CreateOperate(o);
        break;
    case 91:
        CreateOperate(o);
        Vector(40.f, 40.f, 160.f, o->BoundingBoxMax);
        o->HiddenMesh = -2;
        break;
    case 19:
    case 92:
    case 93:
        o->BlendMesh = 0;
        break;
    case 54:
    case 56:
        o->BlendMesh = 1;
        break;
    case 78:
        o->BlendMesh = 3;
        break;
    case 20:
    case 65:
    case 88:
    case 86:
        o->Angle[2] = (float)((int)o->Angle[2] % 360);
        VectorCopy(o->Angle, o->HeadAngle);
        VectorCopy(o->Position, o->HeadTargetAngle);
        break;
    case 100:
        o->HiddenMesh = -2;
        break;
    }
    return true;
}

bool GMDevias::MoveObject(OBJECT *o)
{
    vec3_t p{}, Light{};
    float Luminosity;
    switch (o->Type)
    {
    case 78:
        o->BlendMeshLight = (float)(WorldRandom() % 4 + 4) * 0.1f;
        break;
    case 30: {
        vec3_t Position;
        Position[0] = o->Position[0];
        Position[1] = o->Position[1];
        Position[2] = o->Position[2] + 160.f;
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR))
        {
            CreateParticle(BITMAP_TRUE_FIRE, Position, o->Angle, o->Light, 0, o->Scale);
            CreateParticle(BITMAP_SMOKE, Position, o->Angle, o->Light, 21,
                           0.5f + ((WorldRandom() % 9) * 0.1f));
        }
    }
    break;
    case 66:
        CreateFire(0, o, 0.f, 0.f, 50.f);
        break;
    case 86:
    case 20:
    case 65:
    case 88:
        if (EditFlag == EDIT_NONE)
        {
            float dx = Hero->Object.Position[0] - o->HeadTargetAngle[0];
            float dy = Hero->Object.Position[1] - o->HeadTargetAngle[1];
            float Distance = sqrtf(dx * dx + dy * dy);
            if (Distance < 200.f)
            {
                if (o->Type == 86)
                {
                    if (o->Angle[2] == 90.f)
                        o->Position[1] = o->HeadTargetAngle[1] + (200.f - Distance) * 2.f;
                    if (o->Angle[2] == 270.f)
                        o->Position[1] = o->HeadTargetAngle[1] - (200.f - Distance) * 2.f;
                    if (o->Angle[2] == 0.f)
                        o->Position[0] = o->HeadTargetAngle[0] + (200.f - Distance) * 2.f;
                    if (o->Angle[2] == 180.f)
                        o->Position[0] = o->HeadTargetAngle[0] - (200.f - Distance) * 2.f;
                    PlayBuffer(SOUND_DOOR02);
                }
                else
                {
                    if (o->HeadAngle[2] == 90.f)
                        o->Angle[2] = 30.f - (200.f - Distance) * 0.5f;
                    if (o->HeadAngle[2] == 270.f)
                        o->Angle[2] = 330.f + (200.f - Distance) * 0.5f;
                    if (o->HeadAngle[2] == 0.f)
                        o->Angle[2] = 300.f - (200.f - Distance) * 0.5f;
                    if (o->HeadAngle[2] == 180.f)
                        o->Angle[2] = 240.f + (200.f - Distance) * 0.5f;
                    PlayBuffer(SOUND_DOOR01);
                }
            }
            else
            {
                o->Angle[2] = TurnAngle2(o->Angle[2], o->HeadAngle[2], 10.f * FPS_ANIMATION_FACTOR);
                o->Position[0] += (o->HeadTargetAngle[0] - o->Position[0]) *
                                  Core::Time::Blend(0.2f, FPS_ANIMATION_FACTOR);
                o->Position[1] += (o->HeadTargetAngle[1] - o->Position[1]) *
                                  Core::Time::Blend(0.2f, FPS_ANIMATION_FACTOR);
            }
        }
    }
    return true;
}

void GMDevias::InstallBehavior()
{
    vec3_t Pos, Ang;

    Vector(0.f, 0.f, 0.f, Ang);
    Vector(0.f, 0.f, 270.f, Pos);
    Pos[0] = 191 * TERRAIN_SCALE;
    Pos[1] = 16 * TERRAIN_SCALE;
    SessionLegacyCalls::CreateObject(MODEL_NPC_SERBIS_DONKEY, Pos, Ang);
    Pos[0] = 191 * TERRAIN_SCALE;
    Pos[1] = 17 * TERRAIN_SCALE;
    SessionLegacyCalls::CreateObject(MODEL_NPC_SERBIS_FLAG, Pos, Ang);

    Vector(0.f, 0.f, 10.f, Ang);
    Vector(0.f, 0.f, 0.f, Pos);
    Pos[0] = 53 * TERRAIN_SCALE + 50.f;
    Pos[1] = 92 * TERRAIN_SCALE + 20.f;
    SessionLegacyCalls::CreateObject(MODEL_WARP, Pos, Ang);
}

MapObjectInteraction GMDevias::ObjectInteraction(int type, CHARACTER &actor)
{
    using Action = MapObjectInteraction::Action;
    switch (type)
    {
    case 91:
        return {Action::Pose, true};
    case 22:
    case 25:
    case 40:
    case 55:
        return {Action::Sit, true};
    case 45:
    case 73:
        return {Action::Sit, false};
    default:
        return {};
    }
}

GMDevilSquare::GMDevilSquare(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      Random(keeper.RandomForConstruction())
{
}

bool GMDevilSquare::SetCurrentActionMonster(CHARACTER *character, OBJECT *object)
{
    return TheMapProcess().Crywolf1st().SetCurrentActionCrywolfMonster(character, object);
}

CGMDoppelGanger2Ptr CGMDoppelGanger2::Make(SessionKeeper &keeper)
{
    CGMDoppelGanger2Ptr doppelganger(new CGMDoppelGanger2(keeper));
    doppelganger->Init();
    return doppelganger;
}

CGMDoppelGanger2::CGMDoppelGanger2(SessionKeeper &keeper)
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject()), g_Camera(keeper.CameraStateObject())
{
}

CGMDoppelGanger2::~CGMDoppelGanger2()
{
    Destroy();
}

void CGMDoppelGanger2::Init()
{
}

void CGMDoppelGanger2::Destroy()
{
}

bool CGMDoppelGanger2::CreateObject(OBJECT *o)
{
    if (o->Type == 10 || o->Type == 19 || o->Type == 20 || o->Type == 31 || o->Type == 33)
        o->m_bRenderAfterCharacter = true;

    if (o->Type >= 0 && o->Type <= 6)
    {
        o->CollisionRange = -300;
        return true;
    }

    return false;
}

bool CGMDoppelGanger2::MoveObject(OBJECT *o)
{
    if (IsDoppelGanger2() == false)
        return false;

    switch (o->Type)
    {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 47:
    case 48:
        o->HiddenMesh = -2;
        return true;
    default:
        break;
    }

    return false;
}

bool CGMDoppelGanger2::IsDoppelGanger2()
{
    if (gMapManager.ContextMap() == WD_66DOPPLEGANGER2)
    {
        return true;
    }

    return false;
}

CGMDoppelGanger3Ptr CGMDoppelGanger3::Make(SessionKeeper &keeper)
{
    CGMDoppelGanger3Ptr doppelganger(new CGMDoppelGanger3(keeper));
    doppelganger->Init();
    return doppelganger;
}

CGMDoppelGanger3::CGMDoppelGanger3(SessionKeeper &keeper)
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject())
{
}

CGMDoppelGanger3::~CGMDoppelGanger3()
{
    Destroy();
}

void CGMDoppelGanger3::Init()
{
}

void CGMDoppelGanger3::Destroy()
{
}

bool CGMDoppelGanger3::CreateObject(OBJECT *o)
{
    if (o->Type == 38 || o->Type == 19 || o->Type == 20 || o->Type == 31 || o->Type == 33)
        o->m_bRenderAfterCharacter = true;

    // 	switch(o->Type)
    // 	{
    // 	}

    return false;
}

bool CGMDoppelGanger3::MoveObject(OBJECT *o)
{
    if (IsDoppelGanger3() == false)
        return false;

    switch (o->Type)
    {
    case 22:
        o->HiddenMesh = -2;
        o->Timer += 0.1f * FPS_ANIMATION_FACTOR;
        if (o->Timer > 10.f)
            o->Timer = 0.f;
        if (o->Timer > 5.f)
            CreateParticleFpsChecked(BITMAP_BUBBLE, o->Position, o->Angle, o->Light);
        return true;
    case 23:
        o->BlendMesh = 0;
        //o->BlendMeshLight = o->Light[1]+1.f;
        o->BlendMeshLight = sinf(WorldTime * 0.002f) * 0.3f + 0.5f;
        return true;
    case 32:
    case 34:
        o->BlendMesh = 1;
        o->BlendMeshLight = sinf(WorldTime * 0.004f) * 0.5f + 0.5f;
        return true;
    case 38:
        o->BlendMesh = 0;
        //o->BlendMeshLight = sinf(WorldTime*0.004f)*0.3f+0.7f;
        return true;
    case 40:
        o->BlendMesh = 0;
        o->BlendMeshLight = sinf(WorldTime * 0.004f) * 0.3f + 0.5f;
        o->Velocity = 0.05f;
        return true;
    case 47:
    case 48:
        o->HiddenMesh = -2;
        return true;
    }

    return false;
}

bool CGMDoppelGanger3::IsDoppelGanger3()
{
    if (gMapManager.ContextMap() == WD_67DOPPLEGANGER3)
    {
        return true;
    }

    return false;
}

CGMDoppelGanger4Ptr CGMDoppelGanger4::Make(SessionKeeper &keeper)
{
    CGMDoppelGanger4Ptr doppelganger(new CGMDoppelGanger4(keeper));
    doppelganger->Init();
    return doppelganger;
}

CGMDoppelGanger4::CGMDoppelGanger4(SessionKeeper &keeper)
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject())
{
}

CGMDoppelGanger4::~CGMDoppelGanger4()
{
    Destroy();
}

void CGMDoppelGanger4::Init()
{
}

void CGMDoppelGanger4::Destroy()
{
}

bool CGMDoppelGanger4::CreateObject(OBJECT *o)
{
    if (o->Type == 76 || o->Type == 77 || o->Type == 91 || o->Type == 92 || o->Type == 95 ||
        o->Type == 105 || o->Type == 19 || o->Type == 20 || o->Type == 31 || o->Type == 33)
        o->m_bRenderAfterCharacter = true;

    // 	switch(o->Type)
    // 	{
    // 	}

    return false;
}

bool CGMDoppelGanger4::MoveObject(OBJECT *o)
{
    if (IsDoppelGanger4() == false)
        return false;

    vec3_t Light;
    float Luminosity;

    switch (o->Type)
    {
    case 47:
    case 48:
    case 59:
    case 62:
    case 81:
    case 82:
    case 83:
    case 107:
    case 108:
        o->HiddenMesh = -2;
        return true;
    case 44:
        o->Velocity = 0.02f;
        return true;
    case 46:
        o->Velocity = 0.01f;
        o->BlendMeshLight = (float)sinf(WorldTime * 0.0015f) * 0.8f + 1.0f;
        return true;
    case 60:
        Luminosity = (float)(WorldRandom() % 4 + 3) * 0.1f;
        Vector(Luminosity * 0.9f, Luminosity * 0.2f, Luminosity * 0.1f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
        o->HiddenMesh = -2;
        return true;
    case 61:
        Luminosity = (float)(WorldRandom() % 4 + 3) * 0.1f;
        Vector(Luminosity, Luminosity * 0.6f, Luminosity * 0.2f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
        o->HiddenMesh = -2;
        return true;
    case 70:
        o->Velocity = 0.04f;
        Luminosity = (float)sinf(WorldTime * 0.002f) * 0.45f + 0.55f;
        Vector(Luminosity * 1.4f, Luminosity * 0.7f, Luminosity * 0.4f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 4, PrimaryTerrainLight);
        return true;
    case 76:
        o->Alpha = 0.5f;
        return true;
    case 77:
        o->BlendMeshTexCoordV = -(int)WorldTime % 10000 * 0.0002f;
        return true;
    case 90:
        o->Velocity = 0.04f;
        return true;
    case 96:
        o->Alpha = 0.5f;
        return true;
    case 97:
        o->HiddenMesh = -2;
        o->Timer += 0.1f * FPS_ANIMATION_FACTOR;
        if (o->Timer > 10.f)
            o->Timer = 0.f;
        if (o->Timer > 5.f)
            CreateParticleFpsChecked(BITMAP_BUBBLE, o->Position, o->Angle, o->Light, 5);
        return true;
    case 102:
        o->BlendMeshLight = (float)sinf(WorldTime * 0.0010f) + 1.0f;
        return true;
    }

    return false;
}

bool CGMDoppelGanger4::IsDoppelGanger4()
{
    if (gMapManager.ContextMap() == WD_68DOPPLEGANGER4)
    {
        return true;
    }

    return false;
}

// 몬스터 사운드

void CGMDoppelGanger4::InstallBehavior()
{
    TheMapProcess().Kanturu1st().InstallBehavior();
}

CGMDuelArenaPtr CGMDuelArena::Make(SessionKeeper &keeper)
{
    CGMDuelArenaPtr duelarena(new CGMDuelArena(keeper));
    duelarena->Init();
    return duelarena;
}

CGMDuelArena::CGMDuelArena(SessionKeeper &keeper)
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject())
{
}

CGMDuelArena::~CGMDuelArena()
{
    Destroy();
}

void CGMDuelArena::Init()
{
}

void CGMDuelArena::Destroy()
{
}

bool CGMDuelArena::CreateObject(OBJECT *o)
{
    switch (o->Type)
    {
    case 0:
    case 1:
    case 32:
        o->CollisionRange = -300;
        return true;
    }

    return false;
}

bool CGMDuelArena::MoveObject(OBJECT *o)
{
    if (IsDuelArena() == false)
        return false;

    switch (o->Type)
    {
    case 35:
    case 36: {
        o->HiddenMesh = -2;
    }
    break;
    case 34: {
        float Luminosity = (float)(WorldRandom() % 3 + 5) * 0.1f;
        vec3_t Light;
        Vector(Luminosity * 0.9f, Luminosity * 0.2f, Luminosity * 0.1f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
        o->HiddenMesh = -2;
    }
    break;
    }

    return false;
}

bool CGMDuelArena::IsDuelArena()
{
    if (gMapManager.ContextMap() == WD_64DUELARENA)
    {
        return true;
    }

    return false;
}

GMDungeon::GMDungeon(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper)
{
}

bool GMDungeon::CreateObject(OBJECT *o)
{
    const int Type = o->Type;
    switch (Type)
    {
    case 59:
        CreateOperate(o);
        break;
    case 60:
        CreateOperate(o);
        Vector(40.f, 40.f, 160.f, o->BoundingBoxMax);
        o->HiddenMesh = -2;
        break;
    }
    return true;
}

bool GMDungeon::MoveObject(OBJECT *o)
{
    vec3_t p{}, Light{};
    float Luminosity;
    switch (o->Type)
    {
    case 52:
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 3.f))
            CreateEffect(MODEL_DUNGEON_STONE01, o->Position, o->Angle, o->Light);
        // CreateEffect(MODEL_STONE1,o->Position,o->Angle,o->Light);
        o->HiddenMesh = -2;
        break;
    case 22:
    case 23:
    case 24:
        Models[o->Type].StreamMesh = 1;
        o->BlendMeshTexCoordV = -(float)((int)WorldTime % 1000) * 0.001f;
        break;
    case 41:
        CreateFire(0, o, 0.f, -30.f, 240.f);
        break;
    case 42:
        CreateFire(0, o, 0.f, 0.f, 190.f);
        break;
    case 39:
    case 40:
    case 51:
        o->HiddenMesh = -2;
        break;
    }
    return true;
}

void GMDungeon::InstallBehavior()
{
    Models[40].Actions[1].PlaySpeed = 0.4f;
}

bool GMDungeon::StopMonster(CHARACTER *character, OBJECT *object)
{
    if (object->Type != 40)
        return false;
    SetAction(&character->Object, 0);
    return true;
}

MapObjectInteraction GMDungeon::ObjectInteraction(int type, CHARACTER &actor)
{
    using Action = MapObjectInteraction::Action;
    if (type == 60)
        return {Action::Pose, true};
    if (type == 59)
        return {Action::Sit, false};
    return {};
}

GMEmpireGuardian2Ptr GMEmpireGuardian2::Make(SessionKeeper &keeper)
{
    GMEmpireGuardian2Ptr doppelganger(new GMEmpireGuardian2(keeper));
    doppelganger->Init();
    return doppelganger;
}

GMEmpireGuardian2::GMEmpireGuardian2(SessionKeeper &keeper)
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject())
{
}

GMEmpireGuardian2::~GMEmpireGuardian2()
{
    Destroy();
}

void GMEmpireGuardian2::Init()
{
}

void GMEmpireGuardian2::Destroy()
{
}

bool GMEmpireGuardian2::CreateObject(OBJECT *o)
{
    switch (o->Type)
    {
    case 129:
    case 130:
    case 131:
    case 132: {
        o->Angle[2] = (float)((int)o->Angle[2] % 360);
        VectorCopy(o->Angle, o->HeadAngle);
        VectorCopy(o->Position, o->HeadTargetAngle);
    }
        return true;

    case 115:
    case 117: {
        o->SubType = 100;
    }
        return true;
    }

    return false;
}

CHARACTER *GMEmpireGuardian2::CreateMonster(int iType, int PosX, int PosY, int Key)
{
    CHARACTER *pCharacter = g_EmpireGuardian1.CreateMonster(iType, PosX, PosY, Key);

    if (NULL != pCharacter)
    {
        return pCharacter;
    }

    switch (iType)
    {
    case MONSTER_HAMMERIZE: {
        OpenMonsterModel(MONSTER_MODEL_HAMMERIZE);
        pCharacter = CreateCharacter(Key, MODEL_HAMMERIZE, PosX, PosY);

        pCharacter->Object.Scale = 1.3f;

        m_bCurrentIsRage_Bermont = false;
    }
    break;
    case MONSTER_ATICLES_HEAD: {
        OpenMonsterModel(MONSTER_MODEL_ATICLES_HEAD);
        pCharacter = CreateCharacter(Key, MODEL_ATICLES_HEAD, PosX, PosY);

        pCharacter->Object.Scale = 1.35f;
    }
    break;
    case MONSTER_DARK_GHOST: {
        OpenMonsterModel(MONSTER_MODEL_DARK_GHOST);
        pCharacter = CreateCharacter(Key, MODEL_DARK_GHOST, PosX, PosY);

        OBJECT *pObject = &pCharacter->Object;
        pObject->Scale = 1.3f;

        MoveEye(pObject, &Models[pObject->Type], 79, 33);
        CreateJoint(BITMAP_JOINT_ENERGY, pObject->Position, pObject->Position, pObject->Angle, 22,
                    pObject, 30.f);
        CreateJoint(BITMAP_JOINT_ENERGY, pObject->Position, pObject->Position, pObject->Angle, 23,
                    pObject, 30.f);
    }
    break;

    default:
        return pCharacter;
    }

    return pCharacter;
}

bool GMEmpireGuardian2::MoveObject(OBJECT *o)
{
    if (gMapManager.IsEmpireGuardian2() == false)
        return false;

    Alpha(o, FPS_ANIMATION_FACTOR);
    if (o->Alpha < 0.01f)
        return false;

    BMD *b = &Models[o->Type];
    float fSpeed = o->Velocity;
    switch (o->Type)
    {
    case 20: {
        fSpeed *= 2.0f;
    }
    break;

    case 122:
    case 123:
    case 124: {
        fSpeed *= 3.0f;
    }
    break;

    case 128: {
        fSpeed *= 6.0f;
    }
    break;
    }

    b->PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame, &o->PriorAction, fSpeed,
                     o->Position, o->Angle);

    switch (o->Type)
    {
    case 20: {
        if (objectAnimationFrame_ - o->AnimationFrame > 10 ||
            objectAnimationFrame_ < o->AnimationFrame)
            objectAnimationFrame_ = o->AnimationFrame;
        else
            o->AnimationFrame = objectAnimationFrame_;
    }
        return true;
    case 64: {
        o->Velocity = 0.64f;
    }
        return true;
    case 79:
    case 80:
    case 82:
    case 83:
    case 84:
    case 85:
    case 86:
    case 129:
    case 130:
    case 131:
    case 132: {
        o->HiddenMesh = -2;
    }
    break;
    case 81: {
        o->BlendMeshTexCoordV += (0.015f) * FPS_ANIMATION_FACTOR;
    }
        return true;
    case 36: {
        o->Velocity = 0.02f;
    }
        return true;
    }

    return false;
}

void GMEmpireGuardian2::SetWeather(int weather)
{
    g_EmpireGuardian1.SetWeather(weather);
}

bool GMEmpireGuardian2::SetCurrentActionMonster(CHARACTER *c, OBJECT *o)
{
    if (gMapManager.IsEmpireGuardian2() == false)
        return false;

    if (true == g_EmpireGuardian1.SetCurrentActionMonster(c, o))
    {
        return true;
    }

    switch (c->MonsterIndex)
    {
    case MONSTER_HAMMERIZE: {
        if (m_bCurrentIsRage_Bermont == true)
        {
            SetAction(o, MONSTER01_APEAR);
            c->MonsterSkill = -1;
            return true;
        }

        switch (c->MonsterSkill)
        {
        case 60: {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        break;
        case 61: {
            SetAction(o, MONSTER01_ATTACK3);
            c->MonsterSkill = -1;
        }
        break;
        case ATMON_SKILL_EMPIREGUARDIAN_BERSERKER: {
            SetAction(o, MONSTER01_APEAR);
            c->MonsterSkill = -1;

            m_bCurrentIsRage_Bermont = true;
        }
        break;
        default: {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        break;
        }
    }
        return true;
    case MONSTER_ATICLES_HEAD: {
        switch (c->MonsterSkill)
        {
        case 47: {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        break;
        case 50: {
            SetAction(o, MONSTER01_APEAR);
            c->MonsterSkill = -1;
        }
        break;
        default: {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        break;
        }
    }
        return true;
    case MONSTER_DARK_GHOST: {
        switch (c->MonsterSkill)
        {
        case 51: {
            SetAction(o, MONSTER01_ATTACK2);
        }
        break;
        case 52: {
            SetAction(o, MONSTER01_ATTACK2);
        }
        break;
        default: {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        break;
        }
    }
    case MONSTER_DEFENDER: {
    }
        return true;
    case MONSTER_FORSAKER: {
        switch (c->MonsterSkill)
        {
        case 46: {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        break;
        default: {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        break;
        }
    }
        return true;
    }
    return false;
}

void GMEmpireGuardian2::AdvanceMonsterState(CHARACTER &character, BMD &model)
{
    auto &object = character.Object;
    g_EmpireGuardian1.AdvanceMonsterState(character, model);

    if (!gMapManager.IsEmpireGuardian2())
        return;
    if (object.Type == MODEL_DARK_GHOST && object.CurrentAction == MONSTER01_DIE)
    {
        object.BlendMesh = -2;
        object.m_bRenderShadow = false;
    }
}

void GMEmpireGuardian2::InstallBehavior()
{
    g_EmpireGuardian1.InstallBehavior();
}

GMEmpireGuardian3Ptr GMEmpireGuardian3::Make(SessionKeeper &keeper)
{
    GMEmpireGuardian3Ptr empire(new GMEmpireGuardian3(keeper));
    empire->Init();
    return empire;
}

GMEmpireGuardian3::GMEmpireGuardian3(SessionKeeper &keeper)
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject())
{
    // 	m_iWeather = WEATHER_TYPE::WEATHER_SUN;
}

GMEmpireGuardian3::~GMEmpireGuardian3()
{
    Destroy();
}

void GMEmpireGuardian3::Init()
{
}

void GMEmpireGuardian3::Destroy()
{
}

bool GMEmpireGuardian3::CreateObject(OBJECT *o)
{
    switch (o->Type)
    {
    case 129:
    case 130:
    case 131:
    case 132: {
        o->Angle[2] = (float)((int)o->Angle[2] % 360);
        VectorCopy(o->Angle, o->HeadAngle);
        VectorCopy(o->Position, o->HeadTargetAngle);
    }
        return true;

    case 115:
    case 117: {
        o->SubType = 100;
    }
        return true;
    }

    return false;
}

CHARACTER *GMEmpireGuardian3::CreateMonster(int iType, int PosX, int PosY, int Key)
{
    CHARACTER *pCharacter = g_EmpireGuardian1.CreateMonster(iType, PosX, PosY, Key);

    if (NULL != pCharacter)
    {
        return pCharacter;
    }

    switch (iType)
    {
    case MONSTER_DUAL_BERSERKER: {
        OpenMonsterModel(MONSTER_MODEL_DUAL_BERSERKER);
        pCharacter = CreateCharacter(Key, MODEL_DUAL_BERSERKER, PosX, PosY);
        pCharacter->Object.Scale = 1.35f;

        m_bCurrentIsRage_Kato = false;
    }
    break;
    case MONSTER_BANSHEE: {
        OpenMonsterModel(MONSTER_MODEL_BANSHEE);
        pCharacter = CreateCharacter(Key, MODEL_BANSHEE, PosX, PosY);
        pCharacter->Object.Scale = 1.55f;
    }
    break;
    case MONSTER_HEAD_MOUNTER: {
        OpenMonsterModel(MONSTER_MODEL_HEAD_MOUNTER);
        pCharacter = CreateCharacter(Key, MODEL_HEAD_MOUNTER, PosX, PosY);
        pCharacter->Object.Scale = 1.25f;
    }
    break;

    default:
        return pCharacter;
    }

    return pCharacter;
}

bool GMEmpireGuardian3::MoveObject(OBJECT *o)
{
    if (gMapManager.IsEmpireGuardian3() == false)
        return false;

    Alpha(o, FPS_ANIMATION_FACTOR);
    if (o->Alpha < 0.01f)
        return false;

    BMD *b = &Models[o->Type];
    float fSpeed = o->Velocity;
    switch (o->Type)
    {
    case 20: {
        fSpeed *= 2.0f;
    }
    break;

    case 122:
    case 123:
    case 124: {
        fSpeed *= 3.0f;
    }
    break;

    case 128: {
        fSpeed *= 6.0f;
    }
    break;
    }

    b->PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame, &o->PriorAction, fSpeed,
                     o->Position, o->Angle);

    switch (o->Type)
    {
    case 20: {
        if (objectAnimationFrame_ - o->AnimationFrame > 10 ||
            objectAnimationFrame_ < o->AnimationFrame)
            objectAnimationFrame_ = o->AnimationFrame;
        else
            o->AnimationFrame = objectAnimationFrame_;
    }
        return true;
    case 64: {
        o->Velocity = 0.44f;
    }
        return true;
    case 79:
    case 80:
    case 82:
    case 83:
    case 84:
    case 85:
    case 86:
    case 129:
    case 130:
    case 131:
    case 132: {
        o->HiddenMesh = -2;
    }
        return true;
    case 81: {
        o->BlendMeshTexCoordV += (0.015f) * FPS_ANIMATION_FACTOR;
    }
        return true;
    case 36: {
        o->Velocity = 0.02f;
    }
        return true;
    }

    return false;
}

void GMEmpireGuardian3::SetWeather(int weather)
{
    g_EmpireGuardian1.SetWeather(weather);
}

bool GMEmpireGuardian3::SetCurrentActionMonster(CHARACTER *c, OBJECT *o)
{
    if (gMapManager.IsEmpireGuardian3() == false)
        return false;

    if (true == g_EmpireGuardian1.SetCurrentActionMonster(c, o))
    {
        return true;
    }

    switch (c->MonsterIndex)
    {
    case MONSTER_DUAL_BERSERKER: {
        if (m_bCurrentIsRage_Kato == true)
        {
            SetAction(o, MONSTER01_APEAR);
            c->MonsterSkill = -1;
            return true;
        }

        switch (c->MonsterSkill)
        {
        case 58: {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        break;
        case 60: {
            SetAction(o, MONSTER01_ATTACK3);
            c->MonsterSkill = -1;
        }
        break;
        case ATMON_SKILL_EMPIREGUARDIAN_BERSERKER: {
            SetAction(o, MONSTER01_APEAR);
            c->MonsterSkill = -1;

            m_bCurrentIsRage_Kato = true;
        }
        break;
        default: {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        break;
        }
    }
        return true;
    case MONSTER_HEAD_MOUNTER: {
        switch (c->MonsterSkill)
        {
        case 54: {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        break;
        case 55: {
            SetAction(o, MONSTER01_APEAR);
            c->MonsterSkill = -1;
        }
        break;
        case 56: {
            SetAction(o, MONSTER01_ATTACK3);
            c->MonsterSkill = -1;
        }
        break;
        default: {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        break;
        }
    }
        return true;
    case MONSTER_FORSAKER: {
        switch (c->MonsterSkill)
        {
        case 46: {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        break;
        default: {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        break;
        }
    }
        return true;
    case MONSTER_BANSHEE: {
        switch (c->MonsterSkill)
        {
        case 47: {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        break;
        case 53: {
            SetAction(o, MONSTER01_ATTACK3);
            c->MonsterSkill = -1;
        }
        break;
        default: {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        break;
        }
    }
        return true;
    }
    return false;
}

void GMEmpireGuardian3::AdvanceMonsterState(CHARACTER &character, BMD &model)
{
    g_EmpireGuardian1.AdvanceMonsterState(character, model);
}

void GMEmpireGuardian3::InstallBehavior()
{
    g_EmpireGuardian1.InstallBehavior();
}

GMEmpireGuardian4Ptr GMEmpireGuardian4::Make(SessionKeeper &keeper)
{
    GMEmpireGuardian4Ptr doppelganger(new GMEmpireGuardian4(keeper));
    doppelganger->Init();
    return doppelganger;
}

GMEmpireGuardian4::GMEmpireGuardian4(SessionKeeper &keeper)
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject())
{
}

GMEmpireGuardian4::~GMEmpireGuardian4()
{
    Destroy();
}

void GMEmpireGuardian4::Init()
{
}

void GMEmpireGuardian4::Destroy()
{
}

bool GMEmpireGuardian4::CreateObject(OBJECT *o)
{
    switch (o->Type)
    {
    case 129:
    case 130:
    case 131:
    case 132: {
        o->Angle[2] = (float)((int)o->Angle[2] % 360);
        VectorCopy(o->Angle, o->HeadAngle);
        VectorCopy(o->Position, o->HeadTargetAngle);
    }
        return true;

    case 115:
    case 117: {
        o->SubType = 100;
    }
        return true;
    case 10: {
        o->Timer = static_cast<float>(WorldRandom() % 50);
    }
        return true;
    }

    return false;
}

CHARACTER *GMEmpireGuardian4::CreateMonster(int iType, int PosX, int PosY, int Key)
{
    CHARACTER *pCharacter = g_EmpireGuardian1.CreateMonster(iType, PosX, PosY, Key);

    if (NULL != pCharacter)
    {
        return pCharacter;
    }

    switch (iType)
    {
    case MONSTER_GAYION_THE_GLADIATOR: {
        OpenMonsterModel(MONSTER_MODEL_GAYION);
        pCharacter = CreateCharacter(Key, MODEL_GAYION, PosX, PosY);
        memset(pCharacter->ID, 0, sizeof(pCharacter->ID));
        std::wstring(L"Gayion The Gladiator").copy(pCharacter->ID, 19);

        pCharacter->Object.Scale = 1.40f;

        m_bCurrentIsRage_BossGaion = false;
    }
    break;
    case MONSTER_JERRY:
    case MONSTER_ADVISER_JERINTEU: {
        OpenMonsterModel(MONSTER_MODEL_JERRY);
        pCharacter = CreateCharacter(Key, MODEL_JERRY, PosX, PosY);
        memset(pCharacter->ID, 0, sizeof(pCharacter->ID));
        std::wstring(L"Jerry The Adviseru").copy(pCharacter->ID, 19);
        pCharacter->Object.Scale = 1.45f;

        m_bCurrentIsRage_Jerint = false;
    }
    break;
    case MONSTER_STAR_GATE: {
        OpenMonsterModel(MONSTER_MODEL_STAR_GATE);
        pCharacter = CreateCharacter(Key, MODEL_STAR_GATE, PosX, PosY);
        memset(pCharacter->ID, 0, sizeof(pCharacter->ID));
        std::wstring(L"Star Gate").copy(pCharacter->ID, 10);
        pCharacter->Object.m_bRenderShadow = false;
        pCharacter->Object.Scale = 1.25f;
    }
    break;

    case MONSTER_RUSH_GATE: {
        OpenMonsterModel(MONSTER_MODEL_RUSH_GATE);
        pCharacter = CreateCharacter(Key, MODEL_RUSH_GATE, PosX, PosY);
        memset(pCharacter->ID, 0, sizeof(pCharacter->ID));
        std::wstring(L"Rush Gate").copy(pCharacter->ID, 10);
        pCharacter->Object.m_bRenderShadow = false;
        pCharacter->Object.LifeTime = 100;
        pCharacter->Object.Scale = 1.25f;
    }
    break;

    default:
        return pCharacter;
    }

    return pCharacter;
}

bool GMEmpireGuardian4::MoveObject(OBJECT *o)
{
    if (gMapManager.IsEmpireGuardian4() == false)
        return false;

    Alpha(o, FPS_ANIMATION_FACTOR);
    if (o->Alpha < 0.01f)
        return false;

    BMD *b = &Models[o->Type];
    float fSpeed = o->Velocity;

    switch (o->Type)
    {
    case 20: {
        fSpeed *= 2.0f;
    }
    break;

    case 122:
    case 123:
    case 124: {
        fSpeed *= 3.0f;
    }
    break;

    case 128: {
        fSpeed *= 6.0f;
    }
    break;
    case 10: {
        if (o->Timer > 0.f)
        {
            o->Timer = (std::max)(0.f, o->Timer - FPS_ANIMATION_FACTOR);
            o->AnimationFrame = 0.0f;
            o->PriorAnimationFrame = 0.0f;
        }
    }
    break;
    }

    b->PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame, &o->PriorAction, fSpeed,
                     o->Position, o->Angle);

    switch (o->Type)
    {
    case 20: {
        if (objectAnimationFrame_ - o->AnimationFrame > 10 ||
            objectAnimationFrame_ < o->AnimationFrame)
            objectAnimationFrame_ = o->AnimationFrame;
        else
            o->AnimationFrame = objectAnimationFrame_;
    }
        return true;
    case 64: {
        o->Velocity = 0.64f;
    }
        return true;
    case 79:
    case 80:
    case 82:
    case 83:
    case 84:
    case 85:
    case 86:
    case 129:
    case 130:
    case 131:
    case 132: {
        o->HiddenMesh = -2;
    }
        return true;
    case 81: {
        o->BlendMeshTexCoordV += (0.015f) * FPS_ANIMATION_FACTOR;
    }
        return true;
    case 36: {
        o->Velocity = 0.02f;
    }
        return true;
    }

    return false;
}

void GMEmpireGuardian4::SetWeather(int weather)
{
    g_EmpireGuardian1.SetWeather(weather);
}

bool GMEmpireGuardian4::SetCurrentActionMonster(CHARACTER *c, OBJECT *o)
{
    if (gMapManager.IsEmpireGuardian4() == false)
        return false;

    if (true == g_EmpireGuardian1.SetCurrentActionMonster(c, o))
    {
        return true;
    }

    switch (c->MonsterIndex)
    {
    case MONSTER_GAYION_THE_GLADIATOR: {
        if (m_bCurrentIsRage_BossGaion == true)
        {
            SetAction(o, MONSTER01_APEAR);
            c->MonsterSkill = -1;
            return true;
        }

        switch (c->MonsterSkill)
        {
        case ATMON_SKILL_EMPIREGUARDIAN_GAION_01_GENERALATTACK: {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        break;
        case ATMON_SKILL_EMPIREGUARDIAN_GAION_02_BLOODATTACK: {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        break;
        case ATMON_SKILL_EMPIREGUARDIAN_GAION_03_GIGANTIKSTORM: {
            SetAction(o, MONSTER01_ATTACK3);
            c->MonsterSkill = -1;
        }
        break;
        case ATMON_SKILL_EMPIREGUARDIAN_GAION_04_FLAMEATTACK: {
            SetAction(o, MONSTER01_ATTACK4);
            c->MonsterSkill = -1;
        }
        break;
        case ATMON_SKILL_EMPIREGUARDIAN_BERSERKER: {
            SetAction(o, MONSTER01_APEAR);
            c->MonsterSkill = -1;
            m_bCurrentIsRage_BossGaion = true;
        }
        break;
        default: {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        break;
        }

        return true;
    }
        return true;
    case MONSTER_JERRY: {
        if (m_bCurrentIsRage_Jerint == true)
        {
            SetAction(o, MONSTER01_APEAR);
            c->MonsterSkill = -1;
            return true;
        }

        switch (c->MonsterSkill)
        {
        case 55: {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        break;
        case 61: {
            SetAction(o, MONSTER01_ATTACK3);
            c->MonsterSkill = -1;
        }
        break;
        case ATMON_SKILL_EMPIREGUARDIAN_BERSERKER: {
            SetAction(o, MONSTER01_APEAR);
            c->MonsterSkill = -1;

            m_bCurrentIsRage_Jerint = true;
        }
        break;
        default: {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        break;
        }
    }
        return true;
    }
    return false;
}

void GMEmpireGuardian4::AdvanceMonsterState(CHARACTER &character, BMD &model)
{
    AdvanceGatePlacement(&character.Object);
    g_EmpireGuardian1.AdvanceMonsterState(character, model);
}

void GMEmpireGuardian4::InstallBehavior()
{
    g_EmpireGuardian1.InstallBehavior();
}

CGMGmAreaPtr CGMGmArea::Make(SessionKeeper &keeper)
{
    return CGMGmAreaPtr(new CGMGmArea(keeper));
}

CGMGmArea::CGMGmArea(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject())
{
}

bool CGMGmArea::IsGmArea()
{
    return gMapManager.ContextMap() == WD_40AREA_FOR_GM;
}

bool CGMGmArea::CreateObject(OBJECT *object)
{
    if (object->Type == 76 || object->Type == 77 || object->Type == 91 || object->Type == 92 ||
        object->Type == 95 || object->Type == 105)
        object->m_bRenderAfterCharacter = true;

    return TheMapProcess().Kanturu1st().CreateKanturu1stObject(object);
}

bool CGMGmArea::MoveObject(OBJECT *object)
{
    return TheMapProcess().Kanturu1st().MoveKanturu1stObject(object);
}

GMIcarus::GMIcarus(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      Random(keeper.RandomForConstruction())
{
}

#ifdef ASG_ADD_MAP_KARUTAN

CGMKarutan1::CGMKarutan1(SessionKeeper &keeper)
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject())
{
}

CGMKarutan1::~CGMKarutan1()
{
}

CGMKarutan1Ptr CGMKarutan1::Make(SessionKeeper &keeper)
{
    CGMKarutan1Ptr karutan(new CGMKarutan1(keeper));
    return karutan;
}

bool CGMKarutan1::CreateObject(OBJECT *o)
{
    if (o->Type == 66 || o->Type == 1 || o->Type == 3 || o->Type == 54 || o->Type == 55 ||
        o->Type == 56 || o->Type == 57 || o->Type == 58 || o->Type == 62 || o->Type == 63 ||
        o->Type == 119)
        o->m_bRenderAfterCharacter = true;

    return false;
}

bool CGMKarutan1::MoveObject(OBJECT *o)
{
    if (!IsKarutanMap())
        return false;

    switch (o->Type)
    {
    case 66:
        if (o->AnimationFrame >= 19)
            SetAction(o, rand_fps_check(10) ? 1 : 0);
        return true;

    case 113: {
        vec3_t vLight;
        float fLuminosity = (float)(WorldRandom() % 4 + 3) * 0.1f;
        Vector(fLuminosity, fLuminosity * 0.6f, fLuminosity * 0.2f, vLight);
        AddTerrainLight(o->Position[0], o->Position[1], vLight, 3, PrimaryTerrainLight);
    }
    case 114:
    case 115:
    case 116:
    case 118:
        o->HiddenMesh = -2;
        return true;
    }

    return false;
}

#ifdef ASG_ADD_KARUTAN_MONSTERS
CHARACTER *CGMKarutan1::CreateMonster(int iType, int PosX, int PosY, int Key)
{
    CHARACTER *pCharacter = NULL;

    switch (iType)
    {
    case MONSTER_VENOMOUS_CHAIN_SCORPION:
        OpenMonsterModel(MONSTER_MODEL_VENOMOUS_CHAIN_SCORPION);
        pCharacter = CreateCharacter(Key, MODEL_VENOMOUS_CHAIN_SCORPION, PosX, PosY);
        pCharacter->Object.Scale = 1.0f;
        break;
    case MONSTER_BONE_SCORPION:
        OpenMonsterModel(MONSTER_MODEL_BONE_SCORPION);
        pCharacter = CreateCharacter(Key, MODEL_BONE_SCORPION, PosX, PosY);
        pCharacter->Object.Scale = 0.58f;
        break;
    case MONSTER_ORCUS:
        OpenMonsterModel(MONSTER_MODEL_ORCUS);
        pCharacter = CreateCharacter(Key, MODEL_ORCUS, PosX, PosY);
        pCharacter->Object.Scale = 0.64f;
        break;
    case MONSTER_GOLLOCK:
        OpenMonsterModel(MONSTER_MODEL_GOLLOCK);
        pCharacter = CreateCharacter(Key, MODEL_GOLLOCK, PosX, PosY);
        pCharacter->Object.Scale = 1.5f;
        break;
    case MONSTER_CRYPTA:
        OpenMonsterModel(MONSTER_MODEL_CRYPTA);
        pCharacter = CreateCharacter(Key, MODEL_CRYPTA, PosX, PosY);
        pCharacter->Object.Scale = 1.5f;
        break;
    case MONSTER_CRYPOS:
        OpenMonsterModel(MONSTER_MODEL_CRYPOS);
        pCharacter = CreateCharacter(Key, MODEL_CRYPOS, PosX, PosY);
        pCharacter->Object.Scale = 1.25f;
        break;
    case MONSTER_CONDRA:
        OpenMonsterModel(MONSTER_MODEL_CONDRA);
        pCharacter = CreateCharacter(Key, MODEL_CONDRA, PosX, PosY);
        pCharacter->Object.Scale = 1.45f;
        pCharacter->Object.LifeTime = 100;
        break;
    case MONSTER_NARCONDRA:
        OpenMonsterModel(MONSTER_MODEL_NACONDRA);
        pCharacter = CreateCharacter(Key, MODEL_NACONDRA, PosX, PosY);
        pCharacter->Object.Scale = 1.55f;
        pCharacter->Object.LifeTime = 100;

        OBJECT *o = &pCharacter->Object;

        vec3_t vColor = {1.5f, 0.1f, 0.5f};
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 24, o, 10.f, -1, 0, 0,
                    -1, vColor);
        break;
    }

    return pCharacter;
}

#endif // ASG_ADD_KARUTAN_MONSTERS

bool CGMKarutan1::IsKarutanMap()
{
    return gMapManager.ContextMap() == WD_80KARUTAN1 || gMapManager.ContextMap() == WD_81KARUTAN2;
}

void CGMKarutan1::InstallBehavior()
{
#ifdef ASG_ADD_KARUTAN_MONSTERS
    LoadWaveFile(SOUND_KARUTAN_DESERT_ENV, L"Data\\Sound\\Karutan\\Karutan_desert_env.wav", 1);
    LoadWaveFile(SOUND_KARUTAN_INSECT_ENV, L"Data\\Sound\\Karutan\\Karutan_insect_env.wav", 1);
    LoadWaveFile(SOUND_KARUTAN_KARDAMAHAL_ENV, L"Data\\Sound\\Karutan\\Kardamahal_entrance_env.wav",
                 1);
#endif
    Models[66].Actions[0].PlaySpeed = 0.15f;
    Models[66].Actions[1].PlaySpeed = 0.15f;
    Models[107].Actions[0].PlaySpeed = 5.f;
}

#endif // ASG_ADD_MAP_KARUTAN

GMLegacyLogin::GMLegacyLogin(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper)
{
}

bool GMLegacyLogin::CreateObject(OBJECT *o)
{
    const int Type = o->Type;
    {
        switch (Type)
        {
        case 90:
        case 86:
            o->Position[0] = 8600.0f;
            o->Position[1] = 25000.0f;
            if (Type == 90)
            {
                o->Scale = 110.0f;
                o->Position[2] = 5000.0f;
            }
            else
            {
                o->Scale = 60.0f;
                o->Position[2] = 5000.0f;
            }
            Vector(0.0f, 0.0f, 0.0f, o->Angle);
            break;
        }
    }
    return true;
}

bool GMLegacyLogin::MoveObject(OBJECT *o)
{
    vec3_t p{}, Light{};
    float Luminosity;
    {
        switch (o->Type)
        {
        case 84:
        case 85:
        case 87:
        case 89:
            o->HiddenMesh = -2;
            break;
        case 90:
        case 86:
            if (o->Type == 90)
            {
                o->Alpha = 1.0f;
                Vector(1.0f, 1.0f, 1.0f, o->Light);
                o->Angle[2] -= (0.23f) * FPS_ANIMATION_FACTOR;
            }
            else
            {
                o->Alpha = 0.2f;
                o->Angle[2] -= (0.6f) * FPS_ANIMATION_FACTOR;
                Vector(1.0f, 0.0f, 0.0f, o->Light);
            }

            float fTemp1 = sinf(WorldTime * 0.001f) * 0.2 + 0.6f;
            o->BlendMesh = -2;
            break;
        }
    }
    return true;
}

void GMLegacyLogin::InstallBehavior()
{
    vec3_t Pos, Ang;
    Vector(0.f, 0.f, 0.f, Ang);
    Vector(0.f, 0.f, 0.f, Pos);
    Pos[0] = 56 * TERRAIN_SCALE;
    Pos[1] = 230 * TERRAIN_SCALE;
    SessionLegacyCalls::CreateObject(MODEL_DRAGON, Pos, Ang);
}

GMLorencia::GMLorencia(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      Random(keeper.RandomForConstruction())
{
}

bool GMLorencia::CreateObject(OBJECT *o)
{
    const int Type = o->Type;
    switch (Type)
    {
    case MODEL_BRIDGE:
        o->CollisionRange = -50.f;
        break;
    case MODEL_HOUSE01 + 2:
        o->SetBlendMesh(4);
        break;
    case MODEL_HOUSE01 + 3:
        o->SetBlendMesh(8);
        break;
    case MODEL_HOUSE01 + 4:
        o->SetBlendMesh(2);
        break;
    case MODEL_HOUSE_WALL01 + 1:
        o->SetBlendMesh(4);
        break;
    case MODEL_WATERSPOUT:
        o->SetBlendMesh(3);
        break;
    case MODEL_BONFIRE:
        o->SetBlendMesh(1);
        break;
    case MODEL_CARRIAGE01:
        o->SetBlendMesh(2);
        break;
    case MODEL_TREE01:
    case MODEL_TREE01 + 1:
        // case MODEL_TREE01+10:
        Vector(-150.f, -150.f, 0.f, o->BoundingBoxMin);
        Vector(150.f, 150.f, 500.f, o->BoundingBoxMax);
        o->Velocity = 1.f / o->Scale * 0.4f;
        // o->AlphaEnable = true;
        break;
    case MODEL_STREET_LIGHT:
        o->SetBlendMesh(1);
        o->Velocity = 0.3f;
        break;
    case MODEL_CANDLE:
        o->SetBlendMesh(1);
        o->Velocity = 0.3f;
        break;
    case MODEL_TREASURE_CHEST:
        o->Velocity = 0.f;
        break;
    case MODEL_SIGN01:
    case MODEL_SIGN01 + 1:
        o->Velocity = 0.3f;
        // CreateNpc(o);
        break;
    case MODEL_POSE_BOX:
        CreateOperate(o);
        Vector(40.f, 40.f, 160.f, o->BoundingBoxMax);
        o->SetHiddenMesh(-2);
        break;
    case MODEL_TREE01 + 6:
    case MODEL_FURNITURE01 + 5:
    case MODEL_FURNITURE01 + 6:
        CreateOperate(o);
        break;
    }
    return true;
}

bool GMLorencia::MoveObject(OBJECT *o)
{
    vec3_t p{}, Light{};
    float Luminosity;
    switch (o->Type)
    {
    case MODEL_HOUSE01 + 3:
    case MODEL_HOUSE01 + 4:
        o->BlendMeshTexCoordV = -(int)WorldTime % 1000 * 0.001f;
        break;
    case MODEL_HOUSE01 + 2:
    case MODEL_HOUSE_WALL01 + 1:
        o->BlendMeshLight = (float)(WorldRandom() % 4 + 4) * 0.1f;
        break;
    case MODEL_LIGHT01:
        CreateFire(0, o, 0.f, 0.f, 0.f);
        o->SetHiddenMesh(-2);
        break;
    case MODEL_LIGHT01 + 1:
        CreateFire(1, o, 0.f, 0.f, 0.f);
        o->SetHiddenMesh(-2);
        break;
    case MODEL_LIGHT01 + 2:
        CreateFire(2, o, 0.f, 0.f, 0.f);
        o->SetHiddenMesh(-2);
        break;
    case MODEL_BRIDGE:
        CreateFire(0, o, 90.f, -200.f, 30.f);
        CreateFire(0, o, 90.f, 200.f, 30.f);
        break;
    case MODEL_DUNGEON_GATE:
        CreateFire(0, o, -150.f, -150.f, 140.f);
        CreateFire(0, o, 150.f, -150.f, 140.f);
        break;
    case MODEL_FIRE_LIGHT01:
        CreateFire(0, o, 0.f, 0.f, 200.f);
        break;
    case MODEL_FIRE_LIGHT01 + 1:
        CreateFire(0, o, 0.f, -30.f, 60.f);
        break;
    case MODEL_STREET_LIGHT:
        Luminosity = (float)(WorldRandom() % 2 + 6) * 0.1f;
        Vector(Luminosity, Luminosity * 0.8f, Luminosity * 0.6f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
        break;
    case MODEL_BONFIRE:
        CreateFire(0, o, 0.f, 0.f, 60.f);
        o->BlendMeshLight = (float)(WorldRandom() % 6 + 4) * 0.1f;
        // CreateBonfire(o->Position,o->Angle);
        break;

    case MODEL_CANDLE:
        Luminosity = (float)(WorldRandom() % 4 + 3) * 0.1f;
        Vector(Luminosity, Luminosity * 0.6f, Luminosity * 0.2f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
        break;

    case MODEL_SIGN01 + 1:
        Vector(50.f, -10.f, 120.f, p);
        // CreateShiny(o,p);
        break;
    }
    return true;
}

MapObjectInteraction GMLorencia::ObjectInteraction(int type, CHARACTER &actor)
{
    using Action = MapObjectInteraction::Action;
    switch (type)
    {
    case MODEL_POSE_BOX:
        return {Action::Pose, true};
    case MODEL_TREE01 + 6:
    case MODEL_FURNITURE01 + 6:
        return {Action::Sit, false};
    case MODEL_FURNITURE01 + 5:
        return {Action::Sit, true};
    default:
        return {};
    }
}

GMLostTower::GMLostTower(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper)
{
}

bool GMLostTower::MoveObject(OBJECT *o)
{
    vec3_t p{}, Light{};
    float Luminosity;
    switch (o->Type)
    {
    case 38:
    case 39:
        CheckSkull(o);
        break;
    case 3:
    case 4:
        o->BlendMeshTexCoordU = -(int)WorldTime % 1000 * 0.001f;
        break;
    case 19:
    case 20:
        o->BlendMesh = 4;
        o->BlendMeshTexCoordU = -(int)WorldTime % 1000 * 0.001f;
        break;
    case 18:
        o->BlendMesh = 1;
        break;
    case 23:
        o->BlendMesh = 1;
        // b->TransformPosition(BoneTransform[1],p,Position);
        // CreateSprite(BITMAP_LIGHT,Position,2.f,Light,o);
        break;
    case 24:
        o->HiddenMesh = -2;
        for (auto emission : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR / 64.f))
            CreateEffect(BITMAP_FLAME, o->Position, o->Angle, o->Light);
        // o->BlendMeshTexCoordV = (int)WorldTime%1000 * 0.001f;
        break;
    case 25:
        o->HiddenMesh = -2;
        break;
    }
    return true;
}

using namespace SEASON3B;

GMNewTownPtr GMNewTown::Make(SessionKeeper &keeper)
{
    return GMNewTownPtr(new GMNewTown(keeper));
}

GMNewTown::GMNewTown(SessionKeeper &keeper)
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      Boids(keeper.BoidsStorage()), gMapManager(keeper.MapManagerObject()),
      empireGuardian4_(keeper.GameplayForConstruction().TheMapProcess().EmpireGuardian4())
{
}

GMNewTown::~GMNewTown()
{
}

bool GMNewTown::IsCurrentMap() const
{
    return (gMapManager.ContextMap() == WD_51HOME_6TH_CHAR ||
            gMapManager.ContextMap() == WD_73NEW_LOGIN_SCENE ||
            gMapManager.ContextMap() == WD_74NEW_CHARACTER_SCENE);
}
bool GMNewTown::IsNewMap73_74() const
{
    return (gMapManager.ContextMap() == WD_73NEW_LOGIN_SCENE ||
            gMapManager.ContextMap() == WD_74NEW_CHARACTER_SCENE);
}

bool GMNewTown::CreateObject(OBJECT *pObject)
{
    if (gMapManager.ContextMap() == WD_51HOME_6TH_CHAR &&
        (pObject->Type == 53 || pObject->Type == 55 || pObject->Type == 110 ||
         pObject->Type == 89 || pObject->Type == 78 || pObject->Type == 79 ||
         pObject->Type == 125 || pObject->Type == 128 || pObject->Type == 2))
        pObject->m_bRenderAfterCharacter = true;

    if (!IsCurrentMap())
        return false;
    if (IsNewMap73_74())
    {
        if (empireGuardian4_.CreateObject(pObject))
        {
            switch (pObject->Type)
            {
            case 129:
            case 79:
            case 83:
            case 82:
            case 85:
            case 86:
            case 130:
            case 131:
            case 158:
                pObject->HiddenMesh = -2;
                return true;
            default:
                break;
            }
        }
        return true;
    }

    switch (pObject->Type)
    {
    case 103: {
        CreateOperate(pObject);
    }
    break;
    }

    if (pObject->Type == 15 || pObject->Type == 25 || pObject->Type == 27 || pObject->Type == 45 ||
        pObject->Type == 46 || pObject->Type == 47 || pObject->Type == 48 || pObject->Type == 53 ||
        pObject->Type == 98 || pObject->Type == 107 || pObject->Type == 115 ||
        pObject->Type == 122 || pObject->Type == 130)
    {
        pObject->CollisionRange = -300;
    }
    return true;
}

bool GMNewTown::MoveObject(OBJECT *pObject)
{
    if (!IsCurrentMap())
        return false;

    if (IsNewMap73_74())
        return empireGuardian4_.MoveObject(pObject);

    float Luminosity;
    vec3_t Light;

    switch (pObject->Type)
    {
    case 0:
        Luminosity = (float)(WorldRandom() % 4 + 3) * 0.1f;
        Vector(Luminosity, Luminosity * 0.6f, Luminosity * 0.2f, Light);
        AddTerrainLight(pObject->Position[0], pObject->Position[1], Light, 3, PrimaryTerrainLight);
        pObject->HiddenMesh = -2;
        break;
    case 2: {
        pObject->BlendMeshTexCoordV += (0.015f) * FPS_ANIMATION_FACTOR;
    }
    break;
    case 53: {
        pObject->BlendMeshTexCoordV += (0.015f) * FPS_ANIMATION_FACTOR;
    }
    break;
    case 54:
        pObject->HiddenMesh = -2;
        break;
    case 55: {
        pObject->BlendMeshTexCoordV += (0.015f) * FPS_ANIMATION_FACTOR;
    }
    break;
    case 56:
        pObject->BlendMesh = 0;
        pObject->BlendMeshLight = sinf(WorldTime * 0.003f) * 0.3f + 0.5f;
        pObject->Velocity = 0.05f;
        break;
    case 60:
        pObject->HiddenMesh = -2;
        break;
    case 61:
        Luminosity = (float)(WorldRandom() % 4 + 3) * 0.1f;
        Vector(Luminosity * 0.2f, Luminosity * 0.6f, Luminosity, Light);
        AddTerrainLight(pObject->Position[0], pObject->Position[1], Light, 3, PrimaryTerrainLight);
        pObject->HiddenMesh = -2;
        break;
    case 58:
        pObject->HiddenMesh = -2;
        break;
    case 59:
        pObject->HiddenMesh = -2;
        break;
    case 89: {
        pObject->BlendMeshTexCoordV += (0.005f) * FPS_ANIMATION_FACTOR;
    }
    break;
    case 62:
        pObject->HiddenMesh = -2;
        {
            int iEagleIndex = 1;
            OBJECT *pBoid = &Boids[iEagleIndex];
            if (!pBoid->Live)
            {
                pBoid->Live = true;
                pBoid->Velocity = 0.f;
                pBoid->LightEnable = true;
                pBoid->LifeTime = 0;
                pBoid->SubType = 1;
                Vector(0.5f, 0.5f, 0.5f, pBoid->Light);
                pBoid->Alpha = 0.f;
                pBoid->AlphaTarget = 1.f;
                pBoid->Gravity = 10.0f * pObject->Scale;
                pBoid->AlphaEnable = true;
                pBoid->Scale = 0.5f;

                if (gMapManager.ContextMap() == WD_74NEW_CHARACTER_SCENE)
                    pBoid->ShadowScale = 0.f;
                else if (pObject->Position[2] > 100)
                    pBoid->ShadowScale = 15.f;
                else
                    pBoid->ShadowScale = 0.f;
                pBoid->HiddenMesh = -1;
                pBoid->BlendMesh = -1;
                pBoid->Timer = (float)(WorldRandom() % 314) * 0.01f;
                pBoid->Type = MODEL_EAGLE;
                Vector(0.f, 0.f, 0.f, pBoid->Angle);
                VectorCopy(pObject->Position, pBoid->Position);
            }
        }
        break;
    }
    return true;
}

CHARACTER *GMNewTown::CreateMonster(int iType, int PosX, int PosY, int Key)
{
    CHARACTER *pCharacter = NULL;

    switch (iType)
    {
    case MONSTER_SILVIA:
        OpenNpc(MODEL_ELBELAND_SILVIA);
        pCharacter = CreateCharacter(Key, MODEL_ELBELAND_SILVIA, PosX, PosY);
        pCharacter->Object.Scale = 1.0f;
        pCharacter->Object.m_fEdgeScale = 1.2f;
        break;
    case MONSTER_RHEA:
        OpenNpc(MODEL_ELBELAND_RHEA);
        pCharacter = CreateCharacter(Key, MODEL_ELBELAND_RHEA, PosX, PosY);
        pCharacter->Object.Scale = 1.0f;
        pCharacter->Object.m_fEdgeScale = 1.1f;
        Models[MODEL_ELBELAND_RHEA].Actions[0].PlaySpeed = 0.2f;
        Models[MODEL_ELBELAND_RHEA].Actions[1].PlaySpeed = 0.4f;
        break;
    case MONSTER_MARCE:
        OpenNpc(MODEL_ELBELAND_MARCE);
        pCharacter = CreateCharacter(Key, MODEL_ELBELAND_MARCE, PosX, PosY);
        pCharacter->Object.Scale = 1.05f;
        pCharacter->Object.m_fEdgeScale = 1.2f;
        break;
    case MONSTER_STRANGE_RABBIT:
        OpenMonsterModel(MONSTER_MODEL_RABBIT);
        pCharacter = CreateCharacter(Key, MODEL_RABBIT, PosX, PosY);
        pCharacter->Object.Scale = 1.0f * 0.95f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        break;
    case MONSTER_POLLUTED_BUTTERFLY:
        OpenMonsterModel(MONSTER_MODEL_BUTTERFLY);
        pCharacter = CreateCharacter(Key, MODEL_BUTTERFLY, PosX, PosY);
        pCharacter->Object.Scale = 0.8f * 0.95f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        break;
    case MONSTER_HIDEOUS_RABBIT:
        OpenMonsterModel(MONSTER_MODEL_HIDEOUS_RABBIT);
        pCharacter = CreateCharacter(Key, MODEL_HIDEOUS_RABBIT, PosX, PosY);
        pCharacter->Object.Scale = 1.0f * 0.95f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        break;
    case MONSTER_WEREWOLF2:
        OpenMonsterModel(MONSTER_MODEL_WEREWOLF2);
        pCharacter = CreateCharacter(Key, MODEL_WEREWOLF2, PosX, PosY);
        pCharacter->Object.Scale = 0.8f * 1.1f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        break;
    case MONSTER_CURSED_LICH:
        OpenMonsterModel(MONSTER_MODEL_CURSED_LICH);
        pCharacter = CreateCharacter(Key, MODEL_CURSED_LICH, PosX, PosY);
        pCharacter->Object.Scale = 1.0f * 0.95f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        break;
    case MONSTER_TOTEM_GOLEM:
        OpenMonsterModel(MONSTER_MODEL_TOTEM_GOLEM);
        pCharacter = CreateCharacter(Key, MODEL_TOTEM_GOLEM, PosX, PosY);
        pCharacter->Object.Scale = 0.17f * 0.95f;
        pCharacter->Object.ShadowScale = 0.01f;
        pCharacter->Object.m_fEdgeScale = 1.05f;
        pCharacter->Object.LifeTime = 100;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        break;
    case MONSTER_GRIZZLY:
        OpenMonsterModel(MONSTER_MODEL_GRIZZLY);
        pCharacter = CreateCharacter(Key, MODEL_GRIZZLY, PosX, PosY);
        pCharacter->Object.Scale = 1.2f * 0.95f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        break;
    case MONSTER_CAPTAIN_GRIZZLY:
        OpenMonsterModel(MONSTER_MODEL_CAPTAIN_GRIZZLY);
        pCharacter = CreateCharacter(Key, MODEL_CAPTAIN_GRIZZLY, PosX, PosY);
        pCharacter->Object.Scale = 1.3f * 0.95f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        break;
    }

    return pCharacter;
}

bool GMNewTown::CharacterSceneCheckMouse(OBJECT *pObj)
{
    m_bCharacterSceneCheckMouse = false;
    if (CheckMouseIn(480, 90, 30, 20) == true)
    {
        m_bCharacterSceneCheckMouse = true;
        SetAction(pObj, 2);
        return true;
    }
    else if (CheckMouseIn(485, 110, 50, 50) == true)
    {
        m_bCharacterSceneCheckMouse = true;
        SetAction(pObj, 0);
        return true;
    }

    return false;
}

bool GMNewTown::IsCheckMouseIn() const noexcept
{
    return m_bCharacterSceneCheckMouse;
}

void GMNewTown::AdvanceMonsterState(CHARACTER &character, BMD &model)
{
    auto &object = character.Object;

    if (!IsCurrentMap() || object.Type != MODEL_TOTEM_GOLEM ||
        object.CurrentAction != MONSTER01_DIE)
        return;
    if (object.LifeTime == 100)
    {
        object.LifeTime = 90;
        object.m_bRenderShadow = false;
    }
}

void GMNewTown::InstallBehavior()
{
    if (gMapManager.ContextMap() != WD_51HOME_6TH_CHAR)
        return;
    LoadWaveFile(SOUND_ELBELAND_VILLAGEPROTECTION01,
                 L"Data\\Sound\\w52\\SE_Obj_villageprotection01.wav", 1);
    LoadWaveFile(SOUND_ELBELAND_WATERFALLSMALL01, L"Data\\Sound\\w52\\SE_Obj_waterfallsmall01.wav",
                 1);
    LoadWaveFile(SOUND_ELBELAND_WATERWAY01, L"Data\\Sound\\w52\\SE_Obj_waterway01.wav", 1);
    LoadWaveFile(SOUND_ELBELAND_ENTERDEVIAS01, L"Data\\Sound\\w52\\SE_Obj_enterdevias01.wav", 1);
    LoadWaveFile(SOUND_ELBELAND_WATERSMALL01, L"Data\\Sound\\w52\\SE_Obj_watersmall01.wav", 1);
    LoadWaveFile(SOUND_ELBELAND_RAVINE01, L"Data\\Sound\\w52\\SE_Amb_ravine01.wav", 1);
    LoadWaveFile(SOUND_ELBELAND_ENTERATLANCE01, L"Data\\Sound\\w52\\SE_Amb_enteratlance01.wav", 1);
    Models[MODEL_EAGLE].Actions[0].PlaySpeed = 0.5f;
    Models[MODEL_MAP_TORNADO].Actions[0].PlaySpeed = 0.1f;
}

MapObjectInteraction GMNewTown::ObjectInteraction(int type, CHARACTER &actor)
{
    using Action = MapObjectInteraction::Action;
    return gMapManager.ContextMap() == WD_51HOME_6TH_CHAR && type == 103
               ? MapObjectInteraction{Action::Sit, true}
               : MapObjectInteraction{};
}

GMNoria::GMNoria(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper)
{
}

bool GMNoria::CreateObject(OBJECT *o)
{
    const int Type = o->Type;
    switch (Type)
    {
    case MODEL_WARP: {
        vec3_t Position;
        Vector(o->Position[0], o->Position[1], o->Position[2] + 350.f, Position);
        CreateEffect(MODEL_WARP, Position, o->Angle, o->Light);

        Vector(o->Position[0], o->Position[1] + 4.0f, o->Position[2] + 350.f, Position);
        CreateEffect(MODEL_WARP2, Position, o->Angle, o->Light);

        Vector(o->Position[0], o->Position[1] + 8.0f, o->Position[2] + 350.f, Position);
        CreateEffect(MODEL_WARP, Position, o->Angle, o->Light);

        Vector(o->Position[0], o->Position[1] + 12.0f, o->Position[2] + 350.f, Position);
        CreateEffect(MODEL_WARP2, Position, o->Angle, o->Light);

        Vector(o->Position[0], o->Position[1] + 20.0f, o->Position[2] + 350.f, Position);
        CreateEffect(MODEL_WARP3, Position, o->Angle, o->Light);
    }
    break;
    case 8:
        CreateOperate(o);
        break;
    case 1:
        o->BlendMesh = 1;
        break;
    case 9:
        o->BlendMesh = 3;
        break;
    case 38:
        CreateOperate(o);
        o->HiddenMesh = -2;
        break;
    case 17:
    case 37:
        o->BlendMesh = 0;
        break;
    case 19:
        o->BlendMesh = 0;
        break;
    case 18:
        o->BlendMesh = 2;
        break;
    }
    return true;
}

bool GMNoria::MoveObject(OBJECT *o)
{
    vec3_t p{}, Light{};
    float Luminosity;
    switch (o->Type)
    {
    case MODEL_WARP:
        vec3_t Position;
        Vector(o->Position[0], o->Position[1] - 50.f, o->Position[2] + 350.f, Position);
        Vector(0.5f, 0.5f, 0.5f, Light);
        CreateParticleFpsChecked(BITMAP_SPARK + 1, Position, o->Angle, Light, 9, 1.4f);
        break;
    case 18:
        o->BlendMeshTexCoordV = (int)WorldTime % 1000 * 0.001f;
        break;
    case 39:
        o->BlendMesh = 1;
        break;
    case 41:
        o->BlendMesh = 0;
        o->BlendMeshTexCoordV = (int)WorldTime % 2000 * 0.0005f;
        break;
    case 42:
        Models[o->Type].StreamMesh = 0;
        o->BlendMeshTexCoordU = -(float)((int)WorldTime % 500) * 0.002f;
        break;
    case 43:
        Models[o->Type].StreamMesh = 0;
        o->BlendMeshTexCoordU = (float)((int)WorldTime % 500) * 0.002f;
        break;
    }
    return true;
}

void GMNoria::InstallBehavior()
{
    vec3_t Pos, Ang;
    Vector(0.f, 0.f, 10.f, Ang);
    Vector(0.f, 0.f, 0.f, Pos);
    Pos[0] = 223 * TERRAIN_SCALE;
    Pos[1] = 30 * TERRAIN_SCALE;
    SessionLegacyCalls::CreateObject(MODEL_WARP, Pos, Ang);
}

MapObjectInteraction GMNoria::ObjectInteraction(int type, CHARACTER &actor)
{
    using Action = MapObjectInteraction::Action;
    if (type == 8)
        return {Action::Sit, false};
    if (type == 38)
        return {Action::Heal, true};
    return {};
}

CGMSantaTownPtr CGMSantaTown::Make(SessionKeeper &keeper)
{
    CGMSantaTownPtr santatown(new CGMSantaTown(keeper));
    santatown->Init();
    return santatown;
}

CGMSantaTown::CGMSantaTown(SessionKeeper &keeper)
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject())
{
}

CGMSantaTown::~CGMSantaTown()
{
    Destroy();
}

void CGMSantaTown::Init()
{
}

void CGMSantaTown::Destroy()
{
}

bool CGMSantaTown::CreateObject(OBJECT *o)
{
    switch (o->Type)
    {
    case 21:
    case 18:
    case 19:
    case 12:
    case 13:
    case 25:
        o->CollisionRange = -300;
        return true;
    }

    return false;
}

CHARACTER *CGMSantaTown::CreateMonster(int iType, int PosX, int PosY, int Key)
{
    CHARACTER *pCharacter = NULL;

    switch (iType)
    {
    case 465:
        OpenNpc(MODEL_XMAS2008_SANTA_NPC);
        pCharacter = CreateCharacter(Key, MODEL_XMAS2008_SANTA_NPC, PosX, PosY);
        pCharacter->Object.Scale = 1.7f;
        break;
    case 467:
        OpenNpc(MODEL_XMAS2008_SNOWMAN_NPC);
        pCharacter = CreateCharacter(Key, MODEL_XMAS2008_SNOWMAN_NPC, PosX, PosY);
        pCharacter->Object.Scale = 1.3f;
        break;
    }

    return pCharacter;
}

bool CGMSantaTown::MoveObject(OBJECT *o)
{
    if (IsSantaTown() == false)
        return false;

    switch (o->Type)
    {
    case 16: {
        o->Velocity = 0.06f;
        return true;
    }
    break;
    case 26:
    case 27:
    case 28: {
        o->HiddenMesh = -2;
        return true;
    }
    break;
    }

    return false;
}

bool CGMSantaTown::IsSantaTown()
{
    if (gMapManager.ContextMap() == WD_62SANTA_TOWN)
    {
        return true;
    }

    return false;
}

GMStadium::GMStadium(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper)
{
}

bool GMStadium::MoveObject(OBJECT *o)
{
    vec3_t p{}, Light{};
    float Luminosity;
    switch (o->Type)
    {
    case 21:
        o->BlendMesh = 3;
        o->BlendMeshTexCoordV = -(int)WorldTime % 1000 * 0.001f;
        break;
    case 38:
        o->HiddenMesh = -2;
        break;
    }
    return true;
}

GMTarkan::GMTarkan(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper)
{
}

bool GMTarkan::CreateObject(OBJECT *o)
{
    const int Type = o->Type;
    switch (Type)
    {
    case 78:
        CreateOperate(o);
        // CreateJoint(BITMAP_JOINT_ENERGY,o->Position,o->Position,o->Angle,2,o,30.f);
        // CreateJoint(BITMAP_JOINT_ENERGY,o->Position,o->Position,o->Angle,3,o,30.f);
        break;
    }
    return true;
}

bool GMTarkan::MoveObject(OBJECT *o)
{
    vec3_t p{}, Light{};
    float Luminosity;
    switch (o->Type)
    {
    case 2:
        o->BlendMesh = 0;
        o->BlendMeshTexCoordU = -(int)WorldTime % 1000 * 0.001f;
        break;

    case 4: {
        float sine = (float)sinf(WorldTime * 0.002f) * 0.35f + 0.65f;
        o->BlendMesh = 0;
        o->BlendMeshLight = sine;
        o->BlendMeshTexCoordV = -(int)WorldTime % 10000 * 0.0001f;

        Luminosity = sine;
        Vector(Luminosity, Luminosity, Luminosity, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
    }
    break;

    case 7: {
        float sine = (float)sinf((WorldTime + (o->Angle[2] * 100)) * 0.002f) * 0.35f + 0.65f;

        o->BlendMesh = 0;
        o->BlendMeshLight = sine;

        Luminosity = sine;
        Vector(Luminosity, Luminosity * 0.6f, Luminosity * 0.2f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 3, PrimaryTerrainLight);
    }
    break;

    case 11:
        o->BlendMeshTexCoordV = -(int)WorldTime % 10000 * 0.0002f;
        break;

    case 12:
        o->BlendMeshTexCoordU = -(int)WorldTime % 50000 * 0.00005f;
        o->BlendMeshTexCoordV = -(int)WorldTime % 50000 * 0.00005f;
        break;

    case 13:
        o->BlendMeshTexCoordV = -(int)WorldTime % 10000 * 0.0002f;
        break;

    case 61:
        o->BlendMesh = 1;
        o->BlendMeshTexCoordV = -(int)WorldTime % 1000 * 0.001f;

        Luminosity = (float)sinf(WorldTime * 0.002f) * 0.35f + 0.65f;
        Vector(Luminosity, Luminosity * 0.6f, Luminosity * 0.2f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
        break;

    case 63:
    case 64:
        o->HiddenMesh = -2;
        break;

    case 65:
    case 66:
        o->BlendMesh = 1;
        o->BlendMeshTexCoordV = -(int)WorldTime % 1000 * 0.001f;

        Luminosity = (float)sinf(WorldTime * 0.002f) * 0.35f + 0.65f;
        Vector(Luminosity, Luminosity * 0.6f, Luminosity * 0.2f, Light);
        AddTerrainLight(o->Position[0], o->Position[1], Light, 2, PrimaryTerrainLight);
        break;

    case 72:
        o->BlendMesh = 0;
        o->BlendMeshTexCoordV = -(int)WorldTime % 10000 * 0.0002f;
        break;

    case 73:
        o->BlendMeshTexCoordV = -(int)WorldTime % 10000 * 0.0002f;
        break;

    case 75:
        o->BlendMeshTexCoordV = -(int)WorldTime % 10000 * 0.0002f;
        break;

    case 79:
        o->BlendMeshTexCoordV = -(int)WorldTime % 10000 * 0.0002f;
        break;

    case 82:
        o->BlendMesh = 0;
        Vector(1.0f, 1.0f, 1.0f, o->Light);
        break;
    }
    return true;
}

void GMTarkan::InstallBehavior()
{
    for (const int slot : {11, 12, 13, 73, 75, 79})
        Models[slot].StreamMesh = 0;
}

MapObjectInteraction GMTarkan::ObjectInteraction(int type, CHARACTER &actor)
{
    using Action = MapObjectInteraction::Action;
    return type == 78 ? MapObjectInteraction{Action::Sit, false} : MapObjectInteraction{};
}

GMUnitedMarketPlacePtr GMUnitedMarketPlace::Make(SessionKeeper &keeper)
{
    GMUnitedMarketPlacePtr empire(new GMUnitedMarketPlace(keeper));
    empire->Init();
    return empire;
}

GMUnitedMarketPlace::GMUnitedMarketPlace(SessionKeeper &keeper)
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject())
{
}

GMUnitedMarketPlace::~GMUnitedMarketPlace()
{
    Destroy();
}

void GMUnitedMarketPlace::Init()
{
}

void GMUnitedMarketPlace::Destroy()
{
}

bool GMUnitedMarketPlace::CreateObject(OBJECT *o)
{
    if (o->Type == 8 || o->Type == 30)
        o->m_bRenderAfterCharacter = true;

    switch (o->Type)
    {
    case 67: // 기대기 박스
    {
        CreateOperate(o);
        Vector(100.f, 100.f, 160.f, o->BoundingBoxMax);
        o->HiddenMesh = -2;
    }
    break;
    }

    return false;
}

bool GMUnitedMarketPlace::MoveObject(OBJECT *o)
{
    if (IsUnitedMarketPlace() == false)
        return false;

    Alpha(o, FPS_ANIMATION_FACTOR);
    if (o->Alpha < 0.01f)
        return false;

    BMD *b = &Models[o->Type];

    switch (o->Type)
    {
    case 8: // chofountain01 폭포물 표면의 Animation 속도 처리 약간더 빠르게.
    {
        o->Velocity = 0.2f;
    }
        return true;
    case 30: // 가로등
    {
        VectorCopy(o->Position, b->BodyOrigin);
        b->BodyScale = o->Scale;
        b->Animation(BoneTransform, o->AnimationFrame, o->PriorAnimationFrame, o->PriorAction,
                     o->Angle, o->HeadAngle, false, true);

        vec3_t vLightPosition, vRelativePos;
        Vector(0.0f, 0.0f, 0.0f, vRelativePos);
        b->TransformPosition(BoneTransform[1], vRelativePos, vLightPosition, false);

        float fLumi = (sinf(WorldTime * 0.002f) + 1.0f) * 0.2f + 0.6f;
        vec3_t vLightFire;
        Vector(fLumi * 0.7f, fLumi * 0.5f, fLumi * 0.2f, vLightFire);

        AddTerrainLight(vLightPosition[0], vLightPosition[1], vLightFire, 3, PrimaryTerrainLight);
    }
        return true;
    case 35: // 벽가로등
    {
        VectorCopy(o->Position, b->BodyOrigin);
        b->BodyScale = o->Scale;
        b->Animation(BoneTransform, o->AnimationFrame, o->PriorAnimationFrame, o->PriorAction,
                     o->Angle, o->HeadAngle, false, true);

        vec3_t vLightPosition, vRelativePos;
        Vector(0.0f, 0.0f, 0.0f, vRelativePos);
        b->TransformPosition(BoneTransform[2], vRelativePos, vLightPosition, false);

        float fLumi = (sinf(WorldTime * 0.039f) + 1.0f) * 0.2f + 0.6f;
        vec3_t vLightFire;
        Vector(fLumi * 0.7f, fLumi * 0.5f, fLumi * 0.2f, vLightFire);

        AddTerrainLight(vLightPosition[0], vLightPosition[1], vLightFire, 1, PrimaryTerrainLight);
    }
        return true;
    case 54:
    case 55:
    case 56:
    case 57:
    case 58: {
        o->HiddenMesh = -2;
    }
        return true;
    }

    return false;
}

bool GMUnitedMarketPlace::SetCurrentActionMonster(CHARACTER *c, OBJECT *o)
{
    if (IsUnitedMarketPlace() == false)
    {
        return false;
    }

    switch (c->MonsterIndex)
    {
    case MONSTER_LUCAS: {
        // 			switch(c->MonsterSkill)
        // 			{
        // 			default:
        // 				{
        // 					SetAction(o, MONSTER01_ATTACK1);
        // 					c->MonsterSkill = -1;
        // 				}
        // 				break;
        // 			}
    }
        return true;
    }

    return false;
}

bool GMUnitedMarketPlace::IsUnitedMarketPlace() const
{
    if (gMapManager.ContextMap() == WD_79UNITEDMARKETPLACE)
    {
        return true;
    }
    return false;
}

MapObjectInteraction GMUnitedMarketPlace::ObjectInteraction(int type, CHARACTER &actor)
{
    using Action = MapObjectInteraction::Action;
    return type == 67 ? MapObjectInteraction{Action::Pose, true} : MapObjectInteraction{};
}

GMUnknownWorld::GMUnknownWorld(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper)
{
}

bool GMUnknownWorld::MoveObject(OBJECT *o)
{
    vec3_t p{}, Light{};
    float Luminosity;
    switch (o->Type)
    {
    case 2:
        o->BlendMesh = 0;
        break;
    case 3:
        o->BlendMesh = 0;
        o->BlendMeshLight = (float)(WorldRandom() % 4 + 6) * 0.1f;
        break;
    }
    return true;
}

GMKanturu1stPtr GMKanturu1st::Make(SessionKeeper &keeper)
{
    return GMKanturu1stPtr(new GMKanturu1st(keeper));
}

GMKanturu1st::GMKanturu1st(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject()), g_Direction(keeper.DirectionObject()),
      gmArea_(keeper.GameplayForConstruction().TheMapProcess().GmArea())
{
}

bool GMKanturu1st::IsKanturu1st()
{
    return (gMapManager.ContextMap() == WD_37KANTURU_1ST) ? true : false;
}

bool GMKanturu1st::CreateKanturu1stObject(OBJECT *pObject)
{
    if (!IsKanturu1st())
        return false;

    return true;
}

bool GMKanturu1st::MoveKanturu1stObject(OBJECT *pObject)
{
    if (!(IsKanturu1st() || gmArea_.IsGmArea()))
        return false;

    vec3_t Light;
    float Luminosity;

    switch (pObject->Type)
    {
    case 59:
    case 62:
    case 81:
    case 82:
    case 83:
    case 107:
    case 108:
        pObject->HiddenMesh = -2;
        break;
    case 44:
        pObject->Velocity = 0.02f;
        break;
    case 46:
        pObject->Velocity = 0.01f;
        pObject->BlendMeshLight = (float)sinf(WorldTime * 0.0015f) * 0.8f + 1.0f;
        PlayBuffer(SOUND_KANTURU_1ST_BG_WHEEL);
        break;
    case 60:
        Luminosity = (float)(WorldRandom() % 4 + 3) * 0.1f;
        Vector(Luminosity * 0.9f, Luminosity * 0.2f, Luminosity * 0.1f, Light);
        AddTerrainLight(pObject->Position[0], pObject->Position[1], Light, 3, PrimaryTerrainLight);
        pObject->HiddenMesh = -2;
        break;
    case 61:
        Luminosity = (float)(WorldRandom() % 4 + 3) * 0.1f;
        Vector(Luminosity, Luminosity * 0.6f, Luminosity * 0.2f, Light);
        AddTerrainLight(pObject->Position[0], pObject->Position[1], Light, 3, PrimaryTerrainLight);
        pObject->HiddenMesh = -2;
        break;
    case 70:
        pObject->Velocity = 0.04f;
        Luminosity = (float)sinf(WorldTime * 0.002f) * 0.45f + 0.55f;
        Vector(Luminosity * 1.4f, Luminosity * 0.7f, Luminosity * 0.4f, Light);
        AddTerrainLight(pObject->Position[0], pObject->Position[1], Light, 4, PrimaryTerrainLight);
        break;
    case 76:
        pObject->Alpha = 0.5f;
        break;
    case 77:
        pObject->BlendMeshTexCoordV = -(int)WorldTime % 10000 * 0.0002f;
        PlayBuffer(SOUND_KANTURU_1ST_BG_WATERFALL);
        break;
    case 90:
        pObject->Velocity = 0.04f;
        break;
    case 92:
        PlayBuffer(SOUND_KANTURU_1ST_BG_ELEC);
        break;
    case 96:
        pObject->Alpha = 0.5f;
        break;
    case 97:
        pObject->HiddenMesh = -2;
        pObject->Timer += 0.1f * FPS_ANIMATION_FACTOR;
        if (pObject->Timer > 10.f)
            pObject->Timer = 0.f;
        if (pObject->Timer > 5.f)
            CreateParticleFpsChecked(BITMAP_BUBBLE, pObject->Position, pObject->Angle,
                                     pObject->Light, 5);
        break;
    case 98:
        PlayBuffer(SOUND_KANTURU_1ST_BG_PLANT);
        break;
    case 102:
        pObject->BlendMeshLight = (float)sinf(WorldTime * 0.0010f) + 1.0f;
        break;
    }

    PlayBuffer(SOUND_KANTURU_1ST_BG_GLOBAL);

    return true;
}

CHARACTER *GMKanturu1st::CreateKanturu1stMonster(int iType, int PosX, int PosY, int Key)
{
    CHARACTER *pCharacter = NULL;

    switch (iType)
    {
    case MONSTER_BERSERK:
    case MONSTER_BERSERKER: {
        OpenMonsterModel(MONSTER_MODEL_BERSERK);
        pCharacter = CreateCharacter(Key, MODEL_BERSERK, PosX, PosY);
        pCharacter->Object.Scale = 0.95f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"BERSERK_MOUTH", CharacterSocket::BERSERK_MOUTH);
    }
    break;
    case MONSTER_GIGANTIS2:
    case MONSTER_GIGANTIS: {
        OpenMonsterModel(MONSTER_MODEL_GIGANTIS);
        pCharacter = CreateCharacter(Key, MODEL_GIGANTIS, PosX, PosY);
        pCharacter->Object.Scale = 1.2f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
    }
    break;
    case MONSTER_GENOCIDER: {
        OpenMonsterModel(MONSTER_MODEL_GENOCIDER);
        pCharacter = CreateCharacter(Key, MODEL_GENOCIDER, PosX, PosY);
        pCharacter->Object.Scale = 1.2f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"GENO_WP", CharacterSocket::GENO_WP);
    }
    break;
    case MONSTER_SPLINTER_WOLF: {
        OpenMonsterModel(MONSTER_MODEL_SPLINTER_WOLF);
        pCharacter = CreateCharacter(Key, MODEL_SPLINTER_WOLF, PosX, PosY);
        pCharacter->Object.Scale = 0.8f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        RegisterBone(pCharacter, L"SPL_WOLF_EYE_26", CharacterSocket::SPL_WOLF_EYE_26);
        RegisterBone(pCharacter, L"SPL_WOLF_EYE_25", CharacterSocket::SPL_WOLF_EYE_25);

        OBJECT *o = &pCharacter->Object;
        BMD *b = &Models[o->Type];

        MoveEye(o, b, 16, 17);
        vec3_t vColor = {1.5f, 0.01f, 0.0f};
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 24, o, 10.f, -1, 0, 0,
                    -1, vColor);
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 25, o, 10.f, -1, 0, 0,
                    -1, vColor);
    }
    break;

    case MONSTER_IRON_RIDER: {
        OpenMonsterModel(MONSTER_MODEL_IRON_RIDER);
        pCharacter = CreateCharacter(Key, MODEL_IRON_RIDER, PosX, PosY);
        pCharacter->Object.Scale = 1.3f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        RegisterBone(pCharacter, L"IRON_RIDER_BIP01", CharacterSocket::IRON_RIDER_BIP01);
        RegisterBone(pCharacter, L"IRON_RIDER_BOW_6", CharacterSocket::IRON_RIDER_BOW_6);
        RegisterBone(pCharacter, L"IRON_RIDER_BOW_15", CharacterSocket::IRON_RIDER_BOW_15);
        RegisterBone(pCharacter, L"IRON_RIDER_BOW_16", CharacterSocket::IRON_RIDER_BOW_16);
    }
    break;
    case MONSTER_BLADE_HUNTER: {
        OpenMonsterModel(MONSTER_MODEL_BLADE_HUNTER);
        pCharacter = CreateCharacter(Key, MODEL_BLADE_HUNTER, PosX, PosY);
        pCharacter->Object.Scale = 1.3f;
        pCharacter->Object.Gravity = 0.0f;
        pCharacter->Object.Distance = (float)(WorldRandom() % 20) / 10.0f;
        pCharacter->Object.Angle[0] = 0.0f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        RegisterBone(pCharacter, L"BLADE_L_HAND", CharacterSocket::BLADE_L_HAND);
        RegisterBone(pCharacter, L"BOX1", CharacterSocket::BOX1);
        RegisterBone(pCharacter, L"BOX2", CharacterSocket::BOX2);
    }
    break;
    case MONSTER_SATYROS: {
        OpenMonsterModel(MONSTER_MODEL_SATYROS);
        pCharacter = CreateCharacter(Key, MODEL_SATYROS, PosX, PosY);
        pCharacter->Object.Scale = 1.3f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
    }
    break;
    case MONSTER_KENTAUROS: {
        OpenMonsterModel(MONSTER_MODEL_KENTAUROS);
        pCharacter = CreateCharacter(Key, MODEL_KENTAUROS, PosX, PosY);
        pCharacter->Object.Scale = 1.1f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        RegisterBone(pCharacter, L"KENTAUROS_BIP_23", CharacterSocket::KENTAUROS_BIP_23);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_24", CharacterSocket::KENTAUROS_BIP_24);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_25", CharacterSocket::KENTAUROS_BIP_25);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_26", CharacterSocket::KENTAUROS_BIP_26);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_18", CharacterSocket::KENTAUROS_BIP_18);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_19", CharacterSocket::KENTAUROS_BIP_19);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_20", CharacterSocket::KENTAUROS_BIP_20);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_21", CharacterSocket::KENTAUROS_BIP_21);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_TAIL", CharacterSocket::KENTAUROS_BIP_TAIL);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_TAIL_1", CharacterSocket::KENTAUROS_BIP_TAIL_1);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_TAIL_2", CharacterSocket::KENTAUROS_BIP_TAIL_2);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_SPAIN_1", CharacterSocket::KENTAUROS_BIP_SPAIN_1);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_SPAIN_2", CharacterSocket::KENTAUROS_BIP_SPAIN_2);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_SPAIN_3", CharacterSocket::KENTAUROS_BIP_SPAIN_3);
    }
    break;
    case MONSTER_BERSERKER_WARRIOR: {
        OpenMonsterModel(MONSTER_MODEL_BERSERKER_WARRIOR);
        pCharacter = CreateCharacter(Key, MODEL_BERSERKER_WARRIOR, PosX, PosY);
        //pCharacter->Object.Scale = 0.95f;
        pCharacter->Object.Scale = 1.15f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        RegisterBone(pCharacter, L"BERSERK_MOUTH", CharacterSocket::BERSERK_MOUTH);
    }
    break;
    case MONSTER_KENTAUROS_WARRIOR: {
        OpenMonsterModel(MONSTER_MODEL_KENTAUROS_WARRIOR);
        pCharacter = CreateCharacter(Key, MODEL_KENTAUROS_WARRIOR, PosX, PosY);
        //pCharacter->Object.Scale = 1.1f;
        pCharacter->Object.Scale = 1.3f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        RegisterBone(pCharacter, L"KENTAUROS_BIP_23", CharacterSocket::KENTAUROS_BIP_23);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_24", CharacterSocket::KENTAUROS_BIP_24);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_25", CharacterSocket::KENTAUROS_BIP_25);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_26", CharacterSocket::KENTAUROS_BIP_26);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_18", CharacterSocket::KENTAUROS_BIP_18);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_19", CharacterSocket::KENTAUROS_BIP_19);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_20", CharacterSocket::KENTAUROS_BIP_20);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_21", CharacterSocket::KENTAUROS_BIP_21);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_TAIL", CharacterSocket::KENTAUROS_BIP_TAIL);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_TAIL_1", CharacterSocket::KENTAUROS_BIP_TAIL_1);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_TAIL_2", CharacterSocket::KENTAUROS_BIP_TAIL_2);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_SPAIN_1", CharacterSocket::KENTAUROS_BIP_SPAIN_1);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_SPAIN_2", CharacterSocket::KENTAUROS_BIP_SPAIN_2);
        RegisterBone(pCharacter, L"KENTAUROS_BIP_SPAIN_3", CharacterSocket::KENTAUROS_BIP_SPAIN_3);
    }
    break;
    case MONSTER_GIGANTIS_WARRIOR: {
        OpenMonsterModel(MONSTER_MODEL_GIGANTIS_WARRIOR);
        pCharacter = CreateCharacter(Key, MODEL_GIGANTIS_WARRIOR, PosX, PosY);
        //pCharacter->Object.Scale = 1.2f;
        pCharacter->Object.Scale = 1.5f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
    }
    break;
    case MONSTER_GENOCIDER_WARRIOR: {
        OpenMonsterModel(MONSTER_MODEL_SOCCERBALL);
        pCharacter = CreateCharacter(Key, MODEL_SOCCERBALL, PosX, PosY);
        //pCharacter->Object.Scale = 1.2f;
        pCharacter->Object.Scale = 1.35f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"GENO_WP", CharacterSocket::GENO_WP);
    }
    break;
    }

    return pCharacter;
}

bool GMKanturu1st::SetCurrentActionKanturu1stMonster(CHARACTER *c, OBJECT *o)
{
    if (!IsKanturu1st())
        return false;

    switch (c->MonsterIndex)
    {
    case MONSTER_BERSERKER:
    case MONSTER_GIGANTIS:
    case MONSTER_GENOCIDER:
    case MONSTER_SPLINTER_WOLF:
    case MONSTER_IRON_RIDER: {
        if (rand_fps_check(2))
            SetAction(o, MONSTER01_ATTACK1);
        else
            SetAction(o, MONSTER01_ATTACK2);

        return true;
    }
    break;
    case MONSTER_KENTAUROS: {
        if (c->MonsterSkill == ATMON_SKILL_NUM9)
        {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        else
            SetAction(o, MONSTER01_ATTACK1);
    }
    break;
    case MONSTER_BERSERKER_WARRIOR: {
        if (c->MonsterSkill == ATMON_SKILL_EX_BERSERKERWARRIOR_ATTACKSKILL)
        {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        else
            SetAction(o, MONSTER01_ATTACK1);
    }
    break;
    case MONSTER_KENTAUROS_WARRIOR: {
        if (c->MonsterSkill == ATMON_SKILL_EX_KENTAURUSWARRIOR_ATTACKSKILL)
        {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        else
            SetAction(o, MONSTER01_ATTACK1);
    }
    break;
    case MONSTER_GIGANTIS_WARRIOR: {
        if (rand_fps_check(2))
            SetAction(o, MONSTER01_ATTACK1);
        else
            SetAction(o, MONSTER01_ATTACK2);

        return true;
    }
    break;
    case MONSTER_GENOCIDER_WARRIOR: {
        if (c->MonsterSkill == ATMON_SKILL_EX_GENOSIDEWARRIOR_ATTACKSKILL)
        {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        else
            SetAction(o, MONSTER01_ATTACK1);
    }
    break;
    }

    return false;
}

bool GMKanturu1st::CreateObject(OBJECT *object)
{
    if (object->Type == 76 || object->Type == 77 || object->Type == 91 || object->Type == 92 ||
        object->Type == 95 || object->Type == 105)
        object->m_bRenderAfterCharacter = true;

    return CreateKanturu1stObject(object);
}

bool GMKanturu1st::MoveObject(OBJECT *object)
{
    return MoveKanturu1stObject(object);
}

bool GMKanturu1st::SetCurrentActionMonster(CHARACTER *character, OBJECT *object)
{
    return SetCurrentActionKanturu1stMonster(character, object);
}

void GMKanturu1st::InstallBehavior()
{
    LoadWaveFile(SOUND_KANTURU_1ST_BG_WATERFALL, L"Data\\Sound\\w37\\kan_ruin_waterfall.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_BG_ELEC, L"Data\\Sound\\w37\\kan_ruin_elec.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_BG_WHEEL, L"Data\\Sound\\w37\\kan_ruin_wheel.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_BG_PLANT, L"Data\\Sound\\w37\\kan_ruin_plant.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_BG_GLOBAL, L"Data\\Sound\\w37\\kan_ruin_global.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_BER_MOVE1, L"Data\\Sound\\w37\\ber_idle-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_BER_MOVE2, L"Data\\Sound\\w37\\ber_idle-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_BER_ATTACK1, L"Data\\Sound\\w37\\ber_attack-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_BER_ATTACK2, L"Data\\Sound\\w37\\ber_attack-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_BER_DIE, L"Data\\Sound\\w37\\ber_death.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_GIGAN_MOVE1, L"Data\\Sound\\w37\\gigan_idle-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_GIGAN_ATTACK1, L"Data\\Sound\\w37\\gigan_attack-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_GIGAN_ATTACK2, L"Data\\Sound\\w37\\gigan_attack-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_GIGAN_DIE, L"Data\\Sound\\w37\\gigan_death.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_GENO_MOVE1, L"Data\\Sound\\w37\\geno_idle-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_GENO_MOVE2, L"Data\\Sound\\w37\\geno_idle-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_GENO_ATTACK1, L"Data\\Sound\\w37\\geno_attack-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_GENO_ATTACK2, L"Data\\Sound\\w37\\geno_attack-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_GENO_DIE, L"Data\\Sound\\w37\\geno_death.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_KENTA_MOVE1, L"Data\\Sound\\w37\\kenta_idle-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_KENTA_MOVE2, L"Data\\Sound\\w37\\kenta_idle-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_KENTA_ATTACK1, L"Data\\Sound\\w37\\kenta_attack-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_KENTA_ATTACK2, L"Data\\Sound\\w37\\kenta_skill-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_KENTA_DIE, L"Data\\Sound\\w37\\kenta_death.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_BLADE_MOVE1, L"Data\\Sound\\w37\\blade_idle-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_BLADE_MOVE2, L"Data\\Sound\\w37\\blade_idle-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_BLADE_ATTACK1, L"Data\\Sound\\w37\\blade_attack-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_BLADE_ATTACK2, L"Data\\Sound\\w37\\blade_attack-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_BLADE_DIE, L"Data\\Sound\\w37\\blade_death.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_SATI_MOVE1, L"Data\\Sound\\w37\\sati_idle-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_SATI_MOVE2, L"Data\\Sound\\w37\\sati_idle-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_SATI_ATTACK1, L"Data\\Sound\\w37\\sati_attack-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_SATI_ATTACK2, L"Data\\Sound\\w37\\sati_attack-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_SATI_DIE, L"Data\\Sound\\w37\\sati_death.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_SWOLF_MOVE1, L"Data\\Sound\\w37\\swolf_idle-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_SWOLF_MOVE2, L"Data\\Sound\\w37\\swolf_idle-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_SWOLF_ATTACK1, L"Data\\Sound\\w37\\swolf_attack-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_SWOLF_ATTACK2, L"Data\\Sound\\w37\\swolf_attack-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_SWOLF_DIE, L"Data\\Sound\\w37\\swolf_death.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_IR_MOVE1, L"Data\\Sound\\w37\\ir_idle-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_IR_MOVE2, L"Data\\Sound\\w37\\ir_idle-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_IR_ATTACK1, L"Data\\Sound\\w37\\ir_attack-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_IR_ATTACK2, L"Data\\Sound\\w37\\ir_attack-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_1ST_IR_DIE, L"Data\\Sound\\w37\\ir_death.wav", 1);
}

GMKanturu2ndPtr GMKanturu2nd::Make(SessionKeeper &keeper)
{
    return GMKanturu2ndPtr(new GMKanturu2nd(keeper));
}

GMKanturu2nd::GMKanturu2nd(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      trapCanon_(keeper), gMapManager(keeper.MapManagerObject()),
      g_Direction(keeper.DirectionObject())
{
}

bool GMKanturu2nd::Create_Kanturu2nd_Object(OBJECT *o)
{
    if (!Is_Kanturu2nd())
        return false;

    switch (o->Type)
    {
    case 3: {
        CreateOperate(o);
    }
    break;
    }

    return true;
}

CHARACTER *GMKanturu2nd::Create_Kanturu2nd_Monster(int iType, int PosX, int PosY, int Key)
{
    CHARACTER *pCharacter = NULL;

    switch (iType)
    {
    case MONSTER_PERSONA_DS7:
    case MONSTER_PERSONA: {
        OpenMonsterModel(MONSTER_MODEL_PERSONA);
        pCharacter = CreateCharacter(Key, MODEL_PERSONA, PosX, PosY);
        pCharacter->Object.Scale = 1.0f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"PRSona_A1", CharacterSocket::PRSona_A1);
        RegisterBone(pCharacter, L"PRSona_Tail", CharacterSocket::PRSona_Tail);
        RegisterBone(pCharacter, L"PRSona_Tail1", CharacterSocket::PRSona_Tail1);
    }
    break;
    case MONSTER_TWIN_TALE: {
        OpenMonsterModel(MONSTER_MODEL_TWIN_TAIL);
        pCharacter = CreateCharacter(Key, MODEL_TWIN_TAIL, PosX, PosY);
        pCharacter->Object.Scale = 1.3f;
        pCharacter->Object.Angle[0] = 0.0f;
        pCharacter->Object.Gravity = 0.0f;
        pCharacter->Object.Distance = (float)(WorldRandom() % 20) / 10.0f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"Twintail_Hair24", CharacterSocket::Twintail_Hair24);
        RegisterBone(pCharacter, L"Twintail_Hair32", CharacterSocket::Twintail_Hair32);
    }
    break;
    case MONSTER_DREADFEAR2:
    case MONSTER_DREADFEAR: {
        OpenMonsterModel(MONSTER_MODEL_DREADFEAR);
        pCharacter = CreateCharacter(Key, MODEL_DREADFEAR, PosX, PosY);
        pCharacter->Object.Scale = 1.3f;
        pCharacter->Object.Angle[0] = 0.0f;
        pCharacter->Object.Gravity = 0.0f;
        pCharacter->Object.Distance = (float)(WorldRandom() % 20) / 10.0f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"Dreadfear_Wing32", CharacterSocket::Dreadfear_Wing32);
        RegisterBone(pCharacter, L"Dreadfear_Wing34", CharacterSocket::Dreadfear_Wing34);
        RegisterBone(pCharacter, L"Dreadfear_Wing51", CharacterSocket::Dreadfear_Wing51);
        RegisterBone(pCharacter, L"Dreadfear_Wing53", CharacterSocket::Dreadfear_Wing53);
        RegisterBone(pCharacter, L"Dreadfear_Eye52", CharacterSocket::Dreadfear_Eye52);
        RegisterBone(pCharacter, L"Dreadfear_Eye54", CharacterSocket::Dreadfear_Eye54);
    }
    break;
    case MONSTER_GATEWAY_MACHINE: {
        OpenNpc(MODEL_KANTURU2ND_ENTER_NPC);
        pCharacter = CreateCharacter(Key, MODEL_KANTURU2ND_ENTER_NPC, PosX, PosY);
        pCharacter->Object.Scale = 4.76f;
        pCharacter->Object.Position[0] -= 20.0f;
        pCharacter->Object.Position[1] -= 200.0f;
        pCharacter->Object.m_bRenderShadow = false;
        SetAction(&pCharacter->Object, KANTURU2ND_NPC_ANI_STOP);

        g_pKanturu2ndEnterNpc->SetNpcObject(&pCharacter->Object);
        g_pKanturu2ndEnterNpc->SetNpcAnimation(false);

        RegisterBone(pCharacter, L"KANTURU2ND_ENTER_NPC_1",
                     CharacterSocket::KANTURU2ND_ENTER_NPC_1);
        RegisterBone(pCharacter, L"KANTURU2ND_ENTER_NPC_2",
                     CharacterSocket::KANTURU2ND_ENTER_NPC_2);
        RegisterBone(pCharacter, L"KANTURU2ND_ENTER_NPC_3",
                     CharacterSocket::KANTURU2ND_ENTER_NPC_3);
        RegisterBone(pCharacter, L"KANTURU2ND_ENTER_NPC_4",
                     CharacterSocket::KANTURU2ND_ENTER_NPC_4);
        RegisterBone(pCharacter, L"KANTURU2ND_ENTER_NPC_5",
                     CharacterSocket::KANTURU2ND_ENTER_NPC_5);
        RegisterBone(pCharacter, L"KANTURU2ND_ENTER_NPC_6",
                     CharacterSocket::KANTURU2ND_ENTER_NPC_6);
        RegisterBone(pCharacter, L"KANTURU2ND_ENTER_NPC_7",
                     CharacterSocket::KANTURU2ND_ENTER_NPC_7);
        RegisterBone(pCharacter, L"KANTURU2ND_ENTER_NPC_8",
                     CharacterSocket::KANTURU2ND_ENTER_NPC_8);
        RegisterBone(pCharacter, L"KANTURU2ND_ENTER_NPC_9",
                     CharacterSocket::KANTURU2ND_ENTER_NPC_9);
        RegisterBone(pCharacter, L"KANTURU2ND_ENTER_NPC_10",
                     CharacterSocket::KANTURU2ND_ENTER_NPC_10);
        RegisterBone(pCharacter, L"KANTURU2ND_ENTER_NPC_11",
                     CharacterSocket::KANTURU2ND_ENTER_NPC_11);
        RegisterBone(pCharacter, L"KANTURU2ND_ENTER_NPC_12",
                     CharacterSocket::KANTURU2ND_ENTER_NPC_12);
        RegisterBone(pCharacter, L"KANTURU2ND_ENTER_NPC_13",
                     CharacterSocket::KANTURU2ND_ENTER_NPC_13);
        RegisterBone(pCharacter, L"KANTURU2ND_ENTER_NPC_14",
                     CharacterSocket::KANTURU2ND_ENTER_NPC_14);
    }
    break;
    case MONSTER_CANON_TRAP: {
        pCharacter = trapCanon_.Create_TrapCanon(PosX, PosY, Key);
    }
    break;
    }

    return pCharacter;
}

bool GMKanturu2nd::Set_CurrentAction_Kanturu2nd_Monster(CHARACTER *c, OBJECT *o)
{
    if (Is_Kanturu2nd_3rd() == false)
        return false;

    switch (c->MonsterIndex)
    {
    case MONSTER_PERSONA:
    case MONSTER_TWIN_TALE:
    case MONSTER_DREADFEAR: {
        return CheckMonsterSkill(c, o);
    }
    break;
    }

    return false;
}

CHARACTER *CTrapCanon::Create_TrapCanon(int iPosX, int iPosY, int iKey)
{
    CHARACTER *pCha = nullptr;

    pCha = CreateCharacter(iKey, MODEL_TRAP_CANON, iPosX, iPosY);
    pCha->Object.Scale = 1.0f;
    pCha->AttackTime = 0;
    pCha->LastAttackEffectTime = -1;

    return pCha;
}

void GMKanturu2nd::Sound_Kanturu2nd_Object(OBJECT *o)
{
    PlayBuffer(SOUND_KANTURU_2ND_MAPSOUND_GLOBAL);

    switch (o->Type)
    {
    case 9:
        PlayBuffer(SOUND_KANTURU_2ND_MAPSOUND_GEAR);
        break;
    case 31:
    case 35:
    case 36:
    case 37:
        PlayBuffer(SOUND_KANTURU_2ND_MAPSOUND_INCUBATOR);
        break;
    }
}

bool GMKanturu2nd::Is_Kanturu2nd()
{
    if (gMapManager.ContextMap() == WD_38KANTURU_2ND)
        return true;

    return false;
}

bool GMKanturu2nd::Is_Kanturu2nd_3rd()
{
    if (gMapManager.ContextMap() == WD_38KANTURU_2ND ||
        gMapManager.ContextMap() == WD_39KANTURU_3RD)
        return true;

    return false;
}

void GMKanturu2nd::AdvanceMonsterState(CHARACTER &character)
{
    auto &object = character.Object;
    if (object.CurrentAction == MONSTER01_DIE)
    {
        if (object.Type == MODEL_PERSONA || object.Type == MODEL_TWIN_TAIL)
            object.BlendMesh = -2;
        if (object.Type == MODEL_TWIN_TAIL)
            object.m_bRenderShadow = false;
    }
    if (object.Type == MODEL_TRAP_CANON && character.AttackTime < 1)
        character.SetLastAttackEffectTime();
}

bool GMKanturu2nd::CreateObject(OBJECT *object)
{
    if (object->Type == 8 || object->Type == 10 || object->Type == 31 || object->Type == 33 ||
        object->Type == 35 || object->Type == 36 || object->Type == 59 || object->Type == 76 ||
        object->Type == 80)
        object->m_bRenderAfterCharacter = true;

    return Create_Kanturu2nd_Object(object);
}

bool GMKanturu2nd::MoveObject(OBJECT *object)
{
    return Move_Kanturu2nd_Object(object);
}

bool GMKanturu2nd::SetCurrentActionMonster(CHARACTER *character, OBJECT *object)
{
    return Set_CurrentAction_Kanturu2nd_Monster(character, object);
}

void GMKanturu2nd::InstallBehavior()
{
    LoadWaveFile(SOUND_KANTURU_2ND_MAPSOUND_GEAR, L"Data\\Sound\\w38\\kan_relic_gear.wav", 1);
    LoadWaveFile(SOUND_KANTURU_2ND_MAPSOUND_INCUBATOR, L"Data\\Sound\\w38\\kan_relic_incubator.wav",
                 1);
    LoadWaveFile(SOUND_KANTURU_2ND_MAPSOUND_HOLE, L"Data\\Sound\\w38\\kan_relic_hole.wav", 1);
    LoadWaveFile(SOUND_KANTURU_2ND_MAPSOUND_GLOBAL, L"Data\\Sound\\w38\\kan_relic_global.wav", 1);
    LoadWaveFile(SOUND_KANTURU_2ND_PERSO_MOVE1, L"Data\\Sound\\w38\\perso_idle-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_2ND_PERSO_MOVE2, L"Data\\Sound\\w38\\perso_idle-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_2ND_PERSO_ATTACK1, L"Data\\Sound\\w38\\perso_attack-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_2ND_PERSO_ATTACK2, L"Data\\Sound\\w38\\perso_attack-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_2ND_PERSO_DIE, L"Data\\Sound\\w38\\perso_death.wav", 1);
    LoadWaveFile(SOUND_KANTURU_2ND_TWIN_MOVE1, L"Data\\Sound\\w38\\twin_idle-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_2ND_TWIN_MOVE2, L"Data\\Sound\\w38\\twin_idle-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_2ND_TWIN_ATTACK1, L"Data\\Sound\\w38\\twin_attack-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_2ND_TWIN_ATTACK2, L"Data\\Sound\\w38\\twin_attack-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_2ND_TWIN_DIE, L"Data\\Sound\\w38\\twin_death.wav", 1);
    LoadWaveFile(SOUND_KANTURU_2ND_DRED_MOVE1, L"Data\\Sound\\w38\\dred_idle-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_2ND_DRED_MOVE2, L"Data\\Sound\\w38\\dred_idle-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_2ND_DRED_ATTACK1, L"Data\\Sound\\w38\\dred_attack-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_2ND_DRED_ATTACK2, L"Data\\Sound\\w38\\dred_attack-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_2ND_DRED_DIE, L"Data\\Sound\\w38\\dred_death.wav", 1);
}

MapObjectInteraction GMKanturu2nd::ObjectInteraction(int type, CHARACTER &actor)
{
    using Action = MapObjectInteraction::Action;
    return type == 3 ? MapObjectInteraction{Action::Sit, false} : MapObjectInteraction{};
}

GMKanturu3rdPtr GMKanturu3rd::Make(SessionKeeper &keeper)
{
    return GMKanturu3rdPtr(new GMKanturu3rd(keeper));
}

GMKanturu3rd::GMKanturu3rd(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject()), g_Direction(keeper.DirectionObject()),
      KanturuSuccessMap(keeper.KanturuSuccessMap()),
      KanturuSuccessMapBackup(keeper.KanturuSuccessMapBackup()),
      iMayaAction(keeper.KanturuMayaAction()), bMayaSkill2(keeper.KanturuMayaSkill2()),
      iMayaSkill2_Counter(keeper.KanturuMayaSkill2Counter()),
      iMayaDie_Counter(keeper.KanturuMayaDieCounter()), iKanturuResult(keeper.KanturuResult()),
      fAlpha(keeper.KanturuResultAlpha()), UserCount(keeper.KanturuUserCount()),
      MonsterCount(keeper.KanturuMonsterCount())
{
}

bool GMKanturu3rd::IsInKanturu3rd()
{
    return (gMapManager.ContextMap() == WD_39KANTURU_3RD) ? true : false;
}

void GMKanturu3rd::Kanturu3rdInit()
{
    KanturuSuccessMap = false;
    KanturuSuccessMapBackup = false;
    iMayaAction = -1;
    bMayaSkill2 = false;
    iMayaSkill2_Counter = 0;
    iMayaDie_Counter = 0;
    iKanturuResult = -1;
    fAlpha = 0.1f;
    UserCount = 0;
    MonsterCount = 0;
}

bool GMKanturu3rd::IsSuccessBattle()
{
    if (KanturuSuccessMap)
        return true;
    return false;
}

void GMKanturu3rd::CheckSuccessBattle(BYTE State, BYTE DetailState)
{
    if (State == KANTURU_STATE_TOWER &&
        (DetailState == KANTURU_TOWER_REVITALIXATION || DetailState == KANTURU_TOWER_NOTIFY))
        KanturuSuccessMap = true;
    else
        KanturuSuccessMap = false;

    if (KanturuSuccessMap == KanturuSuccessMapBackup)
        return;

    KanturuSuccessMapBackup = KanturuSuccessMap;

    if (gMapManager.ContextMap() != WD_39KANTURU_3RD)
        return;

    const auto variant =
        KanturuSuccessMap ? MapDefinition::Variant::Success : MapDefinition::Variant::Base;
    if (KanturuSuccessMap)
        PlayBuffer(SOUND_KANTURU_3RD_MAP_SOUND02);
    sessionKeeper_.WorldUnit()->ReloadTerrainVariant(variant);
}

bool GMKanturu3rd::CreateKanturu3rdObject(OBJECT *o)
{
    if (!IsInKanturu3rd())
        return false;

    switch (o->Type)
    {
    case 0:
        o->Position[2] -= 2000.0f;
        break;
    case 32:
    case 47:
    case 51:
    case 52:
    case 53:
    case 54:
    case 57:
    case 58:
    case 70:
        o->HiddenMesh = -2;
        break;
    }

    return true;
}

bool GMKanturu3rd::MoveKanturu3rdObject(OBJECT *o)
{
    if (!IsInKanturu3rd())
        return false;

    float Luminosity;
    vec3_t Light;

    switch (o->Type)
    {
    case 0: {
    }
    break;
    case 25: {
        PlayBuffer(SOUND_KANTURU_3RD_MAP_SOUND05);
    }
    break;
    case 40:
    case 41:
    case 42: {
        PlayBuffer(SOUND_KANTURU_3RD_MAP_SOUND01);
    }
    break;
    case 45: {
        if (rand_fps_check(3))
        {
            o->HiddenMesh = -2;
            Luminosity = (float)(WorldRandom() % 4 + 3) * 0.3f;
            Vector(Luminosity, Luminosity, Luminosity, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 1 + o->Scale / 2.0f,
                            PrimaryTerrainLight);
        }
    }
    break;
    case 48: {
        o->HiddenMesh = -2;
    }
    break;
    case 50: {
        if (rand_fps_check(3))
        {
            Luminosity = (float)(WorldRandom() % 10) * 0.2f;
            Vector(Luminosity, Luminosity, Luminosity, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 1 + o->Scale,
                            PrimaryTerrainLight);
        }
    }
    break;
    case 54: {
        if (o->HiddenMesh == -1)
        {
            o->HiddenMesh = -2;
            Luminosity = (float)(WorldRandom() % 4 + 3) * 0.3f;
            Vector(Luminosity, Luminosity, Luminosity, Light);
            AddTerrainLight(o->Position[0], o->Position[1], Light, 1 + o->Scale / 2.0f,
                            PrimaryTerrainLight);
        }
    }
    break;
    case 71: {
        PlayBuffer(SOUND_KANTURU_3RD_MAP_SOUND04);
    }
    break;
    case 73: {
        PlayBuffer(SOUND_KANTURU_3RD_MAP_SOUND03);
    }
    break;
    }

    PlayBuffer(SOUND_KANTURU_3RD_AMBIENT);

    return true;
}

CHARACTER *GMKanturu3rd::CreateKanturu3rdMonster(int iType, int PosX, int PosY, int Key)
{
    if (!IsInKanturu3rd())
        return NULL;

    CHARACTER *c = NULL;

    switch (iType)
    {
    case MONSTER_NIGHTMARE: {
        OpenMonsterModel(MONSTER_MODEL_DARK_SKULL_SOLDIER_5);
        c = CreateCharacter(Key, MODEL_DARK_SKULL_SOLDIER_5, PosX, PosY);
        c->Object.Scale = 1.6f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;

        RegisterBone(c, L"Body_Bone1", CharacterSocket::Body_Bone1);
        RegisterBone(c, L"Body_Bone2", CharacterSocket::Body_Bone2);
        RegisterBone(c, L"Body_Bone3", CharacterSocket::Body_Bone3);
        RegisterBone(c, L"Body_Bone4", CharacterSocket::Body_Bone4);
        RegisterBone(c, L"Body_Bone5", CharacterSocket::Body_Bone5);
        RegisterBone(c, L"Body_Bone6", CharacterSocket::Body_Bone6);
        RegisterBone(c, L"Body_Bone7", CharacterSocket::Body_Bone7);
        RegisterBone(c, L"Body_Bone8", CharacterSocket::Body_Bone8);
        RegisterBone(c, L"Body_Bone9", CharacterSocket::Body_Bone9);
        RegisterBone(c, L"Body_Bone10", CharacterSocket::Body_Bone10);
        RegisterBone(c, L"Body_Bone11", CharacterSocket::Body_Bone11);
        RegisterBone(c, L"Body_Bone12", CharacterSocket::Body_Bone12);

        RegisterBone(c, L"LHand_Bone", CharacterSocket::LHand_Bone);

        RegisterBone(c, L"Body_Bone13", CharacterSocket::Body_Bone13);

        RegisterBone(c, L"Sword_Bone1", CharacterSocket::Sword_Bone1);
        RegisterBone(c, L"Sword_Bone2", CharacterSocket::Sword_Bone2);

        CreateJoint(BITMAP_JOINT_ENERGY, c->Object.Position, c->Object.Position, c->Object.Angle,
                    20, &c->Object, 10.f);
        CreateJoint(BITMAP_JOINT_ENERGY, c->Object.Position, c->Object.Position, c->Object.Angle,
                    21, &c->Object, 10.f);

        RegisterBone(c, L"Eye_Bone1", CharacterSocket::Eye_Bone1);
        RegisterBone(c, L"Eye_Bone2", CharacterSocket::Eye_Bone2);

        CreateJoint(BITMAP_JOINT_ENERGY, c->Object.Position, c->Object.Position, c->Object.Angle,
                    18, &c->Object, 10.f);
        CreateJoint(BITMAP_JOINT_ENERGY, c->Object.Position, c->Object.Position, c->Object.Angle,
                    19, &c->Object, 10.f);

        RegisterBone(c, L"Windmill_Bone1", CharacterSocket::Windmill_Bone1);
    }
    break;
    case MONSTER_MAYA_HAND_LEFT: {
        OpenMonsterModel(MONSTER_MODEL_MAYA_HAND_LEFT);
        c = CreateCharacter(Key, MODEL_MAYA_HAND_LEFT, PosX, PosY);
        c->Object.Scale = 2.28f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;

        RegisterBone(c, L"L_Hand01", CharacterSocket::L_Hand01);
        RegisterBone(c, L"L_Hand02", CharacterSocket::L_Hand02);
        RegisterBone(c, L"L_Hand03", CharacterSocket::L_Hand03);
        RegisterBone(c, L"L_Hand04", CharacterSocket::L_Hand04);
        RegisterBone(c, L"L_Hand05", CharacterSocket::L_Hand05);

        RegisterBone(c, L"L_Hand11", CharacterSocket::L_Hand11);
        RegisterBone(c, L"L_Hand12", CharacterSocket::L_Hand12);
        RegisterBone(c, L"L_Hand13", CharacterSocket::L_Hand13);
        RegisterBone(c, L"L_Hand14", CharacterSocket::L_Hand14);
        RegisterBone(c, L"L_Hand15", CharacterSocket::L_Hand15);

        RegisterBone(c, L"L_Hand21", CharacterSocket::L_Hand21);
        RegisterBone(c, L"L_Hand22", CharacterSocket::L_Hand22);
        RegisterBone(c, L"L_Hand23", CharacterSocket::L_Hand23);
        RegisterBone(c, L"L_Hand24", CharacterSocket::L_Hand24);
        RegisterBone(c, L"L_Hand25", CharacterSocket::L_Hand25);

        CreateJoint(BITMAP_JOINT_ENERGY, c->Object.Position, c->Object.Position, c->Object.Angle,
                    18, &c->Object, 15.f);
        CreateJoint(BITMAP_JOINT_ENERGY, c->Object.Position, c->Object.Position, c->Object.Angle,
                    19, &c->Object, 15.f);
        CreateJoint(BITMAP_JOINT_ENERGY, c->Object.Position, c->Object.Position, c->Object.Angle,
                    20, &c->Object, 15.f);
        CreateJoint(BITMAP_JOINT_ENERGY, c->Object.Position, c->Object.Position, c->Object.Angle,
                    21, &c->Object, 15.f);
        CreateJoint(BITMAP_JOINT_ENERGY, c->Object.Position, c->Object.Position, c->Object.Angle,
                    27, &c->Object, 15.f);
    }
    break;
    case MONSTER_MAYA_HAND_RIGHT: {
        OpenMonsterModel(MONSTER_MODEL_MAYA_HAND_RIGHT);
        c = CreateCharacter(Key, MODEL_MAYA_HAND_RIGHT, PosX, PosY);
        c->Object.Scale = 2.28f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;

        RegisterBone(c, L"R_Hand01", CharacterSocket::R_Hand01);
        RegisterBone(c, L"R_Hand02", CharacterSocket::R_Hand02);
        RegisterBone(c, L"R_Hand03", CharacterSocket::R_Hand03);
        RegisterBone(c, L"R_Hand04", CharacterSocket::R_Hand04);
        RegisterBone(c, L"R_Hand05", CharacterSocket::R_Hand05);

        RegisterBone(c, L"R_Hand11", CharacterSocket::R_Hand11);
        RegisterBone(c, L"R_Hand12", CharacterSocket::R_Hand12);
        RegisterBone(c, L"R_Hand13", CharacterSocket::R_Hand13);
        RegisterBone(c, L"R_Hand14", CharacterSocket::R_Hand14);
        RegisterBone(c, L"R_Hand15", CharacterSocket::R_Hand15);

        RegisterBone(c, L"R_Hand21", CharacterSocket::R_Hand21);
        RegisterBone(c, L"R_Hand22", CharacterSocket::R_Hand22);
        RegisterBone(c, L"R_Hand23", CharacterSocket::R_Hand23);
        RegisterBone(c, L"R_Hand24", CharacterSocket::R_Hand24);
        RegisterBone(c, L"R_Hand25", CharacterSocket::R_Hand25);

        CreateJoint(BITMAP_JOINT_ENERGY, c->Object.Position, c->Object.Position, c->Object.Angle,
                    28, &c->Object, 15.f);
        CreateJoint(BITMAP_JOINT_ENERGY, c->Object.Position, c->Object.Position, c->Object.Angle,
                    29, &c->Object, 15.f);
        CreateJoint(BITMAP_JOINT_ENERGY, c->Object.Position, c->Object.Position, c->Object.Angle,
                    30, &c->Object, 15.f);
        CreateJoint(BITMAP_JOINT_ENERGY, c->Object.Position, c->Object.Position, c->Object.Angle,
                    31, &c->Object, 15.f);
        CreateJoint(BITMAP_JOINT_ENERGY, c->Object.Position, c->Object.Position, c->Object.Angle,
                    33, &c->Object, 15.f);
    }
    break;
    case MONSTER_MAYA: {
        OpenMonsterModel(MONSTER_MODEL_BULL_FIGHTER); // shouldn't that be MONSTER_MODEL_MAYA?
        c = CreateCharacter(Key, MODEL_MAYA, PosX, PosY);
        c->Object.Scale = 0.2f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        c->Object.HiddenMesh = -2;
    }
    break;
    }

    return c;
}

bool GMKanturu3rd::SetCurrentActionKanturu3rdMonster(CHARACTER *c, OBJECT *o)
{
    if (!IsInKanturu3rd())
        return false;

    switch (c->MonsterIndex)
    {
    case MONSTER_NIGHTMARE:
    case MONSTER_MAYA_HAND_LEFT:
    case MONSTER_MAYA_HAND_RIGHT:
    case MONSTER_MAYA:
        return CheckMonsterSkill(c, o);
    }

    return false;
}

void GMKanturu3rd::MayaSceneMayaAction(BYTE Skill)
{
    iMayaAction = Skill;

    switch (iMayaAction)
    {
    case 0:
        break;
    case 1:
        bMayaSkill2 = true;
        break;
    }
}

void GMKanturu3rd::MayaAction(OBJECT *o, BMD *b)
{
    vec3_t Angle, Direction, Position, Pos, Light;

    if (g_Direction.m_CKanturu.GetMayaExplotion())
        iMayaAction = 2;

    switch (iMayaAction)
    {
    case 0: {
        float Matrix[3][4];

        Vector(0.0f, 0.0f, 0.0f, Angle);
        Vector(-200.0f + (WorldRandom() % 60 + 50.0f), 0.0f, 0.0f, Direction);
        AngleMatrix(Angle, Matrix);
        VectorRotate(Direction, Matrix, Position);

        Vector(0.2f, 0.2f, 0.4f, Light);
        b->TransformPosition(BoneTransform[34], Position, Pos, false);
        CreateEffect(MODEL_STORM3, Pos, o->Angle, Light);

        for (int i = 0; i < 100; i++)
        {
            Vector(1.0f, 1.0f, 1.0f, Light);
            Pos[0] = Hero->Object.Position[0] + (float)(WorldRandom() % 20 - 10) * 90.0f;
            Pos[1] = Hero->Object.Position[1] + (float)(WorldRandom() % 20 - 10) * 90.0f;
            Pos[2] = Hero->Object.Position[2] - (float)(WorldRandom() % 5) * 100.0f - 500.0f;
            CreateEffect(MODEL_STONE1 + WorldRandom() % 2, Pos, o->Angle, Light, 2);
        }
        iMayaAction = -1;

        PlayBuffer(SOUND_KANTURU_3RD_MAYA_STORM);
    }
    break;
    case 1: {
        if (bMayaSkill2)
        {
            if (iMayaSkill2_Counter == 0 || g_Time.GetTimeCheck(WorldTime, 0, 1000))
            {
                for (int i = 0; i < 10; i++)
                {
                    Pos[0] = Hero->Object.Position[0] + (float)(WorldRandom() % 20 - 10) * 80.0f +
                             500.0f;
                    Pos[1] = Hero->Object.Position[1] + (float)(WorldRandom() % 20 - 10) * 80.0f;
                    Pos[2] =
                        Hero->Object.Position[2] + 300.0f + (float)(WorldRandom() % 10) * 100.0f;
                    float Scale = 5.0f + WorldRandom() % 10 / 3.0f;
                    int index = MODEL_MAYASTONE1 + WorldRandom() % 3;
                    Vector(1.0f, 1.0f, 1.0f, Light);
                    CreateEffect(index, Pos, o->Angle, Light, 0, NULL, -1, 0, 0, 0, Scale);
                    CreateEffect(MODEL_MAYASTONEFIRE, Pos, o->Angle, Light, index, NULL, -1, 0, 0,
                                 0, Scale);
                }

                iMayaSkill2_Counter++;
            }

            if (iMayaSkill2_Counter > 3)
            {
                iMayaAction = -1;
                bMayaSkill2 = false;
                iMayaSkill2_Counter = 0;
            }
        }
    }
    break;
    case 2: {
        if (g_Direction.m_CKanturu.GetMayaExplotion())
        {
            int Index[11] = {3, 5, 18, 19, 23, 25, 26, 27, 28, 29, 30};

            if (iMayaDie_Counter == 0)
            {
                if (rand_fps_check(5))
                {
                    for (int j = 0; j < 3; j++)
                    {
                        Vector(WorldRandom() % 20 - 10.0f, WorldRandom() % 20 - 10.0f,
                               WorldRandom() % 20 - 10.0f, Position);
                        Vector(0.0f, 0.0f, 0.0f, Position);
                        Vector(1.0f, 1.0f, 1.0f, Light);
                        b->TransformPosition(BoneTransform[33 + WorldRandom() % 42], Position, Pos,
                                             false);
                        CreateParticle(BITMAP_EXPLOTION, Pos, o->Angle, Light, 1,
                                       (1.5f + WorldRandom() % 10 / 20.0f));
                    }
                }

                if (g_Time.GetTimeCheck(WorldTime, 1, 1200))
                    iMayaDie_Counter++;

                PlayBuffer(SOUND_KANTURU_3RD_MAYA_END);
            }
            else if (iMayaDie_Counter == 1 || iMayaDie_Counter == 3)
            {
                Vector(0.0f, -100.0f, 0.0f, Position);
                Vector(1.0f, 1.0f, 1.0f, Light);
                b->TransformPosition(BoneTransform[11], Position, Pos, false);
                CreateEffect(MODEL_MAYASTAR, Pos, o->Angle, Light);

                Vector(0.0f, -0.0f, 0.0f, Position);
                b->TransformPosition(BoneTransform[21], Position, Pos, false);
                CreateEffect(MODEL_MAYASTAR, Pos, o->Angle, Light);

                Vector(0.0f, -0.0f, 0.0f, Position);
                b->TransformPosition(BoneTransform[24], Position, Pos, false);
                CreateEffect(MODEL_MAYASTAR, Pos, o->Angle, Light);

                iMayaDie_Counter++;
            }
            else if (iMayaDie_Counter >= 5)
            {
                iMayaAction = -1;
                g_Direction.m_CKanturu.SetMayaExplotion(false);
                iMayaDie_Counter = 0;
            }
            else
            {
                for (int i = 0; i < 3; i++)
                {
                    Vector(1.0f, 1.0f, 1.0f, Light);
                    Vector(WorldRandom() % 100 - 50.0f, WorldRandom() % 100 - 50.0f,
                           WorldRandom() % 100 - 50.0f, Position);
                    b->TransformPosition(BoneTransform[Index[WorldRandom() % 11]], Position, Pos,
                                         false);
                    CreateParticle(BITMAP_EXPLOTION, Pos, o->Angle, Light, 1,
                                   1.0f + (i * 1.5f) + WorldRandom() % 10 / 10.0f);

                    Vector(1.0f, 0.5f, 0.3f, Light);
                    Vector(WorldRandom() % 200 - 100.0f, WorldRandom() % 200 - 100.0f,
                           WorldRandom() % 200 - 100.0f, Position);
                    b->TransformPosition(BoneTransform[Index[WorldRandom() % 11]], Position, Pos,
                                         false);
                    CreateParticle(BITMAP_SMOKE, Pos, o->Angle, Light, 47,
                                   1.5f + WorldRandom() % 4);
                }

                if (g_Time.GetTimeCheck(WorldTime, 2, 1200))
                    iMayaDie_Counter++;
            }
        }
    }
    break;
    }
}

void GMKanturu3rd::Kanturu3rdState(BYTE State, BYTE DetailState)
{
    g_Direction.m_CKanturu.GetKanturuAllState(gMapManager.ContextMap(), State, DetailState);
}

void GMKanturu3rd::Kanturu3rdResult(BYTE Result)
{
    resultDisplayMilliseconds_ = 0.0;

    switch (Result)
    {
    case 0:
        iKanturuResult = 0;
        break;
    case 1: {
        if (g_Direction.m_CKanturu.m_iKanturuState != KANTURU_STATE_NIGHTMARE_BATTLE)
            break;

        iKanturuResult = 1;
    }
    break;
    }
}

void GMKanturu3rd::Kanturu3rdUserandMonsterCount(int Count1, int Count2)
{
    UserCount = Count2;
    MonsterCount = Count1;
}

void GMKanturu3rd::Kanturu3rdSuccess()
{
    float fPosX, fPosY, fWidth, fHeight, tu, tv;

    fWidth = 372.0f;
    fHeight = 99.0f;
    fPosX = ((float)REFERENCE_WIDTH - fWidth) / 2.0f;
    fPosY = ((float)REFERENCE_HEIGHT - fWidth) / 2.0f;
    tu = fWidth / 512.f;
    tv = fHeight / 128.f;

    EnableAlphaTest();
    RenderBitmap(BITMAP_KANTURU_SUCCESS, fPosX, fPosY, fWidth, fHeight, 0.f, 0.f, tu, tv, true,
                 true, fAlpha);
}

void GMKanturu3rd::Kanturu3rdFailed()
{
    float fPosX, fPosY, fWidth, fHeight, tu, tv;

    fWidth = 372.0f;
    fHeight = 99.0f;
    fPosX = ((float)REFERENCE_WIDTH - fWidth) / 2.0f;
    fPosY = ((float)REFERENCE_HEIGHT - fWidth) / 2.0f;
    tu = fWidth / 512.f;
    tv = fHeight / 128.f;

    EnableAlphaTest();
    RenderBitmap(BITMAP_KANTURU_FAILED, fPosX, fPosY, fWidth, fHeight, 0.f, 0.f, tu, tv, true, true,
                 fAlpha);
}

void GMKanturu3rd::AdvanceMonsterState(CHARACTER &character, BMD &model)
{
    auto &object = character.Object;

    if (!IsInKanturu3rd())
        return;
    if (object.Type == MODEL_MAYA_HAND_LEFT || object.Type == MODEL_MAYA_HAND_RIGHT)
        object.m_bRenderShadow = false;
    if (object.Type == MODEL_SMELTING_NPC)
    {
        object.Scale = 2.5f;
        object.Angle[2] = 0.f;
        object.Position[2] = -60.f + sinf(WorldTime * 0.002f) * 5.8f;
    }
    AdvanceBattleRing(object);
}

bool GMKanturu3rd::CreateObject(OBJECT *object)
{
    if (object->Type == 8 || object->Type == 10 || object->Type == 19 || object->Type == 20 ||
        object->Type == 21 || object->Type == 24 || object->Type == 25 || object->Type == 73)
        object->m_bRenderAfterCharacter = true;

    return CreateKanturu3rdObject(object);
}

bool GMKanturu3rd::MoveObject(OBJECT *object)
{
    return MoveKanturu3rdObject(object);
}

bool GMKanturu3rd::SetCurrentActionMonster(CHARACTER *character, OBJECT *object)
{
    const bool shared = Set_CurrentAction_Kanturu2nd_Monster(character, object);
    return SetCurrentActionKanturu3rdMonster(character, object) || shared;
}

void GMKanturu3rd::InstallBehavior()
{
    LoadWaveFile(SOUND_KANTURU_3RD_MAYA_INTRO, L"Data\\Sound\\w39\\maya_intro.wav", 1);
    LoadWaveFile(SOUND_KANTURU_3RD_MAYA_END, L"Data\\Sound\\w39\\maya_death.wav", 1);
    LoadWaveFile(SOUND_KANTURU_3RD_MAYA_STORM, L"Data\\Sound\\w39\\maya_storm.wav", 1);
    LoadWaveFile(SOUND_KANTURU_3RD_MAYAHAND_ATTACK1, L"Data\\Sound\\w39\\maya_hand_attack-01.wav",
                 1);
    LoadWaveFile(SOUND_KANTURU_3RD_MAYAHAND_ATTACK2, L"Data\\Sound\\w39\\maya_hand_attack-02.wav",
                 1);
    LoadWaveFile(SOUND_KANTURU_3RD_NIGHTMARE_INTRO, L"Data\\Sound\\w39\\nightmare_intro.wav", 1);
    LoadWaveFile(SOUND_KANTURU_3RD_NIGHTMARE_ATT1, L"Data\\Sound\\w39\\nightmare_attack-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_3RD_NIGHTMARE_ATT2, L"Data\\Sound\\w39\\nightmare_skill-01", 1);
    LoadWaveFile(SOUND_KANTURU_3RD_NIGHTMARE_ATT3, L"Data\\Sound\\w39\\nightmare_skill-02", 1);
    LoadWaveFile(SOUND_KANTURU_3RD_NIGHTMARE_ATT4, L"Data\\Sound\\w39\\nightmare_skill-03", 1);
    LoadWaveFile(SOUND_KANTURU_3RD_NIGHTMARE_DIE, L"Data\\Sound\\w39\\nightmare_death.wav", 1);
    LoadWaveFile(SOUND_KANTURU_3RD_NIGHTMARE_IDLE1, L"Data\\Sound\\w39\\nightmare_idle-01.wav", 1);
    LoadWaveFile(SOUND_KANTURU_3RD_NIGHTMARE_IDLE2, L"Data\\Sound\\w39\\nightmare_idle-02.wav", 1);
    LoadWaveFile(SOUND_KANTURU_3RD_NIGHTMARE_TELE, L"Data\\Sound\\w39\\nightmare_tele.wav", 1);
    LoadWaveFile(SOUND_KANTURU_3RD_MAP_SOUND01, L"Data\\Sound\\w39\\kan_boss_crystal.wav", 1);
    LoadWaveFile(SOUND_KANTURU_3RD_MAP_SOUND02, L"Data\\Sound\\w39\\kan_boss_disfield.wav", 1);
    LoadWaveFile(SOUND_KANTURU_3RD_MAP_SOUND03, L"Data\\Sound\\w39\\kan_boss_field.wav", 1);
    LoadWaveFile(SOUND_KANTURU_3RD_MAP_SOUND04, L"Data\\Sound\\w39\\kan_boss_gear.wav", 1);
    LoadWaveFile(SOUND_KANTURU_3RD_MAP_SOUND05, L"Data\\Sound\\w39\\kan_boss_incubator.wav", 1);
    LoadWaveFile(SOUND_KANTURU_3RD_AMBIENT, L"Data\\Sound\\w39\\kan_boss_global.wav", 1);
}

bool GMKanturu3rd::SetMonsterDeathAction(CHARACTER *character, OBJECT *object)
{
    switch (object->Type)
    {
    case MODEL_BLADE_HUNTER:
    case MODEL_TWIN_TAIL:
        if (g_Direction.m_CKanturu.m_iKanturuState != KANTURU_STATE_MAYA_BATTLE)
            return false;
        FallingMonster(character, object, FPS_ANIMATION_FACTOR);
        break;
    case MODEL_MAYA_HAND_LEFT:
    case MODEL_MAYA_HAND_RIGHT:
        if (g_Direction.m_CKanturu.m_iMayaState < KANTURU_MAYA_DIRECTION_MAYA3)
            return false;
        break;
    default:
        return false;
    }
    SetAction(&character->Object, MONSTER01_STOP2);
    return true;
}

bool MapProcess::ConfigureMonsterLinks(CHARACTER *character, int type)
{
    switch (type)
    {
    case MODEL_ILLUSION_OF_KUNDUN:
    case MODEL_AEGIS:
    case MODEL_DEATH_CENTURION:
    case MODEL_NECRON:
    case MODEL_SHRIKER:
        return hellas_->SettingHellasMonsterLinkBone(character, type);
    case MODEL_NPC_CAPATULT_ATT:
    case MODEL_CASTLE_GATE1:
        return battleCastle_->SettingBattleCastleMonsterLinkBone(character, type);
    default:
        return false;
    }
}

bool MapProcess::StopMonster(CHARACTER *character, OBJECT *object)
{
    BaseMap *const map = ContextBehavior();
    return map && map->StopMonster(character, object);
}

void MapProcess::MoveCharacterState(CHARACTER *character, OBJECT *object)
{
    if (BaseMap *const map = ContextBehavior())
        map->MoveCharacterState(character, object);
}

bool MapProcess::SetMonsterDeathAction(CHARACTER *character, OBJECT *object)
{
    BaseMap *const map = ContextBehavior();
    return map && map->SetMonsterDeathAction(character, object);
}

bool MapProcess::PlayMonsterDeathSound(OBJECT *object)
{
    BaseMap *const map = ContextBehavior();
    return map && map->PlayMonsterDeathSound(object);
}

float MapProcess::MonsterDeathRotationRate(const OBJECT &object)
{
    const float rate =
        object.Type == MODEL_ILLUSION_OF_KUNDUN && object.LifeTime >= 100 ? 0.01f : 0.02f;
    BaseMap *const map = ContextBehavior();
    return map ? map->MonsterDeathRotationRate(object, rate) : rate;
}

float MapProcess::ItemDrawHeight(const OBJECT &object, int index)
{
    BaseMap *const map = ContextBehavior();
    return map ? map->ItemDrawHeight(object, index) : object.Position[2];
}

bool MapProcess::PushCharacter(CHARACTER *character, OBJECT *object, float speed)
{
    BaseMap *const map = ContextBehavior();
    return map && map->PushCharacter(character, object, speed);
}

bool MapProcess::AdvanceCharacterDeath(CHARACTER *character, OBJECT *object)
{
    BaseMap *const map = ContextBehavior();
    return map && map->AdvanceCharacterDeath(character, object);
}

void MapProcess::AdvanceCharacterStopTime()
{
    if (BaseMap *const map = ContextBehavior())
        map->AdvanceCharacterStopTime();
}

void MapProcess::BeginCharacterTick()
{
    if (BaseMap *const map = ContextBehavior())
        map->BeginCharacterTick();
}

void MapProcess::ObserveCharacterTick(const CHARACTER &character)
{
    if (BaseMap *const map = ContextBehavior())
        map->ObserveCharacterTick(character);
}

void MapProcess::FinishCharacterTick()
{
    if (BaseMap *const map = ContextBehavior())
        map->FinishCharacterTick();
}

const MapCharacterPolicy &MapProcess::CharacterPolicy() const noexcept
{
    static const MapCharacterPolicy fallback;
    const auto *definition = sessionKeeper_.WorldContextDefinition();
    return definition ? definition->character : fallback;
}

bool MapProcess::TerrainCutscene() const
{
    BaseMap *const map = ContextBehavior();
    return map && map->TerrainCutscene();
}

bool MapProcess::TerrainIsAirborne() const
{
    return CharacterPolicy().skyTerrain || TerrainCutscene();
}

bool MapProcess::MountsUseFlyingActions() const
{
    return CharacterPolicy().flyingMounts || TerrainCutscene();
}

bool MapProcess::GroundShadowsVisible() const
{
    return CharacterPolicy().groundShadows && !TerrainCutscene();
}

const MapTerrainPolicy &MapProcess::TerrainPolicy() const noexcept
{
    static const MapTerrainPolicy fallback;
    const auto *definition = sessionKeeper_.WorldContextDefinition();
    return definition ? definition->terrain : fallback;
}

namespace
{
void ReleaseWorldTextures(SessionKeeper &keeper)
{
    for (int slot = BITMAP_MAPTILE; slot <= BITMAP_RAIN; ++slot)
        keeper.DeleteSessionBitmap(slot);
}
} // namespace

CMapManager::CMapManager(SessionKeeper &keeper) noexcept // OK
    : SessionLegacyCalls(keeper), binding_(keeper.WorldState()),
      previewActive_(keeper.worldPreviewActive_), Boids(keeper.BoidsStorage()),
      Fishs(keeper.FishsStorage()), Operates(keeper.OperatesStorage()),
      ObjectBlock(keeper.ObjectBlocks()), cameraMove_(keeper.CameraMoveObject()),
      gMapManager(*this), g_Direction(keeper.DirectionObject()),
      g_ErrorReport(keeper.ErrorReport()), g_hWnd(keeper.PlatformWindowHandle())
{
}

void CMapManager::ConnectBattleCastleForConstruction(CGMBattleCastle &battleCastle) noexcept
{
    if (battleCastleForConstruction_ != nullptr)
    {
        std::terminate();
    }
    battleCastleForConstruction_ = &battleCastle;
}

void CMapManager::ConnectCrywolf1stForConstruction(CGMCrywolf1st &crywolf1st) noexcept
{
    if (crywolf1stForConstruction_ != nullptr)
    {
        std::terminate();
    }
    crywolf1stForConstruction_ = &crywolf1st;
}

void CMapManager::ClearMapOwnersForDestruction(CGMBattleCastle *battleCastle,
                                               CGMCrywolf1st *crywolf1st) noexcept
{
    if (battleCastleForConstruction_ != battleCastle || crywolf1stForConstruction_ != crywolf1st)
    {
        std::terminate();
    }
    battleCastleForConstruction_ = nullptr;
    crywolf1stForConstruction_ = nullptr;
}

CGMBattleCastle &CMapManager::BattleCastleForUse() const noexcept
{
    if (battleCastleForConstruction_ == nullptr)
    {
        std::terminate();
    }
    return *battleCastleForConstruction_;
}

CGMCrywolf1st &CMapManager::Crywolf1stForUse() const noexcept
{
    if (crywolf1stForConstruction_ == nullptr)
    {
        std::terminate();
    }
    return *crywolf1stForConstruction_;
}

CMapManager::~CMapManager() // OK
{
}

MapDefinition::Variant CMapManager::EntryTerrainVariant(int rawMap) noexcept
{
    using Variant = MapDefinition::Variant;
    if (rawMap == WD_34CRYWOLF_1ST)
        return Crywolf1stForUse().TerrainVariant();
    if (rawMap == WD_30BATTLECASTLE && IsBattleCastleStart())
        return Variant::War;
    // Entry resets Kanturu success before installing its terrain.
    return Variant::Base;
}

void CMapManager::DeleteObjects()
{
    sessionKeeper_.GameplayForConstruction().ClearWorldEffects();
    if (Models.IsAllocated())
    {
        for (int model = MODEL_WORLD_OBJECT; model < MAX_WORLD_OBJECTS; ++model)
        {
            BMD *const loaded = Models.Find(model);
            if (loaded != nullptr)
            {
                loaded->Release();
            }
        }
    }

    for (int i = 0; i < 16; i++)
    {
        for (int j = 0; j < 16; j++)
        {
            OBJECT_BLOCK *ob = &ObjectBlock[i * 16 + j];
            OBJECT *o = ob->Tail;
            while (1)
            {
                if (o != NULL)
                {
                    OBJECT *Temp = o->Prior;
                    DeleteObject(o, ob);
                    if (Temp == NULL)
                        break;
                    o = Temp;
                }
                else
                    break;
            }
            ob->Head = NULL;
            ob->Tail = NULL;
            std::vector<WorldObjectDrawGroup>().swap(ob->DrawGroups);
            std::vector<RenderTapeRigidInstance>().swap(ob->DrawInstances);
            ob->DrawGroupsDirty = true;
        }
    }

    ReleaseWorldTextures(sessionKeeper_);

    for (int i = 0; i < MAX_BOIDS; i++)
        Boids[i].Live = false;
    for (int i = 0; i < MAX_FISHS; i++)
        Fishs[i].Live = false;
    for (int i = 0; i < MAX_OPERATES; i++)
        Operates[i].Live = false;
}

const MapDefinition *CMapManager::DefinitionFor(int rawMap) const noexcept
{
    return rawMap == -1 ? sessionKeeper_.WorldContextDefinition() : MapDefinition::Find(rawMap);
}

bool CMapManager::InChaosCastle(int iMap)
{
    const MapDefinition *definition = DefinitionFor(iMap);
    return definition && definition->family == MapDefinition::Family::ChaosCastle;
}

bool CMapManager::InBloodCastle(int iMap)
{
    const MapDefinition *definition = DefinitionFor(iMap);
    return definition && definition->family == MapDefinition::Family::BloodCastle;
}

bool CMapManager::InDevilSquare()
{
    const MapDefinition *definition = DefinitionFor();
    return definition && definition->family == MapDefinition::Family::DevilSquare;
}

bool CMapManager::InHellas(int iMap)
{
    const MapDefinition *definition = DefinitionFor(iMap);
    return definition && definition->family == MapDefinition::Family::Hellas;
}

bool CMapManager::InHiddenHellas(int iMap)
{
    if (iMap == -1)
    {
        iMap = this->ContextMap();
    }

    return iMap == WD_24HELLAS_7;
}

bool CMapManager::IsPKField()
{
    return (this->ContextMap() == WD_63PK_FIELD) ? true : false;
}

bool CMapManager::IsCursedTemple()
{
    const auto *definition = DefinitionFor();
    return definition && definition->family == MapDefinition::Family::CursedTemple;
}

bool CMapManager::IsEmpireGuardian1()
{
    return (this->ContextMap() == WD_69EMPIREGUARDIAN1) ? true : false;
}

bool CMapManager::IsEmpireGuardian2()
{
    return (this->ContextMap() == WD_70EMPIREGUARDIAN2) ? true : false;
}

bool CMapManager::IsEmpireGuardian3()
{
    return (this->ContextMap() == WD_71EMPIREGUARDIAN3) ? true : false;
}

bool CMapManager::IsEmpireGuardian4()
{
    const int map = ContextMap();
    return map == WD_72EMPIREGUARDIAN4 || map == WD_73NEW_LOGIN_SCENE ||
           map == WD_74NEW_CHARACTER_SCENE;
}

bool CMapManager::IsEmpireGuardian()
{
    const auto *definition = DefinitionFor();
    return definition &&
           (definition->family == MapDefinition::Family::EmpireGuardian || IsEmpireGuardian4());
}

bool CMapManager::InBattleCastle(int iMap)
{
    const MapDefinition *definition = DefinitionFor(iMap);
    return definition && definition->family == MapDefinition::Family::BattleCastle;
}

const wchar_t *CMapManager::GetMapName(int iMap)
{
    const MapDefinition *definition = MapDefinition::Find(iMap);
    return definition ? I18N::Game::Lookup(definition->nameTextId) : L"";
}
bool MapProcess::CreateObject(OBJECT *o)
{
    BaseMap *const map = ContextBehavior();
    return map != nullptr && map->CreateObject(o);
}

bool MapProcess::MoveObject(OBJECT *o)
{
    BaseMap *const map = ContextBehavior();
    if (map == nullptr)
    {
        return false;
    }
    map->PlayObjectSound(o);
    return map->MoveObject(o);
}

bool MapProcess::MoveObject(OBJECT *o, CTimer2::StartTickTime &timer2StartTickTime)
{
    BaseMap *const map = ContextBehavior();
    if (map == nullptr)
    {
        return false;
    }
    map->PlayObjectSound(o);
    return map->MoveObject(o, timer2StartTickTime);
}

bool MapProcess::SetCurrentActionMonster(CHARACTER *c, OBJECT *o)
{
    // These actions belong to shared monster models, including spawns outside
    // Hellas and Battle Castle. Map-specific action follows once, in legacy order.
    bool handled = hellas_->SetCurrentAction_HellasMonster(c, o);
    handled = battleCastle_->SetCurrentAction_BattleCastleMonster(c, o) || handled;
    if (BaseMap *const map = ContextBehavior())
        handled = map->SetCurrentActionMonster(c, o) || handled;
    return handled;
}

bool MapProcess::ReceiveMapMessage(BYTE code, BYTE subcode, BYTE *ReceiveBuffer)
{
    BaseMap *const map = ContextBehavior();
    return map != nullptr && map->ReceiveMapMessage(code, subcode, ReceiveBuffer);
}

void MapProcess::InstallBehavior()
{
    if (BaseMap *const map = ContextBehavior())
        map->InstallBehavior();
}

const MapPresentationPolicy &MapProcess::Presentation() const noexcept
{
    static const MapPresentationPolicy fallback;
    const auto *definition = sessionKeeper_.WorldContextDefinition();
    return definition ? definition->presentation : fallback;
}

CGMCryingWolf2ndPtr CGMCryingWolf2nd::Make(SessionKeeper &keeper)
{
    return CGMCryingWolf2ndPtr(new CGMCryingWolf2nd(keeper));
}

CGMCryingWolf2nd::CGMCryingWolf2nd(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject())
{
}

CGMCryingWolf2nd::~CGMCryingWolf2nd() = default;

bool CGMCryingWolf2nd::IsCyringWolf2nd()
{
    return (gMapManager.ContextMap() == WD_35CRYWOLF_2ND) ? true : false;
}

bool CGMCryingWolf2nd::CreateCryingWolf2ndObject(OBJECT *pObject)
{
    if (!IsCyringWolf2nd())
        return false;

    if (pObject->Type == 5)
        pObject->Timer = 0.f;
    return true;
}
bool CGMCryingWolf2nd::MoveCryingWolf2ndObject(OBJECT *pObject)
{
    if (!IsCyringWolf2nd())
        return false;

    float Luminosity;
    vec3_t Light;

    switch (pObject->Type)
    {
    case 2:
    case 5:
        pObject->HiddenMesh = -2;
        break;
    case 3:
        Luminosity = (float)(WorldRandom() % 4 + 3) * 0.1f;
        Vector(Luminosity, Luminosity * 0.6f, Luminosity * 0.2f, Light);
        AddTerrainLight(pObject->Position[0], pObject->Position[1], Light, 3, PrimaryTerrainLight);
        pObject->HiddenMesh = -2;
        break;
    }

    return true;
}

CHARACTER *CGMCryingWolf2nd::CreateCryingWolf2ndMonster(int iType, int PosX, int PosY, int Key)
{
    if (!IsCyringWolf2nd())
        return NULL;
    CHARACTER *pCharacter = NULL;

    switch (iType)
    {
    case MONSTER_WEREWOLFHERO: {
        OpenMonsterModel(MONSTER_MODEL_WEREWOLF_HERO);
        pCharacter = CreateCharacter(Key, MODEL_WEREWOLF_HERO, PosX, PosY);
        pCharacter->Object.Scale = 1.25f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"Monster95_Head", CharacterSocket::Monster95_Head);
    }
    break;
    case MONSTER_VALAM: {
        OpenMonsterModel(MONSTER_MODEL_VALAM);
        pCharacter = CreateCharacter(Key, MODEL_VALAM, PosX, PosY);
        pCharacter->Object.Scale = 1.2f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"Monster96_Top", CharacterSocket::Monster96_Top);
        RegisterBone(pCharacter, L"Monster96_Center", CharacterSocket::Monster96_Center);
        RegisterBone(pCharacter, L"Monster96_Bottom", CharacterSocket::Monster96_Bottom);
    }
    break;
    case MONSTER_SOLAM: {
        OpenMonsterModel(MONSTER_MODEL_SOLAM);
        pCharacter = CreateCharacter(Key, MODEL_SOLAM, PosX, PosY);
        pCharacter->Object.Scale = 1.2f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
    }
    break;
    case MONSTER_SCOUT: {
        OpenMonsterModel(MONSTER_MODEL_SCOUT);
        pCharacter = CreateCharacter(Key, MODEL_SCOUT, PosX, PosY);
        pCharacter->Object.Scale = 1.2f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
    }
    break;
    case MONSTER_HAMMER_SCOUT: {
        OpenMonsterModel(MONSTER_MODEL_BALRAM);
        pCharacter = CreateCharacter(Key, MODEL_BALRAM, PosX, PosY);
        pCharacter->Object.Scale = 1.25f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
    }
    break;
    }

    return pCharacter;
}

bool CGMCryingWolf2nd::CreateObject(OBJECT *object)
{
    return CreateCryingWolf2ndObject(object);
}

bool CGMCryingWolf2nd::MoveObject(OBJECT *object)
{
    return MoveCryingWolf2ndObject(object);
}

CGMHuntingGroundPtr CGMHuntingGround::Make(SessionKeeper &keeper)
{
    return CGMHuntingGroundPtr(new CGMHuntingGround(keeper));
}

CGMHuntingGround::CGMHuntingGround(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject())
{
}

CGMHuntingGround::~CGMHuntingGround() = default;

bool CGMHuntingGround::IsInHuntingGround()
{
    return (gMapManager.ContextMap() == WD_31HUNTING_GROUND) ? true : false;
}

bool CGMHuntingGround::IsInHuntingGroundSection2(const vec3_t Position)
{
    if (Position[0] > 5449.f && Position[0] < 17822.f && Position[1] > 6784.f &&
        Position[1] < 22419.f)
        return true;
    return false;
}

bool CGMHuntingGround::CreateHuntingGroundObject(OBJECT *pObject)
{
    if (!IsInHuntingGround())
        return false;

    switch (pObject->Type)
    {
    case 27:
    case 54:
        pObject->Timer = float(WorldRandom() % 1000) * 0.01f;
        break;
    }

    return true;
}
bool CGMHuntingGround::MoveHuntingGroundObject(OBJECT *pObject)
{
    if (!IsInHuntingGround())
        return false;

    float Luminosity;
    vec3_t Light;

    switch (pObject->Type)
    {
    case 1: {
        int time = static_cast<DWORD>(WorldSimulationTime()) % 1024;
        if (time >= 0 && time < 10)
        {
            vec3_t Light;
            Vector(1.f, 1.f, 1.f, Light);
            CreateEffect(MODEL_BUTTERFLY01, pObject->Position, pObject->Angle, Light, 0, pObject);
        }
        pObject->HiddenMesh = -2; //. Hide Object
    }
    break;
    case 44: {
        int time = static_cast<DWORD>(WorldSimulationTime()) % 1024;
        if (time >= 0 && time < 10)
        {
            vec3_t Light;
            Vector(1.f, 1.f, 1.f, Light);
            CreateEffect(MODEL_BUTTERFLY01, pObject->Position, pObject->Angle, Light, 1, pObject);
        }
        pObject->HiddenMesh = -2; //. Hide Object
    }
    break;
    case 45: {
        int time = static_cast<DWORD>(WorldSimulationTime()) % 1024;
        if (time >= 0 && time < 10)
        {
            vec3_t Light;
            Vector(1.f, 1.f, 1.f, Light);
            CreateEffect(MODEL_BUTTERFLY01, pObject->Position, pObject->Angle, Light, 2, pObject);
        }
        pObject->HiddenMesh = -2; //. Hide Object
    }
    break;
    case 3:
    case 53:
        pObject->HiddenMesh = -2;
        break;
    case 27: {
        float vibration = sinf(pObject->Timer + WorldTime * 0.0024f) * 0.3f;
        pObject->Position[2] += vibration;
    }
    break;
    case 42: {
        Luminosity = (float)(WorldRandom() % 4 + 3) * 0.1f;
        Vector(Luminosity * 0.9f, Luminosity * 0.2f, Luminosity * 0.1f, Light);
        AddTerrainLight(pObject->Position[0], pObject->Position[1], Light, 3, PrimaryTerrainLight);
        pObject->HiddenMesh = -2;
    }
    break;
    }

    if (timeGetTime() - g_MusicStartStamp > 300000)
    {
        g_MusicStartStamp = timeGetTime();
        PlayBuffer(SOUND_BC_HUNTINGGROUND_AMBIENT);
    }
    return true;
}

CHARACTER *CGMHuntingGround::CreateHuntingGroundMonster(int iType, int PosX, int PosY, int Key)
{
    CHARACTER *pCharacter = NULL;
    switch (iType)
    {
    case MONSTER_LIZARD_WARRIOR: {
        OpenMonsterModel(MONSTER_MODEL_LIZARD_WARRIOR); //  81
        pCharacter = CreateCharacter(Key, MODEL_LIZARD_WARRIOR, PosX, PosY);
        pCharacter->Object.Scale = 1.2f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"Monster81_EyeRight", CharacterSocket::Monster81_EyeRight);
        RegisterBone(pCharacter, L"Monster81_EyeLeft", CharacterSocket::Monster81_EyeLeft);
    }
    break;
    case MONSTER_FIRE_GOLEM: {
        OpenMonsterModel(MONSTER_MODEL_FIRE_GOLEM); //  82
        pCharacter = CreateCharacter(Key, MODEL_FIRE_GOLEM, PosX, PosY);
        pCharacter->Object.Scale = 1.8f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"Monster82_RHand", CharacterSocket::Monster82_RHand);
        RegisterBone(pCharacter, L"Monster82_LHand", CharacterSocket::Monster82_LHand);
        RegisterBone(pCharacter, L"Monster82_Eye", CharacterSocket::Monster82_Eye);
        RegisterBone(pCharacter, L"Monster82_Back", CharacterSocket::Monster82_Back);
    }
    break;
    case MONSTER_QUEEN_BEE: {
        OpenMonsterModel(MONSTER_MODEL_QUEEN_BEE); //	83
        pCharacter = CreateCharacter(Key, MODEL_QUEEN_BEE, PosX, PosY);
        pCharacter->Object.Scale = 1.4f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"Monster83_Tail", CharacterSocket::Monster83_Tail);
    }
    break;
    case MONSTER_GIGAS_GOLEM:
    case MONSTER_POISON_GOLEM: {
        OpenMonsterModel(MONSTER_MODEL_POISON_GOLEM); // 84
        pCharacter = CreateCharacter(Key, MODEL_POISON_GOLEM, PosX, PosY);
        pCharacter->Object.Scale = 1.4f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"Monster84_PoisonTop", CharacterSocket::Monster84_PoisonTop);
        RegisterBone(pCharacter, L"Monster84_PoisonRight", CharacterSocket::Monster84_PoisonRight);
        RegisterBone(pCharacter, L"Monster84_PoisonLeft", CharacterSocket::Monster84_PoisonLeft);
        RegisterBone(pCharacter, L"Monster84_LeftHand", CharacterSocket::Monster84_LeftHand);
        RegisterBone(pCharacter, L"Monster84_RightHand", CharacterSocket::Monster84_RightHand);
    }
    break;
    case MONSTER_AXE_HERO:
    case MONSTER_AXE_WARRIOR: {
        OpenMonsterModel(MONSTER_MODEL_AXE_HERO); //85
        pCharacter = CreateCharacter(Key, MODEL_AXE_HERO, PosX, PosY);
        pCharacter->Object.Scale = 0.7f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"Monster85_LeftEye", CharacterSocket::Monster85_LeftEye);
        RegisterBone(pCharacter, L"Monster85_RightEye", CharacterSocket::Monster85_RightEye);
    }
    break;
    case MONSTER_EROHIM: {
        OpenMonsterModel(MONSTER_MODEL_EROHIM); //87
        pCharacter = CreateCharacter(Key, MODEL_EROHIM, PosX, PosY);
        pCharacter->Object.Scale = 2.f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"Monster87_LeftEye", CharacterSocket::Monster87_LeftEye);
        RegisterBone(pCharacter, L"Monster87_RightEye", CharacterSocket::Monster87_RightEye);
        RegisterBone(pCharacter, L"Monster87_LeftHand", CharacterSocket::Monster87_LeftHand);

        PlayBuffer(SOUND_BC_EROHIM_ENTER);
    }
    break;
    case MONSTER_PK_DARK_KNIGHT: {
        pCharacter = CreateCharacter(Key, MODEL_PLAYER, PosX, PosY);
        pCharacter->Skin = 1;
        pCharacter->BodyPart[BODYPART_HELM].Type = MODEL_DRAGON_HELM;
        pCharacter->BodyPart[BODYPART_ARMOR].Type = MODEL_DRAGON_ARMOR;
        pCharacter->BodyPart[BODYPART_PANTS].Type = MODEL_DRAGON_PANTS;
        pCharacter->BodyPart[BODYPART_GLOVES].Type = MODEL_DRAGON_GLOVES;
        pCharacter->BodyPart[BODYPART_BOOTS].Type = MODEL_DRAGON_BOOTS;
        pCharacter->Weapon[0].Type = MODEL_SWORD_OF_DESTRUCTION;
        pCharacter->Weapon[1].Type = MODEL_DRAGON_SHIELD;
        int Level = 9;
        pCharacter->BodyPart[BODYPART_HELM].Level = Level;
        pCharacter->BodyPart[BODYPART_ARMOR].Level = Level;
        pCharacter->BodyPart[BODYPART_PANTS].Level = Level;
        pCharacter->BodyPart[BODYPART_GLOVES].Level = Level;
        pCharacter->BodyPart[BODYPART_BOOTS].Level = Level;
        pCharacter->PK = PVP_MURDERER2;
        SetCharacterScale(pCharacter);
    }
    break;
    }
    return pCharacter;
}

bool CGMHuntingGround::SetCurrentActionHuntingGroundMonster(CHARACTER *pCharacter, OBJECT *pObject)
{
    if (!IsInHuntingGround())
        return false;

    switch (pCharacter->MonsterIndex)
    {
    case MONSTER_FIRE_GOLEM:
        if (pCharacter->Skill == AT_SKILL_BOSS)
        {
            SetAction(pObject, MONSTER01_ATTACK1);
        }
        else
        {
            SetAction(pObject, MONSTER01_ATTACK2);
        }

        return true;
    }

    return false;
}

void CGMHuntingGround::AdvanceMonsterState(OBJECT &object)
{
    if (object.CurrentAction == MONSTER01_DIE &&
        (object.Type == MODEL_FIRE_GOLEM || object.Type == MODEL_POISON_GOLEM))
        object.Live = false;
}

bool CGMHuntingGround::CreateObject(OBJECT *object)
{
    return CreateHuntingGroundObject(object);
}

bool CGMHuntingGround::MoveObject(OBJECT *object)
{
    return MoveHuntingGroundObject(object);
}

bool CGMHuntingGround::SetCurrentActionMonster(CHARACTER *character, OBJECT *object)
{
    return SetCurrentActionHuntingGroundMonster(character, object);
}

void CGMHuntingGround::InstallBehavior()
{
    LoadWaveFile(SOUND_BC_HUNTINGGROUND_AMBIENT, L"Data\\Sound\\w31\\aW31.wav", 1, true);
    LoadWaveFile(SOUND_BC_AXEWARRIOR_MOVE1, L"Data\\Sound\\w31\\mAWidle1.wav", 1);
    LoadWaveFile(SOUND_BC_AXEWARRIOR_MOVE2, L"Data\\Sound\\w31\\mAWidle2.wav", 1);
    LoadWaveFile(SOUND_BC_AXEWARRIOR_ATTACK1, L"Data\\Sound\\w31\\mAWattack1.wav", 1);
    LoadWaveFile(SOUND_BC_AXEWARRIOR_ATTACK2, L"Data\\Sound\\w31\\mAWattack2.wav", 1);
    LoadWaveFile(SOUND_BC_AXEWARRIOR_DIE, L"Data\\Sound\\w31\\mAWdeath.wav", 1);
    LoadWaveFile(SOUND_BC_LIZARDWARRIOR_MOVE1, L"Data\\Sound\\w31\\mLWidle1.wav", 1);
    LoadWaveFile(SOUND_BC_LIZARDWARRIOR_MOVE2, L"Data\\Sound\\w31\\mLWidle2.wav", 1);
    LoadWaveFile(SOUND_BC_LIZARDWARRIOR_ATTACK1, L"Data\\Sound\\w31\\mLWattack1.wav", 1);
    LoadWaveFile(SOUND_BC_LIZARDWARRIOR_ATTACK2, L"Data\\Sound\\w31\\mLWattack2.wav", 1);
    LoadWaveFile(SOUND_BC_LIZARDWARRIOR_DIE, L"Data\\Sound\\w31\\mLWdeath.wav", 1);
    LoadWaveFile(SOUND_BC_POISONGOLEM_MOVE1, L"Data\\Sound\\w31\\mPGidle1.wav", 1);
    LoadWaveFile(SOUND_BC_POISONGOLEM_MOVE2, L"Data\\Sound\\w31\\mPGidle2.wav", 1);
    LoadWaveFile(SOUND_BC_POISONGOLEM_ATTACK1, L"Data\\Sound\\w31\\mPGattack1.wav", 1);
    LoadWaveFile(SOUND_BC_POISONGOLEM_ATTACK2, L"Data\\Sound\\w31\\mPGattack2.wav", 1);
    LoadWaveFile(SOUND_BC_POISONGOLEM_ATTACK3, L"Data\\Sound\\w31\\mPGeff1.wav", 1);
    LoadWaveFile(SOUND_BC_POISONGOLEM_DIE, L"Data\\Sound\\w31\\mPGdeath.wav", 1);
    LoadWaveFile(SOUND_BC_QUEENBEE_MOVE1, L"Data\\Sound\\w31\\mQBidle1.wav", 1);
    LoadWaveFile(SOUND_BC_QUEENBEE_MOVE2, L"Data\\Sound\\w31\\mQBidle2.wav", 1);
    LoadWaveFile(SOUND_BC_QUEENBEE_ATTACK1, L"Data\\Sound\\w31\\mQBattack1.wav", 1);
    LoadWaveFile(SOUND_BC_QUEENBEE_ATTACK2, L"Data\\Sound\\w31\\mQBattack2.wav", 1);
    LoadWaveFile(SOUND_BC_QUEENBEE_DIE, L"Data\\Sound\\w31\\mQBdeath.wav", 1);
    LoadWaveFile(SOUND_BC_FIREGOLEM_MOVE1, L"Data\\Sound\\w31\\mFGidle1.wav", 1);
    LoadWaveFile(SOUND_BC_FIREGOLEM_MOVE2, L"Data\\Sound\\w31\\mFGidle2.wav", 1);
    LoadWaveFile(SOUND_BC_FIREGOLEM_ATTACK1, L"Data\\Sound\\w31\\mFGattack1.wav", 1);
    LoadWaveFile(SOUND_BC_FIREGOLEM_ATTACK2, L"Data\\Sound\\w31\\mFGattack2.wav", 1);
    LoadWaveFile(SOUND_BC_FIREGOLEM_DIE, L"Data\\Sound\\w31\\mFGdeath.wav", 1);
    LoadWaveFile(SOUND_BC_EROHIM_ENTER, L"Data\\Sound\\w31\\mELOidle1.wav", 1);
    LoadWaveFile(SOUND_BC_EROHIM_ATTACK1, L"Data\\Sound\\w31\\mELOattack1.wav", 1);
    LoadWaveFile(SOUND_BC_EROHIM_ATTACK2, L"Data\\Sound\\w31\\mELOattack2.wav", 1);
    LoadWaveFile(SOUND_BC_EROHIM_ATTACK3, L"Data\\Sound\\w31\\mELOeff1.wav", 1);
    LoadWaveFile(SOUND_BC_EROHIM_DIE, L"Data\\Sound\\w31\\mELOdeath.wav", 1);
}

using namespace SEASON3C;

GMSwampOfQuietPtr GMSwampOfQuiet::Make(SessionKeeper &keeper)
{
    return GMSwampOfQuietPtr(new GMSwampOfQuiet(keeper));
}

GMSwampOfQuiet::GMSwampOfQuiet(SessionKeeper &keeper)
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject())
{
}

GMSwampOfQuiet::~GMSwampOfQuiet() = default;

bool GMSwampOfQuiet::IsCurrentMap() const
{
    return (gMapManager.ContextMap() == WD_56MAP_SWAMP_OF_QUIET);
}

bool GMSwampOfQuiet::CreateObject(OBJECT *pObject)
{
    if (!IsCurrentMap())
        return false;

    // 	switch(pObject->Type)
    // 	{
    // 	case 103:	// 의자 설정
    // 		{
    // 			CreateOperate(pObject);
    // 		}
    // 		break;
    // 	}
    return true;
}

bool GMSwampOfQuiet::MoveObject(OBJECT *pObject)
{
    if (!IsCurrentMap())
        return false;

    float Luminosity;
    vec3_t Light;

    switch (pObject->Type)
    {
    case 57:
        pObject->HiddenMesh = -2;
        Luminosity = (float)(WorldRandom() % 4 + 3) * 0.1f;
        Vector(Luminosity, Luminosity * 0.6f, Luminosity * 0.2f, Light);
        AddTerrainLight(pObject->Position[0], pObject->Position[1], Light, 3, PrimaryTerrainLight);
        break;
    case 71:
        pObject->HiddenMesh = -2;
        break;
    case 72:
        pObject->HiddenMesh = -2;
        break;
    case 73:
        pObject->HiddenMesh = -2;
        break;
    case 74:
        pObject->HiddenMesh = -2;
        break;
    case 77:
        pObject->HiddenMesh = -2;
        break;
    case 78:
        pObject->HiddenMesh = -2;
        break;
    }
    return true;
}

CHARACTER *GMSwampOfQuiet::CreateMonster(int iType, int PosX, int PosY, int Key)
{
    CHARACTER *pCharacter = NULL;

    switch (iType)
    {
    case MONSTER_SAPIUNUS:
        OpenMonsterModel(MONSTER_MODEL_SAPIUNUS);
        pCharacter = CreateCharacter(Key, MODEL_SAPIUNUS, PosX, PosY);
        pCharacter->Object.Scale = 1.0f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        // 		{
        // 	  		BMD *b = &Models[MODEL_MONSTER01+136];
        // // 			b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        // // 			b->Actions[MONSTER01_STOP2].PlaySpeed = 0.25f;
        // // 			b->Actions[MONSTER01_WALK ].PlaySpeed = 0.25f;
        // // 			b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.25f;
        // // 			b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.25f;
        // // 			b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.25f;
        // 			b->Actions[MONSTER01_DIE  ].PlaySpeed = 0.25f;
        // 		}
        break;
    case MONSTER_SAPIDUO:
        OpenMonsterModel(MONSTER_MODEL_SAPIDUO);
        pCharacter = CreateCharacter(Key, MODEL_SAPIDUO, PosX, PosY);
        pCharacter->Object.Scale = 1.0f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        // 		{
        // 	  		BMD *b = &Models[MODEL_MONSTER01+137];
        // // 			b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        // // 			b->Actions[MONSTER01_STOP2].PlaySpeed = 0.25f;
        // // 			b->Actions[MONSTER01_WALK ].PlaySpeed = 0.25f;
        // // 			b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.25f;
        // // 			b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.25f;
        // // 			b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.25f;
        // 			b->Actions[MONSTER01_DIE  ].PlaySpeed = 0.25f;
        // 		}
        break;
    case MONSTER_SAPITRES:
        OpenMonsterModel(MONSTER_MODEL_SAPITRES);
        pCharacter = CreateCharacter(Key, MODEL_SAPITRES, PosX, PosY);
        pCharacter->Object.Scale = 1.0f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        // 		{
        // 	  		BMD *b = &Models[MODEL_MONSTER01+138];
        // // 			b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        // // 			b->Actions[MONSTER01_STOP2].PlaySpeed = 0.25f;
        // // 			b->Actions[MONSTER01_WALK ].PlaySpeed = 0.25f;
        // // 			b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.25f;
        // // 			b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.25f;
        // // 			b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.25f;
        // 			b->Actions[MONSTER01_DIE  ].PlaySpeed = 0.25f;
        // 		}
        break;
    case MONSTER_SHADOW_PAWN:
        OpenMonsterModel(MONSTER_MODEL_SHADOW_PAWN);
        pCharacter = CreateCharacter(Key, MODEL_SHADOW_PAWN, PosX, PosY);
        pCharacter->Object.Scale = 1.1f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        pCharacter->Object.LifeTime = 100;
        break;
    case MONSTER_SHADOW_KNIGHT:
        OpenMonsterModel(MONSTER_MODEL_SHADOW_KNIGHT);
        pCharacter = CreateCharacter(Key, MODEL_SHADOW_KNIGHT, PosX, PosY);
        pCharacter->Object.Scale = 1.1f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        pCharacter->Object.LifeTime = 100;
        break;
    case MONSTER_SHADOW_LOOK:
        OpenMonsterModel(MONSTER_MODEL_SHADOW_LOOK);
        pCharacter = CreateCharacter(Key, MODEL_SHADOW_LOOK, PosX, PosY);
        pCharacter->Object.Scale = 1.3f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        pCharacter->Object.LifeTime = 100;
        break;
    case MONSTER_THUNDER_NAPIN:
        OpenMonsterModel(MONSTER_MODEL_NAPIN);
        pCharacter = CreateCharacter(Key, MODEL_NAPIN, PosX, PosY);
        pCharacter->Object.Scale = 0.95f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        break;
    case MONSTER_GHOST_NAPIN:
        OpenMonsterModel(MONSTER_MODEL_GHOST_NAPIN);
        pCharacter = CreateCharacter(Key, MODEL_GHOST_NAPIN, PosX, PosY);
        pCharacter->Object.Scale = 0.95f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        break;
    case MONSTER_BLAZE_NAPIN:
        OpenMonsterModel(MONSTER_MODEL_BLAZE_NAPIN);
        pCharacter = CreateCharacter(Key, MODEL_BLAZE_NAPIN, PosX, PosY);
        pCharacter->Object.Scale = 0.95f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        break;
    case MONSTER_MEDUSA:
        OpenMonsterModel(MONSTER_MODEL_MEDUSA);
        pCharacter = CreateCharacter(Key, MODEL_MEDUSA, PosX, PosY);
        pCharacter->Object.Scale = 1.5f;
        pCharacter->Object.LifeTime = 100;
        break;
    case MONSTER_SAPI_QUEEN:
    case MONSTER_SAPI_QUEEN2:
        OpenMonsterModel(MONSTER_MODEL_SAPI_QUEEN);
        pCharacter = CreateCharacter(Key, MODEL_SAPI_QUEEN, PosX, PosY);
        pCharacter->Object.Scale = 1.5f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        break;
    case MONSTER_ICE_NAPIN:
        OpenMonsterModel(MONSTER_MODEL_ICE_NAPIN);
        pCharacter = CreateCharacter(Key, MODEL_ICE_NAPIN, PosX, PosY);
        pCharacter->Object.Scale = 1.1f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        break;
    case MONSTER_SHADOW_MASTER:
        OpenMonsterModel(MONSTER_MODEL_SHADOW_MASTER);
        pCharacter = CreateCharacter(Key, MODEL_SHADOW_MASTER, PosX, PosY);
        pCharacter->Object.Scale = 1.56f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        pCharacter->Object.LifeTime = 100;
        break;
    }

    return pCharacter;
}

bool GMSwampOfQuiet::SetCurrentActionMonster(CHARACTER *pCharacter, OBJECT *pObject)
{
    if (!IsCurrentMap())
        return false;

    switch (pObject->Type)
    {
    case MODEL_MEDUSA: {
        switch (pCharacter->Skill)
        {
        case AT_SKILL_DECAY: {
            SetAction(pObject, MONSTER01_ATTACK2);
            pCharacter->MonsterSkill = -1;
        }
        break;
        case AT_SKILL_CHAOTIC_DISEIER: {
            SetAction(pObject, MONSTER01_ATTACK2);
            pCharacter->MonsterSkill = -1;
        }
        break;
        case AT_SKILL_GIGANTIC_STORM: {
            SetAction(pObject, MONSTER01_ATTACK3);
            pCharacter->MonsterSkill = -1;
        }
        break;
        case AT_SKILL_EVIL_SPIRIT: {
            SetAction(pObject, MONSTER01_ATTACK1);
            pCharacter->MonsterSkill = -1;
        }
        break;
        default: {
            SetAction(pObject, MONSTER01_ATTACK1);
            pCharacter->MonsterSkill = -1;
        }
        break;
        }
    }
        return true;
    case MODEL_SAPI_QUEEN:
    case MODEL_WOLF_STATUS: {
        if (pCharacter->MonsterSkill == ATMON_SKILL_EX_SAPIQUEEN_ATTACKSKILL)
        {
            SetAction(pObject, MONSTER01_ATTACK2);
            pCharacter->MonsterSkill = -1;
        }
        else
        {
            SetAction(pObject, MONSTER01_ATTACK1);
            pCharacter->MonsterSkill = -1;
        }
        return true;
    }
        return true;
    case MODEL_ICE_NAPIN: {
        if (pCharacter->MonsterSkill == ATMON_SKILL_EX_ICENAPIN_ATTACKSKILL)
        {
            SetAction(pObject, MONSTER01_ATTACK2);
            pCharacter->MonsterSkill = -1;
        }
        else
        {
            SetAction(pObject, MONSTER01_ATTACK1);
            pCharacter->MonsterSkill = -1;
        }
        return true;
    }
        return true;
    case MODEL_SHADOW_MASTER: {
        if (pCharacter->MonsterSkill == ATMON_SKILL_EX_SHADOWMASTER_ATTACKSKILL)
        {
            SetAction(pObject, MONSTER01_ATTACK2);
            pCharacter->MonsterSkill = -1;
        }
        else
        {
            SetAction(pObject, MONSTER01_ATTACK1);
            pCharacter->MonsterSkill = -1;
        }
        return true;
    }
        return true;
    default:
        return false;
    }
    return false;
}

void GMSwampOfQuiet::AdvanceMonsterState(CHARACTER &character, BMD &model)
{
    auto &object = character.Object;

    if (!IsCurrentMap() || object.CurrentAction != MONSTER01_DIE)
        return;
    if (object.Type == MODEL_MEDUSA)
        object.m_bRenderShadow = false;
    if (object.Type != MODEL_SHADOW_PAWN && object.Type != MODEL_SHADOW_KNIGHT &&
        object.Type != MODEL_SHADOW_LOOK && object.Type != MODEL_SHADOW_MASTER)
        return;
    if (object.LifeTime == 100)
    {
        object.LifeTime = 90;
        object.m_bRenderShadow = false;
    }
}

extern int GetMp3PlayPosition();

CGMDoppelGanger1Ptr CGMDoppelGanger1::Make(SessionKeeper &keeper)
{
    CGMDoppelGanger1Ptr doppelganger(new CGMDoppelGanger1(keeper));
    doppelganger->Init();
    return doppelganger;
}

CGMDoppelGanger1::CGMDoppelGanger1(SessionKeeper &keeper)
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject())
{
    m_bIsMP3Playing = FALSE;
}

CGMDoppelGanger1::~CGMDoppelGanger1()
{
    Destroy();
}

void CGMDoppelGanger1::Init()
{
}

void CGMDoppelGanger1::Destroy()
{
}

bool CGMDoppelGanger1::CreateObject(OBJECT *o)
{
    if (o->Type == 98 || o->Type == 19 || o->Type == 20 || o->Type == 31 || o->Type == 33)
        o->m_bRenderAfterCharacter = true;

    // 	switch(o->Type)
    // 	{
    // 	}

    return false;
}

CHARACTER *CGMDoppelGanger1::CreateMonster(int iType, int PosX, int PosY, int Key)
{
    CHARACTER *pCharacter = NULL;

    // 	iType = 529+WorldRandom()%10;
    // 	iType = 533;

    switch (iType)
    {
    case MONSTER_TERRIBLE_BUTCHER:
        OpenMonsterModel(MONSTER_MODEL_TERRIBLE_BUTCHER);
        pCharacter = CreateCharacter(Key, MODEL_TERRIBLE_BUTCHER, PosX, PosY);
        pCharacter->Object.Scale = 1.0f;
        break;
    case MONSTER_MAD_BUTCHER:
        OpenMonsterModel(MONSTER_MODEL_MAD_BUTCHER);
        pCharacter = CreateCharacter(Key, MODEL_MAD_BUTCHER, PosX, PosY);
        pCharacter->Object.Scale = 0.8f;
        break;
    case MONSTER_ICE_WALKER2:
        OpenMonsterModel(MONSTER_MODEL_ICE_WALKER);
        pCharacter = CreateCharacter(Key, MODEL_ICE_WALKER, PosX, PosY);
        pCharacter->Object.Scale = 1.2f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        break;
    case MONSTER_LARVA2:
        OpenMonsterModel(MONSTER_MODEL_LARVA);
        pCharacter = CreateCharacter(Key, MODEL_LARVA, PosX, PosY);
        pCharacter->Object.Scale = 0.6f;
        break;
    case MONSTER_DOPPELGANGER:
        OpenMonsterModel(MONSTER_MODEL_DOPPELGANGER);
        pCharacter = CreateCharacter(Key, MODEL_DOPPELGANGER, PosX, PosY);
        pCharacter->Object.Scale = 1.1f;
        pCharacter->Object.m_bRenderShadow = false;
        break;
    case MONSTER_DOPPELGANGER_ELF:
        pCharacter = CreateCharacter(Key, MODEL_PLAYER, PosX, PosY);
        pCharacter->Object.Scale = 1.0f;
        pCharacter->Class = CLASS_ELF;
        pCharacter->BodyPart[BODYPART_HELM].Type = MODEL_SPIRIT_HELM;
        pCharacter->BodyPart[BODYPART_ARMOR].Type = MODEL_SPIRIT_ARMOR;
        pCharacter->BodyPart[BODYPART_PANTS].Type = MODEL_SPIRIT_PANTS;
        pCharacter->BodyPart[BODYPART_GLOVES].Type = MODEL_SPIRIT_GLOVES;
        pCharacter->BodyPart[BODYPART_BOOTS].Type = MODEL_SPIRIT_BOOTS;
        pCharacter->Weapon[0].Type = MODEL_ARROWS;
        pCharacter->Weapon[1].Type = MODEL_CELESTIAL_BOW;
        break;
    case MONSTER_DOPPELGANGER_KNIGHT:
        pCharacter = CreateCharacter(Key, MODEL_PLAYER, PosX, PosY);
        pCharacter->Object.Scale = 1.0f;
        pCharacter->Class = CLASS_KNIGHT;
        pCharacter->BodyPart[BODYPART_HELM].Type = MODEL_DRAGON_HELM;
        pCharacter->BodyPart[BODYPART_ARMOR].Type = MODEL_DRAGON_ARMOR;
        pCharacter->BodyPart[BODYPART_PANTS].Type = MODEL_DRAGON_PANTS;
        pCharacter->BodyPart[BODYPART_GLOVES].Type = MODEL_DRAGON_GLOVES;
        pCharacter->BodyPart[BODYPART_BOOTS].Type = MODEL_DRAGON_BOOTS;
        pCharacter->Weapon[0].Type = MODEL_DOUBLE_BLADE;
        pCharacter->Weapon[1].Type = -1;
        break;
    case MONSTER_DOPPELGANGER_WIZARD:
        pCharacter = CreateCharacter(Key, MODEL_PLAYER, PosX, PosY);
        pCharacter->Object.Scale = 1.0f;
        pCharacter->Class = CLASS_WIZARD;
        pCharacter->BodyPart[BODYPART_HELM].Type = MODEL_GRAND_SOUL_HELM;
        pCharacter->BodyPart[BODYPART_ARMOR].Type = MODEL_GRAND_SOUL_ARMOR;
        pCharacter->BodyPart[BODYPART_PANTS].Type = MODEL_GRAND_SOUL_PANTS;
        pCharacter->BodyPart[BODYPART_GLOVES].Type = MODEL_GRAND_SOUL_GLOVES;
        pCharacter->BodyPart[BODYPART_BOOTS].Type = MODEL_GRAND_SOUL_BOOTS;
        pCharacter->Weapon[0].Type = MODEL_DRAGON_SOUL_STAFF;
        pCharacter->Weapon[1].Type = MODEL_GRAND_SOUL_SHIELD;
        break;
    case MONSTER_DOPPELGANGER_MG:
        pCharacter = CreateCharacter(Key, MODEL_PLAYER, PosX, PosY);
        pCharacter->Object.Scale = 1.0f;
        pCharacter->Class = CLASS_DARK;
        pCharacter->BodyPart[BODYPART_HELM].Type = MODEL_BODY_HELM + 15;
        pCharacter->BodyPart[BODYPART_ARMOR].Type = MODEL_STORM_CROW_ARMOR;
        pCharacter->BodyPart[BODYPART_PANTS].Type = MODEL_STORM_CROW_PANTS;
        pCharacter->BodyPart[BODYPART_GLOVES].Type = MODEL_STORM_CROW_GLOVES;
        pCharacter->BodyPart[BODYPART_BOOTS].Type = MODEL_STORM_CROW_BOOTS;
        pCharacter->Weapon[0].Type = MODEL_RUNE_BLADE;
        pCharacter->Weapon[1].Type = -1;
        break;
    case MONSTER_DOPPELGANGER_DL:
        pCharacter = CreateCharacter(Key, MODEL_PLAYER, PosX, PosY);
        pCharacter->Object.Scale = 1.0f;
        pCharacter->Class = CLASS_DARK_LORD;
        pCharacter->BodyPart[BODYPART_HELM].Type = MODEL_DARK_STEEL_MASK;
        pCharacter->BodyPart[BODYPART_ARMOR].Type = MODEL_DARK_STEEL_ARMOR;
        pCharacter->BodyPart[BODYPART_PANTS].Type = MODEL_DARK_STEEL_PANTS;
        pCharacter->BodyPart[BODYPART_GLOVES].Type = MODEL_DARK_STEEL_GLOVES;
        pCharacter->BodyPart[BODYPART_BOOTS].Type = MODEL_DARK_STEEL_BOOTS;
        pCharacter->Weapon[0].Type = MODEL_GREAT_SCEPTER;
        pCharacter->Weapon[1].Type = MODEL_SPIKED_SHIELD;
        pCharacter->Helper.Type = MODEL_DARK_HORSE_ITEM;
        CreateMount(MODEL_DARK_HORSE, pCharacter->Object.Position, &pCharacter->Object, 1);
        break;
    case MONSTER_DOPPELGANGER_SUM:
        pCharacter = CreateCharacter(Key, MODEL_PLAYER, PosX, PosY);
        pCharacter->Object.Scale = 1.0f;
        pCharacter->Class = CLASS_SUMMONER;
        pCharacter->BodyPart[BODYPART_HELM].Type = MODEL_RED_WING_HELM;
        pCharacter->BodyPart[BODYPART_ARMOR].Type = MODEL_RED_WING_ARMOR;
        pCharacter->BodyPart[BODYPART_PANTS].Type = MODEL_RED_WING_PANTS;
        pCharacter->BodyPart[BODYPART_GLOVES].Type = MODEL_RED_WING_GLOVES;
        pCharacter->BodyPart[BODYPART_BOOTS].Type = MODEL_RED_WING_BOOTS;
        pCharacter->Weapon[0].Type = MODEL_DEMONIC_STICK;
        //pCharacter->Weapon[1].Type = MODEL_STAFF+22;
        break;
    }

    return pCharacter;
}

bool CGMDoppelGanger1::MoveObject(OBJECT *o)
{
    if (IsDoppelGanger1() == false)
        return false;

    switch (o->Type)
    {
    case 22: {
        o->BlendMeshLight = (float)sinf(WorldTime * 0.001f) + 1.0f;
    }
        return true;
    case 70:
    case 80:
    case 99:
    case 101: {
        o->HiddenMesh = -2;
    }
        return true;
    }

    return false;
}

bool CGMDoppelGanger1::IsDoppelGanger1()
{
    if (gMapManager.ContextMap() == WD_65DOPPLEGANGER1)
    {
        return true;
    }

    return false;
}

CGMAidaPtr CGMAida::Make(SessionKeeper &keeper)
{
    return CGMAidaPtr(new CGMAida(keeper));
}

CGMAida::CGMAida(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject())
{
}

CGMAida::~CGMAida() = default;

bool CGMAida::IsInAida()
{
    return (gMapManager.ContextMap() == WD_33AIDA ||
            gMapManager.ContextMap() == WD_54CHARACTERSCENE)
               ? true
               : false;
}

bool CGMAida::IsInAidaSection2(const vec3_t Position)
{
    if (Position[0] > 5449.f && Position[0] < 17822.f && Position[1] > 6784.f &&
        Position[1] < 22419.f)
        return true;
    return false;
}

bool CGMAida::CreateAidaObject(OBJECT *pObject)
{
    if (!IsInAida())
        return false;

    return true;
}

CHARACTER *CGMAida::CreateAidaMonster(int iType, int PosX, int PosY, int Key)
{
    CHARACTER *pCharacter = NULL;
    switch (iType)
    {
    case MONSTER_WITCH_QUEEN: {
        OpenMonsterModel(MONSTER_MODEL_WITCH_QUEEN);
        pCharacter = CreateCharacter(Key, MODEL_WITCH_QUEEN, PosX, PosY);
        pCharacter->Object.Scale = 1.4f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"Monster100_Footstepst", CharacterSocket::Monster100_Footstepst);
        RegisterBone(pCharacter, L"Monster100_L_Hand", CharacterSocket::Monster100_L_Hand);
        RegisterBone(pCharacter, L"Monster100_R_Hand", CharacterSocket::Monster100_R_Hand);
        RegisterBone(pCharacter, L"Monster100_Pelvis", CharacterSocket::Monster100_Pelvis);
        RegisterBone(pCharacter, L"Monster100_Head", CharacterSocket::Monster100_Head);
        RegisterBone(pCharacter, L"Monster100_z02", CharacterSocket::Monster100_z02);
        RegisterBone(pCharacter, L"Monster100_z03", CharacterSocket::Monster100_z03);
        RegisterBone(pCharacter, L"Monster100_z04", CharacterSocket::Monster100_z04);
        RegisterBone(pCharacter, L"Monster100_z05", CharacterSocket::Monster100_z05);
    }
    break;
    case MONSTER_BLUE_GOLEM: {
        OpenMonsterModel(MONSTER_MODEL_GOLDEN_STONE_GOLEM);
        pCharacter = CreateCharacter(Key, MODEL_GOLDEN_STONE_GOLEM, PosX, PosY);
        pCharacter->Object.Scale = 1.35f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"Monster101_L_Arm", CharacterSocket::Monster101_L_Arm);
        RegisterBone(pCharacter, L"Monster101_R_Arm", CharacterSocket::Monster101_R_Arm);
        RegisterBone(pCharacter, L"Monster101_Head", CharacterSocket::Monster101_Head);
    }
    break;
    case MONSTER_DEATH_RIDER: {
        OpenMonsterModel(MONSTER_MODEL_DEATH_RIDER);
        pCharacter = CreateCharacter(Key, MODEL_DEATH_RIDER, PosX, PosY);
        pCharacter->Object.Scale = 1.1f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"Monster102_Footstepst", CharacterSocket::Monster102_Footstepst);
        RegisterBone(pCharacter, L"Monster102_Head", CharacterSocket::Monster102_Head);
    }
    break;
    case MONSTER_FOREST_ORC: {
        OpenMonsterModel(MONSTER_MODEL_FOREST_ORC);
        pCharacter = CreateCharacter(Key, MODEL_FOREST_ORC, PosX, PosY);
        pCharacter->Object.Scale = 1.15f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
    }
    break;
    case MONSTER_DEATH_TREE: {
        OpenMonsterModel(MONSTER_MODEL_DEATH_TREE);
        pCharacter = CreateCharacter(Key, MODEL_DEATH_TREE, PosX, PosY);
        pCharacter->Object.Scale = 1.2f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"Monster104_Horn0", CharacterSocket::Monster104_Horn0);
        RegisterBone(pCharacter, L"Monster104_Horn1", CharacterSocket::Monster104_Horn1);
        RegisterBone(pCharacter, L"Monster104_Horn2", CharacterSocket::Monster104_Horn2);
        RegisterBone(pCharacter, L"Monster104_Horn3", CharacterSocket::Monster104_Horn3);
        RegisterBone(pCharacter, L"Monster104_Horn4", CharacterSocket::Monster104_Horn4);
        RegisterBone(pCharacter, L"Monster104_Horn5", CharacterSocket::Monster104_Horn5);
        RegisterBone(pCharacter, L"Monster104_Footsteps", CharacterSocket::Monster104_Footsteps);
    }
    break;
    case MONSTER_HELL_MAINE: {
        OpenMonsterModel(MONSTER_MODEL_HELL_MAINE);
        pCharacter = CreateCharacter(Key, MODEL_HELL_MAINE, PosX, PosY);
        pCharacter->Object.Scale = 1.8f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"Monster105_R_Eye", CharacterSocket::Monster105_R_Eye);
        RegisterBone(pCharacter, L"Monster105_L_Eye", CharacterSocket::Monster105_L_Eye);
        RegisterBone(pCharacter, L"Monster105_L_Arm00", CharacterSocket::Monster105_L_Arm00);
        RegisterBone(pCharacter, L"Monster105_L_Arm01", CharacterSocket::Monster105_L_Arm01);
        RegisterBone(pCharacter, L"Monster105_L_Arm02", CharacterSocket::Monster105_L_Arm02);
        RegisterBone(pCharacter, L"Monster105_L_Hand", CharacterSocket::Monster105_L_Hand);
        RegisterBone(pCharacter, L"Monster105_R_Hand", CharacterSocket::Monster105_R_Hand);
        //			RegisterBone(pCharacter, L"Monster105_Footsteps", CharacterSocket::Monster105_Footsteps);
    }
    break;
    case MONSTER_BLOODY_ORC: {
        OpenMonsterModel(MONSTER_MODEL_BLOODY_ORC);
        pCharacter = CreateCharacter(Key, MODEL_BLOODY_ORC, PosX, PosY);
        //pCharacter->Object.Scale = 1.15f;
        pCharacter->Object.Scale = 1.35f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
    }
    break;
    case MONSTER_BLOODY_DEATH_RIDER: {
        OpenMonsterModel(MONSTER_MODEL_BLOODY_DEATH_RIDER);
        pCharacter = CreateCharacter(Key, MODEL_BLOODY_DEATH_RIDER, PosX, PosY);
        //pCharacter->Object.Scale = 1.1f;
        pCharacter->Object.Scale = 1.2f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"Monster102_Footstepst", CharacterSocket::Monster102_Footstepst);
        RegisterBone(pCharacter, L"Monster102_Head", CharacterSocket::Monster102_Head);
    }
    break;
    case MONSTER_BLOODY_GOLEM: {
        OpenMonsterModel(MONSTER_MODEL_BLOODY_GOLEM);
        pCharacter = CreateCharacter(Key, MODEL_BLOODY_GOLEM, PosX, PosY);
        //pCharacter->Object.Scale = 1.35f;
        pCharacter->Object.Scale = 1.40f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"Monster101_L_Arm", CharacterSocket::Monster101_L_Arm);
        RegisterBone(pCharacter, L"Monster101_R_Arm", CharacterSocket::Monster101_R_Arm);
        RegisterBone(pCharacter, L"Monster101_Head", CharacterSocket::Monster101_Head);
    }
    break;
    case MONSTER_BLOODY_WITCH_QUEEN: {
        OpenMonsterModel(MONSTER_MODEL_BLOODY_WITCH_QUEEN);
        pCharacter = CreateCharacter(Key, MODEL_BLOODY_WITCH_QUEEN, PosX, PosY);
        //pCharacter->Object.Scale = 1.4f;
        pCharacter->Object.Scale = 1.75f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        RegisterBone(pCharacter, L"Monster100_Footstepst", CharacterSocket::Monster100_Footstepst);
        RegisterBone(pCharacter, L"Monster100_L_Hand", CharacterSocket::Monster100_L_Hand);
        RegisterBone(pCharacter, L"Monster100_R_Hand", CharacterSocket::Monster100_R_Hand);
        RegisterBone(pCharacter, L"Monster100_Pelvis", CharacterSocket::Monster100_Pelvis);
        RegisterBone(pCharacter, L"Monster100_Head", CharacterSocket::Monster100_Head);
        RegisterBone(pCharacter, L"Monster100_z02", CharacterSocket::Monster100_z02);
        RegisterBone(pCharacter, L"Monster100_z03", CharacterSocket::Monster100_z03);
        RegisterBone(pCharacter, L"Monster100_z04", CharacterSocket::Monster100_z04);
        RegisterBone(pCharacter, L"Monster100_z05", CharacterSocket::Monster100_z05);
    }
    break;
    }
    return pCharacter;
}

bool SessionLegacyCalls::IsInAida()
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Aida().IsInAida();
}

bool SessionLegacyCalls::IsInAidaSection2(const vec3_t position)
{
    return sessionKeeper_.Gameplay()->TheMapProcess().Aida().IsInAidaSection2(position);
}

bool CGMAida::SetCurrentActionAidaMonster(CHARACTER *pCharacter, OBJECT *pObject)
{
    if (!IsInAida())
        return false;

    switch (pCharacter->MonsterIndex)
    {
    case MONSTER_WITCH_QUEEN:
    case MONSTER_BLUE_GOLEM:
    case MONSTER_HELL_MAINE:
        return CheckMonsterSkill(pCharacter, pObject);
    case MONSTER_BLOODY_ORC:
    case MONSTER_BLOODY_DEATH_RIDER:
        return CheckMonsterSkill(pCharacter, pObject);
    case MONSTER_BLOODY_GOLEM: {
        if (pCharacter->MonsterSkill == ATMON_SKILL_EX_BLOODYGOLUEM_ATTACKSKILL)
        {
            SetAction(pObject, MONSTER01_ATTACK2);
            pCharacter->MonsterSkill = -1;
        }
        else
        {
            SetAction(pObject, MONSTER01_ATTACK1);
            pCharacter->MonsterSkill = -1;
        }
        return true;
    }
        return true;
    case MONSTER_BLOODY_WITCH_QUEEN: {
        if (pCharacter->MonsterSkill == ATMON_SKILL_EX_BLOODYWITCHQUEEN_ATTACKSKILL)
        {
            SetAction(pObject, MONSTER01_ATTACK2);
            pCharacter->MonsterSkill = -1;
        }
        else
        {
            SetAction(pObject, MONSTER01_ATTACK1);
            pCharacter->MonsterSkill = -1;
        }
        return true;
    }
        return true;
    }
    return false;
}

bool CGMAida::CreateObject(OBJECT *object)
{
    return CreateAidaObject(object);
}

bool CGMAida::SetCurrentActionMonster(CHARACTER *character, OBJECT *object)
{
    return SetCurrentActionAidaMonster(character, object);
}

void CGMAida::InstallBehavior()
{
    if (!IsInAida() || gMapManager.ContextMap() != WD_33AIDA)
        return;
    LoadWaveFile(SOUND_AIDA_AMBIENT, L"Data\\Sound\\w34\\aida_ambi.wav", 1, true);
    LoadWaveFile(SOUND_AIDA_BLUEGOLEM_MOVE1, L"Data\\Sound\\w34\\bg_idle1.wav", 1);
    LoadWaveFile(SOUND_AIDA_BLUEGOLEM_MOVE2, L"Data\\Sound\\w34\\bg_idle2.wav", 1);
    LoadWaveFile(SOUND_AIDA_BLUEGOLEM_ATTACK1, L"Data\\Sound\\w34\\bg_attack1.wav", 1);
    LoadWaveFile(SOUND_AIDA_BLUEGOLEM_ATTACK2, L"Data\\Sound\\w34\\bg_attack2.wav", 1);
    LoadWaveFile(SOUND_AIDA_BLUEGOLEM_DIE, L"Data\\Sound\\w34\\bg_death.wav", 1);
    LoadWaveFile(SOUND_AIDA_DEATHRAIDER_MOVE1, L"Data\\Sound\\w34\\dr_idle1.wav", 1);
    LoadWaveFile(SOUND_AIDA_DEATHRAIDER_MOVE2, L"Data\\Sound\\w34\\dr_idle2.wav", 1);
    LoadWaveFile(SOUND_AIDA_DEATHRAIDER_ATTACK1, L"Data\\Sound\\w34\\dr_attack1.wav", 1);
    LoadWaveFile(SOUND_AIDA_DEATHRAIDER_ATTACK2, L"Data\\Sound\\w34\\dr_attack2.wav", 1);
    LoadWaveFile(SOUND_AIDA_DEATHRAIDER_DIE, L"Data\\Sound\\w34\\dr_death.wav", 1);
    LoadWaveFile(SOUND_AIDA_DEATHTREE_MOVE1, L"Data\\Sound\\w34\\dt_idle1.wav", 1);
    LoadWaveFile(SOUND_AIDA_DEATHTREE_MOVE2, L"Data\\Sound\\w34\\dt_idle2.wav", 1);
    LoadWaveFile(SOUND_AIDA_DEATHTREE_ATTACK1, L"Data\\Sound\\w34\\dt_attack1.wav", 1);
    LoadWaveFile(SOUND_AIDA_DEATHTREE_ATTACK2, L"Data\\Sound\\w34\\dt_attack2.wav", 1);
    LoadWaveFile(SOUND_AIDA_DEATHTREE_DIE, L"Data\\Sound\\w34\\dt_death.wav", 1);
    LoadWaveFile(SOUND_AIDA_FORESTORC_MOVE1, L"Data\\Sound\\w34\\fo_idle1.wav", 1);
    LoadWaveFile(SOUND_AIDA_FORESTORC_MOVE2, L"Data\\Sound\\w34\\fo_idle2.wav", 1);
    LoadWaveFile(SOUND_AIDA_FORESTORC_ATTACK1, L"Data\\Sound\\w34\\fo_attack1.wav", 1);
    LoadWaveFile(SOUND_AIDA_FORESTORC_ATTACK2, L"Data\\Sound\\w34\\fo_attack2.wav", 1);
    LoadWaveFile(SOUND_AIDA_FORESTORC_DIE, L"Data\\Sound\\w34\\fo_death.wav", 1);
    LoadWaveFile(SOUND_AIDA_HELL_MOVE1, L"Data\\Sound\\w34\\hm_idle1.wav", 1);
    LoadWaveFile(SOUND_AIDA_HELL_MOVE2, L"Data\\Sound\\w34\\hm_idle2.wav", 1);
    LoadWaveFile(SOUND_AIDA_HELL_ATTACK1, L"Data\\Sound\\w34\\hm_attack1.wav", 1);
    LoadWaveFile(SOUND_AIDA_HELL_ATTACK2, L"Data\\Sound\\w34\\hm_firelay.wav", 1);
    LoadWaveFile(SOUND_AIDA_HELL_ATTACK3, L"Data\\Sound\\w34\\hm_bloodywind.wav", 1);
    LoadWaveFile(SOUND_AIDA_HELL_DIE, L"Data\\Sound\\w34\\hm_death.wav", 1);
    LoadWaveFile(SOUND_AIDA_WITCHQUEEN_MOVE1, L"Data\\Sound\\w34\\wq_idle1.wav", 1);
    LoadWaveFile(SOUND_AIDA_WITCHQUEEN_MOVE2, L"Data\\Sound\\w34\\wq_idle2.wav", 1);
    LoadWaveFile(SOUND_AIDA_WITCHQUEEN_ATTACK1, L"Data\\Sound\\w34\\wq_attack1.wav", 1);
    LoadWaveFile(SOUND_AIDA_WITCHQUEEN_ATTACK2, L"Data\\Sound\\w34\\wq_attack2.wav", 1);
    LoadWaveFile(SOUND_AIDA_WITCHQUEEN_DIE, L"Data\\Sound\\w34\\wq_death.wav", 1);
    LoadWaveFile(SOUND_CHAOS_THUNDER01, L"Data\\Sound\\eElec1.wav", 1);
    LoadWaveFile(SOUND_CHAOS_THUNDER02, L"Data\\Sound\\eElec2.wav", 1);
}

void CGMAida::UpdateMusic()
{
    if (gMapManager.ContextMap() == WD_33AIDA)
        PlayMp3(MUSIC_BC_ADIA);
    else
        StopMp3(MUSIC_BC_ADIA);
}

bool CGMAida::AllowsMusic(const char *track) const
{
    return std::strcmp(track, MUSIC_BC_ADIA) == 0;
}

GMEmpireGuardian1Ptr GMEmpireGuardian1::Make(SessionKeeper &keeper)
{
    GMEmpireGuardian1Ptr empire(new GMEmpireGuardian1(keeper));
    empire->Init();
    return empire;
}

GMEmpireGuardian1::GMEmpireGuardian1(SessionKeeper &keeper)
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject())
{
    m_iWeather = (int)WEATHER_SUN;
}

GMEmpireGuardian1::~GMEmpireGuardian1()
{
    Destroy();
}

void GMEmpireGuardian1::Init()
{
}

void GMEmpireGuardian1::Destroy()
{
}

bool GMEmpireGuardian1::CreateObject(OBJECT *o)
{
    if (o->Type == 0 || o->Type == 1 || o->Type == 3 || o->Type == 44 || o->Type == 81)
        o->m_bRenderAfterCharacter = true;

    switch (o->Type)
    {
    case 129:
    case 130:
    case 131:
    case 132: {
        o->Angle[2] = (float)((int)o->Angle[2] % 360);
        VectorCopy(o->Angle, o->HeadAngle);
        VectorCopy(o->Position, o->HeadTargetAngle);
    }
        return true;

    case 115:
    case 117: {
        o->SubType = 100;
    }
        return true;
    }

    return false;
}

CHARACTER *GMEmpireGuardian1::CreateMonster(int iType, int PosX, int PosY, int Key)
{
    CHARACTER *pCharacter = NULL;

    switch (iType)
    {
    case MONSTER_RAYMOND: {
        OpenMonsterModel(MONSTER_MODEL_RAYMOND);
        pCharacter = CreateCharacter(Key, MODEL_RAYMOND, PosX, PosY);

        pCharacter->Object.Scale = 1.45f;

        m_bCurrentIsRage_Raymond = false;
    }
    break;
    case MONSTER_LUCAS: {
        OpenMonsterModel(MONSTER_MODEL_LUCAS);
        pCharacter = CreateCharacter(Key, MODEL_LUCAS, PosX, PosY);

        pCharacter->Object.Scale = 1.25f;

        m_bCurrentIsRage_Ercanne = false;
    }
    break;
    case MONSTER_FRED: {
        OpenMonsterModel(MONSTER_MODEL_FRED);
        pCharacter = CreateCharacter(Key, MODEL_FRED, PosX, PosY);
        pCharacter->Object.Scale = 1.55f;

        RegisterBone(pCharacter, L"node_eyes01", CharacterSocket::node_eyes01);
        RegisterBone(pCharacter, L"node_eyes02", CharacterSocket::node_eyes02);
        RegisterBone(pCharacter, L"node_blade01", CharacterSocket::node_blade01);
        RegisterBone(pCharacter, L"node_blade05", CharacterSocket::node_blade05);
        RegisterBone(pCharacter, L"node_blade04", CharacterSocket::node_blade04);
        RegisterBone(pCharacter, L"node_blade02", CharacterSocket::node_blade02);
        //			RegisterBone(pCharacter, L"node_blade03" ,75);

        OBJECT *o = &pCharacter->Object;
        BMD *b = &Models[o->Type];

        MoveEye(o, b, 14, 15, 71, 72, 73, 74);
        vec3_t vColor = {0.7f, 0.7f, 1.0f};
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 24, o, 10.f, -1, 0, 0,
                    -1, vColor);
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 25, o, 10.f, -1, 0, 0,
                    -1, vColor);
        Vector(0.7f, 0.7f, 1.0f, vColor);
        float Sca = 100.f;
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 54, o, Sca, 0, 0, 0,
                    -1, vColor);
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 54, o, Sca, 1, 0, 0,
                    -1, vColor);
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 54, o, Sca, 2, 0, 0,
                    -1, vColor);
        CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 54, o, Sca, 3, 0, 0,
                    -1, vColor);

        m_bCurrentIsRage_Daesuler = false;
    }
    break;
    case MONSTER_DEVIL_LORD: {
        OpenMonsterModel(MONSTER_MODEL_DEVIL_LORD);
        pCharacter = CreateCharacter(Key, MODEL_DEVIL_LORD, PosX, PosY);
        pCharacter->Object.Scale = 1.35f;

        m_bCurrentIsRage_Gallia = false;
    }
    break;
    case MONSTER_QUARTER_MASTER: {
        OpenMonsterModel(MONSTER_MODEL_QUARTER_MASTER);
        pCharacter = CreateCharacter(Key, MODEL_QUARTER_MASTER, PosX, PosY);
        pCharacter->Object.Scale = 1.27f;
    }
    break;
    case MONSTER_COMBAT_INSTRUCTOR: {
        OpenMonsterModel(MONSTER_MODEL_COMBAT_INSTRUCTOR);
        pCharacter = CreateCharacter(Key, MODEL_COMBAT_INSTRUCTOR, PosX, PosY);
        pCharacter->Object.Scale = 1.25f;
    }
    break;
    case MONSTER_DEFENDER: {
        OpenMonsterModel(MONSTER_MODEL_DEFENDER);
        pCharacter = CreateCharacter(Key, MODEL_DEFENDER, PosX, PosY);
        pCharacter->Object.Scale = 1.2f;

        Vector(0.0f, 0.0f, 0.0f, pCharacter->Object.EyeRight3);
        Vector(0.0f, 0.0f, 0.0f, pCharacter->Object.EyeLeft3);
    }
    break;
    case MONSTER_FORSAKER: {
        OpenMonsterModel(MONSTER_MODEL_FORSAKER);
        pCharacter = CreateCharacter(Key, MODEL_FORSAKER, PosX, PosY);
        pCharacter->Object.Scale = 0.9f;
    }
    break;
    case MONSTER_OCELOT_THE_LORD: {
        OpenMonsterModel(MONSTER_MODEL_OCELOT);
        pCharacter = CreateCharacter(Key, MODEL_OCELOT, PosX, PosY);
        pCharacter->Object.Scale = 1.1f;
    }
    break;
    case MONSTER_ERIC_THE_GUARD: {
        OpenMonsterModel(MONSTER_MODEL_ERIC);
        pCharacter = CreateCharacter(Key, MODEL_ERIC, PosX, PosY);
        pCharacter->Object.Scale = 1.1f;
    }
    break;
    case MONSTER_EVIL_GATE: {
        OpenMonsterModel(MONSTER_MODEL_EVIL_GATE);
        pCharacter = CreateCharacter(Key, MODEL_EVIL_GATE, PosX, PosY);
        pCharacter->Object.m_bRenderShadow = false;
        pCharacter->Object.Scale = 1.25f;
    }
    break;
    case MONSTER_LION_GATE: {
        OpenMonsterModel(MONSTER_MODEL_LION_GATE);
        pCharacter = CreateCharacter(Key, MODEL_LION_GATE, PosX, PosY);
        pCharacter->Object.m_bRenderShadow = false;
        pCharacter->Object.LifeTime = 100;
        pCharacter->Object.Scale = 1.25f;
    }
    break;
    case MONSTER_STATUE: {
        OpenMonsterModel(MONSTER_MODEL_STATUE);
        pCharacter = CreateCharacter(Key, MODEL_STATUE, PosX, PosY);
        pCharacter->Object.m_bRenderShadow = false;
        pCharacter->Object.Scale = 0.6f;
        pCharacter->Object.LifeTime = 100;
    }
    break;

    default:
        return pCharacter;
    }

    return pCharacter;
}

bool GMEmpireGuardian1::MoveObject(OBJECT *o)
{
    if (gMapManager.IsEmpireGuardian1() == false)
        return false;

    Alpha(o, FPS_ANIMATION_FACTOR);
    if (o->Alpha < 0.01f)
        return false;

    BMD *b = &Models[o->Type];
    float fSpeed = o->Velocity;
    switch (o->Type)
    {
    case 20: {
        fSpeed *= 2.0f;
    }
    break;

    case 122:
    case 123:
    case 124: {
        fSpeed *= 3.0f;
    }
    break;

    case 128: {
        fSpeed *= 6.0f;
    }
    break;
    }

    b->PlayAnimation(&o->AnimationFrame, &o->PriorAnimationFrame, &o->PriorAction, fSpeed,
                     o->Position, o->Angle);

    switch (o->Type)
    {
    case 20: {
        if (objectAnimationFrame_ - o->AnimationFrame > 10 ||
            objectAnimationFrame_ < o->AnimationFrame)
            objectAnimationFrame_ = o->AnimationFrame;
        else
            o->AnimationFrame = objectAnimationFrame_;
    }
        return true;
    case 64: {
        o->Velocity = 0.64f;
    }
        return true;
    case 79:
    case 80:
    case 82:
    case 83:
    case 84:
    case 85:
    case 86:
    case 129:
    case 130:
    case 131:
    case 132: {
        o->HiddenMesh = -2;
    }
        return true;
    case 81: {
        o->BlendMeshTexCoordV += (0.015f) * FPS_ANIMATION_FACTOR;
    }
        return true;
    case 36: {
        o->Velocity = 0.02f;
    }
        return true;
    }

    return false;
}

bool GMEmpireGuardian1::SetCurrentActionMonster(CHARACTER *c, OBJECT *o)
{
    if (gMapManager.IsEmpireGuardian1() == false && gMapManager.IsEmpireGuardian2() == false &&
        gMapManager.IsEmpireGuardian3() == false && gMapManager.IsEmpireGuardian4() == false)
    {
        return false;
    }

    switch (c->MonsterIndex)
    {
    case MONSTER_RAYMOND: {
        if (m_bCurrentIsRage_Raymond == true)
        {
            SetAction(o, MONSTER01_APEAR);
            c->MonsterSkill = -1;
            return true;
        }

        switch (c->MonsterSkill)
        {
        case 60: {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        break;
        case 52: {
            SetAction(o, MONSTER01_ATTACK3);
            c->MonsterSkill = -1;
        }
        break;
        case ATMON_SKILL_EMPIREGUARDIAN_BERSERKER: {
            SetAction(o, MONSTER01_APEAR);
            c->MonsterSkill = -1;

            m_bCurrentIsRage_Raymond = true;
        }
        break;
        default: {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        break;
        }
    }
        return true;
    case MONSTER_LUCAS: {
        if (m_bCurrentIsRage_Ercanne == true)
        {
            SetAction(o, MONSTER01_APEAR);
            c->MonsterSkill = -1;
            return true;
        }

        switch (c->MonsterSkill)
        {
        case 62: {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        break;
        case 63: {
            SetAction(o, MONSTER01_ATTACK3);
            c->MonsterSkill = -1;
        }
        break;
        case ATMON_SKILL_EMPIREGUARDIAN_BERSERKER: {
            SetAction(o, MONSTER01_APEAR);
            c->MonsterSkill = -1;

            m_bCurrentIsRage_Ercanne = true;
        }
        break;
        default: {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        break;
        }
    }
        return true;
    case MONSTER_FRED: {
        if (m_bCurrentIsRage_Daesuler == true)
        {
            SetAction(o, MONSTER01_APEAR);
            c->MonsterSkill = -1;
            return true;
        }

        switch (c->MonsterSkill)
        {
        case 57: {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        break;
        case 58: {
            SetAction(o, MONSTER01_ATTACK3);
            c->MonsterSkill = -1;
        }
        break;
        case ATMON_SKILL_EMPIREGUARDIAN_BERSERKER: {
            SetAction(o, MONSTER01_APEAR);
            c->MonsterSkill = -1;

            m_bCurrentIsRage_Daesuler = true;
        }
        break;
        default: {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        break;
        }
    }
        return true;
    case MONSTER_DEVIL_LORD: {
        if (m_bCurrentIsRage_Gallia == true)
        {
            SetAction(o, MONSTER01_APEAR);
            c->MonsterSkill = -1;
            return true;
        }

        switch (c->MonsterSkill)
        {
        case 58: {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        break;
        case 60: {
            SetAction(o, MONSTER01_ATTACK3);
            c->MonsterSkill = -1;
        }
        break;
        case ATMON_SKILL_EMPIREGUARDIAN_BERSERKER: {
            SetAction(o, MONSTER01_APEAR);
            c->MonsterSkill = -1;

            m_bCurrentIsRage_Gallia = true;
        }
        break;
        default: {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        break;
        }
    }
        return true;
    case MONSTER_COMBAT_INSTRUCTOR: {
        switch (c->MonsterSkill)
        {
        case 47: {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        break;
        case 49: {
            SetAction(o, MONSTER01_APEAR);
            c->MonsterSkill = -1;
        }
        break;
        default: {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        break;
        }
    }
        return true;
    case MONSTER_DEFENDER: {
        switch (c->MonsterSkill)
        {
        case 44: {
            SetAction(o, MONSTER01_APEAR);
            c->MonsterSkill = -1;
        }
        break;
        case 45: {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        break;
        default: {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        break;
        }
    }
        return true;
    case MONSTER_FORSAKER: {
        switch (c->MonsterSkill)
        {
        case 46: {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        break;
        default: {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        break;
        }
    }
        return true;
    }
    return false;
}

void GMEmpireGuardian1::AdvanceMonsterState(CHARACTER &character, BMD &model)
{
    auto &object = character.Object;
    AdvanceGatePlacement(&object);

    if (!gMapManager.IsEmpireGuardian1() && !gMapManager.IsEmpireGuardian2() &&
        !gMapManager.IsEmpireGuardian3() && !gMapManager.IsEmpireGuardian4())
        return;
    if (object.Type == MODEL_DEFENDER)
    {
        if (object.CurrentAction == MONSTER01_APEAR)
        {
            VectorCopy(object.EyeRight3, object.Angle);
        }
        VectorCopy(object.Angle, object.EyeRight3);
    }
    if (object.Type == MODEL_QUARTER_MASTER && object.AnimationFrame > 2.f &&
        object.AnimationFrame < 3.f)
        object.m_dwTime = 0;
}

void GMEmpireGuardian1::InstallBehavior()
{
    LoadWaveFile(SOUND_EMPIREGUARDIAN_WEATHER_RAIN,
                 L"Data\\Sound\\w69w70w71w72\\ImperialGuardianFort_out1.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_WEATHER_FOG,
                 L"Data\\Sound\\w69w70w71w72\\ImperialGuardianFort_out2.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_WEATHER_STORM,
                 L"Data\\Sound\\w69w70w71w72\\ImperialGuardianFort_out3.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_INDOOR_SOUND,
                 L"Data\\Sound\\w69w70w71w72\\ImperialGuardianFort_in.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_BOSS_GAION_MONSTER_MOVE,
                 L"Data\\Sound\\w69w70w71w72\\GaionKalein_move.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_BOSS_GAION_MONSTER_RAGE,
                 L"Data\\Sound\\w69w70w71w72\\GaionKalein_rage.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_BOSS_GAION_MONSTER_DEATH,
                 L"Data\\Sound\\w69w70w71w72\\GrandWizard_death.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_ATTACK01,
                 L"Data\\Sound\\w69w70w71w72\\Jelint_attack1.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_ATTACK03,
                 L"Data\\Sound\\w69w70w71w72\\Jelint_attack3.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_MOVE01,
                 L"Data\\Sound\\w69w70w71w72\\Jelint_move01.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_MOVE02,
                 L"Data\\Sound\\w69w70w71w72\\Jelint_move02.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_RAGE,
                 L"Data\\Sound\\w69w70w71w72\\Jelint_rage.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_JERINT_MONSTER_DEATH,
                 L"Data\\Sound\\w69w70w71w72\\Jelint_death.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_RAYMOND_MONSTER_ATTACK02,
                 L"Data\\Sound\\w69w70w71w72\\Raymond_attack2.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_RAYMOND_MONSTER_RAGE,
                 L"Data\\Sound\\w69w70w71w72\\Raymond_rage.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_ERCANNE_MONSTER_ATTACK03,
                 L"Data\\Sound\\w69w70w71w72\\Ercanne_attack3.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_1CORP_DEASULER_MONSTER_ATTACK02,
                 L"Data\\Sound\\w69w70w71w72\\1Deasuler_attack2.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_1CORP_DEASULER_MONSTER_ATTACK03,
                 L"Data\\Sound\\w69w70w71w72\\1Deasuler_attack3.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_2CORP_VERMONT_MONSTER_ATTACK01,
                 L"Data\\Sound\\w69w70w71w72\\2Vermont_attack1.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_2CORP_VERMONT_MONSTER_ATTACK02,
                 L"Data\\Sound\\w69w70w71w72\\2Vermont_attack2.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_2CORP_VERMONT_MONSTER_DEATH,
                 L"Data\\Sound\\w69w70w71w72\\2Vermont_death.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_3CORP_CATO_ATTACK02,
                 L"Data\\Sound\\w69w70w71w72\\3Cato_attack2.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_3CORP_CATO_MOVE,
                 L"Data\\Sound\\w69w70w71w72\\3Cato_move.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_4CORP_GALLIA_ATTACK02,
                 L"Data\\Sound\\w69w70w71w72\\4Gallia_attack2.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_QUATERMASTER_ATTACK02,
                 L"Data\\Sound\\w69w70w71w72\\QuaterMaster_attack2.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_COMBATMASTER_ATTACK01,
                 L"Data\\Sound\\w69w70w71w72\\CombatMaster_attack1.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_COMBATMASTER_ATTACK02,
                 L"Data\\Sound\\w69w70w71w72\\CombatMaster_attack2.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_COMBATMASTER_ATTACK03,
                 L"Data\\Sound\\w69w70w71w72\\CombatMaster_attack3.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_GRANDWIZARD_DEATH,
                 L"Data\\Sound\\w69w70w71w72\\GrandWizard_death.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_ASSASSINMASTER_DEATH,
                 L"Data\\Sound\\w69w70w71w72\\AssassinMaster_Death.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_CAVALRYLEADER_ATTACK01,
                 L"Data\\Sound\\w69w70w71w72\\CavalryLeader_attack1.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_CAVALRYLEADER_ATTACK02,
                 L"Data\\Sound\\w69w70w71w72\\CavalryLeader_attack2.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_CAVALRYLEADER_MOVE01,
                 L"Data\\Sound\\w69w70w71w72\\CavalryLeader_move01.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_CAVALRYLEADER_MOVE02,
                 L"Data\\Sound\\w69w70w71w72\\CavalryLeader_move02.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_DEFENDER_ATTACK02,
                 L"Data\\Sound\\w69w70w71w72\\GrandWizard_death.wav");
    LoadWaveFile(SOUND_EMPIREGUARDIAN_PRIEST_STOP, L"Data\\Sound\\w69w70w71w72\\Priest_stay.wav");
}

using namespace SEASON4A;

CGM_RaklionPtr CGM_Raklion::Make(SessionKeeper &keeper)
{
    CGM_RaklionPtr raklion(new CGM_Raklion(keeper));
    raklion->Init();
    return raklion;
}

CGM_Raklion::CGM_Raklion(SessionKeeper &keeper)
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject()), m_bCanGoBossMap(true)
{
    m_byState = RAKLION_STATE_IDLE;
    m_byDetailState = BATTLE_OF_SELUPAN_NONE;
    m_Timer.SetTimer(0);
    m_bVisualEffect = false;
    m_bMusicBossMap = false;
    m_bBossHeightMove = false;
}

CGM_Raklion::~CGM_Raklion()
{
    Destroy();
}

void CGM_Raklion::Init()
{
}

void CGM_Raklion::Destroy()
{
}

bool CGM_Raklion::CreateObject(OBJECT *o)
{
    if (o->Type == 46 || o->Type == 53 || o->Type == 76)
        o->m_bRenderAfterCharacter = true;

    switch (o->Type)
    {
    case MODEL_WARP: {
    }
        return true;
    case MODEL_WARP4: {
        vec3_t Position;
        Vector(o->Position[0], o->Position[1] - 40.f, o->Position[2] + 520.f, Position);
        CreateEffect(MODEL_WARP4, Position, o->Angle, o->Light, 1);

        Vector(o->Position[0], o->Position[1] - 36.f, o->Position[2] + 520.f, Position);
        CreateEffect(MODEL_WARP5, Position, o->Angle, o->Light, 1);

        Vector(o->Position[0], o->Position[1] - 20.f, o->Position[2] + 520.f, Position);
        CreateEffect(MODEL_WARP6, Position, o->Angle, o->Light, 1);
    }
        return true;
    }

    return false;
}

CHARACTER *CGM_Raklion::CreateMonster(int iType, int PosX, int PosY, int Key)
{
    CHARACTER *pCharacter = NULL;
    switch (iType)
    {
    case MONSTER_ICE_WALKER:
        OpenMonsterModel(MONSTER_MODEL_ICE_WALKER);
        pCharacter = CreateCharacter(Key, MODEL_ICE_WALKER, PosX, PosY);
        //pCharacter->Object.Scale = 1.0f;
        pCharacter->Object.Scale = 1.2f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        break;

    case MONSTER_GIANT_MAMMOTH:
        OpenMonsterModel(MONSTER_MODEL_GIANT_MAMMOTH);
        pCharacter = CreateCharacter(Key, MODEL_GIANT_MAMMOTH, PosX, PosY);
        pCharacter->Object.Scale = 1.7f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        RegisterBone(pCharacter, L"GIANT_MAMUD_BIP_TAIL", CharacterSocket::GIANT_MAMUD_BIP_TAIL);
        RegisterBone(pCharacter, L"GIANT_MAMUD_BIP_TAIL_1",
                     CharacterSocket::GIANT_MAMUD_BIP_TAIL_1);
        RegisterBone(pCharacter, L"GIANT_MAMUD_BIP_TAIL_2",
                     CharacterSocket::GIANT_MAMUD_BIP_TAIL_2);
        RegisterBone(pCharacter, L"GIANT_MAMUD_BIP_SPAIN_1",
                     CharacterSocket::GIANT_MAMUD_BIP_SPAIN_1);
        RegisterBone(pCharacter, L"GIANT_MAMUD_BIP_SPAIN_2",
                     CharacterSocket::GIANT_MAMUD_BIP_SPAIN_2);
        RegisterBone(pCharacter, L"GIANT_MAMUD_BIP_SPAIN_3",
                     CharacterSocket::GIANT_MAMUD_BIP_SPAIN_3);
        break;

    case MONSTER_ICE_GIANT:
        OpenMonsterModel(MONSTER_MODEL_ICE_GIANT);
        pCharacter = CreateCharacter(Key, MODEL_ICE_GIANT, PosX, PosY);
        pCharacter->Object.Scale = 1.0f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        pCharacter->Object.LifeTime = 100;
        break;

    case MONSTER_COOLUTIN:
        OpenMonsterModel(MONSTER_MODEL_COOLUTIN);
        pCharacter = CreateCharacter(Key, MODEL_COOLUTIN, PosX, PosY);
        pCharacter->Object.Scale = 1.0f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        break;

    case MONSTER_IRON_KNIGHT:
        OpenMonsterModel(MONSTER_MODEL_IRON_KNIGHT);
        pCharacter = CreateCharacter(Key, MODEL_IRON_KNIGHT, PosX, PosY);
        pCharacter->Object.Scale = 1.5f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        break;

    case MONSTER_SELUPAN: {
        OpenMonsterModel(MONSTER_MODEL_SELUPAN);
        pCharacter = CreateCharacter(Key, MODEL_SELUPAN, PosX, PosY);
        pCharacter->Object.Scale = 2.0f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;

        if (m_byState >= RAKLION_STATE_STANDBY && m_byState <= RAKLION_STATE_READY)
        {
            pCharacter->Object.Position[2] = 1000.f;
            m_bBossHeightMove = true;
        }
    }
    break;
    case MONSTER_SPIDER_EGGS_1: {
        OpenMonsterModel(MONSTER_MODEL_SPIDER_EGGS_1);
        pCharacter = CreateCharacter(Key, MODEL_SPIDER_EGGS_1, PosX, PosY);
        pCharacter->Object.Scale = 0.3f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        pCharacter->Object.EnableShadow = false;
        pCharacter->Object.m_bRenderShadow = false;
    }
    break;
    case MONSTER_SPIDER_EGGS_2: {
        OpenMonsterModel(MONSTER_MODEL_SPIDER_EGGS_2);
        pCharacter = CreateCharacter(Key, MODEL_SPIDER_EGGS_2, PosX, PosY);
        pCharacter->Object.Scale = 0.3f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        pCharacter->Object.EnableShadow = false;
        pCharacter->Object.m_bRenderShadow = false;
    }
    break;
    case MONSTER_SPIDER_EGGS_3: {
        OpenMonsterModel(MONSTER_MODEL_SPIDER_EGGS_3);
        pCharacter = CreateCharacter(Key, MODEL_SPIDER_EGGS_3, PosX, PosY);
        pCharacter->Object.Scale = 0.3f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        pCharacter->Object.EnableShadow = false;
        pCharacter->Object.m_bRenderShadow = false;
    }
    break;

    case MONSTER_DARK_MAMMOTH: {
        OpenMonsterModel(MONSTER_MODEL_DARK_MAMMOTH);
        pCharacter = CreateCharacter(Key, MODEL_DARK_MAMMOTH, PosX, PosY);
        //pCharacter->Object.Scale = 1.7f;
        pCharacter->Object.Scale = 1.9f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        RegisterBone(pCharacter, L"GIANT_MAMUD_BIP_TAIL", CharacterSocket::GIANT_MAMUD_BIP_TAIL);
        RegisterBone(pCharacter, L"GIANT_MAMUD_BIP_TAIL_1",
                     CharacterSocket::GIANT_MAMUD_BIP_TAIL_1);
        RegisterBone(pCharacter, L"GIANT_MAMUD_BIP_TAIL_2",
                     CharacterSocket::GIANT_MAMUD_BIP_TAIL_2);
        RegisterBone(pCharacter, L"GIANT_MAMUD_BIP_SPAIN_1",
                     CharacterSocket::GIANT_MAMUD_BIP_SPAIN_1);
        RegisterBone(pCharacter, L"GIANT_MAMUD_BIP_SPAIN_2",
                     CharacterSocket::GIANT_MAMUD_BIP_SPAIN_2);
        RegisterBone(pCharacter, L"GIANT_MAMUD_BIP_SPAIN_3",
                     CharacterSocket::GIANT_MAMUD_BIP_SPAIN_3);
    }
    break;
    case MONSTER_DARK_GIANT: {
        OpenMonsterModel(MONSTER_MODEL_DARK_GIANT);
        pCharacter = CreateCharacter(Key, MODEL_DARK_GIANT, PosX, PosY);
        //pCharacter->Object.Scale = 1.0f;
        pCharacter->Object.Scale = 1.1f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
        pCharacter->Object.LifeTime = 100;
    }
    break;

    case MONSTER_DARK_COOLUTIN: {
        OpenMonsterModel(MONSTER_MODEL_DARK_COOLUTIN);
        pCharacter = CreateCharacter(Key, MODEL_DARK_COOLUTIN, PosX, PosY);
        //pCharacter->Object.Scale = 1.0f;
        pCharacter->Object.Scale = 1.3f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
    }
    break;
    case MONSTER_DARK_IRON_KNIGHT: {
        OpenMonsterModel(MONSTER_MODEL_DARK_IRON_KNIGHT);
        pCharacter = CreateCharacter(Key, MODEL_DARK_IRON_KNIGHT, PosX, PosY);
        //pCharacter->Object.Scale = 1.5f;
        pCharacter->Object.Scale = 1.8f;
        pCharacter->Weapon[0].Type = -1;
        pCharacter->Weapon[1].Type = -1;
    }
    break;
    }

    return pCharacter;
}

bool CGM_Raklion::MoveObject(OBJECT *o, CTimer2::StartTickTime &timer2StartTickTime)
{
    if (IsIceCity() == false)
        return false;

    if (o->Type == 82 && !m_bCanGoBossMap)
    {
        o->AnimationFrame = o->PriorAnimationFrame = 0.f;
        return true;
    }
    switch (o->Type)
    {
    case 22: {
        o->BlendMeshLight = (float)sinf(WorldTime * 0.001f) + 1.0f;
        return true;
    }
    break;
    case 70:
    case 80: {
        o->HiddenMesh = -2;
        return true;
    }
    break;
    }

    MoveEffect(timer2StartTickTime);

    return false;
}

namespace RaklionDetail
{
float EggDeathBrightness(float animationFrame)
{
    constexpr float FadeEndFrame = 20.f, FadeSpan = 15.f;
    return (FadeEndFrame - animationFrame) / FadeSpan;
}
} // namespace RaklionDetail

bool CGM_Raklion::IsIceCity()
{
    if (gMapManager.ContextMap() >= WD_57ICECITY && gMapManager.ContextMap() <= WD_58ICECITY_BOSS)
    {
        return true;
    }
    else if (gMapManager.ContextMap() == WD_65DOPPLEGANGER1)
    {
        return true;
    }

    return false;
}

bool CGM_Raklion::SetCurrentActionMonster(CHARACTER *c, OBJECT *o)
{
    if (IsIceCity() == false)
        return false;

    switch (c->MonsterIndex)
    {
    case MONSTER_ICE_WALKER: {
        if (c->MonsterSkill == 29)
        {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        else
        {
            SetAction(o, MONSTER01_ATTACK1);
        }
        return true;
    }
    break;
    case MONSTER_GIANT_MAMMOTH: {
        if (c->MonsterSkill == 30)
        {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        else
        {
            SetAction(o, MONSTER01_ATTACK1);
        }
        return true;
    }
    break;
    case MONSTER_ICE_GIANT: {
        if (c->MonsterSkill == 31)
        {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        else
        {
            SetAction(o, MONSTER01_ATTACK1);
        }
        return true;
    }
    break;
    case MONSTER_COOLUTIN: {
        if (c->MonsterSkill == 32)
        {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        else
        {
            SetAction(o, MONSTER01_ATTACK1);
        }
        return true;
    }
    case MONSTER_IRON_KNIGHT: {
        if (c->MonsterSkill == 33)
        {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        else
        {
            SetAction(o, MONSTER01_ATTACK1);
        }
        return true;
    }
    break;
    case MONSTER_SELUPAN: {
        SetBossMonsterAction(c, o);
        return true;
    }
    break;
    case MONSTER_SPIDER_EGGS_1:
    case MONSTER_SPIDER_EGGS_2:
    case MONSTER_SPIDER_EGGS_3: {
        return false;
    }
    break;
    case MONSTER_DARK_MAMMOTH: {
        if (c->MonsterSkill == ATMON_SKILL_EX_DARKMEMUD_ATTACKSKILL)
        {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        else
        {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        return true;
    }
    break;
    case MONSTER_DARK_GIANT: {
        if (c->MonsterSkill == ATMON_SKILL_EX_DARKGIANT_ATTACKSKILL)
        {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        else
        {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        return true;
    }
    break;
    case MONSTER_DARK_IRON_KNIGHT: {
        if (c->MonsterSkill == ATMON_SKILL_EX_DARKAIONNIGHT_ATTACKSKILL)
        {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        else
        {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        return true;
    }
    break;
    case MONSTER_DARK_COOLUTIN: {
        if (c->MonsterSkill == ATMON_SKILL_EX_DARKCOOLERTIN_ATTACKSKILL)
        {
            SetAction(o, MONSTER01_ATTACK2);
            c->MonsterSkill = -1;
        }
        else
        {
            SetAction(o, MONSTER01_ATTACK1);
            c->MonsterSkill = -1;
        }
        return true;
    }
    break;
    }

    return false;
}

void CGM_Raklion::SetBossMonsterAction(CHARACTER *c, OBJECT *o)
{
    if (c->MonsterSkill == 37 || !(c->MonsterSkill >= 34 && c->MonsterSkill <= 42) ||
        m_bBossHeightMove == true)
    {
        c->Object.Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]);
        m_bBossHeightMove = false;
    }

    if (c->MonsterSkill == 34)
    {
        SetAction(o, MONSTER01_ATTACK2);
    }
    else if (c->MonsterSkill == 35)
    {
        SetAction(o, MONSTER01_ATTACK1);
    }
    else if (c->MonsterSkill == 36)
    {
        SetAction(o, MONSTER01_ATTACK3);
    }
    else if (c->MonsterSkill == 37)
    {
        SetAction(o, MONSTER01_APEAR);
    }
    else if (c->MonsterSkill == 38)
    {
        SetAction(o, MONSTER01_ATTACK4);
        PlayBuffer(SOUND_RAKLION_SERUFAN_WORD4);
    }
    else if (c->MonsterSkill == 39)
    {
        SetAction(o, MONSTER01_ATTACK4);
        PlayBuffer(SOUND_RAKLION_SERUFAN_CURE);

        vec3_t vLight, vPos, vAngle;
        Vector(1.f, 1.f, 1.f, vLight);
        for (int i = 0; i < 20; ++i)
        {
            Vector(0.f, 0.f, WorldRandom() % 360, vAngle);
            VectorCopy(o->Position, vPos);
            vPos[0] += WorldRandom() % 400 - 200;
            vPos[1] += WorldRandom() % 400 - 200;
            vPos[2] -= 100.f;
            CreateJoint(BITMAP_FLARE, vPos, vPos, vAngle, 2, NULL, 40);
        }

        Vector(1.f, 0.5f, 0.1f, vLight);
        CreateEffect(BITMAP_MAGIC + 1, o->Position, o->Angle, vLight, 13, o, -1, 0, 0, 0, 8.f);
        CreateEffect(BITMAP_MAGIC + 1, o->Position, o->Angle, vLight, 13, o, -1, 0, 0, 0, 8.f);
    }
    else if (c->MonsterSkill == 40)
    {
        SetAction(o, MONSTER01_ATTACK4);
        PlayBuffer(SOUND_RAKLION_SERUFAN_WORD3);
    }
    else if (c->MonsterSkill == 41)
    {
    }
    else if (c->MonsterSkill == 42)
    {
        SetAction(o, MONSTER01_ATTACK4);

        vec3_t vLight;
        Vector(0.3f, 0.2f, 0.1f, vLight);
        CreateEffect(MODEL_STORM2, o->Position, o->Angle, vLight, 1, NULL, -1, 0, 0, 0, 1.6f);
        CreateEffect(MODEL_SUMMON, o->Position, o->Angle, vLight, 3);
    }
    else
    {
        SetAction(o, MONSTER01_ATTACK4);
    }
}

bool CGM_Raklion::CanGoBossMap()
{
    return m_bCanGoBossMap;
}

void CGM_Raklion::SetCanGoBossMap()
{
    if (m_byState <= RAKLION_STATE_NOTIFY_3)
    {
        m_bCanGoBossMap = true;
    }
    else
    {
        m_bCanGoBossMap = false;
    }
}

void CGM_Raklion::SetState(BYTE byState, BYTE byDetailState)
{
    if (byState == RAKLION_STATE_DETAIL_STATE)
    {
        m_byDetailState = byDetailState;

        if (m_byDetailState == BATTLE_OF_SELUPAN_PATTERN_2 ||
            m_byDetailState == BATTLE_OF_SELUPAN_PATTERN_3 ||
            m_byDetailState == BATTLE_OF_SELUPAN_PATTERN_5 ||
            m_byDetailState == BATTLE_OF_SELUPAN_PATTERN_6)
        {
            PlayBuffer(SOUND_RAKLION_SERUFAN_RAGE);
        }
    }
    else
    {
        m_byState = byState;

        if (m_byState == RAKLION_STATE_NOTIFY_1 || m_byState == RAKLION_STATE_STANDBY)
        {
            PlayBuffer(SOUND_WIND01);
        }
        else if (m_byState == RAKLION_STATE_READY)
        {
            PlayBuffer(SOUND_RAKLION_SERUFAN_WORD1);
            m_bMusicBossMap = true;
        }
        else if (m_byState == RAKLION_STATE_END)
        {
            m_bMusicBossMap = false;
        }

        SetCanGoBossMap();

        SetEffect();
    }
}

void CGM_Raklion::SetEffect()
{
    if (m_byState == RAKLION_STATE_NOTIFY_1)
    {
        m_Timer.SetTimer(500);
        m_bVisualEffect = true;
    }
    else if (m_byState == RAKLION_STATE_STANDBY)
    {
        m_Timer.SetTimer(1000);
        m_bVisualEffect = true;
    }
}

void CGM_Raklion::CaptureMonsterAttackState(CHARACTER &character, BMD &model)
{
    auto &object = character.Object;
    if (gMapManager.ContextMap() != WD_57ICECITY && gMapManager.ContextMap() != WD_58ICECITY_BOSS)
        return;
    if (object.CurrentAction == MONSTER01_DIE &&
        (object.Type == MODEL_ICE_GIANT || object.Type == MODEL_DARK_GIANT) &&
        object.LifeTime == 100.f)
    {
        object.LifeTime = 90.f;
        object.m_bRenderShadow = false;
    }
    if ((object.Type != MODEL_IRON_KNIGHT && object.Type != MODEL_DARK_IRON_KNIGHT) ||
        character.WorldVisualAttackFrameTime != WorldTime ||
        character.WorldVisualAttackAction != MONSTER01_ATTACK2)
        return;
    constexpr float tickTolerance = 0.0001f;
    const float start = character.WorldVisualAttackStart;
    const float end = start + character.WorldVisualAttackFrames;
    const float lastSwordTick = std::min(8.f, std::ceil(end - tickTolerance) - 1.f);
    if (lastSwordTick >= std::ceil(start - tickTolerance))
    {
        AnimationPoseSample pose(&object, model.BoneHead, model.BodyHeight, false,
                                 model.PoseAssetIdentity());
        const float fraction = std::max(0.f, lastSwordTick - start) / FPS_ANIMATION_FACTOR;
        vec3_t offset{};
        pose.SampleBonePosition(model, object, 26, offset, WorldTime, fraction, object.m_vPosSword);
        const float yaw = object.MotionTrace.SampleYaw(WorldTime, fraction, object.Angle[2]);
        constexpr float swordOffset = 300.f;
        object.m_vPosSword[0] += swordOffset * sinf(yaw * Q_PI / 180.f);
        object.m_vPosSword[1] -= swordOffset * cosf(yaw * Q_PI / 180.f);
    }
    if (character.TargetCharacter == -1)
        return;
    for (float tick = std::max(10.f, std::ceil(start - tickTolerance));
         tick <= 12.f && tick < end - tickTolerance; tick += 1.f)
        if (WorldRandom() % 2 == 0)
            sessionKeeper_.CharactersClientStorage().mapHitTargets.push_back(
                character.TargetCharacter);
}

void SEASON4A::CGM_Raklion::InstallBehavior()
{
    LoadWaveFile(SOUND_KANTURU_3RD_MAYAHAND_ATTACK2, L"Data\\Sound\\w39\\maya_hand_attack-02.wav",
                 1);

    vec3_t vPos, vAngle;
    if (gMapManager.ContextMap() == WD_57ICECITY)
    {
        Vector(0.f, 0.f, 35.f, vAngle);
        Vector(0.f, 0.f, 0.f, vPos);
        vPos[0] = 162 * TERRAIN_SCALE;
        vPos[1] = 83 * TERRAIN_SCALE;
        SessionLegacyCalls::CreateObject(MODEL_WARP, vPos, vAngle);

        Vector(0.f, 0.f, 80.f, vAngle);
        Vector(0.f, 0.f, 0.f, vPos);
        vPos[0] = 171 * TERRAIN_SCALE;
        vPos[1] = 24 * TERRAIN_SCALE;
        SessionLegacyCalls::CreateObject(MODEL_WARP4, vPos, vAngle);
    }
    else if (gMapManager.ContextMap() == WD_58ICECITY_BOSS)
    {
        Vector(0.f, 0.f, 85.f, vAngle);
        Vector(0.f, 0.f, 0.f, vPos);
        vPos[0] = 169 * TERRAIN_SCALE;
        vPos[1] = 24 * TERRAIN_SCALE;
        SessionLegacyCalls::CreateObject(MODEL_WARP4, vPos, vAngle);

        Vector(0.f, 0.f, 85.f, vAngle);
        Vector(0.f, 0.f, 0.f, vPos);
        vPos[0] = 170 * TERRAIN_SCALE;
        vPos[1] = 24 * TERRAIN_SCALE;
        SessionLegacyCalls::CreateObject(MODEL_WARP4, vPos, vAngle);
    }
    if (gMapManager.ContextMap() == WD_57ICECITY)
    {
        Models[16].Actions[0].PlaySpeed = 0.8f;
        Models[16].Actions[1].PlaySpeed = 0.8f;
        Models[17].Actions[0].PlaySpeed = 0.8f;
        Models[17].Actions[1].PlaySpeed = 0.8f;
        Models[17].Actions[2].PlaySpeed = 1.f;
        Models[17].Actions[3].PlaySpeed = 1.f;
        Models[68].Actions[0].PlaySpeed = 0.05f;
    }
}

CGMBattleCastlePtr CGMBattleCastle::Make(SessionKeeper &keeper)
{
    return CGMBattleCastlePtr(new CGMBattleCastle(keeper));
}

CGMBattleCastle::CGMBattleCastle(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      Mounts(keeper.MountsStorage()), ObjectBlock(keeper.ObjectBlocks()),
      gMapManager(keeper.MapManagerObject()), g_Camera(keeper.CameraStateObject()),
      cameraProjection_(keeper.CameraProjectionObject())
{
}

bool CGMBattleCastle::IsBattleCastleStart(void)
{
    return g_bBattleCastleStart;
}

bool CGMBattleCastle::InBattleCastle2(vec3_t Position)
{
    return Position[0] >= 16100.f && Position[0] <= 19000.f && Position[1] >= 18900.f &&
           Position[1] <= 21700.f;
}

bool CGMBattleCastle::InBattleCastle3(vec3_t Position)
{
    return Position[0] >= 4700.f && Position[0] <= 14800.f && Position[1] >= 5300.f &&
           Position[1] <= 23900.f;
}
void CGMBattleCastle::SetBattleCastleStart(bool bResult)
{
    g_bBattleCastleStartBackup = g_bBattleCastleStart;
    g_bBattleCastleStart = bResult;
    if (!gMapManager.InBattleCastle())
        return;

    sessionKeeper_.WorldUnit()->ReloadTerrainVariant(bResult ? MapDefinition::Variant::War
                                                             : MapDefinition::Variant::Base);
    g_iMp3PlayTime = 0;
}

bool CGMBattleCastle::InArea(float x, float y, vec3_t Position, float Range)
{
    float dx = x - Position[0];
    float dy = y - Position[1];
    float Distance = sqrtf(dx * dx + dy * dy);
    if (Distance <= Range)
        return true;
    return false;
}

void CGMBattleCastle::CollisionHeroCharacter(vec3_t Position, float Range, int AniType)
{
    OBJECT *o = &Hero->Object;

    if (InArea(Position[0], Position[1], o->Position, Range) && o->CurrentAction != AniType)
    {
        //            if ( Range<=350.f && WorldRandom()%(int)(100-(Range-350.f)/10) )
        if (Hero->Helper.Type == MODEL_HORN_OF_FENRIR && AniType == PLAYER_HIGH_SHOCK)
        {
            SetAction_Fenrir_Damage(Hero, o);
            SendRequestAction(Hero->Object, AniType);
        }
        else
        {
            SetAction(o, AniType);
            SendRequestAction(Hero->Object, AniType);
        }
    }
}

void CGMBattleCastle::CollisionTempCharacter(vec3_t Position, float Range, int AniType)
{
    for (int i = 0; i < CharactersClient.Size(); ++i)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        OBJECT *o = &CharactersClient[i].Object;
        if (o->Live && (o->Kind == KIND_TMP) && o->Type == MODEL_PLAYER &&
            o->CurrentAction != AniType)
        {
            if (InArea(Position[0], Position[1], o->Position, Range))
            {
                //                    if ( Range<=350.f && WorldRandom()%(int)(100-(Range-350.f)/10) )
                {
                    SetAction(o, AniType);
                }
            }
        }
    }
}

bool CGMBattleCastle::CollisionEffectToObject(OBJECT *eff, float Range, float RangeZ,
                                              bool bCollisionGround, bool bRealCollision)
{
    int i = (int)(eff->Position[0] / (16 * TERRAIN_SCALE));
    int j = (int)(eff->Position[1] / (16 * TERRAIN_SCALE));
    if (i < 0 || j < 0 || i >= 16 || j >= 16)
        return false;

    BYTE Block = i * 16 + j;
    OBJECT_BLOCK *ob = &ObjectBlock[Block];
    OBJECT *o = ob->Head;
    while (1)
    {
        if (o != NULL)
        {
            if (o->Live && o->HiddenMesh == -1 && o->m_bCollisionCheck)
            {
                float dx = eff->Position[0] - o->Position[0];
                float dy = eff->Position[1] - o->Position[1];
                float Distance = sqrtf(dx * dx + dy * dy);
                if (Distance <= Range)
                {
                    if (bCollisionGround)
                    {
                        if (o->m_bCollisionCheck && o->ExtState == 0)
                        {
                            o->Timer += 0.3f * FPS_ANIMATION_FACTOR;
                            if (o->Timer >= 5)
                            {
                                o->ExtState = 99;
                            }
                        }
                        return true;
                    }
                    else if (fabs(eff->Position[2] - o->Position[2]) < RangeZ)
                    {
                        if (o->m_bCollisionCheck)
                        {
                            if (o->ExtState == 0)
                            {
                                o->Timer += FPS_ANIMATION_FACTOR;
                                if (o->Timer >= 5)
                                {
                                    o->ExtState = 99;
                                }
                            }
                        }
                        eff->LifeTime = 15;
                        eff->SubType = 99;
                        eff->HiddenMesh = 99;
                        return true;
                    }
                }
            }
            if (o->Next == NULL)
                break;
            o = o->Next;
        }
        else
            break;
    }
    return false;
}

bool CGMBattleCastle::CalcDistanceChrToChr(OBJECT *o, BYTE Type, float fRange)
{
    for (int i = 0; i < CharactersClient.Size(); i++)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        CHARACTER *tc = &CharactersClient[i];
        OBJECT *to = &tc->Object;
        if (to->Live && to->Kind == KIND_PLAYER && to->Type == MODEL_PLAYER)
        {
            if (Type == 0)
            {
                float dx = o->Position[0] - to->Position[0];
                float dy = o->Position[1] - to->Position[1];
                if (dx < fRange && dy < 100)
                    return true;
            }
            else if (Type == 1)
            {
                float dx = fabs(o->Position[0] - to->Position[0]);
                float dy = o->Position[1] - to->Position[1];
                if (dx < 1 && dy > 0 && dy < fRange)
                {
                    return true;
                }
            }
            else if (Type == 2)
            {
                float dx = o->Position[0] - to->Position[0];
                float dy = o->Position[1] - to->Position[1];
                float Distance = sqrtf(dx * dx + dy * dy);
                if (Distance < fRange)
                    return true;
            }
        }
    }
    return false;
}

void CGMBattleCastle::SetCastleGate_Attribute(int x, int y, BYTE Operator, bool bAllClear)
{
    for (int i = 0; i < 6; ++i)
    {
        if (bAllClear == true)
        {
            DWORD wall =
                TerrainWall[(g_byGateLocation[i][1] + 1) * TERRAIN_SIZE + (g_byGateLocation[i][0])];
            if ((wall & TW_NOMOVE) == TW_NOMOVE)
            {
                AddTerrainAttributeRange(g_byGateLocation[i][0] - 1, g_byGateLocation[i][1] + 1, 4,
                                         1, TW_NOMOVE, Operator);
            }
        }
        else
        {
            if (x == g_byGateLocation[i][0] && y == g_byGateLocation[i][1])
            {
                AddTerrainAttributeRange(g_byGateLocation[i][0] - 1, g_byGateLocation[i][1] + 1, 4,
                                         1, TW_NOMOVE, Operator);
                return;
            }
        }
    }
}

void CGMBattleCastle::SetBuildTimeLocation(OBJECT *o)
{
    if (o->Type == MODEL_LIFE_STONE && o->m_byBuildTime < 5)
    {
        BuildTime bt;

        VectorCopy(o->Position, bt.m_vPosition);
        bt.m_vPosition[2] += o->BoundingBoxMax[2] + 100.f;
        bt.m_byBuildTime = o->m_byBuildTime;
        g_qBuildTimeLocation.push(bt);
    }
}

void CGMBattleCastle::Init(void)
{
    if (gMapManager.InBattleCastle() == false)
        return;

    g_byGuardAI = 0;
    g_iMp3PlayTime = 0;
    //        SetBattleCastleStart ( false );

    vec3_t Angle, Position;
    Vector(0.f, 0.f, 0.f, Angle);
    Vector(0.f, 0.f, 0.f, Position);

    SocketClient->ToGameServer()->SendGuildLogoOfCastleOwnerRequest();

    OpenMonsterModel(MONSTER_MODEL_BATTLE_GUARD2);

    constexpr float zOffset = 80.f;

    Position[0] = 65 * TERRAIN_SCALE;
    Position[1] = 113 * TERRAIN_SCALE;
    Position[2] = RequestTerrainHeight(Position[0], Position[1]) + zOffset;
    SessionLegacyCalls::CreateObject(MODEL_BATTLE_GUARD2, Position, Angle);

    Position[0] = 71 * TERRAIN_SCALE;
    Position[1] = 113 * TERRAIN_SCALE;
    Position[2] = RequestTerrainHeight(Position[0], Position[1]) + zOffset;
    SessionLegacyCalls::CreateObject(MODEL_BATTLE_GUARD2, Position, Angle);

    Position[0] = 91 * TERRAIN_SCALE;
    Position[1] = 113 * TERRAIN_SCALE;
    Position[2] = RequestTerrainHeight(Position[0], Position[1]) + zOffset;
    SessionLegacyCalls::CreateObject(MODEL_BATTLE_GUARD2, Position, Angle);

    Position[0] = 97 * TERRAIN_SCALE;
    Position[1] = 113 * TERRAIN_SCALE;
    Position[2] = RequestTerrainHeight(Position[0], Position[1]) + zOffset;
    SessionLegacyCalls::CreateObject(MODEL_BATTLE_GUARD2, Position, Angle);

    Position[0] = 117 * TERRAIN_SCALE;
    Position[1] = 113 * TERRAIN_SCALE;
    Position[2] = RequestTerrainHeight(Position[0], Position[1]) + zOffset;
    SessionLegacyCalls::CreateObject(MODEL_BATTLE_GUARD2, Position, Angle);

    Position[0] = 123 * TERRAIN_SCALE;
    Position[1] = 113 * TERRAIN_SCALE;
    Position[2] = RequestTerrainHeight(Position[0], Position[1]) + zOffset;
    SessionLegacyCalls::CreateObject(MODEL_BATTLE_GUARD2, Position, Angle);
}

bool CGMBattleCastle::SettingBattleFormation(CHARACTER *c, eBuffState state)
{
    if (gMapManager.InBattleCastle() == false)
        return false;

    if (state == eBuff_CastleRegimentAttack1)
    {
        if (c->EtcPart == PARTS_DEFENSE_TEAM_MARK)
        {
            DeleteParts(c);
        }
        c->EtcPart = PARTS_ATTACK_TEAM_MARK;
    }

    else if (state == eBuff_CastleRegimentAttack2)
    {
        if (c->EtcPart == PARTS_DEFENSE_TEAM_MARK)
        {
            DeleteParts(c);
        }
        c->EtcPart = PARTS_ATTACK_TEAM_MARK2;
    }
    else if (state == eBuff_CastleRegimentAttack3)
    {
        if (c->EtcPart == PARTS_DEFENSE_TEAM_MARK)
        {
            DeleteParts(c);
        }
        c->EtcPart = PARTS_ATTACK_TEAM_MARK3;
    }
    else if (state == eBuff_CastleRegimentDefense)
    {
        if (c->EtcPart == PARTS_ATTACK_TEAM_MARK || c->EtcPart == PARTS_ATTACK_TEAM_MARK2 ||
            c->EtcPart == PARTS_ATTACK_TEAM_MARK3)
        {
            DeleteParts(c);
        }
        c->EtcPart = PARTS_DEFENSE_TEAM_MARK;
    }
    else if (c->EtcPart == PARTS_ATTACK_TEAM_MARK || c->EtcPart == PARTS_DEFENSE_TEAM_MARK ||
             c->EtcPart == PARTS_ATTACK_TEAM_MARK2 || c->EtcPart == PARTS_ATTACK_TEAM_MARK3)
    {
        DeleteParts(c);
        return false;
    }
    return true;
}

bool CGMBattleCastle::GetGuildMaster(CHARACTER *c)
{
    if (wcscmp(GuildMark[c->GuildMarkIndex].GuildName, L"") == 0)
        return false;
    if (wcscmp(GuildMark[c->GuildMarkIndex].UnionName, L"") == 0 && c->GuildStatus != G_MASTER)
        return false;
    if (wcscmp(GuildMark[c->GuildMarkIndex].UnionName, GuildMark[c->GuildMarkIndex].GuildName) ==
            0 &&
        c->GuildStatus != G_MASTER)
        return false;

    return true;
}

void CGMBattleCastle::SettingBattleKing(CHARACTER *c)
{
    OBJECT *o = &c->Object;

    if (GetGuildMaster(c) == false)
        return;

    if (g_isCharacterBuff(o, eBuff_CastleRegimentAttack1))
    {
        DeleteParts(c);
        c->EtcPart = PARTS_ATTACK_KING_TEAM_MARK;
    }
    else if (g_isCharacterBuff(o, eBuff_CastleRegimentAttack2))
    {
        DeleteParts(c);
        c->EtcPart = PARTS_ATTACK_KING_TEAM_MARK2;
    }
    else if (g_isCharacterBuff(o, eBuff_CastleRegimentAttack3))
    {
        DeleteParts(c);
        c->EtcPart = PARTS_ATTACK_KING_TEAM_MARK3;
    }
    else if (g_isCharacterBuff(o, eBuff_CastleRegimentDefense))
    {
        DeleteParts(c);
        c->EtcPart = PARTS_DEFENSE_KING_TEAM_MARK;
    }
}

void CGMBattleCastle::DeleteBattleFormation(CHARACTER *c, eBuffState state)
{
    if (gMapManager.InBattleCastle() == false)
        return;

    if (eBuff_CastleRegimentAttack1 != state || eBuff_CastleRegimentAttack2 != state ||
        eBuff_CastleRegimentAttack3 != state || eBuff_CastleRegimentDefense != state)
    {
        return;
    }

    OBJECT *o = &c->Object;

    if (g_isCharacterBuff(o, eBuff_CastleRegimentAttack1) ||
        g_isCharacterBuff(o, eBuff_CastleRegimentAttack2) ||
        g_isCharacterBuff(o, eBuff_CastleRegimentAttack3) ||
        g_isCharacterBuff(o, eBuff_CastleRegimentDefense))
    {
        if (c->EtcPart == PARTS_DEFENSE_TEAM_MARK || c->EtcPart == PARTS_ATTACK_TEAM_MARK ||
            c->EtcPart == PARTS_ATTACK_TEAM_MARK2 || c->EtcPart == PARTS_ATTACK_TEAM_MARK3)
        {
            DeleteParts(c);
        }
    }
}

void CGMBattleCastle::ChangeBattleFormation(wchar_t *GuildName, bool bEffect)
{
    for (int i = 0; i < CharactersClient.Size(); ++i)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        auto access = CharactersClient.AcquireSharedAccess(i);
        auto &character = CharactersClient[i];
        if (character.Object.Live)
            ApplyBattleFormation(character, GuildName);
    }
}

void CGMBattleCastle::ApplyBattleFormation(CHARACTER &character, const wchar_t *GuildName)
{
    auto *c = &character;
    auto *o = &character.Object;
    DeleteParts(c);
    if (c->GuildMarkIndex >= 0 && wcscmp(GuildMark[c->GuildMarkIndex].UnionName, GuildName) == 0)
    {
        // _buffwani_
        g_TokenCharacterBuff(o, eBuff_CastleRegimentDefense);
        c->EtcPart = PARTS_DEFENSE_TEAM_MARK;
    }
    // _buffwani_
    else if (g_isCharacterBuff(o, eBuff_CastleRegimentAttack1) ||
             g_isCharacterBuff(o, eBuff_CastleRegimentDefense))
    {
        if (g_isCharacterBuff(o, eBuff_CastleRegimentDefense))
            g_CharacterUnRegisterBuff(o, eBuff_CastleRegimentDefense);

        g_CharacterRegisterBuff(o, eBuff_CastleRegimentAttack1);
        c->EtcPart = PARTS_ATTACK_TEAM_MARK;
    }
    else if (g_isCharacterBuff(o, eBuff_CastleRegimentAttack2) ||
             g_isCharacterBuff(o, eBuff_CastleRegimentDefense))
    {
        if (g_isCharacterBuff(o, eBuff_CastleRegimentDefense))
            g_CharacterUnRegisterBuff(o, eBuff_CastleRegimentDefense);

        g_CharacterRegisterBuff(o, eBuff_CastleRegimentAttack2);
        c->EtcPart = PARTS_ATTACK_TEAM_MARK2;
    }
    else if (g_isCharacterBuff(o, eBuff_CastleRegimentAttack3) ||
             g_isCharacterBuff(o, eBuff_CastleRegimentDefense))
    {
        if (g_isCharacterBuff(o, eBuff_CastleRegimentDefense))
            g_CharacterUnRegisterBuff(o, eBuff_CastleRegimentDefense);

        g_CharacterRegisterBuff(o, eBuff_CastleRegimentAttack3);
        c->EtcPart = PARTS_ATTACK_TEAM_MARK3;
    }
}

void CGMBattleCastle::DeleteTmpCharacter(void)
{
    for (int i = 0; i < CharactersClient.Size(); ++i)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        CHARACTER *c = &CharactersClient[i];
        OBJECT *o = &c->Object;
        if (o->Live && o->Kind == KIND_TMP)
        {
            o->Live = false;
            UnregisterBone(c);

            for (int j = 0; j < MAX_MOUNTS; j++)
            {
                OBJECT *b = &Mounts[j];
                if (b->Live && b->Owner == o)
                    b->Live = false;
            }
            DeletePet(c);
            DeleteCloth(c, o);
            DeleteParts(c);
        }
    }
}

void CGMBattleCastle::StartFog(vec3_t Color)
{
    glEnable(GL_FOG);

    glFogfv(GL_FOG_COLOR, Color);
    glFogf(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 2000.f);
    glFogf(GL_FOG_END, 2700.f);
}

void CGMBattleCastle::EndFog(void)
{
    glDisable(GL_FOG);
}

void CGMBattleCastle::SetAttackDefenseObjectType(OBJECT *o)
{
    switch (o->Type)
    {
    case 16:
    case 38:
    case 39:
    case 40:
    case 50:
    case 58:
    case 59:
    case 78:
    case 80:
    case 81:
    case 82:
    case 83:
    case 84:
    case 85:
    case 86:
    case 87:
        if (IsBattleCastleStart())
        {
            o->HiddenMesh = -2;
        }
        else
        {
            o->HiddenMesh = -1;
        }
        break;

    case 4:
    case 11:
    case 37:
    case 41:
    case 43:
    case 44:
    case 45:
    case 46:
    case 47:
    case 48:
        //        case 66:
    case 67:
    case 68:
    case 69:
    case 71:
    case 72:
    case 73:
    case 74:
    case 75:
    case 76:
    case 88:
        if (IsBattleCastleStart())
        {
            o->HiddenMesh = -1;
        }
        else
        {
            o->HiddenMesh = -2;
        }
        break;
    }
}

bool CGMBattleCastle::MoveBattleCastleObjectSetting(int &objCount, int object)
{
    if (gMapManager.InBattleCastle() == false)
        return false;

    DWORD current = timeGetTime();
    double dif = (double)(current - g_iMp3PlayTime) / CLOCKS_PER_SEC;
    if (dif > 1800 || g_iMp3PlayTime == 0)
    {
        g_iMp3PlayTime = current;
        if (IsBattleCastleStart())
        {
            StopMp3(MUSIC_CASTLE_PEACE);
            PlayMp3(MUSIC_CASTLE_BATTLE_START);
            //            PlayMp3 ( MUSIC_CASTLE_BATTLE_ING );
        }
        else
        {
            StopMp3(MUSIC_CASTLE_BATTLE_START);
            //            StopMp3 ( MUSIC_CASTLE_BATTLE_ING );
            PlayMp3(MUSIC_CASTLE_PEACE);
        }
    }

    PlayBuffer(SOUND_BC_AMBIENT);
    if (IsBattleCastleStart() && rand_fps_check(10))
    {
        if (rand_fps_check(100))
        {
            PlayBuffer(SOUND_BC_AMBIENT_BATTLE1);
        }
        if (rand_fps_check(100))
        {
            PlayBuffer(SOUND_BC_AMBIENT_BATTLE2);
        }
        if (rand_fps_check(100))
        {
            PlayBuffer(SOUND_BC_AMBIENT_BATTLE3);
        }
        if (rand_fps_check(100))
        {
            PlayBuffer(SOUND_BC_AMBIENT_BATTLE4);
        }
        if (rand_fps_check(100))
        {
            PlayBuffer(SOUND_BC_AMBIENT_BATTLE5);
        }
    }

    if (IsBattleCastleStart())
    {
        if (LastStoneFlyEffect < WorldTime - StoneFlyEffectInterval)
        {
            LastStoneFlyEffect = WorldTime;
            int HeroY = (Hero->PositionY);
            if (WorldRandom() % 3)
            {
                if (HeroY > 50 && HeroY < 131)
                {
                    int dx = WorldRandom() % 1000 - 500;
                    vec3_t Position;
                    vec3_t Angle = {0.f, 0.f, 0.f};
                    vec3_t Light = {1.f, 1.f, 1.f};

                    if (dx < 30 && dx > 0)
                        dx = 100 + WorldRandom() % 30;
                    else if (dx > -30 && dx < 0)
                        dx -= 100 + WorldRandom() % 30;

                    Position[0] = Hero->Object.Position[0] + dx;
                    Position[2] = 500.f;

                    int SiegePositionY;
                    if (HeroY > 55)
                    {
                        SiegePositionY = 45;
                    }
                    else
                    {
                        SiegePositionY = HeroY - (WorldRandom() % 10 + 10);
                    }
                    Position[1] = SiegePositionY * TERRAIN_SCALE + (WorldRandom() % 40 - 20);
                    CreateEffect(MODEL_FLY_BIG_STONE1, Position, Angle, Light, 0);
                }
            }
            else
            {
                if (HeroY > 70 && HeroY < 177)
                {
                    int dx = WorldRandom() % 1000 - 500;
                    vec3_t Position;
                    vec3_t Angle = {0.f, 0.f, 0.f};
                    vec3_t Light = {1.f, 1.f, 1.f};

                    if (dx < 100 && dx > 0)
                        dx = 100 + WorldRandom() % 30;
                    else if (dx > -100 && dx < 0)
                        dx -= 100 + WorldRandom() % 30;

                    Position[0] = Hero->Object.Position[0] + dx;
                    Position[2] = 500.f;

                    int SiegePositionY;
                    if (HeroY < 179)
                    {
                        SiegePositionY = 179;
                    }
                    else
                    {
                        SiegePositionY = HeroY - (WorldRandom() % 10 + 10);
                    }
                    Position[1] = SiegePositionY * TERRAIN_SCALE - (WorldRandom() % 40 - 20);
                    CreateEffect(MODEL_FLY_BIG_STONE2, Position, Angle, Light, 0);
                }
            }
        }

        int HeroY = (Hero->PositionY);
        if ((HeroY > 58 && HeroY < 113) || (HeroY > 117 && HeroY < 159))
        {
            if (LastArrowEffectOnBattlefield < WorldTime - ArrowEffectOnBattlefieldInterval)
            {
                LastArrowEffectOnBattlefield = WorldTime;
                int length = (WorldRandom() % 4 + 2) / 2;
                int dx = (WorldRandom() % 1400 - 700) - 60.f * length;

                if (dx < 100 && dx > 0)
                    dx = 100 + WorldRandom() % 30;
                else if (dx > -100 && dx < 0)
                    dx -= 100 + WorldRandom() % 30;

                vec3_t Position;
                vec3_t Angle = {0.f, 0.f, 0.f};
                vec3_t Light = {1.f, 1.f, 1.f};
                for (int i = -length; i < length; ++i)
                {
                    dx += 60.f;

                    Position[0] = Hero->Object.Position[0] + dx;
                    Position[2] = 350.f;

                    bool attArrow = WorldRandom() % 2;
                    if (HeroY < 113)
                    {
                        attArrow = true;
                    }
                    else if (HeroY > 117)
                    {
                        attArrow = false;
                    }

                    if (attArrow)
                    {
                        int SiegePositionY = HeroY - (WorldRandom() % 1 + 10);
                        Position[1] = SiegePositionY * TERRAIN_SCALE;

                        Angle[2] = 180.f;
                        CreateEffect(MODEL_ARROW, Position, Angle, Light, 4);
                    }
                    else
                    {
                        int SiegePositionY = HeroY + (WorldRandom() % 1 + 10);
                        Position[1] = SiegePositionY * TERRAIN_SCALE;

                        CreateEffect(MODEL_ARROW, Position, Angle, Light, 4);
                    }
                }
            }
        }

        if (LastArrowEffectInStampRoom < WorldTime - ArrowEffectInStampRoomInterval)
        {
            LastArrowEffectInStampRoom = WorldTime;
            if (HeroY > 58 && HeroY < 159)
            {
                vec3_t Position;
                vec3_t Angle = {0.f, 0.f, 0.f};
                vec3_t Light = {1.f, 1.f, 1.f};

                int dx = WorldRandom() % 1400 - 700;
                if (dx < 100 && dx > 0)
                    dx = 100 + WorldRandom() % 30;
                else if (dx > -100 && dx < 0)
                    dx -= 100 + WorldRandom() % 30;

                Position[0] = Hero->Object.Position[0] + dx;
                Position[2] = 350.f;

                BYTE subtype = 3;
                if (HeroY < 82 || HeroY > 112 || rand_fps_check(10))
                {
                    subtype = 4;
                    if (rand_fps_check(10))
                    {
                        subtype = 3;
                    }
                }

                bool attArrow = WorldRandom() % 2;
                if (HeroY < 82)
                {
                    attArrow = true;
                }
                else if (HeroY > 112)
                {
                    attArrow = false;
                }

                if (attArrow)
                {
                    int SiegePositionY = HeroY - (WorldRandom() % 3 + 10);
                    Position[1] = SiegePositionY * TERRAIN_SCALE + (WorldRandom() % 60 - 30);

                    Angle[2] = 180.f;
                    CreateEffect(MODEL_ARROW, Position, Angle, Light, subtype);
                }
                else
                {
                    int SiegePositionY = HeroY + (WorldRandom() % 3 + 10);
                    Position[1] = SiegePositionY * TERRAIN_SCALE - (WorldRandom() % 60 - 30);

                    CreateEffect(MODEL_ARROW, Position, Angle, Light, subtype);
                }
            }
        }
    }
    return true;
}

bool CGMBattleCastle::MoveBattleCastleObject(OBJECT *o, int &object, int &visibleObject)
{
    if (gMapManager.InBattleCastle() == false)
        return false;

    switch (o->Type)
    {
    case 81:
        o->BlendMesh = 1;
        o->BlendMeshLight = 1.f;
        o->BlendMeshTexCoordV = WorldTime * 0.0002f;
        break;

    case 83:
        o->BlendMesh = 1;
        o->BlendMeshLight = 1.f;
        o->BlendMeshTexCoordV = -WorldTime * 0.0004f;
        break;

    case BATTLE_CASTLE_WALL1:
    case BATTLE_CASTLE_WALL2:
        if (IsBattleCastleStart() == false)
            break;

        if (o->ExtState == 0)
        {
            o->HiddenMesh = -1;
        }
        else if (o->ExtState == 99 && o->Visible)
        {
            if (o->HiddenMesh == -1)
            {
                CreateEffect(o->Type, o->Position, o->Angle, o->Light, 1);
                PlayBuffer(SOUND_BC_WALL_HIT);
            }
            o->HiddenMesh = 0;
        }
        break;
    case BATTLE_CASTLE_WALL3:
    case BATTLE_CASTLE_WALL4:
        if (IsBattleCastleStart() == false)
            break;

        if (o->ExtState == 0)
        {
            o->HiddenMesh = -1;
        }
        else if (o->ExtState == 99 && o->Visible)
        {
            if (o->HiddenMesh == -1)
            {
                CreateEffect(o->Type, o->Position, o->Angle, o->Light, 1);
                PlayBuffer(SOUND_BC_WALL_HIT);
            }
            o->HiddenMesh = 0;
        }
        break;
    }

    SetAttackDefenseObjectType(o);
    return false;
}

bool CGMBattleCastle::CreateBattleCastleObject(OBJECT *o)
{
    if (gMapManager.InBattleCastle() == false)
        return false;

    switch (o->Type)
    {
    case 8:
    case 9:
        o->ExtState = 0;
        break;

    case 7:
    case 10:
    case 13:
    case 14:
        //        case    18 :
        o->ExtState = 0;
        o->m_bCollisionCheck = true;
        break;

    case BATTLE_CASTLE_WALL1:
    case BATTLE_CASTLE_WALL2:
    case BATTLE_CASTLE_WALL3:
    case BATTLE_CASTLE_WALL4:
        o->ExtState = 0;
        o->m_bCollisionCheck = true;
        break;

    case 19:
        o->Scale = 1.f;
        break;

    case MODEL_BATTLE_GUARD2:
        o->Scale = 1.f;
        if (IsBattleCastleStart())
        {
            o->HiddenMesh = -2;
        }
        else
        {
            o->HiddenMesh = -1;
        }
        break;

    case 39:
        o->HiddenMesh = -2;
        break;

    case 41:
        o->Timer = float(WorldRandom() % 1000) * 0.01f;
        break;

    case 77:
    case 84:
        CreateOperate(o);
        break;

    case 79:
        CreateEffect(MODEL_TOWER_GATE_PLANE, o->Position, o->Angle, o->Light, 0, o);
        break;
    }

    SetAttackDefenseObjectType(o);
    return true;
}

CHARACTER *CGMBattleCastle::CreateBattleCastleMonster(EMonsterType Type, int PositionX,
                                                      int PositionY, int Key)
{
    if (gMapManager.InBattleCastle() == false)
        return NULL;

    CHARACTER *c = NULL;
    switch (Type)
    {
    case MONSTER_TRAP:
        c = CreateCharacter(Key, 11, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.m_bRenderShadow = false;
        break;

    case MONSTER_SHIELD:
        OpenNpc(MODEL_NPC_BARRIER); //  MODEL_NPC_BARRIER
        c = CreateCharacter(Key, MODEL_NPC_BARRIER, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->m_bIsSelected = false;
        c->Object.m_bRenderShadow = false;
        c->Object.Scale = 1.52f;
        c->Object.LifeTime = 0;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        break;

    case MONSTER_CROWN:
        OpenNpc(MODEL_NPC_CROWN); //  MODEL_NPC_CROWN
        c = CreateCharacter(Key, MODEL_NPC_CROWN, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.m_bRenderShadow = false;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        break;

    case MONSTER_CROWN_SWITCH1:
        OpenNpc(MODEL_NPC_CHECK_FLOOR); //  MODEL_NPC_CHECK_FLOOR
        c = CreateCharacter(Key, MODEL_NPC_CHECK_FLOOR, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        c->Object.Velocity = c->Object.Position[2];
        if (IsBattleCastleStart() == false)
            c->Object.Position[2] -= 100.f;
        break;

    case MONSTER_CROWN_SWITCH2:
        OpenNpc(MODEL_NPC_CHECK_FLOOR); //  MODEL_NPC_CHECK_FLOOR
        c = CreateCharacter(Key, MODEL_NPC_CHECK_FLOOR, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        c->Object.Velocity = c->Object.Position[2];
        if (IsBattleCastleStart() == false)
            c->Object.Position[2] -= 100.f;
        break;

    case MONSTER_CASTLE_GATE_SWITCH:
        OpenNpc(MODEL_NPC_GATE_SWITCH); //  MODEL_NPC_GATE_SWITCH
        c = CreateCharacter(Key, MODEL_NPC_GATE_SWITCH, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.Scale = 1.1f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        break;

    case MONSTER_GUARD:
        OpenNpc(77); //  MODEL_MONSTER01+77
        c = CreateCharacter(Key, MODEL_BATTLE_GUARD2, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.Scale = 1.1f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        break;

    case MONSTER_SLINGSHOT_ATTACK:
        OpenNpc(MODEL_NPC_CAPATULT_ATT);
        c = CreateCharacter(Key, MODEL_NPC_CAPATULT_ATT, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.m_bRenderShadow = false;
        c->Object.Scale = 0.8f;
        c->Object.m_fEdgeScale = 1.03f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        break;

    case MONSTER_SLINGSHOT_DEFENSE:
        OpenNpc(MODEL_NPC_CAPATULT_DEF); //  MODEL_NPC_CATAPULT_DEF
        c = CreateCharacter(Key, MODEL_NPC_CAPATULT_DEF, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.m_bRenderShadow = false;
        c->Object.Scale = 0.8f;
        c->Object.m_fEdgeScale = 1.03f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        break;

    case MONSTER_SENIOR:
        OpenNpc(MODEL_NPC_SENATUS); //  MODEL_NPC_SENATUS
        c = CreateCharacter(Key, MODEL_NPC_SENATUS, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.Scale = 1.1f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        break;

    case MONSTER_GUARDSMAN:
        OpenNpc(MODEL_NPC_CLERK); //
        c = CreateCharacter(Key, MODEL_NPC_CLERK, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.Scale = 1.f;
        c->Object.SubType = WorldRandom() % 2 + 10;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        break;

    case MONSTER_CASTLE_GATE1:
        OpenMonsterModel(MONSTER_MODEL_CASTLE_GATE1);
        c = CreateCharacter(Key, MODEL_CASTLE_GATE1, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.Scale = 1.0f;
        c->Object.m_bRenderShadow = false;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        break;

    case MONSTER_LIFE_STONE:
        OpenMonsterModel(MONSTER_MODEL_LIFE_STONE);
        c = CreateCharacter(Key, MODEL_LIFE_STONE, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.Scale = 1.0f;
        c->Object.m_bRenderShadow = false;
        c->Object.BlendMesh = 3;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;

        CreateEffect(MODEL_AURORA, c->Object.Position, c->Object.Angle, c->Object.Light, 0,
                     &c->Object, 120);
        break;

    case MONSTER_GUARDIAN_STATUE:
        OpenMonsterModel(MONSTER_MODEL_GUARDIAN_STATUE);
        c = CreateCharacter(Key, MODEL_GUARDIAN_STATUE, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.Scale = 1.f;
        c->Object.m_fEdgeScale = 1.03f;
        c->Object.m_bRenderShadow = false;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;

        if (IsBattleCastleStart())
        {
            c->Object.HiddenMesh = -1;
        }
        else
        {
            c->Object.HiddenMesh = -2;
        }
        CreateEffect(MODEL_AURORA, c->Object.Position, c->Object.Angle, c->Object.Light, 0,
                     &c->Object, 120);
        break;

    case MONSTER_GUARDIAN:
        OpenMonsterModel(MONSTER_MODEL_GUARDIAN_STATUE);
        c = CreateCharacter(Key, MODEL_GUARDIAN_STATUE, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.Scale = 1.f;
        c->Object.m_bRenderShadow = false;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        break;

    case MONSTER_BATTLE_GUARD1:
        OpenMonsterModel(MONSTER_MODEL_BATTLE_GUARD1);
        c = CreateCharacter(Key, MODEL_BATTLE_GUARD1, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.Scale = 1.f;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        break;

    case MONSTER_BATTLE_GUARD2:
        OpenMonsterModel(MONSTER_MODEL_BATTLE_GUARD2);
        c = CreateCharacter(Key, MODEL_BATTLE_GUARD2, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.Scale = 1.f;
        c->Object.SubType = 30;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;
        break;

    case MONSTER_CANON_TOWER:
        OpenMonsterModel(MONSTER_MODEL_CANON_TOWER);
        c = CreateCharacter(Key, MODEL_CANON_TOWER, PositionX, PositionY);
        c->NotRotateOnMagicHit = true;
        c->Object.Scale = 1.f;
        c->Object.m_fEdgeScale = 1.04f;
        c->Object.m_bRenderShadow = false;
        c->Weapon[0].Type = -1;
        c->Weapon[1].Type = -1;

        if (IsBattleCastleStart())
        {
            c->Object.HiddenMesh = -1;
        }
        else
        {
            c->Object.HiddenMesh = -2;
        }
        c->Object.HiddenMesh = -1;
        break;
    }

    return c;
}

bool CGMBattleCastle::SettingBattleCastleMonsterLinkBone(CHARACTER *c, int Type)
{
    switch (Type)
    {
    case MODEL_NPC_CAPATULT_ATT:
        Vector(-200.f, -50.f, -100.f, c->Object.BoundingBoxMin);
        Vector(200.f, 50.f, 100.f, c->Object.BoundingBoxMax);
        break;

    case MODEL_CASTLE_GATE1:
        Vector(-140.f, -140.f, 0.f, c->Object.BoundingBoxMin);
        Vector(140.f, 140.f, 300.f, c->Object.BoundingBoxMax);
        c->SwordCount = 0;
        return true;
    }

    return false;
}

bool CGMBattleCastle::StopBattleCastleMonster(CHARACTER *c, OBJECT *o)
{
    if (gMapManager.InBattleCastle() == false)
        return false;

    switch (c->MonsterIndex)
    {
    case MONSTER_BATTLE_GUARD1:
    case MONSTER_BATTLE_GUARD2:
        if (o->CurrentAction == 5)
        {
            o->PriorAction = 4;
            o->PriorAnimationFrame = 0.f;
            o->CurrentAction = 4;
            o->AnimationFrame = 0.f;

            g_byGuardAI = BattleCastleDetail::GUARD_ATTACK_READY;
            o->AI = BattleCastleDetail::GUARD_READY;
            o->Timer = 300;
            return true;
        }
        else if (o->CurrentAction == 4)
        {
            o->PriorAction = 4;
            o->PriorAnimationFrame = 0.f;
            o->CurrentAction = 4;
            o->AnimationFrame = 0.f;
            return true;
        }
        else if (o->CurrentAction == 2)
        {
            o->PriorAction = 4;
            o->PriorAnimationFrame = 0.f;
            o->CurrentAction = 4;
            o->AnimationFrame = 0.f;
            return true;
        }
        else if (o->CurrentAction == 0)
        {
            o->PriorAction = 0;
            o->PriorAnimationFrame = 0.f;
            o->CurrentAction = 0;
            o->AnimationFrame = 0.f;
            return true;
        }
        return false;

    case MONSTER_SLINGSHOT_ATTACK:
    case MONSTER_SLINGSHOT_DEFENSE:
        SetAction(o, 0);
        return true;

    case MONSTER_CASTLE_GATE1: {
        if (g_isCharacterBuff(o, eBuff_CastleGateIsOpen))
        {
            c->m_bIsSelected = false;
            SetAction(o, 2);
        }
        else
        {
            c->m_bIsSelected = true;
            SetAction(o, 0);
        }
    }
        return true;
    }
    return false;
}

void CGMBattleCastle::InitGateAttribute(void)
{
    if (g_bBeGate == false)
    {
        SetCastleGate_Attribute(0, 0, 0, true);
    }
}

void CGMBattleCastle::BeginCharacterTick()
{
    g_fLifeStoneLocation[0] = 0.f;
    g_fLifeStoneLocation[1] = 0.f;
    g_bBeGate = false;
}

void CGMBattleCastle::ObserveCharacterTick(const CHARACTER &character)
{
    const auto &object = character.Object;
    if (!object.Live)
        return;
    if (character.MonsterIndex == MONSTER_CASTLE_GATE1)
        g_bBeGate = true;
    if (character.MonsterIndex == MONSTER_LIFE_STONE && character.m_byFriend == 128)
    {
        g_fLifeStoneLocation[0] = object.Position[0];
        g_fLifeStoneLocation[1] = object.Position[1];
    }
}

void CGMBattleCastle::FinishCharacterTick()
{
    InitGateAttribute();
}

bool CGMBattleCastle::MoveBattleCastleMonster(CHARACTER *c, OBJECT *o)
{
    if (gMapManager.InBattleCastle() == false)
        return false;

    switch (c->MonsterIndex)
    {
    case MONSTER_CROWN:
        o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) + 200.f;
        g_isCrownState = g_isCharacterBuff(o, eBuff_CastleCrown);
        c->m_bIsSelected = (g_isCrownState ? true : false);
        break;

    case MONSTER_GUARDIAN_STATUE:
        o->HiddenMesh = -1;
        c->m_bIsSelected = true;
        if (IsBattleCastleStart() == false)
        {
            o->HiddenMesh = -2;
            c->m_bIsSelected = false;
        }
        break;

    case MONSTER_BATTLE_GUARD1:
    case MONSTER_BATTLE_GUARD2: {
        switch (o->AI)
        {
        case BattleCastleDetail::GUARD_STOP: {
            float fRange = 500.f;

            o->Timer = 0;

            if (CalcDistanceChrToChr(o, 1, fRange) ||
                g_byGuardAI == BattleCastleDetail::GUARD_ATTACK_READY)
            {
                SetAction(o, 4);

                g_byGuardAI = BattleCastleDetail::GUARD_ATTACK_READY;
                o->AI = BattleCastleDetail::GUARD_READY;
                o->Timer = 300;
            }
        }
        break;

        case BattleCastleDetail::GUARD_READY:
            break;

        case BattleCastleDetail::GUARD_ATTACK_READY:
            break;

        case BattleCastleDetail::GUARD_ATTACK:
            break;
        }
        if (o->CurrentAction != MONSTER01_SHOCK && o->AI != BattleCastleDetail::GUARD_STOP &&
            (o->Timer < 0 || g_byGuardAI == BattleCastleDetail::GUARD_STOP))
        {
            float fRange = 500.f;

            if (CalcDistanceChrToChr(o, 1, fRange)) //|| g_byGuardAI==GUARD_ATTACK_READY )
            {
                o->Timer = 300;
                g_byGuardAI = BattleCastleDetail::GUARD_ATTACK_READY;
            }
            else
            {
                SetAction(o, 0);
                o->AI = BattleCastleDetail::GUARD_STOP;
                g_byGuardAI = BattleCastleDetail::GUARD_STOP;
            }
        }
        o->Timer -= FPS_ANIMATION_FACTOR;
    }
    break;
    }
    return false;
}

bool CGMBattleCastle::SetCurrentAction_BattleCastleMonster(CHARACTER *c, OBJECT *o)
{
    switch (o->Type)
    {
    case 11:
        break;

    case MODEL_BATTLE_GUARD1:
    case MODEL_BATTLE_GUARD2:
        o->AI = BattleCastleDetail::GUARD_ATTACK;
        SetAction(o, 5);
        return true;
    }
    return false;
}
void CGMBattleCastle::AdvanceStructureState(OBJECT *object, BMD *model)
{
    if (!gMapManager.InBattleCastle())
        return;
    const bool gate = object->Type == MODEL_CASTLE_GATE1;
    const bool statue = object->Type == MODEL_GUARDIAN_STATUE && IsBattleCastleStart();
    if (!gate && !statue)
        return;
    if (object->CurrentAction != MONSTER01_DIE)
    {
        if (statue)
            object->HiddenMesh = -1;
        return;
    }
    object->Live = false;
}

bool CGMBattleCastle::CreateObject(OBJECT *object)
{
    return CreateBattleCastleObject(object);
}

bool CGMBattleCastle::MoveObject(OBJECT *object)
{
    return MoveBattleCastleVisual(object);
}

void CGMBattleCastle::AdvanceMonsterState(CHARACTER &character, BMD &model)
{
    AdvanceStructurePresentationState(character);
    AdvanceStructureState(&character.Object, &model);
}

void CGMBattleCastle::InstallBehavior()
{
    LoadWaveFile(SOUND_BC_AMBIENT, L"Data\\Sound\\BattleCastle\\aSiegeAmbi.wav", 1, true);
    LoadWaveFile(SOUND_BC_AMBIENT_BATTLE1, L"Data\\Sound\\BattleCastle\\RanAmbi1.wav", 1, true);
    LoadWaveFile(SOUND_BC_AMBIENT_BATTLE2, L"Data\\Sound\\BattleCastle\\RanAmbi2.wav", 1, true);
    LoadWaveFile(SOUND_BC_AMBIENT_BATTLE3, L"Data\\Sound\\BattleCastle\\RanAmbi3.wav", 1, true);
    LoadWaveFile(SOUND_BC_AMBIENT_BATTLE4, L"Data\\Sound\\BattleCastle\\RanAmbi4.wav", 1, true);
    LoadWaveFile(SOUND_BC_AMBIENT_BATTLE5, L"Data\\Sound\\BattleCastle\\RanAmbi5.wav", 1, true);
    LoadWaveFile(SOUND_BC_GUARD_STONE_DIS, L"Data\\Sound\\BattleCastle\\oGuardStoneDis.wav", 1,
                 true);
    LoadWaveFile(SOUND_BC_SHIELD_SPACE_DIS, L"Data\\Sound\\BattleCastle\\oProtectionDis.wav", 1,
                 true);
    LoadWaveFile(SOUND_BC_CATAPULT_ATTACK, L"Data\\Sound\\BattleCastle\\oSWFire.wav", 1, true);
    LoadWaveFile(SOUND_BC_CATAPULT_HIT, L"Data\\Sound\\BattleCastle\\oSWHitG.wav", MAX_CHANNEL,
                 true);
    LoadWaveFile(SOUND_BC_WALL_HIT, L"Data\\Sound\\BattleCastle\\oSWHit.wav", MAX_CHANNEL, true);

    LoadWaveFile(SOUND_BC_GATE_OPEN, L"Data\\Sound\\BattleCastle\\oCDoorMove.wav", 1, true);
    LoadWaveFile(SOUND_BC_GUARDIAN_ATTACK, L"Data\\Sound\\BattleCastle\\mGMercAttack.wav", 1, true);
    LoadWaveFile(SOUND_BMS_STUN, L"Data\\Sound\\BattleCastle\\sDStun.wav", MAX_CHANNEL, true);
    LoadWaveFile(SOUND_BMS_STUN_REMOVAL, L"Data\\Sound\\BattleCastle\\sDStunCancel.wav",
                 MAX_CHANNEL, true);
    LoadWaveFile(SOUND_BMS_MANA, L"Data\\Sound\\BattleCastle\\sDSwllMana.wav", MAX_CHANNEL, true);
    LoadWaveFile(SOUND_BMS_INVISIBLE, L"Data\\Sound\\BattleCastle\\sDTrans.wav", MAX_CHANNEL, true);
    LoadWaveFile(SOUND_BMS_VISIBLE, L"Data\\Sound\\BattleCastle\\sDStunCancel.wav", MAX_CHANNEL,
                 true);
    LoadWaveFile(SOUND_BMS_MAGIC_REMOVAL, L"Data\\Sound\\BattleCastle\\sDMagicCancel.wav",
                 MAX_CHANNEL, true);
    LoadWaveFile(SOUND_BCS_RUSH, L"Data\\Sound\\BattleCastle\\sCHaveyBlow.wav", MAX_CHANNEL, true);
    LoadWaveFile(SOUND_BCS_JAVELIN, L"Data\\Sound\\BattleCastle\\sCShockWave.wav", MAX_CHANNEL,
                 true);
    LoadWaveFile(SOUND_BCS_DEEP_IMPACT, L"Data\\Sound\\BattleCastle\\sCFireArrow.wav", MAX_CHANNEL,
                 true);
    LoadWaveFile(SOUND_BCS_DEATH_CANON, L"Data\\Sound\\BattleCastle\\sCMW.wav", MAX_CHANNEL, true);
    LoadWaveFile(SOUND_BCS_ONE_FLASH, L"Data\\Sound\\BattleCastle\\sCColdAttack.wav", MAX_CHANNEL,
                 true);
    LoadWaveFile(SOUND_BCS_SPACE_SPLIT, L"Data\\Sound\\BattleCastle\\sCDarkAttack.wav", MAX_CHANNEL,
                 true);
    LoadWaveFile(SOUND_BCS_BRAND_OF_SKILL, L"Data\\Sound\\BattleCastle\\sCDarkAssist.wav", 1, true);
}

bool CGMBattleCastle::StopMonster(CHARACTER *character, OBJECT *object)
{
    if (!StopBattleCastleMonster(character, object))
        return false;
    CharacterAnimation(character, object);
    return true;
}

MapObjectInteraction CGMBattleCastle::ObjectInteraction(int type, CHARACTER &actor)
{
    using Action = MapObjectInteraction::Action;
    if (type == 84 || (type == 77 && GetGuildMaster(&actor)))
        return {Action::Sit, true};
    return {};
}

bool CGMBattleCastle::CanObserveCharacter(const CHARACTER &character)
{
    if (&character == Hero || !g_bBattleCastleStart ||
        !character.Object.m_BuffMap.isBuff(eBuff_Cloaking))
        return true;
    const auto sameTeam = [&character](int king, int member) {
        return character.EtcPart == king || character.EtcPart == member;
    };
    switch (Hero->EtcPart)
    {
    case PARTS_ATTACK_KING_TEAM_MARK:
    case PARTS_ATTACK_TEAM_MARK:
        return sameTeam(PARTS_ATTACK_KING_TEAM_MARK, PARTS_ATTACK_TEAM_MARK);
    case PARTS_ATTACK_KING_TEAM_MARK2:
    case PARTS_ATTACK_TEAM_MARK2:
        return sameTeam(PARTS_ATTACK_KING_TEAM_MARK2, PARTS_ATTACK_TEAM_MARK2);
    case PARTS_ATTACK_KING_TEAM_MARK3:
    case PARTS_ATTACK_TEAM_MARK3:
        return sameTeam(PARTS_ATTACK_KING_TEAM_MARK3, PARTS_ATTACK_TEAM_MARK3);
    case PARTS_DEFENSE_KING_TEAM_MARK:
    case PARTS_DEFENSE_TEAM_MARK:
        return sameTeam(PARTS_DEFENSE_KING_TEAM_MARK, PARTS_DEFENSE_TEAM_MARK);
    default:
        return true;
    }
}

#define NUM_HELLAS 7

#define KUNDUN_ZONE NUM_HELLAS

CGMHellasPtr CGMHellas::Make(SessionKeeper &keeper)
{
    return CGMHellasPtr(new CGMHellas(keeper));
}

CGMHellas::CGMHellas(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject()), g_Camera(keeper.CameraStateObject()),
      cameraProjection_(keeper.CameraProjectionObject()), g_RenderText(keeper.SessionText())
{
}

CGMHellas::~CGMHellas()
{
    DeleteWaterTerrain();
}

bool CGMHellas::CreateWaterTerrain(int mapIndex)
{
    if (gMapManager.InHellas(mapIndex))
    {
        DeleteWaterTerrain();

        g_pCSWaterTerrain = std::make_unique<CSWaterTerrain>(mapIndex, sessionKeeper_);

        return true;
    }
    else
    {
        DeleteWaterTerrain();
    }

    return false;
}

bool CGMHellas::IsWaterTerrain(void)
{
    if (g_pCSWaterTerrain != nullptr)
    {
        return true;
    }
    return false;
}

void CGMHellas::AddWaterWave(int x, int y, int range, int height)
{
    if (g_pCSWaterTerrain != nullptr)
    {
        int WaveX = (x * 2);
        int WaveY = (y * 2);
        if (const auto remaining = sessionKeeper_.Gameplay()->WaterWaveRemainingFrames())
            g_pCSWaterTerrain->QueueWave(WaveX, WaveY, range, range, height, *remaining);
        else
            g_pCSWaterTerrain->addSineWave(WaveX, WaveY, range, range, height);
    }
}

void CGMHellas::DeleteWaterTerrain(void)
{
    g_pCSWaterTerrain.reset();
}

float CGMHellas::GetWaterTerrain(float x, float y)
{
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (g_pCSWaterTerrain != nullptr)
    {
        return g_pCSWaterTerrain->GetWaterTerrain(x, y);
    }
    return 0.f;
}

void CGMHellas::SettingHellasColor()
{
    glClearColor(0.f, 0.f, 0.f, 1.f);
}

BYTE CGMHellas::GetHellasLevel(CLASS_TYPE Class, int Level)
{
    int startIndex = 0;
    int baseClass = gCharacterManager.GetBaseClass(Class);
    if (baseClass == CLASS_DARK || baseClass == CLASS_DARK_LORD || baseClass == CLASS_RAGEFIGHTER)
    {
        startIndex = NUM_HELLAS;
    }

    int byLevel = 0;
    for (int i = 0; i < NUM_HELLAS; i++)
    {
        byLevel++;
        if (Level >= HellasDetail::g_iKalimaLevel[startIndex + i][0] &&
            Level <= HellasDetail::g_iKalimaLevel[startIndex + i][1])
        {
            break;
        }
    }
    return byLevel;
}

bool CGMHellas::EnableKalima(CLASS_TYPE Class, int Level, int ItemLevel)
{
    int startIndex = 0;

    auto baseClass = gCharacterManager.GetBaseClass(Class);
    if (baseClass == CLASS_DARK || baseClass == CLASS_DARK_LORD || baseClass == CLASS_RAGEFIGHTER)
    {
        startIndex = NUM_HELLAS;
    }

    if (Level < HellasDetail::g_iKalimaLevel[startIndex + ItemLevel - 1][0])
    {
        return false;
    }

    return true;
}

bool CGMHellas::GetUseLostMap(bool bDrawAlert)
{
    int Level = CharacterAttribute->Level;

    int startIndex = 0;

    int baseClass = gCharacterManager.GetBaseClass(Hero->Class);
    if (baseClass == CLASS_DARK || baseClass == CLASS_DARK_LORD || baseClass == CLASS_RAGEFIGHTER)
    {
        startIndex = NUM_HELLAS;
    }

    if (bDrawAlert && Hero->SafeZone)
    {
        g_pSystemLogBox->AddText(I18N::Game::CanTBeUsedInTheSafeZone, SEASON3B::TYPE_ERROR_MESSAGE);
        return false;
    }

    if (Level >= HellasDetail::g_iKalimaLevel[startIndex][0])
    {
        return true;
    }

    if (bDrawAlert)
    {
        wchar_t Text[100];
        mu_swprintf(Text, I18N::Game::OnlyAboveLevelDCanUse,
                    HellasDetail::g_iKalimaLevel[startIndex][0]);
        g_pSystemLogBox->AddText(Text, SEASON3B::TYPE_ERROR_MESSAGE);
    }

    return false;
}

void CGMHellas::AddObjectDescription(wchar_t *Text, vec3_t position)
{
    ObjectDescript QD;

    memcpy(QD.m_strName, Text, sizeof(char) * 64);
    VectorCopy(position, QD.m_vPos);

    g_qObjDes.push(QD);
}

// every 4 seconds.

// every 2 seconds.

bool CGMHellas::MoveHellasObjectSetting(int &objCount, int object)
{
    if (gMapManager.InHellas() == false)
        return false;

    PlayBuffer(SOUND_KALIMA_AMBIENT);

    if (LastAmbientSoundPlay < WorldTime - HellasDetail::AmbientSoundInterval)
    {
        LastAmbientSoundPlay = WorldTime;
        PlayBuffer(static_cast<ESound>(SOUND_KALIMA_AMBIENT2 + WorldRandom() % 2));
    }

    if (GetHellasLevel(Hero->Class, CharacterAttribute->Level) == KUNDUN_ZONE)
    {
        int CurrX = (Hero->PositionX);
        int CurrY = (Hero->PositionY);

        if ((CurrX >= 25 && CurrY >= 44) && (CurrX <= 51 && CurrY <= 119) &&
            (LastKundunSoundPlay < WorldTime - HellasDetail::KundunSoundInterval))
        {
            LastKundunSoundPlay = WorldTime;
            PlayBuffer(static_cast<ESound>(SOUND_KUNDUN_AMBIENT1 + WorldRandom() % 2));
        }
    }

    if (rand_fps_check(10) && object)
    {
        objCount = WorldRandom() % object;
    }

    const float ambientRate = (object > 0 ? 0.9f : 1.f) / 5.f;
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR * ambientRate))
    {
        vec3_t Position, Light;

        Vector(0.3f, 0.8f, 1.f, Light);

        const float fraction = birth.FrameFraction();
        Hero->Object.MotionTrace.Sample(WorldTime, fraction, Hero->Object.Position, Position);
        Position[0] += WorldRandom() % 800 - 400.f;
        Position[1] += WorldRandom() % 800 - 400.f;
        Position[2] += 50.f;

        CreateParticle(BITMAP_LIGHT, Position, Hero->Object.Angle, Light, 7, 1.f, &Hero->Object);

        if (sessionKeeper_.Random()->FpsCheck(15, 1.f))
        {
            vec3_t Angle = {0.f, 0.f, 0.f};
            Position[2] += 750.f;
            CreateEffect(9, Position, Angle, Light);

            PlayBuffer(SOUND_KALIMA_FALLING_STONE);
        }
    }

    return true;
}

bool CGMHellas::MoveHellasObject(OBJECT *o, int &object, int &visibleObject)
{
    if (gMapManager.InHellas() == true)
    {
        return true;
    }

    return false;
}

bool CGMHellas::MoveHellasAllObject(OBJECT *o)
{
    if (gMapManager.InHellas() == false)
        return false;

    return true;
}

bool CGMHellas::CreateHellasObject(OBJECT *o)
{
    return false;
}

void CGMHellas::CheckGrass(OBJECT *o)
{
    vec3_t Position;
    VectorCopy(Hero->Object.Position, Position);
    if (Hero->Object.CurrentAction >= PLAYER_WALK_MALE &&
            Hero->Object.CurrentAction <= PLAYER_RUN_RIDE_WEAPON ||
        Hero->Object.CurrentAction >= PLAYER_FENRIR_RUN &&
            Hero->Object.CurrentAction <= PLAYER_FENRIR_RUN_ONE_LEFT_ELF ||
        Hero->Object.CurrentAction >= PLAYER_FENRIR_WALK &&
            Hero->Object.CurrentAction <= PLAYER_FENRIR_WALK_ONE_LEFT ||
        Hero->Object.CurrentAction >= PLAYER_RAGE_FENRIR_RUN &&
            Hero->Object.CurrentAction <= PLAYER_RAGE_FENRIR_RUN_ONE_LEFT ||
        Hero->Object.CurrentAction >= PLAYER_RAGE_FENRIR_WALK &&
            Hero->Object.CurrentAction <= PLAYER_RAGE_FENRIR_WALK_TWO_SWORD ||
        Hero->Object.CurrentAction >= PLAYER_RAGE_UNI_RUN &&
            Hero->Object.CurrentAction >= PLAYER_RAGE_UNI_RUN_ONE_RIGHT)
    {
        if (o->Direction[0] < 0.1f)
        {
            float dx = Position[0] - o->Position[0];
            float dy = Position[1] - o->Position[1];
            float Distance = sqrtf(dx * dx + dy * dy);
            if (Distance < 50.f)
            {
                Vector(-dx * 0.6f, -dy * 0.6f, 0.f, o->Direction);
            }
        }

        MoveDampedObject(o, 0.6f, FPS_ANIMATION_FACTOR);
    }
}

// every 4 seconds.

int CGMHellas::CreateBigMon(OBJECT *o)
{
    if (gMapManager.InHellas() == false)
        return 0;

    if (LastBigMonCreation < WorldTime - HellasDetail::BigMonInterval)
    {
        LastBigMonCreation = WorldTime;
        o->Live = true;
        OpenMonsterModel(MONSTER_MODEL_BAHAMUT);
        o->Type = MODEL_BAHAMUT;
        o->Scale = 2.5f + (float)(WorldRandom() % 3 + 6) * 0.05f;
        o->Alpha = 1.f;
        o->AlphaTarget = o->Alpha;
        o->LightEnable = false;
        o->Velocity = (float)(WorldRandom() % 10 + 10) * 0.04f;
        o->Gravity = WorldRandom() % 3 - 1.5f;
        o->LightEnable = true;
        o->AlphaEnable = false;
        o->SubType = 0;
        o->HiddenMesh = 5;
        o->BlendMesh = -1;
        o->LifeTime = 200;
        o->CurrentAction = MONSTER01_WALK;
        SetAction(o, o->CurrentAction);
        Vector(0.f, 0.f, 90.f - WorldRandom() % 30 - 15, o->Angle);
        Vector(Hero->Object.Position[0] - 1000 - WorldRandom() % 200,
               Hero->Object.Position[1] - 500 + WorldRandom() % 200,
               Hero->Object.Position[2] - 800.f, o->Position);
    }
    else
    {
        o->Live = false;
    }
    return 1;
}

void CGMHellas::CreateMonsterSkill_ReduceDef(OBJECT *o, int AttackTime, BYTE time, float Height)
{
    if (AttackTime >= time)
    {
        vec3_t Angle, Light, Position, p;

        Vector(0.f, 0.f, 0.f, Light);
        Vector(0.f, 0.f, 0.f, p);
        VectorCopy(o->Position, Position);

        Position[2] += Height;
        for (int i = 0; i < 3; i++)
        {
            Vector(0.f, 0.f, i * 120.f, Angle);
            CreateEffectFpsChecked(MODEL_SKULL, Position, Angle, Light, 1, o);
        }

        PlayBuffer(SOUND_SKILL_SKULL);
    }
}

void CGMHellas::CreateMonsterSkill_Poison(OBJECT *o, int AttackTime, BYTE time)
{
    if (AttackTime >= time)
    {
        float Matrix[3][4];
        vec3_t Angle, Light, Position, p;

        Vector(0.f, 0.f, (float)(WorldRandom() % 360), Angle);
        Vector(0.f, 300.f, 0.f, p);
        Vector(0.8f, 0.5f, 0.1f, Light);
        for (int i = 0; i < 5; i++)
        {
            Angle[2] += 72.f;
            AngleMatrix(Angle, Matrix);
            VectorRotate(p, Matrix, Position);
            VectorAdd(o->Position, Position, Position);

            CreateEffectFpsChecked(MODEL_FIRE, Position, o->Angle, Light, 8, NULL, 0);
        }

        PlayBuffer(SOUND_GREAT_POISON);
    }
}

void CGMHellas::CreateMonsterSkill_Summon(OBJECT *o, int AttackTime, BYTE time)
{
    if (AttackTime >= time)
    {
        CreateEffect(MODEL_CIRCLE, o->Position, o->Angle, o->Light, 3, o);
        CreateEffect(MODEL_CIRCLE_LIGHT, o->Position, o->Angle, o->Light, 4);
    }
}

void CGMHellas::SetActionDestroy_Def(OBJECT *o)
{
    if (o->Type != MODEL_PLAYER)
    {
        if (g_isCharacterBuff(o, eBuff_WizDefense))
        {
            o->AI = HellasDetail::ACTION_DESTROY_WIZ_DEF;
            g_CharacterUnRegisterBuff(o, eBuff_WizDefense);
        }
        else if (g_isCharacterBuff(o, eBuff_Defense))
        {
            o->AI = HellasDetail::ACTION_DESTROY_DEF;
            g_CharacterUnRegisterBuff(o, eBuff_Defense);
        }
    }
}

CHARACTER *CGMHellas::CreateHellasMonster(EMonsterType Type, int PositionX, int PositionY, int Key)
{
    CHARACTER *c = NULL;
    OBJECT *o = nullptr;
    switch (Type)
    {
    case MONSTER_DEATH_ANGEL_1:
    case MONSTER_DEATH_ANGEL_2:
    case MONSTER_DEATH_ANGEL_3:
    case MONSTER_DEATH_ANGEL_4:
    case MONSTER_DEATH_ANGEL_5:
    case MONSTER_DEATH_ANGEL_6:
    case MONSTER_DEATH_ANGEL_7:
        OpenMonsterModel(MONSTER_MODEL_DEATH_ANGEL);
        c = CreateCharacter(Key, MODEL_DEATH_ANGEL, PositionX, PositionY);
        c->Weapon[0].Type = -1;
        c->Weapon[0].Level = 0;
        c->Object.Scale = 1.2f;
        o = &c->Object;
        o->BlendMesh = 1;
        wcscpy(c->ID, L"장수거북");
        break;
    case MONSTER_DEATH_CENTURION_1:
    case MONSTER_DEATH_CENTURION_2:
    case MONSTER_DEATH_CENTURION_3:
    case MONSTER_DEATH_CENTURION_4:
    case MONSTER_DEATH_CENTURION_5:
    case MONSTER_DEATH_CENTURION_6:
    case MONSTER_DEATH_CENTURION_7:
        OpenMonsterModel(MONSTER_MODEL_DEATH_CENTURION);
        c = CreateCharacter(Key, MODEL_DEATH_CENTURION, PositionX, PositionY);
        c->Weapon[0].Type = MODEL_DRAGON_SPEAR;
        c->Weapon[0].Level = 7;
        c->Object.Scale = 1.5f;
        o = &c->Object;
        o->SubType = 9;
        o->BlendMesh = 0;
        wcscpy(c->ID, L"대형 블루나이트");
        break;
    case MONSTER_BLOOD_SOLDIER_1:
    case MONSTER_BLOOD_SOLDIER_2:
    case MONSTER_BLOOD_SOLDIER_3:
    case MONSTER_BLOOD_SOLDIER_4:
    case MONSTER_BLOOD_SOLDIER_5:
    case MONSTER_BLOOD_SOLDIER_6:
    case MONSTER_BLOOD_SOLDIER_7:
        OpenMonsterModel(MONSTER_MODEL_BLOOD_SOLDIER);
        c = CreateCharacter(Key, MODEL_BLOOD_SOLDIER, PositionX, PositionY);
        c->Weapon[0].Type = -1;
        c->Weapon[0].Level = 0;
        c->Object.Scale = 0.8f;
        o = &c->Object;
        wcscpy(c->ID, L"랍스터");
        break;
    case MONSTER_AEGIS_1:
    case MONSTER_AEGIS_2:
    case MONSTER_AEGIS_3:
    case MONSTER_AEGIS_4:
    case MONSTER_AEGIS_5:
    case MONSTER_AEGIS_6:
    case MONSTER_AEGIS_7:
        OpenMonsterModel(MONSTER_MODEL_AEGIS);
        c = CreateCharacter(Key, MODEL_AEGIS, PositionX, PositionY);
        c->Weapon[0].Type = -1;
        c->Weapon[0].Level = 0;
        c->Object.Scale = 1.4f;
        o = &c->Object;
        o->BlendMesh = 1;
        wcscpy(c->ID, L"가오리");
        break;
    case MONSTER_ROGUE_CENTURION_1:
    case MONSTER_ROGUE_CENTURION_2:
    case MONSTER_ROGUE_CENTURION_3:
    case MONSTER_ROGUE_CENTURION_4:
    case MONSTER_ROGUE_CENTURION_5:
    case MONSTER_ROGUE_CENTURION_6:
    case MONSTER_ROGUE_CENTURION_7:
        OpenMonsterModel(MONSTER_MODEL_DEATH_CENTURION);
        c = CreateCharacter(Key, MODEL_DEATH_CENTURION, PositionX, PositionY);
        c->Weapon[0].Type = MODEL_DRAGON_SPEAR;
        c->Weapon[0].Level = 7;
        c->Object.Scale = 1.f;
        o = &c->Object;
        o->BlendMesh = 0;
        wcscpy(c->ID, L"블루나이트");
        break;
    case MONSTER_NECRON_1:
    case MONSTER_NECRON_2:
    case MONSTER_NECRON_3:
    case MONSTER_NECRON_4:
    case MONSTER_NECRON_5:
    case MONSTER_NECRON_6:
    case MONSTER_NECRON_7:
        OpenMonsterModel(MONSTER_MODEL_NECRON);
        c = CreateCharacter(Key, MODEL_NECRON, PositionX, PositionY);
        c->Weapon[0].Type = -1;
        c->Weapon[0].Level = 7;
        c->Object.Scale = 1.2f;
        o = &c->Object;
        o->BlendMesh = 3;
        wcscpy(c->ID, L"마린보이");
        break;
    case MONSTER_SCHRIKER_1:
    case MONSTER_SCHRIKER_2:
    case MONSTER_SCHRIKER_3:
    case MONSTER_SCHRIKER_4:
    case MONSTER_SCHRIKER_5:
    case MONSTER_SCHRIKER_6:
    case MONSTER_SCHRIKER_7:
        OpenMonsterModel(MONSTER_MODEL_SHRIKER);
        c = CreateCharacter(Key, MODEL_SHRIKER, PositionX, PositionY);
        c->Weapon[0].Type = MODEL_DOUBLE_BLADE;
        c->Weapon[0].Level = 0;
        c->Weapon[1].Type = MODEL_DOUBLE_BLADE;
        c->Weapon[1].Level = 0;
        c->Object.Scale = 1.2f;
        o = &c->Object;
        wcscpy(c->ID, L"쿤둔후보");
        break;
    case MONSTER_ILLUSION_OF_KUNDUN_1:
    case MONSTER_ILLUSION_OF_KUNDUN_2:
    case MONSTER_ILLUSION_OF_KUNDUN_3:
    case MONSTER_ILLUSION_OF_KUNDUN_4:
    case MONSTER_ILLUSION_OF_KUNDUN_5:
    case MONSTER_ILLUSION_OF_KUNDUN_6:
        OpenMonsterModel(MONSTER_MODEL_SHRIKER);
        c = CreateCharacter(Key, MODEL_SHRIKER, PositionX, PositionY);
        c->Weapon[0].Type = MODEL_DOUBLE_BLADE;
        c->Weapon[0].Level = 0;
        c->Weapon[1].Type = MODEL_DOUBLE_BLADE;
        c->Weapon[1].Level = 0;
        o = &c->Object;
        o->SubType = 9;
        o->Scale = 1.5f;
        wcscpy(c->ID, L"쿤둔후보");
        break;

    case MONSTER_ILLUSION_OF_KUNDUN_7:
        OpenMonsterModel(MONSTER_MODEL_ILLUSION_OF_KUNDUN);
        c = CreateCharacter(Key, MODEL_ILLUSION_OF_KUNDUN, PositionX, PositionY);
        c->Weapon[1].Type = MODEL_STAFF_OF_KUNDUN;
        c->Weapon[1].Level = 0;
        c->Object.Scale = 2.0f;
        //		c->Object.Scale = 1.9f;
        o = &c->Object;
        wcscpy(c->ID, L"진짜쿤둔");
        o->LifeTime = 100;
        break;
    }

    return c;
}

bool CGMHellas::SettingHellasMonsterLinkBone(CHARACTER *c, int Type)
{
    switch (Type)
    {
    case MODEL_ILLUSION_OF_KUNDUN:
        c->Weapon[0].LinkBone = 29;
        c->Weapon[1].LinkBone = 49;
        return true;
    case MODEL_AEGIS:
        c->Weapon[0].LinkBone = 13;
        c->Weapon[1].LinkBone = 14;
        return true;
    case MODEL_DEATH_CENTURION:
        c->Weapon[0].LinkBone = 56;
        c->Weapon[1].LinkBone = 42;
        return true;
    case MODEL_NECRON:
        c->Weapon[0].LinkBone = 60;
        c->Weapon[1].LinkBone = 60;
        return true;
    case MODEL_SHRIKER:
        c->Weapon[0].LinkBone = 41;
        c->Weapon[1].LinkBone = 51;
        return true;
    }

    return false;
}

bool CGMHellas::SetCurrentAction_HellasMonster(CHARACTER *c, OBJECT *o)
{
    switch (c->MonsterIndex)
    {
    case MONSTER_DEATH_CENTURION_1:
    case MONSTER_DEATH_CENTURION_2:
    case MONSTER_DEATH_CENTURION_3:
    case MONSTER_DEATH_CENTURION_4:
    case MONSTER_DEATH_CENTURION_5:
    case MONSTER_DEATH_CENTURION_6:
        switch ((c->Skill))
        {
        case AT_SKILL_ENERGYBALL:
        case AT_SKILL_FIRE_SLASH:
        case AT_SKILL_FIRE_SLASH_STR:
        case AT_SKILL_POISON:
        case AT_SKILL_POISON_STR:
        case AT_SKILL_MONSTER_SUMMON:
        case AT_SKILL_MONSTER_MAGIC_DEF:
        case AT_SKILL_MONSTER_PHY_DEF:
            SetAction(o, MONSTER01_ATTACK2);
            break;

        default:
            SetAction(o, MONSTER01_ATTACK1);
            break;
        }
        return true;

    case MONSTER_AEGIS_1:
    case MONSTER_AEGIS_2:
    case MONSTER_AEGIS_3:
    case MONSTER_AEGIS_4:
    case MONSTER_AEGIS_5:
    case MONSTER_AEGIS_6:
        switch ((c->Skill))
        {
        case AT_SKILL_ENERGYBALL:
            SetAction(o, MONSTER01_ATTACK2);
            break;

        default:
            SetAction(o, MONSTER01_ATTACK1);
            break;
        }
        return true;

    case MONSTER_ROGUE_CENTURION_1:
    case MONSTER_ROGUE_CENTURION_2:
    case MONSTER_ROGUE_CENTURION_3:
    case MONSTER_ROGUE_CENTURION_4:
    case MONSTER_ROGUE_CENTURION_5:
    case MONSTER_ROGUE_CENTURION_6:
        switch ((c->Skill))
        {
        case AT_SKILL_ENERGYBALL:
            SetAction(o, MONSTER01_ATTACK2);
            break;

        default:
            SetAction(o, MONSTER01_ATTACK1);
            break;
        }
        return true;

    case MONSTER_NECRON_1:
    case MONSTER_NECRON_2:
    case MONSTER_NECRON_3:
    case MONSTER_NECRON_4:
    case MONSTER_NECRON_5:
    case MONSTER_NECRON_6:
        switch ((c->Skill))
        {
        case AT_SKILL_POISON:
        case AT_SKILL_POISON_STR:
            SetAction(o, MONSTER01_ATTACK2);
            break;

        case AT_SKILL_ENERGYBALL:
            SetAction(o, MONSTER01_ATTACK1);
            break;
        }
        return true;

    case MONSTER_SCHRIKER_1:
    case MONSTER_SCHRIKER_2:
    case MONSTER_SCHRIKER_3:
    case MONSTER_SCHRIKER_4:
    case MONSTER_SCHRIKER_5:
    case MONSTER_SCHRIKER_6:
    case MONSTER_ILLUSION_OF_KUNDUN_1:
    case MONSTER_ILLUSION_OF_KUNDUN_2:
    case MONSTER_ILLUSION_OF_KUNDUN_3:
    case MONSTER_ILLUSION_OF_KUNDUN_4:
    case MONSTER_ILLUSION_OF_KUNDUN_5:
        SetAction(o, MONSTER01_ATTACK1 + WorldRandom() % 2);
        return true;

    case MONSTER_ILLUSION_OF_KUNDUN_7:
        SetAction(o, MONSTER01_ATTACK1 + WorldRandom() % 2);
        return true;
    }
    return false;
}

void CGMHellas::MonsterMoveWaterSmoke(OBJECT *o)
{
    if (o->CurrentAction == MONSTER01_WALK)
    {
        vec3_t Position;
        Vector(o->Position[0] + WorldRandom() % 200 - 100,
               o->Position[1] + WorldRandom() % 200 - 100, o->Position[2], Position);
        CreateParticleFpsChecked(BITMAP_SMOKE + 1, Position, o->Angle, o->Light);
    }
}
void CGMHellas::MonsterDieWaterSmoke(OBJECT *o)
{
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    constexpr std::array<std::pair<int, float>, 1> markers{{{MONSTER01_DIE, 8.f}}};
    o->MotionTrace.VisitAnimationEvents(WorldTime, markers, [&](std::size_t, float fraction) {
        auto birth =
            sessionKeeper_.Gameplay()->EmissionTime(FPS_ANIMATION_FACTOR * (1.f - fraction));
        vec3_t origin, angle, light{1.f, 1.f, 1.f};
        o->MotionTrace.Sample(WorldTime, fraction, o->Position, origin);
        VectorCopy(o->Angle, angle);
        angle[2] = o->MotionTrace.SampleYaw(WorldTime, fraction, angle[2]);
        for (int child = 0; child < 20; ++child)
        {
            vec3_t position{origin[0] + WorldRandom() % 64 - 32.f,
                            origin[1] + WorldRandom() % 64 - 32.f,
                            origin[2] + WorldRandom() % 32 - 16.f};
            CreateParticle(BITMAP_SMOKE + 1, position, angle, light, 1);
        }
    });
}

void CGMHellas::AdvanceMonsterState(CHARACTER &character)
{
    if (character.Object.CurrentAction == MONSTER01_DIE)
    {
        if (character.Object.Type == MODEL_ILLUSION_OF_KUNDUN)
            character.Weapon[1].Type = -1;
        if (character.Object.Type == MODEL_DEATH_CENTURION ||
            character.Object.Type == MODEL_SHRIKER)
            character.Weapon[0].Type = character.Weapon[1].Type = -1;
    }

    auto &object = character.Object;
    if (object.Type == MODEL_WARCRAFT && object.CurrentAction == 1)
    {
        object.LifeTime += FPS_ANIMATION_FACTOR;
        object.LifeTime = std::fmod(object.LifeTime, 5.f);
    }
    if (object.Type == MODEL_ILLUSION_OF_KUNDUN)
    {
        AdvanceKundunState(object);
        if (object.LifeTime >= 90.f)
            object.Alpha = 1.f;
        else
            character.Weapon[0].Level = character.Weapon[1].Level = -1;
    }
    if (((object.Type == MODEL_DEATH_CENTURION && object.SubType == 9) ||
         object.Type == MODEL_SHRIKER) &&
        (object.AI == HellasDetail::ACTION_DESTROY_WIZ_DEF ||
         object.AI == HellasDetail::ACTION_DESTROY_DEF))
        object.AI = 0;
}

bool CGMHellas::CreateObject(OBJECT *object)
{
    return CreateHellasObject(object);
}

bool CGMHellas::MoveObject(OBJECT *object)
{
    return MoveHellasVisual(object);
}

void CGMHellas::InstallBehavior()
{
    Models[MODEL_CUNDUN_PART6].Actions[0].Loop = false;
    Models[MODEL_CUNDUN_PART6].Actions[0].PlaySpeed = 0.13f;
    Models[MODEL_CUNDUN_PART7].Actions[0].Loop = false;
    Models[MODEL_CUNDUN_PART7].Actions[0].PlaySpeed = 0.13f;
    Models[MODEL_CUNDUN_PHOENIX].Actions[0].Loop = true;
    Models[MODEL_CUNDUN_GHOST].Actions[0].Loop = false;

    LoadWaveFile(SOUND_KALIMA_AMBIENT, L"Data\\Sound\\aKalima.wav", 1);
    LoadWaveFile(SOUND_KALIMA_AMBIENT2, L"Data\\Sound\\aKalima01.wav", 1);
    LoadWaveFile(SOUND_KALIMA_AMBIENT3, L"Data\\Sound\\aKalima02.wav", 1);
    LoadWaveFile(SOUND_KALIMA_WATER_FALL, L"Data\\Sound\\aKalimaWaterFall.wav", 3);
    LoadWaveFile(SOUND_KALIMA_FALLING_STONE, L"Data\\Sound\\aKalimaStone.wav", 3);
    LoadWaveFile(SOUND_DEATH_BUBBLE, L"Data\\Sound\\mDeathBubble.wav", 1);

    LoadWaveFile(SOUND_KUNDUN_AMBIENT1, L"Data\\Sound\\mKundunAmbient1.wav", 1);
    LoadWaveFile(SOUND_KUNDUN_AMBIENT2, L"Data\\Sound\\mKundunAmbient2.wav", 1);
    LoadWaveFile(SOUND_KUNDUN_ROAR, L"Data\\Sound\\mKundunRoar.wav", 1);
    LoadWaveFile(SOUND_KUNDUN_SIGHT, L"Data\\Sound\\mKundunSight.wav", 1);
    LoadWaveFile(SOUND_KUNDUN_SHUDDER, L"Data\\Sound\\mKundunShudder.wav", 1);
    LoadWaveFile(SOUND_KUNDUN_DESTROY, L"Data\\Sound\\mKundunDestory.wav", 1);

    LoadWaveFile(SOUND_SKILL_SKULL, L"Data\\Sound\\eSkull.wav", 1);
    LoadWaveFile(SOUND_GREAT_POISON, L"Data\\Sound\\eGreatPoison.wav", 1);
    LoadWaveFile(SOUND_GREAT_SHIELD, L"Data\\Sound\\eGreatShield.wav", 1);
}

GMChaosCastle::GMChaosCastle(SessionKeeper &keeper) noexcept
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      gMapManager(keeper.MapManagerObject()), Random(keeper.RandomForConstruction()),
      actionTime_(keeper.ActionTime()), actionObjectVelocity_(keeper.ActionObjectVelocity())
{
}

bool GMChaosCastle::CreateObject(OBJECT *object)
{
    return CreateChaosCastleObject(object);
}

namespace ChaosCastleDetail
{

float CollapseOffset(float remaining, float initialVelocity)
{
    constexpr float VelocityPerTick = 0.4f;
    const float elapsed = (std::max)(0.f, float(ChaosCastleDetail::kActionTriggerTime) - remaining);
    // At authored tick one, the placement used the initial velocity before
    // incrementing it. Derive later values from phase, never placement count.
    return elapsed * (initialVelocity + VelocityPerTick * (elapsed - 1.f));
}

const ChaosCastleDetail::CastleAreaSet *SelectLimitArea(CastleLevel level)
{
    const auto index = ChaosCastleDetail::LevelValue(level);
    if (index < ChaosCastleDetail::kCastleLimitAreas.size())
    {
        return ChaosCastleDetail::kCastleLimitAreas[index];
    }
    return nullptr;
}

} // namespace ChaosCastleDetail

void ClearChaosCastleHelper(CHARACTER *c)
{
    c->Wing.Type = -1;
    c->Wing.Level = 0;
    c->Helper.Type = -1;
    c->Helper.Level = 0;
    c->Weapon[0].Type = -1;
    c->Weapon[0].Level = 0;
    c->Weapon[0].ExcellentFlags = 0;
    c->Weapon[1].Type = -1;
    c->Weapon[1].Level = 0;
    c->Weapon[1].ExcellentFlags = 0;
#ifdef LJW_FIX_MANY_FLAG_DISAPPEARED_PROBREM
    c->EtcPart = PARTS_NONE;
#endif
}

void GMChaosCastle::ChangeChaosCastleUnit(CHARACTER *c)
{
    if (gMapManager.InChaosCastle() == false)
        return;

    ClearChaosCastleHelper(c);

    DWORD t_dwUIID = g_pWindowMgr->GetAddFriendWindow();
    if (t_dwUIID != 0)
    {
        g_pWindowMgr->SendUIMessage(UI_MESSAGE_CLOSE, t_dwUIID, 0);
    }
    if (g_pUIManager->IsOpen(::INTERFACE_FRIEND))
    {
        CUIFriendWindow *t_pFW = g_pWindowMgr->GetFriendMainWindow();
        t_pFW->Close();
    }

    int Class = gCharacterManager.GetBaseClass(c->Class);

    if (Class == CLASS_KNIGHT || Class == CLASS_DARK || Class == CLASS_DARK_LORD ||
        Class == CLASS_RAGEFIGHTER)
    {
        c->Weapon[0].Type = MODEL_SWORD_OF_DESTRUCTION;
        c->Weapon[0].Level = 0;
        c->Weapon[1].Type = MODEL_SWORD_OF_DESTRUCTION;
        c->Weapon[1].Level = 0;
    }
    else if (Class == CLASS_ELF)
    {
        c->Weapon[0].Type = MODEL_GREAT_REIGN_CROSSBOW;
        c->Weapon[0].Level = 0;
    }
    else if (Class == CLASS_WIZARD || Class == CLASS_SUMMONER)
    {
        c->Weapon[0].Type = MODEL_LEGENDARY_STAFF;
        c->Weapon[0].Level = 0;
    }
}

bool GMChaosCastle::MoveChaosCastleObjectSetting(int &objCount, int object)
{
    if (gMapManager.InChaosCastle() == false)
        return false;

    if (rand_fps_check(10) && object)
    {
        objCount = Random.RangeInt(0, object - 1);
    }

    const float ambientRate = (object > 0 ? 0.9f : 1.f) / 10.f;
    if (!Hero->SafeZone)
        for (auto birth : sessionKeeper_.Gameplay()->Emissions(FPS_ANIMATION_FACTOR * ambientRate))
        {
            vec3_t Position;

            const float fraction = birth.FrameFraction();
            Hero->Object.MotionTrace.Sample(WorldTime, fraction, Hero->Object.Position, Position);
            Position[0] += Random.RangeFloat(-400, 399);
            Position[1] += Random.RangeFloat(-400, 399);
            Position[2] -= 150.f;

            CreateJoint(BITMAP_JOINT_SPIRIT2, Position, Position, Hero->Object.Angle, 9, NULL,
                        Random.RangeFloat(50, 59));
        }

    return true;
}

bool GMChaosCastle::MoveChaosCastleObject(OBJECT *o, int &object, int &visibleObject)
{
    if (gMapManager.InChaosCastle() == true)
    {
        int objectCount = object;
        if (o->Type == 3)
        {
            visibleObject++;
            if (g_actionMatch)
            {
                o->LifeTime = 10;
                o->PKKey = 1;
                g_actionMatch = false;
            }
            else if (objectCount)
            {
                o->LifeTime = 10;
                o->PKKey = 0;
                objectCount--;
                if (objectCount == 0)
                {
                    o->PKKey = 1;
                }
            }
            object = objectCount;
        }

        return true;
    }

    return false;
}

void GMChaosCastle::FinishObjectAction()
{
    switch (g_currentCastleLevel)
    {
    case CastleLevel::One:
        g_currentCastleLevel = CastleLevel::Two;
        break;
    case CastleLevel::Four:
        g_currentCastleLevel = CastleLevel::Five;
        break;
    case CastleLevel::Seven:
        g_currentCastleLevel = CastleLevel::Eight;
        break;
    default:
        break;
    }
}

bool GMChaosCastle::MoveChaosCastleAllObject(OBJECT *o)
{
    if (!gMapManager.InChaosCastle())
        return false;
    CastleLevel level;
    switch (o->Type)
    {
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
        level = CastleLevel::Seven;
        break;
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
        level = CastleLevel::Four;
        break;
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        level = CastleLevel::One;
        break;
    default:
        return true;
    }
    if (g_currentCastleLevel != level || FPS_ANIMATION_FACTOR <= 0.f)
        return true;
    const float shaking =
        (std::min)(FPS_ANIMATION_FACTOR,
                   (std::max)(0.f, actionTime_ - (ChaosCastleDetail::kActionTriggerTime - 1.f)));
    for (auto birth : sessionKeeper_.Gameplay()->Emissions(shaking, shaking))
    {
        vec3_t position{o->Position[0] + Random.RangeFloat(-150, 149), o->Position[1],
                        Hero->Object.Position[2]};
        vec3_t light{1.f, 1.f, 1.f};
        CreateParticle(BITMAP_SMOKE + 4, position, o->Angle, light, 0, 1.5f);
    }
    const float remaining = (std::max)(0.f, actionTime_ - FPS_ANIMATION_FACTOR);
    if (remaining >= ChaosCastleDetail::kActionTriggerTime - 1.f)
        EarthQuake = Random.RangeFloat(-3, -1) * 0.1f;
    else
        o->Position[2] = o->StartPosition[2] -
                         ChaosCastleDetail::CollapseOffset(remaining + 1.f, actionObjectVelocity_);
    if (actionTime_ <= FPS_ANIMATION_FACTOR)
        o->SetHiddenMesh(-2);
    return true;
}

bool GMChaosCastle::CreateChaosCastleObject(OBJECT *o)
{
    if (gMapManager.InChaosCastle() == false)
        return false;

    switch (o->Type)
    {
    case 18:
    case 19:
    case 20:
    case 21:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
        o->HiddenMesh = -2;
        VectorCopy(o->Position, o->StartPosition);
        break;
    }

    return true;
}

void GMChaosCastle::InstallBehavior()
{
    LoadWaveFile(SOUND_CHAOSCASTLE, L"Data\\Sound\\iChaosCastle.wav", 1);
}

bool GMChaosCastle::ActionObject(OBJECT *object)
{
    return MoveChaosCastleAllObject(object);
}

bool GMChaosCastle::PushCharacter(CHARACTER *c, OBJECT *o, float Speed)
{
    const float frames = std::clamp(static_cast<float>(CharacterPushEndFrame - c->JumpTime), 0.f,
                                    FPS_ANIMATION_FACTOR);
    const float blend = Core::Time::Blend(Speed, frames);
    o->Position[0] += (c->TargetX * TERRAIN_SCALE - o->Position[0]) * blend;
    o->Position[1] += (c->TargetY * TERRAIN_SCALE - o->Position[1]) * blend;
    c->JumpTime += frames;
    if (CharacterPushEndFrame - c->JumpTime <= 0.000001)
    {
        SetPlayerStop(c);

        o->Position[0] = c->TargetX * TERRAIN_SCALE;
        o->Position[1] = c->TargetY * TERRAIN_SCALE;

        c->PositionX = c->TargetX;
        c->PositionY = c->TargetY;

        c->JumpTime = 0;
    }
    return true;
}

bool GMChaosCastle::AdvanceCharacterDeath(CHARACTER *c, OBJECT *o)
{
    int startDeadTime = 25;
    if (o->m_bActionStart)
    {
        FallingCharacter(c, o);
        startDeadTime = 15;
    }
    if (c->Dead <= startDeadTime && c->Dead >= startDeadTime - 10 && (((int)c->Dead) % 2))
    {
        vec3_t Position;

        VectorCopy(o->Position, Position);

        Position[0] += WorldRandom() % 160 - 80.f;
        Position[1] += WorldRandom() % 160 - 80.f;
        Position[2] += WorldRandom() % 160 - 80.f + 50.f;
        CreateBomb(Position, true);
    }
    return true;
}

void GMChaosCastle::AdvanceCharacterStopTime()
{
    if ((Hero->PositionX) != g_iOldPositionX || (Hero->PositionY) != g_iOldPositionY)
    {
        g_iOldPositionX = (Hero->PositionX);
        g_iOldPositionY = (Hero->PositionY);

        g_fStopTime = WorldTime;
        return;
    }

    float fStopTime = ((WorldTime - g_fStopTime) / CLOCKS_PER_SEC);
    if (fStopTime >= 10)
    {
        int index = TERRAIN_INDEX_REPEAT(g_iOldPositionX, g_iOldPositionY);

        if ((TerrainWall[index] & TW_NOGROUND) == TW_NOGROUND)
        {
            SocketClient->ToGameServer()->SendChaosCastlePositionSet(g_iOldPositionX,
                                                                     g_iOldPositionY);
        }
        g_fStopTime = WorldTime;
    }
}

CGM_PK_FieldPtr CGM_PK_Field::Make(SessionKeeper &keeper)
{
    CGM_PK_FieldPtr pkfield(new CGM_PK_Field(keeper));
    pkfield->Init();
    return pkfield;
}

CGM_PK_Field::CGM_PK_Field(SessionKeeper &keeper)
    : BaseMap(keeper.FrameAnimationFactor(), keeper.FrameWorldTime()), SessionLegacyCalls(keeper),
      Random(keeper.RandomForConstruction()), gMapManager(keeper.MapManagerObject()),
      g_Camera(keeper.CameraStateObject())
{
}

CGM_PK_Field::~CGM_PK_Field()
{
    Destroy();
}

void CGM_PK_Field::Init()
{
    //n/a
}

void CGM_PK_Field::Destroy()
{
    //n/a
}

const PkFieldDetail::MonsterDefinition *CGM_PK_Field::FindMonsterDefinition(int monsterType)
{
    const auto it = std::find_if(PkFieldDetail::kMonsterDefinitions.begin(),
                                 PkFieldDetail::kMonsterDefinitions.end(),
                                 [monsterType](const PkFieldDetail::MonsterDefinition &def) {
                                     return def.monsterType == monsterType;
                                 });
    return (it != PkFieldDetail::kMonsterDefinitions.end()) ? &(*it) : nullptr;
}

CHARACTER *CGM_PK_Field::CreateMonster(int type, int positionX, int positionY, int key)
{
    if (!gMapManager.IsPKField())
    {
        return nullptr;
    }

    const PkFieldDetail::MonsterDefinition *definition = FindMonsterDefinition(type);
    if (definition == nullptr)
    {
        return nullptr;
    }

    OpenMonsterModel(definition->monsterModelId);

    CHARACTER *character = CreateCharacter(key, definition->objectModelId, positionX, positionY);
    if (character == nullptr)
    {
        return nullptr;
    }

    character->Object.Scale = definition->scale;
    character->Object.m_iAnimation = 0;
    character->Weapon[0].Type = -1;
    character->Weapon[1].Type = -1;

    if (definition->assignLifetime)
    {
        character->Object.LifeTime = definition->lifetime;
    }

    return character;
}

bool CGM_PK_Field::CreateObject(OBJECT *o)
{
    if (!gMapManager.IsPKField())
    {
        return false;
    }

    if (o->Type >= 0 && o->Type <= 6)
    {
        o->CollisionRange = -300;
        return true;
    }

    return false;
}
bool CGM_PK_Field::MoveObject(OBJECT *o)
{
    if (!gMapManager.IsPKField())
    {
        return false;
    }

    switch (o->Type)
    {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
        o->HiddenMesh = -2;
        return true;

    default:
        break;
    }

    return false;
}

void CGM_PK_Field::AdvanceMonsterState(CHARACTER &character, BMD &model)
{
    auto &object = character.Object;

    if (!gMapManager.IsPKField() || object.CurrentAction != MONSTER01_DIE)
        return;
    if (object.Type != MODEL_BLOOD_ASSASSIN && object.Type != MODEL_CRUEL_BLOOD_ASSASSIN)
        return;
    if (object.LifeTime == 100)
    {
        object.LifeTime = 90;
        object.m_bRenderShadow = false;
    }
}
