#ifndef CTAR_VERSION_INTERNAL_H
#define CTAR_VERSION_INTERNAL_H

// The version constants live in the public header, which is the single source
// of truth parsed by cmake/utils.cmake. This forwarding header keeps the
// existing internal includes working without a second definition.
#include <ctar/version.h>

#endif // CTAR_VERSION_INTERNAL_H
