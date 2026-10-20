using System;
using System.Windows.Forms;

namespace SonyDeviceCenter;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        var preview = args.Any(arg => string.Equals(arg, "--preview", StringComparison.OrdinalIgnoreCase));
        if (!preview && args.Any(arg => string.Equals(arg, "--exit", StringComparison.OrdinalIgnoreCase)))
        {
            if (Mutex.TryOpenExisting(SingleInstanceHost.MutexName, out var runningMutex))
            {
                using (runningMutex)
                {
                    if (!SingleInstanceHost.TrySend("EXIT")) { Environment.ExitCode = 1; return; }
                    try
                    {
                        if (runningMutex.WaitOne(TimeSpan.FromSeconds(10))) runningMutex.ReleaseMutex();
                        else Environment.ExitCode = 1;
                    }
                    catch (AbandonedMutexException) { runningMutex.ReleaseMutex(); }
                }
            }
            return;
        }
        if (preview)
        {
            RunUi(args, preview: true);
            return;
        }
        using var mutex = new Mutex(true, SingleInstanceHost.MutexName, out var createdNew);
        var ownsMutex = createdNew;
        if (!createdNew)
        {
            if (SingleInstanceHost.TrySend("RESTORE")) return;
            try { ownsMutex = mutex.WaitOne(0); }
            catch (AbandonedMutexException) { ownsMutex = true; }
            if (!ownsMutex) return;
        }
        try { RunUi(args, preview: false); }
        finally { if (ownsMutex) mutex.ReleaseMutex(); }
    }

    private static void RunUi(string[] args, bool preview)
    {
        ApplicationConfiguration.Initialize();
        var startInTray = args.Any(arg => string.Equals(arg, "--tray", StringComparison.OrdinalIgnoreCase));
        var screenshotIndex = Array.FindIndex(args, arg => string.Equals(arg, "--screenshot", StringComparison.OrdinalIgnoreCase));
        var screenshotPath = preview && screenshotIndex >= 0 && screenshotIndex + 1 < args.Length ? args[screenshotIndex + 1] : null;
        using var form = new MainForm(startInTray, preview, screenshotPath);
        if (preview)
        {
            Application.Run(form);
            return;
        }
        _ = form.Handle;
        using var singleInstance = new SingleInstanceHost();
        singleInstance.Start(
            () => { if (!form.IsDisposed) form.BeginInvoke((Action)form.RestoreFromSingleInstance); },
            () => { if (!form.IsDisposed) form.BeginInvoke((Action)form.ExitFromSingleInstance); });
        Application.Run(form);
    }
}
