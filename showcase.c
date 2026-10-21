#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* by @mmoroca & Arena.AI 2026 (http://arena.ai/) */

/*
 * SHOWCASE is a small, function-oriented SDL3/DOS starter program. It requests
 * a 320x200 INDEX8 display (Mode-13h-style on the DOS backend), fades the VGA
 * palette, draws four information/game panels, accepts text and key input,
 * optionally polls a joystick, mixes WAV clips, and persists a name/settings
 * file plus a best score. Replace update_shooter() and draw_shooter_panel()
 * with the application-specific behavior you want to demonstrate.
 *
 * The OGG source tracks are converted off-line to U8/mono/11025-Hz WAV files;
 * this executable intentionally does not depend on an OGG/Vorbis decoder.
 */
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200
#define PANEL_WIDTH 160
#define PANEL_HEIGHT 100
#define SHOOTER_X 160
#define SHOOTER_Y 100
#define SHOOTER_INNER_X (SHOOTER_X + 1)
#define SHOOTER_INNER_Y (SHOOTER_Y + 1)
#define SHOOTER_INNER_W (PANEL_WIDTH - 2)
#define SHOOTER_INNER_H (PANEL_HEIGHT - 2)

#define DOS_PIXEL_FORMAT_PROPERTY "SDL_PIXELFORMAT"
#define FIXED_POINT_SHIFT 8
#define FIXED_POINT_ONE (1 << FIXED_POINT_SHIFT)
/* 4 = quarter linear size; use 2/3 for larger sprites or 1 for native size. */
#define SPRITE_SCALE_DIVISOR 4
#define TRANSPARENT_INDEX 0
#define CALLSIGN_CAPACITY 9
#define STAR_COUNT 30
#define LASER_COUNT 4
#define AUDIO_SAMPLE_RATE 11025
#define AUDIO_CHUNK_FRAMES 256
#define AUDIO_QUEUE_TARGET_BYTES (AUDIO_CHUNK_FRAMES * 3)
#define AUDIO_QUEUE_LIMIT_BYTES (AUDIO_CHUNK_FRAMES * 8)
#define AUDIO_LOOP_DELAY_MS 16
#define FADE_STEP_MS 20
#define LASER_COOLDOWN_MS 180
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* These indices are selected from the shared RGB 3:3:2 palette. */
#define COLOR_BLACK 0
#define COLOR_BLUE 3
#define COLOR_CYAN 31
#define COLOR_GREEN 28
#define COLOR_YELLOW 252
#define COLOR_RED 224
#define COLOR_WHITE 255
#define COLOR_GRAY 146
#define COLOR_ORANGE 240
#define COLOR_PURPLE 131
#define COLOR_DIM 2

#define CONFIG_FILE "SHOWCASE.CFG"
#define SCORE_FILE "SHOWCASE.SCO"
#define LOG_FILE "SHOWCASE.LOG"
#define SHIP_BMP "SHOWCASE/PLAYER~1.BMP"
#define UFO_BMP "SHOWCASE/UFOGREEN.BMP"
#define LASER_BMP "SHOWCASE/LASERR~1.BMP"
#define MUSIC_WAV "SHOWCASE/MUSIC.WAV"
#define LASER_WAV "SHOWCASE/LASER.WAV"
#define HIT_WAV "SHOWCASE/HIT.WAV"
#define ENGINE_WAV "SHOWCASE/ENGINE.WAV"

typedef struct Sprite {
    SDL_Surface *surface;
    int width;
    int height;
} Sprite;

typedef struct PcmClip {
    Uint8 *samples;
    Uint32 length;
    Uint32 position;
    bool active;
} PcmClip;

typedef struct UserSettings {
    char callsign[CALLSIGN_CAPACITY];
    Uint8 master_volume;
    bool music_enabled;
} UserSettings;

typedef struct ScoreRecord {
    int best_score;
    char callsign[CALLSIGN_CAPACITY];
} ScoreRecord;

typedef struct Star {
    int x;
    int y;
    Uint8 speed_pixels_per_second;
    Uint8 color;
    Uint32 fractional_motion;
    Uint8 twinkle;
} Star;

typedef struct Laser {
    int x;
    Sint32 y_fp;
    bool active;
} Laser;

typedef struct PaletteState {
    SDL_Color base[256];
    Uint8 brightness;
    Uint8 target_brightness;
    Uint64 last_step_ticks;
} PaletteState;

typedef struct AudioState {
    SDL_AudioStream *stream;
    SDL_AudioDeviceID device;
    SDL_AudioSpec source_spec;
    PcmClip music;
    PcmClip laser;
    PcmClip hit;
    PcmClip engine;
    char driver_name[24];
    char device_name[32];
    char blaster_text[32];
    bool ready;
    bool asset_warning;
    bool music_available;
    bool laser_available;
    bool hit_available;
    bool engine_available;
} AudioState;

typedef struct InputState {
    bool key_left;
    bool key_right;
    bool key_up;
    bool key_down;
    bool key_fire;
    bool joystick_left;
    bool joystick_right;
    bool joystick_up;
    bool joystick_down;
    bool joystick_buttons[4];
    bool joystick_present;
    bool editing_name;
    bool pending_fallback_valid;
    char pending_fallback_char;
    Uint64 pending_fallback_ticks;
    Sint16 joystick_axis_x;
    Sint16 joystick_axis_y;
    Uint8 joystick_hat;
} InputState;

typedef struct GameState {
    Sint32 player_x_fp;
    Sint32 player_y_fp;
    Sint32 enemy_x_fp;
    int enemy_y;
    int enemy_direction;
    int score;
    Uint64 last_fire_ticks;
    Uint64 flash_until_ticks;
    bool engine_active;
    Star stars[STAR_COUNT];
    Laser lasers[LASER_COUNT];
} GameState;

typedef struct ShowcaseApp {
    SDL_Window *window;
    SDL_Surface *window_surface;
    SDL_Surface *canvas;
    SDL_Joystick *joystick;
    SDL_JoystickID joystick_id;
    PaletteState palette;
    AudioState audio;
    InputState input;
    UserSettings settings;
    ScoreRecord record;
    Sprite ship;
    Sprite ufo;
    Sprite laser_sprite;
    GameState game;
    char status_text[40];
    Uint64 status_until_ticks;
    bool sdl_initialized;
    bool running;
} ShowcaseApp;

static FILE *log_file;

#define LOG(...) do { \
    if (log_file) { \
        fprintf(log_file, __VA_ARGS__); \
        fputc('\n', log_file); \
        fflush(log_file); \
    } \
} while (0)

/* -------------------------------------------------------------------------- */
/* Small text helpers: bounded copies, uppercase diagnostics, and status text. */
/* -------------------------------------------------------------------------- */
static void copy_upper_text(char *destination, size_t capacity, const char *source)
{
    size_t index = 0;

    if (capacity == 0) {
        return;
    }
    if (!source) {
        source = "";
    }
    while (source[index] && index + 1 < capacity) {
        destination[index] = (char)toupper((unsigned char)source[index]);
        ++index;
    }
    destination[index] = '\0';
}

static bool is_callsign_character(unsigned char character)
{
    return ((character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') ||
            character == '-' || character == '_');
}

static void copy_callsign(char destination[CALLSIGN_CAPACITY], const char *source)
{
    const unsigned char *cursor = (const unsigned char *)source;
    size_t length = 0;

    if (!cursor) {
        cursor = (const unsigned char *)"";
    }
    while (*cursor && length + 1 < CALLSIGN_CAPACITY) {
        const unsigned char character = *cursor++;
        if (is_callsign_character(character)) {
            destination[length++] = (char)toupper(character);
        }
    }
    destination[length] = '\0';
}

static void set_status(ShowcaseApp *app, const char *text, Uint32 duration_ms)
{
    (void)snprintf(app->status_text, sizeof(app->status_text), "%s", text ? text : "");
    app->status_until_ticks = SDL_GetTicks() + (Uint64)duration_ms;
}

static bool parse_long_value(const char *text, long minimum, long maximum, long *value)
{
    char *end = NULL;
    long parsed;

    if (!text || !*text || !value) {
        return false;
    }
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed < minimum || parsed > maximum) {
        return false;
    }
    *value = parsed;
    return true;
}

/* -------------------------------------------------------------------------- */
/* File examples: simple text configuration and best-score records in CWD.     */
/* -------------------------------------------------------------------------- */
static void set_default_data(ShowcaseApp *app)
{
    copy_callsign(app->settings.callsign, "CADET");
    app->settings.master_volume = 200;
    app->settings.music_enabled = true;
    app->record.best_score = 0;
    copy_callsign(app->record.callsign, "---");
}

