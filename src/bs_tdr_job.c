#include "bs_core.h"
#include "bs_tdr_job.h"

void * bs_tdr_job_thread(void *param) {
  FILE *fp; // shell out put
  char cmd[256] = {0};
  char output[300];
  struct bs_core_request core_req;
  struct bs_device_app *app = (struct bs_device_app *) param;
  int progress;

  bs_init_core_request(&core_req);
  strcpy(core_req.dev_id, app->dev_id);
  core_req.cmd = BS_CORE_REQ_TDR_STAT_UPDATE;

  // synthersize command line
  sprintf(cmd, "ls -la %s", app->pkg_stat.name);//"./aaas_runtask.sh ");

  if ((fp = popen(cmd, "r")) == NULL) {
    core_req.payload.stat.progress_percent = 0;
    if (write(bs_get_core_ctx()->core_msg_sock[0], &core_req, sizeof(core_req)) < 0) {
      printf("Writing core sock error!\n");
    }
    return NULL;
  }

  progress = 0;
  while (fgets(output, sizeof(output)-1, fp) != NULL) {
    // forward to core
    printf("Core request TDR_RUN_UPDATE!\n");
    core_req.payload.stat.progress_percent = progress;
    if (write(bs_get_core_ctx()->core_msg_sock[0], &core_req, sizeof(core_req)) < 0) {
      printf("Writing core sock error!\n");
    }
    // TODO: analysis progress from output of the script
    progress += 20;
    if (progress >= 100) 
      progress = 99;
    sleep(1);
  }

  core_req.payload.stat.progress_percent = progress;
  if (write(bs_get_core_ctx()->core_msg_sock[0], &core_req, sizeof(core_req)) < 0) {
    printf("Writing core sock error!\n");
  }
  printf("CGW Orchstrator: tdr run success!\n");
  pclose(fp);
  return NULL;
}
