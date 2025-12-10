

Components:
[gtk_main_window/gtk_app]->[QApplication]
[gtk_main_window + cairo_drawing_surface]->[QWindow + QDrawableArea widget]
[GTK-Cairo-drawing-primitives]->[QPainter]
[GTK-Cairo-drawing-text]->[QPainter]
[GTK-Cairo-drawing-camera-transform]
[GTK vs Qt benchmark on many different primitive types including text drawing]
[GTK_input_processing (keyboard press/mouse move/press)]->[Qt events handling, g_callbacks to a slots]
[GTK widgets fabric]->[QWidgets fabric]

## Goal:
- seamless incremental migration
- migrate each individual component, with validating result
- initial idea is to get cairo-like QPainter implementation at initial stage without advanced render optimization, so we basically copy cairo->QPainter API 1 to 1.
- apply SW render optimization, and HW render optimization if needed after the Qt port is done

**Note:** Intermediate result mostly keeps the API (function signatures) stable to minimize the code diff, and provide easy way to compare GTK/Qt code side by side without switching editor context.

we need Intermediate (Qt-compat layer) to keep API close to original as much as possible to provide smooth port process

## GTK CAIRO -> QPainter mapping

<span style="color:red">There are two main parts cairo_t and 
cairo_surface_t.
Replacing shema:
cairo_t -> PainterContext
cairo_surface_t -> QImage
cairo_... -> cairo_... + QPainter;
cairo:
  draw geometry primitives
  draw text
</span>

cairo draws onto surface(image), than this surface attached to widget render area.

# GTK/Cairo to Qt mapping
## enum
| | Current (GTK-Cairo) | Intermediate (Qt-compat layer) | Final (Qt) | Role |
|-|-|-|-|-|
| | cairo_line_cap_t | using cairo_line_cap_t = Qt::PenCapStyle; | Qt::PenCapStyle
| | CAIRO_LINE_CAP_BUTT | #define CAIRO_LINE_CAP_BUTT	Qt::FlatCap | Qt::FlatCap
| | CAIRO_LINE_CAP_ROUND | #define CAIRO_LINE_CAP_ROUND Qt::RoundCap | Qt::RoundCap
| | CAIRO_LINE_CAP_SQUARE | #define CAIRO_LINE_CAP_SQUARE	Qt::SquareCap | Qt::SquareCap
| | cairo_font_slant_t | using cairo_font_slant_t = QFont::Style; | QFont::Style
| | CAIRO_FONT_SLANT_NORMAL | #define CAIRO_FONT_SLANT_NORMAL QFont::StyleNormal | QFont::StyleNormal
| | CAIRO_FONT_SLANT_ITALIC | #define CAIRO_FONT_SLANT_ITALIC QFont::StyleItalic | QFont::StyleItalic
| | CAIRO_FONT_SLANT_OBLIQUE | #define CAIRO_FONT_SLANT_OBLIQUE QFont::StyleOblique | QFont::StyleOblique

## Drawing Primitives (lines, rectangle, path, arc, circle ...)

