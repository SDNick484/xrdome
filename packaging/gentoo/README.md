# Gentoo ebuilds

Two ebuilds for `media-video/xrdome`:

- `media-video/xrdome/xrdome-9999.ebuild` is a live ebuild: it clones `main` from GitHub and builds
  it with meson. It has no `KEYWORDS`, so a live package has to be accepted by hand (below).
- `media-video/xrdome/xrdome-0.1.0.ebuild` builds the release tarball, `xrdome-0.1.0.tar.xz`, from
  the GitHub release `v0.1.0` (made by the `dist` job in CI), with `KEYWORDS="~amd64"`. It needs a
  `Manifest` before portage will fetch it, and the tarball has to exist first (below).

## Add it to a local overlay

As root, once (skip the `eselect repository` line if you already have a local overlay; use its path
instead of `/var/db/repos/local`):

```
eselect repository create local
mkdir -p /var/db/repos/local/media-video/xrdome
cp media-video/xrdome/* /var/db/repos/local/media-video/xrdome/
echo 'media-video/xrdome **' >> /etc/portage/package.accept_keywords/xrdome
emerge --ask media-video/xrdome
```

Run the `cp` from this directory (`packaging/gentoo/`). The overlay copy does not follow
changes here, so copy again when the ebuild changes. `emerge --ask @live-rebuild` (or
`emerge -a media-video/xrdome` again) rebuilds the live package from the newest `main`.

To run the tests during the build (the three smoke tests skip without `xvfb-run`), enable the
`test` flag for this package and `FEATURES=test`:

```
echo 'media-video/xrdome test' >> /etc/portage/package.use/xrdome
FEATURES=test emerge --ask media-video/xrdome
```

`media-video/mpv` needs the `libmpv` USE flag, `media-libs/libsdl2` needs `X` and `opengl`.
The binary is installed as `/usr/bin/xrdome`; `emerge --deselect` and `emerge -c` remove it.

## The versioned ebuild and its Manifest

`xrdome-0.1.0.ebuild` fetches `https://github.com/SDNick484/xrdome/releases/download/v0.1.0/xrdome-0.1.0.tar.xz`.
`media-video/xrdome/Manifest` holds its `DIST` line (size, BLAKE2B and SHA512), computed from the
published tarball, whose SHA256 is `b895efed...d658e` (the release's `.sha256sum` file and GitHub's
asset digest agree). The `cp` above copies the `Manifest` with the ebuilds. To check it or to
regenerate it after a new release, in the overlay copy of the directory:

```
cd /var/db/repos/local/media-video/xrdome
ebuild xrdome-0.1.0.ebuild manifest      # downloads the tarball, rewrites Manifest
echo 'media-video/xrdome ~amd64' >> /etc/portage/package.accept_keywords/xrdome   # instead of **
emerge --ask =media-video/xrdome-0.1.0
```

The `Manifest` line was written by a script from the downloaded file, not by `ebuild ... manifest`,
and the ebuild has not been built with `emerge`: if `ebuild manifest` prints a different line,
trust it and fix this one.
