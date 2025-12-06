#include <ezgl/_qtcompat.hpp>

#ifdef EZGL_QT

DrawingAreaWidget::DrawingAreaWidget(QWidget* parent): QWidget(parent)
{

}

DrawingAreaWidget::~DrawingAreaWidget()
{

}

Image* DrawingAreaWidget::createSurface() {
  if (!m_image) {
#ifdef HIGHT_DPI_FACTOR
    const double dpr = 2.0 * devicePixelRatioF();
#else
    const double dpr = devicePixelRatioF();
#endif

    const int w = std::max(1, int(width()  * dpr));
    const int h = std::max(1, int(height() * dpr));

    m_image = new Image(w, h, QImage::Format_ARGB32_Premultiplied);
    m_image->setDevicePixelRatio(dpr);
    m_image->fill(Qt::transparent);
  }
  return m_image;
}

void DrawingAreaWidget::paintEvent(QPaintEvent* event)
{
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.drawImage(rect(), *m_image, m_image->rect());
}

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

bool GTK_IS_BUTTON(QObject* obj) {
  return qobject_cast<QPushButton*>(obj) != nullptr;
}

QWidget* gtk_application_get_active_window(Application* app)
{
  return Application::activeWindow();
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

int g_application_run(Application* app)
{
  g_debug("~~~ g_application_run");
  return app->exec();
}

void g_application_quit(Application* app)
{
  g_debug("~~~ g_application_quit");
  app->exit(0);
}

Application* gtk_application_new(const char* appName)
{
  g_debug("~~~ gtk_application_new RISKY");
  static int argc = 0;
  static char** argv = nullptr;
  Application* app = new Application(argc, argv);
  app->setApplicationName(appName);
  return app;
}

Application* gtk_application_new(const char* appName, int& argc, char** argv)
{
  g_debug("~~~ gtk_application_new");
  Application* app = new Application(argc, argv);
  app->setApplicationName(appName);
  return app;
}

int Painter::counter = 0;
int Painter::nextid = 0;

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

// cairo wrapper

// QPainter specific
namespace {
void apply_painter_states_helper(cairo_t* ctx, Painter& painter)
{
  painter.setRenderHints(ctx->renderHints);
  if (ctx->dirtyFlags.empty()) {
    return;
  }

  for (int flag: ctx->dirtyFlags) {
    switch(flag) {
    case cairo_t::DIRTY::PEN: painter.setPen(ctx->pen); break;
    case cairo_t::DIRTY::BRUSH: painter.setBrush(ctx->brush); break;
    case cairo_t::DIRTY::FONT: painter.setFont(ctx->font); break;
    }
  }
  ctx->dirtyFlags.clear();
}
} // namespace

void cairo_fill(cairo_t* ctx, Painter& painter)
{
  apply_painter_states_helper(ctx, painter);

  // draw path
  painter.drawPath(ctx->path);

  // clear path after drawing
  ctx->path = QPainterPath();
}

void cairo_stroke(cairo_t* ctx, Painter& painter)
{
  apply_painter_states_helper(ctx, painter);

  // draw stroke path
  painter.strokePath(ctx->path, ctx->pen);

  // clear path after drawing
  ctx->path = QPainterPath();
}

void cairo_paint(cairo_t* ctx, Painter& painter)
{
  painter.fillRect(painter.viewport(), ctx->color);
}

void cairo_set_source_surface(cairo_t*, Image* surface, double x, double y, Painter& painter)
{
  painter.drawImage(QPointF(x, y), *surface);
}

// void cairo_scale(cairo_t* ctx, double sx, double sy, Painter& painter)
// {
//   painter.scale(sx, sy);
// }

// void cairo_save(cairo_t* ctx, Painter& painter)
// {
//   painter.save();
// }

// void cairo_restore(cairo_t* ctx, Painter& painter)
// {
//   painter.restore();
// }
void cairo_scale(cairo_t* ctx, double sx, double sy)
{
  TODO;
  //painter.scale(sx, sy);
}

void cairo_save(cairo_t* ctx)
{
  TODO;
  //painter.save();
}

void cairo_restore(cairo_t* ctx)
{
  TODO;
  //painter.restore();
}
// QPainter specific

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

void cairo_close_path(cairo_t* ctx)
{
  ctx->path.closeSubpath(); // ??
}

void cairo_move_to(cairo_t* ctx, double x, double y)
{
  // +0.5 pixel to get more consistent line width
  ctx->path.moveTo(QPointF(x+0.5, y+0.5));
}

void cairo_line_to(cairo_t* ctx, double x, double y)
{
  // +0.5 pixel to get more consistent line width
  ctx->path.lineTo(QPointF(x+0.5, y+0.5));
}

void cairo_arc(cairo_t* ctx,
    double xc, double yc,
    double radius,
    double angle1, double angle2)
{
  // radians → degrees
  double startDeg = -angle1 * 180.0 / std::numbers::pi;
  double endDeg   = -angle2 * 180.0 / std::numbers::pi;

  double spanDeg = endDeg - startDeg;

  double d = radius * 2.0;
  QRectF rect(xc - radius, yc - radius, d, d);

  ctx->path.arcTo(rect, startDeg, spanDeg);
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
  if (family) {
    ctx->font.setFamily(QString::fromUtf8(family));
  }

  ctx->font.setStyle(slant);
  ctx->font.setWeight(weight);

  ctx->dirtyFlags.emplace_back(cairo_t::DIRTY::FONT);
}

void cairo_set_dash(cairo_t* ctx, const qreal* pattern, int count, qreal offset)
{
  if (pattern == nullptr || count == 0) {
    ctx->pen.setStyle(Qt::SolidLine);
  } else {
    QVector<double> dashes(count);
    for (int i=0; i < count; ++i) {
      dashes[i] = pattern[i];
    }

    ctx->pen.setDashPattern(dashes);
    ctx->pen.setDashOffset(offset);
  }
  ctx->dirtyFlags.emplace_back(cairo_t::DIRTY::PEN);
}

void cairo_set_font_size(cairo_t* ctx, int size)
{
  ctx->font.setPointSizeF(size);
  ctx->dirtyFlags.emplace_back(cairo_t::DIRTY::FONT);
}

void cairo_set_line_width(cairo_t* ctx, int width)
{
  ctx->pen.setWidthF(width == 0 ? 1.0 : width);
  ctx->dirtyFlags.emplace_back(cairo_t::DIRTY::PEN);
}

void cairo_set_line_cap(cairo_t* ctx, Qt::PenCapStyle cap) {
  ctx->pen.setCapStyle(cap);
  ctx->dirtyFlags.emplace_back(cairo_t::DIRTY::PEN);
}

void cairo_set_source_rgb(cairo_t* ctx, double r, double g, double b) {
  QColor c;
  c.setRedF(r);
  c.setGreenF(g);
  c.setBlueF(b);
  c.setAlphaF(1.0);
  ctx->setColor(c);
}

void cairo_set_source_rgba(cairo_t* ctx, double r, double g, double b, double a) {
  QColor c;
  c.setRedF(r);
  c.setGreenF(g);
  c.setBlueF(b);
  c.setAlphaF(a);
  ctx->setColor(c);
}

void cairo_surface_destroy(QImage* surface) {
  g_debug("~~~cairo_surface_destroy");
  delete surface;
}

void cairo_destroy(cairo_t* cairo) {
  g_debug("~~~cairo_destroy");
  delete cairo;
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
