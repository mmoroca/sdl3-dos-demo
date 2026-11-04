/* TETRISDL3 persistent Top 10 (DOS 8.3 names; no SDL dependency).
 * by @mmoroca & Arena.AI 2026 (http://arena.ai/)
 * The previous single-number .SCO is read as ANON, level 0: that level is
 * UNKNOWN and is not misrepresented as the real historical level. A new
 * versioned text format is committed only after the player enters a name.
 */
#ifndef TETRIS_SCORES_H
#define TETRIS_SCORES_H
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TETRIS_RECORD_FILE "TETSDL3.SCO"
#define TETRIS_RECORD_TMP "TETSDL3.TMP"
#define TETRIS_RECORD_BAK "TETSDL3.BAK"
#define TETRIS_RECORD_MAGIC "TETSDL3TOP1"
#define TETRIS_TOP_COUNT 10
#define TETRIS_NAME_LENGTH 8
#define TETRIS_RECORD_SCORE_MAX UINT64_C(999999999)

typedef struct TetrisRecord {
    char name[TETRIS_NAME_LENGTH+1];
    uint32_t level;
    uint64_t points;
} TetrisRecord;

typedef struct TetrisScores {
    TetrisRecord entry[TETRIS_TOP_COUNT];
    unsigned count;
    bool migrated; /* true when an old plain decimal score was imported */
} TetrisScores;

/* Old entries win ties; zero cannot qualify. If fewer than ten records
 * exist, every positive result qualifies. A rank of -1 means no prompt. */
static inline int tetris_scores_rank(const TetrisScores *s, uint64_t points)
{
    if (!points || points > TETRIS_RECORD_SCORE_MAX) return -1;
    for (unsigned i=0; i<s->count; ++i)
        if (points > s->entry[i].points) return (int)i;
    return s->count < TETRIS_TOP_COUNT ? (int)s->count : -1;
}

static inline bool tetris_scores_insert(TetrisScores *s, const char *name,
                                         uint32_t level, uint64_t points)
{
    const int rank = tetris_scores_rank(s, points);
    const size_t length = strlen(name);
    if (rank<0 || !length || length>TETRIS_NAME_LENGTH) return false;
    for (size_t i=0; i<length; ++i) {
        const unsigned char c=(unsigned char)name[i];
        if (!(c>='A' && c<='Z') && !(c>='0' && c<='9')) return false;
    }
    unsigned last=s->count<TETRIS_TOP_COUNT?s->count:TETRIS_TOP_COUNT-1;
    for (unsigned i=last; i>(unsigned)rank; --i) s->entry[i]=s->entry[i-1];
    TetrisRecord *target=&s->entry[rank];
    memcpy(target->name,name,length+1);
    target->level=level;
    target->points=points;
    if (s->count<TETRIS_TOP_COUNT) ++s->count;
    return true;
}

static inline uint64_t tetris_scores_best(const TetrisScores *s)
{
    return s->count ? s->entry[0].points : 0;
}

/* Parse an entire decimal field and reject overflow/garbage, rather than
 * interpreting a truncated or negative DOS score as an enormous unsigned. */
static inline bool tetris_scores_decimal(const char *line, uint64_t *value)
{
    char *end;
    unsigned long long parsed;
    if (!isdigit((unsigned char)*line)) return false;
    errno=0;
    parsed=strtoull(line,&end,10);
    while (isspace((unsigned char)*end)) ++end;
    if (errno || *end || parsed>TETRIS_RECORD_SCORE_MAX) return false;
    *value=(uint64_t)parsed;
    return true;
}

/* Parse ONE bounded nonnegative decimal field, without scanf's undefined
 * behavior for integer fields too large to fit the destination type. */
static inline bool tetris_scores_field(const char *text, uint64_t ceiling,
                                        uint64_t *value)
{
    if (!text || !isdigit((unsigned char)*text)) return false;
    char *end;
    errno=0;
    const unsigned long long parsed=strtoull(text,&end,10);
    if(errno || *end || parsed>ceiling) return false;
    *value=(uint64_t)parsed;
    return true;
}

/* Load either the versioned table or a legacy one-line score. Return false
 * for malformed content; the caller displays an error but can still play.
 * If a save was interrupted after moving .SCO to .BAK, recover the backup. */
