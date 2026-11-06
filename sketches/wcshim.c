/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Preview only: this box's glibc predates Unicode 16, Arch's does not. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <wchar.h>
int wcwidth(wchar_t c)
{
	static int (*real)(wchar_t);
	if (c >= 0x1CD00 && c <= 0x1CDE5) return 1;
	if (!real) real = (int (*)(wchar_t))dlsym(RTLD_NEXT, "wcwidth");
	return real(c);
}
