/* by @mmoroca & Arena.AI 2026 (http://arena.ai/)
 * A single reusable, header-only startup call: no SDL_Init, window, canvas,
 * audio device, renderer, mixer or SDL_Quit here. Include this file in a game
 * AFTER its SDL video/audio setup, then call sdl3_splash_show() before its
 * normal event loop. Copy this header and SPLASH/ into another SDL3 project.
 */
#ifndef SDL3_DOS_SPLASH_H
#define SDL3_DOS_SPLASH_H

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define SDL3_SPLASH_FADE_MS 500u
#define SDL3_SPLASH_TOTAL_MS 3000u

typedef enum SDL3SplashResult {
    SDL3_SPLASH_DONE,        /* Three seconds completed. */
    SDL3_SPLASH_SKIPPED,     /* Esc: proceed with the host program. */
    SDL3_SPLASH_QUIT,        /* Window close: caller shuts down normally. */
    SDL3_SPLASH_UNAVAILABLE  /* Missing/invalid image or unusable surface. */
} SDL3SplashResult;

/* Helpers are local to the including C file; there is no separate library or
 * set of duplicate SDL initialization paths. All three games use INDEX8
 * canvases and U8 mono 11025 Hz audio streams. */
static Uint8 sdl3_splash_brightness(Uint64 elapsed)
{
    if (elapsed < SDL3_SPLASH_FADE_MS)
        return (Uint8)(elapsed * 255u / SDL3_SPLASH_FADE_MS);
    if (elapsed < SDL3_SPLASH_TOTAL_MS - SDL3_SPLASH_FADE_MS) return 255;
    if (elapsed < SDL3_SPLASH_TOTAL_MS)
        return (Uint8)((SDL3_SPLASH_TOTAL_MS - elapsed) * 255u / SDL3_SPLASH_FADE_MS);
    return 0;
}

static bool sdl3_splash_asset_path(char path[512], const char *base,
                                   const char *directory, const char *name)
{
    const size_t length = strlen(directory);
    const int count = snprintf(path, 512, "%s%s%s%s", base, directory,
                               directory[length - 1] == '/' ? "" : "/", name);
    return count >= 0 && count < 512;
}

static bool sdl3_splash_present(SDL_Window *window, SDL_Surface *canvas,
                                SDL_Surface *screen, const SDL_Color colors[256],
                                Uint8 brightness)
{
    SDL_Color faded[256];
    SDL_Palette *palette = SDL_GetSurfacePalette(canvas);
    if (!palette) return false;
    for (int i = 0; i < 256; ++i) {
        faded[i].r = (Uint8)((unsigned)colors[i].r * brightness / 255u);
        faded[i].g = (Uint8)((unsigned)colors[i].g * brightness / 255u);
        faded[i].b = (Uint8)((unsigned)colors[i].b * brightness / 255u);
        faded[i].a = SDL_ALPHA_OPAQUE;
    }
    if (!SDL_SetPaletteColors(palette, faded, 0, 256)) return false;
    if (canvas != screen && !SDL_BlitSurface(canvas, NULL, screen, NULL)) return false;
    return SDL_UpdateWindowSurface(window);
}

/* The animation owns no resources: a result returns directly to the one
 * caller-side cleanup block. This keeps event exits separate from release of
 * the borrowed canvas/audio and the temporary image/WAV. */
static SDL3SplashResult sdl3_splash_animate(SDL_Window *window,
                                            SDL_Surface *canvas,
                                            SDL_Surface *screen,
                                            const SDL_Color logo[256],
                                            SDL_AudioStream *audio,
                                            const Uint8 *sound,
                                            Uint32 sound_length)
{
    Uint64 started;
    int last_level = -1;
    bool sound_started = false;

    if (!sdl3_splash_present(window, canvas, screen, logo, 0))
        return SDL3_SPLASH_UNAVAILABLE;
    started = SDL_GetTicks();
    for (;;) {
        SDL_Event event;
        Uint64 elapsed;
        Uint8 level;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) return SDL3_SPLASH_QUIT;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
                return SDL3_SPLASH_SKIPPED;
            if (event.type == SDL_EVENT_WINDOW_EXPOSED) last_level = -1;
        }
        elapsed = SDL_GetTicks() - started;
        level = sdl3_splash_brightness(elapsed);
        if (last_level != (int)level) {
            if (!sdl3_splash_present(window, canvas, screen, logo, level))
                return SDL3_SPLASH_UNAVAILABLE;
            last_level = level;
        }
        if (!sound_started && elapsed >= SDL3_SPLASH_FADE_MS) {
            /* Full-brightness frame has already been presented above. */
            if (sound) (void)SDL_PutAudioStreamData(audio, sound, (int)sound_length);
            sound_started = true;
        }
        if (elapsed >= SDL3_SPLASH_TOTAL_MS) return SDL3_SPLASH_DONE;
        SDL_Delay(10);
    }
}

