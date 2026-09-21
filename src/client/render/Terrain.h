#pragma once
#include "data/WorldData.h"
#include "domain/WorldSimulation.h"
#include "session/SessionRuntime.h"
#include "support/CoreMath.h"
#include <array>
#include <bitset>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#define WATER_TERRAIN_SIZE 256
#define MAX_WATER_SIZE 4
#define MAX_WATER_GRID 42
#define VIEW_WATER_GRID 100
#define WAVE_SCALE 50.f

//  CSWaterTerrain.h

class CMapManager;
class SessionKeeper;
class SessionRandom;

class CSWaterTerrain : private SessionLegacyCalls
{
  private:
    friend class GameSessionTestPeer;
    SessionRandom &Random;
    using WaveHeightGrid = std::array<int, WATER_TERRAIN_SIZE * WATER_TERRAIN_SIZE>;

    int m_iMapIndex;
    std::array<WaveHeightGrid, 4> m_iWaveHeight{};
    int m_iWaterPage;

    vec3_t m_vLightVector;
    vec3_t m_Vertices[MAX_WATER_GRID * MAX_WATER_GRID];
    vec3_t m_Normals[MAX_WATER_GRID * MAX_WATER_GRID];
    int m_iTriangleList[MAX_WATER_GRID * MAX_WATER_GRID * 6];
    int m_iTriangleListNum;

    double m_lastAutoWaveTime;
    double simulationTimeMilliseconds_ = 0.0;
    double rippleRemainder_ = 0.0;
    // Remaining reference frames and authored x/y/radii/height; capacity survives updates.
    std::vector<std::pair<float, std::array<int, 5>>> pendingWaves_;
    CMapManager &gMapManager;
    const float &FPS_ANIMATION_FACTOR;

    void calcBaseWave(void);
    void calcWave(void);
    void AdvanceRipples(double frames);

    void CreateTerrain(int x, int y);
    void RenderWaterBitmapTile(float xf, float yf, float lodf, int lodi, vec3_t c[4],
                               bool LightEnable, float Alpha, float Height = 0.f);

    void SpawnAmbientWave(double currentTimeMs);

  public:
    CSWaterTerrain(int map, SessionKeeper &sessionKeeper);
    ~CSWaterTerrain(void) {};

    void Init(void);
    void Update(void);
    void Render(void);

    void addSineWave(int x, int y, int radiusX, int radiusY, int height);
    void QueueWave(int x, int y, int radiusX, int radiusY, int height, float remainingFrames);
    float GetWaterTerrain(float xf, float yf);
    void RenderWaterAlphaBitmap(int Texture, float xf, float yf, float SizeX, float SizeY,
                                const vec3_t Light, float Rotation, float Alpha, float Height);
};

// Immutable while a recorded frame holds it; storage is reused after retirement.
struct TerrainLightSnapshot final
{
    std::uint64_t revision = 0;
    std::vector<float> values;
};

struct VisibleTerrainBlock final
{
    int mapX = 0;
    int mapY = 0;
    std::uint16_t visibleTiles = 0;
};

struct SessionTerrainStorage final
{
    int TerrainFlag = 0;
    bool ActiveTerrain = false;
    bool mainCameraLocked = false;
    bool TerrainGrassEnable = true;
    bool DetailLowEnable = false;
    vec3_t TerrainNormal[TERRAIN_SIZE * TERRAIN_SIZE]{};
    vec3_t PrimaryTerrainLight[TERRAIN_SIZE * TERRAIN_SIZE]{};
    bool TerrainDynamicLightBlocks[64 * 64]{};
    bool dynamicLightActive = false;
    bool dynamicLightDirty = false;
    std::uint64_t lightRevision = 0;
    std::shared_ptr<const TerrainLightSnapshot> lightSnapshot;
    std::vector<std::shared_ptr<TerrainLightSnapshot>> lightSnapshots;
    vec3_t BackTerrainLight[TERRAIN_SIZE * TERRAIN_SIZE]{};
    vec3_t TerrainLight[TERRAIN_SIZE * TERRAIN_SIZE]{};
    float BackTerrainHeight[TERRAIN_SIZE * TERRAIN_SIZE]{};
    inline static const TerrainMappingData EmptyMapping{};
    std::shared_ptr<const WorldTerrainAsset> BaseTerrain;
    std::shared_ptr<const TerrainMappingData> SharedMapping;
    std::unique_ptr<TerrainMappingData> EditedMapping;
    const unsigned char *TerrainMappingLayer1 = EmptyMapping.layer1.data();
    const unsigned char *TerrainMappingLayer2 = EmptyMapping.layer2.data();
    const float *TerrainMappingAlpha = EmptyMapping.alpha.data();
    std::bitset<256> admittedTiles;
    const TerrainMappingData &EffectiveMapping() const noexcept
    {
        return EditedMapping ? *EditedMapping : (SharedMapping ? *SharedMapping : EmptyMapping);
    }
    std::uint64_t contentRevision = 0;
    std::bitset<64 * 64> revisedBlocks;

