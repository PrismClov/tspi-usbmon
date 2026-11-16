/* Compile-check stub, not shipped. */
#ifndef _STUB_TURBOJPEG_H
#define _STUB_TURBOJPEG_H
#include <stddef.h>
typedef void *tjhandle;
#define TJPF_BGRX 1
#define TJFLAG_FASTDCT 2048
tjhandle tjInitDecompress(void);
int tjDestroy(tjhandle);
int tjDecompressHeader3(tjhandle, const unsigned char *, unsigned long, int *, int *, int *, int *);
int tjDecompress2(tjhandle, const unsigned char *, unsigned long, unsigned char *, int, int, int, int, int);
char *tjGetErrorStr2(tjhandle);
#endif