/* Integration examples in this repository (one shared function, three call
 * sites): Dino calls after grayscale-video/sprite/beep setup and resets its
 * physics clock afterward; Shooter calls after RGB332/audio setup with music
 * disabled during prefill, then starts its original UFO phase; SKYHOP calls
 * after loading sprites/window/audio and before starting /L phase profiling.
 * Each caller owns its SDL shutdown path on SDL3_SPLASH_QUIT.
 *
 * The *only public call*: borrows the existing 320x200 INDEX8 canvas, SDL
 * window and optionally its already-opened U8/mono/11025 audio stream. Never
 * owns or destroys them, and never changes the active SDL subsystem state.
 * Saves/restores the caller's palette; the next game draw replaces the image.
 * Assets are looked up beside the EXE, then from the working directory.
 * Missing sound is nonfatal; malformed/missing image skips the intro.
 * Do not call during gameplay: it handles events and blocks for up to 3 s.
 * The stream is cleared around the one-shot cue so game music does not play
 * over it. Returns QUIT on an SDL close event, SKIPPED on Esc. */
static SDL3SplashResult sdl3_splash_show(SDL_Window *window,
                                         SDL_Surface *canvas,
                                         SDL_AudioStream *audio,
                                         const char *directory)
{
    SDL3SplashResult result = SDL3_SPLASH_UNAVAILABLE;
    SDL_Surface *image = NULL, *screen;
    SDL_Color original[256], logo[256];
    SDL_Palette *palette, *image_palette;
    SDL_AudioSpec sound_spec, stream_spec;
    Uint8 *sound = NULL;
    Uint32 sound_length = 0;
    char path[512];
    const char *base;
    bool touched_canvas = false;
    Uint8 black = 0;

    if (!window || !canvas || !directory || !*directory ||
        canvas->w != 320 || canvas->h != 200 ||
        canvas->format != SDL_PIXELFORMAT_INDEX8) return result;
    screen = SDL_GetWindowSurface(window);
    palette = SDL_GetSurfacePalette(canvas);
    if (!screen || screen->w != 320 || screen->h != 200 ||
        !palette || palette->ncolors != 256) return result;
    memcpy(original, palette->colors, sizeof(original));
    /* Find the darkest original index so restoring the game's palette does
     * not flash the logo or a bright background before its first own frame. */
    unsigned best = 766;
    for (int i = 0; i < 256; ++i) {
        const unsigned light = (unsigned)original[i].r + original[i].g + original[i].b;
        if (light < best) { best = light; black = (Uint8)i; }
    }
    base = SDL_GetBasePath();
    if (base && sdl3_splash_asset_path(path, base, directory, "SDL3DOS.BMP"))
        image = SDL_LoadBMP(path);
    if (!image && sdl3_splash_asset_path(path, "", directory, "SDL3DOS.BMP"))
        image = SDL_LoadBMP(path);
    image_palette = image ? SDL_GetSurfacePalette(image) : NULL;
    if (image && image->format == SDL_PIXELFORMAT_INDEX8 &&
        image->w == 320 && image->h == 200 &&
        image_palette && image_palette->ncolors == 256) {
        memcpy(logo, image_palette->colors, sizeof(logo));

        if (audio && SDL_GetAudioStreamFormat(audio, &stream_spec, NULL) &&
            stream_spec.format == SDL_AUDIO_U8 && stream_spec.channels == 1 &&
            stream_spec.freq == 11025) {
            bool loaded = false;
            if (base && sdl3_splash_asset_path(path, base, directory, "ZARVOX.WAV"))
                loaded = SDL_LoadWAV(path, &sound_spec, &sound, &sound_length);
            if (!loaded && sdl3_splash_asset_path(path, "", directory, "ZARVOX.WAV"))
                loaded = SDL_LoadWAV(path, &sound_spec, &sound, &sound_length);
            if (!loaded || sound_spec.format != SDL_AUDIO_U8 ||
                sound_spec.channels != 1 || sound_spec.freq != 11025 || !sound_length) {
                SDL_free(sound);
                sound = NULL;
            }
        }
        if (SDL_LockSurface(image)) {
            if (SDL_LockSurface(canvas)) {
                for (int row = 0; row < 200; ++row)
                    memcpy((Uint8 *)canvas->pixels + row * canvas->pitch,
                           (Uint8 *)image->pixels + row * image->pitch, 320);
                SDL_UnlockSurface(canvas);
                touched_canvas = true;
            }
            SDL_UnlockSurface(image);
        }
        if (touched_canvas) {
            if (audio) (void)SDL_ClearAudioStream(audio);
            result = sdl3_splash_animate(window, canvas, screen, logo,
                                          audio, sound, sound_length);
        }
    }

    if (audio && touched_canvas) (void)SDL_ClearAudioStream(audio);
    if (touched_canvas) {
        if (SDL_LockSurface(canvas)) {
            for (int row = 0; row < 200; ++row)
                memset((Uint8 *)canvas->pixels + row * canvas->pitch, black, 320);
            SDL_UnlockSurface(canvas);
        }
        (void)SDL_SetPaletteColors(palette, original, 0, 256);
        if (result != SDL3_SPLASH_QUIT) {
            if (canvas != screen) (void)SDL_BlitSurface(canvas, NULL, screen, NULL);
            (void)SDL_UpdateWindowSurface(window);
        }
    }
    if (sound) SDL_free(sound);
    if (image) SDL_DestroySurface(image);
    return result;
}

#endif /* SDL3_DOS_SPLASH_H */
