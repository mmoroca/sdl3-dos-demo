#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* by @mmoroca & Leo.AI 2026 (http://Leo.AI) */

/*
 * The sound test synthesizes an original U8 mono melody, keeps a short SDL
 * audio-stream queue filled, and displays device/queue diagnostics. Audio
 * initialization errors remain visible on screen; Esc stops the test.
 */
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200
#define DOS_PIXEL_FORMAT_PROPERTY "SDL_PIXELFORMAT"

#define AUDIO_SAMPLE_RATE 11025
#define AUDIO_CHUNK_FRAMES 256
#define AUDIO_TARGET_QUEUE_FRAMES (AUDIO_CHUNK_FRAMES * 2)
#define WAVEFORM_POINTS 64
#define MAIN_LOOP_DELAY_MS 10
#define SCREEN_REFRESH_MS 100
#define LOG_INTERVAL_MS 1000

#define COLOR_BACKGROUND 0
#define COLOR_WHITE 1
#define COLOR_CYAN 2
#define COLOR_GREEN 3
#define COLOR_YELLOW 4
#define COLOR_RED 5
#define COLOR_BLUE 6
#define COLOR_DIM 7

typedef struct AppSettings {
    bool log_enabled;
} AppSettings;

typedef struct MelodyNote {
    const char *name;
    Uint16 frequency;
    Uint16 duration_ms;
} MelodyNote;

typedef struct AudioGenerator {
    Uint32 phase;
    Uint32 generated_frames;
    Uint32 frames_left_in_note;
    Uint32 phase_step;
    int note_index;
    Uint8 waveform[WAVEFORM_POINTS];
} AudioGenerator;

/* Central runtime state lets the UI report audio failures and clean partial setup safely. */
typedef struct SoundTestApp {
    SDL_Window *window;
    SDL_Surface *window_surface;
    SDL_Surface *drawing_surface;
    SDL_AudioStream *audio_stream;
    SDL_AudioSpec source_spec;
    SDL_AudioSpec output_spec;
    SDL_AudioSpec device_spec;
    AudioGenerator generator;
    SDL_AudioDeviceID audio_device;
    char device_name[48];
    char driver_name[24];
    char blaster_text[48];
    char audio_status[40];
    bool sdl_initialized;
    bool audio_initialized;
    bool audio_ready;
    bool audio_failed;
    bool output_format_known;
    bool device_buffer_known;
    bool logging_active;
    bool low_queue_latched;
    bool empty_queue_latched;
    Uint32 low_water_events;
    Uint32 empty_queue_events;
    Uint32 generated_frames_at_last_log;
    Uint32 loops_in_interval;
    Uint32 loops_per_second;
    int minimum_queue_frames;
    int maximum_queue_frames;
    Uint32 queued_frames;
    int device_buffer_frames;
    Uint64 start_ticks;
    Uint64 last_log_ticks;
    Uint64 last_draw_ticks;
} SoundTestApp;

