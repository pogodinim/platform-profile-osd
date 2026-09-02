#ifndef PLATFORM_PROFILE_OSD_PROFILE_H
#define PLATFORM_PROFILE_OSD_PROFILE_H

#include <signal.h>
#include <stddef.h>

#define PPO_PROFILE_MAX 128
#define PPO_CHOICES_MAX 1024

typedef int (*ppo_profile_callback)(const char *profile, int initial, void *userdata);

int ppo_read_profile(const char *path, char *out, size_t out_size);
int ppo_read_choices(const char *path, char *out, size_t out_size);
void ppo_profile_label(const char *profile, char *out, size_t out_size);
int ppo_watch_profile(const char *path, ppo_profile_callback callback,
                      void *userdata, volatile sig_atomic_t *stop_requested);

#endif
