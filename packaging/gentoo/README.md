# Gentoo ebuild

`media-video/xrdome/xrdome-9999.ebuild` is a live ebuild: it clones `main` from GitHub
and builds it with meson. There are no release tags, so there is no versioned ebuild.
It has no `KEYWORDS`, so a live package has to be accepted by hand (below).

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

To run the tests during the build (the two smoke tests skip without `xvfb-run`), enable the
`test` flag for this package and `FEATURES=test`:

```
echo 'media-video/xrdome test' >> /etc/portage/package.use/xrdome
FEATURES=test emerge --ask media-video/xrdome
```

`media-video/mpv` needs the `libmpv` USE flag, `media-libs/libsdl2` needs `X` and `opengl`.
The binary is installed as `/usr/bin/xrdome`; `emerge --deselect` and `emerge -c` remove it.
