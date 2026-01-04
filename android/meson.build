if not have_android
   subdir_done()
endif

android_ss = ss.source_set()

common_user_inc += include_directories('include/host/' / host_arch)
common_user_inc += include_directories('include')

android_ss.add(files(
  'exit.c',
  'fd-trans.c',
  'mmap.c',
  'signal.c',
  'strace.c',
  'syscall.c',
  'thunk.c',
  'uaccess.c',
  'uname.c',

  'android.c',
))
android_ss.add(rt)
android_ss.add(libdw)

syscall_nr_generators = {}

subdir('arm')
subdir('utils')

specific_ss.add_all(when: 'CONFIG_ANDROID', if_true: android_ss)
