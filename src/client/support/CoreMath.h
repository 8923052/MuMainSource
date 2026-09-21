#pragma once

#pragma warning(disable : 4067)
#pragma warning(disable : 4786)
#pragma warning(disable : 4800)
#pragma warning(disable : 4996)
#pragma warning(disable : 4244)
#pragma warning(disable : 4237)
#pragma warning(disable : 4305)
#pragma warning(disable : 4503)
#pragma warning(disable : 4267)
#pragma warning(disable : 4091)
#pragma warning(disable : 4819)
#pragma warning(disable : 4505)
#pragma warning(disable : 4100)
#pragma warning(disable : 4127)
#pragma warning(disable : 4702)
#pragma warning(disable : 4838)
#pragma warning(disable : 5208)
#pragma warning(disable : 28159)
#pragma warning(disable : 26812)

#ifndef NOMINMAX
#define NOMINMAX
#endif

// Exclude rarely-used stuff from Windows headers
#define WIN32_LEAN_AND_MEAN

// Use 32-bit time_t only for 32-bit MSVC builds. This avoids conflicts
// with 64-bit toolchains such as x86_64-w64-mingw32, which require
// 64-bit time_t.
#if defined(_MSC_VER) && !defined(_WIN64)
#ifndef _USE_32BIT_TIME_T
#define _USE_32BIT_TIME_T
#endif //_USE_32BIT_TIME_T
#endif

#define _CRT_SECURE_NO_DEPRECATE
#define _CRT_NONSTDC_NO_DEPRECATE

// Cross-platform debug breakpoint helper.
#ifdef _MSC_VER
#define MU_DEBUG_BREAK() __debugbreak()
#else
#define MU_DEBUG_BREAK() __builtin_trap()
#endif

#pragma warning(push, 3)

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>

// MinGW workaround: Allow swprintf usage
#if defined(__MINGW32__) || defined(__MINGW64__)
#undef swprintf
#endif

#include <mmsystem.h>
#include <shellapi.h>
#include <tchar.h>
#include <mbstring.h>
#include <conio.h>

#else
#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <pthread.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <assert.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <deque>
#include <errno.h>
#include <functional>
#include <limits>
#include <list>
#include <malloc.h>
#include <map>
#include <math.h>
#include <memory>
#include <memory.h>
#include <optional>
#include <queue>
#include <stdarg.h>
#include <stdexcept>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <thread>
#include <time.h>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#pragma warning(pop)

// Win32 type compatibility shim.
// A handful of widely-included engine headers reference Win32 scalar types,
// handles and small structs in their declarations. On Windows this comes from
// <windows.h>; this header funnels that through one place so the engine can be
// parsed by a non-Windows toolchain.
//   - On Windows: include <windows.h> unchanged (zero behavioral change).
//   - Elsewhere: provide just the Win32 *types* the engine declarations need.
// This only unblocks parsing. Win32 function calls and the wchar_t width
// difference are handled elsewhere; this header deliberately does not try to
// emulate the Win32 API.

#ifndef _WIN32

// Fixed-width scalar aliases. Widths match the Windows definitions (DWORD/LONG
// are 32-bit there, unlike `long` on LP64 Linux) so struct layouts stay stable.
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint32_t UINT;
typedef int32_t INT;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef int16_t SHORT;
typedef uint16_t USHORT;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
typedef uint64_t DWORDLONG;
typedef int BOOL;
typedef float FLOAT;
typedef char CHAR;
typedef unsigned char UCHAR;
typedef wchar_t
    WCHAR; // 32-bit here vs 16-bit on Windows; width handled at the .NET interop boundary (#462).
typedef void VOID;
typedef DWORD COLORREF;

// MSVC fixed-width keyword aliases (gcc/clang lack these). Defined as macros,
// not typedefs, so the common `unsigned __int64` form stays valid.
#ifndef __int64
#define __int64 long long
#endif
#ifndef __int32
#define __int32 int
#endif
#ifndef __int16
#define __int16 short
#endif
#ifndef __int8
#define __int8 char
#endif

// Pointer-sized message/param/int types.
typedef uintptr_t UINT_PTR;
typedef intptr_t INT_PTR;
typedef intptr_t LONG_PTR;
typedef uintptr_t ULONG_PTR;
typedef uintptr_t DWORD_PTR;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef LONG HRESULT;
typedef BYTE BOOLEAN;

// TCHAR maps to the wide character set (the engine builds UNICODE).
typedef WCHAR TCHAR;
typedef WCHAR *LPTSTR;
typedef const WCHAR *LPCTSTR;
#ifndef _T
#define _T(x) L##x
#endif
#ifndef TEXT
#define TEXT(x) L##x
#endif

// Opaque handles. Each is a distinct incompatible pointer type (as on Windows
// via DECLARE_HANDLE) so overloads on different handle types don't collapse.
#define DECLARE_HANDLE(name)                                                                       \
    struct name##__                                                                                \
    {                                                                                              \
        int unused;                                                                                \
    };                                                                                             \
    typedef struct name##__ *name
typedef void *HANDLE;
DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HGLRC);
DECLARE_HANDLE(HINSTANCE);
DECLARE_HANDLE(HMODULE);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HCURSOR);
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HBITMAP);
DECLARE_HANDLE(HGDIOBJ);
DECLARE_HANDLE(HKEY);
DECLARE_HANDLE(HIMC);

// Pointer aliases.
typedef void *PVOID;
typedef void *LPVOID;
typedef const void *LPCVOID;
typedef CHAR *LPSTR;
typedef CHAR *PSTR;
typedef const CHAR *LPCSTR;
typedef WCHAR *LPWSTR;
typedef const WCHAR *LPCWSTR;
typedef BYTE *LPBYTE;
typedef WORD *LPWORD;
typedef DWORD *LPDWORD;
typedef LONG *LPLONG;
typedef BOOL *LPBOOL;

// Small structs used in declarations.
typedef struct tagPOINT
{
    LONG x, y;
} POINT, *LPPOINT;
typedef const POINT *LPCPOINT;
typedef struct tagSIZE
{
    LONG cx, cy;
} SIZE, *LPSIZE;
typedef struct tagRECT
{
    LONG left, top, right, bottom;
} RECT, *LPRECT;
typedef const RECT *LPCRECT;

// SEH record, only ever used as an opaque pointer parameter off Windows.
struct _EXCEPTION_POINTERS;
typedef struct _EXCEPTION_POINTERS *PEXCEPTION_POINTERS;

// Async-I/O struct referenced by a few function declarations (minwinbase.h).
typedef struct _OVERLAPPED
{
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    union {
        struct
        {
            DWORD Offset;
            DWORD OffsetHigh;
        };
        PVOID Pointer;
    };
    HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;

// Bitwise operators for scoped flag enums (winnt.h). The engine's flag enums
// invoke this right after their definition; off Windows we provide the same
// operator set, deducing the integer width from the enum's underlying type.
#define DEFINE_ENUM_FLAG_OPERATORS(ENUMTYPE)                                                       \
    inline constexpr ENUMTYPE operator|(ENUMTYPE a, ENUMTYPE b)                                    \
    {                                                                                              \
        using T = std::underlying_type_t<ENUMTYPE>;                                                \
        return static_cast<ENUMTYPE>(static_cast<T>(a) | static_cast<T>(b));                       \
    }                                                                                              \
    inline ENUMTYPE &operator|=(ENUMTYPE &a, ENUMTYPE b)                                           \
    {                                                                                              \
        a = a | b;                                                                                 \
        return a;                                                                                  \
    }                                                                                              \
    inline constexpr ENUMTYPE operator&(ENUMTYPE a, ENUMTYPE b)                                    \
    {                                                                                              \
        using T = std::underlying_type_t<ENUMTYPE>;                                                \
        return static_cast<ENUMTYPE>(static_cast<T>(a) & static_cast<T>(b));                       \
    }                                                                                              \
    inline ENUMTYPE &operator&=(ENUMTYPE &a, ENUMTYPE b)                                           \
    {                                                                                              \
        a = a & b;                                                                                 \
        return a;                                                                                  \
    }                                                                                              \
    inline constexpr ENUMTYPE operator^(ENUMTYPE a, ENUMTYPE b)                                    \
    {                                                                                              \
        using T = std::underlying_type_t<ENUMTYPE>;                                                \
        return static_cast<ENUMTYPE>(static_cast<T>(a) ^ static_cast<T>(b));                       \
    }                                                                                              \
    inline ENUMTYPE &operator^=(ENUMTYPE &a, ENUMTYPE b)                                           \
    {                                                                                              \
        a = a ^ b;                                                                                 \
        return a;                                                                                  \
    }                                                                                              \
    inline constexpr ENUMTYPE operator~(ENUMTYPE a)                                                \
    {                                                                                              \
        using T = std::underlying_type_t<ENUMTYPE>;                                                \
        return static_cast<ENUMTYPE>(~static_cast<T>(a));                                          \
    }

// Calling-convention / annotation macros: no-ops off Windows.
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __cdecl
#define __cdecl
#endif
#ifndef __fastcall
#define __fastcall
#endif
#ifndef WINAPI
#define WINAPI
#endif
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef CONST
#define CONST const
#endif
#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

// ---- Constants & macros (values match the Win32 SDK) ------------------------

// Virtual-key codes (winuser.h). The input layer maps these onto SDL.
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_MBUTTON 0x04
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_LCONTROL 0xA2
#define VK_OEM_PLUS 0xBB
#define VK_OEM_MINUS 0xBD
#define VK_OEM_4 0xDB
#define VK_OEM_6 0xDD
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B

// Window messages (winuser.h). App-defined WM_USER+n messages live elsewhere.
#define WM_USER 0x0400
#define WM_DESTROY 0x0002
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SETCURSOR 0x0020
#define WM_CHAR 0x0102
#define WM_SYSKEYDOWN 0x0104
#define WM_TIMER 0x0113
#define WM_IME_NOTIFY 0x0282
#define WM_IME_CONTROL 0x0283
#define WM_DISPLAYCHANGE 0x007E

// MessageBox flags (winuser.h).
#define MB_OK 0x00000000
#define MB_YESNO 0x00000004
#define MB_ICONERROR 0x00000010
#define MB_ICONSTOP 0x00000010
#define MB_ICONEXCLAMATION 0x00000030

// Window styles (winuser.h). SDL owns the real window; these let the legacy
// Win32 window/resolution code compile (it is stubbed out at the call level).
#ifndef WS_OVERLAPPED
#define WS_OVERLAPPED 0x00000000L
#define WS_POPUP 0x80000000L
#define WS_VISIBLE 0x10000000L
#define WS_CAPTION 0x00C00000L
#define WS_BORDER 0x00800000L
#define WS_SYSMENU 0x00080000L
#define WS_MINIMIZEBOX 0x00020000L
#define WS_CLIPCHILDREN 0x02000000L
#endif

// SetWindowPos flags + insert-after handles (winuser.h).
#ifndef SWP_NOSIZE
#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_NOZORDER 0x0004
#define SWP_FRAMECHANGED 0x0020
#define SWP_SHOWWINDOW 0x0040
#endif
#ifndef HWND_TOP
#define HWND_TOP (reinterpret_cast<HWND>(0))
#define HWND_BOTTOM (reinterpret_cast<HWND>(1))
#define HWND_TOPMOST (reinterpret_cast<HWND>(-1))
#define HWND_NOTOPMOST (reinterpret_cast<HWND>(-2))
#endif
#ifndef GWL_STYLE
#define GWL_STYLE (-16)
#endif
#ifndef SW_SHOW
#define SW_HIDE 0
#define SW_NORMAL 1
#define SW_SHOW 5
#endif

// ChangeDisplaySettings / DEVMODE (winuser.h / wingdi.h).
#ifndef CDS_FULLSCREEN
#define CDS_FULLSCREEN 0x00000004
#endif
#ifndef DISP_CHANGE_SUCCESSFUL
#define DISP_CHANGE_SUCCESSFUL 0
#define DISP_CHANGE_FAILED (-1)
#endif
#ifndef DM_BITSPERPEL
#define DM_BITSPERPEL 0x00040000L
#define DM_PELSWIDTH 0x00080000L
#define DM_PELSHEIGHT 0x00100000L
#define DM_DISPLAYFREQUENCY 0x00400000L
#endif

// PeekMessage flag, stock-object id (winuser.h / wingdi.h).
#ifndef IMN_SETCONVERSIONMODE
#define IMN_SETCONVERSIONMODE 0x0006
#define IMN_SETSENTENCEMODE 0x0007
#endif
#ifndef PM_REMOVE
#define PM_REMOVE 0x0001
#endif
#ifndef BLACK_BRUSH
#define BLACK_BRUSH 4
#endif

// MIDI sequencer mapper (mmsystem.h).
#ifndef MCI_SEQ_MAPPER
#define MCI_SEQ_MAPPER 0xFFFFu
#endif

// MSVC keyword / fixed-width aliases.
#ifndef __forceinline
#define __forceinline inline
#endif
typedef uint64_t u_int64;
typedef int64_t INT64;

// Console attributes / standard handles (wincon.h).
#define FOREGROUND_BLUE 0x0001
#define FOREGROUND_GREEN 0x0002
#define FOREGROUND_RED 0x0004
#define FOREGROUND_INTENSITY 0x0008
#define STD_INPUT_HANDLE (static_cast<DWORD>(-10))
#define STD_OUTPUT_HANDLE (static_cast<DWORD>(-11))
#define STD_ERROR_HANDLE (static_cast<DWORD>(-12))

// _splitpath component sizes (stdlib.h).
#ifndef _MAX_PATH
#define _MAX_PATH 260
#endif
#ifndef _MAX_DRIVE
#define _MAX_DRIVE 3
#endif
#ifndef _MAX_DIR
#define _MAX_DIR 256
#endif
#ifndef _MAX_FNAME
#define _MAX_FNAME 256
#endif
#ifndef _MAX_EXT
#define _MAX_EXT 256
#endif

// COM-style result codes.
#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0L
#endif
#ifndef S_OK
#define S_OK (static_cast<HRESULT>(0))
#endif
#ifndef S_FALSE
#define S_FALSE (static_cast<HRESULT>(1))
#endif
#ifndef E_FAIL
#define E_FAIL (static_cast<HRESULT>(0x80004005))
#endif
#ifndef E_INVALIDARG
#define E_INVALIDARG (static_cast<HRESULT>(0x80070057))
#endif
#ifndef SUCCEEDED
#define SUCCEEDED(hr) (static_cast<HRESULT>(hr) >= 0)
#endif
#ifndef FAILED
#define FAILED(hr) (static_cast<HRESULT>(hr) < 0)
#endif
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE (reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-1)))
#endif

