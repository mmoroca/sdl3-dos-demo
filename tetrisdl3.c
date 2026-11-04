/* TETRISDL3 — a commented SDL3 / MS-DOS falling-block game example.
 * by @mmoroca & Arena.AI 2026 (http://arena.ai/)
 * The 7x7 bitmap font was drawn by @mmoroca and is kept verbatim.
 *
 * Reading order: constants/ownership -> video/font renderer -> file/audio
 * setup -> phase transitions/input -> fixed-60-Hz loop -> main/cleanup.
 * The deterministic board rules live in tetris_rules.h. No SDL or disk I/O
 * occurs in a rules tick. The same program works with a shared INDEX8 DOS
 * window surface or an owned INDEX8 canvas blitted onto a true-color window.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdl3_splash.h"
#include "tetris_rules.h"
#include "tetris_scores.h"
#include "fonts/tetris_font_7x7.h"
#include "fonts/tetris_art_palette.h"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200
#define CELL_SIZE 8
#define BOARD_X ((SCREEN_WIDTH - TETRIS_COLUMNS * CELL_SIZE) / 2)
#define BOARD_Y ((SCREEN_HEIGHT - TETRIS_ROWS * CELL_SIZE) / 2)
#define LOG_FILE "TETSDL3.LOG"
#define TITLE_DURATION_MS 3000u
#define MENU_DURATION_MS 5000u
#define FADE_STEP_MS 20u /* like Shooter: 16 palette steps, ~320 ms */
#define FLASH_TICKS 30u /* three 5-on / 5-off flashes at 60 Hz = 0.5 s */
#define FRAME_UNITS 1000u /* delta_ms * 60; consume 1000 per logical tick */
#define MAX_FRAME_STEPS 8u /* avoid endless catch-up on a slow DOS computer */
#define DAS_FRAMES 10u    /* horizontal repeat begins ten logical ticks after keydown */
#define ARR_FRAMES 2u     /* then repeats every two ticks */

/* The user-authored TITLE/RECORDS/GAME backgrounds and transparent LOGO share
 * ONE generated 256-color INDEX8 palette. Indices 0..6 remain text/HUD
 * colors and 16..22 remain the seven exact tetromino colors. The splash
 * borrows this palette for three seconds and restores it before artwork is
 * drawn. See tools/convert_tetris_art.py (not the generic RGB 3:3:2 tool).
 */
enum { C_BG=0, C_PANEL=1, C_BORDER=2, C_TEXT=3, C_DIM=4,
       C_GRID=5, C_CELL_EDGE=6, C_PIECE_FIRST=16 };

typedef enum GamePhase {
    PHASE_TITLE,    /* Own 3 s illustrated title after the borrowed logo. */
    PHASE_MENU,     /* Main screen alternates with TOP10 every five seconds. */
    PHASE_TOP10,    /* Separate full-screen record table. */
    PHASE_PLAYING,  /* 60 Hz gravity and key repeat. */
    PHASE_FLASH,    /* Three visible pulses BEFORE compaction/next spawn. */
    PHASE_PAUSED,   /* Freeze the logical clock. */
    PHASE_CONFIRM_EXIT, /* Esc freezes game/flash; Y leaves, N/Esc resumes. */
    PHASE_NAME,     /* A qualifying loss OR Esc abandonment prompts name. */
    PHASE_GAME_OVER /* Nonqualifying loss; Enter/Esc returns to menu. */
} GamePhase;

typedef enum GameArt { ART_TITLE, ART_RECORDS, ART_GAME, ART_LOGO, ART_COUNT } GameArt;

typedef enum GameSound { SOUND_CLICK, SOUND_FALL, SOUND_OVER,
                         SOUND_LEVEL, SOUND_TETRIS, SOUND_COUNT } GameSound;
/* There is one audio stream; background changes only when its context does. */
typedef enum GameMusic { MUSIC_NONE=-1, MUSIC_BEAT, MUSIC_POLKA,
                         MUSIC_COUNT } GameMusic;

typedef struct GameSample {
    Uint8 *data;       /* SDL_LoadWAV owned, freed by SDL_free at shutdown */
    Uint32 length, cursor;
    bool playing;
} GameSample;

typedef struct TetrisApp {
    SDL_Window *window;                 /* owned */
    SDL_Surface *window_surface;        /* SDL-owned/borrowed: NEVER destroy */
    SDL_Surface *canvas;                /* owned iff canvas != window_surface */
    SDL_Surface *art[ART_COUNT];        /* four owned, locked, INDEX8 BMPs */
    Uint8 shade_menu[256], shade_board[256]; /* per-index darkening tables */
    SDL_AudioStream *audio_stream;      /* owned; splash borrows it first */
    GameSample sounds[SOUND_COUNT];    /* 5 short converted PCM game effects */
    GameSample music[MUSIC_COUNT];      /* two owned looped PCM tracks */
    GameMusic music_mode;               /* follows the phase; no new device */
    bool sdl_initialized, audio_ready, running, dirty, preview_visible;
    FILE *log;                          /* optional, opened ONLY under /L */
    GamePhase phase, confirm_return_phase; /* restore PLAYING/FLASH/PAUSED */
    Uint8 fade_level;                    /* Shooter-style 0..16 palette steps */
    uint64_t fade_last_ms;               /* title fade-in clock */
    bool window_closed;                 /* no fading a destroyed host window */
    TetrisRules game;                   /* pure gameplay rules */
    TetrisScores scores;                /* Top 10 loaded from DOS text file */
    uint64_t best;
    uint32_t next_seed;
    uint64_t previous_ms, title_until_ms, menu_until_ms;
    uint32_t frame_units;
    unsigned flash_tick;
    bool left, right, down;
    int horizontal;
    uint32_t repeat_frames;
    char player_name[TETRIS_NAME_LENGTH+1];
    unsigned name_length;
    bool score_file_bad, save_failed;
    char error[192]; /* printed AFTER SDL restores DOS text mode */
} TetrisApp;

static void app_log(TetrisApp *app, const char *fmt, ...)
{
    if (!app->log) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(app->log, fmt, args);
    va_end(args);
    fputc('\n', app->log);
    fflush(app->log); /* infrequent startup/phase/error writes, never per tick */
}

static void app_fail(TetrisApp *app, const char *what)
{
    (void)snprintf(app->error, sizeof(app->error), "%s: %s", what, SDL_GetError());
    app_log(app, "ERROR %s", app->error);
}

