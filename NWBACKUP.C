#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <malloc.h>

#include "dir.h"
#include "nwbackup.h"

//#define FILE_BUF_SIZE 8192
#define FILE_BUF_SIZE 4096
//#define OUTBUF_SIZE BUFSIZ*8 //Cripples performance...
#define OUTBUF_SIZE BUFSIZ

extern void start_timer();
extern uint32_t get_elapsed_time();
static uint32_t computeRate( uint32_t bytes, uint32_t elapsed );

typedef signed char (* nw_algorithm)(nwBackupParms *, char *, char *, char *);

signed char do_backup(nwBackupParms * parms, char * remote_name, char * local_dir, char * logfile_name);
signed char do_diff(nwBackupParms * parms, char * remote_name, char * local_dir, char * logfile_name);
signed char do_restore(nwBackupParms * parms, char * remote_name, char * local_dir, char * logfile_name);
int8_t send_file(FILE * fp, char * remote_name, uint8_t * out_buf, uint16_t payload_size);
int check_heap( void );
void print_banner();
void print_usage();

int main(int argc, char * argv[]) {
  short prog_mode = 0;
  char * logfile_name = NULL;
  unsigned short count = 1;
  nwBackupParms parms;
  nw_algorithm main_program[] = {do_backup, do_diff, do_restore};
  char * mode_string[] = {"FULL", "DIFF", "RESTORE"};

  /* To ask... can option arguments use '-' for stdin/stdout processing? */

  if(argc < 3) {
    print_usage();
    return EXIT_FAILURE;
  }

  /* POSIX option args block */
  {
    int options_parsing = 1;
    while(count < argc && argv[count][0] == '-' && options_parsing) {
      unsigned short num_opts_after_hyphen, i;
      int backup_mode_implied = 0;
      int restore_mode_implied = 0;

      num_opts_after_hyphen = strlen(argv[count]) - 1;
      /* If option doesn't follow hyphen... */
      if(!num_opts_after_hyphen) {
        /* This single hyphen is actually an operand, which in POSIX world
        represents stdin/stdout or a file called '-'... */
        fprintf(stderr, "Single hyphen not supported by this utility.\n");
      }

      for(i = 0; i < num_opts_after_hyphen; i++) {
        switch(argv[count][i + 1]) {
        case 'r':
          if(backup_mode_implied) {
            fprintf(stderr, "Backup and restore options are mutually exclusive.\n");
            return EXIT_FAILURE;
          }
          restore_mode_implied = 1;
          prog_mode = 2;
          break;
        case 'f':
          if(restore_mode_implied) {
            fprintf(stderr, "Full backup and restore options are mutually exclusive.\n");
            return EXIT_FAILURE;
          }
          else if(backup_mode_implied) {
            fprintf(stderr, "A backup mode was supplied twice!\n");
            return EXIT_FAILURE;
          }
          backup_mode_implied = 1;
          prog_mode = 0;
          break;
        case 'd':
          if(restore_mode_implied) {
            fprintf(stderr, "Differential backup and restore options are mutually exclusive.\n");
            return EXIT_FAILURE;
          }
          else if(backup_mode_implied) {
            fprintf(stderr, "A backup mode was supplied twice!\n");
            return EXIT_FAILURE;
          }
          backup_mode_implied = 1;
          prog_mode = 1;
          break;
        case 'h':
          print_usage();
          return EXIT_FAILURE;
        case 'l':
          if(i != num_opts_after_hyphen - 1) {
            printf("Option -l requires an argument, but " \
                   "was specified as part of options which do not "
                   "require arguments\n");
            return EXIT_FAILURE;
          }

          count++;
          /* Skip the leading space! */
          logfile_name = argv[count];
          break;
        default:
          fprintf(stderr, "Invalid option: %c.\n", argv[count][i + 1]);
          return EXIT_FAILURE;
        }
      }
      count++;
    }
  }

  if(argc - count < 2) {
    printf("Not enough command line parameters (type -h for help)\n");
    return EXIT_FAILURE;
  }

  /* POSIX operands and main logic block */
  {
    char * backup_name = NULL;
    char * start_dir = NULL;
    signed char client_rc;

    backup_name = argv[count++];
    start_dir = argv[count];

    if(logfile_name == NULL) {
      logfile_name = malloc(128);
      snprintf(logfile_name, 128, "%s.LOG", backup_name);
    }


    fprintf(stderr, "NW: Backup Params:\n" \
            "Mode: %s\nBackup Name: %s\nRoot Dir: %s\nLogfile: %s\n", \
            mode_string[prog_mode], backup_name, start_dir, logfile_name);

    /* To do: Add NWGET utility for restoring individual files... or add as
    third program variant. NWGET restore mode and "fetch from public server" mode?

    Re-Look up mutual exclusion between program invocation
    types and mutual exclusion within invocation type, as well as whether it is
    legal to re-specify command line params.*/
    client_rc = main_program[prog_mode](&parms, backup_name, start_dir, logfile_name);


    return 0;
  }
}


