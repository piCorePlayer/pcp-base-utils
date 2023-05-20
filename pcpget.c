// pcpget.c
#include <inttypes.h>
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
	CURL *curl;
};

static int xferinfo(void *p,
					curl_off_t dltotal, curl_off_t dlnow,
					curl_off_t ultotal, curl_off_t ulnow)
{
	struct pcp_progress *myp = (struct pcp_progress *)p;
	CURL *curl = myp->curl;
	char percentstr[6];

	if ((dltotal > 0) && (myp->final != 1)) {
		float percent = (float)dlnow/(float)dltotal * (float)100;
		snprintf(percentstr, 5, "%03.0f%%", percent);
		int bar_pos = (int)percent/3;
		if (percent == 100) bar_pos = 34;
		if (percent > myp->next_percent_report) {
			if (myp->istty) {
				char tmp[37];
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
	return 0;
}

struct pcpget_options {
	int quiet;
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
		progress.filename = strdup(opt->file_name);

		while (retry < opt->retries){
			fp = fopen(opt->output, "wb");
			if (fp) {
				curl_easy_setopt(curl, CURLOPT_URL, opt->repo_url);
//				curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
				curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
				curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
				curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo);
				/* pass the struct pointer into the xferinfo function */
				curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
				if (opt->quiet != 1)
					curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L); // set to 0 to show
				else
					curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);

				curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, (long)opt->timeout);
				curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 30L);

				res = curl_easy_perform(curl);
				fclose(fp);

				if (CURLE_OPERATION_TIMEDOUT == res) { // timeout
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
				} else if (response == 404){
					remove(opt->output);
				} else if (response != 200) {
					char newname[100];
					snprintf( newname, 100, "%s.bad", opt->output);
					rename(opt->output, newname);
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

int main(int argc, char * argv[]) {
	int opt=0;
	int code;
	CURL *curl;
	struct pcpget_options opts;
	opts.quiet=0;
	opts.timeout=30;
	opts.retries=5;
	opts.file_path = strdup(".");

	while ((opt = getopt(argc, argv, "cqr:P:T:")) != -1) {
		switch(opt){
			case 'c':
				break;
			case 'q':
				opts.quiet = 1;
				break;
			case 'r':
				opts.retries = atoi(optarg);
				break;
			case 'P':
				opts.file_path = optarg;
				break;
			case 'T':
				opts.timeout = atoi(optarg);
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

		if (opts.repo_url != NULL)
			opts.file_name = strrchr(opts.repo_url, '/');
		while (opts.file_name[0] == '/') opts.file_name++;
		if (opts.quiet != 1)
			fprintf(stderr, "%s, %s\n", opts.repo_url, opts.file_name);
	}

	curl = curl_easy_init();
	snprintf( opts.output, 512, "%s/%s", opts.file_path, opts.file_name);
	code = downloadFile(curl, &opts);

	curl_easy_cleanup(curl);

	return 0;
}

