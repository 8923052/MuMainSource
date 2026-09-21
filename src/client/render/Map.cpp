#include "render/Map.h"
#include "I18N/All.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationConfigScheduling.h"
#include "app/ApplicationDiagnostics.h"
#include "app/ApplicationKeeper.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "data/CharacterData.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "data/WorldData.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/ChatSocial.h"
#include "domain/EffectsUpdate.h"
#include "domain/Events.h"
#include "domain/Guild.h"
#include "domain/ItemsSkills.h"
#include "domain/MapPresentation.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Quests.h"
#include "domain/Shop.h"
#include "domain/WorldPhysics.h"
#include "domain/WorldSimulation.h"
#include "render/ModelGeometry.h"
#include "render/ModelResources.h"
#include "render/Sprites.h"
#include "render/Terrain.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/SessionAudio.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionNetwork.h"
#include "session/SessionPresentation.h"
#include "session/SessionRender.h"
#include "session/SessionUi.h"
#include "session/SessionWorkspace.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/Shell/ShellLogic.h"
#include "ui/features/World/WorldLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"

bool MapProcess::RenderObjectMesh(const ObjectDrawInput &input, BMD *model, bool extraMonster)
{
    const auto &draw = input;
    const auto *object = input.source;
    BaseMap *const map = ContextBehavior();
    if (map && map->RenderObjectMeshBeforeShared(draw, model, extraMonster))
        return true;
    switch (draw.type)
    {
    case MODEL_DEATH_ANGEL:
    case MODEL_ILLUSION_OF_KUNDUN:
    case MODEL_CUNDUN_PART1:
    case MODEL_CUNDUN_PART2:
    case MODEL_CUNDUN_PART3:
    case MODEL_CUNDUN_PART4:
    case MODEL_CUNDUN_PART5:
    case MODEL_CUNDUN_PART8:
    case MODEL_BLOOD_SOLDIER:
    case MODEL_AEGIS:
    case MODEL_DEATH_CENTURION:
    case MODEL_NECRON:
    case MODEL_SHRIKER:
        if (hellas_->RenderHellasMonsterObjectMesh(draw, model))
            return true;
        break;
    case MODEL_WARCRAFT:
    case MODEL_CUNDUN_DRAGON_HEAD:
    case MODEL_CUNDUN_GHOST:
        if (hellas_->RenderHellasObjectMesh(draw, model))
            return true;
        break;
    case MODEL_LIZARD_WARRIOR:
    case MODEL_FIRE_GOLEM:
    case MODEL_QUEEN_BEE:
    case MODEL_POISON_GOLEM:
    case MODEL_AXE_HERO:
    case MODEL_EROHIM:
    case MODEL_FISSURE:
    case MODEL_FISSURE_LIGHT:
        if (huntingGround_->RenderHuntingGroundMonsterObjectMesh(draw, model, extraMonster))
            return true;
        break;
    case MODEL_GOLDEN_STONE_GOLEM:
    case MODEL_DEATH_RIDER:
    case MODEL_HELL_MAINE:
    case MODEL_BLOODY_DEATH_RIDER:
    case MODEL_BLOODY_GOLEM:
        if (aida_->RenderAidaMonsterObjectMesh(draw, model, extraMonster))
            return true;
        break;
    case MODEL_BERSERK:
    case MODEL_GIGANTIS:
    case MODEL_GENOCIDER:
    case MODEL_SPLINTER_WOLF:
    case MODEL_IRON_RIDER:
    case MODEL_BLADE_HUNTER:
    case MODEL_SATYROS:
    case MODEL_KENTAUROS:
    case MODEL_BERSERKER_WARRIOR:
    case MODEL_KENTAUROS_WARRIOR:
    case MODEL_GIGANTIS_WARRIOR:
    case MODEL_SOCCERBALL:
        if (kanturu1st_->RenderKanturu1stMonsterObjectMesh(draw, model, false))
            return true;
        break;
    case MODEL_PERSONA:
    case MODEL_TWIN_TAIL:
    case MODEL_DREADFEAR:
        if (kanturu2nd_->Render_Kanturu2nd_MonsterObjectMesh(draw, model, false))
            return true;
        if (kanturu3rd_->RenderKanturu3rdMonsterObjectMesh(draw, model, extraMonster))
            return true;
        break;
    case MODEL_KANTURU2ND_ENTER_NPC:
    case MODEL_TRAP_CANON:
        if (kanturu2nd_->Render_Kanturu2nd_MonsterObjectMesh(draw, model, false))
            return true;
        break;
    case MODEL_DARK_SKULL_SOLDIER_5:
    case MODEL_MAYA_HAND_LEFT:
    case MODEL_MAYA_HAND_RIGHT:
    case MODEL_SUMMON:
    case MODEL_STORM2:
        if (kanturu3rd_->RenderKanturu3rdMonsterObjectMesh(draw, model, extraMonster))
            return true;
        break;
    case MODEL_BALRAM:
    case MODEL_DEATH_SPIRIT:
    case MODEL_SORAM:
    case MODEL_DARK_ELF:
        if (thirdChange_->RenderMonsterObjectMesh(draw, model, extraMonster))
            return true;
        break;
    case MODEL_SHADOW_PAWN:
    case MODEL_SHADOW_KNIGHT:
    case MODEL_SHADOW_LOOK:
        if (swampOfQuiet_->RenderSharedMonsterMesh(draw, model, extraMonster))
            return true;
        break;
    }
    return map && map->RenderObjectMesh(draw, model, extraMonster);
}

struct TerrainSharedGeometry final
{
    std::shared_ptr<const std::vector<RenderTapeVertex>> vertices;
    std::shared_ptr<const std::vector<std::uint32_t>> indices;
    std::shared_ptr<const std::vector<RenderTapeTerrainCell>> terrainCells;
    std::shared_ptr<const std::vector<RenderTapeTerrainInstance>> terrainInstances;
    std::shared_ptr<const TerrainLightSnapshot> staticLight;
    std::shared_ptr<const std::vector<TerrainGeometryTile>> tiles;
    std::shared_ptr<const std::vector<TerrainGeometryBlock>> blocks;
    std::array<float, TERRAIN_SIZE> grassTexture{};
};

namespace
{
template <typename Value>
std::size_t SharedVectorBytes(const std::shared_ptr<const std::vector<Value>> &values) noexcept
{
    return values == nullptr ? 0 : sizeof(*values) + values->capacity() * sizeof(Value);
}

std::size_t TerrainSharedGeometryBytes(const TerrainSharedGeometry &asset) noexcept
{
    std::size_t bytes = sizeof(asset) + SharedVectorBytes(asset.vertices) +
                         SharedVectorBytes(asset.indices) + SharedVectorBytes(asset.terrainCells) +
                         SharedVectorBytes(asset.terrainInstances) +
                         SharedVectorBytes(asset.tiles) + SharedVectorBytes(asset.blocks);
    if (asset.staticLight != nullptr)
        bytes += sizeof(*asset.staticLight) +
                 asset.staticLight->values.capacity() * sizeof(asset.staticLight->values[0]);
    if (asset.blocks != nullptr)
    {
        for (const TerrainGeometryBlock &block : *asset.blocks)
        {
            bytes += block.draws.capacity() * sizeof(block.draws[0]) +
                     block.afterDraws.capacity() * sizeof(block.afterDraws[0]) +
                      block.grassDraws.capacity() * sizeof(block.grassDraws[0]);
        }
    }
    return bytes;
}

constexpr int TerrainBlockSize = 4;
constexpr int TerrainBlockCount = TERRAIN_SIZE / TerrainBlockSize;
constexpr std::uint64_t HashOffset = 14695981039346656037ULL;
constexpr std::uint64_t HashPrime = 1099511628211ULL;

void HashBytes(std::uint64_t &hash, const void *data, std::size_t size) noexcept
{
    const auto *bytes = static_cast<const unsigned char *>(data);
    for (std::size_t index = 0; index < size; ++index)
        hash = (hash ^ bytes[index]) * HashPrime;
}

template <class T> void HashValue(std::uint64_t &hash, const T &value) noexcept
{
    HashBytes(hash, &value, sizeof(value));
}

struct TextureSize final
{
    float width = 0.0F;
    float height = 0.0F;
};

struct BuildContext final
{
    int world = -1;
    std::uint64_t contentRevision = 0;
    float specialHeight = 0.0F;
    const float *heights = nullptr;
    const unsigned char *layer1 = nullptr;
    const unsigned char *layer2 = nullptr;
    const float *layerAlpha = nullptr;
    const float (*lights)[3] = nullptr;
    const WORD *walls = nullptr;
    const float *grassTexture = nullptr;
    bool pkField = false;
    bool doppelGanger2 = false;
    bool doppelGanger3 = false;
    const SessionBitmapView *bitmaps = nullptr;
};

std::uint64_t OwnerGeometryKey(const BuildContext &context) noexcept
{
    std::uint64_t hash = HashOffset;
    HashValue(hash, context.contentRevision);
    // Unbound legacy fixtures retain their world identity. Loaded content has
    // an application revision; only geometry-changing presentation rules vary.
    if (context.contentRevision == 0)
        HashValue(hash, context.world);
    HashValue(hash, context.world == WD_7ATLANSE);
    HashValue(hash, context.world == WD_39KANTURU_3RD);
    HashValue(hash, context.specialHeight);
    HashValue(hash, context.pkField);
    HashValue(hash, context.doppelGanger2);
    HashValue(hash, context.doppelGanger3);
    const bool hasGrass = context.grassTexture != nullptr;
    HashValue(hash, hasGrass);
    return hash;
}

bool UsesMovingWater(int texture, bool baseWater, bool overlay) noexcept
{
    return baseWater || (overlay && texture == 5 && baseWater);
}

std::optional<TextureSize> FindTextureSize(int texture, const SessionBitmapView &bitmaps,
                                           std::unordered_map<int, TextureSize> &cache) noexcept
{
    const auto found = cache.find(texture);
    if (found != cache.end())
    {
        return found->second;
    }
    const auto properties = bitmaps.GetTextureProperties(BITMAP_MAPTILE + texture);
    if (!properties.has_value() || properties->width <= 0.0F || properties->height <= 0.0F)
    {
        return std::nullopt;
    }
    try
    {
        return cache.emplace(texture, TextureSize{properties->width, properties->height})
            .first->second;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

void AppendLayer(int x, int y, int texture, TerrainGeometryMaterialPass pass,
                  const TextureSize &textureSize, TerrainGeometryTile &tile,
                  std::vector<RenderTapeTerrainInstance> &instances, bool water = false)
{
    const float width = 64.0F / textureSize.width;
    const float height = 64.0F / textureSize.height;
    const std::uint32_t instanceOffset = static_cast<std::uint32_t>(instances.size());
    instances.push_back(
        RenderTapeTerrainInstance{static_cast<std::uint32_t>(TERRAIN_INDEX(x, y))});
    tile.draws[tile.drawCount++] = TerrainGeometryDraw{
        texture, pass, instanceOffset, 1, instanceOffset, {width, height}, water};
}

void GroupBlockMaterialRuns(std::span<TerrainGeometryTile> tiles,
                            std::span<const RenderTapeTerrainInstance> sourceInstances,
                            std::vector<TerrainGeometryBlock> &blocks,
                            std::vector<RenderTapeTerrainInstance> &groupedInstances)
{
    blocks.assign(TerrainBlockCount * TerrainBlockCount, TerrainGeometryBlock{});
    groupedInstances.reserve(sourceInstances.size());
    constexpr std::array passes{TerrainGeometryMaterialPass::Base,
                                TerrainGeometryMaterialPass::Alpha,
                                TerrainGeometryMaterialPass::OceanBlend,
                                TerrainGeometryMaterialPass::AfterAlphaTest,
                                TerrainGeometryMaterialPass::AfterAlphaBlend};
    for (int blockY = 0; blockY < TerrainBlockCount; ++blockY)
        for (int blockX = 0; blockX < TerrainBlockCount; ++blockX)
        {
            TerrainGeometryBlock &block = blocks[blockY * TerrainBlockCount + blockX];
            for (TerrainGeometryMaterialPass pass : passes)
            {
                std::array<int, TerrainBlockSize * TerrainBlockSize> textures{};
                std::size_t textureCount = 0;
                for (int y = 0; y < TerrainBlockSize; ++y)
                    for (int x = 0; x < TerrainBlockSize; ++x)
                    {
                        const TerrainGeometryTile &tile = tiles[TERRAIN_INDEX(
                            blockX * TerrainBlockSize + x, blockY * TerrainBlockSize + y)];
                        for (std::size_t i = 0; i < tile.drawCount; ++i)
                            if (tile.draws[i].pass == pass &&
                                std::find(textures.begin(), textures.begin() + textureCount,
                                          tile.draws[i].texture) == textures.begin() + textureCount)
                                textures[textureCount++] = tile.draws[i].texture;
                    }
                for (std::size_t textureIndex = 0; textureIndex < textureCount; ++textureIndex)
                {
                    const int texture = textures[textureIndex];
                    const std::uint32_t offset =
                        static_cast<std::uint32_t>(groupedInstances.size());
                    const TerrainGeometryDraw *prototype = nullptr;
                    std::uint32_t finalInstance = 0;
                    for (int y = 0; y < TerrainBlockSize; ++y)
                        for (int x = 0; x < TerrainBlockSize; ++x)
                        {
                            TerrainGeometryTile &tile = tiles[TERRAIN_INDEX(
                                blockX * TerrainBlockSize + x, blockY * TerrainBlockSize + y)];
                            for (std::size_t i = 0; i < tile.drawCount; ++i)
                            {
                                TerrainGeometryDraw &draw = tile.draws[i];
                                if (draw.pass != pass || draw.texture != texture)
                                    continue;
                                groupedInstances.push_back(sourceInstances[draw.instanceOffset]);
                                draw.instanceOffset =
                                    static_cast<std::uint32_t>(groupedInstances.size() - 1);
                                draw.finalInstance = draw.instanceOffset;
                                finalInstance = draw.instanceOffset;
                                prototype = &draw;
                            }
                        }
                    if (prototype == nullptr)
                        continue;
                    TerrainGeometryDraw run = *prototype;
                    run.instanceOffset = offset;
                    run.instanceCount =
                        static_cast<std::uint32_t>(groupedInstances.size() - offset);
                    run.finalInstance = finalInstance;
                    auto &runs = pass == TerrainGeometryMaterialPass::AfterAlphaTest ||
                                         pass == TerrainGeometryMaterialPass::AfterAlphaBlend
                                     ? block.afterDraws
                                     : block.draws;
                    runs.push_back(run);
                    block.finalDraw = run;
                }
            }
        }
}

void AppendGrassGeometry(const BuildContext &context, std::span<TerrainGeometryTile> tiles,
                         std::span<TerrainGeometryBlock> blocks,
                         std::vector<RenderTapeTerrainInstance> &groupedInstances)
{
    if (context.grassTexture == nullptr)
        return;
    std::vector<RenderTapeTerrainInstance> sourceInstances;
    sourceInstances.reserve(TERRAIN_SIZE * TERRAIN_SIZE);
    std::unordered_map<int, TextureSize> textureSizes;
    textureSizes.reserve(4);
    for (int y = 0; y < TERRAIN_SIZE_MASK; ++y)
        for (int x = 0; x < TERRAIN_SIZE_MASK; ++x)
        {
            const int terrainIndices[4]{TERRAIN_INDEX(x, y), TERRAIN_INDEX(x + 1, y),
                                        TERRAIN_INDEX(x + 1, y + 1), TERRAIN_INDEX(x, y + 1)};
            if ((context.walls[terrainIndices[0]] & TW_NOGROUND) == TW_NOGROUND ||
                context.layerAlpha[terrainIndices[0]] > 0.0F ||
                context.layerAlpha[terrainIndices[1]] > 0.0F ||
                context.layerAlpha[terrainIndices[2]] > 0.0F ||
                context.layerAlpha[terrainIndices[3]] > 0.0F)
            {
                continue;
            }
            const int texture = BITMAP_MAPGRASS + context.layer1[terrainIndices[0]];
            auto found = textureSizes.find(texture);
            if (found == textureSizes.end())
            {
                const auto properties = context.bitmaps->GetTextureProperties(texture);
                if (!properties.has_value())
                {
                    continue;
                }
                found = textureSizes
                            .emplace(texture, TextureSize{properties->width, properties->height})
                            .first;
            }
            const float height = found->second.height * 2.0F;
            const float u = static_cast<float>(x) * (64.0F / 256.0F) +
                            context.grassTexture[y & TERRAIN_SIZE_MASK];
            const std::uint32_t instanceOffset =
                static_cast<std::uint32_t>(sourceInstances.size());
            sourceInstances.push_back({static_cast<std::uint32_t>(TERRAIN_INDEX(x, y)), u, height});
            TerrainGeometryTile &tile = tiles[TERRAIN_INDEX(x, y)];
            tile.grassDraw = {texture, TerrainGeometryMaterialPass::Base, instanceOffset, 1,
                              instanceOffset, {64.0F / 256.0F, 1.0F}};
            tile.hasGrass = true;
        }
    for (int blockY = 0; blockY < TerrainBlockCount; ++blockY)
        for (int blockX = 0; blockX < TerrainBlockCount; ++blockX)
        {
            TerrainGeometryBlock &block = blocks[blockY * TerrainBlockCount + blockX];
            std::array<int, TerrainBlockSize * TerrainBlockSize> textures{};
            std::size_t textureCount = 0;
            for (int y = 0; y < TerrainBlockSize; ++y)
            {
                for (int x = 0; x < TerrainBlockSize; ++x)
                {
                    const TerrainGeometryTile &tile = tiles[TERRAIN_INDEX(
                        blockX * TerrainBlockSize + x, blockY * TerrainBlockSize + y)];
                    if (!tile.hasGrass)
                    {
                        continue;
                    }
                    block.finalGrassDraw = tile.grassDraw;
                    const int texture = tile.grassDraw.texture;
                    if (std::find(textures.begin(), textures.begin() + textureCount, texture) ==
                        textures.begin() + textureCount)
                    {
                        textures[textureCount++] = texture;
                    }
                }
            }
            for (std::size_t textureIndex = 0; textureIndex < textureCount; ++textureIndex)
            {
                const int texture = textures[textureIndex];
                const std::uint32_t offset = static_cast<std::uint32_t>(groupedInstances.size());
                std::uint32_t finalInstance = 0;
                for (int y = 0; y < TerrainBlockSize; ++y)
                    for (int x = 0; x < TerrainBlockSize; ++x)
                    {
                        TerrainGeometryTile &tile = tiles[TERRAIN_INDEX(
                            blockX * TerrainBlockSize + x, blockY * TerrainBlockSize + y)];
                        if (!tile.hasGrass || tile.grassDraw.texture != texture)
                        {
                            continue;
                        }
                        groupedInstances.push_back(sourceInstances[tile.grassDraw.instanceOffset]);
                        tile.grassDraw.instanceOffset =
                            static_cast<std::uint32_t>(groupedInstances.size() - 1);
                        tile.grassDraw.finalInstance = tile.grassDraw.instanceOffset;
                        finalInstance = tile.grassDraw.instanceOffset;
                    }
                block.grassDraws.push_back(TerrainGeometryDraw{
                    texture, TerrainGeometryMaterialPass::Base, offset,
                    static_cast<std::uint32_t>(groupedInstances.size() - offset), finalInstance,
                    {64.0F / 256.0F, 1.0F}});
                block.finalGrassDraw = block.grassDraws.back();
            }
        }
}

bool BuildGeometry(const BuildContext &context, std::vector<TerrainGeometryTile> &tiles,
                   std::vector<TerrainGeometryBlock> &blocks,
                   std::shared_ptr<const std::vector<RenderTapeVertex>> &outVertices,
                   std::shared_ptr<const std::vector<std::uint32_t>> &outIndices,
                   std::shared_ptr<const std::vector<RenderTapeTerrainCell>> &outTerrainCells,
                   std::shared_ptr<const std::vector<RenderTapeTerrainInstance>> &outInstances,
                   std::shared_ptr<const TerrainLightSnapshot> &outStaticLight)
{
    auto vertices = std::make_shared<std::vector<RenderTapeVertex>>(4);
    for (std::uint32_t corner = 0; corner < vertices->size(); ++corner)
        (*vertices)[corner].bmdSource[3] = corner;
    auto indices = std::make_shared<std::vector<std::uint32_t>>(
        std::initializer_list<std::uint32_t>{0, 1, 2, 0, 2, 3});
    auto terrainCells =
        std::make_shared<std::vector<RenderTapeTerrainCell>>(TERRAIN_SIZE * TERRAIN_SIZE);
    auto staticLight = std::make_shared<TerrainLightSnapshot>();
    staticLight->revision = (std::numeric_limits<std::uint64_t>::max)();
    staticLight->values.resize(TERRAIN_SIZE * TERRAIN_SIZE * 3);
    for (int index = 0; index < TERRAIN_SIZE * TERRAIN_SIZE; ++index)
    {
        (*terrainCells)[index] = {context.heights[index],
                                  static_cast<std::uint32_t>(context.walls[index]),
                                  context.layerAlpha[index]};
        std::copy_n(context.lights[index], 3, staticLight->values.data() + index * 3);
    }
    tiles.assign(TERRAIN_SIZE * TERRAIN_SIZE, TerrainGeometryTile{});
    std::vector<RenderTapeTerrainInstance> sourceInstances;
    sourceInstances.reserve(TERRAIN_SIZE * TERRAIN_SIZE * 2);
    std::unordered_map<int, TextureSize> textureSizes;
    textureSizes.reserve(32);

    for (int y = 0; y < TERRAIN_SIZE_MASK; ++y)
    {
        for (int x = 0; x < TERRAIN_SIZE_MASK; ++x)
        {
            TerrainGeometryTile &tile = tiles[TERRAIN_INDEX(x, y)];
            const int indices4[4]{TERRAIN_INDEX(x, y), TERRAIN_INDEX(x + 1, y),
                                  TERRAIN_INDEX(x + 1, y + 1), TERRAIN_INDEX(x, y + 1)};
            if ((context.walls[indices4[0]] & TW_NOGROUND) == TW_NOGROUND)
            {
                continue;
            }
            const bool fullLayer2 = context.layerAlpha[indices4[0]] >= 1.0F &&
                                    context.layerAlpha[indices4[1]] >= 1.0F &&
                                    context.layerAlpha[indices4[2]] >= 1.0F &&
                                    context.layerAlpha[indices4[3]] >= 1.0F;
            const int baseTexture =
                fullLayer2 ? context.layer2[indices4[0]] : context.layer1[indices4[0]];
            const bool baseWater =
                !fullLayer2 && (baseTexture == 5 ||
                                (baseTexture == 11 && (context.pkField || context.doppelGanger2)));
            const bool anyLayerAlpha =
                context.layerAlpha[indices4[0]] > 0.0F || context.layerAlpha[indices4[1]] > 0.0F ||
                context.layerAlpha[indices4[2]] > 0.0F || context.layerAlpha[indices4[3]] > 0.0F;
            const bool hasOverlay = !fullLayer2 && anyLayerAlpha;
            const int overlayTexture = context.layer2[indices4[0]];
            const bool specialWater = anyLayerAlpha &&
                                      (context.world == WD_7ATLANSE || context.doppelGanger3) &&
                                      overlayTexture == 5;
            const auto baseSize = FindTextureSize(baseTexture, *context.bitmaps, textureSizes);
            if (!baseSize.has_value())
                continue;
            const bool movingBase = UsesMovingWater(baseTexture, baseWater, false);
            if (context.world == WD_39KANTURU_3RD && baseTexture == 100)
                AppendLayer(x, y, baseTexture, TerrainGeometryMaterialPass::AfterAlphaTest,
                            *baseSize, tile, sourceInstances, movingBase);
            else
            {
                AppendLayer(x, y, baseTexture, TerrainGeometryMaterialPass::Base, *baseSize,
                            tile, sourceInstances, movingBase);
                if (context.world == WD_39KANTURU_3RD && baseTexture == 101)
                    AppendLayer(x, y, baseTexture, TerrainGeometryMaterialPass::AfterAlphaBlend,
                                *baseSize, tile, sourceInstances, movingBase);
            }
            if (!hasOverlay || overlayTexture == 255)
                continue;
            const auto overlaySize =
                FindTextureSize(overlayTexture, *context.bitmaps, textureSizes);
            if (!overlaySize.has_value())
                continue;
            const TerrainGeometryMaterialPass overlayPass =
                specialWater ? TerrainGeometryMaterialPass::OceanBlend
                             : TerrainGeometryMaterialPass::Alpha;
            AppendLayer(x, y, overlayTexture, overlayPass, *overlaySize, tile,
                        sourceInstances, UsesMovingWater(overlayTexture, baseWater, true));
        }
    }
    auto groupedInstances = std::make_shared<std::vector<RenderTapeTerrainInstance>>();
    GroupBlockMaterialRuns(tiles, sourceInstances, blocks, *groupedInstances);
    AppendGrassGeometry(context, tiles, blocks, *groupedInstances);
    outVertices = std::move(vertices);
    outIndices = std::move(indices);
    outTerrainCells = std::move(terrainCells);
    outInstances = groupedInstances->empty() ? nullptr : std::move(groupedInstances);
    outStaticLight = std::move(staticLight);
    return true;
}

LogicalGeometryAssetLease MakeTerrainLease(const TerrainSharedGeometry &asset, SessionId sessionId,
                                           SessionGeneration generation,
                                           std::atomic<std::uint64_t> &nextRevision)
{
    // The shared quad and cell grid remain useful even when no terrain instances exist.
    if (asset.vertices->empty())
        return {};
    return {{sessionId.RawValue(), generation.RawValue(),
             nextRevision.fetch_add(1, std::memory_order_relaxed)},
            asset.vertices,
            asset.indices,
            asset.terrainCells,
            asset.terrainInstances,
            asset.staticLight};
}

std::shared_ptr<TerrainSharedGeometry> BuildSharedGeometry(const BuildContext &context)
{
    std::vector<TerrainGeometryTile> tiles;
    std::vector<TerrainGeometryBlock> blocks;
    std::shared_ptr<const std::vector<RenderTapeVertex>> vertices;
    std::shared_ptr<const std::vector<std::uint32_t>> indices;
    std::shared_ptr<const std::vector<RenderTapeTerrainCell>> terrainCells;
    std::shared_ptr<const std::vector<RenderTapeTerrainInstance>> instances;
    std::shared_ptr<const TerrainLightSnapshot> staticLight;
    if (!BuildGeometry(context, tiles, blocks, vertices, indices, terrainCells, instances,
                       staticLight))
    {
        return nullptr;
    }
    auto asset = std::make_shared<TerrainSharedGeometry>(TerrainSharedGeometry{
        std::move(vertices), std::move(indices), std::move(terrainCells), std::move(instances),
        std::move(staticLight),
        std::make_shared<const std::vector<TerrainGeometryTile>>(std::move(tiles)),
        std::make_shared<const std::vector<TerrainGeometryBlock>>(std::move(blocks))});
    if (context.grassTexture != nullptr)
    {
        std::copy_n(context.grassTexture, TERRAIN_SIZE, asset->grassTexture.begin());
    }
    return asset;
}
} // namespace

void TerrainGeometryCache::SetLightSnapshot(
    std::shared_ptr<const TerrainLightSnapshot> light) noexcept
{
    lease_.terrainLight = light != nullptr || asset_ == nullptr ? std::move(light)
                                                                : asset_->staticLight;
}

bool TerrainGeometryCache::PrepareOnOwner(
    ApplicationKeeper &application, SessionId sessionId, SessionGeneration generation,
    std::atomic<std::uint64_t> &nextGeometryRevision, int world, float specialHeight,
    const float *heights, const unsigned char *layer1, const unsigned char *layer2,
    const float *layerAlpha, const float (*lights)[3], const WORD *walls, bool pkField,
    bool doppelGanger2, bool doppelGanger3, const SessionBitmapView &bitmaps, float *grassTexture,
    std::uint64_t contentRevision) noexcept
{
    if (sessionId.RawValue() == 0 || generation.RawValue() == 0 || heights == nullptr ||
        layer1 == nullptr || layer2 == nullptr || layerAlpha == nullptr || lights == nullptr ||
        walls == nullptr)
    {
        return false;
    }
    const bool current = contentRevision == contentRevision_ && world == world_ &&
                         specialHeight == specialHeight_ && pkField == pkField_ &&
                         doppelGanger2 == doppelGanger2_ && doppelGanger3 == doppelGanger3_ &&
                         asset_ != nullptr;
    if (!current)
    {
        assert(application.IsOwnerThread());
        BuildContext context{world,   contentRevision, specialHeight, heights, layer1,
                             layer2,  layerAlpha,      lights,        walls,   grassTexture,
                             pkField, doppelGanger2,   doppelGanger3, &bitmaps};
        std::shared_ptr<TerrainSharedGeometry> asset;
        try
        {
            // The revision identifies the effective immutable snapshot. A local
            // persistent edit never rebinds another session's unedited asset.
            const std::uint64_t hash = OwnerGeometryKey(context);
            const auto cached = application.terrainGeometryAssets_.find(hash);
            if (cached != application.terrainGeometryAssets_.end())
            {
                cached->second.unusedSinceMilliseconds = 0;
                asset = cached->second.asset;
            }
            if (asset == nullptr)
            {
                asset = BuildSharedGeometry(context);
                if (asset == nullptr)
                {
                    return false;
                }
                const std::size_t retainedBytes = TerrainSharedGeometryBytes(*asset);
                application.terrainGeometryAssets_.emplace(
                    hash, ApplicationKeeper::RetainedAsset<TerrainSharedGeometry>{
                              asset, retainedBytes, {}});
                application.retainedTerrainGeometryBytes_ += retainedBytes;
            }
        }
        catch (...)
        {
            return false;
        }
        LogicalGeometryAssetLease lease =
            MakeTerrainLease(*asset, sessionId, generation, nextGeometryRevision);
        if (!asset->vertices->empty() && !IsValid(lease))
            return false;
        if (grassTexture != nullptr)
        {
            // Dynamic-light fallback must sample the same grass pattern as
            // the shared mesh. Synchronize only when binding the map asset.
            std::copy(asset->grassTexture.begin(), asset->grassTexture.end(), grassTexture);
        }
        world_ = world;
        contentRevision_ = contentRevision;
        specialHeight_ = specialHeight;
        pkField_ = pkField;
        doppelGanger2_ = doppelGanger2;
        doppelGanger3_ = doppelGanger3;
        asset_ = std::move(asset);
        lease_ = std::move(lease);
    }
    else if (lease_.vertices != nullptr && (lease_.asset.sessionId != sessionId.RawValue() ||
                                            lease_.asset.generation != generation.RawValue()))
    {
        lease_.asset = {sessionId.RawValue(), generation.RawValue(), lease_.asset.revision};
    }
    return true;
}

void TerrainGeometryCache::Invalidate() noexcept
{
    world_ = -1;
    lease_ = {};
    asset_.reset();
}

std::size_t TerrainGeometryCache::StorageBytes(SharedAllocationCounter &allocations) const noexcept
{
    if (asset_ == nullptr)
        return 0;
    std::size_t bytes = allocations.CountVector(asset_->tiles);
    const std::size_t blocksBytes = allocations.CountVector(asset_->blocks);
    bytes += blocksBytes;
    bytes += allocations.CountVector(lease_.vertices);
    bytes += allocations.CountVector(lease_.indices);
    bytes += allocations.CountVector(lease_.terrainCells);
    const std::size_t instanceBytes = allocations.CountVector(lease_.terrainInstances);
    bytes += instanceBytes;
    if (instanceBytes != 0 && asset_->staticLight != nullptr)
        bytes += sizeof(*asset_->staticLight) +
                 asset_->staticLight->values.capacity() * sizeof(float);
    if (blocksBytes != 0)
    {
        for (const TerrainGeometryBlock &block : *asset_->blocks)
        {
            bytes += block.draws.capacity() * sizeof(block.draws[0]) +
                     block.afterDraws.capacity() * sizeof(block.afterDraws[0]) +
                      block.grassDraws.capacity() * sizeof(block.grassDraws[0]);
        }
    }
    return bytes;
}

const TerrainGeometryTile &TerrainGeometryCache::Tile(int x, int y) const noexcept
{
    static const TerrainGeometryTile empty;
    if (asset_ == nullptr || asset_->tiles->size() != TERRAIN_SIZE * TERRAIN_SIZE || x < 0 ||
        y < 0 || x >= TERRAIN_SIZE_MASK || y >= TERRAIN_SIZE_MASK)
    {
        return empty;
    }
    return (*asset_->tiles)[TERRAIN_INDEX(x, y)];
}

const TerrainGeometryBlock &TerrainGeometryCache::Block(int x, int y) const noexcept
{
    static const TerrainGeometryBlock empty;
    if (asset_ == nullptr || asset_->blocks->size() != TerrainBlockCount * TerrainBlockCount ||
        x < 0 || y < 0 || x >= TERRAIN_SIZE || y >= TERRAIN_SIZE)
    {
        return empty;
    }
    return (*asset_->blocks)[(y / TerrainBlockSize) * TerrainBlockCount + x / TerrainBlockSize];
}

using namespace SEASON3A;

bool CursedTemple::RenderObject_AfterCharacter(const ObjectDrawInput &draw, BMD *b)
{
    if (!gMapManager.IsCursedTemple())
        return false;

    switch (draw.type)
    {
    case 64:
    case 65:
    case 66:
    case 80: {
        if (draw.type == 64 || draw.type == 65 || draw.type == 66 || draw.type == 80)
        {
            b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
        }
    }
        return true;
    }

    return false;
}

bool CursedTemple::RenderObjectMesh(const ObjectDrawInput &draw, BMD *b, bool ExtraMon)
{
    if (!gMapManager.IsCursedTemple())
        return false;

    switch (draw.type)
    {
    case MODEL_CURSEDTEMPLE_STATUE:
        if (draw.action != MONSTER01_DIE)
        {
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV, draw.hiddenMesh);
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV, draw.hiddenMesh);
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV, draw.hiddenMesh);
            b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            b->RenderMesh(4, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            b->RenderMesh(5, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
        }
        return true;
    case MODEL_CURSEDTEMPLE_ALLIED_BASKET:
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
        return true;
    case MODEL_CURSEDTEMPLE_ILLUSION__BASKET:
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
        return true;
    case MODEL_CURSEDTEMPLE_ILLUSION_NPC: {
        float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;
        float fLumi2 = (sinf(WorldTime * 0.002f) + 1.f);

        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 5, fLumi2, draw.blendU,
                      draw.blendV, BITMAP_CURSEDTEMPLE_NPC_MESH_EFFECT);
        b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 2, fLumi);
    }
    break;
    case MODEL_CURSEDTEMPLE_ENTER_NPC: {
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        b->RenderMesh(0, RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);

        Vector(10.f, 10.f, 10.f, b->BodyLight);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 3, draw.blendLight,
                      draw.blendU, (int)WorldTime % 2000 * 0.001f);
    }
        return true;
    case MODEL_ILLUSION_SORCERER_SPIRIT_LIGHTNING:
    case MODEL_ILLUSION_SORCERER_SPIRIT_ICE: {
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.8f;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, WorldTime * 0.02f, 0, fLumi * 4.f,
                      draw.blendU, draw.blendV);
        fLumi = (sinf(WorldTime * 0.006f) + 1.f) * 0.9f;
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, WorldTime * 0.02f, 0, fLumi * 3.f,
                      draw.blendU, draw.blendV);
        b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, WorldTime * 0.002f, 0,
                      draw.blendLight * 2.f, -WorldTime * 0.002f, WorldTime * 0.02f);
    }
        return true;
    case 54: {
        float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.4f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, fLumi + 0.3f, draw.blendU,
                      draw.blendV, 2);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
        return true;
    }
    return false;
}

