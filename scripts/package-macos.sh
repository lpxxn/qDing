#!/usr/bin/env bash
# 使用 macOS 自带 Bash 3.2。所有路径均加引号，支持从任意目录启动和带空格的 Qt 路径。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
QT_ROOT_PATH="${QT_ROOT:-}"
OUTPUT_DIR="$ROOT/dist"
ARCH="$(uname -m)"
JOBS=4
SKIP_TESTS=0
SIGN_IDENTITY=""
NOTARY_PROFILE=""
STAGE=""

usage() {
    cat <<'HELP'
Usage: scripts/package-macos.sh --qt <Qt SDK> [options]
  --qt PATH                Qt macOS SDK directory (or set QT_ROOT)
  --output PATH            Artifact directory (default: project/dist)
  --arch arm64|x86_64|universal  Target architecture (default: host)
  --jobs N                 Parallel build jobs (default: 4)
  --skip-tests             Skip Qt Test; deployed app smoke check still runs
  --sign IDENTITY          Code-sign using an installed signing identity
  --notary-profile NAME    Notarize/staple with a saved Keychain profile; requires --sign
  -h, --help               Show help

Outputs: qDing-<version>-macOS-<arch>.dmg, .zip, .sha256
Without --sign, the app receives an ad-hoc signature for local distribution.
With --notary-profile, the signed app and DMG are submitted to Apple.
HELP
}
fail() { printf 'Error: %s\n' "$*" >&2; exit 1; }
require_value() { [[ $# -ge 2 && -n "$2" ]] || fail "Missing value for $1"; }
while [[ $# -gt 0 ]]; do
    case "$1" in
        --qt) require_value "$@"; QT_ROOT_PATH="$2"; shift 2 ;;
        --output) require_value "$@"; OUTPUT_DIR="$2"; shift 2 ;;
        --arch) require_value "$@"; ARCH="$2"; shift 2 ;;
        --jobs) require_value "$@"; JOBS="$2"; shift 2 ;;
        --skip-tests) SKIP_TESTS=1; shift ;;
        --sign) require_value "$@"; SIGN_IDENTITY="$2"; shift 2 ;;
        --notary-profile) require_value "$@"; NOTARY_PROFILE="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) fail "Unknown option: $1 (use --help)" ;;
    esac
done
[[ "$(uname -s)" == Darwin ]] || fail 'Run this script on macOS.'
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || fail '--jobs must be a positive integer.'
case "$ARCH" in
    arm64|x86_64) CMAKE_ARCH="$ARCH" ;;
    universal) CMAKE_ARCH='arm64;x86_64' ;;
    *) fail '--arch must be arm64, x86_64, or universal.' ;;
esac
[[ -n "$QT_ROOT_PATH" ]] || fail 'Provide --qt <Qt SDK> or set QT_ROOT.'
[[ -f "$QT_ROOT_PATH/lib/cmake/Qt6/Qt6Config.cmake" ]] || fail 'Qt SDK is invalid.'
QT_ROOT_PATH="$(cd "$QT_ROOT_PATH" && pwd -P)"
[[ -x "$QT_ROOT_PATH/bin/macdeployqt" ]] || fail 'macdeployqt is missing from the Qt SDK.'
[[ -z "$NOTARY_PROFILE" || -n "$SIGN_IDENTITY" ]] || fail '--notary-profile requires --sign.'
[[ -z "$NOTARY_PROFILE" || "$SIGN_IDENTITY" != '-' ]] || fail 'Notarization requires a Developer ID identity, not ad-hoc signing.'
for tool in cmake ctest ninja xcrun hdiutil diskutil ditto codesign file shasum unzip; do
    command -v "$tool" >/dev/null || fail "Required tool is missing: $tool"
done
if [[ -n "$NOTARY_PROFILE" ]]; then
    xcrun --find notarytool >/dev/null
    xcrun --find stapler >/dev/null
fi

mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd -P)"
BUILD_DIR="$ROOT/build/release-macos-$ARCH"
STAGE="$(mktemp -d "$OUTPUT_DIR/.qding-macos.XXXXXX")"
# 只清理本脚本创建的唯一临时目录；失败不会删除已有发布包。
trap 'if [[ -n "$STAGE" && -d "$STAGE" ]]; then rm -rf -- "$STAGE"; fi' EXIT
TESTING=ON
if [[ "$SKIP_TESTS" == 1 ]]; then TESTING=OFF; fi
cmake --fresh -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING="$TESTING" \
    -DCMAKE_PREFIX_PATH="$QT_ROOT_PATH" -DCMAKE_OSX_ARCHITECTURES="$CMAKE_ARCH"
