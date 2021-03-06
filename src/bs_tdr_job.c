#include "bs_core.h"
#include "bs_dlc_apis.h"
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
  sprintf(cmd, "sh /tdr/usr/bin/aaas_runtask.sh RDA %s", app->pkg_stat.name);//"./aaas_runtask.sh ");
  printf("We are going to run cmd: %s\n", cmd); 

  if ((fp = popen(cmd, "r")) == NULL) {
    core_req.payload.stat.progress_percent = 0;
    dlc_report_status_finish_wpc_fail();
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
  pclose(fp);
  printf("CGW Orchstrator: tdr run ready!\n");

  //------------------------------
  if ((fp = popen("sh /tdr/usr/bin/aaas_runtask.sh RDA /data/data/ExecuteRdaJob.ea8b7e7f-349b-4790-8f81-fa5dfd1c6fed.v1.zip", "r")) == NULL) {
    printf("Run ExecuteRdaJob error!\n");
    dlc_report_status_finish_wpc_fail();
    return NULL;
  }

  while (fgets(output, sizeof(output)-1, fp) != NULL) {
    sleep(1);
  }
  //------------------------------

  core_req.payload.stat.progress_percent = 100;
  if (write(bs_get_core_ctx()->core_msg_sock[0], &core_req, sizeof(core_req)) < 0) {
    printf("Writing core sock error!\n");
  }

  sleep(40);
  dlc_report_status_finish_wpc();

  printf("CGW Orchstrator: tdr run success!\n");
  pclose(fp);
  return NULL;
}
