//C headers (C++ format)
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>

//DOS headers
#include <bios.h>
#include <dos.h>
#include <io.h>

//mTCP headers
#include "types.h"
#include "utils.h"
#include "packet.h"
#include "arp.h"
#include "udp.h"
#include "dns.h"
#include "tcp.h"
#include "tcpsockm.h"

//C headers
extern "C"
{
#include "nwbackup.h"
}

#define _NL_ "\r\n"
#define CTRL_RECV_BUFFER_SIZE 512
#define CMD_RESPONSE_BUFFER_SIZE 512 // Sent from CTRL_RECV buffer to here
#define CMD_REQUEST_BUFFER_SIZE 512 // Send to Control Socket.
#define FILE_BUFFER_SIZE 8192 //Used for tx/rx of data socket

//Globals
char     PortString[40] = {'\0'},PasvString[40] = {'\0'}; //make union?
static uint16_t ServerPort, CtrlPort, DataPort, NextDataPort, PassPort; // Only used when we are a client.
static int8_t   PasvOrPort = 0; //FTP Pasv Xfer = 1, Port Xfer = 0

//Sockets
static TcpSocket *ctrlSocket, *dataSocket = NULL, *listenSocket;

//Buffers
static uint8_t * cmdResponseBuffer;
static char * cmdSendBuffer; //fprintf expects char *
//uint16_t  inBufIndex=0;          // Index to next char to fill
static uint8_t * fileBuffer;


//Structs
// Data structure used to send packets.  1460 is the maximum payload for
// a normal TCP/IP packet with no options.  The sender has to remember that
// the other side might have an MSS less than this, or that the local MTU
// might be smaller than 1500.

typedef struct {
  TcpBuffer b;
  uint8_t data[1460];
} DataBuf;

// Traps
// Trap Ctrl-Break and Ctrl-C so that we can unhook the timer interrupt
// and shutdown cleanly.
volatile uint8_t CtrlBreakDetected = 0;
void ( __interrupt __far *oldCtrlBreakHandler)( );
void __interrupt __far ctrlBreakHandler( ) {
  CtrlBreakDetected = 1;
}
void __interrupt __far ctrlCHandler( ) {
  // Do Nothing
}




//enums
enum initState {
  OPEN_SERVER, SEND_USER, SEND_PASS, CHDIR_BACKUP_ROOT
};

//Function declarations
//void pollSocket(TcpSocket *s, uint32_t timeout);
/* int16_t pollSocketAndGetRc( TcpSocket *s, uint8_t * buffer, uint16_t currOffset, \
  uint16_t maxSize, uint32_t timeout, uint16_t * ftpRc ); */
uint16_t pollSocket( TcpSocket *s, uint8_t * buffer, uint16_t currOffset, uint16_t maxSize, uint32_t timeout );
void readConfigParms( nwBackupParms * );
int8_t parsePASVResponse( char *pos, IpAddr_t * pasvAddr, uint16_t * pasv_port );
int8_t openDataSocketPASV(TcpSocket ** s, uint16_t bufSize, IpAddr_t pasvAddr, uint16_t pasvPort);
static void closeAndFreeDataSocket( TcpSocket * s );
static void shutdown(int rc);

//Custom calling convention functions
extern unsigned char wherex( void );
#pragma aux wherex = \
  "push bp"          \
  "mov ah, 3h"       \
  "mov bh, 0h"       \
  "int 10h"          \
  "pop bp"           \
  modify [ ax bx cx dx ] \
  value [ dl ];

extern unsigned char wherey( void );
#pragma aux wherey = \
  "push bp"          \
  "mov ah, 3h"       \
  "mov bh, 0h"       \
  "int 10h"          \
  "pop bp"           \
  modify [ ax bx cx dx ] \
  value [ dh ];


//==============================================================================
//NWBACKUP functions
//==============================================================================
//Note: To make things simple for now, let's not worry about returning the proper
//codes... if TCP fails, we can go ahead and exit.

