#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

TAG=""
TITLE=""
NOTES=""
PRERELEASE=1
SKIP_BUILD=0
DRY_RUN=0

usage() {
    cat <<USAGE
Usage: ./scripts/release.sh --tag <version> [options]

Required:
  --tag <vX.Y.Z>      Release tag (e.g. v0.1.0-playtest)

Options:
  --title <text>      Release title (defaults to "Through the Eyes of Carnage <tag>")
  --notes <text>      Release notes (defaults to auto-generated git log summary)
  --stable            Publish as a full release instead of --prerelease
  --skip-build        Upload existing dist/ packages without rebuilding
  --dry-run           Build, package, and verify checksums without pushing to GitHub
  -h, --help          Show this help message

Examples:
  ./scripts/release.sh --tag v0.1.0-playtest
  ./scripts/release.sh --tag v0.1.1-playtest --notes "Fixed floor z-fighting, added Save/Load + HUD upgrades"
  ./scripts/release.sh --tag v0.2.0 --stable
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --tag)        TAG="$2"; shift 2 ;;
        --title)      TITLE="$2"; shift 2 ;;
        --notes)      NOTES="$2"; shift 2 ;;
        --stable)     PRERELEASE=0; shift ;;
        --skip-build) SKIP_BUILD=1; shift ;;
        --dry-run)    DRY_RUN=1; shift ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "Unknown option: $1"; usage; exit 1 ;;
    esac
done

if [[ -z "$TAG" ]]; then
    echo "[ERR] --tag is required (e.g. ./scripts/release.sh --tag v0.1.0-playtest)"
    exit 1
fi

if [[ "$DRY_RUN" -eq 0 ]] && ! command -v gh >/dev/null 2>&1; then
    echo "[ERR] GitHub CLI (gh) not found. Install it on Fedora with:"
    echo "      sudo dnf install -y gh && gh auth login"
    exit 1
fi

[[ -z "$TITLE" ]] && TITLE="Through the Eyes of Carnage $TAG"

if [[ -z "$NOTES" ]]; then
    LAST_TAG="$(git describe --tags --abbrev=0 2>/dev/null || true)"
    if [[ -n "$LAST_TAG" ]]; then
        COMMITS="$(git log "${LAST_TAG}..HEAD" --pretty=format:"- %s (%h)" || true)"
    else
        COMMITS="$(git log -n 10 --pretty=format:"- %s (%h)" || true)"
    fi
    NOTES="Playtest build for **Through the Eyes of Carnage** (\`$TAG\`).

### Changes
${COMMITS:-"- Initial vertical slice build"}

### Setup
- **Windows:** Extract \`Carnage-Win64-Release.zip\` completely before running \`Carnage.exe\`.
- **Linux:** Extract \`Carnage-Linux-Release.tar.gz\` and run \`./Carnage\` from inside the extracted folder."
fi

if [[ "$SKIP_BUILD" -eq 0 ]]; then
    echo "[1/4] Running Linux unit tests..."
    ./scripts/build.sh --linux --test

    echo "[2/4] Building & packaging Linux + Windows Release archives..."
    ./scripts/build.sh --all --package
else
    echo "[1/4] Skipping build (--skip-build set)..."
fi

LINUX_PKG="dist/Carnage-Linux-Release.tar.gz"
WIN_PKG="dist/Carnage-Win64-Release.zip"
SUM_FILE="dist/SHA256SUMS.txt"

for pkg in "$LINUX_PKG" "$WIN_PKG"; do
    if [[ ! -f "$pkg" ]]; then
        echo "[ERR] Expected package missing: $pkg"
        exit 1
    fi
done

echo "[3/4] Generating SHA256 checksums..."
(cd dist && sha256sum "Carnage-Linux-Release.tar.gz" "Carnage-Win64-Release.zip" > SHA256SUMS.txt)
cat "$SUM_FILE"

if [[ "$DRY_RUN" -eq 1 ]]; then
    echo "[DRY-RUN] Packages ready in dist/ for tag $TAG. Skipping GitHub upload."
    exit 0
fi

echo "[4/4] Publishing release $TAG to GitHub..."
GH_FLAGS=(release create "$TAG" "$WIN_PKG" "$LINUX_PKG" "$SUM_FILE" --title "$TITLE" --notes "$NOTES")
[[ "$PRERELEASE" -eq 1 ]] && GH_FLAGS+=(--prerelease)

if gh release view "$TAG" >/dev/null 2>&1; then
    echo "[INF] Release $TAG already exists — uploading updated assets (--clobber)..."
    gh release upload "$TAG" "$WIN_PKG" "$LINUX_PKG" "$SUM_FILE" --clobber
else
    gh "${GH_FLAGS[@]}"
fi

echo "[DONE] Release $TAG published:"
gh release view "$TAG" --json url -q .url