static void app_usage(void)
{
    puts("TETRISDL3 (TETSDL3.EXE) - 320x200 SDL3/DOS falling blocks");
    puts("Usage: TETSDL3.EXE [/H] [/L]");
    puts("  /H  Show help without opening a window");
    puts("  /L  Write startup/phase diagnostics to TETSDL3.LOG (off by default)");
    puts("Enter starts; Left/Right move; Down soft drop; Z/X/Up rotate;");
    puts("Space hard drop; P pause; N next preview; Esc asks EXIT? Y / N.");
    puts("Menu/Top 10 alternate every 5 s; qualifying names: A-Z/0-9, max 8.");
}

/* Parsing precedes SDL and the log. An invalid option never opens graphics or
 * writes a file. /L is the only way to enable disk diagnostics. */
static bool app_parse_args(int argc, char **argv, bool *help, bool *log)
{
    *help = *log = false;
    for (int i=1; i<argc; ++i) {
        if (!SDL_strcasecmp(argv[i], "/h") || !SDL_strcasecmp(argv[i], "/?"))
            *help = true;
        else if (!SDL_strcasecmp(argv[i], "/l") || !SDL_strcasecmp(argv[i], "/log"))
            *log = true;
        else {
            fprintf(stderr, "TETRISDL3: unknown option: %s\n", argv[i]);
            return false;
        }
    }
    return true;
}

/* The independent score module reads only its named Top 10 format.
 * Loading does not rewrite the disk; an invalid file reports a warning
 * until a new player confirms a name: Esc can still cancel without I/O. */
static void app_load_scores(TetrisApp *app)
{
    if (!tetris_scores_load(&app->scores)) {
        app->score_file_bad=true;
        app_log(app,"WARNING malformed %s",TETRIS_RECORD_FILE);
    }
    app->best=tetris_scores_best(&app->scores);
}

/* All three converted BMPs use this exact palette. Shading LUTs darken
 * textured UI regions without an SDL renderer, alpha compositing or costly
 * per-frame RGB searches on a 486. They change pixels, never gameplay. */
static bool app_set_palette(TetrisApp *app)
{
    SDL_Color colors[256];
    for (int i=0;i<256;++i) {
        colors[i].r=tetris_palette_rgb[i][0];
        colors[i].g=tetris_palette_rgb[i][1];
        colors[i].b=tetris_palette_rgb[i][2];
        colors[i].a=SDL_ALPHA_OPAQUE;
    }
    SDL_Palette *palette=SDL_GetSurfacePalette(app->canvas);
    if (!palette) palette=SDL_CreateSurfacePalette(app->canvas);
    if (!palette || !SDL_SetPaletteColors(palette,colors,0,256)) return false;
    for(int i=0;i<256;++i) {
        unsigned best_menu=UINT32_MAX,best_board=UINT32_MAX;
        for(int j=0;j<256;++j) {
            int dr=colors[j].r-(int)colors[i].r*46/100;
            int dg=colors[j].g-(int)colors[i].g*46/100;
            int db=colors[j].b-(int)colors[i].b*46/100;
            unsigned distance=(unsigned)(3*dr*dr+4*dg*dg+2*db*db);
            if(distance<best_menu) {best_menu=distance;app->shade_menu[i]=(Uint8)j;}
            dr=colors[j].r-(int)colors[i].r*72/100;
            dg=colors[j].g-(int)colors[i].g*72/100;
            db=colors[j].b-(int)colors[i].b*72/100;
            distance=(unsigned)(3*dr*dr+4*dg*dg+2*db*db);
            if(distance<best_board) {best_board=distance;app->shade_board[i]=(Uint8)j;}
        }
    }
    return true;
}

/* Shooter's palette ramp: 16 brightness steps at 20 ms intervals. Keep the
 * original generated palette as the immutable base; do NOT fade the loaded
 * BMP surfaces (their palettes are validated at startup and never changed).
 * Update both canvas and window if INDEX8; on an XRGB window app_present()
 * reblits the indexed canvas to make each new brightness visible. */
static bool app_apply_brightness(TetrisApp *app, Uint8 level)
{
    SDL_Color faded[256];
    if(level>16 || !app->canvas) return false;
    for(int index=0;index<256;++index) {
        faded[index].r=(Uint8)((unsigned)tetris_palette_rgb[index][0]*level/16u);
        faded[index].g=(Uint8)((unsigned)tetris_palette_rgb[index][1]*level/16u);
        faded[index].b=(Uint8)((unsigned)tetris_palette_rgb[index][2]*level/16u);
        faded[index].a=SDL_ALPHA_OPAQUE;
    }
    SDL_Palette *canvas_palette=SDL_GetSurfacePalette(app->canvas);
    if(!canvas_palette || !SDL_SetPaletteColors(canvas_palette,faded,0,256))
        return false;
    if(app->window_surface && app->window_surface!=app->canvas &&
       app->window_surface->format==SDL_PIXELFORMAT_INDEX8) {
        SDL_Palette *window_palette=SDL_GetSurfacePalette(app->window_surface);
        if(!window_palette || !SDL_SetPaletteColors(window_palette,faded,0,256))
            return false;
    }
    app->fade_level=level;
    app->dirty=true;
    return true;
}

/* One window/canvas/audio session belongs to THIS executable, never the
 * header-only splash. SDL may give us a direct INDEX8 window surface, or a
 * true-color one requiring an intermediate INDEX8 canvas. Both are legal.
 */
static bool app_init_video(TetrisApp *app)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) { app_fail(app, "SDL video"); return false; }
    app->sdl_initialized = true;
    (void)SDL_SetHint(SDL_HINT_DOS_ALLOW_DIRECT_FRAMEBUFFER, "1");
    SDL_PropertiesID props = SDL_CreateProperties();
    if (!props) { app_fail(app, "window properties"); return false; }
    (void)SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "TETRISDL3");
    (void)SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, SCREEN_WIDTH);
    (void)SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, SCREEN_HEIGHT);
    (void)SDL_SetNumberProperty(props, "SDL_PIXELFORMAT", SDL_PIXELFORMAT_INDEX8);
    app->window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!app->window) { app_fail(app, "SDL window"); return false; }
    app->window_surface = SDL_GetWindowSurface(app->window);
    if (!app->window_surface || app->window_surface->w != SCREEN_WIDTH ||
        app->window_surface->h != SCREEN_HEIGHT) {
        app_fail(app, "320x200 window surface"); return false;
    }
    app->canvas = app->window_surface->format == SDL_PIXELFORMAT_INDEX8 ?
                  app->window_surface :
                  SDL_CreateSurface(SCREEN_WIDTH, SCREEN_HEIGHT, SDL_PIXELFORMAT_INDEX8);
    if (!app->canvas || !app_set_palette(app)) {
        app_fail(app, "INDEX8 canvas/palette"); return false;
    }
    app->fade_level=16; /* splash also returns its borrowed palette restored */
    app_log(app, "video screen=%s canvas=%s shared=%d",
            SDL_GetPixelFormatName(app->window_surface->format),
            SDL_GetPixelFormatName(app->canvas->format), app->canvas==app->window_surface);
    return true;
}

