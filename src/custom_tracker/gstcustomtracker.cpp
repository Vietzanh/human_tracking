/**
 * GStreamer Plugin for Custom BYTETrack Tracker
 *
 * This plugin integrates the BYTETrack algorithm into DeepStream pipeline.
 * It extracts detections from NvDsBatchMeta, runs tracking, and writes
 * tracking IDs back to the metadata.
 */

#include "gstcustomtracker.h"
#include <gstnvdsmeta.h>
#include <nvdsmeta.h>

#include <iostream>
#include <cstring>

/* Define PACKAGE for GStreamer - must be before GST_PLUGIN_DEFINE */
/* Note: PACKAGE must be a valid C identifier (no hyphens) */
#define PACKAGE "gstcustomtracker"

/* Debug category */
GST_DEBUG_CATEGORY_STATIC(custom_tracker_debug);

/* Forward declarations */
static void gst_custom_tracker_class_init(GstCustomTrackerClass* klass);
static void gst_custom_tracker_init(GstCustomTracker* tracker);
static void gst_custom_tracker_finalize(GObject* object);
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

/* Define parent class */
#define gst_custom_tracker_parent_class parent_class
G_DEFINE_TYPE(GstCustomTracker, gst_custom_tracker, GST_TYPE_BASE_TRANSFORM);

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

/* Plugin metadata */
static const gchar* plugin_description =
    "Multi-object tracker using BYTETrack algorithm";

