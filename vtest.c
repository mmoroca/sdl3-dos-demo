/* VTEST.EXE — DOS VGA/VBE versus SDL3 486+ diagnostic, no media files.
 * by @mmoroca & [Arena.AI](http://Arena.AI) 2026. MIT source.
 * BIOS path and SDL path never own a graphics session simultaneously.
 * A missing/unsupported mode leaves text mode and a readable error.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vtest_chart.h"
#ifdef __DJGPP__
#include <conio.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <sys/farptr.h>
#include <sys/movedata.h>
#include <sys/nearptr.h>
#endif

#define MODE_CAP 128
#define DISPLAY_MS 10000u

typedef struct VideoMode {
    uint16_t id, w, h, pitch, lin_pitch;
    uint16_t attributes, win_gran_kb,win_size_kb,win_seg;
    uint8_t win_attr, planes, bpp, model;
    uint8_t rsize,rpos,gsize,gpos,bsize,bpos;
    uint32_t phys;
} VideoMode;

static VideoMode mode_list[MODE_CAP];
static int mode_count=0;
static unsigned vbe_version=0,vram_kib=0;

static int wanted_resolution(int w,int h)
{
    return (w==320&&h==200) || (w==640&&h==400) ||
           (w==640&&h==480) || (w==800&&h==600) ||
           (w==1024&&h==768);
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1]<<8));
}
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}

#ifdef __DJGPP__
static void text_mode(void)
{
    __dpmi_regs r={0};
    r.x.ax=0x0003;
    __dpmi_int(0x10,&r);
}

/* Hide a DOS mouse-driver cursor for direct BIOS/VBE output. SDL's own
 * software cursor is separately disabled with SDL_HideCursor(). */
static void hide_dos_mouse(void)
{
    __dpmi_regs r={0};
    r.x.ax=0x0002; /* INT 33h: hide mouse cursor */
    __dpmi_int(0x33,&r);
}

static uint16_t hardware_mode(void)
{
    __dpmi_regs r={0};
    r.x.ax=0x4F03;
    __dpmi_int(0x10,&r);
    if(r.x.ax==0x004F) return r.x.bx;
    memset(&r,0,sizeof(r));
    r.h.ah=0x0F;
    __dpmi_int(0x10,&r);
    return (uint16_t)(r.h.al&0x7F);
}

static bool raw_mode_info(uint16_t id,uint16_t memseg,VideoMode *m)
{
    uint8_t data[256];
    __dpmi_regs r={0};
    r.x.ax=0x4F01;r.x.cx=id;r.x.es=memseg;r.x.di=0;
    __dpmi_int(0x10,&r);
    if(r.x.ax!=0x004F) return false;
    dosmemget((unsigned long)memseg*16, sizeof(data),data);
    memset(m,0,sizeof(*m));
    m->id=id;m->attributes=le16(data);
    m->win_attr=data[2];m->win_gran_kb=le16(data+4);
    m->win_size_kb=le16(data+6);m->win_seg=le16(data+8);
    m->pitch=le16(data+16);m->w=le16(data+18);m->h=le16(data+20);
    m->planes=data[24];m->bpp=data[25];m->model=data[27];
    m->rsize=data[31];m->rpos=data[32];
    m->gsize=data[33];m->gpos=data[34];
    m->bsize=data[35];m->bpos=data[36];
    m->phys=le32(data+40);m->lin_pitch=le16(data+50);
    return true;
}

static bool valid_mode(const VideoMode *m)
{
    bool bank_ok=(m->win_attr&5)==5 && m->win_seg && m->win_gran_kb &&
                 m->win_size_kb>=m->win_gran_kb;
    bool lfb_ok=(m->attributes&0x80) && m->phys;
    if((m->attributes&0x19)!=0x19 || !wanted_resolution(m->w,m->h) ||
       m->planes!=1 || !m->pitch || (!bank_ok && !lfb_ok)) return false;
    if(m->bpp!=8 && m->bpp!=15 && m->bpp!=16 &&
       m->bpp!=24 && m->bpp!=32) return false;
    if((m->bpp==8 && m->model!=4) ||
       (m->bpp!=8 && m->model!=4 && m->model!=6)) return false;
    if(m->pitch < m->w*((m->bpp+7)/8)) return false;
    if(m->model==6 && m->bpp!=8 &&
       (!m->rsize || !m->gsize || !m->bsize ||
        m->rsize>8 || m->gsize>8 || m->bsize>8 ||
        m->rpos+m->rsize>m->bpp || m->gpos+m->gsize>m->bpp ||
        m->bpos+m->bsize>m->bpp)) return false;
    return true;
}

