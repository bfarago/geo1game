/**
 * File:    plugin_control.c
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-05-10
 * 
 * CONTROL (CLI) Protocol ANSI terminal VT220, VT100
 */
// --- Field formatting model header ---
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
#include <stdarg.h>

#include "sync.h"
#include "data_table.h"
#include "../plugin.h"
#include "cmd.h"


// globals

#define VT100_SLEEP_TIME_MS (50) // 50ms

const PluginHostInterface *g_host = NULL;

static const char* g_http_routes[2]={"/stat.html", "/stat.json"};
static const int g_http_routes_count = 2;
typedef enum {
    CMD_QUIT = 0,
    CMD_SET_DEBUG,
    CMD_STAT,
    CMD_CLEAR,
    CMD_MAXID
} PluginControlCmdId;

static const CommandEntry g_plugin_control_cmds[CMD_MAXID] ={
    [CMD_QUIT]      = {.path="quit",      .help="Quit from the cli.",                .arg_hint=""},
    //[CMD_RELEASE]   = {.path="release",   .help="Release all the plugins",           .arg_hint=""},
    //[CMD_RELOAD]    = {.path="reload",    .help="Reload the plugins",                .arg_hint=""},
    //[CMD_STOP]      = {.path="stop",      .help="Stops the server",                  .arg_hint=""},
    [CMD_SET_DEBUG] = {.path="set debug", .help="set global debug log level",        .arg_hint="On / OFF"},
    [CMD_STAT]      = {.path="stat",      .help="Statistics",                        .arg_hint=""},
    [CMD_CLEAR]     = {.path="clear",     .help="Clear Statistics",                  .arg_hint=""},
};


// --- Field formatting model definitions ---
typedef enum{
    FID_PS_Pid,
    FID_PS_Name,
    FID_PS_State,
    FID_PS_CpuLoad,
    FID_PS_Prio,
    FID_PS_VmRSS,
    FID_PS_VmLib,
    FID_PS_VmData,
    FID_PS_Switch,
    FID_PS_Voluntary,
    FID_PS_Involutary,
    FID_PS_MAXNUMBER
} ProcStatFieldId_t;

const FieldDescr g_fields_ProcStat[FID_PS_MAXNUMBER] = {
    [FID_PS_Pid]        = { .name = "PID",           .fmt = "%6d",     .width = 6,  .align_right = 1, .type = FIELD_TYPE_INT,    .precision = -1 },
    [FID_PS_Name]       = { .name = "Name",          .fmt = "%-16s",   .width = 16, .align_right = 0, .type = FIELD_TYPE_STRING, .precision = -1 },
    [FID_PS_State]      = { .name = "St.",           .fmt = "%c",      .width = 3,  .align_right = 0, .type = FIELD_TYPE_CHAR,   .precision = -1 },
    [FID_PS_CpuLoad]    = { .name = "CPU%",          .fmt = "%6.3f",   .width = 6,  .align_right = 1, .type = FIELD_TYPE_DOUBLE, .precision =  3 },
    [FID_PS_Prio]       = { .name = "Prio",          .fmt = "%4d",     .width = 4,  .align_right = 1, .type = FIELD_TYPE_INT,    .precision = -1 },
    [FID_PS_VmRSS]      = { .name = "VmRSS",         .fmt = "%7d",     .width = 7,  .align_right = 1, .type = FIELD_TYPE_INT,    .precision = -1 },
    [FID_PS_VmLib]      = { .name = "VmLib",         .fmt = "%7d",     .width = 7,  .align_right = 1, .type = FIELD_TYPE_INT,    .precision = -1 },
    [FID_PS_VmData]     = { .name = "VmData",        .fmt = "%7d",     .width = 7,  .align_right = 1, .type = FIELD_TYPE_INT,    .precision = -1 },
    [FID_PS_Switch]     = { .name = "Switch",        .fmt = "%6d",     .width = 6,  .align_right = 1, .type = FIELD_TYPE_INT,    .precision = -1 },
    [FID_PS_Voluntary]  = { .name = "Volun",         .fmt = "%6d",     .width = 6,  .align_right = 1, .type = FIELD_TYPE_INT,    .precision = -1 },
    [FID_PS_Involutary] = { .name = "Invol",         .fmt = "%6d",     .width = 6,  .align_right = 1, .type = FIELD_TYPE_INT,    .precision = -1 }
};
const TableDescr g_table_ProcStat = {
    .fields_count = FID_PS_MAXNUMBER,
    .fields = g_fields_ProcStat
};

