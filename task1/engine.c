/*
 * engine.c - Supervised Multi-Container Runtime (User Space)
 *
 * Intentionally partial starter:
 *   - command-line shape is defined
 *   - key runtime data structures are defined
 *   - bounded-buffer skeleton is defined
 *   - supervisor / client split is outlined
 *
 * Students are expected to design:
 *   - the control-plane IPC implementation
 *   - container lifecycle and metadata synchronization
 *   - clone + namespace setup for each container
 *   - producer/consumer behavior for log buffering
 *   - signal handling and graceful shutdown
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "monitor_ioctl.h"

#define STACK_SIZE (1024 * 1024)
#define CONTAINER_ID_LEN 32
#define CONTROL_PATH "/tmp/mini_runtime.sock"
#define LOG_DIR "logs"
#define CONTROL_MESSAGE_LEN 256
#define CHILD_COMMAND_LEN 256
#define LOG_CHUNK_SIZE 4096
#define LOG_BUFFER_CAPACITY 16
#define DEFAULT_SOFT_LIMIT (40UL << 20)
#define DEFAULT_HARD_LIMIT (64UL << 20)

typedef enum {
    CMD_SUPERVISOR = 0,
    CMD_START,
    CMD_RUN,
    CMD_PS,
    CMD_LOGS,
    CMD_STOP
} command_kind_t;

typedef enum {
    CONTAINER_STARTING = 0,
    CONTAINER_RUNNING,
    CONTAINER_STOPPED,
    CONTAINER_KILLED,
    CONTAINER_EXITED
} container_state_t;

typedef struct container_record {
    char id[CONTAINER_ID_LEN];
    pid_t host_pid;
    time_t started_at;
    container_state_t state;
    unsigned long soft_limit_bytes;
    unsigned long hard_limit_bytes;
    int exit_code;
    int exit_signal;
    char log_path[PATH_MAX];
    struct container_record *next;
} container_record_t;

typedef struct {
    char container_id[CONTAINER_ID_LEN];
    size_t length;
    char data[LOG_CHUNK_SIZE];
} log_item_t;

typedef struct {
    log_item_t items[LOG_BUFFER_CAPACITY];
    size_t head;
    size_t tail;
    size_t count;
    int shutting_down;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} bounded_buffer_t;

typedef struct {
    command_kind_t kind;
    char container_id[CONTAINER_ID_LEN];
    char rootfs[PATH_MAX];
    char command[CHILD_COMMAND_LEN];
    unsigned long soft_limit_bytes;
    unsigned long hard_limit_bytes;
    int nice_value;
} control_request_t;

typedef struct {
    int status;
    char message[CONTROL_MESSAGE_LEN];
} control_response_t;

typedef struct {
    char id[CONTAINER_ID_LEN];
    char rootfs[PATH_MAX];
    char command[CHILD_COMMAND_LEN];
    int nice_value;
    int log_write_fd;
} child_config_t;

typedef struct {
    int server_fd;
    int monitor_fd;
    int should_stop;
    pthread_t logger_thread;
    bounded_buffer_t log_buffer;
    pthread_mutex_t metadata_lock;
    container_record_t *containers;
} supervisor_ctx_t;

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s supervisor <base-rootfs>\n"
            "  %s start <id> <container-rootfs> <command> [--soft-mib N] [--hard-mib N] [--nice N]\n"
            "  %s run <id> <container-rootfs> <command> [--soft-mib N] [--hard-mib N] [--nice N]\n"
            "  %s ps\n"
            "  %s logs <id>\n"
            "  %s stop <id>\n",
            prog, prog, prog, prog, prog, prog);
}

static int parse_mib_flag(const char *flag,
                          const char *value,
                          unsigned long *target_bytes)
{
    char *end = NULL;
    unsigned long mib;

    errno = 0;
    mib = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0') {
        fprintf(stderr, "Invalid value for %s: %s\n", flag, value);
        return -1;
    }

    if (mib > ULONG_MAX / (1UL << 20)) {
        fprintf(stderr, "Value for %s is too large: %s\n", flag, value);
        return -1;
    }

    *target_bytes = mib * (1UL << 20);
    return 0;
}

static int parse_optional_flags(control_request_t *req,
                                int argc,
                                char *argv[],
                                int start_index)
{
    int i;

    for (i = start_index; i < argc; i += 2) {
        char *end = NULL;
        long nice_value;

        if (i + 1 >= argc) {
            fprintf(stderr, "Missing value for option: %s\n", argv[i]);
            return -1;
        }

        if (strcmp(argv[i], "--soft-mib") == 0) {
            if (parse_mib_flag("--soft-mib", argv[i + 1], &req->soft_limit_bytes) != 0)
                return -1;
            continue;
        }

        if (strcmp(argv[i], "--hard-mib") == 0) {
            if (parse_mib_flag("--hard-mib", argv[i + 1], &req->hard_limit_bytes) != 0)
                return -1;
            continue;
        }

        if (strcmp(argv[i], "--nice") == 0) {
            errno = 0;
            nice_value = strtol(argv[i + 1], &end, 10);
            if (errno != 0 || end == argv[i + 1] || *end != '\0' ||
                nice_value < -20 || nice_value > 19) {
                fprintf(stderr,
                        "Invalid value for --nice (expected -20..19): %s\n",
                        argv[i + 1]);
                return -1;
            }
            req->nice_value = (int)nice_value;
            continue;
        }

        fprintf(stderr, "Unknown option: %s\n", argv[i]);
        return -1;
    }

    if (req->soft_limit_bytes > req->hard_limit_bytes) {
        fprintf(stderr, "Invalid limits: soft limit cannot exceed hard limit\n");
        return -1;
    }

    return 0;
}

static const char *state_to_string(container_state_t state)
{
    switch (state) {
    case CONTAINER_STARTING:
        return "starting";
    case CONTAINER_RUNNING:
        return "running";
    case CONTAINER_STOPPED:
        return "stopped";
    case CONTAINER_KILLED:
        return "killed";
    case CONTAINER_EXITED:
        return "exited";
    default:
        return "unknown";
    }
}

static int bounded_buffer_init(bounded_buffer_t *buffer)
{
    int rc;

    memset(buffer, 0, sizeof(*buffer));

    rc = pthread_mutex_init(&buffer->mutex, NULL);
    if (rc != 0)
        return rc;

    rc = pthread_cond_init(&buffer->not_empty, NULL);
    if (rc != 0) {
        pthread_mutex_destroy(&buffer->mutex);
        return rc;
    }

    rc = pthread_cond_init(&buffer->not_full, NULL);
    if (rc != 0) {
        pthread_cond_destroy(&buffer->not_empty);
        pthread_mutex_destroy(&buffer->mutex);
        return rc;
    }

    return 0;
}

static void bounded_buffer_destroy(bounded_buffer_t *buffer)
{
    pthread_cond_destroy(&buffer->not_full);
    pthread_cond_destroy(&buffer->not_empty);
    pthread_mutex_destroy(&buffer->mutex);
}

static void bounded_buffer_begin_shutdown(bounded_buffer_t *buffer)
{
    pthread_mutex_lock(&buffer->mutex);
    buffer->shutting_down = 1;
    pthread_cond_broadcast(&buffer->not_empty);
    pthread_cond_broadcast(&buffer->not_full);
    pthread_mutex_unlock(&buffer->mutex);
}

/*
 * TODO:
 * Implement producer-side insertion into the bounded buffer.
 *
 * Requirements:
 *   - block or fail according to your chosen policy when the buffer is full
 *   - wake consumers correctly
 *   - stop cleanly if shutdown begins
 */
