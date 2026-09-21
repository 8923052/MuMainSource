#include "session/SessionPresentation.h"
#include "ui/features/Shell/ShellLogic.h"
#include "support/CoreMath.h"
#include "session/SessionKeeper.h"
#include "session/SessionGameplay.h"
#include "domain/CharacterPresentation.h"
#include "domain/WorldSimulation.h"
#include "render/World.h"
#include "domain/EffectsUpdate.h"
#include "session/SessionUi.h"
#include "ui/session/UiSessionLogic.h"
#include "domain/MapSimulation.h"
#include "render/ModelResources.h"

void SessionVisualUnit::EmitPeriodicFlames(OBJECT &object)
{
    if (FPS_ANIMATION_FACTOR <= 0.f)
        return;
    constexpr double PeriodMilliseconds = 200.0;
    const double millisecondsPerFrame =
        1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    const double start = WorldTime - FPS_ANIMATION_FACTOR * millisecondsPerFrame;
    const double startTolerance = 4.0 * std::numeric_limits<float>::epsilon() *
                                  (std::max)(1.f, FPS_ANIMATION_FACTOR) * millisecondsPerFrame;
    const double firstPeriod = std::floor((start + startTolerance) / PeriodMilliseconds) + 1.0;
    const double lastPeriod = std::floor(WorldTime / PeriodMilliseconds);
    for (double period = firstPeriod; period <= lastPeriod; ++period)
    {
        const float remaining =
            float((WorldTime - period * PeriodMilliseconds) / millisecondsPerFrame);
        auto birth = sessionKeeper_.Gameplay()->EmissionTime(remaining);
        const int count = WorldRandom() % 4 + 4;
        vec3_t light{0.5f, 0.5f, 0.5f};
        for (int i = 0; i < count; ++i)
            CreateEffect(BITMAP_FLAME, object.Position, object.Angle, light, 6, nullptr, -1, 0,
                         object.Scale);
    }
}

bool SessionVisualUnit::PrepareWorldVisuals(WorldResources &resources,
                                            const MapDefinition &definition,
                                            WorldResources::Failure &failure)
{
    if (definition.scene == MapDefinition::Scene::Login)
    {
        auto &credits = sessionKeeper_.Ui()->LegacyUiManager().m_CreditWin;
        if (!credits || !credits->PrepareText())
        {
            failure.detail = L"Required login credits failed";
            return false;
        }
    }
    return resources.PrepareModelTextures(sessionKeeper_, failure) &&
           resources.PrepareTerrainTextures(sessionKeeper_, definition, L"Data", failure) &&
           resources.PrepareEffectTextures(sessionKeeper_, definition, L"Data", failure);
}

bool SessionVisualUnit::InstallWorldVisuals(const WorldResources &resources,
                                            WorldResources::Failure &failure)
{
    return resources.InstallModelVisuals(sessionKeeper_, failure) &&
           resources.InstallTerrainTextures(sessionKeeper_, failure) &&
           resources.InstallEffectTextures(sessionKeeper_, failure);
}

bool SessionVisualUnit::PrepareMonsterVisuals(int type, WorldResources &resources,
                                              WorldResources::Failure &failure)
{
    const wchar_t *textureFolder = L"Monster\\";
    if (gMapManager.InChaosCastle() && type >= 70 && type <= 72)
        textureFolder = L"Npc\\";
    else if (gMapManager.InBattleCastle() && type == 74)
        textureFolder = L"Object31\\";
    return gLoadData.OpenTexture(MODEL_MONSTER01 + type, textureFolder) &&
           resources.PrepareModelTextures(sessionKeeper_, failure) &&
           resources.InstallModelVisuals(sessionKeeper_, failure);
}

void SessionVisualUnit::InitTerrainLight()
{
    auto &terrain = sessionKeeper_.TerrainStorage();
    terrain.dynamicLightActive = false;
    terrain.dynamicLightDirty = true;
    std::fill_n(sessionKeeper_.TerrainStorage().TerrainDynamicLightBlocks, 64 * 64, false);
    int xi, yi;
    yi = FrustrumBoundMinY;
    for (; yi <= FrustrumBoundMaxY + 3; yi += 1)
    {
        xi = FrustrumBoundMinX;
        for (; xi <= FrustrumBoundMaxX + 3; xi += 1)
        {
            int Index = TERRAIN_INDEX_REPEAT(xi, yi);
            VectorCopy(BackTerrainLight[Index], PrimaryTerrainLight[Index]);
        }
    }
    const auto &policy = TheMapProcess().TerrainPolicy();
    const float WindScale = policy.grassWindScale;
    const float WindSpeed =
        EnableEvent == 0 ? (int)WorldTime % (360000 * 2) * 0.002f : (int)WorldTime % 36000 * 0.01f;
#ifdef ASG_ADD_MAP_KARUTAN
    const float WindSpeed1 = (int)WorldTime % 36000 * 0.008f;
#endif
    yi = FrustrumBoundMinY;

    for (; yi <= std::min<int>(FrustrumBoundMaxY + 3, TERRAIN_SIZE_MASK); yi += 1)
    {
        xi = FrustrumBoundMinX;
        auto xf = (float)xi;
        for (; xi <= std::min<int>(FrustrumBoundMaxX + 3, TERRAIN_SIZE_MASK); xi += 1, xf += 1.f)
        {
            int Index = TERRAIN_INDEX(xi, yi);
            TerrainGrassWind[Index] = sinf(WindSpeed + xf * policy.grassWindFrequency) * WindScale;
#ifdef ASG_ADD_MAP_KARUTAN
            if (policy.secondaryGrassWind)
                g_fTerrainGrassWind1[Index] =
                    sinf(WindSpeed1 + xf * policy.grassWindFrequency) * 15.f;
#endif
        }
    }
    TheMapProcess().AdvanceTerrainEffects();
}

void SessionVisualUnit::ApplySelectedCharacterLighting()
{
    if (SelectedHero == -1)
        return;

    const OBJECT *o = &CharactersClient[SelectedHero].Object;
    if (!o->Live)
        return;

    vec3_t Light;
    Vector(1.0f, 1.0f, 1.0f, Light);
    AddTerrainLight(o->Position[0], o->Position[1], Light, 1, PrimaryTerrainLight);
}

void SessionLegacyCalls::InitTerrainLight()
{
    sessionKeeper_.Visual()->InitTerrainLight();
}
void SessionLegacyCalls::ApplySelectedCharacterLighting()
{
    sessionKeeper_.Visual()->ApplySelectedCharacterLighting();
}
