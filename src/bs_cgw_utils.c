#include <assert.h>
#include <stdio.h>

#include "bs_core.h"
#include "bs_cgw_utils.h"
#include "file_utils.h"
#include "cJSON/cJSON.h"

#define W_JCHR(C)                                                       \
    do {                                                                \
        w_ptr[0] = C;                                                   \
        spac_len -= 1;                                                  \
        w_ptr += 1;                                                 \
    }while(0)

#define W_JBUF(KEY, BUF)                                                \
    do {                                                                \
        if (BUF[sizeof(BUF)-1] != 0) BUF[sizeof(BUF)-1] = 0;            \
        int siz =  snprintf(w_ptr, spac_len, "\"%s\":\"%s\"", KEY, BUF);\
        spac_len -= siz;                                                \
        w_ptr += siz;                                               \
    }while(0)

#define W_JSTR(KEY, STR)                                                \
    do {                                                                \
        int siz =  snprintf(w_ptr, spac_len, "\"%s\":\"%s\"", KEY, STR);\
        spac_len -= siz;                                                \
        w_ptr += siz;                                               \
    }while(0)

#define W_JSTR_AS_OBJ(KEY, STR)                                         \
    do {                                                                \
        int siz =  snprintf(w_ptr, spac_len, "\"%s\":%s", KEY, STR);    \
        spac_len -= siz;                                                \
        w_ptr += siz;                                               \
    }while(0)

#define W_JINT(KEY, INT)                                                \
    do {                                                                \
        int siz =  snprintf(w_ptr, spac_len, "\"%s\":%d", KEY, INT);    \
        spac_len -= siz;                                                \
        w_ptr += siz;                                               \
    }while(0)

#define W_JFLT(KEY, FLT)                                                \
    do {                                                                \
        int siz =  snprintf(w_ptr, spac_len, "\"%s\":%f", KEY, FLT);    \
        spac_len -= siz;                                                \
        w_ptr += siz;                                               \
    }while(0)

#define W_JOBJ_ENT(NAM)                                                 \
    do {                                                                \
        int siz =  snprintf(w_ptr, spac_len, "\"%s\":{", NAM);          \
        spac_len -= siz;                                                \
        w_ptr += siz;                                               \
    } while (0)


