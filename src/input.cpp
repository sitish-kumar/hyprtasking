#include <linux/input-event-codes.h>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/PointerManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/managers/input/UnifiedWorkspaceSwipeGesture.hpp>

#include "config.hpp"
#include "manager.hpp"
#include "overview.hpp"

bool HTManager::start_window_drag() {
    const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
    const PHTVIEW cursor_view = get_view_from_monitor(cursor_monitor);
    if (cursor_monitor == nullptr || cursor_view == nullptr || !cursor_view->active
        || cursor_view->closing)
        return false;

    if (!cursor_view->layout->should_manage_mouse()) {
        // hide all views if should not manage mouse but active
        hide_all_views();
        return true;
    }

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    const WORKSPACEID workspace_id = cursor_view->layout->get_ws_id_from_global(mouse_coords);
    PHLWORKSPACE cursor_workspace = g_pCompositor->getWorkspaceByID(workspace_id);

    // If left click on non-workspace workspace, do nothing
    if (cursor_workspace == nullptr)
        return false;

    // PHLWORKSPACEREF o_workspace = cursor_monitor->m_activeWorkspace;
    cursor_monitor->changeWorkspace(cursor_workspace, true);

    const Vector2D workspace_coords =
        cursor_view->layout->global_to_local_ws_unscaled(mouse_coords, workspace_id)
        + cursor_monitor->m_position;

    g_pPointerManager->warpTo(workspace_coords);
    g_pKeybindManager->changeMouseBindMode(MBIND_MOVE);
    g_pPointerManager->warpTo(mouse_coords);

    const SP<Layout::ITarget> target = g_layoutManager->dragController()->target();
    if (target == nullptr)
        return false;

    const PHLWINDOW dragged_window = target->window();
    if (dragged_window != nullptr) {
        if (g_layoutManager->dragController()->draggingTiled()) {
            const Vector2D pre_pos = cursor_view->layout->local_ws_unscaled_to_global(
                dragged_window->m_realPosition->value() - dragged_window->m_monitor->m_position,
                workspace_id
            );
            const Vector2D post_pos = cursor_view->layout->local_ws_unscaled_to_global(
                dragged_window->m_realPosition->goal() - dragged_window->m_monitor->m_position,
                workspace_id
            );
            const Vector2D mapped_pre_pos =
                (pre_pos - mouse_coords) / cursor_view->layout->drag_window_scale() + mouse_coords;
            const Vector2D mapped_post_pos =
                (post_pos - mouse_coords) / cursor_view->layout->drag_window_scale() + mouse_coords;

            dragged_window->m_realPosition->setValueAndWarp(mapped_pre_pos);
            *dragged_window->m_realPosition = mapped_post_pos;
        } else {
            g_pInputManager->simulateMouseMovement();
        }
    }

    // if (o_workspace != nullptr)
    //     cursor_monitor->changeWorkspace(o_workspace.lock(), true);

    return true;
}

