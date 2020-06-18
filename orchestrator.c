/*
 * Copyright (c) 2014 Cesanta Software Limited
 * All rights reserved
 */

#include "stdio.h"
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "verison.h"
#include "cJSON/cJSON.h"
#include "mongoose/mongoose.h"
#include "src/bs_core.h"
#include "src/file_utils.h"
#include "src/bs_dlc_apis.h"
#include "src/bs_eth_installer_job.h"
#include "src/bs_cgw_utils.h"

//-----------------------------------------------------------------------
// Protocol JSON parser
//-----------------------------------------------------------------------
const char * api_resp_pkg_proto_http = "http";//TODO: in future, supports ftps, tftp
const char * api_resp_err_succ = "succ";
const char * api_resp_err_devid = "err devid";
const char * api_resp_err_payload = "err payload";
const char * api_resp_err_fail = "fail"; 

#define JSON_INIT 0
#define JSON_HAS_DEVID 1
#define JSON_HAS_PAYLOAD 2
typedef int (* cJSONHookFn) (const cJSON *);

//------------------------------------------------------------------
// Global
//------------------------------------------------------------------
static const char *s_http_port = "8018";
static struct mg_serve_http_opts s_http_server_opts;
static bs_l1_manifest_t g_l1_mani = { 0 };

//------------------------------------------------------------------
// Handle core data
//------------------------------------------------------------------


//------------------------------------------------------------------
// HTTP Message handler
//------------------------------------------------------------------

// for live testing
static void handle_test_live(struct mg_connection *nc, int ev, void *p) {
  (void) ev;
  (void) p;

  // update ip addr record
  if (mg_sock_addr_to_str(&(nc->sa),
                          bs_get_core_ctx()->tlc_ip, 32,
                          MG_SOCK_STRINGIFY_IP) <= 0) {
    memset(bs_get_core_ctx()->tlc_ip, 0, 32);
    strcpy(bs_get_core_ctx()->tlc_ip, "127.0.0.1");
    return;
  }

  //dlc_report_status_start();
  //dlc_report_status_finish();
  //dlc_report_status_down();

  mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
  mg_printf_http_chunk(nc, "{ \"result\": \"live\" }");
  mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}

// for ECU versions testing
static void handle_test_vers(struct mg_connection *nc, int ev, void *p) {
  static char msg[512];

  (void) ev;
  (void) p;

  bs_set_get_vers_flag(1);

  bs_eth_installer_req_vers(&(bs_get_core_ctx()->apps[1]), msg);

  mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
  mg_printf_http_chunk(nc, "{ \"result\": \"%s\" }", bs_get_vers());
  mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}

static void handle_pkg_new(struct mg_connection* nc, int ev, void* p) {

    struct http_message* hm = (struct http_message*)p;
    const char* result = api_resp_err_succ;
    (void)ev;

    const char* l1_mani_file_path = "/data/etc/orchestrator/cgw_l1_manifest.json";
    int rc = JCFG_ERR_OK;

    // update dlc ip addr record
    bs_get_core_ctx()->tlc = nc;
    memset(bs_get_core_ctx()->tlc_ip, 0, 32);
    if (mg_sock_addr_to_str(&(nc->sa), bs_get_core_ctx()->tlc_ip, 32, MG_SOCK_STRINGIFY_IP) <= 0) {
        strcpy(bs_get_core_ctx()->tlc_ip, "127.0.0.1");
        return;
    }


    if (!hm || !hm->body.p) {
        return;
    }

    printf("/pkg/new :raw msg from DLC: %s\n", hm->body.p);
    bs_l1_manifest_t l1_mani = { 0 };

    rc = bs_store_and_parse_l1_manifest(hm->body.p, &l1_mani);
    if (JCFG_ERR_OK == rc) {
        printf("/pkg/new :recv l1_mani form DLC\n");
        l1_mani.status = BS_DEV_TYPE_IDLE;
        memcpy(&g_l1_mani, &l1_mani, sizeof(g_l1_mani));

        rc = bs_save_l1_manifest(l1_mani_file_path, &l1_mani);
        if (JCFG_ERR_OK == rc) {
            printf("/pkg/new :save l1_mani success\n");
        }
        else {
            printf("/pkg/new :save l1_mani failed\n");
        }
    }
    else {
        printf("/pkg/new :parse l1_mani failed\n");
    }

    if (JCFG_ERR_OK != rc) {
        result = api_resp_err_fail;
    }

    //req backgroud thread to down pkg if nessesary
    if (JCFG_ERR_OK == rc) {
        struct bs_core_request core_req;

        bs_init_core_request(&core_req);
        core_req.cmd = BS_CORE_REQ_PKG_NEW;
        core_req.payload.l1_mani = &g_l1_mani;

        printf("Core request PKG_NEW\n");
        if (write(bs_get_core_ctx()->core_msg_sock[0], &core_req, sizeof(core_req)) < 0) {
            printf("Writing core sock error!\n");
            result = api_resp_err_fail;
        }
    }

    //send resp to dlc
    mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    mg_printf_http_chunk(nc, "{ \"result\": \"%s\" }", result);
    mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */

}


