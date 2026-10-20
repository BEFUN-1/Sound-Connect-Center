using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.Json;
using SonyDeviceCenter;

internal static class Program
{
    private static readonly string Settings = Path.Combine(AppContext.BaseDirectory, "fake-settings.json");
    private static readonly string Log = Path.Combine(AppContext.BaseDirectory, "fake-requests.jsonl");
    private static readonly Mutex LogMutex = new(false, @"Local\SonyDeviceCenterDesktopCheckLog");
    private static int SoakMinutes => int.TryParse(Environment.GetEnvironmentVariable("SONY_SOAK_MINUTES"), out var value) && value > 0 ? value : 60;
    private const string First = "11:22:33:44:55:66";
    private const string Second = "AA:BB:CC:DD:EE:FF";

    [STAThread]
    private static int Main(string[] args)
    {
        if (args.Contains("--session")) { FakeBackend(); return 0; }
        EnterLogLock();
        try { File.WriteAllText(Log, ""); }
        finally { LogMutex.ReleaseMutex(); }
        Configure();
        ApplicationConfiguration.Initialize();
        using var host = new Form { ShowInTaskbar = false, Opacity = 0, Width = 1, Height = 1 };
        int result = 0;
        host.Shown += async (_, _) =>
        {
            try
            {
                if (args.Contains("--soak", StringComparer.OrdinalIgnoreCase)) await SoakOneHour();
                else await Checks();
                Console.WriteLine("ALL DESKTOP CHECKS PASSED");
            }
            catch (Exception ex) { Console.Error.WriteLine(ex); result = 1; }
            finally { host.Close(); }
        };
        Application.Run(host);
        return result;
    }

