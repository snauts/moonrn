typedef signed char int8;
typedef signed short int16;
typedef unsigned char byte;
typedef unsigned short word;

struct Image {
    const byte *pixel;
    word pixel_size;
    const byte *color;
    word color_size;
};

#include "data.h"

#define ADDR(obj)	((word) (obj))
#define BYTE(addr)	(* (volatile byte *) (addr))
#define WORD(addr)	(* (volatile word *) (addr))
#define SIZE(array)	(sizeof(array) / sizeof(*(array)))

static volatile byte vblank;
static volatile byte ticker;
static byte *map_y[192];

#if defined(ZXS)
#define SETUP_STACK()	__asm__("ld sp, #0xfdfc")
#define SPACE_DOWN()	!(in_fe(0x7f) & 0x01)
#define IRQ_BASE	0xfe00
#endif

static void __sdcc_call_hl(void) __naked {
    __asm__("jp (hl)");
}

static void memset(byte *ptr, byte data, word len) {
    while (len-- > 0) { *ptr++ = data; }
}

static void memcpy(byte *dst, const byte *src, word len) {
    while (len-- > 0) { *dst++ = *src++; }
}

static void memswp(byte *dst, byte *src, word len) {
    while (len-- > 0) {
	byte tmp = *dst;
	*dst++ = *src;
	*src++ = tmp;
    }
}

static void interrupt(void) __naked {
    __asm__("di");
    __asm__("push af");
    __asm__("push hl");
    __asm__("ld a, #1");
    __asm__("ld (_vblank), a");
    __asm__("ld hl, #_ticker");
    __asm__("inc (hl)");
    __asm__("pop hl");
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

static byte in_fe(byte a) __naked {
    __asm__("in a, (#0xfe)"); a;
    __asm__("ret");
}

static void wait_vblank(void) {
    vblank = 0;
    while (!vblank) { }
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
    map_y[y][x >> 3] |= pixel_map[x & 7];
}

static byte get_pixel(byte x, byte y) {
    return map_y[y][x >> 3] & pixel_map[x & 7];
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

static void uncompress(byte *dst, const byte *src, word size) {
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

static void swizzle_strip(byte *src, byte from) {
    byte to = from + 64;
    while (from < to) {
	byte *dst = map_y[from++];
	if (dst < src) memswp(dst, src, 32);
	src += 32;
    }
}

static void display_strip(struct Image *img, byte strip) {
    byte *ptr = (byte *) 0x4000 + (strip << 11);
    uncompress(ptr, img->pixel, img->pixel_size);
    swizzle_strip(ptr, strip << 6);
    ptr =  (byte *) 0x5800 + (strip << 8);
    uncompress(ptr, img->color, img->color_size);
}

static const char * const intro[] = {
    "Each year in kingdom of Mondlauf",
    "last full moon casts its rays",
    "on royal ponds solidifying water",
    "for few seconds, just enough for",
    "agile person to leap over waves.",
    "King Lamsack offers a challenge,",
    "anyone who crosses series of his",
    "ponds gets small patch of land,",
    "sack of potatoes and big jug of",
    "finest moonshine as a reward.",
    "", "",
    "   Press SPACE to participate",
};

static void lit_line(byte offset, byte color) {
    word addr = 0x5900;
    for (byte i = 0; i < 16; i++) {
	if (offset < 0x20) {
	    BYTE(addr + offset) = color;
	}
	addr += 32;
	offset++;
    }
}

static void show_title(void) {
    word offset = 0x140;
    display_strip(&title, 0);
    for (byte i = 0; i < SIZE(intro); i++) {
	put_str(intro[i], offset, 0x01);
	offset += 32;
    }

    byte roll = 0;
    while (!SPACE_DOWN()) {
	wait_vblank();
	lit_line(roll - 24, 0x01);
	lit_line(roll - 12, 0x41);
	roll = (roll + 1) & 0x3f;
    }
}

static byte pos;
static byte lives;

static void init_variables(void) {
    lives = 6;
    pos = 128;
}

static byte draw_player(void) {
    byte *ptr = runner + ((ticker & 0xe) << 2);
    for (byte y = 0; y < 8; y++) {
	byte *addr = map_y[pos + y] + 8;
	*addr = *ptr++;
    }
    return 0;
}

static void shade_cone(byte *ptr, byte color, byte width, byte step) {
    for (byte y = 8; y < 24; y++) {
	memset(ptr, color, width);
	if ((y & step) == step) {
	    if (((byte) ptr & 0x1f) > 0) {
		width++;
		ptr--;
	    }
	    if (width < 32) width++;
	}
	ptr += 32;
    }
}

static void setup_moon_shade(void) {
    memset((void *) 0x5900, 1, 0x200);
    shade_cone(0x5902, 5, 14, 0);
    shade_cone(0x5903, 7, 12, 1);
}

static void game_loop(void) {
    display_strip(&horizon, 0);
    setup_moon_shade();

    byte collision = 0;
    while (!collision) {
	wait_vblank();
	collision = draw_player();
    }
}

void reset(void) {
    SETUP_STACK();
    setup_system();
    precalculate();
    clear_screen();
    init_variables();
    show_title();
    clear_screen();
    game_loop();
    for (;;) { }
}
