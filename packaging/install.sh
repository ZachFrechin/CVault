#!/usr/bin/env sh

# Install the latest VaultCLI release for macOS or Linux.
# The release archive and SHA256SUMS.txt are published by GitHub Actions.

set -eu

repository="${VAULT_REPOSITORY:-ZachFrechin/CVault}"
requested_version="${VAULT_VERSION:-latest}"
if [ "$#" -gt 1 ]; then
    printf '%s\n' 'Usage: install.sh [version]' >&2
    exit 2
fi
if [ "$#" -eq 1 ]; then
    requested_version="$1"
fi

fail()
{
    printf 'Installation impossible : %s\n' "$1" >&2
    exit 1
}

download()
{
    curl --fail --silent --show-error --location --retry 3 \
        --proto '=https' --tlsv1.2 "$1" --output "$2"
}

command -v curl >/dev/null 2>&1 || fail 'curl est requis.'
command -v tar >/dev/null 2>&1 || fail 'tar est requis.'

case "$(uname -s)" in
    Darwin)
        platform='macos'
        ;;
    Linux)
        platform='linux'
        ;;
    *)
        fail 'Cette installation prend en charge macOS et Linux.'
        ;;
esac

case "$(uname -m)" in
    x86_64|amd64)
        architecture='x64'
        ;;
    arm64|aarch64)
        architecture='arm64'
        ;;
    *)
        fail "Architecture non prise en charge : $(uname -m)"
        ;;
esac

if [ "$requested_version" = 'latest' ]; then
    release_url="https://github.com/${repository}/releases/latest/download"
else
    case "$requested_version" in
        v*) release_ref="$requested_version" ;;
        *) release_ref="v${requested_version}" ;;
    esac
    case "$release_ref" in
        *[!A-Za-z0-9._-]*|'')
            fail 'La version demandee contient des caracteres invalides.'
            ;;
    esac
    release_url="https://github.com/${repository}/releases/download/${release_ref}"
fi

asset="vault-${platform}-${architecture}.tar.gz"
home_directory="${HOME:-}"
[ -n "$home_directory" ] || fail 'La variable HOME est absente.'
install_directory="${VAULT_INSTALL_DIR:-${home_directory}/.local/bin}"

temporary_directory="$(mktemp -d "${TMPDIR:-/tmp}/vault-install.XXXXXXXX")"
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM

archive_path="${temporary_directory}/${asset}"
checksums_path="${temporary_directory}/SHA256SUMS.txt"
download "${release_url}/${asset}" "$archive_path" || fail 'Archive introuvable dans la release.'
download "${release_url}/SHA256SUMS.txt" "$checksums_path" || fail 'Sommes de controle introuvables.'

expected_checksum="$(awk -v asset="$asset" '$2 == asset { print $1; exit }' "$checksums_path")"
[ -n "$expected_checksum" ] || fail "Aucune somme de controle pour ${asset}."

if command -v sha256sum >/dev/null 2>&1; then
    actual_checksum="$(sha256sum "$archive_path" | awk '{ print $1 }')"
elif command -v shasum >/dev/null 2>&1; then
    actual_checksum="$(shasum -a 256 "$archive_path" | awk '{ print $1 }')"
else
    fail 'sha256sum ou shasum est requis pour verifier l archive.'
fi

expected_checksum="$(printf '%s' "$expected_checksum" | tr '[:upper:]' '[:lower:]')"
actual_checksum="$(printf '%s' "$actual_checksum" | tr '[:upper:]' '[:lower:]')"
[ "$expected_checksum" = "$actual_checksum" ] || fail 'La somme SHA-256 ne correspond pas.'

extracted_directory="${temporary_directory}/extracted"
mkdir -p "$extracted_directory"
tar -xzf "$archive_path" -C "$extracted_directory"
binary_path="${extracted_directory}/bin/vault"
[ -f "$binary_path" ] || fail 'Le binaire vault est absent de l archive.'

mkdir -p "$install_directory"
install -m 0755 "$binary_path" "${install_directory}/vault"

if [ "${VAULT_NO_PATH:-0}" != '1' ] && [ "$install_directory" = "${home_directory}/.local/bin" ]; then
    case "$(basename "${SHELL:-sh}")" in
        zsh) profile="${VAULT_PROFILE:-${home_directory}/.zshrc}" ;;
        bash) profile="${VAULT_PROFILE:-${home_directory}/.bashrc}" ;;
        *) profile="${VAULT_PROFILE:-${home_directory}/.profile}" ;;
    esac
    marker='# VaultCLI user binary path'
    if touch "$profile" 2>/dev/null; then
        if ! grep -Fq "$marker" "$profile" 2>/dev/null; then
            printf '\n%s\nexport PATH="$HOME/.local/bin:$PATH"\n' "$marker" >> "$profile"
        fi
        printf 'PATH configure dans %s. Ouvrez un nouveau terminal ou executez : . %s\n' \
            "$profile" "$profile"
    else
        printf 'Ajoutez manuellement ~/.local/bin au PATH de votre shell.\n'
    fi
fi

printf 'vault installe dans %s/vault\n' "$install_directory"
