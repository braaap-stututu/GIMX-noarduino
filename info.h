#ifndef INFO_H_
#define INFO_H_

#define INFO_VERSION "8.0"
#define INFO_YEAR "2010-2020"
#define INFO_DEV "Matlo"
#define INFO_WEB "http://gimx.fr"
#define INFO_LICENCE "GNU GPL v3"

#define INFO_FW_VERSION "8.0"

#ifdef __x86_64__
  #define INFO_ARCH "x86_64"
#endif

#ifdef __i386__
  #define INFO_ARCH "i386"
#endif

#ifdef __ARM_ARCH_6__
  #define INFO_ARCH "ARMv6"
#endif

#ifndef INFO_ARCH
  #define INFO_ARCH ""
#endif

/*
 * The launcher's self-update downloads the official GIMX release from gimx.fr, which would replace
 * this build (and its software-only adapter). It is disabled unless GIMX_UPSTREAM_UPDATES is defined.
 */
#ifdef GIMX_UPSTREAM_UPDATES

#define VERSION_URL "https://gimx.fr/download/version"

#ifdef WIN32
#ifdef __x86_64__
#define DOWNLOAD_URL "https://gimx.fr/download/gimx-windows-64bits"
#endif

#ifdef __i386__
#define DOWNLOAD_URL "https://gimx.fr/download/gimx-windows-32bits"
#endif
#else

#ifdef __x86_64__
#define DOWNLOAD_URL "https://gimx.fr/download/gimx-ubuntu-64bits"
#endif

#ifdef __i386__
#define DOWNLOAD_URL "https://gimx.fr/download/gimx-ubuntu-32bits"
#endif

#ifdef __ARM_ARCH_6__
#define DOWNLOAD_URL "https://gimx.fr/download/gimx-raspbian"
#endif

#endif

#endif /* GIMX_UPSTREAM_UPDATES */

#endif /* INFO_H_ */
