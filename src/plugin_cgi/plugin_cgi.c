/**
 * File:    plugin_cgi.c
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-05-02
 * 
 * CGI Client plugin
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

#include "../plugin.h"

// globals
const PluginHostInterface *g_host;
#define MAX_CGI_PIDS (32)
#define INITIAL_MAX_RESULT_BUF_LENGHT (16*1024) // 16kbyte, then realloc
#define BUF_LENGHT_INCREMENT (16*1024) // 16kbyte, then realloc
#define ABSOLUTE_MAX_RESULT_BUF_LENGTH (32*1024*1024) //32MByte, then error

// forward declaration
void handle_cgi_testphp(PluginContext *pc, ClientContext *ctx, RequestParams *params);
int plugin_event(PluginContext *pc, PluginEventType event, const PluginEventContext* ctx);

// config
static void (*plugin_http_functions[])(PluginContext *, ClientContext *, RequestParams *) = {
    handle_cgi_testphp
};
const char* plugin_http_routes[]={"/test.php"};
int plugin_http_routes_count = 1;

typedef enum {
    CGI_STATE_FREE = 0,
    CGI_STATE_ALLOCATED,
    CGI_STATE_RUNNING,
    CGI_STATE_DONE
} cgi_state_t;

typedef struct cgi_data_t {
    int active_pid;
    int pool_index;  // Index in the global pool
    int stdout_pipe[2], stderr_pipe[2];
    char *script_name;
    char *php_path;
    char *script_dir;
    char *php_params;
    char *argv[10]; // Max 10 arguments total
    int argc;
    PluginContext *pc;
    ClientContext *ctx;
    RequestParams *params;
    unsigned char debug_enabled;
    struct timespec start_time;
    struct timespec end_time;
    char query_signature[MAX_PATH]; // for later hashing or caching
    CacheFile cache_file;
    char cache_dir[MAX_PATH];   // could be different from the script dir    char cache_filename[MAX_PATH]; // for later caching
    char *result;
    size_t result_length; //last written pos +1 for the zero terminator.
    size_t result_maxlength; // buffer size
    cgi_state_t state;
}cgi_data_t;

static cgi_data_t g_cgi_pool[MAX_CGI_PIDS];
static pthread_mutex_t cgi_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

int configstr(const char* group, const char* key,
        const char* default_value, char** pstr,
        char* buf,size_t bufsize)
{
    if (!pstr) return -1;
    if (*pstr){
        //the storage is not a null ptr... Hm try the free it.
        free(*pstr); *pstr=NULL;
    }
    g_host->config_get_string(group, key, buf, bufsize, default_value);
    char *str = strdup(buf);
    if (!str) {
        g_host->errormsg("Memmory allocation failed during config string manipulation.");
        return -2;
    }
    *pstr = str;
    return 0;
}
int cgi_data_init(cgi_data_t* p, PluginContext *pc, ClientContext *ctx, RequestParams *params){
    int res=0;
    char tmp[MAX_PATH];
    p->pc = pc;
    p->ctx = ctx;
    p->params = params;
    p->debug_enabled = g_host->config_get_int("CGI", "debug", 1);
    res |= configstr("CGI", "script_name","test.php", &p->script_name, tmp, MAX_PATH);  
    res |= configstr("CGI", "script_dir","../www", &p->script_dir, tmp, MAX_PATH);  
    res |= configstr("CGI", "php_path","/usr/bin/php", &p->php_path, tmp, MAX_PATH);  
    res |= configstr("CGI", "php_params","-c /etc/php5/apache2", &p->php_params, tmp, MAX_PATH);  
   
    if (res){   
        if (p->debug_enabled){
            if (p->script_name == NULL){
                g_host->debugmsg("Failed to allocate memory for script_name in plugin CGI");
                res=-1;
            }
            if (p->php_path == NULL){
                g_host->debugmsg("Failed to allocate memory for php_path in plugin CGI");
                res=-2;
            }
            if (p->php_path == NULL){
                g_host->debugmsg("Failed to allocate memory for php_path in plugin CGI");
                res=-3;
            }
            if (p->script_dir == NULL){
                g_host->debugmsg("Failed to allocate memory for php_path in plugin CGI");
                res=-4;
            }
        }
    }   
    for(int i=0; i<2; i++){
        // set to invalid value
        p->stdout_pipe[i] = -1;
        p->stderr_pipe[i] = -1;
    }
    // temporary output buffer
    p->result = NULL;
    p->result_length = 0;
    p->result_maxlength = 0;
    if (0 == res) {
        size_t initial_size = INITIAL_MAX_RESULT_BUF_LENGHT;
        p->result = malloc(initial_size);
        if (!p->result) {
            g_host->debugmsg("Failed to allocate memory for result in plugin CGI");
            res = -5;
        } else {
            p->result_maxlength = initial_size - 1; // minus 1 for 0x00
        }
    }
    return res;
}
void cgi_data_free(cgi_data_t* p){
    if (p->php_params) {
        free(p->php_params);
        p->php_params = NULL;
    }
    if (p->php_path){
        free(p->php_path);
        p->php_path = NULL;
    }
    if (p->script_dir) {
        free(p->script_dir);
        p->script_dir = NULL;
    }
    if (p->script_name){
        free(p->script_name);
        p->script_name = NULL;
    }
    if (p->result){
        free(p->result); // get a rid of the 1Mbyte allocation, phuuuh
        p->result = NULL;
        p->result_length = 0;
        p->result_maxlength = 0;
    }
}
// close pipes after cgi process is done, if those are still open
void cgi_data_close_pipe(cgi_data_t* p, int pipe_index, unsigned char which){
    if ( which & STDOUT_FILENO){
        if (p->stdout_pipe[pipe_index] != -1){
            int res = close(p->stdout_pipe[pipe_index]);
            if (res){
                g_host->debugmsg("Failed to close stdout[%d] pipe in plugin CGI, due to: %s.", pipe_index, strerror(errno));
            }else{
                p->stdout_pipe[pipe_index] = -1; // bookmark that it was closed
            }
        }else{
            // g_host->debugmsg("Pipe was already closed stdout[%d] in plugin CGI", pipe_index);
        }
    }
    if (which & STDERR_FILENO){
        if (p->stderr_pipe[pipe_index] != -1){
            int res = close(p->stderr_pipe[pipe_index]);
            if (res){
                g_host->debugmsg("Failed to close stderr[%d] pipe in plugin CGI, due to: %s.", pipe_index, strerror(errno));
            }else{
                p->stderr_pipe[pipe_index] = -1; // bookmark that it was closed
            }
        }else{
            // g_host->debugmsg("Pipe was already closed stderr[%d] in plugin CGI", pipe_index);
        }
    }
}

void cgi_data_close_pipes(cgi_data_t* p){
    for (int i=0; i<2; i++){
        cgi_data_close_pipe(p, i, STDERR_FILENO | STDOUT_FILENO);
    }
}
cgi_data_t* cgi_data_allocate() {
    pthread_mutex_lock(&cgi_pool_mutex);
    for (int i = 0; i < MAX_CGI_PIDS; i++) {
        if (g_cgi_pool[i].active_pid == 0) {
            g_cgi_pool[i].active_pid = -1; // lefoglalva
            g_cgi_pool[i].pool_index = i;
            g_cgi_pool[i].state = CGI_STATE_ALLOCATED;
            pthread_mutex_unlock(&cgi_pool_mutex);
            g_host->debugmsg("CGI data allocated at index %d from the pool.", i);
            return &g_cgi_pool[i];
        }
    }
    pthread_mutex_unlock(&cgi_pool_mutex);
    g_host->errormsg("CGI pool is full, no more CGI processes available");
    return NULL;
}

void cgi_data_release(cgi_data_t* p) {
    if (!p) return;
    cgi_data_close_pipes(p);
    g_host->debugmsg("CGI data released at index:%d, pid:%d from the pool.", p->pool_index, p->active_pid);
    pthread_mutex_lock(&cgi_pool_mutex);
    p->state = CGI_STATE_FREE;
    p->active_pid = 0;
    pthread_mutex_unlock(&cgi_pool_mutex);
    cgi_data_free(p);
}
int cgi_data_prepare_query(cgi_data_t* p, PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    if (!p || !pc || !ctx || !params) return -1;
    p->pc = pc;
    p->ctx = ctx;
    p->params = params;
    clock_gettime(CLOCK_MONOTONIC, &p->start_time);

    if (p->debug_enabled) {
        for(int i = 0; i < ctx->request.header_count; i++) {
            g_host->logmsg("H %s: %s", ctx->request.headers[i].key, ctx->request.headers[i].value);
        }
        for(int i = 0; i < ctx->request.query_count; i++) {
            g_host->logmsg("Q %s: %s", ctx->request.query[i].key, ctx->request.query[i].value);
        }
        g_host->logmsg("I session_id: %s", ctx->request.session_id);
    }

    snprintf(p->query_signature, sizeof(p->query_signature), "%s_", p->script_name);
    for (int i = 0; i < ctx->request.query_count; i++) {
        strncat(p->query_signature, ctx->request.query[i].key, sizeof(p->query_signature) - strlen(p->query_signature) - 1);
        strncat(p->query_signature, "_", sizeof(p->query_signature) - strlen(p->query_signature) - 1);
        strncat(p->query_signature, ctx->request.query[i].value, sizeof(p->query_signature) - strlen(p->query_signature) - 1);
        if (i < ctx->request.query_count - 1) {
            strncat(p->query_signature, "_", sizeof(p->query_signature) - strlen(p->query_signature) - 1);
        }
    }
    
    g_host->cache.file_init(&p->cache_file, p->query_signature);
    g_host->debugmsg("CGI cache file: %s", p->cache_file.path);
    return 0;
}

void handle_cgi_testphp(PluginContext *pc, ClientContext *ctx, RequestParams *params){
    (void)pc;
    (void)params;
    cgi_data_t* cgi = cgi_data_allocate();
    ctx->result_status = CTX_RUNNING;
    if (!cgi || cgi_data_init(cgi, pc, ctx, params)) {
        g_host->errormsg("Failed to allocate/init CGI data");
        g_host->http.send_response(ctx->socket_fd, 503, "text/plain", "CGI unavailable\n");
        ctx->result_status = CTX_ERROR;
        return;
    }
    if (cgi_data_prepare_query(cgi, pc, ctx, params)) {
        g_host->errormsg("Failed to prepare CGI query data");
        g_host->http.send_response(ctx->socket_fd, 503, "text/plain", "CGI prep failed\n");
        cgi_data_release(cgi);
        ctx->result_status = CTX_ERROR;
        return;
    }
    
    if (g_host->cache.file_exists_recent(&cgi->cache_file)){
        g_host->http.send_file(ctx->socket_fd, "text/plain", cgi->cache_file.path);
        g_host->logmsg("CGI sent a cached file. index:%d, pid:%d, file:%s",
                 cgi->pool_index, cgi->active_pid, cgi->cache_file.path);
        cgi_data_release(cgi);
        ctx->result_status = CTX_FINISHED_OK;
        return;
    }

    
    g_host->debugmsg("CGI handler invoked for %s", cgi->script_name );
    if (pipe(cgi->stdout_pipe) == -1 || pipe(cgi->stderr_pipe) == -1) {
        g_host->errormsg("Failed to create pipes for test.php");
        g_host->http.send_response(ctx->socket_fd, 500, "text/plain", "Pipe failed\n");
        // Do not call cgi_data_free here; cgi_data_release already does cleanup
        cgi_data_release(cgi);
        ctx->result_status = CTX_ERROR;
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        g_host->errormsg("Fork failed for test.php");
        // Do not call cgi_data_free here; cgi_data_release already does cleanup
        cgi_data_release(cgi);
        ctx->result_status = CTX_ERROR;
        return;
    }

    if (pid == 0) {
        // Child
        cgi->state = CGI_STATE_RUNNING;
        // Child
        cgi_data_close_pipe(cgi, 0, STDOUT_FILENO | STDERR_FILENO);
        dup2(cgi->stdout_pipe[1], STDOUT_FILENO);
        dup2(cgi->stderr_pipe[1], STDERR_FILENO);
        cgi_data_close_pipe(cgi, 1, STDOUT_FILENO | STDERR_FILENO);
        
        g_host->logmsg(
            "cd '%s'; %s %s %s",
            cgi->script_dir, cgi->php_path, cgi->php_params, cgi->script_name);

        chdir(cgi->script_dir);

        // Prepare environment
        size_t ofs = 0;
        
#ifdef CGI_UNUSED_CODE
        const size_t maxstr = BUF_SIZE-1;
        char str[BUF_SIZE];
// Keep this code around for reference only
        snprintf(str, maxstr, "SESSION_ID=%s", ctx->request.session_id);
        putenv(str);
        snprintf(str, maxstr, "PHPSESSID=%s", ctx->request.session_id);
        putenv(str);
        snprintf(str, maxstr, "SCRIPT_NAME=/%s", cgi->script_name);
        putenv(str);
        snprintf(str, maxstr, "SCRIPT_PATH=%s", params->path);
        putenv(str);
        putenv("REQUEST_METHOD=GET");
        putenv("GATEWAY_INTERFACE=CGI/1.1");
        putenv("SERVER_PROTOCOL=HTTP/1.1");
        putenv("SERVER_PORT=80");

        ofs+=snprintf(str+ofs, maxstr - ofs, "QUERY_STRING=");
        for (int i = 0; i < ctx->request.query_count; i++) {
            if (i > 0) str[ofs++] = '&';
            ofs+=snprintf(str+ofs, maxstr - ofs, "%s=%s", ctx->request.query[i].key, ctx->request.query[i].value);
        }
        putenv(str);
        
        for (int i = 0; i < ctx->request.header_count; i++) {
            const char* envkey= NULL;
            const char* headerkey = ctx->request.headers[i].key;
            const char* headervalue = ctx->request.headers[i].value;
            if (strcasecmp(headerkey, "User-Agent") == 0)           envkey = "HTTP_USER_AGENT";
            else if (strcasecmp(headerkey, "X-Forwarded-For") == 0) envkey = "REMOTE_ADDR";
            else if (strcasecmp(headerkey, "X-Forwarded-Host") == 0)envkey = "SERVER_NAME";
            else if (strcasecmp(headerkey, "accept-language") == 0) envkey = "HTTP_ACCEPT_LANGUAGE";
            else if (strcasecmp(headerkey, "Accept") == 0)          envkey = "HTTP_ACCEPT";
            else if (strcasecmp(headerkey, "Accept-Language") == 0) envkey = "HTTP_ACCEPT_LANGUAGE";
            else if (strcasecmp(headerkey, "Accept-Encoding") == 0) envkey = "HTTP_ACCEPT_ENCODING";
            else if (strcasecmp(headerkey, "Cookie") == 0)          envkey = "HTTP_COOKIE";
            else if (strcasecmp(headerkey, "Sec-Fetch-Dest") == 0)  envkey = "HTTP_SEC_FETCH_DEST";
            else if (strcasecmp(headerkey, "Sec-Fetch-Site") == 0)  envkey = "HTTP_SEC_FETCH_SITE";
            else if (strcasecmp(headerkey, "Sec-Fetch-Mode") == 0)  envkey = "HTTP_SEC_FETCH_MODE";
            else {
                // local ?
            }
            if (envkey != NULL) {
                snprintf(str, maxstr, "%s=%s", envkey, headervalue);
                putenv(str);
            }
        }
#endif // CGI_UNUSED_CODE

        // Tokenize php_params
        cgi->argc=0;
        cgi->argv[ cgi->argc++ ] = cgi->php_path;

        char *token = strtok(cgi->php_params, " ");
        while (token != NULL && cgi->argc < 8) {
            cgi->argv[cgi->argc++] = token;
            token = strtok(NULL, " ");
        }
        cgi->argv[cgi->argc++] = cgi->script_name;
        cgi->argv[cgi->argc] = NULL;

        
        int e = 0;
        char env_sessid[128];
        char env_cookie[256];
        char env_script_name[128];
        char env_script_path[256];
        char env_query_string[1024];

        snprintf(env_sessid, sizeof(env_sessid), "PHPSESSID=%s", ctx->request.session_id);
        snprintf(env_cookie, sizeof(env_cookie), "HTTP_COOKIE=PHPSESSID=%s", ctx->request.session_id);
        snprintf(env_script_name, sizeof(env_script_name), "SCRIPT_NAME=/%s", cgi->script_name);
        snprintf(env_script_path, sizeof(env_script_path), "SCRIPT_PATH=%s", params->path);

        ofs = 0;
        ofs += snprintf(env_query_string + ofs, sizeof(env_query_string) - ofs, "QUERY_STRING=");
        for (int i = 0; i < ctx->request.query_count; i++) {
            if (i > 0) env_query_string[ofs++] = '&';
            ofs += snprintf(env_query_string + ofs, sizeof(env_query_string) - ofs, "%s=%s", ctx->request.query[i].key, ctx->request.query[i].value);
        }

        char *envp[32];
        envp[e++] = env_sessid;
        envp[e++] = env_cookie;
        envp[e++] = env_script_name;
        envp[e++] = env_script_path;
        envp[e++] = env_query_string;
        envp[e++] = "REQUEST_METHOD=GET";
        envp[e++] = "GATEWAY_INTERFACE=CGI/1.1";
        envp[e++] = "SERVER_PROTOCOL=HTTP/1.1";
        envp[e++] = "SERVER_PORT=80";

        for (int i = 0; i < ctx->request.header_count && e < 30; i++) {
            const char* envkey = NULL;
            const char* headerkey = ctx->request.headers[i].key;
            const char* headervalue = ctx->request.headers[i].value;
            static char header_buf[1024][2];
            if (strcasecmp(headerkey, "User-Agent") == 0)           envkey = "HTTP_USER_AGENT";
            else if (strcasecmp(headerkey, "X-Forwarded-For") == 0) envkey = "REMOTE_ADDR";
            else if (strcasecmp(headerkey, "X-Forwarded-Host") == 0)envkey = "SERVER_NAME";
            else if (strcasecmp(headerkey, "accept-language") == 0) envkey = "HTTP_ACCEPT_LANGUAGE";
            else if (strcasecmp(headerkey, "Accept") == 0)          envkey = "HTTP_ACCEPT";
            else if (strcasecmp(headerkey, "Accept-Language") == 0) envkey = "HTTP_ACCEPT_LANGUAGE";
            else if (strcasecmp(headerkey, "Accept-Encoding") == 0) envkey = "HTTP_ACCEPT_ENCODING";
            else if (strcasecmp(headerkey, "Cookie") == 0)          envkey = "HTTP_COOKIE";
            else if (strcasecmp(headerkey, "Sec-Fetch-Dest") == 0)  envkey = "HTTP_SEC_FETCH_DEST";
            else if (strcasecmp(headerkey, "Sec-Fetch-Site") == 0)  envkey = "HTTP_SEC_FETCH_SITE";
            else if (strcasecmp(headerkey, "Sec-Fetch-Mode") == 0)  envkey = "HTTP_SEC_FETCH_MODE";
            if (envkey && e < 31) {
                snprintf(header_buf[i], sizeof(header_buf[i]), "%s=%s", envkey, headervalue);
                envp[e++] = header_buf[i];
            }
        }

        envp[e] = NULL;

        execve(cgi->php_path, cgi->argv, envp);
        perror("execvp failed");
        g_host->errormsg(
            "execvp failed for dir:'%s', fname:'%s'",
             cgi->script_dir, cgi-> script_name);
        cgi_data_release(cgi); // execve failed, safe to release memory before exiting
        exit(1);
    } else {
        // Parent
        cgi->active_pid = pid;
        cgi->state = CGI_STATE_RUNNING;

        // Create Cache file for child output
        g_host->cache.file_create(&cgi->cache_file);
        cgi_data_close_pipe(cgi, 1, STDERR_FILENO | STDOUT_FILENO);
        
        g_host->debugmsg("Parent waiting for child output (test.php)");
        char buffer[2048];
        size_t offset = 0;

        // Read stdout
        ssize_t nbytes;
        while ((nbytes = read(cgi->stdout_pipe[0], buffer, sizeof(buffer)-1)) > 0) {
            if (offset + nbytes >= cgi->result_maxlength) {
                size_t new_size = (cgi->result_maxlength+1) +  BUF_LENGHT_INCREMENT;
                if (new_size > ABSOLUTE_MAX_RESULT_BUF_LENGTH) {
                    g_host->errormsg("Result buffer too large, truncating and aborting.");
                    ctx->result_status = CTX_ERROR;
                    break;
                }
                char *new_result = realloc(cgi->result, new_size);
                if (!new_result) {
                    g_host->errormsg("Failed to reallocate result buffer.");
                    ctx->result_status = CTX_ERROR;
                    break;
                }
                cgi->result = new_result;
                cgi->result_maxlength = new_size-1;
            }
            memcpy(cgi->result + offset, buffer, nbytes);
            offset += nbytes;
            cgi->result[offset] = '\0';
            cgi->result_length = offset;
        }
        cgi_data_close_pipe(cgi, 0, STDOUT_FILENO );
        
        // Read stderr and log
        while ((nbytes = read(cgi->stderr_pipe[0], buffer, sizeof(buffer)-1)) > 0) {
            buffer[nbytes] = '\0';
            g_host->errormsg("[cgi stderr] %s", buffer);
        }
        cgi_data_close_pipe(cgi, 0, STDERR_FILENO );
        
        // Parse headers from stdout
        char *headers_end = strstr(cgi->result, "\r\n\r\n");
        if (!headers_end) headers_end = strstr(cgi->result, "\n\n");

        if (headers_end) {
            *headers_end = '\0';
            if (cgi->debug_enabled){
                g_host->logmsg("Headers from test.php:\n%s", cgi->result);
            }
            char *body = headers_end + 4;
            char *content_type = "text/plain";

            char *line = strtok(cgi->result, "\r\n");
            while (line) {
                if (strncasecmp(line, "Content-Type:", 13) == 0) {
                    content_type = line + 13;
                    while (*content_type == ' ') content_type++;
                    break;
                }
                line = strtok(NULL, "\r\n");
            }

            g_host->logmsg("test.php returned Content-Type: %s", content_type);
            g_host->http.send_response(ctx->socket_fd, 200, content_type, body);
            g_host->logmsg("Response sent to client for test.php");
        } else {
            g_host->debugmsg("No headers found in output of test.php, treating entire output as plain text.");
            g_host->http.send_response(ctx->socket_fd, 200, "text/plain", cgi->result);
        }
        g_host->logmsg("Response sent to client for test.php length:%d", cgi->result_length);
        g_host->cache.file_write(&cgi->cache_file, cgi->result, cgi->result_length);
        g_host->cache.file_close(&cgi->cache_file);

        // Wait for child process and remove from active PID list
        int status;
        waitpid(pid, &status, 0);

        clock_gettime(CLOCK_MONOTONIC, &cgi->end_time);
        double elapsed_time = (cgi->end_time.tv_sec - cgi->start_time.tv_sec) +
                              (cgi->end_time.tv_nsec - cgi->start_time.tv_nsec) / 1e9;
        g_host->logmsg("CGI executed %s script, and sent a result. index:%d, pid:%d, file:%s, duration:%.6f",
            cgi->script_name,
            cgi->pool_index, cgi->active_pid, cgi->cache_file.path, elapsed_time);
        cgi_data_release(cgi);
        ctx->elapsed_time = elapsed_time;
    }
    // Do not call cgi_data_free here; cgi_data_release already does cleanup
    if (ctx->result_status != CTX_ERROR){
        ctx->result_status = CTX_FINISHED_OK;
    }
}

/** CGI plugin's main http router fn */
void handle_cgi(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc; // Unused parameter
    (void)params; // Unused parameter
    (void)ctx; // Unused parameter
    // Handle specific paths
    int i;
    for (i = 0; i < plugin_http_routes_count; i++) {
        if (strcmp(plugin_http_routes[i], params->path) == 0) {
            plugin_http_functions[i](pc, ctx, params);
            return;
        }

    }
    const char *body = "{\"msg\":\"Unknown sub path\"}";
    g_host->http.send_response(ctx->socket_fd, 200, "application/json", body);
    g_host->logmsg("%s cgi request", ctx->client_ip);
}

int plugin_register(PluginContext *pc, const PluginHostInterface *host) {
    g_host = host;
    host->server.register_http_route((void*)pc, plugin_http_routes_count, plugin_http_routes);
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
    g_host = host;
    pc->http.request_handler = (void*) handle_cgi;
    // if we need a non-connection specific main thread, we can do it here
    // not in this discussed use-case , I guess.
    // This will runs once, when plugin loaded.
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
        pthread_mutex_lock(&cgi_pool_mutex);
        for (int i = 0; i < MAX_CGI_PIDS; i++) {
            if (g_cgi_pool[i].state == CGI_STATE_RUNNING) {
                pthread_mutex_unlock(&cgi_pool_mutex);
                return 1;
            }
        }
        pthread_mutex_unlock(&cgi_pool_mutex);
    }
    return 0;
}