#include "detect/probe_info.h"

#include <string.h>

void xrd_probe_zero(xrd_probe *p)
{
    memset(p, 0, sizeof *p);
    p->sar_num = 1;
    p->sar_den = 1;
}
