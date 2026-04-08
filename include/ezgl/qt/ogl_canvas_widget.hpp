#pragma once

#if defined(EZGL_QT) && defined(EZGL_OGL)

#include "ezgl/rectangle.hpp"

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QImage>
#include <QMatrix4x4>
#include <QColor>
#include <cstdint>
#include <functional>
#include <vector>

namespace ezgl {

/**
 * Packed GPU vertex: world-space position only.
 * Layout: float x, y (8 bytes).
 */
struct OglPosVertex {
    float x, y;
};
static_assert(sizeof(OglPosVertex) == 8, "OglPosVertex must be 8 bytes");

/**
 * One corner of the thick-line quad template (4 total, static buffer).
 *   t    : 0.0 = at line start,  1.0 = at line end
 *   side : -1.0 = left edge,    +1.0 = right edge
 */
struct OglQuadCorner {
    float t;
    float side;
};
static_assert(sizeof(OglQuadCorner) == 8, "OglQuadCorner must be 8 bytes");

/**
 * Per-instance data for one thick solid line segment (instanced rendering).
 * 20 bytes per line (7× less than a per-vertex approach at 6 verts × 24 B).
 */
struct OglThickLineInstance {
    float x0, y0;   // world-space start
    float x1, y1;   // world-space end
    float width_px; // full line width in screen pixels
};
static_assert(sizeof(OglThickLineInstance) == 20, "OglThickLineInstance must be 20 bytes");

/**
 * Per-instance data for one dashed line segment (instanced rendering).
 * 32 bytes per line. Fragment shader discards gap fragments.
 */
struct OglDashedLineInstance {
    float x0, y0;       // world-space start
    float x1, y1;       // world-space end
    float width_px;     // full line width in pixels (>= 1)
    float dash_px;      // dash run length in screen pixels
    float gap_px;       // gap length in screen pixels
    float phase_world;  // world-distance from original line start to x0/y0
};
static_assert(sizeof(OglDashedLineInstance) == 32, "OglDashedLineInstance must be 32 bytes");

// Compact style index per primitive. The fragment shader resolves it via
// the palette UBO (binding 1), avoiding one draw call per colour run.
using OglStyleIndex = std::uint8_t;
static constexpr std::size_t kMaxOglStyleEntries = 256;

/**
 * CPU-side geometry for one spatial tile.
 * Mirrors RhiTileBatch exactly (same field names and memory layout of
 * each element), so ogl_renderer.cpp is a near-copy of rhi_renderer.cpp.
 */
struct OglTileBatch {
    rectangle                       world_bounds;
    // Thin (1-pixel) lines — GL_LINES
    std::vector<OglPosVertex>       line_verts;
    std::vector<OglStyleIndex>      line_styles;
    // Filled rectangles — GL_TRIANGLES
    std::vector<OglPosVertex>       fill_verts;
    std::vector<OglStyleIndex>      fill_styles;
    // draw_rectangle outlines (thin) — GL_LINES
    std::vector<OglPosVertex>       draw_verts;
    std::vector<OglStyleIndex>      draw_styles;
    // Thick solid lines — instanced GL_TRIANGLE_STRIP
    std::vector<OglThickLineInstance>  thick_line_instances;
    std::vector<OglStyleIndex>         thick_line_styles;
    // Dashed lines — instanced GL_TRIANGLE_STRIP
    std::vector<OglDashedLineInstance> dashed_line_instances;
    std::vector<OglStyleIndex>         dashed_line_styles;

    bool empty() const
    {
        return line_verts.empty()
            && fill_verts.empty()
            && draw_verts.empty()
            && thick_line_instances.empty()
            && dashed_line_instances.empty();
    }
};

/**
 * QOpenGLWidget subclass that renders ezgl geometry using OpenGL 3.3 Core.
 *
 * Public API mirrors RhiCanvasWidget so canvas.cpp / ogl_renderer can use
 * either backend with minimal #ifdef nesting.
 *
 * Geometry streams are uploaded to GPU VBOs inside paintGL().  The overlay
 * QImage (text, arcs drawn by QPainter) is composited on top in paintEvent().
 *
 * Threading: QOpenGLWidget renders on the GUI thread — no mutex needed.
 */
class OglWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit OglWidget(QWidget* parent = nullptr);
    ~OglWidget() override;

    /**
     * Push a full frame (geometry + MVP).  Call before update().
     * All GL uploads happen lazily inside the next paintGL().
     */
    void set_frame_data(std::vector<OglTileBatch>  tiles,
                        std::vector<std::uint32_t> palette_rgba,
                        const QMatrix4x4&          world_to_ndc,
                        const rectangle&           visible_world,
                        const QImage&              overlay,
                        QColor                     bg_color);

    /**
     * Update only the camera MVP — no geometry re-upload.
     * Call after pan/zoom when primitives have not changed.
     */
    void set_mvp_only(const QMatrix4x4& world_to_ndc,
                      const rectangle&  visible_world);

    void setResizeCallback(std::function<void(int,int)> cb);
    void setPreResizeCallback(std::function<void()>     cb);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL()    override;
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    // ---- GPU resource handles (valid only when m_initialized) ---------------

