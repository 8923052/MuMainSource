#include "render/World.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationLoopFrame.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "data/WorldData.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/EffectsUpdate.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/WorldPhysics.h"
#include "domain/WorldSimulation.h"
#include "render/Character.h"
#include "render/ModelGeometry.h"
#include "render/ModelResources.h"
#include "render/Terrain.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionPresentation.h"
#include "session/SessionRender.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"

WorldPreviewContext::WorldPreviewContext(SessionKeeper &keeper) noexcept
    : active_(keeper.worldPreviewActive_), previous_(std::exchange(active_, true))
{
}

WorldPreviewContext::~WorldPreviewContext()
{
    active_ = previous_;
}

const vec34_t *SessionRenderUnit::RenderObject(const ObjectDrawInput &input, bool Translate,
                                               int Select, int ExtraMon)
{
    auto draw = input;
    const auto *o = draw.source;
    if (Calc_RenderObject(draw, Translate, Select, ExtraMon) == false)
    {
        return input.bones;
    }
    Draw_RenderObject(draw, Translate, Select, ExtraMon);
    return draw.bones;
}

const vec34_t *SessionRenderUnit::RenderObject_AfterImage(const ObjectDrawInput &input,
                                                          bool translate, int select,
                                                          int extraMonster)
{
    for (const auto [interval, alpha] : {std::pair{1.4f, 0.2f}, std::pair{0.7f, 0.6f}})
    {
        auto ghost = input;
        ghost.animationFrame -= interval;
        if (ghost.animationFrame <= 0.f)
            continue;
        ghost.alpha = alpha;
        RenderObject(ghost, translate, select, extraMonster);
    }
    return RenderObject(input, translate, select, extraMonster);
}

void SessionRenderUnit::RenderObjectVisual(const ObjectDrawInput &draw)
{
    BMD *b = &Models[draw.type];
    TheMapProcess().RenderObjectVisual(draw, b);
}

void SessionRenderUnit::RenderWorldObject(const OBJECT &object)
{
    ObjectDrawInput draw(&object);
    if (EditFlag == EDIT_NONE && !object.RigidPoseDirty)
        draw.rigidPose = object.RigidPose.get();
    draw.bones = RenderObject(draw);
    TheMapProcess().RenderObjectVisual(draw, &Models[draw.type]);
#ifdef CSK_DEBUG_RENDER_BOUNDINGBOX
    if (g_bRenderBoundingBox)
        RenderBoundingBox(&object);
#endif
}

void SessionRenderUnit::PrepareWorldObjectDrawGroups(OBJECT_BLOCK &block)
{
    block.DrawGroups.clear();
    for (auto *object = block.Head; object; object = object->Next)
    {
        BMD *model = nullptr;
        if (object->RigidPose && !object->RigidPoseDirty && object->Type != MODEL_WATERSPOUT &&
            object->BlendMesh == -1 && object->HiddenMesh == -1 && object->RenderType == 0)
        {
            auto &candidate = Models[object->Type];
            if (!candidate.rigidInstanceMeshes_.empty())
                model = &candidate;
        }
        if (!block.DrawGroups.empty() && block.DrawGroups.back().model == model &&
            (!model || block.DrawGroups.back().lighting == object->LightEnable))
            block.DrawGroups.back().end = object->Next;
        else
            block.DrawGroups.push_back({object, object->Next, model, object->LightEnable});
    }
    block.DrawGroupsDirty = false;
}

