#include <SDL3/SDL.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_main.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* by @mmoroca & Arena.AI 2026 (http://arena.ai/) */

/*
 * SHOOTER is a standalone full-screen 320x200 SDL3/DOS arcade game built on
 * the reusable systems from showcase.c: indexed Mode-13h-style video, RGB 3:3:2
 * palette fading, U8 mono 11025-Hz PCM mixing (with engine sound at 50% of
 * master volume), keyboard + text input, optional 8-direction joystick input,
 * and plain-text configuration / high-score files.
 *
 * Gameplay rules:
 * - Each run begins with 1 UFO descending along one of four distinct movement
 *   patterns, visually identified by its hull ring color:
 *     * Blue   (UFOBLUE.BMP)  -> PATTERN_WAVE   (smooth sinusoidal wave)
 *     * Yellow (UFOYEL~1.BMP) -> PATTERN_ZIGZAG (timed diagonal switchbacks)
 *     * Red    (UFORED.BMP)   -> PATTERN_SWOOP  (cruise + fast vertical dives)
 *     * Green  (UFOGREEN.BMP) -> PATTERN_DRIFT  (diagonal glide + shifts)
 * - Destroying any UFO immediately spawns 2 new descending UFOs (ENEMIES_PER_KILL),
 *   raising the simultaneous active enemy count on screen after every kill up
 *   to MAX_ENEMIES (256 simultaneous live enemies, plus overflow queue).
 * - Any UFO reaching the bottom wraps back to the top with a new random X
 *   coordinate and behavior/color so active threats never disappear.
 * - The run ends immediately when an enemy's opaque pixels collide with the
 *   player ship sprite (pixel-accurate check on pre-baked 1:4 L1-resident masks).
 *
 * All motion uses integer and 24.8 fixed-point arithmetic (zero floating-point
 * operations) to keep frame times fast on 486SX/DX hardware under DJGPP.
 */

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200
#define HUD_HEIGHT 12
#define PLAYFIELD_TOP HUD_HEIGHT
#define PLAYFIELD_BOTTOM SCREEN_HEIGHT

#define DOS_PIXEL_FORMAT_PROPERTY "SDL_PIXELFORMAT"
#define FIXED_POINT_SHIFT 8
#define FIXED_POINT_ONE (1 << FIXED_POINT_SHIFT)
#define DIAGONAL_FACTOR_FP 181 /* ~256 / sqrt(2), keeps 8-way speed uniform. */

/* 4 = quarter linear size (28x19 ship, 23x23 UFOs, 4x10 laser). */
#define SPRITE_SCALE_DIVISOR 4
#define TRANSPARENT_INDEX 0
#define CALLSIGN_CAPACITY 9
#define UFO_MODEL_COUNT 4

/* Gameplay tuning constants. */
#define INITIAL_ENEMIES 1
/*
 * Spawning 2 replacements whenever 1 enemy is destroyed increases the active
 * on-screen enemy count by +1 per kill (1 -> 2 -> 3 -> 4 ...).
 */
#define ENEMIES_PER_KILL 2
#define MAX_ENEMIES 256
#define STAR_COUNT 48
#define LASER_COUNT 10
#define SPARK_COUNT 24
#define LASER_COOLDOWN_MS 150U
#define GAME_OVER_GUARD_MS 500U
#define SPLASH_DURATION_MS 5000U

#define PLAYER_SPEED_PX_PER_SEC 138
#define LASER_SPEED_PX_PER_SEC 225

/* Audio stream configuration (shared with SHOWCASE WAV conversions). */
#define AUDIO_SAMPLE_RATE 11025
#define AUDIO_CHUNK_FRAMES 256
#define AUDIO_QUEUE_TARGET_BYTES (AUDIO_CHUNK_FRAMES * 3)
#define AUDIO_QUEUE_LIMIT_BYTES (AUDIO_CHUNK_FRAMES * 8)
#define AUDIO_LOOP_DELAY_MS 16
#define FADE_STEP_MS 20

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* Shared RGB 3:3:2 palette color indices. */
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

/* Strict DOS 8.3 filenames for runtime persistence and shared assets. */
#define CONFIG_FILE "SHOOTER.CFG"
#define SCORE_FILE "SHOOTER.SCO"
#define LOG_FILE "SHOOTER.LOG"
#define SPLASH_BMP "SHOWCASE/UFO.BMP"
#define SHIP_BMP "SHOWCASE/PLAYER~1.BMP"
#define UFO_BLUE_BMP "SHOWCASE/UFOBLUE.BMP"
#define UFO_YELLOW_BMP "SHOWCASE/UFOYEL~1.BMP"
#define UFO_RED_BMP "SHOWCASE/UFORED.BMP"
#define UFO_GREEN_BMP "SHOWCASE/UFOGREEN.BMP"
#define LASER_BMP "SHOWCASE/LASERR~1.BMP"
#define MUSIC_WAV "SHOWCASE/MUSIC.WAV"
#define LASER_WAV "SHOWCASE/LASER.WAV"
#define HIT_WAV "SHOWCASE/HIT.WAV"
#define ENGINE_WAV "SHOWCASE/ENGINE.WAV"

typedef enum RunPhase {
    PHASE_SPLASH = 0,
    PHASE_TITLE = 1,
    PHASE_PLAYING = 2,
    PHASE_PAUSED = 3,
    PHASE_CONFIRM_EXIT = 4,
    PHASE_GAME_OVER = 5
} RunPhase;

typedef enum EnemyPattern {
    PATTERN_WAVE = 0,   /* Blue UFO:   sinusoidal weave around a base column. */
    PATTERN_ZIGZAG = 1, /* Yellow UFO: crisp diagonal legs reversing on time. */
    PATTERN_SWOOP = 2,  /* Red UFO:    alternating cruise + fast dive bursts. */
    PATTERN_DRIFT = 3   /* Green UFO:  steady diagonal glide + course shifts. */
} EnemyPattern;

typedef struct Sprite {
    SDL_Surface *surface;
    Uint8 *baked;
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
    int best_peak_enemies;
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

typedef struct Spark {
    Sint32 x_fp;
    Sint32 y_fp;
    Sint32 vx_fp_per_sec;
    Sint32 vy_fp_per_sec;
    Uint32 remaining_ms;
    Uint8 color;
    bool active;
} Spark;

typedef struct Enemy {
    Sint32 x_fp;
    Sint32 y_fp;
    Sint32 anchor_x_fp;
    Sint32 vx_px_per_sec;
    Sint32 base_vy_px_per_sec;
    int amplitude_px;
    Uint32 phase_fp;
    Uint32 phase_rate_fp_per_sec;
    Uint32 pattern_timer_ms;
    EnemyPattern pattern;
    bool diving;
    bool active;
} Enemy;

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
    bool previous_joystick_fire;
    bool fire_pressed_edge;
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
    RunPhase phase;
    RunPhase confirm_return_phase;
    Uint64 phase_changed_ticks;
    Uint32 rng_state;
    Sint32 player_x_fp;
    Sint32 player_y_fp;
    int score;
    int active_enemies;
    int peak_enemies;
    int pending_spawns;
    bool new_record_achieved;
    bool engine_active;
    Uint64 last_fire_ticks;
    Star stars[STAR_COUNT];
    Laser lasers[LASER_COUNT];
    Enemy enemies[MAX_ENEMIES];
    Spark sparks[SPARK_COUNT];
} GameState;

typedef struct ShooterApp {
    SDL_Window *window;
    SDL_Surface *window_surface;
    SDL_Surface *canvas;
    SDL_Surface *splash;
    SDL_Joystick *joystick;
    SDL_JoystickID joystick_id;
    char base_path[256];
    PaletteState palette;
    AudioState audio;
    InputState input;
    UserSettings settings;
    ScoreRecord record;
    Sprite ship;
    Sprite ufos[UFO_MODEL_COUNT];
    Sprite laser_sprite;
    GameState game;
    char status_text[44];
    Uint64 status_until_ticks;
    bool sdl_initialized;
    bool running;
} ShooterApp;

/* 64-entry integer sine table scaled by 256 (one full period = 64 steps). */
static const Sint16 sine_table_64[64] = {
       0,   25,   50,   74,   98,  121,  142,  162,
     181,  198,  213,  226,  237,  245,  251,  255,
     256,  255,  251,  245,  237,  226,  213,  198,
     181,  162,  142,  121,   98,   74,   50,   25,
       0,  -25,  -50,  -74,  -98, -121, -142, -162,
    -181, -198, -213, -226, -237, -245, -251, -255,
    -256, -255, -251, -245, -237, -226, -213, -198,
    -181, -162, -142, -121,  -98,  -74,  -50,  -25
};

static FILE *log_file;

#define LOG(...) do { \
    if (log_file) { \
        fprintf(log_file, __VA_ARGS__); \
        fputc('\n', log_file); \
        fflush(log_file); \
    } \
} while (0)

/* -------------------------------------------------------------------------- */
/* Deterministic PRNG and integer helpers (zero floating point).              */
/* -------------------------------------------------------------------------- */
static void seed_rng(GameState *game, Uint32 seed)
{
    game->rng_state = (seed != 0U) ? seed : 0x9E3779B9U;
}

