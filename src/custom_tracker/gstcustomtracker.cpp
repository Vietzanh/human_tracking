/**
 * GStreamer Plugin for Custom BYTETrack Tracker
 *
 * This plugin integrates the BYTETrack algorithm into DeepStream pipeline.
 * It extracts detections from NvDsBatchMeta, runs tracking, and writes
 * tracking IDs back to the metadata.
 */

#include "gstcustomtracker.h"
#include <nvdsgst_meta.h>
#include <nvds_meta.h>

#include <iostream>
#include <cstring>

/* Debug category */
GST_DEBUG_CATEGORY_STATIC(GST_CAT_DEFAULT);

/* Plugin metadata */
static const gchar* plugin_short_desc =
    "Custom BYTETrack tracker for DeepStream";
static const gchar* plugin_description =
    "Multi-object tracker using BYTETrack algorithm";

/* Pad templates */
static GstStaticPadTemplate sink_factory =
    GST_STATIC_PAD_TEMPLATE(
        "sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(
            "video/x-raw(memory:NVMM), "
            "format = (string) { NV12, RGBA }, "
            "width = (int) [ 1, 8192 ], "
            "height = (int) [ 1, 8192 ]; "
            "video/x-raw, "
            "format = (string) { NV12, RGBA }, "
            "width = (int) [ 1, 8192 ], "
            "height = (int) [ 1, 8192 ]"
        )
    );

static GstStaticPadTemplate src_factory =
    GST_STATIC_PAD_TEMPLATE(
        "src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(
            "video/x-raw(memory:NVMM), "
            "format = (string) { NV12, RGBA }, "
            "width = (int) [ 1, 8192 ], "
            "height = (int) [ 1, 8192 ]; "
            "video/x-raw, "
            "format = (string) { NV12, RGBA }, "
            "width = (int) [ 1, 8192 ], "
            "height = (int) [ 1, 8192 ]"
        )
    );

/* Property definitions */
enum {
    PROP_0,
    PROP_HIGH_CONF_THRESHOLD,
    PROP_LOW_CONF_THRESHOLD,
    PROP_MAX_TIME_LOST,
    PROP_IOU_THRESHOLD,
    PROP_FRAME_WIDTH,
    PROP_FRAME_HEIGHT,
};

/* Forward declarations */
static void gst_custom_tracker_set_property(GObject* object, guint prop_id,
                                           const GValue* value, GParamSpec* pspec);
static void gst_custom_tracker_get_property(GObject* object, guint prop_id,
                                           GValue* value, GParamSpec* pspec);
static GstStateChangeReturn gst_custom_tracker_change_state(GstElement* element,
                                                            GstStateChange transition);
static gboolean gst_custom_tracker_set_caps(GstBaseTransform* trans,
                                            GstCaps* incaps, GstCaps* outcaps);
static GstFlowReturn gst_custom_tracker_transform_ip(GstBaseTransform* trans,
                                                      GstBuffer* buffer);

