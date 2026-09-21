// Linux entry point (issue #442, the per-platform entry seam from #441).
// The game bootstrap lives in Winmain.cpp; off Windows its WinMain is a plain
// function (no Win32 entry contract), so this just forwards into it. The
// HINSTANCE/command-line parameters are Win32 artifacts the portable code path
// does not read.
#include "app/Application.h"
#include "support/CoreMath.h"

#include <cstdio>
#include <exception>

#ifndef _WIN32
int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR szCmdLine, int nCmdShow);

int main(int /*argc*/, char * /*argv*/[])
{
    return WinMain(nullptr, nullptr, nullptr, SW_SHOW);
}
#endif

#ifdef _WIN32
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, PSTR, int)
#else
int WinMain(HINSTANCE hInstance, HINSTANCE, PSTR, int)
#endif
{
    try
    {
        return RunApplication(hInstance);
    }
    catch (const std::exception &error)
    {
#ifdef _WIN32
        MessageBoxA(nullptr, error.what(), "MuTwo startup", MB_OK | MB_ICONERROR);
#else
        std::fprintf(stderr, "%s\n", error.what());
#endif
        return 1;
    }
}