static Uint32 next_random_u32(GameState *game)
{
    Uint32 x = game->rng_state;

    if (x == 0U) {
        x = 0x6D2B79F5U;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    game->rng_state = x;
    return x;
}

static int random_range(GameState *game, int minimum, int maximum)
{
    Uint32 span;

    if (maximum <= minimum) {
        return minimum;
    }
    span = (Uint32)(maximum - minimum + 1);
    return minimum + (int)(next_random_u32(game) % span);
}

/* -------------------------------------------------------------------------- */
/* Bounded text utilities, asset path resolution, and plain-text persistence. */
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

static void set_status(ShooterApp *app, const char *text, Uint32 duration_ms)
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

static void resolve_asset_path(const ShooterApp *app, const char *relative_path,
                               char *buffer, size_t capacity)
{
    FILE *probe;

    if (capacity == 0) {
        return;
    }
    probe = fopen(relative_path, "rb");
    if (probe) {
        (void)fclose(probe);
        (void)snprintf(buffer, capacity, "%s", relative_path);
        return;
    }
    if (app->base_path[0] != '\0') {
        (void)snprintf(buffer, capacity, "%s%s", app->base_path, relative_path);
        return;
    }
    (void)snprintf(buffer, capacity, "%s", relative_path);
}

static void set_default_data(ShooterApp *app)
{
    copy_callsign(app->settings.callsign, "CADET");
    app->settings.master_volume = 200;
    app->settings.music_enabled = true;
    app->record.best_score = 0;
    app->record.best_peak_enemies = INITIAL_ENEMIES;
    copy_callsign(app->record.callsign, "---");
}

static bool load_settings(ShooterApp *app)
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

static bool save_settings(const ShooterApp *app)
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

static bool load_score_record(ShooterApp *app)
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
        } else if (strcmp(line, "peak") == 0 &&
                   parse_long_value(value, 1, 2000000000L, &parsed)) {
            loaded.best_peak_enemies = (int)parsed;
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
    LOG("Score record loaded: best=%d peak=%d name=%s",
        app->record.best_score, app->record.best_peak_enemies,
        app->record.callsign);
    return found_score;
}

static bool save_score_record(const ShooterApp *app)
{
    FILE *file = fopen(SCORE_FILE, "w");
    bool ok = true;

    if (!file) {
        LOG("Could not open %s for writing", SCORE_FILE);
        return false;
    }
    if (fprintf(file, "version=1\nbest=%d\npeak=%d\nname=%s\n",
                app->record.best_score,
                app->record.best_peak_enemies,
                app->record.callsign) < 0) {
        ok = false;
    }
    if (fclose(file) != 0) {
        ok = false;
    }
    LOG("Score save %s: best=%d peak=%d",
        ok ? "OK" : "FAILED", app->record.best_score, app->record.best_peak_enemies);
    return ok;
}

/* -------------------------------------------------------------------------- */
/* Video initialization and RGB 3:3:2 palette fade control.                   */
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

static bool apply_palette_brightness(ShooterApp *app, Uint8 level)
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

