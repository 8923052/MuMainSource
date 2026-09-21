#pragma once
#include "support/CoreMath.h"
#include "data/CharacterData.h"
#include "domain/MapPresentation.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "data/GameData.h"

#include <compare>
#include <cstdint>
#include <optional>
#include <array>
#include <filesystem>
#include <span>
#include <vector>
#include <algorithm>
#include <bitset>
#include <utility>
#include <memory>

#pragma pack(push)
#pragma pack()
#define MAX_ITEMS 1000

#pragma pack(pop)

#define MAX_MONSTERSKILL_NUM 10
#define TERRAIN_SCALE 100.f
#define TERRAIN_SIZE_MASK 255
#define BLOODCASTLE_NUM 8
#define HELLAS_NUM 7
#define CHAOS_NUM 6
#define TERRAIN_MAP_NORMAL 0
#define TERRAIN_MAP_ALPHA 1
#define TERRAIN_MAP_GRASS 2
#define TERRAIN_MAP_TRAP 3
#define MAX_GATES 512
#define MAX_MINIMAP_NAME 100
#define MAX_MONSTER_NAME 40

#define TERRAIN_SIZE 256
#define TW_SAFEZONE (0x0001)
#define TW_CHARACTER (0x0002)
#define TW_NOMOVE (0x0004)
#define TW_NOGROUND (0x0008)
#define TW_WATER (0x0010)
#define TW_ACTION (0x0020)
#define TW_HEIGHT (0x0040)
#define TW_CAMERA_UP (0x0080)
#define TW_NOATTACKZONE (0x0100)
#define TW_ATT1 (0x0200)
#define TW_ATT2 (0x0400)
#define TW_ATT3 (0x0800)
#define TW_ATT4 (0x1000)
#define TW_ATT5 (0x2000)
#define TW_ATT6 (0x4000)
#define TW_ATT7 (0x8000)
#define MAX_QUEST_CONDITION 16
#define MAX_QUEST_REQUEST 16
#define MAX_LENGTH_DIALOG (300)
#define MAX_ANSWER_FOR_DIALOG (10)
#define MAX_LENGTH_ANSWER (64)

class WorldCapabilityStatus;
class SessionKeeper;

enum ENUM_WORLD
{
    WD_0LORENCIA = 0,
    WD_1DUNGEON,
    WD_2DEVIAS,
    WD_3NORIA,
    WD_4LOSTTOWER,
    WD_5UNKNOWN,
    WD_6STADIUM,
    WD_7ATLANSE,
    WD_8TARKAN,
    WD_9DEVILSQUARE,
    WD_10HEAVEN,
    WD_11BLOODCASTLE1,
    WD_11BLOODCASTLE_END = WD_11BLOODCASTLE1 + 6,
    WD_18CHAOS_CASTLE,
    WD_18CHAOS_CASTLE_END = WD_18CHAOS_CASTLE + (CHAOS_NUM - 1),
    WD_24HELLAS,
    WD_24HELLAS_END = WD_24HELLAS + (HELLAS_NUM - 2),
    WD_30BATTLECASTLE,
    WD_31HUNTING_GROUND = 31,
    WD_33AIDA = 33,
    WD_34CRYWOLF_1ST = 34,
    WD_35CRYWOLF_2ND = 35,
    WD_24HELLAS_7 = 36,
    WD_37KANTURU_1ST = 37,
    WD_38KANTURU_2ND = 38,
    WD_39KANTURU_3RD = 39,
    WD_40AREA_FOR_GM = 40,
    WD_41CHANGEUP3RD_1ST = 41,
    WD_42CHANGEUP3RD_2ND = 42,
    WD_45CURSEDTEMPLE_LV1 = 45,
    WD_45CURSEDTEMPLE_LV2,
    WD_45CURSEDTEMPLE_LV3,
    WD_45CURSEDTEMPLE_LV4,
    WD_45CURSEDTEMPLE_LV5,
    WD_45CURSEDTEMPLE_LV6,
    WD_51HOME_6TH_CHAR = 51,
    WD_52BLOODCASTLE_MASTER_LEVEL = 52,
    WD_53CAOSCASTLE_MASTER_LEVEL = 53,
    WD_54CHARACTERSCENE = 54,
    WD_55LOGINSCENE = 55,
    WD_56MAP_SWAMP_OF_QUIET = 56,
    WD_57ICECITY = 57,
    WD_58ICECITY_BOSS = 58,
    WD_62SANTA_TOWN = 62,
    WD_63PK_FIELD = 63,
    WD_64DUELARENA = 64,
    WD_65DOPPLEGANGER1 = 65,
    WD_66DOPPLEGANGER2 = 66,
    WD_67DOPPLEGANGER3 = 67,
    WD_68DOPPLEGANGER4 = 68,
    WD_69EMPIREGUARDIAN1 = 69,
    WD_70EMPIREGUARDIAN2 = 70,
    WD_71EMPIREGUARDIAN3 = 71,
    WD_72EMPIREGUARDIAN4 = 72,
    WD_73NEW_LOGIN_SCENE = 73,
    WD_74NEW_CHARACTER_SCENE = 74,
    WD_77NEW_LOGIN_SCENE = 77,
    WD_78NEW_CHARACTER_SCENE = 78,
    WD_79UNITEDMARKETPLACE = 79,
    WD_80KARUTAN1 = 80,
    WD_81KARUTAN2 = 81,
    NUM_WD
};

