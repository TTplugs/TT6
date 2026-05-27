#!/usr/bin/env bash
# build_tt6.sh - SSH-driven build & verify on the aarch64 VM.
#
# Usage:
#   ./scripts/build_tt6.sh user@vm-host             # build + check
#   ./scripts/build_tt6.sh user@vm-host deploy [IP] # build + check + scp to Unraid share
#
# Expects the VM to already have git, gcc/g++, make installed and
# read access to https://github.com/TTplugs/TT6.git.

set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 user@vm-host [deploy IP_UNRAID]"
    exit 1
fi

VM="$1"
ACTION="${2:-}"
UNRAID_IP="${3:-}"

REMOTE_DIR="\$HOME/TT6"
REPO_URL="https://github.com/TTplugs/TT6.git"

echo "==> Building TT6 on $VM"
ssh "$VM" "set -e
    if [ ! -d $REMOTE_DIR ]; then
        git clone $REPO_URL $REMOTE_DIR
    else
        cd $REMOTE_DIR && git pull --ff-only
    fi
    cd $REMOTE_DIR
    make clean
    make arm64
    echo '--- ABI check ---'
    make check
"

if [ "$ACTION" = "deploy" ]; then
    if [ -z "$UNRAID_IP" ]; then
        echo "deploy mode requires IP_UNRAID as 3rd arg" >&2
        exit 1
    fi
    echo "==> Deploying TT6.lv2 from $VM to Unraid [$UNRAID_IP]"
    ssh "$VM" "set -e
        ssh -o StrictHostKeyChecking=accept-new root@$UNRAID_IP 'rm -rf \"/mnt/user/Musica/Desarrollo LV2/TT6.lv2\"'
        scp -r $REMOTE_DIR/TT6.lv2 root@$UNRAID_IP:'/mnt/user/Musica/Desarrollo LV2/'
    "
    echo "==> Deployed. Re-scan plugins on the S2400."
fi