bool HTManager::end_window_drag() {
    const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
    const PHTVIEW cursor_view = get_view_from_monitor(cursor_monitor);
    if (cursor_monitor == nullptr || cursor_view == nullptr) {
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
        return false;
    }

    // Required if dragging and dropping from active to inactive
    if (!cursor_view->active || cursor_view->closing) {
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
        return false;
    }

    // For linear layout: if dropping on big workspace, just pass on
    if (!cursor_view->layout->should_manage_mouse()) {
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
        return false;
    }

    const SP<Layout::ITarget> target = g_layoutManager->dragController()->target();
    if (target == nullptr)
        return false;

    // If not dragging window or drag is not move, then we just let go (supposed to prevent it
    // from messing up resize on border, but it should be good because above?)
    const PHLWINDOW dragged_window = target->window();
    if (dragged_window == nullptr || g_layoutManager->dragController()->mode() != MBIND_MOVE) {
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
        return false;
    }

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    Vector2D use_mouse_coords = mouse_coords;
    const WORKSPACEID workspace_id = cursor_view->layout->get_ws_id_from_global(mouse_coords);
    PHLWORKSPACE cursor_workspace = g_pCompositor->getWorkspaceByID(workspace_id);

    // Release on empty dummy workspace, so create and switch to it
    if (cursor_workspace == nullptr && workspace_id != WORKSPACE_INVALID) {
        cursor_workspace = g_pCompositor->createNewWorkspace(workspace_id, cursor_monitor->m_id);
    } else if (workspace_id == WORKSPACE_INVALID) {
        cursor_workspace = dragged_window->m_workspace;
        // Ensure that the mouse coords are snapped to inside the workspace box itself
        use_mouse_coords = cursor_view->layout->get_global_ws_box(cursor_workspace->m_id)
                               .closestPoint(use_mouse_coords);

        Log::logger->log(
            LOG,
            "[Hyprtasking] Dragging to invalid position, snapping to last ws {}",
            cursor_workspace->m_id
        );
    }

    if (cursor_workspace == nullptr) {
        Log::logger->log(LOG, "[Hyprtasking] tried to drop on null workspace??");
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
        return false;
    }

    Log::logger->log(LOG, "[Hyprtasking] trying to drop window on ws {}", cursor_workspace->m_id);

    // PHLWORKSPACEREF o_workspace = cursor_monitor->m_activeWorkspace;
    cursor_monitor->changeWorkspace(cursor_workspace, true);

    g_pCompositor->moveWindowToWorkspaceSafe(dragged_window, cursor_workspace);

    // Inverts the scale-around-mouse remap that start_window_drag applies for
    // tiled drags; without it, the post-close m_realPosition reads as
    // workspace-local (0, 0) and the window snaps to the cell's corner.
    if (g_layoutManager->dragController()->draggingTiled()) {
        const Vector2D drop_pos = cursor_view->layout->global_to_local_ws_unscaled(
            (dragged_window->m_realPosition->value() - use_mouse_coords)
                    * cursor_view->layout->drag_window_scale()
                + use_mouse_coords,
            cursor_workspace->m_id
        ) + cursor_monitor->m_position;
        dragged_window->m_realPosition->setValueAndWarp(drop_pos);
    }

    const Vector2D workspace_coords =
        cursor_view->layout->global_to_local_ws_unscaled(use_mouse_coords, cursor_workspace->m_id)
        + cursor_monitor->m_position;

    g_pPointerManager->warpTo(workspace_coords);
    g_pInputManager->simulateMouseMovement();
    g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
    g_pPointerManager->warpTo(mouse_coords);

    // otherwise the window leaves blur (?) artifacts on all
    // workspaces
    dragged_window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_TO_WORKSPACE)->setValueAndWarp(1.0);
    dragged_window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_FROM_WORKSPACE)->setValueAndWarp(1.0);

    // if (o_workspace != nullptr)
    //     cursor_monitor->changeWorkspace(o_workspace.lock(), true);

    // Do not return true and cancel the event! Mouse release requires some stuff to be done for
    // floating windows to be unfocused properly
    return false;
}

bool HTManager::exit_to_workspace() {
    const PHTVIEW cursor_view = get_view_from_cursor();
    if (cursor_view == nullptr)
        return false;

    if (!cursor_view->active || !cursor_view->layout->should_manage_mouse())
        return false;

    for (PHTVIEW view : views) {
        if (view == nullptr)
            continue;
        view->hide(true);
    }
    return true;
}

bool HTManager::on_mouse_move() {
    // Focus-follows-cursor (#3) is applied ON CLOSE, not live: focusing the hovered
    // window every mouse-move switches the active workspace (Hyprland can't focus a
    // window on a non-active workspace without activating it), which makes that tile
    // re-render on top -> the "overlay/preview appearing and disappearing" flicker.
    // do_exit_behavior() now focuses the exact hovered window when the overview closes,
    // so the close still lands on whatever you're pointing at, with zero live churn.
    return false;  // never cancel the motion event
}

bool HTManager::on_mouse_axis(double delta) {
    const PHTVIEW cursor_view = get_view_from_cursor();
    if (cursor_view == nullptr)
        return false;

    return cursor_view->layout->on_mouse_axis(delta);
}

