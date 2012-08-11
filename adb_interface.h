#ifndef ADB_INTERFACE_H_
#define ADB_INTERFACE_H_

#include <sys/types.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef void (*i_adb_ls_cb)(char const* name, struct stat const* st, void* cookie);

/**
 * @retval -1 error
 * @retval 0 ok, but no entries
 * @retval >0 ok
 */
int i_adb_ls(char const * path, i_adb_ls_cb cb_func, void* cookie);


/**
 * @param st pointer to struct stat or NULL
 * @retval -1 error
 * @retval 0 ok
 */
int i_adb_stat(char const * path, struct stat* st);


/**
 * @retval 0 ok
 * @retval !0 error
 */
int i_adb_pull(char const* rpath, char const* lpath);

/**
 * @retval 0 ok
 * @retval !0 error
 */
int i_adb_push(char const* lpath, char const* rpath);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus


#endif // ADB_INTERFACE_H_