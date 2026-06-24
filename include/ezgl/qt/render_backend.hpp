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

/// Application-supplied scene-drawing callback.
using draw_canvas_fn = void (*)(renderer*);

/// Backend identifier used by @c canvas::set_renderer_type to select
/// which @ref render_backend subclass to instantiate.
enum class renderer_type {
  /// Immediate-mode QPainter: every draw call executed synchronously
  /// (@ref immediate_backend).
  immediate,
  /// QPainter with the deferred_renderer path: geometry recorded each frame and
  /// replayed in batches grouped by pen/brush style, minimising QPainter state
  /// changes vs the immediate path (@ref deferred_backend).
  deferred,
  /// Qt RHI GPU-accelerated backend; the default (@ref rhi_backend).
  rhi
};

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
 * Lifecycle pattern used by callers (optional; RHI only — others no-op):
 * @code
 *   backend->suspend_redraw();   // hold show/resize-driven renders
 *     ...show or resize the window; run initial setup...
 *   backend->resume_redraw();    // flush the single pending render once
 * @endcode
 * A direct @ref redraw() / @ref redraw_camera_only() is NOT held while
 * redrawing is suspended — those dispatch immediately; only @ref on_resize defers.
 */
class render_backend {
public:
    virtual ~render_backend() = default;

    /// Full redraw: re-run the application draw callback to rebuild the
    /// scene from scratch. Use when scene state (block colors, route
    /// trees, …) has changed.
    virtual void redraw() = 0;

    /// Camera-only redraw: scene geometry is unchanged, only the camera
    /// transform / overlay layout differs. Backends that maintain a
    /// scene cache (rhi) can skip the user draw callback and just
    /// re-render with the new MVP. The immediate and deferred backends
    /// fall through to a full @ref redraw because they have no cache.
    virtual void redraw_camera_only() = 0;

    /// Suspend redrawing around scene setup (optional; honoured only by RHI).
    ///
    /// suspend_redraw()/resume_redraw() bracket "show the window + run the user's
    /// initial setup" so the RHI path produces exactly one correct GPU frame at
    /// the end (@ref resume_redraw), instead of several premature or redundant
    /// ones during window construction. (On RHI, resize events fired while the
    /// window is shown are recorded as pending and flushed once at
    /// @ref resume_redraw; see @ref rhi_backend.)
    ///
    /// Why only RHI: the QPainter backends (immediate/deferred) treat these as
    /// no-ops, because @c QWidget::update() already defers to a single coalesced
    /// paintEvent and their CPU paint is cheap and idempotent — Qt gives them
    /// the same coalescing for free. RHI flushes straight to the GPU, so it
    /// relies on the explicit suspend_redraw()/resume_redraw() bracket instead.
    virtual void suspend_redraw() {}
    /// @see suspend_redraw
    virtual void resume_redraw() {}

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
