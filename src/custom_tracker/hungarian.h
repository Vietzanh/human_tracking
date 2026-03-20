#ifndef HUNGARIAN_H
#define HUNGARIAN_H

#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>

/**
 * Hungarian Algorithm (Kuhn-Munkres) for optimal assignment
 *
 * This implementation solves the assignment problem:
 * Given a cost matrix, find the assignment that minimizes total cost.
 *
 * Time complexity: O(n^3)
 */

class HungarianAlgorithm {
public:
    /**
     * Solve assignment problem minimizing total cost
     * @param cost_matrix: n x m cost matrix (n tracks, m detections)
     * @return: vector of pairs (track_idx, detection_idx) representing optimal assignment
     */
    static std::vector<std::pair<int, int>> solve(
        const std::vector<std::vector<float>>& cost_matrix
    ) {
        int n = cost_matrix.size();
        if (n == 0) return {};

        int m = cost_matrix[0].size();
        if (m == 0) return {};

        // Handle non-square matrices by padding
        int size = std::max(n, m);

        // Create square cost matrix with infinity for padded entries
        std::vector<std::vector<float>> square_cost(size, std::vector<float>(size, 1e9f));

        for (int i = 0; i < n; i++) {
            for (int j = 0; j < m; j++) {
                square_cost[i][j] = cost_matrix[i][j];
            }
        }

        // Initialize potentials
        std::vector<float> u(size + 1, 0);
        std::vector<float> v(size + 1, 0);
        std::vector<int> p(size + 1, 0);
        std::vector<int> way(size + 1, 0);

        // Hungarian algorithm
        for (int i = 1; i <= size; i++) {
            p[0] = i;
            int j0 = 0;
            std::vector<float> minv(size + 1, 1e9f);
            std::vector<char> used(size + 1, false);

            do {
                used[j0] = true;
                int i0 = p[j0];
                float delta = 1e9f;
                int j1 = 0;

                for (int j = 1; j <= size; j++) {
                    if (!used[j]) {
                        float cur = square_cost[i0 - 1][j - 1] - u[i0] - v[j];
                        if (cur < minv[j]) {
                            minv[j] = cur;
                            way[j] = j0;
                        }
                        if (minv[j] < delta) {
                            delta = minv[j];
                            j1 = j;
                        }
                    }
                }

                for (int j = 0; j <= size; j++) {
                    if (used[j]) {
                        u[p[j]] += delta;
                        v[j] -= delta;
                    } else {
                        minv[j] -= delta;
                    }
                }

                j0 = j1;
            } while (p[j0] != 0);

            // Augmenting path
            do {
                int j1 = way[j0];
                p[j0] = p[j1];
                j0 = j1;
            } while (j0);
        }

        // Extract assignment
        std::vector<std::pair<int, int>> assignment;
        for (int j = 1; j <= size; j++) {
            if (p[j] != 0 && p[j] <= n && j <= m) {
                // Check if this is a valid assignment (not from padded entries)
                if (square_cost[p[j] - 1][j - 1] < 1e8f) {
                    assignment.emplace_back(p[j] - 1, j - 1);
                }
            }
        }

        return assignment;
    }

    /**
     * Solve for maximum cost assignment (for profit/cost where higher is better)
     * @param profit_matrix: n x m profit matrix
     * @param max_cost: maximum cost to use for conversion
     * @return: vector of pairs (track_idx, detection_idx) representing optimal assignment
     */
    static std::vector<std::pair<int, int>> solve_maximize(
        const std::vector<std::vector<float>>& profit_matrix,
        float max_cost = 1e9f
    ) {
        // Convert to cost matrix by taking (max - profit)
        int n = profit_matrix.size();
        if (n == 0) return {};
        int m = profit_matrix[0].size();
        if (m == 0) return {};

        // Find max value for normalization
        float max_val = 0;
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < m; j++) {
                max_val = std::max(max_val, profit_matrix[i][j]);
            }
        }

        // Convert to cost matrix
        std::vector<std::vector<float>> cost_matrix(n, std::vector<float>(m));
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < m; j++) {
                // Higher profit = lower cost
                cost_matrix[i][j] = max_val - profit_matrix[i][j];
            }
        }

        return solve(cost_matrix);
    }
};

#endif // HUNGARIAN_H
