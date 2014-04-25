#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "dir.h"
#include "nwbackup.h"


signed char do_backup(nwBackupParms parms, char * remote_name, char * local_dir);
signed char do_restore(nwBackupParms parms, char * remote_name, char * local_dir);
void print_banner();
void print_usage();

int main(int argc, char * argv[]) {
  short prog_mode = 0;
  char * logfile = NULL;
  unsigned short count = 1;
  nwBackupParms parms;
  char * mode_string[] = {"FULL", "DIFF", "RESTORE"};
  
  /* To ask... can option arguments use '-' for stdin/stdout processing? */
  
  if(argc < 3)
  {
    print_usage();
    return EXIT_FAILURE;
  }  
  
  /* POSIX option args block */
  {
    int options_parsing = 1;
    while(count < argc && argv[count][0] == '-' && options_parsing)
    {
      unsigned short num_opts_after_hyphen, i;
      int backup_mode_implied = 0;
      int restore_mode_implied = 0;
      
      num_opts_after_hyphen = strlen(argv[count]) - 1;
      /* If option doesn't follow hyphen... */
      if(!num_opts_after_hyphen)
      { 
         /* This single hyphen is actually an operand, which in POSIX world
         represents stdin/stdout or a file called '-'... */
      	 fprintf(stderr, "Single hyphen not supported by this utility.\n");
      }
      
      for(i = 0; i < num_opts_after_hyphen; i++)
      {
        switch(argv[count][i + 1])
        {
        case 'r':
          if(backup_mode_implied)
          {
            fprintf(stderr, "Backup and restore options are mutually exclusive.\n");
            return EXIT_FAILURE;
          }
          restore_mode_implied = 1;
          prog_mode = 2;
          break;
        case 'f':
          if(restore_mode_implied)
          {
            fprintf(stderr, "Full backup and restore options are mutually exclusive.\n");
            return EXIT_FAILURE;
          }
          else if(backup_mode_implied)
          {
            fprintf(stderr, "A backup mode was supplied twice!\n");
            return EXIT_FAILURE;
          }
	  backup_mode_implied = 1;
	  prog_mode = 0;
	  break;
        case 'd':
	  if(restore_mode_implied)
          {
            fprintf(stderr, "Differential backup and restore options are mutually exclusive.\n");
            return EXIT_FAILURE;
          }
          else if(backup_mode_implied)
          {
            fprintf(stderr, "A backup mode was supplied twice!\n");
            return EXIT_FAILURE;
          }
          backup_mode_implied = 1;
          prog_mode = 1;
	  break;
	case 'h':
	  print_usage();
	  return EXIT_FAILURE;
        case 'l':
          if(i != num_opts_after_hyphen - 1)
          {
            printf("Option -l requires an argument, but " \
              "was specified as part of options which do not "
              "require arguments\n");
            return EXIT_FAILURE;
          }

	  count++;
	  /* Skip the leading space! */
	  logfile = argv[count];
          break;     	
        default:
          fprintf(stderr, "Invalid option: %c.\n", argv[count][i + 1]);
          return EXIT_FAILURE;
        }
      }
      count++;
    }
  }
      
  if(argc - count < 2)
  {
    printf("Not enough command line parameters (type -h for help)\n");
    return EXIT_FAILURE;
  }
  
  /* POSIX operands and main logic block */
  {
    char * backup_name = NULL;
    char * start_dir = NULL;
    signed char client_rc;
    
    backup_name = argv[count++];
    start_dir = argv[count];
    
    if(logfile == NULL)
    {
      logfile = malloc(128);
      snprintf(logfile, 128, "%s.LOG", backup_name);
    }
    
    
    fprintf(stderr, "Backup Params:\n" \
      "Mode: %s\nBackup Name %s\nRoot Dir: %s\nLogfile: %s\n", \
      mode_string[prog_mode], backup_name, start_dir, logfile);
    
    /* To do: Add NWGET utility for restoring individual files... or add as
    third program variant. Re-Look up mutual exclusion between program invocation 
    types and mutual exclusion within invocation type, as well as whether it is 
    legal to re-specify command line params*/
    
    
    /* switch(prog_mode)
    {
    case 0:
      do_backup(mode 0)
    case 1:
      do_backup(mode 1)
    case 2:
      do_restore()
    */
    
    
    return 0; 
  }
}

signed char do_backup(nwBackupParms parms, char * remote_name, char * local_dir)
{
  return 0;
}

signed char do_restore(nwBackupParms parms, char * remote_name, char * local_dir)
{
  return 0;
}

void print_banner()
{
    fprintf( stderr, "NetWork BACKUP program for mTCP FTP by W. Jones (thor0505@comcast.net)\n"
           "TCP stack by M. Brutman (mbbrutman@gmail.com) (C)opyright 2012-2014\n\n" );
}

void print_usage()
{
  print_banner();
  /* Removed: [-s server_name][-u username][-p password] */
  fprintf( stderr, \
    "Usage: NWBACKUP [-d | -f][-l logfile] [backup_name] [local_dir]\n" \
    "  NWBACKUP -r[-l logfile] [backup_name] [local_dir] \n" \
    "  [-d | -f]: Differential or full backup (default full).\n" \
    "  -r: Perform restore. Does NOT need to be first argument.\n" \
    "  Default logfile name is [backup_name].LOG.\n");
}