/* SDL_LoadBMP creates owned INDEX8 software surfaces, never a second SDL
 * session. Art is loaded after the borrowed logo, so startup ordering stays
 * splash -> title -> menu. A missing/incorrect image is REQUIRED and names
 * the exact DOS 8.3 path after restoring text mode, like a missing WAV.
 * All four bitmaps must use the one GENERATED RGB palette: copying in a BMP
 * with different indices would silently recolor it on a direct VGA screen.
 */
static bool app_load_art(TetrisApp *app)
{
    static const char *files[ART_COUNT]={"TITLE.BMP","RECORDS.BMP","GAME.BMP","LOGO.BMP"};
    const char *base=SDL_GetBasePath();
    for(int i=0;i<ART_COUNT;++i) {
        char path[512];
        SDL_Surface *image=NULL;
        if(base && sdl3_splash_asset_path(path,base,"TETRIS",files[i]))
            image=SDL_LoadBMP(path);
        if(!image && sdl3_splash_asset_path(path,"","TETRIS",files[i]))
            image=SDL_LoadBMP(path);
        SDL_Palette *colors=image?SDL_GetSurfacePalette(image):NULL;
        const int expected_w=i==ART_LOGO?111:SCREEN_WIDTH;
        const int expected_h=i==ART_LOGO?25:SCREEN_HEIGHT;
        bool valid=image && image->w==expected_w && image->h==expected_h &&
                   image->format==SDL_PIXELFORMAT_INDEX8 && colors &&
                   colors->ncolors==256;
        if(valid) for(int c=0;c<256;++c) {
            const SDL_Color sample=colors->colors[c];
            if(sample.r!=tetris_palette_rgb[c][0] ||
               sample.g!=tetris_palette_rgb[c][1] ||
               sample.b!=tetris_palette_rgb[c][2]) {valid=false;break;}
        }
        if(valid) valid=SDL_LockSurface(image);
        if(!valid) {
            (void)snprintf(app->error,sizeof(app->error),
                           "required TETRIS\\%s missing, invalid or wrong palette",files[i]);
            app_log(app,"ERROR %s",app->error);
            if(image) SDL_DestroySurface(image);
            return false;
        }
        app->art[i]=image; /* remains locked until app_shutdown() */
        app_log(app,"ART loaded %s",files[i]);
    }
    return true;
}

/* One audio device stream is borrowed by the logo for three seconds, then
 * used by a software mixer for two looped tracks and five short effects.
 * Original OGGs remain untouched in docs/; SDL3-DOS loads only offline
 * U8/mono/11025 PCM WAVs. No separate SDL init or compensating silence loop.
 */
static void app_init_audio(TetrisApp *app)
{
    const SDL_AudioSpec spec = { SDL_AUDIO_U8, 1, 11025 };
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        app_log(app,"audio device unavailable: %s",SDL_GetError());
        return;
    }
    app->audio_stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                  &spec,NULL,NULL);
    if (app->audio_stream && SDL_ResumeAudioStreamDevice(app->audio_stream))
        app->audio_ready=true;
    else app_log(app,"audio stream unavailable: %s",SDL_GetError());
}

/* Missing/malformed sound ASSETS are not masked by silent substitutes. Even
 * if there is no Sound Blaster, validate these supplied files at startup and
 * name the failing WAV on the DOS console instead of changing game timing. */
static bool app_load_sounds(TetrisApp *app)
{
    static const char *files[SOUND_COUNT]={"CLICK.WAV","FALL.WAV","OVER.WAV",
                                            "LEVEL.WAV","TETRIS.WAV"};
    const char *base=SDL_GetBasePath();
    for (int i=0;i<SOUND_COUNT+MUSIC_COUNT;++i) {
        SDL_AudioSpec spec={0};
        char path[512];
        const char *filename=i<SOUND_COUNT?files[i]:
                             (i==SOUND_COUNT?"BEAT.WAV":"POLKA.WAV");
        GameSample *s=i<SOUND_COUNT?&app->sounds[i]:&app->music[i-SOUND_COUNT];
        bool loaded=false;
        if (base && sdl3_splash_asset_path(path,base,"TETRIS",filename))
            loaded=SDL_LoadWAV(path,&spec,&s->data,&s->length);
        if (!loaded && sdl3_splash_asset_path(path,"","TETRIS",filename))
            loaded=SDL_LoadWAV(path,&spec,&s->data,&s->length);
        if (!loaded || !s->length || spec.format!=SDL_AUDIO_U8 ||
            spec.channels!=1 || spec.freq!=11025) {
            (void)snprintf(app->error,sizeof(app->error),
                           "required TETRIS\\%s missing or invalid (need U8 mono 11025 Hz)",
                           filename);
            app_log(app,"ERROR %s",app->error);
            return false;
        }
    }
    return true;
}

static void app_sound(TetrisApp *app, GameSound effect)
{
    if (!app->audio_ready) return;
    GameSample *s=&app->sounds[effect];
    s->cursor=0;
    s->playing=true; /* repeated rotations restart only CLICK, not the others */
}

/* Mix U8 PCM around center 128. Music loops at half level so the five
 * effects remain audible; both use the ONE existing audio stream and still
 * support simultaneous LEVEL + TETRIS. Queue at most ~46 ms ahead: leaving
 * a menu/game switches tracks quickly, without buffering a whole song.
 * No per-frame file writes, reallocations or compensating silence filler. */
static void app_audio_pump(TetrisApp *app)
{
    if (!app->audio_ready) return;
    int queued=SDL_GetAudioStreamQueued(app->audio_stream);
    if (queued<0) {
        app_log(app,"audio queue error: %s",SDL_GetError());
        app->audio_ready=false; /* report once; never write a per-frame log */
        return;
    }
    while (queued<512) {
        bool active=app->music_mode!=MUSIC_NONE;
        for (int j=0;j<SOUND_COUNT;++j) if(app->sounds[j].playing) active=true;
        if (!active) break;
        Uint8 block[256];
        for (int i=0;i<256;++i) {
            int mix=128;
            if(app->music_mode!=MUSIC_NONE) {
                GameSample *track=&app->music[app->music_mode];
                /* Validated nonempty at load; no restart at the 5 s flip. */
                mix+=((int)track->data[track->cursor++]-128)/2;
                if(track->cursor==track->length) track->cursor=0;
            }
            for (int j=0;j<SOUND_COUNT;++j) {
                GameSample *s=&app->sounds[j];
                if(!s->playing) continue;
                if(s->cursor<s->length) mix+=(int)s->data[s->cursor++]-128;
                if(s->cursor>=s->length) s->playing=false;
            }
            if(mix<0)mix=0;
            if(mix>255)mix=255;
            block[i]=(Uint8)mix;
        }
        if (!SDL_PutAudioStreamData(app->audio_stream,block,sizeof(block))) {
            app_log(app,"audio playback error: %s",SDL_GetError());
            app->audio_ready=false;
            break;
        }
        queued+=sizeof(block);
    }
}

