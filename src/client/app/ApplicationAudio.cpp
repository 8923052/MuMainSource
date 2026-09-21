#include "app/ApplicationAudio.h"
#include "app/ApplicationKeeper.h"
#include "app/AppWindow.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "data/ResourceData.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/Shop.h"
#include "domain/WorldSimulation.h"
#include "I18N/All.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/SessionAudio.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionRender.h"
#include "session/SessionUi.h"
#include "session/SessionWorkspace.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "ui/features/Hud/HudLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"

#ifdef _WIN32
#include <objbase.h>
#include <dsound.h>
#include <mmsystem.h>
#endif

ApplicationAudio::ApplicationAudio(ApplicationKeeper &keeper,
                                   std::unique_ptr<ApplicationAudioDevice> device) noexcept
    : ApplicationLegacyCalls(keeper), storage_(keeper.AudioStorageRef()), device_(std::move(device))
{
    (void)applicationKeeper_.RegisterApplicationAudio(*this);
}

ApplicationAudio::~ApplicationAudio()
{
    Shutdown();
}

bool ApplicationAudio::Initialize(NativeWindowHandle nativeWindow) noexcept
{
    if (initialized_)
    {
        return true;
    }
    return SUCCEEDED(InitDirectSound(static_cast<HWND>(nativeWindow)));
}

bool ApplicationAudio::InitializeLegacyAudio(NativeWindowHandle nativeWindow) noexcept
{
    return SUCCEEDED(InitDirectSound(static_cast<HWND>(nativeWindow)));
}

HRESULT ApplicationAudio::InitDirectSound(HWND window) noexcept
{
    if (device_ == nullptr ||
        !device_->Initialize(static_cast<NativeWindowHandle>(window),
                             applicationKeeper_.ApplicationConfig().masterMusicVolume,
                             applicationKeeper_.ApplicationConfig().masterSoundVolume))
    {
        return E_FAIL;
    }
    initialized_ = true;
    return S_OK;
}

void ApplicationAudio::Shutdown() noexcept
{
    if (!initialized_)
    {
        return;
    }
    FreeDirectSound();
}

void ApplicationAudio::ShutdownLegacyAudio() noexcept
{
    if (initialized_)
    {
        for (int i = 0; i < MAX_BUFFER; ++i)
        {
            (void)ReleaseBuffer(i);
        }
        FreeDirectSound();
    }
}

void ApplicationAudio::FreeDirectSound() noexcept
{
    initialized_ = false;
    device_->Shutdown();
}

bool ApplicationAudio::IsInitialized() const noexcept
{
    return initialized_;
}

void ApplicationAudio::SetMasterMusicVolume(int level) noexcept
{
    applicationKeeper_.ApplicationConfig().masterMusicVolume = level;
    if (initialized_)
    {
        device_->SetMasterMusicVolume(level);
    }
}

void ApplicationAudio::SetMasterSoundVolume(int level) noexcept
{
    applicationKeeper_.ApplicationConfig().masterSoundVolume = level;
    if (initialized_)
    {
        device_->SetMasterSoundVolume(level);
    }
}

void ApplicationAudio::UpdateSpatialAudio(float cameraYaw) noexcept
{
    if (initialized_ && device_ != nullptr)
    {
        device_->UpdateSpatialAudio(cameraYaw);
    }
}

int ApplicationAudio::MasterMusicVolume() const noexcept
{
    return applicationKeeper_.ApplicationConfig().masterMusicVolume;
}

int ApplicationAudio::MasterSoundVolume() const noexcept
{
    return applicationKeeper_.ApplicationConfig().masterSoundVolume;
}

SessionAudioWorkspaceView &ApplicationAudio::WorkspaceView() noexcept
{
    return buses_.WorkspaceView();
}

void ApplicationAudio::ReleaseSessionAudio(SessionId id) noexcept
{
    if (device_ != nullptr)
    {
        device_->ReleaseSessionAudio(id);
    }
}

void ApplicationAudio::ApplySessionAudioGating(SessionAudioBusView &bus) noexcept
{
    if (initialized_ && device_ != nullptr)
    {
        device_->ApplySessionAudioGating(bus);
    }
}

void ApplicationAudio::UpdateSessionSpatialAudio(SessionAudioBusView &bus, float cameraYaw,
                                                 const float *listenerPosition) noexcept
{
    if (initialized_ && device_ != nullptr)
    {
        device_->UpdateSessionSpatialAudio(bus, cameraYaw, listenerPosition);
    }
}

void ApplicationAudio::SetEnableSound(bool enabled) noexcept
{
    if (device_ != nullptr)
    {
        device_->SetEnableSound(enabled);
    }
}

void ApplicationAudio::LoadWaveFile(ESound buffer, const wchar_t *fileName, int channels,
                                    bool enable3D) noexcept
{
    if (device_ != nullptr)
    {
        device_->LoadWaveFile(buffer, fileName, channels, enable3D);
    }
}

HRESULT ApplicationAudio::ReleaseBuffer(int buffer) noexcept
{
    return device_ != nullptr ? device_->ReleaseBuffer(buffer) : E_FAIL;
}

void ApplicationAudio::SetVolume(int buffer, long volume) noexcept
{
    if (device_ != nullptr)
    {
        device_->SetVolume(buffer, volume);
    }
}

void ApplicationAudio::SetMasterVolume(long volume) noexcept
{
    if (device_ != nullptr)
    {
        device_->SetMasterVolume(volume);
    }
}

void ApplicationAudio::StopMusic(SessionAudioBusView &bus) noexcept
{
    if (device_ != nullptr)
        device_->StopMusic(bus);
}

void ApplicationAudio::StopMp3(SessionAudioBusView &bus, const char *name, BOOL enforce) noexcept
{
    if (device_ != nullptr)
        device_->StopMp3(bus, name, enforce);
}

void ApplicationAudio::PlayMp3(SessionAudioBusView &bus, const char *name, BOOL enforce) noexcept
{
    if (device_ != nullptr)
        device_->PlayMp3(bus, name, enforce);
}

bool ApplicationAudio::IsEndMp3(SessionAudioBusView &bus) noexcept
{
    return device_ != nullptr && device_->IsEndMp3(bus);
}

int ApplicationAudio::GetMp3PlayPosition(SessionAudioBusView &bus) noexcept
{
    return device_ != nullptr ? device_->GetMp3PlayPosition(bus) : 0;
}

HRESULT ApplicationAudio::PlayBuffer(SessionAudioBusView &bus, ESound buffer, std::uint64_t emitter,
                                     BOOL looped) noexcept
{
    return device_ != nullptr ? device_->PlayBuffer(bus, buffer, emitter, looped) : E_FAIL;
}

void ApplicationAudio::StopBuffer(SessionAudioBusView &bus, ESound buffer,
                                  BOOL resetPosition) noexcept
{
    if (device_ != nullptr)
        device_->StopBuffer(bus, buffer, resetPosition);
}

void ApplicationAudio::AllStopSound(SessionAudioBusView &bus) noexcept
{
    if (device_ != nullptr)
        device_->AllStopSound(bus);
}

HRESULT ApplicationLegacyCalls::InitDirectSound(HWND window)
{
    return applicationKeeper_.ApplicationAudioUnit()->InitDirectSound(window);
}
void ApplicationLegacyCalls::FreeDirectSound()
{
    applicationKeeper_.ApplicationAudioUnit()->FreeDirectSound();
}
void ApplicationLegacyCalls::LoadWaveFile(ESound buffer, const wchar_t *fileName, int channels,
                                          bool enable3D)
{
    applicationKeeper_.ApplicationAudioUnit()->LoadWaveFile(buffer, fileName, channels, enable3D);
}
HRESULT ApplicationLegacyCalls::ReleaseBuffer(int buffer)
{
    return applicationKeeper_.ApplicationAudioUnit()->ReleaseBuffer(buffer);
}
void ApplicationLegacyCalls::SetMasterVolume(long volume)
{
    applicationKeeper_.ApplicationAudioUnit()->SetMasterVolume(volume);
}
void ApplicationLegacyCalls::SetEnableSound(bool enabled) noexcept
{
    return applicationKeeper_.ApplicationAudioUnit()->SetEnableSound(enabled);
} // OMF-00063
void ApplicationLegacyCalls::SetVolume(int buffer, long volume) noexcept
{
    return applicationKeeper_.ApplicationAudioUnit()->SetVolume(buffer, volume);
} // OMF-00071

LegacyApplicationAudioDevice::LegacyApplicationAudioDevice(ApplicationAudioStorage &storage,
                                                           CErrorReport &errorReport,
                                                           CmuConsoleDebug &consoleDebug) noexcept
    : mixer_(storage.mixer), errorReport_(errorReport), consoleDebug_(consoleDebug),
      effects_(CreateLegacyApplicationAudioEffects(errorReport_, consoleDebug_))
{
}

