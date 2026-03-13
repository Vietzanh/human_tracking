from json import encoder

import gi
gi.require_version("Gst", "1.0")

from gi.repository import Gst
from gi.repository import GLib
import pyds

Gst.init(None)

# ─── Classes theo labelfile ───────────────────────────────────────────────────
pgie_classes_str = ["person", "head"]

# ─── Probe: depay sink - check if RTP data reaches depay ─────────────────────
_rtp_count = 0
def depay_sink_probe(pad, info, u_data):
    global _rtp_count
    _rtp_count += 1
    if _rtp_count % 30 == 0:
        print(f"[DEPAY] RTP packet #{_rtp_count}")
    return Gst.PadProbeReturn.OK

# ─── Probe: depay src - check if H264 data leaves depay ─────────────────────
_depay_count = 0
def depay_src_probe(pad, info, u_data):
    global _depay_count
    _depay_count += 1
    if _depay_count % 30 == 0:
        print(f"[DEPAY] H264 packet #{_depay_count}")
    return Gst.PadProbeReturn.OK

# ─── Probe: decoder sink - check if H264 reaches decoder ─────────────────────
_decoder_sink_count = 0
def decoder_sink_probe(pad, info, u_data):
    global _decoder_sink_count
    _decoder_sink_count += 1
    if _decoder_sink_count % 30 == 0:
        print(f"[DECODER_SINK] H264 nal #{_decoder_sink_count}")
    return Gst.PadProbeReturn.OK

# ─── Probe: decoder src - check if frames reach decoder ──────────────────────
_frame_count = 0
def decoder_src_probe(pad, info, u_data):
    global _frame_count
    _frame_count += 1
    if _frame_count % 30 == 0:  # Print every 30 frames
        print(f"[DECODER] Frame #{_frame_count}")
    return Gst.PadProbeReturn.OK

# ─── Probe: streammux src - check if frames leave streammux ──────────────────
def streammux_src_probe(pad, info, u_data):
    print("[STREAMMUX] Frame pushed to inference!")
    return Gst.PadProbeReturn.OK

# ─── Probe: OSD – vẽ bbox + tên class + "anhtv" ──────────────────────────────
_frame_count_osd = 0
def osd_sink_pad_buffer_probe(pad, info, u_data):
    global _frame_count_osd
    _frame_count_osd += 1
    print(f"[OSD] Frame #{_frame_count_osd}")

    gst_buffer = info.get_buffer()
    if not gst_buffer:
        return Gst.PadProbeReturn.OK

    batch_meta = pyds.gst_buffer_get_nvds_batch_meta(hash(gst_buffer))
    if not batch_meta:
        return Gst.PadProbeReturn.OK

    l_frame = batch_meta.frame_meta_list
    while l_frame:
        frame_meta = pyds.NvDsFrameMeta.cast(l_frame.data)
        l_obj = frame_meta.obj_meta_list

        while l_obj:
            obj_meta = pyds.NvDsObjectMeta.cast(l_obj.data)

            # Label: show ID only for head, not for person
            class_name = pgie_classes_str[obj_meta.class_id] \
                if obj_meta.class_id < len(pgie_classes_str) else "unknown"

            # Only display "head" class, ignore "person"
            if class_name != "head":
                # Skip displaying person - set display text to empty
                obj_meta.text_params.display_text = ""
            else:
                # Show ID for head
                track_id = obj_meta.object_id
                obj_meta.text_params.display_text = f"{track_id} | {class_name} | anhtv"

            # Position: head -> top of bbox
            rect_top = obj_meta.rect_params.top
            rect_height = obj_meta.rect_params.height
            rect_left = obj_meta.rect_params.left
            rect_width = obj_meta.rect_params.width

            # DEBUG: Print bbox values to check normalized vs pixel
            print(f"[DEBUG] Pixel bbox: left={rect_left}, top={rect_top}, "
                  f"width={rect_width}, height={rect_height}")
            print(f"[DEBUG] Frame size: {frame_meta.source_width}x{frame_meta.source_height}")

            # For head, position at top of bbox
            if class_name == "head":
                obj_meta.text_params.y_offset = int(rect_top - 25) if rect_top > 25 else int(rect_top)
                # Position at top of bbox
                obj_meta.text_params.y_offset = int(rect_top - 25) if rect_top > 25 else int(rect_top)

            # Style chữ
            obj_meta.text_params.font_params.font_name = "Serif"
            obj_meta.text_params.font_params.font_size = 15   # khớp text-size=15
            obj_meta.text_params.font_params.font_color.set(1.0, 1.0, 1.0, 1.0)  # trắng
            obj_meta.text_params.set_bg_clr = 1
            obj_meta.text_params.text_bg_clr.set(0.0, 0.0, 1.0, 0.5)            # nền xanh dương

            # Style bounding box (border-width=2 khớp config [osd])
            obj_meta.rect_params.border_color.set(0.0, 1.0, 0.0, 1.0)           # viền xanh lá
            obj_meta.rect_params.border_width = 2

            try:
                l_obj = l_obj.next
            except StopIteration:
                break

        try:
            l_frame = l_frame.next
        except StopIteration:
            break

    return Gst.PadProbeReturn.OK