static const MelodyNote melody[] = {
    { "C4", 262, 240 }, { "E4", 330, 240 }, { "G4", 392, 240 }, { "D5", 587, 480 },
    { "B4", 494, 240 }, { "G4", 392, 240 }, { "A4", 440, 240 }, { "E4", 330, 480 },
    { "F4", 349, 240 }, { "A4", 440, 240 }, { "C5", 523, 240 }, { "G4", 392, 480 },
    { "E4", 330, 240 }, { "D4", 294, 240 }, { "G4", 392, 240 }, { "C5", 523, 480 }
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
/* Command-line interface: /L logging and /H help options.                    */
/* -------------------------------------------------------------------------- */
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

static void print_help(const char *program_name)
{
    const char *name = (program_name && *program_name) ? program_name : "SBTEST";

    printf("Sound Blaster PCM test for SDL3 DOS\n\n");
    printf("Usage: %s [/L] [/H]\n\n", name);
    printf("Options:\n");
    printf("  /L, /LOG    Write diagnostic information to SBTEST.LOG (off by default)\n");
    printf("  /H, /HELP   Display this help and exit\n\n");
    printf("The program plays an original generated melody in a continuous loop.\n");
    printf("Press Esc to stop playback and exit.\n");
}

static bool parse_arguments(int argc, char **argv, AppSettings *settings, bool *show_help)
{
    int i;

    settings->log_enabled = false;
    *show_help = false;

    for (i = 1; i < argc; ++i) {
        if (equals_ignore_case(argv[i], "/l") ||
            equals_ignore_case(argv[i], "-l") ||
            equals_ignore_case(argv[i], "/log") ||
            equals_ignore_case(argv[i], "-log") ||
            equals_ignore_case(argv[i], "--log")) {
            settings->log_enabled = true;
        } else if (equals_ignore_case(argv[i], "/h") ||
                   equals_ignore_case(argv[i], "-h") ||
                   equals_ignore_case(argv[i], "/help") ||
                   equals_ignore_case(argv[i], "-help") ||
                   equals_ignore_case(argv[i], "--help") ||
                   equals_ignore_case(argv[i], "/?")) {
            *show_help = true;
            return true;
        } else {
            fprintf(stderr, "Error: unknown option '%s'.\n", argv[i]);
            return false;
        }
    }

    return true;
}

/* -------------------------------------------------------------------------- */
/* Display initialization: INDEX8 palette and window creation.                */
/* -------------------------------------------------------------------------- */
static bool set_indexed_palette(SDL_Surface *surface)
{
    SDL_Color colors[256];
    SDL_Palette *palette;
    int i;

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

    colors[COLOR_WHITE].r = 232;
    colors[COLOR_WHITE].g = 244;
    colors[COLOR_WHITE].b = 255;
    colors[COLOR_CYAN].r = 48;
    colors[COLOR_CYAN].g = 220;
    colors[COLOR_CYAN].b = 255;
    colors[COLOR_GREEN].r = 88;
    colors[COLOR_GREEN].g = 255;
    colors[COLOR_GREEN].b = 128;
    colors[COLOR_YELLOW].r = 255;
    colors[COLOR_YELLOW].g = 224;
    colors[COLOR_YELLOW].b = 80;
    colors[COLOR_RED].r = 255;
    colors[COLOR_RED].g = 80;
    colors[COLOR_RED].b = 72;
    colors[COLOR_BLUE].r = 104;
    colors[COLOR_BLUE].g = 144;
    colors[COLOR_BLUE].b = 255;
    colors[COLOR_DIM].r = 32;
    colors[COLOR_DIM].g = 80;
    colors[COLOR_DIM].b = 96;

    return SDL_SetPaletteColors(palette, colors, 0, 256);
}

static bool create_window(SDL_Window **window, SDL_Surface **window_surface)
{
    SDL_PropertiesID properties = SDL_CreateProperties();

    if (!properties) {
        fprintf(stderr, "Error: could not create SDL window properties: %s\n", SDL_GetError());
        return false;
    }

    /* Request an 8-bit palette-based display mode from the DOS video driver. */
    if (!SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING,
                               "Sound Blaster Test - SDL3 DOS") ||
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
/* Bitmap font and software drawing primitives for the 320x200 diagnostic UI. */
/* -------------------------------------------------------------------------- */
static void get_glyph(char character, Uint8 rows[7])
{
    typedef struct Glyph {
        char character;
        Uint8 rows[7];
    } Glyph;
    static const Glyph glyphs[] = {
        { 'A', { 14, 17, 17, 31, 17, 17, 17 } },
        { 'B', { 30, 17, 17, 30, 17, 17, 30 } },
        { 'C', { 14, 17, 16, 16, 16, 17, 14 } },
        { 'D', { 30, 17, 17, 17, 17, 17, 30 } },
        { 'E', { 31, 16, 16, 30, 16, 16, 31 } },
        { 'F', { 31, 16, 16, 30, 16, 16, 16 } },
        { 'G', { 14, 17, 16, 23, 17, 17, 15 } },
        { 'H', { 17, 17, 17, 31, 17, 17, 17 } },
        { 'I', { 31, 4, 4, 4, 4, 4, 31 } },
        { 'J', { 7, 2, 2, 2, 18, 18, 12 } },
        { 'K', { 17, 18, 20, 24, 20, 18, 17 } },
        { 'L', { 16, 16, 16, 16, 16, 16, 31 } },
        { 'M', { 17, 27, 21, 21, 17, 17, 17 } },
        { 'N', { 17, 25, 21, 19, 17, 17, 17 } },
        { 'O', { 14, 17, 17, 17, 17, 17, 14 } },
        { 'P', { 30, 17, 17, 30, 16, 16, 16 } },
        { 'Q', { 14, 17, 17, 17, 21, 18, 13 } },
        { 'R', { 30, 17, 17, 30, 20, 18, 17 } },
        { 'S', { 15, 16, 16, 14, 1, 1, 30 } },
        { 'T', { 31, 4, 4, 4, 4, 4, 4 } },
        { 'U', { 17, 17, 17, 17, 17, 17, 14 } },
        { 'V', { 17, 17, 17, 17, 17, 10, 4 } },
        { 'W', { 17, 17, 17, 21, 21, 21, 10 } },
        { 'X', { 17, 17, 10, 4, 10, 17, 17 } },
        { 'Y', { 17, 17, 10, 4, 4, 4, 4 } },
        { 'Z', { 31, 1, 2, 4, 8, 16, 31 } },
        { '0', { 14, 17, 19, 21, 25, 17, 14 } },
        { '1', { 4, 12, 4, 4, 4, 4, 14 } },
        { '2', { 14, 17, 1, 2, 4, 8, 31 } },
        { '3', { 30, 1, 1, 14, 1, 1, 30 } },
        { '4', { 2, 6, 10, 18, 31, 2, 2 } },
        { '5', { 31, 16, 16, 30, 1, 1, 30 } },
        { '6', { 14, 16, 16, 30, 17, 17, 14 } },
        { '7', { 31, 1, 2, 4, 8, 8, 8 } },
        { '8', { 14, 17, 17, 14, 17, 17, 14 } },
        { '9', { 14, 17, 17, 15, 1, 1, 14 } },
        { ':', { 0, 4, 4, 0, 4, 4, 0 } },
        { '=', { 0, 31, 0, 31, 0, 0, 0 } },
        { '.', { 0, 0, 0, 0, 0, 0, 4 } },
        { ',', { 0, 0, 0, 0, 0, 4, 8 } },
        { '-', { 0, 0, 0, 31, 0, 0, 0 } },
        { '/', { 1, 2, 2, 4, 8, 8, 16 } },
        { '_', { 0, 0, 0, 0, 0, 0, 31 } },
        { '?', { 14, 17, 1, 2, 4, 0, 4 } }
    };
    size_t i;

    memset(rows, 0, 7 * sizeof(*rows));
    character = (char)toupper((unsigned char)character);
    for (i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); ++i) {
        if (glyphs[i].character == character) {
            memcpy(rows, glyphs[i].rows, 7 * sizeof(*rows));
            return;
        }
    }
}

static void put_pixel(SDL_Surface *surface, int x, int y, Uint8 color)
{
    Uint8 *row;

    if (x < 0 || x >= surface->w || y < 0 || y >= surface->h) {
        return;
    }
    row = (Uint8 *)surface->pixels + (y * surface->pitch);
    row[x] = color;
}

static void draw_text(SDL_Surface *surface, int x, int y, const char *text, Uint8 color)
{
    int origin_x = x;

    while (*text) {
        Uint8 rows[7];
        int row;
        int column;

        if (*text == '\n') {
            x = origin_x;
            y += 9;
            ++text;
            continue;
        }

        get_glyph(*text, rows);
        for (row = 0; row < 7; ++row) {
            for (column = 0; column < 5; ++column) {
                if (rows[row] & (Uint8)(1U << (4 - column))) {
                    put_pixel(surface, x + column, y + row, color);
                }
            }
        }
        x += 6;
        ++text;
    }
}

static void draw_line(SDL_Surface *surface, int x0, int y0, int x1, int y1, Uint8 color)
{
    int dx = abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
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

/* -------------------------------------------------------------------------- */
/* Audio diagnostics: labels, melody timing, waveform, and note estimation.   */
/* -------------------------------------------------------------------------- */
static void uppercase_copy(char *destination, size_t capacity, const char *source)
{
    size_t i = 0;

    if (capacity == 0) {
        return;
    }
    if (!source) {
        destination[0] = '\0';
        return;
    }

    while (i + 1 < capacity && source[i]) {
        const unsigned char c = (unsigned char)source[i];
        destination[i] = (char)((c >= 32 && c <= 126) ? toupper(c) : '?');
        ++i;
    }
    destination[i] = '\0';
}

static const char *format_label(SDL_AudioFormat format)
{
    switch (format) {
    case SDL_AUDIO_U8:
        return "U8";
    case SDL_AUDIO_S8:
        return "S8";
    case SDL_AUDIO_S16LE:
        return "S16LE";
    case SDL_AUDIO_S16BE:
        return "S16BE";
    case SDL_AUDIO_S32LE:
        return "S32LE";
    case SDL_AUDIO_S32BE:
        return "S32BE";
    case SDL_AUDIO_F32LE:
        return "F32LE";
    case SDL_AUDIO_F32BE:
        return "F32BE";
    default:
        return "OTHER";
    }
}

static Uint32 melody_duration_ms(void)
{
    Uint32 total = 0;
    size_t i;

    for (i = 0; i < sizeof(melody) / sizeof(melody[0]); ++i) {
        total += melody[i].duration_ms;
    }
    return total;
}

static Uint32 melody_duration_frames(void)
{
    Uint32 total = 0;
    size_t i;

    for (i = 0; i < sizeof(melody) / sizeof(melody[0]); ++i) {
        total += ((Uint32)AUDIO_SAMPLE_RATE * melody[i].duration_ms) / 1000U;
    }
    return total;
}

static int melody_note_at_frame(Uint32 frame)
{
    const Uint32 total_frames = melody_duration_frames();
    size_t i;

    if (total_frames == 0) {
        return 0;
    }
    frame %= total_frames;
    for (i = 0; i < sizeof(melody) / sizeof(melody[0]); ++i) {
        const Uint32 note_frames =
            ((Uint32)AUDIO_SAMPLE_RATE * melody[i].duration_ms) / 1000U;
        if (frame < note_frames) {
            return (int)i;
        }
        frame -= note_frames;
    }
    return 0;
}

static int estimated_output_note(const AudioGenerator *generator, Uint32 queued_frames,
                                 int device_buffer_frames, bool device_buffer_known,
                                 const SDL_AudioSpec *output_spec, bool output_format_known)
{
    Uint64 buffered_frames = queued_frames;
    Uint64 played_frame;

    if (device_buffer_known && device_buffer_frames > 0 &&
        output_format_known && output_spec->freq > 0) {
        buffered_frames += ((Uint64)device_buffer_frames * AUDIO_SAMPLE_RATE) /
                           (Uint32)output_spec->freq;
    }
    played_frame = (generator->generated_frames > buffered_frames)
                       ? (Uint64)generator->generated_frames - buffered_frames
                       : 0;
    return melody_note_at_frame((Uint32)played_frame);
}

/* Synthesize one fixed-size U8 mono PCM block; phase and note state persist. */
static void generate_audio_block(AudioGenerator *generator, Uint8 samples[AUDIO_CHUNK_FRAMES])
{
    size_t i;

    for (i = 0; i < AUDIO_CHUNK_FRAMES; ++i) {
        const MelodyNote *note;
        int triangle;
        Uint8 phase_byte;
        Uint8 sample;

        if (generator->frames_left_in_note == 0) {
            generator->note_index++;
            if (generator->note_index >= (int)(sizeof(melody) / sizeof(melody[0]))) {
                generator->note_index = 0;
            }
            note = &melody[generator->note_index];
            generator->frames_left_in_note =
                ((Uint32)AUDIO_SAMPLE_RATE * note->duration_ms) / 1000U;
            generator->phase_step =
                ((Uint32)note->frequency * 65536U) / AUDIO_SAMPLE_RATE;
        }

        note = &melody[generator->note_index];
        generator->phase += generator->phase_step;
        phase_byte = (Uint8)(generator->phase >> 8);
        if (phase_byte < 128) {
            triangle = ((int)phase_byte * 2) - 127;
        } else {
            triangle = 383 - ((int)phase_byte * 2);
        }
        sample = (Uint8)(128 + (triangle / 2));
        samples[i] = sample;

        if ((i % (AUDIO_CHUNK_FRAMES / WAVEFORM_POINTS)) == 0) {
            generator->waveform[i / (AUDIO_CHUNK_FRAMES / WAVEFORM_POINTS)] = sample;
        }
        --generator->frames_left_in_note;
        ++generator->generated_frames;
    }
}

/* -------------------------------------------------------------------------- */
/* Audio queue monitoring and screen refresh.                                */
/* -------------------------------------------------------------------------- */
static bool queue_audio_to_target(SDL_AudioStream *stream, AudioGenerator *generator,
                                  Uint32 *queued_frames)
{
    Uint8 samples[AUDIO_CHUNK_FRAMES];
    int queued_bytes = SDL_GetAudioStreamQueued(stream);
    const int source_frame_bytes = 1;
    int frames;

    if (queued_bytes < 0) {
        LOG("SDL_GetAudioStreamQueued failed: %s", SDL_GetError());
        return false;
    }
    frames = queued_bytes / source_frame_bytes;
    *queued_frames = (Uint32)frames;

    while (*queued_frames < AUDIO_TARGET_QUEUE_FRAMES) {
        generate_audio_block(generator, samples);
        if (!SDL_PutAudioStreamData(stream, samples, (int)sizeof(samples))) {
            LOG("SDL_PutAudioStreamData failed: %s", SDL_GetError());
            return false;
        }
        *queued_frames += AUDIO_CHUNK_FRAMES;
    }

    return true;
}

/* Track minimum/maximum queue depth and count transitions into low/empty states. */
static void record_queue_depth(int queued_frames, Uint32 *low_water_events,
                               Uint32 *empty_queue_events, int *minimum_queue_frames,
                               int *maximum_queue_frames, bool *low_queue_latched,
                               bool *empty_queue_latched)
{
    if (queued_frames < *minimum_queue_frames) {
        *minimum_queue_frames = queued_frames;
    }
    if (queued_frames > *maximum_queue_frames) {
        *maximum_queue_frames = queued_frames;
    }

    if (queued_frames < AUDIO_CHUNK_FRAMES) {
        if (!*low_queue_latched) {
            ++(*low_water_events);
            *low_queue_latched = true;
        }
    } else {
        *low_queue_latched = false;
    }

    if (queued_frames == 0) {
        if (!*empty_queue_latched) {
            ++(*empty_queue_events);
            *empty_queue_latched = true;
        }
    } else {
        *empty_queue_latched = false;
    }
}

/* Render device/queue diagnostics, the generated waveform, and note status. */
static void draw_screen(SDL_Surface *surface, SDL_Surface *window_surface,
                        SDL_Window *window, const AudioGenerator *generator,
                        bool audio_ready, const char *audio_status, const char *device_name,
                        const char *driver_name, const char *blaster_text,
                        const SDL_AudioSpec *source_spec, const SDL_AudioSpec *output_spec,
                        bool output_format_known, int device_buffer_frames,
                        bool device_buffer_known, bool logging_active,
                        Uint64 start_ticks, Uint32 loops_per_second,
                        Uint32 low_water_events, Uint32 empty_queue_events,
                        Uint32 queued_frames)
{
    char line[96];
    Uint64 elapsed_ms = 0;
    Uint32 song_length = melody_duration_ms();
    const Uint8 status_color =
        (strstr(audio_status, "FAILED") || strstr(audio_status, "ERROR") ||
         strstr(audio_status, "UNAVAILABLE")) ? COLOR_RED : COLOR_WHITE;
    Uint32 elapsed_seconds;
    Uint32 loop_number;
    Uint32 queue_ms = (Uint32)(((Uint64)queued_frames * 1000U) / AUDIO_SAMPLE_RATE);
    const MelodyNote *current_note = NULL;
    int i;

    elapsed_ms = SDL_GetTicks() - start_ticks;
    elapsed_seconds = (Uint32)(elapsed_ms / 1000U);
    loop_number = (song_length > 0) ? (Uint32)(elapsed_ms / song_length) + 1U : 1U;
    if (audio_ready) {
        const int output_note_index = estimated_output_note(
            generator, queued_frames, device_buffer_frames, device_buffer_known,
            output_spec, output_format_known);
        current_note = &melody[output_note_index];
    }

    if (!SDL_LockSurface(surface)) {
        return;
    }
    for (i = 0; i < surface->h; ++i) {
        memset((Uint8 *)surface->pixels + (i * surface->pitch), COLOR_BACKGROUND,
               (size_t)surface->w);
    }

    draw_text(surface, 4, 5, "SBTEST - SOUND BLASTER PCM DIAGNOSTIC", COLOR_CYAN);
    draw_text(surface, 4, 16, "MELODY: DOS WAVEFORM WALTZ LOOPING", COLOR_GREEN);
    if (current_note) {
        (void)snprintf(line, sizeof(line), "PLAYING: %s %u HZ",
                       current_note->name, (unsigned int)current_note->frequency);
    } else {
        (void)snprintf(line, sizeof(line), "PLAYING: NO AUDIO OUTPUT");
    }
    draw_text(surface, 4, 28, line, COLOR_YELLOW);
    (void)snprintf(line, sizeof(line), "TIME: %02u:%02u  LOOP: %u",
                   (unsigned int)(elapsed_seconds / 60U),
                   (unsigned int)(elapsed_seconds % 60U), (unsigned int)loop_number);
    draw_text(surface, 4, 40, line, COLOR_WHITE);
    draw_line(surface, 4, 50, SCREEN_WIDTH - 5, 50, COLOR_DIM);

    (void)snprintf(line, sizeof(line), "SOURCE: %s MONO %d HZ",
                   format_label(source_spec->format), source_spec->freq);
    draw_text(surface, 4, 55, line, COLOR_WHITE);
    if (output_format_known) {
        (void)snprintf(line, sizeof(line), "OUTPUT: %s %dCH %d HZ",
                       format_label(output_spec->format), output_spec->channels,
                       output_spec->freq);
    } else {
        (void)snprintf(line, sizeof(line), "OUTPUT: UNKNOWN");
    }
    draw_text(surface, 4, 67, line, COLOR_WHITE);
    (void)snprintf(line, sizeof(line), "DEVICE: %.40s", device_name);
    draw_text(surface, 4, 79, line, COLOR_WHITE);
    (void)snprintf(line, sizeof(line), "DRIVER: %.16s  LOG: %s",
                   driver_name, logging_active ? "ON" : "OFF");
    draw_text(surface, 4, 91, line, logging_active ? COLOR_GREEN : COLOR_DIM);

    if (device_buffer_known) {
        (void)snprintf(line, sizeof(line), "DEVICE BUFFER: %d FRAMES", device_buffer_frames);
    } else {
        (void)snprintf(line, sizeof(line), "DEVICE BUFFER: UNKNOWN");
    }
    draw_text(surface, 4, 103, line, COLOR_WHITE);
    (void)snprintf(line, sizeof(line), "QUEUE: %u MS  LOW: %lu  EMPTY: %lu",
                   (unsigned int)queue_ms, (unsigned long)low_water_events,
                   (unsigned long)empty_queue_events);
    draw_text(surface, 4, 115, line,
              (low_water_events || empty_queue_events) ? COLOR_YELLOW : COLOR_GREEN);
    (void)snprintf(line, sizeof(line), "MAIN LOOP: %lu/S  SAMPLES: %lu",
                   (unsigned long)loops_per_second,
                   (unsigned long)generator->generated_frames);
    draw_text(surface, 4, 127, line, COLOR_WHITE);
    (void)snprintf(line, sizeof(line), "BLASTER: %.40s", blaster_text);
    draw_text(surface, 4, 139, line, COLOR_CYAN);
    draw_text(surface, 4, 152, "GENERATED PCM WAVEFORM", COLOR_DIM);
    draw_line(surface, 4, 169, SCREEN_WIDTH - 5, 169, COLOR_DIM);
    for (i = 1; i < WAVEFORM_POINTS; ++i) {
        const int x0 = 8 + ((i - 1) * 4);
        const int x1 = 8 + (i * 4);
        const int y0 = 169 - (((int)generator->waveform[i - 1] - 128) / 5);
        const int y1 = 169 - (((int)generator->waveform[i] - 128) / 5);
        draw_line(surface, x0, y0, x1, y1, COLOR_BLUE);
    }
    draw_text(surface, 4, 188, audio_status, status_color);
    draw_text(surface, SCREEN_WIDTH - 52, 188, "ESC=EXIT", COLOR_YELLOW);

    SDL_UnlockSurface(surface);

    if (surface != window_surface && !SDL_BlitSurface(surface, NULL, window_surface, NULL)) {
        LOG("SDL_BlitSurface failed: %s", SDL_GetError());
        return;
    }
    if (!SDL_UpdateWindowSurface(window)) {
        LOG("SDL_UpdateWindowSurface failed: %s", SDL_GetError());
    }
}

/* -------------------------------------------------------------------------- */
/* Video initialization: SDL, the display surface, and the diagnostic palette. */
/* -------------------------------------------------------------------------- */
static bool initialize_soundtest_video(SoundTestApp *app)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "Error: SDL_Init failed: %s\n", SDL_GetError());
        LOG("SDL video initialization failed: %s", SDL_GetError());
        return false;
    }
    app->sdl_initialized = true;
    LOG("SDL video initialized: driver=%s",
        SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(unknown)");

    if (!create_window(&app->window, &app->window_surface)) {
        LOG("Window creation failed: %s", SDL_GetError());
        return false;
    }

    if (app->window_surface->format == SDL_PIXELFORMAT_INDEX8) {
        app->drawing_surface = app->window_surface;
    } else {
        app->drawing_surface = SDL_CreateSurface(SCREEN_WIDTH, SCREEN_HEIGHT,
                                                 SDL_PIXELFORMAT_INDEX8);
        if (!app->drawing_surface) {
            fprintf(stderr, "Error: could not create the indexed drawing surface: %s\n",
                    SDL_GetError());
            LOG("Indexed surface creation failed: %s", SDL_GetError());
            return false;
        }
    }
    if (!set_indexed_palette(app->drawing_surface)) {
        fprintf(stderr, "Error: could not set the VGA palette: %s\n", SDL_GetError());
        LOG("Palette setup failed: %s", SDL_GetError());
        return false;
    }

    if (app->drawing_surface != app->window_surface) {
        LOG("Window surface format=%s; using INDEX8 drawing surface with palette blit",
            SDL_GetPixelFormatName(app->window_surface->format));
    } else {
        LOG("Using INDEX8 window surface with 256-color palette");
    }
    return true;
}

