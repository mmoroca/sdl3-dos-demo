#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* by @mmoroca & Leo.AI 2026 (http://Leo.AI) */

/*
 * Mistify keeps polygon coordinates in 16.16 fixed point and stores recent
 * outlines in a ring buffer. Each frame is rasterized into an INDEX8 surface;
 * a key press or mouse movement exits. MISTIFY.LOG is optional (/L only).
 */
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200
#define VERTEX_COUNT 4
#define MAX_POLYGONS 8
#define MIN_POLYGONS 1
#define MAX_TRAIL_LENGTH 100
#define MIN_TRAIL_LENGTH 1
#define TRAIL_SHADES 31
#define HUE_COUNT 8
#define FIXED_ONE 65536
#define DOS_PIXEL_FORMAT_PROPERTY "SDL_PIXELFORMAT"

#define DEFAULT_TRAIL_LENGTH 48
#define DEFAULT_POLYGONS 2
#define DEFAULT_SPEED 2

typedef struct AppSettings {
    int trail_length;
    int polygon_count;
    int speed;
    bool log_enabled;
} AppSettings;

typedef struct MovingVertex {
    Sint32 x;
    Sint32 y;
    Sint32 vx;
    Sint32 vy;
} MovingVertex;

typedef struct MovingPolygon {
    MovingVertex vertices[VERTEX_COUNT];
} MovingPolygon;

typedef struct TrailVertex {
    Sint16 x;
    Sint16 y;
} TrailVertex;

/* Owns SDL resources and animation history; shutdown_mistify_app accepts partial setup. */
typedef struct MistifyApp {
    MovingPolygon polygons[MAX_POLYGONS];
    int polygon_hues[MAX_POLYGONS];
    SDL_Window *window;
    SDL_Surface *window_surface;
    SDL_Surface *drawing_surface;
    TrailVertex *history;
    int history_head;
    int history_count;
    bool sdl_initialized;
    bool cursor_hidden;
} MistifyApp;

static FILE *log_file;

#define LOG(...) do { \
    if (log_file) { \
        fprintf(log_file, __VA_ARGS__); \
        fputc('\n', log_file); \
        fflush(log_file); \
    } \
} while (0)

/* Command-line parsing: case-insensitive names, numeric values, and help text. */
static bool equals_ignore_case(const char *left, const char *right)
{
    while (*left && *right) {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
            return false;
        }
        ++left;
        ++right;
    }
    return (*left == '\0' && *right == '\0');
}

static bool parse_integer(const char *text, int *value)
{
    char *end = NULL;
    long parsed;

    if (!text || !*text) {
        return false;
    }

    parsed = strtol(text, &end, 10);
    if (end == text || *end != '\0' || parsed < -2147483647L || parsed > 2147483647L) {
        return false;
    }

    *value = (int)parsed;
    return true;
}

static void print_help(const char *program_name)
{
    const char *name = (program_name && *program_name) ? program_name : "MISTIFY";

    printf("Mistify - DOS 256-color screensaver simulation\n\n");
    printf("Usage: %s [/T frames] [/P count] [/S speed] [/L] [/H]\n\n", name);
    printf("Options:\n");
    printf("  /T, /TRAIL frames   Trail length in frames (1-100; default: 48)\n");
    printf("  /P, /POLYGONS count Number of moving quadrilaterals (1-8; default: 2)\n");
    printf("  /S, /SPEED speed    1 = fast, 2 = normal, 3 = slow (default: 2)\n");
    printf("  /L, /LOG            Write debug information to MISTIFY.LOG (off by default)\n");
    printf("  /H, /HELP           Display this help and exit\n\n");
    printf("An option value may follow the option or use ':' or '='.\n");
    printf("Examples:\n");
    printf("  MISTIFY /T 64 /P 3 /S 1\n");
    printf("  MISTIFY /TRAIL:80 /POLYGONS=2 /SPEED:3 /L\n\n");
    printf("Each quadrilateral starts with a different random color.\n");
    printf("Press any key or move the mouse to exit the animation.\n");
}

