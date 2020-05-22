#include "cJSON/cJSON.h"

#include "bs_dlc_apis.h"
#include "bs_core.h"
#include "bs_eth_installer_job.h"

//struct mg_connection * find_remote (struct mg_connection * nc)
//{
//  char addr[32];

//  for (c = mg_next(nc->mgr, NULL); c != NULL; c = mg_next(nc->mgr, c)) {
//    mg_sock_addr_to_str(c, addr, sizeof(addr),
//                        MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
//    if (!(c->flags & MG_F_USER_2)) continue;  // Skip non-client connections
//    mg_send(c, io->buf, io->len);
//  }
//}

static struct cJSON * find_json_child(struct cJSON * root, char * label)
{
  struct cJSON * iterator = NULL;

  iterator = root->child;
  while(iterator) {
    if (strcmp(iterator->string, label) == 0) {
      return iterator;
    }
    iterator = iterator->next;
  }
  return iterator;
}

static int bs_eth_installer_resp_handler(char * cmd, struct cJSON * resp, struct bs_device_app * origin)
{
  (void) resp;
  if (strcmp(cmd, MSG_TRANSFER_PACKAGE_RESULT) == 0) {
    origin->job.internal_stat = BS_ETH_INSTALLER_PKG_NEW;
  } else if (strcmp(cmd, MSG_REQUEST_VERSIONS_RESULT) == 0) {
    // TODO: compare response and recorded versions
    
    // insert to que of eth installer 
    bs_core_req_eth_instl_prepare(origin);
  } else if (strcmp(cmd, MSG_PREPARE_ACTIVATION_RESULT) == 0) {
    bs_core_req_eth_instl_act(origin);
  } else if (strcmp(cmd, MSG_REQUEST_STATE_RESULT) == 0) {
    bs_core_req_eth_instl_act(origin);
  }
  return 1;
}

#define ETH_JSON_OK 0
#define ETH_JSON_WRONG 1
#define ETH_JSON_NO_TASK 3
#define ETH_JSON_NO_RESP 4
#define ETH_JSON_WRONG_RESP 5
unsigned int bs_eth_installer_msg_parse(char* json, struct bs_device_app * app)
{
  int parse_stat = ETH_JSON_OK;
  struct cJSON * root = NULL;
  struct cJSON * child = NULL;
  char * cmd = NULL;

  root = cJSON_Parse(json);
  //TODO: check code of cJSON if memory leave when root is NULL
  if (root == NULL) {
    parse_stat = ETH_JSON_WRONG;
    goto last_step;
  }

  child = find_json_child(root, "task");
  if (!child) {
    parse_stat = ETH_JSON_NO_TASK;
    goto last_step;
  }
  cmd = child->valuestring;

  child = find_json_child(root, "response");
  if (!child) {
    parse_stat = ETH_JSON_NO_RESP;
    goto last_step;
  }

  if (!bs_eth_installer_resp_handler(cmd, child, app)) {
    parse_stat = ETH_JSON_WRONG_RESP;
    goto last_step;
  }

last_step:
  // release memory
  printf("parse status: %d\n", parse_stat);
  cJSON_Delete(root);
  return 0;
}

void bs_init_eth_installer_core_request(struct bs_eth_installer_core_request * req,
                                        struct bs_device_app * app)
{
  req->conn_id = 0; // reserved
  req->cmd = BS_ETH_INSTALLER_PKG_NEW;
  req->app = app;
  memset(req->payload.info, 0, 128);
}

unsigned int bs_eth_installer_core_msg_parse(struct bs_eth_installer_core_request *req)
{
  static char msg[512];

  switch(req->cmd) {
    case BS_ETH_INSTALLER_PKG_NEW:
      bs_eth_installer_req_pkg_new(req, msg);
      break;
    case BS_ETH_INSTALLER_VER:
      break;
    case BS_ETH_INSTALLER_PREPARE:
      bs_eth_installer_prepare(req, msg);
      break;
    case BS_ETH_INSTALLER_VERS:
      bs_eth_installer_req_vers(req, msg);
      break;
    case BS_ETH_INSTALLER_ROLLBACK:
      break;
    default:
      break;
  }
  return 0;
}