# ─── Callback: link rtspsrc → depay động ─────────────────────────────────────
def cb_newpad(src, new_pad, depay):
    caps = new_pad.get_current_caps()
    if not caps:
        caps = new_pad.query_caps(None)

    structure = caps.get_structure(0)
    name = structure.get_name()
    print(f"[*] Phát hiện Pad: {name}")

    if name == "application/x-rtp":
        encoding = structure.get_string("encoding-name")
        print(f"[*] RTP encoding: {encoding}")

        if encoding == "H264":
            sink_pad = depay.get_static_pad("sink")
            if not sink_pad.is_linked():
                ret = new_pad.link(sink_pad)
                if ret == Gst.PadLinkReturn.OK:
                    print("[+] rtspsrc -> depay linked thành công (H264)!")

                    # Add probe on depay src AFTER linking
                    GLib.timeout_add(500, add_depay_src_probe, depay)
                else:
                    print(f"[-] LỖI: rtspsrc không thể link vào depay. Mã lỗi: {ret}")

# Add depay src probe after delay to ensure pad exists
def add_depay_src_probe(depay):
    print("[*] Adding depay src probe...")
    try:
        depay_src_pad = depay.get_static_pad("src")
        depay_src_pad.add_probe(Gst.PadProbeType.BUFFER, depay_src_probe, 0)
        print("[+] Depay src probe added!")
    except Exception as e:
        print(f"[-] Error adding depay src probe: {e}")
    return False  # Don't repeat

# ─── Helper: link tuần tự có kiểm tra ───────────────────────────────────────
def link_elements_with_check(elements):
    for i in range(len(elements) - 1):
        if not elements[i].link(elements[i + 1]):
            print(f"[-] LỖI KẾT NỐI: {elements[i].get_name()} -> {elements[i+1].get_name()}")
            return False
        else:
            print(f"[*] Kéo link: {elements[i].get_name()} -> {elements[i+1].get_name()}")
    return True

# ─── Bus message handler ─────────────────────────────────────────────────────
_pipeline = None

def bus_call(bus, message, loop):
    global _pipeline
    msg_type = message.type
    if msg_type == Gst.MessageType.ERROR:
        err, debug = message.parse_error()
        print(f"[-] ERROR: {err.message}")
        if debug:
            print(f"    Debug: {debug}")
    elif msg_type == Gst.MessageType.WARNING:
        err, debug = message.parse_warning()
        print(f"[!] WARNING: {err.message}")
    elif msg_type == Gst.MessageType.EOS:
        print("[*] EOS received")
        loop.quit()
    elif msg_type == Gst.MessageType.STATE_CHANGED:
        if _pipeline and message.src == _pipeline:
            old, new, pending = message.parse_state_changed()
            print(f"[*] Pipeline state changed: {old.value_nick} -> {new.value_nick}")
    return True

