#pragma once

#include "ezgl/qt/render_backend.hpp"
#include "ezgl/camera.hpp"
#include "ezgl/color.hpp"
#include "ezgl/qt/rhi_canvas_widget.hpp"

#include <memory>
#include <QColor>

namespace ezgl {

class rhi_renderer;

/**
 * @brief Qt RHI GPU-backed @ref render_backend implementation. Lifecycle
 * wrapper around @ref rhi_renderer.
 *
 * Owns a persistent @ref rhi_renderer plus the redraw-suspension scheduling
 * state (the suspend flag and pending-redraw flags). Full redraws go through
 * @ref rhi_renderer::flush() — the heavy path: it re-runs the draw callback and
 * re-uploads scene geometry to the GPU. Camera-only redraws go through
 * @ref rhi_renderer::flush_mvp_only(), which is cheap: it only updates the MVP
 * transform on the GPU and reuses the existing geometry buffers.
 *
 * @par Suspend / pending flag state machine
 * @code
 *   suspend_redraw();        // m_is_redraw_suspended = true
 *     on_resize(w, h);       // sets m_is_redraw_requested or m_is_camera_update_requested
 *                            //   (no flush while redrawing is suspended)
 *     ...
 *   resume_redraw();         // m_is_redraw_suspended = false; if any pending,
 *                            //   flush once (full beats camera-only).
 * @endcode
 *
 * @ref redraw() and @ref redraw_camera_only() always dispatch immediately;
 * only resize-driven redraws are deferred while redrawing is suspended. The
 * first draw flips @c m_has_drawn_frame so subsequent resize events know
 * whether the scene has been initialised.
 *
 * @par Headless capture
 * @ref render_to_image() never touches the on-screen widget or its
 * @ref rhi_renderer. It constructs a transient @ref rhi_renderer via the
 * headless (size-based) constructor, runs the draw callback through it,
 * calls @ref rhi_renderer::flush_capture(), and passes the captured
 * frame into the static helper @ref RhiCanvasWidget::render_offscreen(),
 * which builds its own standalone @c QRhi over @c QOffscreenSurface. No
 * @ref RhiCanvasWidget instance is created.
 */
class rhi_backend final : public render_backend {
public:
    rhi_backend(RhiCanvasWidget* widget,
                draw_canvas_fn   draw_callback,
                camera*          cam,
                color            background_color);
    ~rhi_backend() override;

    /// Full redraw: (re)create the @ref rhi_renderer if needed, run the draw
    /// callback, and present a GPU frame via @ref rhi_renderer::flush(). Always
    /// flushes immediately; it is not deferred while redrawing is suspended
    /// (@ref suspend_redraw / @ref resume_redraw) — only @ref on_resize defers.
    void redraw() override;

    /// Camera-only redraw: if a frame has already been drawn, re-present with
    /// the new MVP via @ref rhi_renderer::flush_mvp_only(); otherwise falls back
    /// to a full @ref redraw(). Always flushes immediately; it is not deferred
    /// while redrawing is suspended (@ref suspend_redraw / @ref resume_redraw) —
    /// only @ref on_resize defers.
    void redraw_camera_only() override;

    /// Suspend redrawing. While suspended, @ref on_resize records a pending
    /// redraw instead of flushing, so a burst of show/resize events during
    /// window setup coalesces into a single GPU frame at @ref resume_redraw().
    /// Direct redraw() / redraw_camera_only() calls are unaffected and still
    /// flush immediately.
    void suspend_redraw() override;

    /// Resume redrawing and, if a redraw became pending while suspended, flush
    /// once (a pending full redraw beats a camera-only one).
    void resume_redraw() override;

    /// Resize notification: record the new size; while redrawing is suspended,
    /// mark a pending redraw (camera-only if the geometry can be reused,
    /// otherwise a full redraw) instead of flushing; otherwise redraw
    /// immediately.
    void on_resize(int w, int h) override;

    /// Animation overlay renderer. Returns an immediate renderer painting
    /// into the widget surface on top of the cached GPU frame.
    renderer* create_animation_renderer() override;

    /// Headless PNG capture. See class brief for the offscreen flow.
    QImage render_to_image(int w, int h) override;

private:
    RhiCanvasWidget*              m_widget;
    draw_canvas_fn                m_draw_callback;
    camera*                       m_camera;
    QColor                        m_bg_color;
    std::unique_ptr<rhi_renderer> m_renderer;

    bool m_is_redraw_suspended        = false; ///< True between suspend_redraw() and resume_redraw(); while set, resize-driven redraws are recorded as pending instead of flushed (direct redraw() still flushes immediately).
    bool m_is_redraw_requested        = false; ///< Set by on_resize() while redrawing is suspended: a full redraw is pending, to be flushed at resume_redraw().
    bool m_is_camera_update_requested = false; ///< Set by on_resize() while redrawing is suspended: a camera-only redraw is pending, to be flushed at resume_redraw().
    bool m_has_drawn_frame            = false; ///< Whether at least one frame has been drawn; lets resize events tell if the scene is initialised.

    int m_last_w = 0;
    int m_last_h = 0;
};

} // namespace ezgl
