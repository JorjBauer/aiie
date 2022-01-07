/* Filesystem compatability layer.
 *
 * Right now I've got a hokey (but well abstracted) g_filemanager object
 * that has a bunch of file operation primitives which don't conform to 
 * POSIX at all. The woz code comes from my Wozzle utility, and I'd rather
 * not butcher the heck out of it for the sake of Aiie... so this is 
 * bridging that gap, for now.
 */

#include "globals.h"

#define open(path, flags, perms) g_filemanager->openFile(path)
#define close(filedes) g_filemanager->closeFile(filedes)
#define write(filedes,buf,nbyte) g_filemanager->write(filedes,buf,nbyte)
#define read(filedes,buf,nbyte) g_filemanager->read(filedes,buf,nbyte)
#define lseek(filedes,offset,whence) (int) g_filemanager->lseek(filedes,(int)offset,(int)whence)

  
