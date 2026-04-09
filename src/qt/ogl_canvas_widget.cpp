#if defined(EZGL_QT) && defined(EZGL_OGL)

#include "ezgl/qt/ogl_canvas_widget.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QOpenGLContext>
#include <QOpenGLVersionFunctionsFactory>
#include <QSurfaceFormat>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

#include "ezgl/qt/ezgl_qtcompat.hpp"

namespace {

// ---- constants -------------------------------------------------------------

constexpr std::size_t kMvpUboSize            = 80;   // mat4(64) + vec2(8) + pad(8)
constexpr std::size_t kPaletteUboSize        = ezgl::kMaxOglStyleEntries * 4 * sizeof(float);

constexpr std::size_t kInitialPosBytes       = 1 * 1024 * 1024;
constexpr std::size_t kInitialStyleBytes     = 128 * 1024;
constexpr std::size_t kInitialThickBytes     = 512 * 1024;
constexpr std::size_t kInitialDashedBytes    = 512 * 1024;

// Maximum vertices / instances per VBO slot (arbitrary large power-of-two).
constexpr std::size_t kMaxPosPerSlot    = std::size_t(1) << 28;
constexpr std::size_t kMaxThickPerSlot  = std::size_t(1) << 28;
constexpr std::size_t kMaxDashedPerSlot = std::size_t(1) << 28;

// ---- GLSL shader sources ---------------------------------------------------

// Shared by thin lines (GL_LINES), fill rects (GL_TRIANGLES), draw outlines.
static const char* kBaseVert = R"GLSL(
#version 330 core
layout(location = 0) in vec2  inPosition;
layout(location = 1) in float inStyleNorm;

layout(std140) uniform MvpBlock {
    mat4 mvp;
    vec2 viewport;
    vec2 _pad;
} ubo;

out float v_style_index;

void main()
{
    gl_Position  = ubo.mvp * vec4(inPosition, 0.0, 1.0);
    v_style_index = inStyleNorm * 255.0;
}
)GLSL";

static const char* kBaseFrag = R"GLSL(
#version 330 core
in  float v_style_index;

layout(std140) uniform PaletteBlock { vec4 colors[256]; } palette;

out vec4 fragColor;

void main()
{
    int idx  = clamp(int(v_style_index + 0.5), 0, 255);
    fragColor = palette.colors[idx];
}
)GLSL";

// Thick solid lines — instanced GL_TRIANGLE_STRIP (4 quad corners × N instances).
static const char* kThickVert = R"GLSL(
#version 330 core
// Per-vertex (4 constant corners)
layout(location = 0) in vec2  inCorner;    // (t, side)
// Per-instance
layout(location = 1) in vec2  inStart;     // world-space start
layout(location = 2) in vec2  inEnd;       // world-space end
layout(location = 3) in float inWidthPx;   // full width in screen pixels
layout(location = 4) in float inStyleNorm; // palette index normalised 0-1

layout(std140) uniform MvpBlock {
    mat4 mvp;
    vec2 viewport;
    vec2 _pad;
} ubo;

out float v_style_index;

void main()
{
    float t    = inCorner.x;   // 0 or 1
    float side = inCorner.y;   // -1 or +1

    vec2 pos = mix(inStart, inEnd, t);

    // World perpendicular unit vector.
    vec2 dir     = inEnd - inStart;
    float dir_len = length(dir);
    vec2 perp = (dir_len > 1e-6) ? vec2(-dir.y, dir.x) / dir_len : vec2(0.0, 1.0);

    // Map world-space perp → screen pixels (mvp[0][0] = 2*sx/fw, etc.)
    vec2 screen_perp = vec2(
        perp.x * ubo.mvp[0][0] * (ubo.viewport.x * 0.5),
        perp.y * ubo.mvp[1][1] * (ubo.viewport.y * 0.5));
    float sp_len = length(screen_perp);
    if (sp_len > 1e-6)
        screen_perp /= sp_len;

    // Convert half-width in pixels → NDC offset.
    vec2 ndc_offset = side * screen_perp * (inWidthPx / ubo.viewport);

    vec4 clip = ubo.mvp * vec4(pos, 0.0, 1.0);
    clip.xy  += ndc_offset;
    gl_Position   = clip;

    v_style_index = inStyleNorm * 255.0;
}
)GLSL";