static bool parse_arguments(int argc, char **argv, AppSettings *settings, bool *show_help)
{
    int i;

    *show_help = false;
    settings->trail_length = DEFAULT_TRAIL_LENGTH;
    settings->polygon_count = DEFAULT_POLYGONS;
    settings->speed = DEFAULT_SPEED;
    settings->log_enabled = false;

    for (i = 1; i < argc; ++i) {
        const char *argument = argv[i];
        const char *option;
        const char *separator;
        const char *value_text = NULL;
        char option_name[32];
        size_t option_length;
        int value;

        if (equals_ignore_case(argument, "/h") ||
            equals_ignore_case(argument, "-h") ||
            equals_ignore_case(argument, "--help") ||
            equals_ignore_case(argument, "/help") ||
            equals_ignore_case(argument, "-help")) {
            *show_help = true;
            return true;
        }

        if (equals_ignore_case(argument, "/l") ||
            equals_ignore_case(argument, "-l") ||
            equals_ignore_case(argument, "--log") ||
            equals_ignore_case(argument, "/log") ||
            equals_ignore_case(argument, "-log")) {
            settings->log_enabled = true;
            continue;
        }

        option = argument;
        if (*option == '/' || *option == '-') {
            ++option;
            if (argument[0] == '-' && *option == '-') {
                ++option;
            }
        }

        separator = strpbrk(option, ":=");
        if (separator) {
            option_length = (size_t)(separator - option);
            value_text = separator + 1;
        } else {
            option_length = strlen(option);
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: missing value for option '%s'.\n", argument);
                return false;
            }
            value_text = argv[++i];
        }

        if (option_length == 0 || option_length >= sizeof(option_name)) {
            fprintf(stderr, "Error: invalid option '%s'.\n", argument);
            return false;
        }
        memcpy(option_name, option, option_length);
        option_name[option_length] = '\0';

        if (!parse_integer(value_text, &value)) {
            fprintf(stderr, "Error: '%s' is not a valid integer value.\n", value_text);
            return false;
        }

        if (equals_ignore_case(option_name, "T") ||
            equals_ignore_case(option_name, "TRAIL") ||
            equals_ignore_case(option_name, "TRAILS")) {
            if (value < MIN_TRAIL_LENGTH || value > MAX_TRAIL_LENGTH) {
                fprintf(stderr, "Error: trail length must be between %d and %d frames.\n",
                        MIN_TRAIL_LENGTH, MAX_TRAIL_LENGTH);
                return false;
            }
            settings->trail_length = value;
        } else if (equals_ignore_case(option_name, "P") ||
                   equals_ignore_case(option_name, "POLYGON") ||
                   equals_ignore_case(option_name, "POLYGONS")) {
            if (value < MIN_POLYGONS || value > MAX_POLYGONS) {
                fprintf(stderr, "Error: polygon count must be between %d and %d.\n",
                        MIN_POLYGONS, MAX_POLYGONS);
                return false;
            }
            settings->polygon_count = value;
        } else if (equals_ignore_case(option_name, "S") ||
                   equals_ignore_case(option_name, "SPEED")) {
            if (value < 1 || value > 3) {
                fprintf(stderr, "Error: speed must be 1 (fast), 2 (normal), or 3 (slow).\n");
                return false;
            }
            settings->speed = value;
        } else {
            fprintf(stderr, "Error: unknown option '%s'.\n", option_name);
            return false;
        }
    }

    return true;
}

/* -------------------------------------------------------------------------- */
/* Display setup: indexed VGA palette and 31 brightness levels per hue.       */
/* -------------------------------------------------------------------------- */
/* Reserve index zero for the background and give each hue 31 brightness levels. */
static bool set_indexed_palette(SDL_Surface *surface)
{
    static const Uint8 hue_rgb[HUE_COUNT][3] = {
        { 255,  48,  48 },
        { 255, 144,  32 },
        { 255, 232,  48 },
        {  72, 255,  64 },
        {  40, 224, 255 },
        {  56, 112, 255 },
        { 176,  72, 255 },
        { 255,  64, 208 }
    };
    SDL_Color colors[256];
    SDL_Palette *palette;
    int i;
    int hue;
    int shade;

    if (surface->format != SDL_PIXELFORMAT_INDEX8) {
        return SDL_SetError("The drawing surface must use SDL_PIXELFORMAT_INDEX8");
    }

    palette = SDL_GetSurfacePalette(surface);
    if (!palette) {
        palette = SDL_CreateSurfacePalette(surface);
        if (!palette) {
            return false;
        }
    }

    for (i = 0; i < 256; ++i) {
        colors[i].r = 0;
        colors[i].g = 0;
        colors[i].b = 0;
        colors[i].a = SDL_ALPHA_OPAQUE;
    }

    for (hue = 0; hue < HUE_COUNT; ++hue) {
        for (shade = 0; shade < TRAIL_SHADES; ++shade) {
            const int brightness = 24 + (shade * 231) / (TRAIL_SHADES - 1);
            const int index = 1 + (hue * TRAIL_SHADES) + shade;

            colors[index].r = (Uint8)((hue_rgb[hue][0] * brightness) / 255);
            colors[index].g = (Uint8)((hue_rgb[hue][1] * brightness) / 255);
            colors[index].b = (Uint8)((hue_rgb[hue][2] * brightness) / 255);
        }
    }

    return SDL_SetPaletteColors(palette, colors, 0, 256);
}

