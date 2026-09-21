//  CSWaterTerrain.cpp

#include "render/Terrain.h"
#include "I18N/All.h"
#include "app/ApplicationDiagnostics.h"
#include "app/ApplicationLoopFrame.h"
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
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Shop.h"
#include "domain/WorldSimulation.h"
#include "render/ModelResources.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionNetwork.h"
#include "session/SessionPresentation.h"
#include "session/SessionRender.h"
#include "session/SessionUi.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/World/WorldLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"

namespace
{
constexpr double kAutoWaveIntervalMs = (40.0 / 25.0) * 1000.0; // 1600 ms
constexpr float kDefaultWaterHeight = 350.0f;
constexpr float kTileHeightOffset = 400.0f;
} // namespace

CSWaterTerrain::CSWaterTerrain(int map, SessionKeeper &sessionKeeper)
    : SessionLegacyCalls(sessionKeeper), Random(sessionKeeper.RandomForConstruction()),
      m_iMapIndex(map), m_iWaterPage(0), m_iTriangleListNum(0), m_lastAutoWaveTime(0.0),
      gMapManager(sessionKeeper.MapManagerObject()),
      FPS_ANIMATION_FACTOR(sessionKeeper.FrameAnimationFactor())
{
    Init();
}

void CSWaterTerrain::Init(void)
{
    Vector(1.f, -1.f, 1.f, m_vLightVector);
}

void CSWaterTerrain::Update(void)
{
    if (!gMapManager.InHellas(m_iMapIndex) || FPS_ANIMATION_FACTOR <= 0.f)
        return;

    const double startTime = simulationTimeMilliseconds_;
    simulationTimeMilliseconds_ +=
        FPS_ANIMATION_FACTOR * (1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps);
    SpawnAmbientWave(startTime);
    std::sort(pendingWaves_.begin(), pendingWaves_.end(),
              [](const auto &first, const auto &second) {
                  return first.first != second.first ? first.first > second.first
                                                     : first.second < second.second;
              });
    double remaining = FPS_ANIMATION_FACTOR;
    for (const auto &[birthRemaining, wave] : pendingWaves_)
    {
        const double next = std::clamp<double>(birthRemaining, 0.0, remaining);
        AdvanceRipples(remaining - next);
        addSineWave(wave[0], wave[1], wave[2], wave[3], wave[4]);
        remaining = next;
    }
    pendingWaves_.clear();
    AdvanceRipples(remaining);
    calcBaseWave();
}

void CSWaterTerrain::QueueWave(int x, int y, int radiusX, int radiusY, int height,
                               float remainingFrames)
{
    pendingWaves_.push_back({remainingFrames, {x, y, radiusX, radiusY, height}});
}

void CSWaterTerrain::AdvanceRipples(double frames)
{
    // The integer recurrence is authored at one reference frame. Retain the
    // fractional phase; changing callback frequency must not change its equation.
    constexpr double boundaryTolerance = 0.00001;
    rippleRemainder_ += frames;
    while (rippleRemainder_ + boundaryTolerance >= 1.0)
    {
        rippleRemainder_ -= 1.0;
        m_iWaterPage ^= 1;
        calcWave();
    }
}

void CSWaterTerrain::Render(void)
{
    if (!gMapManager.InHellas(m_iMapIndex))
        return;

    CreateTerrain((Hero->PositionX) * 2, (Hero->PositionY) * 2);

    float alpha;
    int offset;
    int i, j;
    for (i = 0; i < MAX_WATER_GRID * MAX_WATER_GRID; i++)
    {
        float *Normal = m_Normals[i];
        g_chrome[i][0] = Normal[2] * 0.5f + 0.1f;
        g_chrome[i][1] = Normal[1] * 0.5f + 0.5f;
    }

    EnableAlphaTest();
    BindTexture(BITMAP_MAPTILE);
    glBegin(GL_TRIANGLES);
    glColor3f(0.2f, 0.5f, 0.65f);
    for (j = 0; j < m_iTriangleListNum; j++)
    {
        offset = m_iTriangleList[j];
        glTexCoord2f(g_chrome[offset][1], g_chrome[offset][0]);
        glVertex3fv(m_Vertices[offset]);
    }
    glEnd();
    EnableAlphaBlend();
    BindTexture(BITMAP_MAPTILE + 1);
    glBegin(GL_TRIANGLES);
    for (j = 0; j < m_iTriangleListNum; j++)
    {
        offset = m_iTriangleList[j];
        alpha = 1.f - DotProduct(m_Normals[offset], m_vLightVector);
        glColor3f(alpha, alpha * 2.5f, alpha * 3.f); //, alpha );
        glTexCoord2f(g_chrome[offset][1], g_chrome[offset][0]);
        glVertex3fv(m_Vertices[offset]);
    }
    glEnd();
}

