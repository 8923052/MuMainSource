#pragma once

#include "domain/CharacterSystem.h"
#include "render/World.h"

struct CharacterDrawInput final
{
    CharacterDrawInput(const CHARACTER *character, const OBJECT *drawObject)
        : source(character), object(drawObject), hideShadow(character->HideShadow)
    {
        object.stableBones = true;
        object.preparedPose = &character->WorldVisualPoseSample;
        if (drawObject == &character->Object && character->WorldVisualPoseRevision != 0)
        {
            object.action = static_cast<unsigned short>(character->WorldVisualAction);
            object.priorAction = static_cast<unsigned short>(character->WorldVisualPriorAction);
            object.animationFrame = character->WorldVisualAnimationFrame;
            object.priorAnimationFrame = character->WorldVisualPriorAnimationFrame;
        }
        VectorCopy(character->Light, light);
        if (object.type == MODEL_PLAYER && object.action == PLAYER_SKILL_DARKSIDE_READY)
            object.angle[2] = 45.f;
    }
    CharacterDrawInput(const CHARACTER *character)
        : CharacterDrawInput(character, &character->Object)
    {
    }
    CharacterDrawInput(const CHARACTER &character) : CharacterDrawInput(&character)
    {
    }

    const CHARACTER *source;
    ObjectDrawInput object;
    vec3_t light;
    bool hideShadow;
};