//nwBackupParms can be both read and set by BOTH the application proper and
//the init function, to pass data between each other. There may be times
//that the app needs information from the backup driver, so parms is a means
//to return information while reducing coupling.
nwBackupCodes initRemote(nwBackupParms * parms) {
  enum initState currState = OPEN_SERVER;
  int16_t recvRc;
  uint8_t retryCount = 0; //retry count only reflects waiting extra time for data.
  //It does not actually resend a command to the server.
  int8_t socketClosedEarly = 0;
  char * afterReturnCode;
  uint16_t return_val;
  uint16_t currCmdLength;
  char return_code[30];


  if(Utils::parseEnv() != 0) {
    printf("Parsing Error!\n");
    exit(EXIT_FAILURE);
    //return PARSEENV_FAILURE;
  }

  // Allocate some data buffers
  cmdResponseBuffer = (uint8_t *) malloc(CMD_RESPONSE_BUFFER_SIZE);
  cmdSendBuffer = (char *) malloc(CMD_REQUEST_BUFFER_SIZE);
  fileBuffer = (uint8_t *) malloc(FILE_BUFFER_SIZE);

  if( cmdResponseBuffer == NULL || cmdSendBuffer == NULL || fileBuffer == NULL) {
    fprintf(stderr, "Initial buffer allocation failed!\n" );
    exit(EXIT_FAILURE);
    //return ALLOCATION_FAILURE;
  }

  readConfigParms(parms);

  // Initialize TCP/IP stack
  if ( Utils::initStack( 3, TCP_SOCKET_RING_SIZE ) ) {
    fprintf( stderr, "\nFailed to initialize TCP/IP - exiting\n" );
    exit(-1);
  }

  // From this point forward you have to call the shutdown( ) routine to
  // exit because we have the timer interrupt hooked.

  // Save off the oldCtrlBreakHander and put our own in.  Shutdown( ) will
  // restore the original handler for us.
  oldCtrlBreakHandler = getvect( 0x1b );
  setvect( 0x1b, ctrlBreakHandler);

  // Get the Ctrl-C interrupt too, but do nothing.  We actually want Ctrl-C
  // to be a legal character to send when in interactive mode.
  setvect( 0x23, ctrlCHandler);

  ServerPort = 21;
  CtrlPort = 1024 + ( rand() % 1024 );
  DataPort = 4096 + ( rand() % 20480 );
  NextDataPort = DataPort + 1;

  int8_t rc;
  uint8_t done = 0;
  //IpAddr_t serverAddr = {0};

  fprintf( stderr, "Resolving server address - press Ctrl-Break to abort\n\n" );

  IpAddr_t serverAddr;

  // Resolve the name and definitely send the request
  int8_t rc2 = Dns::resolve( parms->BackupServer, serverAddr, 1 );
  if ( rc2 < 0 ) {
    fprintf( stderr, "Error resolving server\n" );
    shutdown( -1 );
  }


  while ( !done ) {

    if ( CtrlBreakDetected ) {
      break;
    }
    if ( !Dns::isQueryPending( ) ) {
      break;
    }

    PACKET_PROCESS_SINGLE;
    Arp::driveArp( );
    Tcp::drivePackets( );
    Dns::drivePendingQuery( );

  }

  // Query is no longer pending or we bailed out of the loop.
  rc2 = Dns::resolve( parms->BackupServer, serverAddr, 0 );

  if ( rc2 != 0 ) {
    fprintf( stderr, "Error resolving server\n" );
    shutdown( -1 );
  }

  ctrlSocket = TcpSocketMgr::getSocket( );
  listenSocket = TcpSocketMgr::getSocket( );

  ctrlSocket->setRecvBuffer( CTRL_RECV_BUFFER_SIZE );
  //listenSocket->setRecvBuffer( LISTEN_RECV_BUFFER_SIZE );

  fprintf( stderr, "Server resolved to %d.%d.%d.%d - connecting\n\n",
           serverAddr[0], serverAddr[1], serverAddr[2], serverAddr[3] );

  // Non-blocking connect.  Wait 10 seconds before giving up.
  rc = ctrlSocket->connect( CtrlPort, serverAddr, ServerPort, 10000 );

  if ( rc != 0 ) {
    fprintf( stderr, "Socket open failed\n" );
    shutdown( -1 );
  }
  else {
    fprintf( stderr, "Connected!\n\n" );
    currState = OPEN_SERVER;
  }

  do {
    /* If currOffset (third arg) is 0, recvRc reflects the number of characters
    received. Otherwise, it reflects the index of the next free char
    in the buffer. */
    recvRc = pollSocket(ctrlSocket, cmdResponseBuffer, 0, CMD_RESPONSE_BUFFER_SIZE, 300);
    /* SAMPLE.CPP checks socket before receive. FTP.CPP checks socket after
    receive. Either seems acceptable. */
    if ( ctrlSocket->isRemoteClosed( ) ) {
      socketClosedEarly = 1;
    }


    if(recvRc < 0) {
      fprintf( stderr, "\nError reading from socket.\nBackup aborted." );
      return REMOTE_CONNECTION_DIED;
    }
    else if(recvRc == 0) {
      retryCount++;
    }
    else {
      write( 1, cmdResponseBuffer, recvRc );

      // Do check for return value- although 'unexpected states may be
      //sufficient error 'correction'. afterReturnCode points after the initial
      //return code :P.
      afterReturnCode = Utils::getNextToken( (char *) cmdResponseBuffer, return_code, 30 );
      return_val = atoi(return_code); //Should we expect multiline responses here?

      //We should at least read and write from the socket, even if it closed,
      //just so the data isn't lost.
      if(socketClosedEarly) {
        break;
      }

      switch(currState) {
      case OPEN_SERVER:
        if(return_val != 220) {
          fprintf(stderr, "\nTarget is not accessible.\nBackup aborted.");
          return TARGET_INACCESSIBLE;
        }
        currCmdLength = sprintf(cmdSendBuffer, "%s %s" _NL_, "USER", parms->Username);
        ctrlSocket->send( (uint8_t *) cmdSendBuffer, currCmdLength);
        currState = SEND_USER;
        retryCount = 0;
        break;
      case SEND_USER:
        if(return_val != 331) {
          //fprintf(stderr, "Return val: %d", return_val);
          fprintf( stderr, "\nServer says the username is bad. \nNot continuing." );
          return ACCESS_DENIED;
        }
        currCmdLength = sprintf(cmdSendBuffer, "%s %s" _NL_, "PASS", parms->Password);
        ctrlSocket->send( (uint8_t *) cmdSendBuffer, currCmdLength);
        currState = SEND_PASS;
        retryCount = 0;
        break;
      case SEND_PASS:
        if(return_val != 230) {
          //fprintf(stderr, "Return val: %d != 230", return_val);
          fprintf(stderr, "\nServer says the password is bad. \nNot continuing." );
          return ACCESS_DENIED;
        }
        currCmdLength = sprintf(cmdSendBuffer, "%s %s" _NL_, "CWD", parms->RootDir);
        ctrlSocket->send( (uint8_t *) cmdSendBuffer, currCmdLength);
        currState = CHDIR_BACKUP_ROOT;
        retryCount = 0;
        break;
      case CHDIR_BACKUP_ROOT:
        if(return_val != 250) {
          return TARGET_INACCESSIBLE;
        }
        else {   // We are good if we get here, and can continue.
          done = 1;
        }
        break;
      default:
        retryCount++; //Precaution
        break;
      }
    }
  }
  while((retryCount < 3) && !done && !socketClosedEarly);

  //Is RETRY_COUNT_EXCEEDED necessary?
  if(retryCount >= 3) {
    return RETRY_COUNT_EXCEEDED;
  }

  if(socketClosedEarly) {
    fprintf( stderr, "\nSocket closed during initialization.\nBackup aborted." );
    return REMOTE_CLOSED_EARLY;
  }
  return SUCCESS;
}


