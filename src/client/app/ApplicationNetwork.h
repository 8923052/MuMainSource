#pragma once
#include "support/CoreMath.h"
#include <coreclr_delegates.h>

#include "app/Application.h"
#include "session/SessionRuntime.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <span>
#include <string>
#include <string_view>

class PacketFunctions_ChatServer;
class PacketFunctions_ConnectServer;
class PacketFunctions_ClientToServer;

#define SLM_MAX_SERVER_NAME_LENGTH 32
#define SLM_MAX_SERVER_COUNT 15

enum class ConnectionRole
{
    ConnectServer,
    GameServer,
    ChatServer,
    MapServer,
};

struct ConnectionRouteSnapshot
{
    ConnectionRouteSnapshot(std::int32_t handle, SessionId session, ConnectionRole connectionRole,
                            std::uint64_t routeGeneration) noexcept
        : connectionHandle(handle), sessionId(session), role(connectionRole),
          generation(routeGeneration)
    {
    }

    std::int32_t connectionHandle;
    SessionId sessionId;
    ConnectionRole role;
    std::uint64_t generation;
};

inline bool operator==(const ConnectionRouteSnapshot &left,
                       const ConnectionRouteSnapshot &right) noexcept
{
    return left.connectionHandle == right.connectionHandle && left.sessionId == right.sessionId &&
           left.role == right.role && left.generation == right.generation;
}

struct PacketInfo
{
    PacketInfo(std::unique_ptr<BYTE[]> bytes, std::int32_t size,
               ConnectionRouteSnapshot route) noexcept
        : ReceiveBuffer(std::move(bytes)), Size(size), Route(route)
    {
    }

    std::unique_ptr<BYTE[]> ReceiveBuffer;
    std::int32_t Size;
    ConnectionRouteSnapshot Route;
};

struct ApplicationNetworkStorage
{
    using PacketQueue = std::queue<std::unique_ptr<PacketInfo>>;

    std::mutex mutex;
    std::map<std::int32_t, ConnectionRouteSnapshot> routes;
    std::map<std::int32_t, std::uint64_t> lastGenerations;
    PacketQueue inboundPackets;
    bool acceptingRoutes = true;
};

class Connection;
class SessionManager;

class ApplicationNetwork final : protected ApplicationLegacyCalls
{
  public:
    explicit ApplicationNetwork(ApplicationKeeper &keeper) noexcept;
    ~ApplicationNetwork();

    ApplicationNetwork(const ApplicationNetwork &) = delete;
    ApplicationNetwork &operator=(const ApplicationNetwork &) = delete;
    ApplicationNetwork(ApplicationNetwork &&) = delete;
    ApplicationNetwork &operator=(ApplicationNetwork &&) = delete;

    std::optional<ConnectionRouteSnapshot> RegisterAndBeginReceive(Connection &connection,
                                                                   SessionManager &sessions,
                                                                   SessionId sessionId,
                                                                   ConnectionRole role) noexcept;
    bool UnregisterRoute(const ConnectionRouteSnapshot &route) noexcept;
    std::size_t RemoveSession(SessionId sessionId) noexcept;

    bool QueuePacket(std::int32_t connectionHandle, std::uint64_t generation, const BYTE *bytes,
                     std::int32_t size) noexcept;
    void NotifyDisconnected(std::int32_t connectionHandle, std::uint64_t generation) noexcept;
    std::size_t DispatchPackets(SessionManager &sessions);

    bool IsRouteActive(const ConnectionRouteSnapshot &route) const noexcept;
    std::size_t RouteCount() const noexcept;
    std::size_t PendingPacketCount() const noexcept;
    void BeginShutdown() noexcept;

  private:
    friend class ApplicationKeeperTestPeer;

    std::optional<ConnectionRouteSnapshot> RegisterRoute(std::int32_t connectionHandle,
                                                         SessionManager &sessions,
                                                         SessionId sessionId,
                                                         ConnectionRole role) noexcept;
    ApplicationNetworkStorage::PacketQueue TakeAllPackets() noexcept;
    void RemoveQueuedRouteLocked(const ConnectionRouteSnapshot &route) noexcept;

    ApplicationNetworkStorage &storage_;
};

inline static const BYTE bBuxCode[3] = {0xFC, 0xCF, 0xAB};

inline static void BuxConvert(BYTE *Buffer, int Size)
{
    for (int i = 0; i < Size; ++i)
        Buffer[i] ^= bBuxCode[i % 3];
}
// Portable WinINet type shim (issue #462, Phase 3).
// The in-game-shop headers reference a few WinINet types/constants in their
// declarations (a handle, an FTP port, URL/credential buffer sizes). On Windows
// these come from <wininet.h>; elsewhere this provides just those declarations so
// the headers parse. The shop's actual WinINet/urlmon downloader lives in the