struct MapCharacterPolicy final
{
    bool attackSounds = true;
    int monsterWall = 0x02; // Compiled terrain wall mask for monster path finding.
    bool swimming = false;
    bool ocean = false;
    bool skyTerrain = false;
    bool flyingMounts = false;
    bool groundShadows = true;
    bool snowFootsteps = false;
    bool mounts = true;
    bool nonPlayerMountOwners = false;
    bool festiveNpcs = false;
    bool festiveUniform = false;
    bool cloneAppearances = false;
    float festiveFacing = 0.f;
    float shadowAlpha = 1.f;
};

namespace SEASON3B
{
class CMoveCommandData
{
  public:
#pragma pack(push, 1)
    typedef struct tagMOVEREQ
    {
        int index;
        wchar_t szMainMapName[32];
        wchar_t szSubMapName[32];
        int iReqLevel;
        int m_iReqMaxLevel;
        int iReqZen;
        int iGateNum;
    } MOVEREQINFO;
#pragma pack(pop)

    typedef struct tagMOVEINFODATA
    {
        MOVEREQINFO _ReqInfo;
        bool _bCanMove;
        bool _bStrife;
        bool _bSelected;

        bool operator==(const int &iIndex) const
        {
            int iTempIndex = iIndex;
            return _ReqInfo.index == iTempIndex;
        };
    } MOVEINFODATA;

  private:
    std::list<MOVEINFODATA *> m_listMoveInfoData;

  public:
    CMoveCommandData();
    virtual ~CMoveCommandData();

  public:
    bool OpenMoveReqScript(const std::wstring &filename);

    int GetNumMoveMap();
    const MOVEINFODATA *GetMoveCommandDataByIndex(int iIndex);
    const std::list<MOVEINFODATA *> &GetMoveCommandDatalist();

  protected:
    bool Create(const std::wstring &filename);
    void Release();
};
} // namespace SEASON3B

struct MapDefinition final
{
    enum class Family
    {
        Ordinary,
        DevilSquare,
        BloodCastle,
        ChaosCastle,
        Hellas,
        BattleCastle,
        Crywolf,
        Kanturu,
        CursedTemple,
        EmpireGuardian,
        Doppelganger,
        LocalScene,
        Karutan,
    };
    enum class Scene
    {
        Gameplay,
        Login,
        Character
    };
    enum class Variant
    {
        Base,
        Occupied,
        War,
        Success
    };

    MapId id;
    int assetSet;
    Family family;
    Scene scene;
    int nameTextId;
    bool extendedHeight = false;
    MapPresentationPolicy presentation;
    MapCharacterPolicy character;
    MapTerrainPolicy terrain;

    static const MapDefinition *Find(int rawMap) noexcept;
    int BehaviorMap() const noexcept
    {
        constexpr int DevilSquareBehavior = 9;
        return family == Family::DevilSquare ? DevilSquareBehavior : id.RawValue();
    }
    int AttributeFile(Variant variant) const noexcept;
    const wchar_t *LightFile(Variant variant) const noexcept;
};

class TerrainCell final
{
  public:
    using Coordinate = std::uint16_t;
    using Index = std::uint32_t;

