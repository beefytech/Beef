#pragma warning(disable:4333)

/*
 Basic UTF-8 manipulation routines
 by Jeff Bezanson
 placed in the public domain Fall 2005
 
 This code is designed to provide the utilities you need to manipulate
 UTF-8 as an internal string encoding. These functions do not perform the
 error checking normally needed when handling UTF-8 data, so if you happen
 to be from the Unicode Consortium you will want to flay me alive.
 I do this because error checking can be performed at the boundaries (I/O),
 with these routines reserved for higher performance on data known to be
 valid.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#ifdef WIN32
#include <malloc.h>
#else
//#include <alloca.h>
#endif

#include "util/UTF8.h"

USING_NS_BF;

static const uint32 offsetsFromUTF8[6] = {
    0x00000000UL, 0x00003080UL, 0x000E2080UL,
    0x03C82080UL, 0xFA082080UL, 0x82082080UL
};

static const char trailingBytesForUTF8[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5
};

/* returns length of next utf-8 sequence */
int Beefy::u8_seqlen(char *s)
{
    return trailingBytesForUTF8[(unsigned int)(unsigned char)s[0]] + 1;
}

int Beefy::u8_seqlen(uint32 ch)
{
	if (ch < 0x80) {
		return 1;
	}
	else if (ch < 0x800) {
		return 2;
	}
	else if (ch < 0x10000) {
		return 3;
	}
	else if (ch < 0x110000) {
		return 4;
	}
	return 5;
}

/* conversions without error checking
 only works for valid UTF-8, i.e. no 5- or 6-byte sequences
 srcsz = source size in bytes, or -1 if 0-terminated
 sz = dest size in # of wide characters
 
 returns # characters converted
 dest will always be L'\0'-terminated, even if there isn't enough room
 for all the characters.
 if sz = srcsz+1 (i.e. 4*srcsz+4 bytes), there will always be enough space.
 */
int Beefy::u8_toucs(wchar_t *dest, int sz, char *src, int srcsz)
{
    wchar_t ch;
    char *src_end = src + srcsz;
    int nb;
    int i=0;
    
    while (i < sz-1) {
        nb = trailingBytesForUTF8[(unsigned char)*src];
        if (srcsz == -1) {
            if (*src == 0)
                goto done_toucs;
        }
        else {
            if (src + nb >= src_end)
                goto done_toucs;
        }
        ch = 0;
        switch (nb) {
                /* these fall through deliberately */
            case 3: ch += (unsigned char)*src++; ch <<= 6;
            case 2: ch += (unsigned char)*src++; ch <<= 6;
            case 1: ch += (unsigned char)*src++; ch <<= 6;
            case 0: ch += (unsigned char)*src++;
        }
        ch -= offsetsFromUTF8[nb];
        dest[i++] = ch;
    }
done_toucs:
    dest[i] = 0;
    return i;
}

uint32 Beefy::u8_toucs(const char* src, int srcsz, int* outLen)
{
	const char *src_end = src + srcsz;
	int nb = trailingBytesForUTF8[(unsigned char)*src];	
	if (outLen != NULL)
		*outLen = nb + 1;
	if (srcsz == -1) {
		if (*src == 0)
			return 0;
	}
	else {
		if (src + nb >= src_end)
			return 0;
	}
	uint32 ch = 0;
	switch (nb) {
		/* these fall through deliberately */
	case 3: ch += (unsigned char)*src++; ch <<= 6;
	case 2: ch += (unsigned char)*src++; ch <<= 6;
	case 1: ch += (unsigned char)*src++; ch <<= 6;
	case 0: ch += (unsigned char)*src++;
	}
	ch -= offsetsFromUTF8[nb];
	return ch;
}

/* srcsz = number of source characters, or -1 if 0-terminated
 sz = size of dest buffer in bytes
 
 returns # characters converted
 dest will only be '\0'-terminated if there is enough space. this is
 for consistency; imagine there are 2 bytes of space left, but the next
 character requires 3 bytes. in this case we could NUL-terminate, but in
 general we can't when there's insufficient space. therefore this function
 only NUL-terminates if all the characters fit, and there's space for
 the NUL as well.
 the destination string will never be bigger than the source string.
 */
