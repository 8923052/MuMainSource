using System.Diagnostics;

namespace MuLauncher.Services;

public sealed class GameFiles
{
    public string DirectoryPath { get; }
    public string ConfigDirectory => Path.Combine(DirectoryPath, "configs");
    public string ConfigPath => Path.Combine(ConfigDirectory, "config.ini");
    private string LegacyLauncherConfigPath => Path.Combine(ConfigDirectory, "launcher.ini");
    private string LegacyLastPresetPath => Path.Combine(ConfigDirectory, "last-preset.txt");
    public string Executable => Path.Combine(DirectoryPath, OperatingSystem.IsWindows() ? "Main.exe" : "Main");

    public GameFiles(string? directory = null) =>
        DirectoryPath = Path.GetFullPath(directory ?? AppContext.BaseDirectory);

    public string? ReadLastPreset() => File.Exists(ConfigPath)
        ? new IniDocument(File.ReadAllText(ConfigPath)).Get("Launcher", "LastPreset") : null;

    public void SaveLastPreset(string name)
    {
        var settings = new IniDocument(File.Exists(ConfigPath) ? File.ReadAllText(ConfigPath) : "");
        settings.Set("Launcher", "LastPreset", name);
        settings.Save(ConfigPath);
        if (File.Exists(LegacyLauncherConfigPath)) File.Delete(LegacyLauncherConfigPath);
        if (File.Exists(LegacyLastPresetPath)) File.Delete(LegacyLastPresetPath);
    }

    public void EnsureConfig()
    {
        Directory.CreateDirectory(ConfigDirectory);
        if (File.Exists(ConfigPath)) { MigrateLauncherPreferences(); return; }
        var old = Path.Combine(DirectoryPath, "config.ini");
        if (File.Exists(old)) { File.Move(old, ConfigPath); MigrateLauncherPreferences(); return; }
        using (var template = typeof(GameFiles).Assembly.GetManifestResourceStream("config.ini.template")!)
        using (var output = File.Create(ConfigPath)) template.CopyTo(output);
        MigrateLauncherPreferences();
    }

    private void MigrateLauncherPreferences()
    {
        var current = new IniDocument(File.ReadAllText(ConfigPath)).Get("Launcher", "LastPreset");
        var lastPreset = current.Length > 0 ? "" : File.Exists(LegacyLauncherConfigPath)
            ? new IniDocument(File.ReadAllText(LegacyLauncherConfigPath)).Get("Launcher", "LastPreset")
            : File.Exists(LegacyLastPresetPath) ? File.ReadAllText(LegacyLastPresetPath) : "";
        if (lastPreset.Length > 0) SaveLastPreset(lastPreset);
        if (File.Exists(LegacyLauncherConfigPath)) File.Delete(LegacyLauncherConfigPath);
        if (File.Exists(LegacyLastPresetPath)) File.Delete(LegacyLastPresetPath);
    }

    public ProcessStartInfo CreateStartInfo(string? preset, string main = PresetStore.DefaultMain)
    {
        if (string.IsNullOrWhiteSpace(main)) throw new ArgumentException("Select Main.exe for this session preset.");
        var executable = Path.GetFullPath(main, DirectoryPath);
        if (!File.Exists(executable)) throw new FileNotFoundException("Select an existing Main.exe in the session preset.", executable);
        var info = new ProcessStartInfo(executable) { WorkingDirectory = Path.GetDirectoryName(executable)!, UseShellExecute = false };
        if (preset != null)
        {
            info.ArgumentList.Add("--session-config");
            info.ArgumentList.Add(Path.GetFullPath(preset));
        }
        return info;
    }
}
