**Note:** Intermediate result mostly keeps the API (function signatures) stable to minimize the code diff, and provide easy way to compare GTK/Qt code side by side without switching editor context.


## GTK CAIRO -> QPainter mapping

<strike>There are two main parts cairo_t and cairo_surface_t.
Replacing shema:
cairo_t -> PainterContext
cairo_surface_t -> QImage
cairo_... -> cairo_... + QPainter;
</strike>

- GTK/Cairo to Qt **Structures** mapping

| Role | Current (GTK) | Intermediate (Qt) | Final (Qt) |
|-|-|-|-|
| | cairo_t | <code>struct cairo_t {<br>public:<br>&nbsp;&nbsp;QPainter::RenderHints renderHints;<br>&nbsp;&nbsp;QImage* surface;<br>&nbsp;&nbsp;QColor color;<br>&nbsp;&nbsp;QPen pen;<br>&nbsp;&nbsp;QBrush brush;<br>&nbsp;&nbsp;QPainterPath path;<br>&nbsp;&nbsp;QFont font;<br>&nbsp;&nbsp;std::optional&lt;QTransform&gt; transform;<br>};</code> | <code>struct PainterContext {<br>public:<br>&nbsp;&nbsp;QPainter::RenderHints renderHints;<br><strike>&nbsp;&nbsp;QImage* surface;</strike>// will be part of DrawableAreaWidget<br>&nbsp;&nbsp;QColor color;<br>&nbsp;&nbsp;QPen pen;<br>&nbsp;&nbsp;QBrush brush;<br><strike>&nbsp;&nbsp;QPainterPath path;</strike> // -> becomes local to render call scope<br>&nbsp;&nbsp;QFont font;<strike><br>&nbsp;&nbsp;std::optional&lt;QTransform&gt; transform;</strike> // -> becomes local to render call scope<br>};</code>
| | cairo_surface_t | QImage |
| | cairo_text_extents_t | <code>struct cairo_text_extents_t {<br>&nbsp;&nbsp;double x_bearing;<br>&nbsp;&nbsp;double y_bearing;<br>&nbsp;&nbsp;double width;<br>&nbsp;&nbsp;double height;<br>&nbsp;&nbsp;double x_advance;<br>&nbsp;&nbsp;double y_advance;<br>};</code> | <code>struct TextExtents {<br>&nbsp;&nbsp;double x_bearing;<br>&nbsp;&nbsp;double y_bearing;<br>&nbsp;&nbsp;double width;<br>&nbsp;&nbsp;double height;<br>&nbsp;&nbsp;double x_advance;<br>&nbsp;&nbsp;double y_advance;<br>};</code> | |
| | cairo_font_extents_t | <code>struct cairo_font_extents_t {<br>&nbsp;&nbsp;double ascent;<br>&nbsp;&nbsp;double descent;<br>&nbsp;&nbsp;double height;<br>&nbsp;&nbsp;double max_x_advance;<br>&nbsp;&nbsp;double max_y_advance;<br>};</code> | <code>struct FontExtents {<br>&nbsp;&nbsp;double ascent;<br>&nbsp;&nbsp;double descent;<br>&nbsp;&nbsp;double height;<br>&nbsp;&nbsp;double max_x_advance;<br>&nbsp;&nbsp;double max_y_advance;<br>};</code> |

- GTK/Cairo to Qt **API** mapping

| Current (GTK) | Intermediate (Qt) | Final(Qt) |
|----------|----------|-----------|
|| | |
|  | | |



