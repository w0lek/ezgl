#if defined(EZGL_QT) && defined(EZGL_OGL)

#include "ezgl/qt/ogl_renderer.hpp"
#include "ezgl/camera.hpp"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

std::uint32_t pack_color_rgba(const ezgl::color& c)
{
    return std::uint32_t(c.red)
         | (std::uint32_t(c.green) << 8)
         | (std::uint32_t(c.blue)  << 16)
         | (std::uint32_t(c.alpha) << 24);
}

} // namespace

namespace ezgl {

// ---- construction ----------------------------------------------------------

ogl_renderer::ogl_renderer(OglWidget*   widget,
                             transform_fn transform,
                             camera*      cam,
                             QColor       bg_color)
    : renderer(nullptr,           // painter wired up below
               std::move(transform),
               cam,
               nullptr)           // surface not used in Qt path
    , m_ogl_widget(widget)
    , m_bg_color(bg_color)
    , m_overlay(std::max(1, widget->width()),
                std::max(1, widget->height()),
                QImage::Format_ARGB32_Premultiplied)
    , m_overlay_painter(&m_overlay)
{
    ensure_tile_grid();
    clear_tile_geometry();
    m_overlay.fill(Qt::transparent);
    m_painter = &m_overlay_painter;
    m_overlay_painter.setAntialias(false);
    m_overlay_painter.setSmoothPixmap(false);
}

// ---- frame lifecycle -------------------------------------------------------

void ogl_renderer::begin_frame()
{
    ensure_tile_grid();
    clear_tile_geometry();
    m_palette_rgba.clear();
    m_palette_index.clear();

    if (m_overlay_painter.isActive())
        m_overlay_painter.end();

    const int w = std::max(1, m_ogl_widget->width());
    const int h = std::max(1, m_ogl_widget->height());
    if (m_overlay.width() != w || m_overlay.height() != h)
        m_overlay = QImage(w, h, QImage::Format_ARGB32_Premultiplied);

    m_overlay.fill(Qt::transparent);

    m_overlay_painter.begin(&m_overlay);
    m_overlay_painter.setAntialias(false);
    m_overlay_painter.setSmoothPixmap(false);

    // Reset renderer state to per-frame defaults.
    current_coordinate_system = WORLD;
    rotation_angle             = 0.0;
    horiz_justification        = justification::center;
    vert_justification         = justification::center;
    current_color              = {0, 0, 0, 255};
    current_line_width         = 0;
    current_line_cap           = line_cap::butt;
    current_line_dash          = line_dash::none;
    set_color(current_color);
    set_line_width(current_line_width);
    set_line_cap(current_line_cap);
    set_line_dash(current_line_dash);
}

// ---- helpers ---------------------------------------------------------------

inline OglPosVertex ogl_renderer::make_vertex(point2d p) const
{
    return OglPosVertex{float(p.x), float(p.y)};
}

std::uint32_t ogl_renderer::current_packed_color() const
{
    return pack_color_rgba(current_color);
}

OglStyleIndex ogl_renderer::current_style_index()
{
    const std::uint32_t color = current_packed_color();
    auto it = m_palette_index.find(color);
    if (it != m_palette_index.end())
        return it->second;

    if (m_palette_rgba.size() >= kMaxOglStyleEntries) {
        qFatal("ogl_renderer: palette exceeded %zu RGBA entries",
               kMaxOglStyleEntries);
    }

    const OglStyleIndex next = OglStyleIndex(m_palette_rgba.size());
    m_palette_rgba.push_back(color);
    m_palette_index.emplace(color, next);
    return next;
}

void ogl_renderer::append_segment(std::vector<OglPosVertex>&  verts,
                                   std::vector<OglStyleIndex>& styles,
                                   point2d                     start,
                                   point2d                     end,
                                   OglStyleIndex               style_index)
{
    verts.push_back(make_vertex(start));
    verts.push_back(make_vertex(end));
    styles.push_back(style_index);
    styles.push_back(style_index);
}

void ogl_renderer::append_fill_rect(OglTileBatch& tile,
                                     point2d       p0,
                                     point2d       p1,
                                     OglStyleIndex style_index)
{
    if (p1.x <= p0.x || p1.y <= p0.y)
        return;

    const point2d a{p0.x, p0.y}, b{p1.x, p0.y};
    const point2d c{p0.x, p1.y}, d{p1.x, p1.y};

    tile.fill_verts.push_back(make_vertex(a));
    tile.fill_verts.push_back(make_vertex(b));
    tile.fill_verts.push_back(make_vertex(c));

    tile.fill_verts.push_back(make_vertex(b));
    tile.fill_verts.push_back(make_vertex(d));
    tile.fill_verts.push_back(make_vertex(c));

    tile.fill_styles.insert(tile.fill_styles.end(), 6, style_index);
}

void ogl_renderer::ensure_tile_grid()
{
    const rectangle scene = m_camera->get_initial_world();
    const double scene_width  = std::max(scene.width(),  std::numeric_limits<double>::epsilon());
    const double scene_height = std::max(scene.height(), std::numeric_limits<double>::epsilon());
    const rectangle normalized{{scene.left(), scene.bottom()}, scene_width, scene_height};

    if (m_tiles.size() == std::size_t(kTileGridDimension * kTileGridDimension)
        && normalized == m_scene_bounds) {
        return;
    }

    m_scene_bounds  = normalized;
    m_tile_width    = m_scene_bounds.width()  / double(kTileGridDimension);
    m_tile_height   = m_scene_bounds.height() / double(kTileGridDimension);
    m_tiles.clear();
    m_tiles.resize(std::size_t(kTileGridDimension * kTileGridDimension));

    for (int ty = 0; ty < kTileGridDimension; ++ty) {
        const double bottom = m_scene_bounds.bottom() + double(ty) * m_tile_height;
        const double top    = (ty + 1 == kTileGridDimension)
                              ? m_scene_bounds.top()
                              : (bottom + m_tile_height);
        for (int tx = 0; tx < kTileGridDimension; ++tx) {
            const double left  = m_scene_bounds.left() + double(tx) * m_tile_width;
            const double right = (tx + 1 == kTileGridDimension)
                                 ? m_scene_bounds.right()
                                 : (left + m_tile_width);
            m_tiles[std::size_t(tile_index(tx, ty))].world_bounds =
                rectangle{{left, bottom}, {right, top}};
        }
    }
}

void ogl_renderer::clear_tile_geometry()
{
    for (OglTileBatch& tile : m_tiles) {
        tile.line_verts.clear();   tile.line_styles.clear();
        tile.fill_verts.clear();   tile.fill_styles.clear();
        tile.draw_verts.clear();   tile.draw_styles.clear();
        tile.thick_line_instances.clear(); tile.thick_line_styles.clear();
        tile.dashed_line_instances.clear(); tile.dashed_line_styles.clear();
    }
}

int ogl_renderer::clamp_tile_x(double x) const
{
    const double normalized = (x - m_scene_bounds.left()) / m_tile_width;
    return std::clamp(int(std::floor(normalized)), 0, kTileGridDimension - 1);
}

int ogl_renderer::clamp_tile_y(double y) const
{
    const double normalized = (y - m_scene_bounds.bottom()) / m_tile_height;
    return std::clamp(int(std::floor(normalized)), 0, kTileGridDimension - 1);
}

int ogl_renderer::tile_index(int tile_x, int tile_y) const
{
    return tile_y * kTileGridDimension + tile_x;
}

OglTileBatch& ogl_renderer::tile_at(int tile_x, int tile_y)
{
    return m_tiles[std::size_t(tile_index(tile_x, tile_y))];
}

// ---- thin line helpers -----------------------------------------------------

void ogl_renderer::append_line_to_tiles(point2d start, point2d end, OglStyleIndex style_index)
{
    const rectangle bounds{start, end};
    const int min_tx = clamp_tile_x(bounds.left());
    const int max_tx = clamp_tile_x(bounds.right());
    const int min_ty = clamp_tile_y(bounds.bottom());
    const int max_ty = clamp_tile_y(bounds.top());

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            point2d cs = start, ce = end;
            OglTileBatch& tile = tile_at(tx, ty);
            if (!clip_line_world(tile.world_bounds, cs, ce))
                continue;
            append_segment(tile.line_verts, tile.line_styles, cs, ce, style_index);
        }
    }
}

