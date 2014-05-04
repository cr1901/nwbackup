#ifndef BACKEND_H
#define BACKEND_H

#include <stdint.h>

/* Platform-dependent function declarations */
typedef struct mTCPBackupParms {
  char BackupServer[80];
  char Username[80];
  char Password[80];
  char RootDir[80];
  char BackupMode[8];
  //char TempDir[80];
  char backupDirName[80];
} nwBackupParms;


//difference between retryCount and timeout? Honestly, I'm not sure yet :P.
typedef enum mTCPReturnCodes {
  SUCCESS, ALLOCATION_FAILURE, TARGET_INACCESSIBLE,
  REMOTE_CLOSED_EARLY, ACCESS_DENIED, REMOTE_TIMEOUT,
  TARGET_MKDIR_FAILED, TARGET_NOEXIST, TARGET_EXIST, USER_SIGNAL,
  SYNC_ERROR, TARGET_BUSY, TARGET_REQUEST_MORE_DATA, TARGET_STILL_HAS_DATA
} nwBackupCodes;

typedef void * nwFileHandle;

/* Platform-independent function declarations. */
//It is up to the client program to make sure that the filename and directory is
//well formed for the remote.
nwBackupCodes initRemote(nwBackupParms *);
nwBackupCodes mkDirRemote(char * dirName);
nwBackupCodes chDirRemote(char * dirName);
nwBackupCodes openRemoteFile(nwFileHandle fp, char * remoteFileName, int8_t mode);
nwBackupCodes sendDataRemote(nwFileHandle fp, uint8_t * buffer, uint16_t bufSize, uint16_t * charsSent);
nwBackupCodes rcvDataRemote(nwFileHandle fp, uint8_t * buffer, uint16_t bufSize, uint16_t * charsRcvd);
nwBackupCodes closeRemoteFile(nwFileHandle fp);

//nwBackupCodes openAndSendFile(char * remoteFileName, char * localFileName);
//nwBackupCodes writeAndReceiveFile(char * remoteFileName, char * localFileName);
void abortXfer();
void closeRemote();


//Helper functions
void startTimer();
uint32_t getElapsedTime();
uint32_t computeRate( uint32_t bytes, uint32_t elapsed );


#endif        //  #ifndef BACKEND_H

