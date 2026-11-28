#!/usr/bin/env bash
# Verify that `cmake --install` produces a package a downstream project can use.
#
# Usage: scripts/consumer_smoke_test.sh <build-dir>
#
# The script installs the build into a private temporary prefix, configures and
# builds test/consumer against it, runs the binary, and checks that lz4 is
# bundled into libctar.a (the installed library must not require a separate lz4).
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <build-dir>" >&2
    exit 2
fi

build_dir=$(cd "$1" && pwd -P)
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)

tmp_root="${TMPDIR:-/tmp}/ctar-consumer-$(id -u)"
umask 077
rm -rf "$tmp_root"
mkdir -p "$tmp_root"
cleanup() { rm -rf "$tmp_root"; }
trap cleanup EXIT

prefix="$tmp_root/prefix"
consumer_build="$tmp_root/consumer-build"

echo "== installing to $prefix =="
cmake --install "$build_dir" --prefix "$prefix" >/dev/null

echo "== configuring and building the consumer =="
cmake -S "$repo_root/test/consumer" -B "$consumer_build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$prefix" >/dev/null
cmake --build "$consumer_build" >/dev/null

echo "== running the consumer =="
"$consumer_build/ctar_consumer"

echo "== checking that no test framework was installed =="
pollution=$(find "$prefix" \( -name '*gtest*' -o -name '*gmock*' \) -print)
if [[ -n "$pollution" ]]; then
    echo "error: test framework files were installed:" >&2
    echo "$pollution" >&2
    exit 1
fi

echo "== checking pkg-config =="
if command -v pkg-config >/dev/null 2>&1; then
    pc_file=$(find "$prefix" -name 'ctar.pc' -type f -print -quit)
    if [[ -z "$pc_file" ]]; then
        echo "error: ctar.pc was not installed" >&2
        exit 1
    fi
    pc_dir=$(dirname "$pc_file")
    pkg_cflags=$(PKG_CONFIG_PATH="$pc_dir" pkg-config --cflags ctar)
    pkg_libs=$(PKG_CONFIG_PATH="$pc_dir" pkg-config --static --libs ctar)
    # The prefix is expressed relative to the .pc file, so normalize before
    # comparing it with the install prefix.
    pc_prefix=$(PKG_CONFIG_PATH="$pc_dir" pkg-config --variable=prefix ctar)
    prefix_real=$(cd "$prefix" && pwd -P)
    pc_prefix_real=$(cd "$pc_prefix" 2>/dev/null && pwd -P) || pc_prefix_real=""
    if [[ "$pc_prefix_real" != "$prefix_real" ]]; then
        echo "error: pkg-config prefix '$pc_prefix' does not resolve to '$prefix'" >&2
        exit 1
    fi
    read -r -a pc_cflags <<< "$pkg_cflags"
    read -r -a pc_libs <<< "$pkg_libs"
    "${CXX:-c++}" "${pc_cflags[@]}" "$repo_root/test/consumer/main.cpp" "${pc_libs[@]}" \
        -o "$tmp_root/pkgconfig-consumer"
    "$tmp_root/pkgconfig-consumer"
else
    echo "pkg-config not found; skipping the pkg-config check"
fi

echo "== checking that lz4 is bundled =="
archive=$(find "$prefix" -name 'libctar.a' -type f -print -quit)
if [[ -z "$archive" ]]; then
    echo "error: libctar.a not found under $prefix" >&2
    exit 1
fi
# Capture first: `nm | grep -q` would make nm die on SIGPIPE, and `pipefail`
# would turn that into a false failure even when the symbol is present.
symbols=$(nm -g "$archive")
if [[ "$symbols" != *LZ4_compress* ]]; then
    echo "error: lz4 symbols are not bundled into $archive" >&2
    exit 1
fi

echo "consumer smoke test: ok"