void ogl_renderer::append_draw_segment_to_tiles(point2d start, point2d end, OglStyleIndex style_index)
{
    const rectangle bounds{start, end};
    const int min_tx = clamp_tile_x(bounds.left());
    const int max_tx = clamp_tile_x(bounds.right());
    const int min_ty = clamp_tile_y(bounds.bottom());
    const int max_ty = clamp_tile_y(bounds.top());

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            point2d cs = start, ce = end;
            OglTileBatch& tile = tile_at(tx, ty);
            if (!clip_line_world(tile.world_bounds, cs, ce))
                continue;
            append_segment(tile.draw_verts, tile.draw_styles, cs, ce, style_index);
        }
    }
}

void ogl_renderer::append_fill_rect_to_tiles(point2d p0, point2d p1, OglStyleIndex style_index)
{
    const rectangle bounds{p0, p1};
    const int min_tx = clamp_tile_x(bounds.left());
    const int max_tx = clamp_tile_x(bounds.right());
    const int min_ty = clamp_tile_y(bounds.bottom());
    const int max_ty = clamp_tile_y(bounds.top());

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            OglTileBatch& tile = tile_at(tx, ty);
            const double left   = std::max(bounds.left(),   tile.world_bounds.left());
            const double right  = std::min(bounds.right(),  tile.world_bounds.right());
            const double bottom = std::max(bounds.bottom(), tile.world_bounds.bottom());
            const double top    = std::min(bounds.top(),    tile.world_bounds.top());
            append_fill_rect(tile, {left, bottom}, {right, top}, style_index);
        }
    }
}

