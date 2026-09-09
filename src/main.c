#include "config.h"
#include "profile.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

#ifndef PPO_VERSION
#define PPO_VERSION "0.1.0"
#endif

#define PPO_APP_NAME "Platform Profile"
#define PPO_BUS_NAME "org.freedesktop.Notifications"
#define PPO_BUS_PATH "/org/freedesktop/Notifications"
#define PPO_BUS_INTERFACE "org.freedesktop.Notifications"
#define PPO_DBUS_TIMEOUT_USEC (2ULL * 1000ULL * 1000ULL)
#define PPO_LOCK_FILENAME "platform-profile-osd.lock"

#ifndef PPO_AUDIO_TIMEOUT_SECONDS
#define PPO_AUDIO_TIMEOUT_SECONDS 10
#endif

struct app_paths {
    char profile[PATH_MAX];
    char choices[PATH_MAX];
    char service[PATH_MAX];
    char autostart[PATH_MAX];
    char executable[PATH_MAX];
};

struct notifier {
    sd_bus *bus;
    uint32_t notification_id;
    int warned_unavailable;
};

struct app_state {
    struct ppo_config config;
    struct app_paths paths;
    struct notifier notifier;
    int verbose;
    int warned_audio_backend;
    int warned_sound_file;
};

enum action {
    ACTION_DAEMON,
    ACTION_CHECK,
    ACTION_PRINT_PROFILE,
    ACTION_PRINT_CHOICES,
    ACTION_TEST_NOTIFICATION,
    ACTION_TEST_SOUND,
    ACTION_VERSION,
    ACTION_HELP,
};

struct options {
    enum action action;
    const char *config_path;
    const char *test_sound_profile;
    int sound_override;
    int verbose;
};

static volatile sig_atomic_t stop_requested;
static volatile sig_atomic_t audio_failure_reported;

static void log_message(const char *level, const char *format, ...)
{
    va_list arguments;

    fprintf(stderr, "platform-profile-osd: %s", level);
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
}

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static void handle_audio_child(int signal_number)
{
    static const char warning[] =
        "platform-profile-osd: warning: pw-play exited with an error; audio remains optional\n";
    int saved_errno = errno;
    int status;
    pid_t child;

    (void)signal_number;
    while ((child = waitpid(-1, &status, WNOHANG)) > 0) {
        (void)child;
        if ((!WIFEXITED(status) || WEXITSTATUS(status) != 0) &&
            !audio_failure_reported) {
            ssize_t ignored = write(STDERR_FILENO, warning, sizeof(warning) - 1);
            (void)ignored;
            audio_failure_reported = 1;
        }
    }
    errno = saved_errno;
}

static int copy_string(char *out, size_t out_size, const char *value)
{
    int written = snprintf(out, out_size, "%s", value);
    return written < 0 || (size_t)written >= out_size ? -ENAMETOOLONG : 0;
}

static int join_path(char *out, size_t out_size, const char *left,
                     const char *right)
{
    int written = snprintf(out, out_size, "%s%s%s", left,
                           left[0] && left[strlen(left) - 1] == '/' ? "" : "/",
                           right);
    return written < 0 || (size_t)written >= out_size ? -ENAMETOOLONG : 0;
}

static int xdg_base(char *out, size_t out_size, const char *variable,
                    const char *fallback_suffix)
{
    const char *base = getenv(variable);

    if (base && base[0])
        return copy_string(out, out_size, base);

    {
        const char *home = getenv("HOME");
        if (!home || !home[0])
            return -ENOENT;
        return join_path(out, out_size, home, fallback_suffix);
    }
}

