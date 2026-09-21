#include "render/ModelGeometry.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationKeeper.h"
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
#include "render/FrameTape.h"
#include "render/ModelResources.h"
#include "render/Terrain.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionPresentation.h"
#include "session/SessionRender.h"
#include "session/SessionWorkspace.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"

namespace Render::Models
{
void PrepareClothGridTopology(Mesh_t &mesh, int columns, int rows)
{
    constexpr int cornerX[]{0, 1, 0, 1, 1, 0};
    constexpr int cornerY[]{0, 0, 1, 0, 1, 1};
    for (int index = 0; index < mesh.NumTriangles; ++index)
    {
        const int side = index % 2;
        const int column = (index / 2) % (columns - 1);
        const int row = (index / 2) / (columns - 1);
        auto &triangle = mesh.Triangles[index];
        for (int corner = 0; corner < 3; ++corner)
        {
            const int x = column + cornerX[side * 3 + corner];
            const int y = row + cornerY[side * 3 + corner];
            triangle.VertexIndex[corner] = y * columns + x;
            triangle.NormalIndex[corner] =
                (x * mesh.NumNormals / columns + y * mesh.NumNormals / rows) / 3;
            triangle.TexCoordIndex[corner] = triangle.VertexIndex[corner];
            mesh.TexCoords[triangle.TexCoordIndex[corner]] = {static_cast<float>(x) / (columns - 1),
                                                              static_cast<float>(y) / (rows - 1)};
        }
    }
}
} // namespace Render::Models

// Construction/Destruction

CSideHair::CSideHair(SessionKeeper &keeper)
    : CShadowVolume(keeper), g_Camera(keeper.CameraStateObject())
{
}

CSideHair::~CSideHair()
{
}

void CSideHair::Create(vec3_t ppVertexTransformed[MAX_MESH][MAX_VERTICES], BMD *b,
                       const ObjectDrawInput &draw, bool SkipTga)
{
    m_iNumEdge = 0;
    texturePhase_ = static_cast<std::uint32_t>(draw.animationFrame * 1000.f);
    VectorSubtract(Hero->Object.Position, g_Camera.Position, m_vLight);
    VectorNormalize(m_vLight);

    if (draw.alpha < 0.01f)
    {
        return;
    }
    b->EnsureCpuTransforms();
    short nHiddenMesh = draw.hiddenMesh;
    short nBlendMesh = draw.blendMesh;
    if (nHiddenMesh == -2 || nBlendMesh == -2)
    {
        return;
    }

    int iNumTriangles = 0;
    for (int i = 1; i < 2; ++i)
    {
        if (nHiddenMesh == i || nBlendMesh == i)
        {
            continue;
        }
        if (Bitmaps[b->IndexTexture[i]].Components == 4)
        {
            if (SkipTga)
                continue;
        }
        iNumTriangles += b->Meshs[i].NumTriangles;
    }
    m_iNumEdge = 0;
    const auto required = static_cast<std::size_t>(iNumTriangles) * 3;
    if (edges_.size() < required)
        edges_.resize(required);
    m_pEdges = edges_.data();

    for (short i = 1; i < 2; ++i)
    {
        if (nHiddenMesh == i || nBlendMesh == i)
        {
            continue;
        }

        bool Tga = false;
        if (Bitmaps[b->IndexTexture[i]].Components == 4)
        {
            Tga = true;
            if (SkipTga)
                continue;
        }
        DeterminateSilhouette(i, ppVertexTransformed, b->Meshs[i].NumTriangles,
                              b->Meshs[i].Triangles, Tga);
    }
}

void CSideHair::Render(vec3_t ppVertexTransformed[MAX_MESH][MAX_VERTICES],
                       vec3_t ppLightTransformed[MAX_MESH][MAX_VERTICES])
{
    for (int i = 0; i < m_iNumEdge; ++i)
    {
        // Stable per-edge texture jitter changes with the animation sample, never draw count.
        std::uint32_t sample =
            texturePhase_ * 1664525u + static_cast<std::uint32_t>(i) * 1013904223u;
        sample ^= sample >> 16;
        RenderLine(ppVertexTransformed[m_pEdges[i].m_nMesh][m_pEdges[i].m_nVertexIndex[0]],
                   ppVertexTransformed[m_pEdges[i].m_nMesh][m_pEdges[i].m_nVertexIndex[1]],
                   static_cast<float>(sample % 100) * 0.01f);
    }
}

void CSideHair::RenderLine(const vec3_t v1, const vec3_t v2, float fTextureV)
{
    vec3_t p1, p2, d;

    glColor3f(1.f, 1.f, 1.f);
    VectorSubtract(v2, v1, d);
    const float fLength = VectorLength(d);
    float fTextureMove = 0.0f;
    fTextureMove = (50.0f - fLength) * 0.5f / 50.0f;

    VectorCopy(v1, p1);
    VectorCopy(v2, p2);
    VectorSubtract(p2, p1, d);
    VectorScale(d, 0.1f, d);
    VectorSubtract(p1, d, p1);
    VectorAdd(p2, d, p2);

    glColor3f(1.f, 1.f, 1.f);
    BindTexture(BITMAP_ROBE + 4);
    EnableAlphaBlendMinus();
    //EnableAlphaTest();
    //g_OpenglLib.DisableTexture();
    //g_OpenglLib.Disable(GL_CULL_FACE);
    /*glBegin(GL_QUADS);
    glTexCoord2f(0.f,0.f+fTextureMove);glVertex3f(p1[0]-Scale,p1[1],p1[2]);
    glTexCoord2f(0.f,1.f-fTextureMove);glVertex3f(p2[0]-Scale,p2[1],p2[2]);
    glTexCoord2f(1.f,1.f-fTextureMove);glVertex3f(p2[0]+Scale,p2[1],p2[2]);
    glTexCoord2f(1.f,0.f+fTextureMove);glVertex3f(p1[0]+Scale,p1[1],p1[2]);
    glEnd();
    glBegin(GL_QUADS);
    glTexCoord2f(0.f,0.f+fTextureMove);glVertex3f(p1[0],p1[1]-Scale,p1[2]);
    glTexCoord2f(0.f,1.f-fTextureMove);glVertex3f(p2[0],p2[1]-Scale,p2[2]);
    glTexCoord2f(1.f,1.f-fTextureMove);glVertex3f(p2[0],p2[1]+Scale,p2[2]);
    glTexCoord2f(1.f,0.f+fTextureMove);glVertex3f(p1[0],p1[1]+Scale,p1[2]);
    glEnd();
    glBegin(GL_QUADS);
    glTexCoord2f(0.f,0.f+fTextureMove);glVertex3f(p1[0],p1[1],p1[2]-Scale);
    glTexCoord2f(0.f,1.f-fTextureMove);glVertex3f(p2[0],p2[1],p2[2]-Scale);
    glTexCoord2f(1.f,1.f-fTextureMove);glVertex3f(p2[0],p2[1],p2[2]+Scale);
    glTexCoord2f(1.f,0.f+fTextureMove);glVertex3f(p1[0],p1[1],p1[2]+Scale);
    glEnd();*/
    vec3_t vOrtho;
    CrossProduct(m_vLight, d, vOrtho);
    VectorNormalize(vOrtho);
    VectorScale(vOrtho, 10.f, vOrtho);
    glBegin(GL_QUADS);
    //glColor3fv( c1);
    glTexCoord2f(0.f, 0.f + fTextureMove + fTextureV);
    glVertex3f(p1[0] - vOrtho[0], p1[1] - vOrtho[1], p1[2] - vOrtho[2]);
    //glColor3fv( c2);
    glTexCoord2f(0.f, 1.f - fTextureMove + fTextureV);
    glVertex3f(p2[0] - vOrtho[0], p2[1] - vOrtho[1], p2[2] - vOrtho[2]);
    glTexCoord2f(1.f, 1.f - fTextureMove + fTextureV);
    glVertex3f(p2[0] + vOrtho[0], p2[1] + vOrtho[1], p2[2] + vOrtho[2]);
    //glColor3fv( c1);
    glTexCoord2f(1.f, 0.f + fTextureMove + fTextureV);
    glVertex3f(p1[0] + vOrtho[0], p1[1] + vOrtho[1], p1[2] + vOrtho[2]);
    glEnd();
    //g_OpenglLib.Enable(GL_CULL_FACE);
}

CharacterDrawInput ItemBirthPose::SampleParent(SessionKeeper &keeper,
                                               const CharacterDrawInput &character, double time,
                                               float fraction,
                                               std::array<vec34_t, MAX_BONES> &characterBones)
{
    auto parent = character;
    const OBJECT &source = *parent.object.source;
    auto &characterModel = keeper.ModelPoolObject()[parent.object.type];
    const auto parentPose =
        parent.object.preparedPose
            ? *parent.object.preparedPose
            : AnimationPoseSample(parent.object, characterModel.BoneHead, characterModel.BodyHeight,
                                  false, characterModel.PoseAssetIdentity());
    const int parentHead = characterModel.BoneHead;
    const float parentHeight = characterModel.BodyHeight, parentScale = characterModel.BodyScale;
    vec3_t parentOrigin;
    VectorCopy(characterModel.BodyOrigin, parentOrigin);
    parent.object.bones = parentPose.EvaluateAtTime(characterModel, source, time, fraction,
                                                    characterBones.data(), parent.object.bones);
    characterModel.BoneHead = parentHead;
    characterModel.BodyHeight = parentHeight;
    characterModel.BodyScale = parentScale;
    VectorCopy(parentOrigin, characterModel.BodyOrigin);
    vec3_t root;
    source.MotionTrace.Sample(time, fraction, source.Position, root);
    for (int axis = 0; axis < 3; ++axis)
        parent.object.position[axis] += root[axis] - source.Position[axis];
    return parent;
}

ItemBirthPose::ItemBirthPose(SessionKeeper &keeper, SessionVisualUnit &visual,
                             const CharacterDrawInput &character,
                             const CharacterLinkedItemVisual &entry,
                             const EffectEmissionScope &birth)
    : model_(keeper.ModelPoolObject()[entry.item.Type]), draw_(&entry.item),
      savedScale_(model_.BodyScale), savedHeight_(model_.BodyHeight), savedHead_(model_.BoneHead),
      view_(entry.item.BoneTransform)
{
    VectorCopy(model_.BodyOrigin, savedOrigin_);
    const double time = keeper.FrameWorldTime();
    const float fraction = birth.FrameFraction();
    std::array<vec34_t, MAX_BONES> characterBones;
    auto parent = SampleParent(keeper, character, time, fraction, characterBones);
    float matrix[3][4];
    vec3_t angle;
    visual.BuildCharacterItemParent(entry.parentOffset[0], entry.parentOffset[1],
                                    entry.parentOffset[2], parent, entry.playback, entry.item.Type,
                                    entry.linked, entry.rightHand, matrix, model_.BodyOrigin, angle,
                                    model_.BodyScale, entry.scaleOverride);
    if (entry.item.Type == MODEL_BOSS_HEAD)
        angle[2] = float(time - birth.SceneRemainingFrames() * 1000.0 /
                                    keeper.ApplicationConfig().legacyReferenceFps);
    auto pose = entry.poseSample;
    const auto phase = entry.item.MotionTrace.SampleAnimation(
        time, fraction, {pose.frame, pose.priorFrame, pose.action, pose.priorAction});
    pose.frame = phase.frame;
    pose.priorFrame = phase.priorFrame;
    pose.action = phase.action;
    pose.priorAction = phase.priorAction;
    pose.SetParent(matrix);
    VectorCopy(angle, pose.angle.data());
    VectorCopy(angle, pose.headAngle.data());
    VectorCopy(model_.BodyOrigin, draw_.position);
    VectorCopy(angle, draw_.angle);
    VectorCopy(angle, draw_.headAngle);
    draw_.scale = model_.BodyScale;
    draw_.animationFrame = pose.frame;
    draw_.priorAnimationFrame = pose.priorFrame;
    draw_.action = pose.action;
    draw_.priorAction = pose.priorAction;
    if (pose == entry.poseSample)
        return;
    if (phase.adjacentKeys)
        pose.EvaluateAtFrame(model_, bones_.data());
    else
        pose.Evaluate(model_, bones_.data());
    view_ = bones_.data();
}

ItemBirthPose::ItemBirthPose(SessionKeeper &keeper, const ObjectDrawInput &draw, int type,
                             const vec34_t *preparedBones, const EffectEmissionScope &birth)
    : model_(keeper.ModelPoolObject()[type]), draw_(draw), savedScale_(model_.BodyScale),
      savedHeight_(model_.BodyHeight), savedHead_(model_.BoneHead), view_(preparedBones)
{
    VectorCopy(model_.BodyOrigin, savedOrigin_);
    const double time = keeper.FrameWorldTime();
    const float fraction = birth.FrameFraction();
    const OBJECT &source = *draw.source;
    const AnimationPoseSample prepared(draw, model_.BoneHead, model_.BodyHeight, false,
                                       model_.PoseAssetIdentity());
    auto pose = prepared;
    ObjectMotionTrace::AnimationPhase phase{pose.frame, pose.priorFrame, pose.action,
                                            pose.priorAction};
    if (source.Type == type)
        phase = source.MotionTrace.SampleAnimation(time, fraction, phase);
    pose.frame = draw_.animationFrame = phase.frame;
    pose.priorFrame = draw_.priorAnimationFrame = phase.priorFrame;
    pose.action = draw_.action = phase.action;
    pose.priorAction = draw_.priorAction = phase.priorAction;
    pose.angle[2] +=
        source.MotionTrace.SampleYaw(time, fraction, source.Angle[2]) - source.Angle[2];
    VectorCopy(pose.angle.data(), draw_.angle);
    vec3_t root;
    source.MotionTrace.Sample(time, fraction, source.Position, root);
    for (int axis = 0; axis < 3; ++axis)
        draw_.position[axis] += root[axis] - source.Position[axis];
    VectorCopy(draw_.position, model_.BodyOrigin);
    model_.BodyScale = draw_.scale;
    if (pose == prepared)
        return;
    if (phase.adjacentKeys)
        pose.EvaluateAtFrame(model_, bones_.data());
    else
        pose.Evaluate(model_, bones_.data());
    view_ = bones_.data();
}

ItemBirthPose::~ItemBirthPose()
{
    VectorCopy(savedOrigin_, model_.BodyOrigin);
    model_.BodyScale = savedScale_;
    model_.BodyHeight = savedHeight_;
    model_.BoneHead = savedHead_;
}

ObjectDrawInput ItemBirthPose::Draw() const
{
    auto draw = draw_;
    draw.bones = view_;
    return draw;
}

void AnimationPoseSample::Evaluate(BMD &model, vec34_t *output) const
{
    Evaluate(model, output, false);
}

void AnimationPoseSample::EvaluateAtFrame(BMD &model, vec34_t *output) const
{
    Evaluate(model, output, true);
}

void AnimationPoseSample::Evaluate(BMD &model, vec34_t *output, bool adjacentKeys) const
{
    model.BoneHead = boneHead;
    model.BodyHeight = bodyHeight;
    if (translated && !parent)
    {
        model.BodyScale = scale;
        VectorCopy(origin.data(), model.BodyOrigin);
    }
    if (adjacentKeys)
        model.AnimationAtFrame(
            output, frame, priorFrame, priorAction, angle.data(), headAngle.data(), parent,
            translated, parent ? reinterpret_cast<const float(*)[4]>(parentMatrix.data()) : nullptr,
            action);
    else
        model.Animation(output, frame, priorFrame, priorAction, angle.data(), headAngle.data(),
                        parent, translated,
                        parent ? reinterpret_cast<const float(*)[4]>(parentMatrix.data()) : nullptr,
                        action);
    if (blendBone >= 0)
        model.InterpolationTrans(output[0], output[blendBone], blendWeight);
}

const vec34_t *AnimationPoseSample::EvaluateAtTime(BMD &model, const OBJECT &owner,
                                                   double frameTime, float fraction,
                                                   vec34_t *output, const vec34_t *prepared) const
{
    auto pose = *this;
    const auto phase = owner.MotionTrace.SampleAnimation(
        frameTime, fraction, {pose.frame, pose.priorFrame, pose.action, pose.priorAction});
    pose.frame = phase.frame;
    pose.priorFrame = phase.priorFrame;
    pose.action = phase.action;
    pose.priorAction = phase.priorAction;
    pose.angle[2] = owner.MotionTrace.SampleYaw(frameTime, fraction, pose.angle[2]);
    if (pose.translated)
    {
        vec3_t sampledOrigin;
        owner.MotionTrace.Sample(frameTime, fraction, owner.Position, sampledOrigin);
        for (int axis = 0; axis < 3; ++axis)
            pose.origin[axis] += sampledOrigin[axis] - owner.Position[axis];
    }
    if (prepared != nullptr && pose == *this)
        return prepared;
    if (phase.adjacentKeys)
        pose.EvaluateAtFrame(model, output);
    else
        pose.Evaluate(model, output);
    return output;
}

void AnimationPoseSample::SampleBonePosition(BMD &model, const OBJECT &owner, int bone,
                                             const vec3_t offset, double frameTime, float fraction,
                                             vec3_t position) const
{
    std::array<vec34_t, MAX_BONES> bones;
    EvaluateAtTime(model, owner, frameTime, fraction, bones.data());
    VectorTransform(offset, bones[bone], position);
    VectorScale(position, owner.Scale, position);
    vec3_t origin;
    owner.MotionTrace.Sample(frameTime, fraction, owner.Position, origin);
    VectorAdd(position, origin, position);
}

const vec34_t *SessionDrawPoseStorage::Sample(BMD &model, const AnimationPoseSample &sample)
{
    auto *pose = Allocate(model.NumBones);
    sample.Evaluate(model, pose);
    return pose;
}

#define RENDER_CLOTH
#define ADD_COLLISION

#define RATE_SHORT_SHOULDER (0.6f)

void CPhysicsCloth::Render(SessionRenderUnit &renderer, const vec3_t *pvColor, int iLevel) const
{
    renderer.RenderCloth(*this, pvColor, iLevel);
}

void CPhysicsCloth::RenderCollisions(void)
{
#ifdef RENDER_COLLISION
    glColor3f(1.0f, 1.0f, 0.6f);
    BindTexture(BITMAP_CLOUD);
    CNode<CPhysicsCollision *> *pHead = m_lstCollision.FindHead();
    for (; pHead; pHead = m_lstCollision.GetNext(pHead))
    {
        CPhysicsCollision *pCol = pHead->GetData();
        if (CLT_SPHERE == pCol->GetType())
        {
            CPhysicsColSphere *pColSph = (CPhysicsColSphere *)pCol;

            static GLUquadricObj *pQuad = NULL;
            if (NULL == pQuad)
            {
                pQuad = gluNewQuadric();
            }
            glPushMatrix();
            vec3_t vCenter;
            pColSph->GetCenter(vCenter);
            glTranslatef(vCenter[0], vCenter[1], vCenter[2]);
            gluSphere(pQuad, pColSph->GetRadius() - 2.0f, 20, 20);
            glPopMatrix();
        }
    }
#endif
}

void CPhysicsClothMesh::Render(SessionRenderUnit &renderer, const vec3_t *pvColor, int iLevel) const
{
    Models[m_iBMDType].EnsureCpuTransforms();
    vec3_t vPos;
    for (int iVertex = 0; iVertex < m_iNumVertices; ++iVertex)
    {
        GetPosition(iVertex, &vPos);
        VectorCopy(vPos, VertexTransform[m_iMesh][iVertex]);
    }
}

void CBoneManager::RegisterBone(CHARACTER *character, const std::wstring &name, int bone)
{
    if (character == nullptr)
        return;
    if (!character->SocketSource)
        character->SocketSource = std::make_shared<CharacterSocketSource>(&character->Object);
    character->NamedBones.insert_or_assign(name, bone);
    Admit(&character->Object, character);
}

void CBoneManager::UnregisterBone(CHARACTER *character)
{
    if (character != nullptr)
        Forget(&character->Object);
}

CHARACTER *CBoneManager::GetOwnCharacter(OBJECT *object, const std::wstring &name)
{
    const auto found = characters_.find(object);
    if (found == characters_.end())
        return nullptr;
    auto *character = found->second.character;
    return character->NamedBones.contains(name) ? character : nullptr;
}

int CBoneManager::GetBoneNumber(OBJECT *object, const std::wstring &name)
{
    const auto found = characters_.find(object);
    if (found == characters_.end())
        return -1;
    const auto &bones = found->second.character->NamedBones;
    const auto bone = bones.find(name);
    return bone == bones.end() ? -1 : bone->second;
}

bool CBoneManager::GetBonePosition(OBJECT *object, const std::wstring &name, vec3_t position)
{
    vec3_t relative{};
    return GetBonePosition(object, name, relative, position);
}

std::shared_ptr<const CharacterSocketBinding> CBoneManager::BindSocket(OBJECT *object,
                                                                       std::wstring_view name,
                                                                       int &bone)
{
    const auto found = characters_.find(object);
    if (found == characters_.end())
        return {};
    auto &admission = found->second;
    const auto &character = *admission.character;
    const auto socket = character.NamedBones.find(name);
    if (socket == character.NamedBones.end())
        return {};
    if (!admission.binding || admission.binding->source != character.SocketSource)
        admission.binding = std::make_shared<CharacterSocketBinding>(character.SocketSource);
    bone = socket->second;
    return admission.binding;
}

bool CBoneManager::GetBonePosition(OBJECT *object, const std::wstring &name, vec3_t relative,
                                   vec3_t position)
{
    const int bone = GetBoneNumber(object, name);
    if (bone < 0)
        return false;
    vec3_t local;
    VectorTransform(relative, object->BoneTransform[bone], local);
    VectorScale(local, object->Scale, local);
    VectorAdd(local, object->Position, position);
    return true;
}

bool CBoneManager::GetBonePosition(const OBJECT *object, int bone, vec3_t position) const
{
    const vec3_t relative{};
    return GetBonePosition(object, bone, relative, position);
}

bool CBoneManager::GetBonePosition(const OBJECT *object, int bone, const vec3_t relative,
                                   vec3_t position) const
{
    vec3_t local;
    VectorTransform(relative, object->BoneTransform[bone], local);
    VectorScale(local, object->Scale, local);
    VectorAdd(local, object->Position, position);
    return true;
}

#define RENDER_CLOTH

void SessionRenderUnit::RenderCloth(const CPhysicsCloth &cloth, const vec3_t *pvColor, int iLevel)
{
    const int m_iNumHor = cloth.m_iNumHor;
    const int m_iNumVer = cloth.m_iNumVer;
    const int m_iTexFront = cloth.m_iTexFront;
    const int m_iTexBack = cloth.m_iTexBack;
    const DWORD m_dwType = cloth.m_dwType;

    switch (PCT_MASK_DRAW & m_dwType)
    {
    case PCT_MASK_BLIT:
        DisableAlphaBlend();
        break;
    case PCT_MASK_ALPHA:
        EnableAlphaTest();
        break;
    case PCT_MASK_BLEND:
        EnableAlphaBlend();
        break;
    }

    if (pvColor)
    {
        glColor3fv(*pvColor);
    }
    else
    {
        glColor3f(1.f, 1.f, 1.f);
    }

    if (PCT_MASK_LIGHT & m_dwType)
    {
        float Lum = sinf(WorldTime * 0.001f) * 0.1f + 0.4f;
        float Lum2;
        vec3_t Light;
        int iOffset = 0;
        float fScale = 0.f;
        for (int i = 0; i < m_iNumHor; i++)
        {
            for (int j = 0; j < m_iNumVer; j++)
            {
                fScale = i * 0.2f + 0.4f;
                Lum2 = Lum * (m_iNumHor - i) / 5.f;

                Vector(Lum2, Lum2, Lum2, Light);
                vec3_t position;
                cloth.GetPosition(iOffset, &position);
                CreateSprite(BITMAP_LIGHT, position, fScale, Light, NULL);
                iOffset++;
            }
        }
        glColor3f(Lum, Lum, Lum);
        EnableAlphaBlend();
    }
#ifdef RENDER_CLOTH
    {
        RenderClothFace(cloth, TRUE, m_iTexFront);
        if ((PCT_MASK_DRAW & m_dwType) != PCT_MASK_BLEND || !(PCT_MASK_LIGHT & m_dwType))
        {
            RenderClothFace(cloth, FALSE, m_iTexBack);
        }
    }
#endif
}

void SessionRenderUnit::RenderClothFace(const CPhysicsCloth &cloth, BOOL bFront, int iTexture)
{
    const int m_iNumHor = cloth.m_iNumHor;
    const int m_iNumVer = cloth.m_iNumVer;
    BindTexture(iTexture); //BITMAP_ROBE

    glBegin(GL_QUADS);
    const auto renderVertex = [this, &cloth, m_iNumHor, m_iNumVer](int xVertex, int yVertex) {
        const int vertex = m_iNumHor * yVertex + xVertex;
        vec3_t position;
        cloth.GetPosition(vertex, &position);
        glTexCoord2f(static_cast<float>(xVertex) / static_cast<float>(m_iNumHor - 1),
                     std::min<float>(0.99f, static_cast<float>(yVertex) /
                                                static_cast<float>(m_iNumVer - 1)));
        glVertex3f(position[0], position[1], position[2]);
    };

    if (bFront)
    {
        for (int j = 0; j < m_iNumVer - 1; ++j)
        {
            for (int i = 0; i < m_iNumHor - 1; ++i)
            {
                renderVertex(i, j);
                renderVertex(i + 1, j);
                renderVertex(i + 1, j + 1);
                renderVertex(i, j + 1);
            }
        }
    }
    else
    {
        for (int j = 0; j < m_iNumVer - 1; ++j)
        {
            for (int i = 0; i < m_iNumHor - 1; ++i)
            {
                renderVertex(i, j);
                renderVertex(i, j + 1);
                renderVertex(i + 1, j + 1);
                renderVertex(i + 1, j);
            }
        }
    }

    glEnd();
}

void SessionRenderUnit::RenderPhysicsCloths()
{
    auto &cloths = sessionKeeper_.WorldUnit()->Physics().m_lstCloth;
    for (auto *node = cloths.FindHead(); node; node = cloths.GetNext(node))
        node->GetData()->Render(*this);
}

void SessionRenderUnit::InsertShadowVolume(CShadowVolume *psv)
{
    m_qSV.Insert(psv);
}

void SessionRenderUnit::RenderShadowVolumesAsFrame(void)
{
    glPolygonMode(GL_FRONT, GL_LINE);
    glDepthMask(true);
    DisableAlphaBlend();
    DisableTexture();
    vec3_t vLight = {0.4f, 0.f, 0.f};
    glColor3fv(vLight);

    while (m_qSV.GetCount() > 0)
    {
        CShadowVolume *psv = m_qSV.Remove();
        psv->RenderAsFrame();
        psv->Destroy();
        delete psv;
    }

    glPolygonMode(GL_FRONT, GL_FILL);
}

void SessionRenderUnit::ShadeWithShadowVolumes(void)
{
    DisableAlphaBlend();

    DisableDepthMask();
    glEnable(GL_STENCIL_TEST);

    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilFunc(GL_ALWAYS, 0xFFFFFFFF, 0xFFFFFFFF);

    while (m_qSV.GetCount() > 0)
    {
        CShadowVolume *psv = m_qSV.Remove();
        psv->Shade();
        psv->Destroy();
        delete psv;
    }

    glFrontFace(GL_CCW);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_STENCIL_TEST);
    EnableDepthMask();
}