// Dashed lines — instanced GL_TRIANGLE_STRIP; fragment discards gap pixels.
static const char* kDashedVert = R"GLSL(
#version 330 core
// Per-vertex
layout(location = 0) in vec2  inCorner;      // (t, side)
// Per-instance
layout(location = 1) in vec2  inStart;
layout(location = 2) in vec2  inEnd;
layout(location = 3) in float inWidthPx;
layout(location = 4) in float inDashPx;
layout(location = 5) in float inGapPx;
layout(location = 6) in float inPhaseWorld;  // world-dist from original start to x0/y0
layout(location = 7) in float inStyleNorm;

layout(std140) uniform MvpBlock {
    mat4 mvp;
    vec2 viewport;
    vec2 _pad;
} ubo;

out  float v_style_index;
out  float v_t;
flat out float v_line_len_px;
flat out float v_dash_px;
flat out float v_gap_px;
flat out float v_phase_px;

void main()
{
    float t    = inCorner.x;
    float side = inCorner.y;

    vec2 pos     = mix(inStart, inEnd, t);
    vec2 dir     = inEnd - inStart;
    float dir_len = length(dir);
    vec2 perp = (dir_len > 1e-6) ? vec2(-dir.y, dir.x) / dir_len : vec2(0.0, 1.0);

    vec2 screen_perp = vec2(
        perp.x * ubo.mvp[0][0] * (ubo.viewport.x * 0.5),
        perp.y * ubo.mvp[1][1] * (ubo.viewport.y * 0.5));
    float sp_len = length(screen_perp);
    if (sp_len > 1e-6)
        screen_perp /= sp_len;

    vec2 ndc_offset  = side * screen_perp * (inWidthPx / ubo.viewport);
    vec4 clip        = ubo.mvp * vec4(pos, 0.0, 1.0);
    clip.xy         += ndc_offset;
    gl_Position      = clip;

    // Dash phase — convert world phase to screen pixels using current zoom.
    vec4 clip_start = ubo.mvp * vec4(inStart, 0.0, 1.0);
    vec4 clip_end   = ubo.mvp * vec4(inEnd,   0.0, 1.0);
    vec2 screen_delta = (clip_end.xy - clip_start.xy) * (ubo.viewport * 0.5);
    float line_len_px   = length(screen_delta);
    float line_len_world = length(inEnd - inStart);
    float phase_px = 0.0;
    if (line_len_world > 1e-6)
        phase_px = inPhaseWorld * (line_len_px / line_len_world);

    v_style_index = inStyleNorm * 255.0;
    v_t           = t;
    v_line_len_px = line_len_px;
    v_dash_px     = inDashPx;
    v_gap_px      = inGapPx;
    v_phase_px    = phase_px;
}
)GLSL";

static const char* kDashedFrag = R"GLSL(
#version 330 core
in  float v_style_index;
in  float v_t;
flat in float v_line_len_px;
flat in float v_dash_px;
flat in float v_gap_px;
flat in float v_phase_px;

layout(std140) uniform PaletteBlock { vec4 colors[256]; } palette;

out vec4 fragColor;

void main()
{
    float dist_px = v_phase_px + v_t * v_line_len_px;
    float period  = v_dash_px + v_gap_px;
    if (period > 1e-3) {
        float phase = mod(dist_px, period);
        if (phase >= v_dash_px)
            discard;
    }
    int idx   = clamp(int(v_style_index + 0.5), 0, 255);
    fragColor = palette.colors[idx];
}
)GLSL";

// ---- misc helpers ----------------------------------------------------------

bool rectanglesIntersect(const ezgl::rectangle& a, const ezgl::rectangle& b)
{
    return !(a.right()  < b.left()
          || a.left()   > b.right()
          || a.top()    < b.bottom()
          || a.bottom() > b.top());
}

struct PaletteEntry { float r, g, b, a; };

PaletteEntry unpackRgba(std::uint32_t rgba)
{
    constexpr float kScale = 1.0f / 255.0f;
    return {
        float((rgba >>  0) & 0xFF) * kScale,
        float((rgba >>  8) & 0xFF) * kScale,
        float((rgba >> 16) & 0xFF) * kScale,
        float((rgba >> 24) & 0xFF) * kScale
    };
}

} // anonymous namespace

namespace ezgl {

// ---- OglWidget implementation ----------------------------------------------

OglWidget::OglWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    // Request OpenGL 3.3 Core profile — available everywhere except very old HW.
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    setFormat(fmt);
}

