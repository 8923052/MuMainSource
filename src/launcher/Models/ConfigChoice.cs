namespace MuLauncher.Models;

public sealed record ConfigChoice(string Value, string Label)
{
    public override string ToString() => Label;
}
