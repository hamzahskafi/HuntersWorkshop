using System.Windows.Controls;
using BloodborneLauncher.Core;
using Wpf.Ui.Controls;

namespace BloodborneLauncher.Views
{
    /// <summary>
    /// What is drawn over the game: the port's in-game menu, the HUD's sharpness, the shader note and
    /// the FPS counter (0.4.4 beta 26: these were on the Display page). The counter's settings are
    /// greyed while it is off, as in the in-game menu.
    /// </summary>
    public sealed class InterfacePage : LauncherPage
    {
        private readonly StackPanel _counter = new();

        public InterfacePage() : base(L.T("Interface"), "")
        {
            Group(L.T("Menus and HUD"));
            SettingCombo(SymbolRegular.Keyboard24, L.T("In-game menu shortcut"),
                L.T("What opens the port's settings menu while playing. Applied on the next launch"), Choices.OverlayKeys, "overlay_keys");
            ComboRow(SymbolRegular.LocalLanguage24, L.T("In-game menu language"), L.T("The port's settings menu (Insert or L3 + R3)"),
                Choices.MenuLanguages, () => Ini.Get("menu_language", "en"), v => Ini.Set("menu_language", v));
            SettingToggle(SymbolRegular.TextFont24, L.T("Sharp UI"),
                L.T("Menus, HUD and text drawn at the output resolution instead of 1080p stretched up: much sharper at 1440p and 4K. New: on by default in betas while it is tested. Applied on the next launch"),
                "exp_sharp_ui");
            IniToggle(SymbolRegular.Wrench24, L.T("Note while shaders build"),
                L.T("\"Building shaders\" in a corner while the game builds shaders for a place it shows for the first time. Also in game: Insert or L3 + R3"),
                "compile_note");

            Group(L.T("FPS counter"));
            IniToggle(SymbolRegular.DataUsage24, L.T("FPS counter"), L.T("Also in game: Insert or L3 + R3"), "show_fps", changed: ShowCounter);
            // What the counter shows (on unless turned off).
            foreach (var (icon, key, title) in new[]
                     {
                         (SymbolRegular.TopSpeed24, "show_fps_rate", L.T("Frame rate")),
                         (SymbolRegular.Timer24, "show_fps_frame_time", L.T("Frame time")),
                         (SymbolRegular.ResizeLarge24, "show_fps_upscaler", L.T("Upscaler")),
                         (SymbolRegular.DeveloperBoard24, "show_fps_cpu", L.T("CPU usage")),
                         (SymbolRegular.Gauge24, "show_fps_gpu", L.T("GPU usage")),
                     })
            {
                // CPU and GPU use are off unless chosen (the engine's defaults, bbport_settings.h).
                var fallback = key is "show_fps_cpu" or "show_fps_gpu" ? "0" : "1";
                ToggleRow(icon, title, L.T("Shown in the FPS counter"), () => Ini.Get(key, fallback) == "1", v => Ini.Set(key, v ? "1" : "0"), _counter);
            }
            var look = Advanced(L.T("Look"), _counter);
            IniCombo(SymbolRegular.PositionToFront24, L.T("FPS counter position"), null, new Choice[]
            {
                new(L.T("Top right"), "0"), new(L.T("Top left"), "1"), new(L.T("Bottom right"), "2"), new(L.T("Bottom left"), "3"), new(L.T("Top centre"), "4"),
            }, "fps_position", look);
            IniSlider(SymbolRegular.TextFontSize24, L.T("FPS counter size"), 0.75, 2.0, "fps_scale", look);
            IniCombo(SymbolRegular.Color24, L.T("FPS counter text colour"), L.T("Any colour in game: Insert or L3 + R3 › Other"), new Choice[]
            {
                new(L.T("White"), "#FFFFFF"), new(L.T("Yellow"), "#FFD54A"), new(L.T("Green"), "#7CFC8A"), new(L.T("Cyan"), "#6FE3FF"),
                new(L.T("Red"), "#FF5A5A"), new(L.T("Orange"), "#FFA040"), new(L.T("Pink"), "#FF8AD8"), new(L.T("Black"), "#000000"),
            }, "fps_text_color", look);
            IniSlider(SymbolRegular.Eye24, L.T("FPS counter text opacity"), 0.1, 1.0, "fps_text_opacity", look, fallback: 1.0);
            IniCombo(SymbolRegular.ColorBackground24, L.T("FPS counter background"), null, new Choice[]
            {
                new(L.T("Dark grey"), "#0F0F0F"), new(L.T("Black"), "#000000"), new(L.T("Dark red"), "#3A0A0A"), new(L.T("Dark blue"), "#0A1A3A"), new(L.T("White"), "#FFFFFF"),
            }, "fps_bg_color", look);
            IniSlider(SymbolRegular.Blur24, L.T("FPS counter background opacity"), 0.0, 1.0, "fps_bg_opacity", look, fallback: 0.5);
            IniToggle(SymbolRegular.BorderAll24, L.T("FPS counter border"), null, "fps_border", look);
            Body.Children.Add(_counter);
            ShowCounter();
        }

        private void IniSlider(SymbolRegular icon, string title, double min, double max, string key, Panel into, double fallback = 1.0) =>
            SliderRow(icon, title, null, min, max, 0.05, () => ParseDouble(Ini.Get(key, Fmt(fallback)), fallback), v => Ini.Set(key, Fmt(v)), into: into);

        private void ShowCounter() => _counter.IsEnabled = Ini.Flag("show_fps");
    }
}