static void handle_pkg_stat(struct mg_connection *nc, int ev, void* ev_data) {
  (void) ev;

  struct http_message* hm = (struct http_message*)ev_data;
  if (!hm || hm->body.len <= 0) {
      return;
  }

  char stat_msg[256] = { 0 };
  int cgw_stat = bs_cgw_get_stat();
  if (CGW_STAT_PKG_DOWNING == cgw_stat ||
      CGW_STAT_PKG_NEW == cgw_stat) {

      snprintf(stat_msg, sizeof(stat_msg), "{ \"stat\": \"%s\" }", "down");
  }
  else if (CGW_STAT_PKG_READY == cgw_stat) {
      snprintf(stat_msg, sizeof(stat_msg), "{ \"stat\": \"%s\" }", "ready");
  }
  else if (CGW_STAT_PKG_FAIL == cgw_stat) {
      snprintf(stat_msg, sizeof(stat_msg), "{ \"stat\": \"%s\" }", "fail");
  }
  else {
      snprintf(stat_msg, sizeof(stat_msg), "{ \"stat\": \"%s\" }", "none");
  }


  mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
  mg_printf_http_chunk(nc, stat_msg);
  mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */

  fprintf(stdout, "/api/pkg/stat %d %s\n\n", 200, stat_msg);
}

static void handle_pkg_inst(struct mg_connection* nc, int ev, void* p) 
{
    (void)ev;
    
    struct http_message* hm = (struct http_message*)p;
    if (!hm || !hm->body.p || hm->body.len <= 0) {
        return;
    }
    fprintf(stdout, "-> /api/pkg/inst  %s\n\n", hm->body.p);



    char rsp_msg[256] = { 0 };
    int cgw_stat = bs_cgw_get_stat();
    if (CGW_STAT_PKG_READY == cgw_stat) {

        //req backgroud thread to inst pkg
        struct bs_core_request core_req;

        bs_init_core_request(&core_req);
        core_req.cmd = BS_CORE_REQ_PKG_INST;
        core_req.payload.l1_mani = &g_l1_mani;

        fprintf(stdout, "Core request PKG_INST\n");
        if (write(bs_get_core_ctx()->core_msg_sock[0], &core_req, sizeof(core_req)) < 0) {
            fprintf(stderr, "Writing core sock fail(%d)\n", errno);
            snprintf(rsp_msg, sizeof(rsp_msg), "{ \"result\":\"fail\",  \"err_msg\":\"orch internal error\"}");
        }
        else {
            snprintf(rsp_msg, sizeof(rsp_msg), "{ \"result\":\"succ\",  \"err_msg\":\"pkg inst on going\"}");
        }
    }
    else  {
        if (BS_CORE_REQ_PKG_INST == cgw_stat ||
            BS_CORE_REQ_PKG_RUN == cgw_stat) {
            snprintf(rsp_msg, sizeof(rsp_msg), "{ \"result\":\"succ\",  \"err_msg\":\"pkg inst has going\"}");
        }
        else {
            snprintf(rsp_msg, sizeof(rsp_msg), "{ \"result\":\"fail\",  \"err_msg\":\"pkg inst can not run(%d)\"}", cgw_stat);
        }
    }

    mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    mg_printf_http_chunk(nc, rsp_msg);
    mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */

    fprintf(stdout, "<- /api/pkg/inst %d %s\n\n", 200, rsp_msg);
}

