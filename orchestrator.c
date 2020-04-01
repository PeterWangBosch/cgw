/*
 * Copyright (c) 2014 Cesanta Software Limited
 * All rights reserved
 */

#include "stdio.h"
#include "cJSON/cJSON.h"
#include "src/mongoose.h"

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
const char *bs_pkg_stat_idle = "idle";
const char *bs_pkg_stat_loading = "loading";
const char *bs_pkg_stat_succ = "succ";
const char *bs_pkg_stat_fail = "fail";
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
  struct mg_connection * tlc;
  struct bs_device_app *loading_app;// TODO: support downloading in paralell
  struct bs_device_app apps[BS_MAX_DEVICE_APP_NUM];
};

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

static struct bs_context g_ctx;

//------------------------------------------------------------------
// File utilities
//------------------------------------------------------------------
struct file_writer_data {
  FILE *fp;
  size_t bytes_written;
};

static FILE * on_read(const char *filename)
{
    FILE *fp;

    if ((fp = fopen(filename, "r")) == NULL)
    {
        printf("Could not open file '%s'\n", filename);
        return NULL;
    } else {
        printf("Preparing to start reading file '%s'\n", filename);
        return fp;
    }
}

static FILE * on_write(const char *filename)
{
    FILE *fp;

    if ((fp = fopen(filename, "w")) == NULL)
    {
        printf("Could not open destination file '%s'\n", filename);
        return NULL;
    } else {
        return fp;
    }
}

static int on_read_data(FILE *fp, uint8_t *buffer, int len)
{
    int read_cn = -1;
    if ((read_cn = fread(buffer, 1, len, fp)) > 0) {
        return read_cn;
    }
    return -1;
}

static int on_write_data(FILE *fp, uint8_t *buffer, int len)
{
    fwrite(buffer, 1, len, fp);
    return 1;
}

//------------------------------------------------------------------
// Handle core data
//------------------------------------------------------------------
struct bs_device_app * core_find_app(struct bs_context *p_ctx, const char *id)
{
  struct bs_device_app * result = NULL;
  int i;

  for (i=0; i<BS_MAX_DEVICE_APP_NUM; i++) {
    if (strcmp(p_ctx->apps[i].dev_id, id) == 0) {
      result = &(p_ctx->apps[i]);
      return result;
    }
  }
  return result;
}

int core_api_new_pkg(struct bs_device_app *app, struct cJSON *payload)
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
  struct bs_device_app *app; 

  int parse_stat = JSON_INIT;
  //static char response[1024];
  struct cJSON * root = NULL;
  struct cJSON * iterator = NULL;
  struct cJSON * payload = NULL;
  char * dev_id = NULL;

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
  app = core_find_app(&g_ctx, dev_id);
  if (core_api_new_pkg(app, payload)) {
    g_ctx.loading_app = app;
  } else {
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

      g_ctx.loading_app->pkg_stat.stat = bs_pkg_stat_succ;
      g_ctx.loading_app = NULL;

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
void init_device_app(struct bs_device_app *app) {
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

void init_context() {
  int i;

  g_ctx.tlc = NULL;
  for(i=0; i<BS_MAX_DEVICE_APP_NUM; i++) {
    init_device_app(g_ctx.apps + i);
  }
}

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
  mg_mgr_free(&mgr);

  return 0;
}
