/* The production player uses ten seconds; shorten only this regression test. */
#define PPO_AUDIO_TIMEOUT_SECONDS 1
#define main ppo_cli_main
#include "../src/main.c"
#undef main
#include <time.h>

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static double monotonic_seconds(void)
{
    struct timespec value;
    REQUIRE(clock_gettime(CLOCK_MONOTONIC, &value) == 0);
    return (double)value.tv_sec + (double)value.tv_nsec / 1000000000.0;
}

static void write_player(const char *path, int stall)
{
    FILE *file = fopen(path, "w");
    REQUIRE(file != NULL);
    REQUIRE(fputs("#!/bin/sh\n[ \"$#\" -eq 2 ] && [ \"$1\" = -- ] || exit 5\n", file) >= 0);
    REQUIRE(fputs(stall ? "exec /usr/bin/sleep 30\n" : "exit 0\n", file) >= 0);
    REQUIRE(fclose(file) == 0);
    REQUIRE(chmod(path, 0700) == 0);
}

int main(int argc, char **argv)
{
    char player[PATH_MAX];
    struct sigaction ignored = { .sa_handler = SIG_IGN }, previous_alarm;
    struct sigaction reaper = { .sa_handler = handle_audio_child };
    sigset_t blocked, previous_mask;
    double started;
    struct timespec pause = { .tv_nsec = 50000000 };
    int attempt;

    REQUIRE(argc == 2);
    REQUIRE(join_path(player, sizeof(player), argv[1], "pw-play") == 0);
    REQUIRE(setenv("PATH", argv[1], 1) == 0);
    write_player(player, 1);

    /* Inherited ignored/blocked SIGALRM must not disable the child's limit. */
    sigemptyset(&ignored.sa_mask);
    REQUIRE(sigaction(SIGALRM, &ignored, &previous_alarm) == 0);
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGALRM);
    REQUIRE(sigprocmask(SIG_BLOCK, &blocked, &previous_mask) == 0);
    started = monotonic_seconds();
    REQUIRE(spawn_audio("test sound.wav", 1) == -ETIMEDOUT);
    REQUIRE(monotonic_seconds() - started < 5.0);
    REQUIRE(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);

    sigemptyset(&reaper.sa_mask);
    REQUIRE(sigaction(SIGCHLD, &reaper, NULL) == 0);
    audio_failure_reported = 0;
    started = monotonic_seconds();
    REQUIRE(spawn_audio("test sound.wav", 0) == 0);
    REQUIRE(monotonic_seconds() - started < 0.5);
    for (attempt = 0; attempt < 80 && !audio_failure_reported; attempt++)
        nanosleep(&pause, NULL);
    REQUIRE(audio_failure_reported == 1);
    REQUIRE(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);

    reaper.sa_handler = SIG_DFL;
    REQUIRE(sigaction(SIGCHLD, &reaper, NULL) == 0);
    write_player(player, 0);
    REQUIRE(spawn_audio("test sound.wav", 1) == 0);
    REQUIRE(unlink(player) == 0);
    REQUIRE(spawn_audio("test sound.wav", 1) == -ENOENT);
    REQUIRE(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);
    REQUIRE(sigaction(SIGALRM, &previous_alarm, NULL) == 0);
    REQUIRE(sigprocmask(SIG_SETMASK, &previous_mask, NULL) == 0);
    puts("Audio timeout, asynchronous reaping, and later playback recovery passed.");
    return 0;
}