int Beefy::u8_toutf8(char *dest, int sz, wchar_t *src, int srcsz)
{
	wchar_t ch;
    int i = 0;
    char *dest_end = dest + sz;
    
    while (srcsz<0 ? src[i]!=0 : i < srcsz) {
        ch = src[i];
        if (ch < 0x80) {
            if (dest >= dest_end)
                return i;
            *dest++ = (char)ch;
        }
        else if (ch < 0x800) {
            if (dest >= dest_end-1)
                return i;
            *dest++ = (ch>>6) | 0xC0;
            *dest++ = (ch & 0x3F) | 0x80;
        }
        else if (ch < 0x10000) {
            if (dest >= dest_end-2)
                return i;
            *dest++ = (ch>>12) | 0xE0;
            *dest++ = ((ch>>6) & 0x3F) | 0x80;
            *dest++ = (ch & 0x3F) | 0x80;
        }
        else if (ch < 0x110000) {
            if (dest >= dest_end-3)
                return i;
            *dest++ = (ch>>18) | 0xF0;
            *dest++ = ((ch>>12) & 0x3F) | 0x80;
            *dest++ = ((ch>>6) & 0x3F) | 0x80;
            *dest++ = (ch & 0x3F) | 0x80;
        }
        i++;
    }
    if (dest < dest_end)
        *dest = '\0';
    return i;
}

int Beefy::u8_toutf8(char *dest, int sz, uint32 ch)
{
	char *dest_end = dest + sz;
	int len = 0;
	if (ch < 0x80) {
		if (dest >= dest_end)
			return 1;
		len = 1;
		*dest++ = (char)ch;
	}
	else if (ch < 0x800) {
		if (dest >= dest_end - 1)
			return 2;
		len = 2;
		*dest++ = (ch >> 6) | 0xC0;
		*dest++ = (ch & 0x3F) | 0x80;
	}
	else if (ch < 0x10000) {
		if (dest >= dest_end - 2)
			return 3;
		len = 3;
		*dest++ = (ch >> 12) | 0xE0;
		*dest++ = ((ch >> 6) & 0x3F) | 0x80;
		*dest++ = (ch & 0x3F) | 0x80;
	}
	else if (ch < 0x110000) {
		if (dest >= dest_end - 3)
			return 4;
		len = 4;
		*dest++ = (ch >> 18) | 0xF0;
		*dest++ = ((ch >> 12) & 0x3F) | 0x80;
		*dest++ = ((ch >> 6) & 0x3F) | 0x80;
		*dest++ = (ch & 0x3F) | 0x80;
	}
	if (dest < dest_end)
		*dest = '\0';

	return len;
}

int Beefy::u8_wc_toutf8(char *dest, uint32 ch)
{
    if (ch < 0x80) {
        dest[0] = (char)ch;
        return 1;
    }
    if (ch < 0x800) {
        dest[0] = (ch>>6) | 0xC0;
        dest[1] = (ch & 0x3F) | 0x80;
        return 2;
    }
    if (ch < 0x10000) {
        dest[0] = (ch>>12) | 0xE0;
        dest[1] = ((ch>>6) & 0x3F) | 0x80;
        dest[2] = (ch & 0x3F) | 0x80;
        return 3;
    }
    if (ch < 0x110000) {
        dest[0] = (ch>>18) | 0xF0;
        dest[1] = ((ch>>12) & 0x3F) | 0x80;
        dest[2] = ((ch>>6) & 0x3F) | 0x80;
        dest[3] = (ch & 0x3F) | 0x80;
        return 4;
    }
    return 0;
}

/* charnum => byte offset */
int Beefy::u8_offset(char *str, int charnum)
{
    int offs=0;
    
    while (charnum > 0 && str[offs]) {
        (void)(isutf(str[++offs]) || isutf(str[++offs]) ||
               isutf(str[++offs]) || ++offs);
        charnum--;
    }
    return offs;
}

/* byte offset => charnum */
int Beefy::u8_charnum(char *s, int offset)
{
    int charnum = 0, offs=0;
    
    while (offs < offset && s[offs]) {
        (void)(isutf(s[++offs]) || isutf(s[++offs]) ||
               isutf(s[++offs]) || ++offs);
        charnum++;
    }
    return charnum;
}

/* number of characters */
int Beefy::u8_strlen(char *s)
{
    int count = 0;
    int i = 0;
    
    while (u8_nextchar(s, &i) != 0)
        count++;
    
    return count;
}

/* reads the next utf-8 sequence out of a string, updating an index */
uint32 Beefy::u8_nextchar(char *s, int *i)
{
    uint32 ch = 0;
    int sz = 0;
    
    do {
        ch <<= 6;
        ch += (unsigned char)s[(*i)++];
        sz++;
    } while ((ch != 0) && s[*i] && !isutf(s[*i]));
    ch -= offsetsFromUTF8[sz-1];
    
    return ch;
}

void Beefy::u8_inc(char *s, int *i)
{
    (void)(isutf(s[++(*i)]) || isutf(s[++(*i)]) ||
           isutf(s[++(*i)]) || ++(*i));
}