bool LegacyApplicationAudioDevice::Initialize(NativeWindowHandle nativeWindow,
                                              int masterMusicVolume, int masterSoundVolume) noexcept
{
    InitializeMusic(masterMusicVolume);
    (void)InitializeSoundEffects(nativeWindow, mixer_);
    SetMasterSoundVolume(masterSoundVolume);
    return true;
}

void LegacyApplicationAudioDevice::Shutdown() noexcept
{
    ShutdownSoundEffects();
    ShutdownMusic();
}

void LegacyApplicationAudioDevice::SetMasterSoundVolume(int level) noexcept
{
    if (level > AudioPlayer::MaxVolumeLevel)
        level = AudioPlayer::MaxVolumeLevel;
    if (level < 0)
        level = 0;

    const long volume =
        level == 0 ? -10000L
                   : static_cast<long>(-2000.0 * std::log10(10.0 / static_cast<double>(level)));
    ApplyMasterSoundVolume(volume);
}

bool LegacyApplicationAudioDevice::InitializeSoundEffects(NativeWindowHandle nativeWindow,
                                                          MIX_Mixer *mixer) noexcept
{
    return effects_ != nullptr && effects_->Initialize(nativeWindow, mixer);
}

void LegacyApplicationAudioDevice::ShutdownSoundEffects() noexcept
{
    if (effects_ != nullptr)
        effects_->Shutdown();
}

void LegacyApplicationAudioDevice::ApplyMasterSoundVolume(long volume) noexcept
{
    if (effects_ != nullptr)
        effects_->SetMasterVolume(volume);
}

void LegacyApplicationAudioDevice::SetEnableSound(bool enabled) noexcept
{
    if (effects_ != nullptr)
        effects_->SetEnabled(enabled);
}

void LegacyApplicationAudioDevice::LoadWaveFile(ESound buffer, const wchar_t *fileName,
                                                int channels, bool enable3D) noexcept
{
    if (effects_ != nullptr)
    {
        effects_->LoadWaveFile(buffer, fileName, channels, enable3D);
    }
}

HRESULT LegacyApplicationAudioDevice::ReleaseBuffer(int buffer) noexcept
{
    return effects_ != nullptr ? effects_->ReleaseBuffer(buffer) : E_FAIL;
}

HRESULT LegacyApplicationAudioDevice::PlayBuffer(SessionAudioBusView &bus, ESound buffer,
                                                 std::uint64_t emitter, BOOL looped) noexcept
{
    return effects_ != nullptr ? effects_->PlayBuffer(bus, buffer, emitter, looped) : E_FAIL;
}

void LegacyApplicationAudioDevice::StopBuffer(SessionAudioBusView &bus, ESound buffer,
                                              BOOL resetPosition) noexcept
{
    if (effects_ != nullptr)
    {
        effects_->StopBuffer(bus, buffer, resetPosition);
    }
}

void LegacyApplicationAudioDevice::AllStopSound(SessionAudioBusView &bus) noexcept
{
    if (effects_ != nullptr)
        effects_->StopAll(bus);
}

void LegacyApplicationAudioDevice::SetVolume(int buffer, long volume) noexcept
{
    if (effects_ != nullptr)
        effects_->SetVolume(buffer, volume);
}

void LegacyApplicationAudioDevice::SetMasterVolume(long volume) noexcept
{
    if (effects_ != nullptr)
        effects_->SetMasterVolume(volume);
}

void LegacyApplicationAudioDevice::UpdateSpatialAudio(float) noexcept
{
}

void LegacyApplicationAudioDevice::UpdateSessionSpatialAudio(SessionAudioBusView &bus,
                                                             float cameraYaw,
                                                             const float *listenerPosition) noexcept
{
    if (effects_ != nullptr)
    {
        effects_->UpdateSpatialAudio(bus, cameraYaw, listenerPosition);
    }
}
// DirectX Sound
// Desc: DirectSound support for how to load a wave file and play it using a
//       static DirectSound buffer.
// Copyright (c) 1999 Microsoft Corp. All rights reserved.

#ifdef _WIN32 // ---- Windows: the DirectSound implementation ----------------

namespace
{
constexpr std::size_t kBufferNameLength = 64;

template <typename T> struct ComReleaser
{
    void operator()(T *pointer) const noexcept
    {
        if (pointer != nullptr)
        {
            pointer->Release();
        }
    }
};

template <typename T> using ComPtr = std::unique_ptr<T, ComReleaser<T>>;

struct SoundBufferEntry
{
    std::array<ComPtr<IDirectSoundBuffer>, MAX_CHANNEL> buffers{};
    std::array<ComPtr<IDirectSound3DBuffer>, MAX_CHANNEL> buffers3D{};
    std::array<OBJECT *, MAX_CHANNEL> attachedObjects{};
    std::array<wchar_t, kBufferNameLength> name{};
    int activeChannel = 0;
    int maxChannels = 0;
    bool enable3D = false;
    std::vector<std::uint8_t> waveData{};
    DWORD waveDataSize = 0;
};

class DirectSoundManager
{
  public:
    DirectSoundManager(CErrorReport &errorReport, CmuConsoleDebug &consoleDebug) noexcept
        : errorReport_(errorReport), consoleDebug_(consoleDebug)
    {
    }
    HRESULT Initialize(HWND windowHandle);
    void Shutdown();

    void SetEnabled(bool enabled);
    bool IsEnabled() const noexcept;

    void Set3DEnabled(bool enabled);
    bool Is3DEnabled() const noexcept;

    HRESULT LoadWaveFile(ESound bufferId, const wchar_t *filename, int maxChannel, bool enable3D);
    HRESULT ReleaseBuffer(ESound bufferId);
    void ReleaseAllBuffers();

    HRESULT PlayBuffer(ESound bufferId, OBJECT *object, bool looped);
    void StopBuffer(ESound bufferId, bool resetPosition);
    void StopAll();

    HRESULT RestoreBuffers(int bufferId, int channel);
    void SetVolume(ESound bufferId, long volume);
    void SetMasterVolume(long volume);

    void Update3DPositions(float cameraYaw, const float *listenerPosition);

  private:
    HRESULT CreateStaticBuffer(SoundBufferEntry &entry, ESound bufferId, const wchar_t *filename,
                               bool enable3D);
    HRESULT CopyWaveDataToBuffer(SoundBufferEntry &entry, int bufferId, int channel);
    void ResetEntry(SoundBufferEntry &entry);
    void SetVolumeInternal(ESound bufferId, long volume);
    bool IsValidBufferIndex(int bufferId) const noexcept;
    bool IsValidChannelIndex(int channel) const noexcept;
    IDirectSoundBuffer *GetBuffer(int bufferId, int channel) const noexcept;
    IDirectSound3DBuffer *Get3DBuffer(int bufferId, int channel) const noexcept;
    void EnsureCoInitialized();
    void CoUninitializeIfNeeded();

