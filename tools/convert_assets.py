#!/usr/bin/env python3
"""Convert non-interlaced PNG trees to 8-bit indexed BMPs with DOS 8.3 paths.

All BMPs share the same 256-color RGB 3:3:2 palette, a VGA-style palette for
8-bit indexed graphics. Source dimensions are retained; there is no fixed
320x200 screen-size limit beyond classic BMP format and available memory. The
tool uses only Python's standard library.

Subdirectory structure is preserved. Long or colliding names receive stable
uppercase ``~n`` aliases, for example ``Long artwork.png`` may become
``LONG_A~1.BMP``. Source and destination directories are explicit arguments.
"""
from argparse import ArgumentParser, ArgumentTypeError
from pathlib import Path
import os
import re
import struct
import tempfile
import unicodedata
import zlib

MAX_DOS_STEM = 8
DOS_RESERVED_NAMES = frozenset(
    {"CON", "PRN", "AUX", "NUL", "CLOCK$"}
    | {f"COM{number}" for number in range(1, 10)}
    | {f"LPT{number}" for number in range(1, 10)}
)


def make_332_palette():
    """Build a fixed 256-entry RGB palette with 3 red, 3 green, 2 blue bits."""
    palette = []
    for index in range(256):
        red_level = (index >> 5) & 0x07
        green_level = (index >> 2) & 0x07
        blue_level = index & 0x03
        red = (red_level * 255 + 3) // 7
        green = (green_level * 255 + 3) // 7
        blue = (blue_level * 255 + 1) // 3
        palette.append((red, green, blue))
    return palette


PALETTE = make_332_palette()


def paeth(a, b, c):
    """Return the PNG Paeth predictor: the neighbor nearest a+b-c."""
    prediction = a + b - c
    distance_a = abs(prediction - a)
    distance_b = abs(prediction - b)
    distance_c = abs(prediction - c)
    if distance_a <= distance_b and distance_a <= distance_c:
        return a
    if distance_b <= distance_c:
        return b
    return c