static void collect_modes(void)
{
    _go32_dpmi_seginfo buffer={0};
    uint8_t controller[512];
    __dpmi_regs r={0};
    uint32_t ptr,linear;
    mode_list[0]=(VideoMode){.id=0x13,.w=320,.h=200,.pitch=320,
                              .planes=1,.bpp=8,.model=4,
                              .win_attr=5,.win_gran_kb=64,
                              .win_size_kb=64,.win_seg=0xA000};
    mode_count=1;
    buffer.size=sizeof(controller)/16;
    if(_go32_dpmi_allocate_dos_memory(&buffer)!=0) return;
    dosmemput("VBE2",4,(unsigned long)buffer.rm_segment*16);
    r.x.ax=0x4F00;r.x.es=buffer.rm_segment;r.x.di=0;
    __dpmi_int(0x10,&r);
    if(r.x.ax!=0x004F) {
        _go32_dpmi_free_dos_memory(&buffer);
        return;
    }
    dosmemget((unsigned long)buffer.rm_segment*16,sizeof(controller),controller);
    if(memcmp(controller,"VESA",4)!=0) {
        _go32_dpmi_free_dos_memory(&buffer);
        return;
    }
    vbe_version=le16(controller+4);
    vram_kib=(unsigned)le16(controller+18)*64u;
    ptr=le32(controller+14);
    linear=((ptr>>16)<<4)+(ptr&0xffffu);
    if(vbe_version>=0x102 && linear>0 && linear<0x100000u) {
        for(unsigned i=0;i<256 && mode_count<MODE_CAP;++i) {
            uint16_t id;
            VideoMode mode;
            bool duplicate=false;
            if(linear+2*i+1>=0x100000u) break;
            id=(uint16_t)_farpeekw(_dos_ds,linear+2*i);
            if(id==0xffff) break;
            if(!raw_mode_info(id,buffer.rm_segment,&mode) || !valid_mode(&mode))
                continue;
            for(int j=0;j<mode_count;++j)
                if(mode_list[j].id==id) duplicate=true;
            if(!duplicate) mode_list[mode_count++]=mode;
        }
    }
    _go32_dpmi_free_dos_memory(&buffer);
}

static uint32_t pack_channel(uint8_t value,unsigned size,unsigned shift)
{
    return ((uint32_t)value*((1u<<size)-1u)/255u)<<shift;
}
static uint32_t packed_color(const VideoMode *m,ChartRGB c)
{
    unsigned rs=m->rsize,gs=m->gsize,bs=m->bsize;
    unsigned rp=m->rpos,gp=m->gpos,bp=m->bpos;
    if(!rs || !gs || !bs) {
        rs=(m->bpp==15)?5:((m->bpp==16)?5:8);
        gs=(m->bpp==16)?6:((m->bpp==15)?5:8);
        bs=(m->bpp==15 || m->bpp==16)?5:8;
        rp=(m->bpp==15)?10:((m->bpp==16)?11:16);
        gp=(m->bpp==15)?5:((m->bpp==16)?5:8);
        bp=0;
    }
    return pack_channel(c.r,rs,rp)|pack_channel(c.g,gs,gp)|
           pack_channel(c.b,bs,bp);
}

static void set_dac(void)
{
    for(unsigned i=0;i<256;++i) {
        ChartRGB c=chart_color(i);
        outportb(0x3C8,(uint8_t)i);
        outportb(0x3C9,c.r>>2);
        outportb(0x3C9,c.g>>2);
        outportb(0x3C9,c.b>>2);
    }
}

static bool bank_write(const VideoMode *m,const uint8_t *src,uint32_t dst,
                       unsigned bytes,int *current)
{
    const uint32_t gran=(uint32_t)m->win_gran_kb*1024u;
    const uint32_t window=(uint32_t)m->win_size_kb*1024u;
    if(!gran || !window || gran>window || !m->win_seg || (m->win_attr&5)!=5)
        return false;
    while(bytes) {
        unsigned bank=dst/gran;
        unsigned off=dst%gran;
        unsigned n=window-off;
        __dpmi_regs r={0};
        if(n>bytes) n=bytes;
        if(bank>0xffffu) return false;
        if(*current!=(int)bank) {
            r.x.ax=0x4F05;r.x.bx=0;r.x.dx=(uint16_t)bank;
            __dpmi_int(0x10,&r);
            if(r.x.ax!=0x004F) return false;
            *current=(int)bank;
        }
        dosmemput(src,n,(unsigned long)m->win_seg*16+off);
        src+=n;dst+=n;bytes-=n;
    }
    return true;
}

