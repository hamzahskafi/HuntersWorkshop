using System.Windows.Controls;
using BloodborneLauncher.Core;
using Wpf.Ui.Controls;

namespace BloodborneLauncher.Views
{
    /// <summary>
    /// The camera and the game's extras: the in-game menu's Gameplay tab, beside the hunt (0.4.4 beta 26:
    /// these were on the Graphics page, the follow camera and the extras among its game effects).
    /// </summary>
    public sealed class GameplayPage : LauncherPage
    {
        private readonly StackPanel _camera = new();

        public GameplayPage() : base(L.T("Gameplay"), "")
        {
            Group(L.T("Camera"));
            IniCombo(SymbolRegular.Camera24, L.T("Follow camera"),
                L.T("Original keeps the game's camera distance, angle and follow speed at 60 FPS and above"), Choices.CameraTuning, "camera_tuning");
            IniToggle(SymbolRegular.Camera24, L.T("Camera tuning"), L.T("Turns the three settings below on"), "camera_controls", changed: ShowCamera);
            CameraSlider(SymbolRegular.ZoomOut24, L.T("Field of view"), 1, 1.5, "camera_fov_scale");
            CameraSlider(SymbolRegular.ArrowMaximize24, L.T("Distance"), 0.5, 1.5, "camera_distance_scale");
            CameraSlider(SymbolRegular.ArrowBidirectionalUpDown24, L.T("Height"), 0.5, 1.5, "camera_height_scale");
            Body.Children.Add(_camera);
            ShowCamera();

            Group(L.T("Extras"));
            Body.Children.Add(Text(L.T("Patches to the game, applied when it starts."), "Faint"));
            foreach (var effect in Choices.Effects.Where(e => PicturePage.Gameplay.Contains(e.Key)))
                IniToggle(effect.Key switch
                {
                    "skip_intro" => SymbolRegular.FastForward24,
                    "debug_camera" => SymbolRegular.Camera24,
                    _ => SymbolRegular.Bug24,
                }, effect.Title, effect.Hint, effect.Key);
        }

        private void CameraSlider(SymbolRegular icon, string title, double min, double max, string key) =>
            SliderRow(icon, title, null, min, max, 0.05, () => ParseDouble(Ini.Get(key, "1.00"), 1.0), v => Ini.Set(key, Fmt(v)), into: _camera);

        private void ShowCamera() => _camera.IsEnabled = Ini.Flag("camera_controls");
    }
}
