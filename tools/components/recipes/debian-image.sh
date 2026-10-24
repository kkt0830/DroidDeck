#!/usr/bin/env bash
set -euo pipefail
. tools/linuxfs/debian-image.env
image=public.ecr.aws/docker/library/debian:bullseye
repositories=("$GITHUB_REPOSITORY")
if [ "$GITHUB_REPOSITORY" != Droid-Deck/DroidDeck ]; then
  repositories+=(Droid-Deck/DroidDeck)
fi
for repository in "${repositories[@]}"; do
  if gh release download "$DEBIAN_IMAGE_TAG" -R "$repository" -p "$DEBIAN_IMAGE_ASSET" -O /tmp/debian-image.tar.zst --clobber; then
    [ -n "$DEBIAN_IMAGE_SHA256" ]
    echo "$DEBIAN_IMAGE_SHA256  /tmp/debian-image.tar.zst" | sha256sum -c -
    zstd -dc /tmp/debian-image.tar.zst | docker load
    rm -f /tmp/debian-image.tar.zst
    docker image ls "$image"
    exit 0
  fi
done
  echo "::warning::the hosted Debian image could not be fetched; pulling from the registry instead"
  for attempt in 1 2 3 4 5; do
    docker pull "$image" && break
    [ "$attempt" -lt 5 ] || { echo "::error::could not pull $image after $attempt attempts"; exit 1; }
    echo "pull attempt $attempt failed (registry busy?); retrying in $((attempt * 30)) s"
    sleep $((attempt * 30))
  done
docker image ls "$image"