// ---- thick line helpers ----------------------------------------------------

void ogl_renderer::append_thick_segment(OglTileBatch& tile,
                                         point2d       start,
                                         point2d       end,
                                         float         width_px,
                                         OglStyleIndex style_index)
{
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    if (std::sqrt(dx * dx + dy * dy) < 1e-10)
        return;

    tile.thick_line_instances.push_back({
        float(start.x), float(start.y),
        float(end.x),   float(end.y),
        width_px
    });
    tile.thick_line_styles.push_back(style_index);
}

void ogl_renderer::append_thick_line_to_tiles(point2d start, point2d end,
                                               float width_px, OglStyleIndex style_index)
{
    const rectangle bounds{start, end};
    const int min_tx = clamp_tile_x(bounds.left());
    const int max_tx = clamp_tile_x(bounds.right());
    const int min_ty = clamp_tile_y(bounds.bottom());
    const int max_ty = clamp_tile_y(bounds.top());

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            point2d cs = start, ce = end;
            OglTileBatch& tile = tile_at(tx, ty);
            if (!clip_line_world(tile.world_bounds, cs, ce))
                continue;
            append_thick_segment(tile, cs, ce, width_px, style_index);
        }
    }
}

void ogl_renderer::append_thick_draw_segment_to_tiles(point2d start, point2d end,
                                                        float width_px, OglStyleIndex style_index)
{
    // Reuses the same instance pool as thick lines (same pipeline).
    append_thick_line_to_tiles(start, end, width_px, style_index);
}

// ---- dashed line helpers ---------------------------------------------------

void ogl_renderer::append_dashed_segment(OglTileBatch& tile,
                                          point2d       start,
                                          point2d       end,
                                          float         width_px,
                                          float         dash_px,
                                          float         gap_px,
                                          float         phase_world,
                                          OglStyleIndex style_index)
{
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    if (std::sqrt(dx * dx + dy * dy) < 1e-10)
        return;

    tile.dashed_line_instances.push_back({
        float(start.x), float(start.y),
        float(end.x),   float(end.y),
        width_px, dash_px, gap_px, phase_world
    });
    tile.dashed_line_styles.push_back(style_index);
}