// Word/byte/color helper macros (minwindef.h / wingdi.h).
#ifndef LOWORD
#define LOWORD(l) (static_cast<WORD>(static_cast<uintptr_t>(l) & 0xffff))
#endif
#ifndef HIWORD
#define HIWORD(l) (static_cast<WORD>((static_cast<uintptr_t>(l) >> 16) & 0xffff))
#endif
#ifndef LOBYTE
#define LOBYTE(w) (static_cast<BYTE>(static_cast<uintptr_t>(w) & 0xff))
#endif
#ifndef HIBYTE
#define HIBYTE(w) (static_cast<BYTE>((static_cast<uintptr_t>(w) >> 8) & 0xff))
#endif
#ifndef MAKEWORD
#define MAKEWORD(a, b)                                                                             \
    (static_cast<WORD>((static_cast<BYTE>(a)) | (static_cast<WORD>(static_cast<BYTE>(b)) << 8)))
#endif
#ifndef MAKELONG
#define MAKELONG(a, b)                                                                             \
    (static_cast<LONG>((static_cast<WORD>(a)) | (static_cast<DWORD>(static_cast<WORD>(b)) << 16)))
#endif
#ifndef RGB
#define RGB(r, g, b)                                                                               \
    (static_cast<COLORREF>((static_cast<BYTE>(r)) |                                                \
                           (static_cast<WORD>(static_cast<BYTE>(g)) << 8) |                        \
                           (static_cast<DWORD>(static_cast<BYTE>(b)) << 16)))
#define GetRValue(rgb) (static_cast<BYTE>(rgb))
#define GetGValue(rgb) (static_cast<BYTE>((rgb) >> 8))
#define GetBValue(rgb) (static_cast<BYTE>((rgb) >> 16))
#endif

// Memory fill helper.
#ifndef ZeroMemory
#define ZeroMemory(dst, len) std::memset((dst), 0, (len))
#endif

// Element count of a stack array (MSVC _countof). Type-safe: a pointer argument
// fails to match the array helper and is a compile error, not a silent miscount.
#ifndef _countof
template <typename T, std::size_t N> char (&mu_countof_helper(T (&)[N]))[N];
#define _countof(arr) (sizeof(mu_countof_helper(arr)))
#endif

// ---- GDI rect/point helpers (winuser.h) -------------------------------------
// Pure value-type helpers with no platform dependency; same semantics as Win32.

inline BOOL SetRect(RECT *lprc, int left, int top, int right, int bottom)
{
    if (!lprc)
        return FALSE;
    lprc->left = left;
    lprc->top = top;
    lprc->right = right;
    lprc->bottom = bottom;
    return TRUE;
}

// Point is inside the rectangle's [left,right) x [top,bottom) half-open range.
inline BOOL PtInRect(const RECT *lprc, POINT pt)
{
    if (!lprc)
        return FALSE;
    return (pt.x >= lprc->left && pt.x < lprc->right && pt.y >= lprc->top && pt.y < lprc->bottom)
               ? TRUE
               : FALSE;
}

// Intersection of two rects into dst; returns FALSE (and empties dst) if none.
inline BOOL IntersectRect(RECT *dst, const RECT *a, const RECT *b)
{
    if (!dst || !a || !b)
        return FALSE;
    const LONG left = (a->left > b->left) ? a->left : b->left;
    const LONG top = (a->top > b->top) ? a->top : b->top;
    const LONG right = (a->right < b->right) ? a->right : b->right;
    const LONG bottom = (a->bottom < b->bottom) ? a->bottom : b->bottom;
    if (left < right && top < bottom)
    {
        dst->left = left;
        dst->top = top;
        dst->right = right;
        dst->bottom = bottom;
        return TRUE;
    }
    dst->left = dst->top = dst->right = dst->bottom = 0;
    return FALSE;
}

// Legacy Win32 pointer-validity probe. There is no portable equivalent, and the
// API is deprecated even on Windows; the realistic failure the callers guard
// against is a null pointer, so report only that as "bad".
inline BOOL IsBadReadPtr(const void *lp, UINT_PTR /*ucb*/)
{
    return lp ? FALSE : TRUE;
}

#endif // !_WIN32

#ifdef _DEBUG
#define KWAK_FIX_ALT_KEYDOWN_MENU_BLOCK
#endif // _DEBUG

#define ASG_ADD_GENS_SYSTEM
#ifdef ASG_ADD_GENS_SYSTEM
#define ASG_ADD_INFLUENCE_GROUND_EFFECT
#define ASG_ADD_GENS_MARK
#define PBG_MOD_STRIFE_GENSMARKRENDER
#define PBG_ADD_GENSRANKING
#endif // ASG_ADD_GENS_SYSTEM

#define KJH_PBG_ADD_INGAMESHOP_SYSTEM
#ifdef KJH_PBG_ADD_INGAMESHOP_SYSTEM
#define PBG_ADD_INGAMESHOP_UI_MAINFRAME
#define PBG_ADD_INGAMESHOP_UI_ITEMSHOP
#define PBG_ADD_NAMETOPMSGBOX
#define KJH_ADD_INGAMESHOP_UI_SYSTEM
#define KJH_ADD_PERIOD_ITEM_SYSTEM
#define PBG_ADD_INGAMESHOPMSGBOX
#define PBG_ADD_ITEMRESIZE
#define KJH_MOD_SHOP_SCRIPT_DOWNLOAD
#define PBG_ADD_CHARACTERCARD
#endif //KJH_PBG_ADD_INGAMESHOP_SYSTEM

#define PJH_ADD_PANDA_PET
#define PJH_ADD_PANDA_CHANGERING

#define SEND_POSITION_TO_SERVER

#define ASG_ADD_MAP_KARUTAN
#define ASG_ADD_KARUTAN_MONSTERS

#define FOR_WORK
/*--------------------------------------------------------------------------------------*/

#define LANGUAGE_KOREAN (0)
#define LANGUAGE_ENGLISH (1)
#define LANGUAGE_TAIWANESE (2)
#define LANGUAGE_CHINESE (3)
#define LANGUAGE_JAPANESE (4)
#define LANGUAGE_THAILAND (5)
#define LANGUAGE_PHILIPPINES (6)
#define LANGUAGE_VIETNAMESE (7)
#define NUM_LANGUAGE (8)

#define SELECTED_LANGUAGE (LANGUAGE_ENGLISH)

#ifdef _DEBUG
#define ENABLE_EDIT
#define ENABLE_EDIT2
//#define DEBUG_BITMAP_CACHE
#endif // _DEBUG

#ifdef FOR_WORK
#ifdef _DEBUG

#define CSK_LH_DEBUG_CONSOLE
#ifdef CSK_LH_DEBUG_CONSOLE
#define CONSOLE_DEBUG
#endif // CSK_LH_DEBUG_CONSOLE

#define CSK_DEBUG_MAP_ATTRIBUTE

#define CSK_DEBUG_RENDER_BOUNDINGBOX

#define CSK_DEBUG_MAP_PATHFINDING

#endif // _DEBUG
#endif // FOR_WORK

#define WINDOWMODE

#define ACTIVE_FOCUS_OUT

//#define DEVIAS_XMAS_EVENT  //more snow in devias
//#define GUILD_WAR_EVENT
#define DUEL_SYSTEM
//#define CAMERA_TEST

extern DWORD GetCheckSum(WORD wKey);

// Returns the desktop's current bit depth (queried via EnumDisplaySettings),
// falling back to 32 if the query fails. Used by every fullscreen-mode change
// site so we don't hardcode a value that doesn't match the user's display.

//#if defined _DEBUG || defined PBG_LOG_PACKET_WINSOCKERROR
//	#include "Core/Utilities/Log/DebugAngel.h"
//#define ExecutionLog	DebugAngel_Write
//#else
//#define ExecutionLog	{}
//#endif //_DEBUG

#define FAKE_CODE(pos)                                                                             \
    _asm { jmp pos }                                                                                 \
    ;                                                                                              \
    _asm { __emit 0xFF }                                                                             \
    ;                                                                                              \
    _asm { __emit 0x15 }

enum eBuffState : int;

typedef float vec_t;
typedef vec_t vec2_t[2];
typedef vec_t vec3_t[3];
typedef vec_t vec4_t[4];
typedef vec_t vec34_t[3][4];

template <typename T> inline void InitVector(T *vect, int size)
{
    for (int i = 0; i < size; ++i)
    {
        vect[i] = 0.0f;
    }
}

template <typename T> inline void IdentityMatrix(T (*mat)[4])
{
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            mat[i][j] = 0;
        }
    }
}

template <typename T> inline void IdentityVector2D(T *vect)
{
    InitVector(vect, 2);
}

template <typename T> inline void IdentityVector3D(T *vect)
{
    InitVector(vect, 3);
}

typedef char *PCHAR;
typedef char CHAR;
typedef std::string STRING;

typedef wchar_t *PWCHAR;
typedef wchar_t WCHAR;
typedef std::wstring WSTRING;
typedef std::map<eBuffState, DWORD> BuffStateMap;

// Portable code-page conversion shim.
// The engine converts between UTF-8 (narrow) and UTF-16/UTF-32 (wide) with the
// Win32 MultiByteToWideChar / WideCharToMultiByte. On Windows these come from
// <windows.h>; elsewhere this provides portable implementations (WinNls.cpp).
// Only the CP_UTF8 path the engine uses is meaningful; CP_ACP is treated as
// UTF-8 too (the standard Linux locale). dwFlags and the default-char arguments
// are accepted for signature compatibility and otherwise ignored.

#ifndef _WIN32

#ifndef CP_ACP
#define CP_ACP 0
#endif
#ifndef CP_UTF8
#define CP_UTF8 65001
#endif

// Same contract as Win32: cbMultiByte/cchWideChar == -1 means the source is
// null-terminated (the terminator is converted and counted); a zero output size
// returns the required length without writing. Invalid input maps to U+FFFD.
int MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr, int cbMultiByte,
                        LPWSTR lpWideCharStr, int cchWideChar);

int WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr, int cchWideChar,
                        LPSTR lpMultiByteStr, int cbMultiByte, LPCSTR lpDefaultChar,
                        LPBOOL lpUsedDefaultChar);

#endif // !_WIN32

// Case-correcting path resolution for POSIX filesystems.
// The engine's asset paths are Windows-style: backslash separators and a case
// that does not always match the files on disk, which a case-sensitive
// filesystem rejects. The file-open shims funnel paths through here, which
// flips the separators and, when a component does not exist verbatim, falls
// back to a case-insensitive directory scan for it.

#ifndef _WIN32

// Returns `utf8Path` with separators normalized to '/' and each component
// case-corrected to an existing directory entry where the verbatim spelling
// does not exist. Components with no case-insensitive match are kept as-is, so
// paths for files being created resolve their directory and keep the new name,
// and genuinely missing files still fail at open time.
std::string MuResolvePath(const char *utf8Path);

#endif // !_WIN32

// Cross-platform swprintf replacement
// MinGW blocks swprintf, so we provide mu_swprintf as a safe cross-platform alternative

// Undefine MinGW's blocking macro first
#undef swprintf
#undef swprintf_instead_use_StringCbPrintfW_or_StringCchPrintfW

// Use inline template functions instead of macros to avoid issues with #ifdef in arguments
#ifdef _MSC_VER
// MSVC: swprintf doesn't need buffer size
template <typename... Args>
inline int mu_swprintf(wchar_t *buffer, const wchar_t *format, Args... args)
{
    return swprintf(buffer, format, args...);
}
// mu_swprintf_s with explicit size
template <typename... Args>
inline int mu_swprintf_s(wchar_t *buffer, size_t size, const wchar_t *format, Args... args)
{
    return swprintf_s(buffer, size, format, args...);
}
// mu_swprintf_s with array - auto-deduce size (like MSVC's swprintf_s)
template <size_t N, typename... Args>
inline int mu_swprintf_s(wchar_t (&buffer)[N], const wchar_t *format, Args... args)
{
    return swprintf_s(buffer, N, format, args...);
}
#else
// GCC/MinGW/Clang: use std::swprintf with explicit buffer size. For an array
// the bound is its real size; a 1024 fallback larger than the array would
// overrun smaller buffers, and glibc's _FORTIFY_SOURCE aborts on the
// mismatch even before anything is written. Only a plain pointer (size
// unknowable) keeps the 1024 assumption.
template <typename Buf, typename... Args>
inline int mu_swprintf(Buf &&buffer, const wchar_t *format, Args... args)
{
    using Array = std::remove_reference_t<Buf>;
    if constexpr (std::is_array_v<Array>)
        return std::swprintf(buffer, std::extent_v<Array>, format, args...);
    else
        return std::swprintf(buffer, 1024, format, args...);
}
// mu_swprintf_s with explicit size
template <typename... Args>
inline int mu_swprintf_s(wchar_t *buffer, size_t size, const wchar_t *format, Args... args)
{
    return std::swprintf(buffer, size, format, args...);
}
// mu_swprintf_s with array - auto-deduce size (compatible with MSVC swprintf_s)
template <size_t N, typename... Args>
inline int mu_swprintf_s(wchar_t (&buffer)[N], const wchar_t *format, Args... args)
{
    return std::swprintf(buffer, N, format, args...);
}
#endif

// Reference resolution -- all UI coordinates and screen-space math use this as the base.
// Must be declared BEFORE ZzzOpenglUtil.h because BeginOpengl() uses them as default args.
// g_fScreenRate_x/y (declared further below) scale from reference to actual window size.
inline constexpr int REFERENCE_WIDTH = 640;
inline constexpr int REFERENCE_HEIGHT = 480;

namespace Core::Math
{
struct LineToFaceCollision final
{
    float distance = 9999999.0f;
    float position[3] = {};
};

bool DetectLineToFace(const float *start, const float *target, int polygon, const float *vertex1,
                      const float *vertex2, const float *vertex3, const float *vertex4,
                      const float *normal, LineToFaceCollision &closest,
                      bool storeCollision = true) noexcept;
} // namespace Core::Math

#ifndef __MATHLIB__
#define __MATHLIB__

