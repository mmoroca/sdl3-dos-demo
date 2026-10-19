#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>

/* by @mmoroca & Leo.AI 2026 (http://Leo.AI) */

/*
 * Minimal SDL surface example: initialize a 320x200 window, set an indexed
 * palette when available, draw a fixed box, and quit on Esc or window close.
 * HELLO.LOG is always written in the current DOS directory when writable.
 */
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200

/* Holds the window and mapped colors so setup, drawing, and shutdown are separate. */
typedef struct HelloApp {
    SDL_Window *window;
    SDL_Surface *surface;
    bool sdl_initialized;
    Uint32 background_color;
    Uint32 box_color;
} HelloApp;

static FILE *log_file;

#define LOG(...) do { \
    if (log_file) { \
        fprintf(log_file, __VA_ARGS__); \
        fputc('\n', log_file); \
        fflush(log_file); \
    } \
} while (0)

/* -------------------------------------------------------------------------- */
/* Display initialization: palette, window surface, and mapped draw colors.   */
/* -------------------------------------------------------------------------- */
static bool set_vga_palette_if_needed(SDL_Surface *surface)
{
    if (surface->format != SDL_PIXELFORMAT_INDEX8) {
        LOG("palette: not needed for this format");
        return true;
    }

    SDL_Palette *palette = SDL_GetSurfacePalette(surface);
    if (!palette) {
        /* Some SDL/DOS builds expose INDEX8 before creating its palette. */
        palette = SDL_CreateSurfacePalette(surface);
        if (!palette) {
            LOG("palette: SDL_CreateSurfacePalette failed: %s", SDL_GetError());
            return false;
        }
        LOG("palette: created and attached a palette to the INDEX8 window surface");
    }

    /* VGA Mode 13h stores palette indices rather than RGB values in video RAM. */
    SDL_Color colors[256] = { 0 };
    for (int i = 0; i < 256; ++i) {
        colors[i].a = SDL_ALPHA_OPAQUE;
    }
    colors[0] = (SDL_Color){ 0, 0, 32, SDL_ALPHA_OPAQUE };
    colors[1] = (SDL_Color){ 255, 180, 0, SDL_ALPHA_OPAQUE };

    if (!SDL_SetPaletteColors(palette, colors, 0, 256)) {
        LOG("palette: SDL_SetPaletteColors failed: %s", SDL_GetError());
        return false;
    }

    LOG("palette: 256 entries set; background=index 0, orange=index 1");
    return true;
}

static bool initialize_hello_display(HelloApp *app)
{
    LOG("Initializing SDL video");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    app->sdl_initialized = true;
    LOG("SDL_Init OK; video driver=%s",
        SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(null)");

    app->window = SDL_CreateWindow("SDL3 DOS demo", SCREEN_WIDTH, SCREEN_HEIGHT, 0);
    if (!app->window) {
        LOG("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    LOG("window created; window format=%s",
        SDL_GetPixelFormatName(SDL_GetWindowPixelFormat(app->window)));

    /* Use the window-surface API so an INDEX8 mode can program the VGA palette. */
    app->surface = SDL_GetWindowSurface(app->window);
    if (!app->surface) {
        LOG("SDL_GetWindowSurface failed: %s", SDL_GetError());
        return false;
    }
    LOG("surface=%dx%d format=%s pitch=%d",
        app->surface->w, app->surface->h,
        SDL_GetPixelFormatName(app->surface->format), app->surface->pitch);

    if (!set_vga_palette_if_needed(app->surface)) {
        return false;
    }

    app->background_color = SDL_MapSurfaceRGB(app->surface, 0, 0, 32);
    app->box_color = SDL_MapSurfaceRGB(app->surface, 255, 180, 0);
    LOG("mapped colors: background=%lu orange=%lu",
        (unsigned long)app->background_color, (unsigned long)app->box_color);
    return true;
}

/* -------------------------------------------------------------------------- */
/* Input and control: handle Esc/close, draw the box, and present each frame.  */
/* -------------------------------------------------------------------------- */
static bool run_hello_loop(HelloApp *app, unsigned long *frame_count)
{
    bool running = true;
    SDL_Event event;
    const SDL_Rect box = { 96, 72, 128, 56 };

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                LOG("quit event received");
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                LOG("Escape received");
                running = false;
            }
        }

        if (!running) {
            break;
        }

        if (!SDL_FillSurfaceRect(app->surface, NULL, app->background_color)) {
            LOG("SDL_FillSurfaceRect(background) failed: %s", SDL_GetError());
            return false;
        }
        if (!SDL_FillSurfaceRect(app->surface, &box, app->box_color)) {
            LOG("SDL_FillSurfaceRect(box) failed: %s", SDL_GetError());
            return false;
        }
        if (!SDL_UpdateWindowSurface(app->window)) {
            LOG("SDL_UpdateWindowSurface failed: %s", SDL_GetError());
            return false;
        }

        if (*frame_count == 0) {
            LOG("first frame presented successfully");
        }
        ++(*frame_count);
        SDL_Delay(16);
    }

    return true;
}

/* -------------------------------------------------------------------------- */
/* Resource teardown: destroy the window before shutting SDL down.            */
/* -------------------------------------------------------------------------- */
static void shutdown_hello_display(HelloApp *app)
{
    if (app->window) {
        SDL_DestroyWindow(app->window);
        app->window = NULL;
        app->surface = NULL;
    }
    if (app->sdl_initialized) {
        SDL_Quit();
        app->sdl_initialized = false;
    }
}

/* -------------------------------------------------------------------------- */
/* Program entry point: logging, initialization, event loop, and exit status. */
/* -------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    HelloApp app = { 0 };
    unsigned long frames = 0;
    int exit_status = 1;
    bool initialized;

    (void)argc;
    (void)argv;

    /* Keep diagnostics in the current DOS directory after SDL restores text mode. */
    log_file = fopen("HELLO.LOG", "w");
    LOG("start");

    initialized = initialize_hello_display(&app);
    if (initialized && run_hello_loop(&app, &frames)) {
        exit_status = 0;
    }

    LOG("exit status=%d frames=%lu", exit_status, frames);
    shutdown_hello_display(&app);
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
    return exit_status;
}
