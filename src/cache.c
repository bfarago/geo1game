#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include "cache.h"
#include "config.h"

char g_cache_dir[MAX_PATH];

// Initialize cache directory: create if missing, cleanup old cache files
void cachedir_init(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        if (mkdir(path, 0755) != 0) {
            errormsg("Failed to create cache directory: %s", path);
            return;
        } else {
            logmsg("Created cache directory: %s", path);
            return;
        }
    }

    DIR *dir = opendir(path);
    if (!dir) {
        errormsg("Cannot open cache directory: %s", path);
        return;
    }
    if (config_get_int("CACHE", "cleanup_on_start",1)){
        struct dirent *entry;
        char filepath[MAX_PATH];
        int count_deleted = 0;
        int count_failed = 0;
        int count_others = 0;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type != DT_REG) continue;
            const char *name = entry->d_name;
            if (strstr(name, ".png") || strstr(name, ".png_") || strstr(name, ".json") || strstr(name, ".html") || strstr(name, ".cache")) {
                snprintf(filepath, sizeof(filepath), "%s/%s", path, name);
                if (unlink(filepath) == 0) {
                    count_deleted++;
                    // logmsg("Deleted cache file: %s", filepath);
                } else {
                    count_failed++;
                    debugmsg("Failed to delete file: %s", filepath);
                }
            }else {
                count_others++;
            }
        }
        if (count_deleted + count_failed + count_others > 0) {
            logmsg("Cleanup on start: Deleted %d cache files, failed to delete %d cache files, and %d other files", count_deleted, count_failed, count_others);
        }
    }
    closedir(dir);
}
void cachesystem_init(void){
    config_get_string("CACHE", "dir", g_cache_dir, MAX_PATH, CACHE_DIR);
    cachedir_init(g_cache_dir);
}
const char* cache_get_dir(void){
    return g_cache_dir;
}

unsigned long _hash(const char *str) {
    unsigned long hash = 0;
    int c;
    while ((c = *str++))
        hash = c + (hash << 6) + (hash << 16) - hash;
    return hash;
}

// CacheFile

int cache_file_exists_recent(CacheFile *cf){
    return file_exists_recent(cf->path, CACHE_TIME);
}
int cache_file_init(CacheFile *cf, const char *name){
    snprintf(cf->path, sizeof(cf->path), "%s/%s.cache", cache_get_dir(), name);
    cf->fp = NULL;
    cf->hash = _hash(name); // todo: do it better...
    debugmsg("Cache name:%s, hash:%04x, path:%s", name, cf->hash, cf->path);
    return 0;
}
int cache_file_create(CacheFile *cf){
    cf->fp = NULL;        
    if (cf->path[0] != '\0') {
        /* TODO: do not forget,
        we need a global infrastructure to handle the cache file management, and
        provide it from host to plugins. There are an unhandled situation, even
        in png plugin, when two process is trying to write to the same file, due
        to the tmp file name also could be the same for two requests... I guess,
        the tmp file name should be unique or it should be handled in a different
        way. This is a temporary solution, but it is not good enough now.
         */
        cf->fp = fopen(cf->path, "w");
        if (!cf->fp) {
            logmsg("Failed to open cache file for writing: %s", cf->path);
            return -1;
        }else{
            debugmsg("Open cache file for writing: %s", cf->path);
            return 1;
        }
    }
    debugmsg("Open cache file for writing, but wrong filename was provided.");
    return 0;
}
int cache_file_remove(CacheFile *cf){
    if (cf->fp) {
        errormsg("Cannot remove file, it is still open: %s", cf->path);
        return 2;
    }
    if (unlink(cf->path) != 0) {
        debugmsg("Failed to delete file: %s", cf->path);
        return 1;
    }
    return 0;
}
int cache_file_rename(CacheFile *cf, const char *new_name){
    if (cf->fp) {
        errormsg("Cannot rename file, it is still open: %s", cf->path);
        return 2;
    }
    char new_path[MAX_PATH];
    snprintf(new_path, MAX_PATH, "%s/%s", cache_get_dir(), new_name);
    int res=rename(cf->path, new_path);
    if (res != 0) {
        errormsg("Failed to rename %s to %s", cf->path, new_path);
        return 1;
    }
    strcpy(cf->path, new_path);
    return 0;
}
int cache_file_read(CacheFile *cf, void *buf, size_t size){
    if (cf->fp) {
        return fread(buf, 1, size, cf->fp);
    }
    return 0;
}
int cache_file_write(CacheFile *cf, const void *buf, size_t size){
    if (cf->fp) {
        if (size){
            size_t written = fwrite(buf, 1, size, cf->fp);
            if (written != size){
                errormsg("Failed to write to file: %s written/requested: %zu/%zu", cf->path, written, size);
            }
            return written;
        }else{
            debugmsg("Failed to write to file, size is zero: %s", cf->path);
        }
    }else{
        if (cf->path[0] != '\0'){
            debugmsg("Failed to write to file, it is not open: %s", cf->path);
        }
    }
    return 0;
}
int cache_file_close(CacheFile *cf){
    if (cf->fp){
        fclose(cf->fp);
        cf->fp = NULL;
        debugmsg("Closed file: %s", cf->path);
    }
    return 0;
}