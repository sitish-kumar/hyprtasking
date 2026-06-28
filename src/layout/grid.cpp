#include "grid.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprland/src/managers/animation/AnimationManager.hpp>
#include <hyprland/src/managers/animation/DesktopAnimationManager.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/config/shared/workspace/WorkspaceRuleManager.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>

#include "../config.hpp"
#include "../globals.hpp"
#include "../grid_dims.hpp"
#include "../overview.hpp"
#include "../render.hpp"
#include "../types.hpp"
#include "src/layout/target/Target.hpp"

using Hyprutils::Utils::CScopeGuard;

HTLayoutGrid::HTLayoutGrid(VIEWID new_view_id) : HTLayoutBase(new_view_id) {
    auto &anim_tree = Config::animationTree();
    g_pAnimationManager->createAnimation(
        {0, 0},
        offset,
        anim_tree->getAnimationPropertyConfig("workspaces"),
        AVARDAMAGE_NONE
    );
    g_pAnimationManager->createAnimation(
        1.f,
        scale,
        anim_tree->getAnimationPropertyConfig("workspaces"),
        AVARDAMAGE_NONE
    );

    refresh_workspace_cache();
    init_position();
}

// Bit layout: [layer:24][y:20][x:20]. Used purely as an unordered_map key;
// limits are implicit and not enforced (grid dims are not validated).
long long HTLayoutGrid::pack_slot(int layer, int x, int y) {
    return ((long long)layer << 40) | ((long long)(uint32_t)y << 20) | (long long)(uint32_t)x;
}

WORKSPACEID HTLayoutGrid::slot_workspace(int layer, int x, int y) {
    const auto it = slot_ws_cache.find(pack_slot(layer, x, y));
    if (it == slot_ws_cache.end())
        return WORKSPACE_INVALID;
    return it->second;
}

// Returns the grid dimensions to use. With `grid:adaptive` enabled, the grid is
// sized as a square that fits the number of real workspaces on this monitor
// (n=9 -> 3x3, n=10..16 -> 4x4, ...). Otherwise the static grid:rows/grid:cols
// config values are used. All callers go through this so rendering, navigation
// and slot assignment always agree on the dimensions.
// Recomputes the grid dimensions and caches them in m_grid_rows/m_grid_cols.
// Called ONCE per rebuild from refresh_workspace_cache so that every render and
// navigation site reads a stable size for the whole frame -- recomputing the
// workspace count per-call let the size change underneath an open overview and
// desynced the slot<->window mapping.
void HTLayoutGrid::refresh_grid_dims() {
    if (!HTConfig::value<Config::INTEGER>("grid:adaptive")) {
        m_grid_rows = std::max(1, static_cast<int>(HTConfig::value<Config::INTEGER>("grid:rows")));
        m_grid_cols = std::max(1, static_cast<int>(HTConfig::value<Config::INTEGER>("grid:cols")));
        return;
    }

    // Size to the HIGHEST workspace number on this monitor so workspace N lands
    // on cell N (numbered layout), plus a spare "+" cell. Square. So moving a
    // window to workspace 9 grows the grid to 4x4 even if only a few exist.
    int max_id = 0;
    for (const auto& w : g_pCompositor->getWorkspacesCopy()) {
        if (w == nullptr || w->m_id <= 0)
            continue;
        if (w->monitorID() != view_id)
            continue;
        max_id = std::max(max_id, static_cast<int>(w->m_id));
    }

    // grid_dims_for_count(X) returns the smallest square fitting X+1; passing
    // max_id makes a workspace numbered max_id fit with a trailing spare cell.
    grid_dims_for_count(max_id, m_grid_rows, m_grid_cols);
}

void HTLayoutGrid::get_grid_dims(int& rows, int& cols) {
    rows = m_grid_rows;
    cols = m_grid_cols;
}

