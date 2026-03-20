# Human Tracking DeepStream Project

## Development Environment

**Note:** Debugging is done locally, but testing/running is performed inside a Docker container on an SSH server. Paths in code reference `/work/` as that's where the project is mounted in the container.

**Rule:** Since you're running on server, commands that require local access (git, ls on local paths, etc.) won't work. Always provide commands that can be copy-pasted directly into the terminal on the server.

---

## Project Context

This is a DeepStream-based human tracking application that:
1. Receives an RTSP stream from MediaMTX server
2. Runs object detection (person, head) using YOLO model
3. Tracks detected objects
4. Draws bounding boxes with labels on the video
5. Outputs the processed video to an MP4 file

## Project Structure

```
/home/anhtv/ETC/Human_tracking/
├── deepstream/
│   ├── deepstream_app_config.txt   # Working DeepStream config (uses native file sink)
│   ├── pgie_config.txt            # Primary GIE (inference) config
│   └── config_tracker_BYTETracker.yml  # BYTETracker YAML config
├── src/
│   ├── deepstream_tracking_app.py # Custom Python DeepStream app
│   └── bytetrack_deepstream/      # Official BYTETracker (FoundationVision)
│       ├── includes/               # Header files
│       ├── src/                   # Implementation files
│       ├── cmake/                 # CMake modules
│       └── CMakeLists.txt         # Build config
├── models/
│   └── best.onnx                   # YOLO model
└── .venv/                          # Python virtual environment
```

---

## Official BYTETracker (FoundationVision)