#ifdef __cplusplus
extern "C"
{
#endif

#define SIDE_FRONT 0
#define SIDE_ON 2
#define SIDE_BACK 1
#define SIDE_CROSS -2

#define Q_PI 3.1415926535897932384626433832795029f
#define ON_EPSILON 0.01
#define EQUAL_EPSILON 0.001f
#define EPSILON_V 0.0001f
#define ANGLE_TO_RAD 0.017453292519943294f
#define RAD_TO_ANGLE 57.29577951308232089f

#define swaps(a, b) ((a) ^= (b) ^= (a) ^= (b))
    bool VectorCompare(const vec3_t v1, const vec3_t v2);
    bool QuaternionCompare(const vec4_t v1, const vec4_t v2);

#define Vector(a, b, c, d)                                                                         \
    {                                                                                              \
        (d)[0] = a;                                                                                \
        (d)[1] = b;                                                                                \
        (d)[2] = c;                                                                                \
    }
    inline void Vector4(vec_t a, vec_t b, vec_t c, vec_t d, vec4_t target) noexcept
    {
        target[0] = a;
        target[1] = b;
        target[2] = c;
        target[3] = d;
    }
#define VectorAvg(a) (((a)[0] + (a)[1] + (a)[2]) / 3)
#define VectorSubtract(a, b, c)                                                                    \
    {                                                                                              \
        (c)[0] = (a)[0] - (b)[0];                                                                  \
        (c)[1] = (a)[1] - (b)[1];                                                                  \
        (c)[2] = (a)[2] - (b)[2];                                                                  \
    }
#define VectorSubtractScaled(a, b, c, d)                                                           \
    {                                                                                              \
        (c)[0] = (a)[0] - (b)[0] * (d);                                                            \
        (c)[1] = (a)[1] - (b)[1] * (d);                                                            \
        (c)[2] = (a)[2] - (b)[2] * (d);                                                            \
    }
#define VectorAdd(a, b, c)                                                                         \
    {                                                                                              \
        (c)[0] = (a)[0] + (b)[0];                                                                  \
        (c)[1] = (a)[1] + (b)[1];                                                                  \
        (c)[2] = (a)[2] + (b)[2];                                                                  \
    }
#define VectorAddScaled(a, b, c, d)                                                                \
    {                                                                                              \
        (c)[0] = (a)[0] + (b)[0] * (d);                                                            \
        (c)[1] = (a)[1] + (b)[1] * (d);                                                            \
        (c)[2] = (a)[2] + (b)[2] * (d);                                                            \
    }
#define VectorCopy(a, b)                                                                           \
    {                                                                                              \
        (b)[0] = (a)[0];                                                                           \
        (b)[1] = (a)[1];                                                                           \
        (b)[2] = (a)[2];                                                                           \
    }
#define QuaternionCopy(a, b)                                                                       \
    {                                                                                              \
        (b)[0] = (a)[0];                                                                           \
        (b)[1] = (a)[1];                                                                           \
        (b)[2] = (a)[2];                                                                           \
        (b)[3] = (a)[3];                                                                           \
    }
#define VectorScale(a, b, c)                                                                       \
    {                                                                                              \
        (c)[0] = (b) * (a)[0];                                                                     \
        (c)[1] = (b) * (a)[1];                                                                     \
        (c)[2] = (b) * (a)[2];                                                                     \
    }
    inline vec_t DotProduct(const vec3_t x, const vec3_t y) noexcept
    {
        return x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
    }
#define VectorFill(a, b)                                                                           \
    {                                                                                              \
        (a)[0] = (b);                                                                              \
        (a)[1] = (b);                                                                              \
        (a)[2] = (b);                                                                              \
    }

    inline void SETLIMITS(float &VALUE_, const float MAX_, const float MIN_)
    {
        if (VALUE_ > MAX_)
        {
            VALUE_ = MAX_;
        }
        else if (VALUE_ < MIN_)
        {
            VALUE_ = MIN_;
        }
    }

    inline void LInterpolationF(float &fout, const float &f01, const float &f02,
                                const float fWeight)
    {
        fout = f01 + ((f02 - f01) * fWeight);
    }

    void VectorInterpolation(vec3_t &v_out, const vec3_t &v_1, const vec3_t &v_2,
                             const float fWeight);
    void VectorInterpolation_F(vec3_t &v_out, const vec3_t &v_1, const vec3_t &v_2,
                               const float fArea, const float fCurrent);
    void VectorInterpolation_W(vec3_t &v_out, const vec3_t &v_1, const vec3_t &v_2,
                               const float fWeight);
    void VectorDistanceInterpolation_F(vec3_t &v_out, const vec3_t &v_in, const float fRate);
    float VectorDistance3D(const vec3_t &vPosStart, const vec3_t &vPosEnd);
    void VectorDistance3D_Dir(const vec3_t &vPosStart, const vec3_t &vPosEnd, vec3_t &vOut);
    float VectorDistance3D_DirDist(const vec3_t &vPosStart, const vec3_t &vPosEnd, vec3_t &vOut);

    vec_t Q_rint(vec_t in);
    inline float VectorLength(vec3_t v)
    {
        return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    }

    void VectorMul(const vec3_t va, const vec3_t vb, vec3_t vc);
    void VectorMulF(const vec3_t vIn01, const float fIn01, vec3_t vOut);
    void VectorDivF(const vec3_t vIn01, const float fIn01, vec3_t vOut);
    void VectorDivFSelf(vec3_t vInOut, const float fIn01);
    void VectorDistNormalize(const vec3_t vInFrom, const vec3_t vInTo, vec3_t vOut);
    void VectorMA(vec3_t va, float scale, vec3_t vb, vec3_t vc);

    void CrossProduct(vec3_t v1, vec3_t v2, vec3_t cross);
    vec_t VectorNormalize(vec3_t v);
    void VectorInverse(vec3_t v);

    void ClearBounds(vec3_t mins, vec3_t maxs);
    void AddPointToBounds(vec3_t v, vec3_t mins, vec3_t maxs);

    void AngleMatrix(const vec3_t angles, float matrix[3][4]);
    void AngleIMatrix(const vec3_t angles, float matrix[3][4]);
    void R_ConcatTransforms(const float in1[3][4], const float in2[3][4], float out[3][4]);

    void VectorIRotate(const vec3_t in1, const float in2[3][4], vec3_t out);
    void VectorRotate(const vec3_t in1, const float in2[3][4], vec3_t out);
    void VectorTranslate(const vec3_t in1, const float in2[3][4], vec3_t out);
    void VectorTransform(const vec3_t in1, const float in2[3][4], vec3_t out);

    void AngleQuaternion(const vec3_t angles, vec4_t quaternion);
    void QuaternionMatrix(const vec4_t quaternion, float (*matrix)[4]);
    void QuaternionSlerp(const vec4_t p, vec4_t q, float t, vec4_t qt);
    void QuaternionNLERP(const vec4_t p, const vec4_t q, float t, vec4_t qt);

    void FaceNormalize(vec3_t v1, vec3_t v2, vec3_t v3, vec3_t Normal);

    float VectorDistance2D(vec3_t va, vec3_t vb);
#ifdef __cplusplus
}
#endif

#endif

// Portable shim for the MSVC debug-CRT header.
// The engine includes <crtdbg.h> only for the _ASSERT/_ASSERTE macros. <crtdbg.h>
// is MSVC-specific, so only MSVC uses it; every other compiler (MinGW, Clang,
// Linux GCC) maps the macros onto the standard assert (a no-op under NDEBUG,
// exactly as on a Windows release build).

#ifndef _MSC_VER

#ifndef _ASSERT
#define _ASSERT(expr) assert(expr)
#endif
#ifndef _ASSERTE
#define _ASSERTE(expr) assert(expr)
#endif

#endif // !_MSC_VER

// Portable DPAPI shim.
// The config layer encrypts saved credentials with the Windows Data Protection
// API. On Windows the types come from the platform SDK; elsewhere this provides
// a reversible, local-scope substitute so the "remember me" feature works.
// Windows DPAPI ties the ciphertext to the local user account. Without a system
// secret store wired up (libsecret / Keychain is a follow-up), this derives a
// key from stable per-machine/per-user material (/etc/machine-id + uid) and
// applies a keystream XOR. That is obfuscation, not strong cryptography - it
// keeps the saved password out of config.ini in plaintext and makes it
// decryptable only on the same machine/user, matching DPAPI's local scope and
// threat model (a casual reader of the config file), not a determined attacker.

#ifndef _WIN32

typedef struct _CRYPTOAPI_BLOB
{
    DWORD cbData;
    BYTE *pbData;
} DATA_BLOB;

