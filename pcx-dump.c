#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>

static char option;

struct Header {
    char *name;
    short w, h;
} header;

int line_count = 0;
struct Line {
    int x, y, len, type;
} line[128];

static int is_file(const char *name) {
    return strcmp(header.name, name) == 0;
}

static unsigned short pixel_addr(int x, int y) {
#if defined(ZXS)
    int f = ((y & 7) << 3) | ((y >> 3) & 7) | (y & 0xc0);
    return 0x4000 + (f << 5) + (x >> 3);
#endif

#if defined(CPC)
    int f = ((y & 7) << 11) | (80 * (y >> 3));
    return 0xC000 + f + (x >> 2);
#endif
}

void hexdump(unsigned char *buf, int size) {
    for (int i = 0; i < size; i++) {
	fprintf(stderr, "%02x ", buf[i]);
	if ((i & 0xf) == 0xf) {
	    fprintf(stderr, "\n");
	}
    }
    if ((size & 0xf) != 0x0) fprintf(stderr, "\n");
}

static unsigned char get_color(unsigned char *color) {
    unsigned char result = 0;
    if (color[0] >= 0x80) result |= 0x02;
    if (color[1] >= 0x80) result |= 0x04;
    if (color[2] >= 0x80) result |= 0x01;
    for (int i = 0; i < 3; i++) {
	if (color[i] > (result ? 0xf0 : 0x40)) {
	    result |= 0x40;
	    break;
	}
    }

#if defined(ZXS)
    return result;
#endif

#if defined(CPC)
    if (option == 'l') {
	return result;
    }
    static const unsigned char default_map[] = {
	0, 2, 2, 0, 0, 0, 3, 0,
	0, 1, 2, 0, 0, 0, 3, 0,
    };
    static const unsigned char horizon_map[] = {
	0, 2, 0, 0, 1, 0, 1, 3,
	0, 2, 0, 0, 1, 0, 1, 3,
    };
    static const unsigned char player_map[] = {
	0, 2, 0, 0, 1, 0, 0, 3,
	0, 2, 0, 0, 1, 0, 0, 3,
    };
    const unsigned char *ptr;
    if (is_file("horizon.pcx") || is_file("boat.pcx")) {
	ptr = horizon_map;
    }
    else if (option == 'p') {
	ptr = player_map;
    }
    else {
	ptr = default_map;
    }
    return ptr[(result & 0x07) + ((result & 0x40) >> 3)];
#endif
}

static int equals(unsigned char *src, int size) {
    int count = 1;
    size = size - count;
    unsigned char byte = *(src++);

    while (size > 0 && byte == *src) {
	src++;
	count++;
	size--;
    }

    return count;
}

static int min(int a, int b) {
    return a < b ? a : b;
}

static int back(unsigned char *src, int pos, int size, int *ret) {
    pos = min(255, pos);
    if (size > pos) size = pos;

    for (int i = size; i > 1; i--) {
	for (int x = pos; x >= size; x--) {
	    if (memcmp(src - x, src, i) == 0) {
		*ret = x;
		return i;
	    }
	}
    }

    return 0;
}

#define WINDOW 64

static int win(int value) {
    return min(value, WINDOW);
}

static int compress(unsigned char *dst, unsigned char *src, int size) {
    unsigned char buf[WINDOW];
    int count = 0;
    int diff = 0;
    int pos = 0;

    void update(unsigned char tag, int amount) {
	*(dst++) = tag | (amount - 1);
    }

    void flush(void) {
	update(0x40, diff);
	memcpy(dst, buf, diff);
	count += diff + 1;
	dst += diff;
	diff = 0;
    }

    void encode(unsigned char tag, int amount, int data) {
	if (diff > 0) flush();
	update(tag, amount);
	*(dst++) = data;
	src += amount;
	pos += amount;
	count += 2;
    }

    while (pos < size) {
	int b = 0;
	int c = win(size);
	int e = equals(src, c);
	int n = back(src, pos, c, &b);

	if (e > 1 && e > n) {
	    encode(0x80, e, *src);
	}
	else if (n > 1) {
	    encode(0xc0, n, b);
	}
	else if (diff == 0 && *src < WINDOW) {
	    *(dst++) = *(src++);
	    count++;
	    pos++;
	}
	else {
	    if (diff == WINDOW) flush();
	    buf[diff++] = *(src++);
	    pos++;
	}
    }

    if (diff > 0) flush();

    return count;
}

