#ifndef DIR_H
#define DIR_H

#ifdef __DOS__
#include <dos.h>
#elif defined(_POSIX_SOURCE)
#include <dirent.h>
#endif
#include <stdint.h>

#define DIRSTACK_MAX_ENTRIES 128
#define DIR_MAX_PATH 128

/* For future portability considerations... */
#ifdef __DOS__
typedef struct find_t dirStruct_t;
typedef struct find_t fileStruct_t;
#elif defined(_POSIX_SOURCE)
typedef DIR dirStruct_t;
typedef struct dirent fileStruct_t;
#endif

typedef struct dirStack {
  dirStruct_t ** entries;
  char pathName[DIR_MAX_PATH + 1];
  uint16_t pathSplits[DIRSTACK_MAX_ENTRIES]; /* Probably not worth
	allocating each time... */

  uint8_t nextEntry;
  uint8_t maxDepth;
} dirStack_t;

/* How to handle drive letters? Convert to POSIX-like path in these functions,
a la mTCP FTPSRV? */

int8_t initDirStack(dirStack_t *);
int8_t pushDir(dirStack_t *, dirStruct_t *, char * pathName);
int8_t popDir(dirStack_t *, dirStruct_t *, char * pathName);
void freeDirStack(dirStack_t *);
int8_t openDir(char * name, dirStruct_t * d, fileStruct_t * f);
int8_t getNextFile(dirStruct_t * d, fileStruct_t * f);
int8_t closeDir(dirStruct_t * d);

char * createUnixName(char * dest, char * src);

#endif        //  #ifndef DIR_H