void HTLayoutGrid::refresh_workspace_cache(
    const std::unordered_set<WORKSPACEID>& extra_off_limits
) {
    (void)extra_off_limits;  // numbered layout is deterministic; no cross-grid dedup needed
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    refresh_grid_dims();
    int ROWS, COLS;
    get_grid_dims(ROWS, COLS);
    const int LAYERS = HTConfig::value<Config::INTEGER>("grid:layers");
    if (ROWS <= 0 || COLS <= 0 || LAYERS <= 0)
        return;

    ws_slot_cache.clear();
    slot_ws_cache.clear();

    // Numbered layout: cell index i holds workspace (i+1), row-major. Real
    // workspaces land on their own numbered cell (ws 9 -> cell 9); every other
    // cell is a synthetic workspace that is clickable to create/switch to that
    // number. Deterministic, so it stays stable across refreshes without any
    // prior-slot bookkeeping.
    const int    per_layer    = ROWS * COLS;
    const size_t total_slots  = static_cast<size_t>(LAYERS) * per_layer;
    for (size_t i = 0; i < total_slots; i++) {
        const int        rem = static_cast<int>(i % per_layer);
        const HTGridSlot s {static_cast<int>(i / per_layer), rem % COLS, rem / COLS};
        const WORKSPACEID id = static_cast<WORKSPACEID>(i + 1);
        ws_slot_cache[id]                            = s;
        slot_ws_cache[pack_slot(s.layer, s.x, s.y)] = id;
    }
}

std::string HTLayoutGrid::layout_name() {
    return "grid";
}

WORKSPACEID HTLayoutGrid::get_ws_id_in_direction(int x, int y, std::string& direction) {
    const int LOOP = HTConfig::value<Config::INTEGER>("grid:loop");
    int ROWS, COLS;
    get_grid_dims(ROWS, COLS);

    if (direction == "up") {
        y--;
    } else if (direction == "down") {
        y++;
    } else if (direction == "right") {
        x++;
    } else if (direction == "left") {
        x--;
    } else {
        return WORKSPACE_INVALID;
    }

    if (LOOP) {
        x = (x + COLS) % COLS;
        y = (y + ROWS) % ROWS;
    }
    return get_ws_id_from_xy(x, y);
}

void HTLayoutGrid::on_move_swipe(Vector2D delta) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const float MOVE_DISTANCE = HTConfig::value<Config::FLOAT>("gestures:move_distance");
    int ROWS, COLS;
    get_grid_dims(ROWS, COLS);
    const CBox min_ws = calculate_ws_box(0, 0, HT_VIEW_CLOSED);
    const CBox max_ws = calculate_ws_box(COLS - 1, ROWS - 1, HT_VIEW_CLOSED);

    Vector2D new_offset = offset->value() + delta / MOVE_DISTANCE * max_ws.w;
    new_offset = new_offset.clamp(Vector2D {-max_ws.x, -max_ws.y}, Vector2D {-min_ws.x, -min_ws.y});

    offset->resetAllCallbacks();
    offset->setValueAndWarp(new_offset);
}

WORKSPACEID HTLayoutGrid::on_move_swipe_end() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return WORKSPACE_INVALID;

    build_overview_layout(HT_VIEW_CLOSED);
    WORKSPACEID closest = WORKSPACE_INVALID;
    double closest_dist = 1e9;
    for (const auto& [ws_id, box] : overview_layout) {
        const float dist_sq = offset->value().distanceSq(Vector2D {-box.box.x, -box.box.y});
        if (dist_sq < closest_dist) {
            closest_dist = dist_sq;
            closest = ws_id;
        }
    }
    return closest;
}

void HTLayoutGrid::close_open_lerp(float perc) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    double open_scale =
        calculate_ws_box(0, 0, HT_VIEW_OPENED).w / monitor->m_transformedSize.x; // 1 / ROWS
    Vector2D open_pos = {0, 0};

    build_overview_layout(HT_VIEW_CLOSED);
    double close_scale = 1.;
    Vector2D close_pos = -overview_layout[monitor->m_activeWorkspace->m_id].box.pos();

    double new_scale = std::lerp(close_scale, open_scale, perc);
    Vector2D new_pos = Vector2D {
        std::lerp(close_pos.x, open_pos.x, perc),
        std::lerp(close_pos.y, open_pos.y, perc)
    };

    scale->resetAllCallbacks();
    offset->resetAllCallbacks();
    scale->setValueAndWarp(new_scale);
    offset->setValueAndWarp(new_pos);
}

