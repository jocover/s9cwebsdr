#include <s9c.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>
#include <pthread.h>


///////////////////////////////////////////////////////
//////////工作通道数 最大8个//////////////
#define CHANNEL_NUM 4

//-----------每个通道频率，范围5000-1002000000------------

#define CH_1_FREQ	1810000
#define CH_2_FREQ	7000400
#define CH_3_FREQ	10120000
#define CH_4_FREQ	14010000
#define CH_5_FREQ	21001000
#define CH_6_FREQ	24895000
#define CH_7_FREQ	28010000
#define CH_8_FREQ	80010000

//----------AMP GAIN 0-30---------------
#define AMP_GAIN	30


//-----------每个通道增益------------

#define CH_1_GAIN	2.3
#define CH_2_GAIN	1
#define CH_3_GAIN	1
#define CH_4_GAIN	5
#define CH_5_GAIN	5
#define CH_6_GAIN	5
#define CH_7_GAIN	0
#define CH_8_GAIN	0

///////////////////////////////////////////////////////
#define MAX_CHANNEL 8
#define BUF_NUM 15

static char *fifo_ch[] =
{
	"/tmp/s9c-fifo-ch1", /* ch1 playback device */
	"/tmp/s9c-fifo-ch2",  /* ch2 playback device */
	"/tmp/s9c-fifo-ch3",  /* ch3 playback device */
	"/tmp/s9c-fifo-ch4",  /* ch4 playback device */
	"/tmp/s9c-fifo-ch5", /* ch5 playback device */
	"/tmp/s9c-fifo-ch6",  /* ch6 playback device */
	"/tmp/s9c-fifo-ch7",  /* ch7 playback device */
	"/tmp/s9c-fifo-ch8"  /* ch8 playback device */
};

int handle_fifo[MAX_CHANNEL] = { 0,0,0,0,0,0,0,0 };
static int channel[MAX_CHANNEL]={ 0,1,2,3,4,5,6,7 };

float gains[]={
pow(10.0,CH_1_GAIN/20.0),
pow(10.0,CH_2_GAIN/20.0),
pow(10.0,CH_3_GAIN/20.0),
pow(10.0,CH_4_GAIN/20.0),
pow(10.0,CH_5_GAIN/20.0),
pow(10.0,CH_6_GAIN/20.0),
pow(10.0,CH_7_GAIN/20.0),
pow(10.0,CH_8_GAIN/20.0)
};


static int freq[] =
{
	CH_1_FREQ,
	CH_2_FREQ,
	CH_3_FREQ,
	CH_4_FREQ,
	CH_5_FREQ,
	CH_6_FREQ,
	CH_7_FREQ,
	CH_8_FREQ
};




int do_exit = 0;

pthread_cond_t cond;
pthread_mutex_t mutex;
int head = 0;
int used = 0;
uint8_t buf[BUF_NUM][24576];



pthread_cond_t iq_cond[MAX_CHANNEL];
pthread_mutex_t iq_mutex[MAX_CHANNEL];
pthread_t threadid[MAX_CHANNEL];
int iq_head[MAX_CHANNEL] = { 0,0,0,0,0,0,0,0 };
int iq_used[MAX_CHANNEL] = { 0,0,0,0,0,0,0,0 };
uint8_t iqin[MAX_CHANNEL][BUF_NUM][3072];
float iqout[MAX_CHANNEL][1024];
short out_s16[MAX_CHANNEL][1024];
int thread_start[MAX_CHANNEL] = { 0,0,0,0,0,0,0,0 };

static void* iq_threadproc(void* arg) {

	int res = 0;
	int ch = *(int *)arg;

	printf("channel %d work\n", ch);
	while (!do_exit) {
		pthread_mutex_lock(&iq_mutex[ch]);
		while (iq_used[ch] == 0) {
			pthread_cond_wait(&iq_cond[ch], &iq_mutex[ch]);
		}
		pthread_mutex_unlock(&iq_mutex[ch]);

		uint8_t *_buf = (uint8_t *)iqin[ch][iq_head[ch]];
		for (int i = 0; i < 1024; i++) {
			int temp = (*((int *)(&_buf[i * 3])));
			if (_buf[i * 3 + 2] & 0x80) {
				temp |= 0xff000000;
			}
			else {
				temp &= 0xffffff;
			}
			iqout[ch][i] = (int16_t)temp / (float)8388608.0*gains[ch];
		}
		//-----------------swap IQ------------------
		for (int i = 0; i < 512; i++) {
			out_s16[ch][i * 2] = iqout[ch][i * 2 + 1] * SHRT_MAX;
			out_s16[ch][i * 2 + 1] = iqout[ch][i * 2] * SHRT_MAX;
		}


		pthread_mutex_lock(&iq_mutex[ch]);
		iq_head[ch] = (iq_head[ch] + 1) % BUF_NUM;
		iq_used[ch]--;
		pthread_mutex_unlock(&iq_mutex[ch]);

		res = write(handle_fifo[ch], out_s16[ch], 1024 * sizeof(short));
	}
}

