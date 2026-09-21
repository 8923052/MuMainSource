using Avalonia.Input;
using System.Globalization;

namespace MuLauncher.Models;

public sealed class SessionShortcutSettings
{
    public IReadOnlyList<SessionShortcutBinding> Bindings { get; } =
    [
        new("Hide or show control bar", "ControlBarShortcut", "-2,98"),
        new("View session slot 1", "Slot1Shortcut", "-2,89"),
        new("View session slot 2", "Slot2Shortcut", "-2,90"),
        new("View session slot 3", "Slot3Shortcut", "-2,91"),
        new("View session slot 4", "Slot4Shortcut", "-2,92"),
        new("View session slot 5", "Slot5Shortcut", "-2,93"),
        new("View session slot 6", "Slot6Shortcut", "-2,94"),
        new("View session slot 7", "Slot7Shortcut", "-2,95"),
        new("View session slot 8", "Slot8Shortcut", "-2,96"),
        new("View session slot 9", "Slot9Shortcut", "-2,97"),
    ];
}

public sealed class SessionShortcutBinding : ObservableModel
{
    private const int ControlModifier = -1;
    private const int ShiftModifier = -2;
    private const int AltModifier = -3;
    private const int MetaModifier = -4;
    private const int ScancodeA = 4;
    private const int ScancodeDigit1 = 30;
    private const int ScancodeDigit0 = 39;
    private const int ScancodeF1 = 58;
    private const int ScancodeF13 = 104;
    private const int ScancodeKeypad1 = 89;
    private const int ScancodeKeypad0 = 98;
    private const int ScancodeKeypadDecimal = 99;
    private const int ScancodeInternationalBackslash = 100;
    private const int ScancodeKeypadEquals = 103;
    private const int ScancodeHelp = 117;
    private const int ScancodeSelect = 119;
    private const int ScancodeAgain = 121;
    private const int ScancodeUndo = 122;
    private const int ScancodeCut = 123;
    private const int ScancodeCopy = 124;
    private const int ScancodePaste = 125;
    private const int ScancodeFind = 126;
    private const int ScancodeMute = 127;
    private const int ScancodeVolumeUp = 128;
    private const int ScancodeVolumeDown = 129;
    private const int ScancodeInternational1 = 135;
    private const int ScancodeInternational3 = 137;
    private const int ScancodeInternational4 = 138;
    private const int ScancodeInternational5 = 139;
    private const int ScancodeLang1 = 144;
    private const int ScancodeLang2 = 145;
    private const int ScancodeLang3 = 146;
    private const int ScancodeLang4 = 147;
    private const int ScancodeLang5 = 148;
    private const int ScancodePower = 102;
    private const int ScancodeSleep = 258;
    private const int ScancodeWake = 259;
    private const int ScancodeMediaPlayPause = 271;
    private const int ScancodeMediaSelect = 272;
    private const int ScancodeMediaOpen = 274;
    private const int ScancodeMediaProperties = 279;
    private const int ScancodeBrowserSearch = 280;
    private const int ScancodeBrowserHome = 281;
    private const int ScancodeBrowserBack = 282;
    private const int ScancodeBrowserForward = 283;
    private const int ScancodeBrowserStop = 284;
    private const int ScancodeBrowserRefresh = 285;
    private const int ScancodeBrowserFavorites = 286;

