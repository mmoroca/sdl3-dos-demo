# SKYHOP platform art / Arte de plataformas de SKYHOP

The 21 source PNGs in this folder were retrieved from the user's project at <https://github.com/mmoroca/sdl3-dos-demo/tree/main/Images/PLATFORM> for use in the standalone SKYHOP game. According to the project owner, the art has the same Kenney CC0 1.0 license and origin as the shooter assets. Attribution is voluntary. The precise pack name has **not** been independently verified and is intentionally not claimed here. See [`../../THIRD_PARTY_NOTICES.md`](../../THIRD_PARTY_NOTICES.md). These PNGs and their converted BMPs are **not** covered by the project's MIT license.

Los 21 PNG fuente se obtuvieron del proyecto del usuario en <https://github.com/mmoroca/sdl3-dos-demo/tree/main/Images/PLATFORM> para el juego independiente SKYHOP. Según el propietario del proyecto, el arte comparte la licencia CC0 1.0 y la procedencia Kenney de los recursos del shooter. La atribución es voluntaria. **No** se ha verificado el nombre concreto del paquete y por ello no se afirma aquí. Consulta [`../../THIRD_PARTY_NOTICES.md`](../../THIRD_PARTY_NOTICES.md). Estos PNG y sus BMP convertidos **no** están cubiertos por la licencia MIT del código.

Generate the exact-palette, transparent-index BMP assets from the repository root without Pillow:

```sh
python3 tools/convert_platform_assets.py Images-src/PLATFORM Images/PLATFORM
```