    mutable std::mutex mutex_;
    ComPtr<IDirectSound> device_;
    ComPtr<IDirectSound3DListener> listener_;
    std::array<SoundBufferEntry, MAX_BUFFER> entries_{};
    std::atomic<bool> comInitialized_{false};
    std::uint32_t bufferBytes_ = 0;
    bool enableSound_ = false;
    bool enable3DSound_ = false;
    int loadCount_ = 0;
    long masterVolume_ = 0L;
    CErrorReport &errorReport_;
    CmuConsoleDebug &consoleDebug_;
};

HRESULT DirectSoundManager::Initialize(HWND windowHandle)
{
    std::lock_guard lock(mutex_);

    if (device_)
    {
        enableSound_ = true;
        return S_OK;
    }

    EnsureCoInitialized();

    IDirectSound *rawDevice = nullptr;
    HRESULT hr = DirectSoundCreate(nullptr, &rawDevice, nullptr);
    if (FAILED(hr))
    {
        errorReport_.Write(L"InitDirectSound - DirectSoundCreate failed (0x%08X)\r\n", hr);
        return hr;
    }
    device_.reset(rawDevice);

    hr = device_->SetCooperativeLevel(windowHandle, DSSCL_PRIORITY);
    if (FAILED(hr))
    {
        errorReport_.Write(L"InitDirectSound - SetCooperativeLevel failed (0x%08X)\r\n", hr);
        device_.reset();
        return hr;
    }

    DSBUFFERDESC desc{};
    desc.dwSize = sizeof(DSBUFFERDESC);
    desc.dwFlags = DSBCAPS_CTRL3D | DSBCAPS_PRIMARYBUFFER;

    IDirectSoundBuffer *rawPrimary = nullptr;
    hr = device_->CreateSoundBuffer(&desc, &rawPrimary, nullptr);
    if (FAILED(hr))
    {
        errorReport_.Write(L"InitDirectSound - CreateSoundBuffer failed (0x%08X)\r\n", hr);
        device_.reset();
        return hr;
    }
    ComPtr<IDirectSoundBuffer> primaryBuffer(rawPrimary);

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 22050;
    format.wBitsPerSample = 16;
    format.nBlockAlign = (format.wBitsPerSample / 8) * format.nChannels;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    hr = primaryBuffer->SetFormat(&format);
    if (FAILED(hr))
    {
        errorReport_.Write(L"InitDirectSound - SetFormat failed (0x%08X)\r\n", hr);
        device_.reset();
        return hr;
    }

    IDirectSound3DListener *rawListener = nullptr;
    hr = primaryBuffer->QueryInterface(IID_IDirectSound3DListener,
                                       reinterpret_cast<void **>(&rawListener));
    if (FAILED(hr))
    {
        errorReport_.Write(L"InitDirectSound - QueryInterface listener failed (0x%08X)\r\n", hr);
        device_.reset();
        return hr;
    }
    listener_.reset(rawListener);

    for (auto &entry : entries_)
    {
        ResetEntry(entry);
    }

    bufferBytes_ = 0;
    enableSound_ = true;
    return S_OK;
}

void DirectSoundManager::Shutdown()
{
    std::lock_guard lock(mutex_);

    ReleaseAllBuffers();
    listener_.reset();
    device_.reset();
    enableSound_ = false;
    enable3DSound_ = false;
    CoUninitializeIfNeeded();
}

void DirectSoundManager::SetEnabled(bool enabled)
{
    enableSound_ = enabled;
}

bool DirectSoundManager::IsEnabled() const noexcept
{
    return enableSound_;
}

void DirectSoundManager::Set3DEnabled(bool enabled)
{
    enable3DSound_ = enabled;
}

bool DirectSoundManager::Is3DEnabled() const noexcept
{
    return enable3DSound_;
}

HRESULT DirectSoundManager::LoadWaveFile(ESound bufferId, const wchar_t *filename, int maxChannel,
                                         bool enable3D)
{
    if (!IsEnabled())
    {
        return E_FAIL;
    }

    if (!IsValidBufferIndex(bufferId))
    {
        return E_INVALIDARG;
    }

    const int clampedChannels = std::clamp(maxChannel, 1, MAX_CHANNEL);

    std::lock_guard lock(mutex_);
    auto &entry = entries_[bufferId];
    if (entry.maxChannels > 0)
    {
        return S_FALSE;
    }

    const bool enable3DBuffer = enable3D && Is3DEnabled();
    entry.activeChannel = 0;
    entry.maxChannels = clampedChannels;
    entry.enable3D = enable3DBuffer;

    HRESULT hr = CreateStaticBuffer(entry, bufferId, filename, enable3DBuffer);
    if (FAILED(hr))
    {
        ResetEntry(entry);
        return hr;
    }
    entry.name.fill(L'\0');
    if (filename != nullptr)
    {
        wcsncpy_s(entry.name.data(), entry.name.size(), filename, _TRUNCATE);
    }

    ++loadCount_;
    SetVolumeInternal(bufferId, masterVolume_);

    return S_OK;
}

HRESULT DirectSoundManager::ReleaseBuffer(ESound bufferId)
{
    if (!IsValidBufferIndex(bufferId))
    {
        return E_INVALIDARG;
    }

    std::lock_guard lock(mutex_);
    auto &entry = entries_[bufferId];
    ResetEntry(entry);
    if (loadCount_ > 0)
    {
        --loadCount_;
    }
    return S_OK;
}

void DirectSoundManager::ReleaseAllBuffers()
{
    for (int i = 0; i < MAX_BUFFER; ++i)
    {
        ResetEntry(entries_[i]);
    }
    loadCount_ = 0;
}

HRESULT DirectSoundManager::PlayBuffer(ESound bufferId, OBJECT *object, bool looped)
{
    if (!IsEnabled())
    {
        return E_FAIL;
    }

    if (!IsValidBufferIndex(bufferId))
    {
        return E_INVALIDARG;
    }

    std::lock_guard lock(mutex_);
    auto &entry = entries_[bufferId];
    if (entry.maxChannels == 0)
    {
        return E_FAIL;
    }

    const int currentChannel = entry.activeChannel % entry.maxChannels;
    entry.activeChannel = (entry.activeChannel + 1) % entry.maxChannels;

    IDirectSoundBuffer *buffer = GetBuffer(bufferId, currentChannel);
    if (buffer == nullptr)
    {
        return E_FAIL;
    }

    HRESULT hr = RestoreBuffers(bufferId, currentChannel);
    if (FAILED(hr))
    {
        return hr;
    }

    const DWORD flags = looped ? DSBPLAY_LOOPING : 0;
    hr = buffer->Play(0, 0, flags);
    if (FAILED(hr))
    {
        consoleDebug_.Write(MCD_ERROR, L"DirectSoundManager::PlayBuffer failed for %d (0x%08X)",
                            bufferId, hr);
        return hr;
    }

    if (entry.enable3D)
    {
        entry.attachedObjects[currentChannel] = object;
    }

    return S_OK;
}

void DirectSoundManager::StopBuffer(ESound bufferId, bool resetPosition)
{
    if (!IsValidBufferIndex(bufferId))
    {
        return;
    }

    std::lock_guard lock(mutex_);
    auto &entry = entries_[bufferId];
    for (int channel = 0; channel < entry.maxChannels; ++channel)
    {
        IDirectSoundBuffer *buffer = GetBuffer(bufferId, channel);
        if (buffer == nullptr)
        {
            continue;
        }

        buffer->Stop();
        if (resetPosition)
        {
            buffer->SetCurrentPosition(0);
        }
    }
}

void DirectSoundManager::StopAll()
{
    for (int i = 0; i < MAX_BUFFER; ++i)
    {
        StopBuffer(static_cast<ESound>(i), true);
    }
}

HRESULT DirectSoundManager::RestoreBuffers(int bufferId, int channel)
{
    if (!IsValidBufferIndex(bufferId) || !IsValidChannelIndex(channel))
    {
        return E_INVALIDARG;
    }

    IDirectSoundBuffer *buffer = GetBuffer(bufferId, channel);
    if (buffer == nullptr)
    {
        return S_OK;
    }

    DWORD status = 0;
    HRESULT hr = buffer->GetStatus(&status);
    if (FAILED(hr))
    {
        return hr;
    }

    if ((status & DSBSTATUS_BUFFERLOST) == 0)
    {
        return S_OK;
    }

    do
    {
        hr = buffer->Restore();
        if (hr == DSERR_BUFFERLOST)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    } while (hr == DSERR_BUFFERLOST);

    if (SUCCEEDED(hr))
    {
        auto &entry = entries_[bufferId];
        hr = CopyWaveDataToBuffer(entry, bufferId, channel);
        if (FAILED(hr))
        {
            errorReport_.Write(
                L"RestoreBuffers - CopyWaveDataToBuffer failed for %d channel %d (0x%08X)\r\n",
                bufferId, channel, hr);
            return hr;
        }
    }

    return hr;
}

void DirectSoundManager::SetVolume(ESound bufferId, long volume)
{
    if (!IsValidBufferIndex(bufferId))
    {
        return;
    }

    std::lock_guard lock(mutex_);
    SetVolumeInternal(bufferId, volume);
}

void DirectSoundManager::SetMasterVolume(long volume)
{
    masterVolume_ = std::clamp<long>(volume, DSBVOLUME_MIN, DSBVOLUME_MAX);
    for (int i = 0; i < MAX_BUFFER; ++i)
    {
        SetVolume(static_cast<ESound>(i), masterVolume_);
    }
}

void DirectSoundManager::Update3DPositions(float cameraYaw, const float *listenerPosition)
{
    if (!IsEnabled() || !Is3DEnabled())
    {
        return;
    }

    std::lock_guard lock(mutex_);
    vec3_t angle{};
    float rotation[3][4];
    Vector(0.f, 0.f, cameraYaw, angle);
    AngleMatrix(angle, rotation);

    for (int bufferIndex = 0; bufferIndex < MAX_BUFFER; ++bufferIndex)
    {
        auto &entry = entries_[bufferIndex];
        if (!entry.enable3D)
        {
            continue;
        }

        for (int channel = 0; channel < entry.maxChannels; ++channel)
        {
            OBJECT *object = entry.attachedObjects[channel];
            IDirectSound3DBuffer *buffer3D = Get3DBuffer(bufferIndex, channel);
            if (object == nullptr || buffer3D == nullptr)
            {
                continue;
            }

            vec3_t position;
            VectorCopy(object->Position, position);
            VectorSubtract(listenerPosition, position, position);
            VectorRotate(position, rotation, position);
            VectorScale(position, 0.004f, position);
            buffer3D->SetPosition(-position[0], 0.f, -position[1], DS3D_IMMEDIATE);
        }
    }
}

HRESULT DirectSoundManager::CreateStaticBuffer(SoundBufferEntry &entry, ESound bufferId,
                                               const wchar_t *filename, bool enable3D)
{
    if (!device_)
    {
        return E_FAIL;
    }

    std::unique_ptr<waveIO> waveFile = std::make_unique<waveIO>(waveIO::Mode::Input);
    if (!waveFile->LoadWaveHeader(filename))
    {
        errorReport_.Write(L"CreateStaticBuffer - Failed to load %ls\r\n", filename);
        return E_FAIL;
    }

    const WAVEFORMATEX format = waveFile->GetWaveFormatEx();
    const DWORD dataSize = waveFile->GetDataSize();
    bufferBytes_ = dataSize;

    std::vector<std::uint8_t> waveData(dataSize);
    HRESULT hr = waveFile->ReadWaveData(reinterpret_cast<char *>(waveData.data()), dataSize);
    if (FAILED(hr))
    {
        errorReport_.Write(L"CreateStaticBuffer - ReadWaveData failed for %ls\r\n", filename);
        return hr;
    }

    DSBUFFERDESC desc{};
    desc.dwSize = sizeof(DSBUFFERDESC);
    desc.dwBufferBytes = dataSize;
    desc.lpwfxFormat = const_cast<WAVEFORMATEX *>(&format);

    if (enable3D)
    {
        desc.dwFlags = DSBCAPS_LOCHARDWARE | DSBCAPS_CTRL3D | DSBCAPS_STATIC;
    }
    else
    {
        desc.dwFlags =
            DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY | DSBCAPS_STATIC;
    }

    IDirectSoundBuffer *rawBuffer = nullptr;
    hr = device_->CreateSoundBuffer(&desc, &rawBuffer, nullptr);
    if (FAILED(hr))
    {
        errorReport_.Write(L"CreateStaticBuffer - CreateSoundBuffer failed for %ls (0x%08X)\r\n",
                           filename, hr);
        return hr;
    }

    entry.buffers[0].reset(rawBuffer);
    if (enable3D)
    {
        IDirectSound3DBuffer *raw3D = nullptr;
        hr = entry.buffers[0]->QueryInterface(IID_IDirectSound3DBuffer,
                                              reinterpret_cast<void **>(&raw3D));
        if (FAILED(hr))
        {
            errorReport_.Write(
                L"CreateStaticBuffer - QueryInterface 3D failed for %ls (0x%08X)\r\n", filename,
                hr);
            entry.buffers[0].reset();
            return hr;
        }

        entry.buffers3D[0].reset(raw3D);
        entry.buffers3D[0]->SetPosition(-2.f, 0.f, 2.f, DS3D_IMMEDIATE);
    }

    entry.waveData = std::move(waveData);
    entry.waveDataSize = dataSize;

    hr = CopyWaveDataToBuffer(entry, bufferId, 0);
    if (FAILED(hr))
    {
        entry.buffers[0].reset();
        entry.buffers3D[0].reset();
        entry.waveData.clear();
        entry.waveDataSize = 0;
        return hr;
    }

    for (int channel = 1; channel < entry.maxChannels; ++channel)
    {
        IDirectSoundBuffer *duplicate = nullptr;
        hr = device_->DuplicateSoundBuffer(entry.buffers[0].get(), &duplicate);
        if (FAILED(hr))
        {
            errorReport_.Write(
                L"CreateStaticBuffer - DuplicateSoundBuffer failed for %ls channel %d (0x%08X)\r\n",
                filename, channel, hr);
            return hr;
        }

        entry.buffers[channel].reset(duplicate);
        if (enable3D)
        {
            IDirectSound3DBuffer *raw3D = nullptr;
            hr = entry.buffers[channel]->QueryInterface(IID_IDirectSound3DBuffer,
                                                        reinterpret_cast<void **>(&raw3D));
            if (FAILED(hr))
            {
                errorReport_.Write(
                    L"CreateStaticBuffer - QueryInterface 3D failed for %ls channel %d (0x%08X)\r\n",
                    filename, channel, hr);
                return hr;
            }

            entry.buffers3D[channel].reset(raw3D);
            entry.buffers3D[channel]->SetPosition(-2.f, 0.f, 2.f, DS3D_IMMEDIATE);
        }
    }

    return S_OK;
}

HRESULT DirectSoundManager::CopyWaveDataToBuffer(SoundBufferEntry &entry, int bufferId, int channel)
{
    if (entry.waveData.empty() || entry.waveDataSize == 0)
    {
        return E_FAIL;
    }

    IDirectSoundBuffer *buffer = GetBuffer(bufferId, channel);
    if (buffer == nullptr)
    {
        return E_FAIL;
    }

    std::uint8_t *part1 = nullptr;
    std::uint8_t *part2 = nullptr;
    DWORD part1Size = 0;
    DWORD part2Size = 0;
    HRESULT hr = buffer->Lock(0, entry.waveDataSize, reinterpret_cast<void **>(&part1), &part1Size,
                              reinterpret_cast<void **>(&part2), &part2Size, 0);
    if (FAILED(hr))
    {
        return hr;
    }

    std::memcpy(part1, entry.waveData.data(), part1Size);
    if (part2 != nullptr && part2Size > 0)
    {
        std::memcpy(part2, entry.waveData.data() + part1Size, part2Size);
    }

    buffer->Unlock(part1, part1Size, part2, part2Size);
    return S_OK;
}

void DirectSoundManager::ResetEntry(SoundBufferEntry &entry)
{
    for (auto &buffer : entry.buffers)
    {
        buffer.reset();
    }
    for (auto &buffer3D : entry.buffers3D)
    {
        buffer3D.reset();
    }
    entry.attachedObjects.fill(nullptr);
    entry.name.fill(L'\0');
    entry.activeChannel = 0;
    entry.maxChannels = 0;
    entry.enable3D = false;
    entry.waveData.clear();
    entry.waveDataSize = 0;
}

void DirectSoundManager::SetVolumeInternal(ESound bufferId, long volume)
{
    // NOTE: Assumes caller holds the mutex.
    const long clamped = std::clamp<long>(volume, DSBVOLUME_MIN, DSBVOLUME_MAX);
    auto &entry = entries_[bufferId];
    for (int channel = 0; channel < entry.maxChannels; ++channel)
    {
        IDirectSoundBuffer *buffer = entry.buffers[channel].get();
        if (buffer != nullptr)
        {
            buffer->SetVolume(clamped);
        }
    }
}

bool DirectSoundManager::IsValidBufferIndex(int bufferId) const noexcept
{
    return bufferId >= 0 && bufferId < MAX_BUFFER;
}

bool DirectSoundManager::IsValidChannelIndex(int channel) const noexcept
{
    return channel >= 0 && channel < MAX_CHANNEL;
}

IDirectSoundBuffer *DirectSoundManager::GetBuffer(int bufferId, int channel) const noexcept
{
    if (!IsValidBufferIndex(bufferId) || !IsValidChannelIndex(channel))
    {
        return nullptr;
    }

    return entries_[bufferId].buffers[channel].get();
}

IDirectSound3DBuffer *DirectSoundManager::Get3DBuffer(int bufferId, int channel) const noexcept
{
    if (!IsValidBufferIndex(bufferId) || !IsValidChannelIndex(channel))
    {
        return nullptr;
    }

    return entries_[bufferId].buffers3D[channel].get();
}

void DirectSoundManager::EnsureCoInitialized()
{
    if (!comInitialized_.exchange(true))
    {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    }
}

void DirectSoundManager::CoUninitializeIfNeeded()
{
    if (comInitialized_.exchange(false))
    {
        CoUninitialize();
    }
}
} // namespace

