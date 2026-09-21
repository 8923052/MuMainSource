#include "app/ApplicationNetwork.h"
#include "app/ApplicationKeeper.h"
#include "app/ManagedBindingUnit.h"
#include "I18N/All.h"
#include "network/generated/PacketFunctions_ChatServer.h"
#include "network/generated/PacketFunctions_ClientToServer.h"
#include "network/generated/PacketFunctions_ConnectServer.h"
#include "session/GameSession.h"
#include "session/SessionKeeper.h"
#include "session/SessionRuntime.h"
#include "support/CoreMath.h"

ApplicationNetwork::ApplicationNetwork(ApplicationKeeper &keeper) noexcept
    : ApplicationLegacyCalls(keeper), storage_(keeper.NetworkStorageRef())
{
    (void)applicationKeeper_.RegisterApplicationNetwork(*this);
}

ApplicationNetwork::~ApplicationNetwork()
{
    BeginShutdown();
}

std::optional<ConnectionRouteSnapshot> ApplicationNetwork::RegisterAndBeginReceive(
    Connection &connection, SessionManager &sessions, SessionId sessionId,
    ConnectionRole role) noexcept
{
    const auto route = RegisterRoute(connection.GetHandle(), sessions, sessionId, role);
    if (!route.has_value())
    {
        return std::nullopt;
    }

    if (!connection.AttachRoute(*route))
    {
        (void)UnregisterRoute(*route);
        return std::nullopt;
    }
    if (connection.BeginReceive(route->generation))
    {
        return route;
    }

    connection.DetachRoute(*route);
    (void)UnregisterRoute(*route);
    return std::nullopt;
}

std::optional<ConnectionRouteSnapshot> ApplicationNetwork::RegisterRoute(
    std::int32_t connectionHandle, SessionManager &sessions, SessionId sessionId,
    ConnectionRole role) noexcept
{
    GameSession *session = sessions.Find(sessionId);
    if (!applicationKeeper_.IsReady() || connectionHandle <= 0 || session == nullptr ||
        !session->IsUsable())
    {
        return std::nullopt;
    }

    std::lock_guard lock(storage_.mutex);
    if (!storage_.acceptingRoutes || storage_.routes.contains(connectionHandle))
    {
        return std::nullopt;
    }

    std::uint64_t &lastGeneration = storage_.lastGenerations[connectionHandle];
    ++lastGeneration;
    if (lastGeneration == 0)
    {
        ++lastGeneration;
    }

    ConnectionRouteSnapshot route{
        connectionHandle,
        sessionId,
        role,
        lastGeneration,
    };
    storage_.routes.emplace(connectionHandle, route);
    return route;
}

bool ApplicationNetwork::UnregisterRoute(const ConnectionRouteSnapshot &route) noexcept
{
    std::lock_guard lock(storage_.mutex);
    const auto found = storage_.routes.find(route.connectionHandle);
    if (found == storage_.routes.end() || !(found->second == route))
    {
        return false;
    }

    storage_.routes.erase(found);
    RemoveQueuedRouteLocked(route);
    return true;
}

std::size_t ApplicationNetwork::RemoveSession(SessionId sessionId) noexcept
{
    std::lock_guard lock(storage_.mutex);
    std::size_t removed = 0;
    for (auto route = storage_.routes.begin(); route != storage_.routes.end();)
    {
        if (route->second.sessionId != sessionId)
        {
            ++route;
            continue;
        }

        const ConnectionRouteSnapshot removedRoute = route->second;
        route = storage_.routes.erase(route);
        RemoveQueuedRouteLocked(removedRoute);
        ++removed;
    }
    return removed;
}

