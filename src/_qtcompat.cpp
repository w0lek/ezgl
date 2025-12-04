#include <ezgl/_qtcompat.hpp>

#ifdef EZGL_QT

// gtk wrapper
QWidget* GTK_WIDGET(QObject* obj) {
  return qobject_cast<QWidget*>(obj);
}

QComboBox* GTK_COMBO_BOX(QObject* obj) {
  return qobject_cast<QComboBox*>(obj);
}

QWindow* GTK_WINDOW(QObject* obj) {
  return qobject_cast<QWindow*>(obj);
}

QApplication* G_APPLICATION(QObject* obj) {
  return qobject_cast<QApplication*>(obj);
}

bool GTK_IS_BUTTON(QObject* obj) {
  return qobject_cast<QPushButton*>(obj) != nullptr;
}

QWidget* gtk_application_get_active_window(QApplication* app)
{
  return QApplication::activeWindow();
}

void gtk_main() {
  g_debug("~~~ gtk_main");
  qApp->exec();
}

void gtk_main_quit()
{
  g_debug("~~~ gtk_main_quit");
  QApplication::quit();
}

void g_application_quit(QApplication* app)
{
  g_debug("~~~ g_application_quit");
  app->exit(0);
}

QApplication* gtk_application_new(const char* appName)
{
  g_debug("~~~ gtk_application_new");
  int argc = 0;
  char** argv = nullptr;
  QApplication* app = new QApplication(argc, argv);
  app->setApplicationName(appName);
  return app;
}

void gtk_widget_destroy(QWidget* widget)
{
  g_debug("~~~ gtk_widget_destroy");
  if (!widget)
    return;

  widget->hide();
  widget->setParent(nullptr);
  widget->deleteLater();
}

int gtk_widget_get_allocated_width(QWidget* w) {
  return w->width();
}

int gtk_widget_get_allocated_height(QWidget* w) {
  return w->height();
}

char* gtk_combo_box_text_get_active_text(QComboBox* combo)
{
  if (!combo) {
    return nullptr;
  }

  QByteArray utf8 = combo->currentText().toUtf8();
  char* result = strdup(utf8.constData());  // caller must free()

  return result;
}

void gtk_combo_box_set_active(QComboBox* combo, int idx)
{
  combo->setCurrentIndex(idx);
}


void gtk_widget_queue_draw(QWidget* widget)
{
  g_debug("~~~ gtk_widget_queue_draw");
  widget->update();
}

void g_free(void* ptr)
{
  g_debug("~~~ g_free");
  free(ptr);
}
// gtk wrapper

int cairo_image_surface_get_width(QImage* image)
{
  return image->width();
}

int cairo_image_surface_get_height(QImage* image)
{
  return image->height();
}


void cairo_new_path(cairo_t* ctx)
{
  ctx->path = QPainterPath();
}

void cairo_scale(cairo_t* ctx, double sx, double sy)
{
  ctx->painter.scale(sx, sy);
}

void cairo_save(cairo_t* ctx)
{
  ctx->painter.save();
}

void cairo_restore(cairo_t* ctx)
{
  ctx->painter.restore();
}

void cairo_fill(cairo_t* ctx)
{
  QBrush brush(ctx->color);
  ctx->painter.setBrush(brush);

  ctx->painter.drawPath(ctx->path);
  ctx->path = QPainterPath(); // reset like Cairo resets current path
}

void cairo_close_path(cairo_t* ctx)
{
  ctx->path.closeSubpath(); // ??
}

void cairo_stroke(cairo_t* ctx)
{
  ctx->painter.strokePath(ctx->path, ctx->painter.pen());
  ctx->path = QPainterPath();    // reset for next stroke
}

void cairo_move_to(cairo_t* ctx, double x, double y)
{
  ctx->path.moveTo(QPointF(x, y));
}

void cairo_line_to(cairo_t* ctx, double x, double y)
{
  ctx->path.lineTo(QPointF(x, y));
}

