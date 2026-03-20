#include "nvdsinfer_custom_impl.h"
#include <vector>

// YOLOv8 ONNX output: [1, 300, 6]
// Per detection: [x1, y1, x2, y2, conf, class_id] (xyxy format, pixels in 640x640 space)
// After NMS, Ultralytics outputs xyxy NOT xywh!
// Verify: ONNX runtime test shows -0.09 for x1 (negative = valid corner coord, impossible for xywh)

#define NUM_DETECTIONS 300

extern "C"
bool NvDsInferParseCustomONNX(
    std::vector<NvDsInferLayerInfo> const &outputLayersInfo,
    NvDsInferNetworkInfo const &networkInfo,
    NvDsInferParseDetectionParams const &detectionParams,
    std::vector<NvDsInferObjectDetectionInfo> &objectList)
{
    const NvDsInferLayerInfo &layer = outputLayersInfo[0];

    float *data = (float *)layer.buffer;

    // Parse detections
    for (int i = 0; i < NUM_DETECTIONS; i++) {
        // ONNX outputs xyxy format: [x1, y1, x2, y2, conf, class_id]
        float x1 = data[i * 6 + 0];
        float y1 = data[i * 6 + 1];
        float x2 = data[i * 6 + 2];
        float y2 = data[i * 6 + 3];
        float conf = data[i * 6 + 4];
        int class_id = (int)data[i * 6 + 5];

        // Skip zero/empty detections
        if (conf < 0.001f)
            continue;

        // Skip low confidence
        if (conf < detectionParams.perClassPreclusterThreshold[0])
            continue;

        // Skip invalid class_id (only 0 and 1 are valid for person/head)
        if (class_id < 0 || class_id > 1)
            continue;

        NvDsInferObjectDetectionInfo obj;
        obj.classId = class_id;
        obj.detectionConfidence = conf;

        // Convert xyxy → left, top, width, height
        obj.left = x1;
        obj.top = y1;
        obj.width = x2 - x1;
        obj.height = y2 - y1;

        objectList.push_back(obj);
    }

    return true;
}