/* Initialize class */
static void gst_custom_tracker_class_init(GstCustomTrackerClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass* element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass* basetransform_class = GST_BASE_TRANSFORM_CLASS(klass);

    /* Set up parent class */
    gst_object_parent_class = g_type_class_peek_parent(klass);

    /* Set property handlers */
    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_custom_tracker_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_custom_tracker_get_property);

    /* Set element handlers */
    element_class->change_state = GST_DEBUG_FUNCPTR(gst_custom_tracker_change_state);

    /* Set transform handlers */
    basetransform_class->set_caps = GST_DEBUG_FUNCPTR(gst_custom_tracker_set_caps);
    basetransform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_custom_tracker_transform_ip);

    /* Set in-place transformation */
    basetransform_class->passthrough_on_same_caps = TRUE;

    /* Install properties */
    g_object_class_install_property(
        gobject_class, PROP_HIGH_CONF_THRESHOLD,
        g_param_spec_float("high-conf-threshold", "High Confidence Threshold",
                          "Detection confidence threshold for first association",
                          0.0f, 1.0f, 0.5f, G_PARAM_READWRITE));

    g_object_class_install_property(
        gobject_class, PROP_LOW_CONF_THRESHOLD,
        g_param_spec_float("low-conf-threshold", "Low Confidence Threshold",
                          "Detection confidence threshold for second association",
                          0.0f, 1.0f, 0.1f, G_PARAM_READWRITE));

    g_object_class_install_property(
        gobject_class, PROP_MAX_TIME_LOST,
        g_param_spec_int("max-time-lost", "Max Time Lost",
                        "Maximum frames to keep lost track alive",
                        1, 1000, 30, G_PARAM_READWRITE));

    g_object_class_install_property(
        gobject_class, PROP_IOU_THRESHOLD,
        g_param_spec_float("iou-threshold", "IoU Threshold",
                          "IoU threshold for matching detections to tracks",
                          0.0f, 1.0f, 0.3f, G_PARAM_READWRITE));

    g_object_class_install_property(
        gobject_class, PROP_FRAME_WIDTH,
        g_param_spec_int("frame-width", "Frame Width",
                        "Input frame width",
                        1, 8192, 1920, G_PARAM_READWRITE));

    g_object_class_install_property(
        gobject_class, PROP_FRAME_HEIGHT,
        g_param_spec_int("frame-height", "Frame Height",
                        "Input frame height",
                        1, 8192, 1080, G_PARAM_READWRITE));

    /* Set pad templates */
    gst_element_class_add_static_pad_template(element_class, &sink_factory);
    gst_element_class_add_static_pad_template(element_class, &src_factory);

    /* Set metadata */
    gst_element_class_set_static_metadata(element_class,
                                          "Custom BYTETrack Tracker",
                                          "Tracker/Video",
                                          plugin_description,
                                          "Custom Developer");

    GST_DEBUG_CATEGORY_INIT(GST_CAT_DEFAULT, GST_CUSTOM_TRACKER_NAME,
                           0, "Custom BYTETrack tracker plugin");
}

/* Initialize instance */
static void gst_custom_tracker_init(GstCustomTracker* tracker) {
    /* Initialize default configuration */
    tracker->config.high_confidence_threshold = 0.5f;
    tracker->config.low_confidence_threshold = 0.1f;
    tracker->config.max_time_lost = 30;
    tracker->config.iou_threshold = 0.3f;
    tracker->config.frame_width = 1920;
    tracker->config.frame_height = 1080;

    /* Initialize properties */
    tracker->high_confidence_threshold = 0.5f;
    tracker->low_confidence_threshold = 0.1f;
    tracker->max_time_lost = 30;
    tracker->iou_threshold = 0.3f;
    tracker->width = 1920;
    tracker->height = 1080;
    tracker->fps_n = 30;
    tracker->fps_d = 1;

    /* Initialize statistics */
    tracker->frame_count = 0;
    tracker->track_count = 0;

    /* Initialize tracker */
    tracker->tracker = new CustomTracker(tracker->config);

    GST_INFO("Custom BYTETrack tracker initialized");
}