/* -------------------------------------------------------------------------- */
/* Audio initialization: inspect BLASTER, open the stream, and queue PCM.      */
/* Audio failures are non-fatal here so the display can show the diagnosis.   */
/* -------------------------------------------------------------------------- */
static void initialize_soundtest_audio(SoundTestApp *app)
{
    const char *blaster_raw = getenv("BLASTER");
    const char *device_name_raw;
    const char *driver_name_raw;

    if (blaster_raw) {
        uppercase_copy(app->blaster_text, sizeof(app->blaster_text), blaster_raw);
    }
    LOG("BLASTER=%s", blaster_raw ? blaster_raw : "(not set)");

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "Warning: SDL audio initialization failed: %s\n", SDL_GetError());
        LOG("SDL audio initialization failed: %s", SDL_GetError());
        app->audio_failed = true;
        (void)snprintf(app->audio_status, sizeof(app->audio_status),
                       "AUDIO INIT FAILED - CHECK BLASTER");
        return;
    }
    app->audio_initialized = true;

    app->source_spec.format = SDL_AUDIO_U8;
    app->source_spec.channels = 1;
    app->source_spec.freq = AUDIO_SAMPLE_RATE;
    app->audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                   &app->source_spec, NULL, NULL);
    if (!app->audio_stream) {
        fprintf(stderr, "Warning: could not open audio playback: %s\n", SDL_GetError());
        LOG("SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
        app->audio_failed = true;
        (void)snprintf(app->audio_status, sizeof(app->audio_status),
                       "AUDIO OPEN FAILED - CHECK BLASTER");
        return;
    }

    app->audio_device = SDL_GetAudioStreamDevice(app->audio_stream);
    device_name_raw = SDL_GetAudioDeviceName(app->audio_device);
    if (device_name_raw) {
        uppercase_copy(app->device_name, sizeof(app->device_name), device_name_raw);
    }
    driver_name_raw = SDL_GetCurrentAudioDriver();
    if (driver_name_raw) {
        uppercase_copy(app->driver_name, sizeof(app->driver_name), driver_name_raw);
    }
    app->output_format_known = SDL_GetAudioStreamFormat(app->audio_stream,
                                                         &app->source_spec,
                                                         &app->output_spec);
    app->device_buffer_known = SDL_GetAudioDeviceFormat(app->audio_device,
                                                         &app->device_spec,
                                                         &app->device_buffer_frames);
    if (!app->output_format_known) {
        LOG("SDL_GetAudioStreamFormat failed: %s", SDL_GetError());
    }
    if (!app->device_buffer_known) {
        LOG("SDL_GetAudioDeviceFormat failed: %s", SDL_GetError());
    }

    LOG("Audio stream opened: device_id=%lu name=%s driver=%s",
        (unsigned long)app->audio_device, app->device_name, app->driver_name);
    LOG("Audio source: format=%s channels=%d rate=%d block_frames=%d queue_target_frames=%d",
        format_label(app->source_spec.format), app->source_spec.channels,
        app->source_spec.freq, AUDIO_CHUNK_FRAMES, AUDIO_TARGET_QUEUE_FRAMES);
    if (app->output_format_known) {
        LOG("Audio stream output: format=%s channels=%d rate=%d",
            format_label(app->output_spec.format), app->output_spec.channels,
            app->output_spec.freq);
    }
    if (app->device_buffer_known) {
        LOG("Audio device format: format=%s channels=%d rate=%d buffer_frames=%d",
            format_label(app->device_spec.format), app->device_spec.channels,
            app->device_spec.freq, app->device_buffer_frames);
    }

    if (!queue_audio_to_target(app->audio_stream, &app->generator, &app->queued_frames)) {
        fprintf(stderr, "Warning: could not queue initial audio: %s\n", SDL_GetError());
        LOG("Initial audio queue failed: %s", SDL_GetError());
        app->audio_failed = true;
        (void)snprintf(app->audio_status, sizeof(app->audio_status), "AUDIO QUEUE FAILED");
        return;
    }
    if (!SDL_ResumeAudioStreamDevice(app->audio_stream)) {
        fprintf(stderr, "Warning: could not start audio playback: %s\n", SDL_GetError());
        LOG("SDL_ResumeAudioStreamDevice failed: %s", SDL_GetError());
        app->audio_failed = true;
        (void)snprintf(app->audio_status, sizeof(app->audio_status), "AUDIO START FAILED");
        return;
    }

    app->audio_ready = true;
    (void)snprintf(app->audio_status, sizeof(app->audio_status), "AUDIO PLAYING");
    app->start_ticks = SDL_GetTicks();
    app->last_log_ticks = app->start_ticks;
    app->last_draw_ticks = 0;
    LOG("Playback resumed; initial queue=%lu frames", (unsigned long)app->queued_frames);
}

