using System.IO.Pipes;
using System.Security.Principal;
using System.Text;

namespace SonyDeviceCenter;

internal sealed class SingleInstanceHost : IDisposable
{
    private const string NamePrefix = "SonyDeviceCenter-";
    internal static string MutexName => "Local\\" + NamePrefix + CurrentUserId();
    private static string PipeName => NamePrefix + CurrentUserId() + "-" + System.Diagnostics.Process.GetCurrentProcess().SessionId;
    private readonly CancellationTokenSource _stop = new();
    private Task? _serverTask;

    internal void Start(Action restore, Action exit)
    {
        _serverTask = Task.Run(async () =>
        {
            while (!_stop.IsCancellationRequested)
            {
                try
                {
                    await using var server = new NamedPipeServerStream(
                        PipeName, PipeDirection.In, 1, PipeTransmissionMode.Byte,
                        PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
                    await server.WaitForConnectionAsync(_stop.Token);
                    using var reader = new StreamReader(server, Encoding.UTF8, leaveOpen: false);
                    using var readTimeout = CancellationTokenSource.CreateLinkedTokenSource(_stop.Token);
                    readTimeout.CancelAfter(TimeSpan.FromSeconds(3));
                    var command = await reader.ReadLineAsync(readTimeout.Token);
                    if (string.Equals(command, "RESTORE", StringComparison.OrdinalIgnoreCase)) restore();
                    else if (string.Equals(command, "EXIT", StringComparison.OrdinalIgnoreCase)) exit();
                }
                catch (OperationCanceledException) when (_stop.IsCancellationRequested) { break; }
                catch (IOException) when (_stop.IsCancellationRequested) { break; }
                catch
                {
                    if (_stop.IsCancellationRequested) break;
                    try { await Task.Delay(100, _stop.Token); } catch (OperationCanceledException) { break; }
                }
            }
        }, _stop.Token);
    }

    internal static bool TrySend(string command)
    {
        try
        {
            using var client = new NamedPipeClientStream(".", PipeName, PipeDirection.Out, PipeOptions.CurrentUserOnly);
            client.Connect(3000);
            using var writer = new StreamWriter(client, Encoding.UTF8, leaveOpen: false) { AutoFlush = true };
            writer.WriteLine(command);
            return true;
        }
        catch
        {
            // The existing process may be shutting down; there is no process-wide kill fallback.
            return false;
        }
    }

    public void Dispose()
    {
        _stop.Cancel();
        try { _serverTask?.Wait(750); } catch { }
        _stop.Dispose();
    }

    private static string CurrentUserId()
    {
        try { return WindowsIdentity.GetCurrent().User?.Value ?? Environment.UserName; }
        catch { return Environment.UserName; }
    }
}