void SessionRenderUnit::RenderShadowToScreen(void)
{
    DisableDepthTest();
    DisableDepthMask();
    glEnable(GL_STENCIL_TEST);

    glStencilFunc(GL_LEQUAL, 0x1, 0xFFFFFFFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

    glDepthFunc(GL_ALWAYS);

    EnableAlphaBlendMinus();
    DisableTexture();
    vec3_t vLight = {.7f, 0.7f, 0.5f};

    //RenderTerrainAlphaBitmap(BITMAP_HIDE, Hero->Object.Position[0],Hero->Object.Position[1],20.f,20.f,vLight,0.f,fAlpha);
    float p[4][2];
    const SessionDisplayRect renderRect = sessionKeeper_.Display()->LocalRect();
    auto Width = static_cast<float>(renderRect.width);
    auto Height = static_cast<float>(renderRect.height);
    p[0][0] = 0.f;
    p[0][1] = 0.f;
    p[1][0] = 0.f;
    p[1][1] = Height;
    p[2][0] = 0.f + Width;
    p[2][1] = Height;
    p[3][0] = 0.f + Width;
    p[3][1] = 0.f;
    //BeginBitmap();
    glBegin(GL_TRIANGLE_FAN);
    glColor3fv(vLight);
    for (int i = 0; i < 4; i++)
    {
        glVertex2f(p[i][0], p[i][1]);
    }
    glEnd();
    glDepthFunc(GL_LESS);
    glDisable(GL_STENCIL_TEST);
    EnableDepthMask();
}

CShadowVolume::CShadowVolume(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), Bitmaps(keeper.BitmapRegistry())
{
    Clear();
}

CShadowVolume::~CShadowVolume()
{
}

void CShadowVolume::Clear(void)
{
    m_nNumVertices = 0;
    m_pVertices = NULL;
    m_iNumEdge = 0;
    m_pEdges = NULL;
}

BOOL CShadowVolume::GetReadyToCreate(vec3_t ppVertexTransformed[MAX_MESH][MAX_VERTICES], BMD *b,
                                     OBJECT *o, bool SkipTga)
{
    if (o->Alpha < 0.01f)
    {
        return (FALSE);
    }
    short nHiddenMesh = o->HiddenMesh;
    short nBlendMesh = o->BlendMesh;
    if (nHiddenMesh == -2 || nBlendMesh == -2)
    {
        return (FALSE);
    }

    int iNumTriangles = 0;
    for (int i = 0; i < b->NumMeshs; ++i)
    {
        if (nHiddenMesh == i || nBlendMesh == i)
        {
            continue;
        }

        if (Bitmaps[b->IndexTexture[i]].Components == 4)
        {
            if (SkipTga)
                continue;
        }
        iNumTriangles += std::max<int>(0, b->Meshs[i].NumTriangles);
    }
    m_iNumEdge = 0;
    m_pEdges = new St_Edges[iNumTriangles * 3];

    for (int i = 0; i < b->NumMeshs; ++i)
    {
        if (nHiddenMesh == i || nBlendMesh == i)
        {
            continue;
        }

        bool Tga = false;
        if (Bitmaps[b->IndexTexture[i]].Components == 4)
        {
            Tga = true;
            if (SkipTga)
                continue;
        }
        DeterminateSilhouette(i, ppVertexTransformed, b->Meshs[i].NumTriangles,
                              b->Meshs[i].Triangles, Tga);
    }

    return (TRUE);
}

void CShadowVolume::Create(vec3_t ppVertexTransformed[MAX_MESH][MAX_VERTICES], BMD *b, OBJECT *o,
                           bool SkipTga)
{
    m_vLight[0] = -1.f;
    m_vLight[1] = 0.03f;
    m_vLight[2] = -1.f;
    VectorNormalize(m_vLight);

    if (!GetReadyToCreate(ppVertexTransformed, b, o, SkipTga))
    {
        return;
    }

    GenerateSidePolygon(ppVertexTransformed);
    delete[] m_pEdges;
}

void CShadowVolume::Destroy()
{
    delete[] m_pVertices;
}

void CShadowVolume::AddEdge(short nV1, short nV2, short nMesh)
{
    for (int i = 0; i < m_iNumEdge; ++i)
    {
        if (m_pEdges[i].m_nMesh == nMesh)
        {
            if (m_pEdges[i].m_nVertexIndex[0] == nV2 && m_pEdges[i].m_nVertexIndex[1] == nV1)
            {
                if (m_iNumEdge > 1)
                {
                    m_pEdges[i].m_nVertexIndex[0] = m_pEdges[m_iNumEdge - 1].m_nVertexIndex[0];
                    m_pEdges[i].m_nVertexIndex[1] = m_pEdges[m_iNumEdge - 1].m_nVertexIndex[1];
                    m_pEdges[i].m_nMesh = m_pEdges[m_iNumEdge - 1].m_nMesh;
                    m_iNumEdge--;
                }
                return;
            }
        }
    }

    m_pEdges[m_iNumEdge].m_nVertexIndex[0] = nV1;
    m_pEdges[m_iNumEdge].m_nVertexIndex[1] = nV2;
    m_pEdges[m_iNumEdge].m_nMesh = nMesh;
    m_iNumEdge++;
}

void CShadowVolume::AddEdgeFast(short nV1, short nV2, short nMesh, int iTriangle, int Edge,
                                const Triangle_t *pTriangles)
{
    const Triangle_t *pTriangle = &pTriangles[iTriangle];
    short EdgeTriangleIndex = pTriangle->EdgeTriangleIndex[Edge];
    if (EdgeTriangleIndex == -1 || facing_[EdgeTriangleIndex] == 0)
    {
        m_pEdges[m_iNumEdge].m_nVertexIndex[0] = nV1;
        m_pEdges[m_iNumEdge].m_nVertexIndex[1] = nV2;
        m_pEdges[m_iNumEdge].m_nMesh = nMesh;
        m_pEdges[m_iNumEdge].m_nNormalIndex[0] = pTriangle->NormalIndex[Edge];
        m_pEdges[m_iNumEdge].m_nNormalIndex[1] = pTriangle->NormalIndex[(Edge + 1) % 3];
        m_iNumEdge++;
    }
}

void CShadowVolume::DeterminateSilhouette(short nMesh,
                                          vec3_t ppVertexTransformed[MAX_MESH][MAX_VERTICES],
                                          short nNumTriangles, const Triangle_t *pTriangles,
                                          bool Tga)
{
    if (facing_.size() < static_cast<std::size_t>(nNumTriangles))
        facing_.resize(nNumTriangles);
    for (int iTriangle = 0; iTriangle < nNumTriangles; ++iTriangle)
    {
        const Triangle_t *pTriangle = &pTriangles[iTriangle];
        const short *pnVertexIndex = pTriangle->VertexIndex;

        vec3_t *pVertex[3];
        pVertex[0] = &ppVertexTransformed[nMesh][pnVertexIndex[0]];
        pVertex[1] = &ppVertexTransformed[nMesh][pnVertexIndex[1]];
        pVertex[2] = &ppVertexTransformed[nMesh][pnVertexIndex[2]];
        vec3_t Normal;
        FaceNormalize(*pVertex[0], *pVertex[1], *pVertex[2], Normal);
        facing_[iTriangle] = DotProduct(Normal, m_vLight) <= 0.f;
    }

    for (int iTriangle = 0; iTriangle < nNumTriangles; ++iTriangle)
    {
        const Triangle_t *pTriangle = &pTriangles[iTriangle];

        if (facing_[iTriangle])
        {
            const short *pnVertexIndex = pTriangle->VertexIndex;
            //AddEdge( pnVertexIndex[0], pnVertexIndex[1], nMesh);
            //AddEdge( pnVertexIndex[1], pnVertexIndex[2], nMesh);
            //AddEdge( pnVertexIndex[2], pnVertexIndex[0], nMesh);
            AddEdgeFast(pnVertexIndex[0], pnVertexIndex[1], nMesh, iTriangle, 0, pTriangles);
            AddEdgeFast(pnVertexIndex[1], pnVertexIndex[2], nMesh, iTriangle, 1, pTriangles);
            AddEdgeFast(pnVertexIndex[2], pnVertexIndex[0], nMesh, iTriangle, 2, pTriangles);
        }
    }
}

#define GROUND_HEIGHT 22.5f

void CShadowVolume::GenerateSidePolygon(vec3_t ppVertexTransformed[MAX_MESH][MAX_VERTICES])
{
    m_nNumVertices = 0;
    m_pVertices = new vec3_t[m_iNumEdge * 6];

    vec3_t Vertex[4];
    for (int i = 0; i < m_iNumEdge; ++i)
    {
        VectorCopy(ppVertexTransformed[m_pEdges[i].m_nMesh][m_pEdges[i].m_nVertexIndex[0]],
                   Vertex[0]);
        VectorCopy(ppVertexTransformed[m_pEdges[i].m_nMesh][m_pEdges[i].m_nVertexIndex[1]],
                   Vertex[1]);
        //float fLength = ( std::max<float>( GROUND_HEIGHT, max( Vertex[0][2], Vertex[1][2])) / -m_vLight[2]);
        float fLength = (std::max<float>(GROUND_HEIGHT, Vertex[0][2]) / -m_vLight[2]);
        VectorMA(Vertex[0], fLength, m_vLight, Vertex[2]);
        fLength = (std::max<float>(GROUND_HEIGHT, Vertex[1][2]) / -m_vLight[2]);
        VectorMA(Vertex[1], fLength, m_vLight, Vertex[3]);

        VectorCopy(Vertex[0], m_pVertices[m_nNumVertices]);
        m_nNumVertices++;
        VectorCopy(Vertex[2], m_pVertices[m_nNumVertices]);
        m_nNumVertices++;
        VectorCopy(Vertex[1], m_pVertices[m_nNumVertices]);
        m_nNumVertices++;
        VectorCopy(Vertex[1], m_pVertices[m_nNumVertices]);
        m_nNumVertices++;
        VectorCopy(Vertex[2], m_pVertices[m_nNumVertices]);
        m_nNumVertices++;
        VectorCopy(Vertex[3], m_pVertices[m_nNumVertices]);
        m_nNumVertices++;
    }
}

void CShadowVolume::RenderAsFrame(void)
{
    RenderShadowVolume();
}

void CShadowVolume::RenderShadowVolume(void)
{
    glBegin(GL_TRIANGLES);

    for (int i = 0; i < m_nNumVertices; ++i)
    {
        glVertex3fv(m_pVertices[i]);
    }

    glEnd();
}

void CShadowVolume::Shade(void)
{
    glFrontFace(GL_CCW);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    RenderShadowVolume();

    glFrontFace(GL_CW);
    glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
    RenderShadowVolume();
}

namespace ModelGeometryDetail
{
template <typename Value>
std::size_t SharedVectorBytes(const std::shared_ptr<const std::vector<Value>> &values) noexcept
{
    return values == nullptr ? 0 : sizeof(*values) + values->capacity() * sizeof(Value);
}

template <typename Value> constexpr std::size_t PositiveCount(const Value value) noexcept
{
    return value > 0 ? static_cast<std::size_t>(value) : 0;
}

std::size_t BmdSharedAssetBytes(const BmdSharedAsset &asset) noexcept
{
    const int meshCount = (std::max)(0, static_cast<int>(asset.meshCount));
    const int boneCount = (std::max)(0, static_cast<int>(asset.boneCount));
    const int actionCount = (std::max)(0, static_cast<int>(asset.actionCount));
    std::size_t bytes =
        sizeof(asset) +
        static_cast<std::size_t>((std::max)(1, meshCount)) * (sizeof(Mesh_t) + sizeof(Texture_t)) +
        static_cast<std::size_t>((std::max)(1, boneCount)) * sizeof(Bone_t) +
        static_cast<std::size_t>((std::max)(1, actionCount)) * sizeof(Action_t) +
        SharedVectorBytes(asset.vertices) + SharedVectorBytes(asset.rigidVertices) +
        SharedVectorBytes(asset.indices) +
        asset.invariantLocalPose.capacity() * sizeof(RenderTapeBoneMatrix);

    for (int meshIndex = 0; meshIndex < meshCount; ++meshIndex)
    {
        const Mesh_t &mesh = asset.meshes[meshIndex];
        bytes += PositiveCount(mesh.NumVertices) * sizeof(Vertex_t) +
                 PositiveCount(mesh.NumNormals) * sizeof(Normal_t) +
                 PositiveCount(mesh.NumTexCoords) * sizeof(TexCoord_t) +
                 PositiveCount(mesh.NumVertexColors) * sizeof(VertexColor_t) +
                 PositiveCount(mesh.NumTriangles) * sizeof(Triangle_t) +
                 (mesh.Commands == nullptr ? 0 : PositiveCount(mesh.NumCommandBytes)) +
                 (mesh.m_csTScript == nullptr ? 0 : sizeof(TextureScript)) +
                 SharedVectorBytes(mesh.RenderTapeVertexSources) +
                 SharedVectorBytes(mesh.RenderTapeIndices);
    }
    for (int action = 0; action < actionCount; ++action)
    {
        const Action_t &descriptor = asset.actions[action];
        if (descriptor.Positions != nullptr)
        {
            bytes += PositiveCount(descriptor.NumAnimationKeys) * sizeof(vec3_t);
        }
    }
    for (int bone = 0; bone < boneCount; ++bone)
    {
        const Bone_t &descriptor = asset.bones[bone];
        if (descriptor.Dummy || descriptor.BoneMatrixes == nullptr)
            continue;
        bytes += static_cast<std::size_t>(actionCount) * sizeof(BoneMatrix_t);
        for (int action = 0; action < actionCount; ++action)
        {
            const std::size_t keyCount = PositiveCount(asset.actions[action].NumAnimationKeys);
            const BoneMatrix_t &matrix = descriptor.BoneMatrixes[action];
            if (matrix.Position != nullptr)
                bytes += keyCount * sizeof(vec3_t);
            if (matrix.Rotation != nullptr)
                bytes += keyCount * sizeof(vec3_t);
            if (matrix.Quaternion != nullptr)
                bytes += keyCount * sizeof(vec4_t);
        }
    }
    return bytes;
}

bool UsesRenderMeshVertexColors(bool enableLight, int finalRenderFlags) noexcept
{
    return (enableLight && finalRenderFlags == RENDER_TEXTURE) ||
           finalRenderFlags == RENDER_CHROME || finalRenderFlags == RENDER_CHROME4 ||
           finalRenderFlags == RENDER_OIL;
}

bool BuildRenderTapeTopology(Mesh_t &mesh, int boneCount, bool decoded) noexcept
{
    if (mesh.RenderTapeVertexSources != nullptr)
    {
        return true;
    }

    try
    {
        const std::size_t indexCount = static_cast<std::size_t>(mesh.NumTriangles) * 3U;
        std::vector<BmdRenderTapeVertexSource> sources;
        std::vector<std::uint32_t> indices;
        std::unordered_map<std::uint64_t, std::uint32_t> sourceIndices;
        sources.reserve(indexCount);
        indices.reserve(indexCount);
        sourceIndices.reserve(indexCount);

        for (int triangleIndex = 0; triangleIndex < mesh.NumTriangles; ++triangleIndex)
        {
            const Triangle_t &triangle = mesh.Triangles[triangleIndex];
            if (!decoded && triangle.Polygon != 3)
            {
                return false;
            }
            for (int corner = 0; corner < 3; ++corner)
            {
                const short vertexIndex = triangle.VertexIndex[corner];
                const short normalIndex = triangle.NormalIndex[corner];
                const short texCoordIndex = triangle.TexCoordIndex[corner];
                if (!decoded &&
                    (vertexIndex < 0 || vertexIndex >= mesh.NumVertices || normalIndex < 0 ||
                     normalIndex >= mesh.NumNormals || texCoordIndex < 0 ||
                     texCoordIndex >= mesh.NumTexCoords || mesh.Vertices[vertexIndex].Node < 0 ||
                     mesh.Vertices[vertexIndex].Node >= boneCount ||
                     mesh.Normals[normalIndex].Node < 0 ||
                     mesh.Normals[normalIndex].Node >= boneCount))
                {
                    return false;
                }

                const std::uint64_t key =
                    static_cast<std::uint16_t>(vertexIndex) |
                    (static_cast<std::uint64_t>(static_cast<std::uint16_t>(normalIndex)) << 16U) |
                    (static_cast<std::uint64_t>(static_cast<std::uint16_t>(texCoordIndex)) << 32U);
                const auto [found, inserted] =
                    sourceIndices.emplace(key, static_cast<std::uint32_t>(sources.size()));
                if (inserted)
                {
                    sources.push_back({vertexIndex, normalIndex, texCoordIndex});
                }
                indices.push_back(found->second);
            }
        }

        mesh.RenderTapeVertexSources =
            std::make_shared<const std::vector<BmdRenderTapeVertexSource>>(std::move(sources));
        mesh.RenderTapeIndices =
            std::make_shared<const std::vector<std::uint32_t>>(std::move(indices));
        return true;
    }
    catch (...)
    {
        return false;
    }
}
} // namespace ModelGeometryDetail

BMD::BMD(SessionKeeper &sessionKeeper) noexcept
    : SessionLegacyCalls(sessionKeeper), NumBones(0), NumMeshs(0), NumActions(0), Meshs(nullptr),
      Bones(nullptr), Actions(nullptr), Textures(nullptr), IndexTexture(nullptr),
      LightEnable(false), ContrastEnable(false), HideSkin(false), m_iBMDSeqID(0), bLightMap(false),
      bOffLight(false), iBillType(-1), m_bCompletedAlloc(false),
      gMapManager(sessionKeeper.MapManagerObject()),
      FPS_ANIMATION_FACTOR(sessionKeeper.FrameAnimationFactor()),
      WorldTime(sessionKeeper.FrameWorldTime())
{
}

std::uint64_t BMD::PoseAssetIdentity() const noexcept
{
    return sharedAsset_ ? sharedAsset_->poseIdentity : 0;
}

void BMD::InvalidateCharacterPoses() const noexcept
{
    // Resource binding is an owner preparation step. Temporary decoded BMDs
    // are not installed models and must not invalidate live characters.
    if (sessionKeeper_.ModelPoolObject().Find(m_iBMDSeqID) != this)
        return;
    for (auto *character : sessionKeeper_.CharactersClientStorage().Pointers())
        if (character && character->Object.Type == m_iBMDSeqID)
            character->Object.EnableBoneMatrix = false;
    for (auto &block : sessionKeeper_.ObjectBlocks())
        for (auto *object = block.Head; object; object = object->Next)
            if (object->Type == m_iBMDSeqID)
            {
                object->RigidPoseDirty = true;
                block.DrawGroupsDirty = true;
            }
}

bool BMD::BuildRenderTapeGeometry(bool decoded) noexcept
{
    renderTapeGeometry_ = {};
    renderTapePaletteReady_ = false;
    if (NumBones <= 0 || NumMeshs <= 0 || Meshs == nullptr)
    {
        return false;
    }

    try
    {
        std::size_t vertexCount = 0;
        std::size_t indexCount = 0;
        for (int meshIndex = 0; meshIndex < NumMeshs; ++meshIndex)
        {
            Mesh_t &mesh = Meshs[meshIndex];
            if (!ModelGeometryDetail::BuildRenderTapeTopology(mesh, NumBones, decoded))
            {
                return false;
            }
            vertexCount += mesh.RenderTapeVertexSources->size();
            indexCount += mesh.RenderTapeIndices->size();
            if (vertexCount > (std::numeric_limits<std::uint32_t>::max)() ||
                indexCount > (std::numeric_limits<std::uint32_t>::max)())
            {
                return false;
            }
        }

        auto vertices = std::make_shared<std::vector<RenderTapeVertex>>();
        auto indices = std::make_shared<std::vector<std::uint32_t>>();
        vertices->reserve(vertexCount);
        indices->reserve(indexCount);
        for (int meshIndex = 0; meshIndex < NumMeshs; ++meshIndex)
        {
            Mesh_t &mesh = Meshs[meshIndex];
            mesh.RenderTapeVertexOffset = static_cast<std::uint32_t>(vertices->size());
            mesh.RenderTapeIndexOffset = static_cast<std::uint32_t>(indices->size());
            for (const BmdRenderTapeVertexSource &source : *mesh.RenderTapeVertexSources)
            {
                const Vertex_t &position = mesh.Vertices[source.vertexIndex];
                const Normal_t &normal = mesh.Normals[source.normalIndex];
                const TexCoord_t &uv = mesh.TexCoords[source.texCoordIndex];
                vertices->push_back(RenderTapeVertex{
                    {position.Position[0], position.Position[1], position.Position[2], 1.0F},
                    {uv.TexCoordU, uv.TexCoordV},
                    {1.0F, 1.0F, 1.0F, 1.0F},
                    {normal.Normal[0], normal.Normal[1], normal.Normal[2]},
                    {static_cast<std::uint32_t>(position.Node),
                     static_cast<std::uint32_t>(normal.Node),
                     static_cast<std::uint32_t>(source.vertexIndex), 0}});
            }
            indices->insert(indices->end(), mesh.RenderTapeIndices->begin(),
                            mesh.RenderTapeIndices->end());
        }
        if (vertices->empty() || indices->empty())
        {
            return false;
        }
        SessionRenderUnit *const renderer = sessionKeeper_.Renderer();
        renderTapeGeometryRevision_ =
            renderer == nullptr ? 0 : renderer->AllocateGeometryRevision();
        renderTapeGeometry_ = {{}, std::move(vertices), std::move(indices)};
        return true;
    }
    catch (...)
    {
        renderTapeGeometry_ = {};
        return false;
    }
}

std::size_t BMD::RenderTapeStorageBytes(SharedAllocationCounter &allocations) const noexcept
{
    std::size_t bytes = allocations.CountVector(renderTapeGeometry_.vertices) +
                        allocations.CountVector(renderTapeGeometry_.indices) +
                        allocations.CountVector(renderTapeRigidGeometry_.vertices);
    for (int mesh = 0; mesh < NumMeshs && Meshs != nullptr; ++mesh)
    {
        bytes += allocations.CountVector(Meshs[mesh].RenderTapeVertexSources);
        bytes += allocations.CountVector(Meshs[mesh].RenderTapeIndices);
    }
    return bytes;
}

bool BMD::PrepareRenderTapeGeometry(bool rigid) noexcept
{
    auto &geometry = rigid ? renderTapeRigidGeometry_ : renderTapeGeometry_;
    auto &revision = rigid ? renderTapeRigidGeometryRevision_ : renderTapeGeometryRevision_;
    if (geometry.vertices == nullptr || geometry.indices == nullptr)
    {
        return false;
    }
    if (revision == 0)
    {
        SessionRenderUnit *const renderer = sessionKeeper_.Renderer();
        if (renderer == nullptr)
        {
            return false;
        }
        revision = renderer->AllocateGeometryRevision();
    }
    geometry.asset = {sessionKeeper_.Id().RawValue(), LegacyRender().Generation().RawValue(),
                      revision};
    return IsValid(geometry);
}

bool BMD::PrepareRenderTapePalette(const float (*boneMatrix)[3][4], bool stablePose) noexcept
{
    renderTapePaletteReady_ = false;
    if (boneMatrix == nullptr || NumBones <= 0 || !PrepareRenderTapeGeometry())
    {
        return false;
    }
    static_assert(sizeof(RenderTapeBoneMatrix) == sizeof(float[3][4]));
    const auto *matrices = reinterpret_cast<const RenderTapeBoneMatrix *>(boneMatrix);
    const std::optional<std::uint32_t> offset = LegacyRender().AppendBoneMatrices(
        {matrices, static_cast<std::size_t>(NumBones)}, stablePose);
    if (!offset.has_value())
    {
        return false;
    }
    renderTapeTransform_.paletteOffset = *offset;
    renderTapeTransform_.paletteCount = static_cast<std::uint32_t>(NumBones);
    renderTapeTransform_.enabled = true;
    renderTapePaletteReady_ = true;
    return true;
}

RenderBmdUvMode BMD::SelectRenderTapeUvMode(int renderFlags) const noexcept
{
    if ((renderFlags & RENDER_CHROME2) == RENDER_CHROME2)
        return RenderBmdUvMode::Chrome2;
    if ((renderFlags & RENDER_CHROME3) == RENDER_CHROME3)
        return RenderBmdUvMode::Chrome3;
    if ((renderFlags & RENDER_CHROME4) == RENDER_CHROME4)
        return RenderBmdUvMode::Chrome4;
    if ((renderFlags & RENDER_CHROME5) == RENDER_CHROME5)
        return RenderBmdUvMode::Chrome5;
    if ((renderFlags & RENDER_CHROME6) == RENDER_CHROME6)
        return RenderBmdUvMode::Chrome6;
    if ((renderFlags & RENDER_CHROME7) == RENDER_CHROME7)
        return RenderBmdUvMode::Chrome7;
    if ((renderFlags & RENDER_OIL) == RENDER_OIL)
        return RenderBmdUvMode::Oil;
    if ((renderFlags & RENDER_CHROME) == RENDER_CHROME)
        return RenderBmdUvMode::Chrome;
    if ((renderFlags & RENDER_METAL) == RENDER_METAL)
        return RenderBmdUvMode::Metal;
    return RenderBmdUvMode::Mesh;
}

namespace ModelGeometryDetail
{
void TransformInvariantPose(const BmdSharedAsset &asset, const float root[3][4],
                            float (*output)[3][4])
{
    const auto *local = reinterpret_cast<const float(*)[3][4]>(asset.invariantLocalPose.data());
    for (int index = 0; index < asset.boneCount; ++index)
    {
        const auto &bone = asset.bones[index];
        if (bone.Dummy)
        {
            std::memset(output[index], 0, sizeof(output[index]));
            continue;
        }
        R_ConcatTransforms(bone.Parent == -1 ? root : output[bone.Parent], local[index],
                           output[index]);
    }
}
} // namespace ModelGeometryDetail

void BMD::AnimationAtFrame(float (*BoneMatrix)[3][4], float AnimationFrame, float PriorFrame,
                           unsigned short PriorAction, const vec3_t Angle, const vec3_t HeadAngle,
                           bool Parent, bool Translate, const float (*ExtParentMatrix)[4],
                           short CurrentActionArg)
{
    AnimationImpl(BoneMatrix, AnimationFrame, PriorFrame, PriorAction, Angle, HeadAngle, Parent,
                  Translate, ExtParentMatrix, CurrentActionArg, true);
}

void BMD::Animation(float (*BoneMatrix)[3][4], float AnimationFrame, float PriorFrame,
                    unsigned short PriorAction, const vec3_t Angle, const vec3_t HeadAngle,
                    bool Parent, bool Translate, const float (*ExtParentMatrix)[4],
                    short CurrentActionArg)
{
    AnimationImpl(BoneMatrix, AnimationFrame, PriorFrame, PriorAction, Angle, HeadAngle, Parent,
                  Translate, ExtParentMatrix, CurrentActionArg, false);
}

void BMD::AnimationImpl(float (*BoneMatrix)[3][4], float AnimationFrame, float PriorFrame,
                        unsigned short PriorAction, const vec3_t Angle, const vec3_t HeadAngle,
                        bool Parent, bool Translate, const float (*ExtParentMatrix)[4],
                        short CurrentActionArg, bool adjacentKeys)
{
    if (NumActions <= 0)
        return;
    ++animationEvaluations_;

    unsigned short currentAction =
        (CurrentActionArg < 0) ? CurrentAction : (unsigned short)CurrentActionArg;
    if (PriorAction >= NumActions)
        PriorAction = 0;
    if (currentAction >= NumActions)
        currentAction = 0;

    float currentAnimation = AnimationFrame;
    if (adjacentKeys && PriorAction == currentAction)
    {
        const auto &action = Actions[currentAction];
        const int cycle = (std::max)(1, action.NumAnimationKeys - (action.LockPositions ? 1 : 0));
        if (action.Loop)
            currentAnimation =
                std::clamp(currentAnimation, 0.f, (std::max)(0.f, action.NumAnimationKeys - 0.01f));
        else
        {
            currentAnimation = std::fmod(currentAnimation, static_cast<float>(cycle));
            if (currentAnimation < 0.f)
                currentAnimation += cycle;
        }
        const int key = static_cast<int>(currentAnimation);
        PriorFrame = static_cast<float>(key > 0 ? key - 1 : (action.Loop ? 0 : cycle - 1));
    }
    else if (adjacentKeys)
        currentAnimation = (std::max)(0.f, currentAnimation);
    int currentAnimationFrame = static_cast<int>(currentAnimation);
    float s1 = (currentAnimation - currentAnimationFrame);
    float s2 = 1.f - s1;
    auto PriorAnimationFrame = (int)PriorFrame;
    if (NumActions > 0)
    {
        if (PriorAnimationFrame < 0)
            PriorAnimationFrame = 0;
        if (currentAnimationFrame < 0)
            currentAnimationFrame = 0;
        if (PriorAnimationFrame >= Actions[PriorAction].NumAnimationKeys)
            PriorAnimationFrame = 0;
        if (currentAnimationFrame >= Actions[currentAction].NumAnimationKeys)
            currentAnimationFrame = 0;
    }

    // Pre-calculate localParentMatrix ONCE outside bone loop for thread-safe root transforms
    float localParentMatrix[3][4];
    if (!Parent)
    {
        AngleMatrix(Angle, localParentMatrix);
        if (Translate)
        {
            for (auto &y : localParentMatrix)
            {
                for (int x = 0; x < 3; ++x)
                {
                    y[x] *= BodyScale;
                }
            }

            localParentMatrix[0][3] = BodyOrigin[0];
            localParentMatrix[1][3] = BodyOrigin[1];
            localParentMatrix[2][3] = BodyOrigin[2];
        }
        for (int r = 0; r < 3; ++r)
        {
            for (int c = 0; c < 4; ++c)
            {
                parentMatrix_[r][c] = localParentMatrix[r][c];
            }
        }
    }

    if (sharedAsset_ && !sharedAsset_->invariantLocalPose.empty() && BoneHead < 0 &&
        BodyHeight == 0.f)
    {
        const float(*root)[4] =
            Parent ? (ExtParentMatrix ? ExtParentMatrix : parentMatrix_) : localParentMatrix;
        ModelGeometryDetail::TransformInvariantPose(*sharedAsset_, root, BoneMatrix);
        return;
    }

    vec4_t boneQuaternion[MAX_BONES] = {};
    // Pre-calculate Head bone quaternions ONCE outside loop
    vec4_t headQ1, headQ2;
    const bool hasHeadBone = (BoneHead >= 0 && BoneHead < NumBones && !Bones[BoneHead].Dummy);
    if (hasHeadBone)
    {
        const Bone_t *hb = &Bones[BoneHead];
        const BoneMatrix_t *hbm1 = &hb->BoneMatrixes[PriorAction];
        const BoneMatrix_t *hbm2 = &hb->BoneMatrixes[currentAction];

        vec3_t Angle1, Angle2;
        VectorCopy(hbm1->Rotation[PriorAnimationFrame], Angle1);
        VectorCopy(hbm2->Rotation[currentAnimationFrame], Angle2);

        constexpr float radFactor = 1.0f / (180.f / Q_PI);
        const float HeadAngleX = HeadAngle[0] * radFactor;
        const float HeadAngleY = HeadAngle[1] * radFactor;

        Angle1[0] -= HeadAngleX;
        Angle2[0] -= HeadAngleX;
        Angle1[2] -= HeadAngleY;
        Angle2[2] -= HeadAngleY;

        AngleQuaternion(Angle1, headQ1);
        AngleQuaternion(Angle2, headQ2);
    }

    const bool bLockPositions =
        (Actions[PriorAction].LockPositions || Actions[currentAction].LockPositions);

    // bones loop
    for (int i = 0; i < NumBones; i++)
    {
        const Bone_t *b = &Bones[i];
        if (b->Dummy)
        {
            std::memset(BoneMatrix[i], 0, sizeof(BoneMatrix[i]));
            continue;
        }
        const BoneMatrix_t *bm1 = &b->BoneMatrixes[PriorAction];
        const BoneMatrix_t *bm2 = &b->BoneMatrixes[currentAction];

        const float *q1;
        const float *q2;

        if (i == BoneHead)
        {
            q1 = headQ1;
            q2 = headQ2;
        }
        else
        {
            q1 = bm1->Quaternion[PriorAnimationFrame];
            q2 = bm2->Quaternion[currentAnimationFrame];
        }

        if (!QuaternionCompare(q1, q2))
        {
            QuaternionNLERP(q1, q2, s1, boneQuaternion[i]);
        }
        else
        {
            QuaternionCopy(q1, boneQuaternion[i]);
        }

        float Matrix[3][4];
        QuaternionMatrix(boneQuaternion[i], Matrix);
        const float *Position1 = bm1->Position[PriorAnimationFrame];
        const float *Position2 = bm2->Position[currentAnimationFrame];

        if (i == 0 && bLockPositions)
        {
            Matrix[0][3] = bm2->Position[0][0];
            Matrix[1][3] = bm2->Position[0][1];
            Matrix[2][3] = Position1[2] * s2 + Position2[2] * s1 + BodyHeight;
        }
        else
        {
            Matrix[0][3] = Position1[0] * s2 + Position2[0] * s1;
            Matrix[1][3] = Position1[1] * s2 + Position2[1] * s1;
            Matrix[2][3] = Position1[2] * s2 + Position2[2] * s1;
        }

        if (b->Parent == -1)
        {
            if (Parent && ExtParentMatrix)
            {
                R_ConcatTransforms(ExtParentMatrix, Matrix, BoneMatrix[i]);
            }
            else if (!Parent)
            {
                R_ConcatTransforms(localParentMatrix, Matrix, BoneMatrix[i]);
            }
            else
            {
                R_ConcatTransforms(parentMatrix_, Matrix, BoneMatrix[i]);
            }
        }
        else
        {
            R_ConcatTransforms(BoneMatrix[b->Parent], Matrix, BoneMatrix[i]);
        }
    }
}

void BMD::PrepareTransformLighting(vec3_t LightPosition)
{
    if (LightEnable)
    {
        // Ground shadows project toward -X; keep the light in world space.
        if (HighLight)
        {
            Vector(1.3f, 0.f, 2.f, LightPosition);
        }
        else if (gMapManager.InBattleCastle())
        {
            Vector(0.5f, -1.f, 1.f, LightPosition);
        }
        else
        {
            Vector(0.f, -1.5f, 0.f, LightPosition);
        }
    }
}

void BMD::Transform(const float (*BoneMatrix)[3][4], const vec3_t BoundingBoxMin,
                    const vec3_t BoundingBoxMax, OBB_t *OBB, bool Translate, float _Scale,
                    bool stablePose, const RigidObjectPose *rigidPose)
{
    vec3_t LightPosition{};
    PrepareTransformLighting(LightPosition);
    cpuBoneMatrix_ = BoneMatrix;
    std::copy_n(LightPosition, 3, cpuLightPosition_.begin());
    std::copy_n(BodyOrigin, 3, cpuBodyOrigin_.begin());
    cpuPositionScale_ = _Scale;
    cpuBoneScale_ = BoneScale;
    cpuBodyScale_ = BodyScale;
    cpuTranslate_ = Translate;
    cpuLightEnabled_ = LightEnable;
    cpuTransformsReady_ = false;

    renderTapeTransform_ = {};
    const bool gpuTransformReady = rigidPose ? PrepareRenderTapeGeometry(true)
                                             : PrepareRenderTapePalette(BoneMatrix, stablePose);
    if (rigidPose)
    {
        renderTapePaletteReady_ = gpuTransformReady;
        renderTapeTransform_.enabled = gpuTransformReady;
        renderTapeTransform_.rigid = true;
        renderTapeTransform_.rigidTransform = rigidPose->transform;
        std::memcpy(parentMatrix_, &rigidPose->transform, sizeof(parentMatrix_));
    }
    if (gpuTransformReady)
    {
        renderTapeTransform_.translate = Translate;
        renderTapeTransform_.scaledBone = BoneScale != 1.0F;
        renderTapeTransform_.positionScale = _Scale == 0.0F ? 1.0F : _Scale;
        renderTapeTransform_.boneScale = BoneScale;
        renderTapeTransform_.bodyScale = BodyScale;
        std::copy_n(BodyOrigin, 3, renderTapeTransform_.bodyOrigin.begin());
        std::copy_n(LightPosition, 3, renderTapeTransform_.lightPosition.begin());
    }

    vec3_t dynamicMinimum;
    vec3_t dynamicMaximum;
    if (EditFlag == EDIT_OBJECT)
    {
        Vector(999999.f, 999999.f, 999999.f, dynamicMinimum);
        Vector(-999999.f, -999999.f, -999999.f, dynamicMaximum);
        TransformCpuMeshes(dynamicMinimum, dynamicMaximum);
    }
    else if (!gpuTransformReady)
    {
        TransformCpuMeshes(nullptr, nullptr);
    }

    const float *const minimum = EditFlag == EDIT_OBJECT ? dynamicMinimum : BoundingBoxMin;
    const float *const maximum = EditFlag == EDIT_OBJECT ? dynamicMaximum : BoundingBoxMax;
    VectorCopy(minimum, OBB->StartPos);
    OBB->XAxis[0] = maximum[0] - minimum[0];
    OBB->YAxis[1] = maximum[1] - minimum[1];
    OBB->ZAxis[2] = maximum[2] - minimum[2];
    fTransformedSize = (std::max)({OBB->XAxis[0], OBB->YAxis[1], OBB->ZAxis[2]});
    VectorAdd(OBB->StartPos, BodyOrigin, OBB->StartPos);
    OBB->XAxis[1] = 0.f;
    OBB->XAxis[2] = 0.f;
    OBB->YAxis[0] = 0.f;
    OBB->YAxis[2] = 0.f;
    OBB->ZAxis[0] = 0.f;
    OBB->ZAxis[1] = 0.f;
}

void BMD::TransformCpuMeshes(float *const boundingMin, float *const boundingMax) noexcept
{
    if (cpuTransformsReady_ || cpuBoneMatrix_ == nullptr)
        return;

    const bool measureBounds = boundingMin != nullptr;
    for (int i = 0; i < NumMeshs; i++)
    {
        Mesh_t *m = &Meshs[i];
        for (int j = 0; j < m->NumVertices; j++)
        {
            Vertex_t *v = &m->Vertices[j];
            float *vp = VertexTransform[i][j];

            if (cpuBoneScale_ == 1.f)
            {
                if (cpuPositionScale_ != 0.0F)
                {
                    vec3_t Position;
                    VectorCopy(v->Position, Position);
                    VectorScale(Position, cpuPositionScale_, Position);
                    VectorTransform(Position, cpuBoneMatrix_[v->Node], vp);
                }
                else
                    VectorTransform(v->Position, cpuBoneMatrix_[v->Node], vp);
                if (cpuTranslate_)
                    VectorScale(vp, cpuBodyScale_, vp);
            }
            else
            {
                VectorRotate(v->Position, cpuBoneMatrix_[v->Node], vp);
                vp[0] = vp[0] * cpuBoneScale_ + cpuBoneMatrix_[v->Node][0][3];
                vp[1] = vp[1] * cpuBoneScale_ + cpuBoneMatrix_[v->Node][1][3];
                vp[2] = vp[2] * cpuBoneScale_ + cpuBoneMatrix_[v->Node][2][3];
                if (cpuTranslate_)
                    VectorScale(vp, cpuBodyScale_, vp);
            }
            if (measureBounds)
            {
                for (int k = 0; k < 3; k++)
                {
                    if (vp[k] < boundingMin[k])
                        boundingMin[k] = vp[k];
                    if (vp[k] > boundingMax[k])
                        boundingMax[k] = vp[k];
                }
            }
            if (cpuTranslate_)
                VectorAdd(vp, cpuBodyOrigin_.data(), vp);
        }

        for (int j = 0; j < m->NumNormals; j++)
        {
            Normal_t *sn = &m->Normals[j];
            float *tn = NormalTransform[i][j];
            VectorRotate(sn->Normal, cpuBoneMatrix_[sn->Node], tn);
            if (cpuLightEnabled_)
            {
                float Luminosity = DotProduct(tn, cpuLightPosition_.data()) * 0.8f + 0.4f;

                if (Luminosity < 0.2f)
                    Luminosity = 0.2f;
                IntensityTransform[i][j] = Luminosity;
            }
        }
    }
    cpuTransformsReady_ = true;
}

void BMD::EnsureCpuTransforms() noexcept
{
    TransformCpuMeshes(nullptr, nullptr);
}

void BMD::TransformByObjectBone(vec3_t vResultPosition, const ObjectDrawInput &draw,
                                int iBoneNumber, const vec3_t vRelativePosition)
{
    if (iBoneNumber < 0 || iBoneNumber >= NumBones)
    {
        assert(!"Bone number error");
        return;
    }
    if (draw.source == nullptr)
    {
        assert(!"Empty Bone");
        return;
    }

    const float(*TransformMatrix)[4];
    if (draw.bones != nullptr)
    {
        TransformMatrix = draw.bones[iBoneNumber];
    }
    else
    {
        TransformMatrix = BoneTransform[iBoneNumber];
    }

    vec3_t vTemp;
    if (vRelativePosition == nullptr)
    {
        vTemp[0] = TransformMatrix[0][3];
        vTemp[1] = TransformMatrix[1][3];
        vTemp[2] = TransformMatrix[2][3];
    }
    else
    {
        VectorTransform(vRelativePosition, TransformMatrix, vTemp);
    }
    VectorScale(vTemp, BodyScale, vTemp);
    VectorAdd(vTemp, draw.position, vResultPosition);
}

void BMD::TransformByBoneMatrix(vec3_t vResultPosition, const float (*BoneMatrix)[4],
                                const vec3_t vWorldPosition, const vec3_t vRelativePosition)
{
    if (BoneMatrix == nullptr)
    {
        assert(!"Empty Matrix");
        return;
    }

    vec3_t vTemp;
    if (vRelativePosition == nullptr)
    {
        vTemp[0] = BoneMatrix[0][3];
        vTemp[1] = BoneMatrix[1][3];
        vTemp[2] = BoneMatrix[2][3];
    }
    else
    {
        VectorTransform(vRelativePosition, BoneMatrix, vTemp);
    }
    if (vWorldPosition != nullptr)
    {
        VectorScale(vTemp, BodyScale, vTemp);
        VectorAdd(vTemp, vWorldPosition, vResultPosition);
    }
    else
    {
        VectorScale(vTemp, BodyScale, vResultPosition);
    }
}

void BMD::TransformPosition(const float (*Matrix)[4], const vec3_t Position, vec3_t WorldPosition,
                            bool Translate)
{
    if (Translate)
    {
        vec3_t p;
        VectorTransform(Position, Matrix, p);
        VectorScale(p, BodyScale, p);
        VectorAdd(p, BodyOrigin, WorldPosition);
    }
    else
        VectorTransform(Position, Matrix, WorldPosition);
}

void BMD::RotationPosition(float (*Matrix)[4], vec3_t Position, vec3_t WorldPosition)
{
    vec3_t p;
    VectorRotate(Position, Matrix, p);
    VectorScale(p, BodyScale, WorldPosition);
    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            ParentMatrix[i][j] = Matrix[i][j];
        }
    }
}

