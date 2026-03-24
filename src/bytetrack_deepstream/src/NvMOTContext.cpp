#include "Tracker.h"
#include "BYTETracker.h"
#include <fstream>
#include <map>
#include <cstring>

NvMOTContext::NvMOTContext(const NvMOTConfig &configIn, NvMOTConfigResponse &configResponse) {
    configResponse.summaryStatus = NvMOTConfigStatus_OK;
}

NvMOTStatus NvMOTContext::processFrame(const NvMOTProcessParams *params, NvMOTTrackedObjBatch *pTrackedObjectsBatch) {
    for (uint streamIdx = 0; streamIdx < pTrackedObjectsBatch->numFilled; streamIdx++){
        NvMOTTrackedObjList   *trackedObjList = &pTrackedObjectsBatch->list[streamIdx];
        NvMOTFrame            *frame          = &params->frameList[streamIdx];

        // Build input vector for BYTETracker
        std::vector<NvObject> nvObjects;
        nvObjects.reserve(frame->objectsIn.numFilled);
        for (uint32_t i = 0; i < frame->objectsIn.numFilled; i++) {
            NvMOTObjToTrack *obj = &frame->objectsIn.list[i];
            NvObject nvObject;
            nvObject.prob    = obj->confidence;
            nvObject.label   = obj->classId;
            nvObject.rect[0] = obj->bbox.x;
            nvObject.rect[1] = obj->bbox.y;
            nvObject.rect[2] = obj->bbox.width;
            nvObject.rect[3] = obj->bbox.height;
            nvObject.associatedObjectIn = obj;
            nvObjects.emplace_back(nvObject);
        }

        // Get or create tracker for this stream
        if (byteTrackerMap.find(frame->streamID) == byteTrackerMap.end())
            byteTrackerMap.insert(std::pair<uint64_t, std::shared_ptr<BYTETracker>>(frame->streamID, std::make_shared<BYTETracker>(15, 30)));

        std::vector<STrack> outputTracks = byteTrackerMap.at(frame->streamID)->update(nvObjects);

        // Build map: original object pointer -> STrack
        std::map<NvMOTObjToTrack*, STrack*> trackMap;
        for (STrack &sTrack: outputTracks) {
            if (sTrack.associatedObjectIn != nullptr)
                trackMap[sTrack.associatedObjectIn] = &sTrack;
        }

        // Allocate output buffer for all input objects
        // Each gets either a tracking ID (if tracked) or temp ID (if not)
        NvMOTTrackedObj *trackedObjs = new NvMOTTrackedObj[frame->objectsIn.numFilled];
        uint32_t filled = 0;

        for (uint32_t i = 0; i < frame->objectsIn.numFilled; i++) {
            NvMOTObjToTrack *inObj = &frame->objectsIn.list[i];
            NvMOTTrackedObj outObj;
            // Zero-initialize and fill
            memset(&outObj, 0, sizeof(NvMOTTrackedObj));
            outObj.classId     = inObj->classId;
            outObj.bbox        = inObj->bbox;
            outObj.confidence  = inObj->confidence;
            outObj.visibility  = 1.0f;
            outObj.associatedObjectIn = inObj;

            auto it = trackMap.find(inObj);
            if (it != trackMap.end()) {
                STrack *sTrack = it->second;
                outObj.trackingId = (uint64_t)sTrack->track_id;
                outObj.age        = (uint32_t)sTrack->tracklet_len;
                // Enable tracking on the input object
                inObj->doTracking = true;
            } else {
                outObj.trackingId = (uint64_t)(frame->frameNum * 1000 + i);
                outObj.age        = 0;
                inObj->doTracking = false;
            }
            trackedObjs[filled++] = outObj;
        }

        trackedObjList->numAllocated = frame->objectsIn.numFilled;
        trackedObjList->streamID     = frame->streamID;
        trackedObjList->frameNum     = frame->frameNum;
        trackedObjList->valid       = true;
        trackedObjList->list        = trackedObjs;
        trackedObjList->numFilled  = filled;
    }
    return NvMOTStatus_OK;
}

NvMOTStatus NvMOTContext::processFramePast(const NvMOTProcessParams *params,
                                           NvDsTargetMiscDataBatch *pPastFrameObjectsBatch) {
    return NvMOTStatus_OK;
}

NvMOTStatus NvMOTContext::removeStream(const NvMOTStreamId streamIdMask) {
    if (byteTrackerMap.find(streamIdMask) != byteTrackerMap.end()){
        std::cout << "Removing tracker for stream: " << streamIdMask << std::endl;
        byteTrackerMap.erase(streamIdMask);
    }
    return NvMOTStatus_OK;
}
