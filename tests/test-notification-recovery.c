/* Exercise the production notification callback without watching real sysfs. */
#define main ppo_cli_main
#include "../src/main.c"
#undef main

struct mock_server {
    uint32_t next_id;
    int withhold_reply;
};

static int receive_notification(sd_bus_message *message, void *userdata,
                                 sd_bus_error *error)
{
    const char *application, *icon, *summary, *body;
    uint32_t replaces_id;
    struct mock_server *server = userdata;
    int result;

    (void)error;
    result = sd_bus_message_read(message, "susss", &application, &replaces_id,
                                 &icon, &summary, &body);
    if (result < 0)
        return result;
    if (strcmp(application, "platform-profile-osd") != 0 ||
        strcmp(summary, PPO_APP_NAME) != 0 || icon[0] != '\0')
        return -EINVAL;
    printf("NOTIFY %u %s\n", replaces_id, body);
    if (server->withhold_reply)
        return 1;
    return sd_bus_reply_method_return(message, "u", server->next_id++);
}

static int serve_notifications(int withhold_reply)
{
    static const sd_bus_vtable vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("Notify", "susssasa{sv}i", "u", receive_notification,
                       SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_VTABLE_END
    };
    sd_bus *bus = NULL;
    struct mock_server server = { .next_id = 1, .withhold_reply = withhold_reply };
    int result = sd_bus_open_user(&bus);

    if (result < 0)
        goto done;
    result = sd_bus_add_object_vtable(bus, NULL, PPO_BUS_PATH,
                                      PPO_BUS_INTERFACE, vtable, &server);
    if (result < 0)
        goto done;
    result = sd_bus_request_name(bus, PPO_BUS_NAME, 0);
    if (result < 0)
        goto done;
    puts("READY");
    for (;;) {
        result = sd_bus_process(bus, NULL);
        if (result < 0)
            break;
        if (result > 0)
            continue;
        result = sd_bus_wait(bus, UINT64_MAX);
        if (result < 0)
            break;
    }
done:
    sd_bus_flush_close_unref(bus);
    if (result < 0)
        fprintf(stderr, "mock notification server: %s\n", strerror(-result));
    return result < 0 ? 1 : 0;
}

static int run_notification_client(void)
{
    struct app_state state = {0};
    char command[128];

    /* Do not load user configuration, initialize sysfs, or start audio. */
    ppo_config_defaults(&state.config, NULL);
    puts("READY");
    while (fgets(command, sizeof(command), stdin)) {
        int result;
        command[strcspn(command, "\r\n")] = '\0';
        if (strcmp(command, "quit") == 0)
            break;
        if (strcmp(command, "replace") == 0) {
            state.config.replace_notifications = 1;
            puts("REPLACEMENT");
            continue;
        }
        if (strncmp(command, "send ", 5) != 0) {
            notifier_close(&state.notifier);
            return 2;
        }
        result = profile_changed(command + 5, 0, &state);
        printf("RESULT %d %d\n", result, state.notifier.warned_unavailable);
    }
    notifier_close(&state.notifier);
    return ferror(stdin) ? 1 : 0;
}

int main(int argc, char **argv)
{
    const char *root = getenv("PPO_RECOVERY_TEST_ROOT");
    const char *address = getenv("DBUS_SESSION_BUS_ADDRESS");
    char expected[PATH_MAX];
    struct stat status;
    int length;

    /* Refuse standalone use against an inherited desktop session bus. */
    if (!root || strncmp(root, "/tmp/platform-profile-osd-recovery-", 35) != 0 ||
        !address || lstat(root, &status) < 0 || !S_ISDIR(status.st_mode) ||
        status.st_uid != geteuid() || (status.st_mode & 077) != 0) {
        fputs("A private recovery-test directory and bus are required.\n", stderr);
        return 2;
    }
    length = snprintf(expected, sizeof(expected), "unix:path=%s/bus", root);
    if (length < 0 || (size_t)length >= sizeof(expected) ||
        strcmp(address, expected) != 0) {
        fputs("Refusing a bus outside the private test directory.\n", stderr);
        return 2;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc == 2 && strcmp(argv[1], "server") == 0)
        return serve_notifications(0);
    if (argc == 2 && strcmp(argv[1], "server-hang") == 0)
        return serve_notifications(1);
    if (argc == 2 && strcmp(argv[1], "client") == 0)
        return run_notification_client();
    return 2;
}
