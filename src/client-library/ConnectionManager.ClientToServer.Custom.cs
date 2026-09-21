// <copyright file="ConnectionManager.ClientToServer.Custom.cs" company="MUnique">
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
// </copyright>

namespace MUnique.Client.Library;

using System;
using System.Diagnostics;
using System.IO.Pipelines;
using System.Runtime.InteropServices;
using System.Text;
using MUnique.OpenMU.Network;
using MUnique.OpenMU.Network.Packets.ClientToServer;
using MUnique.OpenMU.Network.Xor;

/// <summary>
/// Extension methods to start writing messages of this namespace on a <see cref="IConnection"/>.
/// </summary>
public unsafe partial class ConnectionManager
{
    private static readonly Xor3Encryptor Xor3Encryptor = new(0);

    /// <summary>
    /// Sends a <see cref="LoginLongPassword" /> to this connection.
    /// </summary>
    /// <param name="handle">The handle of the connection.</param>
    /// <param name="username">The user name, "encrypted" with Xor3.</param>
    /// <param name="password">The password, "encrypted" with Xor3.</param>
    /// <param name="tickCount">The tick count.</param>
    /// <param name="clientVersion">The client version.</param>
    /// <param name="clientSerial">The client serial.</param>
    /// <remarks>
    /// Is sent by the client when: The player tries to log into the game.
    /// Causes reaction on server side: The server is authenticating the sent login name and password. If it's correct, the state of the player is proceeding to be logged in.
    /// </remarks>
    [UnmanagedCallersOnly(EntryPoint = "ConnectionManager_SendLogin")]
    public static void SendLogin(int handle, IntPtr username, IntPtr password, uint @tickCount, byte* @clientVersion, byte* @clientSerial)
    {
        if (!Connections.TryGetValue(handle, out var connection))
        {
            return;
        }

        try
        {
            var usernameStr = NativeInterop.PtrToWideString(@username)
                ?? throw new ArgumentNullException(nameof(username));
            var passwordStr = NativeInterop.PtrToWideString(@password)
                ?? throw new ArgumentNullException(nameof(password));
            ArgumentNullException.ThrowIfNull(@clientVersion);
            ArgumentNullException.ThrowIfNull(@clientSerial);

            const int usernameLength = 10;
            const int passwordLength = 20;
            if (Encoding.UTF8.GetByteCount(usernameStr) > usernameLength
                || Encoding.UTF8.GetByteCount(passwordStr) > passwordLength)
            {
                throw new ArgumentException("Login credentials exceed packet field length.");
            }

            connection.CreateAndSend(writer => WriteLoginPacket(
                writer, usernameStr, passwordStr, @tickCount, @clientVersion, @clientSerial));
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Login packet staging failed: {ex.GetType().Name}");
        }
    }

    private static int WriteLoginPacket(
        PipeWriter writer,
        string username,
        string password,
        uint tickCount,
        byte* clientVersion,
        byte* clientSerial)
    {
        var length = LoginLongPasswordRef.Length;
        var packet = new LoginLongPasswordRef(writer.GetSpan(length)[..length]);
        packet.Username.Clear();
        packet.Password.Clear();
        Encoding.UTF8.GetBytes(username, packet.Username);
        Encoding.UTF8.GetBytes(password, packet.Password);
        Xor3Encryptor.Encrypt(packet.Username);
        Xor3Encryptor.Encrypt(packet.Password);
        packet.TickCount = tickCount;
        new Span<byte>(clientVersion, packet.ClientVersion.Length).CopyTo(packet.ClientVersion);
        new Span<byte>(clientSerial, packet.ClientSerial.Length).CopyTo(packet.ClientSerial);
        return length;
    }
}
