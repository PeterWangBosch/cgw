/*
 * Copyright (c) 2014 Cesanta Software Limited
 * All rights reserved
 */

#include "stdio.h"
#include "src/mongoose.h"

static const char *s_http_port = "8018";
static struct mg_serve_http_opts s_http_server_opts;

static char s_tdr_stat[512] = { 0 };
static unsigned int i_tdr_stat_len = 0;

struct file_writer_data {
  FILE *fp;
  size_t bytes_written;
};

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

  char * stat = "downloading";

  mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
  mg_printf_http_chunk(nc, "{ \"result\": %s }", stat);
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

static void handle_upload(struct mg_connection *nc, int ev, void *p) {
  struct file_writer_data *data = (struct file_writer_data *) nc->user_data;
  struct mg_http_multipart_part *mp = (struct mg_http_multipart_part *) p;

  switch (ev) {
    case MG_EV_HTTP_PART_BEGIN: {
      if (data == NULL) {
        data = calloc(1, sizeof(struct file_writer_data));
        data->fp = tmpfile();
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
