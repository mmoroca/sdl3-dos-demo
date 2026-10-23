# Dino Fall runtime sprites

The 19 8-bit indexed BMPs in this folder are the tested runtime assets used by `dinorun.c`. Their original DOS-friendly 8.3 filenames and subfolders are intentional (for example, `DINO/RUN1.BMP`); the game expects these paths. The runtime renderer uses a shared grayscale palette and treats palette index 5 as transparent. The adapted PNG source files are kept in `Images-src/` and are not needed at runtime.

CMake copies this folder beside the executable. The loader looks there first and then checks the working directory; if moving the executable, copy the whole folder beside it. Missing or unrecognized images fall back to procedural sprites.

`tools/convert_assets.py` is a separate, general-purpose converter with a shared RGB 3:3:2 palette. Its output palette does not match the palette and transparent-index convention expected by this game, so do not use it to overwrite these runtime BMPs unless you also adapt the game deliberately.