static void remove_extension(char *src, char *dst) {
    for (int i = 0; i < strlen(src); i++) {
	if (src[i] == '.') {
	    dst[i] = 0;
	    return;
	}
	else if (src[i] == '/') {
	    dst[i] = '_';
	}
	else {
	    dst[i] = src[i];
	}
    }
}

static void dump_buffer(void *ptr, int size, int step) {
    for (int i = 0; i < size; i++) {
	if (step == 1) {
	    printf(" 0x%02x,", * (unsigned char *) ptr);
	}
	else {
	    printf(" 0x%04x,", * (unsigned short *) ptr);
	}
	if ((i & 7) == 7) printf("\n");
	ptr += step;
    }
    if ((size & 7) != 0) printf("\n");
}

static unsigned short encode_pixel(unsigned char a, unsigned char b) {
    return a > b ? (b << 8) | a : (a << 8) | b;
}

static unsigned char encode_ink(unsigned short colors) {
    unsigned char b = colors >> 8;
    unsigned char f = colors & 0xff;
    unsigned char l = (f > 7 || b > 7) ? 0x40 : 0x00;
    return l | (f & 7) | ((b & 7) << 3);
}

#if defined(ZXS)
#define PiB 8
static unsigned char consume_pixels(unsigned char *buf, unsigned char on) {
    unsigned char ret = 0;
    for (int i = 0; i < 8; i++) {
	ret = ret << 1;
	ret |= (buf[i] == on || (buf[i] != 0 && option == 'p')) ? 1 : 0;
    }
    return ret;
}
#endif

#if defined(CPC)
#define PiB 4
static unsigned char consume_pixels(unsigned char *buf) {
    unsigned char ret = 0;
    for (int i = 0; i < PiB; i++) {
	ret = ret << 1;
	ret |= ((buf[i] >> 1) & 1) | ((buf[i] << 4) & 0x10);
    }
    return ret;
}
#endif

static int ink_index(int i) {
    return (i / header.w / 8) * (header.w / 8) + i % header.w / 8;
}

static unsigned short on_pixel(unsigned char *buf, int i, int w) {
    unsigned char pixel = buf[i];
    for (int y = 0; y < 8; y++) {
	for (int x = 0; x < 8; x++) {
	    unsigned char next = buf[i + x];
	    if (next != pixel) {
		return encode_pixel(next, pixel);
	    }
	}
	i += w;
    }
    return pixel == 0 ? 0x1 : pixel;
}

static void compress_and_save(char *name, char *post, void *buf, int size) {
    unsigned char tmp[2 * size];
    int count = compress(tmp, buf, size);
    printf("static const byte %s_%s[] = {\n", name, post);
    dump_buffer(tmp, count, 1);
    printf("};\n");
}

static void save_image_entry(char *name, char *type) {
    printf(" .%s = %s_%s,\n", type, name, type);
    printf(" .%s_size = sizeof(%s_%s),\n", type, name, type);
}

static void save_scr(unsigned char *pixel, int pixel_size,
		     unsigned char *color, int color_size) {
    for (int y = 0; y < 192; y++) {
	for (int x = 0; x < 32; x++) {
	    int f = ((y & 7) << 3) | ((y >> 3) & 7) | (y & 0xc0);
	    putchar(pixel[(f << 5) + x]);
	}
    }
    for (int i = 0; i < color_size; i++) {
	putchar(color[i]);
    }
}

static void save_image(unsigned char *pixel, int pixel_size,
		       unsigned char *color, int color_size) {

    char name[256];
    remove_extension(header.name, name);

    compress_and_save(name, "pixel", pixel, pixel_size);
#if defined(ZXS)
    compress_and_save(name, "color", color, color_size);
#endif

    printf("static const struct Image %s = {\n", name);
    save_image_entry(name, "pixel");
#if defined(ZXS)
    save_image_entry(name, "color");
#endif
    printf(" .w = %d,", header.w / PiB);
    printf(" .h = %d,\n", header.h / 8);
    printf("};\n");
}

static void save_raw(unsigned char *pixel, int pixel_size, char *extra) {
    char name[256];
    remove_extension(header.name, name);
    printf("static const byte %s%s[] = {\n", name, extra);
    dump_buffer(pixel, pixel_size, 1);
    printf("};\n");
}