nwBackupCodes mkDirRemote(char * dirName) {
  int16_t recvRc;
  uint8_t retryCount = 0; //retry count only reflects waiting extra time for data.
  //It does not actually resend a command to the server.
  int8_t socketClosedEarly = 0;
  char * afterReturnCode;
  uint16_t return_val;
  uint16_t currCmdLength;
  char return_code[30];
  uint8_t mkdirState = 0; //0- pre_mkdir, mkdir_sent

  /* If currOffset (third arg) is 0, recvRc reflects the number of characters
  received. Otherwise, it reflects the index of the next free char
  in the buffer. */
  recvRc = pollSocket(ctrlSocket, cmdResponseBuffer, 0, CMD_RESPONSE_BUFFER_SIZE, 300);
  /* SAMPLE.CPP checks socket before receive. FTP.CPP checks socket after
  receive. Either seems acceptable. */
  if ( ctrlSocket->isRemoteClosed( ) ) {
    return REMOTE_CLOSED_EARLY;
  }


  if(recvRc < 0) {
    fprintf( stderr, "\nError reading from socket.\nBackup aborted." );
    return REMOTE_CONNECTION_DIED;
  }
  else if(recvRc > 0) {   //State 0 doesn't expect data.
    write( 1, cmdResponseBuffer, recvRc );
    afterReturnCode = Utils::getNextToken( (char *) cmdResponseBuffer, return_code, 30 );
    return_val = atoi(return_code); //Should we expect multiline responses here?
    fprintf(stderr, "\nWarning: Data was unexpected at this time...\n");
    //Handle it later
  }

  currCmdLength = sprintf(cmdSendBuffer, "%s %s" _NL_, "MKD", dirName);
  ctrlSocket->send( (uint8_t *) cmdSendBuffer, currCmdLength);

  //Is retry count necessary?
  uint8_t done = 0;
  do {
    recvRc = pollSocket(ctrlSocket, cmdResponseBuffer, 0, CMD_RESPONSE_BUFFER_SIZE, 300);
    /* SAMPLE.CPP checks socket before receive. FTP.CPP checks socket after
    receive. Either seems acceptable. */
    if ( ctrlSocket->isRemoteClosed( ) ) {
      socketClosedEarly = 1;
    }

    if(recvRc < 0) {
      fprintf( stderr, "\nError reading from socket.\nBackup aborted." );
      return REMOTE_CONNECTION_DIED;
    }
    else if(recvRc == 0) {
      retryCount++;
    }
    else {
      write( 1, cmdResponseBuffer, recvRc );
      afterReturnCode = Utils::getNextToken( (char *) cmdResponseBuffer, return_code, 30 );
      return_val = atoi(return_code); //Should we expect multiline responses here
      done = 0;
    }
  }
  while(retryCount < 3 && !done);

  if(socketClosedEarly) {
    return REMOTE_CLOSED_EARLY;
  }

  if(return_val == 257) {
    return SUCCESS;
  }
  else if(return_val == 550) {
    return TARGET_EXIST;
  }
  else {
    return TARGET_MKDIR_FAILED;
  }
}