void CursedTemple::RenderAfterObjectMesh(const ObjectDrawInput &object, BMD *model, bool)
{
    RenderObject_AfterCharacter(object, model);
}

bool SEASON3A::CGM3rdChangeUp::RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (IsBalgasBarrackMap() || IsBalgasRefugeMap())
    {
        switch (draw.type)
        {
        case 79:
            b->StreamMesh = 0;
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          -(int)WorldTime % 10000 * 0.0001f);
            b->StreamMesh = -1;
            return true;
        case 81: {
            float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f + 0.3f;
            b->RenderBody(RENDER_TEXTURE, draw.alpha, 0, fLumi, draw.blendU, draw.blendV,
                          draw.hiddenMesh);
        }
            return true;
        case 82:
        case 83: {
            float fLumi = (sinf(WorldTime * 0.0005f)) * 10.f;
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV, draw.hiddenMesh);
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, 1, fLumi, draw.blendU, draw.blendV,
                          draw.hiddenMesh);
        }
            return true;
        case 57:
        case 78:
        case 84:

            return true;
        }
    }
    return false;
}

void SEASON3A::CGM3rdChangeUp::RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (!(IsBalgasBarrackMap() || IsBalgasRefugeMap()))
        return;

    switch (draw.type)
    {
    case 57:
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        break;
    case 78:
        b->BodyLight[0] = 0.52f;
        b->BodyLight[1] = 0.52f;
        b->BodyLight[2] = 0.52f;

        b->StreamMesh = 0;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, -(int)WorldTime % 100000 * 0.00001f, draw.blendV);
        b->StreamMesh = -1;
        break;
    case 84:
        b->StreamMesh = 0;
        float fLumi = (sinf(WorldTime * 0.001f) + 1.f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, 0, fLumi, (int)WorldTime % 10000 * 0.0001f,
                      draw.blendV, draw.hiddenMesh);
        b->StreamMesh = -1;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        break;
    }
}

bool SEASON3A::CGM3rdChangeUp::RenderMonsterObjectMesh(const ObjectDrawInput &input, BMD *b,
                                                       int ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    switch (draw.type)
    {
    case MODEL_BALRAM: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, 5);
        return true;
    }
    break;
    case MODEL_DEATH_SPIRIT: {
        float meshAlpha = draw.alpha;
        const float textureU = (int)WorldTime % 10000 * 0.0005f;
        b->BeginRender(meshAlpha);
        for (int i = 0; i < Models[draw.type].NumMeshs; i++)
        {
            if (i == 2)
            {
                meshAlpha = std::min(meshAlpha, 0.3f);
                b->EndRender();
                b->BeginRender(meshAlpha);
                Models[draw.type].StreamMesh = i;
            }
            const int blendMesh = i == 2 ? -2 : -1;
            b->RenderMesh(i, RENDER_TEXTURE, meshAlpha, blendMesh, draw.blendLight, textureU,
                          draw.blendV);
        }
        b->EndRender();
        return true;
    }
    break;
    case MODEL_SORAM: {
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        b->BeginRender(draw.alpha);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, 5);
        b->EndRender();
        return true;
    }
    break;
    case MODEL_DARK_ELF:
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME6, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
        b->RenderMesh(1, RENDER_BRIGHT | RENDER_CHROME6, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        return true;
    }
    return false;
}

// MainScene.cpp - Main game scene implementation

bool SessionRenderUnit::RenderMainTerrainAndObjects(int width, int height)
{
    const bool objectsFirst =
        sessionKeeper_.WorldState().definition->presentation.objectsBeforeTerrain;
    const auto beginPass = [this, width, height](RenderTapePass pass) {
        return SwitchWorldRenderTapePass(pass, 0, REFERENCE_HEIGHT - height, width, height);
    };
    if (ShouldRenderGameTerrain())
    {
        if (objectsFirst)
        {
            if (!beginPass(RenderTapePass::Objects))
                return false;
            FRAME_PROFILE(Objects);
            RenderObjects();
        }
        if (!beginPass(RenderTapePass::Terrain))
            return false;
        FRAME_PROFILE(Terrain);
        RenderTerrain(false);
    }
    if (!objectsFirst)
    {
        if (!beginPass(RenderTapePass::Objects))
            return false;
        FRAME_PROFILE(Objects);
        RenderObjects();
    }
    return recordingSucceeded_;
}

// OMF-01785
// OMF-01786
// OMF-01787
// OMF-01788
// OMF-01789
// OMF-01790
// OMF-01793
// OMF-01794
// OMF-01795

bool GMBloodCastle::RenderWholeObject(const ObjectDrawInput &input, BMD *b, bool)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (!(draw.type == 28 || draw.type == 29))
        return false;
    b->BeginRender(draw.alpha);
    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                  draw.blendV, draw.hiddenMesh);
    b->EndRender();

    EnableAlphaTest();

    vec3_t Position;

    glColor4f(0.f, 0.f, 0.f, 1.f);
    VectorCopy(draw.position, Position);
    Position[2] = RequestTerrainHeight(draw.position[0], draw.position[1]);
    VectorCopy(Position, b->BodyOrigin);
    b->RenderBodyShadow(draw.blendMesh, 2);

    return true;
}

void CGMCrywolf1st::RenderNoticesCryWolf()
{
    if (m_CrywolfState != CRYWOLF_STATE_READY)
    {
        return;
    }

    int iTemp = 0;

    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.SetBgColor(0, 0, 0, 170);

    iTemp = 4 * iNextNotice;

    wchar_t szText[256];
    int nText = 0;

    for (int i = 0; i < 4; i++)
    {
        if (i == 0)
        {
            g_RenderText.SetTextColor(100, 200, 255, 255);
        }
        else
        {
            g_RenderText.SetTextColor(100, 150, 255, 255);
        }
        nText = 1957 + i + iTemp;
        if (1966 == nText || 1967 == nText)
        {
            mu_swprintf(szText, I18N::Game::Lookup(nText));
            g_RenderText.RenderText(190, 63 + i * 13, szText);
        }
        else
            g_RenderText.RenderText(190, 63 + i * 13, I18N::Game::Lookup(nText));
    }
}

//. ������Ʈ

bool CGMCrywolf1st::RenderCryWolf1stObjectMesh(const ObjectDrawInput &input, BMD *b, int ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (!IsCyrWolf1st())
    {
        return false;
    }

    float Luminosity;
    vec3_t Light;

    switch (draw.type)
    {
    case 36: {
        if (m_OccupationState == CRYWOLF_OCCUPATION_STATE_PEACE)
        {
            Vector(0.4f, 0.4f, 0.4f, b->BodyLight);
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
        else
        {
            Vector(0.4f, 0.4f, 0.4f, b->BodyLight);
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
    }
        return true;
    case 56:
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        return true;
    case 41:
        Vector(0.2f, 0.7f, 0.f, Light);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        return true;
    case 57:
    case 71:
    case 72:
    case 73:
    case 74:
    case 77:
    case 78:
        return true;
    case 81: {
        if (m_OccupationState == CRYWOLF_OCCUPATION_STATE_WAR)
        {

            float fTemp = 0.1f;

            if (m_StatueHP <= 10)
                fTemp = 1.0f;
            b->BodyLight[0] = (sinf(WorldTime * 0.004f) * 3.0f) * fTemp + 5.0f - m_StatueHP / 20.0f;
            b->BodyLight[1] = m_StatueHP / 25.0f - 1.0f;
            b->BodyLight[2] = m_StatueHP / 20.0f - 0.5f;

            b->StreamMesh = 0;
            b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
            b->StreamMesh = -1;

            b->StreamMesh = 1;
            b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME2, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
            b->StreamMesh = -1;
        }
    }
        return true;
    case MODEL_SKILL_FURY_STRIKE: {
        Vector(0.0f, 0.0f, 0.9f, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
        return true;
    }

    return RenderCryWolf1stMonsterObjectMesh(draw, b, ExtraMon);
}

bool CGMCrywolf1st::RenderCryWolf1stMonsterObjectMesh(const ObjectDrawInput &input, BMD *b,
                                                      int ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    switch (draw.type)
    {
    case MODEL_SORAM: {
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        b->BeginRender(draw.alpha);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, 5);
        b->EndRender();
        return true;
    }
    break;
    case MODEL_BALGASS: {
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        b->BeginRender(draw.alpha);
        for (int i = 0; i < Models[draw.type].NumMeshs; i++)
        {
            b->RenderMesh(i, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(i, RENDER_CHROME2 | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME3);
        }
        b->EndRender();
        return true;
    }
    break;
    case MODEL_BALRAM: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, 5);
        return true;
    }
    break;
    case MODEL_DEATH_SPIRIT: {
        float meshAlpha = draw.alpha;
        const float textureU = (int)WorldTime % 10000 * 0.0005f;
        b->BeginRender(meshAlpha);
        for (int i = 0; i < Models[draw.type].NumMeshs; i++)
        {
            if (i == 2)
            {
                meshAlpha = std::min(meshAlpha, 0.3f);
                b->EndRender();
                b->BeginRender(meshAlpha);
                Models[draw.type].StreamMesh = i;
            }
            const int blendMesh = i == 2 ? -2 : -1;
            b->RenderMesh(i, RENDER_TEXTURE, meshAlpha, blendMesh, draw.blendLight, textureU,
                          draw.blendV);
        }
        b->EndRender();
        return true;
    }
    break;
    case MODEL_SCOUT: {
        if (ExtraMon)
        {
            Vector(0.5f, 0.5f, 0.6f, b->BodyLight);
            b->BeginRender(draw.alpha);
            for (int i = 0; i < Models[draw.type].NumMeshs; i++)
            {
                b->RenderMesh(i, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(i, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);
            }
            b->EndRender();
        }
        else
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, 5);
        return true;
    }
    break;
    case MODEL_WEREWOLF_HERO: {
        if (ExtraMon)
        {
            Vector(0.5f, 0.5f, 0.8f, b->BodyLight);
            b->BeginRender(draw.alpha);
            for (int i = 0; i < Models[draw.type].NumMeshs; i++)
            {
                b->RenderMesh(i, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(i, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);
            }
            b->EndRender();
        }
        else
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, 5);
        return true;
    }
    break;
    case MODEL_TANTALLOS: {
        Vector(0.6f, 0.8f, 0.6f, b->BodyLight);
        b->BeginRender(draw.alpha);
        for (int i = 0; i < Models[draw.type].NumMeshs; i++)
        {
            b->RenderMesh(i, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(i, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);
        }
        b->EndRender();
        return true;
    }
    break;
    case MODEL_BLOODY_WOLF: {
        Vector(0.6f, 0.8f, 0.6f, b->BodyLight);
        b->BeginRender(draw.alpha);
        for (int i = 0; i < Models[draw.type].NumMeshs; i++)
        {
            b->RenderMesh(i, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(i, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);
        }
        b->EndRender();
        return true;
    }
    break;
    case MODEL_BEAM_KNIGHT: //318
    {
        Vector(0.6f, 0.8f, 0.6f, b->BodyLight);
        b->BeginRender(draw.alpha);
        for (int i = 0; i < Models[draw.type].NumMeshs; i++)
        {
            b->RenderMesh(i, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(i, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);
        }
        b->EndRender();
        return true;
    }
    break;
    case MODEL_DRAGON_: //319
    {
        Vector(0.6f, 0.8f, 0.6f, b->BodyLight);
        b->BeginRender(draw.alpha);
        for (int i = 0; i < Models[draw.type].NumMeshs; i++)
        {
            b->RenderMesh(i, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(i, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);
        }
        b->EndRender();
        return true;
    }
    break;
    }
    return false;
}

void CGMCrywolf1st::RenderBaseSmoke()
{
    EnableAlphaTest();

    glColor3f(0.4f, 0.4f, 0.45f);
    float WindX2 = (float)((int)WorldTime % 100000) * 0.0005f;
    RenderBitmapUV(BITMAP_CHROME + 3, 0.f, 0.f, (float)REFERENCE_WIDTH,
                   (float)REFERENCE_HEIGHT - 45.f, WindX2, 0.f, 3.f, 2.f);
    EnableAlphaBlend();
    float WindX = (float)((int)WorldTime % 100000) * 0.0002f;
    RenderBitmapUV(BITMAP_CHROME + 2, 0.f, 0.f, (float)REFERENCE_WIDTH,
                   (float)REFERENCE_HEIGHT - 45.f, WindX, 0.f, 0.3f, 0.3f);
}

// OMF-02133
// OMF-02134
// OMF-02135
// OMF-02136
// OMF-02137
// OMF-02141
// OMF-02142
// OMF-02143
// OMF-02144
// OMF-02145
// OMF-02146
// OMF-02147
// OMF-02148
// OMF-02150
// OMF-02153
// OMF-02155
// OMF-02156
// OMF-02157
// OMF-02159
// OMF-02160
// OMF-02161
// OMF-02163
// OMF-02164
// OMF-02165
// OMF-02166
// OMF-02167

bool CGMCrywolf1st::RenderCryWolf1stMonsterVisual(const CHARACTER *c, const ObjectDrawInput &input,
                                                  BMD *b)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (!IsCyrWolf1st() && !gMapManager.InDevilSquare())
        return false;
    switch (draw.type)
    {
    case MODEL_CRYWOLF_ALTAR1:
    case MODEL_CRYWOLF_ALTAR2:
    case MODEL_CRYWOLF_ALTAR3:
    case MODEL_CRYWOLF_ALTAR4:
    case MODEL_CRYWOLF_ALTAR5: {
        vec3_t Light = {0.f, 0.f, 0.f};
        float fRotation1 = WorldTime * 0.01f, fRotation2 = -WorldTime * 0.01f;
        float Luminosity = sinf(WorldTime * 0.002f) * 0.1f + 0.28f;
        float Luminosity2 = sinf(WorldTime * 0.002f) * 0.04f + 0.2f;

        EnableAlphaBlend();

        if (g_isCharacterBuff(o, eBuff_CrywolfAltarContracted))
        {
            Vector(Luminosity2, Luminosity2, 0.05f, Light);
            fRotation1 = WorldTime * 0.01f;
            fRotation2 = -WorldTime * 0.01f;
        }
        if (g_isCharacterBuff(o, eBuff_CrywolfAltarOccufied))
        {
            Vector(Luminosity, 0.1f, 0.1f, Light);
        }
        if (g_isCharacterBuff(o, eBuff_CrywolfAltarDisable))
        {
            Vector(Luminosity, 0.1f, 0.1f, Light);
        }
        if (g_isCharacterBuff(o, eBuff_CrywolfAltarEnable) ||
            g_isCharacterBuff(o, eBuff_CrywolfAltarAttempt))
        {
            fRotation1 = WorldTime * 0.01f;
            Vector(0.15f, 0.15f, Luminosity, Light);
        }

        Vector(Light[0] * 2.0f, Light[1] * 2.0f, Light[2] * 2.0f, Light);

        RenderTerrainAlphaBitmap(BITMAP_MAGIC_CIRCLE, draw.position[0], draw.position[1], 2.8f,
                                 2.8f, Light, fRotation1);
        RenderTerrainAlphaBitmap(BITMAP_MAGIC_CIRCLE, draw.position[0], draw.position[1], 3.6f,
                                 3.6f, Light, fRotation2);
        DisableAlphaBlend();

        if (fRotation1 >= 360.0f)
            fRotation1 = 0.0f;
        if (fRotation2 >= 360.0f)
            fRotation2 = 0.0f;
    }
    break;
    }
    return false;
}

bool CGMCrywolf1st::RenderObjectMeshBeforeShared(const ObjectDrawInput &input, BMD *model,
                                                 bool extraMonster)
{
    const auto &draw = input;
    const auto *object = input.source;
    return RenderCryWolf1stObjectMesh(draw, model, extraMonster);
}

bool CGMCrywolf1st::RenderMonsterVisual(const CHARACTER *character, const ObjectDrawInput &input,
                                        BMD *model)
{
    const auto &draw = input;
    const auto *object = input.source;
    return RenderCryWolf1stMonsterVisual(character, draw, model);
}

void CGMCrywolf1st::RenderAtmosphere()
{
    if (ashies)
        RenderBaseSmoke();
}

bool GMDevias::RenderObjectVisual(const ObjectDrawInput &input, BMD *b)
{
    const auto &draw = input;
    const auto *o = input.source;
    switch (draw.type)
    {
#ifdef DEVIAS_XMAS_EVENT2007
    case 110: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        Vector(1.f, 1.0f, 1.f, b->BodyLight);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
    }
    break;
#endif // DEVIAS_XMAS_EVENT2007
#ifdef DEVIAS_XMAS_EVENT
    case 105: {
        Vector(1.f, 1.0f, 1.f, b->BodyLight);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
    }
    break;
    case 106: {
        Vector(1.0f, 1.0f, 1.0f, b->BodyLight);
        b->RenderMesh(3, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, BITMAP_CHROME8);
    }
    break;
#endif // DEVIAS_XMAS_EVENT
    }
    return true;
}

bool GMDevilSquare::RenderMonsterVisual(const CHARACTER *character, const ObjectDrawInput &input,
                                        BMD *model)
{
    const auto &draw = input;
    const auto *object = input.source;
    return TheMapProcess().Crywolf1st().RenderCryWolf1stMonsterVisual(character, draw, model);
}

bool CGMDoppelGanger2::RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (IsDoppelGanger2() == false)
        return false;

    float fBlendMeshLight = 0;

    switch (draw.type)
    {
    case 10:
    case 19:
    case 20:
    case 31:
    case 33:

        return true;
    case 15: {
        b->StreamMesh = 0;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      -(int)WorldTime % 10000 * 0.0001f);
        b->StreamMesh = -1;
        return true;
    }
    case 16: {
        float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, 0, fLumi, draw.blendU, draw.blendV,
                      draw.hiddenMesh);
        return true;
    }
    case 67: {
        b->StreamMesh = 1;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      (int)WorldTime % 10000 * 0.0001f);
        b->StreamMesh = -1;
        vec3_t light;
        Vector(1.0f, 0.0f, 0.0f, light);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        VectorCopy(light, b->BodyLight);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, 0.5f, 0, 0.5f, draw.blendU, draw.blendV);

        return true;
    }
    case 68: {
        b->StreamMesh = 1;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      (int)WorldTime % 10000 * 0.0001f);
        b->StreamMesh = -1;

        vec3_t light;
        Vector(1.0f, 0.0f, 0.0f, light);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        VectorCopy(light, b->BodyLight);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, 0.5f, 0, 0.5f, draw.blendU, draw.blendV);

        return true;
    }
    case 72: {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        float fLumi = (sinf(WorldTime * 0.003f) + 1.f) * 0.5f * 0.5f + 0.5f;
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, 1, fLumi, draw.blendU, draw.blendV,
                      draw.hiddenMesh);
        return true;
    }
    case MODEL_ICE_WALKER:
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, BITMAP_DOPPELGANGER_ICEWALKER0);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, BITMAP_DOPPELGANGER_ICEWALKER1);
        return true;
    case MODEL_LARVA:
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, BITMAP_DOPPELGANGER_SNAKE01);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        return true;
    case MODEL_MAD_BUTCHER:
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        fBlendMeshLight = (sinf(WorldTime * 0.003f) + 1.0f) * 0.5f * 0.8f;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fBlendMeshLight,
                      draw.blendU, draw.blendV, BITMAP_BUGBEAR_R);
        //b->Actions[MONSTER01_WALK].PlaySpeed =		0.34f;
        return true;
    case MODEL_TERRIBLE_BUTCHER:
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        fBlendMeshLight = (sinf(WorldTime * 0.003f) + 1.0f) * 0.5f * 0.8f;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fBlendMeshLight,
                      draw.blendU, draw.blendV, BITMAP_BUGBEAR_R);
        return true;
    case MODEL_DOPPELGANGER:
        return true;
    }

    return false;
}

void CGMDoppelGanger2::RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (!IsDoppelGanger2())
        return;

    switch (draw.type)
    {
    case 16: {
        float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, 0, fLumi, draw.blendU, draw.blendV,
                      draw.hiddenMesh);
    }
    break;
    case 10:
    case 19:
    case 20:
    case 31:
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        break;
    case 33:
        b->StreamMesh = 0;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, (int)WorldTime % 10000 * 0.0001f);
        b->StreamMesh = -1;
        break;
    default:
        break;
    }
}

bool CGMDoppelGanger2::RenderMonsterVisual(const CHARACTER *c, const ObjectDrawInput &input, BMD *b)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (draw.type != MODEL_DOPPELGANGER)
        return false;
    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                  draw.blendU, draw.blendV);
    b->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, 0.3f, draw.blendMesh, draw.blendLight,
                  draw.blendU, draw.blendV);
    return true;
}

bool CGMDoppelGanger2::IsEarlyAfterCharacterObject(const OBJECT &object) const
{
    return object.Type == 16 || object.Type == 67 || object.Type == 68;
}

void CGMDoppelGanger2::RenderEarlyAfterCharacterObjects(OBJECT *head)
{
    for (OBJECT *object = head; object; object = object->Next)
        if (IsEarlyAfterCharacterObject(*object) &&
            TestFrustrum2D(object->Position[0] * 0.01f, object->Position[1] * 0.01f, -600.f))
            RenderObject_AfterCharacter(object);
}

bool CGMDoppelGanger3::RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (IsDoppelGanger3() == false)
        return false;

    float fBlendMeshLight = 0;

    switch (draw.type)
    {
    case 19:
    case 20:
    case 31:
    case 33:

        return true;
    case 38: {
    }
        return true;
    case 43: {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
        return true;
    case MODEL_ICE_WALKER:
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, BITMAP_DOPPELGANGER_ICEWALKER0);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, BITMAP_DOPPELGANGER_ICEWALKER1);
        return true;
        break;
    case MODEL_LARVA:
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, BITMAP_DOPPELGANGER_SNAKE01);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        return true;
    case MODEL_MAD_BUTCHER:
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        fBlendMeshLight = (sinf(WorldTime * 0.003f) + 1.0f) * 0.5f * 0.8f;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fBlendMeshLight,
                      draw.blendU, draw.blendV, BITMAP_BUGBEAR_R);
        return true;
    case MODEL_TERRIBLE_BUTCHER:
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        fBlendMeshLight = (sinf(WorldTime * 0.003f) + 1.0f) * 0.5f * 0.8f;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fBlendMeshLight,
                      draw.blendU, draw.blendV, BITMAP_BUGBEAR_R);
        return true;
    case MODEL_DOPPELGANGER:
        return true;
    }

    return false;
}

void CGMDoppelGanger3::RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (!IsDoppelGanger3())
        return;

    switch (draw.type)
    {
    case 19:
    case 20:
    case 31:
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        break;
    case 33:
        b->StreamMesh = 0;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, (int)WorldTime % 10000 * 0.0001f);
        b->StreamMesh = -1;
        break;
    case 38: {
        b->BodyLight[0] = std::min<float>(b->BodyLight[0] * 2.0f, 1.0f);
        b->BodyLight[1] = std::min<float>(b->BodyLight[1] * 2.0f, 1.0f);
        b->BodyLight[2] = std::min<float>(b->BodyLight[2] * 2.0f, 1.0f);
        b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
    }
    break;
    default:
        break;
    }
}

bool CGMDoppelGanger3::RenderMonsterVisual(const CHARACTER *c, const ObjectDrawInput &input, BMD *b)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (draw.type != MODEL_DOPPELGANGER)
        return false;
    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                  draw.blendU, draw.blendV);
    b->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, 0.3f, draw.blendMesh, draw.blendLight,
                  draw.blendU, draw.blendV);
    return true;
}

bool CGMDoppelGanger4::RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    auto draw = input;
    const auto *o = input.source;
    if (IsDoppelGanger4() == false)
        return false;

    float fBlendMeshLight = 0;

    switch (draw.type)
    {
    case 19:
    case 20:
    case 31:
    case 33:

        return true;
    case 103: {
        float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;
        draw.hiddenMesh = 1;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, 1, fLumi, draw.blendU, draw.blendV);
    }
        return true;
    case 76:
    case 77:
    case 91:
    case 92:
    case 95:
    case 105:

        return true;
    case 142:
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, draw.hiddenMesh);
        return true;
    case MODEL_ICE_WALKER:
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, BITMAP_DOPPELGANGER_ICEWALKER0);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, BITMAP_DOPPELGANGER_ICEWALKER1);
        return true;
    case MODEL_LARVA:
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, BITMAP_DOPPELGANGER_SNAKE01);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        return true;
    case MODEL_MAD_BUTCHER:
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        fBlendMeshLight = (sinf(WorldTime * 0.003f) + 1.0f) * 0.5f * 0.8f;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fBlendMeshLight,
                      draw.blendU, draw.blendV, BITMAP_BUGBEAR_R);
        return true;
    case MODEL_TERRIBLE_BUTCHER:
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        fBlendMeshLight = (sinf(WorldTime * 0.003f) + 1.0f) * 0.5f * 0.8f;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fBlendMeshLight,
                      draw.blendU, draw.blendV, BITMAP_BUGBEAR_R);
        return true;
    case MODEL_DOPPELGANGER:
        return true;
    }

    return false;
}

bool CGMDoppelGanger4::RenderObjectVisual(const ObjectDrawInput &input, BMD *b)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (!IsDoppelGanger4())
        return false;
    switch (draw.type)
    {
    case 85:
        b->BeginRender(draw.alpha);
        b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(3, RENDER_CHROME | RENDER_DARK, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, BITMAP_CHROME);
        b->EndRender();
        return true;
    case 96:
        b->StreamMesh = 0;
        glAlphaFunc(GL_GREATER, 0.0f);
        b->RenderMesh(0, RENDER_TEXTURE, 1.0f, draw.blendMesh, draw.blendLight, draw.blendU,
                      -(int)WorldTime % 20000 * 0.00005f);
        glAlphaFunc(GL_GREATER, 0.25f);
        b->StreamMesh = -1;
        return true;
    }
    return false;
}

void CGMDoppelGanger4::RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (!IsDoppelGanger4())
        return;

    switch (draw.type)
    {
    case 19:
    case 20:
    case 31:
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        break;
    case 33:
        b->StreamMesh = 0;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, (int)WorldTime % 10000 * 0.0001f);
        b->StreamMesh = -1;
        break;
    case 76: {
        b->BodyLight[0] = 0.52f;
        b->BodyLight[1] = 0.52f;
        b->BodyLight[2] = 0.52f;
        b->StreamMesh = 0;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, -(int)WorldTime % 100000 * 0.00001f, draw.blendV);
        b->StreamMesh = -1;
    }
    break;
    case 95: {
        b->BeginRender(draw.alpha);

        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(0, RENDER_CHROME | RENDER_DARK, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, BITMAP_CHROME);
        b->EndRender();

        vec3_t p, Position, Light;
        for (int i = 0; i < 10; ++i)
        {
            Vector(0.0f, 0.0f, 0.0f, p);
            b->TransformPosition(draw.bones[i], p, Position, false);
            Vector(0.1f, 0.1f, 0.3f, Light);
            CreateSprite(BITMAP_SPARK + 1, Position, 7.5f, Light, o);
        }
    }
    break;
    case 105: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, BITMAP_CHROME);
    }
    break;
    case 77:
    case 91:
    case 92: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    break;
    }
}

// 몬스터 사운드

bool CGMDoppelGanger4::RenderMonsterVisual(const CHARACTER *c, const ObjectDrawInput &input, BMD *b)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (draw.type != MODEL_DOPPELGANGER)
        return false;
    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                  draw.blendU, draw.blendV);
    b->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, 0.3f, draw.blendMesh, draw.blendLight,
                  draw.blendU, draw.blendV);
    return true;
}

bool GMEmpireGuardian2::RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (gMapManager.IsEmpireGuardian2() == false)
        return false;

    if (true == g_EmpireGuardian1.RenderObjectMesh(draw, b, ExtraMon))
    {
        return true;
    }

    switch (draw.type)
    {
    case MODEL_LUCAS:
    case MODEL_HAMMERIZE:
    case MODEL_ATICLES_HEAD:
    case MODEL_DARK_GHOST: {
        RenderMonster(draw, b, ExtraMon);

        return true;
    }
    }

    return false;
}

