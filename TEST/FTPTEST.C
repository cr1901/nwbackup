#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "nwbackup.h"


int main(int argc, char * argv[]) {
  struct mTCPBackupParms nwParms;
  int rc;
  char tempfile[L_tmpnam];
  FILE * fp;
  char serverPath[129];
  char serverFilePath[129];
  char temp_format_str[] = "This implementation of C stdlib supports up to %u temp files.\n" \
                           "Temp filenames can be up to %u bytes in length.\n" \
                           "The current temp file's name is: %s\n";
  int chars_written;
  long int temp_file_position = 0L;
  long int chars_read = 0;


  if(argc < 2) {
    fprintf(stderr, "Please specify a backup directory name for the server!\n");
    return EXIT_FAILURE;
  }


  tmpnam(tempfile);
  if(tempfile == NULL) {
    perror("Attempt to get temporary file failed!\n");
    return EXIT_FAILURE;
  }

  fp = fopen(tempfile, "w+b");
  if(fp == NULL) {
    perror("Temp file fopen failure");
    return EXIT_FAILURE;
  }

  /* Not checking return values here... unlikely to fail, and the chance
  of catastrophe is low... */
  fprintf(stderr, "A temp file was created: %s\n", tempfile);
  fprintf(fp, temp_format_str, BUFSIZ, TMP_MAX, L_tmpnam, tempfile);
  fclose(fp);


  rc = snprintf(nwParms.backupDirName, 80, "%s", argv[1]);
  if(rc >= 80) {
    fprintf(stderr, "Backup name too long!\n");
    return EXIT_FAILURE;
  }

  fprintf(stderr, "Begin FTP test...\n", serverPath);
  rc = initRemote(&nwParms);
  fprintf(stderr, "Init return code was: %d\n", rc);

  rc = mkDirRemote(argv[1]);
  fprintf(stderr, "Mkdir return code was: %d\n", rc);

  sprintf(serverFilePath, "%s/%s", argv[1], tempfile);
  fprintf(stderr, "Storing %s from %s...\n", serverFilePath, tempfile);

  rc = openAndSendFile(serverFilePath, tempfile);
  fprintf(stderr, "Store return code was: %d\n", rc);


  closeRemote();

  remove(tempfile);
  fprintf(stderr, "A temp file was removed: %s\n", tempfile);

  return 0;
}
