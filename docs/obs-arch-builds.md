# Building MX packages for Arch on OBS

How `home:mx-packaging` produces Arch Linux packages from the same git repo and the
same OBS package that already produces the Debian ones, and publishes them as a
signed pacman repository.

Written while setting this up for `mx-tools`; the steps generalise to any MX app
that already has an OBS package.

## Why Arch packages come from here at all

MX-specific packages that are not in AUR (`desktop-defaults-mx-common` and
friends) can never be updated on an installed system, because there is no
repository to update them from. Anything shipped only on the MX-Arch ISO is
frozen at ISO state. A repository fixes that class of problem; OBS builds it.

## The four pieces

| Piece | Where | Scope |
| --- | --- | --- |
| `arch/PKGBUILD` | the app's git repo | per app |
| `extract` param | the OBS package's `_service` | per app |
| `Arch` repository | project meta | once per project |
| `<enable repository="Arch"/>` | package meta | per app |

### 1. `arch/PKGBUILD`

Not the same file as `aur/PKGBUILD`. **OBS build VMs have no network**, so a
PKGBUILD that fetches a tarball in `source=` cannot work. Instead it consumes the
Debian native tarball that release builds already commit to `debs/`, which the
source service extracts into the build alongside the `.dsc`:

```bash
pkgver=26.09.7                                  # must track debian/changelog
source=("${pkgname}_${pkgver}.tar.xz")          # the file extracted from debs/
sha256sums=('SKIP')

# dpkg-source packed the tarball from a directory called "src", so that - not
# ${pkgname}-${pkgver} - is what it unpacks to.
_srcdir="src"

build()   { cd "${srcdir}/${_srcdir}"; ... }
package() { cd "${srcdir}/${_srcdir}"; ... }
```

Two things to keep in mind:

- **`pkgver` names the tarball**, so it has to track `debian/changelog`.
  `release.sh` already seds `^pkgver=` in `aur/PKGBUILD`; it should do the same
  here.
- **The tarball must contain everything `package()` installs.** `debs/*.tar.xz`
  is only regenerated when you run `obs`, so adding a new file to the repo and
  installing it from the PKGBUILD fails until the next release build refreshes
  that tarball. This bites exactly once per new file and the error is obvious:
  `install: cannot stat '...': No such file or directory`.

### 2. `_service`

Add one `extract` to the existing service - no other change:

```xml
<services>
  <service name="obs_scm">
    <param name="url">https://github.com/MX-Linux/<app>.git</param>
    <param name="scm">git</param>
    <param name="revision">master</param>
    <param name="extract">debs/*.dsc</param>
    <param name="extract">debs/*.tar.xz</param>
    <param name="extract">arch/PKGBUILD</param>
  </service>
</services>
```

The package then holds both a `.dsc` and a `PKGBUILD`. That is fine and is the
point: OBS filters build recipes by the repository's package format first, so
Debian repositories use the `.dsc` and the Arch repository uses the `PKGBUILD`.
Confirmed in the build log, which reports `build _service:obs_scm:PKGBUILD` for
the Arch job while the Debian jobs are unaffected.

```bash
osc co home:mx-packaging <app>
$EDITOR home:mx-packaging/<app>/_service
cd home:mx-packaging/<app> && osc ci -m "Extract arch/PKGBUILD"
# confirm the service ran:
osc api "/source/home:mx-packaging/<app>?expand=1"   # look for _service:obs_scm:PKGBUILD
```

### 3. Project meta - once per project

`Arch:Extra` is a download-on-demand project that pulls binaries straight from an
Arch mirror, so it tracks the rolling distro with no bootstrap on our side. It
paths to `Arch:Core`. **x86_64 is the only architecture those projects define.**

Add the repository, and disable it project-wide so the other ~40 packages do not
all start building Arch:

```xml
  <build>
    ...existing flags...
    <disable repository="Arch"/>
  </build>
  <repository name="Arch">
    <path project="Arch:Extra" repository="standard"/>
    <arch>x86_64</arch>
  </repository>
```

```bash
osc meta prj home:mx-packaging > prj.xml
$EDITOR prj.xml
osc meta prj home:mx-packaging -F prj.xml
```

### 4. Package meta - per app

Opt the package in. Note most packages already have a `<build>` block with
`disable` flags in it; add to that block rather than creating a second one:

```xml
  <build>
    <disable repository="Debian_11"/>
    <disable arch="armv7l" repository="Debian_Testing"/>
    <enable repository="Arch"/>
  </build>
```

