#ifndef GST_CUSTOM_TRACKER_H
#define GST_CUSTOM_TRACKER_H

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include "customtracker.h"

/* GStreamer plugin version */
#define GST_CUSTOM_TRACKER_VERSION "1.0"

/* Forward declarations */
typedef struct _GstCustomTracker GstCustomTracker;
typedef struct _GstCustomTrackerClass GstCustomTrackerClass;

/* Get type function */
GType gst_custom_tracker_get_type(void);

/* Cast macros */
#define GST_TYPE_CUSTOM_TRACKER (gst_custom_tracker_get_type())
#define GST_CUSTOM_TRACKER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_CUSTOM_TRACKER, GstCustomTracker))
#define GST_CUSTOM_TRACKER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_CUSTOM_TRACKER, GstCustomTrackerClass))
#define GST_IS_CUSTOM_TRACKER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_CUSTOM_TRACKER))
#define GST_IS_CUSTOM_TRACKER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_CUSTOM_TRACKER))

/* GstCustomTracker structure */
struct _GstCustomTracker {
    GstBaseTransform parent;

    /* Tracker instance */
    CustomTracker* tracker;

    /* Configuration */
    TrackerConfig config;

    /* Video info */
    gint width;
    gint height;
    gint fps_n;
    gint fps_d;

    /* Statistics */
    guint64 frame_count;
    guint64 track_count;

    /* Properties */
    gfloat high_confidence_threshold;
    gfloat low_confidence_threshold;
    gint max_time_lost;
    gfloat iou_threshold;
};

/* GstCustomTrackerClass structure */
struct _GstCustomTrackerClass {
    GstBaseTransformClass parent_class;
};

#endif /* GST_CUSTOM_TRACKER_H */