typedef enum {
    FID_TS_Tid,
    FID_TS_Name,
    FID_TS_State,
    FID_TS_CpuLoad,
    FID_TS_Prio,
    // FID_TS_Stack,
    FID_TS_Switch,
    FID_TS_Voluntary,
    FID_TS_Involutary,
    FID_TS_MAXNUMBER
} ThreadStatFieldId_t;

const FieldDescr g_fields_ThreadStat[FID_TS_MAXNUMBER] = {
    [FID_TS_Tid]        = { .name = "TID",    .fmt = "%6d",     .width = 6,  .align_right = 1, .type = FIELD_TYPE_INT,    .precision = -1 },
    [FID_TS_Name]       = { .name = "Name",   .fmt = "%-16s",   .width = 16, .align_right = 0, .type = FIELD_TYPE_STRING, .precision = -1 },
    [FID_TS_State]      = { .name = "St.",    .fmt = "%c",      .width = 3,  .align_right = 0, .type = FIELD_TYPE_CHAR,   .precision = -1 },
    [FID_TS_CpuLoad]    = { .name = "CPU%",   .fmt = "%6.3f",   .width = 6,  .align_right = 1, .type = FIELD_TYPE_DOUBLE, .precision =  3 },
    [FID_TS_Prio]       = { .name = "Prio",   .fmt = "%4d",     .width = 4,  .align_right = 1, .type = FIELD_TYPE_INT,    .precision = -1 },
    //[FID_TS_Stack]      = { .name = "Stack",  .fmt = "0x%08x",  .width = 10, .align_right = 1, .type = FIELD_TYPE_INT,    .precision = -1 },
    [FID_TS_Switch]     = { .name = "Switch", .fmt = "%6d",     .width = 6,  .align_right = 1, .type = FIELD_TYPE_INT,    .precision = -1 },
    [FID_TS_Voluntary]  = { .name = "Volun",  .fmt = "%6d",     .width = 6,  .align_right = 1, .type = FIELD_TYPE_INT,    .precision = -1 },
    [FID_TS_Involutary] = { .name = "Invol",  .fmt = "%6d",     .width = 6,  .align_right = 1, .type = FIELD_TYPE_INT,    .precision = -1 }
};

const TableDescr g_table_ThreadStat = {
    .fields_count = FID_TS_MAXNUMBER,
    .fields = g_fields_ThreadStat
};

typedef enum {
    FID_PLG_Id,
    FID_PLG_Name,
    FID_PLG_State,
    FID_PLG_UseCount,
    FID_PLG_LastUsed,
    FID_PLG_Others,
    FID_PLG_MAXNUMBER
} PluginStatFieldId_t;

const FieldDescr g_fields_PluginStat[FID_PLG_MAXNUMBER] = {
    [FID_PLG_Id]        = { .name = "Id",      .fmt = "%2d",      .width = 2,  .align_right = 1, .type = FIELD_TYPE_INT,    .precision = -1 },
    [FID_PLG_Name]      = { .name = "Name",    .fmt = "%-16s",    .width = 16, .align_right = 0, .type = FIELD_TYPE_STRING, .precision = -1 },
    [FID_PLG_State]     = { .name = "State",   .fmt = "%-10s",    .width = 10, .align_right = 0, .type = FIELD_TYPE_STRING, .precision = -1 },
    [FID_PLG_UseCount]  = { .name = "Use",     .fmt = "%5d",      .width = 5,  .align_right = 1, .type = FIELD_TYPE_INT,    .precision = -1 },
    [FID_PLG_LastUsed]  = { .name = "Last",    .fmt = "%6s",      .width = 6,  .align_right = 1, .type = FIELD_TYPE_STRING, .precision = -1 },
    [FID_PLG_Others]    = { .name = "Det, others",    .fmt = "%-64s",    .width = MAX_FIELD_STRING_LEN, .align_right = 0, .type = FIELD_TYPE_STRING, .precision = -1 },
};

const TableDescr g_table_PluginStat = {
    .fields_count = FID_PLG_MAXNUMBER,
    .fields = g_fields_PluginStat
};

// --- End Field formatting model header ---


/**
 *  Process statistics dump from /proc/self/sched and /proc/self/status
 *  Enhanced: Parse starttime from stat, read /proc/uptime, compute per-runtime CPU usage using sum_exec_runtime.
 */
