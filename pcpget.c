// pcpget.c
#include <inttypes.h>
#include <libgen.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <curl/curl.h>

#define DIM(x) (sizeof(x)/sizeof(*(x)))

static const char		*sizes[]  = { "EiB", "PiB", "TiB", "GiB", "MiB", "KiB", "B" };
static const uint64_t	exbibytes = 1024ULL * 1024ULL * 1024ULL *
									1024ULL * 1024ULL * 1024ULL;

char *calculateSize(uint64_t size) {
	char     *result = (char *) malloc(sizeof(char) * 20);
	uint64_t  multiplier = exbibytes;
	int i;

	for (i = 0; i < DIM(sizes); i++, multiplier /= 1024)
	{
		if (size < multiplier)
			continue;
		if (size % multiplier == 0)
			sprintf(result, "%" PRIu64 " %s", size / multiplier, sizes[i]);
		else
			sprintf(result, "%.1f %s", (float) size / multiplier, sizes[i]);
		return result;
	}
	strcpy(result, "0");
	return result;
}

static char *bar[]= {	"[                                  ]", //0
						"[*                                 ]", //3
						"[**                                ]", //6
						"[***                               ]", //9
						"[****                              ]", //12
						"[*****                             ]", //15
						"[******                            ]", //18
						"[*******                           ]", //21
						"[********                          ]", //24
						"[*********                         ]", //27
						"[**********                        ]", //30
						"[***********                       ]", //33
						"[************                      ]", //36
						"[*************                     ]", //39
						"[**************                    ]", //42
						"[***************                   ]", //45
						"[****************                  ]", //48
						"[*****************                 ]", //51
						"[******************                ]", //54
						"[*******************               ]", //57
						"[********************              ]", //60
						"[*********************             ]", //63
						"[**********************            ]", //66
						"[***********************           ]", //69
						"[************************          ]", //72
						"[*************************         ]", //75
						"[**************************        ]", //78
						"[***************************       ]", //81
						"[****************************      ]", //84
						"[*****************************     ]", //87
						"[******************************    ]", //90
						"[*******************************   ]", //93
						"[********************************  ]", //96
						"[********************************* ]", //99
						"[**********************************]", //100
					};

struct pcp_progress {
	char *filename;
	int begin;
	int next_percent_report;
	int istty;
	int final;
	curl_off_t start_time;
	int quiet;
	int conn_timeout;
	CURL *curl;
};

static int xferinfo(void *p,
					curl_off_t dltotal, curl_off_t dlnow,
					curl_off_t ultotal, curl_off_t ulnow)
{
	struct pcp_progress *myp = (struct pcp_progress *)p;
	CURL *curl = myp->curl;
	curl_off_t curtime = 0;
	char percentstr[6];

	curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME_T, &curtime);
	if (myp->start_time == 0) {
		myp->start_time = curtime;
	} else {
		if ( dltotal == 0){
			curl_off_t diff = curtime - myp->start_time;
			if (diff > 4000000) {
//				fprintf(stderr, "%lu\n", diff);
				myp->conn_timeout = 1;
				return 1;
			}
		}
	}

	if (myp->quiet != 1) {
		if ((dltotal > 0) && (myp->final != 1)) {
			float percent = (float)dlnow/(float)dltotal * (float)100;
			snprintf(percentstr, 5, "%03.0f%%", percent);
			int bar_pos = (int)percent/3;
			if (percent == 100) bar_pos = 34;
			if (percent > myp->next_percent_report) {
				if (myp->istty) {
					char tmp[45];
					strncpy(tmp, bar[bar_pos], 15);
					tmp[15]='\0';
					strcat(tmp, percentstr);
					strcat(tmp, &bar[bar_pos][15]);
//				fprintf(stderr, "\r%3.2f\%: %" CURL_FORMAT_CURL_OFF_T " of %" CURL_FORMAT_CURL_OFF_T, percent, dlnow, dltotal);
					fprintf(stderr,"\r%s %s: %s", tmp, myp->filename, calculateSize(dltotal));
					if (percent == 100) {
						fprintf(stderr, " Done\n");
						myp->final = 1;
					}
				} else {
					if (myp->begin == 0) {
						fprintf(stderr, "%s: %s [", myp->filename, calculateSize(dltotal));
						myp->begin = 1;
					}
					for (int i = myp->next_percent_report; i <= (int)percent; i += 3) {
						fprintf(stderr, "*");
					}
					if (percent == 100) {
						fprintf(stderr, "] Done\n");
						myp->final = 1;
					}
				}
				fflush(stderr);
				int next = (int)percent + 3;
				myp->next_percent_report = next > 99 ? 99 : next ;
			}
		}
	}
	return 0;
}

