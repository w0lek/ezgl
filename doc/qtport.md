Components:
[gtk_main_window/gtk_app]->[QApplication]
[GTK-Cairo-drawing-primitives]->[QPainter]
[GTK-Cairo-drawing-text]->[QPainter]
[GTK-Cairo-drawing-camera-transform]
[GTK vs QT benchmark on many different primitive types including text drawing]
[GTK_input_processing (keyboard press/mouse move/press)]->[Qt events handling + callbacks as a slots]

[]



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

</strike>

## GTK/Cairo to Qt **Structures** mapping
- ### Primitives (lines, rectangle, path, arc, circle ...)

| | Current (GTK-Cairo) | Intermediate (Qt-compat layer) | Final (Qt) | Role |
|-|-|-|-|-|
| | cairo_t | <code>struct cairo_t {<br>public:<br>&nbsp;&nbsp;QPainter::RenderHints renderHints;<br>&nbsp;&nbsp;QImage* surface;<br>&nbsp;&nbsp;QColor color;<br>&nbsp;&nbsp;QPen pen;<br>&nbsp;&nbsp;QBrush brush;<br>&nbsp;&nbsp;QPainterPath path;<br>&nbsp;&nbsp;QFont font;<br>&nbsp;&nbsp;std::optional&lt;QTransform&gt; transform;<br>};</code> | <code>struct PainterContext {<br>public:<br>&nbsp;&nbsp;QPainter::RenderHints renderHints;<br><strike>&nbsp;&nbsp;QImage* surface;</strike>// will be part of DrawableAreaWidget<br>&nbsp;&nbsp;QColor color;<br>&nbsp;&nbsp;QPen pen;<br>&nbsp;&nbsp;QBrush brush;<br><strike>&nbsp;&nbsp;QPainterPath path;</strike> // -> becomes local to render call scope<br>&nbsp;&nbsp;QFont font;<strike><br>&nbsp;&nbsp;std::optional&lt;QTransform&gt; transform;</strike> // -> becomes local to render call scope<br>};</code> | Drawing object and context
| | cairo_surface_t | QImage | | Surface to draw on |

- GTK/Cairo to Qt **API** mapping

**QPainter specific**
| Current (GTK-Cairo) | Intermediate (Qt-compat layer) | Final(Qt) |
|----------|----------|-----------|
| void cairo_fill(cairo_t* ctx); | void cairo_fill(cairo_t* ctx, Painter&); | void Painter::fill(); |
| void cairo_stroke(cairo_t* ctx); | void cairo_stroke(cairo_t* ctx, Painter&); | void Painter::stroke(); |
| void cairo_paint(cairo_t* ctx); | void cairo_paint(cairo_t* ctx, Painter&); | void Painter::paint(); |
| void cairo_set_source_surface(cairo_t* cairo, cairo_surface_t* surface, double x, double y); | void cairo_set_source_surface(cairo_t* cairo, QImage* surface, double x, double y, Painter&); | void Painter::setSourceSurface(QImage* surface, double x, double y); |

**QTransform specific**
| Current (GTK-Cairo) | Intermediate (Qt-compat layer) | Final(Qt) |
|----------|----------|-----------|
| void cairo_save(cairo_t* ctx); | void cairo_save(cairo_t* ctx); | void Painter::save()
void cairo_restore(cairo_t* ctx); | void cairo_restore(cairo_t* ctx); | void Painter::restore
void cairo_scale(cairo_t* ctx, double sx, double sy); | void cairo_scale(cairo_t* ctx, double sx, double sy); | void Painter::scale(double sx, double sy) |

**Text specific**
| | Current (GTK-Cairo) | Intermediate (Qt-compat layer) | Final(Qt) | Role |
|-|-|-|-|-|
| 1 | cairo_text_extents_t | <code>struct cairo_text_extents_t {<br>&nbsp;&nbsp;double x_bearing;<br>&nbsp;&nbsp;double y_bearing;<br>&nbsp;&nbsp;double width;<br>&nbsp;&nbsp;double height;<br>&nbsp;&nbsp;double x_advance;<br>&nbsp;&nbsp;double y_advance;<br>};</code> | See row below. | Describes how text is positioned <br> and how much space it occupies
| 2 | void cairo_text_extents(cairo_t* ctx, const char* utf8, cairo_text_extents_t* extents); | <code>void cairo_text_extents(cairo_t* ctx, const char* utf8, cairo_text_extents_t* extents)<br>{<br>&nbsp;&nbsp;QString text = QString::fromUtf8(utf8);<br>&nbsp;&nbsp;QFontMetricsF fm(ctx->font);<br><br>&nbsp;&nbsp;// QRectF is given in logical coords, origin at baseline (like Cairo)<br>&nbsp;&nbsp;QRectF br = fm.boundingRect(text);<br><br>&nbsp;&nbsp;extents->x_bearing = br.x();<br>&nbsp;&nbsp;extents->y_bearing = br.y();<br>&nbsp;&nbsp;extents->width&nbsp;&nbsp;&nbsp;&nbsp;= br.width();<br>&nbsp;&nbsp;extents->height&nbsp;&nbsp;&nbsp;= br.height();<br><br>&nbsp;&nbsp;// Advance: how much the current point moves along the baseline<br>&nbsp;&nbsp;extents->x_advance = fm.horizontalAdvance(text);<br>&nbsp;&nbsp;extents->y_advance = 0.0; // Qt horizontal layout, so y-advance is 0<br>}</code> | <code>class TextExtents {<br>&nbsp;&nbsp;public:<br>&nbsp;&nbsp;TextExtents(const QFont& font, const char* utf8) {<br>&nbsp;&nbsp;&nbsp;&nbsp;QString text = QString::fromUtf8(utf8);<br>&nbsp;&nbsp;&nbsp;&nbsp;QFontMetricsF fm(font);<br>&nbsp;&nbsp;&nbsp;&nbsp;QRectF br = fm.boundingRect(text);<br>&nbsp;&nbsp;&nbsp;&nbsp;x_bearing = br.x();<br>&nbsp;&nbsp;&nbsp;&nbsp;y_bearing = br.y();<br>&nbsp;&nbsp;&nbsp;&nbsp;width&nbsp;&nbsp;&nbsp;&nbsp;= br.width();<br>&nbsp;&nbsp;&nbsp;&nbsp;height&nbsp;&nbsp;&nbsp;= br.height();<br>&nbsp;&nbsp;&nbsp;&nbsp;x_advance = fm.horizontalAdvance(text);<br>&nbsp;&nbsp;&nbsp;&nbsp;y_advance = 0.0;<br>&nbsp;&nbsp;}<br><br>&nbsp;&nbsp;double x_bearing;<br>&nbsp;&nbsp;double y_bearing;<br>&nbsp;&nbsp;double width;<br>&nbsp;&nbsp;double height;<br>&nbsp;&nbsp;double x_advance;<br>&nbsp;&nbsp;double y_advance;<br>};</code> | 
| 3 | cairo_font_extents_t | <code>struct cairo_font_extents_t {<br>&nbsp;&nbsp;double ascent;<br>&nbsp;&nbsp;double descent;<br>&nbsp;&nbsp;double height;<br>&nbsp;&nbsp;double max_x_advance;<br>&nbsp;&nbsp;double max_y_advance;<br>};</code> | <code>class FontMetrics : public QFontMetricsF {<br>&nbsp;&nbsp;public:<br>&nbsp;&nbsp;int maxHorizontalAdvance();<br>&nbsp;&nbsp;int maxVerticalAdvance();<br>};</code> |  Font size properties 
