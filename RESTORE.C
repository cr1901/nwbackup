#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <direct.h>
#include <dos.h>
#include <fcntl.h>

#include "backend.h"
#include "frontend.h"
#include "dir.h"
#include "control.h"



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
  char * path, * path_and_file, * unix_path, \
  * unix_path_and_file, * file_buffer,  * in_buffer;
  uint8_t * ctrl_buffer;

  path = malloc(129 * 4);
  file_buffer = malloc(FILE_BUF_SIZE);
  in_buffer = malloc(OUTBUF_SIZE);
  ctrl_buffer = malloc(BUFSIZ); /* Should be more than enough... handle dynamically
  sizing it later. */
  //dummy_outbuf = calloc(OUTBUF_SIZE, 1); //This doesn't set the first and third byte
  //to zero!
  if(path == NULL || file_buffer == NULL || in_buffer == NULL || ctrl_buffer == NULL) {
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
    return -3;
  }

  if(tmpnam(ctrl_file_name) == NULL) {
    fprintf(stderr, "Attempt to allocate tmpnam() for receiving CONTROL failed!\n");
    return -4;
  }

  ctrl_file = fopen(ctrl_file_name, "wb+");
  if(ctrl_file == NULL) {
    fprintf(stderr, "Attempt to open temp file for receiving CONTROL failed!\n");
    return -5;
  }

  nw_rc = initRemote(parms);
  if(nw_rc != SUCCESS) {
    do_restore_rc = -6;
  }

  nw_rc = chDirRemote(remote_name);
  if(!(nw_rc == SUCCESS)) {
    do_restore_rc = -7;
  }

  if(!do_restore_rc) {
    /* Grab the control file from the server */
    fprintf(stderr, "Receiving control file from the server (no retry)...\n");
    setvbuf(ctrl_file, file_buffer, _IOFBF, FILE_BUF_SIZE);

    if(restore_file(ctrl_file, "CONTROL.NFO", in_buffer, OUTBUF_SIZE)) {
      fprintf(stderr, "Couldn't receive control file (%s) to the server. Supply"
              "the control file manually (not implemented) and try again.\n", ctrl_file_name);
      do_restore_rc = -8;
    }
    else { /* Control file was successfully grabbed. We can continue. */
      ctrlEntryType_t entry_type;
      int8_t all_dirs_restored = 0;
      unsigned int attr, time, date;
      long unsigned int size;
      fprintf(stderr, "Control file received successfully from the server.\n");
      fclose(ctrl_file);
      /* Assume this doesn't fail for now */
      ctrl_file = fopen(ctrl_file_name, "rb");


      entry_type = getNextEntry(ctrl_file, ctrl_buffer, BUFSIZ);
      while(!all_dirs_restored && do_restore_rc == 0) {
        FILE * curr_file;
        int temp;
        switch(entry_type) {
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
          temp = parseDirEntry(ctrl_buffer, current_file_name, &attr, &time, &date, &size);
          /* Change to snprintf soon */
          sprintf(path_and_file, "%s\\%s", path, current_file_name);
          strcpy(path, path_and_file);
          unix_path[0] = '\0';

          /* Skip the leading separator for now... */
          if(createUnixName(unix_path, &path[path_eostr + 1]) == NULL) {
            fprintf(stderr, "Unix directory name creation failed!\n");
            do_restore_rc = -9;
            break;
          }

          /* fprintf(stderr, "Return code: %d Curr directory: %s, Attr: %hu\nUnix name: %s\n",\
            temp, path, attr, unix_path); */

          if(_mkdir(path)) {
            fprintf(stderr, "Directory creation failed (%s)!\n", path);
            do_restore_rc = -10;
          }
          else {
            int dos_handle;
            fprintf(stderr, "Directory created: %s\n", path);
            if(_dos_open(path, O_RDONLY, &dos_handle)) {
              fprintf(stderr, "Warning: Could not open directory to set attributes!\n");
            }
            else {
              if(_dos_setftime(dos_handle, date, time)) {
                fprintf(stderr, "Warning: Could not reset date/time on directory %s!\n", path);
              }
              _dos_close(dos_handle);
              if(_dos_setfileattr(path_and_file, attr)) {
                fprintf(stderr, "Warning: Could not set attributes on directory %s!\n", path);
              }
            }
          }
          //getchar();
          break;

        case CTRL_FILE:
          /* Should not cause buffer overflow, since
          sizeof(current_file_name) set to FILENAME_MAX */
          temp = parseFileEntry(ctrl_buffer, current_file_name, &attr, &time, &date, &size);

          /* Skip the leading separator for now... */
          sprintf(path_and_file, "%s\\%s", path, current_file_name);
          if(!strcmp(path, local_dir)) {
            /* Don't copy a separator if the path is
            at the root... otherwise the server won't find the file and/or
            think the file is at the server's root! */
            strcpy(unix_path_and_file, current_file_name);
          }
          else {
            sprintf(unix_path_and_file, "%s/%s", unix_path, current_file_name);
          }

          /* fprintf(stderr, "Return code: %d Curr directory: %s, Attr: %hu, Time %hu, Date %hu, Size %lu\n" \
             "Unix directory: %s\n", temp, path_and_file, attr, time, date, size, unix_path_and_file); */

          /* Receive file scope block. */
          {
            int retry_count = 0;
            int8_t local_error = 0, rcv_done = 0;
            fprintf(stderr, "Receiving file %s...\n", unix_path_and_file);

            while(!rcv_done && !local_error && retry_count <= 3) {
              int8_t rcv_remote_rc;
              if(retry_count) {
                fprintf(stderr, "Retrying operation... (%d)\n", rcv_remote_rc);
              }

              curr_file = fopen(path_and_file, "wb");
              setvbuf(curr_file, file_buffer, _IOFBF, FILE_BUF_SIZE);
              rcv_remote_rc = restore_file(curr_file, unix_path_and_file, in_buffer, OUTBUF_SIZE);
              //rcv_remote_rc = 0;
              fclose(curr_file); /* Close the file no matter what */

              switch(rcv_remote_rc) {
              case 0:
                rcv_done = 1;
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
              do_restore_rc = -11;
            }
            else if(retry_count > 3) {
              do_restore_rc = -12;
            }
            else { /* File receive ok, try setting attributes now. */
              int dos_handle;
              if(_dos_open(path_and_file, O_RDONLY, &dos_handle)) {
                fprintf(stderr, "Warning: Could not open file to set attributes!\n");
              }
              else {
                if(_dos_setftime(dos_handle, date, time)) {
                  fprintf(stderr, "Warning: Could not reset date/time on file %s!\n", path_and_file);
                }
                _dos_close(dos_handle);
                if(_dos_setfileattr(path_and_file, attr)) {
                  fprintf(stderr, "Warning: Could not set attributes on file %s!\n", path_and_file);
                }
              }
            }
          }

          break;

        case CTRL_CDUP:
          /* Remove the deepest directory of the path, as long as we
          are not back at the invocation directory. */
          //fprintf(stderr, "CDUP occurred.\n");
          if(strcmp(path, local_dir)) {
            char * separator = strrchr(path, 92);
            if(separator != NULL) {
              /* Two characters need to be set to null because */
              (* separator) = '\0';
              //fprintf(stderr, "Path was stripped. );
            }

            fprintf(stderr, "Directory change. New path is: %s\n", path);
            /* Skip the leading separator for now... we need to recreate
            the unix path in case a directory does not follow next! */
            if(createUnixName(unix_path, &path[path_eostr + 1]) == NULL) {
              fprintf(stderr, "Unix directory name creation failed!\n");
              do_restore_rc = -13;
              break;
            }

            //getchar();
          }

          break;

        case CTRL_EOF:
          all_dirs_restored = 1;
          fprintf(stderr, "End of control file.\n");
          break;
        default:
          fprintf(stderr, "Unexpected data from control file!\n");
          do_restore_rc = -14;
          break;
        }
        entry_type = getNextEntry(ctrl_file, ctrl_buffer, BUFSIZ);
        fprintf(stderr, "\n");
      }
    }
  }


  if(do_restore_rc) {
    fprintf(stderr, "Full restore failed with status code %d.\n", do_restore_rc);
  }
  else {
    fprintf(stderr, "Full restore completed successfully.\n");
  }

  fclose(ctrl_file);
  closeRemote();
  remove(ctrl_file_name);
  return do_restore_rc;
}


