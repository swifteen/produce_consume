#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

static  FILE* fp_read_simulate_file = NULL;//真实读取到的模拟数据
static int g_run_flag = 1;//线程运行标识
static char g_simulate_file_path[128] = {0};//模拟测试文件路径
static int g_simulate_rollback = 0;//读取结束后是否重头读取模拟文件,0代表不回头，1代表重头读取模拟

#define BUFFER_SIZE (1024)

typedef struct{
	int a;
	int b;
}MyData;

// 循环缓冲区结构体
typedef struct {
    MyData *buffer;  // 缓冲区数据
    int size;     // 缓冲区大小
    int write_idx;       // 生产者写入位置
    int read_idx;      // 消费者读取位置
    int count;    // 缓冲区中的数据数量
    pthread_mutex_t lock;  // 互斥锁
    pthread_cond_t full;   // 缓冲区满条件变量
    pthread_cond_t empty;  // 缓冲区空条件变量
    
    int *buf_used_count;  // 缓冲区每个MyData数据被使用的记数
} BoundedBuffer;

BoundedBuffer g_buffer;

void get_write_idx(int* write_idx)
{
	pthread_mutex_lock(&g_buffer.lock);
	
	// 等待缓冲区非满
	while ((g_buffer.count == g_buffer.size) 
		|| (g_buffer.buf_used_count[g_buffer.write_idx] > 0)) {
		pthread_cond_wait(&g_buffer.full, &g_buffer.lock);
	}
	*write_idx = g_buffer.write_idx;
	pthread_mutex_unlock(&g_buffer.lock);
}

void write_data()
{
	pthread_mutex_lock(&g_buffer.lock);
	g_buffer.write_idx = (g_buffer.write_idx + 1) % g_buffer.size;
	g_buffer.count++;
	// 唤醒一个消费者
	//pthread_cond_signal(&g_buffer.empty);	
	pthread_cond_broadcast(&g_buffer.empty);
	pthread_mutex_unlock(&g_buffer.lock);
}

int write_data_block(int* data,int data_count)
{
	return 0;
}

int read_first_data(MyData** data,int* read_idx)
{
	pthread_mutex_lock(&g_buffer.lock);
	
	// 等待缓冲区非空
	while (0 == g_buffer.count) {
		pthread_cond_wait(&g_buffer.empty, &g_buffer.lock);
	}
	if(0 == g_buffer.write_idx)
	{
		*read_idx = g_buffer.size - 1;
	}
	else
	{
		*read_idx = g_buffer.write_idx - 1;
	}
	g_buffer.buf_used_count[*read_idx]++;//将使用计数加加
	*data = &(g_buffer.buffer[*read_idx]);
	
	pthread_mutex_unlock(&g_buffer.lock);
	return 0;
}

int avilable_read_len(int last_read_idx)
{
	if(last_read_idx < g_buffer.write_idx)
	{
		return g_buffer.write_idx - last_read_idx;
	}
	return g_buffer.size - last_read_idx + g_buffer.write_idx;
}

int read_data(MyData** data,int last_read_idx,int* read_idx)
{
	assert(0 <= last_read_idx && last_read_idx < g_buffer.size);
	pthread_mutex_lock(&g_buffer.lock);
	
	// 等待缓冲区非空
	while ((0 == g_buffer.count) 
		|| (0 == avilable_read_len(last_read_idx))){
		pthread_cond_wait(&g_buffer.empty, &g_buffer.lock);
	}
	if(last_read_idx == g_buffer.size - 1)
	{
		*read_idx = 0;
	}
	else
	{
		*read_idx = last_read_idx + 1;
	}
	g_buffer.buf_used_count[*read_idx]++;//将使用计数加加
	*data = &(g_buffer.buffer[*read_idx]);
	
	pthread_mutex_unlock(&g_buffer.lock);
	return 0;
}

void release_data(int read_idx)
{
	assert(g_buffer.buf_used_count[read_idx] > 0);
	pthread_mutex_lock(&g_buffer.lock);
	g_buffer.buf_used_count[read_idx]--;
	if(0 == g_buffer.buf_used_count[read_idx])
	{
		g_buffer.count--;
	}
	// 唤醒生产者
	pthread_cond_signal(&g_buffer.full);
	pthread_mutex_unlock(&g_buffer.lock);
}

