#pragma once

#include "support/CoreMath.h"

constexpr int MAX_SERVER_PER_GROUP = 20;

enum EGameScene : int
{
    SERVER_LIST_SCENE = 0,
    WEBZEN_SCENE = 1,
    LOG_IN_SCENE = 2,
    LOADING_SCENE = 3,
    CHARACTER_SCENE = 4,
    MAIN_SCENE = 5,
};

inline constexpr int g_iLengthAuthorityCode = 20;

void UpdateMainSceneGameplay();
bool UpdateSceneGameplay();
bool UpdateSceneGameplaySystems();
bool UpdateSceneSpatialAudio();

namespace SceneTransitionDetail
{
bool IsActivePresentationScene(EGameScene scene);
}
