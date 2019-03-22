#include <n64.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include "gfx.h"

#define     GFX_SIZE 0x7500

 static z2_disp_buf_t gfx_disp;
 static z2_disp_buf_t gfx_disp_work;

extern char _raw_font[];
gfx_font *kfont;

static Gfx kzgfx[] = {
    gsDPPipeSync(),

    gsSPLoadGeometryMode(0),
    gsDPSetScissor(G_SC_NON_INTERLACE,
              0, 0, Z2_SCREEN_WIDTH, Z2_SCREEN_HEIGHT),

    gsDPSetOtherMode(G_AD_DISABLE | G_CD_DISABLE |
        G_CK_NONE | G_TC_FILT |
        G_TD_CLAMP | G_TP_NONE |
        G_TL_TILE | G_TT_NONE |
        G_PM_NPRIMITIVE | G_CYC_1CYCLE |
        G_TF_BILERP, // HI
        G_AC_NONE | G_ZS_PRIM |
        G_RM_XLU_SURF | G_RM_XLU_SURF2), // LO
    
    gsSPEndDisplayList()
};

void gfx_printf(uint16_t left, uint16_t top, const char *format, ...){
    va_list args;
    va_start(args,format);
    gfx_printf_va_color(left,top,GPACK_RGBA8888(0xFF,0xFF,0xFF,0xFF),format,args);
    va_end(args);
}

void gfx_printf_color(uint16_t left, uint16_t top, uint32_t color, const char *format, ...){
    va_list args;
    va_start(args,format);
    gfx_printf_va_color(left,top,color,format,args);
    va_end(args);
}

void gfx_printf_va_color(uint16_t left, uint16_t top, uint32_t color, const char *format, va_list va){
    const size_t max_len = 1024;
    char buf[max_len];
    int l = vsnprintf(buf,max_len,format,va);
    if(l>max_len-1) l=max_len-1;

    gfx_printchars(kfont, left, top, color, buf, l);
}

void gfx_disp_buf_init(z2_disp_buf_t *db, size_t size){
    db->size = size;
    db->buf = malloc(size);
    db->p = db->buf;
    db->d = db->buf + (size + sizeof(*(db->buf)) - 1) / sizeof(db->buf);
}

void gfx_disp_buf_copy(z2_disp_buf_t *src, z2_disp_buf_t *dst){
    dst->buf = src->buf;
    dst->size = src->size;
    dst->p = dst->buf;
    dst->d = dst->buf + (dst->size + sizeof(*(dst->buf)) - 1) / sizeof(dst->buf);
}

void gfx_disp_buf_destroy(z2_disp_buf_t *db){
    if(db->buf){
        free(db->buf);
        db->buf = NULL;
    }
    db->size = 0;
    db->p = NULL;
    db->d = NULL;
}

void gfx_disp_buf_reset(z2_disp_buf_t *db, size_t newsize){
    if(db->buf==NULL){
        gfx_disp_buf_init(db,newsize);
        return;
    }
    db->size = newsize;
    db->buf = realloc(db->buf, newsize);
    db->p = db->buf;
    db->d = db->buf + (newsize + sizeof(*(db->buf)) - 1) / sizeof(db->buf);
}

void gfx_init(){
    gfx_disp_buf_init(&gfx_disp,GFX_SIZE);
    gfx_disp_buf_init(&gfx_disp_work, GFX_SIZE);
    
    kfont = malloc(sizeof(gfx_font));
    static gfx_texture f_tex;
    f_tex.data = _raw_font;
    f_tex.img_fmt = G_IM_FMT_I;
    f_tex.img_size = G_IM_SIZ_4b;
    f_tex.tile_width = 16;
    f_tex.tile_height = 128;
    f_tex.tile_size = ((f_tex.tile_width * f_tex.tile_height * G_SIZ_BITS(f_tex.img_size) + 7) / 8 + 63) / 64 * 64;
    f_tex.x_tiles = 1;
    f_tex.y_tiles = 3; 
    kfont->texture = &f_tex;
    kfont->c_width = 8;
    kfont->c_height = 8;
    kfont->cx_tile = 2;
    kfont->cy_tile = 16;
}

void gfx_begin(){
    gSPDisplayList(gfx_disp.p++,&kzgfx);
}

void gfx_finish(){
    gSPEndDisplayList(gfx_disp.p++);
    gSPDisplayList(z2_game.common.gfx->overlay.p++, gfx_disp.buf);
    z2_disp_buf_t disp_w;
    gfx_disp_buf_copy(&gfx_disp_work, &disp_w);
    gfx_disp_buf_copy(&gfx_disp,&gfx_disp_work);
    gfx_disp_buf_copy(&disp_w, &gfx_disp);
}