/*
Cairo expects clockwise ⇒ Qt expects counterclockwise.
→ multiply angles by -1.

Cairo sweep direction is implicit (angle2 < angle1).
→ compute sweep = endDeg - startDeg.

Qt’s arcTo uses bounding box and angles in degrees.
*/
void cairo_arc(cairo_t* cr,
    double xc, double yc,
    double radius,
    double angle1, double angle2)
{
  // radians → degrees
  double startDeg = -angle1 * 180.0 / std::numbers::pi;
  double endDeg   = -angle2 * 180.0 / std::numbers::pi;

  double spanDeg = endDeg - startDeg;

  QRectF rect(xc - radius, yc - radius,
      radius * 2.0, radius * 2.0);

  cr->path.arcTo(rect, startDeg, spanDeg);
}
void cairo_arc_negative(cairo_t* ctx,
    double xc, double yc,
    double radius,
    double angle1, double angle2)
{
  // radians → degrees
  double startDeg = -angle1 * 180.0 / std::numbers::pi;
  double endDeg   = -angle2 * 180.0 / std::numbers::pi;

  double sweep = endDeg - startDeg; // negative sweep

  double d = radius * 2.0;
  QRectF rect(xc - radius, yc - radius, d, d);

  ctx->path.arcTo(rect, startDeg, sweep);
}

void cairo_select_font_face(cairo_t* ctx, const char* family, cairo_font_slant_t slant, cairo_font_weight_t weight)
{
  QFont font = ctx->painter.font();

  if (family) {
    font.setFamily(QString::fromUtf8(family));
  }

  font.setStyle(slant);
  font.setWeight(weight);

  ctx->painter.setFont(font);
}

void cairo_set_dash(cairo_t* ctx, const qreal* pattern, int count, qreal offset)
{
  QPen pen = ctx->painter.pen();
  if (pattern == nullptr || count == 0) {
    pen.setStyle(Qt::SolidLine);
  } else {
    QVector<double> dashes(count);
    for (int i = 0; i < count; ++i) {
      dashes[i] = pattern[i];
    }

    pen.setDashPattern(dashes);
    pen.setDashOffset(offset);
  }
  ctx->painter.setPen(pen);
}

void cairo_set_font_size(cairo_t* ctx, int size)
{
  QFont font = ctx->painter.font();
  font.setPointSizeF(size);
  ctx->painter.setFont(font);
}

void cairo_set_line_width(cairo_t* ctx, int width)
{
  QPen pen = ctx->painter.pen();
  pen.setWidthF(width == 0 ? 1.0 : width);
  ctx->painter.setPen(pen);
}

void cairo_set_line_cap(cairo_t* ctx, Qt::PenCapStyle cap) {
  QPen pen = ctx->painter.pen();
  pen.setCapStyle(cap);
  ctx->painter.setPen(pen);
}

void cairo_set_source_rgb(cairo_t* ctx, double r, double g, double b) {
  ctx->color.setRedF(r);
  ctx->color.setGreenF(g);
  ctx->color.setBlueF(b);
  ctx->color.setAlphaF(1.0);
}

void cairo_set_source_rgba(cairo_t* ctx, double r, double g, double b, double a) {
  ctx->color.setRedF(r);
  ctx->color.setGreenF(g);
  ctx->color.setBlueF(b);
  ctx->color.setAlphaF(a);
}

void cairo_paint(cairo_t* ctx)
{
  ctx->painter.fillRect(ctx->painter.viewport(), ctx->color);
}

void cairo_surface_destroy(QImage* surface) {
  delete surface;
}

void cairo_destroy(cairo_t* cairo) {
  delete cairo;
}

void cairo_set_source_surface(cairo_t* cairo, QImage* surface, double x, double y)
{
  cairo->painter.drawImage(QPointF(x, y), *surface);
}
// cairo wrapper

// Core logging function (printf-style)
void log_message(const char* level,
    const char* file,
    int line,
    const char* fmt,
    ...)
{
  // timestamp (optional, but nice to have)
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif

  char time_buf[32];
  std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm);

  // prefix: time + level + file:line
  std::fprintf(stderr, "%s %s: %s:%d: ",
      time_buf, level, file, line);

  // body (printf-style)
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(stderr, fmt, ap);
  va_end(ap);

  std::fputc('\n', stderr);
}

#endif // EZGL_QT