/* Initialize class */
static void gst_custom_tracker_class_init(GstCustomTrackerClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass* element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass* basetransform_class = GST_BASE_TRANSFORM_CLASS(klass);

    /* Set finalize handler */
    gobject_class->finalize = GST_DEBUG_FUNCPTR(gst_custom_tracker_finalize);

    /* Set property handlers */
    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_custom_tracker_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_custom_tracker_get_property);

    /* Set element handlers */
    element_class->change_state = GST_DEBUG_FUNCPTR(gst_custom_tracker_change_state);

    /* Set transform handlers */
    basetransform_class->set_caps = GST_DEBUG_FUNCPTR(gst_custom_tracker_set_caps);
    basetransform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_custom_tracker_transform_ip);

    /* IMPORTANT: Disable passthrough so transform_ip is always called */
    basetransform_class->passthrough_on_same_caps = FALSE;

    GST_INFO("Custom BYTETrack tracker class initialized - transform_ip=%p",
             (void*)gst_custom_tracker_transform_ip);

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

    GST_DEBUG_CATEGORY_INIT(custom_tracker_debug, "customtracker",
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

/* Finalize */
static void gst_custom_tracker_finalize(GObject* object) {
    GstCustomTracker* tracker = GST_CUSTOM_TRACKER(object);

    if (tracker->tracker) {
        delete tracker->tracker;
        tracker->tracker = NULL;
    }

    G_OBJECT_CLASS(gst_custom_tracker_parent_class)->finalize(object);
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
    gint width, height;
    gst_structure_get_int(structure, "width", &width);
    gst_structure_get_int(structure, "height", &height);
    tracker->width = width;
    tracker->height = height;

    /* Get FPS if available */
    gint fps_n = 30, fps_d = 1;
    gst_structure_get_fraction(structure, "framerate", &fps_n, &fps_d);
    tracker->fps_n = fps_n;
    tracker->fps_d = fps_d;

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

    GST_DEBUG_OBJECT(tracker, "[EXTRACT] batch_meta->frame_meta_list = %p",
                    (void*)batch_meta->frame_meta_list);

    NvDsFrameMetaList* frame_meta_list = batch_meta->frame_meta_list;
    if (!frame_meta_list) {
        GST_DEBUG_OBJECT(tracker, "[EXTRACT] frame_meta_list is NULL!");
        return detections;
    }

    guint frame_count = 0;
    for (NvDsFrameMetaList* l_frame = frame_meta_list; l_frame != NULL;
         l_frame = l_frame->next) {
        NvDsFrameMeta* frame_meta = (NvDsFrameMeta*)l_frame->data;
        frame_count++;

        // Get frame dimensions from tracker (set from caps)
        guint frame_width = tracker->width;
        guint frame_height = tracker->height;

        GST_DEBUG_OBJECT(tracker, "[EXTRACT] Frame %u: obj_meta_list = %p",
                        frame_count, (void*)frame_meta->obj_meta_list);

        NvDsObjectMetaList* obj_meta_list = frame_meta->obj_meta_list;
        if (!obj_meta_list) {
            continue;
        }

        guint obj_count = 0;
        for (NvDsObjectMetaList* l_obj = obj_meta_list; l_obj != NULL;
             l_obj = l_obj->next) {
            NvDsObjectMeta* obj_meta = (NvDsObjectMeta*)l_obj->data;
            obj_count++;

            GST_DEBUG_OBJECT(tracker, "[EXTRACT] Frame %u, Obj %u: class_id=%d, conf=%.3f, rect=(%.1f,%.1f,%.1f,%.1f)",
                            frame_count, obj_count,
                            obj_meta->class_id, obj_meta->confidence,
                            obj_meta->rect_params.left, obj_meta->rect_params.top,
                            obj_meta->rect_params.width, obj_meta->rect_params.height);

            // Skip objects that are already being tracked (have valid object_id)
            // DeepStream creates fresh metadata each frame with UNTRACKED_OBJECT_ID
            // But if for some reason object_id is set, skip it
            if (obj_meta->object_id != UNTRACKED_OBJECT_ID) {
                GST_DEBUG_OBJECT(tracker, "[EXTRACT] Frame %u, Obj %u: skipping already tracked (id=%lu)",
                                frame_count, obj_count, (gulong)obj_meta->object_id);
                continue;
            }

            // This is a new detection, convert to TrackingObject
            TrackingObject det;

            det.class_id = obj_meta->class_id;
            det.confidence = obj_meta->confidence;

            // Get normalized bounding box (using frame dimensions)
            det.bbox_x = obj_meta->rect_params.left / (float)frame_width;
            det.bbox_y = obj_meta->rect_params.top / (float)frame_height;
            det.bbox_w = obj_meta->rect_params.width / (float)frame_width;
            det.bbox_h = obj_meta->rect_params.height / (float)frame_height;

            det.object_id = -1;  // Not yet tracked

            // Store pixel coordinates
            det.x1 = obj_meta->rect_params.left;
            det.y1 = obj_meta->rect_params.top;
            det.x2 = obj_meta->rect_params.left + obj_meta->rect_params.width;
            det.y2 = obj_meta->rect_params.top + obj_meta->rect_params.height;

            // Store pointer to NvDsObjectMeta so we can update it later
            det.user_data = obj_meta;

            detections.push_back(det);
        }

        GST_DEBUG_OBJECT(tracker, "[EXTRACT] Frame %u: processed %u objects, total detections so far: %lu",
                        frame_count, obj_count, (gulong)detections.size());
    }

    GST_DEBUG_OBJECT(tracker, "[EXTRACT] Total: %lu detections from %u frames",
                    (gulong)detections.size(), frame_count);

    return detections;
}

/* Update DeepStream metadata with tracking results */
static void update_metadata(GstCustomTracker* tracker,
                            NvDsBatchMeta* batch_meta,
                            const std::vector<TrackingObject>& tracks) {
    guint frame_width = tracker->width;
    guint frame_height = tracker->height;

    GST_DEBUG_OBJECT(tracker, "[UPDATE_META] Processing %lu tracks", (gulong)tracks.size());

    // Directly update metadata using user_data pointer (pointer to NvDsObjectMeta)
    // This is much more efficient than IoU matching
    for (const auto& track : tracks) {
        GST_DEBUG_OBJECT(tracker, "[UPDATE_META] Track: object_id=%d, class_id=%d, conf=%.3f, user_data=%p",
                        track.object_id, track.class_id, track.confidence, track.user_data);

        if (track.user_data != nullptr) {
            NvDsObjectMeta* obj_meta = static_cast<NvDsObjectMeta*>(track.user_data);

            GST_DEBUG_OBJECT(tracker, "[UPDATE_META] Assigning track_id=%d to NvDsObjectMeta",
                            track.object_id);

            // Assign the track ID
            obj_meta->object_id = track.object_id;

            // Also update the bbox to match the track's smoothed position
            obj_meta->rect_params.left = track.bbox_x * frame_width;
            obj_meta->rect_params.top = track.bbox_y * frame_height;
            obj_meta->rect_params.width = track.bbox_w * frame_width;
            obj_meta->rect_params.height = track.bbox_h * frame_height;

            GST_DEBUG_OBJECT(tracker, "[UPDATE_META] Updated object_id=%lu to track_id=%d, bbox=(%.2f,%.2f,%.2f,%.2f)",
                           (gulong)obj_meta->object_id, track.object_id,
                           track.bbox_x, track.bbox_y, track.bbox_w, track.bbox_h);
        } else {
            GST_WARNING_OBJECT(tracker, "[UPDATE_META] Track has NULL user_data! Cannot update metadata.");
        }
    }

    GST_DEBUG_OBJECT(tracker, "[UPDATE_META] Done updating %lu tracks", (gulong)tracks.size());
}

/* Transform IP - main processing function */
static GstFlowReturn gst_custom_tracker_transform_ip(GstBaseTransform* trans,
                                                      GstBuffer* buffer) {
    GstCustomTracker* tracker = GST_CUSTOM_TRACKER(trans);

    /* Print immediately to stderr - bypasses GST_DEBUG */
    std::cerr << "[CUSTOM_TRACKER] transform_ip called! frame=" << tracker->frame_count << std::endl;

    GST_DEBUG_OBJECT(tracker, "[TRACKER_TRANSFORM] Frame %lu: START transform_ip",
                    (gulong)tracker->frame_count);

    /* Get metadata from buffer */
    NvDsBatchMeta* batch_meta = NULL;

    /* Get DeepStream batch metadata - use the correct API for DeepStream 7.0 */
    batch_meta = gst_buffer_get_nvds_batch_meta(buffer);

    GST_DEBUG_OBJECT(tracker, "[TRACKER_TRANSFORM] Frame %lu: batch_meta=%p",
                    (gulong)tracker->frame_count, (void*)batch_meta);

    if (!batch_meta) {
        GST_DEBUG_OBJECT(tracker, "[TRACKER_TRANSFORM] Frame %lu: NO batch_meta, returning OK",
                        (gulong)tracker->frame_count);
        return GST_FLOW_OK;
    }

    GST_DEBUG_OBJECT(tracker, "[TRACKER_TRANSFORM] Frame %lu: Extracting detections...",
                    (gulong)tracker->frame_count);

    /* Extract detections */
    std::vector<TrackingObject> detections = extract_detections(tracker, batch_meta);

    GST_DEBUG_OBJECT(tracker, "[TRACKER_TRANSFORM] Frame %lu: Extracted %lu detections",
                    (gulong)tracker->frame_count, (gulong)detections.size());

    if (detections.empty()) {
        tracker->frame_count++;
        return GST_FLOW_OK;
    }

    GST_DEBUG_OBJECT(tracker, "[TRACKER_TRANSFORM] Frame %lu: Running tracker update...",
                    (gulong)tracker->frame_count);

    /* Run tracker */
    std::vector<TrackingObject> tracks = tracker->tracker->update(detections);

    GST_DEBUG_OBJECT(tracker, "[TRACKER_TRANSFORM] Frame %lu: Produced %lu tracks",
                    (gulong)tracker->frame_count, (gulong)tracks.size());

    /* Update metadata with tracking results */
    GST_DEBUG_OBJECT(tracker, "[TRACKER_TRANSFORM] Frame %lu: Updating metadata...",
                    (gulong)tracker->frame_count);
    update_metadata(tracker, batch_meta, tracks);

    /* Update statistics */
    tracker->frame_count++;
    if (tracker->frame_count % 100 == 0) {
        GST_INFO_OBJECT(tracker, "Frame %lu: %lu detections, %lu tracks",
                        (gulong)tracker->frame_count, (gulong)detections.size(), (gulong)tracks.size());
    }

    GST_DEBUG_OBJECT(tracker, "[TRACKER_TRANSFORM] Frame %lu: DONE, returning OK",
                    (gulong)tracker->frame_count);

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

    /* Call parent class change_state */
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
    return gst_element_register(plugin, "customtracker", GST_RANK_NONE,
                                GST_TYPE_CUSTOM_TRACKER);
}

/* Plugin definition */
GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    customtracker,
    "Custom BYTETrack tracker for DeepStream",
    plugin_init,
    "1.0",
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)
