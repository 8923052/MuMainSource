#include "render/Character.h"
#include "I18N/All.h"
#include "app/AppWindow.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationDiagnostics.h"
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
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Quests.h"
#include "domain/Shop.h"
#include "domain/WorldPhysics.h"
#include "domain/WorldSimulation.h"
#include "render/ModelGeometry.h"
#include "render/ModelResources.h"
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
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"

void PetObject::Render(bool bForceRender) const
{
    if (!m_obj || !m_obj->Live || (!bForceRender && !m_obj->Visible))
        return;
    const int state = g_isCharacterBuff(m_obj->Owner, eBuff_Cloaking) ? 10 : 0;
    ObjectDrawInput draw(m_obj);
    draw.preparedPose = &poseSample_;
    draw.stableBones = true;
    RenderObject(draw, false, 0, state);
}

ObjectDrawInput SessionRenderUnit::PrepareItemDraw(const OBJECT &object, int type)
{
    ObjectDrawInput draw(&object);
    draw.type = type;
    Vector(0.3f, 0.3f, 0.3f, draw.light);
    draw.lightEnable = true;
    draw.shadow = false;
    draw.animationFrame = draw.priorAnimationFrame = 0.f;
    draw.action = draw.priorAction = 0;
    draw.hiddenMesh = draw.blendMesh = -1;
    draw.alpha = draw.blendLight = 1.f;
    draw.blendU = draw.blendV = 0.f;
    draw.scale = type >= MODEL_SPEAR && type <= MODEL_PLATINA_STAFF ? 0.7f : 0.8f;
    draw.applyBuffs = false;
    draw.bones = nullptr;
    draw.stableBones = false;
    draw.rigidPose = nullptr;
    draw.preparedPose = nullptr;
    sessionKeeper_.Visual()->UpdateItemDrawMaterial(draw);
    return draw;
}

void SessionRenderUnit::RenderCharacterCape(const CHARACTER &character)
{
    const auto *visual = FindCharacterVisual(character);
    if (!visual || !visual->capeCloth)
        return;
    auto &cape = *visual->capeCloth;
    for (const auto index : cape.visiblePieces)
        cape.pieces[index].Render(*this, &cape.light);
}

void SessionRenderUnit::AppendMapCharacterSprites(const CHARACTER &character)
{
    const int index = CharactersClient.FindIndexByKey(character.Key);
    if (index < 0)
        return;
    AppendCharacterSprites(CharactersClient.WorldVisuals(index));
}

void SessionRenderUnit::AppendCharacterSprites(const WorldCharacterVisualState &visual)
{
    AppendPreparedCharacterSprites(visual.sprites);
}

void SessionRenderUnit::AppendPreparedCharacterSprites(
    const std::unique_ptr<SessionSpriteStorage> &prepared)
{
    if (!prepared)
        return;
    for (std::size_t spriteIndex = 0; spriteIndex < prepared->simulated; ++spriteIndex)
    {
        const auto &sprite = (*prepared)[spriteIndex];
        CreateSprite(sprite.Type, sprite.Position, sprite.Scale, sprite.Light, sprite.Owner,
                     sprite.Angle[2], sprite.SubType);
    }
}

void SessionRenderUnit::RenderCharacterAttachments(const CHARACTER &character,
                                                   const WorldCharacterVisualState &visual,
                                                   bool forceRender)
{
    if (visual.darkSpirit)
        visual.darkSpirit->RenderPet(g_isCharacterBuff(&character.Object, eBuff_Cloaking) ? 10 : 0);
    if (forceRender || TheMapProcess().CharacterPolicy().mounts)
    {
        if (visual.mount)
        {
            ObjectDrawInput draw(&visual.mount->object);
            draw.preparedPose = &visual.mount->poseSample_;
            draw.stableBones = true;
            RenderMount(draw, forceRender);
        }
        if (visual.helperPet)
            visual.helperPet->Render(forceRender);
    }
    AppendPreparedCharacterSprites(visual.attachmentSprites);
}

// CharacterScene.cpp - Character selection scene implementation

// Forward declaration
BOOL Util_CheckOption(std::wstring lpszCommandLine, wchar_t cOption, std::wstring &lpszString);

/**
 * @brief Renders all 3D elements for the character selection scene.
 */
void SessionRenderUnit::RenderCharacterScene3D()
{
    const auto beginWorldPass = [this](RenderTapePass pass) {
        return SwitchWorldRenderTapePass(pass, 0, 0, REFERENCE_WIDTH, REFERENCE_HEIGHT);
    };

    if (!BeginRenderTapePass(RenderTapePass::Terrain))
        return;
    RenderTerrain(false);
    if (!beginWorldPass(RenderTapePass::Objects))
        return;
    RenderObjects();
    if (!beginWorldPass(RenderTapePass::Characters))
        return;
    RenderCharactersClient();

    RenderCharacterAttachments();
    if (!beginWorldPass(RenderTapePass::Effects))
        return;
    RenderBlurs();
    RenderJoints();
    RenderEffects();
    if (!beginWorldPass(RenderTapePass::Effects))
        return;
    RenderBoids();
    if (!beginWorldPass(RenderTapePass::Objects))
        return;
    RenderObjects_AfterCharacter();
    if (!beginWorldPass(RenderTapePass::Sprites))
        return;
    if (!beginWorldPass(RenderTapePass::Effects))
        return;
    RenderSelectedCharacterEffects();
}

void SessionLegacyCalls::RenderCharacterScene3D()
{
    return sessionKeeper_.Renderer()->RenderCharacterScene3D();
} // OMF-01767
// OMF-01769
// OMF-01770

// 케릭터 관련 함수
// 케릭터 랜더링, 움직임등을 처리
// *** 함수 레벨: 3

#define RGZ_FIX_ATTACK_SPEED
void SessionRenderUnit::RenderGuild(const ObjectDrawInput &draw, int Type, vec3_t vPos)
{
    EnableAlphaTest();
    EnableCullFace();
    glColor3f(1.f, 1.f, 1.f);
    BindTexture(BITMAP_GUILD);
    glPushMatrix();

    float Matrix[3][4];
    vec3_t Angle;
    VectorCopy(draw.angle, Angle);
    Angle[2] += 90.f + 45.f;
    Angle[1] += 45.f;
    Angle[0] += 80.f;
    AngleMatrix(Angle, Matrix);
    Matrix[0][3] = 20.f;
    Matrix[1][3] = -5.f;
    if (Type == MODEL_THUNDER_HAWK_ARMOR && Type != -1)
    {
        Matrix[2][3] = -18.f; //-5
    }
    else
    {
        Matrix[2][3] = -10.f; //-5
    }

    if (vPos != NULL)
    {
        Matrix[0][3] = vPos[0];
        Matrix[1][3] = vPos[1];
        Matrix[2][3] = vPos[2];
    }

    R_ConcatTransforms(draw.bones[26], Matrix, ParentMatrix);
    glTranslatef(draw.position[0], draw.position[1], draw.position[2]);
    RenderPlane3D(5.f, 7.f, ParentMatrix);

    glPopMatrix();
    DisableCullFace();
}

void SessionRenderUnit::RenderCharacterCloth(const CHARACTER &character, const vec3_t *light)
{
    const auto *visual = FindCharacterVisual(character);
    if (!visual || !visual->bodyCloth)
        return;
    for (std::size_t index = 0; index < visual->bodyCloth->count; ++index)
        visual->bodyCloth->pieces[index].Render(*this, light);
}
namespace CharacterPresentationDetail
{
void RenderLinkedItemShadow(BMD &model, int type, const CharacterClothVisual *cloth)
{
    switch (type) // 날개인지 검사
    {
    case MODEL_WINGS_OF_ELF:    // Wings of Elf
    case MODEL_WINGS_OF_HEAVEN: // Wings of Heaven
    case MODEL_WINGS_OF_SATAN:  // Wings of Satan
    case MODEL_WING_OF_CURSE:   // Wing of Curse

    case MODEL_WINGS_OF_SPIRITS:  // Wings of Spirits
    case MODEL_WINGS_OF_SOUL:     // Wings of Soul
    case MODEL_WINGS_OF_DRAGON:   // Wings of Dragon
    case MODEL_WINGS_OF_DARKNESS: // Wings of Darkness
    case MODEL_WINGS_OF_DESPAIR:  // Wings of Despair

    case MODEL_WING_OF_STORM:     // Wing of Storm
    case MODEL_WING_OF_ETERNAL:   // Wing of Eternal
    case MODEL_WING_OF_ILLUSION:  // Wing of Illusion
    case MODEL_WING_OF_RUIN:      // Wing of Ruin
    case MODEL_WING_OF_DIMENSION: // Wing of Dimension

    case MODEL_WING + 131: // Small Wing of Curse
    case MODEL_WING + 132: // Small Wings of Elf
    case MODEL_WING + 133: // Small Wings of Heaven
    case MODEL_WING + 134: // Small Wings of Satan
        model.RenderBodyShadow();
        break;

    case MODEL_CAPE_OF_LORD:     // Cape of Lord
    case MODEL_CAPE_OF_FIGHTER:  // Cape of Fighter
    case MODEL_CAPE_OF_EMPEROR:  // Cape of Emperor
    case MODEL_CAPE_OF_OVERRULE: // Cape of Overrule
    case MODEL_WING + 130:       // Small Cape of Lord
    case MODEL_WING + 135:       // Little Warrior's Cloak
        model.RenderBodyShadow(-1, -1, -1, -1, cloth);
        break;
    default:
        if (cloth)
        {
            model.RenderBodyShadow(-1, -1, -1, -1, cloth);
        }
        break;
    }
}
} // namespace CharacterPresentationDetail

const vec34_t *SessionRenderUnit::RenderLinkObject(float x, float y, float z,
                                                   const CharacterDrawInput &characterDraw,
                                                   const PART_t *f, int Type, int Level,
                                                   int Option1, bool Link, bool Translate,
                                                   int RenderType, bool bRightHandItem, int slot)
{
    const auto *c = characterDraw.source;
    const auto *o = characterDraw.object.source;
    const auto &ownerDraw = characterDraw.object;
    BMD *b = &Models[Type];
    const auto *visual = FindCharacterVisual(*c);
    if (!visual)
        return nullptr;
    const auto found = visual->linkedItems.find(slot);
    if (found == visual->linkedItems.end() || found->second->item.Type != Type)
        return nullptr;
    const auto &prepared = *found->second;
    f = &prepared.playback;
    const auto *preparedCloth =
        visual ? (visual->bodyCloth ? visual->bodyCloth.get() : visual->capeCloth.get()) : nullptr;

    if (!sessionKeeper_.Visual()->CanPresentLinkedItem(*c, Type))
        return nullptr;

    //CopyShadowAngle(f,b);
    b->ContrastEnable = o->ContrastEnable;
    b->BodyScale = ownerDraw.scale;
    b->CurrentAction = f->CurrentAction;
    b->BodyHeight = 0.f;

    ObjectDrawInput itemDraw(&prepared.item);
    itemDraw.shadow = ownerDraw.shadow;
    itemDraw.renderShadow = ownerDraw.renderShadow;
    itemDraw.skillCount = ownerDraw.skillCount;
    b->LightEnable = false;

    if (c->MonsterIndex >= MONSTER_TERRIBLE_BUTCHER && c->MonsterIndex <= MONSTER_DOPPELGANGER_SUM)
    {
        if (TheMapProcess().CharacterPolicy().cloneAppearances)
            RenderType = RENDER_DOPPELGANGER | RENDER_TEXTURE;
        else
            RenderType = RENDER_DOPPELGANGER | RENDER_BRIGHT | RENDER_TEXTURE;
    }

    OBB_t OBB;
    vec3_t p, Position;

    sessionKeeper_.Visual()->BuildCharacterItemParent(x, y, z, characterDraw, *f, Type, Link,
                                                      bRightHandItem, ParentMatrix, b->BodyOrigin,
                                                      itemDraw.angle, b->BodyScale);
    if (Type == MODEL_BOSS_HEAD)
    {
        b->BoneHead = 0;
        Vector(0.f, 0.f, WorldTime, itemDraw.angle);
    }

    VectorCopy(b->BodyOrigin, itemDraw.position);
    itemDraw.scale = b->BodyScale;

    vec3_t Temp{};
    AnimationPoseSample sample(itemDraw, b->BoneHead, 0.f, true, b->PoseAssetIdentity());
    sample.SetParent(ParentMatrix);
    VectorCopy(itemDraw.angle, sample.headAngle.data());

    const bool knightWeapon = g_CMonkSystem.IsRagefighterCommonWeapon(c->Class, Type) && !Link;
    constexpr float knightScale = 0.9f;
    if (knightWeapon && Type == MODEL_FLAIL)
    {
        sample.blendBone = 2;
        sample.blendWeight = knightScale;
    }
    const auto *itemBones =
        prepared.poseSample == sample ? prepared.item.BoneTransform : drawPoses_.Sample(*b, sample);
    itemDraw.bones = itemBones;
    itemDraw.stableBones = true;
    b->Transform(itemBones, Temp, Temp, &OBB, Translate, knightWeapon ? knightScale : 0.f, true);

    if (Type != MODEL_CAPE_OF_LORD       // Cape of Lord
        && Type != MODEL_CAPE_OF_FIGHTER // Cape of Fighter
        && Type != MODEL_WING + 130      // Small Cape of Lord
        && Type != MODEL_WING + 135      // Small Cape of Fighter
    )
    {
        RenderPartObjectEffect(itemDraw, Type, characterDraw.light, ownerDraw.alpha, Level, Option1,
                               false, 0,
                               RenderType | ((c->MonsterIndex == MONSTER_METAL_BALROG ||
                                              c->MonsterIndex == MONSTER_ORC_ARCHER_OF_DOOM)
                                                 ? (RENDER_EXTRA | RENDER_TEXTURE)
                                                 : RENDER_TEXTURE));
    }

    if (itemDraw.shadow)
    {
        return itemBones;
    }

    if (TheMapProcess().GroundShadowsVisible())
    {
        CharacterPresentationDetail::RenderLinkedItemShadow(*b, Type, preparedCloth);
    }
    return itemBones;
}

void SessionRenderUnit::RenderLight(const ObjectDrawInput &draw, int Texture, float Scale, int Bone,
                                    float x, float y, float z)
{
    BMD *b = &Models[draw.type];
    vec3_t p, Position;
    Vector(x, y, z, p);
    b->TransformPosition(draw.bones[Bone], p, Position, true);
    float Luminosity = sinf(WorldTime * 0.002f) * 0.3f + 0.7f;
    vec3_t Light;
    Vector(Luminosity * 1.f, Luminosity * 0.6f, Luminosity * 0.4f, Light);
    CreateSprite(Texture, Position, Scale, Light, draw.source);
}

void SessionRenderUnit::RenderEye(const ObjectDrawInput &draw, int Left, int Right, float fSize)
{
    BMD *b = &Models[draw.type];
    vec3_t p, Position;
    float Luminosity = sinf(WorldTime * 0.002f) * 0.3f + 0.8f;
    vec3_t Light;
    Vector(Luminosity, Luminosity, Luminosity, Light);
    Vector(5.f, 0.f, 0.f, p);
    b->TransformPosition(draw.bones[Left], p, Position, true);
    //CreateParticle(BITMAP_SHINY+3,Position,draw.angle,Light);
    CreateSprite(BITMAP_SHINY + 3, Position, fSize, Light, NULL);
    Vector(-5.f, 0.f, 0.f, p);
    b->TransformPosition(draw.bones[Right], p, Position, true);
    //CreateParticle(BITMAP_SHINY+3,Position,draw.angle,Light);
    CreateSprite(BITMAP_SHINY + 3, Position, fSize, Light, NULL);
}

void SessionRenderUnit::PrepareCharacterDrawLight(CharacterDrawInput &characterDraw, vec3_t Light)
{
    const auto *c = characterDraw.source;
    auto &draw = characterDraw.object;
    if (SceneFlag == CHARACTER_SCENE)
    {
        Vector(0.4f, 0.4f, 0.4f, Light);
    }
    else
    {
        RequestTerrainLight(draw.position[0], draw.position[1], Light);
    }

    VectorAdd(Light, draw.light, characterDraw.light);
    if (draw.type == MODEL_DARK_PHEONIX_SHIELD)
    {
        Vector(.6f, .6f, .6f, characterDraw.light);
    }

    else if (c->MonsterIndex >= MONSTER_TERRIBLE_BUTCHER &&
             c->MonsterIndex <= MONSTER_DOPPELGANGER_SUM)
    {
        characterDraw.hideShadow = true;

        if (TheMapProcess().CharacterPolicy().cloneAppearances)
        {
            Vector(0.5f, 0.7f, 1.0f, characterDraw.light);
            draw.alpha = 0.7f;
        }
        else
        {
            Vector(1.0f, 0.3f, 0.1f, characterDraw.light);
        }

        if (!TheMapProcess().CharacterPolicy().cloneAppearances && draw.action == PLAYER_DIE1)
        {
            const float alpha = draw.alpha;
            Vector(alpha, 0.3f * alpha, 0.1f * alpha, characterDraw.light);
        }
        draw.blendMesh = -1;
    }

    if (c == Hero)
    {
        vec3_t AbilityLight = {1.f, 1.f, 1.f};
        if (CharacterAttribute->Ability & ABILITY_FAST_ATTACK_SPEED)
        {
            AbilityLight[0] *= 0.9f;
            AbilityLight[1] *= 0.5f;
            AbilityLight[2] *= 0.5f;
        }
        if (CharacterAttribute->Ability & ABILITY_PLUS_DAMAGE)
        {
            AbilityLight[0] *= 0.5f;
            AbilityLight[1] *= 0.9f;
            AbilityLight[2] *= 0.5f;
        }
        if (CharacterAttribute->Ability & ABILITY_FAST_ATTACK_RING)
        {
            AbilityLight[0] *= 0.9f;
            AbilityLight[1] *= 0.5f;
            AbilityLight[2] *= 0.5f;
        }
        if (CharacterAttribute->Ability & ABILITY_FAST_ATTACK_SPEED2)
        {
            AbilityLight[0] *= 0.9f;
            AbilityLight[1] *= 0.5f;
            AbilityLight[2] *= 0.5f;
        }
        if (SceneFlag == CHARACTER_SCENE)
        {
            Vector(0.5f, 0.5f, 0.5f, Light);
            VectorAdd(Light, draw.light, characterDraw.light);
        }
        else
            VectorCopy(AbilityLight, characterDraw.light);
    }
}