char * dummy_outbuf;
signed char do_backup(nwBackupParms * parms, char * remote_name, char * local_dir, char * logfile) {
  char ctrl_file[L_tmpnam];
  dirStack_t dStack;
  dirStruct_t currDir;
  fileStruct_t currFile;
  int8_t rc, done;
  nwBackupCodes nw_rc = SUCCESS;
  int8_t allDirsTraversed = 0;
  int8_t traversalError = 0;
//  uint8_t i = 0, j = 0;
  int do_backup_rc = 0;
  int pathEostr;
  /* char driveLetter[4] = {'\0', ':', '\\', '\0'}; /* The drive letter has to be handled separately
  		unfortunately... the drive letter must include the trailing
  		backslash, while all other path components do not/must not. */

  int charsCopied;
  char * path;
  char * path_and_file;
  char * unixPath;
  char * unix_path_and_file;
  char * file_buffer;
  char * out_buffer;

  path = malloc(129 * 4);
  file_buffer = malloc(FILE_BUF_SIZE);
  out_buffer = malloc(OUTBUF_SIZE);
  //dummy_outbuf = calloc(OUTBUF_SIZE, 1); //This doesn't set the first and third byte
  //to zero!

  if(path == NULL || file_buffer == NULL || out_buffer == NULL) {
    fprintf(stderr, "NW: Could not allocate memory for path or file buffers!\n");
    return -12;
  }

  /* Don't bother freeing till end of program- if error, program will terminate
  soon anyway! */
  path_and_file = path + 129;
  unixPath = path_and_file + 129;
  unix_path_and_file = unixPath + 129;
  //char * full_server_path;
  /* rc = sscanf(argv[1], "%c:\\%s", driveLetter[0], path);
  if(rc < 2)
  {
    // Edge case where we only include drive letter.
    if(rc < 1 || argv[1][1] != ':' || argv[1][2] != '\\' || argv[1][3] != '\0')
    {
      fprintf(stderr, "The directory name must be absolute and include the drive letter!\n");
      return EXIT_FAILURE;
    }
  } */


  if(initDirStack(&dStack)) {
    fprintf(stderr, "NW: Directory Stack Initialization failed!\n");
    return -1;
  }

  if(!strcpy(path, local_dir))
    /* if(strncpy(path, argv[1], DIR_MAX_PATH + 1) >= (DIR_MAX_PATH + 1)) */
  {
    /* Shouldn't fail... but Heartbleed convinces me to
    code defensively. */
    fprintf(stderr, "NW: Specified path name is too long...\n"
            "Actually if we're got this error on DOS, we have more serious\n"
            "trouble than just a bad path!\n");
  }

  pathEostr = strlen(path);
  if(path[pathEostr - 1] == 92) {
    path[pathEostr - 1] = '\0';
    pathEostr--;
    /* pathEostr now points to the root path's NULL
    terminator. */
  }

  //full_unix_path = malloc(strlen(parms-> strlen(remote_name) + );

  if(traversalError = pushDir(&dStack, &currDir, path)) {
    fprintf(stderr, "NW: Initial directory push failed!\n");
    return -2;
  }

  if(openDir(path, &currDir, &currFile)) {
    fprintf(stderr, "NW: Directory open failed!\n");
    return -3;
  }


  nw_rc = initRemote(parms);
  if(nw_rc != SUCCESS) {
    do_backup_rc = -4;
  }

  nw_rc = mkDirRemote(remote_name);
  if(!(nw_rc == SUCCESS || nw_rc == TARGET_EXIST)) {
    do_backup_rc = -5;
  }

  nw_rc = chDirRemote(remote_name);
  if(!(nw_rc == SUCCESS)) {
    do_backup_rc = -6;
  }



  while(!allDirsTraversed && do_backup_rc == 0) {
    /* int retry_count = 0; */
    int retry_count;
    retry_count = 0;

    assert(_heapchk() == _HEAPOK);
    charsCopied = snprintf(path_and_file, DIR_MAX_PATH + 1, "%s\\%s", path, currFile.name);
    if(charsCopied >= DIR_MAX_PATH + 1) {
      traversalError = 1;
      fprintf(stderr, "NW: Directory traversal error, LINE %u!\n", __LINE__);
      do_backup_rc = -8;
      break;
    }
    //printf("Current Dir+file: %s\n", path_and_file);
    /* Strip the root from the path... the "+1" reflects
    that the NULL terminator of the root coincides with the path
    separator in path. */


    unixPath[0] = '\0';
    if(createUnixName(unixPath, &path[pathEostr + 1]) == NULL) {
      fprintf(stderr, "NW: Unix directory name creation failed!\n");
      do_backup_rc = -7;
      break;
    }


    if(strlen(unixPath) == 0) { /* We are back at directory root if here */
      sprintf(unix_path_and_file, "%s", currFile.name);
    }
    else {
      sprintf(unix_path_and_file, "%s/%s", unixPath, currFile.name);
    }

    if(currFile.attrib & _A_SUBDIR) {
      /* The two relative directories can be safely
      ignored. */
      if(!(strcmp(currFile.name, ".") == 0 \
           || strcmp(currFile.name, "..") == 0)) {

        strcpy(path, path_and_file);
        traversalError = pushDir(&dStack, &currDir, path);
        if(traversalError) {
          fprintf(stderr, "NW: Directory traversal error, LINE %u!\n", __LINE__);
          do_backup_rc = -9;
          break;
        }

        if(openDir(path, &currDir, &currFile)) {
          fprintf(stderr, "NW: Directory traversal error, LINE %u!\n", __LINE__);
          do_backup_rc = -10;
          break;
        }

        fprintf(stderr, "NW: Creating directory %s...\n", unix_path_and_file);
        do {
          if(retry_count) {
            fprintf(stderr, "NW: Retrying operation...\n");
          }
          nw_rc = mkDirRemote(unix_path_and_file);
          retry_count++;
        }
        while(nw_rc != SUCCESS && retry_count <= 3);

        if(retry_count > 3) {
          do_backup_rc = -11;
        }
        else {
          retry_count = 0;
        }

        continue; /*A new open directory already will
        have the first file open, just like before
        the main loop. So we don't skip it, don't get
        the next file after opening a directory.
        On DOS at least, not including
        this continue is harmless, and saves a few cycles,
        as DOS will always return "." first, which would
        be skipped anyway!. */
      }
    }

    else { /* We have a file... send it. */
      FILE * currFp;
      currFp = fopen(path_and_file, "rb");
      if(currFp == NULL) {
        fprintf(stderr, "Read error on file: %s! Not continuing.", path_and_file);
        do_backup_rc = -13;
      }
      else {
        int8_t local_error = 0;
        setvbuf(currFp, file_buffer, _IOFBF, FILE_BUF_SIZE);
        fprintf(stderr, "NW: Storing file %s...\n", unix_path_and_file);

        done = 0;
        while(!done && !local_error && retry_count <= 3) {
          int8_t send_remote_rc;
          if(retry_count) {
            fprintf(stderr, "NW: Retrying operation...\n");
          }

          send_remote_rc = send_file(currFp, unix_path_and_file, out_buffer, OUTBUF_SIZE);
          switch(send_remote_rc) {
          case 0:
            done = 1;
            break;
          case -2:
            fprintf(stderr, "NW: Read error on file: %s! Not continuing.", path_and_file);
            local_error = 1; /* Local file error. */
            break;
          case -1: /* Recoverable error. */
          default:
            break;
          }
          retry_count++;
        }

        if(local_error) { /* If file error, we need to break out. */
          do_backup_rc = -15;
        }
        else {
          if(retry_count > 3) {
            do_backup_rc = -14;
          }
          else {
            retry_count = 0;
          }
        }
      }
      fclose(currFp);
    }

    /* Only perform another iteration of the file loop
    when there is in fact another file to send. If there are no
    files left, we know to break as well. The while loop should
    only execute when a parent directory has finished traversal at
    the same time as its child(ren). */
    if(!do_backup_rc) /* Don't bother if we got here and an error occured,
      we are about to break anyway. */
    {
      while(getNextFile(&currDir, &currFile) && !allDirsTraversed) {
        closeDir(&currDir);
        if(popDir(&dStack, &currDir, path)) {
          allDirsTraversed = 1;
        }
      }
    }
  }

  if(do_backup_rc) {
    fprintf(stderr, "NW: Full backup failed with status code %d.\n", do_backup_rc);
  }
  else {
    fprintf(stderr, "NW: Full backup completed successfully.\n");
  }
  closeRemote();
  return 0;
}