static bool initialize_video(ShooterApp *app)
{
    SDL_PropertiesID properties;
    const char *sdl_base;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "Error: SDL video initialization failed: %s\n", SDL_GetError());
        LOG("SDL_Init(SDL_INIT_VIDEO) failed: %s", SDL_GetError());
        return false;
    }
    app->sdl_initialized = true;
    sdl_base = SDL_GetBasePath();
    if (sdl_base) {
        (void)snprintf(app->base_path, sizeof(app->base_path), "%s", sdl_base);
    }
    LOG("SDL video driver: %s",
        SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(unknown)");

    properties = SDL_CreateProperties();
    if (!properties) {
        fprintf(stderr, "Error: could not create window properties: %s\n", SDL_GetError());
        return false;
    }
    if (!SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING,
                               "SDL3 Arcade Shooter") ||
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
    (void)SDL_HideCursor();

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

static void update_palette_fade(ShooterApp *app, Uint64 now)
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
/* Software drawing primitives and built-in 5x7 uppercase bitmap font.        */
/* -------------------------------------------------------------------------- */
/*
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
*/

typedef struct Glyph {
    char character;
    Uint8 rows[7];
} Glyph;

static const Glyph font_5x7[] = {
    { 'A', { 12,18,17,17,31,17,17 } }, { 'B', { 12,18,17,17,30,17,30 } },
    { 'C', { 12,18,17,16,16,17,30 } }, { 'D', { 12,18,17,17,17,17,30 } },
    { 'E', { 12,18,17,16,28,17,30 } }, { 'F', { 12,18,17,16,28,16,16 } },
    { 'G', { 12,18,17,16,22,17,30 } }, { 'H', { 4,2,17,17,31,17,17 } },
    { 'I', { 12,6,5,4,4,4,14 } }, { 'J', { 12,18,1,1,1,17,30 } },
    { 'K', { 4,18,17,17,30,17,17 } }, { 'L', { 0,16,16,16,16,17,30 } },
    { 'M', { 12,22,21,21,17,17,17 } }, { 'N', { 12,18,17,17,17,17,17 } },
    { 'O', { 12,18,17,17,17,17,14 } }, { 'P', { 12,18,17,17,30,16,16 } },
    { 'Q', { 12,18,17,17,21,18,29 } }, { 'R', { 12,18,17,17,30,17,17 } },
    { 'S', { 12,18,17,16,14,1,30 } }, { 'T', { 12,22,5,4,4,4,4 } },
    { 'U', { 4,18,17,17,17,17,30 } }, { 'V', { 4,18,17,17,18,20,24 } },
    { 'W', { 4,18,17,17,21,21,30 } }, { 'X', { 4,18,17,17,14,17,17 } },
    { 'Y', { 4,18,17,17,14,4,4 } }, { 'Z', { 12,18,1,2,4,8,31 } },
    { '0', { 12,18,25,21,19,17,14 } }, { '1', { 4,6,5,4,4,4,14 } },
    { '2', { 12,18,1,1,14,16,31 } }, { '3', { 12,2,1,1,14,1,30 } },
    { '4', { 4,2,1,17,17,31,4 } }, { '5', { 28,18,17,16,31,1,30 } },
    { '6', { 12,18,17,16,30,17,31 } }, { '7', { 12,2,1,1,2,4,8 } },
    { '8', { 12,18,17,17,14,17,30 } }, { '9', { 12,18,17,17,14,1,14 } },
    { '!', { 4,4,4,4,4,0,4 } }, { '?', { 14,17,1,1,6,0,4 } },
    { ':', { 0,4,4,0,4,4,0 } }, { ';', { 0,4,4,0,4,4,8 } },
    { '=', { 0,0,0,14,0,14,0 } }, { '.', { 0,0,0,0,4,4,0 } },
    { ',', { 0,0,0,0,4,4,8 } }, { '-', { 0,0,0,0,14,0,0 } },
    { '+', { 0,0,0,4,14,4,0 } }, { '/', { 0,2,2,4,4,8,8 } },
    { '_', { 0,0,0,0,0,14,0 } }, { '%', { 0,8,2,4,8,2,0 } },
    { '[', { 0,6,4,4,4,6,0 } }, { ']', { 0,12,4,4,4,12,0 } },
    { '(', { 0,2,4,4,4,2,0 } }, { ')', { 0,8,4,4,4,8,0 } },
    { '<', { 0,0,0,4,8,4,0 } }, { '>', { 0,0,0,4,2,4,0 } },
    { '|', { 0,4,4,4,4,4,0 } }, { '*', { 0,0,0,10,4,10,0 } }
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

static int measure_text_width(const char *text)
{
    size_t length;

    if (!text || !*text) {
        return 0;
    }
    length = strlen(text);
    return (int)(length * 6U - 1U);
}

static void draw_text(SDL_Surface *surface, int x, int y, const char *text, Uint8 color)
{
    const int origin_x = x;

    if (!surface || !surface->pixels || !text) {
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
            const int pixel_y = y + row;
            if (pixel_y < 0 || pixel_y >= SCREEN_HEIGHT) {
                continue;
            }
            for (column = 0; column < 5; ++column) {
                const int pixel_x = x + column;
                if ((rows[row] & (Uint8)(1U << (4 - column))) &&
                    pixel_x >= 0 && pixel_x < SCREEN_WIDTH) {
                    *((Uint8 *)surface->pixels + (pixel_y * surface->pitch) + pixel_x) = color;
                }
            }
        }
        x += 6;
        ++text;
    }
}

static void draw_centered_text(SDL_Surface *surface, int y, const char *text, Uint8 color)
{
    const int width = measure_text_width(text);
    draw_text(surface, (SCREEN_WIDTH - width) / 2, y, text, color);
}

/*
 * Centers "PILOT: <CALLSIGN>" using ONLY the call sign's own character width,
 * then appends the blinking "_" cursor at the right edge without altering X.
 */
static void draw_pilot_line(SDL_Surface *surface, int y, const char *callsign,
                            bool show_cursor)
{
    char line[32];
    int width;
    int base_x;

    (void)snprintf(line, sizeof(line), "PILOT: %.8s", callsign ? callsign : "");
    width = measure_text_width(line);
    base_x = (SCREEN_WIDTH - width) / 2;
    draw_text(surface, base_x, y, line, COLOR_GREEN);
    if (show_cursor) {
        draw_text(surface, base_x + width + 1, y, "_", COLOR_GREEN);
    }
}

/* -------------------------------------------------------------------------- */
/* Sprite loading, pre-baked 1:4 L1 cache buffers, and pixel collision.       */
/* -------------------------------------------------------------------------- */
static void destroy_sprite(Sprite *sprite)
{
    if (!sprite) {
        return;
    }
    if (sprite->baked) {
        free(sprite->baked);
        sprite->baked = NULL;
    }
    if (sprite->surface) {
        SDL_DestroySurface(sprite->surface);
        sprite->surface = NULL;
    }
    sprite->width = 0;
    sprite->height = 0;
}

static bool load_sprite(const ShooterApp *app, Sprite *sprite,
                        const char *relative_path, const char *description)
{
    char resolved_path[320];
    SDL_Surface *surface;
    Uint8 *baked;
    int dst_w;
    int dst_h;
    int dy;

    destroy_sprite(sprite);
    resolve_asset_path(app, relative_path, resolved_path, sizeof(resolved_path));
    surface = SDL_LoadBMP(resolved_path);
    if (!surface) {
        LOG("Sprite missing (%s): %s (%s)", description, resolved_path, SDL_GetError());
        return false;
    }
    if (surface->format != SDL_PIXELFORMAT_INDEX8 || !surface->pixels) {
        LOG("Sprite must be an 8-bit indexed BMP (%s): format=%s",
            description, SDL_GetPixelFormatName(surface->format));
        SDL_DestroySurface(surface);
        return false;
    }

    dst_w = (surface->w + SPRITE_SCALE_DIVISOR - 1) / SPRITE_SCALE_DIVISOR;
    dst_h = (surface->h + SPRITE_SCALE_DIVISOR - 1) / SPRITE_SCALE_DIVISOR;
    baked = (Uint8 *)malloc((size_t)dst_w * (size_t)dst_h);
    if (!baked) {
        LOG("Out of memory baking sprite (%s)", description);
        SDL_DestroySurface(surface);
        return false;
    }

    for (dy = 0; dy < dst_h; ++dy) {
        const int sy = dy * SPRITE_SCALE_DIVISOR;
        const Uint8 *src_row = (sy < surface->h)
            ? ((const Uint8 *)surface->pixels + (sy * surface->pitch))
            : NULL;
        int dx;
        for (dx = 0; dx < dst_w; ++dx) {
            const int sx = dx * SPRITE_SCALE_DIVISOR;
            baked[dy * dst_w + dx] = (src_row && sx < surface->w)
                ? src_row[sx]
                : TRANSPARENT_INDEX;
        }
    }

    sprite->surface = surface;
    sprite->baked = baked;
    sprite->width = dst_w;
    sprite->height = dst_h;
    LOG("Sprite loaded: %s source=%dx%d display=%dx%d",
        description, surface->w, surface->h, sprite->width, sprite->height);
    return true;
}

static bool load_splash_bitmap(ShooterApp *app, const char *relative_path)
{
    char resolved_path[320];
    SDL_Surface *surface;

    resolve_asset_path(app, relative_path, resolved_path, sizeof(resolved_path));
    surface = SDL_LoadBMP(resolved_path);
    if (!surface) {
        LOG("Splash unavailable: %s (%s)", resolved_path, SDL_GetError());
        return false;
    }
    if (surface->format != SDL_PIXELFORMAT_INDEX8 || !surface->pixels) {
        LOG("Splash format mismatch: %s is not 8-bit indexed", resolved_path);
        SDL_DestroySurface(surface);
        return false;
    }
    app->splash = surface;
    LOG("Splash loaded: %s size=%dx%d", resolved_path, surface->w, surface->h);
    return true;
}

static const Sprite *ufo_sprite_for_pattern(const ShooterApp *app,
                                            EnemyPattern pattern)
{
    const unsigned int idx = (unsigned int)pattern % UFO_MODEL_COUNT;
    return &app->ufos[idx];
}

static Uint8 ufo_accent_color(EnemyPattern pattern)
{
    switch (pattern) {
    case PATTERN_WAVE:
        return COLOR_CYAN;
    case PATTERN_ZIGZAG:
        return COLOR_YELLOW;
    case PATTERN_SWOOP:
        return COLOR_RED;
    case PATTERN_DRIFT:
    default:
        return COLOR_GREEN;
    }
}

static int sprite_width_or(const Sprite *sprite, int fallback_width)
{
    return (sprite && sprite->baked) ? sprite->width : fallback_width;
}

static int sprite_height_or(const Sprite *sprite, int fallback_height)
{
    return (sprite && sprite->baked) ? sprite->height : fallback_height;
}

static int reference_ufo_width(const ShooterApp *app)
{
    int i;
    for (i = 0; i < UFO_MODEL_COUNT; ++i) {
        if (app->ufos[i].baked) {
            return app->ufos[i].width;
        }
    }
    return 23;
}

static int reference_ufo_height(const ShooterApp *app)
{
    int i;
    for (i = 0; i < UFO_MODEL_COUNT; ++i) {
        if (app->ufos[i].baked) {
            return app->ufos[i].height;
        }
    }
    return 23;
}

/*
 * Fast contiguous L1-friendly scanline blitter: all 6 pre-baked 1:4 sprites
 * total ~2.7 KB, keeping inner scanline loops resident in i486 L1 cache even
 * when hundreds of multicolor UFOs are drawn on screen simultaneously.
 */
static void draw_scaled_sprite(SDL_Surface *destination, const Sprite *sprite,
                               int x, int y, const SDL_Rect *clip)
{
    int min_x = 0;
    int min_y = 0;
    int max_x = SCREEN_WIDTH;
    int max_y = SCREEN_HEIGHT;
    int start_dx;
    int end_dx;
    int start_dy;
    int end_dy;
    int draw_y;

    if (!destination || !destination->pixels || !sprite || !sprite->baked) {
        return;
    }
    if (clip) {
        if (clip->x > min_x) min_x = clip->x;
        if (clip->y > min_y) min_y = clip->y;
        if (clip->x + clip->w < max_x) max_x = clip->x + clip->w;
        if (clip->y + clip->h < max_y) max_y = clip->y + clip->h;
    }

    start_dx = (x < min_x) ? (min_x - x) : 0;
    start_dy = (y < min_y) ? (min_y - y) : 0;
    end_dx = sprite->width;
    if (x + end_dx > max_x) {
        end_dx = max_x - x;
    }
    end_dy = sprite->height;
    if (y + end_dy > max_y) {
        end_dy = max_y - y;
    }
    if (start_dx >= end_dx || start_dy >= end_dy) {
        return;
    }

    for (draw_y = start_dy; draw_y < end_dy; ++draw_y) {
        const Uint8 *source_row = sprite->baked + (draw_y * sprite->width);
        Uint8 *destination_row = (Uint8 *)destination->pixels +
                                 ((y + draw_y) * destination->pitch) + x;
        int draw_x;

        for (draw_x = start_dx; draw_x < end_dx; ++draw_x) {
            const Uint8 color = source_row[draw_x];
            if (color != TRANSPARENT_INDEX) {
                destination_row[draw_x] = color;
            }
        }
    }
}

static bool sprite_pixel_opaque(const Sprite *sprite, int local_x, int local_y)
{
    if (!sprite || !sprite->baked) {
        return true;
    }
    if (local_x < 0 || local_x >= sprite->width ||
        local_y < 0 || local_y >= sprite->height) {
        return false;
    }
    return sprite->baked[local_y * sprite->width + local_x] != TRANSPARENT_INDEX;
}

static bool sprites_collide(const Sprite *a, int ax, int ay, int aw, int ah,
                            const Sprite *b, int bx, int by, int bw, int bh)
{
    const int top = (ay > by) ? ay : by;
    const int bottom = ((ay + ah) < (by + bh)) ? (ay + ah) : (by + bh);
    int left;
    int right;
    int screen_y;

    if (top >= bottom) {
        return false;
    }
    left = (ax > bx) ? ax : bx;
    right = ((ax + aw) < (bx + bw)) ? (ax + aw) : (bx + bw);
    if (left >= right) {
        return false;
    }
    if (!a || !a->baked || !b || !b->baked) {
        return true;
    }
    for (screen_y = top; screen_y < bottom; ++screen_y) {
        int screen_x;
        for (screen_x = left; screen_x < right; ++screen_x) {
            if (sprite_pixel_opaque(a, screen_x - ax, screen_y - ay) &&
                sprite_pixel_opaque(b, screen_x - bx, screen_y - by)) {
                return true;
            }
        }
    }
    return false;
}

/* -------------------------------------------------------------------------- */
/* Software WAV PCM loader and stream mixer (engine mixed at 50% of master).  */
/* -------------------------------------------------------------------------- */
static bool load_pcm_clip(const ShooterApp *app, PcmClip *clip,
                          const char *relative_path, const char *label)
{
    char resolved_path[320];
    SDL_AudioSpec spec;
    Uint8 *samples = NULL;
    Uint32 length = 0;

    resolve_asset_path(app, relative_path, resolved_path, sizeof(resolved_path));
    if (!SDL_LoadWAV(resolved_path, &spec, &samples, &length)) {
        LOG("WAV unavailable (%s): %s (%s)", label, resolved_path, SDL_GetError());
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

static void mix_audio_block(ShooterApp *app, Uint8 output[AUDIO_CHUNK_FRAMES])
{
    AudioState *audio = &app->audio;
    unsigned int frame;
    const unsigned int master = (unsigned int)app->settings.master_volume;
    const unsigned int music_volume = (master * 60U) / 100U;
    const unsigned int engine_volume = master / 2U; /* Exactly 50% of master. */
    const bool engine_playing = (app->game.phase == PHASE_PLAYING) &&
                                app->game.engine_active;

    if (app->settings.music_enabled && audio->music.samples) {
        audio->music.active = true;
    }
    if (engine_playing && audio->engine.samples) {
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
        if (engine_playing) {
            mixed += mix_clip_sample(&audio->engine, true, engine_volume);
        }
        mixed += mix_clip_sample(&audio->laser, false, master);
        mixed += mix_clip_sample(&audio->hit, false, master);
        if (mixed > 127) mixed = 127;
        if (mixed < -128) mixed = -128;
        output[frame] = (Uint8)(mixed + 128);
    }
}

static void feed_audio_queue(ShooterApp *app)
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

static void initialize_audio(ShooterApp *app)
{
    AudioState *audio = &app->audio;
    const char *environment_blaster = getenv("BLASTER");
    const char *device_name;
    const char *driver_name;

    copy_upper_text(audio->blaster_text, sizeof(audio->blaster_text),
                    environment_blaster ? environment_blaster : "NOT SET");
    audio->music_available = load_pcm_clip(app, &audio->music, MUSIC_WAV, "music");
    audio->laser_available = load_pcm_clip(app, &audio->laser, LASER_WAV, "laser");
    audio->hit_available = load_pcm_clip(app, &audio->hit, HIT_WAV, "impact");
    audio->engine_available = load_pcm_clip(app, &audio->engine, ENGINE_WAV, "engine");
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
/* Optional joystick support: 8 directions + button 1 fire/launch edge.       */
/* -------------------------------------------------------------------------- */
static void initialize_joystick(ShooterApp *app)
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

static void poll_joystick(ShooterApp *app)
{
    int axes;
    int hats;
    int buttons;
    bool current_fire;

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
        app->input.previous_joystick_fire = false;
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

    current_fire = app->input.joystick_buttons[0] || app->input.joystick_buttons[1];
    if (current_fire && !app->input.previous_joystick_fire) {
        app->input.fire_pressed_edge = true;
    }
    app->input.previous_joystick_fire = current_fire;
}

/* -------------------------------------------------------------------------- */
/* Starfield, particle bursts, color-coded enemy patterns, and simulation.    */
/* -------------------------------------------------------------------------- */
static void initialize_starfield(GameState *game)
{
    int index;

    for (index = 0; index < STAR_COUNT; ++index) {
        Star *star = &game->stars[index];
        star->x = random_range(game, 2, SCREEN_WIDTH - 3);
        star->y = random_range(game, PLAYFIELD_TOP + 2, SCREEN_HEIGHT - 3);
        star->speed_pixels_per_second = (Uint8)random_range(game, 10, 42);
        star->color = (random_range(game, 0, 3) == 0) ? COLOR_CYAN :
                      ((random_range(game, 0, 1) == 0) ? COLOR_GRAY : COLOR_WHITE);
        star->fractional_motion = 0;
        star->twinkle = (Uint8)random_range(game, 0, 7);
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
        if (star->y >= SCREEN_HEIGHT - 1) {
            star->y = PLAYFIELD_TOP + 1;
            star->x = random_range(game, 2, SCREEN_WIDTH - 3);
            star->speed_pixels_per_second = (Uint8)random_range(game, 10, 42);
        }
        star->twinkle = (Uint8)((star->twinkle + 1U) & 7U);
    }
}

static void emit_explosion_sparks(GameState *game, int center_x, int center_y,
                                  int count, Uint8 primary_color)
{
    int spawned = 0;
    int index;

    for (index = 0; index < SPARK_COUNT && spawned < count; ++index) {
        Spark *spark = &game->sparks[index];
        if (!spark->active) {
            spark->active = true;
            spark->x_fp = center_x * FIXED_POINT_ONE;
            spark->y_fp = center_y * FIXED_POINT_ONE;
            spark->vx_fp_per_sec = random_range(game, -95, 95) * FIXED_POINT_ONE;
            spark->vy_fp_per_sec = random_range(game, -95, 95) * FIXED_POINT_ONE;
            spark->remaining_ms = (Uint32)random_range(game, 180, 340);
            spark->color = ((spawned & 1) == 0) ? primary_color : COLOR_WHITE;
            ++spawned;
        }
    }
}

static void update_sparks(GameState *game, Uint32 delta_ms)
{
    int index;

    for (index = 0; index < SPARK_COUNT; ++index) {
        Spark *spark = &game->sparks[index];
        if (!spark->active) {
            continue;
        }
        if (delta_ms >= spark->remaining_ms) {
            spark->active = false;
            continue;
        }
        spark->remaining_ms -= delta_ms;
        spark->x_fp += (Sint32)(((Sint64)spark->vx_fp_per_sec * (Sint64)delta_ms) / 1000);
        spark->y_fp += (Sint32)(((Sint64)spark->vy_fp_per_sec * (Sint64)delta_ms) / 1000);
    }
}

/*
 * Assign a randomized position above the top border and select one of the four
 * color-identified UFO behaviors (Blue=Wave, Yellow=Zigzag, Red=Swoop, Green=Drift).
 */
static void configure_enemy_descent(ShooterApp *app, Enemy *enemy, int slot_hint)
{
    GameState *game = &app->game;
    const int ufo_width = reference_ufo_width(app);
    const int ufo_height = reference_ufo_height(app);
    const int min_x = 3;
    const int max_x = SCREEN_WIDTH - ufo_width - 3;
    int chosen_x;
    int vertical_stagger;

    if (slot_hint == 0) {
        chosen_x = random_range(game, min_x, (SCREEN_WIDTH / 2) - ufo_width);
    } else if (slot_hint == 1) {
        chosen_x = random_range(game, SCREEN_WIDTH / 2, max_x);
    } else {
        chosen_x = random_range(game, min_x, max_x);
    }
    if (chosen_x < min_x) chosen_x = min_x;
    if (chosen_x > max_x) chosen_x = max_x;

    vertical_stagger = (slot_hint >= 0) ? (slot_hint * 12 + random_range(game, 0, 14))
                                        : random_range(game, 0, 22);

    enemy->active = true;
    enemy->pattern = (EnemyPattern)random_range(game, 0, UFO_MODEL_COUNT - 1);
    enemy->amplitude_px = random_range(game, 18, 46);
    if (enemy->pattern == PATTERN_WAVE) {
        const int wave_min_x = min_x + enemy->amplitude_px;
        const int wave_max_x = max_x - enemy->amplitude_px;
        if (chosen_x < wave_min_x || chosen_x > wave_max_x) {
            chosen_x = random_range(game, wave_min_x, wave_max_x);
        }
    }
    enemy->anchor_x_fp = chosen_x * FIXED_POINT_ONE;
    enemy->x_fp = enemy->anchor_x_fp;
    enemy->y_fp = (PLAYFIELD_TOP - ufo_height - vertical_stagger) * FIXED_POINT_ONE;
    enemy->base_vy_px_per_sec = random_range(game, 34, 58);
    enemy->phase_fp = (Uint32)random_range(game, 0, 63) * FIXED_POINT_ONE;
    enemy->phase_rate_fp_per_sec = (Uint32)random_range(game, 26, 54) * FIXED_POINT_ONE;
    enemy->vx_px_per_sec = ((random_range(game, 0, 1) == 0) ? -1 : 1) *
                           random_range(game, 38, 76);
    enemy->pattern_timer_ms = (Uint32)random_range(game, 450, 1050);
    enemy->diving = false;
}

static bool spawn_enemy(ShooterApp *app, int slot_hint)
{
    int index;

    for (index = 0; index < MAX_ENEMIES; ++index) {
        Enemy *enemy = &app->game.enemies[index];
        if (!enemy->active) {
            configure_enemy_descent(app, enemy, slot_hint);
            ++app->game.active_enemies;
            if (app->game.active_enemies > app->game.peak_enemies) {
                app->game.peak_enemies = app->game.active_enemies;
            }
            return true;
        }
    }
    ++app->game.pending_spawns;
    return false;
}

static void flush_pending_spawns(ShooterApp *app)
{
    while (app->game.pending_spawns > 0 && app->game.active_enemies < MAX_ENEMIES) {
        --app->game.pending_spawns;
        (void)spawn_enemy(app, -1);
    }
}

static void update_single_enemy(ShooterApp *app, Enemy *enemy, Uint32 delta_ms)
{
    GameState *game = &app->game;
    const Sprite *ufo_sprite = ufo_sprite_for_pattern(app, enemy->pattern);
    const int ufo_width = sprite_width_or(ufo_sprite, 23);
    const Sint32 min_x_fp = 2 * FIXED_POINT_ONE;
    const Sint32 max_x_fp = (SCREEN_WIDTH - ufo_width - 2) * FIXED_POINT_ONE;
    Sint32 vy_px_per_sec = enemy->base_vy_px_per_sec;

    switch (enemy->pattern) {
    case PATTERN_WAVE:
        enemy->phase_fp += (enemy->phase_rate_fp_per_sec * delta_ms) / 1000U;
        {
            const unsigned int table_index = (enemy->phase_fp >> FIXED_POINT_SHIFT) & 63U;
            const Sint32 wave_offset_fp =
                (Sint32)sine_table_64[table_index] * enemy->amplitude_px;
            enemy->x_fp = enemy->anchor_x_fp + wave_offset_fp;
        }
        break;

    case PATTERN_ZIGZAG:
        if (delta_ms >= enemy->pattern_timer_ms) {
            enemy->vx_px_per_sec = -enemy->vx_px_per_sec;
            enemy->pattern_timer_ms = (Uint32)random_range(game, 420, 980);
        } else {
            enemy->pattern_timer_ms -= delta_ms;
        }
        enemy->x_fp += (Sint32)((enemy->vx_px_per_sec * (Sint32)delta_ms *
                                 FIXED_POINT_ONE) / 1000);
        break;

    case PATTERN_SWOOP:
        if (delta_ms >= enemy->pattern_timer_ms) {
            enemy->diving = !enemy->diving;
            enemy->pattern_timer_ms = enemy->diving
                ? (Uint32)random_range(game, 320, 560)
                : (Uint32)random_range(game, 520, 900);
            enemy->vx_px_per_sec = random_range(game, -62, 62);
        } else {
            enemy->pattern_timer_ms -= delta_ms;
        }
        if (enemy->diving) {
            vy_px_per_sec = enemy->base_vy_px_per_sec * 2 + 18;
        }
        enemy->x_fp += (Sint32)((enemy->vx_px_per_sec * (Sint32)delta_ms *
                                 FIXED_POINT_ONE) / 1000);
        break;

    case PATTERN_DRIFT:
    default:
        if (delta_ms >= enemy->pattern_timer_ms) {
            enemy->vx_px_per_sec = random_range(game, -70, 70);
            enemy->pattern_timer_ms = (Uint32)random_range(game, 600, 1250);
        } else {
            enemy->pattern_timer_ms -= delta_ms;
        }
        enemy->x_fp += (Sint32)((enemy->vx_px_per_sec * (Sint32)delta_ms *
                                 FIXED_POINT_ONE) / 1000);
        break;
    }

    if (enemy->x_fp < min_x_fp) {
        enemy->x_fp = min_x_fp;
        enemy->anchor_x_fp = (2 + enemy->amplitude_px) * FIXED_POINT_ONE;
        if (enemy->vx_px_per_sec < 0) {
            enemy->vx_px_per_sec = -enemy->vx_px_per_sec;
        }
    } else if (enemy->x_fp > max_x_fp) {
        enemy->x_fp = max_x_fp;
        enemy->anchor_x_fp =
            (SCREEN_WIDTH - ufo_width - 2 - enemy->amplitude_px) * FIXED_POINT_ONE;
        if (enemy->vx_px_per_sec > 0) {
            enemy->vx_px_per_sec = -enemy->vx_px_per_sec;
        }
    }

    enemy->y_fp += (Sint32)((vy_px_per_sec * (Sint32)delta_ms *
                             FIXED_POINT_ONE) / 1000);
    if ((enemy->y_fp / FIXED_POINT_ONE) >= PLAYFIELD_BOTTOM) {
        configure_enemy_descent(app, enemy, -1);
    }
}

static void reset_run_entities(ShooterApp *app)
{
    const int ship_width = sprite_width_or(&app->ship, 28);
    const int ship_height = sprite_height_or(&app->ship, 19);
    int index;

    app->game.player_x_fp = ((SCREEN_WIDTH - ship_width) / 2) * FIXED_POINT_ONE;
    app->game.player_y_fp = (SCREEN_HEIGHT - ship_height - 5) * FIXED_POINT_ONE;
    app->game.score = 0;
    app->game.active_enemies = 0;
    app->game.peak_enemies = 0;
    app->game.pending_spawns = 0;
    app->game.new_record_achieved = false;
    app->game.engine_active = false;
    app->game.last_fire_ticks = 0;

    for (index = 0; index < LASER_COUNT; ++index) {
        app->game.lasers[index].active = false;
        app->game.lasers[index].x = 0;
        app->game.lasers[index].y_fp = 0;
    }
    for (index = 0; index < MAX_ENEMIES; ++index) {
        app->game.enemies[index].active = false;
    }
    for (index = 0; index < SPARK_COUNT; ++index) {
        app->game.sparks[index].active = false;
    }
}

static void enter_title_screen(ShooterApp *app, Uint64 now)
{
    reset_run_entities(app);
    app->game.phase = PHASE_TITLE;
    app->game.phase_changed_ticks = now;
    app->input.fire_pressed_edge = false;
}

static void initialize_game(ShooterApp *app, Uint64 now)
{
    seed_rng(&app->game, (Uint32)(now ^ 0x5A17C3E9U));
    initialize_starfield(&app->game);
    reset_run_entities(app);
    app->game.phase = PHASE_SPLASH;
    app->game.phase_changed_ticks = now;
}

static void start_new_run(ShooterApp *app, Uint64 now)
{
    int index;

    reset_run_entities(app);
    app->game.phase = PHASE_PLAYING;
    app->game.phase_changed_ticks = now;
    app->input.fire_pressed_edge = false;
    for (index = 0; index < INITIAL_ENEMIES; ++index) {
        (void)spawn_enemy(app, -1);
    }
    set_status(app, "WAVE LIVE - +2 UFOS PER HIT!", 1800);
}

static void fire_laser(ShooterApp *app, Uint64 now)
{
    const int ship_width = sprite_width_or(&app->ship, 28);
    const int laser_width = sprite_width_or(&app->laser_sprite, 4);
    const int laser_height = sprite_height_or(&app->laser_sprite, 10);
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
            laser->y_fp = (player_y - laser_height + 2) * FIXED_POINT_ONE;
            app->game.last_fire_ticks = now;
            start_one_shot(&app->audio.laser);
            return;
        }
    }
}

static void destroy_enemy_and_split(ShooterApp *app, Enemy *enemy)
{
    const Sprite *ufo_sprite = ufo_sprite_for_pattern(app, enemy->pattern);
    const int ufo_width = sprite_width_or(ufo_sprite, 23);
    const int ufo_height = sprite_height_or(ufo_sprite, 23);
    const int hit_center_x = (int)(enemy->x_fp / FIXED_POINT_ONE) + ufo_width / 2;
    const int hit_center_y = (int)(enemy->y_fp / FIXED_POINT_ONE) + ufo_height / 2;
    const Uint8 burst_color = ufo_accent_color(enemy->pattern);
    int spawn_index;

    enemy->active = false;
    if (app->game.active_enemies > 0) {
        --app->game.active_enemies;
    }
    ++app->game.score;
    start_one_shot(&app->audio.hit);
    emit_explosion_sparks(&app->game, hit_center_x, hit_center_y, 6, burst_color);

    for (spawn_index = 0; spawn_index < ENEMIES_PER_KILL; ++spawn_index) {
        (void)spawn_enemy(app, spawn_index);
    }

    if (app->game.score > app->record.best_score) {
        app->record.best_score = app->game.score;
        if (app->game.peak_enemies > app->record.best_peak_enemies) {
            app->record.best_peak_enemies = app->game.peak_enemies;
        }
        copy_callsign(app->record.callsign, app->settings.callsign);
        app->game.new_record_achieved = true;
    }
}

static void trigger_player_impact(ShooterApp *app, Uint64 now)
{
    const int ship_width = sprite_width_or(&app->ship, 28);
    const int ship_height = sprite_height_or(&app->ship, 19);
    const int center_x = (int)(app->game.player_x_fp / FIXED_POINT_ONE) + ship_width / 2;
    const int center_y = (int)(app->game.player_y_fp / FIXED_POINT_ONE) + ship_height / 2;

    app->game.phase = PHASE_GAME_OVER;
    app->game.phase_changed_ticks = now;
    app->game.engine_active = false;
    app->input.fire_pressed_edge = false;
    start_one_shot(&app->audio.hit);
    emit_explosion_sparks(&app->game, center_x, center_y, 14, COLOR_RED);

    if (app->game.new_record_achieved) {
        if (app->game.peak_enemies > app->record.best_peak_enemies) {
            app->record.best_peak_enemies = app->game.peak_enemies;
        }
        if (save_score_record(app)) {
            set_status(app, "NEW BEST SCORE SAVED!", 3000);
        } else {
            set_status(app, "NEW BEST - SAVE FAILED", 3000);
        }
    }
    LOG("Run ended: score=%d active=%d peak=%d best=%d",
        app->game.score, app->game.active_enemies,
        app->game.peak_enemies, app->record.best_score);
}

static void update_playing(ShooterApp *app, Uint32 delta_ms, Uint64 now)
{
    const bool move_left = app->input.key_left || app->input.joystick_left;
    const bool move_right = app->input.key_right || app->input.joystick_right;
    const bool move_up = app->input.key_up || app->input.joystick_up;
    const bool move_down = app->input.key_down || app->input.joystick_down;
    const bool fire = app->input.key_fire ||
                      app->input.joystick_buttons[0] ||
                      app->input.joystick_buttons[1];
    const int ship_width = sprite_width_or(&app->ship, 28);
    const int ship_height = sprite_height_or(&app->ship, 19);
    const int laser_width = sprite_width_or(&app->laser_sprite, 4);
    const int laser_height = sprite_height_or(&app->laser_sprite, 10);
    int dx_dir = (move_right ? 1 : 0) - (move_left ? 1 : 0);
    int dy_dir = (move_down ? 1 : 0) - (move_up ? 1 : 0);
    Sint32 step_fp = (Sint32)((PLAYER_SPEED_PX_PER_SEC * delta_ms *
                               FIXED_POINT_ONE) / 1000U);
    Sint32 min_player_y_fp;
    Sint32 max_player_y_fp;
    int player_x;
    int player_y;
    int laser_index;
    int enemy_index;

    app->game.engine_active = (dx_dir != 0 || dy_dir != 0);
    if (dx_dir != 0 && dy_dir != 0) {
        step_fp = (step_fp * DIAGONAL_FACTOR_FP) / FIXED_POINT_ONE;
    }
    app->game.player_x_fp += dx_dir * step_fp;
    app->game.player_y_fp += dy_dir * step_fp;

    if (app->game.player_x_fp < 2 * FIXED_POINT_ONE) {
        app->game.player_x_fp = 2 * FIXED_POINT_ONE;
    }
    if (app->game.player_x_fp > (SCREEN_WIDTH - ship_width - 2) * FIXED_POINT_ONE) {
        app->game.player_x_fp = (SCREEN_WIDTH - ship_width - 2) * FIXED_POINT_ONE;
    }
    min_player_y_fp = (PLAYFIELD_TOP + 22) * FIXED_POINT_ONE;
    max_player_y_fp = (SCREEN_HEIGHT - ship_height - 2) * FIXED_POINT_ONE;
    if (app->game.player_y_fp < min_player_y_fp) {
        app->game.player_y_fp = min_player_y_fp;
    }
    if (app->game.player_y_fp > max_player_y_fp) {
        app->game.player_y_fp = max_player_y_fp;
    }

    if (fire) {
        fire_laser(app, now);
    }

    for (enemy_index = 0; enemy_index < MAX_ENEMIES; ++enemy_index) {
        Enemy *enemy = &app->game.enemies[enemy_index];
        if (enemy->active) {
            update_single_enemy(app, enemy, delta_ms);
        }
    }

    for (laser_index = 0; laser_index < LASER_COUNT; ++laser_index) {
        Laser *laser = &app->game.lasers[laser_index];
        int laser_y;

        if (!laser->active) {
            continue;
        }
        laser->y_fp -= (Sint32)((LASER_SPEED_PX_PER_SEC * delta_ms *
                                 FIXED_POINT_ONE) / 1000U);
        laser_y = (int)(laser->y_fp / FIXED_POINT_ONE);
        if (laser_y + laser_height < PLAYFIELD_TOP) {
            laser->active = false;
            continue;
        }
        for (enemy_index = 0; enemy_index < MAX_ENEMIES; ++enemy_index) {
            Enemy *enemy = &app->game.enemies[enemy_index];
            const Sprite *ufo_sprite;
            int ufo_w;
            int ufo_h;
            int enemy_x;
            int enemy_y;

            if (!enemy->active) {
                continue;
            }
            enemy_y = (int)(enemy->y_fp / FIXED_POINT_ONE);
            ufo_sprite = ufo_sprite_for_pattern(app, enemy->pattern);
            ufo_h = sprite_height_or(ufo_sprite, 23);
            if (enemy_y + ufo_h <= PLAYFIELD_TOP ||
                enemy_y + ufo_h <= laser_y ||
                enemy_y >= laser_y + laser_height) {
                continue;
            }
            enemy_x = (int)(enemy->x_fp / FIXED_POINT_ONE);
            ufo_w = sprite_width_or(ufo_sprite, 23);
            if (sprites_collide(&app->laser_sprite, laser->x, laser_y,
                                laser_width, laser_height,
                                ufo_sprite, enemy_x, enemy_y,
                                ufo_w, ufo_h)) {
                laser->active = false;
                destroy_enemy_and_split(app, enemy);
                break;
            }
        }
    }

    flush_pending_spawns(app);

    player_x = (int)(app->game.player_x_fp / FIXED_POINT_ONE);
    player_y = (int)(app->game.player_y_fp / FIXED_POINT_ONE);
    for (enemy_index = 0; enemy_index < MAX_ENEMIES; ++enemy_index) {
        const Enemy *enemy = &app->game.enemies[enemy_index];
        const Sprite *ufo_sprite;
        int ufo_w;
        int ufo_h;
        int enemy_x;
        int enemy_y;

        if (!enemy->active) {
            continue;
        }
        enemy_y = (int)(enemy->y_fp / FIXED_POINT_ONE);
        ufo_sprite = ufo_sprite_for_pattern(app, enemy->pattern);
        ufo_h = sprite_height_or(ufo_sprite, 23);
        if (enemy_y + ufo_h <= player_y || enemy_y >= player_y + ship_height) {
            continue;
        }
        enemy_x = (int)(enemy->x_fp / FIXED_POINT_ONE);
        ufo_w = sprite_width_or(ufo_sprite, 23);
        if (sprites_collide(&app->ship, player_x, player_y,
                            ship_width, ship_height,
                            ufo_sprite, enemy_x, enemy_y,
                            ufo_w, ufo_h)) {
            trigger_player_impact(app, now);
            break;
        }
    }
}

static void update_simulation(ShooterApp *app, Uint32 delta_ms, Uint64 now)
{
    if (app->game.phase == PHASE_SPLASH) {
        if (now - app->game.phase_changed_ticks >= SPLASH_DURATION_MS) {
            app->game.phase = PHASE_TITLE;
            app->game.phase_changed_ticks = now;
        }
        app->input.fire_pressed_edge = false;
        return;
    }

    if (app->game.phase != PHASE_PAUSED &&
        app->game.phase != PHASE_CONFIRM_EXIT) {
        update_stars(&app->game, delta_ms);
        update_sparks(&app->game, delta_ms);
    }

    if (app->game.phase == PHASE_TITLE) {
        if (!app->input.editing_name && app->input.fire_pressed_edge) {
            start_new_run(app, now);
        }
    } else if (app->game.phase == PHASE_PLAYING) {
        update_playing(app, delta_ms, now);
    } else if (app->game.phase == PHASE_GAME_OVER) {
        if (!app->input.editing_name && app->input.fire_pressed_edge &&
            now - app->game.phase_changed_ticks >= GAME_OVER_GUARD_MS) {
            start_new_run(app, now);
        }
    }
    app->input.fire_pressed_edge = false;
}

/* -------------------------------------------------------------------------- */
/* Keyboard and call-sign text input handling.                                */
/* -------------------------------------------------------------------------- */
static void append_callsign_character(ShooterApp *app, unsigned char character)
{
    size_t length = strlen(app->settings.callsign);

    if (!is_callsign_character(character) || length + 1 >= CALLSIGN_CAPACITY) {
        return;
    }
    app->settings.callsign[length] = (char)toupper(character);
    app->settings.callsign[length + 1] = '\0';
}

static void remove_callsign_character(ShooterApp *app)
{
    size_t length = strlen(app->settings.callsign);
    if (length > 0) {
        app->settings.callsign[length - 1] = '\0';
    }
}

static void begin_name_editing(ShooterApp *app)
{
    app->input.editing_name = true;
    app->input.pending_fallback_valid = false;
    if (app->window) {
        (void)SDL_StartTextInput(app->window);
    }
    set_status(app, "TYPE PILOT NAME; ENTER SAVES", 3000);
}

static void finish_name_editing(ShooterApp *app, bool save)
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
        const bool cfg_ok = save_settings(app);
        if (app->game.phase == PHASE_GAME_OVER && app->game.new_record_achieved) {
            copy_callsign(app->record.callsign, app->settings.callsign);
            (void)save_score_record(app);
        }
        set_status(app, cfg_ok ? "PILOT SAVED" : "SAVE FAILED", 2200);
    }
}