nwBackupCodes chDirRemote(char * dirName) {
  int16_t recvRc;
  uint8_t retryCount = 0; //retry count only reflects waiting extra time for data.
  //It does not actually resend a command to the server.
  int8_t socketClosedEarly = 0;
  char * afterReturnCode;
  uint16_t return_val;
  uint16_t currCmdLength;
  char return_code[30];
  uint8_t mkdirState = 0; //0- pre_mkdir, mkdir_sent

  /* If currOffset (third arg) is 0, recvRc reflects the number of characters
  received. Otherwise, it reflects the index of the next free char
  in the buffer. */
  recvRc = pollSocket(ctrlSocket, cmdResponseBuffer, 0, CMD_RESPONSE_BUFFER_SIZE, 300);
  /* SAMPLE.CPP checks socket before receive. FTP.CPP checks socket after
  receive. Either seems acceptable. */
  if ( ctrlSocket->isRemoteClosed( ) ) {
    return REMOTE_CLOSED_EARLY;
  }


  if(recvRc < 0) {
    fprintf( stderr, "\nError reading from socket.\nBackup aborted." );
    return REMOTE_CONNECTION_DIED;
  }
  else if(recvRc > 0) {   //State 0 doesn't expect data.
    write( 1, cmdResponseBuffer, recvRc );
    afterReturnCode = Utils::getNextToken( (char *) cmdResponseBuffer, return_code, 30 );
    return_val = atoi(return_code); //Should we expect multiline responses here?
    fprintf(stderr, "\nWarning: Data was unexpected at this time...\n");
    //Handle it later
  }

  currCmdLength = sprintf(cmdSendBuffer, "%s %s" _NL_, "CWD", dirName);
  ctrlSocket->send( (uint8_t *) cmdSendBuffer, currCmdLength);

  //Is retry count necessary?
  uint8_t done = 0;
  do {
    recvRc = pollSocket(ctrlSocket, cmdResponseBuffer, 0, CMD_RESPONSE_BUFFER_SIZE, 300);
    /* SAMPLE.CPP checks socket before receive. FTP.CPP checks socket after
    receive. Either seems acceptable. */
    if ( ctrlSocket->isRemoteClosed( ) ) {
      socketClosedEarly = 1;
    }

    if(recvRc < 0) {
      fprintf( stderr, "\nError reading from socket.\nBackup aborted." );
      return REMOTE_CONNECTION_DIED;
    }
    else if(recvRc == 0) {
      retryCount++;
    }
    else {
      write( 1, cmdResponseBuffer, recvRc );
      afterReturnCode = Utils::getNextToken( (char *) cmdResponseBuffer, return_code, 30 );
      return_val = atoi(return_code); //Should we expect multiline responses here
      done = 0;
    }
  }
  while(retryCount < 3 && !done);

  if(socketClosedEarly) {
    return REMOTE_CLOSED_EARLY;
  }

  if(return_val != 250) {
    return TARGET_INACCESSIBLE;
  }
  else {   // We are good if we get here, and can continue.
    return SUCCESS;
  }
  /* if(return_val == 257) {
    return SUCCESS;
  } else if(return_val == 550) {
    return TARGET_DIR_EXISTS;
  } else {
    return TARGET_MKDIR_FAILED;
  } */
}


