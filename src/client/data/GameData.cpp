#include "data/GameData.h"
#include "app/ApplicationKeeper.h"
#include "support/CoreMath.h"
#include "data/ResourceData.h"
#include "data/CharacterData.h"
#include "data/ItemData.h"
#include "data/WorldData.h"
#include "domain/ChatSocial.h"
#include "domain/EffectsUpdate.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/Quests.h"
#include "domain/WorldSimulation.h"
#include "app/AppWindow.h"
#include "app/ApplicationDiagnostics.h"
#include "data/Localization.h"
#include "domain/Automation.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/Guild.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/WorldPresentation.h"
#include "render/ModelGeometry.h"
#include "render/ModelResources.h"
#include "render/Sprites.h"
#include "render/Terrain.h"
#include "render/Text.h"
#include "session/SessionAudio.h"
#include "support/Camera.h"
#include "support/Scenes.h"
#include "ui/features/Activities/ActivitiesLogic.h"
#include "ui/features/Hud/HudLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/QuestNpc/QuestNpcLogic.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/session/UiSessionLogic.h"
#include "app/ApplicationNetwork.h"
#include "session/SessionPresentation.h"
#include "session/SessionGameplay.h"
#include "session/SessionRender.h"
#include "session/SessionKeeper.h"
#include "ui/runtime/UiControls.h"
#include "render/World.h"
#include "render/Textures.h"
#include "ui/features/Items/ItemsRender.h"
#include "app/ApplicationAudio.h"
#include "domain/WorldPhysics.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/features/Social/SocialRender.h"
#include "ui/features/Hud/HudRender.h"
#include "ui/features/Dialogs/DialogsRender.h"
#include "ui/features/World/WorldRender.h"
#include "I18N/All.h"

GameData::GameData(ApplicationKeeper &keeper, GameDataLoadErrorReporter loadErrorReporter) noexcept
    : ApplicationLegacyCalls(keeper), storage_(keeper.GameDataStorageRef()),
      errorReport_(keeper.ErrorReport()), g_hWnd(keeper.PlatformStorageRef().g_hWnd),
      loadErrorReporter_(loadErrorReporter), itemDataLoader_(ItemAttribute),
      skillDataLoader_(SkillAttribute), registered_(applicationKeeper_.RegisterGameData(*this))
{
}

bool GameData::LoadItemDataFile(wchar_t *fileName, CErrorReport &errorReport, HWND window)
{
    return itemDataLoader_.Load(fileName, errorReport, window);
}

bool GameData::LoadSkillDataFile(wchar_t *fileName, CErrorReport &errorReport, HWND window)
{
    return skillDataLoader_.Load(fileName, errorReport, window);
}

GameData::~GameData() noexcept
{
    Shutdown();
}

bool GameData::SelectLanguage(const std::wstring &assetLanguage) noexcept
{
    const std::lock_guard lock(loadMutex_);
    if (!registered_ || loading_ || loaded_)
    {
        return false;
    }

    storage_.g_strSelectedML = assetLanguage.substr(0, MAX_LANGUAGE_NAME_LENGTH - 1);
    std::fill(std::begin(storage_.g_aszMLSelection), std::end(storage_.g_aszMLSelection), L'\0');
    std::copy(storage_.g_strSelectedML.begin(), storage_.g_strSelectedML.end(),
              storage_.g_aszMLSelection);
    return true;
}

const std::wstring &GameData::AssetLanguage() const noexcept
{
    return storage_.g_strSelectedML;
}

bool GameData::IsLoaded() const noexcept
{
    const std::lock_guard lock(loadMutex_);
    return registered_ && loaded_;
}

void GameData::RecordLoadFailure() noexcept
{
    loadFailed_ = true;
}

void GameData::ReportLoadError(const wchar_t *fileName, GameDataLoadError error,
                               bool closeWindow) noexcept
{
    RecordLoadFailure();
    if (loadErrorReporter_ != nullptr)
    {
        loadErrorReporter_(errorReport_, g_hWnd, fileName, error, closeWindow);
    }
}

void GameData::Shutdown() noexcept
{
    if (!registered_)
    {
        return;
    }
    const std::lock_guard loadLock(loadMutex_);
    ResetStorage();
}

void GameData::ResetStorage() noexcept
{
    delete[] storage_.GateAttribute;
    storage_.GateAttribute = nullptr;
    std::memset(storage_.AbuseFilter, 0, sizeof(storage_.AbuseFilter));
    std::memset(storage_.AbuseNameFilter, 0, sizeof(storage_.AbuseNameFilter));
    std::memset(storage_.MonsterScript, 0, sizeof(storage_.MonsterScript));
    std::memset(storage_.MonsterSkill, 0, sizeof(storage_.MonsterSkill));
    std::memset(storage_.ClassAttribute, 0, sizeof(storage_.ClassAttribute));
    std::memset(storage_.g_aszMLSelection, 0, sizeof(storage_.g_aszMLSelection));
    storage_.AbuseFilterNumber = 0;
    storage_.AbuseNameFilterNumber = 0;
    storage_.EditMonsterNumber = 0;
    storage_.g_strSelectedML.clear();
    loading_ = false;
    loaded_ = false;
    loadFailed_ = false;
}

void ReportLegacyGameDataLoadError(CErrorReport &errorReport, HWND window, const wchar_t *fileName,
                                   GameDataLoadError error, bool closeWindow) noexcept
{
    wchar_t text[256]{};
    mu_swprintf(text,
                error == GameDataLoadError::MissingFile ? L"%ls - File not exist."
                                                        : L"%ls - File corrupted.",
                fileName);
    errorReport.Write(text);
    MessageBox(window, text, nullptr, MB_OK);
    if (closeWindow)
    {
        SendMessage(window, WM_DESTROY, 0, 0);
    }
}

bool SkillDataLoader::Load(wchar_t *fileName, CErrorReport &errorReport, HWND window)
{
    FILE *fp = _wfopen(fileName, L"rb");
    if (fp == NULL)
    {
        wchar_t errorMsg[256];
        mu_swprintf(errorMsg, L"Skill file not found: %ls", fileName);
        DataFileIO::ShowErrorAndExit(errorReport, window, errorMsg);
        return false;
    }

    // Get file size to determine structure version
    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    const int LegacySize = sizeof(SKILL_ATTRIBUTE_FILE_LEGACY);
    const int NewSize = sizeof(SKILL_ATTRIBUTE_FILE);
    const long expectedLegacySize = LegacySize * MAX_SKILLS + sizeof(DWORD);
    const long expectedNewSize = NewSize * MAX_SKILLS + sizeof(DWORD);

    bool isLegacyFormat = (fileSize == expectedLegacySize);
    bool success = false;

    if (isLegacyFormat)
    {
        success = LoadLegacyFormat(fp, fileSize, errorReport, window);
    }
    else
    {
        success = LoadNewFormat(fp, fileSize, errorReport, window);
    }

    fclose(fp);

    return success;
}

template <typename TFileFormat>
bool SkillDataLoader::LoadFormat(FILE *fp, const wchar_t *formatName, CErrorReport &errorReport,
                                 HWND window)
{
    const int Size = sizeof(TFileFormat);

    // Configure I/O
    DataFileIO::IOConfig config;
    config.itemSize = Size;
    config.itemCount = MAX_SKILLS;
    config.checksumKey = 0x5A18;
    config.decryptRecord = [](BYTE *data, int size) { BuxConvert(data, size); };

    // Read buffer and checksum
    DWORD dwCheckSum;
    auto buffer = DataFileIO::ReadBuffer(fp, config, errorReport, window, &dwCheckSum);
    if (!buffer)
    {
        std::wstring errorMsg = L"Failed to read skill file (";
        errorMsg += formatName;
        errorMsg += L").";
        DataFileIO::ShowErrorAndExit(errorReport, window, errorMsg.c_str());
        return false;
    }

    // Verify checksum
    if (!DataFileIO::VerifyChecksum(buffer.get(), config, dwCheckSum))
    {
        std::wstring errorMsg = L"Skill file corrupted (";
        errorMsg += formatName;
        errorMsg += L").";
        DataFileIO::ShowErrorAndExit(errorReport, window, errorMsg.c_str());
        return false;
    }

    // Decrypt buffer
    DataFileIO::DecryptBuffer(buffer.get(), config);

    // Copy skills
    BYTE *pSeek = buffer.get();
    for (int i = 0; i < MAX_SKILLS; i++)
    {
        TFileFormat source;
        memcpy(&source, pSeek, sizeof(source));
        CopySkillAttributeFromSource(SkillAttribute[i], source);
        pSeek += Size;
    }

    return true;
}

bool SkillDataLoader::LoadLegacyFormat(FILE *fp, long fileSize, CErrorReport &errorReport,
                                       HWND window)
{
    return LoadFormat<SKILL_ATTRIBUTE_FILE_LEGACY>(fp, L"legacy format", errorReport, window);
}

bool SkillDataLoader::LoadNewFormat(FILE *fp, long fileSize, CErrorReport &errorReport, HWND window)
{
    return LoadFormat<SKILL_ATTRIBUTE_FILE>(fp, L"new format", errorReport, window);
}

void SessionGameDataUnit::SetMonsterSound(int Type, int s1, int s2, int s3, int s4, int s5, int s6,
                                          int s7, int s8, int s9, int s10)
{
    Models[Type].Sounds[0] = s1;
    Models[Type].Sounds[1] = s2;
    Models[Type].Sounds[2] = s3;
    Models[Type].Sounds[3] = s4;
    Models[Type].Sounds[4] = s5;
    Models[Type].Sounds[5] = s6;
    Models[Type].Sounds[6] = s7;
    Models[Type].Sounds[7] = s8;
    Models[Type].Sounds[8] = s9;
    Models[Type].Sounds[9] = s10;
}

void SessionGameDataUnit::DeleteMonsters()
{
    {
        const std::lock_guard lock(pendingMonsterModelsMutex_);
        pendingMonsterModels_.clear();
        monsterModelLoadPending_.store(false, std::memory_order_release);
    }
    for (int i = MODEL_MONSTER01; i < MODEL_MONSTER_END; i++)
    {
        if (BMD *model = Models.Find(i))
        {
            for (const auto &dependency : WorldModelDependency::ForMonster(i - MODEL_MONSTER01))
                if (BMD *part = Models.Find(dependency.slot))
                    part->Release();
            model->Release();
        }
    }

    for (int i = SOUND_MONSTER; i < SOUND_MONSTER_END; i++)
        ReleaseBuffer(i);

    for (int i = SOUND_ELBELAND_RABBITSTRANGE_ATTACK01; i <= SOUND_ELBELAND_ENTERATLANCE01; i++)
        ReleaseBuffer(i);
}

bool SessionGameDataUnit::OpenMonsterModel(EMonsterModelType Type)
{
    if (Type < 0 || Type >= MONSTER_MODEL_COUNT) // guard the Models[] indexing below
        return false;

    int Index = static_cast<int>(MODEL_MONSTER01) + Type;

    BMD *b = &Models[Index];
    if (b->NumActions > 0 || b->NumMeshs > 0)
        return true;

    if (!sessionKeeper_.ApplicationKeeperRef().IsOwnerThread())
    {
        try
        {
            std::lock_guard lock(pendingMonsterModelsMutex_);
            if (std::find(pendingMonsterModels_.begin(), pendingMonsterModels_.end(), Type) ==
                pendingMonsterModels_.end())
            {
                pendingMonsterModels_.push_back(Type);
                monsterModelLoadPending_.store(true, std::memory_order_release);
            }
        }
        catch (...)
        {
        }
        return false;
    }

    try
    {
        g_ErrorReport.Write(L"OpenMonsterModel(%ls = %d)\r\n",
                            WorldModelLoadingDetail::GetMonsterModelName(Type), Type);
        if (!gLoadData.AccessModel(Index, L"Data\\Monster\\", L"Monster", Type + 1) ||
            !PrepareMonsterResources(Type))
            return false;
        ConfigureMonsterModel(Type);
        return true;
    }
    catch (...)
    {
        b->Release();
        throw;
    }
}