void stat_get_proc_results(const TableDescr *td, TableResults *res){
    unsigned long vmrss = 0, vmsize = 0, vmdata = 0, vmlib = 0;
    if (res->rows_count != 1){
        if (res->rows_count) table_results_free(res);
        table_results_alloc( &g_table_ProcStat, res, 1);
    }

    FILE *status = fopen("/proc/self/status", "r");
    if (status) {
        char line[256];
        while (fgets(line, sizeof(line), status)) {
            if (sscanf(line, "VmRSS: %lu kB", &vmrss) == 1) continue;
            if (sscanf(line, "VmSize: %lu kB", &vmsize) == 1) continue;
            if (sscanf(line, "VmData: %lu kB", &vmdata) == 1) continue;
            if (sscanf(line, "VmLib: %lu kB", &vmlib) == 1) continue;
        }
        fclose(status);
    }

    // Gather: PID, Name, State, Prio, starttime (from stat)
    int pid = getpid();
    char comm[256] = "";
    char state = '?';
    unsigned long priority = 0, starttime = 0;
    FILE *fpstat = fopen("/proc/self/stat", "r");
    if (fpstat) {
        int ret = fscanf(fpstat, "%d (%255[^)]) %c", &pid, comm, &state);
        if (ret != 3) {
            g_host->debugmsg("Warning: fscanf failed to read pid, comm, or state in stat_get_proc_results");
        }
        for (int i = 0; i < 15; i++) {
            ret = fscanf(fpstat, "%*s");
            // No error check needed for %*s
        }
        ret = fscanf(fpstat, "%lu", &priority);
        if (ret != 1) {
            g_host->debugmsg("Warning: fscanf failed to read priority in stat_get_proc_results");
        }
        // skip nice, num_threads, itrealvalue
        for (int i = 0; i < 3; i++) {
            ret = fscanf(fpstat, "%*s");
        }
        ret = fscanf(fpstat, "%lu", &starttime);
        if (ret != 1) {
            g_host->debugmsg("Warning: fscanf failed to read starttime in stat_get_proc_results");
        }
        if (ferror(fpstat) || feof(fpstat)) {
            g_host->debugmsg("Warning: fscanf failed or reached EOF in stat_get_proc_results");
        }
        fclose(fpstat);
    }

    // Read uptime from /proc/uptime
    double uptime = 0.0;
    FILE *uptime_fp = fopen("/proc/uptime", "r");
    if (uptime_fp) {
        int ret = fscanf(uptime_fp, "%lf", &uptime);
        if (ret != 1){
            g_host->errormsg("uptime = timebase calculations will be wrong...");
        }
        fclose(uptime_fp);
    }

    long clk_tck = sysconf(_SC_CLK_TCK);
    double proc_start_secs = (double)starttime / (double)clk_tck;
    double runtime = uptime - proc_start_secs;
    int runtime_is_zero = (runtime < 1e-6);

    // Parse /proc/self/sched for sum_exec_runtime, switches, prio
    double sum_exec_runtime = 0.0;
    unsigned long nr_switches = 0;
    unsigned long nr_voluntary_switches = 0;
    unsigned long nr_involuntary_switches = 0;
    unsigned long sched_prio = 0;
    FILE *fpsched = fopen("/proc/self/sched", "r");
    if (fpsched) {
        char line[256];
        while (fgets(line, sizeof(line), fpsched)) {
            if (sscanf(line, "se.sum_exec_runtime : %lf", &sum_exec_runtime) == 1) continue;
            if (sscanf(line, "nr_switches : %lu", &nr_switches) == 1) continue;
            if (sscanf(line, "nr_voluntary_switches : %lu", &nr_voluntary_switches) == 1) continue;
            if (sscanf(line, "nr_involuntary_switches : %lu", &nr_involuntary_switches) == 1) continue;
            if (sscanf(line, "prio : %lu", &sched_prio) == 1) continue;
        }
        fclose(fpsched);
    }
    // Use prio from sched if available
    if (sched_prio > 0) priority = sched_prio;

    double cpu_pct = 0.0;
    char cpu_pct_str[8] = "n/a";
    if (!runtime_is_zero && sum_exec_runtime > 0.0) {
        cpu_pct = (sum_exec_runtime / runtime) * 100.0;
        snprintf(cpu_pct_str, sizeof(cpu_pct_str), "%6.3f", cpu_pct);
    }
    FieldValue* row = table_row_get(td, res, 0);
    row[FID_PS_Pid].i = pid;
    table_field_set_str( &row[FID_PS_Name], comm);
    row[FID_PS_State].c = state;
    row[FID_PS_CpuLoad].d = cpu_pct;
    row[FID_PS_Prio].i = (int)priority;
    row[FID_PS_VmRSS].i = (int)vmrss;
    row[FID_PS_VmLib].i = (int)vmlib;
    row[FID_PS_VmData].i = (int)vmdata;
    row[FID_PS_Switch].i = (int)nr_switches;
    row[FID_PS_Voluntary].i = (int)nr_voluntary_switches;
    row[FID_PS_Involutary].i = (int)nr_involuntary_switches;
}