    private static readonly Dictionary<string, int> SpecialScancodes = new(StringComparer.Ordinal)
    {
        ["Backquote"] = 53, ["Backslash"] = 49, ["BracketLeft"] = 47, ["BracketRight"] = 48,
        ["Comma"] = 54, ["Equal"] = 46, ["IntlBackslash"] = ScancodeInternationalBackslash,
        ["IntlRo"] = ScancodeInternational1, ["IntlYen"] = ScancodeInternational3,
        ["Minus"] = 45, ["Period"] = 55, ["Quote"] = 52, ["Semicolon"] = 51, ["Slash"] = 56,
        ["AltLeft"] = AltModifier, ["AltRight"] = AltModifier,
        ["Backspace"] = 42, ["CapsLock"] = 57, ["ContextMenu"] = 101,
        ["ControlLeft"] = ControlModifier, ["ControlRight"] = ControlModifier,
        ["Enter"] = 40, ["MetaLeft"] = MetaModifier, ["MetaRight"] = MetaModifier,
        ["ShiftLeft"] = ShiftModifier, ["ShiftRight"] = ShiftModifier,
        ["Space"] = 44, ["Tab"] = 43, ["Convert"] = ScancodeInternational4,
        ["KanaMode"] = ScancodeLang1, ["Lang1"] = ScancodeLang1, ["Lang2"] = ScancodeLang2,
        ["Lang3"] = ScancodeLang3, ["Lang4"] = ScancodeLang4, ["Lang5"] = ScancodeLang5,
        ["NonConvert"] = ScancodeInternational5, ["Delete"] = 76, ["End"] = 77,
        ["Help"] = ScancodeHelp, ["Home"] = 74, ["Insert"] = 73, ["PageDown"] = 78,
        ["PageUp"] = 75, ["ArrowDown"] = 81, ["ArrowLeft"] = 80,
        ["ArrowRight"] = 79, ["ArrowUp"] = 82, ["NumLock"] = 83,
        ["NumPadAdd"] = 87, ["NumPadClear"] = 156, ["NumPadComma"] = 133,
        ["NumPadDecimal"] = ScancodeKeypadDecimal, ["NumPadDivide"] = 84,
        ["NumPadEnter"] = 88, ["NumPadEqual"] = ScancodeKeypadEquals,
        ["NumPadMultiply"] = 85, ["NumPadParenLeft"] = 182, ["NumPadParenRight"] = 183,
        ["NumPadSubtract"] = 86, ["Escape"] = 41, ["PrintScreen"] = 70,
        ["ScrollLock"] = 71, ["Pause"] = 72,
        ["BrowserBack"] = ScancodeBrowserBack, ["BrowserFavorites"] = ScancodeBrowserFavorites,
        ["BrowserForward"] = ScancodeBrowserForward, ["BrowserHome"] = ScancodeBrowserHome,
        ["BrowserRefresh"] = ScancodeBrowserRefresh, ["BrowserSearch"] = ScancodeBrowserSearch,
        ["BrowserStop"] = ScancodeBrowserStop, ["Eject"] = 270,
        ["MediaPlayPause"] = ScancodeMediaPlayPause, ["MediaSelect"] = ScancodeMediaSelect,
        ["MediaStop"] = 269, ["MediaTrackNext"] = 267, ["MediaTrackPrevious"] = 268,
        ["Power"] = ScancodePower, ["Sleep"] = ScancodeSleep,
        ["AudioVolumeDown"] = ScancodeVolumeDown, ["AudioVolumeMute"] = ScancodeMute,
        ["AudioVolumeUp"] = ScancodeVolumeUp, ["WakeUp"] = ScancodeWake,
        ["Again"] = ScancodeAgain, ["Copy"] = ScancodeCopy, ["Cut"] = ScancodeCut,
        ["Find"] = ScancodeFind, ["Open"] = ScancodeMediaOpen, ["Paste"] = ScancodePaste,
        ["Props"] = ScancodeMediaProperties, ["Select"] = ScancodeSelect, ["Undo"] = ScancodeUndo,
    };

    private readonly HashSet<PhysicalKey> heldKeys = [];
    private readonly HashSet<int> recordedKeys = [];
    private string keyCodes;
    private bool isRecording;
    private string captureError = "";

    public SessionShortcutBinding(string name, string configKey, string defaultKeyCodes)
    {
        Name = name;
        ConfigKey = configKey;
        DefaultKeyCodes = defaultKeyCodes;
        keyCodes = defaultKeyCodes;
    }

    public string Name { get; }
    public string ConfigKey { get; }
    public string DefaultKeyCodes { get; }
    public string KeyCodes => keyCodes;
    public string Display => string.Join(" + ", ParseKeyCodes(keyCodes).Select(FormatKeyCode));
    public bool IsRecording { get => isRecording; private set { if (Set(ref isRecording, value)) Changed(nameof(RecordButtonText)); } }
    public string RecordButtonText => IsRecording ? "Release keys to finish" : "Record";
    public string CaptureError { get => captureError; private set => Set(ref captureError, value); }

