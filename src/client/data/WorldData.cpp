#include "data/WorldData.h"
#include "app/ApplicationNetwork.h"
#include "render/ModelResources.h"
#include "support/CoreMath.h"
#include "render/Terrain.h"
#include "turbojpeg.h"
#include "data/Localization.h"
#include "domain/CharacterSystem.h"
#include "domain/ItemsSkills.h"
#include "json.hpp"
#include "domain/WorldSimulation.h"
#include "session/SessionKeeper.h"
#include "app/ApplicationKeeper.h"
#include "session/SessionRender.h"
#include "app/ApplicationLoopFrame.h"
#include "session/SessionGameplay.h"
#include "app/AppWindow.h"
#include "ui/session/UiSessionLogic.h"
#include "render/Textures.h"
#include "domain/CharacterPresentation.h"
#include "render/World.h"
#include "domain/MovementAI.h"
#include "domain/Events.h"
#include "data/ItemData.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "data/GameData.h"
#include "domain/EffectsUpdate.h"
#include "render/Text.h"
#include "support/Scenes.h"
#include "ui/runtime/UiControls.h"
#include "app/ApplicationAudio.h"
#include "I18N/All.h"
#include "domain/Shop.h"
#include "domain/Quests.h"
#include "ui/features/World/WorldLogic.h"
#include "domain/MapSimulation.h"
#include "domain/ChatSocial.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "session/SessionPresentation.h"
#include "data/ResourceData.h"
#include "support/Camera.h"
#include "render/Sprites.h"
#include "session/SessionNetwork.h"

using namespace SEASON3B;

#pragma pack(push, 1)
typedef struct
{
    int index;
    char szMainMapName[32];
    char szSubMapName[32];
    int iReqLevel;
    int m_iReqMaxLevel;
    int iReqZen;
    int iGateNum;
} MOVEREQINFO_FILE;
#pragma pack(pop)

CMoveCommandData::CMoveCommandData()
{
}

CMoveCommandData::~CMoveCommandData()
{
    Release();
}

bool CMoveCommandData::Create(const std::wstring &filename)
{
    FILE *fp = _wfopen(filename.c_str(), L"rb");
    if (fp == NULL)
        return false;

    int count = 0;
    fread(&count, sizeof(int), 1, fp);

    for (int i = 0; i < count; i++)
    {
        auto *pMoveInfoData = new MOVEINFODATA;
        MOVEREQINFO_FILE moveReqInfo{};
        fread(&moveReqInfo, sizeof moveReqInfo, 1, fp);

        BuxConvert((BYTE *)&moveReqInfo, sizeof moveReqInfo);
        pMoveInfoData->_ReqInfo.index = moveReqInfo.index;
        pMoveInfoData->_ReqInfo.iGateNum = moveReqInfo.iGateNum;
        pMoveInfoData->_ReqInfo.iReqLevel = moveReqInfo.iReqLevel;
        pMoveInfoData->_ReqInfo.iReqZen = moveReqInfo.iReqZen;
        pMoveInfoData->_ReqInfo.m_iReqMaxLevel = moveReqInfo.m_iReqMaxLevel;
        CMultiLanguage::ConvertFromUtf8(pMoveInfoData->_ReqInfo.szMainMapName,
                                        moveReqInfo.szMainMapName,
                                        sizeof moveReqInfo.szMainMapName);
        CMultiLanguage::ConvertFromUtf8(pMoveInfoData->_ReqInfo.szSubMapName,
                                        moveReqInfo.szSubMapName, sizeof moveReqInfo.szSubMapName);

        m_listMoveInfoData.push_back(pMoveInfoData);
    }
    fclose(fp);

    return true;
}

void CMoveCommandData::Release()
{
    auto li = m_listMoveInfoData.begin();
    for (; li != m_listMoveInfoData.end(); li++)
        delete (*li);
    m_listMoveInfoData.clear();
}

bool CMoveCommandData::OpenMoveReqScript(const std::wstring &filename)
{
    return Create(filename);
}

int CMoveCommandData::GetNumMoveMap()
{
    if (m_listMoveInfoData.size() > 0)
        return m_listMoveInfoData.size();

    return -1;
}

const CMoveCommandData::MOVEINFODATA *CMoveCommandData::GetMoveCommandDataByIndex(int iIndex)
{
    auto li = m_listMoveInfoData.begin();
    for (; li != m_listMoveInfoData.end(); li++)
    {
        if ((*li)->_ReqInfo.index == iIndex)
        {
            return (*li);
        }
    }
    return 0;
}

const std::list<CMoveCommandData::MOVEINFODATA *> &CMoveCommandData::GetMoveCommandDatalist()
{
    return m_listMoveInfoData;
}