/**
 * Thread statistics from procfs, using /proc/[pid]/task/[tid]/sched for runtime and switches.
 */
void stat_get_thread_results(const TableDescr *td, TableResults *res){
    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "/proc/%d/task/", getpid());
    DIR *dir = opendir(path);
    if (!dir) {
        g_host->debugmsg("No dir ?");
        return;
    }
    size_t row_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_DIR) continue;
        if (!isdigit(entry->d_name[0])) continue;
        row_count++;
    }
    if (row_count != res->rows_count){
       table_results_free(res);
       table_results_alloc(td, res, row_count);
    }
    row_count = 0;
    closedir(dir);
    dir = opendir(path);
    if (!dir) {
        g_host->debugmsg("Reopen of thread task dir failed");
        return;
    }

    // Get process uptime for runtime calculation
    unsigned long proc_starttime = 0;
    long clk_tck = sysconf(_SC_CLK_TCK);
    double uptime = 0.0;
    FILE *uptime_fp = fopen("/proc/uptime", "r");
    if (uptime_fp) {
        int ret = fscanf(uptime_fp, "%lf", &uptime);
        if (ret != 1) {
            g_host->debugmsg("Warning: fscanf failed to read uptime in stat_get_thread_results");
        }
        fclose(uptime_fp);
    }
    // Get process starttime from stat
    FILE *fpstat = fopen("/proc/self/stat", "r");
    if (fpstat) {
        int dummy;
        char dummy_comm[256], dummy_state;
        int ret = fscanf(fpstat, "%d (%255[^)]) %c", &dummy, dummy_comm, &dummy_state);
        if (ret != 3) {
            g_host->debugmsg("Warning: fscanf failed to read pid, comm, or state in stat_get_thread_results");
        }
        for (int i = 0; i < 21; i++) {
            ret = fscanf(fpstat, "%*s");
        }
        ret = fscanf(fpstat, "%lu", &proc_starttime);
        if (ret != 1) {
            g_host->debugmsg("Warning: fscanf failed to read proc_starttime in stat_get_thread_results");
        }
        fclose(fpstat);
    }
    double proc_start_secs = (double)proc_starttime / (double)clk_tck;
    double proc_runtime = uptime - proc_start_secs;
    if (proc_runtime < 1e-6) proc_runtime = 0.0;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_DIR) continue;
        if (!isdigit(entry->d_name[0])) continue;

        FieldValue * row = table_row_get(td, res, row_count++);
        if (!row) continue; // new thread added between these lines...
        char stat_path[MAX_PATH], sched_path[MAX_PATH];
        snprintf(stat_path, MAX_PATH, "/proc/self/task/%s/stat", entry->d_name);
        snprintf(sched_path, MAX_PATH, "/proc/self/task/%s/sched", entry->d_name);

        char comm[256] = "";
        char state = '?';
        unsigned long priority = 0, starttime = 0, kstkesp = 0;
        // Read stat for name, state, prio, starttime, kstkesp
        FILE *fp = fopen(stat_path, "r");
        if (fp) {
            int tid;
            int ret = fscanf(fp, "%d (%255[^)]) %c", &tid, comm, &state);
            if (ret != 3) {
                g_host->debugmsg("Warning: fscanf failed to read tid, comm, or state in stat_get_thread_results");
            }
            for (int i = 0; i < 15; i++) {
                ret = fscanf(fp, "%*s");
            }
            ret = fscanf(fp, "%lu", &priority);
            if (ret != 1) {
                g_host->debugmsg("Warning: fscanf failed to read priority in stat_get_thread_results");
            }
            for (int i = 0; i < 3; i++) {
                ret = fscanf(fp, "%*s");
            }
            ret = fscanf(fp, "%lu", &starttime);
            if (ret != 1) {
                g_host->debugmsg("Warning: fscanf failed to read starttime in stat_get_thread_results");
            }
            for (int i = 0; i < 6; i++) {
                ret = fscanf(fp, "%*s");
            }
            ret = fscanf(fp, "%lu", &kstkesp);
            if (ret != 1) {
                g_host->debugmsg("Warning: fscanf failed to read kstkesp in stat_get_thread_results");
            }
            fclose(fp);
        }

        // Parse sched for sum_exec_runtime, switches, prio
        double sum_exec_runtime = 0.0;
        unsigned long nr_switches = 0;
        unsigned long nr_voluntary_switches = 0;
        unsigned long nr_involuntary_switches = 0;
        unsigned long sched_prio = 0;
        FILE *fpsched = fopen(sched_path, "r");
        if (fpsched) {
            char line[256];
            while (fgets(line, sizeof(line), fpsched)) {
                if (sscanf(line, "se.sum_exec_runtime : %lf", &sum_exec_runtime) == 1) continue;
                if (sscanf(line, "nr_switches : %lu", &nr_switches) == 1) continue;
                if (sscanf(line, "nr_voluntary_switches : %lu", &nr_voluntary_switches) == 1) continue;
                if (sscanf(line, "nr_involuntary_switches : %lu", &nr_involuntary_switches) == 1) continue;
                if (sscanf(line, "prio : %lu", &sched_prio) == 1) continue;
            }
            fclose(fpsched);
        }
        if (sched_prio > 0) priority = sched_prio;

        // Compute per-thread runtime (uptime - thread starttime)
        double thread_start_secs = (double)starttime / (double)clk_tck;
        double thread_runtime = uptime - thread_start_secs;
        if (thread_runtime < 1e-6) thread_runtime = 0.0;
        double cpu_pct = 0.0;
        if (thread_runtime > 0.0 && sum_exec_runtime > 0.0) {
            cpu_pct = (sum_exec_runtime / thread_runtime) * 100.0;
        }
        //table_field_set_str(&row[FID_TS_Tid], entry->d_name);
        row[FID_TS_Tid].i = (int)atoi(entry->d_name);
        table_field_set_str(&row[FID_TS_Name], comm);
        row[FID_TS_State].c = state;
        row[FID_TS_CpuLoad].d = cpu_pct;
        row[FID_TS_Prio].i = priority;
        // row[FID_TS_Stack].i = (int)kstkesp; // useless
        row[FID_TS_Switch].i = (int)nr_switches;
        row[FID_TS_Voluntary].i = (int)nr_voluntary_switches;
        row[FID_TS_Involutary].i = (int)nr_involuntary_switches;
    }
    closedir(dir);
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

