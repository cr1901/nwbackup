#ifndef NWBACKUP_H
#define NWBACKUP_H

/* None of this is needed for now... */
/* enum progMode {
  BACKUP = 1, RESTORE
};

enum pollStatus {
  READ_ERROR=-1, NO_CHARS_RECEIVED, CHARS_RECEIVED
};

enum backupState{
  UNINITIALIZED, OPEN_SERVER, CLOSE_SERVER, CLOSE_SERVER_WAIT_RTN,
  SEND_USER, SEND_USER_WAIT_RTN, SEND_PASS, SEND_PASS_WAIT_RTN, CHDIR_BACKUP_ROOT,
  CHDIR_ROOT_WAIT_RTN, GET_TREE_SEND_CHDIR, SEND_CHDIR_WAIT_RTN,
  BACKUP_ROOT_NLST_PASV, BACKUP_ROOT_NLST_WAIT, BACKUP_ROOT_NLST_DATA,
  MK_BACKUP_DIR
}; */

/* There is probably a better way to do this, but each portion of the program
requires information from both the front end and backend- front ends use the
backend functions directly, while nwbackup calls the front end functions, but
needs the nwBackupParms data structure to send to the backend... */

#include "backend.h"
#include "frontend.h"


/* Stuff specific to the nwbackup program. */
typedef signed char (* nw_algorithm)(nwBackupParms *, char *, char *, char *);




#endif        //  #ifndef NWBACKUP_H