static bool load_settings(ShowcaseApp *app)
{
    FILE *file = fopen(CONFIG_FILE, "r");
    char line[96];
    UserSettings loaded = app->settings;
    bool found_any = false;

    if (!file) {
        LOG("Config not found; using defaults: %s", CONFIG_FILE);
        return false;
    }

    while (fgets(line, sizeof(line), file)) {
        char *separator = strchr(line, '=');
        char *value;
        size_t length;
        long parsed;

        if (!separator) {
            continue;
        }
        *separator = '\0';
        value = separator + 1;
        length = strlen(value);
        while (length > 0 && (value[length - 1] == '\n' || value[length - 1] == '\r')) {
            value[--length] = '\0';
        }

        if (strcmp(line, "name") == 0) {
            copy_callsign(loaded.callsign, value);
            found_any = true;
        } else if (strcmp(line, "volume") == 0 &&
                   parse_long_value(value, 0, 255, &parsed)) {
            loaded.master_volume = (Uint8)parsed;
            found_any = true;
        } else if (strcmp(line, "music") == 0 &&
                   parse_long_value(value, 0, 1, &parsed)) {
            loaded.music_enabled = (parsed != 0);
            found_any = true;
        }
    }

    if (ferror(file)) {
        LOG("Error reading %s", CONFIG_FILE);
        found_any = false;
    }
    (void)fclose(file);
    if (loaded.callsign[0] == '\0') {
        copy_callsign(loaded.callsign, "CADET");
    }
    app->settings = loaded;
    LOG("Config loaded: name=%s volume=%u music=%d",
        app->settings.callsign, (unsigned int)app->settings.master_volume,
        app->settings.music_enabled ? 1 : 0);
    return found_any;
}

static bool save_settings(const ShowcaseApp *app)
{
    FILE *file = fopen(CONFIG_FILE, "w");
    bool ok = true;

    if (!file) {
        LOG("Could not open %s for writing", CONFIG_FILE);
        return false;
    }
    if (fprintf(file, "version=1\nname=%s\nvolume=%u\nmusic=%u\n",
                app->settings.callsign,
                (unsigned int)app->settings.master_volume,
                app->settings.music_enabled ? 1U : 0U) < 0) {
        ok = false;
    }
    if (fclose(file) != 0) {
        ok = false;
    }
    LOG("Config save %s: %s", ok ? "OK" : "FAILED", CONFIG_FILE);
    return ok;
}

static bool load_score_record(ShowcaseApp *app)
{
    FILE *file = fopen(SCORE_FILE, "r");
    char line[96];
    ScoreRecord loaded = app->record;
    bool found_score = false;

    if (!file) {
        LOG("Score record not found; starting at zero: %s", SCORE_FILE);
        return false;
    }

    while (fgets(line, sizeof(line), file)) {
        char *separator = strchr(line, '=');
        char *value;
        size_t length;
        long parsed;

        if (!separator) {
            continue;
        }
        *separator = '\0';
        value = separator + 1;
        length = strlen(value);
        while (length > 0 && (value[length - 1] == '\n' || value[length - 1] == '\r')) {
            value[--length] = '\0';
        }

        if (strcmp(line, "best") == 0 &&
            parse_long_value(value, 0, 2000000000L, &parsed)) {
            loaded.best_score = (int)parsed;
            found_score = true;
        } else if (strcmp(line, "name") == 0) {
            copy_callsign(loaded.callsign, value);
        }
    }

    if (ferror(file)) {
        LOG("Error reading %s", SCORE_FILE);
        found_score = false;
    }
    (void)fclose(file);
    app->record = loaded;
    LOG("Score record loaded: best=%d name=%s",
        app->record.best_score, app->record.callsign);
    return found_score;
}

static bool save_score_record(const ShowcaseApp *app)
{
    FILE *file = fopen(SCORE_FILE, "w");
    bool ok = true;

    if (!file) {
        LOG("Could not open %s for writing", SCORE_FILE);
        return false;
    }
    if (fprintf(file, "version=1\nbest=%d\nname=%s\n",
                app->record.best_score, app->record.callsign) < 0) {
        ok = false;
    }
    if (fclose(file) != 0) {
        ok = false;
    }
    LOG("Score save %s: best=%d", ok ? "OK" : "FAILED", app->record.best_score);
    return ok;
}

/* -------------------------------------------------------------------------- */
/* Palette/video setup: request an indexed 320x200 SDL window, then fade RGB.  */
/* The DOS SDL backend selects its VGA/VESA mode; this code does not call BIOS. */
/* -------------------------------------------------------------------------- */
static bool set_surface_palette(SDL_Surface *surface, const SDL_Color colors[256])
{
    SDL_Palette *palette;

    if (!surface || surface->format != SDL_PIXELFORMAT_INDEX8) {
        return true;
    }
    palette = SDL_GetSurfacePalette(surface);
    if (!palette) {
        palette = SDL_CreateSurfacePalette(surface);
        if (!palette) {
            return false;
        }
    }
    return SDL_SetPaletteColors(palette, colors, 0, 256);
}

static void build_rgb332_palette(PaletteState *palette)
{
    int index;

    for (index = 0; index < 256; ++index) {
        const int red_level = (index >> 5) & 0x07;
        const int green_level = (index >> 2) & 0x07;
        const int blue_level = index & 0x03;
        palette->base[index].r = (Uint8)((red_level * 255 + 3) / 7);
        palette->base[index].g = (Uint8)((green_level * 255 + 3) / 7);
        palette->base[index].b = (Uint8)((blue_level * 255 + 1) / 3);
        palette->base[index].a = SDL_ALPHA_OPAQUE;
    }
}

static bool apply_palette_brightness(ShowcaseApp *app, Uint8 level)
{
    SDL_Color faded[256];
    int index;

    if (level > 16) {
        level = 16;
    }
    for (index = 0; index < 256; ++index) {
        faded[index].r = (Uint8)(((unsigned int)app->palette.base[index].r * level) / 16U);
        faded[index].g = (Uint8)(((unsigned int)app->palette.base[index].g * level) / 16U);
        faded[index].b = (Uint8)(((unsigned int)app->palette.base[index].b * level) / 16U);
        faded[index].a = SDL_ALPHA_OPAQUE;
    }

    if (!set_surface_palette(app->canvas, faded)) {
        LOG("Canvas palette update failed: %s", SDL_GetError());
        return false;
    }
    if (app->window_surface != app->canvas &&
        app->window_surface && app->window_surface->format == SDL_PIXELFORMAT_INDEX8 &&
        !set_surface_palette(app->window_surface, faded)) {
        LOG("Window palette update failed: %s", SDL_GetError());
        return false;
    }
    app->palette.brightness = level;
    return true;
}

static bool initialize_video(ShowcaseApp *app)
{
    SDL_PropertiesID properties;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "Error: SDL video initialization failed: %s\n", SDL_GetError());
        LOG("SDL_Init(SDL_INIT_VIDEO) failed: %s", SDL_GetError());
        return false;
    }
    app->sdl_initialized = true;
    (void)SDL_HideCursor();
    LOG("SDL video driver: %s",
        SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(unknown)");

    properties = SDL_CreateProperties();
    if (!properties) {
        fprintf(stderr, "Error: could not create window properties: %s\n", SDL_GetError());
        return false;
    }
    if (!SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING,
                               "SDL3 Systems Showcase") ||
        !SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER,
                               SCREEN_WIDTH) ||
        !SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER,
                               SCREEN_HEIGHT) ||
        !SDL_SetNumberProperty(properties, DOS_PIXEL_FORMAT_PROPERTY,
                               SDL_PIXELFORMAT_INDEX8)) {
        fprintf(stderr, "Error: could not request the indexed 320x200 display: %s\n",
                SDL_GetError());
        SDL_DestroyProperties(properties);
        return false;
    }

    app->window = SDL_CreateWindowWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (!app->window) {
        fprintf(stderr, "Error: SDL could not create the display: %s\n", SDL_GetError());
        return false;
    }
    app->window_surface = SDL_GetWindowSurface(app->window);
    if (!app->window_surface) {
        fprintf(stderr, "Error: SDL_GetWindowSurface failed: %s\n", SDL_GetError());
        return false;
    }
    if (app->window_surface->w != SCREEN_WIDTH || app->window_surface->h != SCREEN_HEIGHT) {
        fprintf(stderr, "Error: expected 320x200, got %dx%d.\n",
                app->window_surface->w, app->window_surface->h);
        return false;
    }

    if (app->window_surface->format == SDL_PIXELFORMAT_INDEX8) {
        app->canvas = app->window_surface;
    } else {
        app->canvas = SDL_CreateSurface(SCREEN_WIDTH, SCREEN_HEIGHT,
                                        SDL_PIXELFORMAT_INDEX8);
        if (!app->canvas) {
            fprintf(stderr, "Error: could not create INDEX8 canvas: %s\n", SDL_GetError());
            return false;
        }
    }

    build_rgb332_palette(&app->palette);
    if (!set_surface_palette(app->canvas, app->palette.base) ||
        (app->window_surface != app->canvas &&
         app->window_surface->format == SDL_PIXELFORMAT_INDEX8 &&
         !set_surface_palette(app->window_surface, app->palette.base))) {
        fprintf(stderr, "Error: could not set the 256-color palette: %s\n", SDL_GetError());
        return false;
    }
    app->palette.brightness = 16;
    app->palette.target_brightness = 0;
    app->palette.last_step_ticks = SDL_GetTicks();
    if (!apply_palette_brightness(app, 0)) {
        fprintf(stderr, "Error: could not initialize the palette: %s\n", SDL_GetError());
        return false;
    }

    LOG("Display ready: %dx%d window=%s canvas=%s",
        SCREEN_WIDTH, SCREEN_HEIGHT,
        SDL_GetPixelFormatName(app->window_surface->format),
        SDL_GetPixelFormatName(app->canvas->format));
    return true;
}