void HTLayoutGrid::on_show(CallbackFun on_complete) {
    CScopeGuard x([this, &on_complete] {
        if (on_complete != nullptr)
            offset->setCallbackOnEnd(on_complete);
    });

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    *scale = calculate_ws_box(0, 0, HT_VIEW_OPENED).w / monitor->m_transformedSize.x; // 1 / ROWS
    // Offset for the whole grid of workspaces
    *offset = {0, 0};
}

void HTLayoutGrid::on_hide(CallbackFun on_complete) {
    CScopeGuard x([this, &on_complete] {
        if (on_complete != nullptr)
            offset->setCallbackOnEnd(on_complete);
    });

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    build_overview_layout(HT_VIEW_CLOSED);
    *scale = 1.;
    // End workspace to end up on
    *offset = -overview_layout[monitor->m_activeWorkspace->m_id].box.pos();
}

void HTLayoutGrid::on_move(WORKSPACEID old_id, WORKSPACEID new_id, CallbackFun on_complete) {
    CScopeGuard x([this, &on_complete] {
        if (on_complete != nullptr)
            offset->setCallbackOnEnd(on_complete);
    });

    const PHTVIEW par_view = ht_manager->get_view_from_id(view_id);
    if (par_view == nullptr || par_view->active)
        return;

    // prevent the thing from animating
    g_pCompositor->getWorkspaceByID(old_id)->m_renderOffset->warp();
    g_pCompositor->getWorkspaceByID(new_id)->m_renderOffset->warp();

    build_overview_layout(HT_VIEW_CLOSED);
    *scale = 1.;
    // Target workspace to animate to
    *offset = -overview_layout[new_id].box.pos();
}

bool HTLayoutGrid::should_render_window(PHLWINDOW window) {
    bool ori_result = HTLayoutBase::should_render_window(window);

    const PHLMONITOR monitor = get_monitor();
    if (window == nullptr || monitor == nullptr)
        return ori_result;

    const SP<Layout::ITarget> target = g_layoutManager->dragController()->target();
    if (target != nullptr && window == target->window())
        return false;

    PHLWORKSPACE workspace = window->m_workspace;
    if (workspace == nullptr)
        return false;

    CBox window_box = get_global_window_box(window, window->workspaceID());
    if (window_box.empty())
        return false;
    if (window_box.intersection(monitor->logicalBox()).empty())
        return false;

    return ori_result;
}

float HTLayoutGrid::drag_window_scale() {
    return scale->value();
}

void HTLayoutGrid::init_position() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    if (monitor->m_activeWorkspace == nullptr)
        return;

    // Sync to the layer of whatever workspace is currently active on this
    // monitor. Fresh views (e.g. after monitor reconnect) start at layer 0,
    // so without this the overview would open on the wrong layer.
    const auto sit = ws_slot_cache.find(monitor->m_activeWorkspace->m_id);
    if (sit != ws_slot_cache.end())
        layer = sit->second.layer;

    build_overview_layout(HT_VIEW_CLOSED);

    const auto it = overview_layout.find(monitor->m_activeWorkspace->m_id);
    if (it == overview_layout.end() || it->second.box.empty())
        return;

    // Kill any in-flight on_move settle animation (and its navigating=false
    // callback) before seeding the start offset. Otherwise a second move swipe
    // begun mid-animation fights the previous switch's offset goal and snaps
    // instead of tracking the finger.
    offset->resetAllCallbacks();
    offset->setValueAndWarp(-it->second.box.pos());
    scale->setValueAndWarp(1.f);
}

