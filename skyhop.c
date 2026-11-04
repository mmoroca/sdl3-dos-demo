#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "sdl3_splash.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fonts/font_7x7.h"
#include "fonts/font_10x10.h"

/* by @mmoroca & Arena.AI 2026 (http://arena.ai/) */
/*
 * SKYHOP: endless, automatically scrolling, one-way platform runner.
 * This is the independent 486+/DOSBox reference edition, NOT the 386 port.
 * 320x200 indexed software surfaces; sprites use ONE exact shared palette and
 * transparent index zero. All hot-path movement is integer fixed point.
 * Terrain is streamed ahead; no fixed world map or SDL renderer is required.
 *
 * Reading map (all mutable state is in Game):
 *   configuration and types -> optional profiler -> asset/audio setup ->
 *   scores and segment generation -> movement/collision -> drawing ->
 *   input/phase transitions -> main loop -> cleanup.
 * The game is a finite-state machine: TITLE -> PLAYING -> (PAUSED or loss);
 * a qualifying loss enters ENTER_NAME, otherwise GAME_OVER. Saving a name
 * returns to TITLE. See the Phase declaration and key_down() for the edges.
 *
 * Coordinate conventions: screen pixels are 320x200; segment X and hero X
 * refer to world/screen coordinates respectively. *_fp and camera_fp use
 * 24.8 fixed point (FP=256); velocities are pixels/second times FP; time
 * is integer milliseconds. Only PLAYING advances the simulation clock.
 * The SDL window owns its window surface; canvas owns a separate INDEX8
 * surface only if the driver cannot expose one directly. Game owns the audio
 * stream, copied sprites, scores, and optional /L log.
 */
/* Geometry, physics, generation, audio, UI layout, then timing constants.
 * Alter these alongside tests/skyhop_tests.c when making a new game. */
#define W 320
#define H 200
#define HUD_H 16
#define TILE 18
#define SKY_TILE 24
#define SKY_MID_Y 88
#define GROUND_Y 164
#define HERO_W 24
#define HERO_H 24
#define FEET_OFFSET 22
/* 8 fractional bits: one pixel == 256 fixed-point units. This avoids
 * per-frame floating-point work on older CPUs. */
#define FP 256
#define GRAVITY 560
#define JUMP_SPEED 230
/* World-relative velocity starts at zero and approaches a maximum. Releasing
 * a key applies ground/air friction; opposite input brakes more strongly. */
#define RUN_SPEED 125
#define GROUND_ACCEL 1700
#define GROUND_FRICTION 2200
#define GROUND_BRAKE 4200
#define AIR_ACCEL 1500
#define AIR_FRICTION 80
#define AIR_BRAKE 450
#define SEGMENTS_MAX 48
#define SPRITE_COUNT 21
#define SCORE_COUNT 10
#define SCORE_FILE "SKYHOP.SCO"
#define LOG_FILE "SKYHOP.LOG"
#define IMAGE_DIR "Images/PLATFORM/"
/* The camera scrolls automatically; platforms grow with speed to preserve
 * landing time. Six gap sizes are 1..MAX_GAP_TILES tiles. */
#define INITIAL_SCROLL 72
#define SPEED_UP_MS 167 /* about +6 px/s per second of active play */
#define MAX_GAP_TILES 6
#define AUDIO_RATE 11025
#define AUDIO_BLOCK 256
#define AUDIO_TARGET (AUDIO_BLOCK * 3)
#define TITLE_LAST_CONTROL_Y 79
#define SCORE_HEADING_Y 90
#define SCORE_ROW_OFFSET 12
#define SCORE_ROW_PITCH 8
#define TITLE_START_Y 191
/* Physics is substepped in <=8 ms pieces after clamping an unexpectedly
 * long loop delta to 64 ms. This is independent of the display pacing.
 * 16 ms is a ~60 Hz SOFT TARGET (1000/16=62.5), not a physics timestep,
 * maximum frame duration, or promise of 60 FPS. When work exceeds it, yield
 * with SDL_Delay(0) rather than adding another 16 ms to a slow frame. */
#define MAX_STEP_MS 8
#define PROFILE_WINDOW_MS 5000u /* aggregate in RAM; write at most every 5 s */
#define FRAME_TARGET_MS 16u

#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

typedef enum SpriteId {
    TREE, SKYBOT, SKYMID1, SKYMID2, SKYMID3, SKYMID4, SKYTOP,
    PLATEND, PLATBEG, PLATMID, HERO1, HERO2, BRANCH, MUSH,
    GRDBEG, GRDFIL1, GRDFIL2, GRDFEND, GRDTEND, GRDTBEG, GRDTMID
} SpriteId;

static const char *const sprite_files[SPRITE_COUNT] = {
    "TREE.BMP", "SKYBOT.BMP", "SKYMID1.BMP", "SKYMID2.BMP",
    "SKYMID3.BMP", "SKYMID4.BMP", "SKYTOP.BMP", "PLATEND.BMP",
    "PLATBEG.BMP", "PLATMID.BMP", "HERO1.BMP", "HERO2.BMP",
    "BRANCH.BMP", "MUSH.BMP", "GRDBEG.BMP", "GRDFIL1.BMP",
    "GRDFIL2.BMP", "GRDFEND.BMP", "GRDTEND.BMP", "GRDTBEG.BMP",
    "GRDTMID.BMP"
};

typedef struct Sprite {
    Uint8 pixels[SKY_TILE * SKY_TILE];
    int w, h;
} Sprite;

typedef struct Segment {
    int64_t x;         /* world coordinate; tile-aligned */
    int width;         /* multiple of 18; scales with camera speed */
    int y;             /* collision top, not the decorative sprite top */
    bool ground;
} Segment;

typedef struct Record {
    char name[9];
    uint64_t millis;
} Record;

/* Screen/logic states. Do not conflate these with profiler stages: a Phase
 * controls which inputs are accepted, whether physics/audio advance and what
 * draw() shows. Transitions are made by start_game(), lose(), key_down():
 *   TITLE:      Space/Enter -> PLAYING; Esc -> exit.
 *   PLAYING:    P -> PAUSED; Esc -> TITLE; fall -> ENTER_NAME or GAME_OVER.
 *   PAUSED:     P/Space -> PLAYING; Esc -> TITLE.
 *   ENTER_NAME: Enter/save -> TITLE; typed characters edit the name.
 *   GAME_OVER:  Enter/Space/Esc -> TITLE.
 * A fall qualifies for ENTER_NAME when fewer than 10 scores exist, or its
 * elapsed time beats the last Top 10 entry. SDL_EVENT_QUIT exits any phase. */
typedef enum Phase {
    TITLE,      /* Menu/Top 10; no world update, music on. */
    PLAYING,    /* Camera, physics and survival timer advance; draw every loop. */
    PAUSED,     /* Preserve run; no world update; music/effects paused. */
    ENTER_NAME, /* Qualified loss; edit name, then save and return to title. */
    GAME_OVER   /* Nonqualifying loss; wait for menu key. */
} Phase;

typedef struct AudioClip {
    Uint8 *samples;
    Uint32 length, position;
    bool active;
} AudioClip;

/* /L-only profiler. Phase is a game state; ProfileStage is a measured
 * segment of one loop/draw. DOS SDL_GetPerformanceCounter() uses the PIT.
 * Without /L no per-frame performance-counter calls or log writes occur. */
typedef enum ProfileStage {
    PROF_EVENTS, PROF_UPDATE, PROF_AUDIO, PROF_CLEAR, PROF_SKY,
    PROF_TERRAIN, PROF_UI, PROF_PRESENT, PROF_DELAY, PROF_FRAME, PROF_COUNT
} ProfileStage;

