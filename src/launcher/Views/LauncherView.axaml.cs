using System.Diagnostics;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Markup.Xaml;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using MuLauncher.Models;
using MuLauncher.Services;

namespace MuLauncher.Views;

public sealed partial class LauncherView : UserControl
{
    public LauncherState State { get; } = new();
    private readonly GameFiles files;
    private readonly PresetStore presets;
    private readonly ConnectServerClient server = new();
    private readonly DispatcherTimer refreshTimer = new() { Interval = TimeSpan.FromSeconds(20) };
    private readonly DispatcherTimer endpointTimer = new() { Interval = TimeSpan.FromMilliseconds(600) };
    private CancellationTokenSource? refreshCancellation;
    private bool loading, dirty, compact;
    private string? loadedPreset;
    private SessionShortcutBinding? recordingShortcut;
    private IReadOnlyDictionary<int, string> serverNames = new Dictionary<int, string>();

    public LauncherView() : this(new GameFiles()) { }
    public LauncherView(GameFiles gameFiles, bool monitor = true)
    {
        files = gameFiles;
        presets = new PresetStore(files.ConfigDirectory);
        AvaloniaXamlLoader.Load(this);
        DataContext = State;
        AddHandler(InputElement.KeyDownEvent, ShortcutKeyDown, RoutingStrategies.Tunnel, handledEventsToo: true);
        AddHandler(InputElement.KeyUpEvent, ShortcutKeyUp, RoutingStrategies.Tunnel, handledEventsToo: true);
        Run(LoadFiles);
        State.PropertyChanged += (_, args) =>
        {
            if (loading) return;
            if (args.PropertyName == nameof(State.LaunchPreset)) Run(SaveLaunchPreset);
            if (args.PropertyName is nameof(State.MainExecutable) or nameof(State.InitDelay))
                MarkPresetDirty();
        };
        State.Channels.CollectionChanged += (_, _) => RefreshChannelChoices();
        refreshTimer.Tick += async (_, _) => await RefreshAsync(false);
        endpointTimer.Tick += async (_, _) => { endpointTimer.Stop(); await RefreshAsync(true); };
        SizeChanged += (_, _) => ApplyLayout();
        AttachedToVisualTree += async (_, _) =>
        {
            if (!monitor) return;
            refreshTimer.Start();
            await RefreshAsync(true);
        };
        DetachedFromVisualTree += (_, _) =>
        {
            refreshTimer.Stop();
            endpointTimer.Stop();
            refreshCancellation?.Cancel();
        };
    }

    private void LoadFiles()
    {
        files.EnsureConfig();
        State.Settings.EndpointChanged -= EndpointEdited;
        State.Settings = new ClientSettings(File.ReadAllText(files.ConfigPath), files.DirectoryPath);
        State.Settings.EndpointChanged += EndpointEdited;
        serverNames = ServerNames.Load(files.DirectoryPath);
        loadedPreset = null;
        dirty = false;
        loading = true;
        RefreshPresets(launchSelection: State.Settings.LastPreset);
    }

    private void RefreshPresets(string? selected = null, string? launchSelection = null)
    {
        loading = true;
        var launch = launchSelection ?? State.LaunchPreset;
        var names = presets.List();
        if (!names.Contains(State.LaunchPreset)) State.LaunchPreset = LauncherState.DefaultPreset;
        State.Presets.Clear();
        foreach (var name in State.LaunchPresets.Skip(1).Except(names).ToArray()) State.LaunchPresets.Remove(name);
        foreach (var name in names)
        {
            State.Presets.Add(name);
            if (!State.LaunchPresets.Contains(name)) State.LaunchPresets.Add(name);
        }
        State.LaunchPreset = names.Contains(launch) ? launch : LauncherState.DefaultPreset;
        State.PresetName = selected ?? State.Presets.FirstOrDefault() ?? "";
        loading = false;
        LoadPreset();
        SaveLaunchPreset();
    }

