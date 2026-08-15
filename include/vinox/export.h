#ifndef VINOX_EXPORT_H
#define VINOX_EXPORT_H

#if defined(_WIN32)
#  if defined(VINOX_BUILD_SHARED)
#    define VINOX_API __declspec(dllexport)
#  else
#    define VINOX_API __declspec(dllimport)
#  endif
#else
#  define VINOX_API __attribute__((visibility("default")))
#endif

#endif
