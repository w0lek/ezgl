#pragma once

#include "ezgl/irenderer.hpp"
#include "ezgl/qt/painter.hpp"

#include <QLineF>
#include <QPolygonF>
#include <QRectF>
#include <QFont>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ezgl {

// ---- style keys ----------------------------------------------------------

/**
 * Style identity for stroked primitives (lines, rect outlines). Two primitives
 * sharing a LineStyleKey can be drawn with the same QPen, so they are batched
 * together to minimise QPainter state changes during flush.
 */
struct LineStyleKey {
    uint32_t color_rgba;   ///< Packed color: r | g<<8 | b<<16 | a<<24.
    uint16_t line_width;   ///< current_line_width clamped to uint16.
    uint8_t  line_cap;     ///< line_cap enum cast to uint8.
    uint8_t  line_dash;    ///< line_dash enum cast to uint8.

    /// @return The four fields packed into one value for use as a batch-map key.
    uint64_t key() const {
        return uint64_t(color_rgba)
             | (uint64_t(line_width) << 32)
             | (uint64_t(line_cap)   << 48)
             | (uint64_t(line_dash)  << 56);
    }
};

/**
 * Style identity for filled primitives (rectangles, polygons). Fills have no
 * stroke attributes, so only color distinguishes them for batching.
 */
struct FillStyleKey {
    uint32_t color_rgba;   ///< Packed color: r | g<<8 | b<<16 | a<<24.

    /// @return The color, used directly as a batch-map key.
    uint64_t key() const { return color_rgba; }
};

// ---- batch storage -------------------------------------------------------
//
// Each batch groups primitives that share one style key, so flush() can set
// the QPen/QBrush once and emit the whole vector in a single QPainter pass.

/// All lines sharing one LineStyleKey, drawn with a single QPen.
struct LineBatch {
    LineStyleKey        style;   ///< Shared stroke style for every line in the batch.
    std::vector<QLineF> lines;   ///< Lines collected under this style.
};

/// All filled rectangles sharing one FillStyleKey, drawn with a single QBrush.
struct FillRectBatch {
    FillStyleKey        style;   ///< Shared fill style for every rectangle in the batch.
    std::vector<QRectF> rects;   ///< Filled rectangles collected under this style.
};

/// All rectangle outlines sharing one LineStyleKey, drawn with a single QPen.
struct DrawRectBatch {
    LineStyleKey        style;   ///< Shared stroke style for every outline in the batch.
    std::vector<QRectF> rects;   ///< Outlined rectangles collected under this style.
};

/// All filled polygons sharing one FillStyleKey, drawn with a single QBrush.
struct FillPolyBatch {
    FillStyleKey            style;   ///< Shared fill style for every polygon in the batch.
    std::vector<QPolygonF>  polys;   ///< Filled polygons collected under this style.
};

/**
 * Snapshot of the renderer's drawing state taken when a command is recorded.
 *
 * Primitives with a simple style (lines, rects, polys) batch by style key, but
 * richer commands (arcs, text, surfaces) carry a full state snapshot so flush()
 * can restore the exact pen/brush/font/justification/rotation each was issued
 * with — replaying them faithfully regardless of recording order.
 */
struct DeferredPainterState {
    t_coordinate_system coordinate_system = WORLD;            ///< WORLD or SCREEN at record time.
    color               draw_color {0, 0, 0, 255};           ///< Stroke/fill color.
    int                 line_width = 0;                      ///< Line width in pixels (0 means 1).
    line_cap            line_cap_style = line_cap::butt;      ///< Line-cap style.
    line_dash           line_dash_style = line_dash::none;    ///< Line-dash pattern.
    double              rotation_radians = 0.0;              ///< Text rotation, in radians.
    justification       horiz_just = justification::center;   ///< Horizontal text/surface anchoring.
    justification       vert_just = justification::center;    ///< Vertical text/surface anchoring.
    QFont               font;                                ///< Font for text commands.
};

