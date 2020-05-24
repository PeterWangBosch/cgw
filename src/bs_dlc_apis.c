#include "bs_dlc_apis.h"
#include "bs_core.h"

static int dlc_report_thread_exit;

static char g_stub_report_down[] = "{\"fotaProtocolVersion\":\"HHFOTA-0.1\",\"vehicleVersion\":{\"orchestrator\":\"0.100\",\"dlc\":\"0.100\"},\"upgradeResults\":{\"servicePack\":\"VDCM\",\"campaign\":\"VDCM\",\"downloadStartTime\":\"20200531 240000\",\"downloadFinishTime\":\"20200531 240000\",\"userConfirmationTime\":\"20200531 240000\",\"startTime\":\"20200531 240000\",\"finishTime\":\"20200531 240000\",\"result\":\"fail-irrecoverable\",\"dlcReports\":[{\"timestamp\":\"20200501 240000\",\"errorCode\":-1,\"trace\":\"\"}],\"deviceReports\":[{\"ecu\":\"WPC\",\"softwareId\":\"WPC2.0.0\",\"startTime\":\"20200531 240000\",\"finishTime\":\"20200531 240000\",\"previousVersion\":\"1.0.0\",\"result\":\"fail-irrecoverable\",\"targetVersion\":\"2.0.0\",\"currentVersion\":\"2.0.0\",\"logs\":[{\"timestamp\":\"20200531 240000\",\"progress\":0,\"errorCode\":-1,\"trace\":\"Not connected!\"}]},{\"ecu\":\"VDCM1\",\"softwareId\":\"VDCM2.0.0\",\"startTime\":\"20200531 240000\",\"finishTime\":\"20200531 240000\",\"previousVersion\":\"1.0.0\",\"result\":\"fail-irrecoverable\",\"targetVersion\":\"2.0.0\",\"currentVersion\":\"2.0.0\",\"logs\":[{\"timestamp\":\"20200531 240000\",\"progress\":0,\"errorCode\":-1,\"trace\":\"VDCM Down!\"}]},{\"ecu\":\"VDCM2\",\"softwareId\":\"VDCM2.0.0\",\"startTime\":\"20200531 240000\",\"finishTime\":\"20200531 240000\",\"previousVersion\":\"1.0.0\",\"result\":\"fail-irrecoverable\",\"targetVersion\":\"2.0.0\",\"currentVersion\":\"2.0.0\",\"logs\":[{\"timestamp\":\"20200531 240000\",\"progress\":0,\"errorCode\":-1,\"trace\":\"VDCM Down!\"}]},{\"ecu\":\"VDCM3\",\"softwareId\":\"VDCM2.0.0\",\"startTime\":\"20200531 240000\",\"finishTime\":\"20200531 240000\",\"previousVersion\":\"1.0.0\",\"result\":\"fail-irrecoverable\",\"targetVersion\":\"2.0.0\",\"currentVersion\":\"2.0.0\",\"logs\":[{\"timestamp\":\"20200531 240000\",\"progress\":0,\"errorCode\":-1,\"trace\":\"VDCM Down!\"}]},{\"ecu\":\"VDCM4\",\"softwareId\":\"VDCM2.0.0\",\"startTime\":\"20200531 240000\",\"finishTime\":\"20200531 240000\",\"previousVersion\":\"1.0.0\",\"result\":\"fail-irrecoverable\",\"targetVersion\":\"2.0.0\",\"currentVersion\":\"2.0.0\",\"logs\":[{\"timestamp\":\"20200531 240000\",\"progress\":0,\"errorCode\":-1,\"trace\":\"VDCM Down!\"}]}]}}";

