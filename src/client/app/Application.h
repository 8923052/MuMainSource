#pragma once
#include "support/CoreMath.h"
#include "render/Assets.h"

#include <cstdint>
#include <span>
#include <string>

namespace ItemRulesDetail
{
struct GroundItemLabelDescriptor;
}

class ApplicationKeeper;
class CErrorReport;
class CGlobalBitmap;
class CInput;
class CMultiLanguage;
struct ITEM_ATTRIBUTE;
struct SKILL_ATTRIBUTE;
struct _EXCEPTION_POINTERS;
class CHARACTER;
struct FontSizes;
struct tagITEM;
typedef struct tagITEM ITEM;
struct tagMONSTER;
typedef struct tagMONSTER MONSTER;
class OBJECT;
struct SDL_KeyboardEvent;
union SDL_Event;

enum EMonsterType : int;
enum ESound : int;

namespace FrameProfiler
{
enum class Pass : int;
}
namespace Core::Time
{
class FrameTimerScheduler;
}

namespace leaf
{
enum COLOR_INDEX
{
    COLOR_BLACK = 0,

    COLOR_DARKBLUE = FOREGROUND_BLUE,
    COLOR_DARKGREEN = FOREGROUND_GREEN,
    COLOR_DARKRED = FOREGROUND_RED,
    COLOR_INTENSITY = FOREGROUND_INTENSITY,

    COLOR_BLUE = COLOR_DARKBLUE | COLOR_INTENSITY,
    COLOR_GREEN = COLOR_DARKGREEN | COLOR_INTENSITY,
    COLOR_RED = COLOR_DARKRED | COLOR_INTENSITY,
    COLOR_OLIVE = COLOR_DARKRED | COLOR_DARKGREEN,
    COLOR_TEAL = COLOR_DARKGREEN | COLOR_DARKBLUE,
    COLOR_PURPLE = COLOR_DARKRED | COLOR_DARKBLUE,
    COLOR_GRAY = COLOR_DARKRED | COLOR_DARKGREEN | COLOR_DARKBLUE,
    COLOR_YELLOW = COLOR_OLIVE | COLOR_INTENSITY,
    COLOR_AQUA = COLOR_TEAL | COLOR_INTENSITY,
    COLOR_FUCHSIA = COLOR_PURPLE | COLOR_INTENSITY,
    COLOR_WHITE = COLOR_GRAY | COLOR_INTENSITY,

    COLOR_ERROR = 0xFFFF
};
} // namespace leaf

class ApplicationSupportCalls
{
  protected:
    explicit ApplicationSupportCalls(ApplicationKeeper &keeper) noexcept;

    bool GetLogicalMousePosition(int &x, int &y) const;
    void SetKeyState(int virtualKey, int state);
    CInput &Input();
    bool LoadItemDataFile(wchar_t *fileName, CErrorReport &errorReport, HWND window);
    bool LoadSkillDataFile(wchar_t *fileName, CErrorReport &errorReport, HWND window);

    ApplicationKeeper &applicationKeeper_;
};

class ApplicationLegacyCalls : protected ApplicationSupportCalls
{
  protected:
    explicit ApplicationLegacyCalls(ApplicationKeeper &keeper) noexcept;

