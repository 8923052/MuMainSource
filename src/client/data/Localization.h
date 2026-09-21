#pragma once
#include "support/CoreMath.h"

#include <cstdint>
#include <string>

#define MAX_TEXTS 3000
#define MAX_GLOBAL_TEXT_STRING 300
#define MAX_FILTERS 1000
#define MAX_NAMEFILTERS 500
#define MAX_LANGUAGE_NAME_LENGTH 4

class CMultiLanguage
{
  private:
    BYTE byLanguage;

    CMultiLanguage() = default;

  public:
    CMultiLanguage(std::wstring strSelectedML);
    ~CMultiLanguage() = default;

    BYTE GetLanguage(); // Getters

    WPARAM ConvertFulltoHalfWidthChar(DWORD wParam);

    static int32_t ConvertFromUtf8(wchar_t *target, const char *source, int maxSourceLength = -1);
    static int32_t ConvertToUtf8(char *target, const wchar_t *source, int maxSourceLength = -1);
};

namespace SEASON4A
{
class CSocketItemMgr;
}
class CSItemOption;

void SaveTextFile(wchar_t *FileName);
