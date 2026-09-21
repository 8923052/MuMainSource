using System.Buffers.Binary;
using System.Text;

namespace MuLauncher.Services;

public static class ServerNames
{
    public static IReadOnlyDictionary<int, string> Load(string clientDirectory)
    {
        var path = Path.Combine(clientDirectory, "Data", "Local", "ServerList.bmd");
        var names = new Dictionary<int, string>();
        if (!File.Exists(path)) return names;
        using var stream = File.OpenRead(path);
        const int headerLength = 53;
        byte[] xorKey = [0xFC, 0xCF, 0xAB];
        var header = new byte[headerLength];
        while (stream.Position < stream.Length)
        {
            stream.ReadExactly(header);
            for (var index = 0; index < header.Length; index++) header[index] ^= xorKey[index % xorKey.Length];
            var id = BinaryPrimitives.ReadUInt16LittleEndian(header);
            names[id] = Encoding.UTF8.GetString(header, 2, 32).Split('\0')[0];
            var descriptionLength = BinaryPrimitives.ReadInt16LittleEndian(header.AsSpan(51));
            if (descriptionLength < 0 || descriptionLength > stream.Length - stream.Position)
                throw new IOException("Invalid server-list description length.");
            stream.Seek(descriptionLength, SeekOrigin.Current);
        }
        return names;
    }
}
