#!/usr/bin/env bash
# Package the Linux build into the portable release tarball used by the "Downloads" table:
#
#   PosixSCP-linux-x86_64.tar.gz     (or -arm64 on aarch64)
#     linux-x86_64/
#       posixscp.sh        launcher (sets LD_LIBRARY_PATH / QT_PLUGIN_PATH, then execs posixscp)
#       posixscp           the GUI
#       lib/               bundled Qt6 + shared-lib closure (NOT glibc/GL/driver libs)
#       plugins/           xcb platform, xcb-glx/egl, image-format and TLS plugins
#       PosixSCP.desktop   desktop entry
#       posixscp.png       icon
#
# Runs on a stock desktop without qt6-base installed. glibc and the GPU/X driver stack are
# deliberately NOT bundled (they must come from the host).
#
# Prereqs: a finished build (native/scripts/build-linux.sh).
# Usage: package-linux-portable.sh [output dir]      (default: repo root)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NATIVE="$ROOT/native"
BUILD="$NATIVE/build-linux"
OUT_DIR="${1:-$ROOT}"
mkdir -p "$OUT_DIR"

BIN="$BUILD/ui-qt/posixscp"
[ -x "$BIN" ] || BIN="$BUILD/ui-qt/winscp-qt"
[ -x "$BIN" ] || { echo "build first: native/scripts/build-linux.sh"; exit 1; }

RAW_ARCH="$(uname -m)"                  # x86_64 | aarch64
case "$RAW_ARCH" in
  aarch64|arm64) ARCH="arm64" ;;
  x86_64|amd64)  ARCH="x86_64" ;;
  *) echo "unsupported architecture: $RAW_ARCH"; exit 1 ;;
esac
QTLIB="/usr/lib/${RAW_ARCH}-linux-gnu"
QTPLUGINS="$QTLIB/qt6/plugins"

STAGE_ROOT="$BUILD/portable"
STAGE="$STAGE_ROOT/linux-$ARCH"
# Absolute, because tar's -f path is resolved after its -C: a relative one would land inside STAGE_ROOT.
OUT="$(cd "$OUT_DIR" && pwd)/PosixSCP-linux-$ARCH.tar.gz"

# Libs that MUST come from the host (glibc core + GPU/X driver stack) — never bundle these.
is_excluded() {
  case "$1" in
    ld-linux*|libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|libresolv.so*|\
    libGL.so*|libGLX.so*|libGLdispatch.so*|libEGL.so*|libOpenGL.so*|libdrm.so*|\
    libgbm.so*|libglapi.so*) return 0 ;;
    *) return 1 ;;
  esac
}

# Recursively collect the .so closure of a binary into $STAGE/lib.
collect_libs() {
  local target="$1"
  ldd "$target" 2>/dev/null | awk '/=> \//{print $3}' | while read -r so; do
    [ -f "$so" ] || continue
    local base; base="$(basename "$so")"
    is_excluded "$base" && continue
    [ -f "$STAGE/lib/$base" ] && continue
    cp -L "$so" "$STAGE/lib/$base"
    collect_libs "$so"
  done
}

echo "Staging $STAGE ..."
rm -rf "$STAGE" "$OUT"
mkdir -p "$STAGE/lib" "$STAGE/plugins/platforms" "$STAGE/plugins/imageformats"
cp "$BIN" "$STAGE/posixscp"

# Qt platform plugin (xcb) + xcb GL integrations, image formats and the TLS backend for Qt Network.
cp "$QTPLUGINS/platforms/libqxcb.so" "$STAGE/plugins/platforms/"
if [ -d "$QTPLUGINS/xcbglintegrations" ]; then
  mkdir -p "$STAGE/plugins/xcbglintegrations"
  cp "$QTPLUGINS"/xcbglintegrations/*.so "$STAGE/plugins/xcbglintegrations/" 2>/dev/null || :
fi
for img in libqjpeg.so libqgif.so libqico.so libqsvg.so; do
  [ -f "$QTPLUGINS/imageformats/$img" ] && cp "$QTPLUGINS/imageformats/$img" "$STAGE/plugins/imageformats/"
done
if [ -d "$QTPLUGINS/tls" ]; then
  mkdir -p "$STAGE/plugins/tls"
  cp "$QTPLUGINS"/tls/*.so "$STAGE/plugins/tls/" 2>/dev/null || :
fi

collect_libs "$STAGE/posixscp"
collect_libs "$STAGE/plugins/platforms/libqxcb.so"
for so in "$STAGE"/plugins/*/*.so; do collect_libs "$so"; done

ICON_SRC="$NATIVE/ui-qt/posixscp.png"
[ -f "$ICON_SRC" ] || ICON_SRC="$ROOT/icon.png"
cp "$ICON_SRC" "$STAGE/posixscp.png"

cat > "$STAGE/PosixSCP.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=PosixSCP
Comment=SFTP, SCP, and FTP client for POSIX systems
Exec=posixscp.sh
Icon=posixscp
Categories=Network;FileTransfer;Utility;
Terminal=false
EOF

cat > "$STAGE/posixscp.sh" <<'EOF'
#!/bin/sh
HERE="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$HERE/lib:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="$HERE/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$HERE/plugins/platforms"
exec "$HERE/posixscp" "$@"
EOF
chmod +x "$STAGE/posixscp.sh" "$STAGE/posixscp"

echo "Creating tarball ..."
tar -C "$STAGE_ROOT" -czf "$OUT" "linux-$ARCH"
echo
echo "Built: $OUT ($(du -h "$OUT" | awk '{print $1}'))"
