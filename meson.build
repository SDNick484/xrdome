project('xrdome', 'c',
  version : '0.1.0',
  license : 'GPL-2.0-or-later AND MIT',
  default_options : ['c_std=c11', 'warning_level=3'])

cc = meson.get_compiler('c')
is_macos = host_machine.system() == 'darwin'
# macOS: _POSIX_C_SOURCE (set by several sources) hides BSD extensions such as
# O_NOFOLLOW; _DARWIN_C_SOURCE brings them back alongside it.
if is_macos
  add_project_arguments('-D_DARWIN_C_SOURCE', language : 'c')
endif
m_dep = cc.find_library('m', required : true)
cmocka_dep = dependency('cmocka')
threads_dep = dependency('threads')
epoxy_dep = dependency('epoxy')
# EGL: Linux only (the render test's surfaceless context); macOS uses CGL through SDL.
egl_dep = dependency('egl', required : not is_macos)
sdl2_dep = dependency('sdl2')
mpv_dep = dependency('mpv')
py = find_program('python3')

shaders_gen = custom_target('shaders_gen',
  input : ['src/render/shaders/vr180.vert', 'src/render/shaders/vr180.frag',
           'src/render/shaders/sharpen.frag'],
  output : 'shaders_gen.h',
  command : [py, files('tools/embed_shaders.py'), '@OUTPUT@',
             'VR180_VERT=@INPUT0@', 'VR180_FRAG=@INPUT1@', 'SHARPEN_FRAG=@INPUT2@'])

# Core sources: append one line per task.
core_sources = files(
  'src/math/quat.c',
  'src/render/projection.c',
  'src/values.c',
  'src/config.c',
  'src/util/timing.c',
  'src/pose/opentrack_packet.c',
  'src/pose/pose.c',
  'src/pose/pose_null.c',
  'src/pose/pose_udp_opentrack.c',
  'src/track/tracker.c',
  'src/render/renderer_gl.c',
  'src/display/output.c',
  'src/driver/xrdriver.c',
  'src/display/sbs.c',
  'src/video/player_mpv.c',
  'src/video/mpv_scale.c',
  'src/app/controls.c',
  'src/store/filestate.c',
  'src/detect/probe.c',
  'src/detect/detect.c',
  'src/app/session.c',
  'src/app/imagedir.c',
  'src/platform/platform.c',
)

# Test names: each entry NAME builds tests/test_NAME.c into test_NAME.
test_names = [
  'quat',
  'projection',
  'config',
  'opentrack',
  'pose_udp',
  'tracker',
  'render',
  'output',
  'player',
  'timing',
  'controls',
  'filestate',
  'values',
  'probe',
  'detect',
  'probe_run',
  'session',
  'xrdriver',
  'sbs',
  'mpv_scale',
  'platform',
  'imagedir',
]
# test_render draws through a surfaceless Mesa EGL context.
if not egl_dep.found()
  kept = []
  foreach n : test_names
    if n != 'render'
      kept += n
    endif
  endforeach
  test_names = kept
endif

inc = include_directories('src')

xrdcore = static_library('xrdcore', core_sources, shaders_gen,
  include_directories : inc,
  dependencies : [m_dep, threads_dep, epoxy_dep, sdl2_dep, mpv_dep])

# Paths the probe tests read fixtures and the fake ffprobe from.
fixture_args = [
  '-DXRD_FIXTURE_DIR="' + (meson.current_source_dir() / 'tests/fixtures/ffprobe') + '"',
  '-DXRD_TESTS_DIR="' + (meson.current_source_dir() / 'tests') + '"',
]

# cmocka 2.0's assert macros use the GNU ", ##__VA_ARGS__" extension, which
# clang reports under -Wpedantic in every test; gcc does not know the flag.
test_c_args = fixture_args + cc.get_supported_arguments('-Wno-gnu-zero-variadic-macro-arguments')

foreach t : test_names
  exe = executable('test_' + t, 'tests/test_' + t + '.c',
    include_directories : inc,
    c_args : test_c_args,
    link_with : xrdcore,
    dependencies : [cmocka_dep, m_dep, threads_dep, epoxy_dep, egl_dep, sdl2_dep, mpv_dep])
  test(t, exe, env : ['LIBGL_ALWAYS_SOFTWARE=1', 'EGL_PLATFORM=surfaceless',
                      'XRD_TEST_DATA=' + (meson.current_source_dir() / 'tests/data')])
endforeach

xrdome_exe = executable('xrdome', 'src/main.c',
  include_directories : inc,
  link_with : xrdcore,
  dependencies : [m_dep, threads_dep, epoxy_dep, sdl2_dep, mpv_dep],
  install : true)

# Desktop smoke test under Xvfb; skipped (77) without xvfb-run or ffmpeg.
test('smoke', find_program('tests/smoke_player.sh'),
  args : [xrdome_exe, files('tools/make_pattern.sh')],
  is_parallel : false,
  timeout : 300)

# Head tracking end to end under Xvfb, fed by tools/ot_sweep.py over UDP.
test('smoke_tracking', find_program('tests/smoke_tracking.sh'),
  args : [xrdome_exe, files('tools/make_pattern.sh'), files('tools/ot_sweep.py')],
  is_parallel : false,
  timeout : 300)

# The automatic SBS switch under Xvfb, against tests/fake_xr_driver.py.
test('smoke_sbs', find_program('tests/smoke_sbs.sh'),
  args : [xrdome_exe, files('tools/make_pattern.sh'), files('tests/fake_xr_driver.py')],
  is_parallel : false,
  timeout : 300)

# Photos under Xvfb: EXIF orientations, a directory with PageDown / PageUp.
test('smoke_photos', find_program('tests/smoke_photos.sh'),
  args : [xrdome_exe],
  is_parallel : false,
  timeout : 300)
