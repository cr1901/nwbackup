#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef __DOS__
#include <dos.h>
#endif

#include "dir.h"

int8_t initDirStack(dirStack_t * s) {
  int i = 0;
  s->entries = malloc(sizeof(dirStruct_t *) * DIRSTACK_MAX_ENTRIES);
  if(s->entries == NULL) {
    return -1;
  }

  for(i = 0; i < DIRSTACK_MAX_ENTRIES; i++) {
    s->entries[i] = NULL;
  }

  s->pathName[0] = '\0';
  s->pathSplits[0] = 0; /* Interpret as: "Index of next free char" */
  s->nextEntry = 0;
  s->maxDepth = 0;
  return 0;
}

int8_t pushDir(dirStack_t * s, dirStruct_t * d, char * pathName) {
  uint16_t pathLen;
  int copiedChars;
  char * temp;
  /* uint16_t prevSplitIndex; */

  pathLen = strlen(pathName);
  if(s->nextEntry >= DIRSTACK_MAX_ENTRIES) {
    return -2;
  }
  if(pathLen >= DIR_MAX_PATH + 1) {
    return -3;
  }

  /* Compare the input string to the stored string and make sure they
  match up to what is currently stored. This is a precaution more than
  anything to avoid corrupting the dirStack with a path that isn't a
  subdirectory of the current path, since only one copy of the
  path is kept internally. This shouldn't be too slow even on the oldest
  systems.
  Note that a reset function that keeps allocation of previous data
  structures, but permits a temporary full path override (i.e. push whole
  new path on stack and preserve old one) may be useful. I'll have to see
  if I need it. */

  /* Note that if the stack is empty to begin with, the path check will
  surely fail, and so it's skipped. */
  if(s->nextEntry != 0) {
    /* If the input pathName fully contains the pathName currently
    stored in the dirStack, we can continue! (strstr is the "find
    substring" routine. */
    temp = strstr(pathName, s->pathName);
    if(temp != pathName) {
      return -4;
    }
  }

  if(s->nextEntry >= s->maxDepth) {
    s->entries[s->nextEntry] = (dirStruct_t *) malloc(sizeof(dirStruct_t));
    if(s->entries[s->nextEntry] == NULL) {
      return -1;
    }
    s->maxDepth++;
  }

  (* s->entries[s->nextEntry]) = (* d);
  strcpy(s->pathName, pathName); /* We've already verified that paths
		are okay, so strcpy shouldn't be dangerous here */
  /* prevSplitIndex = s->pathSplits[s->nextEntry]; */
  (s->nextEntry)++;
  /* We want to point at the null terminator for the current string, so
  it can be overwritten when we push another directory. */
  s->pathSplits[s->nextEntry] = pathLen;


  return 0;
}

int8_t popDir(dirStack_t * s, dirStruct_t * d, char * pathName) {
  /* find_t * temp; */
  if(s->nextEntry == 0) {
    return -1;
  }

  /* Do not release memory if we are popping directory-
  chances are we will need it again. */
  /* free(s->entries[(s->nextEntry)]);
  s->entries[(s->nextEntry)] = NULL; */
  (* d) = (* s->entries[ --(s->nextEntry) ]);

  /* Since we popped a directory, reset the directory string to it's
  previous state, by truncating it to the previous length before the push.
  Because we verified that during the push that the most-recently pushed
  string (i.e. the one to be popped now) contains the previously-pushed
  string as a substring, we get back the previous directory! */
  s->pathName[s->pathSplits[s->nextEntry]] = '\0';
  strcpy(pathName, s->pathName); /* Again, we've already verified that
		paths are okay, so strcpy shouldn't be dangerous here */
		
  /* If we are back at the root of the path, we want to make sure that
  any routines which strip the path root don't see any invalid old paths!
  Such routines may assume the path separator always follows the path root,
  but in these routines the separator disappears any time we traverse back 
  up to the path root.
  Routines which assume the path root separator always exists may 
  start looking for paths AFTER the path root separator (to save time), 
  and will "see" the previous stack of paths, minus the path separators, 
  and mistake the string contents as valid. Adding another null terminator
  corrects this. (memset 0 may be better).*/
  if(s->nextEntry <= 1)
  {
    pathName[s->pathSplits[s->nextEntry] + 1] = '\0';
  }
  return 0;
}

void freeDirStack(dirStack_t * s) {
  int i = 0;
  /* Free each allocated directory entry */
  while(i < s->maxDepth) {
    free(s->entries[i]);
    i++;
  }

  /* Then free the pointers! */
  free(s->entries);
}


int8_t openDir(char * name, dirStruct_t * d, fileStruct_t * f) {
  char tempName[DIR_MAX_PATH + 1];
  int charCount;

  charCount = snprintf(tempName, DIR_MAX_PATH + 1, "%s\\*.*", name);
  if(charCount >= DIR_MAX_PATH + 1) {
    return -1;
  }

  if(_dos_findfirst(tempName, (_A_NORMAL | _A_HIDDEN | _A_SYSTEM | _A_SUBDIR), d)) {
    return -2;
  }
  (* f) = (* d);
  return 0;
}

int8_t getNextFile(dirStruct_t * d, fileStruct_t * f) {
  if(_dos_findnext(d)) {
    return -1;
  }
  (* f) = (* d);
  return 0;
}

int8_t closeDir(dirStruct_t * d) {
  if(_dos_findclose(d)) {
    return -1;
  }
  return 0;
}



char * createUnixName(char * dest, char * src) {
  int rc, i;
  rc = snprintf(dest, PATH_MAX + 1, "%s", src);

  if((rc < 0) || (rc >= (PATH_MAX + 1))) {
    return NULL;
  }

  for(i = 0; i < rc; i++) {
    /* if(dest[i] == '\\') */
    if(dest[i] == 92) { /* Backslash ASCII */
      dest[i] = '/';
    }
  }
  return dest;
}
