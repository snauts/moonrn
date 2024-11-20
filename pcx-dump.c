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

static int unique(unsigned char *src, int size) {
    int count = 0;
    unsigned char byte = *(src++);

    while (size > 0 && byte != *src) {
	byte = *(src++);
	count++;
	size--;
    }

    return count;
}

static int compress(unsigned char *dst, unsigned char *src, int size) {
    unsigned char *start = src;
    int count = 0;
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