    static constexpr Coordinate Width = 256;
    static constexpr Coordinate Height = 256;
    static constexpr float WorldScale = 100.0f;

    static constexpr std::optional<TerrainCell> TryCreate(Coordinate x, Coordinate y) noexcept
    {
        if (x >= Width || y >= Height)
        {
            return std::nullopt;
        }
        return TerrainCell(x, y);
    }

    constexpr Coordinate X() const noexcept
    {
        return x_;
    }

    constexpr Coordinate Y() const noexcept
    {
        return y_;
    }

    constexpr Index LinearIndex() const noexcept
    {
        return static_cast<Index>(y_) * Width + x_;
    }

    constexpr auto operator<=>(const TerrainCell &) const noexcept = default;

  private:
    constexpr TerrainCell(Coordinate x, Coordinate y) noexcept : x_(x), y_(y)
    {
    }

    Coordinate x_;
    Coordinate y_;
};

struct TerrainContact final
{
    float frames;
    float height;
    float surfaceVelocity; // Vertical surface speed along the requested XY motion.
};

// Owned, decoded legacy map payload. No live session state is changed by Load.
class WorldFileData final
{
  public:
    enum class Kind
    {
        Mapping,
        Attributes,
        Placements
    };
    enum class Error
    {
        None,
        Open,
        Read,
        Size,
        Header,
        Map,
        Attributes,
        Values,
        Allocation
    };
    struct Placement final
    {
        std::int16_t type;
        std::array<float, 3> position;
        std::array<float, 3> angle;
        float scale;
    };

    static std::optional<WorldFileData> Load(const std::filesystem::path &path, Kind kind,
                                             int assetSet, int behaviorMap, Error &error) noexcept;
    static const wchar_t *ErrorText(Error error) noexcept;

    int AssetSet() const noexcept
    {
        return assetSet_;
    }
    std::span<const std::uint8_t> Layer1() const noexcept;
    std::span<const std::uint8_t> Layer2() const noexcept;
    std::span<const std::uint8_t> Alpha() const noexcept;
    std::span<const std::uint16_t> Walls() const noexcept
    {
        return walls_;
    }
    std::size_t SkippedPlacements() const noexcept
    {
        return skippedPlacements_;
    }
    const std::vector<Placement> &Placements() const noexcept
    {
        return placements_;
    }
    std::size_t StorageBytes() const noexcept
    {
        return bytes_.capacity() + walls_.capacity() * sizeof(std::uint16_t) +
               placements_.capacity() * sizeof(Placement);
    }

  private:
    bool Decode(Kind kind, int assetSet, int behaviorMap, Error &error);
    bool DecodeAttributes(int behaviorMap, Error &error);
    bool DecodePlacements(Error &error);
    int assetSet_ = 0;
    std::vector<std::uint8_t> bytes_;
    std::vector<std::uint16_t> walls_;
    std::vector<Placement> placements_;
    std::size_t skippedPlacements_ = 0;
};

struct TerrainMappingData final
{
    static constexpr std::size_t CellCount = TerrainCell::Width * TerrainCell::Height;
    std::array<unsigned char, CellCount> layer1{};
    std::array<unsigned char, CellCount> layer2{};
    std::array<float, CellCount> alpha{};

    TerrainMappingData()
    {
        layer2.fill(255);
    }
    explicit TerrainMappingData(const WorldFileData &source)
    {
        Assign(source);
    }
    void Assign(const WorldFileData &source)
    {
        std::copy(source.Layer1().begin(), source.Layer1().end(), layer1.begin());
        std::copy(source.Layer2().begin(), source.Layer2().end(), layer2.begin());
        std::transform(source.Alpha().begin(), source.Alpha().end(), alpha.begin(),
                       [](unsigned char value) { return static_cast<float>(value) / 255.0F; });
    }
};

class TerrainPickSurface final
{
  public:
    using Heights = std::array<float, 4>;
    using WallBits = std::array<std::uint16_t, 4>;

    constexpr TerrainPickSurface(TerrainCell cell, Heights heights, WallBits wallBits,
                                 float specialHeight) noexcept
        : cell_(cell), heights_(heights), wallBits_(wallBits), specialHeight_(specialHeight)
    {
    }

    constexpr TerrainCell Cell() const noexcept
    {
        return cell_;
    }

