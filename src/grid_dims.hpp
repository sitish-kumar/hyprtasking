#pragma once
#include <algorithm>
#include <cmath>

// Pure adaptive-grid sizing (no Hyprland deps so it is unit-testable).
//
// Chooses a compact, near-square grid that fits `workspace_count` real
// workspaces PLUS one spare cell, so there is always an empty slot to
// create/drag a new workspace into.
//
//   n=1 -> 2x1   n=3 -> 2x2   n=4 -> 3x2   n=8 -> 3x3
//   n=9 -> 4x3   n=15 -> 4x4  n=16 -> 5x4
//
// Invariants (verified in test/grid_dims_test.cpp):
//   * rows >= 1 && cols >= 1
//   * rows*cols >= n + 1            (always at least one spare cell)
//   * cols - rows is 0 or 1         (near-square, columns-major)
//   * cols == ceil(sqrt(n+1))       (minimal near-square fit)
inline void grid_dims_for_count(int workspace_count, int& rows, int& cols) {
    const int slots = std::max(workspace_count + 1, 2);
    cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(slots))));
    rows = static_cast<int>(std::ceil(static_cast<double>(slots) / cols));
}
