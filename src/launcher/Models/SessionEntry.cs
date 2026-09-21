namespace MuLauncher.Models;

public sealed class SessionEntry : ObservableModel
{
    private string name = "Session 1", username = "", password = "", port = "0", character = "";
    public string DisplayName { get => name; set => Set(ref name, value ?? ""); }
    public string Username { get => username; set => Set(ref username, value ?? ""); }
    public string Password { get => password; set => Set(ref password, value ?? ""); }
    public string AutoLoginPort { get => port; set { if (value != null) Set(ref port, value); } }
    public string AutoSelectCharacter { get => character; set => Set(ref character, value ?? ""); }
}
