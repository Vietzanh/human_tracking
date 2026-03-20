#ifndef CUSTOM_TRACKER_H
#define CUSTOM_TRACKER_H

#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <algorithm>
#include <cmath>

/**
 * BYTETrack: Multi-Object Tracking by Associating Every Detection Box
 *
 * Key differences from SORT:
 * - Uses both high and low confidence detections
 * - Two-stage association: first high-score, then low-score
 * - Better handling of occlusions and false positives
 */

/**
 * Configuration for BYTETrack algorithm
 */
struct TrackerConfig {
    // Detection confidence thresholds
    float high_confidence_threshold = 0.5f;   // Use detections above this first
    float low_confidence_threshold = 0.1f;     // Then use detections above this

    // Track lifecycle
    int max_time_lost = 90;        // 3 seconds at 30fps - keep lost track longer
    int min_hits = 30;             // Require 30 frames (~1 sec) before outputting track

    // IoU matching
    float iou_threshold = 0.2f;    // Lower IoU threshold for more matches

    // Frame info (set per frame)
    int frame_width = 1920;
    int frame_height = 1080;
};

/**
 * Represents a detected or tracked object
 */
struct TrackingObject {
    int class_id;              // Class ID from detector
    float confidence;          // Detection confidence
    float bbox_x;              // Normalized bbox [0,1]
    float bbox_y;
    float bbox_w;
    float bbox_h;
    int object_id;             // Assigned tracking ID (-1 if not tracked)

    // Raw pixel coordinates (for IoU calculation)
    float x1, y1, x2, y2;     // Absolute pixel coordinates

    // Pointer back to original NvDsObjectMeta (for updating metadata)
    void* user_data;           // Can store NvDsObjectMeta* or index

    TrackingObject() : class_id(0), confidence(0.0f), bbox_x(0), bbox_y(0),
                       bbox_w(0), bbox_h(0), object_id(-1),
                       x1(0), y1(0), x2(0), y2(0), user_data(nullptr) {}
};

/**
 * Internal track state for BYTETrack
 */
enum class TrackState {
    Tracked,   // Active tracked object
    Lost,      // Lost but may be recovered
    Removed    // Permanently removed
};

/**
 * Single tracklet in BYTETrack
 */
class BYTETrack {
public:
    int track_id;              // Unique ID for this track
    int class_id;             // Class ID from detector (person=0, head=1)
    float score;              // Detection score
    TrackState state;         // Current state
    int frames_since_update;  // Frames since last update
    int hits;                 // Total frames tracked
    int age;                  // Total frames since creation

    // Bounding box (normalized coordinates)
    float x, y, w, h;         // Normalized coordinates

    // Frame dimensions (needed for pixel coordinate conversion)
    int frame_width;
    int frame_height;

    // Pointer to original NvDsObjectMeta (for updating metadata)
    void* user_data;

    BYTETrack(int id, const TrackingObject& det);

    // Predict next position (simple linear prediction)
    void predict();

    // Update with new detection
    void update(const TrackingObject& det);

    // Convert to TrackingObject for output
    TrackingObject to_tracking_object() const;

    // Set frame dimensions
    void set_frame_size(int width, int height) {
        frame_width = width;
        frame_height = height;
    }

private:
    // Compute IOU with detection
    float compute_iou(const TrackingObject& det) const;
};

/**
 * BYTETrack main tracker class
 */
class CustomTracker {
public:
    CustomTracker(TrackerConfig config);
    ~CustomTracker();

    /**
     * Main update function
     * @param detections: List of detections from current frame
     * @return: List of tracked objects with assigned IDs
     */
    std::vector<TrackingObject> update(const std::vector<TrackingObject>& detections);

    /**
     * Reset tracker state
     */
    void reset();

    /**
     * Set frame dimensions
     */
    void set_frame_size(int width, int height);

    // IoU computation between two detections
    static float compute_iou(const TrackingObject& a, const TrackingObject& b);

private:
    // Configuration
    TrackerConfig m_config;

    // Track storage
    std::vector<std::shared_ptr<BYTETrack>> m_tracked_stracks;
    std::vector<std::shared_ptr<BYTETrack>> m_lost_stracks;
    std::vector<std::shared_ptr<BYTETrack>> m_removed_stracks;

    // Track ID counter
    int m_next_id;

    // Linear assignment (greedy IoU matching)
    std::vector<std::pair<int, int>> linear_assignment(
        const std::vector<std::shared_ptr<BYTETrack>>& tracks,
        const std::vector<TrackingObject>& detections,
        float iou_threshold
    );

    // Remove dead tracks
    void remove_duplicate_tracks();

    // Remove lost tracks that are too old
    void remove_lost_tracks();
};

#endif // CUSTOM_TRACKER_H
