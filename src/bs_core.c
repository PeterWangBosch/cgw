#include "bs_core.h"

#include "src/bs_cgw_utils.h"
#include "bs_eth_installer_job.h"
#include "bs_tdr_job.h"
#include "file_utils.h"


const char *bs_pkg_stat_idle = "idle";
const char *bs_pkg_stat_new = "new";
const char *bs_pkg_stat_loading = "loading";
const char *bs_pkg_stat_succ = "succ";
const char *bs_pkg_stat_fail = "fail";

static sig_atomic_t g_received_signal = 0;
static void signal_handler(int sig_num) 
{
  signal(sig_num, signal_handler);
  g_received_signal = sig_num;
}

static struct bs_context g_ctx;
struct bs_context * bs_get_core_ctx()
{
  return &g_ctx;
}

unsigned long bs_get_next_conn_id()
{
  return ++g_ctx.next_conn_id;
}

char * bs_get_safe_str_buf()
{
  static char buf[1024];
  return buf;
}

void bs_init_core_request(struct bs_core_request* req)
{
  int i = 0;

  req->conn_id = 0;
  req->cmd = BS_CORE_REQ_INVALID;
  for(i=0; i<32; i++) {
    req->dev_id[i] = 0;
  }
}

void bs_init_app_upgrade_stat(struct bs_app_upgrade_stat * p_stat)
{
  int i = 0;

  strcpy(p_stat->start_time, "00:00:00 1900-01-01");
  strcpy(p_stat->time_stamp, "00:00:00 1900-01-01");
  for (i=0; i<16; i++) {
    p_stat->status[i] = 0;
  }
  p_stat->progress_percent = 0;
}