#ifdef _WIN32
#include <wininet.h>

#else // ---- non-Windows ----------------------------------------------------

typedef void *HINTERNET;
typedef WORD INTERNET_PORT;

#ifndef INTERNET_MAX_URL_LENGTH
#define INTERNET_MAX_URL_LENGTH 2084
#endif
#ifndef INTERNET_MAX_USER_NAME_LENGTH
#define INTERNET_MAX_USER_NAME_LENGTH 128
#endif
#ifndef INTERNET_MAX_PASSWORD_LENGTH
#define INTERNET_MAX_PASSWORD_LENGTH 128
#endif
#ifndef INTERNET_DEFAULT_FTP_PORT
#define INTERNET_DEFAULT_FTP_PORT 21
#endif

#endif // _WIN32
// Portable Winsock shim (issue #462, Phase 3).
// The reconnect reachability probe is the only Winsock user. Most of what it
// needs (socket/connect/getsockopt/select/sockaddr_in/AF_INET/...) is standard
// on POSIX, so on Windows this includes <ws2tcpip.h> and elsewhere it provides
// just the Winsock-specific names mapped onto BSD sockets.

#ifdef _WIN32

#include <ws2tcpip.h>

#else // ---- non-Windows ----------------------------------------------------

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>  // htons
#include <netdb.h>      // getaddrinfo / addrinfo
#include <unistd.h>     // close
#include <sys/ioctl.h>  // ioctl, FIONBIO
#include <sys/select.h> // select, fd_set
#include <cerrno>
#include <cwchar> // wcstombs

typedef int SOCKET;

#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif
#ifndef WSAEWOULDBLOCK
#define WSAEWOULDBLOCK EWOULDBLOCK
#endif

// Winsock startup is a no-op on BSD sockets.
typedef struct
{
    WORD wVersion;
} WSADATA, *LPWSADATA;
inline int WSAStartup(WORD, LPWSADATA)
{
    return 0;
}
inline int WSACleanup()
{
    return 0;
}
inline int WSAGetLastError()
{
    return errno;
}

inline int closesocket(SOCKET s)
{
    return ::close(s);
}

inline int ioctlsocket(SOCKET s, long cmd, u_long *argp)
{
    // Linux ioctl(FIONBIO) expects an int*, not Winsock's u_long*; convert so
    // the flag is read correctly (size/endianness safe).
    if (cmd == FIONBIO)
    {
        int val = argp ? static_cast<int>(*argp) : 0;
        return ::ioctl(s, FIONBIO, &val);
    }
    return ::ioctl(s, static_cast<unsigned long>(cmd), argp);
}

// Wide name resolution. The probe only reads ai_addr/ai_family, so map the wide
// addrinfo onto POSIX addrinfo and convert the node/service to the locale bytes.
typedef struct addrinfo addrinfoW, ADDRINFOW, *PADDRINFOW;

inline int GetAddrInfoW(const wchar_t *node, const wchar_t *service, const addrinfoW *hints,
                        addrinfoW **result)
{
    char n[256] = {0};
    char sv[64] = {0};
    if (node && wcstombs(n, node, sizeof(n) - 1) == static_cast<size_t>(-1))
        return EAI_FAIL;
    if (service && wcstombs(sv, service, sizeof(sv) - 1) == static_cast<size_t>(-1))
        return EAI_FAIL;
    return ::getaddrinfo(node ? n : nullptr, service ? sv : nullptr, hints, result);
}
inline void FreeAddrInfoW(addrinfo *p)
{
    ::freeaddrinfo(p);
}

#endif // _WIN32

CONST int MAX_KEY_GENERATOR_FILTER = 10;

class CKeyGenerator
{
  public:
    constexpr CKeyGenerator() noexcept = default;
    DWORD GenerateKeyValue(DWORD dwKeyValue) const;
    bool CheckKeyValue(DWORD *dwOldKeyValue, DWORD dwReceiveKeyValue) const;
};

inline constexpr CKeyGenerator g_KeyGenerator;

class ApplicationNetwork;
class ManagedBindingUnit;
class SessionOrderedEffectBatch;

class Connection : protected ApplicationLegacyCalls
{
  public:
    Connection(ApplicationKeeper &keeper, ManagedBindingUnit &bindings, ApplicationNetwork &network,
               const wchar_t *host, std::int32_t port, bool isEncrypted);
    ~Connection();

    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;
    Connection(Connection &&) = delete;
    Connection &operator=(Connection &&) = delete;

