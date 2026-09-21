#include "support/Scenes.h"

namespace SceneTransitionDetail
{
bool IsActivePresentationScene(EGameScene scene)
{
    return scene == LOG_IN_SCENE || scene == CHARACTER_SCENE || scene == MAIN_SCENE;
}
} // namespace SceneTransitionDetail
