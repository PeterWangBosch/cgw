#ifndef BS_ETH_INSTALLER_JOB_H_
#define BS_ETH_INSTALLER_JOB_H_

#include <stdio.h>
#include "mongoose/mongoose.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

const char* bs_get_vers(); // TODO:remove it
void bs_set_get_vers_flag(int v);


union bs_eth_installer_request_data {
  struct bs_app_upgrade_stat stat;
  char info[128];
};

// status of self-installer
#define BS_ETH_INSTALLER_REQ_NOP 0
#define BS_ETH_INSTALLER_PKG_NEW 1
#define BS_ETH_INSTALLER_PREPARE 2
#define BS_ETH_INSTALLER_ACT 3
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

// commands to eth self-installer
#define MSG_TRANSFER_PACKAGE "TRANSFER_PACKAGE"
#define MSG_TRANSFER_PACKAGE_RESULT "TRANSFER_PACKAGE"//it's a bug of sw
#define MSG_REQUEST_VERSIONS "REQUEST_VERSIONS"
#define MSG_REQUEST_VERSIONS_RESULT "REQUEST_VERSIONS_RESULT"
#define MSG_REPORT_STATE "REPORT_STATE"
#define MSG_REQUEST_STATE "REQUEST_STATE"
#define MSG_REQUEST_STATE_RESULT "REQUEST_STATE_RESULT"
#define MSG_PREPARE_ACTIVATION "PREPARE_ACTIVATION"
#define MSG_PREPARE_ACTIVATION_RESULT "PREPARE_ACTIVATION"
#define MSG_ACTVATE "ACTIVATE"
#define MSG_FINALIZE "FINALIZE"
#define MSG_ROLLBACK "ROLLBACK"
enum NodeId
{
  eCGW = 101,
  eBDCM,
  eDLP,
  eIDCM,
  eVSP,
  eARC,
  eDVR,
  eTBOX,
  eVDCM,
  eHDMAP
};
void bs_init_eth_installer_core_request(struct bs_eth_installer_core_request * req,
                                        struct bs_device_app * app);

void * bs_eth_installer_job_thread(void *);
void * bs_eth_installer_job_msg_thread(void *);
void bs_eth_installer_msg_handler(struct mg_connection *, int, void *);

void bs_eth_installer_req_pkg_new(struct bs_device_app *, char *, char *);
void bs_eth_installer_req_vers(struct bs_device_app *, char *);
void bs_eth_installer_prepare(struct bs_device_app *, char *);
void bs_eth_installer_stat(struct bs_device_app *, char *);
//TODO: parameters: (struct bs_device_app *, char *, char *);
void bs_eth_installer_req_act(struct bs_device_app *);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ifdef BS_ETH_INSTALLER_JOB_H_ */