void bs_init_device_app(struct bs_device_app *app)
{
  int i;

  for (i=0; i<32; i++) {
    app->dev_id[0] = 0;
  }
  app->door_module = false;

  app->pkg_stat.type = BS_PKG_TYPE_INVALID;
  app->pkg_stat.stat = bs_pkg_stat_succ;

  bs_init_app_upgrade_stat(&(app->upgrade_stat));

  memset(app->job.ip_addr, 0, 32);
  app->job.internal_id = 0;
  app->job.internal_stat = 0;
  app->job.remote = NULL;
  app->slot_used = false;
}
void bs_init_app_config(const char * filename)
{
  FILE * fp = on_read(filename);
  char *line = (char *)malloc(1024);
  int str_len = 0;
  int cur_app_index = 0;
  int offset = 1;
  char key[32] = { 0 };
  char val[32] = { 0 };

  if (!fp) {
    printf("failed to open config.ini");
    return;
  }

  while(!feof(fp)) {
    // TODO (tianjiao) : check and wirte to g_ctx.apps[cur_app_index]
    memset(line, 0, 1024);
    memset(key, 0, 32);
    memset(val, 0, 32);
    fgets(line, 100, fp);
    str_len = strlen(line);
    char * pos_app = strstr(line, "#");
    if (pos_app)
    {
        cur_app_index++;
    }
    char * pos = strstr(line, ":");
    if (pos == NULL)
    {
        continue;
    }
    if (line[str_len - 1] == '\n')
    {
        offset = 2;
    }
    strncpy(key, line, pos - line);
    strncpy(val, pos + 1, str_len - (pos - line) - offset);
    if (strcmp(key ,"dev_id") == 0)
    {
       strcpy((char *)g_ctx.apps[cur_app_index-1].dev_id, val);
    }
    if (strcmp(key ,"door_module") == 0)
    {
       g_ctx.apps[cur_app_index-1].door_module = (bool)atoi(val);
    }
    if (strcmp(key ,"pkg_stat_type") == 0)
    {
       g_ctx.apps[cur_app_index-1].pkg_stat.type = (uint8_t)atoi(val);
    }
    if (strcmp(key ,"pkg_stat_stat") == 0)
    {
       g_ctx.apps[cur_app_index-1].pkg_stat.stat = val;
    }
    if (strcmp(key ,"pkg_name") == 0)
    {
      strcpy(g_ctx.apps[cur_app_index-1].pkg_stat.name, val);
    }
    if (strcmp(key ,"upgrade_stat") == 0)
    {
       strcpy((char *)g_ctx.apps[cur_app_index-1].upgrade_stat.status, val);
    }
    if (strcmp(key ,"intaller_job_ip_addr") == 0)
    {
       strcpy((char *)g_ctx.apps[cur_app_index-1].job.ip_addr, val);
    }
    if (strcmp(key ,"esti_time") == 0)
    {
      strcpy((char *)g_ctx.apps[cur_app_index-1].upgrade_stat.esti_time , val);
    }
    if (strcmp(key ,"start_time") == 0)
    {
      strcpy((char *)g_ctx.apps[cur_app_index-1].upgrade_stat.start_time , val);
    }
    if (strcmp(key ,"time_stamp") == 0)
    {
      strcpy((char *)g_ctx.apps[cur_app_index-1].upgrade_stat.time_stamp , val);
    }

    if(strcmp(key ,"end") == 0)
    {
        break;
    }
  }
  free(line);
  line = NULL;
  fclose(fp);
  fp = NULL;
}
void bs_core_init_ctx()
{
  int i;

 // char *filename="/vendor/etc/config.ini";
  char *filename="/data/etc/orchestrator/config.ini";
  g_ctx.tlc = NULL;
  memset(g_ctx.tlc_ip, 0, 32);
  strcpy(g_ctx.tlc_ip, "127.0.0.1");

  g_ctx.next_conn_id = 0;

  if (mg_socketpair(g_ctx.core_msg_sock, SOCK_STREAM) == 0) {
    perror("Opening socket pair error");
    exit(1);
  }

  if (mg_socketpair(g_ctx.eth_installer_msg_sock, SOCK_STREAM) == 0) {
    perror("Opening socket pair error");
    exit(1);
  }
//  signal(SIGTERM, signal_handler);
//  signal(SIGINT, signal_handler);

  strcpy(g_ctx.eth_installer_port, "1025");

  for(i=0; i<BS_MAX_DEVICE_APP_NUM; i++) {
    bs_init_device_app(&g_ctx.apps[i]);
  }

  //init from config.ini
  bs_load_app_config(filename, g_ctx.apps, BS_MAX_DEVICE_APP_NUM);

  g_ctx.cgw_stat = CGW_STAT_IDLE;
}

void bs_core_exit_ctx()
{
  closesocket(g_ctx.core_msg_sock[0]);
  closesocket(g_ctx.eth_installer_msg_sock[1]);
}

unsigned int bs_print_json_upgrade_stat(struct bs_device_app *app, char* msg)
{
  unsigned int pc = 0; 
  char buf[128];
 
  // start {
  msg[pc] = '{';
  pc += 1;

  // dev_id
  sprintf(buf, "\"dev_id\":\"%s\",", app->dev_id);
  strcpy(msg + pc, buf);
  pc += strlen(buf);


  // esti_time
  sprintf(buf, "\"esti_time\":\"%s\",", app->upgrade_stat.esti_time);
  strcpy(msg + pc, buf);
  pc += strlen(buf);

  // start_time
  sprintf(buf, "\"start_time\":\"%s\",", app->upgrade_stat.start_time);
  strcpy(msg + pc, buf);
  pc += strlen(buf);

  // time_stamp
  sprintf(buf, "\"time_stamp\":\"%s\",", app->upgrade_stat.time_stamp);
  strcpy(msg + pc, buf);
  pc += strlen(buf);

  // door_module
  if (app->door_module) {
    sprintf(buf, "\"door_module\":\"yes\",");
  } else {
    sprintf(buf, "\"door_module\":\"no\",");
  }
  strcpy(msg + pc, buf);
  pc += strlen(buf);

  // status
  sprintf(buf, "\"status\":\"%s\",", app->upgrade_stat.status);
  strcpy(msg + pc, buf);
  pc += strlen(buf);

  // progress
  sprintf(buf, "\"progress_percent\":%d", (int)(app->upgrade_stat.progress_percent));
  strcpy(msg + pc, buf);
  pc += strlen(buf);

  // end '}' for pkg status
  msg[pc] = '}';
  pc += 1;

  // debug
  printf("/tdr/stat: %s\n", msg);

  return pc;  
}