void *producer(void *arg) {
    if ((fp_read_simulate_file = fopen(g_simulate_file_path, "r")) == NULL)
    {
        printf("\n Can't open simulate file[%s]\n", g_simulate_file_path);
        return 0;
    }
	int ret = 0;
    while (g_run_flag) {
		int write_idx = 0;
		get_write_idx(&write_idx);
		
		ret = fread(&g_buffer.buffer[write_idx], sizeof(MyData), 1, fp_read_simulate_file); /* 读串口数据 */
		
		if (ret != 1)
		{
			perror("read simulate file error:");
			printf("read simulate file[%s] finish,result[%d]\n", g_simulate_file_path, ret);
		
			if (1 == g_simulate_rollback)
			{
				fseek(fp_read_simulate_file, 0, SEEK_SET);
				printf("read simulate file[%s] rollback\n", g_simulate_file_path);
				continue;
			}
			else
			{
				g_run_flag = 0;
				break;
			}
		}
		write_data();
    }
    if (fp_read_simulate_file)
    {
        fclose(fp_read_simulate_file);
        fp_read_simulate_file = NULL;
    }
    return NULL;
}

void *consumer(void *arg) {
	int id = *((int*)arg);
	int last_read_idx = -1;
    char recv_path[128];
    snprintf(recv_path, sizeof(recv_path), "/tmp/buffer_%d.dat",id);
    FILE* fp = NULL;

    if ((fp = fopen(recv_path, "wb")) == NULL)
    {
        printf("\n Can't open buffer  \n");
        return NULL;
    }
	int ret = 0;
	MyData* p_data = NULL;
    while (g_run_flag) {
		if(last_read_idx > 0)
		{
			int cur_read_idx = -1;
			if(read_data(&p_data,last_read_idx,&cur_read_idx) == 0)
			{
				last_read_idx = cur_read_idx;
			}
		}
		else
		{
			read_first_data(&p_data,&last_read_idx);
		}
	    ret = fwrite(p_data, sizeof(MyData), 1, fp);

	    if (ret < 0)
	    {
	        printf("fwrite write nread len error[%d][%d]\n", ret, errno);
	    }
		release_data(last_read_idx);
    }
    fclose(fp);
    return NULL;
}

int main(int argc, char** argv) {
	snprintf(g_simulate_file_path, sizeof(g_simulate_file_path), "%s",argv[1]);

    // 初始化缓冲区
    g_buffer.buffer = (MyData *)malloc(sizeof(MyData) * BUFFER_SIZE);
	g_buffer.buf_used_count  = (int *)malloc(sizeof(int) * BUFFER_SIZE);
	memset(g_buffer.buf_used_count,0,sizeof(int) * BUFFER_SIZE);
    g_buffer.size = BUFFER_SIZE;
    g_buffer.write_idx = 0;
    g_buffer.read_idx = 0;
    g_buffer.count = 0;
    pthread_mutex_init(&g_buffer.lock, NULL);
    pthread_cond_init(&g_buffer.full, NULL);
    pthread_cond_init(&g_buffer.empty, NULL);
    
    // 创建生产者线程
    pthread_t producerThreadId;
    pthread_create(&producerThreadId, NULL, producer, NULL);
    
    // 创建多个消费者线程
    int numConsumers = 3;
    pthread_t consumerThreadIds[numConsumers];
    for (long i = 0; i < numConsumers; i++) {
        pthread_create(&consumerThreadIds[i], NULL, consumer, (void *)&i);
    }
    
    // 等待生产者和消费者线程结束
    pthread_join(producerThreadId, NULL);
    for (int i = 0; i < numConsumers; i++) {
        pthread_join(consumerThreadIds[i], NULL);
    }
    
    // 销毁互斥锁和条件变量，释放缓冲区内存
    pthread_mutex_destroy(&g_buffer.lock);
    pthread_cond_destroy(&g_buffer.full);
    pthread_cond_destroy(&g_buffer.empty);
    free(g_buffer.buffer);
    free(g_buffer.buf_used_count);
    return 0;
}

