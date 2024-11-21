typedef signed char int8;
typedef signed short int16;
typedef unsigned char byte;
typedef unsigned short word;

struct Image {
    const byte *tiles;
    word tiles_size;
    const byte *color;
    word color_size;
    const byte *index;
    word index_size;
};

#include "data.h"

#define ADDR(obj)	((word) (obj))
#define BYTE(addr)	(* (volatile byte *) (addr))
#define WORD(addr)	(* (volatile word *) (addr))
#define SIZE(array)	(sizeof(array) / sizeof(*(array)))

static volatile byte vblank;
static byte *map_y[192];

#if defined(ZXS)
#define SETUP_STACK()	__asm__("ld sp, #0xfdfc")
#define IRQ_BASE	0xfe00
#endif

static void __sdcc_call_hl(void) __naked {
    __asm__("jp (hl)");
}

static void memset(byte *ptr, byte data, word len) {
    while (len-- > 0) { *ptr++ = data; }
}

static void memcpy(byte *dst, byte *src, word len) {
    while (len-- > 0) { *dst++ = *src++; }
}

static void interrupt(void) __naked {
    __asm__("di");
    __asm__("push af");
    __asm__("ld a, #1");
    __asm__("ld (_vblank), a");
    __asm__("pop af");
    __asm__("ei");
    __asm__("reti");
}

static void setup_irq(byte base) {
    __asm__("di");
    __asm__("ld i, a"); base;
    __asm__("im 2");
    __asm__("ei");
}

static void out_fe(byte data) {
    __asm__("out (#0xfe), a"); data;
}

static void setup_system(void) {
#if defined(ZXS)
    byte top = (byte) ((IRQ_BASE >> 8) - 1);
    word jmp_addr = (top << 8) | top;
    BYTE(jmp_addr + 0) = 0xc3;
    WORD(jmp_addr + 1) = ADDR(&interrupt);
    memset((byte *) IRQ_BASE, top, 0x101);
    setup_irq(IRQ_BASE >> 8);
#endif
}

static const byte pixel_map[] = {
    0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01
};

static void put_pixel(byte x, byte y) {
    map_y[y][x >> 3] ^= pixel_map[x & 7];
}

static byte get_pixel(byte x, byte y) {
    return map_y[y][x >> 3] & pixel_map[x & 7];
}

static int16 abs(int16 value) {
    return value < 0 ? -value : value;
}

static void plot_line(byte x0, byte y0, byte x1, byte y1) {
    int8 sx = x0 < x1 ? 1 : -1;
    int8 sy = y0 < y1 ? 1 : -1;
    int16 dx = abs(x1 - x0);
    int16 dy = -abs(y1 - y0);
    int16 error = dx + dy;

    while (x0 != x1 || y0 != y1) {
        int16 e2 = 2 * error;
	put_pixel(x0, y0);
        if (e2 >= dy) {
            error = error + dy;
            x0 = x0 + sx;
        }
        if (e2 <= dx) {
            error = error + dx;
            y0 = y0 + sy;
        }
    }
}

static void precalculate(void) {
#if defined(ZXS)
    for (byte y = 0; y < 192; y++) {
	byte f = ((y & 7) << 3) | ((y >> 3) & 7) | (y & 0xc0);
	map_y[y] = (byte *) (0x4000 + (f << 5));
    }
#endif
}

static void clear_screen(void) {
#if defined(ZXS)
    memset((byte *) 0x5800, 0x00, 0x300);
    memset((byte *) 0x4000, 0x00, 0x1800);
    out_fe(0);
#endif
}

static void put_char(char symbol, word n, byte color) {
    byte x = n & 0x1f;
    byte y = (n >> 2) & ~7;
    byte *ptr = map_y[y] + x;
    byte *addr = (byte *) 0x3c00 + (symbol << 3);
    for (byte i = 0; i < 8; i++) {
	*ptr = *addr++;
	ptr += 0x100;
    }
    BYTE(0x5800 + n) = color;
}

static void put_str(const char *msg, word n, byte color) {
    while (*msg != 0) {
	put_char(*(msg++), n++, color);
    }
}

static char to_hex(byte digit) {
    return (digit < 10) ? '0' + digit : 'A' + digit - 10;
}

static void put_num(word num, word n, byte color) {
    char msg[] = "0000";
    for (byte i = 0; i < 4; i++) {
	msg[3 - i] = to_hex(num & 0xf);
	num = num >> 4;
    }
    put_str(msg, n, color);
}

static void draw_tile(byte *data, byte x, byte y) {
    y = y << 3;
    do {
	map_y[y++][x] = *(data++);
    } while (y & 7);
}

static void uncompress(byte *dst, byte *src, word size) {
    while (size > 0) {
	byte data = (*src & 0x3f) + 1;
	switch (*(src++) & 0xc0) {
	case 0x00:
	    *(dst++) = data - 1;
	    break;
	case 0x40:
	    memcpy(dst, src, data);
	    size -= data;
	    dst += data;
	    src += data;
	    break;
	case 0x80:
	    memset(dst, *src, data);
	    dst += data;
	    size--;
	    src++;
	    break;
	case 0xc0:
	    memcpy(dst, dst - *src, data);
	    dst += data;
	    size--;
	    src++;
	    break;
	}
	size--;
    }
}

#define COLOR_BUF 0x5800
#define INDEX_BUF 0x5b00
#define TILES_BUF 0x5e00

static void display_image(struct Image *img) {
    uncompress((void *) INDEX_BUF, img->index, img->index_size);
    uncompress((void *) TILES_BUF, img->tiles, img->tiles_size);

    byte *ptr = (byte *) INDEX_BUF;
    for (byte y = 0; y < 24; y++) {
	for (byte x = 0; x < 32; x++) {
	    word offset = *(ptr++);
	    offset = TILES_BUF + (offset << 3);
	    draw_tile((byte *) offset, x, y);
	}
    }

    uncompress((void *) COLOR_BUF, img->color, img->color_size);
}

void reset(void) {
    SETUP_STACK();
    setup_system();
    precalculate();
    clear_screen();

    display_image(&image);
    for (;;) { }
}