int bs_load_app_config(const char* file_name, bs_device_app_t* apps, int max_app)
{
    FILE* fp = NULL;
    char* cfg = NULL;
    struct cJSON* j_root = NULL;


    int rc = JCFG_ERR_OK;
    int MAX_CFG_TXT_LEN = BS_CFG_MAX_APP_TXT_LEN * max_app;

    fp = on_read(file_name);
    if (!fp) {
        rc = JCFG_ERR_APP_FILE_OPEN_READ_FAIL;
        goto DONE;
    }

    cfg = malloc(MAX_CFG_TXT_LEN+1);
    if (cfg == NULL) {
        fprintf(stderr, "ERR:INIT_APP_CFG:failed to alloc mem.\n");
        rc = JCFG_ERR_ALLOC_MEM_FAIL;
        goto DONE;
    }

    rc = fread(cfg, 1, MAX_CFG_TXT_LEN, fp);
    if (!rc) {
        fprintf(stderr, "ERR:INIT_APP_CFG:failed to read config text(%d).\n", errno);
        rc = JCFG_ERR_SYSTEM_CALL_FAIL;
        goto DONE;
    }
    if (rc >= MAX_CFG_TXT_LEN) {
        fprintf(stderr, "ERR:INIT_APP_CFG:config txt to large.\n");
        rc = JCFG_ERR_BAD_DOCUMENT;
        goto DONE;
    }

    cfg[rc] = '\0';
    //fprintf(stdout, "INF:INIT_APP_CFG:load config text:\n%s\n", cfg);

    j_root = cJSON_Parse(cfg);
    if (j_root == NULL) {
        fprintf(stderr, "ERR:INIT_APP_CFG:failed to parse config text\n");
        rc = JCFG_ERR_BAD_DOCUMENT;
        goto DONE;
    }
    if (!cJSON_IsArray(j_root)) {
        fprintf(stderr, "ERR:INIT_APP_CFG:config json is not begin with array\n");
        rc = JCFG_ERR_BAD_DOCUMENT;
        goto DONE;
    }
    struct cJSON* j_app;
    int app_i;
    for (app_i = 0, j_app = j_root->child;
        j_app != NULL && app_i < max_app;
        app_i++, j_app = j_app->next) {

        if (!cJSON_IsObject(j_app)) {
            fprintf(stderr, "ERR:INIT_APP_CFG:config app is not a json_object\n");
            rc = JCFG_ERR_BAD_DOCUMENT;
            goto DONE;
        }

        bs_device_app_t* app = &apps[app_i];
        bs_init_device_app(app);
        app->slot_used = true;


        struct cJSON* j_itm;
        for (j_itm = j_app->child; j_itm != NULL; j_itm = j_itm->next) {
            if (!strcmp("dev_id", j_itm->string)) {
                SAFE_CPY_STR(app->dev_id, j_itm->valuestring, sizeof(app->dev_id));
            }
            else if (!strcmp("door_module", j_itm->string)) {
                app->door_module = (bool)(j_itm->valueint);
            }
            else if (!strcmp("pkg_stat", j_itm->string)) {
                struct cJSON* j_pkg;
                for (j_pkg = j_itm->child; j_pkg != NULL; j_pkg = j_pkg->next) {
                    if (!strcmp("type", j_pkg->string)) {
                        app->pkg_stat.type = (uint8_t)(j_pkg->valueint);
                    }
                    else if (!strcmp("name", j_pkg->string)) {
                        SAFE_CPY_STR(app->pkg_stat.name, j_pkg->valuestring,
                            sizeof(app->pkg_stat.name));
                    }
                    else if (!strcmp("stat", j_pkg->string)) {
                        if (!strcmp(bs_pkg_stat_idle, j_pkg->valuestring)) {
                            app->pkg_stat.stat = bs_pkg_stat_idle;
                        }
                        else if (!strcmp(bs_pkg_stat_new, j_pkg->valuestring)) {
                            app->pkg_stat.stat = bs_pkg_stat_new;
                        }
                        else if (!strcmp(bs_pkg_stat_loading, j_pkg->valuestring)) {
                            app->pkg_stat.stat = bs_pkg_stat_loading;
                        }
                        else if (!strcmp(bs_pkg_stat_fail, j_pkg->valuestring)) {
                            app->pkg_stat.stat = bs_pkg_stat_fail;
                        }
                        else {
                            app->pkg_stat.stat = bs_pkg_stat_succ;
                        }
                    }
                }
            }
            else if (!strcmp("upgrade_stat", j_itm->string)) {
                struct cJSON* j_upd;
                for (j_upd = j_itm->child; j_upd != NULL; j_upd = j_upd->next) {
                    if (!strcmp("esti_time", j_upd->string)) {
                        SAFE_CPY_STR(app->upgrade_stat.esti_time, j_upd->valuestring,
                            sizeof(app->upgrade_stat.esti_time));
                    }
                    else if (!strcmp("start_time", j_upd->string)) {
                        SAFE_CPY_STR(app->upgrade_stat.start_time, j_upd->valuestring,
                            sizeof(app->upgrade_stat.start_time));
                    }
                    else if (!strcmp("time_stamp", j_upd->string)) {
                        SAFE_CPY_STR(app->upgrade_stat.time_stamp, j_upd->valuestring,
                            sizeof(app->upgrade_stat.time_stamp));
                    }
                    else if (!strcmp("status", j_upd->string)) {
                        SAFE_CPY_STR(app->upgrade_stat.status, j_upd->valuestring,
                            sizeof(app->upgrade_stat.status));
                    }
                    else if (!strcmp("progress_percent", j_upd->string)) {
                        app->upgrade_stat.progress_percent = (float)j_upd->valuedouble;
                    }
                }
            }
        }
    }

DONE:
    if (fp)
        fclose(fp);
    if (cfg)
        free(cfg);
    if (j_root)
        cJSON_Delete(j_root);

    return (rc);
}

