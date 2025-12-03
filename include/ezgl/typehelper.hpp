#ifndef EZGL_TYPEHELPER_HPP
#define EZGL_TYPEHELPER_HPP

#ifdef EZGL_QT

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <iostream>

#include <QImage>
#include <QWidget>

class QObject;
class QWidget;
class QPushButton;
class QComboBox;
class QDialog;
class QWindow;
class QApplication;
class QPainter;

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
using cairo_t = QPainter;
using cairo_surface_t = QImage;

// cairo fake types
using mouse_callback_fn = void*;
using mouse_callback_fn = void*;
using key_callback_fn = void*;
// gtk fake types

#define TRUE 1
#define FALSE 0

int gtk_widget_get_allocated_width(QWidget* w) {
  return w->width();
}

int gtk_widget_get_allocated_height(QWidget* w) {
  return w->height();
}

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


#endif //EZGL_TYPEHELPER_HPP