    QOpenGLFunctions_3_3_Core* m_gl = nullptr;

    // Shader programs:
    //   m_base_prog   — thin lines (GL_LINES) + fill (GL_TRIANGLES)
    //   m_thick_prog  — thick instanced quads
    //   m_dashed_prog — dashed instanced quads
    GLuint m_base_prog   = 0;
    GLuint m_thick_prog  = 0;
    GLuint m_dashed_prog = 0;

    // VAOs — one per pipeline; divisors set once in initializeGL().
    GLuint m_vao_base   = 0;
    GLuint m_vao_thick  = 0;
    GLuint m_vao_dashed = 0;

    // Constant 4-vertex quad buffer shared by thick and dashed pipelines.
    GLuint m_quad_corner_vbo = 0;

    // UBOs (bound once; updated each frame via glBufferSubData).
    GLuint m_mvp_ubo     = 0;   // 80 bytes: mat4 + vec2 + pad
    GLuint m_palette_ubo = 0;   // 4096 bytes: vec4[256]

    // Per-stream VBO pools (grow by doubling).
    std::vector<GLuint>       m_line_vbos,          m_line_style_vbos;
    std::vector<std::size_t>  m_line_vbo_bytes,     m_line_style_vbo_bytes;
    std::vector<GLuint>       m_fill_vbos,          m_fill_style_vbos;
    std::vector<std::size_t>  m_fill_vbo_bytes,     m_fill_style_vbo_bytes;
    std::vector<GLuint>       m_draw_vbos,          m_draw_style_vbos;
    std::vector<std::size_t>  m_draw_vbo_bytes,     m_draw_style_vbo_bytes;
    std::vector<GLuint>       m_thick_inst_vbos,    m_thick_style_vbos;
    std::vector<std::size_t>  m_thick_inst_bytes,   m_thick_style_bytes;
    std::vector<GLuint>       m_dashed_inst_vbos,   m_dashed_style_vbos;
    std::vector<std::size_t>  m_dashed_inst_bytes,  m_dashed_style_bytes;

    bool m_initialized = false;

    // ---- Pending frame (set_frame_data / set_mvp_only → paintGL) -----------

    std::vector<OglTileBatch>  m_pending_tiles;
    std::vector<std::uint32_t> m_pending_palette_rgba;
    QMatrix4x4                 m_pending_mvp;
    rectangle                  m_pending_visible_world;
    QImage                     m_pending_overlay;
    QColor                     m_pending_bg { Qt::white };
    bool                       m_frame_dirty = false;
    bool                       m_mvp_dirty   = false;

    // ---- Tiled GPU state (reused on MVP-only frames) ------------------------

    struct StreamChunk {
        quint32 vbo_index    = 0;
        quint32 pos_offset   = 0;  // byte offset within the VBO
        quint32 style_offset = 0;
        quint32 count        = 0;  // vertex or instance count
    };

    struct GpuTileBatch {
        rectangle                world_bounds;
        std::vector<StreamChunk> line_chunks;
        std::vector<StreamChunk> fill_chunks;
        std::vector<StreamChunk> draw_chunks;
        std::vector<StreamChunk> thick_chunks;
        std::vector<StreamChunk> dashed_chunks;
    };

    std::vector<GpuTileBatch> m_gpu_tiles;

    // ---- Canvas callbacks ---------------------------------------------------

    std::function<void(int,int)> m_resize_cb;
    std::function<void()>        m_pre_resize_cb;

    // ---- Private helpers ----------------------------------------------------

    GLuint buildProgram(const char* vs_src, const char* fs_src);

    void bindUboToProgram(GLuint prog, const char* block_name, GLuint binding);

    /** Grow VBO at slot if too small; no-op if already large enough. */
    void ensureVbo(std::vector<GLuint>&      vbos,
                   std::vector<std::size_t>& sizes,
                   std::size_t               slot,
                   std::size_t               needed_bytes,
                   std::size_t               initial_bytes);

    /** Upload data into VBO slot at byte_offset. VBO must already be large enough. */
    void uploadVbo(std::vector<GLuint>& vbos,
                   std::size_t          slot,
                   std::size_t          byte_offset,
                   const void*          data,
                   std::size_t          byte_count);

    /** Delete and clear a VBO pool. */
    void freeVboPool(std::vector<GLuint>&      vbos,
                     std::vector<std::size_t>& sizes);

    /** Draw thin-line/fill/draw chunks using m_base_prog. */
    void drawChunksBase(GLenum                        topology,
                        std::vector<GLuint>&           pos_vbos,
                        std::vector<GLuint>&           style_vbos,
                        const std::vector<StreamChunk>& chunks);

    /** Draw thick instanced chunks using m_thick_prog. */
    void drawChunksThick(const std::vector<StreamChunk>& chunks);

    /** Draw dashed instanced chunks using m_dashed_prog. */
    void drawChunksDashed(const std::vector<StreamChunk>& chunks);
};

} // namespace ezgl

#endif // EZGL_QT && EZGL_OGL