static void update_palette_fade(ShowcaseApp *app, Uint64 now)
{
    if (app->palette.brightness == app->palette.target_brightness ||
        now - app->palette.last_step_ticks < FADE_STEP_MS) {
        return;
    }
    app->palette.last_step_ticks = now;
    if (app->palette.brightness < app->palette.target_brightness) {
        ++app->palette.brightness;
    } else {
        --app->palette.brightness;
    }
    (void)apply_palette_brightness(app, app->palette.brightness);
}

/* -------------------------------------------------------------------------- */
/* Drawing primitives and a compact built-in 5x7 font (no SDL_ttf dependency). */
/* -------------------------------------------------------------------------- */
typedef struct Glyph {
    char character;
    Uint8 rows[7];
} Glyph;

static const Glyph font_5x7[] = {
    { 'A', { 14,17,17,31,17,17,17 } }, { 'B', { 30,17,17,30,17,17,30 } },
    { 'C', { 14,17,16,16,16,17,14 } }, { 'D', { 30,17,17,17,17,17,30 } },
    { 'E', { 31,16,16,30,16,16,31 } }, { 'F', { 31,16,16,30,16,16,16 } },
    { 'G', { 14,17,16,23,17,17,15 } }, { 'H', { 17,17,17,31,17,17,17 } },
    { 'I', { 31,4,4,4,4,4,31 } }, { 'J', { 7,2,2,2,18,18,12 } },
    { 'K', { 17,18,20,24,20,18,17 } }, { 'L', { 16,16,16,16,16,16,31 } },
    { 'M', { 17,27,21,21,17,17,17 } }, { 'N', { 17,25,21,19,17,17,17 } },
    { 'O', { 14,17,17,17,17,17,14 } }, { 'P', { 30,17,17,30,16,16,16 } },
    { 'Q', { 14,17,17,17,21,18,13 } }, { 'R', { 30,17,17,30,20,18,17 } },
    { 'S', { 15,16,16,14,1,1,30 } }, { 'T', { 31,4,4,4,4,4,4 } },
    { 'U', { 17,17,17,17,17,17,14 } }, { 'V', { 17,17,17,17,17,10,4 } },
    { 'W', { 17,17,17,21,21,21,10 } }, { 'X', { 17,17,10,4,10,17,17 } },
    { 'Y', { 17,17,10,4,4,4,4 } }, { 'Z', { 31,1,2,4,8,16,31 } },
    { '0', { 14,17,19,21,25,17,14 } }, { '1', { 4,12,4,4,4,4,14 } },
    { '2', { 14,17,1,2,4,8,31 } }, { '3', { 30,1,1,14,1,1,30 } },
    { '4', { 2,6,10,18,31,2,2 } }, { '5', { 31,16,16,30,1,1,30 } },
    { '6', { 14,16,16,30,17,17,14 } }, { '7', { 31,1,2,4,8,8,8 } },
    { '8', { 14,17,17,14,17,17,14 } }, { '9', { 14,17,17,15,1,1,14 } },
    { '!', { 4,4,4,4,4,0,4 } }, { '?', { 14,17,1,2,4,0,4 } },
    { ':', { 0,4,4,0,4,4,0 } }, { ';', { 0,4,4,0,4,4,8 } },
    { '=', { 0,31,0,31,0,0,0 } }, { '.', { 0,0,0,0,0,4,4 } },
    { ',', { 0,0,0,0,0,4,8 } }, { '-', { 0,0,0,31,0,0,0 } },
    { '+', { 0,4,4,31,4,4,0 } }, { '/', { 1,2,2,4,8,8,16 } },
    { '_', { 0,0,0,0,0,0,31 } }, { '%', { 17,2,4,8,17,0,0 } },
    { '[', { 14,8,8,8,8,8,14 } }, { ']', { 14,2,2,2,2,2,14 } },
    { '(', { 2,4,8,8,8,4,2 } }, { ')', { 8,4,2,2,2,4,8 } },
    { '<', { 2,4,8,16,8,4,2 } }, { '>', { 8,4,2,1,2,4,8 } },
    { '|', { 4,4,4,4,4,4,4 } }, { '*', { 0,21,14,31,14,21,0 } }
};

static const Uint8 *find_glyph(char character)
{
    size_t index;
    static const Uint8 blank[7] = { 0,0,0,0,0,0,0 };

    character = (char)toupper((unsigned char)character);
    for (index = 0; index < ARRAY_COUNT(font_5x7); ++index) {
        if (font_5x7[index].character == character) {
            return font_5x7[index].rows;
        }
    }
    return blank;
}

static void clear_canvas(SDL_Surface *surface)
{
    int y;

    if (!surface || !surface->pixels || surface->pitch < SCREEN_WIDTH) {
        return;
    }
    for (y = 0; y < SCREEN_HEIGHT; ++y) {
        memset((Uint8 *)surface->pixels + (y * surface->pitch), COLOR_BLACK,
               SCREEN_WIDTH);
    }
}

static void put_pixel(SDL_Surface *surface, int x, int y, Uint8 color)
{
    Uint8 *row;

    if (!surface || !surface->pixels || x < 0 || x >= SCREEN_WIDTH ||
        y < 0 || y >= SCREEN_HEIGHT) {
        return;
    }
    row = (Uint8 *)surface->pixels + (y * surface->pitch);
    row[x] = color;
}

static void fill_rect(SDL_Surface *surface, int x, int y, int width, int height,
                      Uint8 color)
{
    int row;
    int start_x = x;
    int start_y = y;
    int end_x = x + width;
    int end_y = y + height;

    if (!surface || !surface->pixels || width <= 0 || height <= 0) {
        return;
    }
    if (start_x < 0) start_x = 0;
    if (start_y < 0) start_y = 0;
    if (end_x > SCREEN_WIDTH) end_x = SCREEN_WIDTH;
    if (end_y > SCREEN_HEIGHT) end_y = SCREEN_HEIGHT;
    if (start_x >= end_x || start_y >= end_y) {
        return;
    }
    for (row = start_y; row < end_y; ++row) {
        memset((Uint8 *)surface->pixels + (row * surface->pitch) + start_x,
               color, (size_t)(end_x - start_x));
    }
}

