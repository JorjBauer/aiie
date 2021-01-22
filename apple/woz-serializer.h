#ifndef __WOZ_SERIALIZER_H
#define __WOZ_SERIALIZER_H

#include "woz.h"
class WozSerializer: public virtual Woz {
public:
  WozSerializer();
  const char *diskName();

 public:
  bool Serialize(int8_t fd);
  bool Deserialize(int8_t fd);

  virtual bool flush();

  virtual bool writeNextWozBit(uint8_t datatrack, uint8_t bit);
  virtual bool writeNextWozByte(uint8_t datatrack, uint8_t b);
  virtual uint8_t nextDiskBit(uint8_t datatrack);
  virtual uint8_t nextDiskByte(uint8_t datatrack);
};


#endif
