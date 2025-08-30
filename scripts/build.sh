#!/usr/bin/env bash
set -euo pipefail

# -------- settings / defaults --------
BUILD_TYPE="Debug"      # Default: Debug. Use --release for Release
RUN_AFTER_BUILD=0       # Use --run to run after building
CLEAN_FIRST=0           # Use --clean to wipe the build dir first
RECONFIGURE=0           # Use --reconfigure to force CMake reconfigure
INSTALL_DEPS=0          # Use --install-deps to apt-install common deps

# -------- paths --------
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SERVER_DIR="$REPO_ROOT/server"
BUILD_DIR="$SERVER_DIR/build"
BIN_NAME="krackerapp_server"
BIN_PATH="$BUILD_DIR/$BIN_NAME"

# -------- helpers --------
usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --debug              Build Debug (default)
  --release            Build Release (optimized)
  --run                Run the server after successful build
  --clean              Remove the build directory before building
  --reconfigure        Force CMake reconfigure
  --install-deps       apt install common build deps (cmake, toolchain, etc.)
  -h, --help           Show this help

Examples:
  $(basename "$0") --debug
  $(basename "$0") --release --run
  $(basename "$0") --clean --reconfigure --release
EOF
}

log()  { echo -e "\033[1;34m[build]\033[0m $*"; }
warn() { echo -e "\033[1;33m[warn]\033[0m  $*"; }
err()  { echo -e "\033[1;31m[err]\033[0m   $*" >&2; }

# -------- parse args --------
while [[ $# -gt 0 ]]; do
  case "$1" in
    --debug)        BUILD_TYPE="Debug" ;;
    --release)      BUILD_TYPE="Release" ;;
    --run)          RUN_AFTER_BUILD=1 ;;
    --clean)        CLEAN_FIRST=1 ;;
    --reconfigure)  RECONFIGURE=1 ;;
    --install-deps) INSTALL_DEPS=1 ;;
    -h|--help)      usage; exit 0 ;;
    *) err "Unknown option: $1"; usage; exit 1 ;;
  esac
  shift
done

# -------- preflight --------
if [[ ! -f "$SERVER_DIR/CMakeLists.txt" ]]; then
  err "Missing $SERVER_DIR/CMakeLists.txt. Are you in the right repo?"
  exit 1
fi

if [[ "$INSTALL_DEPS" -eq 1 ]]; then
  log "Installing common build dependencies (requires sudo)…"
  sudo apt update
  sudo apt install -y build-essential cmake git pkg-config libssl-dev zlib1g-dev
  warn "If Drogon isn't installed yet, install it (libdrogon-dev) or build from source."
fi

# -------- clean / prepare --------
if [[ "$CLEAN_FIRST" -eq 1 ]]; then
  log "Removing build directory: $BUILD_DIR"
  rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

# -------- configure --------
if [[ "$RECONFIGURE" -eq 1 || ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  log "Configuring CMake ($BUILD_TYPE)…"
  cmake -S "$SERVER_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
else
  log "CMake already configured. (Use --reconfigure to force)"
fi

# -------- build --------
CORES="$(nproc || echo 4)"
log "Building with $CORES threads…"
cmake --build "$BUILD_DIR" -j "$CORES"

# -------- run (optional) --------
if [[ "$RUN_AFTER_BUILD" -eq 1 ]]; then
  if [[ ! -f "$REPO_ROOT/server/config.json" ]]; then
    warn "server/config.json not found. The server can still run but DB config may be missing."
  fi
  log "Starting $BIN_NAME … (Ctrl+C to stop)"
  exec "$BIN_PATH"
else
  log "Build complete. Binary: $BIN_PATH"
fi
