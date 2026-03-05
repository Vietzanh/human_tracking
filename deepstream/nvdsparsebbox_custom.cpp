#include "nvdsinfer_custom_impl.h"
#include <vector>

extern "C"
bool NvDsInferParseCustomONNX(
    std::vector<NvDsInferLayerInfo> const &outputLayersInfo,
    NvDsInferNetworkInfo const &networkInfo,
    NvDsInferParseDetectionParams const &detectionParams,
    std::vector<NvDsInferObjectDetectionInfo> &objectList)
{
    const NvDsInferLayerInfo &layer = outputLayersInfo[0];

    float *data = (float *)layer.buffer;
    int num_boxes = 300;

    for (int i = 0; i < num_boxes; i++) {
        float x1 = data[i * 6 + 0];
        float y1 = data[i * 6 + 1];
        float x2 = data[i * 6 + 2];
        float y2 = data[i * 6 + 3];
        float conf = data[i * 6 + 4];
        int class_id = (int)data[i * 6 + 5];

        if (conf < detectionParams.perClassPreclusterThreshold[0])
            continue;

        NvDsInferObjectDetectionInfo obj;
        obj.classId = class_id;
        obj.detectionConfidence = conf;
        obj.left = x1;
        obj.top = y1;
        obj.width = x2 - x1;
        obj.height = y2 - y1;

        objectList.push_back(obj);
    }

    return true;
}