void SessionRenderUnit::RenderCharacter(const CHARACTER *c, const OBJECT *o, int Select,
                                        const WorldCharacterVisualState *localVisual,
                                        bool drawLocalAttachments)
{
    CharacterDrawInput characterDraw(c, o);
    auto &draw = characterDraw.object;
    const auto *visual = localVisual != nullptr ? localVisual : FindCharacterVisual(*c);
    if (visual)
        visual->movement.Apply(draw);
    if (SceneFlag == CHARACTER_SCENE && !(localVisual && drawLocalAttachments))
    {
        const float selectedLight =
            SelectedHero >= 0 && c == &CharactersClient[SelectedHero] ? 1.f : 0.f;
        Vector(selectedLight, selectedLight, selectedLight, characterDraw.object.light);
    }
    if (draw.type == MODEL_CURSED_SANTA && visual && visual->santa)
    {
        VectorCopy(visual->santa->position, draw.position);
        draw.angle[2] = visual->santa->angle;
        if (draw.animationFrame >= 9.f && draw.animationFrame < 13.f)
            draw.alpha = 1.f;
    }
    struct CharacterRecordScope final
    {
        bool &active;
        bool previous;
        const CHARACTER *&character;
        const CHARACTER *previousCharacter;
        const WorldCharacterVisualState *&visual;
        const WorldCharacterVisualState *previousVisual;
        ~CharacterRecordScope()
        {
            active = previous;
            character = previousCharacter;
            visual = previousVisual;
        }
    } scope{recordingCharacter_, recordingCharacter_,     drawingCharacter_,
            drawingCharacter_,   drawingCharacterVisual_, drawingCharacterVisual_};
    recordingCharacter_ = true;
    drawingCharacter_ = c;
    drawingCharacterVisual_ = visual;
    if (g_isCharacterBuff(o, eBuff_CrywolfNPCHide))
    {
        return;
    }

    BMD *b = &Models[draw.type];
    if (Models[draw.type].NumActions == 0)
        return;

    const CPhysicsClothMesh *bodyMesh =
        visual && visual->bodyCloth &&
                visual->bodyCloth->kind == CharacterClothVisual::Kind::MagicSkeleton
            ? static_cast<const CPhysicsClothMesh *>(visual->bodyCloth->pieces)
            : nullptr;
    BMD::MeshDrawScope clothDraw(*b, bodyMesh ? bodyMesh->MeshIndex() : -1,
                                 bodyMesh ? &bodyMesh->DrawMesh() : nullptr);

    bool Translate = true;

    vec3_t p, Position, Light;

    Vector(0.f, 0.f, 0.f, p);
    Vector(1.f, 1.f, 1.f, Light);
    PrepareCharacterDrawLight(characterDraw, Light);

    BYTE byRender = CHARACTER_NONE;

    switch (c->MonsterIndex)
    {
    case MONSTER_MAGIC_SKELETON_1:
    case MONSTER_MAGIC_SKELETON_2:
    case MONSTER_MAGIC_SKELETON_3:
    case MONSTER_MAGIC_SKELETON_4:
    case MONSTER_MAGIC_SKELETON_5:
    case MONSTER_MAGIC_SKELETON_6:
    case MONSTER_MAGIC_SKELETON_7: {
        BOOL bRender = Calc_RenderObject(draw, Translate, Select, 0);

        RenderCharacterCloth(*c);
        if (bRender)
        {
            Draw_RenderObject(draw, Translate, Select, 0);
        }
    }
    break;

    default:
        if (draw.type == MODEL_PLAYER)
        {
            byRender = CHARACTER_ANIMATION;
        }
        else
            byRender = CHARACTER_RENDER_OBJ;
        break;
    }

    if (byRender == CHARACTER_ANIMATION)
        Calc_ObjectAnimation(draw, Translate, Select);

    if (draw.alpha >= 0.5f && characterDraw.hideShadow == false)
    {
        if (!TheMapProcess().CharacterPolicy().skyTerrain && (draw.type == MODEL_PLAYER) &&
            (!(MODEL_HORN_OF_UNIRIA <= c->Helper.Type &&
               c->Helper.Type <= MODEL_HORN_OF_DINORANT) ||
             c->SafeZone) &&
            gMapManager.InHellas() == false)
        {
            draw.shadow = true;
            for (int i = MAX_BODYPART - 1; i >= 0; i--)
            {
                const PART_t *p = &c->BodyPart[i];
                if (p->Type != -1)
                {
                    int Type = p->Type;

                    RenderPartObject(draw, Type, p, characterDraw.light, draw.alpha, 0, 0, 0, false,
                                     false, Translate);
                }
                else
                {
                    RenderPartObject(draw, MODEL_SHADOW_BODY, NULL, characterDraw.light, draw.alpha,
                                     0, 0, 0, false, false, Translate);
                }
            }

            for (int i = 0; i < 2; i++)
            {
                if (c->Weapon[i].Type >= MODEL_SHORT_BOW && c->Weapon[i].Type <= MODEL_BOW + 32)
                {
                    continue;
                }

                const PART_t *p = &c->Weapon[i];

                if (p->Type != -1 && c->SafeZone == false)
                {
                    int Type = p->Type;
                    PART_t ShadowPart = *p;

                    RenderLinkObject(0.f, 0.f, 0.f, characterDraw, &ShadowPart, Type, 0, 0, false,
                                     Translate, 0, true, i);
                }
            }
            draw.shadow = false;
        }
    }

    if (byRender == CHARACTER_RENDER_OBJ)
    {
        if (MONSTER_METAL_BALROG == c->MonsterIndex || MONSTER_ALPHA_CRUST == c->MonsterIndex ||
            MONSTER_GREAT_DRAKAN == c->MonsterIndex || MONSTER_WHITE_WIZARD == c->MonsterIndex ||
            MONSTER_ORC_SOLDIER_OF_DOOM == c->MonsterIndex ||
            MONSTER_ORC_ARCHER_OF_DOOM == c->MonsterIndex ||
            MONSTER_MUTANT_HERO == c->MonsterIndex || MONSTER_OMEGA_WING == c->MonsterIndex ||
            MONSTER_AXE_HERO == c->MonsterIndex || MONSTER_GIGAS_GOLEM == c->MonsterIndex ||
            MONSTER_SCOUTHERO == c->MonsterIndex || MONSTER_WEREWOLFHERO == c->MonsterIndex ||
            MONSTER_VALAM == c->MonsterIndex || MONSTER_SOLAM == c->MonsterIndex ||
            MONSTER_SCOUT == c->MonsterIndex || 319 == c->MonsterIndex)
        {
            draw.bones = RenderObject(draw, Translate, Select, c->MonsterIndex);
        }
        else
        {
            if ((c->MonsterIndex == MONSTER_DREADFEAR && draw.action == MONSTER01_ATTACK2))
            {
                draw.bones = RenderObject_AfterImage(draw, Translate, Select, 0);
            }
            else
            {
                draw.bones = RenderObject(draw, Translate, Select, 0);
            }
        }
    }

    if (!characterDraw.hideShadow && CharacterPresentationDetail::CharacterUsesGroundShadow(
                                         *c, TheMapProcess().CharacterPolicy().skyTerrain))
    {
        if (draw.alpha >= 0.3f)
        {
            if (c->MonsterIndex == MONSTER_ARCHANGEL)
                draw.hiddenMesh = 2;
            else if (c->MonsterIndex == MONSTER_MESSENGER_OF_ARCH)
                draw.hiddenMesh = 2;

            if (draw.type != MODEL_STATUE_OF_SAINT && draw.type != MODEL_CASTLE_GATE &&
                !(draw.type >= MODEL_FACE && draw.type <= MODEL_FACE + 6))
            {
                draw.shadow = true;
                RenderPartObject(draw, draw.type, NULL, characterDraw.light, draw.alpha, 0, 0, 0,
                                 false, false, Translate);
                draw.shadow = false;
            }
            if (c->MonsterIndex == MONSTER_ARCHANGEL ||
                c->MonsterIndex == MONSTER_MESSENGER_OF_ARCH)
            {
                EnableAlphaBlend();

                vec3_t Position, Light;

                VectorCopy(draw.position, Position);
                Position[2] += 20.f;

                float Luminosity = sinf(WorldTime * 0.0015f) * 0.3f + 0.8f;

                Vector(Luminosity * 0.5f, Luminosity * 0.5f, Luminosity, Light);
                RenderTerrainAlphaBitmap(BITMAP_MAGIC + 1, draw.position[0], draw.position[1], 2.7f,
                                         2.7f, Light, -draw.angle[2]);

                draw.hiddenMesh = -1;
            }
        }
    }

    if (c->MonsterIndex == MONSTER_BALROG || c->MonsterIndex == MONSTER_GOLDEN_BUDGE_DRAGON ||
        c->MonsterIndex == MONSTER_SILVER_VALKYRIE || c->MonsterIndex == MONSTER_ZAIKAN ||
        c->MonsterIndex == MONSTER_METAL_BALROG ||
        (78 <= c->MonsterIndex && c->MonsterIndex <= MONSTER_GOLDEN_WHEEL) ||
        (c->MonsterIndex >= MONSTER_GOLDEN_DARK_KNIGHT && c->MonsterIndex <= MONSTER_GOLDEN_RABBIT))
    {
        vec3_t vBackupBodyLight;

        float Bright = 1.f;
        if (c->MonsterIndex == MONSTER_ZAIKAN)
            Bright = 0.5f;
        if (c->MonsterIndex == MONSTER_GOLDEN_BUDGE_DRAGON ||
            (78 <= c->MonsterIndex && c->MonsterIndex <= MONSTER_GOLDEN_WHEEL) ||
            (c->MonsterIndex >= MONSTER_GOLDEN_DARK_KNIGHT &&
             c->MonsterIndex <= MONSTER_GOLDEN_RABBIT))
        {
            if (c->MonsterIndex >= MONSTER_GOLDEN_DARK_KNIGHT)
            {
                VectorCopy(Models[draw.type].BodyLight, vBackupBodyLight);
                Vector(1.f, 0.6f, 0.3f, Models[draw.type].BodyLight);
            }

            if (c->MonsterIndex == MONSTER_GOLDEN_GREAT_DRAGON)
            {
                VectorCopy(Models[draw.type].BodyLight, vBackupBodyLight);
                Vector(1.f, 0.0f, 0.0f, Models[draw.type].BodyLight);
            }
            RenderPartObjectBodyColor(&Models[draw.type], draw, draw.type, draw.alpha,
                                      RENDER_METAL | RENDER_BRIGHT, Bright);
        }

        if (c->MonsterIndex == MONSTER_METAL_BALROG)
        {
            RenderPartObjectBodyColor(&Models[draw.type], draw, draw.type, draw.alpha,
                                      RENDER_CHROME | RENDER_BRIGHT | RENDER_EXTRA, Bright);
        }
        else
        {
            RenderPartObjectBodyColor(&Models[draw.type], draw, draw.type, draw.alpha,
                                      RENDER_CHROME | RENDER_BRIGHT, Bright);
        }

        if (c->MonsterIndex >= MONSTER_GOLDEN_DARK_KNIGHT &&
            c->MonsterIndex <= MONSTER_GOLDEN_RABBIT)
        {
            VectorCopy(vBackupBodyLight, Models[draw.type].BodyLight);
        }
    }
    else if (c->MonsterIndex == MONSTER_QUEEN_RAINER)
    {
        b->TransformPosition(draw.bones[20], p, Position, true);
        CreateSprite(BITMAP_LIGHT, Position, 0.8f, Light, o);
    }
    else if (c->MonsterIndex == MONSTER_MEGA_CRUST || c->MonsterIndex == MONSTER_ALPHA_CRUST ||
             c->MonsterIndex == MONSTER_OMEGA_WING)
    {
        RenderCharacterCloth(*c);
    }
    else if (c->MonsterIndex == MONSTER_DRAKAN || c->MonsterIndex == MONSTER_GREAT_DRAKAN)
    {
        int RenderType = (c->MonsterIndex == MONSTER_DRAKAN) ? 0 : RENDER_EXTRA;
        RenderPartObjectBodyColor(&Models[draw.type], draw, draw.type, draw.alpha,
                                  RENDER_CHROME | RENDER_BRIGHT | RenderType, 1.f);

        if (c->MonsterIndex == MONSTER_DRAKAN)
        {
            RenderPartObjectBodyColor2(&Models[draw.type], draw, draw.type, draw.alpha,
                                       RENDER_CHROME2 | RENDER_LIGHTMAP | RENDER_BRIGHT, 1.f);
        }
    }
    else if (c->MonsterIndex == MONSTER_DARK_PHOENIX)
    {
        float fSin = 0.5f * (1.0f + sinf((float)((int)WorldTime % 10000) * 0.001f));
        RenderPartObjectBodyColor(&Models[draw.type], draw, draw.type, draw.alpha,
                                  RENDER_CHROME | RENDER_BRIGHT, 0.3f + fSin * 0.7f);
        fSin = 0.3f * (1.0f - fSin) + 0.3f;
        draw.blendLight = fSin;
        draw.blendMesh = 0;
        draw.bones = RenderObject(draw, Translate, 2, 0);
        draw.bones = RenderObject(draw, Translate, 3, 0);
        draw.blendMesh = -1;
        auto wingDraw = draw;
        ++wingDraw.type;
        if (visual && visual->bodyCloth)
        {
            wingDraw.bones = visual->bodyCloth->poseBones.get();
            wingDraw.preparedPose = &visual->bodyCloth->poseSample;
        }
        RenderObject(wingDraw, Translate, Select, 0);

        RenderCharacterCloth(*c);
    }

    if (c->MonsterIndex == MONSTER_GOLDEN_TITAN || c->MonsterIndex == MONSTER_GOLDEN_SOLDIER)
    {
        RenderPartObjectBodyColor(&Models[draw.type], draw, draw.type, draw.alpha,
                                  RENDER_METAL | RENDER_BRIGHT, 1.f, BITMAP_SHINY + 1);
    }

    if (c->MonsterIndex == MONSTER_RED_DRAGON)
    {
        PART_t drawPart = c->Wing;
        PART_t *w = &drawPart;
        w->Type = MODEL_BOSS_HEAD;
        w->LinkBone = 9;
        w->CurrentAction = 1;
        w->PriorAction = 1;
        w->PlaySpeed = 0.2f;
        RenderLinkObject(0.f, 0.f, -40.f, characterDraw, w, w->Type, 0, 0, false, Translate, 0,
                         true, CharacterLinkedItemVisual::DragonHead);

        w->Type = MODEL_PRINCESS;
        w->LinkBone = 61;
        float TempScale = draw.scale;
        vec3_t TempLight;
        VectorCopy(characterDraw.light, TempLight);
        Vector(1.f, 1.f, 1.f, characterDraw.light);
        draw.scale = 0.9f;
        RenderLinkObject(0.f, -40.f, 45.f, characterDraw, w, w->Type, 0, 0, false, Translate, 0,
                         true, CharacterLinkedItemVisual::Princess);
        VectorCopy(TempLight, characterDraw.light);
        draw.scale = TempScale;
    }
    else if (c->MonsterIndex >= MONSTER_STATUE_OF_SAINT_1 &&
             c->MonsterIndex <= MONSTER_STATUE_OF_SAINT_3)
    {
        PART_t drawPart = c->Wing;
        PART_t *w = &drawPart;
        w->LinkBone = 1;
        w->CurrentAction = 1;
        w->PriorAction = 1;
        w->PlaySpeed = 0.2f;
        float TempScale = draw.scale;
        draw.scale = 0.7f;

        if (c->MonsterIndex == MONSTER_STATUE_OF_SAINT_1)
            w->Type = MODEL_DIVINE_STAFF_OF_ARCHANGEL;
        if (c->MonsterIndex == MONSTER_STATUE_OF_SAINT_2)
            w->Type = MODEL_DIVINE_SWORD_OF_ARCHANGEL;
        if (c->MonsterIndex == MONSTER_STATUE_OF_SAINT_3)
        {
            w->Type = MODEL_DIVINE_CB_OF_ARCHANGEL;
            draw.scale = 0.9f;
        }

        RenderLinkObject(0.f, 0.f, 0.f, characterDraw, w, w->Type, 0, 0, true, true, 0, true,
                         CharacterLinkedItemVisual::Statue);
        draw.scale = TempScale;
    }
    else if (c->MonsterIndex == MONSTER_WHITE_WIZARD)
    {
        RenderPartObjectBodyColor(&Models[draw.type], draw, draw.type, draw.alpha,
                                  RENDER_BRIGHT | RENDER_EXTRA, 1.0f);
    }

    if (o->Kind == KIND_NPC && TheMapProcess().CharacterPolicy().festiveUniform &&
        draw.type == MODEL_PLAYER &&
        (o->SubType >= MODEL_SKELETON1 && o->SubType <= MODEL_SKELETON3))
    {
        RenderPartObject(draw, o->SubType, NULL, characterDraw.light, draw.alpha, c->Level, 0, 0,
                         false, false, Translate, Select);
    }
    else if (o->Kind == KIND_PLAYER && draw.type == MODEL_PLAYER &&
             gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER &&
             (o->SubType == MODEL_SKELETON_PCBANG || o->SubType == MODEL_HALLOWEEN ||
              o->SubType == MODEL_PANDA || o->SubType == MODEL_SKELETON_CHANGED ||
              o->SubType == MODEL_XMAS_EVENT_CHANGE_GIRL || o->SubType == MODEL_SKELETON1))
    {
        PART_t transformedPart;
        transformedPart.Type = o->SubType;
        PART_t *p = &transformedPart;
        if (draw.action == PLAYER_SKILL_DRAGONKICK)
        {
            p->Type = o->SubType;
            RenderCharacter_AfterImage(characterDraw, p, Translate, Select, 2.5f, 1.0f);
        }
        else if (draw.action == PLAYER_SKILL_DARKSIDE_READY ||
                 draw.action == PLAYER_SKILL_DARKSIDE_ATTACK)
        {
            const OBJECT *pObj = &c->Object;
            if (pObj->m_sTargetIndex < 0 || c->JumpTime > 0)
            {
                RenderPartObject(draw, o->SubType, p, characterDraw.light, draw.alpha, 0, 0, 0,
                                 false, false, Translate, Select);
            }
            else
            {
                p->Type = o->SubType;
                RenderCharacterDarkside(characterDraw, p, Translate, Select);
            }
        }
        else
        {
            RenderPartObject(draw, o->SubType, NULL, characterDraw.light, draw.alpha, 0, 0, 0,
                             false, false, Translate, Select);
        }

        if (o->SubType == MODEL_XMAS_EVENT_CHANGE_GIRL)
        {
            RenderParts(c);
        }
    }
    else if (o->Kind == KIND_PLAYER && draw.type == MODEL_PLAYER &&
             o->SubType == MODEL_SKELETON_PCBANG)
    {
        RenderPartObject(draw, o->SubType, NULL, characterDraw.light, draw.alpha, 0, 0, 0, false,
                         false, Translate, Select);
    }
    else if (o->Kind == KIND_PLAYER && draw.type == MODEL_PLAYER && o->SubType == MODEL_HALLOWEEN)
    {
        RenderPartObject(draw, o->SubType, NULL, characterDraw.light, draw.alpha, 0, 0, 0, false,
                         false, Translate, Select);
    }
    else if (o->Kind == KIND_PLAYER && draw.type == MODEL_PLAYER &&
             o->SubType == MODEL_CURSEDTEMPLE_ALLIED_PLAYER)
    {
        RenderPartObject(draw, o->SubType, NULL, characterDraw.light, draw.alpha, 0, 0, 0, false,
                         false, Translate, Select);
    }
    else if (o->Kind == KIND_PLAYER && draw.type == MODEL_PLAYER &&
             o->SubType == MODEL_CURSEDTEMPLE_ILLUSION_PLAYER)
    {
        RenderPartObject(draw, o->SubType, NULL, characterDraw.light, draw.alpha, 0, 0, 0, false,
                         false, Translate, Select);
    }
    else if (o->Kind == KIND_PLAYER && draw.type == MODEL_PLAYER && o->SubType == MODEL_PANDA)
    {
        RenderPartObject(draw, o->SubType, NULL, characterDraw.light, draw.alpha, 0, 0, 0, false,
                         false, Translate, Select);
    }
    else if (o->Kind == KIND_PLAYER && draw.type == MODEL_PLAYER &&
             o->SubType == MODEL_SKELETON_CHANGED)
    {
        RenderPartObject(draw, o->SubType, NULL, characterDraw.light, draw.alpha, 0, 0, 0, false,
                         false, Translate, Select);
    }
    else if (o->Kind == KIND_PLAYER && draw.type == MODEL_PLAYER &&
             o->SubType == MODEL_XMAS_EVENT_CHANGE_GIRL)
    {
        RenderPartObject(draw, o->SubType, NULL, characterDraw.light, draw.alpha, 0, 0, 0, false,
                         false, Translate, Select);

        RenderParts(c);
    }
    else if (o->Kind == KIND_PLAYER && draw.type == MODEL_PLAYER &&
             o->SubType == MODEL_GM_CHARACTER)
    {
        RenderPartObject(draw, o->SubType, NULL, characterDraw.light, draw.alpha, 0, 0, 0, false,
                         false, Translate, Select);

        if (!g_isCharacterBuff(o, eBuff_Cloaking))
        {
            vec3_t vLight, vPos;
            Vector(0.4f, 0.6f, 0.8f, vLight);
            VectorCopy(draw.position, vPos);
            vPos[2] += 100.f;
            CreateSprite(BITMAP_LIGHT, vPos, 6.0f, vLight, o, 0.5f);

            float fLumi;
            fLumi = sinf(WorldTime * 0.05f) * 0.4f + 0.9f;
            Vector(fLumi * 0.3f, fLumi * 0.5f, fLumi * 0.8f, vLight);
            CreateSprite(BITMAP_LIGHT, vPos, 2.0f, vLight, o);

            Vector(0.3f, 0.2f, 1.f, vLight);
            RenderAurora(BITMAP_MAGIC + 1, RENDER_BRIGHT, draw.position[0], draw.position[1], 2.5f,
                         2.5f, vLight);

            Vector(1.0f, 1.0f, 1.f, vLight);
            fLumi = sinf(WorldTime * 0.0015f) * 0.3f + 0.5f;
            EnableAlphaBlend();
            Vector(fLumi * vLight[0], fLumi * vLight[1], fLumi * vLight[2], vLight);
            RenderTerrainAlphaBitmap(BITMAP_GM_AURORA, draw.position[0], draw.position[1], 1.5f,
                                     1.5f, vLight, WorldTime * 0.01f);
            RenderTerrainAlphaBitmap(BITMAP_GM_AURORA, draw.position[0], draw.position[1], 1.f, 1.f,
                                     vLight, -WorldTime * 0.01f);
            //RenderAurora ( BITMAP_GM_AURORA, RENDER_BRIGHT, draw.position[0], draw.position[1], 1.5f, 1.5f, vLight );

            RenderCharacterCloth(*c);

            DisableAlphaBlend();
        }
    }
    else if (draw.type == MODEL_PLAYER && (o->SubType == MODEL_XMAS_EVENT_CHA_SSANTA ||
                                           o->SubType == MODEL_XMAS_EVENT_CHA_SNOWMAN ||
                                           o->SubType == MODEL_XMAS_EVENT_CHA_DEER))
    {
        constexpr float offsets[][2] = {{50.f, 50.f}, {-50.f, -50.f}, {50.f, -50.f}};
        for (const auto &offset : offsets)
        {
            auto companion = draw;
            companion.position[0] += offset[0];
            companion.position[1] += offset[1];
            companion.position[2] =
                RequestTerrainHeight(companion.position[0], companion.position[1]);
            RenderPartObject(companion, o->SubType, nullptr, characterDraw.light, companion.alpha,
                             0, 0, 0, false, false, Translate, Select);
        }
    }
    else if (draw.type == MODEL_PLAYER &&
             (o->SubType >= MODEL_SKELETON1 && o->SubType <= MODEL_SKELETON3))
    {
        RenderPartObject(draw, o->SubType, NULL, characterDraw.light, draw.alpha, 0, 0, 0, false,
                         false, Translate, Select);
    }
    else if (!c->Change)
    {
#ifndef GUILD_WAR_EVENT
        if ((o->Kind == KIND_PLAYER && gMapManager.InChaosCastle() == true))
        {
            int RenderType = RENDER_TEXTURE;
            PART_t transformedPart = c->BodyPart[BODYPART_HEAD];
            transformedPart.Type = MODEL_ANGEL;
            PART_t *p = &transformedPart;
            if (c == Hero)
            {
                RenderType |= RENDER_CHROME;
            }

            if ((draw.action == PLAYER_SKILL_DRAGONKICK) &&
                (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER))
            {
                p->Type = MODEL_ANGEL;
                RenderCharacter_AfterImage(characterDraw, p, Translate, Select, 2.5f, 1.0f);
            }
            else if ((gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER))
            {
                const OBJECT *pObj = &c->Object;
                if (pObj->m_sTargetIndex < 0 || c->JumpTime > 0)
                {
                    RenderPartObject(draw, MODEL_ANGEL, p, characterDraw.light, draw.alpha, 0, 0, 0,
                                     false, false, Translate, Select, RenderType);
                }
                else
                {
                    p->Type = MODEL_ANGEL;
                    RenderCharacterDarkside(characterDraw, p, Translate, Select);
                }
            }
            else
            {
                RenderPartObject(draw, MODEL_ANGEL, p, characterDraw.light, draw.alpha, 0, 0, 0,
                                 false, false, Translate, Select, RenderType);
            }
        }
        else
#endif // GUILD_WAR_EVENT
        {
            for (int i = MAX_BODYPART - 1; i >= 0; i--)
            {
                const PART_t *p = &c->BodyPart[i];
                if (p->Type != -1)
                {
                    int Type = p->Type;

                    if (CLASS_SUMMONER == gCharacterManager.GetBaseClass(c->Class))
                    {
                        int nItemType = (Type - MODEL_ITEM) / MAX_ITEM_INDEX;
                        int nItemSubType = (Type - MODEL_ITEM) % MAX_ITEM_INDEX;

                        if (nItemType >= 7 && nItemType <= 11 &&
                            (nItemSubType == 10 || nItemSubType == 11))
                        {
                            Type = MODEL_HELM2 + (nItemType - 7) * MODEL_ITEM_COMMON_NUM +
                                   nItemSubType - 10;
                        }
                    }
                    else if (CLASS_RAGEFIGHTER == gCharacterManager.GetBaseClass(c->Class))
                    {
                        Type = g_CMonkSystem.ModifyTypeCommonItemMonk(Type);
                    }

                    BMD *b = &Models[Type];

                    if (CLASS_RAGEFIGHTER == gCharacterManager.GetBaseClass(c->Class))
                    {
                        b->Skin = gCharacterManager.GetBaseClass(c->Class) * 2 +
                                  gCharacterManager.IsThirdClass(c->Class);
                    }
                    else
                    {
                        b->Skin = gCharacterManager.GetBaseClass(c->Class) * 2 +
                                  gCharacterManager.IsSecondClass(c->Class);
                    }

                    if (gCharacterManager.GetBaseClass(c->Class) == CLASS_DARK_LORD &&
                        i == BODYPART_HELM)
                    {
                        draw.blendLight = sinf(WorldTime * 0.001f) * 0.1f + 0.7f;
                        if (i == BODYPART_HELM)
                        {
                            int index = Type - MODEL_HELM;
                            if (index == 0 || index == 5 || index == 6 || index == 8 || index == 9)
                            {
                                Type = MODEL_MASK_HELM + index;
                                Models[Type].Skin = b->Skin;
                            }
                        }
                    }

                    if (c->MonsterIndex >= MONSTER_TERRIBLE_BUTCHER &&
                        c->MonsterIndex <= MONSTER_DOPPELGANGER_SUM)
                    {
                        if (TheMapProcess().CharacterPolicy().cloneAppearances)
                            RenderPartObject(draw, Type, p, characterDraw.light, draw.alpha,
                                             p->Level, p->ExcellentFlags, p->AncientDiscriminator,
                                             false, false, Translate, Select,
                                             RENDER_DOPPELGANGER | RENDER_TEXTURE);
                        else
                            RenderPartObject(draw, Type, p, characterDraw.light, draw.alpha,
                                             p->Level, p->ExcellentFlags, p->AncientDiscriminator,
                                             false, false, Translate, Select,
                                             RENDER_DOPPELGANGER | RENDER_BRIGHT | RENDER_TEXTURE);
                        // 						RenderPartObject(draw,Type,p,characterDraw.light,draw.alpha,p->Level<<3,p->Option1,p->ExtOption,false,false,Translate,
                        // 							Select,RENDER_DOPPELGANGER|RENDER_BRIGHT|RENDER_CHROME);
                    }
                    else
                    {
                        if ((draw.action == PLAYER_SKILL_DRAGONKICK) &&
                            (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER))
                        {
                            RenderCharacter_AfterImage(characterDraw, p, Translate, Select, 2.5f,
                                                       1.0f);
                        }
                        else if ((draw.action == PLAYER_SKILL_DARKSIDE_READY) &&
                                 (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER))
                        {
                            if (o->m_sTargetIndex < 0 || c->JumpTime > 0)
                            {
                                RenderPartObject(draw, Type, p, characterDraw.light, draw.alpha,
                                                 p->Level, p->ExcellentFlags,
                                                 p->AncientDiscriminator, false, false, Translate,
                                                 Select);
                            }
                            else
                            {
                                RenderCharacterDarkside(characterDraw, p, Translate, Select);
                            }
                        }
                        else
                        {
                            RenderPartObject(draw, Type, p, characterDraw.light, draw.alpha,
                                             p->Level, p->ExcellentFlags, p->AncientDiscriminator,
                                             false, false, Translate, Select);
                        }
                    }
                }
            }
        }

        if (gMapManager.InChaosCastle() == false)
        {
            if (c->GuildMarkIndex >= 0 && draw.type == MODEL_PLAYER && draw.alpha != 0.0f &&
                (!g_isCharacterBuff(o, eBuff_Cloaking)) && CreateGuildMark(c->GuildMarkIndex))
            {
                if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER)
                {
                    vec3_t vPos;
                    if (c->BodyPart[BODYPART_ARMOR].Type == MODEL_SACRED_ARMOR ||
                        c->BodyPart[BODYPART_ARMOR].Type == MODEL_STORM_HARD_ARMOR ||
                        c->BodyPart[BODYPART_ARMOR].Type == MODEL_PIERCING_ARMOR ||
                        c->BodyPart[BODYPART_ARMOR].Type == MODEL_PHOENIX_SOUL_ARMOR)
                    {
                        Vector(5.0f, 0.0f, -35.0f, vPos);
                    }
                    else
                    {
                        Vector(5.0f, 0.0f, -21.0f, vPos);
                    }
                    RenderGuild(draw, c->BodyPart[BODYPART_ARMOR].Type, vPos);
                }
                else
                {
                    RenderGuild(draw, c->BodyPart[BODYPART_ARMOR].Type);
                }
            }
        }
    }

    RenderCharacterCape(*c);

    if (c->PK >= PVP_MURDERER2)
    {
        Vector(1.f, 0.1f, 0.1f, characterDraw.light);
    }
    else
    {
        VectorAdd(Light, draw.light, characterDraw.light);

        int nCastle = BLOODCASTLE_NUM + (gMapManager.ContextMap() - WD_11BLOODCASTLE_END);
        if (nCastle > 0 && nCastle <= BLOODCASTLE_NUM) //. 블러드 캐슬일경우
        {
            if ((c->MonsterIndex >= MONSTER_DARK_SKULL_SOLDIER_1 &&
                 c->MonsterIndex <= MONSTER_MAGIC_SKELETON_1) ||
                (c->MonsterIndex >= MONSTER_DARK_SKULL_SOLDIER_2 &&
                 c->MonsterIndex <= MONSTER_MAGIC_SKELETON_2) ||
                (c->MonsterIndex == MONSTER_GIANT_OGRE_3 &&
                 c->MonsterIndex == MONSTER_RED_SKELETON_KNIGHT_3 &&
                 c->MonsterIndex == MONSTER_MAGIC_SKELETON_3) ||
                (c->MonsterIndex >= MONSTER_DARK_SKULL_SOLDIER_4 &&
                 c->MonsterIndex <= MONSTER_MAGIC_SKELETON_4) ||
                (c->MonsterIndex >= MONSTER_DARK_SKULL_SOLDIER_5 &&
                 c->MonsterIndex <= MONSTER_MAGIC_SKELETON_5) ||
                (c->MonsterIndex >= MONSTER_DARK_SKULL_SOLDIER_6 &&
                 c->MonsterIndex <= MONSTER_MAGIC_SKELETON_6) ||
                (c->MonsterIndex >= MONSTER_CHIEF_SKELETON_WARRIOR_7 &&
                 c->MonsterIndex <= MONSTER_MAGIC_SKELETON_7))
            {
                int level = nCastle / 3;
                if (level)
                    Vector(level * 0.5f, 0.1f, 0.1f, characterDraw.light);
            }
        }
    }

    if (!gMapManager.InChaosCastle() && !(gMapManager.IsCursedTemple() && !c->SafeZone))
    {
        NextGradeObjectRender(characterDraw);
    }

    for (int i = 0; i < 2; i++)
    {
        if (g_CMonkSystem.EqualItemModelType(c->Weapon[i].Type) == MODEL_PHOENIX_SOUL_STAR)
        {
            g_CMonkSystem.RenderPhoenixGloves(characterDraw, i);
        }
    }

    bool Bind = false;
    Bind = RenderCharacterBackItem(characterDraw, Translate);

    if (!Bind)
    {
        for (int i = 0; i < 2; i++)
        {
            if (i == 0)
            {
                if (draw.action == PLAYER_ATTACK_SKILL_FURY_STRIKE && draw.animationFrame <= 4.f)
                {
                    continue;
                }

                if (true == c->PostMoveProcess_IsProcessing())
                {
                    continue;
                }
            }

            const PART_t *w = &c->Weapon[i];
            if (w->Type != -1 && w->Type != MODEL_BOLT && w->Type != MODEL_ARROWS &&
                w->Type != MODEL_DARK_RAVEN_ITEM)
            {
                if (g_CMonkSystem.IsSwordformGloves(w->Type))
                    g_CMonkSystem.RenderSwordformGloves(characterDraw, w->Type, i, draw.alpha,
                                                        Translate, Select);
                else
                    RenderLinkObject(0.f, 0.f, 0.f, characterDraw, w, w->Type, w->Level,
                                     w->ExcellentFlags, false, Translate, 0, true, i);
            }
        }
    }

    switch (draw.type)
    {
    case MODEL_PLAYER:
        Vector(0.f, 0.f, 0.f, p);

        if (c->Class == CLASS_SOULMASTER)
        {
            if (!g_isCharacterBuff(o, eBuff_Cloaking))
            {
                Vector(-4.f, 11.f, 0.f, p);
                Vector(1.f, 1.f, 1.f, Light);
                b->TransformPosition(draw.bones[19], p, Position, true);
                //CreateSprite(BITMAP_SPARK+1,Position,0.6f,Light,NULL); //Effect Point Armor SM

                float scale = sinf(WorldTime * 0.001f) * 0.4f;
                //CreateSprite(BITMAP_SHINY+1,Position,scale,Light,NULL); //Effect Point Armor SM
            }
        }
        break;
    case MODEL_BULL_FIGHTER:
    case MODEL_DEATH_COW:
        if ((draw.type == MODEL_BULL_FIGHTER && c->Level == 1) || (draw.type == MODEL_DEATH_COW))
            RenderEye(draw, 22, 23);
        break;
    case MODEL_CRUST:
        RenderEye(draw, 26, 27, 2.0f);
        break;
    case MODEL_HYDRA:
        RenderLight(draw, BITMAP_LIGHTNING + 1, 1.f, 63, 0.f, 0.f, 20.f);
        RenderLight(draw, BITMAP_SHINY + 2, 4.f, 63, 0.f, 0.f, 20.f);
        break;
    case MODEL_VEPAR:
        RenderLight(draw, BITMAP_LIGHTNING + 1, 0.5f, 30, 0.f, 0.f, -5.f);
        RenderLight(draw, BITMAP_LIGHTNING + 1, 0.5f, 39, 0.f, 0.f, -5.f);
        RenderLight(draw, BITMAP_SPARK, 4.f, 30, 0.f, 0.f, -5.f);
        RenderLight(draw, BITMAP_SPARK, 4.f, 39, 0.f, 0.f, -5.f);
        RenderLight(draw, BITMAP_SHINY + 2, 2.f, 30, 0.f, 0.f, -5.f);
        RenderLight(draw, BITMAP_SHINY + 2, 2.f, 39, 0.f, 0.f, -5.f);
        break;
    case MODEL_LIZARD:
        RenderEye(draw, 42, 43);
        RenderLight(draw, BITMAP_SPARK, 2.f, 26, 0.f, 0.f, 0.f);
        RenderLight(draw, BITMAP_SPARK, 2.f, 31, 0.f, 0.f, 0.f);
        RenderLight(draw, BITMAP_SPARK, 2.f, 36, 0.f, 0.f, 0.f);
        RenderLight(draw, BITMAP_SPARK, 2.f, 41, 0.f, 0.f, 0.f);
        RenderLight(draw, BITMAP_SHINY + 2, 1.f, 26, 0.f, 0.f, 0.f);
        RenderLight(draw, BITMAP_SHINY + 2, 1.f, 31, 0.f, 0.f, 0.f);
        RenderLight(draw, BITMAP_SHINY + 2, 1.f, 36, 0.f, 0.f, 0.f);
        RenderLight(draw, BITMAP_SHINY + 2, 1.f, 41, 0.f, 0.f, 0.f);
        break;
    case MODEL_BAHAMUT:
        RenderLight(draw, BITMAP_SPARK, 4.f, 9, 0.f, 0.f, 5.f);
        RenderLight(draw, BITMAP_SHINY + 2, 3.f, 9, 0.f, 0.f, 5.f);
        break;
    case MODEL_MIX_NPC:
        RenderLight(draw, BITMAP_LIGHT, 1.5f, 32, 0.f, 0.f, 0.f);
        break;
    case MODEL_NPC_SEVINA:
        RenderLight(draw, BITMAP_LIGHT, 2.5f, 6, 0.f, 0.f, 0.f);
        break;
    case MODEL_NPC_DEVILSQUARE:
    case MODEL_NPC_CASTEL_GATE:
    case MODEL_SHADOW:
        break;
    case MODEL_SEED_MASTER: {
        float fLumi, fScale;
        fScale = 2.0f;
        b->TransformByObjectBone(Position, draw, 39);

        fLumi = (sinf(WorldTime * 0.001f) + 1.0f) * 0.3f + 0.4f;
        Vector(fLumi * 0.5f, fLumi * 0.5f, fLumi * 0.5f, Light);
        CreateSprite(BITMAP_FLARE, Position, fScale, Light, o);
    }
    break;
    case MODEL_SEED_INVESTIGATOR: {
        float fLumi, fScale;
        fScale = 1.0f;
        for (int i = 69; i <= 70; ++i)
        {
            b->TransformByObjectBone(Position, draw, i);

            fLumi = (sinf(WorldTime * 0.001f) + 1.0f) * 0.3f + 0.4f;
            Vector(fLumi * 0.5f, fLumi * 0.5f, fLumi * 0.5f, Light);
            CreateSprite(BITMAP_FLARE_BLUE, Position, fScale, Light, o);
        }
    }
    break;
    default: {
        TheMapProcess().RenderMonsterVisual(c, draw, b);
    }
    }

    if (localVisual)
        AppendCharacterSprites(*localVisual);
    else
        AppendMapCharacterSprites(*c);

    if (localVisual && drawLocalAttachments)
        RenderCharacterAttachments(*c, *localVisual, true);
}

void SessionRenderUnit::PrepareCharacterReaders()
{
    preparedCharacters_.clear();
    for (int slot = 0; slot < CharactersClient.Size(); ++slot)
    {
        if (!CharactersClient.IsValidIndex(slot) || !CharactersClient.IsVisible(slot))
            continue;
        const auto &character = CharactersClient[slot];
        if (!character.Object.Live || !CharacterVisibleToObserver(character))
            continue;
        preparedCharacters_.push_back({&character, &CharactersClient.WorldVisuals(slot),
                                       slot == SelectedCharacter || slot == SelectedNpc ? 1 : 0});
    }
}

void SessionRenderUnit::RenderCharactersClient()
{
    for (const auto &reader : preparedCharacters_)
    {
        RenderCharacter(reader.character, &reader.character->Object, reader.selection,
                        reader.visual, false);
#ifdef CSK_DEBUG_RENDER_BOUNDINGBOX
        if (g_bRenderBoundingBox)
            RenderBoundingBox(&reader.character->Object);
#endif
    }
}
bool SessionRenderUnit::RenderCharacterBackItem(const CharacterDrawInput &characterDraw,
                                                bool bTranslate)
{
    const auto *c = characterDraw.source;
    const auto *o = characterDraw.object.source;
    const auto &draw = characterDraw.object;
    const bool bBindBack = sessionKeeper_.Visual()->CharacterWeaponsOnBack(*c);

    if (draw.type == MODEL_PLAYER)
    {
        bool bBack = false;
        int iBackupType = -1;

        for (int i = 0; i < 2; ++i)
        {
            int iType = c->Weapon[i].Type;
            int iLevel = c->Weapon[i].Level;
            int iOption1 = c->Weapon[i].ExcellentFlags;

            if (iType < 0)
                continue;

            if (o->Kind == KIND_NPC && TheMapProcess().CharacterPolicy().festiveUniform &&
                draw.type == MODEL_PLAYER &&
                (o->SubType >= MODEL_SKELETON1 && o->SubType <= MODEL_SKELETON3))
            {
                if (i == 0)
                {
                    bBack = true;
                    iType = MODEL_GOLDEN_CROSSBOW;
                    iLevel = 8;
                }
            }

            if (IsBackItem(c, iType) == true)
            {
                bBack = true;
            }

            if (iType == MODEL_BOLT || iType == MODEL_ARROWS)
            {
                bBack = true;
            }
            else
            {
                if (bBindBack == false)
                    bBack = false;
            }

            // 			if(iBackupType == iType)
            // 			{
            // 				bBack = false;
            // 			}

            if (bBack && iType != -1)
            {
                PART_t drawPart = c->Wing;
                PART_t *w = &drawPart;

                float fAnimationFrameBackUp = w->AnimationFrame;

                w->LinkBone = 47;

                if (draw.action == PLAYER_FLY || draw.action == PLAYER_FLY_CROSSBOW)
                {
                    w->PlaySpeed = 1.f;
                }
                else
                {
                    w->PlaySpeed = 0.25f;
                }

                PART_t t_Part = *w;

                if (iType >= MODEL_SWORD && iType < MODEL_SHIELD + MAX_ITEM_INDEX)
                {
                    ::memcpy(&t_Part, w, sizeof(PART_t));
                    t_Part.CurrentAction = 0;
                    t_Part.AnimationFrame = 0.f;
                    t_Part.PlaySpeed = 0.f;
                    t_Part.PriorAction = 0;
                    t_Part.PriorAnimationFrame = 0.f;
                }

                if (iType == MODEL_STINGER_BOW)
                {
                    PART_t drawWeapon = c->Weapon[1];
                    PART_t *pWeapon = &drawWeapon;
                    BYTE byTempLinkBone = pWeapon->LinkBone;
                    pWeapon->CurrentAction = 2;
                    pWeapon->PlaySpeed = 0.25f;
                    pWeapon->LinkBone = 47;
                    RenderLinkObject(0.f, 0.f, 15.f, characterDraw, pWeapon, iType, iLevel,
                                     iOption1, true, bTranslate, 0, true, i);
                    pWeapon->LinkBone = byTempLinkBone;
                }
                else if (g_CMonkSystem.IsSwordformGloves(iType))
                    g_CMonkSystem.RenderSwordformGloves(characterDraw, iType, i, draw.alpha,
                                                        bTranslate);
                else
                {
                    bool bRightHandItem = false;
                    if (i == 0)
                        bRightHandItem = true;
                    RenderLinkObject(0.f, 0.f, 15.f, characterDraw, &t_Part, iType, iLevel,
                                     iOption1, true, bTranslate, 0, bRightHandItem, i);
                }

                w->AnimationFrame = fAnimationFrameBackUp;
            }

            iBackupType = iType;
        }

        // Blood Castle Archangel Quest Item Check: Bounded to 1..3 to prevent default iType=0
        // from rendering Models[0] (Blood Castle stone wall map object) on character back.
        if (gMapManager.InBloodCastle() && (c->EtcPart >= 1 && c->EtcPart <= 3))
        {
            PART_t drawPart = c->Wing;
            PART_t *w = &drawPart;

            int iType = 0;
            int iLevel = 0;
            int iOption1 = 0;

            w->LinkBone = 47;
            if (draw.action == PLAYER_FLY || draw.action == PLAYER_FLY_CROSSBOW)
                w->PlaySpeed = 1.f;
            else
                w->PlaySpeed = 0.25f;

            switch (c->EtcPart)
            {
            case 1:
                iType = MODEL_DIVINE_STAFF_OF_ARCHANGEL;
                break;
            case 2:
                iType = MODEL_DIVINE_SWORD_OF_ARCHANGEL;
                break;
            case 3:
                iType = MODEL_DIVINE_CB_OF_ARCHANGEL;
                break;
            }

            if (iType != 0)
            {
                RenderLinkObject(0.f, 0.f, 15.f, characterDraw, w, iType, iLevel, iOption1, true,
                                 bTranslate, 0, false, CharacterLinkedItemVisual::Quest);
            }
        }

        RenderParts(c);

        if (gMapManager.InChaosCastle() == false)
        {
            PART_t drawPart = c->Wing;
            PART_t *w = &drawPart;
            if (w->Type != -1)
            {
                w->LinkBone = 47;
                if (draw.action == PLAYER_FLY || draw.action == PLAYER_FLY_CROSSBOW)
                {
                    if (w->Type == MODEL_WING_OF_STORM)
                    {
                        w->PlaySpeed = 0.5f;
                    }
                    else
                    {
                        w->PlaySpeed = 1.f;
                    }
                }
                else
                {
                    w->PlaySpeed = 0.25f;
                }

                switch (w->Type)
                {
                case MODEL_CAPE_OF_EMPEROR:
                case MODEL_CAPE_OF_OVERRULE:
                    w->LinkBone = 19;
                    RenderLinkObject(0.f, 0.f, 15.f, characterDraw, w, w->Type, w->Level,
                                     w->ExcellentFlags, true, bTranslate, 0, true,
                                     CharacterLinkedItemVisual::Wing);
                    break;
                default:
                    RenderLinkObject(0.f, 0.f, 15.f, characterDraw, w, w->Type, w->Level,
                                     w->ExcellentFlags, false, bTranslate, 0, true,
                                     CharacterLinkedItemVisual::Wing);
                    break;
                }
            }

            // 사탄
            int iType = c->Helper.Type;
            int iLevel = c->Helper.Level;
            int iOption1 = 0;
            if (iType == MODEL_IMP)
            {
                PART_t drawPart = c->Helper;
                PART_t *w = &drawPart;
                w->LinkBone = 34;
                w->PlaySpeed = 0.5f;
                iOption1 = w->ExcellentFlags;
                if (gCharacterManager.GetBaseClass(c->Class) == CLASS_RAGEFIGHTER &&
                    (c->BodyPart[BODYPART_ARMOR].Type == MODEL_SACRED_ARMOR ||
                     c->BodyPart[BODYPART_ARMOR].Type == MODEL_STORM_HARD_ARMOR ||
                     c->BodyPart[BODYPART_ARMOR].Type == MODEL_PIERCING_ARMOR ||
                     c->BodyPart[BODYPART_ARMOR].Type == MODEL_PHOENIX_SOUL_ARMOR))
                {
                    RenderLinkObject(20.f, -5.f, 20.f, characterDraw, w, iType, iLevel, iOption1,
                                     false, bTranslate, 0, true, CharacterLinkedItemVisual::Helper);
                }
                else
                {
                    RenderLinkObject(20.f, 0.f, 0.f, characterDraw, w, iType, iLevel, iOption1,
                                     false, bTranslate, 0, true, CharacterLinkedItemVisual::Helper);
                }
            }
        }
    }

    return bBindBack;
}

// Construction/Destruction

void CMonkSystem::RenderPhoenixGloves(const CharacterDrawInput &character, BYTE _Hand)
{
    const auto *_pCha = character.source;
    PART_t drawPart = _pCha->Weapon[_Hand];
    PART_t *w = &drawPart;
    w->LinkBone = _Hand ? 37 : 28;
    RenderLinkObject(_Hand ? 100.f : 80.f, 10.0, -75.0, character, w, MODEL_SWORD_35_WING, 0, 0,
                     true, true, 0, 1, CharacterLinkedItemVisual::RightPhoenix + _Hand);
}

void CMonkSystem::RenderSwordformGloves(const CharacterDrawInput &character, int _ModelType,
                                        int _Hand, float _Alpha, bool _Translate, int _Select)
{
    const auto *_pCha = character.source;
    const PART_t *w = &_pCha->Weapon[_Hand];
    RenderPartObject(character.object, ModifyTypeSwordformGloves(_ModelType, _Hand), w,
                     character.light, _Alpha, w->Level, w->ExcellentFlags, w->AncientDiscriminator,
                     false, false, _Translate, _Select);
}
void CMonkSystem::RenderRepeatedly(int _Key, OBJECT *pObj)
{
    vec3_t Position, Light, Light2;
    VectorCopy(pObj->Position, Position);
    WORD Damage;
    for (int _index = 0; _index < m_nRepeatedlyCnt; _index++)
    {
        Damage = m_arrRepeatedly[_index].m_Damage;
        float scale = 15.0f;

        switch (m_arrRepeatedly[_index].m_DamageType)
        {
            //데미지타입에 따른컬러
        case 0:
            if (_Key == HeroKey)
            {
                Vector(1.f, 0.f, 0.f, Light);
            }
            else
            {
                Vector(1.f, 0.6f, 0.f, Light);
            }
            break;
        case 1:
            scale = 50.f;
            Vector(0.0f, 1.f, 1.f, Light);
            break;
        case 2:
            scale = 50.f;
            Vector(0.f, 1.f, 0.6f, Light);
            break;
        case 3:
            scale = 50.f;
            Vector(0.f, 0.6f, 1.f, Light);
            break;
        case 4:
            Vector(1.f, 0.f, 1.f, Light);
            break;
        case 5:
            Vector(0.f, 1.f, 0.f, Light);
            break;
        case 6:
            Vector(0.7f, 0.4f, 1.0f, Light);
            break;
        default:
            Vector(1.f, 1.f, 1.f, Light);
            break;
        }

        if (!Damage)
        {
            if (_Key == HeroKey)
            {
                Vector(1.f, 1.f, 1.f, Light);
            }
            else
            {
                Vector(0.5f, 0.5f, 0.5f, Light);
            }
            CreatePoint(Position, -1, Light, 15.0f, true, true);
        }
        else
        {
            CreatePoint(Position, Damage, Light, scale, true, true);
        }

        if (m_arrRepeatedly[_index].m_Double)
        {
            // 더블데미지
            Position[2] += 10.f;
            Vector(Light[0] - 0.2f, Light[1] - 0.2f, Light[2] - 0.2f, Light2);
            CreatePoint(Position, Damage, Light2, scale + 5.f, true);
        }

        Position[1] += (_index % 2 == 0) ? -40.0f : 40.0f;
        Position[2] -= (rand() % 10 + 15.0f);
    }
    m_nRepeatedlyCnt = 0;
}

//  CSParts.cpp

const WorldCharacterVisualState *SessionRenderUnit::FindCharacterVisual(const CHARACTER &character)
{
    if (drawingCharacter_ == &character)
        return drawingCharacterVisual_;
    return sessionKeeper_.Visual()->FindCharacterVisual(character);
}

void SessionRenderUnit::RenderParts(const CHARACTER *character)
{
    if (character == nullptr || g_isCharacterBuff(&character->Object, eBuff_Cloaking))
        return;
    const auto *visual = FindCharacterVisual(*character);
    if (visual == nullptr)
        return;
    if (visual->partsType >= PARTS_ATTACK_TEAM_MARK &&
        visual->partsType <= PARTS_DEFENSE_KING_TEAM_MARK && !IsBattleCastleStart())
        return;
    struct RecordScope final
    {
        bool &active;
        bool previous;
        ~RecordScope()
        {
            active = previous;
        }
    } scope{recordingParts_, recordingParts_};
    recordingParts_ = true;
    if (visual->temporaryParts)
        visual->temporaryParts->IRender(character);
    if (visual->parts)
        visual->parts->IRender(character);
}

void CSParts::IRender(const CHARACTER *c) const
{
    if (c == nullptr || !pose_ || m_pObj.Alpha < CharacterPartsDetail::kRenderableAlphaThreshold)
        return;
    Vector(1.f, 1.f, 1.f, Models[m_pObj.Type].BodyLight);
    ObjectDrawInput draw(&m_pObj);
    draw.preparedPose = &poseSample_;
    draw.stableBones = true;
    RenderObject(draw, true);
}

