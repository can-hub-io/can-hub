#!/bin/sh
# Builds the signed APT repository served at apt.can-hub.io from a directory of
# .deb files, keeping every version so hub and agents can be pinned to one.
# Signs with the default key of the current gpg keyring and exports its public
# half as key.gpg next to dists/ and pool/.
set -eu

ARCHITECTURES="amd64 arm64 armhf"

if [ $# -ne 2 ]; then
    echo "usage: $0 <deb-dir> <out-dir>" >&2
    exit 2
fi
debs=$1
out=$2
dist=dists/stable

mkdir -p "$out/pool/main"
cp "$debs"/*.deb "$out/pool/main/"
cd "$out"

for arch in $ARCHITECTURES; do
    mkdir -p "$dist/main/binary-$arch"
    apt-ftparchive --arch "$arch" packages pool > "$dist/main/binary-$arch/Packages"
    gzip -9kf "$dist/main/binary-$arch/Packages"
done

apt-ftparchive \
    -o APT::FTPArchive::Release::Origin=can-hub \
    -o APT::FTPArchive::Release::Label=can-hub \
    -o APT::FTPArchive::Release::Suite=stable \
    -o APT::FTPArchive::Release::Codename=stable \
    -o APT::FTPArchive::Release::Components=main \
    -o "APT::FTPArchive::Release::Architectures=$ARCHITECTURES" \
    release "$dist" > Release
mv Release "$dist/Release"
gpg --batch --yes --clearsign -o "$dist/InRelease" "$dist/Release"
gpg --batch --yes --armor --detach-sign -o "$dist/Release.gpg" "$dist/Release"
gpg --export > key.gpg
