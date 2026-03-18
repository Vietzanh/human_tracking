#include "nvdsinfer_custom_impl.h"
#include <vector>
#include <iostream>
#include <cmath>

// YOLOv8 output: [1, 300, 6] - standard detection format
// Per detection: [x, y, w, h, conf, class_id]

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
        std::cout << "  det" << i << ": x=" << data[i*6+0]
                  << ", y=" << data[i*6+1]
                  << ", w=" << data[i*6+2]
                  << ", h=" << data[i*6+3]
                  << ", conf=" << data[i*6+4]
                  << ", class=" << (int)data[i*6+5] << std::endl;
    }

    std::cout << "[CUSTOM_PARSER] Threshold: " << detectionParams.perClassPreclusterThreshold[0] << std::endl;

    // Parse detections
    int parsed_count = 0;
    for (int i = 0; i < NUM_DETECTIONS; i++) {
        float x = data[i * 6 + 0];
        float y = data[i * 6 + 1];
        float w = data[i * 6 + 2];
        float h = data[i * 6 + 3];
        float conf = data[i * 6 + 4];
        int class_id = (int)data[i * 6 + 5];

        // Skip low confidence
        if (conf < detectionParams.perClassPreclusterThreshold[0])
            continue;

        // Skip invalid class_id (only 0 and 1 are valid for person/head)
        if (class_id < 0 || class_id > 1)
            continue;

        NvDsInferObjectDetectionInfo obj;
        obj.classId = class_id;
        obj.detectionConfidence = conf;
        obj.left = x;
        obj.top = y;
        obj.width = w;
        obj.height = h;

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