/* Give the full FINSH variable table a real entry for Arm's $$Base/$$Limit
 * section symbols, even when an application exports no variables of its own. */
#include <rtthread.h>
#include <finsh.h>
static int rtt_pack_version = 30104;
FINSH_VAR_EXPORT(rtt_pack_version, finsh_type_int, RT-Thread kernel version);
