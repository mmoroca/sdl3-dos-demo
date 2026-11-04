/* TETRISDL3: deterministic rules, independent of SDL, DOS and rendering.
 * by @mmoroca & Arena.AI 2026 (http://arena.ai/)
 *
 * This small header is deliberately limited to board rules. The owner of a
 * TetrisRules value (tetrisdl3.c) decides when to call tick(), which keys to
 * accept, and how to render/save scores. Tests can include this file without
 * opening a window or audio device. No heap allocation, file I/O or goto.
 */
#ifndef TETRIS_RULES_H
#define TETRIS_RULES_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define TETRIS_COLUMNS 10
#define TETRIS_ROWS 20
#define TETRIS_PIECES 7
#define TETRIS_SCORE_MAX UINT64_C(999999999)

typedef enum TetrisPiece {
    TETRIS_I = 0, TETRIS_O, TETRIS_T, TETRIS_J,
    TETRIS_L, TETRIS_S, TETRIS_Z
} TetrisPiece;

typedef struct TetrisRules {
    /* 0 is empty; 1..7 correspond to the piece enum + 1 and its color.
     * The falling tetromino is NOT stamped here until it locks. */
    uint8_t board[TETRIS_ROWS][TETRIS_COLUMNS];
    TetrisPiece active, next;
    int rotation, x, y; /* top-left of the active piece's normalized 4x4 mask */
    uint32_t rng; /* nonzero xorshift state, independent from the renderer */
    uint32_t lines, level;
    uint64_t score;
    unsigned gravity_elapsed, lock_elapsed; /* logical frames, not milliseconds */
    /* A settled line remains visible and unscored during the flash phase.
     * Only finish_clear() compacts it and spawns the next tetromino. */
    uint32_t pending_rows; /* bit y identifies a complete board row */
    unsigned pending_count;
    bool awaiting_clear, grounded, over;
} TetrisRules;

/* Each shape occupies exactly four bits within the 4x4 mask. The high bit
 * represents the leftmost cell. Orientation 0 follows the familiar letter
 * silhouette; subsequent orientations rotate clockwise about the 4x4 area
 * and normalize to top-left. O stays constant; I/S/Z repeat after 2 turns.
 * Keeping all orientations literal makes the shape table readable in a book
 * and prevents rotations from producing unintended reflected pieces. */
static const uint8_t tetris_shapes[TETRIS_PIECES][4][4] = {
    { {15,0,0,0}, {8,8,8,8}, {15,0,0,0}, {8,8,8,8} },      /* I: red */
    { {12,12,0,0}, {12,12,0,0}, {12,12,0,0}, {12,12,0,0} },/* O: dark blue */
    { {14,4,0,0}, {4,12,4,0}, {4,14,0,0}, {8,12,8,0} },    /* T: green */
    { {8,14,0,0}, {12,8,8,0}, {14,2,0,0}, {4,4,12,0} },   /* J: yellow */
    { {2,14,0,0}, {8,8,12,0}, {14,8,0,0}, {12,4,4,0} },   /* L: purple */
    { {6,12,0,0}, {8,12,4,0}, {6,12,0,0}, {8,12,4,0} },  /* S: light blue */
    { {12,6,0,0}, {4,12,8,0}, {12,6,0,0}, {4,12,8,0} }   /* Z: orange */
};

static inline bool tetris_cell(TetrisPiece kind, int rotation, int x, int y)
{
    return (tetris_shapes[kind][rotation & 3][y] & (8u >> x)) != 0;
}

/* One table lookup per interval; the verbal "subtract 5 through level 9"
 * would incorrectly yield 3 on level 9. The explicitly approved table wins. */
static inline unsigned tetris_gravity_frames(uint32_t level)
{
    static const uint8_t early[10] = {48,43,38,33,28,23,18,13,8,6};
    if (level < 10) return early[level];
    if (level <= 12) return 5;
    if (level <= 15) return 4;
    if (level <= 18) return 3;
    if (level <= 28) return 2;
    return 1;
}

/* Xorshift32 uses a nonzero state. Rejection avoids modulo skew for seven
 * pieces. The preview is not consulted here: hiding it cannot alter the RNG. */
static inline uint32_t tetris_random_u32(TetrisRules *g)
{
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return g->rng = x;
}

