#pragma once
#include <algorithm>
#include <cmath>

// Pure adaptive-grid sizing (no Hyprland deps so it is unit-testable).
//
// Picks the smallest UNIFORM SQUARE grid (N x N) that has strictly MORE cells
// than there are workspaces, so the leftover cell(s) are empty "+" slots for
// creating a new workspace. N = ceil(sqrt(workspace_count + 1)).
//
//   1-3 ws  -> 2x2 (4 cells)
//   4-8 ws  -> 3x3 (9 cells)
//   9-15 ws -> 4x4 (16 cells)
//   16-24   -> 5x5 (25 cells)
//
// Because dims are recomputed on every overview open, the grid grows AND
// shrinks in lock-step with the real workspaces (close enough of them and the
// next open drops back to the smaller square).
//
// Invariants (verified in test/grid_dims_test.cpp):
//   * rows == cols                 (uniform square)
//   * rows*cols > workspace_count   (always at least one empty "+" cell)
//   * rows == ceil(sqrt(n+1))       (smallest such square)
inline void grid_dims_for_count(int workspace_count, int& rows, int& cols) {
    const int slots = std::max(workspace_count + 1, 2);
    const int n = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(slots))));
    rows = n;
    cols = n;
}
