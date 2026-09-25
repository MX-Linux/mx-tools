#!/bin/bash
set -e

RED='\e[0;31m'
GREEN='\e[0;32m'
YELLOW='\e[1;33m'
BLUE='\e[0;34m'
NC='\e[0m'

AUR_DIR="aur"
# The AUR repo's local branch and the remote/branch it is published to
# (the AUR only accepts pushes to master). Set AUR_PUSH=0 to commit the AUR
# update without pushing it.
AUR_REMOTE="${AUR_REMOTE:-aur}"
AUR_LOCAL_BRANCH="${AUR_LOCAL_BRANCH:-main}"
AUR_REMOTE_BRANCH="${AUR_REMOTE_BRANCH:-master}"
AUR_PUSH="${AUR_PUSH:-1}"
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

    if ! aur_toplevel=$(git -C "$AUR_DIR" rev-parse --show-toplevel 2>/dev/null); then
        return 1
    fi

    [ "$aur_toplevel" = "$(realpath "$AUR_DIR")" ]
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

tag_exists() {
    local version=$1
    if git tag -l | grep -q "^${version}$"; then return 0; fi
    if git ls-remote --tags origin 2>/dev/null | grep -q "refs/tags/${version}$"; then return 0; fi
    return 1
}

get_latest_tag() {
    {
        git tag -l
        git ls-remote --tags origin 2>/dev/null | awk '{sub("refs/tags/", "", $2); sub(/\^\{\}$/, "", $2); print $2}'
    } | sort -u | while read -r tag; do
        local clean_tag=${tag#v}
        if [[ $clean_tag =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?[a-zA-Z0-9]*$ ]]; then printf '%s
' "$tag"; fi
    done | sort -V | tail -n1
}

get_tag_annotation() {
    local version=$1
    git for-each-ref --format='%(contents)' "refs/tags/${version}" 2>/dev/null || true
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

    local pkgbuild="$AUR_DIR/PKGBUILD"
    local srcinfo="$AUR_DIR/.SRCINFO"

    if [ ! -f "$pkgbuild" ]; then
        print_error "PKGBUILD not found in $AUR_DIR"
        exit 1
    fi

    print_step "Updating PKGBUILD pkgver to $version..."
    sed -i "s/^pkgver=.*/pkgver=${version}/" "$pkgbuild"
    sed -i "s|^source=.*|source=(\"${tarball_url}\")|" "$pkgbuild"

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

        if [ "$i" -lt "$retries" ]; then
            print_warning "Failed to download tarball, waiting 5 seconds before retry..."
            sleep 5
        fi
    done

    if [ -z "$checksum" ]; then
        print_error "Failed to download tarball after $retries attempts"
        print_warning "Nothing was committed; $AUR_DIR/PKGBUILD has uncommitted edits. Re-run once the tag tarball is reachable."
        exit 1
    fi

    sed -i "s/^sha256sums=.*/sha256sums=('${checksum}')/" "$pkgbuild"

    print_step "Regenerating .SRCINFO..."
    (cd "$AUR_DIR" && makepkg --printsrcinfo) > "$srcinfo"
    # Sanity-check what is about to be committed (and pushed)
    if ! awk -v v="${version}" '$1 == "pkgver" && $3 == v { f = 1 } END { exit !f }' "$srcinfo"; then
        print_error ".SRCINFO does not report pkgver = ${version}"
        exit 1
    fi
    if ! awk -v c="$checksum" '$1 == "sha256sums" && $3 == c { f = 1 } END { exit !f }' "$srcinfo"; then
        print_error ".SRCINFO does not carry the downloaded tarball checksum"
        exit 1
    fi

    print_step "Checking for AUR package changes..."
    if git -C "$AUR_DIR" diff --quiet -- PKGBUILD .SRCINFO; then
        print_warning "No changes in AUR package - skipping commit"
    else
        if aur_has_separate_git_repo; then
            print_step "Committing AUR package changes..."
            git -C "$AUR_DIR" add PKGBUILD .SRCINFO
            git -C "$AUR_DIR" commit -m "$annotation"
            print_success "AUR package updated and committed"
            echo
            git -C "$AUR_DIR" show --stat HEAD
        else
            print_warning "aur/ is not a separate git checkout; updated PKGBUILD and .SRCINFO only"
        fi
    fi

    rm -f "/tmp/${version}.tar.gz"
}

# Push the AUR repo if it has commits the AUR doesn't
push_aur_package() {
    print_step "Pushing AUR package..."

    local aur_toplevel
    aur_toplevel=$(git -C "$AUR_DIR" rev-parse --show-toplevel 2>/dev/null) || aur_toplevel=""
    if [ "$aur_toplevel" != "$(realpath "$AUR_DIR")" ]; then
        print_error "$AUR_DIR is not a separate git checkout"
        return 1
    fi

    if ! git -C "$AUR_DIR" diff --quiet HEAD -- PKGBUILD .SRCINFO; then
        print_error "$AUR_DIR has uncommitted PKGBUILD/.SRCINFO changes; commit them first"
        return 1
    fi

    local current_branch
    current_branch=$(git -C "$AUR_DIR" branch --show-current)
    if [ "$current_branch" != "$AUR_LOCAL_BRANCH" ]; then
        print_error "$AUR_DIR is on branch '$current_branch', expected '$AUR_LOCAL_BRANCH'"
        return 1
    fi

    if ! git -C "$AUR_DIR" fetch --quiet "$AUR_REMOTE" "$AUR_REMOTE_BRANCH"; then
        print_error "Could not fetch $AUR_REMOTE/$AUR_REMOTE_BRANCH"
        return 1
    fi
    local ahead
    ahead=$(git -C "$AUR_DIR" rev-list --count "$AUR_REMOTE/$AUR_REMOTE_BRANCH..$AUR_LOCAL_BRANCH")
    if [ "$ahead" -eq 0 ]; then
        print_success "AUR already up to date; nothing to push"
        return 0
    fi

    if ! git -C "$AUR_DIR" push "$AUR_REMOTE" "$AUR_LOCAL_BRANCH:$AUR_REMOTE_BRANCH"; then
        print_error "Pushing to $AUR_REMOTE failed"
        return 1
    fi
    print_success "Pushed $ahead commit(s) to $AUR_REMOTE/$AUR_REMOTE_BRANCH"
}

# Push to the AUR, or print the manual steps when pushing is disabled or fails
publish_aur_package() {
    if [ "$AUR_PUSH" = 1 ] && push_aur_package; then
        return 0
    fi
    show_push_instructions "$@"
    # A failed push is an error; a disabled one is not
    [ "$AUR_PUSH" != 1 ]
}

show_push_instructions() {
    local tag_status=${1:-created}
    echo
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}  MANUAL PUSH REQUIRED${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo
    if [ -d "${AUR_DIR}/.git" ] || [ -f "${AUR_DIR}/.git" ]; then
        print_warning "Please run this command manually from the AUR checkout:"
        echo
        echo -e "${YELLOW}git -C $AUR_DIR push $AUR_REMOTE $AUR_LOCAL_BRANCH:$AUR_REMOTE_BRANCH${NC}"
        return
    fi

    if [ "$tag_status" = "existing" ]; then
        print_step "The existing tag was reused; no tag was created."
    else
        print_step "The tag has been created and pushed automatically."
    fi

    print_warning "aur/ is packaged in this repo, not a separate AUR checkout."
    echo
    print_step "Copy or sync these files into your AUR repo before publishing:"
    echo -e "${YELLOW}aur/PKGBUILD${NC}"
    echo -e "${YELLOW}aur/.SRCINFO${NC}"
}

main() {
    local version=""
    local tag_status="created"

    while [ $# -gt 0 ]; do
        case "$1" in
            --update|--force)
                print_warning "$1 is no longer needed; existing tags are handled automatically" ;;
            --*)
                print_error "Unknown option: $1"; exit 1 ;;
            *)
                if [ -n "$version" ]; then print_error "Only one version may be specified"; exit 1; fi
                version=$1 ;;
        esac
        shift
    done

    print_header
    if ! git rev-parse --git-dir > /dev/null 2>&1; then print_error "Not in a git repository"; exit 1; fi
    if [ -z "$version" ]; then
        version=$(get_latest_tag)
        if [ -z "$version" ]; then print_error "No version tag found; specify a version explicitly"; exit 1; fi
        print_step "No version specified; using latest tag '$version'"
    fi
    print_step "Validating version format..."
    version=$(validate_version "$version")
    print_success "Version format valid: $version"

    local current_branch
    current_branch=$(git branch --show-current)
    if [ "$current_branch" != "$MAIN_BRANCH" ]; then
        print_warning "Not on $MAIN_BRANCH branch (currently on: $current_branch)"
        echo "Continue anyway? (y/N)"; read -r response
        if [[ ! "$response" =~ ^[Yy]$ ]]; then print_step "Aborted by user"; exit 0; fi
    fi

    print_step "Checking if tag '$version' already exists..."
    if tag_exists "$version"; then
        print_warning "Tag '$version' already exists; no new tag will be created"
        tag_status="existing"
        ANNOTATION=$(get_tag_annotation "$version")
        if [ -z "$ANNOTATION" ]; then ANNOTATION="Update AUR package to $version"; fi
    else
        print_success "Tag '$version' is available"
        print_step "Checking version progression..."
        local latest_tag
        latest_tag=$(get_latest_tag)
        if [ -n "$latest_tag" ] && ! compare_versions "${version#v}" "$latest_tag"; then
            print_error "Version ${version#v} is not higher than latest tag ${latest_tag#v}"; exit 1
        fi
        if [ -n "$latest_tag" ]; then print_success "Version $version > ${latest_tag#v}"; fi
        prompt_annotation "$version"
    fi
    local annotation="$ANNOTATION"

    if [ "$tag_status" = "created" ]; then
        echo; print_warning "About to create tag '$version' with annotation:"; echo "$annotation"; echo
        echo "Continue? (y/N)"; read -r response
        if [[ ! "$response" =~ ^[Yy]$ ]]; then print_step "Aborted by user"; exit 0; fi
        create_tag "$version" "$annotation"
        print_step "Pushing tag to GitHub..."; git push origin "$version"; print_success "Tag pushed to GitHub"
    fi
    update_aur_package "$version" "$annotation"
    publish_aur_package "$tag_status"
    echo; print_success "Release preparation complete!"
}

main "$@"