bool GMEmpireGuardian2::RenderMonster(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    switch (draw.type)
    {
    case MODEL_HAMMERIZE: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
        return true;
    case MODEL_ATICLES_HEAD: {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
    }
        return true;
    case MODEL_DARK_GHOST: {
        if (draw.action == MONSTER01_DIE)
        {
            Vector(0.3f, 1.0f, 0.2f, b->BodyLight);
        }
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 3, 0.5f, draw.blendU,
                      draw.blendV);
    }
        return true;
    }
    return false;
}

void GMEmpireGuardian2::RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (gMapManager.IsEmpireGuardian2() == false)
        return;

    switch (draw.type)
    {
    case 0:
    case 1:
    case 3:
    case 44: {
        float fLumi;
        fLumi = (sinf(WorldTime * 0.0015f) + 1.0f) * 0.4f + 0.2f;

        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 2, fLumi, draw.blendU,
                      draw.blendV);
    }
    break;

    case 81: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    break;
    }
}

void GMEmpireGuardian2::RenderFrontSideVisual()
{
    g_EmpireGuardian1.RenderFrontSideVisual();
}

bool GMEmpireGuardian3::RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (gMapManager.IsEmpireGuardian3() == false)
        return false;

    if (true == g_EmpireGuardian1.RenderObjectMesh(draw, b, ExtraMon))
    {
        return true;
    }

    return false;
}

bool GMEmpireGuardian3::RenderMonster(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    return false;
}

void GMEmpireGuardian3::RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (gMapManager.IsEmpireGuardian3() == false)
        return;

    switch (draw.type)
    {
    case 0:
    case 1:
    case 3:
    case 44: {
        float fLumi;
        fLumi = (sinf(WorldTime * 0.0015f) + 1.0f) * 0.4f + 0.2f;

        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 2, fLumi, draw.blendU,
                      draw.blendV);
    }
    break;

    case 81: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    break;
    }
}

void GMEmpireGuardian3::RenderFrontSideVisual()
{
    g_EmpireGuardian1.RenderFrontSideVisual();
}

bool GMEmpireGuardian3::RenderMonsterVisual(const CHARACTER *c, const ObjectDrawInput &input,
                                            BMD *b)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (draw.type != MODEL_DUAL_BERSERKER)
        return false;
    b->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, 0.5f, 1, draw.blendLight, draw.blendU,
                  draw.blendV);
    return true;
}

bool GMEmpireGuardian4::RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (gMapManager.IsEmpireGuardian4() == false)
        return false;

    if (true == g_EmpireGuardian1.RenderObjectMesh(draw, b, ExtraMon))
    {
        return true;
    }

    switch (draw.type)
    {
    case MODEL_GAYION:
    case MODEL_JERRY:
    case MODEL_LUCAS: {
        RenderMonster(draw, b, ExtraMon);

        return true;
    }
    case 96:
    case 97:
    case 100: {
        Vector(0.170382, 0.170382, 0.170382, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
        return true;
    case MODEL_STAR_GATE: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
        return true;

    case MODEL_RUSH_GATE: {
        if (draw.action != MONSTER01_DIE)
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
    }
        return true;
    }

    return false;
}

bool GMEmpireGuardian4::RenderMonster(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    switch (draw.type)
    {
    case MODEL_GAYION: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    break;
    case MODEL_JERRY:
    case MODEL_DEATH_ANGEL_3: {
        vec3_t v3LightBackup;

        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        Vector(b->BodyLight[0], b->BodyLight[1], b->BodyLight[2], v3LightBackup);
        Vector(0.3f, 0.3f, 0.3f, b->BodyLight);

        b->RenderMesh(3, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);

        VectorCopy(v3LightBackup, b->BodyLight);
    }
        return true;
    }
    return false;
}

void GMEmpireGuardian4::RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (gMapManager.IsEmpireGuardian4() == false)
        return;

    switch (draw.type)
    {
    case 0:
    case 1:
    case 3:
    case 44: {
        float fLumi;
        fLumi = (sinf(WorldTime * 0.0015f) + 1.0f) * 0.4f + 0.2f;

        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 2, fLumi, draw.blendU,
                      draw.blendV);
    }
    break;

    case 81: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    break;
    }
}

void GMEmpireGuardian4::RenderFrontSideVisual()
{
    g_EmpireGuardian1.RenderFrontSideVisual();
}

bool CGMGmArea::RenderObjectVisual(const ObjectDrawInput &input, BMD *model)
{
    const auto &draw = input;
    const auto *object = input.source;
    return TheMapProcess().Kanturu1st().RenderKanturu1stObjectVisual(draw, model);
}

bool CGMGmArea::RenderObjectMesh(const ObjectDrawInput &input, BMD *model, bool)
{
    const auto &draw = input;
    const auto *object = input.source;
    return TheMapProcess().Kanturu1st().RenderKanturu1stObjectMesh(draw, model);
}

void CGMGmArea::RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *model, bool)
{
    const auto &draw = input;
    const auto *object = input.source;
    TheMapProcess().Kanturu1st().RenderKanturu1stAfterObjectMesh(draw, model);
}

float GMIcarus::ItemDrawHeight(const OBJECT &object, int index)
{
    return object.Position[2] + 10.f * sinf(float(index * 1237 + WorldTime) * 0.002f);
}

#ifdef ASG_ADD_MAP_KARUTAN

bool CGMKarutan1::RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (!IsKarutanMap())
        return false;

    switch (draw.type)
    {
    case 1:
    case 3:
    case 54:
    case 55:
    case 56:
    case 57:
    case 58:
    case 62:
    case 63:
    case 119:

        return true;
    case 66:

        return true;

#ifdef ASG_ADD_KARUTAN_MONSTERS
    case MODEL_BONE_SCORPION:
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);

        if (draw.action != MONSTER01_DIE)
        {
            float fLumi = sinf(WorldTime * 0.002f) + 0.5f;
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                          draw.blendV, BITMAP_BONE_SCORPION_SKIN_EFFECT);
        }
        return true;
    case MODEL_CRYPTA:
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);

        if (draw.action != MONSTER01_DIE)
        {
            float fLumi = sinf(WorldTime * 0.002f) + 1.0f;
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                          draw.blendV, BITMAP_KRYPTA_BALL_EFFECT);
        }
        return true;
    case MODEL_CRYPOS:
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        Vector(1.0f, 1.0f, 1.0f, b->BodyLight);
        b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                      draw.blendU, draw.blendV);
        return true;
    case MODEL_CONDRA:
        if (draw.action != MONSTER01_DIE)
        {
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);

            float fLumi = sinf(WorldTime * 0.002f) + 1.0f;
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                          draw.blendV, BITMAP_CONDRA_SKIN_EFFECT);
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, fLumi, draw.blendU,
                          draw.blendV, BITMAP_CONDRA_SKIN_EFFECT2);

            b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 3, draw.blendLight * 0.9f,
                          WorldTime * 0.0010f, draw.blendV);
            Vector(0.4f, 0.95f, 1.0f, b->BodyLight);
            b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 2, draw.blendLight * 0.3f,
                          WorldTime * 0.0015f, draw.blendV);
        }
        return true;
    case MODEL_NACONDRA:
        if (draw.action != MONSTER01_DIE)
        {
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);

            float fLumi = sinf(WorldTime * 0.002f) + 1.0f;
            b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 2, fLumi, draw.blendU,
                          draw.blendV, BITMAP_NARCONDRA_SKIN_EFFECT);
            b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 3, fLumi, draw.blendU,
                          draw.blendV, BITMAP_NARCONDRA_SKIN_EFFECT2);
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, fLumi, draw.blendU,
                          draw.blendV, BITMAP_NARCONDRA_SKIN_EFFECT3);

            Vector(1.0f, 1.0f, 1.0f, b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                          draw.blendU, draw.blendV);

            Vector(1.0f, 0.1f, 1.0f, b->BodyLight);
            b->RenderMesh(4, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 4, draw.blendLight * 0.9f,
                          WorldTime * 0.0010f, draw.blendV);
            Vector(0.7f, 0.4f, 1.0f, b->BodyLight);
            b->RenderMesh(5, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 5, draw.blendLight * 0.7f,
                          WorldTime * 0.0015f, draw.blendV);
        }
        return true;
#endif // ASG_ADD_KARUTAN_MONSTERS
    }

    return false;
}

void CGMKarutan1::RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (!IsKarutanMap())
        return;

    switch (draw.type)
    {
    case 1:
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
        b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight);
        break;
    case 3:
    case 56:
    case 62:
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        break;
    case 54: {
        float fLumi = (sinf(WorldTime * 0.002f) + 0.5f) * 0.5f + 1.0f;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi);
    }
    break;
    case 55:
    case 57:
        b->StreamMesh = 0;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, (int)WorldTime % 10000 * 0.0001f);
        b->StreamMesh = -1;
        break;
    case 58: {
        float fLumi = (sinf(WorldTime * 0.001f) + 1.f) * 0.5f;
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, fLumi);
        b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight);
    }
    break;
    case 63: {
        float fLumi = (sinf(WorldTime * 0.001f) + 1.f) * 0.5f;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 2, fLumi);
        b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
    }
    break;
    case 66:
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight);
        break;
    case 119: {
        float fLumi = (sinf(WorldTime * 0.001f) + 1.f) * 0.5f;
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, fLumi);
    }
    break;
    }
}

void CGMKarutan1::RenderAtmosphere()
{
    EnableAlphaTest();
    EnableAlphaBlend();
    glColor3f(0.3f, 0.3f, 0.25f);
    float fWindX = (float)((int)WorldTime % 100000) * 0.004f;
    RenderBitmapUV(BITMAP_CHROME + 3, 0.f, 0.f, (float)REFERENCE_WIDTH,
                   (float)REFERENCE_HEIGHT - 45.f, fWindX, 0.f, 3.f, 2.f);
}
#endif // ASG_ADD_MAP_KARUTAN

bool GMLegacyLogin::RenderWholeObject(const ObjectDrawInput &input, BMD *b, bool)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (!(draw.type == 90))
        return false;
    b->BodyLight[0] = 1.0f;
    b->BodyLight[1] = 1.0f;
    b->BodyLight[2] = 1.0f;

    b->StreamMesh = 0;
    b->RenderMesh(0, RENDER_DARK, 1.0f, draw.blendMesh, 1.0f, draw.blendU, draw.blendV);
    b->StreamMesh = -1;
    return true;
}

bool GMLorencia::RenderWholeObject(const ObjectDrawInput &input, BMD *b, bool)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (!(draw.type == MODEL_WATERSPOUT))
        return false;
    b->BeginRender(draw.alpha);
    b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
    b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
    b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
    b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, 3, draw.blendLight, draw.blendU, draw.blendV);
    b->EndRender();
    return true;
}

bool GMLostTower::RenderWholeObject(const ObjectDrawInput &input, BMD *b, bool)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (!(draw.type == 23 || draw.type == 19 || draw.type == 20 || draw.type == 3 ||
          draw.type == 4))
        return false;
    vec3_t Light;
    if (draw.type == 23)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (draw.type == 19 || draw.type == 20)
    {
        VectorCopy(b->BodyLight, Light);
        Vector(1.f, 0.2f, 0.1f, b->BodyLight);
        b->StreamMesh = 2;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh, BITMAP_CHROME);
        VectorCopy(Light, b->BodyLight);
        b->StreamMesh = -1;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (draw.type == 3 || draw.type == 4)
    {
        VectorCopy(b->BodyLight, Light);
        Vector(1.f, 0.2f, 0.1f, b->BodyLight);
        b->StreamMesh = 1;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh, BITMAP_CHROME);
        VectorCopy(Light, b->BodyLight);
        b->StreamMesh = -1;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    return true;
}

using namespace SEASON3B;

bool GMNewTown::RenderObjectMesh(const ObjectDrawInput &input, BMD *pModel, bool ExtraMon)
{
    auto draw = input;
    const auto *pObject = input.source;
    if (!IsCurrentMap())
        return false;

    if (IsNewMap73_74())
        return empireGuardian4_.RenderObjectMesh(draw, pModel, ExtraMon);

    // ���
    if ((draw.type >= 5 && draw.type <= 14) || draw.type == 4 || draw.type == 129)
    {
        Mesh_t *m = NULL;
        for (int i = 0; i < pModel->NumMeshs; i++)
        {
            m = &pModel->Meshs[i];
            for (int j = 0; j < m->NumNormals; j++)
            {
                IntensityTransform[i][j] = 2.0f;
            }
        }
        Vector(1.0f, 1.0f, 1.0f, draw.light);
    }

    if (draw.type == 53 || draw.type == 55 || draw.type == 110 || draw.type == 89 ||
        draw.type == 78 || draw.type == 79 || draw.type == 125 || draw.type == 128 ||
        draw.type == 2)
    {
    }
    else if (draw.type == 73)
    {
        pModel->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                           draw.blendV, draw.hiddenMesh);
        pModel->RenderBody(RENDER_BRIGHT | RENDER_CHROME, 0.5f, draw.blendMesh, draw.blendLight,
                           draw.blendU, draw.blendV, draw.hiddenMesh);
    }
    else if (draw.type == 104)
    {
        pModel->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                           draw.blendV, draw.hiddenMesh);
        float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;
        pModel->RenderMesh(4, RENDER_BRIGHT, draw.alpha, 4, fLumi);
    }
    else if (draw.type == 113)
    {
        pModel->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                           draw.blendV, draw.hiddenMesh);
        pModel->RenderMesh(9, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                           draw.blendLight);
    }
    else if (draw.type == 114)
    {
        pModel->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                           draw.blendV, draw.hiddenMesh);
        pModel->RenderMesh(9, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                           draw.blendLight);
    }
    else if (draw.type == 121)
    {
        pModel->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
        pModel->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
        pModel->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
        float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;
        pModel->RenderMesh(1, RENDER_BRIGHT, draw.alpha, 1, fLumi);
        fLumi = (sinf(WorldTime * 0.0015f) + 1.0f) * 0.1f + 0.1f;
        pModel->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, fLumi, 0, fLumi);
    }
    // NPC
    else if (draw.type == MODEL_ELBELAND_MARCE)
    {
        pModel->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
        pModel->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
        pModel->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                           draw.blendLight);
    }
    else if (draw.type == MODEL_HIDEOUS_RABBIT)
    {
        pModel->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
        pModel->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
    }
    else if (draw.type == MODEL_TOTEM_GOLEM && draw.action == MONSTER01_DIE)
    {
    }
    else
        return false;

    return true;
}

void GMNewTown::RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *pModel, bool ExtraMon0)
{
    const auto &draw = input;
    const auto *pObject = input.source;
    if (!IsCurrentMap())
        return;

    if (IsNewMap73_74())
    {
        empireGuardian4_.RenderAfterObjectMesh(draw, pModel, ExtraMon0);
        return;
    }

    if (draw.type == 2 || draw.type == 53 || draw.type == 55 || draw.type == 89 ||
        draw.type == 125 || draw.type == 128) // ������1,2, ����, ȸ����, ��
    {
        pModel->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                           draw.blendV, draw.hiddenMesh);
    }
    else if (draw.type == 110)
    {
        pModel->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME2, draw.alpha,
                           draw.blendMesh, draw.blendLight);
        pModel->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                           draw.blendLight);
        pModel->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                           draw.blendLight);
    }
    else if (draw.type == 78)
    {
        pModel->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           draw.blendU, draw.blendV);
        pModel->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                           draw.blendLight, draw.blendU, draw.blendV);
        vec3_t p, Position, Light;
        for (int i = 1; i <= 4; ++i)
        {
            Vector(0.0f, 0.0f, 0.0f, p);
            pModel->TransformPosition(draw.bones[i], p, Position, false);
            Vector(0.1f, 0.1f, 0.3f, Light);
            CreateSprite(BITMAP_SPARK + 1, Position, 5.5f, Light, pObject);
        }
    }
    else if (draw.type == 79)
    {
        pModel->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           draw.blendU, draw.blendV);
        pModel->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                           draw.blendLight, draw.blendU, draw.blendV);

        vec3_t p, Position, Light;
        for (int i = 1; i <= 6; ++i)
        {
            Vector(0.0f, 0.0f, 0.0f, p);
            pModel->TransformPosition(draw.bones[i], p, Position, false);
            Vector(0.1f, 0.1f, 0.3f, Light);
            CreateSprite(BITMAP_SPARK + 1, Position, 5.5f, Light, pObject);
        }
    }
}

bool GMNewTown::IsEarlyAfterCharacterObject(const OBJECT &object) const
{
    return gMapManager.ContextMap() == WD_51HOME_6TH_CHAR && object.Type == 89;
}

void GMNewTown::RenderEarlyAfterCharacterObjects(OBJECT *head)
{
    for (OBJECT *object = head; object; object = object->Next)
        if (IsEarlyAfterCharacterObject(*object) &&
            TestFrustrum2D(object->Position[0] * 0.01f, object->Position[1] * 0.01f, -400.f))
            RenderObject_AfterCharacter(object);
}

bool GMNoria::RenderWholeObject(const ObjectDrawInput &input, BMD *b, bool)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (!(draw.type == MODEL_WARP3))
        return false;
    b->BodyLight[0] = 0.8f;
    b->BodyLight[1] = 0.8f;
    b->BodyLight[2] = 0.8f;

    b->StreamMesh = 0;
    b->RenderMesh(0, RENDER_DARK, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                  draw.blendV);
    b->StreamMesh = -1;
    return true;
}

bool CGMSantaTown::RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (IsSantaTown() == false)
        return false;

    switch (draw.type)
    {
    case 16: {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        Vector(0.0f, 0.5f, 0.5f, b->BodyLight);
        b->RenderMesh(2, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);

        return true;
    }
    }

    return false;
}

bool GMTarkan::RenderWholeObject(const ObjectDrawInput &input, BMD *b, bool)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (!(draw.type == 81))
        return false;
    b->BeginRender(draw.alpha);
    b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                  draw.blendV);
    b->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                  draw.blendU, draw.blendV, BITMAP_CHROME);
    b->EndRender();
    return true;
}

void GMTarkan::RenderAtmosphere()
{
    EnableAlphaTest();
    glColor4f(1.f, 1.f, 1.f, 0.5f);
    EnableAlphaBlend();
    glColor3f(0.3f, 0.3f, 0.25f);
    float WindX = (float)((int)WorldTime % 100000) * 0.0002f;
    RenderBitmapUV(BITMAP_CHROME + 2, 0.f, 0.f, (float)REFERENCE_WIDTH,
                   (float)REFERENCE_HEIGHT - 45.f, WindX, 0.f, 0.3f, 0.3f);
    float WindX2 = (float)((int)WorldTime % 100000) * 0.001f;
    RenderBitmapUV(BITMAP_CHROME + 3, 0.f, 0.f, (float)REFERENCE_WIDTH,
                   (float)REFERENCE_HEIGHT - 45.f, WindX2, 0.f, 3.f, 2.f);
}

bool GMUnitedMarketPlace::RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (IsUnitedMarketPlace() == false)
    {
        return false;
    }

    switch (draw.type)
    {
    case 8:
    case 30:

        return true;
    }

    return false;
}

bool GMUnitedMarketPlace::RenderMonster(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    switch (draw.type)
    {
    case MODEL_STATUE: {
        if (draw.action != MONSTER01_DIE)
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
    }
        return true;
    }

    return false;
}

void GMUnitedMarketPlace::RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (IsUnitedMarketPlace() == false)
        return;

    switch (draw.type)
    {
    case 8: {
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
        b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
        b->RenderMesh(4, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
        float fFlow_u = sinf(WorldTime * 0.0007f) * 0.05f;
        float fFlow_v = sinf(WorldTime * 0.001f) * 0.05f;
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, 0.3f, draw.blendU,
                      -(int)WorldTime % 10000 * 0.001f);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, 0.3f, fFlow_u, fFlow_v);
    }
    break;
    case 30: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    break;
    }
}

bool GMKanturu1st::RenderKanturu1stObjectVisual(const ObjectDrawInput &input, BMD *pModel)
{
    const auto &draw = input;
    const auto *pObject = input.source;
    if (!(IsKanturu1st() || gmArea_.IsGmArea()))
        return false;
    switch (draw.type)
    {
    case 85:
        pModel->BeginRender(draw.alpha);
        pModel->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                           draw.blendLight, draw.blendU, draw.blendV);
        pModel->RenderMesh(3, RENDER_CHROME | RENDER_DARK, draw.alpha, draw.blendMesh,
                           draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);
        pModel->EndRender();
        break;
    case 96:
        pModel->StreamMesh = 0;
        glAlphaFunc(GL_GREATER, 0.0f);
        pModel->RenderMesh(0, RENDER_TEXTURE, 1.0f, draw.blendMesh, draw.blendLight, draw.blendU,
                           -(int)WorldTime % 20000 * 0.00005f);
        glAlphaFunc(GL_GREATER, 0.25f);
        pModel->StreamMesh = -1;
        break;
    }
    return true;
}

bool GMKanturu1st::RenderKanturu1stObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    auto draw = input;
    const auto *o = input.source;
    if (IsKanturu1st() || gmArea_.IsGmArea())
    {
        switch (draw.type)
        {
        case 103: {
            float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;
            draw.hiddenMesh = 1;
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, 1, fLumi, draw.blendU, draw.blendV);

            return true;
        }

        case 76:
        case 77:
        case 91:
        case 92:
        case 95:
        case 105:

            return true;
        }
    }

    return false;
}

void GMKanturu1st::RenderKanturu1stAfterObjectMesh(const ObjectDrawInput &input, BMD *b)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (!(IsKanturu1st() || gmArea_.IsGmArea()))
        return;

    switch (draw.type)
    {
    case 76:
        b->BodyLight[0] = 0.52f;
        b->BodyLight[1] = 0.52f;
        b->BodyLight[2] = 0.52f;

        b->StreamMesh = 0;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, -(int)WorldTime % 100000 * 0.00001f, draw.blendV);
        b->StreamMesh = -1;
        break;

    case 95: {
        b->BeginRender(draw.alpha);

        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(0, RENDER_CHROME7 | RENDER_DARK, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, BITMAP_CHROME);
        b->EndRender();
        vec3_t Light, p, Position;
        for (int i = 0; i < 10; ++i)
        {
            Vector(0.0f, 0.0f, 0.0f, p);
            b->TransformPosition(draw.bones[i], p, Position, false);
            Vector(0.1f, 0.1f, 0.3f, Light);
            CreateSprite(BITMAP_SPARK + 1, Position, 7.5f, Light, o);
        }
    }
    break;

    case 105:
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, BITMAP_CHROME);
        break;

    case 77:
    case 91:
    case 92:
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        break;
    }
}

bool GMKanturu1st::RenderKanturu1stMonsterObjectMesh(const ObjectDrawInput &input, BMD *b,
                                                     int ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    switch (draw.type)
    {
    case MODEL_BERSERK: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);

        if (draw.action == MONSTER01_DIE)
            return true;

        float fLumi = sinf(WorldTime * 0.002f) + 1.0f;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_BERSERK_EFFECT);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, fLumi, draw.blendU,
                      draw.blendV, BITMAP_BERSERK_WP_EFFECT);

        return true;
    }
    break;
    case MODEL_GIGANTIS: {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        if (draw.action != MONSTER01_DIE)
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);

        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        return true;
    }
    break;
    case MODEL_GENOCIDER: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        return true;
    }
    break;
    case MODEL_SPLINTER_WOLF: {
        float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        b->RenderMesh(0, RENDER_TEXTURE);
        b->RenderMesh(1, RENDER_TEXTURE);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 2, fLumi);
        b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 3, fLumi);
        b->RenderMesh(4, RENDER_TEXTURE);
        return true;
    }
    break;

    case MODEL_IRON_RIDER: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        return true;
    }
    break;
    case MODEL_BLADE_HUNTER: {
        if (draw.action != MONSTER01_DIE)
        {
            float fLumi = sinf(WorldTime * 0.002f) + 1.0f;
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                          draw.blendV, BITMAP_BLADEHUNTER_EFFECT);
        }
        else
        {

            if (draw.alpha > 0.8f)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);
            b->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);
        }
        return true;
    }
    break;
    case MODEL_SATYROS: {
        vec3_t Light;
        VectorCopy(b->BodyLight, Light);
        b->BeginRender(1.f);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->StreamMesh = 1;
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, 1.f, 0, draw.blendLight, draw.blendU,
                      WorldTime * 0.0005f);
        b->RenderMesh(3, RENDER_CHROME | RENDER_BRIGHT, 1.f, 0, draw.blendLight, draw.blendU,
                      WorldTime * 0.001f, BITMAP_CHROME);

        float Luminosity = sinf(WorldTime * 0.001f) * 0.5f + 0.5f;
        Vector(Light[0] * Luminosity, Light[0] * Luminosity, Light[0] * Luminosity, b->BodyLight);
        b->RenderMesh(4, RENDER_TEXTURE | RENDER_BRIGHT, 1.f, 2, draw.blendLight,
                      WorldTime * 0.0001f, -WorldTime * 0.0005f);
        b->EndRender();
        return true;
    }
    break;
    case MODEL_KENTAUROS: {
        float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, fLumi);
        b->RenderMesh(1, RENDER_CHROME2 | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        return true;
    }
    break;
    case MODEL_BERSERKER_WARRIOR: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);

        if (draw.action == MONSTER01_DIE)
            return true;

        float fLumi = sinf(WorldTime * 0.002f) + 1.0f;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_BERSERK_EFFECT);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, fLumi, draw.blendU,
                      draw.blendV, BITMAP_BERSERK_WP_EFFECT);

        return true;
    }
    break;
    case MODEL_KENTAUROS_WARRIOR: {
        float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, fLumi);
        b->RenderMesh(1, RENDER_CHROME2 | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        return true;
    }
    break;
    case MODEL_GIGANTIS_WARRIOR: {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        if (draw.action != MONSTER01_DIE)
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);

        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        return true;
    }
    break;
    case MODEL_SOCCERBALL: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        return true;
    }
    break;
    }

    return false;
}

bool GMKanturu1st::RenderKanturu1stMonsterVisual(const CHARACTER *c, const ObjectDrawInput &input,
                                                 BMD *b)
{
    const auto &draw = input;
    const auto *o = input.source;
    switch (draw.type)
    {
    case MODEL_BLADE_HUNTER:
        if (gMapManager.ContextMap() == WD_39KANTURU_3RD &&
            g_Direction.m_CKanturu.m_iKanturuState == KANTURU_STATE_MAYA_BATTLE)
        {
            vec3_t Light;
            float Scale, Angle;

            Scale = o->Distance;

            Angle = (timeGetTime() % 9000) / 10.0f;

            Light[0] = 0.6f - (Scale / 5.0f);
            Light[1] = 0.6f - (Scale / 5.0f);
            Light[2] = 0.6f - (Scale / 5.0f);

            EnableAlphaBlend();
            RenderTerrainAlphaBitmap(BITMAP_ENERGY_RING, draw.position[0], draw.position[1], Scale,
                                     Scale, Light, Angle);

            Vector(0.5f, 0.5f, 0.5f, Light);
            RenderTerrainAlphaBitmap(BITMAP_ENERGY_FIELD, draw.position[0], draw.position[1], 2.0f,
                                     2.0f, Light, Angle);
        }
        return true;
    }
    return false;
}

bool GMKanturu1st::RenderObjectMesh(const ObjectDrawInput &input, BMD *model, bool extraMonster)
{
    const auto &draw = input;
    const auto *object = input.source;
    return RenderKanturu1stObjectMesh(draw, model);
}

bool GMKanturu1st::RenderObjectVisual(const ObjectDrawInput &input, BMD *model)
{
    const auto &draw = input;
    const auto *object = input.source;
    return RenderKanturu1stObjectVisual(draw, model);
}

void GMKanturu1st::RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *model,
                                         bool extraMonster)
{
    const auto &draw = input;
    const auto *object = input.source;
    RenderKanturu1stAfterObjectMesh(draw, model);
}

bool GMKanturu2nd::Render_Kanturu2nd_ObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    auto draw = input;
    const auto *o = input.source;
    if (Is_Kanturu2nd() == true)
    {
        switch (draw.type)
        {
        case 1: {
            float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;
            draw.hiddenMesh = 1;
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, 1, fLumi, draw.blendU, draw.blendV);

            return true;
        }
        break;
        case 2: {
            float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;
            draw.hiddenMesh = 1;
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, 1, fLumi, draw.blendU, draw.blendV);

            return true;
        }
        break;
        case 12: {
            draw.hiddenMesh = 0;
            b->StreamMesh = 1;
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          -(int)WorldTime % 2000 * 0.0005f, draw.hiddenMesh);
            b->StreamMesh = -1;
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, 0, draw.blendLight,
                          (int)WorldTime % 2000 * 0.0005f, (int)WorldTime % 2000 * 0.0005f);
            b->RenderMesh(0, RENDER_CHROME | RENDER_DARK, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);

            return true;
        }
        break;
        case 13: {
            b->StreamMesh = 1;
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          -(int)WorldTime % 2000 * 0.0005f, draw.hiddenMesh);
            b->StreamMesh = -1;

            return true;
        }
        break;
        case 14: {
            b->StreamMesh = 1;
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          -(int)WorldTime % 2000 * 0.0005f, draw.hiddenMesh);
            b->StreamMesh = -1;

            return true;
        }
        break;
        case 15:
        case 16: {
            b->StreamMesh = 3;
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          -(int)WorldTime % 2000 * 0.0005f, draw.hiddenMesh);
            b->StreamMesh = -1;

            return true;
        }
        break;
        case 27: {
            draw.hiddenMesh = 2;
            b->StreamMesh = 4;
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          -(int)WorldTime % 2000 * 0.0005f, draw.hiddenMesh);
            b->StreamMesh = -1;
            b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);

            return true;
        }
        break;
        case 8:
        case 10:
        case 31:
        case 33:
        case 35:
        case 36:
        case 59:
        case 76:
        case 80: {

            return true;
        }
        break;
        }
    }

    return false;
}

void GMKanturu2nd::Render_Kanturu2nd_AfterObjectMesh(const ObjectDrawInput &input, BMD *b)
{
    auto draw = input;
    const auto *o = input.source;
    if (false == Is_Kanturu2nd())
        return;

    if (draw.type == 31)
    {
        draw.hiddenMesh = 0;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (draw.type == 8)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, BITMAP_CHROME);
    }
    else if (draw.type == 10)
    {
        draw.hiddenMesh = 2;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(2, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, BITMAP_CHROME);
    }
    else if (draw.type == 35 || draw.type == 36)
    {
        draw.hiddenMesh = 1;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (draw.type == 76)
    {
        b->BeginRender(draw.alpha);

        b->BodyLight[0] = 0.4f;
        b->BodyLight[1] = 0.4f;
        b->BodyLight[2] = 0.4f;

        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(0, RENDER_CHROME7 | RENDER_DARK, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, BITMAP_CHROME);
        b->EndRender();

        vec3_t Light, p, Position;
        for (int i = 0; i < 10; ++i)
        {
            Vector(0.0f, 0.0f, 0.0f, p);
            b->TransformPosition(draw.bones[i], p, Position, false);
            Vector(0.1f, 0.1f, 0.3f, Light);
            CreateSprite(BITMAP_SPARK + 1, Position, 7.5f, Light, o);
        }
    }
    else if (draw.type == 33 || draw.type == 59 || draw.type == 80)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
}

