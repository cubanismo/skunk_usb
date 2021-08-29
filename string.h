#ifndef STRING_H_
#define STRING_H_

typedef unsigned int size_t;

/* Minimal string.h containing functions needed by FatFS */
extern void *memcpy(void *dest, const void *src, size_t n);
extern void *memset(void *s, int c, size_t n);
extern int memcmp(const void *s1, const void *s2, size_t n);
extern char *strchr(const char *s, int c);
extern char *strstr(const char *haystack, const char *needle);
extern int strcmp(const char *s1, const char *s2);

#endif /* STRING_H_ */
