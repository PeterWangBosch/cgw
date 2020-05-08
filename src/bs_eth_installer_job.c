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

unsigned int bs_eth_installer_msg_parse(char* json)
{
  (void) json;
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
    case BS_ETH_INSTALLER_VERS:
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
  struct bs_context * p_ctx= (struct bs_context *) param;
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
      bs_eth_installer_msg_parse(&(io->buf[4]));
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
  static char *resp_header = "{\"node\":1,\"task\":\"TRANSFER_PACKAGE\",\"category\":0,\"payload\":{\"manifest\":[";
  // first 4 bytes for length
  pc += 4;

  // header
  strcpy(msg + pc, resp_header);
  pc += strlen(resp_header);

  strcpy(msg, "{");
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

  printf("-------- raw json to eth installer:----------\n");
  printf("%s", msg);

  app = req->app;
  if (app->job.remote == NULL) {
    printf("app data corrupted: %s", app->dev_id);
    return;
  }

  mg_send(app->job.remote, msg, pc);
}
