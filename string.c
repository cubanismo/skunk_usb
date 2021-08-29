#include "string.h"

void *memcpy(void *dest, const void *src, size_t n)
{
	unsigned char *d = dest;
	const unsigned char *s = src;
	size_t i;

	for (i = 0; i < n; i++) {
		d[i] = s[i];
	}

	return dest;
}

void *memset(void *s, int c, size_t n)
{
	unsigned char *dst = s;
	size_t i;

	for (i = 0; i < n; i++) {
		dst[i] = c;
	}

	return s;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
	const unsigned char *a = s1;
	const unsigned char *b = s2;
	int diff;
	size_t i;

	for (i = 0; i < n; i++) {
		diff = (int)a[i] - b[i];
		if (diff) return diff;
	}

	return 0;
}

int strcmp(const char *s1, const char *s2)
{
    int diff;
    size_t i = 0;

    while (1) {
        if (!s1[i] && !s2[i]) {
            return 0;
        }

        diff = (int)s1[i] - s2[i];
        if (diff) return diff;

        i++;
    }
}

char *strchr(const char *s, int c)
{
	size_t i = 0;

	do {
		if (s[i] == c) {
			return (char *)&s[i];
		}
	} while (s[i++]);

	return 0;
}

char *strstr(const char *haystack, const char *needle)
{
	unsigned char found;
	size_t i, j;

	for (i = 0; haystack[i]; i++) {
		found = 1;
		for (j = 0; needle[j]; j++) {
			if (!haystack[i + j]) {
				return 0;
			}

			if (haystack[i + j] != needle[j]) {
				found = 0;
				break;
			}
		}

		if (found) {
			return (char *)&haystack[i];
		}
	}

	return 0;
}
