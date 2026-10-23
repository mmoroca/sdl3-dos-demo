# Source art

These PNG files are image adaptations prepared by the project owner for Dino Fall, based on the dinosaur-game artwork in the owner's asset collection:

- Reference collection: https://github.com/mmoroca/sdl3-dino-fall/tree/main/Images
- Revision consulted: `e984ba9f27604abe4619b0cf7097bcf0464e42df` (2026-10-19)

The project owner authorized the adapted images for use in this project. The reference collection did not declare a separate machine-readable asset license when checked; this note does not place the art under the code's MIT license. Confirm explicit terms with the owner before redistributing these image assets separately or licensing them independently.

The currently tested runtime BMPs are under `../Images/` and use this game's shared grayscale palette with transparent palette index 5. The generic utility `tools/convert_assets.py` instead uses a shared 256-color RGB 3:3:2 palette and composites alpha over black by default. Its output therefore is not a drop-in replacement for the game's current BMP files. For unrelated projects, run `python3 tools/convert_assets.py PNG_SOURCE_DIRECTORY BMP_OUTPUT_DIRECTORY`; the current decoder supports non-interlaced PNGs. See the script's `--help` for options.