namespace
{
using Family = MapDefinition::Family;
using Scene = MapDefinition::Scene;

constexpr bool ObjectsAfterCharacters(int raw, Family family)
{
    constexpr int KanturuFirst = 37, ChangeUpLast = 42, Elveland = 51;
    constexpr int Swamp = 56, Raklion = 57, RaklionBoss = 58, PkField = 63;
    constexpr int Marketplace = 79;
    return (raw >= KanturuFirst && raw <= ChangeUpLast) || family == Family::CursedTemple ||
           raw == Elveland || raw == Swamp || raw == Raklion || raw == RaklionBoss ||
           raw == PkField || family == Family::Doppelganger || raw == Marketplace ||
           family == Family::Karutan;
}

constexpr int Devias = 2, Atlans = 7, Tarkan = 8, Icarus = 10, BattleCastle = 30;
constexpr int Aida = 33, Crywolf = 34, ThirdChangeRefuge = 42, Elbeland = 51;
constexpr int OldCharacterScene = 54, OldLoginScene = 55, Raklion = 57, RaklionBoss = 58;
constexpr int SantaTown = 62, PkField = 63, DoppelGanger2 = 66, DoppelGanger3 = 67;
constexpr int EmpireFirst = 69, EmpireThird = 71, Marketplace = 79, DoppelGanger1 = 65;

constexpr void SetWeatherAndAmbientPolicy(MapPresentationPolicy &policy, int raw, Family family)
{
    policy.weatherAlphaBlend = raw == DoppelGanger1 || raw == Devias || raw == Atlans ||
                               raw == Icarus || raw == BattleCastle || raw == ThirdChangeRefuge ||
                               raw == OldLoginScene || raw == Raklion || raw == RaklionBoss ||
                               raw == SantaTown || raw == PkField || raw == DoppelGanger2 ||
                               (raw >= EmpireFirst && raw <= EmpireThird) || raw == Marketplace;
    policy.weatherSprites = raw == DoppelGanger1 || raw == Devias || raw == Raklion ||
                            raw == RaklionBoss || raw == SantaTown;
    using Admission = MapPresentationPolicy::WeatherAdmission;
    constexpr int Lorencia = 0, Noria = 3, HuntingGround = 31, EmpireLast = 74;
    policy.weather = raw == Lorencia ? Admission::OutsideTavern
                     : raw == Devias ? Admission::OutsideChurch
                     : raw == Noria || raw == Atlans || raw == Icarus || raw == BattleCastle ||
                             raw == HuntingGround || raw == Aida || raw == OldCharacterScene ||
                             raw == Crywolf || raw == ThirdChangeRefuge || raw == Raklion ||
                             raw == RaklionBoss || raw == DoppelGanger1 || raw == SantaTown ||
                             raw == PkField || raw == DoppelGanger2 ||
                             (raw >= EmpireFirst && raw <= EmpireLast) || raw == Marketplace ||
                             family == Family::DevilSquare || family == Family::ChaosCastle
                         ? Admission::Enabled
                         : Admission::Disabled;
    policy.weatherSpritePass = raw == Devias || raw == Raklion || raw == RaklionBoss ||
                               raw == DoppelGanger1 || raw == SantaTown || raw == PkField ||
                               raw == DoppelGanger2 || (raw >= EmpireFirst && raw <= EmpireLast) ||
                               raw == Marketplace;
    policy.weatherWaterPass = raw == Devias;
    policy.weatherTurningForce = family == Family::ChaosCastle;
    policy.weatherRainState = raw == Crywolf;
    policy.extraSnowLeaves = raw == Devias;
    policy.fullAmbientFishPool = raw == Atlans || raw == Tarkan || raw == Crywolf ||
                                 raw == DoppelGanger3 || family == Family::Hellas;
    policy.waterFish = raw == Atlans || raw == DoppelGanger3;
    policy.fishWallObstacles =
        raw == Tarkan || raw == Aida || raw == OldCharacterScene || family == Family::Hellas;
    policy.finiteAmbientFish = family == Family::Hellas;
    policy.persistentAmbientBoids = raw == Elbeland;
    policy.authoredBoidAnimation = raw == Elbeland;
    policy.ambientShadows = raw != Icarus;
    policy.ambientTornadoShadow = raw != Elbeland;
    policy.finiteAmbientBoids = family == Family::BloodCastle;
}

constexpr std::array<float, 3> ClearColorFor(int raw, Family family)
{
    constexpr int Lorencia = 0;
    constexpr float ByteColor = 1.f / 256.f;
    if (raw == Lorencia)
        return {10 * ByteColor, 20 * ByteColor, 14 * ByteColor};
    if (raw == Devias)
        return {0.75f, 0.85f, 1.f};
    if (raw == Icarus)
        return {3 * ByteColor, 25 * ByteColor, 44 * ByteColor};
    if (family == Family::Hellas)
        return {30 * ByteColor, 40 * ByteColor, 40 * ByteColor};
    if (family == Family::CursedTemple)
        return {9 * ByteColor, 8 * ByteColor, 33 * ByteColor};
    if (raw == Elbeland)
        return {178 * ByteColor, 178 * ByteColor, 178 * ByteColor};
    if (raw == DoppelGanger1)
        return {148 * ByteColor, 179 * ByteColor, 223 * ByteColor};
    return {};
}

constexpr MapPresentationPolicy PresentationFor(int raw, Family family)
{
    MapPresentationPolicy policy;
    policy.mainTerrain = raw != Icarus;
    policy.persistentTerrain = true;
    policy.objectsBeforeTerrain = raw == PkField || raw == DoppelGanger2;
    policy.grass = family != Family::BloodCastle && raw != Atlans && raw != DoppelGanger3;
    policy.pkField = raw == PkField;
    policy.doppelGanger2 = raw == DoppelGanger2;
    policy.doppelGanger3 = raw == DoppelGanger3;
    policy.objectsAfterCharacters = ObjectsAfterCharacters(raw, family);
    constexpr int KanturuThird = 39, NewLogin = 73, NewCharacter = 74;
    // These maps admit some objects independently of their block frustum.
    policy.objectBlockCulling = raw != KanturuThird && raw != Elbeland && raw != Raklion &&
                                raw != RaklionBoss && raw != PkField && raw != DoppelGanger2 &&
                                raw != NewLogin && raw != NewCharacter;
    SetWeatherAndAmbientPolicy(policy, raw, family);
    policy.clearColor = ClearColorFor(raw, family);
    return policy;
}

constexpr MapCharacterPolicy CharacterPolicyFor(int raw, Family family)
{
    constexpr int Lorencia = 0, DoppelGangerFirst = 65;
    MapCharacterPolicy policy;
    constexpr int NewLogin = 73;
    constexpr int CharacterWall = 0x02, NoMoveWall = 0x04;
    policy.attackSounds = raw != NewLogin;
    policy.monsterWall = family == Family::Doppelganger ? NoMoveWall : CharacterWall;
    policy.ocean = raw == Atlans || raw == DoppelGanger3;
    policy.swimming = policy.ocean || family == Family::Hellas;
    policy.skyTerrain = raw == Icarus;
    policy.flyingMounts = raw == Tarkan || raw == Icarus;
    policy.groundShadows = raw != Icarus && family != Family::Hellas;
    policy.snowFootsteps = raw == Devias;
    policy.mounts = family != Family::ChaosCastle;
    policy.nonPlayerMountOwners = family == Family::Doppelganger;
    policy.festiveNpcs = raw == Lorencia || raw == Devias;
    policy.festiveUniform = raw == Lorencia;
    policy.cloneAppearances = raw == DoppelGangerFirst;
    policy.festiveFacing = raw == Lorencia ? 90.f : 0.f;
    policy.shadowAlpha = raw == Atlans ? 0.2f : 1.f;
    return policy;
}

constexpr MapTerrainPolicy TerrainPolicyFor(int raw, Family family)
{
    constexpr int KanturuThird = 39, NewLogin = 73, NewCharacter = 74;
    MapTerrainPolicy policy;
    policy.alphaTestTile =
        raw == KanturuThird                                                          ? 3
        : family == Family::CursedTemple                                             ? 4
        : raw == Elbeland                                                            ? 2
        : family == Family::EmpireGuardian || raw == NewLogin || raw == NewCharacter ? 10
        : family == Family::Karutan                                                  ? 12
                                                                                     : -1;
    policy.hiddenBaseTile = raw == KanturuThird ? 100 : -1;
    policy.afterPass = raw == KanturuThird;
    policy.grassEnabled = family != Family::ChaosCastle && family != Family::BattleCastle;
    policy.grassFaces = family != Family::BloodCastle;
    policy.dynamicGrass = family == Family::ChaosCastle;
    policy.magma = raw == PkField || raw == DoppelGanger2;
    policy.ocean = raw == Atlans || raw == DoppelGanger3;
    policy.reverseWater = family == Family::BattleCastle;
    policy.secondaryGrassWind = family == Family::Karutan;
    if (family == Family::BattleCastle)
    {
        policy.lightDirection[1] = -1.f;
        policy.lightDirection[2] = 1.f;
    }
    policy.heightSaveScale = raw == OldLoginScene ? 3.f : 1.5f;
    policy.waterPeriod = raw == Tarkan ? 40000 : raw == ThirdChangeRefuge ? 50000 : 20000;
    policy.waterRate = raw == Tarkan ? 0.000025f : raw == ThirdChangeRefuge ? 0.00002f : 0.00005f;
    policy.grassWindScale = raw == Raklion || raw == RaklionBoss ? 60.f : 10.f;
    policy.grassWindFrequency =
        raw == Tarkan || raw == Raklion || raw == RaklionBoss || family == Family::Karutan ? 50.f
                                                                                           : 5.f;
    return policy;
}

// Numeric text IDs are the existing localized Game text catalog identities.
// Asset numbers select complete WorldN/ObjectN directories, not protocol IDs.
constexpr auto Definitions = [] {
    constexpr int LastLegacyMap = 81;
    std::array<std::optional<MapDefinition>, LastLegacyMap + 1> maps{};
    const auto add = [&](int raw, int asset, Family family, int name, Scene scene = Scene::Gameplay,
                         bool extended = false) {
        maps[raw] = MapDefinition{*MapId::TryCreate(raw),
                                  asset,
                                  family,
                                  scene,
                                  name,
                                  extended,
                                  PresentationFor(raw, family),
                                  CharacterPolicyFor(raw, family),
                                  TerrainPolicyFor(raw, family)};
    };
    const auto range = [&](int first, int last, int asset, Family family, int name) {
        for (int raw = first; raw <= last; ++raw)
            add(raw, asset, family, name);
    };
    for (int raw = 0; raw <= 8; ++raw)
        add(raw, raw + 1, Family::Ordinary, 30 + raw);
    add(9, 10, Family::DevilSquare, 39);
    add(10, 11, Family::Ordinary, 55);
    range(11, 17, 12, Family::BloodCastle, 56);
    range(18, 23, 19, Family::ChaosCastle, 57);
    range(24, 29, 25, Family::Hellas, 58);
    add(30, 31, Family::BattleCastle, 669);
    add(31, 32, Family::Ordinary, 59);
    add(32, 10, Family::DevilSquare, 39);
    add(33, 34, Family::Ordinary, 1850);
    add(34, 35, Family::Crywolf, 1851);
    add(35, 36, Family::Ordinary, 65); // Retained incomplete Crywolf2 content.
    add(36, 25, Family::Hellas, 1852);
    add(37, 38, Family::Kanturu, 2177);
    add(38, 39, Family::Kanturu, 2178);
    add(39, 40, Family::Kanturu, 2179);
    add(40, 41, Family::Ordinary, 2324);
    add(41, 42, Family::Ordinary, 1678);
    add(42, 43, Family::Ordinary, 1679, Scene::Gameplay, true);
    range(45, 50, 47, Family::CursedTemple, 2369);
    add(51, 52, Family::Ordinary, 1853);
    add(52, 12, Family::BloodCastle, 56);
    add(53, 19, Family::ChaosCastle, 57);
    add(54, 55, Family::LocalScene, 84, Scene::Character);
    add(55, 56, Family::LocalScene, 85, Scene::Login);
    add(56, 57, Family::Ordinary, 1854);
    add(57, 58, Family::Ordinary, 1855);
    add(58, 59, Family::Ordinary, 1856);
    add(62, 63, Family::Ordinary, 2611);
    add(63, 64, Family::Ordinary, 2686, Scene::Gameplay, true);
    add(64, 65, Family::Ordinary, 2703);
    for (int raw = 65; raw <= 68; ++raw)
        add(raw, raw + 1, Family::Doppelganger, 3057, Scene::Gameplay, raw == 66);
    for (int raw = 69; raw <= 72; ++raw)
        add(raw, raw + 1, Family::EmpireGuardian, 2806);
    add(73, 74, Family::LocalScene, 103, Scene::Login);
    add(74, 75, Family::LocalScene, 104, Scene::Character);
    add(77, 78, Family::LocalScene, 107, Scene::Login);
    add(78, 79, Family::LocalScene, 108, Scene::Character);
    add(79, 80, Family::Ordinary, 3017);
#ifdef ASG_ADD_MAP_KARUTAN
    add(80, 81, Family::Karutan, 3285);
    add(81, 82, Family::Karutan, 3285);
#endif
    return maps;
}();
} // namespace

const MapDefinition *MapDefinition::Find(int rawMap) noexcept
{
    if (rawMap < 0 || static_cast<std::size_t>(rawMap) >= Definitions.size())
        return nullptr;
    const auto &definition = Definitions[rawMap];
    return definition ? &*definition : nullptr;
}

int MapDefinition::AttributeFile(Variant variant) const noexcept
{
    constexpr int VariantFileMultiplier = 10;
    if (family == Family::Crywolf && variant == Variant::Occupied)
        return assetSet * VariantFileMultiplier + 1;
    if ((family == Family::Crywolf || family == Family::BattleCastle) && variant == Variant::War)
        return assetSet * VariantFileMultiplier + 2;
    constexpr int KanturuRefinery = 39;
    if (id.RawValue() == KanturuRefinery && variant == Variant::Success)
        return assetSet * VariantFileMultiplier + 1;
    return assetSet;
}

const wchar_t *MapDefinition::LightFile(Variant variant) const noexcept
{
    if (family == Family::Crywolf && variant == Variant::Occupied)
        return L"TerrainLight1.jpg";
    if ((family == Family::Crywolf || family == Family::BattleCastle) && variant == Variant::War)
        return L"TerrainLight2.jpg";
    return L"TerrainLight.jpg";
}

std::optional<MapId> WorldBinding::RawMap() const noexcept
{
    return definition ? std::optional{definition->id} : std::nullopt;
}

std::optional<std::uint64_t> WorldBinding::CharacterInstance() const noexcept
{
    if (!definition || !route || definition->scene != MapDefinition::Scene::Gameplay)
        return std::nullopt;
    constexpr std::uint64_t IdentityBias = 1;
    return ((static_cast<std::uint64_t>(*route) + IdentityBias) << 32U) |
           (definition->id.RawValue() + IdentityBias);
}

bool WorldBinding::Matches(int rawMap, std::uint32_t destinationRoute) const noexcept
{
    return definition && definition->id.RawValue() == rawMap && route == destinationRoute;
}

