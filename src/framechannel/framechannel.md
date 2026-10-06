# Frame channel layout (slice 3, host-agnostic)

The guest (Portal) exports colour + depth to a named shared-memory mapping; the
host (TABG) reads the latest frame and composites it. This is the bulk data path
— pixels never go over the WebSocket. Modeled on the reference
`mc/.../FrameExporter.java` + `host/mcframe.py`, adapted for our conventions
(reversed-Z depth, explicit depth-convention flag, pose snapshot).

## Mapping name / handle

- **Windows** (the real target): a named file mapping. The guest creates
  `Local\TABGPortalFrame` (session-local) — or `Global\TABGPortalFrame` with the
  `SeCreateGlobalPrivilege`. Readers open the same name. (The C++ writer uses
  `CreateFileMappingA(INVALID_HANDLE_VALUE, ...)`.)
- **Linux** (tests on this box): a POSIX shared-memory object
  (`shm_open("/TABGPortalFrame")`, i.e. Python `multiprocessing.shared_memory`),
  which lands in `/dev/shm`. Identical byte layout; only the OS handle differs.

Default object name: `TABGPortalFrame`. Total size = `HEADER_BYTES +
slot_count * slot_stride`.

## Overall structure

```
+--------------------------------------------------+ 0
| global header (fixed fields)                     |
+--------------------------------------------------+ DESC_TABLE_OFF (256)
| slot descriptor[0]  (DESC_STRIDE = 128 bytes)    |
| slot descriptor[1]                               |
| ...                                              |
| slot descriptor[slot_count-1]                    |
+--------------------------------------------------+ HEADER_BYTES (4096)
| slot payload[0]   (slot_stride bytes)            |
| slot payload[1]                                  |
| slot payload[2]                                  |
+--------------------------------------------------+
```

3 rotating slots by default (triple-buffer: writer fills the next slot while a
reader may hold the current one and a third is quiescent).

### Global header (offset 0, little-endian)

| off | type | field | notes |
|----|------|-------|-------|
| 0  | i32 | `magic` | `0x31435054` = "TPC1" |
| 4  | i32 | `version` | layout version (1) |
| 8  | i32 | `header_size` | = `HEADER_BYTES` (4096); payload starts here |
| 12 | i32 | `slot_count` | number of rotating slots (3) |
| 16 | i64 | `slot_stride` | bytes per slot payload |
| 24 | i32 | `max_width` | capacity the mapping was sized for |
| 28 | i32 | `max_height` | |
| 32 | i32 | `depth_convention` | default depth convention (see below) |
| 36 | i64 | `published` | total frames published (monotonic) |
| 44 | i32 | `latest_slot` | most-recently-completed slot, `-1` if none |
| 48 | i32 | `flags` | bit0 `ROWS_TOP_DOWN` (row 0 = top). else bottom-up |

`DESC_TABLE_OFF = 256`, `DESC_STRIDE = 128`, `HEADER_BYTES = 4096`.

### Slot descriptor (offset `256 + i*128`, little-endian)

| off | type | field | notes |
|----|------|-------|-------|
| 0  | i64 | `seq` | **seqlock**; odd = write in progress |
| 8  | i64 | `frame_index` | guest frame counter |
| 16 | i64 | `timestamp_us` | guest capture time, µs since epoch |
| 24 | i32 | `width` | this frame's width |
| 28 | i32 | `height` | this frame's height |
| 32 | f32×3 | `pos` | camera position the frame was rendered with |
| 44 | f32×4 | `rot` | camera orientation quaternion (x,y,z,w) |
| 60 | f32 | `fov` | vertical FOV, degrees |
| 64 | i32 | `depth_convention` | per-frame override of the global default |
| 68 | i32 | `payload_size` | bytes actually written (rgba + depth) |
| 72 | i64 | `capture_ns` | perf-counter ns at capture (latency debugging) |
| 80 | i64 | `publish_ns` | perf-counter ns at publish |

The pose snapshot (`pos`,`rot`,`fov`) is **the pose the guest actually rendered
with**, so the host can re-project from it to its current camera (the reference's
latency-hiding trick) and tests can verify pose freshness.

### Slot payload (offset `4096 + i*slot_stride`)

Two tightly packed planes, each `width*height` elements, in row order per the
header `ROWS_TOP_DOWN` flag:

1. **colour**: `width*height` × RGBA8 (4 bytes), straight (non-premultiplied).
2. **depth**: `width*height` × float32, in the renderer's own depth convention
   (see below). Starts at payload offset `width*height*4`.

`slot_stride = ceil(max_width*max_height*(4+4) / 64) * 64` (64-byte aligned).

## Depth convention flag

The depth plane is stored in the **renderer's** convention; the reader linearises.
`depth_convention`:

| value | name | meaning |
|-------|------|---------|
| 0 | `LINEAR01` | already linear eye-depth normalised to `[0,1]` over `[near,far]` (0 = near) |
| 1 | `REVERSED_Z_01` | D3D reversed-Z window depth, range `[0,1]`, **1 = near, 0 = far** (Unity on D3D11, and our Source D3D9 INTZ path configured reversed) |
| 2 | `ZBUF_01` | standard D3D window depth `[0,1]`, 0 = near, 1 = far |
| 3 | `GL_NDC` | OpenGL-style NDC depth `[-1,1]` |
| 4 | `EYE_METRES` | raw view-space distance in world units (no near/far needed) |

Near/far planes needed to linearise non-`EYE_METRES` conventions are carried
out-of-band in the matching `cam`/`hello` link messages (they're constant for a
chamber), or default to the Source `CViewSetup` values; the reader takes them as
constructor args. `REVERSED_Z_01` is our expected default from the Source depth
export reversed to match Unity's `UNITY_REVERSED_Z` host side (see
`recon/MODDING_PLAN.md` §6, `recon/portal-notes.md` §3).

## Seqlock protocol

**Writer** (one writer only):
1. `slot = published % slot_count`
2. `seq += 1`  (now odd → "write in progress"); memory barrier
3. write descriptor fields, then the colour + depth planes
4. barrier; `seq += 1` (now even → complete)
5. `latest_slot = slot`; `published += 1`

**Reader** (many):
1. read `latest_slot`; if `< 0` → no frame yet
2. read `seq`; if odd → writer mid-write, retry
3. copy descriptor + payload
4. re-read `seq`; if it changed → torn read, retry (bounded retries)

Because there are 3 slots, a reader holding slot *k* is not the slot the writer
is filling (`published % 3`) until two more frames pass, so torn reads are rare;
the seqlock catches the rare case deterministically.

## Stale-slot detection (reader side)

The reader tracks the last `frame_index` it returned and the `timestamp_us`. A
caller can ask `is_stale(max_age_us)` → true if `now - timestamp_us` exceeds a
budget (guest stalled), and `latest()` reports whether `frame_index` advanced
since the previous call (duplicate frame). This matches the "pose freshness
within 1 frame" check the slice-3 test asserts.
