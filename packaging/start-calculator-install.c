#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define STATUS_DIR "/run/infiltrator-os"
#define STATUS_PATH STATUS_DIR "/calculator-install.status"
#define LOG_PATH "/var/log/infiltrator-os-calculator.log"
#define WORKER "/usr/lib/infiltrator-os/install-calculator"
#define PROGRESS_UI "/usr/lib/infiltrator-os/calculator-progress"
#define ENV_LIMIT 65536U
#define TEXT_LIMIT 4096U

typedef struct {
    uid_t uid;
    gid_t gid;
    char user[128];
    char home[TEXT_LIMIT];
    char display[256];
    char wayland[256];
    char runtime[TEXT_LIMIT];
    char bus[TEXT_LIMIT];
    char xauthority[TEXT_LIMIT];
    bool found;
} DesktopSession;

static void die(const char *message)
{
    fprintf(stderr, "Infiltrator OS installer launcher: %s\n", message);
    exit(EXIT_FAILURE);
}

static void ensure_status_dir(void)
{
    if (mkdir(STATUS_DIR, 0755) != 0 && errno != EEXIST)
        die("cannot create runtime status directory");
}

static void write_initial_status(void)
{
    FILE *file = fopen(STATUS_PATH, "w");
    if (!file) die("cannot create installation status");
    fputs("state=queued\nprogress=1\nstage=Preparing Calculator optimisation\n"
          "detail=The installation will continue after the package manager releases its lock.\n",
          file);
    if (fclose(file) != 0) die("cannot write installation status");
    (void)chmod(STATUS_PATH, 0644);
}

static bool numeric_name(const char *text)
{
    if (!text || !*text) return false;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if (!isdigit(*p)) return false;
    return true;
}

static bool env_value(const char *data, size_t length, const char *key,
                      char *destination, size_t destination_size)
{
    const size_t key_length = strlen(key);
    for (size_t offset = 0U; offset < length;) {
        const char *entry = data + offset;
        const size_t remaining = length - offset;
        const size_t entry_length = strnlen(entry, remaining);
        if (entry_length == remaining) break;
        if (entry_length > key_length &&
            memcmp(entry, key, key_length) == 0 &&
            entry[key_length] == '=') {
            const char *value = entry + key_length + 1U;
            const size_t value_length = entry_length - key_length - 1U;
            if (value_length + 1U > destination_size) return false;
            memcpy(destination, value, value_length);
            destination[value_length] = '\0';
            return true;
        }
        offset += entry_length + 1U;
    }
    return false;
}

static bool session_from_process(const char *pid_name, DesktopSession *session)
{
    char path[TEXT_LIMIT];
    if (snprintf(path, sizeof(path), "/proc/%s", pid_name) >= (int)sizeof(path))
        return false;
    struct stat st;
    if (stat(path, &st) != 0 || st.st_uid < 1000U || st.st_uid == 65534U)
        return false;

    if (snprintf(path, sizeof(path), "/proc/%s/environ", pid_name) >= (int)sizeof(path))
        return false;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char environment[ENV_LIMIT];
    ssize_t got = read(fd, environment, sizeof(environment));
    close(fd);
    if (got <= 0) return false;

    DesktopSession candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.uid = st.st_uid;
    struct passwd *pw = getpwuid(candidate.uid);
    if (!pw || !pw->pw_name || !pw->pw_dir) return false;
    candidate.gid = pw->pw_gid;
    snprintf(candidate.user, sizeof(candidate.user), "%s", pw->pw_name);
    snprintf(candidate.home, sizeof(candidate.home), "%s", pw->pw_dir);

    (void)env_value(environment, (size_t)got, "DISPLAY",
                    candidate.display, sizeof(candidate.display));
    (void)env_value(environment, (size_t)got, "WAYLAND_DISPLAY",
                    candidate.wayland, sizeof(candidate.wayland));
    (void)env_value(environment, (size_t)got, "XDG_RUNTIME_DIR",
                    candidate.runtime, sizeof(candidate.runtime));
    (void)env_value(environment, (size_t)got, "DBUS_SESSION_BUS_ADDRESS",
                    candidate.bus, sizeof(candidate.bus));
    (void)env_value(environment, (size_t)got, "XAUTHORITY",
                    candidate.xauthority, sizeof(candidate.xauthority));

    if (!candidate.display[0] && !candidate.wayland[0]) return false;
    if (!candidate.runtime[0])
        snprintf(candidate.runtime, sizeof(candidate.runtime), "/run/user/%lu",
                 (unsigned long)candidate.uid);
    if (!candidate.bus[0]) {
        static const char prefix[] = "unix:path=";
        static const char suffix[] = "/bus";
        const size_t runtime_length = strlen(candidate.runtime);
        const size_t total = (sizeof(prefix) - 1U) + runtime_length +
                             (sizeof(suffix) - 1U) + 1U;
        if (total > sizeof(candidate.bus)) return false;
        char *cursor = candidate.bus;
        memcpy(cursor, prefix, sizeof(prefix) - 1U);
        cursor += sizeof(prefix) - 1U;
        memcpy(cursor, candidate.runtime, runtime_length);
        cursor += runtime_length;
        memcpy(cursor, suffix, sizeof(suffix));
    }
    candidate.found = true;
    *session = candidate;
    return true;
}