    void BindMapping(std::shared_ptr<const TerrainMappingData> mapping) noexcept
    {
        EditedMapping.reset();
        SharedMapping = std::move(mapping);
        PointMapping(SharedMapping ? *SharedMapping : EmptyMapping);
    }
    void BindBase(std::shared_ptr<const WorldTerrainAsset> asset) noexcept
    {
        BaseTerrain = std::move(asset);
        if (!EditedMapping)
            BindMapping(
                std::shared_ptr<const TerrainMappingData>(BaseTerrain, &BaseTerrain->surface));
    }
    TerrainMappingData &MutableMapping()
    {
        if (!EditedMapping)
        {
            EditedMapping =
                std::make_unique<TerrainMappingData>(SharedMapping ? *SharedMapping : EmptyMapping);
            PointMapping(*EditedMapping);
        }
        return *EditedMapping;
    }
    void PointMapping(const TerrainMappingData &mapping) noexcept
    {
        TerrainMappingLayer1 = mapping.layer1.data();
        TerrainMappingLayer2 = mapping.layer2.data();
        TerrainMappingAlpha = mapping.alpha.data();
    }
    float TerrainGrassTexture[TERRAIN_SIZE]{};
    float TerrainGrassWind[TERRAIN_SIZE * TERRAIN_SIZE]{};
    float g_fTerrainGrassWind1[TERRAIN_SIZE * TERRAIN_SIZE]{};
    WORD TerrainWall[TERRAIN_SIZE * TERRAIN_SIZE]{};
    float WaterMove = 0.f;
    int CurrentLayer = 0;
    float g_fSpecialHeight = 1200.f;
    float g_fFrustumRange = -40.f;
    unsigned char BMPHeader[1080]{};

    int TerrainIndex1 = 0;
    int TerrainIndex2 = 0;
    int TerrainIndex3 = 0;
    int TerrainIndex4 = 0;
    int Index0 = 0;
    int Index1 = 0;
    int Index2 = 0;
    int Index3 = 0;
    int Index01 = 0;
    int Index12 = 0;
    int Index23 = 0;
    int Index30 = 0;
    int Index02 = 0;
    vec3_t TerrainVertex[4]{};
    vec3_t TerrainVertex01{};
    vec3_t TerrainVertex12{};
    vec3_t TerrainVertex23{};
    vec3_t TerrainVertex30{};
    vec3_t TerrainVertex02{};
    float TerrainTextureCoord[4][2]{};
    float TerrainTextureCoord01[2]{};
    float TerrainTextureCoord12[2]{};
    float TerrainTextureCoord23[2]{};
    float TerrainTextureCoord30[2]{};
    float TerrainTextureCoord02[2]{};
    float TerrainMappingAlpha01 = 0.f;
    float TerrainMappingAlpha12 = 0.f;
    float TerrainMappingAlpha23 = 0.f;
    float TerrainMappingAlpha30 = 0.f;
    float TerrainMappingAlpha02 = 0.f;
    int WaterTextureNumber = 0;
    double lastWaterChange = 0.0;

    int FrustrumBoundMinX = 0;
    int FrustrumBoundMinY = 0;
    int FrustrumBoundMaxX = TERRAIN_SIZE_MASK;
    int FrustrumBoundMaxY = TERRAIN_SIZE_MASK;
    float FrustrumX[16]{};
    float FrustrumY[16]{};
    int FrustrumCount = 4;
    vec3_t FrustrumVertex[5]{};
    vec3_t FrustrumFaceNormal[5]{};
    float FrustrumFaceD[5]{};
    std::vector<VisibleTerrainBlock> VisibleTerrainBlocks;
    OBJECT Sun;
};

//  속성 변경.
//Vector RequestTerrainNormal(float xf,float yf);

void SetTerrainLight(float xf, float yf, vec3_t Light, int Range, vec3_t *Buffer);
void AddTerrainLight(float xf, float yf, vec3_t Light, int Range, vec3_t *Buffer);
void AddTerrainLightClip(float xf, float yf, vec3_t Light, int Range, vec3_t *Buffer);
// Frustum creation and testing (restored original implementations)
void UpdateFrustrumBounds();

// Debug visualization

extern const float g_fMinHeight;
extern const float g_fMaxHeight;