/**
 * Plugin statistics
 */
void stat_get_plugins_results(const TableDescr *td, TableResults *tr){
    
    unsigned long now = time(NULL);
    int pn = g_host->server.get_plugin_count();
    size_t row_count=0;
    for (int i= 0; i< pn; i++){
        PluginContext *p= g_host->server.get_plugin(i);
        if (p->state == PLUGIN_STATE_DISABLED) continue;
        row_count++;
    }
    if (row_count != tr->rows_count){
        table_results_free(tr);
        table_results_alloc( td, tr, row_count);
    }

    row_count =0;
    for (int i=0 ; i<pn; i++){
        PluginContext *p= g_host->server.get_plugin(i);
        if (p->state == PLUGIN_STATE_DISABLED) continue;
        FieldValue *row= table_row_get( td, tr, row_count++);
        row[FID_PLG_Id].i = i;
        table_field_set_str( &row[FID_PLG_Name], p->name);
        table_field_set_str( &row[FID_PLG_State], g_state_labels[p->state]);
        row[FID_PLG_UseCount].i = p->used_count;

        char tmp[16];
        if (p->last_used){
            int dif = now - p->last_used;
            if (dif<120){
                snprintf(tmp, sizeof(tmp), "%4d s", dif );
            }else if (dif<7200){
                snprintf(tmp, sizeof(tmp), "%4d m", dif/60 );
            }else{
                snprintf(tmp, sizeof(tmp), "%4d h", dif/3600 );
            }
        }else{
            snprintf(tmp, sizeof(tmp), "n/a");
        }
        //row[FID_PLG_LastUsed].i = dif; // of we have a converter function callback it would be better
        table_field_set_str( &row[FID_PLG_LastUsed], tmp);

        char dmp[MAX_FIELD_STRING_LEN];
        size_t dlen=MAX_FIELD_STRING_LEN-1;
        size_t od=0;
        if (p->stat.det_str_dump){
            od += p->stat.det_str_dump(dmp+od, dlen - od);
        }else{
            od += snprintf(dmp + od, dlen - od, "n/a");
        }
        if (p->stat.stat_str_dump){
            od += p->stat.stat_str_dump(dmp+od, dlen - od);
        }
        for (int i=0; i< p->thread.own_threads_count; i++){
            PluginOwnThreadInfo *pi = &p->thread.own_threads[i];
            od += snprintf(dmp + od, dlen - od, " %d:%s %d/%d %p", i, pi->name, pi->running, pi->control.keep_running, pi->thread);
        }
        if (p->handle){
            od += snprintf(dmp + od, dlen - od, " 0x%08lx",(unsigned long) p->handle);
        }
        table_field_set_str( &row[FID_PLG_Others], dmp);
    }
}

