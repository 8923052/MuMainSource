namespace MuLauncher.Models;

public sealed record ServerSnapshot(long LatencyMilliseconds, IReadOnlyList<ChannelStatus> Channels);
