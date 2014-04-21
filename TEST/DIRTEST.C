#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "dir.h"

int main(int argc, char * argv[]) {
  dirStack_t dStack;
  dirStruct_t currDir;
  fileStruct_t currFile;
  int8_t rc;
  int8_t allDirsTraversed = 0;
  int8_t traversalError = 0;
  uint8_t i = 0, j = 0;
  int pathEostr;
  /* char driveLetter[4] = {'\0', ':', '\\', '\0'}; /* The drive letter has to be handled separately
  		unfortunately... the drive letter must include the trailing
  		backslash, while all other path components do not/must not. */

  int charsCopied;
  char path[129];
  char unixPath[129];

  if(argc < 2) {
    fprintf(stderr, "Please specify a directory name!\n");
    return EXIT_FAILURE;
  }

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
    fprintf(stderr, "Directory Stack Initialization failed!\n");
    return EXIT_FAILURE;
  }

  if(!strcpy(path, argv[1]))
    /* if(strncpy(path, argv[1], DIR_MAX_PATH + 1) >= (DIR_MAX_PATH + 1)) */
  {
    /* Shouldn't fail... but Heartbleed convinces me to
    code defensively. */
    fprintf(stderr, "Specified path name is too long...\n"
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

  if(traversalError = pushDir(&dStack, &currDir, path)) {
    fprintf(stderr, "Initial directory push failed!\n");
    return EXIT_FAILURE;
  }

  if(openDir(path, &currDir, &currFile)) {
    fprintf(stderr, "Directory open failed!\n");
    return EXIT_FAILURE;
  }

  do {
    printf("Current Dir+file: %s\\%s\n", path, currFile.name);
    /* Strip the root from the path... the "+1" reflects
    that the NULL terminator of the root coincides with the path
    separator in path. */
    if(createUnixName(unixPath, &path[pathEostr + 1]) == NULL) {
      fprintf(stderr, "Unix directory name creation failed!\n");
      return EXIT_FAILURE;
    }
    printf("  Unix path: %s/%s\n", unixPath, currFile.name);
    if(currFile.attrib == (currFile.attrib & _A_SUBDIR)) {
      /* The two relative directories can be safely
      ignored. */
      if(!(strcmp(currFile.name, ".") == 0 \
           || strcmp(currFile.name, "..") == 0)) {
        charsCopied = snprintf(path, DIR_MAX_PATH + 1, \
                               "%s\\%s", path, currFile.name);
        if(charsCopied >= DIR_MAX_PATH + 1) {
          traversalError = 1;
          fprintf(stderr, "Directory traversal error, LINE %u!\n", __LINE__);
          break;
        }

        traversalError = pushDir(&dStack, &currDir, path);
        if(traversalError) {
          fprintf(stderr, "Directory traversal error, LINE %u!\n", __LINE__);
          break;
        }

        if(openDir(path, &currDir, &currFile)) {
          fprintf(stderr, "Directory traversal error, LINE %u!\n", __LINE__);
          break;
        }
        fprintf(stderr, "\n");
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
    else {
      printf("  Current File: %s\n", currFile.name);
    }
    /* Else do nothing for now... */

    /* Only perform another iteration of the file loop
    when there is in fact another file to send. If there are no
    files left, we know to break as well. The while loop should
    only execute when a parent directory has finished traversal at
    the same time as its child(ren). */
    //j = 0;
    while(getNextFile(&currDir, &currFile) && !allDirsTraversed) {
      /* if(j > 0)
      {
      	fprintf(stderr, "Multiple dirs closed in one"
      		"iteration. Check if OK, then press any key...\n");
      	getchar();
      } */

      closeDir(&currDir);
      if(popDir(&dStack, &currDir, path)) {
        allDirsTraversed = 1;
        /* break; */
      }
      //j++;
    }
  }
  while(!allDirsTraversed);

  fprintf(stderr, "Directory traversal completed successfully.\n");
  return EXIT_SUCCESS;

}