static bool direct_preview(const VideoMode *chosen,bool force_bank,
                           char *why,size_t whylen)
{
    VideoMode m=*chosen;
    __dpmi_meminfo mapping={0};
    uint8_t *row=NULL, *lfb=NULL;
    uint32_t lut[256];
    char title[64],details[64];
    Chart chart;
    int current_bank=-1;
    bool mapped=false,ok=false,mode_set=false;
    unsigned bytespp=(m.bpp+7)/8;
    uint32_t pitch=m.pitch;
    if(m.id!=0x13) {
        __dpmi_regs r={0};
        bool lfb_ok=!force_bank && (m.attributes&0x80) && m.phys;
        if(force_bank && ((m.win_attr&5)!=5 || !m.win_seg ||
                          !m.win_gran_kb || m.win_size_kb<m.win_gran_kb)) {
            snprintf(why,whylen,"VBE mode has no writable bank window");
            return false;
        }
        r.x.ax=0x4F02;
        r.x.bx=(uint16_t)(m.id|(lfb_ok?0x4000:0));
        __dpmi_int(0x10,&r);
        if(r.x.ax!=0x004F && lfb_ok) {
            r.x.ax=0x4F02;r.x.bx=m.id;
            __dpmi_int(0x10,&r);
            lfb_ok=false;
        }
        if(r.x.ax!=0x004F) {
            snprintf(why,whylen,"BIOS 4F02 rejected VBE mode %04X",m.id);
            return false;
        }
        mode_set=true;
        if(lfb_ok) {
            uint32_t lin=m.lin_pitch?m.lin_pitch:m.pitch;
            if(lin>=m.w*bytespp) {
                mapping.address=m.phys;
                mapping.size=(lin*m.h+4095u)&~4095u;
                if(__dpmi_physical_address_mapping(&mapping)==0 &&
                   __djgpp_nearptr_enable()) {
                    lfb=(uint8_t*)(mapping.address+__djgpp_conventional_base);
                    pitch=lin;
                    mapped=true;
                } else if(mapping.address!=m.phys && mapping.size)
                    (void)__dpmi_free_physical_address_mapping(&mapping);
            }
        }
        if(!mapped && ((m.win_attr&5)!=5 || !m.win_seg ||
                       !m.win_gran_kb || m.win_size_kb<m.win_gran_kb)) {
            snprintf(why,whylen,"no usable LFB mapping or writable bank window");
            text_mode();
            return false;
        }
        if(!mapped && lfb_ok) {
            __dpmi_regs r2={0};
            r2.x.ax=0x4F02;r2.x.bx=m.id;
            __dpmi_int(0x10,&r2);
            if(r2.x.ax!=0x004F) {
                snprintf(why,whylen,"VBE rejected banked fallback");
                text_mode();return false;
            }
        }
    } else {
        __dpmi_regs r={0};
        r.x.ax=0x13;__dpmi_int(0x10,&r);
        mode_set=true;
    }
    hide_dos_mouse();
    if(m.bpp==8) set_dac();
    row=(uint8_t*)malloc((size_t)m.w*bytespp);
    if(!row) {
        snprintf(why,whylen,"cannot allocate one scanline");
    } else {
        snprintf(title,sizeof(title),"BIOS %uX%u %uB ID %04X",
                 m.w,m.h,m.bpp,m.id);
        snprintf(details,sizeof(details),"PITCH %lu %s VBE %X.%02X",
                 (unsigned long)pitch,mapped?"LFB":"BANK",
                 vbe_version>>8,vbe_version&255);
        chart=(Chart){m.w,m.h,title,details,
                      "WAIT 10 SECONDS OR PRESS ESC TO QUIT"};
        for(unsigned i=0;i<256;++i)
            lut[i]=(m.bpp==8)?i:packed_color(&m,chart_color(i));
        ok=true;
        for(int y=0;y<m.h && ok;++y) {
            for(int x=0;x<m.w;++x) {
                uint32_t pixel=lut[chart_index(&chart,x,y)];
                for(unsigned b=0;b<bytespp;++b)
                    row[x*bytespp+b]=(uint8_t)(pixel>>(b*8));
            }
            if(mapped) memcpy(lfb+(uint32_t)y*pitch,row,(size_t)m.w*bytespp);
            else if(m.id==0x13)
                dosmemput(row,(size_t)m.w,0xA0000ul+(unsigned long)y*320ul);
            else if(!bank_write(&m,row,(uint32_t)y*pitch,(unsigned)m.w*bytespp,
                                &current_bank)) {
                snprintf(why,whylen,"BIOS 4F05 bank switch failed");
                ok=false;
            }
        }
        if(ok) {
            uint32_t start=SDL_GetTicks();
            while(SDL_GetTicks()-start<DISPLAY_MS) {
                if(kbhit()) {
                    int key=getch();
                    if(key==0 || key==0xe0) (void)getch();
                    else if(key==27) break;
                }
                SDL_Delay(10);
            }
        }
    }
    free(row);
    if(mapped) (void)__dpmi_free_physical_address_mapping(&mapping);
    if(mode_set) text_mode();
    return ok;
}
#endif

