using Avalonia.Controls;
using MuLauncher.Services;

namespace MuLauncher.Views;

public sealed class MainWindow : Window
{
    public MainWindow(GameFiles files)
    {
        Title = "Mu Launcher";
        Width = 1180;
        Height = 790;
        MinWidth = 760;
        MinHeight = 600;
        var view = new LauncherView(files);
        Content = view;
        Closing += (_, args) => args.Cancel = !view.SaveBeforeExit();
    }
}