def read_png(path):
    """Decode a supported non-interlaced PNG into top-to-bottom scanlines.

    The returned tuple contains dimensions, bit depth, color type, optional
    palette/transparency chunks, and reconstructed byte rows. Supported PNG
    color types are grayscale, true color, indexed, grayscale-alpha, and RGBA.
    """
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"Not a PNG file: {path}")

    width = height = bit_depth = color_type = interlace = None
    palette = None
    transparency = None
    compressed = bytearray()

    # PNG is a sequence of length/type/data/CRC chunks after its 8-byte signature.
    position = 8
    while position + 12 <= len(data):
        length = struct.unpack_from(">I", data, position)[0]
        chunk_type = data[position + 4:position + 8]
        chunk_start = position + 8
        chunk_end = chunk_start + length
        if chunk_end + 4 > len(data):
            raise ValueError(f"Truncated PNG chunk in {path}")
        chunk = data[chunk_start:chunk_end]

        if chunk_type == b"IHDR":
            if length != 13:
                raise ValueError(f"Invalid PNG header in {path}")
            (width, height, bit_depth, color_type, compression,
             filter_method, interlace) = struct.unpack(">IIBBBBB", chunk)
            if compression != 0 or filter_method != 0:
                raise ValueError(f"Unsupported PNG compression/filter in {path}")
        elif chunk_type == b"PLTE":
            palette = chunk
        elif chunk_type == b"tRNS":
            transparency = chunk
        elif chunk_type == b"IDAT":
            compressed.extend(chunk)
        elif chunk_type == b"IEND":
            break
        position = chunk_end + 4

    if width is None or height is None:
        raise ValueError(f"Missing PNG IHDR in {path}")
    if width <= 0 or height <= 0:
        raise ValueError(f"PNG dimensions must be positive: {path}")
    if width > 0x7FFFFFFF or height > 0x7FFFFFFF:
        raise ValueError(f"PNG dimensions exceed the BMP format limit: {path}")
    if interlace != 0:
        raise ValueError(f"Interlaced PNGs are not supported: {path}")

    channels_by_type = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
    if color_type not in channels_by_type:
        raise ValueError(f"Unsupported PNG color type {color_type} in {path}")
    if bit_depth not in (1, 2, 4, 8, 16):
        raise ValueError(f"Unsupported PNG bit depth {bit_depth} in {path}")
    if color_type in (2, 4, 6) and bit_depth < 8:
        raise ValueError(f"Invalid bit depth for PNG color type {color_type} in {path}")
    if color_type == 3 and (palette is None or bit_depth == 16):
        raise ValueError(f"Indexed PNG has no usable palette: {path}")

    channels = channels_by_type[color_type]
    bits_per_pixel = channels * bit_depth
    row_bytes = (width * bits_per_pixel + 7) // 8
    filter_bytes_per_pixel = max(1, (bits_per_pixel + 7) // 8)
    raw = zlib.decompress(compressed)
    expected_bytes = height * (row_bytes + 1)
    if len(raw) != expected_bytes:
        raise ValueError(
            f"Unexpected decompressed PNG size for {path}: "
            f"{len(raw)} != {expected_bytes}"
        )

    # Undo PNG's per-row predictors using the previously reconstructed scanline.
    rows = []
    previous = bytearray(row_bytes)
    offset = 0
    for _ in range(height):
        filter_type = raw[offset]
        offset += 1
        scanline = bytearray(raw[offset:offset + row_bytes])
        offset += row_bytes
        if filter_type > 4:
            raise ValueError(f"Invalid PNG filter {filter_type} in {path}")

        for index in range(row_bytes):
            left = (scanline[index - filter_bytes_per_pixel]
                    if index >= filter_bytes_per_pixel else 0)
            above = previous[index]
            upper_left = (previous[index - filter_bytes_per_pixel]
                          if index >= filter_bytes_per_pixel else 0)
            if filter_type == 1:       # Sub: predictor is the byte to the left.
                predictor = left
            elif filter_type == 2:     # Up: predictor is the byte above.
                predictor = above
            elif filter_type == 3:     # Average: mean of left and above.
                predictor = (left + above) // 2
            elif filter_type == 4:     # Paeth: nearest of left, above, upper-left.
                predictor = paeth(left, above, upper_left)
            else:                      # None: stored byte is already the sample.
                predictor = 0
            scanline[index] = (scanline[index] + predictor) & 0xFF

        rows.append(scanline)
        previous = scanline

    return width, height, bit_depth, color_type, palette, transparency, rows


def sample(row, pixel, channel, channels, bit_depth):
    """Read one channel sample, including packed 1/2/4-bit and 16-bit values."""
    if bit_depth == 8:
        return row[pixel * channels + channel]
    if bit_depth == 16:
        offset = (pixel * channels + channel) * 2
        return struct.unpack_from(">H", row, offset)[0]

    bit_offset = pixel * bit_depth
    shift = 8 - bit_depth - (bit_offset & 7)
    return (row[bit_offset >> 3] >> shift) & ((1 << bit_depth) - 1)


def rgba_at(row, x, bit_depth, color_type, palette, transparency):
    """Expand one pixel of a supported PNG color type to 8-bit RGBA."""
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color_type]

    if color_type == 3:
        palette_index = sample(row, x, 0, 1, bit_depth)
        palette_offset = palette_index * 3
        if palette_offset + 2 >= len(palette):
            raise ValueError("Indexed PNG references a missing palette entry")
        alpha = (transparency[palette_index]
                 if transparency and palette_index < len(transparency) else 255)
        return (palette[palette_offset], palette[palette_offset + 1],
                palette[palette_offset + 2], alpha)

    raw_values = [sample(row, x, channel, channels, bit_depth)
                  for channel in range(channels)]
    values = [value >> 8 for value in raw_values] if bit_depth == 16 else raw_values
    if bit_depth in (1, 2, 4):
        maximum = (1 << bit_depth) - 1
        values = [(value * 255 + maximum // 2) // maximum for value in values]

    if color_type == 0:
        gray = values[0]
        alpha = 255
        if transparency and len(transparency) >= 2:
            transparent_value = struct.unpack_from(">H", transparency)[0]
            if raw_values[0] == transparent_value:
                alpha = 0
        return gray, gray, gray, alpha

    if color_type == 2:
        alpha = 255
        if transparency and len(transparency) >= 6:
            transparent_rgb = struct.unpack_from(">HHH", transparency)
            if tuple(raw_values) == transparent_rgb:
                alpha = 0
        return values[0], values[1], values[2], alpha

    if color_type == 4:
        return values[0], values[0], values[0], values[1]

    return values[0], values[1], values[2], values[3]


def quantize(rgba, matte):
    """Composite alpha over a matte color and quantize to shared RGB 3:3:2."""
    red, green, blue, alpha = rgba
    inverse_alpha = 255 - alpha
    red = (red * alpha + matte[0] * inverse_alpha + 127) // 255
    green = (green * alpha + matte[1] * inverse_alpha + 127) // 255
    blue = (blue * alpha + matte[2] * inverse_alpha + 127) // 255

    red_index = (red * 7 + 127) // 255
    green_index = (green * 7 + 127) // 255
    blue_index = (blue * 3 + 127) // 255
    return (red_index << 5) | (green_index << 2) | blue_index


def encode_bmp(width, height, indexed_pixels):
    """Build a Windows 8-bit indexed BMP with a 256-entry palette and row padding."""
    row_bytes = (width + 3) & ~3
    pixel_bytes = row_bytes * height
    pixel_offset = 14 + 40 + 256 * 4
    file_size = pixel_offset + pixel_bytes
    if pixel_bytes > 0xFFFFFFFF or file_size > 0xFFFFFFFF:
        raise ValueError("Image exceeds the maximum size of a classic BMP file")
    file_header = struct.pack("<2sIHHI", b"BM", file_size, 0, 0, pixel_offset)
    dib_header = struct.pack(
        "<IiiHHIIiiII", 40, width, height, 1, 8, 0, pixel_bytes,
        2835, 2835, 256, 0
    )
    palette_bytes = b"".join(bytes((blue, green, red, 0))
                              for red, green, blue in PALETTE)
    padding = bytes(row_bytes - width)
    bottom_up_pixels = bytearray()

    # BMP stores rows bottom-to-top; pad every 8-bit row to a 4-byte boundary.
    for y in range(height - 1, -1, -1):
        start = y * width
        bottom_up_pixels.extend(indexed_pixels[start:start + width])
        bottom_up_pixels.extend(padding)

    return file_header + dib_header + palette_bytes + bottom_up_pixels


def clean_dos_component(name):
    """Normalize one path component to uppercase DOS-safe ASCII characters."""
    decomposed = unicodedata.normalize("NFKD", name)
    ascii_name = decomposed.encode("ascii", "ignore").decode("ascii").upper()
    cleaned = re.sub(r"[^A-Z0-9_~]", "_", ascii_name)
    cleaned = re.sub(r"_+", "_", cleaned).strip("_")
    return cleaned or "FILE"


def make_alias(cleaned_name, used_names):
    """Create the first unused 8-character ``PREFIX~n`` name."""
    for number in range(1, 1000000):
        suffix = f"~{number}"
        prefix_length = MAX_DOS_STEM - len(suffix)
        if prefix_length < 1:
            break
        candidate = cleaned_name[:prefix_length] + suffix
        if candidate not in used_names:
            return candidate
    raise ValueError(f"Could not allocate a unique 8.3 alias for {cleaned_name!r}")


def assign_dos_names(items):
    """Assign unique 1-8 character names to ``(key, source_stem)`` items.

    Naturally short names are reserved first. Longer names and duplicate names
    get stable aliases, sorted independently of the filesystem enumeration order.
    """
    ordered = sorted(items, key=lambda item: (str(item[0]).casefold(), str(item[0])))
    cleaned_by_key = {key: clean_dos_component(stem) for key, stem in ordered}
    natural_groups = {}
    for key, _stem in ordered:
        cleaned = cleaned_by_key[key]
        if (len(cleaned) <= MAX_DOS_STEM
                and cleaned not in DOS_RESERVED_NAMES):
            natural_groups.setdefault(cleaned, []).append(key)

    # Reserve every natural name before allocating aliases to avoid stealing one.
    used_names = set(natural_groups)
    assigned = {}
    for cleaned, keys in natural_groups.items():
        assigned[keys[0]] = cleaned

    for key, _stem in ordered:
        if key in assigned:
            continue
        alias = make_alias(cleaned_by_key[key], used_names)
        assigned[key] = alias
        used_names.add(alias)

    return assigned


def map_pngs_to_dos_paths(source_root, png_files):
    """Mirror the PNG folder tree using unique DOS-safe directories and names."""
    children_by_parent = {}
    files_by_parent = {}

    for source_path in png_files:
        relative = source_path.relative_to(source_root)
        parent_parts = relative.parts[:-1]
        files_by_parent.setdefault(parent_parts, []).append(source_path)
        for depth, child_name in enumerate(parent_parts):
            parent_key = parent_parts[:depth]
            children_by_parent.setdefault(parent_key, set()).add(child_name)

    directory_names = {
        parent: assign_dos_names([(name, name) for name in sorted(names)])
        for parent, names in children_by_parent.items()
    }
    file_names = {}
    for parent, files in files_by_parent.items():
        file_names[parent] = assign_dos_names([
            (path.relative_to(source_root).as_posix(), path.stem)
            for path in files
        ])

    output_paths = {}
    for source_path in png_files:
        relative = source_path.relative_to(source_root)
        source_parent = relative.parts[:-1]
        output_parts = []
        for depth, source_directory in enumerate(source_parent):
            parent_key = source_parent[:depth]
            output_parts.append(directory_names[parent_key][source_directory])

        source_key = relative.as_posix()
        output_stem = file_names[source_parent][source_key]
        output_paths[source_path] = Path(*output_parts, f"{output_stem}.BMP")

    # FAT/DOS paths are case-insensitive, so reject any mapping collision early.
    seen = {}
    for source_path, output_path in output_paths.items():
        key = output_path.as_posix().casefold()
        if key in seen:
            raise ValueError(
                f"8.3 name collision: {source_path} and {seen[key]} -> {output_path}"
            )
        seen[key] = source_path

    return output_paths


def convert_png(source_path, output_path, matte):
    """Decode one PNG and write an 8-bit BMP using the shared RGB 3:3:2 palette."""
    width, height, depth, color_type, palette, transparency, rows = read_png(source_path)
    pixels = bytearray(width * height)

    # Keep source scanline order in memory; encode_bmp stores rows bottom-up.
    for y, row in enumerate(rows):
        target = y * width
        for x in range(width):
            rgba = rgba_at(row, x, depth, color_type, palette, transparency)
            pixels[target + x] = quantize(rgba, matte)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(encode_bmp(width, height, pixels))
    return width, height


def clean_output_directory(output_root, keep_paths=()):
    """Remove stale BMPs while preserving this run's outputs and non-BMP files."""
    if not output_root.exists():
        return

    keep = {Path(path).as_posix().casefold() for path in keep_paths}
    for path in output_root.rglob("*"):
        relative = path.relative_to(output_root).as_posix().casefold()
        if (path.is_file() and path.suffix.upper() == ".BMP"
                and relative not in keep):
            path.unlink()

    # Remove directories made empty by deleting old BMPs; keep non-empty folders.
    directories = sorted(
        (path for path in output_root.rglob("*") if path.is_dir()),
        key=lambda path: len(path.parts),
        reverse=True,
    )
    for directory in directories:
        try:
            directory.rmdir()
        except OSError:
            pass


def paths_overlap(first, second):
    """Return true when either resolved directory is equal to/inside the other."""
    try:
        first.relative_to(second)
        return True
    except ValueError:
        pass
    try:
        second.relative_to(first)
        return True
    except ValueError:
        return False


def parse_rgb(value):
    """Parse one hexadecimal RGB matte color, optionally prefixed with '#'."""
    digits = value.strip().lstrip("#")
    if len(digits) != 6 or not re.fullmatch(r"[0-9A-Fa-f]{6}", digits):
        raise ArgumentTypeError("color must be six hexadecimal digits, e.g. 204060")
    return tuple(int(digits[offset:offset + 2], 16) for offset in (0, 2, 4))


def parse_arguments():
    """Read source/destination directories and generic conversion options."""
    parser = ArgumentParser(
        description=(
            "Recursively convert non-interlaced PNG images to 8-bit BMPs "
            "using a shared 256-color RGB 3:3:2 palette."
        )
    )
    parser.add_argument("source", type=Path, help="PNG source directory")
    parser.add_argument("output", type=Path, help="BMP destination directory")
    parser.add_argument(
        "--matte",
        type=parse_rgb,
        default=(0, 0, 0),
        metavar="RRGGBB",
        help="color used behind transparent pixels (default: 000000)",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="remove stale .BMP files after all source images convert successfully",
    )
    return parser.parse_args()


def main():
    """Convert all PNGs recursively, retaining dimensions and DOS 8.3 paths."""
    args = parse_arguments()
    source_root = args.source.expanduser().resolve()
    output_root = args.output.expanduser().resolve()

    if not source_root.is_dir():
        raise SystemExit(f"Source directory does not exist: {source_root}")
    if paths_overlap(source_root, output_root):
        raise SystemExit("Source and output directories must be separate, non-overlapping paths")

    png_files = sorted(
        (path for path in source_root.rglob("*")
         if path.is_file() and path.suffix.lower() == ".png"),
        key=lambda path: (path.relative_to(source_root).as_posix().casefold(),
                          path.relative_to(source_root).as_posix()),
    )
    if not png_files:
        raise SystemExit(f"No PNG images found under: {source_root}")

    output_paths = map_pngs_to_dos_paths(source_root, png_files)
    try:
        output_root.parent.mkdir(parents=True, exist_ok=True)
        if output_root.exists() and not output_root.is_dir():
            raise SystemExit(f"Output path is not a directory: {output_root}")

        # Convert into a temporary sibling first. A bad PNG therefore cannot
        # erase or partially replace the user's existing, working BMP set.
        with tempfile.TemporaryDirectory(
                prefix=".convert-assets-", dir=output_root.parent) as temporary:
            staging_root = Path(temporary)
            dimensions = {}
            for source_path in png_files:
                staged_path = staging_root / output_paths[source_path]
                try:
                    dimensions[source_path] = convert_png(source_path, staged_path, args.matte)
                except (OSError, ValueError, zlib.error) as error:
                    raise SystemExit(
                        f"Failed to convert {source_path}: {error}"
                    ) from error

            output_root.mkdir(parents=True, exist_ok=True)
            for source_path in png_files:
                relative_output = output_paths[source_path]
                destination = output_root / relative_output
                destination.parent.mkdir(parents=True, exist_ok=True)
                os.replace(staging_root / relative_output, destination)

            if args.clean:
                clean_output_directory(output_root, output_paths.values())
    except OSError as error:
        raise SystemExit(f"Could not write BMP files under {output_root}: {error}") from error

    for source_path in png_files:
        width, height = dimensions[source_path]
        print(
            f"{source_path.relative_to(source_root)} -> "
            f"{output_paths[source_path].as_posix()} ({width}x{height})"
        )

    print(f"Converted {len(png_files)} PNG file(s) to {output_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