    constexpr Heights HeightsValue() const noexcept
    {
        return heights_;
    }

    constexpr WallBits WallBitsValue() const noexcept
    {
        return wallBits_;
    }

    constexpr float SpecialHeight() const noexcept
    {
        return specialHeight_;
    }

  private:
    TerrainCell cell_;
    Heights heights_;
    WallBits wallBits_;
    float specialHeight_;
};

namespace WorldTerrain::Surface
{
// Called only at decode or persistent surface changes. Rendering trusts admission.
inline std::bitset<256> RequiredTiles(const MapDefinition &map, const TerrainMappingData &mapping,
                                      std::span<const WORD> walls, int x = 0, int y = 0,
                                      int width = TERRAIN_SIZE, int height = TERRAIN_SIZE)
{
    std::bitset<256> required;
    if (map.BehaviorMap() == WD_10HEAVEN || map.family == MapDefinition::Family::Hellas)
        return required;
    const bool atlans = map.BehaviorMap() == WD_7ATLANSE || map.BehaviorMap() == WD_67DOPPLEGANGER3;
    for (int row = y; row < y + height; ++row)
        for (int column = x; column < x + width; ++column)
        {
            const int cx = column & TERRAIN_SIZE_MASK, cy = row & TERRAIN_SIZE_MASK;
            if (cx == TERRAIN_SIZE_MASK || cy == TERRAIN_SIZE_MASK)
                continue;
            const int index = cx + cy * TERRAIN_SIZE;
            if (walls[index] & TW_NOGROUND)
                continue;
            const std::array corners{index, index + 1, index + TERRAIN_SIZE + 1,
                                     index + TERRAIN_SIZE};
            bool full = true, any = false;
            for (const int corner : corners)
            {
                full = full && mapping.alpha[corner] == 1.f;
                any = any || mapping.alpha[corner] != 0.f;
            }
            const auto base = full ? mapping.layer2[index] : mapping.layer1[index];
            if (!(map.BehaviorMap() == WD_39KANTURU_3RD && base == 100))
                required.set(base);
            const auto overlay = mapping.layer2[index];
            if (!full && any && overlay != 255 && !(atlans && overlay == 5))
                required.set(overlay);
        }
    return required;
}
} // namespace WorldTerrain::Surface

// Owned terrain image data, validated before a session installs it.
class WorldImageData final
{
  public:
    enum class Kind
    {
        Height,
        ExtendedHeight,
        Light
    };
    using Error = WorldFileData::Error;
    static constexpr std::size_t LegacyHeightHeaderSize = 1080;
    static std::optional<WorldImageData> Load(const std::filesystem::path &path, Kind kind,
                                              float heightScale, Error &error) noexcept;
    std::span<const float> Values() const noexcept
    {
        return values_;
    }
    const auto &LegacyHeightHeader() const noexcept
    {
        return heightHeader_;
    }
    std::size_t StorageBytes() const noexcept
    {
        return values_.capacity() * sizeof(float);
    }

  private:
    bool DecodeHeight(std::span<const std::uint8_t> bytes, Kind kind, float scale, Error &error);
    bool DecodeLight(std::span<const std::uint8_t> bytes, Error &error);
    std::vector<float> values_;
    std::array<std::uint8_t, LegacyHeightHeaderSize> heightHeader_{};
};

// Published through a const lease. Effective collision, height and light work stay local.
struct WorldTerrainAsset final
{
    std::uint64_t revision = 0;
    WorldFileData mapping;
    WorldFileData attributes;
    WorldFileData placements;
    WorldImageData height;
    WorldImageData light;
    TerrainMappingData surface;

    std::size_t StorageBytes() const noexcept
    {
        return sizeof(*this) + mapping.StorageBytes() + attributes.StorageBytes() +
               placements.StorageBytes() + height.StorageBytes() + light.StorageBytes();
    }
};

struct WorldBinding final
{
    const MapDefinition *definition = nullptr;
    std::optional<std::uint32_t> route;

    std::optional<MapId> RawMap() const noexcept;
    std::optional<std::uint64_t> CharacterInstance() const noexcept;
    bool Matches(int rawMap, std::uint32_t destinationRoute) const noexcept;
};

