#ifndef CMD_H_
#define CMD_H_

struct PluginContext;
struct ClientContext;

typedef struct CommandEntry {
    const char *path;          // pl. "set debug"
    const char *help;          // pl. "Enable debug mode"
    const char *arg_hint;      // pl. "on|off" vagy "<filename>"
    struct PluginContext *pc;  // melyik plugin regisztrálta
    int handlerid;
    //int (*handler)(ClientContext *ctx, CommandEntry *pe);
} CommandEntry;
//users:
typedef int (*server_execute_commands_fn)(struct ClientContext *ctx, CommandEntry *pe, char* cmd);
typedef int (*plugin_execute_commands_fn)(struct PluginContext *pc, struct ClientContext *ctx, CommandEntry *pe, char* cmd);

//internal:
typedef void (*cmd_register_commands_fn)(struct PluginContext* pc, const CommandEntry* cmds, size_t count);
typedef int (*cmd_search_fn)(const char* str);
typedef int (*cmd_get_fn)(size_t index, CommandEntry **pce);
typedef size_t (*cmd_get_cound_fn)(void);

#ifdef CMD_STATIC_LINKED
// host use the direct interface
void cmd_init();
void cmd_destroy();
void cmd_register_commands(struct PluginContext *pc, const CommandEntry *cmds, size_t count);
int cmd_search(const char* str);
int cmd_get(size_t index, CommandEntry **pce);
size_t cmd_get_count(void);

#else
// plugin will see the host interface only
#endif

#endif // CMD_H_