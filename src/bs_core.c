#include "bs_core.h"

const char *bs_pkg_stat_idle = "idle";
const char *bs_pkg_stat_new = "new";
const char *bs_pkg_stat_loading = "loading";
const char *bs_pkg_stat_succ = "succ";
const char *bs_pkg_stat_fail = "fail";

static sig_atomic_t g_received_signal = 0;
static void signal_handler(int sig_num) 
{
  signal(sig_num, signal_handler);
  g_received_signal = sig_num;
}

static struct bs_context g_ctx;
struct bs_context * bs_get_core_ctx()
{
  return &g_ctx;
}

unsigned long bs_get_next_conn_id()
{
  return ++g_ctx.next_conn_id;
}

void bs_init_core_request(struct bs_core_request* req)
{
  int i = 0;

  req->conn_id = 0;
  req->cmd = BS_CORE_REQ_INVALID;
  for(i=0; i<32; i++) {
    req->dev_id[i] = 0;
  }
}

void bs_init_device_app(struct bs_device_app *app)
{
  int i;

  for (i=0; i<32; i++) {
    app->dev_id[0] = 0;
    app->soft_id[0] = 0;
  }
  app->door_module = false;;

  app->pkg_stat.type = BS_PKG_TYPE_INVALID;
  app->pkg_stat.stat = bs_pkg_stat_succ;

  strcpy(app->upgrade_stat.esti_time, "00:00:00 1900-01-01");
  strcpy(app->upgrade_stat.start_time, "00:00:00 1900-01-01");
  strcpy(app->upgrade_stat.time_stamp, "00:00:00 1900-01-01");
  for (i=0; i<16; i++) {
    app->upgrade_stat.status[i] = 0;
  }
  app->upgrade_stat.progress_percent = 0;

  app->installer.ip_addr = 0;
  app->installer.port = 0;
  app->installer.thread_id = NULL;
  app->installer.thread_exit = false;
}

void bs_core_init_ctx()
{
  int i;

  g_ctx.tlc = NULL;

  g_ctx.next_conn_id = 0;

  if (mg_socketpair(g_ctx.core_msg_sock, SOCK_STREAM) == 0) {
    perror("Opening socket pair");
    exit(1);
  }
  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);

  for(i=0; i<BS_MAX_DEVICE_APP_NUM; i++) {
    bs_init_device_app(g_ctx.apps + i);
  }
}

void bs_core_exit_ctx() {
  closesocket(g_ctx.core_msg_sock[0]);
  closesocket(g_ctx.core_msg_sock[1]);
}

struct bs_device_app * bs_core_find_app(const char *id)
{
  struct bs_device_app * result = NULL;
  int i;

  for (i=0; i<BS_MAX_DEVICE_APP_NUM; i++) {
    if (strcmp(g_ctx.apps[i].dev_id, id) == 0) {
      result = &(g_ctx.apps[i]);
      return result;
    }
  }
  return result;
}

int bs_core_req_pkg_ready(struct bs_core_request* req)
{
  struct bs_device_app *app = bs_core_find_app(req->dev_id);

  if (app == NULL) {
    return 0;
  }
  //assert(app->pkg_stat.type == BS_PKG_TYPE_CAN_ECU||BS_PKG_TYPE_ORCH);
  app->pkg_stat.stat = bs_pkg_stat_succ;

  return 1;
}

int bs_core_req_pkg_new(struct bs_core_request* req)
{
  struct bs_device_app *app = bs_core_find_app(req->dev_id);
  if (app == NULL) {
    return 0;
  }

  switch(app->pkg_stat.type) {
    case BS_PKG_TYPE_CAN_ECU:
    case BS_PKG_TYPE_ORCH:
      // local download & cache
      app->pkg_stat.stat = bs_pkg_stat_loading;
      break;
    case BS_PKG_TYPE_ETH_ECU:
      //TODO: start remote installer job
      break;
    case BS_PKG_TYPE_INVALID:
    default:
      return 0;
  }

  return 1;
}


//--------------------------------------------------------------
//
//--------------------------------------------------------------
void *bs_core_thread(void *param) {
  struct bs_core_request req = {0};
  (void) param;

  while (true) {
    if (g_received_signal != 0) {
      sleep(10);
      continue;
    }

    if (read(g_ctx.core_msg_sock[1], &req, sizeof(req)) < 0) {
      perror("Reading worker sock");
      continue;
    }

    switch (req.cmd) {
      case BS_CORE_REQ_PKG_NEW:
        bs_core_req_pkg_new(&req);
        break;
      case BS_CORE_REQ_PKG_READY:
        bs_core_req_pkg_ready(&req);
        break;
      case BS_CORE_REQ_TDR_RUN:
        break;
      case BS_CORE_REQ_INVALID:
      default:
        break;
    }
  }
  return NULL;
}
