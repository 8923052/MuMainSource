// <copyright file="ConnectionManager.cs" company="MUnique">
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
// </copyright>

namespace MUnique.Client.Library;

using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging.Abstractions;
using MUnique.OpenMU.Network;
using MUnique.OpenMU.Network.SimpleModulus;
using MUnique.OpenMU.Network.Xor;
using Pipelines.Sockets.Unofficial;

/// <summary>
/// Class which manages the connections which are created through the game client.
/// To identify each connection, we use handles (a simple number).
/// </summary>
public unsafe partial class ConnectionManager
{
    /// <summary>
    /// Idle time, in seconds, before the OS starts sending TCP keep-alive probes.
    /// </summary>
    private const int KeepAliveIdleSeconds = 4;

    /// <summary>
    /// Interval, in seconds, between TCP keep-alive probes.
    /// </summary>
    private const int KeepAliveIntervalSeconds = 2;

    /// <summary>
    /// Number of unanswered keep-alive probes before the connection is dropped.
    /// </summary>
    private const int KeepAliveRetryCount = 3;

    /// <summary>
    /// The currently active connections, with their handle as key.
    /// </summary>
    private static readonly ConcurrentDictionary<int, ConnectionWrapper> Connections = new();

    /// <summary>
    /// The currently used maximum handle number.
    /// </summary>
    private static int _maxHandle;

    /// <summary>
    /// Connects the specified host and port.
    /// </summary>
    /// <param name="hostPtr">The pointer to a string which contains the host (ip or hostname).</param>
    /// <param name="port">The port.</param>
    /// <param name="isEncrypted">
    /// A flag, if the connection is supposed to be encrypted.
    /// This is usually <c>1</c> for connections to the game server, but <c>0</c> for connections to the connect server.
    /// </param>
    /// <param name="callbackContext">The native context returned unchanged with each callback.</param>
    /// <param name="onPacketReceived">The pointer to an unmanaged method which is called when a new packet got received.
    /// Parameters: context, handle, route generation, size, pointer to the data.</param>
    /// <param name="onDisconnected">The pointer to an unmanaged method which is called when the connection got disconnected.
    /// Parameters: context, handle, route generation.</param>
    /// <returns>
    /// The handle of the created connection. If negative, the connection couldn't be established.
    /// </returns>
    [UnmanagedCallersOnly(EntryPoint = "ConnectionManager_Connect")]
    public static int Connect(
        IntPtr hostPtr,
        int port,
        byte isEncrypted,
        nuint callbackContext,
        delegate* unmanaged<nuint, int, ulong, int, byte*, void> onPacketReceived,
        delegate* unmanaged<nuint, int, ulong, void> onDisconnected)
    {
        try
        {
            var host = NativeInterop.PtrToWideString(hostPtr) ?? throw new ArgumentNullException(nameof(hostPtr));
            return ConnectInner(
                host,
                port,
                isEncrypted == 1,
                callbackContext,
                onPacketReceived,
                onDisconnected);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Error establishing connection: {ex}");
            return -1;
        }
    }

    /// <summary>
    /// Sends a packet over the connection of the specified handle.
    /// </summary>
    /// <param name="handle">The handle of the connection.</param>
    /// <param name="data">The pointer to the packet data.</param>
    /// <param name="count">The count of bytes which should be sent.</param>
    [UnmanagedCallersOnly(EntryPoint = "ConnectionManager_Send")]
    public static void Send(int handle, byte* data, int count)
    {
        if (Connections.TryGetValue(handle, out var connection))
        {
            try
            {
                var bytes = new Span<byte>(data, count);
                bytes.SetPacketSize();
                connection.Send(bytes);
                Debug.WriteLine("Sent {0} bytes with handle {1}", count, handle);
            }
            catch (Exception ex)
            {
                // A send failure means the connection is broken. Tear it down so
                // the Disconnected event fires and the client can auto-reconnect,
                // instead of silently swallowing the error and looking online.
                Debug.WriteLine($"Error sending {count} bytes with handle {handle}: {ex}");
                connection.DisconnectAndDispose();
            }
        }
        else
        {
            Debug.WriteLine("Connection with handle {0} not found.", handle);
        }
    }

    /// <summary>
    /// Begins bounded plaintext packet capture for an existing connection.
    /// </summary>
    /// <param name="handle">The connection handle.</param>
    /// <param name="callbackContext">The native callback context.</param>
    /// <param name="onSendCaptured">The native packet callback.</param>
    /// <returns>One when capture began; otherwise zero.</returns>
    [UnmanagedCallersOnly(EntryPoint = "ConnectionManager_BeginSendCapture")]
    public static byte BeginSendCapture(
        int handle,
        nuint callbackContext,
        delegate* unmanaged<nuint, int, int, byte*, byte> onSendCaptured)
    {
        try
        {
            return Connections.TryGetValue(handle, out var connection)
                && connection.BeginSendCapture(
                    callbackContext, onSendCaptured)
                ? (byte)1 : (byte)0;
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Error beginning send capture for handle {handle}: {ex}");
            return 0;
        }
    }

