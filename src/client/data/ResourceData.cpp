#include "data/ResourceData.h"
#include "app/ApplicationDiagnostics.h"
#include "support/CoreMath.h"
#include "data/Localization.h"
#include "domain/CharacterSystem.h"
#include "domain/ItemsSkills.h"
#include "app/ApplicationKeeper.h"
#include "render/Textures.h"
#include "domain/WorldSimulation.h"
#include "session/SessionKeeper.h"
#include "render/ModelResources.h"
#include "app/ApplicationNetwork.h"
#include "session/SessionPresentation.h"
#include "session/SessionGameplay.h"
#include "session/SessionRender.h"
#include "ui/runtime/UiControls.h"
#include "domain/CharacterPresentation.h"
#include "render/World.h"
#include "data/WorldData.h"
#include "render/Terrain.h"
#include "data/GameData.h"
#include "domain/MovementAI.h"
#include "data/ItemData.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "app/ApplicationAudio.h"
#include "domain/WorldPhysics.h"
#include "domain/Quests.h"
#include "ui/session/UiSessionLogic.h"
#include "domain/MapSimulation.h"
#include "domain/Events.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/features/Social/SocialRender.h"
#include "ui/features/Hud/HudRender.h"
#include "ui/features/Dialogs/DialogsRender.h"
#include "ui/features/World/WorldRender.h"
#include "support/Camera.h"
#include "session/SessionUi.h"

#ifndef _WIN32

#include <dirent.h>
#include <strings.h> // strcasecmp
#include <unistd.h>  // access

namespace
{
// True if `dir`/`name` exists verbatim. Avoids emitting a leading "//"
// (POSIX gives a double leading slash an implementation-defined meaning)
// when dir is "/" or already ends in a separator.
bool EntryExists(const std::string &dir, const std::string &name)
{
    std::string full;
    if (dir.empty())
        full = name;
    else if (dir.back() == '/')
        full = dir + name;
    else
        full = dir + "/" + name;
    return ::access(full.c_str(), F_OK) == 0;
}

// Scans `dir` for an entry matching `name` case-insensitively; on a match
// replaces `name` with the entry's actual spelling and returns true.
bool FindCaseInsensitive(const std::string &dir, std::string &name)
{
    DIR *d = ::opendir(dir.empty() ? "." : dir.c_str());
    if (!d)
        return false;
    bool found = false;
    while (const dirent *entry = ::readdir(d))
    {
        if (::strcasecmp(entry->d_name, name.c_str()) == 0)
        {
            name = entry->d_name;
            found = true;
            break;
        }
    }
    ::closedir(d);
    return found;
}
} // namespace

std::string MuResolvePath(const char *utf8Path)
{
    if (!utf8Path)
        return {};

    std::string path(utf8Path);
    for (char &c : path)
        if (c == '\\')
            c = '/';

    // The common case: the normalized path exists as spelled.
    if (::access(path.c_str(), F_OK) == 0)
        return path;

    // Walk the components, case-correcting each against the directory that has
    // been resolved so far. `resolved` holds "" for a relative path so the
    // first component is looked up in the working directory.
    std::string resolved;
    size_t pos = 0;
    if (!path.empty() && path[0] == '/')
    {
        resolved = "/";
        pos = 1;
    }

    while (pos < path.size())
    {
        const size_t sep = path.find('/', pos);
        const size_t end = (sep == std::string::npos) ? path.size() : sep;
        std::string component = path.substr(pos, end - pos);

        if (!component.empty() && component != "." && component != ".." &&
            !EntryExists(resolved, component))
        {
            FindCaseInsensitive(resolved, component);
        }

        if (!resolved.empty() && resolved.back() != '/')
            resolved += '/';
        resolved += component;
        pos = end + 1;
    }

    return resolved;
}

#endif // !_WIN32