void HTManager::swipe_start() {
    swipe_state = HT_SWIPE_NONE;
    swipe_amt = 0.0;
    switch_accum = 0.0;
}

bool HTManager::swipe_update(IPointer::SSwipeUpdateEvent e) {
    const int ENABLED = HTConfig::value<Config::INTEGER>("gestures:enabled");
    if (!ENABLED)
        return false;

    // NOTE: cursor_view is created lazily (first overview open). The 4-finger horizontal
    // workspace switch below must work without it, so we do NOT bail on a null view here;
    // the overview-only gestures get a null guard further down instead.
    const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
    const PHTVIEW cursor_view = get_view_from_monitor(cursor_monitor);

    const unsigned int MOVE_FINGERS = HTConfig::value<Config::INTEGER>("gestures:move_fingers");
    const float OPEN_DISTANCE = HTConfig::value<Config::FLOAT>("gestures:open_distance");
    const unsigned int OPEN_FINGERS = HTConfig::value<Config::INTEGER>("gestures:open_fingers");
    const int OPEN_POSITIVE = HTConfig::value<Config::INTEGER>("gestures:open_positive");

    bool res = false;
    char swipe_direction = 0;
    if (std::abs(e.delta.x) > std::abs(e.delta.y)) {
        swipe_direction = 'h';
    } else if (std::abs(e.delta.y) > std::abs(e.delta.x)) {
        swipe_direction = 'v';
    }

    // Continue an already-committed gesture, regardless of the CURRENT finger count.
    // A hand naturally adds/drops a finger mid-swipe; that must never hand a half-done
    // gesture to a different mode's handler. (Feeding a swipe-started gesture a changed
    // event is exactly the kind of mid-gesture mode switch that crashed the native
    // path.) Once a gesture commits to a mode, it sticks until swipe_end().
    if (swipe_state == HT_SWIPE_SWITCH) {
        switch_accum += e.delta.x;
        // Feed Hyprland's native workspace-swipe engine live (absolute accumulated
        // delta). Negated so content follows the finger (swipe right -> the workspace to
        // the left slides in). Flip the sign of switch_accum here to reverse direction.
        g_pUnifiedWorkspaceSwipe->update(-switch_accum);
        return true;
    }
    // Start a 4-finger HORIZONTAL workspace switch via Hyprland's native swipe engine.
    // Handled here, before the cursor_view null guard, because it needs no overview view
    // — otherwise it wouldn't work until the overview had been opened once (the bug where
    // the swipe was dead on first use after a reload).
    if (swipe_state == HT_SWIPE_NONE && e.fingers == OPEN_FINGERS && swipe_direction == 'h'
        && (cursor_view == nullptr || !cursor_view->active)) {
        // 3-finger guard rail: don't hijack a 3-finger move that's still animating.
        if (cursor_view != nullptr && cursor_view->navigating && (int)e.fingers != committed_fingers)
            return false;
        g_pUnifiedWorkspaceSwipe->begin();
        swipe_state = HT_SWIPE_SWITCH;
        switch_accum = e.delta.x;
        committed_fingers = e.fingers;
        g_pUnifiedWorkspaceSwipe->update(-switch_accum);
        return true;
    }

    // The remaining gestures (overview open/close, 3-finger grid navigation) operate on
    // the hyprtasking view, so they need one.
    if (cursor_view == nullptr)
        return false;

    if (swipe_state == HT_SWIPE_MOVE) {
        cursor_view->layout->on_move_swipe(e.delta);
        return true;
    }
    if (swipe_state == HT_SWIPE_OPEN) {
        const float deltaY = OPEN_POSITIVE ? e.delta.y : -e.delta.y;
        swipe_amt += deltaY;
        const float swipe_perc = 1.0 - std::clamp(swipe_amt / OPEN_DISTANCE, 0.01f, 1.0f);
        cursor_view->layout->close_open_lerp(swipe_perc);
        return true;
    }

    // 3-finger guard rail. While a 3-finger move's navigation animation is still
    // settling (navigating == true), a brushed extra finger makes libinput CANCEL the
    // 3-finger swipe and BEGIN a fresh gesture with a different finger count. Starting a
    // new action now (open overview / switch workspace) would corrupt the in-flight
    // navigation, so ignore any new gesture whose finger count differs from the one that
    // is animating. Chained same-count swipes (3 -> 3) are still allowed through.
    if (cursor_view->navigating && (int)e.fingers != committed_fingers)
        return false;

    // No gesture committed yet (swipe_state == HT_SWIPE_NONE): choose the mode from the
    // finger count + dominant direction. The chosen mode then locks in (see above).
    if (e.fingers == OPEN_FINGERS) {
        res = cursor_view->active; // matches old behavior: consume only when overview up
        const float deltaY = OPEN_POSITIVE ? e.delta.y : -e.delta.y;

        if (swipe_direction == 'h') {
            // Horizontal 4-finger reaches here only when the overview is open (the closed
            // case starts the native workspace switch above, before the view guard).
            // Leave it to the overview instead of switching workspaces underneath it.
            return res;
        }
        if (swipe_direction != 'v')
            return res; // direction still ambiguous; wait for a clearer delta

        // 4-finger VERTICAL: open / close the overview (workspace manager).
        if (deltaY <= 0 && (!cursor_view->active || cursor_view->closing)) {
            // Open, or RE-open by interrupting an in-flight close animation.
            refresh_all_grid_caches();  // recompute (adaptive) dims+slots before opening
            cursor_view->show();
            swipe_state = HT_SWIPE_OPEN;
            swipe_amt = OPEN_DISTANCE;
        } else if (deltaY > 0 && cursor_view->active && !cursor_view->closing) {
            cursor_view->hide(false);
            swipe_state = HT_SWIPE_OPEN;
            swipe_amt = 0.0;
        } else {
            return res;
        }
        committed_fingers = e.fingers;

        swipe_amt += deltaY;
        const float swipe_perc = 1.0 - std::clamp(swipe_amt / OPEN_DISTANCE, 0.01f, 1.0f);
        cursor_view->layout->close_open_lerp(swipe_perc);
        return res;
    } else if (e.fingers == MOVE_FINGERS) {
        // 3-finger: navigate the overview grid.
        if (cursor_view->active)
            return false;
        swipe_state = HT_SWIPE_MOVE;
        committed_fingers = e.fingers;
        cursor_view->navigating = true;
        cursor_view->layout->init_position();
        // need to schedule frames for monitor, otherwise the screen doesn't re-render
        g_pHyprRenderer->damageMonitor(cursor_monitor);
        g_pCompositor->scheduleFrameForMonitor(cursor_monitor);
        cursor_view->layout->on_move_swipe(e.delta);
        return true;
    }
    return res;
}

