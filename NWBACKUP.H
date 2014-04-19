#ifndef NWBACKUP_H
#define NWBACKUP_H

#include <stdint.h>
#include <stdio.h>
//#include <direct.h>

//Variables
// Global vars and flags
typedef struct mTCPBackupParms{
  char BackupServer[80];
  char Username[80];
  char Password[80];
  char RootDir[80];
  char BackupMode[8];
  char TempDir[80];
  char * backupDirName;
}nwBackupParms;

typedef enum mTCPReturnCodes{
	SUCCESS, ALLOCATION_FAILURE	
}nwBackupCodes;


//Other stuff
enum progMode{
  BACKUP = 1, RESTORE
};

enum pollStatus{
  READ_ERROR=-1, NO_CHARS_RECEIVED, CHARS_RECEIVED
};

enum backupState{
  UNINITIALIZED, OPEN_SERVER, CLOSE_SERVER, CLOSE_SERVER_WAIT_RTN, 
  SEND_USER, SEND_USER_WAIT_RTN, SEND_PASS, SEND_PASS_WAIT_RTN, CHDIR_BACKUP_ROOT, 
  CHDIR_ROOT_WAIT_RTN, GET_TREE_SEND_CHDIR, SEND_CHDIR_WAIT_RTN, 
  BACKUP_ROOT_NLST_PASV, BACKUP_ROOT_NLST_WAIT, BACKUP_ROOT_NLST_DATA,
  MK_BACKUP_DIR
};

nwBackupCodes initRemote(nwBackupParms *);
void closeRemote();


#endif        //  #ifndef NWBACKUP_H

