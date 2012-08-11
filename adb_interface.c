#include "adb_interface.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include "adb/fdevent.h"
#include "adb/adb_client.h"
#include "adb/sysdeps.h"
#include "adb/file_sync_service.h"


////////////////////////////////////////////////////////////////////////////////
// Macro

#define DL(msg, ...) do { fprintf(stderr, msg, __VA_ARGS__); } while (0)
#define TRACE() do { DL("%s:%d: %s\n", __FILE__, __LINE__, __FUNCTION__); } while(0)



////////////////////////////////////////////////////////////////////////////////
// Some allocations

#undef ADB_TRACE

int   adb_trace_mask;
//#if ADB_TRACE
ADB_MUTEX_DEFINE( D_lock );
//#endif

////////////////////////////////////////////////////////////////////////////////
// readx and writex from adb/transport.c

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

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// interface

void sync_quit(int fd);

// ls

typedef void (*sync_ls_cb)(unsigned mode, unsigned size, unsigned time, const char *name, void *cookie);
int sync_ls(int fd, const char *path, sync_ls_cb func, void *cookie);

typedef struct ls_cookie {
    i_adb_ls_cb cb_func;
    void* cookie;
    int* counter;
} ls_cookie;

static void do_sync_ls_cb(unsigned mode, unsigned size, unsigned time,
                          const char *name, void *cookie)
{
    //printf("%08x %08x %08x %s\n", mode, size, time, name);
    struct stat st = {0};
    st.st_mode = mode,
    st.st_size = size,
    st.st_mtime = time;

    ls_cookie* ck = (ls_cookie*)cookie;

    *(ck->counter) += 1;

    i_adb_ls_cb cb_func = (i_adb_ls_cb)ck->cb_func;
    return cb_func(name, &st, ck->cookie);
}

int i_adb_ls(char const * path, i_adb_ls_cb cb_func, void* cookie)
{
    TRACE();
    DL("path = %s", path);

    assert(cb_func);
    if (!cb_func) {
        fprintf(stderr,"cb_func is NULL");
        return -1;
    }

    int fd = adb_connect("sync:");
    if(fd < 0) {
        fprintf(stderr,"error: %s\n", adb_error());
        return -1;
    }

    int counter = 0;

    ls_cookie ck = {.cb_func = cb_func, .cookie = cookie, .counter = &counter};

    if(sync_ls(fd, path, do_sync_ls_cb, &ck)) {
        return -1;
    } else {
        sync_quit(fd);
        return counter;
    }
}


// ls

static int do_stat(int fd, char const * path, struct stat* st)
{
    TRACE();

    syncmsg msg;
    int len = strlen(path);

    msg.req.id = ID_STAT;
    msg.req.namelen = htoll(len);

    if(writex(fd, &msg.req, sizeof(msg.req)) ||
       writex(fd, path, len)) {
        return -1;
    }

    if(readx(fd, &msg.stat, sizeof(msg.stat))) {
        return -1;
    }

    if(msg.stat.id != ID_STAT) {
        return -1;
    }

    mode_t mode = ltohl(msg.stat.mode);
    off_t  size = ltohl(msg.stat.size);
    time_t time = ltohl(msg.stat.time);

    if (!mode && !size && !time)
        return -1;

    if (st)
    {
        st->st_mode = mode;
        st->st_size = size;
        st->st_mtime = time;
    }
    return 0;
}

int i_adb_stat(char const * path, struct stat* st)
{
    TRACE();
    DL("path = %s\n", path);

    int fd = adb_connect("sync:");
    if(fd < 0) {
        fprintf(stderr,"error: %s\n", adb_error());
        return -1;
    }

    if(do_stat(fd, path, st)) {
        return -1;
    } else {
        sync_quit(fd);
        return 0;
    }
}

// pull

int i_adb_pull(char const* rpath, char const* lpath)
{
    TRACE();
    return do_sync_pull(rpath, lpath);
}

// push

int i_adb_push(char const* lpath, char const* rpath)
{
    TRACE();
    return do_sync_push(lpath, rpath, 0);
}