void * bs_eth_installer_job_thread(void *param)
{
  struct mg_mgr mgr;
  struct bs_context * p_ctx = (struct bs_context *) param;
  struct bs_eth_installer_core_request req;

  //bs_init_app_upgrade_stat(&(app->upgrade_stat));

  mg_mgr_init(&mgr, NULL);

  printf("=== Start socket server ===\n");
  mg_bind(&mgr, p_ctx->eth_installer_port, bs_eth_installer_msg_handler);
  printf("Listen on port %s\n", p_ctx->eth_installer_port);

  for (;;) {
    // step 1: read msg from sock pair
    if (read(p_ctx->eth_installer_msg_sock[1], &req, sizeof(req)) > 0) {
      bs_eth_installer_core_msg_parse(&req);
      continue;
    }

    // step 2: read msg from socket
    mg_mgr_poll(&mgr, 1000);
  }

  mg_mgr_free(&mgr);

  return NULL;
}

void bs_eth_installer_msg_handler(struct mg_connection *nc, int ev, void *p)
{
  struct mbuf *io = &nc->recv_mbuf;
  struct bs_device_app * app = NULL;
  unsigned int len = 0;
  (void) p;

  switch (ev) {
    case MG_EV_ACCEPT:
      bs_core_eth_installer_up(nc);
      break;
    case MG_EV_RECV:
      // first 4 bytes for length
      len = io->buf[3] + (io->buf[2] << 8) + (io->buf[1] << 16) + (io->buf[0] << 24);
      //TODO: parse JSON

      (void) len;
      app = find_app_by_nc(nc);
      if (app) {
        bs_eth_installer_msg_parse(&(io->buf[4]), app);
      } else {
        printf("response from wrong installer\n");        
      }
      mbuf_remove(io, io->len);       // Discard message from recv buffer
      break;
    case MG_EV_CLOSE:
      bs_core_eth_installer_down(nc);
      break;
    default:
      break;
  }
}


void bs_eth_installer_req_pkg_new(struct bs_eth_installer_core_request *req, char *msg)
{
  struct bs_device_app * app = NULL;
  unsigned int pc = 0;
  char piece[64];
  //TODO: 'node' and 'task' configurable. For now, 109 means VDCM
  static char *resp_header = "{\"node\":109,\"task\":\"TRANSFER_PACKAGE\",\"category\":0,\"payload\":{\"manifest\":[";
  // first 4 bytes for length
  pc += 4;

  // header
  strcpy(msg + pc, resp_header);
  pc += strlen(resp_header);

  strcpy(msg + pc, "{");
  pc += 1;

  //TODO: support multiple software
  sprintf(piece, "\"software_id\":\"%s\",", "SV-1");
  strcpy(msg + pc, piece);
  pc += strlen(piece);

  memset(piece, 0, 64);
  sprintf(piece, "\"filename\":\"%s\",", "SV-1");
  strcpy(msg + pc, piece);
  pc += strlen(piece);

  memset(piece, 0, 64);
  sprintf(piece, "\"version\":\"%s\",", "ver.1");
  strcpy(msg + pc, piece);
  pc += strlen(piece);

  memset(piece, 0, 64);
  sprintf(piece, "\"delta\":\"%s\",", "true");
  strcpy(msg + pc, piece);
  pc += strlen(piece);

  memset(piece, 0, 64);
  sprintf(piece, "\"original_version\":\"%s\",", "ver.0");
  strcpy(msg + pc, piece);
  pc += strlen(piece);

  memset(piece, 0, 64);
  sprintf(piece, "\"type\":\"%s\",", "1");
  strcpy(msg + pc, piece);
  pc += strlen(piece);

  memset(piece, 0, 64);
  sprintf(piece, "\"flashing\":\"%s\",", "0");
  strcpy(msg + pc, piece);
  pc += strlen(piece);

  // TODO: attrs
  memset(piece, 0, 64);
  sprintf(piece, "\"attrs\":[%s]","{\"attr0\":\"a0\"}");
  strcpy(msg + pc, piece);
  pc += strlen(piece);

  // end of sw info
  strcpy(msg + pc, "},");
  pc += 1;    

  // end of manifest
  strcpy(msg + pc, "]");
  pc += 1;

  // end of payload
  strcpy(msg + pc, "}");
  pc += 1;    

  // end of whole json
  strcpy(msg + pc, "}");
  pc += 1;

  // to be safe
  msg[pc] = 0;
  pc += 1;

  // the value of pc is the length
  msg[0] = (char) (pc<< 24);
  msg[1] = (char) (pc<< 16);
  msg[2] = (char) (pc<< 8);
  msg[3] = (char) (pc);

  printf("-------- raw json to eth installer:----------\n");
  printf("%s\n", msg+4);

  app = req->app;
  if (app->job.remote == NULL) {
    printf("app data corrupted: %s", app->dev_id);
    return;
  }

  mg_send(app->job.remote, msg, pc);
}