/// A deferred (elliptic) arc, stroked or filled, replayed during flush.
struct DeferredArcCommand {
    DeferredPainterState state;             ///< Painter state captured at record time.
    point2d              center;            ///< Arc center.
    double               radius_x = 0.0;    ///< Horizontal radius.
    double               radius_y = 0.0;    ///< Vertical radius (== radius_x for a circle).
    double               start_angle = 0.0; ///< Start angle, degrees CCW from +x.
    double               extent_angle = 0.0;///< Angular sweep, degrees.
    bool                 fill = false;      ///< True fills the wedge, false strokes the arc.
};

/// A deferred text string, replayed during flush.
struct DeferredTextCommand {
    DeferredPainterState state;        ///< Painter state captured at record time.
    point2d              point;        ///< Anchor position (interpreted per state's coordinate system).
    std::string          text;         ///< The string to draw.
    double               bound_x = 0.0; ///< Max width; text is skipped if it exceeds this (DBL_MAX = unbounded).
    double               bound_y = 0.0; ///< Max height; text is skipped if it exceeds this (DBL_MAX = unbounded).
    bool                 scale_font_with_camera = false; ///< WORLD text: shrink the font as the view zooms out.
    double               recorded_world_scale = 1.0;     ///< Camera world scale at record time, for the rescale ratio.
    point2d              screen_offset_px = {0.0, 0.0};  ///< One-shot screen-pixel offset applied at replay.
};

/// A deferred image-surface blit, replayed during flush.
struct DeferredSurfaceCommand {
    DeferredPainterState state;             ///< Painter state captured at record time.
    surface*             p_surface = nullptr;///< Image to draw (not owned).
    point2d              anchor_point;       ///< Anchor position, placed per the state's justification.
    double               scale_factor = 1.0; ///< Uniform scale applied to the surface.
};

/**
 * A small filled triangle (e.g., an arrow head) that follows a world
 * position but stays at a constant SCREEN size at every zoom level.
 *
 * anchor_world is the centroid of the triangle in WORLD coords; it
 * pans/zooms with the camera. The three corner offsets are stored in
 * SCREEN PIXELS measured at record time and applied at replay time as
 * pixel-space offsets from anchor_world's projected screen position —
 * so the triangle's pixel size stays fixed regardless of zoom.
 */
struct DeferredArrowTriangleCommand {
    DeferredPainterState state;
    point2d              anchor_world;
    point2d              offset_a_px;
    point2d              offset_b_px;
    point2d              offset_c_px;
};

using DeferredOverlayCommand =
    std::variant<DeferredArcCommand,
                 DeferredTextCommand,
                 DeferredSurfaceCommand,
                 DeferredArrowTriangleCommand>;

// ---- deferred_renderer ---------------------------------------------------

class deferred_renderer : public irenderer {
    const double MINIMAL_VISIBLE_TEXT_BOUND_Y_IN_PX = 5.0;
public:
    deferred_renderer(Painter *painter,
                      camera *cam);

    ~deferred_renderer() override = default;

    // ---- irenderer: hot-path draw calls (batched) --------------------------

    void draw_line(const point2d& start, const point2d& end) override;

    void fill_rectangle(const point2d& start, const point2d& end) override;
    void fill_rectangle(const point2d& start, double width, double height) override;
    void fill_rectangle(const rectangle& r) override;

    void draw_rectangle(const point2d& start, const point2d& end) override;
    void draw_rectangle(const point2d& start, double width, double height) override;
    void draw_rectangle(const rectangle& r) override;

    // ---- irenderer: overlay draw calls (deferred to command queue) ---------

