#include <stdio.h>
#include <stdlib.h>

#include "control.h"
#include "dir.h"
/* typedef enum controlRc
{
  
  
}controlRc_t; */


static char * initStrings[] = {"type", "encoding", "origin-fs", "directory-root"};
static char * newLineTypes[] = {"unix", "dos", "mac"};
static char * nwbTypes[] = {"full", "diff"};
static char * fsTypes[] = {"fat16", "fat32"};


static char * filenameBuf;
static int expandBackslashes(char * destStr, char * srcStr, int maxChars);


int8_t initCtrlFile(FILE * ctrlFile, int backupType, int FsType, char * rootDir)
{
  uint8_t count;
  filenameBuf = malloc(256);
  if(filenameBuf == NULL)
  {
    return -1;
  }
  
  rewind(ctrlFile);
  fputc('[', ctrlFile);
  fprintf(ctrlFile, "\"%s\", {", newLineTypes[1]);
  fprintf(ctrlFile, "\"%s\": \"%s\", ", "type", nwbTypes[backupType]);
  fprintf(ctrlFile, "\"%s\": \"%s\", ", "encoding", "cp437");
  fprintf(ctrlFile, "\"%s\": \"%s\", ", "origin-fs", fsTypes[FsType]);
  if(expandBackslashes(filenameBuf, rootDir, 256) >= 256)
  {
    return -2;
  }
  fprintf(ctrlFile, "\"%s\": \"%s\"}, ", "directory-root", filenameBuf);
  fputc('[', ctrlFile);
  return 0;
}

int8_t addDirEntry(FILE * ctrlFile, char * name, dirStruct_t * d)
{
  fputc('[', ctrlFile);
  if(expandBackslashes(filenameBuf, name, 256) >= 256)
  {
    return -1;
  }
  /* If no files, two spaces ensures the leading bracket isn't
  overwritten. */
  fprintf(ctrlFile, "\"%s\", %d, [  ", filenameBuf, d->attrib);
  return 0;
}

int8_t addFileEntry(FILE * ctrlFile, char * name, fileStruct_t * f)
{
  fputc('{', ctrlFile);
  if(expandBackslashes(filenameBuf, name, 256) >= 256)
  {
    return -1;
  }
  fprintf(ctrlFile, "\"%s\": \"%s\", ", "name", filenameBuf);
  fprintf(ctrlFile, "\"%s\": %hu, ", "attr", (unsigned short) f->attrib);
  fprintf(ctrlFile, "\"%s\": %hu, ", "mod-time", f->wr_time);
  fprintf(ctrlFile, "\"%s\": %hu, ", "mod-date", f->wr_date);
  fprintf(ctrlFile, "\"%s\": %lu, ", "size", f->size);
  fprintf(ctrlFile, "\"%s\": []", "copies");
  fputs("}, ", ctrlFile);
  return 0;
}


int8_t closeDirEntry(FILE * ctrlFile)
{
  fseek(ctrlFile, -2, SEEK_CUR);
  return (fputs("]], ", ctrlFile) >= 0) ? 0 : -1; 
}


int8_t finalCtrlFile(FILE * ctrlFile)
{
  fseek(ctrlFile, -2, SEEK_CUR);
  return (fputs("]]", ctrlFile) >= 0) ? 0 : -1;
}




/* A well formed MS-DOS path is expected here. This function should NOT
be used in contexts where escape sequences and backslashes are intermixed. */
static int expandBackslashes(char * destStr, char * srcStr, int maxChars)
{
  int count;
  uint8_t numBackslashes = 0;
  for(count = 0; ((count < maxChars) && (srcStr[count] != '\0')); count++)
  {
    if(srcStr[count] == 92)
    {
      destStr[count + numBackslashes] = 92;
      numBackslashes++;
    }
    destStr[count + numBackslashes] = srcStr[count]; 
  }
  destStr[count + numBackslashes] = '\0';
  
  return count >= maxChars ? -1 : count; 
}
