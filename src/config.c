#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int copy_string(char *out, size_t out_size, const char *value)
{
    int written;

    if (out_size == 0)
        return -ENAMETOOLONG;
    written = snprintf(out, out_size, "%s", value);
    return written < 0 || (size_t)written >= out_size ? -ENAMETOOLONG : 0;
}

static int join_path(char *out, size_t out_size, const char *left,
                     const char *right)
{
    int written = snprintf(out, out_size, "%s%s%s", left,
                           left[0] != '\0' && left[strlen(left) - 1] == '/' ? "" : "/",
                           right);
    return written < 0 || (size_t)written >= out_size ? -ENAMETOOLONG : 0;
}

static int xdg_path(char *out, size_t out_size, const char *variable,
                    const char *home_suffix, const char *project_suffix)
{
    const char *base = getenv(variable);
    char fallback[PATH_MAX];
    int result;

    if (!base || base[0] == '\0') {
        const char *home = getenv("HOME");
        if (!home || home[0] == '\0')
            return -ENOENT;
        result = join_path(fallback, sizeof(fallback), home, home_suffix);
        if (result < 0)
            return result;
        base = fallback;
    }

    return join_path(out, out_size, base, project_suffix);
}

int ppo_default_config_path(char *out, size_t out_size)
{
    return xdg_path(out, out_size, "XDG_CONFIG_HOME", ".config",
                    "platform-profile-osd/config.ini");
}

int ppo_default_sound_directory(char *out, size_t out_size)
{
    return xdg_path(out, out_size, "XDG_DATA_HOME", ".local/share",
                    "platform-profile-osd/sounds");
}

int ppo_config_set_mapping(struct ppo_config *config, const char *profile,
                           const char *sound)
{
    size_t index;

    for (index = 0; index < config->mapping_count; index++) {
        if (strcmp(config->mappings[index].profile, profile) == 0)
            return copy_string(config->mappings[index].sound,
                               sizeof(config->mappings[index].sound), sound);
    }

    if (config->mapping_count >= PPO_MAX_SOUND_MAPPINGS)
        return -E2BIG;

    index = config->mapping_count++;
    if (copy_string(config->mappings[index].profile,
                    sizeof(config->mappings[index].profile), profile) < 0 ||
        copy_string(config->mappings[index].sound,
                    sizeof(config->mappings[index].sound), sound) < 0) {
        config->mapping_count--;
        return -ENAMETOOLONG;
    }
    return 0;
}

void ppo_config_defaults(struct ppo_config *config, const char *config_path)
{
    memset(config, 0, sizeof(*config));
    config->notifications_enabled = 1;
    config->notify_on_startup = 0;
    config->sound_enabled = 0;
    config->replace_notifications = 0;
    config->notification_timeout_ms = 1800;

    if (config_path)
        copy_string(config->config_path, sizeof(config->config_path), config_path);
    else
        ppo_default_config_path(config->config_path, sizeof(config->config_path));
    ppo_default_sound_directory(config->sound_directory,
                                sizeof(config->sound_directory));

    ppo_config_set_mapping(config, "low-power", "quiet.wav");
    ppo_config_set_mapping(config, "cool", "quiet.wav");
    ppo_config_set_mapping(config, "quiet", "quiet.wav");
    ppo_config_set_mapping(config, "balanced", "balanced.wav");
    ppo_config_set_mapping(config, "balanced-performance", "performance.wav");
    ppo_config_set_mapping(config, "performance", "performance.wav");
}

static char *trim(char *value)
{
    char *end;

    while (isspace((unsigned char)*value))
        value++;
    if (*value == '\0')
        return value;

    end = value + strlen(value) - 1;
    while (end > value && isspace((unsigned char)*end))
        *end-- = '\0';
    return value;
}