namespace
{
bool ValidWaypoint(const WorldCameraData::Waypoint &point)
{
    // Terrain sampling computes signed yi * TERRAIN_SIZE + xi. Use half
    // its coordinate capacity to leave room for login offsets, steps, and rounding.
    constexpr double CoordinateLimit =
        ((std::numeric_limits<int>::max)() / (TERRAIN_SIZE + 1) - 1.0) * TERRAIN_SCALE / 2;
    // DefaultCamera displaces the camera by 110 world units per level.
    // Reserve only a quarter of the coordinate extent for that displacement;
    // this also bounds the 390x far plane and tour interpolation products.
    constexpr double CameraDisplacementPerLevel = 110;
    constexpr double DistanceLevelLimit = CoordinateLimit / (4 * CameraDisplacementPerLevel);
    return point.iIndex >= 0 && point.iIndex < TERRAIN_SIZE * TERRAIN_SIZE &&
           std::isfinite(point.fCameraX) && std::abs(double(point.fCameraX)) < CoordinateLimit &&
           std::isfinite(point.fCameraY) && std::abs(double(point.fCameraY)) < CoordinateLimit &&
           std::isfinite(point.fCameraZ) && std::abs(double(point.fCameraZ)) < CoordinateLimit &&
           point.iDelay >= 0 && std::isfinite(point.fCameraMoveAccel) &&
           point.fCameraMoveAccel > 0 && std::isfinite(point.fCameraDistanceLevel) &&
           point.fCameraDistanceLevel > 0 && point.fCameraDistanceLevel < DistanceLevelLimit;
}
} // namespace

std::optional<WorldCameraData> WorldCameraData::Load(const std::filesystem::path &path,
                                                     WorldFileData::Error &error) noexcept
{
    using Error = WorldFileData::Error;
    error = Error::None;
    try
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
        {
            error = Error::Open;
            return std::nullopt;
        }
        const std::streamoff length = input.tellg();
        constexpr std::streamoff HeaderSize = 2 * sizeof(std::uint32_t);
        if (length < HeaderSize)
        {
            error = Error::Size;
            return std::nullopt;
        }
        input.seekg(0);
        std::uint32_t header[2]{};
        if (!input.read(reinterpret_cast<char *>(header), sizeof(header)))
        {
            error = Error::Read;
            return std::nullopt;
        }
        if (header[0] != Signature)
        {
            error = Error::Header;
            return std::nullopt;
        }
        const auto payloadSize = std::uint64_t(header[1]) * sizeof(Waypoint);
        if (header[1] == 0 || payloadSize + HeaderSize != static_cast<std::uint64_t>(length) ||
            payloadSize > (std::numeric_limits<std::size_t>::max)())
        {
            error = Error::Size;
            return std::nullopt;
        }
        WorldCameraData data;
        data.waypoints_.resize(header[1]);
        if (!input.read(reinterpret_cast<char *>(data.waypoints_.data()), payloadSize))
        {
            error = Error::Read;
            return std::nullopt;
        }
        if (!std::all_of(data.waypoints_.begin(), data.waypoints_.end(), ValidWaypoint))
        {
            error = Error::Values;
            return std::nullopt;
        }
        return data;
    }
    catch (const std::bad_alloc &)
    {
        error = Error::Allocation;
        return std::nullopt;
    }
    catch (...)
    {
        error = Error::Read;
        return std::nullopt;
    }
}

namespace
{
constexpr std::size_t CellCount = TERRAIN_SIZE * TERRAIN_SIZE;
constexpr std::size_t MappingHeaderSize = 2;
constexpr std::size_t MappingSize = MappingHeaderSize + CellCount * 3;
constexpr std::uint16_t FirstInvalidAttribute = 128;
constexpr std::size_t AttributeHeaderSize = 4;
constexpr std::size_t NarrowAttributeSize = AttributeHeaderSize + CellCount;
constexpr std::size_t WideAttributeSize = AttributeHeaderSize + CellCount * 2;
constexpr std::size_t PlacementHeaderSize = 4;
constexpr std::size_t PlacementSize = sizeof(std::int16_t) + 7 * sizeof(float);
constexpr std::size_t MaximumPlacementSize =
    PlacementHeaderSize + (std::numeric_limits<std::int16_t>::max)() * PlacementSize;

bool CanRealizePlacement(const WorldFileData::Placement &placement)
{
    const auto finite = [](float value) { return std::isfinite(value); };
    if (placement.type < 0 || placement.type >= MAX_MODELS ||
        !std::all_of(placement.position.begin(), placement.position.end(), finite) ||
        !std::all_of(placement.angle.begin(), placement.angle.end(), finite) ||
        !finite(placement.scale))
        return false;
    // CreateObject truncates XY to one of sixteen blocks. Preserve its small
    // negative-coordinate acceptance without ever converting invalid floats.
    constexpr float BlockWidth = 16 * TERRAIN_SCALE;
    constexpr float WorldWidth = TERRAIN_SIZE * TERRAIN_SCALE;
    return placement.position[0] > -BlockWidth && placement.position[0] < WorldWidth &&
           placement.position[1] > -BlockWidth && placement.position[1] < WorldWidth;
}

std::size_t MaximumSize(WorldFileData::Kind kind)
{
    switch (kind)
    {
    case WorldFileData::Kind::Mapping:
        return MappingSize;
    case WorldFileData::Kind::Attributes:
        return WideAttributeSize;
    case WorldFileData::Kind::Placements:
        return MaximumPlacementSize;
    }
    return 0;
}

bool ReadEncrypted(const std::filesystem::path &path, WorldFileData::Kind kind,
                   std::vector<std::uint8_t> &decoded, WorldFileData::Error &error)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
    {
        error = WorldFileData::Error::Open;
        return false;
    }
    const auto length = input.tellg();
    if (length < static_cast<std::streamoff>(MappingHeaderSize) ||
        length > static_cast<std::streamoff>(MaximumSize(kind)))
    {
        error = WorldFileData::Error::Size;
        return false;
    }
    std::vector<std::uint8_t> encrypted(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char *>(encrypted.data()), length))
    {
        error = WorldFileData::Error::Read;
        return false;
    }
    decoded.resize(encrypted.size());
    MapFileDecrypt(decoded.data(), encrypted.data(), static_cast<int>(encrypted.size()));
    if (kind == WorldFileData::Kind::Attributes)
        BuxConvert(decoded.data(), static_cast<int>(decoded.size()));
    return true;
}
} // namespace

std::optional<WorldFileData> WorldFileData::Load(const std::filesystem::path &path, Kind kind,
                                                 int assetSet, int behaviorMap,
                                                 Error &error) noexcept
{
    error = Error::None;
    try
    {
        WorldFileData data;
        if (!ReadEncrypted(path, kind, data.bytes_, error) ||
            !data.Decode(kind, assetSet, behaviorMap, error))
            return std::nullopt;
        return data;
    }
    catch (const std::bad_alloc &)
    {
        error = Error::Allocation;
        return std::nullopt;
    }
    catch (...)
    {
        error = Error::Read;
        return std::nullopt;
    }
}

bool WorldFileData::Decode(Kind kind, int assetSet, int behaviorMap, Error &error)
{
    if (bytes_[0] != 0)
    {
        error = Error::Header;
        return false;
    }
    if (bytes_[1] != assetSet)
    {
        error = Error::Map;
        return false;
    }
    assetSet_ = bytes_[1];
    switch (kind)
    {
    case Kind::Mapping:
        if (bytes_.size() == MappingSize)
            return true;
        error = Error::Size;
        return false;
    case Kind::Attributes:
        return DecodeAttributes(behaviorMap, error);
    case Kind::Placements:
        return DecodePlacements(error);
    }
    return false;
}

bool WorldFileData::DecodeAttributes(int behaviorMap, Error &error)
{
    if (bytes_.size() != NarrowAttributeSize && bytes_.size() != WideAttributeSize)
    {
        error = Error::Size;
        return false;
    }
    if (bytes_[2] != TERRAIN_SIZE_MASK || bytes_[3] != TERRAIN_SIZE_MASK)
    {
        error = Error::Header;
        return false;
    }
    const std::size_t stride = bytes_.size() == WideAttributeSize ? 2 : 1;
    walls_.resize(CellCount);
    for (std::size_t index = 0; index < CellCount; ++index)
    {
        // Preserve legacy effective attributes: only the low byte is gameplay data.
        walls_[index] = bytes_[AttributeHeaderSize + index * stride];
        if (walls_[index] >= FirstInvalidAttribute)
        {
            error = Error::Attributes;
            return false;
        }
    }
    constexpr std::array sentinelX{135, 227, 208, 186, 193};
    constexpr std::array sentinelY{123, 120, 55, 119, 75};
    constexpr std::array sentinelValue{5, 4, 5, 5, 5};
    if (behaviorMap >= 0 && behaviorMap < static_cast<int>(sentinelX.size()))
    {
        const auto offset = AttributeHeaderSize + stride * (sentinelY[behaviorMap] * TERRAIN_SIZE +
                                                            sentinelX[behaviorMap]);
        const int authoredValue = bytes_[offset] | (stride == 2 ? bytes_[offset + 1] << 8 : 0);
        if (authoredValue != sentinelValue[behaviorMap])
        {
            error = Error::Attributes;
            return false;
        }
    }
    bytes_.clear();
    bytes_.shrink_to_fit();
    return true;
}

bool WorldFileData::DecodePlacements(Error &error)
{
    if (bytes_.size() < PlacementHeaderSize)
    {
        error = Error::Size;
        return false;
    }
    std::int16_t count;
    std::memcpy(&count, bytes_.data() + 2, sizeof(count));
    if (count < 0 || bytes_.size() != PlacementHeaderSize + count * PlacementSize)
    {
        error = Error::Size;
        return false;
    }
    placements_.reserve(count);
    const auto *source = bytes_.data() + PlacementHeaderSize;
    for (int index = 0; index < count; ++index)
    {
        Placement placement;
        std::memcpy(&placement.type, source, sizeof(placement.type));
        source += sizeof(placement.type);
        std::memcpy(placement.position.data(), source, sizeof(placement.position));
        source += sizeof(placement.position);
        std::memcpy(placement.angle.data(), source, sizeof(placement.angle));
        source += sizeof(placement.angle);
        std::memcpy(&placement.scale, source, sizeof(placement.scale));
        source += sizeof(placement.scale);
        if (CanRealizePlacement(placement))
            placements_.push_back(placement);
        else
            ++skippedPlacements_;
    }
    bytes_.clear();
    bytes_.shrink_to_fit();
    return true;
}