    private static void Configure(string delayCommand = "", int delayMs = 0, string failCommand = "", int battery = 82, int notificationBattery = -1,
        bool staleFeatures = false) =>
        File.WriteAllText(Settings, JsonSerializer.Serialize(new { delayCommand, delayMs, failCommand, battery, notificationBattery, staleFeatures }));
    private static void Require(bool yes, string message) { if (!yes) throw new Exception(message); }
    private static object? Field(object owner, string name) => owner.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(owner);
    private static object? Call(object owner, string name, params object?[] args) => owner.GetType().GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(owner, args);
    private static Task Invoke(object owner, string name, params object?[] args) => (Task)Call(owner, name, args)!;
    private static void SetField(object owner, string name, object? value) => owner.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(owner, value);
    private static void EnterLogLock()
    {
        try { LogMutex.WaitOne(); }
        catch (AbandonedMutexException) { }
    }
    private static void AppendRequest(string request)
    {
        EnterLogLock();
        try { File.AppendAllText(Log, request + Environment.NewLine); }
        finally { LogMutex.ReleaseMutex(); }
    }
    private static List<JsonElement> Requests()
    {
        EnterLogLock();
        try { return File.ReadAllLines(Log).Select(line => JsonDocument.Parse(line).RootElement.Clone()).ToList(); }
        finally { LogMutex.ReleaseMutex(); }
    }
    private static int ThreadCount(Process process)
    {
        var threads = process.Threads;
        var count = threads.Count;
        foreach (ProcessThread thread in threads) thread.Dispose();
        return count;
    }
    private static async Task Until(Func<bool> predicate, int timeoutMs = 5000)
    {
        var watch = Stopwatch.StartNew();
        while (!predicate()) { if (watch.ElapsedMilliseconds > timeoutMs) throw new TimeoutException("Check did not complete"); await Task.Delay(20); }
    }
    private static async Task Checks()
    {
        using var form = new MainForm();
        _ = form.Handle;
        await Invoke(form, "LoadHeadsetCandidatesAsync");
        await Invoke(form, "RefreshDashboardAsync", false);
        Require(((Label)Field(form,"_batteryText")!).Text == "82%", "Initial battery");
        Require(((System.Windows.Forms.Timer)Field(form,"_timer")!).Interval == 15000, "Visible dashboard refresh interval");
        Require(((ToolStripMenuItem)Field(form,"_trayModeMenu")!).Enabled, "Tray mode controls");
        var initialPid = Requests()[0].GetProperty("pid").GetInt32();
        var cards = (FlowLayoutPanel)Field(form,"_deviceCards")!;
        var firstCard = cards.Controls[0];
        for (int i = 0; i < 100; ++i) await Invoke(form,"ReadFeatureAsync","snapshot",CancellationToken.None);
        Require(ReferenceEquals(firstCard,cards.Controls[0]), "Unchanged cards must be reused");
        Require(Requests().All(r => r.GetProperty("pid").GetInt32() == initialPid), "Only one backend process");
        int visibleRefreshStart = Requests().Count;
        await Invoke(form,"RefreshDashboardAsync",true);
        var visibleRefresh = Requests().Skip(visibleRefreshStart).Select(r => r.GetProperty("command").GetString()).ToArray();
        Require(!visibleRefresh.Contains("refresh battery") && !visibleRefresh.Contains("refresh multipoint") &&
            visibleRefresh.Contains("refresh noise") && visibleRefresh.Contains("refresh eq") && visibleRefresh.Contains("refresh dsee"),
            "Visible settings refresh must rate-limit battery and multipoint reads");
        Console.WriteLine("PASS initial connection, 100 cached snapshots, backend and card reuse");

        int manualRefreshStart = Requests().Count;
        await Invoke(form,"RefreshDashboardAsync",false);
        var manualRefresh = Requests().Skip(manualRefreshStart).Select(r => r.GetProperty("command").GetString()).ToArray();
        Require(manualRefresh.Contains("refresh multipoint"), "Manual dashboard refresh must refresh multipoint state");
        Console.WriteLine("PASS periodic multipoint rate limit and manual refresh");

        Configure(delayCommand: "refresh eq", delayMs: 700);
        int before = Requests().Count;
        var refresh = Invoke(form,"RefreshDashboardAsync",true);
        await Task.Delay(500);
        await Until(() => Requests().Skip(before).Any(r => r.GetProperty("command").GetString() == "refresh eq"));
        int uiTicks = 0;
        using (var timer = new System.Windows.Forms.Timer { Interval = 20 })
        {
            timer.Tick += (_,_) => ++uiTicks; timer.Start();
            var action = Invoke(form,"ApplyNoiseModeAsync","ambient");
            var duplicate = Invoke(form,"ApplyNoiseModeAsync","ambient");
            await Task.WhenAll(refresh,action,duplicate);
        }
        var tail = Requests().Skip(before).Select(r => r.GetProperty("command").GetString()).ToArray();
        Require(tail.Count(c => c == "ambient 10") == 1, "Repeated action must coalesce");
        Require(!tail.Contains("refresh dsee") && !tail.Contains("refresh multipoint"), "Action must cancel remaining maintenance");
        Require(Requests().Skip(before).All(r => r.GetProperty("pid").GetInt32() == initialPid), "Canceling maintenance must preserve the active backend process");
        Require(uiTicks > 5, "GUI must remain responsive during slow native query");
        Require(!(bool)Field(form,"_busy")!, "Busy restored");
        Configure();
        await Invoke(form,"ApplyNoiseModeAsync","anc");
        await Invoke(form,"ApplyNoiseModeAsync","off");
        Require(((ComboBox)Field(form,"_noiseMode")!).SelectedIndex == 0, "Normal mode readback");
        Console.WriteLine("PASS action priority, repeated clicks, responsive UI, all three noise modes");

        before = Requests().Count;
        await Invoke(form,"ApplyEqualizerPresetAsync","off");
        await Invoke(form,"ApplyCustomEqualizerAsync",0,new[]{0,1,2,3,4,5,6,7,8,9});
        await Invoke(form,"ApplyDseeAsync",true);
        await Invoke(form,"ApplyDseeAsync",false);
        tail = Requests().Skip(before).Select(r => r.GetProperty("command").GetString()).ToArray();
        Require(tail.Contains("eq preset off"), "Equalizer preset command");
        Require(tail.Contains("eq custom 0 1 2 3 4 5 6 7 8 9"), "Ten-band custom equalizer command");
        Require(tail.Contains("dsee on") && tail.Contains("dsee off"), "DSEE control commands");
        Require(Requests().Skip(before).All(r => r.GetProperty("pid").GetInt32() == initialPid), "EQ and DSEE reuse the active backend");
        Console.WriteLine("PASS EQ presets, ten-band custom values, DSEE toggle and state readback");

        Configure(failCommand: "refresh battery");
        await Invoke(form,"RefreshDashboardAsync",false);
        Require(((Label)Field(form,"_batteryText")!).Text == "82%", "Last known battery remains visible after a read failure");
        Require(((Label)Field(form,"_batteryDetail")!).Text.Contains("状态待确认"), "Stale battery is clearly marked");
        Require(((NotifyIcon)Field(form,"_trayIcon")!).Text.Contains("上次电量 82%"), "Tray retains the last known battery");
        Require(((ToolStripMenuItem)Field(form,"_trayModeMenu")!).Enabled, "Read failure must leave active controls available");
        Configure(staleFeatures: true);
        await Invoke(form,"ReadFeatureAsync","noise",CancellationToken.None);
        Require(((ComboBox)Field(form,"_noiseMode")!).Enabled, "Noise control remains available when state readback is stale");
        Require(((ComboBox)Field(form,"_equalizerPreset")!).Enabled, "EQ remains available when state readback is stale");
        Require(((CheckBox)Field(form,"_dsee")!).Enabled, "DSEE remains available when state readback is stale");
        Configure();
        await Invoke(form,"ApplyNoiseModeAsync","anc");
        Require(!(bool)Field(form,"_busy")!, "Failure recovery clears busy");
        Console.WriteLine("PASS stale battery retention, capability-based controls and active-operation recovery");

        Configure(notificationBattery:39);
        var notificationWatch = Stopwatch.StartNew();
        var activeSession = (SonyCtlSession)Field(form,"_session")!;
        await activeSession.RunAsync(new[]{"test notification"},First,TimeSpan.FromSeconds(5),CancellationToken.None);
        await Until(() => ((Label)Field(form,"_batteryText")!).Text == "39%",1000);
        Require(notificationWatch.Elapsed < TimeSpan.FromSeconds(1), "Bluetooth event must update the dashboard immediately");
        Require(((NotifyIcon)Field(form,"_trayIcon")!).Text.Contains("39%"), "Bluetooth event must update tray battery immediately");
        Configure(battery:82);
        await Invoke(form,"ReadFeatureAsync","battery",CancellationToken.None);
        Console.WriteLine("PASS immediate backend notification updates dashboard and tray battery");

        Call(form,"HideToTray");
        Require(((System.Windows.Forms.Timer)Field(form,"_timer")!).Interval == 30000, "Tray battery interval");
        // Complete the reconnect/full initialization before measuring idle tray.
        await Invoke(form,"RefreshDashboardAsync",true);
        before = Requests().Count;
        var trayBackendPid = Requests().Last().GetProperty("pid").GetInt32();
        Configure(battery:64);
        await Until(() => ((NotifyIcon)Field(form,"_trayIcon")!).Text.Contains("64%"),34000);
        tail = Requests().Skip(before).Select(r => r.GetProperty("command").GetString()).ToArray();
        Require(tail.Contains("refresh battery"), "Real timer must refresh tray battery");
        Require(!tail.Contains("refresh multipoint") && !tail.Contains("refresh eq"), "Idle tray must query battery only");
        Require(Requests().Skip(before).All(r => r.GetProperty("pid").GetInt32() == trayBackendPid), "Tray polling keeps the active backend");
        Console.WriteLine("PASS actual 30-second tray timer, updated battery, no settings/history polling");
        await (Task)Field(form,"_refreshTask")!;
        await Until(() => !(bool)Field(form,"_busy")!);

        // A selector change while an old refresh is running cannot paint old state.
        Configure(delayCommand:"refresh eq",delayMs:400);
        before = Requests().Count;
        var oldRefresh = Invoke(form,"RefreshDashboardAsync",false);
        await Until(() => Requests().Skip(before).Any(r => r.GetProperty("command").GetString() == "refresh eq"));
        ((ComboBox)Field(form,"_headsetSelector")!).SelectedIndex = 1;
        await oldRefresh;
        await (Task)Field(form,"_refreshTask")!;
        Require(((Label)Field(form,"_headsetAddress")!).Text == Second, "Switched headset state");
        Configure();
        await Invoke(form,"ApplyNoiseModeAsync","ambient");
        Require(Requests().Last(r=>r.GetProperty("command").GetString()=="ambient 10").GetProperty("address").GetString()==Second,"Action targets selected headset");
        Console.WriteLine("PASS device switching during refresh and command target isolation");

        var multi = Field(form,"_dashboard")!.GetType().GetProperty("Multipoint")!.GetValue(Field(form,"_dashboard"))!;
        var devices = (System.Collections.IList)multi.GetType().GetProperty("Devices")!.GetValue(multi)!;
        foreach (var command in new[]{"source","default","keep off","connect","disconnect","unpair"})
        {
            using var confirm = ConfirmDialogs();
            await Invoke(form,"MultipointActionAsync",command,devices[0],"测试多点操作");
        }
        using (var confirm = ConfirmDialogs()) await Invoke(form,"PowerOffAsync");
        Require(((Label)Field(form,"_status")!).Text == "已发送关机命令", "Power-off status");
        Require(!((ToolStripMenuItem)Field(form,"_trayModeMenu")!).Enabled, "Power off clears tray controls");
        await Invoke(form,"RefreshDashboardAsync",false);
        Console.WriteLine("PASS all multipoint UI actions and power off, including confirmation dialogs");

        using (var session = new SonyCtlSession())
        {
            Configure(delayCommand:"refresh battery",delayMs:3000);
            bool timedOut = false;
            try { await session.RunAsync(new[]{"refresh","battery"},First,TimeSpan.FromMilliseconds(150),CancellationToken.None); }
            catch (TimeoutException) { timedOut=true; }
            Require(timedOut,"Timeout must be reported");
            Configure();
            await session.RunAsync(new[]{"refresh","battery"},First,TimeSpan.FromSeconds(5),CancellationToken.None);
            Configure(delayCommand:"refresh battery",delayMs:15000);
            using var cancellation = new CancellationTokenSource();
            var pending = session.RunAsync(new[]{"refresh","battery"},First,TimeSpan.FromSeconds(20),cancellation.Token);
            await Task.Delay(100);
            cancellation.Cancel();
            try { await pending; } catch(OperationCanceledException) { }
            var watch = Stopwatch.StartNew();
            await session.StopAsync();
            Require(watch.Elapsed < TimeSpan.FromSeconds(3),"Shutdown must be bounded");
        }
        Configure();
        Console.WriteLine("PASS timeout, backend recovery, cancellation and two-second shutdown guard");
        var resources = Process.GetCurrentProcess();
        resources.Refresh();
        Console.WriteLine($"RESOURCE privateBytes={resources.PrivateMemorySize64} handles={resources.HandleCount} threads={resources.Threads.Count}");
        form.Dispose();
        await Task.Delay(250);
        foreach(var pid in Requests().Select(r=>r.GetProperty("pid").GetInt32()).Distinct())
        {
            try { using var process=Process.GetProcessById(pid); Require(process.HasExited,"No orphan backend after disposal"); }
            catch(ArgumentException) { }
        }
        Console.WriteLine("PASS application disposal leaves no backend processes");
    }

