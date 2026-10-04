
#ifndef PACK_EXPORT_H
#define PACK_EXPORT_H

#ifdef _WIN32
#ifdef CTAR_BUILD_LIB
#define CTAR_API __declspec(dllexport)
#else
#define  CTAR_API __declspec(dllimport)
#endif
#else
#define CTAR_API
#endif


#endif // PACK_EXPORT_H