// Owned camera script prepared before world activation. The legacy CWS layout is fixed.
class WorldCameraData final
{
  public:
    struct Waypoint final
    {
        std::int32_t iIndex;
        float fCameraX, fCameraY, fCameraZ;
        std::int32_t iDelay;
        float fCameraMoveAccel, fCameraDistanceLevel;
    };
    static_assert(sizeof(Waypoint) == 28);
    static constexpr std::uint32_t Signature = 0x00535743;
    static std::optional<WorldCameraData> Load(const std::filesystem::path &path,
                                               WorldFileData::Error &error) noexcept;
    std::span<const Waypoint> Waypoints() const noexcept
    {
        return waypoints_;
    }
    std::vector<Waypoint> TakeWaypoints() && noexcept
    {
        return std::move(waypoints_);
    }

  private:
    std::vector<Waypoint> waypoints_;
};

// Owned optional minimap markers; the first empty record ends the authored list.
class WorldMinimapData final
{
  public:
    static constexpr std::size_t MaximumMarkers = 100;
    static constexpr std::size_t MaximumName = 100;
    struct Marker final
    {
        std::uint8_t kind;
        std::array<int, 2> location;
        int rotation;
        std::array<wchar_t, MaximumName> name{};
    };
    static std::optional<WorldMinimapData> Load(const std::filesystem::path &path,
                                                WorldFileData::Error &error) noexcept;
    static std::optional<WorldMinimapData> LoadJson(const std::filesystem::path &path,
                                                    WorldFileData::Error &error) noexcept;
    std::span<const Marker> Markers() const noexcept
    {
        return markers_;
    }
    const std::array<int, 2> &ImageOriginPixels() const noexcept
    {
        return imageOriginPixels_;
    }

  private:
    bool Decode(std::span<std::uint8_t> bytes, WorldFileData::Error &error);
    std::vector<Marker> markers_;
    std::array<int, 2> imageOriginPixels_{};
};

class World;

class WorldReadView final
{
  public:
    SessionId OwnerSession() const noexcept;
    std::uint64_t Revision() const noexcept;
    WorldCapabilityStatus Terrain() const noexcept;
    WorldCapabilityStatus Entities() const noexcept;
    WorldCapabilityStatus Characters() const noexcept;
    WorldCapabilityStatus ItemRegistry() const noexcept;
    WorldCapabilityStatus Map() const noexcept;
    WorldCapabilityStatus Pathing() const noexcept;
    WorldCapabilityStatus GameplayEffects() const noexcept;
    WorldCapabilityStatus ModelClock() const noexcept;
    WorldCapabilityStatus ModelRandom() const noexcept;
    std::optional<TerrainPickSurface> TerrainPickSurfaceAt(TerrainCell cell) const noexcept;
    std::optional<float> HeightAt(float x, float y) const noexcept;
    std::span<const std::uint16_t> MovementAttributes() const noexcept;
    std::optional<std::uint64_t> ModelFrameNumber() const noexcept;

  private:
    friend class World;

    explicit WorldReadView(const World &world) noexcept;

    const World &world_;
};

namespace info
{

struct Script_Silde
{
    typedef std::vector<wchar_t *> SildeVECTOR;
    SildeVECTOR Sildelist;
};

struct Script_Dialog
{
    wchar_t m_lpszText[MAX_LENGTH_DIALOG];
    int m_iNumAnswer;
    int m_iLinkForAnswer[MAX_ANSWER_FOR_DIALOG];
    int m_iReturnForAnswer[MAX_ANSWER_FOR_DIALOG];
    wchar_t m_lpszAnswer[MAX_ANSWER_FOR_DIALOG][MAX_LENGTH_ANSWER];
};

struct Script_Credit
{
    BYTE byClass;
    wchar_t szName[32];
};

struct Script_Movereq
{
    int index;
    wchar_t szMainMapName[32];
    wchar_t szSubMapName[32];
    int iReqLevel;
    int iReqZen;
    int iGateNum;
};

struct Script_Quest_Class_Act
{
    BYTE chLive;
    BYTE byQuestType;
    WORD wItemType;
    BYTE byItemSubType;
    BYTE byItemLevel;
    BYTE byItemNum;
    BYTE byRequestType;
    BYTE byRequestClass[MAX_CLASS];
    short shQuestStartText[4];
};

struct Script_Quest_Class_Request
{
    BYTE byLive;
    BYTE byType;
    WORD wCompleteQuestIndex;
    WORD wLevelMin;
    WORD wLevelMax;
    WORD wRequestStrength;
    DWORD dwZen;
    short shErrorText;
};

struct Script_Quest
{
    short shQuestConditionNum;
    short shQuestRequestNum;
    WORD wNpcType;
    char strQuestName[32];