static void save_bitmap(unsigned char *buf, int size) {
    int j = 0;
    int pixel_size = size / PiB;
    int color_size = size / 64;
    unsigned char pixel[pixel_size];
    unsigned char color[color_size];
    unsigned short on[color_size];

#if defined(ZXS)
    for (int i = 0; i < size; i += 8) {
	if (i / header.w % 8 == 0) {
	    on[j++] = on_pixel(buf, i, header.w);
	}
	unsigned char data = on[ink_index(i)] & 0xff;
	pixel[i / 8] = consume_pixels(buf + i, data);
    }
    for (int i = 0; i < color_size; i++) {
	color[i] = encode_ink(on[i]);
    }
#endif

#if defined(CPC)
    for (int i = 0; i < size; i += PiB) {
	pixel[i / PiB] = consume_pixels(buf + i);
    }
#endif

    switch (option) {
    case 'c':
	save_image(pixel, pixel_size, color, color_size);
	break;
    case 's':
	save_scr(pixel, pixel_size, color, color_size);
	break;
    case 'p':
	save_raw(pixel, pixel_size, "");
	break;
    }
}

static void add_line(int x, int y, int type, int end) {
    struct Line *ptr = line + line_count;
    ptr->x = x;
    ptr->y = y;
    ptr->len = 0;
    ptr->type = 2 * (type - 1) + end;
    if (end) ptr[-1].len = ptr->x - ptr[-1].x;
    line_count++;
}

static int compare(const void *p1, const void *p2) {
    const struct Line *l1 = p1;
    const struct Line *l2 = p2;
    return l1->type < l2->type;
}

static void save_level(unsigned char *buf) {
    int start = -1;
    int offset = 0;
    for (int y = 0; y < header.h; y++) {
	for (int x = 0; x < header.w; x++) {
	    int type = buf[offset];
	    if (start == -1 && type != 0) {
		add_line(x, y, type, 0);
		start = x;
	    }
	    if (start != -1 && type == 0) {
		add_line(x, y, buf[offset - 1], 1);
		start = -1;
	    }
	    offset++;
	}
	if (start != -1) {
	    add_line(header.w, y, buf[offset - 1], 1);
	    line[line_count - 1].x = 0;
	    start = -1;
	}
    }

    qsort(line, line_count, sizeof(struct Line), compare);

    int n = 8;
    unsigned char level[4 * line_count + n];
    memset(level, 0, n);

    for (int i = 0; i < line_count; i++) {
	unsigned char type = line[i].type;
	unsigned short addr = pixel_addr(0, line[i].y + 64);
	level[n++] = addr & 0xff;
	level[n++] = addr >> 8;
	level[n++] = line[i].x / PiB;
	level[n++] = line[i].len / PiB;
	level[7 - type]++;
    }

    fprintf(stderr, "%s: %d\n", header.name, line_count);
    save_raw(level, sizeof(level), "");
}

static unsigned char *read_pcx(const char *file) {
    struct stat st;
    int palette_offset = 16;
    if (stat(file, &st) != 0) {
	fprintf(stderr, "ERROR while opening PCX-file \"%s\"\n", file);
	return NULL;
    }
    unsigned char *buf = malloc(st.st_size);
    int in = open(file, O_RDONLY);
    read(in, buf, st.st_size);
    close(in);

    header.w = (* (unsigned short *) (buf + 0x8)) + 1;
    header.h = (* (unsigned short *) (buf + 0xa)) + 1;
    if (buf[3] == 8) palette_offset = st.st_size - 768;
    int unpacked_size = header.w * header.h / (buf[3] == 8 ? 1 : 2);
    unsigned char *pixels = malloc(unpacked_size);

    int i = 128, j = 0;
    while (j < unpacked_size) {
	if ((buf[i] & 0xc0) == 0xc0) {
	    int count = buf[i++] & 0x3f;
	    while (count-- > 0) {
		pixels[j++] = buf[i];
	    }
	    i++;
	}
	else {
	    pixels[j++] = buf[i++];
	}
    }

    for (i = 0; i < unpacked_size; i++) {
	int entry = palette_offset + 3 * pixels[i];
	pixels[i] = get_color(buf + entry);
    }

    free(buf);
    return pixels;
}

int main(int argc, char **argv) {
    if (argc < 3) {
	printf("USAGE: pcx-dump [option] file.pcx\n");
	printf("  -c   save compressed image\n");
	printf("  -p   save raw pixel data\n");
	printf("  -l   save level data\n");
	printf("  -s   save .scr file\n");
	return 0;
    }

    option = argv[1][1];
    header.name = argv[2];

    void *buf = read_pcx(header.name);
    if (buf == NULL) return -ENOENT;

    if (option == 'l') {
	save_level(buf);
    }
    else {
	save_bitmap(buf, header.w * header.h);
    }
    free(buf);
    return 0;
}