    /// <summary>
    /// Ends bounded plaintext packet capture for an existing connection.
    /// </summary>
    /// <param name="handle">The connection handle.</param>
    /// <returns>One when every packet was accepted; otherwise zero.</returns>
    [UnmanagedCallersOnly(EntryPoint = "ConnectionManager_EndSendCapture")]
    public static byte EndSendCapture(int handle)
    {
        try
        {
            return Connections.TryGetValue(handle, out var connection)
                && connection.EndSendCapture()
                ? (byte)1 : (byte)0;
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Error ending send capture for handle {handle}: {ex}");
            return 0;
        }
    }

    /// <summary>
    /// Begins receiving data for the connection of the specified handle.
    /// </summary>
    /// <param name="connectionHandle">The handle of the connection.</param>
    /// <param name="generation">The native route generation attached to callbacks.</param>
    [UnmanagedCallersOnly(EntryPoint = "ConnectionManager_BeginReceive")]
    public static void BeginReceive(int connectionHandle, ulong generation)
    {
        if (Connections.TryGetValue(connectionHandle, out var connection))
        {
            connection.BeginReceive(generation);
        }
    }

    /// <summary>
    /// Disconnects the connection of the specified handle.
    /// </summary>
    /// <param name="connectionHandle">The handle of the connection.</param>
    [UnmanagedCallersOnly(EntryPoint = "ConnectionManager_Disconnect")]
    public static void Disconnect(int connectionHandle)
    {
        if (Connections.TryRemove(connectionHandle, out var connection))
        {
            connection.DisconnectAndDispose();
        }
    }

    private static int ConnectInner(
        string host,
        int port,
        bool isEncrypted,
        nuint callbackContext,
        delegate* unmanaged<nuint, int, ulong, int, byte*, void> onPacketReceived,
        delegate* unmanaged<nuint, int, ulong, void> onDisconnected)
    {
        var tcpClient = new TcpClient(host, port);

        ConfigureKeepAlive(tcpClient.Client);

        var socketConnection = SocketConnection.Create(tcpClient.Client);

        var encryptor = isEncrypted ? new PipelinedXor32Encryptor(new PipelinedSimpleModulusEncryptor(socketConnection.Output, PipelinedSimpleModulusEncryptor.DefaultClientKey).Writer) : null;
        var decryptor = isEncrypted ? new PipelinedSimpleModulusDecryptor(socketConnection.Input, PipelinedSimpleModulusDecryptor.DefaultClientKey) : null;
        var connection = new Connection(socketConnection, decryptor, encryptor, new NullLogger<Connection>());

        var handle = Interlocked.Increment(ref _maxHandle);
        var wrapper = new ConnectionWrapper(
            callbackContext,
            handle,
            connection,
            onPacketReceived,
            onDisconnected);
        if (!Connections.TryAdd(handle, wrapper))
        {
            wrapper.Dispose();
            return -1;
        }

        connection.Disconnected += () =>
        {
            Connections.TryRemove(handle, out _);
            return ValueTask.CompletedTask;
        };

        return handle;
    }

    /// <summary>
    /// Enables aggressive TCP keep-alive so the OS detects dead or half-open
    /// connections (server crash, network drop) within ~10 seconds and tears the
    /// socket down. That makes the receive loop fault and raise
    /// <see cref="Connection.Disconnected"/>, which the game client polls to
    /// trigger auto-reconnect. Without this, a peer that vanishes without sending
    /// FIN/RST keeps the connection alive for minutes. Probes are answered by the
    /// healthy peer's OS automatically, so this never drops a live connection.
    /// </summary>
    /// <param name="socket">The connected socket to configure.</param>
    private static void ConfigureKeepAlive(Socket socket)
    {
        try
        {
            socket.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.KeepAlive, true);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to enable SO_KEEPALIVE: {ex}");
        }

        // Modern cross-platform options (.NET 5+). Set each independently so an
        // unsupported one doesn't skip the others (notably the retry count, which
        // older Windows ignores).
        TrySetSocketOption(socket, SocketOptionName.TcpKeepAliveTime, KeepAliveIdleSeconds);
        TrySetSocketOption(socket, SocketOptionName.TcpKeepAliveInterval, KeepAliveIntervalSeconds);
        TrySetSocketOption(socket, SocketOptionName.TcpKeepAliveRetryCount, KeepAliveRetryCount);

        // Windows fallback/override: SIO_KEEPALIVE_VALS reliably sets the idle
        // time and probe interval (in milliseconds) where the socket options
        // above are sometimes ignored.
        try
        {
            if (OperatingSystem.IsWindows())
            {
                var values = new byte[12];
                BitConverter.GetBytes(1u).CopyTo(values, 0);                                        // on
                BitConverter.GetBytes((uint)(KeepAliveIdleSeconds * 1000)).CopyTo(values, 4);       // idle (ms)
                BitConverter.GetBytes((uint)(KeepAliveIntervalSeconds * 1000)).CopyTo(values, 8);   // interval (ms)
                socket.IOControl(IOControlCode.KeepAliveValues, values, null);
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to configure keep-alive via IOControl: {ex}");
        }
    }

    private static void TrySetSocketOption(Socket socket, SocketOptionName option, int value)
    {
        try
        {
            socket.SetSocketOption(SocketOptionLevel.Tcp, option, value);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to set socket option {option}: {ex}");
        }
    }
}
