#pragma once

//#ifndef SQLITE_NDK_VFS_NAME
//#define SQLITE_NDK_VFS_NAME "ndk-asset"
//#endif
//
//#ifndef SQLITE_NDK_VFS_MAKE_DEFAULT
//#define SQLITE_NDK_VFS_MAKE_DEFAULT 0
//#endif
//
//#define SQLITE_NDK_VFS_PARENT_VFS NULL

#ifdef __cplusplus
extern "C" {
#endif

int sqlite3_fdvfs_init(
    const char* vfsName, //= SQLITE_NDK_VFS_NAME,
    int makeDflt, // = SQLITE_NDK_VFS_MAKE_DEFAULT,
    const char *osVfs);  // = SQLITE_NDK_VFS_PARENT_VFS);

#ifdef __cplusplus
}
#endif