int8_t restore_file(FILE * fp, char * remote_name, uint8_t * in_buffer, uint16_t payload_size) {
  nwFileHandle nwfp = NULL;
  nwBackupCodes open_rc;
  int8_t remote_rc = 0; /* Assume success... */

  open_rc = openRemoteFile(nwfp, remote_name, 1);

  if(!open_rc) { /* If we opened the file ok, read it FROM the remote */
    //nwBackupCodes rcv_rc = 0;
    nwBackupCodes rcv_rc = TARGET_STILL_HAS_DATA;
    int8_t local_file_error = 0;
    unsigned long total_size = 0, elapsed_time = 0, prev_elapsed = 0;

    startTimer();
    while((rcv_rc == TARGET_STILL_HAS_DATA)  && !local_file_error) {
      uint16_t chars_written, actual_chars_rcvd;
      rcv_rc = rcvDataRemote(nwfp, in_buffer, payload_size, &actual_chars_rcvd);
      chars_written = fwrite(in_buffer, 1, actual_chars_rcvd, fp);

      if(chars_written < actual_chars_rcvd) {
        local_file_error = 1;
      }
      else {
        total_size += actual_chars_rcvd;
        elapsed_time = getElapsedTime();
        if(elapsed_time - prev_elapsed > 1000) {
          prev_elapsed = elapsed_time;
          fprintf(stderr, "%lu total bytes sent... %lu B/sec\r", total_size, \
                  computeRate(total_size, elapsed_time));
        }
      }
    }

    //fprintf(stderr, "We got here...\n");

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
        fprintf(stderr, "There was a local file error...\n");
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
