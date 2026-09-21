using MuLauncher.Models;

namespace MuLauncher.Services;

public sealed class PresetStore(string directory)
{
    public const string DefaultMain = "./Main.exe";
    public string[] List() => Directory.GetFiles(directory, "*.ini")
        .Where(path => !Path.GetFileName(path).Equals("config.ini", StringComparison.OrdinalIgnoreCase)
            && !Path.GetFileName(path).Equals("launcher.ini", StringComparison.OrdinalIgnoreCase))
        .Select(Path.GetFileNameWithoutExtension).OfType<string>().Order(StringComparer.OrdinalIgnoreCase).ToArray();

    public string PathFor(string name)
    {
        name = name.Trim();
        if (name.Length == 0 || name is "." or ".." || name.Equals("config", StringComparison.OrdinalIgnoreCase)
            || name.Equals("launcher", StringComparison.OrdinalIgnoreCase)
            || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || name.Contains('/') || name.Contains('\\'))
            throw new ArgumentException("Choose a preset name without path separators. 'config' and 'launcher' are reserved.");
        return Path.Combine(directory, name + ".ini");
    }

    public List<SessionSlot> Load(string name)
    {
        var document = new IniDocument(File.ReadAllText(PathFor(name)));
        var occupied = document.Get("Sessions", "Slots", "1").Split(',', StringSplitOptions.RemoveEmptyEntries)
            .Select(int.Parse).Where(slot => slot > 0).Distinct().Order().ToArray();
        var sectionSlots = document.Sections.Where(section => section.StartsWith("Slot.", StringComparison.OrdinalIgnoreCase))
            .Select(section => int.Parse(section[5..]));
        var count = Math.Max(sectionSlots.DefaultIfEmpty().Max(), occupied.LastOrDefault());
        var slots = Enumerable.Range(1, count).Select(number => new SessionSlot(number)).ToList();
        for (var index = 0; index < occupied.Length; index++)
            slots[occupied[index] - 1].Session = ReadSession(document, occupied[index], index + 1);
        return slots;
    }

    public string ReadMain(string name) => new IniDocument(File.ReadAllText(PathFor(name)))
        .Get("Sessions", "Main", DefaultMain);

    public string ReadInitDelay(string name) => new IniDocument(File.ReadAllText(PathFor(name)))
        .Get("Sessions", "InitDelay", "0");

    public SessionShortcutSettings ReadShortcuts(string name)
    {
        var document = new IniDocument(File.ReadAllText(PathFor(name)));
        var shortcuts = new SessionShortcutSettings();
        foreach (var binding in shortcuts.Bindings)
            if (!binding.TrySetKeyCodes(document.Get("Sessions", binding.ConfigKey, binding.DefaultKeyCodes)))
                return new SessionShortcutSettings();
        if (shortcuts.Bindings.Select(binding => binding.KeyCodes).Distinct(StringComparer.Ordinal).Count()
            != shortcuts.Bindings.Count)
            return new SessionShortcutSettings();
        return shortcuts;
    }

    private static SessionEntry ReadSession(IniDocument document, int slot, int sessionNumber)
    {
        var section = $"Slot.{slot}";
        if (!document.Sections.Contains(section, StringComparer.OrdinalIgnoreCase)) section = $"Session.{slot}";
        var username = document.Get(section, "Username");
        var legacy = document.Get(section, "EncryptedUsername");
        if (username.Length == 0 && legacy.Length > 0) username = PasswordProtector.Unprotect(legacy);
        var displayName = document.Get(section, "DisplayName");
        return new SessionEntry { DisplayName = displayName.Length == 0 ? $"Session {sessionNumber}" : displayName,
            Username = username, Password = PasswordProtector.Unprotect(document.Get(section, "EncryptedPassword")),
            AutoLoginPort = document.Get(section, "AutoLoginPort", "0"),
            AutoSelectCharacter = document.Get(section, "AutoSelectCharacter") };
    }

    public void Save(string name, IEnumerable<SessionSlot> entries, string main = DefaultMain,
        string initDelay = "0", SessionShortcutSettings? shortcuts = null)
    {
        if (!int.TryParse(initDelay, out var delay) || delay < 0 || delay > 86400)
            throw new ArgumentException("Session init delay must be 0–86400 seconds.");
        shortcuts ??= new SessionShortcutSettings();
        var usedShortcuts = new HashSet<string>(StringComparer.Ordinal);
        foreach (var binding in shortcuts.Bindings)
            if (!SessionShortcutBinding.TryParseKeyCodes(binding.KeyCodes, out _)
                || !usedShortcuts.Add(binding.KeyCodes))
                throw new ArgumentException($"{binding.Name}: choose a unique shortcut or reset it to default.");
        var slots = entries.ToArray();
        var occupied = slots.Where(slot => slot.HasSession).ToArray();
        var document = new IniDocument("; MuTwo session preset — passwords are protected for this Windows user.\n");
        document.Set("Sessions", "Slots", string.Join(',', occupied.Select(slot => slot.Number)));
        document.Set("Sessions", "Main", main);
        document.Set("Sessions", "InitDelay", delay.ToString(System.Globalization.CultureInfo.InvariantCulture));
        foreach (var binding in shortcuts.Bindings)
            document.Set("Sessions", binding.ConfigKey, binding.KeyCodes);
        foreach (var slot in slots)
        {
            document.AddSection($"Slot.{slot.Number}");
            if (slot.HasSession) WriteSession(document, slot.Number, slot.Session!);
        }
        document.Save(PathFor(name));
    }

    private static void WriteSession(IniDocument document, int slot, SessionEntry entry)
    {
        const int maximumUsernameLength = 10;
        const int maximumPasswordLength = 20;
        if (entry.Username.Length > maximumUsernameLength || entry.Password.Length > maximumPasswordLength)
            throw new ArgumentException($"Slot {slot}: account names allow 10 characters and passwords allow 20.");
        if (!int.TryParse(entry.AutoLoginPort, out var port) || port < 0 || port > ushort.MaxValue)
            throw new ArgumentException($"Slot {slot}: enter port 0–65535.");
        var encrypted = PasswordProtector.Protect(entry.Password);
        var section = $"Slot.{slot}";
        document.Set(section, "DisplayName", entry.DisplayName);
        document.Set(section, "Username", entry.Username);
        document.Set(section, "EncryptedPassword", encrypted);
        document.Set(section, "AutoLoginPort", port.ToString(System.Globalization.CultureInfo.InvariantCulture));
        document.Set(section, "AutoSelectCharacter", entry.AutoSelectCharacter);
    }

    public void Delete(string name) => File.Delete(PathFor(name));
}