void bs_eth_installer_req_vers(struct bs_eth_installer_core_request *req, char *msg)
{
  struct bs_device_app * app = NULL;
  unsigned int pc = 0;
  //TODO: 'node' and 'task' configurable. For now, 109 means VDCM
  static char *resp_header = "{\"node\":109,\"task\":\"MSG_REQUEST_VERSIONS\",\"category\":0,\"payload\":{}}";
  // first 4 bytes for length
  pc += 4;

  // header
  strcpy(msg + pc, resp_header);
  pc += strlen(resp_header);

  // to be safe
  msg[pc] = 0;
  pc += 1;

  // the value of pc is the length
  msg[0] = (char) (pc<< 24);
  msg[1] = (char) (pc<< 16);
  msg[2] = (char) (pc<< 8);
  msg[3] = (char) (pc);

  printf("-------- raw json to eth installer:----------\n");
  printf("%s\n", msg+4);

  app = req->app;
  if (app->job.remote == NULL) {
    printf("app data corrupted: %s", app->dev_id);
    return;
  }

  mg_send(app->job.remote, msg, pc);
}

void bs_eth_installer_prepare(struct bs_eth_installer_core_request *req, char *msg)
{
  struct bs_device_app * app = NULL;
  unsigned int pc = 0;
  //TODO: 'node' and 'task' configurable. For now, 109 means VDCM
  static char *resp_header = "{\"node\":109,\"task\":\"MSG_PREPARE_ACTIVATION\",\"category\":0,\"payload\":{\"url\":\"\"}}";
  // first 4 bytes for length
  pc += 4;

  // header
  strcpy(msg + pc, resp_header);
  pc += strlen(resp_header);

  // to be safe
  msg[pc] = 0;
  pc += 1;

  // the value of pc is the length
  msg[0] = (char) (pc<< 24);
  msg[1] = (char) (pc<< 16);
  msg[2] = (char) (pc<< 8);
  msg[3] = (char) (pc);

  printf("-------- raw json to eth installer:----------\n");
  printf("%s\n", msg+4);

  app = req->app;
  if (app->job.remote == NULL) {
    printf("app data corrupted: %s", app->dev_id);
    return;
  }

  mg_send(app->job.remote, msg, pc);
}

void bs_eth_installer_stat(struct bs_eth_installer_core_request *req, char *msg)
{
  struct bs_device_app * app = NULL;
  unsigned int pc = 0;
  //TODO: 'node' and 'task' configurable. For now, 109 means VDCM
  static char *resp_header = "{\"node\":109,\"task\":\"MSG_REQUEST_STATE\",\"category\":0,\"payload\":{}}";
  // first 4 bytes for length
  pc += 4;

  // header
  strcpy(msg + pc, resp_header);
  pc += strlen(resp_header);

  // to be safe
  msg[pc] = 0;
  pc += 1;

  // the value of pc is the length
  msg[0] = (char) (pc<< 24);
  msg[1] = (char) (pc<< 16);
  msg[2] = (char) (pc<< 8);
  msg[3] = (char) (pc);

  printf("-------- raw json to eth installer:----------\n");
  printf("%s\n", msg+4);

  app = req->app;
  if (app->job.remote == NULL) {
    printf("app data corrupted: %s", app->dev_id);
    return;
  }

  mg_send(app->job.remote, msg, pc);
}