void Beefy::u8_dec(char *s, int *i)
{
    (void)(isutf(s[--(*i)]) || isutf(s[--(*i)]) ||
           isutf(s[--(*i)]) || --(*i));
}

int Beefy::octal_digit(char c)
{
    return (c >= '0' && c <= '7');
}

int Beefy::hex_digit(char c)
{
    return ((c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'F') ||
            (c >= 'a' && c <= 'f'));
}

/* assumes that src points to the character after a backslash
 returns number of input characters processed */
int Beefy::u8_read_escape_sequence(char *str, uint32 *dest)
{
    uint32 ch;
    char digs[9]="\0\0\0\0\0\0\0\0";
    int dno=0, i=1;
    
    ch = (uint32)str[0];    /* take literal character */
    if (str[0] == 'n')
        ch = L'\n';
    else if (str[0] == 't')
        ch = L'\t';
    else if (str[0] == 'r')
        ch = L'\r';
    else if (str[0] == 'b')
        ch = L'\b';
    else if (str[0] == 'f')
        ch = L'\f';
    else if (str[0] == 'v')
        ch = L'\v';
    else if (str[0] == 'a')
        ch = L'\a';
    else if (octal_digit(str[0])) {
        i = 0;
        do {
            digs[dno++] = str[i++];
        } while (octal_digit(str[i]) && dno < 3);
        ch = (uint32)strtol(digs, NULL, 8);
    }
    else if (str[0] == 'x') {
        while (hex_digit(str[i]) && dno < 2) {
            digs[dno++] = str[i++];
        }
        if (dno > 0)
            ch = (uint32)strtol(digs, NULL, 16);
    }
    else if (str[0] == 'u') {
        while (hex_digit(str[i]) && dno < 4) {
            digs[dno++] = str[i++];
        }
        if (dno > 0)
            ch = (uint32)strtol(digs, NULL, 16);
    }
    else if (str[0] == 'U') {
        while (hex_digit(str[i]) && dno < 8) {
            digs[dno++] = str[i++];
        }
        if (dno > 0)
            ch = (uint32)strtol(digs, NULL, 16);
    }
    *dest = ch;
    
    return i;
}

/* convert a string with literal \uxxxx or \Uxxxxxxxx characters to UTF-8
 example: u8_unescape(mybuf, 256, "hello\\u220e")
 note the double backslash is needed if called on a C string literal */
int Beefy::u8_unescape(char *buf, int sz, char *src)
{
    int c=0, amt;
    uint32 ch;
    char temp[4];
    
    while (*src && c < sz) {
        if (*src == '\\') {
            src++;
            amt = u8_read_escape_sequence(src, &ch);
        }
        else {
            ch = (uint32)*src;
            amt = 1;
        }
        src += amt;
        amt = u8_wc_toutf8(temp, ch);
        if (amt > sz-c)
            break;
        memcpy(&buf[c], temp, amt);
        c += amt;
    }
    if (c < sz)
        buf[c] = '\0';
    return c;
}

#ifdef _WIN32
#define snprintf _snprintf
#pragma warning (disable:4996)
#endif

int Beefy::u8_escape_wchar(char *buf, int sz, uint32 ch)
{
    if (ch == L'\n')
        return snprintf(buf, sz, "\\n");
    else if (ch == L'\t')
        return snprintf(buf, sz, "\\t");
    else if (ch == L'\r')
        return snprintf(buf, sz, "\\r");
    else if (ch == L'\b')
        return snprintf(buf, sz, "\\b");
    else if (ch == L'\f')
        return snprintf(buf, sz, "\\f");
    else if (ch == L'\v')
        return snprintf(buf, sz, "\\v");
    else if (ch == L'\a')
        return snprintf(buf, sz, "\\a");
    else if (ch == L'\\')
        return snprintf(buf, sz, "\\\\");
    else if (ch < 32 || ch == 0x7f)
        return snprintf(buf, sz, "\\x%hhX", (unsigned char)ch);
    else if (ch > 0xFFFF)
        return snprintf(buf, sz, "\\U%.8X", (uint32)ch);
    else if (ch >= 0x80 && ch <= 0xFFFF)
        return snprintf(buf, sz, "\\u%.4hX", (unsigned short)ch);
    
    return snprintf(buf, sz, "%c", (char)ch);
}

int Beefy::u8_escape(char *buf, int sz, char *src, int escape_quotes)
{
    int c=0, i=0, amt;
    
    while (src[i] && c < sz) {
        if (escape_quotes && src[i] == '"') {
            amt = snprintf(buf, sz - c, "\\\"");
            i++;
        }
        else {
            amt = u8_escape_wchar(buf, sz - c, u8_nextchar(src, &i));
        }
        c += amt;
        buf += amt;
    }
    if (c < sz)
        *buf = '\0';
    return c;
}