    private void LoadPreset()
    {
        if (loading) return;
        SaveEdits();
        var entries = string.IsNullOrWhiteSpace(State.PresetName) ? [] : presets.Load(State.PresetName);
        var selectedNumber = loadedPreset == State.PresetName ? State.SelectedSlot?.Number : null;
        loading = true;
        State.SelectedSlot = null;
        State.Slots.Clear();
        foreach (var entry in entries) AddSlot(entry);
        State.MainExecutable = string.IsNullOrWhiteSpace(State.PresetName)
            ? PresetStore.DefaultMain : presets.ReadMain(State.PresetName);
        State.InitDelay = string.IsNullOrWhiteSpace(State.PresetName)
            ? "0" : presets.ReadInitDelay(State.PresetName);
        State.SessionShortcuts = string.IsNullOrWhiteSpace(State.PresetName)
            ? new SessionShortcutSettings() : presets.ReadShortcuts(State.PresetName);
        foreach (var binding in State.SessionShortcuts.Bindings)
            binding.PropertyChanged += (_, args) =>
            {
                if (args.PropertyName == nameof(SessionShortcutBinding.KeyCodes)) MarkPresetDirty();
            };
        State.RefreshAutoLoginChoices();
        State.SelectedSlot = State.Slots.FirstOrDefault(slot => slot.Number == selectedNumber)
            ?? State.Slots.FirstOrDefault(slot => slot.HasSession);
        loadedPreset = State.PresetName;
        dirty = false;
        loading = false;
    }

    private void RefreshChannelChoices()
    {
        State.RefreshAutoLoginChoices();
        this.FindControl<ComboBox>("AutoLoginPicker")!.SelectedValue = State.SelectedSession?.AutoLoginPort;
    }

    private void AddSlot(SessionSlot slot)
    {
        slot.PropertyChanged += (_, _) =>
        {
            if (loading) return;
            MarkPresetDirty();
        };
        State.Slots.Add(slot);
    }

    private void MarkPresetDirty()
    {
        if (string.IsNullOrWhiteSpace(loadedPreset)) return;
        dirty = true;
        State.Notice = "Unsaved preset changes.";
    }

    private void SaveEdits()
    {
        if (!dirty || string.IsNullOrWhiteSpace(loadedPreset)) return;
        presets.Save(loadedPreset, State.Slots, State.MainExecutable, State.InitDelay, State.SessionShortcuts);
        dirty = false;
    }

    public bool SaveBeforeExit()
    {
        try { SaveEdits(); return true; }
        catch (Exception error) { State.Notice = error.Message; return false; }
    }

    private void EndpointEdited()
    {
        refreshCancellation?.Cancel();
        State.Channels.Clear();
        endpointTimer.Stop();
        endpointTimer.Start();
    }

    public async Task RefreshAsync(bool force)
    {
        if (State.Refreshing && !force) return;
        refreshCancellation?.Cancel();
        using var cancellation = new CancellationTokenSource();
        refreshCancellation = cancellation;
        State.Refreshing = true;
        var host = State.Settings.Host.Trim();
        try
        {
            var port = State.Settings.ValidPort();
            var snapshot = await server.QueryAsync(host, port, cancellation.Token);
            if (cancellation.IsCancellationRequested) return;
            State.Channels.Clear();
            foreach (var channel in snapshot.Channels)
            {
                serverNames.TryGetValue(channel.Id / ChannelStatus.ChannelsPerServer, out var name);
                State.Channels.Add(channel with { ServerName = name ?? "" });
            }
            State.Status = "Online";
            State.StatusColor = "#77D9B4";
            State.ServerDetail = $"{host}:{port}  ·  {snapshot.LatencyMilliseconds} ms  ·  Checked {DateTime.Now:t}";
        }
        catch (OperationCanceledException) when (cancellation.IsCancellationRequested) { }
        catch (Exception error)
        {
            State.Status = "Offline / unavailable";
            State.StatusColor = "#F09591";
            State.ServerDetail = error is OperationCanceledException ? $"{host} · Request timed out" : error.Message;
            for (var index = 0; index < State.Channels.Count; index++)
                State.Channels[index] = State.Channels[index] with { Load = 128 };
        }
        finally
        {
            if (refreshCancellation == cancellation) { refreshCancellation = null; State.Refreshing = false; }
        }
    }

