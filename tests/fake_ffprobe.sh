#!/bin/sh
# Stand-in for ffprobe in tests/test_probe_run.c. Behaviour comes from $FAKE_FFPROBE:
#   ok     print $FAKE_FFPROBE_FILE
#   argv   write the arguments, one per line, to $FAKE_FFPROBE_OUT, then print the fixture
#   hang   sleep 30
#   flood  print an endless stream of x
#   fail   exit 3
#   segv   die from SIGSEGV
case "$FAKE_FFPROBE" in
ok)
    cat "$FAKE_FFPROBE_FILE"
    ;;
argv)
    : > "$FAKE_FFPROBE_OUT"
    for a in "$@"; do
        printf '%s\n' "$a" >> "$FAKE_FFPROBE_OUT"
    done
    cat "$FAKE_FFPROBE_FILE"
    ;;
hang)
    exec sleep 30
    ;;
flood)
    exec yes xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
    ;;
fail)
    exit 3
    ;;
segv)
    kill -SEGV $$
    sleep 1
    ;;
*)
    exit 99
    ;;
esac
