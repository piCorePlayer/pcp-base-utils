// pcpfetch.c
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <signal.h>

#define BUFFER_SIZE 1024
#define MX_SPLIT 1024

static void sigHandler( int sig, siginfo_t *siginfo, void *context ) {
	switch( sig ) {
		case SIGINT:
		case SIGTERM:
//			stop_signal = sig;
			break;
		case SIGPIPE:
			break;
    }
}

size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t written;
    written = fwrite(ptr, size, nmemb, stream);
    return written;
}

struct myprogress {
	curl_off_t lastruntime;
	CURL *curl;
};
#define MINIMAL_PROGRESS_FUNCTIONALITY_INTERVAL     300000
/* this is how the CURLOPT_XFERINFOFUNCTION callback works */
static int xferinfo(void *p,
                    curl_off_t dltotal, curl_off_t dlnow,
                    curl_off_t ultotal, curl_off_t ulnow)
{
	struct myprogress *myp = (struct myprogress *)p;
	CURL *curl = myp->curl;
	curl_off_t curtime = 0;
    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME_T, &curtime);

	if((curtime - myp->lastruntime) >= MINIMAL_PROGRESS_FUNCTIONALITY_INTERVAL) {
		myp->lastruntime = curtime;
//		if (dltotal > 0) {
//			int percent =  (unsigned long)dlnow/(unsigned long)dltotal * (unsigned long)100;
			//fprintf(stdout, "\e[%d;1H \e[2K \r \a", percent);
			fprintf(stderr, "\r%lu of %lu", dlnow, dltotal);
			fflush(stderr);
//		}
	}
	return 0;
}

long downloadFile(CURL *curl, const char* url, const char* fname) {
    FILE *fp;
    CURLcode res;
    struct myprogress prog;

	long response = 0;
	int retry = 0;
    if (curl){
    	prog.curl = curl;
		while (retry < 2){
	        fp = fopen(fname, "wb");
	        curl_easy_setopt(curl, CURLOPT_URL, url);
//			curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
	        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
	        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
			curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo);
			/* pass the struct pointer into the xferinfo function */
			curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &prog);

			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	        res = curl_easy_perform(curl);
			if (CURLE_OK == res) {
				res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response);
			}
    	    fclose(fp);
			if (response == 522){
				retry++;
				continue;
			} else if (response == 404){
				remove(fname);
			} else if (response != 200) {
				char newname[100];
				snprintf( newname, 100, "%s.bad", fname);
				rename(fname, newname);
			}
			break;
		}
		return response;
    }
}

char **split( char **result, char *working, const char *src, const char *delim) {
	int i;
	strncpy(working, src, strlen(src)); // working will get chppped up instead of src 
	char *p=strtok(working, delim);
	for(i=0; p!=NULL && i < (MX_SPLIT -1); i++, p=strtok(NULL, delim) ) {
		result[i]=p;
		result[i+1]=NULL;  // mark the end of result array
	}
	return result;
}


