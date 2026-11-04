# Third-party notices

## SDL3

SDL3 is an external build dependency referenced by the `SDL` Git submodule, which points to [libsdl-org/SDL](https://github.com/libsdl-org/SDL). The parent repository records the upstream commit; SDL's source is not copied into the project. SDL 2.0 and newer, including SDL3, are distributed under the zlib license. This repository's MIT license applies only to the project's own files; it does not relicense SDL3.

The SDL checkout contains the authoritative `LICENSE.txt`. The following is the upstream SDL zlib license text, included here as a convenient notice for source and binary distributions. Verify and retain the license supplied by the exact SDL revision you use.

```text
Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

Official SDL licensing information: <https://www.libsdl.org/license.php>. When distributing SDL source, preserve the license notice that comes with that SDL revision. SDL and this project remain separately licensed components even when linked into the same executable.

## Kenney — Space Shooter Remastered (1.0)

The six Showcase/Shooter sprites and four source sound files below are from **Space Shooter Remastered (1.0)**, created/distributed by Kenney (<https://www.kenney.nl/>):

- `showcase-src/playerShip2_blue.png`
- `showcase-src/ufoBlue.png`
- `showcase-src/ufoGreen.png`
- `showcase-src/ufoRed.png`
- `showcase-src/ufoYellow.png`
- `showcase-src/laserRed02.png`
- `docs/Space Cadet.ogg`
- `docs/laser2.ogg`
- `docs/explosion4.ogg`
- `docs/engine5.ogg`

The collection is released under **Creative Commons Zero v1.0 Universal (CC0 1.0)**: <https://creativecommons.org/publicdomain/zero/1.0/>. It is free to use in personal, educational, and commercial projects. Crediting Kenney is appreciated but not required; this project includes the attribution voluntarily. `showcase-src/UFO.png` (`SHOWCASE/UFO.BMP`) is a 320×200 presentation screen composed from those CC0 sprites by `@mmoroca 2026`. Converted 8-bit indexed BMPs and U8 mono PCM WAVs in `SHOWCASE/` are runtime derivatives of these assets, not MIT-licensed third-party artwork.

### Aviso en español

Los seis sprites compartidos por Showcase y Shooter y los cuatro OGG fuente indicados arriba pertenecen a **Space Shooter Remastered (1.0)**, creado/distribuido por Kenney (<https://www.kenney.nl/>), bajo **Creative Commons Zero v1.0 Universal (CC0 1.0)**: <https://creativecommons.org/publicdomain/zero/1.0/>. Se pueden usar en proyectos personales, educativos y comerciales. La atribución es opcional; este proyecto acredita a Kenney de forma voluntaria. `showcase-src/UFO.png` (`SHOWCASE/UFO.BMP`) es una pantalla de presentación de 320×200 compuesta con esos sprites CC0 por `@mmoroca 2026`. Los BMP indexados y WAV PCM mono de `SHOWCASE/` son conversiones de ejecución de estos recursos y no re-licencian obra de terceros bajo MIT.

## SKYHOP platform graphics (Kenney, CC0 1.0)

The 21 source PNGs in `Images-src/PLATFORM/` were taken from the project owner's [PLATFORM directory](https://github.com/mmoroca/sdl3-dos-demo/tree/main/Images/PLATFORM). The owner states they come from the same Kenney source and have the same **CC0 1.0** license as the shooter graphics. The exact pack title was not independently verified, so no particular pack title is asserted for these sprites. Kenney: <https://www.kenney.nl/>; CC0 1.0: <https://creativecommons.org/publicdomain/zero/1.0/>. Attribution is voluntary. `Images/PLATFORM/` contains the 21 converted 8-bit runtime BMPs; each is a derivative of the corresponding PNG, not MIT-licensed project code. `tools/convert_platform_assets.py` is original MIT-licensed project code and depends only on the Python standard library.

### Aviso en español — gráficos de SKYHOP

Los 21 PNG de `Images-src/PLATFORM/` proceden de la [carpeta PLATFORM del proyecto del usuario](https://github.com/mmoroca/sdl3-dos-demo/tree/main/Images/PLATFORM). El propietario indica que comparten origen Kenney y licencia **CC0 1.0** con los gráficos del shooter. No se ha verificado el título exacto del paquete y por ello no se atribuye uno concreto a estos sprites. Kenney: <https://www.kenney.nl/>; CC0 1.0: <https://creativecommons.org/publicdomain/zero/1.0/>. La atribución es voluntaria. Los 21 BMP de `Images/PLATFORM/` son derivados indexados de los PNG respectivos, **no** código del proyecto bajo MIT. `tools/convert_platform_assets.py` sí es código original MIT del proyecto y solo necesita la biblioteca estándar de Python.

## SKYHOP audio — Kenney, CC0 1.0 / Audio de SKYHOP

The project owner supplied `docs/platform_musica.ogg`, `docs/platform_salto.ogg`, and `docs/platform_pierde.ogg` from their [docs directory](https://github.com/mmoroca/sdl3-dos-demo/tree/main/docs) and confirms that they are **Kenney audio under the same CC0 1.0 license as the Shooter audio**. Kenney: <https://www.kenney.nl/>; CC0 1.0: <https://creativecommons.org/publicdomain/zero/1.0/>. Attribution is voluntary. The exact pack title for these three sounds has not been independently verified and is not claimed here. `Images/PLATFORM/MUSIC.WAV`, `JUMP.WAV`, and `LOSE.WAV` are offline PCM derivatives of the three OGG tracks and remain under CC0 1.0; the project's MIT license does **not** relicense third-party audio. The conversion script `tools/convert_platform_audio.sh` is original MIT-licensed project code; FFmpeg is a build-time tool only.

El propietario del proyecto facilitó `docs/platform_musica.ogg`, `docs/platform_salto.ogg` y `docs/platform_pierde.ogg` de su [directorio docs](https://github.com/mmoroca/sdl3-dos-demo/tree/main/docs) y confirma que son **audios Kenney con la misma licencia CC0 1.0 que los del Shooter**. Kenney: <https://www.kenney.nl/>; CC0 1.0: <https://creativecommons.org/publicdomain/zero/1.0/>. La atribución es voluntaria. No se ha verificado por separado el nombre exacto del paquete de estos tres sonidos y no se afirma uno aquí. Los WAV `Images/PLATFORM/MUSIC.WAV`, `JUMP.WAV` y `LOSE.WAV` son derivados PCM de esos OGG y conservan CC0 1.0; la licencia MIT del código propio **no** se aplica a estos audios de terceros. `tools/convert_platform_audio.sh` es código propio MIT; FFmpeg solo interviene al generar los archivos.

## TETRISDL3 effects and music — Kenney CC0 1.0 / Efectos y música de TETRISDL3

The project owner supplied `docs/click.ogg`, `fall.ogg`, `gameover.ogg`, `levelup.ogg`, `tetris.ogg`, **`Retro Beat.ogg` and `Retro Polka.ogg`** via [their docs directory](https://github.com/mmoroca/sdl3-dos-demo/tree/main/docs), explicitly confirming **Kenney CC0 1.0** for all seven. Kenney: <https://www.kenney.nl/>; CC0: <https://creativecommons.org/publicdomain/zero/1.0/>. No particular sound-pack title is independently verified or asserted. `TETRIS/CLICK.WAV`, `FALL.WAV`, `OVER.WAV`, `LEVEL.WAV`, `TETRIS.WAV`, `BEAT.WAV` and `POLKA.WAV` are offline U8 mono 11025 Hz PCM derivatives of the corresponding OGGs; they remain CC0, **not** the project's MIT code license. `tools/convert_tetris_audio.sh` is original MIT-licensed code; FFmpeg runs only on the development host. The SDL3-DOS startup logo/voice are separately documented and are **not** covered by this Kenney declaration.

El propietario facilitó `docs/click.ogg`, `fall.ogg`, `gameover.ogg`, `levelup.ogg`, `tetris.ogg`, **`Retro Beat.ogg` y `Retro Polka.ogg`** desde [docs de su repositorio](https://github.com/mmoroca/sdl3-dos-demo/tree/main/docs), confirmando expresamente **Kenney CC0 1.0** para los siete. Kenney: <https://www.kenney.nl/>; CC0: <https://creativecommons.org/publicdomain/zero/1.0/>. No se ha verificado ni se atribuye un paquete concreto. `TETRIS/CLICK.WAV`, `FALL.WAV`, `OVER.WAV`, `LEVEL.WAV`, `TETRIS.WAV`, `BEAT.WAV` y `POLKA.WAV` son derivados PCM mono U8 a 11025 Hz de los OGG correspondientes; conservan CC0, **no** la licencia MIT del código propio. `tools/convert_tetris_audio.sh` sí es código original MIT; FFmpeg se usa solamente durante la preparación. El logo/voz SDL3-DOS se documentan aparte y **no** heredan esta declaración Kenney.

## SDL3-DOS startup logo and voice / Logotipo y voz de arranque

The supplied `splash-src/SDL3-DOS Logo 320x200.png` is the source of `SPLASH/SDL3DOS.BMP`. The audio source `splash-src/sdl3-zarvox.wav` was retrieved from the project's companion repository at <https://github.com/mmoroca/sdl3-dos-386/blob/main/docs/sdl3-zarvox.wav>; the smaller `SPLASH/ZARVOX.WAV` is an offline U8 mono PCM derivative. The supplied logo and audio are kept separate from the SDL3-DOS module's original MIT-licensed **code** (`sdl3_splash.h`, `tools/convert_sdl3_splash.py`). No third-party license is invented for these particular media assets; their inclusion does not assert that they are Kenney CC0 or that they are MIT-licensed. Confirm their distribution terms with their rights holder before redistributing independently.

El PNG `splash-src/SDL3-DOS Logo 320x200.png` se facilitó como fuente de `SPLASH/SDL3DOS.BMP`. El audio `splash-src/sdl3-zarvox.wav` se obtuvo del repositorio complementario <https://github.com/mmoroca/sdl3-dos-386/blob/main/docs/sdl3-zarvox.wav>; `SPLASH/ZARVOX.WAV` es su derivado PCM mono U8 convertido previamente. Estos recursos se distinguen del **código** propio MIT (`sdl3_splash.h` y `tools/convert_sdl3_splash.py`). No se atribuye a esta imagen y este sonido una licencia de terceros no comprobada, ni se afirma que sean Kenney CC0 o MIT. Confirma las condiciones de distribución con el titular de derechos antes de redistribuirlos por separado.

## TETRISDL3 original game screens / Pantallas originales de TETRISDL3

The project owner personally created and supplied four 320×200 PNGs: `tetris-src/titulo-juego-tetsdl3 v4.png` (independent title), **`tetris-src/MENU.png`** (new menu with built-in colored wordmark), `tetris-src/menu-juego-tetsdl3.png` (previous menu, now **only** records/Top 10 and name entry), and `tetris-src/fondo-juego-tetsdl3.png` (gameplay). The selected title is v4, not the older alternate title. `tools/convert_tetris_art.py` generates four indexed runtime BMPs `TETRIS/{TITLE,MENU,RECORDS,GAME}.BMP` and the shared palette header `fonts/tetris_art_palette.h`. The converter and gameplay code are original project code under MIT, **but MIT and Kenney CC0 do not automatically apply to the owner's PNG/BMP artwork**. No specific license is presumed for the images; ask the rights holder before separate reuse or redistribution. The DOSBox-X screenshots in `docs/tetris_title.png`, `tetris_menu.png`, `tetris_top10.png` and `tetrisdl3.png` show the art and do not change its rights.

El propietario creó personalmente cuatro PNG de 320×200: `tetris-src/titulo-juego-tetsdl3 v4.png` (portada independiente), **`tetris-src/MENU.png`** (nuevo menú con rótulo de colores incorporado), `tetris-src/menu-juego-tetsdl3.png` (menú anterior, ahora **solo** para récords/Top 10 y entrada de nombre), y `tetris-src/fondo-juego-tetsdl3.png` (partida). Se eligió la portada v4, no la alternativa anterior. `tools/convert_tetris_art.py` genera los cuatro BMP indexados `TETRIS/{TITLE,MENU,RECORDS,GAME}.BMP` y la paleta compartida `fonts/tetris_art_palette.h`. El conversor y el juego son código propio MIT, **pero MIT y Kenney CC0 no se aplican automáticamente al arte PNG/BMP del usuario**. No se presupone licencia concreta para estas imágenes; consulta al titular antes de reutilizarlas o redistribuirlas separadamente. Las capturas DOSBox-X `docs/tetris_title.png`, `tetris_menu.png`, `tetris_top10.png` y `tetrisdl3.png` muestran este arte y no alteran sus derechos.