static void handle_text_input(ShooterApp *app, const char *text)
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

static void adjust_master_volume(ShooterApp *app, int direction)
{
    char text[32];
    int volume = (int)app->settings.master_volume + direction * 16;

    if (volume < 0) volume = 0;
    if (volume > 255) volume = 255;
    app->settings.master_volume = (Uint8)volume;
    (void)snprintf(text, sizeof(text), "VOLUME %u%%",
                   ((unsigned int)app->settings.master_volume * 100U) / 255U);
    set_status(app, text, 1400);
}

static void handle_key_down(ShooterApp *app, const SDL_KeyboardEvent *key)
{
    const SDL_Keycode code = key->key;

    if (app->input.editing_name) {
        if (code == SDLK_ESCAPE) {
            finish_name_editing(app, false);
        } else if (code == SDLK_RETURN) {
            finish_name_editing(app, true);
        } else if (code == SDLK_BACKSPACE) {
            remove_callsign_character(app);
        } else if (!key->repeat && code <= 0x7fU) {
            const unsigned char character = (unsigned char)code;
            if (is_callsign_character(character)) {
                append_callsign_character(app, character);
                app->input.pending_fallback_char = (char)toupper(character);
                app->input.pending_fallback_ticks = SDL_GetTicks();
                app->input.pending_fallback_valid = true;
            }
        }
        return;
    }

    if (code == SDLK_LEFT || code == SDLK_A) app->input.key_left = true;
    if (code == SDLK_RIGHT || code == SDLK_D) app->input.key_right = true;
    if (code == SDLK_UP || code == SDLK_W) app->input.key_up = true;
    if (code == SDLK_DOWN || code == SDLK_S) app->input.key_down = true;
    if (code == SDLK_SPACE) {
        if (!key->repeat && app->game.phase != PHASE_CONFIRM_EXIT) {
            app->input.fire_pressed_edge = true;
        }
        app->input.key_fire = true;
    }

    if (key->repeat) {
        return;
    }

    if (app->game.phase == PHASE_CONFIRM_EXIT) {
        if (code == SDLK_Y) {
            enter_title_screen(app, SDL_GetTicks());
        } else if (code == SDLK_N || code == SDLK_ESCAPE) {
            app->game.phase = app->game.confirm_return_phase;
        }
        return;
    }

    if (code == SDLK_ESCAPE) {
        if (app->game.phase == PHASE_TITLE) {
            app->running = false;
        } else if (app->game.phase == PHASE_PLAYING ||
                   app->game.phase == PHASE_PAUSED) {
            app->game.confirm_return_phase = app->game.phase;
            app->game.phase = PHASE_CONFIRM_EXIT;
            app->game.engine_active = false;
        } else {
            enter_title_screen(app, SDL_GetTicks());
        }
        return;
    }

    if (code == SDLK_RETURN &&
        (app->game.phase == PHASE_TITLE || app->game.phase == PHASE_GAME_OVER)) {
        app->input.fire_pressed_edge = true;
    } else if (code == SDLK_P) {
        if (app->game.phase == PHASE_PLAYING) {
            app->game.phase = PHASE_PAUSED;
            app->game.engine_active = false;
        } else if (app->game.phase == PHASE_PAUSED) {
            app->game.phase = PHASE_PLAYING;
        }
    } else if (code == SDLK_T &&
               (app->game.phase == PHASE_TITLE ||
                app->game.phase == PHASE_GAME_OVER)) {
        begin_name_editing(app);
    } else if (code == SDLK_F2) {
        set_status(app, save_settings(app) ? "CONFIG SAVED" : "CONFIG SAVE FAILED", 2200);
    } else if (code == SDLK_F3) {
        set_status(app, load_settings(app) ? "CONFIG LOADED" : "CONFIG LOAD FAILED", 2200);
    } else if (code == SDLK_M) {
        app->settings.music_enabled = !app->settings.music_enabled;
        set_status(app, app->settings.music_enabled ? "MUSIC ON" : "MUSIC OFF", 1400);
    } else if (code == SDLK_MINUS || code == SDLK_KP_MINUS) {
        adjust_master_volume(app, -1);
    } else if (code == SDLK_PLUS || code == SDLK_EQUALS || code == SDLK_KP_PLUS) {
        adjust_master_volume(app, 1);
    }
}

