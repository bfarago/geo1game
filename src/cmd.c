/**
 * File: cmd.c
 * 
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define CMD_STATIC_LINKED
#include "cmd.h"

// Storage for registered commands
typedef struct {
    CommandEntry *entries;
    size_t count;
    size_t capacity;
} CommandRegistry;

static CommandRegistry g_registry = {NULL, 0, 0};

void cmd_init() {
    g_registry.entries = NULL;
    g_registry.count = 0;
    g_registry.capacity = 0;
}

void cmd_destroy() {
    if (g_registry.entries) {
        // Free each entry's strings
        for (size_t i = 0; i < g_registry.count; ++i) {
            if (g_registry.entries[i].path) free((void*)g_registry.entries[i].path);
            if (g_registry.entries[i].help) free((void*)g_registry.entries[i].help);
            if (g_registry.entries[i].arg_hint) free((void*)g_registry.entries[i].arg_hint);
        }
        free(g_registry.entries);
    }
    g_registry.entries = NULL;
    g_registry.count = 0;
    g_registry.capacity = 0;
}

void cmd_register_commands(struct PluginContext *pc, const CommandEntry *cmds, size_t count) {
    if (count <= 0 || !cmds) return;

    // Ensure capacity
    if (g_registry.count + count > g_registry.capacity) {
        int new_capacity = (g_registry.count + count) * 2;
        CommandEntry *new_entries = realloc(g_registry.entries, new_capacity * sizeof(CommandEntry));
        if (!new_entries) {
            fprintf(stderr, "Failed to allocate memory for CLI command registry\n");
            return;
        }
        g_registry.entries = new_entries;
        g_registry.capacity = new_capacity;
    }

    // Register each command
    for (size_t i = 0; i < count; ++i) {
        CommandEntry *e = &g_registry.entries[g_registry.count];
        e->path = cmds[i].path ? strdup(cmds[i].path) : NULL;
        e->help = cmds[i].help ? strdup(cmds[i].help) : NULL;
        e->arg_hint = cmds[i].arg_hint ? strdup(cmds[i].arg_hint) : NULL;
        e->pc = pc; //cmds[i].plugin_id;
        e->handlerid = i;
        g_registry.count++;
    }
}

int cmd_search(const char* str) {
    if (!str || !g_registry.entries || g_registry.count == 0) return -1;
    size_t slen = strlen(str);
    for (size_t i = 0; i < g_registry.count; ++i) {
        if (!g_registry.entries[i].path) continue;
        if (strncmp(g_registry.entries[i].path, str, slen) == 0) {
            return (int)i;
        }
    }
    return -1;
}

int cmd_get(size_t index, CommandEntry **pce){
    *pce = &g_registry.entries[index];
    return 0;
}

size_t cmd_get_count(void) {
    return g_registry.count;
}