int bs_save_app_config(const char* filename, bs_device_app_t* apps, int max_app)
{
    FILE* fp = NULL;
    char* cfg = NULL;
    int rc = JCFG_ERR_OK;
    int MAX_CFG_TXT_LEN = BS_CFG_MAX_APP_TXT_LEN * max_app;

    fp = on_write(filename);
    if (!fp) {
        rc = JCFG_ERR_APP_FILE_OPEN_WRITE_FAIL;
        goto DONE;
    }

    cfg = malloc(MAX_CFG_TXT_LEN);
    if (cfg == NULL) {
        fprintf(stderr, "ERR:SAVE_APP_CFG:failed to alloc mem.\n");
        rc = JCFG_ERR_ALLOC_MEM_FAIL;
        goto DONE;
    }

    size_t spac_len = MAX_CFG_TXT_LEN - 1;
    memset(cfg, 0, MAX_CFG_TXT_LEN);


    char* w_ptr = cfg;
    W_JCHR('[');
    int app_n = 0;
    {
        for (int i = 0; i < max_app; ++i) {
            bs_device_app_t* app = &apps[i];
            if (app->dev_id[0] == 0)
                continue;

            // avoid buf overflow
            if (spac_len < BS_CFG_MAX_APP_TXT_LEN)
                break;

            if (app_n > 0)
                W_JCHR(',');

            W_JCHR('{');
            {
                W_JBUF("dev_id", app->dev_id); W_JCHR(',');
                W_JINT("door_module", app->door_module); W_JCHR(',');

                W_JOBJ_ENT("pkg_stat");
                {
                    W_JINT("type", app->pkg_stat.type); W_JCHR(',');
                    W_JBUF("name", app->pkg_stat.name); W_JCHR(',');
                    W_JSTR("stat", app->pkg_stat.stat);
                }
                W_JCHR('}'); W_JCHR(',');

                W_JOBJ_ENT("upgrade_stat");
                {
                    W_JBUF("esti_time", app->upgrade_stat.esti_time); W_JCHR(',');
                    W_JBUF("start_time", app->upgrade_stat.start_time); W_JCHR(',');
                    W_JBUF("time_stamp", app->upgrade_stat.time_stamp); W_JCHR(',');
                    W_JBUF("status", app->upgrade_stat.status); W_JCHR(',');
                    W_JFLT("progress_percent", app->upgrade_stat.progress_percent);
                }
                W_JCHR('}');
            }
            W_JCHR('}');

            ++app_n;
        }
    }
    W_JCHR(']');
    W_JCHR('\0');

    size_t w_len = fwrite(cfg, 1, strlen(cfg), fp);
    if (w_len != strlen(cfg)) {
        fprintf(stderr, "ERR:SAVE_APP_CFG:write to file error:%d.\n", errno);
        rc = JCFG_ERR_SYSTEM_CALL_FAIL;
        goto DONE;
    }

DONE:
    if (fp)
        fclose(fp);
    if (cfg)
        free(cfg);

    return (rc);
}

