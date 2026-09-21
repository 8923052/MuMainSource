// <copyright file="ConnectionWrapper.cs" company="MUnique">
// Licensed under the MIT License. See LICENSE file in the project root for full license information.
// </copyright>

namespace MUnique.Client.Library;

using System;
using System.Buffers;
using System.Diagnostics;
using System.IO.Pipelines;
using System.Threading;
using System.Threading.Tasks;
using MUnique.OpenMU.Network;
using Nito.AsyncEx.Synchronous;

/// <summary>
/// A wrapper for a <see cref="Connection"/>.
/// </summary>
public sealed class ConnectionWrapper : IDisposable
{
    private const int MaximumCapturedPacketBytes = 4096;

    private readonly nuint _callbackContext;
    private readonly int _handle;
    private readonly Connection _connection;
    private readonly object _callbackLock = new();
    private readonly BoundedPacketWriter _capturedPacketWriter =
        new(MaximumCapturedPacketBytes);

    /// <summary>
    /// The unmanaged callback to a packet handler. Parameters:
    ///   - handle
    ///   - packet size
    ///   - pointer to packet.
    /// </summary>
    private readonly unsafe delegate* unmanaged<nuint, int, ulong, int, byte*, void> _onPacketReceived;

    /// <summary>
    /// The unmanaged callback to a disconnect handler. Parameter: handle.
    /// </summary>
    private readonly unsafe delegate* unmanaged<nuint, int, ulong, void> _onDisconnected;
    private ulong _generation;
    private int _disconnectStarted;
    private int _disposed;
    private bool _callbacksEnabled = true;
    private nuint _sendCaptureContext;
    private unsafe delegate* unmanaged<nuint, int, int, byte*, byte> _onSendCaptured;
    private bool _sendCaptureActive;
    private bool _sendCaptureSucceeded;

    /// <summary>
    /// Initializes a new instance of the <see cref="ConnectionWrapper"/> class.
    /// </summary>
    /// <param name="callbackContext">The native context returned unchanged with each callback.</param>
    /// <param name="handle">The handle of the connection.</param>
    /// <param name="connection">The connection.</param>
    /// <param name="onPacketReceived">
    /// The pointer to an unmanaged method which is called when a new packet got received.
    /// Parameters: handle, size, pointer to the data.
    /// </param>
    /// <param name="onDisconnected">
    /// The pointer to an unmanaged method which is called when the connection got disconnected.
    /// Parameter: handle.
    /// </param>
    public unsafe ConnectionWrapper(
        nuint callbackContext,
        int handle,
        Connection connection,
        delegate* unmanaged<nuint, int, ulong, int, byte*, void> onPacketReceived,
        delegate* unmanaged<nuint, int, ulong, void> onDisconnected)
    {
        this._callbackContext = callbackContext;
        this._handle = handle;
        this._connection = connection;
        this._onPacketReceived = onPacketReceived;
        this._onDisconnected = onDisconnected;

        connection.PacketReceived += this.OnPacketReceivedAsync;
        connection.Disconnected += this.OnDisconnectedAsync;
    }

    /// <summary>
    /// Gets the output pipe writer.
    /// </summary>
    internal PipeWriter Output => this._connection.Output;

    /// <summary>
    /// Begins a bounded transaction which reports final plaintext packet bytes
    /// to the native ordered-effect producer instead of the socket.
    /// </summary>
    /// <param name="context">The native callback context.</param>
    /// <param name="onSendCaptured">The packet callback.</param>
    /// <returns><see langword="true"/> when capture began.</returns>
    public unsafe bool BeginSendCapture(
        nuint context,
        delegate* unmanaged<nuint, int, int, byte*, byte> onSendCaptured)
    {
        using var l = this._connection.OutputLock.Lock();
        if (Volatile.Read(ref this._disposed) != 0
            || this._sendCaptureActive
            || onSendCaptured == null)
        {
            return false;
        }

        this._sendCaptureContext = context;
        this._onSendCaptured = onSendCaptured;
        this._sendCaptureSucceeded = true;
        this._sendCaptureActive = true;
        return true;
    }

    /// <summary>
    /// Ends the current send-capture transaction.
    /// </summary>
    /// <returns>
    /// <see langword="true"/> when every captured packet was accepted.
    /// </returns>
    public unsafe bool EndSendCapture()
    {
        using var l = this._connection.OutputLock.Lock();
        if (!this._sendCaptureActive)
        {
            return false;
        }

        var succeeded = this._sendCaptureSucceeded;
        this._sendCaptureActive = false;
        this._sendCaptureSucceeded = false;
        this._sendCaptureContext = 0;
        this._onSendCaptured = null;
        this._capturedPacketWriter.Reset();
        return succeeded;
    }

    /// <summary>
    /// Begins receiving packets from the client.
    /// </summary>
    /// <param name="generation">The native route generation attached to callbacks.</param>
    public void BeginReceive(ulong generation)
    {
        lock (this._callbackLock)
        {
            this._generation = generation;
        }

        // we never want it on the main thread, so we do a Task.Run.
        _ = Task.Run(this._connection.BeginReceiveAsync);
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (Interlocked.Exchange(ref this._disposed, 1) != 0)
        {
            return;
        }

        this.DisableCallbacks();
        this._connection.Dispose();
    }