void SessionRenderUnit::RenderRigidObjectGroup(OBJECT_BLOCK &block,
                                               const WorldObjectDrawGroup &group)
{
    auto &instances = block.DrawInstances;
    instances.clear();
    auto &model = *group.model;
    for (auto *object = group.first; object != group.end; object = object->Next)
    {
        if (!object->Live || !object->Visible)
            continue;
        // Fades and buff passes keep their original position in the draw order.
        if (object->Alpha < 0.99f || object->RigidPoseDirty || object->m_BuffMap.isBuff())
        {
            model.RenderRigidInstances(instances);
            instances.clear();
            RenderWorldObject(*object);
            continue;
        }
        ObjectDrawInput draw(object);
        BodyLight(draw, &model);
        const auto &transform = object->RigidPose->transform;
        instances.push_back(
            {transform.row0,
             transform.row1,
             transform.row2,
             {model.BodyLight[0], model.BodyLight[1], model.BodyLight[2], draw.alpha},
             {model.BodyLight[0], model.BodyLight[1], model.BodyLight[2], 1.f},
             {}});
        if (!model.batchRigidPlacements_)
        {
            model.RenderRigidInstances(instances);
            instances.clear();
        }
    }
    model.RenderRigidInstances(instances);
}

void SessionRenderUnit::RenderObjects()
{
    auto &maps = TheMapProcess();
    const bool cullBlocks = maps.Presentation().objectBlockCulling;
    bool grouped =
        WorldPrimaryModel::UsesRigidGeometry(gMapManager.ContextMap(), MODEL_WORLD_OBJECT) &&
        EditFlag == EDIT_NONE && !LegacyRender().UsesPolygonLineMode();
#ifdef CSK_DEBUG_RENDER_BOUNDINGBOX
    grouped &= !g_bRenderBoundingBox;
#endif
    for (auto &block : ObjectBlock)
    {
        if (cullBlocks && !block.Visible)
            continue;
        if (!grouped)
        {
            for (auto *object = block.Head; object; object = object->Next)
            {
                if (!object->Live || !object->Visible || !maps.ObjectEffectsVisible(*object))
                    continue;
                RenderWorldObject(*object);
            }
            continue;
        }
        if (block.DrawGroupsDirty)
            PrepareWorldObjectDrawGroups(block);
        for (const auto &group : block.DrawGroups)
        {
            if (group.model)
                RenderRigidObjectGroup(block, group);
            else
                for (auto *object = group.first; object != group.end; object = object->Next)
                {
                    if (!object->Live || !object->Visible || !maps.ObjectEffectsVisible(*object))
                        continue;
                    RenderWorldObject(*object);
                }
        }
    }
}

void SessionRenderUnit::SortInBlockByType()
{
    WorldObjectDetail::SortObj_t sortMap;
    for (int i = 0; i < 16; ++i)
    {
        for (int j = 0; j < 16; ++j)
        {
            OBJECT_BLOCK *ob = &ObjectBlock[i * 16 + j];
            OBJECT *o = ob->Head;
            while (1)
            {
                if (o == NULL)
                    break;
                sortMap[o->Type].push_back(o);
                o = o->Next;
            }

            o = ob->Head;
            for (WorldObjectDetail::SortObj_t::iterator iter = sortMap.begin();
                 iter != sortMap.end(); ++iter)
            {
                WorldObjectDetail::ObjectPtrVec_t &rObjectVec = iter->second;
                for (WorldObjectDetail::ObjectPtrVec_t::iterator iter2 = rObjectVec.begin();
                     iter2 != rObjectVec.end(); ++iter2)
                {
                    o = *iter2;
                    o = o->Next;
                }
            }

            sortMap.clear();
        }
    }
}

void SessionRenderUnit::InstallWorldPlacements(const WorldFileData &data)
{
    if (data.SkippedPlacements() != 0)
        g_ErrorReport.Write(L"World%d placements: skipped %u unusable records\r\n", data.AssetSet(),
                            static_cast<unsigned>(data.SkippedPlacements()));
    for (const auto &placement : data.Placements())
    {
        auto position = placement.position;
        auto angle = placement.angle;
        CreateObject(placement.type, position.data(), angle.data(), placement.scale);
    }
}