static int bs_parse_l1_manifest(struct cJSON* root, bs_l1_manifest_t* l1_mani)
{
    struct cJSON* manifest = NULL;
    struct cJSON* packages = NULL;
    int rc = JCFG_ERR_OK;

    manifest = cJSON_GetObjectItem(root, "manifest");
    if (!cJSON_IsObject(manifest)) {
        fprintf(stderr, "[/manifest] object not find");
        rc = JCFG_ERR_BAD_DOCUMENT;
        goto DONE;
    }
    packages = cJSON_GetObjectItem(manifest, "packages");
    if (!cJSON_IsArray(packages)) {
        fprintf(stderr, "[/manifest/packages] array not find");
        rc = JCFG_ERR_BAD_DOCUMENT;
        goto DONE;
    }

    if (cJSON_GetArraySize(packages) > BS_MAX_MANI_PKG_NUM) {
        fprintf(stderr,
            "[/manifest/packages].size=%d larger then limit=%d",
            cJSON_GetArraySize(packages), BS_MAX_MANI_PKG_NUM);
        rc = JCFG_ERR_BAD_DOCUMENT;
        goto DONE;
    }
    for (int i = 0; i < cJSON_GetArraySize(packages); ++i) {
        struct cJSON* jpkg = cJSON_GetArrayItem(packages, i);
        assert(jpkg);

        struct cJSON* ecu = cJSON_GetObjectItem(jpkg, "ecu");
        if (!cJSON_IsString(ecu)) {
            fprintf(stderr,
                "[/manifest/packages/[%d]/ecu]] string element not find", i + 1);
            rc = JCFG_ERR_BAD_DOCUMENT;
            goto DONE;
        }
        struct cJSON* typ = cJSON_GetObjectItem(jpkg, "deviceType");
        if (!cJSON_IsString(typ)) {
            fprintf(stderr,
                "[/manifest/packages/[%d]/deviceType]] string element not find", i + 1);
            rc = JCFG_ERR_BAD_DOCUMENT;
            goto DONE;
        }
        struct cJSON* res = cJSON_GetObjectItem(jpkg, "resources");
        if (!cJSON_IsObject(res)) {
            fprintf(stderr,
                "[/manifest/packages/[%d]/resources]] object not find", i + 1);
            rc = JCFG_ERR_BAD_DOCUMENT;
            goto DONE;
        }
        struct cJSON* sum = cJSON_GetObjectItem(res, "fullDownloadChecksum");
        if (!cJSON_IsString(sum)) {
            fprintf(stderr,
                "[/manifest/packages/[%d]/resources/fullDownloadChecksum]] string element not find", i + 1);
            rc = JCFG_ERR_BAD_DOCUMENT;
            goto DONE;
        }
        struct cJSON* url = cJSON_GetObjectItem(res, "fullDownloadUrl");
        if (!cJSON_IsString(url)) {
            fprintf(stderr,
                "[/manifest/packages/[%d]/resources/fullDownloadUrl]] string element not find", i + 1);
            rc = JCFG_ERR_BAD_DOCUMENT;
            goto DONE;
        }
        struct cJSON* siz = cJSON_GetObjectItem(res, "fullSize");
        if (!cJSON_IsNumber(siz)) {
            fprintf(stderr,
                "[/manifest/packages/[%d]/resources/fullSize]] number element not find", i + 1);
            rc = JCFG_ERR_BAD_DOCUMENT;
            goto DONE;
        }


        bs_l1_manifest_pkg_t* pkg = &l1_mani->packages[i];
        if (!strcmp("eth", typ->valuestring)) {
            pkg->dev_type = BS_DEV_TYPE_ETH;
        }
        else if (!strcmp("can", typ->valuestring)) {
            pkg->dev_type = BS_DEV_TYPE_CAN;
        }
        else if (!strcmp("can-fd", typ->valuestring)) {
            pkg->dev_type = BS_DEV_TYPE_CAN_FD;
        }
        else {
            fprintf(stderr,
                "[/manifest/packages/[%d]/deviceType=%s]] can be recognized",
                i + 1, typ->valuestring);
            rc = JCFG_ERR_BAD_DOCUMENT;
            goto DONE;
        }

        strncpy(pkg->dev_id, ecu->valuestring, BS_MAX_DEV_ID_LEN);
        strncpy(pkg->pkg_url, url->valuestring, BS_MAX_PKG_URL_LEN);
        strncpy(pkg->chk_sum, sum->valuestring, BS_PKG_CHK_SUM_LEN);
        pkg->pkg_siz = siz->valueint;

        //
        l1_mani->pkg_num++;

    }//foreach pkg in packages

DONE:
    return (rc);
}

int bs_store_and_parse_l1_manifest(const char* json_txt, bs_l1_manifest_t* l1_mani)
{
    assert(json_txt);
    assert(l1_mani);

    fprintf(stdout, "--- parse l1_manifest enter -----\n");
    //fprintf(stdout, "%s", json_txt);
    //fprintf(stdout, "---------------------------------\n");

    int rc = JCFG_ERR_OK;
    memset(l1_mani, 0, sizeof(bs_l1_manifest_t));
    l1_mani->pkg_num = 0;
    //
    SAFE_CPY_STR(l1_mani->mani_txt, json_txt, BS_CFG_MAX_MANI_TXT_LEN);


    struct cJSON* root = NULL;


    root = cJSON_Parse(json_txt);
    if (NULL == root) {
        fprintf(stderr, "parse l1_mainfest document failed");
        rc = JCFG_ERR_BAD_DOCUMENT;
        goto DONE;
    }
    rc = bs_parse_l1_manifest(root, l1_mani);
    if (JCFG_ERR_OK == rc) {
        fprintf(stdout,
            "--- parse l1_manifest success----\n"
            "---------------------------------\n");
    }

DONE:
    if (root)
        cJSON_Delete(root);

    return (rc);
}