typedef struct Profile {
    Uint64 sum[PROF_COUNT], peak[PROF_COUNT], freq, started_ms;
    Uint64 raw_ms, simulated_ms; /* elapsed wall delta vs applied play delta */
    Uint32 frames, drawn, capped, work_over_16; /* loops, presents, >64, >16 */
    Phase phase; /* attribute this profiling window to one screen state */
} Profile;

/* Single owner of game state and resources; allocated with calloc in main().
 * Render dirtiness belongs to the *screen*, not the simulation: a static
 * screen need not be redrawn, but events and audio still run each loop. */
typedef struct Game {
    SDL_Window *window;
    SDL_Surface *screen; /* borrowed SDL window surface: never destroy it */
    SDL_Surface *canvas; /* screen, or owned INDEX8 fallback, released in main */
    Sprite sprites[SPRITE_COUNT]; /* BMPs copied into fixed-size byte arrays */
    SDL_Color colors[256];
    int white, dark, accent, gold, sky_top_color, sky_bottom_color;
    Segment segments[SEGMENTS_MAX]; /* bounded streaming world, not a level map */
    int count;
    int64_t camera_fp; /* 24.8 world X of the left screen edge */
    int32_t hero_x_fp, hero_y_fp, hero_vy_fp; /* screen X/Y, vertical px/s */
    int64_t hero_vx_fp; /* world velocity, 24.8; independent of camera speed */
    int air_heading;    /* latched -1/0/+1 until landing; no mid-air reversal */
    uint64_t elapsed_ms; /* play-only timer: camera speed and scores */
    uint32_t rng;        /* procedural level; visual hash does not change RNG */
    bool left, right, grounded, jump_held, running, render_dirty;
    Phase phase; /* sole authority for update, input, overlay and music */
    Record top[SCORE_COUNT];
    int top_count;
    char name[9];
    bool logging; /* set only by /L or /LOG */
    FILE *log;   /* written at phase changes / 5-second boundaries / exit */
    SDL_AudioStream *audio_stream;
    AudioClip music, jump, loss;
    bool audio_ready;
    char startup_error[384]; /* required asset failure, printed after SDL_Quit */
    Profile profile;
} Game;

/* ---------- Optional /L diagnostics (one summary per window/phase) ---------- */
static const char *profile_phase(Phase phase)
{
    static const char *const names[] = {
        "TITLE", "PLAYING", "PAUSED", "ENTER_NAME", "GAME_OVER"
    };
    return (unsigned)phase < ARRAY_LEN(names) ? names[phase] : "UNKNOWN";
}

static void profile_add(Game *g, ProfileStage stage, Uint64 start, Uint64 end)
{
    const Uint64 ticks = end - start;
    g->profile.sum[stage] += ticks;
    if (ticks > g->profile.peak[stage]) g->profile.peak[stage] = ticks;
}

static void profile_reset(Game *g, Phase phase, Uint64 now_ms)
{
    const Uint64 frequency = g->profile.freq;
    memset(&g->profile, 0, sizeof(g->profile));
    g->profile.freq = frequency;
    g->profile.phase = phase;
    g->profile.started_ms = now_ms;
}

/* Drawing stages are averaged per actual redraw; input, audio and delay per
 * loop. This matters on TITLE, which might be redrawn only once in 5 seconds. */
static void profile_stage_line(Game *g, ProfileStage stage, const char *label)
{
    const Profile *p = &g->profile;
    const Uint32 count = (stage >= PROF_CLEAR && stage <= PROF_PRESENT) ?
                         p->drawn : p->frames;
    const Uint64 avg_tenths = count ? p->sum[stage] * 10000u / (p->freq * count) : 0;
    const Uint64 peak_tenths = p->peak[stage] * 10000u / p->freq;
    fprintf(g->log, " %s=%llu.%01llu/%llu.%01llu", label,
            (unsigned long long)(avg_tenths / 10u),
            (unsigned long long)(avg_tenths % 10u),
            (unsigned long long)(peak_tenths / 10u),
            (unsigned long long)(peak_tenths % 10u));
}

static void profile_flush(Game *g, Uint64 now_ms)
{
    Profile *p = &g->profile;
    if (!g->log || !p->frames || !p->freq) return;
    const Uint64 span = now_ms > p->started_ms ? now_ms - p->started_ms : 1u;
    const Uint64 fps_tenths = ((Uint64)p->frames * 10000u + span / 2u) / span;
    const Uint64 draw_fps_tenths = ((Uint64)p->drawn * 10000u + span / 2u) / span;
    fprintf(g->log,
            "PERF phase=%s wall_ms=%llu frames=%lu fps=%llu.%01llu "
            "drawn=%lu draw_fps=%llu.%01llu "
            "raw_ms=%llu simulated_ms=%llu capped64=%lu work_gt16=%lu\n",
            profile_phase(p->phase), (unsigned long long)span,
            (unsigned long)p->frames,
            (unsigned long long)(fps_tenths / 10u),
            (unsigned long long)(fps_tenths % 10u),
            (unsigned long)p->drawn,
            (unsigned long long)(draw_fps_tenths / 10u),
            (unsigned long long)(draw_fps_tenths % 10u),
            (unsigned long long)p->raw_ms, (unsigned long long)p->simulated_ms,
            (unsigned long)p->capped, (unsigned long)p->work_over_16);
    fputs("PERF avg/max_ms:", g->log);
    profile_stage_line(g, PROF_EVENTS, "events");
    profile_stage_line(g, PROF_UPDATE, "physics");
    profile_stage_line(g, PROF_AUDIO, "audio");
    profile_stage_line(g, PROF_CLEAR, "clear");
    profile_stage_line(g, PROF_SKY, "sky");
    profile_stage_line(g, PROF_TERRAIN, "terrain");
    profile_stage_line(g, PROF_UI, "hud_menu");
    profile_stage_line(g, PROF_PRESENT, "present");
    profile_stage_line(g, PROF_DELAY, "delay");
    profile_stage_line(g, PROF_FRAME, "frame");
    fputc('\n', g->log);
    fflush(g->log); /* once per window or phase change, never per frame */
}

/* /L-only, flushed startup checkpoints. A graphics-mode failure may close
 * DOSBox before buffered stdio is written; these few writes locate the last
 * successful initialization step. Never call this in the frame loop. */
static void startup_note(Game *g, const char *step)
{
    if (g->log) {
        fprintf(g->log, "STARTUP %s\n", step);
        fflush(g->log);
    }
}

/* ---------- Reproducible world, palette and resource loading ---------- */
/* xorshift mutates game RNG for layout; hash_column is stateless so drawing
 * order cannot alter future gaps or decoration choices. */
static uint32_t random_next(Game *g)
{
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return g->rng = x ? x : 0xB316Fu;
}

static uint32_t hash_column(uint64_t n)
{
    uint32_t x = (uint32_t)n ^ (uint32_t)(n >> 32) ^ 0xC17A9B29u;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    return x ^ (x >> 16);
}

/* C rounds negative integer division toward zero; tiling needs floor() so
 * columns left of the origin align exactly like positive ones. */
static int64_t floor_div(int64_t n, int d)
{
    return n >= 0 ? n / d : -((-n + d - 1) / d);
}

static int max_i(int a, int b) { return a > b ? a : b; }
static int min_i(int a, int b) { return a < b ? a : b; }
static int64_t max_i64(int64_t a, int64_t b) { return a > b ? a : b; }

static int64_t approach_i64(int64_t value, int64_t target, int64_t delta)
{
    if (value < target) return value + (target - value < delta ? target - value : delta);
    if (value > target) return value - (value - target < delta ? value - target : delta);
    return value;
}

static int scroll_speed(const Game *g)
{
    /* About +6 pixels/second of speed each second, no gameplay cap. */
    const uint64_t increase = g->elapsed_ms / SPEED_UP_MS;
    if (increase > (uint64_t)(INT32_MAX - INITIAL_SCROLL)) {
        return INT32_MAX;
    }
    return INITIAL_SCROLL + (int)increase;
}

