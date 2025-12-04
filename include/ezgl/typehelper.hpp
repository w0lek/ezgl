#ifndef EZGL_TYPEHELPER_HPP
#define EZGL_TYPEHELPER_HPP

#ifdef EZGL_QT

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <memory>

#include <QObject>
#include <QApplication>
#include <QImage>
#include <QWidget>
#include <QWindow>
#include <QComboBox>
#include <QPushButton>
#include <QDialog>
#include <QPainter>
#include <QColor>

// gtk to std types
using gchar = char;
using gpointer = void*;
using gboolean = int;
using gint = int;

// gtk to qt types
using GObject = QObject;
using GtkWidget = QWidget;
using GtkButton = QPushButton;
using GtkComboBoxText = QComboBox;
using GtkDialog = QDialog;
using GtkApplication = QApplication;
using GdkWindow = QWindow;

// cairo fake types
struct cairo_t {
public:
  cairo_t(QImage* image): painter(image) {}
  QColor color;
  QPainter painter;
};

using cairo_surface_t = QImage;

// cairo fake types
using mouse_callback_fn = void*;
using mouse_callback_fn = void*;
using key_callback_fn = void*;
// gtk fake types

#define TRUE 1
#define FALSE 0

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
  qApp->exec();
}

void gtk_main_quit()
{
  QApplication::quit();
}

void g_application_quit(QApplication* app)
{
  app->exit(0);
}

QApplication* gtk_application_new(const char* appName)
{
  int argc = 0;
  char** argv = nullptr;
  QApplication* app = new QApplication(argc, argv);
  app->setApplicationName(appName);
  return app;
}

void gtk_widget_destroy(QWidget* widget)
{
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
  widget->update();
}

void g_free(void* ptr)
{
  free(ptr);
}

enum {
  GTK_RESPONSE_NONE         = -1,
  GTK_RESPONSE_REJECT       = -2,
  GTK_RESPONSE_ACCEPT       = -3,
  GTK_RESPONSE_DELETE_EVENT = -4,
  GTK_RESPONSE_OK           = -5,
  GTK_RESPONSE_CANCEL       = -6,
  GTK_RESPONSE_CLOSE        = -7,
  GTK_RESPONSE_YES          = -8,
  GTK_RESPONSE_NO           = -9,
  GTK_RESPONSE_APPLY        = -10,
  GTK_RESPONSE_HELP         = -11
};

// gtk wrapper

// cairo wrapper
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

#define g_return_val_if_fail(expr, val)      \
do {                                         \
      if (!(expr)) {                         \
        std::cerr << "CRITICAL: assertion '" \
        << #expr << "' failed at "           \
        << __FILE__ << ":" << __LINE__       \
        << std::endl;                        \
        return (val);                        \
  }                                          \
} while (0)

#define g_return_if_fail(expr)               \
do {                                         \
      if (!(expr)) {                         \
        std::cerr << "CRITICAL: assertion '" \
        << #expr << "' failed at "           \
        << __FILE__ << ":" << __LINE__       \
        << std::endl;                        \
        return;                              \
  }                                          \
} while (0)

// Core logging function (printf-style)
inline void log_message(const char* level,
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

// Macros similar to g_info / g_warning
#define g_info(fmt, ...)    \
  log_message("INFO",    __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define g_warning(fmt, ...) \
  log_message("WARNING", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif // EZGL_QT

#define g_error(fmt, ...) \
log_message("ERROR", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define TODO() \
  std::cerr << "TODO:" \
            << __FILE__ << ":" << __LINE__ \
            << std::endl; \
  assert(false); \



#endif //EZGL_TYPEHELPER_HPP