static int init_paths(struct app_paths *paths, const char *argv0)
{
    const char *sysfs_dir = getenv("PLATFORM_PROFILE_OSD_SYSFS_DIR");
    char config_base[PATH_MAX];
    ssize_t length;
    int result;

    if (!sysfs_dir || !sysfs_dir[0])
        sysfs_dir = "/sys/firmware/acpi";
    if ((result = join_path(paths->profile, sizeof(paths->profile), sysfs_dir,
                            "platform_profile")) < 0 ||
        (result = join_path(paths->choices, sizeof(paths->choices), sysfs_dir,
                            "platform_profile_choices")) < 0)
        return result;

    result = xdg_base(config_base, sizeof(config_base), "XDG_CONFIG_HOME",
                      ".config");
    if (result < 0)
        return result;
    if ((result = join_path(paths->service, sizeof(paths->service), config_base,
                            "systemd/user/platform-profile-osd.service")) < 0 ||
        (result = join_path(paths->autostart, sizeof(paths->autostart), config_base,
                            "autostart/platform-profile-osd.desktop")) < 0)
        return result;

    length = readlink("/proc/self/exe", paths->executable,
                      sizeof(paths->executable) - 1);
    if (length >= 0) {
        paths->executable[length] = '\0';
    } else if (copy_string(paths->executable, sizeof(paths->executable), argv0) < 0) {
        return -ENAMETOOLONG;
    }
    return 0;
}

static const char *display_path(const char *path, char *out, size_t out_size)
{
    const char *home = getenv("HOME");
    size_t home_length;

    if (!home || !home[0])
        return path;
    home_length = strlen(home);
    if (strncmp(path, home, home_length) != 0 ||
        (path[home_length] != '/' && path[home_length] != '\0'))
        return path;
    if (snprintf(out, out_size, "~%s", path + home_length) < 0)
        return path;
    return out;
}

static int regular_readable_file(const char *path)
{
    struct stat status;
    return stat(path, &status) == 0 && S_ISREG(status.st_mode) &&
           access(path, R_OK) == 0;
}

static int command_exists(const char *command)
{
    const char *path;
    const char *start;

    if (strchr(command, '/'))
        return access(command, X_OK) == 0;

    path = getenv("PATH");
    if (!path)
        return 0;
    start = path;
    while (1) {
        const char *end = strchr(start, ':');
        size_t length = end ? (size_t)(end - start) : strlen(start);
        char candidate[PATH_MAX];
        int written;

        if (length == 0)
            written = snprintf(candidate, sizeof(candidate), "./%s", command);
        else
            written = snprintf(candidate, sizeof(candidate), "%.*s/%s",
                               (int)length, start, command);
        if (written > 0 && (size_t)written < sizeof(candidate) &&
            access(candidate, X_OK) == 0)
            return 1;

        if (!end)
            break;
        start = end + 1;
    }
    return 0;
}

static int acquire_daemon_lock(char *path, size_t path_size, int *lock_fd)
{
    const char *runtime_directory = getenv("XDG_RUNTIME_DIR");
    struct stat status;
    int fd;
    int result;

    if (!runtime_directory || !runtime_directory[0])
        return -ENOENT;
    result = join_path(path, path_size, runtime_directory, PPO_LOCK_FILENAME);
    if (result < 0)
        return result;

    fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return -errno;
    if (fstat(fd, &status) < 0) {
        result = -errno;
        close(fd);
        return result;
    }
    if (!S_ISREG(status.st_mode) || status.st_uid != geteuid()) {
        close(fd);
        return -EPERM;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        result = -errno;
        close(fd);
        return result;
    }

    *lock_fd = fd;
    return 0;
}

static void notifier_close(struct notifier *notifier)
{
    notifier->bus = sd_bus_unref(notifier->bus);
    notifier->notification_id = 0;
}

static int notifier_connect(struct notifier *notifier)
{
    int result;

    if (notifier->bus)
        return 0;
    result = sd_bus_open_user(&notifier->bus);
    if (result < 0)
        notifier->bus = NULL;
    return result;
}