    void fill_poly(const std::vector<point2d>& points) override;
    void fill_triangle(const point2d& a, const point2d& b, const point2d& c) override;
    void fill_arrow_pointer_triangle(const point2d& anchor_world,
                                      const point2d& dir_world,
                                      float          arrow_size_px) override;
    void draw_elliptic_arc(const point2d& center, double radius_x, double radius_y,
                           double start_angle, double extent_angle) override;
    void draw_arc(const point2d& center, double radius,
                  double start_angle, double extent_angle) override;
    void fill_elliptic_arc(const point2d& center, double radius_x, double radius_y,
                           double start_angle, double extent_angle) override;
    void fill_arc(const point2d& center, double radius,
                  double start_angle, double extent_angle) override;
    void draw_text(const point2d& point, std::string const& text) override;
    void draw_text(const point2d& point, std::string const& text,
                   double bound_x, double bound_y) override;
    void draw_surface(surface* p_surface, const point2d& anchor_point,
                      double scale_factor = 1) override;

    // ---- Flush all batches to the underlying QPainter, then reset ----------
    void flush();

    // ---- Methods used by rhi_renderer --------------------------------------

    // Replay stored overlay commands without resetting (for camera-only update).
    void replay_overlay();

    // Discard all stored commands and batches (called at begin of new frame).
    void clear_overlay_and_batches();

protected:
    void replay();
    void clear_deferred_primitives();

private:
    void ensure_overlay_index_grid();
    int clamp_overlay_tile_x(double x) const;
    int clamp_overlay_tile_y(double y) const;
    void index_world_overlay_command(std::uint32_t command_index,
                                     rectangle      bounds);
    void reset();
    DeferredPainterState capture_painter_state() const;
    void apply_painter_state(const DeferredPainterState& state);

    QRectF screen_viewport_rect() const;
    bool screen_rect_visible(const QRectF& rect, double padding = 0.0) const;
    bool screen_line_visible(const QLineF& line, double line_width) const;
    bool screen_arc_visible(const point2d& center,
                            double radius_x,
                            double radius_y) const;
    bool screen_text_visible(const point2d& point,
                             const std::string& text,
                             double bound_x,
                             double bound_y) const;
    bool screen_surface_visible(surface *p_surface,
                                const point2d& point,
                                double scale_factor) const;

    LineStyleKey current_line_style() const;
    FillStyleKey current_fill_style() const;

    void add_line(const LineStyleKey &s, QLineF line);
    void add_fill_rect(const FillStyleKey &s, QRectF rect);
    void add_draw_rect(const LineStyleKey &s, QRectF rect);
    void add_fill_poly(const FillStyleKey &s, QPolygonF poly);

    QRectF to_screen_rect(const point2d& start, const point2d& end);

    void push_arc_command(const point2d& center, double radius_x, double radius_y,
                          double start_angle, double extent_angle, bool fill);

    // Batch vectors — maintain submission order for painter's algorithm.
    std::vector<LineBatch>     m_line_batches;
    std::vector<FillRectBatch> m_fill_rect_batches;
    std::vector<DrawRectBatch> m_draw_rect_batches;
    std::vector<FillPolyBatch> m_fill_poly_batches;

    // Fast lookup: style key → index into the vectors above.
    std::unordered_map<uint64_t, size_t> m_line_idx;
    std::unordered_map<uint64_t, size_t> m_fill_rect_idx;
    std::unordered_map<uint64_t, size_t> m_draw_rect_idx;
    std::unordered_map<uint64_t, size_t> m_fill_poly_idx;
    std::vector<DeferredOverlayCommand>  m_overlay_commands;
    rectangle                            m_overlay_index_scene_bounds;
    double                               m_overlay_index_tile_width = 1.0;
    double                               m_overlay_index_tile_height = 1.0;
    std::vector<std::vector<std::uint32_t>> m_indexed_world_overlay_buckets;
    std::vector<std::uint32_t>           m_unindexed_overlay_commands;
    std::vector<std::uint32_t>           m_overlay_query_marks;
    std::uint32_t                        m_overlay_query_generation = 1;
};

} // namespace ezgl