/* -------------------------------------------------------------------------- */
/* Animation initialization: random colors, polygon geometry, and velocities. */
/* -------------------------------------------------------------------------- */
static int random_range(int minimum, int maximum)
{
    return minimum + (rand() % (maximum - minimum + 1));
}

static void initialize_polygon_hues(int *polygon_hues)
{
    int i;

    for (i = 0; i < HUE_COUNT; ++i) {
        polygon_hues[i] = i;
    }

    for (i = HUE_COUNT - 1; i > 0; --i) {
        const int other = random_range(0, i);
        const int saved_hue = polygon_hues[i];
        polygon_hues[i] = polygon_hues[other];
        polygon_hues[other] = saved_hue;
    }
}

/* Positions and velocities use 16.16 fixed point and require no floating-point unit. */
static Sint32 random_velocity(void)
{
    Sint32 velocity = (Sint32)random_range(1, 3) * FIXED_ONE;
    if (rand() & 1) {
        velocity = -velocity;
    }
    return velocity;
}

static void initialize_polygons(MovingPolygon *polygons, int polygon_count)
{
    int polygon_index;
    int vertex_index;

    for (polygon_index = 0; polygon_index < polygon_count; ++polygon_index) {
        for (vertex_index = 0; vertex_index < VERTEX_COUNT; ++vertex_index) {
            MovingVertex *vertex = &polygons[polygon_index].vertices[vertex_index];
            vertex->x = (Sint32)random_range(12, SCREEN_WIDTH - 13) * FIXED_ONE;
            vertex->y = (Sint32)random_range(12, SCREEN_HEIGHT - 13) * FIXED_ONE;
            vertex->vx = random_velocity();
            vertex->vy = random_velocity();
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Animation control: fixed-point movement and reflection at screen edges.    */
/* -------------------------------------------------------------------------- */
static void advance_axis(Sint32 *position, Sint32 *velocity, Sint32 maximum, int speed_scale)
{
    *position += (*velocity * speed_scale) / 2;

    if (*position < 0) {
        *position = 0;
        *velocity = -*velocity;
    } else if (*position > maximum) {
        *position = maximum;
        *velocity = -*velocity;
    }
}

static void advance_polygons(MovingPolygon *polygons, int polygon_count, int speed)
{
    const int speed_scale = (speed == 1) ? 4 : ((speed == 2) ? 2 : 1);
    const Sint32 max_x = (SCREEN_WIDTH - 1) * FIXED_ONE;
    const Sint32 max_y = (SCREEN_HEIGHT - 1) * FIXED_ONE;
    int polygon_index;
    int vertex_index;

    for (polygon_index = 0; polygon_index < polygon_count; ++polygon_index) {
        for (vertex_index = 0; vertex_index < VERTEX_COUNT; ++vertex_index) {
            MovingVertex *vertex = &polygons[polygon_index].vertices[vertex_index];
            advance_axis(&vertex->x, &vertex->vx, max_x, speed_scale);
            advance_axis(&vertex->y, &vertex->vy, max_y, speed_scale);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Trail storage and software rasterizer: history, Bresenham lines, and fade. */
/* -------------------------------------------------------------------------- */
static void save_current_frame(const MovingPolygon *polygons,
                               int polygon_count,
                               TrailVertex *history,
                               int frame_index)
{
    int polygon_index;
    int vertex_index;

    for (polygon_index = 0; polygon_index < polygon_count; ++polygon_index) {
        for (vertex_index = 0; vertex_index < VERTEX_COUNT; ++vertex_index) {
            const MovingVertex *source = &polygons[polygon_index].vertices[vertex_index];
            const size_t index = (((size_t)frame_index * (size_t)polygon_count +
                                   (size_t)polygon_index) * VERTEX_COUNT) +
                                 (size_t)vertex_index;
            history[index].x = (Sint16)(source->x >> 16);
            history[index].y = (Sint16)(source->y >> 16);
        }
    }
}

static const TrailVertex *get_history_vertex(const TrailVertex *history,
                                             int frame_index,
                                             int polygon_index,
                                             int vertex_index,
                                             int polygon_count)
{
    const size_t index = (((size_t)frame_index * (size_t)polygon_count +
                           (size_t)polygon_index) * VERTEX_COUNT) +
                         (size_t)vertex_index;
    return &history[index];
}

static void draw_line(Uint8 *pixels, int pitch, int x0, int y0, int x1, int y1, Uint8 color)
{
    const int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    const int sx = (x0 < x1) ? 1 : -1;
    const int dy = -((y1 >= y0) ? (y1 - y0) : (y0 - y1));
    const int sy = (y0 < y1) ? 1 : -1;
    int error = dx + dy;
    int x = x0;
    int y = y0;

    for (;;) {
        if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
            pixels[(size_t)y * (size_t)pitch + (size_t)x] = color;
        }
        if (x == x1 && y == y1) {
            break;
        }

        {
            const int twice_error = 2 * error;
            if (twice_error >= dy) {
                error += dy;
                x += sx;
            }
            if (twice_error <= dx) {
                error += dx;
                y += sy;
            }
        }
    }
}

/* Draw old, dim trail outlines first so the newest outlines remain brightest. */
static bool draw_frame(SDL_Surface *surface,
                       const MovingPolygon *polygons,
                       const int *polygon_hues,
                       int polygon_count,
                       TrailVertex *history,
                       int trail_length,
                       int *history_head,
                       int *history_count)
{
    int age;
    int polygon_index;

    *history_head = (*history_head + 1) % trail_length;
    save_current_frame(polygons, polygon_count, history, *history_head);
    if (*history_count < trail_length) {
        ++*history_count;
    }

    if (!SDL_LockSurface(surface)) {
        return false;
    }

    SDL_memset(surface->pixels, 0, (size_t)surface->pitch * (size_t)surface->h);

    for (age = *history_count - 1; age >= 0; --age) {
        int frame_index = (*history_head - age + trail_length) % trail_length;
        int shade;

        if (trail_length <= 1) {
            shade = TRAIL_SHADES - 1;
        } else {
            shade = (TRAIL_SHADES - 1) -
                    (age * (TRAIL_SHADES - 1)) / (trail_length - 1);
        }

        for (polygon_index = 0; polygon_index < polygon_count; ++polygon_index) {
            const Uint8 color = (Uint8)(1 + polygon_hues[polygon_index] * TRAIL_SHADES + shade);
            int vertex_index;

            for (vertex_index = 0; vertex_index < VERTEX_COUNT; ++vertex_index) {
                const int next_vertex = (vertex_index + 1) % VERTEX_COUNT;
                const TrailVertex *first = get_history_vertex(history, frame_index,
                                                              polygon_index, vertex_index,
                                                              polygon_count);
                const TrailVertex *second = get_history_vertex(history, frame_index,
                                                               polygon_index, next_vertex,
                                                               polygon_count);
                draw_line((Uint8 *)surface->pixels, surface->pitch,
                          first->x, first->y, second->x, second->y, color);
            }
        }
    }

    SDL_UnlockSurface(surface);
    return true;
}

/* -------------------------------------------------------------------------- */
/* SDL window setup: request a 320x200 INDEX8 surface from the DOS video port. */
/* -------------------------------------------------------------------------- */
static bool create_window(SDL_Window **window, SDL_Surface **window_surface)
{
    SDL_PropertiesID properties = SDL_CreateProperties();

    if (!properties) {
        fprintf(stderr, "Error: could not create SDL window properties: %s\n", SDL_GetError());
        return false;
    }

    /* Request a palette-based 8-bit display mode from the DOS video driver. */
    if (!SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING,
                               "Mistify - SDL3 DOS") ||
        !SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, SCREEN_WIDTH) ||
        !SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, SCREEN_HEIGHT) ||
        !SDL_SetNumberProperty(properties, DOS_PIXEL_FORMAT_PROPERTY, SDL_PIXELFORMAT_INDEX8)) {
        fprintf(stderr, "Error: could not set SDL window properties: %s\n", SDL_GetError());
        SDL_DestroyProperties(properties);
        return false;
    }

    *window = SDL_CreateWindowWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (!*window) {
        fprintf(stderr, "Error: could not create the SDL window: %s\n", SDL_GetError());
        return false;
    }

    *window_surface = SDL_GetWindowSurface(*window);
    if (!*window_surface) {
        fprintf(stderr, "Error: could not get the SDL window surface: %s\n", SDL_GetError());
        return false;
    }

    if ((*window_surface)->w != SCREEN_WIDTH || (*window_surface)->h != SCREEN_HEIGHT) {
        fprintf(stderr, "Error: expected a %dx%d display, but SDL created %dx%d.\n",
                SCREEN_WIDTH, SCREEN_HEIGHT, (*window_surface)->w, (*window_surface)->h);
        return false;
    }

    return true;
}

/* -------------------------------------------------------------------------- */
/* Application initialization: trail memory, random state, video, and cursor.  */
/* -------------------------------------------------------------------------- */
static bool initialize_mistify_app(MistifyApp *app, const AppSettings *settings)
{
    size_t history_vertex_count;
    int polygon_index;

    app->history_head = -1;
    history_vertex_count = (size_t)settings->trail_length *
                           (size_t)settings->polygon_count * VERTEX_COUNT;
    app->history = (TrailVertex *)malloc(history_vertex_count * sizeof(*app->history));
    if (!app->history) {
        fprintf(stderr, "Error: not enough memory for the trail buffer.\n");
        LOG("Error: not enough memory for the trail buffer");
        return false;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "Error: SDL_Init failed: %s\n", SDL_GetError());
        LOG("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    app->sdl_initialized = true;
    LOG("SDL_Init OK; video driver=%s",
        SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(unknown)");

    /* Randomize both the initial polygon colors and their starting geometry. */
    {
        const Uint64 timer_counter = SDL_GetPerformanceCounter();
        unsigned int seed = (unsigned int)time(NULL) ^
                            (unsigned int)timer_counter ^
                            (unsigned int)(timer_counter >> 32);
        if (seed == 0) {
            seed = 0x4D495354U;
        }
        srand(seed);
    }
    initialize_polygon_hues(app->polygon_hues);
    initialize_polygons(app->polygons, settings->polygon_count);
    for (polygon_index = 0; polygon_index < settings->polygon_count; ++polygon_index) {
        LOG("Polygon %d starting hue=%d", polygon_index + 1,
            app->polygon_hues[polygon_index]);
    }

    if (!create_window(&app->window, &app->window_surface)) {
        LOG("Window creation failed: %s", SDL_GetError());
        return false;
    }

    if (!SDL_HideCursor()) {
        fprintf(stderr, "Error: could not hide the mouse cursor: %s\n", SDL_GetError());
        LOG("SDL_HideCursor failed: %s", SDL_GetError());
        return false;
    }
    app->cursor_hidden = true;
    SDL_PumpEvents();
    SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION);
    LOG("Mouse cursor hidden; initial mouse motion events flushed");

    LOG("Window surface: %dx%d format=%s pitch=%d",
        app->window_surface->w, app->window_surface->h,
        SDL_GetPixelFormatName(app->window_surface->format), app->window_surface->pitch);

    if (app->window_surface->format == SDL_PIXELFORMAT_INDEX8) {
        app->drawing_surface = app->window_surface;
        if (!set_indexed_palette(app->drawing_surface)) {
            fprintf(stderr, "Error: could not set the VGA palette: %s\n", SDL_GetError());
            LOG("Palette setup failed: %s", SDL_GetError());
            return false;
        }
        LOG("Using the 320x200 INDEX8 window surface with a 256-entry palette");
    } else {
        app->drawing_surface = SDL_CreateSurface(SCREEN_WIDTH, SCREEN_HEIGHT,
                                                 SDL_PIXELFORMAT_INDEX8);
        if (!app->drawing_surface) {
            fprintf(stderr, "Error: could not create the indexed drawing surface: %s\n",
                    SDL_GetError());
            LOG("Indexed surface creation failed: %s", SDL_GetError());
            return false;
        }
        if (!set_indexed_palette(app->drawing_surface)) {
            fprintf(stderr, "Error: could not set the drawing palette: %s\n", SDL_GetError());
            LOG("Drawing palette setup failed: %s", SDL_GetError());
            return false;
        }
        LOG("Window surface is not indexed; using an INDEX8 source surface and palette blit");
    }

    return true;
}

/* -------------------------------------------------------------------------- */
/* Main loop: any key, mouse motion, or close event exits; cursor stays hidden. */
/* -------------------------------------------------------------------------- */
static bool run_mistify_loop(MistifyApp *app, const AppSettings *settings)
{
    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                LOG("Key press received; exiting");
                running = false;
            } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                LOG("Mouse motion received; exiting");
                running = false;
            }
        }

        if (!running) {
            break;
        }

        advance_polygons(app->polygons, settings->polygon_count, settings->speed);
        if (!draw_frame(app->drawing_surface, app->polygons, app->polygon_hues,
                        settings->polygon_count, app->history, settings->trail_length,
                        &app->history_head, &app->history_count)) {
            fprintf(stderr, "Error: could not lock the drawing surface: %s\n", SDL_GetError());
            LOG("Surface lock failed: %s", SDL_GetError());
            return false;
        }

        if (app->drawing_surface != app->window_surface &&
            !SDL_BlitSurface(app->drawing_surface, NULL, app->window_surface, NULL)) {
            fprintf(stderr, "Error: could not copy the indexed frame to the display: %s\n",
                    SDL_GetError());
            LOG("SDL_BlitSurface failed: %s", SDL_GetError());
            return false;
        }

        if (!SDL_UpdateWindowSurface(app->window)) {
            fprintf(stderr, "Error: could not present the frame: %s\n", SDL_GetError());
            LOG("SDL_UpdateWindowSurface failed: %s", SDL_GetError());
            return false;
        }

        SDL_Delay(10);
    }

    return true;
}