bool BMD::PlayAnimation(float *AnimationFrame, float *PriorAnimationFrame,
                        unsigned short *PriorAction, float Speed, vec3_t Origin, vec3_t Angle)
{
    return PlayAnimation(AnimationFrame, PriorAnimationFrame, PriorAction, Speed, Origin, Angle,
                         FPS_ANIMATION_FACTOR);
}

bool BMD::PlayAnimation(float *AnimationFrame, float *PriorAnimationFrame,
                        unsigned short *PriorAction, float Speed, vec3_t Origin, vec3_t Angle,
                        float referenceFrames)
{
    bool Loop = true;

    if (AnimationFrame == nullptr || PriorAnimationFrame == nullptr || PriorAction == nullptr ||
        (NumActions > 0 && CurrentAction >= NumActions))
    {
        return Loop;
    }

    if (NumActions == 0 || Actions[CurrentAction].NumAnimationKeys <= 1)
    {
        return Loop;
    }

    if (referenceFrames <= 0.f || Speed == 0.f)
    {
        CurrentAnimation = *AnimationFrame;
        CurrentAnimationFrame = (int)maxf(0, CurrentAnimation);
        return true;
    }

    const float previousKey = std::floor(*AnimationFrame);
    *AnimationFrame += Speed * referenceFrames;
    const bool crossedKey = previousKey != std::floor(*AnimationFrame);
    const int cycleKeys =
        Actions[CurrentAction].NumAnimationKeys - (Actions[CurrentAction].LockPositions ? 1 : 0);
    if (*AnimationFrame < 0.f)
    {
        *AnimationFrame = std::fmod(*AnimationFrame, static_cast<float>(cycleKeys));
        if (*AnimationFrame < 0.f)
            *AnimationFrame += cycleKeys;
    }

    if (Actions[CurrentAction].Loop)
    {
        if (*AnimationFrame >= (float)Actions[CurrentAction].NumAnimationKeys)
        {
            *AnimationFrame = (float)Actions[CurrentAction].NumAnimationKeys - 0.01f;
            Loop = false;
        }
    }
    else
    {
        int Key;
        if (Actions[CurrentAction].LockPositions)
            Key = Actions[CurrentAction].NumAnimationKeys - 1;
        else
            Key = Actions[CurrentAction].NumAnimationKeys;

        float fTemp;

        if (SceneFlag == CHARACTER_SCENE)
        {
            fTemp = *AnimationFrame + 2;
        }
        else if (gMapManager.ContextMap() == WD_39KANTURU_3RD && CurrentAction == MONSTER01_APEAR)
        {
            fTemp = *AnimationFrame + 1;
        }
        else
        {
            fTemp = *AnimationFrame;
        }

        if (fTemp >= (int)Key)
        {
            auto Frame = (int)*AnimationFrame;
            *AnimationFrame = (float)(Frame % (Key)) + (*AnimationFrame - (float)Frame);
            Loop = false;
        }
    }
    if (crossedKey || !Loop)
    {
        *PriorAction = CurrentAction;
        const int currentKey = static_cast<int>(*AnimationFrame);
        *PriorAnimationFrame = static_cast<float>(currentKey > 0 ? currentKey - 1 : cycleKeys - 1);
    }
    CurrentAnimation = *AnimationFrame;
    CurrentAnimationFrame = (int)maxf(0, CurrentAnimation);

    return Loop;
}
void BMD::AnimationTransformWithAttachHighModel(OBJECT *oHighHierarchyModel,
                                                BMD *bmdHighHierarchyModel,
                                                int iBoneNumberHighHierarchyModel,
                                                vec3_t &vOutPosHighHiearachyModelBone,
                                                vec3_t *arrOutSetfAllBonePositions)
{
    if (NumBones < 1)
        return;
    if (NumBones > MAX_BONES)
        return;

    vec34_t *arrBonesTMLocal;
    vec34_t *arrBonesTMLocalResult;
    vec34_t tmBoneHierarchicalObject;
    vec3_t Temp, v3Position;

    arrBonesTMLocal = new vec34_t[NumBones];
    Vector(0.0f, 0.0f, 0.0f, Temp);

    arrBonesTMLocalResult = new vec34_t[NumBones];

    memset(arrBonesTMLocalResult, 0, sizeof(vec34_t) * NumBones);
    memset(arrBonesTMLocal, 0, sizeof(vec34_t) * NumBones);

    memset(tmBoneHierarchicalObject, 0, sizeof(vec34_t));

    memcpy(tmBoneHierarchicalObject,
           oHighHierarchyModel->BoneTransform[iBoneNumberHighHierarchyModel], sizeof(vec34_t));

    BodyScale = oHighHierarchyModel->Scale;

    tmBoneHierarchicalObject[0][3] = tmBoneHierarchicalObject[0][3] * BodyScale;
    tmBoneHierarchicalObject[1][3] = tmBoneHierarchicalObject[1][3] * BodyScale;
    tmBoneHierarchicalObject[2][3] = tmBoneHierarchicalObject[2][3] * BodyScale;

    if (nullptr != vOutPosHighHiearachyModelBone)
    {
        Vector(tmBoneHierarchicalObject[0][3], tmBoneHierarchicalObject[1][3],
               tmBoneHierarchicalObject[2][3], vOutPosHighHiearachyModelBone);
    }

    VectorCopy(oHighHierarchyModel->Position, v3Position);

    Animation(arrBonesTMLocal, 0, 0, 0, Temp, Temp, false, false);
    for (int i_ = 0; i_ < NumBones; ++i_)
    {
        R_ConcatTransforms(tmBoneHierarchicalObject, arrBonesTMLocal[i_],
                           arrBonesTMLocalResult[i_]);
        arrBonesTMLocalResult[i_][0][3] = arrBonesTMLocalResult[i_][0][3] + v3Position[0];
        arrBonesTMLocalResult[i_][1][3] = arrBonesTMLocalResult[i_][1][3] + v3Position[1];
        arrBonesTMLocalResult[i_][2][3] = arrBonesTMLocalResult[i_][2][3] + v3Position[2];

        Vector(arrBonesTMLocalResult[i_][0][3], arrBonesTMLocalResult[i_][1][3],
               arrBonesTMLocalResult[i_][2][3], arrOutSetfAllBonePositions[i_]);
    }

    delete[] arrBonesTMLocalResult;
    delete[] arrBonesTMLocal;
}

void BMD::AnimationTransformOnlySelf(vec3_t *arrOutSetfAllBonePositions, const vec3_t &v3Angle,
                                     const vec3_t &v3Position, const float &fScale,
                                     OBJECT *oRefAnimation, const float fFrameArea,
                                     const float fWeight)
{
    if (NumBones < 1)
        return;
    if (NumBones > MAX_BONES)
        return;

    vec34_t *arrBonesTMLocal;
    vec3_t v3RootAngle, v3RootPosition;
    float fRootScale;
    vec3_t Temp;

    fRootScale = const_cast<float &>(fScale);

    v3RootAngle[0] = v3Angle[0];
    v3RootAngle[1] = v3Angle[1];
    v3RootAngle[2] = v3Angle[2];

    v3RootPosition[0] = v3Position[0];
    v3RootPosition[1] = v3Position[1];
    v3RootPosition[2] = v3Position[2];

    arrBonesTMLocal = new vec34_t[NumBones];
    Vector(0.0f, 0.0f, 0.0f, Temp);

    memset(arrBonesTMLocal, 0, sizeof(vec34_t) * NumBones);

    if (nullptr == oRefAnimation)
    {
        Animation(arrBonesTMLocal, 0, 0, 0, v3RootAngle, Temp, false, true);
    }
    else
    {
        float fAnimationFrame = oRefAnimation->AnimationFrame,
              fPiriorAnimationFrame = oRefAnimation->PriorAnimationFrame;
        unsigned short iPiriorAction = oRefAnimation->PriorAction;

        if (fWeight >= 0.0f && fFrameArea > 0.0f)
        {
            float fAnimationFrameStart = fAnimationFrame - fFrameArea;
            float fAnimationFrameEnd = fAnimationFrame;
            LInterpolationF(fAnimationFrame, fAnimationFrameStart, fAnimationFrameEnd, fWeight);
        }

        Animation(arrBonesTMLocal, fAnimationFrame, fPiriorAnimationFrame, iPiriorAction,
                  v3RootAngle, Temp, false, true);
    }

    vec3_t v3RelatePos;
    Vector(1.0f, 1.0f, 1.0f, v3RelatePos);
    for (int i_ = 0; i_ < NumBones; ++i_)
    {
        Vector(arrBonesTMLocal[i_][0][3], arrBonesTMLocal[i_][1][3], arrBonesTMLocal[i_][2][3],
               arrOutSetfAllBonePositions[i_]);
    }

    delete[] arrBonesTMLocal;
}

void BMD::Chrome(float *pchrome, int bone, vec3_t normal)
{
    Vector(0.f, 0.f, 1.f, g_vright);

    float n;

    {
        vec3_t chromeupvec;
        vec3_t chromerightvec;
        vec3_t tmp;
        VectorScale(BodyOrigin, -1, tmp);
        VectorNormalize(tmp);
        CrossProduct(tmp, g_vright, chromeupvec);
        VectorNormalize(chromeupvec);
        CrossProduct(tmp, chromeupvec, chromerightvec);
        VectorNormalize(chromerightvec);

        g_chromeage[bone] = g_smodels_total;
    }

    n = DotProduct(normal, g_chromeright[bone]);
    pchrome[0] = (n + 1.f); // FIX: make this a float

    n = DotProduct(normal, g_chromeup[bone]);
    pchrome[1] = (n + 1.f); // FIX: make this a float
}

void BMD::Lighting(float *pLight, Light_t *lp, vec3_t Position, vec3_t Normal)
{
    vec3_t Light;
    VectorSubtract(lp->Position, Position, Light);
    float Length = sqrtf(Light[0] * Light[0] + Light[1] * Light[1] + Light[2] * Light[2]);

    float LightCos = (DotProduct(Normal, Light) / Length) * 0.8f + 0.3f;
    if (Length > lp->Range)
        LightCos -= (Length - lp->Range) * 0.01f;
    if (LightCos < 0.f)
        LightCos = 0.f;
    pLight[0] += LightCos * lp->Color[0];
    pLight[1] += LightCos * lp->Color[1];
    pLight[2] += LightCos * lp->Color[2];
}

// light map

#define AXIS_X 0
#define AXIS_Y 1
#define AXIS_Z 2

void SmoothBitmap(int Width, int Height, unsigned char *Buffer)
{
    int RowStride = Width * 3;
    for (int i = 1; i < Height - 1; i++)
    {
        for (int j = 1; j < Width - 1; j++)
        {
            int Index = (i * Width + j) * 3;
            for (int k = 0; k < 3; k++)
            {
                Buffer[Index] = (Buffer[Index - RowStride - 3] + Buffer[Index - RowStride] +
                                 Buffer[Index - RowStride + 3] + Buffer[Index - 3] +
                                 Buffer[Index + 3] + Buffer[Index + RowStride - 3] +
                                 Buffer[Index + RowStride] + Buffer[Index + RowStride + 3]) /
                                8;
                Index++;
            }
        }
    }
}

bool BMD::CollisionDetectLineToMesh(vec3_t Position, vec3_t Target, bool Collision, int Mesh,
                                    int Triangle)
{
    EnsureCpuTransforms();
    int i, j;
    for (i = 0; i < NumMeshs; i++)
    {
        Mesh_t *m = &Meshs[i];

        for (j = 0; j < m->NumTriangles; j++)
        {
            if (i == Mesh && j == Triangle)
                continue;
            Triangle_t *tp = &m->Triangles[j];
            float *vp1 = VertexTransform[i][tp->VertexIndex[0]];
            float *vp2 = VertexTransform[i][tp->VertexIndex[1]];
            float *vp3 = VertexTransform[i][tp->VertexIndex[2]];
            float *vp4 = VertexTransform[i][tp->VertexIndex[3]];

            vec3_t Normal;
            FaceNormalize(vp1, vp2, vp3, Normal);
            bool success = CollisionDetectLineToFace(Position, Target, tp->Polygon, vp1, vp2, vp3,
                                                     vp4, Normal, Collision);
            if (success == true)
                return true;
        }
    }
    return false;
}

void BMD::CreateLightMapSurface(Light_t *lp, Mesh_t *m, int i, int j, int MapWidth, int MapHeight,
                                int MapWidthMax, int MapHeightMax, vec3_t BoundingMin,
                                vec3_t BoundingMax, int Axis)
{
    int k, l;
    Triangle_t *tp = &m->Triangles[j];
    float *np = NormalTransform[i][tp->NormalIndex[0]];
    float *vp = VertexTransform[i][tp->VertexIndex[0]];
    float d = -DotProduct(vp, np);

    Bitmap_t *lmp = &LightMaps[NumLightMaps];
    if (lmp->Buffer == nullptr)
    {
        lmp->Width = MapWidthMax;
        lmp->Height = MapHeightMax;
        int BufferBytes = lmp->Width * lmp->Height * 3;
        lmp->Buffer = new unsigned char[BufferBytes];
        memset(lmp->Buffer, 0, BufferBytes);
    }

    for (k = 0; k < MapHeight; k++)
    {
        for (l = 0; l < MapWidth; l++)
        {
            vec3_t p;
            Vector(0.f, 0.f, 0.f, p);
            switch (Axis)
            {
            case AXIS_Z:
                p[0] = BoundingMin[0] + l * SubPixel;
                p[1] = BoundingMin[1] + k * SubPixel;
                if (p[0] >= BoundingMax[0])
                    p[0] = BoundingMax[0];
                if (p[1] >= BoundingMax[1])
                    p[1] = BoundingMax[1];
                p[2] = (np[0] * p[0] + np[1] * p[1] + d) / -np[2];
                break;
            case AXIS_Y:
                p[0] = BoundingMin[0] + (float)l * SubPixel;
                p[2] = BoundingMin[2] + (float)k * SubPixel;
                if (p[0] >= BoundingMax[0])
                    p[0] = BoundingMax[0];
                if (p[2] >= BoundingMax[2])
                    p[2] = BoundingMax[2];
                p[1] = (np[0] * p[0] + np[2] * p[2] + d) / -np[1];
                break;
            case AXIS_X:
                p[2] = BoundingMin[2] + l * SubPixel;
                p[1] = BoundingMin[1] + k * SubPixel;
                if (p[2] >= BoundingMax[2])
                    p[2] = BoundingMax[2];
                if (p[1] >= BoundingMax[1])
                    p[1] = BoundingMax[1];
                p[0] = (np[2] * p[2] + np[1] * p[1] + d) / -np[0];
                break;
            }
            vec3_t Direction;
            VectorSubtract(p, lp->Position, Direction);
            VectorNormalize(Direction);
            VectorSubtract(p, Direction, p);
            bool success = CollisionDetectLineToMesh(lp->Position, p, true, i, j);

            if (success == false)
            {
                unsigned char *Bitmap = &lmp->Buffer[(k * MapWidthMax + l) * 3];
                vec3_t Light;
                Vector(0.f, 0.f, 0.f, Light);
                Lighting(Light, lp, p, np);
                for (int c = 0; c < 3; c++)
                {
                    int color = Bitmap[c];
                    color += (unsigned char)(Light[c] * 255.f);
                    if (color > 255)
                        color = 255;
                    Bitmap[c] = color;
                }
            }
        }
    }
}

void BMD::CreateLightMaps()
{
}

void BMD::BeginRender(float Alpha)
{
    glPushMatrix();
}

void BMD::EndRender()
{
    glPopMatrix();
}

void BMD::BeginRenderCoinHeap()
{
    constexpr int meshIndex = 0;
    Mesh_t *m = &Meshs[meshIndex];
    const auto textureIndex = IndexTexture[m->Texture];

    BindTexture(textureIndex);
    DisableAlphaBlend();

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
}

