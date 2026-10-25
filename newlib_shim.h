/*
 * newlib compatibility shims — include this once from any sketch that links
 * against the prebuilt AZ3166 SDK archives.
 *
 * Why: libdevkit-sdk-core-lib.a and libwlan.a were compiled against newlib
 * 2.4, while the gcc-arm-none-eabi 4.8.3 toolchain that builds this platform
 * ships newlib 2.1. Newlib 2.4 objects reference __locale_ctype_ptr() and
 * plain C itoa()/utoa(), none of which exist in 2.1, so the link fails with
 * "undefined reference to __locale_ctype_ptr" / "utoa". These definitions fill
 * the gap; drop them if you ever build with a matching newer toolchain.
 */
#pragma once

extern "C" {

/* newlib 2.1 exports __ctype_ptr__; 2.4 replaced it with this accessor */
const char *__locale_ctype_ptr(void) { return __ctype_ptr__; }

char *itoa(int value, char *str, int base)
{
    char *p = str;
    char tmp[34];
    int i = 0;
    unsigned int v;
    bool neg = false;
    if (base == 10 && value < 0) {
        neg = true;
        v = (unsigned int)(-value);
    } else {
        v = (unsigned int)value;
    }
    do {
        int d = v % base;
        tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= base;
    } while (v);
    if (neg) tmp[i++] = '-';
    while (i) *p++ = tmp[--i];
    *p = '\0';
    return str;
}

char *utoa(unsigned int value, char *str, int base)
{
    char *p = str;
    char tmp[34];
    int i = 0;
    do {
        int d = value % base;
        tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        value /= base;
    } while (value);
    while (i) *p++ = tmp[--i];
    *p = '\0';
    return str;
}

/* the Arduino core calls these through C++ (mangled) declarations, the
   prebuilt SDK archive calls them as plain C: provide both symbols */
char *itoa_cxx(int, char *, int) __asm__("_Z4itoaiPci");
char *itoa_cxx(int value, char *str, int base) { return itoa(value, str, base); }

char *utoa_cxx(unsigned int, char *, int) __asm__("_Z4utoajPci");
char *utoa_cxx(unsigned int value, char *str, int base) { return utoa(value, str, base); }

} // extern "C"
