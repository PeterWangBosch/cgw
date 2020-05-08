#ifndef BS_ETH_INSTALLER_JOB_H_
#define BS_ETH_INSTALLER_JOB_H_

#include <stdio.h>
#include "mongoose/mongoose.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

union bs_eth_installer_request_data {
  struct bs_app_upgrade_stat stat;
  char info[128];
};

#define BS_ETH_INSTALLER_REQ_NOP 0
#define BS_ETH_INSTALLER_PKG_NEW 1
#define BS_ETH_INSTALLER_PREPARE 2
#define BS_ETH_INSTALLER_UPGRADE 3
#define BS_ETH_INSTALLER_VER 4
#define BS_ETH_INSTALLER_VERS 5
#define BS_ETH_INSTALLER_ROLLBACK 6
#define BS_ETH_INSTALLER_FINAL 7

struct bs_eth_installer_core_request {
  unsigned long conn_id; // TODO: ??reserved
  unsigned int cmd;
  struct bs_device_app * app;
  union bs_core_request_data payload;
};

void bs_init_eth_installer_core_request(struct bs_eth_installer_core_request * req,
                                        struct bs_device_app * app);

void * bs_eth_installer_job_thread(void *);
void bs_eth_installer_msg_handler(struct mg_connection *, int, void *);

void bs_eth_installer_req_pkg_new(struct bs_eth_installer_core_request *, char *);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ifdef BS_ETH_INSTALLER_JOB_H_ */

