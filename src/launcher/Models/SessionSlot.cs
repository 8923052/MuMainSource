using System.ComponentModel;

namespace MuLauncher.Models;

public sealed class SessionSlot : ObservableModel
{
    private int number;
    public int Number { get => number; internal set { Set(ref number, value); Changed(nameof(Label)); } }
    private SessionEntry? session;

    public SessionSlot(int number, SessionEntry? session = null)
    {
        Number = number;
        Session = session;
    }

    public SessionEntry? Session
    {
        get => session;
        set
        {
            if (ReferenceEquals(session, value)) return;
            if (session != null) session.PropertyChanged -= SessionEdited;
            session = value;
            if (session != null) session.PropertyChanged += SessionEdited;
            Changed();
            Changed(nameof(Label));
            Changed(nameof(HasSession));
        }
    }

    public bool HasSession => Session != null;
    public string Label => $"[{Number}] {(Session == null ? "(Empty)" : Session.DisplayName)}";
    private void SessionEdited(object? sender, PropertyChangedEventArgs args) => Changed(nameof(Label));
}
