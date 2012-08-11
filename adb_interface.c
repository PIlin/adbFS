#include <errno.h>
#include "adb/sysdeps.h"


int   adb_trace_mask;
// #if ADB_TRACE
ADB_MUTEX_DEFINE( D_lock );
// #endif

int readx(int fd, void *ptr, size_t len)
{
    char *p = ptr;
    int r;
#if ADB_TRACE
    int  len0 = len;
#endif
    // D("readx: fd=%d wanted=%d\n", fd, (int)len);
    while(len > 0) {
        r = adb_read(fd, p, len);
        if(r > 0) {
            len -= r;
            p += r;
        } else {
            if (r < 0) {
                // D("readx: fd=%d error %d: %s\n", fd, errno, strerror(errno));
                if (errno == EINTR)
                    continue;
            } else {
                // D("readx: fd=%d disconnected\n", fd);
            }
            return -1;
        }
    }

#if ADB_TRACE
    D("readx: fd=%d wanted=%d got=%d\n", fd, len0, len0 - len);
    dump_hex( ptr, len0 );
#endif
    return 0;
}

int writex(int fd, const void *ptr, size_t len)
{
    char *p = (char*) ptr;
    int r;

#if ADB_TRACE
    D("writex: fd=%d len=%d: ", fd, (int)len);
    dump_hex( ptr, len );
#endif
    while(len > 0) {
        r = adb_write(fd, p, len);
        if(r > 0) {
            len -= r;
            p += r;
        } else {
            if (r < 0) {
                // D("writex: fd=%d error %d: %s\n", fd, errno, strerror(errno));
                if (errno == EINTR)
                    continue;
            } else {
                // D("writex: fd=%d disconnected\n", fd);
            }
            return -1;
        }
    }
    return 0;
}
