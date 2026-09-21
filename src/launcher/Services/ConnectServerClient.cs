using System.Buffers.Binary;
using System.Diagnostics;
using System.Net.Sockets;
using System.Text;
using MuLauncher.Models;

namespace MuLauncher.Services;

public sealed class ConnectServerClient
{
    public async Task<ServerSnapshot> QueryAsync(string host, int port, CancellationToken cancellation)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellation);
        timeout.CancelAfter(TimeSpan.FromSeconds(8));
        using var client = new TcpClient();
        var elapsed = Stopwatch.StartNew();
        await client.ConnectAsync(host, port, timeout.Token);
        using var stream = client.GetStream();
        var hello = await ReadPacketAsync(stream, timeout.Token);
        if (!IsPacket(hello, 0x00, 0x01)) throw new IOException("The endpoint is not a MU connect server.");
        var latency = elapsed.ElapsedMilliseconds;
        await stream.WriteAsync(new byte[] { 0xC1, 4, 0xF4, 6 }, timeout.Token);
        var response = await ReadResponseAsync(stream, timeout.Token);
        var channels = ParseServerList(response);
        var result = channels.Select(channel => new ChannelStatus(channel.Id, channel.Load, host, null)).ToList();
        for (var index = 0; index < result.Count; index++)
        {
            try
            {
                var endpoint = await ReadEndpointAsync(stream, result[index].Id, timeout.Token);
                result[index] = result[index] with { Host = endpoint?.Host ?? host, Port = endpoint?.Port };
            }
            catch (Exception error) when (error is IOException or SocketException or OperationCanceledException)
            {
                cancellation.ThrowIfCancellationRequested();
                // A server list still proves availability when endpoint lookup is unsupported.
                break;
            }
        }
        return new ServerSnapshot(latency, result);
    }

    private static async Task<(string Host, int Port)?> ReadEndpointAsync(
        NetworkStream stream, ushort id, CancellationToken cancellation)
    {
        byte[] request = [0xC1, 6, 0xF4, 3, (byte)id, (byte)(id >> 8)];
        await stream.WriteAsync(request, cancellation);
        var packet = await ReadResponseAsync(stream, cancellation);
        if (!IsPacket(packet, 0xF4, 3)) return null;
        if (packet.Length < 22) throw new IOException("Incomplete channel endpoint.");
        var host = Encoding.ASCII.GetString(packet, 4, 16).TrimEnd('\0');
        return (host, BinaryPrimitives.ReadUInt16LittleEndian(packet.AsSpan(20)));
    }

    public static IReadOnlyList<(ushort Id, byte Load)> ParseServerList(byte[] packet)
    {
        if (packet.Length < 7 || packet[0] != 0xC2 || !IsPacket(packet, 0xF4, 6))
            throw new IOException("Invalid server-list response.");
        var count = BinaryPrimitives.ReadUInt16BigEndian(packet.AsSpan(5));
        if (packet.Length != 7 + count * 4) throw new IOException("Incomplete server list.");
        var result = new List<(ushort, byte)>(count);
        for (var index = 0; index < count; index++)
        {
            var entry = packet.AsSpan(7 + index * 4);
            result.Add((BinaryPrimitives.ReadUInt16LittleEndian(entry), entry[2]));
        }
        return result;
    }

    private static bool IsPacket(byte[] packet, byte code, byte subcode)
    {
        var offset = packet[0] == 0xC2 ? 3 : 2;
        return packet.Length > offset + 1 && packet[offset] == code && packet[offset + 1] == subcode;
    }

    private static async Task<byte[]> ReadResponseAsync(NetworkStream stream, CancellationToken cancellation)
    {
        byte[] packet;
        do
        {
            packet = await ReadPacketAsync(stream, cancellation);
            // Repeated hellos are notifications, not replies to channel queries.
        } while (IsPacket(packet, 0x00, 0x01));
        return packet;
    }

    private static async Task<byte[]> ReadPacketAsync(NetworkStream stream, CancellationToken cancellation)
    {
        var prefix = new byte[3];
        await stream.ReadExactlyAsync(prefix.AsMemory(0, 2), cancellation);
        var headerLength = 2;
        var length = (int)prefix[1];
        if (prefix[0] == 0xC2)
        {
            await stream.ReadExactlyAsync(prefix.AsMemory(2, 1), cancellation);
            length = BinaryPrimitives.ReadUInt16BigEndian(prefix.AsSpan(1));
            headerLength = 3;
        }
        else if (prefix[0] != 0xC1) throw new IOException("Unrecognized connect-server packet.");
        if (length < headerLength + 1) throw new IOException("Invalid packet length.");
        var packet = new byte[length];
        prefix.AsSpan(0, headerLength).CopyTo(packet);
        await stream.ReadExactlyAsync(packet.AsMemory(headerLength), cancellation);
        return packet;
    }
}
