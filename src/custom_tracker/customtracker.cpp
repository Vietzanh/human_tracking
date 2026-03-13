/**
 * BYTETrack: Multi-Object Tracking by Associating Every Detection Box
 *
 * Paper: https://arxiv.org/abs/2110.06864
 * Original implementation: https://github.com/ifzhang/ByteTrack
 *
 * Key features:
 * - Uses BOTH high and low confidence detections for tracking
 * - Two-stage association: first high-score, then low-score
 * - Reduces ID switches significantly compared to SORT
 */

#include "customtracker.h"
#include <iostream>
#include <limits>

// ============================================================================
// BYTETrack - Single Tracklet Implementation
// ============================================================================

BYTETrack::BYTETrack(int id, const TrackingObject& det)
    : track_id(id), score(det.confidence), state(TrackState::Tracked),
      frames_since_update(0), hits(1), age(1),
      x(det.bbox_x), y(det.bbox_y), w(det.bbox_w), h(det.bbox_h),
      vx(0), vy(0) {}

void BYTETrack::predict() {
    // Simple linear velocity prediction
    // In a full implementation, you'd use Kalman filter
    x += vx;
    y += vy;
    age++;
}

void BYTETrack::update(const TrackingObject& det) {
    // Update bbox with detection
    // Simple EMA: new_pos = alpha * det + (1-alpha) * old_pos
    float alpha = 0.8f;     // confidence parameter for detection, helps smoothing bounding box and reduce noises from detection
    x = alpha * det.bbox_x + (1 - alpha) * x;
    y = alpha * det.bbox_y + (1 - alpha) * y;
    w = alpha * det.bbox_w + (1 - alpha) * w;
    h = alpha * det.bbox_h + (1 - alpha) * h;

    // Update velocity (for prediction)
    vx = det.bbox_x - x;
    vy = det.bbox_y - y;

    score = det.confidence;
    frames_since_update = 0;
    hits++;
}

float BYTETrack::compute_iou(const TrackingObject& det) const {
    // Convert track position to TrackingObject for IoU calculation
    TrackingObject track_obj;
    track_obj.bbox_x = x;
    track_obj.bbox_y = y;
    track_obj.bbox_w = w;
    track_obj.bbox_h = h;
    return CustomTracker::compute_iou(det, track_obj);
}

TrackingObject BYTETrack::to_tracking_object() const {
    TrackingObject obj;
    obj.class_id = 0;  // Will be set from detection
    obj.confidence = score;
    obj.bbox_x = x;
    obj.bbox_y = y;
    obj.bbox_w = w;
    obj.bbox_h = h;
    obj.object_id = track_id;

    // Convert to pixel coordinates
    obj.x1 = x * m_config.frame_width;
    obj.y1 = y * m_config.frame_height;
    obj.x2 = (x + w) * m_config.frame_width;
    obj.y2 = (y + h) * m_config.frame_height;

    return obj;
}

// ============================================================================
// CustomTracker - Main BYTETrack Implementation
// ============================================================================

CustomTracker::CustomTracker(TrackerConfig config)
    : m_config(config), m_next_id(0) {
    // Reserve space for efficiency
    m_tracked_stracks.reserve(100);
    m_lost_stracks.reserve(100);
    m_removed_stracks.reserve(100);
}

CustomTracker::~CustomTracker() {
    reset();
}

void CustomTracker::reset() {
    m_tracked_stracks.clear();
    m_lost_stracks.clear();
    m_removed_stracks.clear();
    m_next_id = 0;
}

void CustomTracker::set_frame_size(int width, int height) {
    m_config.frame_width = width;
    m_config.frame_height = height;
}

// ============================================================================
// IoU Computation
// ============================================================================

float CustomTracker::compute_iou(const TrackingObject& a, const TrackingObject& b) {
    // Get absolute coordinates
    float a_x1 = a.bbox_x, a_y1 = a.bbox_y;
    float a_x2 = a_x1 + a.bbox_w, a_y2 = a_y1 + a.bbox_h;
    float b_x1 = b.bbox_x, b_y1 = b.bbox_y;
    float b_x2 = b_x1 + b.bbox_w, b_y2 = b_y1 + b.bbox_h;

    // Calculate intersection
    float inter_x1 = std::max(a_x1, b_x1);
    float inter_y1 = std::max(a_y1, b_y1);
    float inter_x2 = std::min(a_x2, b_x2);
    float inter_y2 = std::min(a_y2, b_y2);

    // No intersection
    if (inter_x2 <= inter_x1 || inter_y2 <= inter_y1) {
        return 0.0f;
    }

    float inter_area = (inter_x2 - inter_x1) * (inter_y2 - inter_y1);

    // Calculate union
    float a_area = a.bbox_w * a.bbox_h;
    float b_area = b.bbox_w * b.bbox_h;
    float union_area = a_area + b_area - inter_area;

    if (union_area <= 0) {
        return 0.0f;
    }

    return inter_area / union_area;
}

