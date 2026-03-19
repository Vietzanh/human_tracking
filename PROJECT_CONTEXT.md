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
│   └── tracker_config.txt         # Tracker config
├── src/
│   ├── deepstream_tracking_app.py # Custom Python DeepStream app (WORKING)
│   └── custom_tracker/            # Custom BYTETrack tracker plugin
│       ├── customtracker.h        # BYTETrack algorithm header
│       ├── customtracker.cpp      # BYTETrack algorithm implementation
│       ├── gstcustomtracker.h     # GStreamer plugin header
│       ├── gstcustomtracker.cpp   # GStreamer plugin implementation
│       └── Makefile               # Build script
├── models/
│   └── best.onnx                   # YOLO model
└── .venv/                          # Python virtual environment
```

---

## Custom BYTETrack Tracker

### Overview
A custom GStreamer plugin that implements the **BYTETrack** multi-object tracking algorithm for DeepStream. It replaces the built-in NvDCF tracker with a simpler, more configurable tracking solution.

### Key Features
- **BYTETrack Algorithm**: Uses both high and low confidence detections for tracking
- **Two-stage Association**: First matches high-score detections, then recovers lost tracks using low-score detections
- **Configurable Parameters**:
  - `high-conf-threshold`: Detection confidence threshold for first association (default: 0.5)
  - `low-conf-threshold`: Detection confidence threshold for second association (default: 0.1)
  - `max-time-lost`: Maximum frames to keep lost track alive (default: 30)
  - `iou-threshold`: IoU threshold for matching detections to tracks (default: 0.3)
  - `frame-width`: Input frame width (default: 1920)
  - `frame-height`: Input frame height (default: 1080)

### Build Instructions (in Docker container)

```bash
cd /work/src/custom_tracker
make
sudo make install
```

### Plugin Registration
The plugin registers as `customtracker` GStreamer element.

### Pipeline Integration
To use in pipeline, replace `nvtracker` with `customtracker`:
```
... → nvinfer → customtracker → nvvideoconvert → nvdsosd → ...
```

### Files
| File | Description |
|------|-------------|
| `customtracker.h` | BYTETrack algorithm header with `CustomTracker` class |
| `customtracker.cpp` | BYTETrack algorithm implementation |
| `gstcustomtracker.h` | GStreamer plugin header with `GstCustomTracker` class |
| `gstcustomtracker.cpp` | GStreamer plugin implementation (element factory, metadata handling) |
| `Makefile` | Build script for the plugin |

---

## The Problem

**Issue:** The custom DeepStream app (`deepstream_tracking_app.py`) does not produce an output MP4 file - the file size remains 0 bytes.

**Root Cause Identified:**
- The working `deepstream-app` uses native DeepStream elements
- The custom app manually builds the pipeline with GStreamer elements
- Something in the pipeline is blocking data flow

---

## Debug Journey (2026-03-10)

### Latest Log (2026-03-10) - NEW ERROR FOUND!

```
[*] Phát hiện Pad: application/x-rtp
[*] RTP encoding: H264
[+] rtspsrc -> depay linked thành công (H264)!
[DEPAY] RTP packet #30
[-] ERROR: Internal data stream error.
    Debug: ../gst/rtsp/gstrtspsrc.c(6252): gst_rtspsrc_loop ():
streaming stopped, reason not-negotiated (-4)
```

**NEW FINDING:** This is a **caps negotiation error** (`not-negotiated -4`), different from the previous decoder issue!
- Data flows to depay (#30 shows up)
- But then negotiation fails between depay and decoder/parser
- Likely caused by the restrictive capsfilter

---

### Step 1: Added Debug Probes
Added probes at various points to trace data flow:
- Depay sink → shows RTP packets reaching
- Depay src/Parser src → shows H264 packets leaving depay
- Decoder sink → shows H264 reaching decoder
- Decoder src → shows decoded frames (NEVER FIRES!)
- Streammux src → shows frames reaching inference (NEVER FIRES!)
- OSD sink → shows frames reaching OSD (NEVER FIRES!)

### Step 2: Key Discovery - Data Flow Analysis

| Stage | Status | Evidence |
|-------|--------|----------|
| RTP → depay | ✅ Working | `[DEPAY] RTP packet #X` appears |
| depay → parser | ✅ Working | `[DEPAY] H264 packet #X` appears |
| parser → decoder | ✅ Working | `[DECODER_SINK] H264 nal #X` appears |
| **decoder → streammux** | ❌ **BLOCKED** | **`[DECODER] Frame` NEVER appears!** |
| streammux → inference | ❌ Blocked | `[STREAMMUX]` NEVER appears |
| OSD | ❌ Blocked | `[OSD]` NEVER appears |