void SessionGameDataUnit::ConfigureMonsterModel(EMonsterModelType Type)
{
    BMD *b = &Models[MODEL_MONSTER01 + Type];
    b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
    b->Actions[MONSTER01_STOP2].PlaySpeed = 0.2f;
    b->Actions[MONSTER01_WALK].PlaySpeed = 0.34f;
    b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.33f;
    b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.33f;
    b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.5f;
    b->Actions[MONSTER01_DIE].PlaySpeed = 0.55f;
    b->Actions[MONSTER01_DIE].Loop = true;

    for (int j = MONSTER01_STOP1; j < MONSTER01_DIE; j++)
    {
        if (Type == 3)
            b->Actions[j].PlaySpeed *= 1.2f;
        if (Type == 5 || Type == 25)
            b->Actions[j].PlaySpeed *= 0.7f;
        if (Type == 37 || Type == 42)
            b->Actions[j].PlaySpeed *= 0.4f;
    }

    switch (Type)
    {
    case MONSTER_MODEL_BUDGE_DRAGON:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.7f;
        break;
    case MONSTER_MODEL_LARVA:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.6f;
        break;
    case MONSTER_MODEL_HELL_SPIDER:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.7f;
        break;
    case MONSTER_MODEL_SPIDER:
        b->Actions[MONSTER01_WALK].PlaySpeed = 1.2f;
        break;
    case MONSTER_MODEL_CYCLOPS:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.28f;
        break;
    case MONSTER_MODEL_YETI:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.3f;
        break;
    case MONSTER_MODEL_ELITE_YETI:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.28f;
        break;
    case MONSTER_MODEL_WORM:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.5f;
        break;
    case MONSTER_MODEL_GOBLIN:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.6f;
        break;
    case MONSTER_MODEL_CHAIN_SCORPION:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.4f;
        break;
    case MONSTER_MODEL_BEETLE_MONSTER:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.5f;
        break;
    case MONSTER_MODEL_SHADOW:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.3f;
        break;
    case MONSTER_MODEL_TITAN:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.22f;
        break;
    case MONSTER_MODEL_GOLDEN_WHEEL:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.18f;
        break;
    case MONSTER_MODEL_TANTALLOS:
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.35f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.35f;
        break;
    case MONSTER_MODEL_BEAM_KNIGHT:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.3f;
        break;
    case MONSTER_MODEL_DEATH_ANGEL:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.1f;
        break;
    case MONSTER_MODEL_ILLUSION_OF_KUNDUN:
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.3f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.15f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.25f;
        break;
    case MONSTER_MODEL_AEGIS:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.1f;
        break;
    case MONSTER_MODEL_DEATH_CENTURION:
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.2f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.3f;
        break;
    case MONSTER_MODEL_SHRIKER:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.1f;
        break;
    case MONSTER_MODEL_CHAOSCASTLE_KNIGHT: //
    case MONSTER_MODEL_CHAOSCASTLE_ELF:    //
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.5f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.5f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.3f;
        break;
    case MONSTER_MODEL_CHAOSCASTLE_WIZARD:
        //    case MONSTER_MODEL_CASTLE_GATE1:
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.4f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.4f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.3f;
        break;
    case MONSTER_MODEL_BALGASS:
        b->Actions[MONSTER01_APEAR].PlaySpeed = 0.33f;
        b->Actions[MONSTER01_ATTACK3].PlaySpeed = 0.33f;
        b->Actions[MONSTER01_ATTACK4].PlaySpeed = 0.33f;
        break;
    case MONSTER_MODEL_SORAM:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.20f;
        break;
    case MONSTER_MODEL_DARK_ELF_1:
        b->Actions[MONSTER01_APEAR].PlaySpeed = 0.33f;
        b->Actions[MONSTER01_ATTACK3].PlaySpeed = 0.33f;
        break;
    case MONSTER_MODEL_BALLISTA:
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.37f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.37f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.15f;
        break;
    case MONSTER_MODEL_WITCH_QUEEN:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.2f;
        break;
    case MONSTER_MODEL_GOLDEN_STONE_GOLEM:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.2f;
    case MONSTER_MODEL_DEATH_RIDER:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.15f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.15f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.23f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.23f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.3f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.15f;
        break;
    case MONSTER_MODEL_DEATH_TREE:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.2f;
        break;
    case MONSTER_MODEL_HELL_MAINE:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.15f;
        break;
    case MONSTER_MODEL_BERSERK:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.23f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.28f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.28f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.19f;
        break;
    case MONSTER_MODEL_SPLINTER_WOLF:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.27f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.30f;
        break;
    case MONSTER_MODEL_IRON_RIDER:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.27f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.25f;
        break;
    case MONSTER_MODEL_SATYROS:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.27f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.3f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.3f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.3f;
        break;
    case MONSTER_MODEL_BLADE_HUNTER:
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.23f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.23f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.2f;
        break;
    case MONSTER_MODEL_KENTAUROS:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.27f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.27f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.27f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.25f;
        break;
    case MONSTER_MODEL_GIGANTIS:
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.26f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.26f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.21f;
        break;
    case MONSTER_MODEL_GENOCIDER:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.3f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.3f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.5f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.3f;
        break;
    case MONSTER_MODEL_PERSONA:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.34f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.33f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.33f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.23f;
        break;
    case MONSTER_MODEL_TWIN_TAIL:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.34f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.23f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.23f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.23f;
        break;
    case MONSTER_MODEL_DREADFEAR:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.34f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.25f;
        break;
    case MONSTER_MODEL_DARK_SKULL_SOLDIER_5:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.22f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.22f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.12f;
        b->Actions[MONSTER01_APEAR].PlaySpeed = 0.22f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK3].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK4].PlaySpeed = 0.25f;
        break;
    case MONSTER_MODEL_MAYA_HAND_LEFT:
        b->Actions[MONSTER01_APEAR].PlaySpeed = 0.12f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.12f;
    case MONSTER_MODEL_MAYA_HAND_RIGHT:
        b->Actions[MONSTER01_APEAR].PlaySpeed = 0.12f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.12f;
    case MONSTER_MODEL_MAYA:
        break;
    case MONSTER_MODEL_POUCH_OF_BLESSING:
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.3f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.22f;
        break;
    case MONSTER_MODEL_LUNAR_RABBIT:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.50f;
        break;
    case MONSTER_MODEL_FIRE_FLAME_GHOST:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.60f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.50f;
        break;
    case MONSTER_MODEL_ZOMBIE_FIGHTER: {
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.28f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.3f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.17f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.2f;
    }
    break;
    case MONSTER_MODEL_GLADIATOR: {
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.3f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.28f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.2f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.18f;
    }
    break;
    case MONSTER_MODEL_SLAUGTHERER: {
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.2f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.4f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.17f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.2f;
    }
    break;
    case MONSTER_MODEL_BLOOD_ASSASSIN: {
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.28f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.5f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.17f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.4f;
    }
    break;
    case MONSTER_MODEL_CRUEL_BLOOD_ASSASSIN: {
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.28f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.5f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.17f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.4f;
    }
    break;
    case MONSTER_MODEL_LAVA_GIANT: {
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.28f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.3f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.25f;
    }
    break;
    case MONSTER_MODEL_BURNING_LAVA_GIANT: {
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.28f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.3f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.25f;
    }
    break;
    case MONSTER_MODEL_RABBIT:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.28f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.25f;
        break;
    case MONSTER_MODEL_BUTTERFLY:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.20f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.20f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.23f;
        break;
    case MONSTER_MODEL_HIDEOUS_RABBIT:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.30f;
        break;
    case MONSTER_MODEL_WEREWOLF2:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.45f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.45f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.45f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.45f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.45f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.45f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.45f;
        break;
    case MONSTER_MODEL_CURSED_LICH:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.20f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.20f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.35;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.35f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.30f;
        break;
    case MONSTER_MODEL_TOTEM_GOLEM:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.20f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.35;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.35f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.20f;
        break;
    case MONSTER_MODEL_GRIZZLY:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.33f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.33f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.22f;
        break;
    case MONSTER_MODEL_CAPTAIN_GRIZZLY:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.28f;
        break;
    case MONSTER_MODEL_SAPIUNUS:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.25f;
        break;
    case MONSTER_MODEL_SAPIDUO:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.25f;
        break;
    case MONSTER_MODEL_SAPITRES:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.25f;
        break;
    case MONSTER_MODEL_SHADOW_PAWN:
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.4f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.4f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.25f;
        break;
    case MONSTER_MODEL_SHADOW_KNIGHT:
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.45f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.45f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.25f;
        break;
    case MONSTER_MODEL_SHADOW_LOOK:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.25f;
        break;
    case MONSTER_MODEL_NAPIN:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.20f;
        break;
    case MONSTER_MODEL_GHOST_NAPIN:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.20f;
        break;
    case MONSTER_MODEL_BLAZE_NAPIN:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.20f;
        break;
    case MONSTER_MODEL_ICE_WALKER:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.6f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.4f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.35f;
        break;
    case MONSTER_MODEL_GIANT_MAMMOTH:
        break;
    case MONSTER_MODEL_ICE_GIANT:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.13f;
        break;
    case MONSTER_MODEL_COOLUTIN:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.46f;
        break;
    case MONSTER_MODEL_IRON_KNIGHT:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.21f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.23f;
        break;
    case MONSTER_MODEL_SELUPAN:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.20f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.35f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.18f;
        b->Actions[MONSTER01_APEAR].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK3].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK4].PlaySpeed = 0.25f;
        break;
    case MONSTER_MODEL_SPIDER_EGGS_1:
    case MONSTER_MODEL_SPIDER_EGGS_2:
    case MONSTER_MODEL_SPIDER_EGGS_3:
        break;
    case MONSTER_MODEL_CURSED_SANTA:
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.3f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.29f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.4f;
        break;
    case MONSTER_MODEL_GAYION:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.38f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.20f;
        b->Actions[MONSTER01_APEAR].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_ATTACK3].PlaySpeed = 0.38f;
        b->Actions[MONSTER01_ATTACK4].PlaySpeed = 0.38f;
        break;
    case MONSTER_MODEL_JERRY:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.86f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.86f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_APEAR].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_ATTACK3].PlaySpeed = 0.76f;
        break;
    case MONSTER_MODEL_RAYMOND:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.60f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.60f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.55f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.75f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_ATTACK3].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_APEAR].PlaySpeed = 0.80f;
        break;
    case MONSTER_MODEL_LUCAS:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.60f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.60f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.71f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_APEAR].PlaySpeed = 0.80f;
        b->Actions[MONSTER01_ATTACK3].PlaySpeed = 0.40f;
        break;
    case MONSTER_MODEL_FRED:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.33f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.38f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_ATTACK3].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_ATTACK4].PlaySpeed = 0.45f;
        break;
    case MONSTER_MODEL_HAMMERIZE:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.35f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.45f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_APEAR].PlaySpeed = 0.45f;
        b->Actions[MONSTER01_ATTACK3].PlaySpeed = 0.45f;
        break;
    case MONSTER_MODEL_DUAL_BERSERKER:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.35f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.35f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.35f;
        b->Actions[MONSTER01_ATTACK3].PlaySpeed = 0.40f;
        break;
    case MONSTER_MODEL_DEVIL_LORD:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.45f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_ATTACK3].PlaySpeed = 0.45f;
        break;
    case MONSTER_MODEL_QUARTER_MASTER:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.20f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.66f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.33f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.55f;
        break;
    case MONSTER_MODEL_COMBAT_INSTRUCTOR:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.20f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.33f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.36f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_APEAR].PlaySpeed = 0.40f;
        break;
    case MONSTER_MODEL_ATICLES_HEAD:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.65f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.86f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.86f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_APEAR].PlaySpeed = 0.86f;
        break;
    case MONSTER_MODEL_DARK_GHOST:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.60f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.60f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.80f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.96f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.96f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 1.00f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.30f;
        break;
    case MONSTER_MODEL_BANSHEE:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.20f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.20f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.38f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_ATTACK3].PlaySpeed = 0.38f;
        break;
    case MONSTER_MODEL_HEAD_MOUNTER:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.37f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.33f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.33f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.30f;
        break;
    case MONSTER_MODEL_DEFENDER:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.45f;
        b->Actions[MONSTER01_APEAR].PlaySpeed = 0.40f;
        break;
#ifdef LDS_ADD_EG_2_MONSTER_GUARDIANPRIEST
    case MONSTER_MODEL_FORSAKER:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.80f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.80f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.80f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.80f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.80f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.40f;
        break;
