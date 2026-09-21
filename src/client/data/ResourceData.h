#pragma once
#include "support/CoreMath.h"
#include "data/WorldData.h"
#include "render/Textures.h"

#include <memory>
#include <functional>
#include <bitset>

class CErrorReport;

/**
 * Generic file I/O operations for game data
 *
 * Provides unified interface for reading/writing encrypted data files
 * with checksum verification. Works with any data type.
 */
namespace DataFileIO
{
/**
     * Configuration for file I/O operations
     */
struct IOConfig
{
    int itemSize;      // Size of each record
    int itemCount;     // Number of records
    DWORD checksumKey; // Checksum generation key (e.g., 0xE2F1 for items, 0x5A18 for skills)

    // Optional: Custom encryption/decryption per record (e.g., BuxConvert)
    std::function<void(BYTE *, int)> encryptRecord = nullptr;
    std::function<void(BYTE *, int)> decryptRecord = nullptr;
};

/**
     * Read buffer from file with optional decryption
     * Returns unique_ptr for automatic memory management
     *
     * @param fp File pointer (must be opened in binary read mode)
     * @param config I/O configuration
     * @param window Exact owner for any error dialog
     * @param outChecksum Optional pointer to receive checksum from file
     * @return Unique pointer to buffer, or nullptr on failure
     */
std::unique_ptr<BYTE[]> ReadBuffer(FILE *fp, const IOConfig &config, CErrorReport &errorReport,
                                   HWND window, DWORD *outChecksum = nullptr);

/**
     * Verify checksum of buffer
     *
     * @param buffer Data buffer to verify
     * @param config I/O configuration (uses checksumKey)
     * @param expectedChecksum Checksum value from file
     * @return true if checksum matches, false otherwise
     */
bool VerifyChecksum(const BYTE *buffer, const IOConfig &config, DWORD expectedChecksum);

/**
     * Decrypt buffer in-place using per-record decryption
     *
     * @param buffer Buffer to decrypt
     * @param config I/O configuration (uses decryptRecord function)
     */
void DecryptBuffer(BYTE *buffer, const IOConfig &config);

/**
     * Show error message and log to error report
     *
     * @param window Exact owner for the error dialog
     * @param message Error message to display
     */
void ShowErrorAndExit(CErrorReport &errorReport, HWND window, const wchar_t *message);
} // namespace DataFileIO

class SessionKeeper;
class ApplicationKeeper;
class BMD;
struct BmdSharedAsset;
struct WorldModelDependency;

// Core world payloads prepared together without touching live session state.
class WorldResources final
{
  public:
    struct Failure
    {
        std::filesystem::path resource;
        WorldFileData::Error reason = WorldFileData::Error::None;
        const wchar_t *detail = nullptr;
    };
    static std::optional<WorldResources> Load(const MapDefinition &definition,
                                              MapDefinition::Variant variant,
                                              const std::filesystem::path &root, Failure &failure,
                                              ApplicationKeeper *application = nullptr) noexcept;
    const WorldFileData &Mapping() const noexcept
    {
        return terrain_->mapping;
    }
    const WorldFileData &Attributes() const noexcept
    {
        return terrain_->attributes;
    }
    const WorldFileData &Placements() const noexcept
    {
        return terrain_->placements;
    }
    const WorldImageData &Height() const noexcept
    {
        return terrain_->height;
    }
    const std::shared_ptr<const WorldTerrainAsset> &TerrainAsset() const noexcept
    {
        return terrain_;
    }
    std::optional<WorldCameraData> &Camera() noexcept
    {
        return camera_;
    }
    const WorldImageData &Light() const noexcept
    {
        return terrain_->light;
    }

