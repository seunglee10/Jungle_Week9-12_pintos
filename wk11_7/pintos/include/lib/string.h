#ifndef __LIB_STRING_H
#define __LIB_STRING_H

#include <stddef.h>

/* 표준 함수. */
void *memcpy (void *, const void *, size_t);
void *memmove (void *, const void *, size_t);
char *strncat (char *, const char *, size_t);
int memcmp (const void *, const void *, size_t);
int strcmp (const char *, const char *);
void *memchr (const void *, int, size_t);
char *strchr (const char *, int);
size_t strcspn (const char *, const char *);
char *strpbrk (const char *, const char *);
char *strrchr (const char *, int);
size_t strspn (const char *, const char *);
char *strstr (const char *, const char *);
void *memset (void *, int, size_t);
size_t strlen (const char *);

/* 확장 함수. */
size_t strlcpy (char *, const char *, size_t);
size_t strlcat (char *, const char *, size_t);
char *strtok_r (char *, const char *, char **);
size_t strnlen (const char *, size_t);

/* 사용 실수를 막기 위한 장치. */
#define strcpy dont_use_strcpy_use_strlcpy
#define strncpy dont_use_strncpy_use_strlcpy
#define strcat dont_use_strcat_use_strlcat
#define strncat dont_use_strncat_use_strlcat
#define strtok dont_use_strtok_use_strtok_r

#endif /* lib/string.h */