float CustomTracker::compute_iou(const TrackingObject& det, const BYTETrack& track) {
    // Convert track to TrackingObject format
    TrackingObject track_obj;
    track_obj.bbox_x = track.x;
    track_obj.bbox_y = track.y;
    track_obj.bbox_w = track.w;
    track_obj.bbox_h = track.h;
    return compute_iou(det, track_obj);
}

// ============================================================================
// Linear Assignment (Greedy for simplicity)
// ============================================================================

std::vector<std::pair<int, int>> CustomTracker::linear_assignment(
    const std::vector<std::shared_ptr<BYTETrack>>& tracks,
    const std::vector<TrackingObject>& detections,
    float iou_threshold
) {
    std::vector<std::pair<int, int>> matches;
    if (tracks.empty() || detections.empty()) {
        return matches;
    }

    // Build IoU matrix
    int n = tracks.size();
    int m = detections.size();
    std::vector<std::vector<float>> iou_matrix(n, std::vector<float>(m, 0.0f));

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < m; j++) {
            iou_matrix[i][j] = compute_iou(detections[j], *tracks[i]);
        }
    }

    // Greedy matching: match highest IoU first
    std::vector<int> matched_tracks(n, 0);
    std::vector<int> matched_dets(m, 0);

    // Sort by IoU descending
    std::vector<std::tuple<float, int, int>> sorted;
    sorted.reserve(n * m);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < m; j++) {
            if (iou_matrix[i][j] > iou_threshold) {
                sorted.emplace_back(iou_matrix[i][j], i, j);
            }
        }
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return std::get<0>(a) > std::get<0>(b); });

    // Greedy match
    for (const auto& [iou, i, j] : sorted) {
        if (!matched_tracks[i] && !matched_dets[j]) {
            matches.emplace_back(i, j);
            matched_tracks[i] = 1;
            matched_dets[j] = 1;
        }
    }

    return matches;
}

// ============================================================================
// Main Update Function - BYTETrack Algorithm
// ============================================================================