OglWidget::~OglWidget()
{
    // GL objects must be deleted while the context is current.
    makeCurrent();

    if (m_gl) {
        m_gl->glDeleteProgram(m_base_prog);
        m_gl->glDeleteProgram(m_thick_prog);
        m_gl->glDeleteProgram(m_dashed_prog);

        GLuint vaos[3] = { m_vao_base, m_vao_thick, m_vao_dashed };
        m_gl->glDeleteVertexArrays(3, vaos);

        m_gl->glDeleteBuffers(1, &m_quad_corner_vbo);
        m_gl->glDeleteBuffers(1, &m_mvp_ubo);
        m_gl->glDeleteBuffers(1, &m_palette_ubo);

        auto freePool = [this](std::vector<GLuint>& v) {
            if (!v.empty())
                m_gl->glDeleteBuffers(GLsizei(v.size()), v.data());
            v.clear();
        };
        freePool(m_line_vbos);   freePool(m_line_style_vbos);
        freePool(m_fill_vbos);   freePool(m_fill_style_vbos);
        freePool(m_draw_vbos);   freePool(m_draw_style_vbos);
        freePool(m_thick_inst_vbos);  freePool(m_thick_style_vbos);
        freePool(m_dashed_inst_vbos); freePool(m_dashed_style_vbos);
    }

    doneCurrent();
}

// ---- public API ------------------------------------------------------------

void OglWidget::set_frame_data(std::vector<OglTileBatch>  tiles,
                                std::vector<std::uint32_t> palette_rgba,
                                const QMatrix4x4&          world_to_ndc,
                                const rectangle&           visible_world,
                                const QImage&              overlay,
                                QColor                     bg_color)
{
    m_pending_tiles        = std::move(tiles);
    m_pending_palette_rgba = std::move(palette_rgba);
    m_pending_mvp          = world_to_ndc;
    m_pending_visible_world = visible_world;
    m_pending_overlay      = overlay;
    m_pending_bg           = bg_color;
    m_frame_dirty          = true;
    m_mvp_dirty            = false;
}

void OglWidget::set_mvp_only(const QMatrix4x4& world_to_ndc,
                              const rectangle&  visible_world)
{
    m_pending_mvp           = world_to_ndc;
    m_pending_visible_world = visible_world;
    m_mvp_dirty             = true;
    // m_frame_dirty intentionally not set — vertex data is reused.
}

void OglWidget::set_mvp_and_overlay(const QMatrix4x4& world_to_ndc,
                                     const rectangle&  visible_world,
                                     const QImage&     overlay)
{
    m_pending_mvp           = world_to_ndc;
    m_pending_visible_world = visible_world;
    m_pending_overlay       = overlay;
    m_mvp_dirty             = true;
    // m_frame_dirty intentionally not set — tile vertex data is reused.
}

void OglWidget::setResizeCallback(std::function<void(int,int)> cb)
{
    m_resize_cb = std::move(cb);
}

void OglWidget::setPreResizeCallback(std::function<void()> cb)
{
    m_pre_resize_cb = std::move(cb);
}

// ---- QOpenGLWidget overrides -----------------------------------------------

