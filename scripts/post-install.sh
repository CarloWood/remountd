#!/bin/sh

set -eu

if [ "$(id -u)" -ne 0 ]
then
  echo "This script must be run as root." >&2
  echo "Run: sudo scripts/post-install.sh" >&2
  exit 1
fi

if ! getent group remountd >/dev/null
then
  groupadd remountd
fi
