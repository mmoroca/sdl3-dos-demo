# SHOWCASE.EXE — function map and extension guide

**by @mmoroca & [Arena.AI](http://arena.ai/) 2026**

## English

### Purpose and layout

`showcase.c` is a single-file, function-oriented SDL3/DOS starter example. It demonstrates reusable display, palette, drawing, keyboard/text, optional joystick, audio, and file-I/O patterns. Its mini-shooter is only sample application behavior: replace `initialize_game()`, `update_shooter()`, and `draw_shooter_panel()` with the logic and presentation for your own program (see `shooter.c` / `SHOOTER.EXE` for a standalone full-screen arcade game built directly from this template).

Build just this target with `cmake --build "$DEMO_DIR/build-dos" --target showcase`; run `SHOWCASE.EXE` from that build directory so its copied `SHOWCASE/` assets and runtime files are found.

The requested window is **320×200, 8-bit indexed color**. The SDL3 DOS backend chooses the corresponding VGA/VESA mode (Mode-13h-style); the program does **not** call BIOS interrupts or assume a direct framebuffer address. The screen is split at x=160 and y=100:

1. **General:** multicolor status, game/high score, call sign, palette-fade state, and save/load hints.
2. **Sound:** SDL audio driver/device, `BLASTER` environment text, sample format, clip availability, and volume.
3. **Input:** keyboard/text mode, live axis/hat/button state, and a 3×3 eight-direction indicator.
4. **Shooter:** black background, randomly placed/moving stars, bottom ship, horizontally moving UFO, lasers, and hit/score feedback.

### Controls

| Input | Action |
|---|---|
| Arrows or **W/A/S/D** | Move the ship in eight directions |
| **Space** or joystick button 1 | Fire a laser |
| **T** | Start call-sign entry; type A–Z, 0–9, `-`, `_` |
| **Backspace**, then **Enter** | Edit and save the call sign/settings |
| **F1** | Fade the palette down/up gradually |
| **M** | Toggle background music |
| **F2 / F3** | Save / reload `SHOWCASE.CFG` |
| **- / +** (also keypad keys) | Adjust master volume |
| **Esc** | Exit; the palette fades out |

Keyboard movement and firing remain available without a joystick. The optional joystick code opens the first SDL joystick, reads axes 0/1 and hat 0, and shows up to four buttons. If SDL or the DOS backend cannot enumerate a joystick, the demo continues normally and displays `JOY: NOT FOUND`.

### Function map

The source has section headers and short comments at each function. Main responsibilities are:

| Area | Functions | What to reuse or replace |
|---|---|---|
| Text and state helpers | `copy_upper_text`, `copy_callsign`, `set_status`, `parse_long_value` | Bounded string handling and status messages. |
| Persistent files | `set_default_data`, `load_settings`, `save_settings`, `load_score_record`, `save_score_record` | Plain-text configuration and high-score patterns, with input validation. |
| Display and palette | `set_surface_palette`, `build_rgb332_palette`, `apply_palette_brightness`, `initialize_video`, `update_palette_fade` | Request INDEX8, build a shared 3:3:2 palette, and scale its RGB entries from black to full brightness. |
| Drawing and font | `find_glyph`, `clear_canvas`, `put_pixel`, `fill_rect`, `draw_line`, `draw_rect`, `draw_text` | Software drawing on an indexed SDL surface; the built-in 5×7 font avoids SDL_ttf. Text is clipped to its 160-pixel panel. |
| Image assets | `load_sprite`, `sprite_width_or`, `sprite_height_or`, `draw_scaled_sprite` | Load indexed BMPs and draw nearest-neighbor at the `SPRITE_SCALE_DIVISOR` (default 4); palette index 0 is transparent. |
| Audio | `load_pcm_clip`, `start_one_shot`, `mix_clip_sample`, `mix_audio_block`, `feed_audio_queue`, `initialize_audio` | Load/validate PCM WAV clips and mix music/engine/effects into a short SDL audio-stream queue. Audio setup failure is non-fatal. |
| Joystick | `initialize_joystick`, `poll_joystick` | Optional first-device enumeration; combine analog axes and a hat into eight directions and read buttons. |
| Sample gameplay | `initialize_game`, `rectangles_overlap`, `fire_laser`, `update_stars`, `update_shooter` | Example initialization, movement, starfield, cooldown, collision, score, and record update. Replace these for a different application. |
| Text and keys | `append_callsign_character`, `begin_name_editing`, `finish_name_editing`, `handle_text_input`, `handle_key_down`, `handle_key_up`, `handle_event` | SDL text events plus an ASCII-key fallback for DOS keyboards; adapt actions in `handle_key_down()`. |
| Screen composition | `draw_general_panel`, `draw_audio_panel`, `draw_direction_pad`, `draw_input_panel`, `draw_shooter_panel`, `present_frame`, `draw_frame` | Keep the reusable diagnostic panels or replace the shooter panel and composition. |
| Lifecycle | `run_showcase`, `fade_out_before_exit`, `shutdown_showcase`, `main` | Event/update/render loop, graceful palette fade, and reverse-order cleanup after partial setup. |

### Assets and runtime formats

The source sprites are `playerShip2_blue.png` (112×75), `ufoGreen.png` (91×91), and `laserRed02.png` (13×37). Convert the PNG folder with the repository's generic, standard-library-only converter:

```sh
python3 tools/convert_assets.py showcase-src SHOWCASE --matte 000000 --clean
```

It creates 8-bit indexed BMPs with the shared RGB 3:3:2 palette. `--matte 000000` composites PNG transparency over black, producing palette index 0 for the transparent key used by the demo. The converter preserves the source dimensions; scaling happens only when drawing. `SPRITE_SCALE_DIVISOR` defaults to `4` (one-quarter of each linear dimension). `load_sprite()` calculates each destination dimension as `(source_dimension + divisor - 1) / divisor`, rounding up so a final partial block remains visible when a source dimension is not divisible by the divisor. `draw_scaled_sprite()` then uses nearest-neighbor decimation: destination pixel `(x, y)` samples source pixel `(x * divisor, y * divisor)`; it does not interpolate. The draw is clipped to the shooter panel and source palette index 0 is skipped as transparent. Thus the 112×75 ship, 91×91 UFO, and 13×37 laser render at **28×19**, **23×23**, and **4×10** pixels. Change the macro in `showcase.c` to `2` for 1:2, `3` for 1:3, or `1` for native-size drawing. The converter's DOS 8.3 output names are `PLAYER~1.BMP`, `UFOGREEN.BMP`, and `LASERR~1.BMP`.

The four original CC0 OGG files stay in `docs/`. This DOS example uses SDL3 core audio with **unsigned 8-bit, mono, 11025-Hz PCM WAV**, not OGG; its runtime does not link an OGG decoder or SDL3_mixer. Regenerate the included files with FFmpeg:

```sh
sh tools/convert_showcase_audio.sh docs SHOWCASE
```

SDL3_mixer is a separate SDL addon (not part of SDL3 core) and its official documentation lists Ogg Vorbis support. The current SDL3_mixer 3.0 documentation requires SDL 3.4.0 or later. This demo's pinned SDL version and the addon's compatibility with the DOS/DJGPP target have not been verified, so the DOS build deliberately remains on WAV and SDL3 core audio. See the [SDL3_mixer overview](https://wiki.libsdl.org/SDL3_mixer) and its [official install requirements](https://github.com/libsdl-org/SDL_mixer/blob/main/INSTALL.md) before adding it to a target. If the addon is later validated for the target, OGG can be used there without storing these PCM copies.

PCM WAV avoids Vorbis decompression during playback and is a simple, predictable input for the DOS audio path; the tradeoff is that uncompressed audio usually needs more storage and memory than compressed OGG. Streaming OGG also trades some playback CPU for lower memory use; a fully decoded/preloaded OGG pays its decoding cost during loading and then occupies PCM memory. The bundled files are an exception to the usual size comparison because they are deliberately reduced to mono, 11025 Hz, unsigned 8-bit PCM. These are the exact checked-in sizes (bytes):

| Sound | Source OGG | Runtime WAV |
|---|---:|---:|
| Music (`Space Cadet.ogg` → `MUSIC.WAV`) | 307,622 | 266,408 |
| Laser (`laser2.ogg` → `LASER.WAV`) | 9,253 | 2,821 |
| Impact (`explosion4.ogg` → `HIT.WAV`) | 10,788 | 4,733 |
| Engine (`engine5.ogg` → `ENGINE.WAV`) | 21,360 | 22,127 |

Only `ENGINE.WAV` is larger here, by 767 bytes; the other WAVs are smaller because these conversions reduce the source to low-rate mono PCM. The software mixer uses music at 60% of the master setting, one-shot effects at the master setting, and the engine at **exactly 50% of master**. Music loops, the laser plays on fire, the explosion plays on collision, and the engine clip loops while the ship moves. `-` and `+` adjust master volume; press F2 to persist changes.

### Files and customization notes

- `SHOWCASE.CFG` is plain text: `name`, `volume` (0–255), and `music` (0/1). It lives in the current working directory. The program loads it (or uses defaults) at startup, writes the initial values after video setup, and saves changes with Enter/F2.
- `SHOWCASE.SCO` stores the best score and the call sign that achieved it. A new record is written immediately.
- `SHOWCASE.LOG` records startup, devices/assets, and errors. These runtime files are ignored by Git.
- The `SHOWCASE/` folder is copied beside the executable by CMake. Run from that directory or keep the folder beside `SHOWCASE.EXE`.
- Audio hardware diagnostics show the SDL-selected driver/device and the `BLASTER` environment string. `BLASTER` is informational here; it is not a direct hardware probe.
- The palette fades RGB colors only; sound volume is controlled separately by the software mixer.
- The CC0 sprite/sound assets are not relicensed under the repository's MIT code license. Attribution and license details are in `showcase-src/README.md` and `THIRD_PARTY_NOTICES.md`.

## Español

### Objetivo y diseño

`showcase.c` es una plantilla de inicio SDL3/DOS en un solo archivo y organizada por funciones. Muestra patrones reutilizables de pantalla, paleta, dibujo, teclado/texto, joystick opcional, audio y ficheros. El mini shooter es solo la funcionalidad de ejemplo: sustituye `initialize_game()`, `update_shooter()` y `draw_shooter_panel()` por la lógica y la presentación de tu propio programa (consulta `shooter.c` / `SHOOTER.EXE` como ejemplo completo independiente construido a partir de esta plantilla).

Compila solo este objetivo con `cmake --build "$DEMO_DIR/build-dos" --target showcase`; ejecuta `SHOWCASE.EXE` desde ese directorio para que se encuentren los recursos `SHOWCASE/` y los ficheros de ejecución.

La ventana solicitada es de **320×200, color indexado de 8 bits**. El backend DOS de SDL3 selecciona el modo VGA/VESA correspondiente (estilo Modo 13h); el programa **no** llama a interrupciones BIOS ni presupone una dirección de framebuffer. La pantalla se divide en x=160 e y=100:

1. **General:** estado multicolor, puntuación/récord, nombre, fundido de paleta e indicaciones de guardar/cargar.
2. **Sonido:** controlador/dispositivo SDL, texto de entorno `BLASTER`, formato de muestra, disponibilidad de clips y volumen.
3. **Entrada:** teclado/texto, ejes/gatillo/botones actuales y un indicador 3×3 de ocho direcciones.
4. **Shooter:** fondo negro, estrellas aleatorias en movimiento, nave abajo, UFO que se mueve horizontalmente, láseres y feedback de impacto/puntuación.

### Controles

| Entrada | Acción |
|---|---|
| Flechas o **W/A/S/D** | Mover la nave en ocho direcciones |
| **Space** o botón 1 del joystick | Disparar un láser |
| **T** | Empezar a escribir el nombre; admite A–Z, 0–9, `-`, `_` |
| **Backspace** y luego **Enter** | Editar y guardar el nombre/ajustes |
| **F1** | Bajar/subir gradualmente el brillo de la paleta |
| **M** | Activar/desactivar la música de fondo |
| **F2 / F3** | Guardar / volver a cargar `SHOWCASE.CFG` |
| **- / +** (también teclado numérico) | Ajustar el volumen general |
| **Esc** | Salir; la paleta se apaga gradualmente |

El teclado sigue funcionando si no hay joystick. El código opcional abre el primer joystick SDL, lee los ejes 0/1 y el hat 0, y muestra hasta cuatro botones. Si SDL o el backend DOS no pueden enumerar un joystick, la demo continúa y muestra `JOY: NOT FOUND`.

### Mapa de funciones

El fuente incluye encabezados de sección y comentarios breves en cada función. Responsabilidades principales:

| Área | Funciones | Qué reutilizar o sustituir |
|---|---|---|
| Texto y estado | `copy_upper_text`, `copy_callsign`, `set_status`, `parse_long_value` | Copias de texto acotadas y mensajes de estado. |
| Ficheros persistentes | `set_default_data`, `load_settings`, `save_settings`, `load_score_record`, `save_score_record` | Patrones de configuración y récord en texto plano, con validación. |
| Pantalla y paleta | `set_surface_palette`, `build_rgb332_palette`, `apply_palette_brightness`, `initialize_video`, `update_palette_fade` | Solicitar INDEX8, construir paleta RGB 3:3:2 y escalar sus entradas RGB desde negro hasta brillo completo. |
| Dibujo y fuente | `find_glyph`, `clear_canvas`, `put_pixel`, `fill_rect`, `draw_line`, `draw_rect`, `draw_text` | Dibujo por software en una superficie SDL indexada; la fuente 5×7 integrada evita SDL_ttf. El texto se recorta al panel de 160 píxeles. |
| Imágenes | `load_sprite`, `sprite_width_or`, `sprite_height_or`, `draw_scaled_sprite` | Cargar BMP indexados y dibujar con vecino más próximo según `SPRITE_SCALE_DIVISOR` (4 por defecto); el índice 0 es transparente. |
| Audio | `load_pcm_clip`, `start_one_shot`, `mix_clip_sample`, `mix_audio_block`, `feed_audio_queue`, `initialize_audio` | Cargar/validar WAV PCM y mezclar música/motor/efectos en una cola corta SDL. Si falla el audio, la demo sigue. |
| Joystick | `initialize_joystick`, `poll_joystick` | Enumeración opcional del primer dispositivo; combinar ejes analógicos y hat en ocho direcciones y leer botones. |
| Juego de ejemplo | `initialize_game`, `rectangles_overlap`, `fire_laser`, `update_stars`, `update_shooter` | Inicialización, movimiento, estrellas, cadencia, colisiones, puntuación y récord de ejemplo. Sustitúyelas para otra aplicación. |
| Texto y teclas | `append_callsign_character`, `begin_name_editing`, `finish_name_editing`, `handle_text_input`, `handle_key_down`, `handle_key_up`, `handle_event` | Eventos de texto SDL y alternativa de teclas ASCII para teclados DOS; adapta las acciones en `handle_key_down()`. |
| Composición de pantalla | `draw_general_panel`, `draw_audio_panel`, `draw_direction_pad`, `draw_input_panel`, `draw_shooter_panel`, `present_frame`, `draw_frame` | Mantener los paneles de diagnóstico o sustituir el shooter y la composición. |
| Ciclo de vida | `run_showcase`, `fade_out_before_exit`, `shutdown_showcase`, `main` | Bucle de eventos/actualización/dibujo, fundido final y limpieza inversa aunque una etapa falle. |

### Recursos y formatos de ejecución

Los sprites fuente son `playerShip2_blue.png` (112×75), `ufoGreen.png` (91×91) y `laserRed02.png` (13×37). Convierte la carpeta PNG con la herramienta genérica del repositorio, que solo necesita la biblioteca estándar de Python:

```sh
python3 tools/convert_assets.py showcase-src SHOWCASE --matte 000000 --clean
```

Genera BMP indexados de 8 bits con la paleta RGB 3:3:2 compartida. `--matte 000000` compone la transparencia PNG sobre negro, produciendo el índice 0 que la demo usa como clave transparente. El conversor conserva las dimensiones fuente; el escalado se aplica únicamente al dibujar. `SPRITE_SCALE_DIVISOR` vale `4` por defecto (un cuarto de cada dimensión lineal). `load_sprite()` calcula cada dimensión de destino como `(dimensión_fuente + divisor - 1) / divisor`, redondeando hacia arriba para no perder el borde final cuando la dimensión no es múltiplo. Después `draw_scaled_sprite()` usa decimación por vecino más próximo: el píxel destino `(x, y)` toma el píxel fuente `(x * divisor, y * divisor)`; no interpola. El dibujo se recorta al panel del shooter y se omite el índice de paleta 0, usado como transparente. Así, la nave de 112×75, el UFO de 91×91 y el láser de 13×37 se muestran a **28×19**, **23×23** y **4×10** píxeles. Cambia la macro en `showcase.c` a `2` para 1:2, `3` para 1:3 o `1` para dibujar al tamaño nativo. Los nombres DOS 8.3 generados son `PLAYER~1.BMP`, `UFOGREEN.BMP` y `LASERR~1.BMP`.

Los cuatro OGG CC0 originales permanecen en `docs/`. Esta demo DOS usa el audio básico de SDL3 con WAV PCM **sin signo, mono, 8 bits y 11025 Hz**, no OGG; el ejecutable no enlaza un decodificador OGG ni SDL3_mixer. Regenera los recursos incluidos con FFmpeg:

```sh
sh tools/convert_showcase_audio.sh docs SHOWCASE
```

SDL3_mixer es un complemento separado de SDL (no forma parte del núcleo SDL3) y su documentación oficial confirma que admite Ogg Vorbis. La documentación actual de SDL3_mixer 3.0 exige SDL 3.4.0 o posterior. No se han comprobado la versión fijada de SDL de este proyecto ni la compatibilidad del complemento con DOS/DJGPP, así que el objetivo DOS sigue deliberadamente con WAV y el audio básico de SDL3. Antes de añadirlo a un objetivo, consulta la [descripción de SDL3_mixer](https://wiki.libsdl.org/SDL3_mixer) y sus [requisitos oficiales de instalación](https://github.com/libsdl-org/SDL_mixer/blob/main/INSTALL.md). Si más adelante se valida para el objetivo, se podrán usar los OGG sin guardar copias PCM.

WAV PCM evita descomprimir Vorbis durante la reproducción y es una entrada sencilla y predecible para el audio DOS; normalmente, la contrapartida es que el audio sin comprimir ocupa más almacenamiento y memoria que OGG. La reproducción en streaming de OGG también intercambia algo de CPU durante la escucha por un menor uso de memoria; si el OGG se decodifica por completo al cargarlo, el coste de decodificación ocurre al cargar y después ocupa memoria PCM. Los archivos incluidos son una excepción a la comparación habitual porque se redujeron expresamente a mono, 11025 Hz y 8 bits sin signo. Estos son los tamaños exactos incluidos (bytes):

| Sonido | OGG fuente | WAV de ejecución |
|---|---:|---:|
| Música (`Space Cadet.ogg` → `MUSIC.WAV`) | 307.622 | 266.408 |
| Láser (`laser2.ogg` → `LASER.WAV`) | 9.253 | 2.821 |
| Impacto (`explosion4.ogg` → `HIT.WAV`) | 10.788 | 4.733 |
| Motor (`engine5.ogg` → `ENGINE.WAV`) | 21.360 | 22.127 |

Aquí solo `ENGINE.WAV` ocupa más (767 bytes); los demás WAV son más pequeños porque estas conversiones reducen la fuente a PCM mono de baja frecuencia. El mezclador por software reproduce la música al 60 % del volumen general, los efectos al volumen general y el motor a **exactamente el 50 % del volumen general**. La música se repite, el láser suena al disparar, la explosión al colisionar y el motor se repite mientras la nave se mueve. `-` y `+` ajustan el volumen general; F2 guarda los cambios.

### Ficheros y notas de personalización

- `SHOWCASE.CFG` es texto plano: `name`, `volume` (0–255) y `music` (0/1). Se guarda en el directorio actual. Al iniciar se carga el archivo (o se usan valores predeterminados); tras preparar el vídeo se escriben los valores iniciales, y Enter/F2 guarda los cambios.
- `SHOWCASE.SCO` guarda el récord y el nombre que lo consiguió. Un nuevo récord se escribe inmediatamente.
- `SHOWCASE.LOG` registra el inicio, dispositivos/recursos y errores. Git ignora estos archivos de ejecución.
- CMake copia la carpeta `SHOWCASE/` junto al ejecutable. Ejecuta desde esa carpeta o conserva `SHOWCASE/` al lado de `SHOWCASE.EXE`.
- El diagnóstico de audio muestra el controlador/dispositivo elegido por SDL y la cadena de entorno `BLASTER`. Aquí `BLASTER` es informativa, no una prueba directa del hardware.
- El fundido modifica los colores de la paleta; el volumen se controla por separado en el mezclador por software.
- Los recursos CC0 no se relicencian bajo la MIT del código del repositorio. La atribución y la licencia aparecen en `showcase-src/README.md` y `THIRD_PARTY_NOTICES.md`.
