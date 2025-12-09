
## GTK CAIRO -> QPainter mapping

## Structure
| Current (GTK) | Intermediate (Qt) | Final (Qt) |
|-|-|-|
| cairo_t | <code>struct cairo_t {<br>public:<br>&nbsp;&nbsp;QPainter::RenderHints renderHints;<br>&nbsp;&nbsp;QImage* surface;<br>&nbsp;&nbsp;QColor color;<br>&nbsp;&nbsp;QPen pen;<br>&nbsp;&nbsp;QBrush brush = QBrush(Qt::SolidPattern);<br>&nbsp;&nbsp;QPainterPath path;<br>&nbsp;&nbsp;QFont font;<br>&nbsp;&nbsp;std::optional&lt;QTransform&gt; transform;<br>};</code>|
| cairo_surface_t | QImage |

| Current (GTK) | Intermediate (Qt) | Final(Qt) |
|----------|----------|-----------|
|<code>int x = 10;<br>return x;</code>| | |
|  | | |