int bounded_buffer_push(bounded_buffer_t *buffer, const log_item_t *item)
{
    pthread_mutex_lock(&buffer->mutex);
    while (buffer->count == LOG_BUFFER_CAPACITY && !buffer->shutting_down) {
        pthread_cond_wait(&buffer->not_full, &buffer->mutex);
    }
    if (buffer->shutting_down) {
        pthread_mutex_unlock(&buffer->mutex);
        return -1;
    }
    buffer->items[buffer->tail] = *item;
    buffer->tail = (buffer->tail + 1) % LOG_BUFFER_CAPACITY;
    buffer->count++;
    pthread_cond_signal(&buffer->not_empty);
    pthread_mutex_unlock(&buffer->mutex);
    return 0;
}

/*
 * TODO:
 * Implement consumer-side removal from the bounded buffer.
 *
 * Requirements:
 *   - wait correctly while the buffer is empty
 *   - return a useful status when shutdown is in progress
 *   - avoid races with producers and shutdown
 */
int bounded_buffer_pop(bounded_buffer_t *buffer, log_item_t *item)
{
    pthread_mutex_lock(&buffer->mutex);
    while (buffer->count == 0 && !buffer->shutting_down) {
        pthread_cond_wait(&buffer->not_empty, &buffer->mutex);
    }
    if (buffer->count == 0 && buffer->shutting_down) {
        pthread_mutex_unlock(&buffer->mutex);
        return -1;
    }
    *item = buffer->items[buffer->head];
    buffer->head = (buffer->head + 1) % LOG_BUFFER_CAPACITY;
    buffer->count--;
    pthread_cond_signal(&buffer->not_full);
    pthread_mutex_unlock(&buffer->mutex);
    return 0;
}

