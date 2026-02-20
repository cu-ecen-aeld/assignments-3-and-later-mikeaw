#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>

int main (int argc, char *argv[]) 
{
	openlog(NULL,0,LOG_USER);
	
	if (argc != 3) {
        fprintf(stderr, "ERROR: The script takes 2 arguments\n 1) Full path to a file to be created\n 2) Text string to be added to file\n");
        syslog(LOG_ERR, "Invalid arguments: expected 2, got %d", argc - 1);
        closelog();
        return 1;
    }
    
    const char *filename = argv[1];
    const char *writestr = argv[2];
	
	FILE *file = fopen (filename, "w");
	if (file == NULL) {
		int stored_errno = errno;
		fprintf(stderr, "ERROR: %s\n", strerror(stored_errno));
    	syslog(LOG_ERR, "ERROR: %s", strerror(stored_errno));
    	closelog();
		return 1;
	} 
		
	if (fprintf(file, "%s", writestr) < 0) {	
		int stored_errno = errno;
		fprintf(stderr, "ERROR: %s\n", strerror(stored_errno));
    	syslog(LOG_ERR, "ERROR: %s", strerror(stored_errno));
    	closelog();
		return 1;	
	}
	
	syslog(LOG_DEBUG, "Writing %s to %s",writestr,filename);
	
	fclose(file);
	closelog();
	return 0;
}