bool GMKanturu2nd::Render_Kanturu2nd_MonsterObjectMesh(const ObjectDrawInput &input, BMD *b,
                                                       int ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    switch (draw.type)
    {
    case MODEL_PERSONA: {
        float fLumi2 = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi2, draw.blendU,
                      draw.blendV, BITMAP_PRSONA_EFFECT);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 2, fLumi2, draw.blendU,
                      draw.blendV, BITMAP_PRSONA_EFFECT2);
        return true;
    }
    break;
    case MODEL_TWIN_TAIL: {
        float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;
        float fLumi2 = (sinf(WorldTime * 0.002f) + 1.f);

        if (draw.action != MONSTER01_DIE)
        {
            Vector(0.9f, 0.9f, 1.0f, b->BodyLight);
        }
        else
        {
            Vector(0.3f, 1.0f, 0.2f, b->BodyLight);
        }

        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);

        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 2, fLumi2, draw.blendU,
                      draw.blendV, BITMAP_TWINTAIL_EFFECT);

        b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 3, fLumi);
        return true;
    }
    break;
    case MODEL_DREADFEAR: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        return true;
    }
    break;
    case MODEL_KANTURU2ND_ENTER_NPC: {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        b->RenderMesh(1, RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, BITMAP_CHROME);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, BITMAP_KANTURU_2ND_NPC3);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        b->RenderMesh(2, RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, BITMAP_CHROME);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, BITMAP_KANTURU_2ND_NPC2);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, 3, draw.blendLight, draw.blendU,
                      -WorldTime * 0.0003f, BITMAP_CHROME);
        b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, BITMAP_KANTURU_2ND_NPC1);
        b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        return true;
    }
    break;
    case MODEL_TRAP_CANON: {
        RenderTrapCanonObject(draw, b);
        return true;
    }
    break;
    }

    return false;
}

void GMKanturu2nd::RenderTrapCanonObject(const ObjectDrawInput &input, BMD *b)
{
    const auto &draw = input;
    const auto *o = input.source;
    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                  draw.blendV);
    float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;
    b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 2, fLumi, draw.blendU,
                  draw.blendV);
}

bool GMKanturu2nd::Render_Kanturu2nd_MonsterVisual(const CHARACTER *c, const ObjectDrawInput &input,
                                                   BMD *b)
{
    const auto &draw = input;
    const auto *o = input.source;
    switch (draw.type)
    {
    case MODEL_TWIN_TAIL:
        if (gMapManager.ContextMap() == WD_39KANTURU_3RD &&
            g_Direction.m_CKanturu.m_iKanturuState == KANTURU_STATE_MAYA_BATTLE)
        {
            vec3_t Light;
            float Scale, Angle;

            Scale = o->Distance;

            Angle = (timeGetTime() % 9000) / 10.0f;

            Light[0] = 0.6f - (Scale / 5.0f);
            Light[1] = 0.6f - (Scale / 5.0f);
            Light[2] = 0.0f;

            EnableAlphaBlend();
            RenderTerrainAlphaBitmap(BITMAP_ENERGY_RING, draw.position[0], draw.position[1], Scale,
                                     Scale, Light, Angle);

            Vector(0.5f, 0.5f, 0.1f, Light);
            RenderTerrainAlphaBitmap(BITMAP_ENERGY_FIELD, draw.position[0], draw.position[1], 2.0f,
                                     2.0f, Light, Angle);
        }
        return true;
    case MODEL_DREADFEAR:
        if (gMapManager.ContextMap() == WD_39KANTURU_3RD &&
            g_Direction.m_CKanturu.m_iKanturuState == KANTURU_STATE_MAYA_BATTLE)
        {
            vec3_t Light;
            float Scale, Angle;

            Scale = o->Distance;

            Angle = (timeGetTime() % 9000) / 10.0f;

            Light[0] = 1.0f - (Scale / 3.0f);
            Light[1] = 0.0f;
            Light[2] = 0.0f;

            EnableAlphaBlend();
            RenderTerrainAlphaBitmap(BITMAP_ENERGY_RING, draw.position[0], draw.position[1], Scale,
                                     Scale, Light, Angle);

            Vector(1.0f, 0.3f, 0.1f, Light);
            RenderTerrainAlphaBitmap(BITMAP_ENERGY_FIELD, draw.position[0], draw.position[1], 2.0f,
                                     2.0f, Light, Angle);
        }
        return true;
    }
    return false;
}

bool GMKanturu2nd::RenderObjectMesh(const ObjectDrawInput &input, BMD *model, bool extraMonster)
{
    const auto &draw = input;
    const auto *object = input.source;
    return Render_Kanturu2nd_ObjectMesh(draw, model);
}

void GMKanturu2nd::RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *model,
                                         bool extraMonster)
{
    const auto &draw = input;
    const auto *object = input.source;
    Render_Kanturu2nd_AfterObjectMesh(draw, model);
}

bool GMKanturu3rd::RenderKanturu3rdObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (IsInKanturu3rd())
    {
        switch (draw.type)
        {
        case 0: // 마야
        {
            if (g_Direction.m_CKanturu.GetMayaExplotion())
                VectorCopy(draw.light, b->BodyLight);

            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(4, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(5, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(6, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(7, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(8, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);

            if (g_Direction.m_CKanturu.GetMayaExplotion())
            {
                VectorCopy(draw.light, b->BodyLight);
            }
            else
            {
                Vector(1.0f, 1.0f, 1.0f, b->BodyLight);
            }
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_CHROME, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);

            VectorCopy(o->StartPosition, b->BodyLight);

            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, BITMAP_MAYA_BODY);
            if (g_Direction.m_CKanturu.GetMayaExplotion())
            {
                VectorCopy(draw.light, b->BodyLight);
            }
            else
            {
                Vector(1.0f, 1.0f, 1.0f, b->BodyLight);
            }
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
            return true;
        case 2: {
            b->RenderMesh(0, RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV, BITMAP_CHROME2);
            Vector(0.3f, 0.3f, 0.3f, b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
        }
            return true;
        case 15:
        case 16:
        case 17: {
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, 1, draw.blendLight, draw.blendU,
                          -(int)WorldTime % 10000 * 0.0005f);
        }
            return true;
        case 33: {
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, draw.blendLight,
                          draw.blendU, -(int)WorldTime % 2000 * 0.0005f);
        }
            return true;
        case 40:
        case 41:
        case 42: {
            Vector(0.6f, 0.7f, 1.f, b->BodyLight);
            b->RenderMesh(1, RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            Vector(0.7f, 0.6, 0.7f, b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                          draw.blendU, -(int)WorldTime % 200000 * 0.00001f);
        }
            return true;
        case 43: {
            vec3_t Light;
            VectorCopy(b->BodyLight, Light);
            b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            Vector(1.0f, 1.0f, 1.0f, b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                          draw.blendU, -0.1f + (int)WorldTime % 5000 * 0.0001f,
                          BITMAP_KANTURU3RD_OBJECT);
            VectorCopy(Light, b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->BodyLight[0] = sinf(WorldTime * 0.0015f) * 0.2f + 0.4f;
            b->BodyLight[1] = sinf(WorldTime * 0.0015f) * 0.2f + 0.4f;
            b->BodyLight[2] = sinf(WorldTime * 0.0015f) * 0.2f + 0.4f;
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
            return true;
        case MODEL_SMELTING_NPC: {
            Vector(1.f, 1.f, 1.f, b->BodyLight);
            b->RenderMesh(0, RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
            Vector(10.f, 10.f, 10.f, b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 3, draw.blendLight,
                          draw.blendU, (int)WorldTime % 2000 * 0.001f);
        }
            return true;
        case MODEL_MAYASTAR: {
            VectorCopy(draw.light, b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
            Vector(1.f, 1.f, 1.f, b->BodyLight);
        }
            return true;
        case MODEL_MAYASTONE1: {
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
        case MODEL_MAYASTONE2:
        case MODEL_MAYASTONE3:
        case MODEL_MAYASTONE4:
        case MODEL_MAYASTONE5:
        case MODEL_MAYASTONEFIRE: {
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
        }
            return true;
        case 8:
        case 10:
        case 19:
        case 20:
        case 21:
        case 24:
        case 25:
        case 73: {
        }
            return true;
        }
    }

    return false;
}

void GMKanturu3rd::RenderKanturu3rdAfterObjectMesh(const ObjectDrawInput &input, BMD *b,
                                                   bool ExtraMon)
{
    auto draw = input;
    const auto *o = input.source;
    if (IsInKanturu3rd())
    {
        switch (draw.type)
        {
        case 8: {
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(4, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(5, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
        }
        break;
        case 10: {
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(4, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(5, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(6, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(7, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
        }
        break;
        case 19: {
            draw.hiddenMesh = 7;
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
            draw.hiddenMesh = -1;

            b->RenderMesh(7, RENDER_TEXTURE, draw.alpha, 7, draw.blendLight, draw.blendU,
                          -(int)WorldTime % 10000 * 0.0001f);
        }
        break;
        case 20: {
            draw.hiddenMesh = 6;
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
            draw.hiddenMesh = -1;

            b->RenderMesh(6, RENDER_TEXTURE, draw.alpha, 6, draw.blendLight, draw.blendU,
                          -(int)WorldTime % 10000 * 0.0001f);
        }
        break;
        case 21: {
            b->BodyLight[0] = sinf(WorldTime * 0.002f) * 0.2f + 0.5f;
            b->BodyLight[1] = sinf(WorldTime * 0.002f) * 0.2f + 0.5f;
            b->BodyLight[2] = sinf(WorldTime * 0.002f) * 0.2f + 0.5f;
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
        }
        break;
        case 24:
        case 25: {
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
        }
        break;
        case 73: {
            if (!KanturuSuccessMap)
            {
                b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                              draw.blendU, (int)WorldTime % 10000 * 0.0002f);
                b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                              (int)WorldTime % 10000 * 0.0002f, draw.blendV);
            }
        }
        break;
        case MODEL_STORM3: {
            VectorCopy(draw.light, b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
            b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
            Vector(1.f, 1.f, 1.f, b->BodyLight);
        }
        break;
        case MODEL_MAYAHANDSKILL: {
            VectorCopy(draw.light, b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                          draw.blendU, -(int)WorldTime % 2000 * 0.001f);
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, draw.blendLight,
                          -(int)WorldTime % 2000 * 0.001f, draw.blendV);
        }
        break;
        }
    }
}

bool GMKanturu3rd::RenderKanturu3rdMonsterObjectMesh(const ObjectDrawInput &input, BMD *b,
                                                     bool ExtraMon)
{
    auto draw = input;
    const auto *o = input.source;
    switch (draw.type)
    {
    case MODEL_PERSONA: {
        float fLumi2 = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi2, draw.blendU,
                      draw.blendV, BITMAP_PRSONA_EFFECT);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 2, fLumi2, draw.blendU,
                      draw.blendV, BITMAP_PRSONA_EFFECT2);
        return true;
    }
    break;
    case MODEL_TWIN_TAIL: {
        float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;
        float fLumi2 = (sinf(WorldTime * 0.002f) + 1.f);

        if (draw.action != MONSTER01_DIE)
        {
            Vector(0.9f, 0.9f, 1.0f, b->BodyLight);
        }
        else
        {
            Vector(0.3f, 1.0f, 0.2f, b->BodyLight);
        }

        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 2, fLumi2, draw.blendU,
                      draw.blendV, BITMAP_TWINTAIL_EFFECT);
        b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 3, fLumi);
        return true;
    }
    break;
    case MODEL_DREADFEAR: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        return true;
    }
    break;
    case MODEL_DARK_SKULL_SOLDIER_5: {
        b->BeginRender(1.f);

        b->BodyLight[0] = 1.0f;
        b->BodyLight[1] = 1.0f;
        b->BodyLight[2] = 1.0f;

        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        float fLumi = (sinf(WorldTime * 0.003f) + 1.f) * 0.35f;

        b->BodyLight[0] = fLumi;
        b->BodyLight[1] = fLumi;
        b->BodyLight[2] = fLumi;

        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, BITMAP_NIGHTMARE_EFFECT2);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, BITMAP_NIGHTMARE_EFFECT1);

        if (draw.action == MONSTER01_ATTACK1 || draw.action == MONSTER01_ATTACK2)
        {
            b->BodyLight[0] = 0.7f;
            b->BodyLight[1] = 0.7f;
            b->BodyLight[2] = 1.0f;

            b->RenderMesh(2, RENDER_BRIGHT | RENDER_CHROME2, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
            b->RenderMesh(3, RENDER_BRIGHT | RENDER_CHROME2, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
        }

        b->EndRender();
    }
        return true;
    case MODEL_MAYA_HAND_LEFT: {
        b->BeginRender(1.f);

        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, BITMAP_CHROME);

        b->EndRender();
    }
        return true;
    case MODEL_MAYA_HAND_RIGHT: {
        b->BeginRender(1.f);

        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);

        b->EndRender();
    }
        return true;
    case MODEL_SUMMON:
    case MODEL_STORM2: {
        VectorCopy(draw.light, b->BodyLight)
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        Vector(1.f, 1.f, 1.f, b->BodyLight);
    }
        return true;
    }
    return false;
}

void GMKanturu3rd::RenderKanturu3rdinterface()
{
    if (!IsInKanturu3rd() || iKanturuResult == -1)
    {
        return;
    }

    RenderKanturu3rdResultInterface();
}

void GMKanturu3rd::RenderKanturu3rdResultInterface()
{
    if (iKanturuResult == 1)
        Kanturu3rdSuccess();
    else if (iKanturuResult == 0)
        Kanturu3rdFailed();
}

bool GMKanturu3rd::RenderObjectMesh(const ObjectDrawInput &input, BMD *model, bool extraMonster)
{
    const auto &draw = input;
    const auto *object = input.source;
    return RenderKanturu3rdObjectMesh(draw, model, extraMonster);
}

void GMKanturu3rd::RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *model,
                                         bool extraMonster)
{
    const auto &draw = input;
    const auto *object = input.source;
    RenderKanturu3rdAfterObjectMesh(draw, model, extraMonster);
}

void GMKanturu3rd::RenderEarlyAfterCharacterObjects(OBJECT *head)
{
    if (!g_Direction.m_CKanturu.IsMayaScene(gMapManager.ContextMap()))
        return;
    for (OBJECT *object = head; object; object = object->Next)
        if (object->Type == MODEL_STORM3)
            RenderObject_AfterCharacter(object);
}

bool GMKanturu3rd::RenderMonsterVisual(const CHARACTER *character, const ObjectDrawInput &input,
                                       BMD *model)
{
    const auto &draw = input;
    const auto *object = input.source;
    if (draw.type == MODEL_BLADE_HUNTER)
        return TheMapProcess().Kanturu1st().RenderKanturu1stMonsterVisual(character, draw, model);
    if (draw.type == MODEL_TWIN_TAIL || draw.type == MODEL_DREADFEAR)
        return TheMapProcess().Kanturu2nd().Render_Kanturu2nd_MonsterVisual(character, draw, model);
    return false;
}

bool GMKanturu3rd::TerrainCutscene() const
{
    return g_Direction.m_CKanturu.IsMayaScene(gMapManager.ContextMap());
}

float GMKanturu3rd::ItemDrawHeight(const OBJECT &object, int index)
{
    return object.Position[2] +
           (TerrainCutscene() ? 10.f * sinf(float(index * 1237 + WorldTime) * 0.002f) : 0.f);
}

void GMKanturu3rd::RenderMapInterface()
{
    RenderKanturu3rdinterface();
}

void MapProcess::RenderAtmosphere()
{
    if (BaseMap *const map = ContextBehavior())
        map->RenderAtmosphere();
    RenderFrontSideVisual();
}

void MapProcess::BeginSceneRender()
{
    if (BaseMap *const map = ContextBehavior())
        map->BeginSceneRender();
}

void MapProcess::EndSceneRender()
{
    if (BaseMap *const map = ContextBehavior())
        map->EndSceneRender();
}

void MapProcess::RenderMapInterface()
{
    if (BaseMap *const map = ContextBehavior())
        map->RenderMapInterface();
}

void MapProcess::RenderEarlyAfterCharacterObjects(OBJECT *head)
{
    if (BaseMap *const map = ContextBehavior())
        map->RenderEarlyAfterCharacterObjects(head);
}

bool MapProcess::IsEarlyAfterCharacterObject(const OBJECT &object) const
{
    BaseMap *const map = ContextBehavior();
    return map && map->IsEarlyAfterCharacterObject(object);
}

bool MapProcess::WeatherSpritePass() const
{
    return Presentation().weatherSpritePass && WeatherEnabled();
}

bool MapProcess::WeatherWaterPass() const
{
    return Presentation().weatherWaterPass && WeatherEnabled();
}

bool MapProcess::RenderWholeObject(const ObjectDrawInput &input, BMD *model, bool extraMonster)
{
    const auto &draw = input;
    const auto *object = input.source;
    BaseMap *const map = ContextBehavior();
    return map != nullptr && map->RenderWholeObject(draw, model, extraMonster);
}

void MapProcess::RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (BaseMap *const map = ContextBehavior(); map != nullptr)
    {
        map->RenderAfterObjectMesh(draw, b, ExtraMon);
    }
}

void MapProcess::RenderFrontSideVisual()
{
    if (BaseMap *const map = ContextBehavior(); map != nullptr)
    {
        map->RenderFrontSideVisual();
    }
}

bool MapProcess::RenderObjectVisual(const ObjectDrawInput &input, BMD *model)
{
    const auto &draw = input;
    const auto *object = input.source;
    BaseMap *const map = ContextBehavior();
    return map != nullptr && map->RenderObjectVisual(draw, model);
}

bool MapProcess::RenderMonsterVisual(const CHARACTER *c, const ObjectDrawInput &input, BMD *b)
{
    const auto &draw = input;
    const auto *o = input.source;
    BaseMap *const map = ContextBehavior();
    return map != nullptr && map->RenderMonsterVisual(c, draw, b);
}

bool CGMCryingWolf2nd::RenderCryingWolf2ndObjectMesh(const ObjectDrawInput &input, BMD *pModel)
{
    const auto &draw = input;
    const auto *pObject = input.source;
    if (!IsCyringWolf2nd())
        return false;

    return RenderCryingWolf2ndMonsterObjectMesh(draw, pModel);
}

bool CGMCryingWolf2nd::RenderCryingWolf2ndMonsterObjectMesh(const ObjectDrawInput &input,
                                                            BMD *pModel)
{
    const auto &draw = input;
    const auto *pObject = input.source;
    return false;
}

bool CGMCryingWolf2nd::RenderObjectMesh(const ObjectDrawInput &input, BMD *model, bool extraMonster)
{
    const auto &draw = input;
    const auto *object = input.source;
    return RenderCryingWolf2ndObjectMesh(draw, model);
}

bool CGMHuntingGround::RenderHuntingGroundObjectMesh(const ObjectDrawInput &input, BMD *pModel,
                                                     bool ExtraMon)
{
    const auto &draw = input;
    const auto *pObject = input.source;
    if (IsInHuntingGround())
    {
        if (draw.type == 27 || draw.type == 54)
        {
            vec3_t LightBackup;
            VectorCopy(pModel->BodyLight, LightBackup); //. backup
            float Luminosity = sinf(pObject->Timer + WorldTime * 0.0012f) * 0.5f + 0.9f;
            pModel->BodyLight[0] *= Luminosity;
            pModel->BodyLight[1] *= Luminosity;
            pModel->BodyLight[2] *= Luminosity;
            pModel->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                               draw.blendU, draw.blendV, draw.hiddenMesh);

            VectorCopy(LightBackup, pModel->BodyLight); //. restore

            return true;
        }
        if (draw.type == 10)
        {
            pModel->BodyLight[0] = 0.56f;
            pModel->BodyLight[1] = 0.80f;
            pModel->BodyLight[2] = 0.81f;
            pModel->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                               draw.blendU, draw.blendV, draw.hiddenMesh);
            return true;
        }
        if (draw.type == 52)
        {
            float Luminosity = sinf(pObject->Timer + WorldTime * 0.0009f) * 0.5f + 0.9f;
            pModel->BodyLight[0] *= Luminosity;
            pModel->BodyLight[1] *= Luminosity;
            pModel->BodyLight[2] *= Luminosity;
            pModel->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                               draw.blendU, draw.blendV, draw.hiddenMesh);
        }
    }
    return false;
}

bool CGMHuntingGround::RenderHuntingGroundMonsterObjectMesh(const ObjectDrawInput &input,
                                                            BMD *pModel, bool ExtraMon)
{
    auto draw = input;
    const auto *pObject = input.source;
    switch (draw.type)
    {
    case MODEL_LIZARD_WARRIOR: {
        pModel->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                           draw.blendV, 1);
        pModel->BeginRender(1.f);
        vec3_t LightBackup;
        VectorCopy(pModel->BodyLight, LightBackup); //. backup
        Vector(0.6f, 0.4f, 0.4f, pModel->BodyLight);
        pModel->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           draw.blendU, draw.blendV);
        VectorCopy(LightBackup, pModel->BodyLight); //. restore
        pModel->EndRender();
        return true;
    }
    break;
    case MODEL_FIRE_GOLEM: {
        pModel->BeginRender(1.f);
        vec3_t LightBackup;
        VectorCopy(pModel->BodyLight, LightBackup); //. backup
        float Luminosity = sinf(WorldTime * 0.0012f) * 0.8f + 1.3f;
        if (Luminosity > 1.3f)
            Luminosity = 1.3f;

        pModel->BodyLight[0] *= Luminosity;
        pModel->BodyLight[1] *= Luminosity;
        pModel->BodyLight[2] *= Luminosity;

        pModel->StreamMesh = 0;
        pModel->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           (int)WorldTime % 10000 * 0.0002f, (int)WorldTime % 10000 * 0.0002f);
        pModel->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                           draw.blendLight, draw.blendU, draw.blendV);
        pModel->StreamMesh = -1;

        VectorCopy(LightBackup, pModel->BodyLight); //. restore

        pModel->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           draw.blendU, draw.blendV);
        pModel->EndRender();
        return true;
    }
    break;
    case MODEL_QUEEN_BEE: {
        pModel->BeginRender(1.f);

        pModel->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                           draw.blendV, 0);

        vec3_t LightBackup;
        VectorCopy(pModel->BodyLight, LightBackup); //. backup

        //Vector ( 0.7f, 0.7f, 0.7f, pModel->BodyLight );
        Vector(1.f, 1.f, 1.f, pModel->BodyLight);
        pModel->RenderMesh(0, RENDER_TEXTURE, draw.alpha, -1, 1.f, draw.blendU, draw.blendV);
        //Vector ( 0.75f, 0.65f, 0.5f, pModel->BodyLight );
        Vector(0.8f, 0.6f, 1.f, pModel->BodyLight);
        pModel->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, -1, 1.f, draw.blendU,
                           draw.blendV);

        VectorCopy(LightBackup, pModel->BodyLight); //. restore

        pModel->EndRender();
        return true;
    }
    break;
    case MODEL_POISON_GOLEM: {
        pModel->BeginRender(1.f);
        vec3_t LightBackup;
        if (ExtraMon)
        {
            Vector(1.f, 1.f, 1.f, pModel->BodyLight);
        }
        VectorCopy(pModel->BodyLight, LightBackup); //. backup
        float Luminosity = sinf(WorldTime * 0.0012f) * 0.8f + 1.3f;
        if (Luminosity > 1.3f)
            Luminosity = 1.3f;

        pModel->BodyLight[0] *= (Luminosity * 0.1f);
        pModel->BodyLight[1] *= Luminosity;
        pModel->BodyLight[2] *= (Luminosity * 0.1f);

        pModel->StreamMesh = 1;
        pModel->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           (int)WorldTime % 10000 * 0.0002f, (int)WorldTime % 10000 * 0.0002f);

        if (ExtraMon)
        {
            pModel->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                               draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);
        }
        else
            pModel->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                               draw.blendLight, draw.blendU, draw.blendV);

        pModel->StreamMesh = -1;

        VectorCopy(LightBackup, pModel->BodyLight); //. restore

        pModel->RenderMesh(0, RENDER_TEXTURE, sinf(WorldTime * 0.0012f) * 0.2f + 0.8f,
                           draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        if (ExtraMon)
        {
            pModel->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                               draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);
        }
        pModel->StreamMesh = 2;
        pModel->RenderMesh(2, RENDER_TEXTURE, draw.alpha, 2, 0.5f, draw.blendU, draw.blendV);
        if (ExtraMon)
        {
            pModel->RenderMesh(2, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                               draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);
            draw.scale = pModel->BodyScale;
        }
        pModel->StreamMesh = -1;
        pModel->EndRender();
        return true;
    }
    break;
    case MODEL_AXE_HERO: {
        pModel->BeginRender(1.f);
        vec3_t LightBackup;
        if (ExtraMon)
        {
            Vector(1.f, 1.f, 1.f, pModel->BodyLight);
        }
        VectorCopy(pModel->BodyLight, LightBackup); //. backup
        pModel->BodyLight[0] *= 0.5f;
        pModel->BodyLight[1] *= 0.6f;
        pModel->BodyLight[2] *= 0.8f;
        pModel->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           draw.blendU, draw.blendV);

        if (ExtraMon)
            pModel->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                               draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);

        Vector(0.5f, 0.8f, 0.6f, pModel->BodyLight);
        pModel->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           draw.blendU, draw.blendV);

        if (ExtraMon)
            pModel->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                               draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);

        VectorCopy(LightBackup, pModel->BodyLight); //. restore
        pModel->EndRender();
        return true;
    }
    break;
    case MODEL_EROHIM: {
        pModel->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                           draw.blendV, 5);

        pModel->BeginRender(1.f);

        constexpr float wingAlpha = 1.f;
        pModel->RenderMesh(5, RENDER_TEXTURE, wingAlpha, -1, draw.blendLight, draw.blendU,
                           draw.blendV, BITMAP_HGBOSS_WING);
        pModel->RenderMesh(5, RENDER_TEXTURE, wingAlpha, -1, draw.blendLight, draw.blendU,
                           draw.blendV);

        vec3_t LightBackup;
        VectorCopy(pModel->BodyLight, LightBackup); //. backup

        Vector(1.f, 0.5f, 0.f, pModel->BodyLight);
        pModel->RenderMesh(4, RENDER_CHROME | RENDER_BRIGHT, wingAlpha, draw.blendMesh,
                           draw.blendLight, draw.blendU, draw.blendV);

        Vector(0.75f, 0.65f, 0.5f, pModel->BodyLight);
        pModel->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, wingAlpha, draw.blendMesh,
                           draw.blendLight, draw.blendU, draw.blendV);
        pModel->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, wingAlpha, draw.blendMesh,
                           draw.blendLight, draw.blendU, draw.blendV);
        pModel->RenderMesh(3, RENDER_CHROME | RENDER_BRIGHT, wingAlpha, draw.blendMesh,
                           draw.blendLight, draw.blendU, draw.blendV);

        float Luminosity = sinf(WorldTime * 0.0012f) * 0.3f + 0.6f;
        pModel->RenderMesh(0, RENDER_TEXTURE, wingAlpha, 0, Luminosity, draw.blendU, draw.blendV,
                           BITMAP_HGBOSS_PATTERN);

        VectorCopy(LightBackup, pModel->BodyLight); //. restore

        pModel->EndRender();

        return true;
    }
    break;
    case MODEL_FISSURE: {
        pModel->BeginRender(1.f);

        Vector(1.f, 1.f, 1.f, pModel->BodyLight);
        pModel->RenderMesh(0, RENDER_TEXTURE, draw.alpha, -1, 1.f, draw.blendU, draw.blendV);
        pModel->RenderMesh(0, RENDER_TEXTURE, draw.alpha, 0, 1.f, draw.blendU, draw.blendV,
                           BITMAP_FISSURE_FIRE);

        pModel->EndRender();

        return true;
    }
    break;
    case MODEL_FISSURE_LIGHT: {
        pModel->BeginRender(1.f);

        Vector(1.f, 1.f, 1.f, pModel->BodyLight);

        draw.blendU = sinf(WorldTime * 0.00008f) * 2.5f;
        pModel->RenderMesh(0, RENDER_TEXTURE, draw.alpha, 0, 1.f, draw.blendU, draw.blendV);
        draw.blendU = 1.f;

        pModel->EndRender();
        return true;
    }
    break;
    }
    return false;
}

bool CGMHuntingGround::RenderObjectMesh(const ObjectDrawInput &input, BMD *model, bool extraMonster)
{
    const auto &draw = input;
    const auto *object = input.source;
    return RenderHuntingGroundObjectMesh(draw, model, extraMonster);
}

using namespace SEASON3C;

void GMSwampOfQuiet::RenderBaseSmoke()
{
    if (!IsCurrentMap())
        return;

    EnableAlphaTest();
    glColor3f(0.4f, 0.4f, 0.45f);
    float WindX2 = (float)((int)WorldTime % 100000) * 0.0005f;
    RenderBitmapUV(BITMAP_CHROME + 3, 0.f, 0.f, (float)REFERENCE_WIDTH,
                   (float)REFERENCE_HEIGHT - 45.f, WindX2, 0.f, 3.f, 2.f);
    EnableAlphaBlend();
    float WindX = (float)((int)WorldTime % 100000) * 0.0002f;
    RenderBitmapUV(BITMAP_CHROME + 2, 0.f, 0.f, (float)REFERENCE_WIDTH,
                   (float)REFERENCE_HEIGHT - 45.f, WindX, 0.f, 0.3f, 0.3f);
}

bool GMSwampOfQuiet::RenderSharedMonsterMesh(const ObjectDrawInput &input, BMD *pModel,
                                             bool ExtraMon)
{
    const auto &draw = input;
    const auto *pObject = input.source;
    if (draw.type >= MODEL_SHADOW_PAWN && draw.type <= MODEL_SHADOW_LOOK)
    {
        if (draw.action == MONSTER01_DIE)
        {
            float fLumi = 1.0f;
            if (draw.animationFrame > 9.0f)
            {
                fLumi = (12.0f - draw.animationFrame) / 3.0f;
            }

            switch (draw.type)
            {
            case MODEL_SHADOW_PAWN:
                Vector(1.0f, 0.2f, 0.2f, pModel->BodyLight);
                pModel->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 3, fLumi);
                break;
            case MODEL_SHADOW_KNIGHT:
                Vector(0.5f, 0.8f, 1.0f, pModel->BodyLight);
                pModel->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 3, fLumi);
                break;
            case MODEL_SHADOW_LOOK:
                Vector(0.5f, 1.0f, 0.5f, pModel->BodyLight);
                pModel->RenderMesh(4, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 4, fLumi);
                break;
            }
        }
        else
        {
            float fLumi = (sinf(WorldTime * 0.002f) + 1.2f) * 0.5f * 1.0f;
            switch (draw.type)
            {
            case MODEL_SHADOW_PAWN:
                pModel->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
                pModel->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
                pModel->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
                pModel->RenderMesh(4, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
                pModel->RenderMesh(3, RENDER_TEXTURE, 0.7f, draw.blendMesh, draw.blendLight);
                pModel->RenderMesh(4, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 4, fLumi, 0, 0,
                                   BITMAP_SHADOW_PAWN_RED);
                pModel->RenderMesh(4, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 4, fLumi, 0, 0,
                                   BITMAP_SHADOW_PAWN_RED);
                break;
            case MODEL_SHADOW_KNIGHT:
                pModel->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
                pModel->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
                pModel->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
                pModel->RenderMesh(4, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
                pModel->RenderMesh(3, RENDER_TEXTURE, 0.7f, draw.blendMesh, draw.blendLight);
                pModel->RenderMesh(4, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 4, fLumi, 0, 0,
                                   BITMAP_SHADOW_KINGHT_BLUE);
                pModel->RenderMesh(4, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 4, fLumi, 0, 0,
                                   BITMAP_SHADOW_KINGHT_BLUE);
                break;
            case MODEL_SHADOW_LOOK:
                pModel->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
                pModel->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
                pModel->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
                pModel->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
                pModel->RenderMesh(4, RENDER_TEXTURE, 0.7f, draw.blendMesh, draw.blendLight);
                pModel->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, 0, 0,
                                   BITMAP_SHADOW_ROOK_GREEN);
                pModel->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, 0, 0,
                                   BITMAP_SHADOW_ROOK_GREEN);
                break;
            }
        }
    }
    else
        return false;

    return true;
}

