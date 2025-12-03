#ifndef EZGL_TYPEHELPER_HPP
#define EZGL_TYPEHELPER_HPP

#ifdef EZGL_QT

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <iostream>

class QWidget;
class QObject;
class QComboBox;
class QDialog;
class QApplication;

using gchar = char;
using GObject = QObject;
using gpointer = void*;
using gboolean = int;
using GtkWidget = QWidget;
using GtkComboBoxText = QComboBox;
using GtkDialog = QDialog;
using GtkApplication = QApplication;

#define TRUE 1
#define FALSE 0

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

#define TODO() \
  std::cerr << "TODO:" \
            << __FILE__ << ":" << __LINE__ \
            << std::endl; \


#endif //EZGL_TYPEHELPER_HPP
