#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define RELEASE_API "https://api.github.com/repos/Infiltrator-Projects/System-Monitor/releases/latest"
#define RELEASE_PREFIX "https://github.com/Infiltrator-Projects/System-Monitor/releases/download/"
#define PATH_LEN 4096
#define JSON_LIMIT (4U * 1024U * 1024U)
#define STATUS_DIR "/run/infiltrator-os"
#define STATUS_PATH STATUS_DIR "/system-monitor-install.status"
#define LOCK_PATH "/run/lock/infiltrator-os-system-monitor.lock"

static void write_status(const char *state, int progress,
                         const char *stage, const char *detail)
{
    (void)mkdir(STATUS_DIR, 0755);
    char temporary[PATH_LEN];
    if (snprintf(temporary, sizeof(temporary), STATUS_PATH ".%ld.tmp",
                 (long)getpid()) >= (int)sizeof(temporary))
        return;
    FILE *file = fopen(temporary, "w");
    if (!file) return;
    fprintf(file, "state=%s\nprogress=%d\nstage=%s\ndetail=%s\n",
            state ? state : "running", progress,
            stage ? stage : "", detail ? detail : "");
    if (fclose(file) == 0) {
        (void)chmod(temporary, 0644);
        (void)rename(temporary, STATUS_PATH);
    } else {
        (void)unlink(temporary);
    }
}

static void fail(const char *message)
{
    write_status("failed", 100, "System Monitor installation failed", message);
    fprintf(stderr, "Infiltrator OS System Monitor setup: %s\n", message);
    exit(EXIT_FAILURE);
}

static int run(const char *const argv[], bool quiet)
{
    pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        if (quiet) {
            int fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
            if (fd >= 0) {
                (void)dup2(fd, STDOUT_FILENO);
                (void)dup2(fd, STDERR_FILENO);
                if (fd > STDERR_FILENO) close(fd);
            }
        }
        execv(argv[0], (char *const *)argv);
        _exit(errno == ENOENT ? 127 : 126);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 125;
}

static void required(const char *const argv[])
{
    int rc = run(argv, false);
    if (rc != 0) {
        char detail[PATH_LEN];
        (void)snprintf(detail, sizeof(detail),
                       "A required command failed (%d): %s", rc, argv[0]);
        write_status("failed", 100, "System Monitor installation failed", detail);
        fprintf(stderr, "Infiltrator OS System Monitor setup: command failed (%d): %s\n",
                rc, argv[0]);
        exit(EXIT_FAILURE);
    }
}

static bool installed(const char *package)
{
    const char *const argv[] = {
        "/usr/bin/dpkg-query", "-W", "-f=${db:Status-Abbrev}", package, NULL
    };
    return run(argv, true) == 0;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long length = ftell(f);
    if (length < 0 || (unsigned long)length > JSON_LIMIT) { fclose(f); return NULL; }
    rewind(f);
    char *data = malloc((size_t)length + 1U);
    if (!data) { fclose(f); return NULL; }
    size_t got = fread(data, 1, (size_t)length, f);
    if (got != (size_t)length || ferror(f)) { free(data); fclose(f); return NULL; }
    data[got] = '\0';
    fclose(f);
    return data;
}

static bool json_string(const char *start, char *out, size_t out_size,
                        const char **after)
{
    if (!start || *start != '"' || out_size == 0U) return false;
    size_t used = 0U;
    const char *p = start + 1;
    while (*p && *p != '"') {
        unsigned char ch = (unsigned char)*p++;
        if (ch == '\\') {
            ch = (unsigned char)*p++;
            if (!ch) return false;
            switch (ch) {
                case '"': case '\\': case '/': break;
                case 'b': ch = '\b'; break;
                case 'f': ch = '\f'; break;
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                default: return false;
            }
        }
        if (ch < 0x20U || used + 1U >= out_size) return false;
        out[used++] = (char)ch;
    }
    if (*p != '"') return false;
    out[used] = '\0';
    if (after) *after = p + 1;
    return true;
}

static bool latest_installer_url(const char *json, char *url, size_t url_size)
{
    const char key[] = "\"browser_download_url\"";
    const char suffix[] = "-native-installer.run";
    const char *p = json;
    while ((p = strstr(p, key)) != NULL) {
        p += sizeof(key) - 1U;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        if (*p++ != ':') continue;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        char candidate[PATH_LEN];
        const char *after = NULL;
        if (!json_string(p, candidate, sizeof(candidate), &after)) continue;
        p = after;
        size_t length = strlen(candidate);
        size_t prefix_length = strlen(RELEASE_PREFIX);
        size_t suffix_length = strlen(suffix);
        if (length <= prefix_length + suffix_length ||
            strncmp(candidate, RELEASE_PREFIX, prefix_length) != 0 ||
            strcmp(candidate + length - suffix_length, suffix) != 0)
            continue;
        const char *name = strrchr(candidate, '/');
        if (!name || strncmp(name + 1, "infiltrator-system-monitor-", 27U) != 0)
            continue;
        if (strlen(candidate) >= url_size) return false;
        memcpy(url, candidate, length + 1U);
        return true;
    }
    return false;
}