void OglWidget::initializeGL()
{
    m_gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(
               QOpenGLContext::currentContext());
    if (!m_gl)
        qFatal("OglWidget: failed to obtain OpenGL 3.3 Core functions");
    m_gl->initializeOpenGLFunctions();

    // ---- Build shader programs ----------------------------------------------
    m_base_prog   = buildProgram(kBaseVert,   kBaseFrag);
    m_thick_prog  = buildProgram(kThickVert,  kBaseFrag);
    m_dashed_prog = buildProgram(kDashedVert, kDashedFrag);

    // Connect UBO binding points to each program.
    bindUboToProgram(m_base_prog,   "MvpBlock",     0);
    bindUboToProgram(m_base_prog,   "PaletteBlock", 1);
    bindUboToProgram(m_thick_prog,  "MvpBlock",     0);
    bindUboToProgram(m_thick_prog,  "PaletteBlock", 1);
    bindUboToProgram(m_dashed_prog, "MvpBlock",     0);
    bindUboToProgram(m_dashed_prog, "PaletteBlock", 1);

    // ---- Create UBOs --------------------------------------------------------
    m_gl->glGenBuffers(1, &m_mvp_ubo);
    m_gl->glBindBuffer(GL_UNIFORM_BUFFER, m_mvp_ubo);
    m_gl->glBufferData(GL_UNIFORM_BUFFER, kMvpUboSize, nullptr, GL_DYNAMIC_DRAW);

    m_gl->glGenBuffers(1, &m_palette_ubo);
    m_gl->glBindBuffer(GL_UNIFORM_BUFFER, m_palette_ubo);
    m_gl->glBufferData(GL_UNIFORM_BUFFER, kPaletteUboSize, nullptr, GL_DYNAMIC_DRAW);

    // Bind UBOs to their global binding points (persist for the lifetime of the context).
    m_gl->glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_mvp_ubo);
    m_gl->glBindBufferBase(GL_UNIFORM_BUFFER, 1, m_palette_ubo);

    // ---- Constant quad-corner buffer (static, uploaded once) ----------------
    static const OglQuadCorner kCorners[4] = {
        { 0.0f, -1.0f },   // t=start, left
        { 0.0f, +1.0f },   // t=start, right
        { 1.0f, -1.0f },   // t=end,   left
        { 1.0f, +1.0f },   // t=end,   right
    };
    m_gl->glGenBuffers(1, &m_quad_corner_vbo);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_quad_corner_vbo);
    m_gl->glBufferData(GL_ARRAY_BUFFER, sizeof(kCorners), kCorners, GL_STATIC_DRAW);

    // ---- Create VAOs and set immutable divisor state ------------------------

    GLuint vaos[3] = {0, 0, 0};
    m_gl->glGenVertexArrays(3, vaos);
    m_vao_base   = vaos[0];
    m_vao_thick  = vaos[1];
    m_vao_dashed = vaos[2];

    // Base VAO: locs 0 (pos) and 1 (style) — both per-vertex.
    m_gl->glBindVertexArray(m_vao_base);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glEnableVertexAttribArray(1);
    m_gl->glVertexAttribDivisor(0, 0);
    m_gl->glVertexAttribDivisor(1, 0);

    // Thick VAO: loc 0 per-vertex (corner); locs 1-4 per-instance.
    m_gl->glBindVertexArray(m_vao_thick);
    for (int i = 0; i <= 4; ++i)
        m_gl->glEnableVertexAttribArray(i);
    m_gl->glVertexAttribDivisor(0, 0); // corner — per vertex
    m_gl->glVertexAttribDivisor(1, 1); // start   — per instance
    m_gl->glVertexAttribDivisor(2, 1); // end
    m_gl->glVertexAttribDivisor(3, 1); // width_px
    m_gl->glVertexAttribDivisor(4, 1); // style

    // Dashed VAO: loc 0 per-vertex; locs 1-7 per-instance.
    m_gl->glBindVertexArray(m_vao_dashed);
    for (int i = 0; i <= 7; ++i)
        m_gl->glEnableVertexAttribArray(i);
    m_gl->glVertexAttribDivisor(0, 0); // corner
    for (int i = 1; i <= 7; ++i)
        m_gl->glVertexAttribDivisor(i, 1);

    m_gl->glBindVertexArray(0);

    m_initialized = true;
}

void OglWidget::resizeGL(int /*w*/, int /*h*/)
{
    // Viewport is set fresh each paintGL() call; nothing to do here.
}