    bool IsConnected() const noexcept;
    bool HasActiveRoute() const noexcept;
    std::wstring_view Host() const noexcept
    {
        return host_;
    }
    std::int32_t Port() const noexcept
    {
        return port_;
    }
    void Send(const BYTE *data, std::int32_t length);
    bool BeginOrderedSendCapture(SessionOrderedEffectBatch &effects,
                                 std::uint64_t &nextStableSequence) noexcept;
    bool EndOrderedSendCapture() noexcept;
    bool CanCommitCapturedSend(SessionId sessionId, std::uint32_t channel) const noexcept;
    void CommitCapturedSend(std::span<const std::byte> payload) noexcept;
    void Close() noexcept;

    std::int32_t GetHandle() const noexcept;
    PacketFunctions_ChatServer *ToChatServer() const noexcept;
    PacketFunctions_ConnectServer *ToConnectServer() const noexcept;
    PacketFunctions_ClientToServer *ToGameServer() const noexcept;

  private:
    friend class ApplicationNetwork;

    static void CORECLR_DELEGATE_CALLTYPE OnPacketReceived(std::uintptr_t context,
                                                           std::int32_t handle,
                                                           std::uint64_t generation,
                                                           std::int32_t size, BYTE *data);
    static void CORECLR_DELEGATE_CALLTYPE OnDisconnected(std::uintptr_t context,
                                                         std::int32_t handle,
                                                         std::uint64_t generation);
    static BYTE CORECLR_DELEGATE_CALLTYPE OnSendCaptured(std::uintptr_t context,
                                                         std::int32_t handle, std::int32_t size,
                                                         BYTE *data);

    bool BeginReceive(std::uint64_t generation) noexcept;
    bool AttachRoute(const ConnectionRouteSnapshot &route) noexcept;
    void DetachRoute(const ConnectionRouteSnapshot &route) noexcept;

    ManagedBindingUnit &bindings_;
    ApplicationNetwork &network_;
    const std::wstring host_;
    const std::int32_t port_;
    std::unique_ptr<PacketFunctions_ChatServer> chatServer_;
    std::unique_ptr<PacketFunctions_ConnectServer> connectServer_;
    std::unique_ptr<PacketFunctions_ClientToServer> gameServer_;
    std::int32_t handle_ = 0;
    std::atomic_bool disconnected_ = false;
    std::optional<ConnectionRouteSnapshot> route_;
    SessionOrderedEffectBatch *sendCaptureEffects_ = nullptr;
    std::uint64_t *sendCaptureNextStableSequence_ = nullptr;
};
// <copyright file="PacketFunctions.h" company="MUnique">
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
// </copyright>

// <auto-generated>
//     This source code was auto-generated by an XSL transformation.
//     Do not change this file. Instead, change the XML data which contains
//     the packet definitions and re-run the transformation (publish/rebuild the
//     managed library project).
// </auto-generated>

struct ManagedBindingStorage;

/// <summary>
/// Extension methods to start writing messages of this namespace on a <see cref="Connection"/>.
/// </summary>
class PacketFunctions_Base
{
  private:
    int32_t _handle = -1;
    ManagedBindingStorage &bindings_;

  public:
    explicit PacketFunctions_Base(ManagedBindingStorage &bindings) noexcept;

    void SetHandle(const int32_t handle)
    {
        _handle = handle;
    }
    int32_t GetHandle() const
    {
        return _handle;
    }
    ManagedBindingStorage &Bindings() noexcept
    {
        return bindings_;
    }
    const ManagedBindingStorage &Bindings() const noexcept
    {
        return bindings_;
    }
};

#pragma pack(push, 1)
struct AreaSkillHitTarget
{
    uint16_t TargetId;
    BYTE AnimationCounter;
};
#pragma pack(pop)

/// <summary>
/// Extension methods to start writing messages of this namespace on a <see cref="Connection"/>.
/// </summary>
class PacketFunctions_ClientToServer_Custom : public PacketFunctions_Base
{
  public:
    using PacketFunctions_Base::PacketFunctions_Base;

    /// <summary>
    /// Sends a LoginLongPassword to this connection.
    /// </summary>
    /// <param name="username">The user name, "encrypted" with Xor3.</param>
    /// <param name="password">The password, "encrypted" with Xor3.</param>
    /// <param name="clientVersion">The client version.</param>
    /// <param name="clientSerial">The client serial.</param>
    /// <remarks>
    /// Is sent by the client when: The player tries to log into the game.
    /// Causes reaction on server side: The server is authenticating the sent login name and password. If it's correct, the state of the player is proceeding to be logged in.
    /// </remarks>
    void SendLogin(const wchar_t *username, const wchar_t *password, const BYTE *clientVersion,
                   const BYTE *clientSerial);
};

/// <summary>
/// Extension methods to start writing messages of this namespace on a <see cref="Connection"/>.
/// </summary>
class PacketFunctions_ConnectServer_Custom : public PacketFunctions_Base
{
  public:
    using PacketFunctions_Base::PacketFunctions_Base;
};