int BMD::AddToCoinHeap(int coinIndex, int target_vertex_index)
{
    EnsureCpuTransforms();
    const auto vertices = RenderArrayVertices;
    const auto colors = RenderArrayColors;
    const auto texCoords = RenderArrayTexCoords;

    constexpr auto alpha = 1.0f;
    constexpr int meshIndex = 0;

    Mesh_t *m = &Meshs[meshIndex];

    for (int j = 0; j < m->NumTriangles; j++)
    {
        const auto triangle = &m->Triangles[j];
        for (int k = 0; k < triangle->Polygon; k++)
        {
            const int source_vertex_index = triangle->VertexIndex[k];
            target_vertex_index++;

            VectorCopy(VertexTransform[meshIndex][source_vertex_index],
                       vertices[target_vertex_index]);

            Vector4(BodyLight[0], BodyLight[1], BodyLight[2], alpha, colors[target_vertex_index]);

            auto texco = m->TexCoords[triangle->TexCoordIndex[k]];
            texCoords[target_vertex_index][0] = texco.TexCoordU;
            texCoords[target_vertex_index][1] = texco.TexCoordV;
        }
    }

    return target_vertex_index;
}

void BMD::EndRenderCoinHeap(int coinCount)
{
    const auto vertices = RenderArrayVertices;
    const auto colors = RenderArrayColors;
    const auto texCoords = RenderArrayTexCoords;

    constexpr std::size_t arrayCapacity = MAX_VERTICES * 3;
    const int drawCount = Meshs[0].NumTriangles * 3 * coinCount;
    const auto boundedCount = std::min<std::size_t>(
        drawCount > 0 ? static_cast<std::size_t>(drawCount) : 0, arrayCapacity);

    (void)LegacyRender().SetClientArray(
        RenderClientArraySemantic::Position,
        std::as_bytes(std::span<const vec3_t>(vertices, boundedCount)), 3,
        RenderClientArrayScalarType::Float, 0, false);
    (void)LegacyRender().SetClientArray(
        RenderClientArraySemantic::Color,
        std::as_bytes(std::span<const vec4_t>(colors, boundedCount)), 4,
        RenderClientArrayScalarType::Float, 0, false);
    (void)LegacyRender().SetClientArray(
        RenderClientArraySemantic::TextureCoordinate,
        std::as_bytes(std::span<const vec2_t>(texCoords, boundedCount)), 2,
        RenderClientArrayScalarType::Float, 0, false);

    constexpr int meshIndex = 0;
    Mesh_t *m = &Meshs[meshIndex];
    glDrawArrays(GL_TRIANGLES, 0, m->NumTriangles * 3 * coinCount);

    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

BMD::MeshDrawScope::MeshDrawScope(BMD &model, int index, const Mesh_t *mesh) noexcept
    : model_(model), previousIndex_(model.drawMeshIndex_), previousMesh_(model.drawMesh_)
{
    model_.drawMeshIndex_ = index;
    model_.drawMesh_ = mesh;
}

BMD::MeshDrawScope::~MeshDrawScope()
{
    model_.drawMeshIndex_ = previousIndex_;
    model_.drawMesh_ = previousMesh_;
}

void BMD::RenderMesh(int meshIndex, int renderFlags, float alpha, int blendMeshIndex,
                     float blendMeshAlpha, float blendMeshTextureCoordU,
                     float blendMeshTextureCoordV, int explicitTextureIndex)
{
    if (meshIndex >= NumMeshs || meshIndex < 0)
        return;

    const Mesh_t *m = MeshForDraw(meshIndex);
    if (m->NumTriangles == 0)
        return;

    float wave = static_cast<long>(WorldTime) % 10000 * 0.0001f;

    int textureIndex = IndexTexture[m->Texture];
    if (textureIndex == BITMAP_HIDE)
    {
        return;
    }

    if (textureIndex == BITMAP_WATER)
    {
        textureIndex = BITMAP_WATER + WaterTextureNumber;
    }

    if (explicitTextureIndex != -1)
    {
        textureIndex = explicitTextureIndex;
    }

    const auto texture = Bitmaps.GetTextureProperties(textureIndex);
    if (texture->isSkin && HideSkin)
    {
        return;
    }

    if (texture->isHair && HideSkin)
    {
        return;
    }

    const bool gpuGeometryReady =
        !(drawMesh_ && drawMeshIndex_ == meshIndex) && renderTapePaletteReady_ &&
        GeometryForDraw().vertices != nullptr && GeometryForDraw().indices != nullptr &&
        !LegacyRender().UsesPolygonLineMode();
    if (!gpuGeometryReady)
        EnsureCpuTransforms();

    bool EnableWave = false;
    int streamMesh = static_cast<u_char>(this->StreamMesh);
    if (m->m_csTScript != nullptr)
    {
        if (m->m_csTScript->getStreamMesh())
        {
            streamMesh = meshIndex;
        }
    }

    if ((meshIndex == blendMeshIndex || meshIndex == streamMesh) &&
        (blendMeshTextureCoordU != 0.f || blendMeshTextureCoordV != 0.f))
    {
        EnableWave = true;
    }

    bool enableLight = LightEnable;
    if (meshIndex == StreamMesh)
    {
        glColor3fv(BodyLight);
        enableLight = false;
    }
    else if (enableLight && !gpuGeometryReady)
    {
        for (int j = 0; j < m->NumNormals; j++)
        {
            VectorScale(BodyLight, IntensityTransform[meshIndex][j], LightTransform[meshIndex][j]);
        }
    }

    int finalRenderFlags = renderFlags;
    if ((renderFlags & RENDER_COLOR) == RENDER_COLOR)
    {
        finalRenderFlags = RENDER_COLOR;
        if ((renderFlags & RENDER_BRIGHT) == RENDER_BRIGHT)
        {
            EnableAlphaBlend();
        }
        else if ((renderFlags & RENDER_DARK) == RENDER_DARK)
        {
            EnableAlphaBlendMinus();
        }
        else
        {
            DisableAlphaBlend();
        }

        if ((renderFlags & RENDER_NODEPTH) == RENDER_NODEPTH)
        {
            DisableDepthTest();
        }

        DisableTexture();
        if (alpha >= 0.99f)
        {
            glColor3fv(BodyLight);
        }
        else
        {
            EnableAlphaTest();
            glColor4f(BodyLight[0], BodyLight[1], BodyLight[2], alpha);
        }
    }
    else if ((renderFlags & RENDER_CHROME) == RENDER_CHROME ||
             (renderFlags & RENDER_CHROME2) == RENDER_CHROME2 ||
             (renderFlags & RENDER_CHROME3) == RENDER_CHROME3 ||
             (renderFlags & RENDER_CHROME4) == RENDER_CHROME4 ||
             (renderFlags & RENDER_CHROME5) == RENDER_CHROME5 ||
             (renderFlags & RENDER_CHROME6) == RENDER_CHROME6 ||
             (renderFlags & RENDER_CHROME7) == RENDER_CHROME7 ||
             (renderFlags & RENDER_METAL) == RENDER_METAL ||
             (renderFlags & RENDER_OIL) == RENDER_OIL)
    {
        if (m->m_csTScript != nullptr)
        {
            if (m->m_csTScript->getNoneBlendMesh())
                return;
        }

        if (m->NoneBlendMesh)
            return;

        finalRenderFlags = RENDER_CHROME;
        if ((renderFlags & RENDER_CHROME4) == RENDER_CHROME4)
        {
            finalRenderFlags = RENDER_CHROME4;
        }
        if ((renderFlags & RENDER_OIL) == RENDER_OIL)
        {
            finalRenderFlags = RENDER_OIL;
        }

        float Wave2 = (int)WorldTime % 5000 * 0.00024f - 0.4f;

        vec3_t L = {(float)(cos(WorldTime * 0.001f)), (float)(sin(WorldTime * 0.002f)), 1.f};
        if (!gpuGeometryReady)
        {
            for (int j = 0; j < m->NumNormals; j++)
            {
                if (j > MAX_VERTICES)
                    break;
                const auto normal = NormalTransform[meshIndex][j];

                if ((renderFlags & RENDER_CHROME2) == RENDER_CHROME2)
                {
                    g_chrome[j][0] = (normal[2] + normal[0]) * 0.8f + Wave2 * 2.f;
                    g_chrome[j][1] = (normal[1] + normal[0]) * 1.0f + Wave2 * 3.f;
                }
                else if ((renderFlags & RENDER_CHROME3) == RENDER_CHROME3)
                {
                    g_chrome[j][0] = DotProduct(normal, LightVector);
                    g_chrome[j][1] = 1.f - DotProduct(normal, LightVector);
                }
                else if ((renderFlags & RENDER_CHROME4) == RENDER_CHROME4)
                {
                    g_chrome[j][0] = DotProduct(normal, L);
                    g_chrome[j][1] = 1.f - DotProduct(normal, L);
                    g_chrome[j][1] -= normal[2] * 0.5f + wave * 3.f;
                    g_chrome[j][0] += normal[1] * 0.5f + L[1] * 3.f;
                }
                else if ((renderFlags & RENDER_CHROME5) == RENDER_CHROME5)
                {
                    g_chrome[j][0] = DotProduct(normal, L);
                    g_chrome[j][1] = 1.f - DotProduct(normal, L);
                    g_chrome[j][1] -= normal[2] * 2.5f + wave * 1.f;
                    g_chrome[j][0] += normal[1] * 3.f + L[1] * 5.f;
                }
                else if ((renderFlags & RENDER_CHROME6) == RENDER_CHROME6)
                {
                    g_chrome[j][0] = (normal[2] + normal[0]) * 0.8f + Wave2 * 2.f;
                    g_chrome[j][1] = (normal[2] + normal[0]) * 0.8f + Wave2 * 2.f;
                }
                else if ((renderFlags & RENDER_CHROME7) == RENDER_CHROME7)
                {
                    g_chrome[j][0] =
                        (normal[2] + normal[0]) * 0.8f + static_cast<float>(WorldTime) * 0.00006f;
                    g_chrome[j][1] =
                        (normal[2] + normal[0]) * 0.8f + static_cast<float>(WorldTime) * 0.00006f;
                }
                else if ((renderFlags & RENDER_OIL) == RENDER_OIL)
                {
                    g_chrome[j][0] = normal[0];
                    g_chrome[j][1] = normal[1];
                }
                else if ((renderFlags & RENDER_CHROME) == RENDER_CHROME)
                {
                    g_chrome[j][0] = normal[2] * 0.5f + wave;
                    g_chrome[j][1] = normal[1] * 0.5f + wave * 2.f;
                }
                else
                {
                    g_chrome[j][0] = normal[2] * 0.5f + 0.2f;
                    g_chrome[j][1] = normal[1] * 0.5f + 0.5f;
                }
            }
        }

        if ((renderFlags & RENDER_CHROME3) == RENDER_CHROME3 ||
            (renderFlags & RENDER_CHROME4) == RENDER_CHROME4 ||
            (renderFlags & RENDER_CHROME5) == RENDER_CHROME5 ||
            (renderFlags & RENDER_CHROME7) == RENDER_CHROME7 ||
            (renderFlags & RENDER_BRIGHT) == RENDER_BRIGHT)
        {
            if (alpha < 0.99f)
            {
                BodyLight[0] *= alpha;
                BodyLight[1] *= alpha;
                BodyLight[2] *= alpha;
            }

            EnableAlphaBlend();
        }
        else if ((renderFlags & RENDER_DARK) == RENDER_DARK)
            EnableAlphaBlendMinus();
        else if ((renderFlags & RENDER_LIGHTMAP) == RENDER_LIGHTMAP)
            EnableLightMap();
        else if (alpha >= 0.99f)
        {
            DisableAlphaBlend();
        }
        else
        {
            EnableAlphaTest();
        }

        if ((renderFlags & RENDER_NODEPTH) == RENDER_NODEPTH)
        {
            DisableDepthTest();
        }

        if (explicitTextureIndex == -1)
        {
            if ((renderFlags & RENDER_CHROME2) == RENDER_CHROME2)
            {
                BindTexture(BITMAP_CHROME2);
            }
            else if ((renderFlags & RENDER_CHROME3) == RENDER_CHROME3)
            {
                BindTexture(BITMAP_CHROME2);
            }
            else if ((renderFlags & RENDER_CHROME4) == RENDER_CHROME4)
            {
                BindTexture(BITMAP_CHROME2);
            }
            else if ((renderFlags & RENDER_CHROME6) == RENDER_CHROME6)
            {
                BindTexture(BITMAP_CHROME6);
            }
            else if ((renderFlags & RENDER_CHROME) == RENDER_CHROME)
            {
                BindTexture(BITMAP_CHROME);
            }
            else if ((renderFlags & RENDER_METAL) == RENDER_METAL)
            {
                BindTexture(BITMAP_SHINY);
            }
        }
        else
        {
            BindTexture(textureIndex);
        }
    }
    else if (blendMeshIndex <= -2 || m->Texture == blendMeshIndex)
    {
        finalRenderFlags = RENDER_TEXTURE;
        BindTexture(textureIndex);
        if ((renderFlags & RENDER_DARK) == RENDER_DARK)
            EnableAlphaBlendMinus();
        else
            EnableAlphaBlend();

        if ((renderFlags & RENDER_NODEPTH) == RENDER_NODEPTH)
        {
            DisableDepthTest();
        }

        glColor3f(BodyLight[0] * blendMeshAlpha, BodyLight[1] * blendMeshAlpha,
                  BodyLight[2] * blendMeshAlpha);
        //glColor3f(BlendMeshLight,BlendMeshLight,BlendMeshLight);
        enableLight = false;
    }
    else if ((renderFlags & RENDER_TEXTURE) == RENDER_TEXTURE)
    {
        finalRenderFlags = RENDER_TEXTURE;
        BindTexture(textureIndex);
        if ((renderFlags & RENDER_BRIGHT) == RENDER_BRIGHT)
        {
            EnableAlphaBlend();
        }
        else if ((renderFlags & RENDER_DARK) == RENDER_DARK)
        {
            EnableAlphaBlendMinus();
        }
        else if (alpha < 0.99f || texture->components == 4)
        {
            EnableAlphaTest();
        }
        else
        {
            DisableAlphaBlend();
        }

        if ((renderFlags & RENDER_NODEPTH) == RENDER_NODEPTH)
        {
            DisableDepthTest();
        }
    }
    else if ((renderFlags & RENDER_BRIGHT) == RENDER_BRIGHT)
    {
        if (texture->components == 4 || m->Texture == blendMeshIndex)
        {
            return;
        }

        finalRenderFlags = RENDER_BRIGHT;
        EnableAlphaBlend();
        DisableTexture();
        DisableDepthMask();

        if ((renderFlags & RENDER_NODEPTH) == RENDER_NODEPTH)
        {
            DisableDepthTest();
        }
    }
    else
    {
        finalRenderFlags = RENDER_TEXTURE;
    }
    if (renderFlags & RENDER_DOPPELGANGER)
    {
        if (texture->components != 4)
        {
            EnableCullFace();
            EnableDepthMask();
        }
    }

    const bool enableColor =
        ModelGeometryDetail::UsesRenderMeshVertexColors(enableLight, finalRenderFlags);
    const std::array<float, 4> &latchedColor = LegacyRender().CurrentColor();
    const std::array<float, 3> &latchedNormal = LegacyRender().CurrentNormal();
    const std::array<float, 4> bodyColor = {BodyLight[0], BodyLight[1], BodyLight[2], alpha};
    const std::array<float, 4> &baseColor =
        enableColor && finalRenderFlags != RENDER_TEXTURE ? bodyColor : latchedColor;
    const std::size_t drawCount = static_cast<std::size_t>(m->NumTriangles) * 3;
    if (gpuGeometryReady)
    {
        RenderTapeBmdConstants bmd = renderTapeTransform_;
        bmd.uvMode = SelectRenderTapeUvMode(renderFlags);
        bmd.lighting = enableLight && finalRenderFlags == RENDER_TEXTURE;
        bmd.uvScroll = EnableWave && finalRenderFlags == RENDER_TEXTURE;
        bmd.wave = (renderFlags & RENDER_SHADOWMAP) != RENDER_SHADOWMAP &&
                   (renderFlags & RENDER_WAVE) == RENDER_WAVE;
        bmd.waveTime = static_cast<float>(static_cast<int>(WorldTime));
        bmd.alpha = alpha;
        std::copy_n(BodyLight, 3, bmd.bodyLight.begin());
        bmd.wavePhase = wave;
        bmd.baseColor = baseColor;
        bmd.chromeWave = static_cast<int>(WorldTime) % 5000 * 0.00024F - 0.4F;
        bmd.uvOffset = {blendMeshTextureCoordU, blendMeshTextureCoordV};
        bmd.chromeTime = static_cast<float>(WorldTime) * 0.00006F;
        bmd.chromeLight = {static_cast<float>(cos(WorldTime * 0.001F)),
                           static_cast<float>(sin(WorldTime * 0.002F)), 1.0F};
        std::copy_n(LightVector, 3, bmd.legacyLight.begin());
        (void)LegacyRender().DrawBmdGeometry(
            GeometryForDraw(), m->RenderTapeVertexOffset,
            static_cast<std::uint32_t>(m->RenderTapeVertexSources->size()),
            m->RenderTapeIndexOffset, static_cast<std::uint32_t>(m->RenderTapeIndices->size()),
            bmd);
    }
    else if (!LegacyRender().UsesPolygonLineMode() && m->RenderTapeVertexSources != nullptr)
    {
        (void)LegacyRender().WriteIndexedTriangles(
            m->RenderTapeVertexSources->size(), m->RenderTapeIndices->size(),
            [&](std::span<RenderTapeVertex> vertices, std::span<std::uint32_t> indices) noexcept {
                std::memcpy(indices.data(), m->RenderTapeIndices->data(), indices.size_bytes());
                const BmdRenderTapeVertexSource *source = m->RenderTapeVertexSources->data();
                RenderTapeVertex *vertex = vertices.data();
                const RenderTapeVertex *const vertexEnd = vertex + vertices.size();
                for (; vertex != vertexEnd; ++vertex, ++source)
                {
                    const int sourceVertexIndex = source->vertexIndex;
                    const int normalIndex = source->normalIndex;
                    const float *position = VertexTransform[meshIndex][sourceVertexIndex];
                    vertex->position = {position[0], position[1], position[2], 1.0F};
                    vertex->color = baseColor;
                    const TexCoord_t &texCoord = m->TexCoords[source->texCoordIndex];
                    vertex->textureCoordinate = {texCoord.TexCoordU, texCoord.TexCoordV};
                    vertex->normal = latchedNormal;

                    switch (finalRenderFlags)
                    {
                    case RENDER_TEXTURE:
                        if (EnableWave)
                        {
                            vertex->textureCoordinate[0] += blendMeshTextureCoordU;
                            vertex->textureCoordinate[1] += blendMeshTextureCoordV;
                        }
                        if (enableLight)
                        {
                            const float *light = LightTransform[meshIndex][normalIndex];
                            vertex->color = {light[0], light[1], light[2], alpha};
                        }
                        break;
                    case RENDER_CHROME:
                        vertex->textureCoordinate = {g_chrome[normalIndex][0],
                                                     g_chrome[normalIndex][1]};
                        break;
                    case RENDER_CHROME4:
                        vertex->textureCoordinate = {
                            g_chrome[normalIndex][0] + blendMeshTextureCoordU,
                            g_chrome[normalIndex][1] + blendMeshTextureCoordV};
                        break;
                    case RENDER_OIL:
                        vertex->textureCoordinate = {
                            g_chrome[normalIndex][0] * vertex->textureCoordinate[0] +
                                blendMeshTextureCoordU,
                            g_chrome[normalIndex][1] * vertex->textureCoordinate[1] +
                                blendMeshTextureCoordV};
                        break;
                    }

                    if ((renderFlags & RENDER_SHADOWMAP) != RENDER_SHADOWMAP &&
                        (renderFlags & RENDER_WAVE) == RENDER_WAVE)
                    {
                        const float timeSin = sinf(static_cast<float>(static_cast<int>(WorldTime) +
                                                                      sourceVertexIndex * 931) *
                                                   0.007F) *
                                              28.0F;
                        const float *normal = NormalTransform[meshIndex][normalIndex];
                        for (int coordinate = 0; coordinate < 3; ++coordinate)
                        {
                            vertex->position[coordinate] += normal[coordinate] * timeSin;
                        }
                    }
                }
            });
    }
    else
    {
        (void)LegacyRender().WriteTriangles(
            drawCount, [&](std::span<RenderTapeVertex> vertices) noexcept {
                RenderTapeVertex *targetVertex = vertices.data();
                for (int j = 0; j < m->NumTriangles; ++j)
                {
                    const Triangle_t &triangle = m->Triangles[j];
                    for (int k = 0; k < triangle.Polygon; ++k)
                    {
                        const int sourceVertexIndex = triangle.VertexIndex[k];
                        const int normalIndex = triangle.NormalIndex[k];
                        RenderTapeVertex &vertex = *targetVertex++;
                        const float *position = VertexTransform[meshIndex][sourceVertexIndex];
                        vertex.position = {position[0], position[1], position[2], 1.0F};
                        vertex.color = baseColor;
                        const TexCoord_t &texCoord = m->TexCoords[triangle.TexCoordIndex[k]];
                        vertex.textureCoordinate = {texCoord.TexCoordU, texCoord.TexCoordV};
                        vertex.normal = latchedNormal;

                        switch (finalRenderFlags)
                        {
                        case RENDER_TEXTURE:
                            if (EnableWave)
                            {
                                vertex.textureCoordinate[0] += blendMeshTextureCoordU;
                                vertex.textureCoordinate[1] += blendMeshTextureCoordV;
                            }
                            if (enableLight)
                            {
                                const float *light = LightTransform[meshIndex][normalIndex];
                                vertex.color = {light[0], light[1], light[2], alpha};
                            }
                            break;
                        case RENDER_CHROME:
                            vertex.textureCoordinate = {g_chrome[normalIndex][0],
                                                        g_chrome[normalIndex][1]};
                            break;
                        case RENDER_CHROME4:
                            vertex.textureCoordinate = {
                                g_chrome[normalIndex][0] + blendMeshTextureCoordU,
                                g_chrome[normalIndex][1] + blendMeshTextureCoordV};
                            break;
                        case RENDER_OIL:
                            vertex.textureCoordinate = {
                                g_chrome[normalIndex][0] * vertex.textureCoordinate[0] +
                                    blendMeshTextureCoordU,
                                g_chrome[normalIndex][1] * vertex.textureCoordinate[1] +
                                    blendMeshTextureCoordV};
                            break;
                        }

                        if ((renderFlags & RENDER_SHADOWMAP) != RENDER_SHADOWMAP &&
                            (renderFlags & RENDER_WAVE) == RENDER_WAVE)
                        {
                            const float timeSin =
                                sinf(static_cast<float>(static_cast<int>(WorldTime) +
                                                        sourceVertexIndex * 931) *
                                     0.007F) *
                                28.0F;
                            const float *normal = NormalTransform[meshIndex][normalIndex];
                            for (int coordinate = 0; coordinate < 3; ++coordinate)
                            {
                                vertex.position[coordinate] += normal[coordinate] * timeSin;
                            }
                        }
                    }
                }
            });
    }

    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    if (enableColor)
        glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

void BMD::RenderMeshAlternative(int iRndExtFlag, int iParam, int i, int RenderFlag, float Alpha,
                                int BlendMesh, float BlendMeshLight, float BlendMeshTexCoordU,
                                float BlendMeshTexCoordV, int MeshTexture)
{
    if (i >= NumMeshs || i < 0)
        return;

    const Mesh_t *m = MeshForDraw(i);
    if (m->NumTriangles == 0)
        return;
    float Wave = (int)WorldTime % 10000 * 0.0001f;

    int Texture = IndexTexture[m->Texture];
    if (Texture == BITMAP_HIDE)
        return;
    if (MeshTexture != -1)
        Texture = MeshTexture;

    const auto texture = Bitmaps.GetTextureProperties(Texture);
    if (!texture)
    {
        return;
    }
    EnsureCpuTransforms();

    bool EnableWave = false;
    int streamMesh = StreamMesh;
    if (m->m_csTScript != nullptr)
    {
        if (m->m_csTScript->getStreamMesh())
        {
            streamMesh = i;
        }
    }
    if ((i == BlendMesh || i == streamMesh) &&
        (BlendMeshTexCoordU != 0.f || BlendMeshTexCoordV != 0.f))
        EnableWave = true;

    bool EnableLight = LightEnable;
    if (i == StreamMesh)
    {
        //vec3_t Light;
        //Vector(1.f,1.f,1.f,Light);
        glColor3fv(BodyLight);
        EnableLight = false;
    }
    else if (EnableLight)
    {
        for (int j = 0; j < m->NumNormals; j++)
        {
            VectorScale(BodyLight, IntensityTransform[i][j], LightTransform[i][j]);
        }
    }

    int Render = RenderFlag;
    if ((RenderFlag & RENDER_COLOR) == RENDER_COLOR)
    {
        Render = RENDER_COLOR;
        if ((RenderFlag & RENDER_BRIGHT) == RENDER_BRIGHT)
            EnableAlphaBlend();
        else if ((RenderFlag & RENDER_DARK) == RENDER_DARK)
            EnableAlphaBlendMinus();
        else
            DisableAlphaBlend();

        if ((RenderFlag & RENDER_NODEPTH) == RENDER_NODEPTH)
        {
            DisableDepthTest();
        }

        DisableTexture();
        if (Alpha >= 0.99f)
        {
            glColor3fv(BodyLight);
        }
        else
        {
            EnableAlphaTest();
            glColor4f(BodyLight[0], BodyLight[1], BodyLight[2], Alpha);
        }
    }
    else if ((RenderFlag & RENDER_CHROME) == RENDER_CHROME ||
             (RenderFlag & RENDER_CHROME2) == RENDER_CHROME2 ||
             (RenderFlag & RENDER_CHROME3) == RENDER_CHROME3 ||
             (RenderFlag & RENDER_CHROME4) == RENDER_CHROME4 ||
             (RenderFlag & RENDER_CHROME5) == RENDER_CHROME5 ||
             (RenderFlag & RENDER_CHROME7) == RENDER_CHROME7 ||
             (RenderFlag & RENDER_METAL) == RENDER_METAL || (RenderFlag & RENDER_OIL) == RENDER_OIL)
    {
        if (m->m_csTScript != nullptr)
        {
            if (m->m_csTScript->getNoneBlendMesh())
                return;
        }
        if (m->NoneBlendMesh)
            return;
        Render = RENDER_CHROME;
        if ((RenderFlag & RENDER_CHROME4) == RENDER_CHROME4)
        {
            Render = RENDER_CHROME4;
        }
        float Wave2 = (int)WorldTime % 5000 * 0.00024f - 0.4f;

        vec3_t L = {(float)(cos(WorldTime * 0.001f)), (float)(sin(WorldTime * 0.002f)), 1.f};
        for (int j = 0; j < m->NumNormals; j++)
        {
            if (j > MAX_VERTICES)
                break;
            float *Normal = NormalTransform[i][j];

            if ((RenderFlag & RENDER_CHROME2) == RENDER_CHROME2)
            {
                g_chrome[j][0] = (Normal[2] + Normal[0]) * 0.8f + Wave2 * 2.f;
                g_chrome[j][1] = (Normal[1] + Normal[0]) * 1.0f + Wave2 * 3.f;
            }
            else if ((RenderFlag & RENDER_CHROME3) == RENDER_CHROME3)
            {
                g_chrome[j][0] = DotProduct(Normal, LightVector);
                g_chrome[j][1] = 1.f - DotProduct(Normal, LightVector);
            }
            else if ((RenderFlag & RENDER_CHROME4) == RENDER_CHROME4)
            {
                g_chrome[j][0] = DotProduct(Normal, L);
                g_chrome[j][1] = 1.f - DotProduct(Normal, L);
                g_chrome[j][1] -= Normal[2] * 0.5f + Wave * 3.f;
                g_chrome[j][0] += Normal[1] * 0.5f + L[1] * 3.f;
            }
            else if ((RenderFlag & RENDER_CHROME5) == RENDER_CHROME5)
            {
                Vector(0.1f, -0.23f, 0.22f, LightVector2);

                g_chrome[j][0] =
                    (DotProduct(Normal, LightVector2) /*+ Normal[1] + LightVector2[1]*3.f */) /
                    1.08f;
                g_chrome[j][1] =
                    (1.f - DotProduct(Normal, LightVector2) /*- Normal[2]*0.5f + 3.f */) / 1.08f;
            }
            else if ((RenderFlag & RENDER_CHROME6) == RENDER_CHROME6)
            {
                g_chrome[j][0] = (Normal[2] + Normal[0]) * 0.8f + Wave2 * 2.f;
                g_chrome[j][1] = (Normal[1] + Normal[0]) * 1.0f + Wave2 * 3.f;
            }
            else if ((RenderFlag & RENDER_CHROME7) == RENDER_CHROME7)
            {
                Vector(0.1f, -0.23f, 0.22f, LightVector2);

                g_chrome[j][0] = (DotProduct(Normal, LightVector2)) / 1.08f;
                g_chrome[j][1] = (1.f - DotProduct(Normal, LightVector2)) / 1.08f;
            }
            else if ((RenderFlag & RENDER_CHROME) == RENDER_CHROME)
            {
                g_chrome[j][0] = Normal[2] * 0.5f + Wave;
                g_chrome[j][1] = Normal[1] * 0.5f + Wave * 2.f;
            }
            else
            {
                g_chrome[j][0] = Normal[2] * 0.5f + 0.2f;
                g_chrome[j][1] = Normal[1] * 0.5f + 0.5f;
            }
        }

        if ((RenderFlag & RENDER_CHROME3) == RENDER_CHROME3 ||
            (RenderFlag & RENDER_CHROME4) == RENDER_CHROME4 ||
            (RenderFlag & RENDER_CHROME5) == RENDER_CHROME5 ||
            (RenderFlag & RENDER_CHROME7) == RENDER_CHROME7)
        {
            if (Alpha < 0.99f)
            {
                BodyLight[0] *= Alpha;
                BodyLight[1] *= Alpha;
                BodyLight[2] *= Alpha;
            }
            EnableAlphaBlend();
        }
        else if ((RenderFlag & RENDER_BRIGHT) == RENDER_BRIGHT)
        {
            if (Alpha < 0.99f)
            {
                BodyLight[0] *= Alpha;
                BodyLight[1] *= Alpha;
                BodyLight[2] *= Alpha;
            }
            EnableAlphaBlend();
        }
        else if ((RenderFlag & RENDER_DARK) == RENDER_DARK)
            EnableAlphaBlendMinus();
        else if ((RenderFlag & RENDER_LIGHTMAP) == RENDER_LIGHTMAP)
            EnableLightMap();
        else if (Alpha >= 0.99f)
        {
            DisableAlphaBlend();
        }
        else
        {
            EnableAlphaTest();
        }

        if ((RenderFlag & RENDER_NODEPTH) == RENDER_NODEPTH)
        {
            DisableDepthTest();
        }

        if ((RenderFlag & RENDER_CHROME2) == RENDER_CHROME2 && MeshTexture == -1)
        {
            BindTexture(BITMAP_CHROME2);
        }
        else if ((RenderFlag & RENDER_CHROME3) == RENDER_CHROME3 && MeshTexture == -1)
        {
            BindTexture(BITMAP_CHROME2);
        }
        else if ((RenderFlag & RENDER_CHROME4) == RENDER_CHROME4 && MeshTexture == -1)
        {
            BindTexture(BITMAP_CHROME2);
        }
        else if ((RenderFlag & RENDER_CHROME) == RENDER_CHROME && MeshTexture == -1)
            BindTexture(BITMAP_CHROME);
        else if ((RenderFlag & RENDER_METAL) == RENDER_METAL && MeshTexture == -1)
            BindTexture(BITMAP_SHINY);
        else
            BindTexture(Texture);
    }
    else if (BlendMesh <= -2 || m->Texture == BlendMesh)
    {
        Render = RENDER_TEXTURE;
        BindTexture(Texture);
        if ((RenderFlag & RENDER_DARK) == RENDER_DARK)
            EnableAlphaBlendMinus();
        else
            EnableAlphaBlend();

        if ((RenderFlag & RENDER_NODEPTH) == RENDER_NODEPTH)
        {
            DisableDepthTest();
        }

        glColor3f(BodyLight[0] * BlendMeshLight, BodyLight[1] * BlendMeshLight,
                  BodyLight[2] * BlendMeshLight);
        //glColor3f(BlendMeshLight,BlendMeshLight,BlendMeshLight);
        EnableLight = false;
    }
    else if ((RenderFlag & RENDER_TEXTURE) == RENDER_TEXTURE)
    {
        Render = RENDER_TEXTURE;
        BindTexture(Texture);
        if ((RenderFlag & RENDER_BRIGHT) == RENDER_BRIGHT)
        {
            EnableAlphaBlend();
        }
        else if ((RenderFlag & RENDER_DARK) == RENDER_DARK)
        {
            EnableAlphaBlendMinus();
        }
        else if (Alpha < 0.99f || texture->components == 4)
        {
            EnableAlphaTest();
        }
        else
        {
            DisableAlphaBlend();
        }

        if ((RenderFlag & RENDER_NODEPTH) == RENDER_NODEPTH)
        {
            DisableDepthTest();
        }
    }
    else if ((RenderFlag & RENDER_BRIGHT) == RENDER_BRIGHT)
    {
        if (texture->components == 4 || m->Texture == BlendMesh)
        {
            return;
        }
        Render = RENDER_BRIGHT;
        EnableAlphaBlend();
        DisableTexture();
        DisableDepthMask();

        if ((RenderFlag & RENDER_NODEPTH) == RENDER_NODEPTH)
        {
            DisableDepthTest();
        }
    }
    else
    {
        Render = RENDER_TEXTURE;
    }

    // ver 1.0 (triangle)
    glBegin(GL_TRIANGLES);
    for (int j = 0; j < m->NumTriangles; j++)
    {
        Triangle_t *tp = &m->Triangles[j];
        for (int k = 0; k < tp->Polygon; k++)
        {
            int vi = tp->VertexIndex[k];
            switch (Render)
            {
            case RENDER_TEXTURE: {
                TexCoord_t *texp = &m->TexCoords[tp->TexCoordIndex[k]];
                if (EnableWave)
                    glTexCoord2f(texp->TexCoordU + BlendMeshTexCoordU,
                                 texp->TexCoordV + BlendMeshTexCoordV);
                else
                    glTexCoord2f(texp->TexCoordU, texp->TexCoordV);
                if (EnableLight)
                {
                    int ni = tp->NormalIndex[k];
                    if (Alpha >= 0.99f)
                    {
                        glColor3fv(LightTransform[i][ni]);
                    }
                    else
                    {
                        float *Light = LightTransform[i][ni];
                        glColor4f(Light[0], Light[1], Light[2], Alpha);
                    }
                }
                break;
            }
            case RENDER_CHROME: {
                if (Alpha >= 0.99f)
                    glColor3fv(BodyLight);
                else
                    glColor4f(BodyLight[0], BodyLight[1], BodyLight[2], Alpha);
                int ni = tp->NormalIndex[k];
                glTexCoord2f(g_chrome[ni][0], g_chrome[ni][1]);
                break;
            }
            }
            if ((iRndExtFlag & RNDEXT_WAVE))
            {
                float vPos[3];
                float fParam = (float)((int)WorldTime + vi * 931) * 0.007f;
                float fSin = sinf(fParam);
                int ni = tp->NormalIndex[k];
                float *Normal = NormalTransform[i][ni];
                for (int iCoord = 0; iCoord < 3; ++iCoord)
                {
                    vPos[iCoord] = VertexTransform[i][vi][iCoord] + Normal[iCoord] * fSin * 28.0f;
                }
                glVertex3fv(vPos);
            }
            else
            {
                glVertex3fv(VertexTransform[i][vi]);
            }
        }
    }
    glEnd();
}

void BMD::RenderBody(int Flag, float Alpha, int BlendMesh, float BlendMeshLight,
                     float BlendMeshTexCoordU, float BlendMeshTexCoordV, int HiddenMesh,
                     int Texture)
{
    if (NumMeshs == 0)
        return;

    int iBlendMesh = BlendMesh;
    BeginRender(Alpha);
    if (!LightEnable)
    {
        if (Alpha >= 0.99f)
            glColor3fv(BodyLight);
        else
            glColor4f(BodyLight[0], BodyLight[1], BodyLight[2], Alpha);
    }
    for (int i = 0; i < NumMeshs; i++)
    {
        iBlendMesh = BlendMesh;

        Mesh_t *m = &Meshs[i];
        if (m->m_csTScript != nullptr)
        {
            if (m->m_csTScript->getHiddenMesh() == false && i != HiddenMesh)
            {
                if (m->m_csTScript->getBright())
                {
                    iBlendMesh = i;
                }
                RenderMesh(i, Flag, Alpha, iBlendMesh, BlendMeshLight, BlendMeshTexCoordU,
                           BlendMeshTexCoordV, Texture);

                BYTE shadowType = m->m_csTScript->getShadowMesh();
                if (shadowType == SHADOW_RENDER_COLOR)
                {
                    DisableAlphaBlend();
                    if (Alpha >= 0.99f)
                        glColor3f(0.f, 0.f, 0.f);
                    else
                        glColor4f(0.f, 0.f, 0.f, Alpha);

                    RenderMesh(i, RENDER_COLOR | RENDER_SHADOWMAP, Alpha, iBlendMesh,
                               BlendMeshLight, BlendMeshTexCoordU, BlendMeshTexCoordV);
                    glColor3f(1.f, 1.f, 1.f);
                }
                else if (shadowType == SHADOW_RENDER_TEXTURE)
                {
                    DisableAlphaBlend();
                    if (Alpha >= 0.99f)
                        glColor3f(0.f, 0.f, 0.f);
                    else
                        glColor4f(0.f, 0.f, 0.f, Alpha);

                    RenderMesh(i, RENDER_TEXTURE | RENDER_SHADOWMAP, Alpha, iBlendMesh,
                               BlendMeshLight, BlendMeshTexCoordU, BlendMeshTexCoordV);
                    glColor3f(1.f, 1.f, 1.f);
                }
            }
        }
        else
        {
            if (i != HiddenMesh)
            {
                RenderMesh(i, Flag, Alpha, iBlendMesh, BlendMeshLight, BlendMeshTexCoordU,
                           BlendMeshTexCoordV, Texture);
            }
        }
    }
    EndRender();
}

void BMD::PrepareRigidInstanceMeshes()
{
    rigidInstanceMeshes_.clear();
    batchRigidPlacements_ = true;
    bool hasAlpha = false;
    if (!renderTapeRigidGeometry_.vertices)
        return;
    if (sessionKeeper_.ModelPoolObject().Find(m_iBMDSeqID) == this)
        for (auto &block : sessionKeeper_.ObjectBlocks())
            for (const auto *object = block.Head; object; object = object->Next)
                if (object->Type == m_iBMDSeqID)
                {
                    block.DrawGroupsDirty = true;
                    break;
                }
    if (BoneHead >= 0 || StreamMesh >= 0)
        return;
    for (int index = 0; index < NumMeshs; ++index)
    {
        const auto &mesh = Meshs[index];
        if (mesh.NumTriangles == 0 || (mesh.m_csTScript && mesh.m_csTScript->getHiddenMesh()))
            continue;
        const int textureIndex = IndexTexture[mesh.Texture];
        if (textureIndex == BITMAP_HIDE)
            continue;
        const auto properties = sessionKeeper_.TextureNamespace().TryGetProperties(textureIndex);
        const auto texture = sessionKeeper_.TextureNamespace().Resolve(textureIndex);
        if (textureIndex == BITMAP_WATER || !properties || !texture ||
            (properties->components != 3 && properties->components != 4) || properties->isSkin ||
            properties->isHair ||
            (mesh.m_csTScript &&
             (mesh.m_csTScript->getBright() || mesh.m_csTScript->getStreamMesh() ||
              mesh.m_csTScript->getShadowMesh() != SHADOW_NONE)))
        {
            rigidInstanceMeshes_.clear();
            return;
        }
        rigidInstanceMeshes_.push_back(
            {mesh.RenderTapeVertexOffset,
             static_cast<std::uint32_t>(mesh.RenderTapeVertexSources->size()),
             mesh.RenderTapeIndexOffset, static_cast<std::uint32_t>(mesh.RenderTapeIndices->size()),
             *texture, properties->components == 4});
        hasAlpha |= properties->components == 4;
    }
    // Multiple alpha meshes must remain in each placement's authored order.
    batchRigidPlacements_ = !hasAlpha || rigidInstanceMeshes_.size() == 1;
}

void BMD::RenderRigidInstances(std::span<const RenderTapeRigidInstance> instances,
                               bool bakedGeometry)
{
    if (instances.empty() || !PrepareRenderTapeGeometry(bakedGeometry))
        return;
    if (!LightEnable)
        glColor3fv(BodyLight);
    DisableAlphaBlend();
    RenderTapeBmdConstants constants;
    constants.enabled = true;
    constants.rigid = true;
    constants.lighting = LightEnable;
    PrepareTransformLighting(constants.lightPosition.data());
    if (instances.size() == 1)
    {
        const auto &instance = instances.front();
        constants.rigidTransform = {instance.row0, instance.row1, instance.row2};
        std::copy_n(instance.bodyLight.begin(), 3, constants.bodyLight.begin());
        constants.alpha = instance.bodyLight[3];
        constants.baseColor = instance.baseColor;
        std::copy_n(instance.uvOffset.begin(), 2, constants.uvOffset.begin());
    }
    const auto &geometry = bakedGeometry ? renderTapeRigidGeometry_ : renderTapeGeometry_;
    bool alphaTest = false;
    for (const auto &mesh : rigidInstanceMeshes_)
    {
        if (mesh.alphaTest != alphaTest)
        {
            if (mesh.alphaTest)
                EnableAlphaTest();
            else
                DisableAlphaBlend();
            alphaTest = mesh.alphaTest;
        }
        LegacyRender().BindTexture(mesh.texture);
        if (instances.size() == 1)
            (void)LegacyRender().DrawBmdGeometry(geometry, mesh.vertexOffset, mesh.vertexCount,
                                                 mesh.indexOffset, mesh.indexCount, constants);
        else
            (void)LegacyRender().DrawRigidInstances(geometry, mesh.vertexOffset, mesh.vertexCount,
                                                    mesh.indexOffset, mesh.indexCount, instances,
                                                    constants);
    }
}

void BMD::RenderBodyAlternative(int iRndExtFlag, int iParam, int Flag, float Alpha, int BlendMesh,
                                float BlendMeshLight, float BlendMeshTexCoordU,
                                float BlendMeshTexCoordV, int HiddenMesh, int Texture)
{
    if (NumMeshs == 0)
        return;

    BeginRender(Alpha);
    if (!LightEnable)
    {
        if (Alpha >= 0.99f)
            glColor3fv(BodyLight);
        else
            glColor4f(BodyLight[0], BodyLight[1], BodyLight[2], Alpha);
    }
    for (int i = 0; i < NumMeshs; i++)
    {
        if (i != HiddenMesh)
        {
            RenderMeshAlternative(iRndExtFlag, iParam, i, Flag, Alpha, BlendMesh, BlendMeshLight,
                                  BlendMeshTexCoordU, BlendMeshTexCoordV, Texture);
        }
    }
    EndRender();
}

void BMD::RenderMeshTranslate(int i, int RenderFlag, float Alpha, int BlendMesh,
                              float BlendMeshLight, float BlendMeshTexCoordU,
                              float BlendMeshTexCoordV, int MeshTexture)
{
    if (i >= NumMeshs || i < 0)
        return;

    const Mesh_t *m = MeshForDraw(i);
    if (m->NumTriangles == 0)
        return;
    float Wave = (int)WorldTime % 10000 * 0.0001f;

    int Texture = IndexTexture[m->Texture];
    if (Texture == BITMAP_HIDE)
        return;
    if (Texture == BITMAP_SKIN)
    {
        if (HideSkin)
            return;
        Texture = BITMAP_SKIN + Skin;
    }
    else if (Texture == BITMAP_WATER)
    {
        Texture = BITMAP_WATER + WaterTextureNumber;
    }
    if (MeshTexture != -1)
        Texture = MeshTexture;

    const auto texture = Bitmaps.GetTextureProperties(Texture);
    if (!texture)
    {
        return;
    }
    EnsureCpuTransforms();

    bool EnableWave = false;
    int streamMesh = StreamMesh;
    if (m->m_csTScript != nullptr)
    {
        if (m->m_csTScript->getStreamMesh())
        {
            streamMesh = i;
        }
    }
    if ((i == BlendMesh || i == streamMesh) &&
        (BlendMeshTexCoordU != 0.f || BlendMeshTexCoordV != 0.f))
        EnableWave = true;

    bool EnableLight = LightEnable;
    if (i == StreamMesh)
    {
        //vec3_t Light;
        //Vector(1.f,1.f,1.f,Light);
        glColor3fv(BodyLight);
        EnableLight = false;
    }
    else if (EnableLight)
    {
        for (int j = 0; j < m->NumNormals; j++)
        {
            VectorScale(BodyLight, IntensityTransform[i][j], LightTransform[i][j]);
        }
    }

    int Render = RenderFlag;
    if ((RenderFlag & RENDER_COLOR) == RENDER_COLOR)
    {
        Render = RENDER_COLOR;
        if ((RenderFlag & RENDER_BRIGHT) == RENDER_BRIGHT)
            EnableAlphaBlend();
        else if ((RenderFlag & RENDER_DARK) == RENDER_DARK)
            EnableAlphaBlendMinus();
        else
            DisableAlphaBlend();
        DisableTexture();
        glColor3fv(BodyLight);
    }
    else if ((RenderFlag & RENDER_CHROME) == RENDER_CHROME ||
             (RenderFlag & RENDER_METAL) == RENDER_METAL ||
             (RenderFlag & RENDER_CHROME2) == RENDER_CHROME2 ||
             (RenderFlag & RENDER_CHROME6) == RENDER_CHROME6)
    {
        if (m->m_csTScript != nullptr)
        {
            if (m->m_csTScript->getNoneBlendMesh())
                return;
        }
        if (m->NoneBlendMesh)
            return;
        Render = RENDER_CHROME;

        float Wave2 = (int)WorldTime % 5000 * 0.00024f - 0.4f;

        for (int j = 0; j < m->NumNormals; j++)
        {
            //			Normal_t *np = &m->Normals[j];
            if (j > MAX_VERTICES)
                break;
            float *Normal = NormalTransform[i][j];

            if ((RenderFlag & RENDER_CHROME2) == RENDER_CHROME2)
            {
                g_chrome[j][0] = (Normal[2] + Normal[0]) * 0.8f + Wave2 * 2.f;
                g_chrome[j][1] = (Normal[1] + Normal[0]) * 1.0f + Wave2 * 3.f;
            }
            else if ((RenderFlag & RENDER_CHROME) == RENDER_CHROME)
            {
                g_chrome[j][0] = Normal[2] * 0.5f + Wave;
                g_chrome[j][1] = Normal[1] * 0.5f + Wave * 2.f;
            }
            else if ((RenderFlag & RENDER_CHROME6) == RENDER_CHROME6)
            {
                g_chrome[j][0] = (Normal[2] + Normal[0]) * 0.8f + Wave2 * 2.f;
                g_chrome[j][1] = (Normal[1] + Normal[0]) * 1.0f + Wave2 * 3.f;
            }
            else
            {
                g_chrome[j][0] = Normal[2] * 0.5f + 0.2f;
                g_chrome[j][1] = Normal[1] * 0.5f + 0.5f;
            }
        }

        if ((RenderFlag & RENDER_BRIGHT) == RENDER_BRIGHT)
            EnableAlphaBlend();
        else if ((RenderFlag & RENDER_DARK) == RENDER_DARK)
            EnableAlphaBlendMinus();
        else if ((RenderFlag & RENDER_LIGHTMAP) == RENDER_LIGHTMAP)
            EnableLightMap();
        else
            DisableAlphaBlend();

        if ((RenderFlag & RENDER_CHROME2) == RENDER_CHROME2 && MeshTexture == -1)
        {
            BindTexture(BITMAP_CHROME2);
        }
        else if ((RenderFlag & RENDER_CHROME) == RENDER_CHROME && MeshTexture == -1)
            BindTexture(BITMAP_CHROME);
        else if ((RenderFlag & RENDER_METAL) == RENDER_METAL && MeshTexture == -1)
            BindTexture(BITMAP_SHINY);
        else
            BindTexture(Texture);
    }
    else if (BlendMesh <= -2 || m->Texture == BlendMesh)
    {
        Render = RENDER_TEXTURE;
        BindTexture(Texture);
        if ((RenderFlag & RENDER_DARK) == RENDER_DARK)
            EnableAlphaBlendMinus();
        else
            EnableAlphaBlend();
        glColor3f(BodyLight[0] * BlendMeshLight, BodyLight[1] * BlendMeshLight,
                  BodyLight[2] * BlendMeshLight);
        //glColor3f(BlendMeshLight,BlendMeshLight,BlendMeshLight);
        EnableLight = false;
    }
    else if ((RenderFlag & RENDER_TEXTURE) == RENDER_TEXTURE)
    {
        Render = RENDER_TEXTURE;
        BindTexture(Texture);
        if ((RenderFlag & RENDER_BRIGHT) == RENDER_BRIGHT)
        {
            EnableAlphaBlend();
        }
        else if ((RenderFlag & RENDER_DARK) == RENDER_DARK)
        {
            EnableAlphaBlendMinus();
        }
        else if (Alpha < 0.99f || texture->components == 4)
        {
            EnableAlphaTest();
        }
        else
        {
            DisableAlphaBlend();
        }
    }
    else if ((RenderFlag & RENDER_BRIGHT) == RENDER_BRIGHT)
    {
        if (texture->components == 4 || m->Texture == BlendMesh)
        {
            return;
        }
        Render = RENDER_BRIGHT;
        EnableAlphaBlend();
        DisableTexture();
        DisableDepthMask();
    }
    else
    {
        Render = RENDER_TEXTURE;
    }

    glBegin(GL_TRIANGLES);
    for (int j = 0; j < m->NumTriangles; j++)
    {
        vec3_t pos;
        Triangle_t *tp = &m->Triangles[j];
        for (int k = 0; k < tp->Polygon; k++)
        {
            int vi = tp->VertexIndex[k];
            switch (Render)
            {
            case RENDER_TEXTURE: {
                TexCoord_t *texp = &m->TexCoords[tp->TexCoordIndex[k]];
                if (EnableWave)
                    glTexCoord2f(texp->TexCoordU + BlendMeshTexCoordU,
                                 texp->TexCoordV + BlendMeshTexCoordV);
                else
                    glTexCoord2f(texp->TexCoordU, texp->TexCoordV);
                if (EnableLight)
                {
                    int ni = tp->NormalIndex[k];
                    if (Alpha >= 0.99f)
                    {
                        glColor3fv(LightTransform[i][ni]);
                    }
                    else
                    {
                        float *Light = LightTransform[i][ni];
                        glColor4f(Light[0], Light[1], Light[2], Alpha);
                    }
                }
                break;
            }
            case RENDER_CHROME: {
                if (Alpha >= 0.99f)
                    glColor3fv(BodyLight);
                else
                    glColor4f(BodyLight[0], BodyLight[1], BodyLight[2], Alpha);
                int ni = tp->NormalIndex[k];
                glTexCoord2f(g_chrome[ni][0], g_chrome[ni][1]);
                break;
            }
            }
            {
                VectorAdd(VertexTransform[i][vi], BodyOrigin, pos);
                glVertex3fv(pos);
            }
        }
    }
    glEnd();
}

void BMD::RenderBodyTranslate(int Flag, float Alpha, int BlendMesh, float BlendMeshLight,
                              float BlendMeshTexCoordU, float BlendMeshTexCoordV, int HiddenMesh,
                              int Texture)
{
    if (NumMeshs == 0)
        return;

    BeginRender(Alpha);
    if (!LightEnable)
    {
        if (Alpha >= 0.99f)
            glColor3fv(BodyLight);
        else
            glColor4f(BodyLight[0], BodyLight[1], BodyLight[2], Alpha);
    }
    for (int i = 0; i < NumMeshs; i++)
    {
        if (i != HiddenMesh)
        {
            RenderMeshTranslate(i, Flag, Alpha, BlendMesh, BlendMeshLight, BlendMeshTexCoordU,
                                BlendMeshTexCoordV, Texture);
        }
    }
    EndRender();
}

void SessionRenderUnit::CalcShadowPosition(vec3_t *position, const vec3_t origin, const float sx,
                                           const float sy)
{
    vec3_t result;
    VectorCopy(*position, result);

    // Subtract the origin (position of the character) from the current position of the vertex
    // The result is the relative coordinate of the vertex to the origin.
    VectorSubtract(result, origin, result)

        // scale the shadow in the x direction
        result[0] += result[2] * (result[0] + sx) / (result[2] - sy);

    // Add the origin again, to get the absolute coordinate of the vertex again
    VectorAdd(result, origin, result);

    // put it on the ground by adding 5 to the actual ground coordinate.
    result[2] = RequestTerrainHeight(result[0], result[1]) + 5.f;

    // copy to result
    VectorCopy(result, *position);
}

void SessionRenderUnit::GetClothShadowPosition(vec3_t *target, const CPhysicsCloth *pCloth,
                                               const int index, const vec3_t origin, const float sx,
                                               const float sy)
{
    pCloth->GetPosition(index, target);
    CalcShadowPosition(target, origin, sx, sy);
}

namespace ModelGeometryDetail
{
void AppendClothShadowGrid(RenderTapeVertex *&target, const std::array<float, 3> *positions,
                           int rows, int columns, const std::array<float, 2> &uv,
                           const std::array<float, 4> &color, const std::array<float, 3> &normal)
{
    for (int col = 0; col < columns - 1; ++col)
        for (int row = 0; row < rows - 1; ++row)
        {
            const int a = rows * col + row;
            const int b = rows * (col + 1) + row;
            for (const int index : {a, b, a + 1, b + 1, b, a + 1})
            {
                const auto &position = positions[index];
                *target++ = {{position[0], position[1], position[2], 1.f}, uv, color, normal};
            }
        }
}
} // namespace ModelGeometryDetail

void BMD::AddClothesShadowTriangles(const CharacterClothVisual &clothes, const float sx,
                                    const float sy) const
{
    auto &renderer = *sessionKeeper_.Renderer();
    auto &positions = renderer.clothShadowVertices_;
    // Cape slots include optional and hidden pieces. Use the same prepared set as color drawing.
    const bool cape = clothes.kind == CharacterClothVisual::Kind::Cape;
    const auto count = cape ? clothes.visiblePieces.size() : clothes.count;
    const auto piece = [&](std::size_t i) -> const CPhysicsCloth & {
        return clothes.pieces[cape ? clothes.visiblePieces[i] : i];
    };
    std::size_t vertexCount = 0, largestGrid = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto rows = static_cast<std::size_t>(piece(i).GetHorizontalCount());
        const auto columns = static_cast<std::size_t>(piece(i).GetVerticalCount());
        vertexCount += (rows - 1) * (columns - 1) * 6;
        largestGrid = std::max(largestGrid, rows * columns);
    }
    if (vertexCount == 0)
        return;
    if (positions.size() < largestGrid)
        positions.resize(largestGrid);

    auto &facade = renderer.LegacyRender();
    const auto uv = facade.CurrentTextureCoordinate();
    const auto color = facade.CurrentColor();
    const auto normal = facade.CurrentNormal();
    glEnableClientState(GL_VERTEX_ARRAY);
    (void)facade.WriteTriangles(vertexCount, [&](std::span<RenderTapeVertex> vertices) noexcept {
        auto *target = vertices.data();
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto &cloth = piece(i);
            for (int vertex = 0; vertex < cloth.GetVerticesCount(); ++vertex)
                renderer.GetClothShadowPosition(reinterpret_cast<vec3_t *>(&positions[vertex]),
                                                &cloth, vertex, BodyOrigin, sx, sy);
            ModelGeometryDetail::AppendClothShadowGrid(target, positions.data(),
                                                       cloth.GetHorizontalCount(),
                                                       cloth.GetVerticalCount(), uv, color, normal);
        }
    });
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

RenderTapeBmdConstants BMD::ShadowConstants(float sx, float sy,
                                            LogicalGeometryAssetRef terrain) const
{
    RenderTapeBmdConstants bmd = renderTapeTransform_;
    bmd.shadow = true;
    bmd.baseColor = LegacyRender().CurrentColor();
    bmd.shadowScaleX = sx;
    bmd.shadowScaleY = sy;
    bmd.terrainSpecialHeight = g_fSpecialHeight;
    bmd.shadowTerrainGeometry = terrain;
    // Shadow draws do not use lighting. Their light-position uniform carries
    // the current projection origin; the cached mesh origin stays unchanged.
    std::copy_n(BodyOrigin, 3, bmd.lightPosition.begin());
    return bmd;
}

void BMD::AddMeshShadowTriangles(const int blendMesh, const int hiddenMesh, const int startMesh,
                                 const int endMesh, const float sx, const float sy)
{
    SessionRenderUnit *const renderer = sessionKeeper_.Renderer();
    const LogicalGeometryAssetLease &terrain = renderer->terrainGeometryCache_.Lease();
    if (!drawMesh_ && renderTapePaletteReady_ && terrain.terrainCells != nullptr &&
        !LegacyRender().UsesPolygonLineMode())
    {
        const auto bmd = ShadowConstants(sx, sy, terrain.asset);
        for (int meshIndex = startMesh; meshIndex < endMesh; ++meshIndex)
        {
            const Mesh_t &mesh = Meshs[meshIndex];
            if (meshIndex == hiddenMesh || mesh.NumTriangles <= 0 || mesh.Texture == blendMesh)
            {
                continue;
            }
            (void)LegacyRender().DrawBmdShadowGeometry(
                GeometryForDraw(), terrain, mesh.RenderTapeVertexOffset,
                static_cast<std::uint32_t>(mesh.RenderTapeVertexSources->size()),
                mesh.RenderTapeIndexOffset,
                static_cast<std::uint32_t>(mesh.RenderTapeIndices->size()), bmd);
        }
        return;
    }
    EnsureCpuTransforms();

    std::size_t uniqueVertexCount = 0;
    std::size_t indexCount = 0;
    for (int i = startMesh; i < endMesh; i++)
    {
        if (i == hiddenMesh)
        {
            continue;
        }

        const Mesh_t *mesh = MeshForDraw(i);
        if (mesh->NumTriangles <= 0 || mesh->Texture == blendMesh)
        {
            continue;
        }
        uniqueVertexCount += static_cast<std::size_t>(mesh->NumVertices);
        for (int j = 0; j < mesh->NumTriangles; j++)
        {
            indexCount += static_cast<std::size_t>(mesh->Triangles[j].Polygon);
        }
    }
    if (uniqueVertexCount == 0 || indexCount == 0)
    {
        return;
    }

    glEnableClientState(GL_VERTEX_ARRAY);
    const std::array<float, 4> &color = LegacyRender().CurrentColor();
    const std::array<float, 3> &normal = LegacyRender().CurrentNormal();
    const std::array<float, 2> &textureCoordinate = LegacyRender().CurrentTextureCoordinate();
    if (!LegacyRender().UsesPolygonLineMode())
    {
        (void)LegacyRender().WriteIndexedTriangles(
            uniqueVertexCount, indexCount,
            [&](std::span<RenderTapeVertex> vertices, std::span<std::uint32_t> indices) noexcept {
                RenderTapeVertex *target = vertices.data();
                std::uint32_t *outputIndex = indices.data();
                std::uint32_t baseVertex = 0;
                for (int i = startMesh; i < endMesh; ++i)
                {
                    if (i == hiddenMesh)
                    {
                        continue;
                    }
                    const Mesh_t &mesh = Meshs[i];
                    if (mesh.NumTriangles <= 0 || mesh.Texture == blendMesh)
                    {
                        continue;
                    }
                    for (int sourceVertexIndex = 0; sourceVertexIndex < mesh.NumVertices;
                         ++sourceVertexIndex)
                    {
                        vec3_t shadowPosition;
                        VectorCopy(VertexTransform[i][sourceVertexIndex], shadowPosition);
                        CalcShadowPosition(&shadowPosition, BodyOrigin, sx, sy);
                        *target++ = {
                            {shadowPosition[0], shadowPosition[1], shadowPosition[2], 1.0F},
                            textureCoordinate,
                            color,
                            normal};
                    }
                    for (int j = 0; j < mesh.NumTriangles; ++j)
                    {
                        const Triangle_t &triangle = mesh.Triangles[j];
                        for (int k = 0; k < triangle.Polygon; ++k)
                        {
                            *outputIndex++ =
                                baseVertex + static_cast<std::uint32_t>(triangle.VertexIndex[k]);
                        }
                    }
                    baseVertex += static_cast<std::uint32_t>(mesh.NumVertices);
                }
            });
    }
    else
    {
        (void)LegacyRender().WriteTriangles(
            indexCount, [&](std::span<RenderTapeVertex> vertices) noexcept {
                RenderTapeVertex *target = vertices.data();
                for (int i = startMesh; i < endMesh; ++i)
                {
                    if (i == hiddenMesh)
                    {
                        continue;
                    }
                    const Mesh_t &mesh = Meshs[i];
                    if (mesh.NumTriangles <= 0 || mesh.Texture == blendMesh)
                    {
                        continue;
                    }
                    for (int sourceVertexIndex = 0; sourceVertexIndex < mesh.NumVertices;
                         ++sourceVertexIndex)
                    {
                        VectorCopy(VertexTransform[i][sourceVertexIndex],
                                   RenderArrayVertices[sourceVertexIndex]);
                        CalcShadowPosition(&RenderArrayVertices[sourceVertexIndex], BodyOrigin, sx,
                                           sy);
                    }
                    for (int j = 0; j < mesh.NumTriangles; ++j)
                    {
                        const Triangle_t &triangle = mesh.Triangles[j];
                        for (int k = 0; k < triangle.Polygon; ++k)
                        {
                            const float *shadowPosition =
                                RenderArrayVertices[triangle.VertexIndex[k]];
                            *target++ = {
                                {shadowPosition[0], shadowPosition[1], shadowPosition[2], 1.0F},
                                textureCoordinate,
                                color,
                                normal};
                        }
                    }
                }
            });
    }
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

void BMD::RenderBodyShadow(const int blendMesh, const int hiddenMesh, const int startMeshNumber,
                           const int endMeshNumber, const CharacterClothVisual *clothes)
{
    if (!g_pOption->GetRenderAllEffects())
    {
        return;
    }

    if (NumMeshs == 0 && clothes == nullptr)
    {
        return;
    }

    EnableAlphaTest(false);

    glColor4f(0.0f, 0.0f, 0.0f, 0.5f); // 50% opacity for shadows

    DisableTexture();
    DisableDepthMask();
    BeginRender(1.f);

    // enable stencil and continue draw
    glEnable(GL_STENCIL_TEST);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

    int startMesh = 0;
    int endMesh = NumMeshs;

    if (startMeshNumber != -1)
    {
        startMesh = startMeshNumber;
    }

    if (endMeshNumber != -1)
    {
        endMesh = endMeshNumber;
    }

    const float sx = gMapManager.InBattleCastle() ? 2500.f : 2000.f;
    const float sy = 4000.f;

    if (clothes == nullptr)
    {
        AddMeshShadowTriangles(blendMesh, hiddenMesh, startMesh, endMesh, sx, sy);
    }
    else
    {
        AddClothesShadowTriangles(*clothes, sx, sy);
    }

    EndRender();
    EnableDepthMask();

    glDisable(GL_STENCIL_TEST);
}

void BMD::RenderObjectBoundingBox()
{
    DisableTexture();
    glPushMatrix();
    glTranslatef(BodyOrigin[0], BodyOrigin[1], BodyOrigin[2]);
    glScalef(BodyScale, BodyScale, BodyScale);
    for (int i = 0; i < NumBones; i++)
    {
        Bone_t *b = &Bones[i];
        if (b->BoundingBox)
        {
            vec3_t BoundingVertices[8];
            for (int j = 0; j < 8; j++)
            {
                VectorTransform(b->BoundingVertices[j], BoneTransform[i], BoundingVertices[j]);
            }

            glBegin(GL_QUADS);
            glColor3f(0.2f, 0.2f, 0.2f);
            glTexCoord2f(1.0F, 1.0F);
            glVertex3fv(BoundingVertices[7]);
            glTexCoord2f(1.0F, 0.0F);
            glVertex3fv(BoundingVertices[6]);
            glTexCoord2f(0.0F, 0.0F);
            glVertex3fv(BoundingVertices[4]);
            glTexCoord2f(0.0F, 1.0F);
            glVertex3fv(BoundingVertices[5]);

            glColor3f(0.2f, 0.2f, 0.2f);
            glTexCoord2f(0.0F, 1.0F);
            glVertex3fv(BoundingVertices[0]);
            glTexCoord2f(1.0F, 1.0F);
            glVertex3fv(BoundingVertices[2]);
            glTexCoord2f(1.0F, 0.0F);
            glVertex3fv(BoundingVertices[3]);
            glTexCoord2f(0.0F, 0.0F);
            glVertex3fv(BoundingVertices[1]);

            glColor3f(0.6f, 0.6f, 0.6f);
            glTexCoord2f(1.0F, 1.0F);
            glVertex3fv(BoundingVertices[7]);
            glTexCoord2f(1.0F, 0.0F);
            glVertex3fv(BoundingVertices[3]);
            glTexCoord2f(0.0F, 0.0F);
            glVertex3fv(BoundingVertices[2]);
            glTexCoord2f(0.0F, 1.0F);
            glVertex3fv(BoundingVertices[6]);

            glColor3f(0.6f, 0.6f, 0.6f);
            glTexCoord2f(0.0F, 1.0F);
            glVertex3fv(BoundingVertices[0]);
            glTexCoord2f(1.0F, 1.0F);
            glVertex3fv(BoundingVertices[1]);
            glTexCoord2f(1.0F, 0.0F);
            glVertex3fv(BoundingVertices[5]);
            glTexCoord2f(0.0F, 0.0F);
            glVertex3fv(BoundingVertices[4]);

            glColor3f(0.4f, 0.4f, 0.4f);
            glTexCoord2f(1.0F, 1.0F);
            glVertex3fv(BoundingVertices[7]);
            glTexCoord2f(1.0F, 0.0F);
            glVertex3fv(BoundingVertices[5]);
            glTexCoord2f(0.0F, 0.0F);
            glVertex3fv(BoundingVertices[1]);
            glTexCoord2f(0.0F, 1.0F);
            glVertex3fv(BoundingVertices[3]);

            glColor3f(0.4f, 0.4f, 0.4f);
            glTexCoord2f(0.0F, 1.0F);
            glVertex3fv(BoundingVertices[0]);
            glTexCoord2f(1.0F, 1.0F);
            glVertex3fv(BoundingVertices[4]);
            glTexCoord2f(1.0F, 0.0F);
            glVertex3fv(BoundingVertices[6]);
            glTexCoord2f(0.0F, 0.0F);
            glVertex3fv(BoundingVertices[2]);
            glEnd();
        }
    }
    glPopMatrix();
    DisableAlphaBlend();
}

void BMD::RenderBone(float (*BoneMatrix)[3][4])
{
    DisableTexture();
    glDepthFunc(GL_ALWAYS);
    glColor3f(0.8f, 0.8f, 0.2f);
    for (int i = 0; i < NumBones; i++)
    {
        Bone_t *b = &Bones[i];
        if (!b->Dummy)
        {
            BoneMatrix_t *bm = &b->BoneMatrixes[CurrentAction];
            int Parent = b->Parent;
            if (Parent > 0)
            {
                float Scale = 1.f;
                float dx = bm->Position[CurrentAnimationFrame][0];
                float dy = bm->Position[CurrentAnimationFrame][1];
                float dz = bm->Position[CurrentAnimationFrame][2];
                Scale = sqrtf(dx * dx + dy * dy + dz * dz) * 0.05f;
                vec3_t Position[3];
                Vector(0.f, 0.f, -Scale, Position[0]);
                Vector(0.f, 0.f, Scale, Position[1]);
                Vector(0.f, 0.f, 0.f, Position[2]);
                vec3_t BoneVertices[3];
                VectorTransform(Position[0], BoneMatrix[Parent], BoneVertices[0]);
                VectorTransform(Position[1], BoneMatrix[Parent], BoneVertices[1]);
                VectorTransform(Position[2], BoneMatrix[i], BoneVertices[2]);
                for (auto &BoneVertice : BoneVertices)
                {
                    VectorMA(BodyOrigin, BodyScale, BoneVertice, BoneVertice);
                }
                glBegin(GL_LINES);
                glVertex3fv(BoneVertices[0]);
                glVertex3fv(BoneVertices[1]);
                glVertex3fv(BoneVertices[1]);
                glVertex3fv(BoneVertices[2]);
                glVertex3fv(BoneVertices[2]);
                glVertex3fv(BoneVertices[0]);
                glEnd();
            }
        }
    }
    glDepthFunc(GL_LEQUAL);
}

void BMD::FindNearTriangle()
{
    for (int iMesh = 0; iMesh < NumMeshs; iMesh++)
    {
        Mesh_t *m = &Meshs[iMesh];

        Triangle_t *pTriangle = m->Triangles;
        int iNumTriangles = m->NumTriangles;
        for (int iTri = 0; iTri < iNumTriangles; ++iTri)
        {
            for (int i = 0; i < 3; ++i)
            {
                pTriangle[iTri].EdgeTriangleIndex[i] = -1;
            }
        }
        for (int iTri = 0; iTri < iNumTriangles; ++iTri)
        {
            FindTriangleForEdge(iMesh, iTri, 0);
            FindTriangleForEdge(iMesh, iTri, 1);
            FindTriangleForEdge(iMesh, iTri, 2);
        }
    }
}

void BMD::FindTriangleForEdge(int iMesh, int iTri1, int iIndex11)
{
    if (iMesh >= NumMeshs || iMesh < 0)
        return;

    Mesh_t *m = &Meshs[iMesh];
    Triangle_t *pTriangle = m->Triangles;

    Triangle_t *pTri1 = &pTriangle[iTri1];
    if (pTri1->EdgeTriangleIndex[iIndex11] != -1)
    {
        return;
    }

    int iNumTriangles = m->NumTriangles;
    for (int iTri2 = 0; iTri2 < iNumTriangles; ++iTri2)
    {
        if (iTri1 == iTri2)
        {
            continue;
        }

        Triangle_t *pTri2 = &pTriangle[iTri2];
        int iIndex12 = (iIndex11 + 1) % 3;
        for (int iIndex21 = 0; iIndex21 < 3; ++iIndex21)
        {
            int iIndex22 = (iIndex21 + 1) % 3;
            if (pTri2->EdgeTriangleIndex[iIndex21] == -1 &&
                pTri1->VertexIndex[iIndex11] == pTri2->VertexIndex[iIndex22] &&
                pTri1->VertexIndex[iIndex12] == pTri2->VertexIndex[iIndex21])
            {
                pTri1->EdgeTriangleIndex[iIndex11] = iTri2;
                pTri2->EdgeTriangleIndex[iIndex21] = iTri1;
                return;
            }
        }
    }
}
//#endif //USE_SHADOWVOLUME

void BMD::BakeRigidGeometry(BmdSharedAsset &asset)
{
    if (asset.invariantLocalPose.empty() || !asset.vertices)
        return;
    auto bones = std::make_unique<vec34_t[]>(NumBones);
    const vec3_t angle{};
    BodyHeight = 0.f;
    Animation(bones.get(), 0.f, 0.f, 0, angle, angle, false, false, nullptr, 0);
    auto vertices = std::make_shared<std::vector<RenderTapeVertex>>(*asset.vertices);
    for (auto &vertex : *vertices)
    {
        vec3_t position, normal;
        VectorTransform(vertex.position.data(), bones[vertex.bmdSource[0]], position);
        VectorRotate(vertex.normal.data(), bones[vertex.bmdSource[1]], normal);
        std::copy_n(position, 3, vertex.position.begin());
        std::copy_n(normal, 3, vertex.normal.begin());
    }
    asset.rigidVertices = std::move(vertices);
}

void BMD::CreateBoundingBox()
{
    for (int i = 0; i < NumBones; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            BoundingMin[i][j] = 9999.0;
            BoundingMax[i][j] = -9999.0;
        }
        BoundingVertices[i] = 0;
    }

    for (int i = 0; i < NumMeshs; i++)
    {
        Mesh_t *m = &Meshs[i];
        for (int j = 0; j < m->NumVertices; j++)
        {
            Vertex_t *v = &m->Vertices[j];
            for (int k = 0; k < 3; k++)
            {
                if (v->Position[k] < BoundingMin[v->Node][k])
                    BoundingMin[v->Node][k] = v->Position[k];
                if (v->Position[k] > BoundingMax[v->Node][k])
                    BoundingMax[v->Node][k] = v->Position[k];
            }
            BoundingVertices[v->Node]++;
        }
    }
    for (int i = 0; i < NumBones; i++)
    {
        Bone_t *b = &Bones[i];
        if (BoundingVertices[i])
            b->BoundingBox = true;
        else
            b->BoundingBox = false;
        Vector(BoundingMax[i][0], BoundingMax[i][1], BoundingMax[i][2], b->BoundingVertices[0]);
        Vector(BoundingMax[i][0], BoundingMax[i][1], BoundingMin[i][2], b->BoundingVertices[1]);
        Vector(BoundingMax[i][0], BoundingMin[i][1], BoundingMax[i][2], b->BoundingVertices[2]);
        Vector(BoundingMax[i][0], BoundingMin[i][1], BoundingMin[i][2], b->BoundingVertices[3]);
        Vector(BoundingMin[i][0], BoundingMax[i][1], BoundingMax[i][2], b->BoundingVertices[4]);
        Vector(BoundingMin[i][0], BoundingMax[i][1], BoundingMin[i][2], b->BoundingVertices[5]);
        Vector(BoundingMin[i][0], BoundingMin[i][1], BoundingMax[i][2], b->BoundingVertices[6]);
        Vector(BoundingMin[i][0], BoundingMin[i][1], BoundingMin[i][2], b->BoundingVertices[7]);
    }
}

BMD::~BMD()
{
    Release();
}

void BMD::InterpolationTrans(float (*Mat1)[4], float (*TransMat2)[4], float _Scale)
{
    TransMat2[0][3] = TransMat2[0][3] - (TransMat2[0][3] - Mat1[0][3]) * (1 - _Scale);
    TransMat2[1][3] = TransMat2[1][3] - (TransMat2[1][3] - Mat1[1][3]) * (1 - _Scale);
    TransMat2[2][3] = TransMat2[2][3] - (TransMat2[2][3] - Mat1[2][3]) * (1 - _Scale);
}

const vec34_t *SessionRenderUnit::PrepareDrawPose(const ObjectDrawInput &draw, bool translate,
                                                  float animationFrame)
{
    auto &model = Models[draw.type];
    const vec34_t *pose = draw.bones;
    if (!WorldObjectDetail::MatchesPreparedDrawPose(draw, model, translate, animationFrame))
    {
        auto *matrices = drawPoses_.Allocate(model.NumBones);
        model.Animation(matrices, animationFrame, draw.priorAnimationFrame, draw.priorAction,
                        draw.angle, draw.headAngle, false, !translate, nullptr, draw.action);
        pose = matrices;
    }
    if (!draw.source->EnableBoneMatrix && model.NumBones > 0)
    {
        // Non-character callers still consume their session's immediate scratch.
        // Lazy model transforms borrow the retained pose above instead.
        memcpy(BoneTransform, pose, model.NumBones * sizeof(vec34_t));
    }
    return pose;
}

bool SessionRenderUnit::Calc_RenderObject(ObjectDrawInput &draw, bool Translate, int Select,
                                          int ExtraMon)
{
    const auto *o = draw.source;
    OBB_t bounds{};
    if (Translate || Select != 0 || ExtraMon != 0)
        draw.rigidPose = nullptr;
    if (draw.alpha < 0.01f)
    {
        return false;
    }

    BMD *b = &Models[draw.type];
    b->BodyHeight = 0.f;
    b->ContrastEnable = o->ContrastEnable;
    BodyLight(draw, b);
    b->BodyScale = draw.scale;
    b->CurrentAction = draw.action;
    VectorCopy(draw.position, b->BodyOrigin);

    if (draw.type == MODEL_CASTLE_GATE)
    {
        vec3_t Position;
        VectorCopy(draw.position, Position);

        Position[1] += 60.f;
        VectorCopy(Position, b->BodyOrigin);
    }
    else if (draw.type == MODEL_STATUE_OF_SAINT)
    {
        vec3_t Position;
        VectorCopy(draw.position, Position);

        Position[1] += 120.f;
        VectorCopy(Position, b->BodyOrigin);
    }

    float animationFrame = draw.animationFrame;
    if (draw.owner != NULL)
    {
        if (g_isCharacterBuff(draw.owner, eDeBuff_Stun) ||
            g_isCharacterBuff(draw.owner, eDeBuff_Sleep))
        {
            animationFrame = 0.f;
        }
    }

    draw.bones = draw.rigidPose ? reinterpret_cast<const vec34_t *>(draw.rigidPose->bones.data())
                                : PrepareDrawPose(draw, Translate, animationFrame);
    draw.stableBones = true;

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
        b->LightEnable = false;

        if (gMapManager.InChaosCastle() == true || o->Kind != KIND_NPC)
        {
            Vector(0.1f, 0.01f, 0.f, b->BodyLight);
            if (draw.type == MODEL_BALI)
            {
                BoneScale = 1.2f;
            }
            else
            {
                BoneScale = 1.f + (0.1f / draw.scale);
            }
            if (o->m_fEdgeScale != 1.2f)
            {
                BoneScale = o->m_fEdgeScale;
            }
        }
        else
        {
            Vector(0.02f, 0.1f, 0.f, b->BodyLight);
            BoneScale = 1.2f;
            BoneScale = o->m_fEdgeScale;
        }
        float Scale = BoneScale;
        RenderPartObjectEdge(b, draw, RENDER_BRIGHT, Translate, Scale);

        if (gMapManager.InChaosCastle() == true || o->Kind != KIND_NPC)
        {
            Vector(0.7f, 0.07f, 0.f, b->BodyLight);
            if (draw.type == MODEL_BALI)
            {
                BoneScale = 1.08f;
            }
            else
            {
                BoneScale = 1.f + (0.04f / draw.scale);
            }
            if (o->m_fEdgeScale != 1.2f)
            {
                BoneScale = maxf(o->m_fEdgeScale - 0.04f, 1.01f);
            }
        }
        else
        {
            Vector(0.16f, 0.7f, 0.f, b->BodyLight);
            BoneScale = 1.08f;
            BoneScale = maxf(o->m_fEdgeScale - 0.12f, 1.01f);
        }

        Scale = BoneScale;
        RenderPartObjectEdge(b, draw, RENDER_BRIGHT, Translate, Scale);
        BodyLight(draw, b);
        BoneScale = 1.f;
    }

    b->Transform(draw.bones, o->BoundingBoxMin, o->BoundingBoxMax, &bounds, Translate, 0.f,
                 draw.stableBones, draw.rigidPose);

    return true;
}