bool HTManager::swipe_end() {
    const PHTVIEW cursor_view = get_view_from_cursor();
    if (cursor_view == nullptr || swipe_state == HT_SWIPE_NONE)
        return false;

    switch (swipe_state) {
        case HT_SWIPE_OPEN: {
            const float OPEN_DISTANCE = HTConfig::value<Config::FLOAT>("gestures:open_distance");
            const float swipe_perc = 1.0 - std::clamp(swipe_amt / OPEN_DISTANCE, 0.01f, 1.0f);
            if (swipe_perc >= 0.5) {
                cursor_view->show(false);
            } else {
                cursor_view->hide(false);
            }
            break;
        }
        case HT_SWIPE_MOVE: {
            const WORKSPACEID ws_id = cursor_view->layout->on_move_swipe_end();
            cursor_view->move_id(ws_id, false);
            break;
        }
        case HT_SWIPE_SWITCH: {
            // 4-finger horizontal: let the native engine commit (it applies
            // workspace_swipe_cancel_ratio / min_speed_to_force and snaps or reverts).
            g_pUnifiedWorkspaceSwipe->end();
            break;
        }
        case HT_SWIPE_NONE:
            break;
    }

    swipe_state = HT_SWIPE_NONE;
    swipe_amt = 0.0;
    switch_accum = 0.0;
    return true;
}