    private void Run(Action action)
    {
        try { action(); }
        catch (Exception error) { loading = false; State.Notice = error.Message; }
    }

    private void PlayTab(object? sender, RoutedEventArgs e) => State.Tab = 0;
    private void PresetsTab(object? sender, RoutedEventArgs e) => State.Tab = 1;
    private void SettingsTab(object? sender, RoutedEventArgs e) => State.Tab = 2;
    private async void RefreshClick(object? sender, RoutedEventArgs e) => await RefreshAsync(true);
    private void PresetSelected(object? sender, SelectionChangedEventArgs e)
    {
        if (loading) return;
        try { LoadPreset(); }
        catch (Exception error)
        {
            loading = true;
            State.PresetName = loadedPreset ?? "";
            loading = false;
            State.Notice = error.Message;
        }
    }

    private void CreatePreset(object? sender, RoutedEventArgs e) => Run(() =>
    {
        var name = State.NewPresetName.Trim();
        if (File.Exists(presets.PathFor(name))) throw new ArgumentException("That preset already exists.");
        SaveEdits();
        presets.Save(name, [new SessionSlot(1, new SessionEntry())]);
        State.NewPresetName = "";
        RefreshPresets(name);
        State.Notice = $"Created configs/{name}.ini";
    });

    private void DeletePreset(object? sender, RoutedEventArgs e) => Run(() =>
    {
        if (string.IsNullOrWhiteSpace(loadedPreset)) return;
        var name = loadedPreset;
        presets.Delete(name);
        loadedPreset = null;
        dirty = false;
        RefreshPresets();
        State.Notice = $"Deleted {name}.ini";
    });

    private SessionEntry NewSession()
    {
        var sessionNumber = State.Sessions.Count() + 1;
        while (State.Sessions.Any(session => session.DisplayName == $"Session {sessionNumber}")) sessionNumber++;
        return new SessionEntry { DisplayName = $"Session {sessionNumber}" };
    }

    private void CreateSessionInSlot(object? sender, RoutedEventArgs e) => Run(() =>
    {
        if (!State.SelectedSlotIsEmpty) return;
        State.SelectedSlot!.Session = NewSession();
        State.Notice = $"Created {State.SelectedSession!.DisplayName} in slot {State.SelectedSlot.Number}.";
    });

    private void AddSession(object? sender, RoutedEventArgs e) => Run(() =>
    {
        if (string.IsNullOrWhiteSpace(loadedPreset)) throw new ArgumentException("Create or select a preset first.");
        var slot = new SessionSlot(State.Slots.Count + 1, NewSession());
        AddSlot(slot);
        State.SelectedSlot = slot;
        dirty = true;
        State.Notice = $"Added {slot.Session!.DisplayName} in slot {slot.Number}.";
    });

    private void DeleteSlot(object? sender, RoutedEventArgs e) => Run(() =>
    {
        var selected = State.SelectedSlot;
        if (selected == null) return;
        var index = selected.Number - 1;
        State.SelectedSlot = null;
        selected.Session = null;
        State.Slots.RemoveAt(index);
        for (var position = index; position < State.Slots.Count; position++)
            State.Slots[position].Number = position + 1;
        State.SelectedSlot = State.Slots.ElementAtOrDefault(Math.Min(index, State.Slots.Count - 1));
        dirty = true;
        State.Notice = $"Deleted slot {index + 1}.";
    });