struct pcpget_options {
	int quiet;
	int verbose;
	int timeout;
	int retries;
	char *repo_url;
	char *file_path;
	char *file_name;
	char output[512];
};

size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
	size_t written;
	written = fwrite(ptr, size, nmemb, stream);
	return written;
}

long downloadFile(CURL *curl, struct pcpget_options *opt) {
	FILE *fp;
	CURLcode res;
	struct pcp_progress progress;

	long response = 0;
	int retry = 0;
	if (curl){
		progress.curl = curl;
		progress.begin = 0;
		progress.istty = isatty(2);
		progress.next_percent_report = 0;
		progress.final = 0;
		progress.filename = opt->file_name;
		progress.start_time = 0;
		progress.quiet = opt->quiet;
		progress.conn_timeout = 0;

		while (retry < opt->retries){
			fp = fopen(opt->output, "wb");
			if (fp) {
				curl_easy_setopt(curl, CURLOPT_URL, opt->repo_url);
				if (opt->verbose)
					curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
				curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
				curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
				curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo);
				/* pass the struct pointer into the xferinfo function */
				curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
				curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L); // set to 0 to show

				curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, (long)opt->timeout);
				curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 30L);

				res = curl_easy_perform(curl);
				fclose(fp);

				if ( (CURLE_OPERATION_TIMEDOUT == res) || (progress.conn_timeout == 1) ){ // timeout
					progress.start_time = 0;
					progress.conn_timeout = 0;
					fprintf(stderr, "Timeout!\n");
					retry++;
					continue;
				}
				if (CURLE_OK == res) {
					res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response);
				}
				if (response == 522){
					fprintf(stderr, ".");
					retry++;
					continue;
				} else if (response != 200) {
					remove(opt->output);
				}
				break;
			} else {
				fprintf(stderr, "Unable to open file: %s\n", opt->output);
				return 1;
			}
		}
		return response;
	}
}

void usage() {
	fprintf(stderr, "pcpget <options> url\n\n");
	fprintf(stderr, "  -q          Quiet.\n");
	fprintf(stderr, "  -v          Verbose curl output.\n");
	fprintf(stderr, "  -r <number> Set number of retries on 522 or timeout errors.(default: 5)\n");
	fprintf(stderr, "  -O <file>   Save output as file.\n");
	fprintf(stderr, "  -P <path>   Save files to path.\n");
	fprintf(stderr, "  -T <second> Set timeout for stalled connections. (defaut 15s)\n\n");
}

int main(int argc, char * argv[]) {
	int opt=0;
	int code;
	CURL *curl;
	struct pcpget_options opts;
	opts.quiet=0;
	opts.verbose=0;
	opts.timeout=30;
	opts.retries=5;
	char *dot = ".";
	opts.file_path = dot;
	opts.repo_url = NULL;
	opts.output[0]= '\0';

	while ((opt = getopt(argc, argv, "chqvr:O:P:T:")) != -1) {
		switch(opt){
			case 'c':
				break;
			case 'q':
				opts.quiet = 1;
				break;
			case 'v':
				opts.verbose = 1;
				break;
			case 'r':
				opts.retries = atoi(optarg);
				break;
			case 'O':
				strncpy(opts.output, optarg, 512);
				opts.file_name = basename(optarg);
				break;
			case 'P':
				opts.file_path = optarg;
				break;
			case 'T':
				opts.timeout = atoi(optarg);
				break;
			case '?':
			case 'h':
				usage();
				return 0;
			default:
				fprintf(stderr, "Unknown option -%c\n", opt);
				break;
		}
	}

	for (int index = optind; index< argc; index++) {
		CURLUcode rc;
		CURLU *url = curl_url();
		rc = curl_url_set(url, CURLUPART_URL, argv[index], 0);
		if (rc != 0) {
			fprintf(stderr, "Bad url: %s\n", curl_url_strerror(rc) );
			return 1;
		}
		rc = curl_url_get(url, CURLUPART_URL, &opts.repo_url, 0);
		if (rc != 0) {
			fprintf(stderr, "Bad url: %s\n", curl_url_strerror(rc) );
			return 1;
		}
		curl_url_cleanup(url);
	}

	if (opts.repo_url != NULL) {
		opts.file_name = strrchr(opts.repo_url, '/');
	} else {
		fprintf(stderr, "Error: No Url provided.\n");
		usage();
		return 1;
	}
	while (opts.file_name[0] == '/') opts.file_name++;
	if (opts.quiet != 1)
		fprintf(stderr, "%s\n", opts.repo_url);

	curl = curl_easy_init();
	if (opts.output[0] == '\0') {
		snprintf( opts.output, 512, "%s/%s", opts.file_path, opts.file_name);
	}
	code = downloadFile(curl, &opts);

	curl_easy_cleanup(curl);

	return 0;
}

