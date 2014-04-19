#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "nwbackup.h"


int main(int argv, char * argc[])
{
	//mTCPBackupParms nwParms;
	
	if(argc < 2) {
		fprintf(stderr, "Please specify a directory name!\n");
		return EXIT_FAILURE;
	}
	
	
	
	closeRemote();
	return 0;
}
