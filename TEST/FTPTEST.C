#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "nwbackup.h"

/* Was causing a nice big fat stack overflow keeping these local! :) */
char serverPath[129];
char serverFilePath[129];

int main(int argc, char * argv[]) {
  struct mTCPBackupParms nwParms;
  nwBackupCodes rc;
  nwFileHandle nwFp = NULL;
  char tempfile[L_tmpnam];
  FILE * fp;
  char temp_format_str[] = "This implementation of C stdlib supports up to %u temp files.\n" \
                           "Temp filenames can be up to %u bytes in length.\n" \
                           "The current temp file's name is: %s\n";
  unsigned short chars_written = 0;
  long int temp_file_position = 0L;
  long int chars_read = 0;
  unsigned short short_chars_read = 0;
  char * retr_buffer;


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

  rc = chDirRemote(argv[1]);
  fprintf(stderr, "Chdir return code was: %d\n", rc);

  rc = openRemoteFile(nwFp, tempfile, 0);
  fprintf(stderr, "Open TX return code was: %d, void pointer lives at: %Fp\n", rc, nwFp);
  sprintf(serverFilePath, "%s/%s", argv[1], tempfile);

  //fprintf(stderr, "Aborting Xfer...\n");
  //abortXfer();

  fprintf(stderr, "Storing %s to %s on server...\n", tempfile, argv[1]);
  rc = sendDataRemote(nwFp, (uint8_t *) temp_format_str, sizeof(temp_format_str), &chars_written);
  fprintf(stderr, "Store return code was: %d. %d bytes were sent.\n", rc, chars_written);

  rc = closeRemoteFile(nwFp);
  fprintf(stderr, "%s was closed on the remote side\n", tempfile);


  rc = chDirRemote("..");
  fprintf(stderr, "Chdir up return code was: %d\n", rc);

  rc = openRemoteFile(nwFp, serverFilePath, 1);
  fprintf(stderr, "Open RX return code was: %d, void pointer lives at: %Fp\n", rc, nwFp);

  retr_buffer = malloc(sizeof(temp_format_str));
  rc = rcvDataRemote(nwFp, (uint8_t *) retr_buffer, sizeof(temp_format_str), &short_chars_read);
  fprintf(stderr, "Receive return code was: %d. %d bytes were sent.\n", rc, short_chars_read);
  fprintf(stderr, "Message received: %s\n", retr_buffer);

  closeRemote();

  free(retr_buffer);
  remove(tempfile);
  fprintf(stderr, "A temp file was removed: %s\n", tempfile);

  return 0;
}
