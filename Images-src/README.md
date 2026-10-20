# Source art
The runtime versions are generated under `../Images/` as 8-bit grayscale/indexed BMPs with transparent palette index 5. To regenerate them after editing the PNGs, run `python3 tools/convert_dino_assets.py` from the project root; the optional conversion script requires Pillow. The game itself does not require Pillow or SDL_image.
