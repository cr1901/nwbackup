#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <malloc.h>

#include "dir.h"
#include "nwbackup.h"
#include "control.h"

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
int8_t restore_file(FILE * fp, char * remote_name, uint8_t * out_buffer, uint16_t payload_size);

int check_heap(void);
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
            fprintf(stderr, "Option -l requires an argument, but " \
                   "was specified as part of options which do not " \
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
    fprintf(stderr, "Not enough command line parameters (type -h for help)\n");
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


    fprintf(stderr, "Backup Params:\n" \
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

/* For those who would complain about function length- treat the following 
functions as if they are part of the main program, but in a different scope. */

//char * dummy_outbuf;
signed char do_backup(nwBackupParms * parms, char * remote_name, char * local_dir, char * logfile) {
  char ctrl_file_name[L_tmpnam];
  FILE * ctrl_file;
  dirStack_t dstack;
  dirStruct_t curr_dir;
  fileStruct_t curr_file;
  nwBackupCodes nw_rc = SUCCESS;
  int8_t all_dirs_traversed = 0;
  int8_t traversal_error = 0;
  int do_backup_rc = 0;
  int path_eostr;
  /* char driveLetter[4] = {'\0', ':', '\\', '\0'}; /* The drive letter has to be handled separately
  		unfortunately... the drive letter must include the trailing
  		backslash, while all other path components do not/must not. */
  char * path, * path_and_file, * unix_path, \
    * unix_path_and_file, * file_buffer,  * out_buffer;

  path = malloc(129 * 4);
  file_buffer = malloc(FILE_BUF_SIZE);
  out_buffer = malloc(OUTBUF_SIZE);
  //dummy_outbuf = calloc(OUTBUF_SIZE, 1); //This doesn't set the first and third byte
  //to zero!

  if(path == NULL || file_buffer == NULL || out_buffer == NULL) {
    fprintf(stderr, "Could not allocate memory for path or file buffers!\n");
    return -1;
  }

  /* Don't bother freeing till end of program- if error, program will terminate
  soon anyway! */
  path_and_file = path + 129;
  unix_path = path_and_file + 129;
  unix_path_and_file = unix_path + 129;
  

  if(initDirStack(&dstack)) {
    fprintf(stderr, "Directory Stack Initialization failed!\n");
    return -2;
  }

  if(!strcpy(path, local_dir))
    /* if(strncpy(path, argv[1], DIR_MAX_PATH + 1) >= (DIR_MAX_PATH + 1)) */
  {
    /* Shouldn't fail... but Heartbleed convinces me to
    code defensively. */
    fprintf(stderr, "Specified path name is too long...\n"
            "Actually if we're got this error on DOS, we have more serious\n"
            "trouble than just a bad path!\n");
  }

  path_eostr = strlen(path);
  if(path[path_eostr - 1] == 92) {
    path[path_eostr - 1] = '\0';
    path_eostr--;
    /* path_eostr now points to the root path's NULL
    terminator. */
  }

  //full_unix_path = malloc(strlen(parms-> strlen(remote_name) + );

  if(traversal_error = pushDir(&dstack, &curr_dir, path)) {
    fprintf(stderr, "Initial directory push failed!\n");
    return -3;
  }

  if(openDir(path, &curr_dir, &curr_file)) {
    fprintf(stderr, "Directory open failed!\n");
    return -4;
  }
  
  if(tmpnam(ctrl_file_name) == NULL)
  {
    fprintf(stderr, "Attempt to allocate tmpnam() for CONTROL file failed!\n");
    return -5;
  }
  
  ctrl_file = fopen(ctrl_file_name, "wb+");
  if(ctrl_file == NULL)
  {
    fprintf(stderr, "Attempt to open CONTROL file failed!\n");
    return -6;
  }
  
  initCtrlFile(ctrl_file, 0, 0, path);
  
  /* Initialize the "root" dir entry... relative to the 
  true root of course :P. No need- implied by ctrl file... */ 
  /* addDirEntry(ctrl_file, "\\", &curr_dir); */
  
  nw_rc = initRemote(parms);
  if(nw_rc != SUCCESS) {
    do_backup_rc = -7;
  }

  nw_rc = mkDirRemote(remote_name);
  if(!(nw_rc == SUCCESS || nw_rc == TARGET_EXIST)) {
    do_backup_rc = -8;
  }

  nw_rc = chDirRemote(remote_name);
  if(!(nw_rc == SUCCESS)) {
    do_backup_rc = -9;
  }


  while(!all_dirs_traversed && do_backup_rc == 0) {
    int retry_count = 0, chars_copied;

    assert(_heapchk() == _HEAPOK);
    chars_copied = snprintf(path_and_file, DIR_MAX_PATH + 1, "%s\\%s", path, curr_file.name);
    if(chars_copied >= DIR_MAX_PATH + 1) {
      traversal_error = 1;
      fprintf(stderr, "Directory traversal error, LINE %u!\n", __LINE__);
      do_backup_rc = -10;
      break;
    }
    //printf("Current Dir+file: %s\n", path_and_file);
    /* Strip the root from the path... the "+1" reflects
    that the NULL terminator of the root coincides with the path
    separator in path. */


    unix_path[0] = '\0';
    if(createUnixName(unix_path, &path[path_eostr + 1]) == NULL) {
      fprintf(stderr, "Unix directory name creation failed!\n");
      do_backup_rc = -11;
      break;
    }


    if(strlen(unix_path) == 0) { /* We are back at directory root if here */
      sprintf(unix_path_and_file, "%s", curr_file.name);
    }
    else {
      sprintf(unix_path_and_file, "%s/%s", unix_path, curr_file.name);
    }

    if(curr_file.attrib & _A_SUBDIR) {
      /* The two relative directories can be safely
      ignored. */
      if(!(strcmp(curr_file.name, ".") == 0 \
           || strcmp(curr_file.name, "..") == 0)) {
      
      /* The "file" we detected is actually a directory! */
        strcpy(path, path_and_file);
        
        /* Not a typo... include the leading separator, which is where
        the NULL terminator of the path root is. This is simply for reading
        purposes in the control file*/
        addDirEntry(ctrl_file, &path[path_eostr], &curr_dir);
        
        traversal_error = pushDir(&dstack, &curr_dir, path);
        if(traversal_error) {
          fprintf(stderr, "Directory traversal error, LINE %u!\n", __LINE__);
          do_backup_rc = -12;
          break;
        }

        if(openDir(path, &curr_dir, &curr_file)) {
          fprintf(stderr, "Directory traversal error, LINE %u!\n", __LINE__);
          do_backup_rc = -13;
          break;
        }

        fprintf(stderr, "Creating directory %s...\n", unix_path_and_file);
        do {
          if(retry_count) {
            fprintf(stderr, "Retrying operation...\n");
          }
          nw_rc = mkDirRemote(unix_path_and_file);
          retry_count++;
        }
        while(nw_rc != SUCCESS && retry_count <= 3);

        if(retry_count > 3) {
          do_backup_rc = -14;
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
      FILE * curr_fp;
      
      addFileEntry(ctrl_file, curr_file.name, &curr_file);
      curr_fp = fopen(path_and_file, "rb");
      if(curr_fp == NULL) {
        fprintf(stderr, "Read error on file: %s! Not continuing.", path_and_file);
        do_backup_rc = -15;
      }
      else {
        int8_t local_error = 0, send_done = 0;
        setvbuf(curr_fp, file_buffer, _IOFBF, FILE_BUF_SIZE);
        fprintf(stderr, "Storing file %s...\n", unix_path_and_file);
        
        while(!send_done && !local_error && retry_count <= 3) {
          int8_t send_remote_rc;
          if(retry_count) {
            fprintf(stderr, "Retrying operation...\n");
          }

          send_remote_rc = send_file(curr_fp, unix_path_and_file, out_buffer, OUTBUF_SIZE);
          switch(send_remote_rc) {
          case 0:
            send_done = 1;
            break;
          case -2:
            fprintf(stderr, "Read error on file: %s! Not continuing.", path_and_file);
            local_error = 1; /* Local file error. */
            break;
          case -1: /* Recoverable error. */
          default:
            break;
          }
          retry_count++;
        }

        if(local_error) { /* If file error, we need to break out. */
          do_backup_rc = -16;
        }
        else {
          if(retry_count > 3) {
            do_backup_rc = -17;
          }
          else {
            retry_count = 0;
          }
        }
      }
      fclose(curr_fp);
    }
    fprintf(stderr, "\n");

    /* Only perform another iteration of the file loop
    when there is in fact another file to send. If there are no
    files left, we know to break as well. The while loop should
    only execute when a parent directory has finished traversal at
    the same time as its child(ren). */
    if(!do_backup_rc) /* Don't bother if we got here and an error occured,
      we are about to break anyway. */
    {
      while(getNextFile(&curr_dir, &curr_file) && !all_dirs_traversed) {
        closeDir(&curr_dir);
        if(popDir(&dstack, &curr_dir, path)) {
          all_dirs_traversed = 1;
        }
        else
        {
          /* Only emit a CHDIR command while there are entries on the stack. */
          finalDirEntry(ctrl_file);
        }
      }
    }
  }

  if(do_backup_rc) {
    /* remove(ctrl_file_name); */
    fprintf(stderr, "Full backup failed with status code %d.\n", do_backup_rc);
  }
  else {
    /* Finalize the control file... */
    finalCtrlFile(ctrl_file);
    fclose(ctrl_file);
    
    /* And send it off! Failure to send is not fatal... as long as it's
    available prior to the restore. */
    fprintf(stderr, "Sending control file to the server (no retry)...\n");
    
    /* Assume this doesn't fail (for now) */
    ctrl_file = fopen(ctrl_file_name, "rb");
    setvbuf(ctrl_file, file_buffer, _IOFBF, FILE_BUF_SIZE);
    
    if(send_file(ctrl_file, "CONTROL.NFO", out_buffer, OUTBUF_SIZE))
    {
      fprintf(stderr, "Couldn't send control file (%s) to the server. You can " \
      	"send it manually using FTP later.\n", ctrl_file_name);
    }
    else
    {
      fprintf(stderr, "Control file sent successfully. A copy (%s) is " \
      	"being kept locally.\n", ctrl_file_name);
    }
    
    fclose(ctrl_file);
    fprintf(stderr, "Full backup completed successfully.\n");
  }

  closeRemote();
  return do_backup_rc;
}

signed char do_diff(nwBackupParms * parms, char * remote_name, char * local_dir, char * logfile) {
  char ctrl_file[L_tmpnam];
  return 0;
}

signed char do_restore(nwBackupParms * parms, char * remote_name, char * local_dir, char * logfile) {
  char ctrl_file_name[L_tmpnam];
  FILE * ctrl_file;
  dirStack_t dstack;
  dirStruct_t dummy_curr_dir;
  fileStruct_t dummy_curr_file;
  nwBackupCodes nw_rc = SUCCESS;
  int do_restore_rc = 0;
  int path_eostr;
  
  char current_file_name[FILENAME_MAX];
  char * path, * parsed_path, * path_and_file, * unix_path, \
    * unix_path_and_file, * file_buffer,  * in_buffer;
  uint8_t * ctrl_buffer;

  path = malloc(129 * 5);
  file_buffer = malloc(FILE_BUF_SIZE);
  in_buffer = malloc(OUTBUF_SIZE);
  ctrl_buffer = malloc(BUFSIZ); /* Should be more than enough... handle dynamically 
  sizing it later. */
  //dummy_outbuf = calloc(OUTBUF_SIZE, 1); //This doesn't set the first and third byte
  //to zero!
  if(path == NULL || file_buffer == NULL || in_buffer == NULL || ctrl_buffer == NULL) {
    fprintf(stderr, "Could not allocate memory for path or file buffers!\n");
    return -4;
  }
  
  
  /* Don't bother freeing till end of program- if error, program will terminate
  soon anyway! */
  parsed_path = path + 129;
  path_and_file = parsed_path + 129;
  unix_path = path_and_file + 129;
  unix_path_and_file = unix_path + 129;
  
  if(initDirStack(&dstack)) {
    fprintf(stderr, "Directory Stack Initialization failed!\n");
    return -1;
  }

  if(!strcpy(path, local_dir))
    /* if(strncpy(path, argv[1], DIR_MAX_PATH + 1) >= (DIR_MAX_PATH + 1)) */
  {
    /* Shouldn't fail... but Heartbleed convinces me to
    code defensively. */
    fprintf(stderr, "Specified path name is too long...\n"
            "Actually if we're got this error on DOS, we have more serious\n"
            "trouble than just a bad path!\n");
  }

  path_eostr = strlen(path);
  if(path[path_eostr - 1] == 92) { /* Backslash */
    path[path_eostr - 1] = '\0';
    local_dir[path_eostr - 1] = '\0'; /* We will need local dir again! */
    path_eostr--;
    /* path_eostr now points to the root path's NULL
    terminator. */
  }

  //full_unix_path = malloc(strlen(parms-> strlen(remote_name) + );

  /* If doing an absolute-path restore, the first push should be deferred until
  the control file header is read. */
  if(pushDir(&dstack, &dummy_curr_dir, path)) {
    fprintf(stderr, "Initial directory push failed!\n");
    return -2;
  }
  
  if(tmpnam(ctrl_file_name) == NULL)
  {
    fprintf(stderr, "Attempt to allocate tmpnam() for receiving CONTROL failed!\n");
    return -5;
  }
  
  ctrl_file = fopen(ctrl_file_name, "wb+");
  if(ctrl_file == NULL)
  {
    fprintf(stderr, "Attempt to open temp file for receiving CONTROL failed!\n");
    return -6;
  }
  
  nw_rc = initRemote(parms);
  if(nw_rc != SUCCESS) {
    do_restore_rc = -7;
  }

  nw_rc = chDirRemote(remote_name);
  if(!(nw_rc == SUCCESS)) {
    do_restore_rc = -8;
  }
  
  if(!do_restore_rc)
  {
    /* Grab the control file from the server */
    fprintf(stderr, "Receiving control file from the server (no retry)...\n");
    setvbuf(ctrl_file, file_buffer, _IOFBF, FILE_BUF_SIZE);
    
    if(restore_file(ctrl_file, "CONTROL.NFO", in_buffer, OUTBUF_SIZE))
    {
      fprintf(stderr, "Couldn't receive control file (%s) to the server. Supply"
      	"the control file manually (not implemented) and try again.\n", ctrl_file_name);
      do_restore_rc = -6;
    }
    else /* Control file was successfully grabbed. We can continue. */
    {
      ctrlEntryType_t entry_type;
      int8_t all_dirs_restored = 0;
      int k = 0;
      unsigned int attr, time, date;
      long unsigned int size;
      fprintf(stderr, "Control file received successfully from the server.\n");
      fclose(ctrl_file);
      /* Assume this doesn't fail for now */
      ctrl_file = fopen(ctrl_file_name, "rb");
      
      
      entry_type = getNextEntry(ctrl_file, ctrl_buffer, BUFSIZ); 
      while(!all_dirs_restored && do_restore_rc == 0)
      {
      	FILE * curr_file;
        int temp;
        switch(entry_type)
        {
        case CTRL_HEADER:
          /* For now, absolute-path restores are disabled. Restores
          are relative to the directory in which the program is invoked. 
          In effect, this code does nothing! */
          
          //parseHeaderEntry(ctrl_buffer, path);
          
          /* if(pushDir(&dstack, &dummy_curr_dir, path)) {
            fprintf(stderr, "Initial directory push failed!\n");
            do_restore_rc = -16;
          } */
          
          fprintf(stderr, "Root directory: %s\n", path);
          break;
          
        case CTRL_DIR:
          temp = parseDirEntry(ctrl_buffer, parsed_path, &attr);
          sprintf(path, "%s\\%s", local_dir, parsed_path);
          unix_path[0] = '\0';
          /* Skip the leading separator for now... */
          if(createUnixName(unix_path, &path[path_eostr + 2]) == NULL) {
            fprintf(stderr, "Unix directory name creation failed!\n");
            do_restore_rc = -11;
            break;
          } 
          fprintf(stderr, "Return code: %d Curr directory: %s, Attr: %hu\nUnix name: %s\n",\
            temp, path, attr, unix_path);
          getchar();
          break;
          
        case CTRL_FILE:
          /* Should not cause buffer overflow, since 
          sizeof(current_file_name) set to FILENAME_MAX */
          temp = parseFileEntry(ctrl_buffer, current_file_name, &attr, &time, &date, &size);
          
          /* Skip the leading separator for now... */
          if(strlen(path) == 0)
          {
            /* Don't copy a leading separator is the path is empty... */
            strncpy(path_and_file, current_file_name, FILENAME_MAX);
            strcpy(unix_path_and_file, path_and_file);
          }
          else
          {
            sprintf(path_and_file, "%s\\%s", path, current_file_name);
            sprintf(unix_path_and_file, "%s/%s", unix_path, current_file_name);
          }
          curr_file = fopen(path_and_file, "wb");
          fprintf(stderr, "Return code: %d Curr directory: %s, Attr: %hu, Time %hu, Date %hu, Size %lu\n" \
            "Unix directory: %s\n", temp, path_and_file, attr, time, date, size, unix_path_and_file);
          break;
          
        case CTRL_CDUP:
          /* Don't bother checking return code, as if the pop fails, the next
          entry should be end of file in a correctly-parsed control file. */
          //popDir(&dstack, &dummy_curr_dir, path);
          {
            /* Remove the deepest directory of the path, taking into account
            that the path separators are duplicated. */
            char * separator = strrchr(path, 92);
            * (separator - 1) = '\0';
          }
          
          //fprintf(stderr, "Pop dir stack\n");
          break;
          
        case CTRL_EOF:
          all_dirs_restored = 1;
          fprintf(stderr, "End of control file.\n");
          break;
        default:
          fprintf(stderr, "Unexpected data from control file!\n");
          do_restore_rc = -15;
          break;
        }     
        entry_type = getNextEntry(ctrl_file, ctrl_buffer, BUFSIZ);
      }     
    }
  }


  if(do_restore_rc)
  {
    fprintf(stderr, "Full restore failed with status code %d.\n", do_restore_rc);
  }
  else
  {
    fprintf(stderr, "Full restore completed successfully.\n");
  }
  
  fclose(ctrl_file);
  closeRemote();
  return do_restore_rc;
}



int8_t send_file(FILE * fp, char * remote_name, uint8_t * out_buffer, uint16_t payload_size) {
  nwFileHandle nwfp = NULL;
  nwBackupCodes open_rc;
  int8_t remote_rc = 0; /* Assume success... */

  open_rc = openRemoteFile(nwfp, remote_name, 0);

  if(!open_rc) { /* If we opened the file ok, read it to the remote */
    nwBackupCodes send_rc = 0;
    int8_t done_read = 0, local_file_error = 0;
    unsigned long total_size = 0, elapsed_time = 0, prev_elapsed = 0;

    start_timer();
    while(!done_read && (send_rc == SUCCESS || send_rc == TARGET_BUSY)  && !local_file_error) {
      uint16_t chars_read, actual_chars_sent;
      
      chars_read = fread(out_buffer, 1, payload_size, fp);
      //fprintf(stderr, "%d chars read\n", chars_read);

      if(chars_read < payload_size) { /* Send the remaining characters... */
        if(feof(fp)) {
          uint16_t chars_left = chars_read;
          uint16_t chars_read_since_eof = 0;
          //Race condition if (all these) fprintfs and sendDataRemote commented out.
          while((send_rc == SUCCESS || send_rc == TARGET_BUSY) && chars_left) {
            send_rc = sendDataRemote(nwfp, out_buffer + chars_read_since_eof, chars_left, &actual_chars_sent);
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
      else { /* We got a full buffer to send. */
        send_rc = sendDataRemote(nwfp, out_buffer, chars_read, &actual_chars_sent);
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
        }
      }
    }
    
    /* Cleanup logic scope block */
    {
      nwBackupCodes close_rc;
      if(!local_file_error) {
        /* We should close the file even if send_rc errored out! */
        close_rc = closeRemoteFile(nwfp);
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
        close_rc = closeRemoteFile(nwfp);
        remote_rc = -2; /* Do not retry transfer. */
      }
    }
  }
  else {
    remote_rc = -1;
  }

  return remote_rc;
}

int8_t restore_file(FILE * fp, char * remote_name, uint8_t * in_buffer, uint16_t payload_size)
{
  nwFileHandle nwfp = NULL;
  nwBackupCodes open_rc;
  int8_t remote_rc = 0; /* Assume success... */

  open_rc = openRemoteFile(nwfp, remote_name, 1);

  if(!open_rc) { /* If we opened the file ok, read it FROM the remote */
    //nwBackupCodes rcv_rc = 0;
    nwBackupCodes rcv_rc = TARGET_STILL_HAS_DATA;
    int8_t local_file_error = 0;
    unsigned long total_size = 0, elapsed_time = 0, prev_elapsed = 0;

    start_timer();
    while((rcv_rc == TARGET_STILL_HAS_DATA)  && !local_file_error) {
      uint16_t chars_written, actual_chars_rcvd;
      rcv_rc = rcvDataRemote(nwfp, in_buffer, payload_size, &actual_chars_rcvd);
      chars_written = fwrite(in_buffer, 1, actual_chars_rcvd, fp);
      
      if(chars_written < actual_chars_rcvd)
      {
      	local_file_error = 1;
      }
      else
      {
      	total_size += actual_chars_rcvd;
        elapsed_time = get_elapsed_time();
        if(elapsed_time - prev_elapsed > 1000) {
          prev_elapsed = elapsed_time;
          fprintf(stderr, "%lu total bytes sent... %lu B/sec\r", total_size, \
                  computeRate(total_size, elapsed_time));
        }
      }
    }
    
    /* Cleanup logic scope block */
    {
      nwBackupCodes close_rc;
      if(!local_file_error) {
        /* We should close the file even if send_rc errored out! */
        close_rc = closeRemoteFile(nwfp);
        if(!close_rc && !rcv_rc) {
          fprintf(stderr, "File receive okay... %lu total bytes received... %lu B/sec\n", total_size, \
                  computeRate(total_size, elapsed_time));
          remote_rc = 0; /* If we successfully sent the file, we are done. */
        }
        else {
          fprintf(stderr, "Open error code: %d, Rcv error code: %d, Close error code: %d\n", open_rc, rcv_rc, close_rc);
          remote_rc = -1; /* Worth retrying transfer. */
        }
      }
      else {
        close_rc = closeRemoteFile(nwfp);
        remote_rc = -2; /* Do not retry transfer. */
      }
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
