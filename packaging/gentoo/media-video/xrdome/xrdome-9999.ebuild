# Copyright 2026 Nick Andrade
# Distributed under the terms of the GNU General Public License v2

EAPI=8

inherit git-r3 meson

DESCRIPTION="VR180 and flat video player for XR glasses with head tracking"
HOMEPAGE="https://github.com/SDNick484/xrdome"
EGIT_REPO_URI="https://github.com/SDNick484/xrdome.git"

LICENSE="GPL-2+"
SLOT="0"
# Live ebuild: no KEYWORDS. Accept it with ~amd64 or ** in package.accept_keywords.
IUSE="test"
RESTRICT="!test? ( test )"

# ffprobe (media-video/ffmpeg) is run at run time to detect the format.
# xrdome links libmpv, which is why it is GPL: mpv's own GPL build is GPL-2+.
RDEPEND="
	media-libs/libepoxy
	media-libs/libglvnd
	media-libs/libsdl2[X,opengl]
	media-video/ffmpeg
	media-video/mpv[libmpv]
"
# meson.build asks for cmocka unconditionally (dependency('cmocka')), even
# when the tests are not run, so it is a build dependency without the flag.
DEPEND="
	${RDEPEND}
	dev-util/cmocka
"
BDEPEND="
	dev-lang/python
	virtual/pkgconfig
"

src_test() {
	# The two smoke tests need xvfb-run and skip (77) without it; the unit
	# tests run either way.
	meson_src_test
}
