#ifndef EZGL_GTKCOMPAT_HPP
#define EZGL_GTKCOMPAT_HPP

#ifdef EZGL_QT

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <memory>
#include <string_view>

#include <QObject>
#include <QApplication>
#include <QImage>
#include <QWidget>
#include <QWindow>
#include <QComboBox>
#include <QPushButton>
#include <QDialog>
#include <QPainter>
#include <QPainterPath>
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
  QPainterPath path;
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
QWidget* GTK_WIDGET(QObject* obj);
QComboBox* GTK_COMBO_BOX(QObject* obj);
QWindow* GTK_WINDOW(QObject* obj);
QApplication* G_APPLICATION(QObject* obj);

bool GTK_IS_BUTTON(QObject* obj);
QWidget* gtk_application_get_active_window(QApplication* app);
void gtk_main();
void gtk_main_quit();

void g_application_quit(QApplication* app);
QApplication* gtk_application_new(const char* appName);
void gtk_widget_destroy(QWidget* widget);
int gtk_widget_get_allocated_width(QWidget* w);
int gtk_widget_get_allocated_height(QWidget* w);
char* gtk_combo_box_text_get_active_text(QComboBox* combo);
void gtk_combo_box_set_active(QComboBox* combo, int idx);
void gtk_widget_queue_draw(QWidget* widget);

void g_free(void* ptr);

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
#define CAIRO_LINE_CAP_BUTT	Qt::FlatCap
#define CAIRO_LINE_CAP_ROUND Qt::RoundCap
#define CAIRO_LINE_CAP_SQUARE	Qt::SquareCap
using cairo_line_cap_t = Qt::PenCapStyle;

#define CAIRO_FONT_SLANT_NORMAL QFont::StyleNormal
#define CAIRO_FONT_SLANT_ITALIC QFont::StyleItalic
#define CAIRO_FONT_SLANT_OBLIQUE QFont::StyleOblique
using cairo_font_slant_t = QFont::Style;

#define CAIRO_FONT_WEIGHT_NORMAL QFont::Normal
#define CAIRO_FONT_WEIGHT_BOLD QFont::Bold
using cairo_font_weight_t = QFont::Weight;

int cairo_image_surface_get_width(QImage* image);
int cairo_image_surface_get_height(QImage* image);
void cairo_new_path(cairo_t* ctx);
void cairo_scale(cairo_t* ctx, double sx, double sy);
void cairo_save(cairo_t* ctx);
void cairo_restore(cairo_t* ctx);
void cairo_fill(cairo_t* ctx);
void cairo_close_path(cairo_t* ctx);
void cairo_stroke(cairo_t* ctx);
void cairo_move_to(cairo_t* ctx, double x, double y);
void cairo_line_to(cairo_t* ctx, double x, double y);
void cairo_arc(cairo_t* cr, double xc, double yc, double radius, double angle1, double angle2);
void cairo_arc_negative(cairo_t* ctx, double xc, double yc, double radius, double angle1, double angle2);

void cairo_select_font_face(cairo_t* ctx, const char* family, cairo_font_slant_t slant, cairo_font_weight_t weight);
void cairo_set_dash(cairo_t* ctx, const qreal* pattern, int count, qreal offset);
void cairo_set_font_size(cairo_t* ctx, int size);
void cairo_set_line_width(cairo_t* ctx, int width);
void cairo_set_line_cap(cairo_t* ctx, Qt::PenCapStyle cap);
void cairo_set_source_rgb(cairo_t* ctx, double r, double g, double b);
void cairo_set_source_rgba(cairo_t* ctx, double r, double g, double b, double a);
void cairo_paint(cairo_t* ctx);
void cairo_surface_destroy(QImage* surface);
void cairo_destroy(cairo_t* cairo);
void cairo_set_source_surface(cairo_t* cairo, QImage* surface, double x, double y);
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

void log_message(const char* level, const char* file, int line, const char* fmt, ...);

constexpr const char* __filename_helper(const char* path)
{
  const char* file = path;
  for (const char* p = path; *p != '\0'; ++p) {
    if (*p == '/' || *p == '\\') {
      file = p + 1;
    }
  }
  return file;
}

#define __FILENAME__ (__filename_helper(__FILE__))

// Macros similar to g_info / g_warning
#define g_info(fmt, ...)    \
  log_message("INFO",    __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)

#define g_warning(fmt, ...) \
  log_message("WARNING", __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)

#define g_error(fmt, ...) \
  log_message("ERROR", __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)

#define g_debug(fmt, ...) \
  log_message("DEBUG", __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)

#define TODO() \
  std::cerr << "TODO:" \
            << __FILENAME__ << ":" << __LINE__ \
            << std::endl; \
  assert(false); \
                                         \

#endif // EZGL_QT
#endif // EZGL_GTKCOMPAT_HPP
