# RHI → OpenGL Re-implementation Plan

## Goal

Replace the Qt RHI backend (`RhiCanvasWidget` + `rhi_renderer`) with a native
OpenGL 3.3 Core backend (`OglWidget` + `ogl_renderer`), keeping every
performance optimisation that the RHI path has:

| Optimisation | RHI | OGL |
|---|---|---|
| 256×256 spatial tile grid | ✓ | ✓ |
| Style-palette UBO (256 entries, 1 byte/vertex) | ✓ | ✓ |
| Instanced thick lines (20 B/line instead of 144 B) | ✓ | ✓ |
| Instanced dashed lines (32 B/line) | ✓ | ✓ |
| MVP-only update path (pan/zoom, no geometry re-upload) | ✓ | ✓ |
| Visible-tile culling | ✓ | ✓ |
| Draw-order: fill → outline → line | ✓ | ✓ |
| QPainter overlay composited on top | ✓ | ✓ |

**Advantage over RHI**: works on Qt ≥ 6.2 (any Qt6 with OpenGL support),
no Qt 6.7 requirement; no pre-compiled `.qsb` shader blobs; shaders are
embedded as GLSL string literals compiled at runtime.

---

## Architecture

```
┌──────────────────────────────────────────────────┐
│  canvas::redraw()  [GUI thread]                  │
│    ogl_renderer::begin_frame()                   │
│    user draw_callback(ogl_renderer*)             │
│       └─ draw_line / fill_rect / draw_rect …     │
│           └─ append to OglTileBatch geometry     │
│    ogl_renderer::flush()                         │
│       └─ OglWidget::set_frame_data(...)          │
│       └─ OglWidget::update()  →  paintGL()       │
└──────────────────────────────────────────────────┘

canvas::redraw_camera_only()
  └─ ogl_renderer::flush_mvp_only()
       └─ OglWidget::set_mvp_only(...)
       └─ OglWidget::update()  →  paintGL() [MVP only]
```

---

## GPU Pipeline Layout

### Shared data structs (defined in `ogl_canvas_widget.hpp`)

```
OglPosVertex        8 B  — float x, y
OglQuadCorner       8 B  — float t, side  (constant 4-vertex buffer)
OglThickLineInstance 20 B — float x0,y0,x1,y1,width_px
OglDashedLineInstance 32 B — float x0,y0,x1,y1,width_px,dash_px,gap_px,phase_world
OglStyleIndex       1 B  — uint8_t (normalised to 0-1 by glVertexAttribPointer GL_TRUE)
```

### Programs and pipelines

| Program | Topology | Vertex attribs | Per-vertex / Per-instance |
|---|---|---|---|
| `m_base_prog` | `GL_LINES` or `GL_TRIANGLES` (caller decides) | loc 0: pos (Float2), loc 1: styleNorm (UByte-norm) | both per-vertex |
| `m_thick_prog` | `GL_TRIANGLE_STRIP` (instanced) | 0: corner, 1-3: start/end/width (inst), 4: style (inst) | 0 per-vertex; 1-4 per-instance |
| `m_dashed_prog` | `GL_TRIANGLE_STRIP` (instanced) | 0: corner (vtx), 1-7: instance fields | 0 per-vertex; 1-7 per-instance |

### VAOs

Three VAOs initialised once in `initializeGL()`, each with the correct
`glVertexAttribDivisor` set permanently:

- `m_vao_base`   — divisors all 0
- `m_vao_thick`  — loc 0 div=0; locs 1-4 div=1
- `m_vao_dashed` — loc 0 div=0; locs 1-7 div=1

For each draw call, the VAO is bound and `glVertexAttribPointer` re-points
the attributes to the current chunk's VBO + byte offset.

### UBOs (binding points shared by all programs)

```
Binding 0: MvpBlock   — mat4 mvp (64B) + vec2 viewport (8B) + vec2 pad (8B) = 80B
Binding 1: PaletteBlock — vec4 colors[256] = 4096B
```