int bs_cgw_get_stat()
{
    return (__sync_fetch_and_or(&g_ctx.cgw_stat, 0));
}

 
int bs_cgw_set_stat(int new_stat)
{    
    int prev_stat;

    while (true) {
        prev_stat = bs_cgw_get_stat();
        if (prev_stat == new_stat)
            break;

        if (__sync_bool_compare_and_swap(&g_ctx.cgw_stat, prev_stat, new_stat))
            break;
    }

    return (prev_stat);
}

struct bs_device_app * bs_core_find_app(const char *id)
{
  struct bs_device_app * result = NULL;
  int i;

  for (i=0; i<BS_MAX_DEVICE_APP_NUM; i++) {
    if (strcmp(g_ctx.apps[i].dev_id, id) == 0) {
      result = &(g_ctx.apps[i]);
      return result;
    }
  }
  return result;
}

// '0' is invalid internal ID
static unsigned int gen_internal_id() {
  static unsigned int i;
  if (++i == 0)
    return i = 1;

  return i;
}

struct bs_device_app* bs_core_eth_installer_up(struct mg_connection * nc)
{
    struct bs_device_app* app = NULL;
    char ecu_ip[32] = { 0 };

    //use ecu ip to identify unique device
    if (mg_sock_addr_to_str(&(nc->sa), ecu_ip, sizeof(ecu_ip),
        MG_SOCK_STRINGIFY_IP) <= 0) {

        fprintf(stderr, "ERROR,ECU_ONLINE,mg_sock_addr_to_str fail\n");
        goto DONE;
    }
    
    fprintf(stdout, "INFO,ECU_ONLINE,%s\n", ecu_ip);

    app = find_app_by_nc(nc, ecu_ip);

    //first time online
    //find a free app slot to store
    if (!app) {
        for (int i = 0; i < BS_MAX_DEVICE_APP_NUM; i++) {
            if (!g_ctx.apps[i].slot_used) {
                app = &(g_ctx.apps[i]);
                app->slot_used = true;
            }
        }

        if (!app) {
            fprintf(stderr, "ERROR,ECU_ONLINE,no free app slot\n");
            goto DONE;
        }
    }

    //update lasted connection    
    memcpy(app->job.ip_addr, ecu_ip, sizeof(ecu_ip));
    app->job.remote = nc;
    app->job.internal_stat = ETH_STAT_IDLE;
    app->job.internal_id = gen_internal_id();

DONE:
      return (app);
}

struct bs_device_app * find_app_by_nc(struct mg_connection* nc, const char* ecu_ip)
{
    struct bs_device_app* app = NULL;
    char dev_ip[32] = { 0 };

    if (!ecu_ip) {
        if (mg_sock_addr_to_str(&(nc->sa), dev_ip, sizeof(dev_ip),
            MG_SOCK_STRINGIFY_IP) <= 0) {

            fprintf(stderr, "ERROR,ECU_EVENT,mg_sock_addr_to_str fail\n");
            goto DONE;
        }

        ecu_ip = dev_ip;
    }
    

    for (int i = 0; i < BS_MAX_DEVICE_APP_NUM; i++) {
        if (!g_ctx.apps[i].slot_used) {
            continue;
        }

        if (strcmp(g_ctx.apps[i].job.ip_addr, ecu_ip) == 0) {
            app = &(g_ctx.apps[i]);
            break;
        }
    }

DONE:
    return (app);
}