namespace mu_dpapi_detail
{
// A stable 64-bit seed for this machine+user. FNV-1a over an app salt, the
// uid, and /etc/machine-id, so the same user on the same install always
// derives the same key (and a different user/machine cannot decrypt).
inline std::uint64_t KeySeed()
{
    std::uint64_t h = 1469598103934665603ull; // FNV offset basis
    auto mix = [&](const void *p, std::size_t n) {
        const auto *b = static_cast<const unsigned char *>(p);
        for (std::size_t i = 0; i < n; ++i)
        {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    };

    static const char kSalt[] = "MuMain-credential-store-v1";
    mix(kSalt, sizeof(kSalt));
    const uid_t uid = ::getuid();
    mix(&uid, sizeof(uid));
    if (FILE *f = std::fopen("/etc/machine-id", "rb"))
    {
        char buf[64];
        const std::size_t n = std::fread(buf, 1, sizeof(buf), f);
        std::fclose(f);
        if (n > 0)
            mix(buf, n);
    }
    return h ? h : 0x9E3779B97F4A7C15ull;
}

// XOR the buffer in place with a SplitMix64 keystream seeded from KeySeed().
// Symmetric: applying it twice restores the original, so the same routine
// both encrypts and decrypts.
inline void KeystreamXor(BYTE *data, DWORD len)
{
    std::uint64_t s = KeySeed();
    for (DWORD i = 0; i < len;)
    {
        s += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z = z ^ (z >> 31);
        for (int b = 0; b < 8 && i < len; ++b, ++i)
            data[i] ^= static_cast<BYTE>(z >> (b * 8));
    }
}

// Encrypt == decrypt for a keystream XOR: copy, transform, hand back a
// freshly allocated buffer (freed by LocalFree, like the Win32 contract).
inline BOOL Transform(const DATA_BLOB *in, DATA_BLOB *out)
{
    if (!in || !out || (!in->pbData && in->cbData != 0))
        return FALSE;
    BYTE *buf = static_cast<BYTE *>(std::malloc(in->cbData ? in->cbData : 1));
    if (!buf)
        return FALSE;
    if (in->cbData)
        std::memcpy(buf, in->pbData, in->cbData);
    KeystreamXor(buf, in->cbData);
    out->pbData = buf;
    out->cbData = in->cbData;
    return TRUE;
}
} // namespace mu_dpapi_detail

inline BOOL CryptProtectData(DATA_BLOB *pDataIn, LPCWSTR, DATA_BLOB *, PVOID, void *, DWORD,
                             DATA_BLOB *pDataOut)
{
    return mu_dpapi_detail::Transform(pDataIn, pDataOut);
}
inline BOOL CryptUnprotectData(DATA_BLOB *pDataIn, LPWSTR *, DATA_BLOB *, PVOID, void *, DWORD,
                               DATA_BLOB *pDataOut)
{
    return mu_dpapi_detail::Transform(pDataIn, pDataOut);
}

// Frees the buffer allocated by Crypt*Data above (Win32 LocalFree returns NULL
// on success).
inline void *LocalFree(void *hMem)
{
    std::free(hMem);
    return nullptr;
}

#endif // !_WIN32

// Portable shims for the MSVC "secure CRT" (_s) functions the engine uses.
// On Windows the real CRT provides these, so this header
// is empty there; elsewhere it maps them onto the standard C library.
// Each function is provided in both the explicit-count form
// (`f(buf, count, ...)`) and the MSVC template form that deduces the size from
// a stack array (`f(buf, ...)`), so existing call sites compile unchanged.

#ifndef _WIN32

typedef int errno_t;

#ifndef _TRUNCATE
#define _TRUNCATE (static_cast<size_t>(-1))
#endif

// EINVAL / ERANGE without pulling Windows error codes.
inline errno_t mu__einval()
{
    return EINVAL;
}
inline errno_t mu__erange()
{
    return ERANGE;
}

// ---- wcscpy_s ----------------------------------------------------------------
inline errno_t wcscpy_s(wchar_t *dst, size_t count, const wchar_t *src)
{
    if (dst == nullptr || count == 0)
        return mu__einval();
    if (src == nullptr)
    {
        dst[0] = L'\0';
        return mu__einval();
    }
    const size_t len = wcslen(src);
    if (len + 1 > count)
    {
        dst[0] = L'\0';
        return mu__erange();
    }
    wmemcpy(dst, src, len + 1);
    return 0;
}
template <size_t N> inline errno_t wcscpy_s(wchar_t (&dst)[N], const wchar_t *src)
{
    return wcscpy_s(dst, N, src);
}

// ---- wcsncpy_s (supports _TRUNCATE) -----------------------------------------
inline errno_t wcsncpy_s(wchar_t *dst, size_t count, const wchar_t *src, size_t n)
{
    if (dst == nullptr || count == 0)
        return mu__einval();
    if (src == nullptr)
    {
        dst[0] = L'\0';
        return (n == 0) ? 0 : mu__einval();
    }
    const size_t srclen = wcslen(src);
    size_t tocopy = (n == _TRUNCATE) ? srclen : (n < srclen ? n : srclen);
    if (tocopy + 1 > count)
    {
        if (n == _TRUNCATE)
        {
            tocopy = count - 1;
        } // truncation explicitly allowed
        else
        {
            dst[0] = L'\0';
            return mu__erange();
        }
    }
    wmemcpy(dst, src, tocopy);
    dst[tocopy] = L'\0';
    return 0;
}
template <size_t N> inline errno_t wcsncpy_s(wchar_t (&dst)[N], const wchar_t *src, size_t n)
{
    return wcsncpy_s(dst, N, src, n);
}

// ---- wcscat_s ----------------------------------------------------------------
inline errno_t wcscat_s(wchar_t *dst, size_t count, const wchar_t *src)
{
    if (dst == nullptr || count == 0)
        return mu__einval();
    if (src == nullptr)
    {
        dst[0] = L'\0';
        return mu__einval();
    }
    const size_t dlen = wcsnlen(dst, count);
    if (dlen == count)
    {
        dst[0] = L'\0';
        return mu__einval();
    } // not null-terminated
    const size_t slen = wcslen(src);
    if (dlen + slen + 1 > count)
    {
        dst[0] = L'\0';
        return mu__erange();
    }
    wmemcpy(dst + dlen, src, slen + 1);
    return 0;
}
template <size_t N> inline errno_t wcscat_s(wchar_t (&dst)[N], const wchar_t *src)
{
    return wcscat_s(dst, N, src);
}

// va_list cores. On error or truncation they return -1 and (unless truncation
// is explicitly allowed) empty the buffer, matching MSVC's non-aborting result.
inline int vsprintf_s(char *buf, size_t count, const char *fmt, va_list argptr)
{
    if (buf == nullptr || count == 0 || fmt == nullptr)
        return -1;
    const int r = vsnprintf(buf, count, fmt, argptr);
    if (r < 0 || static_cast<size_t>(r) >= count)
    {
        buf[0] = '\0';
        return -1;
    }
    return r;
}
inline int vswprintf_s(wchar_t *buf, size_t count, const wchar_t *fmt, va_list argptr)
{
    if (buf == nullptr || count == 0 || fmt == nullptr)
        return -1;
    const int r = vswprintf(buf, count, fmt, argptr);
    if (r < 0)
    {
        buf[0] = L'\0';
        return -1;
    }
    return r;
}
inline int _vsnwprintf_s(wchar_t *buf, size_t count, size_t maxcount, const wchar_t *fmt,
                         va_list argptr)
{
    if (buf == nullptr || count == 0 || fmt == nullptr)
        return -1;
    const size_t lim = (maxcount == _TRUNCATE || maxcount + 1 > count) ? count : maxcount + 1;
    const int r = vswprintf(buf, lim, fmt, argptr);
    if (r < 0)
    {
        if (maxcount != _TRUNCATE)
            buf[0] = L'\0';
        return -1;
    }
    return r;
}

// Non-secure _vsnwprintf(buf, count, fmt, args) -> bounded C99 vswprintf. On
// failure vswprintf leaves the buffer indeterminate, so null-terminate it
// (callers don't check the return value) to keep later reads in bounds.
inline int _vsnwprintf(wchar_t *buf, size_t count, const wchar_t *fmt, va_list argptr)
{
    if (buf == nullptr || count == 0 || fmt == nullptr)
        return -1;
    const int r = vswprintf(buf, count, fmt, argptr);
    if (r < 0)
        buf[count - 1] = L'\0';
    return r;
}

// ---- swprintf_s --------------------------------------------------------------
inline int swprintf_s(wchar_t *buf, size_t count, const wchar_t *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    const int r = vswprintf_s(buf, count, fmt, a);
    va_end(a);
    return r;
}
template <size_t N> inline int swprintf_s(wchar_t (&buf)[N], const wchar_t *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    const int r = vswprintf_s(buf, N, fmt, a);
    va_end(a);
    return r;
}

// ---- _snwprintf_s(buf, count, maxcount, fmt, ...) ---------------------------
inline int _snwprintf_s(wchar_t *buf, size_t count, size_t maxcount, const wchar_t *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    const int r = _vsnwprintf_s(buf, count, maxcount, fmt, a);
    va_end(a);
    return r;
}
template <size_t N>
inline int _snwprintf_s(wchar_t (&buf)[N], size_t maxcount, const wchar_t *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    const int r = _vsnwprintf_s(buf, N, maxcount, fmt, a);
    va_end(a);
    return r;
}

// ---- sprintf_s (narrow) ------------------------------------------------------
inline int sprintf_s(char *buf, size_t count, const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    const int r = vsprintf_s(buf, count, fmt, a);
    va_end(a);
    return r;
}
template <size_t N> inline int sprintf_s(char (&buf)[N], const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    const int r = vsprintf_s(buf, N, fmt, a);
    va_end(a);
    return r;
}

// ---- localtime_s (MSVC arg order: tm* first, then time_t*) ------------------
inline errno_t localtime_s(struct tm *tmDest, const time_t *sourceTime)
{
    if (!tmDest || !sourceTime)
        return mu__einval();
    return ::localtime_r(sourceTime, tmDest) != nullptr ? 0 : mu__einval();
}

// ---- wcstok_s -> C11/glibc reentrant wcstok ---------------------------------
inline wchar_t *wcstok_s(wchar_t *str, const wchar_t *delim, wchar_t **context)
{
    return wcstok(str, delim, context);
}

// ---- _wfopen_s ---------------------------------------------------------------
// The path goes through the case-correcting resolver because asset paths are
// Windows-spelled (backslashes, mixed case). Uses the locale-independent UTF-8
// converter (like _wfopen), not wcstombs, which fails on non-ASCII in the "C"
// locale.
inline errno_t _wfopen_s(FILE **pFile, const wchar_t *path, const wchar_t *mode)
{
    if (pFile == nullptr || path == nullptr || mode == nullptr)
        return mu__einval();
    char narrowPath[4096] = {0}; // accommodate Linux PATH_MAX
    char narrowMode[16] = {0};
    if (WideCharToMultiByte(CP_UTF8, 0, path, -1, narrowPath, sizeof(narrowPath) - 1, nullptr,
                            nullptr) == 0)
        return mu__einval();
    if (WideCharToMultiByte(CP_UTF8, 0, mode, -1, narrowMode, sizeof(narrowMode) - 1, nullptr,
                            nullptr) == 0)
        return mu__einval();
    *pFile = fopen(MuResolvePath(narrowPath).c_str(), narrowMode);
    return (*pFile != nullptr) ? 0 : errno;
}

#endif // !_WIN32

// Portable <strsafe.h> shim.
// The in-game shop code copies and formats wide strings through the strsafe
// Cch family. On Windows the real header is used; elsewhere this maps the used
// subset onto the bounded CRT shims. Counts are in characters, like strsafe.

#ifndef _WIN32

#ifndef STRSAFE_E_INSUFFICIENT_BUFFER
#define STRSAFE_E_INSUFFICIENT_BUFFER (static_cast<HRESULT>(0x8007007AL))
#endif
#ifndef STRSAFE_E_INVALID_PARAMETER
#define STRSAFE_E_INVALID_PARAMETER (static_cast<HRESULT>(0x80070057L))
#endif

inline HRESULT StringCchCopyW(wchar_t *pszDest, size_t cchDest, const wchar_t *pszSrc)
{
    if (!pszDest || !pszSrc || cchDest == 0)
        return STRSAFE_E_INVALID_PARAMETER;
    const size_t len = wcslen(pszSrc);
    if (len >= cchDest)
    {
        wmemcpy(pszDest, pszSrc, cchDest - 1);
        pszDest[cchDest - 1] = L'\0';
        return STRSAFE_E_INSUFFICIENT_BUFFER;
    }
    wmemcpy(pszDest, pszSrc, len + 1);
    return S_OK;
}

inline HRESULT StringCchVPrintfW(wchar_t *pszDest, size_t cchDest, const wchar_t *pszFormat,
                                 va_list args)
{
    if (!pszDest || !pszFormat || cchDest == 0)
        return STRSAFE_E_INVALID_PARAMETER;
    const int r = _vsnwprintf(pszDest, cchDest, pszFormat, args);
    if (r < 0 || static_cast<size_t>(r) >= cchDest)
    {
        // Always leave a null-terminated buffer, like the Win32 contract: on
        // truncation/encoding error vswprintf may not terminate within count.
        pszDest[cchDest - 1] = L'\0';
        return STRSAFE_E_INSUFFICIENT_BUFFER;
    }
    return S_OK;
}

inline HRESULT StringCchPrintfW(wchar_t *pszDest, size_t cchDest, const wchar_t *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    const HRESULT hr = StringCchVPrintfW(pszDest, cchDest, pszFormat, args);
    va_end(args);
    return hr;
}

inline HRESULT StringCchLengthW(const wchar_t *psz, size_t cchMax, size_t *pcchLength)
{
    if (!psz || cchMax == 0)
        return STRSAFE_E_INVALID_PARAMETER;
    const size_t len = wcsnlen(psz, cchMax);
    if (len >= cchMax)
        return STRSAFE_E_INVALID_PARAMETER;
    if (pcchLength)
        *pcchLength = len;
    return S_OK;
}

inline HRESULT StringCchLengthA(const char *psz, size_t cchMax, size_t *pcchLength)
{
    if (!psz || cchMax == 0)
        return STRSAFE_E_INVALID_PARAMETER;
    const size_t len = strnlen(psz, cchMax);
    if (len >= cchMax)
        return STRSAFE_E_INVALID_PARAMETER;
    if (pcchLength)
        *pcchLength = len;
    return S_OK;
}

// The project builds UNICODE, so the generic names map to the wide forms.
#ifndef StringCchCopy
#define StringCchCopy StringCchCopyW
#define StringCchPrintf StringCchPrintfW
#define StringCchVPrintf StringCchVPrintfW
#define StringCchLength StringCchLengthW
#endif

#endif // !_WIN32

// Portable shims for the Win32 timer / wide-string / path helpers the engine
// calls. On Windows these come from the SDK/CRT, so this
// header is empty there; elsewhere it maps them onto the standard library.

#ifndef _WIN32

namespace mu_detail
{
// Shared monotonic epoch so GetTickCount() and timeGetTime() agree.
inline std::chrono::steady_clock::time_point tickEpoch()
{
    static const std::chrono::steady_clock::time_point epoch = std::chrono::steady_clock::now();
    return epoch;
}
} // namespace mu_detail

// Milliseconds since first use (relative timing; matches how the engine uses it).
inline DWORD GetTickCount()
{
    using namespace std::chrono;
    return static_cast<DWORD>(
        duration_cast<milliseconds>(steady_clock::now() - mu_detail::tickEpoch()).count());
}
inline DWORD timeGetTime()
{
    return GetTickCount();
}

// Block the calling thread (Win32 Sleep, milliseconds).
inline void Sleep(DWORD dwMilliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(dwMilliseconds));
}

// Thread-local error of the last failing call. The shimmed APIs sit on POSIX,
// so errno is that error.
inline DWORD GetLastError()
{
    return static_cast<DWORD>(errno);
}

// winmm timer-resolution requests are no-ops without the Windows scheduler API.
#ifndef TIMERR_NOERROR
#define TIMERR_NOERROR 0
#endif
inline unsigned int timeBeginPeriod(unsigned int)
{
    return TIMERR_NOERROR;
}
inline unsigned int timeEndPeriod(unsigned int)
{
    return TIMERR_NOERROR;
}

// 64-bit millisecond tick (no 49-day wrap).
inline unsigned long long GetTickCount64()
{
    using namespace std::chrono;
    return static_cast<unsigned long long>(
        duration_cast<milliseconds>(steady_clock::now() - mu_detail::tickEpoch()).count());
}

// Debug output -> stderr.
inline void OutputDebugStringA(const char *s)
{
    if (s)
        std::fputs(s, stderr);
}
inline void OutputDebugStringW(const wchar_t *s)
{
    if (s)
        std::fputws(s, stderr);
}
#ifndef OutputDebugString
#define OutputDebugString OutputDebugStringW
#endif

// Non-secure _snwprintf(buf, count, fmt, ...) -> the bounded _vsnwprintf core.
inline int _snwprintf(wchar_t *buf, size_t count, const wchar_t *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    const int r = _vsnwprintf(buf, count, fmt, a);
    va_end(a);
    return r;
}

inline void _tzset()
{
    ::tzset();
}

// Narrow int -> string (MSVC itoa/_itoa); a leading '-' only for base 10.
inline char *itoa(int value, char *buffer, int radix)
{
    if (!buffer)
        return buffer;
    if (radix < 2 || radix > 36)
    {
        buffer[0] = '\0';
        return buffer;
    }
    const bool negative = (value < 0 && radix == 10);
    unsigned int v =
        negative ? -static_cast<unsigned int>(value) : static_cast<unsigned int>(value);
    char digits[33];
    int n = 0;
    do
    {
        const unsigned int d = v % static_cast<unsigned int>(radix);
        digits[n++] = static_cast<char>(d < 10 ? '0' + d : 'a' + (d - 10));
        v /= static_cast<unsigned int>(radix);
    } while (v != 0);
    int j = 0;
    if (negative)
        buffer[j++] = '-';
    while (n > 0)
        buffer[j++] = digits[--n];
    buffer[j] = '\0';
    return buffer;
}
inline char *_itoa(int value, char *buffer, int radix)
{
    return itoa(value, buffer, radix);
}

// Path of the running executable (Win32 GetModuleFileNameW). The engine only
// queries its own module (hModule == null), which maps to /proc/self/exe.
inline DWORD GetModuleFileNameW(HMODULE /*hModule*/, LPWSTR lpFilename, DWORD nSize)
{
    if (!lpFilename || nSize == 0)
        return 0;
    char path[4096];
    const ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n <= 0)
    {
        lpFilename[0] = L'\0';
        return 0;
    }
    // UTF-8 -> wide via the project's own converter (locale-independent, unlike
    // mbstowcs which fails on non-ASCII paths in the default "C" locale).
    const int converted = MultiByteToWideChar(CP_UTF8, 0, path, static_cast<int>(n), lpFilename,
                                              static_cast<int>(nSize - 1));
    if (converted <= 0)
    {
        lpFilename[0] = L'\0';
        return 0;
    }
    lpFilename[converted] = L'\0';
    return static_cast<DWORD>(converted);
}
#ifndef GetModuleFileName
#define GetModuleFileName GetModuleFileNameW
#endif

// Terminate the process (Win32 ExitProcess). Never returns, like the real one.
[[noreturn]] inline void ExitProcess(UINT uExitCode)
{
    exit(static_cast<int>(uExitCode));
}

// Process / thread identifiers (Win32). Used only to tag temp file names, so
// the POSIX pid / thread id serve the same uniqueness purpose.
inline DWORD GetCurrentProcessId()
{
    return static_cast<DWORD>(::getpid());
}
inline DWORD GetCurrentThreadId()
{
    // pthread_t is 8 bytes on LP64; fold the high half into the low half before
    // truncating so two threads are less likely to collide on the low 32 bits.
    const std::uint64_t id = reinterpret_cast<std::uintptr_t>(pthread_self());
    return static_cast<DWORD>(id ^ (id >> 32));
}

// String length (Win32 lstrlen; the engine builds UNICODE, hence the wide form).
inline int lstrlen(const wchar_t *s)
{
    return s ? static_cast<int>(wcslen(s)) : 0;
}
inline int lstrlen(const char *s)
{
    return s ? static_cast<int>(strlen(s)) : 0;
}

// Wide string -> int.
inline int _wtoi(const wchar_t *s)
{
    return s ? static_cast<int>(wcstol(s, nullptr, 10)) : 0;
}

// Bytes in the multibyte character at c. Narrow strings are UTF-8 on Linux, so
// this is the UTF-8 sequence length taken from the lead byte.
inline size_t _mbclen(const unsigned char *c)
{
    if (!c || *c < 0x80)
        return 1;
    if ((*c & 0xE0) == 0xC0)
        return 2;
    if ((*c & 0xF0) == 0xE0)
        return 3;
    if ((*c & 0xF8) == 0xF0)
        return 4;
    return 1;
}

// Int -> wide string in the given radix (MSVC _itow). A leading '-' is emitted
// only for base 10, matching MSVC; the caller's buffer must be large enough.
inline wchar_t *_itow(int value, wchar_t *buffer, int radix)
{
    if (!buffer)
        return buffer;
    if (radix < 2 || radix > 36)
    {
        buffer[0] = L'\0';
        return buffer;
    }

    const bool negative = (value < 0 && radix == 10);
    // Unsigned negation is well-defined and yields the correct magnitude even
    // for INT_MIN, unlike negating through a (possibly 32-bit) signed long.
    unsigned int v =
        negative ? -static_cast<unsigned int>(value) : static_cast<unsigned int>(value);

    wchar_t digits[33];
    int n = 0;
    do
    {
        const unsigned int d = v % static_cast<unsigned int>(radix);
        digits[n++] = static_cast<wchar_t>(d < 10 ? L'0' + d : L'a' + (d - 10));
        v /= static_cast<unsigned int>(radix);
    } while (v != 0);

    int j = 0;
    if (negative)
        buffer[j++] = L'-';
    while (n > 0)
        buffer[j++] = digits[--n];
    buffer[j] = L'\0';
    return buffer;
}

// Case-insensitive, length-limited wide compare (MSVC _wcsnicmp/wcsnicmp).
inline int _wcsnicmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        const wint_t ca = towlower(static_cast<wint_t>(a[i]));
        const wint_t cb = towlower(static_cast<wint_t>(b[i]));
        if (ca != cb)
            return (ca < cb) ? -1 : 1;
        if (a[i] == L'\0')
            break;
    }
    return 0;
}
inline int wcsnicmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    return _wcsnicmp(a, b, n);
}

