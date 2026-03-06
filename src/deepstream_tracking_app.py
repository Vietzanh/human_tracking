import gi
gi.require_version("Gst", "1.0")

from gi.repository import Gst
from gi.repository import GLib
import pyds

Gst.init(None)

# ─── Classes theo labelfile ───────────────────────────────────────────────────
pgie_classes_str = ["person", "head"]

# ─── Probe: decoder debug ─────────────────────────────────────────────────────
def decoder_probe(pad, info, u_data):
    print("[+] FRAME ĐÃ QUA DECODER THÀNH CÔNG!")
    return Gst.PadProbeReturn.OK

# ─── Probe: OSD – vẽ bbox + tên class + "anhtv" ──────────────────────────────
def osd_sink_pad_buffer_probe(pad, info, u_data):
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

            # Label: "ClassName | anhtv"
            class_name = pgie_classes_str[obj_meta.class_id] \
                if obj_meta.class_id < len(pgie_classes_str) else "unknown"
            obj_meta.text_params.display_text = f"{class_name} | anhtv"

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
                else:
                    print(f"[-] LỖI: rtspsrc không thể link vào depay. Mã lỗi: {ret}")

# ─── Helper: link tuần tự có kiểm tra ───────────────────────────────────────
def link_elements_with_check(elements):
    for i in range(len(elements) - 1):
        if not elements[i].link(elements[i + 1]):
            print(f"[-] LỖI KẾT NỐI: {elements[i].get_name()} -> {elements[i+1].get_name()}")
            return False
        else:
            print(f"[*] Kéo link: {elements[i].get_name()} -> {elements[i+1].get_name()}")
    return True