bool SessionRenderUnit::SaveObjects(wchar_t *FileName, int iMapNumber)
{
    FILE *fp = _wfopen(FileName, L"wb");

    short ObjectCount = 0;
    int CounterPoint = 3;
    BYTE Version = 0;
    fwrite(&Version, sizeof(BYTE), 1, fp);
    fwrite(&iMapNumber, 1, 1, fp);
    fseek(fp, 4, SEEK_SET);
    for (int i = 0; i < 16; i++)
    {
        for (int j = 0; j < 16; j++)
        {
            OBJECT_BLOCK *ob = &ObjectBlock[i * 16 + j];
            OBJECT *o = ob->Head;
            while (1)
            {
                if (o != NULL)
                {
                    if (o->Live)
                    {
                        fwrite(&o->Type, 2, 1, fp);
                        fwrite(o->Position, sizeof(vec3_t), 1, fp);
                        fwrite(o->Angle, sizeof(vec3_t), 1, fp);
                        fwrite(&o->Scale, sizeof(float), 1, fp);
                    }
                    ObjectCount++;
                    if (o->Next == NULL)
                        break;
                    o = o->Next;
                }
                else
                    break;
            }
        }
    }
    int EndPoint = ftell(fp);
    fseek(fp, 2, SEEK_SET);
    fwrite(&ObjectCount, 2, 1, fp);
    fseek(fp, EndPoint, SEEK_SET);

    fclose(fp);

    {
        fp = _wfopen(FileName, L"rb");
        fseek(fp, 0, SEEK_END);
        int EncBytes = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        auto *EncData = new unsigned char[EncBytes];
        fread(EncData, 1, EncBytes, fp);
        fclose(fp);

        int DataBytes = MapFileEncrypt(NULL, EncData, EncBytes);
        auto *Data = new unsigned char[DataBytes];
        MapFileEncrypt(Data, EncData, EncBytes);
        delete[] EncData;

        fp = _wfopen(FileName, L"wb");
        fwrite(Data, DataBytes, 1, fp);
        fclose(fp);
        delete[] Data;
    }
    return true;
}

void SessionRenderUnit::SaveTrapObjects(wchar_t *FileName)
{
    FILE *fp = _wfopen(FileName, L"wt");
    fwprintf(fp, L"0\n");
    for (int i = 0; i < 16; i++)
    {
        for (int j = 0; j < 16; j++)
        {
            OBJECT_BLOCK *ob = &ObjectBlock[i * 16 + j];
            OBJECT *o = ob->Head;
            while (1)
            {
                if (o != NULL)
                {
                    if ((gMapManager.ContextMap() == WD_1DUNGEON &&
                         (o->Type == 39 || o->Type == 40 || o->Type == 51)) ||
                        (gMapManager.ContextMap() == WD_4LOSTTOWER && (o->Type == 25)))
                    {
                        int Type = 0;
                        switch (o->Type)
                        {
                        case 39:
                            Type = 100;
                            break;
                        case 40:
                            Type = 101;
                            break;
                        case 51:
                            Type = 102;
                            break;
                        case 25:
                            Type = 103;
                            break;
                        }
                        fwprintf(fp, L"%4d %4d 0 %4d %4d %4d\n", Type, gMapManager.ContextMap(),
                                 (BYTE)(o->Position[0] / TERRAIN_SCALE),
                                 (BYTE)(o->Position[1] / TERRAIN_SCALE),
                                 (BYTE)((o->Angle[2] + 22.5f) / 360.f * 8.f + 1.f) % 8);
                    }
                    if (o->Next == NULL)
                        break;
                    o = o->Next;
                }
                else
                    break;
            }
        }
    }
    fwprintf(fp, L"end\n");
    fclose(fp);
}