Binding point connections are established once per program via
`glUniformBlockBinding`.

---

## Shaders (GLSL 330 core, embedded as string literals)

### base.vert  (shared by fill/outline/line)
- `inPosition` → MVP transform → `gl_Position`
- `inStyleNorm` (UByte normalised) × 255 → `v_style_index`

### base.frag
- Palette lookup: `palette.colors[clamp(int(v_style_index + 0.5), 0, 255)]`

### thick_line.vert
- Same perpendicular quad expansion as `shaders/thick_line.vert` (GLSL 440)
- Uses `mvp[0][0]` and `mvp[1][1]` to map world perp → screen pixels → NDC

### dashed_line.vert
- Same as thick_line.vert plus screen-pixel line length and phase varyings
- Flat varyings: `v_line_len_px`, `v_dash_px`, `v_gap_px`, `v_phase_px`

### dashed_line.frag
- `dist_px = v_phase_px + v_t * v_line_len_px`
- `if (mod(dist_px, period) >= v_dash_px) discard;`

---

## VBO Management

Same grow-by-doubling strategy as the RHI path:

```
ensureVbo(vbos, sizes, slot, needed_bytes, initial_bytes)
  → if too small: glDeleteBuffers + glGenBuffers + glBufferData(nullptr, new_size, DYNAMIC_DRAW)
uploadVbo(vbos, slot, byte_offset, data, byte_count)
  → glBufferSubData
```

The chunk scheme (`StreamChunk { vbo_index, pos_offset, style_offset, count }`)
and per-tile `GpuTileBatch` are identical to the RHI path.

Quad-corner VBO is `GL_STATIC_DRAW` (uploaded once, never changes).

---

## Threading

`QOpenGLWidget` renders on the GUI thread (unlike `QRhiWidget`'s render
thread), so **no mutex is needed** in `set_frame_data` / `set_mvp_only`.
GL calls happen only inside `paintGL()` and `initializeGL()`, which Qt
ensures are called with `makeCurrent()` active.

Destructor calls `makeCurrent()` + `doneCurrent()` around `glDeleteBuffers`
and `glDeleteProgram`.

---

## Files to Create

| File | Purpose |
|---|---|
| `include/ezgl/qt/ogl_canvas_widget.hpp` | `OglWidget` (QOpenGLWidget subclass) + data structs |
| `include/ezgl/qt/ogl_renderer.hpp` | `ogl_renderer` (renderer subclass) |
| `src/qt/ogl_canvas_widget.cpp` | `OglWidget` implementation — GL pipeline, upload, draw |
| `src/qt/ogl_renderer.cpp` | `ogl_renderer` implementation — mirrors rhi_renderer |

## Files to Modify

| File | Change |
|---|---|
| `CMakeLists.txt` | `EZGL_USE_OGL` option; `Qt6::OpenGL Qt6::OpenGLWidgets`; add sources; `EZGL_OGL` define |
| `include/ezgl/canvas.hpp` | Add `#ifdef EZGL_OGL` block with `OglWidget*` + `ogl_renderer` members |
| `src/canvas.cpp` | OGL branch in `initialize()`, `redraw()`, `redraw_camera_only()`, deferred-redraw |
| `src/qt/qtgladeloader.cpp` | Create `OglWidget` when `EZGL_OGL` (before `EZGL_RHI`, before `DrawingAreaWidget`) |
| `src/qt/ezgl_qtcompat.cpp` | Accept `OglWidget` events in `Application::notify()` |

---

## Build

```cmake
cmake -DEZGL_USE_OGL=ON -DEZGL_USE_RHI=OFF ...
```

Both flags can be `ON` simultaneously (the loader picks `OglWidget` first when
both are defined, or vice-versa — one flag must win; by convention `EZGL_OGL`
takes priority over `EZGL_RHI`).

`EZGL_OGL` requires `Qt6 >= 6.2` with `OpenGL` and `OpenGLWidgets` components.
No minimum-version check beyond that.