static void handle_key_up(ShooterApp *app, const SDL_KeyboardEvent *key)
{
    const SDL_Keycode code = key->key;

    if (code == SDLK_LEFT || code == SDLK_A) app->input.key_left = false;
    if (code == SDLK_RIGHT || code == SDLK_D) app->input.key_right = false;
    if (code == SDLK_UP || code == SDLK_W) app->input.key_up = false;
    if (code == SDLK_DOWN || code == SDLK_S) app->input.key_down = false;
    if (code == SDLK_SPACE) app->input.key_fire = false;
}

static void handle_event(ShooterApp *app, const SDL_Event *event)
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
/* Rendering: fallback procedural shapes, full-screen playfield, and overlays.*/
/* -------------------------------------------------------------------------- */
static void draw_fallback_ship(SDL_Surface *surface, int x, int y)
{
    draw_line(surface, x + 14, y, x + 2, y + 18, COLOR_CYAN);
    draw_line(surface, x + 14, y, x + 26, y + 18, COLOR_CYAN);
    draw_line(surface, x + 2, y + 18, x + 26, y + 18, COLOR_BLUE);
    fill_rect(surface, x + 11, y + 7, 7, 7, COLOR_WHITE);
}

static void draw_fallback_ufo(SDL_Surface *surface, int x, int y, Uint8 ring_color)
{
    fill_rect(surface, x + 4, y + 3, 15, 15, COLOR_WHITE);
    fill_rect(surface, x + 6, y + 5, 11, 11, ring_color);
    fill_rect(surface, x + 8, y + 7, 7, 7, COLOR_DIM);
}