void OglWidget::paintGL()
{
    if (!m_initialized)
        return;
    if (!m_frame_dirty && !m_mvp_dirty)
        return;

    const bool geom_dirty = m_frame_dirty;
    m_frame_dirty = false;
    m_mvp_dirty   = false;

    const auto frame_start = std::chrono::steady_clock::now();

    // ---- Upload MVP UBO -----------------------------------------------------
    struct alignas(16) MvpUboData {
        float mvp[16];        // column-major mat4, 64 bytes
        float viewport[2];    // vec2, 8 bytes
        float pad[2];         // vec2 padding, 8 bytes
    };
    static_assert(sizeof(MvpUboData) == 80);

    MvpUboData mvp_data{};
    std::memcpy(mvp_data.mvp, m_pending_mvp.constData(), 64);
    mvp_data.viewport[0] = float(width());
    mvp_data.viewport[1] = float(height());

    m_gl->glBindBuffer(GL_UNIFORM_BUFFER, m_mvp_ubo);
    m_gl->glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(mvp_data), &mvp_data);

    // ---- Upload geometry + palette on geometry-dirty frames -----------------
    if (geom_dirty) {
        // Validate incoming batch consistency.
        for (const OglTileBatch& tile : m_pending_tiles) {
            if (tile.line_styles.size()           != tile.line_verts.size()
                || tile.fill_styles.size()        != tile.fill_verts.size()
                || tile.draw_styles.size()        != tile.draw_verts.size()
                || tile.thick_line_styles.size()  != tile.thick_line_instances.size()
                || tile.dashed_line_styles.size() != tile.dashed_line_instances.size()) {
                qFatal("OglWidget: style-stream size mismatch with vertex stream");
            }
        }

        // ---- Plan upload chunks (identical logic to rhi_canvas_widget) ------
        struct PendingUpload {
            std::size_t  slot;
            std::size_t  pos_offset;    // byte offset into pos VBO
            std::size_t  style_offset;  // byte offset into style VBO
            std::size_t  count;
            const void*  pos_data;
            const OglStyleIndex* style_data;
            std::size_t  elem_size;     // sizeof one position element
        };

        std::vector<std::size_t> line_slots, fill_slots, draw_slots;
        std::vector<std::size_t> thick_slots, dashed_slots;
        std::vector<PendingUpload> line_uploads, fill_uploads, draw_uploads;
        std::vector<PendingUpload> thick_uploads, dashed_uploads;

        m_gpu_tiles.resize(m_pending_tiles.size());

        // planStream: distribute tile data across VBO slots, building StreamChunk
        // records for the GPU tile and PendingUpload records for the upload pass.
        auto planStream = [](
            std::vector<StreamChunk>&   chunks,
            std::vector<PendingUpload>& uploads,
            std::vector<std::size_t>&   slot_counts,
            const auto&                 verts,
            const auto&                 styles,
            std::size_t                 max_per_slot,
            std::size_t                 elem_size)
        {
            chunks.clear();
            for (std::size_t begin = 0; begin < verts.size(); ) {
                if (slot_counts.empty() || slot_counts.back() >= max_per_slot)
                    slot_counts.push_back(0);

                const std::size_t slot         = slot_counts.size() - 1;
                const std::size_t slot_offset  = slot_counts.back();
                const std::size_t count =
                    std::min(verts.size() - begin, max_per_slot - slot_offset);

                const std::size_t pos_offset   = slot_offset * elem_size;
                const std::size_t style_offset = slot_offset * sizeof(OglStyleIndex);

                chunks.push_back(StreamChunk{
                    quint32(slot),
                    quint32(pos_offset),
                    quint32(style_offset),
                    quint32(count)
                });
                uploads.push_back(PendingUpload{
                    slot, pos_offset, style_offset, count,
                    static_cast<const void*>(verts.data() + begin),
                    styles.data() + begin,
                    elem_size
                });
                slot_counts.back() += count;
                begin += count;
            }
        };

        for (std::size_t i = 0; i < m_pending_tiles.size(); ++i) {
            const OglTileBatch& tile = m_pending_tiles[i];
            GpuTileBatch& gpu        = m_gpu_tiles[i];
            gpu.world_bounds = tile.world_bounds;

            planStream(gpu.line_chunks,   line_uploads,   line_slots,
                       tile.line_verts,   tile.line_styles,
                       kMaxPosPerSlot,    sizeof(OglPosVertex));
            planStream(gpu.fill_chunks,   fill_uploads,   fill_slots,
                       tile.fill_verts,   tile.fill_styles,
                       kMaxPosPerSlot,    sizeof(OglPosVertex));
            planStream(gpu.draw_chunks,   draw_uploads,   draw_slots,
                       tile.draw_verts,   tile.draw_styles,
                       kMaxPosPerSlot,    sizeof(OglPosVertex));
            planStream(gpu.thick_chunks,  thick_uploads,  thick_slots,
                       tile.thick_line_instances, tile.thick_line_styles,
                       kMaxThickPerSlot,  sizeof(OglThickLineInstance));
            planStream(gpu.dashed_chunks, dashed_uploads, dashed_slots,
                       tile.dashed_line_instances, tile.dashed_line_styles,
                       kMaxDashedPerSlot, sizeof(OglDashedLineInstance));
        }

        // Ensure VBOs large enough then trim excess slots.
        auto resizePool = [this](std::vector<GLuint>&      vbos,
                                 std::vector<std::size_t>& sizes,
                                 const std::vector<std::size_t>& slot_counts,
                                 std::size_t elem_size,
                                 std::size_t initial_bytes)
        {
            for (std::size_t s = 0; s < slot_counts.size(); ++s)
                ensureVbo(vbos, sizes, s, slot_counts[s] * elem_size, initial_bytes);
            // Release VBOs beyond what this frame needs.
            for (std::size_t s = slot_counts.size(); s < vbos.size(); ++s) {
                if (vbos[s]) {
                    m_gl->glDeleteBuffers(1, &vbos[s]);
                    vbos[s] = 0;
                    sizes[s] = 0;
                }
            }
            if (slot_counts.size() < vbos.size()) {
                vbos.resize(slot_counts.size());
                sizes.resize(slot_counts.size());
            }
        };

        auto resizeStylePool = [this](std::vector<GLuint>&      vbos,
                                      std::vector<std::size_t>& sizes,
                                      const std::vector<std::size_t>& slot_counts)
        {
            for (std::size_t s = 0; s < slot_counts.size(); ++s)
                ensureVbo(vbos, sizes, s,
                          slot_counts[s] * sizeof(OglStyleIndex),
                          kInitialStyleBytes);
            for (std::size_t s = slot_counts.size(); s < vbos.size(); ++s) {
                if (vbos[s]) { m_gl->glDeleteBuffers(1, &vbos[s]); vbos[s] = 0; sizes[s] = 0; }
            }
            if (slot_counts.size() < vbos.size()) {
                vbos.resize(slot_counts.size());
                sizes.resize(slot_counts.size());
            }
        };

        resizePool(m_line_vbos,    m_line_vbo_bytes,    line_slots,   sizeof(OglPosVertex),           kInitialPosBytes);
        resizePool(m_fill_vbos,    m_fill_vbo_bytes,    fill_slots,   sizeof(OglPosVertex),           kInitialPosBytes);
        resizePool(m_draw_vbos,    m_draw_vbo_bytes,    draw_slots,   sizeof(OglPosVertex),           kInitialPosBytes);
        resizePool(m_thick_inst_vbos,  m_thick_inst_bytes,  thick_slots,  sizeof(OglThickLineInstance),  kInitialThickBytes);
        resizePool(m_dashed_inst_vbos, m_dashed_inst_bytes, dashed_slots, sizeof(OglDashedLineInstance), kInitialDashedBytes);

        resizeStylePool(m_line_style_vbos,   m_line_style_vbo_bytes,   line_slots);
        resizeStylePool(m_fill_style_vbos,   m_fill_style_vbo_bytes,   fill_slots);
        resizeStylePool(m_draw_style_vbos,   m_draw_style_vbo_bytes,   draw_slots);
        resizeStylePool(m_thick_style_vbos,  m_thick_style_bytes,      thick_slots);
        resizeStylePool(m_dashed_style_vbos, m_dashed_style_bytes,     dashed_slots);

        // Upload geometry data.
        auto doUploads = [this](
            std::vector<GLuint>& pos_vbos,
            std::vector<GLuint>& style_vbos,
            const std::vector<PendingUpload>& uploads)
        {
            for (const PendingUpload& u : uploads) {
                uploadVbo(pos_vbos,   u.slot, u.pos_offset,
                          u.pos_data, u.count * u.elem_size);
                uploadVbo(style_vbos, u.slot, u.style_offset,
                          u.style_data, u.count * sizeof(OglStyleIndex));
            }
        };

        doUploads(m_line_vbos,       m_line_style_vbos,   line_uploads);
        doUploads(m_fill_vbos,       m_fill_style_vbos,   fill_uploads);
        doUploads(m_draw_vbos,       m_draw_style_vbos,   draw_uploads);
        doUploads(m_thick_inst_vbos, m_thick_style_vbos,  thick_uploads);
        doUploads(m_dashed_inst_vbos,m_dashed_style_vbos, dashed_uploads);

        // Upload palette UBO.
        if (m_pending_palette_rgba.size() > kMaxOglStyleEntries)
            qFatal("OglWidget: palette size %zu exceeds %zu",
                   m_pending_palette_rgba.size(), kMaxOglStyleEntries);

        struct PaletteUboData { PaletteEntry colors[256]; };
        PaletteUboData palette_data{};
        const std::size_t n = std::min(m_pending_palette_rgba.size(), kMaxOglStyleEntries);
        for (std::size_t i = 0; i < n; ++i)
            palette_data.colors[i] = unpackRgba(m_pending_palette_rgba[i]);

        m_gl->glBindBuffer(GL_UNIFORM_BUFFER, m_palette_ubo);
        m_gl->glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(palette_data), &palette_data);
    }

    // ---- Render pass --------------------------------------------------------
    m_gl->glViewport(0, 0, width(), height());

    m_gl->glClearColor(m_pending_bg.redF(), m_pending_bg.greenF(),
                       m_pending_bg.blueF(), m_pending_bg.alphaF());
    m_gl->glClear(GL_COLOR_BUFFER_BIT);

    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                               GL_ONE,      GL_ONE_MINUS_SRC_ALPHA);
    m_gl->glDisable(GL_DEPTH_TEST);

    std::size_t visible_tiles = 0;
    for (const GpuTileBatch& tile : m_gpu_tiles) {
        if (!rectanglesIntersect(tile.world_bounds, m_pending_visible_world))
            continue;
        ++visible_tiles;

        // Draw order: fill → outline → thin line → thick → dashed.
        drawChunksBase(GL_TRIANGLES, m_fill_vbos, m_fill_style_vbos, tile.fill_chunks);
        drawChunksBase(GL_LINES,     m_draw_vbos, m_draw_style_vbos, tile.draw_chunks);
        drawChunksBase(GL_LINES,     m_line_vbos, m_line_style_vbos, tile.line_chunks);
        drawChunksThick(tile.thick_chunks);
        drawChunksDashed(tile.dashed_chunks);
    }

    m_gl->glBindVertexArray(0);

    const auto frame_end = std::chrono::steady_clock::now();
    const double frame_ms =
        std::chrono::duration<double, std::milli>(frame_end - frame_start).count();

    g_debug("OGL paintGL() %.3f ms (geom_dirty=%d, tiles=%zu, visible=%zu)",
            frame_ms, int(geom_dirty), m_gpu_tiles.size(), visible_tiles);
}

