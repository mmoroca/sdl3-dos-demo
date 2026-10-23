#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <string.h>

#include "fonts/font_3x5.h"
#include "fonts/font_5x7.h"
#include "fonts/nueva_font_5x7.h"
#include "fonts/font_8x8.h"

/* by @mmoroca & Arena.AI 2026 (http://arena.ai/) */

/*
 * Minimal multi-font bitmap text showcase at 320x200 (INDEX8).
 * Renders the Spanish pangram in three custom fonts created with tools/genfont.py
 * (3x5, 5x7, and full-Spanish 8x8 with lowercase and accents) in white on black,
 * with automatic word wrapping and UTF-8 codepoint decoding.
 *
 * Esc exits. Pass /L (or /LOG) to enable optional startup logging to FONTDEMO.LOG.
 */

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200
#define MARGIN_X 6
#define MAX_TEXT_WIDTH (SCREEN_WIDTH - MARGIN_X * 2)
#define DOS_PIXEL_FORMAT_PROPERTY "SDL_PIXELFORMAT"
#define LOG_FILE "FONTDEMO.LOG"

#define COLOR_BLACK 0
#define COLOR_WHITE 1

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

/*
 * UTF-8 Spanish pangram rendered in pure 7-bit ASCII source escapes so any host
 * or DOS editor preserves the exact UTF-8 byte sequence untouched:
 * "El veloz murciélago hindú comía feliz cardillo y kiwi. "
 * "La cigüeña toca el saxofón detrás del palenque de paja. 1234567890?!"
 */
static const char PANGRAM_UTF8[] =
    "El veloz murci\xC3\xA9" "lago hind\xC3\xBA com\xC3\xAD" "a feliz cardillo y kiwi. "
    "La cig\xC3\xBC" "e\xC3\xB1" "a toca el saxof\xC3\xB3" "n detr\xC3\xA1" "s del palenque de paja. "
    "1234567890?!";

typedef struct BitmapFont {
    int width;
    int height;
    int letter_spacing;
    int line_spacing;
    int space_width;
    const Glyph *glyphs;
    size_t glyph_count;
} BitmapFont;

typedef struct FontDemoApp {
    SDL_Window *window;
    SDL_Surface *window_surface;
    SDL_Surface *canvas;
    bool sdl_initialized;
} FontDemoApp;

static FILE *log_file;

#define LOG(...) do { \
    if (log_file) { \
        fprintf(log_file, __VA_ARGS__); \
        fputc('\n', log_file); \
        fflush(log_file); \
    } \
} while (0)

/* -------------------------------------------------------------------------- */
/* UTF-8 decoding, glyph lookup with case/accent fallbacks, and word wrap.    */
/* -------------------------------------------------------------------------- */
static Uint32 decode_utf8_codepoint(const char **cursor)
{
    const unsigned char *s = (const unsigned char *)*cursor;
    Uint32 cp;

    if (s[0] < 0x80U) {
        *cursor += 1;
        return s[0];
    }
    if ((s[0] & 0xE0U) == 0xC0U && (s[1] & 0xC0U) == 0x80U) {
        cp = ((Uint32)(s[0] & 0x1FU) << 6) | (Uint32)(s[1] & 0x3FU);
        *cursor += 2;
        return cp;
    }
    if ((s[0] & 0xF0U) == 0xE0U &&
        (s[1] & 0xC0U) == 0x80U &&
        (s[2] & 0xC0U) == 0x80U) {
        cp = ((Uint32)(s[0] & 0x0FU) << 12) |
             ((Uint32)(s[1] & 0x3FU) << 6) |
             (Uint32)(s[2] & 0x3FU);
        *cursor += 3;
        return cp;
    }
    *cursor += 1;
    return '?';
}

static Uint32 uppercase_codepoint(Uint32 cp)
{
    if (cp >= 'a' && cp <= 'z') {
        return cp - ('a' - 'A');
    }
    if (cp >= 0x00E0U && cp <= 0x00FEU && cp != 0x00F7U) {
        return cp - 0x20U;
    }
    return cp;
}

static Uint32 strip_spanish_diacritic(Uint32 cp)
{
    switch (cp) {
    case 0x00E1U: case 0x00C1U: return 'A';
    case 0x00E9U: case 0x00C9U: return 'E';
    case 0x00EDU: case 0x00CDU: return 'I';
    case 0x00F3U: case 0x00D3U: return 'O';
    case 0x00FAU: case 0x00DAU:
    case 0x00FCU: case 0x00DCU: return 'U';
    case 0x00F1U: case 0x00D1U: return 'N';
    case 0x00BFU: return '?';
    case 0x00A1U: return '!';
    default: return cp;
    }
}