namespace DataFileIO
{
std::unique_ptr<BYTE[]> ReadBuffer(FILE *fp, const IOConfig &config, CErrorReport &errorReport,
                                   HWND window, DWORD *outChecksum)
{
    int bufferSize = config.itemSize * config.itemCount;
    auto buffer = std::make_unique<BYTE[]>(bufferSize);

    // Read buffer
    size_t bytesRead = fread(buffer.get(), bufferSize, 1, fp);
    if (bytesRead != 1)
    {
        ShowErrorAndExit(errorReport, window, L"Failed to read data from file");
        return nullptr;
    }

    // Read checksum if requested
    if (outChecksum)
    {
        bytesRead = fread(outChecksum, sizeof(DWORD), 1, fp);
        if (bytesRead != 1)
        {
            ShowErrorAndExit(errorReport, window, L"Failed to read checksum from file");
            return nullptr;
        }
    }

    return buffer;
}

bool VerifyChecksum(const BYTE *buffer, const IOConfig &config, DWORD expectedChecksum)
{
    int bufferSize = config.itemSize * config.itemCount;
    DWORD calculatedChecksum = GenerateCheckSum2(buffer, bufferSize, config.checksumKey);
    return (calculatedChecksum == expectedChecksum);
}

void DecryptBuffer(BYTE *buffer, const IOConfig &config)
{
    if (!config.decryptRecord)
    {
        return; // No decryption needed
    }

    BYTE *pSeek = buffer;
    for (int i = 0; i < config.itemCount; i++)
    {
        config.decryptRecord(pSeek, config.itemSize);
        pSeek += config.itemSize;
    }
}

void ShowErrorAndExit(CErrorReport &errorReport, HWND window, const wchar_t *message)
{
    errorReport.Write(message);
    MessageBox(window, message, L"Data File Error", MB_OK | MB_ICONERROR);
    // Note: Application continues running - error handling left to caller
}

} // namespace DataFileIO

std::optional<WorldResources> WorldResources::Load(const MapDefinition &definition,
                                                   MapDefinition::Variant variant,
                                                   const std::filesystem::path &root,
                                                   Failure &failure,
                                                   ApplicationKeeper *application) noexcept
{
    try
    {
        const auto folder = root / (L"World" + std::to_wstring(definition.assetSet));
        WorldResources data;
        data.terrain_ = LoadTerrainAsset(definition, variant, folder, failure, application);
        if (!data.terrain_)
            return std::nullopt;
        constexpr int LoginScene = 73;
        if (definition.id.RawValue() == LoginScene)
        {
            failure.resource =
                folder / (L"CWScript" + std::to_wstring(definition.assetSet) + L".cws");
            data.camera_ = WorldCameraData::Load(failure.resource, failure.reason);
            if (!data.camera_)
                return std::nullopt;
        }
        return data;
    }
    catch (const std::bad_alloc &)
    {
        failure.reason = WorldFileData::Error::Allocation;
        return std::nullopt;
    }
    catch (...)
    {
        failure.reason = WorldFileData::Error::Read;
        return std::nullopt;
    }
}

std::shared_ptr<const WorldTerrainAsset> WorldResources::LoadTerrainAsset(
    const MapDefinition &definition, MapDefinition::Variant variant,
    const std::filesystem::path &folder, Failure &failure, ApplicationKeeper *application)
{
    if (!application)
        return DecodeTerrainAsset(definition, variant, folder, failure);
    assert(application->IsOwnerThread());
    // File identities and decode policy define reuse, independently of raw packet identity.
    const auto key = std::filesystem::absolute(folder).lexically_normal().wstring() + L"|" +
                     std::to_wstring(definition.AttributeFile(variant)) + L"|" +
                     definition.LightFile(variant) + L"|" +
                     std::to_wstring(definition.extendedHeight) + L"|" +
                     std::to_wstring(definition.BehaviorMap() == 55);
    const auto cached = application->worldTerrainAssets_.find(key);
    if (cached != application->worldTerrainAssets_.end())
    {
        cached->second.unusedSinceMilliseconds = 0;
        return cached->second.asset;
    }
    auto terrain = DecodeTerrainAsset(definition, variant, folder, failure);
    if (!terrain)
        return {};
    terrain->revision = application->NextTerrainContentRevision();
    const auto bytes = terrain->StorageBytes();
    application->worldTerrainAssets_.emplace(
        key, ApplicationKeeper::RetainedAsset<const WorldTerrainAsset>{terrain, bytes, {}});
    application->retainedWorldTerrainBytes_ += bytes;
    return terrain;
}

