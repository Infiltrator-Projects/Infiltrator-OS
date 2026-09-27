#define _POSIX_C_SOURCE 200809L
/* Stage the Plymouth theme image and its descriptors in the Debian payload. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int copy_file(const char *source, const char *destination, int png)
{
    static const unsigned char png_signature[8] = {
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'
    };
    unsigned char buffer[16384];
    int input = open(source, O_RDONLY);
    if (input < 0) {
        perror(source);
        return -1;
    }

    if (png) {
        ssize_t count = read(input, buffer, sizeof(png_signature));
        if (count != (ssize_t)sizeof(png_signature) ||
            memcmp(buffer, png_signature, sizeof(png_signature)) != 0 ||
            lseek(input, 0, SEEK_SET) < 0) {
            fprintf(stderr, "%s: invalid PNG signature\n", source);
            close(input);
            return -1;
        }
    }

    int output = open(destination, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (output < 0) {
        perror(destination);
        close(input);
        return -1;
    }

    int failed = 0;
    for (;;) {
        ssize_t count = read(input, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0) {
            perror(source);
            failed = 1;
            break;
        }
        if (count == 0)
            break;
        for (ssize_t offset = 0; offset < count;) {
            ssize_t written = write(output, buffer + offset, (size_t)(count - offset));
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0) {
                perror(destination);
                failed = 1;
                break;
            }
            offset += written;
        }
        if (failed)
            break;
    }
    if (fchmod(output, 0644) < 0) {
        perror(destination);
        failed = 1;
    }
    if (close(output) < 0) {
        perror(destination);
        failed = 1;
    }
    if (close(input) < 0) {
        perror(source);
        failed = 1;
    }
    if (failed)
        unlink(destination);
    return failed ? -1 : 0;
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "Usage: stage-theme THEME_FILE SCRIPT_FILE PNG_FILE DEST_DIR\n");
        return 2;
    }

    static const char *names[] = {
        "infiltrator-os.plymouth", "infiltrator-os.script", "infiltrator-os.png"
    };
    for (int i = 0; i < 3; ++i) {
        char destination[4096];
        int length = snprintf(destination, sizeof(destination), "%s/%s", argv[4], names[i]);
        if (length < 0 || (size_t)length >= sizeof(destination)) {
            fprintf(stderr, "Destination path is too long\n");
            return 1;
        }
        if (copy_file(argv[i + 1], destination, i == 2) < 0)
            return 1;
    }
    return 0;
}