static int find_color(const Game *g, Uint8 r, Uint8 gr, Uint8 b)
{
    int index = 1, smallest = INT32_MAX;
    for (int i = 1; i < 256; ++i) {
        const int dr = (int)g->colors[i].r - r;
        const int dg = (int)g->colors[i].g - gr;
        const int db = (int)g->colors[i].b - b;
        const int distance = dr * dr + dg * dg + db * db;
        if (distance < smallest) {
            smallest = distance;
            index = i;
        }
    }
    return index;
}

/* Required game images are fatal, but the reason must survive graphics-mode
 * teardown even without /L: print to DOS text mode after SDL_Quit in main().
 * The optional log gets the same specific filename only when /L is enabled. */
static void required_sprite_error(Game *g, const char *filename, const char *reason)
{
    (void)snprintf(g->startup_error, sizeof(g->startup_error),
                   "cannot load required image %s%s: %s",
                   IMAGE_DIR, filename, reason);
    if (g->log) {
        fprintf(g->log, "STARTUP required_image_error=%s\n", g->startup_error);
        fflush(g->log);
    }
}

/* All 21 8-bit BMPs must use the same exact 256-color palette. Copy pixels
 * from pitch-strided SDL surfaces into compact Sprite arrays, then free BMP
 * surfaces: the blitter reads only these arrays during gameplay. Try the
 * executable's base path, then current working directory. */
static bool load_sprites(Game *g)
{
    const char *base = SDL_GetBasePath();
    for (int i = 0; i < SPRITE_COUNT; ++i) {
        char path[512];
        SDL_Surface *bmp;
        const int expected = i == SKYBOT || i == SKYTOP ||
                             (i >= SKYMID1 && i <= SKYMID4) ||
                             i == HERO1 || i == HERO2 ? 24 : 18;
        (void)snprintf(path, sizeof(path), "%s%s%s",
                       base ? base : "", IMAGE_DIR, sprite_files[i]);
        bmp = SDL_LoadBMP(path);
        if (!bmp) {
            (void)snprintf(path, sizeof(path), "%s%s", IMAGE_DIR, sprite_files[i]);
            bmp = SDL_LoadBMP(path);
        }
        if (!bmp) {
            required_sprite_error(g, sprite_files[i], SDL_GetError());
            return false;
        }
        SDL_Palette *palette = SDL_GetSurfacePalette(bmp);
        if (bmp->format != SDL_PIXELFORMAT_INDEX8 || bmp->w != expected ||
            bmp->h != expected || !palette || palette->ncolors != 256) {
            required_sprite_error(g, sprite_files[i],
                                  "expected an indexed 8-bit BMP with a 256-color palette and 18x18 or 24x24 dimensions");
            SDL_DestroySurface(bmp);
            return false;
        }
        if (i == 0) {
            memcpy(g->colors, palette->colors, sizeof(g->colors));
        } else if (memcmp(g->colors, palette->colors, sizeof(g->colors)) != 0) {
            required_sprite_error(g, sprite_files[i],
                                  "palette differs from TREE.BMP; regenerate/copy the matching set");
            SDL_DestroySurface(bmp);
            return false;
        }
        if (!SDL_LockSurface(bmp)) {
            required_sprite_error(g, sprite_files[i], SDL_GetError());
            SDL_DestroySurface(bmp);
            return false;
        }
        g->sprites[i].w = bmp->w;
        g->sprites[i].h = bmp->h;
        for (int y = 0; y < bmp->h; ++y) {
            memcpy(g->sprites[i].pixels + y * bmp->w,
                   (const Uint8 *)bmp->pixels + y * bmp->pitch, (size_t)bmp->w);
        }
        SDL_UnlockSurface(bmp);
        SDL_DestroySurface(bmp);
    }
    g->white = find_color(g, 255, 255, 255);
    g->dark = find_color(g, 67, 74, 95);
    g->accent = find_color(g, 54, 227, 119);
    g->gold = find_color(g, 244, 172, 102);
    /* Original SKYTOP and SKYBOT are solid tiles: use their palette indices. */
    g->sky_top_color = g->sprites[SKYTOP].pixels[0];
    g->sky_bottom_color = g->sprites[SKYBOT].pixels[0];
    return true;
}

/* OGG decoding is done offline; DOS mixes only U8 mono PCM at 11025 Hz.
 * Audio failure never prevents play on machines without a sound card. */
static bool load_audio_clip(AudioClip *clip, const char *filename)
{
    SDL_AudioSpec spec;
    Uint8 *samples = NULL;
    Uint32 length = 0;
    char path[512];
    const char *base = SDL_GetBasePath();
    (void)snprintf(path, sizeof(path), "%s%s%s", base ? base : "", IMAGE_DIR, filename);
    if (!SDL_LoadWAV(path, &spec, &samples, &length)) {
        (void)snprintf(path, sizeof(path), "%s%s", IMAGE_DIR, filename);
        if (!SDL_LoadWAV(path, &spec, &samples, &length)) {
            fprintf(stderr, "SKYHOP: audio unavailable %s: %s\n", filename, SDL_GetError());
            return false;
        }
    }
    if (spec.format != SDL_AUDIO_U8 || spec.channels != 1 ||
        spec.freq != AUDIO_RATE || length == 0) {
        fprintf(stderr, "SKYHOP: expected 11025 Hz U8 mono WAV: %s\n", filename);
        SDL_free(samples);
        return false;
    }
    clip->samples = samples;
    clip->length = length;
    return true;
}

static void initialize_audio(Game *g)
{
    const SDL_AudioSpec spec = { SDL_AUDIO_U8, 1, AUDIO_RATE };
    (void)load_audio_clip(&g->music, "MUSIC.WAV");
    (void)load_audio_clip(&g->jump, "JUMP.WAV");
    (void)load_audio_clip(&g->loss, "LOSE.WAV");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "SKYHOP: audio disabled: %s\n", SDL_GetError());
        return;
    }
    g->audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                 &spec, NULL, NULL);
    if (!g->audio_stream || !SDL_ResumeAudioStreamDevice(g->audio_stream)) {
        fprintf(stderr, "SKYHOP: audio device unavailable: %s\n", SDL_GetError());
        return;
    }
    g->audio_ready = true;
}

static void start_effect(AudioClip *clip)
{
    if (clip->samples) {
        clip->position = 0;
        clip->active = true;
    }
}

static int next_audio_sample(AudioClip *clip, bool loop, int volume)
{
    if (!clip->active || !clip->samples || !clip->length) return 0;
    if (clip->position >= clip->length) {
        if (!loop) {
            clip->active = false;
            return 0;
        }
        clip->position = 0;
    }
    return ((int)clip->samples[clip->position++] - 128) * volume / 255;
}

/* Feed small blocks only until the queued stream reaches AUDIO_TARGET.
 * TITLE/PLAYING loop music; PAUSED suspends effects without changing their
 * cursors. Other phases have no music, but the loss effect may finish. */
static void feed_audio(Game *g)
{
    Uint8 samples[AUDIO_BLOCK];
    int queued;
    if (!g->audio_ready) return;
    queued = SDL_GetAudioStreamQueued(g->audio_stream);
    if (queued < 0) { g->audio_ready = false; return; }
    while (queued < AUDIO_TARGET) {
        const bool music = g->phase == TITLE || g->phase == PLAYING;
        g->music.active = music && g->music.samples != NULL;
        for (int i = 0; i < AUDIO_BLOCK; ++i) {
            int mix = music ? next_audio_sample(&g->music, true, 95) : 0;
            if (g->phase != PAUSED) {
                mix += next_audio_sample(&g->jump, false, 205);
                mix += next_audio_sample(&g->loss, false, 205);
            }
            if (mix > 127) mix = 127;
            if (mix < -128) mix = -128;
            samples[i] = (Uint8)(mix + 128);
        }
        if (!SDL_PutAudioStreamData(g->audio_stream, samples, AUDIO_BLOCK)) {
            fprintf(stderr, "SKYHOP: audio queue failed: %s\n", SDL_GetError());
            g->audio_ready = false;
            return;
        }
        queued += AUDIO_BLOCK;
    }
}