CBox HTLayoutGrid::calculate_ws_box(int x, int y, HTViewStage stage) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    // Monitor may not have its final size yet during connect/reconnect
    if (monitor->m_transformedSize.x < 1 || monitor->m_transformedSize.y < 1)
        return {};

    int ROWS, COLS;
    get_grid_dims(ROWS, COLS);
    const int GAPS_USE_ASPECT_RATIO = HTConfig::value<Config::INTEGER>("grid:gaps_use_aspect_ratio");
    const float GAP_SIZE = HTConfig::value<Config::FLOAT>("gap_size") * monitor->m_scale;
    const Vector2D gaps = {
        GAP_SIZE,
        GAPS_USE_ASPECT_RATIO
            ? GAP_SIZE * monitor->m_transformedSize.y / monitor->m_transformedSize.x
            : GAP_SIZE
    };

    if (GAP_SIZE > std::min(monitor->m_transformedSize.x, monitor->m_transformedSize.y)
        || GAP_SIZE < 0)
        return {};

    double render_x = (monitor->m_transformedSize.x - gaps.x * (COLS + 1)) / COLS;
    double render_y = (monitor->m_transformedSize.y - gaps.y * (ROWS + 1)) / ROWS;
    const double mon_aspect = monitor->m_transformedSize.x / monitor->m_transformedSize.y;
    Vector2D start_offset {};

    // make correct aspect ratio
    if (render_y * mon_aspect > render_x) {
        start_offset.y = (render_y - render_x / mon_aspect) * ROWS / 2.f;
        render_y = render_x / mon_aspect;
    } else if (render_x / mon_aspect > render_y) {
        start_offset.x = (render_x - render_y * mon_aspect) * COLS / 2.f;
        render_x = render_y * mon_aspect;
    }

    float use_scale = scale->value();
    Vector2D use_offset = offset->value();
    if (stage == HT_VIEW_CLOSED) {
        use_scale = 1;
        use_offset = Vector2D {0, 0};
    } else if (stage == HT_VIEW_OPENED) {
        use_scale = render_x / monitor->m_transformedSize.x;
        use_offset = Vector2D {0, 0};
    }

    const Vector2D ws_sz = monitor->m_transformedSize * use_scale;
    return CBox {Vector2D {x, y} * (ws_sz + gaps) + gaps + use_offset + start_offset, ws_sz};
};

void HTLayoutGrid::build_overview_layout(HTViewStage stage) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    int ROWS, COLS;
    get_grid_dims(ROWS, COLS);

    const PHLMONITOR last_monitor = Desktop::focusState()->monitor();
    Desktop::focusState()->rawMonitorFocus(monitor);

    overview_layout.clear();
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            const WORKSPACEID ws_id = slot_workspace(layer, x, y);
            if (ws_id == WORKSPACE_INVALID)
                continue;
            CBox ws_box = calculate_ws_box(x, y, stage);
            ws_box.round();
            overview_layout[ws_id] = HTWorkspace {x, y, ws_box};
        }
    }

    if (last_monitor != nullptr)
        Desktop::focusState()->rawMonitorFocus(last_monitor);
}

