#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <ctype.h>

#include "sync.h"
#include "../plugin.h"
// globals

#define VT100_SLEEP_TIME_MS (50) // 50ms
#define VT100_SLEEP_TIME_US (VT100_SLEEP_TIME_MS * 1000)


const PluginHostInterface *g_host;
const char* g_control_routes[2]={"help", "vt100"};
int g_control_routess_count = 2;

int procfs_threads_str_dump(char* buf, size_t len){
    int o = 0;
    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "/proc/%d/task/", getpid());
    DIR *dir = opendir(path);
    if (dir) {
        o += snprintf(buf + o, len - o, "TID    | Name           |St.|  CPU  |Pri| Stack |\r\n");
        o += snprintf(buf + o, len - o, "-------+----------------+---+-------+---+-------+\r\n");
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type != DT_DIR) continue;
            if (!isdigit(entry->d_name[0])) continue;

            char stat_path[MAX_PATH];
            snprintf(stat_path, MAX_PATH, "/proc/%d/task/%s/stat", getpid(), entry->d_name);

            FILE *fp = fopen(stat_path, "r");
            if (fp) {
                char comm[256], state;
                int pid;
                unsigned long utime, stime;
                // Remove RSS, add priority and stack pointer (kstkesp)
                // Parse from /proc/[pid]/task/[tid]/stat: https://man7.org/linux/man-pages/man5/proc.5.html
                fscanf(fp, "%d (%255[^)]) %c", &pid, comm, &state);
                for (int i = 0; i < 11; i++) fscanf(fp, "%*s"); // skip to utime
                fscanf(fp, "%lu %lu", &utime, &stime);
                for (int i = 0; i < 2; i++) fscanf(fp, "%*s"); // skip cutime, cstime
                unsigned long priority;
                fscanf(fp, "%lu", &priority);
                for (int i = 0; i < 12; i++) fscanf(fp, "%*s"); // skip to kstkesp
                unsigned long kstkesp = 0;
                fscanf(fp, "%lu", &kstkesp);
                fclose(fp);

                o += snprintf(buf + o, len - o, "%-6s |%-16s| %c | %-2lu+%-2lu | %lu | 0x%04lx|\r\n",
                    entry->d_name, comm, state, utime, stime, priority, kstkesp);
            }
        }
        o += snprintf(buf + o, len - o, "\r\n");
        closedir(dir);
    }
    return o;
}
static const char *g_state_labels[PLUGIN_STATE_MAX] ={ // "0ulLIsRS7"; 
    [PLUGIN_STATE_NONE] = "None",
    [PLUGIN_STATE_UNLOADED] = "Unloaded",
    [PLUGIN_STATE_LOADING] = "Loading",
    [PLUGIN_STATE_LOADED] = "Loaded",
    [PLUGIN_STATE_INITIALIZED] = "Init",
    [PLUGIN_STATE_RUNNING] = "Running",
    [PLUGIN_STATE_SHUTTING_DOWN] = "Shut down",
    [PLUGIN_STATE_DISABLED] = "disabled"
};
void plugins_str_dump(char *buf, int len){
    int o=0;
    int pn = g_host->server.get_plugin_count();
    o += snprintf(buf + o, len - o, "Id | Plugin name    | State    |\r\n");
    o += snprintf(buf + o, len - o, "---+----------------+----------+\r\n");
    for (int i=0 ; i<pn; i++){
        PluginContext *p= g_host->server.get_plugin(i);
        if (p->state == PLUGIN_STATE_DISABLED) continue;
        
        o += snprintf(buf + o, len - o, "%-2d |%-16s|%-10s|", i, p->name, g_state_labels[p->state] );
        if (p->stat.det_str_dump){
            o += p->stat.det_str_dump(buf+o, len - o);
        }
        o += snprintf(buf + o, len - o, "\r\n");
    }
}
void genStat(int fd){
    char buf[BUF_SIZE];
    procfs_threads_str_dump(buf, BUF_SIZE);
    dprintf(fd, "Thread statistics\r\n%s", buf);
    plugins_str_dump(buf, BUF_SIZE);
    dprintf(fd, "Plugin statistics\r\n%s", buf);
    g_host->server.server_dump_stat(buf, BUF_SIZE);
    dprintf(fd, "\r\nProtocol statistics\r\n%s", buf);
}
void pcontrol_handler(PluginContext *pc, ClientContext *ctx, char* cmd, int argc, char **argv)
{
    (void)pc;
    (void)argc;
    (void)argv;

    if (strcasecmp(cmd, "help") == 0) {
        dprintf(ctx->socket_fd, "HELP:\nThis is the command line interface.\nAvailable commands:\n- help\n- vt100\n");
    } else if (strcasecmp(cmd, "vt100") == 0) {
        int orig_flags = fcntl(ctx->socket_fd, F_GETFL, 0);
        if (orig_flags != -1) {
            fcntl(ctx->socket_fd, F_SETFL, orig_flags | O_NONBLOCK);
        }

        //int fdtype = fcntl(ctx->socket_fd, F_GETFL);
        //g_host->debugmsg("FD flags: 0x%x", fdtype);

        struct stat st;
        fstat(ctx->socket_fd, &st);
        //g_host->debugmsg("FD mode: 0x%x", st.st_mode);
        // Kezdeményezzük a Telnet protokoll tárgyalást a karakterenkénti küldéshez
        unsigned char telnet_negotiation[] = {
            255, 251, 1,   // IAC WILL ECHO (mi echo-znánk, de most inkább csak tárgyaljuk)
            255, 251, 3,   // IAC WILL SUPPRESS-GO-AHEAD
            255, 253, 1,   // IAC DO ECHO (kliens echo-zza vissza nekünk — kikapcsolható)
            255, 253, 3    // IAC DO SUPPRESS-GO-AHEAD (kliens character-at-a-time módra áll)
        };
        send(ctx->socket_fd, telnet_negotiation, sizeof(telnet_negotiation), 0);

        dprintf(ctx->socket_fd, "\x1b[2J\x1b[HVT100 mode started. Press 'q' to exit.\r\n");
        dprintf(ctx->socket_fd, "\x1b[12l");  // DECSET 12 – Disable local echo
        dprintf(ctx->socket_fd, "\x1b[18t"); // Request terminal size

        char buffer[128] = {0};
        PluginOwnThreadInfo *poti= &pc->thread.own_threads[0];
        poti->thread = (sync_thread_t)pthread_self();
        poti->running = 1;
        PluginThreadControl *ptc = &poti->control;
        ptc->keep_running =1;
        sync_cond_init(&ptc->cond);
        sync_mutex_init(&ptc->mutex);
        pc->thread.own_threads_count = 1; // this plugin have only this client thread (b)locked...

        struct pollfd pfd;
        pfd.fd = ctx->socket_fd;
        pfd.events = POLLIN;
        int tick = 0;
        int prompt_row = 25; // default fallback
        int prompt_col = 1;
        int screen_rows = 25;
        int screen_cols = 80;
        int screen_refresh = 1;

        char line[256] = {0};
        size_t line_len = 0;
        size_t cursor_pos = 0;
        char history[10][256];
        for (int i=0; i<10; i++) *history[i]=0;
        int history_count = 0;
        int history_index = -1;

        enum { STATE_NORMAL, STATE_ESC, STATE_CSI };
        int ansi_state = STATE_NORMAL;
        char esc_buf[32] = {0};
        int esc_len = 0;

        while (ptc->keep_running) {
            int ret = poll(&pfd, 1, VT100_SLEEP_TIME_MS);
            if (ret > 0 && (pfd.revents & POLLIN)) {
                ssize_t n = read(ctx->socket_fd, buffer, sizeof(buffer) - 1);
                if (n > 0) {
                    // g_host->debugmsg("ret:%d n:%zu b0: 0x%02x", ret, n, buffer[0]);
                    for (ssize_t i = 0; i < n; ++i) {
                        char c = buffer[i];
                        switch (ansi_state) {
                            case STATE_NORMAL:
                                if ((unsigned char)c == 255) { // Telnet IAC
                                    // Handle 3-byte Telnet IAC command: IAC <cmd> <option>
                                    if (i + 2 < n) {
                                        //unsigned char cmd = (unsigned char)buffer[i + 1];
                                        //unsigned char opt = (unsigned char)buffer[i + 2];
                                        //g_host->debugmsg("TELNET IAC %02x %02x", cmd, opt);
                                        i += 2; // Skip command and option
                                        continue;
                                    } else {
                                        // Incomplete IAC sequence, ignore remaining
                                        break;
                                    }
                                }
                                if (c == '\x1b') {
                                    ansi_state = STATE_ESC;
                                    esc_buf[0] = c;
                                    esc_len = 1;
                                    break;
                                }
                                if (c == 3) { // Ctrl-C
                                    ptc->keep_running = 0;
                                    break;
                                }
                                if (c == '\r' || c == '\n') {
                                    dprintf(ctx->socket_fd, "\r\n");
                                    if (line_len > 0) {
                                        if (history_count < 10) {
                                            strcpy(history[history_count++], line);
                                        }
                                        history_index = history_count;
                                    }
                                    if (strcmp(line, "q") == 0 || strcmp(line, "quit") == 0) {
                                        ptc->keep_running = 0;
                                        break;
                                    }
                                    line[0] = '\0';
                                    line_len = 0;
                                    cursor_pos = 0;
                                    dprintf(ctx->socket_fd, "> ");
                                } else if (c == 127 || c == '\b') {
                                    if (cursor_pos > 0) {
                                        memmove(&line[cursor_pos - 1], &line[cursor_pos], line_len - cursor_pos);
                                        line_len--;
                                        cursor_pos--;
                                        line[line_len] = '\0';
                                    }
                                } else if (c == 1) {
                                    cursor_pos = 0;
                                } else if (c == 5) {
                                    cursor_pos = line_len;
                                } else if (isprint(c) && line_len < sizeof(line) - 1) {
                                    memmove(&line[cursor_pos + 1], &line[cursor_pos], line_len - cursor_pos);
                                    line[cursor_pos++] = c;
                                    line_len++;
                                    //g_host->debugmsg("llen:%zu cpos:%zu", line_len, cursor_pos);
                                }else{
                                    //g_host->debugmsg("miertvanitt");
                                }
                                break;

                            case STATE_ESC:
                                if (esc_len < (int)(sizeof(esc_buf) - 1)) {
                                    esc_buf[esc_len++] = c;
                                    esc_buf[esc_len] = '\0';
                                    if (c == '[') {
                                        ansi_state = STATE_CSI;
                                    } else {
                                        ansi_state = STATE_NORMAL;
                                        esc_len = 0;
                                    }
                                } else {
                                    ansi_state = STATE_NORMAL;
                                    esc_len = 0;
                                }
                                break;

                            case STATE_CSI:
                                if (esc_len < (int)(sizeof(esc_buf) - 1)) {
                                    esc_buf[esc_len++] = c;
                                    esc_buf[esc_len] = '\0';
                                }

                                // Wait until a valid final byte before processing
                                if (c >= '@' && c <= '~') {
                                    // Handle Delete key: "\x1b[3~" (ESC [ 3 ~)
                                    if ((strncmp(esc_buf, "\x1b[3~", 4) == 0 || (esc_buf[0] == '\x1b' && esc_buf[1] == '[' && esc_buf[2] == '3' && esc_buf[3] == '~')) && cursor_pos < line_len) {
                                        memmove(&line[cursor_pos], &line[cursor_pos + 1], line_len - cursor_pos);
                                        line_len--;
                                        line[line_len] = '\0';
                                    } else if (c == 'D' && cursor_pos > 0) cursor_pos--;
                                    else if (c == 'C' && cursor_pos < line_len) cursor_pos++;
                                    else if (c == 'A' && history_index > 0) {
                                        history_index--;
                                        strncpy(line, history[history_index], sizeof(line));
                                        line_len = strlen(line);
                                        cursor_pos = line_len;
                                        screen_refresh =1;
                                    } else if (c == 'B') {
                                        if (history_index < history_count - 1) {
                                            history_index++;
                                            strncpy(line, history[history_index], sizeof(line));
                                            line_len = strlen(line);
                                            cursor_pos = line_len;
                                        } else if (history_index == history_count - 1) {
                                            history_index++;
                                            line[0] = '\0';
                                            line_len = 0;
                                            cursor_pos = 0;
                                        }
                                        screen_refresh = 1;
                                    } else if (strncmp(esc_buf, "\x1b[8;", 4) == 0 && esc_buf[esc_len - 1] == 't') {
                                        int rows = 0, cols = 0;
                                        if (sscanf(esc_buf + 2, "8;%d;%dt", &rows, &cols) == 2) {
                                            if (rows != screen_rows || cols != screen_cols) {
                                                screen_rows = rows;
                                                screen_cols = cols;
                                                prompt_row = rows;
                                                screen_refresh = 1;
                                            }
                                        }
                                    }

                                    ansi_state = STATE_NORMAL;
                                    esc_len = 0;
                                } else if (esc_len >= (int)(sizeof(esc_buf) - 1)) {
                                    ansi_state = STATE_NORMAL;
                                    esc_len = 0;
                                }
                                break;
                        }
                    }
                    if (ansi_state == STATE_NORMAL) {
                        dprintf(ctx->socket_fd, "\x1b[%d;%dH\x1b[2K> %s ", prompt_row, prompt_col, line);
                        dprintf(ctx->socket_fd, "\x1b[%d;%zuH", prompt_row, prompt_col + 2 + cursor_pos);
                    }
                }
            } else if (ret == 0) {
                tick++;
                if (tick % 20 == 0) {
                    screen_refresh =1;
                }
                if (ansi_state == STATE_NORMAL) {
                    if (screen_refresh) {
                        dprintf(ctx->socket_fd, "\x1b[2J\x1b[HVT100 mode started. Press 'q' or 'quit' to exit. 'h' or 'help' for more info.\r\n");
                        dprintf(ctx->socket_fd, "\x1b[2;1HStatus: running %d s state: %d history: %d/%d line: %zu", tick / 20, ansi_state, history_index, history_count, line_len);
                        genStat(ctx->socket_fd);
                        dprintf(ctx->socket_fd, "\x1b[%d;%dH\x1b[2K> %s ", prompt_row, prompt_col, line);
                        dprintf(ctx->socket_fd, "\x1b[%d;%zuH", prompt_row, prompt_col + 2 + cursor_pos);
                        screen_refresh = 0;
                    }
                    if (tick %333 == 0) {
                        dprintf(ctx->socket_fd, "\x1b[18t"); // Request terminal size
                    }
                }
            } else {
                // error
                break;
            }
        }
        dprintf(ctx->socket_fd, "\x1b[12h");  // DECSET 12 – Enable local echo
        // Telnet protokoll beállítások visszavonása
        unsigned char telnet_restore[] = {
            255, 252, 1,  // WONT ECHO
            255, 252, 3,  // WONT SUPPRESS-GO-AHEAD
            255, 254, 1,  // DONT ECHO
            255, 254, 3   // DONT SUPPRESS-GO-AHEAD
        };
        send(ctx->socket_fd, telnet_restore, sizeof(telnet_restore), 0);
        sync_cond_destroy(ptc->cond);
        sync_mutex_destroy(ptc->mutex);
        ptc->cond=0;
        ptc->mutex=0;
        if (orig_flags != -1) {
            fcntl(ctx->socket_fd, F_SETFL, orig_flags);
        }
    }
    return;
}
int plugin_register(PluginContext *pc, const PluginHostInterface *host) {
    (void)pc;
    g_host = host;
    g_host->server.register_control_route(pc, g_control_routess_count, g_control_routes);
    return PLUGIN_SUCCESS;
}

int plugin_thread_init(PluginContext *ctx) {
    (void)ctx;
    // this will be run for each new connection
    return 0;
}

int plugin_thread_finish(PluginContext *ctx) {
    (void)ctx;
    // this will be run for each connection, when finished.
    return 0;
}
int plugin_init(PluginContext* pc, const PluginHostInterface *host) {
    (void)pc;
    g_host = host;
    pc->control.request_handler = pcontrol_handler;
    return PLUGIN_SUCCESS;
}
void plugin_finish(PluginContext* pc) {
    (void)pc;
    // Will runs once, when plugin unloaded.
    pc->http.request_handler = NULL;
}
// Plugin event handler implementation
int plugin_event(PluginContext *pc, PluginEventType event, const PluginEventContext* ctx) {
    (void)pc;
    (void)ctx;
    if (event == PLUGIN_EVENT_STANDBY) {

    }
    return 0;
}