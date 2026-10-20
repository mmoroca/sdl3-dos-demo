# Dino Fall runtime sprites

The 8-bit grayscale/indexed BMPs in this folder are runtime derivatives of the PNG source assets kept in `Images-src/` at the project root (that source folder is not needed at runtime). Their filenames and directories use DOS-friendly 8.3 names. Palette index 5 is the transparent key in `dinorun.c`; the brighter visible gray entries are RGB 80, 150, and 210, with white and black unchanged.

CMake copies this folder into the selected build directory. The loader resolves assets beside `DINO.EXE` first and then checks the working directory; if moving the executable, copy this folder next to it. Missing or unrecognized files fall back to the small procedural sprites in the source, and the title screen reports the loaded count.

Source repository: https://github.com/mmoroca/sdl3-dino-fall/tree/main/Images. Provenance and licensing notes are recorded in the source project's `Images-src/README.md`.
