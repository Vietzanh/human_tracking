#include "nvdsinfer_custom_impl.h"
#include <vector>
#include <iostream>
#include <cmath>

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
    int num_elements = layer.inferDims.numElements;

    std::cout << "[CUSTOM_PARSER] Output: numElements=" << num_elements
              << ", Image: " << networkInfo.width << "x" << networkInfo.height << std::endl;

    // Debug: Print first 5 detections
    std::cout << "[CUSTOM_PARSER] First 5 detections:" << std::endl;
    for (int i = 0; i < 5; i++) {
        float x1 = data[i*6+0];
        float y1 = data[i*6+1];
        float x2 = data[i*6+2];
        float y2 = data[i*6+3];
        float conf = data[i*6+4];
        int class_id = (int)data[i*6+5];
        std::cout << "  det" << i << ": x1=" << x1 << ", y1=" << y1
                  << ", x2=" << x2 << ", y2=" << y2
                  << ", conf=" << conf << ", class=" << class_id << std::endl;
    }

    std::cout << "[CUSTOM_PARSER] Threshold: " << detectionParams.perClassPreclusterThreshold[0] << std::endl;

    // Parse detections
    int parsed_count = 0;
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
        parsed_count++;

        // Debug first few
        if (parsed_count <= 3) {
            std::cout << "[CUSTOM_PARSER] Added obj" << parsed_count
                      << ": classId=" << obj.classId
                      << ", conf=" << obj.detectionConfidence
                      << ", left=" << obj.left
                      << ", top=" << obj.top
                      << ", w=" << obj.width
                      << ", h=" << obj.height << std::endl;
        }
    }

    std::cout << "[CUSTOM_PARSER] Total detections: " << parsed_count << std::endl;
    std::cout << "[CUSTOM_PARSER] objectList.size()=" << objectList.size() << std::endl;

    return true;
}