    private static async Task SoakOneHour()
    {
        File.WriteAllText(Log, "");
        Configure();
        var samples = new List<(int Minute, long PrivateBytes, int Handles, int Threads, int GdiObjects, int UserObjects,
            long BackendPrivateBytes, int BackendHandles, int BackendThreads, int BackendPid)>();
        var reportPath = Path.Combine(AppContext.BaseDirectory, "one-hour-soak.jsonl");
        File.WriteAllText(reportPath, "");
        using (var form = new MainForm())
        {
            _ = form.Handle;
            await Invoke(form, "LoadHeadsetCandidatesAsync");
            await Invoke(form, "RefreshDashboardAsync", false);
            var watch = Stopwatch.StartNew();
            using var app = Process.GetCurrentProcess();
            for (var minute = 1; minute <= SoakMinutes; ++minute)
            {
                await Task.Delay(TimeSpan.FromMinutes(1));
                if (minute is 10 or 30 or 50)
                {
                    SetField(form, "_inTray", true);
                    Call(form, "RestartRefreshTimer");
                    await Invoke(form, "RefreshDashboardAsync", true);
                }
                else if (minute is 20 or 40)
                {
                    SetField(form, "_inTray", false);
                    Call(form, "RestartRefreshTimer");
                    await Invoke(form, "RefreshDashboardAsync", false);
                }
                if (minute % 5 == 0)
                    await Invoke(form, "ApplyNoiseModeAsync", minute % 10 == 0 ? "ambient" : "anc");

                app.Refresh();
                var backendPid = Requests().Last().GetProperty("pid").GetInt32();
                using var backend = Process.GetProcessById(backendPid);
                backend.Refresh();
                var sample = (Minute: minute, PrivateBytes: app.PrivateMemorySize64, Handles: app.HandleCount,
                    Threads: ThreadCount(app), GdiObjects: GetGuiResources(app.Handle, 0), UserObjects: GetGuiResources(app.Handle, 1),
                    BackendPrivateBytes: backend.PrivateMemorySize64,
                    BackendHandles: backend.HandleCount, BackendThreads: ThreadCount(backend), BackendPid: backendPid);
                samples.Add(sample);
                File.AppendAllText(reportPath, JsonSerializer.Serialize(new
                {
                    minute = sample.Minute,
                    elapsedSeconds = (int)watch.Elapsed.TotalSeconds,
                    appPrivateBytes = sample.PrivateBytes,
                    appHandles = sample.Handles,
                    appThreads = sample.Threads,
                    appGdiObjects = sample.GdiObjects,
                    appUserObjects = sample.UserObjects,
                    backendPrivateBytes = sample.BackendPrivateBytes,
                    backendHandles = sample.BackendHandles,
                    backendThreads = sample.BackendThreads,
                    backendPid = sample.BackendPid
                }) + Environment.NewLine);
                Console.WriteLine($"SOAK minute={minute} appPrivateBytes={sample.PrivateBytes} appHandles={sample.Handles} appThreads={sample.Threads} appGdi={sample.GdiObjects} appUser={sample.UserObjects} backendPrivateBytes={sample.BackendPrivateBytes} backendHandles={sample.BackendHandles} backendThreads={sample.BackendThreads}");
            }
            form.Dispose();
        }
        await Task.Delay(250);
        var requestPids = Requests().Select(r => r.GetProperty("pid").GetInt32()).Distinct().ToArray();
        Require(requestPids.Length == 1, "One-hour soak must keep one backend process");
        try { using var process = Process.GetProcessById(requestPids[0]); Require(process.HasExited, "Soak backend must exit with the app"); }
        catch (ArgumentException) { }

        var baseline = samples.Skip(4).Take(10).ToArray();
        var final = samples.TakeLast(10).ToArray();
        var finalStable = samples.TakeLast(Math.Min(3, samples.Count)).ToArray();
        Require(baseline.Length == 10 && final.Length == 10 && finalStable.Length == 3, "One-hour resource samples missing");
        Require(final.Average(s => s.PrivateBytes) - baseline.Average(s => s.PrivateBytes) < 8 * 1024 * 1024,
            "App private bytes grew more than 8 MiB across stable soak windows");
        Require(samples.Max(s => s.Handles) - baseline.Average(s => s.Handles) < 100,
            "App process handles spiked by more than 100 across the soak");
        Require(finalStable.Average(s => s.Handles) - baseline.Average(s => s.Handles) < 12,
            "App process handles did not return to a stable level at the end of the soak");
        Require(final.Average(s => s.Threads) - baseline.Average(s => s.Threads) < 3,
            "App thread count grew across stable soak windows");
        Require(finalStable.Average(s => s.GdiObjects) - baseline.Average(s => s.GdiObjects) < 8,
            "App GDI objects grew across stable soak windows");
        Require(finalStable.Average(s => s.UserObjects) - baseline.Average(s => s.UserObjects) < 8,
            "App USER objects grew across stable soak windows");
        Require(final.Average(s => s.BackendPrivateBytes) - baseline.Average(s => s.BackendPrivateBytes) < 8 * 1024 * 1024,
            "Backend private bytes grew more than 8 MiB across stable soak windows");
        Require(final.Average(s => s.BackendHandles) - baseline.Average(s => s.BackendHandles) < 12,
            "Backend handle count grew across stable soak windows");
        Require(final.Average(s => s.BackendThreads) - baseline.Average(s => s.BackendThreads) < 3,
            "Backend thread count grew across stable soak windows");
        Console.WriteLine("PASS one-hour simulated foreground/tray soak; report=" + reportPath);
    }

