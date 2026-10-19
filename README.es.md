# SDL3 DOS Demo Suite

[English](README.md) · [Español](README.es.md)

**by @mmoroca & [Arena.AI](http://arena.ai/) 2026**

Conjunto de pequeños programas en C para MS-DOS, compilados con DJGPP y el backend DOS de SDL3. SDL3 se referencia como submódulo Git fijado a un commit upstream; su código fuente permanece en el proyecto SDL y no se copia dentro de este repositorio.

## Programas incluidos

| Ejecutable | Fuente | Descripción y controles |
|---|---|---|
| `HELLO.EXE` | `main.c` | Ejemplo mínimo de superficie SDL a 320×200. Dibuja un rectángulo de color y sale con **Esc** o al cerrar la ventana. Escribe `HELLO.LOG` en el directorio actual si se puede escribir en él. |
| `MISTIFY.EXE` | `mistify.c` | Cuadriláteros animados con paleta de 256 colores y estelas degradadas. Valores predeterminados: 48 fotogramas de estela, 2 polígonos y velocidad normal. Sale al pulsar cualquier tecla o mover el ratón. `/L` activa `MISTIFY.LOG`. |
| `SBTEST.EXE` | `soundtest.c` | Reproduce una melodía original generada como PCM mono sin signo de 8 bits a 11025 Hz y muestra diagnósticos de audio/dispositivo. **Esc** sale; `/L` activa `SBTEST.LOG`. |
| `MODPLAY.EXE` | `modplay.c` | Reproduce módulos ProTracker dentro de estos límites: 31 muestras y 4 canales. Salida predeterminada: 22050 Hz, 8 bits, mono. **Space** pausa/reanuda y **Esc** sale. |

## Capturas de pantalla

Captura la pantalla DOS como PNG desde el menú **Capture → Take screenshot** de DOSBox-X. Guarda las imágenes en `docs/screenshots/` con estos nombres y súbelas junto con este README para que se vean los enlaces.

| Demo | Captura |
|---|---|
| `HELLO.EXE` | ![HELLO.EXE ejecutándose en DOSBox-X](docs/screenshots/hello.png) |
| `MISTIFY.EXE` | ![MISTIFY.EXE ejecutándose en DOSBox-X](docs/screenshots/mistify.png) |
| `SBTEST.EXE` | ![Diagnóstico de SBTEST.EXE en DOSBox-X](docs/screenshots/sbtest.png) |
| `MODPLAY.EXE` | ![MODPLAY.EXE reproduciendo un módulo en DOSBox-X](docs/screenshots/modplay.png) |

Para Mistify, usa el atajo de captura de DOSBox-X (normalmente **Ctrl+F5**; algunos teclados Mac requieren **Fn+Ctrl+F5**) en vez de mover el puntero, porque cualquier movimiento del ratón termina el programa.

Los ejemplos gráficos solicitan superficies indexadas de 320×200 cuando están disponibles. Mistify dibuja con una paleta de 256 colores. Los cuatro programas usan superficies SDL por software; el backend DOS utiliza VGA/VESA y no necesita OpenGL ni aceleración de GPU. Los textos y la ayuda visibles dentro de los programas están en inglés.

## Organización del código

Cada archivo C está organizado en secciones visibles para la configuración, el estado, la inicialización, la entrada, el bucle principal y la limpieza. Los recursos se liberan de forma segura aunque falle una etapa de inicialización; no se usan sentencias `goto`.

## Requisitos

- CMake 3.16 o posterior.
- Git.
- Un compilador cruzado DJGPP cuyo destino sea `i386-pc-msdosdjgpp`, incluidos sus binutils.
- El submódulo Git de SDL3 en `SDL/`, fijado a un commit upstream que contiene el backend DOS y el archivo `build-scripts/i586-pc-msdosdjgpp.cmake`. SDL3 **no está vendorizado** en el proyecto; el repositorio solo registra el puntero al submódulo. Al clonar, inicialízalo para obtener la revisión exacta registrada.
- Para probar en DOS, un host DPMI como `CWSDPMI.EXE`. Los ejemplos de audio requieren también una Sound Blaster compatible o una configuración de emulación equivalente.

CMake busca SDL3 en `SDL/` de forma predeterminada. Para usar un checkout en otra ubicación, pasa `-DSDL3_DOS_SOURCE_DIR=/ruta/a/SDL` al configurar.

## Preparar un toolchain DJGPP

Se puede usar cualquier toolchain DJGPP compatible. Lo siguiente es una receta opcional para macOS/Homebrew con [build-gcc](https://github.com/jwt27/build-gcc); en otros sistemas anfitriones, instala los requisitos equivalentes documentados por ese proyecto o usa un toolchain DJGPP ya instalado.

En macOS, instala las herramientas de línea de comandos y las utilidades necesarias:

```sh
xcode-select --install   # only if the tools are not already installed
brew install bash gnu-sed texinfo nasm dos2unix cmake git
```

Define una ruta de instalación configurable, clona `build-gcc` ahí una sola vez y compila el toolchain DJGPP mínimo. El valor predeterminado de abajo está bajo el directorio personal del usuario; puedes cambiar `BUILD_ROOT`.

```sh
export BUILD_ROOT="${BUILD_ROOT:-$HOME/build-gcc}"

if [ ! -d "$BUILD_ROOT/.git" ]; then
  git clone https://github.com/jwt27/build-gcc.git "$BUILD_ROOT"
fi

export DJGPP_PREFIX="${DJGPP_PREFIX:-$BUILD_ROOT/opt/djgpp}"
export PATH="$(brew --prefix bash)/bin:$(brew --prefix nasm)/bin:$(brew --prefix dos2unix)/bin:$(brew --prefix gnu-sed)/libexec/gnubin:$(brew --prefix texinfo)/bin:$PATH"
cd "$BUILD_ROOT"
"$(brew --prefix bash)/bin/bash" ./build-djgpp.sh --prefix="$DJGPP_PREFIX" minimal
```

Usa el objetivo `minimal` para este proyecto; el objetivo más amplio `all` puede intentar compilar el agregado heredado `djgpp/default`. El prefijo debe ser una ruta absoluta. En Linux u otro sistema anfitrión, instala las dependencias de compilación correspondientes y sigue las instrucciones específicas de ese sistema en la documentación de `build-gcc`, o configura `DJGPP_PREFIX` para usar un toolchain existente.

## Configurar una terminal y obtener las fuentes

Define las ubicaciones del proyecto y del toolchain en cada terminal que uses para compilar. Estos valores predeterminados son ejemplos y se pueden cambiar; `DEMO_DIR` se define explícitamente para que los comandos de compilación no dependan del directorio actual.

```sh
export BUILD_ROOT="${BUILD_ROOT:-$HOME/build-gcc}"
export DEMO_DIR="${DEMO_DIR:-$HOME/sdl3-dos-demo}"
export DJGPP_PREFIX="${DJGPP_PREFIX:-$BUILD_ROOT/opt/djgpp}"
export SDL3_DOS_SOURCE_DIR="${SDL3_DOS_SOURCE_DIR:-$DEMO_DIR/SDL}"

export DJDIR="$DJGPP_PREFIX/i386-pc-msdosdjgpp"
export GCC_EXEC_PREFIX="$DJGPP_PREFIX/lib/gcc/"
export PATH="$DJGPP_PREFIX/i386-pc-msdosdjgpp/local/bin:$DJGPP_PREFIX/i386-pc-msdosdjgpp/bin:$DJGPP_PREFIX/bin:$PATH"
export PKG_CONFIG_LIBDIR="$DJGPP_PREFIX/i386-pc-msdosdjgpp/local/lib/pkgconfig:$DJGPP_PREFIX/i386-pc-msdosdjgpp/local/share/pkgconfig:$DJGPP_PREFIX/i386-pc-msdosdjgpp/lib/pkgconfig:$DJGPP_PREFIX/i386-pc-msdosdjgpp/share/pkgconfig"
unset PKG_CONFIG_PATH
```

Comprueba que el compilador genera código para DJGPP:

```sh
i386-pc-msdosdjgpp-gcc -v 2>&1 | grep Target
```

La salida debe contener `Target: i386-pc-msdosdjgpp`. Clona el proyecto e inicializa su submódulo SDL3 fijado, o inicializa el submódulo en un checkout existente:

```sh
# Replace the project URL if the GitHub owner or repository name differs.
if [ ! -d "$DEMO_DIR/.git" ]; then
  git clone --recurse-submodules https://github.com/mmoroca/sdl3-dos-demo.git "$DEMO_DIR"
else
  git -C "$DEMO_DIR" submodule update --init --recursive
fi

export SDL3_DOS_SOURCE_DIR="${SDL3_DOS_SOURCE_DIR:-$DEMO_DIR/SDL}"
```

El checkout del submódulo queda fijado por el repositorio principal, así que `git submodule update --init --recursive` restaura exactamente la revisión registrada de SDL. Si usas un checkout de SDL independiente, define `SDL3_DOS_SOURCE_DIR` con esa ruta.

## Perfiles de compilación

El checkout de SDL proporciona el archivo de toolchain CMake para DOS. El compilador se indica explícitamente como el compilador C de DJGPP; este proyecto solo usa C.

### Compilación Release normal

```sh
cmake -S "$DEMO_DIR" -B "$DEMO_DIR/build-dos" \
  -DSDL3_DOS_SOURCE_DIR="$SDL3_DOS_SOURCE_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$SDL3_DOS_SOURCE_DIR/build-scripts/i586-pc-msdosdjgpp.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$DJGPP_PREFIX/bin/i386-pc-msdosdjgpp-gcc"

cmake --build "$DEMO_DIR/build-dos" --parallel
```

El archivo de toolchain se llama `i586-pc-msdosdjgpp.cmake`; el compilador se indica explícitamente como `i386-pc-msdosdjgpp-gcc`. CMake genera `hello.exe`, `mistify.exe`, `sbtest.exe` y `modplay.exe` dentro del directorio de compilación elegido. Si cambias de compilador o toolchain después de configurar, usa un directorio de compilación nuevo para que CMake no reutilice la caché anterior.

### Compilación Release optimizada para tamaño

Usa un directorio separado para conservar intacta la compilación normal:

```sh
cmake -S "$DEMO_DIR" -B "$DEMO_DIR/build-dos-small" \
  -DSDL3_DOS_SOURCE_DIR="$SDL3_DOS_SOURCE_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$SDL3_DOS_SOURCE_DIR/build-scripts/i586-pc-msdosdjgpp.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$DJGPP_PREFIX/bin/i386-pc-msdosdjgpp-gcc" \
  -DCMAKE_C_FLAGS_RELEASE="-Os -DNDEBUG -ffunction-sections -fdata-sections" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,--gc-sections"

cmake --build "$DEMO_DIR/build-dos-small" --parallel
```

`-Os` prioriza un menor tamaño de código; las secciones separadas de funciones/datos y `--gc-sections` permiten descartar secciones sin uso. Si el enlazador del toolchain no admite `--gc-sections`, quita esa opción y las opciones de secciones; puedes conservar `-Os -DNDEBUG`. Este perfil no solicita instrucciones exclusivas de 486.

### Compilación Release para rendimiento en 486 o superior

Este perfil independiente prioriza la velocidad frente al tamaño y genera código para procesadores 486 o posteriores; no es compatible con un 386.

```sh
cmake -S "$DEMO_DIR" -B "$DEMO_DIR/build-dos-fast" \
  -DSDL3_DOS_SOURCE_DIR="$SDL3_DOS_SOURCE_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$SDL3_DOS_SOURCE_DIR/build-scripts/i586-pc-msdosdjgpp.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$DJGPP_PREFIX/bin/i386-pc-msdosdjgpp-gcc" \
  -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG -march=i486 -mtune=i486 -ffunction-sections -fdata-sections" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,--gc-sections"

cmake --build "$DEMO_DIR/build-dos-fast" --parallel
```

Para comparar `-O3`, usa otro directorio (por ejemplo, `build-dos-fast3`) y sustituye `-O2` por `-O3`; no sobrescribas una compilación configurada con flags distintos.

En una prueba reportada con un 486 DX2/50, `MODPLAY.EXE` funcionó holgadamente a 22050 Hz, 16 bits, estéreo. A 44100 Hz, 16 bits, estéreo, la prueba con `-O2` generó unos 44.150 frames/s y tuvo un tiempo máximo de rellenado de audio de 61 ms; con `-O3` generó unos 45.090 frames/s y el máximo fue de 57 ms. La cola objetivo es de unos 65 ms, por lo que a la tasa más alta queda poco margen. Son mediciones de hardware concreto, no garantías de rendimiento.

## Ejecución en DOS o DOSBox

DJGPP genera ejecutables DPMI de 32 bits. Proporciona un host DPMI compatible, como `CWSDPMI.EXE`, junto a los programas, salvo que el entorno de ejecución ya incluya uno. Monta el directorio de compilación elegido como unidad `C:` en DOSBox o DOSBox-X.

```text
C:
HELLO.EXE
```

### Mistify

```text
MISTIFY.EXE /H
MISTIFY.EXE /T 64 /P 3 /S 1
MISTIFY.EXE /T 64 /P 3 /S 1 /L
```

`/T` fija la longitud de la estela en fotogramas (1–100), `/P` fija el número de cuadriláteros (1–8) y `/S` fija la velocidad (`1` rápida, `2` normal, `3` lenta). `/L` activa `MISTIFY.LOG`; el registro está desactivado por defecto y no escribe una línea por fotograma. Se sale al pulsar cualquier tecla o mover el ratón. El puntero se oculta durante la ejecución. Mistify solicita una superficie `INDEX8` y configura una paleta de 256 colores; si la ventana de DOSBox-X usa color verdadero, SDL convierte la superficie indexada al formato de pantalla.

### Prueba de sonido

```text
SBTEST.EXE /H
SBTEST.EXE
SBTEST.EXE /L
```

La prueba genera su propia melodía mono sin signo de 8 bits a 11025 Hz en bloques de 256 muestras y mantiene una cola corta de audio SDL. La pantalla informa del dispositivo/controlador y formatos seleccionados, la profundidad de cola, eventos de cola baja/vacía, muestras generadas, frecuencia del bucle y forma de onda. `/L` activa `SBTEST.LOG`, que registra la configuración y diagnósticos periódicos, no cada muestra. Pulsa **Esc** para detener la reproducción y salir.

El backend DOS de SDL usa la variable de entorno `BLASTER`. En DOSBox-X, configura la tarjeta emulada y comprueba que `BLASTER` coincida con sus recursos. Por ejemplo, `A220 I5 D1 H5 T6` se usó en una configuración Sound Blaster compatible probada; los valores dependen del hardware y no deben copiarse sin verificar. Si inicializas una tarjeta PnP con UNISOUND, establece los valores correctos de `BLASTER` antes de ejecutar UNISOUND.

### Reproductor MOD de cuatro canales

`MODPLAY.EXE` acepta un archivo `.MOD` (extensión sin distinguir mayúsculas/minúsculas). El parser actual se limita a módulos tipo ProTracker de 31 muestras y cuatro canales, identificados con las firmas `M.K.`, `M!K!`, `FLT4`, `4CHN` o `N.T.`.

```text
MODPLAY.EXE /H
MODPLAY.EXE MUSIC\SONG.MOD
MODPLAY.EXE /R 11025 /B 8 /C MONO MUSIC\SONG.MOD
MODPLAY.EXE /R 44100 /B 16 /C STEREO /L MUSIC\SONG.MOD
```

Los valores predeterminados son **22050 Hz, 8 bits, mono**. `/R` acepta `11025`, `22050` o `44100`; `/B`, `8` o `16`; `/C`, `MONO`/`STEREO` o `1`/`2`. Estas opciones seleccionan el formato PCM de mezcla; SDL puede convertirlo al formato admitido por el dispositivo. **Space** pausa/reanuda y **Esc** sale.

El reproductor mezcla cuatro voces PCM y admite efectos comunes de ProTracker, como arpegio, slides, portamento, vibrato, desplazamiento de muestra, cambio de volumen, salto/ruptura de patrón, velocidad/BPM y varios subefectos `E`. La reproducción de 8 bits usa lectura de muestras nearest-neighbour; la de 16 bits usa interpolación lineal. La cola objetivo es de unos 65 ms. La interfaz se refresca a 10 Hz: primero dibuja el fotograma completo y después actualiza las zonas cambiadas mediante rectángulos sucios de SDL. El ahorro de transferencia depende del modo de vídeo; una ruta de framebuffer lineal puede seguir copiando la pantalla completa. No es una implementación completa del tracker, así que algunos efectos poco frecuentes pueden ignorarse y la reproducción puede diferir de la de un reproductor MOD completo.

`/L` activa `MODPLAY.LOG`, que registra la configuración del módulo/audio, la posición y estadísticas periódicas de rellenado de audio y tiempos de dibujo. El registro está desactivado por defecto. Los eventos de cola de audio vacía son avisos útiles, pero no significan necesariamente que el buffer del dispositivo se haya quedado sin sonido.

## Licencia y software de terceros

Los archivos originales del proyecto se distribuyen bajo la licencia MIT; consulta [`LICENSE`](LICENSE). SDL3 se referencia mediante un submódulo Git y conserva la licencia upstream zlib; el repositorio principal solo guarda el puntero al submódulo, no el código fuente de SDL. Consulta [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) y el archivo `LICENSE.txt` del submódulo.

## Referencias

- [SDL3: instrucciones del backend DOS](https://wiki.libsdl.org/SDL3/README-dos)
- [SDL3: configuración con CMake](https://wiki.libsdl.org/SDL3/README-cmake)
- [Licencia de SDL](https://www.libsdl.org/license.php)
- [Captura desde el menú de DOSBox-X](https://dosbox-x.com/wiki/DOSBox%E2%80%90X%E2%80%99s-Drop%E2%80%90Down-Menus)
- [Cross-compiler DJGPP build-gcc](https://github.com/jwt27/build-gcc)
