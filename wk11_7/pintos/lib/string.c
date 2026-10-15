#include <string.h>
#include <debug.h>

void *
memcpy(void *dst_, const void *src_, size_t size)
{
	unsigned char *dst = dst_;
	const unsigned char *src = src_;

	ASSERT(dst != NULL || size == 0);
	ASSERT(src != NULL || size == 0);

	while (size-- > 0)
		*dst++ = *src++;

	return dst_;
}

void *
memmove(void *dst_, const void *src_, size_t size)
{
	unsigned char *dst = dst_;
	const unsigned char *src = src_;

	ASSERT(dst != NULL || size == 0);
	ASSERT(src != NULL || size == 0);

	if (dst < src)
	{
		while (size-- > 0)
			*dst++ = *src++;
	}
	else
	{
		dst += size;
		src += size;
		while (size-- > 0)
			*--dst = *--src;
	}

	return dst;
}

int memcmp(const void *a_, const void *b_, size_t size)
{
	const unsigned char *a = a_;
	const unsigned char *b = b_;

	ASSERT(a != NULL || size == 0);
	ASSERT(b != NULL || size == 0);

	for (; size-- > 0; a++, b++)
		if (*a != *b)
			return *a > *b ? +1 : -1;
	return 0;
}

int strcmp(const char *a_, const char *b_)
{
	const unsigned char *a = (const unsigned char *)a_;
	const unsigned char *b = (const unsigned char *)b_;

	ASSERT(a != NULL);
	ASSERT(b != NULL);

	while (*a != '\0' && *a == *b)
	{
		a++;
		b++;
	}

	return *a < *b ? -1 : *a > *b;
}

void *
memchr(const void *block_, int ch_, size_t size)
{
	const unsigned char *block = block_;
	unsigned char ch = ch_;

	ASSERT(block != NULL || size == 0);

	for (; size-- > 0; block++)
		if (*block == ch)
			return (void *)block;

	return NULL;
}

char *
strchr(const char *string, int c_)
{
	char c = c_;

	ASSERT(string);

	for (;;)
		if (*string == c)
			return (char *)string;
		else if (*string == '\0')
			return NULL;
		else
			string++;
}

size_t
strcspn(const char *string, const char *stop)
{
	size_t length;

	for (length = 0; string[length] != '\0'; length++)
		if (strchr(stop, string[length]) != NULL)
			break;
	return length;
}

char *
strpbrk(const char *string, const char *stop)
{
	for (; *string != '\0'; string++)
		if (strchr(stop, *string) != NULL)
			return (char *)string;
	return NULL;
}

char *
strrchr(const char *string, int c_)
{
	char c = c_;
	const char *p = NULL;

	for (; *string != '\0'; string++)
		if (*string == c)
			p = string;
	return (char *)p;
}

size_t
strspn(const char *string, const char *skip)
{
	size_t length;

	for (length = 0; string[length] != '\0'; length++)
		if (strchr(skip, string[length]) == NULL)
			break;
	return length;
}

char *
strstr(const char *haystack, const char *needle)
{
	size_t haystack_len = strlen(haystack);
	size_t needle_len = strlen(needle);

	if (haystack_len >= needle_len)
	{
		size_t i;

		for (i = 0; i <= haystack_len - needle_len; i++)
			if (!memcmp(haystack + i, needle, needle_len))
				return (char *)haystack + i;
	}

	return NULL;
}

char *
strtok_r(char *s, const char *delimiters, char **save_ptr)
{
	char *token;

	ASSERT(delimiters != NULL);
	ASSERT(save_ptr != NULL);

	if (s == NULL)
		s = *save_ptr;
	ASSERT(s != NULL);

	while (strchr(delimiters, *s) != NULL)
	{
		if (*s == '\0')
		{
			*save_ptr = s;
			return NULL;
		}

		s++;
	}

	token = s;
	while (strchr(delimiters, *s) == NULL)
		s++;
	if (*s != '\0')
	{
		*s = '\0';
		*save_ptr = s + 1;
	}
	else
		*save_ptr = s;
	return token;
}

void *
memset(void *dst_, int value, size_t size)
{
	unsigned char *dst = dst_;

	ASSERT(dst != NULL || size == 0);

	while (size-- > 0)
		*dst++ = value;

	return dst_;
}

size_t
strlen(const char *string)
{
	const char *p;

	ASSERT(string);

	for (p = string; *p != '\0'; p++)
		continue;
	return p - string;
}

size_t
strnlen(const char *string, size_t maxlen)
{
	size_t length;

	for (length = 0; string[length] != '\0' && length < maxlen; length++)
		continue;
	return length;
}

size_t
strlcpy(char *dst, const char *src, size_t size)
{
	size_t src_len;

	ASSERT(dst != NULL);
	ASSERT(src != NULL);

	src_len = strlen(src);
	if (size > 0)
	{
		size_t dst_len = size - 1;
		if (src_len < dst_len)
			dst_len = src_len;
		memcpy(dst, src, dst_len);
		dst[dst_len] = '\0';
	}
	return src_len;
}

size_t
strlcat(char *dst, const char *src, size_t size)
{
	size_t src_len, dst_len;

	ASSERT(dst != NULL);
	ASSERT(src != NULL);

	src_len = strlen(src);
	dst_len = strlen(dst);
	if (size > 0 && dst_len < size)
	{
		size_t copy_cnt = size - dst_len - 1;
		if (src_len < copy_cnt)
			copy_cnt = src_len;
		memcpy(dst + dst_len, src, copy_cnt);
		dst[dst_len + copy_cnt] = '\0';
	}
	return src_len + dst_len;
}