int bs_save_l1_manifest(const char* file_name, bs_l1_manifest_t* l1_mani)
{
    assert(file_name);
    assert(l1_mani);

    FILE* fp = NULL;
    char* cfg = NULL;
    int rc = JCFG_ERR_OK;

    fp = fopen(file_name, "wt");
    if (!fp)    {
        fprintf(stderr, 
            "Could not open l1_manifest file '%s' to write\n", file_name);
        rc = JCFG_ERR_L1_MANI_FILE_OPEN_WRITE_FAIL;
        goto DONE;
    }

    cfg = malloc(BS_CFG_MAX_MANI_TXT_LEN);
    if (cfg == NULL) {
        fprintf(stderr, "ERR:SAVE_L1_MANI:failed to alloc mem.\n");
        rc = JCFG_ERR_ALLOC_MEM_FAIL;
        goto DONE;
    }

    size_t spac_len = BS_CFG_MAX_MANI_TXT_LEN - 1;
    memset(cfg, 0, BS_CFG_MAX_MANI_TXT_LEN);

    char* w_ptr = cfg;
    W_JCHR('{');
    {
        W_JINT("status", l1_mani->status); W_JCHR(',');
        W_JSTR_AS_OBJ("mani_txt", l1_mani->mani_txt);
    }
    W_JCHR('}');

    size_t w_len = fwrite(cfg, 1, strlen(cfg), fp);
    if (w_len != strlen(cfg)) {
        fprintf(stderr, "ERR:SAVE_L1_MANI:write to file error:%d.\n", errno);
        rc = JCFG_ERR_SYSTEM_CALL_FAIL;
        goto DONE;
    }

DONE:
    if (fp)
        fclose(fp);
    if (cfg)
        free(cfg);

    return (rc);
}

int bs_load_l1_manifest(const char* file_name, bs_l1_manifest_t* l1_mani)
{
    assert(file_name);
    assert(l1_mani);

    FILE* fp = NULL;
    char* cfg = NULL;

    struct cJSON* root = NULL;
    int rc = JCFG_ERR_OK;

    cfg = malloc(BS_CFG_MAX_MANI_TXT_LEN + 1);
    if (cfg == NULL) {
        fprintf(stderr, "ERR::LOAD_L1_MANI:failed to alloc mem.\n");
        rc = JCFG_ERR_ALLOC_MEM_FAIL;
        goto DONE;
    }

    fp = on_read(file_name);
    if (!fp) {
        rc = JCFG_ERR_L1_MANI_FILE_OPEN_READ_FAIL;
        goto DONE;
    }

    rc = fread(cfg, 1, BS_CFG_MAX_MANI_TXT_LEN, fp);
    if (!rc) {
        fprintf(stderr, "ERR:LOAD_L1_MANI:failed to read(%d).\n", errno);
        rc = JCFG_ERR_SYSTEM_CALL_FAIL;
        goto DONE;
    }
    if (rc >= BS_CFG_MAX_MANI_TXT_LEN) {
        fprintf(stderr, "ERR:LOAD_L1_MANI:config txt to large.\n");
        rc = JCFG_ERR_BAD_DOCUMENT;
        goto DONE;
    }
    cfg[rc] = '\0';

    root = cJSON_Parse(cfg);
    if (NULL == root) {
        fprintf(stderr, "ERR:LOAD_L1_MANI:parse l1_mainfest document failed");
        rc = JCFG_ERR_BAD_DOCUMENT;
        goto DONE;
    }

    struct cJSON* status = cJSON_GetObjectItem(root, "status");
    if (!cJSON_IsNumber(status)) {
        fprintf(stderr,
            "[/status] number element not find");
        rc = JCFG_ERR_BAD_DOCUMENT;
        goto DONE;
    }

    struct cJSON* mani_txt = cJSON_GetObjectItem(root, "mani_txt");
    if (!cJSON_IsObject(mani_txt)) {
        fprintf(stderr,
            "[/mani_txt] object element not find");
        rc = JCFG_ERR_BAD_DOCUMENT;
        goto DONE;
    }
    rc = bs_parse_l1_manifest(mani_txt, l1_mani);
    if (JCFG_ERR_OK != rc) {
        goto DONE;
    }
    l1_mani->status = status->valueint;
    //
    cJSON_PrintPreallocated(mani_txt, l1_mani->mani_txt, BS_CFG_MAX_MANI_TXT_LEN, false);

DONE:
    if (fp)
        fclose(fp);
    if (cfg)
        free(cfg);
    if (root)
        cJSON_Delete(root);

    return (rc);
}