    private void RemoveSession(object? sender, RoutedEventArgs e) => Run(() =>
    {
        if (State.SelectedSession == null) return;
        if (State.Sessions.Count() == 1) throw new ArgumentException("Keep at least one session in a preset.");
        State.SelectedSlot!.Session = null;
        dirty = true;
        State.Notice = $"Slot {State.SelectedSlot.Number} is now empty.";
    });

    private void SyncAutoLogin(object? sender, RoutedEventArgs e) => Run(() =>
    {
        var port = State.SelectedSession?.AutoLoginPort;
        if (port == null) return;
        foreach (var session in State.Sessions) session.AutoLoginPort = port;
        State.Notice = "Applied the selected login channel to all sessions.";
    });

    private void MoveSessionUp(object? sender, RoutedEventArgs e) => Run(() => MoveSession(-1));
    private void MoveSessionDown(object? sender, RoutedEventArgs e) => Run(() => MoveSession(1));

    private void MoveSession(int direction)
    {
        var source = State.SelectedSlot;
        if (source?.Session == null) return;
        var destinationIndex = source.Number - 1 + direction;
        if (destinationIndex < 0 || destinationIndex >= State.Slots.Count) return;
        var destination = State.Slots[destinationIndex];
        var movingSession = source.Session;
        source.Session = destination.Session;
        destination.Session = movingSession;
        State.SelectedSlot = destination;
        State.Notice = $"Moved {movingSession.DisplayName} to slot {destination.Number}.";
    }

    private void SavePreset(object? sender, RoutedEventArgs e) => Run(() =>
    {
        if (string.IsNullOrWhiteSpace(loadedPreset)) throw new ArgumentException("Create or select a preset first.");
        presets.Save(loadedPreset, State.Slots, State.MainExecutable, State.InitDelay, State.SessionShortcuts);
        dirty = false;
        LoadPreset();
        State.Notice = $"Saved configs/{loadedPreset}.ini";
    });

    private void SaveAndStartPreset(object? sender, RoutedEventArgs e) => Run(() =>
    {
        if (string.IsNullOrWhiteSpace(loadedPreset)) throw new ArgumentException("Create or select a preset first.");
        presets.Save(loadedPreset, State.Slots, State.MainExecutable, State.InitDelay, State.SessionShortcuts);
        dirty = false;
        LaunchGame(presets.PathFor(loadedPreset), false);
    });

    private void SaveSettings(object? sender, RoutedEventArgs e) => Run(() =>
    {
        State.Settings.Save(files.ConfigPath);
        State.Notice = "Saved configs/config.ini. Settings apply on the next game launch.";
    });

    private void StartGame(object? sender, RoutedEventArgs e) => Run(() =>
    {
        SaveEdits();
        var preset = State.LaunchPreset == LauncherState.DefaultPreset ? null : presets.PathFor(State.LaunchPreset);
        LaunchGame(preset);
    });

    private void LaunchGame(string? preset, bool saveSettings = true)
    {
        if (saveSettings) State.Settings.Save(files.ConfigPath);
        var main = preset == null ? PresetStore.DefaultMain : presets.ReadMain(Path.GetFileNameWithoutExtension(preset));
        using var process = Process.Start(files.CreateStartInfo(preset, main));
        State.Notice = "Game started. You can keep the launcher open or exit.";
    }

    private void SaveLaunchPreset()
    {
        State.Settings.LastPreset = State.LaunchPreset;
        State.Settings.Save(files.ConfigPath);
    }

    private void RecordShortcut(object? sender, RoutedEventArgs e)
    {
        if ((sender as Control)?.DataContext is not SessionShortcutBinding binding) return;
        CancelShortcutRecording();
        recordingShortcut = binding;
        binding.StartRecording();
        State.Notice = $"Recording {binding.Name}: press the shortcut keys, then release all keys.";
    }

