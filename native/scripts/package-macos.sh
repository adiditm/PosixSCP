#!/usr/bin/env bash
# Package the staged UNIVERSAL PosixSCP.app (made by build-macos-universal.sh, Qt already deployed)
# into one thin .app per architecture, zipped for the release:
#
#   PosixSCP-macos-arm64.zip    Apple Silicon (M1/M2/M3/M4)
#   PosixSCP-macos-intel.zip    Intel (x86_64)
#
# Every Mach-O file in the bundle (main executable, Qt frameworks, plugins, dylibs) is lipo-thinned
# to the target architecture and the bundle is signed ad-hoc again - an arm64 Mac refuses to run an
# unsigned/modified binary, and thinning invalidates the signature macdeployqt left behind.
#
# Usage: package-macos.sh <version> <path/to/PosixSCP.app> <output dir>
set -euo pipefail

VERSION="${1:?usage: package-macos.sh <version> <PosixSCP.app> <output dir>}"
APP_SRC="${2:?usage: package-macos.sh <version> <PosixSCP.app> <output dir>}"
OUT_DIR="${3:?usage: package-macos.sh <version> <PosixSCP.app> <output dir>}"

[ -d "$APP_SRC" ] || { echo "no app bundle at $APP_SRC"; exit 1; }
HAVE_VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP_SRC/Contents/Info.plist")"
[ "$HAVE_VERSION" = "$VERSION" ] || { echo "bundle is version $HAVE_VERSION, expected $VERSION"; exit 1; }
[ -d "$APP_SRC/Contents/Frameworks/QtCore.framework" ] || { echo "$APP_SRC has no bundled Qt"; exit 1; }

mkdir -p "$OUT_DIR"

# Thin a copy of the bundle to one architecture, in place. Fails loudly if a Mach-O has no such slice.
thin_bundle() {
  local src="$1" dst="$2" arch="$3"
  rm -rf "$dst"
  ditto "$src" "$dst"

  local errors; errors="$(mktemp)"
  find "$dst" -type f -print0 | while IFS= read -r -d '' f; do
    file -b "$f" 2>/dev/null | grep -q 'Mach-O' || continue
    local archs; archs="$(lipo -archs "$f" 2>/dev/null)" || continue
    case " $archs " in
      *" $arch "*) ;;
      *) echo "no $arch slice: $f ($archs)" >> "$errors"; continue ;;
    esac
    # A single-slice file is already what we want; only fat files get thinned.
    if [ "$(printf '%s\n' $archs | wc -l | tr -d ' ')" -gt 1 ]; then
      local tmp="$f.thin"
      lipo -thin "$arch" -output "$tmp" "$f"
      chmod "$(stat -f '%Lp' "$f")" "$tmp"
      mv "$tmp" "$f"
    fi
  done
  if [ -s "$errors" ]; then cat "$errors"; rm -f "$errors"; return 1; fi
  rm -f "$errors"

  codesign --force --deep --sign - "$dst" >/dev/null
  codesign --verify --deep --strict "$dst" >/dev/null
}

package_arch() {
  local arch="$1" name="$2" work; work="$(mktemp -d)"
  local app="$work/PosixSCP.app"
  echo "==> $arch"
  thin_bundle "$APP_SRC" "$app" "$arch"

  local bin="$app/Contents/MacOS/PosixSCP"
  echo "    architectures: $(lipo -archs "$bin")"
  [ "$(lipo -archs "$bin")" = "$arch" ] || { echo "    not thin ($arch)"; exit 1; }

  # Every Mach-O in the bundle must be a single slice now, and the executable must find Qt through
  # the bundle-relative rpath macdeployqt installed (a leftover absolute rpath means a broken app).
  local bad; bad="$(mktemp)"
  find "$app" -type f -print0 | while IFS= read -r -d '' f; do
    file -b "$f" 2>/dev/null | grep -q 'Mach-O' || continue
    local a; a="$(lipo -archs "$f" 2>/dev/null)" || continue
    [ "$a" = "$arch" ] || echo "$a: $f" >> "$bad"
  done
  if [ -s "$bad" ]; then echo "    not thinned to $arch:"; cat "$bad"; rm -f "$bad"; exit 1; fi
  rm -f "$bad"
  otool -l "$bin" | grep -A2 LC_RPATH | grep -q '@executable_path/../Frameworks' \
    || { echo "    missing @executable_path/../Frameworks rpath"; exit 1; }

  # The thinned bundle must not reference anything outside itself/the system.
  if otool -L "$bin" "$app"/Contents/Frameworks/*.framework/Versions/A/Qt* 2>/dev/null \
       | grep -E '^\s+/' | grep -vE '^\s+(/usr/lib/|/System/Library/)' ; then
    echo "    non-system absolute library reference found (above)"; exit 1
  fi

  local zip="$OUT_DIR/PosixSCP-macos-$name.zip"
  rm -f "$zip"
  ditto -c -k --sequesterRsrc --keepParent "$app" "$zip"
  echo "    $zip ($(du -h "$zip" | awk '{print $1}'))"
  rm -rf "$work"
}

package_arch arm64 arm64
package_arch x86_64 intel