static char *unquote(char *value)
{
    size_t length = strlen(value);

    if (length >= 2 && ((value[0] == '"' && value[length - 1] == '"') ||
                        (value[0] == '\'' && value[length - 1] == '\''))) {
        value[length - 1] = '\0';
        return value + 1;
    }
    return value;
}

static int parse_boolean(const char *value, int *out)
{
    if (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
        strcasecmp(value, "yes") == 0 || strcasecmp(value, "on") == 0) {
        *out = 1;
        return 0;
    }
    if (strcasecmp(value, "false") == 0 || strcmp(value, "0") == 0 ||
        strcasecmp(value, "no") == 0 || strcasecmp(value, "off") == 0) {
        *out = 0;
        return 0;
    }
    return -EINVAL;
}

static int valid_profile_key(const char *key)
{
    const unsigned char *cursor = (const unsigned char *)key;

    if (*cursor == '\0')
        return 0;
    for (; *cursor; cursor++) {
        if (!isalnum(*cursor) && *cursor != '-' && *cursor != '_' &&
            *cursor != '.')
            return 0;
    }
    return 1;
}

static void config_warning(struct ppo_config *config, FILE *warnings,
                           unsigned long line_number, const char *message)
{
    config->warning_count++;
    if (warnings)
        fprintf(warnings, "platform-profile-osd: %s:%lu: %s\n",
                config->config_path, line_number, message);
}

static int config_directory(const char *path, char *out, size_t out_size)
{
    const char *slash = strrchr(path, '/');
    size_t length;

    if (!slash)
        return copy_string(out, out_size, ".");
    if (slash == path)
        return copy_string(out, out_size, "/");
    length = (size_t)(slash - path);
    if (length + 1 > out_size)
        return -ENAMETOOLONG;
    memcpy(out, path, length);
    out[length] = '\0';
    return 0;
}

static int expand_config_path(const struct ppo_config *config, const char *value,
                              char *out, size_t out_size)
{
    if (value[0] == '/')
        return copy_string(out, out_size, value);
    if (value[0] == '~' && value[1] == '/') {
        const char *home = getenv("HOME");
        if (!home || home[0] == '\0')
            return -ENOENT;
        return join_path(out, out_size, home, value + 2);
    }

    {
        char directory[PATH_MAX];
        int result = config_directory(config->config_path, directory,
                                      sizeof(directory));
        if (result < 0)
            return result;
        return join_path(out, out_size, directory, value);
    }
}