**Critical Finding:** `nvv4l2decoder` receives H264 NALs but does NOT output decoded frames!

The decoder is consuming input but not producing output - this is the bottleneck!

### Step 3: Tried Solutions

1. **Removed queue between decoder and streammux** - Same issue
2. **Changed to fakesink** (removed file output) - Same issue (proves it's not file output blocking)
3. **Added capsfilter between parser and decoder** - Still testing

### Step 4: Current Pipeline Flow

```
rtspsrc → rtph264depay → h264parse → capsfilter → nvv4l2decoder
→ nvstreammux → nvinfer → nvtracker → nvvideoconvert → nvdsosd
→ queue → nvvideoconvert → fakesink
```

---

## Solutions Tried

### 1. Fixed Missing Elements (FIXED)
- **Problem:** Code referenced `mux` but never created it
- **Solution:** Added `mux = Gst.ElementFactory.make("qtmux", "mux")`

### 2. Changed fakesink to filesink (FIXED)
- **Problem:** Was using `fakesink` instead of `filesink`
- **Solution:** Changed to proper filesink with output path

### 3. Added Bus Handler (ADDED)
- Added error/warning message handler to catch pipeline issues

### 4. Fixed Tracker Config (CHANGED)
- Changed to match working config

### 5. Added Debug Probes (ADDED)
- Traced exactly where data stops flowing

### 6. Removed Queue Between Decoder and Streammux (TRIED)
- **Result:** Same issue - not the cause

### 7. Changed to Fakesink (TRIED)
- **Result:** Same issue - file output is NOT the problem

### 8. Added Capsfilter (TRIED - CAUSED NEW ERROR)
- Added `video/x-h264, profile=baseline` capsfilter between parser and decoder
- **Result:** NEW ERROR - Caps negotiation failed with `not-negotiated (-4)`
- **Reason:** The restrictive `profile=baseline` likely doesn't match the stream's actual profile (likely high or main)

### 9. Relaxed Capsfilter (CURRENT)
- Changed caps from `video/x-h264, profile=baseline, level=4` to just `video/x-h264`
- Removed profile restriction to allow decoder to handle different profiles

### 10. Pipeline Working! (2026-03-10)
- Relaxed capsfilter: Changed from `profile=baseline` to just `video/x-h264`
- **RESULT:** Pipeline is working! Data flows through all stages:
  - [DEPAY] RTP packets flowing
  - [STREAMMUX] Frame pushed to inference!
  - [OSD] Frame #1606+ showing
- **Next:** Switched from fakesink to filesink for actual output

### 11. Pipeline Fixed! (2026-03-11)
- Relaxed capsfilter fixed the data flow issue
- Pipeline now works: RTP → depay → parser → decoder → streammux → inference → tracker → OSD
- Data flows through all stages: [DEPAY] → [STREAMMUX] → [OSD] Frame #1-4
- Pipeline state reaches "playing"
- **NEW ISSUE FOUND:** Using filesink directly writes raw H264, not MP4 container!

### 12. Adding Encoder + Muxer for MP4
- **Problem:** filesink writes raw bitstream, not MP4 container
- **Solution:** Need encoder + muxer between OSD and filesink
- Tried: `nvv4l2h264enc` - property `insert-sps-pps` doesn't exist
- Tried: `nvv4l2h264enc` with different props - property `external-rc` doesn't exist
- Tried: `x264enc` - element returns None (not available in container)
- **Current Status:** Finding compatible encoder

### 13. Adding Encoder + Muxer (FIXED - 2026-03-11)
- Encoder `nvv4l2h264enc` created successfully!
- **Issue:** Link encoder → muxer failed (missing h264parse)
- **Fix:** Added h264parse between encoder and muxer
- **RESULT:** Pipeline is now FULLY WORKING! ✅
- Output MP4 file is generated correctly

### 14. Label Positioning (Added - 2026-03-11)
- **Feature:** Different label positions based on class
- `person` class: label at **bottom** of bbox
- `head` class: label at **top** of bbox
- Uses `y_offset` property in text_params

### Final Working Pipeline (Built-in Tracker)
```
rtspsrc → rtph264depay → h264parse → capsfilter → nvv4l2decoder
→ nvstreammux → nvinfer → nvtracker → nvvideoconvert → nvdsosd
→ queue → nvvideoconvert → nvv4l2h264enc → h264parse → qtmux → filesink
```

### Alternative: Custom BYTETrack Tracker Pipeline
```
rtspsrc → rtph264depay → h264parse → capsfilter → nvv4l2decoder
→ nvstreammux → nvinfer → customtracker → nvvideoconvert → nvdsosd
→ queue → nvvideoconvert → nvv4l2h264enc → h264parse → qtmux → filesink
```
**Note:** Replace `nvtracker` with `customtracker` to use the custom BYTETrack tracker. Build the plugin first with `make` in `src/custom_tracker/`.

### Features Implemented
1. ✅ RTSP stream input (from MediaMTX)
2. ✅ H264 decoding (nvv4l2decoder)
3. ✅ Object detection with YOLO (nvinfer)
4. ✅ Object tracking (nvtracker or customtracker)
5. ✅ OSD with bounding boxes and labels
6. ✅ Dynamic label positioning (person=bottom, head=top)
7. ✅ MP4 output with encoder + muxer
8. ✅ Only display "head" class (ignore "person")
9. ✅ Show tracking ID only for head class

### Pipeline Order (Important!)
```
1. RTSP Input
2. Decode (nvv4l2decoder)
3. Streammux
4. Detection (nvinfer)      ← Detection happens here
5. Tracking (nvtracker or customtracker)     ← Tracking happens here
6. OSD (nvdsosd)            ← Draws bounding boxes
7. ENCODE (nvv4l2h264enc)   ← Video encoding (AFTER tracking!)
8. Mux (qtmux)              ← MP4 container
9. Filesink                 ← Write to file
```
**Note:** Encoder is AFTER tracking, so it does NOT affect tracking efficiency.

### Tracker Properties (2026-03-11)
- Using original config: `config_tracker_NvDCF_perf.yml`
- Tried setting properties directly in code:
  - `max-shadow-tracking-age: 180` (longer tracking)
  - `min-detector-confidence: 0.25`
- Note: Some properties may require config file, not set_property()

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
ll-lib-file=/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so
ll-config-file=/opt/nvidia/deepstream/deepstream/samples/configs/deepstream-app/config_tracker_NvDCF_perf.yml
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

## Current Debug Status (2026-03-19)

### Issue: No Bounding Boxes in Output Video

Using custom tracker (`customtracker`) - video plays but no bounding boxes appear.

#### Root Cause Analysis - Critical Findings

**The ONNX model outputs `[x1, y1, x2, y2, conf, class_id]` in xyxy format, but the custom parser was reading it as `[cx, cy, w, h, conf, class_id]` in xywh format.**

Evidence from model_info.ipynb ONNX runtime test:
```
[[565.24, 232.26, 628.24, 434.29, 0.90722, 0]
 [ -0.09, 134.11,  44.05, 539.99, 0.79107, 0],  ← NEGATIVE x1! Only valid for xyxy, impossible for xywh]
```

The model was re-exported with `nms=True` (opset=12), which outputs xyxy format instead of the old xywh format.

#### Fix 1: Updated nvdsparsebbox_custom.cpp (xyxy format)

**Before (wrong for current ONNX):**
```cpp
obj.left = x;       // treated as center-x
obj.top = y;        // treated as center-y
obj.width = w;      // treated as width
obj.height = h;     // treated as height
```

**After (correct xyxy format):**
```cpp
obj.left = x1;      // x1 = left corner
obj.top = y1;       // y1 = top corner
obj.width = x2 - x1; // width from x2 - x1
obj.height = y2 - y1; // height from y2 - y1
```

#### Fix 2: TensorRT Engine Mismatch

The pre-built engine `best.onnx_b1_gpu0_fp16.engine` was built from an **older ONNX** with different output format. After deleting the engine and rebuilding, the format now matches.

```bash
rm -f /work/models/best.onnx_b1_gpu0_fp16.engine
cd /work/deepstream && ./run.sh  # Rebuilds engine from current ONNX
```

Verified engine output with trtexec:
```
Input binding: images 1x3x640x640
Output binding: output0 1x300x6  ✅ Matches parser
```

#### Fix 3: Custom Tracker Makefile - Library Path

The custom tracker `.so` was missing runtime library path for DeepStream libs.

**Changes to Makefile:**
```makefile
# Fixed: DS_LIB now points to DS 7.0 correct path
DS_LIB = /opt/nvidia/deepstream/deepstream-7.0/lib

# Added RPATH so runtime loader can find DeepStream libraries
LINK_FLAGS = -shared -Wl,-soname,$(TARGET) -Wl,-rpath,$(DS_LIB) $(LDFLAGS) $(LIBS)
```

#### Build Instructions (Updated)

```bash
# 1. Rebuild YOLO parser
cd /work/deepstream && make clean && make

# 2. Rebuild custom tracker with fixed Makefile
cd /work/src/custom_tracker && make clean && make && sudo make install

# 3. Rebuild TensorRT engine (if ONNX model changed)
rm -f /work/models/best.onnx_b1_gpu0_fp16.engine
cd /work/deepstream && ./run.sh

# 4. Run custom app with correct library path
export LD_LIBRARY_PATH=/opt/nvidia/deepstream/deepstream-7.0/lib:$LD_LIBRARY_PATH
GST_DEBUG=2 python3 /work/src/deepstream_tracking_app.py
```

#### Current Status

**Build system:**
- ✅ YOLO parser rebuilt with xyxy fix
- ✅ Custom tracker rebuilt with fixed Makefile (DS_LIB + RPATH)
- ✅ TensorRT engine rebuilt from current ONNX

**Pipeline behavior:**
- ✅ C++ parser correctly parses detections with proper bbox sizes
- ✅ Metadata attaches to `obj_meta_list` (detections visible in Python probes)
- ✅ Built-in NvDCF tracker works AND produces bounding boxes in output video
- ✅ `./run.sh` with built-in tracker outputs correct video with bounding boxes
- ✅ Pipeline elements all link correctly (`primary-inference -> tracker` succeeds)
- ❌ Custom `customtracker` crashes with **Segmentation fault**
- ❌ **No `[TRACKER_TRANSFORM]` debug messages ever appear** — `transform_ip()` is never called
- ❌ No `[OSD]` messages appear — pipeline dies before reaching OSD

**Key findings:**
1. `transform_ip()` is **never called** — GStreamer never reaches the custom tracker's processing function
2. Crash happens in C++ layer after CUSTOM_PARSER log (before any Python probe)
3. Even with PGIE probe **disabled**, still no `[TRACKER]` messages → tracker itself is crashing
4. Built-in `./run.sh` works perfectly with bounding boxes — proves pipeline, parser, and metadata are all correct

**Latest changes (not yet tested):**
- Added `std::cerr` print at start of `transform_ip()` to bypass GST_DEBUG
- Set `passthrough_on_same_caps = FALSE` to force `transform_ip()` to be called
- Next step: rebuild and test

#### Files Being Debugged

| File | Purpose | Status |
|------|---------|--------|
| `nvdsparsebbox_custom.cpp` | YOLO parser | ✅ Fixed xyxy format |
| `customtracker.cpp` | BYTETrack algorithm | Working |
| `customtracker.h` | BYTETrack header | Working |
| `gstcustomtracker.cpp` | GStreamer plugin | 🔍 Debugging - transform_ip never called |
| `Makefile` | Custom tracker build | ✅ Fixed DS_LIB path + RPATH |
| `pgie_config.txt` | YOLO inference config | ✅ cluster-mode=1 |
| `deepstream_tracking_app.py` | Main app | PGIE probe disabled for testing |
| `model_info.ipynb` | Model analysis | ✅ Confirmed xyxy ONNX output |

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

1. ✅ ~~Why does `nvv4l2decoder` not output frames when linked manually but works with `deepstream-app`?~~ — FIXED: Capsfilter profile issue
2. ✅ ~~ONNX model output format changed (xyxy vs xywh)~~ — FIXED: Parser updated
3. ✅ ~~Pre-built TensorRT engine format mismatch~~ — FIXED: Engine rebuilt from current ONNX
4. ✅ ~~Bounding boxes in output~~ — CONFIRMED: `./run.sh` produces bounding boxes correctly
5. ❌ **Custom tracker segfault** — `transform_ip()` never called. Latest fix: disabled `passthrough_on_same_caps`, added `std::cerr` debug print. Needs rebuild + test.
