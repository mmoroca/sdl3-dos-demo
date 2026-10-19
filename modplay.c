#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* by @mmoroca & Leo.AI 2026 (http://Leo.AI) */

/*
 * MODPLAY loads a bounded 31-sample/four-channel ProTracker .MOD, advances
 * tracker ticks, mixes PCM into an SDL audio stream, and updates a 320x200
 * status UI. SPACE pauses/resumes; Esc exits. MODPLAY.LOG is enabled by /L.
 */
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200
#define DOS_PIXEL_FORMAT_PROPERTY "SDL_PIXELFORMAT"

#define MOD_SAMPLE_COUNT 31
#define MOD_CHANNEL_COUNT 4
#define MOD_ORDER_COUNT 128
#define MOD_PATTERN_COUNT 128
#define MOD_ROWS_PER_PATTERN 64
#define MOD_PATTERN_BYTES (MOD_ROWS_PER_PATTERN * MOD_CHANNEL_COUNT * 4)
#define MAX_MODULE_BYTES (16UL * 1024UL * 1024UL)

#define AUDIO_MAX_CHUNK_FRAMES 1024
#define AUDIO_QUEUE_TARGET_MS 65
#define SCREEN_REFRESH_MS 100
#define LOG_INTERVAL_MS 1000
#define MAIN_LOOP_DELAY_MS 10

#define COLOR_BACKGROUND 0
#define COLOR_WHITE 1
#define COLOR_CYAN 2
#define COLOR_GREEN 3
#define COLOR_YELLOW 4
#define COLOR_RED 5
#define COLOR_BLUE 6
#define COLOR_DIM 7
#define COLOR_HIGHLIGHT 8
#define COLOR_BORDER 9

#define DEFAULT_RATE 22050
#define DEFAULT_BITS 8
#define DEFAULT_CHANNELS 1

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* ProTracker finetune multipliers in signed 16.16 form, indexed by nibble. */
static const Uint32 finetune_ratio[16] = {
    69433, 68933, 68438, 67945, 67456, 66971, 66489, 66011,
    65536, 65065, 64596, 64132, 63670, 63212, 62757, 62306
};

static const Uint16 period_table[] = {
    1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016, 960, 907,
    856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
    428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
    214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113
};

static const char *const note_names[] = {
    "C-1", "C#1", "D-1", "D#1", "E-1", "F-1", "F#1", "G-1", "G#1", "A-1", "A#1", "B-1",
    "C-2", "C#2", "D-2", "D#2", "E-2", "F-2", "F#2", "G-2", "G#2", "A-2", "A#2", "B-2",
    "C-3", "C#3", "D-3", "D#3", "E-3", "F-3", "F#3", "G-3", "G#3", "A-3", "A#3", "B-3",
    "C-4", "C#4", "D-4", "D#4", "E-4", "F-4", "F#4", "G-4", "G#4", "A-4", "A#4", "B-4"
};

typedef struct ModSample {
    char name[23];
    const Uint8 *data;
    Uint32 length;
    Uint32 loop_start;
    Uint32 loop_length;
    Uint8 volume;
    Uint8 finetune;
} ModSample;

typedef struct ModModule {
    Uint8 *file_data;
    size_t file_size;
    char title[21];
    char signature[5];
    Uint8 song_length;
    Uint8 restart_position;
    Uint8 orders[MOD_ORDER_COUNT];
    Uint16 pattern_count;
    const Uint8 *patterns;
    ModSample samples[MOD_SAMPLE_COUNT];
} ModModule;

typedef struct ModCell {
    Uint8 sample;
    Uint16 period;
    Uint8 effect;
    Uint8 parameter;
} ModCell;

typedef struct PlayerChannel {
    Uint8 instrument;
    Uint8 playing_sample;
    Uint8 volume;
    Uint8 finetune;
    Uint16 period;
    Uint16 row_base_period;
    Uint16 target_period;
    Uint32 step_fp;
    Uint32 sample_position;
    Uint16 sample_fraction;
    bool active;

    Uint8 effect;
    Uint8 parameter;
    Uint8 display_sample;
    Uint16 display_period;
    Uint8 display_effect;
    Uint8 display_parameter;

    Uint8 effect_memory[16];
    Uint8 vibrato_speed;
    Uint8 vibrato_depth;
    Uint8 vibrato_phase;
    Uint8 loop_row;
    Uint8 loop_count;

    bool delayed_note_pending;
    Uint16 delayed_period;
    Uint8 delayed_tick;
} PlayerChannel;

typedef struct ModPlayer {
    const ModModule *module;
    Uint32 sample_rate;
    Uint8 output_bits;
    Uint8 output_channels;
    Uint16 order;
    Uint8 row;
    Uint8 tick;
    Uint8 speed;
    Uint16 bpm;
    int pending_order;
    int pending_row;
    Uint32 tick_samples_left;
    Uint32 tick_fraction;
    Uint32 generated_frames;
    Uint32 song_loops;
    PlayerChannel channels[MOD_CHANNEL_COUNT];
} ModPlayer;

typedef struct AppSettings {
    const char *module_path;
    Uint32 rate;
    Uint8 bits;
    Uint8 channels;
    bool log_enabled;
} AppSettings;

typedef struct ScreenCache {
    bool initialized;
    Uint16 order;
    Uint8 pattern;
    Uint8 row;
    Uint8 row_start;
    Uint32 active_samples;
} ScreenCache;

/* Owns module, SDL handles, tracker state, and loop diagnostics for one playback session. */
typedef struct ModPlayApp {
    ModModule module;
    ModPlayer player;
    SDL_Window *window;
    SDL_Surface *window_surface;
    SDL_Surface *drawing_surface;
    SDL_AudioStream *audio_stream;
    ScreenCache screen_cache;
    SDL_AudioSpec source_spec;
    SDL_AudioSpec stream_output_spec;
    SDL_AudioSpec device_spec;
    SDL_AudioDeviceID audio_device;
    int device_buffer_frames;
    char device_name[32];
    char driver_name[20];
    bool sdl_initialized;
    bool audio_initialized;
    bool output_format_known;
    bool device_format_known;
    bool logging_active;
    bool paused;
    bool space_down;
    bool audio_ready;
    bool playback_error;
    bool low_queue_latched;
    bool empty_queue_latched;
    Uint32 queued_frames;
    Uint32 target_frames;
    Uint32 low_queue_events;
    Uint32 empty_queue_events;
    Uint32 minimum_queue_frames;
    Uint32 maximum_queue_frames;
    Uint32 max_refill_ms;
    Uint32 max_draw_ms;
    Uint64 total_draw_ms;
    Uint32 draw_count;
    Uint32 loops_per_second;
    Uint32 loops_this_interval;
    Uint64 start_ticks;
    Uint64 last_draw_ticks;
    Uint64 last_stats_ticks;
    Uint32 generated_at_last_stats;
} ModPlayApp;

static FILE *log_file;

#define LOG(...) do { \
    if (log_file) { \
        fprintf(log_file, __VA_ARGS__); \
        fputc('\n', log_file); \
        fflush(log_file); \
    } \
} while (0)

/* -------------------------------------------------------------------------- */
/* Command-line interface: options, defaults, accepted rates, and help text.  */
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

static bool parse_unsigned(const char *text, unsigned int *value)
{
    unsigned long parsed = 0;
    const unsigned char *cursor = (const unsigned char *)text;

    if (!text || !*text || !value) {
        return false;
    }
    while (*cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        parsed = (parsed * 10UL) + (unsigned long)(*cursor - '0');
        if (parsed > 1000000UL) {
            return false;
        }
        ++cursor;
    }
    *value = (unsigned int)parsed;
    return true;
}

static bool parse_rate(const char *text, Uint32 *rate)
{
    unsigned int value;

    if (!parse_unsigned(text, &value)) {
        return false;
    }
    if (value != 11025U && value != 22050U && value != 44100U) {
        return false;
    }
    *rate = (Uint32)value;
    return true;
}

static bool parse_arguments(int argc, char **argv, AppSettings *settings, bool *show_help)
{
    int i;

    settings->module_path = NULL;
    settings->rate = DEFAULT_RATE;
    settings->bits = DEFAULT_BITS;
    settings->channels = DEFAULT_CHANNELS;
    settings->log_enabled = false;
    *show_help = false;

    for (i = 1; i < argc; ++i) {
        const char *argument = argv[i];
        const char *value = NULL;

        if (equals_ignore_case(argument, "/H") || equals_ignore_case(argument, "-H") ||
            equals_ignore_case(argument, "/HELP") || equals_ignore_case(argument, "--HELP") ||
            equals_ignore_case(argument, "/?")) {
            *show_help = true;
            return true;
        }
        if (equals_ignore_case(argument, "/L") || equals_ignore_case(argument, "-L") ||
            equals_ignore_case(argument, "/LOG") || equals_ignore_case(argument, "--LOG")) {
            settings->log_enabled = true;
            continue;
        }
        if (equals_ignore_case(argument, "/R") || equals_ignore_case(argument, "-R") ||
            equals_ignore_case(argument, "/RATE") || equals_ignore_case(argument, "--RATE")) {
            if (++i >= argc || !parse_rate(argv[i], &settings->rate)) {
                fprintf(stderr, "Error: /R requires 11025, 22050, or 44100.\n");
                return false;
            }
            continue;
        }
        if (equals_ignore_case(argument, "/B") || equals_ignore_case(argument, "-B") ||
            equals_ignore_case(argument, "/BITS") || equals_ignore_case(argument, "--BITS")) {
            unsigned int bits;
            if (++i >= argc || !parse_unsigned(argv[i], &bits) || (bits != 8U && bits != 16U)) {
                fprintf(stderr, "Error: /B requires 8 or 16.\n");
                return false;
            }
            settings->bits = (Uint8)bits;
            continue;
        }
        if (equals_ignore_case(argument, "/C") || equals_ignore_case(argument, "-C") ||
            equals_ignore_case(argument, "/CHANNELS") || equals_ignore_case(argument, "--CHANNELS")) {
            if (++i >= argc) {
                fprintf(stderr, "Error: /C requires MONO or STEREO.\n");
                return false;
            }
            value = argv[i];
            if (equals_ignore_case(value, "1") || equals_ignore_case(value, "MONO")) {
                settings->channels = 1;
            } else if (equals_ignore_case(value, "2") || equals_ignore_case(value, "STEREO")) {
                settings->channels = 2;
            } else {
                fprintf(stderr, "Error: /C requires MONO, STEREO, 1, or 2.\n");
                return false;
            }
            continue;
        }
        if (argument[0] == '/' || argument[0] == '-') {
            fprintf(stderr, "Error: unknown option '%s'.\n", argument);
            return false;
        }
        if (settings->module_path) {
            fprintf(stderr, "Error: only one module filename is accepted.\n");
            return false;
        }
        settings->module_path = argument;
    }

    if (!settings->module_path) {
        fprintf(stderr, "Error: a .MOD filename is required.\n");
        return false;
    }
    return true;
}

