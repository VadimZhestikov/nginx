#!/usr/bin/env bash
# build-njs.sh — build a release nginx binary with the njs module
#
# The resulting binary is placed at: perf/njs-nginx/objs/nginx
#
# Prerequisites: both nginx and njs sources must be present as sibling
# directories of this repo root (the layout this workspace uses).
#
# Usage (from repo root or perf/ directory):
#   bash perf/build-njs.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

NGINX_SRC="$REPO_ROOT"           # the nginx source tree (this repo)
NJS_SRC="$REPO_ROOT/../njs"      # njs source tree (sibling repo)
BUILD_DIR="$SCRIPT_DIR/njs-nginx"

if [ ! -d "$NJS_SRC/nginx" ]; then
    echo "ERROR: njs nginx module not found at $NJS_SRC/nginx"
    echo "  Clone njs next to this repo: git clone https://github.com/nginx/njs ../njs"
    exit 1
fi

echo "── Building njs ─────────────────────────────────────────────"
echo "  njs source: $NJS_SRC"
make -C "$NJS_SRC" 2>&1 | tail -5
echo "  libnjs.a: OK"

echo ""
echo "── Configuring nginx with njs module ───────────────────────"
echo "  nginx source: $NGINX_SRC"
echo "  output:       $BUILD_DIR"
echo ""

mkdir -p "$BUILD_DIR"

cd "$NGINX_SRC"

# The njs nginx/config script locates libnjs.a and headers automatically via
# $ngx_addon_dir/../build/ — no manual --with-cc-opt / --with-ld-opt needed.
auto/configure \
    --builddir="$BUILD_DIR/objs" \
    --prefix=/tmp/njs-nginx-perf \
    --with-http_ssl_module \
    --with-http_realip_module \
    --with-http_sub_module \
    --with-http_auth_request_module \
    --with-http_addition_module \
    --with-stream \
    --add-module="$NJS_SRC/nginx" \
    2>&1 | tail -10

echo ""
echo "── Building nginx ────────────────────────────────────────────"
make -j"$(nproc)" 2>&1 | tail -5

echo ""
echo "── Done ──────────────────────────────────────────────────────"
echo "  Binary: $BUILD_DIR/objs/nginx"
echo ""
echo "  To run the benchmark:"
echo "    cd perf"
echo "    bash bench.sh --njs=$BUILD_DIR/objs/nginx"
