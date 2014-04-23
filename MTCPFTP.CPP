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
  OPEN_SERVER, SEND_USER, SEND_PASS, CHDIR_BACKUP_ROOT, ABORT_INIT
};

//Function declarations
//void pollSocket(TcpSocket *s, uint32_t timeout);
/* int16_t pollSocketAndGetRc( TcpSocket *s, uint8_t * buffer, uint16_t currOffset, \
  uint16_t maxSize, uint32_t timeout, uint16_t * ftpRc ); */
//uint16_t pollSocket( TcpSocket *s, uint8_t * buffer, uint16_t currOffset, uint16_t maxSize, uint32_t timeout );
int8_t getLineFromSocket( TcpSocket *s, uint8_t * buffer, uint16_t * charsRcvd, uint16_t maxSize, uint32_t timeout );
void readConfigParms( nwBackupParms * );
int8_t parsePASVResponse( char *pos, IpAddr_t * pasvAddr, uint16_t * pasv_port );
int8_t openDataSocketPASV(TcpSocket ** s, uint16_t bufSize, IpAddr_t pasvAddr, uint16_t pasvPort);
static void closeAndFreeDataSocket( TcpSocket * s );
static void shutdown(int rc);
int8_t findNextLineBreak(uint8_t ** buf, uint16_t * offsetFromEnd);

extern "C"
{
int8_t checkforFTPResponse(uint8_t * buf, uint16_t bufSiz, uint16_t * retVal, \
  char ** afterRetVal, int8_t * multiLine);
char * checkForNewline(char * buf, uint16_t bufSize);
}


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
  nwBackupCodes finalRc;
  enum initState currState = OPEN_SERVER;
  int16_t recvRc;
  uint8_t retryCount = 0; //retry count only reflects waiting extra time for data.
  //It does not actually resend a command to the server.
  int8_t socketClosedEarly = 0;
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
  
  //int8_t rc = 0;
  uint16_t charsRc = 0;
  uint16_t returnVal = 0;
  char * afterReturnCode;
  int8_t multiLineResponse = 0;
  //int8_t done = 0;
  
  do
  {
    //getCommandResponse function?
    rc = getLineFromSocket(ctrlSocket, cmdResponseBuffer, &charsRc, CMD_RESPONSE_BUFFER_SIZE, 10000);
    if(rc != -2)
    {
      write( 1, cmdResponseBuffer, charsRc);
    }
    else
    {
      finalRc = REMOTE_TIMEOUT;
      break;
    }
    
    //Poll socket will attempt to return a response that ends on a newline.
    //However, it is possible that multiple newlines were embedded in a
    //multiline response. Parse these in case the end of multiline was found.
    uint8_t * bufOffset = cmdResponseBuffer;
    uint16_t charsLeft = charsRc;
    int8_t lineBrkRc = 0;
    
    do{
      	rc = checkforFTPResponse(bufOffset, charsLeft, &returnVal, \
      	  &afterReturnCode, &multiLineResponse);
    }while(multiLineResponse && findNextLineBreak(&bufOffset, &charsLeft));
    
    fprintf(stderr, "FTP return code: %d\n", returnVal);
    fprintf(stderr, "checkforFTPResponse rc: %d\n", rc);
    
    /* if(multiLineResponse)
    {
      //fprintf(stderr, "multiline\n");
    } */
    switch(rc)
    {
      case 2:
      case 1:
	continue;
      case -1:
      case -2:  
      case -3:
	finalRc = SYNC_ERROR;
	currState = ABORT_INIT;
	break; //We can break out of the loop in the default of switch(currState).
      case 0:
	break;
      default:
	continue;
    }
    
    if ( ctrlSocket->isRemoteClosed( ) ) {
      finalRc = REMOTE_CLOSED_EARLY;
      break;
    }
    
    
    switch(currState) {
      case OPEN_SERVER:
        if(returnVal != 220) {
          fprintf(stderr, "\nTarget is not accessible.\nBackup aborted.");
          finalRc = TARGET_INACCESSIBLE;
          done = 1;
        }
        currCmdLength = sprintf(cmdSendBuffer, "%s %s" _NL_, "USER", parms->Username);
        ctrlSocket->send( (uint8_t *) cmdSendBuffer, currCmdLength);
        currState = SEND_USER;
        break;
      case SEND_USER:
        if(returnVal != 331) {
          //fprintf(stderr, "Return val: %d", return_val);
          fprintf( stderr, "\nServer says the username is bad. \nNot continuing." );
          finalRc = ACCESS_DENIED;
          done = 1;
        }
        currCmdLength = sprintf(cmdSendBuffer, "%s %s" _NL_, "PASS", parms->Password);
        ctrlSocket->send( (uint8_t *) cmdSendBuffer, currCmdLength);
        currState = SEND_PASS;
        break;
      case SEND_PASS:
        if(returnVal != 230) {
          //fprintf(stderr, "Return val: %d != 230", return_val);
          fprintf(stderr, "\nServer says the password is bad. \nNot continuing." );
          finalRc = ACCESS_DENIED;
          done = 1;
        }
        currCmdLength = sprintf(cmdSendBuffer, "%s %s" _NL_, "CWD", parms->RootDir);
        ctrlSocket->send( (uint8_t *) cmdSendBuffer, currCmdLength);
        currState = CHDIR_BACKUP_ROOT;
        break;
      case CHDIR_BACKUP_ROOT:
        if(returnVal != 250) {
          finalRc = TARGET_INACCESSIBLE;
          done = 1;
        }
        else {
          finalRc = SUCCESS; // We are good if we get here, and can continue.
          done = 1;
        }
        break;
      default:
        done = 1; //Precaution
        break;
      }
    //Get return code if synchronization not lost, and check if multiline response
    //If multiline response,
  }while(!done);
  
  return finalRc;
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
  //Poll and get remaining contents
  ctrlSocket->close( );
  TcpSocketMgr::freeSocket( ctrlSocket );
  setvect( 0x1b, oldCtrlBreakHandler);

  Utils::endStack( );
  Utils::dumpStats( stderr );
  fclose( TrcStream );
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

