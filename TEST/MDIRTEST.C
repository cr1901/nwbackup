#include <dos.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>


// These two arrays will serve as our stack of fileinfo structures and paths.
// You can save some space by just maintaining one path string and growing it
// and shrinking it as you add or remove directories instead of what we do here,
// which is lazy.
//
struct find_t fileInfos[32];
char *paths[32];


// This is just a temporary variable.
//
char filespec[128];


int main( int argc, char *argv[] ) {

  int filesFound = 0;
  int dirsFound = 0;

  int stackPos = 0;


  // Prime the pump - do the first findfirst on the root directory.

  paths[stackPos] = strdup( argv[1] );
  sprintf( filespec, "%s*.*", paths[stackPos] );
  unsigned int rc = _dos_findfirst( filespec, (_A_NORMAL | _A_SUBDIR), &fileInfos[stackPos] );

  while ( 1 ) {

    if ( rc == 0 ) {

      if ( fileInfos[stackPos].attrib & _A_SUBDIR ) {

        dirsFound++;

        // Exclude . and .. from the directory walk, or we will get nowhere fast.

        if ( strcmp(fileInfos[stackPos].name, ".") != 0 && strcmp(fileInfos[stackPos].name, ".." ) != 0 ) {

          int newPathLength = strlen( paths[stackPos] ) + strlen( fileInfos[stackPos].name ) + 2;
          paths[stackPos+1] = (char *)malloc( newPathLength );
          sprintf( paths[stackPos+1], "%s%s/", paths[stackPos], fileInfos[stackPos].name );

          stackPos++;


          // 32 might not be correct ..  it might be closer to 40 based on the maximum allowable
          // DOS path length.  This is close enough for demo code.

          if ( stackPos == 32 ) {
            puts( "Too many levels of directories" );
            break;
          }

          sprintf( filespec, "%s*.*", paths[stackPos] );
          rc = _dos_findfirst( filespec, (_A_NORMAL | _A_SUBDIR), &fileInfos[stackPos] );
          continue;

        }

      }

      else if ( ((fileInfos[stackPos].attrib & _A_NORMAL) == _A_NORMAL) || ((fileInfos[stackPos].attrib & _A_RDONLY) == _A_RDONLY) ) {
        filesFound++;
        printf( "%s%s\n", paths[stackPos], fileInfos[stackPos].name );
      }

      rc = _dos_findnext( &fileInfos[stackPos] );

    }

    else {

      free( paths[stackPos] );

      if ( stackPos == 0 ) {
        break;
      }

      --stackPos;

      rc = _dos_findnext( &fileInfos[stackPos] );

    }

  }

  printf( "Total subdirectories: %u, Total files: %u\n", dirsFound, filesFound );
  return 0;
}