static char g_stub_report_start[] = "{\"fotaProtocolVersion\":\"HHFOTA-0.1\",\"vehicleVersion\":{\"orchestrator\":\"0.100\",\"dlc\":\"0.100\"},\"upgradeResults\":{\"servicePack\":\"VDCM\",\"campaign\":\"VDCM\",\"downloadStartTime\":\"20200531 240000\",\"downloadFinishTime\":\"20200531 240000\",\"userConfirmationTime\":\"20200531 240000\",\"startTime\":\"20200531 240000\",\"finishTime\":\"20200531 240000\",\"result\":\"success\",\"dlcReports\":[{\"timestamp\":\"20200501 240000\",\"errorCode\":-1,\"trace\":\"\"}],\"deviceReports\":[{\"ecu\":\"WPC\",\"softwareId\":\"VDCM2.0.0\",\"startTime\":\"20200531 240000\",\"finishTime\":\"20200531 240000\",\"previousVersion\":\"1.0.0\",\"result\":\"success\",\"targetVersion\":\"2.0.0\",\"currentVersion\":\"1.0.0\",\"logs\":[{\"timestamp\":\"20200531 240000\",\"progress\":0,\"errorCode\":-1,\"trace\":\"VDCM Upgrade Started!\"}]}, {\"ecu\":\"VDCM1\",\"softwareId\":\"VDCM2.0.0\",\"startTime\":\"20200531 240000\",\"finishTime\":\"20200531 240000\",\"previousVersion\":\"1.0.0\",\"result\":\"success\",\"targetVersion\":\"2.0.0\",\"currentVersion\":\"1.0.0\",\"logs\":[{\"timestamp\":\"20200531 240000\",\"progress\":0,\"errorCode\":-1,\"trace\":\"VDCM Upgrade Started!\"}]},{\"ecu\":\"VDCM2\",\"softwareId\":\"VDCM2.0.0\",\"startTime\":\"20200531 240000\",\"finishTime\":\"20200531 240000\",\"previousVersion\":\"1.0.0\",\"result\":\"success\",\"targetVersion\":\"2.0.0\",\"currentVersion\":\"1.0.0\",\"logs\":[{\"timestamp\":\"20200531 240000\",\"progress\":0,\"errorCode\":-1,\"trace\":\"VDCM Upgrade Started!\"}]},{\"ecu\":\"VDCM3\",\"softwareId\":\"VDCM2.0.0\",\"startTime\":\"20200531 240000\",\"finishTime\":\"20200531 240000\",\"previousVersion\":\"1.0.0\",\"result\":\"success\",\"targetVersion\":\"2.0.0\",\"currentVersion\":\"1.0.0\",\"logs\":[{\"timestamp\":\"20200531 240000\",\"progress\":0,\"errorCode\":-1,\"trace\":\"VDCM Upgrade Started!\"}]},{\"ecu\":\"VDCM4\",\"softwareId\":\"VDCM2.0.0\",\"startTime\":\"20200531 240000\",\"finishTime\":\"20200531 240000\",\"previousVersion\":\"1.0.0\",\"result\":\"success\",\"targetVersion\":\"2.0.0\",\"currentVersion\":\"1.0.0\",\"logs\":[{\"timestamp\":\"20200531 240000\",\"progress\":0,\"errorCode\":-1,\"trace\":\"VDCM Upgrade Started!\"}]}]}}";

