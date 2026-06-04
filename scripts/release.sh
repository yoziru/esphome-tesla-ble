#!/bin/sh

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <patch|minor|major|version>" >&2
    exit 1
fi

ARG="$1"

if ! command -v gh >/dev/null 2>&1; then
    echo "Error: gh CLI not found" >&2
    exit 1
fi

cd "$ROOT_DIR"

if [ -z "${GH_TOKEN:-${GITHUB_TOKEN:-}}" ]; then
    if ! gh auth status >/dev/null 2>&1; then
        echo "Error: gh is not authenticated" >&2
        exit 1
    fi
fi

current_branch=$(git branch --show-current)
if [ "$current_branch" != "main" ]; then
    echo "Error: run releases from main (current branch: $current_branch)" >&2
    exit 1
fi

if [ -n "$(git status --porcelain)" ]; then
    echo "Error: working tree must be clean before releasing" >&2
    exit 1
fi

git fetch origin --tags
git pull --ff-only origin main

current_version=$(sed -n 's/^    version: "\(.*\)"$/\1/p' packages/base.yml)
if [ -z "$current_version" ]; then
    echo "Error: could not read version from packages/base.yml" >&2
    exit 1
fi

case "$ARG" in
    patch|minor|major)
        IFS=. read -r major minor patch <<EOF
$current_version
EOF
        case "$ARG" in
            patch) patch=$((patch + 1)) ;;
            minor) minor=$((minor + 1)); patch=0 ;;
            major) major=$((major + 1)); minor=0; patch=0 ;;
        esac
        next_version="$major.$minor.$patch"
        ;;
    *)
        case "$ARG" in
            ''|*[!0-9.]*|*.*.*.*|*..*|.*|*.)
                echo "Error: argument must be patch, minor, major, or X.Y.Z" >&2
                exit 1
                ;;
        esac
        next_version="$ARG"
        ;;
esac

if [ "$next_version" = "$current_version" ]; then
    echo "Error: next version matches current version ($current_version)" >&2
    exit 1
fi

if git rev-parse -q --verify "refs/tags/v$next_version" >/dev/null 2>&1; then
    echo "Error: tag v$next_version already exists" >&2
    exit 1
fi

# Update version in packages/base.yml (portable: works on macOS and Linux)
sed "s/^    version: \"$current_version\"$/    version: \"$next_version\"/" packages/base.yml > packages/base.yml.tmp
mv packages/base.yml.tmp packages/base.yml

git add packages/base.yml
if git diff --cached --quiet; then
    echo "Error: no version changes to commit" >&2
    exit 1
fi

git commit -m "release: v$next_version"
git tag "v$next_version"
git push origin main
git push origin "v$next_version"

gh release create "v$next_version" --title "v$next_version" --generate-notes

echo "Released v$next_version"
