#pragma once

#include "ezgl/irenderer.hpp"

#include <QImage>

/**
 * @file render_backend.hpp
 *
 * @brief Abstract base for ezgl rendering backends + shared backend enums.
 *
 * Three concrete backends implement @ref ezgl::render_backend; ezgl::canvas
 * picks one at runtime based on @ref ezgl::renderer_type. See
 * `doc/renderers.md` for the user-facing comparison.
 *
 * | Backend | Class | Header |
 * |---|---|---|
 * | `immediate` | @c ezgl::immediate_backend | `immediate_backend.hpp` |
 * | `deferred`  | @c ezgl::deferred_backend  | `deferred_backend.hpp` |
 * | `rhi`       | @c ezgl::rhi_backend       | `rhi_backend.hpp` |
 *
 * The rhi backend is the default and is composed of four cooperating
 * classes: @c rhi_backend (lifecycle), @c rhi_renderer (recording +
 * @c irenderer impl), @c RhiCanvasWidget (Qt widget + thread inbox),
 * @c RhiSceneRenderer (GPU resources). See `rhi_renderer.hpp` for the
 * component map.
 */

namespace ezgl {

/**
 * @brief Reasons corresponding to a change in the view.
 */
enum class view_change_reason {
    pan,      ///< The visible world moved without changing zoom level.
    zoom_in,  ///< The visible world area decreased.
    zoom_out, ///< The visible world area increased.
    setup     ///< The view changed because of renderer setup or resize handling. Happens before the GUI is available for interaction.
};

using draw_canvas_fn = void (*)(renderer*);

/**
 * @brief Ask the client to decide whether cached scene geometry can be reused for the new view.
 * 
 * @param ctx Basic information about the view change that the client uses to make a decision.
 * 
 * @return Return true to update only the camera transform/overlay, or false to force a full redraw.
 */
using decide_reuse_geometry_fn = std::function<bool(view_change_reason, renderer*)>;

/// Backend identifier used by @c canvas::set_renderer_type to select
/// which @ref render_backend subclass to instantiate.
enum class renderer_type { immediate, deferred, rhi };

/// MSAA sample count for the rhi backend (both on-screen QRhiWidget and the
/// offscreen render_to_image path use it; every QRhiGraphicsPipeline must
/// match). Valid Qt values are 1, 2, 4, 8, 16; 1 disables MSAA.
///
/// We default to 1 (MSAA off). With sample counts > 1, the multisample
/// coverage resolve thickens 1-pixel-wide primitives: each pixel a thin
/// diagonal line touches gets partial coverage from multiple subsamples
/// and is blended toward the line color, so the line reads as ~2 px wide
/// (and softer) instead of crisp 1 px. For VPR's dense net / route /
/// channel rendering — which is dominated by 1 px strokes — that
/// visible widening is worse than the aliasing MSAA was meant to fix.
inline constexpr int EZGL_RHI_SAMPLE_COUNT = 1;

/// Stable short name for a @ref renderer_type, suitable for log lines and
/// test matrices.
inline constexpr const char* renderer_type_name(renderer_type t) noexcept
{
    switch (t) {
        case renderer_type::immediate: return "immediate";
        case renderer_type::deferred:  return "deferred";
        case renderer_type::rhi:       return "rhi";
        default:                       return "immediate";
    }
}

/**
 * @brief Abstract rendering backend owned by canvas.
 *
 * Each concrete backend encapsulates one rendering path's full lifecycle:
 * frame scheduling, resize handling, and per-frame draw dispatch. canvas
 * selects the right implementation at @c set_renderer_type() time and
 * routes all redraw / resize / capture requests through this interface.
 *
 * Lifecycle pattern used by callers:
 * @code
 *   backend->begin_deferred_redraw_cycle();  // optional: batch
 *     ...mutate state...
 *     backend->redraw();                     // or redraw_camera_only()
 *   backend->end_deferred_redraw_cycle();    // flushes pending
 * @endcode
 */
class render_backend {
public:
    virtual ~render_backend() = default;

    /// Full redraw: re-run the application draw callback to rebuild the
    /// scene from scratch. Use when scene state (block colors, route
    /// trees, …) has changed.
    virtual void redraw() = 0;

    /**
     * @brief Redraw using only a camera (MVP) update when cached geometry is reusable. Meaningful on the RHI path only.
     *
     * @param reason Reason that triggers this camera redraw (e.g. pan, zoom_in).
     * 
     * On the RHI path, the function checks whether the existing scene buffer can be reused.
     * If reuse is allowed, the backend updates the camera transform/overlay without re-running the draw callback.
     * Otherwise, or on non-RHI paths, this falls back to a full redraw.
     */
    virtual void redraw_camera_only(view_change_reason reason) = 0;

    /// Optional batching window. Multiple @ref redraw / @ref
    /// redraw_camera_only calls between @c begin_ / @c end_ may coalesce
    /// into a single GPU frame. Default impl is a no-op for backends
    /// that don't benefit from batching.
    virtual void begin_deferred_redraw_cycle() {}
    /// @see begin_deferred_redraw_cycle
    virtual void end_deferred_redraw_cycle() {}

    /// Resize notification. Backends recreate render targets / swap chains
    /// here as needed.
    virtual void on_resize(int w, int h) = 0;

    /// Return a transient @c renderer instance suitable for one-off
    /// animation overlays (e.g. mouse hit-test highlights painted on top
    /// of the cached scene without rebuilding it). The returned pointer
    /// is owned by the backend and lives until the next frame.
    virtual renderer* create_animation_renderer() = 0;

    /**
     * Render a frame and return it as a QImage. Used by
     * @c canvas::render_to_image() to back @c save_graphics() and
     * headless visual regression tests.
     *
     * Returns a null QImage by default, which signals
     * @c canvas::render_to_image() to fall back to the QPainter-based
     * deferred path. Backends that support GPU readback (e.g.
     * @ref rhi_backend) override this.
     *
     * @param w  Desired output width  (0 = use the widget's current width).
     * @param h  Desired output height (0 = use the widget's current height).
     */
    virtual QImage render_to_image(int /*w*/, int /*h*/) { return {}; }
};

} // namespace ezgl
