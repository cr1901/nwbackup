#include <stdio.h>

#include "jsmn.h"

/* Although there is nothing preventing a user from changing control functions
to use different input data types, since dirStruct and fileStruct are already
abstracted, I see no harm in using them to create control files. */
#include "dir.h"

int8_t initCtrlFile(FILE * ctrlFile, int backupType, int FsType, char * rootDir);
int8_t addDirEntry(FILE * ctrlFile, char * name, dirStruct_t * d);
int8_t addFileEntry(FILE * ctrlFile, char * name, fileStruct_t * f); 
int8_t closeDirEntry(FILE * ctrlFile);
int8_t finalCtrlFile(FILE * ctrlFile);