std::vector<TrackingObject> CustomTracker::update(const std::vector<TrackingObject>& detections) {
    std::vector<TrackingObject> tracked_objects;

    // Separate detections by confidence
    std::vector<TrackingObject> high_conf_dets;
    std::vector<TrackingObject> low_conf_dets;

    for (const auto& det : detections) {
        if (det.confidence >= m_config.high_confidence_threshold) {
            high_conf_dets.push_back(det);
        } else if (det.confidence >= m_config.low_confidence_threshold) {
            low_conf_dets.push_back(det);
        }
        // Detections below low_threshold are ignored (likely false positives)
    }

    // =========================================================================
    // STEP 1: First association - match high confidence detections
    // =========================================================================
    std::vector<std::shared_ptr<BYTETrack>> tracked_tracks;
    std::vector<std::shared_ptr<BYTETrack>> lost_tracks;

    for (auto& track : m_tracked_stracks) {
        track->predict();
        track->age++;
    }

    // Match high confidence detections with tracked tracks
    auto matches_high = linear_assignment(m_tracked_stracks, high_conf_dets, m_config.iou_threshold);

    // Update matched tracks
    for (const auto& [track_idx, det_idx] : matches_high) {
        m_tracked_stracks[track_idx]->update(high_conf_dets[det_idx]);
        tracked_tracks.push_back(m_tracked_stracks[track_idx]);
    }

    // Find unmatched tracks and unmatched high conf detections
    std::vector<int> unmatched_track_indices;
    std::vector<int> unmatched_high_det_indices;

    // Get unmatched tracks
    for (int i = 0; i < (int)m_tracked_stracks.size(); i++) {
        bool matched = false;
        for (const auto& [t_idx, d_idx] : matches_high) {
            if (t_idx == i) { matched = true; break; }
        }
        if (!matched) {
            unmatched_track_indices.push_back(i);
        }
    }

    // Get unmatched high conf detections
    std::vector<int> matched_high_dets(matches_high.size());
    for (const auto& [t_idx, d_idx] : matches_high) {
        matched_high_dets[d_idx] = 1;
    }
    for (int j = 0; j < (int)high_conf_dets.size(); j++) {
        if (!matched_high_dets[j]) {
            unmatched_high_det_indices.push_back(j);
        }
    }

    // =========================================================================
    // STEP 2: Second association - match low confidence detections
    // =========================================================================
    // Try to recover lost tracks using low confidence detections

    // Combine tracked (unmatched) + lost tracks
    std::vector<std::shared_ptr<BYTETrack>> track_candidates;
    std::vector<int> track_sources; // 0 = tracked, 1 = lost

    for (int idx : unmatched_track_indices) {
        if (m_tracked_stracks[idx]->state == TrackState::Tracked) {
            track_candidates.push_back(m_tracked_stracks[idx]);
            track_sources.push_back(0);
        }
    }

    for (auto& track : m_lost_stracks) {
        track_candidates.push_back(track);
        track_sources.push_back(1);
    }

    // Match low confidence detections with unmatched/lost tracks
    auto matches_low = linear_assignment(track_candidates, low_conf_dets, m_config.iou_threshold);

    // Update matched tracks
    for (const auto& [track_idx, det_idx] : matches_low) {
        track_candidates[track_idx]->update(low_conf_dets[det_idx]);

        // If was lost, now recovered
        if (track_sources[track_idx] == 1) {
            track_candidates[track_idx]->state = TrackState::Tracked;
            tracked_tracks.push_back(track_candidates[track_idx]);
        }
    }

    // =========================================================================
    // STEP 3: Create new tracks for unmatched high confidence detections
    // =========================================================================
    std::vector<int> matched_low_dets(matches_low.size());
    for (const auto& [t_idx, d_idx] : matches_low) {
        matched_low_dets[d_idx] = 1;
    }

    for (int j : unmatched_high_det_indices) {
        bool matched_in_low = false;
        for (int k = 0; k < (int)low_conf_dets.size(); k++) {
            if (matched_low_dets[k]) {
                // Check if this detection was matched
                for (const auto& [t_idx, d_idx] : matches_low) {
                    if (d_idx == k && j == -1) {  // j was not matched
                        matched_in_low = true;
                        break;
                    }
                }
            }
        }

        // Create new track for unmatched high confidence detection
        if (j >= 0 && j < (int)high_conf_dets.size()) {
            auto new_track = std::make_shared<BYTETrack>(m_next_id++, high_conf_dets[j]);
            new_track->hits = 1;
            new_track->state = TrackState::Tracked;
            tracked_tracks.push_back(new_track);
            m_tracked_stracks.push_back(new_track);
        }
    }

    // =========================================================================
    // STEP 4: Handle tracks that didn't match
    // =========================================================================
    for (int idx : unmatched_track_indices) {
        auto& track = m_tracked_stracks[idx];
        track->frames_since_update++;

        if (track->frames_since_update > m_config.max_time_lost) {
            track->state = TrackState::Lost;
            m_lost_stracks.push_back(track);
        } else {
            tracked_tracks.push_back(track);
        }
    }

    // =========================================================================
    // STEP 5: Clean up
    // =========================================================================

    // Remove very old lost tracks
    for (auto it = m_lost_stracks.begin(); it != m_lost_stracks.end(); ) {
        (*it)->age++;
        if ((*it)->age > m_config.max_time_lost * 2) {
            (*it)->state = TrackState::Removed;
            m_removed_stracks.push_back(*it);
            it = m_lost_stracks.erase(it);
        } else {
            ++it;
        }
    }

    // Update tracked_stracks to only contain active tracked tracks
    m_tracked_stracks.clear();
    for (auto& track : tracked_tracks) {
        if (track->state == TrackState::Tracked) {
            m_tracked_stracks.push_back(track);
        }
    }

    // =========================================================================
    // STEP 6: Convert to output format
    // =========================================================================

    for (auto& track : m_tracked_stracks) {
        // Only output tracks that have been confirmed (hit >= min_hits)
        if (track->hits >= m_config.min_hits) {
            TrackingObject obj = track->to_tracking_object();
            tracked_objects.push_back(obj);
        }
    }

    return tracked_objects;
}

void CustomTracker::remove_duplicate_tracks() {
    // This is handled implicitly in the main update loop
    // In a more sophisticated implementation, you'd check for overlapping tracks
    // and remove duplicates
}

void CustomTracker::remove_lost_tracks() {
    for (auto it = m_lost_stracks.begin(); it != m_lost_stracks.end(); ) {
        if ((*it)->age > m_config.max_time_lost) {
            (*it)->state = TrackState::Removed;
            m_removed_stracks.push_back(*it);
            it = m_lost_stracks.erase(it);
        } else {
            ++it;
        }
    }
}