    private static System.Windows.Forms.Timer ConfirmDialogs()
    {
        var timer=new System.Windows.Forms.Timer { Interval=40 };
        timer.Tick+=(_,_) => EnumWindows((window,_)=>
        {
            GetWindowThreadProcessId(window,out var pid);
            var name=new System.Text.StringBuilder(50); GetClassName(window,name,name.Capacity);
            if(pid==Environment.ProcessId && name.ToString()=="#32770") PostMessage(window,0x111,(IntPtr)6,IntPtr.Zero);
            return true;
        },IntPtr.Zero);
        timer.Start(); return timer;
    }
    private delegate bool WindowCallback(IntPtr window,IntPtr data);
    [DllImport("user32.dll")] private static extern int GetGuiResources(IntPtr process, uint flags);
    [DllImport("user32.dll")] private static extern bool EnumWindows(WindowCallback callback,IntPtr data);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window,out int processId);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] private static extern int GetClassName(IntPtr window,System.Text.StringBuilder name,int count);
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr window,uint message,IntPtr wParam,IntPtr lParam);

    private static void FakeBackend()
    {
        var mode="anc"; var connected=true; var battery=82; var dsee=false; var preset="off"; var bands=new int[10];
        string? line;
        while((line=Console.ReadLine()) is not null)
        {
            using var request=JsonDocument.Parse(line); var root=request.RootElement;
            var command=root.GetProperty("command").GetString()!;
            var address=root.GetProperty("address").GetString()!;
            if(address.Length==0) address=First;
            AppendRequest(JsonSerializer.Serialize(new{pid=Environment.ProcessId,command,address}));
            using var config=JsonDocument.Parse(File.ReadAllText(Settings)); var settings=config.RootElement;
            if(settings.GetProperty("delayCommand").GetString()==command) Thread.Sleep(settings.GetProperty("delayMs").GetInt32());
            var success=settings.GetProperty("failCommand").GetString()!=command;
            if(command=="anc on") mode="anc";
            if(command=="anc off") mode="off";
            if(command.StartsWith("ambient ")) mode="ambient";
            if(command.StartsWith("eq preset ")) preset=command[10..];
            if(command.StartsWith("eq custom ")) { preset="manual"; bands=command.Split(' ').Skip(2).Take(10).Select(value=>int.Parse(value)).ToArray(); }
            if(command=="dsee on") dsee=true;
            if(command=="dsee off") dsee=false;
            if(command=="poweroff") connected=false;
            if(command.StartsWith("refresh ")) connected=true;
            if(command=="refresh battery") battery=settings.GetProperty("battery").GetInt32();
            if(command=="test notification") battery=settings.GetProperty("notificationBattery").GetInt32();
            var availability=settings.GetProperty("staleFeatures").GetBoolean() ? "stale" : "valid";
            var state=new
            {
                connected,name="WH-1000XM6",address,battery=new{main=battery,charging=false},
                noiseControl=new{mode,ambientLevel=10},
                features=new { battery=new{availability},noiseControl=new{availability},multipoint=new{availability},equalizer=new{availability},dsee=new{availability} },
                capabilities=new { anc=true,ambient=true,powerOff=true,equalizer=true,tenBandEqualizer=true,clearBass=false,dsee=true },
                equalizer=new { preset=preset=="manual"?160:0,presetName=preset=="manual"?"Manual":"Off",clearBass=0,bands },dsee,
                multipoint=new { supported=true,sourceSwitchSupported=true,sourceKeepSupported=true,maxConnected=2,sourceKeeping=false,playbackIndex=1,
                    devices=new[]{new{address=First,name="PC",connected=true,playing=true,connectionIndex=1},new{address=Second,name="Phone",connected=false,playing=false,connectionIndex=0}} }
            };
            var data=command=="devices" ? $"WH-1000XM6 [{First}]\nWH-1000XM5 [{Second}]" : JsonSerializer.Serialize(state);
            if(command=="test notification")
                Console.WriteLine(JsonSerializer.Serialize(new{ @event="changed",address,data=JsonSerializer.Serialize(state) }));
            Console.WriteLine(JsonSerializer.Serialize(new { id=root.GetProperty("id").GetInt64(),success,message=success?"":"Headset unavailable",data }));
        }
    }
}