/* Every drawing primitive writes only inside the locked 320x200 INDEX8
 * canvas. Use its pitch, never assume that one row is exactly 320 bytes. */
static void app_rect(TetrisApp *app, int x, int y, int w, int h, uint8_t color)
{
    const int x0=x<0?0:x, y0=y<0?0:y;
    const int x1=x+w>SCREEN_WIDTH?SCREEN_WIDTH:x+w;
    const int y1=y+h>SCREEN_HEIGHT?SCREEN_HEIGHT:y+h;
    if (x0>=x1 || y0>=y1) return; /* e.g. a glyph wholly off screen */
    for (int yy=y0; yy<y1; ++yy)
        memset((uint8_t *)app->canvas->pixels + yy*app->canvas->pitch+x0,
               color, (size_t)(x1-x0));
}

static const Glyph *app_glyph(char ch)
{
    for (size_t i=0; i<sizeof(tetris_font_7x7)/sizeof(tetris_font_7x7[0]); ++i)
        if (tetris_font_7x7[i].codepoint == (unsigned char)ch) return &tetris_font_7x7[i];
    return NULL;
}

/* Seven columns of authored pixels; advance eight to leave one pixel of
 * spacing. There is no remapping of the intentionally inverted R glyph. */
static void app_text(TetrisApp *app, int x, int y, const char *text, uint8_t color)
{
    while (*text) {
        const Glyph *glyph = app_glyph(*text++);
        if (glyph) for (int gy=0; gy<7; ++gy) for (int gx=0; gx<7; ++gx)
            if (glyph->rows[gy] & (1u << (6-gx)))
                app_rect(app, x+gx, y+gy, 1, 1, color);
        x+=8;
    }
}

static void app_center_text(TetrisApp *app, int y, const char *text, uint8_t color)
{
    app_text(app, (SCREEN_WIDTH-(int)strlen(text)*8)/2, y, text, color);
}

static void app_block(TetrisApp *app, int x, int y, uint8_t piece)
{
    app_rect(app,x,y,CELL_SIZE,CELL_SIZE,C_CELL_EDGE);
    app_rect(app,x+1,y+1,CELL_SIZE-2,CELL_SIZE-2,
             (uint8_t)(C_PIECE_FIRST+piece-1));
    app_rect(app,x+1,y+1,CELL_SIZE-3,1,C_TEXT); /* tiny pixel highlight */
}

/* Full-screen art is already locked/validated on startup. Copy 320 bytes per
 * scanline using BOTH surfaces' pitch, not a presumed tight stride.
 * There is no SDL_BlitSurface palette translation while the canvas is locked.
 * The same canvas/window remains in use for splash, menu and gameplay. */
static void app_blit_art(TetrisApp *app, GameArt image)
{
    SDL_Surface *source=app->art[image];
    for(int y=0;y<SCREEN_HEIGHT;++y)
        memcpy((Uint8*)app->canvas->pixels+y*app->canvas->pitch,
               (const Uint8*)source->pixels+y*source->pitch,SCREEN_WIDTH);
}

/* LOGO.BMP is a separate 111x25 INDEX8 overlay with index 0 transparent.
 * Unlike a colorkeyed full-screen blit, this leaves the shared RECORDS.BMP
 * wall visible between letters. Draw it AFTER darkening the common panel.
 * Its opaque pixels were mapped to indices 1..255 by the offline converter. */
static void app_blit_menu_logo(TetrisApp *app)
{
    const SDL_Surface *logo=app->art[ART_LOGO];
    const int left=(SCREEN_WIDTH-logo->w)/2;
    const int top=29;
    for(int y=0;y<logo->h;++y) {
        const Uint8 *source=(const Uint8*)logo->pixels+y*logo->pitch;
        Uint8 *dest=(Uint8*)app->canvas->pixels+(top+y)*app->canvas->pitch+left;
        for(int x=0;x<logo->w;++x)
            if(source[x]!=0) dest[x]=source[x];
    }
}

/* Nearest-palette dimming retains the artist's bricks and arches even under
 * busy HUD text: RGB shading was precomputed just once at initialization.
 * Different menu/board LUTs suit the high-contrast menu and darker board. */
static void app_dim_art(TetrisApp *app, int x, int y, int w, int h,
                        const Uint8 shades[256])
{
    for(int row=y;row<y+h;++row) {
        Uint8 *pixels=(Uint8*)app->canvas->pixels+row*app->canvas->pitch;
        for(int col=x;col<x+w;++col) pixels[col]=shades[pixels[col]];
    }
}

static void app_title_background(TetrisApp *app)
{
    /* Logo and ARCADE label are already drawn in the original v4 artwork.
     * Never obscure them with the old placeholder TETRISDL3 text/card. */
    app_blit_art(app,ART_TITLE);
}

static void app_records_background(TetrisApp *app)
{
    /* The previous records wall now serves both menu and leaderboard/name.
     * Both use the EXACT same rectangle. Keep its footer unshaded. */
    app_blit_art(app,ART_RECORDS);
    app_dim_art(app,20,16,280,162,app->shade_menu);
}

static void app_menu_background(TetrisApp *app)
{
    /* Place the separate mini logo on TOP of the darkened common panel,
     * horizontally centered at y=29. TOP10 does not receive this overlay. */
    app_records_background(app);
    app_blit_menu_logo(app);
}

static void app_board_background(TetrisApp *app)
{
    /* Do not flatten the custom golden frame: dim only the 10x20 cells in
     * its middle; side reliefs and the arch stay visible behind HUD/blocks. */
    app_blit_art(app,ART_GAME);
    app_dim_art(app,BOARD_X,BOARD_Y,80,160,app->shade_board);
    for(int x=0;x<TETRIS_COLUMNS;++x)
        app_rect(app,BOARD_X+x*CELL_SIZE,BOARD_Y,1,160,C_GRID);
    for(int y=0;y<TETRIS_ROWS;++y)
        app_rect(app,BOARD_X,BOARD_Y+y*CELL_SIZE,80,1,C_GRID);
}

