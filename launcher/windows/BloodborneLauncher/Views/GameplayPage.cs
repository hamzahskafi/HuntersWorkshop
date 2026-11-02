using System.Windows.Controls;
using BloodborneLauncher.Core;
using Wpf.Ui.Controls;

namespace BloodborneLauncher.Views
{
    /// <summary>
    /// The camera and the game's extras: the in-game menu's Gameplay tab, beside the hunt (0.4.4 beta 26:
    /// these were on the Graphics page, the follow camera and the extras among its game effects; 26b:
    /// camera tuning a switch whose sliders fold under it, the mirror and rebirth from the game's System
    /// menu › Extras).
    /// </summary>
    public sealed class GameplayPage : LauncherPage
    {
        /// <summary>The game's effects shown here, as extras.</summary>
        public static readonly string[] Extras = { "skip_intro", "debug_camera", "debug_menu" };

        private FoldRows _camera = null!;

        public GameplayPage()
        {
            Group(L.T("Camera"));
            IniCombo(SymbolRegular.Camera24, L.T("Follow camera"),
                L.T("Original keeps the game's camera distance, angle and follow speed at 60 FPS and above"), Choices.CameraTuning, "camera_tuning", line: L.T("How the camera follows you at 60 FPS and above"));
            _camera = FoldToggle(SymbolRegular.Camera24, L.T("Camera tuning"), L.T("Applies the three settings below. Also in the game's System menu (Camera Tuning)"),
                () => Ini.Flag("camera_controls"), v => Ini.Set("camera_controls", v ? "1" : "0"), out _, out _, line: L.T("Your own field of view, distance and height"));
            CameraSlider(SymbolRegular.ZoomOut24, L.T("Field of view"), 1, 1.5, "camera_fov_scale");
            CameraSlider(SymbolRegular.ArrowMaximize24, L.T("Distance"), 0.5, 1.5, "camera_distance_scale");
            CameraSlider(SymbolRegular.ArrowBidirectionalUpDown24, L.T("Height"), 0.5, 1.5, "camera_height_scale");
            Body.Children.Add(_camera);

            Group(L.T("Extras"));
            Body.Children.Add(Text(L.T("Patches to the game, applied when it starts."), "Faint"));
            foreach (var effect in Choices.Effects.Where(e => Extras.Contains(e.Key)))
                IniToggle(effect.Key switch
                {
                    "skip_intro" => SymbolRegular.FastForward24,
                    "debug_camera" => SymbolRegular.Camera24,
                    _ => SymbolRegular.Bug24,
                }, effect.Title, effect.Hint, effect.Key, line: effect.Line);
            // The engine's own features, also in the game's System menu › Extras (option_menu.cpp).
            IniToggle(SymbolRegular.Person24, L.T("Hunter's Dream mirror"),
                L.T("The Dream's mirror opens the appearance editor (the game's unused \"Put on Disguise\"). Applied when the game starts"),
                "change_appearance", line: L.T("The Dream's mirror opens the appearance editor"));
            IniToggle(SymbolRegular.ArrowCounterclockwise24, L.T("Rebirth at the altar"),
                L.T("With the Yharnam Stone, the Altar of Despair resets your level and attributes. Applied when the game starts"),
                "rebirth", line: L.T("Reset your level and attributes at the Altar of Despair"));
        }

        private void CameraSlider(SymbolRegular icon, string title, double min, double max, string key)
        {
            SliderRow(icon, title, null, min, max, 0.05, () => ParseDouble(Ini.Get(key, "1.00"), 1.0), v => Ini.Set(key, Fmt(v)), into: _camera);
            LastRow.SetTip(L.T("1.00 is the game's own"));
        }
    }
}
