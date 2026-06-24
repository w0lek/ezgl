#pragma once

#include "ezgl/qt/render_backend.hpp"
#include "ezgl/camera.hpp"
#include "ezgl/color.hpp"

#include <QImage>
#include <QWidget>

namespace ezgl {

class Painter;

/**
 * QPainter-backed rendering backend (deferred_renderer path).
 *
 * Owns the off-screen QImage surface and Painter, recreating them on every
 * resize.  Geometry is re-submitted through deferred_renderer on each frame.
 */
class deferred_backend final : public render_backend {
public:
    /**
     * @param drawing_area     Qt widget to render into (a DrawingAreaWidget,
     *                         which supplies the off-screen surface).
     * @param draw_callback    Application scene-drawing callback, invoked each
     *                         redraw.
     * @param cam              Camera supplying the view and world→screen
     *                         transform.
     * @param background_color Color the surface is cleared to before each frame.
     */
    deferred_backend(QWidget*       drawing_area,
                     draw_canvas_fn draw_callback,
                     camera*        cam,
                     color          background_color);

    /// Destroys the owned Painter and animation renderer. The surface is
    /// owned by the DrawingAreaWidget, so it is not freed here.
    ~deferred_backend() override;

    void redraw() override;
    void redraw_camera_only() override;
    void on_resize(int w, int h) override;
    renderer* create_animation_renderer() override;
    QImage render_to_image(int w, int h) override;

private:
    void recreate_surface();

    QWidget*       m_drawing_area;
    draw_canvas_fn m_draw_callback;
    camera*        m_camera;
    color          m_background_color;

    QImage*   m_surface            = nullptr;
    Painter*  m_painter            = nullptr;
    renderer* m_animation_renderer = nullptr;
};

} // namespace ezgl