// Case-insensitive compares.
inline int _wcsicmp(const wchar_t *a, const wchar_t *b)
{
    return wcscasecmp(a, b);
}
inline int wcsicmp(const wchar_t *a, const wchar_t *b)
{
    return wcscasecmp(a, b);
}
inline int _stricmp(const char *a, const char *b)
{
    return strcasecmp(a, b);
}

// In-place wide upper-case.
inline wchar_t *_wcsupr(wchar_t *s)
{
    if (!s)
        return nullptr;
    for (wchar_t *p = s; *p; ++p)
        *p = static_cast<wchar_t>(towupper(*p));
    return s;
}

// Wide fopen: convert the path/mode to UTF-8 and open. The path goes through
// the case-correcting resolver because asset paths are Windows-spelled.
inline FILE *_wfopen(const wchar_t *path, const wchar_t *mode)
{
    if (!path || !mode)
        return nullptr;
    char narrowPath[4096] = {0};
    char narrowMode[16] = {0};
    if (WideCharToMultiByte(CP_UTF8, 0, path, -1, narrowPath, sizeof(narrowPath) - 1, nullptr,
                            nullptr) == 0)
        return nullptr;
    if (WideCharToMultiByte(CP_UTF8, 0, mode, -1, narrowMode, sizeof(narrowMode) - 1, nullptr,
                            nullptr) == 0)
        return nullptr;
    return fopen(MuResolvePath(narrowPath).c_str(), narrowMode);
}

// Split a wide path into components (POSIX has no drive). Matches MSVC's
// _wsplitpath: any output may be null; `ext` keeps its leading dot.
inline void _wsplitpath(const wchar_t *path, wchar_t *drive, wchar_t *dir, wchar_t *fname,
                        wchar_t *ext)
{
    if (!path)
        return;
    if (drive)
        drive[0] = L'\0';

    const wchar_t *lastSep = nullptr;
    for (const wchar_t *p = path; *p; ++p)
        if (*p == L'/' || *p == L'\\')
            lastSep = p;
    const wchar_t *nameStart = lastSep ? lastSep + 1 : path;

    if (dir)
    {
        const size_t dlen = static_cast<size_t>(nameStart - path);
        wmemcpy(dir, path, dlen);
        dir[dlen] = L'\0';
    }

    const wchar_t *dot = nullptr;
    for (const wchar_t *p = nameStart; *p; ++p)
        if (*p == L'.')
            dot = p;

    if (fname)
    {
        const size_t flen = dot ? static_cast<size_t>(dot - nameStart) : wcslen(nameStart);
        wmemcpy(fname, nameStart, flen);
        fname[flen] = L'\0';
    }
    if (ext)
    {
        if (dot)
            wcscpy(ext, dot);
        else
            ext[0] = L'\0';
    }
}

#endif // !_WIN32

// Portable Win32 file-API shim.
// A few subsystems do file I/O through the Win32 HANDLE API (CreateFile/ReadFile/
// WriteFile/CloseHandle/SetFilePointer/DeleteFile) and read the clock through
// GetLocalTime. On Windows these come from <windows.h>; elsewhere this maps them
// onto POSIX file descriptors and the C clock. A HANDLE carries the fd value.

#ifndef _WIN32

// dwDesiredAccess
#ifndef GENERIC_READ
#define GENERIC_READ 0x80000000u
#endif
#ifndef GENERIC_WRITE
#define GENERIC_WRITE 0x40000000u
#endif
// dwShareMode (advisory only on POSIX, ignored here)
#ifndef FILE_SHARE_READ
#define FILE_SHARE_READ 0x00000001
#endif
#ifndef FILE_SHARE_WRITE
#define FILE_SHARE_WRITE 0x00000002
#endif
// dwCreationDisposition
#ifndef CREATE_NEW
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#endif
#ifndef FILE_ATTRIBUTE_NORMAL
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#endif
// SetFilePointer move method
#ifndef FILE_BEGIN
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#endif
#ifndef INVALID_SET_FILE_POINTER
#define INVALID_SET_FILE_POINTER (static_cast<DWORD>(-1))
#endif

namespace mu_detail
{
inline int HandleToFd(HANDLE h)
{
    return static_cast<int>(reinterpret_cast<std::intptr_t>(h));
}
inline HANDLE FdToHandle(int fd)
{
    return reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(fd));
}
} // namespace mu_detail

inline HANDLE CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD /*share*/,
                          void * /*sec*/, DWORD dwCreationDisposition, DWORD /*flags*/,
                          HANDLE /*tmpl*/)
{
    const bool wantRead = (dwDesiredAccess & GENERIC_READ) != 0;
    const bool wantWrite = (dwDesiredAccess & GENERIC_WRITE) != 0;
    int oflag = wantRead && wantWrite ? O_RDWR : (wantWrite ? O_WRONLY : O_RDONLY);

    switch (dwCreationDisposition)
    {
    case CREATE_ALWAYS:
        oflag |= O_CREAT | O_TRUNC;
        break;
    case CREATE_NEW:
        oflag |= O_CREAT | O_EXCL;
        break;
    case OPEN_ALWAYS:
        oflag |= O_CREAT;
        break;
    case TRUNCATE_EXISTING:
        oflag |= O_TRUNC;
        break;
    case OPEN_EXISTING:
    default:
        break;
    }

    char path[4096] = {0};
    if (!lpFileName || WideCharToMultiByte(CP_UTF8, 0, lpFileName, -1, path, sizeof(path) - 1,
                                           nullptr, nullptr) == 0)
        return INVALID_HANDLE_VALUE;

    const int fd = ::open(MuResolvePath(path).c_str(), oflag, 0644);
    return (fd < 0) ? INVALID_HANDLE_VALUE : mu_detail::FdToHandle(fd);
}
#ifndef CreateFile
#define CreateFile CreateFileW
#endif

// A null HANDLE maps to fd 0 (stdin), so guard it: reading/writing/closing it
// would hang on or clobber standard input.
inline bool MuFileHandleValid(HANDLE h)
{
    return h != nullptr && h != INVALID_HANDLE_VALUE;
}

inline BOOL ReadFile(HANDLE hFile, void *buffer, DWORD count, LPDWORD bytesRead, void * /*ovl*/)
{
    if (!MuFileHandleValid(hFile))
    {
        if (bytesRead)
            *bytesRead = 0;
        return FALSE;
    }
    const ssize_t r = ::read(mu_detail::HandleToFd(hFile), buffer, count);
    if (r < 0)
    {
        if (bytesRead)
            *bytesRead = 0;
        return FALSE;
    }
    if (bytesRead)
        *bytesRead = static_cast<DWORD>(r);
    return TRUE;
}

inline BOOL WriteFile(HANDLE hFile, const void *buffer, DWORD count, LPDWORD bytesWritten,
                      void * /*ovl*/)
{
    if (!MuFileHandleValid(hFile))
    {
        if (bytesWritten)
            *bytesWritten = 0;
        return FALSE;
    }
    const ssize_t w = ::write(mu_detail::HandleToFd(hFile), buffer, count);
    if (w < 0)
    {
        if (bytesWritten)
            *bytesWritten = 0;
        return FALSE;
    }
    if (bytesWritten)
        *bytesWritten = static_cast<DWORD>(w);
    return TRUE;
}

inline BOOL CloseHandle(HANDLE hObject)
{
    if (!MuFileHandleValid(hObject))
        return FALSE;
    return (::close(mu_detail::HandleToFd(hObject)) == 0) ? TRUE : FALSE;
}

inline DWORD SetFilePointer(HANDLE hFile, LONG distance, LONG *distanceHigh, DWORD moveMethod)
{
    if (!MuFileHandleValid(hFile))
        return INVALID_SET_FILE_POINTER;
    const int whence = (moveMethod == FILE_END)       ? SEEK_END
                       : (moveMethod == FILE_CURRENT) ? SEEK_CUR
                                                      : SEEK_SET;

    // Combine the optional high dword for offsets past 2GB.
    std::int64_t offset = distance;
    if (distanceHigh)
        offset =
            (static_cast<std::int64_t>(*distanceHigh) << 32) | static_cast<std::uint32_t>(distance);

    const off_t pos = ::lseek(mu_detail::HandleToFd(hFile), static_cast<off_t>(offset), whence);
    if (pos == static_cast<off_t>(-1))
        return INVALID_SET_FILE_POINTER;
    if (distanceHigh)
        *distanceHigh = static_cast<LONG>(pos >> 32);
    return static_cast<DWORD>(pos & 0xFFFFFFFF);
}

#ifndef INVALID_FILE_SIZE
#define INVALID_FILE_SIZE (static_cast<DWORD>(-1))
#endif
inline DWORD GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh)
{
    if (!MuFileHandleValid(hFile))
        return INVALID_FILE_SIZE;
    // fstat is a single syscall, thread-safe, and leaves the fd offset
    // untouched (unlike seeking to the end and back).
    struct stat st{};
    if (::fstat(mu_detail::HandleToFd(hFile), &st) != 0)
        return INVALID_FILE_SIZE;
    if (lpFileSizeHigh)
        *lpFileSizeHigh = static_cast<DWORD>(static_cast<std::uint64_t>(st.st_size) >> 32);
    return static_cast<DWORD>(static_cast<std::uint64_t>(st.st_size) & 0xFFFFFFFF);
}

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES (static_cast<DWORD>(-1))
#endif
#ifndef FILE_ATTRIBUTE_DIRECTORY
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#endif
inline DWORD GetFileAttributesW(LPCWSTR lpFileName)
{
    char path[4096] = {0};
    if (!lpFileName || WideCharToMultiByte(CP_UTF8, 0, lpFileName, -1, path, sizeof(path) - 1,
                                           nullptr, nullptr) == 0)
        return INVALID_FILE_ATTRIBUTES;
    struct stat st{};
    if (::stat(MuResolvePath(path).c_str(), &st) != 0)
        return INVALID_FILE_ATTRIBUTES;
    return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}
#ifndef GetFileAttributes
#define GetFileAttributes GetFileAttributesW
#endif

inline BOOL CreateDirectoryW(LPCWSTR lpPathName, void * /*securityAttributes*/)
{
    char path[4096] = {0};
    if (!lpPathName || WideCharToMultiByte(CP_UTF8, 0, lpPathName, -1, path, sizeof(path) - 1,
                                           nullptr, nullptr) == 0)
        return FALSE;
    return (::mkdir(MuResolvePath(path).c_str(), 0755) == 0) ? TRUE : FALSE;
}
#ifndef CreateDirectory
#define CreateDirectory CreateDirectoryW
#endif

// Current working directory (Win32 GetCurrentDirectoryW): on success returns the
// length copied (without the terminator); if the buffer is too small or null,
// returns the required size *including* the terminator, like the Win32 contract,
// so callers that query-then-allocate keep working; 0 on failure.
inline DWORD GetCurrentDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer)
{
    char path[4096] = {0};
    if (!::getcwd(path, sizeof(path)))
        return 0;
    const int required = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0); // includes the null
    if (required <= 0)
        return 0;
    if (!lpBuffer || nBufferLength < static_cast<DWORD>(required))
        return static_cast<DWORD>(required);
    const int converted =
        MultiByteToWideChar(CP_UTF8, 0, path, -1, lpBuffer, static_cast<int>(nBufferLength));
    return (converted > 0) ? static_cast<DWORD>(converted - 1) : 0;
}
#ifndef GetCurrentDirectory
#define GetCurrentDirectory GetCurrentDirectoryW
#endif

inline BOOL DeleteFileW(LPCWSTR lpFileName)
{
    char path[4096] = {0};
    if (!lpFileName || WideCharToMultiByte(CP_UTF8, 0, lpFileName, -1, path, sizeof(path) - 1,
                                           nullptr, nullptr) == 0)
        return FALSE;
    return (::unlink(MuResolvePath(path).c_str()) == 0) ? TRUE : FALSE;
}
#ifndef DeleteFile
#define DeleteFile DeleteFileW
#endif