# ─── Main ─────────────────────────────────────────────────────────────────────
def main():
    pipeline = Gst.Pipeline()

    # ── 1. KHỞI TẠO ELEMENTS ─────────────────────────────────────────────────
    source   = Gst.ElementFactory.make("rtspsrc",        "rtsp-source")
    depay    = Gst.ElementFactory.make("rtph264depay",   "depay")
    parser   = Gst.ElementFactory.make("h264parse",      "parser")
    decoder  = Gst.ElementFactory.make("nvv4l2decoder",  "decoder")
    queue    = Gst.ElementFactory.make("queue",          "queue")
    streammux= Gst.ElementFactory.make("nvstreammux",    "streammux")
    pgie     = Gst.ElementFactory.make("nvinfer",        "primary-inference")
    tracker  = Gst.ElementFactory.make("nvtracker",      "tracker")
    nvvidconv= Gst.ElementFactory.make("nvvideoconvert", "nvvidconv")
    nvosd    = Gst.ElementFactory.make("nvdsosd",        "onscreendisplay")

    # Post-OSD convert + capsfilter (fix màu sắc)
    nvvidconv_post_osd = Gst.ElementFactory.make("nvvideoconvert", "nvvidconv_post_osd")
    capsfilter         = Gst.ElementFactory.make("capsfilter",     "capsfilter")
    caps_i420 = Gst.Caps.from_string("video/x-raw(memory:NVMM), format=I420")
    capsfilter.set_property("caps", caps_i420)

    encoder  = Gst.ElementFactory.make("nvv4l2h264enc", "encoder")
    parser2  = Gst.ElementFactory.make("h264parse",     "parser2")

    # Tee để chia ra 2 sink
    tee      = Gst.ElementFactory.make("tee",   "tee")

    # ── Sink0: RTSP output (type=4, rtsp-port=8556, udp-port=5400) ────────────
    queue_rtsp  = Gst.ElementFactory.make("queue",             "queue-rtsp")
    rtsp_sink   = Gst.ElementFactory.make("nvrtspoutsinkbin",  "rtsp-sink")

    # ── Sink1: File MP4 (type=3, container=1, output-file=output.mp4) ─────────
    queue_file  = Gst.ElementFactory.make("queue",   "queue-file")
    mux         = Gst.ElementFactory.make("qtmux",   "mux")
    filesink    = Gst.ElementFactory.make("filesink","filesink")

    # Kiểm tra element tạo thành công
    required = [
        source, depay, parser, decoder, queue, streammux,
        pgie, tracker, nvvidconv, nvosd,
        nvvidconv_post_osd, capsfilter, encoder, parser2,
        tee, queue_rtsp, rtsp_sink, queue_file, mux, filesink
    ]
    for el in required:
        if not el:
            print(f"[-] Không thể tạo element: {el}")
            return

    # ── 2. CẤU HÌNH PROPERTIES ─────────────────────────────────────────────
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
    streammux.set_property("batched-push-timeout", 40000)  # khớp config

    # [primary-gie]
    pgie.set_property("config-file-path", "../deepstream/pgie_config.txt")

    # [tracker] – dùng NvDCF_perf.yml, width=1920, height=1088
    tracker.set_property("ll-lib-file",
        "/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so")
    tracker.set_property("ll-config-file",
        "/opt/nvidia/deepstream/deepstream/samples/configs/deepstream-app/config_tracker_NvDCF_perf.yml")
    tracker.set_property("tracker-width",  1920)
    tracker.set_property("tracker-height", 1088)

    # Encoder (bitrate=4000000 khớp cả 2 sink)
    encoder.set_property("bitrate", 4000000)

    # [sink0] RTSP – port 8556, udp-port 5400
    rtsp_sink.set_property("port",     8556)
    rtsp_sink.set_property("udp-port", 5400)
    rtsp_sink.set_property("sync",     False)

    # [sink1] File
    filesink.set_property("location", "output.mp4")
    filesink.set_property("sync",     False)

    # ── 3. ADD VÀO PIPELINE ─────────────────────────────────────────────────
    for el in required:
        pipeline.add(el)

    # ── 4. KẾT NỐI ──────────────────────────────────────────────────────────
    source.connect("pad-added", cb_newpad, depay)

    print("\n--- BẮT ĐẦU LINK ELEMENTS ---")

    # Decode chain → queue
    link_elements_with_check([depay, parser, decoder, queue])

    # queue → streammux (request pad)
    sinkpad = streammux.get_request_pad("sink_0")
    srcpad  = queue.get_static_pad("src")
    if srcpad.link(sinkpad) == Gst.PadLinkReturn.OK:
        print("[*] Kéo link: queue -> streammux")
    else:
        print("[-] LỖI KẾT NỐI: queue -> streammux")

    # Inference + OSD chain → encoder → parser2 → tee
    link_elements_with_check([
        streammux, pgie, tracker, nvvidconv, nvosd,
        nvvidconv_post_osd, capsfilter, encoder, parser2, tee
    ])

    # tee → Sink0 (RTSP)
    tee_src_rtsp  = tee.get_request_pad("src_%u")
    queue_rtsp_sink = queue_rtsp.get_static_pad("sink")
    if tee_src_rtsp.link(queue_rtsp_sink) == Gst.PadLinkReturn.OK:
        print("[*] Kéo link: tee -> queue-rtsp")
    else:
        print("[-] LỖI KẾT NỐI: tee -> queue-rtsp")
    link_elements_with_check([queue_rtsp, rtsp_sink])

    # tee → Sink1 (File)
    tee_src_file   = tee.get_request_pad("src_%u")
    queue_file_sink = queue_file.get_static_pad("sink")
    if tee_src_file.link(queue_file_sink) == Gst.PadLinkReturn.OK:
        print("[*] Kéo link: tee -> queue-file")
    else:
        print("[-] LỖI KẾT NỐI: tee -> queue-file")
    link_elements_with_check([queue_file, mux, filesink])

    print("------------------------------\n")

    # ── 5. PROBES ────────────────────────────────────────────────────────────
    decoder_src_pad = decoder.get_static_pad("src")
    decoder_src_pad.add_probe(Gst.PadProbeType.BUFFER, decoder_probe, 0)

    osd_sink_pad = nvosd.get_static_pad("sink")
    osd_sink_pad.add_probe(Gst.PadProbeType.BUFFER, osd_sink_pad_buffer_probe, 0)

    # ── 6. CHẠY ──────────────────────────────────────────────────────────────
    loop = GLib.MainLoop()
    pipeline.set_state(Gst.State.PLAYING)
    print("Đang chạy...")
    print("  Sink0 (RTSP) : rtsp://localhost:8556/ds-test")
    print("  Sink1 (File) : output.mp4")
    print("Nhấn Ctrl+C để dừng và lưu file.\n")

    try:
        loop.run()
    except KeyboardInterrupt:
        print("\n[*] Đang gửi tín hiệu EOS. Vui lòng đợi file MP4 được lưu...")
        pipeline.send_event(Gst.Event.new_eos())
        bus = pipeline.get_bus()
        bus.timed_pop_filtered(
            Gst.CLOCK_TIME_NONE,
            Gst.MessageType.EOS | Gst.MessageType.ERROR
        )
        print("[+] Đã lưu file output.mp4 an toàn!")

    pipeline.set_state(Gst.State.NULL)

if __name__ == "__main__":
    main()