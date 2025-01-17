#define AY

typedef signed char int8;
typedef signed short int16;
typedef unsigned char byte;
typedef unsigned short word;

struct Image {
    const byte *pixel;
    word pixel_size;
#if defined(ZXS)
    const byte *color;
    word color_size;
#endif
    byte w, h;
};

struct Level {
    const byte *level;
    const char *msg;
    word length;
    byte mask;
};

struct Twinkle {
    const byte *level;
    byte offset;
    byte height;
};

#include "data.h"

#define NULL		((void *) 0)
#define ADDR(obj)	((word) (obj))
#define BYTE(addr)	(* (volatile byte *) (addr))
#define WORD(addr)	(* (volatile word *) (addr))
#define SIZE(array)	(sizeof(array) / sizeof(*(array)))

static volatile byte vblank;
static volatile byte space_up;
static volatile byte ticker;
static volatile byte use_joy;
static byte *map_y[192];
static void *tmp;

void reset(void);

#if defined(ZXS)
#define SPACE_DOWN()	!space_up
#define SETUP_STACK()	__asm__("ld sp, #0xfdfc")
#define FONT_PTR	((byte *) 0x3c00)
#define IRQ_BASE	0xfe00
#define TEMP_BUF	0x5b00
#define WIDTH		0x20
#define PLAYER		8
#define BPP_SHIFT	0
#endif

#if defined(CPC)
#define SPACE_DOWN()	(space_up != 0x90)
#define SETUP_STACK()	__asm__("ld sp, #0x95fc")
#define FONT_PTR	(((byte *) &font_rom) - 0x100)
#define IRQ_BASE	0x9600
#define TEMP_BUF	0xa000
#define WIDTH		0x40
#define PLAYER		16
#define BPP_SHIFT	1
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

