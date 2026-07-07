#ifndef VM_ENDIAN_COMPAT_H
#define VM_ENDIAN_COMPAT_H 1

/*
 * Portable little-endian conversions for the VanMoof host tools.
 *
 * glibc/Linux provides le32toh()/htole32() in <endian.h>, but macOS and
 * Windows have no such header (and the BSDs put them in <sys/endian.h>).
 * The tools only use the 32-bit little-endian conversions; this shim maps
 * them onto each platform's facilities so `make` works everywhere. Include
 * this instead of <endian.h>.
 */

#if defined(__linux__) || defined(__CYGWIN__) || defined(__GLIBC__)
#  include <endian.h>

#elif defined(__APPLE__)
#  include <libkern/OSByteOrder.h>
#  define htole16(x) OSSwapHostToLittleInt16(x)
#  define le16toh(x) OSSwapLittleToHostInt16(x)
#  define htole32(x) OSSwapHostToLittleInt32(x)
#  define le32toh(x) OSSwapLittleToHostInt32(x)
#  define htole64(x) OSSwapHostToLittleInt64(x)
#  define le64toh(x) OSSwapLittleToHostInt64(x)

#elif defined(__FreeBSD__) || defined(__NetBSD__) || \
      defined(__OpenBSD__) || defined(__DragonFly__)
#  include <sys/endian.h>

#elif defined(_WIN32)
   /* Every Windows target is little-endian; these are identities. */
#  include <stdint.h>
#  define htole16(x) ((uint16_t)(x))
#  define le16toh(x) ((uint16_t)(x))
#  define htole32(x) ((uint32_t)(x))
#  define le32toh(x) ((uint32_t)(x))
#  define htole64(x) ((uint64_t)(x))
#  define le64toh(x) ((uint64_t)(x))

#else
#  include <endian.h>   /* unknown platform: hope it has the glibc header */
#endif

#endif /* VM_ENDIAN_COMPAT_H */