static void app_draw_preview(TetrisApp *app)
{
    app_text(app,224,51,"NEXT",C_TEXT);
    if (!app->preview_visible) { app_text(app,211,69,"HIDDEN",C_DIM); return; }
    for (int y=0;y<4;++y) for(int x=0;x<4;++x)
        if(tetris_cell(app->game.next,0,x,y))
            app_block(app,235+x*CELL_SIZE,71+y*CELL_SIZE,
                      (uint8_t)app->game.next+1);
}

static void app_draw_game(TetrisApp *app)
{
    char number[32];
    app_board_background(app);
    for (int y=0;y<TETRIS_ROWS;++y) for(int x=0;x<TETRIS_COLUMNS;++x)
        if(app->game.board[y][x])
            app_block(app,BOARD_X+x*CELL_SIZE,BOARD_Y+y*CELL_SIZE,
                      app->game.board[y][x]);
    if (app->phase!=PHASE_FLASH &&
        !(app->phase==PHASE_CONFIRM_EXIT && app->confirm_return_phase==PHASE_FLASH)) {
        for(int y=0;y<4;++y) for(int x=0;x<4;++x) {
            if(!tetris_cell(app->game.active,app->game.rotation,x,y)) continue;
            const int bx=app->game.x+x, by=app->game.y+y;
            if(bx>=0 && bx<TETRIS_COLUMNS && by>=0 && by<TETRIS_ROWS)
                app_block(app,BOARD_X+bx*CELL_SIZE,BOARD_Y+by*CELL_SIZE,
                          (uint8_t)app->game.active+1);
        }
    }
    app_text(app,12,40,"SCORE",C_TEXT);
    (void)snprintf(number,sizeof(number),"%09llu",(unsigned long long)app->game.score);
    app_text(app,12,52,number,C_TEXT);
    app_text(app,12,75,"BEST",C_TEXT);
    const uint64_t best=app->best>app->game.score?app->best:app->game.score;
    (void)snprintf(number,sizeof(number),"%09llu",(unsigned long long)best);
    app_text(app,12,87,number,C_TEXT);
    app_text(app,12,110,"LEVEL",C_TEXT);
    (void)snprintf(number,sizeof(number),"%lu",(unsigned long)app->game.level);
    app_text(app,12,122,number,C_TEXT);
    app_text(app,12,145,"LINES",C_TEXT);
    (void)snprintf(number,sizeof(number),"%lu",(unsigned long)app->game.lines);
    app_text(app,12,157,number,C_TEXT);
    app_draw_preview(app);
    /* Keep the two rotation hints brightest against the new stone relief. */
    app_text(app,211,113,"UP/X ROTATE",C_TEXT);
    app_text(app,211,125,"Z ROT LEFT",C_TEXT);
    app_text(app,211,137,"SPACE DROP",C_DIM);
    app_text(app,211,149,"P PAUSE",C_DIM);
    app_text(app,211,161,"N NEXT",C_DIM);
    if((app->phase==PHASE_FLASH ||
        (app->phase==PHASE_CONFIRM_EXIT && app->confirm_return_phase==PHASE_FLASH)) &&
       app->flash_tick%10u<5u) {
        /* Bright row pulses; unchanged board data is compacted only at 30. */
        for(int row=0;row<TETRIS_ROWS;++row)
            if(app->game.pending_rows & (UINT32_C(1)<<row))
                app_rect(app,BOARD_X,BOARD_Y+row*CELL_SIZE,80,CELL_SIZE,C_TEXT);
    }
    if(app->phase==PHASE_CONFIRM_EXIT) {
        /* Same two-line wording as Shooter. Keep the board untouched until
         * redraw, and preserve the active piece/flash during confirmation. */
        app_rect(app,96,82,128,40,C_PANEL);
        app_center_text(app,92,"PAUSED",C_TEXT);
        app_center_text(app,104,"EXIT? Y / N",C_TEXT);
    } else if(app->phase==PHASE_PAUSED || app->phase==PHASE_GAME_OVER) {
        app_rect(app,BOARD_X+1,84,78,32,C_PANEL);
        app_center_text(app,91,app->phase==PHASE_PAUSED?"PAUSED":"GAME OVER",C_TEXT);
    }
}

/* Two alternating menu pages are deliberately separate draw functions, so
 * future art can replace either one without touching scores or game rules. */
static void app_draw_menu(TetrisApp *app)
{
    char record[32];
    app_menu_background(app);
    app_center_text(app,65,"ENTER START",C_BG);
    app_center_text(app,64,"ENTER START",C_TEXT);
    (void)snprintf(record,sizeof(record),"BEST %09llu",(unsigned long long)app->best);
    app_center_text(app,85,record,C_TEXT);
    app_center_text(app,107,app->preview_visible?"N NEXT ON":"N NEXT OFF",C_DIM);
    app_center_text(app,130,"UP X ROTATE RIGHT",C_DIM);
    app_center_text(app,143,"Z ROTATE LEFT",C_DIM);
    app_center_text(app,156,"SPACE DROP ESC QUIT",C_DIM);
    if(app->score_file_bad) app_center_text(app,172,"SCORE FILE INVALID",C_TEXT);
}

static void app_draw_top(TetrisApp *app)
{
    app_records_background(app);
    app_center_text(app,23,"TOP 10",C_BG);
    app_center_text(app,22,"TOP 10",C_TEXT);
    app_text(app,26,43,"NO",C_DIM); /* authored font has no '#' glyph */
    app_text(app,62,43,"NAME",C_DIM);
    app_text(app,143,43,"LV",C_DIM);
    app_text(app,196,43,"POINTS",C_DIM);
    /* Render all ten rows regardless of how many records are saved. Empty
     * rows show 8/2/9 dots for name/level/points at the normal column x.
     * These marks are visual placeholders only, never written to .SCO. */
    for(unsigned i=0;i<TETRIS_TOP_COUNT;++i) {
        char number[32];
        const int y=56+(int)i*12;
        (void)snprintf(number,sizeof(number),"%u",i+1);
        app_text(app,i==9?26:34,y,number,C_TEXT);
        if(i>=app->scores.count) {
            app_text(app,62,y,"........",C_DIM);
            app_text(app,143,y,"..",C_DIM);
            app_text(app,196,y,".........",C_DIM);
            continue;
        }
        const TetrisRecord *e=&app->scores.entry[i];
        app_text(app,62,y,e->name,C_TEXT);
        (void)snprintf(number,sizeof(number),"%lu",(unsigned long)e->level);
        app_text(app,143,y,number,C_TEXT);
        (void)snprintf(number,sizeof(number),"%09llu",(unsigned long long)e->points);
        app_text(app,196,y,number,C_TEXT);
    }
    /* Enter/Esc retain their keyboard behavior without the footer overlay.
     * A rare save failure still appears inside the shaded table region. */
    if(app->save_failed) app_center_text(app,168,"SAVE FAILED",C_TEXT);
}

