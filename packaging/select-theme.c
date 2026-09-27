#define _POSIX_C_SOURCE 200809L
/* v1.0.0 installation and removal operations in C. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define THEME "infiltrator-os"
#define FILE_THEME "/usr/share/plymouth/themes/infiltrator-os/infiltrator-os.plymouth"
#define FILE_PNG "/usr/share/plymouth/themes/infiltrator-os/infiltrator-os.png"
#define FILE_CONF "/etc/plymouth/plymouthd.conf"
#define FILE_GRUB "/etc/default/grub"

static void optional(char *const args[])
{
    pid_t child = fork();
    if (child == 0) {
        execvp(args[0], args);
        _exit(127);
    }
    if (child < 0) return;
    int status;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
}

/* Atomically update a key. For Plymouth, insert it inside [Daemon]. */
static int update(const char *path, const char *key, const char *value,
                  int daemon_section, int *changed)
{
    FILE *input = fopen(path, "r");
    if (!input && errno != ENOENT) return -1;
    struct stat st;
    mode_t mode = input && fstat(fileno(input), &st) == 0 ? st.st_mode & 07777 : 0644;
    char *temporary = malloc(strlen(path) + 16);
    if (!temporary) { if (input) fclose(input); return -1; }
    sprintf(temporary, "%s.tmpXXXXXX", path);
    int fd = mkstemp(temporary);
    if (fd < 0) { free(temporary); if (input) fclose(input); return -1; }
    FILE *output = fdopen(fd, "w");
    if (!output) { close(fd); unlink(temporary); free(temporary); if (input) fclose(input); return -1; }
    int error = fchmod(fd, mode) != 0;
    int in_daemon = !daemon_section, seen_section = 0, written = 0;
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    while (input && !error && (length = getline(&line, &capacity, input)) >= 0) {
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (daemon_section && *p == '[') {
            if (in_daemon && !written) {
                if (fprintf(output, "%s=%s\n", key, value) < 0) error = 1;
                written = 1;
                *changed = 1;
            }
            in_daemon = strncmp(p, "[Daemon]", 8) == 0 &&
                        (p[8] == '\n' || p[8] == '\r' || p[8] == '\0');
            if (in_daemon) seen_section = 1;
        }
        int match = in_daemon && !written && strncmp(p, key, strlen(key)) == 0;
        if (match) {
            p += strlen(key);
            while (*p == ' ' || *p == '\t') ++p;
            match = *p == '=';
        }
        if (match) {
            if (fprintf(output, "%s=%s\n", key, value) < 0) error = 1;
            ++p;
            while (*p == ' ' || *p == '\t') ++p;
            size_t old_length = strcspn(p, "\r\n");
            if (old_length != strlen(value) || strncmp(p, value, old_length)) *changed = 1;
            written = 1;
        } else if (fwrite(line, 1, (size_t)length, output) != (size_t)length) error = 1;
    }
    if (input && ferror(input)) error = 1;
    if (!error && !written) {
        if (daemon_section && !seen_section && fprintf(output, "\n[Daemon]\n") < 0) error = 1;
        if (!error && fprintf(output, "%s=%s\n", key, value) < 0) error = 1;
        *changed = 1;
    }
    free(line);
    if (input && fclose(input) != 0) error = 1;
    if (fflush(output) != 0) error = 1;
    if (!error && fsync(fd) != 0) error = 1;
    if (fclose(output) != 0) error = 1;
    if (!error && *changed && rename(temporary, path) != 0) error = 1;
    if (error || !*changed) unlink(temporary);
    free(temporary);
    return error ? -1 : 0;
}

static int contains(const char *s, const char *word)
{
    size_t n = strlen(word);
    while (*s) {
        while (*s == ' ' || *s == '\t') ++s;
        if (strncmp(s, word, n) == 0 && (s[n] == 0 || s[n] == ' ' || s[n] == '\t')) return 1;
        while (*s && *s != ' ' && *s != '\t') ++s;
    }
    return 0;
}

static int grub(void)
{
    FILE *file = fopen(FILE_GRUB, "r");
    if (!file) return errno == ENOENT ? 0 : -1;
    char *line = NULL, *current = NULL;
    size_t capacity = 0;
    const char prefix[] = "GRUB_CMDLINE_LINUX_DEFAULT=\"";
    while (getline(&line, &capacity, file) >= 0) {
        if (strncmp(line, prefix, sizeof(prefix) - 1)) continue;
        char *start = line + sizeof(prefix) - 1;
        char *end = strchr(start, '"');
        if (end) { *end = 0; current = strdup(start); }
        break;
    }
    int error = ferror(file);
    fclose(file);
    free(line);
    if (error) { free(current); return -1; }
    if (!current) current = strdup("");
    if (!current) return -1;
    int quiet = contains(current, "quiet"), splash = contains(current, "splash");
    if (quiet && splash) { free(current); return 0; }
    char *value = malloc(strlen(current) + 32);
    if (!value) { free(current); return -1; }
    sprintf(value, "\"%s%s%s%s%s\"", current,
            *current ? " " : "", quiet ? "" : "quiet",
            !quiet && !splash ? " " : "", splash ? "" : "splash");
    int changed = 0, result = update(FILE_GRUB, "GRUB_CMDLINE_LINUX_DEFAULT", value, 0, &changed);
    free(current);
    free(value);
    if (!result && changed) {
        char *const args[] = { "update-grub", NULL };
        optional(args);
    }
    return result;
}

int main(int argc, char **argv)
{
    if (argc == 2 && (!strcmp(argv[1], "remove") || !strcmp(argv[1], "deconfigure"))) {
        char *const args[] = { "update-alternatives", "--remove", "default.plymouth", FILE_THEME, NULL };
        optional(args);
        return 0;
    }
    if (argc != 2 || strcmp(argv[1], "configure")) return 2;
    if (access(FILE_THEME, R_OK) || access(FILE_PNG, R_OK)) {
        fprintf(stderr, "Infiltrator OS theme or picture is missing\n");
        return 1;
    }
    if (mkdir("/etc/plymouth", 0755) && errno != EEXIST) return 1;
    int changed = 0;
    if (update(FILE_CONF, "Theme", THEME, 1, &changed)) { perror(FILE_CONF); return 1; }
    char *const debian[] = { "plymouth-set-default-theme", THEME, NULL };
    char *const add[] = { "update-alternatives", "--install",
        "/usr/share/plymouth/themes/default.plymouth", "default.plymouth", FILE_THEME, "250", NULL };
    char *const select[] = { "update-alternatives", "--set", "default.plymouth", FILE_THEME, NULL };
    optional(debian);
    optional(add);
    optional(select);
    if (grub()) { perror(FILE_GRUB); return 1; }
    char *const initramfs[] = { "update-initramfs", "-u", "-k", "all", NULL };
    optional(initramfs);
    return 0;
}