static void draw_line(SDL_Surface *surface, int x0, int y0, int x1, int y1, Uint8 color)
{
    const int dx = abs(x1 - x0);
    const int sx = (x0 < x1) ? 1 : -1;
    const int dy = -abs(y1 - y0);
    const int sy = (y0 < y1) ? 1 : -1;
    int error = dx + dy;

    for (;;) {
        put_pixel(surface, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        {
            const int twice_error = 2 * error;
            if (twice_error >= dy) {
                error += dy;
                x0 += sx;
            }
            if (twice_error <= dx) {
                error += dx;
                y0 += sy;
            }
        }
    }
}

static void draw_rect(SDL_Surface *surface, int x, int y, int width, int height,
                      Uint8 color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    draw_line(surface, x, y, x + width - 1, y, color);
    draw_line(surface, x, y + height - 1, x + width - 1, y + height - 1, color);
    draw_line(surface, x, y, x, y + height - 1, color);
    draw_line(surface, x + width - 1, y, x + width - 1, y + height - 1, color);
}

static void draw_text(SDL_Surface *surface, int x, int y, const char *text, Uint8 color)
{
    const int origin_x = x;
    const int panel_left = (x < PANEL_WIDTH) ? 0 : PANEL_WIDTH;
    const int panel_right = panel_left + PANEL_WIDTH;

    if (!text) {
        return;
    }
    while (*text) {
        const Uint8 *rows;
        int row;
        int column;

        if (*text == '\n') {
            x = origin_x;
            y += 9;
            ++text;
            continue;
        }
        rows = find_glyph(*text);
        for (row = 0; row < 7; ++row) {
            for (column = 0; column < 5; ++column) {
                const int pixel_x = x + column;
                if ((rows[row] & (Uint8)(1U << (4 - column))) &&
                    pixel_x >= panel_left && pixel_x < panel_right) {
                    put_pixel(surface, pixel_x, y + row, color);
                }
            }
        }
        x += 6;
        ++text;
    }
}

/* -------------------------------------------------------------------------- */
/* Asset loading and indexed sprite rendering; source alpha is black-matted.   */
/* -------------------------------------------------------------------------- */
/* Load an indexed BMP and round up to keep partial final source blocks visible. */
static bool load_sprite(Sprite *sprite, const char *path, const char *description)
{
    SDL_Surface *surface = SDL_LoadBMP(path);

    if (!surface) {
        LOG("Sprite missing (%s): %s", description, SDL_GetError());
        return false;
    }
    if (surface->format != SDL_PIXELFORMAT_INDEX8 || !surface->pixels) {
        LOG("Sprite must be an 8-bit indexed BMP (%s): format=%s",
            description, SDL_GetPixelFormatName(surface->format));
        SDL_DestroySurface(surface);
        return false;
    }
    sprite->surface = surface;
    sprite->width = (surface->w + SPRITE_SCALE_DIVISOR - 1) / SPRITE_SCALE_DIVISOR;
    sprite->height = (surface->h + SPRITE_SCALE_DIVISOR - 1) / SPRITE_SCALE_DIVISOR;
    LOG("Sprite loaded: %s source=%dx%d display=%dx%d",
        description, surface->w, surface->h, sprite->width, sprite->height);
    return true;
}

static int sprite_width_or(const Sprite *sprite, int fallback_width)
{
    return sprite->surface ? sprite->width : fallback_width;
}

static int sprite_height_or(const Sprite *sprite, int fallback_height)
{
    return sprite->surface ? sprite->height : fallback_height;
}

/*
 * Nearest-neighbor downscale: each destination pixel samples source
 * (x * SPRITE_SCALE_DIVISOR, y * SPRITE_SCALE_DIVISOR). Clip to the shooter
 * panel and skip palette index 0, which is the converted transparent matte.
 */
static void draw_scaled_sprite(SDL_Surface *destination, const Sprite *sprite,
                               int x, int y, const SDL_Rect *clip)
{
    int draw_y;

    if (!sprite->surface || !sprite->surface->pixels) {
        return;
    }
    for (draw_y = 0; draw_y < sprite->height; ++draw_y) {
        const int destination_y = y + draw_y;
        const int source_y = draw_y * SPRITE_SCALE_DIVISOR;
        const Uint8 *source_row;
        int draw_x;

        if (clip && (destination_y < clip->y || destination_y >= clip->y + clip->h)) {
            continue;
        }
        if (source_y >= sprite->surface->h) {
            continue;
        }
        source_row = (const Uint8 *)sprite->surface->pixels +
                     (source_y * sprite->surface->pitch);
        for (draw_x = 0; draw_x < sprite->width; ++draw_x) {
            const int destination_x = x + draw_x;
            const int source_x = draw_x * SPRITE_SCALE_DIVISOR;
            Uint8 color;

            if (source_x >= sprite->surface->w) {
                continue;
            }
            color = source_row[source_x];
            if (color == TRANSPARENT_INDEX) {
                continue;
            }
            if (clip && (destination_x < clip->x || destination_x >= clip->x + clip->w)) {
                continue;
            }
            put_pixel(destination, destination_x, destination_y, color);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* WAV loading and software mixing. Clips share U8 mono 11025-Hz PCM format.   */
/* -------------------------------------------------------------------------- */
static bool load_pcm_clip(PcmClip *clip, const char *path, const char *label)
{
    SDL_AudioSpec spec;
    Uint8 *samples = NULL;
    Uint32 length = 0;

    if (!SDL_LoadWAV(path, &spec, &samples, &length)) {
        LOG("WAV unavailable (%s): %s (%s)", label, path, SDL_GetError());
        return false;
    }
    if (spec.format != SDL_AUDIO_U8 || spec.channels != 1 ||
        spec.freq != AUDIO_SAMPLE_RATE || length == 0) {
        LOG("WAV format mismatch (%s): expected U8 mono %d Hz; got format=%u channels=%d rate=%d",
            label, AUDIO_SAMPLE_RATE, (unsigned int)spec.format, spec.channels, spec.freq);
        SDL_free(samples);
        return false;
    }
    clip->samples = samples;
    clip->length = length;
    clip->position = 0;
    clip->active = false;
    LOG("WAV loaded: %s bytes=%lu", label, (unsigned long)length);
    return true;
}

static void start_one_shot(PcmClip *clip)
{
    if (clip->samples && clip->length > 0) {
        clip->position = 0;
        clip->active = true;
    }
}

static int mix_clip_sample(PcmClip *clip, bool loop, unsigned int volume)
{
    int centered_sample;

    if (!clip->active || !clip->samples || clip->length == 0) {
        return 0;
    }
    if (clip->position >= clip->length) {
        if (loop) {
            clip->position = 0;
        } else {
            clip->active = false;
            return 0;
        }
    }
    centered_sample = (int)clip->samples[clip->position] - 128;
    ++clip->position;
    return (centered_sample * (int)volume) / 255;
}

static void mix_audio_block(ShowcaseApp *app, Uint8 output[AUDIO_CHUNK_FRAMES])
{
    AudioState *audio = &app->audio;
    unsigned int frame;
    const unsigned int master = (unsigned int)app->settings.master_volume;
    const unsigned int music_volume = (master * 60U) / 100U;
    const unsigned int engine_volume = master / 2U; /* Exactly 50% of master. */

    if (app->settings.music_enabled && audio->music.samples) {
        audio->music.active = true;
    }
    if (app->game.engine_active && audio->engine.samples) {
        audio->engine.active = true;
    } else {
        audio->engine.active = false;
        audio->engine.position = 0;
    }

    for (frame = 0; frame < AUDIO_CHUNK_FRAMES; ++frame) {
        int mixed = 0;
        if (app->settings.music_enabled) {
            mixed += mix_clip_sample(&audio->music, true, music_volume);
        }
        if (app->game.engine_active) {
            mixed += mix_clip_sample(&audio->engine, true, engine_volume);
        }
        mixed += mix_clip_sample(&audio->laser, false, master);
        mixed += mix_clip_sample(&audio->hit, false, master);
        if (mixed > 127) mixed = 127;
        if (mixed < -128) mixed = -128;
        output[frame] = (Uint8)(mixed + 128);
    }
}

static void feed_audio_queue(ShowcaseApp *app)
{
    Uint8 samples[AUDIO_CHUNK_FRAMES];
    int queued_bytes;

    if (!app->audio.ready || !app->audio.stream) {
        return;
    }
    queued_bytes = SDL_GetAudioStreamQueued(app->audio.stream);
    if (queued_bytes < 0) {
        LOG("SDL_GetAudioStreamQueued failed: %s", SDL_GetError());
        app->audio.ready = false;
        return;
    }
    while (queued_bytes < AUDIO_QUEUE_TARGET_BYTES &&
           queued_bytes < AUDIO_QUEUE_LIMIT_BYTES) {
        mix_audio_block(app, samples);
        if (!SDL_PutAudioStreamData(app->audio.stream, samples,
                                    (int)sizeof(samples))) {
            LOG("SDL_PutAudioStreamData failed: %s", SDL_GetError());
            app->audio.ready = false;
            return;
        }
        queued_bytes += (int)sizeof(samples);
    }
}

static void initialize_audio(ShowcaseApp *app)
{
    AudioState *audio = &app->audio;
    const char *environment_blaster = getenv("BLASTER");
    const char *device_name;
    const char *driver_name;

    copy_upper_text(audio->blaster_text, sizeof(audio->blaster_text),
                    environment_blaster ? environment_blaster : "NOT SET");
    audio->music_available = load_pcm_clip(&audio->music, MUSIC_WAV, "music");
    audio->laser_available = load_pcm_clip(&audio->laser, LASER_WAV, "laser");
    audio->hit_available = load_pcm_clip(&audio->hit, HIT_WAV, "impact");
    audio->engine_available = load_pcm_clip(&audio->engine, ENGINE_WAV, "engine");
    audio->asset_warning = !audio->music_available || !audio->laser_available ||
                           !audio->hit_available || !audio->engine_available;

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        LOG("SDL audio subsystem unavailable: %s", SDL_GetError());
        copy_upper_text(audio->driver_name, sizeof(audio->driver_name), "NO AUDIO");
        return;
    }

    audio->source_spec.format = SDL_AUDIO_U8;
    audio->source_spec.channels = 1;
    audio->source_spec.freq = AUDIO_SAMPLE_RATE;
    audio->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                               &audio->source_spec, NULL, NULL);
    if (!audio->stream) {
        LOG("Could not open the audio stream: %s", SDL_GetError());
        copy_upper_text(audio->driver_name, sizeof(audio->driver_name), "OPEN FAILED");
        return;
    }

    audio->device = SDL_GetAudioStreamDevice(audio->stream);
    device_name = SDL_GetAudioDeviceName(audio->device);
    driver_name = SDL_GetCurrentAudioDriver();
    copy_upper_text(audio->device_name, sizeof(audio->device_name),
                    device_name ? device_name : "DEFAULT");
    copy_upper_text(audio->driver_name, sizeof(audio->driver_name),
                    driver_name ? driver_name : "UNKNOWN");
    audio->ready = true;
    feed_audio_queue(app);
    if (!audio->ready) {
        return;
    }
    if (!SDL_ResumeAudioStreamDevice(audio->stream)) {
        LOG("Could not start audio playback: %s", SDL_GetError());
        audio->ready = false;
        return;
    }
    LOG("Audio ready: driver=%s device=%s BLASTER=%s source=U8/mono/%dHz",
        audio->driver_name, audio->device_name, audio->blaster_text,
        AUDIO_SAMPLE_RATE);
}

/* -------------------------------------------------------------------------- */
/* Joystick support is optional: first device, axes/hat for eight-way movement.*/
/* -------------------------------------------------------------------------- */
static void initialize_joystick(ShowcaseApp *app)
{
    SDL_JoystickID *joystick_ids;
    int joystick_count = 0;

    if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK)) {
        LOG("Joystick subsystem unavailable: %s", SDL_GetError());
        return;
    }
    joystick_ids = SDL_GetJoysticks(&joystick_count);
    if (!joystick_ids) {
        LOG("No joystick list available: %s", SDL_GetError());
        return;
    }
    if (joystick_count > 0) {
        app->joystick_id = joystick_ids[0];
        app->joystick = SDL_OpenJoystick(app->joystick_id);
        if (app->joystick) {
            app->input.joystick_present = true;
            LOG("Opened joystick instance %lu axes=%d hats=%d buttons=%d",
                (unsigned long)app->joystick_id,
                SDL_GetNumJoystickAxes(app->joystick),
                SDL_GetNumJoystickHats(app->joystick),
                SDL_GetNumJoystickButtons(app->joystick));
        } else {
            LOG("Could not open joystick: %s", SDL_GetError());
        }
    } else {
        LOG("No joystick detected; keyboard input remains available");
    }
    SDL_free(joystick_ids);
}