static void handle_tdr_stat(struct mg_connection *nc, int ev, void *p) {
  struct http_message *hm = (struct http_message *) p;

  int parse_stat = JSON_INIT;
  struct cJSON * root = NULL;
  struct cJSON * iterator = NULL;
  char * dev_id = NULL;
  struct bs_device_app * app = NULL;
  char * msg_buf = bs_get_safe_str_buf();

  if (ev == MG_EV_CLOSE || !hm || !hm->body.p)
    return;

  printf("/tdr/stat raw msg from TLC: %s\n", hm->body.p);
  root = cJSON_Parse(hm->body.p);
  //TODO: check code of cJSON if memory leave when root is NULL
  if (root == NULL) {
    sprintf(msg_buf, "{\"error\":\"%s\"", api_resp_err_fail);
    goto last_step;
  }

  iterator = root->child;
  while(iterator) {
    if (strcmp(iterator->string, "dev_id") == 0) {
      parse_stat = JSON_HAS_DEVID;
      dev_id = iterator->valuestring;
      break;
    }
    iterator = iterator->next;
  }
  if (parse_stat != JSON_HAS_DEVID) {
    sprintf(msg_buf, "{\"error\":\"%s\"", api_resp_err_devid);
    goto last_step;
  }

  app = bs_core_find_app(dev_id);
  if (!app) {
    sprintf(msg_buf, "{\"error\":\"%s\"", api_resp_err_devid);
    goto last_step;
  }

  // response
  bs_print_json_upgrade_stat(app, msg_buf);

last_step:
  // TODO: configuable to tftp, https
  mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
  mg_printf_http_chunk(nc, "{\"result\":%s}", msg_buf);
  mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
  // release memory
  cJSON_Delete(root);
}

static void handle_tdr_run(struct mg_connection *nc, int ev, void *p) {
  struct http_message *hm = (struct http_message *) p;
  const char *result = api_resp_err_succ;

  int parse_stat = JSON_INIT;
  //static char response[1024];
  struct cJSON * root = NULL;
  struct cJSON * iterator = NULL;
  struct cJSON * payload = NULL;
  char * dev_id = NULL;

  struct bs_core_request core_req;

  if (ev == MG_EV_CLOSE || !hm || !hm->body.p)
    return;

  printf("Raw msg from TLC: %s\n", hm->body.p);
  if (!hm->body.p)
    return;

  root = cJSON_Parse(hm->body.p);
  //TODO: check code of cJSON if memory leave when root is NULL
  if (root == NULL) {
    result = api_resp_err_fail;
    goto last_step; 
  }

  iterator = root->child;
  while(iterator) {
    if (strcmp(iterator->string, "dev_id") == 0) {
      parse_stat = JSON_HAS_DEVID;
      dev_id = iterator->valuestring;
      break;
    }
    iterator = iterator->next;
  }
  if (parse_stat != JSON_HAS_DEVID) {
    result = api_resp_err_devid;
    goto last_step;
  } 

  payload = cJSON_GetObjectItem(root, "payload");
  if (!payload) {
    result = api_resp_err_payload;
    goto last_step; 
  }

  // forward to core
  bs_init_core_request(&core_req);
  strcpy(core_req.dev_id, dev_id);
  core_req.cmd = BS_CORE_REQ_TDR_RUN;
  fprintf(stdout, "Core request TDR_RUN!\n");
  if (write(bs_get_core_ctx()->core_msg_sock[0], &core_req, sizeof(core_req)) < 0) {
    fprintf(stdout, "Writing core sock error!\n");
    result = api_resp_err_fail;
    goto last_step; 
  }

last_step:
  // TODO: configuable to tftp, https
  mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
  mg_printf_http_chunk(nc, "{ \"result\": \"%s\" }", result);
  mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
  // release memory
  cJSON_Delete(root);
}