void CSAnimationParts::IRender(const CHARACTER *c) const
{
    if (c == nullptr || !pose_ || m_pObj.Alpha < CharacterPartsDetail::kRenderableAlphaThreshold)
        return;
    ObjectDrawInput draw(&m_pObj);
    draw.preparedPose = &poseSample_;
    draw.stableBones = true;
    RenderObject(draw, true);
}

void CSParts2D::IRender(const CHARACTER *) const
{
    CreateSprite(m_pObj.Type, m_pObj.Position, 1.f, m_pObj.Light, nullptr, 0, preparedSubType_);
}

// ?
#ifdef _PVP_ADD_MOVE_SCROLL
extern CMurdererMove g_MurdererMove;
#endif // _PVP_ADD_MOVE_SCROLL
void SessionRenderUnit::RenderObjectScreen(int Type, int ItemLevel, int excellentFlags,
                                           int ancientDiscriminator, vec3_t Target, int Select,
                                           bool PickUp, float presentationScale,
                                           const UI::Items::ItemSlotTrs *trs)
{
    int Level = ItemLevel;
    vec3_t Direction, Position;

    VectorSubtract(Target, MousePosition, Direction);
    if (PickUp)
        VectorMA(MousePosition, 0.07f, Direction, Position);
    else
        VectorMA(MousePosition, 0.1f, Direction, Position);

    vec3_t presentationAnchor;
    VectorCopy(Position, presentationAnchor);

    if (Type == MODEL_KRIS)
    {
        Position[0] -= 0.02f;
        Position[1] += 0.03f;
        Vector(180.f, 270.f, 15.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_BOLT || Type == MODEL_ARROWS)
    {
        Vector(0.f, 270.f, 15.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_LIGHT_SPEAR)
    {
        Position[1] += 0.05f;
        Vector(0.f, 90.f, 20.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_CELESTIAL_BOW)
    {
        Vector(0.f, 90.f, 15.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_SYLPHID_RAY_HELM)
    {
        Position[1] -= 0.06f;
        Position[0] += 0.03f;
        Vector(-90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_VENOM_MIST_HELM)
    {
        Position[1] += 0.07f;
        Position[0] -= 0.03f;
        Vector(-90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_VENOM_MIST_ARMOR)
    {
        Position[1] += 0.1f;
        Vector(-90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_DRAGON_KNIGHT_ARMOR)
    {
        Position[1] += 0.07f;
        Vector(-90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_SYLPH_WIND_BOW)
    {
        Position[1] += 0.12f;
        Vector(180.f, -90.f, 15.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_GRAND_VIPER_STAFF)
    {
        Position[1] -= 0.1f;
        Position[0] += 0.025f;
        Vector(180.f, 0.f, 8.f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_BOOK_OF_SAHAMUTT && Type <= MODEL_STAFF + 29)
    {
        Vector(0.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_SOLEIL_SCEPTER)
    {
        Position[1] += 0.1f;
        Position[0] -= 0.01;
        Vector(180.f, 90.f, 13.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_ASHCROW_ARMOR)
    {
        Position[1] += 0.03f;
        Vector(-90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_ECLIPSE_HELM)
    {
        Position[0] -= 0.02f;
        Position[1] += 0.05f;
        Vector(-90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_ECLIPSE_ARMOR)
    {
        Position[1] += 0.05f;
        Vector(-90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_IRIS_ARMOR)
    {
        Position[1] -= 0.05f;
        Vector(-90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_VALIANT_ARMOR)
    {
        Position[1] -= 0.05f;
        Vector(-90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (MODEL_MISTERY_HELM <= Type && MODEL_LILIUM_HELM >= Type)
    {
        Position[1] -= 0.05f;
        Vector(-90.f, 25.f, 0.f, ObjectSelect.Angle);
    }
    else if (MODEL_GLORIOUS_ARMOR <= Type && MODEL_LILIUM_ARMOR >= Type)
    {
        Position[1] -= 0.08f;
        Vector(-90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_DAYBREAK)
    {
        Position[0] -= 0.02f;
        Position[1] += 0.03f;
        Vector(180.f, 90.f, 15.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_SHINING_SCEPTER)
    {
        Position[1] += 0.05f;
        Vector(180.f, 90.f, 13.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_ALBATROSS_BOW || Type == MODEL_STINGER_BOW)
    {
        Position[0] -= 0.10f;
        Position[1] += 0.08f;
        Vector(180.f, -90.f, 15.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_PLATINA_STAFF)
    {
        Position[0] += 0.02f;
        Position[1] += 0.02f;
        Vector(180.f, 90.f, 8.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_ARROW_VIPER_BOW)
    {
        Vector(180.f, -90.f, 15.f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_CROSSBOW && Type < MODEL_BOW + MAX_ITEM_INDEX)
    {
        Vector(90.f, 180.f, 20.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_DRAGON_SPEAR)
    {
        Vector(180.f, 270.f, 20.f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_SWORD && Type < MODEL_STAFF + MAX_ITEM_INDEX)
    {
        switch (Type)
        {
        case MODEL_MISTERY_STICK:
            Position[1] += 0.04f;
            break;
        case MODEL_ANCIENT_STICK:
            Position[0] += 0.02f;
            Position[1] += 0.03f;
            break;
        case MODEL_DEMONIC_STICK:
            Position[0] += 0.02f;
            break;
        case MODEL_STORM_BLITZ_STICK:
            Position[0] -= 0.02f;
            Position[1] -= 0.02f;
            break;
        case MODEL_ETERNAL_WING_STICK:
            Position[0] += 0.01f;
            Position[1] -= 0.01f;
            break;
        }

        if (!ItemAttribute[Type - MODEL_ITEM].TwoHand)
        {
            Vector(180.f, 270.f, 15.f, ObjectSelect.Angle);
        }
        else
        {
            Vector(180.f, 270.f, 25.f, ObjectSelect.Angle);
        }
    }
    else if (Type >= MODEL_SHIELD && Type < MODEL_SHIELD + MAX_ITEM_INDEX)
    {
        Vector(270.f, 270.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HORN_OF_DINORANT)
    {
        Vector(-90.f, -90.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_DARK_HORSE_ITEM)
    {
        Vector(-90.f, -90.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_DARK_RAVEN_ITEM)
    {
        Vector(-90.f, -35.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_SPIRIT)
    {
        Vector(-90.f, -90.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_CAPE_OF_LORD)
    {
        Vector(-90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_EVENT + 16)
    {
        Vector(-90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_SCROLL_OF_ARCHANGEL || Type == MODEL_BLOOD_BONE)
    {
        Vector(270.f, -10.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_INVISIBILITY_CLOAK)
    {
        Vector(290.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_EVENT + 11)
    {
        Vector(-90.f, -20.f, -20.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_EVENT + 12)
    {
        Vector(250.f, 140.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_EVENT + 14)
    {
        Vector(255.f, 160.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_EVENT + 15)
    {
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_RING_OF_FIRE && Type <= MODEL_RING_OF_MAGIC)
    {
        Vector(270.f, 160.f, 20.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_ARMOR_OF_GUARDSMAN)
    {
        Vector(290.f, 0.f, 0.f, ObjectSelect.Angle);
    }

    else if (Type == MODEL_SPLINTER_OF_ARMOR)
    {
        Position[0] += 0.01f;
        Position[1] -= 0.03f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_BLESS_OF_GUARDIAN)
    {
        Position[1] += 0.02f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_CLAW_OF_BEAST)
    {
        Position[0] += 0.01f;
        Position[1] += 0.02f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_FRAGMENT_OF_HORN)
    {
        Position[0] += 0.01f;
        Position[1] += 0.02f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_BROKEN_HORN)
    {
        Position[0] += 0.01f;
        Position[1] += 0.05f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HORN_OF_FENRIR)
    {
        Position[0] += 0.01f;
        Position[1] += 0.04f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_OLD_SCROLL)
    {
        Position[1] -= 0.04f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_ILLUSION_SORCERER_COVENANT)
    {
        Position[1] -= 0.03f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_SCROLL_OF_BLOOD)
    {
        Position[1] -= 0.02f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 64)
    {
        Position[1] += 0.02f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_FLAME_OF_CONDOR)
    {
        Position[1] += 0.045f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_FEATHER_OF_CONDOR)
    {
        Position[1] += 0.04f;
        Vector(270.f, 120.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_WING_OF_ETERNAL)
    {
        Position[1] += 0.05f;
        Vector(270.f, -10.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_WING_OF_ILLUSION)
    {
        Position[1] += 0.05f;
        Vector(270.f, -10.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_WING_OF_RUIN)
    {
        Position[1] += 0.08f;
        Vector(270.f, -10.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_CAPE_OF_EMPEROR)
    {
        Position[1] += 0.05f;
        Vector(270.f, -10.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_WINGS_OF_DESPAIR)
    {
        Position[1] += 0.05f;
        Vector(270.f, 0.f, 2.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 46)
    {
        Position[1] -= 0.04f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 47)
    {
        Position[1] -= 0.04f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 48)
    {
        Position[1] -= 0.04f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 54)
    {
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 58)
    {
        Position[1] += 0.07f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 59 || Type == MODEL_POTION + 60)
    {
        Position[1] += 0.06f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 61 || Type == MODEL_POTION + 62)
    {
        Position[1] += 0.06f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 53)
    {
        Position[1] += 0.042f;
        Vector(180.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 43)
    {
        Position[1] -= 0.027f;
        Position[0] += 0.005f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 44)
    {
        Position[1] -= 0.03f;
        Position[0] += 0.005f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 45)
    {
        Position[1] -= 0.02f;
        Position[0] += 0.005f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_POTION + 70 && Type <= MODEL_POTION + 71)
    {
        Position[0] += 0.01f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_POTION + 72 && Type <= MODEL_POTION + 77)
    {
        Position[1] += 0.08f;
        Vector(0.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 59)
    {
        Position[0] += 0.01f;
        Position[1] += 0.02f;
        Vector(90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_HELPER + 54 && Type <= MODEL_HELPER + 58)
    {
        Position[1] -= 0.02f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_POTION + 78 && Type <= MODEL_POTION + 82)
    {
        Position[1] += 0.01f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 60)
    {
        Position[1] -= 0.06f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 61)
    {
        Position[1] -= 0.04f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 83)
    {
        Position[1] += 0.06f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 91)
    {
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 92)
    {
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 93)
    {
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 95)
    {
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 94)
    {
        Position[0] += 0.01f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_CHERRY_BLOSSOM_PLAYBOX && Type <= MODEL_GOLDEN_CHERRY_BLOSSOM_BRANCH)
    {
        if (Type == MODEL_CHERRY_BLOSSOM_PLAYBOX)
        {
            Position[1] += 0.01f;
            Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
        }
        else if (Type == MODEL_CHERRY_BLOSSOM_WINE)
        {
            Position[1] -= 0.01f;
            Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
        }
        else if (Type == MODEL_CHERRY_BLOSSOM_RICE_CAKE)
        {
            Position[1] += 0.01f;
            Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
        }
        else if (Type == MODEL_CHERRY_BLOSSOM_FLOWER_PETAL)
        {
            Position[1] += 0.01f;
            Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
        }
        else if (Type == MODEL_POTION + 88)
        {
            Position[1] += 0.015f;
            Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
        }
        else if (Type == MODEL_POTION + 89)
        {
            Position[1] += 0.015f;
            Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
        }
        else if (Type == MODEL_GOLDEN_CHERRY_BLOSSOM_BRANCH)
        {
            Position[1] += 0.015f;
            Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
        }
    }
    else if (Type == MODEL_HELPER + 62)
    {
        Position[0] += 0.01f;
        Position[1] -= 0.03f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 63)
    {
        Position[0] += 0.01f;
        Position[1] += 0.082f;
        Vector(90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_POTION + 97 && Type <= MODEL_POTION + 98)
    {
        Position[1] += 0.09f;
        Vector(0.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 96)
    {
        Position[1] -= 0.013f;
        Position[0] += 0.003f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (MODEL_DEMON <= Type && Type <= MODEL_SPIRIT_OF_GUARDIAN)
    {
        switch (Type)
        {
        case MODEL_DEMON:
            Position[1] -= 0.05f;
            break;
        case MODEL_SPIRIT_OF_GUARDIAN:
            Position[1] -= 0.02f;
            break;
        }
        Vector(270.f, -10.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_FLAME_OF_DEATH_BEAM_KNIGHT)
    {
        Position[1] += 0.05f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HORN_OF_HELL_MAINE)
    {
        Position[1] += 0.11f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_FEATHER_OF_DARK_PHOENIX)
    {
        Position[1] += 0.11f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_ELITE_TRANSFER_SKELETON_RING)
    {
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 43)
    {
        //		Position[1] += 0.082f;
        Position[1] -= 0.03f;
        Vector(90.f, 0.f, 180.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 44)
    {
        Position[1] += 0.08f;
        Vector(90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 45)
    {
        Position[1] += 0.07f;
        Vector(90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_JACK_OLANTERN_TRANSFORMATION_RING)
    {
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_CHRISTMAS_TRANSFORMATION_RING)
    {
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_SCROLL_OF_BLOOD)
    {
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_GAME_MASTER_TRANSFORMATION_RING)
    {
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_MOONSTONE_PENDANT)
    {
        Position[0] += 0.00f;
        Position[1] += 0.02f;
        Vector(-48 - 150.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_GEMSTONE)
    {
        Position[1] += 0.02f;
        Vector(270.f, 90.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_JEWEL_OF_HARMONY)
    {
        Position[1] += 0.02f;
        Vector(270.f, -10.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_LOWER_REFINE_STONE || Type == MODEL_HIGHER_REFINE_STONE)
    {
        Position[0] -= 0.04f;
        Position[1] += 0.02f;
        Position[2] += 0.02f;
        Vector(270.f, -10.f, -45.f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_PENDANT_OF_LIGHTING && Type < MODEL_HELPER + MAX_ITEM_INDEX &&
             Type != MODEL_LOCHS_FEATHER && Type != MODEL_FRUITS)
    {
        Vector(270.f + 90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 12)
    {
        switch (Level)
        {
        case 0:
            Vector(180.f, 0.f, 0.f, ObjectSelect.Angle);
            break;
        case 1:
            Vector(270.f, 90.f, 0.f, ObjectSelect.Angle);
            break;
        case 2:
            Vector(90.f, 0.f, 0.f, ObjectSelect.Angle);
            break;
        }
    }
    else if (Type == MODEL_EVENT + 5)
    {
        Vector(270.f, 180.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_EVENT + 6)
    {
        Vector(270.f, 90.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_EVENT + 7)
    {
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 20)
    {
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 27)
    {
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_FIRECRACKER)
    {
        Position[1] += 0.08f;
        Vector(-50.f, -60.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_GM_GIFT)
    {
        //Position[1] += 0.08f;
        Vector(270.f, -25.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_CHAIN_LIGHTNING_PARCHMENT && Type <= MODEL_INNOVATION_PARCHMENT)
    {
        Position[0] += 0.03f;
        Position[1] += 0.03f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_ORB_OF_TWISTING_SLASH)
    {
        Position[0] += 0.005f;
        Position[1] -= 0.015f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_VINE_ARMOR)
    {
        Position[1] -= 0.1f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_VINE_PANTS)
    {
        Position[1] -= 0.08f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_SILK_ARMOR)
    {
        Position[1] -= 0.1f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_SILK_PANTS)
    {
        Position[1] -= 0.08f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_CRYSTAL_OF_DESTRUCTION)
    {
        Position[0] += 0.005f;
        Position[1] -= 0.015f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_CRYSTAL_OF_RECOVERY || Type == MODEL_CRYSTAL_OF_MULTI_SHOT)
    {
        Position[0] += 0.005f;
        Position[1] -= 0.015f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else
    {
        Vector(270.f, -10.f, 0.f, ObjectSelect.Angle);
    }

    if (Type >= MODEL_SEED_FIRE && Type <= MODEL_SEED_EARTH)
    {
        Vector(10.f, -10.f, 10.f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_SPHERE_MONO && Type <= MODEL_SPHERE_5)
    {
        Vector(0.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_SEED_SPHERE_FIRE_1 && Type <= MODEL_SEED_SPHERE_EARTH_5)
    {
        Vector(0.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_PET_RUDOLF)
    {
        Position[1] -= 0.05f;
        Vector(270.f, 40.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_PET_SKELETON)
    {
        Position[1] -= 0.05f;
        Vector(270.f, 40.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 140)
    {
        Position[1] += 0.09f;
        Vector(0.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_POTION + 145 && Type <= MODEL_POTION + 150)
    {
        Position[0] += 0.010f;
        Position[1] += 0.040f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_HELPER + 125 && Type <= MODEL_HELPER + 127)
    {
        Position[0] += 0.007f;
        Position[1] -= 0.035f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 124)
    {
        Position[1] -= 0.04f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_PET_PANDA)
    {
        Position[1] -= 0.05f;
        Vector(270.f, 40.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_PET_UNICORN)
    {
        Position[0] += 0.01f;
        Position[1] -= 0.05f;
        Vector(255.f, 45.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_SNOWMAN_TRANSFORMATION_RING)
    {
        Position[0] += 0.02f;
        Position[1] -= 0.02f;
        Vector(300.f, 10.f, 20.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_PANDA_TRANSFORMATION_RING)
    {
        //		Position[0] += 0.02f;
        Position[1] -= 0.02f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_SKELETON_TRANSFORMATION_RING)
    {
        Position[0] += 0.01f;
        Position[1] -= 0.035f;
        Vector(290.f, -20.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 128)
    {
        Position[0] += 0.017f;
        Position[1] -= 0.053f;
        Vector(270.f, -20.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 129)
    {
        Position[0] += 0.012f;
        Position[1] -= 0.045f;
        Vector(270.f, -20.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 134)
    {
        Position[0] += 0.005f;
        Position[1] -= 0.033f;
        Vector(270.f, -20.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 130)
    {
        Position[0] += 0.007f;
        Position[1] += 0.005f;
        Vector(270.f, -20.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 131)
    {
        Position[0] += 0.017f;
        Position[1] -= 0.053f;
        Vector(270.f, -20.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 132)
    {
        Position[0] += 0.007f;
        Position[1] += 0.045f;
        Vector(270.f, -20.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 133)
    {
        Position[0] += 0.017f;
        Position[1] -= 0.053f;
        Vector(270.f, -20.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 69)
    {
        Position[0] += 0.005f;
        Position[1] -= 0.05f;
        Vector(270.f, -30.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 70)
    {
        Position[0] += 0.040f;
        Position[1] -= 0.000f;
        Vector(270.f, -0.f, 70.f, ObjectSelect.Angle);
    }

    else if (Type == MODEL_HELPER + 81)
    {
        Position[0] += 0.005f;
        Position[1] += 0.035f;
        Vector(-90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 82)
    {
        Position[0] += 0.005f;
        Position[1] += 0.035f;
        Vector(-90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 93)
    {
        Position[0] += 0.005f;
        Vector(-90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 94)
    {
        Position[0] += 0.005f;
        Vector(-90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 66)
    {
        Position[0] += 0.01f;
        Position[1] -= 0.05f;
        Vector(270.0f, 0.0f, 0.0f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 100)
    {
        Position[0] += 0.01f;
        Position[1] -= 0.05f;
        Vector(0.0f, 0.0f, 0.0f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_CHRISTMAS_FIRECRACKER)
    {
        Position[0] += 0.02f;
        Position[1] -= 0.03f;
        //Vector(270.f,0.f,30.f,ObjectSelect.Angle);
        Vector(290.f, -40.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_CHROMATIC_STAFF)
    {
        Position[0] += 0.02f;
        Position[1] -= 0.06f;
        Vector(180.f, 90.f, 10.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_RAVEN_STICK)
    {
        Position[1] -= 0.05f;
        Vector(180.f, 90.f, 10.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_BEUROBA)
    {
        Position[1] += 0.02f;
        Vector(180.f, 90.f, 15.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_STRYKER_SCEPTER)
    {
        Position[0] -= 0.03f;
        Position[1] += 0.06f;
        Vector(180.f, 90.f, 2.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_AIR_LYN_BOW)
    {
        Position[0] -= 0.07f;
        Position[1] += 0.07f;
        Vector(180.f, -90.f, 15.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_CRYSTAL_OF_FLAME_STRIKE)
    {
        Position[0] += 0.005f;
        Position[1] -= 0.015f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 71 || Type == MODEL_HELPER + 72 || Type == MODEL_HELPER + 73 ||
             Type == MODEL_HELPER + 74 || Type == MODEL_HELPER + 75)
    {
        Position[1] += 0.07f;
        Vector(270.f, 180.f, 0.f, ObjectSelect.Angle);
        if (Select != 1)
        {
            ObjectSelect.Angle[1] = WorldTime * 0.2f;
        }
    }
    else if (Type >= static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_BEGIN &&
             Type <= static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_END)
    {
        Position[1] -= 0.03f;
        Vector(-90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 97 || Type == MODEL_HELPER + 98 || Type == MODEL_POTION + 91)
    {
        Position[1] -= 0.04f;
        Position[0] += 0.002f;
        Vector(270.0f, 0.0f, 0.0f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 99)
    {
        Position[0] += 0.002f;
        Position[1] += 0.025f;
        Vector(270.0f, 180.0f, 45.0f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 110)
    {
        Position[0] += 0.005f;
        Position[1] -= 0.02f;
    }
    else if (Type == MODEL_POTION + 111)
    {
        Position[0] += 0.01f;
        Position[1] -= 0.02f;
    }
    else if (Type == MODEL_HELPER + 107)
    {
        Position[0] -= 0.0f;
        Position[1] += 0.0f;
        Vector(90.0f, 225.0f, 45.0f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 104)
    {
        Position[0] += 0.01f;
        Position[1] -= 0.03f;
        Vector(270.0f, 0.0f, 0.0f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 105)
    {
        Position[0] += 0.01f;
        Position[1] -= 0.03f;
        Vector(270.0f, 0.0f, 0.0f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 103)
    {
        Position[0] += 0.01f;
        Position[1] += 0.01f;
        Vector(0.0f, 0.0f, 0.0f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 133)
    {
        Position[0] += 0.01f;
        Position[1] -= 0.0f;
        Vector(270.0f, 0.0f, 0.0f, ObjectSelect.Angle);
    }
    else if (MODEL_SUSPICIOUS_SCRAP_OF_PAPER <= Type && Type <= MODEL_COMPLETE_SECROMICON)
    {
        switch (Type)
        {
        case MODEL_SUSPICIOUS_SCRAP_OF_PAPER: {
            Position[0] += 0.005f;
            //Position[1] -= 0.02f;
        }
        break;
        case MODEL_GAIONS_ORDER: {
            Position[0] += 0.005f;
            Position[1] += 0.05f;
            Vector(0.0f, 0.0f, 30.0f, ObjectSelect.Angle);
        }
        break;
        case MODEL_FIRST_SECROMICON_FRAGMENT:
        case MODEL_SECOND_SECROMICON_FRAGMENT:
        case MODEL_THIRD_SECROMICON_FRAGMENT:
        case MODEL_FOURTH_SECROMICON_FRAGMENT:
        case MODEL_FIFTH_SECROMICON_FRAGMENT:
        case MODEL_SIXTH_SECROMICON_FRAGMENT: {
            Position[0] += 0.005f;
            Position[1] += 0.05f;
            Vector(0.0f, 0.0f, 30.0f, ObjectSelect.Angle);
        }
        break;
        case MODEL_COMPLETE_SECROMICON: {
            Position[0] += 0.005f;
            Position[1] += 0.05f;
            Vector(0.0f, 0.0f, 0.0f, ObjectSelect.Angle);
        }
        break;
        }
    }
    else if (Type >= MODEL_HELPER + 109 && Type <= MODEL_HELPER + 112)
    {
        Position[0] += 0.025f;
        Position[1] -= 0.035f;
        Vector(270.0f, 25.0f, 25.0f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_HELPER + 113 && Type <= MODEL_HELPER + 115)
    {
        Position[0] += 0.005f;
        Position[1] -= 0.00f;
        Vector(270.0f, 0.0f, 0.0f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_POTION + 112 && Type <= MODEL_POTION + 113)
    {
        Position[0] += 0.05f;
        Position[1] += 0.009f;
        Vector(270.0f, 180.0f, 45.0f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 120)
    {
        Position[0] += 0.01f;
        Position[1] += 0.05f;
        Vector(270.0f, 0.0f, 0.0f, ObjectSelect.Angle);
    }
    else if (MODEL_POTION + 134 <= Type && Type <= MODEL_POTION + 139)
    {
        Position[0] += 0.00f;
        Position[1] += 0.05f;
        Vector(270.0f, 0.0f, 0.0f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 116)
    {
        Position[1] -= 0.03f;
        Position[0] += 0.005f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_POTION + 114 && Type <= MODEL_POTION + 119)
    {
        Position[0] += 0.00f;
        Position[1] += 0.06f;
        Vector(270.0f, 0.0f, 0.0f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_POTION + 126 && Type <= MODEL_POTION + 129)
    {
        Position[0] += 0.00f;
        Position[1] += 0.06f;
        Vector(270.0f, 0.0f, 0.0f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_POTION + 130 && Type <= MODEL_POTION + 132)
    {
        Position[0] += 0.00f;
        Position[1] += 0.06f;
        Vector(270.0f, 0.0f, 0.0f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_HELPER + 121)
    {
        Position[1] -= 0.04f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_SACRED_HELM)
    {
        Position[1] += 0.04f;
        Vector(-90.f, 25.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_ARMORINVEN_60 || Type == MODEL_ARMORINVEN_61 ||
             Type == MODEL_ARMORINVEN_62)
    {
        Position[0] += 0.01f;
        Position[1] += 0.08f;
        Vector(0.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_ARMORINVEN_74)
    {
        Position[0] += 0.01f;
        Position[1] += 0.05f;
        Vector(90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_SACRED_GLOVE)
    {
        Position[0] += 0.005f;
        Position[1] += 0.015f;
        Vector(-90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_STORM_HARD_GLOVE && Type <= MODEL_PIERCING_BLADE_GLOVE)
    {
        Position[0] += 0.002f;
        Position[1] += 0.02f;
        Vector(0.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_PHOENIX_SOUL_STAR)
    {
        Position[0] -= 0.005f;
        Position[1] += 0.015f;
        Vector(0.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_CAPE_OF_FIGHTER)
    {
        Position[1] += 0.01f;
        Position[0] += 0.015f;
        Vector(-90.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_CAPE_OF_OVERRULE)
    {
        Position[1] += 0.15f;
        Vector(270.f, -10.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type >= MODEL_CHAIN_DRIVE_PARCHMENT && Type <= MODEL_INCREASE_BLOCK_PARCHMENT)
    {
        Position[0] += 0.03f;
        Position[1] += 0.03f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_WING + 135)
    {
        Position[1] += 0.05f;
        Position[0] += 0.005f;
    }
    else if (Type >= MODEL_HELPER + 135 && Type <= MODEL_HELPER + 145)
    {
        Position[1] += 0.02f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Type == MODEL_POTION + 160 || Type == MODEL_POTION + 161)
    {
        Position[1] += 0.05f;
        Vector(270.f, 0.f, 0.f, ObjectSelect.Angle);
    }
    else if (Check_Jewel_Com(Type - MODEL_ITEM) != COMGEM::NOGEM)
    {
        Vector(270.f, -10.f, 0.f, ObjectSelect.Angle);
        switch (Check_Jewel_Com(Type - MODEL_ITEM))
        {
        case COMGEM::eLOW_C:
        case COMGEM::eUPPER_C:
            Vector(270.f, -10.f, -45.f, ObjectSelect.Angle);
            break;
        case COMGEM::eLIFE_C:
        case COMGEM::eCREATE_C:
            Position[1] -= 0.05f;
            break;
        case COMGEM::eGEMSTONE_C:
            Position[1] -= 0.05f;
            Vector(270.f, 90.f, 0.f, ObjectSelect.Angle);
            break;
        }
    }

    switch (Type)
    {
    case MODEL_FLAMBERGE: {
        Position[0] -= 0.02f;
        Position[1] += 0.04f;
        Vector(180.f, 270.f, 10.f, ObjectSelect.Angle);
    }
    break;
    case MODEL_SWORD_BREAKER: {
        Vector(180.f, 270.f, 15.f, ObjectSelect.Angle);
    }
    break;
    case MODEL_IMPERIAL_SWORD: {
        Position[1] += 0.02f;
        Vector(180.f, 270.f, 10.f, ObjectSelect.Angle);
    }
    break;
    case MODEL_FROST_MACE: {
        Position[0] -= 0.02f;
        Vector(180.f, 270.f, 15.f, ObjectSelect.Angle);
    }
    break;
    case MODEL_ABSOLUTE_SCEPTER: {
        Position[0] -= 0.02f;
        Position[1] += 0.04f;
        Vector(180.f, 270.f, 15.f, ObjectSelect.Angle);
    }
    break;
    case MODEL_DEADLY_STAFF: {
        Vector(180.f, 90.f, 10.f, ObjectSelect.Angle);
    }
    break;
    case MODEL_IMPERIAL_STAFF: {
        Vector(180.f, 90.f, 10.f, ObjectSelect.Angle);
    }
    break;
    case MODEL_STAFF + 32: {
        Vector(180.f, 90.f, 10.f, ObjectSelect.Angle);
    }
    break;
    }

    switch (Type)
    {
    case MODEL_CHAOS_LIGHTNING_STAFF: {
        Vector(0.f, 0.f, 205.f, ObjectSelect.Angle);
    }
    break;
    }

    switch (Type)
    {
    case MODEL_ORB_OF_HEALING:
    case MODEL_ORB_OF_GREATER_DEFENSE:
    case MODEL_ORB_OF_GREATER_DAMAGE:
    case MODEL_ORB_OF_SUMMONING: {
        Position[0] += 0.005f;
        Position[1] -= 0.02f;
    }
    break;
    case MODEL_POTION + 21: {
        Position[0] += 0.005f;
        Position[1] -= 0.005f;
    }
    break;
    case MODEL_JEWEL_OF_BLESS:
    case MODEL_JEWEL_OF_SOUL:
    case MODEL_JEWEL_OF_CREATION: {
        Position[0] += 0.005f;
        Position[1] += 0.015f;
    }
    break;
    }

    if (trs)
    {
        VectorCopy(trs->rotation.data(), ObjectSelect.Angle);
        VectorCopy(presentationAnchor, Position);
    }
    if (1 == Select)
    {
        ObjectSelect.Angle[1] = WorldTime * 0.45f;
    }

    ObjectSelect.Type = Type;
    if (ObjectSelect.Type >= MODEL_HELM && ObjectSelect.Type < MODEL_BOOTS + MAX_ITEM_INDEX ||
        ObjectSelect.Type == MODEL_ARMORINVEN_60 || ObjectSelect.Type == MODEL_ARMORINVEN_61 ||
        ObjectSelect.Type == MODEL_ARMORINVEN_62 || ObjectSelect.Type == MODEL_ARMORINVEN_74)
        ObjectSelect.Type = MODEL_PLAYER;
    else if (ObjectSelect.Type == MODEL_POTION + 12)
    {
        if (Level == 0)
        {
            ObjectSelect.Type = MODEL_EVENT;
            Type = MODEL_EVENT;
        }
        else if (Level == 2)
        {
            ObjectSelect.Type = MODEL_EVENT + 1;
            Type = MODEL_EVENT + 1;
        }
    }

    BMD *b = &Models[ObjectSelect.Type];
    b->CurrentAction = 0;
    ObjectSelect.AnimationFrame = 0;
    ObjectSelect.PriorAnimationFrame = 0;
    ObjectSelect.PriorAction = 0;
    if (Type >= MODEL_HELM && Type < MODEL_HELM + MAX_ITEM_INDEX)
    {
        b->BodyHeight = -160.f;

        if (Check_LuckyItem(Type - MODEL_ITEM))
            b->BodyHeight -= 10.0f;
        if (Type == MODEL_HELM + 65 || Type == MODEL_HELM + 70)
            Position[0] += 0.04f;
    }
    else if (Type >= MODEL_ARMOR && Type < MODEL_ARMOR + MAX_ITEM_INDEX)
    {
        b->BodyHeight = -100.f;

        if (Check_LuckyItem(Type - MODEL_ITEM))
            b->BodyHeight -= 13.0f;
    }
    else if (Type >= MODEL_GLOVES && Type < MODEL_GLOVES + MAX_ITEM_INDEX)
        b->BodyHeight = -70.f;
    else if (Type >= MODEL_PANTS && Type < MODEL_PANTS + MAX_ITEM_INDEX)
        b->BodyHeight = -50.f;
    else
        b->BodyHeight = 0.f;
    float Scale = 0.f;

    if (Type >= MODEL_HELM && Type < MODEL_BOOTS + MAX_ITEM_INDEX)
    {
        if (Type >= MODEL_HELM && Type < MODEL_HELM + MAX_ITEM_INDEX)
        {
            Scale = MODEL_MISTERY_HELM <= Type && MODEL_LILIUM_HELM >= Type ? 0.007f : 0.0039f;
            if (Type == MODEL_SYLPHID_RAY_HELM)
                Scale = 0.007f;

            if (Type == MODEL_HELM + 65 || Type == MODEL_HELM + 70)
                Scale = 0.007f;
        }
        else if (Type >= MODEL_ARMOR && Type < MODEL_ARMOR + MAX_ITEM_INDEX)
            Scale = 0.0039f;
        else if (Type >= MODEL_GLOVES && Type < MODEL_GLOVES + MAX_ITEM_INDEX)
            Scale = 0.0038f;
        else if (Type >= MODEL_PANTS && Type < MODEL_PANTS + MAX_ITEM_INDEX)
            Scale = 0.0033f;
        else if (Type >= MODEL_BOOTS && Type < MODEL_BOOTS + MAX_ITEM_INDEX)
            Scale = 0.0032f;
        else if (Type == MODEL_VENOM_MIST_ARMOR)
            Scale = 0.0035f;
        else if (Type == MODEL_VOLCANO_ARMOR)
            Scale = 0.0035f;
        else if (Type == MODEL_DRAGON_KNIGHT_ARMOR)
            Scale = 0.0033f;
        if (Type == MODEL_ASHCROW_ARMOR)
            Scale = 0.0032f;
        else if (Type == MODEL_ECLIPSE_ARMOR)
            Scale = 0.0032f;
        else if (Type == MODEL_GLORIOUS_GLOVES)
            Scale = 0.0032f;
    }
    else
    {
        if (Type == MODEL_WINGS_OF_DARKNESS)
            Scale = 0.0015f;
        else if (Check_Jewel_Com(Type - MODEL_ITEM) != COMGEM::NOGEM)
        {
            Scale = 0.004f;
            switch (Check_Jewel_Com(Type - MODEL_ITEM))
            {
            case COMGEM::eLOW_C:
                Position[0] -= 0.05f;
                Scale = 0.003f;
                break;
            case COMGEM::eUPPER_C:
                Position[0] -= 0.05f;
                Scale = 0.004f;
                break;
            case COMGEM::eCREATE_C:
                Position[1] += 0.05f;
                Scale = 0.0025f;
                break;
            case COMGEM::eCHAOS_C:
                Position[1] += 0.025f;
                Scale = 0.002f;
                break;
            case COMGEM::ePROTECT_C:
                Position[1] += 0.05f;
                Scale = 0.0036f;
                break;
            case COMGEM::eLIFE_C:
                Position[1] += 0.025f;
                Scale = 0.0035f;
                break;
            case COMGEM::eGEMSTONE_C:
                Position[1] += 0.05f;
                Scale = 0.0035f;
                break;
            case COMGEM::eHARMONY_C:
                Scale = 0.005f;
                break;
            }
        }
        else if (Type >= MODEL_RED_RIBBON_BOX && Type <= MODEL_BLUE_RIBBON_BOX)
        {
            Scale = 0.001f;
            Position[1] -= 0.05f;
        }
        else if (Type >= MODEL_SEED_FIRE && Type <= MODEL_SEED_EARTH)
            Scale = 0.0022f;
        else if (Type >= MODEL_SPHERE_MONO && Type <= MODEL_SPHERE_5)
            Scale = 0.0017f;
        else if (Type >= MODEL_SEED_SPHERE_FIRE_1 && Type <= MODEL_SEED_SPHERE_EARTH_5)
            Scale = 0.0017f;
        else if (Type >= MODEL_WING && Type < MODEL_WING + MAX_ITEM_INDEX)
        {
            Scale = 0.002f;
        }
        else if (Type == MODEL_PUMPKIN_OF_LUCK || Type == MODEL_JACK_OLANTERN_FOOD)
        {
            Scale = 0.003f;
        }
        else if (Type >= MODEL_JACK_OLANTERN_BLESSINGS && Type <= MODEL_JACK_OLANTERN_CRY)
        {
            Scale = 0.0025f;
        }
        else if (Type == MODEL_JACK_OLANTERN_DRINK)
        {
            Scale = 0.001f;
        }
        else if (Type >= MODEL_PINK_CHOCOLATE_BOX && Type <= MODEL_BLUE_CHOCOLATE_BOX)
        {
            Scale = 0.002f;
            Position[1] += 0.05f;
            Vector(0.f, ObjectSelect.Angle[1], 0.f, ObjectSelect.Angle);
        }
        else if (Type >= MODEL_EVENT + 21 && Type <= MODEL_EVENT + 23)
        {
            Scale = 0.002f;
            if (Type == MODEL_EVENT + 21)
                Position[1] += 0.08f;
            else
                Position[1] += 0.06f;
            Vector(0.f, ObjectSelect.Angle[1], 0.f, ObjectSelect.Angle);
        }
        else if (Type == MODEL_POTION + 21)
            Scale = 0.002f;
        else if (Type == MODEL_GREAT_REIGN_CROSSBOW)
            Scale = 0.002f;
        else if (Type == MODEL_EVENT + 11)
            Scale = 0.0015f;
        else if (Type == MODEL_DARK_HORSE_ITEM)
            Scale = 0.0015f;
        else if (Type == MODEL_DARK_RAVEN_ITEM)
            Scale = 0.005f;
        else if (Type == MODEL_CAPE_OF_LORD)
            Scale = 0.002f;
        else if (Type == MODEL_EVENT + 16)
            Scale = 0.002f;
        else if (Type == MODEL_SCROLL_OF_ARCHANGEL)
            Scale = 0.002f;
        else if (Type == MODEL_BLOOD_BONE)
            Scale = 0.0018f;
        else if (Type == MODEL_INVISIBILITY_CLOAK)
            Scale = 0.0018f;
        else if (Type == MODEL_HELPER + 46)
        {
            Scale = 0.0018f;
        }
        else if (Type == MODEL_HELPER + 47)
        {
            Scale = 0.0018f;
        }
        else if (Type == MODEL_HELPER + 48)
        {
            Scale = 0.0018f;
        }
        else if (Type == MODEL_POTION + 54)
        {
            Scale = 0.0024f;
        }
        else if (Type == MODEL_POTION + 58)
        {
            Scale = 0.0012f;
        }
        else if (Type == MODEL_POTION + 59 || Type == MODEL_POTION + 60)
        {
            Scale = 0.0010f;
        }
        else if (Type == MODEL_POTION + 61 || Type == MODEL_POTION + 62)
        {
            Scale = 0.0009f;
        }
        else if (Type == MODEL_POTION + 53)
        {
            Scale = 0.00078f;
        }
        else if (Type == MODEL_HELPER + 43 || Type == MODEL_HELPER + 44 ||
                 Type == MODEL_HELPER + 45)
        {
            Scale = 0.0021f;
        }
        else if (Type >= MODEL_POTION + 70 && Type <= MODEL_POTION + 71)
        {
            Scale = 0.0028f;
        }
        else if (Type >= MODEL_POTION + 72 && Type <= MODEL_POTION + 77)
        {
            Scale = 0.0025f;
        }
        else if (Type == MODEL_HELPER + 59)
        {
            Scale = 0.0008f;
        }
        else if (Type >= MODEL_HELPER + 54 && Type <= MODEL_HELPER + 58)
        {
            Scale = 0.004f;
        }
        else if (Type >= MODEL_POTION + 78 && Type <= MODEL_POTION + 82)
        {
            Scale = 0.0025f;
        }
        else if (Type == MODEL_HELPER + 60)
        {
            Scale = 0.005f;
        }
        else if (Type == MODEL_HELPER + 61)
        {
            Scale = 0.0018f;
        }
        else if (Type == MODEL_POTION + 83)
        {
            Scale = 0.0009f;
        }
        else if (Type == MODEL_HELPER + 43 || Type == MODEL_HELPER + 44 ||
                 Type == MODEL_HELPER + 45)
        {
            Scale = 0.0021f;
        }
        else if (Type == MODEL_POTION + 91)
        {
            Scale = 0.0034f;
        }
        else if (Type == MODEL_POTION + 92)
        {
            Scale = 0.0024f;
        }
        else if (Type == MODEL_POTION + 93)
        {
            Scale = 0.0024f;
        }
        else if (Type == MODEL_POTION + 95)
        {
            Scale = 0.0024f;
        }
        else if (Type == MODEL_POTION + 94)
        {
            Scale = 0.0022f;
        }
        else if (Type == MODEL_CHERRY_BLOSSOM_PLAYBOX)
        {
            Scale = 0.0031f;
        }
        else if (Type == MODEL_CHERRY_BLOSSOM_WINE)
        {
            Scale = 0.0044f;
        }
        else if (Type == MODEL_CHERRY_BLOSSOM_RICE_CAKE)
        {
            Scale = 0.0031f;
        }
        else if (Type == MODEL_CHERRY_BLOSSOM_FLOWER_PETAL)
        {
            Scale = 0.0061f;
        }
        else if (Type == MODEL_POTION + 88)
        {
            Scale = 0.0035f;
        }
        else if (Type == MODEL_POTION + 89)
        {
            Scale = 0.0035f;
        }
        else if (Type == MODEL_GOLDEN_CHERRY_BLOSSOM_BRANCH)
        {
            Scale = 0.0035f;
        }
        else if (Type >= MODEL_HELPER + 62 && Type <= MODEL_HELPER + 63)
        {
            Scale = 0.002f;
        }
        else if (Type >= MODEL_POTION + 97 && Type <= MODEL_POTION + 98)
        {
            Scale = 0.003f;
        }
        else if (Type == MODEL_POTION + 96)
        {
            Scale = 0.0028f;
        }
        else if (MODEL_DEMON == Type || Type == MODEL_SPIRIT_OF_GUARDIAN)
        {
            switch (Type)
            {
            case MODEL_DEMON:
                Scale = 0.0005f;
                break;
            case MODEL_SPIRIT_OF_GUARDIAN:
                Scale = 0.0016f;
                break;
            }
        }
        else if (Type == MODEL_PET_RUDOLF)
        {
            Scale = 0.0015f;
        }
        else if (Type == MODEL_PET_PANDA)
        {
            Scale = 0.0020f;
        }
        else if (Type == MODEL_SNOWMAN_TRANSFORMATION_RING)
        {
            Scale = 0.0026f;
        }
        else if (Type == MODEL_PANDA_TRANSFORMATION_RING)
        {
            Scale = 0.0026f;
        }
        else if (Type == MODEL_HELPER + 69)
        {
            Scale = 0.0023f;
        }
        else if (Type == MODEL_HELPER + 70)
        {
            Scale = 0.0018f;
        }
        else if (Type == MODEL_HELPER + 81)
            Scale = 0.0012f;
        else if (Type == MODEL_HELPER + 82)
            Scale = 0.0012f;
        else if (Type == MODEL_HELPER + 93)
            Scale = 0.0021f;
        else if (Type == MODEL_HELPER + 94)
            Scale = 0.0021f;
        else if (Type == MODEL_DIVINE_SWORD_OF_ARCHANGEL)
        {
            if (ItemLevel >= 0)
            {
                Scale = 0.0025f;
            }
            else
            {
                Scale = 0.001f;
                ItemLevel = 0;
            }
        }
        else if (Type == MODEL_DIVINE_STAFF_OF_ARCHANGEL)
        {
            if (ItemLevel >= 0)
            {
                Scale = 0.0019f;
            }
            else
            {
                Scale = 0.001f;
                ItemLevel = 0;
            }
        }
        else if (Type == MODEL_DIVINE_CB_OF_ARCHANGEL)
        {
            if (ItemLevel >= 0)
            {
                Scale = 0.0025f;
            }
            else
            {
                Scale = 0.0015f;
                ItemLevel = 0;
            }
        }
        else if (Type >= MODEL_BATTLE_SCEPTER && Type <= MODEL_LORD_SCEPTER)
        {
            Scale = 0.003f;
        }
        else if (Type == MODEL_GREAT_LORD_SCEPTER)
        {
            Scale = 0.0025f;
        }
        else if (Type == MODEL_STRYKER_SCEPTER)
        {
            Scale = 0.0024f;
        }
        else if (Type == MODEL_EVENT + 12)
        {
            Scale = 0.0012f;
        }
        else if (Type == MODEL_EVENT + 13)
        {
            Scale = 0.0025f;
        }
        else if (Type == MODEL_EVENT + 14)
        {
            Scale = 0.0028f;
        }
        else if (Type == MODEL_EVENT + 15)
        {
            Scale = 0.0023f;
        }
        else if (Type >= MODEL_JEWEL_OF_CREATION && Type < MODEL_TEAR_OF_ELF)
        {
            Scale = 0.0025f;
        }
        else if (Type >= MODEL_TEAR_OF_ELF && Type < MODEL_POTION + 27)
        {
            Scale = 0.0028f;
        }
        else if (Type == MODEL_FIRECRACKER)
        {
            Scale = 0.007f;
        }
        else if (Type == MODEL_CHRISTMAS_FIRECRACKER)
        {
            Scale = 0.0025f;
        }
        else if (Type == MODEL_GM_GIFT)
        {
            Scale = 0.0014f;
        }
        else if (Type == MODEL_MOONSTONE_PENDANT)
        {
            Scale = 0.0025f;
        }
        else if (Type == MODEL_GEMSTONE)
        {
            Scale = 0.0035f;
        }
        else if (Type == MODEL_JEWEL_OF_HARMONY)
        {
            Scale = 0.005f;
        }
        else if (Type == MODEL_LOWER_REFINE_STONE)
        {
            Position[1] += -0.005f;
            Scale = 0.0035f;
        }
        else if (Type == MODEL_HIGHER_REFINE_STONE)
        {
            Position[1] += -0.005f;
            Scale = 0.004f;
        }
        else if (Type == MODEL_SIEGE_POTION)
        {
            Scale = 0.0025f;
        }
        else if (Type == MODEL_HELPER + 43 || Type == MODEL_HELPER + 44 ||
                 Type == MODEL_HELPER + 45)
        {
            Scale = 0.0021f;
        }
        else if (Type == MODEL_HELPER + 7)
        {
            Scale = 0.0025f;
        }
        else if (Type == MODEL_LIFE_STONE_ITEM)
        {
            Scale = 0.0025f;
        }
        else if (Type == MODEL_SPLINTER_OF_ARMOR)
        {
            Scale = 0.0019f;
        }
        else if (Type == MODEL_BLESS_OF_GUARDIAN)
        {
            Scale = 0.004f;
        }
        else if (Type == MODEL_CLAW_OF_BEAST)
        {
            Scale = 0.004f;
        }
        else if (Type == MODEL_FRAGMENT_OF_HORN)
        {
            Scale = 0.004f;
        }
        else if (Type == MODEL_BROKEN_HORN)
        {
            Scale = 0.007f;
        }
        else if (Type == MODEL_HORN_OF_FENRIR)
        {
            Scale = 0.005f;
        }
        else if (Type == MODEL_SYLPH_WIND_BOW)
        {
            Scale = 0.0022f;
        }
        else if (Type == MODEL_OLD_SCROLL)
        {
            Scale = 0.0013f;
        }
        else if (Type == MODEL_ILLUSION_SORCERER_COVENANT)
        {
            Scale = 0.003f;
        }
        else if (Type == MODEL_SCROLL_OF_BLOOD)
        {
            Scale = 0.003f;
        }
        else if (Type == MODEL_POTION + 64)
        {
            Scale = 0.003f;
        }
        else if (Type == MODEL_FLAME_OF_DEATH_BEAM_KNIGHT)
            Scale = 0.003f;
        else if (Type == MODEL_HORN_OF_HELL_MAINE)
            Scale = 0.0035f;
        else if (Type == MODEL_FEATHER_OF_DARK_PHOENIX)
            Scale = 0.0035f;
        else if (Type == MODEL_EYE_OF_ABYSSAL)
            Scale = 0.003f;
        else if (Type == MODEL_FLAME_OF_CONDOR)
            Scale = 0.005f;
        else if (Type == MODEL_FEATHER_OF_CONDOR)
            Scale = 0.005f;
        else if (Type == MODEL_DAYBREAK)
        {
            Scale = 0.0028f;
        }
        else if (Type == MODEL_ALBATROSS_BOW)
        {
            Scale = 0.0020f;
        }
        else if (Type == MODEL_STINGER_BOW)
        {
            Scale = 0.0032f;
        }
        else if (Type == MODEL_LOCHS_FEATHER || Type == MODEL_FRUITS)
        {
            Scale = 0.003f;
        }
        else if (Type == MODEL_POTION + 100)
        {
            Scale = 0.0040f;
        }
        else if (Type >= MODEL_POTION && Type < MODEL_POTION + MAX_ITEM_INDEX)
        {
            Scale = 0.0035f;
        }
        else if (Type >= MODEL_SPEAR && Type < MODEL_SPEAR + MAX_ITEM_INDEX)
        {
            if (Type == MODEL_DRAGON_SPEAR)
                Scale = 0.0018f;
            else if (Type == MODEL_BEUROBA)
                Scale = 0.0025f;
            else
                Scale = 0.0021f;
        }
        else if (Type >= MODEL_STAFF && Type < MODEL_STAFF + MAX_ITEM_INDEX)
        {
            if (Type >= MODEL_MISTERY_STICK && Type <= MODEL_ETERNAL_WING_STICK)
                Scale = 0.0028f;
            else if (Type >= MODEL_BOOK_OF_SAHAMUTT && Type <= MODEL_STAFF + 29)
                Scale = 0.004f;
            else if (Type == MODEL_CHROMATIC_STAFF)
                Scale = 0.0028f;
            else if (Type == MODEL_RAVEN_STICK)
                Scale = 0.0028f;
            else
                Scale = 0.0022f;
        }
        else if (Type == MODEL_ARROWS)
            Scale = 0.0011f;
        else if (Type == MODEL_BOLT)
            Scale = 0.0012f;
        else if (Type == MODEL_EVENT + 6)
            Scale = 0.0039f;
        else if (Type == MODEL_EVENT + 8)
            Scale = 0.0015f;
        else if (Type == MODEL_EVENT + 9)
            Scale = 0.0019f;
        else
        {
            Scale = 0.0025f;
        }

        if (Type >= static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_BEGIN &&
            Type <= static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_END)
        {
            Scale = 0.0020f;
        }

        if (Type == MODEL_EVENT + 10)
        {
            Scale = 0.001f;
        }
        else if (Type >= MODEL_CHAIN_LIGHTNING_PARCHMENT && Type <= MODEL_INNOVATION_PARCHMENT)
        {
            Scale = 0.0023f;
        }
        else if (Type == MODEL_HELPER + 66)
        {
            Scale = 0.0020f;
        }
        else if (Type == MODEL_POTION + 140)
        {
            Scale = 0.0026f;
        }
        else if (Type == MODEL_SKELETON_TRANSFORMATION_RING)
        {
            Scale = 0.0033f;
        }
        else if (Type == MODEL_PET_SKELETON)
        {
            Scale = 0.0009f;
        }
        else if (Type >= MODEL_POTION + 145 && Type <= MODEL_POTION + 150)
        {
            Scale = 0.0018f;
        }
        else if (Type >= MODEL_HELPER + 125 && Type <= MODEL_HELPER + 127)
        {
            Scale = 0.0013f;
        }
#ifdef LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM
        else if (Type == MODEL_HELPER + 128) // ????
        {
            Scale = 0.0035f;
        }
        else if (Type == MODEL_HELPER + 129) // ????
        {
            Scale = 0.0035f;
        }
        else if (Type == MODEL_HELPER + 134) // ??
        {
            Scale = 0.0033f;
        }
#endif //LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM
#ifdef LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM_PART_2
        else if (Type == MODEL_HELPER + 130) // ???
        {
            Scale = 0.0032f;
        }
        else if (Type == MODEL_HELPER + 131) // ????
        {
            Scale = 0.0033f;
        }
        else if (Type == MODEL_HELPER + 132) // ?????
        {
            Scale = 0.0025f;
        }
        else if (Type == MODEL_HELPER + 133) // ??????
        {
            Scale = 0.0033f;
        }
#endif //LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM_PART_2
        else if (Type == MODEL_HELPER + 71 || Type == MODEL_HELPER + 72 ||
                 Type == MODEL_HELPER + 73 || Type == MODEL_HELPER + 74 ||
                 Type == MODEL_HELPER + 75)
        {
            Scale = 0.0019f;
        }
        else if (Type == MODEL_AIR_LYN_BOW)
        {
            Scale = 0.0023f;
        }
        else if (Type == MODEL_HELPER + 97 || Type == MODEL_HELPER + 98 ||
                 Type == MODEL_POTION + 91)
        {
            Scale = 0.0028f;
        }
        else if (Type == MODEL_HELPER + 99)
        {
            Scale = 0.0025f;
        }
        else if (Type == MODEL_POTION + 110)
        {
            Scale = 0.004f;
        }
        else if (Type == MODEL_HELPER + 107)
        {
            Scale = 0.0034f;
        }
        else if (Type == MODEL_POTION + 133)
        {
            Scale = 0.0030f;
        }
        else if (Type == MODEL_HELPER + 105)
        {
            Scale = 0.002f;
        }
        else if (MODEL_SUSPICIOUS_SCRAP_OF_PAPER <= Type && Type <= MODEL_COMPLETE_SECROMICON)
        {
            switch (Type)
            {
            case MODEL_SUSPICIOUS_SCRAP_OF_PAPER: {
                Scale = 0.004f;
            }
            break;
            case MODEL_GAIONS_ORDER: {
                Scale = 0.005f;
            }
            break;
            case MODEL_FIRST_SECROMICON_FRAGMENT:
            case MODEL_SECOND_SECROMICON_FRAGMENT:
            case MODEL_THIRD_SECROMICON_FRAGMENT:
            case MODEL_FOURTH_SECROMICON_FRAGMENT:
            case MODEL_FIFTH_SECROMICON_FRAGMENT:
            case MODEL_SIXTH_SECROMICON_FRAGMENT: {
                Scale = 0.004f;
            }
            break;
            case MODEL_COMPLETE_SECROMICON: {
                Scale = 0.003f;
            }
            break;
            }
        }
        else if (Type == MODEL_PET_UNICORN)
        {
            Scale = 0.0015f;
        }
        else if (Type == MODEL_WING + 130)
        {
            Scale = 0.0012f;
        }
        else if (Type >= MODEL_POTION + 134 && Type <= MODEL_POTION + 139)
        {
            Scale = 0.0050f;
        }
        else if (Type >= MODEL_HELPER + 109 && Type <= MODEL_HELPER + 112)
        {
            Scale = 0.0045f;
        }

        else if (Type >= MODEL_HELPER + 113 && Type <= MODEL_HELPER + 115)
        {
            Scale = 0.0018f;
        }
        else if (Type >= MODEL_POTION + 112 && Type <= MODEL_POTION + 113)
        {
            Scale = 0.0032f;
        }
        else if (Type == MODEL_HELPER + 116)
        {
            Scale = 0.0021f;
        }
        else if (Type >= MODEL_POTION + 114 && Type <= MODEL_POTION + 119)
        {
            Scale = 0.0038f;
        }
        else if (Type >= MODEL_POTION + 126 && Type <= MODEL_POTION + 129)
        {
            Scale = 0.0038f;
        }
        else if (Type >= MODEL_POTION + 130 && Type <= MODEL_POTION + 132)
        {
            Scale = 0.0038f;
        }
        else if (Type == MODEL_HELPER + 121)
        {
            Scale = 0.0018f;
            //Scale = 1.f;
        }
        else if (Type == MODEL_HELPER + 124)
            Scale = 0.0018f;
        else if (Type >= MODEL_CAPE_OF_FIGHTER && Type <= MODEL_CAPE_OF_OVERRULE)
        {
            Scale = 0.002f;
        }
        else if (Type == MODEL_WING + 135)
        {
            Scale = 0.0012f;
        }
        else if (Type >= MODEL_SACRED_GLOVE && Type <= MODEL_PIERCING_BLADE_GLOVE)
        {
            Scale = 0.0035f;
        }
        else if (Type == MODEL_PHOENIX_SOUL_STAR)
        {
            Scale = 0.003f;
        }
        else if (Type >= MODEL_CHAIN_DRIVE_PARCHMENT && Type <= MODEL_INCREASE_BLOCK_PARCHMENT)
        {
            Scale = 0.0023f;
        }
        else if (Type == MODEL_ARMORINVEN_60 || Type == MODEL_ARMORINVEN_62 ||
                 Type == MODEL_ARMORINVEN_61 || Type == MODEL_ARMORINVEN_74)
        {
            b->BodyHeight = -100.f;
            Scale = 0.0039f;
        }
        // LEM_TSET  ??? ??, ??? ?? ???[lem_2010.9.7]
        else if (Type >= MODEL_HELPER + 135 && Type <= MODEL_HELPER + 145)
        {
            Scale = 0.001f;
        }
        else if (Type == MODEL_POTION + 160 || Type == MODEL_POTION + 161)
        {
            Scale = 0.001f;
        }
        else if (Type == MODEL_HELM + 62 || Type == MODEL_HELM + 63 || Type == MODEL_HELM + 65 ||
                 Type == MODEL_HELM + 70)
        {
            Scale = 0.001f;
        }
    }

    b->Animation(BoneTransform, ObjectSelect.AnimationFrame, ObjectSelect.PriorAnimationFrame,
                 ObjectSelect.PriorAction, ObjectSelect.Angle, ObjectSelect.HeadAngle, false,
                 false);

    CHARACTER Armor;
    OBJECT *o = &Armor.Object;
    o->Type = Type;
    ItemObjectAttribute(o);
    o->LightEnable = false;
    Armor.Class = CLASS_ELF; // ??

#ifdef PBG_ADD_ITEMRESIZE
    int ScreenPos_X = 0, ScreenPos_Y = 0;
    cameraProjection_.WorldToScreen(g_Camera, Position, &ScreenPos_X, &ScreenPos_Y);
#endif //PBG_ADD_ITEMRESIZE

    o->Scale = (trs ? trs->scale : Scale) * presentationScale;
    VectorSubtract(Position, presentationAnchor, Direction);
    VectorMA(presentationAnchor, presentationScale, Direction, Position);

    VectorCopy(Position, o->Position);

    vec3_t Light;
    float alpha = o->Alpha;

    Vector(1.f, 1.f, 1.f, Light);

    // Item slots have no world-effect owner; their temporary model stays local to this draw.
    RenderPartObject(o, Type, NULL, Light, alpha, ItemLevel, excellentFlags, ancientDiscriminator,
                     true, true, true);
}

void SessionRenderUnit::RenderItem3D(float sx, float sy, float Width, float Height, int Type,
                                     int Level, int excellentFlags, int ancientDiscriminator,
                                     bool PickUp, float presentationScale, bool useSlotTrs)
{
    const auto *trs = useSlotTrs ? UI::Items::ItemSlotTrs::Find(Type) : nullptr;
    bool Success = false;
    if ((g_pPickedItem == NULL || PickUp) && CheckMouseIn(sx, sy, Width, Height))
    {
#ifdef PBG_ADD_INGAMESHOPMSGBOX
        if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_INGAMESHOP))
        {
            Success = true;
        }
        else
#endif //PBG_ADD_INGAMESHOPMSGBOX
        {
            if (g_pNewUISystem->CheckMouseUse() == false)
                Success = true;
        }
    }

    if (trs)
    {
        sx += Width * trs->position[0];
        sy += Height * trs->position[1];
    }
    else if (Type >= ITEM_SWORD && Type < ITEM_SWORD + MAX_ITEM_INDEX)
    {
        sx += Width * 0.8f;
        sy += Height * 0.85f;
    }
    else if (Type >= ITEM_AXE && Type < ITEM_MACE + MAX_ITEM_INDEX)
    {
        if (Type == ITEM_DIVINE_SCEPTER_OF_ARCHANGEL)
        {
            sx += Width * 0.6f;
            sy += Height * 0.5f;
        }
        else
        {
            sx += Width * 0.8f;
            sy += Height * 0.7f;
        }
    }
    else if (Type >= ITEM_SPEAR && Type < ITEM_SPEAR + MAX_ITEM_INDEX)
    {
        sx += Width * 0.6f;
        sy += Height * 0.65f;
    }
    else if (Type == ITEM_CELESTIAL_BOW)
    {
        sx += Width * 0.5f;
        sy += Height * 0.5f;
    }
    else if (Type == ITEM_GREAT_REIGN_CROSSBOW)
    {
        sx += Width * 0.7f;
        sy += Height * 0.75f;
    }
    else if (Type == ITEM_ARROW_VIPER_BOW)
    {
        sx += Width * 0.5f;
        sy += Height * 0.55f;
    }
    else if (Type >= ITEM_CROSSBOW && Type < ITEM_BOW + MAX_ITEM_INDEX)
    {
        sx += Width * 0.7f;
        sy += Height * 0.7f;
    }
    else if (Type >= ITEM_STAFF && Type < ITEM_STAFF + MAX_ITEM_INDEX)
    {
        sx += Width * 0.6f;
        sy += Height * 0.55f;
    }
    else if (Type >= ITEM_SHIELD && Type < ITEM_SHIELD + MAX_ITEM_INDEX)
    {
        sx += Width * 0.5f;
        if (Type == ITEM_GRAND_SOUL_SHIELD)
            sy += Height * 0.7f;
        else if (Type == ITEM_ELEMENTAL_SHIELD)
            sy += Height * 0.9f;
        else if (Type == ITEM_CROSS_SHIELD)
        {
            sx += Width * 0.05f;
            sy += Height * 0.5f;
        }
        else
            sy += Height * 0.6f;
    }
    else if (Type >= ITEM_HELM && Type < ITEM_HELM + MAX_ITEM_INDEX)
    {
        sx += Width * 0.5f;
        sy += Height * 0.8f;
    }
    else if (Type >= ITEM_ARMOR && Type < ITEM_ARMOR + MAX_ITEM_INDEX)
    {
        sx += Width * 0.5f;
        if (Type == ITEM_PAD_ARMOR || Type == ITEM_BONE_ARMOR || Type == ITEM_SCALE_ARMOR)
        {
            sy += Height * 1.05f;
        }
        else if (Type == ITEM_LEGENDARY_ARMOR || Type == ITEM_BRASS_ARMOR)
        {
            sy += Height * 1.1f;
        }
        else if (Type == ITEM_DARK_PHOENIX_ARMOR || Type == ITEM_GRAND_SOUL_ARMOR ||
                 Type == ITEM_THUNDER_HAWK_ARMOR)
        {
            sy += Height * 0.8f;
        }
        else if (Type == ITEM_STORM_CROW_ARMOR)
        {
            sy += Height * 1.0f;
        }
        else
        {
            sy += Height * 0.8f;
        }
    }
    else if (Type >= ITEM_PANTS && Type < ITEM_BOOTS + MAX_ITEM_INDEX)
    {
        sx += Width * 0.5f;
        sy += Height * 0.9f;
    }
    else if (Type == ITEM_LOCHS_FEATHER && Level == 1)
    {
        sx += Width * 0.55f;
        sy += Height * 0.85f;
    }
    else if (Type == ITEM_LOCHS_FEATHER || Type == ITEM_FRUITS)
    {
        sx += Width * 0.6f;
        sy += Height * 1.f;
    }
    else if (Type == ITEM_SCROLL_OF_ARCHANGEL || Type == ITEM_BLOOD_BONE)
    {
        sx += Width * 0.5f;
        sy += Height * 0.9f;
    }
    else if (Type == ITEM_INVISIBILITY_CLOAK)
    {
        sx += Width * 0.5f;
        sy += Height * 0.75f;
    }
    else if (Type == ITEM_WEAPON_OF_ARCHANGEL)
    {
        switch (Level)
        {
        case 0:
            sx += Width * 0.5f;
            sy += Height * 0.5f;
            break;
        case 1:
            sx += Width * 0.7f;
            sy += Height * 0.8f;
            break;
        case 2:
            sx += Width * 0.7f;
            sy += Height * 0.7f;
            break;
        }
    }
    else if (Type == ITEM_WIZARDS_RING)
    {
        switch (Level)
        {
        case 0:
            sx += Width * 0.5f;
            sy += Height * 0.65f;
            break;
        case 1:
        case 2:
        case 3:
            sx += Width * 0.5f;
            sy += Height * 0.8f;
            break;
        }
    }
    else if (Type == ITEM_ARMOR_OF_GUARDSMAN)
    {
        sx += Width * 0.5f;
        sy += Height * 0.5f;
    }
    else if (Type == ITEM_DARK_HORSE_ITEM)
    {
        sx += Width * 0.5f;
        sy += Height * 0.6f;
    }
    else if (Type == ITEM_CAPE_OF_LORD)
    {
        sx += Width * 0.5f;
        sy += Height * 0.5f;
    }
    else if (Type == ITEM_SPIRIT)
    {
        sx += Width * 0.5f;
        sy += Height * 0.9f;
    }
    else if (Type == ITEM_SIEGE_POTION)
    {
        sx += Width * 0.5f;
        sy += Height * 0.5f;
    }
    else if (Type == ITEM_HELPER + 7)
    {
        sx += Width * 0.5f;
        sy += Height * 0.9f;
    }
    else if (Type == ITEM_LIFE_STONE_ITEM)
    {
        switch (Level)
        {
        case 0:
            sx += Width * 0.5f;
            sy += Height * 0.8f;
            break;
        case 1:
            sx += Width * 0.5f;
            sy += Height * 0.5f;
            break;
        }
    }
    else if (Type == ITEM_SPLINTER_OF_ARMOR)
    {
        sx += Width * 0.5f;
        sy += Height * 0.5f;
    }
    else if (Type == MODEL_BLESS_OF_GUARDIAN)
    {
        sx += Width * 0.5f;
        sy += Height * 0.5f;
    }
    else if (Type == MODEL_CLAW_OF_BEAST)
    {
        sx += Width * 0.5f;
        sy += Height * 0.5f;
    }
    else if (Type == MODEL_FRAGMENT_OF_HORN)
    {
        sx += Width * 0.5f;
        sy += Height * 0.5f;
    }
    else if (Type == MODEL_BROKEN_HORN)
    {
        sx += Width * 0.5f;
        sy += Height * 0.5f;
    }
    else if (Type == MODEL_HORN_OF_FENRIR)
    {
        sx += Width * 0.5f;
        sy += Height * 0.5f;
    }
    else if (Type >= ITEM_HELPER && Type < ITEM_HELPER + MAX_ITEM_INDEX)
    {
        sx += Width * 0.5f;
        sy += Height * 0.7f;
    }
    else if (Type == ITEM_POTION + 12)
    {
        sx += Width * 0.5f;
        sy += Height * 0.5f;
    }
    else if (Type == ITEM_BOX_OF_LUCK && (Level == 3 || Level == 13))
    {
        sx += Width * 0.5f;
        sy += Height * 0.5f;
    }
    else if (Type == ITEM_BOX_OF_LUCK && (Level == 14 || Level == 15))
    {
        sx += Width * 0.5f;
        sy += Height * 0.8f;
    }
    else if (Type == ITEM_ALE && Level == 1)
    {
        sx += Width * 0.5f;
        sy += Height * 0.8f;
    }
    else if (Type == ITEM_DEVILS_EYE || Type == ITEM_DEVILS_KEY || Type == ITEM_DEVILS_INVITATION)
    {
        sx += Width * 0.5f;
        sy += Height * 0.5f;
    }
    else if (Type == ITEM_POTION + 21)
    {
        switch (Level)
        {
        case 0:
            sx += Width * 0.5f;
            sy += Height * 0.5f;
            break;
        case 1:
            sx += Width * 0.4f;
            sy += Height * 0.8f;
            break;
        case 2:
            sx += Width * 0.4f;
            sy += Height * 0.8f;
            break;
        case 3:
            sx += Width * 0.5f;
            sy += Height * 0.5f;
            break;
        }
    }
    else if (Type >= ITEM_JEWEL_OF_CREATION && Type < ITEM_TEAR_OF_ELF)
    {
        if (Type == ITEM_BROKEN_SWORD_DARK_STONE && Level == 1)
        {
            sx += Width * 0.5f;
            sy += Height * 0.8f;
        }
        else
        {
            sx += Width * 0.5f;
            sy += Height * 0.95f;
        }
    }
    else if (Type >= ITEM_JACK_OLANTERN_BLESSINGS && Type <= ITEM_JACK_OLANTERN_CRY)
    {
        sx += Width * 0.5f;
        sy += Height * 0.5f;
    }
    else if (Type >= ITEM_TEAR_OF_ELF && Type < ITEM_POTION + 27)
    {
        sx += Width * 0.5f;
        sy += Height * 0.9f;
    }
    else if (Type == ITEM_JEWEL_OF_GUARDIAN)
    {
        sx += Width * 0.5f;
        sy += Height * 0.5f;
    }
    else if (Type == INDEX_COMPILED_CELE || Type == INDEX_COMPILED_SOUL)
    {
        sx += Width * 0.55f;
        sy += Height * 0.8f;
    }
    else if (Type == ITEM_WINGS_OF_SPIRITS)
    {
        sx += Width * 0.5f;
        sy += Height * 0.45f;
    }
    else if (Type == ITEM_WINGS_OF_SOUL)
    {
        sx += Width * 0.5f;
        sy += Height * 0.4f;
    }
    else if (Type == ITEM_WINGS_OF_DRAGON)
    {
        sx += Width * 0.5f;
        sy += Height * 0.75f;
    }
    else if (Type == ITEM_WINGS_OF_DARKNESS)
    {
        sx += Width * 0.5f;
        sy += Height * 0.55f;
    }
    else if (Type == ITEM_POTION + 100)
    {
        sx += Width * 0.49f;
        //sy += Height*0.28f;
        sy += Height * 0.28f;
    }
    else if (Check_Jewel_Com(Type) != COMGEM::NOGEM)
    {
        sx += Width * 0.55f;
        sy += Height * 0.82f;
    }
    else if (Type >= ITEM_POTION && Type < ITEM_POTION + MAX_ITEM_INDEX)
    {
        sx += Width * 0.5f;
        sy += Height * 0.95f;
    }
    else if ((Type >= ITEM_ORB_OF_RAGEFUL_BLOW && Type <= ITEM_ORB_OF_GREATER_FORTITUDE) ||
             (Type >= ITEM_ORB_OF_FIRE_SLASH && Type <= ITEM_ORB_OF_DEATH_STAB))
    {
        sx += Width * 0.5f;
        sy += Height * 0.75f;
    }
    else if (Type == ITEM_HELPER + 66)
    {
        sx += Width * 1.5f;
        sy += Height * 1.5f;
    }
    else if (Type == ITEM_CAPE_OF_FIGHTER)
    {
        sx += Width * 0.5f;
        sy += Height * 0.5f;
    }
    else if (Type == ITEM_CAPE_OF_OVERRULE)
    {
        sx += Width * 0.5f;
        sy += Height * 0.5f;
    }
    else
    {
        sx += Width * 0.5f;
        sy += Height * 0.6f;
    }

    if (!trs && Type >= ITEM_SACRED_GLOVE && Type <= ITEM_PHOENIX_SOUL_STAR)
    {
        sx -= Width * 0.25f;
        sy -= Height * 0.25f;
    }

    vec3_t Position;
    cameraProjection_.ScreenToWorldRay(g_Camera, (int)(sx), (int)(sy), Position, false);
    //RenderObjectScreen(Type+MODEL_ITEM,Level,Option1,Position,Success,PickUp);
    if (Type == ITEM_BOX_OF_LUCK && Level == 1) // ????
    {
        RenderObjectScreen(MODEL_EVENT + 4, Level, excellentFlags, ancientDiscriminator, Position,
                           Success, PickUp, presentationScale, trs);
    }
    else if (Type == ITEM_BOX_OF_LUCK && Level == 2)
    {
        RenderObjectScreen(MODEL_EVENT + 5, Level, excellentFlags, ancientDiscriminator, Position,
                           Success, PickUp, presentationScale, trs);
    }
    else if (Type == ITEM_BOX_OF_LUCK && Level == 3)
    {
        RenderObjectScreen(MODEL_EVENT + 6, Level, excellentFlags, ancientDiscriminator, Position,
                           Success, PickUp, presentationScale, trs);
    }
    else if (Type == ITEM_BOX_OF_LUCK && Level == 5)
    {
        RenderObjectScreen(MODEL_EVENT + 8, Level, excellentFlags, ancientDiscriminator, Position,
                           Success, PickUp, presentationScale, trs);
    }
    else if (Type == ITEM_BOX_OF_LUCK && Level == 6)
    {
        RenderObjectScreen(MODEL_EVENT + 9, Level, excellentFlags, ancientDiscriminator, Position,
                           Success, PickUp, presentationScale, trs);
    }
    else if (Type == ITEM_BOX_OF_LUCK && 8 <= Level && Level <= 12)
    {
        RenderObjectScreen(MODEL_EVENT + 10, Level, excellentFlags, ancientDiscriminator, Position,
                           Success, PickUp, presentationScale, trs);
    }
    else if (Type == ITEM_BOX_OF_LUCK && Level == 13)
    {
        RenderObjectScreen(MODEL_EVENT + 6, Level, excellentFlags, ancientDiscriminator, Position,
                           Success, PickUp, presentationScale, trs);
    }
    else if (Type == ITEM_BOX_OF_LUCK && (Level == 14 || Level == 15))
    {
        RenderObjectScreen(MODEL_EVENT + 5, Level, excellentFlags, ancientDiscriminator, Position,
                           Success, PickUp, presentationScale, trs);
    }
    else if (Type == ITEM_LOCHS_FEATHER && Level == 1)
    {
        RenderObjectScreen(MODEL_EVENT + 16, Level, excellentFlags, ancientDiscriminator, Position,
                           Success, PickUp, presentationScale, trs);
    }
    else if (Type == ITEM_ALE && Level == 1)
    {
        RenderObjectScreen(MODEL_EVENT + 7, Level, excellentFlags, ancientDiscriminator, Position,
                           Success, PickUp, presentationScale, trs);
    }
    else if (Type == ITEM_POTION + 21)
    {
        switch (Level)
        {
        case 1:
            RenderObjectScreen(MODEL_EVENT + 11, Level, excellentFlags, ancientDiscriminator,
                               Position, Success, PickUp, presentationScale, trs);
            break;
        case 2:
            RenderObjectScreen(MODEL_EVENT + 11, Level, excellentFlags, ancientDiscriminator,
                               Position, Success, PickUp, presentationScale, trs);
            break;
        case 3:
            RenderObjectScreen(Type + MODEL_ITEM, Level, excellentFlags, ancientDiscriminator,
                               Position, Success, PickUp, presentationScale, trs);
            break;
        default:
            RenderObjectScreen(Type + MODEL_ITEM, Level, excellentFlags, ancientDiscriminator,
                               Position, Success, PickUp, presentationScale, trs);
            break;
        }
    }
    else if (Type == ITEM_PUMPKIN_OF_LUCK)
    {
        RenderObjectScreen(MODEL_PUMPKIN_OF_LUCK, Level, excellentFlags, ancientDiscriminator,
                           Position, Success, PickUp, presentationScale, trs);
    }
    else if (Type >= ITEM_JACK_OLANTERN_BLESSINGS && Type <= ITEM_JACK_OLANTERN_CRY)
    {
        RenderObjectScreen(MODEL_JACK_OLANTERN_BLESSINGS, Level, excellentFlags,
                           ancientDiscriminator, Position, Success, PickUp, presentationScale, trs);
    }
    else if (Type == ITEM_JACK_OLANTERN_FOOD)
    {
        RenderObjectScreen(MODEL_JACK_OLANTERN_FOOD, Level, excellentFlags, ancientDiscriminator,
                           Position, Success, PickUp, presentationScale, trs);
    }
    else if (Type == ITEM_JACK_OLANTERN_DRINK)
    {
        RenderObjectScreen(MODEL_JACK_OLANTERN_DRINK, Level, excellentFlags, ancientDiscriminator,
                           Position, Success, PickUp, presentationScale, trs);
    }
    else if (Type == ITEM_PINK_CHOCOLATE_BOX)
    {
        switch (Level)
        {
        case 0:
            RenderObjectScreen(MODEL_PINK_CHOCOLATE_BOX, Level, excellentFlags,
                               ancientDiscriminator, Position, Success, PickUp, presentationScale,
                               trs);
            break;
        case 1:
            RenderObjectScreen(MODEL_EVENT + 21, Level, excellentFlags, ancientDiscriminator,
                               Position, Success, PickUp, presentationScale, trs);
            break;
        }
    }
    else if (Type == ITEM_RED_CHOCOLATE_BOX)
    {
        switch (Level)
        {
        case 0:
            RenderObjectScreen(MODEL_RED_CHOCOLATE_BOX, Level, excellentFlags, ancientDiscriminator,
                               Position, Success, PickUp, presentationScale, trs);
            break;
        case 1:
            RenderObjectScreen(MODEL_EVENT + 22, Level, excellentFlags, ancientDiscriminator,
                               Position, Success, PickUp, presentationScale, trs);
            break;
        }
    }
    else if (Type == ITEM_BLUE_CHOCOLATE_BOX)
    {
        switch (Level)
        {
        case 0:
            RenderObjectScreen(MODEL_BLUE_CHOCOLATE_BOX, Level, excellentFlags,
                               ancientDiscriminator, Position, Success, PickUp, presentationScale,
                               trs);
            break;
        case 1:
            RenderObjectScreen(MODEL_EVENT + 23, Level, excellentFlags, ancientDiscriminator,
                               Position, Success, PickUp, presentationScale, trs);
            break;
        }
    }
    else if (Type == ITEM_WEAPON_OF_ARCHANGEL)
    {
        switch (Level)
        {
        case 0:
            RenderObjectScreen(MODEL_DIVINE_STAFF_OF_ARCHANGEL, -1, excellentFlags,
                               ancientDiscriminator, Position, Success, PickUp, presentationScale,
                               trs);
            break;
        case 1:
            RenderObjectScreen(MODEL_DIVINE_SWORD_OF_ARCHANGEL, -1, excellentFlags,
                               ancientDiscriminator, Position, Success, PickUp, presentationScale,
                               trs);
            break;
        case 2:
            RenderObjectScreen(MODEL_DIVINE_CB_OF_ARCHANGEL, -1, excellentFlags,
                               ancientDiscriminator, Position, Success, PickUp, presentationScale,
                               trs);
            break;
        }
    }
    else if (Type == ITEM_SCROLL_OF_EMPEROR_RING_OF_HONOR)
    {
        switch (Level)
        {
        case 0:
            RenderObjectScreen(Type + MODEL_ITEM, Level, excellentFlags, ancientDiscriminator,
                               Position, Success, PickUp, presentationScale, trs);
            break;
        case 1:
            RenderObjectScreen(MODEL_EVENT + 12, -1, excellentFlags, ancientDiscriminator, Position,
                               Success, PickUp, presentationScale, trs);
            break;
        }
    }
    else if (Type == ITEM_BROKEN_SWORD_DARK_STONE)
    {
        switch (Level)
        {
        case 0:
            RenderObjectScreen(Type + MODEL_ITEM, Level, excellentFlags, ancientDiscriminator,
                               Position, Success, PickUp, presentationScale, trs);
            break;
        case 1:
            RenderObjectScreen(MODEL_EVENT + 13, -1, excellentFlags, ancientDiscriminator, Position,
                               Success, PickUp, presentationScale, trs);
            break;
        }
    }
    else if (Type == ITEM_WIZARDS_RING)
    {
        switch (Level)
        {
        case 0:
            RenderObjectScreen(MODEL_EVENT + 15, Level, excellentFlags, ancientDiscriminator,
                               Position, Success, PickUp, presentationScale, trs);
            break;
        case 1:
        case 2:
        case 3:
            RenderObjectScreen(MODEL_EVENT + 14, Level, excellentFlags, ancientDiscriminator,
                               Position, Success, PickUp, presentationScale, trs);
            break;
        }
    }
    else if (Type == ITEM_LIFE_STONE_ITEM && Level == 1)
    {
        RenderObjectScreen(MODEL_EVENT + 18, Level, excellentFlags, ancientDiscriminator, Position,
                           Success, PickUp, presentationScale, trs);
    }
    else if (Type == ITEM_POTION + 100)
    {
        bool _Angle;
        if (g_pLuckyCoinRegistration->GetItemRotation())
        {
            _Angle = true;
        }
        else
        {
            _Angle = Success;
        }

        RenderObjectScreen(MODEL_POTION + 100, Level, excellentFlags, ancientDiscriminator,
                           Position, _Angle, PickUp, presentationScale, trs);
    }
    else if (Type == ITEM_SACRED_ARMOR)
    {
        RenderObjectScreen(MODEL_ARMORINVEN_60, Level, excellentFlags, ancientDiscriminator,
                           Position, Success, PickUp, presentationScale, trs);
    }
    else if (Type == ITEM_STORM_HARD_ARMOR)
    {
        RenderObjectScreen(MODEL_ARMORINVEN_61, Level, excellentFlags, ancientDiscriminator,
                           Position, Success, PickUp, presentationScale, trs);
    }
    else if (Type == ITEM_PIERCING_ARMOR)
    {
        RenderObjectScreen(MODEL_ARMORINVEN_62, Level, excellentFlags, ancientDiscriminator,
                           Position, Success, PickUp, presentationScale, trs);
    }
    else if (Type == ITEM_PHOENIX_SOUL_ARMOR)
    {
        RenderObjectScreen(MODEL_ARMORINVEN_74, Level, excellentFlags, ancientDiscriminator,
                           Position, Success, PickUp, presentationScale, trs);
    }
    else
    {
        RenderObjectScreen(Type + MODEL_ITEM, Level, excellentFlags, ancientDiscriminator, Position,
                           Success, PickUp, presentationScale, trs);
    }
}

void SessionRenderUnit::RenderEqiupmentBox()
{
    int StartX = InventoryStartX;
    int StartY = InventoryStartY;
    float x, y, Width, Height;

    EnableAlphaTest();

    //helper
    Width = 40.f;
    Height = 40.f;
    x = 15.f;
    y = 46.f;
    InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_HELPER]);
    RenderBitmap(BITMAP_INVENTORY + 15, x + StartX, y + StartY, Width, Height, 0.f, 0.f,
                 Width / 64.f, Height / 64.f);
    //wing
    Width = 60.f;
    Height = 40.f;
    x = 115.f;
    y = 46.f;
    InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_WING]);
    RenderBitmap(BITMAP_INVENTORY + 14, x + StartX, y + StartY, Width, Height, 0.f, 0.f,
                 Width / 64.f, Height / 64.f);
    if (gCharacterManager.GetBaseClass(CharacterAttribute->Class) != CLASS_DARK)
    {
        //helmet
        Width = 40.f;
        Height = 40.f;
        x = 75.f;
        y = 46.f;
        InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_HELM]);
        RenderBitmap(BITMAP_INVENTORY + 3, x + StartX, y + StartY, Width, Height, 0.f, 0.f,
                     Width / 64.f, Height / 64.f);
    }
    //armor upper
    Width = 40.f;
    Height = 60.f;
    x = 75.f;
    y = 89.f;
    InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_ARMOR]);
    RenderBitmap(BITMAP_INVENTORY + 4, x + StartX, y + StartY, Width, Height, 0.f, 0.f,
                 Width / 64.f, Height / 64.f);
    //armor lower
    //if(GetBaseClass(CharacterAttribute->Class) != CLASS_ELF)
    {
        Width = 40.f;
        Height = 40.f;
        x = 75.f;
        y = 152.f;
        InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_PANTS]);
        RenderBitmap(BITMAP_INVENTORY + 5, x + StartX, y + StartY, Width, Height, 0.f, 0.f,
                     Width / 64.f, Height / 64.f);
    }
    //weapon right
    Width = 40.f;
    Height = 60.f;
    x = 15.f;
    y = 89.f;
    InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT]);
    RenderBitmap(BITMAP_INVENTORY + 6, x + StartX, y + StartY, Width, Height, 0.f, 0.f,
                 Width / 64.f, Height / 64.f);
    //weapon left
    Width = 40.f;
    Height = 60.f;
    x = 134.f;
    y = 89.f;
    InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT]);
    RenderBitmap(BITMAP_INVENTORY + 16, x + StartX, y + StartY, Width, Height, 0.f, 0.f,
                 Width / 64.f, Height / 64.f);
    //glove
    if (gCharacterManager.GetBaseClass(CharacterAttribute->Class) != CLASS_RAGEFIGHTER)
    {
        Width = 40.f;
        Height = 40.f;
        x = 15.f;
        y = 152.f;
        InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_GLOVES]);
        RenderBitmap(BITMAP_INVENTORY + 7, x + StartX, y + StartY, Width, Height, 0.f, 0.f,
                     Width / 64.f, Height / 64.f);
    }

    //boot
    Width = 40.f;
    Height = 40.f;
    x = 134.f;
    y = 152.f;
    InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_BOOTS]);
    RenderBitmap(BITMAP_INVENTORY + 8, x + StartX, y + StartY, Width, Height, 0.f, 0.f,
                 Width / 64.f, Height / 64.f);
    //necklace
    Width = 20.f;
    Height = 20.f;
    x = 55.f;
    y = 89.f;
    InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_AMULET]);
    RenderBitmap(BITMAP_INVENTORY + 9, x + StartX, y + StartY, Width, Height, 0.f, 0.f,
                 Width / 32.f, Height / 32.f);
    //ring
    Width = 20.f;
    Height = 20.f;
    x = 55.f;
    y = 152.f;
    InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_RING_RIGHT]);
    RenderBitmap(BITMAP_INVENTORY + 10, x + StartX, y + StartY, Width, Height, 0.f, 0.f,
                 Width / 32.f, Height / 32.f);
    //ring
    Width = 20.f;
    Height = 20.f;
    x = 115.f;
    y = 152.f;
    InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_RING_LEFT]);
    RenderBitmap(BITMAP_INVENTORY + 10, x + StartX, y + StartY, Width, Height, 0.f, 0.f,
                 Width / 32.f, Height / 32.f);
}

void SessionRenderUnit::RenderEqiupmentPart3D(int Index, float sx, float sy, float Width,
                                              float Height)
{
    ITEM *p = &CharacterMachine->Equipment[Index];
    if (p->Type != -1)
    {
        if (p->Number > 0)
            RenderItem3D(sx, sy, Width, Height, p->Type, p->Level, p->ExcellentFlags,
                         p->AncientDiscriminator, false);
    }
}

void SessionRenderUnit::RenderEqiupment3D()
{
    int StartX = InventoryStartX;
    int StartY = InventoryStartY;
    float x, y, Width, Height;

    //helper
    Width = 40.f;
    Height = 40.f;
    x = 15.f;
    y = 46.f;
    InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_HELPER]);
    RenderEqiupmentPart3D(EQUIPMENT_HELPER, StartX + x, StartY + y, Width, Height);
    //wing
    Width = 60.f;
    Height = 40.f;
    x = 115.f;
    y = 46.f;
    InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_WING]);
    RenderEqiupmentPart3D(EQUIPMENT_WING, StartX + x, StartY + y, Width, Height);
    //helmet
    Width = 40.f;
    Height = 40.f;
    x = 75.f;
    y = 46.f;
    InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_HELM]);
    RenderEqiupmentPart3D(EQUIPMENT_HELM, StartX + x, StartY + y, Width, Height);
    //armor upper
    Width = 40.f;
    Height = 60.f;
    x = 75.f;
    y = 89.f;
    InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_ARMOR]);
    RenderEqiupmentPart3D(EQUIPMENT_ARMOR, StartX + x, StartY + y - 10, Width, Height);
    //armor lower
    //if(GetBaseClass(CharacterAttribute->Class) != CLASS_ELF)
    {
        Width = 40.f;
        Height = 40.f;
        x = 75.f;
        y = 152.f;
        InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_PANTS]);
        RenderEqiupmentPart3D(EQUIPMENT_PANTS, StartX + x, StartY + y, Width, Height);
    }
    //weapon right
    Width = 40.f;
    Height = 60.f;
    x = 15.f;
    y = 89.f;
    InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT]);
    RenderEqiupmentPart3D(EQUIPMENT_WEAPON_RIGHT, StartX + x, StartY + y, Width, Height);
    //weapon left
    Width = 40.f;
    Height = 60.f;
    x = 134.f;
    y = 89.f;
    InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT]);
    RenderEqiupmentPart3D(EQUIPMENT_WEAPON_LEFT, StartX + x, StartY + y, Width, Height);
    //glove
    Width = 40.f;
    Height = 40.f;
    x = 15.f;
    y = 152.f;
    InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_GLOVES]);
    RenderEqiupmentPart3D(EQUIPMENT_GLOVES, StartX + x, StartY + y, Width, Height);
    //boot
    Width = 40.f;
    Height = 40.f;
    x = 134.f;
    y = 152.f;
    InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_BOOTS]);
    RenderEqiupmentPart3D(EQUIPMENT_BOOTS, StartX + x, StartY + y, Width, Height);
    //necklace
    Width = 20.f;
    Height = 20.f;
    x = 55.f;
    y = 89.f;
    InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_AMULET]);
    RenderEqiupmentPart3D(EQUIPMENT_AMULET, StartX + x, StartY + y, Width, Height);
    //ring
    Width = 20.f;
    Height = 20.f;
    x = 55.f;
    y = 152.f;
    InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_RING_RIGHT]);
    RenderEqiupmentPart3D(EQUIPMENT_RING_RIGHT, StartX + x, StartY + y, Width, Height);
    //ring
    Width = 20.f;
    Height = 20.f;
    x = 115.f;
    y = 152.f;
    InventoryColor(&CharacterMachine->Equipment[EQUIPMENT_RING_LEFT]);
    RenderEqiupmentPart3D(EQUIPMENT_RING_LEFT, StartX + x, StartY + y, Width, Height);
}
#define NUM_LINE_CMB (7)

// OMF-00541
// OMF-00542

void SessionRenderUnit::RenderCharacter_AfterImage(const CharacterDrawInput &character,
                                                   const PART_t *part, bool translate, int select,
                                                   float firstInterval, float secondInterval)
{
    const auto &input = character.object;
    const float keys = Models[input.type].Actions[input.action].NumAnimationKeys;
    vec3_t displacement;
    VectorSubtract(input.position, input.source->StartPosition, displacement);
    for (const auto [interval, alpha] :
         {std::pair{firstInterval, 0.3f}, std::pair{secondInterval, 0.5f}})
    {
        auto ghost = input;
        ghost.animationFrame -= interval;
        if (ghost.animationFrame <= 0.f)
            continue;
        ghost.alpha = alpha;
        VectorScale(displacement, ghost.animationFrame / keys, ghost.position);
        VectorAdd(input.source->StartPosition, ghost.position, ghost.position);
        RenderCharacterPartPose(ghost, character.light, *part, translate, select);
    }
    auto main = input;
    RenderCharacterPartPose(main, character.light, *part, translate, select);
}

void SessionRenderUnit::RenderCharacterPartPose(ObjectDrawInput &draw, const vec3_t light,
                                                const PART_t &part, bool translate, int select)
{
    if (!Calc_ObjectAnimation(draw, translate, select))
        return;
    RenderPartObject(draw, part.Type, &part, light, draw.alpha, part.Level, part.ExcellentFlags,
                     part.AncientDiscriminator, false, false, translate, select);
}

void SessionRenderUnit::RenderCharacterDarkside(const CharacterDrawInput &character,
                                                const PART_t *part, bool translate, int select)
{
    const auto *visual = FindCharacterVisual(*character.source);
    if (visual)
    {
        for (const auto &pose : visual->darksidePoses)
        {
            auto ghost = character.object;
            VectorCopy(pose.position, ghost.position);
            VectorCopy(pose.angle, ghost.angle);
            ghost.animationFrame = pose.animationFrame;
            ghost.alpha = pose.alpha;
            ghost.action = PLAYER_SKILL_DARKSIDE_ATTACK;
            RenderCharacterPartPose(ghost, character.light, *part, translate, select);
        }
    }
    auto main = character.object;
    RenderCharacterPartPose(main, character.light, *part, translate, select);
}

void SessionRenderUnit::RenderObject_AfterCharacter(const ObjectDrawInput &input, bool translate,
                                                    int select, int extraMonster)
{
    auto draw = input;
    if (!Calc_RenderObject(draw, translate, select, extraMonster))
        return;
    Draw_RenderObject_AfterCharacter(draw, translate, select, extraMonster);
}

void SessionRenderUnit::RenderObjects_AfterCharacter()
{
    const auto *definition = sessionKeeper_.WorldContextDefinition();
    if (!definition || !definition->presentation.objectsAfterCharacters)
        return;
    auto &maps = TheMapProcess();
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j)
        {
            auto &block = ObjectBlock[i * 16 + j];
            const bool blockVisible = TestFrustrum2D(float(i * 16 + 8), float(j * 16 + 8), -180.f);
            maps.RenderEarlyAfterCharacterObjects(block.Head);
            if (!blockVisible)
                continue;
            for (OBJECT *object = block.Head; object; object = object->Next)
            {
                if (!object->Live || !object->m_bRenderAfterCharacter)
                    continue;
                const bool objectVisible =
                    TestFrustrum2D(object->Position[0] * 0.01f, object->Position[1] * 0.01f,
                                   object->CollisionRange);
                if (objectVisible && !maps.IsEarlyAfterCharacterObject(*object))
                    RenderObject_AfterCharacter(object);
            }
        }
}

// Helper: Handle item falling animation

// Helper: Handle item on ground (set angle and camera rotation)

// Render dropped items and fall animation and camera rotation for items on the ground
void SessionRenderUnit::RenderPartObjectBody(BMD *b, const ObjectDrawInput &input, int Type,
                                             float Alpha, int RenderType)
{
    auto draw = input;
    const auto *o = draw.source;
    int nIndex;
    int nNum;

    nIndex = int((draw.type + 1 - MODEL_ITEM) / 512.0f);
    nNum = (draw.type - MODEL_ITEM) % 512;

    BOOL bIsNotRendered = FALSE;

    if ((Type == MODEL_STORM_CROW_ARMOR || Type == MODEL_STORM_CROW_GLOVES ||
         Type == MODEL_STORM_CROW_PANTS || Type == MODEL_STORM_CROW_BOOTS))
    {
        b->RenderBody(RENDER_TEXTURE | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, draw.hiddenMesh, BITMAP_CHROME + 1);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (Type == MODEL_THUNDER_HAWK_ARMOR || Type == MODEL_THUNDER_HAWK_GLOVES ||
             Type == MODEL_THUNDER_HAWK_PANTS || Type == MODEL_THUNDER_HAWK_BOOTS)
    {
        vec3_t Light;
        VectorCopy(b->BodyLight, Light);
        Vector(0.85f * Light[0], 0.85f * Light[1], 1.2f * Light[2], b->BodyLight);
        b->RenderBody(RENDER_TEXTURE | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, draw.hiddenMesh, BITMAP_CHROME + 1);
        VectorCopy(Light, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (Type == MODEL_WINGS_OF_DARKNESS)
    {
        Vector(0.8f, 0.6f, 1.f, b->BodyLight);
        b->RenderBody(RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, 0.5f, draw.blendU,
                      draw.blendV, draw.hiddenMesh, BITMAP_CHROME + 1);
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (Type == MODEL_WING_OF_STORM)
    {
        Vector(1.f, 0.7f, 0.5f, b->BodyLight);
        glColor3fv(b->BodyLight);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);

        objectTextureAnimation += FPS_ANIMATION_FACTOR;
        if (objectTextureAnimation > 15)
            objectTextureAnimation = 0;
        float fU = ((int)objectTextureAnimation / 4) * 0.25f;
        Vector(0.9f, 0.6f, 0.3f, b->BodyLight);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, draw.blendLight, fU,
                      draw.blendV, draw.hiddenMesh);
        Vector(1.f, 1.f, 1.f, b->BodyLight);
    }
    // 	else if( Type==MODEL_WING+37 )	// 시공날개(법사)
    //     {
    // 		Vector(1.f,1.f,1.f,b->BodyLight);
    // 		b->RenderBody(RENDER_TEXTURE,draw.alpha,draw.blendMesh,draw.blendLight,draw.blendU,draw.blendV,draw.hiddenMesh);
    //     }
    else if (Type == MODEL_WING_OF_RUIN)
    {
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        glColor3fv(b->BodyLight);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        float Luminosity = absf(sinf(WorldTime * 0.001f)) * 0.3f;
        Vector(0.1f + Luminosity, 0.1f + Luminosity, 0.1f + Luminosity, b->BodyLight);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                      draw.blendU, draw.blendV);
        Luminosity = absf(sinf(WorldTime * 0.001f)) * 0.8f;
        Vector(0.0f + Luminosity, 0.0f + Luminosity, 0.0f + Luminosity, b->BodyLight);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, draw.blendLight,
                      draw.blendU, draw.blendV, BITMAP_3RDWING_LAYER);
    }
    else if (Type == MODEL_CAPE_OF_EMPEROR)
    {
        if (b->BodyLight[0] == 1 && b->BodyLight[1] == 1 && b->BodyLight[2] == 1)
        {
            Vector(1.f, 1.f, 1.f, b->BodyLight);
            glColor3fv(b->BodyLight);
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
        else
        {
            Vector(1.f, 1.f, 1.f, b->BodyLight);
            glColor3fv(b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
    }
    else if (Type == MODEL_WINGS_OF_DESPAIR)
    {
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        glColor3fv(b->BodyLight);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME6, draw.alpha,
                      draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_WING_OF_DIMENSION)
    {
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        glColor3fv(b->BodyLight);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_DIVINE_SWORD_OF_ARCHANGEL)
    {
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_METAL, Alpha, 0, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_LIGHTMAP | RENDER_TEXTURE, Alpha, 0, draw.blendLight, draw.blendU,
                      WorldTime * 0.0001f, BITMAP_CHROME);
        b->RenderMesh(1, RENDER_TEXTURE, Alpha, 0, draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_DIVINE_STAFF_OF_ARCHANGEL)
    {
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_METAL, Alpha, 0, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_LIGHTMAP | RENDER_TEXTURE, 1.f, 0, draw.blendLight, draw.blendU,
                      -WorldTime * 0.0001f, BITMAP_CHROME);
        b->RenderMesh(1, RENDER_TEXTURE, Alpha, -1, draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_DIVINE_CB_OF_ARCHANGEL)
    {
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_METAL, Alpha, 0, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_LIGHTMAP | RENDER_TEXTURE, 1.f, 0, draw.blendLight, draw.blendU,
                      -WorldTime * 0.0001f, BITMAP_CHROME);
        b->RenderMesh(1, RENDER_TEXTURE, Alpha, -1, draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_DIVINE_SCEPTER_OF_ARCHANGEL)
    {
        b->RenderMesh(0, RENDER_TEXTURE, Alpha, -1, draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(0, RENDER_LIGHTMAP | RENDER_TEXTURE, Alpha, 0, draw.blendLight, draw.blendU,
                      -WorldTime * 0.0001f, BITMAP_CHROME);
    }
    else if (Type == MODEL_GREAT_REIGN_CROSSBOW)
    {
        vec3_t Light;
        VectorCopy(b->BodyLight, Light);
        Vector(0.8f * Light[0], 0.f, 0.8f * Light[2], b->BodyLight);
        b->RenderMesh(0, RENDER_LIGHTMAP | RENDER_TEXTURE, 1.f, 0, draw.blendLight,
                      -WorldTime * 0.0002f, draw.blendV, BITMAP_CHROME);
        VectorCopy(Light, b->BodyLight);
        b->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, 1.f, 0, draw.blendLight, draw.blendU,
                      draw.blendV, BITMAP_CHROME + 1);
        VectorCopy(Light, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (Type == MODEL_PUMPKIN_OF_LUCK)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_DARK | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      -WorldTime * 0.0002f, draw.blendV, BITMAP_CHROME);

        vec3_t vPos, vRelativePos;
        Vector(0.f, 0.f, 0.f, vRelativePos);
        b->TransformPosition(BoneTransform[8], vRelativePos, vPos, true);
        float fLumi = (sinf(WorldTime * 0.004f) + 1.0f) * 0.05f;
        Vector(0.8f + fLumi, 0.8f + fLumi, 0.3f + fLumi, draw.light);
        CreateSprite(BITMAP_LIGHT, vPos, 1.5f, draw.light, o, 0.5f);
        b->TransformPosition(BoneTransform[10], vRelativePos, vPos, true);
        CreateSprite(BITMAP_LIGHT, vPos, 0.5f, draw.light, o, 0.5f);
        b->TransformPosition(BoneTransform[11], vRelativePos, vPos, true);
        CreateSprite(BITMAP_LIGHT, vPos, 0.5f, draw.light, o, 0.5f);
    }
    else if (draw.type >= MODEL_CHAIN_LIGHTNING_PARCHMENT &&
             draw.type <= MODEL_INNOVATION_PARCHMENT)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.0f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, draw.hiddenMesh, BITMAP_ROOLOFPAPER_EFFECT_R);
    }
    else if (Type == MODEL_RUNE_BLADE && !(RenderType & RENDER_DOPPELGANGER))
    {
        vec3_t Light;
        VectorCopy(b->BodyLight, Light);
        b->BeginRender(1.f);
        glColor3f(b->BodyLight[0], b->BodyLight[1], b->BodyLight[2]);
        b->RenderMesh(3, RENDER_TEXTURE, 1.f, -1, draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, 1.f, -1, draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, sinf(WorldTime * 0.01f), 1, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_CHROME | RENDER_TEXTURE, 1.f, 0, draw.blendLight, draw.blendU,
                      WorldTime * 0.001f, BITMAP_CHROME);

        float Luminosity = sinf(WorldTime * 0.001f) * 0.5f + 0.5f;
        Vector(Light[0] * Luminosity, Light[0] * Luminosity, Light[0] * Luminosity, b->BodyLight);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, 1.f, 2, draw.blendLight,
                      WorldTime * 0.0001f, -WorldTime * 0.0005f);
        b->EndRender();
    }
    else if (Type == MODEL_DRAGON_SPEAR)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE, 1.f, 1, draw.blendLight, WorldTime * 0.0001f,
                      WorldTime * 0.0005f);
    }
    else if (Type == MODEL_ELEMENTAL_MACE)
    {
        vec3_t Light;
        VectorCopy(b->BodyLight, Light);
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      draw.hiddenMesh);

        float time = WorldTime * 0.001f;
        float Luminosity = sinf(time) * 0.5f + 0.3f;
        Vector(Light[0] * Luminosity, Light[0] * Luminosity, Light[0] * Luminosity, b->BodyLight);
        b->RenderMesh(2, RENDER_TEXTURE, 1.f, 2, draw.blendLight, time, -WorldTime * 0.0005f);
    }
    else if (Type == MODEL_DARK_HORSE_ITEM)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        Vector(0.8f, 0.4f, 0.1f, b->BodyLight);
        b->RenderBody(RENDER_BRIGHT | RENDER_CHROME, Alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_DARK_RAVEN_ITEM)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        Vector(0.3f, 0.8f, 1.f, b->BodyLight);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, Alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_BATTLE_SCEPTER)
    {
        draw.blendLight = sinf(WorldTime * 0.001f) * 0.6f + 0.4f;
        b->BeginRender(1.f);
        b->RenderBody(RENDER_TEXTURE, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE, Alpha, 0, draw.blendLight, draw.blendU, draw.blendV,
                      draw.hiddenMesh);
        b->EndRender();
    }
    else if (Type == MODEL_MASTER_SCEPTER)
    {
        b->RenderBody(RENDER_TEXTURE, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        Vector(0.1f, 0.3f, 1.f, b->BodyLight);
        draw.blendMesh = 0;
        draw.blendLight = sinf(WorldTime * 0.001f) * 0.6f + 0.4f;
        b->RenderMesh(0, RENDER_TEXTURE, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        Vector(0.6f, 0.8f, 1.f, b->BodyLight);
        draw.blendMesh = 1;
        draw.blendLight = 1.f;
        b->RenderMesh(1, RENDER_TEXTURE, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      WorldTime * 0.0003f, BITMAP_CHROME);
    }
    else if (Type == MODEL_FLAMBERGE)
    {
        //b->RenderBody( RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV, 5 );

        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        draw.blendMesh = 1;
        Vector(1.f, 0.f, 0.2f, b->BodyLight);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(4, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        float fV;
        fV = (((int)(WorldTime * 0.05) % 16) / 4) * 0.25f;
        b->RenderMesh(5, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 5, draw.blendLight,
                      draw.blendU, fV);
    }
    else if (Type == MODEL_SWORD_BREAKER)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_IMPERIAL_SWORD)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        draw.blendMesh = 1;
        draw.blendLight = sinf(WorldTime * 0.001f) * 0.6f + 0.4f;
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_FROST_MACE)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      1);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_DEADLY_STAFF)
    {
        b->RenderBody(RENDER_TEXTURE, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        draw.blendMesh = 1;
        draw.blendLight = 1.f;
        Vector(1.f, 0.5f, 0.5f, b->BodyLight);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_IMPERIAL_STAFF)
    {
        b->RenderBody(RENDER_TEXTURE, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        draw.blendMesh = 0;
        draw.blendLight = fabs(sinf(WorldTime * 0.001f)) + 0.1f;
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, BITMAP_SOCKETSTAFF);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, BITMAP_SOCKETSTAFF);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, BITMAP_SOCKETSTAFF);
    }
    else if (Type == MODEL_STAFF + 32)
    {
        b->RenderBody(RENDER_TEXTURE, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_CRIMSONGLORY)
    {
        b->RenderBody(RENDER_TEXTURE, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_SALAMANDER_SHIELD)
    {
        b->RenderBody(RENDER_TEXTURE, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, 1);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_FROST_BARRIER)
    {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        draw.blendMesh = 2;
        draw.blendLight = absf((sinf(WorldTime * 0.001f)));
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_GUARDIAN_SHILED)
    {
        b->RenderBody(RENDER_TEXTURE, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_CROSS_SHIELD)
    {
        glColor3f(1.f, 1.f, 1.f);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->StreamMesh = 1;
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, -(int)WorldTime % 2000 * 0.0005f);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type >= MODEL_POTION + 55 && Type <= MODEL_POTION + 57)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderBody(RENDER_BRIGHT | RENDER_CHROME2, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, draw.hiddenMesh);
    }
    else if (draw.type == MODEL_OLD_SCROLL)
    {
        float sine = float(sinf(WorldTime * 0.002f) * 0.3f) + 0.7f;
        b->RenderBody(RenderType, 0.7f, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      1);
        b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, 1.0f, 4, sine, draw.blendU, draw.blendV, 0);
    }
    else if (draw.type == MODEL_ILLUSION_SORCERER_COVENANT)
    {
        float sine = float(sinf(WorldTime * 0.00004f) * 0.15f) + 0.5f;
        b->RenderBody(RenderType, 1.f, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      0);
        b->RenderBody(RenderType, 1.f, 0.5f, sine, draw.blendU, draw.blendV, 1);
    }
    else if (draw.type == MODEL_SCROLL_OF_BLOOD)
    {
        float sine = float(sinf(WorldTime * 0.002f) * 0.3f) + 0.7f;
        b->RenderBody(RenderType, 0.7f, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      0);
        b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, 1.0f, 0, sine, draw.blendU, draw.blendV, 1);
    }
    else if (Type == MODEL_POTION + 64)
    {
        RenderPotionGlow(*b, draw, RenderType, Alpha);
    }
    else if (draw.type == MODEL_FLAME_OF_CONDOR)
    {
        b->RenderBody(RENDER_TEXTURE, 0.9, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderBody(RENDER_BRIGHT | RENDER_CHROME, Alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
    }
    else if (draw.type == MODEL_FEATHER_OF_CONDOR)
    {
        b->RenderBody(RENDER_TEXTURE, 0.9, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (draw.type == MODEL_FLAME_OF_DEATH_BEAM_KNIGHT)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.5f) * 0.4f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_ITEM_EFFECT_DBSTONE_R);
    }
    else if (draw.type == MODEL_HORN_OF_HELL_MAINE)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.5f) * 0.4f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_ITEM_EFFECT_HELLHORN_R);
    }
    else if (draw.type == MODEL_FEATHER_OF_DARK_PHOENIX)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.5f) * 0.4f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_ITEM_EFFECT_PFEATHER_R);
    }
    else if (draw.type == MODEL_EYE_OF_ABYSSAL)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.5f) * 0.4f;
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, Alpha, 1, fLumi, draw.blendU, draw.blendV,
                      BITMAP_ITEM_EFFECT_DEYE_R);
        b->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, 0.2f, -1, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type >= MODEL_HELPER + 46 && Type <= MODEL_HELPER + 48)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.2f) * 0.3f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_FREETICKET_R);
    }
    else if (Type == MODEL_POTION + 54)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.2f) * 0.4f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, 1.0f, 0, draw.blendLight, draw.blendU,
                      draw.blendV, 1);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_CHAOSCARD_R);
    }
    else if (Type == MODEL_POTION + 58)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_RAREITEM1_R);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_POTION + 59)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_RAREITEM2_R);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_POTION + 60)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_RAREITEM3_R);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_POTION + 61)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_RAREITEM4_R);
    }
    else if (Type == MODEL_POTION + 62)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_RAREITEM5_R);
    }
    else if (draw.type == MODEL_POTION + 53)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.5f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_LUCKY_CHARM_EFFECT53);
    }
    else if (draw.type == MODEL_HELPER + 43 || draw.type == MODEL_HELPER + 93)
    {
        float fLumi = (sinf(WorldTime * 0.001f) + 1.5f) * 0.25f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_LUCKY_SEAL_EFFECT43);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (draw.type == MODEL_HELPER + 44 || draw.type == MODEL_HELPER + 94 ||
             draw.type == MODEL_HELPER + 116)
    {
        float fLumi = (sinf(WorldTime * 0.001f) + 1.5f) * 0.25f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_LUCKY_SEAL_EFFECT44);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (draw.type == MODEL_HELPER + 45)
    {
        float fLumi = (sinf(WorldTime * 0.001f) + 1.5f) * 0.25f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_LUCKY_SEAL_EFFECT45);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (draw.type >= MODEL_POTION + 70 && draw.type <= MODEL_POTION + 71)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_CHROME4, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (draw.type >= MODEL_POTION + 72 && draw.type <= MODEL_POTION + 77)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (draw.type == MODEL_HELPER + 59)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (draw.type >= MODEL_HELPER + 54 && draw.type <= MODEL_HELPER + 58)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (draw.type >= MODEL_POTION + 78 && draw.type <= MODEL_POTION + 82)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (draw.type == MODEL_HELPER + 60)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (draw.type == MODEL_HELPER + 61)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.2f) * 0.3f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_FREETICKET_R);
    }
    else if (draw.type == MODEL_POTION + 83)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_RAREITEM5_R);
    }
    else if (draw.type == MODEL_POTION + 145)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_RAREITEM7);
    }
    else if (draw.type == MODEL_POTION + 146)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_RAREITEM8);
    }
    else if (draw.type == MODEL_POTION + 147)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_RAREITEM9);
    }
    else if (draw.type == MODEL_POTION + 148)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_RAREITEM10);
    }
    else if (draw.type == MODEL_POTION + 149)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_RAREITEM11);
    }
    else if (draw.type == MODEL_POTION + 150)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_RAREITEM12);
    }
    else if (Type == MODEL_HELPER + 125)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.2f) * 0.3f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_DOPPLEGANGGER_FREETICKET);
    }
    else if (Type == MODEL_HELPER + 126)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.2f) * 0.3f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_BARCA_FREETICKET);
    }
    else if (Type == MODEL_HELPER + 127)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.2f) * 0.3f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_BARCA7TH_FREETICKET);
    }
    else if (Type == MODEL_POTION + 91)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.2f) * 0.4f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, 1.0f, 0, draw.blendLight, draw.blendU,
                      draw.blendV, 1);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_CHARACTERCARD_R);
    }
    else if (Type == MODEL_POTION + 92)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.2f) * 0.4f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, 1.0f, 0, draw.blendLight, draw.blendU,
                      draw.blendV, 1);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_NEWCHAOSCARD_GOLD_R);
    }
    else if (Type == MODEL_POTION + 93)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.2f) * 0.4f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, 1.0f, 0, draw.blendLight, draw.blendU,
                      draw.blendV, 1);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_NEWCHAOSCARD_RARE_R);
    }
    else if (Type == MODEL_POTION + 95)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.2f) * 0.4f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, 1.0f, 0, draw.blendLight, draw.blendU,
                      draw.blendV, 1);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_NEWCHAOSCARD_MINI_R);
    }
    else if (Type == MODEL_POTION + 94)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_CHERRY_BLOSSOM_PLAYBOX)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_CHERRY_BLOSSOM_WINE)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_CHERRY_BLOSSOM_RICE_CAKE)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_CHERRY_BLOSSOM_FLOWER_PETAL)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_POTION + 88)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_POTION + 89)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_GOLDEN_CHERRY_BLOSSOM_BRANCH)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type >= MODEL_HELPER + 62 && Type <= MODEL_HELPER + 63)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (Type >= MODEL_POTION + 97 && Type <= MODEL_POTION + 98)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (Type == MODEL_POTION + 96)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.5f) * 0.5f;

        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (Type == MODEL_DEMON)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (Type == MODEL_SPIRIT_OF_GUARDIAN)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (Type == MODEL_GREAT_SCEPTER && !(RenderType & RENDER_DOPPELGANGER))
    {
        float Luminosity = WorldTime * 0.0005f;

        draw.hiddenMesh = 2;
        draw.blendMesh = 2;
        draw.blendLight = 1.f;
        b->BeginRender(1.f);
        b->RenderBody(RENDER_TEXTURE, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(2, RENDER_TEXTURE, Alpha, draw.blendMesh, draw.blendLight, Luminosity,
                      Luminosity);
        b->EndRender();
    }
    else if (Type == MODEL_LORD_SCEPTER)
    {
        draw.blendLight = 1.f;
        draw.blendU = WorldTime * 0.0008f;
        b->RenderBody(RENDER_TEXTURE, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        draw.blendLight = sinf(WorldTime * 0.001f) * 0.3f + 0.7f;
        b->RenderMesh(1, RENDER_BRIGHT | RENDER_CHROME, Alpha, 0, draw.blendLight, 0.f,
                      draw.blendV);
    }
    else if (Type == MODEL_KNIGHT_BLADE)
    {
        b->RenderBody(RENDER_TEXTURE, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);

        float Luminosity = sinf(WorldTime * 0.0008f) * 0.7f + 0.5f;
        b->RenderMesh(2, RENDER_TEXTURE, Alpha, 2, Luminosity, draw.blendU, draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, Alpha, 1, draw.blendLight, draw.blendU, draw.blendV);
        //. 날
        glColor3f(0.43f, 0.14f, 0.6f);

        b->RenderMesh(3, RENDER_BRIGHT | RENDER_CHROME, Alpha, 3, draw.blendLight,
                      WorldTime * 0.0001f, WorldTime * 0.0005f);
        glColor3f(1.f, 1.f, 1.f);
    }
    else if (Type == MODEL_DARK_REIGN_BLADE)
    {
        b->RenderBody(RENDER_TEXTURE, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE, 1.f, 0, draw.blendLight, WorldTime * 0.0005f,
                      WorldTime * 0.0005f);
        draw.hiddenMesh = 0;
        b->StreamMesh = 1;
        b->RenderMesh(1, RENDER_TEXTURE, 1.f, -1, draw.blendLight, draw.blendU,
                      WorldTime * 0.0005f);
    }
    else if (Type == MODEL_HURRICANE_ARMOR || Type == MODEL_HURRICANE_GLOVES ||
             Type == MODEL_HURRICANE_PANTS || Type == MODEL_HURRICANE_BOOTS)
    {
        float Luminosity = sinf(WorldTime * 0.002f) * 0.3f + 0.5f;
        vec3_t Light;
        VectorCopy(b->BodyLight, Light);
        Vector(Light[0] * 0.3f, Light[1] * 0.8f, Light[1] * 1.f, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, draw.hiddenMesh, BITMAP_CHROME + 1);
        VectorCopy(Light, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (Type == MODEL_SYLPH_WIND_BOW || Type == MODEL_GRAND_VIPER_STAFF ||
             Type == MODEL_SOLEIL_SCEPTER || Type == MODEL_BONE_BLADE)
    {
        b->BeginRender(1.0f);

        b->StreamMesh = 0;
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        if (Type == MODEL_BONE_BLADE)
        {
            b->BodyLight[0] = 1.0f;
            b->BodyLight[1] = 0.7f;
            b->BodyLight[2] = 0.4f;
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, (int)WorldTime % 4000 * 0.0004f - 0.7f, draw.blendV,
                          BITMAP_CHROME7);
            b->BodyLight[0] = 0.7f;
            b->BodyLight[1] = 0.7f;
            b->BodyLight[2] = 0.7f;
        }
        else if (Type == MODEL_GRAND_VIPER_STAFF)
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, (int)WorldTime % 2000 * 0.0002f - 0.3f,
                          (int)WorldTime % 2000 * 0.0002f - 0.3f, BITMAP_CHROME_ENERGY);
        else
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, (int)WorldTime % 4000 * 0.0002f - 0.3f,
                          (int)WorldTime % 4000 * 0.0002f - 0.3f, BITMAP_CHROME6);
        b->StreamMesh = -1;

        b->EndRender();
    }
    else if (Type == MODEL_EXPLOSION_BLADE)
    {
        b->RenderBody(RENDER_TEXTURE, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, 0, draw.blendLight, draw.blendU, draw.blendU);
        float Luminosity = sinf(WorldTime * 0.0015f) * 0.03f + 0.3f;
        Vector(Luminosity, Luminosity, Luminosity + 0.1f, b->BodyLight);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, -2, draw.blendLight, draw.blendU, draw.blendU);

        draw.alpha = 0.5f;
        float Luminosity4 = sinf(WorldTime * 0.0025f) * 0.5f + 0.7f;
        Vector(0.4f, 0.4f, 0.8f, b->BodyLight);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, -2, draw.blendLight, draw.blendU, draw.blendU);
    }
    else if (Type == MODEL_SYLPHID_RAY_ARMOR)
    {
        if (b->HideSkin == true)
        {
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
        else
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
    }
    else if (Type == MODEL_SYLPHID_RAY_PANTS)
    {
        if (b->HideSkin == true)
        {
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
        else
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
    }
    else if (Type == MODEL_SWORD_DANCER)
    {
        draw.hiddenMesh = 1;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE, 0.5f, 0, draw.blendLight, draw.blendU, draw.blendV,
                      BITMAP_LAVA);
        b->RenderMesh(1, RENDER_TEXTURE, 0.7f, 1, draw.blendLight, draw.blendU,
                      WorldTime * 0.0009f);
    }
    else if (Type == MODEL_SHINING_SCEPTER)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_ALBATROSS_BOW)
    {
        draw.hiddenMesh = 1;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE, 1.0f, 1, draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, 1.0f, -1, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME5, 0.5f, -1, draw.blendLight,
                      draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_PLATINA_STAFF)
    {
        draw.hiddenMesh = 1;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, 1.0f, 1, draw.blendLight,
                      WorldTime * 0.0009f, WorldTime * 0.0009f);
    }
    else if (Type == MODEL_IRIS_ARMOR)
    {
        if (b->HideSkin == true)
        {
            b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
        else
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
    }
    else if (Type == MODEL_IRIS_PANTS)
    {
        if (b->HideSkin == true)
        {
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
        else
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
    }
    else if (Type == MODEL_IRIS_GLOVES)
    {
        if (b->HideSkin == true)
        {
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
        else
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
    }
    else if (MODEL_MISTERY_HELM <= Type && MODEL_LILIUM_HELM >= Type &&
             !(RenderType & RENDER_DOPPELGANGER))
    {
        if (b->HideSkin)
        {
            glColor3fv(b->BodyLight);
            int anMesh[6] = {2, 1, 0, 2, 1, 2};
            b->RenderMesh(anMesh[Type - (MODEL_MISTERY_HELM)], RENDER_TEXTURE, draw.alpha,
                          draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        }
        else
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
    }
    else if (MODEL_MISTERY_ARMOR <= Type && MODEL_LILIUM_ARMOR >= Type &&
             !(RenderType & RENDER_DOPPELGANGER))
    {
        if (b->HideSkin)
        {
            glColor3fv(b->BodyLight);
            int nTexture = Type - (MODEL_MISTERY_ARMOR);
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV, BITMAP_INVEN_ARMOR + nTexture);
            for (int i = 1; i < b->NumMeshs; ++i)
                b->RenderMesh(i, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
        }
        else
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
    }
    else if (MODEL_MISTERY_PANTS <= Type && MODEL_LILIUM_PANTS >= Type &&
             !(RenderType & RENDER_DOPPELGANGER))
    {
        if (b->HideSkin)
        {
            glColor3fv(b->BodyLight);
            int nTexture = Type - (MODEL_MISTERY_PANTS);
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV, BITMAP_INVEN_PANTS + nTexture);
            for (int i = 1; i < b->NumMeshs; ++i)
                b->RenderMesh(i, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
        }
        else
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
    }
    else if (MODEL_LILIUM_GLOVES == Type)
    {
        if (b->HideSkin)
        {
            glColor3fv(b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
        else
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
    }
#ifdef PBG_ADD_CHARACTERCARD
    else if (MODEL_HELPER + 97 == Type || MODEL_HELPER + 98 == Type || MODEL_POTION + 91 == Type)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.2f) * 0.4f;
        int _R_Type = 0;
        switch (Type)
        {
        case MODEL_HELPER + 97:
            _R_Type = BITMAP_CHARACTERCARD_R_MA;
            break;
        case MODEL_HELPER + 98:
            _R_Type = BITMAP_CHARACTERCARD_R_DA;
            break;
        case MODEL_POTION + 91:
            _R_Type = BITMAP_CHARACTERCARD_R;
            break;
        default:
            break;
        }
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, _R_Type);
    }
#endif //PBG_ADD_CHARACTERCARD
#ifdef PBG_ADD_CHARACTERSLOT
    else if (MODEL_HELPER + 99 == Type)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, 0, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
#endif //PBG_ADD_CHARACTERSLOT
    else
    {
        bIsNotRendered = TRUE;
    }
    if (bIsNotRendered == FALSE)
        ;
    else if (Type == MODEL_FAITH_ARMOR || Type == MODEL_FAITH_PANTS || Type == MODEL_ARMOR + 53 ||
             Type == MODEL_PANTS + 53)
    {
        int nTexture = 0;
        switch (Type)
        {
        case MODEL_FAITH_ARMOR:
            nTexture = BITMAP_SKIN_ARMOR_DEVINE;
            break;
        case MODEL_FAITH_PANTS:
            nTexture = BITMAP_SKIN_PANTS_DEVINE;
            break;
        case MODEL_ARMOR + 53:
            nTexture = BITMAP_SKIN_ARMOR_SUCCUBUS;
            break;
        case MODEL_PANTS + 53:
            nTexture = BITMAP_SKIN_PANTS_SUCCUBUS;
            break;
        }
        if (b->HideSkin)
        {
            glColor3fv(b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV, nTexture);
            for (int i = 1; i < b->NumMeshs; ++i)
            {
                b->RenderMesh(i, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
            }
        }
        else
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
    }
    else if (Type == MODEL_SERAPHIM_HELM)
    {
        if (b->HideSkin == true)
        {
            glColor3fv(b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
        else
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
    }
    else if (Type == MODEL_SERAPHIM_ARMOR)
    {
        if (b->HideSkin == true)
        {
            glColor3fv(b->BodyLight);
            b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
        else
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
    }
    else if (Type == MODEL_FAITH_HELM)
    {
        if (b->HideSkin == true)
        {
            glColor3fv(b->BodyLight);
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
        else
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
    }
    else if (Type == MODEL_HELM + 53)
    {
        if (b->HideSkin == true)
        {
            glColor3fv(b->BodyLight);
            b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
        else
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
    }
    else if (Type == MODEL_BEUROBA)
    {
        glColor3fv(b->BodyLight);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(4, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(5, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, 0.4f, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, 0.8f, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_STRYKER_SCEPTER)
    {
        glColor3fv(b->BodyLight);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(4, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(5, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(6, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, 0.5f, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_ARROW_VIPER_BOW)
    {
        float Luminosity = sinf(WorldTime * 0.002f) * 0.3f + 0.5f;
        b->BeginRender(1.f);
        glColor3f(b->BodyLight[0], b->BodyLight[1], b->BodyLight[2]);
        b->RenderMesh(0, RENDER_TEXTURE, 1.f, -1, draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, 1.f, -1, draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, 1.f, -1, draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(6, RENDER_TEXTURE, 1.f, -1, draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(3, RENDER_TEXTURE, 1.f, 3, Luminosity, draw.blendU, draw.blendV);
        b->RenderMesh(4, RENDER_TEXTURE, 1.f, 4, Luminosity, draw.blendU, draw.blendV);
        glColor3f(1.f, 1.f, 1.f);
        b->RenderMesh(5, RENDER_TEXTURE, 1.f, -1, draw.blendLight, draw.blendU, draw.blendV);

        b->EndRender();
    }
    else if (Type == MODEL_LOST_MAP)
    {
        glColor3f(1.f, 1.f, 1.f);
        Models[draw.type].StreamMesh = 1;
        b->RenderMesh(1, RENDER_TEXTURE, 1.f, -1, draw.blendLight, draw.blendU,
                      WorldTime * 0.0005f);
        Models[draw.type].StreamMesh = -1;
        b->RenderMesh(0, RENDER_TEXTURE, 1.f, -1, draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_SYMBOL_OF_KUNDUN)
    {
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        b->StreamMesh = 1;
        b->RenderMesh(1, RENDER_TEXTURE, 1.f, -1, draw.blendLight, draw.blendU,
                      WorldTime * 0.0005f);
        b->StreamMesh = -1;

        Vector(1.f, 0.5f, 0.f, b->BodyLight);
        if (objectMeshLight > 1.f)
        {
            objectMeshLight = 1.00f;
            objectMeshLightDelta = -0.01f;
        }
        if (objectMeshLight < 0.01f)
        {
            objectMeshLight = 0.01f;
            objectMeshLightDelta = 0.01f;
        }
        b->RenderMesh(2, RENDER_TEXTURE, 1.0f, 2, objectMeshLight, draw.blendU, draw.blendV);
        objectMeshLight += objectMeshLightDelta;

        glColor3f(1.f, 1.f, 1.f);
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        b->RenderMesh(0, RENDER_TEXTURE, 1.f, -1, draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, 0.3f, -1, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_STAFF_OF_KUNDUN)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, Alpha, 1, 0.2f, draw.blendU, draw.blendV);

        glColor3f(1.f, 1.f, 1.f);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, Alpha, 0, sinf(WorldTime * 0.005f),
                      draw.blendU, draw.blendV);
        b->RenderMesh(0, RENDER_CHROME4 | RENDER_BRIGHT, Alpha, 0, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_DEMONIC_STICK)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, Alpha, 1, 1.f, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_STORM_BLITZ_STICK)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        float fLumi = (sinf(WorldTime * 0.002f) + 0.5f) * 0.5f;
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, Alpha, 1, fLumi, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_GREAT_LORD_SCEPTER)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, Alpha, 1, 1.f, draw.blendU, draw.blendV);
        b->RenderMesh(3, RENDER_TEXTURE | RENDER_BRIGHT, Alpha, 3, 1.f, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_GRAND_SOUL_SHIELD && !(RenderType & RENDER_DOPPELGANGER))
    {
        b->BeginRender(1.f);

        vec3_t Light;
        VectorCopy(b->BodyLight, Light);
        Vector(Light[0] * 0.3f, Light[1] * 0.3f, Light[2] * 0.3f, b->BodyLight);
        glColor3f(b->BodyLight[0], b->BodyLight[1], b->BodyLight[2]);
        b->RenderMesh(2, RENDER_COLOR, 1.f, -1, draw.blendLight, draw.blendU, draw.blendV);

        VectorCopy(Light, b->BodyLight);
        glColor3f(b->BodyLight[0], b->BodyLight[1], b->BodyLight[2]);
        b->RenderMesh(2, RENDER_CHROME | RENDER_BRIGHT, 1.f, 2, draw.blendLight, draw.blendU,
                      WorldTime * 0.01f, BITMAP_CHROME);
        b->RenderMesh(0, RENDER_TEXTURE, 1.f, -1, draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, 1.f, 1, draw.blendLight, draw.materialJitterU,
                      draw.materialJitterV);

        float Luminosity = sinf(WorldTime * 0.001f) * 0.4f + 0.6f;
        Vector(Light[0] * Luminosity, Light[0] * Luminosity, Light[0] * Luminosity, b->BodyLight);
        glColor3f(b->BodyLight[0], b->BodyLight[1], b->BodyLight[2]);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, 1.f, 2, draw.blendLight,
                      WorldTime * 0.0001f, WorldTime * 0.0005f);
        b->EndRender();
    }
    else if (Type == MODEL_ELEMENTAL_SHIELD)
    {
        b->BeginRender(1.f);
        glColor4f(b->BodyLight[0], b->BodyLight[1], b->BodyLight[2], 0.8f);
        b->RenderMesh(1, RENDER_TEXTURE, 0.8f, -1, draw.blendLight, draw.blendU, draw.blendV);
        glColor4f(b->BodyLight[0], b->BodyLight[1], b->BodyLight[2], 0.5f);
        b->RenderMesh(3, RENDER_TEXTURE, 0.5f, -1, draw.blendLight, draw.blendU, draw.blendV);

        glColor3f(b->BodyLight[0], b->BodyLight[1], b->BodyLight[2]);
        b->RenderMesh(0, RENDER_TEXTURE, 1.f, -1, draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, 1.f, 2, draw.blendLight, WorldTime * 0.0005f, draw.blendV);
        b->RenderMesh(3, RENDER_TEXTURE, 1.f, 3, draw.blendLight, draw.materialJitterU,
                      draw.materialJitterV);
        b->EndRender();
    }
    else if (Type == MODEL_BATTLE_BOW && (RenderType & RENDER_EXTRA))
    {
        RenderType -= RENDER_EXTRA;
        Vector(0.1f, 0.1f, 0.1f, b->BodyLight);
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_ANGEL && b->NumMeshs)
    {
        b->RenderBody(RENDER_TEXTURE, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        if (RenderType & RENDER_CHROME)
        {
            Vector(0.75f, 0.55f, 0.5f, b->BodyLight);
            b->RenderMesh(0, RENDER_CHROME4 | RENDER_BRIGHT, Alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            Vector(1.f, 1.f, 1.f, b->BodyLight);
            b->RenderMesh(0, RENDER_CHROME | RENDER_TEXTURE | RENDER_BRIGHT, Alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
        }
    }
    else if (Type == MODEL_SIEGE_POTION)
    {
        b->BeginRender(1.f);
        if (draw.hiddenMesh == 1)
        {
            glColor3f(1.f, 1.f, 1.f);
            Vector(1.f, 1.f, 1.f, b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE, 1.f, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV);
            Vector(0.1f, 0.5f, 1.f, b->BodyLight);
            b->RenderMesh(0, RENDER_METAL | RENDER_BRIGHT, 1.f, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, 1.f, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
        else if (draw.hiddenMesh == 0)
        {
            glColor3f(1.f, 1.f, 1.f);
            Vector(1.f, 1.f, 1.f, b->BodyLight);
            b->RenderMesh(1, RENDER_TEXTURE, 1.f, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV);
            Vector(0.1f, 0.5f, 1.f, b->BodyLight);
            b->RenderMesh(1, RENDER_METAL | RENDER_BRIGHT, 1.f, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, 1.f, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
        b->EndRender();
    }
    else if (Type == MODEL_HELPER + 7)
    {
        b->BeginRender(1.f);
        if (draw.hiddenMesh == 1)
        {
            glColor3f(1.f, 1.f, 1.f);
            Vector(1.f, 1.f, 1.f, b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE, 1.f, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV);
        }
        else if (draw.hiddenMesh == 0)
        {
            glColor3f(1.f, 1.f, 1.f);
            Vector(1.f, 1.f, 1.f, b->BodyLight);
            b->RenderMesh(1, RENDER_TEXTURE, 1.f, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV);
        }
        b->EndRender();
    }
    else if (Type == MODEL_LIFE_STONE_ITEM)
    {
        b->BeginRender(1.f);
        glColor3f(1.f, 1.f, 1.f);
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        b->RenderMesh(0, RENDER_TEXTURE, 1.f, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        Vector(0.f, 0.5f, 1.f, b->BodyLight);
        b->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, 1.f, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
        b->EndRender();
    }
    else if (Type == MODEL_BOLT || Type == MODEL_ARROWS)
    {
        if (g_isCharacterBuff(o, eBuff_InfinityArrow))
        {
            Vector(1.f, 0.8f, 0.2f, b->BodyLight);
            b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV);
            b->RenderBody(RENDER_CHROME | RENDER_BRIGHT, Alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
        else
        {
            b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV);
        }
    }
    else if (o->m_bpcroom == TRUE &&
             (Type == MODEL_PLATE_HELM || Type == MODEL_PLATE_ARMOR || Type == MODEL_PLATE_PANTS ||
              Type == MODEL_PLATE_GLOVES || Type == MODEL_PLATE_BOOTS))
    {
        if (Type == MODEL_PLATE_ARMOR)
        {
            vec3_t EndRelative, EndPos;
            Vector(0.f, 0.f, 0.f, EndRelative);

            b->TransformPosition(draw.bones[0], EndRelative, EndPos, true);

            Vector(0.4f, 0.6f, 0.8f, draw.light);
            CreateSprite(BITMAP_LIGHT, EndPos, 6.0f, draw.light, o, 0.5f);

            float Luminosity;
            Luminosity = sinf(WorldTime * 0.05f) * 0.4f + 0.9f;
            Vector(Luminosity * 0.3f, Luminosity * 0.5f, Luminosity * 0.8f, draw.light);
            CreateSprite(BITMAP_LIGHT, EndPos, 2.0f, draw.light, o);
        }

        vec3_t Light;
        VectorCopy(b->BodyLight, Light);
        Vector(0.9f, 0.7f, 1.0f, b->BodyLight);

        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, 1, draw.blendLight, draw.blendU,
                      draw.blendV, BITMAP_FENRIR_THUNDER);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_METAL, draw.alpha, 1, draw.blendLight, draw.blendU,
                      draw.blendV, BITMAP_FENRIR_THUNDER);
        VectorCopy(Light, b->BodyLight);
    }
    else if (Type == MODEL_JEWEL_OF_HARMONY || Type == MODEL_LOWER_REFINE_STONE ||
             Type == MODEL_HIGHER_REFINE_STONE)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type >= MODEL_SEED_FIRE && Type <= MODEL_SEED_EARTH)
    {
        int iCategoryIndex = Type - (MODEL_SEED_FIRE) + 1;
        switch (iCategoryIndex)
        {
        case 1: // 0~9
            Vector(0.9f, 0.1f, 0.2f, b->BodyLight);
            break;
        case 2: // 10~15
            Vector(0.4f, 0.5f, 1.0f, b->BodyLight);
            break;
        case 3: // 16~20
            Vector(1.0f, 1.0f, 1.0f, b->BodyLight);
            break;
        case 4: // 21~28
            Vector(0.4f, 1.0f, 0.6f, b->BodyLight);
            break;
        case 5: // 29~33
            Vector(1.0f, 0.8f, 0.4f, b->BodyLight);
            break;
        case 6: // 34~40
            Vector(1.0f, 0.4f, 1.0f, b->BodyLight);
            break;
        }
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderBody(RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, draw.hiddenMesh);
    }
    else if (Type >= MODEL_SEED_SPHERE_FIRE_1 && Type <= MODEL_SEED_SPHERE_EARTH_5)
    {
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        int iCategoryIndex = (Type - (MODEL_SEED_SPHERE_FIRE_1)) % 6 + 1;
        switch (iCategoryIndex)
        {
        case 1: // 0~9
            Vector(0.9f, 0.1f, 0.2f, b->BodyLight);
            break;
        case 2: // 10~15
            Vector(0.4f, 0.5f, 1.0f, b->BodyLight);
            break;
        case 3: // 16~20
            Vector(1.0f, 1.0f, 1.0f, b->BodyLight);
            break;
        case 4: // 21~28
            Vector(0.4f, 1.0f, 0.6f, b->BodyLight);
            break;
        case 5: // 29~33
            Vector(1.0f, 0.8f, 0.4f, b->BodyLight);
            break;
        case 6: // 34~40
            Vector(1.0f, 0.4f, 1.0f, b->BodyLight);
            break;
        }
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, 1);
        b->RenderBody(RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, 1);
    }
    else if (Type == MODEL_HELPER + 71 || Type == MODEL_HELPER + 72 || Type == MODEL_HELPER + 73 ||
             Type == MODEL_HELPER + 74 || Type == MODEL_HELPER + 75)
    {
        int _angle = int(b->BodyAngle[1]) % 360;
        float _meshLight1;
        if (0 < _angle && _angle <= 180)
        {
            _meshLight1 = 0.2f - (sinf(Q_PI * (_angle) / 180.0f) * 0.2f);
        }
        else
        {
            _meshLight1 = 0.2f;
        }
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, 0.35f, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, _meshLight1, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, 2, _meshLight1, draw.blendU, draw.blendV);
    }
    else if (o->Kind == KIND_PLAYER && draw.type == MODEL_PLAYER &&
             o->SubType == MODEL_SKELETON_PCBANG)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
        vec3_t Light;
        VectorCopy(b->BodyLight, Light);
        Vector(0.9f, 0.8f, 1.0f, b->BodyLight);
        b->RenderBody(RENDER_BRIGHT | RENDER_CHROME, Alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, BITMAP_FENRIR_THUNDER);
        b->RenderBody(RENDER_BRIGHT | RENDER_METAL, Alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, BITMAP_FENRIR_THUNDER);
        VectorCopy(Light, b->BodyLight);
    }
    else if (o->Kind == KIND_PLAYER && draw.type == MODEL_PLAYER &&
             o->SubType == MODEL_CURSEDTEMPLE_ALLIED_PLAYER)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      1);

        if (g_isCharacterBuff(o, eDeBuff_CursedTempleRestraint) ||
            g_isCharacterBuff(o, eBuff_CursedTempleProdection))
        {
            vec3_t vRelativePos, vtaWorldPos, Light;
            Vector(0.4f, 0.4f, 0.8f, Light);
            Vector(0.f, 0.f, 0.f, vRelativePos);

            if (g_isCharacterBuff(o, eDeBuff_CursedTempleRestraint))
            {
                float Luminosity = sinf(WorldTime * 0.002f) * 0.3f + 0.5f;
                b->RenderMesh(1, RENDER_TEXTURE, 0.4f + Luminosity, 0, draw.blendLight * Luminosity,
                              -WorldTime * 0.0005f, WorldTime * 0.0005f);
            }
        }

        if (recordingCharacter_)
            RenderCharacterCloth(*drawingCharacter_);
    }
    else if (o->Kind == KIND_PLAYER && draw.type == MODEL_PLAYER &&
             o->SubType == MODEL_CURSEDTEMPLE_ILLUSION_PLAYER)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      2);

        if (g_isCharacterBuff(o, eDeBuff_CursedTempleRestraint) ||
            g_isCharacterBuff(o, eBuff_CursedTempleProdection))
        {
            vec3_t vRelativePos, vtaWorldPos, Light;
            Vector(0.4f, 0.4f, 0.8f, Light);
            Vector(0.f, 0.f, 0.f, vRelativePos);

            if (g_isCharacterBuff(o, eDeBuff_CursedTempleRestraint))
            {
                float Luminosity = sinf(WorldTime * 0.002f) * 0.3f + 0.5f;
                b->RenderMesh(2, RENDER_TEXTURE, 0.4f + Luminosity, 0, draw.blendLight * Luminosity,
                              -WorldTime * 0.0005f, WorldTime * 0.0005f);
            }
        }

        if (recordingCharacter_)
            RenderCharacterCloth(*drawingCharacter_);
    }
    else if (o->Kind == KIND_PLAYER && draw.type == MODEL_PLAYER && o->SubType == MODEL_HALLOWEEN)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);

        if (draw.action == PLAYER_JACK_1 || draw.action == PLAYER_JACK_2)
        {
            vec3_t light;
            VectorCopy(b->BodyLight, light);
            const float stageGreen = (std::max)(0.2f, 0.8f - o->m_iAnimation * (0.8f / 40.f));
            const float stageBlue = (std::max)(0.1f, 0.8f - o->m_iAnimation * (0.9f / 40.f));
            Vector(1.f, stageGreen, stageBlue, b->BodyLight);
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            VectorCopy(light, b->BodyLight);
        }

        if (recordingCharacter_)
            RenderCharacterCloth(*drawingCharacter_);
    }
    else if (o->Kind == KIND_PLAYER && draw.type == MODEL_PLAYER && o->SubType == MODEL_PANDA)
    {

        float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, fLumi, draw.blendU,
                      draw.blendV, BITMAP_PANDABODY_R);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);

        int iAnimationFrame = (int)draw.animationFrame;
    }
    else if (o->Kind == KIND_PLAYER && draw.type == MODEL_PLAYER &&
             o->SubType == MODEL_XMAS_EVENT_CHANGE_GIRL)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (o->SubType == MODEL_XMAS_EVENT_CHA_DEER)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        float fLumi = (sinf(WorldTime * 0.002f) + 1.f) * 0.5f;
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, 1, fLumi, draw.blendU, draw.blendV,
                      BITMAP_BLOOD + 1);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, 1, fLumi, draw.blendU, draw.blendV,
                      BITMAP_BLOOD + 1);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, 1, fLumi, draw.blendU, draw.blendV,
                      BITMAP_BLOOD + 1);
    }
    else if (o->Kind == KIND_PLAYER && draw.type == MODEL_PLAYER &&
             o->SubType == MODEL_GM_CHARACTER)
    {
        draw.hiddenMesh = 2;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_HELPER + 69)
    {
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        float Luminosity = (sinf(WorldTime * 0.003f) + 1) * 0.3f + 0.3f;
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, Luminosity, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (Type == MODEL_HELPER + 70)
    {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, draw.hiddenMesh);
    }
    else if (Type == MODEL_HELPER + 81)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_HELPER + 82)
    {
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
    }
    else if (Type == MODEL_HELPER + 66)
    {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME4, draw.alpha, 0, draw.blendLight,
                      draw.blendU, draw.blendV);

        float Luminosity = (sinf(WorldTime * 0.003f) + 1) * 0.3f + 0.6f;
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, Luminosity, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_POTION + 100)
    {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (draw.type >= static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_BEGIN &&
             draw.type <= static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_END)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (Type == MODEL_HELPER + 107)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, 0, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_HELPER + 104)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, 0.2f, 0, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_HELPER + 105)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, 0.2f, 0, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (draw.type >= MODEL_HELPER + 109 &&
             draw.type <= MODEL_HELPER +
                              112) // InGameShop 장착 아이템 : 반지 (사파이어, 루비, 토파즈, 자수정)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, 0, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (draw.type >= MODEL_HELPER + 113 &&
             draw.type <=
                 MODEL_HELPER + 115) // InGameShop 장착 아이템 : 목걸이 (사파이어, 루비, 에메랄드)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, 0, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (draw.type >= MODEL_POTION + 112 && draw.type <= MODEL_POTION + 113)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, 0, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (draw.type == MODEL_POTION + 120)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, 0, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (draw.type == MODEL_POTION + 121 || draw.type == MODEL_POTION + 122)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, 1, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (draw.type == MODEL_POTION + 123 || draw.type == MODEL_POTION + 124)
    {
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, 1, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (draw.type == MODEL_WING + 130)
    {
        if (b->BodyLight[0] == 1 && b->BodyLight[1] == 1 && b->BodyLight[2] == 1)
        {
            Vector(1.f, 1.f, 1.f, b->BodyLight);
            glColor3fv(b->BodyLight);
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
    }
    else if (draw.type == MODEL_POTION + 134)
    {
        //b->RenderBody(RENDER_TEXTURE,draw.alpha,draw.blendMesh,draw.blendLight, draw.blendU,draw.blendV,draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, -1, draw.blendLight, draw.blendU, draw.blendV,
                      BITMAP_PACKAGEBOX_RED);
    }
    else if (draw.type == MODEL_POTION + 135)
    {
        //b->RenderBody(RENDER_TEXTURE,draw.alpha,draw.blendMesh,draw.blendLight, draw.blendU,draw.blendV,draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, -1, draw.blendLight, draw.blendU, draw.blendV,
                      BITMAP_PACKAGEBOX_BLUE);
    }
    else if (draw.type == MODEL_POTION + 136)
    {
        //b->RenderBody(RENDER_TEXTURE,draw.alpha,draw.blendMesh,draw.blendLight, draw.blendU,draw.blendV,draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, -1, draw.blendLight, draw.blendU, draw.blendV,
                      BITMAP_PACKAGEBOX_GOLD);
    }
    else if (draw.type == MODEL_POTION + 137)
    {
        //b->RenderBody(RENDER_TEXTURE,draw.alpha,draw.blendMesh,draw.blendLight, draw.blendU,draw.blendV,draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, -1, draw.blendLight, draw.blendU, draw.blendV,
                      BITMAP_PACKAGEBOX_GREEN);
    }
    else if (draw.type == MODEL_POTION + 138)
    {
        //b->RenderBody(RENDER_TEXTURE,draw.alpha,draw.blendMesh,draw.blendLight, draw.blendU,draw.blendV,draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, -1, draw.blendLight, draw.blendU, draw.blendV,
                      BITMAP_PACKAGEBOX_PUPLE);
    }
    else if (draw.type == MODEL_POTION + 139)
    {
        //b->RenderBody(RENDER_TEXTURE,draw.alpha,draw.blendMesh,draw.blendLight, draw.blendU,draw.blendV,draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, -1, draw.blendLight, draw.blendU, draw.blendV,
                      BITMAP_PACKAGEBOX_SKY);
    }
    else if (draw.type >= MODEL_POTION + 114 && draw.type <= MODEL_POTION + 119)
    {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, -1, draw.blendLight, draw.blendU, draw.blendV,
                      BITMAP_INGAMESHOP_PRIMIUM6);
    }
    else if (draw.type >= MODEL_POTION + 126 && draw.type <= MODEL_POTION + 129)
    {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, -1, draw.blendLight, draw.blendU, draw.blendV,
                      BITMAP_INGAMESHOP_COMMUTERTICKET4);
    }
    else if (draw.type >= MODEL_POTION + 130 && draw.type <= MODEL_POTION + 132)
    {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, -1, draw.blendLight, draw.blendU, draw.blendV,
                      BITMAP_INGAMESHOP_SIZECOMMUTERTICKET3);
    }
    else if (draw.type == MODEL_HELPER + 121)
    {
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.2f) * 0.3f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, BITMAP_FREETICKET_R);
    }
    else if (Type >= MODEL_POTION + 141 && Type <= MODEL_POTION + 144)
    {
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, 0, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_SKELETON_TRANSFORMATION_RING)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderBody(RENDER_BRIGHT | RENDER_CHROME, 0.5f, 0, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (draw.type == MODEL_15GRADE_ARMOR_OBJ_ARMLEFT ||
             draw.type == MODEL_15GRADE_ARMOR_OBJ_ARMRIGHT ||
             draw.type == MODEL_15GRADE_ARMOR_OBJ_BODYLEFT ||
             draw.type == MODEL_15GRADE_ARMOR_OBJ_BODYRIGHT ||
             draw.type == MODEL_15GRADE_ARMOR_OBJ_BOOTLEFT ||
             draw.type == MODEL_15GRADE_ARMOR_OBJ_BOOTRIGHT ||
             draw.type == MODEL_15GRADE_ARMOR_OBJ_HEAD ||
             draw.type == MODEL_15GRADE_ARMOR_OBJ_PANTLEFT ||
             draw.type == MODEL_15GRADE_ARMOR_OBJ_PANTRIGHT)
    {
        float fLight, texCoordU;

        fLight = 0.8f - absf(sinf(WorldTime * 0.0018f) * 0.5f);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLight - 0.1f, draw.blendU,
                      draw.blendV);
        texCoordU = absf(sinf(WorldTime * 0.0005f));
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLight - 0.3f, texCoordU,
                      draw.blendV, BITMAP_RGB_MIX);
        b->RenderMesh(0, RENDER_TEXTURE | RENDER_CHROME4, draw.alpha, 0, draw.blendLight,
                      draw.blendU, draw.blendV);
    }

