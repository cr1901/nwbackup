//C headers (C++ format)
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

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

//C++ class headers
//#include "mediator.hpp"
//#include "optparse.hpp"
//#include "transcvr.hpp"

//C headers
extern "C"
{
	#include "nwbackup.h"
}

#define CMD_RESPONSE_BUFFER_SIZE 512
#define CMD_REQUEST_BUFFER_SIZE 512
#define FILE_BUFFER_SIZE 8192

//Globals
char     PortString[40] = {'\0'},PasvString[40] = {'\0'}; //make union?
uint16_t ServerPort, CtrlPort, DataPort, NextDataPort, PassPort; // Only used when we are a client.
int8_t   PasvOrPort = 0; //FTP Pasv Xfer = 1, Port Xfer = 0

//Buffers
static uint8_t * cmdResponseBuffer;
static char * cmdSendBuffer; //fprintf expects char *
//uint16_t  inBufIndex=0;          // Index to next char to fill
static uint8_t * fileBuffer;

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


//void pollSocket(TcpSocket *s, uint32_t timeout);
uint16_t pollSocket( TcpSocket *s, uint8_t * buffer, uint16_t currOffset, uint16_t maxSize, uint32_t timeout );
void readConfigParms( nwBackupParms * );
int8_t parsePASVResponse( char *pos, IpAddr_t * pasvAddr, uint16_t * pasv_port );
int8_t openDataSocketPASV(TcpSocket ** s, uint16_t bufSize, IpAddr_t pasvAddr, uint16_t pasvPort);
static void closeAndFreeDataSocket( TcpSocket * s );


//==============================================================================
//NWBACKUP functions
//==============================================================================
nwBackupCodes initRemote(nwBackupParms * parms)
{
	// Allocate some data buffers
  	cmdResponseBuffer = (uint8_t *) malloc(CMD_RESPONSE_BUFFER_SIZE);
  	cmdSendBuffer = (char *) malloc(CMD_REQUEST_BUFFER_SIZE);
  	fileBuffer = (uint8_t *) malloc(FILE_BUFFER_SIZE);  
  	
  	if( cmdResponseBuffer == NULL || cmdSendBuffer == NULL || fileBuffer == NULL)
  	{
  	  fprintf(stderr, "Initial buffer allocation failed!\n" );
  	  return ALLOCATION_FAILURE;
  	}
  	
  	readConfigParms(parms);
	
	
	return SUCCESS;
	
}

void closeRemote()
{
	
	
}