/* -------------------------------------------------------------------------- */
/* Keyboard/control loop: feed audio, update diagnostics, redraw, and log.     */
/* Esc or a window-close event ends the test; audio errors remain visible.    */
/* -------------------------------------------------------------------------- */
static bool run_soundtest_loop(SoundTestApp *app)
{
    bool running = true;
    SDL_Event event;

    while (running) {
        const Uint64 now = SDL_GetTicks();

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                LOG("Quit event received");
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                LOG("Escape key received");
                running = false;
            }
        }
        if (!running) {
            break;
        }

        ++app->loops_in_interval;
        if (app->audio_ready && !app->audio_failed) {
            int current_queue_frames;
            const int queue_bytes = SDL_GetAudioStreamQueued(app->audio_stream);
            if (queue_bytes < 0) {
                LOG("SDL_GetAudioStreamQueued failed during playback: %s", SDL_GetError());
                app->audio_failed = true;
                app->audio_ready = false;
                (void)snprintf(app->audio_status, sizeof(app->audio_status),
                               "AUDIO QUEUE ERROR");
            } else {
                current_queue_frames = queue_bytes;
                record_queue_depth(current_queue_frames, &app->low_water_events,
                                   &app->empty_queue_events, &app->minimum_queue_frames,
                                   &app->maximum_queue_frames, &app->low_queue_latched,
                                   &app->empty_queue_latched);
                app->queued_frames = (Uint32)current_queue_frames;
                if (!queue_audio_to_target(app->audio_stream, &app->generator,
                                           &app->queued_frames)) {
                    fprintf(stderr, "Warning: audio queue update failed: %s\n", SDL_GetError());
                    app->audio_failed = true;
                    app->audio_ready = false;
                    (void)snprintf(app->audio_status, sizeof(app->audio_status),
                                   "AUDIO FEED FAILED");
                }
            }
        }

        if (now - app->last_draw_ticks >= SCREEN_REFRESH_MS) {
            draw_screen(app->drawing_surface, app->window_surface, app->window,
                        &app->generator, app->audio_ready && !app->audio_failed,
                        app->audio_status, app->device_name, app->driver_name,
                        app->blaster_text, &app->source_spec, &app->output_spec,
                        app->output_format_known, app->device_buffer_frames,
                        app->device_buffer_known, app->logging_active, app->start_ticks,
                        app->loops_per_second, app->low_water_events,
                        app->empty_queue_events, app->queued_frames);
            app->last_draw_ticks = now;
        }

        if (now - app->last_log_ticks >= LOG_INTERVAL_MS) {
            const Uint64 interval_ms = now - app->last_log_ticks;
            const Uint32 generated_interval = app->generator.generated_frames -
                                              app->generated_frames_at_last_log;
            if (interval_ms > 0) {
                app->loops_per_second =
                    (Uint32)(((Uint64)app->loops_in_interval * 1000ULL) / interval_ms);
            }
            if (app->logging_active) {
                const int output_note_index = app->audio_ready
                    ? estimated_output_note(&app->generator, app->queued_frames,
                                            app->device_buffer_frames,
                                            app->device_buffer_known, &app->output_spec,
                                            app->output_format_known)
                    : -1;
                LOG("Stats: elapsed_ms=%lu loops_per_sec=%lu generated_frames=%lu generated_this_interval=%lu output_note=%s/%uHz generated_note=%s/%uHz queue_frames=%lu queue_ms=%lu min_queue=%lu max_queue=%lu low_water_events=%lu empty_queue_events=%lu",
                    (unsigned long)(now - app->start_ticks),
                    (unsigned long)app->loops_per_second,
                    (unsigned long)app->generator.generated_frames,
                    (unsigned long)generated_interval,
                    (output_note_index >= 0) ? melody[output_note_index].name : "NONE",
                    (output_note_index >= 0)
                        ? (unsigned int)melody[output_note_index].frequency : 0U,
                    (app->generator.note_index >= 0)
                        ? melody[app->generator.note_index].name : "NONE",
                    (app->generator.note_index >= 0)
                        ? (unsigned int)melody[app->generator.note_index].frequency : 0U,
                    (unsigned long)app->queued_frames,
                    (unsigned long)(((Uint64)app->queued_frames * 1000U) / AUDIO_SAMPLE_RATE),
                    (unsigned long)((app->minimum_queue_frames == INT_MAX)
                                        ? 0 : app->minimum_queue_frames),
                    (unsigned long)app->maximum_queue_frames,
                    (unsigned long)app->low_water_events,
                    (unsigned long)app->empty_queue_events);
            }
            app->generated_frames_at_last_log = app->generator.generated_frames;
            app->loops_in_interval = 0;
            app->last_log_ticks = now;
        }

        SDL_Delay(MAIN_LOOP_DELAY_MS);
    }

    LOG("Playback stopped: exit_status=%d generated_frames=%lu low_water_events=%lu empty_queue_events=%lu",
        app->audio_failed ? 1 : 0, (unsigned long)app->generator.generated_frames,
        (unsigned long)app->low_water_events, (unsigned long)app->empty_queue_events);
    return !app->audio_failed;
}