void GMSwampOfQuiet::RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *pModel,
                                           bool ExtraMon0)
{
    const auto &draw = input;
    const auto *pObject = input.source;
    if (!IsCurrentMap())
        return;

    // 	if(draw.type == 2 || draw.type == 53 || draw.type == 55 || draw.type == 89 || draw.type == 125 || draw.type == 128)	// 폭포물1,2, 수로, 회오리, 빛
    // 	{
    // 		pModel->RenderBody(RENDER_TEXTURE,draw.alpha,draw.blendMesh,draw.blendLight,draw.blendU,draw.blendV,draw.hiddenMesh);
    // 	}
}

void GMSwampOfQuiet::RenderAtmosphere()
{
    RenderBaseSmoke();
}

extern int GetMp3PlayPosition();

bool CGMDoppelGanger1::RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (IsDoppelGanger1() == false)
        return false;

    float fBlendMeshLight = 0;

    switch (draw.type)
    {
    case 19:
    case 20:
    case 31:
    case 33:

        return true;
    case 76:
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        return true;
    case 98:

        return true;
    case 102:
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->StreamMesh = 0;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, -(int)WorldTime % 4000 * 0.00025f);
        b->StreamMesh = -1;
        return true;
    case MODEL_ICE_WALKER:
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, BITMAP_DOPPELGANGER_ICEWALKER0);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, BITMAP_DOPPELGANGER_ICEWALKER1);
        return true;
    case MODEL_LARVA:
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, BITMAP_DOPPELGANGER_SNAKE01);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        return true;
    case MODEL_MAD_BUTCHER:
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        fBlendMeshLight = (sinf(WorldTime * 0.003f) + 1.0f) * 0.5f * 0.8f;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fBlendMeshLight,
                      draw.blendU, draw.blendV, BITMAP_BUGBEAR_R);
        //b->Actions[MONSTER01_WALK].PlaySpeed =		0.34f;
        return true;
    case MODEL_TERRIBLE_BUTCHER:
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        fBlendMeshLight = (sinf(WorldTime * 0.003f) + 1.0f) * 0.5f * 0.8f;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fBlendMeshLight,
                      draw.blendU, draw.blendV, BITMAP_BUGBEAR_R);
        return true;
    case MODEL_DOPPELGANGER:
        return true;
    }

    return false;
}

void CGMDoppelGanger1::RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (IsDoppelGanger1() == false)
        return;

    switch (draw.type)
    {
    case 19:
    case 20:
    case 31:
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        break;
    case 33:
        b->StreamMesh = 0;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, (int)WorldTime % 10000 * 0.0001f);
        b->StreamMesh = -1;
        break;
    case 98:
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
        break;
    default:
        break;
    }
}

bool CGMDoppelGanger1::RenderMonsterVisual(const CHARACTER *c, const ObjectDrawInput &input, BMD *b)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (draw.type != MODEL_DOPPELGANGER)
        return false;
    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                  draw.blendU, draw.blendV);
    b->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, 0.3f, draw.blendMesh, draw.blendLight,
                  draw.blendU, draw.blendV);
    return true;
}

void CGMDoppelGanger1::RenderAtmosphere()
{
    TheMapProcess().Raklion().RenderBaseSmoke();
}

bool CGMAida::RenderAidaObjectVisual(const ObjectDrawInput &input, BMD *pModel)
{
    const auto &draw = input;
    const auto *pObject = input.source;
    if (!IsInAida())
        return false;
    switch (draw.type)
    {
    case 41: {
        pModel->BeginRender(1.0f);

        pModel->BodyLight[0] = 0.52f;
        pModel->BodyLight[1] = 0.52f;
        pModel->BodyLight[2] = 0.52f;

        pModel->StreamMesh = 0;
        pModel->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                           draw.blendLight, draw.blendU, draw.blendV);
        pModel->StreamMesh = -1;

        pModel->EndRender();
    }
    break;
    case 65:
    case 66: {
        pModel->BeginRender(1.0f);

        pModel->StreamMesh = 0;
        pModel->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           -(int)WorldTime % 100000 * 0.00005f, draw.blendV);
        pModel->StreamMesh = -1;

        pModel->EndRender();
    }
    break;
    case 77:
    case 78: {
        pModel->BeginRender(1.0f);
        pModel->StreamMesh = 0;
        pModel->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           (int)WorldTime % 100000 * 0.0002f, draw.blendV);
        pModel->StreamMesh = -1;
        pModel->EndRender();
    }
    break;
    }
    return true;
}

bool CGMAida::RenderAidaObjectMesh(const ObjectDrawInput &input, BMD *pModel, bool ExtraMon)
{
    const auto &draw = input;
    const auto *pObject = input.source;
    if (IsInAida())
    {
    }

    return false;
}

bool CGMAida::RenderAidaMonsterObjectMesh(const ObjectDrawInput &input, BMD *pModel, bool ExtraMon)
{
    const auto &draw = input;
    const auto *pObject = input.source;
    switch (draw.type)
    {
    case MODEL_GOLDEN_STONE_GOLEM: {
        pModel->BeginRender(1.f);

        pModel->BodyLight[0] = 0.9f;
        pModel->BodyLight[1] = 0.9f;
        pModel->BodyLight[2] = 0.9f;

        pModel->StreamMesh = 0;
        pModel->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           -(int)WorldTime % 10000 * 0.0003f, -(int)WorldTime % 10000 * 0.0003f);
        pModel->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME, draw.alpha,
                           draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        pModel->StreamMesh = -1;
        pModel->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           draw.blendU, draw.blendV);

        pModel->EndRender();

        return true;
    }
    break;
    case MODEL_DEATH_RIDER: {
        pModel->BeginRender(1.f);

        pModel->BodyLight[0] = 0.9f;
        pModel->BodyLight[1] = 0.9f;
        pModel->BodyLight[2] = 0.9f;

        pModel->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           draw.blendU, draw.blendV);
        pModel->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           draw.blendU, draw.blendV);
        pModel->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           draw.blendU, draw.blendV);
        pModel->StreamMesh = 0;
        pModel->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           -(int)WorldTime % 10000 * 0.0006f, -(int)WorldTime % 10000 * 0.0006f);
        pModel->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME, draw.alpha,
                           draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        pModel->StreamMesh = -1;

        pModel->EndRender();

        return true;
    }
    break;
    case MODEL_HELL_MAINE: {
        pModel->BeginRender(1.f);

        pModel->BodyLight[0] = 1.0f;
        pModel->BodyLight[1] = 1.0f;
        pModel->BodyLight[2] = 1.0f;

        pModel->StreamMesh = 0;
        pModel->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           draw.blendU, draw.blendV);
        pModel->BodyLight[0] = 1.0f;
        pModel->BodyLight[1] = 0.0f;
        pModel->BodyLight[2] = 0.0f;
        pModel->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME, draw.alpha,
                           draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        pModel->StreamMesh = -1;

        pModel->BodyLight[0] = 1.0f;
        pModel->BodyLight[1] = 1.0f;
        pModel->BodyLight[2] = 1.0f;
        pModel->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           draw.blendU, draw.blendV);

        pModel->StreamMesh = 1;
        pModel->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           draw.blendU, draw.blendV);
        pModel->BodyLight[0] = 0.5f;
        pModel->BodyLight[1] = 0.0f;
        pModel->BodyLight[2] = 0.0f;
        pModel->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME, draw.alpha,
                           draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        pModel->StreamMesh = -1;

        pModel->EndRender();

        return true;
    }
    break;
    case MODEL_BLOODY_DEATH_RIDER: {
        pModel->BeginRender(1.f);

        pModel->BodyLight[0] = 0.9f;
        pModel->BodyLight[1] = 0.9f;
        pModel->BodyLight[2] = 0.9f;

        pModel->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           draw.blendU, draw.blendV);
        pModel->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           draw.blendU, draw.blendV);
        pModel->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           draw.blendU, draw.blendV);
        pModel->StreamMesh = 0;
        pModel->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           -(int)WorldTime % 10000 * 0.0006f, -(int)WorldTime % 10000 * 0.0006f);
        pModel->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME, draw.alpha,
                           draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        pModel->StreamMesh = -1;

        pModel->EndRender();

        return true;
    }
    break;
    case MODEL_BLOODY_GOLEM: {
        pModel->BeginRender(1.f);

        pModel->BodyLight[0] = 0.9f;
        pModel->BodyLight[1] = 0.9f;
        pModel->BodyLight[2] = 0.9f;

        pModel->StreamMesh = 0;
        pModel->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           -(int)WorldTime % 10000 * 0.0003f, -(int)WorldTime % 10000 * 0.0003f);
        pModel->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME, draw.alpha,
                           draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        pModel->StreamMesh = -1;
        pModel->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                           draw.blendU, draw.blendV);

        pModel->EndRender();

        return true;
    }
    break;
    }
    return false;
}

bool CGMAida::RenderObjectMesh(const ObjectDrawInput &input, BMD *model, bool extraMonster)
{
    const auto &draw = input;
    const auto *object = input.source;
    return RenderAidaObjectMesh(draw, model, extraMonster);
}

bool CGMAida::RenderObjectVisual(const ObjectDrawInput &input, BMD *model)
{
    const auto &draw = input;
    const auto *object = input.source;
    return RenderAidaObjectVisual(draw, model);
}

bool GMEmpireGuardian1::RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    auto draw = input;
    const auto *o = input.source;
    if (gMapManager.IsEmpireGuardian1() == false && gMapManager.IsEmpireGuardian2() == false &&
        gMapManager.IsEmpireGuardian3() == false && gMapManager.IsEmpireGuardian4() == false)
    {
        return false;
    }

    switch (draw.type)
    {
    case 0:
    case 1:
    case 3:
    case 44:
    case 81:

        return true;

    case 37: {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
        return true;

    case 41: // sos_bobpu
    {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        float Luminosity = sinf(WorldTime * 0.002f) * 0.5f + 0.6f;
        Vector(Luminosity * b->BodyLight[0], Luminosity * b->BodyLight[1],
               Luminosity * b->BodyLight[2], b->BodyLight);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
        return true;

    case 47: // Gateflag
    {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        Vector(1.2f, 1.2f, 1.2f, b->BodyLight);
        b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
        return true;

    case 48: // Gateflag2
    {
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        Vector(1.1f, 1.1f, 1.1f, b->BodyLight);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
        return true;

    case 49: {
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
        return true;

    case 55: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
        return true;

    case 64: {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        Vector(1.0f, 0.0f, 0.0f, b->BodyLight);
        b->RenderMesh(1, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
        b->RenderMesh(1, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
    }
        return true;

    case 70: {
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
        return true;

    case 115: {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        draw.blendV = (int)WorldTime % 25000 * 0.0004f;
        b->StreamMesh = 1;
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        draw.blendV = (int)WorldTime % 25000 * -0.0004f;
        b->StreamMesh = 3;
        b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->StreamMesh = -1;
    }
        return true;

    case 117: {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        draw.blendV = (int)WorldTime % 25000 * 0.0004f;
        b->StreamMesh = 1;
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        draw.blendV = (int)WorldTime % 25000 * -0.0004f;
        b->StreamMesh = 4;
        b->RenderMesh(4, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->StreamMesh = -1;
    }
        return true;

    case MODEL_PROJECTILE: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
        return true;
    case MODEL_DOOR_CRUSH_EFFECT_PIECE01:
    case MODEL_DOOR_CRUSH_EFFECT_PIECE02:
    case MODEL_DOOR_CRUSH_EFFECT_PIECE03:
    case MODEL_DOOR_CRUSH_EFFECT_PIECE04:
    case MODEL_DOOR_CRUSH_EFFECT_PIECE05:
    case MODEL_DOOR_CRUSH_EFFECT_PIECE06:
    case MODEL_DOOR_CRUSH_EFFECT_PIECE07:
    case MODEL_DOOR_CRUSH_EFFECT_PIECE08:
    case MODEL_DOOR_CRUSH_EFFECT_PIECE09:
    case MODEL_DOOR_CRUSH_EFFECT_PIECE10:
    case MODEL_DOOR_CRUSH_EFFECT_PIECE11:
    case MODEL_DOOR_CRUSH_EFFECT_PIECE12:
    case MODEL_DOOR_CRUSH_EFFECT_PIECE13:
    case MODEL_STATUE_CRUSH_EFFECT_PIECE01:
    case MODEL_STATUE_CRUSH_EFFECT_PIECE02:
    case MODEL_STATUE_CRUSH_EFFECT_PIECE03:
    case MODEL_STATUE_CRUSH_EFFECT_PIECE04: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
        return true;

    case MODEL_RAYMOND:
    case MODEL_LUCAS:
    case MODEL_EVIL_GATE:
    case MODEL_LION_GATE:
    case MODEL_STATUE:
    case MODEL_DEVIL_LORD:
    case MODEL_QUARTER_MASTER:
    case MODEL_DEFENDER: {
        RenderMonster(draw, b, ExtraMon);
    }
        return true;
    }

    return false;
}

bool GMEmpireGuardian1::RenderMonster(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    switch (draw.type)
    {
    case MODEL_RAYMOND: {
        float fBlendMeshLight = (sinf(WorldTime * 0.003f) + 1.0f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        vec3_t _temp;
        VectorCopy(b->BodyLight, _temp);
        Vector(b->BodyLight[0] * 3.0f, b->BodyLight[0] * 3.0f, b->BodyLight[0] * 3.0f,
               b->BodyLight);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, fBlendMeshLight,
                      draw.blendU, draw.blendV, BITMAP_RAYMOND_SWORD);
        VectorCopy(_temp, b->BodyLight);
        b->RenderMesh(3, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
    }
        return true;
    case MODEL_LUCAS: {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(3, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);

        b->RenderMesh(4, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        //b->RenderMesh ( 4, RENDER_BRIGHT|RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV );
        //b->RenderMesh ( 4, RENDER_BRIGHT|RENDER_CHROME6, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV );
    }
        return true;
    case MODEL_DEVIL_LORD: {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_CHROME4, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
        return true;
    case MODEL_QUARTER_MASTER: {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_CHROME | RENDER_BRIGHT, 0.3f, 2, 0.3f, draw.blendU, draw.blendV);
    }
        return true;
    case MODEL_DEFENDER: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
        return true;
    case MODEL_EVIL_GATE: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
        return true;

    case MODEL_LION_GATE: {
        if (draw.action != MONSTER01_DIE)
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
    }
        return true;
    case MODEL_STATUE: {
        if (draw.action != MONSTER01_DIE)
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
    }
        return true;
    }

    return false;
}

void GMEmpireGuardian1::RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (gMapManager.IsEmpireGuardian1() == false)
        return;

    switch (draw.type)
    {
    case 0:
    case 1:
    case 3:
    case 44: {
        float fLumi;
        fLumi = (sinf(WorldTime * 0.0015f) + 1.0f) * 0.4f + 0.2f;

        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 2, fLumi, draw.blendU,
                      draw.blendV);
    }
    break;

    case 81: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    break;
    }
}

void GMEmpireGuardian1::RenderFrontSideVisual()
{
    switch (m_iWeather)
    {
    case WEATHER_FOG: {
        EnableAlphaBlend();
        glColor3f(0.6f, 0.6f, 0.9f);
        float WindX2 = (float)((int)WorldTime % 100000) * 0.00005f;
        float WindY2 = (float)((int)WorldTime % 100000) * 0.00008f;
        RenderBitmapUV(BITMAP_CHROME + 2, 0.f, 0.f, (float)REFERENCE_WIDTH,
                       (float)REFERENCE_HEIGHT - 45.f, WindX2, WindY2, 2.0f, 2.0f);
        float WindX = -(float)((int)WorldTime % 100000) * 0.00005f;
        RenderBitmapUV(BITMAP_CHROME + 2, 0.f, 0.f, (float)REFERENCE_WIDTH,
                       (float)REFERENCE_HEIGHT - 45.f, WindX, 0.f, 0.3f, 0.3f);
    }
    break;
    case WEATHER_STORM: {
        if (stormFlash_)
        {
            EnableAlphaBlend();
            glColor3f(0.7f, 0.7f, 0.9f);
            float WindX2 = (float)((int)WorldTime % 100000) * 0.0006f;
            float WindY2 = -(float)((int)WorldTime % 100000) * 0.0006f;
            RenderBitmapUV(BITMAP_CHROME + 2, 0.f, 0.f, (float)REFERENCE_WIDTH,
                           (float)REFERENCE_HEIGHT - 45.f, WindX2, WindY2, 3.0f, 2.0f);
            WindX2 = -(float)((int)WorldTime % 100000) * 0.0006f;
            WindY2 = (float)((int)WorldTime % 100000) * 0.0006f;
            RenderBitmapUV(BITMAP_CHROME + 2, 0.f, 0.f, (float)REFERENCE_WIDTH,
                           (float)REFERENCE_HEIGHT - 45.f, WindX2, WindY2, 3.0f, 2.0f);
        }
    }
    break;
    }
}

using namespace SEASON4A;

bool CGM_Raklion::RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    auto draw = input;
    const auto *o = input.source;
    if (IsIceCity() == false)
        return false;

    if (draw.type >= 6 && draw.type <= 12)
    {
        for (int i = 0; i < b->NumMeshs; i++)
        {
            Mesh_t *m = &b->Meshs[i];
            for (int j = 0; j < m->NumNormals; j++)
            {
                IntensityTransform[i][j] = 0.5f;
            }
        }
        Vector(1.0f, 1.0f, 1.0f, draw.light);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        return true;
    }
    else if (draw.type == 16)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        return true;
    }
    else if (draw.type == 17)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        return true;
    }
    else if (draw.type == 19)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        return true;
    }
    else if (draw.type == 20)
    {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_CHROME2 | RENDER_DARK, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, BITMAP_CHROME8);
        b->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, BITMAP_CHROME8);

        return true;
    }
    else if (draw.type == 21)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        return true;
    }
    else if (draw.type == 46 || draw.type == 53 || draw.type == 76)
    {

        return true;
    }
    else if (draw.type == 57)
    {
        vec3_t vRelativePos, vWorldPos;
        Vector(0, 0, 0, vRelativePos);
        b->TransformPosition(draw.bones[1], vRelativePos, vWorldPos, false);
        vec3_t vLight;
        Vector(1.0f, 1.0f, 1.0f, vLight);
        //CreateEffect(BITMAP_GATHERING,vWorldPos,draw.angle,vLight,3,o);

        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);

        return true;
    }
    else if (draw.type == 68 || draw.type == 69 || draw.type == 71)
    {
        float fLumi = (sinf(WorldTime * 0.001f) + 1.f) * 0.5f + 0.3f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, 0, fLumi, draw.blendU, draw.blendV);

        return true;
    }
    else if (draw.type == MODEL_WARP || draw.type == MODEL_WARP2 || draw.type == MODEL_WARP3)
    {
        b->BodyLight[0] = 1.0f;
        b->BodyLight[1] = 1.0f;
        b->BodyLight[2] = 1.0f;
        draw.blendLight = 1.0f;

        if (draw.type == MODEL_WARP)
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV);
        }
        else if (draw.type == MODEL_WARP2)
        {
            b->RenderBody(RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV);
        }
        else if (draw.type == MODEL_WARP3)
        {
            b->RenderMesh(0, RENDER_DARK, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV);
        }

        return true;
    }
    else if (draw.type == MODEL_WARP4 || draw.type == MODEL_WARP5 || draw.type == MODEL_WARP6)
    {
        if (m_bCanGoBossMap == false)
        {
            return true;
        }

        b->BodyLight[0] = 0.5f;
        b->BodyLight[1] = 0.6f;
        b->BodyLight[2] = 1.0f;
        draw.blendLight = 0.8f;

        if (draw.type == MODEL_WARP4)
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV);
        }
        else if (draw.type == MODEL_WARP5)
        {
            b->RenderBody(RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV);
        }
        else if (draw.type == MODEL_WARP6)
        {
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }

        return true;
    }
    else if (draw.type == 82)
    {

        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);

        return true;
    }
    else if ((draw.type >= MODEL_ICE_WALKER && draw.type <= MODEL_SPIDER_EGGS_3) ||
             draw.type == MODEL_DARK_MAMMOTH || draw.type == MODEL_DARK_GIANT ||
             draw.type == MODEL_DARK_IRON_KNIGHT || draw.type == MODEL_DARK_COOLUTIN)
    {
        RenderMonster(draw, b, ExtraMon);

        return true;
    }

    return false;
}
bool CGM_Raklion::RenderMonster(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    auto draw = input;
    const auto *o = input.source;
    switch (draw.type)
    {
    case MODEL_ICE_WALKER: {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);

        return true;
    }
    break;
    case MODEL_GIANT_MAMMOTH: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);

        //b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        //b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        //b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);

        b->RenderMesh(1, RENDER_CHROME6 | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);

        return true;
    }
    break;

    case MODEL_ICE_GIANT:
        if (draw.action != MONSTER01_DIE)
        {
            Vector(1.0f, 1.0f, 1.0f, b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME7, draw.alpha,
                          draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
            b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
            b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME, draw.alpha,
                          draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);

            return true;
        }
        break;

    case MODEL_COOLUTIN: {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);

        return true;
    }
    break;

    case MODEL_IRON_KNIGHT:
        if (draw.action == MONSTER01_DIE)
        {
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV, draw.hiddenMesh);
        }
        else
        {
            float fLumi = (sinf(WorldTime * 0.01f) + 1.0f) * 0.4f + 0.2f;
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV, draw.hiddenMesh);
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                          draw.blendV, BITMAP_IRONKNIGHT_BODY_BRIGHT);
            b->RenderMesh(1, RENDER_TEXTURE, 0.4f, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, 0.7f, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
            b->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, 0.5f, 1, 0.5f, draw.blendU, draw.blendV,
                          draw.hiddenMesh);
        }

        return true;

    case MODEL_SELUPAN: {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);

        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);

        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);

        b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);

        b->RenderMesh(4, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);

        b->RenderMesh(5, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 5, draw.blendLight,
                      (int)WorldTime % 10000 * 0.0002f, (int)WorldTime % 10000 * 0.0002f,
                      draw.hiddenMesh);

        float fLumi2 = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;

        b->RenderMesh(4, RENDER_TEXTURE | RENDER_CHROME4 | RENDER_BRIGHT, draw.alpha,
                      draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi2, draw.blendU,
                      draw.blendV, BITMAP_SERUFAN_ARM_R);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi2, draw.blendU,
                      draw.blendV, BITMAP_SERUFAN_ARM_R);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi2, draw.blendU,
                      draw.blendV, BITMAP_SERUFAN_ARM_R);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi2, draw.blendU,
                      draw.blendV, BITMAP_SERUFAN_ARM_R);

        if (m_byDetailState >= BATTLE_OF_SELUPAN_PATTERN_2)
        {
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_CHROME6 | RENDER_BRIGHT, draw.alpha,
                          draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                          BITMAP_MAGIC_EMBLEM);
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_CHROME4 | RENDER_BRIGHT, draw.alpha,
                          draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        }

        b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);

        return true;
    }
    break;
    case MODEL_SPIDER_EGGS_1:
    case MODEL_SPIDER_EGGS_2:
    case MODEL_SPIDER_EGGS_3: {
        if (draw.action == MONSTER01_DIE)
        {
            const float fBlendLight = RaklionDetail::EggDeathBrightness(draw.animationFrame);
            vec3_t vOriginPos;
            VectorCopy(draw.position, vOriginPos);

            for (int i = 0; i < 3; ++i)
            {
                if (i == 0)
                {
                    if (draw.type == MODEL_SPIDER_EGGS_1)
                        draw.position[0] += 60;
                    else
                        draw.position[0] += 100;
                }
                else if (i == 1)
                {
                    if (draw.type == MODEL_SPIDER_EGGS_1)
                        draw.position[1] += 60;
                    else
                        draw.position[1] += 100;
                }
                else
                {
                    VectorCopy(vOriginPos, draw.position);
                }

                Calc_RenderObject(draw, true, false, 0);
                b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 3, fBlendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }

            //b->RenderBody(RENDER_TEXTURE,draw.alpha,draw.blendMesh,draw.blendLight,draw.blendU,draw.blendV,draw.hiddenMesh);
        }
        else
        {
            //b->RenderBody(RENDER_TEXTURE,draw.alpha,draw.blendMesh,draw.blendLight,draw.blendU,draw.blendV,draw.hiddenMesh);

            vec3_t vOriginPos;
            VectorCopy(draw.position, vOriginPos);

            for (int i = 0; i < 3; ++i)
            {
                if (i == 0)
                {
                    if (draw.type == MODEL_SPIDER_EGGS_1)
                        draw.position[0] += 60;
                    else
                        draw.position[0] += 100;
                }
                else if (i == 1)
                {
                    if (draw.type == MODEL_SPIDER_EGGS_1)
                        draw.position[1] += 60;
                    else
                        draw.position[1] += 100;
                }
                else
                {
                    VectorCopy(vOriginPos, draw.position);
                }

                Calc_RenderObject(draw, true, false, 0);
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);

                vec3_t vPos, vLight;
                float fLumi = (sinf(WorldTime * 0.004f) + 1.2f) * 0.5f + 0.1f;
                Vector(0.1f * fLumi, 0.6f * fLumi, 0.7f * fLumi, vLight);
                if (draw.type == MODEL_SPIDER_EGGS_1)
                {
                    b->TransformByObjectBone(vPos, draw, 57);
                    CreateSprite(BITMAP_LIGHT, vPos, 3.f, vLight, o);
                }
                else if (draw.type == MODEL_SPIDER_EGGS_2)
                {
                    b->TransformByObjectBone(vPos, draw, 95);
                    CreateSprite(BITMAP_LIGHT, vPos, 3.f, vLight, o);
                    b->TransformByObjectBone(vPos, draw, 115);
                    CreateSprite(BITMAP_LIGHT, vPos, 3.f, vLight, o);
                }
                else
                {
                    b->TransformByObjectBone(vPos, draw, 95);
                    CreateSprite(BITMAP_LIGHT, vPos, 3.f, vLight, o);
                    b->TransformByObjectBone(vPos, draw, 115);
                    CreateSprite(BITMAP_LIGHT, vPos, 3.f, vLight, o);
                    b->TransformByObjectBone(vPos, draw, 173);
                    CreateSprite(BITMAP_LIGHT, vPos, 3.f, vLight, o);
                }
            }
        }

        return true;
    }
    break;
    case MODEL_DARK_MAMMOTH: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_CHROME6 | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);

        return true;
    }
    break;
    case MODEL_DARK_GIANT: {
        if (draw.action != MONSTER01_DIE)
        {
            Vector(1.0f, 1.0f, 1.0f, b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME7, draw.alpha,
                          draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
            b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
            b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME, draw.alpha,
                          draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);

            return true;
        }
    }
    break;
    case MODEL_DARK_COOLUTIN: {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
    }
    break;
    case MODEL_DARK_IRON_KNIGHT: {
        if (draw.action == MONSTER01_DIE)
        {
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV, draw.hiddenMesh);
        }
        else
        {
            float fLumi = (sinf(WorldTime * 0.01f) + 1.0f) * 0.4f + 0.2f;
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV, draw.hiddenMesh);
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                          draw.blendV, BITMAP_IRONKNIGHT_BODY_BRIGHT);
            b->RenderMesh(1, RENDER_TEXTURE, 0.4f, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, 0.7f, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
            b->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, 0.5f, 1, 0.5f, draw.blendU, draw.blendV,
                          draw.hiddenMesh);
        }

        return true;
    }
    break;
    }

    return false;
}

void CGM_Raklion::RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    auto draw = input;
    const auto *o = input.source;
    if (draw.type == 46 || draw.type == 53)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (draw.type == 76)
    {
        float R, G, B;
        R = (float)sinf(WorldTime * 0.002f) * 0.2f + 0.5f;
        G = (float)sinf(WorldTime * 0.0015f) * 0.2f + 0.5f;
        B = (float)sinf(WorldTime * 0.0014f) * 0.2f + 0.5f;
        Vector(R, G, B, draw.light);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
}

void CGM_Raklion::RenderBaseSmoke()
{
    EnableAlphaBlend();
    glColor3f(0.4f, 0.4f, 0.45f);
    float WindX2 = (float)((int)WorldTime % 100000) * 0.0006f;
    float WindY2 = -(float)((int)WorldTime % 100000) * 0.0006f;
    RenderBitmapUV(BITMAP_CHROME + 3, 0.f, 0.f, (float)REFERENCE_WIDTH,
                   (float)REFERENCE_HEIGHT - 45.f, WindX2, WindY2, 3.0f, 2.0f);
    float WindX = (float)((int)WorldTime % 100000) * 0.0001f;
    RenderBitmapUV(BITMAP_CHROME + 2, 0.f, 0.f, (float)REFERENCE_WIDTH,
                   (float)REFERENCE_HEIGHT - 45.f, WindX, 0.f, 0.3f, 0.3f);
}

bool SEASON4A::CGM_Raklion::IsEarlyAfterCharacterObject(const OBJECT &object) const
{
    return object.Type == 76;
}

void SEASON4A::CGM_Raklion::RenderEarlyAfterCharacterObjects(OBJECT *head)
{
    for (OBJECT *object = head; object; object = object->Next)
        if (IsEarlyAfterCharacterObject(*object) &&
            TestFrustrum2D(object->Position[0] * 0.01f, object->Position[1] * 0.01f, -600.f))
            RenderObject_AfterCharacter(object);
}

void SEASON4A::CGM_Raklion::RenderAtmosphere()
{
    RenderBaseSmoke();
}

//  GMBattleCastle.cpp

void CGMBattleCastle::RenderAurora(int Type, int RenderType, float x, float y, float sx, float sy,
                                   const vec3_t inputLight)
{
    vec3_t Light;
    VectorCopy(inputLight, Light);
    float Luminosity = sinf(WorldTime * 0.0015f) * 0.3f + 0.7f;

    if (RenderType == RENDER_DARK)
    {
        EnableAlphaBlendMinus();
        Vector(Luminosity, Luminosity, Luminosity, Light);
    }
    else
    {
        EnableAlphaBlend();
        Vector(Luminosity * Light[0], Luminosity * Light[1], Luminosity * Light[2], Light);
    }

    RenderTerrainAlphaBitmap(Type, x, y, sx, sy, Light, WorldTime * 0.01f);

    if (RenderType == RENDER_DARK)
    {
        Luminosity = 1.f - Luminosity;
        Vector(Luminosity * Light[0], Luminosity * Light[1], Luminosity * Light[2], Light);
    }
    RenderTerrainAlphaBitmap(Type, x, y, sx, sy, Light, -WorldTime * 0.01f);
}

void CGMBattleCastle::RenderBaseSmoke()
{
    if (gMapManager.InBattleCastle() == false)
        return;
    if (IsBattleCastleStart() == false)
        return;

    EnableAlphaTest();

    glColor3f(0.3f, 0.3f, 0.25f);
    float WindX2 = (float)((int)WorldTime % 100000) * 0.0005f;
    RenderBitmapUV(BITMAP_CHROME + 3, 0.f, 0.f, (float)REFERENCE_WIDTH,
                   (float)REFERENCE_HEIGHT - 45.f, WindX2, 0.f, 3.f, 2.f);
    EnableAlphaBlend();
    float WindX = (float)((int)WorldTime % 100000) * 0.0002f;
    RenderBitmapUV(BITMAP_CHROME + 2, 0.f, 0.f, (float)REFERENCE_WIDTH,
                   (float)REFERENCE_HEIGHT - 45.f, WindX, 0.f, 0.3f, 0.3f);
}

