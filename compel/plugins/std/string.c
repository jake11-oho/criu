#include <sys/types.h>
#include <stdbool.h>
#include <stdarg.h>

#include <compel/plugins/std/syscall.h>
#include <compel/plugins/std/string.h>

#include "features.h"

static const char conv_tab[] = "0123456789abcdefghijklmnopqrstuvwxyz";

void std_dputc(int fd, char c)
{
	sys_write(fd, &c, 1);
}

void std_dputs(int fd, const char *s)
{
	for (; *s; s++)
		std_dputc(fd, *s);
}

static size_t __std_vprint_long_hex(char *buf, size_t blen, unsigned long num, char **ps)
{
	char *s = &buf[blen - 2];

	buf[blen - 1] = '\0';

	if (num == 0) {
		*s = '0', s--;
		goto done;
	}

	while (num > 0) {
		*s = conv_tab[num % 16], s--;
		num /= 16;
	}

done:
	s++;
	*ps = s;
	return blen - (s - buf);
}

static size_t __std_vprint_long(char *buf, size_t blen, long num, char **ps)
{
	char *s = &buf[blen - 2];
	int neg = 0;

	buf[blen - 1] = '\0';

	if (num < 0) {
		neg = 1;
		num = -num;
	} else if (num == 0) {
		*s = '0';
		s--;
		goto done;
	}

	while (num > 0) {
		*s = (num % 10) + '0';
		s--;
		num /= 10;
	}

	if (neg) {
		*s = '-';
		s--;
	}
done:
	s++;
	*ps = s;
	return blen - (s - buf);
}

static size_t __std_vprint_unsigned_long(char *buf, size_t blen, unsigned long num, char **ps)
{
	char *s = &buf[blen - 2];

	buf[blen - 1] = '\0';

	if (num == 0) {
		*s = '0';
		s--;
	}

	while (num > 0) {
		*s = (num % 10) + '0';
		s--;
		num /= 10;
	}

	s++;
	*ps = s;
	return blen - (s - buf);
}

void std_vdprintf(int fd, const char *format, va_list args)
{
	const char *s = format;

	for (; *s != '\0'; s++) {
		char buf[32], *t;
		int along = 0;

		if (*s != '%') {
			std_dputc(fd, *s);
			continue;
		}

		s++;
		if (*s == 'l') {
			along = 1;
			s++;
			if (*s == 'l')
				s++;
		}

		switch (*s) {
		case 's':
			std_dputs(fd, va_arg(args, char *));
			break;
		case 'd':
			__std_vprint_long(buf, sizeof(buf), along ? va_arg(args, long) : (long)va_arg(args, int), &t);
			std_dputs(fd, t);
			break;
		case 'x':
			__std_vprint_long_hex(buf, sizeof(buf), along ? va_arg(args, long) : (long)va_arg(args, int),
					      &t);
			std_dputs(fd, t);
			break;
		case 'u':
			__std_vprint_unsigned_long(buf, sizeof(buf), along ? va_arg(args, unsigned long) : (unsigned long)va_arg(args, unsigned int), &t);
			std_dputs(fd, t);
			break;
		case 'p':
			std_dputs(fd, "0x");
			__std_vprint_long_hex(buf, sizeof(buf), (unsigned long)va_arg(args, void *), &t);
			std_dputs(fd, t);
			break;
		}
	}
}

void std_dprintf(int fd, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	std_vdprintf(fd, format, args);
	va_end(args);
}

static inline bool __isspace(unsigned char c)
{
	return c == ' ' || c == '\f' || c == '\n' || c == '\r' || c == '\t' || c == '\v';
}

static unsigned char __tolower(unsigned char c)
{
	return (c <= 'Z' && c >= 'A') ? c - 'A' + 'a' : c;
}

static inline bool __isalpha(unsigned char c)
{
	return ((c <= 'Z' && c >= 'A') || (c <= 'z' && c >= 'a'));
}

static inline bool __isdigit(unsigned char c)
{
	return (c <= '9' && c >= '0');
}

static inline bool __isalnum(unsigned char c)
{
	return (__isalpha(c) || __isdigit(c));
}

static unsigned int __conv_val(unsigned char c)
{
	if (__isdigit(c))
		return c - '0';
	else if (__isalpha(c))
		/**
		 * If we want the value of something which __isalpha() == true
		 * it has to be base > 10. 'A' = 10, 'B' = 11 ... 'Z' = 35
		 */
		return __tolower(c) - 'a' + 10;

	return -1u;
}

unsigned long std_strtoul(const char *nptr, char **endptr, int base)
{
	const char *s = nptr;
	bool neg = false;
	unsigned int v;
	long num = 0;

	if (base < 0 || base == 1 || base > 36)
		goto fin;

	while (__isspace(*s))
		s++;
	if (!*s)
		goto fin;

	if (*s == '-')
		neg = true, s++;

	if (base == 0) {
		if (s[0] == '0') {
			unsigned char p = __tolower(s[1]);
			switch (p) {
			case 'b':
				base = 2, s += 2;
				break;
			case 'x':
				base = 16, s += 2;
				break;
			default:
				base = 8, s += 1;
				break;
			}
		} else
			base = 10;
	} else if (base == 16) {
		if (s[0] == '0' && __tolower(s[1]) == 'x')
			s += 2;
	}

	for (; *s; s++) {
		if (__isspace(*s))
			continue;
		if (!__isalnum(*s))
			goto fin;
		v = __conv_val(*s);
		if (v == -1u || v > base)
			goto fin;
		num *= base;
		num += v;
	}

fin:
	if (endptr)
		*endptr = (char *)s;
	return neg ? (unsigned long)-num : (unsigned long)num;
}