/* Set property */
static void gst_custom_tracker_set_property(GObject* object, guint prop_id,
                                             const GValue* value, GParamSpec* pspec) {
    GstCustomTracker* tracker = GST_CUSTOM_TRACKER(object);

    switch (prop_id) {
        case PROP_HIGH_CONF_THRESHOLD:
            tracker->high_confidence_threshold = g_value_get_float(value);
            tracker->config.high_confidence_threshold = g_value_get_float(value);
            GST_DEBUG_OBJECT(tracker, "High confidence threshold: %f",
                             tracker->high_confidence_threshold);
            break;
        case PROP_LOW_CONF_THRESHOLD:
            tracker->low_confidence_threshold = g_value_get_float(value);
            tracker->config.low_confidence_threshold = g_value_get_float(value);
            GST_DEBUG_OBJECT(tracker, "Low confidence threshold: %f",
                             tracker->low_confidence_threshold);
            break;
        case PROP_MAX_TIME_LOST:
            tracker->max_time_lost = g_value_get_int(value);
            tracker->config.max_time_lost = g_value_get_int(value);
            GST_DEBUG_OBJECT(tracker, "Max time lost: %d", tracker->max_time_lost);
            break;
        case PROP_IOU_THRESHOLD:
            tracker->iou_threshold = g_value_get_float(value);
            tracker->config.iou_threshold = g_value_get_float(value);
            GST_DEBUG_OBJECT(tracker, "IoU threshold: %f", tracker->iou_threshold);
            break;
        case PROP_FRAME_WIDTH:
            tracker->width = g_value_get_int(value);
            tracker->config.frame_width = g_value_get_int(value);
            break;
        case PROP_FRAME_HEIGHT:
            tracker->height = g_value_get_int(value);
            tracker->config.frame_height = g_value_get_int(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

/* Get property */
static void gst_custom_tracker_get_property(GObject* object, guint prop_id,
                                             GValue* value, GParamSpec* pspec) {
    GstCustomTracker* tracker = GST_CUSTOM_TRACKER(object);

    switch (prop_id) {
        case PROP_HIGH_CONF_THRESHOLD:
            g_value_set_float(value, tracker->high_confidence_threshold);
            break;
        case PROP_LOW_CONF_THRESHOLD:
            g_value_set_float(value, tracker->low_confidence_threshold);
            break;
        case PROP_MAX_TIME_LOST:
            g_value_set_int(value, tracker->max_time_lost);
            break;
        case PROP_IOU_THRESHOLD:
            g_value_set_float(value, tracker->iou_threshold);
            break;
        case PROP_FRAME_WIDTH:
            g_value_set_int(value, tracker->width);
            break;
        case PROP_FRAME_HEIGHT:
            g_value_set_int(value, tracker->height);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

/* Set caps */
static gboolean gst_custom_tracker_set_caps(GstBaseTransform* trans,
                                             GstCaps* incaps, GstCaps* outcaps) {
    GstCustomTracker* tracker = GST_CUSTOM_TRACKER(trans);

    GstStructure* structure = gst_caps_get_structure(incaps, 0);
    const gchar* format = gst_structure_get_string(structure, "format");

    /* Get dimensions */
    gst_structure_get_int(structure, "width", &tracker->width);
    gst_structure_get_int(structure, "height", &tracker->height);

    /* Get FPS if available */
    gst_structure_get_fraction(structure, "framerate", &tracker->fps_n, &tracker->fps_d);

    /* Update tracker with frame dimensions */
    tracker->config.frame_width = tracker->width;
    tracker->config.frame_height = tracker->height;
    tracker->tracker->set_frame_size(tracker->width, tracker->height);

    GST_INFO_OBJECT(tracker, "Caps set: %dx%d, format: %s, fps: %d/%d",
                    tracker->width, tracker->height,
                    format ? format : "unknown",
                    tracker->fps_n, tracker->fps_d);

    return TRUE;
}

/* Extract detections from DeepStream metadata */
static std::vector<TrackingObject> extract_detections(GstCustomTracker* tracker,
                                                       NvDsBatchMeta* batch_meta) {
    std::vector<TrackingObject> detections;

    NvDsFrameMetaList* frame_meta_list = batch_meta->frame_meta_list;
    if (!frame_meta_list) {
        return detections;
    }

    for (NvDsFrameMetaList* l_frame = frame_meta_list; l_frame != NULL;
         l_frame = l_frame->next) {
        NvDsFrameMeta* frame_meta = (NvDsFrameMeta*)l_frame->data;

        NvDsObjectMetaList* obj_meta_list = frame_meta->obj_meta_list;
        if (!obj_meta_list) {
            continue;
        }

        for (NvDsObjectMetaList* l_obj = obj_meta_list; l_obj != NULL;
             l_obj = l_obj->next) {
            NvDsObjectMeta* obj_meta = (NvDsObjectMeta*)l_obj->data;

            // Skip objects that are not being tracked
            if (obj_meta->object_id == UNTRACKED_OBJECT_ID) {
                // This is a new detection, convert to TrackingObject
                TrackingObject det;

                det.class_id = obj_meta->class_id;
                det.confidence = obj_meta->confidence;

                // Get normalized bounding box
                det.bbox_x = obj_meta->rect_params.left / frame_meta->source_width;
                det.bbox_y = obj_meta->rect_params.top / frame_meta->source_height;
                det.bbox_w = obj_meta->rect_params.width / frame_meta->source_width;
                det.bbox_h = obj_meta->rect_params.height / frame_meta->source_height;

                det.object_id = -1;  // Not yet tracked

                // Store pixel coordinates
                det.x1 = obj_meta->rect_params.left;
                det.y1 = obj_meta->rect_params.top;
                det.x2 = obj_meta->rect_params.left + obj_meta->rect_params.width;
                det.y2 = obj_meta->rect_params.top + obj_meta->rect_params.height;

                detections.push_back(det);
            }
        }
    }

    return detections;
}

/* Update DeepStream metadata with tracking results */
static void update_metadata(GstCustomTracker* tracker,
                            NvDsBatchMeta* batch_meta,
                            const std::vector<TrackingObject>& tracks) {
    // Build a map of tracking results
    // Since we don't have frame index in tracks, we update all frames
    // In a more sophisticated implementation, you'd match by frame index

    // Create a mapping from detection to track
    // For simplicity, we'll update based on IoU matching

    NvDsFrameMetaList* frame_meta_list = batch_meta->frame_meta_list;
    if (!frame_meta_list) {
        return;
    }

    // For each frame, try to match tracks to objects
    for (NvDsFrameMetaList* l_frame = frame_meta_list; l_frame != NULL;
         l_frame = l_frame->next) {
        NvDsFrameMeta* frame_meta = (NvDsFrameMeta*)l_frame->data;

        NvDsObjectMetaList* obj_meta_list = frame_meta->obj_meta_list;
        if (!obj_meta_list) {
            continue;
        }

        // For each object in the frame
        for (NvDsObjectMetaList* l_obj = obj_meta_list; l_obj != NULL;
             l_obj = l_obj->next) {
            NvDsObjectMeta* obj_meta = (NvDsObjectMeta*)l_obj->data;

            // Try to match this object to a track
            for (const auto& track : tracks) {
                // Calculate IoU
                float track_x1 = track.bbox_x * frame_meta->source_width;
                float track_y1 = track.bbox_y * frame_meta->source_height;
                float track_x2 = (track.bbox_x + track.bbox_w) * frame_meta->source_width;
                float track_y2 = (track.bbox_y + track.bbox_h) * frame_meta->source_height;

                float obj_x1 = obj_meta->rect_params.left;
                float obj_y1 = obj_meta->rect_params.top;
                float obj_x2 = obj_meta->rect_params.left + obj_meta->rect_params.width;
                float obj_y2 = obj_meta->rect_params.top + obj_meta->rect_params.height;

                // Calculate intersection
                float inter_x1 = std::max(track_x1, obj_x1);
                float inter_y1 = std::max(track_y1, obj_y1);
                float inter_x2 = std::min(track_x2, obj_x2);
                float inter_y2 = std::min(track_y2, obj_y2);

                if (inter_x2 > inter_x1 && inter_y2 > inter_y1) {
                    float inter_area = (inter_x2 - inter_x1) * (inter_y2 - inter_y1);
                    float track_area = (track_x2 - track_x1) * (track_y2 - track_y1);
                    float obj_area = (obj_x2 - obj_x1) * (obj_y2 - obj_y1);
                    float union_area = track_area + obj_area - inter_area;

                    float iou = (union_area > 0) ? (inter_area / union_area) : 0.0f;

                    // If IoU is high enough, assign track ID
                    if (iou > tracker->config.iou_threshold * 0.5f) {
                        // Check if class matches (optional)
                        // if (obj_meta->class_id == track.class_id) {
                        obj_meta->object_id = track.object_id;
                        // }
                        break;
                    }
                }
            }
        }
    }
}

/* Transform IP - main processing function */
static GstFlowReturn gst_custom_tracker_transform_ip(GstBaseTransform* trans,
                                                      GstBuffer* buffer) {
    GstCustomTracker* tracker = GST_CUSTOM_TRACKER(trans);
    GstMapInfo map;

    /* Get metadata from buffer */
    NvDsBatchMeta* batch_meta = NULL;

    /* Map buffer to access metadata */
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        GST_WARNING_OBJECT(tracker, "Failed to map buffer");
        return GST_FLOW_OK;
    }

    /* Get DeepStream batch metadata */
    batch_meta = nvds_get_batch_meta(map.data, map.size);

    if (!batch_meta) {
        gst_buffer_unmap(buffer, &map);
        return GST_FLOW_OK;
    }

    /* Extract detections */
    std::vector<TrackingObject> detections = extract_detections(tracker, batch_meta);

    if (detections.empty()) {
        gst_buffer_unmap(buffer, &map);
        tracker->frame_count++;
        return GST_FLOW_OK;
    }

    /* Run tracker */
    std::vector<TrackingObject> tracks = tracker->tracker->update(detections);

    /* Update metadata with tracking results */
    update_metadata(tracker, batch_meta, tracks);

    /* Update statistics */
    tracker->frame_count++;
    if (tracker->frame_count % 100 == 0) {
        GST_INFO_OBJECT(tracker, "Frame %lu: %lu detections, %lu tracks",
                        tracker->frame_count, detections.size(), tracks.size());
    }

    gst_buffer_unmap(buffer, &map);
    return GST_FLOW_OK;
}

/* State change */
static GstStateChangeReturn gst_custom_tracker_change_state(GstElement* element,
                                                            GstStateChange transition) {
    GstCustomTracker* tracker = GST_CUSTOM_TRACKER(element);
    GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

    switch (transition) {
        case GST_STATE_CHANGE_NULL_TO_READY:
            GST_INFO_OBJECT(tracker, "State: NULL -> READY");
            break;
        case GST_STATE_CHANGE_READY_TO_PAUSED:
            GST_INFO_OBJECT(tracker, "State: READY -> PAUSED");
            break;
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
            GST_INFO_OBJECT(tracker, "State: PAUSED -> PLAYING");
            break;
        default:
            break;
    }

    ret = GST_ELEMENT_CLASS(gst_custom_tracker_parent_class)->change_state(element, transition);

    if (ret == GST_STATE_CHANGE_FAILURE) {
        GST_ERROR_OBJECT(tracker, "State change failed");
        return ret;
    }

    switch (transition) {
        case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
            GST_INFO_OBJECT(tracker, "State: PLAYING -> PAUSED");
            break;
        case GST_STATE_CHANGE_PAUSED_TO_READY:
            GST_INFO_OBJECT(tracker, "State: PAUSED -> READY");
            break;
        case GST_STATE_CHANGE_READY_TO_NULL:
            GST_INFO_OBJECT(tracker, "State: READY -> NULL");
            /* Cleanup tracker */
            if (tracker->tracker) {
                delete tracker->tracker;
                tracker->tracker = NULL;
            }
            break;
        default:
            break;
    }

    return ret;
}

/* Plugin initialization */
static gboolean plugin_init(GstPlugin* plugin) {
    return gst_element_register(plugin, GST_CUSTOM_TRACKER_NAME, GST_RANK_NONE,
                                GST_TYPE_CUSTOM_TRACKER);
}

/* Plugin definition */
GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    GST_CUSTOM_TRACKER_NAME,
    plugin_short_desc,
    plugin_init,
    "1.0",
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)