void ogl_renderer::append_dashed_line_to_tiles(point2d start, point2d end,
                                                float width_px, float dash_px, float gap_px,
                                                OglStyleIndex style_index)
{
    const point2d original_start = start;
    const rectangle bounds{start, end};
    const int min_tx = clamp_tile_x(bounds.left());
    const int max_tx = clamp_tile_x(bounds.right());
    const int min_ty = clamp_tile_y(bounds.bottom());
    const int max_ty = clamp_tile_y(bounds.top());

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            point2d cs = start, ce = end;
            OglTileBatch& tile = tile_at(tx, ty);
            if (!clip_line_world(tile.world_bounds, cs, ce))
                continue;
            const double pdx = cs.x - original_start.x;
            const double pdy = cs.y - original_start.y;
            const float phase_world = float(std::sqrt(pdx * pdx + pdy * pdy));
            append_dashed_segment(tile, cs, ce, width_px, dash_px, gap_px, phase_world, style_index);
        }
    }
}

void ogl_renderer::append_dashed_draw_segment_to_tiles(point2d start, point2d end,
                                                        float width_px, float dash_px, float gap_px,
                                                        OglStyleIndex style_index)
{
    append_dashed_line_to_tiles(start, end, width_px, dash_px, gap_px, style_index);
}

void ogl_renderer::set_dash_pattern(float width_px, float& dash_px, float& gap_px) const
{
    switch (current_line_dash) {
        case line_dash::none:
            dash_px = 0.0f; gap_px = 0.0f; return;
        case line_dash::asymmetric_5_3:
        default:
            dash_px = 5.0f * width_px;
            gap_px  = 3.0f * width_px;
            return;
    }
}

// ---- MVP computation -------------------------------------------------------

QMatrix4x4 ogl_renderer::compute_mvp() const
{
    const float fw = float(std::max(1, m_ogl_widget->width()));
    const float fh = float(std::max(1, m_ogl_widget->height()));

    const rectangle world  = m_camera->get_world();
    const rectangle screen = m_camera->get_screen();

    const float sx = float(screen.width()  / world.width());
    const float sy = float(screen.height() / world.height());
    const float tx = float(screen.left()   - world.left()   * sx);
    const float ty = float(screen.top()    + world.bottom() * sy);

    QMatrix4x4 m;
    m.setToIdentity();
    m(0, 0) =  2.0f * sx / fw;
    m(0, 3) =  2.0f * tx / fw - 1.0f;
    m(1, 1) =  2.0f * sy / fh;
    m(1, 3) =  1.0f - 2.0f * ty / fh;
    return m;
}

// ---- draw_line override ----------------------------------------------------

void ogl_renderer::draw_line(point2d start, point2d end)
{
    if (current_coordinate_system != WORLD) {
        renderer::draw_line(start, end);
        return;
    }
    if (m_skip_tile_writes)
        return;

    const OglStyleIndex style_index = current_style_index();

    if (current_line_width > 0 || current_line_dash != line_dash::none) {
        const float w = float(std::max(1, current_line_width));
        float dash_px = 0.0f, gap_px = 0.0f;
        set_dash_pattern(w, dash_px, gap_px);
        append_dashed_line_to_tiles(start, end, w, dash_px, gap_px, style_index);
        return;
    }

    append_line_to_tiles(start, end, style_index);
}

// ---- fill_rectangle overrides ----------------------------------------------

void ogl_renderer::fill_rectangle(point2d start, point2d end)
{
    if (current_coordinate_system != WORLD) {
        renderer::fill_rectangle(start, end);
        return;
    }
    if (m_skip_tile_writes)
        return;
    const point2d p0{std::min(start.x, end.x), std::min(start.y, end.y)};
    const point2d p1{std::max(start.x, end.x), std::max(start.y, end.y)};
    append_fill_rect_to_tiles(p0, p1, current_style_index());
}

void ogl_renderer::fill_rectangle(point2d start, double width, double height)
{
    fill_rectangle(start, {start.x + width, start.y + height});
}

void ogl_renderer::fill_rectangle(rectangle r)
{
    fill_rectangle({r.left(), r.bottom()}, {r.right(), r.top()});
}

// ---- draw_rectangle overrides ----------------------------------------------