static void draw_fallback_laser(SDL_Surface *surface, int x, int y)
{
    fill_rect(surface, x + 1, y, 2, 10, COLOR_RED);
    put_pixel(surface, x, y + 2, COLOR_YELLOW);
    put_pixel(surface, x + 3, y + 2, COLOR_YELLOW);
}

static void draw_hud(const ShooterApp *app, Uint64 now)
{
    char left_text[40];
    char center_text[40];
    char right_text[40];

    fill_rect(app->canvas, 0, 0, SCREEN_WIDTH, HUD_HEIGHT - 1, COLOR_BLACK);
    draw_line(app->canvas, 0, HUD_HEIGHT - 1, SCREEN_WIDTH - 1, HUD_HEIGHT - 1,
              COLOR_BLUE);

    (void)snprintf(left_text, sizeof(left_text), "SCORE:%d", app->game.score);
    draw_text(app->canvas, 4, 2, left_text, COLOR_GREEN);

    if (app->status_text[0] && now < app->status_until_ticks) {
        draw_centered_text(app->canvas, 2, app->status_text, COLOR_PURPLE);
    } else {
        (void)snprintf(center_text, sizeof(center_text), "UFOS:%d",
                       app->game.active_enemies + app->game.pending_spawns);
        draw_centered_text(app->canvas, 2, center_text, COLOR_CYAN);
    }

    (void)snprintf(right_text, sizeof(right_text), "BEST:%d",
                   app->record.best_score);
    draw_text(app->canvas, SCREEN_WIDTH - measure_text_width(right_text) - 4,
              2, right_text, COLOR_YELLOW);
}

