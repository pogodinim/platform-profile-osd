#!/usr/bin/env python3
"""Recover actual notification code on a private bus; never control host services."""
import os
from pathlib import Path
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time


def require(condition, message):
    if not condition:
        raise AssertionError(message)


class Child:
    def __init__(self, command, env, log):
        self.log = log
        self.buffer = b""
        self.stderr = log.open("wb")
        self.process = subprocess.Popen(command, env=env, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=self.stderr)

    def line(self, timeout=5):
        deadline = time.monotonic() + timeout
        while b"\n" not in self.buffer:
            remaining = deadline - time.monotonic()
            require(remaining > 0, f"Timed out waiting for {self.process.args}")
            readable, _, _ = select.select([self.process.stdout], [], [], remaining)
            require(readable, f"No reply from {self.process.args}")
            data = os.read(self.process.stdout.fileno(), 65536)
            require(data, f"Child exited: {self.process.args}; {self.log.read_text()}")
            self.buffer += data
        line, self.buffer = self.buffer.split(b"\n", 1)
        return line.decode()

    def send(self, command):
        self.process.stdin.write((command + "\n").encode())
        self.process.stdin.flush()

    def stop(self, crash=False):
        if self.process.poll() is None:
            if crash:
                self.process.kill()
            else:
                self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        self.process.stdin.close()
        self.process.stdout.close()
        self.stderr.close()


def run(binary, root):
    address = f"unix:path={root}/bus"
    env = dict(os.environ)
    for key in ("DBUS_STARTER_ADDRESS", "DBUS_STARTER_BUS_TYPE", "DISPLAY", "WAYLAND_DISPLAY"):
        env.pop(key, None)
    for name in ("home", "config", "data", "runtime"):
        (root / name).mkdir(mode=0o700)
    env.update(DBUS_SESSION_BUS_ADDRESS=address, DBUS_SYSTEM_BUS_ADDRESS=address,
               PPO_RECOVERY_TEST_ROOT=str(root), HOME=str(root / "home"),
               XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
               XDG_RUNTIME_DIR=str(root / "runtime"))
    config = root / "bus.conf"
    # No includes, service directories, or systemd activation: this bus cannot
    # start desktop services. Its only clients are our two test processes.
    config.write_text(f"""<busconfig>
  <type>session</type>
  <listen>{address}</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow own="*"/>
    <allow send_destination="*"/>
    <allow receive_sender="*"/>
  </policy>
</busconfig>
""")
    children = []

    def start(command, label):
        child = Child(command, env, root / f"{len(children)}-{label}.log")
        children.append(child)
        return child

    def start_bus():
        # SIGKILL can leave the old private Unix socket behind.
        (root / "bus").unlink(missing_ok=True)
        bus = start([shutil.which("dbus-daemon"), "--nofork", "--nopidfile",
                     "--nosyslog", f"--config-file={config}", "--print-address=1"], "bus")
        require(bus.line().split(",", 1)[0] == address, "Unexpected private bus address")
        return bus

    def start_server(withhold_reply=False):
        server = start([str(binary), "server-hang" if withhold_reply else "server"], "server")
        require(server.line() == "READY", "Notification server did not become ready")
        return server

    def send(profile, available, server=None, replaces_id=0):
        require(client.process.poll() is None, "Notification client died")
        client.send(f"send {profile}")
        reply = client.line()
        require(reply == f"RESULT 0 {0 if available else 1}",
                f"Sending {profile}: expected available={available}, got {reply}")
        if server is not None:
            label = profile.replace("-", " ").title()
            require(server.line() == f"NOTIFY {replaces_id} {label}", "Incorrect notification payload")

    def warnings():
        return client.log.read_text().count("desktop notification failed:")

    try:
        bus = start_bus()
        client = start([str(binary), "client"], "client")
        require(client.line() == "READY", "Notification client did not become ready")
        original_pid = client.process.pid

        send("quiet", False)
        send("balanced", False)
        require(warnings() == 1, "An outage should produce only one warning")
        server = start_server()
        send("performance", True, server)
        print("PASS: unavailable notification service at startup, then recovery", flush=True)

        server.stop(crash=True)
        send("quiet", False)
        send("balanced", False)
        require(warnings() == 2, "A new outage should produce one new warning")
        server = start_server()
        send("performance", True, server)
        print("PASS: notification service crash/restart and warning reset", flush=True)

        server.stop(crash=True)
        server = start_server()
        send("quiet", True, server)
        print("PASS: notification service restart while client is idle", flush=True)

        server.stop(crash=True)
        bus.stop(crash=True)
        send("balanced", False)
        send("performance", False)
        require(warnings() == 3, "Bus outage warning was not suppressed")
        bus = start_bus()
        server = start_server()
        send("quiet", True, server)
        print("PASS: private bus loss, failed sends, and reconnection", flush=True)

        # No send during this outage: the client still holds a stale connection.
        server.stop(crash=True)
        bus.stop(crash=True)
        bus = start_bus()
        server = start_server()
        send("balanced", True, server)
        send("performance", True, server)
        print("PASS: first notification after an idle private-bus restart", flush=True)

        server.stop(crash=True)
        server = start_server(withhold_reply=True)
        send("quiet", False, server)
        require(warnings() == 4, "Timeout must produce one new outage warning")
        require(not server.buffer and not select.select([server.process.stdout], [], [], 0.1)[0],
                "A timed-out notification must not be retransmitted")
        server.stop(crash=True)
        server = start_server()
        send("balanced", True, server)
        print("PASS: bounded timeout without retransmission, then service recovery", flush=True)

        client.send("replace")
        require(client.line() == "REPLACEMENT", "Could not enable replacement in test client")
        send("performance", True, server, replaces_id=1)
        server.stop(crash=True)
        bus.stop(crash=True)
        bus = start_bus()
        server = start_server()
        send("quiet", True, server)
        send("balanced", True, server, replaces_id=1)
        print("PASS: replacement ID resets on bus reconnection", flush=True)

        require(client.process.pid == original_pid and client.process.poll() is None,
                "Recovery must keep the original client alive")
        client.send("quit")
        require(client.process.wait(timeout=3) == 0, "Client did not exit cleanly")
        print("All isolated notification/D-Bus recovery tests passed; client PID unchanged.", flush=True)
    except BaseException:
        for child in children:
            details = child.log.read_text(errors="replace")
            if details:
                print(f"{child.log.name}:\n{details}", file=sys.stderr)
        raise
    finally:
        for child in reversed(children):
            child.stop()


if __name__ == "__main__":
    if len(sys.argv) != 2 or not shutil.which("dbus-daemon"):
        sys.exit("Usage: test-notification-recovery.py BINARY (requires dbus-daemon)")
    binary = Path(sys.argv[1]).resolve(strict=True)
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))
    with tempfile.TemporaryDirectory(prefix="platform-profile-osd-recovery-", dir="/tmp") as temporary:
        run(binary, Path(temporary))