void CSWaterTerrain::SpawnAmbientWave(double startTimeMs)
{
    const double millisecondsPerFrame =
        1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    // Match the ripple boundary tolerance when converting the float reference factor.
    const double boundaryToleranceMs = millisecondsPerFrame * 0.00001;
    while (simulationTimeMilliseconds_ + boundaryToleranceMs >=
           m_lastAutoWaveTime + kAutoWaveIntervalMs)
    {
        m_lastAutoWaveTime += kAutoWaveIntervalMs;
        const float fraction = static_cast<float>(std::clamp(
            (m_lastAutoWaveTime - startTimeMs) / (simulationTimeMilliseconds_ - startTimeMs), 0.0,
            1.0));
        vec3_t heroPosition;
        Hero->Object.MotionTrace.Sample(sessionKeeper_.FrameWorldTime(), fraction,
                                        Hero->Object.Position, heroPosition);
        const int waveX = static_cast<int>(heroPosition[0] / WAVE_SCALE) + Random.RangeInt(-15, 14);
        const int waveY = static_cast<int>(heroPosition[1] / WAVE_SCALE) + 25;
        const float remaining = static_cast<float>(std::max(
            0.0, (simulationTimeMilliseconds_ - m_lastAutoWaveTime) / millisecondsPerFrame));
        QueueWave(waveX, waveY, 20, 2, 2000, remaining);
    }
}

void CSWaterTerrain::CreateTerrain(int x, int y)
{
    float fHeight, fHeight1;
    int offset;
    constexpr int grid = MAX_WATER_GRID / 2;

    m_iTriangleListNum = 0;
    for (int offY = 0, i = y - grid + 6; offY < MAX_WATER_GRID; ++i, offY++)
    {
        for (int offX = 0, j = x - grid - 4; offX < MAX_WATER_GRID; ++j, offX++)
        {
            if (i < 0 || j < 0)
                fHeight = kDefaultWaterHeight;
            else if (i >= WATER_TERRAIN_SIZE || j >= WATER_TERRAIN_SIZE)
                fHeight = kDefaultWaterHeight;
            else
            {
                offset = j + (i * WATER_TERRAIN_SIZE);

                fHeight = m_iWaveHeight[m_iWaterPage][offset] + kDefaultWaterHeight;

                fHeight1 = m_iWaveHeight[2][offset] + kDefaultWaterHeight;
                fHeight1 += m_iWaveHeight[3][offset] + kDefaultWaterHeight;

                fHeight = (fHeight + fHeight1 / 2.f) * 0.5f;
            }

            offset = offX + (offY * MAX_WATER_GRID);
            Vector(static_cast<float>(j) * WAVE_SCALE, static_cast<float>(i) * WAVE_SCALE, fHeight,
                   m_Vertices[offset]);
            Vector(0.f, 0.f, 0.f, m_Normals[offset]);
            if (offX >= MAX_WATER_GRID - 1 || offY >= MAX_WATER_GRID - 1)
            {
                VectorCopy(m_Normals[offset - 1], m_Normals[offset]);
                continue;
            }

            if (((offX % 2) == 0 && (offY % 2) == 0) || ((offX % 2) == 1 && (offY % 2) == 1))
            {
                m_iTriangleList[m_iTriangleListNum + 0] = offset;
                m_iTriangleList[m_iTriangleListNum + 1] = offset + 1;
                m_iTriangleList[m_iTriangleListNum + 2] = offset + 1 + MAX_WATER_GRID;

                m_iTriangleList[m_iTriangleListNum + 3] = offset;
                m_iTriangleList[m_iTriangleListNum + 4] = offset + 1 + MAX_WATER_GRID;
                m_iTriangleList[m_iTriangleListNum + 5] = offset + MAX_WATER_GRID;
            }
            else
            {
                m_iTriangleList[m_iTriangleListNum + 0] = offset;
                m_iTriangleList[m_iTriangleListNum + 1] = offset + 1;
                m_iTriangleList[m_iTriangleListNum + 2] = offset + MAX_WATER_GRID;

                m_iTriangleList[m_iTriangleListNum + 3] = offset + 1;
                m_iTriangleList[m_iTriangleListNum + 4] = offset + 1 + MAX_WATER_GRID;
                m_iTriangleList[m_iTriangleListNum + 5] = offset + MAX_WATER_GRID;
            }

            m_iTriangleListNum += 6;
        }
    }

    std::array<int, MAX_WATER_GRID * MAX_WATER_GRID> normalUseCounts{};
    vec3_t normalV;
    for (int j = 0; j < m_iTriangleListNum; j += 3)
    {
        const int v1 = m_iTriangleList[j + 0];
        const int v2 = m_iTriangleList[j + 1];
        const int v3 = m_iTriangleList[j + 2];

        FaceNormalize(m_Vertices[v1], m_Vertices[v2], m_Vertices[v3], normalV);

        VectorAdd(normalV, m_Normals[v1], m_Normals[v1]);
        VectorAdd(normalV, m_Normals[v2], m_Normals[v2]);
        VectorAdd(normalV, m_Normals[v3], m_Normals[v3]);

        normalUseCounts[v1]++;
        normalUseCounts[v2]++;
        normalUseCounts[v3]++;
    }

    for (int i = 0; i < MAX_WATER_GRID * MAX_WATER_GRID; ++i)
    {
        if (normalUseCounts[i] > 0)
        {
            const float invCount = 1.0f / static_cast<float>(normalUseCounts[i]);
            m_Normals[i][0] *= invCount;
            m_Normals[i][1] *= invCount;
            m_Normals[i][2] *= invCount;
            VectorNormalize(m_Normals[i]);
        }
    }
}