bool ApplicationNetwork::QueuePacket(std::int32_t connectionHandle, std::uint64_t generation,
                                     const BYTE *bytes, std::int32_t size) noexcept
{
    if (bytes == nullptr || size <= 0)
    {
        return false;
    }

    std::optional<ConnectionRouteSnapshot> route;
    {
        std::lock_guard lock(storage_.mutex);
        const auto found = storage_.routes.find(connectionHandle);
        if (!storage_.acceptingRoutes || found == storage_.routes.end() ||
            found->second.generation != generation)
        {
            return false;
        }
        route = found->second;
    }

    std::unique_ptr<PacketInfo> packet;
    try
    {
        auto receiveBuffer = std::make_unique<BYTE[]>(size);
        std::copy(bytes, bytes + size, receiveBuffer.get());
        packet = std::make_unique<PacketInfo>(std::move(receiveBuffer), size, *route);
    }
    catch (...)
    {
        return false;
    }

    std::lock_guard lock(storage_.mutex);
    const auto current = storage_.routes.find(connectionHandle);
    if (!storage_.acceptingRoutes || current == storage_.routes.end() ||
        !(current->second == *route))
    {
        return false;
    }
    storage_.inboundPackets.push(std::move(packet));
    return true;
}

void ApplicationNetwork::NotifyDisconnected(std::int32_t connectionHandle,
                                            std::uint64_t generation) noexcept
{
    std::lock_guard lock(storage_.mutex);
    const auto found = storage_.routes.find(connectionHandle);
    if (found == storage_.routes.end() || found->second.generation != generation)
    {
        return;
    }

    const ConnectionRouteSnapshot route = found->second;
    storage_.routes.erase(found);
    RemoveQueuedRouteLocked(route);
}

std::size_t ApplicationNetwork::DispatchPackets(SessionManager &sessions)
{
    auto packets = TakeAllPackets();
    std::size_t dispatched = 0;
    while (!packets.empty())
    {
        const std::unique_ptr<PacketInfo> &packet = packets.front();
        const ConnectionRouteSnapshot route = packet->Route;
        GameSession *session = sessions.Find(route.sessionId);
        if (session != nullptr && session->IsUsable() && IsRouteActive(route))
        {
            const GameplayExternalEventBatch events = session->ProcessPacket(*packet);
            if (IsRouteActive(route))
            {
                for (const GameplayExternalEvent &event : events)
                {
                    (void)session->ApplyGameplayExternalEvent(event);
                }
                ++dispatched;
            }
        }
        packets.pop();
    }
    return dispatched;
}

bool ApplicationNetwork::IsRouteActive(const ConnectionRouteSnapshot &route) const noexcept
{
    std::lock_guard lock(storage_.mutex);
    const auto found = storage_.routes.find(route.connectionHandle);
    return storage_.acceptingRoutes && found != storage_.routes.end() && found->second == route;
}

std::size_t ApplicationNetwork::RouteCount() const noexcept
{
    std::lock_guard lock(storage_.mutex);
    return storage_.routes.size();
}

std::size_t ApplicationNetwork::PendingPacketCount() const noexcept
{
    std::lock_guard lock(storage_.mutex);
    return storage_.inboundPackets.size();
}

void ApplicationNetwork::BeginShutdown() noexcept
{
    ApplicationNetworkStorage::PacketQueue discarded;
    {
        std::lock_guard lock(storage_.mutex);
        storage_.acceptingRoutes = false;
        storage_.routes.clear();
        std::swap(discarded, storage_.inboundPackets);
    }
}

ApplicationNetworkStorage::PacketQueue ApplicationNetwork::TakeAllPackets() noexcept
{
    ApplicationNetworkStorage::PacketQueue packets;
    std::lock_guard lock(storage_.mutex);
    std::swap(packets, storage_.inboundPackets);
    return packets;
}

void ApplicationNetwork::RemoveQueuedRouteLocked(const ConnectionRouteSnapshot &route) noexcept
{
    ApplicationNetworkStorage::PacketQueue retained;
    while (!storage_.inboundPackets.empty())
    {
        std::unique_ptr<PacketInfo> packet = std::move(storage_.inboundPackets.front());
        storage_.inboundPackets.pop();
        if (!(packet->Route == route))
        {
            retained.push(std::move(packet));
        }
    }
    std::swap(retained, storage_.inboundPackets);
}

static constexpr int KEY_GENERATE_FILTER[MAX_KEY_GENERATOR_FILTER][4] = {
    {321, 37531879, 8734, 32}, // 0
    {873, 64374332, 3546, 87}, {537, 24798765, 5798, 32}, {654, 32498765, 3573, 73},
    {546, 98465432, 6459, 12}, // 4
    {987, 24654876, 5616, 54}, {357, 34599876, 8764, 98}, {665, 78641332, 6547, 54},
    {813, 85132165, 8421, 98}, {454, 57684216, 6875, 45}};

