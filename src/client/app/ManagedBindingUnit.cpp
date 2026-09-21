#include "app/ManagedBindingUnit.h"
#include "app/ApplicationKeeper.h"

#ifndef _WIN32
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace
{
#ifdef _WIN32
ManagedLibraryHandle LoadManagedLibrary()
{
    return LoadLibraryA(MU_MANAGED_LIBRARY_FILENAME);
}

template <typename Symbol> Symbol LoadManagedSymbol(ManagedLibraryHandle handle, const char *name)
{
    return reinterpret_cast<Symbol>(GetProcAddress(handle, name));
}
#else
ManagedLibraryHandle LoadManagedLibrary()
{
    char executable[4096];
    const ssize_t length = ::readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length > 0)
    {
        std::string path(executable, static_cast<std::size_t>(length));
        const auto slash = path.find_last_of('/');
        if (slash != std::string::npos)
        {
            path.resize(slash + 1);
            path += "MUnique.Client.Library.so";
            if (void *library = dlopen(path.c_str(), RTLD_LAZY))
            {
                return library;
            }
        }
    }
    return dlopen("MUnique.Client.Library.so", RTLD_LAZY);
}

template <typename Symbol> Symbol LoadManagedSymbol(ManagedLibraryHandle handle, const char *name)
{
    return reinterpret_cast<Symbol>(dlsym(handle, name));
}
#endif
} // namespace

ManagedBindingUnit::ManagedBindingUnit(ApplicationKeeper &keeper) noexcept
    : ApplicationLegacyCalls(keeper), storage_(keeper.ManagedBindingsRef())
{
    (void)applicationKeeper_.RegisterManagedBindingUnit(*this);
}

bool ManagedBindingUnit::EnsureLoaded()
{
    return IsManagedLibraryAvailable();
}

bool ManagedBindingUnit::IsManagedLibraryAvailable()
{
    std::lock_guard lock(storage_.initializationMutex);
    if (storage_.loadAttempted)
    {
        return storage_.loadSucceeded;
    }
    storage_.loadAttempted = true;

    storage_.libraryHandle = LoadManagedLibrary();
    const ManagedLibraryHandle library = storage_.libraryHandle;
    if (library == nullptr)
    {
        ReportDotNetError("MUnique.Client.Library missing");
        return false;
    }

    auto load = [this, library]<typename Symbol>(const char *name) -> Symbol {
        const Symbol symbol = LoadManagedSymbol<Symbol>(library, name);
        if (symbol == nullptr)
        {
            ReportDotNetError(name);
        }
        return symbol;
    };

    storage_.Core.dotnet_connect = load.template operator()<Connect>("ConnectionManager_Connect");
    storage_.Core.dotnet_disconnect =
        load.template operator()<Disconnect>("ConnectionManager_Disconnect");
    storage_.Core.dotnet_beginreceive =
        load.template operator()<BeginReceive>("ConnectionManager_BeginReceive");
    storage_.Core.dotnet_send = load.template operator()<Send>("ConnectionManager_Send");
    storage_.Core.dotnet_beginSendCapture =
        load.template operator()<BeginSendCapture>("ConnectionManager_BeginSendCapture");
    storage_.Core.dotnet_endSendCapture =
        load.template operator()<EndSendCapture>("ConnectionManager_EndSendCapture");

    bool loaded =
        storage_.Core.dotnet_connect != nullptr && storage_.Core.dotnet_disconnect != nullptr &&
        storage_.Core.dotnet_beginreceive != nullptr && storage_.Core.dotnet_send != nullptr &&
        storage_.Core.dotnet_beginSendCapture != nullptr &&
        storage_.Core.dotnet_endSendCapture != nullptr;
    loaded = LoadManagedBindings(storage_.ChatServer, load) && loaded;
    loaded = LoadManagedBindings(storage_.ConnectServer, load) && loaded;
    loaded = LoadManagedBindings(storage_.ClientToServer, load) && loaded;

    storage_.Custom.dotnet_SendLogin =
        load.template operator()<SendLogin>("ConnectionManager_SendLogin");
    storage_.Custom.dotnet_SendAuthenticateExt =
        load.template operator()<SendAuthenticateExt>("ConnectionManager_SendAuthenticateExt");
    storage_.Custom.dotnet_SendChatMessageExt =
        load.template operator()<SendChatMessageExt>("ConnectionManager_SendChatMessageExt");
    loaded = storage_.Custom.dotnet_SendLogin != nullptr &&
             storage_.Custom.dotnet_SendAuthenticateExt != nullptr &&
             storage_.Custom.dotnet_SendChatMessageExt != nullptr && loaded;

    storage_.loadSucceeded = loaded;
    return loaded;
}

bool ManagedBindingUnit::WasLoadAttempted() const noexcept
{
    std::lock_guard lock(storage_.initializationMutex);
    return storage_.loadAttempted;
}

bool ManagedBindingUnit::IsLoaded() const noexcept
{
    std::lock_guard lock(storage_.initializationMutex);
    return storage_.loadSucceeded;
}

ManagedBindingStorage &ManagedBindingUnit::Bindings() noexcept
{
    return storage_;
}

const ManagedBindingStorage &ManagedBindingUnit::Bindings() const noexcept
{
    return storage_;
}

void ManagedBindingUnit::ReportError(const char *detail)
{
    ReportDotNetError(detail);
}

void ManagedBindingUnit::ReportDotNetError(const char *detail)
{
    std::lock_guard lock(storage_.initializationMutex);
    ReportErrorLocked(detail);
}

void ManagedBindingUnit::ReportErrorLocked(const char *detail)
{
    if (storage_.g_dotnetErrorDisplayed)
    {
        return;
    }
    storage_.g_dotnetErrorDisplayed = true;

    wchar_t buffer[512];
    std::swprintf(
        buffer, std::size(buffer),
        L"Failed to initialize the managed client library (%hs). The game client cannot connect to the server.",
        detail != nullptr ? detail : "unknown error");
#ifdef _WIN32
    MessageBoxW(nullptr, buffer, L"MuMainClient", MB_ICONERROR | MB_OK);
#else
    std::wprintf(L"%ls\n", buffer);
#endif
}

void ApplicationLegacyCalls::ReportDotNetError(const char *detail)
{
    applicationKeeper_.ManagedBindingUnitShortcut()->ReportDotNetError(detail);
}
bool ApplicationLegacyCalls::IsManagedLibraryAvailable()
{
    return applicationKeeper_.ManagedBindingUnitShortcut()->IsManagedLibraryAvailable();
}