// Render each visible workspace directly into its grid tile via a scaled
// renderWorkspace (renderModif). The renderTexture hook keeps the per-surface
// scissor in sync with that renderModif, so window contents aren't culled near
// tile edges.
void HTLayoutGrid::render() {
    HTLayoutBase::render();
    CScopeGuard x([this] { post_render(); });

    const PHTVIEW par_view = ht_manager->get_view_from_id(view_id);
    if (par_view == nullptr)
        return;
    const PHLMONITOR monitor = par_view->get_monitor();
    if (monitor == nullptr)
        return;

    static auto PACTIVECOL = CConfigValue<Config::IComplexConfigValue>("general:col.active_border");
    static auto PINACTIVECOL = CConfigValue<Config::IComplexConfigValue>("general:col.inactive_border");

    auto* const ACTIVECOL = (Config::CGradientValueData*)(PACTIVECOL.ptr());
    auto* const INACTIVECOL = (Config::CGradientValueData*)(PINACTIVECOL.ptr());

    const float BORDERSIZE = HTConfig::value<Config::FLOAT>("border_size");
    const int   ROUNDING   = static_cast<int>(HTConfig::value<Config::INTEGER>("rounding") * monitor->m_scale);
    const auto time = Time::steadyNow();

    CBox monitor_box = {{0, 0}, monitor->m_transformedSize};

    CRectPassElement::SRectData bg;
    bg.color = CHyprColor {HTConfig::value<Config::INTEGER>("bg_color")}.stripA();
    bg.box = monitor_box;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(bg));

    build_overview_layout(HT_VIEW_ANIMATING);

    // Hyprland only fully renders the active workspace, so render_workspace_at_box
    // swaps each tile's workspace in as it renders; capture the real active one to
    // restore at the end and to pick out the active-border color.
    const PHLWORKSPACE start_workspace = monitor->m_activeWorkspace;
    if (start_workspace == nullptr)
        return;
    g_pDesktopAnimationManager->startAnimation(
        start_workspace, CDesktopAnimationManager::ANIMATION_TYPE_OUT, false, true
    );
    start_workspace->m_visible = false;

    CBox global_mon_box = {monitor->m_position, monitor->m_transformedSize};
    const auto tile_visible = [&](const CBox& box) {
        if (box.width < 0.01 || box.height < 0.01)
            return false;
        CBox global_box = {box.pos() + monitor->m_position, box.size()};
        return !global_box.expand(BORDERSIZE).intersection(global_mon_box).empty();
    };

    // Borders first, so window contents render on top of them. Workspace contents now
    // render unclipped (a window moving to a new workspace on release can extend past
    // its tile), and should not be covered by the tile borders.
    for (const auto& [ws_id, ws_layout] : overview_layout) {
        if (!tile_visible(ws_layout.box))
            continue;
        CBorderPassElement::SBorderData bdata;
        bdata.box = ws_layout.box;
        bdata.grad1 = start_workspace->m_id == ws_id ? *ACTIVECOL : *INACTIVECOL;
        bdata.borderSize = BORDERSIZE;
        bdata.round = ROUNDING;
        g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(bdata));
    }

    // workspace may be nullptr for empty/never-visited slots: render_workspace_at_box
    // still draws their background + wallpaper layers so the tile isn't blank. Render
    // the active workspace last so its windows (e.g. one just dropped) stay on top of
    // the neighbouring tiles.
    for (const auto& [ws_id, ws_layout] : overview_layout) {
        if (!tile_visible(ws_layout.box) || ws_id == start_workspace->m_id)
            continue;
        render_workspace_at_box(monitor, g_pCompositor->getWorkspaceByID(ws_id), time, ws_layout.box);
    }
    if (const auto it = overview_layout.find(start_workspace->m_id);
        it != overview_layout.end() && tile_visible(it->second.box))
        render_workspace_at_box(monitor, start_workspace, time, it->second.box);

    // Draw a "+" on cells whose numbered workspace doesn't exist yet, so empty
    // cells read as "create/switch here". (#8)
    if (HTConfig::value<Config::INTEGER>("plus_on_empty")) {
        const CHyprColor plus_col {1.f, 1.f, 1.f, 0.22f};
        for (const auto& [ws_id, ws_layout] : overview_layout) {
            if (!tile_visible(ws_layout.box))
                continue;
            if (g_pCompositor->getWorkspaceByID(ws_id) != nullptr)
                continue;  // real workspace -> no plus
            const CBox&  b     = ws_layout.box;
            const double arm   = std::min(b.width, b.height) * 0.09;
            const double thick = std::max(2.0, arm * 0.28);
            const double cx    = b.x + b.width / 2.0;
            const double cy    = b.y + b.height / 2.0;

            CRectPassElement::SRectData hbar;
            hbar.color = plus_col;
            hbar.box   = CBox {cx - arm, cy - thick / 2.0, arm * 2.0, thick};
            hbar.round = static_cast<int>(thick / 2.0);
            g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(hbar));

            CRectPassElement::SRectData vbar;
            vbar.color = plus_col;
            vbar.box   = CBox {cx - thick / 2.0, cy - arm, thick, arm * 2.0};
            vbar.round = static_cast<int>(thick / 2.0);
            g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(vbar));
        }
    }

    monitor->m_activeWorkspace = start_workspace;
    g_pDesktopAnimationManager->startAnimation(
        start_workspace, CDesktopAnimationManager::ANIMATION_TYPE_IN, false, true
    );
    start_workspace->m_visible = true;

    g_pHyprRenderer->damageMonitor(monitor);

    // Dragged window rendered on top, following the cursor.
    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return;
    const SP<Layout::ITarget> target = g_layoutManager->dragController()->target();
    if (target == nullptr)
        return;
    const PHLWINDOW dragged_window = target->window();
    if (dragged_window == nullptr)
        return;
    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    const CBox window_box = dragged_window->getWindowMainSurfaceBox()
                                .translate(-mouse_coords)
                                .scale(cursor_view->layout->drag_window_scale())
                                .translate(mouse_coords);
    if (!window_box.intersection(monitor->logicalBox()).empty())
        render_window_at_box(dragged_window, monitor, time, window_box);
}
