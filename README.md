# remountd

`remountd` is a small daemon that toggles **preconfigured mount points** between
**read-only** and **read-write**, controlled by an **unprivileged client**
(`remountctl`) over a local UNIX socket.

Typical use-case: allow scripts running inside a sandbox (e.g. bubblewrap with
`--remount-ro` / `no_new_privs`) to temporarily enable write access to specific
directories, without restarting the sandbox.

---

## Design goals

- Only remount **explicitly configured** mount points (no arbitrary targets).
- Keep the client unprivileged; perform remounts with elevated privileges.
- Use a **local UNIX domain socket** with peer-credential checks.
- Work well with **systemd socket activation** (recommended), but can also run standalone.

---

## How it works

- `remountctl` connects to `/run/remountd.sock` and sends a simple command:
  - `ro <name>` or `rw <name>`
- `remountd` validates:
  - The caller (via UNIX peer credentials).
  - The requested `<name>` against an allowlist in the config.
- `remountd` performs a remount of the corresponding mount point:
  - For bind mounts this is typically: `mount -o remount,bind,ro|rw <path>`
  - Optionally inside a target mount namespace (e.g. using `nsenter -t <pid> -m`).

---

## Security model

- The daemon runs with the minimum privileges required to remount (usually `CAP_SYS_ADMIN`).
- The socket permissions restrict who can connect.
- The protocol is intentionally tiny and strict.
- Only preconfigured targets are allowed.

**You should treat `remountd` as privileged code**: keep the parser small, avoid
“run arbitrary command” features, and prefer allowlists over patterns.

---

## Usage

### Toggle a target
```sh
remountctl rw codex
remountctl ro codex

### List configured targets
```sh
remountctl list

---

## Configuration
`remountd` uses an allowlist mapping logical names to mount points (and optionally
a mount-namespace PID).

Example (illustrative):
```yaml
# /etc/remountd/config.yaml
socket: /run/remountd.sock

allow:
  codex:
    path: /opt/ext4/nvme2/codex
    # Optional: enter the mount namespace of this PID before remounting.
    # pid: 12345

---

## systemd socket activation (recommended)

`/etc/systemd/system/remountd.socket`
```ini
[Unit]
Description=remountd control socket

[Socket]
ListenStream=/run/remountd.sock
SocketUser=root
SocketGroup=codex
SocketMode=0660
Accept=yes

[Install]
WantedBy=sockets.target
```

`/etc/systemd/system/remountd@.service`
```ini
[Unit]
Description=remountd request handler

[Service]
ExecStart=/usr/bin/remountd --inetd
StandardInput=socket
StandardOutput=socket
StandardError=journal

User=root
Group=root
NoNewPrivileges=yes
CapabilityBoundingSet=CAP_SYS_ADMIN
AmbientCapabilities=CAP_SYS_ADMIN

PrivateTmp=yes
ProtectSystem=strict
ProtectHome=yes
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectControlGroups=yes
RestrictSUIDSGID=yes
LockPersonality=yes
MemoryDenyWriteExecute=yes
RestrictRealtime=yes
SystemCallArchitectures=native
```
Enable it:
```sh
sudo systemctl enable --now remountd.socket
```
Then add allowed users to the socket group (example group `codex`):
```sh
sudo usermod -aG codex <your-user>
```
Log out/in for group membership to take effect.

---

## License

MIT