TableResults g_results_ProcStat = {.fields = NULL, .rows_count=0};
TableResults g_results_ThreadStat = {.fields = NULL, .rows_count=0};
TableResults g_results_PluginsStat = {.fields = NULL, .rows_count=0};
const TableDescr *gp_table_ServerStat = NULL;
TableResults *gp_results_ServerStat = NULL;
/**
 * Generate statistics
 */
void genStat_ProcFs(){
    stat_get_proc_results(&g_table_ProcStat, &g_results_ProcStat);
    stat_get_thread_results(&g_table_ThreadStat, &g_results_ThreadStat);
}
void genStat_Plugins(){
    stat_get_plugins_results(&g_table_PluginStat, &g_results_PluginsStat);
    g_host->server.server_stat_table(&gp_table_ServerStat, &gp_results_ServerStat);
}

/**
 * Textual dump from prviously calculated datas
 */
size_t stat_text_gen(TextContext *tc, char* buf, size_t len){
    size_t o = 0; 
    tc->title = "Process statistics"; tc->id = "proc"; tc->flags=1;
    o += table_gen_text( &g_table_ProcStat, &g_results_ProcStat, buf + o, len - o, tc);
    tc->title = "Thread statistics"; tc->id = "threads"; tc->flags=2;
    o += table_gen_text( &g_table_ThreadStat, &g_results_ThreadStat, buf + o, len - o, tc);
    tc->title = "Plugin statistics"; tc->id = "plugins"; tc->flags=2;
    o += table_gen_text( &g_table_PluginStat, &g_results_PluginsStat, buf + o, len - o, tc);
    tc->title = "Server statistics"; tc->id = "servers"; tc->flags=2|4;
    o += table_gen_text( gp_table_ServerStat, gp_results_ServerStat, buf + o, len - o, tc);
    return o;
}

void dumpStat(int fd){
    (void)fd;
    char buf[BUF_SIZE];
    size_t o=0;
    TextContext tc;
    tc.format = TEXT_FMT_TEXT;
    o+=stat_text_gen(&tc, buf, sizeof(buf));
    dprintf(fd, "\r\n%s", buf);

    g_host->server.server_det_str_dump(buf, sizeof(buf));
    dprintf(fd, "Host Det:%s\r\n", buf);

}

/**
 * Clears statistics
 */
void stat_clear(){
    int pn = g_host->server.get_plugin_count();
    for (int i=0 ; i<pn; i++){
        PluginContext *p= g_host->server.get_plugin(i);
        if (p->stat.stat_clear) p->stat.stat_clear();
    }
    g_host->server.server_stat_clear();
}

/** 
 * command line handler for CLI protocol
 */