//It is the backup driver's responsibility to open and close files.
//This routine is modified from mTCP FTP...
nwBackupCodes openAndSendFile(char * remoteFileName, char * localFileName) {
  int8_t pasvDataRc;
  FILE * sourceFile;
  int8_t finalRc;
  int8_t rc;
  int16_t recvRc;
  uint8_t done = 0;
  int8_t socketClosedEarly = 0;
  char * afterReturnCode;
  uint16_t return_val;
  uint16_t currCmdLength;
  uint8_t retryCount = 0;
  char return_code[30];
  uint16_t pasv_port;
  IpAddr_t pasvAddr;


  //Send PASV Command
  currCmdLength = sprintf(cmdSendBuffer, "%s %s" _NL_, "PASV", NULL);
  ctrlSocket->send((uint8_t *) cmdSendBuffer, currCmdLength);
  do {
    recvRc = pollSocket(ctrlSocket, cmdResponseBuffer, 0, CMD_RESPONSE_BUFFER_SIZE, 300);
    /* SAMPLE.CPP checks socket before receive. FTP.CPP checks socket after
    receive. Either seems acceptable. */
    if ( ctrlSocket->isRemoteClosed( ) ) {
      socketClosedEarly = 1;
    }

    if(recvRc < 0) {
      fprintf( stderr, "\nError reading from socket.\nBackup aborted." );
      return REMOTE_CONNECTION_DIED;
    }
    else if(recvRc == 0) {
      retryCount++;
    }
    else {
      write( 1, cmdResponseBuffer, recvRc );
      afterReturnCode = Utils::getNextToken( (char *) cmdResponseBuffer, return_code, 30 );
      return_val = atoi(return_code); //Should we expect multiline responses here
      done = 1;
    }
  }
  while(retryCount < 3 && !done);

  if(retryCount >= 3) {
    return RETRY_COUNT_EXCEEDED;
  }

  if(socketClosedEarly) {
    fprintf( stderr, "\nSocket closed during initialization.\nBackup aborted." );
    return REMOTE_CLOSED_EARLY;
  }


  //Parse PASV Response and open a data socket
  if(parsePASVResponse(afterReturnCode, &pasvAddr, &pasv_port)) {
    fprintf(stderr, "Attempt to get passive port failed.\n Not continuing. \n");
    return ACCESS_DENIED;
  }
  else {
    fprintf(stderr, "Pasv Address: %d,%d,%d,%d Pasv Port: %hi\n", pasvAddr[0],
            pasvAddr[1], pasvAddr[2], pasvAddr[3], pasv_port);
    pasvDataRc = openDataSocketPASV(&dataSocket, FILE_BUFFER_SIZE, pasvAddr, pasv_port);
    if(pasvDataRc == 0) {
      //fprintf(stderr, "Passive command okay...\n");
      currCmdLength = sprintf(cmdSendBuffer, "%s %s" _NL_, "STOR", remoteFileName);
      ctrlSocket->send( (uint8_t *) cmdSendBuffer, currCmdLength);
    }
    else if(pasvDataRc == -1) {
      fprintf(stderr, "Attempt to allocate data socket failed.\n Not continuing. \n");
      return ALLOCATION_FAILURE;
    }
    else {
      fprintf(stderr, "Attempt to connect data socket failed.\n Not continuing. \n");
      return REMOTE_CONNECTION_DIED;
    }
  }

  //Needed?
  if ( dataSocket == NULL ) {
    return ALLOCATION_FAILURE;
  }


  //Wait for okay to send data.
  done = 0;
  do {
    recvRc = pollSocket(ctrlSocket, cmdResponseBuffer, 0, CMD_RESPONSE_BUFFER_SIZE, 300);
    /* SAMPLE.CPP checks socket before receive. FTP.CPP checks socket after
    receive. Either seems acceptable. */
    if ( ctrlSocket->isRemoteClosed( ) ) {
      socketClosedEarly = 1;
    }

    if(recvRc < 0) {
      fprintf( stderr, "\nError reading from socket.\nBackup aborted." );
      return REMOTE_CONNECTION_DIED;
    }
    else if(recvRc == 0) {
      retryCount++;
    }
    else {
      write( 1, cmdResponseBuffer, recvRc );
      afterReturnCode = Utils::getNextToken( (char *) cmdResponseBuffer, return_code, 30 );
      return_val = atoi(return_code); //Should we expect multiline responses here
      done = 1;
    }
  }
  while(retryCount < 3 && !done);

  if(retryCount >= 3) {
    fprintf(stderr, "Retry count exceeded...\n");
    return RETRY_COUNT_EXCEEDED;
  }
  if(socketClosedEarly) {
    fprintf( stderr, "\nSocket closed during initialization.\nBackup aborted." );
    return REMOTE_CLOSED_EARLY;
  }
  if(return_val != 150) {
    return TARGET_INACCESSIBLE;
  }


  //Open File
  sourceFile = fopen( localFileName, "rb" );
  if ( sourceFile == NULL ) {
    printf( "Local error opening file %s for reading\n", sourceFile);
    return LOCAL_FILE_NOEXIST;
  }
  //setvbuf( sourceFile, (char *)FileBuffer, _IOFBF, SendFileBufSize );
  setvbuf( sourceFile, (char *) fileBuffer, _IOFBF, FILE_BUFFER_SIZE);

  clockTicks_t startTicks = TIMER_GET_CURRENT( );

  uint32_t totalBytesSent = 0;

  //int x = wherex( );
  //int y = wherey( );

  uint8_t update = 0;

  DataBuf *buf = NULL;


  // The while loop exits when done gets set.  After that, the close call will
  // push out any remaining queued packets.  We need to be careful with the file
  // read-ahead; that buffer has to get enqueued before we can leave the loop.

  done = 0;  // 1=done, 2=socket error, 3=local abort, 4=file error

  while ( !done ) {

    if ( CtrlBreakDetected ) {
      done = 3;
      break;
    }


    // Try to pick up and process as many returning ACK packets as
    // possible.  This makes room for new packets going out.

    PACKET_PROCESS_MULT(5);
    Arp::driveArp( );
    Tcp::drivePackets( );


    if ( dataSocket->isRemoteClosed( ) ) {
      done = 2;
      break;
    }


    // Don't bother trying to send packets if we don't have room
    // for new packets in the outgoing queue.

    while ( dataSocket->outgoing.hasRoom( ) ) {

      if ( CtrlBreakDetected ) {
        done = 3;
        break;
      }

      // In an ideal world we have already read ahead in the file and filled
      // a buffer so that it is ready to send immediately.  If a buffer is
      // not ready then read more data into one now.

      if ( buf == NULL ) {

        buf = (DataBuf *)TcpBuffer::getXmitBuf( );

        if ( buf == NULL ) {

          // Could not get an outgoing buffer.  It is unlikely that all of them
          // are in use but if they are, just break out of this loop without
          // doing anything and it will retry on the next pass.

          break;  // Exit the inner while loop

        }


        // Read file data directly into one of our outgoing TCP buffers.
        //
        // maxEnqueueSize is the lesser of the other side's MSS and our MSS.
        // (Both of those take MTU into account.)

        uint16_t bytesToSend = fread( buf->data, 1, dataSocket->maxEnqueueSize, sourceFile );

        if ( bytesToSend == 0 ) {

          TcpBuffer::returnXmitBuf( (TcpBuffer *)buf );

          if ( feof( sourceFile ) ) {

            // No more data to read and all of the previous data was already enqueued.
            // (Buf was null when we came in here.)  Set done = 1 so that we break the
            // outer while loop.

            done = 1;

          }
          else if ( ferror( sourceFile ) ) {

            // Error reading the file - abort
            done = 4;

          }

          // Hmm - nothing read, but not end of file or an error.
          // This shouldn't happen, but it's a legal case.

          break;  // Exit the inner while loop

        }

        // We have a buffer and were able to read from the file.  Set the
        // length of the buffer.

        buf->b.dataLen = bytesToSend;
      }


      // If you get to this point we have a buffer with data in it.
      // We have outgoing room - the while loop enforces that.  So enqueue
      // should only fail if the socket closes early.

      if ( dataSocket->enqueue( &buf->b ) ) {
        done = 2;
        break;
      }

      totalBytesSent += buf->b.dataLen;

      PACKET_PROCESS_MULT(5);
      Tcp::drivePackets( );


      /* if ( update == 0 ) {
        myCprintf( x, y, "Bytes transferred: %lu", totalBytesSent );
      } */
      update = (update + 1) & 0x0F;

      // We no longer hold a pointer to that buffer.  Set our copy to
      // NULL so that we don't accidentally touch it, and also to
      // indicate that we need to get another one.

      buf = NULL;

    } // End inner while loop



    // If you are here then you might have a buffer with data to send in it
    // already that could not be sent because there was no room in the
    // outgoing ring queue, or you sent everything you had and need to
    // read more.  (The while loop is not guaranteed to run, so you could
    // have pre-read a buffer, not been able to enter the while loop, and
    // gotten here again.)

    if ( buf == NULL ) {

      // Need to read more of the file.  Do this here to take advantage of
      // the time the other packets are on the wire.  If you can't get
      // another outgoing buffer then do nothing; we'll try again the next
      // time around.

      buf = (DataBuf *)TcpBuffer::getXmitBuf( );

      if ( buf != NULL ) {

        uint16_t bytesToSend = fread( buf->data, 1, dataSocket->maxEnqueueSize, sourceFile );

        if ( bytesToSend == 0 ) {

          TcpBuffer::returnXmitBuf( (TcpBuffer *)buf );

          if ( feof( sourceFile ) ) {

            // No more data to read and all of the previous data was already enqueued.
            // (Buf was null when we came in here.)  Set done = 1 so that we break the
            // outer while loop.

            done = 1;

          }
          else if ( ferror( sourceFile ) ) {
            // Error reading the file - abort
            done = 4;
          }

          // Hmm - nothing read, but not end of file or an error.
          // This shouldn't happen, but it's a legal case.

        }
        else {
          buf->b.dataLen = bytesToSend;
        }

      }

    } // end buf == NULL so need to try to read ahead in the file

  } // end main while


  //gotoxy( x, y );

  if ( done == 2 ) {
    printf( "Data socket closed early - close reason: %d\n", dataSocket->getCloseReason( ) );
  }
  else if ( done == 3 ) {
    puts( "Xfer aborted due to Ctrl-Break" );
  }
  else if ( done == 4 ) {
    puts( "Xfer aborted due to filesystem error" );
  }

  //If this was a standard xfer, we can close the file now!
  fclose( sourceFile );

  closeAndFreeDataSocket(dataSocket);

  uint32_t elapsed = Timer_diff( startTicks, TIMER_GET_CURRENT( ) ) * TIMER_TICK_LEN;

  //uint32_t rate = computeRate( totalBytesSent, elapsed );

  //printf( "%lu bytes sent in %lu.%03lu seconds (%lu.%03lu KBytes/sec)\n",
  //        totalBytesSent, (elapsed/1000), (elapsed%1000), (rate/1024), (rate%1024) );

  finalRc = done;
  done = 0;
  //Dummy read, don't bother checking return codes.
  do {
    recvRc = pollSocket(ctrlSocket, cmdResponseBuffer, 0, CMD_RESPONSE_BUFFER_SIZE, 300);
    /* SAMPLE.CPP checks socket before receive. FTP.CPP checks socket after
    receive. Either seems acceptable. */
    if ( ctrlSocket->isRemoteClosed( ) ) {
      return REMOTE_CLOSED_EARLY;
    }

    if(recvRc < 0) {
      fprintf( stderr, "\nError reading from socket.\nBackup aborted." );
      return REMOTE_CONNECTION_DIED;
    }
    else if(recvRc == 0) {
      retryCount++;
      if(retryCount >= 3) {
        return RETRY_COUNT_EXCEEDED;
      }

    }
    else {
      write( 1, cmdResponseBuffer, recvRc );
      afterReturnCode = Utils::getNextToken( (char *) cmdResponseBuffer, return_code, 30 );
      return_val = atoi(return_code); //Should we expect multiline responses here
      done = 1;
    }
  }
  while(!done);

  switch(finalRc) {
  case 1:
    return SUCCESS;
  case 2:
    return REMOTE_CLOSED_EARLY;
  case 3:
    return USER_SIGNAL;
  case 4:
    return LOCAL_FILE_ERROR;
  default:
    return SUCCESS;
  }
}




