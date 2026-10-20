#!/usr/bin/env bash
# Create stripped copies of DOS executables in one directory.

PROGRAM=${0##*/}
STRIP_TOOL=${STRIP_TOOL:-i386-pc-msdosdjgpp-strip}
ACTIVE_TEMP=

usage() {
    cat <<EOF
Usage: $PROGRAM [--help] [--] <directory>

Create a stripped copy of each .exe/.EXE file directly inside <directory>.
The original is kept unchanged; output names add the suffix "str.exe"
(e.g. game.exe -> gamestr.exe). Files already ending in "str.exe" are
considered generated outputs and are skipped on later runs.

Options:
  -h, --help   Show this help message.

Environment:
  STRIP_TOOL   Override the strip program (default: $STRIP_TOOL).
EOF
}

cleanup() {
    if [[ -n "$ACTIVE_TEMP" && -e "$ACTIVE_TEMP" ]]; then
        rm -f "$ACTIVE_TEMP"
    fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [[ $# -eq 1 && ( "$1" == "-h" || "$1" == "--help" ) ]]; then
    usage
    exit 0
fi

if [[ $# -eq 0 ]]; then
    printf '%s\n' "Error: missing directory argument." >&2
    usage >&2
    exit 2
fi

ALLOW_DASH_DIR=0
if [[ "$1" == "--" ]]; then
    shift
    ALLOW_DASH_DIR=1
fi
if [[ $# -ne 1 ]]; then
    printf '%s\n' "Error: expected exactly one directory argument." >&2
    usage >&2
    exit 2
fi
if [[ "$1" == -* && $ALLOW_DASH_DIR -eq 0 ]]; then
    printf 'Error: unknown option: %s\n' "$1" >&2
    usage >&2
    exit 2
fi

DIR=$1
if [[ ! -d "$DIR" ]]; then
    printf "Error: directory does not exist: %s\n" "$DIR" >&2
    exit 1
fi

# Prefix relative paths so names beginning with '-' cannot be parsed as options.
if [[ "$DIR" != /* ]]; then
    DIR=./$DIR
fi

# Handle .exe and .EXE on case-sensitive hosts; arrays preserve spaces/newlines.
shopt -s nullglob nocaseglob nocasematch
candidates=("$DIR"/*.exe)
source_files=()
for file in "${candidates[@]}"; do
    [[ -f "$file" ]] || continue
    filename=${file##*/}
    if [[ "$filename" == *str.exe ]]; then
        continue
    fi
    source_files+=("$file")
done

if [[ ${#source_files[@]} -eq 0 ]]; then
    printf 'No source .exe files found in: %s\n' "$1"
    exit 0
fi

if ! command -v "$STRIP_TOOL" >/dev/null 2>&1; then
    printf 'Error: strip tool not found: %s\n' "$STRIP_TOOL" >&2
    printf 'Install the DJGPP binutils or set STRIP_TOOL to its path.\n' >&2
    exit 1
fi

# Catch case-insensitive output collisions before writing any copies.
output_names=()
for file in "${source_files[@]}"; do
    filename=${file##*/}
    base=${filename%.*}
    output_name=${base}str.exe
    for seen_name in "${output_names[@]}"; do
        if [[ "$output_name" == "$seen_name" ]]; then
            printf 'Error: output-name collision for %s\n' "$output_name" >&2
            exit 1
        fi
    done
    output_names+=("$output_name")
done

failures=0
for file in "${source_files[@]}"; do
    filename=${file##*/}
    base=${filename%.*}
    output="$DIR/${base}str.exe"
    if [[ -d "$output" ]]; then
        printf 'Error: output path is a directory: %s\n' "$output" >&2
        failures=1
        continue
    fi

    ACTIVE_TEMP=$(mktemp "$DIR/.trimexe.XXXXXX") || {
        printf 'Error: could not create a temporary file in %s\n' "$DIR" >&2
        failures=1
        continue
    }

    if ! cp -p "$file" "$ACTIVE_TEMP"; then
        printf 'Error: could not copy %s\n' "$file" >&2
        rm -f "$ACTIVE_TEMP"
        ACTIVE_TEMP=
        failures=1
        continue
    fi
    if ! chmod u+w "$ACTIVE_TEMP"; then
        printf 'Error: could not make the temporary copy writable: %s\n' "$ACTIVE_TEMP" >&2
        rm -f "$ACTIVE_TEMP"
        ACTIVE_TEMP=
        failures=1
        continue
    fi

    # Match GNU strip's default behavior explicitly: remove the symbol table,
    # including debug symbols, from the copy rather than changing the original.
    if ! "$STRIP_TOOL" --strip-all -- "$ACTIVE_TEMP"; then
        printf 'Error: strip failed for %s; existing output was left untouched.\n' "$file" >&2
        rm -f "$ACTIVE_TEMP"
        ACTIVE_TEMP=
        failures=1
        continue
    fi

    if ! mv -f "$ACTIVE_TEMP" "$output"; then
        printf 'Error: could not install stripped copy: %s\n' "$output" >&2
        rm -f "$ACTIVE_TEMP"
        ACTIVE_TEMP=
        failures=1
        continue
    fi

    ACTIVE_TEMP=
    printf '%s -> %s done\n' "$file" "$output"
done

exit "$failures"