static void draw_overlay_box(SDL_Surface *canvas, int x, int y, int w, int h,
                             Uint8 border_color)
{
    fill_rect(canvas, x, y, w, h, COLOR_BLACK);
    draw_rect(canvas, x, y, w, h, border_color);
    draw_rect(canvas, x + 2, y + 2, w - 4, h - 4, COLOR_DIM);
}

static void draw_splash_screen(const ShooterApp *app)
{
    if (app->splash && app->splash->pixels &&
        app->canvas && app->canvas->pixels) {
        const int copy_w = (app->splash->w < SCREEN_WIDTH)
            ? app->splash->w : SCREEN_WIDTH;
        const int copy_h = (app->splash->h < SCREEN_HEIGHT)
            ? app->splash->h : SCREEN_HEIGHT;
        int row;

        if (copy_w < SCREEN_WIDTH || copy_h < SCREEN_HEIGHT) {
            clear_canvas(app->canvas);
        }
        for (row = 0; row < copy_h; ++row) {
            const Uint8 *src = (const Uint8 *)app->splash->pixels +
                               (row * app->splash->pitch);
            Uint8 *dst = (Uint8 *)app->canvas->pixels +
                         (row * app->canvas->pitch);
            memcpy(dst, src, (size_t)copy_w);
        }
    } else {
        clear_canvas(app->canvas);
        draw_centered_text(app->canvas, 86, "UFO", COLOR_YELLOW);
        draw_centered_text(app->canvas, 186, "@MMOROCA 2026", COLOR_YELLOW);
    }
}

static void draw_title_overlay(const ShooterApp *app, Uint64 now)
{
    char line[64];
    const bool blink = ((now / 350U) & 1U) != 0U;

    draw_overlay_box(app->canvas, 18, 22, 284, 146, COLOR_CYAN);
    draw_centered_text(app->canvas, 30, "How long will you survive?", COLOR_CYAN);
    draw_centered_text(app->canvas, 41, "1 UFO START -> +2 ON EACH HIT!", COLOR_ORANGE);

    draw_text(app->canvas, 36, 54, "BLUE:WAVE", COLOR_CYAN);
    draw_text(app->canvas, 166, 54, "YELLOW:ZIGZAG", COLOR_YELLOW);
    draw_text(app->canvas, 36, 64, "RED:DIVE", COLOR_RED);
    draw_text(app->canvas, 166, 64, "GREEN:DRIFT", COLOR_GREEN);

    (void)snprintf(line, sizeof(line), "BEST: %d BY %.8s (MAX %d)",
                   app->record.best_score, app->record.callsign,
                   app->record.best_peak_enemies);
    draw_centered_text(app->canvas, 78, line, COLOR_YELLOW);

    draw_pilot_line(app->canvas, 90, app->settings.callsign,
                    app->input.editing_name && blink);

    draw_centered_text(app->canvas, 105, "ARROWS/WASD MOVE  SPACE FIRE", COLOR_GRAY);
    draw_centered_text(app->canvas, 115, "P PAUSE  M MUSIC  -/+ VOL  T NAME", COLOR_GRAY);
    draw_centered_text(app->canvas, 125, "F2 SAVE CFG  F3 LOAD CFG  ESC QUIT", COLOR_GRAY);

    if (app->input.editing_name) {
        draw_centered_text(app->canvas, 145, "TYPE NAME - ENTER SAVES", COLOR_YELLOW);
    } else if (blink) {
        draw_centered_text(app->canvas, 145, "PRESS SPACE OR FIRE TO START", COLOR_WHITE);
    }
}

static void draw_game_over_overlay(const ShooterApp *app, Uint64 now)
{
    char line[64];
    const bool blink = ((now / 350U) & 1U) != 0U;
    const bool ready_to_restart =
        (now - app->game.phase_changed_ticks) >= GAME_OVER_GUARD_MS;

    draw_overlay_box(app->canvas, 30, 34, 260, 122,
                     app->game.new_record_achieved ? COLOR_YELLOW : COLOR_RED);
    draw_centered_text(app->canvas, 44,
                       app->game.new_record_achieved ? "IMPACT! NEW HIGH SCORE!" :
                                                       "SHIP IMPACT - GAME OVER",
                       app->game.new_record_achieved ? COLOR_YELLOW : COLOR_RED);

    (void)snprintf(line, sizeof(line), "SCORE: %d   PEAK UFOS: %d",
                   app->game.score, app->game.peak_enemies);
    draw_centered_text(app->canvas, 61, line, COLOR_WHITE);

    (void)snprintf(line, sizeof(line), "BEST: %d BY %.8s",
                   app->record.best_score, app->record.callsign);
    draw_centered_text(app->canvas, 73, line, COLOR_ORANGE);

    draw_pilot_line(app->canvas, 86, app->settings.callsign,
                    app->input.editing_name && blink);

    draw_centered_text(app->canvas, 102, "T EDIT PILOT   F2 SAVE CFG", COLOR_GRAY);
    if (app->input.editing_name) {
        draw_centered_text(app->canvas, 120, "ENTER SAVES PILOT NAME", COLOR_YELLOW);
    } else if (ready_to_restart && blink) {
        draw_centered_text(app->canvas, 120, "SPACE / FIRE: PLAY AGAIN", COLOR_CYAN);
    }
    draw_centered_text(app->canvas, 136, "ESC: MAIN MENU", COLOR_GRAY);
}