void OglWidget::paintEvent(QPaintEvent* e)
{
    // Renders GL content (calls paintGL internally).
    QOpenGLWidget::paintEvent(e);

    // Composite QPainter overlay (text, arcs, polygons) on top.
    if (!m_pending_overlay.isNull()) {
        QPainter p(this);
        p.drawImage(rect(), m_pending_overlay, m_pending_overlay.rect());
    }
}

void OglWidget::resizeEvent(QResizeEvent* e)
{
    if (m_pre_resize_cb)
        m_pre_resize_cb();
    QOpenGLWidget::resizeEvent(e);
    if (width() > 0 && height() > 0 && m_resize_cb)
        m_resize_cb(width(), height());
}

// ---- private helpers -------------------------------------------------------

GLuint OglWidget::buildProgram(const char* vs_src, const char* fs_src)
{
    auto compile = [this](GLenum type, const char* src) -> GLuint {
        GLuint shader = m_gl->glCreateShader(type);
        m_gl->glShaderSource(shader, 1, &src, nullptr);
        m_gl->glCompileShader(shader);
        GLint ok = 0;
        m_gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048];
            m_gl->glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            qFatal("OglWidget shader compile error:\n%s", log);
        }
        return shader;
    };

    GLuint vs = compile(GL_VERTEX_SHADER,   vs_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src);

    GLuint prog = m_gl->glCreateProgram();
    m_gl->glAttachShader(prog, vs);
    m_gl->glAttachShader(prog, fs);
    m_gl->glLinkProgram(prog);

    GLint ok = 0;
    m_gl->glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        m_gl->glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        qFatal("OglWidget shader link error:\n%s", log);
    }

    m_gl->glDeleteShader(vs);
    m_gl->glDeleteShader(fs);
    return prog;
}

