/* VTEST.EXE test card: generated pixels, no images or floating point.
 * by @mmoroca & [Arena.AI](http://Arena.AI) 2026
 * MIT code; the original font_5x7.h is copied from the 486+ project.
 */
#ifndef VTEST_CHART_H
#define VTEST_CHART_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "fonts/font_5x7.h"

typedef struct Chart {
    int w,h;
    const char *line1,*line2,*footer;
} Chart;

typedef struct ChartRGB { uint8_t r,g,b; } ChartRGB;

static ChartRGB chart_color(unsigned i)
{
    static const ChartRGB basic[16]={
        { 9, 13, 25},{250,250,250},{245,52,52},{56,238,75},
        {54,98,246},{42,230,235},{242,65,231},{248,228,48},
        {125,125,125},{220,135,50},{120,60,240},{80,190,130},
        {245,165,190},{155,220,245},{185,175,135},{40,45,55}
    };
    ChartRGB c={0,0,0};
    if(i<16) return basic[i];
    if(i>=32 && i<64) {
        uint8_t v=(uint8_t)((i-32u)*255u/31u);
        c.r=c.g=c.b=v;
    } else if(i>=64 && i<128) c.r=(uint8_t)((i-64u)*255u/63u);
    else if(i>=128 && i<192) c.g=(uint8_t)((i-128u)*255u/63u);
    else if(i>=192 && i<256) c.b=(uint8_t)((i-192u)*255u/63u);
    return c;
}

static const Glyph *chart_glyph(char c)
{
    const size_t count=sizeof(font_5x7)/sizeof(font_5x7[0]);
    for(size_t i=0;i<count;++i)
        if(font_5x7[i].codepoint==(unsigned char)c) return &font_5x7[i];
    return NULL;
}

static int chart_text_pixel(const char *s,int px,int y,int x0,int y0,int scale)
{
    int col,row,glyph_x;
    const Glyph *glyph;
    if(px<x0 || y<y0 || y>=y0+7*scale) return 0;
    col=(px-x0)/(6*scale);
    if(col<0 || col>=63 || (size_t)col>=strlen(s)) return 0;
    glyph=chart_glyph(s[col]);
    if(!glyph) return 0;
    glyph_x=((px-x0)%(6*scale))/scale;
    row=(y-y0)/scale;
    return glyph_x<5 && (glyph->rows[row] & (1u<<(4-glyph_x)))!=0;
}

/* Color indices are intentionally identical for BIOS VGA DAC and SDL INDEX8.
 * Non-indexed paths map the SAME RGB triplets into their actual pixel format. */
static uint8_t chart_index(const Chart *c,int x,int y)
{
    int scale=c->w<500?1:2;
    int header=scale==1?27:44;
    int footer=scale==1?15:27;
    int content=c->h-header-footer;
    int p=y-header;
    if(x<2 || x>=c->w-2 || y<2 || y>=c->h-2) return 1;
    if(y<header) {
        if(chart_text_pixel(c->line1,x,y,6,4,scale) ||
           chart_text_pixel(c->line2,x,y,6,5+8*scale,scale)) return 1;
        return y==header-1?1:0;
    }
    if(y>=c->h-footer) {
        if(chart_text_pixel(c->footer,x,y,6,c->h-footer+4,scale)) return 1;
        return y==c->h-footer?1:0;
    }
    if(p<content/3) {
        const uint8_t bars[8]={1,7,3,5,4,6,2,0};
        int band=x*8/c->w;
        if(band>7) band=7;
        if((x*8)%c->w<2) return 1;
        return bars[band];
    }
    if(p<content/2) return (uint8_t)(32+(x*32/c->w>31?31:x*32/c->w));
    if(p<content*2/3) {
        int usable=c->w-4;
        int xx=x-2;
        int band=xx*3/usable;
        int v=(xx*3%usable)*64/usable;
        if(v>63) v=63;
        return (uint8_t)(64+64*band+v);
    }
    {
        int origin=header+content*2/3;
        int cx=c->w/2,cy=(origin+c->h-footer)/2;
        int r=(c->h-footer-origin)/2-5;
        int dx=x-cx,dy=y-cy;
        int d=dx*dx+dy*dy;
        if(r>3 && d<r*r && d>(r-2)*(r-2)) return 7;
        if(x==cx || y==cy) return 1;
        if(x%(c->w/10)==0 || (y-origin)%10==0) return 8;
        if((x/4+y/4)%2==0) return 15;
        return 0;
    }
}
#endif