struct bs_device_app * bs_core_eth_installer_down(struct mg_connection * nc)
{
  struct bs_device_app * result = NULL;
  char ip[32];
  int i;

  for (i=0; i<BS_MAX_DEVICE_APP_NUM; i++) {
    // invalid device
    if (g_ctx.apps[i].dev_id[0] == 0) {
      continue;
    }

    // use {ip:port} as key to recognize installer
    if (mg_sock_addr_to_str(&(nc->sa), 
                            ip, 32,
                            MG_SOCK_STRINGIFY_IP |
                            MG_SOCK_STRINGIFY_PORT) <= 0) {
      return NULL;
    }

    // TODO: make sure we have config file to obtain installer's ip 
    if (strcmp(g_ctx.apps[i].job.ip_addr, ip) == 0) {
      g_ctx.apps[i].job.internal_id = 0;
    } 
  }
  return result;
}

int bs_core_req_tdr_run(struct bs_core_request* req)
{
  struct bs_eth_installer_core_request inst_req;
  struct bs_device_app *app = bs_core_find_app(req->dev_id);

  if (app == NULL) {
    return 0;
  }

  switch(app->pkg_stat.type) {
    case BS_PKG_TYPE_CAN_ECU:
    case BS_PKG_TYPE_ORCH:
      // start tdr running job
      mg_start_thread(bs_tdr_job_thread, app);
      break;
    case BS_PKG_TYPE_ETH_ECU:
      //TODO: start remote installer job
      bs_init_eth_installer_core_request(&inst_req, app);
      inst_req.cmd = BS_ETH_INSTALLER_PKG_NEW;
      inst_req.app = app;
      strcpy(inst_req.payload.info, req->payload.info);
      if (write(bs_get_core_ctx()->eth_installer_msg_sock[0], &inst_req, sizeof(inst_req)) < 0) {
        printf("Writing eth instl sock error!\n");
        return 0;
      }

      break;
    case BS_PKG_TYPE_INVALID:
    default:
      return 0;
  }

  return 1;
}

int bs_core_req_eth_instl_prepare(struct bs_device_app * app)
{
  struct bs_eth_installer_core_request inst_req;
  bs_init_eth_installer_core_request(&inst_req, app);
  inst_req.cmd = BS_ETH_INSTALLER_PREPARE;
  inst_req.app = app;
  inst_req.payload.info[0] = 0; // empty string
  if (write(bs_get_core_ctx()->eth_installer_msg_sock[0], &inst_req, sizeof(inst_req)) < 0) {
    printf("bs_core_req_eth_instl_prepare: Writing eth instl sock error! \n");
    return 0;
  }

  return 1;
}

int bs_core_req_eth_instl_act(struct bs_device_app * app)
{
  struct bs_eth_installer_core_request inst_req;
  bs_init_eth_installer_core_request(&inst_req, app);
  inst_req.cmd = BS_ETH_INSTALLER_ACT;
  inst_req.app = app;
  inst_req.payload.info[0] = 0; // empty string
  if (write(bs_get_core_ctx()->eth_installer_msg_sock[0], &inst_req, sizeof(inst_req)) < 0) {
    printf("bs_core_req_eth_instl_prepare: Writing eth instl sock error! \n");
    return 0;
  }

  return 1;
}


int bs_core_req_tdr_stat_update(struct bs_core_request* req)
{
  struct bs_device_app *app = bs_core_find_app(req->dev_id);

  if (app == NULL) {
    return 0;
  }

  switch(app->pkg_stat.type) {
    case BS_PKG_TYPE_CAN_ECU:
    case BS_PKG_TYPE_ORCH:
      app->upgrade_stat.progress_percent = req->payload.stat.progress_percent;
      printf("Core: progress %f \n", app->upgrade_stat.progress_percent);
      break;
    case BS_PKG_TYPE_ETH_ECU:
      break;
    case BS_PKG_TYPE_INVALID:
    default:
      return 0;
  }

  return 1;
}