    bool PrepareModels(SessionKeeper &keeper, const MapDefinition &definition,
                       const std::filesystem::path &root, Failure &failure);
    bool PrepareModels(SessionKeeper &keeper, std::span<const WorldModelDependency> dependencies,
                       const std::filesystem::path &root, Failure &failure, int behaviorMap = -1);
    bool PreparePrimaryModels(SessionKeeper &keeper, const MapDefinition &definition,
                              const std::filesystem::path &root, Failure &failure);
    bool PrepareModelTextures(SessionKeeper &keeper, Failure &failure);
    bool InstallModels(SessionKeeper &keeper, Failure &failure) const;
    bool InstallModelVisuals(SessionKeeper &keeper, Failure &failure) const;
    bool HasPreparedModel(int slot) const noexcept;
    bool PrepareTerrainTextures(SessionKeeper &keeper, const MapDefinition &definition,
                                const std::filesystem::path &root, Failure &failure,
                                const TerrainMappingData *effectiveMapping = nullptr);
    bool InstallTerrainTextures(SessionKeeper &keeper, Failure &failure) const;
    bool PrepareEffectTextures(SessionKeeper &keeper, const MapDefinition &definition,
                               const std::filesystem::path &root, Failure &failure);
    bool InstallEffectTextures(SessionKeeper &keeper, Failure &failure) const;
    bool InstallMinimapTexture(SessionKeeper &keeper, Failure &failure) const;
    bool HasMinimapImage() const noexcept
    {
        return minimapTexture_.has_value();
    }
    const WorldMinimapData &Minimap() const noexcept
    {
        return minimap_;
    }

  private:
    struct PreparedTexture
    {
        std::uint32_t slot;
        std::wstring path;
        LegacyTextureFilter filter;
        LegacyTextureWrap wrap;
    };
    bool InstallTextures(SessionKeeper &keeper, std::span<const PreparedTexture> textures,
                         Failure &failure) const;
    std::unique_ptr<SessionTextureNamespace> effectTextures_;
    std::vector<PreparedTexture> preparedEffectTextures_;
    bool TryPrepareTerrainTexture(SessionKeeper &keeper, std::uint32_t slot,
                                  const std::filesystem::path &path, LegacyTextureFilter filter,
                                  LegacyTextureWrap wrap);
    void PrepareSceneryTextures(SessionKeeper &keeper, const MapDefinition &definition,
                                const std::filesystem::path &root);
    bool PrepareTerrainTexture(SessionKeeper &keeper, std::uint32_t slot,
                               const std::filesystem::path &path, LegacyTextureFilter filter,
                               LegacyTextureWrap wrap, bool required, Failure &failure);
    void PrepareMinimap(SessionKeeper &keeper, const MapDefinition &definition,
                        const std::filesystem::path &root);
    std::optional<PreparedTexture> minimapTexture_;
    WorldMinimapData minimap_;
    std::unique_ptr<SessionTextureNamespace> terrainTextures_;
    std::vector<PreparedTexture> preparedTextures_;
    std::bitset<256> admittedTiles_;
    struct PreparedModel
    {
        int slot;
        std::filesystem::path path;
        std::shared_ptr<BmdSharedAsset> asset;
        std::filesystem::path textureDirectory;
        std::vector<PreparedTexture> textures;
        bool required = true;
        LegacyTextureFilter textureFilter = LegacyTextureFilter::Nearest;
    };
    bool PrepareModelTexture(SessionKeeper &keeper, PreparedModel &model, int index,
                             Failure &failure);
    bool PrepareModel(SessionKeeper &keeper, int slot, const std::filesystem::path &path,
                      int requiredActions, Failure &failure,
                      const std::filesystem::path &textureDirectory = {},
                      bool requireGeometry = true, int behaviorMap = -1);
    bool PrepareActionModels(SessionKeeper &keeper, const MapDefinition &definition,
                             const std::filesystem::path &root, Failure &failure);
    bool PrepareSpawnModels(SessionKeeper &keeper, const MapDefinition &definition,
                            const std::filesystem::path &root, Failure &failure);
    bool PrepareCharacterModels(SessionKeeper &keeper, const std::filesystem::path &root,
                                Failure &failure);
    bool InstallModelTextures(SessionKeeper &keeper, const PreparedModel &prepared, BMD &model,
                              Failure &failure) const;
    std::unique_ptr<SessionTextureNamespace> modelTextures_;
    std::vector<PreparedModel> models_;
    static std::shared_ptr<const WorldTerrainAsset> LoadTerrainAsset(
        const MapDefinition &definition, MapDefinition::Variant variant,
        const std::filesystem::path &folder, Failure &failure, ApplicationKeeper *application);
    static std::shared_ptr<WorldTerrainAsset> DecodeTerrainAsset(
        const MapDefinition &definition, MapDefinition::Variant variant,
        const std::filesystem::path &folder, Failure &failure);
    static bool LoadImages(WorldTerrainAsset &terrain, const MapDefinition &definition,
                           MapDefinition::Variant variant, const std::filesystem::path &folder,
                           Failure &failure);
    std::optional<WorldCameraData> camera_;
    std::shared_ptr<const WorldTerrainAsset> terrain_;
};