static int notifier_probe(struct notifier *notifier, char *server,
                          size_t server_size)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    const char *name = NULL;
    const char *vendor = NULL;
    const char *version = NULL;
    const char *spec_version = NULL;
    int result;

    result = notifier_connect(notifier);
    if (result < 0)
        return result;

    result = sd_bus_call_method(notifier->bus, PPO_BUS_NAME, PPO_BUS_PATH,
                                PPO_BUS_INTERFACE, "GetServerInformation",
                                &error, &reply, "");
    if (result < 0)
        goto done;

    result = sd_bus_message_read(reply, "ssss", &name, &vendor, &version,
                                 &spec_version);
    if (result < 0)
        goto done;
    (void)vendor;
    (void)spec_version;
    if (snprintf(server, server_size, "%s%s%s", name ? name : "unknown",
                 version && version[0] ? " " : "", version ? version : "") < 0)
        result = -EINVAL;
    else
        result = 0;

done:
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    return result;
}

static int notifier_send_once(struct notifier *notifier, const char *body,
                              int timeout_ms, int replace)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *message = NULL;
    sd_bus_message *reply = NULL;
    uint32_t id = replace ? notifier->notification_id : 0;
    int result;

    result = notifier_connect(notifier);
    if (result < 0)
        goto done;

    result = sd_bus_message_new_method_call(notifier->bus, &message,
                                            PPO_BUS_NAME, PPO_BUS_PATH,
                                            PPO_BUS_INTERFACE, "Notify");
    if (result < 0)
        goto done;
    result = sd_bus_message_append(message, "susss", "platform-profile-osd",
                                   id, "", PPO_APP_NAME, body);
    if (result < 0)
        goto done;
    result = sd_bus_message_open_container(message, 'a', "s");
    if (result < 0)
        goto done;
    result = sd_bus_message_close_container(message);
    if (result < 0)
        goto done;
    result = sd_bus_message_open_container(message, 'a', "{sv}");
    if (result < 0)
        goto done;
    result = sd_bus_message_close_container(message);
    if (result < 0)
        goto done;
    result = sd_bus_message_append(message, "i", (int32_t)timeout_ms);
    if (result < 0)
        goto done;

    result = sd_bus_call(notifier->bus, message, PPO_DBUS_TIMEOUT_USEC,
                         &error, &reply);
    if (result < 0)
        goto done;
    result = sd_bus_message_read(reply, "u", &id);
    if (result >= 0) {
        notifier->notification_id = id;
        notifier->warned_unavailable = 0;
        result = 0;
    }

done:
    sd_bus_error_free(&error);
    sd_bus_message_unref(message);
    sd_bus_message_unref(reply);
    return result;
}

static int notifier_send(struct notifier *notifier, const char *body,
                         int timeout_ms, int replace)
{
    int result = 0;
    int attempt;

    /* A bus can restart while we are idle. Retry this notification once on a
     * fresh connection instead of losing the first profile change afterward.
     * Timeouts and server errors are not retried: delivery may have occurred. */
    for (attempt = 0; attempt < 2; attempt++) {
        result = notifier_send_once(notifier, body, timeout_ms, replace);
        if (result != -ECONNRESET && result != -ENOTCONN && result != -EPIPE &&
            result != -ESHUTDOWN)
            break;
        notifier_close(notifier);
    }
    return result;
}