static const unsigned short *lookup_exact_glyph(const BitmapFont *font, Uint32 cp)
{
    size_t i;

    for (i = 0; i < font->glyph_count; ++i) {
        if ((Uint32)font->glyphs[i].codepoint == cp) {
            return font->glyphs[i].rows;
        }
    }
    return NULL;
}

static const unsigned short *find_font_glyph(const BitmapFont *font, Uint32 cp)
{
    static const unsigned short empty[16] = { 0 };
    const unsigned short *rows;
    Uint32 upper;
    Uint32 base;

    rows = lookup_exact_glyph(font, cp);
    if (rows) {
        return rows;
    }
    upper = uppercase_codepoint(cp);
    if (upper != cp) {
        rows = lookup_exact_glyph(font, upper);
        if (rows) {
            return rows;
        }
    }
    base = strip_spanish_diacritic(cp);
    if (base != cp) {
        rows = lookup_exact_glyph(font, base);
        if (rows) {
            return rows;
        }
    }
    return empty;
}

static void draw_glyph(SDL_Surface *surface, const BitmapFont *font,
                       int x, int y, Uint32 cp, Uint8 color)
{
    const unsigned short *rows = find_font_glyph(font, cp);
    int row;

    for (row = 0; row < font->height; ++row) {
        const int py = y + row;
        const unsigned int bits = rows[row];
        int col;

        if (py < 0 || py >= SCREEN_HEIGHT || bits == 0U) {
            continue;
        }
        for (col = 0; col < font->width; ++col) {
            const int px = x + col;
            const unsigned int mask = 1U << (font->width - 1 - col);
            if ((bits & mask) != 0U && px >= 0 && px < SCREEN_WIDTH) {
                *((Uint8 *)surface->pixels + (py * surface->pitch) + px) = color;
            }
        }
    }
}

static int measure_word_px(const BitmapFont *font, const char *p)
{
    int width = 0;
    int chars = 0;

    while (*p && *p != ' ' && *p != '\n') {
        (void)decode_utf8_codepoint(&p);
        width += font->width;
        ++chars;
    }
    if (chars > 1) {
        width += (chars - 1) * font->letter_spacing;
    }
    return width;
}

/*
 * Draws word-wrapped UTF-8 text inside [x, x + max_width) and returns the Y
 * coordinate right below the last rendered line.
 */
static int draw_font_wrapped(SDL_Surface *surface, const BitmapFont *font,
                             int x, int y, int max_width,
                             const char *utf8_text, Uint8 color)
{
    const int origin_x = x;
    const char *p = utf8_text;

    if (!surface || !surface->pixels || !utf8_text) {
        return y;
    }

    while (*p) {
        if (*p == '\n') {
            x = origin_x;
            y += font->height + font->line_spacing;
            ++p;
            continue;
        }
        if (*p == ' ') {
            if (x > origin_x) {
                x += font->space_width + font->letter_spacing;
            }
            ++p;
            continue;
        }

        {
            const int word_w = measure_word_px(font, p);
            if (x > origin_x && (x - origin_x) + word_w > max_width) {
                x = origin_x;
                y += font->height + font->line_spacing;
            }
        }

        while (*p && *p != ' ' && *p != '\n') {
            const Uint32 cp = decode_utf8_codepoint(&p);
            if (x > origin_x && (x - origin_x) + font->width > max_width) {
                x = origin_x;
                y += font->height + font->line_spacing;
            }
            draw_glyph(surface, font, x, y, cp, color);
            x += font->width + font->letter_spacing;
        }
    }
    return y + font->height;
}

static void render_pangram_showcase(SDL_Surface *canvas)
{
    const BitmapFont tiny_3x5 = {
        3, 5, 1, 2, 3, font_3x5, ARRAY_COUNT(font_3x5)
    };
    const BitmapFont medium_5x7 = {
        5, 7, 1, 2, 4, font_5x7, ARRAY_COUNT(font_5x7)
    };
    const BitmapFont nuevamedium_5x7 = {
        5, 7, 1, 2, 4, nueva_font_5x7, ARRAY_COUNT(nueva_font_5x7)
    };
    const BitmapFont large_8x8 = {
        8, 8, 1, 3, 5, font_8x8, ARRAY_COUNT(font_8x8)
    };
    int row;
    int y;

    for (row = 0; row < SCREEN_HEIGHT; ++row) {
        memset((Uint8 *)canvas->pixels + (row * canvas->pitch), COLOR_BLACK,
               SCREEN_WIDTH);
    }

    y = 10;
    y = draw_font_wrapped(canvas, &tiny_3x5, MARGIN_X, y, MAX_TEXT_WIDTH,
                          PANGRAM_UTF8, COLOR_WHITE);
    y += 14;
    y = draw_font_wrapped(canvas, &medium_5x7, MARGIN_X, y, MAX_TEXT_WIDTH,
                          PANGRAM_UTF8, COLOR_WHITE);
    y += 22;
    y = draw_font_wrapped(canvas, &nuevamedium_5x7, MARGIN_X, y, MAX_TEXT_WIDTH,
                          PANGRAM_UTF8, COLOR_WHITE);
    y += 30;
    (void)draw_font_wrapped(canvas, &large_8x8, MARGIN_X, y, MAX_TEXT_WIDTH,
                            PANGRAM_UTF8, COLOR_WHITE);
}