bool CGMBattleCastle::RenderBattleCastleObjectMesh(const ObjectDrawInput &input, BMD *b)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (gMapManager.InBattleCastle() == false)
        return false;

    if (draw.type == 12)
    {
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, BITMAP_INTERFACE_MAP);
        return true;
    }
    else if (draw.type == 17)
    {
        b->StreamMesh = 0;
        Vector(0.45f, 0.45f, 0.45f, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      (int)WorldTime % 10000 * 0.0002f, draw.hiddenMesh);
        b->StreamMesh = -1;
        return true;
    }
    else if (draw.type == 51)
    {
        b->StreamMesh = 0;
        b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      -(int)WorldTime % 10000 * 0.0002f, draw.blendV, draw.hiddenMesh);
        b->StreamMesh = -1;
        return true;
    }
    else if (draw.type == 66)
    {
        Vector(0.1f, 0.4f, 0.6f, b->BodyLight);
        b->RenderBody(RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      sinf(WorldTime * 0.001f) * 0.2f + 0.3f, draw.blendU, draw.blendV,
                      draw.hiddenMesh);
        return true;
    }
    else if (draw.type == 50 || draw.type == 83 || draw.type == 84 || draw.type == 85)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);

        if (IsBattleCastleStart() == false)
        {
            DisableAlphaBlend();
            glColor3f(0.f, 0.f, 0.f);
            b->RenderBodyShadow(draw.blendMesh, draw.hiddenMesh);
        }
        return true;
    }
    else if (draw.type == 19)
    {
        if (IsBattleCastleStart())
        {
            b->BeginRender(draw.alpha);
            glColor3fv(b->BodyLight);
            b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            Vector(0.3f, 0.3f, 0.3f, b->BodyLight);
            b->RenderMesh(3, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);
            Vector(0.2f, 0.2f, 0.5f, b->BodyLight);
            b->RenderMesh(3, RENDER_BRIGHT | RENDER_METAL, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);
            Vector(1.f, 1.f, 1.f, b->BodyLight);
            b->EndRender();
        }
        else
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
            Vector(0.3f, 0.3f, 0.3f, b->BodyLight);
            b->RenderBody(RENDER_BRIGHT | RENDER_CHROME, draw.alpha, 0.5f, draw.blendLight,
                          draw.blendU, draw.blendV, draw.hiddenMesh, BITMAP_CHROME);
            Vector(0.2f, 0.2f, 0.5f, b->BodyLight);
            b->RenderBody(RENDER_BRIGHT | RENDER_METAL, draw.alpha, 0.5f, draw.blendLight,
                          draw.blendU, draw.blendV, draw.hiddenMesh, BITMAP_CHROME);
            Vector(1.f, 1.f, 1.f, b->BodyLight);
        }
        return true;
    }
    else if (draw.type >= BATTLE_CASTLE_WALL1 && draw.type <= BATTLE_CASTLE_WALL4)
    {
        if (o->SubType == 0)
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
        else
        {
        }
        return true;
    }
    else
    {
        return RenderBattleCastleMonsterObjectMesh(draw, b);
    }

    return false;
}
bool CGMBattleCastle::RenderBattleCastleMonsterObjectMesh(const ObjectDrawInput &input, BMD *b)
{
    auto draw = input;
    const auto *o = input.source;
    if (gMapManager.InBattleCastle() == false)
        return false;

    bool success = false;

    switch (draw.type)
    {
    case MODEL_NPC_CAPATULT_ATT: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);

        OBB_t OBB;
        vec3_t Temp{}, p, Position;
        BMD *linkBmd = &Models[MODEL_FLY_BIG_STONE1];
        linkBmd->BodyHeight = 0.f;
        linkBmd->ContrastEnable = o->ContrastEnable;
        BodyLight(draw, linkBmd);
        linkBmd->BodyScale = 1.3f;
        linkBmd->CurrentAction = 0;

        if (draw.action == 0 || draw.animationFrame < 2.f || draw.animationFrame >= 22.f)
        {
            Vector(0.f, 0.f, 0.f, p);
            b->TransformPosition(draw.bones[12], p, Position, true);
            VectorCopy(Position, linkBmd->BodyOrigin);
            auto *stonePose = AllocateDrawPose(linkBmd->NumBones);
            linkBmd->Animation(stonePose, 0.f, 0.f, 0, draw.angle, draw.angle, false, false);
            linkBmd->Transform(stonePose, Temp, Temp, &OBB, true);
            linkBmd->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                draw.blendU, draw.blendV, draw.hiddenMesh);
        }

        if (draw.action == 1 && draw.animationFrame > 13.f && draw.animationFrame < 22.f)
        {
            Vector(0.f, 0.f, 0.f, p);
            b->TransformPosition(draw.bones[44], p, Position, true);
            VectorCopy(Position, linkBmd->BodyOrigin);
            auto *stonePose = AllocateDrawPose(linkBmd->NumBones);
            linkBmd->Animation(stonePose, 0.f, 0.f, 0, draw.angle, draw.angle, false, false);
            linkBmd->Transform(stonePose, Temp, Temp, &OBB, true);
            linkBmd->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                draw.blendU, draw.blendV, draw.hiddenMesh);
        }
    }
        success = true;
        break;

    case MODEL_NPC_BARRIER:
        Vector(0.3f, 0.3f, 1.f, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, draw.hiddenMesh, BITMAP_WATER + WaterTextureNumber);
        Vector(0.3f, 1.f, 0.3f, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, draw.hiddenMesh,
                      BITMAP_WATER + ((int)(WorldTime * 0.01f) % 32));
        success = true;
        break;

    case MODEL_CASTLE_GATE1:
        if (draw.action != MONSTER01_DIE)
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        break;

    case MODEL_GUARDIAN_STATUE:
        if (IsBattleCastleStart())
        {
            if (draw.action != MONSTER01_DIE)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                Vector(0.3f, 0.3f, 0.3f, b->BodyLight);
                b->RenderBody(RENDER_BRIGHT | RENDER_CHROME, draw.alpha, 0.5f, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh, BITMAP_CHROME);
                Vector(0.2f, 0.2f, 0.5f, b->BodyLight);
                b->RenderBody(RENDER_BRIGHT | RENDER_METAL, draw.alpha, 0.5f, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh, BITMAP_CHROME);
                Vector(1.f, 1.f, 1.f, b->BodyLight);
            }
        }
        success = true;
        break;

    case MODEL_LIFE_STONE:
        draw.blendLight = o->m_byBuildTime / 10.f;
        Vector(draw.blendLight, draw.blendLight, draw.blendLight, b->BodyLight);
        if (o->m_byBuildTime >= 2)
        {
            b->RenderMesh(0, RENDER_TEXTURE);
        }
        if (o->m_byBuildTime >= 3)
        {
            b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT);
        }
        if (o->m_byBuildTime >= 4)
        {
            Vector(draw.blendLight * 0.5f, draw.blendLight * 0.5f, draw.blendLight * 0.5f,
                   b->BodyLight);
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT);
        }
        if (o->m_byBuildTime >= 5)
        {
            Vector(0.5f, 0.5f, 0.5f, b->BodyLight);
            b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, 1.f, 3, 1.f, WorldTime * 0.0001f);

            Vector(0.2f, 0.2f, 0.2f, b->BodyLight);
            b->RenderMesh(4, RENDER_TEXTURE | RENDER_BRIGHT, 1.f, 3, 1.f, WorldTime * 0.0001f);
        }
        success = true;
        break;
    }

    return success;
}

void CGMBattleCastle::RenderBuildTimes()
{
    BuildTime buildTime;
    for (int i = 0; i < static_cast<int>(g_qBuildTimeLocation.size()); ++i)
    {
        buildTime = g_qBuildTimeLocation.front();

        int screenX;
        int screenY;
        constexpr int width = 50;
        constexpr int height = 2;
        cameraProjection_.WorldToScreen(g_Camera, buildTime.m_vPosition, &screenX, &screenY);
        screenX -= width / 2;

        const int time = static_cast<int>(buildTime.m_byBuildTime / 5.f * width);
        RenderBar(screenX, screenY + 12, width, height, static_cast<float>(time));

        g_qBuildTimeLocation.pop();
    }
}

// OMF-02084

bool CGMBattleCastle::RenderBattleCastleMonsterVisual(const CHARACTER *c,
                                                      const ObjectDrawInput &input, BMD *b)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (!gMapManager.InBattleCastle())
        return false;
    vec3_t light = {0.3f, 0.2f, 0.f};
    if (draw.type == MODEL_GUARDIAN_STATUE)
    {
        if (IsBattleCastleStart())
            RenderAurora(BITMAP_MAGIC + 1, RENDER_BRIGHT, draw.position[0], draw.position[1], 9.f,
                         9.f, light);
        else
            RenderAurora(BITMAP_MAGIC + 1, RENDER_DARK, draw.position[0], draw.position[1], 5.5f,
                         5.5f, draw.light);
        return true;
    }
    if (draw.type == MODEL_LIFE_STONE && o->m_byBuildTime >= 5)
        RenderAurora(BITMAP_MAGIC + 1, RENDER_BRIGHT, draw.position[0], draw.position[1], 9.f, 9.f,
                     light);
    if (draw.type == MODEL_CANON_TOWER && IsBattleCastleStart())
        RenderAurora(BITMAP_MAGIC + 1, RENDER_DARK, draw.position[0], draw.position[1], 5.5f, 5.5f,
                     draw.light);
    return false;
}

bool CGMBattleCastle::RenderObjectMesh(const ObjectDrawInput &input, BMD *model, bool extraMonster)
{
    const auto &draw = input;
    const auto *object = input.source;
    return RenderBattleCastleObjectMesh(draw, model);
}

bool CGMBattleCastle::RenderMonsterVisual(const CHARACTER *character, const ObjectDrawInput &input,
                                          BMD *model)
{
    const auto &draw = input;
    const auto *object = input.source;
    return RenderBattleCastleMonsterVisual(character, draw, model);
}

void CGMBattleCastle::RenderAtmosphere()
{
    RenderBaseSmoke();
}

void CGMBattleCastle::BeginSceneRender()
{
    if (!InBattleCastle2(Hero->Object.Position))
        return;
    vec3_t color{};
    StartFog(color);
}

void CGMBattleCastle::EndSceneRender()
{
    if (InBattleCastle2(Hero->Object.Position))
        EndFog();
}

void CGMBattleCastle::RenderMapInterface()
{
    RenderBuildTimes();
}

//  GMHellas.cpp

#define NUM_HELLAS 7

#define KUNDUN_ZONE NUM_HELLAS

bool CGMHellas::RenderWaterTerrain(void)
{
    if (g_pCSWaterTerrain != nullptr)
    {
        g_pCSWaterTerrain->Render();
        return true;
    }
    return false;
}

void CGMHellas::RenderWaterTerrain(int Texture, float xf, float yf, float SizeX, float SizeY,
                                   const vec3_t Light, float Rotation, float Alpha, float Height)
{
    if (g_pCSWaterTerrain != nullptr)
    {
        g_pCSWaterTerrain->RenderWaterAlphaBitmap(Texture, xf, yf, SizeX, SizeY, Light, Rotation,
                                                  Alpha, Height);
    }
}

int CGMHellas::RenderHellasItemInfo(ITEM *ip, int textNum)
{
    int TextNum = textNum;
    switch (ip->Type)
    {
    case ITEM_LOST_MAP: {
        int startIndex = 0;
        int baseClass = gCharacterManager.GetBaseClass(Hero->Class);
        if (baseClass == CLASS_DARK || baseClass == CLASS_DARK_LORD ||
            baseClass == CLASS_RAGEFIGHTER)
        {
            startIndex = NUM_HELLAS;
        }

        int HeroLevel = CharacterAttribute->Level;
        int ItemLevel = ip->Level;

        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        mu_swprintf(TextList[TextNum], L"%ls %ls       %ls    ", I18N::Game::Kalima,
                    I18N::Game::Level, I18N::Game::MinLevel);
        TextListColor[TextNum] = TEXT_COLOR_WHITE;
        TextBold[TextNum] = false;
        TextNum++;
        for (int i = 0; i < NUM_HELLAS; i++)
        {
            mu_swprintf(TextList[TextNum], L"        %d             %3d~%3d     ", i + 1,
                        HellasDetail::g_iKalimaLevel[startIndex + i][0],
                        std::min<int>(400, HellasDetail::g_iKalimaLevel[startIndex + i][1]));

            if (ItemLevel == i + 1)
            {
                TextListColor[TextNum] = TEXT_COLOR_DARKYELLOW;
            }
            else
            {
                TextListColor[TextNum] = TEXT_COLOR_WHITE;
            }
            TextBold[TextNum] = false;
            TextNum++;
        }

        mu_swprintf(TextList[TextNum], L"\n");
        TextNum++;
        mu_swprintf(TextList[TextNum], I18N::Game::MagicStoneWillAppearWhenYouThrowItInTheScreen);
        TextListColor[TextNum] = TEXT_COLOR_DARKBLUE;
        TextNum++;

        if (HeroLevel < HellasDetail::g_iKalimaLevel[startIndex][0])
        {
            mu_swprintf(TextList[TextNum], L"\n");
            TextNum++;
            mu_swprintf(TextList[TextNum], I18N::Game::OnlyAboveLevelDCanUse,
                        HellasDetail::g_iKalimaLevel[startIndex][0]);
            TextListColor[TextNum] = TEXT_COLOR_DARKRED;
            TextNum++;
        }
    }
    break;

    case ITEM_SYMBOL_OF_KUNDUN: {
        mu_swprintf(TextList[TextNum], I18N::Game::DD, ip->Durability, 5);
        TextNum++;
        if (ip->Durability >= 5)
        {
            mu_swprintf(TextList[TextNum], I18N::Game::CanCreateLostMap);
            TextListColor[TextNum] = TEXT_COLOR_YELLOW;
            TextNum++;
        }
        else
        {
            mu_swprintf(TextList[TextNum], I18N::Game::DIsLackingToCreateLostMap,
                        (5 - ip->Durability));
            TextListColor[TextNum] = TEXT_COLOR_YELLOW;
            TextNum++;
        }
    }
    break;
    }

    return TextNum;
}

void CGMHellas::RenderObjectDescription()
{
    glColor3f(1.f, 1.f, 1.f);
    while (!g_qObjDes.empty())
    {
        ObjectDescript QD = g_qObjDes.front();

        int x, y;
        vec3_t Position;
        Vector(QD.m_vPos[0], QD.m_vPos[1], QD.m_vPos[2] + 370.f, Position);
        cameraProjection_.WorldToScreen(g_Camera, Position, &x, &y);

        if (x >= 0 && y >= 0)
        {
            g_RenderText.SetFont(LegacyFontRole::Bold);
            g_RenderText.SetTextColor(255, 230, 200, 255);
            g_RenderText.SetBgColor(100, 0, 0, 255);
            g_RenderText.RenderText(x, y, QD.m_strName, 0, 0, RT3_WRITE_CENTER);
        }

        g_qObjDes.pop();
    }
}

// every 4 seconds.

// every 2 seconds.

bool CGMHellas::RenderHellasObjectMesh(const ObjectDrawInput &input, BMD *b)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (draw.type == MODEL_BAHAMUT && gMapManager.InHellas())
    {
        Vector(0.0f, 0.0f, 0.0f, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        return true;
    }
    else if (draw.type == MODEL_WARCRAFT)
    {
        if (o->SubType == 1)
        {
            Vector(1.0f, 0.1f, 0.1f, b->BodyLight);
        }
        else
        {
            Vector(1.0f, 1.0f, 1.0f, b->BodyLight);
        }
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        return true;
    }
    else if (draw.type == MODEL_CUNDUN_DRAGON_HEAD)
    {
        Vector(0.3f, 0.3f, 0.3f, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);

        float Luminosity = (float)sin(WorldTime * 0.003f) * 0.2f + 0.8f;
        vec3_t p, Light, Position;
        Vector(0, 0, 0, p);
        Vector(Luminosity * 1.0f, Luminosity * 0.2f, Luminosity * 0.1f, Light);
        b->TransformPosition(draw.bones[3], p, Position, false);
        CreateSprite(BITMAP_ENERGY, Position, 0.2f, Light, o, 0.f);
        Vector(Luminosity * 1.0f, Luminosity * 0.2f, Luminosity * 0.1f, Light);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f, Light, o, 0.f);

        Vector(Luminosity * 1.0f, Luminosity * 0.2f, Luminosity * 0.1f, Light);
        b->TransformPosition(draw.bones[4], p, Position, false);
        CreateSprite(BITMAP_ENERGY, Position, 0.2f, Light, o, 0.f);
        Vector(Luminosity * 1.0f, Luminosity * 0.2f, Luminosity * 0.1f, Light);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f, Light, o, 0.f);

        return true;
    }
    else if (draw.type == MODEL_CUNDUN_GHOST)
    {
        Vector(1.0f, 1.0f, 1.0f, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE | RENDER_DARK, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, draw.hiddenMesh);
        b->RenderBody(RENDER_TEXTURE | RENDER_DARK, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, draw.hiddenMesh);
        Vector(0.0f, 0.0f, 0.0f, b->BodyLight);
        RenderPartObjectEdge(b, draw, RENDER_COLOR, true, 0.7f);

        float Luminosity = (float)sin(WorldTime * 0.003f) * 0.2f + 0.8f;
        vec3_t p, Light, Position;

        Vector(0, 0, 0, p);
        Vector(Luminosity * 1.0f, Luminosity * 0.2f, Luminosity * 0.1f, Light);
        b->TransformPosition(draw.bones[8], p, Position, false);
        CreateSprite(BITMAP_ENERGY, Position, 0.2f, Light, o, 0.f);
        Vector(Luminosity * 1.0f, Luminosity * 0.2f, Luminosity * 0.1f, Light);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f, Light, o, 0.f);

        Vector(Luminosity * 1.0f, Luminosity * 0.2f, Luminosity * 0.1f, Light);
        b->TransformPosition(draw.bones[9], p, Position, false);
        CreateSprite(BITMAP_ENERGY, Position, 0.2f, Light, o, 0.f);
        Vector(Luminosity * 1.0f, Luminosity * 0.2f, Luminosity * 0.1f, Light);
        CreateSprite(BITMAP_SHINY + 1, Position, 0.5f, Light, o, 0.f);

        return true;
    }
    else if (gMapManager.InHellas() == true &&
             (draw.type >= MODEL_WORLD_OBJECT && draw.type < MAX_WORLD_OBJECTS))
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);

        if (draw.type >= 0 && draw.type <= 66)
        {
            if (draw.type != 2 && draw.type != 4 && draw.type != 12 && draw.type != 14 &&
                draw.type != 15 && draw.type != 18 && draw.type != 20 && draw.type != 21 &&
                draw.type != 27 && draw.type != 29 && draw.type != 30 && draw.type != 31 &&
                draw.type != 32 && draw.type != 41 && draw.type != 43 && draw.type != 52 &&
                draw.type != 54 && draw.type != 55)
            {
                float Luminosity = sinf(WorldTime * 0.002f) * 0.1f + 0.3f;
                Vector(Luminosity, Luminosity, Luminosity, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE, 0.3f, draw.blendMesh, draw.blendLight, draw.blendU,
                              draw.blendV, draw.hiddenMesh, BITMAP_WATER + WaterTextureNumber);
            }

            if (draw.type == 34)
            {
                if (b->NumMeshs > 1)
                {
                    b->BeginRender(draw.alpha);
                    b->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                    b->EndRender();
                }
            }

            if (draw.type == 15 || draw.type == 29 || draw.type == 32)
            {
                DisableAlphaBlend();
                Vector(0.1f, 0.1f, 0.1f, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE | RENDER_SHADOWMAP, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                Vector(1.f, 1.f, 1.f, b->BodyLight);
            }
        }
        return true;
    }
    else
    {
        return RenderHellasMonsterObjectMesh(draw, b);
    }

    return false;
}

// every 4 seconds.

bool CGMHellas::RenderHellasMonsterObjectMesh(const ObjectDrawInput &input, BMD *b)
{
    auto draw = input;
    const auto *o = input.source;
    bool success = false;
    if (draw.type == MODEL_DEATH_ANGEL)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_METAL | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, BITMAP_CHROME);
        success = true;
    }
    else if (draw.type == MODEL_ILLUSION_OF_KUNDUN)
    {
        if (draw.action == MONSTER01_DIE && draw.animationFrame > 14.8f &&
            draw.priorAnimationFrame <= 14.8f)
        {
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV, draw.hiddenMesh);
        }
        else
        {
            if (o->LifeTime >= 90)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                Vector(0.4f, 0.4f, 0.3f, b->BodyLight);
                b->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderMesh(2, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderMesh(3, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderMesh(4, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderMesh(5, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderMesh(6, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);

                if (draw.action == MONSTER01_SHOCK)
                {
                    if ((draw.animationFrame > 3.5f && draw.animationFrame < 6.0f) ||
                        (draw.animationFrame > 8.0f && draw.animationFrame < 11.0f))
                    {
                        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                        b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                        b->RenderMesh(5, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                    }
                }
            }
        }
        success = true;
    }
    else if (draw.type == MODEL_CUNDUN_PART1 || draw.type == MODEL_CUNDUN_PART2 ||
             draw.type == MODEL_CUNDUN_PART3 || draw.type == MODEL_CUNDUN_PART4 ||
             draw.type == MODEL_CUNDUN_PART5 || draw.type == MODEL_CUNDUN_PART8)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        Vector(0.4f, 0.4f, 0.3f, b->BodyLight);
        b->RenderBody(RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, draw.hiddenMesh);
        success = true;
    }
    else if (draw.type == MODEL_BLOOD_SOLDIER)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        Vector(0.5f, 0.1f, 0.0f, b->BodyLight);
        b->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, draw.hiddenMesh);
        success = true;
    }
    else if (draw.type == MODEL_AEGIS)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        Vector(0.25f, 0.2f, 0.1f, b->BodyLight);
        b->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, draw.hiddenMesh);
        b->RenderMesh(2, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, draw.hiddenMesh);
        success = true;
    }
    else if (draw.type == MODEL_DEATH_CENTURION)
    {
        if (o->SubType == 9)
        {
            Vector(0.3f, 0.1f, 0.1f, b->BodyLight);
        }
        else
        {
        }
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        if (o->SubType == 9)
        {
            Vector(1.f, 0.6f, 0.3f, b->BodyLight);
            b->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, 0.5f, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV, draw.hiddenMesh);
            Vector(1.f, 1.f, 1.f, b->BodyLight);
            b->RenderMesh(1, RENDER_CHROME2 | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            Vector(1.f, 0.6f, 0.1f, b->BodyLight);
        }
        else
        {
            Vector(0.1f, 0.6f, 1.f, b->BodyLight);
        }
        b->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, draw.hiddenMesh);
        b->RenderMesh(2, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, draw.hiddenMesh);

        if (o->SubType == 9)
        {
        }
        success = true;
    }
    else if (draw.type == MODEL_NECRON)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);

        float Luminosity = sinf(WorldTime * 0.002f) * 0.3f + 0.7f;
        Vector(0.5f, Luminosity, 0.5f, b->BodyLight);
        b->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, 0.8f, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, draw.hiddenMesh);
        success = true;
    }
    else if (draw.type == MODEL_SHRIKER)
    {
        b->BeginRender(1.f);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, -1, draw.blendLight, draw.blendU, draw.blendV,
                      draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, -1, draw.blendLight, draw.blendU, draw.blendV,
                      draw.hiddenMesh);
        b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, -1, draw.blendLight, draw.blendU, draw.blendV,
                      draw.hiddenMesh);
        b->RenderMesh(5, RENDER_TEXTURE, draw.alpha, -1, draw.blendLight, draw.blendU, draw.blendV,
                      draw.hiddenMesh);

        float Luminosity = sinf(WorldTime * 0.002f) * 0.3f + 0.7f;
        if (o->SubType == 9)
        {
            draw.blendLight = 0.5f;

            Vector(Luminosity, Luminosity * 0.4f, Luminosity * 0.4f, b->BodyLight);
        }
        else
        {
            draw.blendLight = 1.f;
            Vector(Luminosity * 0.3f, Luminosity * 0.6f, Luminosity * 1.f, b->BodyLight);
        }
        b->RenderMesh(2, RENDER_WAVE | RENDER_BRIGHT, draw.alpha, 2, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(4, RENDER_WAVE | RENDER_BRIGHT, draw.alpha, 4, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);

        if (o->SubType == 9)
        {
            Vector(1.f, 1.f, 1.f, b->BodyLight);
            b->RenderMesh(0, RENDER_METAL | RENDER_BRIGHT, draw.alpha, -1, draw.blendLight,
                          draw.blendU, draw.blendV, draw.hiddenMesh);
            b->RenderMesh(1, RENDER_METAL | RENDER_BRIGHT, draw.alpha, -1, draw.blendLight,
                          draw.blendU, draw.blendV, draw.hiddenMesh);
            b->RenderMesh(3, RENDER_METAL | RENDER_BRIGHT, draw.alpha, -1, draw.blendLight,
                          draw.blendU, draw.blendV, draw.hiddenMesh);
        }
        else
        {
            float Light = sinf(WorldTime * 0.002f) * 0.3f + 0.7f;
            b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, 2, Light, draw.blendU, draw.blendV,
                          draw.hiddenMesh);
            b->RenderMesh(4, RENDER_TEXTURE, draw.alpha, 4, Light, draw.blendU, draw.blendV,
                          draw.hiddenMesh);
        }

        draw.blendLight = sinf(WorldTime * 0.002f) * 0.1f + 0.05f;
        b->RenderMesh(6, RENDER_TEXTURE, draw.alpha, 6, draw.blendLight, draw.blendU, draw.blendV,
                      draw.hiddenMesh);
        b->EndRender();

        success = true;
    }
    if (success)
    {
        if (g_isCharacterBuff(o, eBuff_WizDefense) || g_isCharacterBuff(o, eBuff_Defense))
        {
            float Luminosity = sinf(WorldTime * 0.001f) * 0.2f + 0.5f;

            if (g_isCharacterBuff(o, eBuff_WizDefense))
            {
                Vector(Luminosity * 0.1f, Luminosity * 0.3f, Luminosity * 0.6f, b->BodyLight);
            }
            else if (g_isCharacterBuff(o, eBuff_Defense))
            {
                Vector(Luminosity * 0.1f, Luminosity * 0.6f, Luminosity * 0.3f, b->BodyLight);
            }

            RenderPartObjectEdge(b, draw, RENDER_CHROME | RENDER_BRIGHT, true, 1.3f);
        }
    }

    return success;
}

bool CGMHellas::RenderObjectMesh(const ObjectDrawInput &input, BMD *model, bool extraMonster)
{
    const auto &draw = input;
    const auto *object = input.source;
    if (draw.type == MODEL_BAHAMUT ||
        (draw.type >= MODEL_WORLD_OBJECT && draw.type < MAX_WORLD_OBJECTS))
        return RenderHellasObjectMesh(draw, model);
    return false;
}

float CGMHellas::ItemDrawHeight(const OBJECT &object, int index)
{
    return GetWaterTerrain(object.Position[0], object.Position[1]) + 180.f;
}

// file    : GM_PK_Field.cpp

bool CGM_PK_Field::RenderObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (!gMapManager.IsPKField())
    {
        return false;
    }

    if (draw.type >= MODEL_ZOMBIE_FIGHTER && draw.type <= MODEL_BURNING_LAVA_GIANT)
    {
        RenderMonster(draw, b, ExtraMon);

        return true;
    }
    else if (draw.type == 15)
    {
        b->StreamMesh = 0;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      -(int)WorldTime % 10000 * 0.0001f);
        b->StreamMesh = -1;

        return true;
    }
    else if (draw.type == 67)
    {
        b->StreamMesh = 1;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      (int)WorldTime % 10000 * 0.0001f);
        b->StreamMesh = -1;
        vec3_t light;
        Vector(1.0f, 0.0f, 0.0f, light);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        VectorCopy(light, b->BodyLight);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, 0.2f, 0, 0.2f, draw.blendU, draw.blendV);

        return true;
    }
    else if (draw.type == 68)
    {
        b->StreamMesh = 1;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      (int)WorldTime % 10000 * 0.0001f);
        b->StreamMesh = -1;

        vec3_t light;
        Vector(1.0f, 0.0f, 0.0f, light);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        VectorCopy(light, b->BodyLight);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, 0.2f, 0, 0.2f, draw.blendU, draw.blendV);

        return true;
    }
    return false;
}

void CGM_PK_Field::RenderAfterObjectMesh(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (!gMapManager.IsPKField())
        return;

    switch (draw.type)
    {
    case 16: //song_lava2 fade in-out
    {
        float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, 0, fLumi, draw.blendU, draw.blendV,
                      draw.hiddenMesh);
    }
    break;
    default:
        break;
    }
}

bool CGM_PK_Field::RenderMonster(const ObjectDrawInput &input, BMD *b, bool ExtraMon)
{
    const auto &draw = input;
    const auto *o = input.source;
    if (!gMapManager.IsPKField())
    {
        return false;
    }

    auto fRotation = (float)((int)(WorldTime * 0.1f) % 360);
    float fAngle = (sinf(WorldTime * 0.004f) + 1.0f) * 0.4f + 0.2f;
    vec3_t vWorldPos, vLight;
    Vector(0.1f, 0.4f, 0.5f, vLight);

    switch (draw.type)
    {
    case MODEL_ZOMBIE_FIGHTER: {
        b->TransformByObjectBone(vWorldPos, draw, 9);
        CreateSprite(BITMAP_LIGHTMARKS, vWorldPos, fAngle, vLight, o, fRotation);

        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        DisableDepthTest();
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight * fAngle,
                      draw.blendU, draw.blendV);
        EnableDepthTest();
    }
        return true;
    case MODEL_GLADIATOR: {
        b->TransformByObjectBone(vWorldPos, draw, 9);
        CreateSprite(BITMAP_LIGHTMARKS, vWorldPos, fAngle, vLight, o, fRotation);

        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        DisableDepthTest();
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight * fAngle,
                      draw.blendU, draw.blendV);
        EnableDepthTest();
    }
        return true;
    case MODEL_SLAUGHTERER: {
        float fBlendMeshLight = 0.0f;
        fBlendMeshLight = (sinf(WorldTime * 0.003f) + 1.0f) * 0.5f;

        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fBlendMeshLight,
                      draw.blendU, draw.blendV, BITMAP_BUGBEAR_R);
    }
        return true;
    case MODEL_BLOOD_ASSASSIN:
    case MODEL_CRUEL_BLOOD_ASSASSIN: {
        if (draw.action != MONSTER01_DIE)
        {
            float fBlendMeshLight = 0.0f;
            fBlendMeshLight = (sinf(WorldTime * 0.005f) + 1.0f) * 0.3f + 0.3f;

            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);

            switch (draw.type)
            {
            case MODEL_BLOOD_ASSASSIN: {
                b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, 1, fBlendMeshLight, draw.blendU,
                              draw.blendV, BITMAP_PKMON02);

                b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, 3, fBlendMeshLight, draw.blendU,
                              draw.blendV, BITMAP_PKMON01);
            }
            break;
            case MODEL_CRUEL_BLOOD_ASSASSIN: {
                b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, 1, fBlendMeshLight, draw.blendU,
                              draw.blendV, BITMAP_PKMON04);

                b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, 3, fBlendMeshLight, draw.blendU,
                              draw.blendV, BITMAP_PKMON03);
            }
            break;
            }
            Vector(b->BodyLight[0] * 0.65f, b->BodyLight[0] * 0.65f, b->BodyLight[0] * 0.65f,
                   b->BodyLight);
            b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);

            Vector(b->BodyLight[0] * 2.0f, b->BodyLight[0] * 1.0f, b->BodyLight[0] * 0.4f,
                   b->BodyLight);
            b->RenderMesh(2, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
        }
    }
        return true;
    case MODEL_LAVA_GIANT:
    case MODEL_BURNING_LAVA_GIANT: {
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        float fBlendMeshLight = 0.0f;
        fBlendMeshLight = (sinf(WorldTime * 0.001f));

        switch (draw.type)
        {
        case MODEL_LAVA_GIANT:
        case MODEL_BURNING_LAVA_GIANT: {
            float fAlpha = 1.0f;
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);

            int iTexture = (draw.type == MODEL_LAVA_GIANT) ? BITMAP_PKMON06 : BITMAP_PKMON05;

            if (fBlendMeshLight < 0)
            {
                //fBlendMeshLight = fabs(fBlendMeshLight);
                fBlendMeshLight = -(fBlendMeshLight);
                //b->RenderMesh(0,RENDER_TEXTURE|RENDER_DARK, fAlpha, 0,fBlendMeshLight,draw.blendU,draw.blendV, iTexture);
                b->RenderMesh(0, RENDER_TEXTURE, fAlpha, 0, fBlendMeshLight, draw.blendU,
                              draw.blendV, iTexture);
            }
            else
            {
                for (int i = 0; i < 3; ++i)
                    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, fAlpha, 0, fBlendMeshLight,
                                  draw.blendU, draw.blendV, iTexture);
            }
        }
        break;
        }
    }
        return true;
    case MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_HEAD:
    case MODEL_PKFIELD_ASSASSIN_EFFECT_RED_HEAD: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
        return true;
    case MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_BODY:
    case MODEL_PKFIELD_ASSASSIN_EFFECT_RED_BODY: {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
        return true;
    }
    return false;
}

