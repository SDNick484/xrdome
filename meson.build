project('xrdome', 'c',
  version : '0.1.0',
  default_options : ['c_std=c11', 'warning_level=3'])

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : true)
cmocka_dep = dependency('cmocka')
threads_dep = dependency('threads')
epoxy_dep = dependency('epoxy')
egl_dep = dependency('egl')
py = find_program('python3')

shaders_gen = custom_target('shaders_gen',
  input : ['src/render/shaders/vr180.vert', 'src/render/shaders/vr180.frag'],
  output : 'shaders_gen.h',
  command : [py, files('tools/embed_shaders.py'), '@OUTPUT@',
             'VR180_VERT=@INPUT0@', 'VR180_FRAG=@INPUT1@'])

# Core sources: append one line per task.
core_sources = files(
  'src/math/quat.c',
  'src/render/projection.c',
  'src/config.c',
  'src/util/timing.c',
  'src/pose/opentrack_packet.c',
  'src/pose/pose.c',
  'src/pose/pose_null.c',
  'src/pose/pose_udp_opentrack.c',
  'src/track/tracker.c',
  'src/render/renderer_gl.c',
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
]

inc = include_directories('src')

xrdcore = static_library('xrdcore', core_sources, shaders_gen,
  include_directories : inc,
  dependencies : [m_dep, threads_dep, epoxy_dep])

foreach t : test_names
  exe = executable('test_' + t, 'tests/test_' + t + '.c',
    include_directories : inc,
    link_with : xrdcore,
    dependencies : [cmocka_dep, m_dep, threads_dep, epoxy_dep, egl_dep])
  test(t, exe, env : ['LIBGL_ALWAYS_SOFTWARE=1', 'EGL_PLATFORM=surfaceless'])
endforeach
