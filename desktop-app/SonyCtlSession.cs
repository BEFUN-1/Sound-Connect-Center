using System.Collections.Concurrent;
using System.Diagnostics;
using System.Text;
using System.Text.Json;

namespace SonyDeviceCenter;

// One private backend process per app. All Bluetooth work stays on its executor;
// cancellation discards a read, rather than destroying a healthy connection.
internal sealed class SonyCtlSession : IDisposable
{
    private static readonly TimeSpan CancelledRequestDrainTimeout = TimeSpan.FromSeconds(3);
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly ConcurrentDictionary<long, TaskCompletionSource<JsonElement>> _pending = new();
    private Process? _process;
    private long _requestId;
    private bool _stopping;
    private bool _restartNeeded;
    private string _lastDiagnostic = "";
    public event Action<string, string>? StateChanged;

    public async Task<string> RunAsync(IReadOnlyList<string> arguments, string? selectedAddress,
        TimeSpan timeout, CancellationToken cancellationToken)
    {
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        deadline.CancelAfter(timeout);
        var acquired = false;
        var requestStarted = false;
        long id = 0;
        TaskCompletionSource<JsonElement>? completion = null;
        try
        {
            await _gate.WaitAsync(deadline.Token);
            acquired = true;
            ObjectDisposedException.ThrowIf(_stopping, this);
            var process = await StartIfNeededAsync(deadline.Token);
            var command = new List<string>(arguments);
            var deviceIndex = command.IndexOf("--device");
            if (deviceIndex >= 0)
            {
                selectedAddress = command[deviceIndex + 1];
                command.RemoveRange(deviceIndex, 2);
            }
            id = Interlocked.Increment(ref _requestId);
            completion = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
            _pending[id] = completion;
            var request = JsonSerializer.Serialize(new { id, address = selectedAddress ?? "", command = string.Join(' ', command) });
            cancellationToken.ThrowIfCancellationRequested();
            requestStarted = true;
            await process.StandardInput.WriteLineAsync(request.AsMemory(), deadline.Token);
            var response = await completion.Task.WaitAsync(deadline.Token);
            if (!response.GetProperty("success").GetBoolean())
                throw new InvalidOperationException(response.GetProperty("message").GetString() ?? "设备未响应。");
            return response.GetProperty("data").GetString() ?? "";
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            // A wedged native connect/read cannot hold all subsequent controls.
            // Do not retry a write: its result may already have reached the headset.
            if (acquired) KillBackend();
            throw new TimeoutException($"设备未响应（{timeout.TotalSeconds:0} 秒），状态可能过期，请刷新确认。");
        }
        catch (OperationCanceledException)
        {
            // Let a bounded in-flight read finish so the next foreground
            // command can reuse the same Bluetooth connection.
            if (requestStarted && acquired && !_stopping && completion is not null)
            {
                try { await completion.Task.WaitAsync(CancelledRequestDrainTimeout); }
                catch (TimeoutException) { KillBackend(); }
                catch (Exception ex) when (ex is IOException or InvalidOperationException or ObjectDisposedException) { }
            }
            throw;
        }
        finally
        {
            if (id != 0) _pending.TryRemove(id, out _);
            if (acquired) _gate.Release();
        }
    }

    private async Task<Process> StartIfNeededAsync(CancellationToken cancellationToken)
    {
        if (_process is { HasExited: false } active)
        {
            if (!_restartNeeded) return active;
            await active.WaitForExitAsync(cancellationToken);
        }
        _process?.Dispose();
        var exe = Path.Combine(AppContext.BaseDirectory, "sonyctl.exe");
        if (!File.Exists(exe)) throw new FileNotFoundException("找不到 sonyctl.exe，请先构建 C++ 后端。", exe);
        var process = new Process
        {
            StartInfo = new ProcessStartInfo(exe)
            {
                UseShellExecute = false, CreateNoWindow = true,
                RedirectStandardInput = true, RedirectStandardOutput = true, RedirectStandardError = true,
                StandardInputEncoding = new UTF8Encoding(false),
                StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8,
                WorkingDirectory = AppContext.BaseDirectory
            }
        };
        process.StartInfo.ArgumentList.Add("--session");
        process.Start();
        process.StandardInput.AutoFlush = true;
        _process = process;
        _restartNeeded = false;
        _lastDiagnostic = "";
        _ = ReadResponsesAsync(process);
        _ = DrainDiagnosticsAsync(process);
        return process;
    }

    private async Task ReadResponsesAsync(Process process)
    {
        try
        {
            while (await process.StandardOutput.ReadLineAsync().ConfigureAwait(false) is { } line)
            {
                if (line.Length > 256 * 1024) throw new InvalidDataException("后端响应超出大小限制。");
                using var document = JsonDocument.Parse(line);
                var response = document.RootElement;
                if (response.TryGetProperty("event", out _))
                {
                    if (ReferenceEquals(_process, process) && !_stopping)
                        StateChanged?.Invoke(response.GetProperty("address").GetString() ?? "",
                            response.GetProperty("data").GetString() ?? "{}");
                }
                else if (ReferenceEquals(_process, process) && response.TryGetProperty("id", out var id) && id.TryGetInt64(out var key)
                         && _pending.TryRemove(key, out var pending)) pending.TrySetResult(response.Clone());
            }
        }
        catch (Exception ex) when (ex is IOException or ObjectDisposedException or JsonException or InvalidDataException)
        {
            if (ReferenceEquals(_process, process)) _lastDiagnostic = ex.Message;
        }
        finally
        {
            if (ReferenceEquals(_process, process))
            {
                foreach (var item in _pending)
                    if (_pending.TryRemove(item.Key, out var completion))
                        completion.TrySetException(new IOException("设备后端已断开。" + _lastDiagnostic));
            }
        }
    }

    private async Task DrainDiagnosticsAsync(Process process)
    {
        // Keep a bounded tail even during long sessions or noisy failures.
        var buffer = new char[1024];
        try
        {
            int count;
            while ((count = await process.StandardError.ReadAsync(buffer).ConfigureAwait(false)) > 0)
                if (ReferenceEquals(_process, process))
                {
                    var tail = _lastDiagnostic + new string(buffer, 0, count);
                    _lastDiagnostic = tail.Length > 4096 ? tail[^4096..] : tail;
                }
        }
        catch (Exception ex) when (ex is IOException or ObjectDisposedException) { }
    }

    private void KillBackend()
    {
        var process = _process;
        if (process is null) return;
        _restartNeeded = true;
        try { if (!process.HasExited) process.Kill(entireProcessTree: true); }
        catch (InvalidOperationException) { }
        // Keep the handle until the next start so it cannot be reused early.
    }

    public async Task StopAsync()
    {
        if (_stopping) return;
        _stopping = true;
        var process = _process;
        if (process is null) return;
        try
        {
            process.StandardInput.Close();
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(2));
            await process.WaitForExitAsync(deadline.Token);
        }
        catch (Exception ex) when (ex is OperationCanceledException or IOException or InvalidOperationException)
        { KillBackend(); }
    }

    public void Dispose()
    {
        _stopping = true;
        KillBackend();
        foreach (var item in _pending)
            if (_pending.TryRemove(item.Key, out var pending)) pending.TrySetCanceled();
        _process?.Dispose();
        _process = null;
    }
}