    Script_Quest_Class_Act QuestAct[MAX_QUEST_CONDITION];
    Script_Quest_Class_Request QuestRequest[MAX_QUEST_REQUEST];
};
}; // namespace info

typedef struct
{
    BYTE Flag;
    BYTE Map;
    BYTE x1;
    BYTE y1;
    BYTE x2;
    BYTE y2;
    WORD Target;
    BYTE Angle;
    WORD Level;
    WORD m_wMaxLevel;
} GATE_ATTRIBUTE;

typedef struct
{
    BYTE Kind;
    int Location[2];
    int Rotation;
    char Name[MAX_MINIMAP_NAME];
} MINI_MAP_FILE;

typedef struct
{
    BYTE Kind;
    int Location[2];
    int Rotation;
    wchar_t Name[MAX_MINIMAP_NAME];
} MINI_MAP;

#pragma pack(push, 1)

#pragma pack(pop)

typedef struct
{
    WORD Life;
    WORD MoveSpeed;
    WORD AttackSpeed;
    WORD AttackDamageMin;
    WORD AttackDamageMax;
    WORD Defense;
    WORD MagicDefense;
    WORD AttackRating;
    WORD SuccessfulBlocking;
} MONSTER_ATTRIBUTE;

typedef struct
{
    WORD Type;
    wchar_t Name[MAX_MONSTER_NAME];
    WORD Level;
    MONSTER_ATTRIBUTE Attribute;
} MONSTER_SCRIPT;

typedef struct tagMONSTER
{
    short Type;
    BYTE Level;
    int Experince;
    MONSTER_ATTRIBUTE Attribute;
} MONSTER;

typedef struct
{
    wchar_t m_strName[64];
    vec3_t m_vPos;
} ObjectDescript;

typedef struct
{
    BYTE bIndex;
    BYTE x;
    BYTE y;
} VisibleUnitLocation;

inline int TERRAIN_INDEX(int x, int y)
{
    return (y)*TERRAIN_SIZE + (x);
}

inline int TERRAIN_INDEX_REPEAT(int x, int y)
{
    return ((y & TERRAIN_SIZE_MASK) * TERRAIN_SIZE) + (x & TERRAIN_SIZE_MASK);
}

inline int MapFileEncrypt(BYTE *pbyDst, BYTE *pbySrc, int iSize)
{
    if (!pbyDst)
    {
        return (iSize);
    }
    BYTE byMapXorKey[16] = {0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27,
                            0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2};

    WORD wMapKey = 0x5E;
    for (int i = 0; i < iSize; ++i)
    {
        pbyDst[i] = (pbySrc[i] + (BYTE)wMapKey) ^ byMapXorKey[i % 16];

        wMapKey = pbyDst[i] + 0x3D;
        wMapKey = wMapKey & 0xFF;
    }

    return (iSize);
}

inline int MapFileDecrypt(BYTE *pbyDst, BYTE *pbySrc, int iSize)
{
    if (!pbyDst)
    {
        return (iSize);
    }
    BYTE byMapXorKey[16] = {0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27,
                            0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2};

    WORD wMapKey = 0x5E;
    for (int i = 0; i < iSize; ++i)
    {
        pbyDst[i] = (pbySrc[i] ^ byMapXorKey[i % 16]) - (BYTE)wMapKey;
        wMapKey = pbySrc[i] + 0x3D;
        wMapKey = wMapKey & 0xFF;
    }
    return (iSize);
}

namespace WorldLoadingDetail
{

void ReleaseWorldBehaviorModels(SessionKeeper &keeper, const MapDefinition *definition);
}

enum class CastleLevel : std::uint8_t
{
    Zero = 0,
    One = 1,
    Two = 2,
    Three = 3,
    Four = 4,
    Five = 5,
    Six = 6,
    Seven = 7,
    Eight = 8,
    Invalid = 255,
};

#define MAX_BOIDS 40
#define MAX_FISHS 10