static void poll_joystick(ShowcaseApp *app)
{
    int axes;
    int hats;
    int buttons;

    app->input.joystick_left = false;
    app->input.joystick_right = false;
    app->input.joystick_up = false;
    app->input.joystick_down = false;
    memset(app->input.joystick_buttons, 0, sizeof(app->input.joystick_buttons));
    app->input.joystick_axis_x = 0;
    app->input.joystick_axis_y = 0;
    app->input.joystick_hat = SDL_HAT_CENTERED;
    if (!app->joystick) {
        app->input.joystick_present = false;
        return;
    }

    axes = SDL_GetNumJoystickAxes(app->joystick);
    if (axes > 0) {
        app->input.joystick_axis_x = SDL_GetJoystickAxis(app->joystick, 0);
        app->input.joystick_left = app->input.joystick_axis_x < -10000;
        app->input.joystick_right = app->input.joystick_axis_x > 10000;
    }
    if (axes > 1) {
        app->input.joystick_axis_y = SDL_GetJoystickAxis(app->joystick, 1);
        app->input.joystick_up = app->input.joystick_axis_y < -10000;
        app->input.joystick_down = app->input.joystick_axis_y > 10000;
    }

    hats = SDL_GetNumJoystickHats(app->joystick);
    if (hats > 0) {
        app->input.joystick_hat = SDL_GetJoystickHat(app->joystick, 0);
        app->input.joystick_up = app->input.joystick_up ||
                                  ((app->input.joystick_hat & SDL_HAT_UP) != 0);
        app->input.joystick_down = app->input.joystick_down ||
                                    ((app->input.joystick_hat & SDL_HAT_DOWN) != 0);
        app->input.joystick_left = app->input.joystick_left ||
                                    ((app->input.joystick_hat & SDL_HAT_LEFT) != 0);
        app->input.joystick_right = app->input.joystick_right ||
                                     ((app->input.joystick_hat & SDL_HAT_RIGHT) != 0);
    }

    buttons = SDL_GetNumJoystickButtons(app->joystick);
    if (buttons > 4) buttons = 4;
    if (buttons > 0) {
        int button;
        for (button = 0; button < buttons; ++button) {
            app->input.joystick_buttons[button] =
                SDL_GetJoystickButton(app->joystick, button);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Shooter initialization and updates: replace this module for custom gameplay.*/
/* -------------------------------------------------------------------------- */
static void initialize_game(ShowcaseApp *app)
{
    int index;
    const int ship_width = sprite_width_or(&app->ship, 28);
    const int ship_height = sprite_height_or(&app->ship, 20);
    const int ufo_width = sprite_width_or(&app->ufo, 24);

    srand((unsigned int)SDL_GetTicks());
    app->game.player_x_fp = ((PANEL_WIDTH - ship_width) / 2) * FIXED_POINT_ONE;
    app->game.player_y_fp = (PANEL_HEIGHT - ship_height - 3) * FIXED_POINT_ONE;
    app->game.enemy_x_fp = ((PANEL_WIDTH - ufo_width) / 2) * FIXED_POINT_ONE;
    app->game.enemy_y = 15;
    app->game.enemy_direction = 1;
    app->game.score = 0;
    app->game.last_fire_ticks = 0;
    app->game.flash_until_ticks = 0;
    app->game.engine_active = false;

    for (index = 0; index < STAR_COUNT; ++index) {
        Star *star = &app->game.stars[index];
        star->x = 2 + rand() % (PANEL_WIDTH - 4);
        star->y = 12 + rand() % (PANEL_HEIGHT - 14);
        star->speed_pixels_per_second = (Uint8)(8 + rand() % 24);
        star->color = (rand() % 3 == 0) ? COLOR_CYAN :
                      ((rand() % 2 == 0) ? COLOR_GRAY : COLOR_WHITE);
        star->fractional_motion = 0;
        star->twinkle = (Uint8)(rand() % 8);
    }
    for (index = 0; index < LASER_COUNT; ++index) {
        app->game.lasers[index].active = false;
        app->game.lasers[index].x = 0;
        app->game.lasers[index].y_fp = 0;
    }
}

static bool rectangles_overlap(int ax, int ay, int aw, int ah,
                               int bx, int by, int bw, int bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void fire_laser(ShowcaseApp *app, Uint64 now)
{
    const int ship_width = sprite_width_or(&app->ship, 28);
    const int laser_width = sprite_width_or(&app->laser_sprite, 4);
    const int laser_height = sprite_height_or(&app->laser_sprite, 12);
    const int player_x = (int)(app->game.player_x_fp / FIXED_POINT_ONE);
    const int player_y = (int)(app->game.player_y_fp / FIXED_POINT_ONE);
    int index;

    if (app->game.last_fire_ticks != 0 &&
        now - app->game.last_fire_ticks < LASER_COOLDOWN_MS) {
        return;
    }
    for (index = 0; index < LASER_COUNT; ++index) {
        Laser *laser = &app->game.lasers[index];
        if (!laser->active) {
            laser->active = true;
            laser->x = player_x + (ship_width - laser_width) / 2;
            laser->y_fp = (player_y - laser_height) * FIXED_POINT_ONE;
            app->game.last_fire_ticks = now;
            start_one_shot(&app->audio.laser);
            LOG("Laser fired at x=%d y=%d", laser->x,
                (int)(laser->y_fp / FIXED_POINT_ONE));
            return;
        }
    }
}

static void update_stars(GameState *game, Uint32 delta_ms)
{
    int index;

    for (index = 0; index < STAR_COUNT; ++index) {
        Star *star = &game->stars[index];
        star->fractional_motion += (Uint32)star->speed_pixels_per_second * delta_ms;
        while (star->fractional_motion >= 1000U) {
            star->fractional_motion -= 1000U;
            ++star->y;
        }
        if (star->y >= PANEL_HEIGHT - 2) {
            star->y = 12 + rand() % (PANEL_HEIGHT - 14);
            star->x = 2 + rand() % (PANEL_WIDTH - 4);
            star->speed_pixels_per_second = (Uint8)(8 + rand() % 24);
        }
        star->twinkle = (Uint8)((star->twinkle + 1U) & 7U);
    }
}

static void update_shooter(ShowcaseApp *app, Uint32 delta_ms, Uint64 now)
{
    const bool move_left = app->input.key_left || app->input.joystick_left;
    const bool move_right = app->input.key_right || app->input.joystick_right;
    const bool move_up = app->input.key_up || app->input.joystick_up;
    const bool move_down = app->input.key_down || app->input.joystick_down;
    const bool fire = app->input.key_fire || app->input.joystick_buttons[0];
    const int ship_width = sprite_width_or(&app->ship, 28);
    const int ship_height = sprite_height_or(&app->ship, 20);
    const int ufo_width = sprite_width_or(&app->ufo, 24);
    const int ufo_height = sprite_height_or(&app->ufo, 24);
    const int laser_width = sprite_width_or(&app->laser_sprite, 4);
    const int laser_height = sprite_height_or(&app->laser_sprite, 12);
    const Sint32 horizontal_step = (Sint32)((130 * delta_ms * FIXED_POINT_ONE) / 1000U);
    const Sint32 vertical_step = (Sint32)((55 * delta_ms * FIXED_POINT_ONE) / 1000U);
    const Sint32 enemy_step = (Sint32)((42 * delta_ms * FIXED_POINT_ONE) / 1000U);
    int minimum_player_y;
    int maximum_player_y;
    Sint32 minimum_enemy_x;
    Sint32 maximum_enemy_x;
    int index;

    update_stars(&app->game, delta_ms);
    app->game.engine_active = move_left || move_right || move_up || move_down;

    if (move_left && !move_right) app->game.player_x_fp -= horizontal_step;
    if (move_right && !move_left) app->game.player_x_fp += horizontal_step;
    if (move_up && !move_down) app->game.player_y_fp -= vertical_step;
    if (move_down && !move_up) app->game.player_y_fp += vertical_step;

    if (app->game.player_x_fp < 2 * FIXED_POINT_ONE) {
        app->game.player_x_fp = 2 * FIXED_POINT_ONE;
    }
    if (app->game.player_x_fp >
        (PANEL_WIDTH - ship_width - 2) * FIXED_POINT_ONE) {
        app->game.player_x_fp = (PANEL_WIDTH - ship_width - 2) * FIXED_POINT_ONE;
    }
    minimum_player_y = PANEL_HEIGHT - ship_height - 30;
    maximum_player_y = PANEL_HEIGHT - ship_height - 2;
    if (app->game.player_y_fp < minimum_player_y * FIXED_POINT_ONE) {
        app->game.player_y_fp = minimum_player_y * FIXED_POINT_ONE;
    }
    if (app->game.player_y_fp > maximum_player_y * FIXED_POINT_ONE) {
        app->game.player_y_fp = maximum_player_y * FIXED_POINT_ONE;
    }

    minimum_enemy_x = 2 * FIXED_POINT_ONE;
    maximum_enemy_x = (PANEL_WIDTH - ufo_width - 2) * FIXED_POINT_ONE;
    app->game.enemy_x_fp += app->game.enemy_direction * enemy_step;
    if (app->game.enemy_x_fp < minimum_enemy_x) {
        app->game.enemy_x_fp = minimum_enemy_x;
        app->game.enemy_direction = 1;
    } else if (app->game.enemy_x_fp > maximum_enemy_x) {
        app->game.enemy_x_fp = maximum_enemy_x;
        app->game.enemy_direction = -1;
    }
    if (app->game.enemy_y + ufo_height >= PANEL_HEIGHT - 2) {
        app->game.enemy_y = PANEL_HEIGHT - ufo_height - 3;
    }

    if (fire) {
        fire_laser(app, now);
    }
    for (index = 0; index < LASER_COUNT; ++index) {
        Laser *laser = &app->game.lasers[index];
        if (laser->active) {
            laser->y_fp -= (Sint32)((185 * delta_ms * FIXED_POINT_ONE) / 1000U);
            if (laser->y_fp / FIXED_POINT_ONE + laser_height < 0) {
                laser->active = false;
            } else if (rectangles_overlap(
                           laser->x, (int)(laser->y_fp / FIXED_POINT_ONE),
                           laser_width, laser_height,
                           (int)(app->game.enemy_x_fp / FIXED_POINT_ONE),
                           app->game.enemy_y, ufo_width, ufo_height)) {
                laser->active = false;
                ++app->game.score;
                app->game.flash_until_ticks = now + 160U;
                start_one_shot(&app->audio.hit);
                set_status(app, "UFO HIT - SCORE SAVED", 1800);
                if (app->game.score > app->record.best_score) {
                    app->record.best_score = app->game.score;
                    copy_callsign(app->record.callsign, app->settings.callsign);
                    if (save_score_record(app)) {
                        set_status(app, "NEW BEST SCORE SAVED", 2500);
                    } else {
                        set_status(app, "BEST SCORE - SAVE FAILED", 2500);
                    }
                }
                app->game.enemy_x_fp =
                    (2 + rand() % (PANEL_WIDTH - ufo_width - 3)) * FIXED_POINT_ONE;
                app->game.enemy_direction = (rand() & 1) ? 1 : -1;
                break;
            }
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Keyboard/text event handling. SDL text events are used when present; direct */
/* ASCII key fallback keeps short DOS call signs editable without an IME.      */
/* -------------------------------------------------------------------------- */
static void append_callsign_character(ShowcaseApp *app, unsigned char character)
{
    size_t length = strlen(app->settings.callsign);

    if (!is_callsign_character(character) || length + 1 >= CALLSIGN_CAPACITY) {
        return;
    }
    app->settings.callsign[length] = (char)toupper(character);
    app->settings.callsign[length + 1] = '\0';
}

static void remove_callsign_character(ShowcaseApp *app)
{
    size_t length = strlen(app->settings.callsign);
    if (length > 0) {
        app->settings.callsign[length - 1] = '\0';
    }
}

static void begin_name_editing(ShowcaseApp *app)
{
    app->input.editing_name = true;
    app->input.pending_fallback_valid = false;
    if (app->window) {
        (void)SDL_StartTextInput(app->window);
    }
    set_status(app, "TYPE NAME; ENTER SAVES", 3000);
}

static void finish_name_editing(ShowcaseApp *app, bool save)
{
    app->input.editing_name = false;
    app->input.pending_fallback_valid = false;
    if (app->window) {
        (void)SDL_StopTextInput(app->window);
    }
    if (app->settings.callsign[0] == '\0') {
        copy_callsign(app->settings.callsign, "CADET");
    }
    if (save) {
        set_status(app, save_settings(app) ? "CONFIG SAVED" : "CONFIG SAVE FAILED", 2500);
    }
}

static void handle_text_input(ShowcaseApp *app, const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;
    bool skip_duplicate = app->input.pending_fallback_valid;

    if (!app->input.editing_name || !text) {
        return;
    }
    while (*cursor) {
        const unsigned char character = *cursor++;
        if (skip_duplicate &&
            (unsigned char)toupper(character) ==
                (unsigned char)app->input.pending_fallback_char &&
            SDL_GetTicks() - app->input.pending_fallback_ticks < 250U) {
            skip_duplicate = false;
            app->input.pending_fallback_valid = false;
            continue;
        }
        skip_duplicate = false;
        append_callsign_character(app, character);
    }
    app->input.pending_fallback_valid = false;
}

static void adjust_master_volume(ShowcaseApp *app, int direction)
{
    int volume = (int)app->settings.master_volume + direction * 16;

    if (volume < 0) volume = 0;
    if (volume > 255) volume = 255;
    app->settings.master_volume = (Uint8)volume;
    set_status(app, "MASTER VOLUME CHANGED", 1500);
}

static void handle_key_down(ShowcaseApp *app, const SDL_KeyboardEvent *key)
{
    const SDL_Keycode code = key->key;

    if (app->input.editing_name) {
        if (code == SDLK_ESCAPE) {
            finish_name_editing(app, false);
            app->running = false;
        } else if (code == SDLK_RETURN) {
            finish_name_editing(app, true);
        } else if (code == SDLK_BACKSPACE) {
            remove_callsign_character(app);
        } else if (!key->repeat && code <= 0x7fU) {
            const unsigned char character = (unsigned char)code;
            if (is_callsign_character(character)) {
                append_callsign_character(app, character);
                app->input.pending_fallback_char =
                    (char)toupper(character);
                app->input.pending_fallback_ticks = SDL_GetTicks();
                app->input.pending_fallback_valid = true;
            }
        }
        return;
    }

    if (code == SDLK_ESCAPE) {
        app->running = false;
        return;
    }
    if (code == SDLK_LEFT || code == SDLK_A) app->input.key_left = true;
    if (code == SDLK_RIGHT || code == SDLK_D) app->input.key_right = true;
    if (code == SDLK_UP || code == SDLK_W) app->input.key_up = true;
    if (code == SDLK_DOWN || code == SDLK_S) app->input.key_down = true;
    if (code == SDLK_SPACE) app->input.key_fire = true;

    if (key->repeat) {
        return;
    }
    if (code == SDLK_T) {
        begin_name_editing(app);
    } else if (code == SDLK_RETURN) {
        set_status(app, save_settings(app) ? "CONFIG SAVED" : "CONFIG SAVE FAILED", 2500);
    } else if (code == SDLK_F2) {
        set_status(app, save_settings(app) ? "CONFIG SAVED" : "CONFIG SAVE FAILED", 2500);
    } else if (code == SDLK_F3) {
        set_status(app, load_settings(app) ? "CONFIG LOADED" : "CONFIG LOAD FAILED", 2500);
    } else if (code == SDLK_F1) {
        app->palette.target_brightness =
            (app->palette.target_brightness == 0) ? 16 : 0;
        app->palette.last_step_ticks = SDL_GetTicks();
    } else if (code == SDLK_M) {
        app->settings.music_enabled = !app->settings.music_enabled;
        set_status(app, app->settings.music_enabled ? "MUSIC ON" : "MUSIC OFF", 1500);
    } else if (code == SDLK_MINUS || code == SDLK_KP_MINUS) {
        adjust_master_volume(app, -1);
    } else if (code == SDLK_PLUS || code == SDLK_EQUALS || code == SDLK_KP_PLUS) {
        adjust_master_volume(app, 1);
    }
}

static void handle_key_up(ShowcaseApp *app, const SDL_KeyboardEvent *key)
{
    const SDL_Keycode code = key->key;

    if (code == SDLK_LEFT || code == SDLK_A) app->input.key_left = false;
    if (code == SDLK_RIGHT || code == SDLK_D) app->input.key_right = false;
    if (code == SDLK_UP || code == SDLK_W) app->input.key_up = false;
    if (code == SDLK_DOWN || code == SDLK_S) app->input.key_down = false;
    if (code == SDLK_SPACE) app->input.key_fire = false;
}

static void handle_event(ShowcaseApp *app, const SDL_Event *event)
{
    if (event->type == SDL_EVENT_QUIT) {
        app->running = false;
    } else if (event->type == SDL_EVENT_KEY_DOWN) {
        handle_key_down(app, &event->key);
    } else if (event->type == SDL_EVENT_KEY_UP) {
        handle_key_up(app, &event->key);
    } else if (event->type == SDL_EVENT_TEXT_INPUT) {
        handle_text_input(app, event->text.text);
    }
}

/* -------------------------------------------------------------------------- */
/* Screen panels: general status, audio diagnostics, input/joystick, shooter.  */
/* -------------------------------------------------------------------------- */
static void draw_general_panel(ShowcaseApp *app, Uint64 now)
{
    char line[64];
    const char *fade_state = (app->palette.target_brightness == 0 &&
                              app->palette.brightness == 0) ? "OFF" :
                             (app->palette.brightness < 16) ? "RAMP" : "ON";

    draw_text(app->canvas, 4, 3, "SDL3 SYSTEMS SHOWCASE", COLOR_CYAN);
    draw_text(app->canvas, 4, 12, "MODE 13H / 320X200", COLOR_WHITE);
    (void)snprintf(line, sizeof(line), "PALETTE FADE: %s", fade_state);
    draw_text(app->canvas, 4, 21, line, COLOR_YELLOW);
    (void)snprintf(line, sizeof(line), "SCORE: %d", app->game.score);
    draw_text(app->canvas, 4, 30, line, COLOR_GREEN);
    (void)snprintf(line, sizeof(line), "BEST: %d BY %.8s",
                   app->record.best_score, app->record.callsign);
    draw_text(app->canvas, 4, 39, line, COLOR_ORANGE);
    (void)snprintf(line, sizeof(line), "NAME: %.8s%s",
                   app->settings.callsign,
                   (app->input.editing_name && ((now / 300U) & 1U)) ? "_" : "");
    draw_text(app->canvas, 4, 48, line, COLOR_WHITE);
    draw_text(app->canvas, 4, 57, "F1 FADE  M MUSIC", COLOR_CYAN);
    draw_text(app->canvas, 4, 66, "F2 SAVE  F3 LOAD", COLOR_YELLOW);
    draw_text(app->canvas, 4, 75, "T TYPE NAME  ENTER SAVE", COLOR_GREEN);
    if (app->status_text[0] && now < app->status_until_ticks) {
        draw_text(app->canvas, 4, 86, app->status_text, COLOR_PURPLE);
    } else {
        draw_text(app->canvas, 4, 86, "ESC QUIT", COLOR_GRAY);
    }
}

static void draw_audio_panel(ShowcaseApp *app)
{
    char line[64];
    unsigned int percent = ((unsigned int)app->settings.master_volume * 100U) / 255U;
    const char *music_state = app->settings.music_enabled ? "ON" : "OFF";

    draw_text(app->canvas, PANEL_WIDTH + 4, 3, "SOUND / BLASTER", COLOR_CYAN);
    (void)snprintf(line, sizeof(line), "DRIVER: %.16s",
                   app->audio.driver_name[0] ? app->audio.driver_name : "N/A");
    draw_text(app->canvas, PANEL_WIDTH + 4, 12, line, COLOR_WHITE);
    (void)snprintf(line, sizeof(line), "DEVICE: %.16s",
                   app->audio.device_name[0] ? app->audio.device_name : "N/A");
    draw_text(app->canvas, PANEL_WIDTH + 4, 21, line, COLOR_GRAY);
    (void)snprintf(line, sizeof(line), "MIX: U8 MONO %d HZ", AUDIO_SAMPLE_RATE);
    draw_text(app->canvas, PANEL_WIDTH + 4, 30, line,
              app->audio.ready ? COLOR_GREEN : COLOR_RED);
    (void)snprintf(line, sizeof(line), "BLASTER: %.18s",
                   app->audio.blaster_text[0] ? app->audio.blaster_text : "NOT SET");
    draw_text(app->canvas, PANEL_WIDTH + 4, 39, line, COLOR_YELLOW);
    (void)snprintf(line, sizeof(line), "MUSIC: %s", music_state);
    draw_text(app->canvas, PANEL_WIDTH + 4, 48, line,
              app->audio.music_available ? COLOR_CYAN : COLOR_RED);
    draw_text(app->canvas, PANEL_WIDTH + 4, 57,
              app->audio.asset_warning ? "ASSET WARNING: CHECK WAVS" : "WAV ASSETS: READY",
              app->audio.asset_warning ? COLOR_RED : COLOR_GREEN);
    draw_text(app->canvas, PANEL_WIDTH + 4, 66, "SPACE LASER / HIT ON UFO", COLOR_ORANGE);
    draw_text(app->canvas, PANEL_WIDTH + 4, 75, "ENGINE: 50% OF MASTER", COLOR_PURPLE);
    (void)snprintf(line, sizeof(line), "MASTER: %u%%  - / +", percent);
    draw_text(app->canvas, PANEL_WIDTH + 4, 84, line, COLOR_WHITE);
}

static void draw_pad_cell(SDL_Surface *surface, int x, int y, const char *label,
                          bool active)
{
    const Uint8 background = active ? COLOR_GREEN : COLOR_DIM;
    const Uint8 foreground = active ? COLOR_BLACK : COLOR_WHITE;

    fill_rect(surface, x, y, 21, 10, background);
    draw_rect(surface, x, y, 21, 10, COLOR_BLUE);
    draw_text(surface, x + 4, y + 1, label, foreground);
}

static void draw_direction_pad(ShowcaseApp *app, int x, int y)
{
    const bool left = app->input.key_left || app->input.joystick_left;
    const bool right = app->input.key_right || app->input.joystick_right;
    const bool up = app->input.key_up || app->input.joystick_up;
    const bool down = app->input.key_down || app->input.joystick_down;
    const bool idle = !left && !right && !up && !down;

    draw_pad_cell(app->canvas, x, y, "NW", left && up);
    draw_pad_cell(app->canvas, x + 23, y, "N", up && !left && !right);
    draw_pad_cell(app->canvas, x + 46, y, "NE", right && up);
    draw_pad_cell(app->canvas, x, y + 12, "W", left && !up && !down);
    draw_pad_cell(app->canvas, x + 23, y + 12, "--", idle);
    draw_pad_cell(app->canvas, x + 46, y + 12, "E", right && !up && !down);
    draw_pad_cell(app->canvas, x, y + 24, "SW", left && down);
    draw_pad_cell(app->canvas, x + 23, y + 24, "S", down && !left && !right);
    draw_pad_cell(app->canvas, x + 46, y + 24, "SE", right && down);
}

static void draw_input_panel(ShowcaseApp *app)
{
    char line[64];
    int button;

    draw_text(app->canvas, 4, PANEL_HEIGHT + 3, "INPUT / JOYSTICK", COLOR_CYAN);
    draw_text(app->canvas, 4, PANEL_HEIGHT + 12,
              app->input.joystick_present ? "JOY: CONNECTED" : "JOY: NOT FOUND",
              app->input.joystick_present ? COLOR_GREEN : COLOR_YELLOW);
    draw_direction_pad(app, 4, PANEL_HEIGHT + 23);
    (void)snprintf(line, sizeof(line), "X:%6d", (int)app->input.joystick_axis_x);
    draw_text(app->canvas, 82, PANEL_HEIGHT + 23, line, COLOR_WHITE);
    (void)snprintf(line, sizeof(line), "Y:%6d", (int)app->input.joystick_axis_y);
    draw_text(app->canvas, 82, PANEL_HEIGHT + 32, line, COLOR_WHITE);
    draw_text(app->canvas, 82, PANEL_HEIGHT + 41,
              (app->input.joystick_hat == SDL_HAT_CENTERED) ? "HAT: CENTER" : "HAT: ACTIVE",
              COLOR_GRAY);

    (void)snprintf(line, sizeof(line), "BUTTONS:");
    draw_text(app->canvas, 4, PANEL_HEIGHT + 60, line, COLOR_ORANGE);
    for (button = 0; button < 4; ++button) {
        (void)snprintf(line, sizeof(line), "%d:%c", button + 1,
                       app->input.joystick_buttons[button] ? '1' : '0');
        draw_text(app->canvas, 58 + button * 24, PANEL_HEIGHT + 60, line,
                  app->input.joystick_buttons[button] ? COLOR_GREEN : COLOR_GRAY);
    }
    (void)snprintf(line, sizeof(line), "KEY L:%c R:%c U:%c D:%c FIRE:%c",
                   app->input.key_left ? '1' : '0',
                   app->input.key_right ? '1' : '0',
                   app->input.key_up ? '1' : '0',
                   app->input.key_down ? '1' : '0',
                   app->input.key_fire ? '1' : '0');
    draw_text(app->canvas, 4, PANEL_HEIGHT + 69, line, COLOR_WHITE);
    draw_text(app->canvas, 4, PANEL_HEIGHT + 78,
              app->input.editing_name ? "TEXT INPUT: TYPE NAME" : "T TYPE NAME / ENTER SAVE",
              app->input.editing_name ? COLOR_YELLOW : COLOR_GREEN);
    draw_text(app->canvas, 4, PANEL_HEIGHT + 87, "ARROWS/WASD + SPACE", COLOR_GRAY);
}

static void draw_fallback_ship(SDL_Surface *surface, int x, int y)
{
    draw_line(surface, x + 14, y, x + 2, y + 18, COLOR_CYAN);
    draw_line(surface, x + 14, y, x + 26, y + 18, COLOR_CYAN);
    draw_line(surface, x + 2, y + 18, x + 26, y + 18, COLOR_BLUE);
    fill_rect(surface, x + 11, y + 7, 7, 7, COLOR_WHITE);
}

static void draw_fallback_ufo(SDL_Surface *surface, int x, int y)
{
    fill_rect(surface, x + 4, y + 3, 16, 5, COLOR_GREEN);
    fill_rect(surface, x + 1, y + 8, 22, 6, COLOR_CYAN);
    fill_rect(surface, x + 5, y + 14, 14, 3, COLOR_GREEN);
    fill_rect(surface, x + 3, y + 16, 3, 2, COLOR_YELLOW);
    fill_rect(surface, x + 10, y + 16, 3, 2, COLOR_YELLOW);
    fill_rect(surface, x + 17, y + 16, 3, 2, COLOR_YELLOW);
}

static void draw_fallback_laser(SDL_Surface *surface, int x, int y)
{
    fill_rect(surface, x + 1, y, 2, 10, COLOR_RED);
    put_pixel(surface, x, y + 2, COLOR_YELLOW);
    put_pixel(surface, x + 3, y + 2, COLOR_YELLOW);
}

static void draw_shooter_panel(ShowcaseApp *app, Uint64 now)
{
    const SDL_Rect clip = { SHOOTER_INNER_X, SHOOTER_INNER_Y,
                            SHOOTER_INNER_W, SHOOTER_INNER_H };
    const int player_x = SHOOTER_X + (int)(app->game.player_x_fp / FIXED_POINT_ONE);
    const int player_y = SHOOTER_Y + (int)(app->game.player_y_fp / FIXED_POINT_ONE);
    const int enemy_x = SHOOTER_X + (int)(app->game.enemy_x_fp / FIXED_POINT_ONE);
    const int enemy_y = SHOOTER_Y + app->game.enemy_y;
    int index;
    char score_text[24];

    fill_rect(app->canvas, SHOOTER_X, SHOOTER_Y, PANEL_WIDTH, PANEL_HEIGHT, COLOR_BLACK);
    for (index = 0; index < STAR_COUNT; ++index) {
        const Star *star = &app->game.stars[index];
        const Uint8 color = (star->twinkle == 0) ? COLOR_WHITE : star->color;
        put_pixel(app->canvas, SHOOTER_X + star->x, SHOOTER_Y + star->y, color);
    }

    if (app->ufo.surface) {
        draw_scaled_sprite(app->canvas, &app->ufo, enemy_x, enemy_y, &clip);
    } else {
        draw_fallback_ufo(app->canvas, enemy_x, enemy_y);
    }
    for (index = 0; index < LASER_COUNT; ++index) {
        const Laser *laser = &app->game.lasers[index];
        if (laser->active) {
            const int laser_x = SHOOTER_X + laser->x;
            const int laser_y = SHOOTER_Y + (int)(laser->y_fp / FIXED_POINT_ONE);
            if (app->laser_sprite.surface) {
                draw_scaled_sprite(app->canvas, &app->laser_sprite,
                                   laser_x, laser_y, &clip);
            } else {
                draw_fallback_laser(app->canvas, laser_x, laser_y);
            }
        }
    }
    if (app->ship.surface) {
        draw_scaled_sprite(app->canvas, &app->ship, player_x, player_y, &clip);
    } else {
        draw_fallback_ship(app->canvas, player_x, player_y);
    }

    draw_text(app->canvas, SHOOTER_X + 4, SHOOTER_Y + 3, "MINI SHOOTER", COLOR_CYAN);
    (void)snprintf(score_text, sizeof(score_text), "S:%d", app->game.score);
    draw_text(app->canvas, SHOOTER_X + 112, SHOOTER_Y + 3, score_text, COLOR_YELLOW);
    if (now < app->game.flash_until_ticks) {
        draw_rect(app->canvas, enemy_x - 2, enemy_y - 2,
                  sprite_width_or(&app->ufo, 24) + 4,
                  sprite_height_or(&app->ufo, 24) + 4, COLOR_ORANGE);
    }
}

static bool present_frame(ShowcaseApp *app)
{
    if (app->canvas != app->window_surface &&
        !SDL_BlitSurface(app->canvas, NULL, app->window_surface, NULL)) {
        LOG("SDL_BlitSurface failed: %s", SDL_GetError());
        return false;
    }
    if (!SDL_UpdateWindowSurface(app->window)) {
        LOG("SDL_UpdateWindowSurface failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

static bool draw_frame(ShowcaseApp *app, Uint64 now)
{
    clear_canvas(app->canvas);
    draw_general_panel(app, now);
    draw_audio_panel(app);
    draw_input_panel(app);
    draw_shooter_panel(app, now);
    draw_line(app->canvas, PANEL_WIDTH - 1, 0, PANEL_WIDTH - 1,
              SCREEN_HEIGHT - 1, COLOR_BLUE);
    draw_line(app->canvas, 0, PANEL_HEIGHT - 1, SCREEN_WIDTH - 1,
              PANEL_HEIGHT - 1, COLOR_BLUE);
    return present_frame(app);
}

/* -------------------------------------------------------------------------- */
/* Application loop and orderly shutdown, including the final palette fade.   */
/* -------------------------------------------------------------------------- */
static bool run_showcase(ShowcaseApp *app)
{
    SDL_Event event;
    Uint64 previous_ticks = SDL_GetTicks();

    app->running = true;
    while (app->running) {
        Uint64 now;
        Uint64 elapsed;
        Uint32 delta_ms;

        while (SDL_PollEvent(&event)) {
            handle_event(app, &event);
        }
        if (!app->running) {
            break;
        }

        poll_joystick(app);
        now = SDL_GetTicks();
        elapsed = now - previous_ticks;
        if (elapsed > 50U) {
            elapsed = 50U;
        }
        delta_ms = (Uint32)elapsed;
        previous_ticks = now;

        update_shooter(app, delta_ms, now);
        update_palette_fade(app, now);
        feed_audio_queue(app);
        if (!draw_frame(app, now)) {
            app->running = false;
            return false;
        }
        SDL_Delay(AUDIO_LOOP_DELAY_MS);
    }
    return true;
}

static void fade_out_before_exit(ShowcaseApp *app)
{
    if (!app->canvas || !app->window || app->palette.brightness == 0) {
        return;
    }
    app->palette.target_brightness = 0;
    app->palette.last_step_ticks = SDL_GetTicks();
    while (app->palette.brightness > 0) {
        const Uint64 now = SDL_GetTicks();
        update_palette_fade(app, now);
        feed_audio_queue(app);
        (void)draw_frame(app, now);
        SDL_Delay(FADE_STEP_MS);
    }
}

static void shutdown_showcase(ShowcaseApp *app)
{
    if (app->input.editing_name && app->window) {
        (void)SDL_StopTextInput(app->window);
        app->input.editing_name = false;
    }
    if (app->joystick) {
        SDL_CloseJoystick(app->joystick);
        app->joystick = NULL;
    }
    if (app->audio.stream) {
        SDL_DestroyAudioStream(app->audio.stream);
        app->audio.stream = NULL;
    }
    if (app->audio.music.samples) SDL_free(app->audio.music.samples);
    if (app->audio.laser.samples) SDL_free(app->audio.laser.samples);
    if (app->audio.hit.samples) SDL_free(app->audio.hit.samples);
    if (app->audio.engine.samples) SDL_free(app->audio.engine.samples);

    if (app->ship.surface) SDL_DestroySurface(app->ship.surface);
    if (app->ufo.surface) SDL_DestroySurface(app->ufo.surface);
    if (app->laser_sprite.surface) SDL_DestroySurface(app->laser_sprite.surface);
    if (app->canvas && app->canvas != app->window_surface) {
        SDL_DestroySurface(app->canvas);
        app->canvas = NULL;
    }
    if (app->window) {
        SDL_DestroyWindow(app->window);
        app->window = NULL;
        app->window_surface = NULL;
    }
    if (app->sdl_initialized) {
        SDL_Quit();
        app->sdl_initialized = false;
    }
}

/* -------------------------------------------------------------------------- */
static void print_usage(void)
{
    puts("Usage: SHOWCASE.EXE [/L] [/H]");
    puts("");
    puts("Four-panel 320x200 indexed SDL3/DOS starter template.");
    puts("  /L, /LOG   Write startup/device diagnostics to SHOWCASE.LOG (off by default)");
    puts("  /H, /?     Show this help text");
}

/* Entry point: try optional assets/devices without losing the core demo.      */
/* -------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    ShowcaseApp app = { 0 };
    bool log_enabled = false;
    bool video_ready;
    bool run_ok = true;
    int exit_status = 1;
    int arg_index;

    for (arg_index = 1; arg_index < argc; ++arg_index) {
        const char *arg = argv[arg_index];
        if (SDL_strcasecmp(arg, "/h") == 0 ||
            SDL_strcasecmp(arg, "-h") == 0 ||
            SDL_strcasecmp(arg, "--help") == 0 ||
            strcmp(arg, "/?") == 0) {
            print_usage();
            return 0;
        }
        if (SDL_strcasecmp(arg, "/l") == 0 ||
            SDL_strcasecmp(arg, "/log") == 0 ||
            SDL_strcasecmp(arg, "-l") == 0 ||
            SDL_strcasecmp(arg, "--log") == 0) {
            log_enabled = true;
        } else {
            fprintf(stderr, "Unknown option: %s\n\n", arg);
            print_usage();
            return 1;
        }
    }

    if (log_enabled) {
        log_file = fopen(LOG_FILE, "w");
        if (!log_file) {
            fprintf(stderr, "Warning: could not open %s; continuing without debug logging.\n",
                    LOG_FILE);
        }
    }
    LOG("SHOWCASE start");

    set_default_data(&app);
    (void)load_settings(&app);
    (void)load_score_record(&app);

    video_ready = initialize_video(&app);
    if (video_ready) {
        (void)load_sprite(&app.ship, SHIP_BMP, "playerShip2_blue");
        (void)load_sprite(&app.ufo, UFO_BMP, "ufoGreen");
        (void)load_sprite(&app.laser_sprite, LASER_BMP, "laserRed02");
        initialize_joystick(&app);
        initialize_audio(&app);
        initialize_game(&app);
        app.palette.target_brightness = 16;
        app.palette.last_step_ticks = SDL_GetTicks();
        (void)save_settings(&app);
        run_ok = run_showcase(&app);
        fade_out_before_exit(&app);
        exit_status = run_ok ? 0 : 1;
    } else if (log_enabled) {
        fprintf(stderr, "SHOWCASE could not initialize the display. See %s if writable.\n",
                LOG_FILE);
    } else {
        fprintf(stderr, "SHOWCASE could not initialize the display. Run with /L to write %s.\n",
                LOG_FILE);
    }

    LOG("SHOWCASE exit status=%d score=%d best=%d",
        exit_status, app.game.score, app.record.best_score);
    shutdown_showcase(&app);
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
    return exit_status;
}
