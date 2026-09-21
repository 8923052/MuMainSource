using MuLauncher.Services;
using System.Buffers.Binary;
using System.Globalization;
using System.Text;

namespace MuLauncher.Models;

public sealed class ClientSettings : ObservableModel
{
    private static readonly string[] SupportedLocales =
        ["en", "de", "es", "id", "ja", "pl", "pt", "ru", "tl", "uk", "zh-TW"];

    // Match the choices in NewUIOptionWindow.cpp.
    public IReadOnlyList<string> Resolutions { get; } = ["640 x 480", "800 x 600", "1024 x 768", "1280 x 720", "1280 x 1024", "1600 x 900", "1600 x 1200", "1680 x 1050", "1920 x 1080", "2560 x 1440"];
    public IReadOnlyList<ConfigChoice> Languages { get; }
    public IReadOnlyList<ConfigChoice> Fonts { get; }
    public IReadOnlyList<ConfigChoice> RendererBackends { get; } = OperatingSystem.IsWindows()
        ? [new("auto", "Auto"), new("direct3d12", "Direct3D 12"), new("vulkan", "Vulkan")]
        : OperatingSystem.IsMacOS()
            ? [new("auto", "Auto"), new("metal", "Metal")]
            : [new("auto", "Auto"), new("vulkan", "Vulkan")];
    private IniDocument document;
    public event Action? EndpointChanged;
    public ClientSettings(string text, string? clientDirectory = null)
    {
        Languages = LanguageChoices(SupportedLocales);
        Fonts = FindFonts(clientDirectory);
        document = new IniDocument(text);
        NormalizePerformanceSettings();
        var controlScale = document.Get("UI", "ControlUIScale",
            document.Get("Workspace", "ControlUIScale", "120"));
        document.Set("UI", "ControlUIScale", InRange(controlScale, 100, 200) ? controlScale : "120");
        var rmlScale = document.Get("UI", "RmlScale", "160");
        if (double.TryParse(rmlScale, System.Globalization.NumberStyles.Float,
            System.Globalization.CultureInfo.InvariantCulture, out var legacyScale) && legacyScale <= 3)
            rmlScale = Math.Round(legacyScale * 100).ToString(System.Globalization.CultureInfo.InvariantCulture);
        document.Set("UI", "RmlScale", InRange(rmlScale, 100, 200) ? rmlScale : "160");
        document.Remove("Performance", "LegacySimulationFps");
        document.Remove("Performance", "LegacyReferenceFps");
        document.RemoveSection("Workspace");
        document.RemoveSection("Graphics");
        document.RemoveSection("Graphic");
    }
    public string Host { get => document.Get("Network", "DefaultConnectServerIP", "127.0.0.1"); set => Write("Network", "DefaultConnectServerIP", value, true); }
    public string Port { get => document.Get("Network", "DefaultConnectServerPort", "44406"); set => Write("Network", "DefaultConnectServerPort", value, true); }
    public string Workers { get => document.Get("Performance", "SessionWorkerCount", "8"); set => Write("Performance", "SessionWorkerCount", value); }
    public bool VSync { get => document.GetInt("Performance", "VSync", 1) != 0; set => Write("Performance", "VSync", value ? "1" : "0"); }
    public string FpsLimit { get => document.Get("Performance", "FpsLimit", "0"); set => Write("Performance", "FpsLimit", value); }
    public string RendererBackend { get => document.Get("Performance", "RendererBackend", "auto"); set { if (value != null) Write("Performance", "RendererBackend", value); } }
    public string Width { get => document.Get("Window", "Width", "1024"); set => Write("Window", "Width", value); }
    public string Height { get => document.Get("Window", "Height", "768"); set => Write("Window", "Height", value); }
    public string Resolution
    {
        get => $"{Width} x {Height}";
        set
        {
            if (value == null || value == Resolution) return;
            var dimensions = value.Split(" x ");
            document.Set("Window", "Width", dimensions[0]);
            document.Set("Window", "Height", dimensions[1]);
            Changed(null);
        }
    }
    public string Language { get => document.Get("Localization", "UILocale", "en"); set { if (value != null) Write("Localization", "UILocale", value); } }
    public string Font { get => document.Get("UI", "Font"); set => Write("UI", "Font", value); }
    public int RmlScale { get => document.GetInt("UI", "RmlScale", 160); set => Write("UI", "RmlScale", value.ToString(System.Globalization.CultureInfo.InvariantCulture)); }
    public int ControlUiScale { get => document.GetInt("UI", "ControlUIScale", 120); set => Write("UI", "ControlUIScale", value.ToString(System.Globalization.CultureInfo.InvariantCulture)); }
    public string LastPreset { get => document.Get("Launcher", "LastPreset"); set => Write("Launcher", "LastPreset", value); }
    public bool Windowed { get => document.GetInt("Window", "Windowed", 1) != 0; set => Write("Window", "Windowed", value ? "1" : "0"); }
    public string Sound { get => document.Get("Audio", "MasterSoundVolume", "5"); set => Write("Audio", "MasterSoundVolume", value); }
    public string Music { get => document.Get("Audio", "MasterMusicVolume", "5"); set => Write("Audio", "MasterMusicVolume", value); }
    public int SoundLevel { get => document.GetInt("Audio", "MasterSoundVolume", 5); set => Sound = value.ToString(System.Globalization.CultureInfo.InvariantCulture); }
    public int MusicLevel { get => document.GetInt("Audio", "MasterMusicVolume", 5); set => Music = value.ToString(System.Globalization.CultureInfo.InvariantCulture); }
    public string RawText
    {
        get => document.ToString();
        set
        {
            if (value == RawText) return;
            var previousEndpoint = (Host, Port);
            document = new IniDocument(value ?? "");
            Changed(null);
            if (previousEndpoint != (Host, Port)) EndpointChanged?.Invoke();
        }
    }