void gfx_load_tile(gfx_texture *texture, uint16_t tilenum){
    if(texture->img_size == G_IM_SIZ_4b){
        gDPLoadTextureTile_4b(gfx_disp.p++, texture->data + (texture->tile_size * tilenum),
            texture->img_fmt, texture->tile_width, texture->tile_height,
            0, 0, texture->tile_width - 1, texture->tile_height-1,
            0, 
            G_TX_NOMIRROR | G_TX_WRAP,
            G_TX_NOMIRROR | G_TX_WRAP,
            G_TX_NOMASK, G_TX_NOMASK,
            G_TX_NOLOD, G_TX_NOLOD);
    }else{
        gDPLoadTextureTile(gfx_disp.p++, texture->data + (texture->tile_size * tilenum),
            texture->img_fmt, texture->img_size,
            texture->tile_width, texture->tile_height,
            0, 0, texture->tile_width - 1, texture->tile_height-1,
            0, 
            G_TX_NOMIRROR | G_TX_WRAP,
            G_TX_NOMIRROR | G_TX_WRAP,
            G_TX_NOMASK, G_TX_NOMASK,
            G_TX_NOLOD, G_TX_NOLOD);
    }
}

void desaturate(void *data, size_t len){
    struct rgba{
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
    };

    MtxF m = guDefMtxF(0.3086f, 0.6094f, 0.0820f, 0.f,
                       0.3086f, 0.6094f, 0.0820f, 0.f,
                       0.3086f, 0.6094f, 0.0820f, 0.f,
                       0.f,     0.f,     0.f,     1.f);

    struct rgba *datargba = data;
    for(int i=0;i<len/sizeof(struct rgba);i++){
        struct rgba p = datargba[i];
        float r = p.r * m.xx + p.g*m.xy + p.g*m.xz + p.a*m.xw;
        float g = p.r * m.yx + p.g*m.yy + p.g*m.yz + p.a*m.yw;
        float b = p.r * m.zx + p.g*m.zy + p.g*m.zz + p.a*m.zw;
        float a = p.r * m.wx + p.g*m.wy + p.g*m.wz + p.a*m.ww;
        struct rgba n = {
            r<0?0:r>0xFF?0xFF:r,
            g<0?0:g>0xFF?0xFF:g,
            b<0?0:g>0xFF?0xFF:b,
            a<0?0:a>0xFF?0xFF:a,
        };
        datargba[i] = n;
    }
}

gfx_texture *gfx_load_icon_item_static(){
    gfx_texture *texture = malloc(sizeof(*texture));
    if(texture){
        texture->data = memalign(64, (0x1000 * (Z2_ITEM_BOTTLE + 1)) * 2 );
        for(int i=0;i<=Z2_ITEM_BOTTLE;i++){
            z2_DecodeArchiveFile(z2_icon_item_static_vrom,i,texture->data + (0x1000 * i));
            // Copy newly decoded file and desaturate
            memcpy(texture->data + (0x1000 * (Z2_ITEM_BOTTLE + 1)) + (0x1000 * i), texture->data + (0x1000 * i), 0x1000);
            desaturate(texture->data + (0x1000 * (Z2_ITEM_BOTTLE + 1)) + (0x1000 * i),0x1000);
        }
        texture->img_fmt = G_IM_FMT_RGBA;
        texture->img_size = G_IM_SIZ_32b;
        texture->tile_width = 32;
        texture->tile_height = 32;
        texture->x_tiles = 1;
        texture->y_tiles = (Z2_ITEM_BOTTLE + 1) * 2;
        texture->tile_size = G_SIZ_BITS(G_IM_FMT_RGBA) * texture->tile_width * texture->tile_height;
    }
    return texture;
}

gfx_texture *gfx_load_bottle_icons(){
    gfx_texture *texture = malloc(sizeof(*texture));
    if(texture){
        texture->data = memalign(64, 0x1000 * (Z2_ITEM_BOTTLE2 - Z2_ITEM_BOTTLE + 1));
        for(int i=Z2_ITEM_BOTTLE;i<=Z2_ITEM_BOTTLE2;i++){
            z2_DecodeArchiveFile(z2_icon_item_static_vrom,i,texture->data + (0x1000 * (i - Z2_ITEM_BOTTLE)));
        }
        texture->img_fmt = G_IM_FMT_RGBA;
        texture->img_size = G_IM_SIZ_32b;
        texture->tile_width = 32;
        texture->tile_height = 32;
        texture->x_tiles = 1;
        texture->y_tiles = Z2_ITEM_BOTTLE2 - Z2_ITEM_BOTTLE + 1;
        texture->tile_size = G_SIZ_BITS(G_IM_FMT_RGBA) * texture->tile_width * texture->tile_height;
    }
    return texture;
}