    int GetFPSLimit() const;
    bool ExceptionCallback(_EXCEPTION_POINTERS *exceptionInfo);
    void RecordCpuUsage();
    void SetMaxMessagePerCycle(int messages);
    FontSizes CalculateFontSizes();
    HFONT CreateUIFont(int size, int weight);
    void CreateNewFonts(FontSizes sizes);
    void ReinitializeTextRenderer();
    void HandleWindowResize(int width, int height);
    void HandleFocusChange(bool active);
    void MuApplyWindowResolution(unsigned int width, unsigned int height, bool windowed);
    void RegisterBundledFonts();
    void UnregisterBundledFonts();
    void ReinitializeFonts();
    void UpdateResolutionDependentSystems();
    void UpdateCursorClip();
    MSG MainLoop();
    void DestroyWindow();
    LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam); // OMF-00014
    bool Win32MessageHook(void *userdata, MSG *message);                               // OMF-00020
    void HandleMouseMotion(float windowX, float windowY);                              // OMF-00021
    void FeedPortableTextInput(const char *utf8);                                      // OMF-00028
    bool FeedPortableKey(const SDL_KeyboardEvent &key);                                // OMF-00029
    void HandleMouseButton(const SDL_Event &event);
    void CheckHack();
    bool IsKeyDown(int virtualKey) const;    // OMF-00146
    bool PressKey(int key);                  // OMF-00437
    bool IsNone(int virtualKey) const;       // OMF-01977
    bool IsRelease(int virtualKey) const;    // OMF-01978
    bool IsPress(int virtualKey) const;      // OMF-01979
    bool IsRepeat(int virtualKey) const;     // OMF-01980
    bool IsEnterPressed();                   // OMF-01812
    void SetEnterPressed(bool enterPressed); // OMF-01813
    void SaveIMEStatus();                    // OMF-01869
    void RestoreIMEStatus();                 // OMF-01870
    void CheckTextInputBoxIME(int mode);     // OMF-01871
    int CBTMessageBox(HWND window, const std::wstring &text, const std::wstring &caption, UINT type,
                      bool alwaysOnTop = false); // OMF-01992
    void InitVSync();
    bool IsVSyncAvailable();
    bool IsVSyncEnabled();
    const wchar_t *SdlGpuDriverName() const noexcept;
    void EnableVSync();
    void DisableVSync();
    void DeleteBitmap(unsigned int textureIndex, bool force = false); // OMF-01760
    bool LoadBitmapW(const wchar_t *fileName, std::uint32_t textureIndex,
                     LegacyTextureFilter filter = LegacyTextureFilter::Nearest,
                     LegacyTextureWrap wrapMode = LegacyTextureWrap::ClampToEdge, bool check = true,
                     bool fullPath = false);
    void PopUpErrorCheckMsgBox(const wchar_t *errorMessage, bool forceDestroy = false);
    HRESULT InitDirectSound(HWND window);
    void FreeDirectSound();
    float &AccumulatorMs(FrameProfiler::Pass pass);
    void ResetFrame();
    bool OpenConsoleWindow(const std::wstring &title);
    void CloseConsoleWindow();
    bool SetConsoleTitleW(const std::wstring &title);
    const std::wstring &GetConsoleTitleW();
    HWND GetConsoleWndHandle();
    bool IsConsoleVisible();
    void ShowConsole(bool show = true);
    void ClearConsoleScreen();
    WORD GetConsoleTextColorIndex(WORD *background = nullptr);
    void SetConsoleTextColor(WORD text = leaf::COLOR_WHITE, WORD background = leaf::COLOR_BLACK);
    void ActivateCloseButton(bool active = true);
    bool IsActiveCloseButton();
    bool SaveConsoleScreenBuffer(const std::wstring &filename);
    void ReportDotNetError(const char *detail);
    bool IsManagedLibraryAvailable();
    void OpenFilterFile(const wchar_t *fileName);
    void OpenNameFilterFile(const wchar_t *fileName);
    void OpenGateScript(const wchar_t *fileName);
    void OpenMonsterSkillScript(const wchar_t *fileName);
    void OpenMonsterScript(wchar_t *fileName);
    void CreateClassAttribute(int characterClass, int strength, int dexterity, int vitality,
                              int energy, int life, int mana, int levelLife, int levelMana,
                              int vitalityToLife, int energyToMana);
    void CreateClassAttributes();
    void SetShowDebugInfo(bool enabled);
    void SetShowFpsCounter(bool enabled);
    void ResetFrameStats();
    void SetTargetFps(double targetFps);
    double GetTargetFps() const;
    void LoadWaveFile(ESound buffer, const wchar_t *fileName, int channels = 4,
                      bool enable3D = false);
    HRESULT ReleaseBuffer(int buffer);
    void SetMasterVolume(long volume);
    void SetEffectVolumeLevel(int level);

    CGlobalBitmap &Bitmaps;
    int (&KeyState)[256];
    int &FontHeight;
    bool &First;
    int &FirstTime;
    DWORD &g_dwBKConv;
    DWORD &g_dwBKSent;
    BOOL &g_bForceIMEConv;
    BOOL &g_bForceIMESent;
    BOOL &g_bIMEBlock;
    int &g_iChatInputType;
    CMultiLanguage *&pMultiLanguage;
    float &g_fScreenRate_x;
    float &g_fScreenRate_y;
    bool &g_bShowPath;
    Core::Time::FrameTimerScheduler &frameTimerScheduler_;
    BYTE *&RendomMemoryDump;
    ITEM_ATTRIBUTE *&ItemAttRibuteMemoryDump;
    SKILL_ATTRIBUTE *&SkillAttribute;
    ITEM_ATTRIBUTE *&ItemAttribute;
    int (&RandomTable)[100];
    std::wstring &bootstrapServerIp;
    WORD &bootstrapServerPort;
    int &m_SoundOnOff;
    int &m_MusicOnOff;
    wchar_t (&g_lpszCmdURL)[50];
    float &g_LoginSceneOffsetX;
    float &g_LoginSceneOffsetY;
    float &g_LoginSceneOffsetZ;
    float &g_LoginSceneAnglePitch;
    float &g_LoginSceneAngleYaw;
    void SetEnableSound(bool enabled) noexcept;           // OMF-00063
    void SetVolume(int buffer, long volume) noexcept;     // OMF-00071
    void WaitForNextActivity(bool usePreciseSleep) const; // OMF-01814
    void UpdateFrameStats() noexcept;                     // OMF-01819
};

// Platform bootstrap owns the complete composition within Application.cpp.
int RunApplication(HINSTANCE instance);