static void app_draw_name(TetrisApp *app)
{
    char points[32], name[TETRIS_NAME_LENGTH+3];
    app_records_background(app);
    app_center_text(app,32,"TOP 10!",C_TEXT);
    app_center_text(app,57,"ENTER NAME",C_TEXT);
    (void)snprintf(name,sizeof(name),"%s_",app->player_name);
    app_center_text(app,79,name,C_TEXT);
    (void)snprintf(points,sizeof(points),"%09llu POINTS",
                   (unsigned long long)app->game.score);
    app_center_text(app,106,points,C_TEXT);
    (void)snprintf(points,sizeof(points),"LEVEL %lu",
                   (unsigned long)app->game.level);
    app_center_text(app,125,points,C_TEXT);
    app_center_text(app,151,"ENTER SAVE",C_DIM);
    app_center_text(app,165,"ESC CANCEL",C_DIM);
}

static bool app_present(TetrisApp *app)
{
    if (!SDL_LockSurface(app->canvas)) return false;
    switch(app->phase) {
    case PHASE_TITLE:
        app_title_background(app);
        /* Image carries both its logo and ARCADE subcaption. A small prompt
         * occupies only the stone floor below them; it can be skipped. */
        /*
        app_center_text(app,183,"ENTER SPACE ESC",C_BG);
        app_center_text(app,182,"ENTER SPACE ESC",C_TEXT);
        */
        break;
    case PHASE_MENU: app_draw_menu(app); break;
    case PHASE_TOP10: app_draw_top(app); break;
    case PHASE_NAME: app_draw_name(app); break;
    default: app_draw_game(app); break;
    }
    SDL_UnlockSurface(app->canvas);
    if (app->canvas != app->window_surface &&
        !SDL_BlitSurface(app->canvas,NULL,app->window_surface,NULL)) return false;
    return SDL_UpdateWindowSurface(app->window);
}

static void app_clear_input(TetrisApp *app)
{
    app->left=app->right=app->down=false;
    app->horizontal=0;
    app->repeat_frames=0;
    app->frame_units=0;
    app->previous_ms=SDL_GetTicks();
}

static void app_phase(TetrisApp *app, GamePhase next)
{
    const GamePhase previous=app->phase;
    app->phase=next;
    /* First title frame is black after the borrowed splash; the 3 s title
     * clock INCLUDES its 16-step fade. Skipping the title immediately shows
     * the full-bright menu, never a permanently dimmed page. */
    if(next==PHASE_TITLE) {
        if(!app_apply_brightness(app,0)) app_fail(app,"title fade-in palette");
        app->fade_last_ms=SDL_GetTicks();
    } else if(previous==PHASE_TITLE && app->fade_level<16) {
        if(!app_apply_brightness(app,16)) app_fail(app,"title palette restore");
    }
    /* Beat persists across title/menu/records/name, Polka across the game
     * and P-pause. A different context resets its track to the start. */
    GameMusic wanted=MUSIC_NONE;
    if(next==PHASE_TITLE || next==PHASE_MENU || next==PHASE_TOP10 || next==PHASE_NAME)
        wanted=MUSIC_BEAT;
    else if(next==PHASE_PLAYING || next==PHASE_FLASH ||
            next==PHASE_PAUSED || next==PHASE_CONFIRM_EXIT)
        wanted=MUSIC_POLKA;
    if(wanted!=app->music_mode) {
        if(wanted!=MUSIC_NONE) app->music[wanted].cursor=0;
        app->music_mode=wanted;
    }
    app->dirty=true;
    app_clear_input(app); /* no input held across title/menu/pause/flash */
    if(next==PHASE_MENU || next==PHASE_TOP10)
        app->menu_until_ms=SDL_GetTicks()+MENU_DURATION_MS;
    app_log(app,"PHASE %d",(int)next);
}

/* Qualification occurs at BOTH ways a player finishes: loss and voluntary
 * Esc. Window close itself has no keyboard/name input, so cannot create a
 * nameless score. Top 10 ties retain older entries, as in scores_rank(). */
static void app_finish_run(TetrisApp *app, bool lost)
{
    if(lost) app_sound(app,SOUND_OVER);
    if(tetris_scores_rank(&app->scores,app->game.score)>=0) {
        app->player_name[0]='\0';
        app->name_length=0;
        app_phase(app,PHASE_NAME);
    } else app_phase(app,lost?PHASE_GAME_OVER:PHASE_MENU);
}

static void app_confirm_name(TetrisApp *app)
{
    if(!app->name_length) return;
    const int attained_rank=tetris_scores_rank(&app->scores,app->game.score);
    if(tetris_scores_insert(&app->scores,app->player_name,
                             app->game.level,app->game.score)) {
        app->best=tetris_scores_best(&app->scores);
        if(!tetris_scores_save(&app->scores)) {
            app->save_failed=true;
            (void)snprintf(app->error,sizeof(app->error),
                           "cannot save Top 10 to %s",TETRIS_RECORD_FILE);
            app_log(app,"ERROR %s",app->error);
        } else app_log(app,"SCORE saved rank=%d name=%s level=%lu points=%llu",
                       attained_rank+1,
                       app->player_name,(unsigned long)app->game.level,
                       (unsigned long long)app->game.score);
    }
    /* Whether save succeeded or failed, show this separate page for >=5 s. */
    app_phase(app,PHASE_TOP10);
}

static void app_start(TetrisApp *app)
{
    app->next_seed^=(uint32_t)SDL_GetTicks()+UINT32_C(0x9E3779B9);
    tetris_reset(&app->game,app->next_seed);
    app_phase(app,PHASE_PLAYING);
}

/* After a lock without lines the rules already spawned; with lines they
 * retain the stamped rows. UI owns the 30-tick flash, never the rule math. */
static void app_after_lock(TetrisApp *app)
{
    app->dirty=true;
    if(app->game.awaiting_clear) {
        app->flash_tick=0;
        app_phase(app,PHASE_FLASH);
    } else if(app->game.over) app_finish_run(app,true);
}

/* SDL key.repeat is deliberately ignored: horizontal DAS/ARR is measured in
 * game ticks, not DOSBox/host keyboard settings. Name entry accepts ASCII
 * A-Z/0-9 through keycodes, so it also works without SDL text-input IME. */