static void print_help(const char *program_name)
{
    const char *name = (program_name && *program_name) ? program_name : "MODPLAY";

    printf("Four-channel ProTracker MOD player for SDL3 DOS\n\n");
    printf("Usage: %s [/R 11025|22050|44100] [/B 8|16] [/C MONO|STEREO] [/L] file.MOD\n\n", name);
    printf("Defaults: /R 22050 /B 8 /C MONO\n");
    printf("  /R, /RATE      Source playback rate in Hz (11025, 22050, or 44100)\n");
    printf("  /B, /BITS      Source sample depth (8 or 16 bit)\n");
    printf("  /C, /CHANNELS  Source output channels (MONO or STEREO)\n");
    printf("  /L, /LOG       Write diagnostics and one-second statistics to MODPLAY.LOG\n");
    printf("  /H, /HELP      Display this help and exit\n\n");
    printf("Only four-channel, 31-sample ProTracker MOD files are accepted.\n");
    printf("Supported tags: M.K., M!K!, FLT4, 4CHN, and N.T.\n");
    printf("Press Space to pause/resume and Esc to stop playback.\n");
}

/* -------------------------------------------------------------------------- */
/* MOD file handling: DOS paths, signature validation, and ProTracker parsing. */
/* -------------------------------------------------------------------------- */
static bool has_mod_extension(const char *path)
{
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *last_separator = slash;

    if (backslash && (!last_separator || backslash > last_separator)) {
        last_separator = backslash;
    }
    if (!dot || (last_separator && dot < last_separator)) {
        return false;
    }
    return equals_ignore_case(dot, ".MOD");
}

static Uint16 read_be16(const Uint8 *bytes)
{
    return (Uint16)(((Uint16)bytes[0] << 8) | (Uint16)bytes[1]);
}

static void copy_header_text(char *destination, size_t capacity,
                             const Uint8 *source, size_t source_length)
{
    size_t i;
    size_t length = source_length;

    if (capacity == 0) {
        return;
    }
    if (length >= capacity) {
        length = capacity - 1;
    }
    for (i = 0; i < length; ++i) {
        const unsigned char c = source[i];
        destination[i] = (char)((c >= 32 && c <= 126) ? c : ' ');
    }
    while (length > 0 && destination[length - 1] == ' ') {
        --length;
    }
    destination[length] = '\0';
}

static bool supported_signature(const Uint8 *signature)
{
    static const char *const signatures[] = { "M.K.", "M!K!", "FLT4", "4CHN", "N.T." };
    size_t i;
    size_t j;

    for (i = 0; i < ARRAY_COUNT(signatures); ++i) {
        for (j = 0; j < 4; ++j) {
            if (toupper((unsigned char)signature[j]) != signatures[i][j]) {
                break;
            }
        }
        if (j == 4) {
            return true;
        }
    }
    return false;
}

/*
 * Load and validate the complete file before exposing any pattern/sample data.
 * On success, file_data owns the allocation and the internal pointers refer to
 * it; free_module must be called once, including after later setup failures.
 */
