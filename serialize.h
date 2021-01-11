#ifndef __SERIALIZE_H
#define __SERIALIZE_H

#define serialize8(var) { uint8_t buf = (uint8_t)var;   \
  if (g_filemanager->write(fd, &buf, 1) != 1) { \
    printf("Failed to write 1 byte\n"); \
    goto err;				\
  } \
}

#define serializeMagic(var) { \
  printf("Serializing magic '%d'\n", var); \
  uint8_t buf = var; \
  if (g_filemanager->write(fd, &buf, 1) != 1) { \
    printf("Failed to write 1 byte of magic\n"); \
    goto err; \
  } \
}

#define serialize16(var) { \
  uint8_t buf[2]; \
  uint8_t ptr = 0; \
  buf[ptr++] = ((var >>  8) & 0xFF); \
  buf[ptr++] = ((var      ) & 0xFF); \
  if (g_filemanager->write(fd, buf, 2) != 2) { \
    printf("Failed to write 2 bytes\n"); \
    goto err; \
  } \
}

#define serialize32(var) { \
  uint8_t buf[4]; \
  uint8_t ptr = 0; \
  buf[ptr++] = ((var >> 24) & 0xFF); \
  buf[ptr++] = ((var >> 16) & 0xFF); \
  buf[ptr++] = ((var >>  8) & 0xFF); \
  buf[ptr++] = ((var      ) & 0xFF); \
  if (g_filemanager->write(fd, buf, 4) != 4) { \
    printf("Failed to write 4 bytes\n"); \
    goto err; \
  } \
}

#define serialize64(var) {			\
  uint8_t buf[8]; \
  uint8_t ptr = 0; \
  buf[ptr++] = ((var >> 56) & 0xFF); \
  buf[ptr++] = ((var >> 48) & 0xFF); \
  buf[ptr++] = ((var >> 40) & 0xFF); \
  buf[ptr++] = ((var >> 32) & 0xFF); \
  buf[ptr++] = ((var >> 24) & 0xFF); \
  buf[ptr++] = ((var >> 16) & 0xFF); \
  buf[ptr++] = ((var >>  8) & 0xFF); \
  buf[ptr++] = ((var      ) & 0xFF); \
  if (g_filemanager->write(fd, buf, 8) != 8) { \
    printf("Failed to write 8 bytes\n"); \
    goto err; \
  } \
}

#define serializeString(s) { \
  if (g_filemanager->write(fd, s, strlen(s)+1) != strlen(s)+1) { \
    printf("Failed to write string '%s'\n", s); \
    goto err; \
  } \
}

#define deserialize8(var) { \
  uint8_t buf; \
  if (g_filemanager->read(fd, &buf, 1) != 1) { \
    printf("Failed to deserialize 1 byte\n"); \
    goto err; \
  } \
  var = buf; \
}

#define deserializeMagic(expect) {		\
  uint8_t buf; \
  printf("Deserializing magic, expecting 0x%X\n", expect); \
  if (g_filemanager->read(fd, &buf, 1) != 1) { \
    printf("Failed to deserialize 1 byte of magic\n"); \
    goto err; \
  } \
  if (buf != expect) { \
    printf("magic error: 0x%X does not match expected 0x%X\n", buf, expect); \
    goto err; \
  } \
}

#define deserialize16(var) { \
  uint8_t buf[2]; \
  uint8_t ptr = 0; \
  if (g_filemanager->read(fd, buf, 2) != 2) { \
    printf("Failed to deserialize 2 bytes\n"); \
    goto err; \
  } \
  var = buf[ptr++]; \
  var <<= 8; var |= buf[ptr++]; }

#define deserialize32(var) { \
  uint8_t buf[4]; \
  uint8_t ptr = 0; \
  if (g_filemanager->read(fd, buf, 4) != 4) { \
    printf("Failed to deserialize 4 bytes\n"); \
    goto err; \
  } \
  var = buf[ptr++]; \
  var <<= 8; var |= buf[ptr++]; \
  var <<= 8; var |= buf[ptr++]; \
  var <<= 8; var |= buf[ptr++]; }

#define deserialize64(var) { \
  uint8_t buf[8]; \
  uint8_t ptr = 0; \
  if (g_filemanager->read(fd, buf, 8) != 8) { \
    printf("Failed to deserialize 8 bytes\n"); \
    goto err; \
  } \
  var = buf[ptr++]; \
  var <<= 8; var |= buf[ptr++]; \
  var <<= 8; var |= buf[ptr++]; \
  var <<= 8; var |= buf[ptr++]; \
  var <<= 8; var |= buf[ptr++]; \
  var <<= 8; var |= buf[ptr++]; \
  var <<= 8; var |= buf[ptr++]; \
  var <<= 8; var |= buf[ptr++]; }

#define deserializeString(var) { \
  uint8_t c; \
  char *ptr = var; \
  while (1) { \
    if (g_filemanager->read(fd, &c, 1) != 1) { \
      printf("Failed to read string byte\n"); \
      goto err; \
    } \
      printf(". "); \
    *(ptr++) = c; \
    if (c == 0) { break; } \
  } \
}

#endif
