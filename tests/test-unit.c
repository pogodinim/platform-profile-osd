#include "config.h"
#include "profile.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

struct callback_result {
    int calls;
    int initial;
    char profile[PPO_PROFILE_MAX];
};

#define EXPECT(condition)                                                        \
    do {                                                                         \
        if (!(condition)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static void test_labels(void)
{
    char label[256];

    ppo_profile_label("low-power", label, sizeof(label));
    EXPECT(strcmp(label, "Low Power") == 0);
    ppo_profile_label("balanced-performance", label, sizeof(label));
    EXPECT(strcmp(label, "Balanced Performance") == 0);
    ppo_profile_label("future_ultra-mode", label, sizeof(label));
    EXPECT(strcmp(label, "Future Ultra Mode") == 0);
    ppo_profile_label("odd---profile", label, sizeof(label));
    EXPECT(strcmp(label, "Odd Profile") == 0);
}

static void test_default_mappings(void)
{
    struct ppo_config config;
    char path[PATH_MAX];

    ppo_config_defaults(&config, "/tmp/ppo-test/config.ini");
    EXPECT(config.notifications_enabled == 1);
    EXPECT(config.notify_on_startup == 0);
    EXPECT(config.sound_enabled == 0);
    EXPECT(config.replace_notifications == 0);
    snprintf(config.sound_directory, sizeof(config.sound_directory), "%s", "/sounds");
    EXPECT(ppo_config_sound_path(&config, "quiet", path, sizeof(path)) == 0);
    EXPECT(strcmp(path, "/sounds/quiet.wav") == 0);
    EXPECT(ppo_config_sound_path(&config, "balanced-performance", path,
                                 sizeof(path)) == 0);
    EXPECT(strcmp(path, "/sounds/performance.wav") == 0);
    EXPECT(ppo_config_sound_path(&config, "custom", path, sizeof(path)) == 0);
    EXPECT(path[0] == '\0');
    EXPECT(ppo_config_sound_path(&config, "future-profile", path, sizeof(path)) == 0);
    EXPECT(path[0] == '\0');
}

static void test_config_parser(void)
{
    static const char contents[] =
        "[general]\n"
        "notifications_enabled = false\n"
        "notify_on_startup = true\n"
        "sound_enabled = false\n"
        "replace_notifications = false\n"
        "notification_timeout_ms = 2500\n"
        "sound_directory = ../my sounds\n"
        "\n"
        "[sounds]\n"
        "quiet = custom quiet.wav\n"
        "balanced =\n"
        "future-profile = /opt/audio/future.wav\n"
        "default = generic.wav\n";
    char template[] = "/tmp/platform-profile-osd-config-XXXXXX";
    struct ppo_config config;
    char path[PATH_MAX];
    int fd = mkstemp(template);
    ssize_t written;

    EXPECT(fd >= 0);
    if (fd < 0)
        return;
    written = write(fd, contents, sizeof(contents) - 1);
    EXPECT(written == (ssize_t)(sizeof(contents) - 1));
    close(fd);

    ppo_config_defaults(&config, template);
    EXPECT(ppo_config_load(&config, NULL) == 0);
    EXPECT(config.file_loaded == 1);
    EXPECT(config.warning_count == 0);
    EXPECT(config.notifications_enabled == 0);
    EXPECT(config.notify_on_startup == 1);
    EXPECT(config.sound_enabled == 0);
    EXPECT(config.replace_notifications == 0);
    EXPECT(config.notification_timeout_ms == 2500);
    EXPECT(strcmp(config.sound_directory, "/tmp/../my sounds") == 0);
    EXPECT(ppo_config_sound_path(&config, "quiet", path, sizeof(path)) == 0);
    EXPECT(strcmp(path, "/tmp/../my sounds/custom quiet.wav") == 0);
    EXPECT(ppo_config_sound_path(&config, "balanced", path, sizeof(path)) == 0);
    EXPECT(path[0] == '\0');
    EXPECT(ppo_config_sound_path(&config, "future-profile", path, sizeof(path)) == 0);
    EXPECT(strcmp(path, "/opt/audio/future.wav") == 0);
    EXPECT(ppo_config_sound_path(&config, "unknown-profile", path, sizeof(path)) == 0);
    EXPECT(strcmp(path, "/tmp/../my sounds/generic.wav") == 0);
    unlink(template);
}

static void test_invalid_config_is_nonfatal(void)
{
    static const char contents[] =
        "[general]\n"
        "notify_on_startup = perhaps\n"
        "sound_enabled = perhaps\n"
        "notification_timeout_ms = forever\n"
        "unknown = value\n"
        "[sounds]\n"
        "bad profile = nope.wav\n";
    char template[] = "/tmp/platform-profile-osd-invalid-XXXXXX";
    struct ppo_config config;
    int fd = mkstemp(template);

    EXPECT(fd >= 0);
    if (fd < 0)
        return;
    EXPECT(write(fd, contents, sizeof(contents) - 1) ==
           (ssize_t)(sizeof(contents) - 1));
    close(fd);

    ppo_config_defaults(&config, template);
    EXPECT(ppo_config_load(&config, NULL) == 0);
    EXPECT(config.notify_on_startup == 0);
    EXPECT(config.sound_enabled == 0);
    EXPECT(config.notification_timeout_ms == 1800);
    EXPECT(config.warning_count == 5);
    unlink(template);
}

static void test_profile_files(void)
{
    static const char contents[] = "future-profile\n";
    char template[] = "/tmp/platform-profile-osd-profile-XXXXXX";
    char value[PPO_PROFILE_MAX];
    int fd = mkstemp(template);

    EXPECT(fd >= 0);
    if (fd < 0)
        return;
    EXPECT(write(fd, contents, sizeof(contents) - 1) ==
           (ssize_t)(sizeof(contents) - 1));
    close(fd);
    EXPECT(ppo_read_profile(template, value, sizeof(value)) == 0);
    EXPECT(strcmp(value, "future-profile") == 0);
    unlink(template);
    EXPECT(ppo_read_profile(template, value, sizeof(value)) == -ENOENT);
}

static int record_profile(const char *profile, int initial, void *userdata)
{
    struct callback_result *result = userdata;

    result->calls++;
    result->initial = initial;
    snprintf(result->profile, sizeof(result->profile), "%s", profile);
    return 0;
}

static void test_unknown_profile_startup(void)
{
    static const char contents[] = "future-ultra-mode\n";
    char template[] = "/tmp/platform-profile-osd-watch-XXXXXX";
    struct callback_result callback = {0};
    volatile sig_atomic_t stop = 0;
    int fd = mkstemp(template);

    EXPECT(fd >= 0);
    if (fd < 0)
        return;
    EXPECT(write(fd, contents, sizeof(contents) - 1) ==
           (ssize_t)(sizeof(contents) - 1));
    close(fd);

    /* A regular file cannot be registered for sysfs EPOLLPRI, but the initial
     * callback still exercises unknown-profile startup behavior. */
    EXPECT(ppo_watch_profile(template, record_profile, &callback, &stop) == -EPERM);
    EXPECT(callback.calls == 1);
    EXPECT(callback.initial == 1);
    EXPECT(strcmp(callback.profile, "future-ultra-mode") == 0);
    unlink(template);
}

int main(void)
{
    test_labels();
    test_default_mappings();
    test_config_parser();
    test_invalid_config_is_nonfatal();
    test_profile_files();
    test_unknown_profile_startup();

    if (failures) {
        fprintf(stderr, "%d unit test(s) failed\n", failures);
        return 1;
    }
    puts("All unit tests passed.");
    return 0;
}