// Http uploading file
static void handle_upload(struct mg_connection *nc, int ev, void *p) {
  struct file_writer_data *data = (struct file_writer_data *) nc->user_data;
  struct mg_http_multipart_part *mp = (struct mg_http_multipart_part *) p;
  struct bs_core_request core_req;

  switch (ev) {
    case MG_EV_HTTP_PART_BEGIN: {
      if (data == NULL) {
        data = calloc(1, sizeof(struct file_writer_data));
        data->fp = on_write("/data/var/orchestrator/wpc.1.0.0");//TODO: read file name from context
        data->bytes_written = 0;

        if (data->fp == NULL) {
          mg_printf(nc, "%s",
                    "HTTP/1.1 500 Failed to open a file\r\n"
                    "Content-Length: 0\r\n\r\n");
          mg_send_http_chunk(nc, "", 0);
          nc->flags |= MG_F_SEND_AND_CLOSE;
          free(data);
          return;
        }
        nc->user_data = (void *) data;
        //g_ctx.pkg_stat = "downloading";
      }
      break;
    }
    case MG_EV_HTTP_PART_DATA: {
      if (fwrite(mp->data.p, 1, mp->data.len, data->fp) != mp->data.len) {
        mg_printf(nc, "%s",
                  "HTTP/1.1 500 Failed to write to a file\r\n"
                  "Content-Length: 0\r\n\r\n");
        mg_send_http_chunk(nc, "", 0);
        nc->flags |= MG_F_SEND_AND_CLOSE;
        return;
      }
      data->bytes_written += mp->data.len;
      break;
    }
    case MG_EV_HTTP_PART_END: {
      mg_printf(nc,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Connection: close\r\n\r\n"
                "Written %ld of POST data to a temp file successfully\n\n",
                (long) ftell(data->fp));
      mg_send_http_chunk(nc, "", 0);

      nc->flags |= MG_F_SEND_AND_CLOSE;
      fclose(data->fp);

      // forward to core
      bs_init_core_request(&core_req);
      strcpy(core_req.dev_id, bs_get_core_ctx()->loading_app->dev_id);
      core_req.conn_id = (unsigned long) nc->user_data;
      core_req.cmd = BS_CORE_REQ_PKG_READY;
      fprintf(stdout, "Core request: PKG_READY\n");
      if (write(bs_get_core_ctx()->core_msg_sock[0], &core_req, sizeof(core_req)) < 0) {
        fprintf(stdout, "Writing core sock error!");
      }

      //bs_get_core_ctx()->loading_app->pkg_stat.stat = bs_pkg_stat_succ;
      //bs_get_core_ctx()->loading_app = NULL;

      free(data);
      nc->user_data = NULL;
      break;
    }
  }
}

static void dlc_handle_background_msg()
{
    sock_t fd = bs_get_core_ctx()->core_msg_sock[0];
    struct timeval tv = { 0 };

    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(fd, &rd);

    int rc = select(fd + 1, &rd, NULL, NULL, &tv);
    //= 0 : timeout
    //< 0 : fail
    //> 0 : event num
    if (rc <= 0)
        return;

    struct bs_core_request req = { 0 };
    if (read(fd, &req, sizeof(req)) <= 0) {
        perror("Error reading worker notify sock");
        return;
    }

    switch (req.cmd) {
    case BS_CORE_REQ_PKG_READY:
        fprintf(stdout, "Main_Thrd:Recv background cmd: BS_CORE_REQ_PKG_READY\n");
        break;
    case BS_CORE_REQ_PKG_RUN:
        fprintf(stdout, "Main_Thrd:Recv background cmd: BS_CORE_REQ_PKG_RUN\n");
        break;
    case BS_CORE_REQ_PKG_SUCC:
        fprintf(stdout, "Main_Thrd:Recv background cmd: BS_CORE_REQ_PKG_SUCC\n");
        break;
    case BS_CORE_REQ_PKG_FAIL:
        fprintf(stdout, "Main_Thrd:Recv background cmd: BS_CORE_REQ_PKG_FAIL\n");
        break;
    default:
        fprintf(stdout, "Main_Thrd:Recv background cmd: UNKOWN=%d\n", req.cmd);
        break;
    }
}

static void dlc_msg_handler(struct mg_connection *nc, int ev, void *ev_data) {

    switch (ev) {
    case MG_EV_HTTP_REQUEST:
        mg_serve_http(nc, ev_data, s_http_server_opts);
        break;
    case MG_EV_POLL:
        //proc background thread notify
        dlc_handle_background_msg();
        break;
    }
}