static int spawn_audio(const char *path, int wait_for_exit)
{
    int error_pipe[2];
    int exec_error = 0;
    pid_t child;
    char *const arguments[] = {"pw-play", "--", (char *)path, NULL};
    int result;
    ssize_t count;

    if (pipe2(error_pipe, O_CLOEXEC) < 0)
        return -errno;
    child = fork();
    if (child < 0) {
        result = -errno;
        close(error_pipe[0]);
        close(error_pipe[1]);
        return result;
    }
    if (child == 0) {
        struct sigaction action = { .sa_handler = SIG_DFL };
        sigset_t signals;
        int null_fd;

        close(error_pipe[0]);
        sigemptyset(&action.sa_mask);
        sigemptyset(&signals);
        sigaddset(&signals, SIGALRM);
        if (sigaction(SIGALRM, &action, NULL) < 0 ||
            sigprocmask(SIG_UNBLOCK, &signals, NULL) < 0)
            goto child_failed;
        null_fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (null_fd < 0)
            goto child_failed;
        if (dup2(null_fd, STDOUT_FILENO) < 0 ||
            dup2(null_fd, STDERR_FILENO) < 0)
            goto child_failed;
        if (null_fd > STDERR_FILENO)
            close(null_fd);
        /* The one-shot alarm survives exec and belongs only to this player.
         * A stalled backend must not accumulate permanent audio processes. */
        alarm(PPO_AUDIO_TIMEOUT_SECONDS);
        execvp(arguments[0], arguments);
child_failed:
        exec_error = errno;
        count = write(error_pipe[1], &exec_error, sizeof(exec_error));
        (void)count;
        _exit(127);
    }
    close(error_pipe[1]);
    do {
        count = read(error_pipe[0], &exec_error, sizeof(exec_error));
    } while (count < 0 && errno == EINTR);
    result = count < 0 ? -errno : count > 0 ? -exec_error : 0;
    close(error_pipe[0]);
    if (result < 0) {
        if (wait_for_exit)
            while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {}
        return result;
    }
    if (!wait_for_exit)
        return 0;

    while (waitpid(child, &result, 0) < 0) {
        if (errno != EINTR)
            return -errno;
    }
    if (WIFSIGNALED(result) && WTERMSIG(result) == SIGALRM)
        return -ETIMEDOUT;
    if (!WIFEXITED(result) || WEXITSTATUS(result) != 0)
        return -EIO;
    return 0;
}

static int play_profile_sound(struct app_state *state, const char *profile,
                              int force)
{
    char path[PATH_MAX];
    int result;

    if (!force && !state->config.sound_enabled)
        return 0;

    result = ppo_config_sound_path(&state->config, profile, path, sizeof(path));
    if (result < 0) {
        if (!state->warned_sound_file) {
            log_message("warning: ", "cannot resolve configured sound for '%s': %s",
                        profile, strerror(-result));
            state->warned_sound_file = 1;
        }
        return result;
    }
    if (path[0] == '\0')
        return 0;

    if (!regular_readable_file(path)) {
        if (!state->warned_sound_file) {
            log_message("warning: ", "sound file is unavailable: %s", path);
            state->warned_sound_file = 1;
        }
        return -ENOENT;
    }
    if (!command_exists("pw-play")) {
        if (!state->warned_audio_backend) {
            log_message("warning: ",
                        "audio feedback is unavailable (pw-play not found)");
            state->warned_audio_backend = 1;
        }
        return -ENOENT;
    }

    result = spawn_audio(path, force);
    if (result < 0 && !state->warned_audio_backend) {
        log_message("warning: ", "audio playback failed: %s", strerror(-result));
        state->warned_audio_backend = 1;
    }
    if (result == 0 && state->verbose)
        log_message("", "started sound feedback: %s", path);
    return result;
}

static int profile_changed(const char *profile, int initial, void *userdata)
{
    struct app_state *state = userdata;
    char label[PPO_PROFILE_MAX * 2];
    int result;

    ppo_profile_label(profile, label, sizeof(label));
    if (state->verbose)
        log_message("", "%s profile: %s (raw: %s)",
                    initial ? "initial" : "active", label, profile);

    if (initial && !state->config.notify_on_startup)
        return 0;

    if (state->config.notifications_enabled) {
        result = notifier_send(&state->notifier, label,
                               state->config.notification_timeout_ms,
                               state->config.replace_notifications);
        if (result < 0 && !state->notifier.warned_unavailable) {
            log_message("warning: ", "desktop notification failed: %s",
                        strerror(-result));
            state->notifier.warned_unavailable = 1;
        }
    }

    /* Startup is visual-only; audio is reserved for actual profile changes. */
    if (!initial)
        play_profile_sound(state, profile, 0);
    return 0;
}

static void trim_output(char *output)
{
    size_t length = strlen(output);
    while (length > 0 && (output[length - 1] == '\n' ||
                          output[length - 1] == '\r' ||
                          output[length - 1] == ' ' ||
                          output[length - 1] == '\t'))
        output[--length] = '\0';
}

