/*
 * Copyright (c) 2014 Cesanta Software Limited
 * All rights reserved
 */

#include "stdio.h"
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

/**
 * Upgrading status of single ECU
**/
struct bs_ecu_upgrade_stat {
  char esti_time[64];
  char start_time[32];
  char time_stamp[32];
  // "yes" or "no"
  char door_module[8];
  // "pending", "in progress", "failed", "success"
  char status[16];
  // raw percentage data, e.g., 0, 55, or 100
  float progress_percent;
};

struct bs_context {
  char dev_id[32];
  char soft_id[32];
  unsigned int door_module;
  char* pkg_stat; // "no start", "downloading", "succ", "fail"
  struct bs_ecu_upgrade_stat upgrade_stat;
};

//------------------------------------------------------------------
// Global
//------------------------------------------------------------------
static const char *s_http_port = "8018";
static struct mg_serve_http_opts s_http_server_opts;

static char s_tdr_stat[512] = { 0 };
static unsigned int i_tdr_stat_len = 0;

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
        printf("Preparing to start writing file '%s'\n", filename);
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
  (void) ev;
  (void) p;

  // TODO: configuable to tftp, https
  char * proto = "http";

  mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
  mg_printf_http_chunk(nc, "{ \"protocol\": %s }", proto);
  mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}


static void handle_pkg_stat(struct mg_connection *nc, int ev, void *p) {
  (void) ev;
  (void) p;

  mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
  mg_printf_http_chunk(nc, "{ \"result\": %s }", g_ctx.pkg_stat);
  mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}

static void handle_tdr_stat(struct mg_connection *nc, int ev, void *p) {
  (void) ev;
  (void) p;

  mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
  mg_printf_http_chunk(nc, "{ \"TDR status\": \"%s\"}", s_tdr_stat);
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
      mg_printf_http_chunk(nc, "{ \"TDR status\": \"Going to run\"}");
      mg_send_http_chunk(nc, "", 0);
  } else {
      mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
      mg_printf_http_chunk(nc, "{ \"TDR status\": \"Failed to run\"}");
      mg_send_http_chunk(nc, "", 0);
      return;
  }

  while (fgets(output, sizeof(output)-1, fp) != NULL){
      if ((i_tdr_stat_len + sizeof(output)) >= 512) {
	sprintf(s_tdr_stat, "%s", output);
	i_tdr_stat_len = strlen(s_tdr_stat);
      } else {
	sprintf(s_tdr_stat, "%s\n %s", s_tdr_stat, output);
	i_tdr_stat_len = strlen(s_tdr_stat);
      }
  }

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
          nc->flags |= MG_F_SEND_AND_CLOSE;
          free(data);
          return;
        }
        nc->user_data = (void *) data;
        g_ctx.pkg_stat = "downloading";
      }
      break;
    }
    case MG_EV_HTTP_PART_DATA: {
      if (fwrite(mp->data.p, 1, mp->data.len, data->fp) != mp->data.len) {
        mg_printf(nc, "%s",
                  "HTTP/1.1 500 Failed to write to a file\r\n"
                  "Content-Length: 0\r\n\r\n");
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
                "Written %ld of POST data to a temp file\n\n",
                (long) ftell(data->fp));
      nc->flags |= MG_F_SEND_AND_CLOSE;
      fclose(data->fp);
      g_ctx.pkg_stat = "succ";
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
void init_context(){
  g_ctx.pkg_stat = "nostart";
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
