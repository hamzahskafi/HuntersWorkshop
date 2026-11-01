using System.Windows;
using System.Windows.Controls;
using BloodborneLauncher.Core;

namespace BloodborneLauncher.Views
{
    /// <summary>
    /// A tab of the Modern layout that holds several pages behind segmented buttons at its top: Visuals
    /// (Display, Graphics, Interface) and More (mods, cheats, the engine's switches, the log).
    /// The sidebar lists the same pages as entries of their own, under the same names.
    /// </summary>
    public abstract class SectionedPage : LauncherPage
    {
        private readonly (string Key, string Label, Type Page)[] _sections;
        private readonly Dictionary<Type, LauncherPage> _pages = new();
        private readonly Frame _frame = new() { NavigationUIVisibility = System.Windows.Navigation.NavigationUIVisibility.Hidden, Focusable = false };
        private readonly Segmented _tabs;
        private int _current = -1;

        protected SectionedPage(string title, (string Key, string Label, Type Page)[] sections) : base(title, "")
        {
            _sections = sections;
            // As wide as the longest name needs (the columns share the width evenly), at least 104 each.
            _tabs = new Segmented(_sections.Select(s => new Choice(s.Label, s.Key)).ToArray())
            {
                HorizontalAlignment = HorizontalAlignment.Center,
                MinWidth = _sections.Length * 104,
                Margin = new Thickness(0, 6, 0, 2),
            };
            _tabs.Changed += Open;
            _frame.Navigated += (_, _) => { while (_frame.CanGoBack) _frame.RemoveBackEntry(); };

            var root = new Grid();
            root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            root.RowDefinitions.Add(new RowDefinition());
            root.Children.Add(_tabs);
            Grid.SetRow(_frame, 1);
            root.Children.Add(_frame);
            Content = root;
            Open(0);
        }

        public bool HasSection(string key) => Array.FindIndex(_sections, s => s.Key == key) >= 0;

        public void ShowSection(string key)
        {
            var index = Array.FindIndex(_sections, s => s.Key == key);
            if (index >= 0) Open(index);
        }

        public IEnumerable<string> SectionKeys => _sections.Select(s => s.Key);

        /// <summary>The page types of the sections, made or not.</summary>
        public IEnumerable<Type> SectionTypes => _sections.Select(s => s.Page);

        /// <summary>The key of the section shown (MainWindow.ReloadPages keeps it).</summary>
        public string? ShownKey => _current >= 0 ? _sections[_current].Key : null;

        /// <summary>The page of the section shown (Settings › Navigation keeps it when it switches).</summary>
        public Type? ShownPage => _current >= 0 ? _sections[_current].Page : null;

        /// <summary>Makes a section's page without showing it (the settings search, in the background).</summary>
        public void MakeSection(string key)
        {
            var index = Array.FindIndex(_sections, s => s.Key == key);
            if (index >= 0) PageOf(_sections[index].Page);
        }

        /// <summary>The sections whose page is made: the settings search looks through them as pages
        /// of their own (MakeSection makes the others).</summary>
        public IEnumerable<(string Key, string Label, LauncherPage Page)> MadeSectionPages() =>
            _sections.Where(s => _pages.ContainsKey(s.Page)).Select(s => (s.Key, s.Label, _pages[s.Page]));

        private LauncherPage PageOf(Type type)
        {
            if (!_pages.TryGetValue(type, out var page))
                _pages[type] = page = (LauncherPage)Activator.CreateInstance(type)!;
            return page;
        }

        private void Open(int index)
        {
            _tabs.Select(index, notify: false);
            var page = PageOf(_sections[index].Page);
            if (_current != index) _frame.Navigate(page);
            _current = index;
            page.OnShown();
        }

        public override void OnShown()
        {
            if (_current >= 0 && _pages.TryGetValue(_sections[_current].Page, out var page))
                page.OnShown();
        }
    }

    /// <summary>How the game looks (0.4.4 beta 26: the Display and Graphics tabs before; 26b: Display,
    /// Graphics, Look and Interface, were Display, Upscaling, Picture and Interface; 27b: Look went, its
    /// effects to Graphics and its picture tuning to Display).</summary>
    public sealed class VisualsPage : SectionedPage
    {
        public static readonly (string Key, string Label, Type Page)[] Sections =
        {
            ("display", L.T("Display"), typeof(DisplayPage)),
            ("graphics", L.T("Graphics"), typeof(GraphicsPage)),
            ("interface", L.T("Interface"), typeof(InterfacePage)),
        };

        public VisualsPage() : base(L.T("Visuals"), Sections) { }
    }

    /// <summary>The less used pages behind one tab: mods, cheats, the engine's switches and the game's
    /// log (Online and Settings have tabs of their own).</summary>
    public sealed class MorePage : SectionedPage
    {
        // Cheats only once the engine applies them.
        public static readonly (string Key, string Label, Type Page)[] Sections = new (string, string, Type)[]
        {
            ("mods", L.T("Mods & patches"), typeof(ModsPage)),
            ("cheats", L.T("Cheats"), typeof(CheatsPage)),
            ("engine", L.T("Engine switches"), typeof(EnginePage)),
            ("output", L.T("Log"), typeof(OutputPage)),
        }.Where(s => s.Item1 != "cheats" || CheatsPage.Supported).ToArray();

        public MorePage() : base(L.T("More"), Sections) { }
    }
}
