#define AY

typedef signed char int8;
typedef signed short int16;
typedef unsigned char byte;
typedef unsigned short word;

struct Image {
    const byte *pixel;
    word pixel_size;
    const byte *color;
    word color_size;
    byte w, h;
};

struct Level {
    const byte *level;
    const char *msg;
    word length;
    byte mask;
};

#include "data.h"

#define ADDR(obj)	((word) (obj))
#define BYTE(addr)	(* (volatile byte *) (addr))
#define WORD(addr)	(* (volatile word *) (addr))
#define SIZE(array)	(sizeof(array) / sizeof(*(array)))

static volatile byte vblank;
static volatile byte ticker;
static byte *map_y[192];

void reset(void);

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

static void interrupt(void) __naked {
    __asm__("di");
    __asm__("push af");
    __asm__("push hl");

#if defined(AY)
    __asm__("ld a, (_enable_AY)");
    __asm__("and a");
    __asm__("jp z, skip_AY");

    __asm__("push bc");
    __asm__("push de");
    __asm__("push ix");
    __asm__("push iy");
    __asm__("call _Player_Decode");
    __asm__("call _Player_CopyAY");
    __asm__("pop iy");
    __asm__("pop ix");
    __asm__("pop de");
    __asm__("pop bc");

    __asm__("skip_AY:");
#endif

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
#endif

static void setup_system(void) {
#if defined(AY)
    enable_AY = 0;
#endif

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
    byte *ptr = (byte *) 0x5b00;
    uncompress(ptr, img->pixel, img->pixel_size);

    byte bottom = (y + img->h) << 3;
    for (byte i = y << 3; i < bottom; i++) {
	memcpy(map_y[i] + x, ptr, img->w);
	ptr += img->w;
    }
    uncompress(ptr, img->color, img->color_size);
    byte *addr = (byte *) 0x5800 + (y << 5) + x;
    for (byte i = 0; i < img->h; i++) {
	memcpy(addr, ptr, img->w);
	ptr += img->w;
	addr += 0x20;
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
    word addr = 0x5900;
    for (byte i = 0; i < 16; i++) {
	if (offset < 0x20) {
	    BYTE(addr + offset) = color;
	}
	addr += 32;
	offset++;
    }
}

static byte rotate(byte a) {
    __asm__("rlc a");
    return a;
}

static void shift_water_row(byte y) {
    byte *addr = map_y[y];
    for (int8 i = 0; i < 32; i++) {
	byte value = *addr;
	*addr++ = rotate(value);
    }
}

static void animate_water(void) {
    static const byte ripple[] = { 57, 61, 59, 61 };
    shift_water_row(ripple[ticker & 3]);
}

static void show_title(void) {
    display_image(&title, 0, 1);
    for (byte i = 0; i < SIZE(intro); i++) {
	put_str(intro[i], 20, 80 + (i << 3));
    }
    put_str("Press SPACE to participate", 52, 168);

    display_image(&credits, 0, 23);

    byte roll = 0;
    while (!SPACE_DOWN()) {
	wait_vblank();
	animate_water();
	lit_line(roll - 32, 0x01);
	lit_line(roll - 16, 0x41);
	roll = (roll + 1) & 0x3f;
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
#define BRIDGE_LEN	72
#define BRIDGE_TOP	136
#define WAVE_TYPES	8

static byte level_mask;
static word level_length;
static const byte *current_level;

static byte wave_data[MAX_WAVES];
static byte *wave_addr[MAX_WAVES];

static const struct Level level_list[] = {
    { level1, "Liezeris", 256, 0x1f },
    { level2, "Titikaka", 256, 0x1f },
    { level3, "Baikal",   256, 0x1f },
};

static void reset_variables(void) {
    vel = 0;
    jump = 0;
    pos = STANDING;
}

static void init_variables(void) {
    lives = 6;
    level = 0;
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
    memset((void *) 0x5900, 1, 0x200);
    shade_cone((byte *) 0x5902, 5, 14, 0);
    shade_cone((byte *) 0x5903, 7, 12, 1);
    for (byte i = 0; i <= 2; i++) {
	memset(map_y[BRIDGE_TOP + i], bridge[i], BRIDGE_LEN / 8);
    }
}

static void animate_wave(void) {
    frame = waver + (ticker & 16 ? 0 : 8);
}

static byte on_bridge(void) {
    return pos == STANDING;
}

static void wave_before_start(void) {
    while (on_bridge() && !SPACE_DOWN()) {
	animate_wave();
	draw_player();
	wait_vblank();
	clear_player();
    }
}

static void vblank_delay(word ticks) {
    for (word i = 0; i < ticks; i++) { if (vblank) break; }
}

static void sound_fx(word period, byte border) {
    vblank = 0;
    while (!vblank) {
	out_fe(border | 0x10);
	vblank_delay(period);
	out_fe(0x0);
	vblank_delay(period);
    }
}

static void drown_player(void) {
    word period = 20;
    frame = drowner;
    while (frame < drowner + sizeof(drowner)) {
	if ((ticker & 3) == 0) {
	    period -= 30;
	    frame += 8;
	}
	draw_player();
	sound_fx(period, 0);
	clear_player();
	period += 10;
    }
}

static word fade_period;
static void fade_sound(byte ticks) {
    if (!fade_period) {
	delay(ticks);
    }
    else {
	for (byte i = 0; i < ticks; i++) {
	    static const byte border[] = { 1, 5, 7 };
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
	if (offset < 0x20) {
	    byte length = ptr[3];
	    byte *addr = * (byte **) ptr + offset;
	    if (offset + length >= 0x20) {
		length = 0x20 - offset;
	    }
	    memset(addr, data, length);
	}
	ptr += 4;
    }
    fade_sound(3);
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
    }
    prepare_level(*ptr);
}

static byte *current_data;
static byte **current_addr;
static const byte *level_ptr;

#define UPDATE_WAVE(addr, data) \
    *current_addr++ = addr; \
    *current_data++ = data;

static void scroller(byte count, byte offset, byte data) {
    while (count-- > 0) {
	byte distance = level_ptr[2] - offset;
	distance = (distance - 1) & level_mask;

	if (distance < 0x20) {
	    byte *addr = * (byte **) level_ptr;
	    UPDATE_WAVE(addr + distance, data);
	}

	level_ptr += 4;
    }
}

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

static byte scroll_data(byte i) {
    return scroll_table[(i << 3) + (scroll & 7)];
}

static void draw_and_clear_bridge(void) {
    byte offset = scroll >> 3;

    if (scroll >= level_length) {
	byte data = scroll_data(7);
	for (byte i = 0; i <= 2; i++) {
	    byte *addr = map_y[BRIDGE_TOP + i] + 31 - (offset & 0x1f);
	    UPDATE_WAVE(addr, data & bridge[i]);
	}
    }

    if (scroll < BRIDGE_LEN || scroll >= BRIDGE_LEN + level_length) {
	byte data = scroll_data(6);
	byte from = scroll < BRIDGE_LEN ? 8 : 40;
	for (byte i = BRIDGE_TOP; i <= BRIDGE_TOP + 2; i++) {
	    byte *addr = map_y[i] + from - (offset & 0x1f);
	    UPDATE_WAVE(addr, data & *addr);
	}
    }
}

static void move_level(void) {
    byte offset = scroll;
    current_data = wave_data;
    current_addr = wave_addr;
    level_ptr = current_level + WAVE_TYPES;
    for (byte i = 0; i < WAVE_TYPES; i++) {
	scroller(current_level[i], offset, scroll_data(i));
	if (i & 1) offset >>= 1;
    }
    draw_and_clear_bridge();
    *current_addr = 0;
    scroll++;
}

static byte level_done(void) {
    return scroll > level_length + 256;
}

static void level_message(const char *msg) {
    memset((void *) 0x5893, 1, 10);
    for (byte y = 32; y < 40; y++) {
	memset(map_y[y] + 0x13, 0, 10);
    }
    put_str(msg, 152 + str_offset(msg, 40), 32);
}

static void select_level(byte i) {
    const struct Level *ptr = level_list + i;
    current_level = ptr->level;
    level_length = ptr->length;
    level_mask = ptr->mask;
    level_message(ptr->msg);
}

static void end_game(const char *msg, byte y) {
    clear_screen();
    put_str(msg, str_offset(msg, 128), y);
    memset((void *) 0x5900, 1, 0x100);
}

static const char * const outro[] = {
    " As you skip over the last patch of moonlight,",
    "the crowd cheers especially loud. After all,",
    "no one has completed this challenge in the past",
    "three years. A big celebration is in order.",
};

static void game_done(void) {
    end_game("CHALLENGE COMPLETED", 64);
    display_image(&title, 0, 1);
    display_image(&reward, 22, 16);

    for (byte i = 0; i < SIZE(outro); i++) {
	put_str(outro[i], 4, 85 + (i << 3));
    }

    while (!SPACE_DOWN()) {
	wait_vblank();
	animate_water();
    }
    reset();
}

static void change_level(void) {
    level++;
    if (level < SIZE(level_list)) {
	select_level(level);
    }
    else {
	game_done();
    }
}

static void stop_player(void) {
    clear_player();
    animate_wave();
    draw_player();
}

static void reset_player_sprite(void) {
    if (on_bridge()) frame = runner;
}

static void advance_level(void) {
    if (on_bridge()) {
	stop_player();
    }
    fade_period = 500;
    fade_level(fade_out);
    change_level();
}

static void game_loop(void) {
    byte drown = 0;
    fade_period = 0;
    reset_variables();
    setup_moon_shade();

  restart:
    scroll = 0;
    *wave_addr = 0;
    fade_level(fade_in);
    erase_player(8, pos);
    wave_before_start();
    reset_player_sprite();
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
	    advance_level();
	    goto restart;
	}

	/* done */
	wait_vblank();
    }

    erase_player(8, pos);
    drown_player();
}

static void game_over(void) {
    end_game("GAME OVER", 92);
    while (!SPACE_DOWN()) { }
    reset();
}

static void lose_cleanup(void) {
    map_y[63][8] = map_y[63][9];
    memset((void *) 0x4800, 0, 0x1000);
    if (lives >= 0) erase_player(21 + lives, 44);
}

static void top_level(void) {
    display_image(&horizon, 0, 0);
    select_level(level);

    while (lives-- >= 0) {
	game_loop();
	lose_cleanup();
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