int ppo_config_load(struct ppo_config *config, FILE *warnings)
{
    enum { SECTION_NONE, SECTION_GENERAL, SECTION_SOUNDS } section = SECTION_NONE;
    char line[8192];
    unsigned long line_number = 0;
    FILE *file;

    errno = 0;
    file = fopen(config->config_path, "re");
    if (!file) {
        if (errno == ENOENT)
            return 0;
        if (warnings)
            fprintf(warnings, "platform-profile-osd: cannot read config %s: %s\n",
                    config->config_path, strerror(errno));
        config->warning_count++;
        return -errno;
    }
    config->file_loaded = 1;

    while (fgets(line, sizeof(line), file)) {
        char *key;
        char *value;
        char *equals;

        line_number++;
        key = trim(line);
        if (*key == '\0' || *key == '#' || *key == ';')
            continue;

        if (*key == '[') {
            size_t length = strlen(key);
            if (length < 3 || key[length - 1] != ']') {
                config_warning(config, warnings, line_number,
                               "invalid section header; line ignored");
                section = SECTION_NONE;
                continue;
            }
            key[length - 1] = '\0';
            key++;
            if (strcasecmp(key, "general") == 0)
                section = SECTION_GENERAL;
            else if (strcasecmp(key, "sounds") == 0)
                section = SECTION_SOUNDS;
            else {
                config_warning(config, warnings, line_number,
                               "unknown section; section ignored");
                section = SECTION_NONE;
            }
            continue;
        }

        equals = strchr(key, '=');
        if (!equals) {
            config_warning(config, warnings, line_number,
                           "expected key = value; line ignored");
            continue;
        }
        *equals = '\0';
        value = unquote(trim(equals + 1));
        key = trim(key);

        if (section == SECTION_GENERAL) {
            int parsed;

            if (strcmp(key, "notifications_enabled") == 0) {
                if (parse_boolean(value, &config->notifications_enabled) < 0)
                    config_warning(config, warnings, line_number,
                                   "invalid notifications_enabled value");
            } else if (strcmp(key, "notify_on_startup") == 0) {
                if (parse_boolean(value, &config->notify_on_startup) < 0)
                    config_warning(config, warnings, line_number,
                                   "invalid notify_on_startup value");
            } else if (strcmp(key, "sound_enabled") == 0) {
                if (parse_boolean(value, &config->sound_enabled) < 0)
                    config_warning(config, warnings, line_number,
                                   "invalid sound_enabled value");
            } else if (strcmp(key, "replace_notifications") == 0) {
                if (parse_boolean(value, &config->replace_notifications) < 0)
                    config_warning(config, warnings, line_number,
                                   "invalid replace_notifications value");
            } else if (strcmp(key, "notification_timeout_ms") == 0) {
                char *end = NULL;
                long timeout;
                errno = 0;
                timeout = strtol(value, &end, 10);
                if (errno || end == value || *end != '\0' || timeout < -1 ||
                    timeout > 60000) {
                    config_warning(config, warnings, line_number,
                                   "notification_timeout_ms must be -1..60000");
                } else {
                    config->notification_timeout_ms = (int)timeout;
                }
            } else if (strcmp(key, "sound_directory") == 0) {
                char expanded[PATH_MAX];
                parsed = value[0] == '\0' ? -EINVAL :
                         expand_config_path(config, value, expanded,
                                            sizeof(expanded));
                if (parsed < 0 || copy_string(config->sound_directory,
                                              sizeof(config->sound_directory),
                                              expanded) < 0)
                    config_warning(config, warnings, line_number,
                                   "invalid or too-long sound_directory");
            } else {
                config_warning(config, warnings, line_number,
                               "unknown general setting; line ignored");
            }
        } else if (section == SECTION_SOUNDS) {
            if (strcmp(key, "default") == 0) {
                if (copy_string(config->default_sound,
                                sizeof(config->default_sound), value) < 0)
                    config_warning(config, warnings, line_number,
                                   "default sound path is too long");
            } else if (!valid_profile_key(key)) {
                config_warning(config, warnings, line_number,
                               "invalid profile name; mapping ignored");
            } else if (ppo_config_set_mapping(config, key, value) < 0) {
                config_warning(config, warnings, line_number,
                               "sound mapping is too long or mapping limit reached");
            }
        } else {
            config_warning(config, warnings, line_number,
                           "setting outside a known section; line ignored");
        }
    }

    if (ferror(file)) {
        int saved_errno = errno ? errno : EIO;
        fclose(file);
        return -saved_errno;
    }
    fclose(file);
    return 0;
}

int ppo_config_sound_path(const struct ppo_config *config, const char *profile,
                          char *out, size_t out_size)
{
    const char *sound = config->default_sound;
    size_t index;

    for (index = 0; index < config->mapping_count; index++) {
        if (strcmp(config->mappings[index].profile, profile) == 0) {
            sound = config->mappings[index].sound;
            break;
        }
    }

    if (sound[0] == '\0') {
        if (out_size > 0)
            out[0] = '\0';
        return 0;
    }
    if (sound[0] == '/')
        return copy_string(out, out_size, sound);
    if (sound[0] == '~' && sound[1] == '/') {
        const char *home = getenv("HOME");
        if (!home || home[0] == '\0')
            return -ENOENT;
        return join_path(out, out_size, home, sound + 2);
    }
    return join_path(out, out_size, config->sound_directory, sound);
}
