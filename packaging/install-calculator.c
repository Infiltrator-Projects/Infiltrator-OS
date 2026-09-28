#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
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

#define RELEASE_API "https://api.github.com/repos/Infiltrator-Projects/Calculator/releases/latest"
#define RELEASE_PREFIX "https://github.com/Infiltrator-Projects/Calculator/releases/download/"
#define PATH_LEN 4096
#define JSON_LIMIT (4U * 1024U * 1024U)
#define STATUS_DIR "/run/infiltrator-os"
#define STATUS_PATH STATUS_DIR "/calculator-install.status"
#define APP_LOCK_PATH "/run/lock/infiltrator-os-calculator.lock"
#define GLOBAL_LOCK_PATH "/run/lock/infiltrator-os-native-install.lock"

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
    write_status("failed", 100, "Calculator installation failed", message);
    fprintf(stderr, "Infiltrator OS Calculator setup: %s\n", message);
    exit(EXIT_FAILURE);
}

static int run_at(const char *working_directory, const char *const argv[], bool quiet)
{
    pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        if (working_directory && chdir(working_directory) != 0) _exit(126);
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

static int run(const char *const argv[], bool quiet)
{
    return run_at(NULL, argv, quiet);
}

static void required_at(const char *working_directory, const char *const argv[])
{
    int rc = run_at(working_directory, argv, false);
    if (rc != 0) {
        char detail[PATH_LEN];
        (void)snprintf(detail, sizeof(detail),
                       "A required command failed (%d): %s", rc, argv[0]);
        write_status("failed", 100, "Calculator installation failed", detail);
        fprintf(stderr, "Infiltrator OS Calculator setup: command failed (%d): %s\n",
                rc, argv[0]);
        exit(EXIT_FAILURE);
    }
}

static void required(const char *const argv[])
{
    required_at(NULL, argv);
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

static bool file_contains(const char *path, const char *needle)
{
    char *text = read_file(path);
    if (!text) return false;
    bool found = strstr(text, needle) != NULL;
    free(text);
    return found;
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

static bool valid_version(const char *version)
{
    if (!version || !version[0]) return false;
    for (const unsigned char *p = (const unsigned char *)version; *p; ++p) {
        if (isalnum(*p) || *p == '.' || *p == '+' || *p == '~' || *p == '-')
            continue;
        return false;
    }
    return true;
}

static bool latest_source_url(const char *json, char *url, size_t url_size,
                              char *version, size_t version_size)
{
    const char key[] = "\"browser_download_url\"";
    const char file_prefix[] = "calculator_";
    const char suffix[] = "_source.tar.gz";
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
        size_t release_prefix_length = strlen(RELEASE_PREFIX);
        size_t suffix_length = strlen(suffix);
        if (length <= release_prefix_length + suffix_length ||
            strncmp(candidate, RELEASE_PREFIX, release_prefix_length) != 0 ||
            strcmp(candidate + length - suffix_length, suffix) != 0)
            continue;
        const char *name = strrchr(candidate, '/');
        if (!name) continue;
        ++name;
        size_t name_length = strlen(name);
        size_t file_prefix_length = strlen(file_prefix);
        if (name_length <= file_prefix_length + suffix_length ||
            strncmp(name, file_prefix, file_prefix_length) != 0)
            continue;
        size_t version_length = name_length - file_prefix_length - suffix_length;
        if (version_length + 1U > version_size || length + 1U > url_size)
            return false;
        memcpy(version, name + file_prefix_length, version_length);
        version[version_length] = '\0';
        if (!valid_version(version)) return false;
        memcpy(url, candidate, length + 1U);
        return true;
    }
    return false;
}

static void purge_stock_calculators(void)
{
    static const char *packages[] = {
        "gnome-calculator",
        "mate-calc",
        "kcalc",
        "galculator",
        "deepin-calculator",
        "ukui-calculator"
    };
    for (size_t i = 0U; i < sizeof(packages) / sizeof(packages[0]); ++i) {
        if (!installed(packages[i])) continue;
        printf("Removing distribution Calculator package: %s\n", packages[i]);
        const char *const argv[] = {
            "/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=300",
            "purge", "-y", packages[i], NULL
        };
        required(argv);
    }
}

static void write_build_info(void)
{
    const char path[] = "/usr/share/doc/infiltrator-calculator/BUILD-INFO";
    FILE *file = fopen(path, "w");
    if (!file) fail("cannot write Calculator native build information");
    fputs("Profile: aggressive\n"
          "PGO: trained two-pass profile\n"
          "CXX flags: -O3 -march=native -mtune=native -flto=auto\n"
          "Installed by: Infiltrator OS native application provisioning\n",
          file);
    if (fclose(file) != 0)
        fail("cannot finish Calculator native build information");
    (void)chmod(path, 0644);
}

static void cleanup_work(const char *work)
{
    if (!work || !work[0]) return;
    const char *const argv[] = { "/usr/bin/rm", "-rf", "--", work, NULL };
    (void)run(argv, true);
}

int main(void)
{
    if (geteuid() != 0) fail("must run as root");
    (void)mkdir(STATUS_DIR, 0755);

    int app_lock_fd = open(APP_LOCK_PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (app_lock_fd < 0) fail("cannot create Calculator installation lock");
    if (flock(app_lock_fd, LOCK_EX | LOCK_NB) != 0) {
        puts("A Calculator native installation is already running.");
        close(app_lock_fd);
        return EXIT_SUCCESS;
    }

    int global_lock_fd = open(GLOBAL_LOCK_PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (global_lock_fd < 0) fail("cannot create global native installation lock");
    write_status("queued", 2, "Waiting for another native installation",
                 "Calculator will start as soon as the current Infiltrator native build has finished.");
    while (flock(global_lock_fd, LOCK_EX) != 0) {
        if (errno == EINTR) continue;
        close(global_lock_fd);
        close(app_lock_fd);
        fail("cannot acquire global native installation lock");
    }

    write_status("running", 5, "Waiting for the package manager",
                 "The Calculator native build will begin as soon as the package transaction has finished.");
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

    write_status("running", 10, "Checking the latest Calculator release",
                 "Infiltrator OS is selecting the newest published Calculator source release.");

    char work[] = "/tmp/infiltrator-calculator-XXXXXX";
    if (!mkdtemp(work)) fail("cannot create temporary Calculator working directory");

    char json_path[PATH_LEN];
    char archive_path[PATH_LEN];
    if (snprintf(json_path, sizeof(json_path), "%s/release.json", work) >= (int)sizeof(json_path) ||
        snprintf(archive_path, sizeof(archive_path), "%s/calculator-source.tar.gz", work) >= (int)sizeof(archive_path)) {
        cleanup_work(work);
        fail("temporary Calculator path is too long");
    }

    const char *const api_argv[] = {
        "/usr/bin/wget", "--quiet", "--https-only", "--secure-protocol=TLSv1_2",
        "--output-document", json_path, RELEASE_API, NULL
    };
    required(api_argv);

    char *json = read_file(json_path);
    if (!json) { cleanup_work(work); fail("cannot read latest Calculator release metadata"); }
    char url[PATH_LEN];
    char version[128];
    bool found = latest_source_url(json, url, sizeof(url), version, sizeof(version));
    free(json);
    if (!found) { cleanup_work(work); fail("latest Calculator release has no trusted source bundle"); }

    write_status("running", 16, "Downloading Calculator",
                 "Downloading the latest verified Calculator source release from Infiltrator Projects.");
    printf("Downloading latest Calculator source bundle:\n  %s\n", url);
    const char *const download_argv[] = {
        "/usr/bin/wget", "--quiet", "--https-only", "--secure-protocol=TLSv1_2",
        "--output-document", archive_path, url, NULL
    };
    required(download_argv);

    write_status("running", 22, "Preparing Calculator build requirements",
                 "Installing the Debian/Mint C++ and GTK4 build requirements needed for a local native build.");
    const char *const deps_argv[] = {
        "/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=300", "install", "-y",
        "build-essential", "cmake", "debhelper-compat", "dpkg-dev",
        "libgtk-4-dev", "pkg-config", "xvfb", "tar", "gzip", NULL
    };
    required(deps_argv);

    write_status("running", 30, "Extracting Calculator source",
                 "Preparing the exact published source bundle for this computer.");
    const char *const tar_argv[] = {
        "/usr/bin/tar", "-xzf", archive_path, "-C", work, NULL
    };
    required(tar_argv);

    char source_root[PATH_LEN];
    char pgo_dir[PATH_LEN];
    if (snprintf(source_root, sizeof(source_root), "%s/Calculator-%s", work, version) >= (int)sizeof(source_root) ||
        snprintf(pgo_dir, sizeof(pgo_dir), "%s/pgo", work) >= (int)sizeof(pgo_dir)) {
        cleanup_work(work);
        fail("Calculator source path is too long");
    }
    struct stat source_stat;
    if (stat(source_root, &source_stat) != 0 || !S_ISDIR(source_stat.st_mode)) {
        cleanup_work(work);
        fail("Calculator source bundle did not contain its expected source root");
    }
    if (mkdir(pgo_dir, 0755) != 0) {
        cleanup_work(work);
        fail("cannot create Calculator PGO profile directory");
    }

    char generate_flags[PATH_LEN + 160];
    char generate_ldflags[PATH_LEN + 96];
    if (snprintf(generate_flags, sizeof(generate_flags),
                 "-O3 -march=native -mtune=native -flto=auto -fprofile-generate=%s", pgo_dir) >= (int)sizeof(generate_flags) ||
        snprintf(generate_ldflags, sizeof(generate_ldflags),
                 "-flto=auto -fprofile-generate=%s", pgo_dir) >= (int)sizeof(generate_ldflags)) {
        cleanup_work(work);
        fail("Calculator PGO generation flags are too long");
    }
    if (setenv("DEB_CXXFLAGS_MAINT_APPEND", generate_flags, 1) != 0 ||
        setenv("DEB_LDFLAGS_MAINT_APPEND", generate_ldflags, 1) != 0) {
        cleanup_work(work);
        fail("cannot set Calculator PGO generation flags");
    }

    write_status("running", 40, "Training Calculator optimisation",
                 "Building the instrumented native Calculator and running its test suite to collect profile data.");
    const char *const build_argv[] = {
        "/usr/bin/dpkg-buildpackage", "-us", "-uc", "-b", NULL
    };
    required_at(source_root, build_argv);

    char built_gui[PATH_LEN];
    if (snprintf(built_gui, sizeof(built_gui), "%s/build/infiltrator-calc", source_root) < (int)sizeof(built_gui) &&
        access(built_gui, X_OK) == 0) {
        write_status("running", 55, "Exercising the Calculator interface",
                     "Running the instrumented GTK interface briefly to add real startup and rendering paths to the PGO profile.");
        (void)setenv("GTK_A11Y", "none", 1);
        const char *const train_argv[] = {
            "/usr/bin/xvfb-run", "-a", "/usr/bin/timeout", "5s", built_gui, NULL
        };
        (void)run(train_argv, true);
    }

    char use_flags[PATH_LEN + 224];
    char use_ldflags[PATH_LEN + 160];
    if (snprintf(use_flags, sizeof(use_flags),
                 "-O3 -march=native -mtune=native -flto=auto -fprofile-use=%s -fprofile-correction -Wno-missing-profile", pgo_dir) >= (int)sizeof(use_flags) ||
        snprintf(use_ldflags, sizeof(use_ldflags),
                 "-flto=auto -fprofile-use=%s -fprofile-correction", pgo_dir) >= (int)sizeof(use_ldflags)) {
        cleanup_work(work);
        fail("Calculator PGO use flags are too long");
    }
    if (setenv("DEB_CXXFLAGS_MAINT_APPEND", use_flags, 1) != 0 ||
        setenv("DEB_LDFLAGS_MAINT_APPEND", use_ldflags, 1) != 0) {
        cleanup_work(work);
        fail("cannot set Calculator PGO use flags");
    }

    write_status("running", 64, "Building the optimised Calculator",
                 "Rebuilding with -O3, CPU-native tuning, LTO and the trained two-pass PGO profile.");
    required_at(source_root, build_argv);

    char cache_path[PATH_LEN];
    if (snprintf(cache_path, sizeof(cache_path), "%s/build/CMakeCache.txt", source_root) >= (int)sizeof(cache_path)) {
        cleanup_work(work);
        fail("Calculator CMake cache path is too long");
    }
    if (!file_contains(cache_path, "-march=native") ||
        !file_contains(cache_path, "-flto=auto") ||
        !file_contains(cache_path, "-fprofile-use=")) {
        cleanup_work(work);
        fail("Calculator final build did not retain the requested native optimisation flags");
    }

    char deb_path[PATH_LEN];
    if (snprintf(deb_path, sizeof(deb_path), "%s/infiltrator-calculator_%s_amd64.deb", work, version) >= (int)sizeof(deb_path) ||
        access(deb_path, R_OK) != 0) {
        cleanup_work(work);
        fail("Calculator final Debian package was not produced");
    }

    write_status("running", 78, "Installing Calculator",
                 "Installing the locally compiled native Calculator package.");
    const char *const install_argv[] = {
        "/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=300",
        "install", "-y", deb_path, NULL
    };
    required(install_argv);

    write_status("running", 88, "Verifying the native Calculator",
                 "Confirming the Infiltrator Calculator package and executable are installed before replacing the distribution calculator.");
    if (!installed("infiltrator-calculator") || access("/usr/bin/infiltrator-calc", X_OK) != 0) {
        cleanup_work(work);
        fail("native Calculator package is not installed correctly after the build");
    }
    write_build_info();

    write_status("running", 94, "Removing replaced Calculator packages",
                 "Cleaning up distribution Calculator packages after the native Infiltrator build succeeded.");
    purge_stock_calculators();

    cleanup_work(work);
    write_status("complete", 100, "Calculator native installation complete",
                 "The latest Calculator is installed and optimised for this computer.");
    puts("Infiltrator OS Calculator replacement completed successfully.");
    close(global_lock_fd);
    close(app_lock_fd);
    return EXIT_SUCCESS;
}
