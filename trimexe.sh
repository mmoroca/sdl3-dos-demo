#!/bin/bash
if [ -z "$1" ]; then
    echo "trimexe.sh: purge the debug information from .exe files in a directory."
    echo "Usage: $0 <directory>"
    echo "Example: $0 build-dos-small"
    exit 1
fi

DIR="$1"

if [ ! -d "$DIR" ]; then
    echo "Error: the directory '$DIR' does not exist."
    exit 1
fi

for f in "$DIR"/*.exe; do
    [ -e "$f" ] || continue
    base="$(basename "$f" .exe)"
    cp "$DIR/$base.exe" "$DIR/${base}str.exe"
    i386-pc-msdosdjgpp-strip "$DIR/${base}str.exe"
    echo "$DIR/${base}str.exe done"
done   