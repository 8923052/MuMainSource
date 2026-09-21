namespace MuLauncher.Models;

public sealed record ChannelStatus(ushort Id, byte Load, string Host, int? Port)
{
    public const int ChannelsPerServer = 20;
    public string ServerName { get; init; } = "";
    public string Name => $"{(ServerName.Length > 0 ? ServerName + " · " : "")}Channel {Id % ChannelsPerServer + 1:00}";
    public string Endpoint => Port.HasValue ? $"{Host}:{Port}" : "Endpoint unavailable";
    public bool Online => Load < 128;
    public int LoadValue => Math.Min((int)Load, 100);
    public string LoadLabel => Online ? $"{LoadValue}% load" : "Offline";
    public string State => !Online ? "OFFLINE" : Load >= 100 ? "FULL" : "ONLINE";
    public string StatusColor => !Online ? "#84909F" : Load >= 100 ? "#EFB866" : "#77D9B4";
    public string Details => $"ID {Id}  ·  {Endpoint}";
}