void CSWaterTerrain::addSineWave(int x, int y, int radiusX, int radiusY, int height)
{
    if (radiusX <= 0 || radiusY <= 0 || height == 0)
    {
        return;
    }

    int *waveBuffer = m_iWaveHeight[m_iWaterPage].data();

    const float length =
        (1024.f / static_cast<float>(radiusX)) * (1024.f / static_cast<float>(radiusX));

    if (x < 0)
    {
        const int minX = 1 + radiusX;
        const int maxX = WATER_TERRAIN_SIZE - radiusX - 1;
        x = Random.RangeInt(minX, std::max<int>(minX, maxX));
    }
    if (y < 0)
    {
        const int minY = 1 + radiusY;
        const int maxY = WATER_TERRAIN_SIZE - radiusY - 1;
        y = Random.RangeInt(minY, std::max<int>(minY, maxY));
    }

    const int radsquare = (radiusX * radiusY);

    int left = -radiusX;
    int right = radiusX;
    int top = -radiusY;
    int bottom = radiusY;

    // Perform edge clipping...
    if ((x - radiusX) < 1)
        left -= (x - radiusX - 1);
    if ((y - radiusY) < 1)
        top -= (y - radiusY - 1);
    if ((x + radiusX) > WATER_TERRAIN_SIZE - 1)
        right -= (x + radiusX - WATER_TERRAIN_SIZE + 1);
    if ((y + radiusY) > WATER_TERRAIN_SIZE - 1)
        bottom -= (y + radiusY - WATER_TERRAIN_SIZE + 1);

    for (int cy = top; cy < bottom; ++cy)
    {
        for (int cx = left; cx < right; ++cx)
        {
            const int square = cy * cy + cx * cx;
            if (square < radsquare)
            {
                const float dist = std::sqrt(static_cast<float>(square) * length);
                int sine = static_cast<int>((std::cos(dist) + 0xffff) * height) >> 19;
                waveBuffer[WATER_TERRAIN_SIZE * (cy + y) + cx + x] += sine;
            }
        }
    }
}

void CSWaterTerrain::calcBaseWave(void)
{
    /*
        if ( rand_fps_check(10) )
        {
            m_iSelectWaveX = rand()%WATER_TERRAIN_SIZE;
            m_iSelectWaveY = rand()%WATER_TERRAIN_SIZE;
            m_iAddHeight   = rand()%20+10;
        }
    */
    int MaxHeight;
    int offset;
    int HeroX = static_cast<int>((Hero->PositionX) * 2);
    int HeroY = static_cast<int>((Hero->PositionY) * 2);
    /*
        int HeroX = ( Hero->Object.Position[0]/TERRAIN_SCALE )*2;
        int HeroY = ( Hero->Object.Position[1]/TERRAIN_SCALE )*2;
    */

    int StartX = std::max<int>(0, HeroX - (VIEW_WATER_GRID / 2));
    int StartY = std::max<int>(0, HeroY - (VIEW_WATER_GRID / 2));
    int EndX = std::min<int>(WATER_TERRAIN_SIZE, HeroX + (VIEW_WATER_GRID / 2));
    int EndY = std::min<int>(WATER_TERRAIN_SIZE, HeroY + (VIEW_WATER_GRID / 2));
    for (int i = StartY; i < EndY; i++) //  y
    {
        for (int j = StartX; j < EndX; j++) //  x
        {
            offset = j + (i * WATER_TERRAIN_SIZE);

            //  ū ���ΰ
            float alpha = 0.f; //TerrainMappingAlpha[(j/2)+(i/2)*WATER_TERRAIN_SIZE];

            MaxHeight =
                (int)(sin((simulationTimeMilliseconds_ * 0.005f) + (i * 0.1f) + (j * 0.1f)) * 50 *
                      (1 + alpha));
            m_iWaveHeight[2][offset] =
                (int)(MaxHeight -
                      sin((simulationTimeMilliseconds_ * 0.003f) + (j * 0.1f) + (i * 0.5f)) * 50 *
                          (1 + alpha));

            //  ���� ���ΰ
            MaxHeight =
                (int)(sin((simulationTimeMilliseconds_ * 0.001f) + (i * 0.5f) + (j * 0.5f)) * 25 *
                      (1 + alpha));
            m_iWaveHeight[3][offset] =
                (int)(MaxHeight -
                      sin((simulationTimeMilliseconds_ * 0.002f) + (j * 1.f) + (i * 0.3f)) * 25 *
                          (1 + alpha));
        }
    }
}

