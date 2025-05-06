/*
 * File:    cache.h
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 * 
 * cache utility functions
 * Key features:
 *  manage cache director and files.
 *  create, remove, rename, write
 */
#ifndef CACHE_H
#define CACHE_H
#include "global.h"


typedef struct CacheFile {
    unsigned long hash;
    char path[MAX_PATH];
    void *fp;
} CacheFile;

typedef struct CacheHostInterface
{
    const char* (*get_dir)(void);
    int (*file_init)(CacheFile *cf, const char *name);
    int (*file_create)(CacheFile *cf);
    int (*file_close)(CacheFile *cf);
    int (*file_remove)(CacheFile *cf);
    int (*file_rename)(CacheFile *cf, const char *name);
    int (*file_exists_recent)(CacheFile *cf);
    int (*file_write)(CacheFile *cf, const void *buf, size_t size);
}
CacheHostInterface;


extern char g_cache_dir[MAX_PATH]; // temoporary export

// main program needs this
void cachesystem_init(void);
const char* cache_get_dir(void);

// subsystems
void cachedir_init(const char *path);

//CacheFile functions
int cache_file_exists_recent(CacheFile *cf);
int cache_file_init(CacheFile *cf, const char *name);
int cache_file_create(CacheFile *cf);
int cache_file_remove(CacheFile *cf);
int cache_file_rename(CacheFile *cf, const char *new_name);
int cache_file_read(CacheFile *cf, void *buf, size_t size);
int cache_file_write(CacheFile *cf, const void *buf, size_t size);
int cache_file_close(CacheFile *cf);

#endif // CACHE_H