signed char do_diff(nwBackupParms * parms, char * remote_name, char * local_dir, char * logfile) {
  char ctrl_file[L_tmpnam];
  return 0;
}

signed char do_restore(nwBackupParms * parms, char * remote_name, char * local_dir, char * logfile) {
  char ctrl_file[L_tmpnam];
  return 0;
}

int8_t send_file(FILE * fp, char * remote_name, uint8_t * out_buffer, uint16_t payload_size) {
  nwFileHandle nwFp = NULL;
  nwBackupCodes open_rc;
  int8_t remote_rc = 0; /* Assume success... */

  open_rc = openRemoteFile(nwFp, remote_name, 0);

  if(!open_rc) { /* If we opened the file ok, read it to the remote */
    nwBackupCodes close_rc;
    nwBackupCodes send_rc;
    uint16_t chars_read;
    int8_t done_read;
    uint16_t actual_chars_sent;
    int8_t local_file_error;
    //long test;
    unsigned long total_size = 0;
    unsigned long elapsed_time = 0;
    unsigned long prev_elapsed = 0;

    done_read = 0;
    send_rc = 0;
    local_file_error = 0;


    start_timer();
    while(!done_read && (send_rc == SUCCESS || send_rc == TARGET_BUSY)  && !local_file_error) {
      chars_read = fread(out_buffer, 1, payload_size, fp);
      //fprintf(stderr, "%d chars read\n", chars_read);

      if(chars_read < payload_size) { /* Send the remaining characters... */
        if(feof(fp)) {
          uint16_t chars_left = chars_read;
          uint16_t chars_read_since_eof = 0;
          //Race condition if (all these) fprintfs and sendDataRemote commented out.
          while((send_rc == SUCCESS || send_rc == TARGET_BUSY) && chars_left) {
            send_rc = sendDataRemote(nwFp, out_buffer + chars_read_since_eof, chars_left, &actual_chars_sent);
            //send_rc = SUCCESS;
            //actual_chars_sent = chars_left;
            //fprintf(stderr, "%d chars sent\n", actual_chars_sent);
            total_size += actual_chars_sent;
            chars_read_since_eof += actual_chars_sent;
            chars_left -= actual_chars_sent;

            elapsed_time = get_elapsed_time();
            if(elapsed_time - prev_elapsed > 1000) {
              prev_elapsed = elapsed_time;
              fprintf(stderr, "%lu total bytes sent... %lu B/sec\r", total_size, \
                      computeRate(total_size, elapsed_time));
            }
          }

          /* We will break out anyway if send_rc is triggered... */
          if(!send_rc) {
            done_read = 1;
          }
        }
        else { /* A file error occurred. */
          local_file_error = 1;
        }
      }
      else {
        send_rc = sendDataRemote(nwFp, out_buffer, chars_read, &actual_chars_sent);
        total_size += actual_chars_sent;
        elapsed_time = get_elapsed_time();
        if(elapsed_time - prev_elapsed > 1000) {
          prev_elapsed = elapsed_time;
          fprintf(stderr, "%lu total bytes sent... %lu B/sec\r", total_size, \
                  computeRate(total_size, elapsed_time));
        }
        //send_rc = 0;
        //actual_chars_sent = payload_size;
        //fprintf(stderr, "%d chars sent\n", actual_chars_sent);
        if(actual_chars_sent < chars_read) { /* This is okay, and will happen sometimes... */
          /* Barring transient errors, this cast should aways work...
          chars_sent will likely not be > 536 and chars_read is bounded by
          payload_size. */

          fseek(fp, (long) ((short) actual_chars_sent - (short) chars_read), SEEK_CUR);

          /* Just reset the file pointer back the number of characters that
          weren't sent */

          /* Casting fun
          //test = (long) ((short) actual_chars_sent - (short) chars_read); //Good
          //test = (long) (actual_chars_sent - chars_read); //Bad
          //(long) ((short) actual_chars_sent - (short) chars_read); //Good
          //(long) (actual_chars_sent - chars_read); //Good? Apparently cast is meaningless...
          //fseek(fp, (long) ((short) actual_chars_sent - (short) chars_read), SEEK_CUR); //Good
          //fseek(fp, (long) (actual_chars_sent - chars_read), SEEK_CUR); //Bad
          */
        }
      }
    }

    if(!local_file_error) {
      /* We should close the file even if send_rc errored out! */
      close_rc = closeRemoteFile(nwFp);
      if(!close_rc && !send_rc) {
        fprintf(stderr, "File sent okay... %lu total bytes sent... %lu B/sec\n", total_size, \
                computeRate(total_size, elapsed_time));
        remote_rc = 0; /* If we successfully sent the file, we are done. */
      }
      else {
        fprintf(stderr, "Open error code: %d, Send error code: %d, Close error code: %d\n", open_rc, send_rc, close_rc);
        remote_rc = -1; /* Worth retrying transfer. */
      }
    }
    else {
      close_rc = closeRemoteFile(nwFp);
      remote_rc = -2; /* Do not retry transfer. */
    }
  }
  else {
    remote_rc = -1;
  }

  return remote_rc;
}