| | Current (GTK-Cairo) | Intermediate (Qt-compat layer) | Final (Qt) | Role |
|-|-|-|-|-|
| | cairo_t | <code>struct cairo_t {<br>public:<br>&nbsp;&nbsp;QPainter::RenderHints renderHints;<br>&nbsp;&nbsp;QImage* surface;<br>&nbsp;&nbsp;QColor color;<br>&nbsp;&nbsp;QPen pen;<br>&nbsp;&nbsp;QBrush brush;<br>&nbsp;&nbsp;QPainterPath path;<br>&nbsp;&nbsp;QFont font;<br>&nbsp;&nbsp;std::optional&lt;QTransform&gt; transform;<br>};</code> | <code>struct PainterContext {<br>public:<br>&nbsp;&nbsp;QPainter::RenderHints renderHints;<br><strike>&nbsp;&nbsp;QImage* surface;</strike>// will be part of DrawableAreaWidget<br>&nbsp;&nbsp;QColor color;<br>&nbsp;&nbsp;QPen pen;<br>&nbsp;&nbsp;QBrush brush;<br><strike>&nbsp;&nbsp;QPainterPath path;</strike> // -> becomes local to render call scope<br>&nbsp;&nbsp;QFont font;<strike><br>&nbsp;&nbsp;std::optional&lt;QTransform&gt; transform;</strike> // -> becomes local to render call scope<br>};</code> | Drawing object and context
| | cairo_surface_t | QImage | | Surface to draw on |

**QPainter specific (immediate drawing calls)**
|| Current (GTK-Cairo) | Intermediate (Qt-compat layer) | Final(Qt) |
|-|-|-|-|
| | void cairo_fill(cairo_t* ctx); | void cairo_fill(cairo_t* ctx, Painter&); | void Painter::fill(); |
| | void cairo_stroke(cairo_t* ctx); | void cairo_stroke(cairo_t* ctx, Painter&); | void Painter::stroke(); |
| | void cairo_paint(cairo_t* ctx); | void cairo_paint(cairo_t* ctx, Painter&); | void Painter::paint(); |
| | void cairo_set_source_surface(cairo_t* cairo, cairo_surface_t* surface, double x, double y); | void cairo_set_source_surface(cairo_t* cairo, QImage* surface, double x, double y, Painter&); | void Painter::setSourceSurface(QImage* surface, double x, double y); |

**QTransform specific**
| | Current (GTK-Cairo) | Intermediate (Qt-compat layer) | Final(Qt) |
|-|-|-|-|
| | void cairo_save(cairo_t* ctx); | void cairo_save(cairo_t* ctx); | void Painter::save()
| | void cairo_restore(cairo_t* ctx); | void cairo_restore(cairo_t* ctx); | void Painter::restore
| | void cairo_scale(cairo_t* ctx, double sx, double sy); | void cairo_scale(cairo_t* ctx, double sx, double sy); | void Painter::scale(double sx, double sy) |

**Text specific**
| | Current (GTK-Cairo) | Intermediate (Qt-compat layer) | Final(Qt) | Role |
|-|-|-|-|-|
| 1 | cairo_text_extents_t | <code>struct cairo_text_extents_t {<br>&nbsp;&nbsp;double x_bearing;<br>&nbsp;&nbsp;double y_bearing;<br>&nbsp;&nbsp;double width;<br>&nbsp;&nbsp;double height;<br>&nbsp;&nbsp;double x_advance;<br>&nbsp;&nbsp;double y_advance;<br>};</code> | See row below. | Describes how text is positioned <br> and how much space it occupies
| 2 | void cairo_text_extents(cairo_t* ctx, const char* utf8, cairo_text_extents_t* extents); | <code>void cairo_text_extents(cairo_t* ctx, const char* utf8, cairo_text_extents_t* extents)<br>{<br>&nbsp;&nbsp;QString text = QString::fromUtf8(utf8);<br>&nbsp;&nbsp;QFontMetricsF fm(ctx->font);<br><br>&nbsp;&nbsp;// QRectF is given in logical coords, origin at baseline (like Cairo)<br>&nbsp;&nbsp;QRectF br = fm.boundingRect(text);<br><br>&nbsp;&nbsp;extents->x_bearing = br.x();<br>&nbsp;&nbsp;extents->y_bearing = br.y();<br>&nbsp;&nbsp;extents->width&nbsp;&nbsp;&nbsp;&nbsp;= br.width();<br>&nbsp;&nbsp;extents->height&nbsp;&nbsp;&nbsp;= br.height();<br><br>&nbsp;&nbsp;// Advance: how much the current point moves along the baseline<br>&nbsp;&nbsp;extents->x_advance = fm.horizontalAdvance(text);<br>&nbsp;&nbsp;extents->y_advance = 0.0; // Qt horizontal layout, so y-advance is 0<br>}</code> | <code>class TextExtents {<br>&nbsp;&nbsp;public:<br>&nbsp;&nbsp;TextExtents(const QFont& font, const char* utf8) {<br>&nbsp;&nbsp;&nbsp;&nbsp;QString text = QString::fromUtf8(utf8);<br>&nbsp;&nbsp;&nbsp;&nbsp;QFontMetricsF fm(font);<br>&nbsp;&nbsp;&nbsp;&nbsp;QRectF br = fm.boundingRect(text);<br>&nbsp;&nbsp;&nbsp;&nbsp;x_bearing = br.x();<br>&nbsp;&nbsp;&nbsp;&nbsp;y_bearing = br.y();<br>&nbsp;&nbsp;&nbsp;&nbsp;width&nbsp;&nbsp;&nbsp;&nbsp;= br.width();<br>&nbsp;&nbsp;&nbsp;&nbsp;height&nbsp;&nbsp;&nbsp;= br.height();<br>&nbsp;&nbsp;&nbsp;&nbsp;x_advance = fm.horizontalAdvance(text);<br>&nbsp;&nbsp;&nbsp;&nbsp;y_advance = 0.0;<br>&nbsp;&nbsp;}<br><br>&nbsp;&nbsp;double x_bearing;<br>&nbsp;&nbsp;double y_bearing;<br>&nbsp;&nbsp;double width;<br>&nbsp;&nbsp;double height;<br>&nbsp;&nbsp;double x_advance;<br>&nbsp;&nbsp;double y_advance;<br>};</code> | 
| 3 | cairo_font_extents_t | <code>struct cairo_font_extents_t {<br>&nbsp;&nbsp;double ascent;<br>&nbsp;&nbsp;double descent;<br>&nbsp;&nbsp;double height;<br>&nbsp;&nbsp;double max_x_advance;<br>&nbsp;&nbsp;double max_y_advance;<br>};</code> | <code>class FontMetrics : public QFontMetricsF {<br>&nbsp;&nbsp;public:<br>&nbsp;&nbsp;int maxHorizontalAdvance();<br>&nbsp;&nbsp;int maxVerticalAdvance();<br>};</code> |  Font size properties 

**Rest of Cairo**
| | Current (GTK-Cairo) | Intermediate (Qt-compat layer) | Final(Qt) | Role |
|-|-|-|-|-|
| | int cairo_image_surface_get_width(cairo_surface_t* surface); | <code>int cairo_image_surface_get_width(cairo_surface_t* surface){<br>&nbsp;&nbsp;return surface->width();<br>}</code> | int QImage::width()</code>;
| | int cairo_image_surface_get_height(cairo_surface_t* surface); | | int QImage::height();
| | void cairo_new_path(cairo_t* ctx); | | void Painter::newPath();
| | void cairo_close_path(cairo_t* ctx); | | void Painter::closePath();
| | void cairo_move_to(cairo_t* ctx, double x, double y); | <code>void cairo_move_to(cairo_t* ctx, double x, double y){<br>&nbsp;&nbsp;// Add 0.5 for extra half-pixel accuracy<br>&nbsp;&nbsp;ctx->path.moveTo(x+0.5,y+0.5);<br>}</code> | void Painter::moveTo(double x, double y)
| | void cairo_line_to(cairo_t* ctx, double x, double y); | | Painter::lineTo(double x, double y)
| | void cairo_arc(cairo_t* cr, double xc, double yc, double radius, double angle1, double angle2); | | void Painter::arc(double xc, double yc, double radius, double angle1, double angle2)
| | void cairo_arc_negative(cairo_t* ctx, double xc, double yc, double radius, double angle1, double angle2); | | void Painter::arcNegative(double xc, double yc, double radius, double angle1, double angle2)
| | void cairo_select_font_face(cairo_t* ctx, const char* family, cairo_font_slant_t slant, cairo_font_weight_t weight);
| | void cairo_set_dash(cairo_t* ctx, const double* pattern, int count, double offset); | | void Painter::setDash(const std::vector<double>& pattern, double offset);
| | void cairo_set_font_size(cairo_t* ctx, int size); | | void Painter::setFontSize(int size);
| | void cairo_set_line_width(cairo_t* ctx, int width); | | void Painter::setLineWidth(int width)
| | void cairo_set_line_cap(cairo_t* ctx, cairo_line_cap_t cap); | | void Painter::setLineCap(Qt::PenCapStyle cap);
| | void cairo_set_source_rgb(cairo_t* ctx, double r, double g, double b); | | void Painter::setColor(double r, double g, double b) 
| | void cairo_set_source_rgba(cairo_t* ctx, double r, double g, double b, double a); | | void Painter::setColor(double r, double g, double b, double a)
| | void cairo_surface_destroy(cairo_surface_t* surface); | | OBSOLETE (QImage will not be raw pointer)
| | void cairo_destroy(cairo_t* cairo); | | OBSOLETE (Painter will not be raw pointer)

## QPainter SW render optimization
<span style="color:red">
QPainter when drawing into target device as QImage is software renderer.
To make SW renderer optimal, we could do batch. QPainter has batch function for lines QPainter::drawLines(), where we could group lines by style(color + width + cap).
To optimize rendering many rectangles (filled or stroked) we could try to imitate drawing multiple shapes with QPainterPath. So we group several rectangle by similar style and draw it with one draw call.
</span>

  ## QPainter HW render optimization

<span style="color:red">
    Note: batching optimization we did for SW render will be usefull here too.
  By changing target render device for QPainter from QImage to FBO, we could get HW accelerated drawing.
  The only thing to refactor here is to render objects in specific stage (when gl context is active), also we need do gl initialization.
</span>