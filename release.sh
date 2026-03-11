#!/bin/bash
set -e

RED='\e[0;31m'
GREEN='\e[0;32m'
YELLOW='\e[1;33m'
BLUE='\e[0;34m'
NC='\e[0m'

AUR_DIR="aur"
MAIN_BRANCH="master"
ANNOTATION=""

print_header() {
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}  MX Tools Arch Release Script${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo
}

print_step() {
    echo -e "${GREEN}> $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}WARNING: $1${NC}"
}

print_error() {
    echo -e "${RED}ERROR: $1${NC}"
}

print_success() {
    echo -e "${GREEN}OK: $1${NC}"
}

aur_has_separate_git_repo() {
    local aur_toplevel

    if ! aur_toplevel=$(git rev-parse --show-toplevel 2>/dev/null); then
        return 1
    fi

    [ "$aur_toplevel" = "$PWD" ]
}

validate_version() {
    local version=$1
    local clean_version=${version#v}

    if ! [[ $clean_version =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?[a-z]*$ ]]; then
        print_error "Invalid version format: $version"
        echo "Expected formats: 1.0.0, v1.0.0, YY.MM (like 26.01), or YY.MMsuffix (like 26.01arch)"
        exit 1
    fi

    echo "$version"
}

check_tag_exists() {
    local version=$1

    if git tag -l | grep -q "^${version}$"; then
        print_error "Tag '$version' already exists locally"
        return 1
    fi

    if git ls-remote --tags origin 2>/dev/null | grep -q "refs/tags/${version}$"; then
        print_error "Tag '$version' already exists on remote"
        return 1
    fi

    return 0
}

get_latest_tag() {
    local latest_tag
    latest_tag=$(git tag -l | grep -E '^v?[0-9]+\.[0-9]+(\.[0-9]+)?[a-z]*$' | sort -V | tail -n1)
    if [ -z "$latest_tag" ]; then
        echo "0.0.0"
    else
        echo "${latest_tag#v}"
    fi
}

compare_versions() {
    local new_version=$1
    local old_version=$2

    new_version=${new_version#v}
    old_version=${old_version#v}

    if [ "$new_version" = "$(echo -e "$new_version\n$old_version" | sort -V | tail -n1)" ] && [ "$new_version" != "$old_version" ]; then
        return 0
    fi

    return 1
}

prompt_annotation() {
    local version=$1
    local annotation=""
    local tmpfile

    echo
    print_step "Enter release annotation/notes"

    tmpfile=$(mktemp) || {
        print_error "Failed to create temp file"
        exit 1
    }

    cat > "$tmpfile" <<EOF
## Release $version

- Feature 1
- Bug fix 2
- Other changes
EOF

    if [ -n "${EDITOR:-}" ]; then
        "${EDITOR}" "$tmpfile"
    else
        echo "No EDITOR set. Enter a single-line annotation and press Enter:"
        echo "(Multi-line notes require setting EDITOR.)"
        read -r annotation
        if [ -n "$annotation" ]; then
            printf "%s\n" "$annotation" > "$tmpfile"
        fi
    fi

    annotation=$(sed '/^[[:space:]]*$/d' "$tmpfile")
    rm -f "$tmpfile"

    if [ -z "$annotation" ]; then
        print_error "Annotation cannot be empty"
        exit 1
    fi

    ANNOTATION="$annotation"
}

create_tag() {
    local version=$1
    local annotation=$2

    print_step "Creating annotated tag '$version'..."
    git tag -a "$version" -m "$annotation"
    print_success "Tag '$version' created successfully"
    echo
    git show "$version" --stat
}

update_aur_package() {
    local version=$1
    local annotation=$2
    local tarball_url="https://github.com/MX-Linux/mx-tools/archive/refs/tags/${version}.tar.gz"
    local checksum=""
    local retries=5

    print_step "Updating AUR package..."

    if [ ! -d "$AUR_DIR" ]; then
        print_error "AUR directory '$AUR_DIR' not found"
        exit 1
    fi

    cd "$AUR_DIR"

    if [ ! -f PKGBUILD ]; then
        print_error "PKGBUILD not found in $AUR_DIR"
        exit 1
    fi

    print_step "Updating PKGBUILD pkgver to $version..."
    sed -i "s/^pkgver=.*/pkgver=${version}/" PKGBUILD
    sed -i "s|^source=.*|source=(\"${tarball_url}\")|" PKGBUILD

    print_step "Downloading release tarball and calculating checksum..."
    for i in $(seq 1 $retries); do
        print_step "Attempting to download tarball (attempt $i/$retries)..."
        if curl -L --fail --silent --show-error "$tarball_url" -o "/tmp/${version}.tar.gz" 2>/dev/null; then
            checksum=$(sha256sum "/tmp/${version}.tar.gz" | cut -d' ' -f1)
            if [ -n "$checksum" ]; then
                print_success "Checksum calculated: ${checksum:0:16}..."
                break
            fi
        fi

        if [ $i -lt $retries ]; then
            print_warning "Failed to download tarball, waiting 5 seconds before retry..."
            sleep 5
        fi
    done

    if [ -z "$checksum" ]; then
        print_error "Failed to download tarball after $retries attempts"
        print_warning "Leaving sha256sums as SKIP; update it manually before publishing to AUR."
        checksum="SKIP"
    fi

    sed -i "s/^sha256sums=.*/sha256sums=('${checksum}')/" PKGBUILD

    print_step "Regenerating .SRCINFO..."
    makepkg --printsrcinfo > .SRCINFO

    print_step "Checking for AUR package changes..."
    if git diff --quiet -- PKGBUILD .SRCINFO; then
        print_warning "No changes in AUR package - skipping commit"
    else
        if aur_has_separate_git_repo; then
            print_step "Committing AUR package changes..."
            git add PKGBUILD .SRCINFO
            git commit -m "$annotation"
            print_success "AUR package updated and committed"
            echo
            git show --stat HEAD
        else
            print_warning "aur/ is not a separate git checkout; updated PKGBUILD and .SRCINFO only"
        fi
    fi

    rm -f "/tmp/${version}.tar.gz"
    cd ..
}

show_push_instructions() {
    echo
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}  MANUAL PUSH REQUIRED${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo
    if [ -d "${AUR_DIR}/.git" ] || [ -f "${AUR_DIR}/.git" ]; then
        print_warning "Please run this command manually from the AUR checkout:"
        echo
        echo -e "${YELLOW}cd aur && git push${NC}"
        return
    fi

    print_warning "aur/ is packaged in this repo, not a separate AUR checkout."
    echo
    print_step "Copy or sync these files into your AUR repo before publishing:"
    echo -e "${YELLOW}aur/PKGBUILD${NC}"
    echo -e "${YELLOW}aur/.SRCINFO${NC}"
}

main() {
    local version=""
    local mode="release"

    if [ $# -eq 0 ]; then
        print_error "Usage: $0 <version> [--update|--force]"
        echo "Example: $0 26.01"
        echo "         $0 26.01 --update"
        exit 1
    fi

    version=$1
    if [ $# -gt 1 ]; then
        case "$2" in
            --update|--force)
                mode="update"
                ;;
            *)
                print_error "Unknown option: $2"
                exit 1
                ;;
        esac
    fi

    print_header

    print_step "Validating version format..."
    version=$(validate_version "$version")
    print_success "Version format valid: $version"

    if [ "$mode" = "update" ]; then
        print_step "Update-only mode enabled: skipping tag checks and creation"
        update_aur_package "$version" "Update AUR package to $version"
        show_push_instructions
        print_success "AUR update complete (no tag created)"
        return 0
    fi

    if ! git rev-parse --git-dir > /dev/null 2>&1; then
        print_error "Not in a git repository"
        exit 1
    fi

    local current_branch
    current_branch=$(git branch --show-current)
    if [ "$current_branch" != "$MAIN_BRANCH" ]; then
        print_warning "Not on $MAIN_BRANCH branch (currently on: $current_branch)"
        echo "Continue anyway? (y/N)"
        read -r response
        if [[ ! "$response" =~ ^[Yy]$ ]]; then
            print_step "Aborted by user"
            exit 0
        fi
    fi

    print_step "Checking if tag '$version' already exists..."
    if ! check_tag_exists "$version"; then
        exit 1
    fi
    print_success "Tag '$version' is available"

    print_step "Checking version progression..."
    local latest_tag
    latest_tag=$(get_latest_tag)
    if ! compare_versions "${version#v}" "$latest_tag"; then
        print_error "Version ${version#v} is not higher than latest tag $latest_tag"
        exit 1
    fi
    print_success "Version $version > $latest_tag"

    prompt_annotation "$version"
    local annotation="$ANNOTATION"

    echo
    print_warning "About to create tag '$version' with annotation:"
    echo "$annotation"
    echo
    echo "Continue? (y/N)"
    read -r response
    if [[ ! "$response" =~ ^[Yy]$ ]]; then
        print_step "Aborted by user"
        exit 0
    fi

    create_tag "$version" "$annotation"

    print_step "Pushing tag to GitHub..."
    git push origin "$version"
    print_success "Tag pushed to GitHub"

    update_aur_package "$version" "$annotation"
    show_push_instructions

    echo
    print_success "Release preparation complete!"
}

main "$@"
