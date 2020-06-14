/*
 * Copyright (c) 2014 Cesanta Software Limited
 * All rights reserved
 */

#include "stdio.h"
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

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


static void handle_pkg_stat(struct mg_connection *nc, int ev, void *p) {
  (void) ev;
  (void) p;

  mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
  mg_printf_http_chunk(nc, "{ \"stat\": %s }", "succ");//g_ctx.pkg_stat);
  mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
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
  printf("Core request TDR_RUN!\n");
  if (write(bs_get_core_ctx()->core_msg_sock[0], &core_req, sizeof(core_req)) < 0) {
    printf("Writing core sock error!\n");
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
      printf("Core request: PKG_READY\n");
      if (write(bs_get_core_ctx()->core_msg_sock[0], &core_req, sizeof(core_req)) < 0) {
        printf("Writing core sock error!");
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
    if (read(fd, &req, sizeof(req)) < 0) {
        perror("Error reading worker notify sock");
        return;
    }

    switch (req.cmd) {
    case BS_CORE_REQ_PKG_READY:
        fprintf(stdout, "Main_Thrd:Recv background cmd: BS_CORE_REQ_PKG_READY\n");
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

//------------------------------------------------------------------
// HTTP Thread
//------------------------------------------------------------------

int main(int argc, char *argv[]) {
  struct mg_mgr mgr;
  struct mg_connection *nc;
  struct mg_bind_opts bind_opts;
  const char *err_str;
#if MG_ENABLE_SSL
  const char *ssl_cert = NULL;
#endif
  (void) argc;
  (void) argv;


  if (argc >= 2 && strcmp(argv[1], "-v") == 0) {
    printf("====== Orchestrator =========\n");
    printf("     Version: 1.0.0.0\n");
    printf("=============================\n");
    return 0;
  }

  bs_core_init_ctx();
  mg_start_thread(bs_core_thread, NULL);
  mg_start_thread(bs_eth_installer_job_thread, bs_get_core_ctx());
  mg_start_thread(bs_eth_installer_job_msg_thread, bs_get_core_ctx());
  mg_mgr_init(&mgr, NULL);

  // in case we need listen to socket
  //if (argc == 2 && strcmp(argv[1], "-o") == 0) {
  //  printf("Start socket server\n");
  //  mg_bind(&mgr, "8018", socket_ev_handler);
  //  printf("Listen on port 8018\n");
  //  for (;;) {
  //    mg_mgr_poll(&mgr, 1000);
  //  }
  //  mg_mgr_free(&mgr);
  //  return 1;
  //}

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
  mg_register_http_endpoint(nc, "/tdr/run", handle_tdr_run MG_UD_ARG(NULL));
  mg_register_http_endpoint(nc, "/tdr/stat", handle_tdr_stat MG_UD_ARG(NULL));
  // Set up HTTP server parameters
  mg_set_protocol_http_websocket(nc);

  for (;;) {
    mg_mgr_poll(&mgr, 1000);
  }

  bs_core_exit_ctx();
  mg_mgr_free(&mgr);

  return 0;
}