#ifdef LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM
    else if (Type == MODEL_HELPER + 128 || Type == MODEL_HELPER + 129)
    {
        float fEyeBlinkingTime = sinf(WorldTime * 0.005f) + 1;

        b->RenderBody(RENDER_TEXTURE, 1.0f, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, 0.3f, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, fEyeBlinkingTime,
                      draw.blendU, draw.blendV);
    }
#endif //LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM
#ifdef LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM_PART_2
    else if (Type >= MODEL_HELPER + 130 && Type <= MODEL_HELPER + 133)
    {
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        glColor3fv(b->BodyLight);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        float Luminosity = absf(sinf(WorldTime * 0.001f)) * 0.3f;
        Vector(0.1f + Luminosity, 0.1f + Luminosity, 0.1f + Luminosity, b->BodyLight);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                      draw.blendU, draw.blendV);
        Luminosity = absf(sinf(WorldTime * 0.001f)) * 0.8f;
        Vector(0.0f + Luminosity, 0.0f + Luminosity, 0.0f + Luminosity, b->BodyLight);

        switch (Type)
        {
        case MODEL_HELPER + 130:
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, draw.blendLight,
                          draw.blendU, draw.blendV, BITMAP_ORK_CHAM_LAYER_R);
            break;
        case MODEL_HELPER + 131:
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, draw.blendLight,
                          draw.blendU, draw.blendV, BITMAP_MAPLE_CHAM_LAYER_R);
            break;
        case MODEL_HELPER + 132:
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, draw.blendLight,
                          draw.blendU, draw.blendV, BITMAP_GOLDEN_ORK_CHAM_LAYER_R);
            break;
        case MODEL_HELPER + 133:
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, draw.blendLight,
                          draw.blendU, draw.blendV, BITMAP_GOLDEN_MAPLE_CHAM_LAYER_R);
            break;
        default:
            break;
        }
    }