void ogl_renderer::draw_rectangle(point2d start, point2d end)
{
    if (current_coordinate_system != WORLD) {
        renderer::draw_rectangle(start, end);
        return;
    }
    if (m_skip_tile_writes)
        return;

    const point2d p0{std::min(start.x, end.x), std::min(start.y, end.y)};
    const point2d p1{std::max(start.x, end.x), std::max(start.y, end.y)};
    const OglStyleIndex style_index = current_style_index();

    if (current_line_width > 0 || current_line_dash != line_dash::none) {
        const float w = float(std::max(1, current_line_width));
        float dash_px = 0.0f, gap_px = 0.0f;
        set_dash_pattern(w, dash_px, gap_px);
        append_dashed_draw_segment_to_tiles({p0.x, p0.y}, {p1.x, p0.y}, w, dash_px, gap_px, style_index);
        append_dashed_draw_segment_to_tiles({p1.x, p0.y}, {p1.x, p1.y}, w, dash_px, gap_px, style_index);
        append_dashed_draw_segment_to_tiles({p1.x, p1.y}, {p0.x, p1.y}, w, dash_px, gap_px, style_index);
        append_dashed_draw_segment_to_tiles({p0.x, p1.y}, {p0.x, p0.y}, w, dash_px, gap_px, style_index);
        return;
    }

    append_draw_segment_to_tiles({p0.x, p0.y}, {p1.x, p0.y}, style_index);
    append_draw_segment_to_tiles({p1.x, p0.y}, {p1.x, p1.y}, style_index);
    append_draw_segment_to_tiles({p1.x, p1.y}, {p0.x, p1.y}, style_index);
    append_draw_segment_to_tiles({p0.x, p1.y}, {p0.x, p0.y}, style_index);
}

void ogl_renderer::draw_rectangle(point2d start, double width, double height)
{
    draw_rectangle(start, {start.x + width, start.y + height});
}

void ogl_renderer::draw_rectangle(rectangle r)
{
    draw_rectangle({r.left(), r.bottom()}, {r.right(), r.top()});
}

// ---- flush -----------------------------------------------------------------

void ogl_renderer::flush()
{
    if (m_overlay_painter.isActive())
        m_overlay_painter.end();

    std::vector<OglTileBatch> non_empty;
    non_empty.reserve(m_tiles.size() / 4);
    for (OglTileBatch& tile : m_tiles) {
        if (!tile.empty())
            non_empty.push_back(std::move(tile));
    }

    m_ogl_widget->set_frame_data(
        std::move(non_empty),
        std::move(m_palette_rgba),
        compute_mvp(),
        get_visible_world(),
        m_overlay,
        m_bg_color);

    m_ogl_widget->update();
}

// ---- flush_mvp_only --------------------------------------------------------

void ogl_renderer::flush_mvp_only()
{
    m_ogl_widget->set_mvp_only(compute_mvp(), get_visible_world());
    m_ogl_widget->update();
}

// ---- begin_overlay_frame ---------------------------------------------------

void ogl_renderer::begin_overlay_frame()
{
    if (m_overlay_painter.isActive())
        m_overlay_painter.end();

    const int w = std::max(1, m_ogl_widget->width());
    const int h = std::max(1, m_ogl_widget->height());
    if (m_overlay.width() != w || m_overlay.height() != h)
        m_overlay = QImage(w, h, QImage::Format_ARGB32_Premultiplied);

    m_overlay.fill(Qt::transparent);

    m_overlay_painter.begin(&m_overlay);
    m_overlay_painter.setAntialias(false);
    m_overlay_painter.setSmoothPixmap(false);

    // Reset renderer state so the draw callback sees clean defaults.
    current_coordinate_system = WORLD;
    rotation_angle             = 0.0;
    horiz_justification        = justification::center;
    vert_justification         = justification::center;
    current_color              = {0, 0, 0, 255};
    current_line_width         = 0;
    current_line_cap           = line_cap::butt;
    current_line_dash          = line_dash::none;
    set_color(current_color);
    set_line_width(current_line_width);
    set_line_cap(current_line_cap);
    set_line_dash(current_line_dash);

    m_skip_tile_writes = true;
}

// ---- flush_overlay_and_mvp -------------------------------------------------

void ogl_renderer::flush_overlay_and_mvp()
{
    m_skip_tile_writes = false;

    if (m_overlay_painter.isActive())
        m_overlay_painter.end();

    m_ogl_widget->set_mvp_and_overlay(compute_mvp(), get_visible_world(), m_overlay);
    m_ogl_widget->update();
}

} // namespace ezgl

#endif // EZGL_QT && EZGL_OGL