static void app_key_down(TetrisApp *app, const SDL_KeyboardEvent *key)
{
    if (key->repeat) return;
    const SDL_Keycode k=key->key;
    if (app->phase==PHASE_TITLE) {
        if(k==SDLK_RETURN || k==SDLK_KP_ENTER || k==SDLK_SPACE || k==SDLK_ESCAPE)
            app_phase(app,PHASE_MENU);
        return;
    }
    if (app->phase==PHASE_NAME) {
        if(k==SDLK_ESCAPE) {app_phase(app,PHASE_MENU);return;}
        if(k==SDLK_RETURN || k==SDLK_KP_ENTER) {app_confirm_name(app);return;}
        if(k==SDLK_BACKSPACE) {
            if(app->name_length) app->player_name[--app->name_length]='\0';
            app->dirty=true;
            return;
        }
        if(app->name_length<TETRIS_NAME_LENGTH &&
           ((k>=SDLK_A && k<=SDLK_Z) || (k>=SDLK_0 && k<=SDLK_9))) {
            const unsigned char letter=(unsigned char)k;
            app->player_name[app->name_length++]=(char)toupper(letter);
            app->player_name[app->name_length]='\0';
            app->dirty=true;
        }
        return;
    }
    if (app->phase==PHASE_MENU || app->phase==PHASE_TOP10) {
        if(k==SDLK_RETURN || k==SDLK_KP_ENTER) app_start(app);
        else if(k==SDLK_ESCAPE) app->running=false;
        else if(k==SDLK_N) {app->preview_visible=!app->preview_visible;app->dirty=true;}
        return;
    }
    if (app->phase==PHASE_GAME_OVER) {
        if(k==SDLK_RETURN || k==SDLK_KP_ENTER || k==SDLK_ESCAPE)
            app_phase(app,PHASE_MENU);
        return;
    }
    if(app->phase==PHASE_CONFIRM_EXIT) {
        if(k==SDLK_Y) app_finish_run(app,false); /* still asks for name */
        else if(k==SDLK_N || k==SDLK_ESCAPE)
            app_phase(app,app->confirm_return_phase); /* no board mutation */
        return; /* ignore P, rotations, movement, hard drop and all else */
    }
    if(k==SDLK_ESCAPE && (app->phase==PHASE_PLAYING ||
                          app->phase==PHASE_PAUSED || app->phase==PHASE_FLASH)) {
        app->confirm_return_phase=app->phase;
        app_phase(app,PHASE_CONFIRM_EXIT); /* logical clock is frozen */
        return;
    }
    if(app->phase==PHASE_FLASH) return; /* other keys remain frozen */
    if(k==SDLK_N) {app->preview_visible=!app->preview_visible;app->dirty=true;return;}
    if(k==SDLK_P) {
        app_phase(app,app->phase==PHASE_PLAYING?PHASE_PAUSED:PHASE_PLAYING);
        return;
    }
    if (app->phase!=PHASE_PLAYING) return;
    if(k==SDLK_LEFT || k==SDLK_RIGHT) {
        if(k==SDLK_LEFT) app->left=true; else app->right=true;
        app->horizontal=k==SDLK_LEFT?-1:1;
        app->repeat_frames=0;
        if(tetris_move(&app->game,app->horizontal)) app->dirty=true;
    } else if(k==SDLK_DOWN) app->down=true;
    else if(k==SDLK_Z || k==SDLK_X || k==SDLK_UP) {
        if(tetris_rotate(&app->game,k==SDLK_Z?-1:1)) {
            app_sound(app,SOUND_CLICK);
            app->dirty=true;
        }
    } else if(k==SDLK_SPACE) {
        if(!app->game.grounded) app_sound(app,SOUND_FALL);
        tetris_hard_drop(&app->game);
        app_after_lock(app);
    }
}

static void app_key_up(TetrisApp *app, SDL_Keycode k)
{
    if(k==SDLK_DOWN) app->down=false;
    if(k==SDLK_LEFT) app->left=false;
    if(k==SDLK_RIGHT) app->right=false;
    if((k==SDLK_LEFT && app->horizontal==-1) ||
       (k==SDLK_RIGHT && app->horizontal==1)) {
        app->horizontal=app->left?-1:app->right?1:0;
        app->repeat_frames=0;
    }
}

static void app_logic_frame(TetrisApp *app)
{
    if(app->horizontal) {
        ++app->repeat_frames;
        if(app->repeat_frames==DAS_FRAMES ||
           (app->repeat_frames>DAS_FRAMES &&
            (app->repeat_frames-DAS_FRAMES)%ARR_FRAMES==0))
            if(tetris_move(&app->game,app->horizontal)) app->dirty=true;
    }
    const int old_y=app->game.y;
    const TetrisPiece old_piece=app->game.active;
    const bool grounded=app->game.grounded;
    tetris_tick(&app->game,app->down);
    if(!grounded && app->game.grounded && !app->game.over)
        app_sound(app,SOUND_FALL); /* first contact, BEFORE full lock delay */
    if(app->game.y!=old_y || app->game.active!=old_piece ||
       app->game.over || app->game.awaiting_clear) app->dirty=true;
    if(app->game.over || app->game.awaiting_clear) app_after_lock(app);
}

static void app_flash_frame(TetrisApp *app)
{
    ++app->flash_tick;
    if(app->flash_tick>=FLASH_TICKS) {
        const uint32_t old_level=app->game.level;
        const unsigned cleared=app->game.pending_count;
        tetris_finish_clear(&app->game);
        if(cleared==4) app_sound(app,SOUND_TETRIS);
        if(app->game.level>old_level) app_sound(app,SOUND_LEVEL);
        if(app->game.over) app_finish_run(app,true);
        else app_phase(app,PHASE_PLAYING);
    } else if(app->flash_tick%5u==0u) app->dirty=true;
}

/* Independent wall-clock timers: title is 3 s, each of the two independent
 * menu pages 5 s. Tests can drive this function with synthetic timestamps. */
static void app_timed_phases(TetrisApp *app, uint64_t now)
{
    if(app->phase==PHASE_TITLE && now>=app->title_until_ms)
        app_phase(app,PHASE_MENU);
    else if((app->phase==PHASE_MENU || app->phase==PHASE_TOP10) &&
            now>=app->menu_until_ms)
        app_phase(app,app->phase==PHASE_MENU?PHASE_TOP10:PHASE_MENU);
}

/* One 1/16 Shooter-style brightness step every 20 ms. The title still
 * lasts three seconds including the fade; update only when a step changes. */
static bool app_title_fade_tick(TetrisApp *app, uint64_t now)
{
    if(app->phase!=PHASE_TITLE || app->fade_level>=16 ||
       now-app->fade_last_ms<FADE_STEP_MS) return true;
    app->fade_last_ms=now;
    return app_apply_brightness(app,(Uint8)(app->fade_level+1));
}