static inline bool tetris_scores_load(TetrisScores *s)
{
    FILE *file=fopen(TETRIS_RECORD_FILE,"r");
    char line[96];
    memset(s,0,sizeof(*s));
    if (!file) file=fopen(TETRIS_RECORD_BAK,"r");
    if (!file) return true; /* first run: no record */
    if (!fgets(line,sizeof(line),file)) {fclose(file);return false;}
    if (strcmp(line,TETRIS_RECORD_MAGIC "\n") &&
        strcmp(line,TETRIS_RECORD_MAGIC "\r\n")) {
        uint64_t legacy=0;
        const bool one_line=fgetc(file)==EOF;
        fclose(file);
        if (!one_line || strlen(line)>11 || !tetris_scores_decimal(line,&legacy))
            return false;
        if (legacy) {
            s->count=1;
            memcpy(s->entry[0].name,"ANON",5);
            s->entry[0].level=0; /* migration placeholder, not a real level */
            s->entry[0].points=legacy;
            s->migrated=true;
        }
        return true;
    }
    bool valid=true;
    while (fgets(line,sizeof(line),file)) {
        /* Native text files use LF; DOS can return LF or CRLF depending on
         * the C runtime. Reject every other trailing/control byte. */
        char *newline=strchr(line,'\n');
        if(newline) {
            if(newline>line && newline[-1]=='\r') newline[-1]='\0';
            else *newline='\0';
        }
        char *level_text=strchr(line,' ');
        char *points_text=level_text?strchr(level_text+1,' '):NULL;
        if(s->count>=TETRIS_TOP_COUNT || !level_text || !points_text ||
           strchr(points_text+1,' ')) {valid=false;break;}
        *level_text++='\0';
        *points_text++='\0';
        const size_t length=strlen(line);
        if(!length || length>TETRIS_NAME_LENGTH) {valid=false;break;}
        for(size_t i=0;i<length;++i)
            if(!((line[i]>='A' && line[i]<='Z') ||
                 (line[i]>='0' && line[i]<='9'))) valid=false;
        uint64_t level=0, points=0;
        if(!valid || !tetris_scores_field(level_text,UINT32_MAX,&level) ||
           !tetris_scores_field(points_text,TETRIS_RECORD_SCORE_MAX,&points) ||
           !points || (s->count && points>s->entry[s->count-1].points)) {
            valid=false;break;
        }
        TetrisRecord *e=&s->entry[s->count++];
        memcpy(e->name,line,length+1);
        e->level=(uint32_t)level;
        e->points=points;
    }
    if (ferror(file)) valid=false;
    fclose(file);
    if (!valid) memset(s,0,sizeof(*s));
    return valid;
}

/* Write all 10 records only after a successful name confirmation. A temp
 * file and backup ensure a failed rename can restore the last good table;
 * .BAK is also understood on startup after a mid-save power failure.
 * Distinct from /L diagnostics: this writes only on actual high-score entry.
 */
static inline bool tetris_scores_save(const TetrisScores *s)
{
    FILE *file=fopen(TETRIS_RECORD_TMP,"w");
    if (!file) return false;
    bool ok=fprintf(file,"%s\n",TETRIS_RECORD_MAGIC)>0;
    for (unsigned i=0; i<s->count; ++i)
        if (fprintf(file,"%s %lu %llu\n",s->entry[i].name,
            (unsigned long)s->entry[i].level,
            (unsigned long long)s->entry[i].points)<=0) ok=false;
    if (fflush(file)!=0) ok=false;
    if (fclose(file)!=0) ok=false;
    if (!ok) {remove(TETRIS_RECORD_TMP);return false;}
    bool moved=false;
    FILE *old=fopen(TETRIS_RECORD_FILE,"r");
    if (old) {
        fclose(old);
        (void)remove(TETRIS_RECORD_BAK); /* last run's completed backup */
        if (rename(TETRIS_RECORD_FILE,TETRIS_RECORD_BAK)!=0) {
            remove(TETRIS_RECORD_TMP);return false;
        }
        moved=true;
    } else if (errno!=ENOENT) {
        remove(TETRIS_RECORD_TMP);return false;
    }
    if (rename(TETRIS_RECORD_TMP,TETRIS_RECORD_FILE)!=0) {
        if (moved) (void)rename(TETRIS_RECORD_BAK,TETRIS_RECORD_FILE);
        (void)remove(TETRIS_RECORD_TMP);
        return false;
    }
    if (moved) (void)remove(TETRIS_RECORD_BAK);
    return true;
}
#endif /* TETRIS_SCORES_H */
