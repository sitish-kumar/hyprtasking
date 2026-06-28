// Unit test for the adaptive grid sizing logic (grid_dims_for_count).
// Build & run:  g++ -std=c++23 -O2 test/grid_dims_test.cpp -o /tmp/gdt && /tmp/gdt
#include "../src/grid_dims.hpp"
#include <cmath>
#include <cstdio>

static int failures = 0;

static void check(bool cond, const char* what, int n, int rows, int cols) {
    if (!cond) {
        std::printf("  FAIL  n=%-2d -> %dx%d : %s\n", n, cols, rows, what);
        failures++;
    }
}

int main() {
    std::printf("%-4s %-8s %-7s %-6s\n", "n", "grid", "cells", "spare");
    std::printf("---- -------- ------- ------\n");

    for (int n = 0; n <= 20; n++) {
        int rows = -1, cols = -1;
        grid_dims_for_count(n, rows, cols);
        const int cells = rows * cols;
        std::printf("%-4d %dx%-6d %-7d %-6d\n", n, cols, rows, cells, cells - n);

        check(rows >= 1 && cols >= 1, "dims must be >= 1", n, rows, cols);
        check(cells >= n + 1, "must fit n+1 (always a spare cell)", n, rows, cols);
        check(cols - rows == 0 || cols - rows == 1, "must be near-square (cols-rows in {0,1})", n, rows, cols);
        const int expect_cols = (int)std::ceil(std::sqrt((double)std::max(n + 1, 2)));
        check(cols == expect_cols, "cols must equal ceil(sqrt(n+1)) (minimal fit)", n, rows, cols);
    }

    // Concrete expectations for the cases that matter day-to-day.
    auto expect = [&](int n, int ec, int er) {
        int r, c;
        grid_dims_for_count(n, r, c);
        check(c == ec && r == er, "specific expectation not met", n, r, c);
    };
    expect(1, 2, 1);
    expect(3, 2, 2);   // 3 workspaces -> 2x2 (3 + 1 spare)
    expect(4, 3, 2);   // 4 workspaces -> 3x2
    expect(8, 3, 3);   // 8 workspaces -> 3x3 (exactly fits 9)
    expect(9, 4, 3);   // 9 workspaces -> 4x3
    expect(15, 4, 4);  // 15 workspaces -> 4x4

    if (failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return 0;
    }
    std::printf("\n%d FAILURE(S)\n", failures);
    return 1;
}
