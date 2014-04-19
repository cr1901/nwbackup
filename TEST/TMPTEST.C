#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* In the future, a temporary file may be needed to store information regarding
the stored files in a platform-independent manner (such as attributes, 
access-date, etc). A temporary file might be a good fit for this for HOSTED 
implementations (freestanding would need to store such file in RAM or 
something). */

int main(int argc, char * argv[])
{
	char filename[L_tmpnam];
	char temp_format_str[] = "This implementation of C stdlib supports up to %u temp files.\n" \
		"Temp filenames can be up to %u bytes in length.\n" \
		"The current temp file's name is: %s\n";
	char * temp_str_in = NULL;
	char * temp_str_out = NULL;	
	int chars_written;
	long int temp_file_position = 0L;
	long int chars_read = 0;
	
	FILE *fp;
	tmpnam(filename);
	
	if(filename == NULL)
	{
		fprintf(stderr, "Attempt to get temporary file failed!\n");
	}
	
	fprintf(stderr, "%s", filename);
	
	temp_str_in = malloc(BUFSIZ);
	temp_str_out = malloc(BUFSIZ);
	if(temp_str_in == NULL || temp_str_out == NULL)
	{
		fprintf(stderr, "Temp string in/out malloc failure!\n");
		return EXIT_FAILURE;
	}
	
	chars_written = snprintf(temp_str_in, BUFSIZ, temp_format_str, \
		TMP_MAX, L_tmpnam, filename);
	if(chars_written >= BUFSIZ)
	{
		fprintf(stderr, "Temp string too small to hold data!\n");
		return EXIT_FAILURE;
	}
	
	fprintf(stderr, "temp_str_in: ");
	fprintf(stderr, temp_str_in);
	fprintf(stderr, "\n");
	
	fp = fopen(filename, "w+b");
	if(fp = NULL)
	{
		perror("Temp file fopen failure");
	}
	
	fprintf(fp, temp_str_in);
	temp_file_position = ftell(fp);
	
	if(temp_file_position != -1L)
	{
		rewind(fp);
		fread(temp_str_out, 1, temp_file_position, fp);
		temp_str_out[temp_file_position] = '\0';
		fprintf(stderr, "temp_str_out: ");
		fprintf(stderr, temp_str_out);
	}
	else
	{
		perror("Temp file ftell failure");
	}
	fclose( fp );
	
	if((argc < 2) || strcmp(argv[1], "-k"))
	{
		remove(filename);
	}
	
	free(temp_str_in);
	free(temp_str_out);
	return EXIT_SUCCESS;
}