namespace
{
class DirectSoundEffects final : public LegacyApplicationAudioEffects
{
  public:
    DirectSoundEffects(CErrorReport &errorReport, CmuConsoleDebug &consoleDebug) noexcept
        : manager_(errorReport, consoleDebug)
    {
    }

    bool Initialize(NativeWindowHandle window, MIX_Mixer *) noexcept override
    {
        return SUCCEEDED(manager_.Initialize(static_cast<HWND>(window)));
    }
    void Shutdown() noexcept override
    {
        outputOwner_.reset();
        manager_.Shutdown();
    }
    void ReleaseSession(SessionId id) noexcept override
    {
        if (outputOwner_ == id)
        {
            manager_.StopAll();
            outputOwner_.reset();
        }
    }
    void ApplyGating(SessionAudioBusView &bus) noexcept override
    {
        if (bus.IsMuted() && outputOwner_ == bus.Id())
        {
            manager_.StopAll();
            outputOwner_.reset();
        }
    }
    void SetEnabled(bool enabled) noexcept override
    {
        manager_.SetEnabled(enabled);
        if (!enabled)
            manager_.StopAll();
    }
    void LoadWaveFile(ESound buffer, const wchar_t *fileName, int channels,
                      bool enable3D) noexcept override
    {
        (void)manager_.LoadWaveFile(buffer, fileName, channels, enable3D);
    }
    HRESULT ReleaseBuffer(int) noexcept override
    {
        // Samples are retained for the one application audio lifetime. This
        // prevents one session from invalidating another session's preload.
        return S_OK;
    }
    HRESULT PlayBuffer(SessionAudioBusView &bus, ESound buffer, std::uint64_t,
                       BOOL looped) noexcept override
    {
        if (bus.IsMuted())
            return S_OK;
        if (outputOwner_.has_value() && outputOwner_ != bus.Id())
        {
            manager_.StopAll();
        }
        outputOwner_ = bus.Id();
        return manager_.PlayBuffer(buffer, nullptr, looped != FALSE);
    }
    void StopBuffer(SessionAudioBusView &bus, ESound buffer, BOOL resetPosition) noexcept override
    {
        if (outputOwner_ == bus.Id())
        {
            manager_.StopBuffer(buffer, resetPosition != FALSE);
        }
    }
    void StopAll(SessionAudioBusView &bus) noexcept override
    {
        if (outputOwner_ == bus.Id())
            manager_.StopAll();
    }
    void SetVolume(int buffer, long volume) noexcept override
    {
        manager_.SetVolume(static_cast<ESound>(buffer), volume);
    }
    void SetMasterVolume(long volume) noexcept override
    {
        manager_.SetMasterVolume(volume);
    }
    void UpdateSpatialAudio(SessionAudioBusView &bus, float cameraYaw,
                            const float *listenerPosition) noexcept override
    {
        if (outputOwner_ == bus.Id())
            manager_.Update3DPositions(cameraYaw, listenerPosition);
    }

