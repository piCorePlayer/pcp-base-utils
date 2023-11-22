#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>


static const char *const evval[3] = {
	"RELEASED",
	"PRESSED ",
	"REPEATED"
};

uint32_t gettime_ms(void) {
	struct timespec ts;
	if (!clock_gettime(CLOCK_REALTIME, &ts)) {
		return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
	}
	return 0;
}

int powerbtnevents = 0;
bool powerbtn = false;

pthread_cond_t event = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t lock = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;

void *monitor( void* data){
	char *name = (char*)data;
	int clicks = 0;
	uint32_t timepressed = 0;
	bool pressed = false;
	int lastevent=0;

	while(1){
		if (lastevent != powerbtnevents){
			if ((!powerbtn) & (pressed)){
				clicks += 1;
				pressed = false;
			}
			if (powerbtn){
				pressed = true;
				if (clicks == 0)
					timepressed = gettime_ms();
			}
			lastevent = powerbtnevents;
		}
		if (timepressed){
			if ((gettime_ms() - timepressed) > 750){
				if (clicks == 1){
//					printf("Single Click\n");
					system("/usr/local/bin/pcp rb");
				}else if(clicks == 2){
//					printf("Double Click\n");
					system("/usr/local/bin/pcp sd");
				}
				if (!pressed){
					timepressed = 0;
					clicks = 0;
				}
			}
			usleep(100);
		} else {
//			printf("waiting\n");
			pthread_mutex_lock(&lock);
			pthread_cond_wait(&event,&lock);
			pthread_mutex_unlock(&lock);
		}
	}
	return 0;
}

int main(void)
{
	const char *dev = "/dev/input/by-path/platform-pwr_button-event";
	struct input_event ev;
	ssize_t n;
	int fd;
	pthread_t thr1;

	fd = open(dev, O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "Cannot open %s: %s.\n", dev, strerror(errno));
		return EXIT_FAILURE;
	}

	pthread_create(&thr1, NULL, monitor, NULL);

	while (1) {
		n = read(fd, &ev, sizeof ev);
		if (n == (ssize_t)-1) {
			if (errno == EINTR)
				continue;
			else
				break;
		} else
		if (n != sizeof ev) {
			errno = EIO;
			continue;
		}
		if (ev.type == EV_KEY && ev.value >= 0 && ev.value <= 2) {
//			printf("%s 0x%04x (%d)\n", evval[ev.value], (int)ev.code, (int)ev.code);
			//ensure we only handle the kernel power button key code
			if ((int)ev.code == 116) {
				if (ev.value)
					powerbtn = true;
				else
					powerbtn = false;
				++powerbtnevents;
				pthread_cond_signal(&event);
			}
		}
	}

	fflush(stdout);
	fprintf(stderr, "%s.\n", strerror(errno));
	return EXIT_FAILURE;
}