typedef struct _SYSTEMTIME
{
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME, *LPSYSTEMTIME, *PSYSTEMTIME;

inline void GetLocalTime(LPSYSTEMTIME st)
{
    if (!st)
        return;
    const time_t t = ::time(nullptr);
    struct tm lt{};
    ::localtime_r(&t, &lt);
    st->wYear = static_cast<WORD>(lt.tm_year + 1900);
    st->wMonth = static_cast<WORD>(lt.tm_mon + 1);
    st->wDayOfWeek = static_cast<WORD>(lt.tm_wday);
    st->wDay = static_cast<WORD>(lt.tm_mday);
    st->wHour = static_cast<WORD>(lt.tm_hour);
    st->wMinute = static_cast<WORD>(lt.tm_min);
    st->wSecond = static_cast<WORD>(lt.tm_sec);
    st->wMilliseconds = 0;
}

#endif // !_WIN32

// Portable Win32 directory-enumeration / file-operation shim.
// The editor's atomic-save/backup code (CommonDataSaver) enumerates backup
// files with FindFirstFileW/FindNextFileW, copies and atomically renames them
// with CopyFileW/MoveFileExW, and sorts them by FILETIME. On Windows these come
// from <windows.h>; elsewhere this maps the used subset onto POSIX (dirent,
// stat, fnmatch, rename). Editor-only, so it is pulled in via the editor sources.

#ifndef _WIN32

// ---- FILETIME ---------------------------------------------------------------
// A 64-bit timestamp split into two DWORDs, like the Win32 struct. The shim
// stores the POSIX mtime (seconds); only the ordering matters to callers
// (sorting backups), not the 1601 epoch.
#ifndef _FILETIME_DEFINED
#define _FILETIME_DEFINED
typedef struct _FILETIME
{
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;
#endif

inline std::uint64_t MuFileTimeValue(const FILETIME &ft)
{
    return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

inline LONG CompareFileTime(const FILETIME *a, const FILETIME *b)
{
    if (!a || !b)
        return 0;
    const std::uint64_t va = MuFileTimeValue(*a), vb = MuFileTimeValue(*b);
    return (va < vb) ? -1 : (va > vb ? 1 : 0);
}

// ---- WIN32_FIND_DATAW + FindFirstFileW / FindNextFileW / FindClose ----------
typedef struct _WIN32_FIND_DATAW
{
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    wchar_t cFileName[MAX_PATH];
    wchar_t cAlternateFileName[14];
} WIN32_FIND_DATAW, *PWIN32_FIND_DATAW, *LPWIN32_FIND_DATAW;

namespace mu_detail
{
// Live state for an in-progress directory scan, carried in the HANDLE.
struct FindState
{
    DIR *dir = nullptr;
    std::string dirPathUtf8; // directory, with trailing '/'
    std::string globUtf8;    // filename wildcard (Win32 *, ? -> fnmatch)
};

// Fill `out` for `dirPath/name`; returns false if the entry vanished.
inline bool FillFindData(const std::string &dirPath, const char *name, WIN32_FIND_DATAW &out)
{
    std::memset(&out, 0, sizeof(out));
    MultiByteToWideChar(CP_UTF8, 0, name, -1, out.cFileName, MAX_PATH);

    struct stat st{};
    const std::string full = dirPath + name;
    if (::stat(full.c_str(), &st) != 0)
        return false;

    out.dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    const std::uint64_t mtime = static_cast<std::uint64_t>(st.st_mtime);
    out.ftLastWriteTime.dwLowDateTime = static_cast<DWORD>(mtime & 0xFFFFFFFF);
    out.ftLastWriteTime.dwHighDateTime = static_cast<DWORD>(mtime >> 32);
    out.nFileSizeLow = static_cast<DWORD>(st.st_size & 0xFFFFFFFF);
    out.nFileSizeHigh = static_cast<DWORD>(static_cast<std::uint64_t>(st.st_size) >> 32);
    return true;
}

// Advance the scan to the next entry matching the glob; fill `out`.
inline bool FindAdvance(FindState *s, WIN32_FIND_DATAW &out)
{
    for (;;)
    {
        errno = 0;
        const dirent *e = ::readdir(s->dir);
        if (!e)
            return false;
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0)
            continue;
        if (::fnmatch(s->globUtf8.c_str(), e->d_name, 0) != 0)
            continue;
        if (FillFindData(s->dirPathUtf8, e->d_name, out))
            return true;
    }
}
} // namespace mu_detail

inline HANDLE FindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData)
{
    if (!lpFileName || !lpFindFileData)
        return INVALID_HANDLE_VALUE;

    char patternUtf8[4096] = {0};
    if (WideCharToMultiByte(CP_UTF8, 0, lpFileName, -1, patternUtf8, sizeof(patternUtf8) - 1,
                            nullptr, nullptr) == 0)
        return INVALID_HANDLE_VALUE;

    // Split into directory and filename wildcard (accept either separator).
    std::string pattern(patternUtf8);
    for (char &c : pattern)
        if (c == '\\')
            c = '/';
    const std::string::size_type slash = pattern.find_last_of('/');
    std::string dirPath =
        (slash == std::string::npos) ? std::string("./") : pattern.substr(0, slash + 1);
    const std::string glob = (slash == std::string::npos) ? pattern : pattern.substr(slash + 1);

    auto *s = new mu_detail::FindState();
    // Resolve the directory once and reuse it: FillFindData stat()s
    // dirPathUtf8 + name, so storing the unresolved (wrong-case) path would make
    // every stat fail on a case-sensitive filesystem and skip every entry.
    std::string resolvedDir = MuResolvePath(dirPath.c_str());
    if (!resolvedDir.empty() && resolvedDir.back() != '/')
        resolvedDir += '/';
    s->dir = ::opendir(resolvedDir.c_str());
    if (!s->dir)
    {
        delete s;
        return INVALID_HANDLE_VALUE;
    }
    s->dirPathUtf8 = resolvedDir;
    s->globUtf8 = glob;

    if (!mu_detail::FindAdvance(s, *lpFindFileData))
    {
        ::closedir(s->dir);
        delete s;
        return INVALID_HANDLE_VALUE;
    }
    return reinterpret_cast<HANDLE>(s);
}

inline BOOL FindNextFileW(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData)
{
    if (hFindFile == INVALID_HANDLE_VALUE || !hFindFile || !lpFindFileData)
        return FALSE;
    auto *s = reinterpret_cast<mu_detail::FindState *>(hFindFile);
    return mu_detail::FindAdvance(s, *lpFindFileData) ? TRUE : FALSE;
}

inline BOOL FindClose(HANDLE hFindFile)
{
    if (hFindFile == INVALID_HANDLE_VALUE || !hFindFile)
        return FALSE;
    auto *s = reinterpret_cast<mu_detail::FindState *>(hFindFile);
    if (s->dir)
        ::closedir(s->dir);
    delete s;
    return TRUE;
}
#ifndef FindFirstFile
#define FindFirstFile FindFirstFileW
#define FindNextFile FindNextFileW
#endif

// ---- CopyFileW / MoveFileExW ------------------------------------------------
inline BOOL CopyFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, BOOL bFailIfExists)
{
    char src[4096] = {0}, dst[4096] = {0};
    if (!lpExistingFileName || !lpNewFileName)
        return FALSE;
    if (WideCharToMultiByte(CP_UTF8, 0, lpExistingFileName, -1, src, sizeof(src) - 1, nullptr,
                            nullptr) == 0)
        return FALSE;
    if (WideCharToMultiByte(CP_UTF8, 0, lpNewFileName, -1, dst, sizeof(dst) - 1, nullptr,
                            nullptr) == 0)
        return FALSE;

    const std::string srcPath = MuResolvePath(src);
    const std::string dstPath = MuResolvePath(dst);
    // Copying a file onto itself would open the destination "wb" and truncate
    // the source to zero before any bytes are read.
    if (srcPath == dstPath)
        return FALSE;
    if (bFailIfExists && ::access(dstPath.c_str(), F_OK) == 0)
        return FALSE;

    FILE *in = ::fopen(srcPath.c_str(), "rb");
    if (!in)
        return FALSE;
    FILE *out = ::fopen(dstPath.c_str(), "wb");
    if (!out)
    {
        ::fclose(in);
        return FALSE;
    }

    char buf[65536];
    size_t n;
    bool ok = true;
    while ((n = ::fread(buf, 1, sizeof(buf), in)) > 0)
        if (::fwrite(buf, 1, n, out) != n)
        {
            ok = false;
            break;
        }
    if (::ferror(in))
        ok = false;
    ::fclose(in);
    if (::fclose(out) != 0)
        ok = false;
    // Don't leave a truncated/corrupt destination behind on failure.
    if (!ok)
        ::unlink(dstPath.c_str());
    return ok ? TRUE : FALSE;
}
#ifndef CopyFile
#define CopyFile CopyFileW
#endif

#ifndef MOVEFILE_REPLACE_EXISTING
#define MOVEFILE_REPLACE_EXISTING 0x00000001
#define MOVEFILE_COPY_ALLOWED 0x00000002
#define MOVEFILE_WRITE_THROUGH 0x00000008
#endif
inline BOOL MoveFileExW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, DWORD /*dwFlags*/)
{
    char src[4096] = {0}, dst[4096] = {0};
    if (!lpExistingFileName || !lpNewFileName)
        return FALSE;
    if (WideCharToMultiByte(CP_UTF8, 0, lpExistingFileName, -1, src, sizeof(src) - 1, nullptr,
                            nullptr) == 0)
        return FALSE;
    if (WideCharToMultiByte(CP_UTF8, 0, lpNewFileName, -1, dst, sizeof(dst) - 1, nullptr,
                            nullptr) == 0)
        return FALSE;
    // rename(2) replaces the destination atomically, which covers the
    // REPLACE_EXISTING | WRITE_THROUGH the caller uses to publish a backup.
    return (::rename(MuResolvePath(src).c_str(), MuResolvePath(dst).c_str()) == 0) ? TRUE : FALSE;
}
#ifndef MoveFileEx
#define MoveFileEx MoveFileExW
#endif

#endif // !_WIN32

// Portable GDI struct shim.
// The terrain/UI code reads and builds Windows DIB/BMP structures for file I/O
// and texture uploads. On Windows these come from <wingdi.h> (via <windows.h>);
// elsewhere this provides just the structs and constants used, laid out to match
// the on-disk/in-memory Windows formats. GDI *functions* are a separate concern.

#ifndef _WIN32

#ifndef BI_RGB
#define BI_RGB 0
#endif

// The file header is read/written with sizeof == 14, so it must be packed (the
// DWORD bfSize sits at offset 2, not 4).
#pragma pack(push, 2)
typedef struct tagBITMAPFILEHEADER
{
    WORD bfType;
    DWORD bfSize;
    WORD bfReserved1;
    WORD bfReserved2;
    DWORD bfOffBits;
} BITMAPFILEHEADER, *LPBITMAPFILEHEADER, *PBITMAPFILEHEADER;
#pragma pack(pop)

