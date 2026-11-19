# xrdome-pose-viture: reads the head pose of VITURE glasses through VITURE's
# SDK library, loaded at run time, and sends it as OpenTrack UDP. MIT licence
# (LICENSE here); shares no source with xrdome. Configured only when
# -Dviture_sdk_include=PATH is set (see the top-level meson.build).

fs = import('fs')
viture_inc_opt = get_option('viture_sdk_include')
if not fs.is_absolute(viture_inc_opt)
  viture_inc_opt = meson.project_source_root() / viture_inc_opt
endif
if not fs.is_dir(viture_inc_opt) or not fs.exists(viture_inc_opt / 'viture_glasses_provider.h')
  error('viture_sdk_include: no viture_glasses_provider.h in ' + viture_inc_opt)
endif
# meson refuses an absolute include path inside the source tree (the CI's
# stand-in headers): there it takes a path relative to this directory, which
# is two levels below the root.
viture_root = meson.project_source_root() + '/'
if viture_inc_opt.startswith(viture_root)
  viture_inc = include_directories('../../' + viture_inc_opt.split(viture_root)[1])
else
  viture_inc = include_directories(viture_inc_opt)
endif

# dlopen: libdl before glibc 2.34; Windows loads with LoadLibraryW.
viture_deps = [m_dep, threads_dep]
viture_args = []
if is_windows
  foreach l : ['ws2_32', 'setupapi']
    viture_deps += cc.find_library(l)
  endforeach
elif is_macos
  viture_deps += dependency('appleframeworks', modules : ['IOKit', 'CoreFoundation'])
else
  viture_deps += cc.find_library('dl', required : false)
endif

viture_common = static_library('viture_common',
  'bridge.c', 'convert.c', 'options.c', 'packet.c', 'udp_out.c', 'usb_enum.c',
  'viture_loader.c', 'xrdriver_check.c',
  include_directories : viture_inc,
  dependencies : viture_deps)

viture_exe = executable('xrdome-pose-viture', 'main.c',
  include_directories : viture_inc,
  link_with : viture_common,
  dependencies : viture_deps,
  win_subsystem : 'console',
  install : true)

# The fake SDK the tests load: same entry points, fake poses (tests/fake_sdk.c),
# built against whichever headers viture_sdk_include names. A second copy
# lacks xr_device_provider_start, for the loader's missing-symbol path.
fake_sdk = shared_library('fake_viture', 'tests/fake_sdk.c',
  name_prefix : '',
  include_directories : viture_inc,
  dependencies : [m_dep, threads_dep])
fake_sdk_nostart = shared_library('fake_viture_nostart', 'tests/fake_sdk.c',
  name_prefix : '',
  c_args : ['-DFAKE_OMIT_START'],
  include_directories : viture_inc,
  dependencies : [m_dep, threads_dep])

viture_test_args = [
  '-DXRD_FAKE_SDK="' + fake_sdk.full_path().replace('\\', '/') + '"',
  '-DXRD_FAKE_SDK_NOSTART="' + fake_sdk_nostart.full_path().replace('\\', '/') + '"',
  '-DXRD_FAKE_SDK_SOURCE="' + (meson.current_source_dir() / 'tests/fake_sdk.c').replace('\\', '/') + '"',
]
viture_test_args += cc.get_supported_arguments('-Wno-gnu-zero-variadic-macro-arguments')

foreach t : ['convert', 'options', 'packet', 'loader', 'usb', 'xrdriver_check', 'bridge']
  exe = executable('test_viture_' + t, 'tests/test_' + t + '.c',
    include_directories : [viture_inc, inc, include_directories('../../tests')],
    c_args : viture_test_args,
    link_with : [viture_common, xrdcore],
    dependencies : [cmocka_dep, m_dep, threads_dep, epoxy_dep, sdl2_dep, mpv_dep, rt_dep, win_deps,
                    viture_deps],
    win_subsystem : 'console')
  test('viture_' + t, exe,
    depends : [fake_sdk, fake_sdk_nostart],
    env : ['XRD_TEST_DATA=' + meson.current_source_dir() / 'tests'])
endforeach

# The helper as a program: usage and error exits, packets over UDP, the display
# mode put back after --sbs and on SIGINT, the rate cap.
test('viture_process', py,
  args : [files('tests/test_process.py'), viture_exe, fake_sdk],
  timeout : 120)