char* Beefy::u8_strchr(char *s, uint32 ch, int *charn)
{
    int i = 0, lasti=0;
    uint32 c;
    
    *charn = 0;
    while (s[i]) {
        c = u8_nextchar(s, &i);
        if (c == ch) {
            return &s[lasti];
        }
        lasti = i;
        (*charn)++;
    }
    return NULL;
}

char* Beefy::u8_memchr(char *s, uint32 ch, size_t sz, int *charn)
{
    int i = 0, lasti=0;
    uint32 c;
    int csz;
    
    *charn = 0;
    while (i < (int)sz) {
        c = csz = 0;
        do {
            c <<= 6;
            c += (unsigned char)s[i++];
            csz++;
        } while (i < (int)sz && !isutf(s[i]));
        c -= offsetsFromUTF8[csz-1];
        
        if (c == ch) {
            return &s[lasti];
        }
        lasti = i;
        (*charn)++;
    }
    return NULL;
}

int Beefy::u8_is_locale_utf8(char *locale)
{
    /* this code based on libutf8 */
    const char* cp = locale;
    
    for (; *cp != '\0' && *cp != '@' && *cp != '+' && *cp != ','; cp++) {
        if (*cp == '.') {
            const char* encoding = ++cp;
            for (; *cp != '\0' && *cp != '@' && *cp != '+' && *cp != ','; cp++)
                ;
            if ((cp-encoding == 5 && !strncmp(encoding, "UTF-8", 5))
                || (cp-encoding == 4 && !strncmp(encoding, "utf8", 4)))
                return 1; /* it's UTF-8 */
            break;
        }
    }
    return 0;
}

int Beefy::u8_vprintf(char *fmt, va_list ap)
{
    int cnt, sz=0;
    char *buf;
	wchar_t *wcs;
    
    sz = 512;
    buf = (char*)alloca(sz);
try_print:
    cnt = vsnprintf(buf, sz, fmt, ap);
    if (cnt >= sz) {
        buf = (char*)alloca(cnt - sz + 1);
        sz = cnt + 1;
        goto try_print;
    }
    wcs = (wchar_t*)alloca((cnt+1) * sizeof(wchar_t));
    cnt = u8_toucs(wcs, cnt+1, buf, cnt);
    printf("%ls", (wchar_t*)wcs);
    return cnt;
}

int Beefy::u8_printf(char *fmt, ...)
{
    int cnt;
    va_list args;
    
    va_start(args, fmt);
    
    cnt = u8_vprintf(fmt, args);
    
    va_end(args);
    return cnt;
}

bool Beefy::UTF8IsCombiningMark(uint32 c)
{	
	return ((c >= 0x0300) && (c <= 0x036F)) || ((c >= 0x1DC0) && (c <= 0x1DFF));
}

bool Beefy::UTF8GetGraphemeClusterSpan(const char* str, int strLength, int idx, int& startIdx, int& spanLength)
{
	const char* ptr = str;	

	// Move to start of char
	while (startIdx >= 0)
	{
		char c = ptr[startIdx];
		if (((uint8)c & 0x80) == 0)
		{
			// This is the simple and fast case - ASCII followed by the string end or more ASCII
			if ((startIdx == strLength - 1) || (((uint8)ptr[startIdx + 1] & 0x80) == 0))
			{
				spanLength = 1;
				return true;
			}
			break;
		}
		if (((uint8)c & 0xC0) != 0x80)
		{			
			uint32 c32 = u8_toucs(ptr + startIdx, strLength - startIdx);

			if (!UTF8IsCombiningMark(c32))
				break;
		}
		startIdx--;
	}

	int curIdx = startIdx;
	while (true)
	{		
		int cLen = 0;
		uint32 c32 = u8_toucs(ptr + startIdx, strLength - startIdx, &cLen);

		int nextIdx = curIdx + cLen;
		if ((curIdx != startIdx) && (!UTF8IsCombiningMark(c32)))
		{
			spanLength = curIdx - startIdx;			
			return true;
		}
		if (nextIdx == strLength)
		{
			spanLength = nextIdx - startIdx;
			return false;
		}
		curIdx = nextIdx;
	}	
}

void Beefy::UTF8Categorize(const char* str, int strLength, int& numCodePoints, int& numCombiningMarks)
{
	numCodePoints = 0;
	numCombiningMarks = 0;

	int offset = 0;
	while (offset < strLength)
	{
		int cLen = 0;
		uint32 c32 = u8_toucs(str + offset, strLength - offset, &cLen);

		numCodePoints++;
		if (UTF8IsCombiningMark(c32))
			numCombiningMarks++;

		offset += cLen;
	}
}