std::span<const std::uint8_t> WorldFileData::Layer1() const noexcept
{
    return {bytes_.data() + MappingHeaderSize, CellCount};
}
std::span<const std::uint8_t> WorldFileData::Layer2() const noexcept
{
    return {bytes_.data() + MappingHeaderSize + CellCount, CellCount};
}
std::span<const std::uint8_t> WorldFileData::Alpha() const noexcept
{
    return {bytes_.data() + MappingHeaderSize + CellCount * 2, CellCount};
}

const wchar_t *WorldFileData::ErrorText(Error error) noexcept
{
    switch (error)
    {
    case Error::None:
        return L"";
    case Error::Open:
        return L"cannot open world resource";
    case Error::Read:
        return L"cannot read complete world resource";
    case Error::Size:
        return L"invalid world resource size or record count";
    case Error::Header:
        return L"unsupported world resource header";
    case Error::Map:
        return L"world resource belongs to another asset set";
    case Error::Attributes:
        return L"invalid legacy terrain attributes";
    case Error::Values:
        return L"invalid resource values";
    case Error::Allocation:
        return L"world resource allocation failed";
    }
    return L"world resource decode failed";
}

namespace
{
constexpr std::size_t Cells = TERRAIN_SIZE * TERRAIN_SIZE;
constexpr std::size_t HeightWrapperSize = 4;
constexpr std::size_t ExtendedHeaderSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
constexpr std::size_t JpegWrapperSize = 24;

bool ReadImage(const std::filesystem::path &path, WorldImageData::Kind kind,
               std::vector<std::uint8_t> &bytes, WorldImageData::Error &error)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
    {
        error = WorldImageData::Error::Open;
        return false;
    }
    const auto size = input.tellg();
    const auto expected = kind == WorldImageData::Kind::Height
                              ? HeightWrapperSize + WorldImageData::LegacyHeightHeaderSize + Cells
                              : HeightWrapperSize + ExtendedHeaderSize + Cells * 3;
    const bool validSize =
        kind == WorldImageData::Kind::Light
            ? size > static_cast<std::streamoff>(JpegWrapperSize) &&
                  size <= (std::numeric_limits<std::streamsize>::max)() &&
                  size <= static_cast<std::streamoff>((std::numeric_limits<unsigned long>::max)())
            : size == static_cast<std::streamoff>(expected);
    if (!validSize)
    {
        error = WorldImageData::Error::Size;
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char *>(bytes.data()), size))
    {
        error = WorldImageData::Error::Read;
        return false;
    }
    return true;
}
} // namespace

std::optional<WorldImageData> WorldImageData::Load(const std::filesystem::path &path, Kind kind,
                                                   float heightScale, Error &error) noexcept
{
    error = Error::None;
    try
    {
        std::vector<std::uint8_t> bytes;
        if (!ReadImage(path, kind, bytes, error))
            return std::nullopt;
        WorldImageData data;
        const bool decoded = kind == Kind::Light
                                 ? data.DecodeLight(bytes, error)
                                 : data.DecodeHeight(bytes, kind, heightScale, error);
        if (!decoded)
            return std::nullopt;
        return data;
    }
    catch (const std::bad_alloc &)
    {
        error = Error::Allocation;
        return std::nullopt;
    }
    catch (...)
    {
        error = Error::Read;
        return std::nullopt;
    }
}

bool WorldImageData::DecodeHeight(std::span<const std::uint8_t> bytes, Kind kind, float scale,
                                  Error &error)
{
    const auto payload = bytes.subspan(HeightWrapperSize);
    if (kind == Kind::Height)
    {
        // Legacy offset is authoritative, even for the blank World19 header.
        std::copy_n(payload.begin(), heightHeader_.size(), heightHeader_.begin());
        values_.resize(Cells);
        std::transform(payload.begin() + LegacyHeightHeaderSize, payload.end(), values_.begin(),
                       [scale](std::uint8_t height) { return height * scale; });
        return true;
    }
    BITMAPFILEHEADER fileHeader;
    BITMAPINFOHEADER imageHeader;
    std::memcpy(&fileHeader, payload.data(), sizeof(fileHeader));
    std::memcpy(&imageHeader, payload.data() + sizeof(fileHeader), sizeof(imageHeader));
    constexpr WORD BitmapSignature = 0x4D42;
    constexpr WORD HeightBits = 24;
    if (fileHeader.bfType != BitmapSignature || fileHeader.bfOffBits != ExtendedHeaderSize ||
        imageHeader.biSize != sizeof(imageHeader) || imageHeader.biWidth != TERRAIN_SIZE ||
        imageHeader.biHeight != TERRAIN_SIZE || imageHeader.biPlanes != 1 ||
        imageHeader.biBitCount != HeightBits || imageHeader.biCompression != BI_RGB)
    {
        error = Error::Header;
        return false;
    }
    values_.resize(Cells);
    const auto pixels = payload.subspan(ExtendedHeaderSize);
    for (std::size_t i = 0; i < Cells; ++i)
    {
        const auto *source = pixels.data() + i * 3;
        const auto height = source[2] | (source[1] << 8) | (source[0] << 16);
        values_[i] = static_cast<float>(height) + g_fMinHeight;
    }
    return true;
}

bool WorldImageData::DecodeLight(std::span<const std::uint8_t> bytes, Error &error)
{
    const auto jpeg = bytes.subspan(JpegWrapperSize);
    const auto destroy = [](void *handle) { tjDestroy(handle); };
    std::unique_ptr<void, decltype(destroy)> decoder(tjInitDecompress(), destroy);
    if (!decoder)
    {
        error = Error::Allocation;
        return false;
    }
    int width, height, subsampling, colorSpace;
    if (tjDecompressHeader3(decoder.get(), jpeg.data(), static_cast<unsigned long>(jpeg.size()),
                            &width, &height, &subsampling, &colorSpace) != 0 ||
        width != TERRAIN_SIZE || height != TERRAIN_SIZE)
    {
        error = Error::Header;
        return false;
    }
    std::vector<std::uint8_t> pixels(Cells * 3);
    if (tjDecompress2(decoder.get(), jpeg.data(), static_cast<unsigned long>(jpeg.size()),
                      pixels.data(), width, 0, height, TJPF_RGB, TJFLAG_BOTTOMUP) != 0)
    {
        error = Error::Read;
        return false;
    }
    values_.resize(pixels.size());
    std::transform(pixels.begin(), pixels.end(), values_.begin(),
                   [](std::uint8_t channel) { return static_cast<float>(channel) / 255.0F; });
    return true;
}

namespace
{
constexpr std::size_t RecordSize = 116;
constexpr std::size_t AuthoredPaddingSize = 45;
constexpr std::size_t PayloadSize =
    RecordSize * WorldMinimapData::MaximumMarkers + AuthoredPaddingSize;
constexpr std::size_t FileSize = PayloadSize + sizeof(std::uint32_t);
constexpr WORD ChecksumKey = 0x2BC1;
static_assert(sizeof(MINI_MAP_FILE) == RecordSize && offsetof(MINI_MAP_FILE, Name) == 16);
static_assert(MAX_MINI_MAP_DATA == WorldMinimapData::MaximumMarkers &&
              MAX_MINIMAP_NAME == WorldMinimapData::MaximumName);

int MarkerInteger(const nlohmann::json &value)
{
    if (!value.is_number_integer())
        throw std::runtime_error("Expected marker integer");
    if (value.is_number_unsigned() &&
        value.get<std::uint64_t>() > (std::numeric_limits<int>::max)())
        throw std::runtime_error("Marker integer out of range");
    const auto number = value.get<std::int64_t>();
    if (number < (std::numeric_limits<int>::min)() || number > (std::numeric_limits<int>::max)())
        throw std::runtime_error("Marker integer out of range");
    return static_cast<int>(number);
}

WorldMinimapData::Marker DecodeJsonMarker(const nlohmann::json &value)
{
    const auto kind = value.at("kind").get<std::string>();
    if (kind != "npc" && kind != "portal")
        throw std::runtime_error("Unknown marker kind");
    WorldMinimapData::Marker marker{static_cast<std::uint8_t>(kind == "npc" ? 1 : 2),
                                    {MarkerInteger(value.at("x")), MarkerInteger(value.at("y"))},
                                    MarkerInteger(value.at("rotation"))};
    const auto name = value.at("name").get<std::string>();
    if (name.find('\0') != std::string::npos)
        throw std::runtime_error("Embedded marker name terminator");
    const int length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.c_str(), -1, marker.name.data(),
                            static_cast<int>(marker.name.size()));
    if (!length)
        throw std::runtime_error("Marker name exceeds the native buffer or is invalid UTF-8");
    return marker;
}
} // namespace

std::optional<WorldMinimapData> WorldMinimapData::LoadJson(const std::filesystem::path &path,
                                                           WorldFileData::Error &error) noexcept
{
    using Error = WorldFileData::Error;
    error = Error::None;
    try
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            error = Error::Open;
            return std::nullopt;
        }
        const auto document = nlohmann::json::parse(input);
        const auto &markers = document.at("markers");
        if (!markers.is_array() || markers.size() > MaximumMarkers)
        {
            error = Error::Size;
            return std::nullopt;
        }
        WorldMinimapData data;
        if (document.contains("imageOriginPixels"))
        {
            const auto &origin = document.at("imageOriginPixels");
            data.imageOriginPixels_ = {MarkerInteger(origin.at(0)), MarkerInteger(origin.at(1))};
        }
        data.markers_.reserve(markers.size());
        for (const auto &marker : markers)
            data.markers_.push_back(DecodeJsonMarker(marker));
        return data;
    }
    catch (const std::bad_alloc &)
    {
        error = Error::Allocation;
        return std::nullopt;
    }
    catch (...)
    {
        error = Error::Values;
        return std::nullopt;
    }
}

