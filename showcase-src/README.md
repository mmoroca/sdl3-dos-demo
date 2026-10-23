# Showcase asset sources / Fuentes de assets de Showcase

## English

The PNG sprites in this directory are from **Space Shooter Remastered (1.0)** by Kenney, <https://www.kenney.nl/>. They are offered under **Creative Commons Zero (CC0 1.0 Universal)**. The game can be used in personal, educational, and commercial projects. Crediting Kenney is optional; this project credits the artist in `THIRD_PARTY_NOTICES.md`.

The original source names are retained: `playerShip2_blue.png`, `ufoBlue.png`, `ufoGreen.png`, `ufoRed.png`, `ufoYellow.png`, and `laserRed02.png`, plus the 320×200 presentation screen `UFO.png` composed from those sprites by `@mmoroca 2026`. They are converted for runtime with the repository's project-neutral converter:

```sh
python3 tools/convert_assets.py showcase-src SHOWCASE --matte 000000
```

The converter preserves source dimensions and creates indexed BMPs with a shared RGB 3:3:2 palette (`PLAYER~1.BMP`, `UFOBLUE.BMP`, `UFOGREEN.BMP`, `UFORED.BMP`, `UFOYEL~1.BMP`, `LASERR~1.BMP`, and `UFO.BMP`). `SHOOTER.EXE` shows `UFO.BMP` full-screen (320×200 at 1:1) for 5 seconds at startup before entering the title screen. `SHOWCASE.EXE` and `SHOOTER.EXE` draw the six gameplay sprites at one-quarter linear size with nearest-neighbor sampling and treat palette index 0 (black matte) as transparent. The source PNGs are 112×75 (ship), 91×91 (the four UFO models), and 13×37 (laser) pixels; the displayed sizes at 1:4 are 28×19, 23×23, and 4×10 pixels. In `SHOOTER.EXE`, each UFO color identifies one movement pattern (blue = sinusoidal wave, yellow = zigzag, red = swoop dive, green = drift). Change `SPRITE_SCALE_DIVISOR` in `showcase.c` or `shooter.c` to 2 or 3 to compare larger 1:2 or 1:3 renders.

The four original OGG sounds are published in `docs/` and are also CC0 from Kenney. DOS runtime audio is stored separately as unsigned 8-bit mono PCM WAV at 11025 Hz in `SHOWCASE/`; OGG decoding is deliberately not a runtime dependency. `tools/convert_showcase_audio.sh` documents/repeats that conversion using FFmpeg.

## Español

Los sprites PNG de este directorio pertenecen a **Space Shooter Remastered (1.0)**, de Kenney, <https://www.kenney.nl/>. Se ofrecen bajo **Creative Commons Zero (CC0 1.0 Universal)** y pueden usarse en proyectos personales, educativos y comerciales. Atribuir a Kenney es opcional; este proyecto incluye el crédito en `THIRD_PARTY_NOTICES.md`.

Se conservan los nombres originales: `playerShip2_blue.png`, `ufoBlue.png`, `ufoGreen.png`, `ufoRed.png`, `ufoYellow.png` y `laserRed02.png`, además de la pantalla de presentación 320×200 `UFO.png` compuesta con esos sprites por `@mmoroca 2026`. Para la ejecución se convierten con el conversor genérico del repositorio:

```sh
python3 tools/convert_assets.py showcase-src SHOWCASE --matte 000000
```

El conversor conserva las dimensiones y genera BMP indexados con una paleta RGB 3:3:2 compartida (`PLAYER~1.BMP`, `UFOBLUE.BMP`, `UFOGREEN.BMP`, `UFORED.BMP`, `UFOYEL~1.BMP`, `LASERR~1.BMP` y `UFO.BMP`). `SHOOTER.EXE` muestra `UFO.BMP` a pantalla completa (320×200 a escala 1:1) durante 5 segundos al iniciar antes de pasar a la pantalla de título. `SHOWCASE.EXE` y `SHOOTER.EXE` dibujan los seis sprites de juego a un cuarto de su tamaño lineal con muestreo del vecino más cercano y tratan el índice 0 (fondo negro) como transparente. Las imágenes fuente miden 112×75 (nave), 91×91 (los cuatro modelos de UFO) y 13×37 (láser) píxeles; a escala 1:4 se muestran a 28×19, 23×23 y 4×10. En `SHOOTER.EXE`, el color identifica el patrón de movimiento (azul = onda sinusoidal, amarillo = zigzag, rojo = picado rápido, verde = deriva). Cambia `SPRITE_SCALE_DIVISOR` en `showcase.c` o `shooter.c` a 2 o 3 para comparar escalados mayores de 1:2 o 1:3.

Los cuatro OGG originales están publicados en `docs/` y también son CC0 de Kenney. Para DOS se usan archivos WAV PCM mono sin signo de 8 bits a 11025 Hz dentro de `SHOWCASE/`; la decodificación OGG no es una dependencia del ejecutable. `tools/convert_showcase_audio.sh` documenta y automatiza esa conversión con FFmpeg.
