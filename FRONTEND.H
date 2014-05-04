#ifndef FRONTEND_H
#define FRONTEND_H

#include <stdio.h>
#include <stdint.h>
#include "backend.h" /* Front-end requires nwBackupParms from backend */

//#define FILE_BUF_SIZE 8192
#define FILE_BUF_SIZE 4096
//#define OUTBUF_SIZE BUFSIZ*8 //Cripples performance...
#define OUTBUF_SIZE BUFSIZ

/* Front end declarations */
signed char do_backup(nwBackupParms * parms, char * remote_name, char * local_dir, char * logfile_name);
signed char do_diff(nwBackupParms * parms, char * remote_name, char * local_dir, char * logfile_name);
signed char do_restore(nwBackupParms * parms, char * remote_name, char * local_dir, char * logfile_name);
int8_t send_file(FILE * fp, char * remote_name, uint8_t * out_buf, uint16_t payload_size);
int8_t restore_file(FILE * fp, char * remote_name, uint8_t * out_buffer, uint16_t payload_size);
/* Not in any include file... this is a bit of a hack, but we'll manage for now... */
int check_heap(void);



#endif        //  #ifndef FRONTEND_H