/*
 * TODO:
 * Implement the logging consumer thread.
 *
 * Suggested responsibilities:
 *   - remove log chunks from the bounded buffer
 *   - route each chunk to the correct per-container log file
 *   - exit cleanly when shutdown begins and pending work is drained
 */
void *logging_thread(void *arg)
{
    supervisor_ctx_t *ctx = (supervisor_ctx_t *)arg;
    struct stat st = {0};
    if (stat(LOG_DIR, &st) == -1) {
        mkdir(LOG_DIR, 0700);
    }

    log_item_t item;
    while (bounded_buffer_pop(&ctx->log_buffer, &item) == 0) {
        char filepath[PATH_MAX];
        snprintf(filepath, sizeof(filepath), "%s/%s.log", LOG_DIR, item.container_id);
        FILE *f = fopen(filepath, "a");
        if (f) {
            fwrite(item.data, 1, item.length, f);
            fclose(f);
        }
    }
    return NULL;
}

/*
 * Implement the clone child entrypoint.
 *
 * Required outcomes:
 *   - isolated PID / UTS / mount context
 *   - chroot or pivot_root into rootfs
 *   - working /proc inside container
 *   - stdout / stderr redirected to the supervisor logging path
 *   - configured command executed inside the container
 */
int child_fn(void *arg)
{
    child_config_t *config = (child_config_t *)arg;
    char *argv[] = {"/bin/sh", "-c", config->command, NULL};

    if (config->log_write_fd > 0) {
        dup2(config->log_write_fd, STDOUT_FILENO);
        dup2(config->log_write_fd, STDERR_FILENO);
        close(config->log_write_fd);
    }

    setpgid(0, 0);

    if (nice(config->nice_value) < 0) {
        perror("nice");
    }

    if (mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL) != 0) {
        perror("mount / private");
        return 1;
    }

    if (mount(config->rootfs, config->rootfs, "bind", MS_BIND | MS_REC, NULL) != 0) {
        perror("bind mount rootfs");
        return 1;
    }

    if (chdir(config->rootfs) != 0) {
        perror("chdir rootfs");
        return 1;
    }

    if (mkdir("put_old", 0777) != 0 && errno != EEXIST) {
        perror("mkdir put_old");
        return 1;
    }

    if (syscall(SYS_pivot_root, ".", "put_old") != 0) {
        perror("pivot_root");
        return 1;
    }

    if (chdir("/") != 0) {
        perror("chdir /");
        return 1;
    }

    if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
        perror("mount /proc");
        return 1;
    }

    if (umount2("/put_old", MNT_DETACH) != 0) {
        perror("umount2 /put_old");
        return 1;
    }
    rmdir("/put_old");

    execvp(argv[0], argv);
    perror("execvp");
    return 1;
}

int register_with_monitor(int monitor_fd,
                          const char *container_id,
                          pid_t host_pid,
                          unsigned long soft_limit_bytes,
                          unsigned long hard_limit_bytes)
{
    struct monitor_request req;

    memset(&req, 0, sizeof(req));
    req.pid = host_pid;
    req.soft_limit_bytes = soft_limit_bytes;
    req.hard_limit_bytes = hard_limit_bytes;
    strncpy(req.container_id, container_id, sizeof(req.container_id) - 1);

    if (ioctl(monitor_fd, MONITOR_REGISTER, &req) < 0)
        return -1;

    return 0;
}