void SessionRenderUnit::RenderCloudLowLevel(int index, int Type)
{
    OBJECT *o = &g_CloudsLow;

    o->Alpha = 1.f;
    o->Live = true;
    o->Type = Type;
    o->SubType = 0;
    o->Scale = 10.f;

    if (o->LifeTime != 10)
    {
        Vector(0.f, 0.f, 0.f, o->StartPosition);
        Vector(0.f, 0.f, 0.f, o->Position);
    }
    o->LifeTime = 10;

    Vector(0.f, 0.f, 0.f, o->Angle);
    Vector(0.f, 0.f, 0.f, o->HeadAngle);
    Vector(0.f, 0.f, 0.f, o->HeadTargetAngle);

    o->EnableBoneMatrix = false;
    o->CurrentAction = 0;
    o->PriorAction = 0;
    o->AnimationFrame = 1.f;
    o->PriorAnimationFrame = 1.f;

    o->BlendMeshLight = 1.f;
    o->SetLightEnable(true);

    o->SetHiddenMesh(-1);

    if (index != 0)
    {
        vec3_t delPosition;
        delPosition[0] = (Hero->Object.Position[0] - o->Position[0]);
        delPosition[1] = (Hero->Object.Position[1] - o->Position[1]);
        delPosition[2] = 0.f;

        float Matrix[3][4];
        vec3_t angle;

        Vector(0.f, 0.f, -45.f, angle);
        AngleMatrix(angle, Matrix);
        VectorRotate(delPosition, Matrix, delPosition);

        if (index == 1)
        {
            o->StartPosition[0] -= sinf(WorldTime * 0.0002f) * 0.001f;
            o->StartPosition[1] += cosf(WorldTime * 0.0002f) * 0.001f;
        }
        else
        {
            o->StartPosition[0] += sinf(WorldTime * 0.0002f) * 0.001f;
            o->StartPosition[1] -= cosf(WorldTime * 0.0002f) * 0.001f;
        }

        if (index == 1)
        {
            o->SetBlendMesh(-1);
        }
        else
        {
            o->BlendMeshLight = 1.f;
            o->SetBlendMesh(0);
        }
        o->BlendMeshTexCoordU = o->StartPosition[0];
        o->BlendMeshTexCoordV = o->StartPosition[1];
        Vector(Hero->Object.Position[0], Hero->Object.Position[1],
               Hero->Object.Position[2] - 150.f + (index * 20), o->Position);

        RenderObject(o);
    }
}

// Helper: Handle item falling animation

// Helper: Handle item on ground (set angle and camera rotation)

void SessionRenderUnit::RenderZen(int itemIndex, ITEM_t *item, vec3_t light)
{
    auto o = &item->Object;
    auto k = itemIndex;
    vec3_t tempPosition;
    VectorCopy(o->Position, tempPosition);

    int coinCount = static_cast<int>(sqrtf(static_cast<float>(Items[k].Item.Level))) / 2;

    coinCount = std::max<int>(std::min<int>(coinCount, 80), 3);

    vec3_t randomRadius;
    vec3_t randomAngle;
    vec3_t randomPosition;
    float angleMatrix[3][4];

    BMD *b = &Models[MODEL_ZEN];
    b->BodyScale = o->Scale;

    BoneScale = 1.f;
    BodyLight(o, b);

    constexpr auto alpha = 1.0f;
    b->BeginRender(alpha);
    b->BeginRenderCoinHeap();
    int target_vertex_index = -1;
    for (int i = 0; i < coinCount; ++i)
    {
        // Get a random angle
        Vector(0.f, 0.f, static_cast<float>(RandomTable[(k * 20 + i) % 100] % 360), randomAngle);

        // And a random radius
        const auto maxRadius = coinCount + 20;
        Vector(static_cast<float>(RandomTable[(k + i) % 100] % maxRadius), 0.f, 0.f, randomRadius);

        // Calculate the position based on the random angle and radius
        AngleMatrix(randomAngle, angleMatrix);
        VectorRotate(randomRadius, angleMatrix, randomPosition);

        VectorAdd(tempPosition, randomPosition, o->Position);
        VectorCopy(o->Position, b->BodyOrigin);
        b->Transform(BoneTransform, o->BoundingBoxMin, o->BoundingBoxMax, &o->OBB, true);

        target_vertex_index = b->AddToCoinHeap(i, target_vertex_index);
    }

    b->EndRenderCoinHeap(coinCount);
    b->EndRender();

    VectorCopy(tempPosition, o->Position);
}