static bool present_frame(ShooterApp *app)
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

static bool draw_frame(ShooterApp *app, Uint64 now)
{
    const SDL_Rect playfield_clip = {
        0, PLAYFIELD_TOP, SCREEN_WIDTH, SCREEN_HEIGHT - PLAYFIELD_TOP
    };
    const int player_x = (int)(app->game.player_x_fp / FIXED_POINT_ONE);
    const int player_y = (int)(app->game.player_y_fp / FIXED_POINT_ONE);
    int index;

    if (app->game.phase == PHASE_SPLASH) {
        draw_splash_screen(app);
        return present_frame(app);
    }

    clear_canvas(app->canvas);

    for (index = 0; index < STAR_COUNT; ++index) {
        const Star *star = &app->game.stars[index];
        const Uint8 color = (star->twinkle == 0) ? COLOR_WHITE : star->color;
        put_pixel(app->canvas, star->x, star->y, color);
    }

    for (index = 0; index < LASER_COUNT; ++index) {
        const Laser *laser = &app->game.lasers[index];
        if (laser->active) {
            const int laser_y = (int)(laser->y_fp / FIXED_POINT_ONE);
            if (app->laser_sprite.baked) {
                draw_scaled_sprite(app->canvas, &app->laser_sprite,
                                   laser->x, laser_y, &playfield_clip);
            } else {
                draw_fallback_laser(app->canvas, laser->x, laser_y);
            }
        }
    }

    for (index = 0; index < MAX_ENEMIES; ++index) {
        const Enemy *enemy = &app->game.enemies[index];
        if (enemy->active) {
            const int enemy_x = (int)(enemy->x_fp / FIXED_POINT_ONE);
            const int enemy_y = (int)(enemy->y_fp / FIXED_POINT_ONE);
            const Sprite *ufo_sprite = ufo_sprite_for_pattern(app, enemy->pattern);
            if (ufo_sprite->baked) {
                draw_scaled_sprite(app->canvas, ufo_sprite,
                                   enemy_x, enemy_y, &playfield_clip);
            } else if (enemy_y + 20 > PLAYFIELD_TOP) {
                draw_fallback_ufo(app->canvas, enemy_x, enemy_y,
                                  ufo_accent_color(enemy->pattern));
            }
        }
    }

    if (app->game.phase != PHASE_GAME_OVER) {
        if (app->ship.baked) {
            draw_scaled_sprite(app->canvas, &app->ship,
                               player_x, player_y, &playfield_clip);
        } else {
            draw_fallback_ship(app->canvas, player_x, player_y);
        }
    }

    for (index = 0; index < SPARK_COUNT; ++index) {
        const Spark *spark = &app->game.sparks[index];
        if (spark->active) {
            const int sx = (int)(spark->x_fp / FIXED_POINT_ONE);
            const int sy = (int)(spark->y_fp / FIXED_POINT_ONE);
            if (sy >= PLAYFIELD_TOP) {
                fill_rect(app->canvas, sx, sy, 2, 2, spark->color);
            }
        }
    }

    draw_hud(app, now);

    if (app->game.phase == PHASE_TITLE) {
        draw_title_overlay(app, now);
    } else if (app->game.phase == PHASE_PAUSED) {
        draw_overlay_box(app->canvas, 96, 82, 128, 36, COLOR_CYAN);
        draw_centered_text(app->canvas, 92, "PAUSED", COLOR_YELLOW);
        draw_centered_text(app->canvas, 104, "PRESS P TO RESUME", COLOR_WHITE);
    } else if (app->game.phase == PHASE_CONFIRM_EXIT) {
        draw_overlay_box(app->canvas, 96, 82, 128, 36, COLOR_CYAN);
        draw_centered_text(app->canvas, 92, "PAUSED", COLOR_YELLOW);
        draw_centered_text(app->canvas, 104, "EXIT? Y / N", COLOR_WHITE);
    } else if (app->game.phase == PHASE_GAME_OVER) {
        draw_game_over_overlay(app, now);
    }

    return present_frame(app);
}

/* -------------------------------------------------------------------------- */
/* Main loop, palette fade-out, command-line help, and orderly shutdown.      */
/* -------------------------------------------------------------------------- */
static bool run_shooter(ShooterApp *app)
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

        update_simulation(app, delta_ms, now);
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

static void fade_out_before_exit(ShooterApp *app)
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

static void shutdown_shooter(ShooterApp *app)
{
    int i;

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

    destroy_sprite(&app->ship);
    for (i = 0; i < UFO_MODEL_COUNT; ++i) {
        destroy_sprite(&app->ufos[i]);
    }
    destroy_sprite(&app->laser_sprite);
    if (app->splash) {
        SDL_DestroySurface(app->splash);
        app->splash = NULL;
    }

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

static void print_usage(void)
{
    puts("Usage: SHOOTER.EXE [/L] [/H]");
    puts("");
    puts("Full-screen 320x200 SDL3/DOS arcade shooter.");
    puts("Displays SHOWCASE/UFO.BMP for 5 seconds at startup, then starts with");
    puts("1 descending UFO; each hit spawns 2 new UFOs (up to 256 simultaneous");
    puts("live enemies on screen) until one impacts your ship.");
    puts("");
    puts("Options:");
    puts("  /L, /LOG        Write startup/summary info to SHOOTER.LOG (off by default)");
    puts("  /H, /?          Show this help text");
    puts("");
    puts("UFO color <-> behavior:");
    puts("  Blue   (UFOBLUE.BMP)   Sinusoidal horizontal wave");
    puts("  Yellow (UFOYEL~1.BMP)  Diagonal zigzag switchbacks");
    puts("  Red    (UFORED.BMP)    Alternating fast dive bursts");
    puts("  Green  (UFOGREEN.BMP)  Steady diagonal drift + course shifts");
    puts("");
    puts("Controls:");
    puts("  Arrows / WASD   Move ship in 8 directions (joystick supported)");
    puts("  Space / Btn 1   Start run, fire laser, or play again after crash");
    puts("  P               Pause / resume");
    puts("  T               Edit pilot call sign on title / game-over screen");
    puts("  M, - / +        Toggle music and adjust master volume");
    puts("  F2, F3          Save SHOOTER.CFG, load SHOOTER.CFG");
    puts("  Esc             Skip splash / confirm exit (Y/N) / quit from menu");
}

int main(int argc, char **argv)
{
    ShooterApp app = { 0 };
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
    LOG("SHOOTER start");

    set_default_data(&app);
    (void)load_settings(&app);
    (void)load_score_record(&app);

    video_ready = initialize_video(&app);
    if (video_ready) {
        (void)load_splash_bitmap(&app, SPLASH_BMP);
        (void)load_sprite(&app, &app.ship, SHIP_BMP, "playerShip2_blue");
        (void)load_sprite(&app, &app.ufos[PATTERN_WAVE], UFO_BLUE_BMP, "ufoBlue");
        (void)load_sprite(&app, &app.ufos[PATTERN_ZIGZAG], UFO_YELLOW_BMP, "ufoYellow");
        (void)load_sprite(&app, &app.ufos[PATTERN_SWOOP], UFO_RED_BMP, "ufoRed");
        (void)load_sprite(&app, &app.ufos[PATTERN_DRIFT], UFO_GREEN_BMP, "ufoGreen");
        (void)load_sprite(&app, &app.laser_sprite, LASER_BMP, "laserRed02");
        initialize_joystick(&app);
        initialize_audio(&app);
        initialize_game(&app, SDL_GetTicks());
        app.palette.target_brightness = 16;
        app.palette.last_step_ticks = SDL_GetTicks();
        (void)save_settings(&app);
        run_ok = run_shooter(&app);
        fade_out_before_exit(&app);
        exit_status = run_ok ? 0 : 1;
    } else if (log_enabled) {
        fprintf(stderr, "SHOOTER could not initialize the display. See %s if writable.\n",
                LOG_FILE);
    } else {
        fprintf(stderr, "SHOOTER could not initialize the display. Run with /L to write %s.\n",
                LOG_FILE);
    }

    LOG("SHOOTER exit status=%d best=%d peak=%d",
        exit_status, app.record.best_score, app.record.best_peak_enemies);
    shutdown_shooter(&app);
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
    if (video_ready) {
#if defined(SDL_PLATFORM_DOS) || defined(__DJGPP__) || defined(__MSDOS__)
        /* CP437 / CP850 / CP858 code 0xAD (173) is inverted '!' in DOS. */
        puts("\xAD" "Gracias por jugar!");
#else
        puts("\xC2\xA1" "Gracias por jugar!");
#endif
        fflush(stdout);
    }
    return exit_status;
}
