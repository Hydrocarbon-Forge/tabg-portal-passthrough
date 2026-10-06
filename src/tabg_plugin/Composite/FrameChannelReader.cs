using System;
using System.IO.MemoryMappedFiles;
using UnityEngine;

namespace TabgPortal.Composite
{
    // Reader for the shared-memory frame channel (src/framechannel/framechannel.md).
    // The guest (Portal) writes colour+depth into a named mapping; we read the latest completed
    // slot via the seqlock and hand it to the composite pass.
    //
    // Windows mapping name: Local\TABGPortalFrame (session-local). Byte layout per the spec.
    public sealed class FrameData
    {
        public int Width;
        public int Height;
        public long FrameIndex;
        public long TimestampUs;
        public Vector3 Pos;
        public Quaternion Rot;
        public float Fov;
        public int DepthConvention;
        public bool RowsTopDown;
        public byte[] Color;  // width*height*4 RGBA8
        public float[] Depth; // width*height float32
    }

    public sealed class FrameChannelReader : IDisposable
    {
        private const int MAGIC = 0x31435054; // "TPC1"
        private const int DESC_TABLE_OFF = 256;
        private const int DESC_STRIDE = 128;
        private const int HEADER_BYTES = 4096;

        private readonly string _name;
        private MemoryMappedFile _mmf;
        private MemoryMappedViewAccessor _view;

        private int _slotCount;
        private long _slotStride;
        private long _lastFrameIndex = -1;

        public FrameData Latest { get; private set; }
        public bool Mapped => _view != null;
        public long LastFrameIndex => _lastFrameIndex;

        public FrameChannelReader(string name)
        {
            _name = string.IsNullOrEmpty(name) ? "TABGPortalFrame" : name;
        }

        public bool TryOpen()
        {
            if (_view != null) return true;
            try
            {
                // Try session-local then global, matching the writer's Local\ / Global\ choice.
                _mmf = OpenExisting("Local\\" + _name) ?? OpenExisting("Global\\" + _name) ?? OpenExisting(_name);
                if (_mmf == null) return false;
                _view = _mmf.CreateViewAccessor(0, 0, MemoryMappedFileAccess.Read);

                if (_view.ReadInt32(0) != MAGIC)
                {
                    Close();
                    return false;
                }
                _slotCount = _view.ReadInt32(12);
                _slotStride = _view.ReadInt64(16);
                return true;
            }
            catch
            {
                Close();
                return false;
            }
        }

        private static MemoryMappedFile OpenExisting(string name)
        {
            try { return MemoryMappedFile.OpenExisting(name, MemoryMappedFileRights.Read); }
            catch { return null; }
        }

        // Read the newest completed frame. Returns false if nothing new / not mapped / torn.
        public bool Poll()
        {
            if (_view == null && !TryOpen()) return false;

            try
            {
                int latest = _view.ReadInt32(44);
                if (latest < 0 || latest >= _slotCount) return false;

                for (int attempt = 0; attempt < 4; attempt++)
                {
                    long descOff = DESC_TABLE_OFF + (long)latest * DESC_STRIDE;
                    long seq1 = _view.ReadInt64(descOff + 0);
                    if ((seq1 & 1) != 0) continue; // writer mid-write

                    long frameIndex = _view.ReadInt64(descOff + 8);
                    long tsUs = _view.ReadInt64(descOff + 16);
                    int w = _view.ReadInt32(descOff + 24);
                    int h = _view.ReadInt32(descOff + 28);
                    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;

                    var fd = new FrameData
                    {
                        Width = w,
                        Height = h,
                        FrameIndex = frameIndex,
                        TimestampUs = tsUs,
                        Pos = new Vector3(_view.ReadSingle(descOff + 32), _view.ReadSingle(descOff + 36), _view.ReadSingle(descOff + 40)),
                        Rot = new Quaternion(_view.ReadSingle(descOff + 44), _view.ReadSingle(descOff + 48), _view.ReadSingle(descOff + 52), _view.ReadSingle(descOff + 56)),
                        Fov = _view.ReadSingle(descOff + 60),
                        DepthConvention = _view.ReadInt32(descOff + 64),
                        RowsTopDown = (_view.ReadInt32(48) & 1) != 0,
                    };

                    int pixels = w * h;
                    long payloadOff = HEADER_BYTES + (long)latest * _slotStride;
                    fd.Color = new byte[pixels * 4];
                    fd.Depth = new float[pixels];
                    _view.ReadArray(payloadOff, fd.Color, 0, fd.Color.Length);
                    _view.ReadArray(payloadOff + (long)pixels * 4, fd.Depth, 0, fd.Depth.Length);

                    long seq2 = _view.ReadInt64(descOff + 0);
                    if (seq1 != seq2) continue; // torn read -> retry

                    Latest = fd;
                    bool advanced = frameIndex != _lastFrameIndex;
                    _lastFrameIndex = frameIndex;
                    return advanced;
                }
                return false;
            }
            catch
            {
                Close();
                return false;
            }
        }

        public bool IsStale(long maxAgeUs)
        {
            if (Latest == null) return true;
            long nowUs = (DateTime.UtcNow - new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc)).Ticks / 10L;
            return (nowUs - Latest.TimestampUs) > maxAgeUs;
        }

        private void Close()
        {
            try { _view?.Dispose(); } catch { }
            try { _mmf?.Dispose(); } catch { }
            _view = null;
            _mmf = null;
        }

        public void Dispose() { Close(); }
    }
}
