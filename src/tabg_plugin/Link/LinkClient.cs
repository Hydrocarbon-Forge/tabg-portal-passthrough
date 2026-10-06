using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Net.WebSockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace TabgPortal.Link
{
    // WebSocket client for the local link hub (src/link/protocol.md). Role = "host".
    // Uses System.Net.WebSockets.ClientWebSocket (net472 / Windows 8+). Runs its own
    // connect+receive loop on the thread pool; the Unity main thread only touches the
    // bounded outgoing ring buffer (Send) and drains received messages (PollReceived).
    //
    // Back-pressure mirrors the hub (protocol.md "Ring-buffer + back-pressure"): a bounded
    // outgoing queue, drop-oldest on overflow so a stale pose never blocks a fresh one.
    public sealed class LinkClient : IDisposable
    {
        private readonly Uri _uri;
        private readonly string _name;
        private readonly int _capacity;
        private readonly Action<string> _log;

        private readonly ConcurrentQueue<string> _outgoing = new ConcurrentQueue<string>();
        private readonly ConcurrentQueue<Dictionary<string, object>> _incoming = new ConcurrentQueue<Dictionary<string, object>>();
        private readonly SemaphoreSlim _outSignal = new SemaphoreSlim(0);

        private volatile bool _running;
        private volatile bool _connected;
        private int _queued;
        public long Dropped;
        public long Sent;

        public bool Connected => _connected;

        public LinkClient(string url, string name, int capacity, Action<string> log)
        {
            _uri = new Uri(url);
            _name = name;
            _capacity = Math.Max(16, capacity);
            _log = log ?? (_ => { });
        }

        public void Start()
        {
            if (_running) return;
            _running = true;
            Task.Run(RunLoop);
        }

        // Enqueue a JSON frame for sending (non-blocking, drop-oldest when full).
        public void Send(string json)
        {
            if (!_running) return;
            if (Interlocked.Increment(ref _queued) > _capacity)
            {
                if (_outgoing.TryDequeue(out _))
                {
                    Interlocked.Decrement(ref _queued);
                    Interlocked.Increment(ref Dropped);
                }
            }
            _outgoing.Enqueue(json);
            try { _outSignal.Release(); } catch (SemaphoreFullException) { }
        }

        // Drain received messages on the main thread.
        public bool PollReceived(out Dictionary<string, object> msg)
        {
            return _incoming.TryDequeue(out msg);
        }

        private async Task RunLoop()
        {
            while (_running)
            {
                ClientWebSocket ws = null;
                try
                {
                    ws = new ClientWebSocket();
                    using (var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5)))
                        await ws.ConnectAsync(_uri, cts.Token).ConfigureAwait(false);

                    // Handshake: first frame MUST be hello (protocol.md).
                    string hello = Messages.Hello(_name);
                    await SendRaw(ws, hello).ConfigureAwait(false);
                    _connected = true;
                    _log("link connected to " + _uri + " as host");

                    var recvTask = ReceiveLoop(ws);
                    var sendTask = SendLoop(ws);
                    await Task.WhenAny(recvTask, sendTask).ConfigureAwait(false);
                }
                catch (Exception e)
                {
                    _log("link connect/run error: " + e.Message);
                }
                finally
                {
                    _connected = false;
                    try { ws?.Dispose(); } catch { }
                }

                if (_running)
                    await Task.Delay(1000).ConfigureAwait(false); // reconnect backoff
            }
        }

        private async Task SendLoop(ClientWebSocket ws)
        {
            while (_running && ws.State == WebSocketState.Open)
            {
                await _outSignal.WaitAsync(500).ConfigureAwait(false);
                while (_outgoing.TryDequeue(out string json))
                {
                    Interlocked.Decrement(ref _queued);
                    await SendRaw(ws, json).ConfigureAwait(false);
                    Interlocked.Increment(ref Sent);
                }
            }
        }

        private static async Task SendRaw(ClientWebSocket ws, string json)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(json);
            await ws.SendAsync(new ArraySegment<byte>(bytes), WebSocketMessageType.Text, true, CancellationToken.None)
                .ConfigureAwait(false);
        }

        private async Task ReceiveLoop(ClientWebSocket ws)
        {
            var buffer = new byte[64 * 1024];
            var sb = new StringBuilder();
            while (_running && ws.State == WebSocketState.Open)
            {
                WebSocketReceiveResult result;
                sb.Length = 0;
                do
                {
                    result = await ws.ReceiveAsync(new ArraySegment<byte>(buffer), CancellationToken.None).ConfigureAwait(false);
                    if (result.MessageType == WebSocketMessageType.Close)
                    {
                        try { await ws.CloseAsync(WebSocketCloseStatus.NormalClosure, "bye", CancellationToken.None).ConfigureAwait(false); } catch { }
                        return;
                    }
                    sb.Append(Encoding.UTF8.GetString(buffer, 0, result.Count));
                }
                while (!result.EndOfMessage);

                string text = sb.ToString();
                try
                {
                    if (Json.Parse(text) is Dictionary<string, object> map)
                        _incoming.Enqueue(map);
                }
                catch (Exception e)
                {
                    _log("link recv parse error: " + e.Message);
                }
            }
        }

        public void Dispose()
        {
            _running = false;
            try { _outSignal.Release(); } catch { }
        }
    }
}
