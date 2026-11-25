project('xrdome', 'c',
  version : '0.1.0',
  license : 'GPL-2.0-or-later AND MIT',
  meson_version : '>=1.1.0', # meson.options
  default_options : ['c_std=c11', 'warning_level=3'])

cc = meson.get_compiler('c')
is_macos = host_machine.system() == 'darwin'
# Windows 11, MSYS2 UCRT64 (MinGW-w64 gcc). Linux is the reference platform.
is_windows = host_machine.system() == 'windows'
# macOS: _POSIX_C_SOURCE (set by several sources) hides BSD extensions such as
# O_NOFOLLOW; _DARWIN_C_SOURCE brings them back alongside it.
if is_macos
  add_project_arguments('-D_DARWIN_C_SOURCE', language : 'c')
endif
add_project_arguments('-DXRD_VERSION="' + meson.project_version() + '"', language : 'c')
# Windows: the maths functions are in the C runtime, no libm.
m_dep = cc.find_library('m', required : not is_windows)
# cmocka only for the unit tests (-Dtests); without it they are left out.
cmocka_dep = dependency('cmocka', required : get_option('tests'))
threads_dep = dependency('threads')
epoxy_dep = dependency('epoxy')
# EGL: Linux only (the render test's surfaceless context); macOS uses CGL and
# Windows WGL through SDL.
if is_windows
  egl_dep = dependency('', required : false)
else
  egl_dep = dependency('egl', required : not is_macos)
endif
sdl2_dep = dependency('sdl2')
mpv_dep = dependency('mpv')
# shm_open: in libc on glibc 2.34+ and macOS, in librt before.
rt_dep = cc.find_library('rt', required : false)
# Windows: Winsock (the UDP pose), the known folders (SHGetKnownFolderPath in
# shell32, CoTaskMemFree in ole32).
win_deps = []
if is_windows
  foreach l : ['ws2_32', 'shell32', 'ole32']
    win_deps += cc.find_library(l)
  endforeach
endif
py = find_program('python3')

shaders_gen = custom_target('shaders_gen',
  input : ['src/render/shaders/vr180.vert', 'src/render/shaders/vr180.frag',
           'src/render/shaders/sharpen.frag'],
  output : 'shaders_gen.h',
  command : [py, files('tools/embed_shaders.py'), '@OUTPUT@',
             'VR180_VERT=@INPUT0@', 'VR180_FRAG=@INPUT1@', 'SHARPEN_FRAG=@INPUT2@'])

# The frame sink's downscale (src/video/framesink.c): its own header, so no
# translation unit carries shaders it does not use.
framesink_gen = custom_target('framesink_gen',
  input : ['src/render/shaders/vr180.vert', 'src/video/framesink.frag'],
  output : 'framesink_gen.h',
  command : [py, files('tools/embed_shaders.py'), '@OUTPUT@',
             'FRAMESINK_VERT=@INPUT0@', 'FRAMESINK_FRAG=@INPUT1@'])

# libxrdome-core: the sources every front end shares, listed in
# src/core_sources.txt (one path per line; blank and '#' lines dropped), which
# the Android CMake build reads too. Add a core file there, not here.
fs = import('fs')
core_list = []
foreach line : fs.read('src/core_sources.txt').split('\n')
  line = line.strip()
  if line != '' and not line.startswith('#')
    core_list += line
  endif
endforeach
core_sources = files(core_list)

# The Linux, macOS and Windows front end (SDL, mpv, the window, the probe):
# append one line per task.
front_sources = files(
  'src/config.c',
  'src/display/output.c',
  'src/driver/xrdriver.c',
  'src/display/sbs.c',
  'src/video/player_mpv.c',
  'src/video/mpv_scale.c',
  'src/app/controls_keys.c',
  'src/detect/probe.c',
  'src/detect/probe_win.c',
  'src/app/session.c',
  'src/app/imagedir.c',
  'src/platform/platform.c',
  'src/platform/exepath.c',
  'src/render/depthmap.c',
  'src/app/depthclient.c',
  'src/video/framesink.c',
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
  'exepath',
  'imagedir',
  'depthmap',
  'depthclient',
  'framesink',
  'framesink_gl',
  'devices',
]
# test_render and test_framesink_gl draw through a surfaceless Mesa EGL context.
if not egl_dep.found()
  kept = []
  foreach n : test_names
    if n != 'render' and n != 'framesink_gl'
      kept += n
    endif
  endforeach
  test_names = kept
endif
if not cmocka_dep.found()
  test_names = []
endif

inc = include_directories('src')

# The core: no SDL, mpv or EGL dependency (test_core_link proves it links
# without them; core_clean checks its includes).
xrdome_core = static_library('xrdome_core', core_sources, shaders_gen,
  include_directories : inc,
  dependencies : [m_dep, threads_dep, epoxy_dep, rt_dep, win_deps])
xrdome_core_dep = declare_dependency(link_with : xrdome_core,
  include_directories : inc,
  dependencies : [m_dep, threads_dep, epoxy_dep, rt_dep, win_deps])