static char g_stub_report_finish[] = "{\"fotaProtocolVersion\":\"HHFOTA-0.1\",\"vehicleVersion\":{\"orchestrator\":\"0.100\",\"dlc\":\"0.100\"},\"upgradeResults\":{\"servicePack\":\"VDCM\",\"campaign\":\"VDCM\",\"downloadStartTime\":\"20200531 240000\",\"downloadFinishTime\":\"20200531 240000\",\"userConfirmationTime\":\"20200531 240000\",\"startTime\":\"20200531 240000\",\"finishTime\":\"20200531 240000\",\"result\":\"success\",\"dlcReports\":[{\"timestamp\":\"20200501 240000\",\"errorCode\":-1,\"trace\":\"\"}],\"deviceReports\":[{\"ecu\":\"WPC\",\"softwareId\":\"WPC2.0.0\",\"startTime\":\"20200531 240000\",\"finishTime\":\"20200531 240000\",\"previousVersion\":\"1.0.0\",\"result\":\"success\",\"targetVersion\":\"2.0.0\",\"currentVersion\":\"2.0.0\",\"logs\":[{\"timestamp\":\"20200531 240000\",\"progress\":100,\"errorCode\":-1,\"trace\":\"not connected\"}]},{\"ecu\":\"VDCM1\",\"softwareId\":\"VDCM2.0.0\",\"startTime\":\"20200531 240000\",\"finishTime\":\"20200531 240000\",\"previousVersion\":\"1.0.0\",\"result\":\"success\",\"targetVersion\":\"2.0.0\",\"currentVersion\":\"2.0.0\",\"logs\":[{\"timestamp\":\"20200531 240000\",\"progress\":100,\"errorCode\":-1,\"trace\":\"VDCM Upgrade Finished!\"}]},{\"ecu\":\"VDCM2\",\"softwareId\":\"VDCM2.0.0\",\"startTime\":\"20200531 240000\",\"finishTime\":\"20200531 240000\",\"previousVersion\":\"1.0.0\",\"result\":\"success\",\"targetVersion\":\"2.0.0\",\"currentVersion\":\"2.0.0\",\"logs\":[{\"timestamp\":\"20200531 240000\",\"progress\":100,\"errorCode\":-1,\"trace\":\"VDCM Upgrade Finished!\"}]},{\"ecu\":\"VDCM3\",\"softwareId\":\"VDCM2.0.0\",\"startTime\":\"20200531 240000\",\"finishTime\":\"20200531 240000\",\"previousVersion\":\"1.0.0\",\"result\":\"success\",\"targetVersion\":\"2.0.0\",\"currentVersion\":\"2.0.0\",\"logs\":[{\"timestamp\":\"20200531 240000\",\"progress\":100,\"errorCode\":-1,\"trace\":\"VDCM Upgrade Finished!\"}]},{\"ecu\":\"VDCM4\",\"softwareId\":\"VDCM2.0.0\",\"startTime\":\"20200531 240000\",\"finishTime\":\"20200531 240000\",\"previousVersion\":\"1.0.0\",\"result\":\"success\",\"targetVersion\":\"2.0.0\",\"currentVersion\":\"2.0.0\",\"logs\":[{\"timestamp\":\"20200531 240000\",\"progress\":100,\"errorCode\":-1,\"trace\":\"VDCM Upgrade Finished!\"}]}]}}";

void dlc_report_stat_resp(struct mg_connection *nc, int ev, void *ev_data)
{
  struct http_message *hm = (struct http_message *) ev_data;
  (void) hm;

  switch (ev) {
    case MG_EV_CONNECT:
      break;
    case MG_EV_HTTP_REPLY:
      nc->flags |= MG_F_CLOSE_IMMEDIATELY;
      dlc_report_thread_exit = 1;
      break;
    case MG_EV_CLOSE:
      if (dlc_report_thread_exit == 0) {
        printf("Orchestrator report status done\n");
        dlc_report_thread_exit = 1;
      }
      break;
    default:
      break;
  }
}

void * dlc_report_thread(void *param)
{
  struct mg_mgr mgr;
  char *post_data = (char *) param;
  char api[64];

  sprintf(api, "http://%s:8019/status", bs_get_core_ctx()->tlc_ip);
  printf("----------Report to DLC: %s\n", bs_get_core_ctx()->tlc_ip);
  printf("%s\n", post_data);
  printf("---------------------------\n");
  mg_mgr_init(&mgr, NULL);
  mg_connect_http(&mgr, dlc_report_stat_resp, api,
                  "Content-Type: application/json\r\n",
                  post_data);

  while (dlc_report_thread_exit) {
    // run until one api request end
    mg_mgr_poll(&mgr, 1000);
  }

  dlc_report_thread_exit = 0;
  mg_mgr_free(&mgr);
  return NULL;
}

void dlc_report_status_down()
{
  mg_start_thread(dlc_report_thread, g_stub_report_down);
}

void dlc_report_status_start()
{
  mg_start_thread(dlc_report_thread, g_stub_report_start);
}

void dlc_report_status_finish()
{
  mg_start_thread(dlc_report_thread, g_stub_report_finish);
}