/* -------------------------------------------------------------------------- */
/* Resource teardown: free owned surfaces/history and restore the cursor.     */
/* -------------------------------------------------------------------------- */
static void shutdown_mistify_app(MistifyApp *app)
{
    if (app->drawing_surface && app->drawing_surface != app->window_surface) {
        SDL_DestroySurface(app->drawing_surface);
        app->drawing_surface = NULL;
    }
    if (app->window) {
        SDL_DestroyWindow(app->window);
        app->window = NULL;
        app->window_surface = NULL;
    }
    if (app->cursor_hidden && app->sdl_initialized) {
        if (!SDL_ShowCursor()) {
            LOG("SDL_ShowCursor failed during cleanup: %s", SDL_GetError());
        }
        app->cursor_hidden = false;
    }
    if (app->sdl_initialized) {
        SDL_Quit();
        app->sdl_initialized = false;
    }
    free(app->history);
    app->history = NULL;
}

/* -------------------------------------------------------------------------- */
/* Program entry point: parse options, optionally log, then run the app.       */
/* -------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    AppSettings settings;
    MistifyApp app = { 0 };
    bool show_help;
    bool app_ok;
    int result;

    if (!parse_arguments(argc, argv, &settings, &show_help)) {
        print_help(argv[0]);
        return 1;
    }
    if (show_help) {
        print_help(argv[0]);
        return 0;
    }

    if (settings.log_enabled) {
        log_file = fopen("MISTIFY.LOG", "w");
        if (!log_file) {
            fprintf(stderr, "Warning: could not open MISTIFY.LOG; continuing without debug logging.\n");
        }
    }
    LOG("Start");
    LOG("Settings: trail_length=%d polygons=%d speed=%d",
        settings.trail_length, settings.polygon_count, settings.speed);

    app_ok = initialize_mistify_app(&app, &settings);
    if (app_ok) {
        app_ok = run_mistify_loop(&app, &settings);
    }
    result = app_ok ? 0 : 1;
    LOG("Exit status=%d", result);

    shutdown_mistify_app(&app);
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
    return result;
}