static bool load_module(const char *path, ModModule *module, char *error, size_t error_capacity)
{
    FILE *file = NULL;
    long file_length;
    size_t patterns_offset = 1084;
    size_t sample_data_offset;
    Uint32 total_sample_bytes = 0;
    Uint8 max_pattern = 0;
    size_t i;

    memset(module, 0, sizeof(*module));
    if (!has_mod_extension(path)) {
        (void)snprintf(error, error_capacity, "The input filename must end in .MOD.");
        return false;
    }

    file = fopen(path, "rb");
    if (!file) {
        (void)snprintf(error, error_capacity, "Could not open module file.");
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (file_length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        (void)snprintf(error, error_capacity, "Could not determine module file size.");
        fclose(file);
        return false;
    }
    if ((unsigned long)file_length > MAX_MODULE_BYTES) {
        (void)snprintf(error, error_capacity, "Module is larger than the 16 MB safety limit.");
        fclose(file);
        return false;
    }
    if (file_length < 1084) {
        (void)snprintf(error, error_capacity, "File is too short to contain a 31-sample MOD header.");
        fclose(file);
        return false;
    }

    module->file_size = (size_t)file_length;
    module->file_data = (Uint8 *)malloc(module->file_size);
    if (!module->file_data) {
        (void)snprintf(error, error_capacity, "Not enough memory to load the module.");
        fclose(file);
        return false;
    }
    if (fread(module->file_data, 1, module->file_size, file) != module->file_size) {
        (void)snprintf(error, error_capacity, "Could not read the complete module file.");
        fclose(file);
        free(module->file_data);
        module->file_data = NULL;
        return false;
    }
    fclose(file);

    if (!supported_signature(module->file_data + 1080)) {
        (void)snprintf(error, error_capacity,
                       "Unsupported MOD signature (expected a four-channel ProTracker tag).");
        free(module->file_data);
        module->file_data = NULL;
        return false;
    }
    copy_header_text(module->title, sizeof(module->title), module->file_data, 20);
    memcpy(module->signature, module->file_data + 1080, 4);
    module->signature[4] = '\0';
    module->song_length = module->file_data[950];
    module->restart_position = module->file_data[951];
    if (module->song_length == 0 || module->song_length > MOD_ORDER_COUNT) {
        (void)snprintf(error, error_capacity, "The MOD song length is invalid.");
        free(module->file_data);
        module->file_data = NULL;
        return false;
    }
    if (module->restart_position >= module->song_length) {
        module->restart_position = 0;
    }
    memcpy(module->orders, module->file_data + 952, MOD_ORDER_COUNT);
    for (i = 0; i < module->song_length; ++i) {
        if (module->orders[i] >= MOD_PATTERN_COUNT) {
            (void)snprintf(error, error_capacity, "The MOD order table references an invalid pattern.");
            free(module->file_data);
            module->file_data = NULL;
            return false;
        }
        if (module->orders[i] > max_pattern) {
            max_pattern = module->orders[i];
        }
    }
    module->pattern_count = (Uint16)(max_pattern + 1U);
    if (patterns_offset + ((size_t)module->pattern_count * MOD_PATTERN_BYTES) > module->file_size) {
        (void)snprintf(error, error_capacity, "The MOD pattern data is incomplete.");
        free(module->file_data);
        module->file_data = NULL;
        return false;
    }
    module->patterns = module->file_data + patterns_offset;
    sample_data_offset = patterns_offset + ((size_t)module->pattern_count * MOD_PATTERN_BYTES);

    for (i = 0; i < MOD_SAMPLE_COUNT; ++i) {
        const Uint8 *header = module->file_data + 20 + (i * 30);
        ModSample *sample = &module->samples[i];
        Uint16 sample_words = read_be16(header + 22);
        Uint16 loop_start_words = read_be16(header + 26);
        Uint16 loop_length_words = read_be16(header + 28);
        Uint32 loop_end;

        copy_header_text(sample->name, sizeof(sample->name), header, 22);
        sample->length = (Uint32)sample_words * 2U;
        sample->finetune = (Uint8)(header[24] & 0x0FU);
        sample->volume = (header[25] > 64U) ? 64U : header[25];
        sample->loop_start = (Uint32)loop_start_words * 2U;
        sample->loop_length = (Uint32)loop_length_words * 2U;
        if (sample->loop_length <= 2U || sample->loop_start >= sample->length) {
            sample->loop_length = 0;
            sample->loop_start = 0;
        } else {
            loop_end = sample->loop_start + sample->loop_length;
            if (loop_end > sample->length) {
                sample->loop_length = sample->length - sample->loop_start;
            }
            if (sample->loop_length <= 2U) {
                sample->loop_length = 0;
                sample->loop_start = 0;
            }
        }
        if ((Uint32)(module->file_size - sample_data_offset) < sample->length) {
            (void)snprintf(error, error_capacity, "Sample data extends beyond the end of the file.");
            free(module->file_data);
            module->file_data = NULL;
            return false;
        }
        sample->data = sample->length ? module->file_data + sample_data_offset : NULL;
        sample_data_offset += sample->length;
        total_sample_bytes += sample->length;
    }
    (void)total_sample_bytes;
    return true;
}

/* Release the file buffer that backs the pattern and sample pointers. */
static void free_module(ModModule *module)
{
    free(module->file_data);
    module->file_data = NULL;
}

static ModCell read_cell(const ModModule *module, Uint16 order, Uint8 row, Uint8 channel)
{
    ModCell cell;
    const Uint8 pattern_number = module->orders[order];
    const size_t offset = ((size_t)pattern_number * MOD_PATTERN_BYTES) +
                          ((size_t)row * MOD_CHANNEL_COUNT * 4U) + ((size_t)channel * 4U);
    const Uint8 *data = module->patterns + offset;

    cell.sample = (Uint8)((data[0] & 0xF0U) | ((data[2] & 0xF0U) >> 4));
    cell.period = (Uint16)(((Uint16)(data[0] & 0x0FU) << 8) | data[1]);
    cell.effect = (Uint8)(data[2] & 0x0FU);
    cell.parameter = data[3];
    return cell;
}

/* -------------------------------------------------------------------------- */
/* Tracker playback: periods, samples, channel state, effects, and row timing. */
/* -------------------------------------------------------------------------- */
static Uint16 apply_finetune(Uint16 period, Uint8 finetune)
{
    Uint32 adjusted;

    adjusted = ((Uint32)period * finetune_ratio[finetune & 0x0FU] + 32768U) >> 16;
    if (adjusted == 0) {
        adjusted = 1;
    }
    if (adjusted > 4095U) {
        adjusted = 4095U;
    }
    return (Uint16)adjusted;
}

static Uint32 period_to_step(const ModPlayer *player, Uint16 period, Uint8 finetune)
{
    Uint16 adjusted_period;
    Uint64 numerator;
    Uint64 denominator;
    Uint64 result;

    if (period == 0 || player->sample_rate == 0) {
        return 0;
    }
    adjusted_period = apply_finetune(period, finetune);
    numerator = (Uint64)7093789UL * 32768UL;
    denominator = (Uint64)adjusted_period * player->sample_rate;
    if (denominator == 0) {
        return 0;
    }
    result = numerator / denominator;
    if (result == 0) {
        result = 1;
    }
    if (result > 0xFFFFFFFFUL) {
        result = 0xFFFFFFFFUL;
    }
    return (Uint32)result;
}

static int period_to_note(Uint16 period)
{
    unsigned int i;
    unsigned int best_index = 0;
    unsigned int best_difference = UINT_MAX;

    if (period == 0) {
        return -1;
    }
    for (i = 0; i < ARRAY_COUNT(period_table); ++i) {
        const unsigned int table_period = period_table[i];
        const unsigned int difference = (period > table_period)
                                           ? (unsigned int)period - table_period
                                           : table_period - (unsigned int)period;
        if (difference < best_difference) {
            best_difference = difference;
            best_index = i;
        }
    }
    return (int)best_index;
}

static void set_channel_step(ModPlayer *player, PlayerChannel *channel, Uint16 period)
{
    channel->step_fp = period_to_step(player, period, channel->finetune);
}

static void trigger_note(ModPlayer *player, PlayerChannel *channel, Uint16 period, Uint32 offset)
{
    const ModSample *sample;

    if (channel->instrument == 0 || channel->instrument > MOD_SAMPLE_COUNT || period == 0) {
        return;
    }
    sample = &player->module->samples[channel->instrument - 1U];
    channel->playing_sample = channel->instrument;
    channel->period = period;
    channel->row_base_period = period;
    channel->sample_position = offset;
    channel->sample_fraction = 0;
    channel->active = (sample->data != NULL && offset < sample->length);
    channel->step_fp = period_to_step(player, period, channel->finetune);
}

static void set_channel_volume(PlayerChannel *channel, int volume)
{
    if (volume < 0) {
        volume = 0;
    } else if (volume > 64) {
        volume = 64;
    }
    channel->volume = (Uint8)volume;
}

static void apply_volume_slide(PlayerChannel *channel, Uint8 parameter)
{
    const int up = (parameter >> 4) & 0x0F;
    const int down = parameter & 0x0F;

    if (up != 0) {
        set_channel_volume(channel, (int)channel->volume + up);
    } else if (down != 0) {
        set_channel_volume(channel, (int)channel->volume - down);
    }
}

static Uint8 effect_parameter_with_memory(PlayerChannel *channel, Uint8 effect, Uint8 parameter)
{
    if (effect == 0x1U || effect == 0x2U || effect == 0x3U || effect == 0x4U ||
        effect == 0x5U || effect == 0x6U || effect == 0x9U || effect == 0xAU) {
        if (parameter == 0) {
            parameter = channel->effect_memory[effect];
        } else {
            channel->effect_memory[effect] = parameter;
        }
    }
    return parameter;
}

static void set_order_break(ModPlayer *player, int order, int row)
{
    if (order < 0) {
        order = 0;
    }
    if (order >= player->module->song_length) {
        order = player->module->restart_position;
    }
    if (row < 0) {
        row = 0;
    }
    if (row >= MOD_ROWS_PER_PATTERN) {
        row = 0;
    }
    player->pending_order = order;
    player->pending_row = row;
}

static void apply_row_effect(ModPlayer *player, unsigned int channel_number,
                             const ModCell *cell, Uint8 parameter)
{
    PlayerChannel *channel = &player->channels[channel_number];
    const Uint8 effect = cell->effect;
    const Uint8 sub_effect = (Uint8)(parameter >> 4);
    const Uint8 sub_parameter = (Uint8)(parameter & 0x0FU);

    switch (effect) {
    case 0x0:
        break;
    case 0x3:
        if (parameter != 0) {
            channel->effect_memory[0x3] = parameter;
        }
        break;
    case 0x4:
    case 0x6:
        if ((parameter >> 4) != 0) {
            channel->vibrato_speed = (Uint8)(parameter >> 4);
        }
        if ((parameter & 0x0FU) != 0) {
            channel->vibrato_depth = (Uint8)(parameter & 0x0FU);
        }
        break;
    case 0x9:
        if (cell->period != 0) {
            trigger_note(player, channel, cell->period, (Uint32)parameter * 256U);
        }
        break;
    case 0xB:
        set_order_break(player, parameter, 0);
        break;
    case 0xC:
        set_channel_volume(channel, parameter & 0x7FU);
        break;
    case 0xD: {
        int break_row = ((parameter >> 4) * 10) + (parameter & 0x0F);
        const int break_order = (player->pending_order >= 0)
                                    ? player->pending_order
                                    : (int)player->order + 1;
        set_order_break(player, break_order, break_row);
        break;
    }
    case 0xE:
        switch (sub_effect) {
        case 0x1:
            if (channel->period > sub_parameter) {
                channel->period = (Uint16)(channel->period - sub_parameter);
                set_channel_step(player, channel, channel->period);
            }
            break;
        case 0x2:
            channel->period = (Uint16)(channel->period + sub_parameter);
            set_channel_step(player, channel, channel->period);
            break;
        case 0x5:
            channel->finetune = sub_parameter;
            set_channel_step(player, channel, channel->period);
            break;
        case 0x6:
            if (sub_parameter == 0) {
                channel->loop_row = player->row;
            } else if (channel->loop_count == 0) {
                channel->loop_count = sub_parameter;
                set_order_break(player, player->order, channel->loop_row);
            } else {
                --channel->loop_count;
                if (channel->loop_count > 0) {
                    set_order_break(player, player->order, channel->loop_row);
                }
            }
            break;
        case 0x9:
            break;
        case 0xA:
            set_channel_volume(channel, (int)channel->volume + sub_parameter);
            break;
        case 0xB:
            set_channel_volume(channel, (int)channel->volume - sub_parameter);
            break;
        case 0xC:
            if (sub_parameter == 0) {
                set_channel_volume(channel, 0);
            }
            break;
        case 0xD:
            if (sub_parameter == 0 && cell->period != 0) {
                trigger_note(player, channel, cell->period, 0);
            }
            break;
        default:
            break;
        }
        break;
    case 0xF:
        if (parameter != 0) {
            if (parameter < 32U) {
                player->speed = parameter;
            } else {
                player->bpm = parameter;
            }
        }
        break;
    default:
        break;
    }
}

static Uint32 next_tick_length(ModPlayer *player)
{
    const Uint64 numerator = (Uint64)player->sample_rate * 5ULL * 65536ULL;
    const Uint64 denominator = (Uint64)player->bpm * 2ULL;
    Uint32 fixed_length;
    Uint32 frames;

    if (denominator == 0) {
        return 1;
    }
    fixed_length = (Uint32)(numerator / denominator);
    player->tick_fraction += fixed_length;
    frames = player->tick_fraction >> 16;
    player->tick_fraction &= 0xFFFFU;
    return (frames == 0) ? 1U : frames;
}

/* Decode one pattern row, trigger notes, and apply effects that start at row boundaries. */
static void process_row(ModPlayer *player)
{
    unsigned int i;

    player->pending_order = -1;
    player->pending_row = -1;
    for (i = 0; i < MOD_CHANNEL_COUNT; ++i) {
        PlayerChannel *channel = &player->channels[i];
        const ModCell cell = read_cell(player->module, player->order, player->row, (Uint8)i);
        Uint8 parameter;
        bool delayed_note;
        Uint8 delay_tick = 0;

        channel->display_sample = cell.sample ? cell.sample : channel->instrument;
        channel->display_period = cell.period;
        channel->display_effect = cell.effect;
        channel->display_parameter = cell.parameter;
        channel->effect = cell.effect;
        parameter = effect_parameter_with_memory(channel, cell.effect, cell.parameter);
        channel->parameter = parameter;

        if (cell.sample > 0 && cell.sample <= MOD_SAMPLE_COUNT) {
            const ModSample *sample = &player->module->samples[cell.sample - 1U];
            channel->instrument = cell.sample;
            channel->volume = sample->volume;
            channel->finetune = sample->finetune;
        }
        if (channel->period == 0) {
            channel->row_base_period = channel->period;
        }

        delayed_note = (cell.effect == 0xEU && (cell.parameter >> 4) == 0xDU &&
                        cell.parameter != 0 && cell.period != 0);
        if (delayed_note) {
            delay_tick = (Uint8)(cell.parameter & 0x0FU);
            channel->delayed_note_pending = true;
            channel->delayed_period = cell.period;
            channel->delayed_tick = delay_tick;
        } else {
            channel->delayed_note_pending = false;
            if (cell.period != 0 && cell.effect != 0x3U && cell.effect != 0x5U &&
                cell.effect != 0x9U &&
                !(cell.effect == 0xEU && (cell.parameter >> 4) == 0xDU)) {
                trigger_note(player, channel, cell.period, 0);
            } else if (cell.period != 0 && (cell.effect == 0x3U || cell.effect == 0x5U)) {
                channel->target_period = cell.period;
                if (channel->period == 0) {
                    trigger_note(player, channel, cell.period, 0);
                    channel->target_period = 0;
                }
            }
        }
        channel->row_base_period = channel->period;
        apply_row_effect(player, i, &cell, parameter);
    }
    player->tick = 0;
    player->tick_samples_left = next_tick_length(player);
}

static void advance_row(ModPlayer *player)
{
    if (player->pending_order >= 0) {
        player->order = (Uint16)player->pending_order;
        player->row = (Uint8)((player->pending_row >= 0) ? player->pending_row : 0);
    } else {
        ++player->row;
        if (player->row >= MOD_ROWS_PER_PATTERN) {
            player->row = 0;
            ++player->order;
        }
        if (player->order >= player->module->song_length) {
            player->order = player->module->restart_position;
            player->row = 0;
            ++player->song_loops;
        }
    }
}

static void update_period_slide(ModPlayer *player, PlayerChannel *channel, bool upward)
{
    const Uint16 amount = channel->parameter;

    if (upward) {
        channel->period = (channel->period > amount) ? (Uint16)(channel->period - amount) : 113U;
    } else {
        Uint32 next_period = (Uint32)channel->period + amount;
        channel->period = (next_period > 4095U) ? 4095U : (Uint16)next_period;
    }
    set_channel_step(player, channel, channel->period);
}

static void update_portamento(ModPlayer *player, PlayerChannel *channel, Uint8 speed)
{
    if (channel->target_period == 0) {
        return;
    }
    if (channel->period < channel->target_period) {
        Uint32 next = (Uint32)channel->period + speed;
        channel->period = (next >= channel->target_period) ? channel->target_period : (Uint16)next;
    } else if (channel->period > channel->target_period) {
        channel->period = (channel->period - channel->target_period <= speed)
                              ? channel->target_period
                              : (Uint16)(channel->period - speed);
    }
    set_channel_step(player, channel, channel->period);
}

static void update_vibrato(ModPlayer *player, PlayerChannel *channel)
{
    static const Sint8 wave[32] = {
        0, 12, 24, 36, 46, 54, 60, 63, 64, 63, 60, 54, 46, 36, 24, 12,
        0, -12, -24, -36, -46, -54, -60, -63, -64, -63, -60, -54, -46, -36, -24, -12
    };
    const int displacement = ((int)wave[channel->vibrato_phase & 31U] *
                              (int)channel->vibrato_depth) / 64;
    int vibrated_period = (int)channel->period + displacement;

    if (vibrated_period < 113) {
        vibrated_period = 113;
    } else if (vibrated_period > 4095) {
        vibrated_period = 4095;
    }
    channel->step_fp = period_to_step(player, (Uint16)vibrated_period, channel->finetune);
    channel->vibrato_phase = (Uint8)((channel->vibrato_phase + channel->vibrato_speed) & 31U);
}

static void update_tick_effects(ModPlayer *player)
{
    unsigned int i;

    for (i = 0; i < MOD_CHANNEL_COUNT; ++i) {
        PlayerChannel *channel = &player->channels[i];
        const Uint8 effect = channel->effect;
        const Uint8 parameter = channel->parameter;

        if (channel->delayed_note_pending && player->tick == channel->delayed_tick) {
            trigger_note(player, channel, channel->delayed_period, 0);
            channel->delayed_note_pending = false;
        }
        switch (effect) {
        case 0x0:
            if (parameter != 0 && channel->row_base_period != 0) {
                int note_index = period_to_note(channel->row_base_period);
                const Uint8 semitone = (player->tick % 3U == 1U)
                                           ? (Uint8)(parameter >> 4)
                                           : ((player->tick % 3U == 2U)
                                                  ? (Uint8)(parameter & 0x0FU)
                                                  : 0U);
                if (note_index >= 0) {
                    note_index += semitone;
                    if (note_index >= (int)ARRAY_COUNT(period_table)) {
                        note_index = (int)ARRAY_COUNT(period_table) - 1;
                    }
                    set_channel_step(player, channel, period_table[note_index]);
                }
            }
            break;
        case 0x1:
            if (player->tick > 0 && parameter != 0) {
                update_period_slide(player, channel, true);
            }
            break;
        case 0x2:
            if (player->tick > 0 && parameter != 0) {
                update_period_slide(player, channel, false);
            }
            break;
        case 0x3:
            if (player->tick > 0) {
                update_portamento(player, channel, channel->parameter);
            }
            break;
        case 0x4:
            if (channel->period != 0) {
                update_vibrato(player, channel);
            }
            break;
        case 0x5:
            if (player->tick > 0) {
                update_portamento(player, channel, channel->effect_memory[0x3]);
                apply_volume_slide(channel, parameter);
            }
            break;
        case 0x6:
            if (channel->period != 0) {
                update_vibrato(player, channel);
            }
            if (player->tick > 0) {
                apply_volume_slide(channel, parameter);
            }
            break;
        case 0x9:
            if (player->tick > 0 && parameter > 0 && (player->tick % parameter) == 0) {
                channel->sample_position = 0;
                channel->sample_fraction = 0;
                channel->active = (channel->playing_sample != 0);
            }
            break;
        case 0xA:
            if (player->tick > 0) {
                apply_volume_slide(channel, parameter);
            }
            break;
        case 0xE: {
            const Uint8 sub_effect = (Uint8)(channel->display_parameter >> 4);
            const Uint8 sub_parameter = (Uint8)(channel->display_parameter & 0x0FU);
            if (sub_effect == 0x9U && sub_parameter > 0 && player->tick > 0 &&
                (player->tick % sub_parameter) == 0) {
                channel->sample_position = 0;
                channel->sample_fraction = 0;
                channel->active = (channel->playing_sample != 0);
            } else if (sub_effect == 0xCU && player->tick == sub_parameter) {
                set_channel_volume(channel, 0);
            }
            break;
        }
        default:
            break;
        }
    }
}

/* Advance the ProTracker tick clock, then update row/tick effects at their proper cadence. */
static void advance_tracker_tick(ModPlayer *player)
{
    if (player->tick + 1U < player->speed) {
        ++player->tick;
        update_tick_effects(player);
        player->tick_samples_left = next_tick_length(player);
    } else {
        advance_row(player);
        process_row(player);
    }
}

/* Set the initial song position, speed/BPM, output format, and four silent channels. */
static void initialize_player(ModPlayer *player, const ModModule *module,
                              Uint32 sample_rate, Uint8 bits, Uint8 channels)
{
    memset(player, 0, sizeof(*player));
    player->module = module;
    player->sample_rate = sample_rate;
    player->output_bits = bits;
    player->output_channels = channels;
    player->speed = 6;
    player->bpm = 125;
    player->pending_order = -1;
    player->pending_row = -1;
    process_row(player);
}

static inline bool normalize_sample_position(const ModSample *sample,
                                            PlayerChannel *channel,
                                            Uint32 *loop_end_out)
{
    if (sample->loop_length > 2U) {
        const Uint32 loop_end = sample->loop_start + sample->loop_length;
        *loop_end_out = loop_end;
        if (channel->sample_position >= loop_end) {
            /* Normal playback crosses the loop by only a few bytes; avoid a divide in the hot path. */
            channel->sample_position -= sample->loop_length;
            if (channel->sample_position >= loop_end) {
                channel->sample_position = sample->loop_start +
                    ((channel->sample_position - sample->loop_start) % sample->loop_length);
            }
        }
    } else {
        *loop_end_out = sample->length;
        if (channel->sample_position >= sample->length) {
            channel->active = false;
            return false;
        }
    }
    if (channel->sample_position >= sample->length) {
        channel->active = false;
        return false;
    }
    return true;
}

static inline void advance_sample_position(PlayerChannel *channel)
{
    const Uint32 fractional_sum = (Uint32)channel->sample_fraction +
                                  (channel->step_fp & 0xFFFFU);
    channel->sample_position += (channel->step_fp >> 16) + (fractional_sum >> 16);
    channel->sample_fraction = (Uint16)(fractional_sum & 0xFFFFU);
}

static inline Sint32 read_channel_sample_nearest(const ModModule *module,
                                                 PlayerChannel *channel)
{
    const ModSample *sample;
    Uint32 loop_end;
    Sint32 value;

    if (!channel->active || channel->playing_sample == 0 ||
        channel->playing_sample > MOD_SAMPLE_COUNT) {
        return 0;
    }
    sample = &module->samples[channel->playing_sample - 1U];
    if (!sample->data || sample->length == 0 ||
        !normalize_sample_position(sample, channel, &loop_end)) {
        channel->active = false;
        return 0;
    }
    (void)loop_end;
    if (channel->volume == 0) {
        advance_sample_position(channel);
        return 0;
    }
    value = (Sint8)sample->data[channel->sample_position];
    value = (value * (Sint32)channel->volume) / 64;
    advance_sample_position(channel);
    return value;
}

static inline Sint32 read_channel_sample_linear(const ModModule *module,
                                                PlayerChannel *channel)
{
    const ModSample *sample;
    Uint32 loop_end;
    Uint32 next_position;
    Sint32 first;
    Sint32 second;
    Sint32 interpolated;

    if (!channel->active || channel->playing_sample == 0 ||
        channel->playing_sample > MOD_SAMPLE_COUNT) {
        return 0;
    }
    sample = &module->samples[channel->playing_sample - 1U];
    if (!sample->data || sample->length == 0 ||
        !normalize_sample_position(sample, channel, &loop_end)) {
        channel->active = false;
        return 0;
    }
    if (channel->volume == 0) {
        advance_sample_position(channel);
        return 0;
    }
    next_position = channel->sample_position + 1U;
    if (sample->loop_length > 2U && next_position >= loop_end) {
        next_position = sample->loop_start;
    } else if (next_position >= sample->length) {
        next_position = channel->sample_position;
    }
    first = (Sint8)sample->data[channel->sample_position];
    second = (Sint8)sample->data[next_position];
    interpolated = first + (((second - first) * (Sint32)channel->sample_fraction) >> 16);
    interpolated = (interpolated * (Sint32)channel->volume) / 64;
    advance_sample_position(channel);
    return interpolated;
}

static inline void write_output_sample(Uint8 *buffer, size_t offset,
                                       Sint32 value, Uint8 bits)
{
    if (bits == 8) {
        if (value > 127) {
            value = 127;
        } else if (value < -128) {
            value = -128;
        }
        buffer[offset] = (Uint8)(value + 128);
    } else {
        Sint16 sample;
        if (value > 32767) {
            value = 32767;
        } else if (value < -32768) {
            value = -32768;
        }
        sample = (Sint16)value;
        memcpy(buffer + offset, &sample, sizeof(sample));
    }
}

static inline void prepare_audio_frame(ModPlayer *player)
{
    if (player->tick_samples_left == 0) {
        if (player->tick == 0 && player->generated_frames == 0) {
            process_row(player);
        } else {
            advance_tracker_tick(player);
        }
    }
}

static inline void finish_audio_frame(ModPlayer *player)
{
    if (player->tick_samples_left > 0) {
        --player->tick_samples_left;
    }
    ++player->generated_frames;
}

/*
 * Mix the requested number of frames in the selected source format. Tracker
 * ticks advance on generated frames, so audio generation drives musical time.
 */
static void generate_audio_block(ModPlayer *player, Uint8 *buffer, Uint32 frames)
{
    Uint32 frame;
    const ModModule *module = player->module;
    const size_t sample_bytes = (player->output_bits == 8U) ? 1U : 2U;

    /* The low-cost 8-bit mono path uses nearest-neighbour sample playback. */
    if (player->output_bits == 8U && player->output_channels == 1U) {
        for (frame = 0; frame < frames; ++frame) {
            Sint32 mixed;
            prepare_audio_frame(player);
            mixed = read_channel_sample_nearest(module, &player->channels[0]) +
                    read_channel_sample_nearest(module, &player->channels[1]) +
                    read_channel_sample_nearest(module, &player->channels[2]) +
                    read_channel_sample_nearest(module, &player->channels[3]);
            buffer[frame] = (Uint8)(128 + (mixed / 4));
            finish_audio_frame(player);
        }
        return;
    }

    for (frame = 0; frame < frames; ++frame) {
        Sint32 channel_sample[MOD_CHANNEL_COUNT];
        Sint32 left;
        Sint32 right;
        Sint32 mono;
        unsigned int channel;
        const size_t output_base = (size_t)frame * sample_bytes * player->output_channels;

        prepare_audio_frame(player);
        for (channel = 0; channel < MOD_CHANNEL_COUNT; ++channel) {
            channel_sample[channel] = (player->output_bits == 8U)
                ? read_channel_sample_nearest(module, &player->channels[channel])
                : read_channel_sample_linear(module, &player->channels[channel]);
        }
        left = channel_sample[0] + channel_sample[3];
        right = channel_sample[1] + channel_sample[2];
        mono = channel_sample[0] + channel_sample[1] + channel_sample[2] + channel_sample[3];

        if (player->output_bits == 8U) {
            write_output_sample(buffer, output_base, left / 2, player->output_bits);
            write_output_sample(buffer, output_base + 1U, right / 2, player->output_bits);
        } else if (player->output_channels == 1U) {
            write_output_sample(buffer, output_base, (mono / 4) * 256, player->output_bits);
        } else {
            write_output_sample(buffer, output_base, (left / 2) * 256, player->output_bits);
            write_output_sample(buffer, output_base + 2U, (right / 2) * 256, player->output_bits);
        }
        finish_audio_frame(player);
    }
}

/* -------------------------------------------------------------------------- */
/* Audio generation and queueing: four-voice PCM mix and SDL stream feeding.  */
/* -------------------------------------------------------------------------- */
static Uint32 bytes_per_source_frame(const AppSettings *settings)
{
    return (Uint32)((settings->bits / 8U) * settings->channels);
}

static Uint32 audio_chunk_frames(Uint32 rate)
{
    Uint32 frames = rate / 44U;
    if (frames < 256U) {
        frames = 256U;
    } else if (frames > AUDIO_MAX_CHUNK_FRAMES) {
        frames = AUDIO_MAX_CHUNK_FRAMES;
    }
    return frames;
}

/* Refill SDL's source queue in rate-scaled chunks until its time target is met. */
static bool queue_audio_to_target(SDL_AudioStream *stream, ModPlayer *player,
                                  const AppSettings *settings, Uint32 *queued_frames,
                                  Uint32 *target_frames)
{
    Uint8 buffer[AUDIO_MAX_CHUNK_FRAMES * 4];
    const Uint32 frame_bytes = bytes_per_source_frame(settings);
    const Uint32 chunk_frames = audio_chunk_frames(settings->rate);
    const Uint32 queue_target = (settings->rate * AUDIO_QUEUE_TARGET_MS) / 1000U;
    int queued_bytes = SDL_GetAudioStreamQueued(stream);
    Uint32 frames;

    if (queued_bytes < 0 || frame_bytes == 0) {
        LOG("SDL_GetAudioStreamQueued failed: %s", SDL_GetError());
        return false;
    }
    frames = (Uint32)queued_bytes / frame_bytes;
    *queued_frames = frames;
    *target_frames = queue_target;

    while (*queued_frames < queue_target) {
        const Uint32 chunk_bytes = chunk_frames * frame_bytes;
        generate_audio_block(player, buffer, chunk_frames);
        if (!SDL_PutAudioStreamData(stream, buffer, (int)chunk_bytes)) {
            LOG("SDL_PutAudioStreamData failed: %s", SDL_GetError());
            return false;
        }
        *queued_frames += chunk_frames;
    }
    return true;
}

static const char *audio_format_label(Uint8 bits)
{
    return (bits == 8U) ? "U8" : "S16LE";
}

static const char *channel_label(Uint8 channels)
{
    return (channels == 1U) ? "MONO" : "STEREO";
}

static const char *sdl_format_label(SDL_AudioFormat format)
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

/* -------------------------------------------------------------------------- */
/* Screen primitives: SDL window, palette, bitmap fonts, and drawing helpers. */
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
    colors[COLOR_WHITE].r = 232; colors[COLOR_WHITE].g = 244; colors[COLOR_WHITE].b = 255;
    colors[COLOR_CYAN].r = 48; colors[COLOR_CYAN].g = 220; colors[COLOR_CYAN].b = 255;
    colors[COLOR_GREEN].r = 88; colors[COLOR_GREEN].g = 255; colors[COLOR_GREEN].b = 128;
    colors[COLOR_YELLOW].r = 255; colors[COLOR_YELLOW].g = 224; colors[COLOR_YELLOW].b = 80;
    colors[COLOR_RED].r = 255; colors[COLOR_RED].g = 80; colors[COLOR_RED].b = 72;
    colors[COLOR_BLUE].r = 104; colors[COLOR_BLUE].g = 144; colors[COLOR_BLUE].b = 255;
    colors[COLOR_DIM].r = 32; colors[COLOR_DIM].g = 80; colors[COLOR_DIM].b = 96;
    colors[COLOR_HIGHLIGHT].r = 0; colors[COLOR_HIGHLIGHT].g = 52; colors[COLOR_HIGHLIGHT].b = 72;
    colors[COLOR_BORDER].r = 20; colors[COLOR_BORDER].g = 128; colors[COLOR_BORDER].b = 144;
    return SDL_SetPaletteColors(palette, colors, 0, 256);
}

