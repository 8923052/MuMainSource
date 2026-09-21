using System.Collections.ObjectModel;

namespace MuLauncher.Models;

public sealed class LauncherState : ObservableModel
{
    public const string DefaultPreset = "Default · one empty session";
    public ObservableCollection<string> LaunchPresets { get; } = [DefaultPreset];
    public ObservableCollection<string> Presets { get; } = [];
    public ObservableCollection<SessionSlot> Slots { get; } = [];
    public IEnumerable<SessionEntry> Sessions => Slots.Select(slot => slot.Session).OfType<SessionEntry>();
    public ObservableCollection<ChannelStatus> Channels { get; } = [];
    private IReadOnlyList<ConfigChoice> autoLoginChoices = [new("0", "0 - Manual login")];
    public IReadOnlyList<ConfigChoice> AutoLoginChoices { get => autoLoginChoices; private set => Set(ref autoLoginChoices, value); }

    public void RefreshAutoLoginChoices()
    {
        var choices = new List<ConfigChoice> { new("0", "0 - Manual login") };
        foreach (var channel in Channels.Where(channel => channel.Port.HasValue))
        {
            var port = channel.Port!.Value.ToString(System.Globalization.CultureInfo.InvariantCulture);
            if (choices.Any(choice => choice.Value == port)) continue;
            var name = channel.ServerName.Length > 0 ? channel.Name : $"{channel.Host} · {channel.Name}";
            choices.Add(new ConfigChoice(port, $"{name} · {port}"));
        }
        foreach (var port in Sessions.Select(session => session.AutoLoginPort).Distinct())
            if (!choices.Any(choice => choice.Value == port))
                choices.Add(new ConfigChoice(port, $"Port {port} - saved channel, currently unavailable"));
        if (!AutoLoginChoices.SequenceEqual(choices)) AutoLoginChoices = choices;
    }
    private string launchPreset = DefaultPreset, presetName = "", newPresetName = "", mainExecutable = "", initDelay = "0";
    private string status = "Checking server", statusColor = "#EFB866", serverDetail = "Connecting…", notice = "";
    private SessionSlot? selectedSlot;
    private ClientSettings settings = new("");
    private SessionShortcutSettings sessionShortcuts = new();
    private int tab;
    private bool refreshing;
    public string LaunchPreset { get => launchPreset; set => Set(ref launchPreset, value ?? DefaultPreset); }
    public string PresetName { get => presetName; set => Set(ref presetName, value ?? ""); }
    public string NewPresetName { get => newPresetName; set => Set(ref newPresetName, value ?? ""); }
    public string MainExecutable { get => mainExecutable; set => Set(ref mainExecutable, value ?? ""); }
    public string InitDelay { get => initDelay; set => Set(ref initDelay, value ?? "0"); }
    public SessionSlot? SelectedSlot
    {
        get => selectedSlot;
        set
        {
            if (selectedSlot != null) selectedSlot.PropertyChanged -= SelectedSlotChanged;
            Set(ref selectedSlot, value);
            if (selectedSlot != null) selectedSlot.PropertyChanged += SelectedSlotChanged;
            RefreshSelection();
        }
    }
    public SessionEntry? SelectedSession => SelectedSlot?.Session;
    public bool HasSelectedSession => SelectedSession != null;
    public bool SelectedSlotIsEmpty => SelectedSlot != null && SelectedSession == null;
    public bool CanMoveUp => HasSelectedSession && SelectedSlot!.Number > 1;
    public bool CanMoveDown => HasSelectedSession && SelectedSlot!.Number < Slots.Count;
    public bool CanRemoveSession => HasSelectedSession && Sessions.Count() > 1;
    public bool CanDeleteSlot => SelectedSlot != null;

    private void SelectedSlotChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
    {
        if (args.PropertyName == nameof(SessionSlot.Session)) RefreshSelection();
    }

    private void RefreshSelection()
    {
        Changed(nameof(SelectedSession));
        Changed(nameof(HasSelectedSession));
        Changed(nameof(SelectedSlotIsEmpty));
        Changed(nameof(CanMoveUp));
        Changed(nameof(CanMoveDown));
        Changed(nameof(CanRemoveSession));
        Changed(nameof(CanDeleteSlot));
    }
    public ClientSettings Settings { get => settings; set => Set(ref settings, value); }
    public SessionShortcutSettings SessionShortcuts { get => sessionShortcuts; set => Set(ref sessionShortcuts, value); }
    public string Status { get => status; set => Set(ref status, value); }
    public string StatusColor { get => statusColor; set => Set(ref statusColor, value); }
    public string ServerDetail { get => serverDetail; set => Set(ref serverDetail, value); }
    public string Notice { get => notice; set => Set(ref notice, value); }
    public bool Refreshing { get => refreshing; set => Set(ref refreshing, value); }
    public int Tab
    {
        get => tab;
        set { Set(ref tab, value); Changed(nameof(PlayVisible)); Changed(nameof(PresetsVisible)); Changed(nameof(SettingsVisible)); }
    }
    public bool PlayVisible => Tab == 0;
    public bool PresetsVisible => Tab == 1;
    public bool SettingsVisible => Tab == 2;
}
