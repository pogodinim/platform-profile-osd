#include "profile.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

static int read_text_fd(int fd, char *out, size_t out_size)
{
    ssize_t count;
    size_t length;

    if (out_size < 2)
        return -EINVAL;

    if (lseek(fd, 0, SEEK_SET) < 0)
        return -errno;

    do {
        count = read(fd, out, out_size - 1);
    } while (count < 0 && errno == EINTR);

    if (count < 0)
        return -errno;
    if (count == 0)
        return -ENODATA;

    out[count] = '\0';
    length = (size_t)count;
    while (length > 0 && isspace((unsigned char)out[length - 1]))
        out[--length] = '\0';

    if (length == 0)
        return -ENODATA;

    return 0;
}

static int read_text_path(const char *path, char *out, size_t out_size)
{
    int fd;
    int result;

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -errno;

    result = read_text_fd(fd, out, out_size);
    close(fd);
    return result;
}

int ppo_read_profile(const char *path, char *out, size_t out_size)
{
    return read_text_path(path, out, out_size);
}

int ppo_read_choices(const char *path, char *out, size_t out_size)
{
    return read_text_path(path, out, out_size);
}

void ppo_profile_label(const char *profile, char *out, size_t out_size)
{
    size_t source = 0;
    size_t dest = 0;
    int capitalize = 1;

    if (out_size == 0)
        return;

    while (profile[source] != '\0' && dest + 1 < out_size) {
        unsigned char ch = (unsigned char)profile[source++];

        if (ch == '-' || ch == '_' || isspace(ch)) {
            if (dest > 0 && out[dest - 1] != ' ' && dest + 1 < out_size)
                out[dest++] = ' ';
            capitalize = 1;
            continue;
        }

        if (!isprint(ch))
            ch = '?';

        if (capitalize && isalpha(ch))
            ch = (unsigned char)toupper(ch);
        else if (isalpha(ch))
            ch = (unsigned char)tolower(ch);

        out[dest++] = (char)ch;
        capitalize = 0;
    }

    while (dest > 0 && out[dest - 1] == ' ')
        dest--;
    out[dest] = '\0';
}

int ppo_watch_profile(const char *path, ppo_profile_callback callback,
                      void *userdata, volatile sig_atomic_t *stop_requested)
{
    struct epoll_event interest = {0};
    struct epoll_event event = {0};
    char current[PPO_PROFILE_MAX];
    char previous[PPO_PROFILE_MAX];
    int profile_fd = -1;
    int epoll_fd = -1;
    int result;

    profile_fd = open(path, O_RDONLY | O_CLOEXEC);
    if (profile_fd < 0)
        return -errno;

    result = read_text_fd(profile_fd, current, sizeof(current));
    if (result < 0)
        goto done;

    snprintf(previous, sizeof(previous), "%s", current);
    result = callback(current, 1, userdata);
    if (result != 0)
        goto done;

    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        result = -errno;
        goto done;
    }

    /* kernfs/sysfs change notifications are delivered as priority events. */
    interest.events = EPOLLPRI | EPOLLERR;
    interest.data.fd = profile_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, profile_fd, &interest) < 0) {
        result = -errno;
        goto done;
    }

    result = 0;
    while (!*stop_requested) {
        int ready = epoll_wait(epoll_fd, &event, 1, -1);

        if (ready < 0) {
            if (errno == EINTR)
                continue;
            result = -errno;
            break;
        }

        if (!(event.events & (EPOLLPRI | EPOLLERR))) {
            if (event.events & EPOLLHUP) {
                result = -ENODEV;
                break;
            }
            continue;
        }

        result = read_text_fd(profile_fd, current, sizeof(current));
        if (result < 0)
            break;

        if (strcmp(current, previous) == 0)
            continue;

        snprintf(previous, sizeof(previous), "%s", current);
        result = callback(current, 0, userdata);
        if (result != 0)
            break;
    }

done:
    if (epoll_fd >= 0)
        close(epoll_fd);
    if (profile_fd >= 0)
        close(profile_fd);
    return result;
}