static bool create_window(SDL_Window **window, SDL_Surface **window_surface)
{
    SDL_PropertiesID properties = SDL_CreateProperties();

    if (!properties) {
        fprintf(stderr, "Error: could not create SDL window properties: %s\n", SDL_GetError());
        return false;
    }
    if (!SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING,
                               "MODPLAY - SDL3 DOS") ||
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

static void put_pixel(SDL_Surface *surface, int x, int y, Uint8 color)
{
    Uint8 *row;

    if (x < 0 || x >= surface->w || y < 0 || y >= surface->h) {
        return;
    }
    row = (Uint8 *)surface->pixels + (y * surface->pitch);
    row[x] = color;
}

static void fill_rect(SDL_Surface *surface, int x, int y, int width, int height, Uint8 color)
{
    int row;

    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x + width > surface->w) {
        width = surface->w - x;
    }
    if (y + height > surface->h) {
        height = surface->h - y;
    }
    if (width <= 0 || height <= 0) {
        return;
    }
    for (row = 0; row < height; ++row) {
        memset((Uint8 *)surface->pixels + ((y + row) * surface->pitch) + x,
               color, (size_t)width);
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

static void get_glyph5(char character, Uint8 rows[7])
{
    typedef struct Glyph5 {
        char character;
        Uint8 rows[7];
    } Glyph5;
    static const Glyph5 glyphs[] = {
        { 'A', {14,17,17,31,17,17,17} }, { 'B', {30,17,17,30,17,17,30} },
        { 'C', {14,17,16,16,16,17,14} }, { 'D', {30,17,17,17,17,17,30} },
        { 'E', {31,16,16,30,16,16,31} }, { 'F', {31,16,16,30,16,16,16} },
        { 'G', {14,17,16,23,17,17,15} }, { 'H', {17,17,17,31,17,17,17} },
        { 'I', {31,4,4,4,4,4,31} }, { 'J', {7,2,2,2,18,18,12} },
        { 'K', {17,18,20,24,20,18,17} }, { 'L', {16,16,16,16,16,16,31} },
        { 'M', {17,27,21,21,17,17,17} }, { 'N', {17,25,21,19,17,17,17} },
        { 'O', {14,17,17,17,17,17,14} }, { 'P', {30,17,17,30,16,16,16} },
        { 'Q', {14,17,17,17,21,18,13} }, { 'R', {30,17,17,30,20,18,17} },
        { 'S', {15,16,16,14,1,1,30} }, { 'T', {31,4,4,4,4,4,4} },
        { 'U', {17,17,17,17,17,17,14} }, { 'V', {17,17,17,17,17,10,4} },
        { 'W', {17,17,17,21,21,21,10} }, { 'X', {17,17,10,4,10,17,17} },
        { 'Y', {17,17,10,4,4,4,4} }, { 'Z', {31,1,2,4,8,16,31} },
        { '0', {14,17,19,21,25,17,14} }, { '1', {4,12,4,4,4,4,14} },
        { '2', {14,17,1,2,4,8,31} }, { '3', {30,1,1,14,1,1,30} },
        { '4', {2,6,10,18,31,2,2} }, { '5', {31,16,16,30,1,1,30} },
        { '6', {14,16,16,30,17,17,14} }, { '7', {31,1,2,4,8,8,8} },
        { '8', {14,17,17,14,17,17,14} }, { '9', {14,17,17,15,1,1,14} },
        { ':', {0,4,4,0,4,4,0} }, { '=', {0,31,0,31,0,0,0} },
        { '.', {0,0,0,0,0,0,4} }, { ',', {0,0,0,0,0,4,8} },
        { '-', {0,0,0,31,0,0,0} }, { '/', {1,2,2,4,8,8,16} },
        { '_', {0,0,0,0,0,0,31} }, { '?', {14,17,1,2,4,0,4} },
        { '#', {10,31,10,10,31,10,0} }, { '+', {0,4,4,31,4,4,0} },
        { '[', {14,8,8,8,8,8,14} }, { ']', {14,2,2,2,2,2,14} },
        { '*', {0,17,10,31,10,17,0} }, { '!', {4,4,4,4,4,0,4} }
    };
    size_t i;

    memset(rows, 0, 7 * sizeof(*rows));
    character = (char)toupper((unsigned char)character);
    for (i = 0; i < ARRAY_COUNT(glyphs); ++i) {
        if (glyphs[i].character == character) {
            memcpy(rows, glyphs[i].rows, 7 * sizeof(*rows));
            return;
        }
    }
}

static void draw_text5(SDL_Surface *surface, int x, int y, const char *text, Uint8 color)
{
    const int origin_x = x;

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
        get_glyph5(*text, rows);
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

static void get_glyph4(char character, Uint8 rows[5])
{
    typedef struct Glyph4 {
        char character;
        Uint8 rows[5];
    } Glyph4;
    static const Glyph4 glyphs[] = {
        { 'A', {6,9,9,15,9} }, { 'B', {14,9,14,9,14} }, { 'C', {7,8,8,8,7} },
        { 'D', {14,9,9,9,14} }, { 'E', {15,8,14,8,15} }, { 'F', {15,8,14,8,8} },
        { 'G', {7,8,11,9,7} }, { 'H', {9,9,15,9,9} }, { 'I', {14,4,4,4,14} },
        { 'J', {3,1,1,9,6} }, { 'K', {9,10,12,10,9} }, { 'L', {8,8,8,8,15} },
        { 'M', {9,15,15,9,9} }, { 'N', {9,13,11,9,9} }, { 'O', {6,9,9,9,6} },
        { 'P', {14,9,14,8,8} }, { 'Q', {6,9,9,11,7} }, { 'R', {14,9,14,10,9} },
        { 'S', {7,8,6,1,14} }, { 'T', {15,6,6,6,6} }, { 'U', {9,9,9,9,6} },
        { 'V', {9,9,9,6,6} }, { 'W', {9,9,15,15,9} }, { 'X', {9,6,6,6,9} },
        { 'Y', {9,6,6,6,6} }, { 'Z', {15,1,2,4,15} },
        { '0', {6,9,11,13,6} }, { '1', {4,12,4,4,14} }, { '2', {14,1,6,8,15} },
        { '3', {14,1,6,1,14} }, { '4', {9,9,15,1,1} }, { '5', {15,8,14,1,14} },
        { '6', {7,8,14,9,6} }, { '7', {15,1,2,4,4} }, { '8', {6,9,6,9,6} },
        { '9', {6,9,7,1,14} }, { ':', {0,4,0,4,0} }, { '.', {0,0,0,0,4} },
        { ',', {0,0,0,4,8} }, { '-', {0,0,15,0,0} }, { '/', {1,2,2,4,8} },
        { '_', {0,0,0,0,15} }, { '?', {14,1,6,0,4} }, { '+', {0,4,14,4,0} },
        { ' ', {0,0,0,0,0} }, { '#', {10,15,10,15,10} }, { '[', {14,8,8,8,14} },
        { ']', {14,2,2,2,14} }, { '%', {9,1,2,4,9} }
    };
    size_t i;

    memset(rows, 0, 5 * sizeof(*rows));
    character = (char)toupper((unsigned char)character);
    for (i = 0; i < ARRAY_COUNT(glyphs); ++i) {
        if (glyphs[i].character == character) {
            memcpy(rows, glyphs[i].rows, 5 * sizeof(*rows));
            return;
        }
    }
}

static void draw_text4(SDL_Surface *surface, int x, int y, const char *text, Uint8 color)
{
    while (*text) {
        Uint8 rows[5];
        int row;
        int column;
        get_glyph4(*text, rows);
        for (row = 0; row < 5; ++row) {
            for (column = 0; column < 4; ++column) {
                if (rows[row] & (Uint8)(1U << (3 - column))) {
                    put_pixel(surface, x + column, y + row, color);
                }
            }
        }
        x += 5;
        ++text;
    }
}

static void sanitize_text(char *destination, size_t capacity, const char *source)
{
    size_t i = 0;

    if (capacity == 0) {
        return;
    }
    while (i + 1 < capacity && source && source[i]) {
        const unsigned char c = (unsigned char)source[i];
        destination[i] = (char)((c >= 32 && c <= 126) ? toupper(c) : ' ');
        ++i;
    }
    while (i > 0 && destination[i - 1] == ' ') {
        --i;
    }
    destination[i] = '\0';
}

/* -------------------------------------------------------------------------- */
/* Tracker display: status text, row view, sample panel, and dirty rectangles. */
/* -------------------------------------------------------------------------- */
static Uint32 active_sample_mask(const ModPlayer *player)
{
    Uint32 mask = 0;
    unsigned int i;

    for (i = 0; i < MOD_CHANNEL_COUNT; ++i) {
        const PlayerChannel *channel = &player->channels[i];
        if (channel->active && channel->playing_sample > 0 &&
            channel->playing_sample <= MOD_SAMPLE_COUNT) {
            mask |= (Uint32)(1U << (channel->playing_sample - 1U));
        }
    }
    return mask;
}

static void format_cell(const ModCell *cell, char text[9])
{
    char note[4] = "...";
    const int note_index = period_to_note(cell->period);
    const char sample_hi = (char)('0' + (cell->sample / 10U));
    const char sample_lo = (char)('0' + (cell->sample % 10U));
    const char effect = "0123456789ABCDEF"[cell->effect & 0x0F];
    const char param_hi = "0123456789ABCDEF"[(cell->parameter >> 4) & 0x0F];
    const char param_lo = "0123456789ABCDEF"[cell->parameter & 0x0F];

    if (note_index >= 0) {
        memcpy(note, note_names[note_index], 3);
    }
    if (cell->sample == 0) {
        text[0] = '.';
        text[1] = '.';
    } else {
        text[0] = sample_hi;
        text[1] = sample_lo;
    }
    text[2] = note[0];
    text[3] = note[1];
    text[4] = note[2];
    if (cell->effect == 0 && cell->parameter == 0) {
        text[5] = '-';
        text[6] = '-';
        text[7] = '-';
    } else {
        text[5] = effect;
        text[6] = param_hi;
        text[7] = param_lo;
    }
    text[8] = '\0';
}

static Uint8 pattern_view_start(Uint8 row)
{
    return (Uint8)(((unsigned int)row / 13U) * 13U);
}

static void draw_pattern_row(SDL_Surface *surface, const ModModule *module,
                             Uint16 order, unsigned int visible_row,
                             int row_number, bool current_row,
                             const int channel_x[MOD_CHANNEL_COUNT])
{
    char line[8];
    const int y = 55 + ((int)visible_row * 10);
    unsigned int channel;

    fill_rect(surface, 3, y - 1, 214, 9,
              current_row ? COLOR_HIGHLIGHT : COLOR_BACKGROUND);
    if (row_number < 0 || row_number >= MOD_ROWS_PER_PATTERN) {
        return;
    }

    (void)snprintf(line, sizeof(line), "%02X", (unsigned int)row_number);
    draw_text5(surface, 4, y, line, current_row ? COLOR_YELLOW : COLOR_DIM);
    for (channel = 0; channel < MOD_CHANNEL_COUNT; ++channel) {
        const ModCell cell = read_cell(module, order, (Uint8)row_number, (Uint8)channel);
        char cell_text[9];
        format_cell(&cell, cell_text);
        draw_text5(surface, channel_x[channel], y, cell_text,
                   current_row ? COLOR_WHITE : COLOR_DIM);
    }
}

static void draw_pattern_grid(SDL_Surface *surface, const ModModule *module,
                              Uint16 order, Uint8 row_start, Uint8 current_row,
                              const int channel_x[MOD_CHANNEL_COUNT])
{
    unsigned int visible_row;

    for (visible_row = 0; visible_row < 13U; ++visible_row) {
        const int row_number = (int)row_start + (int)visible_row;
        draw_pattern_row(surface, module, order, visible_row, row_number,
                         row_number == (int)current_row, channel_x);
    }
}

static void draw_sample_line(SDL_Surface *surface, const ModModule *module,
                             Uint32 active_samples, unsigned int sample_index)
{
    const ModSample *sample = &module->samples[sample_index];
    char sample_name[24];
    char sample_line[32];
    const Uint8 color = (active_samples & (Uint32)(1U << sample_index))
                            ? COLOR_GREEN
                            : (sample->length ? COLOR_WHITE : COLOR_DIM);
    const int y = 43 + ((int)sample_index * 5);

    fill_rect(surface, 224, y, 96, 5, COLOR_BACKGROUND);
    sanitize_text(sample_name, sizeof(sample_name), sample->name);
    if (sample_name[0] == '\0') {
        (void)snprintf(sample_name, sizeof(sample_name), "--------");
    }
    (void)snprintf(sample_line, sizeof(sample_line), "%02u:%.8s V%02u",
                   sample_index + 1U, sample_name, (unsigned int)sample->volume);
    draw_text4(surface, 225, y, sample_line, color);
}

static void add_dirty_rect(SDL_Rect rects[16], unsigned int *rect_count,
                           int x, int y, int width, int height)
{
    if (*rect_count < 16U) {
        SDL_Rect *rect = &rects[*rect_count];
        rect->x = x;
        rect->y = y;
        rect->w = width;
        rect->h = height;
        ++(*rect_count);
    }
}

static bool present_full_surface(SDL_Surface *surface, SDL_Surface *window_surface,
                                 SDL_Window *window)
{
    if (surface != window_surface && !SDL_BlitSurface(surface, NULL, window_surface, NULL)) {
        LOG("SDL_BlitSurface failed: %s", SDL_GetError());
        return false;
    }
    if (!SDL_UpdateWindowSurface(window)) {
        LOG("SDL_UpdateWindowSurface failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

static bool present_dirty_rects(SDL_Surface *surface, SDL_Surface *window_surface,
                                SDL_Window *window, const SDL_Rect *rects,
                                unsigned int rect_count)
{
    unsigned int i;

    if (surface != window_surface) {
        for (i = 0; i < rect_count; ++i) {
            SDL_Rect destination = rects[i];
            if (!SDL_BlitSurface(surface, &rects[i], window_surface, &destination)) {
                LOG("SDL_BlitSurface for dirty rect failed: %s", SDL_GetError());
                return false;
            }
        }
    }
    if (!SDL_UpdateWindowSurfaceRects(window, rects, (int)rect_count)) {
        LOG("SDL_UpdateWindowSurfaceRects failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

/* Draw the complete layout once; thereafter repaint only changing fields/rows. */
static void draw_screen(SDL_Surface *surface, SDL_Surface *window_surface, SDL_Window *window,
                        const ModModule *module, const ModPlayer *player,
                        const AppSettings *settings, const SDL_AudioSpec *output_spec,
                        bool output_format_known, const char *driver_name,
                        Uint32 queued_frames,
                        Uint32 low_queue_events, Uint32 empty_queue_events,
                        Uint32 target_frames, bool paused, bool logging_active,
                        Uint64 elapsed_ms, Uint32 loops_per_second,
                        ScreenCache *cache)
{
    char line[96];
    char title[48];
    const Uint8 row_start = pattern_view_start(player->row);
    const Uint8 pattern = module->orders[player->order];
    const Uint32 active_samples = active_sample_mask(player);
    const Uint32 queue_ms = (Uint32)(((Uint64)queued_frames * 1000U) / settings->rate);
    const Uint32 target_ms = (Uint32)(((Uint64)target_frames * 1000U) / settings->rate);
    const Uint8 status_color = paused ? COLOR_YELLOW : COLOR_GREEN;
    static const int channel_x[MOD_CHANNEL_COUNT] = { 20, 70, 120, 170 };

    sanitize_text(title, sizeof(title), module->title);
    if (title[0] == '\0') {
        (void)snprintf(title, sizeof(title), "UNTITLED MODULE");
    }

    if (!SDL_LockSurface(surface)) {
        return;
    }

    if (!cache->initialized) {
        fill_rect(surface, 0, 0, surface->w, surface->h, COLOR_BACKGROUND);
        draw_text5(surface, 4, 3, "MODPLAY - FOUR CHANNEL MOD PLAYER", COLOR_CYAN);
        draw_text5(surface, 4, 13, title, COLOR_GREEN);
        (void)snprintf(line, sizeof(line), "ORD:%03u/%03u PAT:%02u ROW:%02X BPM:%03u SPD:%02u",
                       (unsigned int)(player->order + 1U), (unsigned int)module->song_length,
                       (unsigned int)pattern, (unsigned int)player->row,
                       (unsigned int)player->bpm, (unsigned int)player->speed);
        draw_text4(surface, 4, 23, line, COLOR_WHITE);
        if (output_format_known) {
            (void)snprintf(line, sizeof(line), "IN:%lu %s %s OUT:%.5s %uCH %uHZ",
                           (unsigned long)settings->rate, audio_format_label(settings->bits),
                           channel_label(settings->channels), sdl_format_label(output_spec->format),
                           (unsigned int)output_spec->channels, (unsigned int)output_spec->freq);
        } else {
            (void)snprintf(line, sizeof(line), "IN:%lu %s %s OUT:UNKNOWN",
                           (unsigned long)settings->rate, audio_format_label(settings->bits),
                           channel_label(settings->channels));
        }
        draw_text4(surface, 4, 31, line, COLOR_CYAN);

        draw_line(surface, 4, 42, 219, 42, COLOR_BORDER);
        draw_text4(surface, 4, 46, "ROW", COLOR_DIM);
        draw_text5(surface, channel_x[0], 45, "CH1", COLOR_CYAN);
        draw_text5(surface, channel_x[1], 45, "CH2", COLOR_CYAN);
        draw_text5(surface, channel_x[2], 45, "CH3", COLOR_CYAN);
        draw_text5(surface, channel_x[3], 45, "CH4", COLOR_CYAN);
        draw_line(surface, 219, 0, 219, 199, COLOR_BORDER);
        draw_text5(surface, 225, 30, "SAMPLES", COLOR_YELLOW);

        draw_pattern_grid(surface, module, player->order, row_start, player->row, channel_x);
        {
            unsigned int sample_index;
            for (sample_index = 0; sample_index < MOD_SAMPLE_COUNT; ++sample_index) {
                draw_sample_line(surface, module, active_samples, sample_index);
            }
        }

        (void)snprintf(line, sizeof(line), "%s Q:%lu/%luMS LOOP:%lu/S",
                       paused ? "PAUSED" : "PLAYING", (unsigned long)queue_ms,
                       (unsigned long)target_ms, (unsigned long)loops_per_second);
        draw_text4(surface, 4, 188, line, status_color);
        if (low_queue_events || empty_queue_events) {
            (void)snprintf(line, sizeof(line), "QLOW:%lu EMPTY:%lu",
                           (unsigned long)low_queue_events, (unsigned long)empty_queue_events);
            draw_text4(surface, 224, 0, line, COLOR_DIM);
        }
        draw_text4(surface, 224, 7, paused ? "SPACE:RESUME" : "SPACE:PAUSE", COLOR_YELLOW);
        (void)snprintf(line, sizeof(line), "ESC:EXIT LOG:%s", logging_active ? "ON" : "OFF");
        draw_text4(surface, 224, 14, line, logging_active ? COLOR_GREEN : COLOR_DIM);
        (void)snprintf(line, sizeof(line), "DRV:%.14s", driver_name);
        draw_text4(surface, 224, 21, line, COLOR_DIM);
        (void)elapsed_ms;
        (void)player->output_bits;
        (void)player->output_channels;
        (void)SDL_UnlockSurface(surface);
        (void)present_full_surface(surface, window_surface, window);

        cache->initialized = true;
        cache->order = player->order;
        cache->pattern = pattern;
        cache->row = player->row;
        cache->row_start = row_start;
        cache->active_samples = active_samples;
        return;
    }

    {
        SDL_Rect dirty[16];
        unsigned int dirty_count = 0;
        bool redraw_grid = (cache->order != player->order) ||
                           (cache->pattern != pattern) ||
                           (cache->row_start != row_start);
        unsigned int sample_index;
        Uint32 changed_samples;
        int sample_group_start = -1;
        int sample_group_end = -1;

        fill_rect(surface, 4, 22, 215, 7, COLOR_BACKGROUND);
        (void)snprintf(line, sizeof(line), "ORD:%03u/%03u PAT:%02u ROW:%02X BPM:%03u SPD:%02u",
                       (unsigned int)(player->order + 1U), (unsigned int)module->song_length,
                       (unsigned int)pattern, (unsigned int)player->row,
                       (unsigned int)player->bpm, (unsigned int)player->speed);
        draw_text4(surface, 4, 23, line, COLOR_WHITE);
        add_dirty_rect(dirty, &dirty_count, 4, 22, 215, 7);

        if (redraw_grid) {
            draw_pattern_grid(surface, module, player->order, row_start, player->row, channel_x);
            add_dirty_rect(dirty, &dirty_count, 3, 54, 214, 130);
        } else if (cache->row != player->row) {
            const unsigned int old_visible_row = (unsigned int)(cache->row - row_start);
            const unsigned int new_visible_row = (unsigned int)(player->row - row_start);
            const int old_y = 55 + ((int)old_visible_row * 10);
            const int new_y = 55 + ((int)new_visible_row * 10);
            draw_pattern_row(surface, module, player->order, old_visible_row,
                             (int)cache->row, false, channel_x);
            draw_pattern_row(surface, module, player->order, new_visible_row,
                             (int)player->row, true, channel_x);
            add_dirty_rect(dirty, &dirty_count, 3, old_y - 1, 214, 9);
            add_dirty_rect(dirty, &dirty_count, 3, new_y - 1, 214, 9);
        }

        changed_samples = cache->active_samples ^ active_samples;
        if (changed_samples != 0U) {
            for (sample_index = 0; sample_index < MOD_SAMPLE_COUNT; ++sample_index) {
                if (changed_samples & (Uint32)(1U << sample_index)) {
                    const int current_index = (int)sample_index;
                    draw_sample_line(surface, module, active_samples, sample_index);
                    if (sample_group_start < 0) {
                        sample_group_start = current_index;
                    } else if (current_index > (sample_group_end + 1)) {
                        add_dirty_rect(dirty, &dirty_count, 224,
                                       43 + (sample_group_start * 5), 96,
                                       ((sample_group_end - sample_group_start) + 1) * 5);
                        sample_group_start = current_index;
                    }
                    sample_group_end = current_index;
                }
            }
            if (sample_group_start >= 0) {
                add_dirty_rect(dirty, &dirty_count, 224, 43 + (sample_group_start * 5), 96,
                               ((sample_group_end - sample_group_start) + 1) * 5);
            }
        }

        fill_rect(surface, 4, 187, 215, 8, COLOR_BACKGROUND);
        (void)snprintf(line, sizeof(line), "%s Q:%lu/%luMS LOOP:%lu/S",
                       paused ? "PAUSED" : "PLAYING", (unsigned long)queue_ms,
                       (unsigned long)target_ms, (unsigned long)loops_per_second);
        draw_text4(surface, 4, 188, line, status_color);
        add_dirty_rect(dirty, &dirty_count, 3, 187, 216, 8);

        fill_rect(surface, 224, 0, 96, 5, COLOR_BACKGROUND);
        if (low_queue_events || empty_queue_events) {
            (void)snprintf(line, sizeof(line), "QLOW:%lu EMPTY:%lu",
                           (unsigned long)low_queue_events, (unsigned long)empty_queue_events);
            draw_text4(surface, 224, 0, line, COLOR_DIM);
        }
        add_dirty_rect(dirty, &dirty_count, 224, 0, 96, 5);

        fill_rect(surface, 224, 7, 96, 5, COLOR_BACKGROUND);
        draw_text4(surface, 224, 7, paused ? "SPACE:RESUME" : "SPACE:PAUSE", COLOR_YELLOW);
        add_dirty_rect(dirty, &dirty_count, 224, 7, 96, 5);

        (void)elapsed_ms;
        (void)player->output_bits;
        (void)player->output_channels;
        (void)SDL_UnlockSurface(surface);
        (void)present_dirty_rects(surface, window_surface, window, dirty, dirty_count);

        cache->order = player->order;
        cache->pattern = pattern;
        cache->row = player->row;
        cache->row_start = row_start;
        cache->active_samples = active_samples;
    }
}

/* -------------------------------------------------------------------------- */
/* Runtime lifecycle: diagnostics, initialization, controls, loop, and cleanup. */
/* -------------------------------------------------------------------------- */
static void log_player_stats(const ModPlayer *player, Uint64 elapsed_ms,
                             Uint32 generated_interval, Uint32 loops_per_second,
                             Uint32 queued_frames, Uint32 target_frames,
                             Uint32 minimum_queue_frames, Uint32 maximum_queue_frames,
                             Uint32 low_queue_events, Uint32 empty_queue_events,
                             Uint32 max_refill_ms, Uint32 max_draw_ms,
                             Uint64 total_draw_ms, Uint32 draw_count, bool paused)
{
    unsigned int active_voices = 0;
    unsigned int i;
    const Uint32 average_draw_ms_tenths = (draw_count == 0U)
                                              ? 0U
                                              : (Uint32)((total_draw_ms * 10U) / draw_count);
    for (i = 0; i < MOD_CHANNEL_COUNT; ++i) {
        if (player->channels[i].active && player->channels[i].volume > 0) {
            ++active_voices;
        }
    }
    LOG("Stats: elapsed_ms=%lu order=%u pattern=%u row=%u tick=%u speed=%u bpm=%u loops=%lu generated_frames=%lu generated_interval=%lu loops_per_sec=%lu active_voices=%u queue_frames=%lu target_frames=%lu min_queue=%lu max_queue=%lu low_queue_events=%lu empty_queue_events=%lu max_refill_ms=%lu avg_draw_ms=%u.%u max_draw_ms=%lu draw_count=%u paused=%u",
        (unsigned long)elapsed_ms, (unsigned int)player->order,
        (unsigned int)player->module->orders[player->order], (unsigned int)player->row,
        (unsigned int)player->tick, (unsigned int)player->speed, (unsigned int)player->bpm,
        (unsigned long)player->song_loops, (unsigned long)player->generated_frames,
        (unsigned long)generated_interval, (unsigned long)loops_per_second, active_voices,
        (unsigned long)queued_frames, (unsigned long)target_frames,
        (unsigned long)minimum_queue_frames, (unsigned long)maximum_queue_frames,
        (unsigned long)low_queue_events, (unsigned long)empty_queue_events,
        (unsigned long)max_refill_ms, average_draw_ms_tenths / 10U,
        average_draw_ms_tenths % 10U, (unsigned long)max_draw_ms, draw_count,
        paused ? 1U : 0U);
}

/* -------------------------------------------------------------------------- */
/* Display initialization: SDL video, a window surface, and the 256-color DAC. */
/* -------------------------------------------------------------------------- */
static bool initialize_modplay_display(ModPlayApp *app)
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
        fprintf(stderr, "Error: could not set the 256-color palette: %s\n", SDL_GetError());
        LOG("Palette setup failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

/* -------------------------------------------------------------------------- */
/* Audio initialization: open the requested source format and prime its queue. */
/* -------------------------------------------------------------------------- */
static bool initialize_modplay_audio(ModPlayApp *app, const AppSettings *settings)
{
    const char *raw_name;
    const char *raw_driver;

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "Error: SDL audio initialization failed: %s\n", SDL_GetError());
        LOG("SDL audio initialization failed: %s", SDL_GetError());
        return false;
    }
    app->audio_initialized = true;

    app->source_spec.format = (settings->bits == 8U) ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE;
    app->source_spec.channels = settings->channels;
    app->source_spec.freq = (int)settings->rate;
    app->audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                   &app->source_spec, NULL, NULL);
    if (!app->audio_stream) {
        fprintf(stderr, "Error: could not open audio playback: %s\n", SDL_GetError());
        LOG("SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
        return false;
    }

    app->audio_device = SDL_GetAudioStreamDevice(app->audio_stream);
    raw_name = SDL_GetAudioDeviceName(app->audio_device);
    if (raw_name) {
        (void)snprintf(app->device_name, sizeof(app->device_name), "%.31s", raw_name);
    }
    raw_driver = SDL_GetCurrentAudioDriver();
    if (raw_driver) {
        (void)snprintf(app->driver_name, sizeof(app->driver_name), "%.19s", raw_driver);
    }
    app->output_format_known = SDL_GetAudioStreamFormat(app->audio_stream,
                                                         &app->source_spec,
                                                         &app->stream_output_spec);
    app->device_format_known = SDL_GetAudioDeviceFormat(app->audio_device,
                                                         &app->device_spec,
                                                         &app->device_buffer_frames);
    if (!app->output_format_known) {
        LOG("SDL_GetAudioStreamFormat failed: %s", SDL_GetError());
    }
    if (!app->device_format_known) {
        LOG("SDL_GetAudioDeviceFormat failed: %s", SDL_GetError());
    }

    LOG("Audio stream opened: device_id=%lu name=%s driver=%s",
        (unsigned long)app->audio_device, app->device_name, app->driver_name);
    LOG("Audio source: format=%s channels=%d rate=%d block_frames=%u",
        sdl_format_label(app->source_spec.format), app->source_spec.channels,
        app->source_spec.freq, (unsigned int)audio_chunk_frames(settings->rate));
    LOG("Audio queue policy: target_ms=%u target_frames=%lu chunk_frames=%u screen_refresh_ms=%u screen_update=dirty_rects",
        (unsigned int)AUDIO_QUEUE_TARGET_MS,
        (unsigned long)((settings->rate * AUDIO_QUEUE_TARGET_MS) / 1000U),
        (unsigned int)audio_chunk_frames(settings->rate), (unsigned int)SCREEN_REFRESH_MS);
    if (app->output_format_known) {
        LOG("Audio stream output: format=%s channels=%d rate=%d",
            sdl_format_label(app->stream_output_spec.format),
            app->stream_output_spec.channels, app->stream_output_spec.freq);
    }
    if (app->device_format_known) {
        LOG("Audio device format: format=%s channels=%d rate=%d buffer_frames=%d",
            sdl_format_label(app->device_spec.format), app->device_spec.channels,
            app->device_spec.freq, app->device_buffer_frames);
    }

    initialize_player(&app->player, &app->module, settings->rate,
                      settings->bits, settings->channels);
    if (!queue_audio_to_target(app->audio_stream, &app->player, settings,
                               &app->queued_frames, &app->target_frames)) {
        fprintf(stderr, "Error: could not queue initial audio: %s\n", SDL_GetError());
        LOG("Initial audio queue failed: %s", SDL_GetError());
        return false;
    }
    if (!SDL_ResumeAudioStreamDevice(app->audio_stream)) {
        fprintf(stderr, "Error: could not start audio playback: %s\n", SDL_GetError());
        LOG("SDL_ResumeAudioStreamDevice failed: %s", SDL_GetError());
        return false;
    }

    app->audio_ready = true;
    app->start_ticks = SDL_GetTicks();
    app->last_draw_ticks = 0;
    app->last_stats_ticks = app->start_ticks;
    LOG("Playback resumed: initial_queue_frames=%lu target_frames=%lu",
        (unsigned long)app->queued_frames, (unsigned long)app->target_frames);
    return true;
}

/* -------------------------------------------------------------------------- */
/* Application setup: load the MOD, then initialize video and audio services. */
/* -------------------------------------------------------------------------- */
static bool initialize_modplay_app(ModPlayApp *app, const AppSettings *settings)
{
    char error[160];

    if (!load_module(settings->module_path, &app->module, error, sizeof(error))) {
        fprintf(stderr, "Error: %s\n", error);
        LOG("Module load failed: %s", error);
        return false;
    }
    LOG("Module loaded: title=%s signature=%s song_length=%u patterns=%u samples=31 bytes=%lu",
        app->module.title, app->module.signature, (unsigned int)app->module.song_length,
        (unsigned int)app->module.pattern_count, (unsigned long)app->module.file_size);

    if (!initialize_modplay_display(app)) {
        return false;
    }
    return initialize_modplay_audio(app, settings);
}

/* -------------------------------------------------------------------------- */
/* Keyboard and playback control: SPACE pauses/resumes; Esc or close exits.   */
/* Audio refill, dirty-screen updates, and periodic performance logs run here. */
/* -------------------------------------------------------------------------- */
static bool run_modplay_loop(ModPlayApp *app, const AppSettings *settings)
{
    bool running = true;
    SDL_Event event;

    while (running) {
        const Uint64 now = SDL_GetTicks();

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                LOG("Quit event received");
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_ESCAPE) {
                    LOG("Escape key received");
                    running = false;
                } else if (event.key.key == SDLK_SPACE && !app->space_down) {
                    app->space_down = true;
                    if (!app->paused) {
                        if (SDL_PauseAudioStreamDevice(app->audio_stream)) {
                            app->paused = true;
                            LOG("Playback paused at order=%u row=%u tick=%u",
                                (unsigned int)app->player.order,
                                (unsigned int)app->player.row,
                                (unsigned int)app->player.tick);
                        } else {
                            LOG("SDL_PauseAudioStreamDevice failed: %s", SDL_GetError());
                        }
                    } else if (SDL_ResumeAudioStreamDevice(app->audio_stream)) {
                        app->paused = false;
                        LOG("Playback resumed at order=%u row=%u tick=%u",
                            (unsigned int)app->player.order,
                            (unsigned int)app->player.row,
                            (unsigned int)app->player.tick);
                    } else {
                        LOG("SDL_ResumeAudioStreamDevice failed: %s", SDL_GetError());
                    }
                }
            } else if (event.type == SDL_EVENT_KEY_UP && event.key.key == SDLK_SPACE) {
                app->space_down = false;
            }
        }
        if (!running) {
            break;
        }
        ++app->loops_this_interval;

        if (!app->paused && app->audio_ready) {
            const int queued_bytes = SDL_GetAudioStreamQueued(app->audio_stream);
            const Uint32 frame_bytes = bytes_per_source_frame(settings);
            if (queued_bytes < 0 || frame_bytes == 0U) {
                LOG("SDL_GetAudioStreamQueued failed during playback: %s", SDL_GetError());
                app->playback_error = true;
                running = false;
                break;
            }

            app->queued_frames = (Uint32)queued_bytes / frame_bytes;
            if (app->queued_frames < app->minimum_queue_frames) {
                app->minimum_queue_frames = app->queued_frames;
            }
            if (app->queued_frames > app->maximum_queue_frames) {
                app->maximum_queue_frames = app->queued_frames;
            }
            if (app->queued_frames < audio_chunk_frames(settings->rate)) {
                if (!app->low_queue_latched) {
                    ++app->low_queue_events;
                    app->low_queue_latched = true;
                }
            } else {
                app->low_queue_latched = false;
            }
            if (app->queued_frames == 0U) {
                if (!app->empty_queue_latched) {
                    ++app->empty_queue_events;
                    app->empty_queue_latched = true;
                }
            } else {
                app->empty_queue_latched = false;
            }

            if (app->logging_active) {
                const Uint64 refill_started = SDL_GetTicks();
                if (!queue_audio_to_target(app->audio_stream, &app->player, settings,
                                           &app->queued_frames, &app->target_frames)) {
                    fprintf(stderr, "Error: audio feed failed: %s\n", SDL_GetError());
                    LOG("Audio feed failed: %s", SDL_GetError());
                    app->playback_error = true;
                    running = false;
                    break;
                }
                {
                    const Uint32 refill_ms = (Uint32)(SDL_GetTicks() - refill_started);
                    if (refill_ms > app->max_refill_ms) {
                        app->max_refill_ms = refill_ms;
                    }
                }
            } else if (!queue_audio_to_target(app->audio_stream, &app->player, settings,
                                              &app->queued_frames, &app->target_frames)) {
                fprintf(stderr, "Error: audio feed failed: %s\n", SDL_GetError());
                app->playback_error = true;
                running = false;
                break;
            }
        }

        if (now - app->last_draw_ticks >= SCREEN_REFRESH_MS) {
            const Uint64 draw_started = app->logging_active ? SDL_GetTicks() : 0;
            draw_screen(app->drawing_surface, app->window_surface, app->window,
                        &app->module, &app->player, settings, &app->stream_output_spec,
                        app->output_format_known, app->driver_name, app->queued_frames,
                        app->low_queue_events, app->empty_queue_events,
                        app->target_frames, app->paused, app->logging_active,
                        now - app->start_ticks, app->loops_per_second,
                        &app->screen_cache);
            if (app->logging_active) {
                const Uint32 draw_ms = (Uint32)(SDL_GetTicks() - draw_started);
                app->total_draw_ms += draw_ms;
                ++app->draw_count;
                if (draw_ms > app->max_draw_ms) {
                    app->max_draw_ms = draw_ms;
                }
            }
            app->last_draw_ticks = now;
        }

        if (now - app->last_stats_ticks >= LOG_INTERVAL_MS) {
            const Uint64 interval_ms = now - app->last_stats_ticks;
            if (interval_ms > 0) {
                app->loops_per_second =
                    (Uint32)(((Uint64)app->loops_this_interval * 1000ULL) / interval_ms);
            }
            if (app->logging_active) {
                const Uint32 generated_interval = app->player.generated_frames -
                                                  app->generated_at_last_stats;
                const Uint32 minimum_queue =
                    (app->minimum_queue_frames == UINT_MAX) ? 0U : app->minimum_queue_frames;
                log_player_stats(&app->player, now - app->start_ticks,
                                 generated_interval, app->loops_per_second,
                                 app->queued_frames, app->target_frames,
                                 minimum_queue, app->maximum_queue_frames,
                                 app->low_queue_events, app->empty_queue_events,
                                 app->max_refill_ms, app->max_draw_ms,
                                 app->total_draw_ms, app->draw_count, app->paused);
            }
            app->max_refill_ms = 0;
            app->max_draw_ms = 0;
            app->total_draw_ms = 0;
            app->draw_count = 0;
            app->generated_at_last_stats = app->player.generated_frames;
            app->loops_this_interval = 0;
            app->last_stats_ticks = now;
        }
        SDL_Delay(MAIN_LOOP_DELAY_MS);
    }

    LOG("Playback stopped: exit_status=%u order=%u row=%u loops=%lu generated_frames=%lu low_queue_events=%lu empty_queue_events=%lu",
        (app->audio_ready && !app->playback_error) ? 0U : 1U,
        (unsigned int)app->player.order, (unsigned int)app->player.row,
        (unsigned long)app->player.song_loops,
        (unsigned long)app->player.generated_frames,
        (unsigned long)app->low_queue_events,
        (unsigned long)app->empty_queue_events);
    return app->audio_ready && !app->playback_error;
}

/* -------------------------------------------------------------------------- */
/* Resource teardown: release stream, owned surfaces, window, SDL, and MOD.    */
/* -------------------------------------------------------------------------- */
static void shutdown_modplay_app(ModPlayApp *app)
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
    free_module(&app->module);
}

/* -------------------------------------------------------------------------- */
/* Program entry point: parse arguments, open optional log, and run playback.  */
/* -------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    AppSettings settings;
    ModPlayApp app = { 0 };
    const char *raw_blaster;
    bool show_help = false;
    bool app_initialized;
    int result = 1;

    if (!parse_arguments(argc, argv, &settings, &show_help)) {
        print_help(argv[0]);
        return 1;
    }
    if (show_help) {
        print_help(argv[0]);
        return 0;
    }

    app.minimum_queue_frames = UINT_MAX;
    (void)snprintf(app.device_name, sizeof(app.device_name), "UNKNOWN");
    (void)snprintf(app.driver_name, sizeof(app.driver_name), "UNKNOWN");
    if (settings.log_enabled) {
        log_file = fopen("MODPLAY.LOG", "w");
        if (!log_file) {
            fprintf(stderr, "Warning: could not open MODPLAY.LOG; continuing without debug logging.\n");
        } else {
            app.logging_active = true;
        }
    }

    LOG("MODPLAY started");
    LOG("Module path: %s", settings.module_path);
    LOG("Requested source: rate=%lu format=%s channels=%u",
        (unsigned long)settings.rate, audio_format_label(settings.bits),
        (unsigned int)settings.channels);
    raw_blaster = getenv("BLASTER");
    LOG("BLASTER=%s", raw_blaster ? raw_blaster : "(not set)");

    app_initialized = initialize_modplay_app(&app, &settings);
    if (app_initialized) {
        result = run_modplay_loop(&app, &settings) ? 0 : 1;
    }

    shutdown_modplay_app(&app);
    if (log_file) {
        LOG("Shutdown");
        fclose(log_file);
        log_file = NULL;
    }
    return result;
}
