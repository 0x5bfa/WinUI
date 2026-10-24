using System.ComponentModel;

namespace Fixture;

public partial class MainPage
{
    public DemoModel ViewModel { get; } = new();
    public int SaveCount { get; private set; }

    private void SaveClicked(object? sender, EventArgs args) => SaveCount++;
}

public sealed class DemoModel : INotifyPropertyChanged
{
    private string _title = "Initial";
    public string Title
    {
        get => _title;
        set { _title = value; PropertyChanged?.Invoke(this, new(nameof(Title))); }
    }

    public event PropertyChangedEventHandler? PropertyChanged;
}

internal static class Program
{
    private static void Main()
    {
        var page = new MainPage();
        page.InitializeComponent();
        if (page.TitleBlock?.Text != "Initial") throw new InvalidOperationException("Initial x:Bind failed.");
        page.ViewModel.Title = "Updated";
        if (page.TitleBlock.Text != "Updated") throw new InvalidOperationException("OneWay x:Bind failed.");
        page.TitleBlock.Text = "From UI";
        if (page.ViewModel.Title != "From UI") throw new InvalidOperationException("TwoWay x:Bind failed.");
        if (page.TitleBlock.ActiveCallbackCount != 1) throw new InvalidOperationException("Unexpected TwoWay subscriptions.");
        page.RaiseUnloaded();
        if (page.TitleBlock.ActiveCallbackCount != 0) throw new InvalidOperationException("TwoWay callback survived Unloaded.");
        page.ViewModel.Title = "While unloaded";
        if (page.TitleBlock.Text != "From UI") throw new InvalidOperationException("OneWay subscription survived Unloaded.");
        page.TitleBlock.Text = "UI while unloaded";
        if (page.ViewModel.Title != "While unloaded") throw new InvalidOperationException("TwoWay callback survived Unloaded.");
        page.RaiseLoaded();
        if (page.TitleBlock.ActiveCallbackCount != 1 || page.TitleBlock.Text != "While unloaded")
            throw new InvalidOperationException("Bindings failed to reconnect after Loaded.");
        page.RaiseLoaded();
        if (page.TitleBlock.ActiveCallbackCount != 1) throw new InvalidOperationException("TwoWay callback registered twice.");
        page.TitleBlock.Text = "Reconnected";
        if (page.ViewModel.Title != "Reconnected") throw new InvalidOperationException("TwoWay binding failed after Loaded.");
        page.SaveButton?.RaiseClick();
        if (page.SaveCount != 1) throw new InvalidOperationException("Event connector failed.");
        Console.WriteLine("Generated C# XAML and TwoWay binding integration passed.");
    }
}