static int capture_command(char *const arguments[], char *output,
                           size_t output_size)
{
    int descriptors[2];
    pid_t child;
    size_t used = 0;
    int status;

    if (output_size == 0)
        return -EINVAL;
    output[0] = '\0';
    if (pipe2(descriptors, O_CLOEXEC) < 0)
        return -errno;

    child = fork();
    if (child < 0) {
        int saved_errno = errno;
        close(descriptors[0]);
        close(descriptors[1]);
        return -saved_errno;
    }
    if (child == 0) {
        int null_fd;
        dup2(descriptors[1], STDOUT_FILENO);
        null_fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (null_fd >= 0)
            dup2(null_fd, STDERR_FILENO);
        close(descriptors[0]);
        close(descriptors[1]);
        execvp(arguments[0], arguments);
        _exit(127);
    }

    close(descriptors[1]);
    while (used + 1 < output_size) {
        ssize_t count = read(descriptors[0], output + used,
                             output_size - used - 1);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        used += (size_t)count;
    }
    output[used] = '\0';
    close(descriptors[0]);
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR)
            return -errno;
    }
    trim_output(output);
    if (!WIFEXITED(status))
        return -EIO;
    return WEXITSTATUS(status);
}

static void print_file_capability(const char *name, const char *path)
{
    printf("  %-17s %s\n", name, regular_readable_file(path) ? "available" : "missing");
}

static int any_configured_sound_exists(const struct ppo_config *config)
{
    char path[PATH_MAX];
    size_t index;

    for (index = 0; index < config->mapping_count; index++) {
        if (ppo_config_sound_path(config, config->mappings[index].profile,
                                  path, sizeof(path)) == 0 &&
            path[0] && regular_readable_file(path))
            return 1;
    }
    if (config->default_sound[0] &&
        ppo_config_sound_path(config, "/", path,
                              sizeof(path)) == 0 &&
        path[0] && regular_readable_file(path))
        return 1;
    return 0;
}

