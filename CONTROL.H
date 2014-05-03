#include <stdio.h>

#include "jsmn.h"

/* Although there is nothing preventing a user from changing control functions
to use different input data types, since dirStruct and fileStruct are already
abstracted, I see no harm in using them to create control files. */
#include "dir.h"

/* typedef struct jsmnCtrlParser
{
  jsmn_parser parserState;
  jsmntok_t * tokens;
  uint16_t numTokens;
}ctrlParser_t; */

typedef enum ctrlEntry {
  CTRL_FAIL = -2, CTRL_INCOMPLETE = -1, CTRL_SUCCESS,
  CTRL_HEADER, CTRL_DIR, CTRL_FILE, CTRL_CDUP, CTRL_EOF
} ctrlEntryType_t;

int8_t initCtrlFile(FILE * ctrlFile, int backupType, int FsType, char * rootDir);
int8_t addDirEntry(FILE * ctrlFile, char * name, dirStruct_t * d);
int8_t addFileEntry(FILE * ctrlFile, char * name, fileStruct_t * f);
int8_t finalDirEntry(FILE * ctrlFile);
int8_t finalCtrlFile(FILE * ctrlFile);

//int8_t initParser(ctrlParser_t * parser, FILE * ctrlFile, uint8_t * fileBuf, uint16_t fileBufSiz);
ctrlEntryType_t getNextEntry(FILE * ctrlFile, uint8_t * fileBuf, uint16_t fileBufSize);
int8_t parseHeaderEntry(uint8_t * ctrlFileBuf, char * rootPath);
int8_t parseDirEntry(uint8_t * ctrlFileBuf, char * pathName, unsigned int * attr, \
                     unsigned int * time, unsigned int * date, unsigned long * size);
int8_t parseFileEntry(uint8_t * ctrlFileBuf, char * pathName, unsigned int * attr, \
                      unsigned int * time, unsigned int * date, unsigned long * size);


//in8_t initParser(FILE * ctrlFile
//int8_t getNextEntry(FILE * ctrlFile);