int unregister_from_monitor(int monitor_fd, const char *container_id, pid_t host_pid)
{
    struct monitor_request req;

    memset(&req, 0, sizeof(req));
    req.pid = host_pid;
    strncpy(req.container_id, container_id, sizeof(req.container_id) - 1);

    if (ioctl(monitor_fd, MONITOR_UNREGISTER, &req) < 0)
        return -1;

    return 0;
}

static void sigchld_handler(int sig)
{
    (void)sig;
    /* Do nothing, actual waitpid done in main loop */
}

static void sigint_handler(int sig)
{
    (void)sig;
}

typedef struct {
    char container_id[CONTAINER_ID_LEN];
    int read_fd;
    bounded_buffer_t *log_buffer;
} producer_ctx_t;

void *producer_thread(void *arg)
{
    producer_ctx_t *ctx = (producer_ctx_t *)arg;
    char buf[LOG_CHUNK_SIZE];
    ssize_t n;
    
    while ((n = read(ctx->read_fd, buf, sizeof(buf))) > 0) {
        log_item_t item;
        strncpy(item.container_id, ctx->container_id, sizeof(item.container_id));
        item.length = n;
        memcpy(item.data, buf, n);
        if (bounded_buffer_push(ctx->log_buffer, &item) != 0) {
            break;
        }
    }
    close(ctx->read_fd);
    free(ctx);
    return NULL;
}

static int run_supervisor(const char *rootfs)
{
    supervisor_ctx_t ctx;
    int rc;

    memset(&ctx, 0, sizeof(ctx));
    ctx.server_fd = -1;
    ctx.monitor_fd = -1;

    rc = pthread_mutex_init(&ctx.metadata_lock, NULL);
    if (rc != 0) {
        errno = rc;
        perror("pthread_mutex_init");
        return 1;
    }

    rc = bounded_buffer_init(&ctx.log_buffer);
    if (rc != 0) {
        errno = rc;
        perror("bounded_buffer_init");
        pthread_mutex_destroy(&ctx.metadata_lock);
        return 1;
    }

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    ctx.server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ctx.server_fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CONTROL_PATH, sizeof(addr.sun_path) - 1);

    unlink(CONTROL_PATH);
    if (bind(ctx.server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        return 1;
    }

    if (listen(ctx.server_fd, 5) == -1) {
        perror("listen");
        return 1;
    }

    printf("Supervisor started with base rootfs %s\n", rootfs);

    while (!ctx.should_stop) {
        int client_fd = accept(ctx.server_fd, NULL, NULL);

        if (client_fd < 0) {
            if (errno == EINTR) {
                // Signal caught, reap children or exit
                int status;
                pid_t pid;
                while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
                    pthread_mutex_lock(&ctx.metadata_lock);
                    container_record_t *curr = ctx.containers;
                    while (curr) {
                        if (curr->host_pid == pid) {
                            curr->state = CONTAINER_EXITED;
                            if (WIFEXITED(status)) {
                                curr->exit_code = WEXITSTATUS(status);
                            } else if (WIFSIGNALED(status)) {
                                curr->exit_code = 128 + WTERMSIG(status);
                                curr->exit_signal = WTERMSIG(status);
                            }
                            printf("Container %s exited with code %d\n", curr->id, curr->exit_code);
                            break;
                        }
                        curr = curr->next;
                    }
                    pthread_mutex_unlock(&ctx.metadata_lock);
                }
                continue;
            }
            perror("accept");
            continue;
        }

        control_request_t req;
        if (recv(client_fd, &req, sizeof(req), 0) == sizeof(req)) {
            control_response_t res;
            memset(&res, 0, sizeof(res));

            if (req.kind == CMD_START || req.kind == CMD_RUN) {
                child_config_t *config = malloc(sizeof(child_config_t));
                strncpy(config->id, req.container_id, sizeof(config->id));
                strncpy(config->rootfs, req.rootfs, sizeof(config->rootfs));
                strncpy(config->command, req.command, sizeof(config->command));
                config->nice_value = req.nice_value;

                void *stack = malloc(STACK_SIZE);
                if (!stack) {
                    res.status = 1;
                    strcpy(res.message, "Failed to allocate stack");
                } else {
                    pid_t pid = clone(child_fn, stack + STACK_SIZE, CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | SIGCHLD, config);
                    if (pid < 0) {
                        res.status = 1;
                        strcpy(res.message, "clone failed");
                        free(stack);
                        free(config);
                    } else {
                        res.status = 0;
                        strcpy(res.message, "Started");

                        container_record_t *rec = malloc(sizeof(container_record_t));
                        memset(rec, 0, sizeof(*rec));
                        strncpy(rec->id, req.container_id, sizeof(rec->id));
                        rec->host_pid = pid;
                        rec->started_at = time(NULL);
                        rec->state = CONTAINER_RUNNING;
                        rec->soft_limit_bytes = req.soft_limit_bytes;
                        rec->hard_limit_bytes = req.hard_limit_bytes;
                        
                        pthread_mutex_lock(&ctx.metadata_lock);
                        rec->next = ctx.containers;
                        ctx.containers = rec;
                        pthread_mutex_unlock(&ctx.metadata_lock);
                    }
                }
            } else if (req.kind == CMD_PS) {
                res.status = 0;
            }
            send(client_fd, &res, sizeof(res), 0);
        }
        close(client_fd);
    }

    unlink(CONTROL_PATH);

    bounded_buffer_begin_shutdown(&ctx.log_buffer);
    bounded_buffer_destroy(&ctx.log_buffer);
    pthread_mutex_destroy(&ctx.metadata_lock);
    return 0;
}