DWORD CKeyGenerator::GenerateKeyValue(DWORD dwKeyValue) const
{
    DWORD dwRegenerateKeyValue = 0;
    BYTE btNumericValue = 0;

    btNumericValue = dwKeyValue % MAX_KEY_GENERATOR_FILTER;

    dwRegenerateKeyValue =
        dwKeyValue * KEY_GENERATE_FILTER[btNumericValue][0] +
        KEY_GENERATE_FILTER[btNumericValue][1] -
        KEY_GENERATE_FILTER[btNumericValue][2] / KEY_GENERATE_FILTER[btNumericValue][3];
    return dwRegenerateKeyValue;
}

bool CKeyGenerator::CheckKeyValue(DWORD *dwOldKeyValue, DWORD dwReceiveKeyValue) const
{
    DWORD dwGeneratedKeyValue = 0;

    dwGeneratedKeyValue = GenerateKeyValue(*dwOldKeyValue);
    if (dwReceiveKeyValue == dwGeneratedKeyValue)
    {
        *dwOldKeyValue = dwGeneratedKeyValue;
        return true;
    }

    return false;
}

Connection::Connection(ApplicationKeeper &keeper, ManagedBindingUnit &bindings,
                       ApplicationNetwork &network, const wchar_t *host, std::int32_t port,
                       bool isEncrypted)
    : ApplicationLegacyCalls(keeper), bindings_(bindings), network_(network),
      host_(host != nullptr ? host : L""), port_(port)
{
    if (!IsManagedLibraryAvailable())
    {
        return;
    }

    ManagedCoreBindings &core = bindings_.Bindings().Core;
    handle_ =
        core.dotnet_connect(host, port, isEncrypted ? 1 : 0, reinterpret_cast<std::uintptr_t>(this),
                            &Connection::OnPacketReceived, &Connection::OnDisconnected);
    if (handle_ <= 0)
    {
        ReportDotNetError("ConnectionManager_Connect");
        handle_ = 0;
        return;
    }

    ManagedBindingStorage &bindingStorage = bindings_.Bindings();
    chatServer_ = std::make_unique<PacketFunctions_ChatServer>(bindingStorage);
    connectServer_ = std::make_unique<PacketFunctions_ConnectServer>(bindingStorage);
    gameServer_ = std::make_unique<PacketFunctions_ClientToServer>(bindingStorage);
    chatServer_->SetHandle(handle_);
    connectServer_->SetHandle(handle_);
    gameServer_->SetHandle(handle_);
}

Connection::~Connection()
{
    Close();
}

bool Connection::IsConnected() const noexcept
{
    return handle_ > 0 && !disconnected_.load(std::memory_order_acquire);
}

bool Connection::HasActiveRoute() const noexcept
{
    return route_.has_value() && network_.IsRouteActive(*route_);
}

void Connection::Send(const BYTE *data, std::int32_t length)
{
    if (data == nullptr || length <= 0 || !IsConnected())
    {
        return;
    }

    ::Send send = bindings_.Bindings().Core.dotnet_send;
    if (send == nullptr)
    {
        ReportDotNetError("ConnectionManager_Send");
        return;
    }
    send(handle_, data, length);
}

bool Connection::BeginOrderedSendCapture(SessionOrderedEffectBatch &effects,
                                         std::uint64_t &nextStableSequence) noexcept
{
    BeginSendCapture beginCapture = bindings_.Bindings().Core.dotnet_beginSendCapture;
    if (!IsConnected() || beginCapture == nullptr || sendCaptureEffects_ != nullptr ||
        nextStableSequence == 0)
    {
        return false;
    }

    sendCaptureEffects_ = &effects;
    sendCaptureNextStableSequence_ = &nextStableSequence;
    if (beginCapture(handle_, reinterpret_cast<std::uintptr_t>(this),
                     &Connection::OnSendCaptured) == 0)
    {
        sendCaptureEffects_ = nullptr;
        sendCaptureNextStableSequence_ = nullptr;
        return false;
    }
    return true;
}

