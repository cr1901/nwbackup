#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>

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


int8_t initCtrlFile(FILE * ctrlFile, int backupType, int FsType, char * rootDir) {
  uint8_t count;
  filenameBuf = malloc(256);
  if(filenameBuf == NULL) {
    return -1;
  }
  if(expandBackslashes(filenameBuf, rootDir, 256) >= 256) {
    return -2;
  }

  rewind(ctrlFile);
  fprintf(ctrlFile, "h,%s,%s,%s,%s,%s,%s\n", "0.8", newLineTypes[1], nwbTypes[backupType], "cp437", \
          fsTypes[FsType], filenameBuf);
  return 0;
}

int8_t addDirEntry(FILE * ctrlFile, char * name, dirStruct_t * d) {
  if(expandBackslashes(filenameBuf, name, 256) >= 256) {
    return -1;
  }
  /* If no files, two spaces ensures the leading bracket isn't
  overwritten. */
  fprintf(ctrlFile, "d,%s,%hu,%hu,%hu,%lu,%s\n", filenameBuf, d->attrib, \
          d->wr_time, d->wr_date, d->size, "[]");
  return 0;
}

int8_t addFileEntry(FILE * ctrlFile, char * name, fileStruct_t * f) {
  if(expandBackslashes(filenameBuf, name, 256) >= 256) {
    return -1;
  }
  fprintf(ctrlFile, "f,%s,%hu,%hu,%hu,%lu,%s\n", filenameBuf, (unsigned short) f->attrib, \
          f->wr_time, f->wr_date, f->size, "[]");
  return 0;
}


int8_t finalDirEntry(FILE * ctrlFile) {
  return (fputs("c\n", ctrlFile) >= 0) ? 0 : -1;
}


int8_t finalCtrlFile(FILE * ctrlFile) {
  return (fputs("e\n", ctrlFile) >= 0) ? 0 : -1;
}

/* int8_t initParser(ctrlParser_t * parser, FILE * ctrlFile, uint8_t * fileBuf, uint16_t fileBufSiz)
{
  uint16_t charsRead;
  int currTokensReq, maxTokensReq = 0, i = 0;
  jsmn_init(&(parser->parserState));
  if(parser == NULL)
  {
    return -1;
  }

  rewind(ctrlFile);

  do{
    charsRead = fread(fileBuf, 1, fileBufSiz, ctrlFile);
    currTokensReq = jsmn_parse(&(parser->parserState), fileBuf, charsRead, NULL, 10);
    if(maxTokensReq < currTokensReq)
    {
      maxTokensReq = currTokensReq;
    }
  fprintf(stderr, "fileBuf (%d bytes) contains %d tokens.\n", fileBufSiz, currTokensReq);
  }while(charsRead == fileBufSiz);

  fprintf(stderr, "maxTokensReq: %d.\n", maxTokensReq);
  parser->tokens = malloc(maxTokensReq * sizeof(jsmntok_t));
  if(parser->tokens == NULL)
  {
    return -2;
  }

  rewind(ctrlFile);
  jsmn_init(&(parser->parserState));
  do{
  charsRead = fread(fileBuf, 1, fileBufSiz, ctrlFile);
  jsmn_parse(&(parser->parserState), fileBuf, charsRead, parser->tokens, maxTokensReq);
  fprintf(stderr, "currTokensReq: %d.\n", currTokensReq);
  for(i = 0; i < (parser->parserState).toknext; i++)
  {
    if((parser->tokens[i]).end < fileBufSiz)
    {
      _write(1, &fileBuf[(parser->tokens[i]).start], (parser->tokens[i]).end - (parser->tokens[i]).start);
    }
    getchar();
  }
  }while(charsRead == fileBufSiz);

  if(!feof(ctrlFile))
  {
    return -2;
  }

  return 0;
} */

ctrlEntryType_t getNextEntry(FILE * ctrlFile, uint8_t * fileBuf, uint16_t fileBufSize) {
  /* CTRL_INCOMPLETE is not appropriate for this data format, but it is included
  for future considerations. */
  ctrlEntryType_t entryRc;
  char * fgetsRc;
  fgetsRc = fgets(fileBuf, fileBufSize, ctrlFile);

  if(fgetsRc == NULL) {
    entryRc = CTRL_FAIL;
  }
  else {
    switch(fileBuf[0]) {
    case 'h':
      entryRc = CTRL_HEADER;
      break;
    case 'd':
      entryRc = CTRL_DIR;
      break;
    case 'f':
      entryRc = CTRL_FILE;
      break;
    case 'c':
      entryRc = CTRL_CDUP;
      break;
    case 'e':
      entryRc = CTRL_EOF;
      break;
    default:
      entryRc = CTRL_FAIL;
      break;
    }
  }
  return entryRc;
}

int8_t parseHeaderEntry(uint8_t * ctrlFileBuf, char * rootPath) {
  char * lastDelim;
  lastDelim = strrchr(ctrlFileBuf, ',');
  strcpy(rootPath, lastDelim + 1);
  return 0;
}

int8_t parseDirEntry(uint8_t * ctrlFileBuf, char * pathName, unsigned int * attr, \
                     unsigned int * time, unsigned int * date, unsigned long * size) {
  return parseFileEntry(ctrlFileBuf, pathName, attr, time, date, size);
}

int8_t parseFileEntry(uint8_t * ctrlFileBuf, char * pathName, unsigned int * attr, \
                      unsigned int * time, unsigned int * date, unsigned long * size) {
  int i = 0;
  while(ctrlFileBuf[i] != ',') {
    i++;
  }

  i++; /* Go past the comma. */
  {
    int j = 0;
    while(ctrlFileBuf[i] != ',') {
      pathName[j] = ctrlFileBuf[i];
      i++;
      j++;
    }
    pathName[j] = 0;
  }

  i++; /* Go past the comma... again! */
  return (sscanf(&ctrlFileBuf[i], "%hu,%hu,%hu,%lu", attr, time, date, size) == 4) ? 0 : -1;
}






/* A well formed MS-DOS path is expected here. This function should NOT
be used in contexts where escape sequences and backslashes are intermixed. */
static int expandBackslashes(char * destStr, char * srcStr, int maxChars) {
  int count;
  uint8_t numBackslashes = 0;
  for(count = 0; ((count < maxChars) && (srcStr[count] != '\0')); count++) {
    if(srcStr[count] == 92) {
      destStr[count + numBackslashes] = 92;
      numBackslashes++;
    }
    destStr[count + numBackslashes] = srcStr[count];
  }
  destStr[count + numBackslashes] = '\0';

  return count >= maxChars ? -1 : count;
}