static const char *pick_file_name(const char *file_path) {
  int siz = (int)strlen(file_path);
  for (int i = siz - 1; i >= 0; --i) {
    if (file_path[i] == '/') {
      return (&file_path[i + 1]);
    }
  }

  return (file_path);
}

int bs_core_down_can_pkg(bs_l1_manifest_pkg_t* pkg)
{
    assert(pkg);
    int rc = 0;
    FILE* fp = NULL;
    char cmd_buf[1024] = { 0 };
    char cmd_out[1024] = { 0 };
    char ftp_url[512] = { 0 };

    const char* pkg_name = pick_file_name(pkg->pkg_url);

    snprintf(ftp_url, sizeof(ftp_url) - 1, "tftp://%s/%s", bs_get_core_ctx()->tlc_ip, pkg_name);
    snprintf(cmd_buf, sizeof(cmd_buf) - 1, "curl -o /data/var/orchestrator/%s  %s", pkg_name, ftp_url);
    fwrite(cmd_buf, 1, strlen(cmd_buf), stdout);
    fwrite("\n\n", 1, 2, stdout);

    if ((fp = popen(cmd_buf, "r")) == NULL) {
        fprintf(
            stderr,
            "ERROR,bs_core_down_can_pkg,popen failed(%d)\n", 
            errno);

        rc = -1;;
        goto DONE;
    }
    while (fgets(cmd_out, sizeof(cmd_out), fp) != NULL) {
        fwrite(cmd_out, 1, strlen(cmd_out), stdout);
        if (strstr(cmd_out, "Failed") != NULL) {
            rc = -2;
            goto DONE;
        }
    }
    fprintf(stdout, "\n\n");


DONE:
    if (fp)
        pclose(fp);

    return (rc);
}

int bs_core_req_pkg_new(struct bs_core_request* req)
{
    int rc = 0;
    assert(req);

    //down pkg for dev_type == can||can-fd
    bs_l1_manifest_t* l1_mani = req->payload.l1_mani;
    for (int i = 0; i < l1_mani->pkg_num; ++i) {
        bs_l1_manifest_pkg_t* pkg = &l1_mani->packages[i];
        if (pkg->dev_type == BS_DEV_TYPE_CAN ||
            pkg->dev_type == BS_DEV_TYPE_CAN_FD)
        {
            rc = bs_core_down_can_pkg(pkg);
            if (rc) {
                fprintf(stderr,
                    "ERROR,bs_core_req_pkg_new,down pkg fail(%s:%s)\n",
                    pkg->dev_id, pick_file_name(pkg->pkg_url));

                goto DONE;
            }
            else {
                fprintf(stdout, 
                    "INFO,bs_core_req_pkg_new,down pkg succ(%s:%s)\n",
                    pkg->dev_id, pick_file_name(pkg->pkg_url));
            }
        }//down can/can-fd pkg      
    }

DONE:
    if (rc) {

        fprintf(stdout, "Main notify BS_CORE_SVC_DOWN_FAIL\n");
        bs_cgw_set_stat(CGW_STAT_PKG_FAIL);
        //        
        struct bs_core_request core_req;
        bs_init_core_request(&core_req);
        core_req.cmd = BS_CORE_REQ_PKG_FAIL;
        //from core_stat_thread to main_thread
        if (write(g_ctx.core_msg_sock[1], &core_req,
            sizeof(core_req)) < 0) {
            fprintf(stderr, "Writing core sock error!\n");
        }
    }
    else {
        fprintf(stdout, "Main notify BS_CORE_REQ_PKG_READY\n");
        bs_cgw_set_stat(CGW_STAT_PKG_READY);
        //
        struct bs_core_request core_req;
        bs_init_core_request(&core_req);
        core_req.cmd = BS_CORE_REQ_PKG_READY;
        //from core_stat_thread to main_thread
        if (write(g_ctx.core_msg_sock[1], &core_req,
            sizeof(core_req)) < 0) {
            fprintf(stderr, "Writing core sock error!\n");
        }
    }

    return (rc);
}