// Blatantly stolen/modified from FTP.CPP!
// Timeout is specified in milliseconds

//This pollSocket routine attempts to check for an FTP return code... if
//the buffer started in the middle of a response, it will attempt to synchronize
//to a newline, but other program logic should catch that the program lost
//synchronization.
int8_t getLineFromSocket( TcpSocket *s, uint8_t * buffer, uint16_t * charsRcvd, uint16_t maxSize, uint32_t timeout ) 
{

  int8_t pollRc = 0;
  uint16_t recvRc = 0;
  int8_t checkForMultiLine = 0;
  int8_t multiLine = 0;
  //return 0; //Bytes read while polling
  clockTicks_t startTime = TIMER_GET_CURRENT( );

  while ( 1 ) {

    PACKET_PROCESS_SINGLE;
    Arp::driveArp( );
    Tcp::drivePackets( );

    int rc = s->recv( buffer + recvRc, (maxSize - recvRc) );
    if ( rc > -1 ) {
      recvRc += rc;
    }
    
    //If buffer was filled, break.
    if(recvRc >= maxSize)
    {
      //fprintf(stderr, "TOO LONG!\n");
      pollRc = -1;
      break;
    }
    
    //check whether a line was received. If multiline, keep waiting for full
    //response. If line received, break
    if(buffer[recvRc - 2] == '\r' && buffer[recvRc - 1] == '\n')
    {
      //fprintf(stderr, "NEWLINE!\n");
      pollRc = 0;
      break;
    }

    
    uint32_t t_ms = Timer_diff( startTime, TIMER_GET_CURRENT( ) ) * TIMER_TICK_LEN;

    // Timeout?
    if ( t_ms > timeout ) {
      pollRc = -2;
      break;
    }

  }
  (* charsRcvd) = recvRc;
  return pollRc;
}