void CSWaterTerrain::calcWave(void)
{
    int newh;

    int *newptr = m_iWaveHeight[m_iWaterPage].data();
    int *oldptr = m_iWaveHeight[m_iWaterPage ^ 1].data();

    int x;
    int y = (WATER_TERRAIN_SIZE - 1) * WATER_TERRAIN_SIZE;
    for (int count = WATER_TERRAIN_SIZE + 1; count < y; count += 2)
    {
        for (x = count + WATER_TERRAIN_SIZE - 2; count < x; count++)
        {
            newh = ((oldptr[count + WATER_TERRAIN_SIZE] + oldptr[count - WATER_TERRAIN_SIZE] +
                     oldptr[count + 1] + oldptr[count - 1]) >>
                    1) -
                   newptr[count];
            newptr[count] = newh - (newh >> 4);
        }
    }
}

float CSWaterTerrain::GetWaterTerrain(float xf, float yf)
{
    int x = static_cast<int>(xf / TERRAIN_SCALE * 2);
    int y = static_cast<int>(yf / TERRAIN_SCALE * 2);

    x = std::clamp(x, 0, WATER_TERRAIN_SIZE - 1);
    y = std::clamp(y, 0, WATER_TERRAIN_SIZE - 1);

    float fHeight;
    if (m_iWaterPage)
    {
        fHeight = m_iWaveHeight[1][x + (y * WATER_TERRAIN_SIZE)] + kDefaultWaterHeight;
    }
    else
    {
        fHeight = m_iWaveHeight[0][x + (y * WATER_TERRAIN_SIZE)] + kDefaultWaterHeight;
    }

    float fHeight1 = m_iWaveHeight[2][x + (y * WATER_TERRAIN_SIZE)] + kDefaultWaterHeight;
    fHeight1 += m_iWaveHeight[3][x + (y * WATER_TERRAIN_SIZE)] + kDefaultWaterHeight;
    fHeight = (fHeight + fHeight1 / 2.f) * 0.25f;

    return fHeight;
}

void CSWaterTerrain::RenderWaterAlphaBitmap(int Texture, float xf, float yf, float SizeX,
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
    float mxf = (xf / TERRAIN_SCALE * 2);
    float myf = (yf / TERRAIN_SCALE * 2);
    int mxi = (int)(mxf);
    int myi = (int)(myf);

    float sizeMax = std::max<float>(SizeX, SizeY);
    float TexU = (((float)mxi - mxf) + 0.5f * sizeMax);
    float TexV = (((float)myi - myf) + 0.5f * sizeMax);
    float TexScaleU = 1.f / sizeMax;
    float TexScaleV = 1.f / sizeMax;
    sizeMax = static_cast<float>(static_cast<int>(sizeMax) + 1);
    float Aspect = SizeX / SizeY;
    for (float y = -sizeMax; y <= sizeMax; y += 1.f)
    {
        for (float x = -sizeMax; x <= sizeMax; x += 1.f)
        {
            vec3_t p1[4], p2[4];
            Vector((TexU + x) * TexScaleU, (TexV + y) * TexScaleV, 0.f, p1[0]);
            Vector((TexU + x + 1.f) * TexScaleU, (TexV + y) * TexScaleV, 0.f, p1[1]);
            Vector((TexU + x + 1.f) * TexScaleU, (TexV + y + 1.f) * TexScaleV, 0.f, p1[2]);
            Vector((TexU + x) * TexScaleU, (TexV + y + 1.f) * TexScaleV, 0.f, p1[3]);
            //bool Clip = false;
            for (int i = 0; i < 4; i++)
            {
                p1[i][0] -= 0.5f;
                p1[i][1] -= 0.5f;
                VectorRotate(p1[i], Matrix, p2[i]);
                p2[i][0] *= Aspect;
                p2[i][0] += 0.5f;
                p2[i][1] += 0.5f;
                //if((p2[i][0]>=0.f && p2[i][0]<=1.f) || (p2[i][1]>=0.f && p2[i][1]<=1.f)) Clip = true;
            }
            //if(Clip==true)
            RenderWaterBitmapTile((float)mxi + x, (float)myi + y, 1.f, 1, p2, false, Alpha, Height);
        }
    }
}