std::optional<WorldMinimapData> WorldMinimapData::Load(const std::filesystem::path &path,
                                                       WorldFileData::Error &error) noexcept
{
    using Error = WorldFileData::Error;
    error = Error::None;
    try
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
        {
            error = Error::Open;
            return std::nullopt;
        }
        if (input.tellg() != FileSize)
        {
            error = Error::Size;
            return std::nullopt;
        }
        std::array<std::uint8_t, FileSize> bytes{};
        input.seekg(0);
        if (!input.read(reinterpret_cast<char *>(bytes.data()), bytes.size()))
        {
            error = Error::Read;
            return std::nullopt;
        }
        std::uint32_t checksum;
        std::memcpy(&checksum, bytes.data() + PayloadSize, sizeof(checksum));
        if (checksum != GenerateCheckSum2(bytes.data(), PayloadSize, ChecksumKey))
        {
            error = Error::Header;
            return std::nullopt;
        }
        WorldMinimapData data;
        if (!data.Decode(bytes, error))
            return std::nullopt;
        return data;
    }
    catch (const std::bad_alloc &)
    {
        error = Error::Allocation;
        return std::nullopt;
    }
    catch (...)
    {
        error = Error::Read;
        return std::nullopt;
    }
}

bool WorldMinimapData::Decode(std::span<std::uint8_t> bytes, WorldFileData::Error &error)
{
    markers_.reserve(MaximumMarkers);
    for (std::size_t i = 0; i < MaximumMarkers; ++i)
    {
        auto *record = bytes.data() + i * RecordSize;
        BuxConvert(record, RecordSize);
        MINI_MAP_FILE authored{};
        std::memcpy(&authored, record, RecordSize);
        if (authored.Kind == 0)
            break;
        if (authored.Kind > 2 || std::memchr(authored.Name, '\0', sizeof(authored.Name)) == nullptr)
        {
            error = WorldFileData::Error::Values;
            return false;
        }
        Marker marker{
            authored.Kind, {authored.Location[0], authored.Location[1]}, authored.Rotation};
        // Preserve legacy CP_UTF8 flags=0 replacement for shipped non-UTF-8 names.
        CMultiLanguage::ConvertFromUtf8(marker.name.data(), authored.Name);
        markers_.push_back(marker);
    }
    return true;
}

WorldReadView::WorldReadView(const World &world) noexcept : world_(world)
{
}

SessionId WorldReadView::OwnerSession() const noexcept
{
    return world_.OwnerSession();
}

std::uint64_t WorldReadView::Revision() const noexcept
{
    return world_.Revision();
}

WorldCapabilityStatus WorldReadView::Terrain() const noexcept
{
    return world_.TerrainStatus();
}

WorldCapabilityStatus WorldReadView::Entities() const noexcept
{
    return world_.EntityRegistryStatus();
}

WorldCapabilityStatus WorldReadView::Characters() const noexcept
{
    return world_.CharacterRegistryStatus();
}

WorldCapabilityStatus WorldReadView::ItemRegistry() const noexcept
{
    return world_.ItemRegistryStatus();
}

WorldCapabilityStatus WorldReadView::Map() const noexcept
{
    return world_.MapStatus();
}

WorldCapabilityStatus WorldReadView::Pathing() const noexcept
{
    return world_.PathStatus();
}

WorldCapabilityStatus WorldReadView::GameplayEffects() const noexcept
{
    return world_.GameplayEffectStatus();
}

WorldCapabilityStatus WorldReadView::ModelClock() const noexcept
{
    return world_.ModelClockStatus();
}

WorldCapabilityStatus WorldReadView::ModelRandom() const noexcept
{
    return world_.ModelRandomStatus();
}

std::optional<TerrainPickSurface> WorldReadView::TerrainPickSurfaceAt(
    TerrainCell cell) const noexcept
{
    return world_.TerrainPickSurfaceAt(cell);
}

std::optional<std::uint64_t> WorldReadView::ModelFrameNumber() const noexcept
{
    return world_.ModelFrameNumber();
}

std::optional<float> WorldReadView::HeightAt(float x, float y) const noexcept
{
    if (world_.LoadingState() != World::LoadState::Active)
        return std::nullopt;
    return world_.SampleTerrainHeight(x, y);
}

std::span<const std::uint16_t> WorldReadView::MovementAttributes() const noexcept
{
    if (world_.LoadingState() != World::LoadState::Active)
        return {};
    return {world_.TerrainWall, TerrainCell::Width * TerrainCell::Height};
}

namespace
{
std::optional<double> EnteringQuadraticRoot(double a, double b, double c, double duration)
{
    if (c < 0.0)
        return 0.0;
    if (a == 0.0)
    {
        if (b >= 0.0)
            return std::nullopt;
        const double root = -c / b;
        return root <= duration ? std::optional(root) : std::nullopt;
    }
    const double discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0)
        return std::nullopt;
    const double q = -0.5 * (b + std::copysign(std::sqrt(discriminant), b));
    double first = q == 0.0 ? 0.0 : q / a;
    double second = q == 0.0 ? 0.0 : c / q;
    if (first > second)
        std::swap(first, second);
    for (const double root : {first, second})
    {
        if (root < 0.0 || root > duration)
            continue;
        const double direction = 2.0 * a * root + b;
        if (root == 0.0 && (direction > 0.0 || (direction == 0.0 && a > 0.0)))
            continue;
        if (direction <= 0.0)
            return root;
    }
    return std::nullopt;
}

int ForwardTerrainCell(double coordinate, double velocity)
{
    const double cell = coordinate / TERRAIN_SCALE;
    int index = int(std::floor(cell));
    if (velocity < 0.0 && cell == index)
        --index;
    return index;
}
} // namespace

float World::SampleTerrainHeight(float xf, float yf) const noexcept
{
    if (SceneFlag == SERVER_LIST_SCENE || SceneFlag == WEBZEN_SCENE || SceneFlag == LOADING_SCENE)
        return 0.0f;
    if (xf < 0.0f || yf < 0.0f)
        return 0.0f;

    xf /= TERRAIN_SCALE;
    yf /= TERRAIN_SCALE;

    const int xi = static_cast<int>(xf);
    const int yi = static_cast<int>(yf);
    const unsigned int Index = static_cast<unsigned int>(yi * TERRAIN_SIZE + xi);

    if (Index >= TERRAIN_SIZE * TERRAIN_SIZE)
        return g_fSpecialHeight;

    if ((TerrainWall[Index] & TW_HEIGHT) == TW_HEIGHT)
        return g_fSpecialHeight;

    const float xd = xf - static_cast<float>(xi);
    const float yd = yf - static_cast<float>(yi);
    const unsigned int x0 = static_cast<unsigned int>(xi) & TERRAIN_SIZE_MASK;
    const unsigned int x1 = static_cast<unsigned int>(xi + 1) & TERRAIN_SIZE_MASK;
    const unsigned int y0 = static_cast<unsigned int>(yi) & TERRAIN_SIZE_MASK;
    const unsigned int y1 = static_cast<unsigned int>(yi + 1) & TERRAIN_SIZE_MASK;
    const unsigned int row0 = y0 * TERRAIN_SIZE;
    const unsigned int row1 = y1 * TERRAIN_SIZE;
    const unsigned int Index1 = row0 + x0;
    const unsigned int Index2 = row1 + x0;
    const unsigned int Index3 = row0 + x1;
    const unsigned int Index4 = row1 + x1;

    const float left =
        BackTerrainHeight[Index1] + (BackTerrainHeight[Index2] - BackTerrainHeight[Index1]) * yd;
    const float right =
        BackTerrainHeight[Index3] + (BackTerrainHeight[Index4] - BackTerrainHeight[Index3]) * yd;
    return left + (right - left) * xd;
}

std::array<double, 3> World::TerrainHeightPolynomial(const vec3_t position, const vec3_t velocity,
                                                     int x, int y) const noexcept
{
    double height = 0.0, heightRate = 0.0, heightQuadratic = 0.0;
    const auto index = static_cast<long long>(y) * TERRAIN_SIZE + x;
    if (x >= 0 && y >= 0 &&
        (index >= TERRAIN_SIZE * TERRAIN_SIZE || (TerrainWall[index] & TW_HEIGHT)))
        height = g_fSpecialHeight;
    else if (x >= 0 && y >= 0)
    {
        const int x0 = x & TERRAIN_SIZE_MASK, x1 = (x + 1) & TERRAIN_SIZE_MASK;
        const int y0 = (y & TERRAIN_SIZE_MASK) * TERRAIN_SIZE,
                  y1 = ((y + 1) & TERRAIN_SIZE_MASK) * TERRAIN_SIZE;
        const double h00 = BackTerrainHeight[y0 + x0], h10 = BackTerrainHeight[y0 + x1];
        const double h01 = BackTerrainHeight[y1 + x0], h11 = BackTerrainHeight[y1 + x1];
        const double u = position[0] / double(TERRAIN_SCALE) - x,
                     v = position[1] / double(TERRAIN_SCALE) - y;
        const double du = velocity[0] / double(TERRAIN_SCALE),
                     dv = velocity[1] / double(TERRAIN_SCALE);
        const double cross = h11 - h10 - h01 + h00;
        height = h00 + (h10 - h00) * u + (h01 - h00) * v + cross * u * v;
        heightRate = (h10 - h00) * du + (h01 - h00) * dv + cross * (u * dv + v * du);
        heightQuadratic = cross * du * dv;
    }
    return {height, heightRate, heightQuadratic};
}

float World::TerrainHeightRate(const vec3_t position, const vec3_t velocity) const noexcept
{
    if (SceneFlag == SERVER_LIST_SCENE || SceneFlag == WEBZEN_SCENE || SceneFlag == LOADING_SCENE)
        return 0.f;
    const int x = ForwardTerrainCell(position[0], velocity[0]);
    const int y = ForwardTerrainCell(position[1], velocity[1]);
    return static_cast<float>(TerrainHeightPolynomial(position, velocity, x, y)[1]);
}