    private void NormalizePerformanceSettings()
    {
        var workers = document.GetInt("Performance", "SessionWorkerCount", 8);
        if (workers < 2 || workers > 64) document.Set("Performance", "SessionWorkerCount", "8");
        if (!TryFpsLimit(FpsLimit, out _)) FpsLimit = "0";
        if (document.Get("Performance", "VSync", "1") is not ("0" or "1")) VSync = true;
        if (!RendererBackends.Any(choice => choice.Value == RendererBackend)) RendererBackend = "auto";
    }

    private static bool TryFpsLimit(string text, out float value) =>
        float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out value)
        && float.IsFinite(value) && value >= 0;

    private void Write(string section, string key, string value, bool endpoint = false)
    {
        value ??= "";
        if (document.Get(section, key) == value) return;
        document.Set(section, key, value);
        Changed(null);
        if (endpoint) EndpointChanged?.Invoke();
    }

    public int ValidPort() => Number(Port, "Connect-server port", 1, 65535);

    public void Save(string path)
    {
        if (string.IsNullOrWhiteSpace(Host)) throw new ArgumentException("Enter the connect-server address.");
        ValidPort();
        Number(Workers, "Session workers", 2, 64);
        if (!TryFpsLimit(FpsLimit, out var fpsLimit))
            throw new ArgumentException("FPS limit: enter a positive number, or 0 for unlimited.");
        document.Set("Performance", "FpsLimit", fpsLimit.ToString("G9", CultureInfo.InvariantCulture));
        document.Set("Performance", "VSync", VSync ? "1" : "0");
        if (!RendererBackends.Any(choice => choice.Value == RendererBackend))
            throw new ArgumentException("Choose a renderer backend from the list.");
        document.Set("Performance", "RendererBackend", RendererBackend);
        Number(Width, "Window width", 640, int.MaxValue);
        Number(Height, "Window height", 480, int.MaxValue);
        Number(Sound, "Sound volume", 0, 10);
        Number(Music, "Music volume", 0, 10);
        Number(RmlScale.ToString(), "RmlUi scale", 100, 200);
        Number(ControlUiScale.ToString(), "Control UI scale", 100, 200);
        document.Remove("Performance", "LegacySimulationFps");
        document.Remove("Performance", "LegacyReferenceFps");
        document.RemoveSection("Workspace");
        document.RemoveSection("Graphics");
        document.RemoveSection("Graphic");
        document.Save(path);
        Changed(null);
    }

    private static int Number(string text, string name, int minimum, int maximum)
    {
        if (!int.TryParse(text, out var value) || value < minimum || value > maximum)
            throw new ArgumentException($"{name}: enter {minimum}–{maximum}.");
        return value;
    }

    private static bool InRange(string text, int minimum, int maximum) =>
        int.TryParse(text, out var value) && value >= minimum && value <= maximum;

    private static IReadOnlyList<ConfigChoice> LanguageChoices(IEnumerable<string> locales) =>
        locales.Where(code => code.Length > 0).Distinct(StringComparer.OrdinalIgnoreCase)
            .OrderBy(code => code.Equals("en", StringComparison.OrdinalIgnoreCase) ? "" : code)
            .Select(code => new ConfigChoice(code, NativeLanguageName(code))).ToArray();

    private static string NativeLanguageName(string code)
    {
        try { return CultureInfo.GetCultureInfo(code).NativeName; }
        catch (CultureNotFoundException) { return code; }
    }

    private static IReadOnlyList<ConfigChoice> FindFonts(string? clientDirectory)
    {
        var choices = new List<ConfigChoice> { new("", "Default") };
        var directory = FindDirectory(clientDirectory, "fonts");
        if (directory == null) return choices;
        choices.AddRange(Directory.EnumerateFiles(directory)
            .Where(path => Path.GetFileName(path).EndsWith("-Regular.ttf", StringComparison.Ordinal))
            .Select(path => (Path: path, Family: ReadFontFamily(path)))
            .Where(font => font.Family.Length > 0
                && Path.GetFileName(font.Path) == font.Family + "-Regular.ttf")
            .Select(font => font.Family)
            .Distinct(StringComparer.OrdinalIgnoreCase).Order(StringComparer.OrdinalIgnoreCase)
            .Select(name => new ConfigChoice(name, name)));
        return choices;
    }

    private static string? FindDirectory(string? start, params string[] relatives)
    {
        for (var directory = start == null ? null : new DirectoryInfo(start);
             directory != null; directory = directory.Parent)
            foreach (var relative in relatives)
            {
                var candidate = Path.Combine(directory.FullName, relative);
                if (Directory.Exists(candidate)) return candidate;
            }
        return null;
    }

    private static string ReadFontFamily(string path)
    {
        try
        {
            var data = File.ReadAllBytes(path);
            var tables = BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(4));
            for (var index = 0; index < tables; index++)
            {
                var record = 12 + index * 16;
                if (!data.AsSpan(record, 4).SequenceEqual("name"u8)) continue;
                return ReadNameTable(data, checked((int)BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(record + 8))));
            }
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException
            or ArgumentOutOfRangeException or OverflowException) { }
        return "";
    }

    private static string ReadNameTable(byte[] data, int table)
    {
        var count = BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(table + 2));
        var strings = table + BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(table + 4));
        string fallback = "";
        for (var index = 0; index < count; index++)
        {
            var record = table + 6 + index * 12;
            if (BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(record)) != 3
                || BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(record + 6)) != 1) continue;
            var language = BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(record + 4));
            var length = BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(record + 8));
            var offset = strings + BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(record + 10));
            var name = Encoding.BigEndianUnicode.GetString(data, offset, length);
            if (language == 0x0409) return name;
            if (fallback.Length == 0) fallback = name;
        }
        return fallback;
    }
}