static void shutdown_audio(Game *g)
{
    if (g->audio_stream) SDL_DestroyAudioStream(g->audio_stream);
    g->audio_stream = NULL;
    SDL_free(g->music.samples);
    SDL_free(g->jump.samples);
    SDL_free(g->loss.samples);
    g->music.samples = g->jump.samples = g->loss.samples = NULL;
}

static bool apply_palette(SDL_Surface *surface, const SDL_Color *colors)
{
    SDL_Palette *palette = SDL_GetSurfacePalette(surface);
    if (!palette) {
        palette = SDL_CreateSurfacePalette(surface);
    }
    return palette && SDL_SetPaletteColors(palette, colors, 0, 256);
}

/* INDEX8 screen/canvas may be the same DOS window surface. Otherwise draw
 * into an INDEX8 canvas and blit on present. The direct-framebuffer hint is
 * a preference, not a guarantee: always inspect the actual screen format. */
static bool initialize(Game *g)
{
    SDL_PropertiesID props;
    startup_note(g, "video_init_begin");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SKYHOP: SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    startup_note(g, "video_init_ok");
    (void)SDL_SetHint(SDL_HINT_DOS_ALLOW_DIRECT_FRAMEBUFFER, "1");
    startup_note(g, "sprites_begin");
    if (!load_sprites(g)) return false;
    startup_note(g, "sprites_ok");
    props = SDL_CreateProperties();
    if (!props) return false;
    (void)SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "SKYHOP");
    (void)SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, W);
    (void)SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, H);
    (void)SDL_SetNumberProperty(props, "SDL_PIXELFORMAT", SDL_PIXELFORMAT_INDEX8);
    g->window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!g->window) return false;
    startup_note(g, "window_ok");
    g->screen = SDL_GetWindowSurface(g->window);
    if (!g->screen) return false;
    if (g->screen->format == SDL_PIXELFORMAT_INDEX8) {
        g->canvas = g->screen;
    } else {
        g->canvas = SDL_CreateSurface(W, H, SDL_PIXELFORMAT_INDEX8);
        if (!g->canvas) return false;
    }
    if (!apply_palette(g->canvas, g->colors) ||
        (g->screen != g->canvas && g->screen->format == SDL_PIXELFORMAT_INDEX8 &&
         !apply_palette(g->screen, g->colors))) return false;
    if (g->log) {
        fprintf(g->log, "STARTUP surface=%s shared=%d pitch=%d\n",
                SDL_GetPixelFormatName(g->screen->format),
                g->screen == g->canvas, g->canvas->pitch);
        fflush(g->log);
    }
    startup_note(g, "audio_begin");
    initialize_audio(g);
    startup_note(g, g->audio_ready ? "audio_ok" : "audio_unavailable");
    return true;
}

/* ---------- Indexed software drawing (caller locks canvas in draw()) ---------- */
static void put_pixel(SDL_Surface *s, int x, int y, Uint8 color)
{
    if (x >= 0 && x < W && y >= 0 && y < H) {
        ((Uint8 *)s->pixels)[y * s->pitch + x] = color;
    }
}

static void rect(SDL_Surface *s, int x, int y, int w, int h, Uint8 color)
{
    const int l = max_i(0, x), r = min_i(W, x + w);
    const int t = max_i(0, y), b = min_i(H, y + h);
    for (int row = t; row < b; ++row) {
        if (r > l) memset((Uint8 *)s->pixels + row * s->pitch + l, color, (size_t)(r - l));
    }
}

/* Index zero is transparent; clip each sprite at the screen boundaries.
 * Using surface->pitch matters even when the current DOS mode uses 320. */
static void sprite(Game *g, SpriteId id, int x, int y)
{
    const Sprite *s = &g->sprites[id];
    for (int sy = max_i(0, -y); sy < min_i(s->h, H - y); ++sy) {
        const Uint8 *src = s->pixels + sy * s->w;
        Uint8 *dst = (Uint8 *)g->canvas->pixels + (y + sy) * g->canvas->pitch;
        for (int sx = max_i(0, -x); sx < min_i(s->w, W - x); ++sx) {
            if (src[sx]) dst[x + sx] = src[sx];
        }
    }
}

static const Glyph *glyph(const Glyph *font, size_t count, unsigned char letter)
{
    for (size_t i = 0; i < count; ++i) {
        if (font[i].codepoint == letter) return &font[i];
    }
    return NULL;
}

static void text(Game *g, const char *message, int x, int y, int size, Uint8 color)
{
    const Glyph *font = size == 10 ? font_10x10 : font_7x7;
    const size_t count = size == 10 ? ARRAY_LEN(font_10x10) : ARRAY_LEN(font_7x7);
    while (*message) {
        const Glyph *c = glyph(font, count, (unsigned char)*message++);
        if (c) {
            for (int row = 0; row < size; ++row) {
                for (int col = 0; col < size; ++col) {
                    if (c->rows[row] & (1u << (size - 1 - col))) {
                        put_pixel(g->canvas, x + col, y + row, color);
                    }
                }
            }
        }
        x += size + 1;
    }
}

static void centered(Game *g, const char *message, int y, int size, Uint8 color)
{
    const int width = (int)strlen(message) * (size + 1) - 1;
    text(g, message, (W - width) / 2, y, size, color);
}

static void elapsed_label(char result[24], uint64_t elapsed_ms)
{
    unsigned long long tenths = (unsigned long long)(elapsed_ms / 100u);
    if (tenths > 999999u) tenths = 999999u;
    (void)snprintf(result, 24, "%llu.%01lluS", tenths / 10u, tenths % 10u);
}

/* ---------- Local Top 10; plain text name + integer milliseconds ---------- */
/* A missing score file is normal. Reject invalid names/times instead of
 * trusting external input. Only save_score() writes SKYHOP.SCO. */
static void load_scores(Game *g)
{
    FILE *f = fopen(SCORE_FILE, "r");
    if (!f) return;
    for (int i = 0; i < SCORE_COUNT; ++i) {
        char name[32], number[32], line[96], *end;
        unsigned long long ms;
        if (!fgets(line, sizeof(line), f)) break;
        if (sscanf(line, "%31s %31s", name, number) != 2 || strlen(name) > 8) continue;
        ms = strtoull(number, &end, 10);
        if (*end || ms == 0) continue;
        bool valid = true;
        for (size_t j = 0; name[j]; ++j) {
            if (!((name[j] >= 'A' && name[j] <= 'Z') ||
                  (name[j] >= '0' && name[j] <= '9'))) valid = false;
        }
        if (!valid) continue;
        strcpy(g->top[g->top_count].name, name);
        g->top[g->top_count++].millis = (uint64_t)ms;
    }
    fclose(f);
}

/* A tie with the last stored time does not qualify for a full leaderboard. */
static bool qualifies(const Game *g)
{
    return g->elapsed_ms > 0 &&
           (g->top_count < SCORE_COUNT || g->elapsed_ms > g->top[SCORE_COUNT - 1].millis);
}

/* Keep records sorted longest-first. Empty name defaults to PLAYER; the
 * display's 999.9S cap never truncates stored ranking precision. */