/* XRGB8888 has 24 color bits but occupies 32 bits in video memory. */
static unsigned sdl_storage_bpp(SDL_PixelFormat format)
{
    return SDL_BYTESPERPIXEL(format)==4?32u:(unsigned)SDL_BITSPERPIXEL(format);
}

static bool sdl_preview(const VideoMode *selected,char *why,size_t whylen)
{
    SDL_Window *window=NULL;
    SDL_Surface *surface=NULL;
    SDL_DisplayMode **available=NULL;
    const SDL_DisplayMode *chosen=NULL;
    SDL_DisplayID display;
    const SDL_PixelFormatDetails *fmt;
    SDL_Palette *palette;
    SDL_Color colors[256];
    uint32_t lut[256];
    int count=0,bytespp;
    char title[64],details[64];
    Chart chart;
    bool ok=false,video_ready=false;
    if(!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        snprintf(why,whylen,"SDL video init: %.100s",SDL_GetError());
        return false;
    }
    video_ready=true;
    (void)SDL_HideCursor(); /* DOS SDL backend draws a software cursor. */
#ifdef __DJGPP__
    hide_dos_mouse();      /* Also hide any BIOS mouse-driver pointer. */
#endif
    display=SDL_GetPrimaryDisplay();
    available=SDL_GetFullscreenDisplayModes(display,&count);
    for(int i=0;i<count;++i) {
        const SDL_DisplayMode *m=available[i];
        if(m->w==selected->w && m->h==selected->h &&
           sdl_storage_bpp(m->format)==selected->bpp) {
            chosen=m;break;
        }
    }
    if(!chosen) {
        snprintf(why,whylen,"SDL does not advertise %ux%ux%u",
                 selected->w,selected->h,selected->bpp);
    } else {
        window=SDL_CreateWindow("VTEST SDL3",selected->w,selected->h,
                                SDL_WINDOW_HIDDEN);
        if(!window || !SDL_SetWindowFullscreenMode(window,chosen) ||
           !SDL_SetWindowFullscreen(window,true) || !SDL_ShowWindow(window)) {
            snprintf(why,whylen,"SDL cannot set mode: %.100s",SDL_GetError());
        } else {
            surface=SDL_GetWindowSurface(window);
            if(!surface || surface->w!=selected->w ||
               surface->h!=selected->h ||
               sdl_storage_bpp(surface->format)!=selected->bpp) {
                snprintf(why,whylen,"SDL returned a different size/depth");
            } else {
                bytespp=SDL_BYTESPERPIXEL(surface->format);
                fmt=SDL_GetPixelFormatDetails(surface->format);
                palette=SDL_GetSurfacePalette(surface);
                if(selected->bpp==8 && !palette)
                    palette=SDL_CreateSurfacePalette(surface);
                if(bytespp<1 || bytespp>4 || !fmt ||
                   (selected->bpp==8 && !palette)) {
                    snprintf(why,whylen,"SDL surface/palette unavailable");
                } else {
                    for(int i=0;i<256;++i) {
                        ChartRGB c=chart_color((unsigned)i);
                        colors[i]=(SDL_Color){c.r,c.g,c.b,SDL_ALPHA_OPAQUE};
                    }
                    if(selected->bpp==8 &&
                       !SDL_SetPaletteColors(palette,colors,0,256)) {
                        snprintf(why,whylen,"SDL palette: %.100s",SDL_GetError());
                    } else if(!SDL_LockSurface(surface)) {
                        snprintf(why,whylen,"SDL lock: %.100s",SDL_GetError());
                    } else {
                        unsigned active=selected->id;
#ifdef __DJGPP__
                        active=hardware_mode();
#endif
                        snprintf(title,sizeof(title),"SDL3 %uX%u %uB ID %04X",
                                 selected->w,selected->h,selected->bpp,active);
                        snprintf(details,sizeof(details),"PITCH %d %s",
                                 surface->pitch,SDL_GetPixelFormatName(surface->format));
                        chart=(Chart){selected->w,selected->h,title,details,
                                      "WAIT 10 SECONDS OR PRESS ESC TO QUIT"};
                        for(unsigned i=0;i<256;++i) {
                            ChartRGB c=chart_color(i);
                            lut[i]=selected->bpp==8?i:SDL_MapRGB(fmt,palette,c.r,c.g,c.b);
                        }
                        for(int y=0;y<surface->h;++y) {
                            uint8_t *dst=(uint8_t*)surface->pixels+(size_t)y*surface->pitch;
                            for(int x=0;x<surface->w;++x) {
                                uint32_t pix=lut[chart_index(&chart,x,y)];
                                for(int b=0;b<bytespp;++b)
                                    dst[x*bytespp+b]=(uint8_t)(pix>>(8*b));
                            }
                        }
                        SDL_UnlockSurface(surface);
                        if(!SDL_UpdateWindowSurface(window)) {
                            snprintf(why,whylen,"SDL present: %.100s",SDL_GetError());
                        } else {
                            uint32_t start=SDL_GetTicks();
                            ok=true;
                            while(SDL_GetTicks()-start<DISPLAY_MS) {
                                SDL_Event e;
                                while(SDL_PollEvent(&e)) {
                                    if(e.type==SDL_EVENT_QUIT ||
                                       (e.type==SDL_EVENT_KEY_DOWN &&
                                        e.key.key==SDLK_ESCAPE)) {
                                        start=SDL_GetTicks()-DISPLAY_MS;
                                        break;
                                    }
                                }
                                SDL_Delay(10);
                            }
                        }
                    }
                }
            }
        }
    }
    SDL_free(available);
    if(window) SDL_DestroyWindow(window);
    if(video_ready) SDL_QuitSubSystem(SDL_INIT_VIDEO);
#ifdef __DJGPP__
    text_mode();
#endif
    return ok;
}