cmake --build "$BUILD_DIR" --parallel "$JOBS"
if [[ "$SKIP_TESTS" != 1 ]]; then
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi
cmake --install "$BUILD_DIR" --prefix "$STAGE/install" --config Release
APP="$STAGE/install/qDing.app"
[[ -x "$APP/Contents/MacOS/qDing" ]] || fail 'Installed application is missing.'
# 不允许开发机的 Qt 插件环境变量掩盖部署缺失；使用临时数据、无声音启动。
env -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH \
    QT_QPA_PLATFORM=offscreen "$APP/Contents/MacOS/qDing" --smoke-test
VERSION="$(awk -F= '/^CMAKE_PROJECT_VERSION:STATIC=/{print $2}' "$BUILD_DIR/CMakeCache.txt")"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail 'Cannot read project version.'
NAME="qDing-$VERSION-macOS-$ARCH"

# 先签内部 Mach-O 和 framework，再签外层 app；正式签名不依赖 --deep。
SIGN_ARGS=(--force --sign "${SIGN_IDENTITY:--}")
if [[ -n "$SIGN_IDENTITY" ]]; then SIGN_ARGS+=(--options runtime --timestamp); fi
while IFS= read -r -d '' binary; do
    if file -b "$binary" | grep -q 'Mach-O'; then
        codesign "${SIGN_ARGS[@]}" "$binary"
    fi
done < <(find "$APP/Contents" -type f -print0)
while IFS= read -r -d '' framework; do
    codesign "${SIGN_ARGS[@]}" "$framework"
done < <(find "$APP/Contents" -depth -type d -name '*.framework' -print0)
codesign "${SIGN_ARGS[@]}" "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"

if [[ -n "$NOTARY_PROFILE" ]]; then
    ditto -c -k --keepParent "$APP" "$STAGE/notary.zip"
    xcrun notarytool submit "$STAGE/notary.zip" --keychain-profile "$NOTARY_PROFILE" \
        --wait --timeout 20m --output-format plist > "$STAGE/notary-result.plist"
    [[ "$(/usr/libexec/PlistBuddy -c 'Print :status' "$STAGE/notary-result.plist")" == Accepted ]] \
        || fail 'App notarization was not accepted. Check the submission in notarytool.'
    xcrun stapler staple "$APP"
    xcrun stapler validate "$APP"
fi
mkdir "$STAGE/dmg"
ditto "$APP" "$STAGE/dmg/qDing.app"
ln -s /Applications "$STAGE/dmg/Applications"
printf '将 qDing.app 拖入 Applications，然后从应用程序目录启动。\n版本：%s\n架构：%s\n' \
    "$VERSION" "$ARCH" > "$STAGE/dmg/安装说明.txt"
# 新系统优先使用 diskutil；旧 macOS 使用 hdiutil 的兼容接口。
if diskutil image create from --help >/dev/null 2>&1; then
    diskutil image create from --volumeName "qDing $VERSION" --format UDZO \
        "$STAGE/dmg" "$STAGE/$NAME.dmg"
else
    hdiutil create -volname "qDing $VERSION" -srcfolder "$STAGE/dmg" \
        -format UDZO -ov "$STAGE/$NAME.dmg"
fi
# 创建结束时磁盘映像服务可能短暂仍占用文件；只重试校验，损坏文件仍使打包失败。
VERIFIED=0
for attempt in 1 2 3; do
    if hdiutil verify "$STAGE/$NAME.dmg"; then VERIFIED=1; break; fi
    if [[ "$attempt" != 3 ]]; then sleep 2; fi
done
[[ "$VERIFIED" == 1 ]] || fail 'DMG verification failed.'
if [[ -n "$SIGN_IDENTITY" ]]; then
    codesign --force --sign "$SIGN_IDENTITY" --timestamp "$STAGE/$NAME.dmg"
fi
if [[ -n "$NOTARY_PROFILE" ]]; then
    xcrun notarytool submit "$STAGE/$NAME.dmg" --keychain-profile "$NOTARY_PROFILE" \
        --wait --timeout 20m --output-format plist > "$STAGE/dmg-notary-result.plist"
    [[ "$(/usr/libexec/PlistBuddy -c 'Print :status' "$STAGE/dmg-notary-result.plist")" == Accepted ]] \
        || fail 'DMG notarization was not accepted.'
    xcrun stapler staple "$STAGE/$NAME.dmg"
    xcrun stapler validate "$STAGE/$NAME.dmg"
fi
ditto -c -k --sequesterRsrc --keepParent "$APP" "$STAGE/$NAME.zip"
unzip -tq "$STAGE/$NAME.zip"
(cd "$STAGE" && shasum -a 256 "$NAME.dmg" "$NAME.zip" > "$NAME.sha256")
for extension in dmg zip sha256; do
    mv -f "$STAGE/$NAME.$extension" "$OUTPUT_DIR/$NAME.$extension"
done
printf '\nRelease artifacts:\n%s\n%s\n%s\n' \
    "$OUTPUT_DIR/$NAME.dmg" "$OUTPUT_DIR/$NAME.zip" "$OUTPUT_DIR/$NAME.sha256"
