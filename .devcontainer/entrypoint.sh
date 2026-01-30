#!/bin/bash
# Register APE loader with binfmt_misc for Cosmopolitan binaries
# This needs to run at container startup (not build time) with --privileged

if [ -d /proc/sys/fs/binfmt_misc ] && [ -w /proc/sys/fs/binfmt_misc/register ]; then
    # Copy APE loader to standard location
    cp /opt/cosmos/bin/ape.elf /usr/bin/ape 2>/dev/null || true

    # Register APE formats (ignore errors if already registered)
    echo ':APE:M::MZqFpD::/usr/bin/ape:' >/proc/sys/fs/binfmt_misc/register 2>/dev/null || true
    echo ':APE-jart:M::jartsr::/usr/bin/ape:' >/proc/sys/fs/binfmt_misc/register 2>/dev/null || true
fi

# Execute the command passed to the container
exec "$@"