static int run_check(struct app_state *state)
{
    char current[PPO_PROFILE_MAX] = "";
    char choices[PPO_CHOICES_MAX] = "";
    char server[256] = "";
    char displayed[PATH_MAX + 2];
    char quiet_path[PATH_MAX];
    char balanced_path[PATH_MAX];
    char performance_path[PATH_MAX];
    char service_enabled[128] = "unavailable";
    char service_active[128] = "unavailable";
    char *const enabled_arguments[] = {"systemctl", "--user", "is-enabled",
                                       "platform-profile-osd.service", NULL};
    char *const active_arguments[] = {"systemctl", "--user", "is-active",
                                      "platform-profile-osd.service", NULL};
    int profile_result = ppo_read_profile(state->paths.profile, current,
                                          sizeof(current));
    int choices_result = ppo_read_choices(state->paths.choices, choices,
                                          sizeof(choices));
    int bus_result = notifier_connect(&state->notifier);
    int notification_result = bus_result < 0 ? bus_result :
                              notifier_probe(&state->notifier, server,
                                             sizeof(server));
    int pw_play_available = command_exists("pw-play");
    int any_mapped_sound;
    int service_installed = regular_readable_file(state->paths.service);
    int autostart_installed = regular_readable_file(state->paths.autostart);
    const char *runtime_directory = getenv("XDG_RUNTIME_DIR");
    int runtime_directory_available =
        runtime_directory && runtime_directory[0] &&
        access(runtime_directory, W_OK | X_OK) == 0;

    printf("platform-profile-osd %s compatibility report\n\n", PPO_VERSION);
    printf("Kernel interface\n");
    printf("  Platform profile:  %s\n", profile_result == 0 ? "available" : "unavailable");
    printf("  Profile path:      %s\n", state->paths.profile);
    printf("  Current profile:   %s\n", profile_result == 0 ? current : strerror(-profile_result));
    printf("  Choices path:      %s\n", state->paths.choices);
    printf("  Available choices: %s\n", choices_result == 0 ? choices : strerror(-choices_result));

    printf("\nNotifications\n");
    printf("  Configuration:     %s\n",
           state->config.notifications_enabled ? "enabled" : "disabled");
    printf("  Notify on startup: %s\n",
           state->config.notify_on_startup ? "yes" : "no");
    printf("  Replace previous:  %s\n",
           state->config.replace_notifications ? "yes" : "no");
    printf("  Session D-Bus:     %s\n", bus_result == 0 ? "available" : "unavailable");
    if (notification_result == 0)
        printf("  Notification API:  available (%s)\n", server);
    else {
        printf("  Notification API:  unavailable\n");
        printf("  Notification error: %s\n", strerror(-notification_result));
    }

    join_path(quiet_path, sizeof(quiet_path), state->config.sound_directory,
              "quiet.wav");
    join_path(balanced_path, sizeof(balanced_path), state->config.sound_directory,
              "balanced.wav");
    join_path(performance_path, sizeof(performance_path),
              state->config.sound_directory, "performance.wav");
    any_mapped_sound = any_configured_sound_exists(&state->config);
    printf("\nAudio (optional)\n");
    printf("  Configuration:     %s\n", state->config.sound_enabled ? "enabled" : "disabled");
    printf("  pw-play:           %s\n", pw_play_available ? "available" : "unavailable (not found)");
    print_file_capability("quiet.wav:", quiet_path);
    print_file_capability("balanced.wav:", balanced_path);
    print_file_capability("performance.wav:", performance_path);
    printf("  Audio feedback:    %s\n",
           !state->config.sound_enabled ? "disabled by configuration" :
           pw_play_available && any_mapped_sound ? "available" :
           !pw_play_available ? "unavailable (pw-play not found; visual notifications still work)" :
                                "unavailable (no mapped sound files found; visual notifications still work)");

    if (command_exists("systemctl")) {
        int enabled_result = capture_command(enabled_arguments, service_enabled,
                                             sizeof(service_enabled));
        int active_result = capture_command(active_arguments, service_active,
                                            sizeof(service_active));
        if (enabled_result < 0)
            copy_string(service_enabled, sizeof(service_enabled), "unavailable");
        else if (!service_enabled[0])
            copy_string(service_enabled, sizeof(service_enabled),
                        enabled_result == 0 ? "enabled" : "unavailable");
        if (active_result < 0)
            copy_string(service_active, sizeof(service_active), "unavailable");
        else if (!service_active[0])
            copy_string(service_active, sizeof(service_active),
                        active_result == 0 ? "active" : "unavailable");
    }
    printf("\nSession startup\n");
    printf("  Runtime directory: %s\n",
           runtime_directory_available ? "available" :
           "unavailable (daemon single-instance lock cannot be created)");
    printf("  systemd service:   %s\n", service_installed ? "installed" : "not installed");
    printf("  Service enabled:   %s\n", service_enabled);
    printf("  Service running:   %s\n", service_active);
    printf("  XDG autostart:     %s\n", autostart_installed ? "installed" : "not installed");

    printf("\nPaths\n");
    printf("  Executable:        %s\n",
           display_path(state->paths.executable, displayed, sizeof(displayed)));
    printf("  Configuration:     %s%s\n",
           display_path(state->config.config_path, displayed, sizeof(displayed)),
           state->config.file_loaded ? " (loaded)" : " (defaults; file not found)");
    printf("  Sound directory:   %s\n",
           display_path(state->config.sound_directory, displayed, sizeof(displayed)));
    printf("  Service file:      %s\n",
           display_path(state->paths.service, displayed, sizeof(displayed)));
    printf("  Autostart file:    %s\n",
           display_path(state->paths.autostart, displayed, sizeof(displayed)));
    if (state->config.warning_count)
        printf("\nConfiguration warnings: %u\n", state->config.warning_count);

    return profile_result == 0 && notification_result == 0 &&
           runtime_directory_available ? 0 : 1;
}

