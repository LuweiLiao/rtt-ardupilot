#ifndef RTT_TEST_AP_STRINGS_SHIM_H
#define RTT_TEST_AP_STRINGS_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, unsigned int n);

#ifdef __cplusplus
}
#endif

#endif