#endif //LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM_PART_2

    else if (g_CMonkSystem.EqualItemModelType(Type) == MODEL_PHOENIX_SOUL_STAR) //ok
    {
        float fLumi = (sinf(WorldTime * 0.003) + 1.f) * 0.3f + 0.4f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_BRIGHT, draw.alpha * fLumi, 0, draw.blendLight * fLumi, draw.blendU,
                      draw.blendV, BITMAP_PHOENIXSOULWING);

        Vector(.15f, 1.f, .25f, b->BodyLight);
        glColor3fv(b->BodyLight);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        b->RenderMesh(1, RENDER_BRIGHT | RENDER_CHROME3, draw.alpha, 1, draw.blendLight,
                      draw.blendU, draw.blendV);
        Vector(1.f, 1.f, 1.f, b->BodyLight);
    }
    else if (Type == MODEL_SWORD_35_WING)
    {
        float fLumi = (sinf(WorldTime * 0.003) + 1.f) * 0.3f + 0.4f;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(0, RENDER_BRIGHT, draw.alpha * fLumi, 0, draw.blendLight * fLumi, draw.blendU,
                      draw.blendV, BITMAP_PHOENIXSOULWING);

        Vector(.15f, 1.f, .25f, b->BodyLight);
        glColor3fv(b->BodyLight);
        b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                      draw.blendLight, draw.blendU, draw.blendV);
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        b->RenderMesh(1, RENDER_CHROME3 | RENDER_BRIGHT, draw.alpha, 1, draw.blendLight,
                      draw.blendU, draw.blendV);
        Vector(1.f, 1.f, 1.f, b->BodyLight);
    }
    else if (Type == MODEL_PHOENIX_SOUL_HELMET)
    {
        float fLumi = (sinf(WorldTime * 0.003) + 1.f) * 0.3f + 0.4f;
        if (b->HideSkin == true)
        {
            glColor3fv(b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV, -1);
            b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV, -1);
            b->RenderMesh(2, RENDER_CHROME | RENDER_BRIGHT, draw.alpha * fLumi, 2,
                          draw.blendLight * fLumi, (double)(-int(WorldTime) % 1000) * 0.00009f,
                          draw.blendV, -1);
        }
        else
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV);
            b->RenderMesh(2, RENDER_CHROME | RENDER_BRIGHT, draw.alpha * fLumi, 2,
                          draw.blendLight * fLumi, (double)(-int(WorldTime) % 1000) * 0.00009f,
                          draw.blendV, -1);
        }
    }
    else if (Type == MODEL_PHOENIX_SOUL_ARMOR)
    {
        float fLumi = (sinf(WorldTime * 0.003) + 1.f) * 0.3f + 0.4f;
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, -1);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      (double)(-int(WorldTime) % 1000) * 0.00009f, -1);
        b->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, draw.alpha * fLumi, 1,
                      draw.blendLight * fLumi, (double)(-int(WorldTime) % 1000) * 0.00009f,
                      draw.blendV, -1);
        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, -1);
    }
    else if (Type == MODEL_ARMORINVEN_74)
    {
        vec3_t Light;
        VectorCopy(b->BodyLight, Light);
        glColor3fv(b->BodyLight);
        float fLumi = (sinf(WorldTime * 0.003) + 1.f) * 0.3f + 0.4f;
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, -1);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                      (double)(-int(WorldTime) % 1000) * 0.00009f, draw.blendV, -1);
        b->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, draw.alpha * fLumi, draw.blendMesh,
                      draw.blendLight * fLumi, (double)(-int(WorldTime) % 1000) * 0.00009f,
                      draw.blendV, -1);
        VectorCopy(Light, b->BodyLight);
    }
    else if (Type == MODEL_PHOENIX_SOUL_BOOTS)
    {
        glColor3fv(b->BodyLight);
        float fLumi = (sinf(WorldTime * 0.003) + 1.f) * 0.3f + 0.4f;
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, -1);
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                      (double)(-int(WorldTime) % 1000) * 0.00009f, draw.blendV, -1);
        b->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, draw.alpha * fLumi, 1,
                      draw.blendLight * fLumi, (double)(-int(WorldTime) % 1000) * 0.00009f,
                      draw.blendV, -1);
    }
    else if (Type == MODEL_CAPE_OF_FIGHTER || Type == MODEL_WING + 135)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_CAPE_OF_OVERRULE)
    {
        if (b->BodyLight[0] == 1 && b->BodyLight[1] == 1 && b->BodyLight[2] == 1)
        {
            Vector(1.f, 1.f, 1.f, b->BodyLight);
            glColor3fv(b->BodyLight);
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
        else
        {
            Vector(1.f, 1.f, 1.f, b->BodyLight);
            glColor3fv(b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
    }
    else if (Type == MODEL_SACRED_HELM || Type == MODEL_STORM_HARD_HELM)
    {
        if (b->HideSkin)
        {
            glColor3fv(b->BodyLight);
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
        else
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
    }
    else if (Type == MODEL_PIERCING_HELM)
    {
        if (b->HideSkin)
        {
            glColor3fv(b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
        else
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
    }
    else if ((g_CMonkSystem.EqualItemModelType(Type) == MODEL_SACRED_GLOVE) ||
             (g_CMonkSystem.EqualItemModelType(Type) == MODEL_STORM_HARD_GLOVE) ||
             (g_CMonkSystem.EqualItemModelType(Type) == MODEL_PIERCING_BLADE_GLOVE) ||
             (g_CMonkSystem.EqualItemModelType(Type) == MODEL_PHOENIX_SOUL_STAR))
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (Type >= MODEL_CHAIN_DRIVE_PARCHMENT && Type <= MODEL_INCREASE_BLOCK_PARCHMENT)
    {
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        float fLumi = (sinf(WorldTime * 0.0015f) + 1.0f) * 0.5f;
        b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                      draw.blendV, draw.hiddenMesh, BITMAP_ROOLOFPAPER_EFFECT_R);
    }
    /*
    else if (Type == MODEL_BODY_ARMOR + (MAX_CLASS * 2) + CLASS_RAGEFIGHTER || Type == MODEL_BODY_PANTS + (MAX_CLASS * 2) + CLASS_RAGEFIGHTER
        || Type == MODEL_BODY_GLOVES + (MAX_CLASS * 2) + CLASS_RAGEFIGHTER || Type == MODEL_BODY_BOOTS + (MAX_CLASS * 2) + CLASS_RAGEFIGHTER)
    {
        const char* pSkinTextureName = "LevelClass207_1";
        int nLen = strlen(pSkinTextureName);
        for (int i = 0; i < b->NumMeshs; ++i)
        {
            Texture_t* pTexture = &b->Textures[i];
            int SkinTexture = (!strnicmp(pTexture->FileName, pSkinTextureName, nLen)) ? BITMAP_SKIN + 14 : -1;
            if (SkinTexture != -1)
            {
                b->RenderMesh(i, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV, SkinTexture);
            }
        }
    }*/

    else if (Check_LuckyItem(Type, -MODEL_ITEM))
    {
        bool bHide = false;
        int nIndex = 0;
        if (Type == MODEL_HELM + 65)
            nIndex = 2;
        else if (Type == MODEL_HELM + 70)
            nIndex = 1;
        if (nIndex > 0)
            bHide = true;

        if (bHide)
        {
            b->RenderMesh(nIndex, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
        }
        else if (b->HideSkin)
        {
            if (Type == MODEL_ARMOR + 65)
                nIndex = BITMAP_INVEN_ARMOR + 6;
            else if (Type == MODEL_ARMOR + 70)
                nIndex = BITMAP_INVEN_ARMOR + 7;
            else if (Type == MODEL_PANTS + 65)
                nIndex = BITMAP_INVEN_PANTS + 6;
            else if (Type == MODEL_PANTS + 70)
                nIndex = BITMAP_INVEN_PANTS + 7;

            if (nIndex > 0)
            {
                b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, nIndex);
                for (int i = 1; i < b->NumMeshs; ++i)
                    b->RenderMesh(i, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV);
            }
            else
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
        }
        else
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
    }
    else
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
    }
}

void SessionRenderUnit::RenderPartObjectBodyColor(BMD *b, const ObjectDrawInput &draw, int Type,
                                                  float Alpha, int RenderType, float Bright,
                                                  int Texture, int iMonsterIndex)
{
    const int hiddenMesh = Type == MODEL_ELEMENTAL_MACE ? 2 : draw.hiddenMesh;
    if (Type >= MODEL_HELM_MONK && Type <= MODEL_BOOTS_MONK + MODEL_ITEM_COMMONCNT_RAGEFIGHTER)
        Type = g_CMonkSystem.OrginalTypeCommonItemMonk(Type);
    if ((RenderType & RENDER_LIGHTMAP) == RENDER_LIGHTMAP)
    {
        Vector(1.f, 1.f, 1.f, b->BodyLight);
    }
    else if (Type == MODEL_DRAKAN)
    {
        if (RenderType & RENDER_EXTRA)
        {
            RenderType -= RENDER_EXTRA;
            Vector(1.f, 0.1f, 0.1f, b->BodyLight);
        }
        else
        {
            Vector(0.2f, 0.2f, 0.8f, b->BodyLight);
            //Vector(1.0f,0.5f,0.0f,b->BodyLight);
        }
    }
    else if (iMonsterIndex >= 493 && iMonsterIndex <= 502)
    {
        if (iMonsterIndex == 495)
        {
            Vector(1.0f, 0.8f, 0.0f, b->BodyLight);
        }
        else if (iMonsterIndex == 496)
        {
            Vector(1.0f, 0.6f, 0.0f, b->BodyLight);
        }
        else if (iMonsterIndex == 499)
        {
            Vector(1.0f, 0.5f, 0.0f, b->BodyLight);
        }
        else if (iMonsterIndex == 501)
        {
            Vector(1.0f, 0.5f, 0.0f, b->BodyLight);
        }
        else
        {
            Vector(1.f, 0.6f, 0.1f, b->BodyLight);
        }
    }
    else
    {
        PartObjectColor(Type, Alpha, Bright, b->BodyLight,
                        (RenderType & RENDER_EXTRA) ? true : false);
    }

    if (Type == MODEL_LEGENDARY_STAFF)
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      1, Texture);
    else if (Type == MODEL_KNIGHT_BLADE)
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      1, Texture);
    else if (Type == MODEL_DARK_REIGN_BLADE)
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      0, Texture);
    else if (Type == MODEL_RUNE_BLADE || Type == MODEL_DRAGON_SPEAR ||
             Type == MODEL_ELEMENTAL_MACE || Type == MODEL_ELEMENTAL_SHIELD)
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      hiddenMesh, Texture);
    else if (Type >= MODEL_BATTLE_SCEPTER && Type <= MODEL_DIVINE_SCEPTER_OF_ARCHANGEL)
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      hiddenMesh, Texture);
    else if (Type == MODEL_FLAMBERGE)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      5);
    }
    else if (Type == MODEL_SWORD_BREAKER)
    {
        b->RenderMesh(0, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_IMPERIAL_SWORD)
    {
        b->RenderMesh(2, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_DEADLY_STAFF)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      -1, Texture);
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        b->RenderMesh(1, RenderType, draw.alpha, 1, draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_STAFF + 32)
    {
        b->RenderMesh(1, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_FROST_BARRIER)
    {
        b->RenderMesh(0, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_GUARDIAN_SHILED)
    {
        b->RenderMesh(0, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_SERAPHIM_HELM)
    {
        b->RenderMesh(0, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_FAITH_HELM)
    {
        b->RenderMesh(1, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_HELM + 53)
    {
        b->RenderMesh(2, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_CROSS_SHIELD)
    {
        b->RenderMesh(0, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_BEUROBA)
    {
        b->RenderMesh(0, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        b->RenderMesh(1, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_CHROMATIC_STAFF)
    {
        b->RenderMesh(0, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_RAVEN_STICK)
    {
        b->RenderMesh(0, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_STRYKER_SCEPTER)
    {
        b->RenderMesh(0, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_AIR_LYN_BOW)
    {
        b->RenderMesh(0, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type >= MODEL_SACRED_GLOVE && Type <= MODEL_PHOENIX_SOUL_STAR)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_SACRED_HELM || Type == MODEL_STORM_HARD_HELM)
    {
        glColor3fv(b->BodyLight);
        b->RenderMesh(1, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_PIERCING_HELM)
    {
        glColor3fv(b->BodyLight);
        b->RenderMesh(0, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_SACRED_ARMOR)
    {
        b->RenderMesh(1, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_STORM_HARD_ARMOR)
    {
        b->RenderMesh(1, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_PIERCING_ARMOR)
    {
        b->RenderMesh(0, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_PHOENIX_SOUL_HELMET)
    {
        b->RenderMesh(0, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_PHOENIX_SOUL_ARMOR)
    {
        b->RenderMesh(2, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_ARMORINVEN_74)
    {
        if (RenderType & RENDER_METAL)
        {
            b->RenderMesh(0, RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV);
            b->RenderMesh(0, RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV);
        }
        b->RenderMesh(0, RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_PHOENIX_SOUL_BOOTS)
    {
        b->RenderMesh(0, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      -1, Texture);
    }
}

void SessionRenderUnit::RenderPartObjectBodyColor2(BMD *b, const ObjectDrawInput &draw, int Type,
                                                   float Alpha, int RenderType, float Bright,
                                                   int Texture)
{
    const auto *o = draw.source;
    if (Type >= MODEL_HELM_MONK && Type <= MODEL_BOOTS_MONK + MODEL_ITEM_COMMONCNT_RAGEFIGHTER)
        Type = g_CMonkSystem.OrginalTypeCommonItemMonk(Type);

    if ((RenderType & RENDER_LIGHTMAP) == RENDER_LIGHTMAP)
    {
        Vector(1.f, 1.f, 1.f, b->BodyLight);
    }
    else if ((RenderType & RENDER_CHROME3) == RENDER_CHROME3)
    {
        PartObjectColor3(Type, Alpha, Bright, b->BodyLight,
                         (RenderType & RENDER_EXTRA) ? true : false);
    }
    else
    {
        PartObjectColor2(Type, Alpha, Bright, b->BodyLight,
                         (RenderType & RENDER_EXTRA) ? true : false);
    }
    if (Type == MODEL_LEGENDARY_STAFF)
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      1, Texture);
    else if (Type == MODEL_KNIGHT_BLADE)
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      1, Texture);
    else if (Type == MODEL_DARK_REIGN_BLADE)
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      0, Texture);
    else if (Type == MODEL_GRAND_SOUL_SHIELD || Type == MODEL_ELEMENTAL_SHIELD)
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      -1, Texture);
    else if (Type == MODEL_ILLUSION_SORCERER_COVENANT)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      1, Texture);
    }
    else if (Type == MODEL_POTION + 64)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      0, Texture);
    }
    else if (Type == MODEL_FLAMBERGE)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      5, Texture);
    }
    else if (Type == MODEL_FROST_BARRIER)
    {
        b->RenderMesh(0, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_SERAPHIM_HELM)
    {
        b->RenderMesh(0, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_FAITH_HELM)
    {
        b->RenderMesh(1, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_HELM + 53)
    {
        b->RenderMesh(2, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type >= MODEL_SACRED_GLOVE && Type <= MODEL_PHOENIX_SOUL_STAR)
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
    }
    else if (Type == MODEL_SACRED_HELM || Type == MODEL_STORM_HARD_HELM)
    {
        glColor3fv(b->BodyLight);
        b->RenderMesh(1, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_PIERCING_HELM)
    {
        glColor3fv(b->BodyLight);
        b->RenderMesh(0, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_SACRED_ARMOR)
    {
        b->RenderMesh(1, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_STORM_HARD_ARMOR)
    {
        b->RenderMesh(1, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_PIERCING_ARMOR)
    {
        b->RenderMesh(0, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_PHOENIX_SOUL_HELMET)
    {
        b->RenderMesh(0, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_PHOENIX_SOUL_ARMOR)
    {
        b->RenderMesh(2, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_ARMORINVEN_74)
    {
        b->RenderMesh(0, RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else if (Type == MODEL_PHOENIX_SOUL_BOOTS)
    {
        b->RenderMesh(0, RenderType, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
    }
    else
    {
        b->RenderBody(RenderType, Alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      -1, Texture);
    }
}

void SessionRenderUnit::RenderPartObjectEffect(const ObjectDrawInput &input, int Type,
                                               const vec3_t Light, float Alpha, int ItemLevel,
                                               int ExcellentFlags, int ancientDiscriminator,
                                               int Select, int RenderType)
{
    auto draw = input;
    const auto *o = draw.source;
    int Level = ItemLevel;
    if (RenderType & RENDER_WAVE)
    {
        Level = 0;
    }
    BMD *b = &Models[Type];

    if (draw.shadow == true)
    {
        if (TheMapProcess().CharacterPolicy().shadowAlpha < 1.f)
        {
            EnableAlphaTest();
            glColor4f(0.f, 0.f, 0.f, TheMapProcess().CharacterPolicy().shadowAlpha);
        }
        else
        {
            DisableAlphaBlend();
            glColor3f(0.f, 0.f, 0.f);
        }
        bool bRenderShadow = true;

        if (!TheMapProcess().GroundShadowsVisible() || draw.skillCount == 3)
        {
            bRenderShadow = false;
        }

        if (draw.HasBuff(eBuff_Cloaking))
        {
            bRenderShadow = false;
        }

        if (bRenderShadow)
        {
            if (draw.renderShadow)
            {
                int iHiddenMesh = draw.hiddenMesh;

                if (draw.type == MODEL_DREADFEAR)
                {
                    iHiddenMesh = 2;
                }
                else if (draw.type == MODEL_GAYION)
                {
                    iHiddenMesh = 0;
                }

                b->RenderBodyShadow(draw.blendMesh, iHiddenMesh);
            }
        }
        return;
    }

    switch (Type)
    {
    case MODEL_HORN_OF_DINORANT:
        Level = 8;
        break;
    case MODEL_ELITE_TRANSFER_SKELETON_RING:
        Level = 13;
        break;
    case MODEL_JACK_OLANTERN_TRANSFORMATION_RING:
        Level = 13;
        break;
    case MODEL_CHRISTMAS_TRANSFORMATION_RING:
        Level = 13;
        break;
    case MODEL_CHRISTMAS_STAR:
        Level = 13;
        break;
    case MODEL_GAME_MASTER_TRANSFORMATION_RING:
        Level = 13;
        break;
    case MODEL_TRANSFORMATION_RING:
        Level = 8;
        break;
    case MODEL_CAPE_OF_LORD:
    case MODEL_CAPE_OF_FIGHTER:
        Level = 0;
        break;
    case MODEL_EVENT + 16:
        Level = 0;
        break;
    case MODEL_JEWEL_OF_BLESS:
    case MODEL_JEWEL_OF_SOUL:
    case MODEL_JEWEL_OF_LIFE:
    case MODEL_JEWEL_OF_GUARDIAN:
    case MODEL_PACKED_JEWEL_OF_LIFE:
    case MODEL_PACKED_JEWEL_OF_GUARDIAN:
    case MODEL_PACKED_JEWEL_OF_CHAOS:
    case MODEL_COMPILED_CELE:
    case MODEL_COMPILED_SOUL:
    case MODEL_JEWEL_OF_CHAOS:
        Level = 8;
        break;
    case MODEL_WING_OF_STORM:
    case MODEL_WING_OF_ETERNAL:
    case MODEL_WING_OF_ILLUSION:
    case MODEL_WING_OF_RUIN:
    case MODEL_CAPE_OF_EMPEROR:
    case MODEL_WING_OF_CURSE:
    case MODEL_WINGS_OF_DESPAIR:
    case MODEL_WING_OF_DIMENSION:
    case MODEL_WINGS_OF_SPIRITS:
    case MODEL_WINGS_OF_SOUL:
    case MODEL_WINGS_OF_DRAGON:
    case MODEL_WINGS_OF_DARKNESS:
    case MODEL_WING:
    case MODEL_WINGS_OF_HEAVEN:
    case MODEL_CAPE_OF_OVERRULE:
    case MODEL_WINGS_OF_SATAN:
        Level = 0;
        break;
    case MODEL_ORB_OF_TWISTING_SLASH:
        Level = 9;
        break;
    case MODEL_ORB_OF_SUMMONING:
        Level = 0;
        break;
    case MODEL_ORB_OF_RAGEFUL_BLOW:
    case MODEL_ORB_OF_IMPALE:
    case MODEL_ORB_OF_FIRE_SLASH:
    case MODEL_ORB_OF_PENETRATION:
    case MODEL_ORB_OF_ICE_ARROW:
    case MODEL_ORB_OF_DEATH_STAB:
    case MODEL_CRYSTAL_OF_DESTRUCTION:
    case MODEL_CRYSTAL_OF_MULTI_SHOT:
    case MODEL_CRYSTAL_OF_RECOVERY:
    case MODEL_CRYSTAL_OF_FLAME_STRIKE: {
        Level = 9;
        break;
    }
    case MODEL_ORB_OF_GREATER_FORTITUDE:
        Level = 9;
        break;
    case MODEL_POTION + 12:
        Level = 8;
        break;
    case MODEL_DEVILS_EYE:
    case MODEL_DEVILS_KEY:
    case MODEL_DEVILS_INVITATION: {
        if (Level <= 6)
        {
            Level /= 2;
        }
        else
        {
            Level = 13;
        }
    }
    break;
    case MODEL_POTION + 20:
        Level = 9;
        break;
    case MODEL_JEWEL_OF_CREATION:
        Level = 8;
        break;
    case MODEL_PACKED_JEWEL_OF_CREATION:
        Level = 8;
        break;
    case MODEL_TEAR_OF_ELF:
        Level = 8;
        break;
    case MODEL_SOUL_SHARD_OF_WIZARD:
        Level = 8;
        break;
    case MODEL_PACKED_GEMSTONE:
    case MODEL_PACKED_JEWEL_OF_HARMONY:
    case MODEL_PACKED_LOWER_REFINE_STONE:
    case MODEL_PACKED_HIGHER_REFINE_STONE:
    case MODEL_GEMSTONE:
        Level = 0;
        break;
    case MODEL_JEWEL_OF_HARMONY:
        Level = 0;
        break;
    case MODEL_LOWER_REFINE_STONE:
        Level = 0;
        break;
    case MODEL_HIGHER_REFINE_STONE:
        Level = 0;
        break;
    case MODEL_ILLUSION_SORCERER_COVENANT:
    case MODEL_POTION + 64:
        Level = 0;
        break;
    case MODEL_FLAME_OF_CONDOR:
    case MODEL_FEATHER_OF_CONDOR:
        Level = 0;
        break;
    case MODEL_EVENT + 4:
        Level = 0;
        break;
    case MODEL_EVENT + 6:
        if (Level == 13)
        {
            Level = 13;
        }
        else
        {
            Level = 9;
        }
        break;
    case MODEL_EVENT + 7:
        Level = 0;
        break;
    case MODEL_EVENT + 8:
        Level = 0;
        break;
    case MODEL_EVENT + 9:
        Level = 8;
        break;
    case MODEL_EVENT + 5: {
        Level = 0;
    }
    break;
    case MODEL_EVENT + 10:
        Level = (Level - 8) * 2 + 1;
        break;
    case MODEL_EVENT + 11:
        Level--;
        break;
    case MODEL_EVENT + 12:
        Level = 0;
        break;
    case MODEL_EVENT + 13:
        Level = 0;
        break;
    case MODEL_EVENT + 14:
        Level += 7;
        break;
    case MODEL_EVENT + 15:
        Level = 8;
        break;
    case MODEL_EVENT:
    case MODEL_EVENT + 1:
        Level = 8;
        break;
    case MODEL_BOLT:
        Level >= 1 ? Level = Level * 2 + 1 : Level = 0;
        break;
    case MODEL_ARROWS:
        Level >= 1 ? Level = Level * 2 + 1 : Level = 0;
        break;
#ifdef LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM
    case MODEL_HELPER + 134:
        Level = 13;
        break;
#endif //LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM
    }

    if (g_pOption->GetRenderLevel() < 4)
    {
        Level = std::min<int>(Level, g_pOption->GetRenderLevel() * 2 + 5);
    }

    if (draw.type == MODEL_BILL_OF_BALROG)
    {
        Vector(0.5f, 0.5f, 1.5f, b->BodyLight);
        b->StreamMesh = 0;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh, BITMAP_CHROME);
        b->StreamMesh = -1;
    }
    else if (draw.type == MODEL_POTION + 27)
    {
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        b->StreamMesh = 0;
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, -1, draw.blendLight, draw.blendU, draw.blendV);
        if (Level == 1)
        {
        }
        else if (Level == 2)
        {
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, -1, draw.blendLight, draw.blendU,
                          draw.blendV);
            Vector(0.75f, 0.65f, 0.5f, b->BodyLight);
            b->RenderMesh(1, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, -1, draw.blendLight,
                          draw.blendU, draw.blendV, BITMAP_CHROME);
        }
        else if (Level == 3)
        {
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, -1, draw.blendLight, draw.blendU,
                          draw.blendV);
            b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, -1, draw.blendLight, draw.blendU,
                          draw.blendV);
            Vector(0.75f, 0.65f, 0.5f, b->BodyLight);
            b->RenderMesh(1, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, -1, draw.blendLight,
                          draw.blendU, draw.blendV, BITMAP_CHROME);
            b->RenderMesh(2, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, -1, draw.blendLight,
                          draw.blendU, draw.blendV, BITMAP_CHROME);
        }
        b->StreamMesh = -1;
        return;
    }
    else if (draw.type == MODEL_FIRECRACKER)
    {
        b->StreamMesh = 0;
        draw.blendLight = 1.f;
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        Vector(1.f, 0.f, 0.f, b->BodyLight);
        b->LightEnable = true;
        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, -1, draw.blendLight, draw.blendU, draw.blendV);
        b->RenderMesh(1, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, -1, draw.blendLight,
                      draw.blendU, draw.blendV);
        b->StreamMesh = -1;
        return;
    }
    else if (draw.type == MODEL_GM_GIFT)
    {
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, -1, draw.blendLight, draw.blendU, draw.blendV);
        b->LightEnable = true;
        Vector(0.1f, 0.6f, 0.4f, b->BodyLight);
        draw.alpha = 0.5f;
        b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, -1, draw.blendLight,
                      draw.blendU, draw.blendV);
        return;
    }
    else if (draw.type == MODEL_EVENT + 14 && Level == 9)
    {
        Vector(0.3f, 0.8f, 1.f, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, -1, draw.blendLight, draw.blendU, draw.blendV);
        Vector(1.f, 0.8f, 0.3f, b->BodyLight);
        b->RenderBody(RENDER_BRIGHT | RENDER_CHROME, draw.alpha, -1, draw.blendLight, draw.blendU,
                      draw.blendV);
        return;
    }
    else if ((draw.type >= MODEL_SCROLL_OF_FIREBURST &&
              draw.type <= MODEL_SCROLL_OF_ELECTRIC_SPARK) ||
             draw.type == MODEL_SCROLL_OF_FIRE_SCREAM ||
             (draw.type == MODEL_SCROLL_OF_CHAOTIC_DISEIER))
    {
        b->BeginRender(draw.alpha);
        glColor3f(1.f, 1.f, 1.f);
        draw.blendLight = 1.f;
        b->RenderMesh(0, RENDER_TEXTURE, Alpha, -1, draw.blendLight, draw.blendU, draw.blendV,
                      draw.hiddenMesh);
        draw.blendLight = sinf(WorldTime * 0.001f) * 0.5f + 0.5f;
        b->RenderMesh(1, RENDER_BRIGHT | RENDER_TEXTURE, Alpha, 1, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(2, RENDER_BRIGHT | RENDER_TEXTURE, Alpha, 2, 1 - draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->EndRender();
        return;
    }
    else if (draw.type == MODEL_SPIRIT)
    {
        b->RenderBody(RENDER_TEXTURE, Alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        switch (Level)
        {
        case 0:
            b->RenderMesh(0, RENDER_BRIGHT | RENDER_TEXTURE, Alpha, 0, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
            break;

        case 1:
            Vector(0.3f, 0.8f, 1.f, b->BodyLight);
            b->RenderMesh(0, RENDER_BRIGHT | RENDER_TEXTURE, Alpha, 0, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
            break;
        }
        return;
    }
    else if (draw.type >= MODEL_POTION && draw.type <= MODEL_LARGE_MANA_POTION)
    {
        if (Level > 0)
            Level = 7;
    }
    else if ((draw.type >= MODEL_SEED_FIRE && draw.type <= MODEL_SEED_EARTH) ||
             (draw.type >= MODEL_SPHERE_MONO && draw.type <= MODEL_SPHERE_5) ||
             (draw.type >= MODEL_SEED_SPHERE_FIRE_1 && draw.type <= MODEL_SEED_SPHERE_EARTH_5))
    {
        Level = 0;
    }
    else if (draw.type == MODEL_FRUITS)
    {
        switch (Level)
        {
        case 0:
            Vector(0.0f, 0.5f, 1.0f, b->BodyLight);
            break;
        case 1:
            Vector(1.0f, 0.2f, 0.0f, b->BodyLight);
            break;
        case 2:
            Vector(1.0f, 0.8f, 0.0f, b->BodyLight);
            break;
        case 3:
            Vector(0.6f, 0.8f, 0.4f, b->BodyLight);
            break;
        }
        b->RenderBody(RENDER_METAL, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh, BITMAP_CHROME + 1);
        b->RenderBody(RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, draw.hiddenMesh, BITMAP_CHROME + 1);
        return;
    }
    else if (draw.type == MODEL_EVENT + 11)
    {
        Vector(0.9f, 0.9f, 0.9f, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderBody(RENDER_CHROME | RENDER_BRIGHT, 0.5f, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV, -1, BITMAP_CHROME + 1);
        return;
    }
    else if (Type == MODEL_EVENT + 5 && ItemLevel == 14)
    {
        Vector(0.2f, 0.3f, 0.5f, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        Vector(0.1f, 0.3f, 1.f, b->BodyLight);
        b->RenderBody(RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
        b->RenderBody(RENDER_METAL | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
        return;
    }
    else if (Type == MODEL_EVENT + 5 && ItemLevel == 15)
    {
        Vector(0.5f, 0.3f, 0.2f, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        Vector(1.f, 0.3f, 0.1f, b->BodyLight);
        b->RenderBody(RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
        b->RenderBody(RENDER_METAL | RENDER_BRIGHT, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
        return;
    }
    else if (draw.type == MODEL_BLOOD_BONE)
    {
        Vector(.9f, .1f, .1f, b->BodyLight);
        draw.blendU = sinf(gMapManager.ContextMap() * 0.0001f);
        draw.blendV = -WorldTime * 0.0005f;
        Models[draw.type].StreamMesh = 0;
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh, BITMAP_CHROME);
        Models[draw.type].StreamMesh = -1;
        Vector(.9f, .9f, .9f, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        return;
    }
    else if (draw.type == MODEL_INVISIBILITY_CLOAK)
    {
        Vector(0.8f, 0.8f, 0.8f, b->BodyLight);
        float sine = float(sinf(WorldTime * 0.002f) * 0.3f) + 0.7f;

        b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, 1.0f, 0, sine, draw.blendU, draw.blendV,
                      draw.hiddenMesh);
        return;
    }
    else if (draw.type == MODEL_EVENT + 12)
    {
        float Luminosity = (float)sinf((WorldTime) * 0.002f) * 0.35f + 0.65f;
        vec3_t p, Position, EffLight;
        Vector(0.f, 0.f, 15.f, p);

        float Scale = Luminosity * 0.8f + 2.f;
        Vector(Luminosity * 0.32f, Luminosity * 0.32f, Luminosity * 2.f, EffLight);

        b->TransformPosition(draw.bones[0], p, Position);
        VectorAdd(Position, draw.position, Position);

        CreateSprite(BITMAP_SPARK + 1, Position, Scale, EffLight, o);
    }
    else if (draw.type == MODEL_EVENT + 6 && Level == 13)
    {
        Vector(0.4f, 0.6f, 1.0f, b->BodyLight);
        b->RenderBody(RENDER_COLOR, 1.0f, 0, 1.0f, draw.blendU, draw.blendV, draw.hiddenMesh);
        b->RenderBody(RENDER_CHROME | RENDER_BRIGHT, 1.0f, 0, 1.0f, draw.blendU, draw.blendV,
                      draw.hiddenMesh);
        return;
    }
    else if (draw.type == MODEL_EVENT + 13)
    {
        float Luminosity = (float)sinf((WorldTime) * 0.002f) * 0.35f + 0.65f;
        vec3_t p, Position, EffLight;
        Vector(0.f, -5.f, -15.f, p);

        float Scale = Luminosity * 0.8f + 2.5f;
        Vector(Luminosity * 2.f, Luminosity * 0.32f, Luminosity * 0.32f, EffLight);

        b->StreamMesh = 0;
        draw.blendV = (int)-WorldTime % 4000 * 0.00025f;

        b->TransformPosition(draw.bones[0], p, Position);
        VectorAdd(Position, draw.position, Position);

        CreateSprite(BITMAP_SPARK + 1, Position, Scale, EffLight, o);
    }
    else if (draw.type == MODEL_DEVILS_EYE)
    {
        float sine = (float)sinf(WorldTime * 0.002f) * 10.f + 15.65f;

        draw.blendMesh = 1;
        draw.blendLight = sine;
        draw.blendV = (int)WorldTime % 2000 * 0.0005f;
        draw.alpha = 2.0f;

        float Luminosity = sine;
        Vector(Luminosity / 5.0f, Luminosity / 5.0f, Luminosity / 5.0f, draw.light);
    }
    else if (draw.type == MODEL_DEVILS_KEY)
    {
        float Luminosity = (float)sinf((WorldTime) * 0.002f) * 0.35f + 0.65f;
        vec3_t p, Position, EffLight;
        Vector(0.f, 0.f, 0.f, p);

        float Scale = Luminosity * 0.8f;
        Vector(Luminosity * 2, Luminosity * 0.32f, Luminosity * 0.32f, EffLight);

        b->TransformPosition(draw.bones[1], p, Position);
        VectorAdd(Position, draw.position, Position);
        CreateSprite(BITMAP_SPARK + 1, Position, Scale, EffLight, o);

        b->TransformPosition(draw.bones[2], p, Position);
        VectorAdd(Position, draw.position, Position);
        CreateSprite(BITMAP_SPARK + 1, Position, Scale, EffLight, o);
    }
    else if (draw.type == MODEL_DEVILS_INVITATION)
    {
        float Luminosity = (float)sinf((WorldTime) * 0.002f) * 0.35f + 0.65f;
        vec3_t p, Position, EffLight;
        Vector(0.f, 0.f, 0.f, p);

        float Scale = Luminosity * 0.8f;
        Vector(Luminosity * 2, Luminosity * 0.32f, Luminosity * 0.32f, EffLight);

        b->TransformPosition(draw.bones[9], p, Position);
        VectorAdd(Position, draw.position, Position);
        CreateSprite(BITMAP_SPARK + 1, Position, Scale, EffLight, o);

        b->TransformPosition(draw.bones[10], p, Position);
        VectorAdd(Position, draw.position, Position);
        CreateSprite(BITMAP_SPARK + 1, Position, Scale, EffLight, o);
    }
    else if (draw.type == MODEL_POTION + 21)
    {
        float Luminosity = (float)sinf((WorldTime) * 0.002f) * 0.25f + 0.75f;
        vec3_t EffLight;

        Vector(Luminosity * 1.f, Luminosity * 0.5f, Luminosity * 0.f, EffLight);
        CreateSprite(BITMAP_SPARK + 1, draw.position, 2.5f, EffLight, o);
    }
    else if (draw.type == MODEL_WINGS_OF_DRAGON)
    {
        draw.blendLight = (float)(sinf(WorldTime * 0.001f) + 1.f) / 4.f;
    }
    else if (draw.type == MODEL_WINGS_OF_SOUL)
    {
        draw.blendLight = (float)sinf(WorldTime * 0.001f) + 1.1f;
    }
    else if (Type == MODEL_RED_SPIRIT_PANTS || Type == MODEL_RED_SPIRIT_HELM)
    {
        draw.blendLight = sinf(WorldTime * 0.001f) * 0.4f + 0.6f;
    }
    else if (draw.type == MODEL_STAFF_OF_KUNDUN)
    {
        draw.blendLight = sinf(WorldTime * 0.004f) * 0.3f + 0.7f;
    }
    else if (Type == MODEL_DIVINE_HELM || Type == MODEL_DIVINE_GLOVES || Type == MODEL_DIVINE_BOOTS)
    {
        draw.blendLight = 1.f;
    }
    else if (Type == MODEL_SIEGE_POTION)
    {
        switch (Level)
        {
        case 0:
            draw.hiddenMesh = 1;
            break;
        case 1:
            draw.hiddenMesh = 0;
            break;
        }
    }
    else if (Type == MODEL_HELPER + 7)
    {
        switch (Level)
        {
        case 0:
            draw.hiddenMesh = 1;
            break;
        case 1:
            draw.hiddenMesh = 0;
            break;
        }
    }
    else if (Type == MODEL_LIFE_STONE_ITEM)
    {
        draw.hiddenMesh = 1;
    }
    else if (Type == MODEL_EVENT + 18)
    {
        draw.blendMesh = 1;
    }

    if (!draw.shadow)
    {
        float Luminosity = 1.f;

        if (draw.HasBuff(eBuff_Cloaking))
        {
            Alpha = 0.5f;
            Vector(1.f, 1.f, 1.f, b->BodyLight);
            b->RenderBody(RENDER_BRIGHT | RENDER_CHROME5, Alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU * 8.f, draw.blendV * 2.f, -1, BITMAP_CHROME2);
        }
        else if (draw.HasBuff(eDeBuff_Poison) && draw.HasBuff(eDeBuff_Freeze))
        {
            Vector(Luminosity * 0.3f, Luminosity * 1.f, Luminosity * 1.f, b->BodyLight);
            RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
        }
        else if (draw.HasBuff(eDeBuff_BlowOfDestruction))
        {
            Vector(Luminosity * 0.3f, Luminosity * 1.f, Luminosity * 1.f, b->BodyLight);
            RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
        }
        else if (draw.HasBuff(eDeBuff_Poison))
        {
            Vector(Luminosity * 0.3f, Luminosity * 1.f, Luminosity * 0.5f, b->BodyLight);
            RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
        }
        else if (draw.HasBuff(eDeBuff_Stun))
        {
            Vector(Luminosity * 0.f, Luminosity * 0.f, Luminosity * 1.0f, b->BodyLight);
            RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
        }
        else if (draw.HasBuff(eDeBuff_Freeze))
        {
            Vector(Luminosity * 0.3f, Luminosity * 0.5f, Luminosity * 1.f, b->BodyLight);
            RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
        }
        else if (draw.HasBuff(eDeBuff_BlowOfDestruction))
        {
            Vector(Luminosity * 0.3f, Luminosity * 0.5f, Luminosity * 1.f, b->BodyLight);
            RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
        }
        else if (draw.HasBuff(eDeBuff_Harden))
        {
            Vector(Luminosity * 0.3f, Luminosity * 0.5f, Luminosity * 1.f, b->BodyLight);
            RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
        }
        else if (Level < 3 || draw.type == MODEL_ZEN)
        {
            if (draw.type == MODEL_POTION + 64)
            {
                RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
                RenderPartObjectBodyColor2(
                    b, draw, Type, 0.5f,
                    RENDER_TEXTURE | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 0.5f);
                RenderPartObjectBodyColor2(
                    b, draw, Type, 1.f,
                    RENDER_CHROME4 | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
            }
            else if (draw.type == MODEL_ILLUSION_SORCERER_COVENANT)
            {
                VectorCopy(Light, b->BodyLight);
                RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
                RenderPartObjectBodyColor2(
                    b, draw, Type, 1.5f,
                    RENDER_CHROME2 | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.5f);
                RenderPartObjectBodyColor2(
                    b, draw, Type, 1.f,
                    RENDER_CHROME4 | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
            }
            else if (draw.type == MODEL_JEWEL_OF_HARMONY || draw.type == MODEL_MOONSTONE_PENDANT)
            {
                VectorCopy(Light, b->BodyLight);
                RenderPartObjectBody(b, draw, Type, Alpha, RenderType);

                RenderPartObjectBodyColor2(
                    b, draw, Type, 1.5f,
                    RENDER_CHROME2 | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.5f);
                RenderPartObjectBodyColor2(
                    b, draw, Type, 1.f,
                    RENDER_CHROME4 | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
            }
            else if (draw.type == MODEL_HELPER + 43 || draw.type == MODEL_HELPER + 93)
            {
                Vector(Light[0] * 0.9f, Light[1] * 0.9f, Light[2] * 0.9f, b->BodyLight);
                RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
                RenderPartObjectBodyColor2(
                    b, draw, Type, 1.5f,
                    RENDER_CHROME2 | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.5f);
                RenderPartObjectBodyColor2(
                    b, draw, Type, 1.f,
                    RENDER_CHROME4 | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
            }
            else if (draw.type == MODEL_HELPER + 44 || draw.type == MODEL_HELPER + 94 ||
                     draw.type == MODEL_HELPER + 116)
            {
                Vector(Light[0] * 0.9f, Light[1] * 0.9f, Light[2] * 0.9f, b->BodyLight);
                RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
                RenderPartObjectBodyColor2(
                    b, draw, Type, 1.5f,
                    RENDER_CHROME2 | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.5f);
                RenderPartObjectBodyColor2(
                    b, draw, Type, 1.f,
                    RENDER_CHROME4 | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
            }
            else if (draw.type == MODEL_HELPER + 45)
            {
                Vector(Light[0] * 0.9f, Light[1] * 0.9f, Light[2] * 0.9f, b->BodyLight);
                RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
                RenderPartObjectBodyColor2(
                    b, draw, Type, 1.5f,
                    RENDER_CHROME2 | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.5f);
                RenderPartObjectBodyColor2(
                    b, draw, Type, 1.f,
                    RENDER_CHROME4 | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
            }
            else
            {
                VectorCopy(Light, b->BodyLight);
                RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
            }
        }
        else if (Level < 5)
        {
            vec3_t l;
            Vector(g_Luminosity, g_Luminosity * 0.6f, g_Luminosity * 0.6f, l);
            VectorMul(l, Light, b->BodyLight);
            RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
        }
        else if (Level < 7)
        {
            vec3_t l;
            Vector(g_Luminosity * 0.5f, g_Luminosity * 0.7f, g_Luminosity, l);
            VectorMul(l, Light, b->BodyLight);
            RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
        }
        else if (g_pOption->GetRenderLevel())
        {
            if (Level < 8 && g_pOption->GetRenderLevel() >= 1) //  +7
            {
                Vector(Light[0] * 0.8f, Light[1] * 0.8f, Light[2] * 0.8f, b->BodyLight);
                RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
                RenderPartObjectBodyColor(b, draw, Type, Alpha, RENDER_CHROME | RENDER_BRIGHT, 1.f);
            }
            else if (Level < 9 && g_pOption->GetRenderLevel() >= 1) //  +8
            {
                Vector(Light[0] * 0.8f, Light[1] * 0.8f, Light[2] * 0.8f, b->BodyLight);
                RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
                RenderPartObjectBodyColor(b, draw, Type, Alpha, RENDER_CHROME | RENDER_BRIGHT, 1.f);
            }
            else if (Level < 10 && g_pOption->GetRenderLevel() >= 2) //  +9
            {
                Vector(Light[0] * 0.9f, Light[1] * 0.9f, Light[2] * 0.9f, b->BodyLight);
                RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
                RenderPartObjectBodyColor(
                    b, draw, Type, Alpha,
                    RENDER_CHROME | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
                RenderPartObjectBodyColor(
                    b, draw, Type, Alpha,
                    RENDER_METAL | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
            }
            else if (Level < 11 && g_pOption->GetRenderLevel() >= 2) //  +10
            {
                Vector(Light[0] * 0.9f, Light[1] * 0.9f, Light[2] * 0.9f, b->BodyLight);
                RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
                RenderPartObjectBodyColor(
                    b, draw, Type, Alpha,
                    RENDER_CHROME | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
                RenderPartObjectBodyColor(
                    b, draw, Type, Alpha,
                    RENDER_METAL | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
            }
            else if (Level < 12 && g_pOption->GetRenderLevel() >= 3) //  +11
            {
                Vector(Light[0] * 0.9f, Light[1] * 0.9f, Light[2] * 0.9f, b->BodyLight);
                RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
                RenderPartObjectBodyColor2(
                    b, draw, Type, 1.f,
                    RENDER_CHROME2 | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
                RenderPartObjectBodyColor(
                    b, draw, Type, Alpha,
                    RENDER_METAL | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
                RenderPartObjectBodyColor(
                    b, draw, Type, Alpha,
                    RENDER_CHROME | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
            }
            else if (Level < 13 && g_pOption->GetRenderLevel() >= 3) //  +12
            {
                Vector(Light[0] * 0.9f, Light[1] * 0.9f, Light[2] * 0.9f, b->BodyLight);
                RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
                RenderPartObjectBodyColor2(
                    b, draw, Type, 1.f,
                    RENDER_CHROME2 | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
                RenderPartObjectBodyColor(
                    b, draw, Type, Alpha,
                    RENDER_METAL | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
                RenderPartObjectBodyColor(
                    b, draw, Type, Alpha,
                    RENDER_CHROME | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
            }
            else if (Level < 14 && g_pOption->GetRenderLevel() >= 4) //  +13
            {
                Vector(Light[0] * 0.9f, Light[1] * 0.9f, Light[2] * 0.9f, b->BodyLight);
                RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
                RenderPartObjectBodyColor2(
                    b, draw, Type, 1.f,
                    RENDER_CHROME4 | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
                RenderPartObjectBodyColor(
                    b, draw, Type, Alpha,
                    RENDER_METAL | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
                RenderPartObjectBodyColor(
                    b, draw, Type, Alpha,
                    RENDER_CHROME | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
            }
            else if (Level < 15 && g_pOption->GetRenderLevel() >= 4) //  +14
            {
                Vector(Light[0] * 0.9f, Light[1] * 0.9f, Light[2] * 0.9f, b->BodyLight);
                RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
                RenderPartObjectBodyColor2(
                    b, draw, Type, 1.f,
                    RENDER_CHROME4 | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
                RenderPartObjectBodyColor(
                    b, draw, Type, Alpha,
                    RENDER_METAL | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
                RenderPartObjectBodyColor(
                    b, draw, Type, Alpha,
                    RENDER_CHROME | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
            }
            else if (Level < 16 && g_pOption->GetRenderLevel() >= 4) //  +15
            {
                Vector(Light[0] * 0.9f, Light[1] * 0.9f, Light[2] * 0.9f, b->BodyLight);
                RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
                RenderPartObjectBodyColor2(
                    b, draw, Type, 1.f,
                    RENDER_CHROME4 | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
                RenderPartObjectBodyColor(
                    b, draw, Type, Alpha,
                    RENDER_METAL | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
                RenderPartObjectBodyColor(
                    b, draw, Type, Alpha,
                    RENDER_CHROME | RENDER_BRIGHT | (RenderType & RENDER_EXTRA), 1.f);
            }
            else
            {
                VectorCopy(Light, b->BodyLight);
                RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
            }
        }
        else
        {
            VectorCopy(Light, b->BodyLight);
            RenderPartObjectBody(b, draw, Type, Alpha, RenderType);
        }

        if (g_pOption->GetRenderLevel() == 0)
        {
            return;
        }

        if (!draw.HasBuff(eDeBuff_Harden) && !draw.HasBuff(eBuff_Cloaking) &&
            !draw.HasBuff(eDeBuff_CursedTempleRestraint))
        {
            if ((ExcellentFlags & 63) > 0 &&
                (draw.type < MODEL_WING || draw.type > MODEL_WINGS_OF_DARKNESS) &&
                draw.type != MODEL_CAPE_OF_LORD &&
                (draw.type < MODEL_WING_OF_STORM || draw.type > MODEL_WING_OF_DIMENSION) &&
                (draw.type < MODEL_WING + 130 || MODEL_WING + 134 < draw.type) &&
                !(draw.type >= MODEL_CAPE_OF_FIGHTER && draw.type <= MODEL_CAPE_OF_OVERRULE) &&
                (draw.type != MODEL_WING + 135))
            {
                Luminosity = sinf(WorldTime * 0.002f) * 0.5f + 0.5f;
                Vector(Luminosity, Luminosity * 0.3f, 1.f - Luminosity, b->BodyLight);
                Alpha = 1.f;
                if (b->HideSkin && MODEL_MISTERY_HELM <= draw.type &&
                    MODEL_LILIUM_HELM >= draw.type)
                {
                    int anMesh[6] = {2, 1, 0, 2, 1, 2};
                    b->RenderMesh(anMesh[draw.type - (MODEL_MISTERY_HELM)],
                                  RENDER_TEXTURE | RENDER_BRIGHT, Alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV);
                }

                else if (Type == MODEL_SACRED_HELM || Type == MODEL_STORM_HARD_HELM)
                {
                    b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, Alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV);
                }
                else if (Type == MODEL_PIERCING_HELM)
                {
                    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, Alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV);
                }
                else if (Type == MODEL_SACRED_ARMOR)
                {
                    b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, Alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV);
                }
                else if (Type == MODEL_STORM_HARD_ARMOR)
                {
                    b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, Alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV);
                }
                else if (Type == MODEL_PIERCING_ARMOR)
                {
                    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, Alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV);
                }
                else if (Type == MODEL_PHOENIX_SOUL_HELMET)
                {
                    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, Alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV);
                }
                else if (Type == MODEL_PHOENIX_SOUL_ARMOR)
                {
                    b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, Alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV);
                }
                else if (Type == MODEL_ARMORINVEN_74)
                {
                    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, Alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV);
                }
                else if (Type == MODEL_PHOENIX_SOUL_BOOTS)
                {
                    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, Alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV);
                }
                else
                {
                    b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, Alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV);
                }
            }
            else if (ancientDiscriminator > 0)
            {
                Alpha = sinf(WorldTime * 0.001f) * 0.5f + 0.4f;
                RenderPartObjectBodyColor2(b, draw, Type, Alpha, RENDER_CHROME3 | RENDER_BRIGHT,
                                           1.f);
            }
        }
    }
#ifndef CAMERA_TEST
    else
    {
        if (TheMapProcess().CharacterPolicy().shadowAlpha < 1.f)
        {
            EnableAlphaTest();
            glColor4f(0.f, 0.f, 0.f, TheMapProcess().CharacterPolicy().shadowAlpha);
        }
        else
        {
            DisableAlphaBlend();
            glColor3f(0.f, 0.f, 0.f);
        }
        bool bRenderShadow = true;

        if (!TheMapProcess().GroundShadowsVisible() || draw.skillCount == 3)
        {
            bRenderShadow = false;
        }

        if (draw.HasBuff(eBuff_Cloaking))
        {
            bRenderShadow = false;
        }

        if (bRenderShadow)
        {
            bRenderShadow = draw.renderShadow;
            if (bRenderShadow)
            {
                b->RenderBodyShadow(draw.blendMesh, draw.hiddenMesh);
            }
        }
#endif
    }
}

void SessionRenderUnit::RenderPartObjectEdge(BMD *b, const ObjectDrawInput &draw, int Flag,
                                             bool Translate, float Scale)
{
    const auto *o = draw.source;
    OBB_t bounds{};
    if (g_isCharacterBuff(o, eBuff_Cloaking))
    {
        return;
    }

    b->LightEnable = false;

    BoneScale = Scale;
    const auto *bones = draw.bones;
    if (!bones)
    {
        auto *pose = drawPoses_.Allocate(b->NumBones);
        memcpy(pose, BoneTransform, b->NumBones * sizeof(vec34_t));
        bones = pose;
    }
    b->Transform(bones, o->BoundingBoxMin, o->BoundingBoxMax, &bounds,
                 o->EnableBoneMatrix ? Translate : false, 0.f, draw.stableBones || !draw.bones);

    if (draw.type == MODEL_WARCRAFT)
    {
        b->BeginRender(draw.alpha);
        if (draw.alpha >= 0.99f)
        {
            glColor3fv(b->BodyLight);
        }
        else
        {
            glColor4f(b->BodyLight[0], b->BodyLight[1], b->BodyLight[2], draw.alpha);
        }
        b->RenderMesh(0, Flag, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->EndRender();
    }
    else if (draw.type == MODEL_PERSONA)
    {
        glColor3fv(b->BodyLight);
        b->RenderMesh(0, Flag, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, Flag, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (draw.type == MODEL_DREADFEAR)
    {
        glColor3fv(b->BodyLight);
        b->RenderMesh(0, Flag, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(1, Flag, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else if (draw.type == MODEL_DARK_SKULL_SOLDIER_5)
    {
        glColor3fv(b->BodyLight);
        b->RenderMesh(0, Flag, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(2, Flag, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
        b->RenderMesh(3, Flag, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV, draw.hiddenMesh);
    }
    else
    {
        b->RenderBody(Flag, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                      draw.hiddenMesh);
    }

    BoneScale = 1.f;
}

void SessionRenderUnit::RenderPartObjectEdge2(BMD *b, const ObjectDrawInput &draw, int Flag,
                                              bool Translate, float Scale, OBB_t *OBB)
{
    vec3_t tmp{};

    b->LightEnable = false;

    BoneScale = Scale;
    const auto *pose = draw.bones;
    if (!pose)
    {
        auto *retained = drawPoses_.Allocate(b->NumBones);
        memcpy(retained, BoneTransform, b->NumBones * sizeof(vec34_t));
        pose = retained;
    }
    b->Transform(pose, tmp, tmp, OBB, Translate, 0.f, draw.stableBones || !draw.bones);
    b->RenderBody(Flag, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV,
                  draw.hiddenMesh);

    BoneScale = 1.f;
}

void SessionRenderUnit::RenderPartObjectEdgeLight(BMD *b, const ObjectDrawInput &draw, int Flag,
                                                  bool Translate, float Scale)
{
    float Luminosity = sinf(WorldTime * 0.001f) * 0.5f + 0.5f;
    Vector(Luminosity * 1.f, Luminosity * 0.8f, Luminosity * 0.3f, b->BodyLight);
    RenderPartObjectEdge(b, draw, Flag, Translate, Scale);
}
void SessionRenderUnit::RenderCharacterPartCloth(int type)
{
    if (!drawingCharacterVisual_ || !drawingCharacterVisual_->partCloth)
        return;
    auto &cloth = *drawingCharacterVisual_->partCloth;
    if (cloth.shape[0] == type)
        cloth.pieces->Render(*this);
}

void SessionRenderUnit::RenderPartObject(const ObjectDrawInput &input, int Type, const PART_t *p,
                                         const vec3_t Light, float Alpha, int ItemLevel,
                                         int ExcellentFlags, int ancientDiscriminator,
                                         bool GlobalTransform, bool HideSkin, bool Translate,
                                         int Select, int RenderType)
{
    auto draw = input;
    const auto *o = draw.source;
    OBB_t bounds{};
    if (Alpha <= 0.01f)
    {
        return;
    }

    if (Type == MODEL_POTION + 12)
    {
        int Level = ItemLevel;

        if (Level == 0)
        {
            Type = MODEL_EVENT;
        }
        else if (Level == 2)
        {
            Type = MODEL_EVENT + 1;
        }
    }

    BMD *b = &Models[Type];
    if (GlobalTransform)
    {
        auto *pose = drawPoses_.Allocate(b->NumBones);
        memcpy(pose, BoneTransform, b->NumBones * sizeof(vec34_t));
        draw.bones = pose;
        draw.stableBones = true;
    }
    b->HideSkin = HideSkin;
    b->BodyScale = draw.scale;
    b->ContrastEnable = draw.contrastEnable;
    b->LightEnable = draw.lightEnable;
    VectorCopy(draw.position, b->BodyOrigin);

    BoneScale = 1.f;
    if (3 == Select)
    {
        BoneScale = 1.4f;
    }
    else if (2 == Select)
    {
        BoneScale = 1.2f;
    }
    else if (1 == Select)
    {
        float Scale = 1.2f;
        Scale = o->m_fEdgeScale;
        if (o->Kind == KIND_NPC)
        {
            Vector(0.02f, 0.1f, 0.f, b->BodyLight);
        }
        else
        {
            Vector(0.1f, 0.03f, 0.f, b->BodyLight);
        }

        if (gMapManager.InChaosCastle())
        {
            Vector(0.1f, 0.01f, 0.f, b->BodyLight);
            Scale = 1.f + 0.1f / draw.scale;
        }

        RenderPartObjectEdge(b, draw, RENDER_BRIGHT, Translate, Scale);
        if (o->Kind == KIND_NPC)
        {
            Vector(0.16f, 0.7f, 0.f, b->BodyLight);
        }
        else
        {
            Vector(0.7f, 0.2f, 0.f, b->BodyLight);
        }

        if (gMapManager.InChaosCastle())
        {
            Vector(0.7f, 0.07f, 0.f, b->BodyLight);
            Scale = 1.f + 0.04f / draw.scale + 0.02f;
        }
        RenderPartObjectEdge(b, draw, RENDER_BRIGHT, Translate, Scale - 0.02f);
    }
    BodyLight(draw, b);

    b->Transform(draw.bones, o->BoundingBoxMin, o->BoundingBoxMax, &bounds, Translate, 0.f,
                 draw.stableBones);

    if (p && recordingCharacter_)
    {
        RenderCharacterPartCloth(Type);
    }
    const CPhysicsClothMesh *preparedCloth = nullptr;
    if (p && WorldObjectDetail::HasCharacterPartCloth(Type))
    {
        if (recordingCharacter_ && drawingCharacterVisual_ && drawingCharacterVisual_->partCloth &&
            drawingCharacterVisual_->partCloth->shape[0] == Type)
            preparedCloth =
                static_cast<const CPhysicsClothMesh *>(drawingCharacterVisual_->partCloth->pieces);
    }
    BMD::MeshDrawScope clothDraw(*b, preparedCloth ? preparedCloth->MeshIndex() : -1,
                                 preparedCloth ? &preparedCloth->DrawMesh() : nullptr);

    if (!g_CMonkSystem.RageFighterEffect(draw, Type))
        RenderPartObjectEffect(draw, Type, Light, Alpha, ItemLevel, ExcellentFlags,
                               ancientDiscriminator, Select, RenderType);
}

// OMF-00605
// OMF-00606
// OMF-00633

//  CSPetSystem.
void CSPetDarkSpirit::RenderPetInventory(void)
{
    RenderCmdType();
}

void CSPetDarkSpirit::RenderPet(int PetState) const
{
    auto *o = &m_PetCharacter.Object;
    if (!o->Live || !o->Visible)
        return;
    if (m_PetOwner->Object.Type != MODEL_PLAYER && o->Type != MODEL_DARK_SPIRIT)
        return;
    ObjectDrawInput draw(o);
    draw.preparedPose = &poseSample_;
    draw.stableBones = true;
    RenderObject(draw, false, 0, PetState);
}

void CSPetDarkSpirit::RenderCmdType(void)
{
    float x, y, Width, Height;
    float PartyWidth = 0.f;

    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.SetTextColor(220, 220, 220, 255);
    g_RenderText.SetBgColor(0, 0, 0, 128);

    if (PartyNumber > 0)
    {
        PartyWidth = 50.f;
    }
    if ((Hero->Helper.Type >= MODEL_GUARDIAN_ANGEL && Hero->Helper.Type <= MODEL_DARK_HORSE_ITEM) ||
        Hero->Helper.Type == MODEL_HORN_OF_FENRIR)
    {
        PartyWidth += 60.f;
    }

    int Dur = 255;
    Width = 50;
    Height = 2;
    x = GetScreenWidth() - Width - PartyWidth - 15;
    y = 4;
    int Life = CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT].Durability * (int)Width / Dur;

    EnableAlphaTest();

    g_RenderText.RenderText((int)x + 50, (int)y, I18N::Game::DarkRaven, 0, 0,
                            RT3_WRITE_RIGHT_TO_LEFT);

    RenderBar(x, y + 12, Width, Height, (float)Life);

    glColor3f(1.f, 1.f, 1.f);

    Width = 20.f;
    Height = 28.f;
    x = GetScreenWidth() - Width - PartyWidth - 65.f;
    y = 5.f;
    RenderBitmap(BITMAP_SKILL_INTERFACE + 2, (float)x, (float)y, (float)Width - 4,
                 (float)Height - 8, (((m_byCommand) % 8) * 32 + 6.f) / 256.f,
                 (((m_byCommand) / 8) * Height + 3.f) / 256.f, Width / 256.f,
                 (Height - 1.f) / 256.f);

    Width -= 8.f;
    Height -= 8.f;

    if (MouseX >= x && MouseX <= x + Width && MouseY >= y && MouseY <= y + Height)
    {
        RenderTipText((int)x, (int)(y + Height), I18N::Game::Lookup(1219 + m_byCommand));
    }
}

void SessionRenderUnit::RenderPet(CHARACTER *c)
{
    OBJECT *o = &c->Object;
    if (auto *petSystem = ResolvePetSystem(c))
    {
        if (g_isCharacterBuff(o, eBuff_Cloaking))
        {
            petSystem->RenderPet(10);
        }
        else
        {
            petSystem->RenderPet();
        }
    }
}

// OMF-00827
// OMF-00834

//  GOBoid.cpp

bool SessionRenderUnit::RenderMount(const ObjectDrawInput &draw, bool bForceRender)
{
    const auto *o = draw.source;
    if (!o->Live || (!bForceRender && !o->Visible))
        return true;
    if (o->Owner->Type != MODEL_PLAYER && o->Type != MODEL_HELPER)
        return true;
    const int state = g_isCharacterBuff(o->Owner, eBuff_Cloaking) ? 10 : 0;
    RenderObject(draw, false, 0, state);
    return true;
}

void SessionRenderUnit::RenderCharacterAttachments()
{
    for (int i = 0; i < MAX_MOUNTS; i++)
    {
        OBJECT *o = &Mounts[i];
        if (RenderMount(o) == FALSE)
        {
            return;
        }
    }
    for (int index = 0; index < CharactersClient.Size(); ++index)
    {
        if (!CharactersClient.IsValidIndex(index))
            continue;
        auto &visual = CharactersClient.WorldVisuals(index);
        if (CharacterVisibleToObserver(CharactersClient[index]))
            RenderCharacterAttachments(CharactersClient[index], visual);
    }
}
