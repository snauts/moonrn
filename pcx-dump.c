#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>

struct Header {
    char *name;
    short w, h;
} header;

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
    return result;
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

static int back(unsigned char *src, int pos, int size, int *ret) {
    if (size > pos) size = pos;

    for (int i = size; i > 1; i--) {
	for (int x = 0; x <= size - i; x++) {
	    if (memcmp(src - pos + x, src, i) == 0) {
		*ret = pos - x;
		return i;
	    }
	}
    }

    return 0;
}

#define WINDOW 64

static int win(int value) {
    return value < WINDOW ? value : WINDOW;
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
	int n = back(src, win(pos), c, &b);

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

static unsigned char consume_pixels(unsigned char *buf, unsigned char on) {
    unsigned char ret = 0;
    for (int i = 0; i < 8; i++) {
	ret = ret << 1;
	ret |= (buf[i] == on) ? 1 : 0;
    }
    return ret;
}

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

static void convert_to_stripe(int w, int h, unsigned char *output) {
    int n = 0;
    int size = w * h / 8;
    unsigned char tmp[size];
    for (int y = 0; y < h; y += 8) {
	for (int x = 0; x < w / 8; x++) {
	    for (int i = 0; i < 8; i++) {
		tmp[n++] = output[((y + i) * w / 8) + x];
	    }
	}
    }
    memcpy(output, tmp, size);
}

static int match(void *pixel, int n, void *tiles, int i) {
    unsigned long *ptr1 = pixel + n;
    unsigned long *ptr2 = tiles + i;
    return *ptr1 == *ptr2;
}

static void save_tileset(unsigned char *pixel, int pixel_size,
			 unsigned char *color, int color_size) {

    int tiles_size = 0;
    unsigned char tiles[pixel_size];
    unsigned char index[pixel_size / 8];

    for (int n = 0; n < pixel_size; n += 8) {
	int have_match = -1;
	for (int i = 0; i < tiles_size; i += 8) {
	    if (match(pixel, n, tiles, i)) {
		have_match = i / 8;
		break;
	    }
	}
	if (have_match < 0) {
	    memcpy(tiles + tiles_size, pixel + n, 8);
	    have_match = tiles_size / 8;
	    tiles_size += 8;
	}
	index[n / 8] = have_match;
    }

    fprintf(stderr, "IMAGE:%s TILES:%d\n", header.name, tiles_size / 8);

    char name[256];
    remove_extension(header.name, name);

    compress_and_save(name, "index", index, pixel_size / 8);
    compress_and_save(name, "tiles", tiles, tiles_size);
    compress_and_save(name, "color", color, color_size);
}

static void save_bitmap(unsigned char *buf, int size) {
    int j = 0;
    int pixel_size = size / 8;
    int color_size = size / 64;
    unsigned char pixel[pixel_size];
    unsigned char color[color_size];
    unsigned short on[color_size];
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

    convert_to_stripe(header.w, header.h, pixel);

    save_tileset(pixel, pixel_size, color, color_size);
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
    if (argc < 2) {
	printf("USAGE: pcx-dump file.pcx\n");
	return 0;
    }

    header.name = argv[1];

    void *buf = read_pcx(header.name);
    if (buf == NULL) return -ENOENT;

    save_bitmap(buf, header.w * header.h);
    free(buf);
    return 0;
}