// Render dropped items and fall animation and camera rotation for items on the ground
void SessionRenderUnit::RenderItems()
{
    for (int i = 0; i < MAX_ITEMS; i++)
    {
        OBJECT *o = &Items[i].Object;
        if (o->Live)
        {
            float cullRadius = DEFAULT_CULL_RADIUS_ITEM;
            o->Visible = TestFrustrum(o->Position, cullRadius);

            if (o->Visible)
            {
                auto draw = sessionKeeper_.Visual()->PrepareDroppedItemDraw(*o);
                int Type = o->Type;
                if (o->Type >= MODEL_HELM && o->Type < MODEL_BOOTS + MAX_ITEM_INDEX)
                    Type = MODEL_PLAYER;
                else if (o->Type == MODEL_POTION + 12)
                {
                    int Level = Items[i].Item.Level;
                    if (Level == 0)
                        Type = MODEL_EVENT;
                    else if (Level == 2)
                        Type = MODEL_EVENT + 1;
                }
                BMD *b = &Models[Type];
                b->CurrentAction = 0;
                b->Skin = gCharacterManager.GetBaseClass(Hero->Class); // ???
                b->CurrentAction = o->CurrentAction;
                VectorCopy(o->Position, b->BodyOrigin);
                ItemHeight(o->Type, b);
                b->Animation(BoneTransform, o->AnimationFrame, o->PriorAnimationFrame,
                             o->PriorAction, draw.angle, o->HeadAngle, false, false);

                if (o->Type >= MODEL_HELM && o->Type < MODEL_BOOTS + MAX_ITEM_INDEX)
                    Type = o->Type;
                b = &Models[Type];
                vec3_t Light;
                RequestTerrainLight(o->Position[0], o->Position[1], Light);
                VectorAdd(Light, o->Light, Light);
                if (o->Type == MODEL_ZEN) // Zen
                {
                    RenderZen(i, &Items[i], Light);
                }
                else if (o->Type == MODEL_CAPE_OF_OVERRULE)
                {
                    Vector(1.0f, 1.0f, 1.0f, Light);
                }

                draw.position[2] = TheMapProcess().ItemDrawHeight(*o, i);

                RenderPartObject(draw, o->Type, NULL, Light, o->Alpha, Items[i].Item.Level,
                                 Items[i].Item.ExcellentFlags, Items[i].Item.AncientDiscriminator,
                                 true, true, true);

                vec3_t Position;
                VectorCopy(o->Position, Position);
                Position[2] += 30.f;
                int ScreenX, ScreenY;
                cameraProjection_.WorldToScreen(g_Camera, Position, &ScreenX, &ScreenY);
                o->ScreenX = ScreenX;
                o->ScreenY = ScreenY;
            }
        }
    }
}
void SessionRenderUnit::PartObjectColor2(int Type, float Alpha, float Bright, vec3_t Light,
                                         bool ExtraMon)
{
    int Color = 0;
    if (Type == MODEL_SILVER_BOW || Type == MODEL_BLUEWING_CROSSBOW)
    {
        Color = 2;
    }
    else if (Type == MODEL_LIGHTING_SWORD || Type == MODEL_LEGENDARY_STAFF)
    {
        Color = 2;
    }
    else if (Type == MODEL_THUNDER_BLADE)
    {
        Color = 0;
    }
    else if (Type == MODEL_CELESTIAL_BOW)
    {
        Color = 0;
    }
    else if (Type == MODEL_DRAGON_SOUL_STAFF)
    {
        Color = 0;
    }
    else if (Type == MODEL_ARMORINVEN_60 || Type == MODEL_ARMORINVEN_61 ||
             Type == MODEL_ARMORINVEN_62)
    {
        Color = 0;
    }
    else
    {
        int ItemType = Type - MODEL_ITEM;
        if (ItemType / MAX_ITEM_INDEX >= 7 && ItemType / MAX_ITEM_INDEX <= 11)
        {
            switch (ItemType % MAX_ITEM_INDEX)
            {
            case 0:
                Color = 0;
                break;
            case 1:
                Color = 0;
                break;
            case 2:
                Color = 0;
                break;
            case 3:
                Color = 0;
                break;
            case 4:
                Color = 1;
                break;
            case 5:
                Color = 0;
                break;
            case 6:
                Color = 0;
                break;
            case 7:
                Color = 0;
                break;
            case 8:
                Color = 0;
                break;
            case 9:
                Color = 0;
                break;
            case 10:
                Color = 0;
                break;
            case 11:
                Color = 0;
                break;
            case 12:
                Color = 0;
                break;
            case 13:
                Color = 0;
                break;
            case 14:
                Color = 1;
                break;
            case 15:
                Color = 1;
                break;
            case 16:
                Color = 0;
                break;
            case 17:
                Color = 1;
                break;
            case 18:
                Color = 2;
                break;
            case 19:
                Color = 0;
                break;
            case 21:
                Color = 3;
                break;
            case 39:
                Color = 1;
                break;
            case 40:
                Color = 1;
                break;
            case 41:
                Color = 1;
                break;
            case 42:
                Color = 1;
                break;
            case 43:
                Color = 2;
                break;
            case 44:
                Color = 3;
                break;
            case 45:
                Color = 0;
                break;
            case 59:
                Color = 0;
                break;
            case 60:
                Color = 0;
                break;
            case 61:
                Color = 0;
                break;
            }
        }
    }
    WorldObjectDetail::ApplyPartModulation(Color, Alpha, Bright, Light);
}