    private void ResetShortcut(object? sender, RoutedEventArgs e)
    {
        if ((sender as Control)?.DataContext is not SessionShortcutBinding binding) return;
        if (ReferenceEquals(recordingShortcut, binding)) recordingShortcut = null;
        binding.ResetToDefault();
        State.Notice = $"Reset {binding.Name} to {binding.Display}.";
    }

    private void ShortcutKeyDown(object? sender, KeyEventArgs e)
    {
        var binding = recordingShortcut;
        if (binding == null || !binding.CaptureKeyDown(e.PhysicalKey)) return;
        e.Handled = true;
        if (!binding.IsRecording) FinishShortcutRecording(binding);
    }

    private void ShortcutKeyUp(object? sender, KeyEventArgs e)
    {
        var binding = recordingShortcut;
        if (binding == null || !binding.CaptureKeyUp(e.PhysicalKey)) return;
        e.Handled = true;
        if (!binding.IsRecording) FinishShortcutRecording(binding);
    }

    private void FinishShortcutRecording(SessionShortcutBinding binding)
    {
        if (ReferenceEquals(recordingShortcut, binding)) recordingShortcut = null;
        State.Notice = binding.CaptureError.Length > 0
            ? binding.CaptureError : $"Recorded {binding.Display} for {binding.Name}.";
    }

    private void CancelShortcutRecording()
    {
        recordingShortcut?.CancelRecording();
        recordingShortcut = null;
    }

    private void ExitLauncher(object? sender, RoutedEventArgs e)
    {
        if (SaveBeforeExit() && TopLevel.GetTopLevel(this) is Window window) window.Close();
    }

    private async void ChoosePresetMain(object? sender, RoutedEventArgs e)
    {
        try
        {
            var provider = TopLevel.GetTopLevel(this)?.StorageProvider;
            if (provider == null) return;
            var selected = await provider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = "Select Main.exe for this preset", AllowMultiple = false,
                FileTypeFilter = [new FilePickerFileType("Windows executable") { Patterns = ["*.exe"] }]
            });
            var path = selected.FirstOrDefault()?.TryGetLocalPath();
            if (path == null) return;
            State.MainExecutable = Path.GetDirectoryName(path) == files.DirectoryPath
                ? "./" + Path.GetFileName(path) : path;
        }
        catch (Exception error) { State.Notice = error.Message; }
    }

    private void ApplyLayout()
    {
        var narrow = Bounds.Width < 850;
        if (narrow == compact) return;
        compact = narrow;
        var shell = this.FindControl<Grid>("Shell")!;
        var sidebar = this.FindControl<Border>("Sidebar")!;
        var workspace = this.FindControl<Grid>("Workspace")!;
        shell.ColumnDefinitions = new ColumnDefinitions(narrow ? "*" : "190,*");
        shell.RowDefinitions = new RowDefinitions(narrow ? "Auto,*" : "*");
        Grid.SetColumn(workspace, narrow ? 0 : 1);
        Grid.SetRow(workspace, narrow ? 1 : 0);
        sidebar.Padding = narrow ? new Thickness(12, 8) : new Thickness(18, 28);
        workspace.Margin = narrow ? new Thickness(18) : new Thickness(32, 28);
        this.FindControl<StackPanel>("Logo")!.IsVisible = !narrow;
        this.FindControl<StackPanel>("SidebarFooter")!.IsVisible = !narrow;
        this.FindControl<StackPanel>("Navigation")!.Orientation = narrow ? Orientation.Horizontal : Orientation.Vertical;
        var editor = this.FindControl<Grid>("PresetEditor")!;
        editor.ColumnDefinitions = new ColumnDefinitions(narrow ? "*" : "240,*");
        editor.RowDefinitions = new RowDefinitions(narrow ? "Auto,*" : "*");
        var fields = this.FindControl<Grid>("SessionEditorArea")!;
        Grid.SetColumn(fields, narrow ? 0 : 1);
        Grid.SetRow(fields, narrow ? 1 : 0);
    }
}