static void print_usage(FILE *stream)
{
    fprintf(stream,
            "Usage: platform-profile-osd [OPTIONS]\n"
            "\n"
            "Monitor Linux platform-profile changes and show desktop notifications.\n"
            "\n"
            "Actions:\n"
            "  --check                    Print a paste-friendly compatibility report\n"
            "  --print-profile            Print the current raw kernel profile\n"
            "  --print-choices            Print profiles exposed by the kernel\n"
            "  --test-notification        Send a desktop test notification\n"
            "  --test-sound PROFILE       Play the sound mapped to PROFILE\n"
            "  --version                  Print the version\n"
            "  --help                     Show this help\n"
            "\n"
            "Options:\n"
            "  --config PATH              Use a different configuration file\n"
            "  --no-sound                 Disable sound for this invocation\n"
            "  --sound                    Enable sound for this invocation\n"
            "  --verbose                  Log normal profile changes\n");
}

static int set_action(struct options *options, enum action action)
{
    if (options->action != ACTION_DAEMON && options->action != action)
        return -EINVAL;
    options->action = action;
    return 0;
}

static int parse_options(int argc, char **argv, struct options *options)
{
    int index;

    memset(options, 0, sizeof(*options));
    options->sound_override = -1;
    for (index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--check") == 0) {
            if (set_action(options, ACTION_CHECK) < 0)
                return -EINVAL;
        } else if (strcmp(argv[index], "--print-profile") == 0) {
            if (set_action(options, ACTION_PRINT_PROFILE) < 0)
                return -EINVAL;
        } else if (strcmp(argv[index], "--print-choices") == 0) {
            if (set_action(options, ACTION_PRINT_CHOICES) < 0)
                return -EINVAL;
        } else if (strcmp(argv[index], "--test-notification") == 0) {
            if (set_action(options, ACTION_TEST_NOTIFICATION) < 0)
                return -EINVAL;
        } else if (strcmp(argv[index], "--test-sound") == 0) {
            if (++index >= argc || set_action(options, ACTION_TEST_SOUND) < 0)
                return -EINVAL;
            options->test_sound_profile = argv[index];
        } else if (strcmp(argv[index], "--version") == 0) {
            if (set_action(options, ACTION_VERSION) < 0)
                return -EINVAL;
        } else if (strcmp(argv[index], "--help") == 0 ||
                   strcmp(argv[index], "-h") == 0) {
            if (set_action(options, ACTION_HELP) < 0)
                return -EINVAL;
        } else if (strcmp(argv[index], "--config") == 0) {
            if (++index >= argc)
                return -EINVAL;
            options->config_path = argv[index];
        } else if (strcmp(argv[index], "--no-sound") == 0) {
            options->sound_override = 0;
        } else if (strcmp(argv[index], "--sound") == 0) {
            options->sound_override = 1;
        } else if (strcmp(argv[index], "--verbose") == 0 ||
                   strcmp(argv[index], "--debug") == 0) {
            options->verbose = 1;
        } else {
            fprintf(stderr, "platform-profile-osd: unknown option: %s\n", argv[index]);
            return -EINVAL;
        }
    }
    return 0;
}

static int run_daemon(struct app_state *state)
{
    struct sigaction action = {0};
    struct sigaction child_action = {0};
    char choices[PPO_CHOICES_MAX];
    char lock_path[PATH_MAX];
    int lock_fd = -1;
    int result;

    result = acquire_daemon_lock(lock_path, sizeof(lock_path), &lock_fd);
    if (result == -EWOULDBLOCK || result == -EAGAIN) {
        log_message("", "another daemon instance is already running; exiting");
        return 0;
    }
    if (result < 0) {
        log_message("error: ", "cannot acquire daemon lock %s: %s",
                    result == -ENAMETOOLONG ? PPO_LOCK_FILENAME :
                    getenv("XDG_RUNTIME_DIR") ? lock_path :
                    "$XDG_RUNTIME_DIR/" PPO_LOCK_FILENAME,
                    strerror(-result));
        return 1;
    }

    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    child_action.sa_handler = handle_audio_child;
    sigemptyset(&child_action.sa_mask);
    sigaction(SIGCHLD, &child_action, NULL);

    result = ppo_read_choices(state->paths.choices, choices, sizeof(choices));
    if (result == 0)
        log_message("", "monitoring %s (choices: %s)", state->paths.profile,
                    choices);
    else
        log_message("warning: ", "cannot read profile choices: %s",
                    strerror(-result));

    result = ppo_watch_profile(state->paths.profile, profile_changed, state,
                               &stop_requested);
    if (result < 0)
        log_message("error: ", "profile monitoring stopped: %s",
                    strerror(-result));
    else if (state->verbose)
        log_message("", "stopped");
    close(lock_fd);
    return result < 0 ? 1 : 0;
}

