# Releases

A release is a source tarball, `xrdome-X.Y.Z.tar.xz`, made with `meson dist` by the `dist` job in
CI, with its `.sha256sum`. It holds the sources, tests, docs and `xrdome-tools`; it ships no mpv
or FFmpeg. Build it as in [Build and run](setup.md#build-and-run). Release 0.1.0 is Linux only; what is in
it, what is experimental (macOS, Windows, depth for films, the VITURE pose helper) and the known
limits are in [CHANGELOG.md](../CHANGELOG.md). The macOS and Windows zips CI makes are test builds,
not releases.

On a pushed `v*` tag CI builds the tarball again and attaches it to a **draft** GitHub release,
with the matching CHANGELOG section as its notes; the maintainer publishes it by hand.

## Release checklist (maintainer)

1. Bump `version` in `meson.build`, `pkgver` in `packaging/arch/PKGBUILD` (with `pkgrel=1`), and add
   an `X.Y.Z-1` entry to `packaging/debian/changelog`: CI checks all three against `meson.build`.
2. In `CHANGELOG.md`, move `[Unreleased]` into the new version's section and give it the date.
3. Merge to `main` through a pull request with CI green, including `dist`.
4. Tag and push: `git tag -a vX.Y.Z -m "xrdome vX.Y.Z"` (or `-s` with a signing key), then
   `git push origin vX.Y.Z`.
5. Wait for CI to finish the draft release, check its notes and the two attached files, publish it.
6. Generate the Gentoo `Manifest` (`ebuild xrdome-X.Y.Z.ebuild manifest`), put the tarball's
   sha256 in `packaging/arch/PKGBUILD` and regenerate `packaging/arch/.SRCINFO` in a follow-up pull
   request.
7. Push `PKGBUILD` and `.SRCINFO` to the AUR ([packaging/arch/README.md](../packaging/arch/README.md))
   and upload the source package to the PPA ([packaging/debian/README.md](../packaging/debian/README.md)).
