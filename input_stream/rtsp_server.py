import gi
gi.require_version('Gst', '1.0')
gi.require_version('GstRtspServer', '1.0')

from gi.repository import Gst, GstRtspServer, GLib

Gst.init(None)

class RTSPServer:
    def __init__(self):
        self.server = GstRtspServer.RTSPServer()
        self.server.set_service("8554")

        factory = GstRtspServer.RTSPMediaFactory()
        factory.set_launch(
            '( filesrc location=/home/anhtv/ETC/Human_tracking/data/output_h264.mp4 ! '
            'qtdemux ! '
            'h264parse ! '
            'queue ! '
            'rtph264pay config-interval=1 pt=96 name=pay0 )'
        )
        factory.set_shared(True)

        mount_points = self.server.get_mount_points()
        mount_points.add_factory("/input", factory)

        self.server.attach(None)
        print("RTSP Stream ready at rtsp://localhost:8554/input")

if __name__ == "__main__":
    server = RTSPServer()
    loop = GLib.MainLoop()
    loop.run()