void SessionRenderUnit::RenderPotionGlow(BMD &model, const ObjectDrawInput &draw, int renderType,
                                         float alpha)
{
    model.RenderBody(renderType, alpha, draw.blendMesh, draw.blendLight * 1.5f, draw.blendU,
                     draw.blendV);
    model.RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, alpha, draw.blendMesh, draw.blendLight / 4.f,
                     WorldTime * 0.005f, -WorldTime * 0.005f, 0);
    vec3_t light{1.f, 0.f, 0.f}, relative{}, position;
    model.TransformPosition(BoneTransform[1], relative, position, true);
    CreateSprite(BITMAP_LIGHT, position, 3.f, light, draw.source, 0.f);
}

void SessionRenderUnit::NextGradeObjectRender(const CharacterDrawInput &characterDraw)
{
    const auto *c = characterDraw.source;
    BMD *b = &Models[c->Object.Type];
    vec3_t vRelativePos, vPos, vLight;
    float fLight2, fScale;
    int Level;
    PART_t *w;

    int bornIndex[2]{}; // left, right;
    int gradeType[2]{}; // left, right;

    for (int k = 0; k < MAX_BODYPART; k++)
    {
        PART_t drawPart = c->BodyPart[k];
        w = &drawPart;
        Level = w->Level;

        if (k == 0)
            continue;
        if (Level < 15 || w->Type == -1)
            continue;

        switch (k)
        {
        case 1: {
            bornIndex[0] = 20;
            bornIndex[1] = -1;
            gradeType[0] = MODEL_15GRADE_ARMOR_OBJ_HEAD;
            gradeType[1] = -1;
        }
        break;
        case 2: {
            bornIndex[0] = 35;
            bornIndex[1] = 26;
            gradeType[0] = MODEL_15GRADE_ARMOR_OBJ_BODYLEFT;
            gradeType[1] = MODEL_15GRADE_ARMOR_OBJ_BODYRIGHT;
        }
        break;
        case 3: {
            bornIndex[0] = 3;
            bornIndex[1] = 10;
            gradeType[0] = MODEL_15GRADE_ARMOR_OBJ_PANTLEFT;
            gradeType[1] = MODEL_15GRADE_ARMOR_OBJ_PANTRIGHT;
        }
        break;
        case 4: {
            bornIndex[0] = 36;
            bornIndex[1] = 27;
            gradeType[0] = MODEL_15GRADE_ARMOR_OBJ_ARMLEFT;
            gradeType[1] = MODEL_15GRADE_ARMOR_OBJ_ARMRIGHT;
        }
        break;
        case 5: {
            bornIndex[0] = 4;
            bornIndex[1] = 11;
            gradeType[0] = MODEL_15GRADE_ARMOR_OBJ_BOOTLEFT;
            gradeType[1] = MODEL_15GRADE_ARMOR_OBJ_BOOTRIGHT;
        }
        break;
        default:
            bornIndex[0] = -1;
            bornIndex[1] = -1;
            gradeType[0] = -1;
            gradeType[1] = -1;
            break;
        }

        const OBJECT *o = &c->Object;

        for (int m = 0; m < 2; m++)
        {
            if (gradeType[m] == -1)
                continue;

            switch (Level)
            {
            case 15: // +15
            {
                w->LinkBone = bornIndex[m];
                const auto *itemBones = RenderLinkObject(
                    0.f, 0.f, 0.f, characterDraw, w, gradeType[m], 0, 0, true, true, 0, true,
                    CharacterLinkedItemVisual::GradeHead + (k - 1) * 2 + m);

                if (!itemBones)
                    continue;
                b->TransformByBoneMatrix(vPos, itemBones[0], characterDraw.object.position);

                fLight2 = absf(sinf(WorldTime * 0.01f));
                Vector(0.2f * fLight2, 0.4f * fLight2, 1.0f * fLight2, vLight);
                CreateSprite(BITMAP_MAGIC, vPos, 0.12f, vLight, o);

                Vector(0.4f, 0.7f, 1.0f, vLight);
                CreateSprite(BITMAP_SHINY + 5, vPos, 0.4f, vLight, o);

                Vector(0.1f, 0.3f, 1.0f, vLight);
                CreateSprite(BITMAP_PIN_LIGHT, vPos, 0.6f, vLight, o, 90.0f);
            }
            break;
            }
        }
    } //for
}
void SessionRenderUnit::CreateShadowAngle()
{
    for (int i = 0; i < 16; i++)
    {
        for (int j = 0; j < 16; j++)
        {
            OBJECT_BLOCK *ob = &ObjectBlock[i * 16 + j];
            OBJECT *o = ob->Head;
            while (1)
            {
                if (o != NULL)
                {
                    if (o->Live)
                    {
                        o->Visible = true;
                        ShadowAngle(o);
                    }
                    if (o->Next == NULL)
                        break;
                    o = o->Next;
                }
                else
                    break;
            }
        }
    }
}

