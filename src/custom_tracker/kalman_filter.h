#ifndef KALMAN_FILTER_H
#define KALMAN_FILTER_H

#include <eigen3/Eigen/Dense>

/**
 * Kalman Filter for 2D bounding box tracking
 *
 * State vector: [x, y, w, h, vx, vy, vw, vh]
 * - x, y: center position
 * - w, h: width and height
 * - vx, vy: velocity in x and y
 * - vw, vh: width and height velocity
 *
 * Measurement vector: [x, y, w, h]
 */

class KalmanFilter {
public:
    // State dimension
    static constexpr int state_dim = 8;
    // Measurement dimension
    static constexpr int meas_dim = 4;
    // Control dimension (no control input)
    static constexpr int ctrl_dim = 0;

    KalmanFilter() {
        init();
    }

    void init() {
        // Transition matrix F (state transition)
        F.setIdentity();
        // Position updates with velocity
        F(0, 4) = 1;  // x += vx
        F(1, 5) = 1;  // y += vy
        F(2, 6) = 1;  // w += vw
        F(3, 7) = 1;  // h += vh

        // Measurement matrix H (we only observe position and size)
        H.setZero();
        H(0, 0) = 1;  // observe x
        H(1, 1) = 1;  // observe y
        H(2, 2) = 1;  // observe w
        H(3, 3) = 1;  // observe h

        // Measurement noise covariance
        R.setIdentity();
        R *= 0.1;  // Measurement noise

        // Process noise covariance
        Q.setIdentity();
        // Position has small process noise
        Q(0, 0) = 1.0;
        Q(1, 1) = 1.0;
        Q(2, 2) = 1.0;
        Q(3, 3) = 1.0;
        // Velocity has larger process noise (more uncertainty in motion changes)
        Q(4, 4) = 10.0;
        Q(5, 5) = 10.0;
        Q(6, 6) = 10.0;
        Q(7, 7) = 10.0;

        // Initial state covariance
        P.setIdentity();
        P *= 10.0;

        // Initial state
        x.setZero();
    }

    /**
     * Predict the next state
     */
    void predict() {
        // x = F * x
        x = F * x;
        // P = F * P * F^T + Q
        P = F * P * F.transpose() + Q;
    }

    /**
     * Update with measurement [x, y, w, h]
     */
    void update(const Eigen::Vector4f& measurement) {
        // Innovation: y = z - H * x
        Eigen::Vector4f y = measurement - H * x;

        // Innovation covariance: S = H * P * H^T + R
        Eigen::Matrix4f S = H * P * H.transpose() + R;

        // Kalman gain: K = P * H^T * S^-1
        Eigen::Matrix<float, state_dim, 4> K = P * H.transpose() * S.inverse();

        // Update state: x = x + K * y
        x = x + K * y;

        // Update covariance: P = (I - K * H) * P
        Eigen::Matrix<float, state_dim, state_dim> I;
        I.setIdentity();
        P = (I - K * H) * P;
    }

    /**
     * Get predicted state [x, y, w, h]
     */
    Eigen::Vector4f get_predicted_measurement() const {
        return H * x;
    }

    /**
     * Get current state vector
     */
    Eigen::Matrix<float, state_dim, 1> get_state() const {
        return x;
    }

    /**
     * Set state from measurement [x, y, w, h]
     * Used when creating a new track
     */
    void set_measurement(const Eigen::Vector4f& measurement) {
        x.setZero();
        x(0) = measurement(0);  // x
        x(1) = measurement(1);  // y
        x(2) = measurement(2);  // w
        x(3) = measurement(3);  // h
        // Velocities initialized to 0
    }

    /**
     * Get Mahalanobis distance to a measurement
     */
    float mahalanobis_distance(const Eigen::Vector4f& measurement) const {
        Eigen::Vector4f predicted = H * x;
        Eigen::Vector4f innovation = measurement - predicted;
        Eigen::Matrix4f S = H * P * H.transpose() + R;
        return std::sqrt(innovation.transpose() * S.inverse() * innovation);
    }

private:
    Eigen::Matrix<float, state_dim, state_dim> F;  // Transition matrix
    Eigen::Matrix<float, state_dim, state_dim> Q;  // Process noise covariance
    Eigen::Matrix<float, state_dim, state_dim> P;  // State covariance
    Eigen::Matrix<float, 4, state_dim> H;           // Measurement matrix
    Eigen::Matrix4f R;                               // Measurement noise covariance
    Eigen::Matrix<float, state_dim, 1> x;           // State vector
};

#endif // KALMAN_FILTER_H
