import gi
gi.require_version('Gst', '1.0')
gi.require_version('GstRtspServer', '1.0')

from gi.repository import Gst, GstRtspServer, GLib

Gst.init(None)

class RTSPServer:
    def __init__(self):
        self.server = GstRtspServer.RTSPServer()
        self.server.set_service("8556")

        self.factory = GstRtspServer.RTSPMediaFactory()

        self.factory.set_launch(
            "( filesrc location=/work/data/output_h264.mp4 loop=true ! "
            "qtdemux name=demux demux.video_0 ! "
            "queue ! "
            "h264parse ! "
            "rtph264pay config-interval=1 pt=96 name=pay0 )"
        )

        # QUAN TRỌNG
        self.factory.set_shared(True)
        self.factory.set_eos_shutdown(False)
        self.factory.set_suspend_mode(GstRtspServer.RTSPSuspendMode.NONE)

        self.factory.connect("media-configure", self.on_media_configure)

        mount_points = self.server.get_mount_points()
        mount_points.add_factory("/input", self.factory)

        self.server.attach(None)
        print("RTSP Stream ready at rtsp://localhost:8556/input")

    def on_media_configure(self, factory, media):
        self.pipeline = media.get_element()
        bus = self.pipeline.get_bus()
        bus.add_signal_watch()
        bus.connect("message::eos", self.on_eos)

    def on_eos(self, bus, msg):
        # EOS received, try to wrap back to the beginning of the MP4 file
        print("EOS reached → Looping video")

        success = self.pipeline.seek(
            1.0,
            Gst.Format.TIME,
            Gst.SeekFlags.FLUSH | Gst.SeekFlags.KEY_UNIT,
            Gst.SeekType.SET, 0,
            Gst.SeekType.NONE, Gst.CLOCK_TIME_NONE
        )

        if not success:
            # if seek fails (e.g. demuxer complains about non-seekable
            # stream), reset state as a brute‑force fallback.
            print("Seek failed, resetting pipeline")
            self.pipeline.set_state(Gst.State.NULL)
            # allow a short delay so state change propagates
            self.pipeline.set_state(Gst.State.PLAYING)

if __name__ == "__main__":
    server = RTSPServer()
    loop = GLib.MainLoop()
    loop.run()