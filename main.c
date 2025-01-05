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

static void delay(byte n) {
    while (n-- > 0) wait_vblank();
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

static void put_char(char symbol, byte x, byte y) {
    byte shift = x & 7;
    byte offset = x >> 3;
    byte *addr = (byte *) 0x3c00 + (symbol << 3);
    for (byte i = 0; i < 8; i++) {
	byte data = *addr++;
	byte *ptr = map_y[y + i] + offset;
	ptr[0] |= (data >> shift);
	ptr[1] |= (data << (8 - shift));
    }
}

static byte char_mask(char symbol) {
    byte mask = 0;
    byte *addr = (byte *) 0x3c00 + (symbol << 3);
    for (byte i = 0; i < 8; i++) {
	mask |= *addr++;
    }
    return mask;
}

static byte leading(char symbol) {
    byte i;
    byte mask = char_mask(symbol);
    for (i = 0; i < 8; i++) {
	if (mask & 0x80) goto done;
	mask = mask << 1;
    }
  done:
    return i - 1;
}

static byte trailing(char symbol) {
    byte i;
    byte mask = char_mask(symbol);
    for (i = 0; i < 8; i++) {
	if (mask & 1) goto done;
	mask = mask >> 1;
    }
  done:
    return 8 - i;
}

static void put_str(const char *msg, byte x, byte y) {
    while (*msg != 0) {
	char symbol = *(msg++);
	if (symbol == ' ') {
	    x = x + 4;
	}
	else {
	    byte lead = leading(symbol);
	    if (lead <= x) x -= lead;
	    put_char(symbol, x, y);
	    x += trailing(symbol);
	}
    }
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
    " Each year in the kingdom of Mondlauf,",
    "the second full moon casts its rays on",
    "royal ponds solidifying water for a few",
    "seconds, just enough for an agile person",
    "to leap over waves. King Lamsack offers",
    "a challenge: Anyone who crosses a series",
    "of his ponds gets a small patch of land,",
    "a sack of seed potatoes and a big jug of",
    "the finest moonshine as a reward.",
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
    display_strip(&title, 0);
    for (byte i = 0; i < SIZE(intro); i++) {
	put_str(intro[i], 20, 80 + (i << 3));
    }
    put_str("Press SPACE to participate", 52, 168);

    const byte *ptr = credits;
    for (byte i = 0; i < 8; i++) {
	memcpy(map_y[184 + i], ptr, 8);
	ptr += 8;
    }

    byte roll = 0;
    while (!SPACE_DOWN()) {
	wait_vblank();
	lit_line(roll - 32, 0x01);
	lit_line(roll - 16, 0x41);
	roll = (roll + 1) & 0x3f;
    }
}

static int8 vel;
static byte pos;
static byte jump;
static int8 lives;
static word scroll;
static const byte *frame;

#define MAX_WAVES	128
#define VELOCITY	-12

static byte wave_count;

static byte wave_data[MAX_WAVES];
static byte *wave_addr[MAX_WAVES];

static void reset_variables(void) {
    vel = 0;
    jump = 0;
    scroll = 0;
    wave_count = 0;

    pos = 128;
}

static void init_variables(void) {
    lives = 6;
    frame = runner;
    reset_variables();
}

static void clear_player(void) {
    byte y = pos;
    const byte *ptr = frame;
    for (byte i = 0; i < 8; i++) {
	map_y[y++][8] ^= *ptr++;
    }
}

static void erase_player(byte x, byte y) {
    for (byte i = 0; i < 8; i++) {
	map_y[y++][x] = 0;
    }
}

static byte draw_player(void) {
    byte y = pos;
    const byte *ptr = frame;
    for (byte i = 0; i < 8; i++) {
	byte data = *ptr++;
	byte *addr = map_y[y++] + 8;
	if (*addr & data) return 1;
	*addr |= data;
    }
    return 0;
}

static byte contact(void) {
    return map_y[pos + 8][8];
}

static void double_jump(byte space) {
    if (!space) {
	if (jump == 0) jump++;
    }
    else if (jump == 1) {
	vel = VELOCITY;
	jump++;
    }
}

static void move_player(void) {
    byte space = SPACE_DOWN();
    if (vel > 0 && contact()) {
	vel = space ? VELOCITY : 0;
	jump = 0;
    }
    else {
	byte new;
	vel = vel + 1;
	double_jump(space);
	new = pos + (vel >> 2);
	if (vel > 0) {
	    for (; pos < new; pos++) {
		if (contact()) return;
	    }
	}
	pos = new;
    }
}

static void animate_player(void) {
    move_player();
    if (!contact()) {
	frame = runner + (jump == 2 ? 0 : 48);
    }
    else {
	if (ticker & 1) frame += 8;
	if (frame - runner >= 64) {
	    frame = runner;
	}
    }
}

static void draw_pond_waves(void) {
    for (byte i = 0; i < wave_count; i++) {
	*wave_addr[i] = wave_data[i];
    }
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

static const byte bridge[] = { 0xff, 0x22, 0x44 };

static void setup_moon_shade(void) {
    memset((void *) 0x5900, 1, 0x200);
    shade_cone((byte *) 0x5902, 5, 14, 0);
    shade_cone((byte *) 0x5903, 7, 12, 1);
    for (byte i = 0; i <= 2; i++) {
	memset(map_y[pos + 0x8 + i], bridge[i], 9);
    }
}

static void animate_wave(void) {
    frame = waver + (ticker & 16 ? 0 : 8);
}

static void wave_before_start(void) {
    while (pos == 128 && !SPACE_DOWN()) {
	animate_wave();
	draw_player();
	wait_vblank();
	clear_player();
    }
}

static void drown_player(void) {
    frame = drowner;
    while (frame < drowner + sizeof(drowner)) {
	if ((ticker & 3) == 0) frame += 8;
	draw_player();
	wait_vblank();
	clear_player();
    }
}

static void prepare_level(byte data) {
    const byte *ptr = level1 + 8;
    for (byte i = 0; i < 8; i++) {
	for (byte n = 0; n < level1[i]; n++) {
	    byte *addr = (* (byte **) ptr) + ptr[2];
	    memset(addr + 1, data, ptr[3]);
	    ptr += 4;
	}
    }
}

static const byte fade_in[] =  {
    0x18, 0x3c, 0x7e, 0xff,
};
static const byte fade_out[] = {
    0x7e, 0x3c, 0x18, 0x00,
};

static void fade_level(const byte *ptr) {
    while (*ptr != 0x00 && *ptr != 0xff) {
	prepare_level(*ptr++);
	delay(3);
    }
    prepare_level(*ptr);
}

static const byte *level_ptr;
static void scroller(byte count, byte offset, byte data) {
    memset(wave_data + wave_count, data, count);

    while (count-- > 0) {
	byte *addr = * (byte **) level_ptr;
	addr += (level_ptr[2] - offset) & 0x1f;
	wave_addr[wave_count++] = addr;
	level_ptr += 4;
    }
}

static const byte scroll1_f[] = {
    0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff
};

static const byte scroll1_b[] = {
    0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80, 0x00
};

static const byte scroll2_f[] = {
    0x03, 0x0f, 0x3f, 0xff
};

static const byte scroll2_b[] = {
    0xfc, 0xf0, 0xc0, 0x00
};

static byte scroll_data(byte i) {
    switch (i) {
    case 0:
	return 0x00;
    case 1:
	return 0xff;
    case 2:
	return (scroll & 1) ? 0x00 : 0xf0;
    case 3:
	return (scroll & 1) ? 0xff : 0x0f;
    case 4:
	return scroll2_b[scroll & 3];
    case 5:
	return scroll2_f[scroll & 3];
    case 6:
	return scroll1_b[scroll & 7];
    case 7:
	return scroll1_f[scroll & 7];
    }
    return 0x00;
}

static void draw_and_clear_bridge(void) {
    word length = 256;
    byte offset = scroll >> 3;

    if (scroll >= length) {
	byte data = scroll_data(7);
	for (byte i = 0; i <= 2; i++) {
	    byte *addr = map_y[136 + i] + 31 - (offset & 0x1f);
	    wave_data[wave_count] = data & bridge[i];
	    wave_addr[wave_count] = addr;
	    wave_count++;
	}
    }

    if (scroll < 72 || scroll >= length + 72) {
	byte from = scroll < 72 ? 8 : 40;
	byte data = scroll_data(6);
	for (byte i = 136; i <= 138; i++) {
	    byte *addr = map_y[i] + from - (offset & 0x1f);
	    wave_data[wave_count] = data & *addr;
	    wave_addr[wave_count] = addr;
	    wave_count++;
	}
    }
}

static void move_level(void) {
    wave_count = 0;
    byte offset = scroll;
    level_ptr = level1 + 8;
    for (byte i = 0; i < 8; i++) {
	scroller(level1[i], offset, scroll_data(i));
	if (i & 1) offset >>= 1;
    }
    draw_and_clear_bridge();
    scroll++;
}

static byte level_done(void) {
    return scroll > 512;
}

static void stop_player(void) {
    clear_player();
    animate_wave();
    draw_player();
}

static void game_loop(void) {
    byte drown = 0;

    reset_variables();
    setup_moon_shade();
  restart:
    scroll = 0;
    wave_count = 0;
    fade_level(fade_in);
    wave_before_start();
    frame = runner;
    draw_player();
    wait_vblank();

    while (!drown && pos < 184) {
	/* draw */
	clear_player();
	animate_player();
	draw_pond_waves();
	drown = draw_player();

	/* calculate */
	move_level();
	if (level_done()) {
	    fade_level(fade_out);
	    goto restart;
	}

	/* done */
	wait_vblank();
    }

    erase_player(8, pos);
    drown_player();
}

static void game_over(void) {
    clear_screen();
    put_str("GAME OVER", 98, 92);
    memset((void *) 0x5900, 1, 0x100);
    while (!SPACE_DOWN()) { }
    reset();
}

static void top_level(void) {
    display_strip(&horizon, 0);
    byte grass = map_y[63][8];

    while (lives-- >= 0) {
	game_loop();
	map_y[63][8] = grass;
	memset((void *) 0x4800, 0, 0x1000);
	if (lives >= 0) erase_player(21 + lives, 44);
    }

    game_over();
}

void reset(void) {
    SETUP_STACK();
    setup_system();
    precalculate();
    clear_screen();
    init_variables();
    show_title();
    clear_screen();
    top_level();
    for (;;) { }
}