xrdcore = static_library('xrdcore', front_sources, framesink_gen,
  include_directories : inc,
  dependencies : [xrdome_core_dep, sdl2_dep, mpv_dep])

# test_probe_run's stand-in for ffprobe: a program, so it runs on Windows too.
fake_ffprobe = executable('fake_ffprobe', 'tests/fake_ffprobe.c')

# Paths the probe tests read fixtures and the fake ffprobe from: C string
# literals, so forward slashes (in a Windows "D:\a" the "\a" is an escape).
fixture_args = [
  '-DXRD_FIXTURE_DIR="' + (meson.current_source_dir() / 'tests/fixtures/ffprobe').replace('\\', '/') + '"',
  '-DXRD_TESTS_DIR="' + (meson.current_source_dir() / 'tests').replace('\\', '/') + '"',
  # test_depthmap: the helper's cache writer, run to cross-check the C reader.
  '-DXRD_TOOLS_DIR="' + (meson.current_source_dir() / 'tools').replace('\\', '/') + '"',
  '-DXRD_PYTHON="' + py.full_path().replace('\\', '/') + '"',
  '-DXRD_FAKE_FFPROBE="' + fake_ffprobe.full_path().replace('\\', '/') + '"',
]

# cmocka 2.0's assert macros use the GNU ", ##__VA_ARGS__" extension, which
# clang reports under -Wpedantic in every test; gcc does not know the flag.
test_c_args = fixture_args + cc.get_supported_arguments('-Wno-gnu-zero-variadic-macro-arguments')

foreach t : test_names
  exe = executable('test_' + t, 'tests/test_' + t + '.c',
    include_directories : inc,
    c_args : test_c_args,
    link_with : xrdcore,
    dependencies : [xrdome_core_dep, cmocka_dep, m_dep, threads_dep, epoxy_dep, egl_dep, sdl2_dep,
                    mpv_dep, rt_dep, win_deps],
    win_subsystem : 'console')
  test(t, exe, depends : fake_ffprobe, env : ['LIBGL_ALWAYS_SOFTWARE=1', 'EGL_PLATFORM=surfaceless',
                      'XRD_TEST_DATA=' + (meson.current_source_dir() / 'tests/data')])
endforeach

# The core links alone: test_core_link against xrdome_core_dep and cmocka
# only (no SDL, mpv or EGL).
if cmocka_dep.found()
  core_link_exe = executable('test_core_link', 'tests/test_core_link.c',
    c_args : test_c_args,
    dependencies : [xrdome_core_dep, cmocka_dep],
    win_subsystem : 'console')
  test('core_link', core_link_exe)
endif

# No X11, GLX, mpv or SDL include in a core file or a src/ header it includes.
test('core_clean', py,
  args : [files('tools/check_core.py'), files('src/core_sources.txt'), meson.current_source_dir()])

# Windows: the manifest sets the process code page to UTF-8 (argv, fopen and
# opendir take UTF-8 file names, as on Linux) and makes it long-path aware.
xrdome_sources = ['src/main.c']
if is_windows
  xrdome_sources += import('windows').compile_resources('packaging/windows/xrdome.rc',
    depend_files : files('packaging/windows/xrdome.manifest'))
endif

xrdome_exe = executable('xrdome', xrdome_sources,
  include_directories : inc,
  link_with : xrdcore,
  dependencies : [xrdome_core_dep, m_dep, threads_dep, epoxy_dep, sdl2_dep, mpv_dep, rt_dep,
                  win_deps],
  win_subsystem : 'console',
  install : true)

# `xrdome --version` needs no display; the macOS bundle's smoke check runs it.
test('version', xrdome_exe, args : ['--version'])

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

# Depth 3D for photos under Xvfb: a step depth in the cache, the eyes' shift.
test('smoke_depth', find_program('tests/smoke_depth.sh'),
  args : [xrdome_exe],
  is_parallel : false,
  timeout : 300)

# The depth helper under Xvfb: tests/fake_depth_helper.py (serve.py's loop, a
# fake depth) started by xrdome, prefetch, a missing or failing helper.
test('smoke_depth_serve', find_program('tests/smoke_depth_serve.sh'),
  args : [xrdome_exe, files('tests/fake_depth_helper.py')],
  is_parallel : false,
  timeout : 300)

# Video frames out to the depth helper under Xvfb: the fake helper maps the
# ring, reads frames and writes depth back; 3D off reads nothing back.
test('smoke_depth_video', find_program('tests/smoke_depth_video.sh'),
  args : [xrdome_exe, files('tests/fake_depth_helper.py')],
  is_parallel : false,
  timeout : 300)

# xrdome-pose-viture: a separate MIT program that reads VITURE's SDK (never
# linked: loaded at run time) and sends the pose to xrdome as OpenTrack UDP.
# Built only with -Dviture_sdk_include=PATH to VITURE's headers.
if get_option('viture_sdk_include') != ''
  subdir('helpers/xrdome-pose-viture')
endif
