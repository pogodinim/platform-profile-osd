#ifndef PLATFORM_PROFILE_OSD_CONFIG_H
#define PLATFORM_PROFILE_OSD_CONFIG_H

#include <limits.h>
#include <stddef.h>
#include <stdio.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define PPO_MAX_SOUND_MAPPINGS 64

struct ppo_sound_mapping {
    char profile[128];
    char sound[PATH_MAX];
};

struct ppo_config {
    int notifications_enabled;
    int notify_on_startup;
    int sound_enabled;
    int replace_notifications;
    int notification_timeout_ms;
    char config_path[PATH_MAX];
    char sound_directory[PATH_MAX];
    char default_sound[PATH_MAX];
    struct ppo_sound_mapping mappings[PPO_MAX_SOUND_MAPPINGS];
    size_t mapping_count;
    unsigned int warning_count;
    int file_loaded;
};

int ppo_default_config_path(char *out, size_t out_size);
int ppo_default_sound_directory(char *out, size_t out_size);
void ppo_config_defaults(struct ppo_config *config, const char *config_path);
int ppo_config_load(struct ppo_config *config, FILE *warnings);
int ppo_config_set_mapping(struct ppo_config *config, const char *profile,
                           const char *sound);
int ppo_config_sound_path(const struct ppo_config *config, const char *profile,
                          char *out, size_t out_size);

#endif
