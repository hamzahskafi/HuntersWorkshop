using System.IO;
using System.Windows;
using System.Windows.Controls;
using BloodborneLauncher.Core;
using Wpf.Ui.Controls;
using TextBox = System.Windows.Controls.TextBox;

namespace BloodborneLauncher.Views
{
    /// <summary>The game's own output (run.py and bb-probe), live while it runs, and what its log holds
    /// (the log level and the Diagnostics fold).</summary>
    public sealed class OutputPage : LauncherPage
    {
        private readonly TextBox _output;
        private readonly ToggleSwitch _follow;

        public OutputPage()
        {
            var bar = new Grid { Margin = new Thickness(0, 18, 0, 8) };
            bar.ColumnDefinitions.Add(new ColumnDefinition());
            bar.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

            _follow = new ToggleSwitch { IsChecked = true, Content = L.T("Follow new output"), VerticalAlignment = VerticalAlignment.Center };
            bar.Children.Add(_follow);

            var buttons = Row(
                SmallButton(L.T("Copy"), SymbolRegular.Copy24, (_, _) => Copy()),
                SmallButton(L.T("Export…"), SymbolRegular.Save24, (_, _) => Export()),
                SmallButton(L.T("Clear"), SymbolRegular.Broom24, (_, _) => Ws.ClearOutput()));
            Grid.SetColumn(buttons, 1);
            bar.Children.Add(buttons);
            Body.Children.Add(bar);

            SettingToggle(SymbolRegular.WindowConsole20, L.T("Show this page when the game starts"), null, "switch_to_output");
            SettingCombo(SymbolRegular.DocumentText24, L.T("Log level"),
                L.T("How much the game writes into its log: Standard for everyday play, Detailed when asked for a log of something odd, Debug only to chase a specific bug"),
                Choices.LogLevels, "log_level");
            // 0.4.4 beta 28: what else the game writes into its log, as one fold; the launcher passes
            // them as one list, BB_DIAG (GameEnvironment), which the engine's start programs expand.
            var diag = Advanced(L.T("Diagnostics"));
            SettingToggle(SymbolRegular.TextBulletListSquare24, L.T("Frame statistics in the log"), "BB_FRAME_STATS", "frame_stats", diag);
            SettingCombo(SymbolRegular.Pulse24, L.T("Hitch reports"),
                L.T("A frame slower than this puts every thread's wait and stack in the log, for stutter reports. Pick about twice your frame time: 50 ms at 30 FPS, 40 at 60, 25 at 90, 20 at 120, 16 at 144 or uncapped. Each report pauses the game about 0.2 s; at most 12 a session, none in the first 20 s or just after a loading screen"),
                Choices.HitchReports, "hitch_report_ms", diag);
            SettingToggle(SymbolRegular.DeveloperBoard24, L.T("GPU time in the log"),
                L.T("BB_GPU_PROFILE: the GPU's time a frame by render pass, dispatch and upscaler, every 5 seconds. Costs a little GPU time"),
                "gpu_profile", diag);
            SettingToggle(SymbolRegular.Timer24, L.T("Render stage timings in the log"), L.T("BB_SECTIONS: where each frame's CPU time goes (with frame statistics). Costs a little speed: a few tenths of a millisecond a frame on the draw recording thread"), "section_stats", diag);
            SettingToggle(SymbolRegular.HourglassHalf24, L.T("Loading profile in the log"),
                L.T("BB_SAMPLE_THREADS: where the game's main, render and file threads spend their time, every 5 seconds. For slow loading; costs a little speed"), "load_profile", diag);
            SettingToggle(SymbolRegular.Speaker224, L.T("Audio statistics in the log"), "BB_AUDIO_STATS", "audio_stats", diag);
            SettingToggle(SymbolRegular.XboxController24, L.T("Controller details in the log"),
                L.T("BB_PAD_LOG: each controller's USB ids, connection and rumble support, disconnects, the game's first vibration request and sticks past their round gate. For controller problems"),
                "pad_log", diag);
            SettingToggle(SymbolRegular.ShieldError24, L.T("Heap corruption checks"),
                L.T("BB_WRITE_LOG: the GPU side's last writes into the game's memory, put in the log if the game crashes on damaged memory. For crash reports; changes the timing of those writes a little"),
                "heap_checks", diag);
            SettingToggle(SymbolRegular.Camera24, L.T("Capture shaders"),
                "BB_SHADER_CAPTURE: every shader the game uses is saved once to logs\\shaders (a few MB), for testing the new native shader compiler offline. Costs a little time the first time each shader is seen", "shader_capture", diag);

            _output = new TextBox { Height = 520, Margin = new Thickness(0, 4, 0, 0) };
            _output.SetResourceReference(StyleProperty, "CodeBox");
            Body.Children.Add(_output);

            // One append per batch; while the page is hidden nothing is laid out, and it catches up
            // in one go when shown.
            Ws.GameOutput += batch =>
            {
                if (!IsVisible)
                {
                    _stale = true;
                    return;
                }
                _lines += batch.Count;
                if (_lines > Workspace.MaxOutputLines + 500)
                {
                    Rebuild();
                    return;
                }
                _output.AppendText(string.Join("\n", batch) + "\n");
                if (_follow.IsChecked == true)
                    _output.ScrollToEnd();
            };
            Ws.OutputCleared += () =>
            {
                _output.Clear();
                _lines = 0;
            };
            IsVisibleChanged += (_, _) => { if (IsVisible && _stale) Rebuild(); };
            Rebuild();
        }

        private int _lines;
        private bool _stale;

        private void Rebuild()
        {
            _output.Text = Ws.OutputLines.Count > 0 ? string.Join("\n", Ws.OutputLines) + "\n" : "";
            _lines = Ws.OutputLines.Count;
            _stale = false;
            if (_follow.IsChecked == true)
                _output.ScrollToEnd();
        }

        public override void OnShown()
        {
            if (_stale) Rebuild();
            else if (_follow.IsChecked == true)
                _output.ScrollToEnd();
        }

        private void Copy()
        {
            if (_output.Text.Length == 0) return;
            if (ClipboardText.Set(_output.Text))
                Ws.Log(L.T("Game output copied."), LogLevel.Good);
            else
                Ws.Log(L.T("Could not copy: another program is using the clipboard. Try again, or use Export."), LogLevel.Warn);
        }

        private void Export()
        {
            var dialog = new Microsoft.Win32.SaveFileDialog
            {
                Title = L.T("Export the game output"),
                FileName = $"hunters-workshop-{DateTime.Now:yyyyMMdd_HHmmss}.log",
                Filter = L.T("Log files|*.log|Text files|*.txt|All files|*.*"),
            };
            if (dialog.ShowDialog() != true) return;
            try
            {
                // The whole session (logs\last_session.log), not the last lines this page keeps: a
                // report trimmed to its end lost the start and what came before a freeze.
                if (File.Exists(GameRunner.SessionLog))
                {
                    using var from = new FileStream(GameRunner.SessionLog, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                    using var to = File.Create(dialog.FileName);
                    from.CopyTo(to);
                }
                else
                    File.WriteAllText(dialog.FileName, _output.Text);
                Ws.Log(L.T("Game output saved: ") + dialog.FileName, LogLevel.Good);
            }
            catch (Exception e)
            {
                Ws.Log(L.T("Could not save the output: ") + e.Message, LogLevel.Bad);
            }
        }
    }
}
