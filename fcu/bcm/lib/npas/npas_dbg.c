#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <assert.h>
#include "log.h"
#include "vty.h"
#include "command.h"
#include "netl_netlink.h"
#include "netl_comm.h"
#include "npas_nlmsg.h"
#include "hsl_op_tableid.h"
#include "hsl_msg_header.h"
#include "npas_dbg.h"


extern struct netlsock npas_tbl_link;
DEFUN (  debug_kernel_hsl_hnd,
         debug_kernel_hsl_cmd,
         "debug hsl (all|common|general|ifmgr|msg|fib|fdb|devdrv|pktdrv|platform|gen-pkt|bpdu-pkt|arp-pkt|igmp-pkt|l3if|ipmc) (enable|disable) level <0-5>",
         "debug"
         "hsl debugging"
         "module all "
         "module common"
         "module general"
         "module ifmgr"
         "module msg"
         "module fib"
         "module fdb"
         "module devdrv"
         "module pktdrv"
         "module platform"
         "module generic_pkt"
         "module bpdu_pkt"
         "module arp_pkt"
         "module igmp_pkt"
         "module l3if"
         "module ipmc"
         "Enable debugging"
         "Disable debugging"
         "level "
         "level value")
{
    const char *module_str;
    u_int16_t enable = 0;
    u_int16_t level = 0;
    int ret;
    
    struct {
        struct hsl_msg_header header;
        struct hal_msg_debug_hsl_req v;
    } PACK_ATTR msg;
    
    module_str = argv[0];
    if (!module_str) {
        vty_out(vty, "%% Invalid module%s", VTY_NEWLINE);
        return CMD_WARNING; 

    }
    
    if ( 0 == strncmp (argv[1], "en", 2)) {
        enable = 1;
    } else {
        enable = 0;
    }
    
    
    VTY_GET_INTEGER ("debug levels", level, argv[2]);

    msg.header.type = 0;
    msg.header.length = sizeof(msg);
    msg.header.table_id = TABLE_ID_XXX;
    msg.header.op_type = HSL_TLV_OPERATION_TYPE_UPDATE;
    
    memset (&msg, 0, sizeof(msg));
    msg.v.enable = enable;
    msg.v.level = level;
    memcpy(msg.v.module_str, module_str, NPAS_DBG_M_NAME_LEN);
    ret = npas_nlmsg_sendto_kernel(&npas_tbl_link, &msg, sizeof(msg), HSL_NETL_NLMSG_DB);
    
    return CMD_SUCCESS;
}

int npas_dbg_cmd_init() {
    install_element(ENABLE_NODE, &debug_kernel_hsl_cmd);
    return 0;
}