### Overview
The project uses the **official BYTETracker implementation** from [FoundationVision/ByteTrack](https://github.com/FoundationVision/ByteTrack), integrated as a DeepStream NvMOT plugin. It replaces the built-in NvDCF tracker.

### Architecture
BYTETracker is loaded by the native `nvtracker` element via `ll-lib-file`. The pipeline uses `nvtracker` (not a custom GstBaseTransform element) — the tracker library implements `NvMOT_*` functions that DeepStream calls.

### Key Features
- **Kalman Filter**: 8-state (x, y, w, h, vx, vy, vw, vh) motion prediction
- **Hungarian Algorithm (LAPJV)**: Optimal IoU-based assignment — O(n³) globally optimal instead of greedy
- **Two-stage Association**: First matches high-score detections (`track_thresh`), then recovers lost tracks using low-score detections (`high_thresh`)
- **Duplicate Removal**: Removes overlapping tracks from tracked/lost lists

### Algorithm Steps (per frame)
1. Separate detections into high/low confidence by `high_thresh`
2. **First association**: Kalman-predict all tracks → IoU-match with high-conf dets → `match_thresh=0.8`
3. **Second association**: Match unmatched tracks with low-conf dets → `match_thresh=0.5`
4. **Unconfirmed tracks**: Match with high-conf dets → `match_thresh=0.7`
5. **New tracks**: Create from unmatched high-conf dets above `high_thresh`
6. Mark lost tracks exceeding `max_time_lost` → remove after `2×max_time_lost`

### Default Parameters
| Parameter | Default | Description |
|-----------|---------|-------------|
| `track_thresh` | 0.0 | Confidence threshold for creating tracks |
| `high_thresh` | 0.2 | Threshold for high/low detection separation |
| `match_thresh` | 0.8 | IoU threshold for first association |
| `track_buffer` | 30 | Frames to keep lost track alive (~1 sec at 30fps) |
| `max_time_lost` | `frame_rate/30 × track_buffer` | Auto-computed: 30fps → 30 frames |

### Build Instructions

```bash
# 1. Build the BYTETracker NvMOT library
cd /work/src/bytetrack_deepstream
mkdir -p build && cd build
cmake .. && make -j$(nproc)
sudo cp libnvds_bytetrack.so /opt/nvidia/deepstream/deepstream-7.0/lib/

# 2. Build YOLO parser
cd /work/deepstream && make clean && make

# 3. Run
python3 /work/src/deepstream_tracking_app.py
```

### Integration: How it works
The tracker config (`config_tracker_BYTETracker.yml`) is passed to `nvtracker` via `ll-config-file`. The library path (`libnvds_bytetrack.so`) is set via `ll-lib-file` in the app config.

### Files
| File | Description |
|------|-------------|
| `src/bytetrack_deepstream/includes/BYTETracker.h` | BYTETracker class header |
| `src/bytetrack_deepstream/includes/STrack.h` | Single tracklet (Kalman state, box coords) |
| `src/bytetrack_deepstream/includes/KalmanFilter.h` | Kalman filter header |
| `src/bytetrack_deepstream/includes/Lapjv.h` | LAPJV Hungarian algorithm header |
| `src/bytetrack_deepstream/includes/Tracker.h` | NvMOT context (maps streamID → BYTETracker) |
| `src/bytetrack_deepstream/src/BYTETracker.cpp` | Main update loop, 3 association stages |
| `src/bytetrack_deepstream/src/BYTETrackerUtils.cpp` | IoU distance, linear_assignment, LAPJV wrapper |
| `src/bytetrack_deepstream/src/KalmanFilter.cpp` | Kalman predict/update/project |
| `src/bytetrack_deepstream/src/STrack.cpp` | Track activation, reID, prediction |
| `src/bytetrack_deepstream/src/NvMOTContext.cpp` | DeepStream NvMOT plugin interface |
| `src/bytetrack_deepstream/src/Tracker.cpp` | NvMOT_Init/DeInit/Process/RemoveStreams |
| `src/bytetrack_deepstream/src/Lapjv.cpp` | Jonker-Volgenant LAP solver |
| `src/bytetrack_deepstream/CMakeLists.txt` | Build config |
| `deepstream/config_tracker_BYTETracker.yml` | Tracker YAML config |

### Bugs Fixed from Original Repo
- `NvMOTContext.cpp`: Fixed vector `push_back` on pre-sized vector (doubled entries)
- `NvMOTContext.cpp`: Fixed memory leak — allocated `NvMOTTrackedObj` per track unnecessarily
- `BYTETrackerUtils.cpp`: Fixed `sizeof(T)*n` → `n` in `new[]` allocations (was allocating `n×sizeof(T)` elements instead of `n`)
- Added missing `#include <vector>` in `STrack.h` and `BYTETracker.h`
- Updated CMakeLists for DS 7.0 paths + Eigen3

### Tracker Properties
BYTETracker parameters are set in the library source code (`BYTETracker.cpp`). The `track_buffer` parameter controls how long lost tracks are kept alive:

- `track_buffer = 30` → `max_time_lost = frame_rate/30 × 30` = 30 frames (~1 sec at 30fps)
- To change: edit `NvMOTContext.cpp` line 28 where `BYTETracker` is instantiated

---

## Working Configuration (deepstream_app_config.txt)

```ini
[source0]
type=4
uri=rtsp://127.0.0.1:8554/input_stream
latency=200
select-rtp-protocol=4

[streammux]
batch-size=1
width=1920
height=1080
batched-push-timeout=40000
live-source=1

[primary-gie]
config-file=pgie_config.txt

[tracker]
enable=1
tracker-width=1920
tracker-height=1088
ll-lib-file=/opt/nvidia/deepstream/deepstream-7.0/lib/libnvds_bytetrack.so
ll-config-file=config_tracker_BYTETracker.yml
gpu-id=0

[osd]
enable=1
gpu-id=0
border-width=2
text-size=15

[sink0]
enable=1
type=4
codec=1
bitrate=4000000
rtsp-port=8556
udp-port=5400
sync=0

[sink1]
enable=1
type=3
container=1
codec=1
bitrate=4000000
output-file=output_default.mp4
sync=0
```

## pgie's pgie_config.txt (YOLO inference config)

```ini
[property]
gpu-id=0
onnx-file=/work/models/best.onnx
# model-engine-file is intentionally removed - rebuild from ONNX each time
labelfile-path=/work/models/labels.txt
batch-size=1
network-mode=2
num-detected-classes=2
interval=0
gie-unique-id=1
process-mode=1

network-type=0
cluster-mode=1    # Important: use cluster-mode=1 with custom xyxy parser
net-scale-factor=0.003921568627
offsets=0;0;0
model-color-format=0
infer-dims=3;640;640

maintain-aspect-ratio=1
symmetric-padding=1

parse-bbox-func-name=NvDsInferParseCustomONNX
custom-lib-path=/work/deepstream/libnvdsparsebbox_custom.so

[class-attrs-all]
pre-cluster-threshold=0.25
nms-iou-threshold=0.45
```

---

## Commands to Run

```bash
# Terminal 1: Start MediaMTX
./mediamtx

# Terminal 2: Stream video to RTSP
ffmpeg -re -stream_loop -1 -i /work/data/output_h264.mp4 -c copy -rtsp_transport tcp -f rtsp rtsp://127.0.0.1:8554/input_stream

# Terminal 3: Run custom DeepStream app
python3 src/deepstream_tracking_app.py
```

---

## Open Questions

All issues have been resolved! 🎉

1. ✅ ~~Why does `nvv4l2decoder` not output frames when linked manually but works with `deepstream-app`?~~ — FIXED: Capsfilter profile issue
2. ✅ ~~ONNX model output format changed (xyxy vs xywh)~~ — FIXED: Parser updated
3. ✅ ~~Pre-built TensorRT engine format mismatch~~ — FIXED: Engine rebuilt from current ONNX
4. ✅ ~~Bounding boxes in output~~ — CONFIRMED: `./run.sh` produces bounding boxes correctly
5. ✅ ~~Custom tracker segfault~~ — FIXED: Vector size bug in `matched_high_dets` + IoU-based metadata matching
6. ✅ ~~Hand-rolled BYTETrack~~ — REPLACED: Official FoundationVision BYTETracker integrated as DeepStream NvMOT plugin