static void save_score(Game *g)
{
    Record r;
    FILE *f;
    int i = 0;
    (void)snprintf(r.name, sizeof(r.name), "%s", g->name[0] ? g->name : "PLAYER");
    r.millis = g->elapsed_ms;
    while (i < g->top_count && g->top[i].millis >= r.millis) ++i;
    for (int j = min_i(g->top_count, SCORE_COUNT - 1); j > i; --j) g->top[j] = g->top[j - 1];
    if (i < SCORE_COUNT) g->top[i] = r;
    if (g->top_count < SCORE_COUNT) ++g->top_count;
    f = fopen(SCORE_FILE, "w");
    if (!f) {
        fprintf(stderr, "SKYHOP: could not write %s\n", SCORE_FILE);
        return;
    }
    for (int j = 0; j < g->top_count; ++j) {
        fprintf(f, "%s %llu\n", g->top[j].name,
                (unsigned long long)g->top[j].millis);
    }
    fclose(f);
    if (g->log) fprintf(g->log, "New record %s %llu ms\n", r.name,
                         (unsigned long long)r.millis);
}

/* ---------- Streamed procedural platforms and play-only simulation ---------- */
/* Six possible gap widths (18..108px), with a vertical difference <=36px.
 * A player can jump and steer onto the next segment; regression tests exercise
 * 2,000 generated pairs with the actual fixed-point physics. Segment lengths
 * scale with camera speed so faster scrolling does not shorten landing time.
 */
static void append_segment(Game *g)
{
    const Segment *prev = &g->segments[g->count - 1];
    Segment s;
    const int speed = scroll_speed(g);
    const int gap = TILE * (1 + (int)(random_next(g) % MAX_GAP_TILES));
    const int motion = ((int)(random_next(g) % 5u) - 2) * TILE;
    int y = prev->y + motion;
    int width;
    if (y < 92) y = 92;
    if (y > GROUND_Y) y = GROUND_Y;
    if (y > 146 && (random_next(g) & 3u) == 0u) y = GROUND_Y;
    /* Avoid overflow in any one platform; an extreme speed is impractical. */
    width = speed > 1000000 ? 1200000 : max_i(108, (speed * 6) / 5);
    width = ((width + TILE - 1) / TILE) * TILE;
    s.x = prev->x + prev->width + gap;
    s.width = width;
    s.y = y;
    s.ground = y == GROUND_Y;
    g->segments[g->count++] = s;
}

/* Recycle platforms that have left the camera and fill to 250 px past the
 * right edge. Arrays are bounded by SEGMENTS_MAX; nothing is saved to disk. */
static void generate_ahead(Game *g)
{
    const int64_t camera = g->camera_fp / FP;
    while (g->count > 1 && g->segments[0].x + g->segments[0].width < camera - 100) {
        memmove(g->segments, g->segments + 1, (size_t)(--g->count) * sizeof(Segment));
    }
    while (g->count < SEGMENTS_MAX) {
        const Segment *last = &g->segments[g->count - 1];
        if (last->x + last->width >= camera + W + 250) break;
        append_segment(g);
    }
}

/* Enter PLAYING with a safe introductory floor and a clean active timer;
 * old scores and loaded assets intentionally survive between runs. */
static void start_game(Game *g)
{
    memset(g->segments, 0, sizeof(g->segments));
    g->count = 1;
    g->segments[0] = (Segment){ -360, 630, GROUND_Y, true };
    g->camera_fp = 0;
    g->hero_x_fp = 66 * FP;
    g->hero_y_fp = (GROUND_Y - FEET_OFFSET) * FP;
    g->hero_vx_fp = g->hero_vy_fp = 0;
    g->air_heading = 0;
    g->elapsed_ms = 0;
    g->left = g->right = g->jump_held = false;
    g->grounded = true;
    g->phase = PLAYING;
    g->render_dirty = true;
    g->jump.active = g->loss.active = false;
    generate_ahead(g);
}

/* Loss is triggered by falling below the view, NOT simply by touching the
 * left border. Route to name entry only for a Top 10 result. */
static void lose(Game *g)
{
    g->left = g->right = g->jump_held = false;
    g->phase = qualifies(g) ? ENTER_NAME : GAME_OVER;
    g->render_dirty = true;
    g->name[0] = 0;
    g->jump.active = false;
    start_effect(&g->loss);
    if (g->log) fprintf(g->log, "Game over after %llu ms at speed %d\n",
                        (unsigned long long)g->elapsed_ms, scroll_speed(g));
}

/* One <=MAX_STEP_MS physics step, only while PLAYING. Camera motion and
 * horizontal hero velocity are separate: idle hero stays at a fixed world X
 * while camera scrolls it left on screen. Controls change world velocity,
 * not camera speed. All positions/collision checks use fixed-point units. */
static void simulate_step(Game *g, int ms)
{
    const int speed = scroll_speed(g);
    const int direction = (g->right ? 1 : 0) - (g->left ? 1 : 0);
    const int32_t before_x = g->hero_x_fp;
    const int32_t before_y = g->hero_y_fp;
    const int64_t before_camera = g->camera_fp;
    const int64_t camera_step = (int64_t)speed * FP * ms / 1000;
    /* World-relative speed starts at zero, approaches a maximum while a
     * direction is held and decays on release. Ground release brakes fast;
     * air release brakes slowly. Opposite input brakes much harder in both
     * states; a jump cannot reverse its latched heading before landing. */
    if (g->grounded) {
        const int64_t target = direction > 0 ? ((int64_t)speed + RUN_SPEED) * FP :
                               direction < 0 ? -(int64_t)RUN_SPEED * FP : 0;
        const bool reversing = (g->hero_vx_fp > 0 && direction < 0) ||
                               (g->hero_vx_fp < 0 && direction > 0);
        const int64_t rate = direction == 0 ?
            max_i64(GROUND_FRICTION, ((int64_t)speed + RUN_SPEED) * 12) :
            reversing ? max_i64(GROUND_BRAKE,
                                  ((int64_t)speed + RUN_SPEED) * 24) :
                        max_i64(GROUND_ACCEL,
                                  ((int64_t)speed + RUN_SPEED) * 9);
        g->hero_vx_fp = approach_i64(g->hero_vx_fp, target,
                                      rate * FP * ms / 1000);
    } else {
        if (!g->air_heading && direction) g->air_heading = direction;
        if (!direction) {
            const int64_t rate = max_i64(AIR_FRICTION,
                                          ((int64_t)speed + RUN_SPEED) / 5);
            g->hero_vx_fp = approach_i64(g->hero_vx_fp, 0,
                                          rate * FP * ms / 1000);
        } else if (direction == -g->air_heading) {
            const int64_t rate = max_i64(AIR_BRAKE,
                                          ((int64_t)speed + RUN_SPEED) * 2);
            g->hero_vx_fp = approach_i64(g->hero_vx_fp, 0,
                                          rate * FP * ms / 1000);
        } else if (direction == g->air_heading) {
            const int64_t target = g->air_heading > 0 ?
                ((int64_t)speed + RUN_SPEED) * FP : -(int64_t)RUN_SPEED * FP;
            const int64_t rate = max_i64(AIR_ACCEL,
                                          ((int64_t)speed + RUN_SPEED) * 9);
            g->hero_vx_fp = approach_i64(g->hero_vx_fp, target,
                                          rate * FP * ms / 1000);
        }
    }
    /* Subtract camera movement in screen coordinates. Clip before narrowing
     * to int32 so even a very late run cannot overflow character position. */
    int64_t screen_x = (int64_t)g->hero_x_fp + g->hero_vx_fp * ms / 1000 - camera_step;
    if (screen_x < 3 * FP) screen_x = 3 * FP;
    if (screen_x > (W - HERO_W - 3) * FP) screen_x = (W - HERO_W - 3) * FP;
    g->hero_x_fp = (int32_t)screen_x;
    g->camera_fp += camera_step;
    g->elapsed_ms += (unsigned)ms;

    if (!g->grounded) {
        /* On descent, test whether the feet crossed a segment's top.
         * Interpolate X at that crossing to avoid missing narrow landings;
         * decorations are never considered solid. */
        g->hero_vy_fp += (int32_t)((int64_t)GRAVITY * FP * ms / 1000);
        g->hero_y_fp += (int32_t)((int64_t)g->hero_vy_fp * ms / 1000);
        if (g->hero_vy_fp >= 0) {
            const int32_t old_feet = before_y + FEET_OFFSET * FP;
            const int32_t new_feet = g->hero_y_fp + FEET_OFFSET * FP;
            if (new_feet > old_feet) {
                for (int i = 0; i < g->count; ++i) {
                    const Segment *s = &g->segments[i];
                    const int32_t top = s->y * FP;
                    if (old_feet > top || new_feet < top) continue;
                    const int64_t old_world_x = before_camera + before_x;
                    const int64_t new_world_x = g->camera_fp + g->hero_x_fp;
                    const int64_t at_landing = old_world_x +
                        (new_world_x - old_world_x) * (top - old_feet) / (new_feet - old_feet);
                    if (at_landing + 19 * FP > s->x * FP &&
                        at_landing + 5 * FP < (s->x + s->width) * FP) {
                        g->hero_y_fp = (s->y - FEET_OFFSET) * FP;
                        g->hero_vy_fp = 0;
                        g->grounded = true;
                        g->air_heading = 0;
                        break;
                    }
                }
            }
        }
    } else {
        /* Walking off an edge is immediate, even without pressing Jump. */
        const int64_t left = g->camera_fp + g->hero_x_fp + 5 * FP;
        const int64_t right = g->camera_fp + g->hero_x_fp + 19 * FP;
        const int feet = g->hero_y_fp / FP + FEET_OFFSET;
        bool supported = false;
        for (int i = 0; i < g->count; ++i) {
            const Segment *s = &g->segments[i];
            if (feet == s->y && right > s->x * FP && left < (s->x + s->width) * FP) {
                supported = true;
                break;
            }
        }
        if (!supported) {
            g->grounded = false;
            g->air_heading = g->hero_vx_fp > 0 ? 1 :
                             g->hero_vx_fp < 0 ? -1 : 0;
        }
    }
    generate_ahead(g);
    if (g->hero_y_fp > (H + 32) * FP) lose(g);
}