int bs_core_req_pkg_ready(struct bs_core_request* req)
{
    (void)(req);
    int rc = 0;


    fprintf(stdout, "bs_core_req_pkg_ready, enter ...\n\n");
    {
        //nothing to do now
        goto DONE;
    }

DONE:
    fprintf(stdout, "bs_core_req_pkg_ready, leave =%d\n\n", rc);
    return (rc);
}

static int bs_core_inst_can_pkg(bs_l1_manifest_pkg_t* pkg)
{
    assert(pkg);

    int rc = 0;
    FILE* fp = NULL;

    char cmd[256] = { 0 };
    char out[512] = { 0 };
    char pkg_filepath[256] = { 0 };

    snprintf(pkg_filepath, sizeof(pkg_filepath), 
        "/data/var/orchestrator/%s", pick_file_name(pkg->pkg_url));

    struct bs_device_app* app = bs_core_find_app(pkg->dev_id);
    if (NULL == app) {
        fprintf(stderr,
            "ERROR,bs_core_inst_can_pkg, ecu not online(%s|%s)\n", 
            pkg->dev_id, pkg_filepath);

        rc = -1;
        goto DONE;
    }


    snprintf(cmd, sizeof(cmd),
        "sh /tdr/usr/bin/aaas_runtask.sh RDA %s", pkg_filepath);
    fwrite(cmd, 1, strlen(cmd), stdout);
    fwrite("\n\n", 1, 2, stdout);

    if ((fp = popen(cmd, "r")) == NULL) {
        app->upgrade_stat.progress_percent = 0;

        fprintf(stderr,
            "ERROR,bs_core_inst_can_pkg, open cmd fail(%d)(%s|%s)\n",
            errno, pkg->dev_id, pkg_filepath);

        rc = -2;
        goto DONE;
    }

    while (fgets(out, sizeof(out) - 1, fp) != NULL) {

        fwrite(out, 1, strlen(out), stdout);
        //TODO:check fail cond

        app->upgrade_stat.progress_percent += 20;
        if (app->upgrade_stat.progress_percent >= 100)
            app->upgrade_stat.progress_percent = 99;
        sleep(1);
    }
    app->upgrade_stat.progress_percent = 100;

    fprintf(stdout,
        "INFO,bs_core_inst_can_pkg, ecu inst succ(%s|%s)\n",
        pkg->dev_id, pkg_filepath);

DONE:
    if(fp)
        pclose(fp);

    return (rc);
}

#define BS_ETH_INST_PKG_NUM 4
typedef struct eth_inst_pkg_group_s {

    int pkg_num;
    int inst_id;
    int inst_stat;

    char dev_id[BS_MAX_DEV_ID_LEN + 1];
    char ftp_uri[BS_ETH_INST_PKG_NUM][BS_MAX_PKG_URL_LEN + 1];

}eth_inst_pkg_group_t;

static int bs_core_inst_eth_pkg(eth_inst_pkg_group_t* grp)
{
    (void)(grp);
    return 0;
}