    public bool TrySetKeyCodes(string value)
    {
        if (!TryParseKeyCodes(value, out var parsed)) return false;
        var normalized = SerializeKeyCodes(parsed);
        if (keyCodes == normalized) return true;
        keyCodes = normalized;
        Changed(nameof(KeyCodes));
        Changed(nameof(Display));
        return true;
    }

    public void ResetToDefault()
    {
        CancelRecording();
        CaptureError = "";
        TrySetKeyCodes(DefaultKeyCodes);
    }

    public void StartRecording()
    {
        heldKeys.Clear();
        recordedKeys.Clear();
        CaptureError = "";
        IsRecording = true;
    }

    public void CancelRecording()
    {
        heldKeys.Clear();
        recordedKeys.Clear();
        IsRecording = false;
    }

    public bool CaptureKeyDown(PhysicalKey key)
    {
        if (!IsRecording) return false;
        if (key == PhysicalKey.None) return true;
        if (!TryGetScancode(key, out var scancode))
        {
            CaptureError = $"The {key} key cannot be used for a shortcut.";
            CancelRecording();
            return true;
        }
        heldKeys.Add(key);
        recordedKeys.Add(scancode);
        return true;
    }

    public bool CaptureKeyUp(PhysicalKey key)
    {
        if (!IsRecording) return false;
        if (!heldKeys.Remove(key)) return true;
        if (heldKeys.Count != 0) return true;

        IsRecording = false;
        if (!recordedKeys.Any(scancode => scancode >= 0))
        {
            CaptureError = "Hold at least one non-modifier key, then release all keys.";
            recordedKeys.Clear();
            return true;
        }
        TrySetKeyCodes(SerializeKeyCodes(recordedKeys));
        recordedKeys.Clear();
        return true;
    }

    public static bool TryParseKeyCodes(string value, out int[] parsed)
    {
        parsed = [];
        if (string.IsNullOrWhiteSpace(value)) return false;
        var codes = new HashSet<int>();
        foreach (var token in value.Split(','))
        {
            if (!int.TryParse(token.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var code)
                || !IsSupportedCode(code) || !codes.Add(code)) return false;
        }
        if (!codes.Any(code => code >= 0)) return false;
        parsed = codes.ToArray();
        return true;
    }

    private static bool TryGetScancode(PhysicalKey key, out int scancode)
    {
        var name = key.ToString();
        if (name.Length == 1 && name[0] is >= 'A' and <= 'Z')
        {
            scancode = ScancodeA + name[0] - 'A';
            return true;
        }
        if (name.StartsWith("Digit", StringComparison.Ordinal)
            && int.TryParse(name.AsSpan("Digit".Length), NumberStyles.None, CultureInfo.InvariantCulture, out var digit))
        {
            scancode = digit == 0 ? ScancodeDigit0 : ScancodeDigit1 + digit - 1;
            return true;
        }
        if (name.StartsWith("F", StringComparison.Ordinal)
            && int.TryParse(name.AsSpan(1), NumberStyles.None, CultureInfo.InvariantCulture, out var functionKey))
        {
            if (functionKey is >= 1 and <= 12) scancode = ScancodeF1 + functionKey - 1;
            else if (functionKey is >= 13 and <= 24) scancode = ScancodeF13 + functionKey - 13;
            else { scancode = 0; return false; }
            return true;
        }
        if (name.StartsWith("NumPad", StringComparison.Ordinal)
            && int.TryParse(name.AsSpan("NumPad".Length), NumberStyles.None, CultureInfo.InvariantCulture, out var keypadDigit)
            && keypadDigit is >= 0 and <= 9)
        {
            scancode = keypadDigit == 0 ? ScancodeKeypad0 : ScancodeKeypad1 + keypadDigit - 1;
            return true;
        }
        if (SpecialScancodes.TryGetValue(name, out scancode)) return true;
        scancode = 0;
        return false;
    }

