#ifndef BS_JSON_UTILS_H_
#define BS_JSON_UTILS_H_

#include "bs_core.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum err_json_cfg_e {

    JCFG_ERR_OK = 0,
    JCFG_ERR_APP_FILE_OPEN_READ_FAIL,//open app config file to read
    JCFG_ERR_APP_FILE_OPEN_WRITE_FAIL,//open app config file to write
    JCFG_ERR_BAD_DOCUMENT,//invalid json document
    JCFG_ERR_SYSTEM_CALL_FAIL,//system call failed
    JCFG_ERR_ALLOC_MEM_FAIL,//mem alloc failed

}err_json_cfg_t;
//
typedef enum bs_dev_type_e {

    BS_DEV_TYPE_NONE = 0,//!not initized
    BS_DEV_TYPE_ETH,
    BS_DEV_TYPE_CAN,
    BS_DEV_TYPE_CAN_FD,

}bs_dev_type_e_t;

#define BS_CFG_MAX_APP_TXT_LEN 2048
typedef struct bs_device_app bs_device_app_t;

int bs_load_app_config(const char* file_name, bs_device_app_t apps[], int max_app);
int bs_save_app_config(const char* file_name, bs_device_app_t apps[], int max_app);

#define SAFE_CPY_STR(dst, src, n) \
do { \
    size_t m = strlen(src) > n - 1 ? n - 1 : strlen(src); \
    memcpy(dst, src, m); \
    dst[m] = '\0'; \
}while (0)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* BS_JSON_UTILS_H_ */

