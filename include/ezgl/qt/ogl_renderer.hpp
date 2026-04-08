#pragma once

#if defined(EZGL_QT) && defined(EZGL_OGL)

#include "ezgl/graphics.hpp"
#include "ezgl/qt/painter.hpp"
#include "ezgl/qt/ogl_canvas_widget.hpp"

#include <QMatrix4x4>
#include <QImage>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace ezgl {

/**
 * OpenGL 3.3 GPU-backed renderer with scene tiling.
 *
 * Hot-path primitives (lines, rectangles) are clipped into a fixed 256×256
 * tile grid over the scene bounds.  Each non-empty tile carries its own
 * geometry streams and is submitted to OglWidget as an independent GPU batch.
 *
 * Non-overridden primitives (fill_poly, draw_arc, draw_text, draw_surface, …)
 * fall through to the base-class renderer which draws into m_overlay via
 * m_painter (QPainter → QImage).  The overlay is composited on top of the GPU
 * frame inside OglWidget::paintEvent().
 *
 * API mirrors rhi_renderer exactly so canvas.cpp can switch backends with
 * minimal #ifdef nesting.
 */
class ogl_renderer : public renderer {
public:
    ogl_renderer(OglWidget*   widget,
                 transform_fn transform,
                 camera*      cam,
                 QColor       bg_color);

    ~ogl_renderer() = default;

    // ---- Frame lifecycle ----------------------------------------------------

    /** Reset per-frame state (vertex buffers, overlay) ready for a new draw. */
    void begin_frame();

    // ---- Hot-path overrides -------------------------------------------------

    void draw_line(point2d start, point2d end) override;

    void fill_rectangle(point2d start, point2d end) override;
    void fill_rectangle(point2d start, double width, double height) override;
    void fill_rectangle(rectangle r) override;

    void draw_rectangle(point2d start, point2d end) override;
    void draw_rectangle(point2d start, double width, double height) override;
    void draw_rectangle(rectangle r) override;

    /**
     * Upload all collected geometry to OglWidget and schedule repaint.
     * Ends the overlay painter so the QImage is fully flushed.
     */
    void flush();

    /**
     * Push only a new camera MVP to OglWidget (no geometry re-upload).
     * Call after pan/zoom when primitives have not changed.
     */
    void flush_mvp_only();

private:
    static constexpr int kTileGridDimension = 256;

    // ---- helpers ------------------------------------------------------------

    OglPosVertex    make_vertex(point2d world_pt) const;
    std::uint32_t   current_packed_color() const;
    OglStyleIndex   current_style_index();

    void append_segment(std::vector<OglPosVertex>&  verts,
                        std::vector<OglStyleIndex>& styles,
                        point2d                     start,
                        point2d                     end,
                        OglStyleIndex               style_index);

    void append_fill_rect(OglTileBatch& tile,
                          point2d       p0,
                          point2d       p1,
                          OglStyleIndex style_index);

    void append_line_to_tiles(point2d start, point2d end, OglStyleIndex style_index);
    void append_draw_segment_to_tiles(point2d start, point2d end, OglStyleIndex style_index);
    void append_fill_rect_to_tiles(point2d p0, point2d p1, OglStyleIndex style_index);

    // Thick line helpers (instanced).
    void append_thick_segment(OglTileBatch& tile,
                              point2d       start,
                              point2d       end,
                              float         width_px,
                              OglStyleIndex style_index);
    void append_thick_line_to_tiles(point2d start, point2d end,
                                    float width_px, OglStyleIndex style_index);
    void append_thick_draw_segment_to_tiles(point2d start, point2d end,
                                            float width_px, OglStyleIndex style_index);

    // Dashed line helpers (instanced).
    void append_dashed_segment(OglTileBatch& tile,
                               point2d       start,
                               point2d       end,
                               float         width_px,
                               float         dash_px,
                               float         gap_px,
                               float         phase_world,
                               OglStyleIndex style_index);
    void append_dashed_line_to_tiles(point2d start, point2d end,
                                     float width_px, float dash_px, float gap_px,
                                     OglStyleIndex style_index);
    void append_dashed_draw_segment_to_tiles(point2d start, point2d end,
                                             float width_px, float dash_px, float gap_px,
                                             OglStyleIndex style_index);

    /** Map current_line_dash → dash_px / gap_px (in screen pixels). */
    void set_dash_pattern(float width_px, float& dash_px, float& gap_px) const;

    void ensure_tile_grid();
    void clear_tile_geometry();
    int  clamp_tile_x(double x) const;
    int  clamp_tile_y(double y) const;
    int  tile_index(int tile_x, int tile_y) const;
    OglTileBatch& tile_at(int tile_x, int tile_y);

    /** Compute screen→NDC orthographic matrix from current widget size. */
    QMatrix4x4 compute_mvp() const;

    // ---- state --------------------------------------------------------------

    OglWidget*                    m_ogl_widget;
    QColor                        m_bg_color;

    // Scene tiling metadata and CPU-side tile batches.
    rectangle                     m_scene_bounds;
    double                        m_tile_width  = 1.0;
    double                        m_tile_height = 1.0;
    std::vector<OglTileBatch>     m_tiles;

    // Global style palette shared by every tile for the frame.
    std::vector<std::uint32_t>                    m_palette_rgba;
    std::unordered_map<std::uint32_t, OglStyleIndex> m_palette_index;

    // QPainter overlay — base-class draw calls (text, arcs, …) write here.
    QImage   m_overlay;
    Painter  m_overlay_painter;  // declared AFTER m_overlay
};

} // namespace ezgl

#endif // EZGL_QT && EZGL_OGL