#ifdef __DJGPP__
/* BIOS mode 03h is 80x25 at B800:0000. Assemble the WHOLE menu in one
 * local text buffer, then copy it to VGA memory in one operation. DJGPP
 * stdio and conio may disagree about cursor position on actual cards;
 * mixing printf/cprintf was corrupting the menu on the owner's 486. */
static uint16_t menu_cells[80*25];

static void menu_line(int row,const char *text,uint8_t attribute)
{
    for(int x=0;x<80 && text[x];++x)
        menu_cells[row*80+x]=(uint16_t)(((uint16_t)attribute<<8) |
                                        (unsigned char)text[x]);
}

static void menu_draw(int selected,char backend,const char *message)
{
    const int page=selected/14;
    char line[160];
    for(int i=0;i<80*25;++i) menu_cells[i]=0x0720;
    snprintf(line,sizeof(line),
             "VTEST 486 - VIDEO MODE TEST CARD     VBE %X.%02X  VRAM %u KB",
             vbe_version>>8,vbe_version&255,vram_kib);
    menu_line(0,line,0x0F);
    menu_line(1,"by @mmoroca & Arena.AI 2026  http://Arena.AI",0x0F);
    menu_line(2,"Resolution   Depth  BIOS ID   LFB   Bank      Backend choice",0x07);
    menu_line(3,"UP/DOWN: select PGUP/DWN: page ENTER: test K: Bank S: SDL3 ESC: quit",0x07);
    snprintf(line,sizeof(line),"Page %d / %d  |  selected backend: %s  |  B: BIOS",
             page+1,(mode_count+13)/14,
             backend=='S'?"SDL3":(backend=='K'?"BIOS BANK":"BIOS AUTO"));
    menu_line(4,line,0x07);
    for(int i=page*14;i<mode_count && i<(page+1)*14;++i) {
        const VideoMode *m=&mode_list[i];
        snprintf(line,sizeof(line),
                 "%c %3d  %4ux%-4u  %2u bpp   %04X     %s     %s",
                 i==selected?'>':' ',i+1,m->w,m->h,m->bpp,m->id,
                 (m->attributes&0x80)?"YES":"NO ",
                 (m->win_attr&5)==5?"YES":"NO ");
        menu_line(6+i-page*14,line,i==selected?0x0E:0x07);
    }
    menu_line(21,"Select mode and BIOS/Bank/SDL, then ENTER to test for 10 secs",0x07);
    if(message && *message) {
        snprintf(line,sizeof(line),"Last result: %.65s",message);
        menu_line(22,line,0x07);
    }
    dosmemput(menu_cells,sizeof(menu_cells),0xB8000ul);
}
#endif

