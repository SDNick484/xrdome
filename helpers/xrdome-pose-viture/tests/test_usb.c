#define _POSIX_C_SOURCE 200809L /* mkdtemp, mkdir */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <cmocka.h>

#include "test_compat.h"
#include "usb_enum.h"

static void hardware_ids(void **state)
{
    (void)state;
    unsigned vid = 0, pid = 0;
    assert_true(viture_parse_hwid("USB\\VID_35CA&PID_101D&MI_00", &vid, &pid));
    assert_int_equal(vid, 0x35CA);
    assert_int_equal(pid, 0x101D);
    assert_true(viture_parse_hwid("USB\\VID_35ca&PID_1019", &vid, &pid));
    assert_int_equal(pid, 0x1019);
    assert_false(viture_parse_hwid("USB\\VID_35CA", &vid, &pid));
    assert_false(viture_parse_hwid("USB\\VID_35CA&PID_10", &vid, &pid));
    assert_false(viture_parse_hwid("USB\\VID_ZZZZ&PID_1019", &vid, &pid));
    assert_false(viture_parse_hwid("", &vid, &pid));
}

#if defined(__linux__)
static void put(const char *dir, const char *dev, const char *file, const char *text)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, dev);
    mkdir(path, 0700);
    snprintf(path, sizeof path, "%s/%s/%s", dir, dev, file);
    FILE *f = fopen(path, "w");
    assert_non_null(f);
    fputs(text, f);
    fclose(f);
}

static void sysfs_scan(void **state)
{
    (void)state;
    char root[] = "/tmp/xrd-viture-usb-XXXXXX";
    assert_non_null(mkdtemp(root));
    put(root, "1-2", "idVendor", "35ca\n");
    put(root, "1-2", "idProduct", "101d\n");
    put(root, "1-3", "idVendor", "35ca\n"); /* same vendor, another product */
    put(root, "1-3", "idProduct", "1019\n");
    put(root, "2-1", "idVendor", "35ca\n"); /* a second port with the Pro: once */
    put(root, "2-1", "idProduct", "101d\n");
    put(root, "1-4", "idVendor", "046d\n"); /* another vendor */
    put(root, "1-4", "idProduct", "c52b\n");
    put(root, "1-5", "idVendor", "35ca\n"); /* no product id */
    put(root, "1-2:1.0", "idVendor", "35ca\n"); /* an interface, not a device */
    put(root, "1-2:1.0", "idProduct", "ffff\n");

    int pids[8];
    int n = viture_usb_scan_sysfs(root, pids, 8);
    assert_int_equal(n, 2);
    int saw_101d = 0, saw_1019 = 0;
    for (int i = 0; i < n; i++) {
        saw_101d += pids[i] == 0x101D;
        saw_1019 += pids[i] == 0x1019;
    }
    assert_int_equal(saw_101d, 1);
    assert_int_equal(saw_1019, 1);

    /* Room for one only. */
    assert_int_equal(viture_usb_scan_sysfs(root, pids, 1), 1);
    /* No such tree. */
    assert_int_equal(viture_usb_scan_sysfs("/nonexistent/sysfs", pids, 8), 0);
    xrd_test_rmtree(root);
}
#endif

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(hardware_ids),
#if defined(__linux__)
        cmocka_unit_test(sysfs_scan),
#endif
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