static void purge_stock_monitors(void)
{
    static const char *packages[] = {
        "gnome-system-monitor",
        "mate-system-monitor",
        "xfce4-taskmanager",
        "plasma-systemmonitor",
        "lxtask",
        "qps"
    };
    for (size_t i = 0U; i < sizeof(packages) / sizeof(packages[0]); ++i) {
        if (!installed(packages[i])) continue;
        printf("Removing distribution System Monitor package: %s\n", packages[i]);
        const char *const argv[] = {
            "/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=300",
            "purge", "-y", packages[i], NULL
        };
        required(argv);
    }
}

static bool file_contains(const char *path, const char *needle)
{
    char *text = read_file(path);
    if (!text) return false;
    bool found = strstr(text, needle) != NULL;
    free(text);
    return found;
}

int main(void)
{
    if (geteuid() != 0) fail("must run as root");
    (void)mkdir(STATUS_DIR, 0755);

    int lock_fd = open(LOCK_PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (lock_fd < 0) fail("cannot create installation lock");
    if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        puts("A System Monitor native installation is already running.");
        close(lock_fd);
        return EXIT_SUCCESS;
    }

    write_status("running", 5, "Waiting for the package manager",
                 "The native build will begin as soon as the package transaction has finished.");
    if (access("/usr/bin/apt-get", X_OK) != 0 ||
        access("/usr/bin/dpkg-query", X_OK) != 0 ||
        access("/usr/bin/wget", X_OK) != 0)
        fail("required Debian/Mint package tools are missing");

    if (setenv("DEBIAN_FRONTEND", "noninteractive", 1) != 0)
        fail("cannot set noninteractive package environment");

    const char *const wait_argv[] = {
        "/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=300", "check", NULL
    };
    required(wait_argv);

    write_status("running", 12, "Checking the latest System Monitor release",
                 "Infiltrator OS is selecting the newest published native installer.");

    char work[] = "/tmp/infiltrator-system-monitor-XXXXXX";
    if (!mkdtemp(work)) fail("cannot create temporary working directory");

    char json_path[PATH_LEN];
    char installer_path[PATH_LEN];
    if (snprintf(json_path, sizeof(json_path), "%s/release.json", work) >= (int)sizeof(json_path) ||
        snprintf(installer_path, sizeof(installer_path), "%s/system-monitor.run", work) >= (int)sizeof(installer_path))
        fail("temporary path is too long");

    const char *const api_argv[] = {
        "/usr/bin/wget", "--quiet", "--https-only", "--secure-protocol=TLSv1_2",
        "--output-document", json_path, RELEASE_API, NULL
    };
    required(api_argv);

    char *json = read_file(json_path);
    if (!json) fail("cannot read latest System Monitor release metadata");
    char url[PATH_LEN];
    bool found = latest_installer_url(json, url, sizeof(url));
    free(json);
    if (!found) fail("latest System Monitor release has no trusted native installer asset");

    write_status("running", 22, "Downloading System Monitor",
                 "Downloading the latest native installer from Infiltrator Projects.");
    printf("Downloading latest System Monitor native installer:\n  %s\n", url);
    const char *const download_argv[] = {
        "/usr/bin/wget", "--quiet", "--https-only", "--secure-protocol=TLSv1_2",
        "--output-document", installer_path, url, NULL
    };
    required(download_argv);
    if (chmod(installer_path, 0700) != 0)
        fail("cannot mark native installer executable");

    write_status("running", 35, "Compiling and optimising System Monitor",
                 "Building for this CPU with aggressive native optimisation, LTO and two-pass PGO.");
    const char *const install_argv[] = {
        installer_path, "--profile", "aggressive", "--system-package-mode", NULL
    };
    required(install_argv);

    write_status("running", 88, "Removing replaced System Monitor packages",
                 "Cleaning up distribution System Monitor packages after the native build succeeded.");
    purge_stock_monitors();

    write_status("running", 95, "Verifying the native installation",
                 "Checking that the installed build is aggressive, PGO-trained and CPU-native.");

    if (!installed("infiltrator-system-monitor"))
        fail("native System Monitor package is not installed after the build");
    const char info[] = "/usr/share/doc/infiltrator-system-monitor/BUILD-INFO";
    if (!file_contains(info, "Profile: aggressive") ||
        !file_contains(info, "PGO: trained two-pass profile") ||
        !file_contains(info, "C flags: -O3") ||
        !file_contains(info, "-march=native"))
        fail("installed System Monitor is not an aggressive two-pass native build");

    unlink(installer_path);
    unlink(json_path);
    rmdir(work);
    write_status("complete", 100, "System Monitor native installation complete",
                 "The latest System Monitor is installed and optimised for this computer.");
    puts("Infiltrator OS System Monitor replacement completed successfully.");
    close(lock_fd);
    return EXIT_SUCCESS;
}
