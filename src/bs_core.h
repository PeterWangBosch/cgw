#ifndef BS_CORE_H_
#define BS_CORE_H_

#include <stdio.h>
#include "mongoose/mongoose.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

//------------------------------------------------------------------
// 
//------------------------------------------------------------------
struct bs_pkg_info {
  // “yes” or “no”
  char door_module[8];
  char dev_id[32];
  char soft_id[32];
  char release_notes[256];
};

struct bs_app_upgrade_stat {
  char esti_time[32];
  char start_time[32];
  char time_stamp[32];
  // deifferent app might have differnt status set 
  char status[16];
  // raw percentage data, e.g., 0, 55, or 100
  float progress_percent;
};

#define BS_PKG_TYPE_INVALID 0
#define BS_PKG_TYPE_CAN_ECU 1
#define BS_PKG_TYPE_ETH_ECU 2
#define BS_PKG_TYPE_ORCH 3
extern const char *bs_pkg_stat_idle;
extern const char *bs_pkg_stat_new;
extern const char *bs_pkg_stat_loading;
extern const char *bs_pkg_stat_succ;
extern const char *bs_pkg_stat_fail;
struct bs_app_pkg_stat {
  unsigned char type;
  const char *stat;
};

struct bs_app_intaller_proxy {
  unsigned int ip_addr;
  unsigned int port;
  void *thread_id;
  bool thread_exit;
  struct mg_connection *remote;
};

struct bs_device_app {
  char dev_id[32];
  char soft_id[32];
  bool door_module; 
  struct bs_app_pkg_stat pkg_stat;
  struct bs_app_upgrade_stat upgrade_stat;
  struct bs_app_intaller_proxy installer;
};

#define BS_MAX_DEVICE_APP_NUM 128
struct bs_context {
  unsigned long next_conn_id; 
  sock_t core_msg_sock[2];
  struct mg_connection * tlc;
  struct bs_device_app *loading_app;// TODO: support downloading in paralell
  struct bs_device_app apps[BS_MAX_DEVICE_APP_NUM];
};

#define BS_CORE_REQ_INVALID 0
#define BS_CORE_REQ_PKG_NEW 1
#define BS_CORE_REQ_PKG_READY 2
#define BS_CORE_REQ_TDR_RUN 3

struct bs_core_request {
  unsigned long conn_id;
  unsigned int cmd;
  char dev_id[32];
};

struct bs_context * bs_get_core_ctx();
unsigned long bs_get_next_conn_id();
void bs_init_core_request(struct bs_core_request*);
void bs_init_device_app(struct bs_device_app *);
void bs_core_init_ctx();
void bs_core_exit_ctx();

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ifdef BS_CORE_H_ */