  private:
    DirectSoundManager manager_;
    std::optional<SessionId> outputOwner_;
};
} // namespace

std::unique_ptr<LegacyApplicationAudioEffects> CreateLegacyApplicationAudioEffects(
    CErrorReport &errorReport, CmuConsoleDebug &consoleDebug)
{
    return std::make_unique<DirectSoundEffects>(errorReport, consoleDebug);
}

#endif // _WIN32 (the non-Windows backend lives in SdlSfxPlayer.cpp)

namespace
{
constexpr int kWaveHeaderSize = 16;
constexpr int kSilentSample8Bit = 128;

void ReportWaveWarning(const wchar_t *message, const wchar_t *filename = nullptr)
{
    if (filename != nullptr)
    {
        (void)std::fwprintf(stderr, L"%ls (%ls)\n", message, filename);
    }
    else
    {
        (void)std::fwprintf(stderr, L"%ls\n", message);
    }
}
} // namespace

waveIO::waveIO(Mode mode)
    : m_hmmio(nullptr), m_wfex{}, m_DataSize(0), m_DataLeft(0), m_SilentSample(0), m_mode(mode)
{
}

waveIO::waveIO() : waveIO(Mode::Input)
{
}

waveIO::waveIO(bool legacyMode) : waveIO(legacyMode ? Mode::Output : Mode::Input)
{
}

waveIO::~waveIO()
{
    CloseWaveFile();
}

bool waveIO::CloseWaveFile()
{
    if (m_hmmio != nullptr)
    {
        mmioClose(m_hmmio, 0);
        m_hmmio = nullptr;
    }
    return true;
}

bool waveIO::IsInputMode() const noexcept
{
    return m_mode == Mode::Input;
}

bool waveIO::IsOutputMode() const noexcept
{
    return m_mode == Mode::Output;
}

bool waveIO::LoadWaveHeader(const wchar_t *filename)
{
    if (filename == nullptr || !IsInputMode())
    {
        return false;
    }

    CloseWaveFile();

    m_hmmio = mmioOpenW(const_cast<wchar_t *>(filename), nullptr, MMIO_ALLOCBUF | MMIO_READ);
    if (m_hmmio == nullptr)
    {
        ReportWaveWarning(L"Cannot find wave file", filename);
        return false;
    }

    MMCKINFO riffChunk{};
    if (mmioDescend(m_hmmio, &riffChunk, nullptr, 0) != 0)
    {
        ReportWaveWarning(L"Cannot descend into RIFF chunk", filename);
        CloseWaveFile();
        return false;
    }

    if (riffChunk.ckid != FOURCC_RIFF || riffChunk.fccType != mmioFOURCC('W', 'A', 'V', 'E'))
    {
        ReportWaveWarning(L"File is not a valid WAVE container", filename);
        CloseWaveFile();
        return false;
    }

    MMCKINFO formatChunk{};
    formatChunk.ckid = mmioFOURCC('f', 'm', 't', ' ');
    if (mmioDescend(m_hmmio, &formatChunk, &riffChunk, MMIO_FINDCHUNK) != 0)
    {
        ReportWaveWarning(L"Failed to locate fmt chunk", filename);
        CloseWaveFile();
        return false;
    }

    if (mmioRead(m_hmmio, reinterpret_cast<char *>(&m_wfex), sizeof(PCMWAVEFORMAT)) == -1)
    {
        ReportWaveWarning(L"Failed to read wave format", filename);
        CloseWaveFile();
        return false;
    }

    if (m_wfex.wBitsPerSample == 8)
    {
        m_SilentSample = kSilentSample8Bit;
    }

    if (m_wfex.wFormatTag != WAVE_FORMAT_PCM)
    {
        ReportWaveWarning(L"Unsupported wave format (expecting PCM)", filename);
        CloseWaveFile();
        return false;
    }

    if (mmioAscend(m_hmmio, &formatChunk, 0) != 0)
    {
        ReportWaveWarning(L"Failed to ascend fmt chunk", filename);
        CloseWaveFile();
        return false;
    }

    MMCKINFO dataChunk{};
    dataChunk.ckid = mmioFOURCC('d', 'a', 't', 'a');
    if (mmioDescend(m_hmmio, &dataChunk, &riffChunk, MMIO_FINDCHUNK) != 0)
    {
        MessageBoxW(nullptr, L"Bad Format in Wave file!", L"WaveLoad", MB_OK | MB_ICONSTOP);
        CloseWaveFile();
        return false;
    }

    m_DataSize = static_cast<int>(dataChunk.cksize);
    m_DataLeft = m_DataSize;
    return true;
}

bool waveIO::ReadWaveData(char *buffer, int bufferSize)
{
    if (!IsInputMode() || buffer == nullptr || bufferSize <= 0 || m_hmmio == nullptr)
    {
        return false;
    }

    int bytesToRead = bufferSize;
    if (m_DataLeft < bufferSize)
    {
        bytesToRead = m_DataLeft;
        std::memset(buffer, 0, static_cast<std::size_t>(bufferSize));
    }

    const int readResult = mmioRead(m_hmmio, buffer, bytesToRead);
    if (readResult < bytesToRead)
    {
        ReportWaveWarning(L"Failed to read expected number of bytes");
        CloseWaveFile();
        return false;
    }

    m_DataLeft -= bytesToRead;
    return true;
}

bool waveIO::WriteWaveData(const char *buffer, int bufferSize)
{
    if (!IsOutputMode() || buffer == nullptr || bufferSize <= 0 || m_hmmio == nullptr)
    {
        return false;
    }

    const int result = mmioWrite(m_hmmio, const_cast<char *>(buffer), bufferSize);
    if (result != bufferSize)
    {
        ReportWaveWarning(L"Failed to write audio data");
        CloseWaveFile();
        return false;
    }
    return true;
}

bool waveIO::WriteWaveHeader(const wchar_t *filename, const PCMWAVEFORMAT &format, int waveDataSize)
{
    if (filename == nullptr || !IsOutputMode())
    {
        return false;
    }

    CloseWaveFile();

    m_hmmio = mmioOpenW(const_cast<wchar_t *>(filename), nullptr, MMIO_CREATE | MMIO_WRITE);
    if (m_hmmio == nullptr)
    {
        ReportWaveWarning(L"Failed to create output wave file", filename);
        return false;
    }

    const int riffSize = 12 + sizeof(PCMWAVEFORMAT) + 8 + waveDataSize;

    if (mmioWrite(m_hmmio, "RIFF", 4) != 4 ||
        mmioWrite(m_hmmio, reinterpret_cast<const char *>(&riffSize), 4) != 4 ||
        mmioWrite(m_hmmio, "WAVE", 4) != 4)
    {
        ReportWaveWarning(L"Failed to write RIFF header", filename);
        CloseWaveFile();
        return false;
    }

    if (mmioWrite(m_hmmio, "fmt ", 4) != 4 ||
        mmioWrite(m_hmmio, reinterpret_cast<const char *>(&kWaveHeaderSize), 4) != 4 ||
        mmioWrite(m_hmmio, reinterpret_cast<const char *>(&format), sizeof(format)) !=
            sizeof(format))
    {
        ReportWaveWarning(L"Failed to write fmt chunk", filename);
        CloseWaveFile();
        return false;
    }

    if (mmioWrite(m_hmmio, "data", 4) != 4 ||
        mmioWrite(m_hmmio, reinterpret_cast<const char *>(&waveDataSize), 4) != 4)
    {
        ReportWaveWarning(L"Failed to write data chunk header", filename);
        CloseWaveFile();
        return false;
    }

    m_wfex = *reinterpret_cast<const WAVEFORMATEX *>(&format);
    m_DataSize = waveDataSize;
    m_DataLeft = waveDataSize;
    return true;
}
// SDL_mixer sound effects (non-Windows)