/*
 * Implement the client-side control request path.
 */
static int send_control_request(const control_request_t *req)
{
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CONTROL_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("connect");
        close(sock);
        return 1;
    }

    if (send(sock, req, sizeof(*req), 0) != sizeof(*req)) {
        perror("send");
        close(sock);
        return 1;
    }

    control_response_t res;
    if (recv(sock, &res, sizeof(res), 0) != sizeof(res)) {
        perror("recv");
        close(sock);
        return 1;
    }

    if (res.status != 0) {
        fprintf(stderr, "Error: %s\n", res.message);
    } else {
        if (req->kind == CMD_PS) {
            printf("%s\n", res.message);
        } else {
            printf("Success: %s\n", res.message);
        }
    }

    close(sock);
    return res.status;
}

static int cmd_start(int argc, char *argv[])
{
    control_request_t req;

    if (argc < 5) {
        fprintf(stderr,
                "Usage: %s start <id> <container-rootfs> <command> [--soft-mib N] [--hard-mib N] [--nice N]\n",
                argv[0]);
        return 1;
    }

    memset(&req, 0, sizeof(req));
    req.kind = CMD_START;
    strncpy(req.container_id, argv[2], sizeof(req.container_id) - 1);
    strncpy(req.rootfs, argv[3], sizeof(req.rootfs) - 1);
    strncpy(req.command, argv[4], sizeof(req.command) - 1);
    req.soft_limit_bytes = DEFAULT_SOFT_LIMIT;
    req.hard_limit_bytes = DEFAULT_HARD_LIMIT;

    if (parse_optional_flags(&req, argc, argv, 5) != 0)
        return 1;

    return send_control_request(&req);
}

static volatile sig_atomic_t run_interrupted = 0;
static char run_target_id[CONTAINER_ID_LEN];

static void run_sig_handler(int sig)
{
    (void)sig;
    run_interrupted = 1;
}