#endif // LDS_ADD_EG_2_MONSTER_GUARDIANPRIEST
    case MONSTER_MODEL_OCELOT:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.20f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.55f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.66f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.66f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.55f;
        break;
    case MONSTER_MODEL_ERIC:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.35f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.33f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.33f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.50f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.30f;
        break;
    case MONSTER_MODEL_MAD_BUTCHER:
    case MONSTER_MODEL_TERRIBLE_BUTCHER:
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.3f;
        break;
    case MONSTER_MODEL_DOPPELGANGER:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f * 2.0f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.2f * 2.0f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.34f * 2.0f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.25f * 2.0f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.25f * 2.0f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.5f * 2.0f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.55f * 2.0f;
        b->Actions[MONSTER01_APEAR].PlaySpeed = 0.33f * 3.0f;
        break;
    case MONSTER_MODEL_MEDUSA:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.30f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.30;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.30;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.30;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.30;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.30;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.30;
        b->Actions[MONSTER01_APEAR].PlaySpeed = 0.30;
        b->Actions[MONSTER01_ATTACK3].PlaySpeed = 0.30;
        break;
    case MONSTER_MODEL_DARK_MAMMOTH:
        break;
    case MONSTER_MODEL_DARK_GIANT:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.13f;
        break;
    case MONSTER_MODEL_DARK_COOLUTIN:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.46f;
        break;
    case MONSTER_MODEL_DARK_IRON_KNIGHT:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.21f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.23f;
        break;
    case MONSTER_MODEL_BLOODY_ORC:
        break;
    case MONSTER_MODEL_BLOODY_DEATH_RIDER:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.15f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.15f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.23f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.23f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.3f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.15f;
        break;
    case MONSTER_MODEL_BLOODY_GOLEM:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.2f;
        break;
    case MONSTER_MODEL_BLOODY_WITCH_QUEEN:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.2f;
        break;
    case MONSTER_MODEL_SAPI_QUEEN:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.25f;
        break;
    case MONSTER_MODEL_ICE_NAPIN:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.20f;
        break;
    case MONSTER_MODEL_SHADOW_MASTER:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.25f;
        break;
    case MONSTER_MODEL_BERSERKER_WARRIOR:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.23f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.28f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.28f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.19f;
        break;
    case MONSTER_MODEL_KENTAUROS_WARRIOR:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.27f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.27f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.27f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.25f;
        break;
    case MONSTER_MODEL_GIGANTIS_WARRIOR:
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.26f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.26f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.21f;
        break;
    case MONSTER_MODEL_SOCCERBALL:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.3f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.3f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.5f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.3f;
        break;
#ifdef ASG_ADD_KARUTAN_MONSTERS
    case MONSTER_MODEL_VENOMOUS_CHAIN_SCORPION:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.95f;
        break;
    case MONSTER_MODEL_BONE_SCORPION:
        b->Actions[MONSTER01_WALK].PlaySpeed = 1.00f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.40f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.20f;
        break;
    case MONSTER_MODEL_ORCUS:
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.7f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.7f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.6f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.8f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.8f;
        b->Actions[MONSTER01_SHOCK].PlaySpeed = 0.25f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.3f;
        break;
    case MONSTER_MODEL_GOLLOCK:
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.4f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.3f;
        break;
    case MONSTER_MODEL_CRYPTA:
    case MONSTER_MODEL_CRYPOS:
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.9f;
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.37f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.37f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.25f;
        break;
    case MONSTER_MODEL_CONDRA:
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.80f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.80f;
        break;
    case MONSTER_MODEL_NACONDRA:
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.75f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.75f;
        break;