// platforms without DirectSound (issue #462 follow-up).  It shares the mixer
// device that AudioPlayer creates for music and mirrors the observable
// behavior of the DirectSound backend: one decoded buffer per ESound with a
// small round-robin channel pool, and volumes in DirectSound's hundredths of
// a decibel.  3D spatialization is not implemented Ã¢â‚¬â€ the DirectSound backend
// never enables it either (its 3D flag has no callers), so parity is 2D.

#ifndef _WIN32

namespace
{
// DirectSound volume scale: hundredths of a decibel, 0 (full) .. -10000 (mute).
constexpr long kDsVolumeMax = 0L;
constexpr long kDsVolumeMin = -10000L;
// Hundredths of dB per factor-10 amplitude change (20 dB * 100).
constexpr float kDsCentibelsPerDecade = 2000.0f;

float DsVolumeToGain(long volume)
{
    const long clamped = std::clamp(volume, kDsVolumeMin, kDsVolumeMax);
    if (clamped <= kDsVolumeMin)
    {
        return 0.0f;
    }

    return std::pow(10.0f, static_cast<float>(clamped) / kDsCentibelsPerDecade);
}

struct SfxEntry
{
    MIX_Audio *audio = nullptr;
    std::array<MIX_Track *, MAX_CHANNEL> tracks{};
    int activeChannel = 0;
    int maxChannels = 0;
};

class SdlSfxManager
{
  public:
    SdlSfxManager(CErrorReport &errorReport, CmuConsoleDebug &consoleDebug) noexcept
        : errorReport_(errorReport), consoleDebug_(consoleDebug)
    {
    }
    HRESULT Initialize(MIX_Mixer *mixer);
    void Shutdown();

    void SetEnabled(bool enabled);
    bool IsEnabled() const noexcept;

    HRESULT LoadWaveFile(ESound bufferId, const wchar_t *filename, int maxChannel);
    HRESULT ReleaseBuffer(ESound bufferId);

    HRESULT PlayBuffer(ESound bufferId, bool looped);
    void StopBuffer(ESound bufferId);
    void StopAll();

    void SetVolume(ESound bufferId, long volume);
    void SetMasterVolume(long volume);

  private:
    void ReleaseAllBuffers();
    void ResetEntry(SfxEntry &entry);
    void StopEntryTracks(SfxEntry &entry);
    void SetGainInternal(SfxEntry &entry, float gain);
    bool IsValidBufferIndex(int bufferId) const noexcept;

    mutable std::mutex mutex_;
    MIX_Mixer *mixer_ = nullptr;
    std::array<SfxEntry, MAX_BUFFER> entries_{};
    bool enableSound_ = false;
    float masterGain_ = 1.0f;
    CErrorReport &errorReport_;
    CmuConsoleDebug &consoleDebug_;
};

HRESULT SdlSfxManager::Initialize(MIX_Mixer *mixer)
{
    std::lock_guard lock(mutex_);

    mixer_ = mixer;
    if (mixer_ == nullptr)
    {
        errorReport_.Write(L"InitDirectSound - no SDL mixer available, sound effects disabled\r\n");
        return E_FAIL;
    }

    enableSound_ = true;
    return S_OK;
}

void SdlSfxManager::Shutdown()
{
    std::lock_guard lock(mutex_);

    ReleaseAllBuffers();
    mixer_ = nullptr; // owned by AudioPlayer, which shuts down after us
    enableSound_ = false;
}

void SdlSfxManager::SetEnabled(bool enabled)
{
    enableSound_ = enabled;
}

bool SdlSfxManager::IsEnabled() const noexcept
{
    // Without a mixer there is nothing to play on, and SetEnabled() cannot know
    // that: it may be turned on after a failed Initialize().
    return enableSound_ && mixer_ != nullptr;
}

HRESULT SdlSfxManager::LoadWaveFile(ESound bufferId, const wchar_t *filename, int maxChannel)
{
    if (!IsEnabled())
    {
        return E_FAIL;
    }

    if (!IsValidBufferIndex(bufferId) || filename == nullptr)
    {
        return E_INVALIDARG;
    }

    const int clampedChannels = std::clamp(maxChannel, 1, MAX_CHANNEL);

    std::lock_guard lock(mutex_);
    auto &entry = entries_[bufferId];
    if (entry.maxChannels > 0)
    {
        return S_FALSE;
    }

    // Sound paths are Windows-spelled wide strings (backslashes, mixed case);
    // convert to UTF-8 and resolve against the case-sensitive filesystem.
    char path[4096] = {0};
    if (WideCharToMultiByte(CP_UTF8, 0, filename, -1, path, sizeof(path) - 1, nullptr, nullptr) ==
        0)
    {
        return E_INVALIDARG;
    }

    // Predecode: effects are short and played repeatedly, so trading a little
    // memory for zero decode latency at play time is the right call.
    MIX_Audio *audio = MIX_LoadAudio(mixer_, MuResolvePath(path).c_str(), /*predecode=*/true);
    if (audio == nullptr)
    {
        errorReport_.Write(L"LoadWaveFile - failed to load %ls (%hs)\r\n", filename,
                           SDL_GetError());
        return E_FAIL;
    }

    for (int channel = 0; channel < clampedChannels; ++channel)
    {
        MIX_Track *track = MIX_CreateTrack(mixer_);
        if (track == nullptr || !MIX_SetTrackAudio(track, audio))
        {
            errorReport_.Write(L"LoadWaveFile - failed to create track for %ls (%hs)\r\n", filename,
                               SDL_GetError());
            if (track != nullptr)
            {
                MIX_DestroyTrack(track);
            }
            entry.audio = audio;
            entry.maxChannels = channel; // release only the tracks created so far
            ResetEntry(entry);
            return E_FAIL;
        }

        MIX_SetTrackGain(track, masterGain_);
        entry.tracks[channel] = track;
    }

    entry.audio = audio;
    entry.activeChannel = 0;
    entry.maxChannels = clampedChannels;
    return S_OK;
}

HRESULT SdlSfxManager::ReleaseBuffer(ESound bufferId)
{
    if (!IsValidBufferIndex(bufferId))
    {
        return E_INVALIDARG;
    }

    std::lock_guard lock(mutex_);
    ResetEntry(entries_[bufferId]);
    return S_OK;
}

void SdlSfxManager::ReleaseAllBuffers()
{
    for (auto &entry : entries_)
    {
        ResetEntry(entry);
    }
}

HRESULT SdlSfxManager::PlayBuffer(ESound bufferId, bool looped)
{
    if (!IsEnabled())
    {
        return E_FAIL;
    }

    if (!IsValidBufferIndex(bufferId))
    {
        return E_INVALIDARG;
    }

    std::lock_guard lock(mutex_);
    auto &entry = entries_[bufferId];
    if (entry.maxChannels == 0)
    {
        return E_FAIL;
    }

    const int currentChannel = entry.activeChannel % entry.maxChannels;
    entry.activeChannel = (entry.activeChannel + 1) % entry.maxChannels;

    MIX_Track *track = entry.tracks[currentChannel];
    if (track == nullptr)
    {
        return E_FAIL;
    }

    // DirectSound treats Play() on an already-playing buffer as a no-op, and
    // callers depend on that: the ambient world loops and the blacksmith's
    // hammer are re-requested on every frame.  MIX_PlayTrack restarts the track
    // instead, which would cut the sample a few milliseconds in, sixty times a
    // second, and turn it into a buzz.  Let a running track finish.
    if (MIX_TrackPlaying(track))
    {
        return S_OK;
    }

    // Looping must be requested at play time: starting a stopped track resets
    // the loop count to the MIX_PROP_PLAY_LOOPS_NUMBER option (default 0), so
    // a prior MIX_SetTrackLoops would be overwritten here.
    bool played = false;
    if (looped)
    {
        const SDL_PropertiesID options = SDL_CreateProperties();
        SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
        played = MIX_PlayTrack(track, options);
        SDL_DestroyProperties(options);
    }
    else
    {
        played = MIX_PlayTrack(track, 0);
    }

    if (!played)
    {
        consoleDebug_.Write(MCD_ERROR, L"SdlSfxManager::PlayBuffer failed for %d (%hs)", bufferId,
                            SDL_GetError());
        return E_FAIL;
    }

    return S_OK;
}

void SdlSfxManager::StopBuffer(ESound bufferId)
{
    if (!IsValidBufferIndex(bufferId))
    {
        return;
    }

    std::lock_guard lock(mutex_);
    StopEntryTracks(entries_[bufferId]);
}

void SdlSfxManager::StopAll()
{
    // One lock for the whole sweep: taking it per buffer would mean a thousand
    // lock/unlock pairs for a single map change.
    std::lock_guard lock(mutex_);
    for (auto &entry : entries_)
    {
        StopEntryTracks(entry);
    }
}

void SdlSfxManager::SetVolume(ESound bufferId, long volume)
{
    if (!IsValidBufferIndex(bufferId))
    {
        return;
    }

    std::lock_guard lock(mutex_);
    SetGainInternal(entries_[bufferId], DsVolumeToGain(volume));
}

void SdlSfxManager::SetMasterVolume(long volume)
{
    const float gain = DsVolumeToGain(volume);

    std::lock_guard lock(mutex_);
    masterGain_ = gain;
    for (auto &entry : entries_)
    {
        SetGainInternal(entry, gain);
    }
}

void SdlSfxManager::ResetEntry(SfxEntry &entry)
{
    // Tracks hold a pointer to the audio, so destroy them before it.
    for (int channel = 0; channel < entry.maxChannels; ++channel)
    {
        if (entry.tracks[channel] != nullptr)
        {
            MIX_DestroyTrack(entry.tracks[channel]);
            entry.tracks[channel] = nullptr;
        }
    }

    if (entry.audio != nullptr)
    {
        MIX_DestroyAudio(entry.audio);
        entry.audio = nullptr;
    }

    entry.activeChannel = 0;
    entry.maxChannels = 0;
}

void SdlSfxManager::StopEntryTracks(SfxEntry &entry)
{
    // NOTE: Assumes caller holds the mutex.
    for (int channel = 0; channel < entry.maxChannels; ++channel)
    {
        if (entry.tracks[channel] != nullptr)
        {
            MIX_StopTrack(entry.tracks[channel], 0);
        }
    }
}

void SdlSfxManager::SetGainInternal(SfxEntry &entry, float gain)
{
    // NOTE: Assumes caller holds the mutex.
    for (int channel = 0; channel < entry.maxChannels; ++channel)
    {
        if (entry.tracks[channel] != nullptr)
        {
            MIX_SetTrackGain(entry.tracks[channel], gain);
        }
    }
}

bool SdlSfxManager::IsValidBufferIndex(int bufferId) const noexcept
{
    return bufferId >= 0 && bufferId < MAX_BUFFER;
}
} // namespace