static int cmd_run(int argc, char *argv[])
{
    control_request_t req;

    if (argc < 5) {
        fprintf(stderr,
                "Usage: %s run <id> <container-rootfs> <command> [--soft-mib N] [--hard-mib N] [--nice N]\n",
                argv[0]);
        return 1;
    }

    memset(&req, 0, sizeof(req));
    req.kind = CMD_RUN;
    strncpy(req.container_id, argv[2], sizeof(req.container_id) - 1);
    strncpy(req.rootfs, argv[3], sizeof(req.rootfs) - 1);
    strncpy(req.command, argv[4], sizeof(req.command) - 1);
    req.soft_limit_bytes = DEFAULT_SOFT_LIMIT;
    req.hard_limit_bytes = DEFAULT_HARD_LIMIT;

    if (parse_optional_flags(&req, argc, argv, 5) != 0)
        return 1;

    int status = send_control_request(&req);
    if (status != 0) return status;

    strncpy(run_target_id, req.container_id, sizeof(run_target_id));
    struct sigaction sa;
    sa.sa_handler = run_sig_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("Waiting for container %s to exit...\n", req.container_id);

    while (1) {
        if (run_interrupted) {
            run_interrupted = 0;
            printf("\nInterrupted! Sending stop to %s...\n", run_target_id);
            control_request_t stop_req;
            memset(&stop_req, 0, sizeof(stop_req));
            stop_req.kind = CMD_STOP;
            strncpy(stop_req.container_id, run_target_id, sizeof(stop_req.container_id));
            stop_req.container_id[sizeof(stop_req.container_id) - 1] = '\0';
            send_control_request(&stop_req);
        }

        control_request_t ps_req;
        memset(&ps_req, 0, sizeof(ps_req));
        ps_req.kind = CMD_PS;

        int sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock >= 0) {
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, CONTROL_PATH, sizeof(addr.sun_path) - 1);
            if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
                if (send(sock, &ps_req, sizeof(ps_req), 0) == sizeof(ps_req)) {
                    control_response_t res;
                    if (recv(sock, &res, sizeof(res), 0) == sizeof(res)) {
                        char search_str[64];
                        snprintf(search_str, sizeof(search_str), "ID: %s |", req.container_id);
                        char *ptr = strstr(res.message, search_str);
                        if (ptr) {
                            if (strstr(ptr, "State: exited") || strstr(ptr, "State: killed") || strstr(ptr, "State: stopped")) {
                                char *exit_code_str = strstr(ptr, "ExitCode: ");
                                if (exit_code_str) {
                                    int code = atoi(exit_code_str + 10);
                                    printf("Container exited with code %d\n", code);
                                    close(sock);
                                    return code;
                                }
                            }
                        }
                    }
                }
            }
            close(sock);
        }
        sleep(1);
    }
    return 0;
}

static int cmd_ps(void)
{
    control_request_t req;

    memset(&req, 0, sizeof(req));
    req.kind = CMD_PS;

    /*
     * TODO:
     * The supervisor should respond with container metadata.
     * Keep the rendering format simple enough for demos and debugging.
     */
    printf("Expected states include: %s, %s, %s, %s, %s\n",
           state_to_string(CONTAINER_STARTING),
           state_to_string(CONTAINER_RUNNING),
           state_to_string(CONTAINER_STOPPED),
           state_to_string(CONTAINER_KILLED),
           state_to_string(CONTAINER_EXITED));
    return send_control_request(&req);
}

static int cmd_logs(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s logs <id>\n", argv[0]);
        return 1;
    }

    char filepath[PATH_MAX];
    snprintf(filepath, sizeof(filepath), "%s/%s.log", LOG_DIR, argv[2]);
    FILE *f = fopen(filepath, "r");
    if (!f) {
        perror("fopen logs");
        return 1;
    }

    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        fwrite(buf, 1, n, stdout);
    }
    fclose(f);
    return 0;
}

static int cmd_stop(int argc, char *argv[])
{
    control_request_t req;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s stop <id>\n", argv[0]);
        return 1;
    }

    memset(&req, 0, sizeof(req));
    req.kind = CMD_STOP;
    strncpy(req.container_id, argv[2], sizeof(req.container_id) - 1);

    return send_control_request(&req);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "supervisor") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s supervisor <base-rootfs>\n", argv[0]);
            return 1;
        }
        return run_supervisor(argv[2]);
    }

    if (strcmp(argv[1], "start") == 0)
        return cmd_start(argc, argv);

    if (strcmp(argv[1], "run") == 0)
        return cmd_run(argc, argv);

    if (strcmp(argv[1], "ps") == 0)
        return cmd_ps();

    if (strcmp(argv[1], "logs") == 0)
        return cmd_logs(argc, argv);

    if (strcmp(argv[1], "stop") == 0)
        return cmd_stop(argc, argv);

    usage(argv[0]);
    return 1;
}