int callback(s9c_transfer* transfer) {

	uint8_t * samples = (uint8_t *)transfer->samples;
	pthread_mutex_lock( &mutex );	
	int tril=(head+used)%BUF_NUM;
	memcpy(buf[tril],samples, 24576);
	if(used==BUF_NUM){
		head =(head +1)%BUF_NUM;
	}else{
		used++;
	}
	pthread_mutex_unlock( &mutex );
	pthread_cond_signal(&cond);
	
	return 0;
	}



int main(int argc ,char* argv[])
{
	int channel_max = CHANNEL_NUM;
	for (int i = 0; i < channel_max; i++) {
		
		unlink(fifo_ch[i]);
		mkfifo(fifo_ch[i],0666);
		handle_fifo[i] = open(fifo_ch[i], O_RDWR);

		pthread_cond_init(&iq_cond[i], NULL);
		pthread_mutex_init(&iq_mutex[i], NULL);
	}

	int ret;
	s9c_device* dev;
	ret=s9c_open(&dev);
	if(ret<0){
		printf("s9c_open Filed%d\n", ret);
	}
	ret = s9c_load_fpga(dev, "S9_C.rbf");
	if(ret<0){	
		printf("s9c_load_fpga Failed:%d\n", ret);
	}

	ret=s9c_set_gain(dev,AMP_GAIN);
	ret = s9c_start_rx(dev, callback, S9C_FLAG_SAMPLE_TYPE_RAW, NULL);
	if(ret<0){
		printf("s9c_start_rx Failed:%d\n", ret);
	}

	do_exit = 0;

	for (int i = 0; i < channel_max; i++) {

	ret = s9c_set_multichannel_frequency(dev, freq[i], i);
	if (ret<0) {
		printf("s9c_set_frequency Failed:%d\n", ret);
	}
	else {
		printf("channel:%d set freq:%d kHZ\n", i, freq[i]/1000);
	
	}
	
	int ret=pthread_create(&threadid[i], 0, iq_threadproc, &channel[i]);
	if (ret == 0) {
		thread_start[i] = 1;
		}
	}

		while (1){
			pthread_mutex_lock( &mutex );
			while(used==0){
				pthread_cond_wait(&cond,&mutex);
			}
			pthread_mutex_unlock( &mutex );


			uint8_t *_buf = (uint8_t *)buf[head];

			for (int i = 0; i < channel_max; i++) {
				pthread_mutex_lock(&iq_mutex[i]);

				int tril = (iq_head[i] + iq_used[i]) %BUF_NUM;;
				memcpy(iqin[i][tril], &_buf[i*3072], 3072);

				if (iq_used[i] == BUF_NUM) {
					iq_head[i] = (iq_head[i] + 1) % BUF_NUM;
				}
				else {
					iq_used[i]++;
				}

				pthread_mutex_unlock(&iq_mutex[i]);
				pthread_cond_signal(&iq_cond[i]);

			}

			pthread_mutex_lock( &mutex );
			head=(head+1)%BUF_NUM;
			used--;
			pthread_mutex_unlock( &mutex );

		}


		do_exit = 1;

		for (int i = 0; i < channel_max; i++) {
			void* value=NULL;
			if (thread_start[i]) {
				int ret = pthread_join(threadid[i], &value);
				if (ret == 0) {
					thread_start[i] = 0;
				}
			}
		}

		for (int i = 0; i < channel_max; i++) {
		
			if (handle_fifo[i] != 0) {
				close(handle_fifo[i]);
				handle_fifo[i] = 0;
				}
		
		}

		return 0;
	}




