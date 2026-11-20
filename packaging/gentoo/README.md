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
No checksum is stored in this repository: the `DIST` line of the `Manifest` is generated from the
real tarball, after the release has been published. In the overlay copy of the directory:

```
cd /var/db/repos/local/media-video/xrdome
ebuild xrdome-0.1.0.ebuild manifest      # downloads the tarball, writes Manifest
echo 'media-video/xrdome ~amd64' >> /etc/portage/package.accept_keywords/xrdome   # instead of **
emerge --ask =media-video/xrdome-0.1.0
```

The CI `dist` job writes `xrdome-0.1.0.tar.xz.sha256sum` next to the tarball; compare it with the
`SHA256` the Manifest line holds (`ebuild ... manifest` prints the BLAKE2B and SHA512 digests
portage uses, so run `sha256sum` on the downloaded file as well). The ebuild has not been built
with `emerge`: that needs the published tarball.
