#ifndef NWBACKUP_H
#define NWBACKUP_H

#include <stdint.h>
#include <stdio.h>
//#include <direct.h>

//Variables
// Global vars and flags
typedef struct mTCPBackupParms {
  char BackupServer[80];
  char Username[80];
  char Password[80];
  char RootDir[80];
  char BackupMode[8];
  char TempDir[80];
  char backupDirName[80];
} nwBackupParms;


//difference between retryCount and timeout? Honestly, I'm not sure yet :P.
typedef enum mTCPReturnCodes {
  SUCCESS, PARSEENV_FAILURE, ALLOCATION_FAILURE, TARGET_INACCESSIBLE,
  REMOTE_CLOSED_EARLY, REMOTE_CONNECTION_DIED, BAD_STATE, ACCESS_DENIED,
  RETRY_COUNT_EXCEEDED, REMOTE_TIMEOUT, TARGET_MKDIR_FAILED, TARGET_NOEXIST,
  TARGET_EXIST, LOCAL_FILE_NOEXIST, LOCAL_FILE_ERROR, USER_SIGNAL,
  TARGET_SEND_FAILED, SYNC_ERROR
} nwBackupCodes;


//Other stuff
enum progMode {
  BACKUP = 1, RESTORE
};

enum pollStatus {
  READ_ERROR=-1, NO_CHARS_RECEIVED, CHARS_RECEIVED
};

/* enum backupState{
  UNINITIALIZED, OPEN_SERVER, CLOSE_SERVER, CLOSE_SERVER_WAIT_RTN,
  SEND_USER, SEND_USER_WAIT_RTN, SEND_PASS, SEND_PASS_WAIT_RTN, CHDIR_BACKUP_ROOT,
  CHDIR_ROOT_WAIT_RTN, GET_TREE_SEND_CHDIR, SEND_CHDIR_WAIT_RTN,
  BACKUP_ROOT_NLST_PASV, BACKUP_ROOT_NLST_WAIT, BACKUP_ROOT_NLST_DATA,
  MK_BACKUP_DIR
}; */

//It is up to the client program to make sure that the filename and directory is
//well formed for the remote.
nwBackupCodes initRemote(nwBackupParms *);
nwBackupCodes mkDirRemote(char * dirName);
nwBackupCodes chDirRemote(char * dirName);
nwBackupCodes openAndSendFile(char * remoteFileName, char * localFileName);
void closeRemote();


#endif        //  #ifndef NWBACKUP_H

