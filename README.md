# remountd

`remountd` is a daemon that toggles preconfigured mount points between
**read-only** and **read-write**, controlled by an unprivileged client
(`remountctl`) over a local UNIX socket.

Typical use-case: allow a process running inside a sandbox (e.g. bubblewrap with
`--bind [--remount-ro]` / `no_new_privs`) to toggle write access to specific directories
without the need to restart the sandbox. This can be used for example to
make sure an A.I. temporarily no longer has write access to a container (e.g.
as part of implementing a "planning mode").

# Arch AUR package

`remountd` is available as an AUR package for Arch here:
https://aur.archlinux.org/packages/remountd

---

## Design goals

- Only remount explicitly configured mount points (no arbitrary targets).
- Keep the client unprivileged; perform remounts with elevated privileges.
- Use a local UNIX domain socket owned by the remountd group (for write access).
- Work with **systemd socket activation** (recommended), but can also run standalone.

---

## How it works

- `remountctl` connects to `/run/remountd/remountd.sock` and sends a simple command:
  - `ro <name> <pid>` or `rw <name> <pid>` (remountctl appends its PID automatically,
    which is used to determine the mount namespace).
- `remountd` validates the requested `<name>` against an allowlist in the config.
- `remountd` performs a remount of the corresponding mount point in the mount namespace
  of <pid> by running:`nsenter -t <pid> -m -- mount -o remount,bind,ro|rw <path>`

---

## Security model

- The daemon runs with the minimum privileges required to remount in a different mount namespace (`CAP_SYS_ADMIN`, `CAP_SYS_PTRACE` and `CAP_SYS_CHROOT`).
- The socket mode permissions restrict who can connect.
- The protocol is intentionally tiny and strict.
- Only preconfigured targets are allowed.

---

## Usage

### Toggle a target
```sh
remountctl rw codex
remountctl ro codex
```

### List configured targets
```sh
remountctl --list
```

---

## Configuration
`remountd` uses an allowlist mapping logical names to mount points (and
a mount-namespace PID).

Example (illustrative):
```yaml
# /etc/remountd/config.yaml
socket: /run/remountd/remountd.sock

allow:
  codex:
    path: /opt/ext4/nvme2/codex
```

---

## Installation from source tree

If you install from the source tree (instead of using a package), run this
after `make install`:

```sh
sudo scripts/post-install.sh
```

This creates the `remountd` group if it does not already exist.

---

## systemd socket activation (recommended)

The files `/etc/systemd/system/remountd.socket` and
`/etc/systemd/system/remountd.service` are provided
as part of the `install` target.

To enable the service:
```sh
sudo systemctl enable --now remountd.socket
```

Then add allowed users to the socket group:
```sh
sudo usermod -aG remountd <your-user>
```

Log out/in for group membership to take effect.

---


## License

MIT
