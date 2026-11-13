#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Writes update.toml for a release and signs it (docs/updates.md): the version, its release notes
# and every package in <folder> (Open1602-<version>-<platform>.zip or .tar.gz) with its download
# address, size and SHA-256. The signature, Ed25519 over the file's bytes, goes to update.toml.sig
# as 128 hexadecimal digits.
#
#   update-manifest.sh <folder> <tag> <repository> <private key PEM> [<expected public key, hex>]
#
# With the expected public key, the script refuses a private key whose public half differs, so that
# a wrong secret cannot publish update information no copy of Open1602 believes.
set -euo pipefail

folder=$1
tag=$2
repository=$3
key=$4
expected=${5:-}
version=${tag#v}

hex() { od -An -tx1 -v | tr -d ' \n'; }

if [[ -n "$expected" ]]; then
  public=$(openssl pkey -in "$key" -pubout -outform DER | tail -c 32 | hex)
  if [[ "$public" != "$expected" ]]; then
    echo "::error::the signing key's public half $public is not the release key $expected" >&2
    exit 1
  fi
fi

manifest="$folder/update.toml"
{
  echo "# Open1602's update information, signed in update.toml.sig (docs/updates.md)."
  echo "format = 1"
  echo "version = \"$version\""
  echo "notes = \"https://github.com/$repository/releases/tag/$tag\""
  found=0
  for file in "$folder"/Open1602-"$version"-*; do
    name=$(basename "$file")
    platform=${name#Open1602-"$version"-}
    case "$platform" in
      *.tar.gz) platform=${platform%.tar.gz} ;;
      *.zip)    platform=${platform%.zip} ;;
      *)        continue ;;
    esac
    echo
    echo "[$platform]"
    echo "url = \"https://github.com/$repository/releases/download/$tag/$name\""
    echo "size = $(wc -c < "$file" | tr -d ' ')"
    echo "sha256 = \"$(sha256sum "$file" | cut -d' ' -f1)\""
    found=1
  done
  if [[ $found == 0 ]]; then
    echo "::error::no packages of $version in $folder" >&2
    exit 1
  fi
} > "$manifest"

openssl pkeyutl -sign -inkey "$key" -rawin -in "$manifest" | hex > "$manifest.sig"
echo >> "$manifest.sig"
cat "$manifest"
echo "signature: $(cat "$manifest.sig")"
