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
        check(rows == cols, "must be a uniform square (rows == cols)", n, rows, cols);
        check(cells > n, "must have a free '+' cell (cells > n)", n, rows, cols);
        const int expect_n = (int)std::ceil(std::sqrt((double)std::max(n + 1, 2)));
        check(rows == expect_n, "side must equal ceil(sqrt(n+1)) (smallest square)", n, rows, cols);
    }

    // Concrete expectations: uniform squares stepping 2x2 -> 3x3 -> 4x4 -> 5x5.
    auto expect = [&](int n, int side) {
        int r, c;
        grid_dims_for_count(n, r, c);
        check(c == side && r == side, "specific expectation not met", n, r, c);
    };
    expect(1, 2);
    expect(3, 2);   // 3 ws -> 2x2 (3 + 1 "+" cell)
    expect(4, 3);   // 4 ws -> 3x3
    expect(8, 3);   // 8 ws -> 3x3
    expect(9, 4);   // 9 ws -> 4x4
    expect(15, 4);  // 15 ws -> 4x4
    expect(16, 5);  // 16 ws -> 5x5

    if (failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return 0;
    }
    std::printf("\n%d FAILURE(S)\n", failures);
    return 1;
}