/* Consumes the clamped loop delta in small slices. Paused/title/name-over
 * screens do not accumulate time to catch up when a run resumes. */
static void update(Game *g, Uint32 milliseconds)
{
    if (g->phase != PLAYING) return;
    /* Small substeps prevent tunnelling through one-tile platforms. */
    while (milliseconds && g->phase == PLAYING) {
        const int step = (int)min_i((int)milliseconds, MAX_STEP_MS);
        simulate_step(g, step);
        milliseconds -= (Uint32)step;
    }
}

/* ---------- Render layers: two solid bands -> parallax -> tiles/hero -> UI ---------- */
/* Select one stable variant per world column. Stateless hash avoids using
 * random_next(), which would change gameplay simply by drawing more frames. */
static void draw_sky(Game *g)
{
    /* Only the 24px middle strip needs sprite work. The solid top/bottom
     * tiles are filled as two matching-color bands in draw(). */
    const int64_t offset = (g->camera_fp / FP) / 4; /* slower 1:4 parallax */
    const int64_t column = floor_div(offset, SKY_TILE);
    const int shift = (int)(offset - column * SKY_TILE);
    for (int k = -1; k < W / SKY_TILE + 2; ++k) {
        const int x = k * SKY_TILE - shift;
        const int64_t column_id = column + k;
        const SpriteId middle = SKYMID1 + (hash_column((uint64_t)column_id) % 4u);
        sprite(g, middle, x, SKY_MID_Y);
    }
}

/* Draw only visible tiles, not the full potentially wide segment; the
 * Segment top/width alone controls collisions, not the decorative sprites. */
static void draw_segment(Game *g, const Segment *s)
{
    const int64_t camera = g->camera_fp / FP;
    const int64_t tile_start = floor_div(camera - s->x, TILE) - 1;
    const int64_t tile_end = floor_div(camera + W - s->x, TILE) + 1;
    const int tiles = s->width / TILE;
    for (int64_t t = tile_start < 0 ? 0 : tile_start;
         t < tiles && t <= tile_end; ++t) {
        const int x = (int)(s->x + t * TILE - camera);
        if (x < -TILE || x >= W) continue;
        SpriteId top = s->ground ? GRDTMID : PLATMID;
        if (t == 0) top = s->ground ? GRDTBEG : PLATBEG;
        if (t == tiles - 1) top = s->ground ? GRDTEND : PLATEND;
        sprite(g, top, x, s->y);
        if (s->ground) {
            for (int y = s->y + TILE; y < H; y += TILE) {
                SpriteId fill = GRDFIL1 + (hash_column((uint64_t)(s->x / TILE + t + y)) % 2u);
                if (t == 0) fill = GRDBEG;
                if (t == tiles - 1) fill = GRDFEND;
                sprite(g, fill, x, y);
            }
        }
        /* Decorative tiles have no physical collision geometry. */
        if (t > 1 && t + 2 < tiles) {
            const uint32_t h = hash_column((uint64_t)(s->x / TILE + t));
            if (h % 11u == 0u) sprite(g, TREE, x, s->y - TILE);
            else if (h % 7u == 0u) sprite(g, MUSH, x, s->y - TILE);
            else if (h % 13u == 0u) sprite(g, BRANCH, x, s->y - TILE);
        }
    }
}

static void draw_hud(Game *g)
{
    char timer[24], time_text[40], speed_text[40];
    elapsed_label(timer, g->elapsed_ms);
    (void)snprintf(time_text, sizeof(time_text), "TIME %s", timer);
    (void)snprintf(speed_text, sizeof(speed_text), "SPD %d", scroll_speed(g));
    rect(g->canvas, 0, 0, W, HUD_H, (Uint8)g->dark);
    text(g, "SKYHOP", 5, 4, 7, (Uint8)g->accent);
    text(g, time_text, 84, 4, 7, (Uint8)g->white);
    text(g, speed_text, 228, 4, 7, (Uint8)g->white);
}

/* Every Top 10 entry has identical columns: 2 rank, 1 space, 8 name,
 * 1 space, 3 seconds + decimal + 1 tenth + S = 18 glyph cells. Storage keeps
 * the actual milliseconds; only the six-character on-screen time saturates. */
static void format_score_row(char row[32], int rank, const Record *record)
{
    uint64_t tenths = record->millis / 100u;
    if (tenths > 9999u) tenths = 9999u;
    (void)snprintf(row, 32, "%2d %-8.8s %03llu.%01lluS", rank, record->name,
                   (unsigned long long)(tenths / 10u),
                   (unsigned long long)(tenths % 10u));
}

static void draw_scores(Game *g, int y)
{
    centered(g, "TOP 10", y, 7, (Uint8)g->accent);
    for (int i = 0; i < g->top_count && i < SCORE_COUNT; ++i) {
        char row[32];
        format_score_row(row, i + 1, &g->top[i]);
        centered(g, row, y + SCORE_ROW_OFFSET + i * SCORE_ROW_PITCH,
                 7, (Uint8)g->white);
    }
}

/* Present once after every active-play loop, or only when a static screen
 * becomes dirty. TITLE covers most scenery; pause/loss overlays retain the
 * frozen world. Separate canvas needs one extra SDL_BlitSurface(). */
