#ifndef BS_JSON_UTILS_H_
#define BS_JSON_UTILS_H_

#include "bs_core.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

//
#define BS_CFG_MAX_APP_TXT_LEN 2048
#define BS_CFG_MAX_MANI_TXT_LEN 4096
typedef struct bs_device_app bs_device_app_t;


typedef enum err_json_cfg_e {

    JCFG_ERR_OK = 0,
    JCFG_ERR_APP_FILE_OPEN_READ_FAIL,//open app config file to read
    JCFG_ERR_APP_FILE_OPEN_WRITE_FAIL,//open app config file to write
    JCFG_ERR_L1_MANI_FILE_OPEN_READ_FAIL,//open app config file to write
    JCFG_ERR_L1_MANI_FILE_OPEN_WRITE_FAIL,//open app config file to write
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
//
#define BS_MAX_MANI_PKG_NUM 4
#define BS_MAX_DEV_ID_LEN   32
#define BS_PKG_CHK_SUM_LEN  32
#define BS_MAX_PKG_URL_LEN  128
//
//
typedef struct bs_l1_manifest_pkg_s {
    int dev_type;//bs_dev_type_e_t
    char dev_id[BS_MAX_DEV_ID_LEN + 1];
    char chk_sum[BS_PKG_CHK_SUM_LEN + 1];
    char pkg_url[BS_MAX_PKG_URL_LEN + 1];
    int pkg_siz;
}bs_l1_manifest_pkg_t;
//
typedef enum bs_l1_manifest_status_e {

    BS_DEV_TYPE_IDLE = 0,//just recv document
    BS_DEV_TYPE_DONE,//all flush ok
    BS_DEV_TYPE_PROCESSING,//in processing

}bs_l1_manifest_status_t;
//
typedef struct bs_l1_manifest_s {
    int status;//bs_l1_manifest_status_t
    int pkg_num;
    bs_l1_manifest_pkg_t packages[BS_MAX_MANI_PKG_NUM];
    char mani_txt[BS_CFG_MAX_MANI_TXT_LEN];
}bs_l1_manifest_t;

//


int bs_load_app_config(const char* file_name, bs_device_app_t apps[], int max_app);
int bs_save_app_config(const char* file_name, bs_device_app_t apps[], int max_app);

//parse l1_manifest json document to bs_l1_manifest_t::packages & pkg_num
//and store orig json document to bs_l1_manifest_t::mani_txt
int bs_store_and_parse_l1_manifest(const char* json_txt, bs_l1_manifest_t* l1_mani);
//save bs_l1_manifest_t::mani_txt & status to file
int bs_save_l1_manifest(const char* file_name, bs_l1_manifest_t* l1_mani); 
//load file to bs_l1_manifest_t::mani_txt & status
int bs_load_l1_manifest(const char* file_name, bs_l1_manifest_t* l1_mani);

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

