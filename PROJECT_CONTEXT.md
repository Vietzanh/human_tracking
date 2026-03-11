# Human Tracking DeepStream Project

## Development Environment

**Note:** Debugging is done locally, but testing/running is performed inside a Docker container on an SSH server. Paths in code reference `/work/` as that's where the project is mounted in the container.

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
│   └── deepstream_tracking_app.py # Custom Python DeepStream app (NOT working)
├── models/
│   └── best.onnx                   # YOLO model
└── .venv/                          # Python virtual environment
```

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

### Final Working Pipeline
```
rtspsrc → rtph264depay → h264parse → capsfilter → nvv4l2decoder
→ nvstreammux → nvinfer → nvtracker → nvvideoconvert → nvdsosd
→ queue → nvvideoconvert → nvv4l2h264enc → h264parse → qtmux → filesink
```

### Features Implemented
1. ✅ RTSP stream input (from MediaMTX)
2. ✅ H264 decoding (nvv4l2decoder)
3. ✅ Object detection with YOLO (nvinfer)
4. ✅ Object tracking (nvtracker)
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
5. Tracking (nvtracker)     ← Tracking happens here
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
ll-lib-file=/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so
ll-config-file=/opt/nvidia/deepstream/deepstream/samples/configs/deepstream-app/config_tracker_NvDCF_perf.yml
tracker-width=1920
tracker-height=1088

[osd]
enable=1
border-width=2
text-size=15

[sink1]
enable=1
type=3           # File sink
container=1      # MP4
codec=1          # H264
bitrate=4000000
output-file=output.mp4
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

1. Why does `nvv4l2decoder` not output frames when linked manually but works with `deepstream-app`?
2. Does `deepstream-app` use different decoder configuration?
3. Is there a property missing on the decoder?
4. Should we try using DeepStream's `nvstreamdemux` or other DeepStream-specific elements?

---

## Notes for Future Debugging

The issue is specifically with **nvv4l2decoder not outputting decoded frames** despite receiving H264 input. This happens:
- Whether or not there's a queue between decoder and streammux
- Whether or not there's a file output (fakesink test confirmed this)

Possible next steps:
1. Try using software decoder (`avdec_h264`) - NOT available in container
2. Try adding more decoder properties (disable-dpb, etc.)
3. Investigate if DeepStream's internal handling differs from manual pipeline
4. Consider using DeepStream's native source elements instead of rtspsrc