#endif // ASG_ADD_KARUTAN_MONSTERS
    }
    switch (Type)
    {
    case MONSTER_MODEL_ASSASSIN:
    case MONSTER_MODEL_DRAGON:
    case MONSTER_MODEL_TITAN:
    case MONSTER_MODEL_SOLDIER:
        b->Actions[MONSTER01_STOP2].Loop = true;
        break;
    }

    int Channel = 2;
    bool Enable = true;
    switch (Type)
    {
    case MONSTER_MODEL_CHAOSCASTLE_KNIGHT:
    case MONSTER_MODEL_CHAOSCASTLE_ELF:
        LoadWaveFile(SOUND_MONSTER_ORCCAPATTACK1, L"Data\\Sound\\mOrcCapAttack1.wav", Channel,
                     Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, -1, -1, 161, 161, -1);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 20;
        break;

    case MONSTER_MODEL_CHAOSCASTLE_WIZARD:
        LoadWaveFile(SOUND_MONSTER_ORCARCHERATTACK1, L"Data\\Sound\\mOrcArcherAttack1.wav", Channel,
                     Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, -1, -1, 162, 162, -1);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 20;
        break;
    case MONSTER_MODEL_BULL_FIGHTER:
    case MONSTER_MODEL_DEATH_COW:
        LoadWaveFile(SOUND_MONSTER_BULL1, L"Data\\Sound\\mBull1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_BULL2, L"Data\\Sound\\mBull2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_BULLATTACK1, L"Data\\Sound\\mBullAttack1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_BULLATTACK2, L"Data\\Sound\\mBullAttack2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_BULLDIE, L"Data\\Sound\\mBullDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 0, 1, 2, 3, 4);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 20;
        break;
    case MONSTER_MODEL_HOUND:
        LoadWaveFile(SOUND_MONSTER_HOUND1, L"Data\\Sound\\mHound1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_HOUND2, L"Data\\Sound\\mHound2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_HOUNDATTACK1, L"Data\\Sound\\mHoundAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_HOUNDATTACK2, L"Data\\Sound\\mHoundAttack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_HOUNDDIE, L"Data\\Sound\\mHoundDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 5, 6, 7, 8, 9);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 5;
        break;
    case MONSTER_MODEL_BUDGE_DRAGON:
        LoadWaveFile(SOUND_MONSTER_BUDGE1, L"Data\\Sound\\mBudge1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_BUDGEATTACK1, L"Data\\Sound\\mBudgeAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_BUDGEDIE, L"Data\\Sound\\mBudgeDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 10, 11, 11, 11, 12);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 7;
        break;
    case MONSTER_MODEL_SPIDER:
        LoadWaveFile(SOUND_MONSTER_SPIDER1, L"Data\\Sound\\mSpider1.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 13, 13, 13, 13, 13);
        break;
    case MONSTER_MODEL_DARK_KNIGHT:
    case MONSTER_MODEL_DEATH_KNIGHT:
    case MONSTER_MODEL_TITAN:
        LoadWaveFile(SOUND_MONSTER_DARKKNIGHT1, L"Data\\Sound\\mDarkKnight1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_DARKKNIGHT2, L"Data\\Sound\\mDarkKnight2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_DARKKNIGHTATTACK1, L"Data\\Sound\\mDarkKnightAttack1.wav",
                     Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_DARKKNIGHTATTACK2, L"Data\\Sound\\mDarkKnightAttack2.wav",
                     Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_DARKKNIGHTDIE, L"Data\\Sound\\mDarkKnightDie.wav", Channel,
                     Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 15, 16, 17, 18, 19);
        if (Type == 3)
            Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 16;
        else if (Type == 29)
            Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 20;
        else
            Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 19;
        break;
    case MONSTER_MODEL_LICH:
        LoadWaveFile(SOUND_MONSTER_WIZARD1, L"Data\\Sound\\mWizard1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_WIZARD2, L"Data\\Sound\\mWizard2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_WIZARDATTACK1, L"Data\\Sound\\mWizardAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_WIZARDATTACK2, L"Data\\Sound\\mWizardAttack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_WIZARDDIE, L"Data\\Sound\\mWizardDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 20, 21, 22, 23, 24);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 20;
        break;
    case MONSTER_MODEL_GIANT:
        LoadWaveFile(SOUND_MONSTER_GIANT1, L"Data\\Sound\\mGiant1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_GIANT2, L"Data\\Sound\\mGiant2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_GIANTATTACK1, L"Data\\Sound\\mGiantAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_GIANTATTACK2, L"Data\\Sound\\mGiantAttack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_GIANTDIE, L"Data\\Sound\\mGiantDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 25, 26, 27, 28, 29);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 20;
        break;
    case MONSTER_MODEL_LARVA:
        LoadWaveFile(SOUND_MONSTER_LARVA1, L"Data\\Sound\\mLarva1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_LARVA2, L"Data\\Sound\\mLarva2.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 30, 31, 30, 31, 31);
        break;
    case MONSTER_MODEL_HELL_SPIDER:
        LoadWaveFile(SOUND_MONSTER_HELLSPIDER1, L"Data\\Sound\\mHellSpider1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_HELLSPIDERATTACK1, L"Data\\Sound\\mHellSpiderAttack1.wav",
                     Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_HELLSPIDERDIE, L"Data\\Sound\\mHellSpiderDie.wav", Channel,
                     Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 32, 33, 33, 33, 34);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 16;
        break;
    case MONSTER_MODEL_GHOST:
        LoadWaveFile(SOUND_MONSTER_GHOST1, L"Data\\Sound\\mGhost1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_GHOST2, L"Data\\Sound\\mGhost2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_GHOSTATTACK1, L"Data\\Sound\\mGhostAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_GHOSTATTACK2, L"Data\\Sound\\mGhostAttack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_GHOSTDIE, L"Data\\Sound\\mGhostDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 35, 36, 37, 38, 39);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 20;
        break;
    case MONSTER_MODEL_CYCLOPS:
        LoadWaveFile(SOUND_MONSTER_OGRE1, L"Data\\Sound\\mOgre1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_OGRE2, L"Data\\Sound\\mOgre2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_OGREATTACK1, L"Data\\Sound\\mOgreAttack1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_OGREATTACK2, L"Data\\Sound\\mOgreAttack2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_OGREDIE, L"Data\\Sound\\mOgreDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 40, 41, 42, 43, 44);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 20;
        break;
    case MONSTER_MODEL_GORGON:
        LoadWaveFile(SOUND_MONSTER_GORGON1, L"Data\\Sound\\mGorgon1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_GORGON2, L"Data\\Sound\\mGorgon2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_GORGONATTACK1, L"Data\\Sound\\mGorgonAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_GORGONATTACK2, L"Data\\Sound\\mGorgonAttack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_GORGONDIE, L"Data\\Sound\\mGorgonDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 45, 46, 47, 48, 49);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 20;
        break;
    case MONSTER_MODEL_ICE_MONSTER:
        LoadWaveFile(SOUND_MONSTER_ICEMONSTER1, L"Data\\Sound\\mIceMonster1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_ICEMONSTER2, L"Data\\Sound\\mIceMonster2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_ICEMONSTERDIE, L"Data\\Sound\\mIceMonsterDie.wav", Channel,
                     Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 50, 51, 50, 50, 52);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 19;
        break;
    case MONSTER_MODEL_WORM:
        LoadWaveFile(SOUND_MONSTER_WORM1, L"Data\\Sound\\mWorm1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_WORM2, L"Data\\Sound\\mWorm2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_WORMDIE, L"Data\\Sound\\mWormDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 53, 53, 55, 55, 55);
        break;
    case MONSTER_MODEL_HOMMERD:
        LoadWaveFile(SOUND_MONSTER_HOMORD1, L"Data\\Sound\\mHomord1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_HOMORD2, L"Data\\Sound\\mHomord2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_HOMORDATTACK1, L"Data\\Sound\\mHomordAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_HOMORDDIE, L"Data\\Sound\\mHomordDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 56, 57, 58, 58, 59);
        break;
    case MONSTER_MODEL_ICE_QUEEN:
        LoadWaveFile(SOUND_MONSTER_ICEQUEEN1, L"Data\\Sound\\mIceQueen1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_ICEQUEEN2, L"Data\\Sound\\mIceQueen2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_ICEQUEENATTACK1, L"Data\\Sound\\mIceQueenAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_ICEQUEENATTACK2, L"Data\\Sound\\mIceQueenAttack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_ICEQUEENDIE, L"Data\\Sound\\mIceQueenDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 60, 61, 62, 63, 64);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 16;
        break;
    case MONSTER_MODEL_ASSASSIN:
        LoadWaveFile(SOUND_MONSTER_ASSASSINATTACK1, L"Data\\Sound\\mAssassinAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_ASSASSINATTACK2, L"Data\\Sound\\mAssassinAttack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_ASSASSINDIE, L"Data\\Sound\\mAssassinDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, -1, -1, 65, 66, 67);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 20;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.35f;
        break;
    case MONSTER_MODEL_YETI:
    case MONSTER_MODEL_ELITE_YETI:
        LoadWaveFile(SOUND_MONSTER_YETI1, L"Data\\Sound\\mYeti1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_YETI2, L"Data\\Sound\\mYeti2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_YETIATTACK1, L"Data\\Sound\\mYetiAttack1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_YETIDIE, L"Data\\Sound\\mYetiDie.wav", Channel, Enable);
        SetMonsterSound(MODEL_YETI, 68, 68, 70, 70, 71);
        SetMonsterSound(MODEL_ELITE_YETI, 68, 69, 70, 70, 71);
        Models[MODEL_YETI].BoneHead = 20;
        Models[MODEL_ELITE_YETI].BoneHead = 20;
        break;
    case MONSTER_MODEL_GOBLIN:
        LoadWaveFile(SOUND_MONSTER_GOBLIN1, L"Data\\Sound\\mGoblin1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_GOBLIN2, L"Data\\Sound\\mGoblin2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_GOBLINATTACK1, L"Data\\Sound\\mGoblinAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_GOBLINATTACK2, L"Data\\Sound\\mGoblinAttack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_GOBLINDIE, L"Data\\Sound\\mGoblinDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 72, 73, 74, 75, 76);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 6;
        break;
    case MONSTER_MODEL_CHAIN_SCORPION:
        LoadWaveFile(SOUND_MONSTER_SCORPION1, L"Data\\Sound\\mScorpion1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_SCORPION2, L"Data\\Sound\\mScorpion2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_SCORPIONATTACK1, L"Data\\Sound\\mScorpionAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_SCORPIONATTACK2, L"Data\\Sound\\mScorpionAttack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_SCORPIONDIE, L"Data\\Sound\\mScorpionDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 77, 78, 79, 80, 81);
        break;
    case MONSTER_MODEL_BEETLE_MONSTER:
        LoadWaveFile(SOUND_MONSTER_BEETLE1, L"Data\\Sound\\mBeetle1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_BEETLEATTACK1, L"Data\\Sound\\mBeetleAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_BEETLEDIE, L"Data\\Sound\\mBeetleDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 82, 82, 83, 83, 84);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 5;
        break;
    case MONSTER_MODEL_HUNTER:
        LoadWaveFile(SOUND_MONSTER_HUNTER1, L"Data\\Sound\\mHunter1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_HUNTER2, L"Data\\Sound\\mHunter2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_HUNTERATTACK1, L"Data\\Sound\\mHunterAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_HUNTERATTACK2, L"Data\\Sound\\mHunterAttack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_HUNTERDIE, L"Data\\Sound\\mHunterDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 85, 86, 87, 88, 89);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 6;
        break;
    case MONSTER_MODEL_FOREST_MONSTER:
        LoadWaveFile(SOUND_MONSTER_WOODMON1, L"Data\\Sound\\mWoodMon1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_WOODMON2, L"Data\\Sound\\mWoodMon2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_WOODMONATTACK1, L"Data\\Sound\\mWoodMonAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_WOODMONATTACK2, L"Data\\Sound\\mWoodMonAttack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_WOODMONDIE, L"Data\\Sound\\mWoodMonDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 90, 91, 92, 93, 94);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 6;
        break;
    case MONSTER_MODEL_AGON:
        LoadWaveFile(SOUND_MONSTER_ARGON1, L"Data\\Sound\\mArgon1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_ARGON2, L"Data\\Sound\\mArgon2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_ARGONATTACK1, L"Data\\Sound\\mArgonAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_ARGONATTACK2, L"Data\\Sound\\mArgonAttack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_ARGONDIE, L"Data\\Sound\\mArgonDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 95, 96, 97, 98, 99);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 16;
        break;
    case MONSTER_MODEL_STONE_GOLEM:
        LoadWaveFile(SOUND_MONSTER_GOLEM1, L"Data\\Sound\\mGolem1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_GOLEM2, L"Data\\Sound\\mGolem2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_GOLEMATTACK1, L"Data\\Sound\\mGolemAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_GOLEMATTACK2, L"Data\\Sound\\mGolemAttack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_GOLEMDIE, L"Data\\Sound\\mGolemDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 100, 101, 102, 103, 104);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 5;
        break;
    case MONSTER_MODEL_DEVIL:
        LoadWaveFile(SOUND_MONSTER_YETI1, L"Data\\Sound\\mYeti1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_SATANATTACK1, L"Data\\Sound\\mSatanAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_YETIDIE, L"Data\\Sound\\mYetiDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 105, 105, 106, 106, 107);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 6;
        break;
    case MONSTER_MODEL_BALROG:
        LoadWaveFile(SOUND_MONSTER_BALROG1, L"Data\\Sound\\mBalrog1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_BALROG2, L"Data\\Sound\\mBalrog2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_WIZARDATTACK2, L"Data\\Sound\\mWizardAttack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_GORGONATTACK2, L"Data\\Sound\\mGorgonAttack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_BALROGDIE, L"Data\\Sound\\mBalrogDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 108, 109, 110, 111, 112);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 6;
        Models[static_cast<int>(MODEL_MONSTER01) + Type].StreamMesh = 1;
        break;
    case MONSTER_MODEL_SHADOW:
        LoadWaveFile(SOUND_MONSTER_SHADOW1, L"Data\\Sound\\mShadow1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_SHADOW2, L"Data\\Sound\\mShadow2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_SHADOWATTACK1, L"Data\\Sound\\mShadowAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_SHADOWATTACK2, L"Data\\Sound\\mShadowAttack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_SHADOWDIE, L"Data\\Sound\\mShadowDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 113, 114, 115, 116, 117);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 5;
        break;
    case MONSTER_MODEL_DRAGON:
        LoadWaveFile(SOUND_MONSTER_YETI1, L"Data\\Sound\\mYeti1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_BULLATTACK1, L"Data\\Sound\\mBullAttack1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_YETIDIE, L"Data\\Sound\\mYetiDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 123, 123, 124, 124, 125);
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.5f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.7f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.8f;
        b->Actions[MONSTER01_DIE + 1].PlaySpeed = 0.8f;
        break;
    case MONSTER_MODEL_BALI:
        LoadWaveFile(SOUND_MONSTER_BALI1, L"Data\\Sound\\mBali1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_BALI2, L"Data\\Sound\\mBali2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_BALIATTACK1, L"Data\\Sound\\mBaliAttack1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_BALIATTACK2, L"Data\\Sound\\mBaliAttack2.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 126, 127, 128, 129, 127);
        b->Actions[MONSTER01_ATTACK3].PlaySpeed = 0.4f;
        b->Actions[MONSTER01_ATTACK4].PlaySpeed = 0.4f;
        b->Actions[MONSTER01_APEAR].PlaySpeed = 0.4f;
        b->Actions[MONSTER01_RUN].PlaySpeed = 0.4f;
        b->BoneHead = 6;
        break;

    case MONSTER_MODEL_BAHAMUT:
        LoadWaveFile(SOUND_MONSTER_BAHAMUT1, L"Data\\Sound\\mBahamut1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_YETI1, L"Data\\Sound\\mYeti1.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 130, 130, 131, 131, 130);
        break;
    case MONSTER_MODEL_VEPAR:
        LoadWaveFile(SOUND_MONSTER_BEPAR1, L"Data\\Sound\\mBepar1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_BEPAR2, L"Data\\Sound\\mBepar2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_BALROG1, L"Data\\Sound\\mBalrog1.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 132, 133, 104, 104, 133);
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.5f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.5f;
        b->BoneHead = 20; //인어
        break;
    case MONSTER_MODEL_VALKYRIE:
        LoadWaveFile(SOUND_MONSTER_VALKYRIE1, L"Data\\Sound\\mValkyrie1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_BALIATTACK2, L"Data\\Sound\\mBaliAttack2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_VALKYRIEDIE, L"Data\\Sound\\mValkyrieDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 135, 135, 136, 136, 137);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 19;
        break;
    case MONSTER_MODEL_LIZARD:
    case MONSTER_MODEL_SOLDIER:
        LoadWaveFile(SOUND_MONSTER_LIZARDKING1, L"Data\\Sound\\mLizardKing1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_LIZARDKING2, L"Data\\Sound\\mLizardKing2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_GORGONDIE, L"Data\\Sound\\mGorgonDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 138, 139, 138, 139, 140);
        if (Type == 36)
            Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 19;
        else
            Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 6;
        break;
    case MONSTER_MODEL_HYDRA:
        LoadWaveFile(SOUND_MONSTER_HYDRA1, L"Data\\Sound\\mHydra1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_HYDRAATTACK1, L"Data\\Sound\\mHydraAttack1.wav", Channel,
                     Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 141, 141, 142, 142, 141);
        b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.15f;
        b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.15f;
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.2f;
        break;
    case MONSTER_MODEL_GOLDEN_WHEEL:
        LoadWaveFile(SOUND_MONSTER_IRON1, L"Data\\Sound\\iron1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_IRON_ATTACK1, L"Data\\Sound\\iron_attack1.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 143, 143, 144, 144, 144);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 3;
        break;
    case MONSTER_MODEL_TANTALLOS:
        LoadWaveFile(SOUND_MONSTER_JAIKAN1, L"Data\\Sound\\jaikan1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_JAIKAN2, L"Data\\Sound\\jaikan2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_JAIKAN_ATTACK1, L"Data\\Sound\\jaikan_attack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_JAIKAN_ATTACK2, L"Data\\Sound\\jaikan_attack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_JAIKAN_DIE, L"Data\\Sound\\jaikan_die.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 145, 146, 147, 148, 149);
        LoadBitmapW(L"Monster\\bv01_2.jpg", BITMAP_MONSTER_SKIN, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::Repeat);
        LoadBitmapW(L"Monster\\bv02_2.jpg", BITMAP_MONSTER_SKIN + 1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::Repeat);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 20;
        break;
    case MONSTER_MODEL_BLOODY_WOLF:
        LoadWaveFile(SOUND_MONSTER_BLOOD1, L"Data\\Sound\\blood1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_BLOOD_ATTACK1, L"Data\\Sound\\blood_attack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_BLOOD_ATTACK2, L"Data\\Sound\\blood_attack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_BLOOD_DIE, L"Data\\Sound\\blood_die.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 150, 150, 151, 152, 153);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 7;
        break;
    case MONSTER_MODEL_BEAM_KNIGHT:
        LoadWaveFile(SOUND_MONSTER_DEATH1, L"Data\\Sound\\death1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_DEATH_ATTACK1, L"Data\\Sound\\death_attack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_DEATH_DIE, L"Data\\Sound\\death_die.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 154, 154, 155, 155, 156);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 6;
        break;
    case MONSTER_MODEL_MUTANT:
        LoadWaveFile(SOUND_MONSTER_UTANT1, L"Data\\Sound\\mutant1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_UTANT2, L"Data\\Sound\\mutant2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_UTANT_ATTACK1, L"Data\\Sound\\mutant_attack1.wav", Channel,
                     Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 157, 158, 159, 159, 159);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 6;
        break;
    case MONSTER_MODEL_ORC_ARCHER:
        LoadWaveFile(SOUND_MONSTER_ORCARCHERATTACK1, L"Data\\Sound\\mOrcArcherAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_BULLDIE, L"Data\\Sound\\mBullDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, -1, -1, 162, 162, 4);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 7;
        break;
    case MONSTER_MODEL_ORC:
        LoadWaveFile(SOUND_MONSTER_HUNTER2, L"Data\\Sound\\mHunter2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_BULLDIE, L"Data\\Sound\\mBullDie.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_ORCCAPATTACK1, L"Data\\Sound\\mOrcCapAttack1.wav", Channel,
                     Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 86, 86, 161, 161, 4);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 20;
        break;
    case MONSTER_MODEL_CURSED_KING:
        LoadWaveFile(SOUND_MONSTER_CURSEDKING1, L"Data\\Sound\\mCursedKing1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_CURSEDKING2, L"Data\\Sound\\mCursedKing2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_CURSEDKINGDIE1, L"Data\\Sound\\mCursedKingDie1.wav", Channel,
                     Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 160, 164, -1, -1, 163);
        Models[static_cast<int>(MODEL_MONSTER01) + Type].BoneHead = 20;
        break;
    case MONSTER_MODEL_CRUST:
        LoadBitmapW(L"Monster\\iui02.tga", BITMAP_ROBE + 3);
        LoadBitmapW(L"Monster\\iui03.tga", BITMAP_ROBE + 5);
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.22f;
        LoadWaveFile(SOUND_MONSTER_MEGACRUST1, L"Data\\Sound\\mMegaCrust1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_MEGACRUSTATTACK1, L"Data\\Sound\\mMegaCrustAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_MEGACRUSTDIE, L"Data\\Sound\\mMegaCrustDie.wav", Channel,
                     Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 180, 180, 181, 181, 182);
        break;
    case MONSTER_MODEL_MOLT:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.22f;
        LoadWaveFile(SOUND_MONSTER_MOLT1, L"Data\\Sound\\mMolt1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_MOLTATTACK1, L"Data\\Sound\\mMoltAttack1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_MOLTDIE, L"Data\\Sound\\mMoltDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 177, 177, 178, 178, 179);
        break;
    case MONSTER_MODEL_ALQUAMOS:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.22f;
        //LoadWaveFile(SOUND_MONSTER+174,"Data\\Sound\\mAlquamos1.wav"    ,Channel,Enable);
        LoadWaveFile(SOUND_MONSTER_ALQUAMOSATTACK1, L"Data\\Sound\\mAlquamosAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_ALQUAMOSDIE, L"Data\\Sound\\mAlquamosDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 175, 175, 175, 175, 176);
        break;
    case MONSTER_MODEL_QUEEN_RAINER:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.22f;
        LoadWaveFile(SOUND_MONSTER_RAINNER1, L"Data\\Sound\\mRainner1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_RAINNERATTACK1, L"Data\\Sound\\mRainnerAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_RAINNERDIE, L"Data\\Sound\\mRainnerDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 171, -1, 172, 172, 173);
        break;
    case MONSTER_MODEL_PHANTOM_KNIGHT:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.22f;
        LoadWaveFile(SOUND_MONSTER_PHANTOM1, L"Data\\Sound\\mPhantom1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_PHANTOMATTACK1, L"Data\\Sound\\mPhantomAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_PHANTOMDIE, L"Data\\Sound\\mPhantomDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 168, 168, 169, 169, 170);
        break;
    case MONSTER_MODEL_DRAKAN:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.22f;
        LoadWaveFile(SOUND_MONSTER_DRAKAN1, L"Data\\Sound\\mDrakan1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_DRAKANATTACK1, L"Data\\Sound\\mDrakanAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_DRAKANDIE, L"Data\\Sound\\mDrakanDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 165, 165, 166, 166, 167);
        break;
    case MONSTER_MODEL_DARK_PHOENIX_SHIELD:
        LoadWaveFile(SOUND_MONSTER_PHOENIX1, L"Data\\Sound\\mPhoenix1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_PHOENIX1, L"Data\\Sound\\mPhoenix1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_PHOENIXATTACK1, L"Data\\Sound\\mPhoenixAttack1.wav", Channel,
                     Enable);
        //LoadWaveFile(SOUND_MONSTER+186,"Data\\Sound\\mDarkPhoenixDie.wav"    ,Channel,Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 183, 184, 185, 185, -1);
    case MONSTER_MODEL_DARK_PHOENIX:
        b->Actions[MONSTER01_DIE].PlaySpeed = 0.22f;
        //b->Actions[MONSTER01_ATTACK1].PlaySpeed = 0.01f;
        //b->Actions[MONSTER01_ATTACK2].PlaySpeed = 0.01f;
        break;
    case MONSTER_MODEL_MAGIC_SKELETON:
        LoadWaveFile(SOUND_MONSTER_MAGICSKULL1, L"Data\\Sound\\mMagicSkull.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_MAGICSKULL2, L"Data\\Sound\\mMagicSkull.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 186, -1, -1, -1, 187);
        break;
    case MONSTER_MODEL_CASTLE_GATE:
        break;
    case MONSTER_MODEL_STATUE_OF_SAINT:
        break;
    case MONSTER_MODEL_DARK_SKULL_SOLDIER:
        LoadWaveFile(SOUND_MONSTER_BULLDIE, L"Data\\Sound\\mBullDie.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_HUNTER2, L"Data\\Sound\\mHunter2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_BLACKSKULLDIE, L"Data\\Sound\\mBlackSkullDie.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_BLACKSKULLATTACK, L"Data\\Sound\\mBlackSkullAttack.wav", Channel,
                     Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 188, -1, 190, -1, 189);
        break;
    case MONSTER_MODEL_GIANT_OGRE:
        LoadWaveFile(SOUND_MONSTER_HUNTER2, L"Data\\Sound\\mHunter2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_GHAINTORGERDIE, L"Data\\Sound\\mGhaintOrgerDie.wav", Channel,
                     Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 188, -1, 190, -1, 191);
        break;
    case MONSTER_MODEL_RED_SKELETON_KNIGHT:
        LoadWaveFile(SOUND_MONSTER_REDSKULL, L"Data\\Sound\\mRedSkull.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_REDSKULLDIE, L"Data\\Sound\\mRedSkullDie.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_REDSKULLATTACK, L"Data\\Sound\\mRedSkullAttack.wav", Channel,
                     Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 192, -1, 194, -1, 193);
        break;
    case MONSTER_MODEL_DEATH_ANGEL:
        LoadWaveFile(SOUND_MONSTER_DANGELIDLE, L"Data\\Sound\\mDAngelIdle.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_DANGELATTACK, L"Data\\Sound\\mDAngelAttack.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_DANGELDEATH, L"Data\\Sound\\mDAngelDeath.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 195, 195, 196, 196, 197);
        break;
    case MONSTER_MODEL_ILLUSION_OF_KUNDUN:
        LoadWaveFile(SOUND_MONSTER_OCDOORDIS, L"Data\\Sound\\mKundunIdle.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_BOWMERCATTACK, L"Data\\Sound\\mKundunAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_BOWMERCDEATH, L"Data\\Sound\\mKundunAttack2.wav", Channel,
                     Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 232, 232, 233, 234, -1);
        break;
    case MONSTER_MODEL_BLOOD_SOLDIER:
        LoadWaveFile(SOUND_MONSTER_BSOLDIERIDLE1, L"Data\\Sound\\mBSoldierIdle1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_BSOLDIERIDLE2, L"Data\\Sound\\mBSoldierIdle2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_BSOLDIERATTACK1, L"Data\\Sound\\mBSoldierAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_BSOLDIERATTACK2, L"Data\\Sound\\mBSoldierAttack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_BSOLDIERDEATH, L"Data\\Sound\\mBSoldierDeath.wav", Channel,
                     Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 198, 199, 200, 201, 202);
        break;
    case MONSTER_MODEL_AEGIS:
        LoadWaveFile(SOUND_MONSTER_ESISIDLE, L"Data\\Sound\\mEsisIdle.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_ESISATTACK1, L"Data\\Sound\\mEsisAttack1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_ESISATTACK2, L"Data\\Sound\\mEsisAttack2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_ESISDEATH, L"Data\\Sound\\mEsisDeath.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 203, 203, 204, 205, 206);
        break;
    case MONSTER_MODEL_DEATH_CENTURION:
        LoadWaveFile(SOUND_MONSTER_DSIDLE1, L"Data\\Sound\\mDsIdle1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_DSIDLE2, L"Data\\Sound\\mDsIdle2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_DSATTACK1, L"Data\\Sound\\mDsAttack1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_DSATTACK2, L"Data\\Sound\\mDsAttack2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_DSDEATH, L"Data\\Sound\\mDsDeath.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_LSIDLE1, L"Data\\Sound\\mLsIdle1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_LSIDLE2, L"Data\\Sound\\mLsIdle2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_LSATTACK1, L"Data\\Sound\\mLsAttack1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_LSATTACK2, L"Data\\Sound\\mLsAttack2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_LSDEATH, L"Data\\Sound\\mLsDeath.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 207, 208, 209, 210, 211, 212, 213,
                        214, 215, 216);
        break;
    case MONSTER_MODEL_NECRON:
        LoadWaveFile(SOUND_MONSTER_NECRONIDLE1, L"Data\\Sound\\mNecronIdle1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_NECRONIDLE2, L"Data\\Sound\\mNecronIdle2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_NECRONATTACK1, L"Data\\Sound\\mNecronAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_NECRONATTACK2, L"Data\\Sound\\mNecronAttack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_NECRONDEATH, L"Data\\Sound\\mNecronDeath.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 217, 218, 219, 220, 221);
        break;
    case MONSTER_MODEL_SHRIKER:
        LoadWaveFile(SOUND_MONSTER_SVIDLE1, L"Data\\Sound\\mSvIdle1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_SVIDLE2, L"Data\\Sound\\mSvIdle2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_SVATTACK1, L"Data\\Sound\\mSvAttack1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_SVATTACK2, L"Data\\Sound\\mSvAttack2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_SVDEATH, L"Data\\Sound\\mSvDeath.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_LVIDLE1, L"Data\\Sound\\mLvIdle1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_LVIDLE2, L"Data\\Sound\\mLvIdle2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_LVATTACK1, L"Data\\Sound\\mLvAttack1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_LVATTACK2, L"Data\\Sound\\mLvAttack2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_LVDEATH, L"Data\\Sound\\mLvDeath.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 222, 223, 224, 225, 226, 227, 228,
                        229, 230, 231);
        break;
    case MONSTER_MODEL_CASTLE_GATE1:
        LoadWaveFile(SOUND_MONSTER_OCDOORDIS, L"Data\\Sound\\BattleCastle\\oCDoorDis.wav", Channel,
                     Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, -1, -1, -1, -1, 232);
        break;
    case MONSTER_MODEL_LIFE_STONE:
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, -1, -1, -1, -1, -1);
        b->Actions[MONSTER01_STOP1].PlaySpeed = 0.05f;
        b->Actions[MONSTER01_STOP2].PlaySpeed = 0.05f;
        b->Actions[MONSTER01_WALK].PlaySpeed = 0.1f;
        break;
    case MONSTER_MODEL_BATTLE_GUARD1:
        LoadWaveFile(SOUND_MONSTER_BOWMERCATTACK, L"Data\\Sound\\BattleCastle\\mBowMercAttack.wav",
                     Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_BOWMERCDEATH, L"Data\\Sound\\BattleCastle\\mBowMercDeath.wav",
                     Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, -1, -1, 233, 233, 234);
        break;
    case MONSTER_MODEL_BATTLE_GUARD2:
        LoadWaveFile(SOUND_MONSTER_SPEARMERCATTACK,
                     L"Data\\Sound\\BattleCastle\\mSpearMercAttack.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_SPEARMERCDEATH,
                     L"Data\\Sound\\BattleCastle\\mSpearMercDeath.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, -1, -1, 235, 235, 236);
        break;
    case MONSTER_MODEL_CANON_TOWER:
        break;
    case MONSTER_MODEL_RABBIT:
        LoadWaveFile(SOUND_ELBELAND_RABBITSTRANGE_ATTACK01,
                     L"Data\\Sound\\w52\\SE_Mon_rabbitstrange_attack01.wav", 1);
        LoadWaveFile(SOUND_ELBELAND_RABBITSTRANGE_DEATH01,
                     L"Data\\Sound\\w52\\SE_Mon_rabbitstrange_death01.wav", 1);
        break;
    case MONSTER_MODEL_BUTTERFLY:
        LoadWaveFile(SOUND_ELBELAND_RABBITUGLY_BREATH01,
                     L"Data\\Sound\\w52\\SE_Mon_rabbitugly_breath01.wav", 1);
        LoadWaveFile(SOUND_ELBELAND_RABBITUGLY_ATTACK01,
                     L"Data\\Sound\\w52\\SE_Mon_rabbitugly_attack01.wav", 1);
        LoadWaveFile(SOUND_ELBELAND_RABBITUGLY_DEATH01,
                     L"Data\\Sound\\w52\\SE_Mon_rabbitugly_death01.wav", 1);
        break;
    case MONSTER_MODEL_HIDEOUS_RABBIT:
        LoadWaveFile(SOUND_ELBELAND_WOLFHUMAN_MOVE02,
                     L"Data\\Sound\\w52\\SE_Mon_wolfhuman_move02.wav", 1);
        LoadWaveFile(SOUND_ELBELAND_WOLFHUMAN_ATTACK01,
                     L"Data\\Sound\\w52\\SE_Mon_wolfhuman_attack01.wav", 1);
        LoadWaveFile(SOUND_ELBELAND_WOLFHUMAN_DEATH01,
                     L"Data\\Sound\\w52\\SE_Mon_wolfhuman_death01.wav", 1);
        break;
    case MONSTER_MODEL_WEREWOLF2:
        LoadWaveFile(SOUND_ELBELAND_BUTTERFLYPOLLUTION_MOVE01,
                     L"Data\\Sound\\w52\\SE_Mon_butterflypollution_move01.wav", 1);
        LoadWaveFile(SOUND_ELBELAND_BUTTERFLYPOLLUTION_DEATH01,
                     L"Data\\Sound\\w52\\SE_Mon_butterflypollution_death01.wav", 1);
        break;
    case MONSTER_MODEL_CURSED_LICH:
        LoadWaveFile(SOUND_ELBELAND_CURSERICH_MOVE01,
                     L"Data\\Sound\\w52\\SE_Mon_curserich_move01.wav", 1);
        LoadWaveFile(SOUND_ELBELAND_CURSERICH_ATTACK01,
                     L"Data\\Sound\\w52\\SE_Mon_curserich_attack01.wav", 1);
        LoadWaveFile(SOUND_ELBELAND_CURSERICH_DEATH01,
                     L"Data\\Sound\\w52\\SE_Mon_curserich_death01.wav", 1);
        break;
    case MONSTER_MODEL_TOTEM_GOLEM:
        LoadWaveFile(SOUND_ELBELAND_TOTEMGOLEM_MOVE01,
                     L"Data\\Sound\\w52\\SE_Mon_totemgolem_move01.wav", 1);
        LoadWaveFile(SOUND_ELBELAND_TOTEMGOLEM_MOVE02,
                     L"Data\\Sound\\w52\\SE_Mon_totemgolem_move02.wav", 1);
        LoadWaveFile(SOUND_ELBELAND_TOTEMGOLEM_ATTACK01,
                     L"Data\\Sound\\w52\\SE_Mon_totemgolem_attack01.wav", 1);
        LoadWaveFile(SOUND_ELBELAND_TOTEMGOLEM_ATTACK02,
                     L"Data\\Sound\\w52\\SE_Mon_totemgolem_attack02.wav", 1);
        LoadWaveFile(SOUND_ELBELAND_TOTEMGOLEM_DEATH01,
                     L"Data\\Sound\\w52\\SE_Mon_totemgolem_death01.wav", 1);
        break;
    case MONSTER_MODEL_GRIZZLY:
        LoadWaveFile(SOUND_ELBELAND_BEASTWOO_MOVE01,
                     L"Data\\Sound\\w52\\SE_Mon_beastwoo_move01.wav", 1);
        LoadWaveFile(SOUND_ELBELAND_BEASTWOO_ATTACK01,
                     L"Data\\Sound\\w52\\SE_Mon_beastwoo_attack01.wav", 1);
        LoadWaveFile(SOUND_ELBELAND_BEASTWOO_DEATH01,
                     L"Data\\Sound\\w52\\SE_Mon_beastwoo_death01.wav", 1);
        break;
    case MONSTER_MODEL_CAPTAIN_GRIZZLY:
        LoadWaveFile(SOUND_ELBELAND_BEASTWOOLEADER_MOVE01,
                     L"Data\\Sound\\w52\\SE_Mon_beastwooleader_move01.wav", 1);
        LoadWaveFile(SOUND_ELBELAND_BEASTWOOLEADER_ATTACK01,
                     L"Data\\Sound\\w52\\SE_Mon_beastwooleader_attack01.wav", 1);
        LoadWaveFile(SOUND_ELBELAND_BEASTWOO_DEATH01,
                     L"Data\\Sound\\w52\\SE_Mon_beastwoo_death01.wav", 1);
        break;
    case MONSTER_MODEL_SAPIUNUS:
        LoadWaveFile(SOUND_SWAMPOFQUIET_SAPI_UNUS_ATTACK01, L"Data\\Sound\\w57\\Sapi-Attack.wav",
                     1);
        LoadWaveFile(SOUND_SWAMPOFQUIET_SAPI_DEATH01, L"Data\\Sound\\w57\\Sapi-Death.wav", 1);
        break;
    case MONSTER_MODEL_SAPIDUO:
        LoadWaveFile(SOUND_SWAMPOFQUIET_SAPI_UNUS_ATTACK01, L"Data\\Sound\\w57\\Sapi-Attack.wav",
                     1);
        LoadWaveFile(SOUND_SWAMPOFQUIET_SAPI_DEATH01, L"Data\\Sound\\w57\\Sapi-Death.wav", 1);
        break;
    case MONSTER_MODEL_SAPITRES:
        LoadWaveFile(SOUND_SWAMPOFQUIET_SAPI_TRES_ATTACK01, L"Data\\Sound\\w57\\Sapi-Attack1.wav",
                     1);
        LoadWaveFile(SOUND_SWAMPOFQUIET_SAPI_DEATH01, L"Data\\Sound\\w57\\Sapi-Death.wav", 1);
        break;
    case MONSTER_MODEL_SHADOW_PAWN:
        LoadWaveFile(SOUND_SWAMPOFQUIET_SHADOW_PAWN_ATTACK01,
                     L"Data\\Sound\\w57\\ShadowPawn-Attack.wav", 1);
        LoadWaveFile(SOUND_SWAMPOFQUIET_SHADOW_DEATH01, L"Data\\Sound\\w57\\Shadow-Death.wav", 1);
        break;
    case MONSTER_MODEL_SHADOW_KNIGHT:
        LoadWaveFile(SOUND_SWAMPOFQUIET_SHADOW_KNIGHT_ATTACK01,
                     L"Data\\Sound\\w57\\ShadowKnight-Attack.wav", 1);
        LoadWaveFile(SOUND_SWAMPOFQUIET_SHADOW_DEATH01, L"Data\\Sound\\w57\\Shadow-Death.wav", 1);
        break;
    case MONSTER_MODEL_SHADOW_LOOK:
        LoadWaveFile(SOUND_SWAMPOFQUIET_SHADOW_ROOK_ATTACK01,
                     L"Data\\Sound\\w57\\ShadowRook-Attack.wav", 1);
        LoadWaveFile(SOUND_SWAMPOFQUIET_SHADOW_DEATH01, L"Data\\Sound\\w57\\Shadow-Death.wav", 1);
        break;
    case MONSTER_MODEL_NAPIN:
        LoadWaveFile(SOUND_SWAMPOFQUIET_THUNDER_NAIPIN_BREATH01,
                     L"Data\\Sound\\w57\\Naipin-Thunder.wav", 1);
        LoadWaveFile(SOUND_SWAMPOFQUIET_NAIPIN_ATTACK01, L"Data\\Sound\\w57\\Naipin-Attack.wav", 1);
        LoadWaveFile(SOUND_SWAMPOFQUIET_NAIPIN_DEATH01, L"Data\\Sound\\w57\\Naipin-Death.wav", 1);
        break;
    case MONSTER_MODEL_GHOST_NAPIN:
        LoadWaveFile(SOUND_SWAMPOFQUIET_GHOST_NAIPIN_BREATH01,
                     L"Data\\Sound\\w57\\Naipin-Ghost.wav", 1);
        LoadWaveFile(SOUND_SWAMPOFQUIET_NAIPIN_ATTACK01, L"Data\\Sound\\w57\\Naipin-Attack.wav", 1);
        LoadWaveFile(SOUND_SWAMPOFQUIET_NAIPIN_DEATH01, L"Data\\Sound\\w57\\Naipin-Death.wav", 1);
        break;
    case MONSTER_MODEL_BLAZE_NAPIN:
        LoadWaveFile(SOUND_SWAMPOFQUIET_BLAZE_NAIPIN_BREATH01,
                     L"Data\\Sound\\w57\\Naipin-Blaze.wav", 1);
        LoadWaveFile(SOUND_SWAMPOFQUIET_NAIPIN_ATTACK01, L"Data\\Sound\\w57\\Naipin-Attack.wav", 1);
        LoadWaveFile(SOUND_SWAMPOFQUIET_NAIPIN_DEATH01, L"Data\\Sound\\w57\\Naipin-Death.wav", 1);
        break;
    case MONSTER_MODEL_ICE_WALKER:
        LoadWaveFile(SOUND_ELBELAND_WOLFHUMAN_DEATH01,
                     L"Data\\Sound\\w52\\SE_Mon_wolfhuman_death01.wav", 1);
        LoadWaveFile(SOUND_RAKLION_ICEWALKER_ATTACK, L"Data\\Sound\\w58w59\\IceWalker_attack.wav",
                     1);
        LoadWaveFile(SOUND_RAKLION_ICEWALKER_MOVE, L"Data\\Sound\\w58w59\\IceWalker_move.wav", 1);
        break;
    case MONSTER_MODEL_GIANT_MAMMOTH:
        LoadWaveFile(SOUND_RAKLION_GIANT_MAMUD_MOVE, L"Data\\Sound\\w58w59\\GiantMammoth_move.wav",
                     1);
        LoadWaveFile(SOUND_RAKLION_GIANT_MAMUD_ATTACK,
                     L"Data\\Sound\\w58w59\\GiantMammoth_attack.wav", 1);
        LoadWaveFile(SOUND_RAKLION_GIANT_MAMUD_DEATH,
                     L"Data\\Sound\\w58w59\\GiantMammoth_death.wav", 1);
        break;
    case MONSTER_MODEL_ICE_GIANT:
        LoadWaveFile(SOUND_RAKLION_ICEGIANT_MOVE, L"Data\\Sound\\w58w59\\IceGiant_move.wav", 1);
        LoadWaveFile(SOUND_RAKLION_ICEGIANT_DEATH, L"Data\\Sound\\w58w59\\IceGiant_death.wav", 1);
        break;
    case MONSTER_MODEL_COOLUTIN:
        // LoadWaveFile(SOUND_MONSTER_HELLSPIDERDIE, L"Data\\Sound\\m헬스파이더죽기.wav", 1);
        LoadWaveFile(SOUND_RAKLION_COOLERTIN_ATTACK, L"Data\\Sound\\w58w59\\Coolertin_attack.wav",
                     1);
        LoadWaveFile(SOUND_RAKLION_COOLERTIN_MOVE, L"Data\\Sound\\w58w59\\Coolertin_move.wav", 1);
        break;
    case MONSTER_MODEL_IRON_KNIGHT:
        LoadWaveFile(SOUND_RAKLION_IRON_KNIGHT_MOVE, L"Data\\Sound\\w58w59\\IronKnight_move.wav",
                     1);
        LoadWaveFile(SOUND_RAKLION_IRON_KNIGHT_ATTACK,
                     L"Data\\Sound\\w58w59\\IronKnight_attack.wav", 1);
        LoadWaveFile(SOUND_MONSTER_DEATH1, L"Data\\Sound\\death1.wav", 1);
        break;
    case MONSTER_MODEL_SELUPAN:
        LoadWaveFile(SOUND_RAKLION_SERUFAN_ATTACK1, L"Data\\Sound\\w58w59\\Selupan_attack1.wav", 1);
        LoadWaveFile(SOUND_RAKLION_SERUFAN_ATTACK2, L"Data\\Sound\\w58w59\\Selupan_attack2.wav", 1);
        LoadWaveFile(SOUND_RAKLION_SERUFAN_CURE, L"Data\\Sound\\w58w59\\Selupan_cure.wav", 1);
        LoadWaveFile(SOUND_RAKLION_SERUFAN_RAGE, L"Data\\Sound\\w58w59\\Selupan_rage.wav", 1);
        LoadWaveFile(SOUND_RAKLION_SERUFAN_WORD1, L"Data\\Sound\\w58w59\\Selupan_word1.wav", 1);
        LoadWaveFile(SOUND_RAKLION_SERUFAN_WORD2, L"Data\\Sound\\w58w59\\Selupan_word2.wav", 1);
        LoadWaveFile(SOUND_RAKLION_SERUFAN_WORD3, L"Data\\Sound\\w58w59\\Selupan_word3.wav", 1);
        LoadWaveFile(SOUND_RAKLION_SERUFAN_WORD4, L"Data\\Sound\\w58w59\\Selupan_word4.wav", 1);
        break;
    case MONSTER_MODEL_CURSED_SANTA:
        LoadWaveFile(SOUND_XMAS_SANTA_IDLE_1, L"Data\\Sound\\xmas\\DarkSanta_Idle01.wav");
        LoadWaveFile(SOUND_XMAS_SANTA_IDLE_2, L"Data\\Sound\\xmas\\DarkSanta_Idle02.wav");
        LoadWaveFile(SOUND_XMAS_SANTA_WALK_1, L"Data\\Sound\\xmas\\DarkSanta_Walk01.wav");
        LoadWaveFile(SOUND_XMAS_SANTA_WALK_2, L"Data\\Sound\\xmas\\DarkSanta_Walk02.wav");
        LoadWaveFile(SOUND_XMAS_SANTA_ATTACK_1, L"Data\\Sound\\xmas\\DarkSanta_Attack01.wav");
        LoadWaveFile(SOUND_XMAS_SANTA_DAMAGE_1, L"Data\\Sound\\xmas\\DarkSanta_Damage01.wav");
        LoadWaveFile(SOUND_XMAS_SANTA_DAMAGE_2, L"Data\\Sound\\xmas\\DarkSanta_Damage02.wav");
        LoadWaveFile(SOUND_XMAS_SANTA_DEATH_1, L"Data\\Sound\\xmas\\DarkSanta_Death01.wav");
        break;
    case MONSTER_MODEL_EVIL_GOBLIN:
        LoadWaveFile(SOUND_MONSTER_GOBLIN1, L"Data\\Sound\\mGoblin1.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_GOBLIN2, L"Data\\Sound\\mGoblin2.wav", Channel, Enable);
        LoadWaveFile(SOUND_MONSTER_GOBLINATTACK1, L"Data\\Sound\\mGoblinAttack1.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_GOBLINATTACK2, L"Data\\Sound\\mGoblinAttack2.wav", Channel,
                     Enable);
        LoadWaveFile(SOUND_MONSTER_GOBLINDIE, L"Data\\Sound\\mGoblinDie.wav", Channel, Enable);
        SetMonsterSound(static_cast<int>(MODEL_MONSTER01) + Type, 72, 73, 74, 75, 76);
        break;
    case MONSTER_MODEL_ZOMBIE_FIGHTER: {
        LoadWaveFile(SOUND_PKFIELD_ZOMBIEWARRIOR_ATTACK,
                     L"Data\\Sound\\w64\\ZombieWarrior_attack.wav");
        LoadWaveFile(SOUND_PKFIELD_ZOMBIEWARRIOR_DAMAGE01,
                     L"Data\\Sound\\w64\\ZombieWarrior_damage01.wav");
        LoadWaveFile(SOUND_PKFIELD_ZOMBIEWARRIOR_DAMAGE02,
                     L"Data\\Sound\\w64\\ZombieWarrior_damage02.wav");
        LoadWaveFile(SOUND_PKFIELD_ZOMBIEWARRIOR_DEATH,
                     L"Data\\Sound\\w64\\ZombieWarrior_death.wav");
        LoadWaveFile(SOUND_PKFIELD_ZOMBIEWARRIOR_MOVE01,
                     L"Data\\Sound\\w64\\ZombieWarrior_move01.wav");
        LoadWaveFile(SOUND_PKFIELD_ZOMBIEWARRIOR_MOVE02,
                     L"Data\\Sound\\w64\\ZombieWarrior_move02.wav");
    }
    break;
    case MONSTER_MODEL_GLADIATOR: {
        LoadWaveFile(SOUND_PKFIELD_RAISEDGLADIATOR_ATTACK,
                     L"Data\\Sound\\w64\\RaisedGladiator_attack.wav");
        LoadWaveFile(SOUND_PKFIELD_RAISEDGLADIATOR_DAMAGE01,
                     L"Data\\Sound\\w64\\RaisedGladiator_damage01.wav");
        LoadWaveFile(SOUND_PKFIELD_RAISEDGLADIATOR_DAMAGE02,
                     L"Data\\Sound\\w64\\RaisedGladiator_damage02.wav");
        LoadWaveFile(SOUND_PKFIELD_RAISEDGLADIATOR_DEATH,
                     L"Data\\Sound\\w64\\RaisedGladiator_death.wav");
        LoadWaveFile(SOUND_PKFIELD_RAISEDGLADIATOR_MOVE01,
                     L"Data\\Sound\\w64\\RaisedGladiator_move01.wav");
        LoadWaveFile(SOUND_PKFIELD_RAISEDGLADIATOR_MOVE02,
                     L"Data\\Sound\\w64\\RaisedGladiator_move02.wav");
    }
    break;
    case MONSTER_MODEL_SLAUGTHERER: {
        LoadWaveFile(SOUND_PKFIELD_ASHESBUTCHER_ATTACK,
                     L"Data\\Sound\\w64\\AshesButcher_attack.wav");
        LoadWaveFile(SOUND_PKFIELD_ASHESBUTCHER_DAMAGE01,
                     L"Data\\Sound\\w64\\AshesButcher_damage01.wav");
        LoadWaveFile(SOUND_PKFIELD_ASHESBUTCHER_DAMAGE02,
                     L"Data\\Sound\\w64\\AshesButcher_damage02.wav");
        LoadWaveFile(SOUND_PKFIELD_ASHESBUTCHER_DEATH, L"Data\\Sound\\w64\\AshesButcher_death.wav");
        LoadWaveFile(SOUND_PKFIELD_ASHESBUTCHER_MOVE01,
                     L"Data\\Sound\\w64\\AshesButcher_move01.wav");
        LoadWaveFile(SOUND_PKFIELD_ASHESBUTCHER_MOVE02,
                     L"Data\\Sound\\w64\\AshesButcher_move02.wav");
    }
    break;
    case MONSTER_MODEL_BLOOD_ASSASSIN: {
        LoadWaveFile(SOUND_PKFIELD_BLOODASSASSIN_ATTACK,
                     L"Data\\Sound\\w64\\BloodAssassin_attack.wav");
        LoadWaveFile(SOUND_PKFIELD_BLOODASSASSIN_DAMAGE01,
                     L"Data\\Sound\\w64\\BloodAssassin_damage01.wav");
        LoadWaveFile(SOUND_PKFIELD_BLOODASSASSIN_DAMAGE02,
                     L"Data\\Sound\\w64\\BloodAssassin_damage02.wav");
        LoadWaveFile(SOUND_PKFIELD_BLOODASSASSIN_DEDTH,
                     L"Data\\Sound\\w64\\BloodAssassin_death.wav");
        LoadWaveFile(SOUND_PKFIELD_BLOODASSASSIN_MOVE01,
                     L"Data\\Sound\\w64\\BloodAssassin_move01.wav");
        LoadWaveFile(SOUND_PKFIELD_BLOODASSASSIN_MOVE02,
                     L"Data\\Sound\\w64\\BloodAssassin_move02.wav");
    }
    break;
    case MONSTER_MODEL_LAVA_GIANT: {
        LoadWaveFile(SOUND_PKFIELD_BURNINGLAVAGOLEM_ATTACK01,
                     L"Data\\Sound\\w64\\BurningLavaGolem_attack01.wav");
        LoadWaveFile(SOUND_PKFIELD_BURNINGLAVAGOLEM_ATTACK02,
                     L"Data\\Sound\\w64\\BurningLavaGolem_attack02.wav");
        LoadWaveFile(SOUND_PKFIELD_BURNINGLAVAGOLEM_DAMAGE01,
                     L"Data\\Sound\\w64\\BurningLavaGolem_damage01.wav");
        LoadWaveFile(SOUND_PKFIELD_BURNINGLAVAGOLEM_DAMAGE02,
                     L"Data\\Sound\\w64\\BurningLavaGolem_damage02.wav");
        LoadWaveFile(SOUND_PKFIELD_BURNINGLAVAGOLEM_DEATH,
                     L"Data\\Sound\\w64\\BurningLavaGolem_death.wav");
        LoadWaveFile(SOUND_PKFIELD_BURNINGLAVAGOLEM_MOVE01,
                     L"Data\\Sound\\w64\\BurningLavaGolem_move01.wav");
        LoadWaveFile(SOUND_PKFIELD_BURNINGLAVAGOLEM_MOVE02,
                     L"Data\\Sound\\w64\\BurningLavaGolem_move02.wav");
    }
    break;
    case MONSTER_MODEL_TERRIBLE_BUTCHER: {
        LoadWaveFile(SOUND_DOPPELGANGER_RED_BUGBEAR_ATTACK,
                     L"Data\\Sound\\Doppelganger\\Angerbutcher_attack.wav");
        LoadWaveFile(SOUND_DOPPELGANGER_RED_BUGBEAR_DEATH,
                     L"Data\\Sound\\Doppelganger\\Angerbutcher_death.wav");
    }
    break;
    case MONSTER_MODEL_MAD_BUTCHER: {
        LoadWaveFile(SOUND_DOPPELGANGER_BUGBEAR_ATTACK,
                     L"Data\\Sound\\Doppelganger\\Butcher_attack.wav");
        LoadWaveFile(SOUND_DOPPELGANGER_BUGBEAR_DEATH,
                     L"Data\\Sound\\Doppelganger\\Butcher_death.wav");
    }
    break;
    case MONSTER_MODEL_DOPPELGANGER: {
        LoadWaveFile(SOUND_DOPPELGANGER_SLIME_ATTACK,
                     L"Data\\Sound\\Doppelganger\\Doppelganger_attack.wav");
        LoadWaveFile(SOUND_DOPPELGANGER_SLIME_DEATH,
                     L"Data\\Sound\\Doppelganger\\Doppelganger_death.wav");
    }
    break;
#ifdef ASG_ADD_KARUTAN_MONSTERS
    case MONSTER_MODEL_VENOMOUS_CHAIN_SCORPION:
        LoadWaveFile(SOUND_KARUTAN_TCSCORPION_ATTACK,
                     L"Data\\Sound\\Karutan\\ToxyChainScorpion_attack.wav");
        LoadWaveFile(SOUND_KARUTAN_TCSCORPION_DEATH,
                     L"Data\\Sound\\Karutan\\ToxyChainScorpion_death.wav");
        LoadWaveFile(SOUND_KARUTAN_TCSCORPION_HIT,
                     L"Data\\Sound\\Karutan\\ToxyChainScorpion_hit.wav");
        break;
    case MONSTER_MODEL_BONE_SCORPION:
        LoadWaveFile(SOUND_KARUTAN_BONESCORPION_ATTACK,
                     L"Data\\Sound\\Karutan\\BoneScorpion_attack.wav");
        LoadWaveFile(SOUND_KARUTAN_BONESCORPION_DEATH,
                     L"Data\\Sound\\Karutan\\BoneScorpion_death.wav");
        LoadWaveFile(SOUND_KARUTAN_BONESCORPION_HIT, L"Data\\Sound\\Karutan\\BoneScorpion_hit.wav");
        break;
    case MONSTER_MODEL_ORCUS:
        LoadWaveFile(SOUND_KARUTAN_ORCUS_MOVE1, L"Data\\Sound\\Karutan\\Orcus_move1.wav");
        LoadWaveFile(SOUND_KARUTAN_ORCUS_MOVE2, L"Data\\Sound\\Karutan\\Orcus_move2.wav");
        LoadWaveFile(SOUND_KARUTAN_ORCUS_ATTACK1, L"Data\\Sound\\Karutan\\Orcus_attack_1.wav");
        LoadWaveFile(SOUND_KARUTAN_ORCUS_ATTACK2, L"Data\\Sound\\Karutan\\Orcus_attack_2.wav");
        LoadWaveFile(SOUND_KARUTAN_ORCUS_DEATH, L"Data\\Sound\\Karutan\\Orcus_death.wav");
        break;
    case MONSTER_MODEL_GOLLOCK:
        LoadWaveFile(SOUND_KARUTAN_GOLOCH_MOVE1, L"Data\\Sound\\Karutan\\Goloch_move1.wav");
        LoadWaveFile(SOUND_KARUTAN_GOLOCH_MOVE2, L"Data\\Sound\\Karutan\\Goloch_move2.wav");
        LoadWaveFile(SOUND_KARUTAN_GOLOCH_ATTACK, L"Data\\Sound\\Karutan\\Goloch_attack.wav");
        LoadWaveFile(SOUND_KARUTAN_GOLOCH_DEATH, L"Data\\Sound\\Karutan\\Goloch_death.wav");
        break;
    case MONSTER_MODEL_CRYPTA:
        LoadWaveFile(SOUND_KARUTAN_CRYPTA_MOVE1, L"Data\\Sound\\Karutan\\Crypta_move1.wav");
        LoadWaveFile(SOUND_KARUTAN_CRYPTA_MOVE2, L"Data\\Sound\\Karutan\\Crypta_move2.wav");
        LoadWaveFile(SOUND_KARUTAN_CRYPTA_ATTACK, L"Data\\Sound\\Karutan\\Crypta_attack.wav");
        LoadWaveFile(SOUND_KARUTAN_CRYPTA_DEATH, L"Data\\Sound\\Karutan\\Crypta_death.wav");
        break;
    case MONSTER_MODEL_CRYPOS:
        LoadWaveFile(SOUND_KARUTAN_CRYPOS_MOVE1, L"Data\\Sound\\Karutan\\Crypos_move1.wav");
        LoadWaveFile(SOUND_KARUTAN_CRYPOS_MOVE2, L"Data\\Sound\\Karutan\\Crypos_move2.wav");
        LoadWaveFile(SOUND_KARUTAN_CRYPOS_ATTACK1, L"Data\\Sound\\Karutan\\Crypos_attack_1.wav");
        LoadWaveFile(SOUND_KARUTAN_CRYPOS_ATTACK2, L"Data\\Sound\\Karutan\\Crypos_attack_2.wav");
        break;
    case MONSTER_MODEL_CONDRA:
        LoadWaveFile(SOUND_KARUTAN_CONDRA_MOVE1, L"Data\\Sound\\Karutan\\Condra_move1.wav");
        LoadWaveFile(SOUND_KARUTAN_CONDRA_MOVE2, L"Data\\Sound\\Karutan\\Condra_move2.wav");
        LoadWaveFile(SOUND_KARUTAN_CONDRA_ATTACK, L"Data\\Sound\\Karutan\\Condra_attack.wav");
        LoadWaveFile(SOUND_KARUTAN_CONDRA_DEATH, L"Data\\Sound\\Karutan\\Condra_death.wav");
        break;
    case MONSTER_MODEL_NACONDRA:
        LoadWaveFile(SOUND_KARUTAN_CONDRA_MOVE1, L"Data\\Sound\\Karutan\\Condra_move1.wav");
        LoadWaveFile(SOUND_KARUTAN_CONDRA_MOVE2, L"Data\\Sound\\Karutan\\Condra_move2.wav");
        LoadWaveFile(SOUND_KARUTAN_NARCONDRA_ATTACK, L"Data\\Sound\\Karutan\\NarCondra_attack.wav");
        LoadWaveFile(SOUND_KARUTAN_CONDRA_DEATH, L"Data\\Sound\\Karutan\\Condra_death.wav");
        break;
#endif // ASG_ADD_KARUTAN_MONSTERS
    }

    if (b->BoneHead >= b->NumBones)
    {
        sessionKeeper_.WorldUnit()->FinishLoad(false);
        throw std::runtime_error("Monster model lacks its configured head bone");
    }
}

bool SessionGameDataUnit::PrepareMonsterResources(EMonsterModelType type)
{
    WorldResources resources;
    WorldResources::Failure failure;
    const auto dependencies = WorldModelDependency::ForMonster(type);
    if (resources.PrepareModels(sessionKeeper_, dependencies, L"Data", failure) &&
        resources.InstallModels(sessionKeeper_, failure) &&
        sessionKeeper_.Visual()->PrepareMonsterVisuals(type, resources, failure))
        return true;
    g_ErrorReport.Write(L"%ls: %ls\r\n", failure.resource.c_str(), failure.detail);
    sessionKeeper_.WorldUnit()->FinishLoad(false);
    throw std::runtime_error("Runtime monster resources failed");
}

bool SessionGameDataUnit::FlushPendingMonsterModelsOnOwner() noexcept
try
{
    if (!sessionKeeper_.ApplicationKeeperRef().IsOwnerThread())
    {
        return false;
    }
    if (!monsterModelLoadPending_.load(std::memory_order_acquire))
    {
        return true;
    }

    std::vector<EMonsterModelType> pending;
    {
        std::lock_guard lock(pendingMonsterModelsMutex_);
        pending.swap(pendingMonsterModels_);
        monsterModelLoadPending_.store(false, std::memory_order_release);
    }
    for (const EMonsterModelType type : pending)
    {
        if (!OpenMonsterModel(type))
            return false;
    }
    return true;
}

catch (...)
{
    return sessionKeeper_.WorldUnit()->FinishLoad(false);
}

namespace
{

DWORD GenerateGameDataChecksum(const BYTE *buffer, DWORD size, WORD key) noexcept
{
    DWORD rollingKey = static_cast<DWORD>(key);
    DWORD result = rollingKey << 9;
    for (DWORD checked = 0; checked <= size - 4; checked += 4)
    {
        DWORD value = 0;
        std::memcpy(&value, buffer + checked, sizeof(value));
        if ((checked / 4 + key) % 2 == 0)
        {
            result ^= value;
        }
        else
        {
            result += value;
        }
        if (checked % 16 == 0)
        {
            result ^= ((rollingKey + result) >> ((checked / 4) % 8 + 1));
        }
    }
    return result;
}
} // namespace

bool GameData::Load()
{
    const std::unique_lock loadLock(loadMutex_, std::try_to_lock);
    if (!loadLock.owns_lock())
    {
        return false;
    }
    if (!registered_)
    {
        return false;
    }
    if (loaded_)
    {
        return true;
    }
    if (loading_)
    {
        return false;
    }

    loading_ = true;
    loadFailed_ = false;
    if (storage_.g_strSelectedML.empty())
    {
        ResetStorage();
        return false;
    }

    storage_.EditMonsterNumber = 0;
    storage_.GateAttribute = new (std::nothrow) GATE_ATTRIBUTE[MAX_GATES]{};
    if (storage_.GateAttribute == nullptr)
    {
        RecordLoadFailure();
    }

    wchar_t monsterPath[100]{};
    std::swprintf(monsterPath, std::size(monsterPath), L"Data\\Local\\%ls\\NpcName_%ls.txt",
                  storage_.g_strSelectedML.c_str(), storage_.g_strSelectedML.c_str());

    if (!loadFailed_)
        OpenMonsterScript(monsterPath);
    if (!loadFailed_)
        OpenGateScript(L"Data\\Gate.bmd");
    if (!loadFailed_)
        OpenFilterFile(L"Data\\Local\\Filter.bmd");
    if (!loadFailed_)
        OpenNameFilterFile(L"Data\\Local\\FilterName.bmd");
    if (!loadFailed_)
        OpenMonsterSkillScript(L"Data\\Local\\MonsterSkill.bmd");
    if (!loadFailed_)
        CreateClassAttributes();

    loading_ = false;
    loaded_ = !loadFailed_;
    if (!loaded_)
    {
        ResetStorage();
        return false;
    }
    return true;
}

void GameData::OpenFilterFile(const wchar_t *fileName)
{
    FILE *file = _wfopen(fileName, L"rb");
    if (file == nullptr)
    {
        ReportLoadError(fileName, GameDataLoadError::MissingFile, true);
        return;
    }

    constexpr int recordSize = 20;
    std::vector<BYTE> buffer(recordSize * MAX_FILTERS);
    const bool dataRead = fread(buffer.data(), buffer.size(), 1, file) == 1;
    DWORD checksum = 0;
    const bool checksumRead = fread(&checksum, sizeof(checksum), 1, file) == 1;
    fclose(file);
    if (!dataRead || !checksumRead)
    {
        ReportLoadError(fileName, GameDataLoadError::CorruptFile, true);
        return;
    }

    BYTE *current = buffer.data();
    for (int index = 0; index < MAX_FILTERS; ++index)
    {
        BuxConvert(current, recordSize);
        std::memcpy(storage_.AbuseFilter[index], current, recordSize);
        if (storage_.AbuseFilter[index][0] == 0)
        {
            storage_.AbuseFilterNumber = index;
            break;
        }
        current += recordSize;
    }
}

void GameData::OpenNameFilterFile(const wchar_t *fileName)
{
    FILE *file = _wfopen(fileName, L"rb");
    if (file == nullptr)
    {
        ReportLoadError(fileName, GameDataLoadError::MissingFile, true);
        return;
    }

    constexpr int recordSize = 20;
    std::vector<BYTE> buffer(recordSize * MAX_NAMEFILTERS);
    const bool dataRead = fread(buffer.data(), buffer.size(), 1, file) == 1;
    DWORD checksum = 0;
    const bool checksumRead = fread(&checksum, sizeof(checksum), 1, file) == 1;
    fclose(file);
    if (!dataRead || !checksumRead ||
        checksum !=
            GenerateGameDataChecksum(buffer.data(), static_cast<DWORD>(buffer.size()), 0x2BC1))
    {
        ReportLoadError(fileName, GameDataLoadError::CorruptFile, true);
        return;
    }

    BYTE *current = buffer.data();
    for (int index = 0; index < MAX_NAMEFILTERS; ++index)
    {
        BuxConvert(current, recordSize);
        std::memcpy(storage_.AbuseNameFilter[index], current, recordSize);
        if (storage_.AbuseNameFilter[index][0] == 0)
        {
            storage_.AbuseNameFilterNumber = index;
            break;
        }
        current += recordSize;
    }
}

void GameData::OpenGateScript(const wchar_t *fileName)
{
    FILE *file = _wfopen(fileName, L"rb");
    if (file == nullptr)
    {
        ReportLoadError(fileName, GameDataLoadError::MissingFile, true);
        return;
    }

    const int recordSize = sizeof(GATE_ATTRIBUTE);
    std::vector<BYTE> buffer(recordSize);
    for (int index = 0; index < MAX_GATES; ++index)
    {
        if (fread(buffer.data(), buffer.size(), 1, file) != 1)
        {
            ReportLoadError(fileName, GameDataLoadError::CorruptFile, true);
            break;
        }
        BuxConvert(buffer.data(), recordSize);
        std::memcpy(&storage_.GateAttribute[index], buffer.data(), recordSize);
    }
    fclose(file);
}

void ApplicationLegacyCalls::OpenFilterFile(const wchar_t *fileName)
{
    applicationKeeper_.GameDataUnit()->OpenFilterFile(fileName);
}
void ApplicationLegacyCalls::OpenNameFilterFile(const wchar_t *fileName)
{
    applicationKeeper_.GameDataUnit()->OpenNameFilterFile(fileName);
}
void ApplicationLegacyCalls::OpenGateScript(const wchar_t *fileName)
{
    applicationKeeper_.GameDataUnit()->OpenGateScript(fileName);
}

bool ApplicationSupportCalls::LoadItemDataFile(wchar_t *fileName, CErrorReport &errorReport,
                                               HWND window)
{
    return applicationKeeper_.GameDataUnit()->LoadItemDataFile(fileName, errorReport, window);
}
bool ApplicationSupportCalls::LoadSkillDataFile(wchar_t *fileName, CErrorReport &errorReport,
                                                HWND window)
{
    return applicationKeeper_.GameDataUnit()->LoadSkillDataFile(fileName, errorReport, window);
}

// dialog

// item

void SessionGameDataUnit::PrintItem(wchar_t *FileName) const
{
    FILE *fp = _wfopen(FileName, L"wt");
    fwprintf(
        fp,
        L"                이름  최소공격력 최대공격력 방어력 방어율 필요힘 필요민첩 필요에너지\n");
    //fwprintf(fp,"                이름    카오스성공확률\n");
    bool Excellent = true;
    for (int i = 0; i < 16 * MAX_ITEM_INDEX; i++)
    {
        if ((i & 0x1FF) == 0)
        {
            fwprintf(
                fp,
                L"------------------------------------------------------------------------------------------------------\n");
        }
        ITEM_ATTRIBUTE *p = &ItemAttribute[i];
        if (p->Name[0] != 0)
        {
            int Plus;
            if (i >= 12 * MAX_ITEM_INDEX)
                Plus = 1;
            else
                Plus = 10;
            for (int j = 0; j < Plus; j++)
            {
                int Level = j;
                int RequireStrength = 0;
                int RequireDexterity = 0;
                int RequireEnergy = 0;
                int DamageMin = p->DamageMin;
                int DamageMax = p->DamageMax;
                int Defense = p->Defense;
                int SuccessfulBlocking = p->SuccessfulBlocking;
                if (DamageMin > 0)
                {
                    if (Excellent)
                    {
                        if (p->Level)
                            DamageMin += p->DamageMin * 25 / p->Level + 5;
                    }
                    DamageMin += Level * 3;
                }
                if (DamageMax > 0)
                {
                    if (Excellent)
                    {
                        if (p->Level)
                            DamageMax += p->DamageMin * 25 / p->Level + 5;
                    }
                    DamageMax += Level * 3;
                }
                if (Defense > 0)
                {
                    if (i >= ITEM_SHIELD && i < ITEM_SHIELD + MAX_ITEM_INDEX)
                    {
                        Defense += Level;
                    }
                    else
                    {
                        if (Excellent)
                        {
                            if (p->Level)
                                Defense += p->Defense * 12 / p->Level + 4 + p->Level / 5;
                        }
                        Defense += Level * 3;
                    }
                }
                if (SuccessfulBlocking > 0)
                {
                    if (Excellent)
                    {
                        if (p->Level)
                            SuccessfulBlocking += p->SuccessfulBlocking * 25 / p->Level + 5;
                    }
                    SuccessfulBlocking += Level * 3;
                }
                int ItemLevel = p->Level;
                if (Excellent)
                    ItemLevel = p->Level + 25;
                if (p->RequireStrength)
                    RequireStrength = 20 + p->RequireStrength * (ItemLevel + Level * 3) * 3 / 100;
                else
                    RequireStrength = 0;
                if (p->RequireDexterity)
                    RequireDexterity = 20 + p->RequireDexterity * (ItemLevel + Level * 3) * 3 / 100;
                else
                    RequireDexterity = 0;
                if (p->RequireEnergy)
                {
                    RequireEnergy = 20 + p->RequireEnergy * (ItemLevel + Level * 3) * 4 / 10;
                }
                else
                {
                    RequireEnergy = 0;
                }
                if (i >= ITEM_STAFF && i < ITEM_STAFF * MAX_ITEM_INDEX)
                {
                    DamageMin = DamageMin / 2 + Level * 2;
                    DamageMax = 0;
                }
                ITEM ip;
                ip.Type = i;
                ip.ExcellentFlags = Excellent;
                ip.Level = Level;
                SetItemAttributes(&ip);

                ItemValue(&ip, 0);

                if (j == 0)
                    fwprintf(fp, L"%20s %8d %8d %8d %8d %8d %8d %8d %8d %8lld\n", p->Name, DamageMin,
                             DamageMax, Defense, SuccessfulBlocking, RequireStrength,
                             RequireDexterity, RequireEnergy, p->WeaponSpeed,
                             static_cast<long long>(ItemValue(&ip)));
                //fwprintf(fp,"%20s %4d%%",p->Name, iRate);
                else
                    fwprintf(fp, L"%17s +%d %8d %8d %8d %8d %8d %8d %8d %8d %8lld\n", L"", Level,
                             DamageMin, DamageMax, Defense, SuccessfulBlocking, RequireStrength,
                             RequireDexterity, RequireEnergy, p->WeaponSpeed,
                             static_cast<long long>(ItemValue(&ip)));
                //fwprintf(fp,"%4d%%<+%d>",iRate,Level);
            }
            fwprintf(fp, L"\n");
        }
    }
    fclose(fp);
}

const wchar_t *SessionGameDataUnit::getMonsterName(int type) const
{
    for (int i = 0; i < MAX_MONSTER; ++i)
    {
        if (MonsterScript[i].Type == type)
        {
            return MonsterScript[i].Name;
        }
    }

    return L"()";
}
