#ifndef BS_DLC_APIS_H_
#define BS_DLC_APIS_H_

#include <stdio.h>
#include "mongoose/mongoose.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

//------------------------------------------------------------------
// 
//------------------------------------------------------------------

void dlc_report_stat_resp(struct mg_connection *, int, void *);
void * cgw_msg_thread(void *);
void dlc_report_status_start();
void dlc_report_status_finish_fail();
void dlc_report_status_finish();
void dlc_report_status_finish_wpc_fail();
void dlc_report_status_finish_wpc();
void dlc_report_status_down();
#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* BS_DLC_APIS_H_ */


