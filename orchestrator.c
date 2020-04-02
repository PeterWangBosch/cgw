/*
 * Copyright (c) 2014 Cesanta Software Limited
 * All rights reserved
 */

#include "stdio.h"

#include "cJSON/cJSON.h"
#include "mongoose/mongoose.h"
#include "src/bs_core.h"
#include "src/file_utils.h"

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

static char s_tdr_stat[512] = { 0 };



//------------------------------------------------------------------
// Handle core data
//------------------------------------------------------------------

int core_api_tdr_run(struct bs_device_app *app, struct cJSON *payload)
{
  int result = 1;

  switch(app->pkg_stat.type) {
    case BS_PKG_TYPE_CAN_ECU:
    case BS_PKG_TYPE_ORCH:
      // local download & cache
      app->pkg_stat.stat = bs_pkg_stat_loading;
      break;
    case BS_PKG_TYPE_ETH_ECU:
      //TODO: start remote installer job
      (void) payload;
      break;
    case BS_PKG_TYPE_INVALID:
    default:
      result = 0;
      break;
  }  

  return result;
}

//------------------------------------------------------------------
// HTTP Message handler
//------------------------------------------------------------------

// for live testing
static void handle_test_live(struct mg_connection *nc, int ev, void *p) {
  (void) ev;
  (void) p;

  float result = 0.0000001;
  mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
  mg_printf_http_chunk(nc, "{ \"result\": %lf }", result);
  mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}

static void handle_pkg_new(struct mg_connection *nc, int ev, void *p) {
  struct http_message *hm = (struct http_message *) p;
  const char *result = api_resp_err_succ;
  const char *proto = api_resp_pkg_proto_http;

  int parse_stat = JSON_INIT;
  //static char response[1024];
  struct cJSON * root = NULL;
  struct cJSON * iterator = NULL;
  struct cJSON * payload = NULL;
  char * dev_id = NULL;

  struct bs_core_request core_req;

  (void) ev;
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
    if (strcmp(iterator->string, "deviceId") == 0) {
      parse_stat = JSON_HAS_DEVID;
      dev_id = iterator->valuestring;
    }
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
  core_req.conn_id = (unsigned long) nc->user_data;
  core_req.cmd = BS_CORE_REQ_PKG_NEW;
  if (write(bs_get_core_ctx()->core_msg_sock[0], &core_req, sizeof(core_req)) < 0) {
    printf("Writing core sock error!");
    result = api_resp_err_fail;
    goto last_step; 
  }

last_step:
  // TODO: configuable to tftp, https
  mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
  mg_printf_http_chunk(nc, "{ \"protocol\": %s, \"error\": %s }", proto, result);
  mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
  // release memory
  cJSON_Delete(root);
}


static void handle_pkg_stat(struct mg_connection *nc, int ev, void *p) {
  (void) ev;
  (void) p;

  mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
  mg_printf_http_chunk(nc, "{ \"result\": %s }", "succ");//g_ctx.pkg_stat);
  mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}

static void handle_tdr_stat(struct mg_connection *nc, int ev, void *p) {
  (void) ev;
  (void) p;
  strcpy(s_tdr_stat, "succ"); // just for debug

  mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
  mg_printf_http_chunk(nc, "{ \"result\": \"%s\"}", s_tdr_stat);
  mg_send_http_chunk(nc, "", 0);
  return;
}

static void handle_tdr_run(struct mg_connection *nc, int ev, void *p) {
  FILE *fp;
  char cmd[256] = {0};
  char output[300];
 
  (void) ev;
  (void) p;

  strcpy(cmd, "ls -la ");
  if ((fp = popen(cmd, "r")) != NULL) {
      mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
      mg_printf_http_chunk(nc, "{ \"result\": \"succ\"}");
      mg_send_http_chunk(nc, "", 0);
  } else {
      mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
      mg_printf_http_chunk(nc, "{ \"result\": \"failed\"}");
      mg_send_http_chunk(nc, "", 0);
      return;
  }

  while (fgets(output, sizeof(output)-1, fp) != NULL) {
  }

  // TODO: check s_tdr_stat to decide result
  mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
  mg_printf_http_chunk(nc, "{ \"result\": \"succ\"}");
  mg_send_http_chunk(nc, "", 0);

  printf("CGW Orchstrator: tdr run success!\n");

  pclose(fp);
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
        data->fp = on_write("wpc.1.0.0");//TODO: read file name from context
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

static void ev_handler(struct mg_connection *nc, int ev, void *ev_data) {
  if (ev == MG_EV_HTTP_REQUEST) {
    mg_serve_http(nc, ev_data, s_http_server_opts);
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

  bs_core_init_ctx();

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
  nc = mg_bind_opt(&mgr, s_http_port, ev_handler, bind_opts);
  if (nc == NULL) {
    fprintf(stderr, "Error starting server on port %s: %s\n", s_http_port,
            *bind_opts.error_string);
    exit(1);
  }

  nc->user_data = (void *) bs_get_next_conn_id();

  // Register endpoints
  mg_register_http_endpoint(nc, "/upload", handle_upload MG_UD_ARG(NULL));
  mg_register_http_endpoint(nc, "/test/live", handle_test_live MG_UD_ARG(NULL));
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