typedef struct tagBITMAPINFOHEADER
{
    DWORD biSize;
    LONG biWidth;
    LONG biHeight;
    WORD biPlanes;
    WORD biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG biXPelsPerMeter;
    LONG biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BITMAPINFOHEADER, *LPBITMAPINFOHEADER, *PBITMAPINFOHEADER;

typedef struct tagRGBQUAD
{
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFO
{
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD bmiColors[1];
} BITMAPINFO, *LPBITMAPINFO, *PBITMAPINFO;

typedef struct tagPALETTEENTRY
{
    BYTE peRed;
    BYTE peGreen;
    BYTE peBlue;
    BYTE peFlags;
} PALETTEENTRY, *LPPALETTEENTRY, *PPALETTEENTRY;

// ---- Font creation / text metrics / DIB text rasterization (wingdi.h) -------
// The text is rendered through CUIRenderText, a Win32 GDI font-DC pipeline:
// TextOut rasterizes white-on-black into a 24bpp DIB section, the engine scans
// the pixels into a GL texture. Off Windows the same pipeline is backed by a
// stb_truetype rasterizer (GdiText.cpp): CreateFont picks a
// system TTF by weight, CreateDIBSection allocates the pixel buffer with DIB
// pitch rules, and TextOut draws antialiased glyphs into it.

#ifndef FW_BOLD
#define FW_BOLD 700
#endif
#ifndef FW_NORMAL
#define FW_NORMAL 400
#endif
#ifndef FW_SEMIBOLD
#define FW_SEMIBOLD 600
#endif
#ifndef CLEARTYPE_NATURAL_QUALITY
#define CLEARTYPE_NATURAL_QUALITY 6
#endif
#ifndef DEFAULT_CHARSET
#define DEFAULT_CHARSET 1
#endif
#ifndef OUT_DEFAULT_PRECIS
#define OUT_DEFAULT_PRECIS 0
#endif
#ifndef CLIP_DEFAULT_PRECIS
#define CLIP_DEFAULT_PRECIS 0
#endif
#ifndef DEFAULT_QUALITY
#define DEFAULT_QUALITY 0
#endif
#ifndef NONANTIALIASED_QUALITY
#define NONANTIALIASED_QUALITY 3
#endif
#ifndef ANTIALIASED_QUALITY
#define ANTIALIASED_QUALITY 4
#endif
#ifndef DEFAULT_PITCH
#define DEFAULT_PITCH 0
#endif
#ifndef FF_DONTCARE
#define FF_DONTCARE 0
#endif

HFONT CreateFontW(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight,
                  DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet,
                  DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily,
                  LPCWSTR pszFaceName);
#ifndef CreateFont
#define CreateFont CreateFontW
#endif

BOOL GetTextExtentPoint32W(HDC hdc, LPCWSTR lpString, int c, LPSIZE psizl);
#ifndef GetTextExtentPoint32
#define GetTextExtentPoint32 GetTextExtentPoint32W
#endif

// Release a GDI object. The engine also declares its own DeleteObject(OBJECT*)
// for game objects; these typed overloads coexist with it (an HFONT/HGDIOBJ
// argument selects one of these, an OBJECT* selects the engine's).
BOOL DeleteObject(HFONT hObject);
BOOL DeleteObject(HGDIOBJ hObject);
BOOL DeleteObject(HBITMAP hObject);
BOOL DeleteObject(HBRUSH hObject);

#ifndef DIB_RGB_COLORS
#define DIB_RGB_COLORS 0
#endif

HBITMAP CreateDIBSection(HDC hdc, const BITMAPINFO *pbmi, UINT usage, void **ppvBits,
                         HANDLE hSection, DWORD offset);
HDC CreateCompatibleDC(HDC hdc);
BOOL DeleteDC(HDC hdc);

// SelectObject returns the previously selected object of the same kind.
HGDIOBJ SelectObject(HDC hdc, HGDIOBJ h);
HGDIOBJ SelectObject(HDC hdc, HBITMAP h);
HGDIOBJ SelectObject(HDC hdc, HFONT h);
inline HGDIOBJ GetStockObject(int)
{
    return nullptr;
}

BOOL TextOut(HDC hdc, int x, int y, LPCWSTR lpString, int c);
COLORREF SetBkColor(HDC hdc, COLORREF color);
COLORREF SetTextColor(HDC hdc, COLORREF color);

// UI-font discovery diagnostics for the crash log: one line per resolved font
// weight (which file loaded, or that none was found). Empty until the first
// font is created. Implemented in GdiText.cpp.
std::string MuFontDiagnostics();

#endif // !_WIN32

template <class T> class CList;

template <class T> class CNode
{
  public:
    CNode() = default;
    explicit CNode(const T &data) : m_Data(data)
    {
    }

    void SetData(const T &data)
    {
        m_Data = data;
    }
    T &GetData()
    {
        return m_Data;
    }
    const T &GetData() const
    {
        return m_Data;
    }

  private:
    void SetPrev(CNode<T> *previous)
    {
        m_pPrev = previous;
    }
    void SetNext(CNode<T> *next)
    {
        m_pNext = next;
    }
    CNode<T> *GetPrev()
    {
        return m_pPrev;
    }
    const CNode<T> *GetPrev() const
    {
        return m_pPrev;
    }
    CNode<T> *GetNext()
    {
        return m_pNext;
    }
    const CNode<T> *GetNext() const
    {
        return m_pNext;
    }

    friend class CList<T>;

    T m_Data{};
    CNode<T> *m_pPrev{nullptr};
    CNode<T> *m_pNext{nullptr};
};

template <class T> class CList
{
  public:
    CList();
    ~CList();

    std::size_t GetCount() const
    {
        return m_Count;
    }
    bool IsEmpty() const
    {
        return m_Count == 0;
    }

    CNode<T> *AddHead(const T &newElement);
    CNode<T> *AddTail(const T &newElement);
    CNode<T> *InsertBefore(CNode<T> *node, const T &newElement);
    CNode<T> *InsertAfter(CNode<T> *node, const T &newElement);

    T RemoveHead();
    T RemoveTail();
    T RemoveNode(CNode<T> *&node);
    void RemoveAll();

    CNode<T> *FindHead();
    const CNode<T> *FindHead() const;
    CNode<T> *FindTail();
    const CNode<T> *FindTail() const;
    CNode<T> *FindNode(const T &searchValue);
    const CNode<T> *FindNode(const T &searchValue) const;

    CNode<T> *GetPrev(CNode<T> *node);
    const CNode<T> *GetPrev(const CNode<T> *node) const;
    CNode<T> *GetNext(CNode<T> *node);
    const CNode<T> *GetNext(const CNode<T> *node) const;

    void SetData(CNode<T> *node, const T &data);
    T &GetData(CNode<T> *node);
    const T &GetData(const CNode<T> *node) const;

  private:
    CNode<T> *CreateDataNode(const T &value);
    bool IsSentinel(const CNode<T> *node) const
    {
        return node == m_pHead || node == m_pTail;
    }

    std::size_t m_Count{0};
    CNode<T> *m_pHead{nullptr};
    CNode<T> *m_pTail{nullptr};
    T m_NullData{};
};

template <class T> CList<T>::CList() : m_pHead(new CNode<T>()), m_pTail(new CNode<T>())
{
    m_pHead->SetNext(m_pTail);
    m_pTail->SetPrev(m_pHead);
}

template <class T> CList<T>::~CList()
{
    RemoveAll();
    delete m_pTail;
    delete m_pHead;
}

template <class T> CNode<T> *CList<T>::CreateDataNode(const T &value)
{
    return new CNode<T>(value);
}

template <class T> CNode<T> *CList<T>::AddHead(const T &newElement)
{
    auto *node = CreateDataNode(newElement);
    auto *first = m_pHead->GetNext();
    node->SetNext(first);
    first->SetPrev(node);
    node->SetPrev(m_pHead);
    m_pHead->SetNext(node);
    ++m_Count;
    return node;
}

template <class T> CNode<T> *CList<T>::AddTail(const T &newElement)
{
    auto *node = CreateDataNode(newElement);
    auto *last = m_pTail->GetPrev();
    node->SetPrev(last);
    last->SetNext(node);
    node->SetNext(m_pTail);
    m_pTail->SetPrev(node);
    ++m_Count;
    return node;
}

template <class T> CNode<T> *CList<T>::InsertBefore(CNode<T> *node, const T &newElement)
{
    if (!node || node == m_pHead)
    {
        return nullptr;
    }

    auto *previous = node->GetPrev();
    auto *newNode = CreateDataNode(newElement);
    newNode->SetPrev(previous);
    newNode->SetNext(node);
    previous->SetNext(newNode);
    node->SetPrev(newNode);
    ++m_Count;
    return newNode;
}

template <class T> CNode<T> *CList<T>::InsertAfter(CNode<T> *node, const T &newElement)
{
    if (!node || node == m_pTail)
    {
        return nullptr;
    }

    auto *next = node->GetNext();
    auto *newNode = CreateDataNode(newElement);
    newNode->SetNext(next);
    newNode->SetPrev(node);
    next->SetPrev(newNode);
    node->SetNext(newNode);
    ++m_Count;
    return newNode;
}

template <class T> T CList<T>::RemoveHead()
{
    if (IsEmpty())
    {
        throw std::out_of_range("Cannot remove from an empty list.");
    }

    auto *node = m_pHead->GetNext();
    auto *next = node->GetNext();
    next->SetPrev(m_pHead);
    m_pHead->SetNext(next);
    T data = node->GetData();
    delete node;
    --m_Count;
    return data;
}

template <class T> T CList<T>::RemoveTail()
{
    if (IsEmpty())
    {
        throw std::out_of_range("Cannot remove from an empty list.");
    }

    auto *node = m_pTail->GetPrev();
    auto *previous = node->GetPrev();
    previous->SetNext(m_pTail);
    m_pTail->SetPrev(previous);
    T data = node->GetData();
    delete node;
    --m_Count;
    return data;
}

template <class T> T CList<T>::RemoveNode(CNode<T> *&node)
{
    if (!node || IsSentinel(node))
    {
        throw std::invalid_argument("Cannot remove a null or sentinel node.");
    }

    auto *next = node->GetNext();
    auto *prev = node->GetPrev();
    prev->SetNext(next);
    next->SetPrev(prev);
    T data = node->GetData();
    delete node;
    node = (next != m_pTail) ? next : nullptr;
    --m_Count;
    return data;
}

template <class T> void CList<T>::RemoveAll()
{
    CNode<T> *current = m_pHead->GetNext();
    while (current != m_pTail)
    {
        CNode<T> *toDelete = current;
        current = current->GetNext();
        delete toDelete;
    }

    m_pHead->SetNext(m_pTail);
    m_pTail->SetPrev(m_pHead);
    m_Count = 0;
}

template <class T> CNode<T> *CList<T>::FindHead()
{
    return IsEmpty() ? nullptr : m_pHead->GetNext();
}

template <class T> const CNode<T> *CList<T>::FindHead() const
{
    return IsEmpty() ? nullptr : m_pHead->GetNext();
}

template <class T> CNode<T> *CList<T>::FindTail()
{
    return IsEmpty() ? nullptr : m_pTail->GetPrev();
}

template <class T> const CNode<T> *CList<T>::FindTail() const
{
    return IsEmpty() ? nullptr : m_pTail->GetPrev();
}

template <class T> CNode<T> *CList<T>::FindNode(const T &searchValue)
{
    CNode<T> *current = m_pHead->GetNext();
    while (current != m_pTail)
    {
        if (current->GetData() == searchValue)
        {
            return current;
        }
        current = current->GetNext();
    }
    return nullptr;
}

template <class T> const CNode<T> *CList<T>::FindNode(const T &searchValue) const
{
    const CNode<T> *current = m_pHead->GetNext();
    while (current != m_pTail)
    {
        if (current->GetData() == searchValue)
        {
            return current;
        }
        current = current->GetNext();
    }
    return nullptr;
}

template <class T> CNode<T> *CList<T>::GetPrev(CNode<T> *node)
{
    if (!node)
    {
        return nullptr;
    }
    auto *prev = node->GetPrev();
    return (prev == m_pHead) ? nullptr : prev;
}

template <class T> const CNode<T> *CList<T>::GetPrev(const CNode<T> *node) const
{
    if (!node)
    {
        return nullptr;
    }
    auto *prev = node->GetPrev();
    return (prev == m_pHead) ? nullptr : prev;
}

template <class T> CNode<T> *CList<T>::GetNext(CNode<T> *node)
{
    if (!node)
    {
        return nullptr;
    }
    auto *next = node->GetNext();
    return (next == m_pTail) ? nullptr : next;
}

template <class T> const CNode<T> *CList<T>::GetNext(const CNode<T> *node) const
{
    if (!node)
    {
        return nullptr;
    }
    auto *next = node->GetNext();
    return (next == m_pTail) ? nullptr : next;
}

template <class T> void CList<T>::SetData(CNode<T> *node, const T &data)
{
    if (!node || IsSentinel(node))
    {
        return;
    }
    node->SetData(data);
}

template <class T> T &CList<T>::GetData(CNode<T> *node)
{
    if (IsSentinel(node))
    {
        throw std::out_of_range("Attempted to get data from a sentinel node.");
    }
    return node->GetData();
}

template <class T> const T &CList<T>::GetData(const CNode<T> *node) const
{
    if (IsSentinel(node))
    {
        throw std::out_of_range("Attempted to get data from a sentinel node.");
    }
    return node->GetData();
}

template <class T> class CQueue : private CList<T>
{
  public:
    CQueue() = default;
    ~CQueue() = default;

    bool Insert(const T &newElement);
    T Remove();
    void CleanUp();

    bool Find(const T &element);

    std::size_t GetCount() const
    {
        return CList<T>::GetCount();
    }
};

template <class T> bool CQueue<T>::Insert(const T &newElement)
{
    return this->AddTail(newElement) != nullptr;
}

template <class T> T CQueue<T>::Remove()
{
    return this->RemoveHead();
}

template <class T> void CQueue<T>::CleanUp()
{
    this->RemoveAll();
}

template <class T> bool CQueue<T>::Find(const T &element)
{
    return this->FindNode(element) != nullptr;
}

template <class T, class S> class CBTree;

template <class T, class S> class CBNode
{
  public:
    CBNode() = default;
    CBNode(const T &data, const S &value) : m_Data(data), m_CompValue(value)
    {
    }

    void SetData(const T &data)
    {
        m_Data = data;
    }
    void SetValue(const S &value)
    {
        m_CompValue = value;
    }
    T &GetData()
    {
        return m_Data;
    }
    const T &GetData() const
    {
        return m_Data;
    }
    S GetValue() const
    {
        return m_CompValue;
    }

  private:
    void SetLeft(CBNode<T, S> *left)
    {
        m_pLeft = left;
        if (m_pLeft)
        {
            m_pLeft->m_pParent = this;
        }
    }

    void SetRight(CBNode<T, S> *right)
    {
        m_pRight = right;
        if (m_pRight)
        {
            m_pRight->m_pParent = this;
        }
    }

    CBNode<T, S> *GetLeft()
    {
        return m_pLeft;
    }
    const CBNode<T, S> *GetLeft() const
    {
        return m_pLeft;
    }
    CBNode<T, S> *GetRight()
    {
        return m_pRight;
    }
    const CBNode<T, S> *GetRight() const
    {
        return m_pRight;
    }
    CBNode<T, S> *GetParent()
    {
        return m_pParent;
    }
    const CBNode<T, S> *GetParent() const
    {
        return m_pParent;
    }

    friend class CBTree<T, S>;

    T m_Data{};
    S m_CompValue{};
    CBNode<T, S> *m_pLeft{nullptr};
    CBNode<T, S> *m_pRight{nullptr};
    CBNode<T, S> *m_pParent{nullptr};
};

template <class T, class S> class CBTree
{
  public:
    CBTree();
    ~CBTree();

    CBNode<T, S> *Add(const T &newElement, const S &value);
    void RemoveNode(CBNode<T, S> *&node);
    void RemoveAll();

    bool IsEmpty() const
    {
        return m_Count == 0;
    }
    std::size_t GetCount() const
    {
        return m_Count;
    }

    CBNode<T, S> *FindHead();
    CBNode<T, S> *FindNode(const S &value);
    const CBNode<T, S> *FindNode(const S &value) const;
    CBNode<T, S> *GetLeft(CBNode<T, S> *node);
    CBNode<T, S> *GetRight(CBNode<T, S> *node);
    CBNode<T, S> *GetParent(CBNode<T, S> *node);

    void SetData(CBNode<T, S> *node, const T &data);
    T &GetData(CBNode<T, S> *node);
    S GetValue(CBNode<T, S> *node);

    void Cycle(std::function<void(const T &, const S &)> process);

  private:
    CBNode<T, S> *CreateNode(const T &value, const S &compValue);
    void RemoveFrom(CBNode<T, S> *node);
    void CycleFrom(CBNode<T, S> *node, std::function<void(const T &, const S &)> process);

    std::size_t m_Count{0};
    CBNode<T, S> *m_pHead{nullptr};
};

template <class T, class S> CBTree<T, S>::CBTree() : m_pHead(nullptr)
{
}

template <class T, class S> CBTree<T, S>::~CBTree()
{
    RemoveAll();
}

template <class T, class S>
CBNode<T, S> *CBTree<T, S>::CreateNode(const T &value, const S &compValue)
{
    return new CBNode<T, S>(value, compValue);
}

template <class T, class S> CBNode<T, S> *CBTree<T, S>::Add(const T &newElement, const S &value)
{
    if (!m_pHead)
    {
        m_pHead = CreateNode(newElement, value);
        ++m_Count;
        return m_pHead;
    }

    CBNode<T, S> *current = m_pHead;
    while (true)
    {
        if (value < current->GetValue())
        {
            if (current->GetLeft())
            {
                current = current->GetLeft();
            }
            else
            {
                current->SetLeft(CreateNode(newElement, value));
                ++m_Count;
                return current->GetLeft();
            }
        }
        else
        {
            if (current->GetRight())
            {
                current = current->GetRight();
            }
            else
            {
                current->SetRight(CreateNode(newElement, value));
                ++m_Count;
                return current->GetRight();
            }
        }
    }
}

template <class T, class S> CBNode<T, S> *CBTree<T, S>::FindNode(const S &value)
{
    CBNode<T, S> *current = m_pHead;
    while (current)
    {
        if (value == current->GetValue())
        {
            return current;
        }
        if (value < current->GetValue())
        {
            current = current->GetLeft();
        }
        else
        {
            current = current->GetRight();
        }
    }
    return nullptr;
}

template <class T, class S> const CBNode<T, S> *CBTree<T, S>::FindNode(const S &value) const
{
    const CBNode<T, S> *current = m_pHead;
    while (current)
    {
        if (value == current->GetValue())
        {
            return current;
        }
        if (value < current->GetValue())
        {
            current = current->GetLeft();
        }
        else
        {
            current = current->GetRight();
        }
    }
    return nullptr;
}

template <class T, class S> void CBTree<T, S>::RemoveNode(CBNode<T, S> *&node)
{
    if (!node)
    {
        return;
    }

    if (node->GetLeft() && node->GetRight())
    {
        CBNode<T, S> *minNode = node->GetRight();
        while (minNode->GetLeft())
        {
            minNode = minNode->GetLeft();
        }

        node->SetData(minNode->GetData());
        node->SetValue(minNode->GetValue());
        RemoveNode(minNode);
    }
    else if (node->GetLeft())
    {
        CBNode<T, S> *left = node->GetLeft();
        if (node == m_pHead)
        {
            m_pHead = left;
        }
        else if (node->GetParent()->GetLeft() == node)
        {
            node->GetParent()->SetLeft(left);
        }
        else
        {
            node->GetParent()->SetRight(left);
        }
        delete node;
        --m_Count;
    }
    else if (node->GetRight())
    {
        CBNode<T, S> *right = node->GetRight();
        if (node == m_pHead)
        {
            m_pHead = right;
        }
        else if (node->GetParent()->GetLeft() == node)
        {
            node->GetParent()->SetLeft(right);
        }
        else
        {
            node->GetParent()->SetRight(right);
        }
        delete node;
        --m_Count;
    }
    else
    {
        if (node == m_pHead)
        {
            m_pHead = nullptr;
        }
        else if (node->GetParent()->GetLeft() == node)
        {
            node->GetParent()->SetLeft(nullptr);
        }
        else
        {
            node->GetParent()->SetRight(nullptr);
        }
        delete node;
        --m_Count;
    }
}

template <class T, class S> void CBTree<T, S>::RemoveAll()
{
    RemoveFrom(m_pHead);
    m_pHead = nullptr;
    m_Count = 0;
}

template <class T, class S> void CBTree<T, S>::RemoveFrom(CBNode<T, S> *node)
{
    if (!node)
    {
        return;
    }

    std::vector<std::pair<CBNode<T, S> *, bool>> stack;
    stack.emplace_back(node, false);

    while (!stack.empty())
    {
        auto currentEntry = stack.back();
        stack.pop_back();

        CBNode<T, S> *current = currentEntry.first;
        bool visited = currentEntry.second;

        if (!current)
        {
            continue;
        }

        if (visited)
        {
            delete current;
            continue;
        }

        stack.emplace_back(current, true);
        if (current->GetRight())
        {
            stack.emplace_back(current->GetRight(), false);
        }
        if (current->GetLeft())
        {
            stack.emplace_back(current->GetLeft(), false);
        }
    }
}

template <class T, class S> CBNode<T, S> *CBTree<T, S>::FindHead()
{
    return m_pHead;
}

template <class T, class S> CBNode<T, S> *CBTree<T, S>::GetLeft(CBNode<T, S> *node)
{
    return node->GetLeft();
}

template <class T, class S> CBNode<T, S> *CBTree<T, S>::GetRight(CBNode<T, S> *node)
{
    return node->GetRight();
}

template <class T, class S> CBNode<T, S> *CBTree<T, S>::GetParent(CBNode<T, S> *node)
{
    return node->GetParent();
}

template <class T, class S> void CBTree<T, S>::SetData(CBNode<T, S> *node, const T &data)
{
    node->SetData(data);
}

template <class T, class S> T &CBTree<T, S>::GetData(CBNode<T, S> *node)
{
    return node->GetData();
}

template <class T, class S> S CBTree<T, S>::GetValue(CBNode<T, S> *node)
{
    return node->GetValue();
}

template <class T, class S>
void CBTree<T, S>::Cycle(std::function<void(const T &, const S &)> process)
{
    CycleFrom(m_pHead, process);
}

template <class T, class S>
void CBTree<T, S>::CycleFrom(CBNode<T, S> *node, std::function<void(const T &, const S &)> process)
{
    if (!node)
    {
        return;
    }

    CycleFrom(node->GetLeft(), process);
    process(node->GetData(), node->GetValue());
    CycleFrom(node->GetRight(), process);
}

template <class T> class CDimension
{
  protected:
    int m_nSize;
    T *m_pData;

  public:
    CDimension();  // Constructor
    ~CDimension(); // Destructor

    T Set(int nIndex, T Data);
    T Get(int nIndex);

  protected:
    void CheckDimensionSize(int nIndex);
};

template <class T> CDimension<T>::CDimension()
{
    m_nSize = 16;
    m_pData = new T[m_nSize];
}

template <class T> CDimension<T>::~CDimension()
{
    delete[] m_pData;
}

template <class T> T CDimension<T>::Set(int nIndex, T Data)
{
    CheckDimensionSize(nIndex);
    m_pData[nIndex] = Data;

    return (Data);
}

template <class T> T CDimension<T>::Get(int nIndex)
{
    CheckDimensionSize(nIndex);
    return (m_pData[nIndex]);
}

template <class T> void CDimension<T>::CheckDimensionSize(int nIndex)
{
    if (nIndex >= m_nSize)
    {
        int nNewSize = m_nSize;
        for (; nNewSize <= nIndex; nNewSize *= 2)
        {
        }

        T *pTempBuffer = new T[m_nSize];
        memcpy(pTempBuffer, m_pData, m_nSize * sizeof(T));
        delete[] m_pData;
        m_pData = new T[nNewSize];
        memcpy(m_pData, pTempBuffer, m_nSize * sizeof(T));
        m_nSize = nNewSize;
        delete[] pTempBuffer;
    }
}

class CSubject;

class CObserver
{
  public:
    virtual ~CObserver();
    virtual void UpdateData(CSubject *pChangedSubject) = 0;

  protected:
    CObserver();
};

#ifndef _PLIST_H_
#define _PLIST_H_

struct NODE
{
    NODE *pNext;
    NODE *pPrev;
    void *data;
};

class CPList
{
  public:
    CPList();
    ~CPList();

    // Head/Tail Access
    void *GetHead() const;
    void *GetTail() const;

    void *RemoveHead();
    void *RemoveTail();
    NODE *AddHead(void *newElement);
    NODE *AddTail(void *newElement);
    BOOL AddHead(CPList *pNewList);
    BOOL AddTail(CPList *pNewList);
    void RemoveAll();

    NODE *GetHeadPosition() const
    {
        return m_pNodeHead;
    }
    NODE *GetTailPosition() const
    {
        return m_pNodeTail;
    }
    void *GetNext(NODE *&rPosition) const;
    void *GetPrev(NODE *&rPosition) const;

    void *GetAt(NODE *position) const;
    BOOL SetAt(NODE *pos, void *newElement);
    BOOL RemoveAt(NODE *position);

    NODE *InsertBefore(NODE *position, void *newElement);
    NODE *InsertAfter(NODE *position, void *newElement);

    void Swap(NODE *pNode1, NODE *pNode2);

    NODE *Find(void *searchValue, NODE *startAfter = NULL) const;
    NODE *FindIndex(int nIndex) const;

    int GetCount() const
    {
        return m_nCount;
    }
    BOOL IsEmpty() const
    {
        return 0 == m_nCount;
    }

  protected:
    NODE *m_pNodeHead;
    NODE *m_pNodeTail;
    int m_nCount;

    NODE *NewNode(NODE *pPrev, NODE *pNext);
    void FreeNode(NODE *pNode);
};

#endif

class CSubject
{
  public:
    virtual ~CSubject();

    virtual void Attach(CObserver *pObserver);
    virtual void Detach(CObserver *pObserver);
    virtual void Notify();

  protected:
    CSubject();

  private:
    CPList m_ObserverList;
};

class SharedAllocationCounter final
{
  public:
    template <typename Vector>
    std::size_t CountVector(const std::shared_ptr<Vector> &allocation) noexcept
    {
        if (allocation == nullptr || !Remember(allocation.get()))
            return 0;
        using Value = typename std::remove_cv_t<Vector>::value_type;
        return sizeof(*allocation) + allocation->capacity() * sizeof(Value);
    }

  private:
    bool Remember(const void *allocation) noexcept
    {
        try
        {
            return allocations_.insert(allocation).second;
        }
        catch (...)
        {
            return false;
        }
    }

    std::unordered_set<const void *> allocations_;
};

#ifndef __SINGLETON_H__
#define __SINGLETON_H__

template <typename T> class Singleton
{
    static T *_Singleton;

  public:
    Singleton(void)
    {
        if (_Singleton == 0)
        {
            _Singleton = static_cast<T *>(this);
        }
    }

    virtual ~Singleton(void)
    { /*assert( _Singleton );*/
        _Singleton = 0;
    }

    static T &GetSingleton(void)
    { /*assert( _Singleton );*/
        return (*_Singleton);
    }
    static T *GetSingletonPtr(void)
    {
        return (_Singleton);
    }
    static bool IsInitialized(void)
    {
        return _Singleton ? true : false;
    }
};

template <typename T> T *Singleton<T>::_Singleton = 0;

#endif

class SpinLock
{
    std::atomic_flag locked = ATOMIC_FLAG_INIT;

  public:
    void lock()
    {
        while (locked.test_and_set(std::memory_order_acquire))
        {
        }
    }

    void unlock()
    {
        locked.clear(std::memory_order_release);
    }
};

namespace StringUtils
{
inline std::wstring NarrowToWide(const char *str)
{
    if (!str || *str == '\0')
        return L"";
    const int size = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
    if (size <= 1)
        return L"";
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str, -1, result.data(), size);
    result.pop_back();
    return result;
}

// Convert wide string (UTF-16) to narrow string (UTF-8)
inline std::string WideToNarrow(const wchar_t *wstr)
{
    if (!wstr)
        return "";
    size_t len = wcslen(wstr);
    if (len == 0)
        return "";

    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, (int)len, NULL, 0, NULL, NULL);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, (int)len, &result[0], size, NULL, NULL);
    return result;
}
} // namespace StringUtils

