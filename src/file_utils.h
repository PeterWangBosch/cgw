#include "stdio.h"
#include "mongoose/mongoose.h"

struct file_writer_data {
  FILE *fp;
  size_t bytes_written;
};

FILE * on_read(const char *filename);
FILE * on_write(const char *filename);
int on_read_data(FILE *fp, uint8_t *buffer, int len);
int on_write_data(FILE *fp, uint8_t *buffer, int len);

