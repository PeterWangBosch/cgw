#include <assert.h>
#include <stdio.h>

#include "bs_cgw_utils.h"
#include "file_utils.h"
#include "cJSON/cJSON.h"


int bs_load_app_config(const char* file_name, bs_device_app_t apps[], int max_app)
{
    FILE* fp = NULL;
    char* cfg = NULL;
    int rc = JCFG_ERR_OK;
    int MAX_CFG_TXT_LEN = BS_CFG_MAX_APP_TXT_LEN * max_app;

    fp = on_read(file_name);
    if (!fp) {
        rc = JCFG_ERR_APP_FILE_OPEN_READ_FAIL;
        goto DONE;
    }

    cfg = malloc(MAX_CFG_TXT_LEN);
    if (cfg == NULL) {
        fprintf(stderr, "ERR:INIT_APP_CFG:failed to alloc mem.\n");
        rc = JCFG_ERR_ALLOC_MEM_FAIL;
        goto DONE;
    }

    rc = fread(cfg, 1, MAX_CFG_TXT_LEN - 1, fp);
    if (!rc) {
        fprintf(stderr, "ERR:INIT_APP_CFG:failed to read config text(%d).\n", errno);
        rc = JCFG_ERR_SYSTEM_CALL_FAIL;
        goto DONE;
    }
    cfg[rc] = '\0';
    //fprintf(stdout, "INF:INIT_APP_CFG:load config text:\n%s\n", cfg);

    struct cJSON* j_root = NULL;
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

        struct bs_device_app* app = &apps[app_i];
        bs_init_device_app(app);

        if (!cJSON_IsObject(j_app)) {
            fprintf(stderr, "ERR:INIT_APP_CFG:config app is not a json_object\n");
            rc = JCFG_ERR_BAD_DOCUMENT;
            goto DONE;
        }

        struct cJSON* j_itm;
        for (j_itm = j_app->child; j_itm != NULL; j_itm = j_itm->next) {
            if (!strcmp("dev_id", j_itm->string)) {
                SAFE_CPY_STR(app->dev_id, j_itm->valuestring, sizeof(app->dev_id));
            }
            else if (!strcmp("soft_id", j_itm->string)) {
                SAFE_CPY_STR(app->soft_id, j_itm->valuestring, sizeof(app->soft_id));
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

int bs_save_app_config(const char* filename, bs_device_app_t apps[], int max_app)
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
    memset(cfg, 0, spac_len);

#define W_JCHR(C)                                                       \
    do {                                                                \
        w_ptr[0] = C;                                                   \
        spac_len -= 1;                                                  \
        w_ptr += 1;                                                 \
    }while(0)

#define W_JBUF(KEY, BUF)                                                \
    do {                                                                \
        if (BUF[sizeof(BUF)-1] != 0) BUF[sizeof(BUF)-1] = 0;            \
        siz =  snprintf(w_ptr, spac_len, "\"%s\":\"%s\"", KEY, BUF);    \
        spac_len -= siz;                                                \
        w_ptr += siz;                                               \
    }while(0)

#define W_JSTR(KEY, STR)                                                \
    do {                                                                \
        siz =  snprintf(w_ptr, spac_len, "\"%s\":\"%s\"", KEY, STR);    \
        spac_len -= siz;                                                \
        w_ptr += siz;                                               \
    }while(0)

#define W_JINT(KEY, INT)                                                \
    do {                                                                \
        siz =  snprintf(w_ptr, spac_len, "\"%s\":%d", KEY, INT);        \
        spac_len -= siz;                                                \
        w_ptr += siz;                                               \
    }while(0)

#define W_JFLT(KEY, FLT)                                                \
    do {                                                                \
        siz =  snprintf(w_ptr, spac_len, "\"%s\":%f", KEY, FLT);        \
        spac_len -= siz;                                                \
        w_ptr += siz;                                               \
    }while(0)

#define W_JOBJ_ENT(NAM)                                                 \
    do {                                                                \
            siz = snprintf(w_ptr, spac_len, "\"%s\":{", NAM);           \
            spac_len -= siz;                                            \
            w_ptr += siz;                                        \
    } while (0)

    char* w_ptr = cfg;
    w_ptr[0] = '[';
    --spac_len;
    ++w_ptr;
    int siz = 0;
    int app_n = 0;
    {
        for (int i = 0; i < max_app; ++i) {
            struct bs_device_app* app = &apps[i];
            if (app->dev_id[0] == 0 && app->soft_id[0] == 0)
                continue;

            // avoid buf overflow
            if (spac_len < BS_CFG_MAX_APP_TXT_LEN)
                break;

            if (app_n > 1)
                W_JCHR(',');

            W_JCHR('{');
            {
                W_JBUF("dev_id", app->dev_id); W_JCHR(',');
                W_JBUF("soft_id", app->soft_id); W_JCHR(',');
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
    w_ptr[0] = ']';

    size_t w_len = fwrite(cfg, 1, strlen(cfg) + 1, fp);
    if (w_len != strlen(cfg) + 1) {
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

