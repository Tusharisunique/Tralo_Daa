#!/bin/bash
# Start script for the full TRALO exchange: builds backend + launches server.

set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
BACKEND="$ROOT/Backend"

echo "=== TRALO Exchange ==="

# --- Step 1: Build the C++ engine ---
echo "[1/3] Building C++ engine..."
# Clear build dir if it exists but is invalid (e.g. from another machine)
if [ -d "$BACKEND/build" ]; then
    cmake -S "$BACKEND" -B "$BACKEND/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null 2>&1 || {
        echo "  [!] Build cache invalid, performing clean build..."
        rm -rf "$BACKEND/build"
    }
fi
cmake -S "$BACKEND" -B "$BACKEND/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null
cmake --build "$BACKEND/build" --parallel $(sysctl -n hw.ncpu)
# Copy compile_commands.json to Backend root so VS Code clangd works
cp "$BACKEND/build/compile_commands.json" "$BACKEND/" 2>/dev/null || true
echo "[1/3] Build complete."

# --- Step 2: Setup Python env if missing ---
echo "[2/3] Checking Python environment..."
if [ ! -d "$ROOT/.venv" ]; then
    echo "  Creating virtual environment..."
    python3 -m venv "$ROOT/.venv"
fi
"$ROOT/.venv/bin/python" -m pip install websockets -q
echo "[2/3] Python environment ready."

# --- Step 3: Kill anything already on our ports ---
echo "[3/3] Clearing ports 8080 and 8081..."
lsof -ti:8080,8081 | xargs kill -9 2>/dev/null || true
sleep 0.5

# --- Launch ---
echo ""
echo "Starting exchange..."
echo "  Frontend → http://localhost:8080"
echo "  Engine   → ws://localhost:8081"
echo ""
cd "$BACKEND"
"$ROOT/.venv/bin/python" server.py
