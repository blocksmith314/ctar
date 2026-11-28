#ifndef CTAR_CTAR_PUBLIC_H
#define CTAR_CTAR_PUBLIC_H

// Umbrella header for the installed ctar library.
//
// Only public, self-contained headers belong here. Internal headers under
// src/ and api/ are not installed and must never be included from this file.
//
// The public surface currently exposes version information only; the archive
// read API (ctar::ArchiveReader) is tracked in issue #101 and will be added
// here without changing the install layout.

#include <ctar/version.h>

#endif // CTAR_CTAR_PUBLIC_H
