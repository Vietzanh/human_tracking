from json import encoder

import gi
gi.require_version("Gst", "1.0")

from gi.repository import Gst
from gi.repository import GLib
import pyds

Gst.init(None)

# ─── Classes theo labelfile ───────────────────────────────────────────────────
pgie_classes_str = ["person", "head"]

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

            # For head, position at top of bbox
            if class_name == "head":
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

# ─── Callback: link qtdemux → h264parse ───────────────────────────────────
def cb_newpad(src, new_pad, data):
    print(f"[*] Demux pad added: {new_pad.get_name()}")

    # Get the parser from data
    parser = data

    # Check if it's video
    caps = new_pad.get_current_caps()
    if not caps:
        caps = new_pad.query_caps(None)

    structure = caps.get_structure(0)
    name = structure.get_name()

    if name == "video/x-h264":
        print(f"[*] H264 stream detected!")
        sink_pad = parser.get_static_pad("sink")
        if not sink_pad.is_linked():
            ret = new_pad.link(sink_pad)
            if ret == Gst.PadLinkReturn.OK:
                print("[+] qtdemux -> h264parse linked!")
            else:
                print(f"[-] Failed to link: {ret}")

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

    # Input and output file paths
    input_file = "/work/data/output_h264.mp4"
    output_file = "/work/src/output_file.mp4"

    pipeline = Gst.Pipeline()
    _pipeline = pipeline

    # ── 1. KHỞI TẠO ELEMENTS ─────────────────────────────────────────────────
    # Use filesrc + qtdemux + h264parse for MP4 file input
    filesrc = Gst.ElementFactory.make("filesrc", "file-source")
    filesrc.set_property("location", input_file)

    # MP4 demuxer
    qtdemux = Gst.ElementFactory.make("qtdemux", "demux")

    # H264 parser
    h264parse_input = Gst.ElementFactory.make("h264parse", "h264parse-input")

    # Add capsfilter to ensure proper format between parser and decoder
    capsfilter = Gst.ElementFactory.make("capsfilter", "capsfilter")
    caps = Gst.Caps.from_string("video/x-h264")  # Relaxed - no profile restriction
    capsfilter.set_property("caps", caps)

    # Use NVIDIA decoder
    decoder = Gst.ElementFactory.make("nvv4l2decoder", "decoder")

    # Use NVIDIA decoder
    decoder = Gst.ElementFactory.make("nvv4l2decoder", "decoder")

    streammux = Gst.ElementFactory.make("nvstreammux", "streammux")
    pgie = Gst.ElementFactory.make("nvinfer", "primary-inference")
    tracker = Gst.ElementFactory.make("nvtracker", "tracker")
    nvvidconv = Gst.ElementFactory.make("nvvideoconvert", "nvvidconv")
    nvosd = Gst.ElementFactory.make("nvdsosd", "onscreendisplay")

    # Queue for better buffering
    queue_osd = Gst.ElementFactory.make("queue", "queue-osd")

    # Post-OSD convert to system memory
    nvvidconv_post_osd = Gst.ElementFactory.make("nvvideoconvert", "nvvidconv_post_osd")

    # Add encoder + muxer for MP4 output
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
    filesink.set_property("location", output_file)
    filesink.set_property("sync", False)

    # Kiểm tra element tạo thành công
    required = [
        filesrc, qtdemux, h264parse_input, capsfilter, decoder, streammux,
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

    # [streammux] - NOT live source for file input
    streammux.set_property("width", 1920)
    streammux.set_property("height", 1080)
    streammux.set_property("batch-size", 1)
    streammux.set_property("live-source", 0)  # NOT live for file input
    streammux.set_property("batched-push-timeout", 40000)

    # [primary-gie]
    pgie.set_property("config-file-path", "/work/deepstream/pgie_config.txt")

    # [tracker] – use original config
    tracker.set_property("ll-lib-file",
        "/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so")
    tracker.set_property("ll-config-file",
        "/opt/nvidia/deepstream/deepstream/samples/configs/deepstream-app/config_tracker_NvDCF_perf.yml")
    tracker.set_property("tracker-width", 1920)
    tracker.set_property("tracker-height", 1088)

    # Try setting tracker properties directly
    try:
        tracker.set_property("max-shadow-tracking-age", 180)
        tracker.set_property("min-detector-confidence", 0.25)
    except Exception as e:
        print(f"[!] Could not set tracker properties directly: {e}")

    # ── 3. ADD VÀO PIPELINE ─────────────────────────────────────────────────
    for el in required:
        pipeline.add(el)

    # ── 4. KẾT NỐI ──────────────────────────────────────────────────────────
    # Connect uridecodebin pad-added signal to link to decoder
    qtdemux.connect("pad-added", cb_newpad, h264parse_input)

    print("\n--- BẮT ĐẦU LINK ELEMENTS ---")

    # Link input chain: filesrc -> qtdemux -> h264parse -> decoder
    if not filesrc.link(qtdemux):
        print("[-] ERROR: Failed to link filesrc -> qtdemux")
    else:
        print("[*] Linked filesrc -> qtdemux")

    # Link h264parse -> capsfilter -> decoder
    if not h264parse_input.link(capsfilter):
        print("[-] ERROR: Failed to link h264parse -> capsfilter")
    else:
        print("[*] Linked h264parse -> capsfilter")

    if not capsfilter.link(decoder):
        print("[-] ERROR: Failed to link capsfilter -> decoder")
    else:
        print("[*] Linked capsfilter -> decoder")

    # Link decoder to streammux
    decoder_src = decoder.get_static_pad("src")
    streammux_sink = streammux.get_request_pad("sink_0")
    if decoder_src.link(streammux_sink) == Gst.PadLinkReturn.OK:
        print("[*] Linked decoder -> streammux")
    else:
        print("[-] ERROR: Failed to link decoder -> streammux")

    # DeepStream pipeline: streammux → pgie → tracker → nvvidconv → nvosd → queue → nvvidconv → encoder → h264parse → muxer → filesink
    link_elements_with_check([
        streammux, pgie, tracker, nvvidconv, nvosd, queue_osd,
        nvvidconv_post_osd, encoder, h264parse, muxer, filesink
    ])

    print("------------------------------\n")

    # ── 5. PROBES ────────────────────────────────────────────────────────────
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
    print(f"  Input: {input_file}")
    print(f"  Output: {output_file}")
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
    print(f"[+] Đã lưu file {output_file} an toàn!")

if __name__ == "__main__":
    main()