bool SessionRenderUnit::Calc_ObjectAnimation(ObjectDrawInput &draw, bool Translate, int Select)
{
    const auto *o = draw.source;
    if (draw.alpha < 0.01f)
        return false;

    BMD *b = &Models[draw.type];
    b->BodyHeight = 0.f;
    b->ContrastEnable = o->ContrastEnable;
    BodyLight(draw, b);
    b->BodyScale = draw.scale;
    b->CurrentAction = draw.action;
    VectorCopy(draw.position, b->BodyOrigin);

    draw.bones = PrepareDrawPose(draw, Translate, draw.animationFrame);
    draw.stableBones = true;

    return true;
}

void SessionRenderUnit::Draw_RenderObject(const ObjectDrawInput &input, bool Translate, int Select,
                                          int ExtraMon)
{
    auto draw = input;
    const auto *o = draw.source;
    BMD *b = &Models[draw.type];
    bool View = true;

    if ((EditFlag != EDIT_NONE) || (EditFlag == EDIT_NONE && draw.hiddenMesh != -2))
    {
        if (ExtraMon == 10)
        {
            float Alpha = 0.5f;

            const auto *owner = boneManager_.FindCharacter(draw.owner);
            if (owner && owner != Hero && IsBattleCastleStart() &&
                g_isCharacterBuff(draw.owner, eBuff_Cloaking))
            {
                const auto team = [](int mark) {
                    switch (mark)
                    {
                    case PARTS_ATTACK_KING_TEAM_MARK:
                    case PARTS_ATTACK_TEAM_MARK:
                        return 1;
                    case PARTS_ATTACK_KING_TEAM_MARK2:
                    case PARTS_ATTACK_TEAM_MARK2:
                        return 2;
                    case PARTS_ATTACK_KING_TEAM_MARK3:
                    case PARTS_ATTACK_TEAM_MARK3:
                        return 3;
                    case PARTS_DEFENSE_KING_TEAM_MARK:
                    case PARTS_DEFENSE_TEAM_MARK:
                        return 4;
                    default:
                        return 0;
                    }
                };
                const int viewerTeam = team(Hero->EtcPart);
                View = viewerTeam == 0 || team(owner->EtcPart) == viewerTeam;
            }

            if (View == true)
            {
                Vector(1.f, 1.f, 1.f, b->BodyLight);
                for (int i = 0; i < Models[draw.type].NumMeshs; i++)
                    b->RenderMesh(i, RENDER_BRIGHT | RENDER_CHROME5, Alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU * 8.f, draw.blendV * 2.f,
                                  BITMAP_CHROME2);
            }

            return;
        }
        TheMapProcess().PrepareObjectLight(draw, *b);
        if (ExtraMon && draw.type == MODEL_BALROG)
        {
            Vector(0.0f, 0.0f, 1.0f, b->BodyLight);
        }

        if (o->RenderType == RENDER_DARK)
        {
            b->RenderBody(RENDER_TEXTURE | RENDER_DARK, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV, draw.hiddenMesh);
        }
        else if (draw.type == MODEL_CHANGE_UP_EFF)
        {
            if (o->SubType == 0 || o->SubType == 1)
            {
                Vector(0.1f, 0.4f, 0.6f, b->BodyLight);
            }
            else
            {
                Vector(0.1f, 0.2f, 0.9f, b->BodyLight);
            }
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV);
        }
        else if (draw.type == MODEL_CHANGE_UP_NASA)
        {
            Vector(0.5f, 0.5f, 0.9f, b->BodyLight);
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV);
        }
        else if (draw.type == MODEL_CHANGE_UP_CYLINDER)
        {
            Vector(0.4f, 0.5f, 1.f, b->BodyLight);
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV);
        }
        else if (draw.type == MODEL_SUMMON)
        {
            //			Vector(0.4f,0.5f,1.f,b->BodyLight);
            //			b->RenderBody(RENDER_TEXTURE,draw.alpha,draw.blendMesh,draw.blendLight,draw.blendU,draw.blendV);
            if (!IsInKanturu3rd())
            {
                VectorCopy(draw.light, b->BodyLight)
                    b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV);
                b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV);
                b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV);
                Vector(1.f, 1.f, 1.f, b->BodyLight);
            }
        }
        else if (draw.type == MODEL_DEASULER)
        {
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            Vector(0.3f, 0.4f, 1.0f, b->BodyLight);
            b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
        }
        else if (draw.type == MODEL_FRED)
        {
            if (drawingCharacter_)
                RenderCharacterCloth(*drawingCharacter_);
            Vector(1.f, 1.f, 1.f, b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);

            if (draw.action != MONSTER01_ATTACK2 || draw.animationFrame < 2.5f)
            {
                b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                Vector(0.4f, 0.5f, 1.0f, b->BodyLight);
                b->RenderMesh(4, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV);
            }
        }
        else if (draw.type == MODEL_BANSHEE)
        {
            float fLumi = (sinf(WorldTime * 0.0015f) + 1.f) * 0.35f;

            Vector(1.f, 1.f, 1.f, b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);
            b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV);

            Vector(fLumi, fLumi, fLumi, b->BodyLight);
            b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, BITMAP_ASSASSIN_EFFECT1);
        }
        else if (draw.type == MODEL_RAKLION_BOSS_CRACKEFFECT)
        {
            float fLumi = draw.alpha;
            Vector(draw.light[0] * fLumi, draw.light[1] * fLumi, draw.light[2] * fLumi,
                   b->BodyLight);
            b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
        }
        else if (draw.type == MODEL_RAKLION_BOSS_MAGIC)
        {
            float fLumi = draw.alpha;
            Vector(draw.light[0] * fLumi, draw.light[1] * fLumi, draw.light[2] * fLumi,
                   b->BodyLight);
            b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
        }
        else if (draw.type == MODEL_NIGHTWATER_01)
        {
            float fLumi = draw.alpha;
            Vector(draw.light[0] * fLumi, draw.light[1] * fLumi, draw.light[2] * fLumi,
                   b->BodyLight);
            b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV);
        }
        else if (draw.type == MODEL_KNIGHT_PLANCRACK_A)
        {
            float fLumi = draw.alpha;
            Vector(draw.light[0] * fLumi, draw.light[1] * fLumi, draw.light[2] * fLumi,
                   b->BodyLight);
            b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, draw.blendLight,
                          draw.blendU, -(int)WorldTime % 2000 * 0.0001f);
        }
        else if (draw.type == MODEL_KNIGHT_PLANCRACK_B)
        {
            float fLumi = draw.alpha;
            Vector(draw.light[0] * fLumi, draw.light[1] * fLumi, draw.light[2] * fLumi,
                   b->BodyLight);
            b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, draw.blendLight,
                          draw.blendU, -(int)WorldTime % 2000 * 0.0001f);
        }
        else if (draw.type == MODEL_ALICE_BUFFSKILL_EFFECT ||
                 draw.type == MODEL_ALICE_BUFFSKILL_EFFECT2)
        {
            if (o->SubType == 1)
            {
                b->RenderBody(RENDER_TEXTURE | RENDER_DARK, draw.alpha, 0, draw.blendLight,
                              draw.blendU, draw.blendV);
            }
            else
            {
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                              draw.blendU, draw.blendV);
            }
        }
        else if (draw.type == MODEL_CURSEDTEMPLE_HOLYITEM)
        {
            vec3_t Light;
            float fLuminosity = (float)sinf((WorldTime) * 0.0002f) * 0.002f;
            Vector(0.9f, 0.4f, 0.4f, Light);
            CreateSprite(BITMAP_LIGHT, draw.position, 3.f, Light, o, 0.0f, 0); // flare01.jpg
            Vector(0.9f, 0.6f, 0.6f, Light);
            CreateSprite(BITMAP_POUNDING_BALL, draw.position, 0.9f + fLuminosity, Light, NULL,
                         (WorldTime / 10.0f));

            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, 0);
            b->RenderMesh(0, RENDER_TEXTURE, 0.7f, 0, draw.blendLight * 3.f, draw.blendU,
                          -WorldTime * 0.004f);
        }
        else if (draw.type == MODEL_CURSEDTEMPLE_PRODECTION_SKILL)
        {
            Vector(0.3f, 0.3f, 1.0f, b->BodyLight);
            VectorCopy(draw.angle, b->BodyAngle);
            b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, 0, draw.blendLight, draw.blendU,
                          -WorldTime * 0.0004f);
        }
        else if (draw.type == MODEL_CURSEDTEMPLE_RESTRAINT_SKILL)
        {
            float fLuminosity = (float)sinf((WorldTime) * 0.0002f) * 0.002f;
            b->RenderMesh(0, RENDER_TEXTURE, 0.7f, 0, 0.35f + fLuminosity, -WorldTime * 0.0004f,
                          WorldTime * 0.0004f);
        }
        else if (gMapManager.IsCursedTemple() == true &&
                 (draw.type == 32 || draw.type == 39 || draw.type == 41 || draw.type == 46 ||
                  draw.type == 62 || draw.type == 67 || draw.type == 68 || draw.type == 64 ||
                  draw.type == 65 || draw.type == 66 || draw.type == 80))
        {
            if (draw.type == 32)
            {
                b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
                b->RenderMesh(1, RENDER_TEXTURE | RENDER_CHROME, draw.alpha, draw.blendMesh,
                              draw.blendLight);
                b->StreamMesh = 2;
                b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, -(int)WorldTime % 4000 * 0.00025f, draw.blendV);
                b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, -(int)WorldTime % 5000 * 0.0002f, draw.blendV);
                b->StreamMesh = -1;
            }
            else if (draw.type == 39 || draw.type == 41)
            {
                b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
                b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight);
            }
            else if (draw.type == 46)
            {
                b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
                b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
                b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight);
            }
            else if (draw.type == 62)
            {
                b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
                b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME, draw.alpha,
                              draw.blendMesh, draw.blendLight);
                b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight);
                b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME, draw.alpha,
                              draw.blendMesh, draw.blendLight);

                vec3_t vPos, vRelativePos, vLight;
                Vector(0.f, 0.f, 0.f, vPos);
                Vector(6.f, 5.f, 2.f, vRelativePos);
                Vector(1.f, 1.f, 1.f, vLight);
                float fLumi = sinf(WorldTime * 0.001f) * 0.5f + 0.5f;
                b->TransformPosition(draw.bones[22], vRelativePos, vPos);
                Vector(1.f, 0.5f, 0.5f, vLight);
                CreateSprite(BITMAP_SHINY + 1, vPos, 1.5f + fLumi / 2.f, vLight, NULL);
                Vector(6.f, -5.f, 2.f, vRelativePos);
                b->TransformPosition(draw.bones[23], vRelativePos, vPos);
                Vector(1.f, 0.5f, 0.5f, vLight);
                CreateSprite(BITMAP_SHINY + 1, vPos, 1.5f + fLumi / 2.f, vLight, NULL);
            }
            else if (draw.type == 67)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME, draw.alpha,
                              draw.blendMesh, draw.blendLight);
            }
            else if (draw.type == 68)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight);
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME, draw.alpha,
                              draw.blendMesh, draw.blendLight);
            }
            else if (draw.type == 64 || draw.type == 65 || draw.type == 66 || draw.type == 80)
            {
                // Placement preparation selects the after-character pass.
            }
        }
        else if (draw.type == MODEL_EFFECT_SKURA_ITEM)
        {
            b->RenderBody(RENDER_COLOR, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }

        else if (draw.type == MODEL_DEVIL)
        {
            Vector(0.4f, 0.6f, 1.f, b->BodyLight);
            b->StreamMesh = 0;
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh, BITMAP_CHROME);
            Vector(1.f, 1.f, 1.f, b->BodyLight);
            b->StreamMesh = -1;
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
        else if (draw.type == MODEL_PEGASUS)
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
        }
        else if (draw.type == MODEL_SKIN_SHELL)
        {
            if (o->SubType == 0)
            {
                VectorCopy(draw.light, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh, BITMAP_CHROME);
            }
            else if (o->SubType == 1)
            {
                Vector(0.1f, 0.5f, 1.f, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh,
                              BITMAP_CHROME);
            }
        }
        else if (draw.type == MODEL_STUN_STONE)
        {
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);

            Vector(1.f, 1.f, 1.f, b->BodyLight);
            b->RenderBody(RENDER_CHROME, 0.5f, draw.blendMesh, draw.blendLight, draw.blendU * 5.f,
                          draw.blendV * 2.f, -1, BITMAP_CHROME);
        }
        else if (draw.type == MODEL_DARK_HORSE && o->SubType == 1)
        {
            if (TheMapProcess().CharacterPolicy().cloneAppearances)
            {
                draw.alpha = 0.7f;
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else
            {
                Vector(0.2f, 0.2f, 0.2f, b->BodyLight);
                b->RenderBody(RENDER_BRIGHT | RENDER_TEXTURE, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            }
        }
        else if (draw.type == MODEL_DARK_HORSE)
        {
            Vector(1.f, 1.f, 1.f, b->BodyLight);
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);

            Vector(1.f, 0.8f, 0.3f, b->BodyLight);
            b->RenderMesh(12, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            b->RenderMesh(13, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            b->RenderMesh(14, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            b->RenderMesh(15, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                          draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);

            if (TheMapProcess().CharacterPolicy().groundShadows)
            {
                if (!TheMapProcess().TerrainCutscene())
                {
                    EnableAlphaTest();

                    if (TheMapProcess().CharacterPolicy().shadowAlpha < 1.f)
                    {
                        glColor4f(0.f, 0.f, 0.f, TheMapProcess().CharacterPolicy().shadowAlpha);
                    }
                    else
                    {
                        glColor4f(0.f, 0.f, 0.f, 0.7f);
                    }
                    b->RenderBodyShadow(-1, -1, 8, 9);
                }
            }
        }
        else if (draw.type == MODEL_FENRIR_BLACK || draw.type == MODEL_FENRIR_BLUE ||
                 draw.type == MODEL_FENRIR_RED || draw.type == MODEL_FENRIR_GOLD)
        {
            vec3_t vLight, vPos, vPosition;
            float fLuminosity = (float)sinf((WorldTime) * 0.002f) * 0.2f;

            b->BeginRender(1.f);

            b->BodyLight[0] = 1.0f;
            b->BodyLight[1] = 1.0f;
            b->BodyLight[2] = 1.0f;

            if (draw.type == MODEL_FENRIR_GOLD)
            {
                b->StreamMesh = 0;

                b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME, draw.alpha,
                              draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
                b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);

                b->StreamMesh = -1;
            }
            else
            {
                b->StreamMesh = 0;

                b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);

                // custom glow? :
                b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME, draw.alpha,
                              draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);

                if (draw.action == FENRIR_ATTACK_SKILL)
                {
                    b->BodyLight[0] = 1.0f;
                    b->BodyLight[1] = 1.0f;
                    b->BodyLight[2] = 1.0f;

                    b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT | RENDER_CHROME, draw.alpha,
                                  draw.blendMesh, draw.blendLight, draw.blendU, draw.blendV);
                }
                b->StreamMesh = -1;
            }

            b->EndRender();

            if (TheMapProcess().CharacterPolicy().groundShadows)
            {
                if (!TheMapProcess().TerrainCutscene())
                {
                    EnableAlphaTest();

                    if (TheMapProcess().CharacterPolicy().shadowAlpha < 1.f)
                    {
                        glColor4f(0.f, 0.f, 0.f, TheMapProcess().CharacterPolicy().shadowAlpha);
                    }
                    else
                    {
                        glColor4f(0.f, 0.f, 0.f, 0.7f);
                    }

                    b->RenderBodyShadow();
                }
            }

            Vector(0.9f + fLuminosity, 0.2f + (fLuminosity * 0.5f), 0.1f + (fLuminosity * 0.5f),
                   vLight);
            Vector(50.f, 2.f, 11.f, vPos);
            b->TransformPosition(draw.bones[11], vPos, vPosition, false);
            CreateSprite(BITMAP_LIGHT, vPosition, 0.5f + (fLuminosity * 0.1f), vLight, o);
            CreateSprite(BITMAP_LIGHT, vPosition, 0.5f + (fLuminosity * 0.1f), vLight, o);
            Vector(50.f, 2.f, -11.f, vPos);
            b->TransformPosition(draw.bones[11], vPos, vPosition, false);
            CreateSprite(BITMAP_LIGHT, vPosition, 0.5f + (fLuminosity * 0.1f), vLight, o);
            CreateSprite(BITMAP_LIGHT, vPosition, 0.5f + (fLuminosity * 0.1f), vLight, o);

            Vector(1.0f, 0.3f, 0.2f, vLight);
            Vector(40.f, 15.f, 0.f, vPos);
            b->TransformPosition(draw.bones[13], vPos, vPosition, false);
            CreateSprite(BITMAP_LIGHT, vPosition, 1.5f, vLight, o);
            CreateSprite(BITMAP_LIGHT, vPosition, 1.0f, vLight, o);
        }
        else if (draw.type >= MODEL_FACE && draw.type <= MODEL_FACE + 6)
        {
            Vector(4.8f, 4.8f, 4.8f, b->BodyLight);
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
            Vector(1.f, 1.f, 1.f, b->BodyLight);
        }
        else if (draw.type == MODEL_DARKLORD_SKILL)
        {
            VectorCopy(draw.light, b->BodyLight);
            b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight, draw.blendU,
                          draw.blendV, draw.hiddenMesh);
            Vector(1.f, 1.f, 1.f, b->BodyLight);
        }
        else if (draw.type == MODEL_DARK_SPIRIT)
        {
            glColor3f(1.f, 1.f, 1.f);
            b->BeginRender(draw.alpha);
            b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                          draw.blendU, draw.blendV, draw.hiddenMesh);
            if (o->WeaponLevel >= 40)
            {
                b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                Vector(0.3f, 0.6f, 1.f, b->BodyLight);
                b->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                Vector(1.f, 1.f, 1.f, b->BodyLight);
                b->RenderMesh(3, RENDER_BRIGHT | RENDER_TEXTURE, draw.alpha, 3,
                              sinf(WorldTime * 0.001f), draw.blendU, draw.blendV,
                              BITMAP_MONSTER_SKIN + 2);
            }
            else if (o->WeaponLevel >= 20)
            {
                b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else
            {
                b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            b->EndRender();

            if (TheMapProcess().CharacterPolicy().groundShadows)
            {
                if (!TheMapProcess().TerrainCutscene())
                {
                    vec3_t Position;
                    EnableAlphaTest();

                    if (TheMapProcess().CharacterPolicy().shadowAlpha < 1.f)
                    {
                        glColor4f(0.f, 0.f, 0.f, TheMapProcess().CharacterPolicy().shadowAlpha);
                    }
                    else
                    {
                        glColor4f(0.f, 0.f, 0.f, 1.f);
                    }
                    VectorCopy(draw.position, Position);
                    Position[2] = RequestTerrainHeight(draw.position[0], draw.position[1]);
                    VectorCopy(Position, b->BodyOrigin);
                    b->RenderBodyShadow();
                }
            }
        }
        else if (TheMapProcess().RenderWholeObject(draw, b, ExtraMon))
        {
        }
        else
        {
            BOOL bIsRendered = TRUE;

            if (g_isCharacterBuff(o, eDeBuff_Poison) && g_isCharacterBuff(o, eDeBuff_Freeze))
            {
                Vector(0.3f, 1.f, 0.8f, b->BodyLight);
            }
            else if (g_isCharacterBuff(o, eDeBuff_Poison))
            {
                Vector(0.3f, 1.f, 0.5f, b->BodyLight);
            }
            else if (g_isCharacterBuff(o, eDeBuff_Freeze))
            {
                Vector(0.3f, 0.5f, 1.f, b->BodyLight);
            }
            else if (g_isCharacterBuff(o, eDeBuff_BlowOfDestruction))
            {
                Vector(0.3f, 0.5f, 1.f, b->BodyLight);
            }

            if (draw.type == MODEL_VALKYRIE || draw.type == MODEL_ICE_MONSTER ||
                draw.type == MODEL_ALQUAMOS || draw.type == MODEL_QUEEN_RAINER)
            {
                if (draw.alpha == 1.0f && draw.blendLight == 0.05f)
                {
                    draw.blendLight = 1.0f;
                }
            }

            if (TheMapProcess().RenderObjectMesh(draw, b, ExtraMon))
            {
            }
            else if (draw.type == MODEL_KALIMA_SHOP)
            {
                Vector(1.f, 1.f, 1.f, b->BodyLight);
                float Luminosity = sinf(WorldTime * 0.002f) * 0.3f + 0.6f;
                b->BeginRender(draw.alpha);
                b->RenderMesh(4, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                vec3_t Position, Light, p, Angle;
                Vector(0.4f, 0.2f, 0.4f, Light);
                Vector(30.f, 40.f, 0.f, p);
                b->TransformPosition(draw.bones[3], p, Position);

                VectorAdd(Position, draw.position, Position);
                CreateSprite(BITMAP_LIGHT, Position, Luminosity + 3.5f, Light, o, 50.f);

                Vector(0.f, 0.f, 0.f, p);
                b->TransformPosition(draw.bones[21], p, Position);
                VectorAdd(Position, draw.position, Position);
                CreateSprite(BITMAP_LIGHT, Position, Luminosity + 3.5f, Light, o, 50.f);

                b->EndRender();
            }
            else if (draw.type == MODEL_NPC_QUARREL)
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
            else if (draw.type == MODEL_SEED_MASTER)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                float fLumi = (sinf(WorldTime * 0.001f) + 1.0f) * 0.2f + 0.0f;
                b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, fLumi, 1, fLumi, draw.blendU,
                              draw.blendV);
            }
            else if (draw.type == MODEL_MUTANT)
            {
                if (ExtraMon)
                {
                    Vector(1.f, 1.f, 1.f, b->BodyLight);
                    b->BeginRender(draw.alpha);
                    b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV);
                    b->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);
                    b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV);
                    b->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);
                    b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV);
                    b->RenderMesh(2, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);
                    b->EndRender();
                }
                else
                {
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                }
            }
            else if (draw.type == MODEL_TANTALLOS && o->SubType == 1)
            {
                b->BeginRender(draw.alpha);
                float Light = sinf(WorldTime * 0.002f) * 0.01f + 1.f;
                b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, Light, draw.blendU,
                              draw.blendV, BITMAP_MONSTER_SKIN + 1);
                b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, Light, draw.blendU,
                              draw.blendV, BITMAP_MONSTER_SKIN);
                b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, Light, draw.blendU,
                              draw.blendV);
                b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, Light, draw.blendU,
                              draw.blendV);
                b->EndRender();
            }
            else if (draw.type == MODEL_MAGIC_SKELETON)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, 2, 1.0f, draw.blendU, draw.blendV);
            }
            else if (draw.type == MODEL_MOLT)
            {
                b->BeginRender(draw.alpha);
                b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->EndRender();
                b->EnsureCpuTransforms();
                if (!sideHair_)
                    sideHair_ = std::make_unique<CSideHair>(sessionKeeper_);
                sideHair_->Create(VertexTransform, b, draw);
                sideHair_->Render(VertexTransform, LightTransform);
            }
            else if (draw.type == MODEL_DRAKAN)
            {
                if (ExtraMon)
                {
                    Vector(.1f, 0.1f, 0.1f, b->BodyLight);
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, 1);
                    Vector(1.0f, 0.1f, 0.1f, b->BodyLight);
                    b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV);
                }
                else
                {
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                }
            }
            else if (draw.type == MODEL_ORC_ARCHER)
            {
                if (ExtraMon)
                {
                    Vector(1.0f, 1.0f, 1.0f, b->BodyLight);
                    b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, BITMAP_DEST_ORC_WAR0);
                    //					b->RenderMesh(1,RENDER_TEXTURE,draw.alpha,draw.blendMesh,draw.blendLight,draw.blendU,draw.blendV,BITMAP_DEST_ORC_WAR1);
                    b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, BITMAP_DEST_ORC_WAR2);
                }
                else
                {
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                }
            }
            else if (draw.type == MODEL_ORC)
            {
                if (ExtraMon)
                {
                    Vector(1.0f, 1.0f, 1.0f, b->BodyLight);
                    b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, BITMAP_DEST_ORC_WAR1);
                    b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, BITMAP_DEST_ORC_WAR0);
                    //b->RenderMesh(2,RENDER_TEXTURE,draw.alpha,draw.blendMesh,draw.blendLight,draw.blendU,draw.blendV,BITMAP_DEST_ORC_WAR2);
                }
                else
                {
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                }
            }
            else if (draw.type == MODEL_CURSED_KING)
            {
                if (ExtraMon)
                {
                    Vector(1.0f, 1.0f, 1.0f, b->BodyLight);
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, 1, BITMAP_WHITE_WIZARD);
                }
                else
                {
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                }
            }
            else if (draw.type == MODEL_CRUST)
            {
                if (ExtraMon == 0)
                {
                    float Luminosity = sinf(WorldTime * 0.002f) * 0.3f + 0.5f;
                    Vector(Luminosity + 0.5f, 0.3f - Luminosity * 0.5f, -Luminosity * 0.5f + 0.5f,
                           b->BodyLight);
                    //Vector(1.f,1.f,1.f,b->BodyLight);
                    //if ( c->Dead == 0)
                    {
                        b->StreamMesh = 0;
                        Vector(.4f, .3f, .5f, b->BodyLight);
                        b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                      draw.blendU, draw.blendV, draw.hiddenMesh, BITMAP_JANUSEXT);
                        Vector(.5f, .5f, .5f, b->BodyLight);
                        b->StreamMesh = -1;
                    }
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                }
                else if (ExtraMon == 301)
                {
                    //					b->BodyScale     = draw.scale + (draw.scale/1.0f);
                    //					Vector(0.7f,0.5f,0.8f,b->BodyLight);
                    Vector(1.f, 1.f, 1.f, b->BodyLight);
                    b->BeginRender(draw.alpha);
                    b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV);
                    b->RenderMesh(0, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);
                    b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV);
                    b->RenderMesh(1, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV, BITMAP_CHROME);
                    b->EndRender();
                }
                else
                {
                    Vector(0.1f, 1.0f, .8f, b->BodyLight);
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                    //Vector( 0.1f, sinf(WorldTime*0.002f)*0.5f+0.5f, sinf(WorldTime*0.00173f)*0.5f+0.5f,b->BodyLight);
                }
            }
            else if (draw.type == MODEL_PHANTOM_KNIGHT)
            {
                float Luminosity = sinf(WorldTime * 0.002f) * 0.3f + 0.5f;
                Vector(Luminosity + 0.5f, 0.3f - Luminosity * 0.5f, -Luminosity * 0.5f + 0.5f,
                       b->BodyLight);
                //Vector(1.f,1.f,1.f,b->BodyLight);
                Vector(.9f, .8f, 1.0f, b->BodyLight);
                b->RenderBody(RENDER_CHROME, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh, BITMAP_CHROME + 1);
                Vector(1.f, 1.f, 1.f, b->BodyLight);
                //Vector(.7f,.2f,.2f,b->BodyLight);
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_QUEEN_RAINER)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, 1, -1);
                //b->RenderMesh(1,RENDER_TEXTURE|RENDER_PONG|RENDER_WAVE,1.0f,draw.blendMesh,draw.blendLight,draw.blendU,draw.blendV,draw.hiddenMesh);
                b->RenderMesh(1, RENDER_TEXTURE | RENDER_WAVE, 0.5f, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderMesh(1, RENDER_TEXTURE | RENDER_WAVE, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_LUNAR_RABBIT)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_MOONHARVEST_MOON)
            {
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_MOONHARVEST_GAM)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_MOONHARVEST_SONGPUEN1)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_MOONHARVEST_SONGPUEN2)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_NPC_CHERRYBLOSSOM)
            {
                b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, 2.f, draw.blendU,
                              draw.blendV);
                b->RenderMesh(0, RENDER_TEXTURE | RENDER_CHROME7, 0.25f, draw.blendMesh, 1.f,
                              draw.blendU, draw.blendV);
                b->RenderBody(RENDER_TEXTURE, 0.8f, draw.blendMesh, 2.f, draw.blendU, draw.blendV,
                              0);
            }
            else if (draw.type == MODEL_NPC_CHERRYBLOSSOMTREE)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_DARK_PHEONIX)
            {
                b->StreamMesh = 0;
                Vector(1.f, 1.0f, 1.0f, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh,
                              BITMAP_CHROME);
                b->StreamMesh = -1;
                Vector(.6f, .6f, .6f, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_STATUE_OF_SAINT)
            {
                if (draw.action == MONSTER01_DIE || (WorldTime - o->InitialSceneTime) < 1000)
                {
                    // Destruction and appearance debris advance with the observer.
                }
                else
                {
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                    Vector(1.f, 1.f, 1.f, b->BodyLight);
                    b->RenderBody(RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh,
                                  BITMAP_CHROME);
                    Vector(0.3f, 0.3f, 1.f, b->BodyLight);
                    b->RenderBody(RENDER_BRIGHT | RENDER_METAL, draw.alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh,
                                  BITMAP_CHROME);
                }
            }
            else if (draw.type == MODEL_CASTLE_GATE && draw.action == MONSTER01_DIE)
            {
                // Destruction is emitted by the observer update.
            }
            else if (draw.type == MODEL_STONE_COFFIN || draw.type == MODEL_STONE_COFFIN + 1)
            {
                if (o->SubType == 2 || o->SubType == 3)
                {
                    if (o->SubType == 2)
                    {
                        Vector(0.1f, 0.3f, 0.6f, b->BodyLight);
                    }
                    else
                    {
                        Vector(0.1f, 0.6f, 0.3f, b->BodyLight);
                    }
                    b->RenderBody(RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh,
                                  BITMAP_CHROME);
                }
                else
                {
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                    Vector(1.f, 1.f, 1.f, b->BodyLight);
                    b->RenderBody(RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh,
                                  BITMAP_CHROME);
                    Vector(0.3f, 0.3f, 1.f, b->BodyLight);
                    b->RenderBody(RENDER_BRIGHT | RENDER_METAL, draw.alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh,
                                  BITMAP_CHROME);
                }
            }
            else if (draw.type == MODEL_FLY_BIG_STONE1)
            {
                {
                    Vector(1.f, 1.f, 1.f, b->BodyLight);
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                    if (o->SubType <= 1)
                    {
                        Vector(1.0f, 0.2f, 0.1f, b->BodyLight);
                        b->RenderBody(RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                                      1.f, draw.blendU, draw.blendV, draw.hiddenMesh);
                    }
                }
            }
            else if (draw.type == MODEL_FLY_BIG_STONE2)
            {
                {
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                }
            }
            else if (draw.type == MODEL_BIG_STONE_PART1 || draw.type == MODEL_BIG_STONE_PART2)
            {
                if (o->SubType == 1)
                {
                    Vector(1.f, 1.f, 1.f, b->BodyLight);
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                    Vector(1.0f, 0.2f, 0.1f, b->BodyLight);
                    b->RenderBody(RENDER_CHROME | RENDER_BRIGHT, draw.alpha, draw.blendMesh, 1.f,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                    Vector(1.f, 1.f, 1.f, b->BodyLight);
                }
                else if (o->SubType == 2 || o->SubType == 3)
                {
                    Vector(0.5f, 1.0f, 0.3f, b->BodyLight);
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                    Vector(1.f, 1.f, 1.f, b->BodyLight);
                }
                else
                {
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                }
            }
            else if (draw.type == MODEL_WALL_PART1 || draw.type == MODEL_WALL_PART2)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_CHAOS_CASTLE_KNIGHT || draw.type == MODEL_CHAOS_CASTLE_ELF)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_COMBO && o->SubType == 1)
            {
                b->RenderBody(RENDER_TEXTURE | o->RenderType, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_CIRCLE_LIGHT && (o->SubType == 3 || o->SubType == 4))
            {
                Vector(0.1f, 0.1f, 10.f, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE | o->RenderType, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                Vector(1.f, 1.f, 1.f, b->BodyLight);
            }
            else if (draw.type == MODEL_CIRCLE && (o->SubType == 2 || o->SubType == 3))
            {
                Vector(0.5f, 0.5f, 1.f, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh, BITMAP_MAGIC_EMBLEM);
                Vector(1.f, 1.f, 1.f, b->BodyLight);
            }
            else if (draw.type == MODEL_MULTI_SHOT1 || draw.type == MODEL_MULTI_SHOT2 ||
                     draw.type == MODEL_MULTI_SHOT3)
            {
                VectorCopy(draw.light, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_DESAIR)
            {
                Vector(1.f, 1.f, 1.f, draw.light);
                VectorCopy(draw.light, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE | RENDER_DARK, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_DARK_SCREAM)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, 1.f, draw.blendU,
                              draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_DARK_SCREAM_FIRE)
            {
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh, 1.f,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_ARROW_SPARK)
            {
                b->RenderBody(RENDER_TEXTURE | RENDER_CHROME5 | RENDER_BRIGHT, draw.alpha,
                              draw.blendMesh, 1.f, draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_SKULL)
            {
                Vector(1.f, 0.6f, 0.3f, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_WAVES)
            {
                if (o->SubType == 3)
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, -1, BITMAP_PINK_WAVE);
                else if (o->SubType == 4 || o->SubType == 5)
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, -1, BITMAP_PINK_WAVE);
                else
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_PROTECTGUILD)
            {
                Vector(0.4f, 0.6f, 1.f, b->BodyLight);
                b->RenderBody(RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderBody(RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_WEBZEN_MARK)
            {
                Vector(1.f, 1.f, 1.f, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                Vector(1.f, 0.5f, 0.f, b->BodyLight);
                b->RenderBody(RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_MANA_RUNE && o->SubType == 0)
            {
                Vector(0.3f, 0.6f, 1.f, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderBody(RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_SKILL_JAVELIN)
            {
                Vector(1.f, 0.6f, 0.3f, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderBody(RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, 1);
            }
            else if (draw.type == MODEL_MAGIC_CAPSULE2 && o->SubType == 1)
            {
                b->RenderBody(RENDER_BRIGHT | RENDER_CHROME4, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, 1);
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_ARROW_AUTOLOAD)
            {
                VectorCopy(draw.light, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            }

            else if (draw.type == MODEL_INFINITY_ARROW)
            {
                // Its visible result is emitted by the effect owner.
            }
            else if (draw.type >= MODEL_INFINITY_ARROW && draw.type <= MODEL_INFINITY_ARROW4)
            {
                VectorCopy(draw.light, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_SHIELD_CRASH || draw.type == MODEL_SHIELD_CRASH2)
            {
                VectorCopy(draw.light, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_IRON_RIDER_ARROW)
            {
                VectorCopy(draw.light, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_BLADE_SKILL)
            {
                VectorCopy(draw.light, b->BodyLight);
                for (int abc = 0; abc < 3; abc++)
                    b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_KENTAUROS_ARROW)
            {
                VectorCopy(draw.light, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_XMAS_EVENT_EARRING)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderBody(RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                              draw.blendLight, -WorldTime * 0.0002f, draw.blendV, BITMAP_CHROME);
            }
            else if (draw.type == MODEL_XMAS_EVENT_ICEHEART)
            {
                b->StreamMesh = 0;
                Vector(1.f, 0.4f, 0.4f, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, -(int)WorldTime % 2000 * 0.0005f, draw.blendV,
                              draw.hiddenMesh);
                b->StreamMesh = -1;
            }
            else if (draw.type == MODEL_ARROW_BEST_CROSSBOW)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_NEWYEARSDAY_EVENT_PIG)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                Vector(1.0f, 0.4f, 0.2f, b->BodyLight);
                b->RenderBody(RENDER_CHROME3 | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                Vector(1.0f, 1.f, 1.f, b->BodyLight);
            }
            else if (draw.type == MODEL_FENRIR_THUNDER)
            {
                if (o->SubType == 1)
                {
                    VectorCopy(draw.light, b->BodyLight);
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                    Vector(1.f, 1.f, 1.f, b->BodyLight);
                }
                else
                {
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                }
            }
            else if (draw.type == MODEL_MAP_TORNADO)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, -1);
            }
            else if (draw.type >= MODEL_SUMMONER_CASTING_EFFECT1 &&
                     draw.type <= MODEL_SUMMONER_CASTING_EFFECT4)
            {
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, -1);
            }
            else if (draw.type == MODEL_SUMMONER_SUMMON_SAHAMUTT)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, -1);
                b->RenderBody(RENDER_BRIGHT | RENDER_CHROME7, draw.alpha, draw.blendMesh,
                              draw.alpha, draw.blendU, draw.blendV, -1);
            }
            else if (draw.type == MODEL_SUMMONER_SUMMON_NEIL)
            {
                if (draw.alpha < 0.7f)
                {
                    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.alpha,
                                  draw.blendU, draw.blendV, -1);
                    b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 2, draw.alpha,
                                  draw.blendU, draw.blendV, -1);
                }
                else
                {
                    b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, -1);
                    b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, -1);
                }
                Vector(1.0f, 0.0f, 0.0f, b->BodyLight);
                b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, 1, draw.alpha, draw.blendU,
                              draw.blendV, -1);

                vec3_t vPos, vRelative, vLight;
                Vector(0.0f, 0.0f, 0.0f, vRelative);
                Vector(1.0f, 0.0f, 0.0f, vLight);
                for (int i = 51; i <= 59; ++i)
                {
                    b->TransformPosition(draw.bones[i], vRelative, vPos, false);
                    CreateSprite(BITMAP_LIGHT, vPos, 1.0f, vLight, o);
                }
            }
            else if (draw.type >= MODEL_SUMMONER_SUMMON_NEIL_NIFE1 &&
                     draw.type <= MODEL_SUMMONER_SUMMON_NEIL_NIFE3)
            {
                b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, -1);
                b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, -1);
                Vector(1.0f, 0.0f, 0.0f, b->BodyLight);
                b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, draw.alpha,
                              draw.blendU, draw.blendV, -1);
            }
            else if (draw.type >= MODEL_SUMMONER_SUMMON_NEIL_GROUND1 &&
                     draw.type <= MODEL_SUMMONER_SUMMON_NEIL_GROUND3)
            {
                b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.alpha,
                              draw.blendU, draw.blendV, -1);
                b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, draw.alpha,
                              -WorldTime * 0.001f, draw.blendV, -1);
            }
            else if (draw.type == MODEL_SUMMONER_SUMMON_LAGUL)
            {
                b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.alpha,
                              draw.blendU, draw.blendV, -1);
                b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, draw.alpha,
                              draw.blendU, draw.blendV, -1);
            }
            else if (draw.type >= MODEL_EFFECT_BROKEN_ICE0 && draw.type <= MODEL_EFFECT_BROKEN_ICE3)
            {
                VectorCopy(draw.light, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_MOVE_TARGETPOSITION_EFFECT)
            {
                Vector(1.0f, 0.7f, 0.3f, b->BodyLight);
                b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight);
            }
            else if (draw.type == MODEL_ARROW_DARKSTINGER)
            {
                b->BodyLight[0] = 0.7f;
                b->BodyLight[1] = 0.7f;
                b->BodyLight[2] = 0.9f;

                b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, 0, 1.f, draw.blendU, draw.blendV);

                b->BodyLight[0] = 0.3f;
                b->BodyLight[1] = 0.4f;
                b->BodyLight[2] = 0.9f;
                b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, 1, 1.f, draw.blendU, draw.blendV);
                b->BodyLight[0] = 1.0f;
                b->BodyLight[1] = 1.0f;
                b->BodyLight[2] = 1.0f;
            }
            else if (draw.type == MODEL_FEATHER)
            {
                if (o->SubType == 2 || o->SubType == 3)
                    b->RenderBody(RENDER_TEXTURE | RENDER_DARK, draw.alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                else
                    b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, 0, draw.alpha, draw.blendU,
                                  draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_ARROW_GAMBLE)
            {
                b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.alpha,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_DEMON)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                //b->RenderMesh(0, RENDER_TEXTURE|RENDER_BRIGHT, draw.alpha, 0, draw.blendLight, draw.blendU, draw.blendV, BITMAP_DEMONWING_R);
            }
            else if (draw.type == MODEL_SPIRIT_OF_GUARDIAN)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else
            {
                bIsRendered = FALSE;
            }

            if (bIsRendered == TRUE)
            {
            }
            else if (draw.type == MODEL_XMAS2008_SNOWMAN)
            {
                if (draw.action != MONSTER01_DIE)
                {
                    vec3_t vRelativePos, vWorldPos, Light;
                    Vector(0.f, 0.f, 0.f, vRelativePos);
                    Vector(0.8f, 0.8f, 0.9f, Light);

                    b->TransformPosition(draw.bones[7], vRelativePos, vWorldPos, true);
                    CreateSprite(BITMAP_LIGHT, vWorldPos, 6.0f, Light, o);
                    CreateSprite(BITMAP_LIGHT, vWorldPos, 4.0f, Light, o);

                    b->TransformPosition(draw.bones[34], vRelativePos, vWorldPos, true);
                    Vector(1.0f, 0.8f, 0.2f, Light);
                    CreateSprite(BITMAP_LIGHT, vWorldPos, 2.0f, Light, o);

                    b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                    b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                    b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                    b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, 1, 0.5f, draw.blendU, draw.blendV,
                                  draw.hiddenMesh);
                }
            }
            else if (draw.type == MODEL_XMAS2008_SNOWMAN_BODY)
            {
                b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, 1, 0.5f, draw.blendU, draw.blendV,
                              draw.hiddenMesh);
            }
            else if (draw.type == MODEL_FEATHER_FOREIGN)
            {
                if (o->SubType == 2 || o->SubType == 3)
                    b->RenderBody(RENDER_TEXTURE | RENDER_DARK, draw.alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                else
                    b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, 0, draw.alpha, draw.blendU,
                                  draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_SWELL_OF_MAGICPOWER)
            {
                draw.blendMesh = 0;
                Vector(0.7f, 0.4f, 0.9f, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_ARROWSRE06)
            {
                draw.blendMesh = 0;
                VectorCopy(draw.light, b->BodyLight);
                //Vector(0.7f, 0.2f, 0.9f, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_XMAS2008_SNOWMAN_NPC)
            {
                vec3_t vRelativePos, vWorldPos, Light;
                Vector(0.f, 0.f, 0.f, vRelativePos);
                Vector(0.8f, 0.8f, 0.9f, Light);

                b->TransformPosition(draw.bones[7], vRelativePos, vWorldPos, true);
                CreateSprite(BITMAP_LIGHT, vWorldPos, 6.0f, Light, o);
                CreateSprite(BITMAP_LIGHT, vWorldPos, 4.0f, Light, o);

                b->TransformPosition(draw.bones[34], vRelativePos, vWorldPos, true);
                Vector(1.0f, 0.8f, 0.2f, Light);
                CreateSprite(BITMAP_LIGHT, vWorldPos, 2.0f, Light, o);

                b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, 1, 0.5f, draw.blendU, draw.blendV,
                              draw.hiddenMesh);
            }
            else if (draw.type == MODEL_XMAS2008_SANTA_NPC)
            {
                vec3_t vRelativePos, vWorldPos, Light;
                Vector(0.f, 0.f, 0.f, vRelativePos);

                b->TransformPosition(draw.bones[4], vRelativePos, vWorldPos, true);
                Vector(1.0f, 0.8f, 0.2f, Light);
                CreateSprite(BITMAP_LIGHT, vWorldPos, 8.0f, Light, o);

                b->TransformPosition(draw.bones[13], vRelativePos, vWorldPos, true);
                Vector(1.0f, 0.4f, 0.0f, Light);
                CreateSprite(BITMAP_LIGHT, vWorldPos, 5.0f, Light, o);

                b->TransformPosition(draw.bones[38], vRelativePos, vWorldPos, true);
                Vector(1.0f, 0.8f, 0.2f, Light);
                CreateSprite(BITMAP_LIGHT, vWorldPos, 2.0f, Light, o);

                vRelativePos[1] = 17.0f;
                b->TransformPosition(draw.bones[53], vRelativePos, vWorldPos, true);
                Vector(1.0f, 0.4f, 0.0f, Light);
                RenderAurora(BITMAP_LIGHTMARKS, RENDER_BRIGHT, vWorldPos[0], vWorldPos[1], 2.0f,
                             2.0f, Light);

                b->TransformPosition(draw.bones[58], vRelativePos, vWorldPos, true);
                Vector(1.0f, 0.4f, 0.0f, Light);
                RenderAurora(BITMAP_LIGHTMARKS, RENDER_BRIGHT, vWorldPos[0], vWorldPos[1], 2.0f,
                             2.0f, Light);

                Vector(0.f, 0.f, 0.f, vRelativePos);
                int temp[11] = {39, 41, 43, 44, 46, 49, 40, 42, 45, 47, 48};
                float fCos1 = cosf(WorldTime * 0.002f);
                float fCos2 = sinf(WorldTime * 0.002f);
                float fSize = 0.0f;
                for (int i = 0; i < 11; i++)
                {
                    b->TransformPosition(draw.bones[temp[i]], vRelativePos, vWorldPos, true);

                    if (i < 6)
                    {
                        Vector(1.0f, 0.9f, 0.3f, Light);
                        fSize = 0.7f * fCos1;
                    }
                    else
                    {
                        Vector(1.0f, 0.5f, 0.5f, Light);
                        fSize = 0.7f * fCos2;
                    }
                    CreateSprite(BITMAP_LIGHT, vWorldPos, fSize, Light, o);
                }

                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_CURSED_SANTA)
            {
                if (draw.action == MONSTER01_DIE)
                {
                    vec3_t vLight;
                    Vector(1.0 * draw.alpha, 0.8 * draw.alpha, 0.5 * draw.alpha, vLight);
                    vec3_t vPosition;
                    Vector(draw.position[0], draw.position[1], draw.position[2] + 200, vPosition);

                    if (draw.animationFrame >= 13) //11
                    {

                        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh,
                                      draw.blendLight, draw.blendU, draw.blendV, BITMAP_GOOD_SANTA);
                        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh,
                                      draw.blendLight, draw.blendU, draw.blendV,
                                      BITMAP_GOOD_SANTA_BAGGAGE);
                        b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh,
                                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);

                        CreateSprite(BITMAP_LIGHT, vPosition, 5.5f, vLight, o);
                    }
                    else if (draw.animationFrame >= 9) //9
                    {
                        draw.alpha = 1.0f;

                        float fFade = (13.0f - draw.animationFrame) / (13.0f - 9.0f);
                        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh,
                                      draw.blendLight, draw.blendU, draw.blendV, BITMAP_GOOD_SANTA);
                        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh,
                                      draw.blendLight, draw.blendU, draw.blendV,
                                      BITMAP_GOOD_SANTA_BAGGAGE);
                        b->RenderMesh(2, RENDER_TEXTURE, 1.0f - fFade, draw.blendMesh,
                                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);

                        b->RenderMesh(0, RENDER_TEXTURE, fFade, draw.blendMesh, draw.blendLight,
                                      draw.blendU, draw.blendV, draw.hiddenMesh);
                        b->RenderMesh(1, RENDER_TEXTURE, fFade, draw.blendMesh, draw.blendLight,
                                      draw.blendU, draw.blendV, draw.hiddenMesh);

                        CreateSprite(BITMAP_LIGHT, vPosition, 5.5f, vLight, o);
                    }
                    else
                    {

                        b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh,
                                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                        b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh,
                                      draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                    }
                }
                else
                {
                    b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                    b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                }
            }
            else if (draw.type >= MODEL_LITTLESANTA && draw.type <= MODEL_LITTLESANTA_END)
            {
                float fLumi = (sinf(WorldTime * 0.004f) + 1.2f) * 0.5f + 0.1f;
                auto Rotation = (float)((int)(WorldTime * 0.1f) % 360);
                vec3_t vWorldPos, vLight;

                LittleSantaLight(draw.type, vLight);
                b->TransformByObjectBone(vWorldPos, draw, 17);
                CreateSprite(BITMAP_LIGHTMARKS, vWorldPos, 0.7f, vLight, o, Rotation); //scale 0.7
                CreateSprite(BITMAP_LIGHTMARKS, vWorldPos, 1.3f, vLight, o, Rotation); //scale 1.3

                b->TransformByObjectBone(vWorldPos, draw, 20);
                CreateSprite(BITMAP_LIGHTMARKS, vWorldPos, 0.7f, vLight, o, Rotation); //scale 0.7
                CreateSprite(BITMAP_LIGHTMARKS, vWorldPos, 1.3f, vLight, o, Rotation); //scale 1.3

                RenderAurora(BITMAP_LIGHTMARKS, RENDER_BRIGHT, draw.position[0], draw.position[1],
                             2.0f + fLumi, 2.0f + fLumi, vLight);
                RenderAurora(BITMAP_LIGHTMARKS, RENDER_BRIGHT, draw.position[0], draw.position[1],
                             0.5f + fLumi, 0.5 + fLumi, vLight);

                b->RenderBody(RENDER_TEXTURE, 0.9f, draw.blendMesh, draw.blendLight, draw.blendU,
                              draw.blendV);
                Vector(b->BodyLight[0] * 0.5f, b->BodyLight[0] * 0.5f, b->BodyLight[0] * 0.5f,
                       b->BodyLight);
                b->RenderBody(RENDER_BRIGHT | RENDER_TEXTURE, 0.9f, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
            }
            else if (draw.type == MODEL_DUEL_NPC_TITUS)
            {
                b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(4, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                              draw.blendU, WorldTime * 0.0001f);
            }
#ifdef ASG_ADD_TIME_LIMIT_QUEST_NPC
            else if (draw.type == MODEL_GAMBLE_NPC_MOSS ||
                     draw.type == MODEL_TIME_LIMIT_QUEST_NPC_ZAIRO)
#else  // ASG_ADD_TIME_LIMIT_QUEST_NPC
            else if (draw.type == MODEL_GAMBLE_NPC_MOSS)
#endif // ASG_ADD_TIME_LIMIT_QUEST_NPC
            {
                vec3_t vRelativePos, vWorldPos, Light;
                Vector(0.f, 0.f, 0.f, vRelativePos);
                Vector(0.8f, 0.8f, 0.8f, Light);
                Vector(0.5f, 0.5f, 0.5f, Light);
                b->TransformPosition(draw.bones[55], vRelativePos, vWorldPos, true);
                CreateSprite(BITMAP_LIGHT, vWorldPos, 2.0f, Light, o);

                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_DOPPELGANGER_NPC_LUGARD)
            {
                b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(4, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                // 날개
                Vector(1.0f, 1.0f, 1.0f, b->BodyLight);
                b->RenderMesh(5, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(5, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 5, 0.1f, draw.blendU,
                              draw.blendV);
            }
            else if (draw.type == MODEL_DOPPELGANGER_NPC_BOX ||
                     draw.type == MODEL_DOPPELGANGER_NPC_GOLDENBOX)
            {
                if (draw.action == MONSTER01_DIE)
                {
                    draw.alpha = (10 - draw.animationFrame) * 0.1f;
                }

                if (draw.type == MODEL_DOPPELGANGER_NPC_BOX)
                {
                    b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV);
                    b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV);
                }
                else if (draw.type == MODEL_DOPPELGANGER_NPC_GOLDENBOX)
                {
                    b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, BITMAP_DOPPELGANGER_GOLDENBOX2);
                    b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, BITMAP_DOPPELGANGER_GOLDENBOX1);
                    b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV);
                    b->RenderMesh(1, RENDER_BRIGHT | RENDER_CHROME7, 0.5f, 1, 0.3f, draw.blendU,
                                  draw.blendV);
                }
            }
            else if (draw.type == MODEL_DOPPELGANGER_SLIME_CHIP)
            {
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_FIRE_FLAME_GHOST)
            {
                if (draw.action == MONSTER01_DIE)
                {
                    Vector(1.0f, 0.6f, 0.9f, b->BodyLight);
                    draw.renderShadow = false;
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, -2, draw.blendLight, draw.blendU,
                                  draw.blendV, draw.hiddenMesh);
                    b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV);
                }
                else
                {
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                    vec3_t vOriBodyLight;
                    VectorCopy(b->BodyLight, vOriBodyLight);
                    Vector(1.0f, 0.6f, 0.5f, b->BodyLight);
                    b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                                  draw.blendLight, draw.blendU, draw.blendV);
                    VectorCopy(vOriBodyLight, b->BodyLight);
                }
            }
            else if (draw.type == MODEL_EFFECT_UMBRELLA_GOLD)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV);
            }
            else if (draw.type == MODEL_EFFECT_SD_AURA)
            {
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                              draw.blendU, WorldTime * 0.0006f);
            }
            else if (draw.type == MODEL_UNITEDMARKETPLACE_CHRISTIN)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_UNITEDMARKETPLACE_RAUL)
            {
                b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(1, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(2, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(3, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(4, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV);
                b->RenderMesh(5, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(6, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(7, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(8, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(9, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(10, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(11, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(12, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(13, RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV);
                b->RenderMesh(14, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV);
            }
            else if (draw.type == MODEL_UNITEDMARKETPLACE_JULIA)
            {
                vec3_t vRelativePos, vWorldPos, Light;
                Vector(0.f, 0.f, 0.f, vRelativePos);
                float fScale = 0.0f;

                Vector(0.f, 0.f, 0.f, vRelativePos);
                Vector(0.f, 0.f, 0.f, vWorldPos);
                Vector(1.0f, 1.0f, 1.0f, Light);
                b->TransformPosition(draw.bones[71], vRelativePos, vWorldPos, true);
                CreateSprite(BITMAP_LIGHT, vWorldPos, 3.0f, Light, o);

                vec3_t vLight;
                float fLight = (sinf(WorldTime * 0.001f) + 1.0f) * 0.5f * 0.9f + 0.4f;
                Vector(1.0f * fLight, 1.0f * fLight, 1.0f * fLight, vLight);
                CreateSprite(BITMAP_FLARE, vWorldPos, 1.5f, vLight, o);

                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_TERSIA)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, draw.alpha * 0.4f, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV);
            }
            else if (draw.type == MODEL_LUCKYITEM_NPC)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderMesh(0, RENDER_BRIGHT | RENDER_CHROME, draw.alpha * 0.4f, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV);
            }
            else if (draw.type == MODEL_KARUTAN_NPC_VOLVO)
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
                float fLumi = (sinf(WorldTime * 0.001f) + 1.0f) * 0.45f + 0.1f;
                b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, fLumi, draw.blendU,
                              draw.blendV, BITMAP_VOLO_SKIN_EFFECT);
            }
            else if (draw.type == MODEL_WOLF_HEAD_EFFECT2)
            {
                float _BlendLight = draw.blendLight;
                Vector(0.4f, 0.4f, 0.6f, draw.light);
                VectorCopy(draw.light, b->BodyLight);
                VectorScale(b->BodyLight, 0.3f, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              _BlendLight * 0.5f, draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_SHOCKWAVE01)
            {
                if (o->SubType == 1)
                {
                    VectorCopy(draw.light, b->BodyLight);
                    VectorScale(b->BodyLight, 5.0f, b->BodyLight);
                    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                                  draw.blendU, draw.blendV, BITMAP_DAMAGE2);
                }
                else if (o->SubType == 2)
                {
                    VectorCopy(draw.light, b->BodyLight);
                    VectorScale(b->BodyLight, 15.0f, b->BodyLight);
                    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                                  draw.blendU, draw.blendV);
                    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                                  draw.blendU, draw.blendV);
                }
                else if (o->SubType == 3)
                {
                    VectorCopy(draw.light, b->BodyLight);
                    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                                  draw.blendU, draw.blendV, BITMAP_DAMAGE2);
                    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                                  draw.blendU, draw.blendV, BITMAP_DAMAGE2);
                }
                else if (o->SubType == 4 || o->SubType == 5)
                {
                    VectorCopy(draw.light, b->BodyLight);
                    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                                  draw.blendU, draw.blendV, BITMAP_DAMAGE2);
                }
                else
                {
                    VectorCopy(draw.light, b->BodyLight);
                    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                                  draw.blendU, draw.blendV);
                    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                                  draw.blendU, draw.blendV);
                }
            }
            else if (draw.type == MODEL_SHOCKWAVE02)
            {
                VectorCopy(draw.light, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_SHOCKWAVE_SPIN01)
            {
                VectorCopy(draw.light, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh,
                              BITMAP_DAMAGE1);
            }
            else if (draw.type == MODEL_DRAGON_KICK_DUMMY ||
                     draw.type == MODEL_DOWN_ATTACK_DUMMY_L ||
                     draw.type == MODEL_DOWN_ATTACK_DUMMY_R || draw.type == MODEL_WOLF_HEAD_EFFECT)
            {
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_WINDFOCE)
            {
                VectorCopy(draw.light, b->BodyLight);
                VectorScale(b->BodyLight, 6.0f, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_WINDFOCE_MIRROR)
            {
                VectorCopy(draw.light, b->BodyLight);
                VectorScale(b->BodyLight, 6.0f, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight, draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_SHOCKWAVE_GROUND01)
            {
                if (o->SubType == 1)
                {
                    VectorCopy(draw.light, b->BodyLight);
                    VectorScale(b->BodyLight, 6.0f, b->BodyLight);
                    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                                  draw.blendU, draw.blendV, BITMAP_KWAVE2);
                }
                else
                {
                    VectorCopy(draw.light, b->BodyLight);
                    VectorScale(b->BodyLight, 6.0f, b->BodyLight);
                    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                                  draw.blendU, draw.blendV);
                }
            }
            else if (draw.type == MODEL_DRAGON_LOWER_DUMMY)
            {
                Vector(1.0f, 0.8f, 0.2f, draw.light);
                VectorCopy(draw.light, b->BodyLight);
                VectorScale(b->BodyLight, draw.alpha, b->BodyLight);
                // grandmark2.jpg
                b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                              draw.blendU, -(int)WorldTime % 2000 * 0.0001f);
                b->RenderMesh(2, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 2, draw.blendLight,
                              draw.blendU, -(int)WorldTime % 2000 * 0.0001f);

                Vector(1.0f, 0.2f, 0.1f, draw.light);
                VectorCopy(draw.light, b->BodyLight);
                VectorScale(b->BodyLight, draw.alpha, b->BodyLight);
                // lines2.jpg
                b->RenderMesh(1, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 1, draw.blendLight,
                              draw.blendU, -(int)WorldTime % 2000 * 0.0001f);
            }
            else if (draw.type == MODEL_VOLCANO_OF_MONK)
            {
                if (o->SubType == 1)
                {
                    VectorCopy(draw.light, b->BodyLight);
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);

                    b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, 0, draw.blendLight,
                                  draw.blendU, draw.blendV, BITMAP_VOLCANO_CORE);
                }
            }
            else if (draw.type == MODEL_VOLCANO_STONE)
            {
                VectorCopy(draw.light, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            else if (draw.type == MODEL_SWORD_FORCE)
            {
                if (o->SubType == 2 || o->SubType == 3)
                {
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh, BITMAP_KNIGHTST_BLUE);
                }
                else
                {
                    b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                                  draw.blendU, draw.blendV, draw.hiddenMesh);
                }
            }
            else
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, draw.blendMesh, draw.blendLight,
                              draw.blendU, draw.blendV, draw.hiddenMesh);
            }
            if (g_isCharacterBuff(o, eDeBuff_Freeze))
            {
                b->RenderBody(RENDER_TEXTURE, draw.alpha, -2, 1.f, draw.blendU, draw.blendV,
                              draw.hiddenMesh);
            }
            else if (g_isCharacterBuff(o, eDeBuff_BlowOfDestruction))
            {
                Vector(0.3f, 0.5f, 1.f, b->BodyLight);
                b->RenderBody(RENDER_TEXTURE, draw.alpha, -2, 1.f, draw.blendU, draw.blendV,
                              draw.hiddenMesh);
            }

            if (draw.type == MODEL_HYDRA)
            {
                b->BeginRender(draw.alpha);
                float Light = sinf(WorldTime * 0.002f) * 0.3f + 0.5f;
                b->RenderMesh(0, RENDER_TEXTURE, draw.alpha, 0, Light, draw.blendU, draw.blendV,
                              b->IndexTexture[6]);
                float TexCoord = (float)((int)WorldTime % 100) * 0.01f;
                b->RenderMesh(3, RENDER_CHROME | RENDER_BRIGHT, draw.alpha, 3, Light, draw.blendU,
                              -TexCoord);
                b->EndRender();
            }
            if (draw.type == MODEL_NPC_ARCHANGEL_MESSENGER || draw.type == MODEL_NPC_ARCHANGEL)
            {
                b->RenderMesh(0, RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, draw.blendMesh,
                              draw.blendLight);
            }

            if (draw.type == MODEL_ARROW_TANKER_HIT || draw.type == MODEL_ARROW_TANKER)
            {
                b->BodyLight[0] = 1.0f;
                b->BodyLight[1] = 1.0f;
                b->BodyLight[2] = 1.0f;

                b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, draw.alpha, -2, 1.f, draw.blendU,
                              draw.blendV, draw.hiddenMesh);

                vec3_t p1, p2, glowLight;
                BMD *b = &Models[draw.type];

                Vector(1.0f, 1.0f, 1.0f, glowLight);
                Vector(0.0f, 1.5f, 0.0f, p1);
                b->TransformPosition(draw.bones[2], p1, p2);
                Vector(1.0f, 0.4f, 0.0f, glowLight);

                for (int i = 0; i < 10; i++)
                {
                    Vector(1.0f - (i * 0.1f), 0.4f - (i * 0.04f), 0.0f, glowLight);
                    Vector(15.0f * i, 1.5f, 0.0f, p1);
                    b->TransformPosition(draw.bones[1], p1, p2);
                    CreateSprite(BITMAP_SPARK + 1, p2, 3.0f + (i * 0.9f), glowLight, o);
                }
            }
        }
    }
}

