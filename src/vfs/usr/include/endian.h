#ifndef WASTE_ENDIAN_H
#define WASTE_ENDIAN_H

#include <byteswap.h>

#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN 4321
#define __BYTE_ORDER __LITTLE_ENDIAN
#define htobe16(value) bswap_16(value)
#define htole16(value) ((unsigned short)(value))
#define be16toh(value) bswap_16(value)
#define le16toh(value) ((unsigned short)(value))
#define htobe32(value) bswap_32(value)
#define htole32(value) ((unsigned int)(value))
#define be32toh(value) bswap_32(value)
#define le32toh(value) ((unsigned int)(value))
#define htobe64(value) bswap_64(value)
#define htole64(value) ((unsigned long long)(value))
#define be64toh(value) bswap_64(value)
#define le64toh(value) ((unsigned long long)(value))

#endif