int main(int argc, char **argv)
{
    struct options options;
    struct app_state state = {0};
    char value[PPO_CHOICES_MAX];
    int result;
    int exit_status = 0;

    result = parse_options(argc, argv, &options);
    if (result < 0) {
        print_usage(stderr);
        return 2;
    }
    if (options.action == ACTION_HELP) {
        print_usage(stdout);
        return 0;
    }
    if (options.action == ACTION_VERSION) {
        printf("platform-profile-osd %s\n", PPO_VERSION);
        return 0;
    }

    result = init_paths(&state.paths, argv[0]);
    if (result < 0) {
        log_message("error: ", "cannot determine runtime paths: %s",
                    strerror(-result));
        return 1;
    }
    ppo_config_defaults(&state.config, options.config_path);
    result = ppo_config_load(&state.config, stderr);
    if (result < 0)
        log_message("warning: ", "continuing with available configuration values");
    if (options.sound_override >= 0)
        state.config.sound_enabled = options.sound_override;
    state.verbose = options.verbose;

    switch (options.action) {
    case ACTION_CHECK:
        exit_status = run_check(&state);
        break;
    case ACTION_PRINT_PROFILE:
        result = ppo_read_profile(state.paths.profile, value, sizeof(value));
        if (result < 0) {
            log_message("error: ", "cannot read %s: %s", state.paths.profile,
                        strerror(-result));
            exit_status = 1;
        } else {
            puts(value);
        }
        break;
    case ACTION_PRINT_CHOICES:
        result = ppo_read_choices(state.paths.choices, value, sizeof(value));
        if (result < 0) {
            log_message("error: ", "cannot read %s: %s", state.paths.choices,
                        strerror(-result));
            exit_status = 1;
        } else {
            puts(value);
        }
        break;
    case ACTION_TEST_NOTIFICATION:
        if (state.config.replace_notifications)
            result = notifier_send(&state.notifier,
                                   "Testing notification replacement…",
                                   state.config.notification_timeout_ms, 1);
        else
            result = 0;
        if (result == 0)
            result = notifier_send(&state.notifier, "Notifications are working",
                                   state.config.notification_timeout_ms,
                                   state.config.replace_notifications);
        if (result < 0) {
            log_message("error: ", "test notification failed: %s",
                        strerror(-result));
            exit_status = 1;
        } else {
            puts("Test notification sent.");
        }
        break;
    case ACTION_TEST_SOUND:
        result = play_profile_sound(&state, options.test_sound_profile, 1);
        if (result < 0) {
            exit_status = 1;
        } else {
            char sound_path[PATH_MAX];
            ppo_config_sound_path(&state.config, options.test_sound_profile,
                                  sound_path, sizeof(sound_path));
            if (sound_path[0])
                printf("Started sound mapped to '%s'.\n",
                       options.test_sound_profile);
            else {
                printf("No sound is mapped to '%s'.\n",
                       options.test_sound_profile);
                exit_status = 1;
            }
        }
        break;
    case ACTION_DAEMON:
        exit_status = run_daemon(&state);
        break;
    default:
        exit_status = 2;
        break;
    }

    notifier_close(&state.notifier);
    return exit_status;
}