/// <summary>
/// Extension methods to start writing messages of this namespace on a <see cref="Connection"/>.
/// </summary>
class PacketFunctions_ChatServer_Custom : public PacketFunctions_Base
{
  public:
    using PacketFunctions_Base::PacketFunctions_Base;

    /// <summary>
    /// Sends a Authenticate to this connection.
    /// </summary>
    /// <param name="roomId">The room id.</param>
    /// <param name="token">The token to authenticate the client.</param>
    /// <remarks>
    /// Is sent by the client when: This packet is sent by the client after it connected to the server, to authenticate itself.
    /// Causes reaction on server side: The server will check the token. If it's correct, the client gets added to the requested chat room.
    /// </remarks>
    void SendAuthenticateExt(uint16_t roomId, uint32_t token);

    /// <summary>
    /// Sends a ChatMessage to this connection.
    /// </summary>
    /// <param name="senderIndex">The sender index.</param>
    /// <param name="message">The message.</param>
    /// <remarks>
    /// Is sent by the server when: This packet is sent by the server after another chat client sent a message to the current chat room.
    /// Causes reaction on client side: The client will show the message.
    /// </remarks>
    void SendChatMessageExt(BYTE senderIndex, const wchar_t *message);
};

class CServerInfo
{
  public:
    CServerInfo();
    virtual ~CServerInfo();

    int m_iSequence;
    int m_iIndex;
    int m_iConnectIndex;
    int m_iPercent;
    BYTE m_byNonPvP;
    wchar_t m_bName[MAX_TEXT_LENGTH];
};

typedef std::list<CServerInfo *> type_listServer;

class CServerGroup
{
  public:
    enum SERVER_BTN_POSITION
    {
        SBP_LEFT = 0,
        SBP_RIGHT = 1,
        SBP_CENTER = 2,
    };

  public:
    CServerGroup();
    virtual ~CServerGroup();

  public:
    void Release();

  public:
    int m_iSequence;
    int m_iWidthPos;
    int m_iBtnPos;
    int m_iServerIndex;
    int m_iNumServer;
    bool m_bPvPServer;
    BYTE m_abyNonPvpServer[MAX_SERVER_PER_GROUP];
    wchar_t m_szName[MAX_TEXT_LENGTH];
    wchar_t m_szDescription[MAX_TEXT_LENGTH];

    type_listServer m_listServerInfo;

  protected:
    type_listServer::iterator m_iterServerList;

  public:
    void InsertServerInfo(CServerInfo *pServerInfo);

    void SetFirst();
    bool GetNext(OUT CServerInfo *&pServerInfo);

    int GetServerSize();
    CServerInfo *GetServerInfo(int iSequence);
};

struct SServerGroupInfo
{
    wchar_t m_szName[SLM_MAX_SERVER_NAME_LENGTH + 1];
    BYTE m_byPos;
    BYTE m_bySequence;
    BYTE m_abyNonPVP[SLM_MAX_SERVER_COUNT];
    std::wstring m_strDescript;
};

typedef std::map<WORD, SServerGroupInfo> ServerListScriptMap;
typedef std::map<int, CServerGroup *> type_mapServerGroup;

class CErrorReport;
class SessionKeeper;

class CServerListManager
{
  public:
    explicit CServerListManager(SessionKeeper &keeper) noexcept;
    virtual ~CServerListManager();

    void InsertServerGroup(int iConnectIndex, int iServerPercent);
    void Release();
    bool LoadServerListScript();
    void SetFirst();
    bool GetNext(OUT CServerGroup *&pServerGroup);
    CServerGroup *GetServerGroupByBtnPos(int iBtnPos);

    int GetServerGroupSize();

    void SetSelectServerInfo(wchar_t *pszName, int iIndex, BYTE byNonPvP);
    wchar_t *GetSelectServerName();
    int GetSelectServerIndex();
    BYTE GetNonPVPInfo();
    bool IsNonPvP();
    void SetTotalServer(int iTotalServer);
    int GetTotalServer();
    bool IsStrifeMap(int nMapIndex);

  protected:
    const SServerGroupInfo *GetServerGroupInfoInScript(WORD wServerGroupIndex);
    bool MakeServerGroup(IN int iServerGroupIndex, OUT CServerGroup *pServerGroup);
    void InsertServer(CServerGroup *pServerGroup, int iConnectIndex, int iServerPercent);

  public:
    type_mapServerGroup m_mapServerGroup;
    type_mapServerGroup::iterator m_iterServerGroup;

    int m_iTotalServer;
    wchar_t m_szSelectServerName[MAX_TEXT_LENGTH];
    int m_iSelectServerIndex;
    BYTE m_byNonPvP;

  protected:
    CErrorReport &g_ErrorReport;
    ServerListScriptMap m_mapServerListScript;
    bool scriptLoaded_ = false;
};