bool Connection::EndOrderedSendCapture() noexcept
{
    if (sendCaptureEffects_ == nullptr || sendCaptureNextStableSequence_ == nullptr)
    {
        return false;
    }

    EndSendCapture endCapture = bindings_.Bindings().Core.dotnet_endSendCapture;
    const bool succeeded = endCapture != nullptr && endCapture(handle_) != 0;
    sendCaptureEffects_ = nullptr;
    sendCaptureNextStableSequence_ = nullptr;
    return succeeded;
}

bool Connection::CanCommitCapturedSend(SessionId sessionId, std::uint32_t channel) const noexcept
{
    return handle_ > 0 && static_cast<std::uint32_t>(handle_) == channel &&
           bindings_.Bindings().Core.dotnet_send != nullptr && route_.has_value() &&
           route_->sessionId == sessionId && network_.IsRouteActive(*route_);
}

void Connection::CommitCapturedSend(std::span<const std::byte> payload) noexcept
{
    if (payload.empty() ||
        payload.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
    {
        return;
    }
    Send(reinterpret_cast<const BYTE *>(payload.data()), static_cast<std::int32_t>(payload.size()));
}

void Connection::Close() noexcept
{
    if (handle_ <= 0)
    {
        return;
    }

    if (sendCaptureEffects_ != nullptr)
    {
        (void)EndOrderedSendCapture();
    }

    if (route_.has_value())
    {
        (void)network_.UnregisterRoute(*route_);
        route_.reset();
    }

    if (Disconnect disconnect = bindings_.Bindings().Core.dotnet_disconnect)
    {
        disconnect(handle_);
    }
    handle_ = 0;
}

std::int32_t Connection::GetHandle() const noexcept
{
    return handle_;
}

PacketFunctions_ChatServer *Connection::ToChatServer() const noexcept
{
    return chatServer_.get();
}

PacketFunctions_ConnectServer *Connection::ToConnectServer() const noexcept
{
    return connectServer_.get();
}

PacketFunctions_ClientToServer *Connection::ToGameServer() const noexcept
{
    return gameServer_.get();
}

void CORECLR_DELEGATE_CALLTYPE Connection::OnPacketReceived(std::uintptr_t context,
                                                            std::int32_t handle,
                                                            std::uint64_t generation,
                                                            std::int32_t size, BYTE *data)
{
    auto *connection = reinterpret_cast<Connection *>(context);
    if (connection == nullptr)
    {
        return;
    }
    (void)connection->network_.QueuePacket(handle, generation, data, size);
}

void CORECLR_DELEGATE_CALLTYPE Connection::OnDisconnected(std::uintptr_t context,
                                                          std::int32_t handle,
                                                          std::uint64_t generation)
{
    auto *connection = reinterpret_cast<Connection *>(context);
    if (connection != nullptr)
    {
        connection->disconnected_.store(true, std::memory_order_release);
        connection->network_.NotifyDisconnected(handle, generation);
    }
}

BYTE CORECLR_DELEGATE_CALLTYPE Connection::OnSendCaptured(std::uintptr_t context,
                                                          std::int32_t handle, std::int32_t size,
                                                          BYTE *data)
{
    auto *connection = reinterpret_cast<Connection *>(context);
    if (connection == nullptr || handle != connection->handle_ || size <= 0 || data == nullptr ||
        connection->sendCaptureEffects_ == nullptr ||
        connection->sendCaptureNextStableSequence_ == nullptr)
    {
        return 0;
    }

    const auto payload = std::as_bytes(std::span(data, static_cast<std::size_t>(size)));
    if (!connection->sendCaptureEffects_->TryAppend(
            SessionOrderedEffectKind::ManagedConnectionSend, static_cast<std::uint32_t>(handle),
            *connection->sendCaptureNextStableSequence_, payload))
    {
        return 0;
    }
    ++*connection->sendCaptureNextStableSequence_;
    return 1;
}

bool Connection::BeginReceive(std::uint64_t generation) noexcept
{
    ::BeginReceive beginReceive = bindings_.Bindings().Core.dotnet_beginreceive;
    if (handle_ <= 0 || beginReceive == nullptr)
    {
        return false;
    }
    beginReceive(handle_, generation);
    return true;
}

bool Connection::AttachRoute(const ConnectionRouteSnapshot &route) noexcept
{
    if (route.connectionHandle != handle_)
    {
        return false;
    }
    if (route_.has_value() && network_.IsRouteActive(*route_))
    {
        return false;
    }
    route_ = route;
    return true;
}

void Connection::DetachRoute(const ConnectionRouteSnapshot &route) noexcept
{
    if (route_.has_value() && *route_ == route)
    {
        route_.reset();
    }
}
// <copyright file="PacketFunctions.cpp" company="MUnique">
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
// </copyright>

// <auto-generated>
//     This source code was auto-generated by an XSL transformation.
//     Do not change this file. Instead, change the XML data which contains
//     the packet definitions and re-run the transformation (publish/rebuild the
//     managed library project).
// </auto-generated>

PacketFunctions_Base::PacketFunctions_Base(ManagedBindingStorage &bindings) noexcept
    : bindings_(bindings)
{
}

void PacketFunctions_ClientToServer_Custom::SendLogin(const wchar_t *username,
                                                      const wchar_t *password,
                                                      const BYTE *clientVersion,
                                                      const BYTE *clientSerial)
{
    this->Bindings().Custom.dotnet_SendLogin(this->GetHandle(), username, password, GetTickCount(),
                                             clientVersion, clientSerial);
}

void PacketFunctions_ChatServer_Custom::SendAuthenticateExt(uint16_t roomId, uint32_t token)
{
    this->Bindings().Custom.dotnet_SendAuthenticateExt(this->GetHandle(), roomId, token);
}

void PacketFunctions_ChatServer_Custom::SendChatMessageExt(BYTE senderIndex, const wchar_t *message)
{
    this->Bindings().Custom.dotnet_SendChatMessageExt(this->GetHandle(), senderIndex, message);
}

CServerGroup::CServerGroup()
{
    m_iSequence = 0;
    m_iServerIndex = 0;
    m_iNumServer = 0;

    for (int i = 0; i < MAX_SERVER_PER_GROUP; i++)
    {
        m_abyNonPvpServer[i] = 0;
    }
}

CServerGroup::~CServerGroup()
{
    Release();
}

void CServerGroup::Release()
{
    auto iterServer = m_listServerInfo.begin();
    for (; iterServer != m_listServerInfo.end(); iterServer++)
    {
        delete (*iterServer);
    }

    m_listServerInfo.clear();
}

void CServerGroup::InsertServerInfo(CServerInfo *pServerInfo)
{
    m_listServerInfo.push_back(pServerInfo);

    m_iterServerList = m_listServerInfo.begin();

    m_iNumServer = m_listServerInfo.size();
}

void CServerGroup::SetFirst()
{
    m_iterServerList = m_listServerInfo.begin();
}

bool CServerGroup::GetNext(OUT CServerInfo *&pServerInfo)
{
    if (m_iterServerList == m_listServerInfo.end())
    {
        pServerInfo = NULL;

        return false;
    }

    pServerInfo = (*m_iterServerList);

    m_iterServerList++;

    return true;
}

int CServerGroup::GetServerSize()
{
    return m_listServerInfo.size();
}

CServerInfo *CServerGroup::GetServerInfo(int iSequence)
{
    auto iterServer = m_listServerInfo.begin();

    while (iterServer != m_listServerInfo.end())
    {
        if ((*iterServer)->m_iSequence == iSequence)
            return (*iterServer);

        iterServer++;
    }

    return NULL;
}

CServerInfo::CServerInfo()
{
}

CServerInfo::~CServerInfo()
{
}

CServerListManager::CServerListManager(SessionKeeper &keeper) noexcept
    : m_iTotalServer(0), m_szSelectServerName{}, m_iSelectServerIndex(-1), m_byNonPvP(0),
      g_ErrorReport(keeper.ErrorReport())
{
}

CServerListManager::~CServerListManager()
{
    Release();
}

void CServerListManager::Release()
{
    auto iterServerGroup = m_mapServerGroup.begin();
    for (; iterServerGroup != m_mapServerGroup.end(); iterServerGroup++)
    {
        delete iterServerGroup->second;
    }

    m_mapServerGroup.clear();
    m_iTotalServer = 0;
}

bool CServerListManager::LoadServerListScript()
{
    if (scriptLoaded_)
        return true;
    std::ifstream file("Data/Local/ServerList.bmd", std::ios::binary);
    if (!file)
    {
        g_ErrorReport.Write(L"Cannot open Data/Local/ServerList.bmd\r\n");
        return false;
    }

#pragma pack(push, 1)
    typedef struct _SERVER_GROUP_INFO
    {
        WORD m_wIndex;
        char m_szName[SLM_MAX_SERVER_NAME_LENGTH];
        BYTE m_byPos;
        BYTE m_bySequence;
        BYTE m_abyNonPVP[SLM_MAX_SERVER_COUNT];
        short m_nDescriptLen;
    } SERVER_GROUP_INFO;
#pragma pack(pop)

    ServerListScriptMap candidate;
    SERVER_GROUP_INFO record{};
    while (file.read(reinterpret_cast<char *>(&record), sizeof(record)))
    {
        BuxConvert(reinterpret_cast<BYTE *>(&record), sizeof(record));
        if (record.m_nDescriptLen < 0 ||
            std::find(std::begin(record.m_szName), std::end(record.m_szName), '\0') ==
                std::end(record.m_szName))
            return false;
        // Descriptions were never displayed by this script reader. Consume their exact extent.
        file.ignore(record.m_nDescriptLen);
        if (file.gcount() != record.m_nDescriptLen)
            return false;
        SServerGroupInfo info{};
        CMultiLanguage::ConvertFromUtf8(info.m_szName, record.m_szName);
        info.m_byPos = record.m_byPos;
        info.m_bySequence = record.m_bySequence;
        std::copy_n(record.m_abyNonPVP, SLM_MAX_SERVER_COUNT, info.m_abyNonPVP);
        candidate.emplace(record.m_wIndex, std::move(info));
    }
    if (!file.eof() || file.gcount() != 0 || candidate.empty())
        return false;
    m_mapServerListScript = std::move(candidate);
    scriptLoaded_ = true;
    return true;
}

const SServerGroupInfo *CServerListManager::GetServerGroupInfoInScript(WORD wServerGroupIndex)
{
    ServerListScriptMap::const_iterator iter = m_mapServerListScript.find(wServerGroupIndex);
    if (iter == m_mapServerListScript.end())
        return NULL;

    return &(iter->second);
}

void CServerListManager::InsertServerGroup(int iConnectIndex, int iServerPercent)
{
    CServerGroup *pServerGroup = NULL;

    auto iterServerGroup = m_mapServerGroup.begin();

    bool bEqual = false;
    while (iterServerGroup != m_mapServerGroup.end())
    {
        if ((iterServerGroup->second)->m_iServerIndex == iConnectIndex / MAX_SERVER_PER_GROUP)
        {
            bEqual = true;
            break;
        }

        iterServerGroup++;
    }

    if (bEqual == true)
    {
        pServerGroup = iterServerGroup->second;
    }
    else
    {
        pServerGroup = new CServerGroup;

        if (MakeServerGroup(iConnectIndex / MAX_SERVER_PER_GROUP, pServerGroup) == false)
            return;

        m_mapServerGroup.insert(
            type_mapServerGroup::value_type(pServerGroup->m_iSequence, pServerGroup));
    }

    InsertServer(pServerGroup, iConnectIndex, iServerPercent);

    m_iterServerGroup = m_mapServerGroup.begin();
}

bool CServerListManager::MakeServerGroup(IN int iServerGroupIndex, OUT CServerGroup *pServerGroup)
{
    const SServerGroupInfo *pServerGroupInfo = GetServerGroupInfoInScript(iServerGroupIndex);
    if (NULL == pServerGroupInfo)
        return false;

    ::wcscpy(pServerGroup->m_szName, pServerGroupInfo->m_szName);
    ::wcscpy(pServerGroup->m_szDescription, pServerGroupInfo->m_strDescript.c_str());
    pServerGroup->m_iSequence = (int)pServerGroupInfo->m_bySequence;
    pServerGroup->m_iWidthPos = (int)pServerGroupInfo->m_byPos;
    pServerGroup->m_iServerIndex = iServerGroupIndex;
    pServerGroup->m_bPvPServer = true;
    int i;
    for (i = 0; i < SLM_MAX_SERVER_COUNT; ++i)
    {
        pServerGroup->m_abyNonPvpServer[i] = pServerGroupInfo->m_abyNonPVP[i];
        if (0x01 & pServerGroup->m_abyNonPvpServer[i])
            pServerGroup->m_bPvPServer = false;
    }
    for (; i < MAX_SERVER_PER_GROUP; ++i)
        pServerGroup->m_abyNonPvpServer[i] = 0;

    return true;
}

void CServerListManager::InsertServer(CServerGroup *pServerGroup, int iConnectIndex,
                                      int iServerPercent)
{
    auto *pServerInfo = new CServerInfo;
    pServerInfo->m_iSequence = pServerGroup->GetServerSize();
    pServerInfo->m_iIndex = (iConnectIndex % MAX_SERVER_PER_GROUP) + 1;
    pServerInfo->m_iConnectIndex = iConnectIndex;
    pServerInfo->m_iPercent = iServerPercent;
    pServerInfo->m_byNonPvP = pServerGroup->m_abyNonPvpServer[pServerInfo->m_iIndex - 1];

    int iTextIndex;
    if (iServerPercent >= 128)
    {
        iTextIndex = 560;
    }
    else if (iServerPercent >= 100)
    {
        iTextIndex = 561;
    }
    else
    {
        iTextIndex = 562;
    }

    switch (pServerInfo->m_byNonPvP)
    {
    case 0:
        mu_swprintf(pServerInfo->m_bName, L"%ls-%d %ls", pServerGroup->m_szName,
                    pServerInfo->m_iIndex, I18N::Game::Lookup(iTextIndex));
        break;

    case 1:
        mu_swprintf(pServerInfo->m_bName, L"%ls-%d(Non-PVP) %ls", pServerGroup->m_szName,
                    pServerInfo->m_iIndex, I18N::Game::Lookup(iTextIndex));
        break;

    case 2:
        mu_swprintf(pServerInfo->m_bName, L"%ls-%d(Gold PVP) %ls", pServerGroup->m_szName,
                    pServerInfo->m_iIndex, I18N::Game::Lookup(iTextIndex));
        break;

    case 3:
        mu_swprintf(pServerInfo->m_bName, L"%ls-%d(Gold) %ls", pServerGroup->m_szName,
                    pServerInfo->m_iIndex, I18N::Game::Lookup(iTextIndex));
        break;
    }

    pServerGroup->InsertServerInfo(pServerInfo);
}

int CServerListManager::GetServerGroupSize()
{
    return m_mapServerGroup.size();
}

void CServerListManager::SetFirst()
{
    m_iterServerGroup = m_mapServerGroup.begin();
}

bool CServerListManager::GetNext(OUT CServerGroup *&pServerGroup)
{
    if (m_iterServerGroup == m_mapServerGroup.end())
    {
        pServerGroup = NULL;

        return false;
    }

    pServerGroup = m_iterServerGroup->second;

    m_iterServerGroup++;

    return true;
}

CServerGroup *CServerListManager::GetServerGroupByBtnPos(int iBtnPos)
{
    auto iterServerGroup = m_mapServerGroup.begin();

    while (iterServerGroup != m_mapServerGroup.end())
    {
        if (iterServerGroup->second->m_iBtnPos == iBtnPos)
            return iterServerGroup->second;

        iterServerGroup++;
    }

    return NULL;
}

void CServerListManager::SetSelectServerInfo(wchar_t *pszName, int iIndex, BYTE byNonPvP)
{
    wcscpy(m_szSelectServerName, pszName);
    m_iSelectServerIndex = iIndex;
    m_byNonPvP = byNonPvP;
}

wchar_t *CServerListManager::GetSelectServerName()
{
    return m_szSelectServerName;
}

int CServerListManager::GetSelectServerIndex()
{
    return m_iSelectServerIndex;
}

BYTE CServerListManager::GetNonPVPInfo()
{
    return m_byNonPvP;
}

bool CServerListManager::IsNonPvP()
{
    return bool(0x01 & GetNonPVPInfo());
}

void CServerListManager::SetTotalServer(int iTotalServer)
{
    m_iTotalServer = iTotalServer;
}

int CServerListManager::GetTotalServer()
{
    return m_iTotalServer;
}