namespace
{
class SdlSoundEffects final : public LegacyApplicationAudioEffects
{
  public:
    SdlSoundEffects(CErrorReport &errorReport, CmuConsoleDebug &consoleDebug) noexcept
        : manager_(errorReport, consoleDebug)
    {
    }
    bool Initialize(NativeWindowHandle, MIX_Mixer *mixer) noexcept override
    {
        return SUCCEEDED(manager_.Initialize(mixer));
    }
    void Shutdown() noexcept override
    {
        outputOwner_.reset();
        manager_.Shutdown();
    }
    void ReleaseSession(SessionId id) noexcept override
    {
        if (outputOwner_ == id)
        {
            manager_.StopAll();
            outputOwner_.reset();
        }
    }
    void ApplyGating(SessionAudioBusView &bus) noexcept override
    {
        if (bus.IsMuted() && outputOwner_ == bus.Id())
        {
            manager_.StopAll();
            outputOwner_.reset();
        }
    }
    void SetEnabled(bool enabled) noexcept override
    {
        manager_.SetEnabled(enabled);
        if (!enabled)
            manager_.StopAll();
    }
    void LoadWaveFile(ESound buffer, const wchar_t *fileName, int channels, bool) noexcept override
    {
        (void)manager_.LoadWaveFile(buffer, fileName, channels);
    }
    HRESULT ReleaseBuffer(int) noexcept override
    {
        return S_OK;
    }
    HRESULT PlayBuffer(SessionAudioBusView &bus, ESound buffer, std::uint64_t,
                       BOOL looped) noexcept override
    {
        if (bus.IsMuted())
            return S_OK;
        if (outputOwner_.has_value() && outputOwner_ != bus.Id())
        {
            manager_.StopAll();
        }
        outputOwner_ = bus.Id();
        return manager_.PlayBuffer(buffer, looped != FALSE);
    }
    void StopBuffer(SessionAudioBusView &bus, ESound buffer, BOOL) noexcept override
    {
        if (outputOwner_ == bus.Id())
            manager_.StopBuffer(buffer);
    }
    void StopAll(SessionAudioBusView &bus) noexcept override
    {
        if (outputOwner_ == bus.Id())
            manager_.StopAll();
    }
    void SetVolume(int buffer, long volume) noexcept override
    {
        manager_.SetVolume(static_cast<ESound>(buffer), volume);
    }
    void SetMasterVolume(long volume) noexcept override
    {
        manager_.SetMasterVolume(volume);
    }
    void UpdateSpatialAudio(SessionAudioBusView &, float) noexcept override
    {
    }

  private:
    SdlSfxManager manager_;
    std::optional<SessionId> outputOwner_;
};
} // namespace

std::unique_ptr<LegacyApplicationAudioEffects> CreateLegacyApplicationAudioEffects(
    CErrorReport &errorReport, CmuConsoleDebug &consoleDebug)
{
    return std::make_unique<SdlSoundEffects>(errorReport, consoleDebug);
}

#endif // !_WIN32
// Non-Windows implementation of the mmio RIFF reader/writer.
// Backs the WAV loader with buffered stdio file access.
#ifndef _WIN32

namespace
{
struct MuMmio
{
    FILE *fp;
};

FOURCC ReadFourCC(const unsigned char *p)
{
    return static_cast<FOURCC>(p[0]) | (static_cast<FOURCC>(p[1]) << 8) |
           (static_cast<FOURCC>(p[2]) << 16) | (static_cast<FOURCC>(p[3]) << 24);
}
} // namespace

HMMIO mmioOpenW(wchar_t *szFilename, void * /*lpmmioinfo*/, DWORD dwOpenFlags)
{
    if (!szFilename)
        return nullptr;

    char path[4096] = {0};
    if (std::wcstombs(path, szFilename, sizeof(path) - 1) == static_cast<size_t>(-1))
        return nullptr;

    const char *mode = (dwOpenFlags & MMIO_WRITE) ? "wb" : "rb";
    FILE *fp = std::fopen(MuResolvePath(path).c_str(), mode);
    if (!fp)
        return nullptr;

    try
    {
        return static_cast<HMMIO>(new MuMmio{fp});
    }
    catch (...) // don't leak the file if the (tiny) allocation throws
    {
        std::fclose(fp);
        return nullptr;
    }
}

int mmioClose(HMMIO hmmio, UINT /*wFlags*/)
{
    auto *h = static_cast<MuMmio *>(hmmio);
    if (!h)
        return 0;
    if (h->fp)
        std::fclose(h->fp);
    delete h;
    return 0;
}

int mmioRead(HMMIO hmmio, char *pch, int cch)
{
    auto *h = static_cast<MuMmio *>(hmmio);
    if (!h || !h->fp || !pch || cch < 0)
        return -1;
    return static_cast<int>(std::fread(pch, 1, static_cast<size_t>(cch), h->fp));
}

int mmioWrite(HMMIO hmmio, const char *pch, int cch)
{
    auto *h = static_cast<MuMmio *>(hmmio);
    if (!h || !h->fp || !pch || cch < 0)
        return -1;
    return static_cast<int>(std::fwrite(pch, 1, static_cast<size_t>(cch), h->fp));
}

// Read the next chunk header (or search for a specific chunk with MMIO_FINDCHUNK
// / MMIO_FINDRIFF), leaving the file positioned at the start of the chunk data.
int mmioDescend(HMMIO hmmio, MMCKINFO *lpck, const MMCKINFO *lpckParent, UINT wFlags)
{
    auto *h = static_cast<MuMmio *>(hmmio);
    if (!h || !h->fp || !lpck)
        return 1;

    const FOURCC wantId = lpck->ckid;      // MMIO_FINDCHUNK
    const FOURCC wantType = lpck->fccType; // MMIO_FINDRIFF

    // Search limit: the end of the parent chunk's data, or unbounded at top level.
    // All offset math below is unsigned 64-bit so corrupted chunk sizes can never
    // overflow into a negative seek or skip a chunk backwards.
    const unsigned long long parentEnd =
        lpckParent ? static_cast<unsigned long long>(lpckParent->dwDataOffset) + lpckParent->cksize
                   : ~0ull;

    for (;;)
    {
        const long pos = std::ftell(h->fp);
        if (pos < 0)
            return 1;
        const unsigned long long start = static_cast<unsigned long long>(pos);

        if (start + 8 > parentEnd)
            return 1; // header must fit within the parent

        unsigned char header[8];
        if (std::fread(header, 1, sizeof(header), h->fp) != sizeof(header))
            return 1; // EOF

        lpck->ckid = ReadFourCC(header);
        lpck->cksize = ReadFourCC(header + 4); // little-endian u32
        lpck->fccType = 0;
        lpck->dwFlags = 0;
        lpck->dwDataOffset = static_cast<DWORD>(start + 8);

        if (lpck->ckid == FOURCC_RIFF || lpck->ckid == FOURCC_LIST)
        {
            if (start + 12 > parentEnd)
                return 1; // form type must fit too
            unsigned char formType[4];
            if (std::fread(formType, 1, sizeof(formType), h->fp) != sizeof(formType))
                return 1;
            lpck->fccType = ReadFourCC(formType);
            lpck->dwDataOffset = static_cast<DWORD>(start + 12);
        }

        bool match;
        if (wFlags & MMIO_FINDCHUNK)
            match = (lpck->ckid == wantId);
        else if (wFlags & MMIO_FINDRIFF)
            match = (lpck->ckid == FOURCC_RIFF && lpck->fccType == wantType);
        else
            match = true;

        if (match)
        {
            std::fseek(h->fp, static_cast<long>(lpck->dwDataOffset), SEEK_SET);
            return 0;
        }

        // Skip to the next chunk; RIFF chunks are word-aligned (pad byte if odd).
        const unsigned long long aligned =
            static_cast<unsigned long long>(lpck->cksize) + (lpck->cksize & 1u);
        const unsigned long long next = start + 8 + aligned; // always advances by >= 8
        if (next > parentEnd)
            return 1; // runs past parent
        if (next > static_cast<unsigned long long>(LONG_MAX))
            return 1; // beyond seekable range
        if (std::fseek(h->fp, static_cast<long>(next), SEEK_SET) != 0)
            return 1;
    }
}