std::optional<TerrainContact> World::FirstTerrainContact(const vec3_t position,
                                                         const vec3_t velocity,
                                                         float verticalAcceleration, float frames,
                                                         float clearance,
                                                         bool solidOnly) const noexcept
{
    if (frames <= 0.f)
        return std::nullopt;
    const double acceleration = verticalAcceleration * 0.5;
    if (SceneFlag == SERVER_LIST_SCENE || SceneFlag == WEBZEN_SCENE || SceneFlag == LOADING_SCENE)
    {
        const auto hit =
            EnteringQuadraticRoot(acceleration, velocity[2], position[2] - clearance, frames);
        return hit ? std::optional(TerrainContact{float(*hit), clearance, 0.f}) : std::nullopt;
    }
    int x = ForwardTerrainCell(position[0], velocity[0]);
    int y = ForwardTerrainCell(position[1], velocity[1]);
    const int stepX = velocity[0] < 0.f ? -1 : 1, stepY = velocity[1] < 0.f ? -1 : 1;
    const double infinity = std::numeric_limits<double>::infinity();
    const double deltaX =
        velocity[0] == 0.f ? infinity : TERRAIN_SCALE / std::abs(double(velocity[0]));
    const double deltaY =
        velocity[1] == 0.f ? infinity : TERRAIN_SCALE / std::abs(double(velocity[1]));
    double nextX = velocity[0] == 0.f
                       ? infinity
                       : ((x + (stepX > 0)) * double(TERRAIN_SCALE) - position[0]) / velocity[0];
    double nextY = velocity[1] == 0.f
                       ? infinity
                       : ((y + (stepY > 0)) * double(TERRAIN_SCALE) - position[1]) / velocity[1];
    double time = 0.0;
    while (true)
    {
        const auto [height, heightRate, heightQuadratic] =
            TerrainHeightPolynomial(position, velocity, x, y);
        const double a = acceleration - heightQuadratic, b = velocity[2] - heightRate;
        double c = (a * time + b) * time + position[2] - clearance - height;
        const double currentHeight = (heightQuadratic * time + heightRate) * time + height;
        const double tolerance =
            4.0 * std::numeric_limits<float>::epsilon() * (std::max)(1.0, std::abs(currentHeight));
        if (std::abs(c) <= tolerance)
            c = 0.0;
        const double end = (std::min)({double(frames), nextX, nextY});
        const bool solid =
            !solidOnly || (TerrainWall[TERRAIN_INDEX_REPEAT(x, y)] & TW_NOGROUND) == 0;
        if (const auto hit =
                solid ? EnteringQuadraticRoot(a, 2.0 * a * time + b, c, end - time) : std::nullopt)
        {
            const double contact = time + *hit;
            // At a height discontinuity, the forward cell owns contact.
            if (contact < end || (end != nextX && end != nextY))
                return TerrainContact{
                    float(contact),
                    float((heightQuadratic * contact + heightRate) * contact + height + clearance),
                    float(heightRate + 2.0 * heightQuadratic * contact)};
        }
        if (end == frames && nextX > end && nextY > end)
            break;
        if (nextX <= end)
        {
            x += stepX;
            nextX += deltaX;
        }
        if (nextY <= end)
        {
            y += stepY;
            nextY += deltaY;
        }
        time = end;
    }
    return std::nullopt;
}

BYTE World::TerrainAttribute(float x, float y) const noexcept
{
    const int xf = static_cast<int>(x / TERRAIN_SCALE + 0.5f);
    const int yf = static_cast<int>(y / TERRAIN_SCALE + 0.5f);
    return static_cast<BYTE>(TerrainWall[yf * TERRAIN_SIZE + xf]);
}

WORD *World::MovementGrid() noexcept
{
    return TerrainWall;
}

std::optional<TerrainPickSurface> World::TerrainPickSurfaceAt(TerrainCell cell) const noexcept
{
    if (loadState_ != LoadState::Active || cell.X() >= TERRAIN_SIZE_MASK ||
        cell.Y() >= TERRAIN_SIZE_MASK)
        return std::nullopt;
    const int first = cell.LinearIndex();
    const std::array indices{first, first + 1, first + TERRAIN_SIZE + 1, first + TERRAIN_SIZE};
    TerrainPickSurface::Heights heights;
    TerrainPickSurface::WallBits walls;
    for (int corner = 0; corner < 4; ++corner)
    {
        heights[corner] = BackTerrainHeight[indices[corner]];
        walls[corner] = TerrainWall[indices[corner]];
    }
    return TerrainPickSurface(cell, heights, walls, g_fSpecialHeight);
}

std::bitset<64 * 64> World::TerrainContentChanged(int x, int y, int width, int height) noexcept
{
    auto &terrain = sessionKeeper_.TerrainStorage();
    terrain.contentRevision = applicationKeeper_.NextTerrainContentRevision();
    std::bitset<64 * 64> affected;
    // A corner change also affects the cells immediately above and to the left.
    const int lastX = x + std::min(width, TERRAIN_SIZE);
    const int lastY = y + std::min(height, TERRAIN_SIZE);
    for (int row = (y - 1) & ~3; row < lastY; row += 4)
        for (int column = (x - 1) & ~3; column < lastX; column += 4)
            affected[((row & TERRAIN_SIZE_MASK) / 4) * 64 + ((column & TERRAIN_SIZE_MASK) / 4)] =
                true;
    terrain.revisedBlocks |= affected;
    return affected;
}

void World::CommitTerrainBase(std::shared_ptr<const WorldTerrainAsset> asset) noexcept
{
    auto &terrain = sessionKeeper_.TerrainStorage();
    terrain.BindBase(std::move(asset));
    terrain.contentRevision = terrain.BaseTerrain->revision != 0
                                  ? terrain.BaseTerrain->revision
                                  : applicationKeeper_.NextTerrainContentRevision();
    terrain.revisedBlocks.reset();
}

bool World::ClearPathDestination(int index) noexcept
{
    if (index < 0 || index >= TERRAIN_SIZE * TERRAIN_SIZE || TerrainWall[index] == 0)
        return true;
    const WORD previous = TerrainWall[index];
    TerrainWall[index] = 0;
    const int x = index % TERRAIN_SIZE, y = index / TERRAIN_SIZE;
    if ((previous & TW_NOGROUND) && !AdmitTerrainSurface(x, y, 1, 1))
        return false;
    TerrainContentChanged(x, y, 1, 1);
    return true;
}

void World::SampleTerrainNormal(float x, float y, vec3_t normal) const noexcept
{
    const int column = static_cast<int>(x / TERRAIN_SCALE);
    const int row = static_cast<int>(y / TERRAIN_SCALE);
    VectorCopy(TerrainNormal[TERRAIN_INDEX_REPEAT(column, row)], normal);
}

int World::RetryPathWall(int sx, int sy, int tx, int ty, int defaultWall) const noexcept
{
    const WORD start = TerrainWall[TERRAIN_INDEX_REPEAT(sx, sy)];
    const WORD target = TerrainWall[TERRAIN_INDEX_REPEAT(tx, ty)];
    return ((start | target) & TW_SAFEZONE) != 0 && (target & TW_CHARACTER) == 0 ? TW_NOMOVE
                                                                                 : defaultWall;
}

bool World::AdmitTerrainSurface(int x, int y, int width, int height) noexcept
{
    const auto &terrain = sessionKeeper_.TerrainStorage();
    const auto *definition = Binding().definition;
    if (!definition)
        return FinishLoad(false);
    const auto required = WorldTerrain::Surface::RequiredTiles(
        *definition, terrain.EffectiveMapping(), TerrainWall, x, y, width, height);
    if ((required & ~terrain.admittedTiles).any())
        return FinishLoad(false);
    return true;
}

void World::ChangeTerrainHeight(float x, float y, float delta, int range)
{
    const float tileX = x / TERRAIN_SCALE, tileY = y / TERRAIN_SCALE;
    const int centerX = static_cast<int>(tileX), centerY = static_cast<int>(tileY);
    for (int row = centerY - range; row <= centerY + range; ++row)
        for (int column = centerX - range; column <= centerX + range; ++column)
        {
            const float dx = tileX - column, dy = tileY - row;
            const float weight = (range - sqrtf(dx * dx + dy * dy)) / range;
            if (weight > 0.f)
                BackTerrainHeight[TERRAIN_INDEX_REPEAT(column, row)] += delta * weight;
        }
    const auto affected =
        TerrainContentChanged(centerX - range, centerY - range, range * 2 + 1, range * 2 + 1);
    for (int block = 0; block < affected.size(); ++block)
        if (affected[block])
            sessionKeeper_.Renderer()->RefreshTerrainLighting((block % 64) * 4, (block / 64) * 4, 4,
                                                              4);
}

bool World::ReloadTerrainVariant(MapDefinition::Variant variant) noexcept
{
    if (!Binding().definition)
        return false;
    try
    {
        WorldResources::Failure failure;
        auto resources = WorldResources::Load(*Binding().definition, variant, L"Data", failure,
                                              &applicationKeeper_);
        if (!resources ||
            !resources->PrepareTerrainTextures(
                sessionKeeper_, *Binding().definition, L"Data", failure,
                sessionKeeper_.TerrainStorage().EditedMapping.get()) ||
            !resources->InstallTerrainTextures(sessionKeeper_, failure))
        {
            FinishLoad(false);
            return false;
        }
        // Prepare both payloads and any newly visible surface before replacing either grid.
        auto &terrain = sessionKeeper_.TerrainStorage();
        const bool edited = terrain.revisedBlocks.any() || terrain.EditedMapping != nullptr;
        InstallTerrainAttributes(resources->Attributes());
        InstallTerrainLight(resources->Light());
        if (edited)
        {
            terrain.BindBase(resources->TerrainAsset());
            TerrainContentChanged();
        }
        else
            CommitTerrainBase(resources->TerrainAsset());
        return true;
    }
    catch (...)
    {
        return FinishLoad(false);
    }
}

// Includes mirror ZzzInterface.cpp, the unit this was extracted from.