```bash
osc meta pkg home:mx-packaging <app> > pkg.xml
$EDITOR pkg.xml
osc meta pkg home:mx-packaging <app> -F pkg.xml
osc results home:mx-packaging <app>        # an "Arch x86_64" row should appear
```

## What gets published

`https://download.opensuse.org/repositories/home:/mx-packaging/Arch/x86_64/`

The database is named after the project and repository, colons becoming
underscores. Verified as `home_mx-packaging_arch_Arch.*` for the pilot project
`home:mx-packaging:arch`; for `home:mx-packaging` it follows as
`home_mx-packaging_Arch.*` - worth confirming on the first successful publish.

```
<app>-<version>-1-x86_64.pkg.tar.zst          + .sig
<app>-debug-<version>-1-x86_64.pkg.tar.zst    + .sig     (split automatically)
home_mx-packaging_Arch.db  / .db.tar.gz / .files / .files.tar.gz   (all signed)
home_mx-packaging_Arch.key
```

Every file is GPG-signed and the signing key is published alongside, so
`SigLevel = Required` is available rather than only `TrustAll`.

Consuming it directly:

```ini
[home_mx-packaging_Arch]
Server = https://download.opensuse.org/repositories/home:/mx-packaging/Arch/$arch
```

### Serving it from mxrepo.com

pacman requires the `[section]` name to match the database filename, so straight
off OBS users would type `[home_mx-packaging_Arch]`. When syncing into R2 for
`arch.mxrepo.com`, rename the four database artifacts and their `.sig` files to
`mxarch.db*` / `mxarch.files*`. Signatures stay valid: pacman verifies
`X.db.sig` against `X.db` by content, and the database does not embed its own
name.

Cache rules, same as the Debian repos: package files are immutable and can cache
indefinitely; the `.db` must be short-TTL or purged on publish, or clients see a
stale index and silently get no updates.

## Adding another app

1. Copy `arch/PKGBUILD` from `mx-tools`, adjust `pkgname`, `depends`,
   `makedepends`, `build()` and `package()`.
2. Check `pkgver` matches `debian/changelog`.
3. Commit and **push** - the service pulls `master`, so an unpushed PKGBUILD
   does not exist as far as OBS is concerned.
4. Add `<param name="extract">arch/PKGBUILD</param>` to the package's `_service`.
5. Add `<enable repository="Arch"/>` to the package meta.
6. If the PKGBUILD installs files added since the last release build, run `obs`
   and commit the refreshed `debs/` first, or the build fails in `package()`.

## Notes and gotchas

- **x86_64 only.** The Arch DoD projects define no other architecture.
- **A `-debug` package appears automatically.** Harmless; it is published
  alongside and users do not have to install it.
- **`BUILD_TESTING` defaults on**, so Arch builds compile the test binary and
  throw it away. Either pass `-DBUILD_TESTING=OFF` in `build()`, or add
  `check() { cd "${srcdir}/${_srcdir}/build"; ctest --output-on-failure; }` and
  get the suite run on every Arch build.
- **AUR vs the repo.** If a package is in both, the repo wins: pacman only knows
  repositories, and once a repo provides the name the package is no longer
  "foreign", so AUR helpers stop offering to update it. That makes the repo a
  ceiling - publish repo and AUR from the same tag, at the same version.
- **Qt rebuilds are rarely needed.** Qt 6 is binary compatible across the whole
  6.x series and keeps `.so.6`, and these binaries link nothing else that churns
  (`objdump -p <binary> | grep NEEDED` to check). A rebuild is needed for Qt 7,
  or if an app grows a direct dependency on a library whose soname moves.

## Status

- The pilot project `home:mx-packaging:arch` still exists and publishes its own
  copy of `mx-tools`. It was only ever a scratch space; delete it once the main
  project builds green: `osc rdelete -r -m "pilot done" home:mx-packaging:arch`.
- `home:mx-packaging/mx-tools` builds Arch but currently fails in `package()`,
  because `debs/mx-tools_26.09.7.tar.xz` on master predates `data/menu/`. The
  next `obs` run refreshes it. This is the "tarball must contain everything
  package() installs" gotcha above, and it resolves itself at the next release.

## History

- 2026-09-15: first set up, piloted with `mx-tools` in the throwaway project
  `home:mx-packaging:arch`, then moved into `home:mx-packaging` itself.
