#pragma once
#include <array>

// Immutable rules prepared with the authored map definition.
struct MapPresentationPolicy final
{
    enum class WeatherAdmission
    {
        Disabled,
        Enabled,
        OutsideTavern,
        OutsideChurch
    };
    WeatherAdmission weather = WeatherAdmission::Disabled;
    bool weatherSpritePass = false;
    bool weatherWaterPass = false;
    std::array<float, 3> clearColor{0.f, 0.f, 0.f};
    bool mainTerrain = true;
    bool persistentTerrain = true;
    bool objectsBeforeTerrain = false;
    bool grass = true;
    bool pkField = false;
    bool doppelGanger2 = false;
    bool doppelGanger3 = false;
    bool objectsAfterCharacters = false;
    bool objectBlockCulling = true;
    bool weatherAlphaBlend = false;
    bool weatherSprites = false;
    bool weatherTurningForce = false;
    bool weatherRainState = false;
    bool extraSnowLeaves = false;
    bool fullAmbientFishPool = false;
    bool waterFish = false;
    bool fishWallObstacles = false;
    bool finiteAmbientFish = false;
    bool persistentAmbientBoids = false;
    bool authoredBoidAnimation = false;
    bool ambientShadows = true;
    bool ambientTornadoShadow = true;
    bool finiteAmbientBoids = false;
};

struct MapTerrainPolicy final
{
    int alphaTestTile = -1;
    int hiddenBaseTile = -1;
    bool afterPass = false;
    bool grassEnabled = true;
    bool grassFaces = true;
    bool dynamicGrass = false;
    bool magma = false;
    bool ocean = false;
    bool reverseWater = false;
    bool secondaryGrassWind = false;
    float lightDirection[3] = {0.5f, -0.5f, 0.5f};
    float heightSaveScale = 1.5f;
    int waterPeriod = 20000;
    float waterRate = 0.00005f;
    float grassWindScale = 10.f;
    float grassWindFrequency = 5.f;
};