class CharacterId final
{
  public:
    using ValueType = std::uint64_t;

    static constexpr std::optional<CharacterId> TryCreate(ValueType value) noexcept
    {
        if (value == InvalidValue)
        {
            return std::nullopt;
        }
        return CharacterId(value);
    }

    constexpr ValueType RawValue() const noexcept
    {
        return value_;
    }

    constexpr auto operator<=>(const CharacterId &) const noexcept = default;

  private:
    static constexpr ValueType InvalidValue = 0;

    explicit constexpr CharacterId(ValueType value) noexcept : value_(value)
    {
    }

    ValueType value_;
};

class EntityId final
{
  public:
    using ValueType = std::uint64_t;

    static constexpr std::optional<EntityId> TryCreate(ValueType value) noexcept
    {
        if (value == InvalidValue)
        {
            return std::nullopt;
        }
        return EntityId(value);
    }

    constexpr ValueType RawValue() const noexcept
    {
        return value_;
    }

    constexpr auto operator<=>(const EntityId &) const noexcept = default;

  private:
    static constexpr ValueType InvalidValue = 0;

    explicit constexpr EntityId(ValueType value) noexcept : value_(value)
    {
    }

    ValueType value_;
};

class GameplayEffectId final
{
  public:
    using ValueType = std::uint64_t;

    static constexpr std::optional<GameplayEffectId> TryCreate(ValueType value) noexcept
    {
        if (value == InvalidValue)
        {
            return std::nullopt;
        }
        return GameplayEffectId(value);
    }

    constexpr ValueType RawValue() const noexcept
    {
        return value_;
    }

    constexpr auto operator<=>(const GameplayEffectId &) const noexcept = default;

  private:
    static constexpr ValueType InvalidValue = 0;

    explicit constexpr GameplayEffectId(ValueType value) noexcept : value_(value)
    {
    }

    ValueType value_;
};

class ItemId final
{
  public:
    using ValueType = std::uint64_t;

    static constexpr std::optional<ItemId> TryCreate(ValueType value) noexcept
    {
        if (value == InvalidValue)
        {
            return std::nullopt;
        }
        return ItemId(value);
    }

    constexpr ValueType RawValue() const noexcept
    {
        return value_;
    }

    constexpr auto operator<=>(const ItemId &) const noexcept = default;

  private:
    static constexpr ValueType InvalidValue = 0;

    explicit constexpr ItemId(ValueType value) noexcept : value_(value)
    {
    }

    ValueType value_;
};

class MapId final
{
  public:
    using ValueType = std::uint16_t;

    static constexpr std::optional<MapId> TryCreate(ValueType value) noexcept
    {
        if (value == InvalidValue)
        {
            return std::nullopt;
        }
        return MapId(value);
    }

    constexpr ValueType RawValue() const noexcept
    {
        return value_;
    }

    constexpr auto operator<=>(const MapId &) const noexcept = default;

  private:
    static constexpr ValueType InvalidValue = (std::numeric_limits<ValueType>::max)();

    explicit constexpr MapId(ValueType value) noexcept : value_(value)
    {
    }

    ValueType value_;
};

#define Smart_Ptr(classname) std::shared_ptr<classname>
#define Weak_Ptr(classname) std::weak_ptr<classname>

#define SmartPointer(classname)                                                                    \
    class classname;                                                                               \
    typedef Smart_Ptr(classname) classname##Ptr

#define PtrReset(p)                                                                                \
    {                                                                                              \
        if (p)                                                                                     \
        {                                                                                          \
            p.reset();                                                                             \
        }                                                                                          \
    }

#define SAFE_DELETE(p)                                                                             \
    {                                                                                              \
        if (p)                                                                                     \
        {                                                                                          \
            delete (p);                                                                            \
            (p) = NULL;                                                                            \
        }                                                                                          \
    }
#define SAFE_DELETE_ARRAY(p)                                                                       \
    {                                                                                              \
        if (p)                                                                                     \
        {                                                                                          \
            delete[] (p);                                                                          \
            (p) = NULL;                                                                            \
        }                                                                                          \
    }
#define SAFE_RELEASE(p)                                                                            \
    {                                                                                              \
        if (p)                                                                                     \
        {                                                                                          \
            (p)->Release();                                                                        \
            (p) = NULL;                                                                            \
        }                                                                                          \
    }
#define BYTECAST(T, X) static_cast<T>(X & 0xFF)

#define MAX_USERNAME_SIZE 10

#define MAX_PASSWORD_SIZE 20

#define MAX_TEXT_LENGTH 255

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define LIMIT(val, l, h) ((val) < (l) ? (l) : (val) > (h) ? (h) : (val))
#define SWAP(a, b)                                                                                 \
    {                                                                                              \
        (a) ^= (b) ^= (a) ^= (b);                                                                  \
    }
#define ARGB(a, r, g, b) (((DWORD)(a)) << 24 | (DWORD)(r) | ((DWORD)(g)) << 8 | ((DWORD)(b)) << 16)

namespace SEASON4A
{
class CSocketItemMgr;
}
class CSItemOption;

inline DWORD GenerateCheckSum2(const BYTE *pbyBuffer, DWORD dwSize, WORD wKey)
{
    DWORD dwKey = (DWORD)wKey;
    DWORD dwResult = dwKey << 9;
    for (DWORD dwChecked = 0; dwChecked <= dwSize - 4; dwChecked += 4)
    {
        DWORD dwTemp;
        memcpy(&dwTemp, pbyBuffer + dwChecked, sizeof(DWORD));

        switch ((dwChecked / 4 + wKey) % 2)
        {
        case 0:
            dwResult ^= dwTemp;
            break;
        case 1:
            dwResult += dwTemp;
            break;
        }
        if (0 == (dwChecked % 16))
        {
            dwResult ^= ((dwKey + dwResult) >> ((dwChecked / 4) % 8 + 1));
        }
    }

    return (dwResult);
}

// Session identities share the primitive identity boundary with map, item and character IDs.
class SessionId final
{
  public:
    using ValueType = std::uint64_t;

    static constexpr std::optional<SessionId> TryCreate(ValueType value) noexcept
    {
        if (value == InvalidValue)
        {
            return std::nullopt;
        }
        return SessionId(value);
    }

    constexpr ValueType RawValue() const noexcept
    {
        return value_;
    }

    constexpr auto operator<=>(const SessionId &) const noexcept = default;

  private:
    static constexpr ValueType InvalidValue = 0;

    explicit constexpr SessionId(ValueType value) noexcept : value_(value)
    {
    }

    ValueType value_;
};

class SessionSlotId final
{
  public:
    using ValueType = std::uint32_t;

    static std::optional<SessionSlotId> TryCreate(ValueType value) noexcept
    {
        return value == 0 ? std::nullopt : std::optional<SessionSlotId>(SessionSlotId(value));
    }

    ValueType RawValue() const noexcept
    {
        return value_;
    }

    friend bool operator==(SessionSlotId, SessionSlotId) = default;
    friend auto operator<=>(SessionSlotId, SessionSlotId) = default;

  private:
    explicit SessionSlotId(ValueType value) noexcept : value_(value)
    {
    }

    ValueType value_;
};

class SessionGeneration final
{
  public:
    using ValueType = std::uint64_t;

    static constexpr std::optional<SessionGeneration> TryCreate(ValueType value) noexcept
    {
        if (value == InvalidValue)
        {
            return std::nullopt;
        }
        return SessionGeneration(value);
    }

    constexpr ValueType RawValue() const noexcept
    {
        return value_;
    }

    constexpr auto operator<=>(const SessionGeneration &) const noexcept = default;

  private:
    static constexpr ValueType InvalidValue = 0;

    explicit constexpr SessionGeneration(ValueType value) noexcept : value_(value)
    {
    }

    ValueType value_;
};

namespace Core::Time
{
inline constexpr int DefaultReferenceFps = 25;
inline constexpr double ReferenceFrameMilliseconds = 1000.0 / DefaultReferenceFps;
} // namespace Core::Time

// The portable texture loaders use this name independently of Win32 GDI.
#ifdef LoadImage
#undef LoadImage
#endif

float absf(float a);

float minf(float a, float b);

float maxf(float a, float b);