void OglWidget::bindUboToProgram(GLuint prog, const char* block_name, GLuint binding)
{
    const GLuint idx = m_gl->glGetUniformBlockIndex(prog, block_name);
    if (idx != GL_INVALID_INDEX)
        m_gl->glUniformBlockBinding(prog, idx, binding);
}

void OglWidget::ensureVbo(std::vector<GLuint>&      vbos,
                           std::vector<std::size_t>& sizes,
                           std::size_t               slot,
                           std::size_t               needed_bytes,
                           std::size_t               initial_bytes)
{
    if (slot >= vbos.size()) {
        vbos.resize(slot + 1, 0);
        sizes.resize(slot + 1, 0);
    }
    if (sizes[slot] >= needed_bytes)
        return;

    // Grow by doubling.
    std::size_t new_size = sizes[slot] > 0 ? sizes[slot] : initial_bytes;
    while (new_size < needed_bytes)
        new_size *= 2;

    if (vbos[slot])
        m_gl->glDeleteBuffers(1, &vbos[slot]);

    m_gl->glGenBuffers(1, &vbos[slot]);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, vbos[slot]);
    m_gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(new_size), nullptr, GL_DYNAMIC_DRAW);
    sizes[slot] = new_size;
}

void OglWidget::uploadVbo(std::vector<GLuint>& vbos,
                           std::size_t          slot,
                           std::size_t          byte_offset,
                           const void*          data,
                           std::size_t          byte_count)
{
    if (byte_count == 0)
        return;
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, vbos[slot]);
    m_gl->glBufferSubData(GL_ARRAY_BUFFER,
                          GLintptr(byte_offset),
                          GLsizeiptr(byte_count),
                          data);
}