void closeRemote() {
  uint16_t currCmdLength;
  int16_t recvRc;

  if(!ctrlSocket->isRemoteClosed()) {
    currCmdLength = sprintf(cmdSendBuffer, "%s %s" _NL_, "QUIT", NULL);
    ctrlSocket->send( (uint8_t *) cmdSendBuffer, currCmdLength);


    while(!ctrlSocket->isRemoteClosed()) {
      PACKET_PROCESS_SINGLE;
      Arp::driveArp( );
      Tcp::drivePackets( );
    }
    recvRc = ctrlSocket->recv( cmdResponseBuffer, CMD_RESPONSE_BUFFER_SIZE );

    if(recvRc > 0) {
      write( 1, cmdResponseBuffer, recvRc );
    }
  }


  ctrlSocket->close( );
  TcpSocketMgr::freeSocket( ctrlSocket );
  setvect( 0x1b, oldCtrlBreakHandler);

  Utils::endStack( );
  Utils::dumpStats( stderr );
  fclose( TrcStream );
  //exit( rc );
  //return SUCCESS;
}


//===============================================================================
//Other functions- mostly from mTCP files
//===============================================================================
// Blatantly "stolen" from IRC.CPP! Modified slightly.
void readConfigParms( nwBackupParms * parms ) {
#define NUM_PARAMS 5
  const static char *ParmNames[]  = { "NWBACKUP_SERVER", "NWBACKUP_USERNAME",
                                      "NWBACKUP_PASSWORD", "NWBACKUP_DIR", "NWBACKUP_FORMAT"
                                    };
  const static unsigned char ParmLens[] = {80, 80, 80, 80, 8};

  char * ParmStrs[NUM_PARAMS];
  char tmp[80];
  uint16_t tmpVal;

  ParmStrs[0] = (parms->BackupServer);
  ParmStrs[1] = (parms->Username);
  ParmStrs[2] = (parms->Password);
  ParmStrs[3] = (parms->RootDir);
  ParmStrs[4] = (parms->BackupMode);

  Utils::openCfgFile( );

  for ( uint8_t i=0; i < NUM_PARAMS; i++ ) {
    Utils::getAppValue( ParmNames[i], ParmStrs[i], ParmLens[i] );
    if ( *ParmStrs[i] == 0 ) {
      printf( "Need to set %s in the config file\n", ParmNames[i] );
      exit(1);
    }
    //printf("%s\n", ParmStrs[i]);
  }

  /*  Utils::getAppValue( "nwbackup_ftp_mode", tmp, 10 );
  if ( *tmp != 0 ) {
    BsLinesChannel = atoi( tmp );
  } */

  Utils::closeCfgFile( );

}


