#ifndef __WOZHOST_AIIE_H
#define __WOZHOST_AIIE_H

// Point the wozzle library at Aiie's file manager (and, on the Teensy,
// at external RAM and the serial console). WozSerializer calls this
// itself once g_filemanager exists, so a platform doesn't have to.
// Calling it again is harmless.
void installWozHost();

#endif