/* On normal keyboard exit, fade out BEFORE SDL_Quit restores DOS text mode.
 * No second SDL session, no per-step disk logs; continue pumping the same
 * borrowed audio stream as Shooter does during its palette ramp. */
static bool app_fade_out(TetrisApp *app)
{
    if(!app->window || !app->canvas || app->window_closed) return true;
    while(app->fade_level>0) {
        if(!app_apply_brightness(app,(Uint8)(app->fade_level-1))) return false;
        app_audio_pump(app);
        if(!app_present(app)) return false;
        app->dirty=false;
        SDL_Delay(FADE_STEP_MS);
    }
    return true;
}

/* On an overloaded machine, at most eight 60 Hz ticks per display pass.
 * PLAYING and FLASH own logical time; paused/menu/name screens do not accrue
 * gravity. Five-second menu alternation uses monotonic wall-clock SDL ticks. */
static bool app_run(TetrisApp *app)
{
    SDL_Event ev;
    app->running=true;
    app->previous_ms=SDL_GetTicks();
    while(app->running) {
        while(SDL_PollEvent(&ev)) {
            if(ev.type==SDL_EVENT_QUIT) {
                app->running=false;
                app->window_closed=true; /* the host may have destroyed it */
            }
            else if(ev.type==SDL_EVENT_WINDOW_EXPOSED) app->dirty=true;
            else if(ev.type==SDL_EVENT_KEY_DOWN) app_key_down(app,&ev.key);
            else if(ev.type==SDL_EVENT_KEY_UP) app_key_up(app,ev.key.key);
        }
        if(!app->running) break;
        const uint64_t now=SDL_GetTicks();
        uint64_t elapsed=now-app->previous_ms;
        app->previous_ms=now;
        app_timed_phases(app,now);
        if(!app_title_fade_tick(app,now)) {
            app_fail(app,"title fade-in palette");return false;
        }
        if(app->phase==PHASE_PLAYING || app->phase==PHASE_FLASH) {
            if(elapsed>200) elapsed=200;
            app->frame_units+=(uint32_t)(elapsed*60u);
            unsigned steps=0;
            while((app->phase==PHASE_PLAYING || app->phase==PHASE_FLASH) &&
                  app->frame_units>=FRAME_UNITS && steps<MAX_FRAME_STEPS) {
                app->frame_units-=FRAME_UNITS;
                if(app->phase==PHASE_FLASH) app_flash_frame(app);
                else app_logic_frame(app);
                ++steps;
            }
            if(steps==MAX_FRAME_STEPS && app->frame_units>=FRAME_UNITS)
                app->frame_units%=FRAME_UNITS;
        } else app->frame_units=0;
        app_audio_pump(app);
        if(app->dirty) {
            if(!app_present(app)) {app_fail(app,"present frame");return false;}
            app->dirty=false;
        }
        SDL_Delay(1);
    }
    return true;
}

static void app_shutdown(TetrisApp *app)
{
    if(app->audio_stream) SDL_DestroyAudioStream(app->audio_stream);
    app->audio_stream=NULL;
    for(int i=0;i<SOUND_COUNT;++i) {
        SDL_free(app->sounds[i].data);
        app->sounds[i].data=NULL;
    }
    for(int i=0;i<MUSIC_COUNT;++i) {
        SDL_free(app->music[i].data);
        app->music[i].data=NULL;
    }
    for(int i=0;i<ART_COUNT;++i) {
        if(app->art[i]) {
            SDL_UnlockSurface(app->art[i]); /* balance app_load_art's lock */
            SDL_DestroySurface(app->art[i]);
            app->art[i]=NULL;
        }
    }
    if(app->canvas && app->canvas!=app->window_surface) SDL_DestroySurface(app->canvas);
    app->canvas=NULL;
    if(app->window) SDL_DestroyWindow(app->window);
    app->window=NULL;
    app->window_surface=NULL;
    if(app->sdl_initialized) SDL_Quit();
    app->sdl_initialized=false;
    if(app->log) {fclose(app->log);app->log=NULL;}
    if(app->error[0]) {fprintf(stdout,"TETRISDL3: %s\n",app->error);fflush(stdout);}
}

int main(int argc, char **argv)
{
    bool help=false,logging=false;
    TetrisApp *app=calloc(1,sizeof(*app));
    int result=1;
    bool game_started=false; /* /H and asset failures print no farewell */
    if(!app) {puts("TETRISDL3: out of memory");return 1;}
    if(!app_parse_args(argc,argv,&help,&logging)) {free(app);return 1;}
    if(help) {app_usage();free(app);return 0;}
    if(logging) {
        app->log=fopen(LOG_FILE,"w");
        if(!app->log) {fprintf(stderr,"TETRISDL3: cannot open %s\n",LOG_FILE);
                       free(app);return 1;}
    }
    app_log(app,"STARTUP options_ok");
    app_load_scores(app);
    app->music_mode=MUSIC_NONE;
    app->preview_visible=true;
    app->next_seed=(uint32_t)SDL_GetTicks() ^ UINT32_C(0x527D1A3B);
    if(app_init_video(app)) {
        app_init_audio(app);
        app_log(app,"STARTUP audio_ready=%d",app->audio_ready);
        /* Logo borrows our window/canvas/audio; after it returns, the same
         * audio stream becomes the game's mixer output. No second SDL run. */
        app_log(app,"STARTUP logo_begin");
        const SDL3SplashResult intro=sdl3_splash_show(app->window,app->canvas,
            app->audio_ready?app->audio_stream:NULL,"SPLASH");
        app_log(app,"STARTUP logo_end result=%d",(int)intro);
        if(intro==SDL3_SPLASH_QUIT) result=0;
        else if(intro==SDL3_SPLASH_UNAVAILABLE) {
            (void)snprintf(app->error,sizeof(app->error),
                           "required SPLASH\\SDL3DOS.BMP missing, invalid or undisplayable");
        } else if(app_load_art(app) && app_load_sounds(app)) {
            game_started=true;
            app_phase(app,PHASE_TITLE);
            app->title_until_ms=SDL_GetTicks()+TITLE_DURATION_MS;
            if(!app->error[0] && app_run(app)) {
                if(!app_fade_out(app)) app_fail(app,"fade-out before DOS");
                result=app->error[0]?1:0;
            }
        }
    }
    app_shutdown(app); /* SDL_Quit restores text mode BEFORE printing below */
    if(game_started && result==0) {
        puts("Thank you for playing!");
        fflush(stdout);
    }
    free(app);
    return result;
}