//Stolen/modified from FTP.CPP!
int8_t parsePASVResponse( char *pos, IpAddr_t * PasvAddr, uint16_t * PasvPort ) {
  // Find first digit.  This should be the start of the IP addr and port
  // string.
  //Of course, this assumes the server return code was already parsed.- W. Jones

  uint8_t found = 0;
  while ( *pos ) {
    if ( isdigit( *pos ) ) {
      found = 1;
      break;
    }
    pos++;
  }



  if ( !found ) {
    (* PasvAddr)[0] = 0;
    (* PasvAddr)[1] = 0;
    (* PasvAddr)[2] = 0;
    (* PasvAddr)[3] = 0;
    PasvPort = 0;
    return -1;
  }


  uint16_t t0, t1, t2, t3, t4, t5;
  int rc = sscanf( pos, "%d,%d,%d,%d,%d,%d",
                   &t0, &t1, &t2, &t3, &t4, &t5 );
  if ( rc != 6 ) {
    (* PasvAddr)[0] = 0;
    (* PasvAddr)[1] = 0;
    (* PasvAddr)[2] = 0;
    (* PasvAddr)[3] = 0;
    PasvPort = 0;
    return -1;
  }

  (* PasvAddr)[0] = t0;
  (* PasvAddr)[1] = t1;
  (* PasvAddr)[2] = t2;
  (* PasvAddr)[3] = t3;
  (* PasvPort) = (t4<<8) + t5;

  return 0;
}


