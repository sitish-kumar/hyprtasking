#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>

#include "overview.hpp"

class HTManager {
  public:
    HTManager();

    std::vector<PHTVIEW> views;

    PHTVIEW get_view_from_monitor(PHLMONITOR pMonitor);
    PHTVIEW get_view_from_cursor();
    PHTVIEW get_view_from_id(VIEWID view_id);

    PHLWINDOW get_window_from_cursor(bool return_focused = true);

    void reset();

    void show_all_views();
    void hide_all_views();
    void show_cursor_view();

    void refresh_all_grid_caches();
    void remove_view_for_monitor_id(MONITORID mid);

    bool start_window_drag();
    bool end_window_drag();
    bool exit_to_workspace();
    bool on_mouse_move();
    bool on_mouse_axis(double delta);

    enum swipe_state_t {
        HT_SWIPE_OPEN,
        HT_SWIPE_MOVE,
        HT_SWIPE_SWITCH,
        HT_SWIPE_NONE,
    };

    swipe_state_t swipe_state;
    float swipe_amt;
    // 4-finger horizontal "switch workspace" gesture: a safe replacement for the native
    // workspace swipe, which crashes Hyprland 0.55.4 (a pinch gets routed into
    // CWorkspaceSwipeGesture::update and null-derefs e.swipe). This is a plain
    // next/prev workspace switch (Hyprland animates the window slide) routed through
    // hyprtasking's swipe path (no CTrackpadGestures) — NOT the overview grid pan.
    // switch_accum sums horizontal delta; on release we commit if it passed a threshold.
    double switch_accum = 0.0;
    // Finger count of the gesture that last committed to a mode. Used as a guard rail:
    // while a 3-finger move's animation is still settling (cursor_view->navigating), a
    // brushed extra finger makes libinput cancel the 3-finger swipe and begin a new one
    // with a different finger count — we ignore that so it can't corrupt the in-flight
    // navigation (open the overview / switch workspace). Chained same-count swipes still
    // work. NOT reset in swipe_start (must survive the cancel+rebegin).
    int committed_fingers = 0;
    void swipe_start();
    bool swipe_update(IPointer::SSwipeUpdateEvent e);
    bool swipe_end();

    bool has_active_view();
    bool cursor_view_active();
};
