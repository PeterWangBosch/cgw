#include "cJSON/cJSON.h"

#include "bs_dlc_apis.h"
#include "bs_core.h"
#include "bs_eth_installer_job.h"

static char msg[1024];

//TODO: remove it
static char g_vers[512];
static int g_get_vers_flag = 1;
const char * bs_get_vers()
{
  return g_vers;
}

void bs_set_get_vers_flag(int v)
{
  g_get_vers_flag = v;
}


static char * ftp_links[4] = {
"/xcu8.0_kernel_v1.1.2.bins\\\", \\\"size\\\": 6898540, \\\"checksum\\\": \\\"83c1238dbdb2d1bb38c00056f5a70e9d\\\", \\\"signature\\\": \\\"XXXXXX\\\", \\\"credential\\\": \\\"admin:12345\\\"}\"",
"/xcu8.0_rootfs_hh.bin.zip\\\", \\\"size\\\": 12359749, \\\"checksum\\\": \\\"69666b0d1a9b275ca50b3a090e0675fd\\\", \\\"signature\\\": \\\"XXXXXX\\\", \\\"credential\\\": \\\"admin:12345\\\"}\"",
"/xcu8.0_app_hh.bin.zip\\\", \\\"size\\\": 207429, \\\"checksum\\\": \\\"9d9a4bf90a550170865b9bc2122bfa54\\\", \\\"signature\\\": \\\"XXXXXX\\\", \\\"credential\\\": \\\"admin:12345\\\"}\"",
"/mcu_SH0105A2T1.hex\\\", \\\"size\\\": 3288766, \\\"checksum\\\": \\\"262e747b622a0e2071e223d728d614ba\\\", \\\"signature\\\": \\\"XXXXXX\\\", \\\"credential\\\": \\\"admin:12345\\\"}\"",
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

static int bs_eth_ecu_ver_update(struct bs_device_app* app, const char* ver_cstr)
{
    int rc = 0;

    struct cJSON* root = NULL;
    struct cJSON* vers = NULL;
    struct cJSON* ver = NULL;

    char ver_json_str[512] = { 0 };

    int j_i = 0, c_i = 0;
    int c_m = (int)(strlen(ver_cstr));
    int j_m = (int)(sizeof(ver_json_str) - 1);

    for (; c_i < c_m;) {
        if (ver_cstr[c_i] == '\\' && c_i + 1 < c_m && ver_cstr[c_i + 1] == 'n') {
            c_i += 2;
            continue;
        }
        if (ver_cstr[c_i] == '\\' && c_i + 1 < c_m && ver_cstr[c_i + 1] == '"') {
            c_i += 1;
            continue;
        }

        ver_json_str[j_i] = ver_cstr[c_i];
        j_i += 1;
        c_i += 1;

        if (j_i + 1 >= j_m)
            break;
        if (c_i + 1 >= c_m)
            break;
    }
    ver_json_str[j_i] = '\0';

    root = cJSON_Parse(ver_json_str);
    if (root == NULL) {
        fprintf(stderr, 
            "ERROR,UPD_VERS,parse vers to json object fail\n");

        rc = -1;
        goto DONE;
    }
    vers = cJSON_GetObjectItem(root, "versions");
    if (vers == NULL) {
        fprintf(stderr,
            "ERROR,UPD_VERS,[/versions] not find\n");

        rc = -1;
        goto DONE;
    }

    int v = 0;
    memset(app->dev_vers, 0, sizeof(app->dev_vers));
    cJSON_ArrayForEach(ver, vers) {
        SAFE_CPY_STR(app->dev_vers[v].soft_id, ver->string, BS_MAX_SOFT_ID_LEN);
        SAFE_CPY_STR(app->dev_vers[v].soft_ver, ver->valuestring, BS_MAX_SOFT_VER_LEN);

        ++v;
        if (v >= BS_MAX_VER_NUM)
            break;
    }
    //TODO:change report json ?
    //there is no way to get dev_id
    strcpy(app->dev_id, "VDCM");

DONE:
    if (root)
        cJSON_Delete(root);


    return (rc);
}

static int bs_eth_installer_resp_handler(char * cmd, struct cJSON * resp, struct bs_device_app * origin)
{
  static int bin_index = 0;// TODO: from L1_Manifate
  if (strstr(cmd, MSG_TRANSFER_PACKAGE_RESULT) != NULL) {
    printf("recv from SelfInstaller: MSG_TRANSFER_PACKAGE_RESULT\n");
    origin->job.internal_stat = BS_ETH_INSTALLER_PKG_NEW;
    if (bin_index>=0 && bin_index<4) {
      bs_eth_installer_req_pkg_new(origin, msg, ftp_links[bin_index]);
      bin_index++;
    } else if (bin_index == 4) {
      bs_eth_installer_prepare(origin, msg);
      bin_index=-1;
    }
  } else if (strcmp(cmd, MSG_REQUEST_VERSIONS_RESULT) == 0) {
    printf("recv from SelfInstaller: MSG_REQUEST_VERSIONS_RESULT\n");
    // insert to que of eth installer 
    printf("get versions of VDCM: %s \n", resp->valuestring);

    bs_eth_ecu_ver_update(origin, resp->valuestring);

    if (g_get_vers_flag) {      
      bs_set_get_vers_flag(0);
    } else  {      
      bs_eth_installer_stat(origin, msg);
    }
  } else if (strstr(cmd, MSG_PREPARE_ACTIVATION_RESULT) != NULL) {
    printf("recv from SelfInstaller: MSG_PREPARE_ACTIVATION_RESULT\n");
    bs_eth_installer_req_act(origin);

    // TODO: use current info of current app
  } else if (strcmp(cmd, MSG_REQUEST_STATE_RESULT) == 0) {
    printf("recv from SelfInstaller: MSG_REQUEST_STATE_RESULT\n");
    if (bin_index == 0) {
      bs_eth_installer_req_pkg_new(origin, msg, ftp_links[bin_index]);
      bin_index++;
    } else if (bin_index == 4) {
    }
  } else if (strcmp(cmd, MSG_FINALIZE) == 0) {
    bin_index = 0;
    dlc_report_status_finish();
    printf("recv from SelfInstaller: MSG_FINALIZE\n");
  } else if (strcmp(cmd, MSG_REPORT_STATE) == 0) {
    if (resp) {
      if (strstr(resp->valuestring, "\"5")) {
        bin_index = 0;
        dlc_report_status_finish();
      }
    } 
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

  child = find_json_child(root, "payload");
//  if (!child) {
//    child = find_json_child(root, "response");
//    if (!child) {
//      parse_stat = ETH_JSON_NO_RESP;
//      goto last_step;
//    }
//  }

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
      //bs_eth_installer_req_pkg_new(req->app, msg, ftp_links[0]);
      break;
    case BS_ETH_INSTALLER_VER:
      break;
    case BS_ETH_INSTALLER_PREPARE:
      bs_eth_installer_prepare(req->app, msg);
      break;
    case BS_ETH_INSTALLER_VERS:
      bs_eth_installer_req_vers(req->app, msg);
      //TODO: in real world, should be trigred by HMI?  
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

void bs_clean_str(char* in, char* out)
{
  int len = 0;
  len = strlen(in);
  
  int i = 0;
  int j = 0;
  while(i < len) {
    if (in[i] != 32 && in[i] != 13) {
      out[j++] = in[i];
    }  
    i++;
  }
  out[j] = 0;
}

void bs_eth_installer_msg_handler(struct mg_connection *nc, int ev, void *p)
{
    (void)p;

    struct mbuf* io = &nc->recv_mbuf;
    struct bs_device_app* app = NULL;
    char msg[512] = { 0 };

    switch (ev) {
    case MG_EV_ACCEPT: {

        app = bs_core_eth_installer_up(nc);
        if (app) {
            bs_eth_installer_req_vers(app, msg);
        }
    }break;


    case MG_EV_RECV:
        // first 4 bytes for length
        //len = io->buf[3] + (io->buf[2] << 8) + (io->buf[1] << 16) + (io->buf[0] << 24);
        //TODO: parse JSON
        fprintf(stdout, "INFO,ECU_MSG,raw:%s", &(io->buf[4]));

        app = find_app_by_nc(nc, NULL);
        if (!app) {
            fprintf(stderr, "ERR,ECU_MSG,no app find");
            break;
        }

        bs_clean_str(&(io->buf[4]), msg);
        fprintf(stdout, "INFO,ECU_MSG,%s", msg);
        bs_eth_installer_msg_parse(msg, app);

        mbuf_remove(io, io->len);
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
"{\"node\":109,\"task\":\"TRANSFER_PACKAGE\",\"uuid\":\"813953b3-9beb-11ea-aafa-24418ccef951\",\"category\":0,\"payload\":\"{\\\"uri\\\":\\\"ftp://",
"{\"node\":109,\"task\":\"TRANSFER_PACKAGE\",\"uuid\":\"813953b3-9beb-11ea-aafa-24418ccef951\",\"category\":0,\"payload\":\"{\\\"uri\\\":\\\"ftp://", 
"{\"node\":109,\"task\":\"TRANSFER_PACKAGE\",\"uuid\":\"813953b3-9beb-11ea-aafa-24418ccef951\",\"category\":0,\"payload\":\"{\\\"uri\\\":\\\"ftp://",
"{\"node\":109,\"task\":\"TRANSFER_PACKAGE\",\"uuid\":\"813953b3-9beb-11ea-aafa-24418ccef951\",\"category\":0,\"payload\":\"{\\\"uri\\\":\\\"ftp://"};

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

  // end }
  strcpy(msg + pc, "}");
  pc += 1;

  // to be safe
  msg[pc] = 0;
//  pc += 1;

  // the value of pc is the length
  msg[0] = (char) ((pc-4)>> 24);
  msg[1] = (char) ((pc-4)>> 16);
  msg[2] = (char) ((pc-4)>> 8);
  msg[3] = (char) (pc-4);

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
  static char *resp_header = "{\"node\":109,\"task\":\"REQUEST_VERSIONS\",\"category\":0,\"payload\":\"{}\", \"uuid\":\"803953b3-9beb-11ea-aafa-24418ccef951\"}";
  // first 4 bytes for length
  pc += 4;

  // header
  strcpy(msg + pc, resp_header);
  pc += strlen(resp_header);

  // to be safe
  msg[pc] = 0;
//  pc += 1;

  // the value of pc is the length
  msg[0] = (char) ((pc-4)>> 24);
  msg[1] = (char) ((pc-4)>> 16);
  msg[2] = (char) ((pc-4)>> 8);
  msg[3] = (char) (pc-4);

  printf("-------- raw json to eth installer:----------\n");
  printf("%s\n", msg+4);

  if (app->job.remote == NULL) {
    printf("app data corrupted: %s", app->dev_id);
    return;
  }

  mg_send(app->job.remote, msg, pc);
}

void bs_eth_installer_req_act(struct bs_device_app * app)
{
static char *resp_header = "{\"node\": 109, \"task\": \"ACTIVATE\", \"category\": 0, \"payload\": \"{\\\"manifest\\\": [{\\\"software_id\\\": \\\"vdcm_mpu_kernel\\\", \\\"filename\\\": \\\"xcu8.0_kernel_v1.1.2.bins\\\", \\\"version\\\": \\\"1.1.2\\\", \\\"delta\\\": false, \\\"original_version\\\": \\\"\\\", \\\"type\\\": 1, \\\"flashing\\\": 0, \\\"attrs\\\": [{\\\"attr0\\\": \\\"a0\\\"}, {\\\"attr1\\\": \\\"a1\\\"}, {\\\"attr2\\\": \\\"a2\\\"}]}, {\\\"software_id\\\": \\\"vdcm_mpu_rootfs\\\", \\\"filename\\\": \\\"xcu8.0_rootfs_hh.bin.zip\\\", \\\"version\\\": \\\"1.1.2\\\", \\\"delta\\\": false, \\\"original_version\\\": \\\"\\\", \\\"type\\\": 1, \\\"flashing\\\": 0, \\\"attrs\\\": [{\\\"attr0\\\": \\\"a0\\\"}, {\\\"attr1\\\": \\\"a1\\\"}, {\\\"attr2\\\": \\\"a2\\\"}]}, {\\\"software_id\\\": \\\"vdcm_mpu_app\\\", \\\"filename\\\": \\\"xcu8.0_app_hh.bin.zip\\\", \\\"version\\\": \\\"1.1.1\\\", \\\"delta\\\": false, \\\"original_version\\\": \\\"\\\", \\\"type\\\": 1, \\\"flashing\\\": 0, \\\"attrs\\\": [{\\\"attr0\\\": \\\"a0\\\"}, {\\\"attr1\\\": \\\"a1\\\"}, {\\\"attr2\\\": \\\"a2\\\"}]}, {\\\"software_id\\\": \\\"vdcm_mcu_image\\\", \\\"filename\\\": \\\"mcu_SH0105A2T1.hex\\\", \\\"version\\\": \\\"SH0105A2T1\\\", \\\"delta\\\": false, \\\"original_version\\\": \\\"\\\", \\\"type\\\": 1, \\\"flashing\\\": 0, \\\"attrs\\\": [{\\\"attr0\\\": \\\"a0\\\"}, {\\\"attr1\\\": \\\"a1\\\"}, {\\\"attr2\\\": \\\"a2\\\"}]}]}\", \"uuid\": \"4cbd7609-50f7-48a4-86e9-e349368e1329\"}";
  unsigned int pc = 0;
  // first 4 bytes for length
  pc += 4;

  // header
  strcpy(msg + pc, resp_header);
  pc += strlen(resp_header);

  // to be safe
  msg[pc] = 0;
//  pc += 1;

  // the value of pc is the length
  msg[0] = (char) ((pc-4)>> 24);
  msg[1] = (char) ((pc-4)>> 16);
  msg[2] = (char) ((pc-4)>> 8);
  msg[3] = (char) (pc-4);

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
  //static char *resp_header = "{\"node\":109,\"task\":\"PREPARE_ACTIVATION\",\"category\":0,\"payload\":\"{}\",\"uuid\":\"803953b3-9beb-11ea-aafa-24418ccef954\"}";
  static char *resp_header = "{\"node\": 109, \"task\": \"PREPARE_ACTIVATION\", \"category\": 0,\"payload\": \"{\\\"manifest\\\": [{\\\"software_id\\\": \\\"vdcm_mpu_kernel\\\", \\\"filename\\\": \\\"xcu8.0_kernel_v1.1.2.bins\\\", \\\"version\\\": \\\"1.1.2\\\", \\\"delta\\\": false, \\\"original_version\\\": \\\"\\\", \\\"type\\\": 1, \\\"flashing\\\": 0, \\\"attrs\\\": [{\\\"attr0\\\": \\\"a0\\\"}, {\\\"attr1\\\": \\\"a1\\\"}, {\\\"attr2\\\": \\\"a2\\\"}]}, {\\\"software_id\\\": \\\"vdcm_mpu_rootfs\\\", \\\"filename\\\": \\\"xcu8.0_rootfs_hh.bin.zip\\\", \\\"version\\\": \\\"1.1.2\\\", \\\"delta\\\": false, \\\"original_version\\\": \\\"\\\", \\\"type\\\": 1, \\\"flashing\\\": 0, \\\"attrs\\\": [{\\\"attr0\\\": \\\"a0\\\"}, {\\\"attr1\\\": \\\"a1\\\"}, {\\\"attr2\\\": \\\"a2\\\"}]}, {\\\"software_id\\\": \\\"vdcm_mpu_app\\\", \\\"filename\\\": \\\"xcu8.0_app_hh.bin.zip\\\", \\\"version\\\": \\\"1.1.1\\\", \\\"delta\\\": false, \\\"original_version\\\": \\\"\\\", \\\"type\\\": 1, \\\"flashing\\\": 0, \\\"attrs\\\": [{\\\"attr0\\\": \\\"a0\\\"}, {\\\"attr1\\\": \\\"a1\\\"}, {\\\"attr2\\\": \\\"a2\\\"}]}, {\\\"software_id\\\": \\\"vdcm_mcu_image\\\", \\\"filename\\\": \\\"mcu_SH0105A2T1.hex\\\", \\\"version\\\": \\\"SH0105A2T1\\\", \\\"delta\\\": false, \\\"original_version\\\": \\\"\\\", \\\"type\\\": 1, \\\"flashing\\\": 0, \\\"attrs\\\": [{\\\"attr0\\\": \\\"a0\\\"}, {\\\"attr1\\\": \\\"a1\\\"}, {\\\"attr2\\\": \\\"a2\\\"}]}]}\", \"uuid\": \"de5a1807-e9ba-4645-b413-6799eb42494d\"}";


  // first 4 bytes for length
  pc += 4;

  // header
  strcpy(msg + pc, resp_header);
  pc += strlen(resp_header);

  // to be safe
  msg[pc] = 0;
//  pc += 1;

  // the value of pc is the length
  msg[0] = (char) ((pc-4)>> 24);
  msg[1] = (char) ((pc-4)>> 16);
  msg[2] = (char) ((pc-4)>> 8);
  msg[3] = (char) (pc-4);

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
  static char *resp_header = "{\"node\":109,\"task\":\"REQUEST_STATE\",\"category\":0,\"payload\":\"{}\",\"uuid\":\"803953b3-9beb-11ea-aafa-24418ccef954\"}";
  // first 4 bytes for length
  pc += 4;

  // header
  strcpy(msg + pc, resp_header);
  pc += strlen(resp_header);

  // to be safe
  msg[pc] = 0;
//  pc += 1;

  // the value of pc is the length
  msg[0] = (char) ((pc-4)>> 24);
  msg[1] = (char) ((pc-4)>> 16);
  msg[2] = (char) ((pc-4)>> 8);
  msg[3] = (char) (pc-4);

  printf("-------- raw json to eth installer:----------\n");
  printf("%s\n", msg+4);

  if (app->job.remote == NULL) {
    printf("app data corrupted: %s", app->dev_id);
    return;
  }

  mg_send(app->job.remote, msg, pc);
}