static void interrupt(void) __naked {
    __asm__("di");
    __asm__("push af");
    __asm__("push bc");
    __asm__("push hl");

#if defined(CPC)
    __asm__("ld b, #0xf5");
    __asm__("in a, (c)");
    __asm__("and a, #1");
    __asm__("jp z, skip_handler");
#endif

#if defined(AY)
    __asm__("ld a, (_enable_AY)");
    __asm__("and a");
    __asm__("jp z, skip_AY");

    __asm__("push de");
    __asm__("push ix");
    __asm__("push iy");
    __asm__("call _Player_Decode");
    __asm__("call _Player_CopyAY");
    __asm__("pop iy");
    __asm__("pop ix");
    __asm__("pop de");

    __asm__("skip_AY:");
#endif

#if defined(ZXS)
    __asm__("ld a, #0x7f");
    __asm__("in a, (#0xfe)");
    __asm__("and #1");
    __asm__("ld l, a");

    __asm__("ld a, (_use_joy)");
    __asm__("and a");
    __asm__("jp z, dont_use_joy");
    __asm__("ld a, #0x00");
    __asm__("in a, (#0x1f)");
    __asm__("xor #0xff");
    __asm__("sra a");
    __asm__("sra a");
    __asm__("sra a");
    __asm__("sra a");
    __asm__("and l");
    __asm__("ld l, a");
    __asm__("dont_use_joy:");

    __asm__("ld a, l");
    __asm__("ld (_space_up), a");
#endif

#if defined(CPC)
    __asm__("ld a, #5");
    __asm__("call _cpc_key");
    __asm__("and #0x80");
    __asm__("ld l, a");

    __asm__("ld a, #9");
    __asm__("call _cpc_key");
    __asm__("and #0x10");
    __asm__("or l");

    __asm__("ld (_space_up), a");
#endif

    __asm__("ld a, #1");
    __asm__("ld (_vblank), a");

    __asm__("ld hl, #_ticker");
    __asm__("inc (hl)");

    __asm__("skip_handler:");

    __asm__("pop hl");
    __asm__("pop bc");
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

static void wait_vblank(void) {
    vblank = 0;
    while (!vblank) { }
}

static void delay(byte n) {
    while (n-- > 0) wait_vblank();
}

#if defined(CPC)
#include "cpc.c"
#endif

#if defined(AY)
static byte enable_AY;
#include "PT3player.c"

static void start_music(void) {
    Player_Resume();
    enable_AY = 1;
}

static void stop_music(void) {
    enable_AY = 0;
    Player_Pause();
}

static void silence_music(void) {
    PT3_state |= (1 << 2);
}

static void resume_music(void) {
    __asm__("di");
    memset(AYREGS, 0, 14);
    PT3_state &= ~(1 << 2);
    __asm__("ei");
}

static void select_music(void *ptr) {
    static void *current;
    if (enable_AY) {
	if (ptr == current) {
	    return; /* already playing */
	}
	else {
	    stop_music();
	}
    }
    Player_Init();
    Player_InitSong((word) ptr);
    Player_Loop(1);

    current = ptr;
    start_music();
}

static void music_tune(void) {
    __asm__(".incbin \"music.pt3\"");
}
#else

#define select_music(x)
#define silence_music()
#define resume_music()
#define stop_music()

#endif

static void setup_system(void) {
#if defined(AY)
    enable_AY = 0;
#endif

    byte top = (byte) ((IRQ_BASE >> 8) - 1);
    word jmp_addr = (top << 8) | top;
    BYTE(jmp_addr + 0) = 0xc3;
    WORD(jmp_addr + 1) = ADDR(&interrupt);
    memset((byte *) IRQ_BASE, top, 0x101);
    setup_irq(IRQ_BASE >> 8);

#if defined(CPC)
    setup_system_amstrad_cpc();
#endif
}

static void precalculate(void) {
    for (byte y = 0; y < 192; y++) {
#if defined(ZXS)
	byte f = ((y & 7) << 3) | ((y >> 3) & 7) | (y & 0xc0);
	map_y[y] = (byte *) (0x4000 + (f << 5));
#elif defined(CPC)
	word f = ((y & 7) << 11) | mul80(y >> 3);
	map_y[y] = (byte *) (0xC000 + f);
#endif
    }
}

static void clear_screen(void) {
#if defined(ZXS)
    memset((byte *) 0x5800, 0x00, 0x300);
    memset((byte *) 0x4000, 0x00, 0x1800);
    out_fe(0);
#elif defined(CPC)
    memset((byte *) 0xC000, 0x00, 0x4000);
    amstrad_cpc_select_palette(0);
#endif
}

static void put_char(char symbol, byte x, byte y) {
    byte shift = x & (7 >> BPP_SHIFT);
    byte offset = x >> (3 - BPP_SHIFT);
    byte *addr = FONT_PTR + (symbol << 3);
    for (byte i = 0; i < 8; i++) {
	byte data = *addr++;
	byte *ptr = map_y[y + i] + offset;
#if defined(ZXS)
	ptr[0] |= (data >> shift);
	ptr[1] |= (data << (8 - shift));
#elif defined(CPC)
	byte value = data >> shift;
	ptr[0] |= value >> 4;
	ptr[1] |= value & 0xf;
	ptr[2] |= (data << (4 - shift)) & 0xf;
#endif
    }
}

static byte char_mask(char symbol) {
    byte mask = 0;
    byte *addr = FONT_PTR + (symbol << 3);
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

static char to_hex(byte digit) {
    return (digit < 10) ? '0' + digit : 'A' + digit - 10;
}

static void put_num(word num, byte x, byte y) {
    char msg[] = "0000";
    for (byte i = 0; i < 4; i++) {
	msg[3 - i] = to_hex(num & 0xf);
	num = num >> 4;
    }
    put_str(msg, x, y);
}

static byte str_len(const char *msg) {
    byte len = 0;
    while (*msg != 0) {
	char symbol = *(msg++);
	len += symbol == ' ' ? 4 : trailing(symbol) - leading(symbol);
    }
    return len;
}

static byte str_offset(const char *msg, byte from) {
    return from - (str_len(msg) >> 1);
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

static void display_image(struct Image *img, byte x, byte y) {
    byte *ptr = tmp;
    x = x << BPP_SHIFT;
    uncompress(ptr, img->pixel, img->pixel_size);

    byte bottom = (y + img->h) << 3;
    for (byte i = y << 3; i < bottom; i++) {
	memcpy(map_y[i] + x, ptr, img->w);
	ptr += img->w;
    }

#if defined(ZXS)
    uncompress(ptr, img->color, img->color_size);
    byte *addr = (byte *) 0x5800 + (y << 5) + x;
    for (byte i = 0; i < img->h; i++) {
	memcpy(addr, ptr, img->w);
	ptr += img->w;
	addr += 0x20;
    }
#endif
}

#define PiB (8 >> BPP_SHIFT) /* pixels in byte */
static byte *generate_sprite(const byte *src, byte *dst, byte w, byte h) {
#if defined(CPC)
    static const byte mask1[] = { 0xff, 0x77, 0x33, 0x11 };
    static const byte mask2[] = { 0xff, 0xee, 0xcc, 0x88, 0x00 };
#endif
    byte **ptr = dst;
    byte *buf = dst + 16;
    w = w << BPP_SHIFT;
    for (byte i = 0; i < PiB; i++) {
	ptr[i] = buf;
	const byte *from = src;
	for (byte y = 0; y < h; y++) {
	    memset(buf, 0, w + 1);
	    for (byte x = 0; x < w; x++) {
		byte j = (PiB - i);
		byte data = *from++;
#if defined(ZXS)
		buf[0] |= data >> i;
		buf[1] |= data << j;
#elif defined(CPC)
		buf[0] |= (data >> i) & mask1[i];
		buf[1] |= (data << j) & mask2[j];
#endif
		buf++;
	    }
	    buf++;
	}
    }
    return buf;
}

static void put_sprite(byte *addr, byte x, byte y, byte w, byte h) {
    addr = ((byte **) addr)[x & (7 >> BPP_SHIFT)];
    x = x >> (3 - BPP_SHIFT);
    w = (w << BPP_SHIFT) + 1;
    byte top = y + h;
    for (; y < top; y++) {
	byte *ptr = map_y[y] + x;
	for (byte i = 0; i < w; i++) {
	    *ptr++ ^= *addr++;
	}
    }
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
#if defined(ZXS)
    word addr = 0x5900;
    for (byte i = 0; i < 14; i++) {
	if (offset < 0x20) {
	    BYTE(addr + offset) = color;
	}
	addr += 32;
	offset++;
    }
#elif defined(CPC)
    offset; color;
#endif
}

static byte rlc(byte a) {
    __asm__("rlc a");
#if defined(CPC)
    if (a & 0x10) a = (a | 0x01) & 0x0f;
#endif
    return a;
}

static byte rrc(byte a) {
#if defined(ZXS)
    __asm__("rrc a");
    return a;
#elif defined(CPC)
    if (a & 0x01) a |= 0x10;
    return a >> 1;
#endif
}

static void shift_water_row(byte y) {
    byte *addr = map_y[y];
    for (int8 i = 0; i < 32 << BPP_SHIFT; i++) {
	byte value = *addr;
	*addr++ = rlc(value);
    }
}

static void animate_water(void) {
    static const byte ripple[] = { 57, 61, 59, 61 };
    shift_water_row(ripple[ticker & 3]);
}

static char *strcpy(char *dst, const char *src) {
    while (*src) { *dst++ = *src++; }
    *dst = 0;
    return dst;
}

static void center_msg(const char *msg, byte y) {
    put_str(msg, str_offset(msg, 128), y);
}

static byte *run_num;
static const byte run_value = 0;

static byte practice_run(void) {
    return *run_num == 0;
}

static byte hard_run(void) {
    return *run_num >= 2;
}

static byte bonus_run(void) {
    return *run_num >= 3;
}

static void advance_run(void) {
    if (!hard_run()) *run_num = *run_num + 1;
}

static const char* start_string(void) {
    switch (*run_num) {
    case 0:
	return "practice";
    case 1:
	return "participate";
    case 2:
	return "accept challenge";
    default:
	return "assert oneself";
    }
}

static void print_start_message(void) {
    char *ptr = tmp;
    ptr = strcpy(ptr, "Press SPACE to ");
    ptr = strcpy(ptr, start_string());
    center_msg(tmp, 168);
}

#if defined(ZXS)
static byte in_key(byte a) {
    __asm__("in a, (#0xfe)");
    return a;
}

static byte read_j(void) {
    return ~in_key(0xbf) & 0x08;
}
#endif

static void select_joystick(byte a) {
#if defined(ZXS)
    __asm__("in a, (#0x1f)");
    if ((a & 0x10) == 0) use_joy = 1;
#else
    a;
#endif
}

static void show_title(void) {
    for (byte i = 0; i < SIZE(intro); i++) {
	put_str(intro[i], 20, 80 + (i << 3));
    }
    print_start_message();

    display_image(&hazard, 24, 23);
    display_image(&credits, 0, 23);
    display_image(&title, 0, 1);

    use_joy = 0;
    byte roll = 0;
    while (!SPACE_DOWN()) {
	wait_vblank();
	animate_water();
	lit_line(roll - 32, 0x01);
	lit_line(roll - 16, 0x41);
	roll = (roll + 1) & 0x3f;
	select_joystick(0);
    }
}

static int8 vel;
static byte pos;
static byte jump;
static int8 lives;
static int8 level;
static word scroll;
static const byte *frame;

#define MAX_WAVES	128
#define VELOCITY	-12
#define STANDING	128
#define BRIDGE_LEN	(72 << BPP_SHIFT)
#define BRIDGE_TOP	136
#define WAVE_TYPES	8

static byte level_mask;
static word level_length;
static const byte *current_level;

static byte wave_data[MAX_WAVES];
static byte *wave_addr[MAX_WAVES];

static byte twinkle_num;
static byte twinkle_mask;
static word twinkle_offset;
static word twinkle_height;
static byte *twinkle_ptr[3];

static const struct Level level_list[] = {
    { level0, "Victoria",  512, 0x1f },
    { levelB, "Baltic",    256, 0x1f },
    { levelZ, "Pededze",   512, 0x3f },
    { levelP, "Peipus",    512, 0x3f },
    { levelG, "Niagara",   512, 0x3f },
    { levelM, "Mariana",   512, 0x3f },
    { levelN, "Nyos",      512, 0x3f },
    { levelA, "Atlantic",  512, 0x1f },
    { levelS, "Suez",      512, 0x3f },
    { level1, "Liezeris",  512, 0x1f },
    { levelL, "Nile",      512, 0x3f },
    { level2, "Titikaka",  512, 0x1f },
    { level3, "Baikal",    512, 0x1f },
    { levelO, "Amazon",    512, 0x3f },
    { level4, "Panama",    512, 0x1f },
    { level5, "Komo",      512, 0x1f },
    { level6, "Balaton",   512, 0x1f },
    { level7, "Loch Ness", 512, 0x1f },
    { levelC, "Pacific",   512, 0x3f },
};

static const struct Twinkle twinkle_map[] = {
    { levelB, 68, 112 },
    { levelP, 56, 152 },
    { NULL, NULL },
};

static void reset_variables(void) {
    vel = 0;
    jump = 0;
    pos = STANDING;
    twinkle_ptr[2] = NULL;
}

static void init_variables(void) {
    lives = 6;
    level = 1;
    space_up = 0;
    frame = runner;
    reset_variables();
    tmp = (void *) TEMP_BUF;
    run_num = (void *) &run_value;
}

static void clear_player(void) {
    byte y = pos;
    const byte *ptr = frame;
    for (byte i = 0; i < 8; i++) {
	byte *addr = map_y[y++] + PLAYER;
#if defined(CPC)
	*addr++ ^= *ptr++;
#endif
	*addr ^= *ptr++;
    }
}

static void erase_player(byte x, byte y) {
    x = x << BPP_SHIFT;
    for (byte i = 0; i < 8; i++) {
	byte *addr = map_y[y++] + x;
#if defined(CPC)
	*addr++ = 0;
#endif
	*addr = 0;
    }
}

static byte draw_player(void) {
    byte y = pos;
    const byte *ptr = frame;
    for (byte i = 0; i < 8; i++) {
	byte data = *ptr++;
	byte *addr = map_y[y++] + PLAYER;
#if defined(CPC)
	if (*addr & data) return 1;
	*addr++ |= data;
	data = *ptr++;
#endif
	if (*addr & data) return 1;
	*addr |= data;
    }
    return 0;
}

static byte contact(void) {
    byte *addr = map_y[pos + 8] + PLAYER;

#if defined (ZXS)
    return *addr;
#elif defined (CPC)
    return addr[0] | addr[1];
#endif
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
	if (vel < -VELOCITY) {
	    vel = vel + 1;
	}
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
	frame = runner + (jump == 2 ? 0 : (48 << BPP_SHIFT));
    }
    else {
	if (ticker & 1) frame += PLAYER;
	if (frame - runner >= 64 << BPP_SHIFT) {
	    frame = runner;
	}
    }
}

static void draw_pond_waves(void) {
    byte *data = wave_data;
    byte **addr = wave_addr;
    while (*addr) **addr++ = *data++;
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

static const byte bridge[] = { 0xff, 0x44, 0x22 };

static void setup_moon_shade(void) {
#if defined(ZXS)
    memset((void *) 0x5900, 1, 0x200);
    shade_cone((byte *) 0x5902, 5, 14, 0);
    shade_cone((byte *) 0x5903, 7, 12, 1);
    memset((void *) 0x5a80, 5, 0x18);
    memset((void *) 0x5aa0, 5, 0x18);
    memset((void *) 0x5ac0, 1, 0x40);
#elif defined(CPC)
    amstrad_cpc_select_palette(1);
#endif
}

static void draw_bridge(void) {
    for (byte i = 0; i <= 2; i++) {
	byte *addr = map_y[BRIDGE_TOP + i];
	memset(addr, bridge[i], BRIDGE_LEN >> 3);
    }
}

static void clear_twinkle(void) {
    byte *ptr = twinkle_ptr[2];
    if (ptr != NULL) {
	*ptr ^= twinkle_mask;
	twinkle_ptr[2] = NULL;
    }
}

static void sound_fx(word period, byte border);

static void twinkle_sound(void) {
    for (word p = 150; p > 50; p -= 20) sound_fx(p, 0);
}

static byte twinkle_box(byte offset) {
    return offset == 8
	&& (twinkle_mask & 0x7e)
	&& pos >= (twinkle_height - 5)
	&& pos <= (twinkle_height - 3);
}

static void put_bonus(byte offset, byte x) {
    const byte *addr = bonus + offset;
    for (byte y = 32; y < 40; y++) {
	byte *ptr = map_y[y] + x;
	*ptr = *addr++;
    }
#if defined(ZXS)
    BYTE(0x5880 + x) = 6;
#endif
}

static void draw_twinkle(void) {
    static const byte mask[] = {
	0x80, 0x40, 0x20, 0x10,
	0x08, 0x04, 0x02, 0x01
    };
    if (*twinkle_ptr) {
	word pos = twinkle_offset - scroll;
	byte offset = (pos >> 3) & level_mask;
	if (offset < 0x20) {
	    byte index = (ticker & 4) == 0;
	    byte *ptr = twinkle_ptr[index] + offset;
	    twinkle_mask = mask[pos & 7];
	    if ((*ptr & twinkle_mask) || twinkle_box(offset)) {
		put_bonus(PLAYER, 21 + twinkle_num);
		*twinkle_ptr = NULL;
		twinkle_sound();
	    }
	    else {
		*ptr ^= twinkle_mask;
		twinkle_ptr[2] = ptr;
	    }
	}
    }
}

static void animate_wave(void) {
    frame = waver + (ticker & 16 ? PLAYER : 0);
}

static byte on_bridge(void) {
    return pos == STANDING;
}

static void wave_before_start(void) {
    while (on_bridge() && !SPACE_DOWN()) {
	animate_wave();
	draw_player();
	draw_twinkle();
	wait_vblank();
	clear_twinkle();
	clear_player();
    }
}

static void vblank_delay(word ticks) {
    for (word i = 0; i < ticks; i++) { if (vblank) break; }
}

static void sound_fx(word period, byte border) {
    vblank = 0;

#if defined(CPC)
    silence_music();
    cpc_psg(0x7, 0x3E);
    cpc_psg(0x8, 0x0F);
    period = period << 1;
    cpc_psg(0, period & 0xff);
    cpc_psg(1, period >> 8);
#endif

    while (!vblank) {
#if defined(ZXS)
	out_fe(border | 0x10);
	vblank_delay(period);
	out_fe(0x0);
	vblank_delay(period);
#elif defined(CPC)
	set_border(border);
	vblank_delay(period);
	set_border(0x54);
	vblank_delay(period);
#endif
    }

#if defined(CPC)
    cpc_psg(0x7, 0x3F);
    resume_music();
#endif
}

static void drown_player(void) {
    word period = 20;
    frame = drowner;
    erase_player(8, pos);
    while (frame < drowner + sizeof(drowner)) {
	clear_twinkle();
	draw_twinkle();
	draw_player();
	sound_fx(period, 0);
	clear_player();
	period += 10;
	if ((ticker & 3) == 0) {
	    period -= 30;
	    frame += PLAYER;
	}
    }
}

static word fade_period;
static void fade_sound(byte ticks) {
    if (!fade_period) {
	delay(ticks);
    }
    else {
	for (byte i = 0; i < ticks; i++) {
#if defined(ZXS)
	    static const byte border[] = { 0x01, 0x05, 0x07 };
#elif defined(CPC)
	    static const byte border[] = { 0x52, 0x5D, 0x4B };
#endif
	    sound_fx(fade_period >> i, border[i]);
	}
	fade_period -= 50;
    }
}

static byte total_waves(void) {
    byte total = 0;
    for (byte i = 0; i < WAVE_TYPES; i++) {
	total += current_level[i];
    }
    return total;
}

static void prepare_level(byte data) {
    byte total = total_waves();
    const byte *ptr = current_level + WAVE_TYPES;
    for (byte n = 0; n < total; n++) {
	byte offset = ptr[2];
	offset = offset & level_mask;
	if (offset < WIDTH) {
	    byte length = ptr[3];
	    byte *addr = * (byte **) ptr + offset;
	    if (offset + length >= WIDTH) {
		length = WIDTH - offset;
	    }
	    memset(addr, data, length);
	}
	ptr += 4;
    }
    fade_sound(3);
}

#if defined(ZXS)
static const byte fade_in[] =  {
    0x18, 0x3c, 0x7e, 0xff,
};
static const byte fade_out[] = {
    0x7e, 0x3c, 0x18, 0x00,
};
#elif defined(CPC)
static const byte fade_in[] =  {
    0x03, 0x0f, 0xf0, 0xff,
};
static const byte fade_out[] = {
    0xf0, 0x0f, 0x03, 0x00,
};
#endif

static void fade_level(const byte *ptr) {
    while (*ptr != 0x00 && *ptr != 0xff) {
	prepare_level(*ptr++);
    }
    prepare_level(*ptr);
}

static byte *current_data;
static byte **current_addr;
static const byte *level_ptr;

#define UPDATE_WAVE(addr, data) { \
    *current_addr++ = addr; \
    *current_data++ = data; }

static byte two_byte;
static void scroller(byte count, byte offset, byte data) {
#if defined(CPC)
    if (two_byte) offset++;
#endif

    while (count-- > 0) {
	byte distance = level_ptr[2] - offset;
	distance = (distance - 1) & level_mask;

	if (distance < WIDTH) {
	    byte *addr = * (byte **) level_ptr;
	    addr = addr + distance;
	    UPDATE_WAVE(addr, data);
#if defined(CPC)
	    if (two_byte) UPDATE_WAVE(++addr, data);
#endif
	}

	level_ptr += 4;
    }
}

#if defined(ZXS)
static const byte scroll_table[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xf0, 0x00, 0xf0, 0x00, 0xf0, 0x00, 0xf0, 0x00,
    0x0f, 0xff, 0x0f, 0xff, 0x0f, 0xff, 0x0f, 0xff,
    0xfc, 0xf0, 0xc0, 0x00, 0xfc, 0xf0, 0xc0, 0x00,
    0x03, 0x0f, 0x3f, 0xff, 0x03, 0x0f, 0x3f, 0xff,
    0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80, 0x00,
    0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff,
};
#elif defined(CPC)
static const byte scroll_table[] = {
    0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff,
    0xcc, 0x00, 0xcc, 0x00,
    0x33, 0xff, 0x33, 0xff,
    0xee, 0xcc, 0x88, 0x00,
    0x11, 0x33, 0x77, 0xff,
};
#endif

static byte scroll_data(byte i) {
#if defined(ZXS)
    return scroll_table[(i << 3) + (scroll & 7)];
#elif defined(CPC)
    return scroll_table[(i << 2) + ((scroll & 6) >> 1)];
#endif
}

static void draw_and_clear_bridge(void) {
    byte offset = (scroll >> 3) & (WIDTH - 1);
    word start = level_length - (256 << BPP_SHIFT);

    if (scroll >= start) {
	byte data = scroll_data(7);
	for (byte i = 0; i <= 2; i++) {
	    byte *addr = map_y[BRIDGE_TOP + i] + WIDTH - 1;
	    UPDATE_WAVE(addr - offset, data & bridge[i]);
	}
    }

    if (scroll < BRIDGE_LEN || scroll >= BRIDGE_LEN + start) {
	byte data = scroll_data(6);
	byte from = (scroll < BRIDGE_LEN ? 8 << BPP_SHIFT : 40 << BPP_SHIFT);
	for (byte i = BRIDGE_TOP; i <= BRIDGE_TOP + 2; i++) {
	    byte *addr = map_y[i] + from - offset + BPP_SHIFT;
	    UPDATE_WAVE(addr, data & *addr);
	}
    }
}

static void move_level(void) {
    word offset = scroll;
    current_data = wave_data;
    current_addr = wave_addr;
    level_ptr = current_level + WAVE_TYPES;
    for (byte i = 0; i < WAVE_TYPES; i++) {
#if defined(CPC)
	two_byte = i < 2;
#endif
	scroller(current_level[i], offset, scroll_data(i));
	if (i & 1) offset >>= 1;
    }
    draw_and_clear_bridge();
    scroll += 1 + BPP_SHIFT;
    *current_addr = 0;
}

static byte level_done(void) {
    return scroll >= level_length;
}

static void level_message(const char *msg) {
    byte top = bonus_run() ? 24 : 32;
    for (byte y = top; y < top + 8; y++) {
	memset(map_y[y] + (0x13 << BPP_SHIFT), 0, 10 << BPP_SHIFT);
    }
    put_str(msg, 152 + str_offset(msg, 40), top);
}

static void twinkle_init_ptr(byte y) {
    twinkle_height = y;
    for (byte i = 0; i < 2; i++) {
	twinkle_ptr[i] = map_y[y + i];
    }
}

static void search_twinkle_map(const struct Level *ptr) {
    const struct Twinkle *map = twinkle_map;

    twinkle_num = 0;
    while (map->level) {
	if (ptr->level == map->level) {
	    twinkle_init_ptr(map->height);
	    twinkle_offset = map->offset;
	    break;
	}
	twinkle_num++;
	map++;
    }
}

static void select_twinkle(const struct Level *ptr) {
    memset(twinkle_ptr, 0, sizeof(twinkle_ptr));
    if (bonus_run()) search_twinkle_map(ptr);
}

static void select_level(byte i) {
    const struct Level *ptr = level_list + i;
    current_level = ptr->level;
    level_length = ptr->length << BPP_SHIFT;
    level_mask = ptr->mask << BPP_SHIFT;
    level_mask = level_mask | 1;
    level_message(ptr->msg);
    select_twinkle(ptr);
}

static void end_game(const char *msg, byte y) {
    clear_screen();
    center_msg(msg, y);
#if defined(ZXS)
    memset((void *) 0x5900, 1, 0x200);
#endif
}

static const char * const outro[] = {
    " As you skip over the last patch of moonlight,",
    "the crowd cheers especially loud. After all,",
    "no one has completed this challenge in the past",
    "three years. A big celebration is in order.",
    NULL,
};

static const char * const p_done[] = {
    " As you skip over the last patch of moonlight,",
    "you feel a surge of confidence to participate",
    "in next year's challenge.",
    NULL,
};

static void show_outro_text(void) {
    byte y = 88;
    const char * const *text;
    text = practice_run() ? p_done : outro;
    while (*text) {
	put_str(*text++, 4, y);
	y = y + 8;
    }
}

static const char *done_message(void) {
    switch (*run_num) {
    case 0:
	return "PRACTICE";
    case 1:
	return "GAME";
    default:
	return "CHALLENGE";
    }
}

static void game_done(void) {
    char *ptr = tmp;
    ptr = strcpy(ptr, done_message());
    ptr = strcpy(ptr, " COMPLETE");

    end_game(tmp, 72);

    show_outro_text();
    if (!practice_run()) {
	display_image(&reward, 22, 16);
    }
    if (hard_run()) {
	display_image(&deed, 2, 16);
    }
    if (bonus_run()) {
	center_msg("THE END", 152) ;
    }
    display_image(&title, 0, 1);

    while (!SPACE_DOWN()) {
	wait_vblank();
	animate_water();
    }
    advance_run();
    reset();
}

static void outro_dimming(void) {
#if defined(ZXS)
    byte *ptr = (byte *) 0x5920 - 3;
    memset((byte *) 0x5a20, 7, 64);
    memset((byte *) 0x5a60, 1, 32);
    for (byte y = 8; y < 24; y++) {
	memset(ptr, 0, 3);
	ptr += 0x20;
    }
    wait_vblank();
#endif
}

static void draw_boat(byte *buf, byte x) {
    put_sprite(buf, x, 145, 3, 8);
}

#define SPLASH_WIDTH ((3 << BPP_SHIFT) + 1)
static void splashing(byte *buf, byte dir, byte rev) {
    byte y = 64;
    for (byte i = 0; i < (8 >> BPP_SHIFT); i++) {
	byte *addr = ((byte **) buf)[i];
	addr = addr + sizeof(boat) + 8 - SPLASH_WIDTH;
	for (byte x = 0; x < SPLASH_WIDTH; x++) {
	    byte value = *addr;
	    byte n = rev ? 8 - i : i;
	    for (byte j = 0; j < n; j++) {
		value = dir ? rlc(value) : rrc(value);
	    }
	    *addr++ = value;
	}
    }
}

static void boat_arrives(byte *buf) {
    byte x = 232;
    splashing(buf, 1, 0);
    while (x > 88) {
	draw_boat(buf, x);
	wait_vblank();
	wait_vblank();
	draw_boat(buf, x);
	x--;
    }
    draw_boat(buf, x);
}

static void draw_jumper(byte *ptr, byte x, byte y) {
    put_sprite(ptr, x, y, 1, 8);
}

static void draw_in_boat(byte x) {
    put_sprite((void *) frame, x + 8, 142, 1, 6);
}

static void draw_with_boat(byte *buf, byte x) {
    draw_in_boat(x);
    draw_boat(buf, x);
}

static byte *wave_sprite[2];
static void animate_wave_sprite(void) {
    frame = wave_sprite[(ticker & 16) != 0];
}

static void boat_leaves(byte *buf) {
    byte x = 88;
    splashing(buf, 0, 0);
    splashing(buf, 0, 1);
    while (x < 232) {
	wait_vblank();
	wait_vblank();
	draw_with_boat(buf, x);
	animate_wave_sprite();
	x = x + 1;
	draw_with_boat(buf, x);
    }
    draw_boat(buf, x);
}

static byte *free;
static byte *generate_one(const byte *src, byte w, byte h) {
    byte *ptr = free;
    free = generate_sprite(src, ptr, w, h);
    return ptr;
}

static byte *generate_jumper(void) {
    return generate_one(runner + (48 << BPP_SHIFT), 1, 8);
}

static void jump_in_boat(byte *buf) {
    byte x = 64;
    byte y = 128;
    vel = VELOCITY;
    clear_player();
    while (y < 142) {
	draw_jumper(buf, x, y);
	wait_vblank();
	draw_jumper(buf, x, y);
	y = y + (vel >> 2);
	vel = vel + 1;
	x++;
    }
    animate_wave_sprite();
    draw_in_boat(88);
    delay(25);
}

static void animate_finish(void) {
    free = tmp;
    byte *jumper = generate_jumper();
    byte *laiva = generate_one(boat, 3, 8);
    wave_sprite[0] = generate_one(waver, 1, 8);
    wave_sprite[1] = generate_one(waver + PLAYER, 1, 8);
    outro_dimming();
    boat_arrives(laiva);
    jump_in_boat(jumper);
    boat_leaves(laiva);
}

static byte flip_bits(byte source) {
    byte result = 0;
    for (byte i = 0; i < 8; i++) {
	result = result << 1;
	result |= source & 1;
	source = source >> 1;
    }
#if defined(CPC)
    result = (result >> 4) | (result << 4);
#endif
    return result;
}

static void flip_runner(byte *buf) {
    for (byte i = 0; i < SIZE(runner); i += BPP_SHIFT + 1) {
#if defined(ZXS)
	*buf++ = flip_bits(runner[i]);
#elif defined(CPC)
	*buf++ = flip_bits(runner[i + 1]);
	*buf++ = flip_bits(runner[i + 0]);
#endif
    }
}

static void draw_away_runner(byte *buf, byte x) {
    put_sprite(buf, x, 128, 1, 8);
}

static void generate_runner(byte **frm) {
    flip_runner(tmp);
    byte *ptr = tmp;
    byte *buf = tmp;
    buf += sizeof(runner);
    for (byte i = 0; i < 8; i++) {
	frm[i] = buf;
	buf = generate_sprite(ptr, buf, 1, 8);
	ptr += PLAYER;
    }
}

static void player_run_away(void) {
    byte x = 64;
    byte *frm[8];
    generate_runner(frm);
    wait_vblank();
    clear_player();
    while (x > 0) {
	byte *ptr = frm[(ticker & 0xe) >> 1];
	draw_away_runner(ptr, x);
	wait_vblank();
	draw_away_runner(ptr, x);
	x = x - 1;
    }
}

static void animate_victory(void) {
    if (practice_run()) {
	player_run_away();
    }
    else {
	animate_finish();
    }
}

static void fade_empty_level(void) {
    select_level(0);
    fade_level(fade_in);
    if (!practice_run()) {
	for (byte i = 0; i < 2; i++) {
	    delay(20);
	    fade_period = 500;
	    fade_level(fade_out);
	    fade_level(fade_in);
	}
    }
}

static void change_level(void) {
    level++;
    if (level < SIZE(level_list)) {
	select_level(level);
    }
    else {
	stop_music();
	fade_empty_level();
	animate_victory();
	game_done();
    }
}

static void stop_player(void) {
    ticker = 0;
    clear_player();
    clear_twinkle();
    animate_wave();
    draw_player();
}

static void reset_player_sprite(void) {
    if (on_bridge()) frame = runner;
}

static void advance_level(void) {
    stop_player();
    draw_pond_waves();
    fade_period = 500;
    fade_level(fade_out);
    change_level();
}

static byte next_level(void) {
    byte done = level_done();
    byte next = done && on_bridge();
    if (next) {
	advance_level();
    }
    else if (done) {
	scroll = 0;
    }
    return next;
}

static void game_loop(void) {
    byte drown = 0;
    fade_period = 0;
    reset_variables();
    draw_bridge();

  restart:
    scroll = 0;
    *wave_addr = 0;
    fade_level(fade_in);
    erase_player(8, pos);
    wave_before_start();
    reset_player_sprite();
    draw_player();
    wait_vblank();
    space_up = 0;

    while (!drown && pos < 184) {
	/* draw */
	clear_twinkle();
	clear_player();
	animate_player();
	draw_pond_waves();
	drown = draw_player();
	draw_twinkle();

	/* calculate */
	move_level();
	if (next_level()) {
	    goto restart;
	}

	/* done */
	wait_vblank();
    }

    erase_player(8, pos);
    drown_player();
}

static void put_fatal_level_name(void) {
    char *ptr = tmp;
    ptr = strcpy(ptr, "Pitch black depths of ");
    ptr = strcpy(ptr, level_list[level].msg);
    ptr = strcpy(ptr, " consumes you.");
    center_msg(tmp, 112);
}

static void game_over(void) {
    end_game("GAME OVER", 92);
    put_fatal_level_name();
    while (!SPACE_DOWN()) { }
    reset();
}

static void lose_cleanup(void) {
    byte *addr = map_y[63];
#if defined(ZXS)
    addr[8] = addr[9];
#elif defined(CPC)
    memcpy(addr + 16, addr + 18, 2);
#endif
    for (byte y = 64; y < 192; y++) {
	memset(map_y[y], 0, WIDTH);
    }
    if (lives >= 0 && !practice_run()) {
	erase_player(21 + lives, 44);
    }
}

static void no_lives(void) {
    lives = 0;
    for (byte x = 0; x < 6; x++) {
	erase_player(26 - x, 44);
	if (bonus_run()) put_bonus(0, 21 + x);
	sound_fx((x + 8) << 4, 0);
	delay(3);
    }
}

static void top_level(void) {
    setup_moon_shade();
    display_image(&horizon, 0, 0);
    if (hard_run()) no_lives();
    select_music(&music_tune);
    select_level(level);

    while (lives >= 0) {
	game_loop();
	if (!practice_run()) {
	    lives--;
	}
	lose_cleanup();
    }

    stop_music();
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