bool CGM_PK_Field::IsEarlyAfterCharacterObject(const OBJECT &object) const
{
    return object.Type == 16 || object.Type == 67 || object.Type == 68;
}

void CGM_PK_Field::RenderEarlyAfterCharacterObjects(OBJECT *head)
{
    for (OBJECT *object = head; object; object = object->Next)
        if (IsEarlyAfterCharacterObject(*object) &&
            TestFrustrum2D(object->Position[0] * 0.01f, object->Position[1] * 0.01f, -600.f))
            RenderObject_AfterCharacter(object);
}

// Terrain ���� �Լ�

/*
#ifndef BATTLE_CASTLE
    void CreateGround(int Type,int x,int y,float Angle)
    {
        for(int i=0;i<MAX_GROUNDS;i++)
        {
            GROUND *o = &Grounds[i];
            if(!o->Live)
            {
                o->Live  = true;
                o->Type  = Type;
                o->x     = x;
                o->y     = y;
                o->Angle = (unsigned char)(Angle/360.f*255.f);
                return;
            }
        }
    }

    void DeleteGround(int x,int y)
    {
        for(int i=0;i<MAX_GROUNDS;i++)
        {
            GROUND *o = &Grounds[i];
            if(o->Live)
            {
                if(o->x==x && o->y==y) o->Live = false;
            }
        }
    }

    void RenderGrounds()
    {
        for(int i=0;i<MAX_GROUNDS;i++)
        {
            GROUND *o = &Grounds[i];
            if(o->Live)
            {
                float Angle = (float)o->Angle/255.f*360.f;
                RenderTerrainBitmap(BITMAP_CURSOR+6,o->x,o->y,Angle);
            }
        }
    }
#endif// BATTLE_CASTLE
*/

void SessionRenderUnit::RenderFace(int Texture, int mx, int my)
{
    if (!PrepareTerrainBaseMaterial(Texture))
    {
        return;
    }

    const int terrainIndices[4] = {TerrainIndex1, TerrainIndex2, TerrainIndex3, TerrainIndex4};
    const std::array<float, 3> &normal = LegacyRender().CurrentNormal();
    (void)LegacyRender().WriteTriangleFan(4, [&](std::span<RenderTapeVertex> vertices) noexcept {
        RenderTapeVertex *target = vertices.data();
        for (int index = 0; index < 4; ++index)
        {
            const float *position = TerrainVertex[index];
            const float *textureCoordinate = TerrainTextureCoord[index];
            const float *light = PrimaryTerrainLight[terrainIndices[index]];
            *target++ = {{position[0], position[1], position[2], 1.0F},
                         {textureCoordinate[0], textureCoordinate[1]},
                         {light[0], light[1], light[2], 1.0F},
                         normal};
        }
    });
    glTexCoord2f(TerrainTextureCoord[3][0], TerrainTextureCoord[3][1]);
    glColor3fv(PrimaryTerrainLight[TerrainIndex4]);
}

void SessionRenderUnit::RenderFace_After(int Texture, int mx, int my)
{
    if (Texture == 100)
        EnableAlphaTest();
    else if (Texture == 101)
        EnableAlphaBlend();
    else
        return;

    BindTexture(BITMAP_MAPTILE + Texture);

    const int terrainIndices[4] = {TerrainIndex1, TerrainIndex2, TerrainIndex3, TerrainIndex4};
    const std::array<float, 3> &normal = LegacyRender().CurrentNormal();
    (void)LegacyRender().WriteTriangleFan(4, [&](std::span<RenderTapeVertex> vertices) noexcept {
        RenderTapeVertex *target = vertices.data();
        for (int index = 0; index < 4; ++index)
        {
            const float *position = TerrainVertex[index];
            const float *textureCoordinate = TerrainTextureCoord[index];
            const float *light = PrimaryTerrainLight[terrainIndices[index]];
            *target++ = {{position[0], position[1], position[2], 1.0F},
                         {textureCoordinate[0], textureCoordinate[1]},
                         {light[0], light[1], light[2], 1.0F},
                         normal};
        }
    });
    glTexCoord2f(TerrainTextureCoord[3][0], TerrainTextureCoord[3][1]);
    glColor3fv(PrimaryTerrainLight[TerrainIndex4]);
}

void SessionRenderUnit::RenderFaceAlpha(int Texture, int mx, int my)
{
    EnableAlphaTest();
    BindTexture(BITMAP_MAPTILE + Texture);
    const int terrainIndices[4] = {TerrainIndex1, TerrainIndex2, TerrainIndex3, TerrainIndex4};
    const std::array<float, 3> &normal = LegacyRender().CurrentNormal();
    (void)LegacyRender().WriteTriangleFan(4, [&](std::span<RenderTapeVertex> vertices) noexcept {
        RenderTapeVertex *target = vertices.data();
        for (int index = 0; index < 4; ++index)
        {
            const int terrainIndex = terrainIndices[index];
            const float *position = TerrainVertex[index];
            const float *textureCoordinate = TerrainTextureCoord[index];
            const float *light = PrimaryTerrainLight[terrainIndex];
            *target++ = {{position[0], position[1], position[2], 1.0F},
                         {textureCoordinate[0], textureCoordinate[1]},
                         {light[0], light[1], light[2], TerrainMappingAlpha[terrainIndex]},
                         normal};
        }
    });
    glTexCoord2f(TerrainTextureCoord[3][0], TerrainTextureCoord[3][1]);
    const float *finalLight = PrimaryTerrainLight[TerrainIndex4];
    glColor4f(finalLight[0], finalLight[1], finalLight[2], TerrainMappingAlpha[TerrainIndex4]);
}

void SessionRenderUnit::RenderFaceBlend(int Texture, int mx, int my)
{
    EnableAlphaBlend();
    BindTexture(BITMAP_MAPTILE + Texture);
    const int terrainIndices[4] = {TerrainIndex1, TerrainIndex2, TerrainIndex3, TerrainIndex4};
    const std::array<float, 3> &normal = LegacyRender().CurrentNormal();
    (void)LegacyRender().WriteTriangleFan(4, [&](std::span<RenderTapeVertex> vertices) noexcept {
        RenderTapeVertex *target = vertices.data();
        for (int index = 0; index < 4; ++index)
        {
            const int terrainIndex = terrainIndices[index];
            const float *position = TerrainVertex[index];
            const float *textureCoordinate = TerrainTextureCoord[index];
            const float light = TerrainMappingAlpha[terrainIndex];
            *target++ = {{position[0], position[1], position[2], 1.0F},
                         {textureCoordinate[0], textureCoordinate[1]},
                         {light, light, light, 1.0F},
                         normal};
        }
    });
    glTexCoord2f(TerrainTextureCoord[3][0], TerrainTextureCoord[3][1]);
    const float finalLight = TerrainMappingAlpha[TerrainIndex4];
    glColor3f(finalLight, finalLight, finalLight);
}

void SessionRenderUnit::RenderTerrainFace(float xf, float yf, int xi, int yi, float lodf)
{
    const auto &policy = TheMapProcess().TerrainPolicy();
    const bool magmaWorld = policy.magma;
    const bool atlansWorld = policy.ocean;

    if (TerrainFlag != TERRAIN_MAP_GRASS)
    {
        int Texture;
        bool Alpha;
        bool Water = false;
        if (TerrainMappingAlpha[TerrainIndex1] >= 1.f &&
            TerrainMappingAlpha[TerrainIndex2] >= 1.f &&
            TerrainMappingAlpha[TerrainIndex3] >= 1.f && TerrainMappingAlpha[TerrainIndex4] >= 1.f)
        {
            Texture = TerrainMappingLayer2[TerrainIndex1];
            Alpha = false;
        }
        else
        {
            Texture = TerrainMappingLayer1[TerrainIndex1];
            Alpha = true;
            if (Texture == 5)
            {
                Water = true;
            }
            if (Texture == 11 && magmaWorld)
                Water = true;
        }
        FaceTexture(Texture, xf, yf, Water, false);
        RenderFace(Texture, xi, yi);

        if (TerrainMappingAlpha[TerrainIndex1] > 0.f || TerrainMappingAlpha[TerrainIndex2] > 0.f ||
            TerrainMappingAlpha[TerrainIndex3] > 0.f || TerrainMappingAlpha[TerrainIndex4] > 0.f)
        {
            if (atlansWorld && TerrainMappingLayer2[TerrainIndex1] == 5)
            {
                Texture = BITMAP_WATER - BITMAP_MAPTILE + WaterTextureNumber;
                FaceTexture(Texture, xf, yf, false, true);
                RenderFaceBlend(Texture, xi, yi);
            }
            else if (Alpha)
            {
                Texture = TerrainMappingLayer2[TerrainIndex1];
                if (Texture != 5)
                    Water = false;
                if (Texture != 255)
                {
                    FaceTexture(Texture, xf, yf, Water, false);
                    RenderFaceAlpha(Texture, xi, yi);
                }
            }
        }
    }
    else
    {
        if (TerrainMappingAlpha[TerrainIndex1] > 0.f || TerrainMappingAlpha[TerrainIndex2] > 0.f ||
            TerrainMappingAlpha[TerrainIndex3] > 0.f || TerrainMappingAlpha[TerrainIndex4] > 0.f)
        {
            return;
        }
        if (CurrentLayer == 0 && policy.grassFaces)
        {
            int Texture = BITMAP_MAPGRASS + TerrainMappingLayer1[TerrainIndex1];

            const auto texture = Bitmaps.GetTextureProperties(Texture);
            if (texture)
            {
                float Height = texture->height * 2.f;
                BindTexture(Texture);

                if (magmaWorld)
                    EnableAlphaBlend();

                float Width = 64.f / 256.f;
                float su = xf * Width;
                su += TerrainGrassTexture[yi & TERRAIN_SIZE_MASK];
                TEXCOORD(TerrainTextureCoord[0], su, 0.f);
                TEXCOORD(TerrainTextureCoord[1], su + Width, 0.f);
                TEXCOORD(TerrainTextureCoord[2], su + Width, 1.f);
                TEXCOORD(TerrainTextureCoord[3], su, 1.f);
                VectorCopy(TerrainVertex[0], TerrainVertex[3]);
                VectorCopy(TerrainVertex[2], TerrainVertex[1]);
                TerrainVertex[0][2] += Height;
                TerrainVertex[1][2] += Height;
                TerrainVertex[0][0] += -50.f;
                TerrainVertex[1][0] += -50.f;
#ifdef ASG_ADD_MAP_KARUTAN
                if (TheMapProcess().TerrainPolicy().secondaryGrassWind)
                {
                    TerrainVertex[0][1] += g_fTerrainGrassWind1[TerrainIndex1];
                    TerrainVertex[1][1] += g_fTerrainGrassWind1[TerrainIndex2];
                }
                else
                {
#endif // ASG_ADD_MAP_KARUTAN
                    TerrainVertex[0][1] += TerrainGrassWind[TerrainIndex1];
                    TerrainVertex[1][1] += TerrainGrassWind[TerrainIndex2];
#ifdef ASG_ADD_MAP_KARUTAN
                }
#endif // ASG_ADD_MAP_KARUTAN
                glBegin(GL_QUADS);
                glTexCoord2f(TerrainTextureCoord[0][0], TerrainTextureCoord[0][1]);
                glColor3fv(PrimaryTerrainLight[TerrainIndex1]);
                glVertex3fv(TerrainVertex[0]);
                glTexCoord2f(TerrainTextureCoord[1][0], TerrainTextureCoord[1][1]);
                glColor3fv(PrimaryTerrainLight[TerrainIndex2]);
                glVertex3fv(TerrainVertex[1]);
                glTexCoord2f(TerrainTextureCoord[2][0], TerrainTextureCoord[2][1]);
                glColor3fv(PrimaryTerrainLight[TerrainIndex3]);
                glVertex3fv(TerrainVertex[2]);
                glTexCoord2f(TerrainTextureCoord[3][0], TerrainTextureCoord[3][1]);
                glColor3fv(PrimaryTerrainLight[TerrainIndex4]);
                glVertex3fv(TerrainVertex[3]);
                glEnd();

                if (magmaWorld)
                    DisableAlphaBlend();
            }
        }
    }
}

void SessionRenderUnit::RenderTerrainFace_After(float xf, float yf, int xi, int yi, float lodf)
{
    if (TerrainFlag != TERRAIN_MAP_GRASS)
    {
        int Texture;
        int Water = 0;
        if (TerrainMappingAlpha[TerrainIndex1] >= 1.f &&
            TerrainMappingAlpha[TerrainIndex2] >= 1.f &&
            TerrainMappingAlpha[TerrainIndex3] >= 1.f && TerrainMappingAlpha[TerrainIndex4] >= 1.f)
        {
            Texture = TerrainMappingLayer2[TerrainIndex1];
        }
        else
        {
            Texture = TerrainMappingLayer1[TerrainIndex1];
            if (TerrainMappingLayer1[TerrainIndex1] == 5)
                Water = 1;
        }

        FaceTexture(Texture, xf, yf, Water, false);
        RenderFace_After(Texture, xi, yi);
    }
}

bool SessionRenderUnit::RenderTerrainTile(float xf, float yf, int xi, int yi, float lodf, int lodi,
                                          bool Flag)
{
    TerrainIndex1 = TERRAIN_INDEX(xi, yi);
    if ((TerrainWall[TerrainIndex1] & TW_NOGROUND) == TW_NOGROUND && !Flag)
        return false;

    TerrainIndex2 = TERRAIN_INDEX(xi + lodi, yi);
    TerrainIndex3 = TERRAIN_INDEX(xi + lodi, yi + lodi);
    TerrainIndex4 = TERRAIN_INDEX(xi, yi + lodi);

    float sx = xf * TERRAIN_SCALE;
    float sy = yf * TERRAIN_SCALE;

    Vector(sx, sy, BackTerrainHeight[TerrainIndex1], TerrainVertex[0]);
    Vector(sx + TERRAIN_SCALE, sy, BackTerrainHeight[TerrainIndex2], TerrainVertex[1]);
    Vector(sx + TERRAIN_SCALE, sy + TERRAIN_SCALE, BackTerrainHeight[TerrainIndex3],
           TerrainVertex[2]);
    Vector(sx, sy + TERRAIN_SCALE, BackTerrainHeight[TerrainIndex4], TerrainVertex[3]);

    if ((TerrainWall[TerrainIndex1] & TW_HEIGHT) == TW_HEIGHT)
        TerrainVertex[0][2] = g_fSpecialHeight;
    if ((TerrainWall[TerrainIndex2] & TW_HEIGHT) == TW_HEIGHT)
        TerrainVertex[1][2] = g_fSpecialHeight;
    if ((TerrainWall[TerrainIndex3] & TW_HEIGHT) == TW_HEIGHT)
        TerrainVertex[2][2] = g_fSpecialHeight;
    if ((TerrainWall[TerrainIndex4] & TW_HEIGHT) == TW_HEIGHT)
        TerrainVertex[3][2] = g_fSpecialHeight;

    if (!Flag)
    {
        RenderTerrainFace(xf, yf, xi, yi, lodf);
#ifdef SHOW_PATH_INFO
#ifdef CSK_DEBUG_MAP_PATHFINDING
        if (g_bShowPath == true)
#endif // CSK_DEBUG_MAP_PATHFINDING
        {
            if (2 <= path.GetClosedStatus(TerrainIndex1))
            {
                EnableAlphaTest();
                DisableTexture();
                glBegin(GL_TRIANGLE_FAN);
                if (4 <= path.GetClosedStatus(TerrainIndex1))
                {
                    glColor4f(0.3f, 0.3f, 1.0f, 0.5f);
                }
                else
                {
                    glColor4f(1.0f, 1.0f, 1.0f, 0.3f);
                }
                for (int i = 0; i < 4; i++)
                {
                    glVertex3fv(TerrainVertex[i]);
                }
                glEnd();
                DisableAlphaBlend();
            }
        }
#endif // SHOW_PATH_INFO
    }
    else
    {
#ifdef _DEBUG
        if (EditFlag != EDIT_LIGHT)
        {
            DisableTexture();
            glColor3f(0.5f, 0.5f, 0.5f);
            glBegin(GL_LINE_STRIP);
            for (int i = 0; i < 4; i++)
            {
                glVertex3fv(TerrainVertex[i]);
            }
            glEnd();
            DisableAlphaBlend();
        }
#endif // _DEBUG

#ifdef CSK_DEBUG_MAP_ATTRIBUTE
        if (EditFlag == EDIT_WALL &&
            ((SelectWall == 0 && (TerrainWall[TerrainIndex1] & TW_NOMOVE) == TW_NOMOVE) ||
             (SelectWall == 2 && (TerrainWall[TerrainIndex1] & TW_SAFEZONE) == TW_SAFEZONE) ||
             (SelectWall == 6 && (TerrainWall[TerrainIndex1] & TW_CAMERA_UP) == TW_CAMERA_UP) ||
             (SelectWall == 7 &&
              (TerrainWall[TerrainIndex1] & TW_NOATTACKZONE) == TW_NOATTACKZONE) ||
             (SelectWall == 8 && (TerrainWall[TerrainIndex1] & TW_ATT1) == TW_ATT1) ||
             (SelectWall == 9 && (TerrainWall[TerrainIndex1] & TW_ATT2) == TW_ATT2) ||
             (SelectWall == 10 && (TerrainWall[TerrainIndex1] & TW_ATT3) == TW_ATT3) ||
             (SelectWall == 11 && (TerrainWall[TerrainIndex1] & TW_ATT4) == TW_ATT4) ||
             (SelectWall == 12 && (TerrainWall[TerrainIndex1] & TW_ATT5) == TW_ATT5) ||
             (SelectWall == 13 && (TerrainWall[TerrainIndex1] & TW_ATT6) == TW_ATT6) ||
             (SelectWall == 14 && (TerrainWall[TerrainIndex1] & TW_ATT7) == TW_ATT7)))
        {
            DisableDepthTest();
            EnableAlphaTest();
            DisableTexture();

            glBegin(GL_TRIANGLE_FAN);
            glColor4f(1.f, 0.5f, 0.5f, 0.3f);
            for (int i = 0; i < 4; i++)
            {
                glVertex3fv(TerrainVertex[i]);
            }
            glEnd();

            DisableAlphaBlend();
        }
#endif // CSK_DEBUG_MAP_ATTRIBUTE

        return Success;
    }
    return false;
}

void SessionRenderUnit::RenderTerrainTile_After(float xf, float yf, int xi, int yi, float lodf,
                                                int lodi, bool Flag)
{
    TerrainIndex1 = TERRAIN_INDEX(xi, yi);
    TerrainIndex2 = TERRAIN_INDEX(xi + lodi, yi);
    TerrainIndex3 = TERRAIN_INDEX(xi + lodi, yi + lodi);
    TerrainIndex4 = TERRAIN_INDEX(xi, yi + lodi);

    float sx = xf * TERRAIN_SCALE;
    float sy = yf * TERRAIN_SCALE;

    Vector(sx, sy, BackTerrainHeight[TerrainIndex1], TerrainVertex[0]);
    Vector(sx + TERRAIN_SCALE, sy, BackTerrainHeight[TerrainIndex2], TerrainVertex[1]);
    Vector(sx + TERRAIN_SCALE, sy + TERRAIN_SCALE, BackTerrainHeight[TerrainIndex3],
           TerrainVertex[2]);
    Vector(sx, sy + TERRAIN_SCALE, BackTerrainHeight[TerrainIndex4], TerrainVertex[3]);

    if ((TerrainWall[TerrainIndex1] & TW_HEIGHT) == TW_HEIGHT)
        TerrainVertex[0][2] = g_fSpecialHeight;
    if ((TerrainWall[TerrainIndex2] & TW_HEIGHT) == TW_HEIGHT)
        TerrainVertex[1][2] = g_fSpecialHeight;
    if ((TerrainWall[TerrainIndex3] & TW_HEIGHT) == TW_HEIGHT)
        TerrainVertex[2][2] = g_fSpecialHeight;
    if ((TerrainWall[TerrainIndex4] & TW_HEIGHT) == TW_HEIGHT)
        TerrainVertex[3][2] = g_fSpecialHeight;

    if (!Flag)
    {
        if ((TerrainWall[TerrainIndex1] & TW_NOGROUND) != TW_NOGROUND)
            RenderTerrainFace_After(xf, yf, xi, yi, lodf);
    }
}

void SessionRenderUnit::RenderTerrainBitmapTile(float xf, float yf, float lodf, int lodi,
                                                vec3_t c[4], bool LightEnable, float Alpha,
                                                float Height, std::uint64_t quadRunId)
{
    int xi = (int)xf;
    int yi = (int)yf;
    if (xi < 0 || yi < 0 || xi >= TERRAIN_SIZE_MASK || yi >= TERRAIN_SIZE_MASK)
        return;
    float TileScale = TERRAIN_SCALE * lodf;
    float sx = xf * TERRAIN_SCALE;
    float sy = yf * TERRAIN_SCALE;
    TerrainIndex1 = TERRAIN_INDEX(xi, yi);
    TerrainIndex2 = TERRAIN_INDEX(xi + lodi, yi);
    TerrainIndex3 = TERRAIN_INDEX(xi + lodi, yi + lodi);
    TerrainIndex4 = TERRAIN_INDEX(xi, yi + lodi);
    Vector(sx, sy, BackTerrainHeight[TerrainIndex1] + Height, TerrainVertex[0]);
    Vector(sx + TileScale, sy, BackTerrainHeight[TerrainIndex2] + Height, TerrainVertex[1]);
    Vector(sx + TileScale, sy + TileScale, BackTerrainHeight[TerrainIndex3] + Height,
           TerrainVertex[2]);
    Vector(sx, sy + TileScale, BackTerrainHeight[TerrainIndex4] + Height, TerrainVertex[3]);

    if (!LightEnable)
    {
        RenderTapeQuadInstance instance{};
        for (std::size_t index = 0; index < instance.corners.size(); ++index)
        {
            instance.corners[index] = {TerrainVertex[index][0], TerrainVertex[index][1],
                                       TerrainVertex[index][2], c[index][0]};
            instance.v[index] = c[index][1];
        }
        instance.color = LegacyRender().CurrentColor();
        (void)DrawEffectQuadInstance(instance, quadRunId);
        (void)LegacyRender().TexCoord2(c[3][0], c[3][1]);
        return;
    }

    vec3_t Light[4];
    if (LightEnable)
    {
        VectorCopy(PrimaryTerrainLight[TerrainIndex1], Light[0]);
        VectorCopy(PrimaryTerrainLight[TerrainIndex2], Light[1]);
        VectorCopy(PrimaryTerrainLight[TerrainIndex3], Light[2]);
        VectorCopy(PrimaryTerrainLight[TerrainIndex4], Light[3]);
    }

    glBegin(GL_TRIANGLE_FAN);
    for (int i = 0; i < 4; i++)
    {
        if (LightEnable)
        {
            if (Alpha == 1.f)
                glColor3fv(Light[i]);
            else
                glColor4f(Light[i][0], Light[i][1], Light[i][2], Alpha);
        }
        glTexCoord2f(c[i][0], c[i][1]);
        glVertex3fv(TerrainVertex[i]);
    }
    glEnd();
}

void SessionRenderUnit::RenderTerrainBitmap(int Texture, int mxi, int myi, float Rotation)
{
    glColor3f(1.f, 1.f, 1.f);

    vec3_t Angle;
    Vector(0.f, 0.f, Rotation, Angle);
    float Matrix[3][4];
    AngleMatrix(Angle, Matrix);

    BindTexture(Texture);
    const auto texture = Bitmaps.GetTextureProperties(Texture);
    if (!texture)
    {
        return;
    }
    float TexScaleU = 64.f / texture->width;
    float TexScaleV = 64.f / texture->height;
    for (float y = 0.f; y < texture->height / 64.f; y += 1.f)
    {
        for (float x = 0.f; x < texture->width / 64.f; x += 1.f)
        {
            vec3_t p1[4], p2[4];
            Vector((x)*TexScaleU, (y)*TexScaleV, 0.f, p1[0]);
            Vector((x + 1.f) * TexScaleU, (y)*TexScaleV, 0.f, p1[1]);
            Vector((x + 1.f) * TexScaleU, (y + 1.f) * TexScaleV, 0.f, p1[2]);
            Vector((x)*TexScaleU, (y + 1.f) * TexScaleV, 0.f, p1[3]);
            for (int i = 0; i < 4; i++)
            {
                p1[i][0] -= 0.5f;
                p1[i][1] -= 0.5f;
                VectorRotate(p1[i], Matrix, p2[i]);
                p2[i][0] += 0.5f;
                p2[i][1] += 0.5f;
            }
            RenderTerrainBitmapTile((float)mxi + x, (float)myi + y, 1.f, 1, p2, true, 1.f);
        }
    }
}

void SessionRenderUnit::RenderTerrainAlphaBitmap(int Texture, float xf, float yf, float SizeX,
                                                 float SizeY, const vec3_t Light, float Rotation,
                                                 float Alpha, float Height)
{
    if (Alpha == 1.f)
        glColor3fv(Light);
    else
        glColor4f(Light[0], Light[1], Light[2], Alpha);

    vec3_t Angle;
    Vector(0.f, 0.f, Rotation, Angle);
    float Matrix[3][4];
    AngleMatrix(Angle, Matrix);

    BindTexture(Texture);
    const std::uint64_t quadRunId = LegacyRender().BeginQuadInstanceRun();
    float mxf = (xf / TERRAIN_SCALE);
    float myf = (yf / TERRAIN_SCALE);
    int mxi = (int)(mxf);
    int myi = (int)(myf);

    float Size;
    if (SizeX >= SizeY)
        Size = SizeX;
    else
        Size = SizeY;
    float TexU = (((float)mxi - mxf) + 0.5f * Size);
    float TexV = (((float)myi - myf) + 0.5f * Size);
    float TexScaleU = 1.f / Size;
    float TexScaleV = 1.f / Size;
    Size = (float)((int)Size + 1);
    float Aspect = SizeX / SizeY;
    const float halfUv = 0.5f;
    const float halfTileU = TexScaleU * halfUv;
    const float halfTileV = TexScaleV * halfUv;
    const float tileAxisXU = Aspect * Matrix[0][0];
    const float tileAxisXV = Matrix[1][0];
    const float tileAxisYU = Aspect * Matrix[0][1];
    const float tileAxisYV = Matrix[1][1];
    // Separating-axis projections keep rotated UV quads outside the texture from being recorded.
    const float uvAxisRadius = halfUv + fabsf(tileAxisXU) * halfTileU +
                               fabsf(tileAxisYU) * halfTileV;
    const float uvVerticalRadius = halfUv + fabsf(tileAxisXV) * halfTileU +
                                   fabsf(tileAxisYV) * halfTileV;
    const float tileAxisXNormalU = -tileAxisXV;
    const float tileAxisXNormalV = tileAxisXU;
    const float tileAxisYNormalU = -tileAxisYV;
    const float tileAxisYNormalV = tileAxisYU;
    const float tileAxisXNormalRadius =
        fabsf(tileAxisXNormalU * tileAxisXU + tileAxisXNormalV * tileAxisXV) * halfTileU +
        fabsf(tileAxisXNormalU * tileAxisYU + tileAxisXNormalV * tileAxisYV) * halfTileV +
        halfUv * (fabsf(tileAxisXNormalU) + fabsf(tileAxisXNormalV));
    const float tileAxisYNormalRadius =
        fabsf(tileAxisYNormalU * tileAxisXU + tileAxisYNormalV * tileAxisXV) * halfTileU +
        fabsf(tileAxisYNormalU * tileAxisYU + tileAxisYNormalV * tileAxisYV) * halfTileV +
        halfUv * (fabsf(tileAxisYNormalU) + fabsf(tileAxisYNormalV));
    for (float y = -Size; y <= Size; y += 1.f)
    {
        for (float x = -Size; x <= Size; x += 1.f)
        {
            const float tileCenterX = (TexU + x + halfUv) * TexScaleU - halfUv;
            const float tileCenterY = (TexV + y + halfUv) * TexScaleV - halfUv;
            const float centerU =
                Aspect * (tileCenterX * Matrix[0][0] + tileCenterY * Matrix[0][1]);
            const float centerV = tileCenterX * Matrix[1][0] + tileCenterY * Matrix[1][1];
            if (fabsf(centerU) >= uvAxisRadius || fabsf(centerV) >= uvVerticalRadius ||
                fabsf(centerU * tileAxisXNormalU + centerV * tileAxisXNormalV) >=
                    tileAxisXNormalRadius ||
                fabsf(centerU * tileAxisYNormalU + centerV * tileAxisYNormalV) >=
                    tileAxisYNormalRadius)
            {
                continue;
            }

            vec3_t p1[4], p2[4];
            Vector((TexU + x) * TexScaleU, (TexV + y) * TexScaleV, 0.f, p1[0]);
            Vector((TexU + x + 1.f) * TexScaleU, (TexV + y) * TexScaleV, 0.f, p1[1]);
            Vector((TexU + x + 1.f) * TexScaleU, (TexV + y + 1.f) * TexScaleV, 0.f, p1[2]);
            Vector((TexU + x) * TexScaleU, (TexV + y + 1.f) * TexScaleV, 0.f, p1[3]);
            for (int i = 0; i < 4; i++)
            {
                p1[i][0] -= 0.5f;
                p1[i][1] -= 0.5f;
                VectorRotate(p1[i], Matrix, p2[i]);
                p2[i][0] *= Aspect;
                p2[i][0] += 0.5f;
                p2[i][1] += 0.5f;
            }
            RenderTerrainBitmapTile((float)mxi + x, (float)myi + y, 1.f, 1, p2, false, Alpha,
                                    Height, quadRunId);
        }
    }
}

// Compute FrustrumBound{Min,Max}{X,Y} from the current FrustrumX/Y/Count hull.
// Snaps to a TERRAIN_ITERATION_TILE grid and clamps to valid terrain range.

// Build a CW convex hull from points projected to tile-space XY, storing into
// FrustrumX/Y/Count, then compute iteration bounds.

// Expand CW convex hull outward by `offset` tiles.
// Compensates for TestFrustrum2D only checking tile centers — tiles at the hull
// boundary whose centers are just outside would otherwise be culled even though
// part of the tile is visible. Expanding by ~1 tile ensures full coverage.

/**
 * @brief Renders a wireframe sphere for debugging culling volumes
 * @param center Center position of the sphere in world space
 * @param radius Radius of the sphere
 * @param r Red color component (0-1)
 * @param g Green color component (0-1)
 * @param b Blue color component (0-1)
 */