# ─── Main ─────────────────────────────────────────────────────────────────────
def main():
    global _pipeline
    pipeline = Gst.Pipeline()
    _pipeline = pipeline

    # ── 1. KHỞI TẠO ELEMENTS ─────────────────────────────────────────────────
    source   = Gst.ElementFactory.make("rtspsrc",        "rtsp-source")
    depay    = Gst.ElementFactory.make("rtph264depay",   "depay")
    parser   = Gst.ElementFactory.make("h264parse",      "parser")
    parser.set_property("config-interval", 1)  # Insert SPS/PPS every second

    # Add capsfilter to ensure proper format between parser and decoder
    # Use relaxed caps - let decoder handle different profiles
    capsfilter = Gst.ElementFactory.make("capsfilter", "capsfilter")
    caps = Gst.Caps.from_string("video/x-h264")  # Relaxed - no profile restriction
    capsfilter.set_property("caps", caps)
    # Use NVIDIA decoder
    decoder  = Gst.ElementFactory.make("nvv4l2decoder",  "decoder")
    queue    = Gst.ElementFactory.make("queue",          "queue")
    streammux= Gst.ElementFactory.make("nvstreammux",    "streammux")
    pgie     = Gst.ElementFactory.make("nvinfer",        "primary-inference")
    tracker  = Gst.ElementFactory.make("nvtracker",      "tracker")
    nvvidconv= Gst.ElementFactory.make("nvvideoconvert", "nvvidconv")
    nvosd    = Gst.ElementFactory.make("nvdsosd",        "onscreendisplay")

    # Queue for better buffering
    queue_osd = Gst.ElementFactory.make("queue", "queue-osd")

    # Post-OSD convert to system memory
    nvvidconv_post_osd = Gst.ElementFactory.make("nvvideoconvert", "nvvidconv_post_osd")

    # Add encoder + muxer for MP4 output
    # Try multiple encoder options
    encoder = Gst.ElementFactory.make("nvv4l2h264enc", "encoder")
    encoder_name = "nvv4l2h264enc"
    if encoder is None:
        print("[-] Failed to create nvv4l2h264enc, trying avenc_h264...")
        encoder = Gst.ElementFactory.make("avenc_h264", "encoder")
        encoder_name = "avenc_h264"
    if encoder is None:
        print("[-] Failed to create avenc_h264, trying x264enc...")
        encoder = Gst.ElementFactory.make("x264enc", "encoder")
        encoder_name = "x264enc"
    if encoder is None:
        print("[-] CRITICAL: No encoder available!")
        return

    print(f"[+] Using encoder: {encoder_name}")

    # Use h264parse to convert encoder output to stream format
    h264parse = Gst.ElementFactory.make("h264parse", "h264parse")

    # Use qtmux for MP4 container
    muxer = Gst.ElementFactory.make("qtmux", "muxer")

    # Use filesink for output
    filesink = Gst.ElementFactory.make("filesink", "filesink")
    filesink.set_property("location", "/work/src/output.mp4")
    filesink.set_property("sync", False)

    # Kiểm tra element tạo thành công
    required = [
        source, depay, parser, capsfilter, decoder, streammux,
        pgie, tracker, nvvidconv, nvosd, queue_osd,
        nvvidconv_post_osd, encoder, h264parse, muxer, filesink
    ]
    for el in required:
        if not el:
            print(f"[-] Không thể tạo element: {el}")
            return

    # ── 2. CẤU HÌNH PROPERTIES ─────────────────────────────────────────────
    # [encoder] - configure based on which encoder we have
    if encoder_name == "nvv4l2h264enc":
        encoder.set_property("bitrate", 4000000)  # 4 Mbps
    elif encoder_name == "avenc_h264":
        encoder.set_property("bitrate", 4000)  # 4 Mbps
    elif encoder_name == "x264enc":
        encoder.set_property("bitrate", 4000)  # 4 Mbps
        encoder.set_property("speed-preset", "ultrafast")

    # [source0]
    source.set_property("location",        "rtsp://127.0.0.1:8554/input_stream")
    source.set_property("latency",         200)
    source.set_property("drop-on-latency", True)
    source.set_property("protocols",       4)       # TCP

    # [streammux]
    streammux.set_property("width",                1920)
    streammux.set_property("height",               1080)
    streammux.set_property("batch-size",           1)
    streammux.set_property("live-source",          1)
    streammux.set_property("batched-push-timeout", 40000)  # 40ms - push frames faster

    # [primary-gie]
    pgie.set_property("config-file-path", "/work/deepstream/pgie_config.txt")

    # [tracker] – use original config + set properties directly
    tracker.set_property("ll-lib-file",
        "/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so")
    tracker.set_property("ll-config-file",
        "/opt/nvidia/deepstream/deepstream/samples/configs/deepstream-app/config_tracker_NvDCF_perf.yml")
    tracker.set_property("tracker-width",  1920)
    tracker.set_property("tracker-height", 1088)

    # Try setting tracker properties directly (may override config)
    # Note: Some properties may require config file, but let's try
    try:
        tracker.set_property("max-shadow-tracking-age", 180)  # Higher = longer tracking
        tracker.set_property("min-detector-confidence", 0.25)  # Lower = more detections
    except Exception as e:
        print(f"[!] Could not set tracker properties directly: {e}")

    # ── 3. ADD VÀO PIPELINE ─────────────────────────────────────────────────
    for el in required:
        pipeline.add(el)

    # ── 4. KẾT NỐI ──────────────────────────────────────────────────────────
    source.connect("pad-added", cb_newpad, depay)

    print("\n--- BẮT ĐẦU LINK ELEMENTS ---")

    # Decode chain → queue
    # Try linking decoder directly to streammux without queue
    link_elements_with_check([depay, parser, capsfilter, decoder])
    # Link decoder to streammux directly
    # Link decoder directly to streammux (no queue)
    decoder_src = decoder.get_static_pad("src")
    streammux_sink = streammux.get_request_pad("sink_0")
    if decoder_src.link(streammux_sink) == Gst.PadLinkReturn.OK:
        print("[*] Linked decoder -> streammux directly (no queue)")
    else:
        print("[-] ERROR: Failed to link decoder -> streammux")

    # DeepStream pipeline: streammux → pgie → tracker → nvvidconv → nvosd → queue → nvvidconv → encoder → h264parse → muxer → filesink
    link_elements_with_check([
        streammux, pgie, tracker, nvvidconv, nvosd, queue_osd,
        nvvidconv_post_osd, encoder, h264parse, muxer, filesink
    ])

    print("------------------------------\n")

    # ── 5. PROBES ────────────────────────────────────────────────────────────
    # Add probes to debug frame flow
    # Depay sink probe (this pad exists)
    depay_sink_pad = depay.get_static_pad("sink")
    depay_sink_pad.add_probe(Gst.PadProbeType.BUFFER, depay_sink_probe, 0)

    # Note: depay src probe is added in cb_newpad callback after pad linking
    # Parser src probe (to check if depay outputs data)
    parser_src_pad = parser.get_static_pad("src")
    parser_src_pad.add_probe(Gst.PadProbeType.BUFFER, depay_src_probe, 0)  # Reuse for simplicity

    # Decoder sink probe (to check if H264 reaches decoder)
    decoder_sink_pad = decoder.get_static_pad("sink")
    decoder_sink_pad.add_probe(Gst.PadProbeType.BUFFER, decoder_sink_probe, 0)

    decoder_src_pad = decoder.get_static_pad("src")
    decoder_src_pad.add_probe(Gst.PadProbeType.BUFFER, decoder_src_probe, 0)

    streammux_src_pad = streammux.get_static_pad("src")
    streammux_src_pad.add_probe(Gst.PadProbeType.BUFFER, streammux_src_probe, 0)

    osd_sink_pad = nvosd.get_static_pad("sink")
    osd_sink_pad.add_probe(Gst.PadProbeType.BUFFER, osd_sink_pad_buffer_probe, 0)

    # ── 6. CHẠY ──────────────────────────────────────────────────────────────
    loop = GLib.MainLoop()

    # Add bus handler
    bus = pipeline.get_bus()
    bus.add_signal_watch()
    bus.connect("message", bus_call, loop)

    pipeline.set_state(Gst.State.PLAYING)
    print("Đang chạy...")
    print("  Output: /work/src/output.mp4")
    print("Nhấn Ctrl+C để dừng và lưu file.\n")

    try:
        loop.run()
    except KeyboardInterrupt:
        print("\n[*] Đang lưu file...")

        # Send EOS to pipeline
        pipeline.send_event(Gst.Event.new_eos())

        # Wait for EOS
        bus = pipeline.get_bus()
        msg = bus.timed_pop_filtered(5 * Gst.SECOND, Gst.MessageType.EOS)

        if msg:
            print("[+] Đã đóng file thành công!")
        else:
            print("[*] Timeout, file saved.")

    pipeline.set_state(Gst.State.NULL)
    print("[+] Đã lưu file output.mp4 an toàn!")

if __name__ == "__main__":
    main()
