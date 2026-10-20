using System.Diagnostics;
using System.Drawing.Drawing2D;
using System.Security;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.Win32;

namespace SonyDeviceCenter;

public sealed class MainForm : Form
{
    private const string StartupKeyPath = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string StartupValueName = "SonyDeviceCenter";
    private const int DashboardRefreshIntervalMs = 15_000;
    private const int BatteryRefreshIntervalMs = 30_000;
    private const int TrayBatteryRefreshIntervalMs = BatteryRefreshIntervalMs;
    private const int MultipointRefreshIntervalMs = 60_000;
    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNameCaseInsensitive = true };
    private static readonly Color Accent = Color.FromArgb(99, 82, 210);
    private static readonly Color Muted = Color.FromArgb(114, 118, 130);
    private static readonly Color Ink = Color.FromArgb(37, 40, 52);

    private sealed record HeadsetCandidate(string Name, string Address)
    {
        public override string ToString() => Name + "  ·  " + Address;
    }

    private sealed record EqualizerPresetOption(string Id, string Name)
    {
        public override string ToString() => Name;
    }

    private sealed class Dashboard
    {
        [JsonPropertyName("connected")] public bool Connected { get; set; }
        [JsonPropertyName("name")] public string? Name { get; set; }
        [JsonPropertyName("address")] public string? Address { get; set; }
        [JsonPropertyName("battery")] public BatteryState? Battery { get; set; }
        [JsonPropertyName("noiseControl")] public NoiseState? NoiseControl { get; set; }
        [JsonPropertyName("features")] public FeatureState? Features { get; set; }
        [JsonPropertyName("capabilities")] public CapabilityState? Capabilities { get; set; }
        [JsonPropertyName("multipoint")] public MultipointState? Multipoint { get; set; }
        [JsonPropertyName("equalizer")] public EqualizerState? Equalizer { get; set; }
        [JsonPropertyName("dsee")] public bool Dsee { get; set; }
    }

    private sealed class BatteryState
    {
        public int? Main { get; set; }
        public int? Left { get; set; }
        public int? Right { get; set; }
        public int? Case { get; set; }
        public bool Charging { get; set; }
    }

    private sealed class NoiseState
    {
        public string? Mode { get; set; }
        public int? AmbientLevel { get; set; }
        public string? Availability { get; set; }
    }

    private sealed class FeatureState
    {
        public FeatureAvailability? Battery { get; set; }
        public FeatureAvailability? NoiseControl { get; set; }
        public FeatureAvailability? Multipoint { get; set; }
        public FeatureAvailability? Equalizer { get; set; }
        public FeatureAvailability? Dsee { get; set; }
    }

    private sealed class FeatureAvailability
    {
        public string? Availability { get; set; }
        public string? Error { get; set; }
    }

    private sealed class CapabilityState
    {
        public JsonElement NoiseControl { get; set; }
        public bool Anc { get; set; }
        public bool Ambient { get; set; }
        public bool PowerOff { get; set; }
        public bool Equalizer { get; set; }
        public bool Dsee { get; set; }
        public bool TenBandEqualizer { get; set; }
        public bool ClearBass { get; set; }
    }

    private sealed class EqualizerState
    {
        public int Preset { get; set; }
        public string? PresetName { get; set; }
        public int ClearBass { get; set; }
        public List<int> Bands { get; set; } = [];
    }

    private sealed class MultipointState
    {
        public bool Supported { get; set; }
        public bool SourceSwitchSupported { get; set; }
        public bool SourceKeepSupported { get; set; }
        public int? InquiredType { get; set; }
        public int? MaxConnected { get; set; }
        public bool SourceKeeping { get; set; }
        public int? PlaybackIndex { get; set; }
        public string? Error { get; set; }
        public List<MultipointDevice> Devices { get; set; } = [];
        [JsonPropertyName("historyDevices")] public List<MultipointDevice> HistoryDevices { get; set; } = [];
        [JsonPropertyName("history")] public List<MultipointDevice> History { get; set; } = [];
    }

    private sealed class MultipointDevice
    {
        public string? Address { get; set; }
        public string? Name { get; set; }
        public int? ConnectionIndex { get; set; }
        public int? DeviceClass { get; set; }
        public bool Connected { get; set; }
        public bool Playing { get; set; }
    }

    private readonly ComboBox _headsetSelector = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 270, FlatStyle = FlatStyle.Flat };
    private readonly Label _connectionBadge = new() { AutoSize = true };
    private readonly Label _headsetName = new() { AutoSize = true, Font = new Font("Segoe UI", 20, FontStyle.Bold) };
    private readonly Label _headsetAddress = new() { AutoSize = true, ForeColor = Color.FromArgb(103, 108, 120) };
    private readonly Label _status = new() { AutoSize = true };
    private readonly Label _batteryText = new() { AutoSize = true, Font = new Font("Segoe UI", 18, FontStyle.Bold) };
    private readonly Label _batteryDetail = new() { AutoSize = true, ForeColor = Color.FromArgb(103, 108, 120) };
    private readonly ProgressBar _batteryBar = new() { Minimum = 0, Maximum = 100, Height = 8, Style = ProgressBarStyle.Continuous };
    private readonly ComboBox _noiseMode = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 165, FlatStyle = FlatStyle.Flat };
    private readonly ComboBox _equalizerPreset = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 168, FlatStyle = FlatStyle.Flat };
    private readonly Button _equalizerCustom = new() { Text = "自定义…", AutoSize = true };
    private readonly CheckBox _dsee = new() { Text = "DSEE", AutoSize = true };
    private readonly Button _refresh = new() { Text = "刷新", AutoSize = true };
    private readonly Button _powerOff = new() { Text = "关闭耳机", AutoSize = true };
    private readonly CheckBox _startup = new() { Text = "登录 Windows 时启动并隐藏到托盘", AutoSize = true };
    private readonly Label _error = new() { AutoSize = true, ForeColor = Color.FromArgb(177, 52, 62), MaximumSize = new Size(820, 0) };
    private readonly Label _sourceSummary = new() { AutoSize = true, ForeColor = Color.FromArgb(80, 85, 98) };
    private readonly FlowLayoutPanel _deviceCards = new() { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, AutoScroll = true, Padding = new Padding(0, 3, 12, 3) };
    private readonly FlowLayoutPanel _historyCards = new() { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, AutoScroll = true, Padding = new Padding(0, 3, 12, 3) };
    private readonly System.Windows.Forms.Timer _timer = new() { Interval = DashboardRefreshIntervalMs };
    private readonly NotifyIcon _trayIcon = new() { Visible = true, Text = "Sony Device Center" };
    private readonly ContextMenuStrip _trayMenu = new();
    private readonly bool _preview;
    private readonly ToolStripMenuItem _trayModeMenu = new("切换降噪模式");
    private readonly ToolStripMenuItem _trayPowerOff = new("关闭耳机");
    private readonly List<Button> _deviceActionButtons = [];
    private readonly CancellationTokenSource _lifetime = new();
    private CancellationTokenSource? _refreshCancellation;
    private Task _refreshTask = Task.CompletedTask;
    private bool _refreshIsBatteryOnly;
    private bool _fullRefreshRequested;
    private bool _needsFullRefresh = true;
    private bool _loadingCandidates;
    private bool _inTray;
    private string? _renderedMultipoint;
    private int? _displayedTrayBattery;
    private bool? _displayedTrayConnected;
    private bool _displayedTrayFresh;
    private DateTime? _lastMultipointRefreshUtc;
    private Dashboard? _dashboard;
    private string? _selectedHeadsetAddress;
    private bool _busy;
    private bool _updatingUi;
    private bool _exitRequested;
    private bool _resourcesDisposed;
    private readonly bool _startInTray;
    private DateTime? _lastBatteryRefreshUtc;
    private Icon? _trayBatteryIcon;
    private Image? _headsetImage;
    private readonly SonyCtlSession _session = new();
    private readonly object _notificationSync = new();
    private string? _pendingNotificationAddress;
    private string? _pendingNotificationJson;
    private bool _notificationPosted;

    public MainForm(bool startInTray = false, bool preview = false, string? screenshotPath = null)
    {
        _startInTray = startInTray;
        _preview = preview;
        Text = "Sony Device Center";
        var launcherPath = Path.Combine(AppContext.BaseDirectory, "SonyDeviceCenter.exe");
        if (File.Exists(launcherPath)) Icon = Icon.ExtractAssociatedIcon(launcherPath);
        Width = 1120;
        Height = 840;
        MinimumSize = new Size(880, 680);
        StartPosition = FormStartPosition.CenterScreen;
        ShowInTaskbar = !startInTray;
        if (startInTray) WindowState = FormWindowState.Minimized;
        BackColor = Color.FromArgb(247, 246, 249);
        ForeColor = Ink;
        Font = new Font("Segoe UI", 9.5f);
        AutoScaleMode = AutoScaleMode.Dpi;

        _noiseMode.Items.AddRange(["正常", "降噪", "通透"]);
        _noiseMode.SelectedIndex = 0;
        _equalizerPreset.Items.AddRange([
            new EqualizerPresetOption("off", "关闭"),
            new EqualizerPresetOption("bass-boost", "低音增强"),
            new EqualizerPresetOption("treble-boost", "高音增强"),
            new EqualizerPresetOption("vocal", "人声"),
            new EqualizerPresetOption("bright", "明亮"),
            new EqualizerPresetOption("excited", "激昂"),
            new EqualizerPresetOption("mellow", "柔和"),
            new EqualizerPresetOption("relaxed", "舒缓"),
            new EqualizerPresetOption("speech", "语音"),
            new EqualizerPresetOption("manual", "自定义")]);
        _equalizerPreset.SelectedIndex = 0;
        _headsetSelector.SelectedIndexChanged += async (_, _) => await HeadsetSelectionChangedAsync();
        _noiseMode.SelectedIndexChanged += async (_, _) => await NoiseModeChangedAsync();
        _equalizerPreset.SelectedIndexChanged += async (_, _) => await EqualizerPresetChangedAsync();
        _equalizerCustom.Click += async (_, _) => await EditEqualizerAsync();
        _dsee.CheckedChanged += async (_, _) => await DseeChangedAsync();
        _refresh.Click += async (_, _) => await RefreshDashboardAsync();
        _powerOff.Click += async (_, _) => await PowerOffAsync();
        _startup.Checked = IsStartupEnabled();
        _startup.CheckedChanged += (_, _) => UpdateStartupSetting();
        _timer.Tick += async (_, _) => await RefreshDashboardAsync(automatic: true);
        _trayBatteryIcon = CreateBatteryIcon(null);
        _trayIcon.Icon = _trayBatteryIcon;
        BuildTrayMenu();
        _trayIcon.ContextMenuStrip = _trayMenu;
        _trayIcon.DoubleClick += (_, _) => RestoreFromTray();

        Controls.Add(BuildRootLayout());
        _session.StateChanged += OnBackendStateChanged;
        _deviceCards.Resize += (_, _) => ResizeDeviceCards(_deviceCards);
        _historyCards.Resize += (_, _) => ResizeDeviceCards(_historyCards);
        RenderDisconnected("");
        _status.Text = "正在读取耳机状态…";
        FormClosing += OnFormClosing;
        Shown += async (_, _) =>
        {
            if (_preview)
            {
                _dashboard = CreatePreviewDashboard();
                EnsureHeadsetCandidate(_dashboard.Name!, _dashboard.Address!);
                SelectHeadset(_dashboard.Address!);
                RenderDashboard(_dashboard);
                _startup.Enabled = false;
                _refresh.Enabled = false;
                _headsetSelector.Enabled = false;
                if (!string.IsNullOrWhiteSpace(screenshotPath))
                {
                    await Task.Delay(150);
                    using var screenshot = new Bitmap(Width, Height);
                    DrawToBitmap(screenshot, new Rectangle(Point.Empty, Size));
                    screenshot.Save(screenshotPath, System.Drawing.Imaging.ImageFormat.Png);
                    _exitRequested = true;
                    Close();
                }
                return;
            }
            if (_startInTray) HideToTray();
            await LoadHeadsetCandidatesAsync();
            await RefreshDashboardAsync();
            if (!IsStopping) _timer.Start();
        };
    }

    private Control BuildRootLayout()
    {
        var root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 4, Padding = new Padding(28, 22, 28, 20) };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 66));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 212));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.Controls.Add(BuildHeader(), 0, 0);
        root.Controls.Add(BuildOverview(), 0, 1);
        root.Controls.Add(BuildMultipoint(), 0, 2);
        root.Controls.Add(BuildFooter(), 0, 3);
        return root;
    }

    private Control BuildHeader()
    {
        var panel = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3, RowCount = 1 };
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        var title = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, FlowDirection = FlowDirection.TopDown, Margin = new Padding(0) };
        title.Controls.Add(new Label { Text = "Sony Device Center", AutoSize = true, Font = new Font("Segoe UI", 14, FontStyle.Bold), ForeColor = Color.FromArgb(35, 37, 47) });
        title.Controls.Add(new Label { Text = _preview ? "界面预览 · 以下均为示例数据" : "设备状态 · 多点连接 · 听音控制", AutoSize = true, ForeColor = _preview ? Color.FromArgb(177, 92, 12) : Muted });
        panel.Controls.Add(title, 0, 0);
        _headsetSelector.Margin = new Padding(10, 17, 14, 0);
        panel.Controls.Add(_headsetSelector, 1, 0);
        _connectionBadge.Margin = new Padding(0, 19, 0, 0);
        panel.Controls.Add(_connectionBadge, 2, 0);
        return panel;
    }

    private Control BuildOverview()
    {
        var card = new RoundedPanel { Dock = DockStyle.Fill, Padding = new Padding(20, 18, 20, 16), Margin = new Padding(0) };
        var panel = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 4, RowCount = 2, BackColor = Color.White };
        panel.RowStyles.Add(new RowStyle(SizeType.Percent, 62));
        panel.RowStyles.Add(new RowStyle(SizeType.Percent, 38));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 112));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 43));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 28));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 29));
        _headsetImage = LoadHeadsetImage();
        panel.Controls.Add(new PictureBox { Image = _headsetImage, Dock = DockStyle.Fill, SizeMode = PictureBoxSizeMode.Zoom, Margin = new Padding(0, 0, 16, 0) }, 0, 0);
        panel.SetRowSpan(panel.Controls[0], 2);
        var identity = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, Margin = new Padding(0, 19, 0, 0) };
        _headsetName.Margin = new Padding(0, 0, 0, 8);
        _headsetName.Font = new Font("Segoe UI", 18, FontStyle.Bold);
        _headsetAddress.Margin = new Padding(0, 0, 0, 8);
        _status.Margin = new Padding(0);
        _status.ForeColor = Muted;
        identity.Controls.Add(_headsetName);
        identity.Controls.Add(_headsetAddress);
        identity.Controls.Add(_status);
        panel.Controls.Add(identity, 1, 0);

        var battery = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, Margin = new Padding(0, 4, 0, 0) };
        battery.Controls.Add(new Label { Text = "耳机电量", AutoSize = true, ForeColor = Color.FromArgb(103, 108, 120) });
        battery.Controls.Add(_batteryText);
        _batteryBar.Width = 150;
        battery.Controls.Add(_batteryBar);
        battery.Controls.Add(_batteryDetail);
        panel.Controls.Add(battery, 2, 0);

        var controls = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, Margin = new Padding(0, 2, 0, 0) };
        controls.Controls.Add(new Label { Text = "快捷控制", AutoSize = true, ForeColor = Color.FromArgb(103, 108, 120) });
        _noiseMode.Margin = new Padding(0, 3, 0, 5);
        controls.Controls.Add(_noiseMode);
        _powerOff.BackColor = Color.FromArgb(252, 239, 240);
        _powerOff.FlatStyle = FlatStyle.Flat;
        _powerOff.FlatAppearance.BorderColor = Color.FromArgb(239, 190, 194);
        controls.Controls.Add(_powerOff);
        panel.Controls.Add(controls, 3, 0);

        var soundControls = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, FlowDirection = FlowDirection.LeftToRight, Margin = new Padding(0, 2, 0, 0), Padding = new Padding(0, 5, 0, 0) };
        soundControls.Controls.Add(new Label { Text = "均衡器", AutoSize = true, ForeColor = Color.FromArgb(103, 108, 120), Margin = new Padding(0, 7, 8, 0) });
        soundControls.Controls.Add(_equalizerPreset);
        soundControls.Controls.Add(_equalizerCustom);
        soundControls.Controls.Add(new Label { Width = 16 });
        soundControls.Controls.Add(_dsee);
        panel.Controls.Add(soundControls, 1, 1);
        panel.SetColumnSpan(soundControls, 3);
        card.Controls.Add(panel);
        return card;
    }

    private Control BuildMultipoint()
    {
        var outer = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 3, Padding = new Padding(0, 18, 0, 0) };
        outer.RowStyles.Add(new RowStyle(SizeType.Absolute, 58));
        outer.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        outer.RowStyles.Add(new RowStyle(SizeType.Absolute, 130));
        var heading = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2 };
        heading.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        heading.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        var text = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, Margin = new Padding(0) };
        text.Controls.Add(new Label { Text = "多点连接", AutoSize = true, Font = new Font("Segoe UI", 14, FontStyle.Bold), ForeColor = Color.FromArgb(35, 37, 47) });
        text.Controls.Add(_sourceSummary);
        heading.Controls.Add(text, 0, 0);
        _refresh.Margin = new Padding(0, 3, 0, 0);
        heading.Controls.Add(_refresh, 1, 0);
        outer.Controls.Add(heading, 0, 0);

        outer.Controls.Add(BuildDeviceSection("当前连接", _deviceCards), 0, 1);
        outer.Controls.Add(BuildDeviceSection("配对记录", _historyCards), 0, 2);
        return outer;
    }

    private static Control BuildDeviceSection(string title, Control list)
    {
        var section = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 2, Padding = new Padding(0), Margin = new Padding(0, 0, 0, 7) };
        section.RowStyles.Add(new RowStyle(SizeType.Absolute, 24));
        section.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        section.Controls.Add(new Label { Text = title, AutoSize = true, ForeColor = Muted, Margin = new Padding(2, 0, 0, 0) }, 0, 0);
        section.Controls.Add(list, 0, 1);
        return section;
    }

    private Control BuildFooter()
    {
        var panel = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, AutoSize = true, Margin = new Padding(0, 4, 0, 0) };
        panel.Controls.Add(_startup);
        panel.Controls.Add(_error);
        return panel;
    }

    private void BuildTrayMenu()
    {
        var title = new ToolStripMenuItem("Sony Device Center") { Name = "trayTitle" };
        title.Click += (_, _) => RestoreFromTray();
        _trayMenu.Items.Add(title);
        _trayMenu.Items.Add(new ToolStripSeparator());
        var normal = new ToolStripMenuItem("正常");
        normal.Click += async (_, _) => await ApplyNoiseModeAsync("off");
        var anc = new ToolStripMenuItem("降噪");
        anc.Click += async (_, _) => await ApplyNoiseModeAsync("anc");
        var ambient = new ToolStripMenuItem("通透");
        ambient.Click += async (_, _) => await ApplyNoiseModeAsync("ambient");
        _trayModeMenu.DropDownItems.AddRange([normal, anc, ambient]);
        _trayMenu.Items.Add(_trayModeMenu);
        _trayPowerOff.Click += async (_, _) => await PowerOffAsync();
        _trayMenu.Items.Add(_trayPowerOff);
        _trayMenu.Items.Add(new ToolStripSeparator());
        var restore = new ToolStripMenuItem("打开主窗口");
        restore.Click += (_, _) => RestoreFromTray();
        _trayMenu.Items.Add(restore);
        var exit = new ToolStripMenuItem("退出");
        exit.Click += async (_, _) => await ExitApplicationAsync();
        _trayMenu.Items.Add(exit);
    }

    private async Task LoadHeadsetCandidatesAsync()
    {
        if (IsStopping || _preview || _loadingCandidates || _busy) return;
        _loadingCandidates = true;
        SetBusy(_busy);
        try
        {
            await CancelRefreshAsync();
            var result = await RunSonyCtlAsync(["devices"], _lifetime.Token);
            if (IsStopping) return;
            var candidates = new List<HeadsetCandidate>();
            foreach (var line in result.Stdout.Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
            {
                var bracket = line.LastIndexOf(" [", StringComparison.Ordinal);
                if (bracket <= 0 || !line.EndsWith(']')) continue;
                var address = line[(bracket + 2)..^1].Trim().ToUpperInvariant();
                if (address.Length < 11) continue;
                candidates.Add(new HeadsetCandidate(line[..bracket].Trim(), address));
            }
            _headsetSelector.BeginUpdate();
            _updatingUi = true;
            try
            {
                _headsetSelector.Items.Clear();
                foreach (var item in candidates) _headsetSelector.Items.Add(item);
                if (_selectedHeadsetAddress is not null) SelectHeadset(_selectedHeadsetAddress);
                if (_headsetSelector.SelectedIndex < 0 && _headsetSelector.Items.Count > 0) _headsetSelector.SelectedIndex = 0;
                if (_headsetSelector.SelectedItem is HeadsetCandidate selected) _selectedHeadsetAddress = selected.Address;
            }
            finally { _updatingUi = false; _headsetSelector.EndUpdate(); }
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) { SetError("无法读取电脑端耳机列表：" + ex.Message); }
        finally { _loadingCandidates = false; if (!IsStopping) SetBusy(_busy); }
    }

    private async Task HeadsetSelectionChangedAsync()
    {
        if (_preview || _updatingUi || _busy || IsStopping) return;
        if (_headsetSelector.SelectedItem is not HeadsetCandidate candidate) return;
        _selectedHeadsetAddress = candidate.Address;
        RenderDisconnected("");
        _fullRefreshRequested = true;
        _refreshCancellation?.Cancel();
        await RefreshDashboardAsync();
    }

    private bool IsStopping => _exitRequested || IsDisposed || Disposing;

    private async Task RefreshDashboardAsync(bool automatic = false)
    {
        if (IsStopping || _preview || _busy || _loadingCandidates) return;
        if (!automatic) _fullRefreshRequested = true;
        if (!_refreshTask.IsCompleted)
        {
            // A restore/device change needs a full snapshot. Ordinary timer
            // ticks coalesce; foreground actions cancel and await this task.
            if (!automatic && (_refreshIsBatteryOnly || _needsFullRefresh)) _refreshCancellation?.Cancel();
            await _refreshTask;
            return;
        }
        _refreshTask = RefreshLoopAsync();
        await _refreshTask;
    }

    private async Task RefreshLoopAsync()
    {
        do
        {
            var requestedFullRefresh = _fullRefreshRequested;
            var batteryOnly = _inTray && !_fullRefreshRequested && !_needsFullRefresh && _dashboard is not null;
            var forceBattery = _fullRefreshRequested || _needsFullRefresh;
            var refreshMultipoint = requestedFullRefresh || _dashboard?.Multipoint is null || !_lastMultipointRefreshUtc.HasValue ||
                DateTime.UtcNow - _lastMultipointRefreshUtc.Value >= TimeSpan.FromMilliseconds(MultipointRefreshIntervalMs);
            _fullRefreshRequested = false;
            _refreshIsBatteryOnly = batteryOnly;
            using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(_lifetime.Token);
            _refreshCancellation = cancellation;
            SetBusy(_busy);
            try { await ReadDashboardAsync(batteryOnly, forceBattery, refreshMultipoint, cancellation.Token); }
            catch (OperationCanceledException) { }
            catch (Exception ex) { if (!IsStopping) RenderRefreshFailure(ex.Message); }
            finally { _refreshCancellation = null; if (!IsStopping) SetBusy(_busy); }
        } while (_fullRefreshRequested && !_busy && !_loadingCandidates && !IsStopping);
        if (!IsStopping) RestartRefreshTimer();
    }

    private async Task CancelRefreshAsync()
    {
        _fullRefreshRequested = false;
        _refreshCancellation?.Cancel();
        await _refreshTask;
    }

    private async Task ReadDashboardAsync(bool batteryOnly, bool forceBattery, bool refreshMultipoint, CancellationToken cancellationToken)
    {
        // Battery reads stay at 30 seconds while visible settings update every 15.
        // Separate requests let a foreground action cancel the remaining refresh.
        var batteryDue = forceBattery || !_lastBatteryRefreshUtc.HasValue ||
            DateTime.UtcNow - _lastBatteryRefreshUtc.Value >= TimeSpan.FromMilliseconds(BatteryRefreshIntervalMs);
        var features = batteryOnly ? new[] { "battery" } : batteryDue
            ? new[] { "battery", "noise", "eq", "dsee" }
            : new[] { "noise", "eq", "dsee" };
        if (!batteryOnly && refreshMultipoint) features = [.. features, "multipoint"];
        foreach (var feature in features) await ReadFeatureAsync(feature, cancellationToken);
        _needsFullRefresh = false;
    }

    private async Task ReadFeatureAsync(string feature, CancellationToken cancellationToken)
    {
        try
        {
            var address = _selectedHeadsetAddress;
            var args = feature == "snapshot" ? new[] { "snapshot" } : new[] { "refresh", feature };
            var result = await RunSonyCtlAsync(args, cancellationToken);
            cancellationToken.ThrowIfCancellationRequested();
            if (IsStopping || !string.Equals(address, _selectedHeadsetAddress, StringComparison.OrdinalIgnoreCase)) return;
            var dashboard = JsonSerializer.Deserialize<Dashboard>(result.Stdout, JsonOptions) ?? throw new InvalidOperationException("设备状态返回为空。");
            dashboard = RetainLastKnownDashboard(dashboard);
            _dashboard = dashboard;
            if (feature == "battery" && IsAvailable(dashboard.Features?.Battery)) _lastBatteryRefreshUtc = DateTime.UtcNow;
            if (feature == "multipoint") _lastMultipointRefreshUtc = DateTime.UtcNow;
            if (!string.IsNullOrWhiteSpace(dashboard.Address))
            {
                _selectedHeadsetAddress = dashboard.Address;
                EnsureHeadsetCandidate(dashboard.Name ?? "Sony 耳机", dashboard.Address);
                _updatingUi = true;
                try { SelectHeadset(dashboard.Address); }
                finally { _updatingUi = false; }
            }
            SetError("");
            RenderDashboard(dashboard);
            SetBusy(_busy);
        }
        catch (OperationCanceledException) { throw; }
        catch (Exception ex)
        {
            if (feature == "multipoint") _lastMultipointRefreshUtc = DateTime.UtcNow;
            if (!IsStopping) RenderRefreshFailure(ex.Message, feature);
            throw;
        }
    }

    private Dashboard RetainLastKnownDashboard(Dashboard incoming)
    {
        var previous = _dashboard;
        if (previous is null || !string.Equals(previous.Address, incoming.Address, StringComparison.OrdinalIgnoreCase)) return incoming;

        incoming.Features ??= new FeatureState();
        if (!IsAvailable(incoming.Features.Battery) && IsKnown(previous.Features?.Battery) && previous.Battery?.Main is >= 0 and <= 100)
        {
            incoming.Battery = previous.Battery;
            incoming.Features.Battery = StaleFeature(incoming.Features.Battery);
        }
        if (!IsAvailable(incoming.Features.NoiseControl) && IsKnown(previous.Features?.NoiseControl) && previous.NoiseControl?.Mode is not null)
        {
            incoming.NoiseControl = previous.NoiseControl;
            incoming.Features.NoiseControl = StaleFeature(incoming.Features.NoiseControl);
        }
        if (!IsAvailable(incoming.Features.Equalizer) && IsKnown(previous.Features?.Equalizer) && previous.Equalizer is not null)
        {
            incoming.Equalizer = previous.Equalizer;
            incoming.Features.Equalizer = StaleFeature(incoming.Features.Equalizer);
        }
        if (!IsAvailable(incoming.Features.Dsee) && IsKnown(previous.Features?.Dsee))
        {
            incoming.Dsee = previous.Dsee;
            incoming.Features.Dsee = StaleFeature(incoming.Features.Dsee);
        }
        if (!IsAvailable(incoming.Features.Multipoint) && IsKnown(previous.Features?.Multipoint) && previous.Multipoint is not null)
        {
            incoming.Multipoint = previous.Multipoint;
            incoming.Features.Multipoint = StaleFeature(incoming.Features.Multipoint);
        }
        return incoming;
    }

    private static bool IsKnown(FeatureAvailability? feature) =>
        string.Equals(feature?.Availability, "valid", StringComparison.OrdinalIgnoreCase) ||
        string.Equals(feature?.Availability, "stale", StringComparison.OrdinalIgnoreCase);

    private static FeatureAvailability StaleFeature(FeatureAvailability? feature) => new()
    {
        Availability = "stale",
        Error = string.IsNullOrWhiteSpace(feature?.Error) ? "等待耳机重新报告状态" : feature.Error
    };

    private void OnBackendStateChanged(string address, string snapshotJson)
    {
        if (IsStopping) return;
        var post = false;
        lock (_notificationSync)
        {
            _pendingNotificationAddress = address;
            _pendingNotificationJson = snapshotJson;
            if (!_notificationPosted) { _notificationPosted = true; post = true; }
        }
        if (!post) return;
        try
        {
            BeginInvoke((Action)DrainBackendNotification);
        }
        catch (InvalidOperationException) { lock (_notificationSync) _notificationPosted = false; }
    }

    private void DrainBackendNotification()
    {
        string? address;
        string? snapshotJson;
        lock (_notificationSync)
        {
            _notificationPosted = false;
            if (IsStopping || _busy || _loadingCandidates) return;
            address = _pendingNotificationAddress;
            snapshotJson = _pendingNotificationJson;
            _pendingNotificationAddress = null;
            _pendingNotificationJson = null;
        }
        if (address is null || snapshotJson is null ||
            !string.Equals(address, _selectedHeadsetAddress, StringComparison.OrdinalIgnoreCase)) return;
        try
        {
            var dashboard = JsonSerializer.Deserialize<Dashboard>(snapshotJson, JsonOptions);
            if (dashboard is null) return;
            dashboard = RetainLastKnownDashboard(dashboard);
            var batteryChanged = !Equals(_dashboard?.Battery?.Main, dashboard.Battery?.Main);
            _dashboard = dashboard;
            if (batteryChanged && IsAvailable(dashboard.Features?.Battery) && dashboard.Battery?.Main is not null)
                _lastBatteryRefreshUtc = DateTime.UtcNow;
            if (!dashboard.Connected) _needsFullRefresh = true;
            RenderDashboard(dashboard);
        }
        catch (Exception ex) { if (!IsStopping) RenderRefreshFailure(ex.Message); }
    }

    private void RenderRefreshFailure(string error, string? failedFeature = null)
    {
        _needsFullRefresh = true;
        if (_dashboard is null) { RenderDisconnected(error); return; }
        _dashboard.Features ??= new FeatureState();
        switch (failedFeature)
        {
            case "battery": _dashboard.Features.Battery = new FeatureAvailability { Availability = "stale", Error = error }; break;
            case "noise": _dashboard.Features.NoiseControl = new FeatureAvailability { Availability = "stale", Error = error }; break;
            case "eq": _dashboard.Features.Equalizer = new FeatureAvailability { Availability = "stale", Error = error }; break;
            case "dsee": _dashboard.Features.Dsee = new FeatureAvailability { Availability = "stale", Error = error }; break;
            case "multipoint": _dashboard.Features.Multipoint = new FeatureAvailability { Availability = "stale", Error = error }; break;
        }
        RenderDashboard(_dashboard);
        _connectionBadge.Text = "● 状态待确认";
        _connectionBadge.ForeColor = Muted;
        _status.Text = "刷新失败，将自动重试";
        SetError(error);
    }

    private async Task ExecuteActionAsync(string title, Func<CancellationToken, Task> action, string? refreshFeature = "snapshot")
    {
        if (IsStopping || _preview || _busy || _loadingCandidates) return;
        SetBusy(true);
        _timer.Stop();
        try
        {
            await CancelRefreshAsync();
            if (IsStopping) return;
            SetError("");
            await action(_lifetime.Token);
            if (IsStopping) return;
            if (refreshFeature is not null)
            {
                try { await ReadFeatureAsync(refreshFeature, _lifetime.Token); }
                catch (OperationCanceledException) { throw; }
                catch (Exception ex)
                {
                    RenderRefreshFailure(ex.Message);
                    SetError(title + "已执行，但状态回读失败：" + ex.Message);
                }
            }
        }
        catch (OperationCanceledException) { }
        catch (Exception ex)
        {
            if (!IsStopping)
            {
                RenderRefreshFailure(title + "失败：" + ex.Message);
            }
        }
        finally
        {
            if (!IsStopping) { SetBusy(false); RestartRefreshTimer(); }
        }
    }

    private void RenderDashboard(Dashboard dashboard)
    {
        var connected = dashboard.Connected;
        _headsetName.Text = string.IsNullOrWhiteSpace(dashboard.Name) ? "Sony 耳机" : dashboard.Name;
        _headsetAddress.Text = string.IsNullOrWhiteSpace(dashboard.Address) ? "地址未知" : dashboard.Address;
        _status.Text = connected ? "已连接到这台电脑" : "未连接到这台电脑";
        _connectionBadge.Text = connected ? "● 已连接" : "● 未连接";
        _connectionBadge.ForeColor = connected ? Color.FromArgb(27, 131, 87) : Color.FromArgb(137, 140, 150);
        RenderBattery(dashboard);
        RenderNoise(dashboard);
        RenderEqualizer(dashboard);
        RenderMultipoint(dashboard);
        _powerOff.Enabled = !_preview && connected && (dashboard.Capabilities?.PowerOff ?? false);
        UpdateTrayBattery(dashboard.Battery?.Main, connected, IsAvailable(dashboard.Features?.Battery));
        var featureErrors = new[]
        {
            FeatureError("电量", dashboard.Features?.Battery),
            FeatureError("降噪", dashboard.Features?.NoiseControl),
            FeatureError("均衡器", dashboard.Features?.Equalizer),
            FeatureError("DSEE", dashboard.Features?.Dsee),
            FeatureError("多点连接", dashboard.Features?.Multipoint)
        }.Where(x => !string.IsNullOrWhiteSpace(x));
        var errorMessage = string.Join("  |  ", featureErrors);
        if (!string.IsNullOrWhiteSpace(errorMessage)) SetError(errorMessage);
    }

    private void RenderBattery(Dashboard dashboard)
    {
        var battery = dashboard.Battery;
        var hasReading = battery?.Main is >= 0 and <= 100;
        var valid = IsAvailable(dashboard.Features?.Battery) && hasReading;
        _batteryText.Text = hasReading ? battery!.Main + "%" : "未知";
        _batteryBar.Value = hasReading ? battery!.Main!.Value : 0;
        _batteryBar.Enabled = valid;
        if (hasReading && battery is not null)
        {
            var parts = new List<string>();
            if (battery.Left is not null) parts.Add("左 " + battery.Left + "%");
            if (battery.Right is not null) parts.Add("右 " + battery.Right + "%");
            if (battery.Case is not null) parts.Add("盒 " + battery.Case + "%");
            var detail = parts.Count == 0 ? "" : string.Join("  ·  ", parts) + "  ·  ";
            _batteryDetail.Text = valid ? detail + (battery.Charging ? "充电中" : "") : detail + "上次读取，状态待确认";
        }
        else _batteryDetail.Text = AvailabilityDescription(dashboard.Features?.Battery, "电量未提供");
    }

    private void RenderNoise(Dashboard dashboard)
    {
        var mode = dashboard.NoiseControl?.Mode?.ToLowerInvariant();
        var available = HasNoiseControlCapability(dashboard.Capabilities);
        _updatingUi = true;
        _noiseMode.SelectedIndex = mode switch { "anc" or "cancelling" => 1, "ambient" => 2, "off" => 0, _ => -1 };
        _updatingUi = false;
        _noiseMode.Enabled = !_preview && dashboard.Connected && available;
        _trayModeMenu.Enabled = !_preview && dashboard.Connected && available;
        for (var i = 0; i < _trayModeMenu.DropDownItems.Count; i++)
            if (_trayModeMenu.DropDownItems[i] is ToolStripMenuItem item) item.Checked = i == _noiseMode.SelectedIndex;
    }

    private void RenderEqualizer(Dashboard dashboard)
    {
        var capability = dashboard.Capabilities;
        var equalizerAvailable = dashboard.Connected && capability?.Equalizer == true;
        _updatingUi = true;
        try
        {
            EnsureEqualizerPresetOptions(capability?.TenBandEqualizer == true);
            var presetId = dashboard.Equalizer?.Preset switch
            {
                0x10 => "bright", 0x11 => "excited", 0x12 => "mellow", 0x13 => "relaxed",
                0x14 => "vocal", 0x15 => "treble-boost", 0x16 => "bass-boost", 0x17 => "speech",
                0xa0 => "manual", _ => "off"
            };
            _equalizerPreset.SelectedIndex = _equalizerPreset.Items.Cast<EqualizerPresetOption>()
                .Select((option, index) => (option, index))
                .FirstOrDefault(item => item.option.Id == presetId).index;
            if (IsAvailable(dashboard.Features?.Dsee)) _dsee.Checked = dashboard.Dsee;
        }
        finally { _updatingUi = false; }

        _equalizerPreset.Enabled = !_preview && equalizerAvailable && !_busy;
        _equalizerCustom.Enabled = !_preview && equalizerAvailable && !_busy;
        _dsee.Enabled = !_preview && dashboard.Connected && capability?.Dsee == true && !_busy;
    }

    private void EnsureEqualizerPresetOptions(bool tenBand)
    {
        var expected = tenBand
            ? new[] { new EqualizerPresetOption("off", "关闭"), new EqualizerPresetOption("manual", "自定义") }
            : new[]
            {
                new EqualizerPresetOption("off", "关闭"), new EqualizerPresetOption("bass-boost", "低音增强"),
                new EqualizerPresetOption("treble-boost", "高音增强"), new EqualizerPresetOption("vocal", "人声"),
                new EqualizerPresetOption("bright", "明亮"), new EqualizerPresetOption("excited", "激昂"),
                new EqualizerPresetOption("mellow", "柔和"), new EqualizerPresetOption("relaxed", "舒缓"),
                new EqualizerPresetOption("speech", "语音"), new EqualizerPresetOption("manual", "自定义")
            };
        var current = _equalizerPreset.Items.Cast<EqualizerPresetOption>().Select(option => option.Id);
        if (current.SequenceEqual(expected.Select(option => option.Id))) return;
        _equalizerPreset.Items.Clear();
        _equalizerPreset.Items.AddRange(expected);
    }

    private async Task EqualizerPresetChangedAsync()
    {
        if (_updatingUi || _busy || _equalizerPreset.SelectedItem is not EqualizerPresetOption option) return;
        await ApplyEqualizerPresetAsync(option.Id);
    }

    private Task ApplyEqualizerPresetAsync(string presetId)
    {
        if (_preview || _busy || _dashboard?.Connected != true || _dashboard.Capabilities?.Equalizer != true) return Task.CompletedTask;
        return ExecuteActionAsync("设置均衡器", async token =>
        {
            await RunSonyCtlAsync(["eq", "preset", presetId], token);
        }, refreshFeature: "eq");
    }

    private async Task EditEqualizerAsync()
    {
        if (_preview || _busy || _dashboard?.Connected != true || _dashboard.Equalizer is null) return;
        var tenBand = _dashboard.Capabilities?.TenBandEqualizer == true;
        var count = tenBand ? 10 : 5;
        var labels = tenBand
            ? new[] { "31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k" }
            : new[] { "400", "1k", "2.5k", "6.3k", "16k" };
        var low = tenBand ? 0 : -10;
        var high = tenBand ? 12 : 10;
        var initial = _dashboard.Equalizer;
        using var editor = new Form
        {
            Text = "自定义均衡器",
            ClientSize = new Size(count * 76 + 48, tenBand ? 410 : 500),
            FormBorderStyle = FormBorderStyle.FixedDialog,
            MaximizeBox = false,
            MinimizeBox = false,
            ShowInTaskbar = false,
            StartPosition = FormStartPosition.CenterParent,
            Font = Font
        };
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(16), ColumnCount = 1, RowCount = tenBand ? 2 : 3 };
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        if (!tenBand) layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 76));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 46));
        var grid = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = count, RowCount = 1, Padding = new Padding(0) };
        grid.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        var sliders = new List<TrackBar>(count);
        for (var index = 0; index < count; index++)
        {
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f / count));
            var initialValue = index < initial.Bands.Count ? Math.Clamp(initial.Bands[index], low, high) : 0;
            var value = new Label { Text = initialValue.ToString(), AutoSize = true, TextAlign = ContentAlignment.MiddleCenter, Dock = DockStyle.Fill };
            var slider = new TrackBar
            {
                Dock = DockStyle.Fill,
                Orientation = Orientation.Vertical,
                Minimum = low,
                Maximum = high,
                Value = initialValue,
                TickFrequency = tenBand ? 2 : 5,
                TickStyle = TickStyle.Both,
                SmallChange = 1,
                LargeChange = 1
            };
            slider.ValueChanged += (_, _) => value.Text = slider.Value.ToString();
            sliders.Add(slider);
            var column = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 3, Margin = new Padding(1) };
            column.RowStyles.Add(new RowStyle(SizeType.Absolute, 24));
            column.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            column.RowStyles.Add(new RowStyle(SizeType.Absolute, 24));
            column.Controls.Add(new Label { Text = labels[index], AutoSize = true, TextAlign = ContentAlignment.MiddleCenter, Dock = DockStyle.Fill }, 0, 0);
            column.Controls.Add(slider, 0, 1);
            column.Controls.Add(value, 0, 2);
            grid.Controls.Add(column, index, 0);
        }
        layout.Controls.Add(grid, 0, 0);

        TrackBar? clearBass = null;
        if (!tenBand)
        {
            var bassRow = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3, RowCount = 1 };
            bassRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 96));
            bassRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            bassRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 40));
            var value = new Label { Text = initial.ClearBass.ToString(), AutoSize = true, TextAlign = ContentAlignment.MiddleCenter, Dock = DockStyle.Fill };
            clearBass = new TrackBar { Dock = DockStyle.Fill, Minimum = -10, Maximum = 10, Value = Math.Clamp(initial.ClearBass, -10, 10), TickFrequency = 5, SmallChange = 1, LargeChange = 1 };
            clearBass.ValueChanged += (_, _) => value.Text = clearBass.Value.ToString();
            bassRow.Controls.Add(new Label { Text = "Clear Bass", AutoSize = true, TextAlign = ContentAlignment.MiddleLeft, Dock = DockStyle.Fill }, 0, 0);
            bassRow.Controls.Add(clearBass, 1, 0);
            bassRow.Controls.Add(value, 2, 0);
            layout.Controls.Add(bassRow, 0, 1);
        }

        var buttons = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft, WrapContents = false };
        var apply = new Button { Text = "应用", DialogResult = DialogResult.OK, Width = 84, Height = 30 };
        var cancel = new Button { Text = "取消", DialogResult = DialogResult.Cancel, Width = 84, Height = 30 };
        buttons.Controls.Add(apply);
        buttons.Controls.Add(cancel);
        layout.Controls.Add(buttons, 0, tenBand ? 1 : 2);
        editor.AcceptButton = apply;
        editor.CancelButton = cancel;
        editor.Controls.Add(layout);
        if (editor.ShowDialog(this) != DialogResult.OK) return;
        await ApplyCustomEqualizerAsync(clearBass?.Value ?? 0, sliders.Select(slider => slider.Value).ToArray());
    }

    private Task ApplyCustomEqualizerAsync(int clearBass, IReadOnlyList<int> bands)
    {
        if (_preview || _busy || _dashboard?.Connected != true || _dashboard.Capabilities?.Equalizer != true) return Task.CompletedTask;
        var tenBand = _dashboard.Capabilities.TenBandEqualizer;
        var command = new List<string> { "eq", "custom" };
        if (!tenBand) command.Add(Math.Clamp(clearBass, -10, 10).ToString());
        var count = tenBand ? 10 : 5;
        command.AddRange(bands.Take(count).Select(value => Math.Clamp(value, tenBand ? 0 : -10, tenBand ? 12 : 10).ToString()));
        if (bands.Count < count) command.AddRange(Enumerable.Repeat("0", count - bands.Count));
        return ExecuteActionAsync("应用自定义均衡器", async token =>
        {
            await RunSonyCtlAsync(command, token);
        }, refreshFeature: "eq");
    }

    private async Task DseeChangedAsync()
    {
        if (_updatingUi || _busy) return;
        await ApplyDseeAsync(_dsee.Checked);
    }

    private Task ApplyDseeAsync(bool enabled)
    {
        if (_preview || _busy || _dashboard?.Connected != true || _dashboard.Capabilities?.Dsee != true) return Task.CompletedTask;
        return ExecuteActionAsync("设置 DSEE", async token =>
        {
            await RunSonyCtlAsync(["dsee", enabled ? "on" : "off"], token);
        }, refreshFeature: "dsee");
    }

    private void RenderMultipoint(Dashboard dashboard)
    {
        var multi = dashboard.Multipoint;
        var supported = dashboard.Connected && multi?.Supported == true && IsAvailable(dashboard.Features?.Multipoint);
        var renderKey = supported + ":" + JsonSerializer.Serialize(multi, JsonOptions);
        if (_renderedMultipoint == renderKey) return;
        _renderedMultipoint = renderKey;
        _deviceActionButtons.Clear();
        _sourceSummary.Text = !supported
            ? dashboard.Features?.Multipoint?.Availability == "unsupported" ? "耳机未提供多点连接能力" : "等待耳机报告多点连接状态"
            : (multi!.SourceKeeping ? "已固定播放源（当前连接期间）" : "未固定播放源") + (multi.MaxConnected is null ? "" : "  ·  最多 " + multi.MaxConnected + " 台");
        var connectedDevices = multi?.Devices.Where(d => d.Connected) ?? Enumerable.Empty<MultipointDevice>();
        RenderCards(_deviceCards, connectedDevices, supported, false, multi);
        var history = (multi?.Devices.Where(d => !d.Connected) ?? Enumerable.Empty<MultipointDevice>())
            .Concat(multi?.HistoryDevices ?? [])
            .Concat(multi?.History ?? [])
            .GroupBy(d => d.Address, StringComparer.OrdinalIgnoreCase)
            .Select(g => g.First());
        RenderCards(_historyCards, history, supported, true, multi);
        if (!supported)
        {
            var message = dashboard.Features?.Multipoint?.Error ?? multi?.Error;
            if (!string.IsNullOrWhiteSpace(message)) SetError("多点连接：" + message);
        }
    }

    private void RenderCards(FlowLayoutPanel panel, IEnumerable<MultipointDevice> devices, bool supported, bool history, MultipointState? multi)
    {
        panel.SuspendLayout();
        foreach (Control child in panel.Controls.Cast<Control>().ToArray()) child.Dispose();
        panel.Controls.Clear();
        var list = devices.Where(d => !string.IsNullOrWhiteSpace(d.Address)).ToList();
        if (list.Count == 0)
        {
            panel.Controls.Add(new Label { Text = supported ? (history ? "暂无历史设备" : "当前没有连接到其他设备") : "等待耳机报告状态", AutoSize = true, ForeColor = Color.FromArgb(125, 128, 140), Margin = new Padding(3, 9, 0, 0) });
            panel.ResumeLayout();
            return;
        }
        foreach (var device in list) panel.Controls.Add(CreateDeviceCard(device, supported && !_preview, history, multi));
        ResizeDeviceCards(panel);
        panel.ResumeLayout();
    }

    private Control CreateDeviceCard(MultipointDevice device, bool supported, bool history, MultipointState? multi)
    {
        var card = new RoundedPanel { Width = Math.Max(560, _deviceCards.ClientSize.Width - 8), Height = history ? 74 : 86, Margin = new Padding(0, 0, 0, 8), Padding = new Padding(15, 10, 12, 9) };
        var name = string.IsNullOrWhiteSpace(device.Name) ? "未命名设备" : device.Name!;
        var title = new Label { Text = name, AutoSize = true, Font = new Font("Segoe UI", 10.5f, FontStyle.Bold), Location = new Point(14, 10) };
        var address = new Label { Text = device.Address, AutoSize = true, ForeColor = Color.FromArgb(115, 119, 130), Location = new Point(14, 37) };
        var isPlaying = device.Connected && device.ConnectionIndex is > 0 &&
                        (device.Playing || (multi?.PlaybackIndex is > 0 && device.ConnectionIndex == multi.PlaybackIndex));
        var marker = new Label { Text = isPlaying ? (multi?.SourceKeeping == true ? "正在播放  ·  固定播放源" : "正在播放") : "", AutoSize = true, ForeColor = Color.FromArgb(88, 74, 190), Location = new Point(14, 60) };
        card.Controls.Add(title);
        card.Controls.Add(address);
        if (!string.IsNullOrWhiteSpace(marker.Text)) card.Controls.Add(marker);
        var actions = new FlowLayoutPanel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, WrapContents = false, FlowDirection = FlowDirection.LeftToRight, Anchor = AnchorStyles.Top | AnchorStyles.Right, Location = new Point(Math.Max(350, card.Width - (history ? 170 : 355)), history ? 19 : 26), Height = 32 };
        var switchButton = MakeActionButton("切换播放", Color.FromArgb(239, 236, 255));
        var keepButton = MakeActionButton(multi?.SourceKeeping == true && isPlaying ? "取消固定" : "设为默认", Color.FromArgb(236, 247, 242));
        var disconnectButton = MakeActionButton(history ? "连接" : "断开", Color.FromArgb(247, 247, 250));
        var unpairButton = MakeActionButton("取消配对", Color.FromArgb(252, 239, 240));
        switchButton.Enabled = supported && !history && multi?.SourceSwitchSupported == true && device.Connected;
        keepButton.Enabled = supported && !history && multi?.SourceKeepSupported == true && device.Connected && (isPlaying || multi.SourceSwitchSupported);
        disconnectButton.Enabled = supported && (history || device.Connected);
        unpairButton.Enabled = supported;
        switchButton.Click += async (_, _) => await MultipointActionAsync("source", device, "切换播放源");
        keepButton.Click += async (_, _) => await MultipointActionAsync(multi?.SourceKeeping == true && isPlaying ? "keep off" : "default", device, "设置默认播放设备");
        disconnectButton.Click += async (_, _) => await MultipointActionAsync(history ? "connect" : "disconnect", device, history ? "连接设备" : "断开设备");
        unpairButton.Click += async (_, _) => await MultipointActionAsync("unpair", device, "取消配对");
        foreach (var button in new[] { switchButton, keepButton, disconnectButton, unpairButton })
        {
            if (history && (button == switchButton || button == keepButton)) { button.Dispose(); continue; }
            button.Tag = button.Enabled;
            _deviceActionButtons.Add(button);
        }
        if (!history) actions.Controls.AddRange([switchButton, keepButton]);
        actions.Controls.AddRange([disconnectButton, unpairButton]);
        card.Controls.Add(actions);
        card.Resize += (_, _) =>
        {
            actions.Left = Math.Max(260, card.ClientSize.Width - actions.PreferredSize.Width - 13);
            title.MaximumSize = new Size(Math.Max(100, actions.Left - 28), 26);
            title.AutoEllipsis = true;
        };
        return card;
    }

    private async Task MultipointActionAsync(string action, MultipointDevice device, string title)
    {
        if (_preview || _busy || string.IsNullOrWhiteSpace(device.Address)) return;
        await ExecuteActionAsync(title, async token =>
        {
            if (action == "unpair" && MessageBox.Show("确定要取消配对“" + (device.Name ?? device.Address) + "”吗？\n如果这是当前电脑，将失去耳机控制连接。", title, MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes) return;
            var args = new List<string> { "multipoint" };
            if (action == "keep off") args.AddRange(["keep", "off"]);
            else args.AddRange([action, device.Address]);
            await RunSonyCtlAsync(args, token);
        });
    }

    private async Task NoiseModeChangedAsync()
    {
        if (_updatingUi || _busy || !_noiseMode.Focused || _noiseMode.SelectedIndex < 0) return;
        var mode = _noiseMode.SelectedIndex switch { 1 => "anc", 2 => "ambient", _ => "off" };
        await ApplyNoiseModeAsync(mode);
    }

    private async Task ApplyNoiseModeAsync(string mode)
    {
        if (_preview || _busy || _dashboard?.Connected != true || !HasNoiseControlCapability(_dashboard.Capabilities)) return;
        await ExecuteActionAsync("切换降噪", async token =>
        {
            var command = mode switch { "anc" => new[] { "anc", "on" }, "ambient" => new[] { "ambient", "10" }, _ => new[] { "anc", "off" } };
            await RunSonyCtlAsync(command, token);
        }, refreshFeature: "noise");
    }

    private async Task PowerOffAsync()
    {
        if (_preview || _busy || _dashboard?.Connected != true || _dashboard.Capabilities?.PowerOff != true) return;
        await ExecuteActionAsync("关闭耳机", async token =>
        {
            if (MessageBox.Show("确定要关闭耳机吗？", "关闭耳机", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes) return;
            await RunSonyCtlAsync(["poweroff"], token);
            if (IsStopping) return;
            RenderDisconnected("");
            _status.Text = "已发送关机命令";
        }, refreshFeature: null);
    }

    private void RenderDisconnected(string error)
    {
        _renderedMultipoint = null;
        _needsFullRefresh = true;
        _deviceActionButtons.Clear();
        _dashboard = null;
        _headsetName.Text = "未读取到耳机";
        _headsetAddress.Text = "请确认耳机已连接，并点击刷新";
        _status.Text = "状态未知";
        _connectionBadge.Text = "● 未知";
        _connectionBadge.ForeColor = Color.FromArgb(137, 140, 150);
        _batteryText.Text = "未知";
        _batteryDetail.Text = "电量未提供";
        _batteryBar.Value = 0;
        _batteryBar.Enabled = false;
        _noiseMode.SelectedIndex = -1;
        _noiseMode.Enabled = false;
        _equalizerPreset.Enabled = false;
        _equalizerCustom.Enabled = false;
        _dsee.Enabled = false;
        _trayModeMenu.Enabled = false;
        _powerOff.Enabled = false;
        _trayPowerOff.Enabled = false;
        _sourceSummary.Text = "多点连接状态未读取";
        RenderCards(_deviceCards, [], false, false, null);
        RenderCards(_historyCards, [], false, true, null);
        SetError(error);
        UpdateTrayBattery(null, false);
    }

    private static bool IsAvailable(FeatureAvailability? feature) => string.Equals(feature?.Availability, "valid", StringComparison.OrdinalIgnoreCase);

    private static string AvailabilityDescription(FeatureAvailability? feature, string fallback)
    {
        return feature?.Availability?.ToLowerInvariant() switch { "unsupported" => "耳机不支持电量查询", "stale" => "电量状态已过期", "unknown" => "等待耳机报告电量", _ => fallback };
    }

    private static string? FeatureError(string featureName, FeatureAvailability? feature) =>
        !IsAvailable(feature) && !string.IsNullOrWhiteSpace(feature?.Error) ? featureName + "：" + feature.Error : null;

    private static bool HasNoiseControlCapability(CapabilityState? capability)
    {
        if (capability is null) return false;
        if (capability.Anc || capability.Ambient) return true;
        if (capability.NoiseControl.ValueKind is JsonValueKind.Null or JsonValueKind.Undefined) return false;
        if (capability.NoiseControl.ValueKind == JsonValueKind.True) return true;
        if (capability.NoiseControl.ValueKind == JsonValueKind.String) return !string.IsNullOrWhiteSpace(capability.NoiseControl.GetString());
        return false;
    }

    private static Button MakeActionButton(string text, Color backColor)
    {
        var button = new Button { Text = text, AutoSize = true, Height = 29, FlatStyle = FlatStyle.Flat, BackColor = backColor, Margin = new Padding(0, 0, 6, 0), Padding = new Padding(7, 0, 7, 0) };
        button.FlatAppearance.BorderColor = Color.FromArgb(218, 218, 226);
        return button;
    }

    private void EnsureHeadsetCandidate(string name, string address)
    {
        if (_headsetSelector.Items.Cast<object>().OfType<HeadsetCandidate>().Any(x => x.Address.Equals(address, StringComparison.OrdinalIgnoreCase))) return;
        _headsetSelector.Items.Add(new HeadsetCandidate(name, address));
    }

    private void SelectHeadset(string address)
    {
        for (var i = 0; i < _headsetSelector.Items.Count; i++)
            if (_headsetSelector.Items[i] is HeadsetCandidate item && item.Address.Equals(address, StringComparison.OrdinalIgnoreCase)) { _headsetSelector.SelectedIndex = i; return; }
    }

    private void SetError(string message) => _error.Text = message;

    private void SetBusy(bool busy)
    {
        _busy = busy;
        UseWaitCursor = busy;
        _refresh.Enabled = !busy && !_preview && !_loadingCandidates && _refreshCancellation is null;
        _refresh.Text = busy ? "处理中…" : _refreshCancellation is not null ? "刷新中…" : "刷新";
        _headsetSelector.Enabled = !busy && !_preview && !_loadingCandidates;
        var canControl = !busy && !_preview && _dashboard?.Connected == true;
        _noiseMode.Enabled = canControl && HasNoiseControlCapability(_dashboard?.Capabilities);
        _trayModeMenu.Enabled = _noiseMode.Enabled;
        _powerOff.Enabled = canControl && _dashboard?.Capabilities?.PowerOff == true;
        _trayPowerOff.Enabled = _powerOff.Enabled;
        var equalizerAvailable = canControl && _dashboard?.Capabilities?.Equalizer == true;
        _equalizerPreset.Enabled = equalizerAvailable;
        _equalizerCustom.Enabled = equalizerAvailable;
        _dsee.Enabled = canControl && _dashboard?.Capabilities?.Dsee == true;
        foreach (var button in _deviceActionButtons)
            if (!button.IsDisposed) button.Enabled = !busy && button.Tag is true;
        if (!busy) DrainBackendNotification();
    }

    private static void ResizeDeviceCards(FlowLayoutPanel panel)
    {
        var width = Math.Max(560, panel.ClientSize.Width - panel.Padding.Horizontal - SystemInformation.VerticalScrollBarWidth - 8);
        foreach (Control child in panel.Controls) if (child is RoundedPanel) child.Width = width;
    }

    private void UpdateTrayBattery(int? battery, bool connected, bool fresh = true)
    {
        if (_displayedTrayBattery == battery && _displayedTrayConnected == connected && _displayedTrayFresh == fresh) return;
        _displayedTrayBattery = battery;
        _displayedTrayConnected = connected;
        _displayedTrayFresh = fresh;
        var text = battery is >= 0 and <= 100
            ? fresh ? $"Sony Device Center · 电量 {battery}%" : $"Sony Device Center · 上次电量 {battery}% · 状态待确认"
            : connected ? "Sony Device Center · 电量未知" : "Sony Device Center · 设备未连接";
        _trayIcon.Text = text.Length > 63 ? "Sony Device Center" : text;
        var previousIcon = _trayBatteryIcon;
        _trayBatteryIcon = CreateBatteryIcon(battery);
        _trayIcon.Icon = _trayBatteryIcon;
        previousIcon?.Dispose();
        if (_trayMenu.Items[0] is ToolStripMenuItem title) title.Text = text;
    }

    private static Icon CreateBatteryIcon(int? battery)
    {
        using var bitmap = new Bitmap(32, 32);
        using (var graphics = Graphics.FromImage(bitmap))
        using (var brush = new SolidBrush(battery is >= 0 and <= 20 ? Color.FromArgb(171, 56, 69) : Accent))
        using (var textBrush = new SolidBrush(Color.White))
        using (var font = new Font("Segoe UI", battery == 100 ? 10.5f : 13f, FontStyle.Bold))
        {
            graphics.SmoothingMode = SmoothingMode.AntiAlias;
            graphics.Clear(Color.Transparent);
            graphics.FillEllipse(brush, 1, 1, 30, 30);
            var value = battery is >= 0 and <= 100 ? battery.ToString() : "?";
            var size = graphics.MeasureString(value, font);
            graphics.DrawString(value, font, textBrush, (32 - size.Width) / 2, (32 - size.Height) / 2 - 1);
        }
        var handle = bitmap.GetHicon();
        try { using var source = Icon.FromHandle(handle); return (Icon)source.Clone(); }
        finally { NativeMethods.DestroyIcon(handle); }
    }

    private static Image? LoadHeadsetImage()
    {
        try
        {
            var path = Path.Combine(AppContext.BaseDirectory, "Assets", "wh-1000xm6.png");
            return File.Exists(path) ? Image.FromFile(path) : null;
        }
        catch { return null; }
    }

    private void RestartRefreshTimer()
    {
        _timer.Stop();
        _timer.Interval = _inTray ? TrayBatteryRefreshIntervalMs : DashboardRefreshIntervalMs;
        if (!IsStopping && !_preview) _timer.Start();
    }

    private void HideToTray()
    {
        _inTray = true;
        Hide();
        RestartRefreshTimer();
    }

    private void RestoreFromTray()
    {
        if (IsStopping) return;
        _inTray = false;
        Show();
        ShowInTaskbar = true;
        WindowState = FormWindowState.Normal;
        Activate();
        RestartRefreshTimer();
        _ = RefreshDashboardAsync();
    }

    internal void RestoreFromSingleInstance() => RestoreFromTray();

    private void OnFormClosing(object? sender, FormClosingEventArgs e)
    {
        if (_exitRequested || e.CloseReason is CloseReason.WindowsShutDown or CloseReason.TaskManagerClosing) return;
        e.Cancel = true;
        HideToTray();
    }

    private async Task ExitApplicationAsync()
    {
        if (IsStopping) return;
        _exitRequested = true;
        _lifetime.Cancel();
        _timer.Stop();
        _trayIcon.Visible = false;
        await _session.StopAsync();
        Application.Exit();
    }

    internal void ExitFromSingleInstance() => _ = ExitApplicationAsync();

    private static bool IsStartupEnabled()
    {
        try
        {
            using var runKey = Registry.CurrentUser.OpenSubKey(StartupKeyPath, writable: false);
            return runKey?.GetValue(StartupValueName) is string value && !string.IsNullOrWhiteSpace(value);
        }
        catch (SecurityException) { return false; }
        catch (UnauthorizedAccessException) { return false; }
    }

    private void UpdateStartupSetting()
    {
        try
        {
            using var runKey = Registry.CurrentUser.CreateSubKey(StartupKeyPath);
            if (runKey is null) throw new InvalidOperationException("无法打开当前用户的开机启动设置。");
            if (_startup.Checked) runKey.SetValue(StartupValueName, BuildStartupCommand(), RegistryValueKind.String);
            else runKey.DeleteValue(StartupValueName, throwOnMissingValue: false);
            SetError("");
        }
        catch (Exception ex) { SetError("开机自启设置失败：" + ex.Message); }
    }

    private static string BuildStartupCommand()
    {
        var bundledLauncher = Path.Combine(AppContext.BaseDirectory, "SonyDeviceCenter.exe");
        if (File.Exists(bundledLauncher)) return $"\"{bundledLauncher}\" --tray";
        var processPath = Environment.ProcessPath ?? Application.ExecutablePath;
        var processName = Path.GetFileNameWithoutExtension(processPath);
        if (processName.Equals("dotnet", StringComparison.OrdinalIgnoreCase))
        {
            var assemblyPath = Path.Combine(AppContext.BaseDirectory, "SonyDeviceCenter.dll");
            return $"\"{processPath}\" \"{assemblyPath}\" --tray";
        }
        return $"\"{processPath}\" --tray";
    }

    private async Task<(string Stdout, string Stderr)> RunSonyCtlAsync(IEnumerable<string> arguments, CancellationToken cancellationToken = default)
    {
        var commandArguments = arguments.ToList();
        // A bounded busy retry can require one protocol deadline per attempt.
        var seconds = commandArguments.Contains("multipoint") ? 55 : 10;
        return (await _session.RunAsync(commandArguments, _selectedHeadsetAddress, TimeSpan.FromSeconds(seconds), cancellationToken), "");
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing && !_resourcesDisposed)
        {
            _resourcesDisposed = true;
            _exitRequested = true;
            _lifetime.Cancel();
            _session.StateChanged -= OnBackendStateChanged;
            _session.Dispose();
            _timer.Stop();
            _timer.Dispose();
            _trayIcon.Visible = false;
            _trayIcon.Dispose();
            _trayMenu.Dispose();
            _trayBatteryIcon?.Dispose();
            _headsetImage?.Dispose();
            _lifetime.Dispose();
        }
        base.Dispose(disposing);
    }

    private static Dashboard CreatePreviewDashboard() => new()
    {
        Connected = true,
        Name = "WH-1000XM6",
        Address = "00:11:22:33:44:55",
        Battery = new BatteryState { Main = 82 },
        NoiseControl = new NoiseState { Mode = "anc", AmbientLevel = 10 },
        Features = new FeatureState
        {
            Battery = new FeatureAvailability { Availability = "valid" },
            NoiseControl = new FeatureAvailability { Availability = "valid" },
            Multipoint = new FeatureAvailability { Availability = "valid" },
            Equalizer = new FeatureAvailability { Availability = "valid" },
            Dsee = new FeatureAvailability { Availability = "valid" }
        },
        Capabilities = new CapabilityState { NoiseControl = JsonSerializer.SerializeToElement(true), PowerOff = true, Equalizer = true, TenBandEqualizer = true, Dsee = true },
        Equalizer = new EqualizerState { Preset = 0x00, PresetName = "Off", Bands = [0, 0, 0, 0, 0] },
        Multipoint = new MultipointState
        {
            Supported = true, SourceSwitchSupported = true, SourceKeepSupported = true, MaxConnected = 2,
            PlaybackIndex = 1, SourceKeeping = true,
            Devices =
            [
                new() { Address = "A0:B1:C2:D3:E4:F5", Name = "Windows 工作电脑", Connected = true, Playing = true, ConnectionIndex = 1 },
                new() { Address = "10:20:30:40:50:60", Name = "我的手机", Connected = true, ConnectionIndex = 2 },
                new() { Address = "60:50:40:30:20:10", Name = "平板电脑", Connected = false }
            ]
        }
    };

    private sealed class RoundedPanel : Panel
    {
        public RoundedPanel()
        {
            DoubleBuffered = true;
            BackColor = Color.White;
        }

        protected override void OnPaintBackground(PaintEventArgs e)
        {
            e.Graphics.Clear(Parent?.BackColor ?? SystemColors.Control);
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            using var fill = new SolidBrush(BackColor);
            using var path = CreateRoundedPath();
            e.Graphics.FillPath(fill, path);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            using var border = new Pen(Color.FromArgb(232, 231, 239));
            using var path = CreateRoundedPath();
            e.Graphics.DrawPath(border, path);
        }

        private GraphicsPath CreateRoundedPath()
        {
            var path = new GraphicsPath();
            var bounds = new RectangleF(0.5f, 0.5f, Width - 1f, Height - 1f);
            const float radius = 13;
            path.AddArc(bounds.Left, bounds.Top, radius * 2, radius * 2, 180, 90);
            path.AddArc(bounds.Right - radius * 2, bounds.Top, radius * 2, radius * 2, 270, 90);
            path.AddArc(bounds.Right - radius * 2, bounds.Bottom - radius * 2, radius * 2, radius * 2, 0, 90);
            path.AddArc(bounds.Left, bounds.Bottom - radius * 2, radius * 2, radius * 2, 90, 90);
            path.CloseFigure();
            return path;
        }
    }

    private static class NativeMethods
    {
        [System.Runtime.InteropServices.DllImport("user32.dll", SetLastError = true)]
        public static extern bool DestroyIcon(IntPtr hIcon);
    }
}
