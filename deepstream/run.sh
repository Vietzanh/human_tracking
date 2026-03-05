#!/bin/bash

# RTSP_URL="rtsp://localhost:8554/input_stream"
CONFIG=deepstream_app_config.txt
LOG=deepstream.log

echo "Launching DeepStream..."
deepstream-app -c $CONFIG 2>&1 | tee $LOG