static void orch_report_inventory()
{
    char* msg = NULL;
    cJSON* root = NULL;
    cJSON* ele = NULL;
    cJSON* inv = NULL;
    cJSON* ecu = NULL;
    cJSON* ver = NULL;
    cJSON* vers = NULL;

    struct bs_context* g_ctx = bs_get_core_ctx();
    if (g_ctx->tlc == NULL) {
        fprintf(stdout, "INFO,RPT_INV,<-,DLC not online\n");
        return;
    }

    //
    root = cJSON_CreateObject();
    if (!root) {
        fprintf(stderr, "ERR,RPT_INV,alloc json root object fail\n");
        goto DONE;
    }
    ///orchestrator
    ele = cJSON_CreateString(ORCH_VER" "ORCH_MAK);
    if (!ele) {
        fprintf(stderr, "ERR,RPT_INV,alloc orch ver object fail\n");
        goto DONE;
    }
    cJSON_AddItemToObject(root, "orchestrator", ele);
    ///orchestrator/inventory
    inv = cJSON_CreateArray();
    if (!inv) {
        fprintf(stderr, "ERR,RPT_INV,alloc inv array fail\n");
        goto DONE;
    }
    cJSON_AddItemToObject(root, "inventory", inv);
    for (int i = 0; i < BS_MAX_DEVICE_APP_NUM; ++i) {
        struct bs_device_app* app = &g_ctx->apps[i];
        if (!app->slot_used || !app->dev_id[0])
            continue;
        /*
        {
            "ecu": "WPC",
            "softwareList": [{
                    "softwareId": "WPC",
                    "version": "WPC_App_V1.0",
                    "lastUpdated": "19000101 000000",
                    "servicePack": "unknown",
                    "campaign": "unknown"
                }
            ]
        }
        */

        ecu = cJSON_CreateObject();
        if (!ecu) {
            fprintf(stderr, "ERR,RPT_INV,alloc ecu object fail\n");
            goto DONE;
        }

        ele = cJSON_CreateString(app->dev_id);
        if (!ele) {
            fprintf(stderr, "ERR,RPT_INV,alloc ecu/dev_id string fail\n");
            goto DONE;
        }
        cJSON_AddItemToObject(ecu, "ecu", ele);

        vers = cJSON_CreateArray();
        if (!vers) {
            fprintf(stderr, "ERR,RPT_INV,alloc inv/vers array fail\n");
            goto DONE;
        }
        cJSON_AddItemToObject(ecu, "softwareList", vers);

        for (int v = 0; v < BS_MAX_VER_NUM; ++v) {
            if (!app->dev_vers[v].soft_id[0])
                break;

            ver = cJSON_CreateObject();
            if (!ver) {
                fprintf(stderr, "ERR,RPT_INV,alloc inv/vers[%d] object fail\n", v);
                goto DONE;
            }
            else
            {
                ele = cJSON_CreateString(app->dev_vers[v].soft_id);
                if (!ele) {
                    fprintf(stderr, "ERR,RPT_INV,alloc vers[%d]/softwareId string fail\n", v);
                    goto DONE;
                }
                cJSON_AddItemToObject(ver, "softwareId", ele);
                //
                ele = cJSON_CreateString(app->dev_vers[v].soft_ver);
                if (!ele) {
                    fprintf(stderr, "ERR,RPT_INV,alloc vers[%d]/version string fail\n", v);
                    goto DONE;
                }
                cJSON_AddItemToObject(ver, "version", ele);
                //TODO:
                ele = cJSON_CreateString("092230 20200618");
                if (!ele) {
                    fprintf(stderr, "ERR,RPT_INV,alloc vers[%d]/lastUpdated string fail\n", v);
                    goto DONE;
                }
                cJSON_AddItemToObject(ver, "lastUpdated", ele);
                //
                ele = cJSON_CreateString("");
                if (!ele) {
                    fprintf(stderr, "ERR,RPT_INV,alloc vers[%d]/servicePack string fail\n", v);
                    goto DONE;
                }
                cJSON_AddItemToObject(ver, "servicePack", ele);
                //
                ele = cJSON_CreateString("");
                if (!ele) {
                    fprintf(stderr, "ERR,RPT_INV,alloc vers[%d]/campaign string fail\n", v);
                    goto DONE;
                }
                cJSON_AddItemToObject(ver, "campaign", ele);
            }
            //
            cJSON_AddItemToArray(vers, ver);
        }
        cJSON_AddItemToArray(inv, ecu);
    }

    msg = cJSON_PrintUnformatted(root);
    if (!msg) {
        fprintf(stderr, "ERR,RPT_INV,cJSON_PrintUnformatted fail\n");
        goto DONE;
    }


    mg_printf(g_ctx->tlc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    mg_printf_http_chunk(g_ctx->tlc, msg);
    mg_send_http_chunk(g_ctx->tlc, "", 0); /* Send empty chunk, the end of response */

    fprintf(stdout, "INFO,RPT_INV,<-,%d,%s\n\n", 200, msg);

DONE:
    if (root)
        cJSON_Delete(root);
    if (msg)
        cJSON_free(msg);
}

static struct bs_device_app* bs_core_ecu_online(struct mg_connection* nc, const char* ecu_ip)
{
    struct bs_context* g_ctx = bs_get_core_ctx();
    struct bs_device_app* app = NULL;


    fprintf(stdout, "INFO,ECU_ONLINE,%s\n", ecu_ip);

    app = find_app_by_nc(nc, ecu_ip);

    //first time online
    //find a free app slot to store
    if (!app) {
        for (int i = 0; i < BS_MAX_DEVICE_APP_NUM; i++) {
            if (!g_ctx->apps[i].slot_used) {
                app = &(g_ctx->apps[i]);
                app->slot_used = true;
            }
        }

        if (!app) {
            fprintf(stderr, 
                "ERROR,ECU_ONLINE,no free app slot\n");
            goto DONE;
        }
    }

    //update lasted connection    
    memcpy(app->job.ip_addr, ecu_ip, sizeof(app->job.ip_addr));
    app->job.remote = nc;
    app->pkg_stat.type = BS_PKG_TYPE_ETH_ECU;
    app->job.internal_stat = ETH_STAT_IDLE;
    //app->job.internal_id = gen_internal_id();

DONE:
    return (app);
}

static struct bs_device_app* bs_core_ecu_offline(struct mg_connection* nc, const char* ecu_ip)
{
    struct bs_device_app* app = NULL;


    fprintf(stdout, "INFO,ECU_OFFLINE,%s\n", ecu_ip);

    app = find_app_by_nc(nc, ecu_ip);
    if (!app) {
        fprintf(stderr,
            "ERROR,ECU_OFFLINE,%s,no app find\n", ecu_ip);
        goto DONE;
    }
    else
    {
        fprintf(stdout,
            "INFO,ECU_OFFLINE,%s,%s\n", ecu_ip, app->dev_id);
    }

    //update lasted connection    
    app->job.remote = NULL;

DONE:
    return (app);
}


static int uti_cstr_to_jstr(const char* cstr, char* jbuf, int buf_size)
{
    int rc = 0;
    int j_i = 0, c_i = 0;
    int c_m = (int)(strlen(cstr));
    int j_m = (int)(buf_size - 1);

    for (; c_i < c_m;) {
        if (cstr[c_i] == '\\' && c_i + 1 < c_m && cstr[c_i + 1] == 'n') {
            c_i += 2;
            continue;
        }
        if (cstr[c_i] == '\\' && c_i + 1 < c_m && cstr[c_i + 1] == '"') {
            c_i += 1;
            continue;
        }

        jbuf[j_i] = cstr[c_i];
        j_i += 1;
        c_i += 1;


        if (c_i + 1 > c_m)
            break;

        if (j_i + 1 > j_m) {
            if (c_i < c_m)
                rc = -1;

            break;
        }
    }
    jbuf[j_i] = '\0';

    return (rc);
}

static int eth_fsm_run_online(struct bs_device_app* app)
{
    const char* REQ = "{ \"node\":109,\"task\":\"REQUEST_VERSIONS\",\"category\":0,\"payload\":\"{}\", \"uuid\" : \"803953b3-9beb-11ea-aafa-24418ccef951\" }";
    int rc = 0;


    assert(app);
    if (!app->job.remote) {
        fprintf(stderr,
            "ERROR,ETH_ONLINE,%s,app->remote == NULL\n",
            app->job.ip_addr);

        rc = -1;
        goto DONE;
    }

    mg_send(app->job.remote, REQ, strlen(REQ));
    fprintf(stdout,
        "INFO,ETH_RUN_ONLINE,%s,->,%s\n",
        app->job.ip_addr, REQ);
DONE:

    return (rc);
}

static int eth_fsm_run_req_ver_result(struct bs_device_app* app, 
    struct cJSON* payload)
{
    int rc = 0;

    struct cJSON* ver = NULL;
    struct cJSON* vero = NULL;
    struct cJSON* vers = NULL;

    vers = cJSON_GetObjectItem(payload, "versions");
    if (vers == NULL) {
        fprintf(stderr,
            "ERROR,RUN_VER_RLT,[/versions] not find\n");

        rc = -1;
        goto DONE;
    }

    if (cJSON_GetArraySize(vers) > BS_MAX_VER_NUM) {

        fprintf(stderr,
            "ERROR,RUN_VER_RLT,ver list size(%d) > core limit(%d)\n",
            cJSON_GetArraySize(vers), BS_MAX_VER_NUM);

        rc = -1;
        goto DONE;
    }

    //TODO:require json to change?
    //there is no way to get dev_id
    if (app->dev_id[0] == 0) {
        strcpy(app->dev_id, "VDCM");
    }


    int v = 0;
    memset(app->dev_vers, 0, sizeof(app->dev_vers));
    cJSON_ArrayForEach(vero, vers) {

        ver = vero->child;
        if (!cJSON_IsString(ver)) {
            fprintf(stderr,
                "ERROR,RUN_VER_RLT,versions[%d] is not string object\n", v);
            rc = -1;
            goto DONE;
        }

        SAFE_CPY_STR(app->dev_vers[v].soft_id, ver->string, BS_MAX_SOFT_ID_LEN);
        SAFE_CPY_STR(app->dev_vers[v].soft_ver, ver->valuestring, BS_MAX_SOFT_VER_LEN);

        ++v;
    }

DONE:

    return (rc);
}

static const char* eth_job_stat_name(int stat)
{
    static const char* eth_job_stat_names[] = {
        "IDLE",
        "ONLINE",
        "REQ_VER",
        "REQ_VER_RESULT",
    };
    static char eth_job_stat_unk[16];

    if (stat < 0 || stat > ETH_STAT_REQ_VER_RESULT) {

        snprintf(eth_job_stat_unk, sizeof(eth_job_stat_unk), "UNK(%d)", stat);
        return (eth_job_stat_unk);
    }

    return (eth_job_stat_names[stat]);
}

static void eth_fsm_drv_stat(struct bs_device_app* app, 
    int new_stat, struct cJSON* payload)
{
    assert(app);
    assert(new_stat == ETH_STAT_ONLINE ? true : NULL!=payload);

    fprintf(stdout,
        "INFO,DRV_STAT,%s,%s,%s,->,%s\n",
        app->job.ip_addr, app->dev_id, 
        eth_job_stat_name(app->job.internal_stat), eth_job_stat_name(new_stat));

    switch (new_stat)
    {
    case ETH_STAT_ONLINE: {
        if (0 == eth_fsm_run_online(app)) {
            app->job.internal_stat = ETH_STAT_REQ_VER;
        }
    } break;
    case ETH_STAT_REQ_VER_RESULT: {
        if (0 == eth_fsm_run_req_ver_result(app, payload)) {
            app->job.internal_stat = ETH_STAT_REQ_VER_RESULT;
            orch_report_inventory();
        }
    } break;
    default:
        break;
    }
}



static void eth_fsm_hdl_resp(struct bs_device_app* app, const char* msg)
{
    struct cJSON* root = NULL;
    struct cJSON* task = NULL;
    struct cJSON* payload = NULL;

    char jstr[2048] = { 0 };


    fprintf(stdout, "INFO,ETH_RSP,%s\n", msg);    

    root = cJSON_Parse(msg);
    if (!root) {
        fprintf(stderr,
            "ERROR,ETH_RSP,rsp parse fail: %s\n",
            msg);

        goto DONE;
    }

    payload = cJSON_GetObjectItem(root, "payload");
    if (!payload) {
        fprintf(stderr,
            "ERROR,ETH_RSP,</payload> not find: %s\n",
            msg);
        payload = NULL;
        goto DONE;
    }

    if (uti_cstr_to_jstr(payload->valuestring, jstr, sizeof(jstr))) {
        fprintf(stderr,
            "ERROR,ETH_RSP,payload msg too big =%d\n",
            (int)strlen(payload->valuestring));

        payload = NULL;
        goto DONE;
    }

    payload = cJSON_Parse(jstr);
    if (!payload) {
        fprintf(stderr,
            "ERROR,ETH_RSP,payload parse fail: %s\n",
            jstr);

        goto DONE;
    }
    

    task = cJSON_GetObjectItem(root, "task");
    if (!task) {
        fprintf(stderr,
            "ERROR,ETH_RSP,</task> not find: %s\n",
            jstr);

        goto DONE;
    }
    

    if (strcmp(task->valuestring, "REQUEST_VERSIONS_RESULT") == 0) {
        eth_fsm_drv_stat(app, ETH_STAT_REQ_VER_RESULT, payload);
        goto DONE;
    }

DONE:
    if (root)
        cJSON_Delete(root);
    if (payload)
        cJSON_Delete(payload);
}


static void eth_msg_handler(struct mg_connection* nc, int ev, void* ev_data) {

    (void)(nc);
    (void)(ev);
    (void)(ev_data);

    struct mbuf* io = &nc->recv_mbuf;
    struct bs_device_app* app = NULL;
    char ecu_ip[32] = { 0 };

    //use ecu ip to identify unique device
    if (nc) {
        if (mg_sock_addr_to_str(&(nc->sa), ecu_ip, sizeof(ecu_ip),
            MG_SOCK_STRINGIFY_IP) <= 0) {

            fprintf(stderr, "ERROR,ETH_HDL,mg_sock_addr_to_str fail\n");
            return;
        }
    }
    

    switch (ev) {
    case MG_EV_ACCEPT: {

        app = bs_core_ecu_online(nc, ecu_ip);
        if (app) {
            eth_fsm_drv_stat(app, ETH_STAT_ONLINE, NULL);
        }
    }break;


    case MG_EV_RECV: {

        app = find_app_by_nc(nc, ecu_ip);
        if (app) {
            // first 4 bytes for length
            eth_fsm_hdl_resp(app, &(io->buf[4]));
        }
        else
        {
            fprintf(stderr,
                "ERROR,ETH_RSP,no app find for %s\n",
                ecu_ip);
        }

        mbuf_remove(io, io->len);
    } break;

    case MG_EV_CLOSE:
        bs_core_ecu_offline(nc, ecu_ip);
        break;

    default:
        break;
    }
}

//------------------------------------------------------------------
// HTTP Thread
//------------------------------------------------------------------

int main(int argc, char *argv[]) 
{
    struct mg_mgr mgr;
    struct mg_connection* nc;
    struct mg_bind_opts bind_opts;
    const char* err_str;
#if MG_ENABLE_SSL
    const char* ssl_cert = NULL;
#endif
    (void)argc;
    (void)argv;


    if (argc >= 2 && strcmp(argv[1], "-v") == 0) {
        printf("====== Orchestrator =========\n");
        printf("     Version: 1.0.0.0\n");
        printf("=============================\n");
        return 0;
    }

    bs_core_init_ctx();
    mg_start_thread(bs_core_thread, NULL);
    //mg_start_thread(bs_eth_installer_job_thread, bs_get_core_ctx());
    //mg_start_thread(bs_eth_installer_job_msg_thread, bs_get_core_ctx());
    mg_mgr_init(&mgr, NULL);

    /* Process command line options to customize HTTP server */
#if MG_ENABLE_SSL
    if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
        ssl_cert = argv[++i];
    }
#endif

    /* Set HTTP server options */
    memset(&bind_opts, 0, sizeof(bind_opts));
    bind_opts.error_string = &err_str;
#if MG_ENABLE_SSL
    if (ssl_cert != NULL) {
        bind_opts.ssl_cert = ssl_cert;
    }
#endif
    fprintf(stdout, "=== Start http&socket server ===\n\n");

    nc = mg_bind_opt(&mgr, s_http_port, dlc_msg_handler, bind_opts);
    if (nc == NULL) {
        fprintf(stderr, "Starting server on port %s for DLC fail(%s)\n", s_http_port,
            *bind_opts.error_string);
        exit(1);
    }
    else {
        fprintf(stdout, "Starting server on port %s for DLC\n", s_http_port);
    }
    // Register endpoints
      //mg_register_http_endpoint(nc, "/upload", handle_upload MG_UD_ARG(NULL));
    mg_register_http_endpoint(nc, "/test/live", handle_test_live MG_UD_ARG(NULL));
    mg_register_http_endpoint(nc, "/test/vers", handle_test_vers MG_UD_ARG(NULL));
    mg_register_http_endpoint(nc, "/pkg/new", handle_pkg_new MG_UD_ARG(NULL));
    mg_register_http_endpoint(nc, "/pkg/stat", handle_pkg_stat MG_UD_ARG(NULL));
    mg_register_http_endpoint(nc, "/pkg/inst", handle_pkg_inst MG_UD_ARG(NULL));
    mg_register_http_endpoint(nc, "/tdr/run", handle_tdr_run MG_UD_ARG(NULL));
    mg_register_http_endpoint(nc, "/tdr/stat", handle_tdr_stat MG_UD_ARG(NULL));
    // Set up HTTP server parameters
    mg_set_protocol_http_websocket(nc);

    nc = mg_bind_opt(&mgr, bs_get_core_ctx()->eth_installer_port,
        eth_msg_handler, bind_opts);
    if (nc == NULL) {
        fprintf(stderr, 
            "Starting server on port %s for ETH fail(%s)\n",
            bs_get_core_ctx()->eth_installer_port,
            *bind_opts.error_string);
        exit(1);
    }
    else {
        fprintf(stdout,
            "Starting server on port %s for ETH\n",
            bs_get_core_ctx()->eth_installer_port);
    }


    for (;;) {
        mg_mgr_poll(&mgr, 1000);
    }

    bs_core_exit_ctx();
    mg_mgr_free(&mgr);

    return 0;
}
