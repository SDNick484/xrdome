# Debian and Ubuntu

This directory is the `debian/` directory for the release tarball: it is kept here, beside the
other packaging, and copied in as `debian/` when a package is built. It builds `xrdome` only;
`xrdome-tools` and `xrdome-pose-viture` are not packaged.

Built and installed in containers on 2026-11-21 from the v0.1.0 tarball, on Ubuntu 24.04 and on
Debian 13: the unit tests passed during the build (the Xvfb smoke tests skip without `xvfb-run`,
which is left out of the build dependencies on purpose), and lintian reported no errors. CI's
`deb` job does the same on Ubuntu 24.04 for every change. Not run on glasses from the package.

## Build a .deb

On Debian or Ubuntu, from a directory holding the release tarball:

```sh
sudo apt-get install devscripts equivs
cp xrdome-0.1.0.tar.xz xrdome_0.1.0.orig.tar.xz
tar -xf xrdome_0.1.0.orig.tar.xz
cp -r /path/to/xrdome-checkout/packaging/debian xrdome-0.1.0/debian
cd xrdome-0.1.0
sudo mk-build-deps -i -r debian/control
dpkg-buildpackage -us -uc -b
sudo apt-get install ../xrdome_0.1.0-1_*.deb
```

The 0.1.0 tarball is older than this directory, so it comes from a checkout of the repository;
later tarballs hold it as `packaging/debian`. A `.deb` built this way installs only on the release it was built on (Ubuntu 24.04 and Debian 13
ship different library versions), which is why a PPA, which builds for each Ubuntu release, is the
way to hand them out.

## Publish on a Launchpad PPA (once)

1. Make a Launchpad account, upload an OpenPGP key to it, and create a PPA (for example
   `ppa:sdnick484/xrdome`). Launchpad may ask you to sign the Ubuntu Code of Conduct first.
2. Give the upload a version for one Ubuntu release, sign the source package and upload it:

   ```sh
   cd xrdome-0.1.0                       # unpacked, with debian/ copied in as above
   dch -v 0.1.0-1~noble1 -D noble "PPA build for Ubuntu 24.04."
   debuild -S -sa -k<KEYID>              # -sa: include the orig tarball (first upload)
   dput ppa:sdnick484/xrdome ../xrdome_0.1.0-1~noble1_source.changes
   ```

   Repeat for each Ubuntu release (`~plucky1` and so on), or use the PPA's "Copy packages" page to
   copy a build to another release. Launchpad builds, signs and serves it; users then run
   `sudo add-apt-repository ppa:sdnick484/xrdome && sudo apt install xrdome`.

Keep the `dch` change out of this directory: it is only for the upload.

## Each release

1. Add an entry at the top of `changelog` here for `X.Y.Z-1` (`dch -v X.Y.Z-1 -D unstable`), with
   the date. CI fails when its upstream version is not `meson.build`'s.
2. From the first release with meson's `tests` option, `libcmocka-dev` in `control` can become
   `libcmocka-dev <!nocheck>`, so a `nocheck` build needs no cmocka.

## Not done yet

- A manual page (lintian's `no-manual-page` warning).
- `debian/watch`, for uscan (lintian's `debian-watch-file-is-missing`, information only).
- Debian itself: a package in Debian needs an ITP bug and a sponsor; lintian's
  `initial-upload-closes-no-bugs` warning is about that.