void print_banner() {
  fprintf( stderr, "NetWork BACKUP program for mTCP FTP by W. Jones (thor0505@comcast.net)\n"
           "TCP stack by M. Brutman (mbbrutman@gmail.com) (C)opyright 2012-2014\n\n" );
}

void print_usage() {
  print_banner();
  /* Removed: [-s server_name][-u username][-p password] */
  fprintf( stderr, \
           "Usage: NWBACKUP [-d | -f][-l logfile] [backup_name] [local_dir]\n" \
           "  NWBACKUP -r[-l logfile] [backup_name] [local_dir] \n" \
           "  [-d | -f]: Differential or full backup (default full).\n" \
           "  -r: Perform restore. Does NOT need to be first argument.\n" \
           "  Default logfile name is [backup_name].LOG.\n");
}


/* Returns non-zero value if heap is corrupted */
int check_heap( void ) {
  int     rc = 0;

  switch( _heapchk() ) {
  case _HEAPOK:
    printf( "OK - heap is good\n" );
    break;
  case _HEAPEMPTY:
    printf( "OK - heap is empty\n" );
    break;
  case _HEAPBADBEGIN:
    printf( "ERROR - heap is damaged\n" );
    rc = -1;
    break;
  case _HEAPBADNODE:
    printf( "ERROR - bad node in heap\n" );
    rc = -1;
    break;
  }
  return( rc );
}



/* Stolen from MTCP FTP... */
static uint32_t computeRate( uint32_t bytes, uint32_t elapsed ) {

  uint32_t rate;

  if ( elapsed == 0 ) {
    elapsed = 55;
  }

  if ( bytes < 2000000ul ) {
    rate = (bytes * 1000) / elapsed;
  }
  else if ( bytes < 20000000ul ) {
    rate = (bytes * 100) / (elapsed/10);
  }
  else if ( bytes < 200000000ul ) {
    rate = (bytes * 10) / (elapsed/100);
  }
  else {
    rate = bytes / (elapsed / 1000);
  }

  return rate;
}