int main(int argc,char **argv)
{
    char error[160]={0};
    int selected=0;
    char backend='B';
    (void)argc;(void)argv;
#ifndef __DJGPP__
    puts("VTEST.EXE is a DOS/486 VGA-VBE utility. Build with DJGPP.");
    return 1;
#else
    if(!SDL_Init(0)) {
        fprintf(stderr,"VTEST base init: %s\n",SDL_GetError());
        return 1;
    }
    text_mode();
    collect_modes();
    hide_dos_mouse(); /* menu is keyboard-only */
    if(argc>1 && (!strcmp(argv[1],"/LISTSDL") || !strcmp(argv[1],"/listsdl"))) {
        if(SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            int n=0;
            SDL_DisplayMode **list=SDL_GetFullscreenDisplayModes(
                SDL_GetPrimaryDisplay(),&n);
            printf("VTEST: SDL3 advertises %d modes\n",n);
            for(int i=0;i<n;++i)
                if(wanted_resolution(list[i]->w,list[i]->h))
                    printf("%dx%d %u bpp %s\n",list[i]->w,list[i]->h,
                           sdl_storage_bpp(list[i]->format),
                           SDL_GetPixelFormatName(list[i]->format));
            SDL_free(list);
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        } else printf("SDL video init failed: %s\n",SDL_GetError());
        text_mode();SDL_Quit();return 0;
    }
    if(argc>1 && (!strcmp(argv[1],"/LIST") || !strcmp(argv[1],"/list"))) {
        printf("VTEST: VBE %X.%02X, %u KB video RAM; %d selected modes\n",
               vbe_version>>8,vbe_version&255,vram_kib,mode_count);
        for(int i=0;i<mode_count;++i) {
            const VideoMode *m=&mode_list[i];
            printf("%04X: %ux%u %u bpp pitch=%u LFB=%u bank=%u\n",
                   m->id,m->w,m->h,m->bpp,m->pitch,
                   !!(m->attributes&0x80),!!((m->win_attr&5)==5));
        }
        SDL_Quit();
        return 0;
    }
    for(;;) {
        int key;
        menu_draw(selected,backend,error);
        key=getch();
        if(key==27) break;
        if(key==0 || key==0xe0) {
            key=getch();
            if(key==72 && selected>0) --selected;
            if(key==80 && selected<mode_count-1) ++selected;
            if(key==73) selected=selected>=14?selected-14:0;
            if(key==81) selected=selected+14<mode_count?selected+14:mode_count-1;
            if(key==71) selected=0;
            if(key==79) selected=mode_count-1;
            continue;
        }
        if(key=='b' || key=='B') {backend='B';continue;}
        if(key=='k' || key=='K') {backend='K';continue;}
        if(key=='s' || key=='S') {backend='S';continue;}
        if(key==13) {
            bool ok;
            error[0]='\0';
            if(backend!='S') ok=direct_preview(&mode_list[selected],
                            backend=='K',error,sizeof(error));
            else ok=sdl_preview(&mode_list[selected],error,sizeof(error));
            if(ok) snprintf(error,sizeof(error),"%s %ux%u %u bpp completed",
                            backend=='S'?"SDL3":(backend=='K'?"BIOS BANK":"BIOS"),
                            mode_list[selected].w,
                            mode_list[selected].h,mode_list[selected].bpp);
            text_mode();
        }
    }
    text_mode();
    SDL_Quit();
    puts("VTEST: bye!");
    return 0;
#endif
}
