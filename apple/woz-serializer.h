#ifndef __WOZ_SERIALIZER_H
#define __WOZ_SERIALIZER_H

#include "woz.h"
class WozSerializer: public virtual Woz {
public:
  WozSerializer();

 public:
  bool Serialize(int8_t fd);
  bool Deserialize(int8_t fd);
};


#endif