#ifdef CSK_DEBUG_RENDER_BOUNDINGBOX
void SessionRenderUnit::RenderBoundingBox(const OBJECT *pObj)
{
    EnableAlphaBlend();
    glPushMatrix();

    float Matrix[3][4];
    AngleMatrix(pObj->Angle, Matrix);
    Matrix[0][3] = pObj->Position[0];
    Matrix[1][3] = pObj->Position[1];
    Matrix[2][3] = pObj->Position[2];

    vec3_t BoundingVertices[8];
    Vector(pObj->BoundingBoxMax[0], pObj->BoundingBoxMax[1], pObj->BoundingBoxMax[2],
           BoundingVertices[0]);
    Vector(pObj->BoundingBoxMax[0], pObj->BoundingBoxMax[1], pObj->BoundingBoxMin[2],
           BoundingVertices[1]);
    Vector(pObj->BoundingBoxMax[0], pObj->BoundingBoxMin[1], pObj->BoundingBoxMax[2],
           BoundingVertices[2]);
    Vector(pObj->BoundingBoxMax[0], pObj->BoundingBoxMin[1], pObj->BoundingBoxMin[2],
           BoundingVertices[3]);
    Vector(pObj->BoundingBoxMin[0], pObj->BoundingBoxMax[1], pObj->BoundingBoxMax[2],
           BoundingVertices[4]);
    Vector(pObj->BoundingBoxMin[0], pObj->BoundingBoxMax[1], pObj->BoundingBoxMin[2],
           BoundingVertices[5]);
    Vector(pObj->BoundingBoxMin[0], pObj->BoundingBoxMin[1], pObj->BoundingBoxMax[2],
           BoundingVertices[6]);
    Vector(pObj->BoundingBoxMin[0], pObj->BoundingBoxMin[1], pObj->BoundingBoxMin[2],
           BoundingVertices[7]);

    vec3_t TransformVertices[8];
    for (int j = 0; j < 8; j++)
    {
        VectorTransform(BoundingVertices[j], Matrix, TransformVertices[j]);
    }

    //glBegin(GL_QUADS);
    glBegin(GL_LINES);
    glColor3f(0.9f, 0.2f, 0.2f);
    glTexCoord2f(1.0F, 1.0F);
    glVertex3fv(TransformVertices[7]);
    glTexCoord2f(1.0F, 0.0F);
    glVertex3fv(TransformVertices[6]);
    glTexCoord2f(0.0F, 0.0F);
    glVertex3fv(TransformVertices[4]);
    glTexCoord2f(0.0F, 1.0F);
    glVertex3fv(TransformVertices[5]);

    glColor3f(0.9f, 0.2f, 0.2f);
    glTexCoord2f(0.0F, 1.0F);
    glVertex3fv(TransformVertices[0]);
    glTexCoord2f(1.0F, 1.0F);
    glVertex3fv(TransformVertices[2]);
    glTexCoord2f(1.0F, 0.0F);
    glVertex3fv(TransformVertices[3]);
    glTexCoord2f(0.0F, 0.0F);
    glVertex3fv(TransformVertices[1]);

    glColor3f(0.9f, 0.6f, 0.6f);
    glTexCoord2f(1.0F, 1.0F);
    glVertex3fv(TransformVertices[7]);
    glTexCoord2f(1.0F, 0.0F);
    glVertex3fv(TransformVertices[3]);
    glTexCoord2f(0.0F, 0.0F);
    glVertex3fv(TransformVertices[2]);
    glTexCoord2f(0.0F, 1.0F);
    glVertex3fv(TransformVertices[6]);

    glColor3f(0.9f, 0.6f, 0.6f);
    glTexCoord2f(0.0F, 1.0F);
    glVertex3fv(TransformVertices[0]);
    glTexCoord2f(1.0F, 1.0F);
    glVertex3fv(TransformVertices[1]);
    glTexCoord2f(1.0F, 0.0F);
    glVertex3fv(TransformVertices[5]);
    glTexCoord2f(0.0F, 0.0F);
    glVertex3fv(TransformVertices[4]);

    glColor3f(0.9f, 0.4f, 0.4f);
    glTexCoord2f(1.0F, 1.0F);
    glVertex3fv(TransformVertices[7]);
    glTexCoord2f(1.0F, 0.0F);
    glVertex3fv(TransformVertices[5]);
    glTexCoord2f(0.0F, 0.0F);
    glVertex3fv(TransformVertices[1]);
    glTexCoord2f(0.0F, 1.0F);
    glVertex3fv(TransformVertices[3]);

    glColor3f(0.9f, 0.4f, 0.4f);
    glTexCoord2f(0.0F, 1.0F);
    glVertex3fv(TransformVertices[0]);
    glTexCoord2f(1.0F, 1.0F);
    glVertex3fv(TransformVertices[4]);
    glTexCoord2f(1.0F, 0.0F);
    glVertex3fv(TransformVertices[6]);
    glTexCoord2f(0.0F, 0.0F);
    glVertex3fv(TransformVertices[2]);
    glEnd();

    glPopMatrix();
}
#endif // CSK_DEBUG_RENDER_BOUNDINGBOX

// OMF-00605
// OMF-00606
// OMF-00633