static int plugin_control_execute_command(PluginContext *pc, ClientContext *ctx, CommandEntry *pe, char* cmd)
{
    (void)ctx;
    (void)cmd;

    PluginContext *targetPc= pe->pc;
    if (!targetPc){
        // host implemented command handler
        return g_host->server.execute_commands(ctx, pe, cmd);
    }else{
        // a target plugin implemented command handler
        if (pc->id == targetPc->id){
            //the same plugin, surly no need to start the plugin
            switch (pe->handlerid)
            {
            case CMD_QUIT: return -3; break;
            case CMD_CLEAR: stat_clear(); break;
            case CMD_SET_DEBUG: return 0; break;
            case CMD_STAT:
                return -1;
            }
        }else{
            //another plugin
            if (g_host->start( targetPc->id)){
                targetPc->control.execute_command(targetPc, ctx, pe, cmd);
                g_host->stop(targetPc->id);
            }
        }
    }

    /*
    if (strcmp(line, "q") == 0 || strcmp(line, "quit") == 0) {
        return -3;
    } else if (strcmp(line, "c") == 0 || strcmp(line, "clear") == 0) {
        stat_clear();
        return 0;
    } 
    */
    return -2;
}

/**
 * Control protocol (CLI) handler
 */
void pcontrol_handler(PluginContext *pc, ClientContext *ctx, char* cmd, int argc, char **argv)
{
    (void)pc;
    (void)argc;
    (void)argv;
    (void)cmd;
/*
    if (strcasecmp(cmd, "help") == 0) {
        dprintf(ctx->socket_fd, "HELP:\nThis is the command line interface.\nAvailable commands:\n- help\n- vt100\n");
    } else if (strcasecmp(cmd, "vt100") == 0) */
    {
        genStat_ProcFs();
        genStat_Plugins();

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
            255, 251, 1,   // IAC WILL ECHO
            255, 251, 3,   // IAC WILL SUPPRESS-GO-AHEAD
            255, 253, 1,   // IAC DO ECHO
            255, 253, 3    // IAC DO SUPPRESS-GO-AHEAD
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
                                    // dprintf(ctx->socket_fd, "\r\n");
                                    if (line_len > 0) {
                                        if (history_count < 10) {
                                            strcpy(history[history_count++], line);
                                        }
                                        history_index = history_count;
                                    }
                                    if (strlen(line) > 0) {
                                        int found = g_host->server.cmd_search(line);
                                        if (found >= 0) {
                                            CommandEntry *pe;
                                            if (!g_host->server.cmd_get(found, &pe)) {
                                                g_host->debugmsg("Found command: %s\r\n", pe->path);
                                                int ret= plugin_control_execute_command(pc, ctx, pe, line);
                                                if (-3 ==ret) ptc->keep_running = 0;
                                            }
                                        } else {
                                            g_host->debugmsg("Unknown command: %s\r\n", line);
                                        }
                                    }
                                    screen_refresh = 1;
                                    line[0] = '\0';
                                    line_len = 0;
                                    cursor_pos = 0;
                                    dprintf(ctx->socket_fd, "> ");
                                    pc->last_used = time(0);
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
                                } else if (c == '\t') {
                                    int match_count = 0;
                                    int last_match = -1;
                                    for (size_t k = 0; k < g_host->server.cmd_get_count(); k++) {
                                        CommandEntry *pe = NULL;
                                        if (g_host->server.cmd_get(k, &pe) == 0 && pe && pe->path &&
                                            strncmp(pe->path, line, strlen(line)) == 0) {
                                            match_count++;
                                            last_match = (int)k;
                                        }
                                    }

                                    if (match_count == 1 && last_match >= 0) {
                                        CommandEntry *pe = NULL;
                                        if (!g_host->server.cmd_get((size_t)last_match, &pe)) {
                                            size_t match_len = strlen(pe->path);
                                            if (match_len < sizeof(line)) {
                                                strcpy(line, pe->path);
                                                line_len = strlen(line);
                                                cursor_pos = line_len;
                                                screen_refresh = 1;
                                            }
                                        }
                                    } else if (match_count > 1) {
                                        screen_refresh = 1;
                                    }
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
                if (tick % 40 == 0) {
                    genStat_ProcFs();
                    screen_refresh =1;
                }
                if (tick % 20 == 0) {
                    genStat_Plugins();
                    screen_refresh =1;
                }
                if (ansi_state == STATE_NORMAL) {
                if (screen_refresh) {
                        dprintf(ctx->socket_fd, "\x1b[2J\x1b[HVT100 mode started. Press 'q' or 'quit' to exit. 'h' or 'help' for more info.\r\n");
                        dprintf(ctx->socket_fd, "\x1b[2;1HStatus: running %d s state: %d history: %d/%d line: %zu", tick / 20, ansi_state, history_index, history_count, line_len);
                        dumpStat(ctx->socket_fd);
                        // Print matching suggestions above prompt
                        dprintf(ctx->socket_fd, "\x1b[%d;1H\x1b[2K", prompt_row - 1); // move cursor up and clear line
                        dprintf(ctx->socket_fd, "Suggestions:");
                        for (size_t k = 0; k < g_host->server.cmd_get_count(); k++) {
                            CommandEntry *pe = NULL;
                            if (g_host->server.cmd_get(k, &pe) == 0 && pe && pe->path &&
                                strncmp(pe->path, line, strlen(line)) == 0) {
                                dprintf(ctx->socket_fd, " '%s'", pe->path);
                            }
                        }
                        dprintf(ctx->socket_fd, "\r\n");
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

        table_results_free(&g_results_ProcStat);
        table_results_free(&g_results_ThreadStat);
        table_results_free(&g_results_PluginsStat);

        dprintf(ctx->socket_fd, "\x1b[12h");  // DECSET 12 – Enable local echo
        // Telnet protokoll reset to echo mode
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

void http_html_handler(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc;
    (void)params;
    char buf[BUF_SIZE];
    size_t len = BUF_SIZE -1;
    size_t o = 0; // offset
    o += snprintf(buf + o, len - o, "<html><head>\n");
    o += snprintf(buf + o, len - o, "<style>\n");
    o += snprintf(buf + o, len - o, "body { background-color: #000; color: #ddd; font-family: monospace, sans-serif; font-size: 14px; }\n");
    o += snprintf(buf + o, len - o, "table { border-collapse: collapse; margin: 1em 0; width: 100%%; }\n");
    o += snprintf(buf + o, len - o, "th, td { border: 1px solid #444; padding: 4px 8px; }\n");
    o += snprintf(buf + o, len - o, "tr.head { background-color: #444; color: #fff; font-weight: bold; }\n");
    o += snprintf(buf + o, len - o, "tr.roweven { background-color: #111; color: #ccc; }\n");
    o += snprintf(buf + o, len - o, "tr.rowodd { background-color: #222; color: #ccc; }\n");
    o += snprintf(buf + o, len - o, "td.cellleft { text-align: left; }\n");
    o += snprintf(buf + o, len - o, "td.cellright { text-align: right; }\n");
    o += snprintf(buf + o, len - o, "</style>\n");
    o += snprintf(buf + o, len - o, "</head><body>\n");
    genStat_ProcFs();
    genStat_Plugins();
    TextContext tc;
    tc.format= TEXT_FMT_HTML;
    o += stat_text_gen(&tc, buf+o, len-o);
    o += snprintf(buf + o, len - o, "\n</body></html>\n");
    g_host->http.send_response(ctx->socket_fd, 200, "text/html", buf);
}
void http_json_handler(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc;
    (void)params;
    char buf[BUF_SIZE];
    size_t len = BUF_SIZE -1;
    size_t o = 0;
    genStat_ProcFs();
    genStat_Plugins();
    TextContext tc;
    tc.format = TEXT_FMT_JSON_OBJECTS;
    o += stat_text_gen(&tc, buf+o, len-o);
    g_host->http.send_response(ctx->socket_fd, 200, "application/json", buf);
}
static void (* const g_http_handlers[2])(PluginContext *, ClientContext *, RequestParams *) = {
    http_html_handler, http_json_handler
};
void http_handler(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    for (int i = 0; i < g_http_routes_count; i++) {
        if (strcmp(g_http_routes[i], params->path) == 0) {
            g_http_handlers[i](pc, ctx, params);
            return;
        }
    }
    const char *body = "{\"msg\":\"Unknown sub path\"}";
    g_host->http.send_response(ctx->socket_fd, 200, "application/json", body);
    g_host->logmsg("%s mysql request", ctx->client_ip);
}

int plugin_register(PluginContext *pc, const PluginHostInterface *host) {
    (void)pc;
    g_host = host;
    g_host->server.register_commands(pc, g_plugin_control_cmds, sizeof(g_plugin_control_cmds)/sizeof(CommandEntry));
    g_host->server.register_http_route(pc, g_http_routes_count, g_http_routes);
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
    // control protocol
    pc->control.request_handler = pcontrol_handler; // communication layer of the CLI
    pc->control.execute_command = plugin_control_execute_command; // command execution layer
    // http protocol
    pc->http.request_handler= http_handler;
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