static inline TetrisPiece tetris_random_piece(TetrisRules *g)
{
    /* Xorshift32 cycles over 1..UINT32_MAX, never zero. Accept a multiple
     * of seven values from THAT range (inclusive), not from 0..UINT32_MAX.
     * Otherwise color 0 would be infinitesimally less frequent per cycle. */
    const uint32_t limit = UINT32_MAX - (UINT32_MAX % TETRIS_PIECES);
    uint32_t value;
    do { value = tetris_random_u32(g); } while (value > limit);
    return (TetrisPiece)((value - 1u) % TETRIS_PIECES);
}

/* Reject every occupied cell outside the sides/bottom or overlapping fixed
 * blocks. Cells ABOVE row zero are permitted temporarily and never index the
 * array; the spawn and locking paths check top-out separately. */
static inline bool tetris_fits(const TetrisRules *g, TetrisPiece piece,
                                int rotation, int px, int py)
{
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
        if (!tetris_cell(piece, rotation, x, y)) continue;
        const int bx = px + x, by = py + y;
        if (bx < 0 || bx >= TETRIS_COLUMNS || by >= TETRIS_ROWS) return false;
        if (by >= 0 && g->board[by][bx] != 0) return false;
    }
    return true;
}

/* Contact is defined by whether the active shape could move down one more
 * cell. Lateral motion/rotation on the floor does NOT reset lock time unless
 * it really leaves support, at which point a later landing gets a fresh
 * gravity interval. This rule also prevents unlimited floor stalling. */
static inline void tetris_refresh_contact(TetrisRules *g)
{
    const bool touching = !tetris_fits(g, g->active, g->rotation, g->x, g->y + 1);
    if (touching != g->grounded) {
        g->lock_elapsed = 0;
        g->gravity_elapsed = 0;
    }
    g->grounded = touching;
}

static inline void tetris_spawn(TetrisRules *g)
{
    g->active = g->next;
    g->next = tetris_random_piece(g);
    g->rotation = 0;
    g->x = 3;
    g->y = 0; /* normalize shapes so every new piece is visible immediately */
    g->gravity_elapsed = g->lock_elapsed = 0;
    g->grounded = false;
    if (!tetris_fits(g, g->active, 0, g->x, g->y)) {
        g->over = true; /* blocked spawn: never overwrite settled blocks */
        return;
    }
    tetris_refresh_contact(g);
}

static inline void tetris_reset(TetrisRules *g, uint32_t seed)
{
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : UINT32_C(0x13579BDF);
    g->next = tetris_random_piece(g);
    tetris_spawn(g);
}

/* A successful action changes only the active shape. The caller handles key
 * repeat; these rules make no assumptions about DOSBox's keyboard timing. */
static inline bool tetris_move(TetrisRules *g, int dx)
{
    if (g->over || g->awaiting_clear ||
        !tetris_fits(g, g->active, g->rotation, g->x + dx, g->y))
        return false;
    g->x += dx;
    tetris_refresh_contact(g);
    return true;
}

static inline bool tetris_rotate(TetrisRules *g, int direction)
{
    const int turn = (g->rotation + (direction < 0 ? 3 : 1)) & 3;
    /* Deliberately NO wall kicks: try this position only. */
    if (g->over || g->awaiting_clear ||
        !tetris_fits(g, g->active, turn, g->x, g->y)) return false;
    g->rotation = turn;
    tetris_refresh_contact(g);
    return true;
}

/* Remove full rows bottom-up. Retesting the same y after shifting ensures
 * adjacent completed lines are not skipped. The preceding flash changes
 * pixels only; it cannot change this count or its scoring. */
static inline unsigned tetris_clear_lines(TetrisRules *g)
{
    unsigned cleared = 0;
    for (int y = TETRIS_ROWS - 1; y >= 0; --y) {
        bool full = true;
        for (int x = 0; x < TETRIS_COLUMNS; ++x)
            if (!g->board[y][x]) { full = false; break; }
        if (full) {
            for (int row = y; row > 0; --row)
                memcpy(g->board[row], g->board[row - 1], TETRIS_COLUMNS);
            memset(g->board[0], 0, TETRIS_COLUMNS);
            ++cleared;
            ++y;
        }
    }
    return cleared;
}

