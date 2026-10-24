#ifndef __WOZHOST_AIIE_H
#define __WOZHOST_AIIE_H

// Point the wozzle library at Aiie's file manager (and, on the Teensy,
// at external RAM and the serial console). Call it once g_filemanager
// exists and before any disk image is opened.
void installWozHost();

#endif
