#ifndef PUTTY_WILDCARD_H_INCLUDE
#define PUTTY_WILDCARD_H_INCLUDE

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *wc_error(int value);
int wc_match(const char *wildcard, const char *target);
bool wc_unescape(char *output, const char *wildcard);

#ifdef __cplusplus
}
#endif

#endif // PUTTY_WILDCARD_H_INCLUDE