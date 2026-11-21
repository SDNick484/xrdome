# Arch Linux (AUR)

`PKGBUILD` builds the release tarball from the GitHub release; `.SRCINFO` is the metadata the AUR
reads, generated from it. Users install with an AUR helper (`yay -S xrdome`, `paru -S xrdome`) or
by hand (`git clone https://aur.archlinux.org/xrdome.git && cd xrdome && makepkg -si`).

## Publish it on the AUR (once)

1. Make an account on https://aur.archlinux.org and add an SSH public key under My Account.
2. Clone the empty package repository (the name `xrdome` was free on 2026-11-21) and add the two
   files:

   ```sh
   git clone ssh://aur@aur.archlinux.org/xrdome.git aur-xrdome
   cp packaging/arch/PKGBUILD packaging/arch/.SRCINFO aur-xrdome/
   cd aur-xrdome
   git add PKGBUILD .SRCINFO
   git commit -m "xrdome 0.1.0-1"
   git push origin master
   ```

The AUR accepts only the `master` branch, and only commits that hold both files.

## Each release

1. In `PKGBUILD`, set `pkgver`, put `pkgrel=1` and the new tarball's sha256 (from the release's
   `.sha256sum` file) in `sha256sums`.
2. Regenerate `.SRCINFO`: `makepkg --printsrcinfo > .SRCINFO` (on Arch, as a normal user). CI fails
   when it no longer matches the `PKGBUILD`.
3. Commit both here, then copy them to the AUR clone, commit and push as above.

A change to the packaging alone, with the same tarball, raises `pkgrel` instead.

From the first release with meson's `tests` option, `cmocka` can move from `makedepends` to
`checkdepends` only. The 0.1.0 tarball's `meson.build` requires cmocka even when the tests are not
run, and `arch-meson` turns `auto` features on, so the `build()` would have to pass
`-Dtests=disabled` when cmocka is missing.