void SessionRenderUnit::RenderDebugSphere(const vec3_t center, float radius, float r, float g,
                                          float b)
{
    (void)facade_.PushAttrib();

    // Disable unnecessary features
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST); // Keep depth test for proper 3D rendering

    glColor3f(r, g, b);
    glLineWidth(1.0f);

    // Draw three circle rings (XY, XZ, YZ planes) to represent the sphere
    const int segments = 16; // Number of line segments per circle
    const float angleStep = (2.0f * Q_PI) / segments;

    // XY plane circle (around Z axis)
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < segments; i++)
    {
        float angle = i * angleStep;
        float x = center[0] + radius * cosf(angle);
        float y = center[1] + radius * sinf(angle);
        glVertex3f(x, y, center[2]);
    }
    glEnd();

    // XZ plane circle (around Y axis)
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < segments; i++)
    {
        float angle = i * angleStep;
        float x = center[0] + radius * cosf(angle);
        float z = center[2] + radius * sinf(angle);
        glVertex3f(x, center[1], z);
    }
    glEnd();

    // YZ plane circle (around X axis)
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < segments; i++)
    {
        float angle = i * angleStep;
        float y = center[1] + radius * cosf(angle);
        float z = center[2] + radius * sinf(angle);
        glVertex3f(center[0], y, z);
    }
    glEnd();

    (void)facade_.PopAttrib();
}

void SessionRenderUnit::RenderDebugBox(const vec3_t origin, float sizeX, float sizeY, float sizeZ,
                                       float r, float g, float b)
{
    (void)facade_.PushAttrib();

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);

    glColor3f(r, g, b);
    glLineWidth(1.0f);

    float x0 = origin[0], y0 = origin[1], z0 = origin[2];
    float x1 = x0 + sizeX, y1 = y0 + sizeY, z1 = z0 + sizeZ;

    // Bottom face
    glBegin(GL_LINE_LOOP);
    glVertex3f(x0, y0, z0);
    glVertex3f(x1, y0, z0);
    glVertex3f(x1, y1, z0);
    glVertex3f(x0, y1, z0);
    glEnd();

    // Top face
    glBegin(GL_LINE_LOOP);
    glVertex3f(x0, y0, z1);
    glVertex3f(x1, y0, z1);
    glVertex3f(x1, y1, z1);
    glVertex3f(x0, y1, z1);
    glEnd();

    // Vertical edges
    glBegin(GL_LINES);
    glVertex3f(x0, y0, z0);
    glVertex3f(x0, y0, z1);
    glVertex3f(x1, y0, z0);
    glVertex3f(x1, y0, z1);
    glVertex3f(x1, y1, z0);
    glVertex3f(x1, y1, z1);
    glVertex3f(x0, y1, z0);
    glVertex3f(x0, y1, z1);
    glEnd();

    (void)facade_.PopAttrib();
}

/*bool TestFrustrum(vec3_t Position,float Range)
{
    int j = 3;
    for(int i=0;i<4;j=i,i++)
    {
        float d = (Frustrum[i][0]-Position[0]) * (Frustrum[j][1]-Position[1]) -
                  (Frustrum[j][0]-Position[0]) * (Frustrum[i][1]-Position[1]);
        if(d < 0.f) return false;
    }
    return true;
}*/

void SessionRenderUnit::RenderTerrainBlock(float xf, float yf, int xi, int yi, bool EditFlag)
{
    RenderTerrainBlockVisible(xf, yf, xi, yi, EditFlag, 0, false);
}

void SessionRenderUnit::RenderTerrainBlockVisible(float xf, float yf, int xi, int yi, bool EditFlag,
                                                  std::uint16_t visibleTiles, bool visibilityKnown)
{
    //int x = ((xi/4)&63);
    //int y = ((yi/4)&63);
    int lodi = 1;
    auto lodf = (float)lodi;
    for (int i = 0; i < 4; i += lodi)
    {
        float temp = xf;
        for (int j = 0; j < 4; j += lodi)
        {
            const int tileOffset = i * 4 + j;
            const bool visible = visibilityKnown ? (visibleTiles & (1U << tileOffset)) != 0
                                                 : TestFrustrum2D(xf + 0.5f, yf + 0.5f, 0.f);
            if (visible)
            {
                RenderTerrainTile(xf, yf, xi + j, yi + i, lodf, lodi, EditFlag);
            }
            xf += lodf;
        }
        xf = temp;
        yf += lodf;
    }
}

bool SessionRenderUnit::PrepareWorldTerrainOnOwner(SessionGeneration generation) noexcept
{
    preparedPersistentTerrain_ = false;
    if (SceneFlag == MAIN_SCENE)
    {
        if (!CanRenderMainScene())
            return true;
    }
    else if (SceneFlag == LOG_IN_SCENE)
    {
        if (!InitLogIn)
            return true;
    }
    else if (SceneFlag != CHARACTER_SCENE || !CanRenderCharacterScene())
    {
        return true;
    }
    const auto *definition = sessionKeeper_.WorldState().definition;
    if (!definition || !definition->presentation.persistentTerrain ||
        (SceneFlag == MAIN_SCENE && !ShouldRenderGameTerrain()) ||
        (SceneFlag == LOG_IN_SCENE && LegacyUiManager().m_CreditWin->IsShow()))
        return true;
    // Simulation has already prepared the current camera and grounding state.
    const float geometryHeight = g_fSpecialHeight;
    if (!PreparePersistentTerrainGeometry(generation, geometryHeight))
        return false;
    preparedPersistentTerrain_ = terrainGeometryCache_.Lease().vertices != nullptr;
    return true;
}

bool SessionRenderUnit::PreparePersistentTerrainGeometry(SessionGeneration generation,
                                                         float specialHeight) noexcept
{
    const auto *definition = sessionKeeper_.WorldState().definition;
    if (!definition || !definition->presentation.persistentTerrain)
        return false;
    const int world = definition->BehaviorMap();
    const auto &policy = definition->presentation;
    if (!terrainGeometryCache_.PrepareOnOwner(
            applicationKeeper_, sessionKeeper_.Id(), generation, nextGeometryRevision_, world,
            specialHeight, BackTerrainHeight, TerrainMappingLayer1, TerrainMappingLayer2,
            TerrainMappingAlpha, BackTerrainLight, TerrainWall, policy.pkField,
            policy.doppelGanger2, policy.doppelGanger3, Bitmaps, TerrainGrassTexture,
            sessionKeeper_.TerrainStorage().contentRevision))
        return false;
    return PrepareTerrainLightSnapshot();
}

bool SessionRenderUnit::DrawPersistentTerrainGeometry(const LogicalGeometryAssetLease &geometry,
                                                      const TerrainGeometryDraw &draw)
{
    if (!PrepareTerrainGeometryMaterial(draw))
        return true;
    const auto &policy = TheMapProcess().TerrainPolicy();
    RenderTapeTerrainConstants terrain;
    terrain.uvScale = draw.uvScale;
    terrain.specialHeight = g_fSpecialHeight;
    terrain.flags = draw.pass == TerrainGeometryMaterialPass::Alpha
                        ? RenderTapeTerrainAlpha
                    : draw.pass == TerrainGeometryMaterialPass::OceanBlend
                        ? RenderTapeTerrainOceanBlend
                        : 0;
    if (draw.water)
    {
        terrain.flags |= RenderTapeTerrainWater;
        terrain.waterMove = policy.reverseWater && draw.texture == 5 ? -WaterMove : WaterMove;
        terrain.waterWindScale = 0.002F;
        terrain.windSpeed = EnableEvent == 0
                                ? static_cast<int>(WorldTime) % (360000 * 2) * 0.002F
                                : static_cast<int>(WorldTime) % 36000 * 0.01F;
        terrain.windScale = policy.grassWindScale;
        terrain.windFrequency = policy.grassWindFrequency;
    }
    if (!LegacyRender().DrawTerrainInstances(geometry, draw.instanceOffset, draw.instanceCount,
                                             terrain))
    {
        recordingSucceeded_ = false;
        return false;
    }
    return true;
}

void SessionRenderUnit::RenderPersistentTerrainBlock(int mapX, int mapY,
                                                     std::uint16_t visibleTiles)
{
    constexpr std::uint16_t allTiles = 0xFFFFU;
    const bool allTilesVisible = visibleTiles == allTiles;
    const LogicalGeometryAssetLease &geometry = terrainGeometryCache_.Lease();
    const TerrainGeometryBlock &block = terrainGeometryCache_.Block(mapX, mapY);
    const TerrainGeometryDraw *lastDraw = nullptr;
    if (allTilesVisible)
    {
        for (const TerrainGeometryDraw &draw : block.draws)
        {
            if (!DrawPersistentTerrainGeometry(geometry, draw))
                return;
            lastDraw = &draw;
        }
    }
    else
    {
        for (int y = 0; y < TerrainBlockSize; ++y)
            for (int x = 0; x < TerrainBlockSize; ++x)
            {
                const int tileOffset = y * 4 + x;
                if ((visibleTiles & (1U << tileOffset)) == 0)
                    continue;
                const TerrainGeometryTile &tile = terrainGeometryCache_.Tile(mapX + x, mapY + y);
                for (std::size_t i = 0; i < tile.drawCount; ++i)
                {
                    const TerrainGeometryDraw &draw = tile.draws[i];
                    if (draw.pass == TerrainGeometryMaterialPass::AfterAlphaTest ||
                        draw.pass == TerrainGeometryMaterialPass::AfterAlphaBlend)
                        continue;
                    if (!DrawPersistentTerrainGeometry(geometry, draw))
                        return;
                    lastDraw = &draw;
                }
            }
    }
    if (lastDraw != nullptr)
        RestoreTerrainVertexAttributes(geometry, *lastDraw, false);
}

void SessionRenderUnit::RestoreTerrainVertexAttributes(const LogicalGeometryAssetLease &geometry,
                                                       const TerrainGeometryDraw &draw, bool grass)
{
    const RenderTapeTerrainInstance &instance =
        (*geometry.terrainInstances)[draw.finalInstance];
    const std::uint32_t x = instance.tileIndex & TERRAIN_SIZE_MASK;
    const std::uint32_t y = instance.tileIndex / TERRAIN_SIZE;
    const std::uint32_t lightIndex = TERRAIN_INDEX(x, y + 1);
    const float *light = geometry.terrainLight->values.data() + lightIndex * 3;
    if (grass)
    {
        LegacyRender().TexCoord2(instance.grassU, 1.0F);
        LegacyRender().Color4(light[0], light[1], light[2], 1.0F);
        return;
    }
    float u = static_cast<float>(x) * draw.uvScale[0];
    float v = static_cast<float>(y + 1) * draw.uvScale[1];
    if (draw.water)
    {
        const auto &policy = TheMapProcess().TerrainPolicy();
        u += policy.reverseWater && draw.texture == 5 ? -WaterMove : WaterMove;
        v += TerrainGrassWind[instance.tileIndex] * 0.002F;
    }
    LegacyRender().TexCoord2(u, v);
    if (draw.pass == TerrainGeometryMaterialPass::OceanBlend)
    {
        const float alpha = (*geometry.terrainCells)[lightIndex].alpha;
        LegacyRender().Color4(alpha, alpha, alpha, 1.0F);
    }
    else
    {
        const float alpha = draw.pass == TerrainGeometryMaterialPass::Alpha
                                ? (*geometry.terrainCells)[lightIndex].alpha
                                : 1.0F;
        LegacyRender().Color4(light[0], light[1], light[2], alpha);
    }
}

bool SessionRenderUnit::CollectVisibleTerrainBlocks() noexcept
{
    std::vector<VisibleTerrainBlock> &visible =
        sessionKeeper_.TerrainStorage().VisibleTerrainBlocks;
    visible.clear();
    try
    {
        if (FrustrumBoundMaxX >= FrustrumBoundMinX && FrustrumBoundMaxY >= FrustrumBoundMinY)
        {
            const std::size_t columns =
                static_cast<std::size_t>((FrustrumBoundMaxX - FrustrumBoundMinX) / 4 + 1);
            const std::size_t rows =
                static_cast<std::size_t>((FrustrumBoundMaxY - FrustrumBoundMinY) / 4 + 1);
            visible.reserve(columns * rows);
        }
        for (int y = FrustrumBoundMinY; y <= FrustrumBoundMaxY; y += 4)
        {
            for (int x = FrustrumBoundMinX; x <= FrustrumBoundMaxX; x += 4)
            {
                if (!TestFrustrum2D(static_cast<float>(x) + 2.0F, static_cast<float>(y) + 2.0F,
                                    g_fFrustumRange))
                {
                    continue;
                }
                const bool allTilesVisible = TestFrustrum2D(static_cast<float>(x) + 0.5F,
                                                            static_cast<float>(y) + 0.5F, 0.0F) &&
                                             TestFrustrum2D(static_cast<float>(x) + 3.5F,
                                                            static_cast<float>(y) + 0.5F, 0.0F) &&
                                             TestFrustrum2D(static_cast<float>(x) + 3.5F,
                                                            static_cast<float>(y) + 3.5F, 0.0F) &&
                                             TestFrustrum2D(static_cast<float>(x) + 0.5F,
                                                            static_cast<float>(y) + 3.5F, 0.0F);
                std::uint16_t visibleTiles = 0xFFFFU;
                if (!allTilesVisible)
                {
                    visibleTiles = 0;
                    for (int tileY = 0; tileY < 4; ++tileY)
                    {
                        for (int tileX = 0; tileX < 4; ++tileX)
                        {
                            if (TestFrustrum2D(static_cast<float>(x + tileX) + 0.5F,
                                               static_cast<float>(y + tileY) + 0.5F, 0.0F))
                            {
                                visibleTiles |=
                                    static_cast<std::uint16_t>(1U << (tileY * 4 + tileX));
                            }
                        }
                    }
                }
                visible.push_back({x, y, visibleTiles});
            }
        }
        return true;
    }
    catch (...)
    {
        visible.clear();
        return false;
    }
}

void SessionRenderUnit::RenderPersistentTerrainFrustrum()
{
    for (const VisibleTerrainBlock &block : sessionKeeper_.TerrainStorage().VisibleTerrainBlocks)
    {
        RenderPersistentTerrainBlock(block.mapX, block.mapY, block.visibleTiles);
    }
}

void SessionRenderUnit::RenderPersistentTerrainAfterBlock(int mapX, int mapY,
                                                          std::uint16_t visibleTiles)
{
    constexpr std::uint16_t allTiles = 0xFFFFU;
    const LogicalGeometryAssetLease &geometry = terrainGeometryCache_.Lease();
    const TerrainGeometryBlock &block = terrainGeometryCache_.Block(mapX, mapY);
    const TerrainGeometryDraw *lastDraw = nullptr;
    if (visibleTiles == allTiles)
    {
        for (const TerrainGeometryDraw &draw : block.afterDraws)
        {
            if (!DrawPersistentTerrainGeometry(geometry, draw))
                return;
            lastDraw = &draw;
        }
    }
    else
    {
        for (int y = 0; y < TerrainBlockSize; ++y)
            for (int x = 0; x < TerrainBlockSize; ++x)
            {
                const int tileOffset = y * TerrainBlockSize + x;
                if ((visibleTiles & (1U << tileOffset)) == 0)
                    continue;
                const TerrainGeometryTile &tile = terrainGeometryCache_.Tile(mapX + x, mapY + y);
                for (std::size_t i = 0; i < tile.drawCount; ++i)
                {
                    const TerrainGeometryDraw &draw = tile.draws[i];
                    if (draw.pass != TerrainGeometryMaterialPass::AfterAlphaTest &&
                        draw.pass != TerrainGeometryMaterialPass::AfterAlphaBlend)
                        continue;
                    if (!DrawPersistentTerrainGeometry(geometry, draw))
                        return;
                    lastDraw = &draw;
                }
            }
    }
    if (lastDraw != nullptr)
        RestoreTerrainVertexAttributes(geometry, *lastDraw, false);
}

bool SessionRenderUnit::DrawPersistentGrassGeometry(const LogicalGeometryAssetLease &geometry,
                                                    const TerrainGeometryDraw &draw,
                                                    const RenderTapeGrassConstants &grass,
                                                    bool alphaBlend, int &activeTexture,
                                                    std::uint64_t &activeRunId)
{
    if (alphaBlend)
    {
        EnableAlphaBlend();
    }
    if (draw.texture != activeTexture || activeRunId == 0)
    {
        BindTexture(draw.texture);
        activeRunId = LegacyRender().BeginGrassGeometryRun(grass);
        activeTexture = activeRunId != 0 ? draw.texture : -1;
    }
    if (!LegacyRender().DrawGrassGeometry(geometry, draw.instanceOffset, draw.instanceCount,
                                          activeRunId))
    {
        recordingSucceeded_ = false;
        activeTexture = -1;
        activeRunId = 0;
    }
    if (alphaBlend)
    {
        DisableAlphaBlend();
    }
    return recordingSucceeded_;
}

void SessionRenderUnit::RenderPersistentGrassBlock(int mapX, int mapY, std::uint16_t visibleTiles,
                                                   const RenderTapeGrassConstants &grass,
                                                   int &activeTexture, std::uint64_t &activeRunId)
{
    constexpr std::uint16_t allTiles = 0xFFFFU;
    const bool allTilesVisible = visibleTiles == allTiles;
    const auto &policy = TheMapProcess().TerrainPolicy();
    const TerrainGeometryBlock &block = terrainGeometryCache_.Block(mapX, mapY);
    if (mapX < 0 || mapY < 0 || mapX >= TERRAIN_SIZE || mapY >= TERRAIN_SIZE)
        return;

    const LogicalGeometryAssetLease &geometry = terrainGeometryCache_.Lease();
    const bool alphaBlend = policy.magma;
    const TerrainGeometryDraw *finalDraw = nullptr;
    if (allTilesVisible)
    {
        for (const TerrainGeometryDraw &draw : block.grassDraws)
        {
            if (!DrawPersistentGrassGeometry(geometry, draw, grass, alphaBlend, activeTexture,
                                             activeRunId))
            {
                return;
            }
        }
        if (block.finalGrassDraw.instanceCount != 0)
        {
            finalDraw = &block.finalGrassDraw;
        }
    }
    else
    {
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                const int tileX = mapX + x;
                const int tileY = mapY + y;
                const int tileOffset = y * 4 + x;
                if ((visibleTiles & (1U << tileOffset)) == 0)
                {
                    continue;
                }
                const TerrainGeometryTile &tile = terrainGeometryCache_.Tile(tileX, tileY);
                if (!tile.hasGrass)
                {
                    continue;
                }
                if (!DrawPersistentGrassGeometry(geometry, tile.grassDraw, grass, alphaBlend,
                                                 activeTexture, activeRunId))
                {
                    return;
                }
                finalDraw = &tile.grassDraw;
            }
        }
    }
    if (finalDraw != nullptr)
    {
        BindTexture(finalDraw->texture);
        RestoreTerrainVertexAttributes(geometry, *finalDraw, true);
    }
}

void SessionRenderUnit::RenderPersistentGrassFrustrum()
{
    RenderTapeGrassConstants grass;
    const auto &policy = TheMapProcess().TerrainPolicy();
    if (policy.secondaryGrassWind)
    {
        grass.windSpeed = static_cast<int>(WorldTime) % 36000 * 0.008F;
        grass.windScale = 15.f;
    }
    else
    {
        grass.windSpeed = EnableEvent == 0 ? static_cast<int>(WorldTime) % (360000 * 2) * 0.002F
                                           : static_cast<int>(WorldTime) % 36000 * 0.01F;
        grass.windScale = policy.grassWindScale;
    }
    grass.windFrequency = policy.grassWindFrequency;
    grass.specialHeight = g_fSpecialHeight;
    int activeTexture = -1;
    std::uint64_t activeRunId = 0;
    for (const VisibleTerrainBlock &block : sessionKeeper_.TerrainStorage().VisibleTerrainBlocks)
    {
        RenderPersistentGrassBlock(block.mapX, block.mapY, block.visibleTiles, grass, activeTexture,
                                   activeRunId);
    }
}

void SessionRenderUnit::RenderTerrainFrustrum(bool EditFlag)
{
    int xi;
    int yi = FrustrumBoundMinY;
    float xf;
    auto yf = (float)yi;

    for (; yi <= FrustrumBoundMaxY; yi += 4, yf += 4.f)
    {
        xi = FrustrumBoundMinX;
        xf = (float)xi;
        for (; xi <= FrustrumBoundMaxX; xi += 4, xf += 4.f)
        {
            if (TestFrustrum2D(xf + 2.f, yf + 2.f, g_fFrustumRange))
            {
                // Login scene: terrain distance is controlled by ViewFar (projection clipping)
                // No per-tile distance cap — effects (flames etc.) are tied to terrain blocks
                RenderTerrainBlock(xf, yf, xi, yi, EditFlag);
            }
        }
    }
}

void SessionRenderUnit::RenderTerrainBlock_After(float xf, float yf, int xi, int yi, bool EditFlag)
{
    int lodi = 1;
    auto lodf = (float)lodi;
    for (int i = 0; i < 4; i += lodi)
    {
        float temp = xf;
        for (int j = 0; j < 4; j += lodi)
        {
            if (TestFrustrum2D(xf + 0.5f, yf + 0.5f, 0.f))
            {
                RenderTerrainTile_After(xf, yf, xi + j, yi + i, lodf, lodi, EditFlag);
            }
            xf += lodf;
        }
        xf = temp;
        yf += lodf;
    }
}

void SessionRenderUnit::RenderTerrainFrustrum_After(bool EditFlag)
{
    int xi, yi;
    float xf, yf;
    yi = FrustrumBoundMinY;
    yf = (float)yi;
    for (; yi <= FrustrumBoundMaxY; yi += 4, yf += 4.f)
    {
        xi = FrustrumBoundMinX;
        xf = (float)xi;
        for (; xi <= FrustrumBoundMaxX; xi += 4, xf += 4.f)
        {
            if (TestFrustrum2D(xf + 2.f, yf + 2.f, -80.f))
            {
                RenderTerrainBlock_After(xf, yf, xi, yi, EditFlag);
            }
        }
    }
}

extern void RenderCharactersClient();

void SessionRenderUnit::RenderTerrain(bool EditFlag)
{
    const auto &policy = sessionKeeper_.WorldState().definition->presentation;
    if (!EditFlag)
        UpdateTerrainWaterUv();

    if (!EditFlag)
    {
        DisableAlphaBlend();
    }

    TerrainFlag = TERRAIN_MAP_NORMAL;
    const bool persistentTerrain = !EditFlag && preparedPersistentTerrain_;
    if (persistentTerrain)
    {
        if (!CollectVisibleTerrainBlocks())
        {
            recordingSucceeded_ = false;
            return;
        }
        RenderPersistentTerrainFrustrum();
    }
    else
    {
        RenderTerrainFrustrum(EditFlag);
    }
    if (EditFlag && SelectFlag)
    {
        RenderTerrainTile(SelectXF, SelectYF, (int)SelectXF, (int)SelectYF, 1.f, 1, EditFlag);
    }
    if (!EditFlag)
    {
        EnableAlphaTest();
        if (TerrainGrassEnable && CurrentLayer == 0 && policy.grass)
        {
            TerrainFlag = TERRAIN_MAP_GRASS;
            if (persistentTerrain)
            {
                RenderPersistentGrassFrustrum();
            }
            else
            {
                RenderTerrainFrustrum(EditFlag);
            }
        }
        DisableDepthTest();
        EnableCullFace();
        RenderPointers();
        EnableDepthTest();
    }
}

void SessionRenderUnit::RenderTerrain_After(bool EditFlag)
{
    if (!TheMapProcess().TerrainPolicy().afterPass)
        return;

    TerrainFlag = TERRAIN_MAP_NORMAL;
    if (!EditFlag && preparedPersistentTerrain_)
    {
        for (const VisibleTerrainBlock &block : sessionKeeper_.TerrainStorage().VisibleTerrainBlocks)
            RenderPersistentTerrainAfterBlock(block.mapX, block.mapY, block.visibleTiles);
        return;
    }
    RenderTerrainFrustrum_After(EditFlag);
}

void SessionRenderUnit::RenderSun()
{
    EnableAlphaBlend();
    vec3_t Angle;
    float Matrix[3][4];
    Angle[0] = 0.f;
    Angle[1] = 0.f;
    Angle[2] = g_Camera.Angle[2];
    AngleIMatrix(Angle, Matrix);
    vec3_t p, Position;
    Vector(-900.f, g_Camera.ViewFar * 0.9f, 0.f, p);
    VectorRotate(p, Matrix, Position);
    VectorAdd(g_Camera.Position, Position, Sun.Position);
    Sun.Position[2] = 550.f;
    BeginSprite();
    //RenderSprite(&Sun);
    EndSprite();
    DisableAlphaBlend();
}

void SessionRenderUnit::RenderSky()
{
    vec3_t Angle;
    float Matrix[3][4];
    Angle[0] = 0.f;
    Angle[1] = 0.f;
    Angle[2] = g_Camera.Angle[2];
    AngleIMatrix(Angle, Matrix);
    float Aspect = 1.f;
    float Width = 1780.f * Aspect;

    BeginSprite();
    vec3_t p, Position;
    float Num = 20.f;
    vec3_t LightTable[21];

    for (int i = 0; i <= Num; i++)
    {
        Vector(((float)i - Num * 0.5f) * (Width / Num), g_Camera.ViewFar * 0.99f, 0.f, p);
        VectorRotate(p, Matrix, Position);
        VectorAdd(g_Camera.Position, Position, Position);
        RequestTerrainLight(Position[0], Position[1], LightTable[i]);
    }

    for (int i = 1; i <= (int)Num; i++)
    {
        if (LightTable[i][0] <= 0.f)
        {
            Vector(0.f, 0.f, 0.f, LightTable[i - 1]);
        }
    }

    for (int i = (int)Num - 1; i >= 0; i--)
    {
        if (LightTable[i][0] <= 0.f)
        {
            Vector(0.f, 0.f, 0.f, LightTable[i + 1]);
        }
    }
    for (float x = 0.f; x < Num; x += 1.f)
    {
        float UV[4][2];
        TEXCOORD(UV[0], (x) * (1.f / Num), 1.f);
        TEXCOORD(UV[1], (x + 1.f) * (1.f / Num), 1.f);
        TEXCOORD(UV[2], (x + 1.f) * (1.f / Num), 0.f);
        TEXCOORD(UV[3], (x) * (1.f / Num), 0.f);

        vec3_t Light[4];
        VectorCopy(LightTable[(int)x], Light[0]);
        VectorCopy(LightTable[(int)x + 1], Light[1]);
        //VectorCopy(LightTable[(int)x+1],Light[2]);
        //VectorCopy(LightTable[(int)x  ],Light[3]);
        Vector(1.f, 1.f, 1.f, Light[2]);
        Vector(1.f, 1.f, 1.f, Light[3]);

        Vector((x - Num * 0.5f + 0.5f) * (Width / Num), g_Camera.ViewFar * 0.9f, 0.f, p);
        VectorRotate(p, Matrix, Position);
        VectorAdd(g_Camera.Position, Position, Position);
        Position[2] = 400.f;
        // NOTE: BITMAP_SKY texture doesn't exist in this codebase - sky rendering disabled
        //RenderSpriteUV(BITMAP_SKY,Position,Width/Num,Height,UV,Light);
    }
    EndSprite();
}

//  GOBoid.cpp

void SessionRenderUnit::RenderBoids(bool bAfterCharacter)
{
    if (!g_pOption->GetRenderAllEffects())
    {
        return;
    }

    for (int i = 0; i < MAX_BOIDS; i++)
    {
        OBJECT *o = &Boids[i];
        if (o->m_bRenderAfterCharacter != bAfterCharacter)
            continue;
        if (MODEL_SPEARSKILL == o->Type)
            continue;
        const CharacterMotionDetail::BoidDrawingHeading drawingHeading(*o);
        if (o->Live)
        {
            o->Visible = TestFrustrum2D(o->Position[0] * 0.01f, o->Position[1] * 0.01f, -20.f);
            if (o->Visible)
            {
                if (MODEL_SPEARSKILL == o->Type)
                {
                    continue;
                }
                RenderObject(o, true);

                BMD *b = &Models[o->Type];
                vec3_t p, Position, Light;
                switch (o->Type)
                {
                case MODEL_DRAGON_:
                    if (o->SubType == 1)
                    {
                        float Bright = 1.0f;
                        RenderPartObjectBodyColor(&Models[o->Type], o, o->Type, o->Alpha,
                                                  RENDER_METAL | RENDER_BRIGHT, Bright);
                        RenderPartObjectBodyColor(&Models[o->Type], o, o->Type, o->Alpha,
                                                  RENDER_CHROME | RENDER_BRIGHT, Bright);
                    }
                    if (EnableEvent != 0)
                    {
                        Vector(0.f, -50.f, 0.f, p);
                        b->TransformPosition(BoneTransform[11], p, Position);
                        Vector(1.f, 0.f, 0.f, Light);
                        CreateSprite(BITMAP_LIGHTNING + 1, Position, 1.f, Light, o);
                    }
                    break;

                case MODEL_BUTTERFLY01: {
                    float Luminosity = (float)(WorldRandom() % 32 + 64) * 0.01f;

                    Vector(Luminosity * 0.2f, Luminosity * 0.4f, Luminosity * 0.4f, Light);
                    CreateSprite(BITMAP_LIGHT, o->Position, 1.f, Light, o);
                }
                break;

                case MODEL_CROW:
                    Vector(-5.f, 0.f, 0.f, p);
                    b->TransformPosition(BoneTransform[1], p, Position, true);

                    float Luminosity = (float)(WorldRandom() % 32 + 128) * 0.01f;
                    Vector(Luminosity * 1.f, Luminosity * 0.2f, 0.f, Light);
                    CreateSprite(BITMAP_LIGHT, Position, 0.1f, Light, o);

                    Vector(5.f, 0.f, 0.f, p);
                    b->TransformPosition(BoneTransform[1], p, Position, true);
                    CreateSprite(BITMAP_LIGHT, Position, 0.1f, Light, o);
                    break;
                }
                if (TheMapProcess().Presentation().ambientShadows)
                {
                    EnableAlphaTest();
                    if (o->Type == MODEL_EAGLE)
                    {
                        if (o->ShadowScale == 0)
                            glColor4f(0.f, 0.f, 0.f, 0.0f);
                        else
                            glColor4f(0.f, 0.f, 0.f, 1.0f);
                    }
                    else
                        glColor4f(0.f, 0.f, 0.f, 0.2f);

                    if (!TheMapProcess().Presentation().ambientTornadoShadow &&
                        o->Type == MODEL_MAP_TORNADO)
                        ;
                    else
                    {
                        VectorCopy(o->Position, Position);
                        Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]);
                        VectorCopy(Position, b->BodyOrigin);

                        b->RenderBodyShadow();
                    }
                }
            }
        }
    }
}

void SessionRenderUnit::RenderFishs()
{
    if (!g_pOption->GetRenderAllEffects())
    {
        return;
    }

    for (int i = 0; i < MAX_FISHS; i++)
    {
        OBJECT *o = &Fishs[i];
        const CharacterMotionDetail::BoidDrawingHeading drawingHeading(*o);
        if (o->Live)
        {
            o->Visible = TestFrustrum2D(o->Position[0] * 0.01f, o->Position[1] * 0.01f, -20.f);
            if (o->Visible && o->Type != -1)
            {
                RenderObject(o);

                if (o->Type == MODEL_FISH01 + 7 || o->Type == MODEL_FISH01 + 8)
                {
                }
                else
                {
                    if (TheMapProcess().Presentation().ambientShadows)
                    {
                        EnableAlphaTest();
                        glColor4f(0.f, 0.f, 0.f, 0.2f);
                        BMD *b = &Models[o->Type];
                        vec3_t Position;
                        VectorCopy(o->Position, Position);
                        Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]);
                        VectorCopy(Position, b->BodyOrigin);
                        b->RenderBodyShadow();
                    }
                }
            }
        }
    }
}