/* -------------------------------------------------------------------------- */
/* Resource teardown: stop ownership in reverse order of acquisition.         */
/* -------------------------------------------------------------------------- */
static void shutdown_soundtest_app(SoundTestApp *app)
{
    if (app->audio_stream) {
        SDL_DestroyAudioStream(app->audio_stream);
        app->audio_stream = NULL;
    }
    if (app->drawing_surface && app->drawing_surface != app->window_surface) {
        SDL_DestroySurface(app->drawing_surface);
        app->drawing_surface = NULL;
    }
    if (app->window) {
        SDL_DestroyWindow(app->window);
        app->window = NULL;
        app->window_surface = NULL;
    }
    if (app->sdl_initialized) {
        SDL_Quit();
        app->sdl_initialized = false;
    } else if (app->audio_initialized) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        app->audio_initialized = false;
    }
}

/* -------------------------------------------------------------------------- */
/* Program entry point: parse options, initialize the test, then tear down.    */
/* -------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    AppSettings settings;
    SoundTestApp app = { 0 };
    bool show_help;
    bool video_ready;
    int result = 1;

    if (!parse_arguments(argc, argv, &settings, &show_help)) {
        print_help(argv[0]);
        return 1;
    }
    if (show_help) {
        print_help(argv[0]);
        return 0;
    }

    app.source_spec.format = SDL_AUDIO_U8;
    app.source_spec.channels = 1;
    app.source_spec.freq = AUDIO_SAMPLE_RATE;
    (void)snprintf(app.device_name, sizeof(app.device_name), "UNKNOWN");
    (void)snprintf(app.driver_name, sizeof(app.driver_name), "UNKNOWN");
    (void)snprintf(app.blaster_text, sizeof(app.blaster_text), "NOT SET");
    (void)snprintf(app.audio_status, sizeof(app.audio_status), "AUDIO INIT FAILED");
    app.minimum_queue_frames = INT_MAX;
    app.generator.note_index = -1;
    memset(app.generator.waveform, 128, sizeof(app.generator.waveform));

    if (settings.log_enabled) {
        log_file = fopen("SBTEST.LOG", "w");
        if (!log_file) {
            fprintf(stderr, "Warning: could not open SBTEST.LOG; continuing without debug logging.\n");
        } else {
            app.logging_active = true;
        }
    }
    LOG("Sound Blaster test started");
    LOG("Logging enabled; periodic performance statistics are written once per second");

    video_ready = initialize_soundtest_video(&app);
    if (video_ready) {
        initialize_soundtest_audio(&app);
        result = run_soundtest_loop(&app) ? 0 : 1;
    }

    shutdown_soundtest_app(&app);
    if (log_file) {
        LOG("Shutdown");
        fclose(log_file);
        log_file = NULL;
    }
    return result;
}
