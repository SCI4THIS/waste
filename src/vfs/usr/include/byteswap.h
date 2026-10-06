#ifndef WASTE_BYTESWAP_H
#define WASTE_BYTESWAP_H

static inline unsigned short waste_byteswap16(unsigned short value) {
  return (unsigned short)((value << 8) | (value >> 8));
}
static inline unsigned int waste_byteswap32(unsigned int value) {
  return ((value & 0x000000ffu) << 24) |
         ((value & 0x0000ff00u) << 8) |
         ((value & 0x00ff0000u) >> 8) |
         ((value & 0xff000000u) >> 24);
}
static inline unsigned long long waste_byteswap64(unsigned long long value) {
  return ((unsigned long long)waste_byteswap32((unsigned int)value) << 32) |
         waste_byteswap32((unsigned int)(value >> 32));
}

#define bswap_16(value) waste_byteswap16((unsigned short)(value))
#define bswap_32(value) waste_byteswap32((unsigned int)(value))
#define bswap_64(value) waste_byteswap64((unsigned long long)(value))

#endif