int main(int argc, char * argv[]) {
	int opt=0;
    CURL *curl;
	long PORT=0;
	long code;
    char buffer[BUFFER_SIZE];
    char resp[] = "HTTP/1.0 200 OK\r\n"
                  "Server: webserver-c\r\n"
                  "Content-type: text/html\r\n\r\n"
                  "OK\r\n";
    char fail[] = "HTTP/1.0 404 OK\r\n"
                  "Server: webserver-c\r\n"
                  "Content-type: text/html\r\n\r\n"
                  "FAIL\r\n";
	struct linger optlinger;
	optlinger.l_onoff = 1;
	optlinger.l_linger = 0;
	const socklen_t optlingerlen = sizeof(optlinger);

	if (geteuid() == 0) {
		fprintf(stderr, "DO not run as root\n");
		exit(1);
	}



	while ((opt = getopt(argc, argv, "p:")) != -1) {
		switch(opt){
			case 'p':
				PORT=atoi(optarg);
				break;
			break;
		}
	}

	if ( PORT == 0) {
		printf("Port not set\n");
		return 1;
	}
	// Get kernel name
	char KERNEL[50];
	FILE *fp = popen("uname -r", "r");
	fscanf(fp, "%50s", &KERNEL);
	pclose(fp);
//	printf("KERNEL:%s\n", KERNEL);
	// Get onboot.lst
	char ONBOOTNAME[50];
	char CMDLINE[1024];
	fp = popen("cat /proc/cmdline", "r");
	fscanf(fp, "%[^\n]", &CMDLINE);
	pclose(fp);
	if (strstr(CMDLINE, "lst")) {
		fp = popen("CMDL=$(cat /proc/cmdline);res=${CMDL##* lst=};echo ${res%%[    ]*}", "r");
		fscanf(fp, "%50s", &ONBOOTNAME);
		pclose(fp);
	} else {
		strcpy(ONBOOTNAME, "onboot.lst");
	}
//	printf("ONBOOTNAME:%s\n", ONBOOTNAME);
	char *TCEINSTALLED = "/usr/local/tce.installed";
	char *TCEDIR = "/etc/sysconfig/tcedir";
	
	curl = curl_easy_init();

    // Create a socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
    	const int optVal = 1;
    	const socklen_t optLen = sizeof(optVal);
    	setsockopt( sockfd, SOL_SOCKET, SO_REUSEADDR, (void *) &optVal, optLen);
    	setsockopt( sockfd, SOL_SOCKET, SO_LINGER, (void *) &optlinger, optlingerlen);
        perror("webserver (socket)");
        return 1;
    }
    printf("socket created successfully\n");

    // Create the address to bind the socket to
    struct sockaddr_in host_addr;
    int host_addrlen = sizeof(host_addr);

    host_addr.sin_family = AF_INET;
    host_addr.sin_port = htons(PORT);
    host_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Create client address
    struct sockaddr_in client_addr;
    int client_addrlen = sizeof(client_addr);

    // Bind the socket to the address
    if (bind(sockfd, (struct sockaddr *)&host_addr, host_addrlen) != 0) {
        perror("webserver (bind)");
        return 1;
    }
    printf("socket successfully bound to port:%d\n", PORT);

    // Listen for incoming connections
    if (listen(sockfd, SOMAXCONN) != 0) {
        perror("webserver (listen)");
        return 1;
    }
    printf("server listening for connections\n");

    for (;;) {
        // Accept incoming connections
        int newsockfd = accept(sockfd, (struct sockaddr *)&host_addr,
                               (socklen_t *)&host_addrlen);
        if (newsockfd < 0) {
            perror("webserver (accept)");
            continue;
        }
        printf("connection accepted\n");

        // Get client address
        int sockn = getsockname(newsockfd, (struct sockaddr *)&client_addr,
                                (socklen_t *)&client_addrlen);
        if (sockn < 0) {
            perror("webserver (getsockname)");
            continue;
        }

        // Read from the socket
        int valread = read(newsockfd, buffer, BUFFER_SIZE);
        if (valread < 0) {
            perror("webserver (read)");
            continue;
        }

        // Read the request
        char method[BUFFER_SIZE], uri[BUFFER_SIZE], version[BUFFER_SIZE];
        sscanf(buffer, "%s %s %s", method, uri, version);
//        printf("[%s:%u] %s %s %s\n", inet_ntoa(client_addr.sin_addr),
//               ntohs(client_addr.sin_port), method, version, uri);

		char url[sizeof(uri)+1];
		strncpy(url, uri, sizeof(uri));
		printf("URI:%s\n", uri);
//		printf("URL:%s\n", url);

		char repo_url[1024] = "";
		char file_path[1024] = "";

		char * strip = strtok(url, "?");  // Strip head of url
		char * qs = strtok(NULL, "?");  // get string
//		printf("QS: %s\n", qs);
		if (qs) {
			int i=0;
			char *result[MX_SPLIT]={NULL};
			char working[1024]={0x0};
			char mydelim[]="&";
			split(result, working, qs, mydelim);

			while(result[i]!=NULL){
//				printf("token #%d: %s\n", i, result[i]);
				char *variable[MX_SPLIT]={NULL};
				char work2[1024]={0x0};
				char vardelim[]="=";
				split( variable, work2, result[i], vardelim);
				if (variable[0] != NULL && variable[1] !=NULL) {
//					printf("var=%s:%s\n", variable[0], variable[1]);
					if (strcmp("URL", variable[0]) == 0){
						strncpy(repo_url, variable[1], strlen(variable[1])+1);
					}
					if (strcmp("DIR", variable[0]) == 0){
						strncpy(file_path, variable[1], strlen(variable[1])+1);
					}
				}
				i++;
			}
		}

		if (strlen(repo_url) > 1) {
			if (strncmp("http", repo_url, 4) == 0) {
//				printf("REPO:%s, FILE:%s\n", repo_url, file_path);
				code = downloadFile(curl, repo_url, file_path);
			}
		}

        if (strncmp("/quit", uri, 5) == 0){
	    	setsockopt( newsockfd, SOL_SOCKET, SO_LINGER, (void *) &optlinger, optlingerlen);
			code=200;
		}

		int valwrite;
        // Write to the socket
        if (code == 200) {
			printf("OK");
        	valwrite = write(newsockfd, resp, strlen(resp));
	    } else {
			printf("NOT OK");
	    	valwrite = write(newsockfd, fail, strlen(fail));
	    }
	    if (valwrite < 0) {
            perror("webserver (write)");
            continue;
        }

        close(newsockfd);

        if (strncmp("/quit", uri, 5) == 0)
        	break;;

    }

    curl_easy_cleanup(curl);
	close(sockfd);

    return 0;
}