static bool draw(Game *g)
{
    Uint64 mark = g->log ? SDL_GetPerformanceCounter() : 0;
    Uint64 done;
    if (!SDL_LockSurface(g->canvas)) return false;
    /* Clear all 64,000 pixels each drawn frame to avoid trails. The bands
     * meet behind the opaque middle strip at the screen's midpoint. */
    rect(g->canvas, 0, 0, W, SKY_MID_Y + SKY_TILE / 2,
         (Uint8)g->sky_top_color);
    rect(g->canvas, 0, SKY_MID_Y + SKY_TILE / 2, W,
         H - SKY_MID_Y - SKY_TILE / 2, (Uint8)g->sky_bottom_color);
    if (g->log) {
        done = SDL_GetPerformanceCounter();
        profile_add(g, PROF_CLEAR, mark, done);
        mark = done;
    }
    draw_sky(g);
    if (g->log) {
        done = SDL_GetPerformanceCounter();
        profile_add(g, PROF_SKY, mark, done);
        mark = done;
    }
    for (int i = 0; i < g->count; ++i) draw_segment(g, &g->segments[i]);
    if (g->phase != TITLE) {
        sprite(g, (g->grounded && g->hero_vx_fp != 0 && (g->elapsed_ms / 140u) % 2u)
               ? HERO2 : HERO1, g->hero_x_fp / FP, g->hero_y_fp / FP);
    }
    if (g->log) {
        done = SDL_GetPerformanceCounter();
        profile_add(g, PROF_TERRAIN, mark, done);
        mark = done;
    }
    draw_hud(g);
    if (g->phase == TITLE) {
        rect(g->canvas, 21, 20, 278, 180, (Uint8)g->dark);
        centered(g, "SKYHOP", 24, 10, (Uint8)g->white);
        centered(g, "ENDLESS PLATFORM RUN", 45, 7, (Uint8)g->gold);
        centered(g, "A/D OR ARROWS: MOVE", 59, 7, (Uint8)g->white);
        centered(g, "SPACE/W/UP: JUMP", 69, 7, (Uint8)g->white);
        centered(g, "P: PAUSE  ESC: QUIT", TITLE_LAST_CONTROL_Y, 7, (Uint8)g->white);
        draw_scores(g, SCORE_HEADING_Y);
        centered(g, "SPACE/ENTER: START", TITLE_START_Y, 7, (Uint8)g->gold);
    } else if (g->phase == PAUSED) {
        rect(g->canvas, 50, 75, 220, 51, (Uint8)g->dark);
        centered(g, "PAUSED", 82, 10, (Uint8)g->white);
        centered(g, "P/SPACE: RESUME", 109, 7, (Uint8)g->gold);
    } else if (g->phase == ENTER_NAME || g->phase == GAME_OVER) {
        char timer[24], line[32];
        elapsed_label(timer, g->elapsed_ms);
        rect(g->canvas, 30, 57, 260, 95, (Uint8)g->dark);
        centered(g, "GAME OVER", 66, 10, (Uint8)g->white);
        (void)snprintf(line, sizeof(line), "TIME %s", timer);
        centered(g, line, 91, 7, (Uint8)g->gold);
        if (g->phase == ENTER_NAME) {
            (void)snprintf(line, sizeof(line), "NAME %.8s_", g->name);
            centered(g, line, 109, 7, (Uint8)g->white);
            centered(g, "ENTER: SAVE  BACKSPACE: EDIT", 133, 7, (Uint8)g->accent);
        } else {
            centered(g, "ENTER: MENU", 112, 7, (Uint8)g->accent);
        }
    }
    if (g->log) {
        done = SDL_GetPerformanceCounter();
        profile_add(g, PROF_UI, mark, done);
        mark = done;
    }
    SDL_UnlockSurface(g->canvas);
    if (g->screen != g->canvas && !SDL_BlitSurface(g->canvas, NULL, g->screen, NULL))
        return false;
    const bool presented = SDL_UpdateWindowSurface(g->window);
    if (g->log) profile_add(g, PROF_PRESENT, mark, SDL_GetPerformanceCounter());
    return presented;
}

/* ---------- Game phase transitions, keyboard and the main loop ---------- */
/* Each state accepts only its own controls: name editing, menu, loss,
 * pause/resume, then play movement. Jump requires a fresh keypress and ground
 * contact; its air heading is latched until the next landing. */
static void key_down(Game *g, SDL_Keycode key, bool repeat)
{
    if (g->phase == ENTER_NAME) {
        const size_t len = strlen(g->name);
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            save_score(g);
            g->phase = TITLE;
        } else if (key == SDLK_BACKSPACE && len) {
            g->name[len - 1] = '\0';
        } else if (len < 8) {
            int c = (int)key;
            if (c >= 'a' && c <= 'z') c = toupper(c);
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
                g->name[len] = (char)c;
                g->name[len + 1] = '\0';
            }
        }
        return;
    }
    if (g->phase == TITLE) {
        if (key == SDLK_SPACE || key == SDLK_RETURN || key == SDLK_KP_ENTER) start_game(g);
        else if (key == SDLK_ESCAPE) g->running = false;
        return;
    }
    if (g->phase == GAME_OVER) {
        if (key == SDLK_RETURN || key == SDLK_SPACE || key == SDLK_ESCAPE) g->phase = TITLE;
        return;
    }
    if (g->phase == PAUSED) {
        if (key == SDLK_P || key == SDLK_SPACE) g->phase = PLAYING;
        else if (key == SDLK_ESCAPE) g->phase = TITLE;
        return;
    }
    if (key == SDLK_LEFT || key == SDLK_A) g->left = true;
    if (key == SDLK_RIGHT || key == SDLK_D) g->right = true;
    if (key == SDLK_UP || key == SDLK_W || key == SDLK_SPACE) {
        if (!g->jump_held && !repeat && g->grounded) {
            g->grounded = false;
            g->air_heading = g->hero_vx_fp > 0 ? 1 :
                             g->hero_vx_fp < 0 ? -1 :
                             (g->right ? 1 : 0) - (g->left ? 1 : 0);
            g->hero_vy_fp = -JUMP_SPEED * FP;
            start_effect(&g->jump);
        }
        g->jump_held = true;
    }
    if (key == SDLK_P && !repeat) g->phase = PAUSED;
    if (key == SDLK_ESCAPE) g->phase = TITLE;
}

static void key_up(Game *g, SDL_Keycode key)
{
    if (key == SDLK_LEFT || key == SDLK_A) g->left = false;
    if (key == SDLK_RIGHT || key == SDLK_D) g->right = false;
    if (key == SDLK_UP || key == SDLK_W || key == SDLK_SPACE) g->jump_held = false;
}

static bool needs_render(const Game *g)
{
    /* The camera does not move outside PLAYING; static screens change only
     * on input/phase changes or a window-expose event. Audio still runs. */
    return g->phase == PLAYING || g->render_dirty;
}

/* Centralize state-change invalidation: key_down decides the transition,
 * handle_event notices it and requests a redraw. Editing a name can change
 * pixels without changing phase. Window expose also invalidates the image. */
static void handle_event(Game *g, const SDL_Event *ev)
{
    const Phase before = g->phase;
    if (ev->type == SDL_EVENT_QUIT) g->running = false;
    else if (ev->type == SDL_EVENT_WINDOW_EXPOSED) g->render_dirty = true;
    else if (ev->type == SDL_EVENT_KEY_DOWN) {
        key_down(g, ev->key.key, ev->key.repeat);
        if (before == ENTER_NAME) g->render_dirty = true;
    } else if (ev->type == SDL_EVENT_KEY_UP) key_up(g, ev->key.key);
    if (g->phase != before) g->render_dirty = true;
}

/* Per iteration: sample wall-clock delta -> events -> PLAYING-only physics ->
 * audio -> conditional draw -> adaptive wait -> optional /L aggregation.
 * previous is refreshed even while PAUSED/TITLE, so resuming does not replay
 * all the time spent in a static screen. Large delta (>64 ms) is clamped for
 * simulation; /L reports this as capped64 and tracks raw vs simulated time. */
