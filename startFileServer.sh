#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXE_DIR="$SCRIPT_DIR/exe/linux"
BIN_DIR="$SCRIPT_DIR/build/bin"
LOG_DIR="$EXE_DIR/logs"

mkdir -p "$LOG_DIR"

if [ ! -f "$BIN_DIR/SwgFileControl" ]; then
    echo "ERROR: $BIN_DIR/SwgFileControl not found. Run compile_src first."
    exit 1
fi

if [ ! -f "$EXE_DIR/SwgFileControl.cfg" ]; then
    echo "ERROR: $EXE_DIR/SwgFileControl.cfg not found."
    exit 1
fi

cd "$EXE_DIR"

echo "Starting SwgFileControl file server..."
echo "  Binary:  $BIN_DIR/SwgFileControl"
echo "  Config:  $EXE_DIR/SwgFileControl.cfg"
echo "  Logs:    $LOG_DIR/filecontrol.log"

nohup "$BIN_DIR/SwgFileControl" -- @SwgFileControl.cfg > "$LOG_DIR/filecontrol.log" 2>&1 &
FC_PID=$!

echo "SwgFileControl started with PID $FC_PID"
echo "$FC_PID" > "$EXE_DIR/SwgFileControl.pid"