void SessionRenderUnit::Draw_RenderObject_AfterCharacter(const ObjectDrawInput &draw,
                                                         bool translate, int select,
                                                         int extraMonster)
{
    if (EditFlag == EDIT_NONE && draw.hiddenMesh == -2)
        return;
    TheMapProcess().RenderAfterObjectMesh(draw, &Models[draw.type]);
}

// Helper: Handle item falling animation

// Helper: Handle item on ground (set angle and camera rotation)

// Render dropped items and fall animation and camera rotation for items on the ground
// OMF-00605
// OMF-00606
// OMF-00633

void MoveCharacter(CHARACTER *c, OBJECT *o);

void SessionRenderUnit::PrepareGaionSwordPose(const OBJECT &object, vec3_t *positions)
{
    auto &model = Models[object.Type];
    auto *pose = drawPoses_.Allocate(model.NumBones);
    model.BodyHeight = 0.f;
    model.ContrastEnable = object.ContrastEnable;
    model.BodyScale = object.Scale;
    model.CurrentAction = object.CurrentAction;
    VectorCopy(object.Position, model.BodyOrigin);
    BodyLight(&object, &model);
    vec3_t zero{};
    OBB_t bounds{};
    if (object.ChromeEnable)
    {
        const auto &owner = *object.Owner;
        constexpr int sockets[]{4, 2, 8, 10, 6};
        vec34_t parent;
        memcpy(
            parent,
            owner
                .BoneTransform[sockets[object.Type - MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_]],
            sizeof(parent));
        model.BodyScale = owner.Scale;
        for (int axis = 0; axis < 3; ++axis)
            parent[axis][3] *= owner.Scale;
        model.Animation(pose, 0.f, 0.f, 0, zero, zero, false, false);
        for (int bone = 0; bone < model.NumBones; ++bone)
        {
            vec34_t world;
            R_ConcatTransforms(parent, pose[bone], world);
            for (int axis = 0; axis < 3; ++axis)
                world[axis][3] += owner.Position[axis];
            memcpy(pose[bone], world, sizeof(world));
        }
        model.Transform(pose, zero, zero, &bounds, false);
    }
    else
    {
        model.Animation(pose, object.AnimationFrame, object.PriorAnimationFrame, object.PriorAction,
                        object.Angle, object.HeadAngle, false, true);
        model.Transform(pose, object.BoundingBoxMin, object.BoundingBoxMax, &bounds, false);
    }
    for (int bone = 0; bone < model.NumBones; ++bone)
        Vector(pose[bone][0][3], pose[bone][1][3], pose[bone][2][3], positions[bone]);
}

void CBoneManager::Forget(OBJECT *object)
{
    const auto found = characters_.find(object);
    if (found == characters_.end())
        return;
    if (found->second.binding)
        found->second.binding->source.reset();
    characters_.erase(found);
}

void CBoneManager::UnregisterAll()
{
    for (auto &[object, admission] : characters_)
        if (admission.binding)
            admission.binding->source.reset();
    characters_.clear();
}