//Blatantly stolen/modified from FTP.CPP
static void closeAndFreeDataSocket( TcpSocket * s ) {
  s->close( );
  TcpSocketMgr::freeSocket( s );
  s = NULL;
}

//Blatantly stolen/modified from FTP.CPP!
// Timeout is specified in milliseconds
uint16_t pollSocket( TcpSocket *s, uint8_t * buffer, uint16_t currOffset, uint16_t maxSize, uint32_t timeout ) {

  uint16_t recvRc = 0;
  //return 0; //Bytes read while polling
  clockTicks_t startTime = TIMER_GET_CURRENT( );

  while ( 1 ) {

    PACKET_PROCESS_SINGLE;
    Arp::driveArp( );
    Tcp::drivePackets( );

    int rc = s->recv( buffer + currOffset, (maxSize - currOffset) );
    if ( rc > -1 ) {
      currOffset += rc;
    }

    //processSocketInput( ); Defer processing

    uint32_t t_ms = Timer_diff( startTime, TIMER_GET_CURRENT( ) ) * TIMER_TICK_LEN;

    // Timeout?
    if ( t_ms > timeout ) {
      break;
    }

  }
  return currOffset;
}

//Based on mTCP FTP
int8_t openDataSocketPASV(TcpSocket ** s, uint16_t bufSize, IpAddr_t pasvAddr, uint16_t pasvPort) {

  (* s) = TcpSocketMgr::getSocket( );
  int8_t rc = (* s)->setRecvBuffer( bufSize );
  if ( rc ) {
    //fprintf(stderr, "Attempt to allocate data socket failed.\n Not continuing. \n");
    closeAndFreeDataSocket((* s));
    return -1;
  }

  uint16_t port = NextDataPort;
  NextDataPort = ( ( (NextDataPort-4096) + 1) % 20480 ) + 4096;

  rc = (* s)->connect( port, pasvAddr, pasvPort, 10000 );

  if ( rc != 0 ) {
    //fprintf(stderr, "Attempt to connect data socket failed.\n Not continuing. \n");
    closeAndFreeDataSocket((* s) );
    return -2;
  }

  return 0;
}


static void shutdown( int rc ) {

  setvect( 0x1b, oldCtrlBreakHandler);

  Utils::endStack( );
  Utils::dumpStats( stderr );
  fclose( TrcStream );
  exit( rc );
}