/*
 * C compiler is free to insert implicit calls to memcmp, memset,
 * memcpy and memmove, assuming they are available during linking.
 * As the parasite code is not linked with libc, it must provide
 * our own implementations of the above functions.
 * Surely, these functions can also be called explicitly.
 *
 * Note: for now, not having memmove() seems OK for both gcc and clang.
 */

#ifndef ARCH_HAS_MEMCPY
void *memcpy(void *to, const void *from, size_t n)
{
	size_t i;
	unsigned char *cto = to;
	const unsigned char *cfrom = from;

	for (i = 0; i < n; ++i, ++cto, ++cfrom)
		*cto = *cfrom;

	return to;
}
#endif

#ifndef ARCH_HAS_MEMCMP
int memcmp(const void *cs, const void *ct, size_t count)
{
	const unsigned char *su1, *su2;
	int res = 0;

	for (su1 = cs, su2 = ct; 0 < count; ++su1, ++su2, count--)
		if ((res = *su1 - *su2) != 0)
			break;
	return res;
}
#endif

#ifndef ARCH_HAS_MEMSET
void *memset(void *s, const int c, size_t count)
{
	volatile char *dest = s;
	size_t i = 0;

	while (i < count)
		dest[i++] = (char)c;

	return s;
}
#endif

int std_strcmp(const char *cs, const char *ct)
{
	unsigned char c1, c2;

	while (1) {
		c1 = *cs++;
		c2 = *ct++;
		if (c1 != c2)
			return c1 < c2 ? -1 : 1;
		if (!c1)
			break;
	}
	return 0;
}

int std_strncmp(const char *cs, const char *ct, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++) {
		if (cs[i] != ct[i])
			return cs[i] < ct[i] ? -1 : 1;
		if (!cs[i])
			break;
	}
	return 0;
}

static int vsnprintf(char *buf, size_t size, const char *format, va_list args)
{
	const char *s = format;
	size_t i = 0;

	for (; *s != '\0'; s++) {
		char tbuf[32], *t;
		int along = 0;
		size_t len = 0;

		if (*s != '%') {
			if (i < size)
				buf[i] = *s;
			i++;
			continue;
		}

		s++;
		if (*s == 'l') {
			along = 1;
			s++;
			if (*s == 'l')
				s++;
		}

		switch (*s) {
		case 's':
			for (t = va_arg(args, char *); *t; t++) {
				if (i < size)
					buf[i] = *t;
				i++;
			}
			break;
		case 'd':
			len = __std_vprint_long(tbuf, sizeof(tbuf), along ? va_arg(args, long) : (long)va_arg(args, int), &t);
			if (i < size)
				memcpy(&buf[i], t, ((i + len - 1) < size) ?  (len - 1) : (size - i));
			i += (len - 1);
			break;
		case 'x':
			len = __std_vprint_long_hex(tbuf, sizeof(tbuf), along ? va_arg(args, long) : (long)va_arg(args, int), &t);
			if (i < size)
				memcpy(&buf[i], t, ((i + len - 1) < size) ?  (len - 1) : (size - i));
			i += (len - 1);
			break;
		case 'u':
			len = __std_vprint_unsigned_long(tbuf, sizeof(tbuf), along ? va_arg(args, unsigned long) : (unsigned long)va_arg(args, unsigned int), &t);
			if (i < size)
				memcpy(&buf[i], t, ((i + len - 1) < size) ?  (len - 1) : (size - i));
			i += (len - 1);
			break;
		case 'p':
			if (i < size)
				buf[i] = '0';
			i++;
			if (i < size)
				buf[i] = 'x';
			i++;
			len = __std_vprint_long_hex(tbuf, sizeof(tbuf), (unsigned long)va_arg(args, void *), &t);
			if (i < size)
				memcpy(&buf[i], t, ((i + len - 1) < size) ?  (len - 1) : (size - i));
			i += (len - 1);
			break;
		}
	}

	if (i < size)
		buf[i] = '\0';
	else
		buf[size - 1] = '\0';

	return i;
}

int snprintf(char *buf, size_t size, const char *format, ...)
{
	int result;
	va_list args;

	va_start(args, format);
	result = vsnprintf(buf, size, format, args);
	va_end(args);
	return result;
}


int strcmp(const char *cs, const char *ct)
{
    return std_strcmp(cs, ct);
}

// int fprintf(FILE *stream, const char *format, ...)
// {
// 	va_list args;
// 	va_start(args, format);
// 	std_vdprintf(stream->fd, format, args);
// 	va_end(args);
// 	return 0;
// }

// __thread
int local_errno;

int *__errno_location(void)
{
    return &local_errno;
}
