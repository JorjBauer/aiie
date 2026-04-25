#ifndef __DISKTYPES_H
#define __DISKTYPES_H

enum {
  T_AUTO = 0,
  T_WOZ = 1,
  T_NIB = 2,
  T_DSK = 3,
  T_PO = 4,
  T_HDV = 5   // ProDOS block-ordered hard-disk image (.hdv, .2mg, large .po/.img)
};

#endif
