#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "control.h"

/* Data serialization test- create a backup control file and read it's 
parameters for each file. */

int main(int argc, char * argv[])
{
  FILE * fp;
  dirStack_t dStack;
  dirStruct_t currDir;
  fileStruct_t currFile;
  int8_t allDirsTraversed = 0;
  int pathEostr;
  char path[129];
  char path_and_file[129];
  int8_t traversalError = 0;
  int charsCopied;
  
  
  if(argc < 3) {
    fprintf(stderr, "Please specify a directory name and an output file!\n");
    return EXIT_FAILURE;
  }
  
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

  if(pushDir(&dStack, &currDir, path)) {
    fprintf(stderr, "Initial directory push failed!\n");
    return EXIT_FAILURE;
  }

  if(openDir(path, &currDir, &currFile)) {
    fprintf(stderr, "Directory open failed!\n");
    return EXIT_FAILURE;
  }
  
  /* Main tests begin here */
  fp = fopen(argv[2], "wb+");
  if(fp == NULL)
  {
    fprintf(stderr, "Output file open failed!\n");
    return EXIT_FAILURE;
  }
  
  initCtrlFile(fp, 0, 0, path);
  
  /* Initialize the "root" dir entry... relative to the 
  true root of course :P. */
  addDirEntry(fp, "\\", &currDir);
  
  do {
    if((currFile.attrib & _A_SUBDIR)) {
      /* The two relative directories can be safely
      ignored. */
      if(!(strcmp(currFile.name, ".") == 0 \
           || strcmp(currFile.name, "..") == 0)) {
        /* Undefined behavior? */
        charsCopied = snprintf(path, DIR_MAX_PATH + 1, \
                               "%s\\%s", path, currFile.name);
        
        if(charsCopied >= DIR_MAX_PATH + 1) {
          traversalError = 1;
          fprintf(stderr, "Directory traversal error, LINE %u!\n", __LINE__);
          break;
        }
        
        /* Not a typo... include the leading separator, which is where
        the NULL terminator of the path root is. This is simply for reading
        purposes in the control file*/
        addDirEntry(fp, currFile.name, &currDir);

        traversalError = pushDir(&dStack, &currDir, path);
        if(traversalError) {
          fprintf(stderr, "Directory traversal error, LINE %u!\n", __LINE__);
          break;
        }
        
        if(openDir(path, &currDir, &currFile)) {
          fprintf(stderr, "Directory traversal error, LINE %u!\n", __LINE__);
          perror("Reason");
          break;
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
    else {
      addFileEntry(fp, currFile.name, &currFile);
    }

    /* Only perform another iteration of the file loop
    when there is in fact another file to send. If there are no
    files left, we know to break as well. The while loop should
    only execute when a parent directory has finished traversal at
    the same time as its child(ren). */
    //j = 0;
    //getchar();
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
      else
      {
      	/* Don't close the root entry automatically- that is the job of
      	finalizing the control file. */
      	finalDirEntry(fp);
      }
      //j++;
    }
  }
  while(!allDirsTraversed);
  finalCtrlFile(fp);
  fclose(fp);
  freeDirStack(&dStack);
  
  
  fprintf(stderr, "Control file successfully created. Press a key to parse...\n");
  getchar();
  
  {
    ctrlEntryType_t entryType;
    uint8_t * ctrlBuffer;
    int k = 0;
    unsigned int attr, time, date;
    long unsigned int size;
    
    fp = fopen(argv[2], "rb+");
    if(fp == NULL)
    {
      fprintf(stderr, "Output file open failed (2)!\n");
      return EXIT_FAILURE;
    }
    
    ctrlBuffer = malloc(BUFSIZ);
    if(ctrlBuffer == NULL)
    {
      fprintf(stderr, "Buffer allocation failed!\n");
      return EXIT_FAILURE;
    }
    entryType = getNextEntry(fp, ctrlBuffer, BUFSIZ); 
    while(entryType != CTRL_EOF && entryType != CTRL_FAIL)
    {
      int temp;
      //fprintf(stderr, "%d: %d,%s", k, entryType, ctrlBuffer);
      switch(entryType)
      {
      case CTRL_HEADER:
      	parseHeaderEntry(ctrlBuffer, path);
      	fprintf(stderr, "Root directory: %s\n", path);
      	break;
      case CTRL_DIR:
	temp = parseDirEntry(ctrlBuffer, path, &attr);
      	fprintf(stderr, "Return code: %d Curr directory: %s, Attr: %hu\n", temp, path, attr);
      	break;
      case CTRL_FILE:
      	temp = parseFileEntry(ctrlBuffer, path, &attr, &time, &date, &size);
      	fprintf(stderr, "Return code: %d Curr directory: %s, Attr: %hu, Time %hu, Date %hu, Size %lu\n", \
      	  temp, path, attr, time, date, size);
      	break;
      }
            
      entryType = getNextEntry(fp, ctrlBuffer, BUFSIZ); 
      k++;
    }
    
    //initParser(&parser, fp, ctrlBuffer, BUFSIZ);
    
    
    free(ctrlBuffer);
  }
  
  
  return EXIT_SUCCESS;
}