/* -------------------------------------------------------------------------- */
/* SDL3 320x200 INDEX8 initialization, event loop, and cleanup.               */
/* -------------------------------------------------------------------------- */
static bool set_bw_palette(SDL_Surface *surface)
{
    static const SDL_Color colors[2] = {
        {   0,   0,   0, 255 },
        { 255, 255, 255, 255 }
    };
    SDL_Palette *palette = SDL_GetSurfacePalette(surface);

    if (!palette) {
        palette = SDL_CreateSurfacePalette(surface);
    }
    if (!palette) {
        return false;
    }
    return SDL_SetPaletteColors(palette, colors, 0, 2);
}

static bool initialize_video(FontDemoApp *app)
{
    SDL_PropertiesID props;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG("SDL_Init(SDL_INIT_VIDEO) failed: %s", SDL_GetError());
        return false;
    }
    app->sdl_initialized = true;
    (void)SDL_HideCursor();

    props = SDL_CreateProperties();
    if (!props) {
        return false;
    }
    (void)SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING,
                                "SDL3 Multi-Font Pangram Demo");
    (void)SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER,
                                SCREEN_WIDTH);
    (void)SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER,
                                SCREEN_HEIGHT);
    (void)SDL_SetNumberProperty(props, DOS_PIXEL_FORMAT_PROPERTY,
                                SDL_PIXELFORMAT_INDEX8);

    app->window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!app->window) {
        LOG("SDL_CreateWindowWithProperties failed: %s", SDL_GetError());
        return false;
    }

    app->window_surface = SDL_GetWindowSurface(app->window);
    if (!app->window_surface) {
        return false;
    }

    if (app->window_surface->format == SDL_PIXELFORMAT_INDEX8 &&
        app->window_surface->w == SCREEN_WIDTH &&
        app->window_surface->h == SCREEN_HEIGHT) {
        app->canvas = app->window_surface;
    } else {
        app->canvas = SDL_CreateSurface(SCREEN_WIDTH, SCREEN_HEIGHT,
                                        SDL_PIXELFORMAT_INDEX8);
        if (!app->canvas) {
            return false;
        }
    }
    return set_bw_palette(app->canvas);
}

static bool present_frame(FontDemoApp *app)
{
    if (app->canvas != app->window_surface &&
        !SDL_BlitSurface(app->canvas, NULL, app->window_surface, NULL)) {
        return false;
    }
    return SDL_UpdateWindowSurface(app->window);
}

static void shutdown_app(FontDemoApp *app)
{
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
    puts("Usage: FONTDEMO.EXE [/L] [/H]");
    puts("");
    puts("Renders a Spanish pangram at 320x200 using font_3x5, font_5x7, and font_8x8.");
    puts("  /L, /LOG   Write startup diagnostics to FONTDEMO.LOG (off by default)");
    puts("  /H, /?     Show this help text");
    puts("  Esc        Exit");
}

int main(int argc, char **argv)
{
    FontDemoApp app = { 0 };
    SDL_Event event;
    bool log_enabled = false;
    bool running = true;
    int exit_status = 1;
    int i;

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];
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
    }
    LOG("FONTDEMO start");

    if (initialize_video(&app)) {
        render_pangram_showcase(app.canvas);
        if (present_frame(&app)) {
            exit_status = 0;
            while (running) {
                while (SDL_PollEvent(&event)) {
                    if (event.type == SDL_EVENT_QUIT ||
                        (event.type == SDL_EVENT_KEY_DOWN &&
                         event.key.key == SDLK_ESCAPE)) {
                        running = false;
                    }
                }
                SDL_Delay(16);
            }
        }
    } else {
        fprintf(stderr, "FONTDEMO could not initialize the 320x200 display.\n");
    }

    LOG("FONTDEMO exit status=%d", exit_status);
    shutdown_app(&app);
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
    return exit_status;
}