static DesktopSession find_desktop_session(void)
{
    DesktopSession result;
    memset(&result, 0, sizeof(result));
    DIR *proc = opendir("/proc");
    if (!proc) return result;
    struct dirent *entry;
    while ((entry = readdir(proc)) != NULL) {
        if (!numeric_name(entry->d_name)) continue;
        if (session_from_process(entry->d_name, &result)) break;
    }
    closedir(proc);
    return result;
}

static void close_extra_fds(void)
{
    long limit = sysconf(_SC_OPEN_MAX);
    if (limit < 0L || limit > 65536L) limit = 4096L;
    for (int fd = 3; fd < (int)limit; ++fd) close(fd);
}

static void redirect_worker_io(void)
{
    int input = open("/dev/null", O_RDONLY | O_CLOEXEC);
    int log = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (input >= 0) (void)dup2(input, STDIN_FILENO);
    if (log >= 0) {
        (void)dup2(log, STDOUT_FILENO);
        (void)dup2(log, STDERR_FILENO);
    }
    if (input > STDERR_FILENO) close(input);
    if (log > STDERR_FILENO) close(log);
}

static void spawn_worker(void)
{
    pid_t first = fork();
    if (first < 0) die("cannot start Calculator installer");
    if (first == 0) {
        if (setsid() < 0) _exit(120);
        pid_t second = fork();
        if (second < 0) _exit(121);
        if (second > 0) _exit(0);
        if (chdir("/") != 0) _exit(122);
        redirect_worker_io();
        close_extra_fds();
        execl(WORKER, WORKER, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    while (waitpid(first, &status, 0) < 0 && errno == EINTR) {}
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        die("cannot detach Calculator installer");
}

static void spawn_progress_ui(const DesktopSession *session)
{
    if (!session || !session->found || access(PROGRESS_UI, X_OK) != 0) return;
    pid_t first = fork();
    if (first != 0) return;
    if (setsid() < 0) _exit(120);
    pid_t second = fork();
    if (second < 0) _exit(121);
    if (second > 0) _exit(0);

    if (setgid(session->gid) != 0 ||
        initgroups(session->user, session->gid) != 0 ||
        setuid(session->uid) != 0)
        _exit(122);

    (void)setenv("HOME", session->home, 1);
    (void)setenv("USER", session->user, 1);
    (void)setenv("LOGNAME", session->user, 1);
    (void)setenv("XDG_RUNTIME_DIR", session->runtime, 1);
    (void)setenv("DBUS_SESSION_BUS_ADDRESS", session->bus, 1);
    if (session->display[0]) (void)setenv("DISPLAY", session->display, 1);
    if (session->wayland[0]) (void)setenv("WAYLAND_DISPLAY", session->wayland, 1);
    if (session->xauthority[0]) (void)setenv("XAUTHORITY", session->xauthority, 1);

    int null_fd = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (null_fd >= 0) {
        (void)dup2(null_fd, STDIN_FILENO);
        (void)dup2(null_fd, STDOUT_FILENO);
        (void)dup2(null_fd, STDERR_FILENO);
        if (null_fd > STDERR_FILENO) close(null_fd);
    }
    close_extra_fds();
    execl(PROGRESS_UI, PROGRESS_UI, STATUS_PATH, (char *)NULL);
    _exit(127);
}

int main(void)
{
    if (geteuid() != 0) die("must run as root");
    ensure_status_dir();
    write_initial_status();
    DesktopSession session = find_desktop_session();
    spawn_worker();
    spawn_progress_ui(&session);
    puts("Calculator native optimisation started in the background.");
    return EXIT_SUCCESS;
}
