#include "cJSON/cJSON.h"

#include "bs_dlc_apis.h"
#include "bs_core.h"
#include "bs_eth_installer_job.h"

static char msg[512];
static char * ftp_links[4] = {
  "/vdcm1_1.0.0\", \"size\": 3288766, \"checksum\": \"262e747b622a0e2071e223d728d614ba\", \"signature\": \"\", \"credential\": \"\"}",
  "/vdcm2_1.0.0\", \"size\": 207429, \"checksum\": \"9d9a4bf90a550170865b9bc2122bfa54\", \"signature\": \"\", \"credential\": \"\"}",
  "/vdcm3_1.0.0\", \"size\": 6898540, \"checksum\": \"83c1238dbdb2d1bb38c00056f5a70e9d\", \"signature\": \"\", \"credential\": \"\"}",
  "/vdcm4_1.0.0\", \"size\": 12359749, \"checksum\": \"69666b0d1a9b275ca50b3a090e0675fd\", \"signature\": \"\", \"credential\": \"\"}"
};
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
    printf("recv from SelfInstaller: MSG_TRANSFER_PACKAGE_RESULT\n");
    origin->job.internal_stat = BS_ETH_INSTALLER_PKG_NEW;
    //bs_core_req_eth_instl_act(origin);
    bs_eth_installer_prepare(origin, msg);
  } else if (strcmp(cmd, MSG_REQUEST_VERSIONS_RESULT) == 0) {
    printf("recv from SelfInstaller: MSG_REQUEST_VERSIONS_RESULT\n");
    // insert to que of eth installer 
    bs_eth_installer_req_pkg_new(origin, msg, ftp_links[0]);
    bs_eth_installer_req_pkg_new(origin, msg, ftp_links[1]);
    bs_eth_installer_req_pkg_new(origin, msg, ftp_links[2]);
    bs_eth_installer_req_pkg_new(origin, msg, ftp_links[3]);
  } else if (strcmp(cmd, MSG_PREPARE_ACTIVATION_RESULT) == 0) {
    printf("recv from SelfInstaller: MSG_REQUEST_ACTIVATION_RESULT\n");
    // TODO: use current info of current app
    printf("send to SelfInstaller: MSG_REQUEST_ACTIVATE\n");
    bs_eth_installer_req_act(origin, msg, "{\"uri\":\"vdcm1_1.0.0\"}");
    bs_eth_installer_req_act(origin, msg, "{\"uri\":\"vdcm2_1.0.0\"}");
    bs_eth_installer_req_act(origin, msg, "{\"uri\":\"vdcm3_1.0.0\"}");
    bs_eth_installer_req_act(origin, msg, "{\"uri\":\"vdcm4_1.0.0\"}");
  } else if (strcmp(cmd, MSG_REQUEST_STATE_RESULT) == 0) {
    printf("recv from SelfInstaller: MSG_REQUEST_STATE_RESULT\n");
    //bs_core_req_eth_instl_act(origin);// TODO: in realworld, we should send it here?
  } else if (strcmp(cmd, MSG_FINALIZE) == 0) {
    dlc_report_status_finish();
    printf("recv from SelfInstaller: MSG_FINALIZE\n");
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

  switch(req->cmd) {
    case BS_ETH_INSTALLER_PKG_NEW:
      bs_eth_installer_req_pkg_new(req->app, msg, ftp_links[0]);
      bs_eth_installer_req_pkg_new(req->app, msg, ftp_links[1]);
      bs_eth_installer_req_pkg_new(req->app, msg, ftp_links[2]);
      bs_eth_installer_req_pkg_new(req->app, msg, ftp_links[3]);
      break;
    case BS_ETH_INSTALLER_VER:
      break;
    case BS_ETH_INSTALLER_PREPARE:
      bs_eth_installer_prepare(req->app, msg);
      break;
    case BS_ETH_INSTALLER_VERS:
      bs_eth_installer_req_vers(req->app, msg);
      //TODO: in real world, should be trigred by HMI?  
      //bs_eth_installer_req_pkg_new(req, msg, ftp_links[0]);
      //bs_eth_installer_req_pkg_new(req, msg, ftp_links[1]);
      //bs_eth_installer_req_pkg_new(req, msg, ftp_links[2]);
      //bs_eth_installer_req_pkg_new(req, msg, ftp_links[3]);
      //bs_eth_installer_prepare(req, msg);
      //bs_eth_installer_req_act(req, msg, "{\"uri\":\"vdcm1_1.0.0\"}");
      //bs_eth_installer_req_act(req, msg, "{\"uri\":\"vdcm2_1.0.0\"}");
      //bs_eth_installer_req_act(req, msg, "{\"uri\":\"vdcm3_1.0.0\"}");
      //bs_eth_installer_req_act(req, msg, "{\"uri\":\"vdcm4_1.0.0\"}");
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
  struct bs_eth_installer_core_request req;
  struct bs_context * p_ctx = (struct bs_context *) param;

  //bs_init_app_upgrade_stat(&(app->upgrade_stat));
  for (;;) {
     if (read(p_ctx->eth_installer_msg_sock[1], &req, sizeof(req)) > 0) {
      bs_eth_installer_core_msg_parse(&req);
    }
  }

  return NULL;
}

void * bs_eth_installer_job_msg_thread(void *param)
{
  struct mg_mgr mgr;
  struct bs_context * p_ctx = (struct bs_context *) param;

  mg_mgr_init(&mgr, NULL);

  printf("=== Start socket server ===\n");
  mg_bind(&mgr, p_ctx->eth_installer_port, bs_eth_installer_msg_handler);
  printf("Listen on port %s\n", p_ctx->eth_installer_port);

  for (;;) {
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
      printf("Recv raw msg from sel-finstaller: %s", &(io->buf[4]));

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


void bs_eth_installer_req_pkg_new(struct bs_device_app * app, char *msg, char* payload)
{
  static int i; //just for temp uuid
  unsigned int pc = 0;
  //TODO: 'node' and 'task' configurable. For now, 109 means VDCM
  static char *resp_header[] ={
"{\"node\":109,\"task\":\"TRANSFER_PACKAGE\",\"uuid\":\"803953b3-9beb-11ea-aafa-24418ccef95e\",\"category\":0,\"payload\":{\"uri\":\"ftp://",
"{\"node\":109,\"task\":\"TRANSFER_PACKAGE\",\"uuid\":\"813953b3-9beb-11ea-aafa-24418ccef95e\",\"category\":0,\"payload\":{\"uri\":\"ftp://", 
"{\"node\":109,\"task\":\"TRANSFER_PACKAGE\",\"uuid\":\"823953b3-9beb-11ea-aafa-24418ccef95e\",\"category\":0,\"payload\":{\"uri\":\"ftp://",
"{\"node\":109,\"task\":\"TRANSFER_PACKAGE\",\"uuid\":\"833953b3-9beb-11ea-aafa-24418ccef95e\",\"category\":0,\"payload\":{\"uri\":\"ftp://"};
 
  // first 4 bytes for length
  pc += 4;

  // header
  i = (i+1)%4;
  strcpy(msg + pc, resp_header[i]);
  pc += strlen(resp_header[i]);

  // ip addr
  strcpy(msg+pc, bs_get_core_ctx()->tlc_ip);
  pc += strlen(bs_get_core_ctx()->tlc_ip);

  //payload 
  strcpy(msg + pc, payload);
  pc += strlen(payload);

  // to be safe
  msg[pc] = 0;
  pc += 1;

  // the value of pc is the length
  msg[0] = (char) (pc>> 24);
  msg[1] = (char) (pc>> 16);
  msg[2] = (char) (pc>> 8);
  msg[3] = (char) (pc);

  printf("-------- raw json to eth installer:----------\n");
  printf("%s\n", msg+4);

  if (app->job.remote == NULL) {
    printf("app data corrupted: %s", app->dev_id);
    return;
  }

  mg_send(app->job.remote, msg, pc);
}

void bs_eth_installer_req_vers(struct bs_device_app * app, char *msg)
{
  unsigned int pc = 0;
  //TODO: 'node' and 'task' configurable. For now, 109 means VDCM
  static char *resp_header = "{\"node\":109,\"task\":\"REQUEST_VERSIONS\",\"uuid\":\"803953b3-9beb-11ea-aafa-24418ccef951\",\"category\":0,\"payload\":{}}";
  // first 4 bytes for length
  pc += 4;

  // header
  strcpy(msg + pc, resp_header);
  pc += strlen(resp_header);

  // to be safe
  msg[pc] = 0;
  pc += 1;

  // the value of pc is the length
  msg[0] = (char) (pc>> 24);
  msg[1] = (char) (pc>> 16);
  msg[2] = (char) (pc>> 8);
  msg[3] = (char) (pc);

  printf("-------- raw json to eth installer:----------\n");
  printf("%s\n", msg+4);

  if (app->job.remote == NULL) {
    printf("app data corrupted: %s", app->dev_id);
    return;
  }

  mg_send(app->job.remote, msg, pc);
}

void bs_eth_installer_req_act(struct bs_device_app * app, char *msg, char *payload)
{
  static int i;
  unsigned int pc = 0;

  //TODO: 'node' and 'task' configurable. For now, 109 means VDCM
  static char *resp_header[] = {
"{\"node\":109,\"task\":\"ACTVATE\",\"uuid\":\"803953b3-9beb-11ea-aafa-24418ccef951\",\"category\":0,\"payload\":",
"{\"node\":109,\"task\":\"ACTVATE\",\"uuid\":\"803953b3-9beb-11ea-aafa-24418ccef952\",\"category\":0,\"payload\":",
"{\"node\":109,\"task\":\"ACTVATE\",\"uuid\":\"803953b3-9beb-11ea-aafa-24418ccef953\",\"category\":0,\"payload\":",
"{\"node\":109,\"task\":\"ACTVATE\",\"uuid\":\"803953b3-9beb-11ea-aafa-24418ccef954\",\"category\":0,\"payload\":"
  };
  // first 4 bytes for length
  pc += 4;

  i = (i+1)%4;
  
  // header
  strcpy(msg + pc, resp_header[i]);
  pc += strlen(resp_header[i]);

  // payload
  strcpy(msg + pc, payload);
  pc += strlen(payload);

  // end }
  strcpy(msg + pc, "}");
  pc += 1;

  // to be safe
  msg[pc] = 0;
  pc += 1;

  // the value of pc is the length
  msg[0] = (char) (pc>> 24);
  msg[1] = (char) (pc>> 16);
  msg[2] = (char) (pc>> 8);
  msg[3] = (char) (pc);

  printf("-------- raw json to eth installer:----------\n");
  printf("%s\n", msg+4);

  if (app->job.remote == NULL) {
    printf("app data corrupted: %s", app->dev_id);
    return;
  }

  mg_send(app->job.remote, msg, pc);
}


void bs_eth_installer_prepare(struct bs_device_app * app, char *msg)
{
  unsigned int pc = 0;
  //TODO: 'node' and 'task' configurable. For now, 109 means VDCM
  static char *resp_header = "{\"node\":109,\"task\":\"PREPARE_ACTIVATION\",\"category\":0,\"payload\":{}}";
  // first 4 bytes for length
  pc += 4;

  // header
  strcpy(msg + pc, resp_header);
  pc += strlen(resp_header);

  // to be safe
  msg[pc] = 0;
  pc += 1;

  // the value of pc is the length
  msg[0] = (char) (pc>> 24);
  msg[1] = (char) (pc>> 16);
  msg[2] = (char) (pc>> 8);
  msg[3] = (char) (pc);

  printf("-------- raw json to eth installer:----------\n");
  printf("%s\n", msg+4);

  if (app->job.remote == NULL) {
    printf("app data corrupted: %s", app->dev_id);
    return;
  }

  mg_send(app->job.remote, msg, pc);
}

void bs_eth_installer_stat(struct bs_device_app * app, char *msg)
{
  unsigned int pc = 0;
  //TODO: 'node' and 'task' configurable. For now, 109 means VDCM
  static char *resp_header = "{\"node\":109,\"task\":\"REQUEST_STATE\",\"category\":0,\"payload\":{}}";
  // first 4 bytes for length
  pc += 4;

  // header
  strcpy(msg + pc, resp_header);
  pc += strlen(resp_header);

  // to be safe
  msg[pc] = 0;
  pc += 1;

  // the value of pc is the length
  msg[0] = (char) (pc>> 24);
  msg[1] = (char) (pc>> 16);
  msg[2] = (char) (pc>> 8);
  msg[3] = (char) (pc);

  printf("-------- raw json to eth installer:----------\n");
  printf("%s\n", msg+4);

  if (app->job.remote == NULL) {
    printf("app data corrupted: %s", app->dev_id);
    return;
  }

  mg_send(app->job.remote, msg, pc);
}