    /// <summary>
    /// Disconnects the connection.
    /// </summary>
    public void DisconnectAndDispose()
    {
        if (Interlocked.Exchange(ref this._disconnectStarted, 1) != 0)
        {
            return;
        }

        this.DisableCallbacks();

        _ = Task.Run(async () =>
        {
            try
            {
                await this._connection.DisconnectAsync().ConfigureAwait(false);
                this._connection.Dispose();
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex);
            }
        });
    }

    /// <summary>
    /// Sends the specified bytes.
    /// </summary>
    /// <param name="bytes">The bytes.</param>
    public void Send(Span<byte> bytes)
    {
        using var l = this._connection.OutputLock.Lock();
        if (this._sendCaptureActive)
        {
            this.ReportCapturedPacket(bytes);
            return;
        }

        var targetSpan = this._connection.Output.GetSpan(bytes.Length);

        bytes.CopyTo(targetSpan);
        this._connection.Output.Advance(bytes.Length);
        this._connection.Output.FlushAsync().AsTask().WaitAndUnwrapException();
    }

    /// <summary>
    /// Sends the specified bytes.
    /// </summary>
    /// <param name="packetFactory">The factory which creates the packet and returns the length of it.</param>
    public void CreateAndSend(Func<PipeWriter, int> packetFactory)
    {
        using var l = this._connection.OutputLock.Lock();
        if (this._sendCaptureActive)
        {
            try
            {
                this._capturedPacketWriter.Reset();
                var capturedLength = packetFactory(
                    this._capturedPacketWriter);
                this._capturedPacketWriter.Advance(capturedLength);
                this.ReportCapturedPacket(
                    this._capturedPacketWriter.WrittenSpan);
            }
            catch
            {
                this._sendCaptureSucceeded = false;
                throw;
            }

            return;
        }

        var length = packetFactory(this._connection.Output);
        this._connection.Output.Advance(length);
        this._connection.Output.FlushAsync().AsTask().WaitAndUnwrapException();
    }

    private unsafe void ReportCapturedPacket(ReadOnlySpan<byte> packet)
    {
        if (packet.IsEmpty || packet.Length > MaximumCapturedPacketBytes
            || this._onSendCaptured == null)
        {
            this._sendCaptureSucceeded = false;
            return;
        }

        fixed (byte* packetPointer = packet)
        {
            if (this._onSendCaptured(
                    this._sendCaptureContext,
                    this._handle,
                    packet.Length,
                    packetPointer) == 0)
            {
                this._sendCaptureSucceeded = false;
            }
        }
    }

    private unsafe ValueTask OnPacketReceivedAsync(ReadOnlySequence<byte> args)
    {
        if (Volatile.Read(ref this._disposed) != 0)
        {
            return ValueTask.CompletedTask;
        }

        using var memoryOwner = MemoryPool<byte>.Shared.Rent((int)args.Length);
        var packet = memoryOwner.Memory.Slice(0, (int)args.Length);
        args.CopyTo(packet.Span);

        fixed (byte* packetPtr = &packet.Span.GetPinnableReference())
        {
            lock (this._callbackLock)
            {
                if (this._callbacksEnabled)
                {
                    try
                    {
                        this._onPacketReceived(
                            this._callbackContext,
                            this._handle,
                            this._generation,
                            packet.Length,
                            packetPtr);
                    }
                    catch (Exception ex)
                    {
                        Debug.WriteLine(ex);
                    }
                }
            }
        }

        return ValueTask.CompletedTask;
    }

    private unsafe ValueTask OnDisconnectedAsync()
    {
        lock (this._callbackLock)
        {
            if (this._callbacksEnabled)
            {
                try
                {
                    this._onDisconnected(
                        this._callbackContext,
                        this._handle,
                        this._generation);
                }
                catch (Exception ex)
                {
                    Debug.WriteLine(ex);
                }
            }
        }

        this.Dispose();
        return ValueTask.CompletedTask;
    }

    private void DisableCallbacks()
    {
        lock (this._callbackLock)
        {
            if (!this._callbacksEnabled)
            {
                return;
            }

            this._callbacksEnabled = false;
            this._connection.PacketReceived -= this.OnPacketReceivedAsync;
            this._connection.Disconnected -= this.OnDisconnectedAsync;
        }
    }

    private sealed class BoundedPacketWriter : PipeWriter
    {
        private readonly byte[] _buffer;
        private int _written;

        public BoundedPacketWriter(int capacity)
        {
            this._buffer = new byte[capacity];
        }

        public ReadOnlySpan<byte> WrittenSpan =>
            this._buffer.AsSpan(0, this._written);

        public void Reset()
        {
            this._written = 0;
        }

        public override void Advance(int bytes)
        {
            if (bytes < 0 || bytes > this._buffer.Length - this._written)
            {
                throw new InvalidOperationException(
                    "Captured packet exceeds its bounded writer.");
            }

            this._written += bytes;
        }

        public override void CancelPendingFlush()
        {
        }

        public override void Complete(Exception? exception = null)
        {
        }

        public override ValueTask<FlushResult> FlushAsync(
            CancellationToken cancellationToken = default) =>
            new(new FlushResult(false, false));

        public override Memory<byte> GetMemory(int sizeHint = 0)
        {
            this.ValidateSizeHint(sizeHint);
            return this._buffer.AsMemory(this._written);
        }

        public override Span<byte> GetSpan(int sizeHint = 0)
        {
            this.ValidateSizeHint(sizeHint);
            return this._buffer.AsSpan(this._written);
        }

        private void ValidateSizeHint(int sizeHint)
        {
            if (sizeHint < 0
                || sizeHint > this._buffer.Length - this._written)
            {
                throw new InvalidOperationException(
                    "Captured packet exceeds its bounded writer.");
            }
        }
    }
}
