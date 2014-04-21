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
}


nwBackupCodes mkDirRemote(char * dirName) {
  return SUCCESS;
}


nwBackupCodes chDirRemote(char * dirName) {
  return SUCCESS;
}


//It is the backup driver's responsibility to open and close files.
//This routine is modified from mTCP FTP...
nwBackupCodes openAndSendFile(char * remoteFileName, char * localFileName) {
  return SUCCESS;
}




void closeRemote() {
  return SUCCESS;
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
    (* PasvAddr)[0] = 0; (* PasvAddr)[1] = 0; (* PasvAddr)[2] = 0;
    (* PasvAddr)[3] = 0; PasvPort = 0;
    return -1;
  }

  uint16_t t0, t1, t2, t3, t4, t5;
  int rc = sscanf( pos, "%d,%d,%d,%d,%d,%d", &t0, &t1, &t2, &t3, &t4, &t5 );
  if ( rc != 6 ) {
    (* PasvAddr)[0] = 0; (* PasvAddr)[1] = 0; (* PasvAddr)[2] = 0;
    (* PasvAddr)[3] = 0; PasvPort = 0;
    return -1;
  }

  (* PasvAddr)[0] = t0; (* PasvAddr)[1] = t1; (* PasvAddr)[2] = t2;
  (* PasvAddr)[3] = t3; (* PasvPort) = (t4<<8) + t5;

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
uint16_t pollSocket( TcpSocket *s, uint8_t * buffer, uint16_t maxSize, uint32_t timeout ) {

  uint16_t recvRc = 0;
  //return 0; //Bytes read while polling
  clockTicks_t startTime = TIMER_GET_CURRENT( );

  while ( 1 ) {

    PACKET_PROCESS_SINGLE;
    Arp::driveArp( );
    Tcp::drivePackets( );

    int rc = s->recv( buffer + recvRc, (maxSize - recvRv) );
    if ( rc > -1 ) {
      recvRc += rc;
    }

    //processSocketInput( ); Defer processing

    uint32_t t_ms = Timer_diff( startTime, TIMER_GET_CURRENT( ) ) * TIMER_TICK_LEN;

    // Timeout?
    if ( t_ms > timeout ) {
      break;
    }

  }
  return recvRc;
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



