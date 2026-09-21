#include "render/ModelResources.h"
#include "I18N/All.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationDiagnostics.h"
#include "app/ApplicationKeeper.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "data/ResourceData.h"
#include "data/WorldData.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/EffectsUpdate.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Quests.h"
#include "domain/Shop.h"
#include "domain/WorldPhysics.h"
#include "domain/WorldSimulation.h"
#include "render/Effects.h"
#include "render/FrameTape.h"
#include "render/ModelGeometry.h"
#include "render/Sprites.h"
#include "render/Terrain.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionNetwork.h"
#include "session/SessionPresentation.h"
#include "session/SessionRender.h"
#include "session/SessionUi.h"
#include "session/SessionWorkspace.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Dialogs/DialogsRender.h"
#include "ui/features/Hud/HudRender.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/Shell/ShellLogic.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/features/Social/SocialRender.h"
#include "ui/features/World/WorldRender.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"

BmdSharedAsset::BmdSharedAsset()
    : poseIdentity([] {
          // Resource identity is assigned once at decode, never during recording.
          static std::atomic<std::uint64_t> next{1};
          return next.fetch_add(1, std::memory_order_relaxed);
      }())
{
}

BmdSharedAsset::~BmdSharedAsset()
{
    for (int bone = 0; bones != nullptr && bone < boneCount; ++bone)
    {
        if (bones[bone].Dummy || bones[bone].BoneMatrixes == nullptr)
            continue;
        for (int action = 0; action < actionCount; ++action)
        {
            delete[] bones[bone].BoneMatrixes[action].Position;
            delete[] bones[bone].BoneMatrixes[action].Rotation;
            delete[] bones[bone].BoneMatrixes[action].Quaternion;
        }
        delete[] bones[bone].BoneMatrixes;
    }
    for (int action = 0; actions != nullptr && action < actionCount; ++action)
    {
        if (actions[action].LockPositions)
            delete[] actions[action].Positions;
    }
    for (int mesh = 0; meshes != nullptr && mesh < meshCount; ++mesh)
    {
        delete[] meshes[mesh].Vertices;
        delete[] meshes[mesh].Normals;
        delete[] meshes[mesh].TexCoords;
        delete[] meshes[mesh].Triangles;
        delete meshes[mesh].m_csTScript;
    }
    delete[] meshes;
    delete[] bones;
    delete[] actions;
    delete[] textures;
}

namespace
{
using Error = BmdSharedAsset::Error;
struct Reader final
{
    std::span<const unsigned char> bytes;
    void Copy(void *target, std::size_t count)
    {
        if (count > bytes.size())
            throw Error::Size;
        std::memcpy(target, bytes.data(), count);
        bytes = bytes.subspan(count);
    }
    template <class T> T Read()
    {
        T value;
        Copy(&value, sizeof(value));
        return value;
    }
    template <class T> T *Array(short count)
    {
        if (count < 0 || std::size_t(count) > bytes.size() / sizeof(T))
            throw Error::Size;
        auto values = std::make_unique<T[]>(count);
        Copy(values.get(), std::size_t(count) * sizeof(T));
        if constexpr (std::is_same_v<T, vec3_t>)
            for (int i = 0; i < count; ++i)
                for (const auto value : values[i])
                    if (!std::isfinite(value))
                        throw Error::Values;
        return values.release();
    }
    void Name(char *target)
    {
        constexpr std::size_t NameSize = 32;
        Copy(target, NameSize);
        if (std::memchr(target, 0, NameSize) == nullptr)
            throw Error::Values;
    }
};

std::vector<unsigned char> ReadPayload(const std::filesystem::path &path, char &version)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw Error::Open;
    const std::streamoff size = input.tellg();
    constexpr std::streamoff HeaderSize = 4;
    if (size < HeaderSize || size > (std::numeric_limits<int>::max)())
        throw Error::Size;
    std::vector<unsigned char> file(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char *>(file.data()), size))
        throw Error::Read;
    if (std::memcmp(file.data(), "BMD", 3) != 0)
        throw Error::Header;
    version = static_cast<char>(file[3]);
    Reader reader{std::span(file).subspan(HeaderSize)};
    if (version == 0x0A)
        return {reader.bytes.begin(), reader.bytes.end()};
    if (version != 0x0C)
        throw Error::Header;
    const auto encodedSize = reader.Read<std::int32_t>();
    if (encodedSize < 0 || std::size_t(encodedSize) != reader.bytes.size())
        throw Error::Size;
    std::vector<unsigned char> decoded(encodedSize);
    MapFileDecrypt(decoded.data(), const_cast<unsigned char *>(reader.bytes.data()), encodedSize);
    return decoded;
}

constexpr short DeletedExportNode = -8888;

bool IsExportPlaceholder(const Triangle_t2 &triangle, const Mesh_t &mesh)
{
    const auto &vertex = mesh.Vertices[triangle.VertexIndex[0]];
    const auto &normal = mesh.Normals[triangle.NormalIndex[0]];
    const auto zero = [](const vec3_t &value) {
        return value[0] == 0 && value[1] == 0 && value[2] == 0;
    };
    return triangle.VertexIndex[0] == triangle.VertexIndex[1] &&
           triangle.VertexIndex[0] == triangle.VertexIndex[2] &&
           triangle.NormalIndex[0] == triangle.NormalIndex[1] &&
           triangle.NormalIndex[0] == triangle.NormalIndex[2] && vertex.Node == DeletedExportNode &&
           normal.Node == DeletedExportNode && zero(vertex.Position) && zero(normal.Normal);
}

template <class T> std::vector<short> CompactExportRecords(T *&records, short &count)
{
    std::vector<short> indices(count, -1);
    short kept = 0;
    for (short i = 0; i < count; ++i)
    {
        if (records[i].Node != DeletedExportNode)
        {
            indices[i] = kept++;
            continue;
        }
        const auto *value = [&]() {
            if constexpr (std::is_same_v<T, Vertex_t>)
                return records[i].Position;
            else
                return records[i].Normal;
        }();
        if (value[0] != 0 || value[1] != 0 || value[2] != 0)
            throw Error::Values;
    }
    if (kept == count)
        return indices;
    auto compact = std::make_unique<T[]>(kept);
    for (short i = 0; i < count; ++i)
        if (indices[i] >= 0)
            compact[indices[i]] = records[i];
    delete[] records;
    records = compact.release();
    count = kept;
    return indices;
}

void CompactExportMesh(Mesh_t &mesh, short keptTriangles)
{
    const auto vertices = CompactExportRecords(mesh.Vertices, mesh.NumVertices);
    const auto normals = CompactExportRecords(mesh.Normals, mesh.NumNormals);
    for (int i = 0; i < mesh.NumNormals; ++i)
    {
        auto &boundVertex = mesh.Normals[i].BindVertex;
        if (boundVertex >= 0 && std::size_t(boundVertex) < vertices.size())
            boundVertex = vertices[boundVertex];
    }
    auto triangles = std::make_unique<Triangle_t[]>(keptTriangles);
    for (short i = 0; i < keptTriangles; ++i)
    {
        triangles[i] = mesh.Triangles[i];
        for (int corner = 0; corner < 4; ++corner)
        {
            auto &vertex = triangles[i].VertexIndex[corner];
            auto &normal = triangles[i].NormalIndex[corner];
            vertex = vertices[vertex];
            normal = normals[normal];
            if (vertex < 0 || normal < 0)
                throw Error::Values;
        }
    }
    delete[] mesh.Triangles;
    mesh.Triangles = triangles.release();
    mesh.NumTriangles = keptTriangles;
}

void ReadTriangles(Reader &reader, Mesh_t &mesh, BmdSharedAsset &data)
{
    if (std::size_t(mesh.NumTriangles) > reader.bytes.size() / sizeof(Triangle_t2))
        throw Error::Size;
    mesh.Triangles = new Triangle_t[mesh.NumTriangles]();
    short kept = 0;
    for (int i = 0; i < mesh.NumTriangles; ++i)
    {
        const auto disk = reader.Read<Triangle_t2>();
        if (disk.Polygon != 3)
            throw Error::Values;
        for (int corner = 0; corner < disk.Polygon; ++corner)
            if (disk.VertexIndex[corner] < 0 || disk.VertexIndex[corner] >= mesh.NumVertices ||
                disk.NormalIndex[corner] < 0 || disk.NormalIndex[corner] >= mesh.NumNormals ||
                disk.TexCoordIndex[corner] < 0 || disk.TexCoordIndex[corner] >= mesh.NumTexCoords)
                throw Error::Values;
        if (IsExportPlaceholder(disk, mesh))
        {
            ++data.droppedExportTriangles;
            continue;
        }
        auto &triangle = mesh.Triangles[kept++];
        triangle.Polygon = disk.Polygon;
        std::copy_n(disk.VertexIndex, 4, triangle.VertexIndex);
        std::copy_n(disk.NormalIndex, 4, triangle.NormalIndex);
        std::copy_n(disk.TexCoordIndex, 4, triangle.TexCoordIndex);
        // Legacy collision forms corner 3 even for a triangle.
        triangle.VertexIndex[3] = triangle.VertexIndex[0];
        triangle.NormalIndex[3] = triangle.NormalIndex[0];
        triangle.TexCoordIndex[3] = triangle.TexCoordIndex[0];
        std::fill_n(triangle.EdgeTriangleIndex, 4, short(-1));
    }
    if (kept != mesh.NumTriangles)
        CompactExportMesh(mesh, kept);
}

void ReadMeshArrays(Reader &reader, Mesh_t &mesh)
{
    mesh.Vertices = reader.Array<Vertex_t>(mesh.NumVertices);
    mesh.Normals = reader.Array<Normal_t>(mesh.NumNormals);
    mesh.TexCoords = reader.Array<TexCoord_t>(mesh.NumTexCoords);
}

void ReadMesh(Reader &reader, BmdSharedAsset &data, int index)
{
    auto &mesh = data.meshes[index];
    mesh.NumVertices = reader.Read<short>();
    mesh.NumNormals = reader.Read<short>();
    mesh.NumTexCoords = reader.Read<short>();
    mesh.NumTriangles = reader.Read<short>();
    mesh.Texture = reader.Read<short>();
    mesh.NoneBlendMesh = false;
    if (mesh.NumVertices < 0 || mesh.NumVertices > MAX_VERTICES || mesh.NumNormals < 0 ||
        mesh.NumNormals > MAX_VERTICES || mesh.NumTexCoords < 0 || mesh.NumTriangles < 0 ||
        mesh.Texture < 0 || mesh.Texture >= data.meshCount)
        throw Error::Values;
    ReadMeshArrays(reader, mesh);
    ReadTriangles(reader, mesh, data);
    reader.Name(data.textures[index].FileName);
    TextureScriptParsing script;
    if (script.parsingTScriptA(data.textures[index].FileName))
    {
        mesh.m_csTScript = new TextureScript;
        mesh.m_csTScript->setScript(script);
    }
}

void ReadActions(Reader &reader, BmdSharedAsset &data)
{
    for (int i = 0; i < data.actionCount; ++i)
    {
        auto &action = data.actions[i];
        action.NumAnimationKeys = reader.Read<short>();
        const auto lock = reader.Read<unsigned char>();
        if (action.NumAnimationKeys <= 0 || lock > 1)
            throw Error::Values;
        action.LockPositions = lock != 0;
        if (action.LockPositions)
            action.Positions = reader.Array<vec3_t>(action.NumAnimationKeys);
    }
}

void ReadBones(Reader &reader, BmdSharedAsset &data)
{
    for (int i = 0; i < data.boneCount; ++i)
    {
        auto &bone = data.bones[i];
        bone.Dummy = reader.Read<char>();
        if (bone.Dummy)
            continue;
        reader.Name(bone.Name);
        bone.Parent = reader.Read<short>();
        if (bone.Parent < -1 || bone.Parent >= data.boneCount || bone.Parent == i)
            throw Error::Values;
        bone.BoneMatrixes = new BoneMatrix_t[data.actionCount]();
        for (int j = 0; j < data.actionCount; ++j)
        {
            auto &matrix = bone.BoneMatrixes[j];
            const auto count = data.actions[j].NumAnimationKeys;
            matrix.Position = reader.Array<vec3_t>(count);
            matrix.Rotation = reader.Array<vec3_t>(count);
            matrix.Quaternion = new vec4_t[count];
            for (int key = 0; key < count; ++key)
                AngleQuaternion(matrix.Rotation[key], matrix.Quaternion[key]);
        }
    }
}

void PrepareGrandSoulRestCloth(BmdSharedAsset &data, const std::filesystem::path &path)
{
    // OpenBasicData formerly constructed a temporary physics object to rewrite this
    // asset. Prepare its authored rest grid before publishing indexed/GPU geometry.
    if (_wcsicmp(path.filename().c_str(), L"t_PantMale19.bmd") != 0)
        return;
    constexpr int clothMesh = 2;
    constexpr int clothBone = 17;
    constexpr int columns = 5, rows = 8;
    constexpr float width = 45.f, height = 85.f;
    if (data.meshCount <= clothMesh || data.boneCount <= clothBone || data.bones[clothBone].Dummy)
        throw Error::Values;
    auto &mesh = data.meshes[clothMesh];
    if (mesh.NumVertices < columns * rows || mesh.NumTexCoords < columns * rows ||
        mesh.NumNormals == 0 || mesh.NumTriangles > 2 * (columns - 1) * (rows - 1))
        throw Error::Values;
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < columns; ++x)
        {
            auto &vertex = mesh.Vertices[y * columns + x];
            vertex.Node = clothBone;
            Vector(-height * y / (rows - 1), 0.f, width * x / (columns - 1) - width * 0.5f,
                   vertex.Position);
        }
    Render::Models::PrepareClothGridTopology(mesh, columns, rows);
}

void CheckMeshValues(const BmdSharedAsset &data)
{
    for (int meshIndex = 0; meshIndex < data.meshCount; ++meshIndex)
    {
        const auto &mesh = data.meshes[meshIndex];
        for (int i = 0; i < mesh.NumVertices; ++i)
        {
            if (mesh.Vertices[i].Node < 0 || mesh.Vertices[i].Node >= data.boneCount ||
                data.bones[mesh.Vertices[i].Node].Dummy)
                throw Error::Values;
            for (float value : mesh.Vertices[i].Position)
                if (!std::isfinite(value))
                    throw Error::Values;
        }
        for (int i = 0; i < mesh.NumNormals; ++i)
        {
            if (mesh.Normals[i].Node < 0 || mesh.Normals[i].Node >= data.boneCount ||
                data.bones[mesh.Normals[i].Node].Dummy)
                throw Error::Values;
            for (float value : mesh.Normals[i].Normal)
                if (!std::isfinite(value))
                    throw Error::Values;
        }
        for (int i = 0; i < mesh.NumTexCoords; ++i)
            if (!std::isfinite(mesh.TexCoords[i].TexCoordU) ||
                !std::isfinite(mesh.TexCoords[i].TexCoordV))
                throw Error::Values;
    }
}

bool HasInvariantLocalPose(const BmdSharedAsset &data)
{
    if (data.actionCount == 0 || data.boneCount == 0)
        return false;
    for (int boneIndex = 0; boneIndex < data.boneCount; ++boneIndex)
    {
        const auto &bone = data.bones[boneIndex];
        if (bone.Dummy)
            continue;
        const auto &first = bone.BoneMatrixes[0];
        for (int action = 0; action < data.actionCount; ++action)
        {
            const auto &matrix = bone.BoneMatrixes[action];
            for (int key = 0; key < data.actions[action].NumAnimationKeys; ++key)
                if (std::memcmp(first.Position[0], matrix.Position[key], sizeof(vec3_t)) != 0 ||
                    std::memcmp(first.Quaternion[0], matrix.Quaternion[key], sizeof(vec4_t)) != 0)
                    return false;
        }
    }
    return true;
}

void PrepareInvariantLocalPose(BmdSharedAsset &data)
{
    if (!HasInvariantLocalPose(data))
        return;
    data.invariantLocalPose.resize(data.boneCount);
    for (int boneIndex = 0; boneIndex < data.boneCount; ++boneIndex)
    {
        const auto &bone = data.bones[boneIndex];
        if (bone.Dummy)
            continue;
        const auto &key = bone.BoneMatrixes[0];
        float matrix[3][4];
        QuaternionMatrix(key.Quaternion[0], matrix);
        for (int axis = 0; axis < 3; ++axis)
            matrix[axis][3] = key.Position[0][axis];
        static_assert(sizeof(matrix) == sizeof(RenderTapeBoneMatrix));
        std::memcpy(&data.invariantLocalPose[boneIndex], matrix, sizeof(matrix));
    }
}

} // namespace

std::shared_ptr<BmdSharedAsset> BmdSharedAsset::Load(const std::filesystem::path &path,
                                                     Error &error) noexcept
{
    error = Error::None;
    try
    {
        auto data = std::make_shared<BmdSharedAsset>();
        auto payload = ReadPayload(path, data->version);
        Reader reader{payload};
        // Model names occupy 32 bytes on disk; the runtime has a 64-byte zeroed field.
        reader.Copy(data->name, 32);
        data->meshCount = reader.Read<short>();
        data->boneCount = reader.Read<short>();
        data->actionCount = reader.Read<short>();
        if (data->meshCount < 0 || data->meshCount > MAX_MESH || data->boneCount < 0 ||
            data->boneCount > MAX_BONES || data->actionCount < 0)
            throw Error::Values;
        data->meshes = new Mesh_t[(std::max)(1, int(data->meshCount))]();
        data->bones = new Bone_t[(std::max)(1, int(data->boneCount))]();
        data->actions = new Action_t[(std::max)(1, int(data->actionCount))]();
        data->textures = new Texture_t[(std::max)(1, int(data->meshCount))]();
        for (int i = 0; i < data->meshCount; ++i)
            ReadMesh(reader, *data, i);
        ReadActions(reader, *data);
        ReadBones(reader, *data);
        CheckMeshValues(*data);
        PrepareGrandSoulRestCloth(*data, path);
        if (!reader.bytes.empty())
            throw Error::Size;
        PrepareInvariantLocalPose(*data);
        return data;
    }
    catch (Error failure)
    {
        error = failure;
    }
    catch (const std::bad_alloc &)
    {
        error = Error::Allocation;
    }
    catch (...)
    {
        error = Error::Read;
    }
    return {};
}

const wchar_t *BmdSharedAsset::ErrorText(Error error) noexcept
{
    switch (error)
    {
    case Error::None:
        return L"No model error";
    case Error::Open:
        return L"Cannot open required model";
    case Error::Read:
        return L"Cannot read model";
    case Error::Size:
        return L"Invalid model size";
    case Error::Header:
        return L"Unsupported model header";
    case Error::Values:
        return L"Invalid model data";
    case Error::Allocation:
        return L"Model allocation failed";
    }
    return L"Unknown model error";
}

SessionModelLoader::SessionModelLoader(SessionKeeper &keeper) noexcept
    : SessionLegacyCalls(keeper), Models(keeper.ModelPoolObject()),
      g_ErrorReport(keeper.ErrorReport())
{
}

bool SessionModelLoader::AccessModel(int type, const wchar_t *directory, const wchar_t *fileName,
                                     int index)
{
    wchar_t name[64];
    if (index == -1)
        mu_swprintf(name, L"%ls.bmd", fileName);
    else
        mu_swprintf(name, L"%ls%02d.bmd", fileName, index);
    auto &model = Models[type];
    model.m_iBMDSeqID = type;
    const wchar_t *reason = nullptr;
    const bool npc = type >= MODEL_NPC_BEGIN && type < MODEL_NPC_END;
    const bool monster = type >= MODEL_MONSTER01 && type < MODEL_MONSTER01 + MONSTER_MODEL_COUNT;
    const auto *map = sessionKeeper_.WorldContextDefinition();
    const bool rigid = (map && WorldPrimaryModel::UsesRigidGeometry(map->BehaviorMap(), type)) ||
                       Render::Effects::RequestsRigidGeometry(type);
    const auto asset = model.PrepareSharedAsset(
        std::filesystem::path(directory) / name, reason, ModelResourceRequirements::Actions(type),
        ModelResourceRequirements::RequiredBones(type, monster),
        std::max(ModelResourceRequirements::Meshes(type), monster ? 1 : 0), rigid);
    if (asset && (!npc || asset->boneCount > 0 || asset->meshCount > 0) &&
        model.BindSharedAsset(asset))
        return true;
    // Basic/runtime character failures belong to this session, never the app window.
    if ((type >= MODEL_NPC_BEGIN && type < MODEL_NPC_END) ||
        ModelResourceRequirements::Actions(type) != 0 ||
        ModelResourceRequirements::Bones(type) != 0 ||
        ModelResourceRequirements::Meshes(type) != 0 || wcscmp(fileName, L"Monster") == 0 ||
        wcscmp(fileName, L"Player") == 0 || wcscmp(fileName, L"PlayerTest") == 0 ||
        wcscmp(fileName, L"Angel") == 0)
    {
        g_ErrorReport.Write(L"Required model failed: %ls%ls\r\n", directory, name);
        sessionKeeper_.WorldUnit()->FinishLoad(false);
        throw std::runtime_error("Required character model failed");
    }
    return false;
}

std::uint32_t SessionModelLoader::LoadTexture(const char *filename, const wchar_t *subFolder,
                                              LegacyTextureWrap wrap, LegacyTextureFilter filter)
{
    if (std::string_view(filename).starts_with("hid"))
        return BITMAP_HIDE;
    wchar_t wideName[32]{};
    if (!MultiByteToWideChar(CP_UTF8, 0, filename, -1, wideName, 32))
        return BITMAP_UNKNOWN;
    const auto path = std::filesystem::path(L"Data") / subFolder / wideName;
    if (_wcsicmp(path.extension().c_str(), L".tga") == 0)
        filter = LegacyTextureFilter::Nearest;
    auto texture = Bitmaps.LoadImage(path.c_str(), g_ErrorReport, filter, wrap);
    // Legacy basic/runtime art still uses its established common texture namespace.
    // World candidates resolve every shared texture through an explicit authored path.
    if (texture == BITMAP_UNKNOWN)
        texture = Bitmaps.RetainTextureByName(wideName).value_or(BITMAP_UNKNOWN);
    if (texture == BITMAP_UNKNOWN)
        g_ErrorReport.Write(L"Required model texture failed: %ls\r\n", path.c_str());
    return texture;
}

bool SessionModelLoader::OpenTexture(int slot, const wchar_t *subFolder, LegacyTextureWrap wrap,
                                     LegacyTextureFilter filter, bool required)
{
    auto &model = Models[slot];
    bool complete = true;
    for (int mesh = 0; mesh < model.NumMeshs; ++mesh)
    {
        const auto texture = LoadTexture(model.Textures[mesh].FileName, subFolder, wrap, filter);
        if (texture == BITMAP_UNKNOWN)
        {
            if (required)
            {
                sessionKeeper_.WorldUnit()->FinishLoad(false);
                throw std::runtime_error("Required character texture failed");
            }
            complete = false;
            continue;
        }
        const auto previous = model.IndexTexture[mesh];
        model.IndexTexture[mesh] = texture;
        if (previous != BITMAP_UNKNOWN && previous != BITMAP_HIDE)
            sessionKeeper_.TextureNamespace().Unload(previous);
    }
    model.PrepareRigidInstanceMeshes();
    return complete;
}

SessionModelPool::SessionModelPool(SessionKeeper &keeper) noexcept : sessionKeeper_(keeper)
{
}

SessionModelPool::~SessionModelPool()
{
    Reset();
}

bool SessionModelPool::Allocate()
{
    if (!models_.empty())
    {
        return true;
    }

    try
    {
        models_.resize(MAX_MODELS);
    }
    catch (...)
    {
        return false;
    }
    return true;
}

BMD &SessionModelPool::operator[](int model)
{
    if (models_.empty() && !Allocate())
    {
        throw std::bad_alloc();
    }

    std::unique_ptr<BMD> &entry = models_[static_cast<std::size_t>(model)];
    if (entry == nullptr)
    {
        entry = std::make_unique<BMD>(sessionKeeper_);
        entry->m_iBMDSeqID = model;
        ++loadedModelCount_;
    }
    return *entry;
}

bool SessionModelPool::IsAllocated() const noexcept
{
    return !models_.empty();
}

BMD *SessionModelPool::Find(int model) noexcept
{
    if (models_.empty())
    {
        return nullptr;
    }
    return models_[static_cast<std::size_t>(model)].get();
}

std::size_t SessionModelPool::LoadedModelCount() const noexcept
{
    return loadedModelCount_;
}

std::size_t SessionModelPool::StorageBytes() const noexcept
{
    std::size_t bytes = models_.capacity() * sizeof(std::unique_ptr<BMD>);
    for (const std::unique_ptr<BMD> &model : models_)
    {
        if (model != nullptr)
        {
            bytes += sizeof(BMD) + model->MutableOverlayStorageBytes();
        }
    }
    return bytes;
}

std::size_t SessionModelPool::RenderTapeStorageBytes(
    SharedAllocationCounter &allocations) const noexcept
{
    std::size_t bytes = 0;
    for (const std::unique_ptr<BMD> &model : models_)
    {
        if (model != nullptr)
        {
            bytes += model->RenderTapeStorageBytes(allocations);
        }
    }
    return bytes;
}

void SessionModelPool::Reset() noexcept
{
    models_.clear();
    loadedModelCount_ = 0;
}

namespace
{
struct SharedTextureReference
{
    const wchar_t *folder;
    const wchar_t *model;
    const wchar_t *texture;
    const wchar_t *source;
};
constexpr SharedTextureReference SharedTextures[]{
    {L"Object3", L"Object102.bmd", L"sword14.jpg", L"Item"},
    {L"Object3", L"Object102.bmd", L"sword14.tga", L"Item"},
    {L"Object3", L"Object102.bmd", L"sword11.jpg", L"Item"},
    {L"Object3", L"Object102.bmd", L"sword11.tga", L"Item"},
    {L"Object3", L"Object102.bmd", L"sword06.jpg", L"Item"},
    {L"Object3", L"Object102.bmd", L"hound_sword1.jpg", L"Item"},
    {L"Object3", L"Object102.bmd", L"hound_shield1.tga", L"Item"},
    {L"Object3", L"Object102.bmd", L"shield05.jpg", L"Item"},
    {L"Object3", L"Object102.bmd", L"sword03.jpg", L"Item"},
    {L"Object3", L"Object105.bmd", L"sword03.jpg", L"Item"},
    {L"Object52", L"Object103.bmd", L"sword02.jpg", L"Item"},
    {L"Object52", L"Object114.bmd", L"shield02.jpg", L"Item"},
    {L"Object52", L"Object114.bmd", L"shield03.jpg", L"Item"},
    {L"Object52", L"Object114.bmd", L"sword08.jpg", L"Item"},
    {L"Object52", L"Object115.bmd", L"medicine2.jpg", L"Item"},
    {L"Object52", L"Object115.bmd", L"medicine1.jpg", L"Item"},
    {L"Object52", L"Object118.bmd", L"bottle.tga", L"Item"},
    {L"Object42", L"Object34.bmd", L"bons.jpg", L"Skill"},
    {L"Object42", L"Object35.bmd", L"bons.jpg", L"Skill"},
    {L"Object43", L"Object34.bmd", L"bons.jpg", L"Skill"},
    {L"Object43", L"Object35.bmd", L"bons.jpg", L"Skill"},
    {L"Object68", L"Object36.bmd", L"bons.jpg", L"Skill"},
    {L"Object35", L"Object43.bmd", L"bons.jpg", L"Skill"},
    {L"Object35", L"Object44.bmd", L"bons.jpg", L"Skill"},
    {L"Object64", L"Object09.bmd", L"bons.jpg", L"Skill"},
    {L"Object64", L"Object10.bmd", L"bons.jpg", L"Skill"},
    {L"Monster", L"Boss_Karane_sword_left01.bmd", L"mu_rgb_lights.jpg", L"Item"},
    {L"Monster", L"Boss_Karane_sword_left02.bmd", L"mu_rgb_lights.jpg", L"Item"},
    {L"Monster", L"Boss_Karane_sword_right01.bmd", L"mu_rgb_lights.jpg", L"Item"},
    {L"Monster", L"Boss_Karane_sword_right02.bmd", L"mu_rgb_lights.jpg", L"Item"},
    {L"Monster", L"Boss_Karane_sword_main01.bmd", L"mu_rgb_lights.jpg", L"Item"},

};

constexpr WorldModelDependency Models0LORENCIA[]{
    {MODEL_BIRD01, L"Object1/Bird01.bmd", 0},
    {MODEL_FISH01, L"Object1/Fish01.bmd", 0},
};

constexpr WorldModelDependency Models4LOSTTOWER[]{
    {MODEL_DUNGEON_STONE01, L"Object2/DungeonStone01.bmd", 0},
    {MODEL_BAT01, L"Object2/Bat01.bmd", 0},
    {MODEL_RAT01, L"Object2/Rat01.bmd", 0},
};

constexpr WorldModelDependency Models2DEVIAS[]{
    {MODEL_NPC_SERBIS_DONKEY, L"Npc/obj_donkey.bmd", 0},
    {MODEL_NPC_SERBIS_FLAG, L"Npc/obj_flag.bmd", 0},
    {MODEL_WARP, L"Npc/warp01.bmd", 0},
    {MODEL_WARP2, L"Npc/warp02.bmd", 0},
    {MODEL_WARP3, L"Npc/warp03.bmd", 0},
};

constexpr WorldModelDependency Models3NORIA[]{
    {MODEL_BUTTERFLY01, L"Object1/Butterfly01.bmd", 0},
    {MODEL_WARP, L"Npc/warp01.bmd", 0},
    {MODEL_WARP2, L"Npc/warp02.bmd", 0},
    {MODEL_WARP3, L"Npc/warp03.bmd", 0},
};

constexpr WorldModelDependency Models5UNKNOWN[]{
    {MODEL_BIG_METEO1 + 0, L"Object6/Meteo01.bmd", 0},
    {MODEL_BIG_METEO1 + 1, L"Object6/Meteo02.bmd", 0},
    {MODEL_BIG_METEO1 + 2, L"Object6/Meteo03.bmd", 0},
    {MODEL_BIG_METEO1 + 3, L"Object6/Meteo04.bmd", 0},
    {MODEL_BIG_METEO1 + 4, L"Object6/Meteo05.bmd", 0},
    {MODEL_BOSS_HEAD, L"Object6/BossHead01.bmd", 0},
    {MODEL_PRINCESS, L"Object6/Princess01.bmd", 0},
};

constexpr WorldModelDependency Models6STADIUM[]{
    {MODEL_BUG01, L"Object7/Bug01.bmd", 0},
};

constexpr WorldModelDependency Models67DOPPLEGANGER3[]{
    {MODEL_FISH01 + 1, L"Object8/Fish02.bmd", 0}, {MODEL_FISH01 + 2, L"Object8/Fish03.bmd", 0},
    {MODEL_FISH01 + 3, L"Object8/Fish04.bmd", 0}, {MODEL_FISH01 + 4, L"Object8/Fish05.bmd", 0},
    {MODEL_FISH01 + 5, L"Object8/Fish06.bmd", 0}, {MODEL_FISH01 + 6, L"Object8/Fish07.bmd", 0},
    {MODEL_FISH01 + 7, L"Object8/Fish08.bmd", 0}, {MODEL_FISH01 + 8, L"Object8/Fish09.bmd", 0},
};

constexpr WorldModelDependency Models8TARKAN[]{
    {MODEL_BUG01 + 1, L"Object9/Bug02.bmd", 0},
};

constexpr WorldModelDependency Models10HEAVEN[]{
    {MODEL_CLOUD, L"Object11/cloud.bmd", 0},
};

constexpr WorldModelDependency Models52BLOODCASTLE_MASTER_LEVEL[]{
    {MODEL_CROW, L"Object12/Crow01.bmd", 0},
    {MODEL_GATE, L"Object12/Gate01.bmd", 0, L"Monster",
     WorldModelDependency::Lifetime::SessionBasic},
    {MODEL_GATE + 1, L"Object12/Gate02.bmd", 0, L"Monster",
     WorldModelDependency::Lifetime::SessionBasic},
};

constexpr WorldModelDependency Models34CRYWOLF_1ST[]{
    {MODEL_SCOLPION, L"Object35/scorpion.bmd", 0},
    {MODEL_ARROW_TANKER, L"Monster/arrowstusk.bmd", 0},
    {MODEL_ARROW_TANKER_HIT, L"Monster/arrowstusk.bmd", 0},
};

constexpr WorldModelDependency Models31HUNTING_GROUND[]{
    {MODEL_BUTTERFLY01, L"Object1/Butterfly01.bmd", 0},
};

constexpr WorldModelDependency Models33AIDA[]{
    {MODEL_BUTTERFLY01, L"Object1/Butterfly01.bmd", 0},
    {MODEL_TREE_ATTACK, L"Object34/tree_eff.bmd", 0},
    {MODEL_BUG01 + 1, L"Object9/Bug02.bmd", 0},
};

constexpr WorldModelDependency Models37KANTURU_1ST[]{
    {MODEL_BUTTERFLY01, L"Object1/Butterfly01.bmd", 0},
};

constexpr WorldModelDependency Models38KANTURU_2ND[]{
    {MODEL_TRAP_CANON, L"Npc/c_mon.bmd", 0},
    {MODEL_STORM2, L"SKill/boswind.bmd", 0},
    {MODEL_STORM3, L"Skill/mayatonedo.bmd", 0},
};

constexpr WorldModelDependency Models39KANTURU_3RD[]{
    {MODEL_STORM2, L"SKill/boswind.bmd", 0},
    {MODEL_STORM3, L"Skill/mayatonedo.bmd", 0},
    {MODEL_MAYASTAR, L"Skill/arrowsre05.bmd", 0},
    {MODEL_MAYASTONE1, L"Skill/mayastone01.bmd", 0},
    {MODEL_MAYASTONE2, L"Skill/mayastone02.bmd", 0},
    {MODEL_MAYASTONE3, L"Skill/mayastone03.bmd", 0},
    {MODEL_MAYASTONE4, L"Skill/mayastone04.bmd", 0},
    {MODEL_MAYASTONE5, L"Skill/mayastone05.bmd", 0},
    {MODEL_MAYASTONEFIRE, L"Skill/mayastonebluefire.bmd", 0},
    {MODEL_MAYAHANDSKILL, L"Skill/hendlight02.bmd", 0},
};

constexpr WorldModelDependency Models45CURSEDTEMPLE_LV6[]{
    {MODEL_FALL_STONE_EFFECT, L"Object47/Stoneeffec.bmd", 0},
};

constexpr WorldModelDependency Models51HOME_6TH_CHAR[]{
    {MODEL_EAGLE, L"Object52/sos3bi01.bmd", 1},
    {MODEL_MAP_TORNADO, L"Object52/typhoonall.bmd", 1},
    {MODEL_TOTEMGOLEM_PART1, L"Monster/totemhead.bmd", 0},
    {MODEL_TOTEMGOLEM_PART2, L"Monster/totembody.bmd", 0},
    {MODEL_TOTEMGOLEM_PART3, L"Monster/totemleft.bmd", 0},
    {MODEL_TOTEMGOLEM_PART4, L"Monster/totemright.bmd", 0},
    {MODEL_TOTEMGOLEM_PART5, L"Monster/totemleg.bmd", 0},
    {MODEL_TOTEMGOLEM_PART6, L"Monster/totemleg2.bmd", 0},
};

constexpr WorldModelDependency Models63PK_FIELD[]{
    {MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_HEAD, L"Monster/pk_manhead_green.bmd", 0},
    {MODEL_PKFIELD_ASSASSIN_EFFECT_RED_HEAD, L"Monster/pk_manhead_red.bmd", 0},
    {MODEL_PKFIELD_ASSASSIN_EFFECT_GREEN_BODY, L"Monster/assassin_dieg.bmd", 0},
    {MODEL_PKFIELD_ASSASSIN_EFFECT_RED_BODY, L"Monster/assassin_dier.bmd", 0},
};

constexpr WorldModelDependency Models56MAP_SWAMP_OF_QUIET[]{
    {MODEL_SHADOW_PAWN_ANKLE_LEFT, L"Monster/shadow_pawn_7_ankle_left.bmd", 0},
    {MODEL_SHADOW_PAWN_ANKLE_RIGHT, L"Monster/shadow_pawn_7_ankle_right.bmd", 0},
    {MODEL_SHADOW_PAWN_BELT, L"Monster/shadow_pawn_7_belt.bmd", 0},
    {MODEL_SHADOW_PAWN_CHEST, L"Monster/shadow_pawn_7_chest.bmd", 0},
    {MODEL_SHADOW_PAWN_HELMET, L"Monster/shadow_pawn_7_helmet.bmd", 0},
    {MODEL_SHADOW_PAWN_KNEE_LEFT, L"Monster/shadow_pawn_7_knee_left.bmd", 0},
    {MODEL_SHADOW_PAWN_KNEE_RIGHT, L"Monster/shadow_pawn_7_knee_right.bmd", 0},
    {MODEL_SHADOW_PAWN_WRIST_LEFT, L"Monster/shadow_pawn_7_wrist_left.bmd", 0},
    {MODEL_SHADOW_PAWN_WRIST_RIGHT, L"Monster/shadow_pawn_7_wrist_right.bmd", 0},
    {MODEL_SHADOW_KNIGHT_ANKLE_LEFT, L"Monster/shadow_knight_7_ankle_left.bmd", 0},
    {MODEL_SHADOW_KNIGHT_ANKLE_RIGHT, L"Monster/shadow_knight_7_ankle_right.bmd", 0},
    {MODEL_SHADOW_KNIGHT_BELT, L"Monster/shadow_knight_7_belt.bmd", 0},
    {MODEL_SHADOW_KNIGHT_CHEST, L"Monster/shadow_knight_7_chest.bmd", 0},
    {MODEL_SHADOW_KNIGHT_HELMET, L"Monster/shadow_knight_7_helmet.bmd", 0},
    {MODEL_SHADOW_KNIGHT_KNEE_LEFT, L"Monster/shadow_knight_7_knee_left.bmd", 0},
    {MODEL_SHADOW_KNIGHT_KNEE_RIGHT, L"Monster/shadow_knight_7_knee_right.bmd", 0},
    {MODEL_SHADOW_KNIGHT_WRIST_LEFT, L"Monster/shadow_knight_7_wrist_left.bmd", 0},
    {MODEL_SHADOW_KNIGHT_WRIST_RIGHT, L"Monster/shadow_knight_7_wrist_right.bmd", 0},
    {MODEL_SHADOW_ROOK_ANKLE_LEFT, L"Monster/shadow_rock_7_ankle_left.bmd", 0},
    {MODEL_SHADOW_ROOK_ANKLE_RIGHT, L"Monster/shadow_rock_7_ankle_right.bmd", 0},
    {MODEL_SHADOW_ROOK_BELT, L"Monster/shadow_rock_7_belt.bmd", 0},
    {MODEL_SHADOW_ROOK_CHEST, L"Monster/shadow_rock_7_chest.bmd", 0},
    {MODEL_SHADOW_ROOK_HELMET, L"Monster/shadow_rock_7_helmet.bmd", 0},
    {MODEL_SHADOW_ROOK_KNEE_LEFT, L"Monster/shadow_rock_7_knee_left.bmd", 0},
    {MODEL_SHADOW_ROOK_KNEE_RIGHT, L"Monster/shadow_rock_7_knee_right.bmd", 0},
    {MODEL_SHADOW_ROOK_WRIST_LEFT, L"Monster/shadow_rock_7_wrist_left.bmd", 0},
    {MODEL_SHADOW_ROOK_WRIST_RIGHT, L"Monster/shadow_rock_7_wrist_right.bmd", 0},
    {MODEL_EX01_SHADOW_MASTER_ANKLE_LEFT, L"Monster/ex01shadow_rock_7_ankle_left.bmd", 0},
    {MODEL_EX01_SHADOW_MASTER_ANKLE_RIGHT, L"Monster/ex01shadow_rock_7_ankle_right.bmd", 0},
    {MODEL_EX01_SHADOW_MASTER_BELT, L"Monster/ex01shadow_rock_7_belt.bmd", 0},
    {MODEL_EX01_SHADOW_MASTER_CHEST, L"Monster/ex01shadow_rock_7_chest.bmd", 0},
    {MODEL_EX01_SHADOW_MASTER_HELMET, L"Monster/ex01shadow_rock_7_helmet.bmd", 0},
    {MODEL_EX01_SHADOW_MASTER_KNEE_LEFT, L"Monster/ex01shadow_rock_7_knee_left.bmd", 0},
    {MODEL_EX01_SHADOW_MASTER_KNEE_RIGHT, L"Monster/ex01shadow_rock_7_knee_right.bmd", 0},
    {MODEL_EX01_SHADOW_MASTER_WRIST_LEFT, L"Monster/ex01shadow_rock_7_wrist_left.bmd", 0},
    {MODEL_EX01_SHADOW_MASTER_WRIST_RIGHT, L"Monster/ex01shadow_rock_7_wrist_right.bmd", 0},
};

constexpr WorldModelDependency Models58ICECITY_BOSS[]{
    {MODEL_FALL_STONE_EFFECT, L"Object47/Stoneeffec.bmd", 0},
    {MODEL_WARP, L"Npc/warp01.bmd", 0},
    {MODEL_WARP2, L"Npc/warp02.bmd", 0},
    {MODEL_WARP3, L"Npc/warp03.bmd", 0},
    {MODEL_WARP4, L"Npc/warp01.bmd", 0},
    {MODEL_WARP5, L"Npc/warp02.bmd", 0},
    {MODEL_WARP6, L"Npc/warp03.bmd", 0},
    {MODEL_SUMMON, L"SKill/nightmaresum.bmd", 0, nullptr,
     WorldModelDependency::Lifetime::SessionBasic},
    {MODEL_STORM2, L"SKill/boswind.bmd", 0},
};

constexpr WorldModelDependency Models72EMPIREGUARDIAN4[]{
    {MODEL_PROJECTILE, L"Effect/choarms_06.bmd", 0},
    {MODEL_DOOR_CRUSH_EFFECT_PIECE01, L"Effect/piece01_01.bmd", 0},
    {MODEL_DOOR_CRUSH_EFFECT_PIECE02, L"Effect/piece01_02.bmd", 0},
    {MODEL_DOOR_CRUSH_EFFECT_PIECE03, L"Effect/piece01_03.bmd", 0},
    {MODEL_DOOR_CRUSH_EFFECT_PIECE04, L"Effect/piece01_04.bmd", 0},
    {MODEL_DOOR_CRUSH_EFFECT_PIECE05, L"Effect/piece01_05.bmd", 0},
    {MODEL_DOOR_CRUSH_EFFECT_PIECE06, L"Effect/piece01_06.bmd", 0},
    {MODEL_DOOR_CRUSH_EFFECT_PIECE07, L"Effect/piece01_07.bmd", 0},
    {MODEL_DOOR_CRUSH_EFFECT_PIECE08, L"Effect/piece01_08.bmd", 0},
    {MODEL_DOOR_CRUSH_EFFECT_PIECE09, L"Effect/newdoor break_01.bmd", 0},
    {MODEL_STATUE_CRUSH_EFFECT_PIECE01, L"Effect/NpcGagoil_Crack01.bmd", 0},
    {MODEL_STATUE_CRUSH_EFFECT_PIECE02, L"Effect/NpcGagoil_Crack02.bmd", 0},
    {MODEL_STATUE_CRUSH_EFFECT_PIECE03, L"Effect/NpcGagoil_Crack03.bmd", 0},
    {MODEL_STATUE_CRUSH_EFFECT_PIECE04, L"Effect/NpcGagoil_Ruin.bmd", 0},
    {MODEL_DOOR_CRUSH_EFFECT_PIECE10, L"Effect/sojghmoon02.bmd", 0},
    {MODEL_DOOR_CRUSH_EFFECT_PIECE11, L"Effect/sojghmj01.bmd", 0},
    {MODEL_DOOR_CRUSH_EFFECT_PIECE12, L"Effect/sojghmj02.bmd", 0},
    {MODEL_DOOR_CRUSH_EFFECT_PIECE13, L"Effect/sojghmj03.bmd", 0},
};

constexpr WorldModelDependency Models55LOGINSCENE[]{
    {MODEL_DRAGON, L"Object56/Dragon.bmd", 0},
};

constexpr WorldModelDependency GaionSwords[]{
    {MODEL_SWORDLEFT01_EMPIREGUARDIAN_BOSS_GAION_, L"Monster/Boss_Karane_sword_left01.bmd"},
    {MODEL_SWORDLEFT02_EMPIREGUARDIAN_BOSS_GAION_, L"Monster/Boss_Karane_sword_left02.bmd"},
    {MODEL_SWORDRIGHT01_EMPIREGUARDIAN_BOSS_GAION_, L"Monster/Boss_Karane_sword_right01.bmd"},
    {MODEL_SWORDRIGHT02_EMPIREGUARDIAN_BOSS_GAION_, L"Monster/Boss_Karane_sword_right02.bmd"},
    {MODEL_SWORDMAIN01_EMPIREGUARDIAN_BOSS_GAION_, L"Monster/Boss_Karane_sword_main01.bmd"},
};
constexpr WorldModelDependency IceGiantParts[]{
    {MODEL_ICE_GIANT_PART1, L"Monster/icegiantpart_1.bmd"},
    {MODEL_ICE_GIANT_PART2, L"Monster/icegiantpart_2.bmd"},
    {MODEL_ICE_GIANT_PART3, L"Monster/icegiantpart_3.bmd"},
    {MODEL_ICE_GIANT_PART4, L"Monster/icegiantpart_4.bmd"},
    {MODEL_ICE_GIANT_PART5, L"Monster/icegiantpart_5.bmd"},
    {MODEL_ICE_GIANT_PART6, L"Monster/icegiantpart_6.bmd"},
};
constexpr auto ModelsGaion = [] {
    std::array<WorldModelDependency, std::size(Models72EMPIREGUARDIAN4) + std::size(GaionSwords)>
        models{};
    std::copy(std::begin(Models72EMPIREGUARDIAN4), std::end(Models72EMPIREGUARDIAN4),
              models.begin());
    std::copy(std::begin(GaionSwords), std::end(GaionSwords),
              models.begin() + std::size(Models72EMPIREGUARDIAN4));
    return models;
}();

#ifdef ASG_ADD_KARUTAN_MONSTERS
constexpr WorldModelDependency Models81KARUTAN2[]{
    {MODEL_CONDRA_ARM_L, L"Monster/condra_7_arm_left.bmd", 0},
    {MODEL_CONDRA_ARM_L2, L"Monster/condra_7_arm_left_2.bmd", 0},
    {MODEL_CONDRA_SHOULDER, L"Monster/condra_7_shoulder_right.bmd", 0},
    {MODEL_CONDRA_ARM_R, L"Monster/condra_7_arm_right.bmd", 0},
    {MODEL_CONDRA_ARM_R2, L"Monster/condra_7_arm_right_2.bmd", 0},
    {MODEL_CONDRA_CONE_L, L"Monster/condra_7_cone_left.bmd", 1, nullptr,
     WorldModelDependency::Lifetime::WorldVisit, WorldModelDependency::Geometry::PoseOnly},
    {MODEL_CONDRA_CONE_R, L"Monster/condra_7_cone_right.bmd", 0},
    {MODEL_CONDRA_PELVIS, L"Monster/condra_7_pelvis.bmd", 0},
    {MODEL_CONDRA_STOMACH, L"Monster/condra_7_stomach.bmd", 0},
    {MODEL_CONDRA_NECK, L"Monster/condra_7_neck.bmd", 0},
    {MODEL_NARCONDRA_ARM_L, L"Monster/nar_condra_7_arm_left.bmd", 0},
    {MODEL_NARCONDRA_ARM_L2, L"Monster/nar_condra_7_arm_left_2.bmd", 0},
    {MODEL_NARCONDRA_SHOULDER_L, L"Monster/nar_condra_7_shoulder_left.bmd", 0},
    {MODEL_NARCONDRA_SHOULDER_R, L"Monster/nar_condra_7_shoulder_right.bmd", 0},
    {MODEL_NARCONDRA_ARM_R, L"Monster/nar_condra_7_arm_right.bmd", 0},
    {MODEL_NARCONDRA_ARM_R2, L"Monster/nar_condra_7_arm_right_2.bmd", 0},
    {MODEL_NARCONDRA_ARM_R3, L"Monster/nar_condra_7_arm_right_3.bmd", 0},
    {MODEL_NARCONDRA_CONE_1, L"Monster/nar_condra_7_cone_1.bmd", 0},
    {MODEL_NARCONDRA_CONE_2, L"Monster/nar_condra_7_cone_2.bmd", 0},
    {MODEL_NARCONDRA_CONE_3, L"Monster/nar_condra_7_cone_3.bmd", 0},
    {MODEL_NARCONDRA_CONE_4, L"Monster/nar_condra_7_cone_4.bmd", 0},
    {MODEL_NARCONDRA_CONE_5, L"Monster/nar_condra_7_cone_5.bmd", 0},
    {MODEL_NARCONDRA_CONE_6, L"Monster/nar_condra_7_cone_6.bmd", 0},
    {MODEL_NARCONDRA_PELVIS, L"Monster/nar_condra_7_pelvis.bmd", 0},
    {MODEL_NARCONDRA_STOMACH, L"Monster/nar_condra_7_stomach.bmd", 0},
    {MODEL_NARCONDRA_NECK, L"Monster/nar_condra_7_neck.bmd", 0},
};

#endif
constexpr WorldModelDependency Chaos[]{
    {MODEL_ANGEL, L"Player/Angel.bmd", 0, L"Npc"},
};

constexpr WorldModelDependency Hellas[]{
    {9, L"Object25/Object10.bmd", 0}, // Runtime falling-stone effect, even without a placement.
    {MODEL_CUNDUN_PART1, L"Monster/cd71a.bmd", 0},
    {MODEL_CUNDUN_PART2, L"Monster/cd71b.bmd", 0},
    {MODEL_CUNDUN_PART3, L"Monster/cd71c.bmd", 0},
    {MODEL_CUNDUN_PART4, L"Monster/cd71d.bmd", 0},
    {MODEL_CUNDUN_PART5, L"Monster/cd71e.bmd", 0},
    {MODEL_CUNDUN_PART6, L"Monster/cd71f.bmd", 1},
    {MODEL_CUNDUN_PART7, L"Monster/cd71g.bmd", 1},
    {MODEL_CUNDUN_PART8, L"Monster/cd71h.bmd", 0},
    {MODEL_CUNDUN_DRAGON_HEAD, L"Skill/dragonhead.bmd", 0},
    {MODEL_CUNDUN_PHOENIX, L"Skill/phoenix.bmd", 1},
    {MODEL_CUNDUN_GHOST, L"Monster/cundun_gone.bmd", 1},
};

} // namespace

std::span<const WorldModelDependency> WorldModelDependency::For(int behaviorMap) noexcept
{
    if ((behaviorMap >= WD_18CHAOS_CASTLE && behaviorMap <= WD_18CHAOS_CASTLE_END) ||
        behaviorMap == WD_53CAOSCASTLE_MASTER_LEVEL)
        return Chaos;
    if ((behaviorMap >= WD_24HELLAS && behaviorMap <= WD_24HELLAS_END) ||
        behaviorMap == WD_24HELLAS_7)
        return Hellas;
    switch (behaviorMap)
    {
    case WD_0LORENCIA:
        return Models0LORENCIA;
    case WD_1DUNGEON:
    case WD_4LOSTTOWER:
        return Models4LOSTTOWER;
    case WD_2DEVIAS:
        return Models2DEVIAS;
    case WD_3NORIA:
        return Models3NORIA;
    case WD_5UNKNOWN:
        return Models5UNKNOWN;
    case WD_6STADIUM:
        return Models6STADIUM;
    case WD_7ATLANSE:
    case WD_67DOPPLEGANGER3:
        return Models67DOPPLEGANGER3;
    case WD_8TARKAN:
        return Models8TARKAN;
    case WD_10HEAVEN:
        return Models10HEAVEN;
    case WD_11BLOODCASTLE1:
    case WD_11BLOODCASTLE1 + 1:
    case WD_11BLOODCASTLE1 + 2:
    case WD_11BLOODCASTLE1 + 3:
    case WD_11BLOODCASTLE1 + 4:
    case WD_11BLOODCASTLE1 + 5:
    case WD_11BLOODCASTLE1 + 6:
    case WD_52BLOODCASTLE_MASTER_LEVEL:
        return Models52BLOODCASTLE_MASTER_LEVEL;
    case WD_34CRYWOLF_1ST:
        return Models34CRYWOLF_1ST;
    case WD_31HUNTING_GROUND:
        return Models31HUNTING_GROUND;
    case WD_33AIDA:
        return Models33AIDA;
    case WD_68DOPPLEGANGER4:
    case WD_37KANTURU_1ST:
        return Models37KANTURU_1ST;
    case WD_38KANTURU_2ND:
        return Models38KANTURU_2ND;
    case WD_39KANTURU_3RD:
        return Models39KANTURU_3RD;
    case WD_45CURSEDTEMPLE_LV1:
    case WD_45CURSEDTEMPLE_LV2:
    case WD_45CURSEDTEMPLE_LV3:
    case WD_45CURSEDTEMPLE_LV4:
    case WD_45CURSEDTEMPLE_LV5:
    case WD_45CURSEDTEMPLE_LV6:
        return Models45CURSEDTEMPLE_LV6;
    case WD_51HOME_6TH_CHAR:
        return Models51HOME_6TH_CHAR;
    case WD_63PK_FIELD:
        return Models63PK_FIELD;
    case WD_56MAP_SWAMP_OF_QUIET:
        return Models56MAP_SWAMP_OF_QUIET;
    case WD_57ICECITY:
    case WD_58ICECITY_BOSS:
        return Models58ICECITY_BOSS;
    case WD_69EMPIREGUARDIAN1:
    case WD_70EMPIREGUARDIAN2:
    case WD_71EMPIREGUARDIAN3:
        return Models72EMPIREGUARDIAN4;
    case WD_72EMPIREGUARDIAN4:
        return ModelsGaion;
    case WD_55LOGINSCENE:
        return Models55LOGINSCENE;
#ifdef ASG_ADD_KARUTAN_MONSTERS
    case WD_80KARUTAN1:
    case WD_81KARUTAN2:
        return Models81KARUTAN2;
#endif
    default:
        return {};
    }
}

std::span<const WorldModelDependency> WorldModelDependency::ForMonster(int monsterModel) noexcept
{
    switch (monsterModel)
    {
    case MONSTER_MODEL_GAYION:
        return GaionSwords;
    case MONSTER_MODEL_ICE_GIANT:
    case MONSTER_MODEL_DARK_GIANT:
        return IceGiantParts;
    default:
        return {};
    }
}

std::span<const int> WorldModelDependency::ForWorldSpawns(int behaviorMap) noexcept
{
    static constexpr int elbeland[]{MONSTER_MODEL_BUTTERFLY,      MONSTER_MODEL_HIDEOUS_RABBIT,
                                    MONSTER_MODEL_WEREWOLF2,      MONSTER_MODEL_CURSED_LICH,
                                    MONSTER_MODEL_TOTEM_GOLEM,    MONSTER_MODEL_GRIZZLY,
                                    MONSTER_MODEL_CAPTAIN_GRIZZLY};
    static constexpr int crywolf[]{MONSTER_MODEL_BALGASS,      MONSTER_MODEL_DARK_ELF_1,
                                   MONSTER_MODEL_DEATH_SPIRIT, MONSTER_MODEL_BALRAM,
                                   MONSTER_MODEL_SORAM,        MONSTER_MODEL_BALLISTA};
    static constexpr int kanturu[]{MONSTER_MODEL_DARK_SKULL_SOLDIER_5};
    static constexpr int battleCastle[]{MONSTER_MODEL_BATTLE_GUARD2};
    static constexpr int hellas[]{MONSTER_MODEL_BAHAMUT};
    if ((behaviorMap >= WD_24HELLAS && behaviorMap <= WD_24HELLAS_END) ||
        behaviorMap == WD_24HELLAS_7)
        return hellas;
    switch (behaviorMap)
    {
    case WD_51HOME_6TH_CHAR:
        return elbeland;
    case WD_34CRYWOLF_1ST:
        return crywolf;
    case WD_39KANTURU_3RD:
        return kanturu;
    case WD_30BATTLECASTLE:
        return battleCastle;
    default:
        return {};
    }
}

std::filesystem::path WorldModelDependency::SharedTexturePath(const std::filesystem::path &model,
                                                              const wchar_t *texture)
{
    const auto folder = model.parent_path().filename();
    const auto name = model.filename();
    for (const auto &reference : SharedTextures)
    {
        if (folder != reference.folder || name != reference.model ||
            _wcsicmp(texture, reference.texture) != 0)
            continue;
        return model.parent_path().parent_path() / reference.source / texture;
    }
    return {};
}

namespace
{
struct NamedRange
{
    int first;
    int count;
    const wchar_t *name;
};
constexpr NamedRange Lorencia[]{
    {MODEL_TREE01, 13, L"Tree"},
    {MODEL_GRASS01, 8, L"Grass"},
    {MODEL_STONE01, 5, L"Stone"},
    {MODEL_STONE_STATUE01, 3, L"StoneStatue"},
    {MODEL_STEEL_STATUE, 1, L"SteelStatue"},
    {MODEL_TOMB01, 3, L"Tomb"},
    {MODEL_FIRE_LIGHT01, 2, L"FireLight"},
    {MODEL_BONFIRE, 1, L"Bonfire"},
    {MODEL_DUNGEON_GATE, 1, L"DoungeonGate"},
    {MODEL_TREASURE_DRUM, 1, L"TreasureDrum"},
    {MODEL_TREASURE_CHEST, 1, L"TreasureChest"},
    {MODEL_SHIP, 1, L"Ship"},
    {MODEL_STONE_WALL01, 6, L"StoneWall"},
    {MODEL_MU_WALL01, 4, L"StoneMuWall"},
    {MODEL_STEEL_WALL01, 3, L"SteelWall"},
    {MODEL_STEEL_DOOR, 1, L"SteelDoor"},
    {MODEL_CANNON01, 3, L"Cannon"},
    {MODEL_BRIDGE, 1, L"Bridge"},
    {MODEL_FENCE01, 4, L"Fence"},
    {MODEL_BRIDGE_STONE, 1, L"BridgeStone"},
    {MODEL_STREET_LIGHT, 1, L"StreetLight"},
    {MODEL_CURTAIN, 1, L"Curtain"},
    {MODEL_CARRIAGE01, 4, L"Carriage"},
    {MODEL_STRAW01, 2, L"Straw"},
    {MODEL_SIGN01, 2, L"Sign"},
    {MODEL_MERCHANT_ANIMAL01, 2, L"MerchantAnimal"},
    {MODEL_WATERSPOUT, 1, L"Waterspout"},
    {MODEL_WELL01, 4, L"Well"},
    {MODEL_HANGING, 1, L"Hanging"},
    {MODEL_HOUSE01, 5, L"House"},
    {MODEL_TENT, 1, L"Tent"},
    {MODEL_STAIR, 1, L"Stair"},
    {MODEL_HOUSE_WALL01, 6, L"HouseWall"},
    {MODEL_HOUSE_ETC01, 3, L"HouseEtc"},
    {MODEL_LIGHT01, 3, L"Light"},
    {MODEL_POSE_BOX, 1, L"PoseBox"},
    {MODEL_FURNITURE01, 7, L"Furniture"},
    {MODEL_CANDLE, 1, L"Candle"},
    {MODEL_BEER01, 3, L"Beer"},
};

} // namespace

std::filesystem::path WorldPrimaryModel::Path(const MapDefinition &map, int slot)
{
    const auto folder = L"Object" + std::to_wstring(map.assetSet);
    if (map.BehaviorMap() == WD_0LORENCIA)
    {
        for (const auto &range : Lorencia)
        {
            if (slot < range.first || slot >= range.first + range.count)
                continue;
            const int number = slot - range.first + 1;
            return std::filesystem::path(folder) /
                   (std::wstring(range.name) + (number < 10 ? L"0" : L"") +
                    std::to_wstring(number) + L".bmd");
        }
        return {};
    }
    const int number = slot + 1;
    return std::filesystem::path(folder) / (std::wstring(L"Object") + (number < 10 ? L"0" : L"") +
                                            std::to_wstring(number) + L".bmd");
}

bool WorldPrimaryModel::UsesRigidGeometry(int behaviorMap, int slot) noexcept
{
    return behaviorMap == WD_0LORENCIA && slot >= MODEL_WORLD_OBJECT && slot < MAX_WORLD_OBJECTS;
}

bool WorldPrimaryModel::RequiresGeometry(int behaviorMap, int slot) noexcept
{
    // These authored markers use only object placement/default bounds in their hooks.
    if (behaviorMap == WD_0LORENCIA && slot == MODEL_POSE_BOX)
        return false;
    if (behaviorMap == WD_2DEVIAS && slot == 91)
        return false;
    if (behaviorMap == WD_3NORIA && slot == 38)
        return false;
    if (behaviorMap == WD_7ATLANSE && slot == 39)
        return false;
    if (behaviorMap == WD_1DUNGEON && slot == 60)
        return false; // CreateOperate.
    if (behaviorMap == WD_38KANTURU_2ND && slot == 51)
        return false; // Cloud emitter.
    if ((behaviorMap == WD_72EMPIREGUARDIAN4 || behaviorMap == WD_73NEW_LOGIN_SCENE ||
         behaviorMap == WD_74NEW_CHARACTER_SCENE) &&
        slot == 132)
        return false; // Smoke emitter.
    return true;
}

// Owner-approved existing missing geometry, inventoried from shipped placements on 2026-09-09.
// Only open failures are tolerated; present but invalid models still fail preparation.
bool WorldPrimaryModel::AllowsMissingGeometry(int assetSet, int slot) noexcept
{
    switch (assetSet)
    {
    case 4:
        return slot == 43;
    case 5:
        return slot == 40;
    case 9:
        return slot == 0 || slot == 3 || slot == 4 || slot == 35 || slot == 38;
    case 12:
        return slot == 37;
    case 31:
        return slot == 35;
    case 35:
        return slot == 79 || slot == 80;
    case 39:
        return slot == 21 || slot == 22 || slot == 25 || slot == 28 || slot == 39 || slot == 40 ||
               slot == 42 || slot == 43 || slot == 69 || slot == 84;
    case 41:
        return slot == 15 || slot == 33 || slot == 52 || slot == 74;
    case 47:
        return slot == 81;
    case 57:
        return slot == 58 || slot == 59 || slot == 64;
    case 59:
        return slot == 83;
    case 63:
        return slot == 37 || slot == 40 || slot == 51;
    case 64:
        return slot == 54;
    case 65:
        return slot == 39;
    case 66:
        return slot == 6 || slot == 32 || slot == 96;
    case 69:
        return slot == 34;
    case 70:
        return slot == 95 || slot == 126;
    case 73:
        return slot == 75 || slot == 159;
    case 74:
        return slot == 1 || slot == 48 || slot == 75;
    case 75:
        return slot == 11 || slot == 124 || slot == 126;
    case 80:
        return slot == 51;
    default:
        return false;
    }
}

bool WorldPrimaryModel::IsLegacyUnboundPlacement(int assetSet, int slot) noexcept
{
    // These shipped global placement slots have no entry loader. Preserve cold-entry behavior;
    // do not borrow the last world's bird/logo or invent a replacement model identity.
    switch (assetSet)
    {
    case 52:
        return slot == MODEL_CARD + 1;
    case 69:
        return slot == MODEL_BIRD01;
    case 73:
        return slot == MODEL_LOGO || slot == MODEL_LOGOSUN;
    case 74:
        return slot == MODEL_LOGO || slot == MODEL_MUGAME || slot == MODEL_LOGOSUN ||
               slot == MODEL_CARD;
    default:
        return false;
    }
}

int WorldPrimaryModel::RequiredBones(int behaviorMap, int slot) noexcept
{
    if (slot >= MAX_WORLD_OBJECTS)
        return 0;
    const auto *definition = MapDefinition::Find(behaviorMap);
    if (definition && definition->family == MapDefinition::Family::BloodCastle)
        return slot == 11 ? 12 : slot == 13 ? 4 : 0;
    if (definition && definition->family == MapDefinition::Family::CursedTemple)
        return slot == 62 ? 24 : 0;
    if (definition && definition->family == MapDefinition::Family::Hellas)
        return slot == 12 || slot == 32 ? 6 : 0;
    if (behaviorMap >= WD_69EMPIREGUARDIAN1 && behaviorMap <= WD_74NEW_CHARACTER_SCENE)
    {
        if (slot == 12)
            return 4;
        if (slot == 37)
            return 2;
        if (slot == 50 && behaviorMap != WD_72EMPIREGUARDIAN4)
            return 8;
    }
    struct Requirement
    {
        int map;
        int slot;
        int bones;
    };
    static constexpr Requirement requirements[]{
        {WD_0LORENCIA, MODEL_WATERSPOUT, 5},
        {WD_0LORENCIA, MODEL_MERCHANT_ANIMAL01, 58},
        {WD_2DEVIAS, 100, 1},
        {WD_3NORIA, 9, 2},
        {WD_3NORIA, 35, 4},
        {WD_3NORIA, 1, 7},
        {WD_3NORIA, 17, 14},
        {WD_3NORIA, 39, 66},
        {WD_4LOSTTOWER, 19, 22},
        {WD_4LOSTTOWER, 20, 22},
        {WD_6STADIUM, 9, 2},
        {WD_8TARKAN, 63, 3},
        {WD_8TARKAN, 64, 3},
        {WD_9DEVILSQUARE, 2, 32},
        {WD_10HEAVEN, 10, 4},
        {WD_31HUNTING_GROUND, 49, 4},
        {WD_33AIDA, 30, 18},
        {WD_33AIDA, 71, 18},
        {WD_33AIDA, 75, 5},
        {WD_37KANTURU_1ST, 70, 7},
        {WD_37KANTURU_1ST, 92, 3},
        {WD_37KANTURU_1ST, 95, 10},
        {WD_37KANTURU_1ST, 98, 2},
        {WD_37KANTURU_1ST, 105, 5},
        {WD_37KANTURU_1ST, 110, 2},
        {WD_38KANTURU_2ND, 4, 2},
        {WD_38KANTURU_2ND, 8, 5},
        {WD_39KANTURU_3RD, 0, 35},
        {WD_39KANTURU_3RD, 5, 2},
        {WD_51HOME_6TH_CHAR, 63, 6},
        {WD_51HOME_6TH_CHAR, 110, 1},
        {WD_51HOME_6TH_CHAR, 121, 9},
        {WD_57ICECITY, 19, 20},
        {WD_57ICECITY, 20, 12},
        {WD_57ICECITY, 21, 25},
        {WD_57ICECITY, 57, 2},
        {WD_58ICECITY_BOSS, 19, 20},
        {WD_58ICECITY_BOSS, 20, 12},
        {WD_58ICECITY_BOSS, 21, 25},
        {WD_58ICECITY_BOSS, 57, 2},
        {WD_66DOPPLEGANGER2, 67, 7},
        {WD_66DOPPLEGANGER2, 68, 7},
        {WD_68DOPPLEGANGER4, 70, 7},
        {WD_68DOPPLEGANGER4, 92, 3},
        {WD_68DOPPLEGANGER4, 95, 10},
        {WD_68DOPPLEGANGER4, 98, 2},
        {WD_68DOPPLEGANGER4, 105, 5},
        {WD_68DOPPLEGANGER4, 110, 2},
        {WD_69EMPIREGUARDIAN1, 115, 10},
        {WD_69EMPIREGUARDIAN1, 117, 10},
        {WD_72EMPIREGUARDIAN4, 157, 27},
        {WD_79UNITEDMARKETPLACE, 30, 4},
        {WD_79UNITEDMARKETPLACE, 35, 3},
        {WD_80KARUTAN1, 66, 15},
        {WD_80KARUTAN1, 72, 12},
        {WD_81KARUTAN2, 66, 15},
        {WD_81KARUTAN2, 72, 12},
    };
    for (const auto &requirement : requirements)
        if (requirement.map == behaviorMap && requirement.slot == slot)
            return requirement.bones;
    return 0;
}

int WorldPrimaryModel::RequiredActions(int behaviorMap, int slot) noexcept
{
    switch (behaviorMap)
    {
    case WD_1DUNGEON:
        return slot == 40 ? 2 : 0;
    case WD_2DEVIAS:
        return slot == 103 ? 2 : 0;
    case WD_57ICECITY:
    case WD_58ICECITY_BOSS:
        if (slot == 17)
            return 4;
        if (slot == 16)
            return 2;
        return slot == 68 ? 1 : 0;
#ifdef ASG_ADD_MAP_KARUTAN
    case WD_80KARUTAN1:
    case WD_81KARUTAN2:
        return slot == 66 ? 2 : slot == 107 ? 1 : 0;
#endif
    default:
        return 0;
    }
}

bool WorldResources::PrepareModel(SessionKeeper &keeper, int slot,
                                  const std::filesystem::path &path, int requiredActions,
                                  Failure &failure, const std::filesystem::path &textureDirectory,
                                  bool requireGeometry, int behaviorMap)
{
    failure.resource = path;
    BMD loader(keeper);
    auto asset = loader.PrepareSharedAsset(
        path, failure.detail,
        std::max({requiredActions, ModelResourceRequirements::Actions(slot),
                  WorldPrimaryModel::RequiredActions(behaviorMap, slot)}),
        std::max(ModelResourceRequirements::RequiredBones(slot, requireGeometry),
                 WorldPrimaryModel::RequiredBones(behaviorMap, slot)),
        std::max(ModelResourceRequirements::Meshes(slot), requireGeometry ? 1 : 0),
        WorldPrimaryModel::UsesRigidGeometry(behaviorMap, slot));
    if (!asset)
        return false;
    models_.push_back({slot,
                       path,
                       std::move(asset),
                       textureDirectory.empty() ? path.parent_path() : textureDirectory,
                       {}});
    return true;
}

bool WorldResources::PrepareModels(SessionKeeper &keeper, const MapDefinition &definition,
                                   const std::filesystem::path &root, Failure &failure)
{
    const auto dependencies = WorldModelDependency::For(definition.BehaviorMap());
    if (!PrepareModels(keeper, dependencies, root, failure, definition.BehaviorMap()) ||
        !PrepareActionModels(keeper, definition, root, failure) ||
        !PrepareSpawnModels(keeper, definition, root, failure))
        return false;
    return definition.id.RawValue() != WD_74NEW_CHARACTER_SCENE ||
           PrepareCharacterModels(keeper, root, failure);
}

bool WorldResources::PrepareModels(SessionKeeper &keeper,
                                   std::span<const WorldModelDependency> dependencies,
                                   const std::filesystem::path &root, Failure &failure,
                                   int behaviorMap)
{
    models_.reserve(dependencies.size());
    for (const auto &dependency : dependencies)
    {
        if (!PrepareModel(keeper, dependency.slot, root / dependency.path,
                          dependency.requiredActions, failure,
                          dependency.textureDirectory ? root / dependency.textureDirectory
                                                      : std::filesystem::path{},
                          dependency.geometry == WorldModelDependency::Geometry::Drawable,
                          behaviorMap))
            return false;
    }
    return true;
}

bool WorldResources::PrepareCharacterModels(SessionKeeper &keeper,
                                            const std::filesystem::path &root, Failure &failure)
{
    if (!PrepareModel(keeper, MODEL_LOGO + 4, root / L"Logo/Logo05.bmd", 0, failure))
        return false;
    models_.back().textureFilter = LegacyTextureFilter::Linear;
    for (int index = 0; index < MAX_CLASS; ++index)
    {
        const auto file = L"Logo/NewFace0" + std::to_wstring(index + 1) + L".bmd";
        if (!PrepareModel(keeper, MODEL_FACE + index, root / file, 2, failure))
            return false;
    }
    return true;
}

bool WorldResources::PrepareActionModels(SessionKeeper &keeper, const MapDefinition &definition,
                                         const std::filesystem::path &root, Failure &failure)
{
    const auto folder = root / (L"Object" + std::to_wstring(definition.assetSet));
    const auto actionModel = [&](int slot, int actions) {
        const auto number = std::to_wstring(slot + 1);
        return PrepareModel(
            keeper, slot,
            folder / (std::wstring(L"Object") + (slot + 1 < 10 ? L"0" : L"") + number + L".bmd"),
            actions, failure, {}, true, definition.BehaviorMap());
    };
    switch (definition.BehaviorMap())
    {
    case WD_1DUNGEON:
        return actionModel(40, 2);
    case WD_57ICECITY:
        return actionModel(16, 2) && actionModel(17, 4) && actionModel(68, 1);
#ifdef ASG_ADD_MAP_KARUTAN
    case WD_80KARUTAN1:
    case WD_81KARUTAN2:
        return actionModel(66, 2) && actionModel(107, 1);
#endif
    default:
        return true;
    }
}

bool WorldResources::InstallModels(SessionKeeper &keeper, Failure &failure) const
{
    for (const auto &prepared : models_)
    {
        auto &model = keeper.ModelPoolObject()[prepared.slot];
        if (!model.BindSharedAsset(prepared.asset))
        {
            failure.resource = prepared.path;
            failure.detail = L"Model installation allocation failed";
            return false;
        }
        model.m_iBMDSeqID = prepared.slot;
        if (prepared.slot >= MODEL_MONSTER01 &&
            prepared.slot < MODEL_MONSTER01 + MONSTER_MODEL_COUNT)
            keeper.GameData()->ConfigureMonsterModel(
                static_cast<EMonsterModelType>(prepared.slot - MODEL_MONSTER01));
    }
    return true;
}

bool WorldResources::HasPreparedModel(int slot) const noexcept
{
    return std::any_of(models_.begin(), models_.end(),
                       [slot](const PreparedModel &model) { return model.slot == slot; });
}

bool WorldResources::PrepareSpawnModels(SessionKeeper &keeper, const MapDefinition &definition,
                                        const std::filesystem::path &root, Failure &failure)
{
    for (const int type : WorldModelDependency::ForWorldSpawns(definition.BehaviorMap()))
    {
        const auto number = std::to_wstring(type + 1);
        const auto path =
            root / L"Monster" /
            (L"Monster" + std::wstring(type + 1 < 10 ? L"0" : L"") + number + L".bmd");
        if (!PrepareModels(keeper, WorldModelDependency::ForMonster(type), root, failure) ||
            !PrepareModel(keeper, MODEL_MONSTER01 + type, path, 0, failure))
            return false;
    }
    return true;
}

bool WorldResources::PreparePrimaryModels(SessionKeeper &keeper, const MapDefinition &definition,
                                          const std::filesystem::path &root, Failure &failure)
{
    std::bitset<MAX_WORLD_OBJECTS> required;
    bool requiresBasicSkeleton = false;
    for (const auto &placement : Placements().Placements())
    {
        if (placement.type < MAX_WORLD_OBJECTS)
        {
            if (WorldPrimaryModel::RequiresGeometry(definition.BehaviorMap(), placement.type))
                required.set(placement.type);
            continue;
        }
        if (placement.type == MODEL_SKELETON_PCBANG)
        {
            requiresBasicSkeleton = true;
            continue;
        }
        if (WorldPrimaryModel::IsLegacyUnboundPlacement(definition.assetSet, placement.type))
            continue;
        failure.resource = root / (L"World" + std::to_wstring(definition.assetSet)) /
                           (L"EncTerrain" + std::to_wstring(definition.assetSet) + L".obj");
        failure.detail = L"Placement references an undeclared global model";
        return false;
    }
    // OpenSkills owns this session-basic slot. Map exit deliberately retains it.
    if (requiresBasicSkeleton &&
        !PrepareModel(keeper, MODEL_SKELETON_PCBANG, root / L"Skill/Skeleton03.bmd", 0, failure))
        return false;
    std::bitset<MAX_WORLD_OBJECTS> prepared;
    for (const auto &model : models_)
        if (model.slot < MAX_WORLD_OBJECTS)
            prepared.set(model.slot);
    for (int slot = MODEL_WORLD_OBJECT; slot < MAX_WORLD_OBJECTS; ++slot)
    {
        if (prepared[slot])
            continue;
        const auto relative = WorldPrimaryModel::Path(definition, slot);
        if (relative.empty())
        {
            if (!required[slot])
                continue;
            failure.resource = root / (L"Object" + std::to_wstring(definition.assetSet));
            failure.detail = L"Placement references an undefined primary model";
            return false;
        }
        if (!PrepareModel(keeper, slot, root / relative, 0, failure, {}, required[slot],
                          definition.BehaviorMap()))
        {
            const bool knownMissing =
                WorldPrimaryModel::AllowsMissingGeometry(definition.assetSet, slot) &&
                failure.detail != nullptr &&
                std::wstring_view(failure.detail) ==
                    BmdSharedAsset::ErrorText(BmdSharedAsset::Error::Open);
            if (required[slot] && !knownMissing)
                return false;
            failure = {}; // Unused slots and proven markers do not require a model file.
            continue;
        }
        models_.back().required = required[slot];
    }
    return true;
}

#ifdef PBG_ADD_INGAMESHOP_UI_ITEMSHOP

using namespace SEASON3B;

void CNewUIInGameShop::RenderDisplayItems()
{
    EndBitmap();

    glMatrixMode(GL_PROJECTION);
    SaveCameraPerspective();
    glPushMatrix();
    glLoadIdentity();
    glViewport2(0, 0, WindowWidth, WindowHeight);
    gluPerspective2(2.0f, (float)(WindowWidth) / (float)(WindowHeight), RENDER_ITEMVIEW_NEAR,
                    RENDER_ITEMVIEW_FAR);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    cameraProjection_.GetModelViewMatrix(g_Camera.Matrix);
    EnableDepthTest();
    EnableDepthMask();

    glClear(GL_DEPTH_BUFFER_BIT);

    for (int i = 0; i < g_InGameShopSystem.GetSizePackageAsDisplayPackage(); i++)
    {
        int iPosX = IGS_ITEMRENDER_POS_X_STANDAD +
                    (IMAGE_IGS_VIEWDETAIL_BTN_DISTANCE_X * (i % IGS_NUM_ITEMS_WIDTH));
        int iPosY = IGS_ITEMRENDER_POS_Y_STANDAD +
                    (IMAGE_IGS_VIEWDETAIL_BTN_DISTANCE_Y * (i / IGS_NUM_ITEMS_HEIGHT));
        RenderItem3D(iPosX, iPosY, IGS_ITEMRENDER_POS_WIDTH, IGS_ITEMRENDER_POS_HEIGHT,
                     g_InGameShopSystem.GetPackageItemCode(i), 0, 0, 0, true);
    }

    UpdateMousePositionn();

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();

    RestoreCameraPerspective();
    BeginBitmap();
}

#endif //PBG_ADD_INGAMESHOP_UI_ITEMSHOP

void SessionRenderUnit::FixupSMD()
{
    Skeleton_t *s = &smdStorage_->NodeGroup.Skeleton;
    NodeGroup_t *ng = &smdStorage_->NodeGroup;
    for (int i = 0; i < ng->NodeNum; i++)
    {
        Node_t *n = &ng->Node[i];

        vec3_t Angle;
        Angle[0] = s->Rotation[i][0] * (180.f / Q_PI);
        Angle[1] = s->Rotation[i][1] * (180.f / Q_PI);
        Angle[2] = s->Rotation[i][2] * (180.f / Q_PI);

        if (n->Parent == -1)
        {
            AngleMatrix(Angle, smdStorage_->BoneFixup[i].m);
            AngleIMatrix(Angle, smdStorage_->BoneFixup[i].im);
            VectorCopy(s->Position[i], smdStorage_->BoneFixup[i].WorldOrg);
        }
        else
        {
            float m[3][4];
            AngleMatrix(Angle, m);
            R_ConcatTransforms(smdStorage_->BoneFixup[n->Parent].m, m, smdStorage_->BoneFixup[i].m);
            AngleIMatrix(Angle, m);
            R_ConcatTransforms(m, smdStorage_->BoneFixup[n->Parent].im,
                               smdStorage_->BoneFixup[i].im);

            vec3_t p;
            VectorTransform(s->Position[i], smdStorage_->BoneFixup[n->Parent].m, p);
            VectorAdd(p, smdStorage_->BoneFixup[n->Parent].WorldOrg,
                      smdStorage_->BoneFixup[i].WorldOrg);
        }
    }

    TriangleGroup_t *tg = &smdStorage_->TriangleGroup;

    for (int i = 0; i < tg->TriangleNum; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            SMDVertex_t *v = &tg->Vertex[i][j];
            vec3_t p;
            VectorSubtract(v->Position, smdStorage_->BoneFixup[v->Node].WorldOrg, p);
            VectorTransform(p, smdStorage_->BoneFixup[v->Node].im, v->Position);

            VectorCopy(v->Normal, p);
            VectorTransform(p, smdStorage_->BoneFixup[v->Node].im, v->Normal);
            VectorNormalize(v->Normal);
        }
    }

    SMDMeshGroup_t *mg = &smdStorage_->MeshGroup;
    mg->MeshNum = 0;

    for (int i = 0; i < MESH_MAX; i++)
    {
        SMDMesh_t *m = &mg->Mesh[i];
        m->VertexNum = 0;
        m->NormalNum = 0;
        m->TexCoordNum = 0;
        m->TriangleNum = 0;
    }

    for (int i = 0; i < tg->TriangleNum; i++)
    {
        int MeshNum = 0;
        for (int k = 0; k < mg->MeshNum; k++)
        {
            if (strcmp(tg->TextureName[i], mg->Texture[k].FileName) == 0)
            {
                MeshNum = k;
                break;
            }
        }

        if (MeshNum == 0)
        {
            MeshNum = mg->MeshNum;
            mg->Mesh[MeshNum].Texture = mg->MeshNum;
            strcpy(mg->Texture[MeshNum].FileName, tg->TextureName[i]);
            mg->MeshNum++;
        }

        SMDMesh_t *m = &mg->Mesh[MeshNum];

        for (int j = 0; j < 3; j++)
        {
            SMDVertex_t *v = &tg->Vertex[i][j];
            int k = m->VertexNum - 1;
            if (k >= 0)
            {
                for (k; k >= 0; k--)
                {
                    Vertex_t *v2 = &m->Vertex[k];
                    if (v->Position[0] == v2->Position[0] && v->Position[1] == v2->Position[1] &&
                        v->Position[2] == v2->Position[2])
                    {
                        m->VertexList[m->TriangleNum][j] = k;
                        break;
                    }
                }
            }
            if (k == -1)
            {
                m->VertexList[m->TriangleNum][j] = m->VertexNum;
                m->Vertex[m->VertexNum].Node = tg->Vertex[i][j].Node;
                m->Vertex[m->VertexNum].Position[0] = tg->Vertex[i][j].Position[0];
                m->Vertex[m->VertexNum].Position[1] = tg->Vertex[i][j].Position[1];
                m->Vertex[m->VertexNum].Position[2] = tg->Vertex[i][j].Position[2];
                m->VertexNum++;
            }
            k = m->NormalNum - 1;
            if (k >= 0)
            {
                for (k; k >= 0; k--)
                {
                    Normal_t *n = &m->Normal[k];
                    if (v->Normal[0] == n->Normal[0] && v->Normal[1] == n->Normal[1] &&
                        v->Normal[2] == n->Normal[2])
                    {
                        m->NormalList[m->TriangleNum][j] = k;
                        break;
                    }
                }
            }
            if (k == -1)
            {
                m->NormalList[m->TriangleNum][j] = m->NormalNum;
                m->Normal[m->NormalNum].Node = tg->Vertex[i][j].Node;
                m->Normal[m->NormalNum].Normal[0] = tg->Vertex[i][j].Normal[0];
                m->Normal[m->NormalNum].Normal[1] = tg->Vertex[i][j].Normal[1];
                m->Normal[m->NormalNum].Normal[2] = tg->Vertex[i][j].Normal[2];
                m->Normal[m->NormalNum].BindVertex = m->VertexList[m->TriangleNum][j];
                m->NormalNum++;
            }
            k = m->TexCoordNum - 1;
            if (k >= 0)
            {
                for (k; k >= 0; k--)
                {
                    TexCoord_t *t = &m->TexCoord[k];
                    if (v->TexCoordU == t->TexCoordU && v->TexCoordV == t->TexCoordV)
                    {
                        m->TexCoordList[m->TriangleNum][j] = k;
                        break;
                    }
                }
            }
            if (k == -1)
            {
                m->TexCoordList[m->TriangleNum][j] = m->TexCoordNum;
                m->TexCoord[m->TexCoordNum].TexCoordU = tg->Vertex[i][j].TexCoordU;
                m->TexCoord[m->TexCoordNum].TexCoordV = tg->Vertex[i][j].TexCoordV;
                m->TexCoordNum++;
            }
        }

        m->Polygon[m->TriangleNum] = 3;
        m->TriangleNum++;
    }
}

void SessionRenderUnit::SMD2BMDModel(int ID, int Actions)
{
    BMD *bmd = &Models[ID];

    bmd->NumBones = smdStorage_->NodeGroup.NodeNum;
    bmd->NumMeshs = smdStorage_->MeshGroup.MeshNum;
    bmd->NumActions = 0;
    bmd->NumLightMaps = 0;

    bmd->Bones = new (std::nothrow) Bone_t[bmd->NumBones]();
    bmd->Meshs = new (std::nothrow) Mesh_t[bmd->NumMeshs]();
    bmd->Textures = new (std::nothrow) Texture_t[bmd->NumMeshs]();
    bmd->IndexTexture = new (std::nothrow) unsigned int[bmd->NumMeshs]();
    bmd->Actions = new (std::nothrow) Action_t[Actions]();

    if (!bmd->Bones || !bmd->Meshs || !bmd->Textures || !bmd->IndexTexture || !bmd->Actions)
    {
        wprintf(L"[SMD2BMDModel] ERROR: Memory allocation failed.\n");
        // TODO: implement safe cleanup if needed (Release() or delete[] manually)
        return;
    }

    for (int i = 0; i < bmd->NumMeshs; ++i)
    {
        SMDMesh_t &sm = smdStorage_->MeshGroup.Mesh[i];
        Mesh_t &m = bmd->Meshs[i];

        m.NoneBlendMesh = false;
        m.Texture = sm.Texture;

        // Vertices
        m.NumVertices = sm.VertexNum;
        m.Vertices = new (std::nothrow) Vertex_t[m.NumVertices];
        if (m.Vertices)
            memcpy(m.Vertices, sm.Vertex, sizeof(Vertex_t) * m.NumVertices);

        // Normals
        m.NumNormals = sm.NormalNum;
        m.Normals = new (std::nothrow) Normal_t[m.NumNormals];
        if (m.Normals)
            memcpy(m.Normals, sm.Normal, sizeof(Normal_t) * m.NumNormals);

        // TexCoords
        m.NumTexCoords = sm.TexCoordNum;
        m.TexCoords = new (std::nothrow) TexCoord_t[m.NumTexCoords];
        if (m.TexCoords)
            memcpy(m.TexCoords, sm.TexCoord, sizeof(TexCoord_t) * m.NumTexCoords);

        // Texture FileName
        memcpy(bmd->Textures[i].FileName, smdStorage_->MeshGroup.Texture[i].FileName, 32);

        // TextureScript
        TextureScriptParsing TSParsing;
        if (TSParsing.parsingTScriptA(bmd->Textures[i].FileName))
        {
            m.m_csTScript = new TextureScript;
            m.m_csTScript->setScript((TextureScript &)TSParsing);
        }
        else
        {
            m.m_csTScript = nullptr;
        }

        // Triangles
        m.NumTriangles = sm.TriangleNum;
        m.Triangles = new (std::nothrow) Triangle_t[m.NumTriangles];

        if (m.Triangles)
        {
            for (int j = 0; j < m.NumTriangles; ++j)
            {
                Triangle_t &tp = m.Triangles[j];
                tp.Polygon = sm.Polygon[j];
                for (int k = 0; k < tp.Polygon; ++k)
                {
                    tp.VertexIndex[k] = sm.VertexList[j][k];
                    tp.NormalIndex[k] = sm.NormalList[j][k];
                    tp.TexCoordIndex[k] = sm.TexCoordList[j][k];
                }
            }
        }
    }

    for (int i = 0; i < bmd->NumBones; ++i)
    {
        Node_t &n = smdStorage_->NodeGroup.Node[i];
        Bone_t &b = bmd->Bones[i];

        strncpy(b.Name, n.Name, sizeof(b.Name));
        b.Name[sizeof(b.Name) - 1] = '\0'; // Ensure null-termination
        b.Parent = n.Parent;

        b.BoneMatrixes = new (std::nothrow) BoneMatrix_t[Actions]();
    }

    bmd->Init(true);
    (void)bmd->BuildRenderTapeGeometry();
}

void SessionRenderUnit::SMD2BMDAnimation(int ID, bool LockPosition)
{
    int i, j;
    BMD *bmd = &Models[ID];

    for (i = 0; i < bmd->NumBones; i++)
    {
        Bone_t *b = &bmd->Bones[i];

        {
            BoneMatrix_t *bm = &b->BoneMatrixes[bmd->NumActions];
            bm->Position = new vec3_t[smdStorage_->SkeletonGroup.TimeNum];
            bm->Rotation = new vec3_t[smdStorage_->SkeletonGroup.TimeNum];
            bm->Quaternion = new vec4_t[smdStorage_->SkeletonGroup.TimeNum];
            for (j = 0; j < smdStorage_->SkeletonGroup.TimeNum; j++)
            {
                VectorCopy(smdStorage_->SkeletonGroup.Skeleton[j].Position[i], bm->Position[j]);
                VectorCopy(smdStorage_->SkeletonGroup.Skeleton[j].Rotation[i], bm->Rotation[j]);
                AngleQuaternion(bm->Rotation[j], bm->Quaternion[j]);
            }
        }
    }

    //action
    Action_t *a = &bmd->Actions[bmd->NumActions];
    a->Loop = false;
    a->LockPositions = LockPosition;
    a->NumAnimationKeys = smdStorage_->SkeletonGroup.TimeNum;
    a->PlaySpeed = 0.3f;

    a->Positions = new vec3_t[a->NumAnimationKeys];
    Bone_t *b = &bmd->Bones[0];
    for (i = 0; i < a->NumAnimationKeys; i++)
    {
        BoneMatrix_t *bm = &b->BoneMatrixes[bmd->NumActions];
        j = i + 1;
        if (j > a->NumAnimationKeys - 1)
            j = a->NumAnimationKeys - 1;
        VectorSubtract(bm->Position[j], bm->Position[i], a->Positions[i]);
    }

    bmd->NumActions++;
}

bool CMapManager::InstallWorldAssets(const WorldResources &resources)
{
    WorldResources::Failure failure;
    if (!resources.InstallModels(sessionKeeper_, failure) ||
        !sessionKeeper_.Visual()->InstallWorldVisuals(resources, failure))
    {
        g_ErrorReport.Write(L"%ls: %ls\r\n", failure.resource.c_str(), failure.detail);
        return false;
    }
    return true;
}

SMDToken SessionRenderUnit::GetSmdToken()
{
    char ch;
    smdStorage_->TokenString[0] = '\0';
    do
    {
        if ((ch = static_cast<char>(fgetc(smdStorage_->SMDFile))) == EOF)
            return END;
        if (ch == '/' && (ch = static_cast<char>(fgetc(smdStorage_->SMDFile))) == '/')
        {
            while ((ch = static_cast<char>(fgetc(smdStorage_->SMDFile))) != '\n')
            {
            }
        }
    } while (isspace(ch));

    char *output;
    char number[100];
    switch (ch)
    {
    case '#':
        return smdStorage_->CurrentToken = COMMAND;
    case ';':
        return smdStorage_->CurrentToken = SEMICOLON;
    case ',':
        return smdStorage_->CurrentToken = COMMA;
    case '{':
        return smdStorage_->CurrentToken = LBRACKET;
    case '}':
        return smdStorage_->CurrentToken = RBRACKET;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '.':
    case '-':
        ungetc(ch, smdStorage_->SMDFile);
        output = number;
        while (((ch = static_cast<char>(getc(smdStorage_->SMDFile))) != EOF) &&
               (ch == '.' || isdigit(ch) || ch == '-'))
        {
            *output++ = ch;
        }
        *output = 0;
        smdStorage_->TokenNumber = static_cast<float>(atof(number));
        return smdStorage_->CurrentToken = NUMBER;
    case '"':
        output = smdStorage_->TokenString;
        while (((ch = static_cast<char>(getc(smdStorage_->SMDFile))) != EOF) && ch != '"')
        {
            *output++ = ch;
        }
        if (ch != '"')
            ungetc(ch, smdStorage_->SMDFile);
        *output = 0;
        return smdStorage_->CurrentToken = NAME;
    default:
        if (isalpha(ch))
        {
            output = smdStorage_->TokenString;
            *output++ = ch;
            while (((ch = static_cast<char>(getc(smdStorage_->SMDFile))) != EOF) &&
                   (ch == '.' || ch == '_' || isalnum(ch)))
            {
                *output++ = ch;
            }
            ungetc(ch, smdStorage_->SMDFile);
            *output = 0;
            return smdStorage_->CurrentToken = NAME;
        }
        return smdStorage_->CurrentToken = SMD_ERROR;
    }
}

void SessionRenderUnit::ParseNodes()
{
    SMDToken Token;
    NodeGroup_t *ng = &smdStorage_->NodeGroup;
    ng->NodeNum = 0;

    while (true)
    {
        Token = GetSmdToken();
        if (Token == END)
            break;
        if (Token == NAME && strcmp("nodes", smdStorage_->TokenString) == 0)
            break;
    }
    while (true)
    {
        Token = GetSmdToken();
        if (Token == END)
            break;
        if (Token == NAME && strcmp("end", smdStorage_->TokenString) == 0)
            break;
        if (Token == NUMBER)
        {
            Node_t *n = &ng->Node[ng->NodeNum];
            Token = GetSmdToken();
            strcpy(n->Name, smdStorage_->TokenString);
            Token = GetSmdToken();
            n->Parent = (short)smdStorage_->TokenNumber;
        }
        ng->NodeNum++;
    }
    while (true)
    {
        Token = GetSmdToken();
        if (Token == END)
            break;
        if (Token == NAME && strcmp("skeleton", smdStorage_->TokenString) == 0)
            break;
    }
    while (true)
    {
        Token = GetSmdToken();
        if (Token == END)
            break;
        if (Token == NAME)
        {
            if (strcmp("end", smdStorage_->TokenString) == 0)
                break;
            if (strcmp("time", smdStorage_->TokenString) == 0)
            {
                Token = GetSmdToken();
                Skeleton_t *s = &ng->Skeleton;
                for (int i = 0; i < ng->NodeNum; i++)
                {
                    Token = GetSmdToken();
                    Token = GetSmdToken();
                    s->Position[i][0] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    s->Position[i][1] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    s->Position[i][2] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    s->Rotation[i][0] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    s->Rotation[i][1] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    s->Rotation[i][2] = smdStorage_->TokenNumber;
                }
            }
        }
    }
}

void SessionRenderUnit::ParseSkeleton()
{
    SMDToken Token;
    while (true)
    {
        Token = GetSmdToken();
        if (Token == END)
            break;
        if (Token == NAME && strcmp("skeleton", smdStorage_->TokenString) == 0)
            break;
    }

    SkeletonGroup_t *sg = &smdStorage_->SkeletonGroup;
    sg->TimeNum = 0;
    while (true)
    {
        Token = GetSmdToken();
        if (Token == END)
            break;
        if (Token == NAME)
        {
            if (strcmp("end", smdStorage_->TokenString) == 0)
                break;
            if (strcmp("time", smdStorage_->TokenString) == 0)
            {
                Token = GetSmdToken();
                int TimeNum = (int)smdStorage_->TokenNumber;
                Skeleton_t *s = &sg->Skeleton[TimeNum];
                for (int i = 0; i < smdStorage_->NodeGroup.NodeNum; i++)
                {
                    Token = GetSmdToken();
                    Token = GetSmdToken();
                    s->Position[i][0] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    s->Position[i][1] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    s->Position[i][2] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    s->Rotation[i][0] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    s->Rotation[i][1] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    s->Rotation[i][2] = smdStorage_->TokenNumber;
                }
                sg->TimeNum++;
            }
        }
    }
}

void SessionRenderUnit::ParseTriangles(bool Flip)
{
    SMDToken Token;
    while (true)
    {
        Token = GetSmdToken();
        if (Token == END)
            break;
        if (Token == NAME && strcmp("triangles", smdStorage_->TokenString) == 0)
            break;
    }

    TriangleGroup_t *tg = &smdStorage_->TriangleGroup;
    tg->TriangleNum = 0;
    while (true)
    {
        Token = GetSmdToken();
        if (Token == END)
            break;
        if (Token == NAME)
        {
            if (strcmp("end", smdStorage_->TokenString) == 0)
                break;
            strcpy(tg->TextureName[tg->TriangleNum], smdStorage_->TokenString);
            if (!Flip)
            {
                for (int i = 0; i < 3; i++)
                {
                    SMDVertex_t *v = &tg->Vertex[tg->TriangleNum][i];
                    Token = GetSmdToken();
                    v->Node = (short)smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    v->Position[0] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    v->Position[1] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    v->Position[2] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    v->Normal[0] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    v->Normal[1] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    v->Normal[2] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    v->TexCoordU = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    v->TexCoordV = 1.f - smdStorage_->TokenNumber;
                }
            }
            else
            {
                for (int i = 2; i >= 0; i--)
                {
                    SMDVertex_t *v = &tg->Vertex[tg->TriangleNum][i];
                    Token = GetSmdToken();
                    v->Node = (short)smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    v->Position[0] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    v->Position[1] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    v->Position[2] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    v->Normal[0] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    v->Normal[1] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    v->Normal[2] = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    v->TexCoordU = smdStorage_->TokenNumber;
                    Token = GetSmdToken();
                    v->TexCoordV = 1.f - smdStorage_->TokenNumber;
                }
            }
            tg->TriangleNum++;
        }
    }
}

bool SessionRenderUnit::OpenSMDFile(wchar_t *FileName, int Type, bool Flip)
{
    if (FileName == NULL)
        return false;
    if ((smdStorage_->SMDFile = _wfopen(FileName, L"rb")) == NULL)
    {
#ifdef _DEBUG
        wchar_t Text[1024];
        mu_swprintf(Text, L"%ls - File not exist..\r\n", FileName);
        g_ErrorReport.Write(Text);
        MessageBox(g_hWnd, Text, NULL, MB_OK);
#endif
        return false;
    }

    GetSmdToken();
    GetSmdToken();

    if (Type == REFERENCE_FRAME)
    {
        ParseNodes();
        ParseTriangles(Flip);
    }
    if (Type == SKELETAL_ANIMATION)
    {
        ParseSkeleton();
    }

    fclose(smdStorage_->SMDFile);
    return true;
}

bool SessionRenderUnit::OpenSMDModel(int ID, wchar_t *FileName1, int Actions, bool Flip)
{
    if (Models[ID].NumMeshs > 0)
        return false;

    if (OpenSMDFile(FileName1, REFERENCE_FRAME, Flip))
    {
        WideCharToMultiByte(CP_UTF8, 0, FileName1, wcslen(FileName1), Models[ID].Name, 32, 0, 0);
        Models[ID].Version = 10;
        FixupSMD();
        SMD2BMDModel(ID, Actions);
        return true;
    }
    return false;
}

bool SessionRenderUnit::OpenSMDAnimation(int ID, wchar_t *FileName2, bool LockPosition)
{
    if (Models[ID].NumBones > 0)
    {
        if (OpenSMDFile(FileName2, SKELETAL_ANIMATION, false))
        {
            SMD2BMDAnimation(ID, LockPosition);
            return true;
        }
    }
    return false;
}

void RenderColor(float x, float y, float Width, float Height, float Alpha, int Flag);

extern void MoveCharacter(CHARACTER *c, OBJECT *o);

/*
void CChatRoomSocketList::ProcessSocketMessage(DWORD dwSocketID, WORD wMessage)
{
    CHATROOM_SOCKET * pChatroomSocket = GetChatRoomSocketData(GetChatRoomSocketID(dwSocketID));
    if (pChatroomSocket == NULL) return;
    Connection* pSocketClient = &pChatroomSocket->m_WSClient;

    if (pSocketClient == NULL)
    {
        return;
    }
    switch(wMessage)
    {
    case FD_CONNECT:
        break;
    case FD_READ :
        // pSocketClient->nRecv();
        break;
    case FD_WRITE :
        // pSocketClient->FDWriteSend();
        break;
    case FD_CLOSE :
        CUIChatWindow * pWindow = (CUIChatWindow *)g_pWindowMgr->GetWindow(pChatroomSocket->m_dwWindowUIID);
        if (pWindow != NULL)
            pWindow->AddChatText(255, I18N::Game::YouAreDisconnectedFromTheServer, 1, 0);
        pSocketClient->Close();
        break;
    }
}

//void CChatRoomSocketList::ProtocolCompile()
//{
//	// TODO: Change that
//	for (m_ChatRoomSocketMapIter = m_ChatRoomSocketMap.begin(); m_ChatRoomSocketMapIter != m_ChatRoomSocketMap.end(); ++m_ChatRoomSocketMapIter)
//	{
//		ProtocolCompiler(&m_ChatRoomSocketMapIter->second->m_WSClient, 1, m_ChatRoomSocketMapIter->second->m_dwWindowUIID);
//	}
//}
*/

void SessionRenderUnit::OpenModel(int Type, wchar_t *Dir, wchar_t *ModelFileName, ...)
{
    va_list Marker;
    va_start(Marker, ModelFileName);
    OpenModel(Type, Dir, ModelFileName, Marker);
    va_end(Marker);
}

void SessionRenderUnit::OpenModel(int Type, wchar_t *Dir, wchar_t *ModelFileName, va_list Marker)
{
    smdStorage_ = std::make_unique<SessionSmdStorage>();
    wchar_t ModelName[100];
    wchar_t AnimationName[20][100];
    wcscpy(ModelName, Dir);
    wcscat(ModelName, ModelFileName);

    int AnimationCount = 0;
    while (1)
    {
        wchar_t *Temp = va_arg(Marker, wchar_t *);
        if (Temp == nullptr || wcscmp(Temp, L"end") == 0)
        {
            break;
        }
        else
        {
            wcscpy(AnimationName[AnimationCount], Dir);
            wcscat(AnimationName[AnimationCount], Temp);
            AnimationCount++;
        }
    }
    if (AnimationCount == 0)
    {
        OpenSMDModel(Type, ModelName, 1, false);
        OpenSMDAnimation(Type, ModelName);
    }
    else
    {
        OpenSMDModel(Type, ModelName, AnimationCount, false);
        for (int i = 0; i < AnimationCount; i++)
        {
            bool Walk = false;
            if (i == MONSTER01_WALK || i == MONSTER01_RUN)
                Walk = true;
            OpenSMDAnimation(Type, AnimationName[i], Walk);
        }
    }
    smdStorage_.reset();
}

void SessionRenderUnit::OpenModels(int Model, wchar_t *FileName, int i)
{
    smdStorage_ = std::make_unique<SessionSmdStorage>();
    wchar_t Name[64];
    if (i < 10)
        mu_swprintf(Name, L"%ls0%d.smd", FileName, i);
    else
        mu_swprintf(Name, L"%ls%d.smd", FileName, i);
    OpenSMDModel(Model, Name);
    OpenSMDAnimation(Model, Name);
    smdStorage_.reset();
}

void SessionRenderUnit::OpenPlayers()
{
    if (!modelPool_.Allocate())
    {
        return;
    }

    gLoadData.AccessModel(MODEL_PLAYER, L"Data\\Player\\", L"Player");

    // AccessModel has admitted the skeleton/action extents; Player is a skeleton-only root.
    if (Models[MODEL_PLAYER].NumMeshs > 0)
    {
        g_ErrorReport.Write(L"Player.bmd file error.\r\n");
        sessionKeeper_.WorldUnit()->FinishLoad(false);
        throw std::runtime_error("Player skeleton root contains mesh geometry");
    }

    for (int i = 0; i < MAX_CLASS; ++i)
    {
        gLoadData.AccessModel(MODEL_BODY_HELM + i, L"Data\\Player\\", L"HelmClass", i + 1);
        gLoadData.AccessModel(MODEL_BODY_ARMOR + i, L"Data\\Player\\", L"ArmorClass", i + 1);
        gLoadData.AccessModel(MODEL_BODY_PANTS + i, L"Data\\Player\\", L"PantClass", i + 1);
        gLoadData.AccessModel(MODEL_BODY_GLOVES + i, L"Data\\Player\\", L"GloveClass", i + 1);
        gLoadData.AccessModel(MODEL_BODY_BOOTS + i, L"Data\\Player\\", L"BootClass", i + 1);

        if (CLASS_DARK != i && CLASS_DARK_LORD != i && CLASS_RAGEFIGHTER != i)
        {
            gLoadData.AccessModel(MODEL_BODY_HELM + MAX_CLASS + i, L"Data\\Player\\", L"HelmClass2",
                                  i + 1);
            gLoadData.AccessModel(MODEL_BODY_ARMOR + MAX_CLASS + i, L"Data\\Player\\",
                                  L"ArmorClass2", i + 1);
            gLoadData.AccessModel(MODEL_BODY_PANTS + MAX_CLASS + i, L"Data\\Player\\",
                                  L"PantClass2", i + 1);
            gLoadData.AccessModel(MODEL_BODY_GLOVES + MAX_CLASS + i, L"Data\\Player\\",
                                  L"GloveClass2", i + 1);
            gLoadData.AccessModel(MODEL_BODY_BOOTS + MAX_CLASS + i, L"Data\\Player\\",
                                  L"BootClass2", i + 1);
        }

        gLoadData.AccessModel(MODEL_BODY_HELM + (MAX_CLASS * 2) + i, L"Data\\Player\\",
                              L"HelmClass3", i + 1);
        gLoadData.AccessModel(MODEL_BODY_ARMOR + (MAX_CLASS * 2) + i, L"Data\\Player\\",
                              L"ArmorClass3", i + 1);
        gLoadData.AccessModel(MODEL_BODY_PANTS + (MAX_CLASS * 2) + i, L"Data\\Player\\",
                              L"PantClass3", i + 1);
        gLoadData.AccessModel(MODEL_BODY_GLOVES + (MAX_CLASS * 2) + i, L"Data\\Player\\",
                              L"GloveClass3", i + 1);
        gLoadData.AccessModel(MODEL_BODY_BOOTS + (MAX_CLASS * 2) + i, L"Data\\Player\\",
                              L"BootClass3", i + 1);
    }

    for (int i = 0; i < 10; i++)
    {
        gLoadData.AccessModel(MODEL_HELM + i, L"Data\\Player\\", L"HelmMale", i + 1);
        gLoadData.AccessModel(MODEL_ARMOR + i, L"Data\\Player\\", L"ArmorMale", i + 1);
        gLoadData.AccessModel(MODEL_PANTS + i, L"Data\\Player\\", L"PantMale", i + 1);
        gLoadData.AccessModel(MODEL_GLOVES + i, L"Data\\Player\\", L"GloveMale", i + 1);
        gLoadData.AccessModel(MODEL_BOOTS + i, L"Data\\Player\\", L"BootMale", i + 1);
    }

    for (int i = 0; i < 5; i++)
    {
        gLoadData.AccessModel(MODEL_HELM + i + 10, L"Data\\Player\\", L"HelmElf", i + 1);
        gLoadData.AccessModel(MODEL_ARMOR + i + 10, L"Data\\Player\\", L"ArmorElf", i + 1);
        gLoadData.AccessModel(MODEL_PANTS + i + 10, L"Data\\Player\\", L"PantElf", i + 1);
        gLoadData.AccessModel(MODEL_GLOVES + i + 10, L"Data\\Player\\", L"GloveElf", i + 1);
        gLoadData.AccessModel(MODEL_BOOTS + i + 10, L"Data\\Player\\", L"BootElf", i + 1);
    }
    gLoadData.AccessModel(MODEL_STORM_CROW_ARMOR, L"Data\\Player\\", L"ArmorMale", 16);
    gLoadData.AccessModel(MODEL_STORM_CROW_PANTS, L"Data\\Player\\", L"PantMale", 16);
    gLoadData.AccessModel(MODEL_STORM_CROW_GLOVES, L"Data\\Player\\", L"GloveMale", 16);
    gLoadData.AccessModel(MODEL_STORM_CROW_BOOTS, L"Data\\Player\\", L"BootMale", 16);

    gLoadData.AccessModel(MODEL_BLACK_DRAGON_HELM, L"Data\\Player\\", L"HelmMale", 17);
    gLoadData.AccessModel(MODEL_BLACK_DRAGON_ARMOR, L"Data\\Player\\", L"ArmorMale", 17);
    gLoadData.AccessModel(MODEL_BLACK_DRAGON_PANTS, L"Data\\Player\\", L"PantMale", 17);
    gLoadData.AccessModel(MODEL_BLACK_DRAGON_GLOVES, L"Data\\Player\\", L"GloveMale", 17);
    gLoadData.AccessModel(MODEL_BLACK_DRAGON_BOOTS, L"Data\\Player\\", L"BootMale", 17);

    gLoadData.AccessModel(MODEL_MASK_HELM + 0, L"Data\\Player\\", L"MaskHelmMale", 1);
    gLoadData.AccessModel(MODEL_MASK_HELM + 5, L"Data\\Player\\", L"MaskHelmMale", 6);
    gLoadData.AccessModel(MODEL_MASK_HELM + 6, L"Data\\Player\\", L"MaskHelmMale", 7);
    gLoadData.AccessModel(MODEL_MASK_HELM + 8, L"Data\\Player\\", L"MaskHelmMale", 9);
    gLoadData.AccessModel(MODEL_MASK_HELM + 9, L"Data\\Player\\", L"MaskHelmMale", 10);

    for (int i = 0; i < 4; i++)
    {
        if (18 + i == 20)
        {
            gLoadData.AccessModel(MODEL_DARK_PHOENIX_HELM + i, L"Data\\Player\\", L"HelmMaleTest",
                                  18 + i);
            gLoadData.AccessModel(MODEL_DARK_PHOENIX_ARMOR + i, L"Data\\Player\\", L"ArmorMaleTest",
                                  18 + i);
            gLoadData.AccessModel(MODEL_DARK_PHOENIX_PANTS + i, L"Data\\Player\\", L"PantMaleTest",
                                  18 + i);
            gLoadData.AccessModel(MODEL_DARK_PHOENIX_GLOVES + i, L"Data\\Player\\",
                                  L"GloveMaleTest", 18 + i);
            gLoadData.AccessModel(MODEL_DARK_PHOENIX_BOOTS + i, L"Data\\Player\\", L"BootMaleTest",
                                  18 + i);
        }
        else
        {
            if (i < 3)
            {
                gLoadData.AccessModel(MODEL_DARK_PHOENIX_HELM + i, L"Data\\Player\\", L"HelmMale",
                                      18 + i);
            }
            gLoadData.AccessModel(MODEL_DARK_PHOENIX_ARMOR + i, L"Data\\Player\\", L"ArmorMale",
                                  18 + i);
            if (18 + i == 19)
            {
                gLoadData.AccessModel(MODEL_DARK_PHOENIX_PANTS + i, L"Data\\Player\\",
                                      L"t_PantMale", 18 + i);
            }
            else
            {
                gLoadData.AccessModel(MODEL_DARK_PHOENIX_PANTS + i, L"Data\\Player\\", L"PantMale",
                                      18 + i);
            }
            gLoadData.AccessModel(MODEL_DARK_PHOENIX_GLOVES + i, L"Data\\Player\\", L"GloveMale",
                                  18 + i);
            gLoadData.AccessModel(MODEL_DARK_PHOENIX_BOOTS + i, L"Data\\Player\\", L"BootMale",
                                  18 + i);
        }
    }

    for (int i = 0; i < 4; i++)
    {
        if (i != 2)
        {
            gLoadData.AccessModel(MODEL_GREAT_DRAGON_HELM + i, L"Data\\Player\\", L"HelmMale",
                                  22 + i);
        }
        gLoadData.AccessModel(MODEL_GREAT_DRAGON_ARMOR + i, L"Data\\Player\\", L"ArmorMale",
                              22 + i);
        gLoadData.AccessModel(MODEL_GREAT_DRAGON_PANTS + i, L"Data\\Player\\", L"PantMale", 22 + i);
        gLoadData.AccessModel(MODEL_GREAT_DRAGON_GLOVES + i, L"Data\\Player\\", L"GloveMale",
                              22 + i);
        gLoadData.AccessModel(MODEL_GREAT_DRAGON_BOOTS + i, L"Data\\Player\\", L"BootMale", 22 + i);
    }

    for (int i = 0; i < 4; i++)
    {
        gLoadData.AccessModel(MODEL_LIGHT_PLATE_MASK + i, L"Data\\Player\\", L"HelmMale", 26 + i);
        gLoadData.AccessModel(MODEL_LIGHT_PLATE_ARMOR + i, L"Data\\Player\\", L"ArmorMale", 26 + i);
        gLoadData.AccessModel(MODEL_LIGHT_PLATE_PANTS + i, L"Data\\Player\\", L"PantMale", 26 + i);
        gLoadData.AccessModel(MODEL_LIGHT_PLATE_GLOVES + i, L"Data\\Player\\", L"GloveMale",
                              26 + i);
        gLoadData.AccessModel(MODEL_LIGHT_PLATE_BOOTS + i, L"Data\\Player\\", L"BootMale", 26 + i);
    }

    for (int i = 0; i < 5; ++i)
    {
        gLoadData.AccessModel(MODEL_DRAGON_KNIGHT_ARMOR + i, L"Data\\Player\\", L"HDK_ArmorMale",
                              i + 1);
        gLoadData.AccessModel(MODEL_DRAGON_KNIGHT_PANTS + i, L"Data\\Player\\", L"HDK_PantMale",
                              i + 1);
        gLoadData.AccessModel(MODEL_DRAGON_KNIGHT_GLOVES + i, L"Data\\Player\\", L"HDK_GloveMale",
                              i + 1);
        gLoadData.AccessModel(MODEL_DRAGON_KNIGHT_BOOTS + i, L"Data\\Player\\", L"HDK_BootMale",
                              i + 1);
    }

    gLoadData.AccessModel(MODEL_DRAGON_KNIGHT_HELM, L"Data\\Player\\", L"HDK_HelmMale", 1);
    gLoadData.AccessModel(MODEL_VENOM_MIST_HELM, L"Data\\Player\\", L"HDK_HelmMale", 2);
    gLoadData.AccessModel(MODEL_SYLPHID_RAY_HELM, L"Data\\Player\\", L"HDK_HelmMale", 3);
    gLoadData.AccessModel(MODEL_SUNLIGHT_MASK, L"Data\\Player\\", L"HDK_HelmMale", 5);

    for (int i = 0; i < 5; ++i)
    {
        gLoadData.AccessModel(MODEL_ASHCROW_ARMOR + i, L"Data\\Player\\", L"CW_ArmorMale", i + 1);
        gLoadData.AccessModel(MODEL_ASHCROW_PANTS + i, L"Data\\Player\\", L"CW_PantMale", i + 1);
        gLoadData.AccessModel(MODEL_ASHCROW_GLOVES + i, L"Data\\Player\\", L"CW_GloveMale", i + 1);
        gLoadData.AccessModel(MODEL_ASHCROW_BOOTS + i, L"Data\\Player\\", L"CW_BootMale", i + 1);
    }

    //마검사는 제외하고 투구도 추가
    gLoadData.AccessModel(MODEL_ASHCROW_HELM, L"Data\\Player\\", L"CW_HelmMale", 1);
    gLoadData.AccessModel(MODEL_ECLIPSE_HELM, L"Data\\Player\\", L"CW_HelmMale", 2);
    gLoadData.AccessModel(MODEL_IRIS_HELM, L"Data\\Player\\", L"CW_HelmMale", 3);
    gLoadData.AccessModel(MODEL_GLORIOUS_MASK, L"Data\\Player\\", L"CW_HelmMale", 5);

    for (int i = 0; i < 6; ++i)
    {
        gLoadData.AccessModel(MODEL_MISTERY_HELM + i, L"Data\\Player\\", L"HelmMale", 40 + i);
        gLoadData.AccessModel(MODEL_MISTERY_ARMOR + i, L"Data\\Player\\", L"ArmorMale", 40 + i);
        gLoadData.AccessModel(MODEL_MISTERY_PANTS + i, L"Data\\Player\\", L"PantMale", 40 + i);
        gLoadData.AccessModel(MODEL_MISTERY_GLOVES + i, L"Data\\Player\\", L"GloveMale", 40 + i);
        gLoadData.AccessModel(MODEL_MISTERY_BOOTS + i, L"Data\\Player\\", L"BootMale", 40 + i);
    }

    for (int i = 0; i < MODEL_ITEM_COMMON_NUM; ++i)
    {
        gLoadData.AccessModel(MODEL_HELM2 + i, L"Data\\Player\\", L"HelmElfC", i + 1);
        gLoadData.AccessModel(MODEL_ARMOR2 + i, L"Data\\Player\\", L"ArmorElfC", i + 1);
        gLoadData.AccessModel(MODEL_PANTS2 + i, L"Data\\Player\\", L"PantElfC", i + 1);
        gLoadData.AccessModel(MODEL_GLOVES2 + i, L"Data\\Player\\", L"GloveElfC", i + 1);
        gLoadData.AccessModel(MODEL_BOOTS2 + i, L"Data\\Player\\", L"BootElfC", i + 1);
    }

    for (int i = 45; i <= 53; i++)
    {
        if (i == 47 || i == 48)
            continue;
        gLoadData.AccessModel(MODEL_HELM + i, L"Data\\Player\\", L"HelmMale", i + 1);
    } // for()

    for (int i = 45; i <= 53; i++)
    {
        gLoadData.AccessModel(MODEL_ARMOR + i, L"Data\\Player\\", L"ArmorMale", i + 1);
        gLoadData.AccessModel(MODEL_PANTS + i, L"Data\\Player\\", L"PantMale", i + 1);
        gLoadData.AccessModel(MODEL_GLOVES + i, L"Data\\Player\\", L"GloveMale", i + 1);
        gLoadData.AccessModel(MODEL_BOOTS + i, L"Data\\Player\\", L"BootMale", i + 1);
    } // for()

    for (int i = 0; i < MODEL_ITEM_COMMONCNT_RAGEFIGHTER; ++i)
    {
        gLoadData.AccessModel(MODEL_HELM_MONK + i, L"Data\\Player\\", L"HelmMonk", i + 1);
        gLoadData.AccessModel(MODEL_ARMOR_MONK + i, L"Data\\Player\\", L"ArmorMonk", i + 1);
        gLoadData.AccessModel(MODEL_PANTS_MONK + i, L"Data\\Player\\", L"PantMonk", i + 1);
        gLoadData.AccessModel(MODEL_BOOTS_MONK + i, L"Data\\Player\\", L"BootMonk", i + 1);
    }

    for (int i = 0; i < 3; ++i)
    {
        gLoadData.AccessModel(MODEL_SACRED_HELM + i, L"Data\\Player\\", L"HelmMale", 60 + i);
        gLoadData.AccessModel(MODEL_SACRED_ARMOR + i, L"Data\\Player\\", L"ArmorMale", 60 + i);
        gLoadData.AccessModel(MODEL_SACRED_PANTS + i, L"Data\\Player\\", L"PantMale", 60 + i);
        gLoadData.AccessModel(MODEL_SACRED_BOOTS + i, L"Data\\Player\\", L"BootMale", 60 + i);
    }

    gLoadData.AccessModel(MODEL_PHOENIX_SOUL_HELMET, L"Data\\Player\\", L"HelmMale74", -1);
    gLoadData.AccessModel(MODEL_PHOENIX_SOUL_ARMOR, L"Data\\Player\\", L"ArmorMale74", -1);
    gLoadData.AccessModel(MODEL_PHOENIX_SOUL_PANTS, L"Data\\Player\\", L"PantMale74", -1);
    gLoadData.AccessModel(MODEL_PHOENIX_SOUL_BOOTS, L"Data\\Player\\", L"BootMale74", -1);

    for (int i = 0; i < 1; i++)
        gLoadData.AccessModel(MODEL_SHADOW_BODY + i, L"Data\\Player\\", L"Shadow", i + 1);

    Models[MODEL_PLAYER].BoneHead = 20;
    Models[MODEL_PLAYER].BoneFoot[0] = 6;
    Models[MODEL_PLAYER].BoneFoot[1] = 13;

    for (int i = PLAYER_STOP_MALE; i <= PLAYER_STOP_RIDE_WEAPON; i++)
        Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.28f;

    Models[MODEL_PLAYER].Actions[PLAYER_STOP_SWORD].PlaySpeed = 0.26f;
    Models[MODEL_PLAYER].Actions[PLAYER_STOP_TWO_HAND_SWORD].PlaySpeed = 0.24f;
    Models[MODEL_PLAYER].Actions[PLAYER_STOP_SPEAR].PlaySpeed = 0.24f;
    Models[MODEL_PLAYER].Actions[PLAYER_STOP_BOW].PlaySpeed = 0.22f;
    Models[MODEL_PLAYER].Actions[PLAYER_STOP_CROSSBOW].PlaySpeed = 0.22f;
    Models[MODEL_PLAYER].Actions[PLAYER_STOP_SUMMONER].PlaySpeed = 0.24f;
    Models[MODEL_PLAYER].Actions[PLAYER_STOP_WAND].PlaySpeed = 0.30f;

    for (int i = PLAYER_WALK_MALE; i <= PLAYER_RUN_RIDE_WEAPON; i++)
        Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.3f;

    Models[MODEL_PLAYER].Actions[PLAYER_WALK_WAND].PlaySpeed = 0.44f;
    Models[MODEL_PLAYER].Actions[PLAYER_RUN_WAND].PlaySpeed = 0.76f;
    Models[MODEL_PLAYER].Actions[PLAYER_WALK_SWIM].PlaySpeed = 0.35f;
    Models[MODEL_PLAYER].Actions[PLAYER_RUN_SWIM].PlaySpeed = 0.35f;

    for (int i = PLAYER_DEFENSE1; i <= PLAYER_SHOCK; i++)
        Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.32f;

    for (int i = PLAYER_DIE1; i <= PLAYER_DIE2; i++)
        Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.45f;

    for (int i = PLAYER_SIT1; i < MAX_PLAYER_ACTION; i++)
        Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.4f;

    Models[MODEL_PLAYER].Actions[PLAYER_SHOCK].PlaySpeed = 0.4f;
    Models[MODEL_PLAYER].Actions[PLAYER_SEE1].PlaySpeed = 0.28f;
    Models[MODEL_PLAYER].Actions[PLAYER_SEE_FEMALE1].PlaySpeed = 0.28f;
    Models[MODEL_PLAYER].Actions[PLAYER_HEALING1].PlaySpeed = 0.2f;
    Models[MODEL_PLAYER].Actions[PLAYER_HEALING_FEMALE1].PlaySpeed = 0.2f;

    Models[MODEL_PLAYER].Actions[PLAYER_JACK_1].PlaySpeed = 0.38f;
    Models[MODEL_PLAYER].Actions[PLAYER_JACK_2].PlaySpeed = 0.38f;

    Models[MODEL_PLAYER].Actions[PLAYER_SANTA_1].PlaySpeed = 0.34f;
    Models[MODEL_PLAYER].Actions[PLAYER_SANTA_2].PlaySpeed = 0.30f;

    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_RIDER].PlaySpeed = 0.2f;
    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_RIDER_FLY].PlaySpeed = 0.2f;

    Models[MODEL_PLAYER].Actions[PLAYER_STOP_TWO_HAND_SWORD_TWO].PlaySpeed = 0.24f;
    Models[MODEL_PLAYER].Actions[PLAYER_WALK_TWO_HAND_SWORD_TWO].PlaySpeed = 0.3f;
    Models[MODEL_PLAYER].Actions[PLAYER_RUN_TWO_HAND_SWORD_TWO].PlaySpeed = 0.3f;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_TWO_HAND_SWORD_TWO].PlaySpeed = 0.24f;

    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_DEATHSTAB].PlaySpeed = 0.45f;

    Models[MODEL_PLAYER].Actions[PLAYER_DIE1].Loop = true;
    Models[MODEL_PLAYER].Actions[PLAYER_DIE2].Loop = true;
    Models[MODEL_PLAYER].Actions[PLAYER_COME_UP].Loop = true;

    Models[MODEL_PLAYER].Actions[PLAYER_SKILL_HELL_BEGIN].Loop = true;

    Models[MODEL_PLAYER].Actions[PLAYER_DARKLORD_STAND].PlaySpeed = 0.3f;
    Models[MODEL_PLAYER].Actions[PLAYER_DARKLORD_WALK].PlaySpeed = 0.3f;
    Models[MODEL_PLAYER].Actions[PLAYER_STOP_RIDE_HORSE].PlaySpeed = 0.3f;
    Models[MODEL_PLAYER].Actions[PLAYER_RUN_RIDE_HORSE].PlaySpeed = 0.3f;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_STRIKE].PlaySpeed = 0.2f;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_TELEPORT].PlaySpeed = 0.28f;

    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_RIDE_STRIKE].PlaySpeed = 0.3f;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_RIDE_TELEPORT].PlaySpeed = 0.3f;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_RIDE_HORSE_SWORD].PlaySpeed = 0.28f;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_RIDE_ATTACK_FLASH].PlaySpeed = 0.3f;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_RIDE_ATTACK_MAGIC].PlaySpeed = 0.3f;
    Models[MODEL_PLAYER].Actions[PLAYER_ATTACK_DARKHORSE].PlaySpeed = 0.2f;

    Models[MODEL_PLAYER].Actions[PLAYER_IDLE1_DARKHORSE].PlaySpeed = 1.0f;
    Models[MODEL_PLAYER].Actions[PLAYER_IDLE2_DARKHORSE].PlaySpeed = 1.0f;

    for (int i = PLAYER_FENRIR_ATTACK; i <= PLAYER_FENRIR_WALK_ONE_LEFT; i++)
    {
        Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.45f;
    }

    for (int i = PLAYER_FENRIR_RUN; i <= PLAYER_FENRIR_RUN_ONE_LEFT_ELF; i++)
    {
        Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.71f;
    }

    for (int i = PLAYER_FENRIR_STAND; i <= PLAYER_FENRIR_STAND_ONE_LEFT; i++)
    {
        Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.4f;
    }

    Models[MODEL_PLAYER].Actions[PLAYER_FENRIR_ATTACK_MAGIC].PlaySpeed = 0.30f;
    Models[MODEL_PLAYER].Actions[PLAYER_FENRIR_ATTACK_DARKLORD_STRIKE].PlaySpeed = 0.3f;
    Models[MODEL_PLAYER].Actions[PLAYER_FENRIR_ATTACK_DARKLORD_TELEPORT].PlaySpeed = 0.3f;
    Models[MODEL_PLAYER].Actions[PLAYER_FENRIR_ATTACK_DARKLORD_SWORD].PlaySpeed = 0.28f;
    Models[MODEL_PLAYER].Actions[PLAYER_FENRIR_ATTACK_DARKLORD_FLASH].PlaySpeed = 0.3f;

    Models[MODEL_PLAYER].Actions[PLAYER_HIGH_SHOCK].PlaySpeed = 0.3f;

    for (int i = PLAYER_RAGE_FENRIR; i <= PLAYER_RAGE_FENRIR_ATTACK_RIGHT; i++)
    {
        if (i >= PLAYER_RAGE_FENRIR_TWO_SWORD && i <= PLAYER_RAGE_FENRIR_ONE_LEFT)
            Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.225f;
        else
            Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.45f;
    }
    for (int i = PLAYER_RAGE_FENRIR_STAND_TWO_SWORD; i <= PLAYER_RAGE_FENRIR_STAND_ONE_LEFT; i++)
    {
        Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.2f;
    }
    Models[MODEL_PLAYER].Actions[PLAYER_RAGE_FENRIR_STAND].PlaySpeed = 0.21f;

    for (int i = PLAYER_RAGE_FENRIR_RUN; i <= PLAYER_RAGE_FENRIR_RUN_ONE_LEFT; i++)
    {
        Models[MODEL_PLAYER].Actions[i].PlaySpeed = 0.355f;
    }
    Models[MODEL_PLAYER].Actions[PLAYER_RAGE_UNI_RUN].PlaySpeed = 0.3f;
    Models[MODEL_PLAYER].Actions[PLAYER_RAGE_UNI_ATTACK_ONE_RIGHT].PlaySpeed = 0.2f;
    Models[MODEL_PLAYER].Actions[PLAYER_RAGE_UNI_STOP_ONE_RIGHT].PlaySpeed = 0.18f;
    Models[MODEL_PLAYER].Actions[PLAYER_STOP_RAGEFIGHTER].PlaySpeed = 0.16f;
    SetAttackSpeed();

    gLoadData.AccessModel(MODEL_GM_CHARACTER, L"Data\\Skill\\", L"youngza");
}

void SessionRenderUnit::OpenPlayerTextures()
{
    LoadBitmapW(L"Player\\hair_r.jpg", BITMAP_HAIR);
    LoadBitmapW(L"Player\\Robe01.jpg", BITMAP_ROBE);
    LoadBitmapW(L"Player\\Robe02.jpg", BITMAP_ROBE + 1);
    LoadBitmapW(L"Player\\Robe03.tga", BITMAP_ROBE + 2);
    LoadBitmapW(L"Player\\DarklordRobe.tga", BITMAP_ROBE + 7);
    LoadBitmapW(L"Item\\msword03.tga", BITMAP_ROBE + 8);
    LoadBitmapW(L"Item\\dl_redwings02.tga", BITMAP_ROBE + 9);
    LoadBitmapW(L"Item\\dl_redwings03.tga", BITMAP_ROBE + 10);

    int nIndex;

    for (int j = 0; j < MAX_CLASS_STAGES; ++j)
    {
        for (int i = 0; i < MAX_CLASS; ++i)
        {
            nIndex = MAX_CLASS * j + i;
            if (1 == j && (CLASS_DARK == i || CLASS_DARK_LORD == i || CLASS_RAGEFIGHTER == i))
                continue;

            gLoadData.OpenTexture(MODEL_BODY_HELM + nIndex, L"Player\\");
            gLoadData.OpenTexture(MODEL_BODY_ARMOR + nIndex, L"Player\\");
            gLoadData.OpenTexture(MODEL_BODY_PANTS + nIndex, L"Player\\");
            gLoadData.OpenTexture(MODEL_BODY_GLOVES + nIndex, L"Player\\");
            gLoadData.OpenTexture(MODEL_BODY_BOOTS + nIndex, L"Player\\");
        }
    }

    for (int i = 0; i <= CLASS_END; i++)
    {
        gLoadData.OpenTexture(MODEL_HELM + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_ARMOR + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_PANTS + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_GLOVES + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_BOOTS + i, L"Player\\");
    }

    for (int i = 0; i < 4; i++)
    {
        if (i != 2)
        {
            gLoadData.OpenTexture(MODEL_GREAT_DRAGON_HELM + i, L"Player\\");
        }
        gLoadData.OpenTexture(MODEL_GREAT_DRAGON_ARMOR + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_GREAT_DRAGON_PANTS + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_GREAT_DRAGON_GLOVES + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_GREAT_DRAGON_BOOTS + i, L"Player\\");
    }

    for (int i = 0; i < 4; i++)
    {
        gLoadData.OpenTexture(MODEL_LIGHT_PLATE_MASK + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_LIGHT_PLATE_ARMOR + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_LIGHT_PLATE_PANTS + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_LIGHT_PLATE_GLOVES + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_LIGHT_PLATE_BOOTS + i, L"Player\\");
    }

    for (int i = 0; i < 4; i++)
    {
        gLoadData.OpenTexture(MODEL_DARK_PHOENIX_HELM + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_DARK_PHOENIX_ARMOR + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_DARK_PHOENIX_PANTS + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_DARK_PHOENIX_GLOVES + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_DARK_PHOENIX_BOOTS + i, L"Player\\");
    }

    gLoadData.OpenTexture(MODEL_MASK_HELM + 0, L"Player\\");
    gLoadData.OpenTexture(MODEL_MASK_HELM + 5, L"Player\\");
    gLoadData.OpenTexture(MODEL_MASK_HELM + 6, L"Player\\");
    gLoadData.OpenTexture(MODEL_MASK_HELM + 8, L"Player\\");
    gLoadData.OpenTexture(MODEL_MASK_HELM + 9, L"Player\\");

    gLoadData.OpenTexture(MODEL_SHADOW_BODY, L"Player\\");

    for (int i = 0; i < 5; ++i)
    {
        gLoadData.OpenTexture(MODEL_DRAGON_KNIGHT_ARMOR + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_DRAGON_KNIGHT_PANTS + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_DRAGON_KNIGHT_GLOVES + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_DRAGON_KNIGHT_BOOTS + i, L"Player\\");
    }
    gLoadData.OpenTexture(MODEL_DRAGON_KNIGHT_HELM, L"Player\\");
    gLoadData.OpenTexture(MODEL_VENOM_MIST_HELM, L"Player\\");
    gLoadData.OpenTexture(MODEL_SYLPHID_RAY_HELM, L"Player\\");
    gLoadData.OpenTexture(MODEL_SUNLIGHT_MASK, L"Player\\");

    for (int i = 0; i < 5; ++i)
    {
        gLoadData.OpenTexture(MODEL_ASHCROW_ARMOR + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_ASHCROW_PANTS + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_ASHCROW_GLOVES + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_ASHCROW_BOOTS + i, L"Player\\");
    }
    gLoadData.OpenTexture(MODEL_ASHCROW_HELM, L"Player\\");
    gLoadData.OpenTexture(MODEL_ECLIPSE_HELM, L"Player\\");
    gLoadData.OpenTexture(MODEL_IRIS_HELM, L"Player\\");
    gLoadData.OpenTexture(MODEL_GLORIOUS_MASK, L"Player\\");

    wchar_t szFileName[64];

    for (int i = 0; i < 6; ++i)
    {
        gLoadData.OpenTexture(MODEL_MISTERY_HELM + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_MISTERY_ARMOR + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_MISTERY_PANTS + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_MISTERY_GLOVES + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_MISTERY_BOOTS + i, L"Player\\");

        ::mu_swprintf(szFileName, L"Player\\InvenArmorMale%d.tga", 40 + i);
        LoadBitmapW(szFileName, BITMAP_INVEN_ARMOR + i);
        ::mu_swprintf(szFileName, L"Player\\InvenPantsMale%d.tga", 40 + i);
        LoadBitmapW(szFileName, BITMAP_INVEN_PANTS + i);
    }

    ::mu_swprintf(szFileName, L"Player\\Item312_Armoritem.tga");
    LoadBitmapW(szFileName, BITMAP_SKIN_ARMOR_DEVINE);
    ::mu_swprintf(szFileName, L"Player\\Item312_Pantitem.tga");
    LoadBitmapW(szFileName, BITMAP_SKIN_PANTS_DEVINE);
    ::mu_swprintf(szFileName, L"Player\\SkinClass706_upperitem.tga");
    LoadBitmapW(szFileName, BITMAP_SKIN_ARMOR_SUCCUBUS);
    ::mu_swprintf(szFileName, L"Player\\SkinClass706_loweritem.tga");
    LoadBitmapW(szFileName, BITMAP_SKIN_PANTS_SUCCUBUS);

    for (int i = 0; i < MODEL_ITEM_COMMON_NUM; ++i)
    {
        gLoadData.OpenTexture(MODEL_HELM2 + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_ARMOR2 + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_PANTS2 + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_GLOVES2 + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_BOOTS2 + i, L"Player\\");
    }

    for (int i = 45; i <= 53; i++)
    {
        if (i == 47 || i == 48)
            continue;

        gLoadData.OpenTexture(MODEL_HELM + i, L"Player\\");
    } // for()

    for (int i = 45; i <= 53; i++)
    {
        gLoadData.OpenTexture(MODEL_ARMOR + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_PANTS + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_GLOVES + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_BOOTS + i, L"Player\\");
    } // for()

    gLoadData.OpenTexture(MODEL_GM_CHARACTER, L"Skill\\");

    for (int i = 0; i < MODEL_ITEM_COMMONCNT_RAGEFIGHTER; ++i)
    {
        gLoadData.OpenTexture(MODEL_HELM_MONK + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_ARMOR_MONK + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_PANTS_MONK + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_BOOTS_MONK + i, L"Player\\");
    }

    for (int i = 0; i < 3; ++i)
    {
        gLoadData.OpenTexture(MODEL_SACRED_HELM + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_SACRED_ARMOR + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_SACRED_PANTS + i, L"Player\\");
        gLoadData.OpenTexture(MODEL_SACRED_BOOTS + i, L"Player\\");
    }
    gLoadData.OpenTexture(MODEL_PHOENIX_SOUL_HELMET, L"Player\\");
    gLoadData.OpenTexture(MODEL_PHOENIX_SOUL_ARMOR, L"Player\\");
    gLoadData.OpenTexture(MODEL_PHOENIX_SOUL_PANTS, L"Player\\");
    gLoadData.OpenTexture(MODEL_PHOENIX_SOUL_BOOTS, L"Player\\");
}

void SessionRenderUnit::OpenItems()
{
    //  MODEL_SWORD

    for (int i = 0; i < 17; i++)
        gLoadData.AccessModel(MODEL_SWORD + i, L"Data\\Item\\", L"Sword", i + 1);

    gLoadData.AccessModel(MODEL_DARK_BREAKER, L"Data\\Item\\", L"Sword", 18);
    gLoadData.AccessModel(MODEL_THUNDER_BLADE, L"Data\\Item\\", L"Sword", 19);
    gLoadData.AccessModel(MODEL_DIVINE_SWORD_OF_ARCHANGEL, L"Data\\Item\\", L"Sword", 20);

    gLoadData.AccessModel(MODEL_KNIGHT_BLADE, L"Data\\Item\\", L"Sword", 21);
    gLoadData.AccessModel(MODEL_DARK_REIGN_BLADE, L"Data\\Item\\", L"Sword", 22);
    gLoadData.AccessModel(MODEL_RUNE_BLADE, L"Data\\Item\\", L"Sword", 32);

    //	MODEL_AXE

    for (int i = 0; i < 9; i++)
        gLoadData.AccessModel(MODEL_AXE + i, L"Data\\Item\\", L"Axe", i + 1);

    //	MODEL_MACE

    for (int i = 0; i < 7; i++)
        gLoadData.AccessModel(MODEL_MACE + i, L"Data\\Item\\", L"Mace", i + 1);

    gLoadData.AccessModel(MODEL_ELEMENTAL_MACE, L"Data\\Item\\", L"Mace", 8);

    // MODEL_MACE+8,9,10,11,12
    for (int i = 0; i < 5; i++)
        gLoadData.AccessModel(MODEL_BATTLE_SCEPTER + i, L"Data\\Item\\", L"Mace", 9 + i);

    gLoadData.AccessModel(MODEL_DIVINE_SCEPTER_OF_ARCHANGEL, L"Data\\Item\\", L"Saint");

    //	MODEL_SPEAR

    for (int i = 0; i < 10; i++)
        gLoadData.AccessModel(MODEL_SPEAR + i, L"Data\\Item\\", L"Spear", i + 1);

    gLoadData.AccessModel(MODEL_DRAGON_SPEAR, L"Data\\Item\\", L"Spear", 11);

    //	MODEL_SHIELD

    for (int i = 0; i < 15; i++)
        gLoadData.AccessModel(MODEL_SHIELD + i, L"Data\\Item\\", L"Shield", i + 1);

    gLoadData.AccessModel(MODEL_GRAND_SOUL_SHIELD, L"Data\\Item\\", L"Shield", 16);
    gLoadData.AccessModel(MODEL_ELEMENTAL_SHIELD, L"Data\\Item\\", L"Shield", 17);

    //  MODEL_STAFF

    for (int i = 0; i < 9; i++)
        gLoadData.AccessModel(MODEL_STAFF + i, L"Data\\Item\\", L"Staff", i + 1);

    gLoadData.AccessModel(MODEL_DRAGON_SOUL_STAFF, L"Data\\Item\\", L"Staff", 10);
    gLoadData.AccessModel(MODEL_DIVINE_STAFF_OF_ARCHANGEL, L"Data\\Item\\", L"Staff", 11);
    gLoadData.AccessModel(MODEL_DIVINE_STICK_OF_ARCHANGEL, L"Data\\Item\\", L"Archangelus");
    gLoadData.AccessModel(MODEL_STAFF_OF_KUNDUN, L"Data\\Item\\", L"Staff", 12);

    for (int i = 14; i <= 20; ++i)
        gLoadData.AccessModel(MODEL_STAFF + i, L"Data\\Item\\", L"Staff", i + 1);

    //  MODEL_BOW

    for (int i = 0; i < 7; i++)
        gLoadData.AccessModel(MODEL_BOW + i, L"Data\\Item\\", L"Bow", i + 1);

    for (int i = 0; i < 7; i++)
        gLoadData.AccessModel(MODEL_BOW + i + 8, L"Data\\Item\\", L"CrossBow", i + 1);

    gLoadData.AccessModel(MODEL_BOLT, L"Data\\Item\\", L"Arrows", 1);
    gLoadData.AccessModel(MODEL_ARROWS, L"Data\\Item\\", L"Arrows", 2);
    gLoadData.AccessModel(MODEL_SAINT_CROSSBOW, L"Data\\Item\\", L"CrossBow", 17);
    gLoadData.AccessModel(MODEL_CELESTIAL_BOW, L"Data\\Item\\", L"Bow", 18);
    gLoadData.AccessModel(MODEL_DIVINE_CB_OF_ARCHANGEL, L"Data\\Item\\", L"Bow", 19);
    gLoadData.AccessModel(MODEL_GREAT_REIGN_CROSSBOW, L"Data\\Item\\", L"CrossBow", 20);
    gLoadData.AccessModel(MODEL_ARROW_VIPER_BOW, L"Data\\Item\\", L"Bow", 20);

    //  MODEL_HELPER

    for (int i = 0; i < 3; i++)
        gLoadData.AccessModel(MODEL_HELPER + i, L"Data\\Player\\", L"Helper", i + 1);

    for (int i = 0; i < 2; i++)
        gLoadData.AccessModel(MODEL_HELPER + i + 8, L"Data\\Item\\", L"Ring", i + 1);

    LoadChangeRingItemModels();

    for (int i = 0; i < 2; i++)
        gLoadData.AccessModel(MODEL_HELPER + i + 12, L"Data\\Item\\", L"Necklace", i + 1);

    gLoadData.AccessModel(MODEL_HORN_OF_DINORANT, L"Data\\Player\\", L"Helper", 4);
    gLoadData.AccessModel(MODEL_DARK_HORSE_ITEM, L"Data\\Item\\", L"DarkHorseHorn");
    gLoadData.AccessModel(MODEL_SPIRIT, L"Data\\Item\\", L"DarkHorseSoul");
    gLoadData.AccessModel(MODEL_DARK_RAVEN_ITEM, L"Data\\Item\\", L"SpiritBill");

    gLoadData.AccessModel(MODEL_RING_OF_FIRE, L"Data\\Item\\", L"FireRing");
    gLoadData.AccessModel(MODEL_RING_OF_EARTH, L"Data\\Item\\", L"GroundRing");
    gLoadData.AccessModel(MODEL_RING_OF_WIND, L"Data\\Item\\", L"WindRing");
    gLoadData.AccessModel(MODEL_RING_OF_MAGIC, L"Data\\Item\\", L"ManaRing");
    gLoadData.AccessModel(MODEL_PENDANT_OF_ICE, L"Data\\Item\\", L"IceNecklace");
    gLoadData.AccessModel(MODEL_PENDANT_OF_WIND, L"Data\\Item\\", L"WindNecklace");
    gLoadData.AccessModel(MODEL_PENDANT_OF_WATER, L"Data\\Item\\", L"WaterNecklace");
    gLoadData.AccessModel(MODEL_PENDANT_OF_ABILITY, L"Data\\Item\\", L"AgNecklace");
    gLoadData.AccessModel(MODEL_ARMOR_OF_GUARDSMAN, L"Data\\Item\\", L"EventChaosCastle");
    gLoadData.AccessModel(MODEL_HELPER + 7, L"Data\\Item\\", L"Covenant");
    gLoadData.AccessModel(MODEL_LIFE_STONE_ITEM, L"Data\\Item\\", L"SummonBook");
    gLoadData.AccessModel(MODEL_EVENT + 18, L"Data\\Item\\", L"LifeStoneItem");

    gLoadData.AccessModel(MODEL_SPLINTER_OF_ARMOR, L"Data\\Item\\", L"FR_1");
    gLoadData.AccessModel(MODEL_BLESS_OF_GUARDIAN, L"Data\\Item\\", L"FR_2");
    gLoadData.AccessModel(MODEL_CLAW_OF_BEAST, L"Data\\Item\\", L"FR_3");
    gLoadData.AccessModel(MODEL_FRAGMENT_OF_HORN, L"Data\\Item\\", L"FR_4");
    gLoadData.AccessModel(MODEL_BROKEN_HORN, L"Data\\Item\\", L"FR_5");
    gLoadData.AccessModel(MODEL_HORN_OF_FENRIR, L"Data\\Item\\", L"FR_6");

    gLoadData.AccessModel(MODEL_HELPER + 46, L"Data\\Item\\partCharge1\\", L"entrancegray");
    gLoadData.AccessModel(MODEL_HELPER + 47, L"Data\\Item\\partCharge1\\", L"entrancered");
    gLoadData.AccessModel(MODEL_HELPER + 48, L"Data\\Item\\partCharge1\\", L"entrancebleu");
    gLoadData.AccessModel(MODEL_POTION + 54, L"Data\\Item\\partCharge1\\", L"juju");
    gLoadData.AccessModel(MODEL_HELPER + 43, L"Data\\Item\\partCharge1\\", L"monmark01");
    gLoadData.AccessModel(MODEL_HELPER + 44, L"Data\\Item\\partCharge1\\", L"monmark02");
    gLoadData.AccessModel(MODEL_HELPER + 45, L"Data\\Item\\partCharge1\\", L"monmark03");
    gLoadData.AccessModel(MODEL_POTION + 53, L"Data\\Item\\partCharge1\\", L"bujuck01");

    gLoadData.AccessModel(MODEL_POTION + 58, L"Data\\Item\\partCharge1\\", L"expensiveitem01");
    gLoadData.AccessModel(MODEL_POTION + 59, L"Data\\Item\\partCharge1\\", L"expensiveitem02a");
    gLoadData.AccessModel(MODEL_POTION + 60, L"Data\\Item\\partCharge1\\", L"expensiveitem02b");
    gLoadData.AccessModel(MODEL_POTION + 61, L"Data\\Item\\partCharge1\\", L"expensiveitem03a");
    gLoadData.AccessModel(MODEL_POTION + 62, L"Data\\Item\\partCharge1\\", L"expensiveitem03b");
    gLoadData.AccessModel(MODEL_POTION + 70, L"Data\\Item\\partCharge2\\", L"EPotionR");
    gLoadData.AccessModel(MODEL_POTION + 71, L"Data\\Item\\partCharge2\\", L"EPotionB");
    gLoadData.AccessModel(MODEL_POTION + 72, L"Data\\Item\\partCharge2\\", L"elitescroll_quick");
    gLoadData.AccessModel(MODEL_POTION + 73, L"Data\\Item\\partCharge2\\", L"elitescroll_depence");
    gLoadData.AccessModel(MODEL_POTION + 74, L"Data\\Item\\partCharge2\\", L"elitescroll_anger");
    gLoadData.AccessModel(MODEL_POTION + 75, L"Data\\Item\\partCharge2\\", L"elitescroll_magic");
    gLoadData.AccessModel(MODEL_POTION + 76, L"Data\\Item\\partCharge2\\", L"elitescroll_strenth");
    gLoadData.AccessModel(MODEL_POTION + 77, L"Data\\Item\\partCharge2\\", L"elitescroll_mana");

    wchar_t szPC6Path[24];
    mu_swprintf(szPC6Path, L"Data\\Item\\partCharge6\\");

    gLoadData.AccessModel(static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_KNIGHT_1_CHARM,
                          szPC6Path, L"amulet_satan");
    gLoadData.AccessModel(static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_MAGICIAN_1_CHARM,
                          szPC6Path, L"amulet_sky");
    gLoadData.AccessModel(static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_ELF_1_CHARM, szPC6Path,
                          L"amulet_elf");
    gLoadData.AccessModel(static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_SUMMONER_1_CHARM,
                          szPC6Path, L"amulet_disaster");
    gLoadData.AccessModel(static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_DARKLORD_1_CHARM,
                          szPC6Path, L"amulet_cloak");
    gLoadData.AccessModel(static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_KNIGHT_2_CHARM,
                          szPC6Path, L"amulet_dragon");
    gLoadData.AccessModel(static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_MAGICIAN_2_CHARM,
                          szPC6Path, L"amulet_soul");
    gLoadData.AccessModel(static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_ELF_2_CHARM, szPC6Path,
                          L"amulet_spirit");
    gLoadData.AccessModel(static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_SUMMONER_2_CHARM,
                          szPC6Path, L"amulet_despair");
    gLoadData.AccessModel(static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_DARKKNIGHT_2_CHARM,
                          szPC6Path, L"amulet_dark");
    gLoadData.AccessModel(MODEL_HELPER + 59, L"Data\\Item\\partCharge2\\", L"sealmove");
    gLoadData.AccessModel(MODEL_HELPER + 54, L"Data\\Item\\partCharge2\\", L"resetfruit_power");
    gLoadData.AccessModel(MODEL_HELPER + 55, L"Data\\Item\\partCharge2\\", L"resetfruit_quick");
    gLoadData.AccessModel(MODEL_HELPER + 56, L"Data\\Item\\partCharge2\\", L"resetfruit_strenth");
    gLoadData.AccessModel(MODEL_HELPER + 57, L"Data\\Item\\partCharge2\\", L"resetfruit_energe");
    gLoadData.AccessModel(MODEL_HELPER + 58, L"Data\\Item\\partCharge2\\", L"resetfruit_command");
    gLoadData.AccessModel(MODEL_POTION + 78, L"Data\\Item\\partCharge2\\", L"secret_power");
    gLoadData.AccessModel(MODEL_POTION + 79, L"Data\\Item\\partCharge2\\", L"secret_quick");
    gLoadData.AccessModel(MODEL_POTION + 80, L"Data\\Item\\partCharge2\\", L"secret_strenth");
    gLoadData.AccessModel(MODEL_POTION + 81, L"Data\\Item\\partCharge2\\", L"secret_energe");
    gLoadData.AccessModel(MODEL_POTION + 82, L"Data\\Item\\partCharge2\\", L"secret_command");
    gLoadData.AccessModel(MODEL_HELPER + 60, L"Data\\Item\\partCharge2\\", L"indulgence");
    gLoadData.AccessModel(MODEL_HELPER + 61, L"Data\\Item\\partCharge2\\", L"entrancepurple");
    gLoadData.AccessModel(MODEL_POTION + 83, L"Data\\Item\\partCharge2\\", L"expensiveitem04b");
    gLoadData.AccessModel(MODEL_POTION + 145, L"Data\\Item\\partCharge8\\", L"rareitem_ticket7");
    gLoadData.AccessModel(MODEL_POTION + 146, L"Data\\Item\\partCharge8\\", L"rareitem_ticket8");
    gLoadData.AccessModel(MODEL_POTION + 147, L"Data\\Item\\partCharge8\\", L"rareitem_ticket9");
    gLoadData.AccessModel(MODEL_POTION + 148, L"Data\\Item\\partCharge8\\", L"rareitem_ticket10");
    gLoadData.AccessModel(MODEL_POTION + 149, L"Data\\Item\\partCharge8\\", L"rareitem_ticket11");
    gLoadData.AccessModel(MODEL_POTION + 150, L"Data\\Item\\partCharge8\\", L"rareitem_ticket12");
    gLoadData.AccessModel(MODEL_HELPER + 125, L"Data\\Item\\partCharge8\\", L"DoppelCard");
    gLoadData.AccessModel(MODEL_HELPER + 126, L"Data\\Item\\partCharge8\\", L"BarcaCard");
    gLoadData.AccessModel(MODEL_HELPER + 127, L"Data\\Item\\partCharge8\\", L"Barca7Card");

#ifdef LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM
    gLoadData.AccessModel(MODEL_HELPER + 128, L"Data\\Item\\", L"HawkStatue");
    gLoadData.AccessModel(MODEL_HELPER + 129, L"Data\\Item\\", L"SheepStatue");
    gLoadData.AccessModel(MODEL_HELPER + 134, L"Data\\Item\\", L"horseshoe");
#endif //LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM
#ifdef LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM_PART_2
    gLoadData.AccessModel(MODEL_HELPER + 130, L"Data\\Item\\", L"ork_cham");
    //	gLoadData.AccessModel(MODEL_HELPER+131, L"Data\\Item\\", L"maple_cham");
    gLoadData.AccessModel(MODEL_HELPER + 132, L"Data\\Item\\", L"goldenork_cham");
    //	gLoadData.AccessModel(MODEL_HELPER+132, L"Data\\Item\\", L"goldenmaple_cham");
#endif //LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM_PART_2

    gLoadData.AccessModel(MODEL_POTION + 91, L"Data\\Item\\partCharge3\\", L"alicecard");

    gLoadData.AccessModel(MODEL_POTION + 92, L"Data\\Item\\partCharge3\\", L"juju");
    gLoadData.AccessModel(MODEL_POTION + 93, L"Data\\Item\\partCharge3\\", L"juju");
    gLoadData.AccessModel(MODEL_POTION + 95, L"Data\\Item\\partCharge3\\", L"juju");

    gLoadData.AccessModel(MODEL_POTION + 94, L"Data\\Item\\partCharge2\\", L"EPotionR");
    gLoadData.AccessModel(MODEL_CHERRY_BLOSSOM_PLAYBOX, L"Data\\Item\\cherryblossom\\",
                          L"cherrybox");
    gLoadData.AccessModel(MODEL_CHERRY_BLOSSOM_WINE, L"Data\\Item\\cherryblossom\\", L"chwine");
    gLoadData.AccessModel(MODEL_CHERRY_BLOSSOM_RICE_CAKE, L"Data\\Item\\cherryblossom\\",
                          L"chgateaux");
    gLoadData.AccessModel(MODEL_CHERRY_BLOSSOM_FLOWER_PETAL, L"Data\\Item\\cherryblossom\\",
                          L"chpetal");
    gLoadData.AccessModel(MODEL_POTION + 88, L"Data\\Item\\cherryblossom\\", L"chbranche");
    gLoadData.AccessModel(MODEL_POTION + 89, L"Data\\Item\\cherryblossom\\", L"chbranche_red");
    gLoadData.AccessModel(MODEL_GOLDEN_CHERRY_BLOSSOM_BRANCH, L"Data\\Item\\cherryblossom\\",
                          L"chbranche_yellow");
    gLoadData.AccessModel(MODEL_HELPER + 62, L"Data\\Item\\partCharge4\\", L"Curemark");
    gLoadData.AccessModel(MODEL_HELPER + 63, L"Data\\Item\\partCharge4\\", L"Holinessmark");
    gLoadData.AccessModel(MODEL_POTION + 97, L"Data\\Item\\partCharge4\\", L"battlescroll");
    gLoadData.AccessModel(MODEL_POTION + 98, L"Data\\Item\\partCharge4\\", L"strengscroll");

    gLoadData.AccessModel(MODEL_POTION + 96, L"Data\\Item\\partCharge4\\", L"strengamulet");

    gLoadData.AccessModel(MODEL_DEMON, L"Data\\Item\\partCharge4\\", L"demon");
    gLoadData.AccessModel(MODEL_SPIRIT_OF_GUARDIAN, L"Data\\Item\\partCharge4\\", L"maria");

    gLoadData.AccessModel(MODEL_PET_PANDA, L"Data\\Item\\", L"PandaPet");
    gLoadData.AccessModel(MODEL_PET_RUDOLF, L"Data\\Item\\xmas\\", L"xmas_deer");
    gLoadData.AccessModel(MODEL_PET_UNICORN, L"Data\\Item\\partcharge7\\", L"pet_unicorn");
    gLoadData.AccessModel(MODEL_PET_SKELETON, L"Data\\Item\\", L"skeletonpet");

    gLoadData.AccessModel(MODEL_HELPER + 69, L"Data\\Item\\partCharge5\\", L"ressurection");
    gLoadData.AccessModel(MODEL_HELPER + 70, L"Data\\Item\\partCharge5\\", L"potalcharm");
    gLoadData.AccessModel(MODEL_HELPER + 81, L"Data\\Item\\partCharge6\\", L"suhocham01");
    gLoadData.AccessModel(MODEL_HELPER + 82, L"Data\\Item\\partCharge6\\", L"imteam_protect");
    gLoadData.AccessModel(MODEL_HELPER + 93, L"Data\\Item\\partCharge6\\", L"MasterSealA");

    gLoadData.AccessModel(MODEL_HELPER + 94, L"Data\\Item\\partCharge6\\", L"MasterSealB");
    gLoadData.AccessModel(MODEL_POTION + 140, L"Data\\Item\\", L"strengscroll");

    gLoadData.AccessModel(MODEL_OLD_SCROLL, L"Data\\Item\\", L"scrollpaper");
    gLoadData.AccessModel(MODEL_ILLUSION_SORCERER_COVENANT, L"Data\\Item\\", L"oath");
    gLoadData.AccessModel(MODEL_SCROLL_OF_BLOOD, L"Data\\Item\\", L"songbl");

    gLoadData.AccessModel(MODEL_FLAME_OF_CONDOR, L"Data\\Item\\", L"condolstone");
    gLoadData.AccessModel(MODEL_FEATHER_OF_CONDOR, L"Data\\Item\\", L"condolwing");

    gLoadData.AccessModel(MODEL_POTION + 64, L"Data\\Item\\", L"songss");
    gLoadData.AccessModel(MODEL_FLAME_OF_DEATH_BEAM_KNIGHT, L"Data\\Item\\", L"deathbeamstone");
    gLoadData.AccessModel(MODEL_HORN_OF_HELL_MAINE, L"Data\\Item\\", L"hellhorn");
    gLoadData.AccessModel(MODEL_FEATHER_OF_DARK_PHOENIX, L"Data\\Item\\", L"phoenixfeather");
    gLoadData.AccessModel(MODEL_EYE_OF_ABYSSAL, L"Data\\Item\\", L"Deye");

    for (int i = 0; i < 6; ++i)
        gLoadData.AccessModel(MODEL_SEED_FIRE + i, L"Data\\Item\\", L"s30_seed");
    for (int i = 0; i < 5; ++i)
        gLoadData.AccessModel(MODEL_SPHERE_MONO + i, L"Data\\Item\\", L"s30_sphere_body", i + 1);
    for (int i = 0; i < 5; ++i)
    {
        for (int j = 0; j < 6; ++j)
            gLoadData.AccessModel(MODEL_SEED_SPHERE_FIRE_1 + i * 6 + j, L"Data\\Item\\",
                                  L"s30_sphere", i + 1);
    }

    gLoadData.AccessModel(MODEL_HELPER + 107, L"Data\\Item\\partcharge7\\", L"FatalRing");
    gLoadData.AccessModel(MODEL_HELPER + 104, L"Data\\Item\\partcharge7\\", L"injang_AG");
    gLoadData.AccessModel(MODEL_HELPER + 105, L"Data\\Item\\partcharge7\\", L"injang_SD");
    gLoadData.AccessModel(MODEL_HELPER + 103, L"Data\\Item\\partcharge7\\", L"EXPscroll");
    gLoadData.AccessModel(MODEL_POTION + 133, L"Data\\Item\\partcharge7\\", L"ESDPotion");

    gLoadData.AccessModel(MODEL_HELPER + 109, L"Data\\Item\\InGameShop\\", L"PeriodRingBlue");

    gLoadData.AccessModel(MODEL_HELPER + 110, L"Data\\Item\\InGameShop\\", L"PeriodRingRed");
    gLoadData.AccessModel(MODEL_HELPER + 111, L"Data\\Item\\InGameShop\\", L"PeriodRingYellow");
    gLoadData.AccessModel(MODEL_HELPER + 112, L"Data\\Item\\InGameShop\\", L"PeriodRingViolet");
    gLoadData.AccessModel(MODEL_HELPER + 113, L"Data\\Item\\InGameShop\\", L"necklace_red");
    gLoadData.AccessModel(MODEL_HELPER + 114, L"Data\\Item\\InGameShop\\", L"necklace_blue");
    gLoadData.AccessModel(MODEL_HELPER + 115, L"Data\\Item\\InGameShop\\", L"necklace_green");

    gLoadData.AccessModel(MODEL_HELPER + 116, L"Data\\Item\\", L"monmark02");

    gLoadData.AccessModel(MODEL_WING + 130, L"Data\\Item\\", L"DarkLordRobe");
    gLoadData.AccessModel(MODEL_WING + 131, L"Data\\Item\\Ingameshop\\", L"alice1wing");
    gLoadData.AccessModel(MODEL_WING + 132, L"Data\\Item\\Ingameshop\\", L"elf_wing");
    gLoadData.AccessModel(MODEL_WING + 133, L"Data\\Item\\Ingameshop\\", L"angel_wing");
    gLoadData.AccessModel(MODEL_WING + 134, L"Data\\Item\\Ingameshop\\", L"devil_wing");
    gLoadData.AccessModel(MODEL_HELPER + 124, L"Data\\Item\\partCharge6\\", L"ChannelCard");

    for (int i = 0; i < 7; i++)
        gLoadData.AccessModel(MODEL_POTION + i, L"Data\\Item\\", L"Potion", i + 1);

    gLoadData.AccessModel(MODEL_ANTIDOTE, L"Data\\Item\\", L"Antidote", 1);
    gLoadData.AccessModel(MODEL_ALE, L"Data\\Item\\", L"Beer", 1);
    gLoadData.AccessModel(MODEL_TOWN_PORTAL_SCROLL, L"Data\\Item\\", L"Scroll", 1);
    gLoadData.AccessModel(MODEL_BOX_OF_LUCK, L"Data\\Item\\", L"MagicBox", 1);
    gLoadData.AccessModel(MODEL_POTION + 12, L"Data\\Item\\", L"Event", 1);

    for (int i = 0; i < 2; i++)
        gLoadData.AccessModel(MODEL_POTION + i + 13, L"Data\\Item\\", L"Jewel", i + 1);

    gLoadData.AccessModel(MODEL_POTION + 15, L"Data\\Item\\", L"Gold", 1);
    gLoadData.AccessModel(MODEL_JEWEL_OF_LIFE, L"Data\\Item\\", L"Jewel", 3);

    for (int i = 0; i < 3; i++)
        gLoadData.AccessModel(MODEL_DEVILS_EYE + i, L"Data\\Item\\", L"Devil", i);

    gLoadData.AccessModel(MODEL_POTION + 20, L"Data\\Item\\", L"Drink", 0);
    gLoadData.AccessModel(MODEL_POTION + 21, L"Data\\Item\\", L"ConChip", 0);
    gLoadData.AccessModel(MODEL_JEWEL_OF_GUARDIAN, L"Data\\Item\\", L"suho", -1);
    gLoadData.AccessModel(MODEL_MOONSTONE_PENDANT, L"Data\\Item\\", L"kanneck2");
    gLoadData.AccessModel(MODEL_GEMSTONE, L"Data\\Item\\", L"rs");
    gLoadData.AccessModel(MODEL_JEWEL_OF_HARMONY, L"Data\\Item\\", L"jos");
    gLoadData.AccessModel(MODEL_LOWER_REFINE_STONE, L"Data\\Item\\", L"LowRefineStone");
    gLoadData.AccessModel(MODEL_HIGHER_REFINE_STONE, L"Data\\Item\\", L"HighRefineStone");

    gLoadData.AccessModel(MODEL_SIEGE_POTION, L"Data\\Item\\", L"SpecialPotion");

    for (int i = 0; i < 4; ++i)
    {
        gLoadData.AccessModel(MODEL_SCROLL_OF_EMPEROR_RING_OF_HONOR + i, L"Data\\Item\\", L"Quest",
                              i);
    }
    gLoadData.AccessModel(MODEL_POTION + 27, L"Data\\Item\\", L"godesteel");

    for (int i = 0; i < 2; i++)
    {
        gLoadData.AccessModel(MODEL_LOST_MAP + i, L"Data\\Item\\", L"HELLASITEM", i);
    }

    for (int i = 0; i < 3; ++i)
        gLoadData.AccessModel(MODEL_SMALL_SHIELD_POTION + i, L"Data\\Item\\", L"sdwater", i + 1);

    for (int i = 0; i < 3; ++i)
        gLoadData.AccessModel(MODEL_SMALL_COMPLEX_POTION + i, L"Data\\Item\\", L"megawater", i + 1);

    gLoadData.AccessModel(MODEL_POTION + 120, L"Data\\Item\\InGameShop\\", L"gold_coin");
    gLoadData.AccessModel(MODEL_POTION + 121, L"Data\\Item\\InGameShop\\", L"itembox_gold");
    gLoadData.AccessModel(MODEL_POTION + 122, L"Data\\Item\\InGameShop\\", L"itembox_silver");
    gLoadData.AccessModel(MODEL_POTION + 123, L"Data\\Item\\InGameShop\\", L"itembox_gold");
    gLoadData.AccessModel(MODEL_POTION + 124, L"Data\\Item\\InGameShop\\", L"itembox_silver");
    for (int k = 0; k < 6; k++)
    {
        gLoadData.AccessModel(MODEL_POTION + 134 + k, L"Data\\Item\\InGameShop\\",
                              L"package_money_item");
    }

    gLoadData.AccessModel(MODEL_COMPILED_CELE, L"Data\\Item\\", L"Jewel", 1);
    gLoadData.AccessModel(MODEL_COMPILED_SOUL, L"Data\\Item\\", L"Jewel", 2);
    gLoadData.AccessModel(MODEL_PACKED_JEWEL_OF_LIFE, L"Data\\Item\\", L"Jewel", 3);
    gLoadData.AccessModel(MODEL_PACKED_JEWEL_OF_CREATION, L"Data\\Item\\", L"jewel", 22);
    gLoadData.AccessModel(MODEL_PACKED_JEWEL_OF_GUARDIAN, L"Data\\Item\\", L"suho", -1);
    gLoadData.AccessModel(MODEL_PACKED_GEMSTONE, L"Data\\Item\\", L"rs");
    gLoadData.AccessModel(MODEL_PACKED_JEWEL_OF_HARMONY, L"Data\\Item\\", L"jos");
    gLoadData.AccessModel(MODEL_PACKED_JEWEL_OF_CHAOS, L"Data\\Item\\", L"Jewel", 15);
    gLoadData.AccessModel(MODEL_PACKED_LOWER_REFINE_STONE, L"Data\\Item\\", L"LowRefineStone");
    gLoadData.AccessModel(MODEL_PACKED_HIGHER_REFINE_STONE, L"Data\\Item\\", L"HighRefineStone");

    gLoadData.AccessModel(MODEL_EVENT + 4, L"Data\\Item\\", L"MagicBox", 2);
    gLoadData.AccessModel(MODEL_EVENT + 6, L"Data\\Item\\", L"MagicBox", 5);
    gLoadData.AccessModel(MODEL_EVENT + 7, L"Data\\Item\\", L"Beer", 2);
    gLoadData.AccessModel(MODEL_EVENT + 8, L"Data\\Item\\", L"MagicBox", 6);
    gLoadData.AccessModel(MODEL_EVENT + 9, L"Data\\Item\\", L"MagicBox", 7);

    gLoadData.AccessModel(MODEL_HELPER + 66, L"Data\\Item\\xmas\\", L"santa_village", -1);

    gLoadData.AccessModel(MODEL_POTION + 100, L"Data\\Item\\", L"coin7", -1);

    gameplay_.XmasEvent().LoadXmasEventItem();

    gLoadData.AccessModel(MODEL_EVENT + 10, L"Data\\Item\\", L"MagicBox", 8);

    gLoadData.AccessModel(MODEL_EVENT + 5, L"Data\\Item\\", L"MagicBox", 3);

    gLoadData.AccessModel(MODEL_FIRECRACKER, L"Data\\Item\\", L"GM", 1);
    gLoadData.AccessModel(MODEL_GM_GIFT, L"Data\\Item\\", L"GM", 2);

    gLoadData.AccessModel(MODEL_EVENT, L"Data\\Item\\", L"Event", 2);
    gLoadData.AccessModel(MODEL_EVENT + 1, L"Data\\Item\\", L"Event", 3);

    gLoadData.AccessModel(MODEL_CHRISTMAS_FIRECRACKER, L"Data\\Item\\XMas\\", L"xmasfire", -1);

    for (int i = 0; i < 4; i++)
    {
        if (i < 3)
        {
            gLoadData.AccessModel(MODEL_SCROLL_OF_ARCHANGEL + i, L"Data\\Item\\",
                                  L"EventBloodCastle", i);
        }
        else
        {
            gLoadData.AccessModel(MODEL_EVENT + 11 + (i - 3), L"Data\\Item\\", L"EventBloodCastle",
                                  i);
        }
    }

    gLoadData.AccessModel(MODEL_EVENT + 12, L"Data\\Item\\", L"QuestItem3RD", 0);
    gLoadData.AccessModel(MODEL_EVENT + 13, L"Data\\Item\\", L"QuestItem3RD", 1);
    gLoadData.AccessModel(MODEL_EVENT + 14, L"Data\\Item\\", L"RingOfLordEvent", 0);
    gLoadData.AccessModel(MODEL_EVENT + 15, L"Data\\Item\\", L"MagicRing", 0);

    gLoadData.AccessModel(MODEL_JEWEL_OF_CREATION, L"Data\\Item\\", L"jewel", 22);

    for (int i = 0; i < 2; ++i)
    {
        gLoadData.AccessModel(MODEL_LOCHS_FEATHER + i, L"Data\\Item\\", L"Quest", 4 + i);
    }

    gLoadData.AccessModel(MODEL_EVENT + 16, L"Data\\Item\\", L"DarkLordSleeve");
    gLoadData.AccessModel(MODEL_CAPE_OF_LORD, L"Data\\Item\\", L"DarkLordRobe");

    for (int i = 0; i < 3; i++)
        gLoadData.AccessModel(MODEL_WING + i, L"Data\\Item\\", L"Wing", i + 1);

    for (int i = 0; i < 4; i++)
    {
        gLoadData.AccessModel(MODEL_WINGS_OF_SPIRITS + i, L"Data\\Item\\", L"Wing", 4 + i);
    }

    for (int i = 0; i < 4; i++)
    {
        gLoadData.AccessModel(MODEL_WING_OF_STORM + i, L"Data\\Item\\", L"Wing", 8 + i);
    }
    gLoadData.AccessModel(MODEL_CAPE_OF_EMPEROR, L"Data\\Item\\", L"DarkLordRobe02");

    for (int i = 41; i <= 43; ++i)
        gLoadData.AccessModel(MODEL_WING + i, L"Data\\Item\\", L"Wing", i + 1);

    gLoadData.AccessModel(MODEL_BOOK_OF_SAHAMUTT, L"Data\\Item\\", L"Book_of_Sahamutt");
    gLoadData.AccessModel(MODEL_BOOK_OF_NEIL, L"Data\\Item\\", L"Book_of_Neil");
    gLoadData.AccessModel(MODEL_BOOK_OF_LAGLE, L"Data\\Item\\", L"Book_of_Rargle");

    for (int i = 0; i < 9; ++i)
    {
        gLoadData.AccessModel(MODEL_CHAIN_LIGHTNING_PARCHMENT + i, L"Data\\Item\\", L"rollofpaper");
    }

    for (int i = 0; i < 13; i++)
    {
        if (i + 7 != 15)
            gLoadData.AccessModel(MODEL_WING + i + 7, L"Data\\Item\\", L"Gem", i + 1);
    }

    gLoadData.AccessModel(MODEL_JEWEL_OF_CHAOS, L"Data\\Item\\", L"Jewel", 15);
    gLoadData.AccessModel(MODEL_WING + 20, L"Data\\Item\\", L"Gem", 14);

    gLoadData.AccessModel(MODEL_CRYSTAL_OF_DESTRUCTION, L"Data\\Item\\", L"Gem", 6);

    gLoadData.AccessModel(MODEL_CRYSTAL_OF_MULTI_SHOT, L"Data\\Item\\", L"Gem", 6);

    gLoadData.AccessModel(MODEL_CRYSTAL_OF_RECOVERY, L"Data\\Item\\", L"Gem", 6);

    gLoadData.AccessModel(MODEL_CRYSTAL_OF_FLAME_STRIKE, L"Data\\Item\\", L"Gem", 6);

    for (int i = 0; i < 4; i++)
    {
        gLoadData.AccessModel(MODEL_SCROLL_OF_FIREBURST + i, L"Data\\Item\\", L"SkillScroll");
    }
    gLoadData.AccessModel(MODEL_SCROLL_OF_FIRE_SCREAM, L"Data\\Item\\", L"SkillScroll");

    gLoadData.AccessModel(MODEL_SCROLL_OF_CHAOTIC_DISEIER, L"Data\\Item\\", L"SkillScroll");

    gLoadData.AccessModel(MODEL_PUMPKIN_OF_LUCK, L"Data\\Item\\", L"hobakhead");
    gLoadData.AccessModel(MODEL_JACK_OLANTERN_BLESSINGS, L"Data\\Item\\", L"hellowinscroll");
    gLoadData.AccessModel(MODEL_JACK_OLANTERN_WRATH, L"Data\\Item\\", L"hellowinscroll");
    gLoadData.AccessModel(MODEL_JACK_OLANTERN_CRY, L"Data\\Item\\", L"hellowinscroll");
    gLoadData.AccessModel(MODEL_JACK_OLANTERN_FOOD, L"Data\\Item\\", L"Gogi");
    gLoadData.AccessModel(MODEL_JACK_OLANTERN_DRINK, L"Data\\Item\\", L"pumpkincup");

    gLoadData.AccessModel(MODEL_PINK_CHOCOLATE_BOX, L"Data\\Item\\", L"giftbox_bp");
    gLoadData.AccessModel(MODEL_RED_CHOCOLATE_BOX, L"Data\\Item\\", L"giftbox_br");
    gLoadData.AccessModel(MODEL_BLUE_CHOCOLATE_BOX, L"Data\\Item\\", L"giftbox_bb");

    gLoadData.AccessModel(MODEL_EVENT + 21, L"Data\\Item\\", L"p03box");
    gLoadData.AccessModel(MODEL_EVENT + 22, L"Data\\Item\\", L"obox02");
    gLoadData.AccessModel(MODEL_EVENT + 23, L"Data\\Item\\", L"blue01");

    gLoadData.AccessModel(MODEL_RED_RIBBON_BOX, L"Data\\Item\\", L"giftbox_r");
    gLoadData.AccessModel(MODEL_GREEN_RIBBON_BOX, L"Data\\Item\\", L"giftbox_g");
    gLoadData.AccessModel(MODEL_BLUE_RIBBON_BOX, L"Data\\Item\\", L"giftbox_b");

    for (int i = 0; i < 19; i++)
        gLoadData.AccessModel(MODEL_ETC + i, L"Data\\Item\\", L"Book", i + 1);

    gLoadData.AccessModel(MODEL_SCROLL_OF_GIGANTIC_STORM, L"Data\\Item\\", L"Book", 18);

    gLoadData.AccessModel(MODEL_SCROLL_OF_WIZARDRY_ENHANCE, L"Data\\Item\\", L"Book", 18);

    gLoadData.AccessModel(MODEL_BONE_BLADE, L"Data\\Item\\", L"HDK_Sword");
    gLoadData.AccessModel(MODEL_EXPLOSION_BLADE, L"Data\\Item\\", L"HDK_Sword2");
    gLoadData.AccessModel(MODEL_SOLEIL_SCEPTER, L"Data\\Item\\", L"HDK_Mace");
    gLoadData.AccessModel(MODEL_SYLPH_WIND_BOW, L"Data\\Item\\", L"HDK_Bow");
    gLoadData.AccessModel(MODEL_GRAND_VIPER_STAFF, L"Data\\Item\\", L"HDK_Staff");

    gLoadData.AccessModel(MODEL_DAYBREAK, L"Data\\Item\\", L"CW_Sword");
    gLoadData.AccessModel(MODEL_SWORD_DANCER, L"Data\\Item\\", L"CW_Sword2");
    gLoadData.AccessModel(MODEL_SHINING_SCEPTER, L"Data\\Item\\", L"CW_Mace");
    gLoadData.AccessModel(MODEL_ALBATROSS_BOW, L"Data\\Item\\", L"CW_Bow");
    gLoadData.AccessModel(MODEL_PLATINA_STAFF, L"Data\\Item\\", L"CW_Staff");

    gLoadData.AccessModel(MODEL_FLAMBERGE, L"Data\\Item\\", L"Sword_27");
    gLoadData.AccessModel(MODEL_SWORD_BREAKER, L"Data\\Item\\", L"Sword_28");
    gLoadData.AccessModel(MODEL_IMPERIAL_SWORD, L"Data\\Item\\", L"Sword_29");
    gLoadData.AccessModel(MODEL_FROST_MACE, L"Data\\Item\\", L"Mace_17");
    gLoadData.AccessModel(MODEL_ABSOLUTE_SCEPTER, L"Data\\Item\\", L"Mace_18");
    gLoadData.AccessModel(MODEL_STINGER_BOW, L"Data\\Item\\", L"Bow_24");
    gLoadData.AccessModel(MODEL_DEADLY_STAFF, L"Data\\Item\\", L"Staff_31");
    gLoadData.AccessModel(MODEL_IMPERIAL_STAFF, L"Data\\Item\\", L"Staff_32");
    gLoadData.AccessModel(MODEL_STAFF + 32, L"Data\\Item\\", L"Staff_33");
    gLoadData.AccessModel(MODEL_CRIMSONGLORY, L"Data\\Item\\", L"Shield_18");
    gLoadData.AccessModel(MODEL_SALAMANDER_SHIELD, L"Data\\Item\\", L"Shield_19");
    gLoadData.AccessModel(MODEL_FROST_BARRIER, L"Data\\Item\\", L"Shield_20");
    gLoadData.AccessModel(MODEL_GUARDIAN_SHILED, L"Data\\Item\\", L"Shield_21");

    gLoadData.AccessModel(MODEL_CROSS_SHIELD, L"Data\\Item\\", L"crosssheild");

    gLoadData.AccessModel(MODEL_AIR_LYN_BOW, L"Data\\Item\\", L"gamblebow");
    gLoadData.AccessModel(MODEL_CHROMATIC_STAFF, L"Data\\Item\\", L"gamble_wand");
    gLoadData.AccessModel(MODEL_RAVEN_STICK, L"Data\\Item\\", L"gamble_stick");
    gLoadData.AccessModel(MODEL_BEUROBA, L"Data\\Item\\", L"gamble_scyder01");
    gLoadData.AccessModel(MODEL_STRYKER_SCEPTER, L"Data\\Item\\", L"gamble_safter01");

    gLoadData.AccessModel(MODEL_HELPER + 71, L"Data\\Item\\", L"gamble_scyderx01");
    gLoadData.AccessModel(MODEL_HELPER + 72, L"Data\\Item\\", L"gamble_wand01");
    gLoadData.AccessModel(MODEL_HELPER + 73, L"Data\\Item\\", L"gamble_bowx01");
    gLoadData.AccessModel(MODEL_HELPER + 74, L"Data\\Item\\", L"gamble_safterx01");
    gLoadData.AccessModel(MODEL_HELPER + 75, L"Data\\Item\\", L"gamble_stickx01");

    gLoadData.AccessModel(MODEL_HELPER + 97, L"Data\\Item\\Ingameshop\\", L"charactercard");
    gLoadData.AccessModel(MODEL_HELPER + 98, L"Data\\Item\\Ingameshop\\", L"charactercard");
    gLoadData.AccessModel(MODEL_POTION + 91, L"Data\\Item\\partCharge3\\", L"alicecard");

#ifdef PBG_ADD_CHARACTERSLOT
    gLoadData.AccessModel(MODEL_HELPER + 99, L"Data\\Item\\Ingameshop\\", L"key");
    gLoadData.AccessModel(MODEL_SLOT_LOCK, L"Data\\Item\\Ingameshop\\", L"lock");
#endif //PBG_ADD_CHARACTERSLOT
#ifdef PBG_ADD_SECRETITEM
#ifdef PBG_MOD_SECRETITEM
    gLoadData.AccessModel(MODEL_HELPER + 117, L"Data\\Item\\Ingameshop\\", L"FRpotionD");
#else  //PBG_MOD_SECRETITEM
    gLoadData.AccessModel(MODEL_HELPER + 117, L"Data\\Item\\Ingameshop\\", L"FRpotionA");
#endif //PBG_MOD_SECRETITEM
    gLoadData.AccessModel(MODEL_HELPER + 118, L"Data\\Item\\Ingameshop\\", L"FRpotionA");
    gLoadData.AccessModel(MODEL_HELPER + 119, L"Data\\Item\\Ingameshop\\", L"FRpotionB");
    gLoadData.AccessModel(MODEL_HELPER + 120, L"Data\\Item\\Ingameshop\\", L"FRpotionC");
#endif //PBG_ADD_SECRETITEM

    gLoadData.AccessModel(MODEL_POTION + 110, L"Data\\Item\\", L"indication");
    gLoadData.AccessModel(MODEL_POTION + 111, L"Data\\Item\\", L"speculum");

    gLoadData.AccessModel(MODEL_SUSPICIOUS_SCRAP_OF_PAPER, L"Data\\Item\\", L"doubt_paper");
    gLoadData.AccessModel(MODEL_GAIONS_ORDER, L"Data\\Item\\", L"warrant");
    gLoadData.AccessModel(MODEL_COMPLETE_SECROMICON, L"Data\\Item\\", L"secromicon");
    for (int c = 0; c < 6; c++)
    {
        gLoadData.AccessModel(MODEL_FIRST_SECROMICON_FRAGMENT + c, L"Data\\Item\\",
                              L"secromicon_piece");
    }

    gLoadData.AccessModel(MODEL_POTION + 112, L"Data\\Item\\Ingameshop\\", L"ItemBoxKey_silver");

    gLoadData.AccessModel(MODEL_POTION + 113, L"Data\\Item\\Ingameshop\\", L"ItemBoxKey_gold");

    {
        for (int c = 0; c < 6; c++)
        {
            gLoadData.AccessModel(MODEL_POTION + 114 + c, L"Data\\Item\\Ingameshop\\",
                                  L"primium_membership_item");
        }
    }
    {
        for (int c = 0; c < 4; c++)
        {
            gLoadData.AccessModel(MODEL_POTION + 126 + c, L"Data\\Item\\Ingameshop\\",
                                  L"primium_membership_item");
        }
    }
    {
        for (int c = 0; c < 3; c++)
        {
            gLoadData.AccessModel(MODEL_POTION + 130 + c, L"Data\\Item\\Ingameshop\\",
                                  L"primium_membership_item");
        }
    }
    {
        gLoadData.AccessModel(MODEL_HELPER + 121, L"Data\\Item\\Ingameshop\\", L"entrancegreen");
    }
    gLoadData.AccessModel(MODEL_POTION + 141, L"Data\\Item\\", L"requitalbox_red");
    gLoadData.AccessModel(MODEL_POTION + 142, L"Data\\Item\\", L"requitalbox_violet");
    gLoadData.AccessModel(MODEL_POTION + 143, L"Data\\Item\\", L"requitalbox_blue");
    gLoadData.AccessModel(MODEL_POTION + 144, L"Data\\Item\\", L"requitalbox_wood");

    gLoadData.AccessModel(MODEL_15GRADE_ARMOR_OBJ_ARMLEFT, L"Data\\Item\\", L"class15_armleft");
    gLoadData.AccessModel(MODEL_15GRADE_ARMOR_OBJ_ARMRIGHT, L"Data\\Item\\", L"class15_armright");
    gLoadData.AccessModel(MODEL_15GRADE_ARMOR_OBJ_BODYLEFT, L"Data\\Item\\", L"class15_bodyleft");
    gLoadData.AccessModel(MODEL_15GRADE_ARMOR_OBJ_BODYRIGHT, L"Data\\Item\\", L"class15_bodyright");
    gLoadData.AccessModel(MODEL_15GRADE_ARMOR_OBJ_BOOTLEFT, L"Data\\Item\\", L"class15_bootleft");
    gLoadData.AccessModel(MODEL_15GRADE_ARMOR_OBJ_BOOTRIGHT, L"Data\\Item\\", L"class15_bootright");
    gLoadData.AccessModel(MODEL_15GRADE_ARMOR_OBJ_HEAD, L"Data\\Item\\", L"class15_head");
    gLoadData.AccessModel(MODEL_15GRADE_ARMOR_OBJ_PANTLEFT, L"Data\\Item\\", L"class15_pantleft");
    gLoadData.AccessModel(MODEL_15GRADE_ARMOR_OBJ_PANTRIGHT, L"Data\\Item\\", L"class15_pantright");

    gLoadData.AccessModel(MODEL_CAPE_OF_FIGHTER, L"Data\\Item\\", L"Wing", 50);
    gLoadData.AccessModel(MODEL_CAPE_OF_OVERRULE, L"Data\\Item\\", L"Wing", 51);
    gLoadData.AccessModel(MODEL_WING + 135, L"Data\\Item\\", L"Wing", 50);
    LoadBitmapW(L"Item\\NCcape.tga", BITMAP_NCCAPE, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Item\\monk_manto01.TGA", BITMAP_MANTO01, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Item\\monke_manto.TGA", BITMAP_MANTOE, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Repeat);
    g_CMonkSystem.LoadModelItem();
    for (int _nRollIndex = 0; _nRollIndex < 7; ++_nRollIndex)
        gLoadData.AccessModel(MODEL_CHAIN_DRIVE_PARCHMENT + _nRollIndex, L"Data\\Item\\",
                              L"rollofpaper");

    gLoadData.AccessModel(MODEL_HELPER + 135, L"Data\\Item\\LuckyItem\\", L"LuckyCardgreen");
    gLoadData.AccessModel(MODEL_HELPER + 136, L"Data\\Item\\LuckyItem\\", L"LuckyCardgreen");
    gLoadData.AccessModel(MODEL_HELPER + 137, L"Data\\Item\\LuckyItem\\", L"LuckyCardgreen");
    gLoadData.AccessModel(MODEL_HELPER + 138, L"Data\\Item\\LuckyItem\\", L"LuckyCardgreen");
    gLoadData.AccessModel(MODEL_HELPER + 139, L"Data\\Item\\LuckyItem\\", L"LuckyCardgreen");
    gLoadData.AccessModel(MODEL_HELPER + 140, L"Data\\Item\\LuckyItem\\", L"LuckyCardred");
    gLoadData.AccessModel(MODEL_HELPER + 141, L"Data\\Item\\LuckyItem\\", L"LuckyCardred");
    gLoadData.AccessModel(MODEL_HELPER + 142, L"Data\\Item\\LuckyItem\\", L"LuckyCardred");
    gLoadData.AccessModel(MODEL_HELPER + 143, L"Data\\Item\\LuckyItem\\", L"LuckyCardred");
    gLoadData.AccessModel(MODEL_HELPER + 144, L"Data\\Item\\LuckyItem\\", L"LuckyCardred");

    gLoadData.AccessModel(MODEL_POTION + 160, L"Data\\Item\\LuckyItem\\", L"lucky_items01");
    gLoadData.AccessModel(MODEL_POTION + 161, L"Data\\Item\\LuckyItem\\", L"lucky_items02");

    const wchar_t szLuckySetFileName[][50] = {L"new_Helm", L"new_Armor", L"new_Pant", L"new_Glove",
                                              L"new_Boot"};
    const wchar_t *szLuckySetPath = {L"Data\\Player\\LuckyItem\\"};
    wchar_t szLuckySetPathName[50] = {L""};
    int nIndex = 62;

    for (int i = 0; i < 11; i++)
    {
        mu_swprintf(szLuckySetPathName, L"%ls%d\\", szLuckySetPath, nIndex);
        if (nIndex != 71)
            gLoadData.AccessModel(MODEL_HELM + nIndex, szLuckySetPathName, szLuckySetFileName[0],
                                  i + 1);
        gLoadData.AccessModel(MODEL_ARMOR + nIndex, szLuckySetPathName, szLuckySetFileName[1],
                              i + 1);
        gLoadData.AccessModel(MODEL_PANTS + nIndex, szLuckySetPathName, szLuckySetFileName[2],
                              i + 1);
        if (nIndex != 72)
            gLoadData.AccessModel(MODEL_GLOVES + nIndex, szLuckySetPathName, szLuckySetFileName[3],
                                  i + 1);
        gLoadData.AccessModel(MODEL_BOOTS + nIndex, szLuckySetPathName, szLuckySetFileName[4],
                              i + 1);
        nIndex++;
    }

    Models[MODEL_SPEAR].Meshs[1].NoneBlendMesh = true;
    Models[MODEL_LIGHT_SABER].Meshs[1].NoneBlendMesh = true;
    Models[MODEL_STAFF_OF_RESURRECTION].Meshs[2].NoneBlendMesh = true;
    Models[MODEL_CHAOS_DRAGON_AXE].Meshs[1].NoneBlendMesh = true;
    Models[MODEL_EVENT + 9].Meshs[1].NoneBlendMesh = true;
}

void SessionRenderUnit::OpenItemTextures()
{
    for (int i = 0; i < 4; i++)
    {
        if (i < 3)
        {
            gLoadData.OpenTexture(MODEL_SCROLL_OF_ARCHANGEL + i, L"Item\\");
        }
        else
        {
            gLoadData.OpenTexture(MODEL_EVENT + 11 + (i - 3), L"Item\\");
        }
    }
    gLoadData.OpenTexture(MODEL_EVENT + 12, L"Item\\");
    gLoadData.OpenTexture(MODEL_EVENT + 13, L"Item\\");
    gLoadData.OpenTexture(MODEL_EVENT + 14, L"Item\\");
    gLoadData.OpenTexture(MODEL_EVENT + 15, L"Item\\");

    for (int i = 0; i < 4; i++)
    {
        gLoadData.OpenTexture(MODEL_WINGS_OF_SPIRITS + i, L"Item\\");
    }

    for (int i = 0; i < 4; i++)
    {
        gLoadData.OpenTexture(MODEL_WING_OF_STORM + i, L"Item\\");
    }
    gLoadData.OpenTexture(MODEL_CAPE_OF_EMPEROR, L"Item\\");

    LoadBitmapW(L"Item\\msword01_r.jpg", BITMAP_3RDWING_LAYER, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Repeat);

    for (int i = 41; i <= 43; ++i)
        gLoadData.OpenTexture(MODEL_WING + i, L"Item\\");

    for (int i = 21; i <= 23; ++i)
        gLoadData.OpenTexture(MODEL_STAFF + i, L"Item\\");

    for (int i = 0; i < 9; ++i)
    {
        gLoadData.OpenTexture(MODEL_CHAIN_LIGHTNING_PARCHMENT + i, L"Item\\");
    }

    LoadBitmapW(L"Item\\rollofpaper_R.jpg", BITMAP_ROOLOFPAPER_EFFECT_R,
                LegacyTextureFilter::Linear);

    gLoadData.OpenTexture(MODEL_DARK_HORSE_ITEM, L"Skill\\");
    gLoadData.OpenTexture(MODEL_DARK_HORSE_ITEM, L"Item\\");
    gLoadData.OpenTexture(MODEL_SPIRIT, L"Item\\");
    gLoadData.OpenTexture(MODEL_DARK_RAVEN_ITEM, L"Skill\\");
    gLoadData.OpenTexture(MODEL_DARK_RAVEN_ITEM, L"Item\\");

    gLoadData.OpenTexture(MODEL_JEWEL_OF_CREATION, L"Item\\");

    gLoadData.OpenTexture(MODEL_JEWEL_OF_GUARDIAN, L"Item\\");

    gLoadData.OpenTexture(MODEL_SIEGE_POTION, L"Item\\");
    gLoadData.OpenTexture(MODEL_HELPER + 7, L"Item\\");
    gLoadData.OpenTexture(MODEL_LIFE_STONE_ITEM, L"Item\\");
    gLoadData.OpenTexture(MODEL_EVENT + 18, L"Monster\\");

    for (int i = 0; i < 2; i++)
        gLoadData.OpenTexture(MODEL_LOCHS_FEATHER + i, L"Item\\");

    gLoadData.OpenTexture(MODEL_DARK_BREAKER, L"Item\\");
    gLoadData.OpenTexture(MODEL_THUNDER_BLADE, L"Item\\");

    gLoadData.OpenTexture(MODEL_DRAGON_SOUL_STAFF, L"Item\\");
    gLoadData.OpenTexture(MODEL_CELESTIAL_BOW, L"Item\\");

    gLoadData.OpenTexture(MODEL_HORN_OF_DINORANT, L"Skill\\");

    for (int i = 0; i < 4; i++)
        gLoadData.OpenTexture(MODEL_SCROLL_OF_EMPEROR_RING_OF_HONOR + i, L"Item\\");

    gLoadData.OpenTexture(MODEL_POTION + 27, L"Item\\");

    for (int i = 0; i < 2; i++)
        gLoadData.OpenTexture(MODEL_LOST_MAP + i, L"Item\\");

    gLoadData.OpenTexture(MODEL_ARMOR_OF_GUARDSMAN, L"Npc\\");
    gLoadData.OpenTexture(MODEL_DIVINE_SWORD_OF_ARCHANGEL, L"Item\\");
    gLoadData.OpenTexture(MODEL_DIVINE_STAFF_OF_ARCHANGEL, L"Item\\");
    gLoadData.OpenTexture(MODEL_DIVINE_STICK_OF_ARCHANGEL, L"Item\\");
    gLoadData.OpenTexture(MODEL_DIVINE_CB_OF_ARCHANGEL, L"Item\\");
    gLoadData.OpenTexture(MODEL_GREAT_REIGN_CROSSBOW, L"Item\\");

    gLoadData.OpenTexture(MODEL_RUNE_BLADE, L"Item\\");
    gLoadData.OpenTexture(MODEL_GRAND_SOUL_SHIELD, L"Item\\");
    gLoadData.OpenTexture(MODEL_ELEMENTAL_SHIELD, L"Item\\");
    gLoadData.OpenTexture(MODEL_DRAGON_SPEAR, L"Item\\");
    gLoadData.OpenTexture(MODEL_ELEMENTAL_MACE, L"Item\\");

    for (int i = 0; i < 17; i++)
    {
        gLoadData.OpenTexture(MODEL_SWORD + i, L"Item\\");
        gLoadData.OpenTexture(MODEL_AXE + i, L"Item\\");
        gLoadData.OpenTexture(MODEL_MACE + i, L"Item\\");
        gLoadData.OpenTexture(MODEL_SPEAR + i, L"Item\\");
        gLoadData.OpenTexture(MODEL_STAFF + i, L"Item\\");
        gLoadData.OpenTexture(MODEL_SHIELD + i, L"Item\\");
        gLoadData.OpenTexture(MODEL_BOW + i, L"Item\\");
        gLoadData.OpenTexture(MODEL_HELPER + i, L"Item\\");
        gLoadData.OpenTexture(MODEL_WING + i, L"Item\\");
        gLoadData.OpenTexture(MODEL_POTION + i, L"Item\\");
        gLoadData.OpenTexture(MODEL_ETC + i, L"Item\\");
    }

    for (int i = 14; i <= 20; ++i)
        gLoadData.OpenTexture(MODEL_STAFF + i, L"Item\\");

    for (int i = 21; i <= 28; ++i)
        gLoadData.OpenTexture(MODEL_HELPER + i, L"Item\\");
    //. MODEL_MACE+8,9,10,11
    for (int i = 0; i < 5; i++)
        gLoadData.OpenTexture(MODEL_BATTLE_SCEPTER + i, L"Item\\");

    for (int i = 0; i < 2; i++)
        gLoadData.OpenTexture(MODEL_KNIGHT_BLADE + i, L"Item\\");

    gLoadData.OpenTexture(MODEL_ARROW_VIPER_BOW, L"Item\\");

    for (int i = 17; i < 21; i++)
    {
        gLoadData.OpenTexture(MODEL_WING + i, L"Item\\");
    }
    for (int i = 0; i < 4; i++)
    {
        gLoadData.OpenTexture(MODEL_SCROLL_OF_FIREBURST + i, L"Item\\");
    }

    gLoadData.OpenTexture(MODEL_SCROLL_OF_CHAOTIC_DISEIER, L"Item\\");
    gLoadData.OpenTexture(MODEL_SCROLL_OF_FIRE_SCREAM, L"Item\\");
    gLoadData.OpenTexture(MODEL_CRYSTAL_OF_DESTRUCTION, L"Item\\");
    gLoadData.OpenTexture(MODEL_CRYSTAL_OF_MULTI_SHOT, L"Item\\");
    gLoadData.OpenTexture(MODEL_CRYSTAL_OF_RECOVERY, L"Item\\");
    gLoadData.OpenTexture(MODEL_CRYSTAL_OF_FLAME_STRIKE, L"Item\\");
    gLoadData.OpenTexture(MODEL_SCROLL_OF_GIGANTIC_STORM, L"Item\\");
    gLoadData.OpenTexture(MODEL_SCROLL_OF_WIZARDRY_ENHANCE, L"Item\\");

    for (int i = 17; i < 19; ++i)
    {
        gLoadData.OpenTexture(MODEL_ETC + i, L"Item\\");
    }

    gLoadData.OpenTexture(MODEL_ARROW, L"Item\\Bow\\");

    gLoadData.OpenTexture(MODEL_EVENT + 4, L"Item\\");
    gLoadData.OpenTexture(MODEL_EVENT + 5, L"Item\\");
    gLoadData.OpenTexture(MODEL_EVENT + 6, L"Item\\");
    gLoadData.OpenTexture(MODEL_EVENT + 7, L"Item\\");
    gLoadData.OpenTexture(MODEL_EVENT + 8, L"Item\\");
    gLoadData.OpenTexture(MODEL_EVENT + 9, L"Item\\");
    gLoadData.OpenTexture(MODEL_EVENT + 10, L"Item\\");
    gLoadData.OpenTexture(MODEL_EVENT + 16, L"Item\\");
    gLoadData.OpenTexture(MODEL_CAPE_OF_LORD, L"Item\\");

    for (int i = 0; i < 3; i++)
        gLoadData.OpenTexture(MODEL_DEVILS_EYE + i, L"Item\\");

    for (int i = 0; i < 2; i++)
        gLoadData.OpenTexture(MODEL_POTION + 20 + i, L"Item\\");

    for (int i = 0; i < 6; ++i)
        gLoadData.OpenTexture(MODEL_SMALL_SHIELD_POTION + i, L"Item\\");

    for (int i = 0; i < 2; i++)
        gLoadData.OpenTexture(MODEL_EVENT + i, L"Item\\");

    //gLoadData.OpenTexture(MODEL_GOLD01  ,"Data\\Item\\Etc\\");
    //gLoadData.OpenTexture(MODEL_APPLE01 ,"Data\\Item\\Etc\\");
    //gLoadData.OpenTexture(MODEL_BOTTLE01,"Data\\Item\\Etc\\");
    gLoadData.OpenTexture(MODEL_COMPILED_CELE, L"Item\\");
    gLoadData.OpenTexture(MODEL_COMPILED_SOUL, L"Item\\");

    gLoadData.OpenTexture(MODEL_PACKED_JEWEL_OF_LIFE, L"Item\\");
    gLoadData.OpenTexture(MODEL_PACKED_JEWEL_OF_CREATION, L"Item\\");
    gLoadData.OpenTexture(MODEL_PACKED_JEWEL_OF_GUARDIAN, L"Item\\");
    gLoadData.OpenTexture(MODEL_PACKED_GEMSTONE, L"Item\\");
    gLoadData.OpenTexture(MODEL_PACKED_JEWEL_OF_HARMONY, L"Item\\");
    gLoadData.OpenTexture(MODEL_PACKED_JEWEL_OF_CHAOS, L"Item\\");
    gLoadData.OpenTexture(MODEL_PACKED_LOWER_REFINE_STONE, L"Item\\");
    gLoadData.OpenTexture(MODEL_PACKED_HIGHER_REFINE_STONE, L"Item\\");

    gLoadData.OpenTexture(MODEL_BONE_BLADE, L"Item\\");
    gLoadData.OpenTexture(MODEL_EXPLOSION_BLADE, L"Item\\");
    gLoadData.OpenTexture(MODEL_SOLEIL_SCEPTER, L"Item\\");
    gLoadData.OpenTexture(MODEL_SYLPH_WIND_BOW, L"Item\\");
    gLoadData.OpenTexture(MODEL_GRAND_VIPER_STAFF, L"Item\\");

    gLoadData.OpenTexture(MODEL_PUMPKIN_OF_LUCK, L"Item\\");
    gLoadData.OpenTexture(MODEL_JACK_OLANTERN_BLESSINGS, L"Item\\");
    gLoadData.OpenTexture(MODEL_JACK_OLANTERN_WRATH, L"Item\\");
    gLoadData.OpenTexture(MODEL_JACK_OLANTERN_CRY, L"Item\\");
    gLoadData.OpenTexture(MODEL_JACK_OLANTERN_FOOD, L"Item\\");
    gLoadData.OpenTexture(MODEL_JACK_OLANTERN_DRINK, L"Item\\");
    gLoadData.OpenTexture(MODEL_PINK_CHOCOLATE_BOX, L"Item\\");
    gLoadData.OpenTexture(MODEL_RED_CHOCOLATE_BOX, L"Item\\");
    gLoadData.OpenTexture(MODEL_BLUE_CHOCOLATE_BOX, L"Item\\");

    gLoadData.OpenTexture(MODEL_EVENT + 21, L"Item\\");
    gLoadData.OpenTexture(MODEL_EVENT + 22, L"Item\\");
    gLoadData.OpenTexture(MODEL_EVENT + 23, L"Item\\");
    gLoadData.OpenTexture(MODEL_MOONSTONE_PENDANT, L"Item\\");
    gLoadData.OpenTexture(MODEL_GEMSTONE, L"Item\\");
    gLoadData.OpenTexture(MODEL_JEWEL_OF_HARMONY, L"Item\\");
    gLoadData.OpenTexture(MODEL_LOWER_REFINE_STONE, L"Item\\");
    gLoadData.OpenTexture(MODEL_HIGHER_REFINE_STONE, L"Item\\");
    gLoadData.OpenTexture(MODEL_RED_RIBBON_BOX, L"Item\\");
    gLoadData.OpenTexture(MODEL_GREEN_RIBBON_BOX, L"Item\\");
    gLoadData.OpenTexture(MODEL_BLUE_RIBBON_BOX, L"Item\\");

    gLoadData.OpenTexture(MODEL_DAYBREAK, L"Item\\");
    gLoadData.OpenTexture(MODEL_SWORD_DANCER, L"Item\\");
    gLoadData.OpenTexture(MODEL_SHINING_SCEPTER, L"Item\\");
    gLoadData.OpenTexture(MODEL_ALBATROSS_BOW, L"Item\\");
    gLoadData.OpenTexture(MODEL_PLATINA_STAFF, L"Item\\");

    gLoadData.OpenTexture(MODEL_SPLINTER_OF_ARMOR, L"Item\\");
    gLoadData.OpenTexture(MODEL_BLESS_OF_GUARDIAN, L"Item\\");
    gLoadData.OpenTexture(MODEL_CLAW_OF_BEAST, L"Item\\");
    gLoadData.OpenTexture(MODEL_FRAGMENT_OF_HORN, L"Item\\");
    gLoadData.OpenTexture(MODEL_BROKEN_HORN, L"Item\\");
    gLoadData.OpenTexture(MODEL_HORN_OF_FENRIR, L"Item\\");

    gLoadData.OpenTexture(MODEL_HELPER + 46, L"Item\\partCharge1\\");
    gLoadData.OpenTexture(MODEL_HELPER + 47, L"Item\\partCharge1\\");
    gLoadData.OpenTexture(MODEL_HELPER + 48, L"Item\\partCharge1\\");

    gLoadData.OpenTexture(MODEL_POTION + 54, L"Item\\partCharge1\\");

    gLoadData.OpenTexture(MODEL_HELPER + 43, L"Item\\partCharge1\\");
    gLoadData.OpenTexture(MODEL_HELPER + 44, L"Item\\partCharge1\\");
    gLoadData.OpenTexture(MODEL_HELPER + 45, L"Item\\partCharge1\\");

    gLoadData.OpenTexture(MODEL_POTION + 53, L"Item\\partCharge1\\");

    for (int i = 0; i < 5; ++i)
    {
        gLoadData.OpenTexture(MODEL_POTION + 58 + i, L"Item\\partCharge1\\");
    }
    gLoadData.OpenTexture(MODEL_POTION + 70, L"Item\\partCharge2\\");
    gLoadData.OpenTexture(MODEL_POTION + 71, L"Item\\partCharge2\\");
    gLoadData.OpenTexture(MODEL_POTION + 72, L"Item\\partCharge2\\");
    gLoadData.OpenTexture(MODEL_POTION + 73, L"Item\\partCharge2\\");
    gLoadData.OpenTexture(MODEL_POTION + 74, L"Item\\partCharge2\\");
    gLoadData.OpenTexture(MODEL_POTION + 75, L"Item\\partCharge2\\");
    gLoadData.OpenTexture(MODEL_POTION + 76, L"Item\\partCharge2\\");
    gLoadData.OpenTexture(MODEL_POTION + 77, L"Item\\partCharge2\\");
    gLoadData.OpenTexture(static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_KNIGHT_1_CHARM,
                          L"Item\\partCharge6\\");
    gLoadData.OpenTexture(static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_MAGICIAN_1_CHARM,
                          L"Item\\partCharge6\\");
    gLoadData.OpenTexture(static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_ELF_1_CHARM,
                          L"Item\\partCharge6\\");
    gLoadData.OpenTexture(static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_SUMMONER_1_CHARM,
                          L"Item\\partCharge6\\");
    gLoadData.OpenTexture(static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_DARKLORD_1_CHARM,
                          L"Item\\partCharge6\\");

    gLoadData.OpenTexture(static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_KNIGHT_2_CHARM,
                          L"Item\\partCharge6\\");
    gLoadData.OpenTexture(static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_MAGICIAN_2_CHARM,
                          L"Item\\partCharge6\\");
    gLoadData.OpenTexture(static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_ELF_2_CHARM,
                          L"Item\\partCharge6\\");
    gLoadData.OpenTexture(static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_SUMMONER_2_CHARM,
                          L"Item\\partCharge6\\");
    gLoadData.OpenTexture(static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_DARKKNIGHT_2_CHARM,
                          L"Item\\partCharge6\\");

    gLoadData.OpenTexture(MODEL_HELPER + 59, L"Item\\partCharge2\\");

    gLoadData.OpenTexture(MODEL_HELPER + 54, L"Item\\partCharge2\\");
    gLoadData.OpenTexture(MODEL_HELPER + 55, L"Item\\partCharge2\\");
    gLoadData.OpenTexture(MODEL_HELPER + 56, L"Item\\partCharge2\\");
    gLoadData.OpenTexture(MODEL_HELPER + 57, L"Item\\partCharge2\\");
    gLoadData.OpenTexture(MODEL_HELPER + 58, L"Item\\partCharge2\\");
    gLoadData.OpenTexture(MODEL_POTION + 78, L"Item\\partCharge2\\");
    gLoadData.OpenTexture(MODEL_POTION + 79, L"Item\\partCharge2\\");
    gLoadData.OpenTexture(MODEL_POTION + 80, L"Item\\partCharge2\\");
    gLoadData.OpenTexture(MODEL_POTION + 81, L"Item\\partCharge2\\");
    gLoadData.OpenTexture(MODEL_POTION + 82, L"Item\\partCharge2\\");
    gLoadData.OpenTexture(MODEL_HELPER + 60, L"Item\\partCharge2\\");

    gLoadData.OpenTexture(MODEL_HELPER + 61, L"Item\\partCharge2\\");

    gLoadData.OpenTexture(MODEL_POTION + 91, L"Item\\partCharge3\\");

    gLoadData.OpenTexture(MODEL_POTION + 92, L"Item\\partCharge3\\");
    gLoadData.OpenTexture(MODEL_POTION + 93, L"Item\\partCharge3\\");
    gLoadData.OpenTexture(MODEL_POTION + 95, L"Item\\partCharge3\\");

    gLoadData.OpenTexture(MODEL_POTION + 94, L"Item\\partCharge2\\");

    gLoadData.OpenTexture(MODEL_HELPER + 62, L"Item\\partCharge4\\");
    gLoadData.OpenTexture(MODEL_HELPER + 63, L"Item\\partCharge4\\");

    gLoadData.OpenTexture(MODEL_POTION + 97, L"Item\\partCharge4\\");
    gLoadData.OpenTexture(MODEL_POTION + 98, L"Item\\partCharge4\\");

    gLoadData.OpenTexture(MODEL_POTION + 96, L"Item\\partCharge4\\");

    gLoadData.OpenTexture(MODEL_DEMON, L"Item\\partCharge4\\");
    gLoadData.OpenTexture(MODEL_SPIRIT_OF_GUARDIAN, L"Item\\partCharge4\\");

    gLoadData.OpenTexture(MODEL_PET_RUDOLF, L"Item\\xmas\\");
    gLoadData.OpenTexture(MODEL_PET_PANDA, L"Item\\");

    gLoadData.OpenTexture(MODEL_PET_UNICORN, L"Item\\partcharge7\\");
    gLoadData.OpenTexture(MODEL_PET_SKELETON, L"Item\\");
    gLoadData.OpenTexture(MODEL_HELPER + 66, L"Item\\xmas\\");
    gLoadData.OpenTexture(MODEL_POTION + 100, L"Item\\");
    gLoadData.OpenTexture(MODEL_HELPER + 69, L"Item\\partCharge5\\");
    gLoadData.OpenTexture(MODEL_HELPER + 70, L"Item\\partCharge5\\");
    gLoadData.OpenTexture(MODEL_HELPER + 81, L"Item\\partCharge6\\");
    gLoadData.OpenTexture(MODEL_HELPER + 82, L"Item\\partCharge6\\");
    gLoadData.OpenTexture(MODEL_HELPER + 93, L"Item\\partCharge6\\");
    gLoadData.OpenTexture(MODEL_HELPER + 94, L"Item\\partCharge6\\");
    gLoadData.OpenTexture(MODEL_POTION + 140, L"Item\\");

    gLoadData.OpenTexture(MODEL_CHERRY_BLOSSOM_PLAYBOX, L"Item\\cherryblossom\\");
    gLoadData.OpenTexture(MODEL_CHERRY_BLOSSOM_WINE, L"Item\\cherryblossom\\");
    gLoadData.OpenTexture(MODEL_CHERRY_BLOSSOM_RICE_CAKE, L"Item\\cherryblossom\\");
    gLoadData.OpenTexture(MODEL_CHERRY_BLOSSOM_FLOWER_PETAL, L"Item\\cherryblossom\\");
    gLoadData.OpenTexture(MODEL_POTION + 88, L"Item\\cherryblossom\\");
    gLoadData.OpenTexture(MODEL_POTION + 89, L"Item\\cherryblossom\\");
    gLoadData.OpenTexture(MODEL_GOLDEN_CHERRY_BLOSSOM_BRANCH, L"Item\\cherryblossom\\");

    gLoadData.OpenTexture(MODEL_OLD_SCROLL, L"Item\\");
    gLoadData.OpenTexture(MODEL_ILLUSION_SORCERER_COVENANT, L"Item\\");
    gLoadData.OpenTexture(MODEL_SCROLL_OF_BLOOD, L"Item\\");

    gLoadData.OpenTexture(MODEL_FLAME_OF_CONDOR, L"Item\\");
    gLoadData.OpenTexture(MODEL_FEATHER_OF_CONDOR, L"Item\\");

    gLoadData.OpenTexture(MODEL_POTION + 55, L"Item\\");
    gLoadData.OpenTexture(MODEL_POTION + 56, L"Item\\");
    gLoadData.OpenTexture(MODEL_POTION + 57, L"Item\\");

    gLoadData.OpenTexture(MODEL_POTION + 64, L"Item\\");
    gLoadData.OpenTexture(MODEL_FLAME_OF_DEATH_BEAM_KNIGHT, L"Item\\");
    gLoadData.OpenTexture(MODEL_HORN_OF_HELL_MAINE, L"Item\\");
    gLoadData.OpenTexture(MODEL_FEATHER_OF_DARK_PHOENIX, L"Item\\");
    gLoadData.OpenTexture(MODEL_EYE_OF_ABYSSAL, L"Item\\");

    gLoadData.OpenTexture(MODEL_FIRECRACKER, L"Item\\");
    gLoadData.OpenTexture(MODEL_GM_GIFT, L"Item\\");
    LoadChangeRingItemTextures();

    gLoadData.OpenTexture(MODEL_CHRISTMAS_FIRECRACKER, L"Item\\XMas\\");

    gLoadData.OpenTexture(MODEL_POTION + 145, L"Item\\partCharge8\\");
    gLoadData.OpenTexture(MODEL_POTION + 146, L"Item\\partCharge8\\");
    gLoadData.OpenTexture(MODEL_POTION + 147, L"Item\\partCharge8\\");
    gLoadData.OpenTexture(MODEL_POTION + 148, L"Item\\partCharge8\\");
    gLoadData.OpenTexture(MODEL_POTION + 149, L"Item\\partCharge8\\");
    gLoadData.OpenTexture(MODEL_POTION + 150, L"Item\\partCharge8\\");

    gLoadData.OpenTexture(MODEL_HELPER + 125, L"Item\\partCharge8\\");
    gLoadData.OpenTexture(MODEL_HELPER + 126, L"Item\\partCharge8\\");
    gLoadData.OpenTexture(MODEL_HELPER + 127, L"Item\\partCharge8\\");

#ifdef LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM
    gLoadData.OpenTexture(MODEL_HELPER + 128, L"Item\\");
    gLoadData.OpenTexture(MODEL_HELPER + 129, L"Item\\");
    gLoadData.OpenTexture(MODEL_HELPER + 134, L"Item\\");
#endif //LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM
#ifdef LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM_PART_2
    gLoadData.OpenTexture(MODEL_HELPER + 130, L"Item\\");
    //	gLoadData.OpenTexture(MODEL_HELPER+131, L"Item\\");
    gLoadData.OpenTexture(MODEL_HELPER + 132, L"Item\\");
    //	gLoadData.OpenTexture(MODEL_HELPER+133, L"Item\\");
#endif //LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM_PART_2

    gLoadData.OpenTexture(MODEL_FLAMBERGE, L"Item\\");
    gLoadData.OpenTexture(MODEL_SWORD_BREAKER, L"Item\\");
    gLoadData.OpenTexture(MODEL_IMPERIAL_SWORD, L"Item\\");
    gLoadData.OpenTexture(MODEL_FROST_MACE, L"Item\\");
    gLoadData.OpenTexture(MODEL_ABSOLUTE_SCEPTER, L"Item\\");
    gLoadData.OpenTexture(MODEL_STINGER_BOW, L"Item\\");
    gLoadData.OpenTexture(MODEL_DEADLY_STAFF, L"Item\\");
    gLoadData.OpenTexture(MODEL_IMPERIAL_STAFF, L"Item\\");
    gLoadData.OpenTexture(MODEL_STAFF + 32, L"Item\\");
    gLoadData.OpenTexture(MODEL_CRIMSONGLORY, L"Item\\");
    gLoadData.OpenTexture(MODEL_SALAMANDER_SHIELD, L"Item\\");
    gLoadData.OpenTexture(MODEL_FROST_BARRIER, L"Item\\");
    gLoadData.OpenTexture(MODEL_GUARDIAN_SHILED, L"Item\\");

    for (int i = 0; i < 6; ++i)
        gLoadData.OpenTexture(MODEL_SEED_FIRE + i, L"Effect\\");

    for (int i = 0; i < 5; ++i)
        gLoadData.OpenTexture(MODEL_SPHERE_MONO + i, L"Item\\");

    for (int i = 0; i < 30; ++i)
    {
        gLoadData.OpenTexture(MODEL_SEED_SPHERE_FIRE_1 + i, L"Effect\\");
        gLoadData.OpenTexture(MODEL_SEED_SPHERE_FIRE_1 + i, L"Item\\");
    }

    gLoadData.OpenTexture(MODEL_CROSS_SHIELD, L"Item\\");

    gLoadData.OpenTexture(MODEL_AIR_LYN_BOW, L"Item\\");
    gLoadData.OpenTexture(MODEL_CHROMATIC_STAFF, L"Item\\");
    gLoadData.OpenTexture(MODEL_RAVEN_STICK, L"Item\\");
    gLoadData.OpenTexture(MODEL_SWORD + 29, L"Item\\");
    gLoadData.OpenTexture(MODEL_STRYKER_SCEPTER, L"Item\\");

    gLoadData.OpenTexture(MODEL_HELPER + 71, L"Item\\");
    gLoadData.OpenTexture(MODEL_HELPER + 72, L"Item\\");
    gLoadData.OpenTexture(MODEL_HELPER + 73, L"Item\\");
    gLoadData.OpenTexture(MODEL_HELPER + 74, L"Item\\");
    gLoadData.OpenTexture(MODEL_HELPER + 75, L"Item\\");

    gLoadData.OpenTexture(MODEL_HELPER + 97, L"Item\\Ingameshop\\");
    gLoadData.OpenTexture(MODEL_HELPER + 98, L"Item\\Ingameshop\\");
    gLoadData.OpenTexture(MODEL_POTION + 91, L"Item\\partCharge3\\");

#ifdef PBG_ADD_CHARACTERSLOT
    gLoadData.OpenTexture(MODEL_HELPER + 99, L"Item\\Ingameshop\\");
    gLoadData.OpenTexture(MODEL_SLOT_LOCK, L"Item\\Ingameshop\\");
#endif //PBG_ADD_CHARACTERSLOT

    gLoadData.OpenTexture(MODEL_POTION + 110, L"Item\\");
    gLoadData.OpenTexture(MODEL_POTION + 111, L"Item\\");

    gLoadData.OpenTexture(MODEL_SUSPICIOUS_SCRAP_OF_PAPER, L"Item\\");
    gLoadData.OpenTexture(MODEL_GAIONS_ORDER, L"Item\\");
    gLoadData.OpenTexture(MODEL_COMPLETE_SECROMICON, L"Item\\");
    for (int c = 0; c < 6; c++)
    {
        gLoadData.OpenTexture(MODEL_FIRST_SECROMICON_FRAGMENT + c, L"Item\\");
    }

    gLoadData.OpenTexture(MODEL_HELPER + 107, L"Item\\partcharge7\\");
    gLoadData.OpenTexture(MODEL_HELPER + 104, L"Item\\partcharge7\\");
    gLoadData.OpenTexture(MODEL_HELPER + 105, L"Item\\partcharge7\\");
    gLoadData.OpenTexture(MODEL_HELPER + 103, L"Item\\partcharge7\\");
    gLoadData.OpenTexture(MODEL_POTION + 133, L"Item\\partcharge7\\");

    gLoadData.OpenTexture(MODEL_HELPER + 109, L"Item\\InGameShop\\");
    gLoadData.OpenTexture(MODEL_HELPER + 110, L"Item\\InGameShop\\");
    gLoadData.OpenTexture(MODEL_HELPER + 111, L"Item\\InGameShop\\");
    gLoadData.OpenTexture(MODEL_HELPER + 112, L"Item\\InGameShop\\");
    gLoadData.OpenTexture(MODEL_HELPER + 113, L"Item\\InGameShop\\");
    gLoadData.OpenTexture(MODEL_HELPER + 114, L"Item\\InGameShop\\");
    gLoadData.OpenTexture(MODEL_HELPER + 115, L"Item\\InGameShop\\");

    gLoadData.OpenTexture(MODEL_POTION + 120, L"Item\\InGameShop\\");
    gLoadData.OpenTexture(MODEL_POTION + 121, L"Item\\InGameShop\\");
    gLoadData.OpenTexture(MODEL_POTION + 122, L"Item\\InGameShop\\");
    gLoadData.OpenTexture(MODEL_POTION + 123, L"Item\\InGameShop\\");
    gLoadData.OpenTexture(MODEL_POTION + 124, L"Item\\InGameShop\\");

    for (int k = 0; k < 6; k++)
    {
        gLoadData.OpenTexture(MODEL_POTION + 134 + k, L"Item\\InGameShop\\");
    }
    LoadBitmapW(L"Item\\InGameShop\\membership_item_blue.jpg", BITMAP_PACKAGEBOX_BLUE,
                LegacyTextureFilter::Linear, LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Item\\InGameShop\\membership_item_gold.jpg", BITMAP_PACKAGEBOX_GOLD,
                LegacyTextureFilter::Linear, LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Item\\InGameShop\\membership_item_green.jpg", BITMAP_PACKAGEBOX_GREEN,
                LegacyTextureFilter::Linear, LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Item\\InGameShop\\membership_item_pouple.jpg", BITMAP_PACKAGEBOX_PUPLE,
                LegacyTextureFilter::Linear, LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Item\\InGameShop\\membership_item_red.jpg", BITMAP_PACKAGEBOX_RED,
                LegacyTextureFilter::Linear, LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Item\\InGameShop\\membership_item_sky.jpg", BITMAP_PACKAGEBOX_SKY,
                LegacyTextureFilter::Linear, LegacyTextureWrap::Repeat);

    gLoadData.OpenTexture(MODEL_HELPER + 116, L"Item\\");

    gLoadData.OpenTexture(MODEL_WING + 130, L"Item\\");
    for (int j = 0; j < 4; j++)
    {
        gLoadData.OpenTexture(MODEL_WING + 131 + j, L"Item\\Ingameshop\\");
    }

    gLoadData.OpenTexture(MODEL_HELPER + 124, L"Item\\partCharge6\\");
    gLoadData.OpenTexture(MODEL_POTION + 112, L"Item\\Ingameshop\\");
    gLoadData.OpenTexture(MODEL_POTION + 113, L"Item\\Ingameshop\\");

    for (int k = 0; k < 6; k++)
    {
        gLoadData.OpenTexture(MODEL_POTION + 114 + k, L"Item\\InGameShop\\");
    }

    LoadBitmapW(L"Item\\InGameShop\\mebership_3items_green.jpg", BITMAP_INGAMESHOP_PRIMIUM6,
                LegacyTextureFilter::Linear, LegacyTextureWrap::Repeat);
    for (int k = 0; k < 4; k++)
    {
        gLoadData.OpenTexture(MODEL_POTION + 126 + k, L"Item\\InGameShop\\");
    }

    LoadBitmapW(L"Item\\InGameShop\\mebership_3items_red.jpg", BITMAP_INGAMESHOP_COMMUTERTICKET4,
                LegacyTextureFilter::Linear, LegacyTextureWrap::Repeat);
    for (int k = 0; k < 3; k++)
    {
        gLoadData.OpenTexture(MODEL_POTION + 130 + k, L"Item\\InGameShop\\");
    }

    LoadBitmapW(L"Item\\InGameShop\\mebership_3items_yellow.jpg",
                BITMAP_INGAMESHOP_SIZECOMMUTERTICKET3, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Repeat);

    gLoadData.OpenTexture(MODEL_HELPER + 121, L"Item\\InGameShop\\");

#ifdef PBG_ADD_GENSRANKING
    for (int _index = 0; _index < 4; ++_index)
        gLoadData.OpenTexture(MODEL_POTION + 141 + _index, L"Item\\");
#endif //PBG_ADD_GENSRANKING

    gLoadData.OpenTexture(MODEL_15GRADE_ARMOR_OBJ_ARMLEFT,
                          L"Item\\"); // 14, 15 efeito chifres brancos
    gLoadData.OpenTexture(MODEL_15GRADE_ARMOR_OBJ_ARMRIGHT, L"Item\\");  // 14, 15
    gLoadData.OpenTexture(MODEL_15GRADE_ARMOR_OBJ_BODYLEFT, L"Item\\");  // 14, 15
    gLoadData.OpenTexture(MODEL_15GRADE_ARMOR_OBJ_BODYRIGHT, L"Item\\"); // 14, 15
    gLoadData.OpenTexture(MODEL_15GRADE_ARMOR_OBJ_BOOTLEFT, L"Item\\");  // 14, 15
    gLoadData.OpenTexture(MODEL_15GRADE_ARMOR_OBJ_BOOTRIGHT, L"Item\\"); // 14, 15
    gLoadData.OpenTexture(MODEL_15GRADE_ARMOR_OBJ_HEAD, L"Item\\");      // 14, 15
    gLoadData.OpenTexture(MODEL_15GRADE_ARMOR_OBJ_PANTLEFT, L"Item\\");  // 14, 15
    gLoadData.OpenTexture(MODEL_15GRADE_ARMOR_OBJ_PANTRIGHT, L"Item\\"); // 14, 15
    LoadBitmapW(L"Item\\rgb_mix.jpg", BITMAP_RGB_MIX, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Repeat);

    gLoadData.OpenTexture(MODEL_CAPE_OF_FIGHTER, L"Item\\");
    gLoadData.OpenTexture(MODEL_CAPE_OF_OVERRULE, L"Item\\");
    gLoadData.OpenTexture(MODEL_WING + 135, L"Item\\");
    g_CMonkSystem.LoadModelItemTexture();
    for (int _nRollIndex = 0; _nRollIndex < 7; ++_nRollIndex)
        gLoadData.OpenTexture(MODEL_CHAIN_DRIVE_PARCHMENT + _nRollIndex, L"Item\\");

    LoadBitmapW(L"Item\\PhoenixSoul_render.JPG", BITMAP_PHOENIXSOULWING,
                LegacyTextureFilter::Linear, LegacyTextureWrap::Repeat);

    gLoadData.OpenTexture(MODEL_HELPER + 135, L"Item\\LuckyItem\\");
    gLoadData.OpenTexture(MODEL_HELPER + 136, L"Item\\LuckyItem\\");
    gLoadData.OpenTexture(MODEL_HELPER + 137, L"Item\\LuckyItem\\");
    gLoadData.OpenTexture(MODEL_HELPER + 138, L"Item\\LuckyItem\\");
    gLoadData.OpenTexture(MODEL_HELPER + 139, L"Item\\LuckyItem\\");
    gLoadData.OpenTexture(MODEL_HELPER + 140, L"Item\\LuckyItem\\");
    gLoadData.OpenTexture(MODEL_HELPER + 141, L"Item\\LuckyItem\\");
    gLoadData.OpenTexture(MODEL_HELPER + 142, L"Item\\LuckyItem\\");
    gLoadData.OpenTexture(MODEL_HELPER + 143, L"Item\\LuckyItem\\");
    gLoadData.OpenTexture(MODEL_HELPER + 144, L"Item\\LuckyItem\\");

    const wchar_t *szLuckySetPath = {L"Player\\LuckyItem\\"};
    wchar_t szLuckySetPathName[50] = {L""};
    int nIndex = 62;

    for (int i = 0; i < 11; i++)
    {
        mu_swprintf(szLuckySetPathName, L"%ls%d\\", szLuckySetPath, nIndex);
        if (nIndex != 71)
            gLoadData.OpenTexture(MODEL_HELM + nIndex, szLuckySetPathName);
        gLoadData.OpenTexture(MODEL_ARMOR + nIndex, szLuckySetPathName);
        gLoadData.OpenTexture(MODEL_PANTS + nIndex, szLuckySetPathName);
        if (nIndex != 72)
            gLoadData.OpenTexture(MODEL_GLOVES + nIndex, szLuckySetPathName);
        gLoadData.OpenTexture(MODEL_BOOTS + nIndex, szLuckySetPathName);
        nIndex++;
    }

    gLoadData.OpenTexture(MODEL_POTION + 160, L"Item\\LuckyItem\\");
    gLoadData.OpenTexture(MODEL_POTION + 161, L"Item\\LuckyItem\\");

    mu_swprintf(szLuckySetPathName, L"Player\\LuckyItem\\65\\InvenArmorMale40_luck.tga");
    LoadBitmapW(szLuckySetPathName, BITMAP_INVEN_ARMOR + 6);
    mu_swprintf(szLuckySetPathName, L"Player\\LuckyItem\\65\\InvenPantsMale40_luck.tga");
    LoadBitmapW(szLuckySetPathName, BITMAP_INVEN_PANTS + 6);
    mu_swprintf(szLuckySetPathName, L"Player\\LuckyItem\\70\\InvenArmorMale41_luck.tga");
    LoadBitmapW(szLuckySetPathName, BITMAP_INVEN_ARMOR + 7);
    mu_swprintf(szLuckySetPathName, L"Player\\LuckyItem\\70\\InvenPantsMale41_luck.tga");
    LoadBitmapW(szLuckySetPathName, BITMAP_INVEN_PANTS + 7);
}

void SessionRenderUnit::OpenNpc(int Type)
{
    BMD *b = &Models[Type];
    if (b->NumActions > 0)
        return;

    switch (Type)
    {
    case MODEL_MERCHANT_FEMALE:
        gLoadData.AccessModel(MODEL_MERCHANT_FEMALE, L"Data\\Npc\\", L"Female", 1);

        for (int i = 0; i < 2; i++)
        {
            gLoadData.AccessModel(MODEL_MERCHANT_FEMALE_HEAD + i, L"Data\\Npc\\", L"FemaleHead",
                                  i + 1);
            gLoadData.AccessModel(MODEL_MERCHANT_FEMALE_UPPER + i, L"Data\\Npc\\", L"FemaleUpper",
                                  i + 1);
            gLoadData.AccessModel(MODEL_MERCHANT_FEMALE_LOWER + i, L"Data\\Npc\\", L"FemaleLower",
                                  i + 1);
            gLoadData.AccessModel(MODEL_MERCHANT_FEMALE_BOOTS + i, L"Data\\Npc\\", L"FemaleBoots",
                                  i + 1);
            gLoadData.OpenTexture(MODEL_MERCHANT_FEMALE_HEAD + i, L"Npc\\");
            gLoadData.OpenTexture(MODEL_MERCHANT_FEMALE_UPPER + i, L"Npc\\");
            gLoadData.OpenTexture(MODEL_MERCHANT_FEMALE_LOWER + i, L"Npc\\");
            gLoadData.OpenTexture(MODEL_MERCHANT_FEMALE_BOOTS + i, L"Npc\\");
        }
        break;
    case MODEL_MERCHANT_MAN:
        gLoadData.AccessModel(MODEL_MERCHANT_MAN, L"Data\\Npc\\", L"Man", 1);

        for (int i = 0; i < 2; i++)
        {
            gLoadData.AccessModel(MODEL_MERCHANT_MAN_HEAD + i, L"Data\\Npc\\", L"ManHead", i + 1);
            gLoadData.AccessModel(MODEL_MERCHANT_MAN_UPPER + i, L"Data\\Npc\\", L"ManUpper", i + 1);
            gLoadData.AccessModel(MODEL_MERCHANT_MAN_GLOVES + i, L"Data\\Npc\\", L"ManGloves",
                                  i + 1);
            gLoadData.AccessModel(MODEL_MERCHANT_MAN_BOOTS + i, L"Data\\Npc\\", L"ManBoots", i + 1);
            gLoadData.OpenTexture(MODEL_MERCHANT_MAN_HEAD + i, L"Npc\\");
            gLoadData.OpenTexture(MODEL_MERCHANT_MAN_UPPER + i, L"Npc\\");
            gLoadData.OpenTexture(MODEL_MERCHANT_MAN_GLOVES + i, L"Npc\\");
            gLoadData.OpenTexture(MODEL_MERCHANT_MAN_BOOTS + i, L"Npc\\");
        }
        break;
    case MODEL_MERCHANT_GIRL:
        gLoadData.AccessModel(MODEL_MERCHANT_GIRL, L"Data\\Npc\\", L"Girl", 1);

        for (int i = 0; i < 2; i++)
        {
            gLoadData.AccessModel(MODEL_MERCHANT_GIRL_HEAD + i, L"Data\\Npc\\", L"GirlHead", i + 1);
            gLoadData.AccessModel(MODEL_MERCHANT_GIRL_UPPER + i, L"Data\\Npc\\", L"GirlUpper",
                                  i + 1);
            gLoadData.AccessModel(MODEL_MERCHANT_GIRL_LOWER + i, L"Data\\Npc\\", L"GirlLower",
                                  i + 1);
            gLoadData.OpenTexture(MODEL_MERCHANT_GIRL_HEAD + i, L"Npc\\");
            gLoadData.OpenTexture(MODEL_MERCHANT_GIRL_UPPER + i, L"Npc\\");
            gLoadData.OpenTexture(MODEL_MERCHANT_GIRL_LOWER + i, L"Npc\\");
        }
        break;
    case MODEL_SMITH:
        gLoadData.AccessModel(MODEL_SMITH, L"Data\\Npc\\", L"Smith", 1);
        LoadWaveFile(SOUND_NPC_BLACK_SMITH, L"Data\\Sound\\nBlackSmith.wav", 1);
        break;
    case MODEL_SCIENTIST:
        gLoadData.AccessModel(MODEL_SCIENTIST, L"Data\\Npc\\", L"Wizard", 1);
        break;
    case MODEL_SNOW_MERCHANT:
        gLoadData.AccessModel(MODEL_SNOW_MERCHANT, L"Data\\Npc\\", L"SnowMerchant", 1);
        break;
    case MODEL_SNOW_SMITH:
        gLoadData.AccessModel(MODEL_SNOW_SMITH, L"Data\\Npc\\", L"SnowSmith", 1);
        break;
    case MODEL_SNOW_WIZARD:
        gLoadData.AccessModel(MODEL_SNOW_WIZARD, L"Data\\Npc\\", L"SnowWizard", 1);
        break;
    case MODEL_ELF_WIZARD:
        gLoadData.AccessModel(MODEL_ELF_WIZARD, L"Data\\Npc\\", L"ElfWizard", 1);
        LoadWaveFile(SOUND_NPC_HARP, L"Data\\Sound\\nHarp.wav", 1);
        break;
    case MODEL_ELF_MERCHANT:
        gLoadData.AccessModel(MODEL_ELF_MERCHANT, L"Data\\Npc\\", L"ElfMerchant", 1);
        break;
    case MODEL_MASTER:
        gLoadData.AccessModel(MODEL_MASTER, L"Data\\Npc\\", L"Master", 1);
        break;
    case MODEL_STORAGE:
        gLoadData.AccessModel(MODEL_STORAGE, L"Data\\Npc\\", L"Storage", 1);
        break;
    case MODEL_TOURNAMENT:
        gLoadData.AccessModel(MODEL_TOURNAMENT, L"Data\\Npc\\", L"Tournament", 1);
        break;
    case MODEL_MIX_NPC:
        gLoadData.AccessModel(MODEL_MIX_NPC, L"Data\\Npc\\", L"MixNpc", 1);
        LoadWaveFile(SOUND_NPC_MIX, L"Data\\Sound\\nMix.wav", 1);
        break;
    case MODEL_REFINERY_NPC:
        gLoadData.AccessModel(MODEL_REFINERY_NPC, L"Data\\Npc\\", L"os");
        gLoadData.OpenTexture(Type, L"Npc\\");
        break;
    case MODEL_RECOVERY_NPC:
        gLoadData.AccessModel(MODEL_RECOVERY_NPC, L"Data\\Npc\\", L"je");
        gLoadData.OpenTexture(Type, L"Npc\\");
        break;
    case MODEL_NPC_DEVILSQUARE:
        gLoadData.AccessModel(MODEL_NPC_DEVILSQUARE, L"Data\\Npc\\", L"DevilNpc", 1);
        break;

    case MODEL_NPC_SEVINA:
        gLoadData.AccessModel(MODEL_NPC_SEVINA, L"Data\\Npc\\", L"Sevina", 1);
        gLoadData.OpenTexture(Type, L"Npc\\");
        break;
    case MODEL_NPC_ARCHANGEL:
        gLoadData.AccessModel(MODEL_NPC_ARCHANGEL, L"Data\\Npc\\", L"BloodCastle", 1);
        gLoadData.OpenTexture(Type, L"Npc\\");
        break;
    case MODEL_NPC_ARCHANGEL_MESSENGER:
        gLoadData.AccessModel(MODEL_NPC_ARCHANGEL_MESSENGER, L"Data\\Npc\\", L"BloodCastle", 2);
        gLoadData.OpenTexture(Type, L"Npc\\");
        break;

        //  데비아스, 로랜시아 추가 상점 NPC
    case MODEL_DEVIAS_TRADER:
        gLoadData.AccessModel(MODEL_DEVIAS_TRADER, L"Data\\Npc\\", L"DeviasTrader", 1);
        gLoadData.OpenTexture(Type, L"Npc\\");
        break;

#ifdef _PVP_ATTACK_GUARD
    case MODEL_ANGEL:
        gLoadData.AccessModel(MODEL_ANGEL, L"Data\\Player\\", L"Angel");
        gLoadData.OpenTexture(MODEL_ANGEL, L"Npc\\");
        break;
#endif                      // _PVP_ATTACK_GUARD
    case MODEL_NPC_BREEDER: //  조련사 NPC.
        gLoadData.AccessModel(MODEL_NPC_BREEDER, L"Data\\Npc\\", L"Breeder");
        gLoadData.OpenTexture(MODEL_NPC_BREEDER, L"Npc\\");
        break;
#ifdef _PVP_MURDERER_HERO_ITEM
    case MODEL_HERO_SHOP: // 영웅 상점
        gLoadData.AccessModel(MODEL_HERO_SHOP, L"Data\\Npc\\", L"HeroNpc");
        gLoadData.OpenTexture(MODEL_HERO_SHOP, L"Npc\\");
        break;
#endif // _PVP_MURDERER_HERO_ITEM

    case MODEL_NPC_CAPATULT_ATT:
        gLoadData.AccessModel(MODEL_NPC_CAPATULT_ATT, L"Data\\Npc\\", L"Model_Npc_Catapult_Att");
        gLoadData.OpenTexture(MODEL_NPC_CAPATULT_ATT, L"Npc\\");
        break;

    case MODEL_NPC_CAPATULT_DEF:
        gLoadData.AccessModel(MODEL_NPC_CAPATULT_DEF, L"Data\\Npc\\", L"Model_Npc_Catapult_Def");
        gLoadData.OpenTexture(MODEL_NPC_CAPATULT_DEF, L"Npc\\");
        break;

    case MODEL_NPC_SENATUS:
        gLoadData.AccessModel(MODEL_NPC_SENATUS, L"Data\\Npc\\", L"NpcSenatus");
        gLoadData.OpenTexture(MODEL_NPC_SENATUS, L"Npc\\");
        break;

    case MODEL_NPC_GATE_SWITCH:
        gLoadData.AccessModel(MODEL_NPC_GATE_SWITCH, L"Data\\Npc\\", L"NpcGateSwitch");
        gLoadData.OpenTexture(MODEL_NPC_GATE_SWITCH, L"Npc\\");
        break;

    case MODEL_NPC_CROWN:
        gLoadData.AccessModel(MODEL_NPC_CROWN, L"Data\\Npc\\", L"NpcCrown");
        gLoadData.OpenTexture(MODEL_NPC_CROWN, L"Npc\\");
        break;

    case MODEL_NPC_CHECK_FLOOR:
        gLoadData.AccessModel(MODEL_NPC_CHECK_FLOOR, L"Data\\Npc\\", L"NpcCheckFloor");
        gLoadData.OpenTexture(MODEL_NPC_CHECK_FLOOR, L"Npc\\");
        break;

    case MODEL_NPC_CLERK:
        gLoadData.AccessModel(MODEL_NPC_CLERK, L"Data\\Npc\\", L"NpcClerk");
        gLoadData.OpenTexture(MODEL_NPC_CLERK, L"Npc\\");
        break;

    case MODEL_NPC_BARRIER:
        gLoadData.AccessModel(MODEL_NPC_BARRIER, L"Data\\Npc\\", L"NpcBarrier");
        gLoadData.OpenTexture(MODEL_NPC_BARRIER, L"Npc\\");
        break;
    case MODEL_NPC_SERBIS:
        gLoadData.AccessModel(MODEL_NPC_SERBIS, L"Data\\Npc\\", L"npc_mulyak");
        gLoadData.OpenTexture(MODEL_NPC_SERBIS, L"Npc\\");
        break;
    case MODEL_KALIMA_SHOP:
        gLoadData.AccessModel(MODEL_KALIMA_SHOP, L"Data\\Npc\\", L"kalnpc");
        gLoadData.OpenTexture(MODEL_KALIMA_SHOP, L"Npc\\");
        break;
    case MODEL_BC_NPC1:
        gLoadData.AccessModel(MODEL_BC_NPC1, L"Data\\Npc\\", L"npcpharmercy1");
        gLoadData.OpenTexture(MODEL_BC_NPC1, L"Npc\\");
        gLoadData.AccessModel(MODEL_BC_BOX, L"Data\\Npc\\", L"box");
        gLoadData.OpenTexture(MODEL_BC_BOX, L"Npc\\");
        break;
    case MODEL_BC_NPC2:
        gLoadData.AccessModel(MODEL_BC_NPC2, L"Data\\Npc\\", L"npcpharmercy2");
        gLoadData.OpenTexture(MODEL_BC_NPC2, L"Npc\\");
        gLoadData.AccessModel(MODEL_BC_BOX, L"Data\\Npc\\", L"box");
        gLoadData.OpenTexture(MODEL_BC_BOX, L"Npc\\");
        break;
    case MODEL_CRYWOLF_STATUE:
        gLoadData.AccessModel(MODEL_CRYWOLF_STATUE, L"Data\\Object35\\", L"Object82");
        gLoadData.OpenTexture(MODEL_CRYWOLF_STATUE, L"Object35\\");
        break;
    case MODEL_CRYWOLF_ALTAR1:
        gLoadData.AccessModel(MODEL_CRYWOLF_ALTAR1, L"Data\\Object35\\", L"Object57");
        gLoadData.OpenTexture(MODEL_CRYWOLF_ALTAR1, L"Object35\\");
        break;
    case MODEL_CRYWOLF_ALTAR2:
        gLoadData.AccessModel(MODEL_CRYWOLF_ALTAR2, L"Data\\Object35\\", L"Object57");
        gLoadData.OpenTexture(MODEL_CRYWOLF_ALTAR2, L"Object35\\");
        break;
    case MODEL_CRYWOLF_ALTAR3:
        gLoadData.AccessModel(MODEL_CRYWOLF_ALTAR3, L"Data\\Object35\\", L"Object57");
        gLoadData.OpenTexture(MODEL_CRYWOLF_ALTAR3, L"Object35\\");
        break;
    case MODEL_CRYWOLF_ALTAR4:
        gLoadData.AccessModel(MODEL_CRYWOLF_ALTAR4, L"Data\\Object35\\", L"Object57");
        gLoadData.OpenTexture(MODEL_CRYWOLF_ALTAR4, L"Object35\\");
        break;
    case MODEL_CRYWOLF_ALTAR5:
        gLoadData.AccessModel(MODEL_CRYWOLF_ALTAR5, L"Data\\Object35\\", L"Object57");
        gLoadData.OpenTexture(MODEL_CRYWOLF_ALTAR5, L"Object35\\");
        break;
    case MODEL_KANTURU2ND_ENTER_NPC: {
        gLoadData.AccessModel(MODEL_KANTURU2ND_ENTER_NPC, L"Data\\Npc\\", L"to3gate");
        gLoadData.OpenTexture(MODEL_KANTURU2ND_ENTER_NPC, L"Npc\\");
    }
    break;
    case MODEL_SMELTING_NPC:
        gLoadData.AccessModel(MODEL_SMELTING_NPC, L"Data\\Npc\\", L"Elpis");
        gLoadData.OpenTexture(MODEL_SMELTING_NPC, L"Npc\\");
        break;
    case MODEL_NPC_DEVIN:
        gLoadData.AccessModel(MODEL_NPC_DEVIN, L"Data\\Npc\\", L"devin");
        gLoadData.OpenTexture(MODEL_NPC_DEVIN, L"Npc\\");
        break;
    case MODEL_NPC_QUARREL:
        gLoadData.AccessModel(MODEL_NPC_QUARREL, L"Data\\Npc\\", L"WereQuarrel");
        gLoadData.OpenTexture(MODEL_NPC_QUARREL, L"Monster\\");
        break;
    case MODEL_NPC_CASTEL_GATE:
        gLoadData.AccessModel(MODEL_NPC_CASTEL_GATE, L"Data\\Npc\\", L"cry2doorhead");
        gLoadData.OpenTexture(MODEL_NPC_CASTEL_GATE, L"Npc\\");
        break;
    case MODEL_CURSEDTEMPLE_ENTER_NPC:
        gLoadData.AccessModel(MODEL_CURSEDTEMPLE_ENTER_NPC, L"Data\\Npc\\", L"mirazu");
        gLoadData.OpenTexture(MODEL_CURSEDTEMPLE_ENTER_NPC, L"Npc\\");
        break;
    case MODEL_CURSEDTEMPLE_ALLIED_NPC:
        gLoadData.AccessModel(MODEL_CURSEDTEMPLE_ALLIED_NPC, L"Data\\Npc\\", L"allied");
        gLoadData.OpenTexture(MODEL_CURSEDTEMPLE_ALLIED_NPC, L"Npc\\");
        break;
    case MODEL_CURSEDTEMPLE_ILLUSION_NPC:
        gLoadData.AccessModel(MODEL_CURSEDTEMPLE_ILLUSION_NPC, L"Data\\Npc\\", L"illusion");
        gLoadData.OpenTexture(MODEL_CURSEDTEMPLE_ILLUSION_NPC, L"Npc\\");
        break;
    case MODEL_CURSEDTEMPLE_STATUE:
        gLoadData.AccessModel(MODEL_CURSEDTEMPLE_STATUE, L"Data\\Npc\\", L"songsom");
        gLoadData.OpenTexture(MODEL_CURSEDTEMPLE_STATUE, L"Npc\\");
        break;
    case MODEL_CURSEDTEMPLE_ALLIED_BASKET:
        gLoadData.AccessModel(MODEL_CURSEDTEMPLE_ALLIED_BASKET, L"Data\\Npc\\", L"songko");
        gLoadData.OpenTexture(MODEL_CURSEDTEMPLE_ALLIED_BASKET, L"Npc\\");
        break;
    case MODEL_CURSEDTEMPLE_ILLUSION__BASKET:
        gLoadData.AccessModel(MODEL_CURSEDTEMPLE_ILLUSION__BASKET, L"Data\\Npc\\", L"songk2");
        gLoadData.OpenTexture(MODEL_CURSEDTEMPLE_ILLUSION__BASKET, L"Npc\\");
        break;
    case MODEL_WEDDING_NPC:
        gLoadData.AccessModel(MODEL_WEDDING_NPC, L"Data\\Npc\\", L"Wedding");
        gLoadData.OpenTexture(MODEL_WEDDING_NPC, L"Npc\\");
        break;
    case MODEL_ELBELAND_SILVIA:
        gLoadData.AccessModel(MODEL_ELBELAND_SILVIA, L"Data\\Npc\\", L"silvia");
        gLoadData.OpenTexture(MODEL_ELBELAND_SILVIA, L"Npc\\");
        break;
    case MODEL_ELBELAND_RHEA:
        gLoadData.AccessModel(MODEL_ELBELAND_RHEA, L"Data\\Npc\\", L"rhea");
        gLoadData.OpenTexture(MODEL_ELBELAND_RHEA, L"Npc\\");
        break;
    case MODEL_ELBELAND_MARCE:
        gLoadData.AccessModel(MODEL_ELBELAND_MARCE, L"Data\\Npc\\", L"marce");
        gLoadData.OpenTexture(MODEL_ELBELAND_MARCE, L"Npc\\");
        break;
    case MODEL_NPC_CHERRYBLOSSOM:
        gLoadData.AccessModel(MODEL_NPC_CHERRYBLOSSOM, L"Data\\Npc\\cherryblossom\\",
                              L"cherry_blossom");
        gLoadData.OpenTexture(MODEL_NPC_CHERRYBLOSSOM, L"Npc\\cherryblossom\\");
        break;
    case MODEL_NPC_CHERRYBLOSSOMTREE:
        gLoadData.AccessModel(MODEL_NPC_CHERRYBLOSSOMTREE, L"Data\\Npc\\cherryblossom\\",
                              L"sakuratree");
        gLoadData.OpenTexture(MODEL_NPC_CHERRYBLOSSOMTREE, L"Npc\\cherryblossom\\");
        break;
    case MODEL_SEED_MASTER:
        gLoadData.AccessModel(MODEL_SEED_MASTER, L"Data\\Npc\\", L"goblinmaster");
        gLoadData.OpenTexture(MODEL_SEED_MASTER, L"Npc\\");
        break;
    case MODEL_SEED_INVESTIGATOR:
        gLoadData.AccessModel(MODEL_SEED_INVESTIGATOR, L"Data\\Npc\\", L"seedgoblin");
        gLoadData.OpenTexture(MODEL_SEED_INVESTIGATOR, L"Npc\\");
        break;
    case MODEL_LITTLESANTA:
    case MODEL_LITTLESANTA + 1:
    case MODEL_LITTLESANTA + 2:
    case MODEL_LITTLESANTA + 3:
    case MODEL_LITTLESANTA + 4:
    case MODEL_LITTLESANTA + 5:
    case MODEL_LITTLESANTA + 6:
    case MODEL_LITTLESANTA + 7: {
        gLoadData.AccessModel(MODEL_LITTLESANTA + (Type - MODEL_LITTLESANTA), L"Data\\Npc\\",
                              L"xmassanta");

        int _index = 9;
        int _index_end = 14;

        Models[Type].Textures->FileName[_index_end] = 0;
        for (int i = _index_end - 1; i > _index; i--)
        {
            Models[Type].Textures->FileName[i] = Models[Type].Textures->FileName[i - 1];
        }
        int _temp = (Type - MODEL_LITTLESANTA) + 1;
        wchar_t _temp2[10] = {
            0,
        };
        _itow(_temp, _temp2, 10);
        Models[Type].Textures->FileName[_index] = _temp2[0];

        gLoadData.OpenTexture(MODEL_LITTLESANTA + (Type - MODEL_LITTLESANTA), L"Npc\\");
    }
    break;
    case MODEL_XMAS2008_SNOWMAN: {
        gLoadData.AccessModel(MODEL_XMAS2008_SNOWMAN, L"Data\\Item\\xmas\\", L"snowman");
        gLoadData.AccessModel(MODEL_XMAS2008_SNOWMAN_HEAD, L"Data\\Item\\xmas\\",
                              L"snowman_die_head_model");
        gLoadData.AccessModel(MODEL_XMAS2008_SNOWMAN_BODY, L"Data\\Item\\xmas\\",
                              L"snowman_die_body");
        gLoadData.OpenTexture(MODEL_XMAS2008_SNOWMAN, L"Item\\xmas\\");
        gLoadData.OpenTexture(MODEL_XMAS2008_SNOWMAN_HEAD, L"Item\\xmas\\");
        gLoadData.OpenTexture(MODEL_XMAS2008_SNOWMAN_BODY, L"Item\\xmas\\");

        LoadWaveFile(SOUND_XMAS_SNOWMAN_WALK_1, L"Data\\Sound\\xmas\\SnowMan_Walk01.wav");
        LoadWaveFile(SOUND_XMAS_SNOWMAN_ATTACK_1, L"Data\\Sound\\xmas\\SnowMan_Attack01.wav");
        LoadWaveFile(SOUND_XMAS_SNOWMAN_ATTACK_2, L"Data\\Sound\\xmas\\SnowMan_Attack02.wav");
        LoadWaveFile(SOUND_XMAS_SNOWMAN_DAMAGE_1, L"Data\\Sound\\xmas\\SnowMan_Damage01.wav");
        LoadWaveFile(SOUND_XMAS_SNOWMAN_DEATH_1, L"Data\\Sound\\xmas\\SnowMan_Death01.wav");
    }
    break;
    case MODEL_XMAS2008_SNOWMAN_NPC:
        gLoadData.AccessModel(MODEL_XMAS2008_SNOWMAN_NPC, L"Data\\Npc\\", L"snowman");
        gLoadData.OpenTexture(MODEL_XMAS2008_SNOWMAN_NPC, L"Npc\\");
        break;
    case MODEL_XMAS2008_SANTA_NPC:
        gLoadData.AccessModel(MODEL_XMAS2008_SANTA_NPC, L"Data\\Npc\\", L"npcsanta");
        gLoadData.OpenTexture(MODEL_XMAS2008_SANTA_NPC, L"Npc\\");
        break;
    case MODEL_DUEL_NPC_TITUS:
        gLoadData.AccessModel(MODEL_DUEL_NPC_TITUS, L"Data\\Npc\\", L"titus");
        gLoadData.OpenTexture(MODEL_DUEL_NPC_TITUS, L"Npc\\");

        LoadWaveFile(SOUND_DUEL_NPC_IDLE_1, L"Data\\Sound\\w64\\GatekeeperTitus.wav");
        break;
    case MODEL_GAMBLE_NPC_MOSS:
        gLoadData.AccessModel(MODEL_GAMBLE_NPC_MOSS, L"Data\\Npc\\", L"gambler_moss");
        gLoadData.OpenTexture(MODEL_GAMBLE_NPC_MOSS, L"Npc\\");
        break;
    case MODEL_DOPPELGANGER_NPC_LUGARD:
        gLoadData.AccessModel(MODEL_DOPPELGANGER_NPC_LUGARD, L"Data\\Npc\\", L"Lugard");
        gLoadData.OpenTexture(MODEL_DOPPELGANGER_NPC_LUGARD, L"Npc\\");
        LoadWaveFile(SOUND_DOPPELGANGER_LUGARD_BREATH, L"Data\\Sound\\Doppelganger\\Lugard.wav");
        break;
    case MODEL_DOPPELGANGER_NPC_BOX:
        gLoadData.AccessModel(MODEL_DOPPELGANGER_NPC_BOX, L"Data\\Npc\\", L"DoppelgangerBox");
        gLoadData.OpenTexture(MODEL_DOPPELGANGER_NPC_BOX, L"Npc\\");
        LoadWaveFile(SOUND_DOPPELGANGER_JEWELBOX_OPEN,
                     L"Data\\Sound\\Doppelganger\\treasurebox_open.wav");
        break;
    case MODEL_DOPPELGANGER_NPC_GOLDENBOX:
        gLoadData.AccessModel(MODEL_DOPPELGANGER_NPC_GOLDENBOX, L"Data\\Npc\\", L"DoppelgangerBox");
        gLoadData.OpenTexture(MODEL_DOPPELGANGER_NPC_GOLDENBOX, L"Npc\\");
        LoadWaveFile(SOUND_DOPPELGANGER_JEWELBOX_OPEN,
                     L"Data\\Sound\\Doppelganger\\treasurebox_open.wav");
        break;
    case MODAL_GENS_NPC_DUPRIAN:
        gLoadData.AccessModel(MODAL_GENS_NPC_DUPRIAN, L"Data\\Npc\\", L"duprian");
        gLoadData.OpenTexture(MODAL_GENS_NPC_DUPRIAN, L"Npc\\");
        break;
    case MODAL_GENS_NPC_BARNERT:
        gLoadData.AccessModel(MODAL_GENS_NPC_BARNERT, L"Data\\Npc\\", L"barnert");
        gLoadData.OpenTexture(MODAL_GENS_NPC_BARNERT, L"Npc\\");
        break;
    case MODEL_UNITEDMARKETPLACE_CHRISTIN:
        gLoadData.AccessModel(MODEL_UNITEDMARKETPLACE_CHRISTIN, L"Data\\Npc\\",
                              L"UnitedMarketPlace_christine");
        gLoadData.OpenTexture(MODEL_UNITEDMARKETPLACE_CHRISTIN, L"Npc\\");
        break;
    case MODEL_UNITEDMARKETPLACE_RAUL:
        gLoadData.AccessModel(MODEL_UNITEDMARKETPLACE_RAUL, L"Data\\Npc\\",
                              L"UnitedMarkedPlace_raul");
        gLoadData.OpenTexture(MODEL_UNITEDMARKETPLACE_RAUL, L"Npc\\");
        break;
    case MODEL_UNITEDMARKETPLACE_JULIA:
        gLoadData.AccessModel(MODEL_UNITEDMARKETPLACE_JULIA, L"Data\\Npc\\",
                              L"UnitedMarkedPlace_julia");
        gLoadData.OpenTexture(MODEL_UNITEDMARKETPLACE_JULIA, L"Npc\\");
        break;
    case MODEL_KARUTAN_NPC_REINA: // 로랜시장 NPC 잡화상인 크리스틴과 동일.
        gLoadData.AccessModel(MODEL_KARUTAN_NPC_REINA, L"Data\\Npc\\",
                              L"UnitedMarketPlace_christine");
        gLoadData.OpenTexture(MODEL_KARUTAN_NPC_REINA, L"Npc\\");
        break;
    case MODEL_KARUTAN_NPC_VOLVO:
        gLoadData.AccessModel(MODEL_KARUTAN_NPC_VOLVO, L"Data\\Npc\\", L"volvo");
        gLoadData.OpenTexture(MODEL_KARUTAN_NPC_VOLVO, L"Npc\\");
        break;
    case MODEL_LUCKYITEM_NPC:
        gLoadData.AccessModel(MODEL_LUCKYITEM_NPC, L"Data\\Npc\\LuckyItem\\", L"npc_burial");
        gLoadData.OpenTexture(MODEL_LUCKYITEM_NPC, L"Npc\\LuckyItem\\");
        break;
    case MODEL_TERSIA:
        gLoadData.AccessModel(MODEL_TERSIA, L"Data\\Npc\\", L"tersia");
        gLoadData.OpenTexture(MODEL_TERSIA, L"Npc\\");
    case MODEL_BENA:
        gLoadData.AccessModel(MODEL_BENA, L"Data\\Npc\\", L"bena");
        gLoadData.OpenTexture(MODEL_BENA, L"Npc\\");
        break;
    }

    for (int i = 0; i < b->NumActions; i++)
        b->Actions[i].PlaySpeed = 0.25f;
    //SetTexture(BITMAP_NPC);

    if (b->NumMeshs > 0)
        gLoadData.OpenTexture(Type, L"Npc\\");

    switch (Type)
    {
    case MODEL_XMAS2008_SNOWMAN:
        Models[Type].Actions[MONSTER01_WALK].PlaySpeed = 0.9f;
        break;
    case MODEL_PANDA:
        Models[Type].Actions[MONSTER01_WALK].PlaySpeed = 0.9f;
        break;
    case MODEL_DOPPELGANGER_NPC_BOX:
        Models[Type].Actions[MONSTER01_DIE].PlaySpeed = 0.1f;
        break;
    case MODEL_DOPPELGANGER_NPC_GOLDENBOX:
        Models[Type].Actions[MONSTER01_DIE].PlaySpeed = 0.1f;
        break;
    case MODAL_GENS_NPC_DUPRIAN:
        Models[Type].Actions[MONSTER01_STOP1].PlaySpeed = 0.6f;
        Models[Type].Actions[MONSTER01_STOP2].PlaySpeed = 0.6f;
        Models[Type].Actions[MONSTER01_WALK].PlaySpeed = 1.4f;
        break;
    case MODAL_GENS_NPC_BARNERT:
        Models[Type].Actions[MONSTER01_STOP1].PlaySpeed = 0.3f;
        Models[Type].Actions[MONSTER01_STOP2].PlaySpeed = 1.2f;
        Models[Type].Actions[MONSTER01_WALK].PlaySpeed = 0.3f;
    case MODEL_UNITEDMARKETPLACE_RAUL:
        Models[Type].Actions[MONSTER01_STOP1].PlaySpeed = 0.5f;
        Models[Type].Actions[MONSTER01_STOP2].PlaySpeed = 0.5f;
        Models[Type].Actions[MONSTER01_WALK].PlaySpeed = 0.5f;
        break;
    case MODEL_UNITEDMARKETPLACE_JULIA:
        Models[Type].Actions[MONSTER01_STOP1].PlaySpeed = 0.5f;
        Models[Type].Actions[MONSTER01_STOP2].PlaySpeed = 0.5f;
        Models[Type].Actions[MONSTER01_WALK].PlaySpeed = 0.5f;
        break;
    case MODEL_UNITEDMARKETPLACE_CHRISTIN:
    case MODEL_KARUTAN_NPC_REINA:
        Models[Type].Actions[MONSTER01_STOP1].PlaySpeed = 0.5f;
        Models[Type].Actions[MONSTER01_STOP2].PlaySpeed = 0.6f;
        Models[Type].Actions[MONSTER01_WALK].PlaySpeed = 0.5f;
        break;
    case MODEL_TERSIA:
        Models[Type].Actions[MONSTER01_STOP1].PlaySpeed = 0.35f;
        Models[Type].Actions[MONSTER01_STOP2].PlaySpeed = 0.3f;
        break;
    case MODEL_KARUTAN_NPC_VOLVO:
        Models[Type].Actions[MONSTER01_STOP1].PlaySpeed = 0.2f;
        Models[Type].Actions[MONSTER01_STOP2].PlaySpeed = 0.25f;
        break;
    }

    //#endif
}

// Maps a monster model type to its enum identifier for diagnostic logging.
// Indexed directly by the dense, 0-based EMonsterModelType value; returns
// L"UNKNOWN" for values outside the table so the log always has a name.
// The static_assert keeps the table in sync if the enum grows.
void SessionRenderUnit::OpenSkills()
{
    gLoadData.AccessModel(MODEL_ICE, L"Data\\Skill\\", L"Ice", 1);
    gLoadData.AccessModel(MODEL_ICE_SMALL, L"Data\\Skill\\", L"Ice", 2);
    gLoadData.AccessModel(MODEL_FIRE, L"Data\\Skill\\", L"Fire", 1);
    gLoadData.AccessModel(MODEL_POISON, L"Data\\Skill\\", L"Poison", 1);
    for (int i = 0; i < 2; i++)
        gLoadData.AccessModel(MODEL_STONE1 + i, L"Data\\Skill\\", L"Stone", i + 1);
    gLoadData.AccessModel(MODEL_CIRCLE, L"Data\\Skill\\", L"Circle", 1);
    gLoadData.AccessModel(MODEL_CIRCLE_LIGHT, L"Data\\Skill\\", L"Circle", 2);
    gLoadData.AccessModel(MODEL_MAGIC1, L"Data\\Skill\\", L"Magic", 1);
    gLoadData.AccessModel(MODEL_MAGIC2, L"Data\\Skill\\", L"Magic", 2);
    gLoadData.AccessModel(MODEL_STORM, L"Data\\Skill\\", L"Storm", 1);
    gLoadData.AccessModel(MODEL_LASER, L"Data\\Skill\\", L"Laser", 1);

    for (int i = 0; i < 3; i++)
        gLoadData.AccessModel(MODEL_SKELETON1 + i, L"Data\\Skill\\", L"Skeleton", i + 1);

    gLoadData.AccessModel(MODEL_SKELETON_PCBANG, L"Data\\Skill\\", L"Skeleton", 3);
    gLoadData.AccessModel(MODEL_HALLOWEEN, L"Data\\Skill\\", L"Jack");

    gLoadData.AccessModel(MODEL_HALLOWEEN_CANDY_BLUE, L"Data\\Skill\\", L"hcandyblue");
    gLoadData.AccessModel(MODEL_HALLOWEEN_CANDY_ORANGE, L"Data\\Skill\\", L"hcandyorange");
    gLoadData.AccessModel(MODEL_HALLOWEEN_CANDY_RED, L"Data\\Skill\\", L"hcandyred");
    gLoadData.AccessModel(MODEL_HALLOWEEN_CANDY_YELLOW, L"Data\\Skill\\", L"hcandyyellow");
    gLoadData.AccessModel(MODEL_HALLOWEEN_CANDY_HOBAK, L"Data\\Skill\\", L"hhobak");
    gLoadData.AccessModel(MODEL_HALLOWEEN_CANDY_STAR, L"Data\\Skill\\", L"hstar");
    LoadBitmapW(L"Skill\\jack04.jpg", BITMAP_JACK_1);
    LoadBitmapW(L"Skill\\jack05.jpg", BITMAP_JACK_2);
    LoadBitmapW(L"Monster\\iui02.tga", BITMAP_ROBE + 3);
    gLoadData.AccessModel(MODEL_PUMPKIN_OF_LUCK, L"Data\\Item\\", L"hobakhead");
    gLoadData.OpenTexture(MODEL_PUMPKIN_OF_LUCK, L"Item\\");

    gLoadData.AccessModel(MODEL_CURSEDTEMPLE_ALLIED_PLAYER, L"Data\\Skill\\", L"unitedsoldier");
    gLoadData.AccessModel(MODEL_CURSEDTEMPLE_ILLUSION_PLAYER, L"Data\\Skill\\", L"illusionist");

    gLoadData.AccessModel(MODEL_WOOSISTONE, L"Data\\Skill\\", L"woositone");
    gLoadData.OpenTexture(MODEL_WOOSISTONE, L"Skill\\");

    gameplay_.NewYearsDayEvent().LoadModel();

    gLoadData.AccessModel(MODEL_SAW, L"Data\\Skill\\", L"Saw", 1);

    for (int i = 0; i < 2; i++)
        gLoadData.AccessModel(MODEL_BONE1 + i, L"Data\\Skill\\", L"Bone", i + 1);

    for (int i = 0; i < 3; i++)
        gLoadData.AccessModel(MODEL_SNOW1 + i, L"Data\\Skill\\", L"Snow", i + 1);

    gLoadData.AccessModel(MODEL_UNICON, L"Data\\Skill\\", L"Rider", 1);
    gLoadData.AccessModel(MODEL_PEGASUS, L"Data\\Skill\\", L"Rider", 2);
    gLoadData.AccessModel(MODEL_DARK_HORSE, L"Data\\Skill\\", L"DarkHorse");

    gLoadData.AccessModel(MODEL_FENRIR_BLACK, L"Data\\Skill\\", L"fenril_black");
    gLoadData.OpenTexture(MODEL_FENRIR_BLACK, L"Skill\\");

    gLoadData.AccessModel(MODEL_FENRIR_RED, L"Data\\Skill\\", L"fenril_red");
    gLoadData.OpenTexture(MODEL_FENRIR_RED, L"Skill\\");

    gLoadData.AccessModel(MODEL_FENRIR_BLUE, L"Data\\Skill\\", L"fenril_blue");
    gLoadData.OpenTexture(MODEL_FENRIR_BLUE, L"Skill\\");

    gLoadData.AccessModel(MODEL_FENRIR_GOLD, L"Data\\Skill\\", L"fenril_gold");
    gLoadData.OpenTexture(MODEL_FENRIR_BLUE, L"Skill\\");

    gLoadData.AccessModel(MODEL_PANDA, L"Data\\Item\\", L"panda");
    gLoadData.OpenTexture(MODEL_PANDA, L"Item\\");

    gLoadData.AccessModel(MODEL_SKELETON_CHANGED, L"Data\\Item\\", L"trans_skeleton");
    gLoadData.OpenTexture(MODEL_SKELETON_CHANGED, L"Item\\");

    gLoadData.AccessModel(MODEL_DARK_SPIRIT, L"Data\\Skill\\", L"DarkSpirit");
    LoadBitmapW(L"Skill\\dkthreebody_r.jpg", BITMAP_MONSTER_SKIN + 2, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Repeat);

    gLoadData.AccessModel(MODEL_WARCRAFT, L"Data\\Skill\\", L"HellGate");
    Models[MODEL_WARCRAFT].Actions[0].LockPositions = false;
    Models[MODEL_WARCRAFT].Actions[0].PlaySpeed = 0.15f;

    gLoadData.AccessModel(MODEL_ARROW, L"Data\\Skill\\", L"Arrow", 1);
    gLoadData.AccessModel(MODEL_ARROW_STEEL, L"Data\\Skill\\", L"ArrowSteel", 1);
    gLoadData.AccessModel(MODEL_ARROW_THUNDER, L"Data\\Skill\\", L"ArrowThunder", 1);
    gLoadData.AccessModel(MODEL_ARROW_LASER, L"Data\\Skill\\", L"ArrowLaser", 1);
    gLoadData.AccessModel(MODEL_ARROW_V, L"Data\\Skill\\", L"ArrowV", 1);
    gLoadData.AccessModel(MODEL_ARROW_SAW, L"Data\\Skill\\", L"ArrowSaw", 1);
    gLoadData.AccessModel(MODEL_ARROW_NATURE, L"Data\\Skill\\", L"ArrowNature", 1);

    gLoadData.OpenTexture(MODEL_MAGIC_CAPSULE2, L"Skill\\");
    gLoadData.AccessModel(MODEL_MAGIC_CAPSULE2, L"Data\\Skill\\", L"Protect", 2);

    gLoadData.AccessModel(MODEL_ARROW_SPARK, L"Data\\Skill\\", L"Arrow_Spark");
    gLoadData.OpenTexture(MODEL_ARROW_SPARK, L"Skill\\");

    gLoadData.AccessModel(MODEL_DARK_SCREAM, L"Data\\Skill\\", L"darkfirescrem02");
    gLoadData.OpenTexture(MODEL_DARK_SCREAM, L"Skill\\");
    gLoadData.AccessModel(MODEL_DARK_SCREAM_FIRE, L"Data\\Skill\\", L"darkfirescrem01");
    gLoadData.OpenTexture(MODEL_DARK_SCREAM_FIRE, L"Skill\\");
    gLoadData.AccessModel(MODEL_SUMMON, L"Data\\SKill\\", L"nightmaresum");
    gLoadData.OpenTexture(MODEL_SUMMON, L"SKill\\");
    gLoadData.AccessModel(MODEL_MULTI_SHOT1, L"Data\\Effect\\", L"multishot01");
    gLoadData.OpenTexture(MODEL_MULTI_SHOT1, L"Effect\\");
    gLoadData.AccessModel(MODEL_MULTI_SHOT2, L"Data\\Effect\\", L"multishot02");
    gLoadData.OpenTexture(MODEL_MULTI_SHOT2, L"Effect\\");
    gLoadData.AccessModel(MODEL_MULTI_SHOT3, L"Data\\Effect\\", L"multishot03");
    gLoadData.OpenTexture(MODEL_MULTI_SHOT3, L"Effect\\");
    gLoadData.AccessModel(MODEL_DESAIR, L"Data\\SKill\\", L"desair");
    gLoadData.OpenTexture(MODEL_DESAIR, L"SKill\\");
    gLoadData.AccessModel(MODEL_ARROW_RING, L"Data\\Skill\\", L"CW_Bow_Skill");
    gLoadData.OpenTexture(MODEL_ARROW_RING, L"Skill\\");

    gLoadData.AccessModel(MODEL_ARROW_DARKSTINGER, L"Data\\Skill\\", L"sketbows_arrows");
    gLoadData.OpenTexture(MODEL_ARROW_DARKSTINGER, L"Skill\\");
    gLoadData.AccessModel(MODEL_FEATHER, L"Data\\Skill\\", L"darkwing_hetachi");
    gLoadData.OpenTexture(MODEL_FEATHER, L"Skill\\");
    gLoadData.AccessModel(MODEL_FEATHER_FOREIGN, L"Data\\Skill\\", L"darkwing_hetachi");
    gLoadData.OpenTexture(MODEL_FEATHER_FOREIGN, L"Skill\\");
    LoadBitmapW(L"Effect\\Bugbear_R.jpg", BITMAP_BUGBEAR_R, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Effect\\pk_mon02_fire.jpg", BITMAP_PKMON01, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Effect\\pk_mon03_red.jpg", BITMAP_PKMON02, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Effect\\pk_mon02_green.jpg", BITMAP_PKMON03, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Effect\\pk_mon03_green.jpg", BITMAP_PKMON04, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Effect\\lavagiantAa_e.jpg", BITMAP_PKMON05, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Effect\\lavagiantBa_e.jpg", BITMAP_PKMON06, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Effect\\eff_magma_red.jpg", BITMAP_LAVAGIANT_FOOTPRINT_R,
                LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
    LoadBitmapW(L"Effect\\eff_magma_violet.jpg", BITMAP_LAVAGIANT_FOOTPRINT_V,
                LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
    LoadBitmapW(L"Effect\\raymond_sword_R.jpg", BITMAP_RAYMOND_SWORD, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Effect\\mist01.jpg", BITMAP_AG_ADDITION_EFFECT, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Repeat);
    gLoadData.AccessModel(MODEL_ARROW_GAMBLE, L"Data\\Skill\\", L"gamble_arrows01");
    gLoadData.OpenTexture(MODEL_ARROW_GAMBLE, L"Skill\\");

    gLoadData.OpenTexture(MODEL_SPEARSKILL, L"Skill\\");
    gLoadData.AccessModel(MODEL_SPEARSKILL, L"Data\\Skill\\", L"RidingSpear", 1);
    gLoadData.AccessModel(MODEL_PROTECT, L"Data\\Skill\\", L"Protect", 1);

    for (int i = 0; i < 2; i++)
        gLoadData.AccessModel(MODEL_BIG_STONE1 + i, L"Data\\Skill\\", L"BigStone", i + 1);

    gLoadData.AccessModel(MODEL_MAGIC_CIRCLE1, L"Data\\Skill\\", L"MagicCircle", 1);
    gLoadData.AccessModel(MODEL_ARROW_WING, L"Data\\Skill\\", L"ArrowWing", 1);
    gLoadData.AccessModel(MODEL_ARROW_BOMB, L"Data\\Skill\\", L"ArrowBomb", 1);
    gLoadData.AccessModel(MODEL_BALL, L"Data\\Skill\\", L"Ball", 1); //공
    Models[MODEL_BALL].Actions[0].PlaySpeed = 0.5f;
    gLoadData.AccessModel(MODEL_SKILL_BLAST, L"Data\\Skill\\", L"Blast", 1);
    gLoadData.AccessModel(MODEL_SKILL_INFERNO, L"Data\\Skill\\", L"Inferno", 1);
    gLoadData.AccessModel(MODEL_ARROW_DOUBLE, L"Data\\Skill\\", L"ArrowDouble", 1);

    gLoadData.AccessModel(MODEL_ARROW_BEST_CROSSBOW, L"Data\\Skill\\", L"KCross");
    gLoadData.AccessModel(MODEL_ARROW_DRILL, L"Data\\Skill\\", L"Carow");
    gLoadData.AccessModel(MODEL_COMBO, L"Data\\Skill\\", L"combo");

    gLoadData.AccessModel(MODEL_GATE + 0, L"Data\\Object12\\", L"Gate", 1);
    gLoadData.AccessModel(MODEL_GATE + 1, L"Data\\Object12\\", L"Gate", 2);
    gLoadData.AccessModel(MODEL_STONE_COFFIN + 0, L"Data\\Object12\\", L"StoneCoffin", 1);
    gLoadData.AccessModel(MODEL_STONE_COFFIN + 1, L"Data\\Object12\\", L"StoneCoffin", 2);

    for (int i = 0; i < 2; ++i)
    {
        gLoadData.OpenTexture(MODEL_GATE + 1, L"Monster\\");
        gLoadData.OpenTexture(MODEL_STONE_COFFIN + i, L"Monster\\");
    }

    gLoadData.AccessModel(MODEL_AIR_FORCE, L"Data\\Skill\\", L"AirForce");
    gLoadData.AccessModel(MODEL_WAVES, L"Data\\Skill\\", L"m_Waves");
    gLoadData.AccessModel(MODEL_PIERCING2, L"Data\\Skill\\", L"m_Piercing");
    gLoadData.AccessModel(MODEL_PIER_PART, L"Data\\Skill\\", L"PierPart");
    gLoadData.AccessModel(MODEL_DARKLORD_SKILL, L"Data\\Skill\\", L"DarkLordSkill");
    gLoadData.AccessModel(MODEL_GROUND_STONE, L"Data\\Skill\\", L"groundStone");
    Models[MODEL_GROUND_STONE].Actions[0].Loop = false;
    gLoadData.AccessModel(MODEL_GROUND_STONE2, L"Data\\Skill\\", L"groundStone2");
    Models[MODEL_GROUND_STONE2].Actions[0].Loop = false;
    gLoadData.AccessModel(MODEL_WATER_WAVE, L"Data\\Skill\\", L"seamanFX");
    gLoadData.AccessModel(MODEL_SKULL, L"Data\\Skill\\", L"Skull");
    gLoadData.AccessModel(MODEL_LACEARROW, L"Data\\Skill\\", L"LaceArrow");
    gLoadData.OpenTexture(MODEL_LACEARROW, L"Item\\", LegacyTextureWrap::ClampToEdge);

    gLoadData.AccessModel(MODEL_MANY_FLAG, L"Data\\Skill\\", L"ManyFlag");
    gLoadData.AccessModel(MODEL_WEBZEN_MARK, L"Data\\Skill\\", L"MuSign");
    gLoadData.AccessModel(MODEL_STUN_STONE, L"Data\\Skill\\", L"GroundCrystal");
    gLoadData.AccessModel(MODEL_SKIN_SHELL, L"Data\\Skill\\", L"skinshell");
    gLoadData.AccessModel(MODEL_MANA_RUNE, L"Data\\Skill\\", L"ManaRune");
    gLoadData.AccessModel(MODEL_SKILL_JAVELIN, L"Data\\Skill\\", L"Javelin");
    gLoadData.AccessModel(MODEL_ARROW_IMPACT, L"Data\\Skill\\", L"ArrowImpact");
    gLoadData.AccessModel(MODEL_SWORD_FORCE, L"Data\\Skill\\", L"SwordForce");

    gLoadData.AccessModel(MODEL_FLY_BIG_STONE1, L"Data\\Skill\\", L"FlyBigStone1");
    gLoadData.AccessModel(MODEL_FLY_BIG_STONE2, L"Data\\Skill\\", L"FlyBigStone2");
    gLoadData.AccessModel(MODEL_BIG_STONE_PART1, L"Data\\Skill\\", L"FlySmallStone1");
    gLoadData.AccessModel(MODEL_BIG_STONE_PART2, L"Data\\Skill\\", L"FlySmallStone2");
    gLoadData.AccessModel(MODEL_WALL_PART1, L"Data\\Skill\\", L"WallStone1");
    gLoadData.AccessModel(MODEL_WALL_PART2, L"Data\\Skill\\", L"WallStone2");
    gLoadData.AccessModel(MODEL_GATE_PART1, L"Data\\Skill\\", L"GatePart1");
    gLoadData.AccessModel(MODEL_GATE_PART2, L"Data\\Skill\\", L"GatePart2");
    gLoadData.AccessModel(MODEL_GATE_PART3, L"Data\\Skill\\", L"GatePart3");
    gLoadData.AccessModel(MODEL_AURORA, L"Data\\Skill\\", L"Aurora");
    gLoadData.AccessModel(MODEL_TOWER_GATE_PLANE, L"Data\\Skill\\", L"TowerGatePlane");
    gLoadData.AccessModel(MODEL_GOLEM_STONE, L"Data\\Skill\\", L"golem_stone");
    gLoadData.AccessModel(MODEL_FISSURE, L"Data\\Skill\\", L"bossrock");
    gLoadData.AccessModel(MODEL_FISSURE_LIGHT, L"Data\\Skill\\", L"bossrocklight");
    gLoadData.AccessModel(MODEL_PROTECTGUILD, L"Data\\Skill\\", L"ProtectGuild");
    gLoadData.AccessModel(MODEL_DARK_ELF_SKILL, L"Data\\Skill\\", L"elf_skill");
    gLoadData.AccessModel(MODEL_BALGAS_SKILL, L"Data\\Skill\\", L"WaveForce");
    gLoadData.AccessModel(MODEL_DEATH_SPI_SKILL, L"Data\\Skill\\", L"deathsp_eff");
    Models[MODEL_BALGAS_SKILL].Actions[0].Loop = false;

    gLoadData.OpenTexture(MODEL_DARK_ELF_SKILL, L"Skill\\");
    gLoadData.OpenTexture(MODEL_BALGAS_SKILL, L"Skill\\");
    gLoadData.OpenTexture(MODEL_DEATH_SPI_SKILL, L"Skill\\");

    gLoadData.OpenTexture(MODEL_DARK_HORSE, L"Skill\\");
    gLoadData.OpenTexture(MODEL_DARK_SPIRIT, L"Skill\\");

    gLoadData.OpenTexture(MODEL_WARCRAFT, L"Skill\\");
    gLoadData.OpenTexture(MODEL_PEGASUS, L"Skill\\");
    gLoadData.OpenTexture(MODEL_SKILL_FURY_STRIKE + 1, L"Skill\\");
    gLoadData.OpenTexture(MODEL_SKILL_FURY_STRIKE + 2, L"Skill\\");
    gLoadData.OpenTexture(MODEL_SKILL_FURY_STRIKE + 3, L"Skill\\");
    gLoadData.OpenTexture(MODEL_SKILL_FURY_STRIKE + 5, L"Skill\\");
    gLoadData.OpenTexture(MODEL_SKILL_FURY_STRIKE + 7, L"Skill\\");
    gLoadData.OpenTexture(MODEL_SKILL_FURY_STRIKE + 8, L"Skill\\");
    gLoadData.OpenTexture(MODEL_WAVE, L"Skill\\");
    gLoadData.OpenTexture(MODEL_TAIL, L"Skill\\");

    gLoadData.OpenTexture(MODEL_WAVE_FORCE, L"Skill\\");
    gLoadData.OpenTexture(MODEL_BLIZZARD, L"Skill\\");

    gameplay_.XmasEvent().LoadXmasEvent();

    gLoadData.OpenTexture(MODEL_ARROW_BEST_CROSSBOW, L"Skill\\");
    gLoadData.OpenTexture(MODEL_ARROW_DRILL, L"Skill\\");
    gLoadData.OpenTexture(MODEL_COMBO, L"Skill\\");

    gLoadData.OpenTexture(MODEL_AIR_FORCE, L"Skill\\", LegacyTextureWrap::ClampToEdge);
    gLoadData.OpenTexture(MODEL_WAVES, L"Skill\\");
    gLoadData.OpenTexture(MODEL_PIERCING2, L"Skill\\");
    gLoadData.OpenTexture(MODEL_PIER_PART, L"Skill\\");
    gLoadData.OpenTexture(MODEL_GROUND_STONE, L"Skill\\");
    gLoadData.OpenTexture(MODEL_GROUND_STONE2, L"Skill\\");
    gLoadData.OpenTexture(MODEL_WATER_WAVE, L"Skill\\", LegacyTextureWrap::ClampToEdge);
    gLoadData.OpenTexture(MODEL_SKULL, L"Skill\\", LegacyTextureWrap::ClampToEdge);
    gLoadData.OpenTexture(MODEL_MANY_FLAG, L"Skill\\");
    gLoadData.OpenTexture(MODEL_WEBZEN_MARK, L"Skill\\");

    gLoadData.OpenTexture(MODEL_FLY_BIG_STONE1, L"Npc\\");
    gLoadData.OpenTexture(MODEL_FLY_BIG_STONE2, L"Skill\\");
    gLoadData.OpenTexture(MODEL_BIG_STONE_PART1, L"Skill\\");
    gLoadData.OpenTexture(MODEL_BIG_STONE_PART2, L"Skill\\");
    gLoadData.OpenTexture(MODEL_WALL_PART1, L"Object31\\");
    gLoadData.OpenTexture(MODEL_WALL_PART2, L"Object31\\");
    gLoadData.OpenTexture(MODEL_GATE_PART1, L"Monster\\");
    gLoadData.OpenTexture(MODEL_GATE_PART2, L"Monster\\");
    gLoadData.OpenTexture(MODEL_GATE_PART3, L"Monster\\");
    gLoadData.OpenTexture(MODEL_AURORA, L"Monster\\");
    gLoadData.OpenTexture(MODEL_TOWER_GATE_PLANE, L"Skill\\");
    gLoadData.OpenTexture(MODEL_GOLEM_STONE, L"Monster\\");
    gLoadData.OpenTexture(MODEL_FISSURE, L"Skill\\");
    gLoadData.OpenTexture(MODEL_FISSURE_LIGHT, L"Skill\\");
    gLoadData.OpenTexture(MODEL_SKIN_SHELL, L"Effect\\");
    gLoadData.OpenTexture(MODEL_PROTECTGUILD, L"Item\\");

    gLoadData.AccessModel(MODEL_SKILL_FURY_STRIKE + 1, L"Data\\Skill\\", L"EarthQuake", 1);
    gLoadData.AccessModel(MODEL_SKILL_FURY_STRIKE + 2, L"Data\\Skill\\", L"EarthQuake", 2);
    gLoadData.AccessModel(MODEL_SKILL_FURY_STRIKE + 3, L"Data\\Skill\\", L"EarthQuake", 3);
    gLoadData.AccessModel(MODEL_SKILL_FURY_STRIKE + 5, L"Data\\Skill\\", L"EarthQuake", 5);
    gLoadData.AccessModel(MODEL_SKILL_FURY_STRIKE + 7, L"Data\\Skill\\", L"EarthQuake", 7);
    gLoadData.AccessModel(MODEL_SKILL_FURY_STRIKE + 8, L"Data\\Skill\\", L"EarthQuake", 8);
    gLoadData.AccessModel(MODEL_WAVE, L"Data\\Skill\\", L"flashing");
    gLoadData.AccessModel(MODEL_TAIL, L"Data\\Skill\\", L"tail");

    gLoadData.AccessModel(MODEL_SKILL_FURY_STRIKE + 4, L"Data\\Skill\\", L"EarthQuake", 4);
    gLoadData.AccessModel(MODEL_SKILL_FURY_STRIKE + 6, L"Data\\Skill\\", L"EarthQuake", 6);
    gLoadData.AccessModel(MODEL_PIERCING, L"Data\\Skill\\", L"Piercing");

    gLoadData.AccessModel(MODEL_WAVE_FORCE, L"Data\\Skill\\", L"WaveForce");
    gLoadData.AccessModel(MODEL_BLIZZARD, L"Data\\Skill\\", L"Blizzard");

    gLoadData.AccessModel(MODEL_ARROW_AUTOLOAD, L"Data\\Skill\\", L"arrowsrefill");
    gLoadData.OpenTexture(MODEL_ARROW_AUTOLOAD, L"Effect\\");

    gLoadData.AccessModel(MODEL_INFINITY_ARROW, L"Data\\Skill\\", L"arrowsre", 1);
    gLoadData.OpenTexture(MODEL_INFINITY_ARROW, L"Skill\\");
    gLoadData.AccessModel(MODEL_INFINITY_ARROW1, L"Data\\Skill\\", L"arrowsre", 2);
    gLoadData.OpenTexture(MODEL_INFINITY_ARROW1, L"Skill\\");
    gLoadData.AccessModel(MODEL_INFINITY_ARROW2, L"Data\\Skill\\", L"arrowsre", 3);
    gLoadData.OpenTexture(MODEL_INFINITY_ARROW2, L"Skill\\");
    gLoadData.AccessModel(MODEL_INFINITY_ARROW3, L"Data\\Skill\\", L"arrowsre", 4);
    gLoadData.OpenTexture(MODEL_INFINITY_ARROW3, L"Effect\\");
    gLoadData.AccessModel(MODEL_INFINITY_ARROW4, L"Data\\Skill\\", L"arrowsre", 5);
    gLoadData.OpenTexture(MODEL_INFINITY_ARROW4, L"Skill\\");

    gLoadData.AccessModel(MODEL_SHIELD_CRASH, L"Data\\Effect\\", L"atshild");
    gLoadData.OpenTexture(MODEL_SHIELD_CRASH, L"Effect\\");

    gLoadData.AccessModel(MODEL_SHIELD_CRASH2, L"Data\\Effect\\", L"atshild2");
    gLoadData.OpenTexture(MODEL_SHIELD_CRASH2, L"Effect\\");

    gLoadData.AccessModel(MODEL_IRON_RIDER_ARROW, L"Data\\Effect\\", L"ironobj");
    gLoadData.OpenTexture(MODEL_IRON_RIDER_ARROW, L"Effect\\");

    gLoadData.AccessModel(MODEL_KENTAUROS_ARROW, L"Data\\Effect\\", L"cantasarrow");
    gLoadData.OpenTexture(MODEL_KENTAUROS_ARROW, L"Effect\\");

    gLoadData.AccessModel(MODEL_BLADE_SKILL, L"Data\\Effect\\", L"bladetonedo");
    gLoadData.OpenTexture(MODEL_BLADE_SKILL, L"Effect\\");

    gLoadData.AccessModel(MODEL_CHANGE_UP_EFF, L"Data\\Effect\\", L"Change_Up_Eff");
    gLoadData.OpenTexture(MODEL_CHANGE_UP_EFF, L"Effect\\");
    Models[MODEL_CHANGE_UP_EFF].Actions[0].PlaySpeed = 0.005f;
    gLoadData.AccessModel(MODEL_CHANGE_UP_NASA, L"Data\\Effect\\", L"changup_nasa");

    gLoadData.OpenTexture(MODEL_CHANGE_UP_NASA, L"Effect\\");
    gLoadData.AccessModel(MODEL_CHANGE_UP_CYLINDER, L"Data\\Effect\\", L"clinderlight");
    gLoadData.OpenTexture(MODEL_CHANGE_UP_CYLINDER, L"Effect\\");

    gLoadData.AccessModel(MODEL_CURSEDTEMPLE_HOLYITEM, L"Data\\Skill\\", L"eventsungmul");
    gLoadData.OpenTexture(MODEL_CURSEDTEMPLE_HOLYITEM, L"Skill\\");

    gLoadData.AccessModel(MODEL_CURSEDTEMPLE_PRODECTION_SKILL, L"Data\\Skill\\", L"eventshild");
    gLoadData.OpenTexture(MODEL_CURSEDTEMPLE_PRODECTION_SKILL, L"Skill\\");

    gLoadData.AccessModel(MODEL_CURSEDTEMPLE_RESTRAINT_SKILL, L"Data\\Skill\\", L"eventroofe");
    gLoadData.OpenTexture(MODEL_CURSEDTEMPLE_RESTRAINT_SKILL, L"Skill\\");

    gLoadData.AccessModel(MODEL_CURSEDTEMPLE_STATUE_PART1, L"Data\\Npc\\", L"songck1");
    gLoadData.OpenTexture(MODEL_CURSEDTEMPLE_STATUE_PART1, L"Npc\\");

    gLoadData.AccessModel(MODEL_CURSEDTEMPLE_STATUE_PART2, L"Data\\Npc\\", L"songck2");
    gLoadData.OpenTexture(MODEL_CURSEDTEMPLE_STATUE_PART2, L"Npc\\");

    gLoadData.AccessModel(MODEL_FENRIR_THUNDER, L"Data\\Effect\\", L"lightning_type01");
    gLoadData.OpenTexture(MODEL_FENRIR_THUNDER, L"Effect\\");

    for (int i = MODEL_SKILL_BEGIN; i < MODEL_SKILL_END; i++)
    {
        if (i == MODEL_PIERCING)
        {
            gLoadData.OpenTexture(i, L"Skill\\");
        }
        else
            gLoadData.OpenTexture(i, L"Skill\\");
    }

    LoadBitmapW(L"Skill\\flower1.tga", BITMAP_FLOWER01);
    LoadBitmapW(L"Skill\\flower2.tga", BITMAP_FLOWER01 + 1);
    LoadBitmapW(L"Skill\\flower3.tga", BITMAP_FLOWER01 + 2);

    gLoadData.AccessModel(MODEL_MOONHARVEST_GAM, L"Data\\Effect\\", L"chusukgam");
    gLoadData.OpenTexture(MODEL_MOONHARVEST_GAM, L"Effect\\");
    gLoadData.AccessModel(MODEL_MOONHARVEST_SONGPUEN1, L"Data\\Effect\\", L"chusukseung1");
    gLoadData.OpenTexture(MODEL_MOONHARVEST_SONGPUEN1, L"Effect\\");
    gLoadData.AccessModel(MODEL_MOONHARVEST_SONGPUEN2, L"Data\\Effect\\", L"chusukseung2");
    gLoadData.OpenTexture(MODEL_MOONHARVEST_SONGPUEN2, L"Effect\\");
    gLoadData.AccessModel(MODEL_MOONHARVEST_MOON, L"Data\\Effect\\", L"chysukmoon");
    gLoadData.OpenTexture(MODEL_MOONHARVEST_MOON, L"Effect\\");

    gLoadData.AccessModel(MODEL_ALICE_BUFFSKILL_EFFECT, L"Data\\Effect\\", L"elshildring");
    gLoadData.OpenTexture(MODEL_ALICE_BUFFSKILL_EFFECT, L"Effect\\");
    gLoadData.AccessModel(MODEL_ALICE_BUFFSKILL_EFFECT2, L"Data\\Effect\\", L"elshildring2");
    gLoadData.OpenTexture(MODEL_ALICE_BUFFSKILL_EFFECT2, L"Effect\\");

    gLoadData.AccessModel(MODEL_SUMMONER_WRISTRING_EFFECT, L"Data\\Effect\\", L"ringtyperout");
    gLoadData.OpenTexture(MODEL_SUMMONER_WRISTRING_EFFECT, L"Effect\\");

    gLoadData.AccessModel(MODEL_SUMMONER_EQUIP_HEAD_SAHAMUTT, L"Data\\Skill\\", L"sahatail");
    gLoadData.OpenTexture(MODEL_SUMMONER_EQUIP_HEAD_SAHAMUTT, L"Skill\\");
    gLoadData.AccessModel(MODEL_SUMMONER_EQUIP_HEAD_NEIL, L"Data\\Skill\\", L"nillsohwanz");
    gLoadData.OpenTexture(MODEL_SUMMONER_EQUIP_HEAD_NEIL, L"Skill\\");

    gLoadData.AccessModel(MODEL_SUMMONER_EQUIP_HEAD_LAGUL, L"Data\\Skill\\", L"lagul_head");
    gLoadData.OpenTexture(MODEL_SUMMONER_EQUIP_HEAD_LAGUL, L"Skill\\");

    gLoadData.AccessModel(MODEL_SUMMONER_CASTING_EFFECT1, L"Data\\Effect\\", L"Suhwanzin1");
    gLoadData.OpenTexture(MODEL_SUMMONER_CASTING_EFFECT1, L"Effect\\");
    gLoadData.AccessModel(MODEL_SUMMONER_CASTING_EFFECT11, L"Data\\Effect\\", L"Suhwanzin11");
    gLoadData.OpenTexture(MODEL_SUMMONER_CASTING_EFFECT11, L"Effect\\");
    gLoadData.AccessModel(MODEL_SUMMONER_CASTING_EFFECT111, L"Data\\Effect\\", L"Suhwanzin111");
    gLoadData.OpenTexture(MODEL_SUMMONER_CASTING_EFFECT111, L"Effect\\");
    gLoadData.AccessModel(MODEL_SUMMONER_CASTING_EFFECT2, L"Data\\Effect\\", L"Suhwanzin2");
    gLoadData.OpenTexture(MODEL_SUMMONER_CASTING_EFFECT2, L"Effect\\");
    gLoadData.AccessModel(MODEL_SUMMONER_CASTING_EFFECT22, L"Data\\Effect\\", L"Suhwanzin22");
    gLoadData.OpenTexture(MODEL_SUMMONER_CASTING_EFFECT22, L"Effect\\");
    gLoadData.AccessModel(MODEL_SUMMONER_CASTING_EFFECT222, L"Data\\Effect\\", L"Suhwanzin222");
    gLoadData.OpenTexture(MODEL_SUMMONER_CASTING_EFFECT222, L"Effect\\");
    gLoadData.AccessModel(MODEL_SUMMONER_CASTING_EFFECT4, L"Data\\Effect\\", L"Suhwanzin4");
    gLoadData.OpenTexture(MODEL_SUMMONER_CASTING_EFFECT4, L"Effect\\");
    gLoadData.AccessModel(MODEL_SUMMONER_SUMMON_SAHAMUTT, L"Data\\Skill\\", L"summon_sahamutt");
    gLoadData.OpenTexture(MODEL_SUMMONER_SUMMON_SAHAMUTT, L"Skill\\");
    gLoadData.AccessModel(MODEL_SUMMONER_SUMMON_NEIL, L"Data\\Skill\\", L"summon_neil");
    gLoadData.OpenTexture(MODEL_SUMMONER_SUMMON_NEIL, L"Skill\\", LegacyTextureWrap::Repeat,
                          LegacyTextureFilter::Nearest, false);
    gLoadData.OpenTexture(MODEL_SUMMONER_SUMMON_NEIL, L"Effect\\");
    gLoadData.AccessModel(MODEL_SUMMONER_SUMMON_LAGUL, L"Data\\Skill\\", L"summon_lagul");
    gLoadData.OpenTexture(MODEL_SUMMONER_SUMMON_LAGUL, L"Skill\\");

    gLoadData.AccessModel(MODEL_SUMMONER_SUMMON_NEIL_NIFE1, L"Data\\Skill\\", L"nelleff_nife01");
    gLoadData.OpenTexture(MODEL_SUMMONER_SUMMON_NEIL_NIFE1, L"Skill\\");
    gLoadData.AccessModel(MODEL_SUMMONER_SUMMON_NEIL_NIFE2, L"Data\\Skill\\", L"nelleff_nife02");
    gLoadData.OpenTexture(MODEL_SUMMONER_SUMMON_NEIL_NIFE2, L"Skill\\");
    gLoadData.AccessModel(MODEL_SUMMONER_SUMMON_NEIL_NIFE3, L"Data\\Skill\\", L"nelleff_nife03");
    gLoadData.OpenTexture(MODEL_SUMMONER_SUMMON_NEIL_NIFE3, L"Skill\\");
    gLoadData.AccessModel(MODEL_SUMMONER_SUMMON_NEIL_GROUND1, L"Data\\Skill\\",
                          L"nell_nifegrund01");
    gLoadData.OpenTexture(MODEL_SUMMONER_SUMMON_NEIL_GROUND1, L"Skill\\");
    gLoadData.AccessModel(MODEL_SUMMONER_SUMMON_NEIL_GROUND2, L"Data\\Skill\\",
                          L"nell_nifegrund02");
    gLoadData.OpenTexture(MODEL_SUMMONER_SUMMON_NEIL_GROUND2, L"Skill\\");
    gLoadData.AccessModel(MODEL_SUMMONER_SUMMON_NEIL_GROUND3, L"Data\\Skill\\",
                          L"nell_nifegrund03");
    gLoadData.OpenTexture(MODEL_SUMMONER_SUMMON_NEIL_GROUND3, L"Skill\\");
    gLoadData.AccessModel(MODEL_MOVE_TARGETPOSITION_EFFECT, L"Data\\Effect\\",
                          L"MoveTargetPosEffect");
    gLoadData.OpenTexture(MODEL_MOVE_TARGETPOSITION_EFFECT, L"Effect\\");
    gLoadData.AccessModel(MODEL_EFFECT_SAPITRES_ATTACK_1, L"Data\\Effect\\", L"Sapiatttres");
    gLoadData.OpenTexture(MODEL_EFFECT_SAPITRES_ATTACK_1, L"Effect\\");
    gLoadData.AccessModel(MODEL_EFFECT_SAPITRES_ATTACK_2, L"Data\\Effect\\", L"Sapiatttres2");
    gLoadData.OpenTexture(MODEL_EFFECT_SAPITRES_ATTACK_2, L"Effect\\");

    gLoadData.AccessModel(MODEL_RAKLION_BOSS_CRACKEFFECT, L"Data\\Effect\\",
                          L"knight_plancrack_grand");
    gLoadData.OpenTexture(MODEL_RAKLION_BOSS_CRACKEFFECT, L"Effect\\");
    gLoadData.AccessModel(MODEL_RAKLION_BOSS_MAGIC, L"Data\\Effect\\", L"serufan_magic");
    gLoadData.OpenTexture(MODEL_RAKLION_BOSS_MAGIC, L"Effect\\");
    //Models[MODEL_RAKLION_BOSS_MAGIC].Actions[0].PlaySpeed = 0.005f;

    gLoadData.AccessModel(MODEL_EFFECT_SKURA_ITEM, L"Data\\Effect\\cherryblossom\\",
                          L"Skura_iteam_event");

    gLoadData.AccessModel(MODEL_EFFECT_BROKEN_ICE0, L"Data\\Effect\\", L"ice_stone00");
    gLoadData.OpenTexture(MODEL_EFFECT_BROKEN_ICE0, L"Effect\\");
    gLoadData.AccessModel(MODEL_EFFECT_BROKEN_ICE1, L"Data\\Effect\\", L"ice_stone01");
    gLoadData.OpenTexture(MODEL_EFFECT_BROKEN_ICE1, L"Effect\\");
    gLoadData.AccessModel(MODEL_EFFECT_BROKEN_ICE2, L"Data\\Effect\\", L"ice_stone02");
    gLoadData.OpenTexture(MODEL_EFFECT_BROKEN_ICE2, L"Effect\\");
    gLoadData.AccessModel(MODEL_EFFECT_BROKEN_ICE3, L"Data\\Effect\\", L"ice_stone03");
    gLoadData.OpenTexture(MODEL_EFFECT_BROKEN_ICE3, L"Effect\\");
    gLoadData.AccessModel(MODEL_NIGHTWATER_01, L"Data\\Effect\\", L"nightwater01");
    gLoadData.OpenTexture(MODEL_NIGHTWATER_01, L"Effect\\");
    gLoadData.AccessModel(MODEL_KNIGHT_PLANCRACK_A, L"Data\\Effect\\", L"knight_plancrack_a");
    gLoadData.OpenTexture(MODEL_KNIGHT_PLANCRACK_A, L"Effect\\");
    Models[MODEL_KNIGHT_PLANCRACK_A].Actions[0].PlaySpeed = 0.3f;
    gLoadData.AccessModel(MODEL_KNIGHT_PLANCRACK_B, L"Data\\Effect\\", L"knight_plancrack_b");
    gLoadData.OpenTexture(MODEL_KNIGHT_PLANCRACK_B, L"Effect\\");
    Models[MODEL_KNIGHT_PLANCRACK_B].Actions[0].PlaySpeed = 0.3f;
    gLoadData.AccessModel(MODEL_EFFECT_FLAME_STRIKE, L"Data\\Effect\\", L"FlameStrike");
    gLoadData.OpenTexture(MODEL_EFFECT_FLAME_STRIKE, L"Effect\\");
    gLoadData.AccessModel(MODEL_SWELL_OF_MAGICPOWER, L"Data\\Effect\\", L"magic_powerup");
    gLoadData.OpenTexture(MODEL_SWELL_OF_MAGICPOWER, L"Effect\\");
    gLoadData.AccessModel(MODEL_ARROWSRE06, L"Data\\Effect\\", L"arrowsre06");
    gLoadData.OpenTexture(MODEL_ARROWSRE06, L"Effect\\");
    gLoadData.AccessModel(MODEL_DOPPELGANGER_SLIME_CHIP, L"Data\\Effect\\", L"slime_chip");
    gLoadData.OpenTexture(MODEL_DOPPELGANGER_SLIME_CHIP, L"Effect\\");
    gLoadData.AccessModel(MODEL_EFFECT_UMBRELLA_GOLD, L"Data\\Effect\\", L"japan_gold01");
    gLoadData.OpenTexture(MODEL_EFFECT_UMBRELLA_GOLD, L"Effect\\");
    gLoadData.AccessModel(MODEL_EMPIREGUARDIANBOSS_FRAMESTRIKE, L"Data\\Effect\\",
                          L"Karanebos_sword_framestrike");
    gLoadData.OpenTexture(MODEL_EMPIREGUARDIANBOSS_FRAMESTRIKE, L"Effect\\");
    //Models[MODEL_EMPIREGUARDIANBOSS_FRAMESTRIKE].Actions[MONSTER01_STOP1].PlaySpeed = 3.0f;
    gLoadData.AccessModel(MODEL_DEASULER, L"Data\\Monster\\", L"deasther_boomerang");
    gLoadData.OpenTexture(MODEL_DEASULER, L"Monster\\");
    gLoadData.AccessModel(MODEL_EFFECT_SD_AURA, L"Data\\Effect\\", L"shield_up");
    gLoadData.OpenTexture(MODEL_EFFECT_SD_AURA, L"Effect\\");
    gLoadData.AccessModel(MODEL_WOLF_HEAD_EFFECT, L"Data\\Effect\\", L"wolf_head_effect");
    gLoadData.OpenTexture(MODEL_WOLF_HEAD_EFFECT, L"Effect\\");
    LoadBitmapW(L"Effect\\sbumb.jpg", BITMAP_SBUMB, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Repeat);
    gLoadData.AccessModel(MODEL_DOWN_ATTACK_DUMMY_L, L"Data\\Effect\\", L"down_right_punch");
    gLoadData.OpenTexture(MODEL_DOWN_ATTACK_DUMMY_L, L"Effect\\");
    gLoadData.AccessModel(MODEL_DOWN_ATTACK_DUMMY_R, L"Data\\Effect\\", L"down_left_punch");
    gLoadData.OpenTexture(MODEL_DOWN_ATTACK_DUMMY_R, L"Effect\\");
    gLoadData.AccessModel(MODEL_SHOCKWAVE01, L"Data\\Effect\\", L"shockwave01");
    gLoadData.OpenTexture(MODEL_SHOCKWAVE01, L"Effect\\");
    gLoadData.AccessModel(MODEL_SHOCKWAVE02, L"Data\\Effect\\", L"shockwave02");
    gLoadData.OpenTexture(MODEL_SHOCKWAVE02, L"Effect\\");
    gLoadData.AccessModel(MODEL_SHOCKWAVE_SPIN01, L"Data\\Effect\\", L"shockwave_spin01");
    gLoadData.OpenTexture(MODEL_SHOCKWAVE_SPIN01, L"Effect\\");
    gLoadData.AccessModel(MODEL_WINDFOCE, L"Data\\Effect\\", L"wind_foce");
    gLoadData.OpenTexture(MODEL_WINDFOCE, L"Effect\\");
    gLoadData.AccessModel(MODEL_WINDFOCE_MIRROR, L"Data\\Effect\\", L"wind_foce_mirror");
    gLoadData.OpenTexture(MODEL_WINDFOCE_MIRROR, L"Effect\\");
    gLoadData.AccessModel(MODEL_WOLF_HEAD_EFFECT2, L"Data\\Effect\\", L"wolf_head_effect2");
    gLoadData.OpenTexture(MODEL_WOLF_HEAD_EFFECT2, L"skill\\");
    gLoadData.AccessModel(MODEL_SHOCKWAVE_GROUND01, L"Data\\Effect\\", L"shockwave_ground01");
    gLoadData.OpenTexture(MODEL_SHOCKWAVE_GROUND01, L"Effect\\");
    gLoadData.AccessModel(MODEL_DRAGON_KICK_DUMMY, L"Data\\Effect\\", L"dragon_kick_dummy");
    gLoadData.OpenTexture(MODEL_DRAGON_KICK_DUMMY, L"Effect\\");
    gLoadData.AccessModel(MODEL_DRAGON_LOWER_DUMMY, L"Data\\Effect\\", L"knight_plancrack_dragon");
    gLoadData.OpenTexture(MODEL_DRAGON_LOWER_DUMMY, L"Effect\\");
    gLoadData.AccessModel(MODEL_VOLCANO_OF_MONK, L"Data\\Effect\\", L"volcano_of_monk");
    gLoadData.OpenTexture(MODEL_VOLCANO_OF_MONK, L"Effect\\");
    gLoadData.AccessModel(MODEL_VOLCANO_STONE, L"Data\\Effect\\", L"volcano_stone");
    gLoadData.OpenTexture(MODEL_VOLCANO_STONE, L"Effect\\");
    LoadBitmapW(L"Effect\\force_Pillar.jpg", BITMAP_FORCEPILLAR, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Effect\\!SwordEff.jpg", BITMAP_SWORDEFF, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Effect\\Damage1.jpg", BITMAP_DAMAGE1, LegacyTextureFilter::Linear,
                LegacyTextureWrap::ClampToEdge);
    LoadBitmapW(L"Effect\\ground_wind.jpg", BITMAP_GROUND_WIND, LegacyTextureFilter::Linear,
                LegacyTextureWrap::ClampToEdge);
    LoadBitmapW(L"Effect\\Kwave2.jpg", BITMAP_KWAVE2, LegacyTextureFilter::Linear,
                LegacyTextureWrap::ClampToEdge);
    LoadBitmapW(L"Effect\\Damage2.jpg", BITMAP_DAMAGE2, LegacyTextureFilter::Linear,
                LegacyTextureWrap::ClampToEdge);
    LoadBitmapW(L"Effect\\volcano_core.jpg", BITMAP_VOLCANO_CORE, LegacyTextureFilter::Linear,
                LegacyTextureWrap::ClampToEdge);
    gLoadData.AccessModel(MODEL_SHOCKWAVE03, L"Data\\Effect\\", L"shockwave03");
    gLoadData.OpenTexture(MODEL_SHOCKWAVE03, L"Effect\\");
    LoadBitmapW(L"Effect\\ground_smoke.tga", BITMAP_GROUND_SMOKE, LegacyTextureFilter::Linear,
                LegacyTextureWrap::ClampToEdge);
    LoadBitmapW(L"Effect\\knightSt_blue.jpg", BITMAP_KNIGHTST_BLUE, LegacyTextureFilter::Linear,
                LegacyTextureWrap::ClampToEdge);

    gLoadData.AccessModel(MODEL_PHOENIX_SHOT, L"Data\\Effect\\", L"phoenix_shot_effect");
    gLoadData.OpenTexture(MODEL_PHOENIX_SHOT, L"Effect\\");
    gLoadData.AccessModel(MODEL_WINDSPIN01, L"Data\\Effect\\", L"wind_spin01");
    gLoadData.OpenTexture(MODEL_WINDSPIN01, L"Effect\\");
    gLoadData.AccessModel(MODEL_WINDSPIN02, L"Data\\Effect\\", L"wind_spin02");
    gLoadData.OpenTexture(MODEL_WINDSPIN02, L"Effect\\");
    gLoadData.AccessModel(MODEL_WINDSPIN03, L"Data\\Effect\\", L"wind_spin03");
    gLoadData.OpenTexture(MODEL_WINDSPIN03, L"Effect\\");

    gLoadData.AccessModel(MODEL_SWORD_35_WING, L"Data\\Item\\", L"sword36wing");
    gLoadData.OpenTexture(MODEL_SWORD_35_WING, L"Item\\");

#ifdef ASG_ADD_KARUTAN_MONSTERS
    // 콘드라 돌조각
    gLoadData.AccessModel(MODEL_CONDRA_STONE, L"Data\\Monster\\", L"condra_7_stone");
    gLoadData.OpenTexture(MODEL_CONDRA_STONE, L"Monster\\");
    gLoadData.AccessModel(MODEL_CONDRA_STONE1, L"Data\\Monster\\", L"condra_7_stone_2");
    gLoadData.OpenTexture(MODEL_CONDRA_STONE1, L"Monster\\");
    gLoadData.AccessModel(MODEL_CONDRA_STONE2, L"Data\\Monster\\", L"condra_7_stone_3");
    gLoadData.OpenTexture(MODEL_CONDRA_STONE2, L"Monster\\");
    gLoadData.AccessModel(MODEL_CONDRA_STONE3, L"Data\\Monster\\", L"condra_7_stone_4");
    gLoadData.OpenTexture(MODEL_CONDRA_STONE3, L"Monster\\");
    gLoadData.AccessModel(MODEL_CONDRA_STONE4, L"Data\\Monster\\", L"condra_7_stone_5");
    gLoadData.OpenTexture(MODEL_CONDRA_STONE4, L"Monster\\");
    gLoadData.AccessModel(MODEL_CONDRA_STONE5, L"Data\\Monster\\", L"condra_7_stone_6");
    gLoadData.OpenTexture(MODEL_CONDRA_STONE5, L"Monster\\");

    gLoadData.AccessModel(MODEL_NARCONDRA_STONE, L"Data\\Monster\\", L"nar_condra_7_stone_1");
    gLoadData.OpenTexture(MODEL_NARCONDRA_STONE, L"Monster\\");
    gLoadData.AccessModel(MODEL_NARCONDRA_STONE1, L"Data\\Monster\\", L"nar_condra_7_stone_2");
    gLoadData.OpenTexture(MODEL_NARCONDRA_STONE1, L"Monster\\");
    gLoadData.AccessModel(MODEL_NARCONDRA_STONE2, L"Data\\Monster\\", L"nar_condra_7_stone_3");
    gLoadData.OpenTexture(MODEL_NARCONDRA_STONE2, L"Monster\\");
    gLoadData.AccessModel(MODEL_NARCONDRA_STONE3, L"Data\\Monster\\", L"nar_condra_7_stone_4");
    gLoadData.OpenTexture(MODEL_NARCONDRA_STONE3, L"Monster\\");
#endif // ASG_ADD_KARUTAN_MONSTERS
}

void SessionRenderUnit::ConfigureCharacterSceneModels()
{
    constexpr int Class = MAX_CLASS;
    for (int i = 0; i < Class; i++)
    {
        Models[MODEL_FACE + i].Actions[0].PlaySpeed = 0.3f;
        Models[MODEL_FACE + i].Actions[1].PlaySpeed = 0.3f;
    }
    Models[static_cast<int>(MODEL_FACE) + CLASS_SUMMONER].Actions[0].PlaySpeed = 0.25f;
}

bool BMD::BindSharedAsset(const std::shared_ptr<BmdSharedAsset> &asset) noexcept
{
    const int meshCount = asset->meshCount > 0 ? asset->meshCount : 1;
    const int actionCount = asset->actionCount > 0 ? asset->actionCount : 1;
    auto *meshes = new (std::nothrow) Mesh_t[meshCount]();
    auto *actions = new (std::nothrow) Action_t[actionCount]();
    auto *indexTextures = new (std::nothrow) unsigned int[meshCount];
    if (meshes == nullptr || actions == nullptr || indexTextures == nullptr)
    {
        delete[] meshes;
        delete[] actions;
        delete[] indexTextures;
        return false;
    }

    std::fill_n(indexTextures, meshCount, BITMAP_UNKNOWN);
    Release();
    std::copy_n(asset->meshes, asset->meshCount, meshes);
    std::copy_n(asset->actions, asset->actionCount, actions);
    std::memcpy(Name, asset->name, sizeof(Name));
    Version = asset->version;
    NumBones = asset->boneCount;
    NumMeshs = asset->meshCount;
    NumActions = asset->actionCount;
    Meshs = meshes;
    Bones = asset->bones;
    Actions = actions;
    Textures = asset->textures;
    IndexTexture = indexTextures;
    renderTapeGeometry_ = {{}, asset->vertices, asset->indices};
    renderTapeRigidGeometry_ = {{}, asset->rigidVertices, asset->indices};
    renderTapeRigidGeometryRevision_ = 0;
    renderTapeGeometryRevision_ = 0;
    sharedAsset_ = asset;
    BoneHead = -1;
    StreamMesh = -1;
    m_bCompletedAlloc = true;
    InvalidateCharacterPoses();
    return true;
}
#define AXIS_X 0
#define AXIS_Y 1
#define AXIS_Z 2

void BMD::ReleaseLightMaps()
{
    if (LightMapEnable == false)
        return;
    for (int i = 0; i < NumLightMaps; i++)
    {
        Bitmap_t *lmp = &LightMaps[i];
        if (lmp->Buffer != nullptr)
        {
            delete lmp->Buffer;
            lmp->Buffer = nullptr;
        }
    }
    LightMapEnable = false;
}
void BMD::ReleaseTextures()
{
    if (!IndexTexture)
        return;
    for (int index = 0; index < NumMeshs; ++index)
    {
        const auto texture = IndexTexture[index];
        if (texture >= BITMAP_SKIN_BEGIN && texture <= BITMAP_SKIN_END)
            continue;
        DeleteBitmap(texture);
    }
}

void BMD::Release()
{
    std::vector<PreparedRigidMesh>().swap(rigidInstanceMeshes_);
    renderTapeGeometry_ = {};
    renderTapeRigidGeometry_ = {};
    renderTapeRigidGeometryRevision_ = 0;
    renderTapeGeometryRevision_ = 0;
    renderTapePaletteReady_ = false;
    const bool ownsDecodedData = sharedAsset_ == nullptr;
    if (ownsDecodedData && Bones)
    {
        for (int i = 0; i < NumBones; ++i)
        {
            Bone_t *b = &Bones[i];

            if (!b->Dummy && b->BoneMatrixes)
            {
                for (int j = 0; j < NumActions; ++j)
                {
                    BoneMatrix_t *bm = &b->BoneMatrixes[j];
                    if (bm)
                    {
                        if (bm->Position)
                        {
                            delete[] bm->Position;
                            bm->Position = nullptr;
                        }
                        if (bm->Rotation)
                        {
                            delete[] bm->Rotation;
                            bm->Rotation = nullptr;
                        }
                        if (bm->Quaternion)
                        {
                            delete[] bm->Quaternion;
                            bm->Quaternion = nullptr;
                        }
                    }
                }
                if (b->BoneMatrixes)
                {
                    delete[] b->BoneMatrixes;
                    b->BoneMatrixes = nullptr;
                }
            }
        }
    }

    if (ownsDecodedData && Actions)
    {
        for (int i = 0; i < NumActions; ++i)
        {
            Action_t *a = &Actions[i];
            if (a && a->LockPositions && a->Positions)
            {
                delete[] a->Positions;
                a->Positions = nullptr;
            }
        }
    }

    if (Meshs)
    {
        for (int i = 0; i < NumMeshs; ++i)
        {
            Mesh_t *m = &Meshs[i];

            if (ownsDecodedData)
            {
                delete[] m->Vertices;
                delete[] m->Normals;
                delete[] m->TexCoords;
                delete[] m->Triangles;
                delete m->m_csTScript;
            }
        }
    }

    ReleaseTextures();

    if (Meshs)
    {
        delete[] Meshs;
        Meshs = nullptr;
    }
    if (ownsDecodedData && Bones)
    {
        delete[] Bones;
    }
    Bones = nullptr;
    if (Actions)
    {
        delete[] Actions;
        Actions = nullptr;
    }
    if (ownsDecodedData && Textures)
    {
        delete[] Textures;
    }
    Textures = nullptr;
    if (IndexTexture)
    {
        delete[] IndexTexture;
        IndexTexture = nullptr;
    }
    sharedAsset_.reset();

    NumBones = 0;
    NumActions = 0;
    NumMeshs = 0;

#ifdef LDS_FIX_SETNULLALLOCVALUE_WHEN_BMDRELEASE
    m_bCompletedAlloc = false;
#endif
}

//#endif //USE_SHADOWVOLUME

bool BMD::Open2(const wchar_t *directory, const wchar_t *fileName, bool reallocate)
{
    if (m_bCompletedAlloc && !reallocate)
        return true;
    try
    {
        const wchar_t *reason = nullptr;
        auto asset = PrepareSharedAsset(std::filesystem::path(directory) / fileName, reason);
        return asset && BindSharedAsset(asset);
    }
    catch (...)
    {
        return false;
    }
}

std::shared_ptr<BmdSharedAsset> BMD::PrepareSharedAsset(const std::filesystem::path &path,
                                                        const wchar_t *&reason, int requiredActions,
                                                        int requiredBones, int requiredMeshes,
                                                        bool rigidGeometry) noexcept
{
    reason = nullptr;
    try
    {
        const auto cached = applicationKeeper_.modelAssets_.find(path.wstring());
        if (cached != applicationKeeper_.modelAssets_.end())
        {
            if (cached->second.asset->actionCount < requiredActions)
            {
                reason = L"Model lacks required actions";
                return {};
            }
            if (cached->second.asset->boneCount < requiredBones ||
                cached->second.asset->meshCount < requiredMeshes)
            {
                reason = L"Model lacks required geometry or bones";
                return {};
            }
            if (requiredMeshes > 0 &&
                (!cached->second.asset->indices || cached->second.asset->indices->empty()))
            {
                reason = L"Model lacks drawable geometry";
                return {};
            }
            if (rigidGeometry && !cached->second.asset->rigidVertices &&
                !cached->second.asset->invariantLocalPose.empty())
            {
                BMD prepared(sessionKeeper_);
                if (!prepared.BindSharedAsset(cached->second.asset))
                {
                    reason = L"Model allocation failed";
                    return {};
                }
                prepared.BakeRigidGeometry(*cached->second.asset);
                const auto bytes = ModelGeometryDetail::BmdSharedAssetBytes(*cached->second.asset);
                applicationKeeper_.retainedModelAssetBytes_ += bytes - cached->second.bytes;
                cached->second.bytes = bytes;
            }
            cached->second.unusedSinceMilliseconds = 0;
            return cached->second.asset;
        }
        auto asset = BuildSharedAsset(path, reason, requiredActions, requiredBones, requiredMeshes,
                                      rigidGeometry);
        if (!asset)
            return {};
        const std::size_t bytes = ModelGeometryDetail::BmdSharedAssetBytes(*asset);
        applicationKeeper_.modelAssets_.emplace(
            path.wstring(), ApplicationKeeper::RetainedAsset<BmdSharedAsset>{asset, bytes, {}});
        applicationKeeper_.retainedModelAssetBytes_ += bytes;
        return asset;
    }
    catch (const std::bad_alloc &)
    {
        reason = L"Model allocation failed";
    }
    catch (...)
    {
        reason = L"Model preparation failed";
    }
    return {};
}

std::shared_ptr<BmdSharedAsset> BMD::BuildSharedAsset(const std::filesystem::path &path,
                                                      const wchar_t *&reason, int requiredActions,
                                                      int requiredBones, int requiredMeshes,
                                                      bool rigidGeometry)
{
    BmdSharedAsset::Error error;
    auto asset = BmdSharedAsset::Load(path, error);
    if (!asset)
    {
        reason = BmdSharedAsset::ErrorText(error);
        return {};
    }
    if (asset->actionCount < requiredActions)
    {
        reason = L"Model lacks required actions";
        return {};
    }
    if (asset->boneCount < requiredBones || asset->meshCount < requiredMeshes)
    {
        reason = L"Model lacks required geometry or bones";
        return {};
    }
    const bool hasTriangles = asset->meshCount > 0 &&
                              std::any_of(asset->meshes, asset->meshes + asset->meshCount,
                                          [](const Mesh_t &mesh) { return mesh.NumTriangles > 0; });
    if (requiredMeshes > 0 && !hasTriangles)
    {
        reason = L"Model lacks drawable geometry";
        return {};
    }
    BMD prepared(sessionKeeper_);
    if (!prepared.BindSharedAsset(asset))
    {
        reason = L"Model allocation failed";
        return {};
    }
    prepared.Init(false);
    if (hasTriangles && !prepared.BuildRenderTapeGeometry(true))
    {
        reason = L"Model geometry preparation failed";
        return {};
    }
    std::copy_n(prepared.Meshs, asset->meshCount, asset->meshes);
    asset->vertices = prepared.renderTapeGeometry_.vertices;
    asset->indices = prepared.renderTapeGeometry_.indices;
    if (rigidGeometry)
        prepared.BakeRigidGeometry(*asset);
    return asset;
}

bool BMD::Save2(wchar_t *DirName, wchar_t *ModelFileName)
{
    wchar_t ModelName[64];
    wcscpy(ModelName, DirName);
    wcscat(ModelName, ModelFileName);
    FILE *fp = _wfopen(ModelName, L"wb");
    if (fp == nullptr)
        return false;
    putc('B', fp);
    putc('M', fp);
    putc('D', fp);
    Version = 12;
    fwrite(&Version, 1, 1, fp);

    auto *pbyBuffer = new BYTE[1024 * 1024];
    BYTE *pbyCur = pbyBuffer;
    memcpy(pbyCur, Name, 32);
    pbyCur += 32;
    memcpy(pbyCur, &NumMeshs, 2);
    pbyCur += 2;
    memcpy(pbyCur, &NumBones, 2);
    pbyCur += 2;
    memcpy(pbyCur, &NumActions, 2);
    pbyCur += 2;

    int i;
    for (i = 0; i < NumMeshs; i++)
    {
        Mesh_t *m = &Meshs[i];
        memcpy(pbyCur, &m->NumVertices, 2);
        pbyCur += 2;
        memcpy(pbyCur, &m->NumNormals, 2);
        pbyCur += 2;
        memcpy(pbyCur, &m->NumTexCoords, 2);
        pbyCur += 2;
        memcpy(pbyCur, &m->NumTriangles, 2);
        pbyCur += 2;
        memcpy(pbyCur, &m->Texture, 2);
        pbyCur += 2;
        memcpy(pbyCur, m->Vertices, m->NumVertices * sizeof(Vertex_t));
        pbyCur += m->NumVertices * sizeof(Vertex_t);
        memcpy(pbyCur, m->Normals, m->NumNormals * sizeof(Normal_t));
        pbyCur += m->NumNormals * sizeof(Normal_t);
        memcpy(pbyCur, m->TexCoords, m->NumTexCoords * sizeof(TexCoord_t));
        pbyCur += m->NumTexCoords * sizeof(TexCoord_t);
        for (int j = 0; j < m->NumTriangles; j++)
        {
            memcpy(pbyCur, &m->Triangles[j], sizeof(Triangle_t2));
            pbyCur += sizeof(Triangle_t2);
        }
        memcpy(pbyCur, Textures[i].FileName, 32);
        pbyCur += 32;
    }
    for (i = 0; i < NumActions; i++)
    {
        Action_t *a = &Actions[i];
        memcpy(pbyCur, &a->NumAnimationKeys, 2);
        pbyCur += 2;
        memcpy(pbyCur, &a->LockPositions, 1);
        pbyCur += 1;
        if (a->LockPositions)
        {
            memcpy(pbyCur, a->Positions, a->NumAnimationKeys * sizeof(vec3_t));
            pbyCur += a->NumAnimationKeys * sizeof(vec3_t);
        }
    }
    for (i = 0; i < NumBones; i++)
    {
        Bone_t *b = &Bones[i];
        memcpy(pbyCur, &b->Dummy, 1);
        pbyCur += 1;
        if (!b->Dummy)
        {
            memcpy(pbyCur, b->Name, 32);
            pbyCur += 32;
            memcpy(pbyCur, &b->Parent, 2);
            pbyCur += 2;
            for (int j = 0; j < NumActions; j++)
            {
                BoneMatrix_t *bm = &b->BoneMatrixes[j];
                memcpy(pbyCur, bm->Position, Actions[j].NumAnimationKeys * sizeof(vec3_t));
                pbyCur += Actions[j].NumAnimationKeys * sizeof(vec3_t);
                memcpy(pbyCur, bm->Rotation, Actions[j].NumAnimationKeys * sizeof(vec3_t));
                pbyCur += Actions[j].NumAnimationKeys * sizeof(vec3_t);
            }
        }
    }
    auto lSize = (long)(pbyCur - pbyBuffer);
    // The on-disk size field is 32-bit; writing a `long` would emit 8 bytes on
    // LP64 (Linux x64) and corrupt the file.
    auto lEncSize = (std::int32_t)MapFileEncrypt(nullptr, pbyBuffer, lSize);
    auto *pbyEnc = new BYTE[lEncSize];
    MapFileEncrypt(pbyEnc, pbyBuffer, lSize);
    fwrite(&lEncSize, sizeof(std::int32_t), 1, fp);
    fwrite(pbyEnc, lEncSize, 1, fp);
    fclose(fp);
    delete[] pbyBuffer;
    delete[] pbyEnc;
    return true;
}

void BMD::Init(bool Dummy)
{
    if (Dummy)
    {
        int i;
        for (i = 0; i < NumBones; i++)
        {
            Bone_t *b = &Bones[i];
            if (b->Name[0] == 'D' && b->Name[1] == 'u')
                b->Dummy = true;
            else
                b->Dummy = false;
        }
    }
    BoneHead = -1;
    StreamMesh = -1;
    CreateBoundingBox();
}

void World::ReleaseSceneResources()
{
    sessionKeeper_.MapManagerObject().DeleteObjects();
    DeleteNpcs();
    DeleteMonsters();
    DeleteWaterTerrain();
    sessionKeeper_.Renderer()->ReleaseWorldRenderResources();
    WorldLoadingDetail::ReleaseWorldBehaviorModels(sessionKeeper_, Binding().definition);
    if (Binding().definition && Binding().definition->id.RawValue() == WD_74NEW_CHARACTER_SCENE)
        ReleaseCharacterSceneData();
    if (Binding().definition && Binding().definition->scene == MapDefinition::Scene::Login)
        if (auto &credits = sessionKeeper_.Ui()->LegacyUiManager().m_CreditWin)
            credits->ReleaseIllustrations();
    ReleaseEffectTextures();
    auto &terrain = sessionKeeper_.TerrainStorage();
    terrain.BindMapping({});
    terrain.BaseTerrain.reset();
    terrain.admittedTiles.reset();
    simulationTimeMilliseconds_ = 0.0;
    terrain.contentRevision = 0;
    terrain.revisedBlocks.reset();
}

int ModelResourceRequirements::Actions(int slot) noexcept
{
    switch (slot)
    {
    case MODEL_PLAYER:
        return MAX_PLAYER_ACTION;
    case MODEL_LUCKYITEM_NPC:
        return 2;
    case MODEL_WARCRAFT:
    case MODEL_BALL:
    case MODEL_GROUND_STONE:
    case MODEL_GROUND_STONE2:
    case MODEL_BALGAS_SKILL:
    case MODEL_CHANGE_UP_EFF:
    case MODEL_KNIGHT_PLANCRACK_A:
    case MODEL_KNIGHT_PLANCRACK_B:
        return 1;
    case MODEL_ELBELAND_RHEA:
    case MODEL_TERSIA:
    case MODEL_KARUTAN_NPC_VOLVO:
        return 2;
    case MODEL_XMAS2008_SNOWMAN:
    case MODAL_GENS_NPC_DUPRIAN:
    case MODAL_GENS_NPC_BARNERT:
    case MODEL_UNITEDMARKETPLACE_RAUL:
    case MODEL_UNITEDMARKETPLACE_JULIA:
    case MODEL_UNITEDMARKETPLACE_CHRISTIN:
    case MODEL_KARUTAN_NPC_REINA:
        return MONSTER01_WALK + 1;
    case MODEL_DOPPELGANGER_NPC_BOX:
    case MODEL_DOPPELGANGER_NPC_GOLDENBOX:
        return MONSTER01_DIE + 1;
    default:
        break;
    }
    if (slot < MODEL_MONSTER01 || slot >= MODEL_MONSTER01 + MONSTER_MODEL_COUNT)
        return 0;
    switch (slot - MODEL_MONSTER01)
    {
    case MONSTER_MODEL_BALI:
        return MONSTER01_RUN + 1;
    case MONSTER_MODEL_BALGASS:
    case MONSTER_MODEL_DARK_SKULL_SOLDIER_5:
    case MONSTER_MODEL_SELUPAN:
    case MONSTER_MODEL_GAYION:
    case MONSTER_MODEL_FRED:
        return MONSTER01_ATTACK4 + 1;
    case MONSTER_MODEL_DARK_ELF_1:
    case MONSTER_MODEL_JERRY:
    case MONSTER_MODEL_RAYMOND:
    case MONSTER_MODEL_LUCAS:
    case MONSTER_MODEL_HAMMERIZE:
    case MONSTER_MODEL_DUAL_BERSERKER:
    case MONSTER_MODEL_DEVIL_LORD:
    case MONSTER_MODEL_BANSHEE:
    case MONSTER_MODEL_MEDUSA:
        return MONSTER01_ATTACK3 + 1;
    case MONSTER_MODEL_MAYA_HAND_LEFT:
    case MONSTER_MODEL_MAYA_HAND_RIGHT:
    case MONSTER_MODEL_COMBAT_INSTRUCTOR:
    case MONSTER_MODEL_ATICLES_HEAD:
    case MONSTER_MODEL_DEFENDER:
    case MONSTER_MODEL_DOPPELGANGER:
    case MONSTER_MODEL_DRAGON:
        return MONSTER01_APEAR + 1;
    default:
        return MONSTER01_DIE + 1;
    }
}

int ModelResourceRequirements::Bones(int slot) noexcept
{
    switch (slot)
    {
    case MODEL_CONDRA_CONE_L:
        return 1;
    case MODEL_BATTLE_SCEPTER:
    case MODEL_DARK_REIGN_BLADE:
    case MODEL_ELEMENTAL_MACE:
    case MODEL_GRAND_SOUL_SHIELD:
    case MODEL_GREAT_LORD_SCEPTER:
    case MODEL_RUNE_BLADE:
    case MODEL_SALAMANDER_SHIELD:
        return 2;
    case MODEL_CANON_TOWER:
    case MODEL_DAYBREAK:
    case MODEL_DEVILS_KEY:
    case MODEL_FLAIL:
    case MODEL_GRAND_VIPER_STAFF:
    case MODEL_GREAT_SCEPTER:
    case MODEL_KNIGHT_BLADE:
    case MODEL_LORD_SCEPTER:
        return 3;
    case MODEL_ARROW_AUTOLOAD:
    case MODEL_CURSEDTEMPLE_ILLUSION__BASKET:
    case MODEL_RAVEN_STICK:
    case MODEL_SHINING_SCEPTER:
    case MODEL_SKULL:
    case MODEL_STAFF + 32:
        return 4;
    case MODEL_ABSOLUTE_SCEPTER:
    case MODEL_NPC_CASTEL_GATE:
        return 5;
    case MODEL_BEUROBA:
    case MODEL_CURSEDTEMPLE_ALLIED_BASKET:
    case MODEL_GREAT_REIGN_CROSSBOW:
    case MODEL_IMPERIAL_STAFF:
    case MODEL_MASTER_SCEPTER:
        return 6;
    case MODEL_NPC_SEVINA:
    case MODEL_SILVER_BOW:
    case MODEL_SLAUGHTERER:
    case MODEL_STRYKER_SCEPTER:
    case MODEL_SWORD_DANCER:
    case MODEL_TIGER_BOW:
        return 7;
    case MODEL_SWORD_BREAKER:
        return 8;
    case MODEL_BONE_SCORPION:
    case MODEL_CHROMATIC_STAFF:
    case MODEL_DIVINE_SWORD_OF_ARCHANGEL:
    case MODEL_DRAGON_SPEAR:
    case MODEL_EXPLOSION_BLADE:
    case MODEL_STAFF_OF_KUNDUN:
    case MODEL_WARCRAFT:
        return 9;
    case MODEL_ARROW_VIPER_BOW:
    case MODEL_BAHAMUT:
    case MODEL_DEADLY_STAFF:
    case MODEL_GUARDIAN_SHILED:
    case MODEL_INFINITY_ARROW:
    case MODEL_MUTANT:
    case MODEL_RED_SKELETON_KNIGHT_1:
    case MODEL_ZOMBIE_FIGHTER:
        return 10;
    case MODEL_CRIMSONGLORY:
    case MODEL_DEVILS_INVITATION:
        return 11;
    case MODEL_IMPERIAL_SWORD:
        return 12;
    case MODEL_BLOODY_WOLF:
        return 13;
    case MODEL_CROSS_SHIELD:
    case MODEL_FLAMBERGE:
    case MODEL_PLATINA_STAFF:
        return 14;
    case MODEL_CURSEDTEMPLE_STATUE:
        return 15;
    case MODEL_SYLPH_WIND_BOW:
    case MODEL_VENOMOUS_CHAIN_SCORPION:
        return 16;
    case MODEL_BALROG:
    case MODEL_SMITH:
        return 18;
    case MODEL_CELESTIAL_BOW:
    case MODEL_CHAOS_NATURE_BOW:
        return 19;
    case MODEL_COOLUTIN:
    case MODEL_DARK_COOLUTIN:
    case MODEL_HOUND:
        return 20;
    case MODEL_BLOODY_GOLEM:
    case MODEL_GOLDEN_STONE_GOLEM:
    case MODEL_NPC_DEVILSQUARE:
    case MODEL_SWORD_35_WING:
        return 21;
    case MODEL_KALIMA_SHOP:
        return 22;
    case MODEL_CURSEDTEMPLE_ENTER_NPC:
        return 23;
    case MODEL_GOLDEN_WHEEL:
        return 24;
    case MODEL_BEETLE_MONSTER:
    case MODEL_FROST_MACE:
    case MODEL_SCOUT:
        return 25;
    case MODEL_HUNTER:
    case MODEL_SOLAM:
        return 26;
    case MODEL_BLOODY_DEATH_RIDER:
    case MODEL_DARK_KNIGHT:
    case MODEL_DEATH_RIDER:
    case MODEL_ICE_QUEEN:
        return 27;
    case MODEL_SORAM:
        return 28;
    case MODEL_DEATH_SPIRIT:
    case MODEL_TITAN:
        return 29;
    case MODEL_AIR_LYN_BOW:
    case MODEL_HELL_SPIDER:
    case MODEL_OCELOT:
    case MODEL_VALAM:
        return 30;
    case MODEL_CURSED_LICH:
    case MODEL_DEATH_KNIGHT:
    case MODEL_LIZARD_WARRIOR:
    case MODEL_PHANTOM_KNIGHT:
    case MODEL_RED_SKELETON_KNIGHT:
    case MODEL_VALKYRIE:
    case MODEL_WINGS_OF_DARKNESS:
        return 31;
    case MODEL_GOBLIN:
        return 32;
    case MODEL_MIX_NPC:
        return 33;
    case MODEL_BALGASS:
    case MODEL_BLOOD_SOLDIER:
    case MODEL_CHAOS_CASTLE_ELF:
    case MODEL_CHAOS_CASTLE_WIZARD:
    case MODEL_DARK_ELF_1:
    case MODEL_DARK_SKULL_SOLDIER:
    case MODEL_MAGIC_SKELETON:
    case MODEL_SOLDIER:
        return 34;
    case MODEL_ALBATROSS_BOW:
    case MODEL_AXE_HERO:
    case MODEL_BERSERK:
    case MODEL_BERSERKER_WARRIOR:
    case MODEL_CONDRA:
    case MODEL_GIGANTIS:
    case MODEL_KENTAUROS_WARRIOR:
    case MODEL_MAD_BUTCHER:
    case MODEL_QUARTER_MASTER:
    case MODEL_QUEEN_RAINER:
    case MODEL_XMAS2008_SNOWMAN:
    case MODEL_XMAS2008_SNOWMAN_NPC:
        return 35;
    case MODEL_ELBELAND_RHEA:
        return 36;
    case MODEL_AEGIS:
    case MODEL_DEVIAS_TRADER:
        return 38;
    case MODEL_ERIC:
    case MODEL_WING_OF_ILLUSION:
        return 39;
    case MODEL_AGON:
    case MODEL_GORGON:
    case MODEL_HELL_MAINE:
    case MODEL_HOMMERD:
    case MODEL_NAPIN:
    case MODEL_ORC_ARCHER:
    case MODEL_SEED_MASTER:
    case MODEL_SHADOW_KNIGHT:
    case MODEL_SHADOW_LOOK:
    case MODEL_SHADOW_MASTER:
    case MODEL_SHADOW_PAWN:
        return 40;
    case MODEL_GLADIATOR:
    case MODEL_ILLUSION_SORCERER_SPIRIT_POISON:
        return 41;
    case MODEL_CYCLOPS:
    case MODEL_GIANT:
    case MODEL_ILLUSION_SORCERER_SPIRIT_ICE:
    case MODEL_LICH:
        return 42;
    case MODEL_BULL_FIGHTER:
    case MODEL_CHAOS_CASTLE_KNIGHT:
    case MODEL_DEATH_COW:
        return 43;
    case MODEL_BURNING_LAVA_GIANT:
    case MODEL_LAVA_GIANT:
    case MODEL_SAPIDUO:
    case MODEL_SAPIUNUS:
    case MODEL_SAPI_QUEEN:
    case MODEL_STINGER_BOW:
    case MODEL_TANTALLOS:
    case MODEL_WING_OF_DIMENSION:
    case MODEL_WOLF_STATUS:
        return 44;
    case MODEL_FENRIR_BLACK:
    case MODEL_FENRIR_BLUE:
    case MODEL_FENRIR_GOLD:
    case MODEL_FENRIR_RED:
    case MODEL_MAYA_HAND_LEFT:
    case MODEL_NPC_CAPATULT_ATT:
    case MODEL_SATYROS:
        return 45;
    case MODEL_BLOODY_ORC:
    case MODEL_CRUST:
    case MODEL_DARK_GIANT:
    case MODEL_DARK_MAMMOTH:
    case MODEL_DEATH_TREE:
    case MODEL_FOREST_ORC:
    case MODEL_GIANT_MAMMOTH:
    case MODEL_ICE_GIANT:
        return 46;
    case MODEL_ATICLES_HEAD:
    case MODEL_PLAYER:
        return 48;
    case MODEL_FIRE_GOLEM:
        return 49;
    case MODEL_BALI:
    case MODEL_CURSEDTEMPLE_ILLUSION_NPC:
    case MODEL_HAMMERIZE:
    case MODEL_TERRIBLE_BUTCHER:
        return 50;
    case MODEL_BANSHEE:
    case MODEL_DARK_IRON_KNIGHT:
    case MODEL_IRON_KNIGHT:
    case MODEL_SHRIKER:
        return 52;
    case MODEL_GENOCIDER:
    case MODEL_GIGANTIS_WARRIOR:
    case MODEL_LIZARD:
        return 53;
    case MODEL_COMBAT_INSTRUCTOR:
        return 54;
    case MODEL_BLADE_HUNTER:
    case MODEL_FORSAKER:
    case MODEL_LUCAS:
    case MODEL_POISON_GOLEM:
        return 56;
    case MODEL_DEATH_CENTURION:
        return 57;
    case MODEL_GAYION:
    case MODEL_ORCUS:
    case MODEL_SPIDER_EGGS_1:
        return 58;
    case MODEL_WING_OF_ETERNAL:
    case MODEL_XMAS2008_SANTA_NPC:
        return 59;
    case MODEL_MAYA_HAND_RIGHT:
    case MODEL_RAYMOND:
    case MODEL_SUMMONER_SUMMON_NEIL:
    case MODEL_TWIN_TAIL:
        return 60;
    case MODEL_HIDEOUS_RABBIT:
        return 62;
    case MODEL_QUEEN_BEE:
        return 63;
    case MODEL_HYDRA:
    case MODEL_WING_OF_RUIN:
        return 64;
    case MODEL_DEATH_ANGEL_3:
    case MODEL_JERRY:
    case MODEL_NECRON:
        return 68;
    case MODEL_BLOOD_ASSASSIN:
    case MODEL_CRUEL_BLOOD_ASSASSIN:
    case MODEL_SEED_INVESTIGATOR:
        return 71;
    case MODEL_DARK_SKULL_SOLDIER_5:
    case MODEL_DREADFEAR:
        return 72;
    case MODEL_FRED:
    case MODEL_SELUPAN:
        return 75;
    case MODEL_DEATH_ANGEL:
    case MODEL_ILLUSION_SORCERER_SPIRIT_LIGHTNING:
        return 79;
    case MODEL_GHOST_NAPIN:
    case MODEL_ICE_WALKER:
    case MODEL_UNITEDMARKETPLACE_RAUL:
        return 80;
    case MODEL_ELBELAND_MARCE:
        return 82;
    case MODEL_WEREWOLF_HERO:
        return 83;
    case MODEL_SAPITRES:
        return 85;
    case MODEL_BEAM_KNIGHT:
        return 87;
    case MODEL_NACONDRA:
    case MODEL_PERSONA:
        return 90;
    case MODEL_HEAD_MOUNTER:
    case MODEL_TOTEM_GOLEM:
        return 99;
    case MODEL_DARK_GHOST:
    case MODEL_ILLUSION_OF_KUNDUN:
        return 101;
    case MODEL_DEVIL_LORD:
        return 106;
    case MODEL_WING_OF_STORM:
        return 107;
    case MODEL_BLOODY_WITCH_QUEEN:
    case MODEL_WITCH_QUEEN:
        return 111;
    case MODEL_SPIDER_EGGS_2:
        return 116;
    case MODEL_BLAZE_NAPIN:
    case MODEL_ICE_NAPIN:
        return 123;
    case MODEL_MEDUSA:
        return 125;
    case MODEL_DUAL_BERSERKER:
        return 127;
    case MODEL_UNITEDMARKETPLACE_JULIA:
        return 128;
    case MODEL_CRYPOS:
        return 157;
    case MODEL_SPIDER_EGGS_3:
        return 174;
    default:
        return slot >= MODEL_MONSTER01 && slot < MODEL_MONSTER01 + MONSTER_MODEL_COUNT ? 1 : 0;
    }
}

int ModelResourceRequirements::Meshes(int slot) noexcept
{
    if (slot == MODEL_PANDA)
        return 4; // Costume meshes use the player's actions.
    if (slot == MODEL_DRAKAN)
        return 5; // Character creation configures meshes 0..4.
    if (slot == MODEL_DARK_SOUL_PANTS)
        return 3; // Cloth uses mesh 2.
    return 0;
}

int ModelResourceRequirements::HeadBone(int slot) noexcept
{
    switch (slot - MODEL_MONSTER01)
    {
    case MONSTER_MODEL_GOLDEN_WHEEL:
        return 3;
    case MONSTER_MODEL_BEETLE_MONSTER:
    case MONSTER_MODEL_HOUND:
    case MONSTER_MODEL_SHADOW:
    case MONSTER_MODEL_STONE_GOLEM:
        return 5;
    case MONSTER_MODEL_BALI:
    case MONSTER_MODEL_BALROG:
    case MONSTER_MODEL_BEAM_KNIGHT:
    case MONSTER_MODEL_DEVIL:
    case MONSTER_MODEL_FOREST_MONSTER:
    case MONSTER_MODEL_GOBLIN:
    case MONSTER_MODEL_HUNTER:
    case MONSTER_MODEL_MUTANT:
    case MONSTER_MODEL_SOLDIER:
        return 6;
    case MONSTER_MODEL_BLOODY_WOLF:
    case MONSTER_MODEL_BUDGE_DRAGON:
    case MONSTER_MODEL_ORC_ARCHER:
        return 7;
    case MONSTER_MODEL_AGON:
    case MONSTER_MODEL_DARK_KNIGHT:
    case MONSTER_MODEL_HELL_SPIDER:
    case MONSTER_MODEL_ICE_QUEEN:
        return 16;
    case MONSTER_MODEL_DEATH_KNIGHT:
    case MONSTER_MODEL_ICE_MONSTER:
    case MONSTER_MODEL_LIZARD:
    case MONSTER_MODEL_VALKYRIE:
        return 19;
    case MONSTER_MODEL_ASSASSIN:
    case MONSTER_MODEL_BULL_FIGHTER:
    case MONSTER_MODEL_CHAOSCASTLE_ELF:
    case MONSTER_MODEL_CHAOSCASTLE_KNIGHT:
    case MONSTER_MODEL_CHAOSCASTLE_WIZARD:
    case MONSTER_MODEL_CURSED_KING:
    case MONSTER_MODEL_CYCLOPS:
    case MONSTER_MODEL_DEATH_COW:
    case MONSTER_MODEL_ELITE_YETI:
    case MONSTER_MODEL_GHOST:
    case MONSTER_MODEL_GIANT:
    case MONSTER_MODEL_GORGON:
    case MONSTER_MODEL_LICH:
    case MONSTER_MODEL_ORC:
    case MONSTER_MODEL_TANTALLOS:
    case MONSTER_MODEL_TITAN:
    case MONSTER_MODEL_VEPAR:
    case MONSTER_MODEL_YETI:
        return 20;
    default:
        return 0;
    }
}

int ModelResourceRequirements::RequiredBones(int slot, bool geometry) noexcept
{
    const int head = slot >= MODEL_MONSTER01 && slot < MODEL_MONSTER01 + MONSTER_MODEL_COUNT
                         ? HeadBone(slot) + 1
                         : 0;
    return std::max({Bones(slot), head, geometry ? 1 : 0});
}

void SessionRenderUnit::LoadChangeRingItemModels()
{
    gLoadData.AccessModel(MODEL_TRANSFORMATION_RING, L"Data\\Item\\", L"Ring", 1);
    gLoadData.AccessModel(MODEL_ELITE_TRANSFER_SKELETON_RING, L"Data\\Item\\", L"Ring", 1);
    gLoadData.AccessModel(MODEL_JACK_OLANTERN_TRANSFORMATION_RING, L"Data\\Item\\", L"Ring", 1);
    gLoadData.AccessModel(MODEL_CHRISTMAS_TRANSFORMATION_RING, L"Data\\Item\\", L"Ring", 1);
    gLoadData.AccessModel(MODEL_GAME_MASTER_TRANSFORMATION_RING, L"Data\\Item\\", L"Ring", 1);
    gLoadData.AccessModel(MODEL_SNOWMAN_TRANSFORMATION_RING, L"Data\\Item\\xmas\\", L"xmasring");
    gLoadData.AccessModel(MODEL_PANDA_TRANSFORMATION_RING, L"Data\\Item\\", L"PandaPetRing");
    gLoadData.AccessModel(MODEL_SKELETON_TRANSFORMATION_RING, L"Data\\Item\\", L"SkeletonRing");
}

void SessionRenderUnit::LoadChangeRingItemTextures()
{
    gLoadData.OpenTexture(MODEL_ELITE_TRANSFER_SKELETON_RING, L"Item\\");
    gLoadData.OpenTexture(MODEL_CHRISTMAS_TRANSFORMATION_RING, L"Item\\");
    gLoadData.OpenTexture(MODEL_JACK_OLANTERN_TRANSFORMATION_RING, L"Item\\");
    gLoadData.OpenTexture(MODEL_GAME_MASTER_TRANSFORMATION_RING, L"Item\\");
    gLoadData.OpenTexture(MODEL_SNOWMAN_TRANSFORMATION_RING, L"Item\\xmas\\");
    gLoadData.OpenTexture(MODEL_PANDA_TRANSFORMATION_RING, L"Item\\");
    gLoadData.OpenTexture(MODEL_SKELETON_TRANSFORMATION_RING, L"Item\\");
}
