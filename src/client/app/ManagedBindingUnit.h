#pragma once
#include "support/CoreMath.h"

#include "app/Application.h"

#include "network/generated/PacketBindings_ChatServer.h"
#include "network/generated/PacketBindings_ClientToServer.h"
#include "network/generated/PacketBindings_ConnectServer.h"

#include <coreclr_delegates.h>

#include <cstdint>
#include <mutex>

#ifdef _WIN32
using ManagedLibraryHandle = HINSTANCE;
#else
using ManagedLibraryHandle = void *;
#endif

using ManagedPacketReceivedCallback = void(CORECLR_DELEGATE_CALLTYPE *)(std::uintptr_t,
                                                                        std::int32_t, std::uint64_t,
                                                                        std::int32_t, BYTE *);
using ManagedDisconnectedCallback = void(CORECLR_DELEGATE_CALLTYPE *)(std::uintptr_t, std::int32_t,
                                                                      std::uint64_t);

using Connect = std::int32_t(CORECLR_DELEGATE_CALLTYPE *)(const wchar_t *, std::int32_t, BYTE,
                                                          std::uintptr_t,
                                                          ManagedPacketReceivedCallback,
                                                          ManagedDisconnectedCallback);
using Disconnect = void(CORECLR_DELEGATE_CALLTYPE *)(std::int32_t);
using BeginReceive = void(CORECLR_DELEGATE_CALLTYPE *)(std::int32_t, std::uint64_t);
using Send = void(CORECLR_DELEGATE_CALLTYPE *)(std::int32_t, const BYTE *, std::int32_t);
using ManagedSendCapturedCallback = BYTE(CORECLR_DELEGATE_CALLTYPE *)(std::uintptr_t, std::int32_t,
                                                                      std::int32_t, BYTE *);
using BeginSendCapture = BYTE(CORECLR_DELEGATE_CALLTYPE *)(std::int32_t, std::uintptr_t,
                                                           ManagedSendCapturedCallback);
using EndSendCapture = BYTE(CORECLR_DELEGATE_CALLTYPE *)(std::int32_t);

using SendLogin = void(CORECLR_DELEGATE_CALLTYPE *)(std::int32_t, const wchar_t *, const wchar_t *,
                                                    std::uint32_t, const BYTE *, const BYTE *);
using SendAuthenticateExt = void(CORECLR_DELEGATE_CALLTYPE *)(std::int32_t, std::uint16_t,
                                                              std::uint32_t);
using SendChatMessageExt = void(CORECLR_DELEGATE_CALLTYPE *)(std::int32_t, BYTE, const wchar_t *);

struct ManagedCoreBindings
{
    Connect dotnet_connect = nullptr;
    Disconnect dotnet_disconnect = nullptr;
    BeginReceive dotnet_beginreceive = nullptr;
    Send dotnet_send = nullptr;
    BeginSendCapture dotnet_beginSendCapture = nullptr;
    EndSendCapture dotnet_endSendCapture = nullptr;
};

struct ManagedCustomBindings
{
    SendLogin dotnet_SendLogin = nullptr;
    SendAuthenticateExt dotnet_SendAuthenticateExt = nullptr;
    SendChatMessageExt dotnet_SendChatMessageExt = nullptr;
};

struct ManagedBindingStorage
{
    std::recursive_mutex initializationMutex;
    ManagedLibraryHandle libraryHandle = nullptr;
    bool loadAttempted = false;
    bool loadSucceeded = false;
    bool g_dotnetErrorDisplayed = false;
    ManagedCoreBindings Core;
    ManagedBindings_ChatServer ChatServer;
    ManagedBindings_ConnectServer ConnectServer;
    ManagedBindings_ClientToServer ClientToServer;
    ManagedCustomBindings Custom;
};

class ManagedBindingUnit final : protected ApplicationLegacyCalls
{
  public:
    explicit ManagedBindingUnit(ApplicationKeeper &keeper) noexcept;

    ManagedBindingUnit(const ManagedBindingUnit &) = delete;
    ManagedBindingUnit &operator=(const ManagedBindingUnit &) = delete;
    ManagedBindingUnit(ManagedBindingUnit &&) = delete;
    ManagedBindingUnit &operator=(ManagedBindingUnit &&) = delete;

    bool EnsureLoaded();
    bool WasLoadAttempted() const noexcept;
    bool IsLoaded() const noexcept;
    ManagedBindingStorage &Bindings() noexcept;
    const ManagedBindingStorage &Bindings() const noexcept;
    void ReportError(const char *detail);

  private:
    friend class ApplicationLegacyCalls;
    friend class ApplicationKeeperTestPeer;

    bool IsManagedLibraryAvailable();
    void ReportDotNetError(const char *detail);
    void ReportErrorLocked(const char *detail);

    ManagedBindingStorage &storage_;
};