void OglWidget::drawChunksBase(GLenum                         topology,
                                std::vector<GLuint>&           pos_vbos,
                                std::vector<GLuint>&           style_vbos,
                                const std::vector<StreamChunk>& chunks)
{
    if (chunks.empty())
        return;

    m_gl->glUseProgram(m_base_prog);
    m_gl->glBindVertexArray(m_vao_base);

    for (const StreamChunk& c : chunks) {
        if (c.count == 0)
            continue;

        // Bind position VBO to attrib location 0.
        m_gl->glBindBuffer(GL_ARRAY_BUFFER, pos_vbos[c.vbo_index]);
        m_gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                                    sizeof(OglPosVertex),
                                    reinterpret_cast<void*>(uintptr_t(c.pos_offset)));

        // Bind style VBO to attrib location 1 (normalised UByte → float).
        m_gl->glBindBuffer(GL_ARRAY_BUFFER, style_vbos[c.vbo_index]);
        m_gl->glVertexAttribPointer(1, 1, GL_UNSIGNED_BYTE, GL_TRUE,
                                    sizeof(OglStyleIndex),
                                    reinterpret_cast<void*>(uintptr_t(c.style_offset)));

        m_gl->glDrawArrays(topology, 0, GLsizei(c.count));
    }
}

void OglWidget::drawChunksThick(const std::vector<StreamChunk>& chunks)
{
    if (chunks.empty())
        return;

    m_gl->glUseProgram(m_thick_prog);
    m_gl->glBindVertexArray(m_vao_thick);

    for (const StreamChunk& c : chunks) {
        if (c.count == 0)
            continue;

        // Loc 0: constant quad corner buffer (per-vertex).
        m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_quad_corner_vbo);
        m_gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                                    sizeof(OglQuadCorner), nullptr);

        // Locs 1-3: ThickLineInstance fields (per-instance).
        m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_thick_inst_vbos[c.vbo_index]);
        const auto* base = reinterpret_cast<void*>(uintptr_t(c.pos_offset));
        m_gl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                                    sizeof(OglThickLineInstance),
                                    static_cast<const char*>(base) + offsetof(OglThickLineInstance, x0));
        m_gl->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                                    sizeof(OglThickLineInstance),
                                    static_cast<const char*>(base) + offsetof(OglThickLineInstance, x1));
        m_gl->glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE,
                                    sizeof(OglThickLineInstance),
                                    static_cast<const char*>(base) + offsetof(OglThickLineInstance, width_px));

        // Loc 4: style (per-instance, normalised UByte).
        m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_thick_style_vbos[c.vbo_index]);
        m_gl->glVertexAttribPointer(4, 1, GL_UNSIGNED_BYTE, GL_TRUE,
                                    sizeof(OglStyleIndex),
                                    reinterpret_cast<void*>(uintptr_t(c.style_offset)));

        // 4 quad corners, c.count instances.
        m_gl->glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, GLsizei(c.count));
    }
}

void OglWidget::drawChunksDashed(const std::vector<StreamChunk>& chunks)
{
    if (chunks.empty())
        return;

    m_gl->glUseProgram(m_dashed_prog);
    m_gl->glBindVertexArray(m_vao_dashed);

    for (const StreamChunk& c : chunks) {
        if (c.count == 0)
            continue;

        // Loc 0: constant quad corner buffer.
        m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_quad_corner_vbo);
        m_gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                                    sizeof(OglQuadCorner), nullptr);

        // Locs 1-6: DashedLineInstance fields (per-instance).
        m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_dashed_inst_vbos[c.vbo_index]);
        const auto* base = reinterpret_cast<void*>(uintptr_t(c.pos_offset));
        m_gl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                                    sizeof(OglDashedLineInstance),
                                    static_cast<const char*>(base) + offsetof(OglDashedLineInstance, x0));
        m_gl->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                                    sizeof(OglDashedLineInstance),
                                    static_cast<const char*>(base) + offsetof(OglDashedLineInstance, x1));
        m_gl->glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE,
                                    sizeof(OglDashedLineInstance),
                                    static_cast<const char*>(base) + offsetof(OglDashedLineInstance, width_px));
        m_gl->glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE,
                                    sizeof(OglDashedLineInstance),
                                    static_cast<const char*>(base) + offsetof(OglDashedLineInstance, dash_px));
        m_gl->glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE,
                                    sizeof(OglDashedLineInstance),
                                    static_cast<const char*>(base) + offsetof(OglDashedLineInstance, gap_px));
        m_gl->glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE,
                                    sizeof(OglDashedLineInstance),
                                    static_cast<const char*>(base) + offsetof(OglDashedLineInstance, phase_world));

        // Loc 7: style (per-instance, normalised UByte).
        m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_dashed_style_vbos[c.vbo_index]);
        m_gl->glVertexAttribPointer(7, 1, GL_UNSIGNED_BYTE, GL_TRUE,
                                    sizeof(OglStyleIndex),
                                    reinterpret_cast<void*>(uintptr_t(c.style_offset)));

        m_gl->glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, GLsizei(c.count));
    }
}

} // namespace ezgl

#endif // EZGL_QT && EZGL_OGL
