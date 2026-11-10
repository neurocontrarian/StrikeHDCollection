/* Two C library functions that GCC 15's static libstdc++ uses but glibc 2.35 lacks. */
#include <stdlib.h>
#include <sys/random.h>
unsigned long __isoc23_strtoul(const char *s, char **end, int base) { return strtoul(s, end, base); }
unsigned int arc4random(void)
{
    unsigned int v = 0;
    while (getrandom(&v, sizeof v, 0) != (long)sizeof v) {}
    return v;
}