// Seek past the end of the current chunk's data (word-aligned). Used on plain
// data chunks (the fmt chunk), not on the RIFF container.
int mmioAscend(HMMIO hmmio, MMCKINFO *lpck, UINT /*wFlags*/)
{
    auto *h = static_cast<MuMmio *>(hmmio);
    if (!h || !h->fp || !lpck)
        return 1;

    const unsigned long long aligned =
        static_cast<unsigned long long>(lpck->cksize) + (lpck->cksize & 1u);
    const unsigned long long end = static_cast<unsigned long long>(lpck->dwDataOffset) + aligned;
    if (end > static_cast<unsigned long long>(LONG_MAX))
        return 1;
    return (std::fseek(h->fp, static_cast<long>(end), SEEK_SET) == 0) ? 0 : 1;
}

#endif // !_WIN32

namespace AudioPlayer
{
int ClampVolume(int level)
{
    if (level < MinVolumeLevel || level > MaxVolumeLevel)
    {
        return DefaultVolumeLevel;
    }
    return level;
}
} // namespace AudioPlayer

LegacyApplicationAudioDevice::~LegacyApplicationAudioDevice() = default;

LegacyApplicationAudioDevice::MusicState &LegacyApplicationAudioDevice::Music(
    SessionAudioBusView &bus)
{
    auto [entry, inserted] = music_.try_emplace(bus.Id());
    if (inserted)
    {
        entry->second = std::make_unique<MusicState>();
        entry->second->muted = bus.IsMuted();
        if (mixer_ != nullptr)
        {
            entry->second->track = MIX_CreateTrack(mixer_);
        }
    }
    return *entry->second;
}

void LegacyApplicationAudioDevice::ReleaseMusic(MusicState &music) noexcept
{
    if (music.track != nullptr)
    {
        MIX_DestroyTrack(music.track);
        music.track = nullptr;
    }
    if (music.audio != nullptr)
    {
        MIX_DestroyAudio(music.audio);
        music.audio = nullptr;
    }
    music.path.clear();
}

void LegacyApplicationAudioDevice::InitializeMusic(int masterMusicVolume) noexcept
{
    if (mixer_ != nullptr)
    {
        return;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO) || !MIX_Init())
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }
    mixer_ = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (mixer_ == nullptr)
    {
        MIX_Quit();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }
    SetMasterMusicVolume(masterMusicVolume);
}

void LegacyApplicationAudioDevice::ShutdownMusic() noexcept
{
    for (auto &[id, music] : music_)
    {
        (void)id;
        ReleaseMusic(*music);
    }
    music_.clear();
    if (mixer_ != nullptr)
    {
        MIX_DestroyMixer(mixer_);
        mixer_ = nullptr;
    }
    MIX_Quit();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void LegacyApplicationAudioDevice::SetMasterMusicVolume(int level) noexcept
{
    masterMusicVolume_ = AudioPlayer::ClampVolume(level);
    const float masterGain =
        static_cast<float>(masterMusicVolume_) / static_cast<float>(AudioPlayer::MaxVolumeLevel);
    for (auto &[id, music] : music_)
    {
        (void)id;
        if (music->track != nullptr)
        {
            MIX_SetTrackGain(music->track, music->muted ? 0.0f : masterGain);
        }
    }
}

void LegacyApplicationAudioDevice::ReleaseSessionAudio(SessionId id) noexcept
{
    const auto music = music_.find(id);
    if (music != music_.end())
    {
        ReleaseMusic(*music->second);
        music_.erase(music);
    }
    if (effects_ != nullptr)
    {
        effects_->ReleaseSession(id);
    }
}

void LegacyApplicationAudioDevice::ApplySessionAudioGating(SessionAudioBusView &bus) noexcept
{
    MusicState &music = Music(bus);
    music.muted = bus.IsMuted();
    if (music.track != nullptr)
    {
        const float gain = music.muted ? 0.0f
                                       : static_cast<float>(masterMusicVolume_) /
                                             static_cast<float>(AudioPlayer::MaxVolumeLevel);
        MIX_SetTrackGain(music.track, gain);
    }
    if (effects_ != nullptr)
    {
        effects_->ApplyGating(bus);
    }
}

void LegacyApplicationAudioDevice::StopMusic(SessionAudioBusView &bus) noexcept
{
    const auto found = music_.find(bus.Id());
    if (found == music_.end())
        return;
    MusicState &music = *found->second;
    if (music.track != nullptr)
    {
        MIX_StopTrack(music.track, 0);
    }
}

void LegacyApplicationAudioDevice::StopMp3(SessionAudioBusView &bus, const char *name,
                                           BOOL) noexcept
{
    if (name == nullptr)
    {
        return;
    }
    MusicState &music = Music(bus);
    if (music.track == nullptr || music.path != name)
    {
        return;
    }
    MIX_StopTrack(music.track, 0);
    MIX_SetTrackAudio(music.track, nullptr);
    if (music.audio != nullptr)
    {
        MIX_DestroyAudio(music.audio);
        music.audio = nullptr;
    }
    music.path.clear();
}

void LegacyApplicationAudioDevice::PlayMp3(SessionAudioBusView &bus, const char *name,
                                           BOOL) noexcept
{
    if (name == nullptr || mixer_ == nullptr)
    {
        return;
    }
    MusicState &music = Music(bus);
    if (music.track == nullptr || music.path == name)
    {
        return;
    }
#ifdef _WIN32
    MIX_Audio *audio = MIX_LoadAudio(mixer_, name, false);
#else
    MIX_Audio *audio = MIX_LoadAudio(mixer_, MuResolvePath(name).c_str(), false);
#endif
    if (audio == nullptr || !MIX_SetTrackAudio(music.track, audio))
    {
        if (audio != nullptr)
            MIX_DestroyAudio(audio);
        return;
    }
    MIX_StopTrack(music.track, 0);
    if (music.audio != nullptr)
        MIX_DestroyAudio(music.audio);
    music.audio = audio;
    music.path = name;
    ApplySessionAudioGating(bus);
    if (!MIX_PlayTrack(music.track, 0))
    {
        music.path.clear();
    }
}

bool LegacyApplicationAudioDevice::IsEndMp3(SessionAudioBusView &bus) noexcept
{
    MusicState &music = Music(bus);
    return music.track != nullptr && music.audio != nullptr && !MIX_TrackPlaying(music.track);
}

int LegacyApplicationAudioDevice::GetMp3PlayPosition(SessionAudioBusView &bus) noexcept
{
    MusicState &music = Music(bus);
    if (music.track == nullptr || music.audio == nullptr)
    {
        return 0;
    }
    const Sint64 duration = MIX_GetAudioDuration(music.audio);
    if (duration <= 0)
    {
        return 0;
    }
    return static_cast<int>((MIX_GetTrackPlaybackPosition(music.track) * 100) / duration);
}
void ApplicationAudio::SetEffectVolumeLevel(int level) noexcept
{
    if (level > AudioPlayer::MaxVolumeLevel)
        level = AudioPlayer::MaxVolumeLevel;
    if (level < 0)
        level = 0;

    if (level == 0)
    {
        SetMasterVolume(-10000);
    }
    else
    {
        long vol = -2000 * log10(10.f / float(level));
        SetMasterVolume(vol);
    }
}

void ApplicationLegacyCalls::SetEffectVolumeLevel(int level)
{
    applicationKeeper_.ApplicationAudioUnit()->SetEffectVolumeLevel(level);
}
