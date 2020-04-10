#include "file_utils.h"

//------------------------------------------------------------------
// File utilities
//------------------------------------------------------------------

FILE * on_read(const char *filename)
{
    FILE *fp;

    if ((fp = fopen(filename, "r")) == NULL)
    {
        printf("Could not open file '%s'\n", filename);
        return NULL;
    } else {
        printf("Preparing to start reading file '%s'\n", filename);
        return fp;
    }
}

FILE * on_write(const char *filename)
{
    FILE *fp;

    if ((fp = fopen(filename, "w")) == NULL)
    {
        printf("Could not open destination file '%s'\n", filename);
        return NULL;
    } else {
        return fp;
    }
}

int on_read_data(FILE *fp, uint8_t *buffer, int len)
{
    int read_cn = -1;
    if ((read_cn = fread(buffer, 1, len, fp)) > 0) {
        return read_cn;
    }
    return -1;
}

int on_write_data(FILE *fp, uint8_t *buffer, int len)
{
    fwrite(buffer, 1, len, fp);
    return 1;
}