static int bs_core_req_pkg_inst(struct bs_core_request* req)
{
    (void)(req);
    int rc = 0;
    char eth_ftp_uri[256] = { 0 };

    bs_l1_manifest_t* mani = req->payload.l1_mani;

    fprintf(stdout, "bs_core_req_pkg_inst, enter ...\n\n");

    //install all can/can-fd first 
    for (int i = 0; i < mani->pkg_num; ++i) {

        bs_l1_manifest_pkg_t* pkg = &mani->packages[i];
        if (pkg->dev_type == BS_DEV_TYPE_CAN ||
            pkg->dev_type == BS_DEV_TYPE_CAN_FD)
        {
            rc = bs_core_inst_can_pkg(pkg);
            if (rc) {
                fprintf(stderr,
                    "ERROR,bs_core_req_pkg_inst,inst pkg fail(%s:%s)\n",
                    pkg->dev_id, pick_file_name(pkg->pkg_url));

                goto DONE;
            }
            else {
                fprintf(stdout, "INFO,bs_core_req_pkg_inst,inst pkg succ(%s:%s)\n",
                    pkg->dev_id, pick_file_name(pkg->pkg_url));
            }
        }//inst can/can-fd pkg 
    }

    //install all eth pkg/only support 1 group now 
    eth_inst_pkg_group_t eth_grp = { 0 };
    for (int i = 0; i < mani->pkg_num; ++i) {

        bs_l1_manifest_pkg_t* pkg = &mani->packages[i];
        if (pkg->dev_type != BS_DEV_TYPE_ETH) {
            continue;
        }

        if (eth_grp.dev_id[0] == 0) {
            SAFE_CPY_STR(eth_grp.dev_id, pkg->dev_id, sizeof(eth_grp.dev_id));
        }        
        snprintf(eth_ftp_uri, sizeof(eth_ftp_uri),
            "ftp://%s/%s", g_ctx.tlc_ip, pick_file_name(pkg->pkg_url));

        SAFE_CPY_STR(eth_grp.ftp_uri[eth_grp.pkg_num], 
            eth_ftp_uri, BS_MAX_PKG_URL_LEN);

        eth_grp.pkg_num += 1;
        if (eth_grp.pkg_num >= BS_ETH_INST_PKG_NUM)
            break;
    }
    if (eth_grp.pkg_num > 0) {
        rc = bs_core_inst_eth_pkg(&eth_grp);
        if (rc) {
            fprintf(stderr,
                "ERROR,bs_core_req_pkg_inst,inst eth grp fail(%s)\n",
                eth_grp.dev_id);

            goto DONE;
        }
        else {
            fprintf(stdout, 
                "INFO,bs_core_req_pkg_inst,inst eth grp succ(%s)\n",
                eth_grp.dev_id);
        }
    }

DONE:
    fprintf(stdout, "bs_core_req_pkg_inst, leave =%d\n\n", rc);
    return (rc);
}

//--------------------------------------------------------------
//
//--------------------------------------------------------------
void *bs_core_thread(void *param)
{
  struct bs_core_request req = {0};
  (void) param;

  while (true) {
    if (g_received_signal != 0) {
      sleep(1);
      continue;
    }

    if (read(g_ctx.core_msg_sock[1], &req, sizeof(req)) < 0) {
      perror("Error reading worker sock");
      continue;
    }

    printf("Core_Thrd:rcv cmd :%d\n", req.cmd);    

    switch (req.cmd) {
      case BS_CORE_REQ_PKG_NEW:
        bs_cgw_set_stat(CGW_STAT_PKG_DOWNING);
        bs_core_req_pkg_new(&req);
        break;
      case BS_CORE_REQ_PKG_READY:
        bs_cgw_set_stat(CGW_STAT_PKG_READY);
        bs_core_req_pkg_ready(&req);
        //TODO:auto inst for demo
        break;
      case BS_CORE_REQ_PKG_INST:
        bs_cgw_set_stat(BS_CORE_REQ_PKG_INST);
        bs_core_req_pkg_inst(&req);
        break;
      case BS_CORE_REQ_TDR_STAT_UPDATE:
        bs_core_req_tdr_stat_update(&req);
        break;
      case BS_CORE_REQ_INVALID:
      default:
        fprintf(stderr, "ERROR,bs_core_thread,unkown cmd:%d", req.cmd);
        break;
    }
  }
  return NULL;
}