void CSWaterTerrain::RenderWaterBitmapTile(float xf, float yf, float lodf, int lodi, vec3_t c[4],
                                           bool LightEnable, float Alpha, float Height)
{
    vec3_t TerrainVertex[4];
    int xi = (int)xf;
    int yi = (int)yf;
    if (xi < 0 || yi < 0 || xi >= TERRAIN_SIZE_MASK || yi >= TERRAIN_SIZE_MASK)
        return;
    float TileScale = WAVE_SCALE;
    float sx = xf * WAVE_SCALE;
    float sy = yf * WAVE_SCALE;
    int TerrainIndex1 = xi + (yi * WATER_TERRAIN_SIZE);
    int TerrainIndex2 = xi + lodi + (yi * WATER_TERRAIN_SIZE);
    int TerrainIndex3 = xi + lodi + ((yi + lodi) * WATER_TERRAIN_SIZE);
    int TerrainIndex4 = xi + ((yi + lodi) * WATER_TERRAIN_SIZE);
    Vector(sx, sy, m_iWaveHeight[0][TerrainIndex1] + kTileHeightOffset + Height, TerrainVertex[0]);
    Vector(sx + TileScale, sy, m_iWaveHeight[0][TerrainIndex2] + kTileHeightOffset + Height,
           TerrainVertex[1]);
    Vector(sx + TileScale, sy + TileScale,
           m_iWaveHeight[0][TerrainIndex3] + kTileHeightOffset + Height, TerrainVertex[2]);
    Vector(sx, sy + TileScale, m_iWaveHeight[0][TerrainIndex4] + kTileHeightOffset + Height,
           TerrainVertex[3]);

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

// MainScene.cpp - Main game scene implementation

bool SessionRenderUnit::ShouldRenderLeaves()
{
    return TheMapProcess().WeatherSpritePass();
}

bool SessionRenderUnit::ShouldRenderGameTerrain()
{
    const auto *definition = sessionKeeper_.WorldState().definition;
    if (!definition || !definition->presentation.mainTerrain || IsWaterTerrain())
        return false;
    return !TheMapProcess().TerrainCutscene();
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

// Terrain ���� �Լ�

const float g_fMinHeight = -500.f;
const float g_fMaxHeight = 1000.f;

void SessionRenderUnit::InitializeTerrainGrass()
{
    // Preserve all legacy RNG draws, although only one offset per row is consumed.
    for (int index = 0; index < TERRAIN_SIZE * TERRAIN_SIZE; ++index)
    {
        const float offset = static_cast<float>(rand() % 4) / 4.0f;
        if (index < TERRAIN_SIZE)
            TerrainGrassTexture[index] = offset;
#ifdef ASG_ADD_MAP_KARUTAN
        g_fTerrainGrassWind1[index] = 0.0f;
#endif
    }
}

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

void SetTerrainLight(float xf, float yf, vec3_t Light, int Range, vec3_t *Buffer)
{
    auto rf = (float)Range;

    xf = (xf / TERRAIN_SCALE);
    yf = (yf / TERRAIN_SCALE);
    int xi = (int)xf;
    int yi = (int)yf;
    int syi = yi - Range;
    int eyi = yi + Range;
    auto syf = (float)(syi);
    for (; syi <= eyi; syi++, syf += 1.f)
    {
        int sxi = xi - Range;
        int exi = xi + Range;
        auto sxf = (float)(sxi);
        for (; sxi <= exi; sxi++, sxf += 1.f)
        {
            float xd = xf - sxf;
            float yd = yf - syf;
            float lf = (rf - sqrtf(xd * xd + yd * yd)) / rf;
            if (lf > 0.f)
            {
                float *b = &Buffer[TERRAIN_INDEX_REPEAT(sxi, syi)][0];
                for (int i = 0; i < 3; i++)
                {
                    b[i] += Light[i] * lf;
                }
            }
        }
    }
}

void AddTerrainLight(float xf, float yf, vec3_t Light, int Range, vec3_t *Buffer)
{
    auto rf = (float)Range;

    xf = (xf / TERRAIN_SCALE);
    yf = (yf / TERRAIN_SCALE);
    int xi = (int)xf;
    int yi = (int)yf;
    int syi = yi - Range;
    int eyi = yi + Range;
    auto syf = (float)(syi);
    for (; syi <= eyi; syi++, syf += 1.f)
    {
        int sxi = xi - Range;
        int exi = xi + Range;
        auto sxf = (float)(sxi);
        for (; sxi <= exi; sxi++, sxf += 1.f)
        {
            float xd = xf - sxf;
            float yd = yf - syf;
            float lf = (rf - sqrtf(xd * xd + yd * yd)) / rf;
            if (lf > 0.f)
            {
                float *b = &Buffer[TERRAIN_INDEX_REPEAT(sxi, syi)][0];
                for (int i = 0; i < 3; i++)
                {
                    b[i] += Light[i] * lf;
                    if (b[i] < 0.f)
                        b[i] = 0.f;
                }
            }
        }
    }
}

void AddTerrainLightClip(float xf, float yf, vec3_t Light, int Range, vec3_t *Buffer)
{
    auto rf = (float)Range;

    xf = (xf / TERRAIN_SCALE);
    yf = (yf / TERRAIN_SCALE);
    int xi = (int)xf;
    int yi = (int)yf;
    int syi = yi - Range;
    int eyi = yi + Range;
    auto syf = (float)(syi);
    for (; syi <= eyi; syi++, syf += 1.f)
    {
        int sxi = xi - Range;
        int exi = xi + Range;
        auto sxf = (float)(sxi);
        for (; sxi <= exi; sxi++, sxf += 1.f)
        {
            float xd = xf - sxf;
            float yd = yf - syf;
            float lf = (rf - sqrtf(xd * xd + yd * yd)) / rf;
            if (lf > 0.f)
            {
                float *b = &Buffer[TERRAIN_INDEX_REPEAT(sxi, syi)][0];
                for (int i = 0; i < 3; i++)
                {
                    b[i] += Light[i] * lf;
                    if (b[i] < 0.f)
                        b[i] = 0.f;
                    else if (b[i] > 1.f)
                        b[i] = 1.f;
                }
            }
        }
    }
}

void SessionRenderUnit::Vertex0()
{
    glTexCoord2f(TerrainTextureCoord[0][0], TerrainTextureCoord[0][1]);
    glColor3fv(PrimaryTerrainLight[TerrainIndex1]);
    glVertex3fv(TerrainVertex[0]);
}

void SessionRenderUnit::Vertex1()
{
    glTexCoord2f(TerrainTextureCoord[1][0], TerrainTextureCoord[1][1]);
    glColor3fv(PrimaryTerrainLight[TerrainIndex2]);
    glVertex3fv(TerrainVertex[1]);
}

void SessionRenderUnit::Vertex2()
{
    glTexCoord2f(TerrainTextureCoord[2][0], TerrainTextureCoord[2][1]);
    glColor3fv(PrimaryTerrainLight[TerrainIndex3]);
    glVertex3fv(TerrainVertex[2]);
}

void SessionRenderUnit::Vertex3()
{
    glTexCoord2f(TerrainTextureCoord[3][0], TerrainTextureCoord[3][1]);
    glColor3fv(PrimaryTerrainLight[TerrainIndex4]);
    glVertex3fv(TerrainVertex[3]);
}

void SessionRenderUnit::Vertex01()
{
    glTexCoord2f(TerrainTextureCoord01[0], TerrainTextureCoord01[1]);
    glColor3fv(PrimaryTerrainLight[Index01]);
    glVertex3fv(TerrainVertex01);
}

void SessionRenderUnit::Vertex12()
{
    glTexCoord2f(TerrainTextureCoord12[0], TerrainTextureCoord12[1]);
    glColor3fv(PrimaryTerrainLight[Index12]);
    glVertex3fv(TerrainVertex12);
}

void SessionRenderUnit::Vertex23()
{
    glTexCoord2f(TerrainTextureCoord23[0], TerrainTextureCoord23[1]);
    glColor3fv(PrimaryTerrainLight[Index23]);
    glVertex3fv(TerrainVertex23);
}

void SessionRenderUnit::Vertex30()
{
    glTexCoord2f(TerrainTextureCoord30[0], TerrainTextureCoord30[1]);
    glColor3fv(PrimaryTerrainLight[Index30]);
    glVertex3fv(TerrainVertex30);
}

void SessionRenderUnit::Vertex02()
{
    glTexCoord2f(TerrainTextureCoord02[0], TerrainTextureCoord02[1]);
    glColor3fv(PrimaryTerrainLight[Index02]);
    glVertex3fv(TerrainVertex02);
}

void SessionRenderUnit::VertexAlpha0()
{
    glTexCoord2f(TerrainTextureCoord[0][0], TerrainTextureCoord[0][1]);
    float *Light = &PrimaryTerrainLight[TerrainIndex1][0];
    glColor4f(Light[0], Light[1], Light[2], TerrainMappingAlpha[TerrainIndex1]);
    glVertex3fv(TerrainVertex[0]);
}

void SessionRenderUnit::VertexAlpha1()
{
    glTexCoord2f(TerrainTextureCoord[1][0], TerrainTextureCoord[1][1]);
    float *Light = &PrimaryTerrainLight[TerrainIndex2][0];
    glColor4f(Light[0], Light[1], Light[2], TerrainMappingAlpha[TerrainIndex2]);
    glVertex3fv(TerrainVertex[1]);
}

void SessionRenderUnit::VertexAlpha2()
{
    glTexCoord2f(TerrainTextureCoord[2][0], TerrainTextureCoord[2][1]);
    float *Light = &PrimaryTerrainLight[TerrainIndex3][0];
    glColor4f(Light[0], Light[1], Light[2], TerrainMappingAlpha[TerrainIndex3]);
    glVertex3fv(TerrainVertex[2]);
}

void SessionRenderUnit::VertexAlpha3()
{
    glTexCoord2f(TerrainTextureCoord[3][0], TerrainTextureCoord[3][1]);
    float *Light = &PrimaryTerrainLight[TerrainIndex4][0];
    glColor4f(Light[0], Light[1], Light[2], TerrainMappingAlpha[TerrainIndex4]);
    glVertex3fv(TerrainVertex[3]);
}

void SessionRenderUnit::VertexAlpha01()
{
    glTexCoord2f(TerrainTextureCoord01[0], TerrainTextureCoord01[1]);
    float *Light = &PrimaryTerrainLight[Index01][0];
    glColor4f(Light[0], Light[1], Light[2], TerrainMappingAlpha01);
    glVertex3fv(TerrainVertex01);
}

void SessionRenderUnit::VertexAlpha12()
{
    glTexCoord2f(TerrainTextureCoord12[0], TerrainTextureCoord12[1]);
    float *Light = &PrimaryTerrainLight[Index12][0];
    glColor4f(Light[0], Light[1], Light[2], TerrainMappingAlpha12);
    glVertex3fv(TerrainVertex12);
}

void SessionRenderUnit::VertexAlpha23()
{
    glTexCoord2f(TerrainTextureCoord23[0], TerrainTextureCoord23[1]);
    float *Light = &PrimaryTerrainLight[Index23][0];
    glColor4f(Light[0], Light[1], Light[2], TerrainMappingAlpha23);
    glVertex3fv(TerrainVertex23);
}

void SessionRenderUnit::VertexAlpha30()
{
    glTexCoord2f(TerrainTextureCoord30[0], TerrainTextureCoord30[1]);
    float *Light = &PrimaryTerrainLight[Index30][0];
    glColor4f(Light[0], Light[1], Light[2], TerrainMappingAlpha30);
    glVertex3fv(TerrainVertex30);
}

void SessionRenderUnit::VertexAlpha02()
{
    glTexCoord2f(TerrainTextureCoord02[0], TerrainTextureCoord02[1]);
    float *Light = &PrimaryTerrainLight[Index02][0];
    glColor4f(Light[0], Light[1], Light[2], TerrainMappingAlpha02);
    glVertex3fv(TerrainVertex02);
}

void SessionRenderUnit::VertexBlend0()
{
    glTexCoord2f(TerrainTextureCoord[0][0], TerrainTextureCoord[0][1]);
    float Light = TerrainMappingAlpha[TerrainIndex1];
    glColor3f(Light, Light, Light);
    glVertex3fv(TerrainVertex[0]);
}

void SessionRenderUnit::VertexBlend1()
{
    glTexCoord2f(TerrainTextureCoord[1][0], TerrainTextureCoord[1][1]);
    float Light = TerrainMappingAlpha[TerrainIndex2];
    glColor3f(Light, Light, Light);
    glVertex3fv(TerrainVertex[1]);
}

void SessionRenderUnit::VertexBlend2()
{
    glTexCoord2f(TerrainTextureCoord[2][0], TerrainTextureCoord[2][1]);
    float Light = TerrainMappingAlpha[TerrainIndex3];
    glColor3f(Light, Light, Light);
    glVertex3fv(TerrainVertex[2]);
}

void SessionRenderUnit::VertexBlend3()
{
    glTexCoord2f(TerrainTextureCoord[3][0], TerrainTextureCoord[3][1]);
    float Light = TerrainMappingAlpha[TerrainIndex4];
    glColor3f(Light, Light, Light);
    glVertex3fv(TerrainVertex[3]);
}

bool SessionRenderUnit::PrepareTerrainBaseMaterial(int Texture)
{
    const auto &policy = TheMapProcess().TerrainPolicy();
    if (Texture == policy.hiddenBaseTile)
        return false;
    if (Texture == policy.alphaTestTile)
        EnableAlphaTest();
    else
        DisableAlphaBlend();
    BindTexture(BITMAP_MAPTILE + Texture);
    return true;
}

void SessionRenderUnit::FaceTexture(int Texture, float xf, float yf, bool Water, bool Scale)
{
    const auto texture = Bitmaps.GetTextureProperties(BITMAP_MAPTILE + Texture);
    if (!texture)
    {
        return;
    }
    float Width, Height;
    if (Scale)
    {
        Width = 16.f / texture->width;
        Height = 16.f / texture->height;
    }
    else
    {
        Width = 64.f / texture->width;
        Height = 64.f / texture->height;
    }
    float suf = xf * Width;
    float svf = yf * Height;
    if (!Water)
    {
        TEXCOORD(TerrainTextureCoord[0], suf, svf);
        TEXCOORD(TerrainTextureCoord[1], suf + Width, svf);
        TEXCOORD(TerrainTextureCoord[2], suf + Width, svf + Height);
        TEXCOORD(TerrainTextureCoord[3], suf, svf + Height);
    }
    else
    {
        float Water1 = 0.f;
        float Water2 = 0.f;
        float Water3 = 0.f;
        float Water4 = 0.f;
        suf +=
            TheMapProcess().TerrainPolicy().reverseWater && Texture == 5 ? -WaterMove : WaterMove;

        if (Scale)
        {
            Water3 = TerrainGrassWind[TerrainIndex1] * 0.008f;
            Water4 = TerrainGrassWind[TerrainIndex2] * 0.008f;
        }
        else
        {
            Water3 = TerrainGrassWind[TerrainIndex1] * 0.002f;
            Water4 = TerrainGrassWind[TerrainIndex2] * 0.002f;
        }

        TEXCOORD(TerrainTextureCoord[0], suf + Water1, svf + Water3);
        TEXCOORD(TerrainTextureCoord[1], suf + Width + Water2, svf + Water4);
        TEXCOORD(TerrainTextureCoord[2], suf + Width + Water2, svf + Height + Water4);
        TEXCOORD(TerrainTextureCoord[3], suf + Water1, svf + Height + Water3);
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

void UpdateFrustrumBounds()
{
    // No-op: bounds are now computed by CreateFrustrum() each frame
}

/**
 * @brief Renders a wireframe sphere for debugging culling volumes
 * @param center Center position of the sphere in world space
 * @param radius Radius of the sphere
 * @param r Red color component (0-1)
 * @param g Green color component (0-1)
 * @param b Blue color component (0-1)
 */

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

bool SessionRenderUnit::PrepareTerrainLightSnapshot() noexcept
{
    auto &terrain = sessionKeeper_.TerrainStorage();
    if (!terrain.dynamicLightDirty)
    {
        terrainGeometryCache_.SetLightSnapshot(terrain.lightSnapshot);
        return true;
    }
    terrainGeometryCache_.SetLightSnapshot(nullptr);
    terrain.lightSnapshot.reset();
    if (terrain.dynamicLightActive)
    {
        try
        {
            auto slot =
                std::find_if(terrain.lightSnapshots.begin(), terrain.lightSnapshots.end(),
                             [](const auto &snapshot) { return snapshot.use_count() == 1; });
            if (slot == terrain.lightSnapshots.end())
            {
                terrain.lightSnapshots.push_back(std::make_shared<TerrainLightSnapshot>());
                slot = std::prev(terrain.lightSnapshots.end());
            }
            auto &snapshot = **slot;
            constexpr std::size_t LightValues = TERRAIN_SIZE * TERRAIN_SIZE * 3;
            snapshot.values.resize(LightValues);
            std::memcpy(snapshot.values.data(), PrimaryTerrainLight,
                        sizeof(terrain.PrimaryTerrainLight));
            snapshot.revision = ++terrain.lightRevision;
            terrain.lightSnapshot = *slot;
        }
        catch (...)
        {
            return false;
        }
    }
    terrainGeometryCache_.SetLightSnapshot(terrain.lightSnapshot);
    terrain.dynamicLightDirty = false;
    return true;
}

bool SessionRenderUnit::PrepareTerrainGeometryMaterial(const TerrainGeometryDraw &draw)
{
    switch (draw.pass)
    {
    case TerrainGeometryMaterialPass::Base:
        return PrepareTerrainBaseMaterial(draw.texture);
    case TerrainGeometryMaterialPass::Alpha:
        EnableAlphaTest();
        BindTexture(BITMAP_MAPTILE + draw.texture);
        return true;
    case TerrainGeometryMaterialPass::OceanBlend:
        EnableAlphaBlend();
        BindTexture(BITMAP_WATER + WaterTextureNumber);
        return true;
    case TerrainGeometryMaterialPass::AfterAlphaTest:
        EnableAlphaTest();
        BindTexture(BITMAP_MAPTILE + draw.texture);
        return true;
    case TerrainGeometryMaterialPass::AfterAlphaBlend:
        EnableAlphaBlend();
        BindTexture(BITMAP_MAPTILE + draw.texture);
        return true;
    }
    return false;
}

extern void RenderCharactersClient();

void SessionRenderUnit::UpdateTerrainWaterUv()
{
    const auto &policy = TheMapProcess().TerrainPolicy();
    WaterMove = static_cast<int>(WorldTime) % policy.waterPeriod * policy.waterRate;
}

void SessionRenderUnit::CreateSun()
{
    //Sun.Type = BITMAP_LIGHT;
    Sun.Scale = 8.f;
    Sun.AnimationFrame = 1.f;
}