void SessionGameplayUnit::EditTerrainMapping()
{
    int sx = REFERENCE_WIDTH - 30;
    int sy = 0;
    for (int i = 0; i < 14; i++)
    {
        if (MouseX >= sx && MouseY >= sy + i * 30 && MouseX < sx + 30 && MouseY < sy + i * 30 + 30)
        {
            if (MouseLButton)
            {
                SelectMapping = i;
                return;
            }
        }
    }
    int x = (int)SelectXF;
    int y = (int)SelectYF;
    int Index1 = TERRAIN_INDEX_REPEAT(x, y);
    int Index2 = TERRAIN_INDEX_REPEAT(x + 1, y);
    int Index3 = TERRAIN_INDEX_REPEAT(x + 1, y + 1);
    int Index4 = TERRAIN_INDEX_REPEAT(x, y + 1);
    if (Bitmaps[BITMAP_MAPTILE + SelectMapping].Components != 4 && (MouseLButton || MouseRButton))
    {
        auto &mapping = sessionKeeper_.TerrainStorage().MutableMapping();
        if (MouseLButton)
        {
            if (CurrentLayer == 0)
            {
                for (int i = y - BrushSize; i <= y + BrushSize; i++)
                {
                    for (int j = x - BrushSize; j <= x + BrushSize; j++)
                    {
                        mapping.layer1[TERRAIN_INDEX_REPEAT(j, i)] = SelectMapping;
                    }
                }
            }
            if (CurrentLayer == 1)
            {
                for (int i = y - 1; i <= y + 1; i++)
                {
                    for (int j = x - 1; j <= x + 1; j++)
                    {
                        mapping.layer2[TERRAIN_INDEX_REPEAT(j, i)] = SelectMapping;
                    }
                }
                mapping.alpha[Index1] += 0.1f;
                mapping.alpha[Index2] += 0.1f;
                mapping.alpha[Index3] += 0.1f;
                mapping.alpha[Index4] += 0.1f;
                if (mapping.alpha[Index1] > 1.f)
                    mapping.alpha[Index1] = 1.f;
                if (mapping.alpha[Index2] > 1.f)
                    mapping.alpha[Index2] = 1.f;
                if (mapping.alpha[Index3] > 1.f)
                    mapping.alpha[Index3] = 1.f;
                if (mapping.alpha[Index4] > 1.f)
                    mapping.alpha[Index4] = 1.f;
            }
        }
        if (MouseRButton)
        {
            if (CurrentLayer == 1)
            {
                for (int i = y - 1; i <= y + 1; i++)
                {
                    for (int j = x - 1; j <= x + 1; j++)
                    {
                        mapping.layer2[TERRAIN_INDEX_REPEAT(j, i)] = 255;
                    }
                }
                mapping.alpha[Index1] = 0.f;
                mapping.alpha[Index2] = 0.f;
                mapping.alpha[Index3] = 0.f;
                mapping.alpha[Index4] = 0.f;
            }
        }
        const int radius = std::max(BrushSize, 1) + 1;
        if (!sessionKeeper_.WorldUnit()->AdmitTerrainSurface(x - radius - 1, y - radius - 1,
                                                             radius * 2 + 2, radius * 2 + 2))
            return;
        sessionKeeper_.WorldUnit()->TerrainContentChanged(x - radius, y - radius, radius * 2 + 1,
                                                          radius * 2 + 1);
    }
}

void CMapManager::InstallWorldLayout(WorldResources &resources)
{
    sessionKeeper_.TerrainStorage().BaseTerrain = resources.TerrainAsset();
    InstallTerrainMapping(resources.Mapping());
    if (resources.Camera())
        cameraMove_.InstallCameraWalkScript(std::move(*resources.Camera()));
    InstallTerrainAttributes(resources.Attributes());
}

void WorldResources::PrepareMinimap(SessionKeeper &keeper, const MapDefinition &map,
                                    const std::filesystem::path &root)
{
    minimapTexture_.reset();
    minimap_ = {};
    const int raw = map.id.RawValue();
    if (raw == WD_73NEW_LOGIN_SCENE || raw == WD_74NEW_CHARACTER_SCENE)
        return;
    const auto world = L"World" + std::to_wstring(map.assetSet);
    const auto folder = root / L"UI" / L"PC" / L"Maps" / world;
    const auto path = folder / L"layout.tga";
    constexpr auto Filter = LegacyTextureFilter::Linear;
    constexpr auto Wrap = LegacyTextureWrap::ClampToEdge;
    if (!terrainTextures_->Load(BITMAP_MINI_MAP_BEGIN, path.wstring(), keeper.ErrorReport(), Filter,
                                Wrap))
        return;
    minimapTexture_ = PreparedTexture{BITMAP_MINI_MAP_BEGIN, path.wstring(), Filter, Wrap};
    const auto &language = keeper.AssetLanguage();
    const auto markersPath = folder / (L"positions." + language + L".json");
    WorldFileData::Error error;
    if (auto markers = WorldMinimapData::LoadJson(markersPath, error))
        minimap_ = std::move(*markers);
    else if (error != WorldFileData::Error::Open)
        keeper.ErrorReport().Write(L"%ls: optional minimap markers ignored (%ls)\r\n",
                                   markersPath.c_str(), WorldFileData::ErrorText(error));
}

BYTE SessionGameplayUnit::TERRAIN_ATTRIBUTE(float x, float y)
{
    return sessionKeeper_.WorldUnit()->TerrainAttribute(x, y);
}

void SessionRenderUnit::InitTerrainMappingLayer()
{
    sessionKeeper_.TerrainStorage().BindMapping({});
    InitializeTerrainGrass();
    sessionKeeper_.WorldUnit()->TerrainContentChanged();
    terrainGeometryCache_.Invalidate();
}

void SessionRenderUnit::InstallTerrainAttributes(const WorldFileData &data)
{
    std::copy(data.Walls().begin(), data.Walls().end(), TerrainWall);
    sessionKeeper_.WorldUnit()->TerrainContentChanged();
    terrainGeometryCache_.Invalidate();
}

bool SessionRenderUnit::SaveTerrainAttribute(wchar_t *FileName, int iMap)
{
    FILE *fp = _wfopen(FileName, L"wb");
    if (fp == NULL)
    {
        wchar_t Text[256];
        mu_swprintf_s(Text, std::size(Text), L"%ls file not found.", FileName);
        g_ErrorReport.Write(Text);
        g_ErrorReport.Write(L"\r\n");
        MessageBox(g_hWnd, Text, NULL, MB_OK);
        SendMessage(g_hWnd, WM_DESTROY, 0, 0);
        return false;
    }
    const BYTE Version = 0;
    const BYTE Width = 255;
    const BYTE Height = 255;

    fwrite(&Version, sizeof(Version), 1, fp);
    fwrite(&iMap, sizeof(iMap), 1, fp);
    fwrite(&Width, sizeof(Width), 1, fp);
    fwrite(&Height, sizeof(Height), 1, fp);

    fwrite(TerrainWall, TERRAIN_SIZE * TERRAIN_SIZE * sizeof(WORD), 1, fp);

    fclose(fp);
    return true;
}

void SessionGameplayUnit::AddTerrainAttribute(int x, int y, BYTE att)
{
    int iIndex = (x + (y * TERRAIN_SIZE));
    const WORD previous = TerrainWall[iIndex];
    TerrainWall[iIndex] |= att;
    if (previous == TerrainWall[iIndex])
        return;
    sessionKeeper_.WorldUnit()->TerrainContentChanged(x, y, 1, 1);
}

void SessionGameplayUnit::SubTerrainAttribute(int x, int y, BYTE att)
{
    int iIndex = (x + (y * TERRAIN_SIZE));

    const WORD previous = TerrainWall[iIndex];
    TerrainWall[iIndex] ^= (TerrainWall[iIndex] & att);
    if (previous == TerrainWall[iIndex])
        return;
    if ((att & TW_NOGROUND) && !sessionKeeper_.WorldUnit()->AdmitTerrainSurface(x, y, 1, 1))
        return;
    sessionKeeper_.WorldUnit()->TerrainContentChanged(x, y, 1, 1);
}

void SessionGameplayUnit::AddTerrainAttributeRange(int x, int y, int width, int height,
                                                   BYTE attribute, BYTE add)
{
    bool changed = false;
    for (int row = y; row < y + height; ++row)
        for (int column = x; column < x + width; ++column)
        {
            auto &value = TerrainWall[TERRAIN_INDEX(column, row)];
            const WORD next = add ? value | attribute : value & ~attribute;
            changed |= next != value;
            value = next;
        }
    if (!changed)
        return;
    if (!add && (attribute & TW_NOGROUND) &&
        !sessionKeeper_.WorldUnit()->AdmitTerrainSurface(x, y, width, height))
        return;
    sessionKeeper_.WorldUnit()->TerrainContentChanged(x, y, width, height);
}

void SessionRenderUnit::SetTerrainWaterState(std::list<int> &terrainIndex, int state)
{
    sessionKeeper_.WorldUnit()->TerrainContentChanged();
    terrainGeometryCache_.Invalidate();
    if (state == 0)
    {
        terrainIndex.clear();
        for (int i = 0; i < TERRAIN_SIZE * TERRAIN_SIZE; ++i)
        {
            if ((TerrainWall[i] & TW_WATER) == TW_WATER)
            {
                const WORD previous = TerrainWall[i];
                TerrainWall[i] = 0;
                if ((previous & TW_NOGROUND) && !sessionKeeper_.WorldUnit()->AdmitTerrainSurface(
                                                    i % TERRAIN_SIZE, i / TERRAIN_SIZE, 1, 1))
                    return;
                terrainIndex.push_back(i);
            }
        }
    }
    else
    {
        for (std::list<int>::iterator iter = terrainIndex.begin(); iter != terrainIndex.end();
             ++iter)
        {
            int index = *iter;
            const WORD previous = TerrainWall[index];
            TerrainWall[index] = TW_WATER;
            if ((previous & TW_NOGROUND) && !sessionKeeper_.WorldUnit()->AdmitTerrainSurface(
                                                index % TERRAIN_SIZE, index / TERRAIN_SIZE, 1, 1))
                return;
        }
    }
}

void SessionRenderUnit::InstallTerrainMapping(const WorldFileData &data)
{
    auto &terrain = sessionKeeper_.TerrainStorage();
    const auto &base = terrain.BaseTerrain;
    terrain.BindMapping(base && &data == &base->mapping
                            ? std::shared_ptr<const TerrainMappingData>(base, &base->surface)
                            : std::make_shared<const TerrainMappingData>(data));
    InitializeTerrainGrass();
    TerrainGrassEnable = TheMapProcess().TerrainPolicy().grassEnabled;
    sessionKeeper_.WorldUnit()->TerrainContentChanged();
    terrainGeometryCache_.Invalidate();
}