//Returns 1 if the next newline is before the end of the buffer,
//0 if the next newline is at the end (at which point we should get
//more data. A single-line response shouldn't have multiple newlines- that
//probably indicates a sync error.
int8_t findNextLineBreak(uint8_t ** buf, uint16_t * offsetFromEnd)
{
  uint16_t i = 0;
  uint16_t currOffset = (* offsetFromEnd);
  while(currOffset > 2 && !((* buf)[i] == '\r' &&  (* buf)[i + 1] == '\n'))
  {
    i++;
    currOffset--;
  }
  
  (* buf) = (* buf) + i + 2;
  (* offsetFromEnd) = currOffset;
  
  return currOffset > 2 ? 1 : 0;
  
 /* nextNewlineAtEnd =  (currOffset > 2);
  if(!nextNewlineAtEnd)
  {
    (* buf) = (* buf) + i + 2;
    (* offsetFromEnd) = currOffset;
  }
  
  return nextNewlineAtEnd; */
}

//Modified from mTCP FTP
int8_t checkforFTPResponse(uint8_t * buf, uint16_t bufSiz, uint16_t * retVal, \
  char ** afterRetVal, int8_t * multiLine)
{
  //buf is a buffer, presumably a line from getLineFromSocket
  //bufSize is the number of chars written.
  //retVal is the parsed FTP return value. For purposes of multiline parsing,
  	//it should be also used as an input
  //afterRetVal returns a pointer to after the response code. Needed for parsing
  	//the PASV port.
  //multiLine input is set when we are in the middle of parsing a multiLine response
  	//it is an input variable to avoid static state.
  
  char retCode[4] = {'\0'};
  int8_t respRc = -1; //Assume not a server response
  uint16_t tempRetVal = 0; //This variable is a guard against loss of synchronization.
  //If a multiline response's final line doesn't match the input retVal, a loss
  //of synchronization occurred. Otherwise, retVal gets set to this without a check.
  if (bufSiz > 2 ) 
  {
    if (isdigit(buf[0]) && isdigit(buf[1]) && isdigit(buf[2])) 
    {
      (* afterRetVal) = Utils::getNextToken((char *) buf, retCode, 4 );
      tempRetVal = atoi(retCode);
      
      // Great, it's a server response.
      if((bufSiz > 3) && (buf[3] == '-') ) 
      {
        // Multi-line reponse
        (* multiLine) = 1;
        respRc = 1;
      }
      else 
      {
        // Normal return code or end of a multiline response.
        respRc = 0;
        if((* multiLine))
        {
          (* multiLine) = 0;
          if((* retVal) != tempRetVal)
          {
            respRc = -3; //Server and client lost synchronization
            //if initial return code and the multiline end return code differ!
          }
        }
      }
      (* retVal) = tempRetVal;
    }
    else
    {
        if ((* multiLine)) 
        {
          respRc = 2; //Lines not beginning with numbers
          //okay in multiline response (will happen on buffer full,
          //for instance).
    	}
    }
  }
  else 
  {
    respRc = -2; //Response too short.
  }
  return respRc;
  
  //Return vals:
  //2: Continuation of multiline
  //1: Start of multiline
  //0: Standard response, end of multiline
  //-1: Not a valid server response
  //-2: Buffer too short
  //-3: Multiline synchronization loss
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


//This function doesn't actually parse the response, it just checks that a
//response exists.
//int8_t checkforFTPResponse(char * buf, uint16_t bufSiz, uint16_t currFtpRc)
//perhaps is better?




//checkForUnexpectedResponse //poll until timeout... if even partial response,
	//wait until full response arrives after timeout.
//handleUnexpectedResponse(currState, retCode)


static void shutdown( int rc ) {

  setvect( 0x1b, oldCtrlBreakHandler);

  Utils::endStack( );
  Utils::dumpStats( stderr );
  fclose( TrcStream );
  exit( rc );
}



