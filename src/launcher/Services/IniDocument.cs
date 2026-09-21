using System.Globalization;

namespace MuLauncher.Services;

public sealed class IniDocument
{
    private readonly List<string> lines;
    public IniDocument(string text) => lines = text.Replace("\r\n", "\n").Split('\n').ToList();

    public IEnumerable<string> Sections => lines.Select(line => line.Trim())
        .Where(line => line.StartsWith('[') && line.EndsWith(']')).Select(line => line[1..^1]);

    public void AddSection(string section)
    {
        if (!lines.Any(line => IsSection(line, section))) lines.Add($"[{section}]");
    }

    public string Get(string section, string key, string fallback = "")
    {
        var index = FindKey(section, key);
        return index < 0 ? fallback : lines[index][(lines[index].IndexOf('=') + 1)..].Trim();
    }

    public int GetInt(string section, string key, int fallback = 0) =>
        int.TryParse(Get(section, key), NumberStyles.Integer, CultureInfo.InvariantCulture,
            out var number) ? number : fallback;

    public void Set(string section, string key, string value)
    {
        if (value.Contains('\n') || value.Contains('\r'))
            throw new ArgumentException($"{key} must be a single line.");
        var index = FindKey(section, key);
        if (index >= 0) { lines[index] = $"{key}={value}"; return; }
        index = lines.FindIndex(line => IsSection(line, section));
        if (index < 0) { lines.Add($"[{section}]"); index = lines.Count - 1; }
        lines.Insert(index + 1, $"{key}={value}");
    }

    public void Remove(string section, string key)
    {
        for (var index = FindKey(section, key); index >= 0; index = FindKey(section, key))
            lines.RemoveAt(index);
    }

    public void RemoveSection(string section)
    {
        var start = lines.FindIndex(line => IsSection(line, section));
        if (start < 0) return;
        var end = lines.FindIndex(start + 1, line => line.TrimStart().StartsWith('['));
        lines.RemoveRange(start, (end < 0 ? lines.Count : end) - start);
    }

    private int FindKey(string section, string key)
    {
        var active = false;
        for (var index = 0; index < lines.Count; index++)
        {
            var line = lines[index].Trim();
            if (line.StartsWith('[')) { active = IsSection(line, section); continue; }
            var separator = line.IndexOf('=');
            if (active && separator > 0 && line[..separator].Trim().Equals(key,
                    StringComparison.OrdinalIgnoreCase)) return index;
        }
        return -1;
    }

    private static bool IsSection(string line, string section) =>
        line.Trim().Equals($"[{section}]", StringComparison.OrdinalIgnoreCase);

    public override string ToString() => string.Join(Environment.NewLine, lines).TrimEnd()
        + Environment.NewLine;

    public void Save(string path)
    {
        var temporary = path + ".tmp";
        try
        {
            File.WriteAllText(temporary, ToString(), System.Text.Encoding.Unicode);
            File.Move(temporary, path, true);
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
}