bool SessionRenderUnit::SaveTerrainMapping(wchar_t *FileName, int iMapNumber)
{
    FILE *fp = _wfopen(FileName, L"wb");
    BYTE Version = 0;
    fwrite(&Version, 1, 1, fp);
    fwrite(&iMapNumber, 1, 1, fp);
    fwrite(TerrainMappingLayer1, TERRAIN_SIZE * TERRAIN_SIZE, 1, fp);
    fwrite(TerrainMappingLayer2, TERRAIN_SIZE * TERRAIN_SIZE, 1, fp);
    for (int i = 0; i < TERRAIN_SIZE * TERRAIN_SIZE; i++)
    {
        auto Alpha = (unsigned char)(TerrainMappingAlpha[i] * 255.f);
        fwrite(&Alpha, 1, 1, fp);
    }
    fclose(fp);

    {
        fp = _wfopen(FileName, L"rb");
        if (fp == NULL)
        {
            return (false);
        }
        fseek(fp, 0, SEEK_END);
        int EncBytes = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        auto *EncData = new unsigned char[EncBytes];
        fread(EncData, 1, EncBytes, fp);
        fclose(fp);

        int DataBytes = MapFileEncrypt(NULL, EncData, EncBytes);
        auto *Data = new unsigned char[DataBytes];
        MapFileEncrypt(Data, EncData, EncBytes);
        delete[] EncData;

        fp = _wfopen(FileName, L"wb");
        fwrite(Data, DataBytes, 1, fp);
        fclose(fp);
        delete[] Data;
    }
    return true;
}

void SessionRenderUnit::CalculateTerrainNormal(int x, int y)
{
    const int index = TERRAIN_INDEX_REPEAT(x, y);
    vec3_t first, second, third, fourth, normal;
    Vector(x * TERRAIN_SCALE, y * TERRAIN_SCALE, BackTerrainHeight[index], fourth);
    Vector((x + 1) * TERRAIN_SCALE, y * TERRAIN_SCALE,
           BackTerrainHeight[TERRAIN_INDEX_REPEAT(x + 1, y)], first);
    Vector((x + 1) * TERRAIN_SCALE, (y + 1) * TERRAIN_SCALE,
           BackTerrainHeight[TERRAIN_INDEX_REPEAT(x + 1, y + 1)], second);
    Vector(x * TERRAIN_SCALE, (y + 1) * TERRAIN_SCALE,
           BackTerrainHeight[TERRAIN_INDEX_REPEAT(x, y + 1)], third);
    FaceNormalize(first, second, third, TerrainNormal[index]);
    FaceNormalize(third, fourth, first, normal);
    VectorAdd(TerrainNormal[index], normal, TerrainNormal[index]);
}

void SessionRenderUnit::CreateTerrainNormal()
{
    for (int y = 0; y < TERRAIN_SIZE; ++y)
        for (int x = 0; x < TERRAIN_SIZE; ++x)
            CalculateTerrainNormal(x, y);
}

void SessionRenderUnit::CreateTerrainNormal_Part(int x, int y)
{
    x = std::clamp(x, 4, TERRAIN_SIZE - 4);
    y = std::clamp(y, 4, TERRAIN_SIZE - 4);
    for (int row = y - 4; row < y + 4; ++row)
        for (int column = x - 4; column < x + 4; ++column)
            CalculateTerrainNormal(column, row);
}

void SessionRenderUnit::RefreshTerrainLighting(int x, int y, int width, int height)
{
    const auto &direction = TheMapProcess().TerrainPolicy().lightDirection;
    for (int row = y; row < y + height; ++row)
        for (int column = x; column < x + width; ++column)
        {
            CalculateTerrainNormal(column, row);
            const int index = TERRAIN_INDEX_REPEAT(column, row);
            const float luminosity =
                std::clamp(DotProduct(TerrainNormal[index], direction) + 0.5f, 0.f, 1.f);
            for (int channel = 0; channel < 3; ++channel)
                BackTerrainLight[index][channel] = TerrainLight[index][channel] * luminosity;
        }
}

void SessionRenderUnit::CreateTerrainLight()
{
    sessionKeeper_.WorldUnit()->TerrainContentChanged();
    terrainGeometryCache_.Invalidate();
    const auto &Light = TheMapProcess().TerrainPolicy().lightDirection;
    for (int y = 0; y < TERRAIN_SIZE; y++)
    {
        for (int x = 0; x < TERRAIN_SIZE; x++)
        {
            int Index = TERRAIN_INDEX(x, y);
            float Luminosity = DotProduct(TerrainNormal[Index], Light) + 0.5f;
            if (Luminosity < 0.f)
                Luminosity = 0.f;
            else if (Luminosity > 1.f)
                Luminosity = 1.f;
            for (int i = 0; i < 3; i++)
            {
                BackTerrainLight[Index][i] = TerrainLight[Index][i] * Luminosity;
            }
        }
    }
}

void SessionRenderUnit::CreateTerrainLight_Part(int xi, int yi)
{
    sessionKeeper_.WorldUnit()->TerrainContentChanged();
    terrainGeometryCache_.Invalidate();
    if (xi > TERRAIN_SIZE - 4)
        xi = TERRAIN_SIZE - 4;
    else if (xi < 4)
        xi = 4;

    if (yi > TERRAIN_SIZE - 4)
        yi = TERRAIN_SIZE - 4;
    else if (yi < 4)
        yi = 4;

    vec3_t Light;
    Vector(0.5f, -0.5f, 0.5f, Light);
    for (int y = yi - 4; y < yi + 4; y++)
    {
        for (int x = xi - 4; x < xi + 4; x++)
        {
            int Index = TERRAIN_INDEX(x, y);
            float Luminosity = DotProduct(TerrainNormal[Index], Light) + 0.5f;
            if (Luminosity < 0.f)
                Luminosity = 0.f;
            else if (Luminosity > 1.f)
                Luminosity = 1.f;
            for (int i = 0; i < 3; i++)
                BackTerrainLight[Index][i] = TerrainLight[Index][i] * Luminosity;
        }
    }
}

void SessionRenderUnit::InstallTerrainLight(const WorldImageData &data)
{
    std::memcpy(TerrainLight, data.Values().data(), data.Values().size_bytes());
    CreateTerrainNormal();
    CreateTerrainLight();
}

void SessionRenderUnit::SaveTerrainLight(wchar_t *FileName)
{
    auto *Buffer = new unsigned char[TERRAIN_SIZE * TERRAIN_SIZE * 3];
    for (int i = 0; i < TERRAIN_SIZE * TERRAIN_SIZE; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            float Light = TerrainLight[i][j] * 255.f;
            Light = (Light < 0.f) ? 0.f : (Light > 255.f) ? 255.f : Light;
            Buffer[i * 3 + j] = static_cast<unsigned char>(Light);
        }
    }
    WriteJpeg(FileName, TERRAIN_SIZE, TERRAIN_SIZE, Buffer, 100);
    delete[] Buffer;
}

void SessionRenderUnit::CreateTerrain(const WorldImageData &data, bool extended)
{
    InstallTerrainHeight(data, extended);
    ActiveTerrain = true;
    CreateSun();
}

void SessionRenderUnit::InstallTerrainHeight(const WorldImageData &data, bool extended)
{
    std::copy(data.Values().begin(), data.Values().end(), BackTerrainHeight);
    if (!extended)
        std::copy(data.LegacyHeightHeader().begin(), data.LegacyHeightHeader().end(), BMPHeader);
    sessionKeeper_.WorldUnit()->TerrainContentChanged();
    terrainGeometryCache_.Invalidate();
}

void SessionRenderUnit::SaveTerrainHeight(wchar_t *name)
{
    auto *Buffer = new unsigned char[256 * 256];
    for (int i = 0; i < 256; i++)
    {
        float *src = &BackTerrainHeight[i * 256];
        unsigned char *dst = &Buffer[(255 - i) * 256];
        const float factor = TheMapProcess().TerrainPolicy().heightSaveScale;
        for (int j = 0; j < 256; j++)
        {
            float Height = *src / factor;
            if (Height < 0.f)
                Height = 0.f;
            else if (Height > 255.f)
                Height = 255.f;
            *dst = (unsigned char)(Height);
            src++;
            dst++;
        }
    }
    FILE *fp = _wfopen(name, L"wb");
    fwrite(BMPHeader, 1080, 1, fp);

    for (int i = 0; i < 256; i++)
        fwrite(Buffer + (255 - i) * 256, 256, 1, fp);

    SAFE_DELETE_ARRAY(Buffer);
    fclose(fp);
}

float SessionGameplayUnit::RequestTerrainHeight(float x, float y)
{
    return sessionKeeper_.WorldUnit()->SampleTerrainHeight(x, y);
}

void SessionGameplayUnit::RequestTerrainNormal(float x, float y, vec3_t normal)
{
    sessionKeeper_.WorldUnit()->SampleTerrainNormal(x, y, normal);
}

void SessionGameplayUnit::RequestTerrainLight(float xf, float yf, vec3_t Light)
{
    if (SceneFlag == SERVER_LIST_SCENE || SceneFlag == WEBZEN_SCENE || SceneFlag == LOADING_SCENE ||
        ActiveTerrain == false)
    {
        Vector(0.f, 0.f, 0.f, Light);
        return;
    }

    xf = xf / TERRAIN_SCALE;
    yf = yf / TERRAIN_SCALE;
    int xi = (int)xf;
    int yi = (int)yf;
    if (xi < 0 || yi < 0 || xi > TERRAIN_SIZE_MASK - 1 || yi > TERRAIN_SIZE_MASK - 1)
    {
        Vector(0.f, 0.f, 0.f, Light);
        return;
    }
    int Index1 = ((xi) + (yi)*TERRAIN_SIZE);
    int Index2 = ((xi + 1) + (yi)*TERRAIN_SIZE);
    int Index3 = ((xi + 1) + (yi + 1) * TERRAIN_SIZE);
    int Index4 = ((xi) + (yi + 1) * TERRAIN_SIZE);
    float xd = xf - (float)xi;
    float yd = yf - (float)yi;
    for (int i = 0; i < 3; i++)
    {
        float left = PrimaryTerrainLight[Index1][i] +
                     (PrimaryTerrainLight[Index4][i] - PrimaryTerrainLight[Index1][i]) * yd;
        float right = PrimaryTerrainLight[Index2][i] +
                      (PrimaryTerrainLight[Index3][i] - PrimaryTerrainLight[Index2][i]) * yd;
        Light[i] = (left + (right - left) * xd);
    }
}

extern void RenderCharactersClient();