/* Score at the OLD level, then increase lines/level. Both actions are bounded
 * so an extreme run cannot wrap an integer back to a small/highscore value. */
static inline void tetris_award_lines(TetrisRules *g, unsigned count)
{
    static const unsigned base[5] = {0,40,100,300,1200};
    if (count > 4) count = 4; /* only one tetromino can fill at most four rows */
    if (count) {
        const uint64_t increment = (uint64_t)base[count] * (g->level + UINT64_C(1));
        g->score = increment > TETRIS_SCORE_MAX - g->score ?
                   TETRIS_SCORE_MAX : g->score + increment;
        g->lines = count > UINT32_MAX - g->lines ? UINT32_MAX : g->lines + count;
        g->level = g->lines / 10;
    }
}

/* Inspect without mutation: during the flash the colored row is still in
 * the board, so neither score nor next piece can advance prematurely. */
static inline void tetris_find_pending_rows(TetrisRules *g)
{
    g->pending_rows = 0;
    g->pending_count = 0;
    for (int y = 0; y < TETRIS_ROWS; ++y) {
        bool full = true;
        for (int x = 0; x < TETRIS_COLUMNS; ++x)
            if (!g->board[y][x]) { full = false; break; }
        if (full) { g->pending_rows |= UINT32_C(1) << y; ++g->pending_count; }
    }
    g->awaiting_clear = g->pending_count != 0;
}

/* Complete the transaction ONLY after the animator has presented three
 * flashes. This order guarantees that the score uses the previous level.
 * Calling twice is harmless; the next piece is spawned exactly once. */
static inline void tetris_finish_clear(TetrisRules *g)
{
    if (!g->awaiting_clear || g->over) return;
    const unsigned removed = tetris_clear_lines(g);
    g->awaiting_clear = false;
    g->pending_rows = 0;
    g->pending_count = 0;
    tetris_award_lines(g, removed);
    tetris_spawn(g);
}

/* Fix the piece. If at least one row is full, hand control to the animation
 * BEFORE compacting/scoring/spawning. Without full rows, spawn immediately.
 * A cell above the board on lock is top-out, with no partial board write. */
static inline void tetris_lock(TetrisRules *g)
{
    if (g->over || g->awaiting_clear) return;
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
        if (tetris_cell(g->active, g->rotation, x, y) && g->y + y < 0) {
            g->over = true;
            return;
        }
    }
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
        if (tetris_cell(g->active, g->rotation, x, y))
            g->board[g->y + y][g->x + x] = (uint8_t)g->active + 1;
    }
    tetris_find_pending_rows(g);
    if (!g->awaiting_clear) tetris_spawn(g);
}

/* Hard drop is the ONE operation that bypasses the gravity-sized lock delay.
 * No drop points are awarded. Even on a slow host, this input is one action. */
static inline void tetris_hard_drop(TetrisRules *g)
{
    if (g->over || g->awaiting_clear) return;
    while (tetris_fits(g, g->active, g->rotation, g->x, g->y + 1)) ++g->y;
    tetris_lock(g);
}

/* Advance exactly ONE logical frame, called by the SDL-facing 60 Hz clock.
 * Soft drop moves at most one cell per logical frame and does not accelerate
 * locking after contact. A normal fall waits G frames per cell. A new landing
 * starts the full G-frame countdown on the NEXT frame, including at level 29.
 */
static inline void tetris_tick(TetrisRules *g, bool soft_drop)
{
    if (g->over || g->awaiting_clear) return;
    const unsigned interval = tetris_gravity_frames(g->level);
    if (g->grounded) {
        if (tetris_fits(g, g->active, g->rotation, g->x, g->y + 1)) {
            tetris_refresh_contact(g); /* piece was moved off support */
        } else {
            ++g->lock_elapsed;
            if (g->lock_elapsed >= interval) tetris_lock(g);
            return;
        }
    }
    if (soft_drop) {
        if (tetris_fits(g, g->active, g->rotation, g->x, g->y + 1)) ++g->y;
        g->gravity_elapsed = 0;
    } else if (++g->gravity_elapsed >= interval) {
        g->gravity_elapsed = 0;
        if (tetris_fits(g, g->active, g->rotation, g->x, g->y + 1)) ++g->y;
    }
    tetris_refresh_contact(g);
}

#endif /* TETRIS_RULES_H */