    private static bool IsSupportedCode(int code) => code is >= MetaModifier and <= ControlModifier
        || code is > 0 and < 512;

    private static int[] ParseKeyCodes(string value) => TryParseKeyCodes(value, out var parsed)
        ? OrderKeyCodes(parsed) : [];

    private static string SerializeKeyCodes(IEnumerable<int> codes) =>
        string.Join(',', OrderKeyCodes(codes));

    private static int[] OrderKeyCodes(IEnumerable<int> codes) => codes
        .OrderBy(code => code < 0 ? 0 : 1).ThenBy(code => code).ToArray();

    private static string FormatKeyCode(int code)
    {
        if (code == ControlModifier) return "Ctrl";
        if (code == ShiftModifier) return "Shift";
        if (code == AltModifier) return "Alt";
        if (code == MetaModifier) return "Win";
        if (code is >= ScancodeA and < ScancodeA + 26) return ((char)('A' + code - ScancodeA)).ToString();
        if (code is >= ScancodeDigit1 and < ScancodeDigit0) return (code - ScancodeDigit1 + 1).ToString(CultureInfo.InvariantCulture);
        if (code == ScancodeDigit0) return "0";
        if (code is >= ScancodeF1 and < ScancodeF1 + 12) return $"F{code - ScancodeF1 + 1}";
        if (code is >= ScancodeF13 and < ScancodeF13 + 12) return $"F{code - ScancodeF13 + 13}";
        if (code is >= ScancodeKeypad1 and < ScancodeKeypad0) return $"NumPad {code - ScancodeKeypad1 + 1}";
        if (code == ScancodeKeypad0) return "NumPad 0";
        return DisplayNames.GetValueOrDefault(code, $"Key {code.ToString(CultureInfo.InvariantCulture)}");
    }

    private static readonly Dictionary<int, string> DisplayNames = new()
    {
        [41] = "Escape", [42] = "Backspace", [43] = "Tab", [40] = "Enter", [44] = "Space",
        [45] = "-", [46] = "=", [47] = "[", [48] = "]", [49] = "\\", [50] = "#",
        [51] = ";", [52] = "'", [53] = "`", [54] = ",", [55] = ".", [56] = "/",
        [57] = "Caps Lock", [70] = "Print Screen", [71] = "Scroll Lock", [72] = "Pause",
        [73] = "Insert", [74] = "Home", [75] = "Page Up", [76] = "Delete", [77] = "End",
        [78] = "Page Down", [79] = "Right", [80] = "Left", [81] = "Down", [82] = "Up",
        [83] = "Num Lock", [84] = "NumPad /", [85] = "NumPad *", [86] = "NumPad -",
        [87] = "NumPad +", [88] = "NumPad Enter", [99] = "NumPad .", [100] = "Intl Backslash",
        [101] = "Menu", [102] = "Power", [103] = "NumPad =", [116] = "Execute",
        [117] = "Help", [118] = "Menu", [119] = "Select", [120] = "Stop", [121] = "Again",
        [122] = "Undo", [123] = "Cut", [124] = "Copy", [125] = "Paste", [126] = "Find",
        [127] = "Mute", [128] = "Volume Up", [129] = "Volume Down", [133] = "NumPad ,",
        [135] = "Intl Ro", [137] = "Intl Yen", [138] = "Convert", [139] = "Non-Convert",
        [144] = "Kana / Lang 1", [145] = "Lang 2", [146] = "Lang 3", [147] = "Lang 4",
        [148] = "Lang 5", [156] = "NumPad Clear", [182] = "NumPad (", [183] = "NumPad )",
        [258] = "Sleep", [259] = "Wake", [267] = "Next Track", [268] = "Previous Track",
        [269] = "Media Stop", [270] = "Eject", [271] = "Play / Pause", [272] = "Media Select",
        [274] = "Open", [279] = "Properties", [280] = "Browser Search", [281] = "Browser Home",
        [282] = "Browser Back", [283] = "Browser Forward", [284] = "Browser Stop",
        [285] = "Browser Refresh", [286] = "Browser Favorites",
    };
}