//Main function- This code is in part, copied from the sample program in mTCP/* 
/* int main(int argc, char * argv[]){
  nwBackupParms progParms;
  backupState currState = UNINITIALIZED;
  //Needed in too many places?
  uint32_t totalBytesReceived = 0;
  char currentDir[13] = {'\0'};
  //To ask: 	
  //Can a modern protected-mode application use multiple stacks?
  
  fprintf( stderr, "BACKUP program using mTCP by W. Jones (thor0505@comcast.net)\n" 
  	  "TCP stack by M. Brutman (mbbrutman@gmail.com) (C)opyright 2012-2013\n\n" );
  
  if(Utils::parseEnv() != 0){
      printf("Parsing Error!\n");
      return EXIT_FAILURE;
  }
  
  // Allocate some data buffers
  cmdResponseBuffer = (uint8_t *) malloc(CMD_RESPONSE_BUFFER_SIZE);
  cmdSendBuffer = (char *) malloc(CMD_REQUEST_BUFFER_SIZE);
  fileBuffer = (uint8_t *) malloc(FILE_BUFFER_SIZE);
  
  if( cmdResponseBuffer == NULL || cmdSendBuffer == NULL || fileBuffer == NULL)
  {
    fprintf(stderr, "Initial buffer allocation failed!\n" );
    exit(1);
  }
  
  readConfigParms(&progParms);
  
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
  
  TcpSocket *ctrlSocket, *dataSocket = NULL, *listenSocket;
  
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
  int8_t rc2 = Dns::resolve( progParms.BackupServer, serverAddr, 1 );
  if ( rc2 < 0 ) {
    fprintf( stderr, "Error resolving server\n" );
    shutdown( -1 );
  }
  
  
  while ( !done ) {
  
    if ( CtrlBreakDetected ) break;
    if ( !Dns::isQueryPending( ) ) break;
  
    PACKET_PROCESS_SINGLE;
    Arp::driveArp( );
    Tcp::drivePackets( );
    Dns::drivePendingQuery( );
  
  }
  
  // Query is no longer pending or we bailed out of the loop.
  rc2 = Dns::resolve( progParms.BackupServer, serverAddr, 0 );
  
  if ( rc2 != 0 ) {
    fprintf( stderr, "Error resolving server\n" );
    shutdown( -1 );
  }
  
  ctrlSocket = TcpSocketMgr::getSocket( );
  listenSocket = TcpSocketMgr::getSocket( );
  
  ctrlSocket->setRecvBuffer( CTRL_RECV_BUFFER_SIZE );
  listenSocket->setRecvBuffer( LISTEN_RECV_BUFFER_SIZE );
  
  fprintf( stderr, "Server resolved to %d.%d.%d.%d - connecting\n\n",
  		 serverAddr[0], serverAddr[1], serverAddr[2], serverAddr[3] );
  
  // Non-blocking connect.  Wait 10 seconds before giving up.
  rc = ctrlSocket->connect( CtrlPort, serverAddr, ServerPort, 10000 );

  if ( rc != 0 ) {
    fprintf( stderr, "Socket open failed\n" );
    shutdown( -1 );
  }
  else
  {
    fprintf( stderr, "Connected!\n\n" );
    currState = OPEN_SERVER;
  }

  done = 0;
  uint8_t cmd_buff_size = 0, done_rx = 0, unexpected_return_code = 0;
  uint8_t counter = 0;
  //uint8_t curr_cmd = sample_prog[counter], next_cmd = PASS;
  uint16_t currCmdLength;
  
  char return_code[30];
  char pasv_buffer[30];
  char token_buf[30];
  char * afterReturnCode;
  uint16_t return_val;
  uint16_t pasv_port;
  IpAddr_t pasvAddr;
  int8_t pasvDataRc;
  
  
  while ( !done ) {

    if ( CtrlBreakDetected ) {
      fprintf( stderr, "\nCtrl-Break detected\n" );
      done = 1;
    }
    
    //ctrlSocket->send( (uint8_t *) "USER wjones" _NL_, 13 );
    int16_t recvRc = 0;
    //if(!skip_receive)
    //{
    // Service the connection
    PACKET_PROCESS_SINGLE;
    Arp::driveArp( );
    Tcp::drivePackets( );
      
    if ( ctrlSocket->isRemoteClosed( ) ) {
      done = 1;
    }

    // Process incoming packets first.
    //uint16_t?
    recvRc = ctrlSocket->recv( cmdResponseBuffer, CMD_RESPONSE_BUFFER_SIZE );
    //}

    if ( recvRc > 0 ) {
      //fprintf( stderr, "C: ");
      //write to stdout (why aren't we using fwrite())?
      afterReturnCode = Utils::getNextToken( (char *) cmdResponseBuffer, return_code, 30 );
      // Do check for return value- although 'unexpected states may be
      //sufficient error 'correction'.
      
       return_val = atoi(return_code);
      write( 1, cmdResponseBuffer, recvRc );
      //fprintf(stderr, "\n%d\n", return_val);
    }
    else if ( recvRc == 0)
    {
      //fprintf( stderr, "\nC: Nothing to read from socket." );
      //if(noDataExpected
      // If we are here, chances are we are expecting some data.
      continue;
    }
    else if ( recvRc < 0 ) {
      fprintf( stderr, "\nError reading from socket.\nBackup aborted." );
      done = 1;
      break;
    }
    
    /* Essentially, backups are done in the following manner: Check for packets,
    execute actions until packet needs to be sent, wait for packet and check
    return code. Sadly, this ends up having a large number of states. */
    
    /* It is better to think of the states as what data was PREVIOUSLY sent to
    the server and responding to what the server returns. */
    
    /*
    switch(currState)
    {
      case OPEN_SERVER:
      	//pollSocket(
        if(return_val == 220)
        {
          currCmdLength = sprintf(cmdSendBuffer, "%s %s" _NL_, "USER", progParms.Username);
          ctrlSocket->send( (uint8_t *) cmdSendBuffer, currCmdLength);
          currState = SEND_USER;
        }
        else
        {
          unexpected_return_code = 1;
          currState = CLOSE_SERVER;
        }
        break;
        
      case SEND_USER:
      	switch(return_val)
      	{
      	  case 331:
      	    currCmdLength = sprintf(cmdSendBuffer, "%s %s" _NL_, "PASS", progParms.Password);
            ctrlSocket->send( (uint8_t *) cmdSendBuffer, currCmdLength);
            currState = SEND_PASS;      	  	  
      	    //currState = SEND_PASS;
      	    break;
      	  case 332:
      	  case 430:
      	    fprintf(stderr, "Server says you need an account or bad username (%d).\n" \
      	    	    "Not continuing.\n", return_val);
      	    currState = CLOSE_SERVER;
      	    break;
      	  default:
      	    unexpected_return_code = 1;
            currState = CLOSE_SERVER;
            break;
        }
        break;
      //case SEND_USER_WAIT_RTN:
  
      case SEND_PASS:
      	switch(return_val)
      	{
      	  case 230:
      	    //fprintf(stderr, "User name and password okay (%d).\n", return_val);
      	    currCmdLength = sprintf(cmdSendBuffer, "%s %s" _NL_, "CWD", progParms.RootDir);
            ctrlSocket->send( (uint8_t *) cmdSendBuffer, currCmdLength);
      	    currState = CHDIR_BACKUP_ROOT;
      	    break;
      	  case 430:
      	    fprintf(stderr, "Server says password is bad (%d).\n" \
      	    	    "Not continuing.\n", return_val);
      	    currState = CLOSE_SERVER;
      	    break;
      	  default:
      	    unexpected_return_code = 1;
            currState = CLOSE_SERVER;
            break;
        }
        break;
        
      case CHDIR_BACKUP_ROOT:
        switch(return_val)
      	{
      	  case 250:
      	    // We know the relevant directory exists... go get a directory
      	    //listing
      	    //fprintf(stderr, "Directory change okay (%d).\n", return_val);
      	    currCmdLength = sprintf(cmdSendBuffer, "%s %s" _NL_, "PASV", NULL);
            ctrlSocket->send( (uint8_t *) cmdSendBuffer, currCmdLength);
      	    currState = BACKUP_ROOT_NLST_PASV;
      	    break;
      	  case 450:
      	    fprintf(stderr, "Server says backup root directory doesn't exist (%d).\n" \
      	    	    "Not continuing.\n", return_val);
      	    currState = CLOSE_SERVER;
      	    break;
      	  default:
      	    unexpected_return_code = 1;
            currState = CLOSE_SERVER;
            break;
        }
        break;
        
      case BACKUP_ROOT_NLST_PASV:
        if(return_val == 227)
        {
          if(parsePASVResponse(afterReturnCode, &pasvAddr, &pasv_port) < 0)
          {
            fprintf(stderr, "Attempt to get passive port failed.\n Not continuing. \n");
            currState = CLOSE_SERVER;
          }
          else
          {
            //fprintf(stderr, "Pasv Address: %d,%d,%d,%d Pasv Port: %hi\n", pasvAddr[0],
            //	    pasvAddr[1], pasvAddr[2], pasvAddr[3], pasv_port);	  
            pasvDataRc = openDataSocketPASV(&dataSocket, DATA_RECV_BUFFER_SIZE, pasvAddr, pasv_port);
            if(pasvDataRc == 0)
            {
              //fprintf(stderr, "Passive command okay...\n");
              currCmdLength = sprintf(cmdSendBuffer, "%s %s" _NL_, "LIST", NULL);
              ctrlSocket->send( (uint8_t *) cmdSendBuffer, currCmdLength);
      	      currState = BACKUP_ROOT_NLST_WAIT;
            }
            else if(pasvDataRc == -1)
            {
              fprintf(stderr, "Attempt to allocate data socket failed.\n Not continuing. \n");
              currState = CLOSE_SERVER;
            }
            else
            {
              fprintf(stderr, "Attempt to connect data socket failed.\n Not continuing. \n");
              currState = CLOSE_SERVER;
            } //if pasvDataRc
          } //if parsePASVResponse
        } //if return code == 227
        else
        {
          unexpected_return_code = 1;
          currState = CLOSE_SERVER;
        }
        break;
        
      case BACKUP_ROOT_NLST_WAIT:
      	if(return_val == 150)
      	{
      	  totalBytesReceived = 0;
      	  uint8_t receivingData = 1;
      	  uint16_t dataBytesRead = 0;
      	  uint16_t bytesToRead = FILE_BUFFER_SIZE;
      	  while(receivingData)
      	  {

            PACKET_PROCESS_SINGLE;
            Arp::driveArp( );
            Tcp::drivePackets( );	
            if ( dataSocket->isRemoteClosed( ) ) {
              receivingData = 0;
              currState = BACKUP_ROOT_NLST_DATA;
            }
            
            do{
            	    recvRc = dataSocket->recv( fileBuffer, bytesToRead - dataBytesRead);
		    if(recvRc > 0) {
		      totalBytesReceived += recvRc;
		      bytesToRead -= recvRc;
		      dataBytesRead += recvRc;
		      
		      if ( bytesToRead == 0 ) {
                        // int rc = fwrite( FileBuffer, bytesRead, 1, targetFile );
                        //if (rc != 1 ) {
                        //  done = 3;
                        //}
                        //write( fd, FileBuffer, bytesRead );
                        bytesToRead = FILE_BUFFER_SIZE;
                        dataBytesRead = 0;
                        
                        // if ( !IsStdoutFile ) {
                        //  if ( update == 0 ) {
                        //   myCprintf( x, y, "Bytes transferred: %lu", totalBytesReceived );
                        // }
                        //  update = (update + 1) & 0x03;
                        }
		    }
		    else if ( recvRc < 0 ) {
		      fprintf( stderr, "\nError reading from data socket.\nBackup aborted." );
		      receivingData = 0;
		      currState = CLOSE_SERVER;
		      //No choice... we have to break cleanly out of nested
		      //loop
		      goto abort_data_receive;
		    }
	    }while(recvRc);
          }         
      	}
      	else
      	{
      	  unexpected_return_code = 1;
          currState = CLOSE_SERVER;
        }
abort_data_receive:         
        break;
        
      case BACKUP_ROOT_NLST_DATA:
      	if(return_val == 226)
      	{
          write(1, fileBuffer, totalBytesReceived);
          // Check that there are no file conflicts and choose directory name
          strncpy(currentDir, "BACKUP", 13);
          currCmdLength = sprintf(cmdSendBuffer, "%s %s" _NL_, "MKD", currentDir);
          ctrlSocket->send( (uint8_t *) cmdSendBuffer, currCmdLength);
      	  currState = MK_BACKUP_DIR;	
      	}
      	else
      	{
      	  unexpected_return_code = 1;
          currState = CLOSE_SERVER;
        }
        break;
        
      case MK_BACKUP_DIR:
      	// 257 Directory created
        currState = CLOSE_SERVER;
        
      	else
      	{
      	  unexpected_return_code = 1;
          currState = CLOSE_SERVER;
        }
        break;
        
      case CWD_BACKUP_DIR:
        
      case CLOSE_SERVER: // CLOSE_SERVER commands executed outside loop in
      	      //previous iteration
      	if(return_val == 221)
        {
          currState = UNINITIALIZED;
        }
        else
        {
          // Nothing may be valid anymore, so skip server close!	
          unexpected_return_code = 1;
          currState = UNINITIALIZED;
        }
        break;
      case UNINITIALIZED:
      default:
      	break;
    }
    if(unexpected_return_code)
    {
       	 fprintf(stderr, "Server responded unexpectedly: State %d, Return Code %d.\n" \
       	 	 "Attempting a graceful abort.\n", currState, return_val);
       	 unexpected_return_code = 0;
    }*/
    
    /* This state is handled outside the switch because each iteration of the
    loop will not go to the switch unless packets are received. It is very 
    possible that the server has closed already by the time this command is sent.
    (Look up: behavior of socket when it sends data to already closed destination). */
    //if(currState == CLOSE_SERVER)
    //{
      //currCmdLength = sprintf(cmdSendBuffer, "%s %s" _NL_, "QUIT", NULL);
      //ctrlSocket->send( (uint8_t *) cmdSendBuffer, currCmdLength);
    //}
  //}
  
  //args[PORT] = PortString;
  //snprintf?
  //sprintf(PortString, " %d,%d,%d,%d,%d,%d", MyIpAddr[0], MyIpAddr[1], 
