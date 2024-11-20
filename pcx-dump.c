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

static int win(int value) {
    return value < 63 ? value : 63;
}

static int compress(unsigned char *dst, unsigned char *src, int size) {
    unsigned char buf[63];
    int count = 0;
    int diff = 0;
    int pos = 0;

    void flush(void) {
	*dst = 0x40 | diff;
	memcpy(dst + 1, buf, diff);
	count += diff + 1;
	dst += diff + 1;
	diff = 0;
    }

    void encode(unsigned char tag, int amount, int data) {
	if (diff > 0) flush();
	*(dst++) = tag | amount;
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
	else if (diff == 0 && *src < 64) {
	    *(dst++) = *(src++);
	    count++;
	    pos++;
	}
	else {
	    if (diff == 63) flush();
	    buf[diff++] = *(src++);
	    pos++;
	}
    }

    if (diff > 0) flush();

    return count;
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
	printf("  -t   update tileset zx\n");
	return 0;
    }

    header.name = argv[2];

    unsigned char *buf = read_pcx(header.name);
    if (buf == NULL) return -ENOENT;

    free(buf);
    return 0;
}