std::shared_ptr<WorldTerrainAsset> WorldResources::DecodeTerrainAsset(
    const MapDefinition &definition, MapDefinition::Variant variant,
    const std::filesystem::path &folder, Failure &failure)
{
    auto terrain = std::make_shared<WorldTerrainAsset>();
    const auto binary = [&](WorldFileData &target, WorldFileData::Kind kind, int fileNumber,
                            const wchar_t *extension) {
        failure.resource = folder / (L"EncTerrain" + std::to_wstring(fileNumber) + extension);
        auto decoded = WorldFileData::Load(failure.resource, kind, definition.assetSet,
                                           definition.BehaviorMap(), failure.reason);
        if (!decoded)
            return false;
        target = std::move(*decoded);
        return true;
    };
    if (!binary(terrain->mapping, WorldFileData::Kind::Mapping, definition.assetSet, L".map") ||
        !binary(terrain->attributes, WorldFileData::Kind::Attributes,
                definition.AttributeFile(variant), L".att") ||
        !binary(terrain->placements, WorldFileData::Kind::Placements, definition.assetSet,
                L".obj") ||
        !LoadImages(*terrain, definition, variant, folder, failure))
        return {};
    terrain->surface.Assign(terrain->mapping);
    return terrain;
}

bool WorldResources::LoadImages(WorldTerrainAsset &terrain, const MapDefinition &definition,
                                MapDefinition::Variant variant, const std::filesystem::path &folder,
                                Failure &failure)
{
    failure.resource = folder / L"TerrainHeight.OZB";
    constexpr int LegacyLoginScene = 55;
    auto height = WorldImageData::Load(
        failure.resource,
        definition.extendedHeight ? WorldImageData::Kind::ExtendedHeight
                                  : WorldImageData::Kind::Height,
        definition.BehaviorMap() == LegacyLoginScene ? 3.0F : 1.5F, failure.reason);
    if (!height)
        return false;
    terrain.height = std::move(*height);
    failure.resource = folder / definition.LightFile(variant);
    failure.resource.replace_extension(L".OZJ");
    auto light =
        WorldImageData::Load(failure.resource, WorldImageData::Kind::Light, 1.5F, failure.reason);
    if (!light)
        return false;
    terrain.light = std::move(*light);
    return true;
}

bool WorldResources::PrepareEffectTextures(SessionKeeper &keeper, const MapDefinition &definition,
                                           const std::filesystem::path &root, Failure &failure)
{
    effectTextures_ = std::make_unique<SessionTextureNamespace>(keeper.BitmapRegistry());
    preparedEffectTextures_.clear();
    const auto common = WorldTextureDependency::Common();
    const auto specific = WorldTextureDependency::For(definition.BehaviorMap());
    preparedEffectTextures_.reserve(common.size() + specific.size());
    for (const auto dependencies : {common, specific})
    {
        for (const auto &texture : dependencies)
        {
            const auto path = root / texture.path;
            if (!effectTextures_->Load(texture.slot, path.wstring(), keeper.ErrorReport(),
                                       texture.filter, texture.wrap))
            {
                failure.resource = path;
                failure.detail = L"Required map effect texture is missing or cannot be decoded";
                return false;
            }
            preparedEffectTextures_.push_back(
                {texture.slot, path.wstring(), texture.filter, texture.wrap});
        }
    }
    return true;
}