//			MyIpAddr[2], MyIpAddr[3], (DataPort/256), (DataPort%256));
  
  
  
  
  //optionParser op = optionParser(cfgOps, cfgLen, cfgNum);
  //mediator Med;
  //ctrlSocket->close( );
  //TcpSocketMgr::freeSocket( ctrlSocket );
  //shutdown( 0 );
  //return 0;
//}


//Other functions


// Blatantly "stolen" from IRC.CPP! Modified slightly.
void readConfigParms( nwBackupParms * parms ) {
  #define NUM_PARAMS 5
  const static char *ParmNames[]  = { "NWBACKUP_SERVER", "NWBACKUP_USERNAME", 
  "NWBACKUP_PASSWORD", "NWBACKUP_DIR", "NWBACKUP_FORMAT"};
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
    (* PasvAddr)[0] = 0; (* PasvAddr)[1] = 0; (* PasvAddr)[2] = 0; (* PasvAddr)[3] = 0; PasvPort = 0;
    return -1;
  }


  uint16_t t0, t1, t2, t3, t4, t5;
  int rc = sscanf( pos, "%d,%d,%d,%d,%d,%d",
               &t0, &t1, &t2, &t3, &t4, &t5 );
  if ( rc != 6 ) {
    (* PasvAddr)[0] = 0; (* PasvAddr)[1] = 0; (* PasvAddr)[2] = 0; (* PasvAddr)[3] = 0; PasvPort = 0;
    return -1;
  }

  (* PasvAddr)[0] = t0; (* PasvAddr)[1] = t1; (* PasvAddr)[2] = t2; (* PasvAddr)[3] = t3;
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
    if ( rc > -1 ) currOffset += rc;

    //processSocketInput( ); Defer processing

    uint32_t t_ms = Timer_diff( startTime, TIMER_GET_CURRENT( ) ) * TIMER_TICK_LEN;

    // Timeout?
    if ( t_ms > timeout ) break;

  }
  return currOffset;
}

//Based on mTCP FTP
int8_t openDataSocketPASV(TcpSocket ** s, uint16_t bufSize, IpAddr_t pasvAddr, uint16_t pasvPort)
{
  	
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