static int run(Game *g)
{
    Uint64 previous = SDL_GetTicks();
    SDL_Event ev;
    bool first_frame = true;
    g->running = true;
    g->phase = TITLE;
    g->render_dirty = true;
    if (g->log) {
        g->profile.freq = SDL_GetPerformanceFrequency();
        if (!g->profile.freq) g->profile.freq = 1;
        profile_reset(g, TITLE, previous);
        fprintf(g->log,
                "PERF setup video=%s screen=%dx%d format=%s pitch=%d "
                "canvas=%s pitch=%d shared=%d audio=%s "
                "counter_hz=%llu window_ms=%u\n",
                SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "unknown",
                g->screen->w, g->screen->h,
                SDL_GetPixelFormatName(g->screen->format), g->screen->pitch,
                SDL_GetPixelFormatName(g->canvas->format), g->canvas->pitch,
                g->canvas == g->screen,
                SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "none",
                (unsigned long long)g->profile.freq, PROFILE_WINDOW_MS);
    }
    while (g->running) {
        Uint64 frame_start = g->log ? SDL_GetPerformanceCounter() : 0;
        Uint64 mark = 0;
        const Uint64 now = SDL_GetTicks();
        const Uint64 delta = now - previous;
        const Uint32 simulated_delta = (Uint32)(delta > 64u ? 64u : delta);
        previous = now;
        while (SDL_PollEvent(&ev)) handle_event(g, &ev);
        if (!g->running) break;
        if (g->log) {
            /* An occasional phase-change write must not pollute a timed stage. */
            if (g->profile.phase != g->phase) {
                profile_flush(g, now);
                profile_reset(g, g->phase, now);
                frame_start = SDL_GetPerformanceCounter();
            }
            mark = SDL_GetPerformanceCounter();
            profile_add(g, PROF_EVENTS, frame_start, mark);
        }
        update(g, simulated_delta);
        if (g->log) {
            const Uint64 done = SDL_GetPerformanceCounter();
            profile_add(g, PROF_UPDATE, mark, done);
            mark = done;
        }
        feed_audio(g);
        if (g->log) {
            const Uint64 done = SDL_GetPerformanceCounter();
            profile_add(g, PROF_AUDIO, mark, done);
        }
        if (needs_render(g)) {
            if (!draw(g)) {
                fprintf(stderr, "SKYHOP: render failed: %s\n", SDL_GetError());
                return 1;
            }
            g->render_dirty = false;
            if (g->log) ++g->profile.drawn;
        }
        const Uint64 work_end = g->log ? SDL_GetPerformanceCounter() : 0;
        const Uint64 work_ms = SDL_GetTicks() - now;
        /* ~60 Hz when there is spare CPU time: sleep only 16 ms minus the
         * work already spent, not 16 ms after it. On an overrun, SDL_Delay(0)
         * yields to the cooperative DOS audio scheduler. This is pacing, not
         * a hard 16 ms deadline; physics receives actual elapsed time. */
        SDL_Delay((Uint32)(work_ms < FRAME_TARGET_MS ?
                           FRAME_TARGET_MS - work_ms : 0));
        if (g->log) {
            const Uint64 frame_end = SDL_GetPerformanceCounter();
            Profile *p = &g->profile;
            profile_add(g, PROF_DELAY, work_end, frame_end);
            profile_add(g, PROF_FRAME, frame_start, frame_end);
            ++p->frames;
            p->raw_ms += delta;
            if (p->phase == PLAYING) {
                p->simulated_ms += simulated_delta;
                if (delta > 64u) ++p->capped;
            }
            if (work_end - frame_start > p->freq * 16u / 1000u)
                ++p->work_over_16;
            if (first_frame) {
                fputs("Title frame rendered\n", g->log);
            }
            const Uint64 end_ms = SDL_GetTicks();
            if (end_ms - p->started_ms >= PROFILE_WINDOW_MS) {
                profile_flush(g, end_ms);
                profile_reset(g, g->phase, end_ms);
            }
        }
        first_frame = false;
    }
    return 0;
}

/* Parse options before opening any log, load local scores and resources,
 * then run. Only /L or /LOG opens SKYHOP.LOG; clean up after partial init too.
 * Do not destroy the borrowed window surface when canvas == screen. */
int main(int argc, char **argv)
{
    Game *g = calloc(1, sizeof(*g));
    int result = 1;
    if (!g) return 1;
    g->rng = (uint32_t)SDL_GetTicks() ^ 0x506C6174u;
    for (int i = 1; i < argc; ++i) {
        if (SDL_strcasecmp(argv[i], "/l") == 0 ||
            SDL_strcasecmp(argv[i], "/log") == 0) g->logging = true;
        else if (SDL_strcasecmp(argv[i], "/h") == 0 ||
                 SDL_strcasecmp(argv[i], "/?") == 0) {
            puts("SKYHOP: LEFT/RIGHT or A/D move, SPACE/UP/W jump, P pause, ESC menu.");
            puts("SKYHOP.EXE [/L] - 5-second timing summaries in SKYHOP.LOG.");
            puts("High scores: SKYHOP.SCO in the current directory.");
            free(g);
            return 0;
        } else {
            fprintf(stderr, "SKYHOP: unknown argument: %s\n", argv[i]);
            free(g);
            return 1;
        }
    }
    if (g->logging) {
        g->log = fopen(LOG_FILE, "w");
        if (!g->log) {
            fprintf(stderr, "SKYHOP: cannot create %s in current directory\n", LOG_FILE);
            free(g);
            return 1;
        }
    }
    startup_note(g, "options_ok");
    load_scores(g);
    startup_note(g, "scores_ok");
    if (initialize(g)) {
        /* SKYHOP initializes SDL and loads its own indexed canvas/sound
         * *before* calling the shared header helper; unlike Dino and Shooter,
         * its /L file is already open, so flush the splash boundary to locate
         * silent startup exits. The helper restores SKYHOP's exact palette. */
        startup_note(g, "intro_begin");
        /* Reuse SKYHOP's own indexed canvas, window and optional sound
         * stream. The /L phase counters begin only when run() starts. */
        const SDL3SplashResult intro = sdl3_splash_show(g->window, g->canvas,
            g->audio_ready ? g->audio_stream : NULL, "SPLASH");
        if (g->log) {
            fprintf(g->log, "STARTUP intro_end result=%d\n", (int)intro);
            fflush(g->log);
        }
        if (intro == SDL3_SPLASH_QUIT) {
            result = 0;
        } else {
            if (g->log) {
                fprintf(g->log, "SKYHOP initialized; %d sprites, music=%lu jump=%lu loss=%lu bytes, audio=%s\n",
                        SPRITE_COUNT, (unsigned long)g->music.length,
                        (unsigned long)g->jump.length, (unsigned long)g->loss.length,
                        g->audio_ready ? "ready" : "unavailable");
                fflush(g->log);
            }
            startup_note(g, "loop_begin");
            result = run(g);
            if (g->log) profile_flush(g, SDL_GetTicks());
        }
    } else {
        if (g->log) {
            fprintf(g->log, "STARTUP init_failed reason=%s\n",
                    g->startup_error[0] ? g->startup_error : SDL_GetError());
            fflush(g->log);
        }
        if (!g->startup_error[0]) {
            (void)snprintf(g->startup_error, sizeof(g->startup_error),
                           "initialization failed: %s", SDL_GetError());
        }
    }
    shutdown_audio(g);
    if (g->canvas && g->canvas != g->screen) SDL_DestroySurface(g->canvas);
    if (g->window) SDL_DestroyWindow(g->window);
    SDL_Quit();
    if (g->startup_error[0]) {
        /* SDL_Quit restores DOS text mode, so this remains readable even if
         * the window could not be created. No /L or log file is required. */
        fprintf(stdout, "SKYHOP: %s\n", g->startup_error);
        fputs("Check Images/PLATFORM next to SKYHOP.EXE (or in the current directory).\n", stdout);
        fflush(stdout);
    }
    if (g->log) fclose(g->log);
    free(g);
    return result;
}