// Maps a monster model type to its enum identifier for diagnostic logging.
// Indexed directly by the dense, 0-based EMonsterModelType value; returns
// L"UNKNOWN" for values outside the table so the log always has a name.
// The static_assert keeps the table in sync if the enum grows.
namespace WorldModelLoadingDetail
{
const wchar_t *GetMonsterModelName(EMonsterModelType Type)
{
    static const wchar_t *const s_names[] = {
        L"MONSTER_MODEL_BULL_FIGHTER",
        L"MONSTER_MODEL_HOUND",
        L"MONSTER_MODEL_BUDGE_DRAGON",
        L"MONSTER_MODEL_DARK_KNIGHT",
        L"MONSTER_MODEL_LICH",
        L"MONSTER_MODEL_GIANT",
        L"MONSTER_MODEL_LARVA",
        L"MONSTER_MODEL_GHOST",
        L"MONSTER_MODEL_HELL_SPIDER",
        L"MONSTER_MODEL_SPIDER",
        L"MONSTER_MODEL_CYCLOPS",
        L"MONSTER_MODEL_GORGON",
        L"MONSTER_MODEL_YETI",
        L"MONSTER_MODEL_ELITE_YETI",
        L"MONSTER_MODEL_ASSASSIN",
        L"MONSTER_MODEL_ICE_MONSTER",
        L"MONSTER_MODEL_HOMMERD",
        L"MONSTER_MODEL_WORM",
        L"MONSTER_MODEL_ICE_QUEEN",
        L"MONSTER_MODEL_GOBLIN",
        L"MONSTER_MODEL_CHAIN_SCORPION",
        L"MONSTER_MODEL_BEETLE_MONSTER",
        L"MONSTER_MODEL_HUNTER",
        L"MONSTER_MODEL_FOREST_MONSTER",
        L"MONSTER_MODEL_AGON",
        L"MONSTER_MODEL_STONE_GOLEM",
        L"MONSTER_MODEL_DEVIL",
        L"MONSTER_MODEL_BALROG",
        L"MONSTER_MODEL_SHADOW",
        L"MONSTER_MODEL_DEATH_KNIGHT",
        L"MONSTER_MODEL_DEATH_COW",
        L"MONSTER_MODEL_DRAGON",
        L"MONSTER_MODEL_BALI",
        L"MONSTER_MODEL_BAHAMUT",
        L"MONSTER_MODEL_VEPAR",
        L"MONSTER_MODEL_VALKYRIE",
        L"MONSTER_MODEL_LIZARD",
        L"MONSTER_MODEL_HYDRA",
        L"MONSTER_MODEL_SEA_WORM",
        L"MONSTER_MODEL_TITAN",
        L"MONSTER_MODEL_SOLDIER",
        L"MONSTER_MODEL_GOLDEN_WHEEL",
        L"MONSTER_MODEL_TANTALLOS",
        L"MONSTER_MODEL_BLOODY_WOLF",
        L"MONSTER_MODEL_BEAM_KNIGHT",
        L"MONSTER_MODEL_MUTANT",
        L"MONSTER_MODEL_ORC_ARCHER",
        L"MONSTER_MODEL_ORC",
        L"MONSTER_MODEL_CURSED_KING",
        L"MONSTER_MODEL_MOLT",
        L"MONSTER_MODEL_ALQUAMOS",
        L"MONSTER_MODEL_QUEEN_RAINER",
        L"MONSTER_MODEL_CRUST",
        L"MONSTER_MODEL_PHANTOM_KNIGHT",
        L"MONSTER_MODEL_DRAKAN",
        L"MONSTER_MODEL_DARK_PHOENIX_SHIELD",
        L"MONSTER_MODEL_DARK_PHOENIX",
        L"MONSTER_MODEL_RED_SKELETON_KNIGHT",
        L"MONSTER_MODEL_GIANT_OGRE",
        L"MONSTER_MODEL_DARK_SKULL_SOLDIER",
        L"MONSTER_MODEL_STATUE_OF_SAINT",
        L"MONSTER_MODEL_CASTLE_GATE",
        L"MONSTER_MODEL_MAGIC_SKELETON",
        L"MONSTER_MODEL_DEATH_ANGEL",
        L"MONSTER_MODEL_ILLUSION_OF_KUNDUN",
        L"MONSTER_MODEL_BLOOD_SOLDIER",
        L"MONSTER_MODEL_AEGIS",
        L"MONSTER_MODEL_DEATH_CENTURION",
        L"MONSTER_MODEL_NECRON",
        L"MONSTER_MODEL_SHRIKER",
        L"MONSTER_MODEL_CHAOSCASTLE_KNIGHT",
        L"MONSTER_MODEL_CHAOSCASTLE_ELF",
        L"MONSTER_MODEL_CHAOSCASTLE_WIZARD",
        L"MONSTER_MODEL_CASTLE_GATE1",
        L"MONSTER_MODEL_GUARDIAN_STATUE",
        L"MONSTER_MODEL_GREAT_DRAKAN",
        L"MONSTER_MODEL_BATTLE_GUARD1",
        L"MONSTER_MODEL_BATTLE_GUARD2",
        L"MONSTER_MODEL_GOLDEN_GOBLIN",
        L"MONSTER_MODEL_CANON_TOWER",
        L"MONSTER_MODEL_GOLDEN_LIZARD_KING",
        L"MONSTER_MODEL_LIZARD_WARRIOR",
        L"MONSTER_MODEL_FIRE_GOLEM",
        L"MONSTER_MODEL_QUEEN_BEE",
        L"MONSTER_MODEL_POISON_GOLEM",
        L"MONSTER_MODEL_AXE_HERO",
        L"MONSTER_MODEL_LIFE_STONE",
        L"MONSTER_MODEL_EROHIM",
        L"MONSTER_MODEL_RED_SKELETON_KNIGHT_1",
        L"MONSTER_MODEL_BALGASS",
        L"MONSTER_MODEL_CHIEF_SKELETON_WARRIOR_2",
        L"MONSTER_MODEL_BALRAM",
        L"MONSTER_MODEL_DARK_ELF_1",
        L"MONSTER_MODEL_DEATH_SPIRIT",
        L"MONSTER_MODEL_SORAM",
        L"MONSTER_MODEL_WEREWOLF_HERO",
        L"MONSTER_MODEL_VALAM",
        L"MONSTER_MODEL_SOLAM",
        L"MONSTER_MODEL_SCOUT",
        L"MONSTER_MODEL_BALLISTA",
        L"MONSTER_MODEL_WITCH_QUEEN",
        L"MONSTER_MODEL_GOLDEN_STONE_GOLEM",
        L"MONSTER_MODEL_DEATH_RIDER",
        L"MONSTER_MODEL_FOREST_ORC",
        L"MONSTER_MODEL_DEATH_TREE",
        L"MONSTER_MODEL_HELL_MAINE",
        L"MONSTER_MODEL_BERSERK",
        L"MONSTER_MODEL_SPLINTER_WOLF",
        L"MONSTER_MODEL_IRON_RIDER",
        L"MONSTER_MODEL_SATYROS",
        L"MONSTER_MODEL_BLADE_HUNTER",
        L"MONSTER_MODEL_KENTAUROS",
        L"MONSTER_MODEL_GIGANTIS",
        L"MONSTER_MODEL_GENOCIDER",
        L"MONSTER_MODEL_PERSONA",
        L"MONSTER_MODEL_TWIN_TAIL",
        L"MONSTER_MODEL_DREADFEAR",
        L"MONSTER_MODEL_RED_SKELETON_KNIGHT_4",
        L"MONSTER_MODEL_MAYA_HAND_LEFT",
        L"MONSTER_MODEL_MAYA_HAND_RIGHT",
        L"MONSTER_MODEL_MAYA",
        L"MONSTER_MODEL_DARK_SKULL_SOLDIER_5",
        L"MONSTER_MODEL_POUCH_OF_BLESSING",
        L"MONSTER_MODEL_ILLUSION_SORCERER_SPIRIT_LIGHTNING",
        L"MONSTER_MODEL_ILLUSION_SORCERER_SPIRIT_ICE",
        L"MONSTER_MODEL_ILLUSION_SORCERER_SPIRIT_POISON",
        L"MONSTER_MODEL_DARK_ELF",
        L"MONSTER_MODEL_LUNAR_RABBIT",
        L"MONSTER_MODEL_RABBIT",
        L"MONSTER_MODEL_BUTTERFLY",
        L"MONSTER_MODEL_HIDEOUS_RABBIT",
        L"MONSTER_MODEL_WEREWOLF2",
        L"MONSTER_MODEL_CURSED_LICH",
        L"MONSTER_MODEL_TOTEM_GOLEM",
        L"MONSTER_MODEL_GRIZZLY",
        L"MONSTER_MODEL_CAPTAIN_GRIZZLY",
        L"MONSTER_MODEL_SAPIUNUS",
        L"MONSTER_MODEL_SAPIDUO",
        L"MONSTER_MODEL_SAPITRES",
        L"MONSTER_MODEL_SHADOW_PAWN",
        L"MONSTER_MODEL_SHADOW_KNIGHT",
        L"MONSTER_MODEL_SHADOW_LOOK",
        L"MONSTER_MODEL_NAPIN",
        L"MONSTER_MODEL_GHOST_NAPIN",
        L"MONSTER_MODEL_BLAZE_NAPIN",
        L"MONSTER_MODEL_ICE_WALKER",
        L"MONSTER_MODEL_GIANT_MAMMOTH",
        L"MONSTER_MODEL_ICE_GIANT",
        L"MONSTER_MODEL_COOLUTIN",
        L"MONSTER_MODEL_IRON_KNIGHT",
        L"MONSTER_MODEL_SELUPAN",
        L"MONSTER_MODEL_SPIDER_EGGS_1",
        L"MONSTER_MODEL_SPIDER_EGGS_2",
        L"MONSTER_MODEL_SPIDER_EGGS_3",
        L"MONSTER_MODEL_FIRE_FLAME_GHOST",
        L"MONSTER_MODEL_CURSED_SANTA",
        L"MONSTER_MODEL_EVIL_GOBLIN",
        L"MONSTER_MODEL_ZOMBIE_FIGHTER",
        L"MONSTER_MODEL_GLADIATOR",
        L"MONSTER_MODEL_SLAUGTHERER",
        L"MONSTER_MODEL_BLOOD_ASSASSIN",
        L"MONSTER_MODEL_CRUEL_BLOOD_ASSASSIN",
        L"MONSTER_MODEL_LAVA_GIANT",
        L"MONSTER_MODEL_BURNING_LAVA_GIANT",
        L"MONSTER_MODEL_GAYION",
        L"MONSTER_MODEL_JERRY",
        L"MONSTER_MODEL_RAYMOND",
        L"MONSTER_MODEL_LUCAS",
        L"MONSTER_MODEL_FRED",
        L"MONSTER_MODEL_HAMMERIZE",
        L"MONSTER_MODEL_DUAL_BERSERKER",
        L"MONSTER_MODEL_DEVIL_LORD",
        L"MONSTER_MODEL_QUARTER_MASTER",
        L"MONSTER_MODEL_COMBAT_INSTRUCTOR",
        L"MONSTER_MODEL_ATICLES_HEAD",
        L"MONSTER_MODEL_DARK_GHOST",
        L"MONSTER_MODEL_BANSHEE",
        L"MONSTER_MODEL_HEAD_MOUNTER",
        L"MONSTER_MODEL_DEFENDER",
        L"MONSTER_MODEL_FORSAKER",
        L"MONSTER_MODEL_OCELOT",
        L"MONSTER_MODEL_ERIC",
        L"MONSTER_MODEL_DEATH_ANGEL_3",
        L"MONSTER_MODEL_EVIL_GATE",
        L"MONSTER_MODEL_LION_GATE",
        L"MONSTER_MODEL_STATUE",
        L"MONSTER_MODEL_STAR_GATE",
        L"MONSTER_MODEL_RUSH_GATE",
        L"MONSTER_MODEL_SCHRIKER_3",
        L"MONSTER_MODEL_MAD_BUTCHER",
        L"MONSTER_MODEL_TERRIBLE_BUTCHER",
        L"MONSTER_MODEL_DOPPELGANGER",
        L"MONSTER_MODEL_MEDUSA",
        L"MONSTER_MODEL_BLOODY_ORC",
        L"MONSTER_MODEL_BLOODY_DEATH_RIDER",
        L"MONSTER_MODEL_BLOODY_GOLEM",
        L"MONSTER_MODEL_BLOODY_WITCH_QUEEN",
        L"MONSTER_MODEL_BERSERKER_WARRIOR",
        L"MONSTER_MODEL_KENTAUROS_WARRIOR",
        L"MONSTER_MODEL_GIGANTIS_WARRIOR",
        L"MONSTER_MODEL_SOCCERBALL",
        L"MONSTER_MODEL_SAPI_QUEEN",
        L"MONSTER_MODEL_ICE_NAPIN",
        L"MONSTER_MODEL_SHADOW_MASTER",
        L"MONSTER_MODEL_WOLF_STATUS",
        L"MONSTER_MODEL_DARK_MAMMOTH",
        L"MONSTER_MODEL_DARK_GIANT",
        L"MONSTER_MODEL_DARK_COOLUTIN",
        L"MONSTER_MODEL_DARK_IRON_KNIGHT",
        L"MONSTER_MODEL_VENOMOUS_CHAIN_SCORPION",
        L"MONSTER_MODEL_BONE_SCORPION",
        L"MONSTER_MODEL_ORCUS",
        L"MONSTER_MODEL_GOLLOCK",
        L"MONSTER_MODEL_CRYPTA",
        L"MONSTER_MODEL_CRYPOS",
        L"MONSTER_MODEL_CONDRA",
        L"MONSTER_MODEL_NACONDRA",
    };
    static_assert(sizeof(s_names) / sizeof(s_names[0]) == MONSTER_MODEL_COUNT,
                  "GetMonsterModelName table is out of sync with EMonsterModelType");

    if (Type < 0 || Type >= MONSTER_MODEL_COUNT)
        return L"UNKNOWN";
    return s_names[Type];
}
} // namespace WorldModelLoadingDetail

bool World::PrepareResources()
{
    const auto &definition = *preparedBinding_.definition;
    if (definition.scene == MapDefinition::Scene::Login)
    {
        if (!sessionKeeper_.ServerListManagerObject().LoadServerListScript())
        {
            ReportTransitionFailure(definition.id.RawValue(), "Required login metadata failed");
            return false;
        }
    }
    WorldResources::Failure failure;
    preparedResources_ = WorldResources::Load(
        definition, sessionKeeper_.MapManagerObject().EntryTerrainVariant(definition.id.RawValue()),
        L"Data", failure, &applicationKeeper_);
    if (preparedResources_ &&
        preparedResources_->PrepareModels(sessionKeeper_, definition, L"Data", failure) &&
        preparedResources_->PreparePrimaryModels(sessionKeeper_, definition, L"Data", failure) &&
        sessionKeeper_.Visual()->PrepareWorldVisuals(*preparedResources_, definition, failure))
        return true;
    FinishLoad(false);
    sessionKeeper_.ErrorReport().Write(
        L"%ls: %ls\r\n", failure.resource.c_str(),
        failure.detail != nullptr ? failure.detail : WorldFileData::ErrorText(failure.reason));
    return false;
}