gfx_texture *gfx_load_trade_icons(){
    gfx_texture *texture = malloc(sizeof(*texture));
    if(texture){
        texture->data = memalign(64, 0x1000 * (Z2_ITEM_PENDANT - Z2_ITEM_MOONS_TEAR + 1));
        for(int i=Z2_ITEM_MOONS_TEAR;i<=Z2_ITEM_PENDANT;i++){
            z2_DecodeArchiveFile(z2_icon_item_static_vrom,i,texture->data + (0x1000 * (i - Z2_ITEM_MOONS_TEAR)));
        }
        texture->img_fmt = G_IM_FMT_RGBA;
        texture->img_size = G_IM_SIZ_32b;
        texture->tile_width = 32;
        texture->tile_height = 32;
        texture->x_tiles = 1;
        texture->y_tiles = Z2_ITEM_PENDANT - Z2_ITEM_MOONS_TEAR + 1;
        texture->tile_size = G_SIZ_BITS(G_IM_FMT_RGBA) * texture->tile_width * texture->tile_height;
    }
    return texture;
}

gfx_texture *gfx_load_game_texture(g_ifmt_t format, g_isiz_t size, uint16_t width, uint16_t height, uint16_t x_tiles, uint16_t y_tiles, int file, uint32_t offset){
    gfx_texture *texture = malloc(sizeof(*texture));
    if(texture){
        texture->tile_size = ((width * height * G_SIZ_BITS(size) + 7) / 8 + 63) / 64 * 64;
        texture->data = memalign(64, texture->tile_size * x_tiles * y_tiles);
        texture->img_fmt = format;
        texture->img_size = size;
        texture->tile_width = 32;
        texture->tile_height = 32;
        texture->x_tiles = x_tiles;
        texture->y_tiles = y_tiles;
        void *tempdata = malloc(z2_file_table[file].vrom_end - z2_file_table[file].vrom_start);
        OSMesgQueue queue;
        OSMesg msg;
        z2_createOSMesgQueue(&queue,&msg,1);
        z2_getfile_t getfile = {
            z2_file_table[file].vrom_start, tempdata,
            z2_file_table[file].vrom_end - z2_file_table[file].vrom_start,
            NULL, 0, 0,
            &queue, 0
        };
        z2_osSendMessage(&z2_file_msgqueue, &getfile, OS_MESG_NOBLOCK);
        z2_osRecvMessage(&queue,NULL,OS_MESG_BLOCK);
        memcpy(texture->data,tempdata + offset,texture->tile_size * x_tiles * y_tiles);
        free(tempdata);
    }
    return texture;
}

void gfx_draw_sprite(gfx_texture *texture, int x, int y, int tile, int width, int height){
    gfx_load_tile(texture, tile);
    gDPSetPrimColor(gfx_disp.p++,0,0,0xFF,0xFF,0xFF,0xFF);
    gSPScisTextureRectangle(gfx_disp.p++,
                         qs102(x) & ~3,
                         qs102(y) & ~3,
                         qs102(x + texture->tile_width * 1 + 1) & ~3,
                         qs102(y + texture->tile_height * 1 + 1) & ~3,
                         G_TX_RENDERTILE,
                         qu105(0),
                         qu105(0),
                         qu510(1/1), qu510(1/1));
}

void gfx_destroy_texture(gfx_texture *texture){
    if(texture){
        if(texture->data) free(texture->data);
        free(texture);
    }
    texture = NULL;
}

void gfx_printchars(gfx_font *font, uint16_t x, uint16_t y, uint32_t color, const char *chars, size_t charcnt){

    gDPSetCombineMode(gfx_disp.p++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

    int chars_per_tile = font->cx_tile * font->cy_tile;

    for(int i=0;i<font->texture->x_tiles * font->texture->y_tiles;i++){
        int tile_start = chars_per_tile*i;
        int tile_end = tile_start + chars_per_tile;

        gfx_load_tile(font->texture,i);
        int char_x = 0;
        int char_y = 0;
        for(int j=0;j<charcnt;j++, char_x += font->c_width){
            char c = chars[j];
            if(c<33) continue;
            c-=33;
            if(c<tile_start || c>=tile_end) continue;
            c-=tile_start; 
            gDPSetPrimColor(gfx_disp.p++, 0, 0, 0x00,0x00,0x00, 0xFF);
            gSPScisTextureRectangle(gfx_disp.p++,
                         qs102(x + char_x + 1 ),
                         qs102(y + char_y + 1),
                         qs102(x + char_x + font->c_width + 1),
                         qs102(y + char_y + font->c_height + 1),
                         G_TX_RENDERTILE,
                         qu105(c % font->cx_tile *
                               font->c_width),
                         qu105(c / font->cx_tile *
                               font->c_height),
                         qu510(1), qu510(1));
            gDPSetPrimColor(gfx_disp.p++, 0, 0, (color >> 24) & 0xFF, (color >> 16) & 0xFF, (color >> 8) & 0xFF, 0xFF);
            gSPScisTextureRectangle(gfx_disp.p++,
                         qs102(x + char_x),
                         qs102(y + char_y),
                         qs102(x + char_x + font->c_width),
                         qs102(y + char_y + font->c_height),
                         G_TX_RENDERTILE,
                         qu105(c % font->cx_tile *
                               font->c_width),
                         qu105(c / font->cx_tile *
                               font->c_height),
                         qu510(1), qu510(1));
        }
    }
}