#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <semaphore.h>

#define DUMP_RED printf("\033[0;32;31m")
#define DUMP_YELLOW printf("\033[1;33m")
#define DUMP_GREEN printf("\033[0;32;32m")
#define DUMP_NONE printf("\033[m")
#if 1
#define DEBUG_PN(msg, args...)        do{printf("[%s][%d]:\t\t ", __FUNCTION__ , __LINE__ );printf(msg, ##args);}while(0)
#define DEBUG_PD(msg, args...)        do{DUMP_GREEN; printf("[%s][%d]:\t\t ", __FUNCTION__ , __LINE__ );printf(msg, ##args);DUMP_NONE;}while(0)
#define DEBUG_PW(msg, args...)        do{DUMP_RED; printf("[%s][%d]:\t\t ", __FUNCTION__ , __LINE__ );printf(msg, ##args);DUMP_NONE;}while(0)
#else
#define DEBUG_PN(msg, args...)        
#define DEBUG_PD(msg, args...)        
#define DEBUG_PW(msg, args...)        
#endif

#define BUFFER_SIZE (1024)
#define MAGIC_NUMBER (0xAACC9527)
#define CONSUMER_NUM (10)
#define DEBUG_MAX_SEQ_NO (10)

static  FILE* g_simulate_fp = NULL;//真实读取到的模拟数据
static int g_run_flag = 1;//线程运行标识
static char g_output_dir[128] = {0};//模拟测试文件路径
static int g_simulate_rollback = 0;//读取结束后是否重头读取模拟文件,0代表不回头，1代表重头读取模拟
static int g_seqNo = 0;//模拟数据序列号
static int g_lastSeqNo[CONSUMER_NUM] = {0};//保存上次包序号，用于debug
static sem_t g_produceSema;
static sem_t g_consumerSema[CONSUMER_NUM];

typedef struct{
    int magic;
    uint64_t seqNo;
    int count;
    int write_idx;       // 生产者写入位置
    int read_idx;      // 消费者读取位置
}MyData;

// 循环缓冲区结构体
typedef struct {
    MyData *buffer;  // 缓冲区数据
    int size;     // 缓冲区大小
    int write_idx;       // 生产者写入位置
    int read_idx[CONSUMER_NUM];      // 消费者读取位置
    pthread_mutex_t lock;  // 互斥锁
    pthread_cond_t full;   // 缓冲区满条件变量
    pthread_cond_t empty;  // 缓冲区空条件变量
    
    int *buf_used_count;  // 缓冲区每个MyData数据被使用的记数，用于优化减少判断，空间换时间
} BoundedBuffer;

BoundedBuffer g_buffer;

//从3个消费者中找到最小的可写长度
int avilable_write_len()
{
	int min_avilable_write_len = BUFFER_SIZE;
	for (int i = 0; i < CONSUMER_NUM; i++)
	{
		int len = 0;
		if(g_buffer.read_idx[i] > g_buffer.write_idx)
		{
			len = g_buffer.read_idx[i] - g_buffer.write_idx;
		}
		else
		{
			len = g_buffer.size - g_buffer.write_idx  + g_buffer.read_idx[i];
		}
		//取最小的可写入长度，它代表了最慢的那个消费者
		if(len < min_avilable_write_len)
		{
			min_avilable_write_len = len;
		}
	}
	return min_avilable_write_len;
}

/*
可写的情况，需要处理第一次启动时，读写指针相等的情况
1、写指针对应的使用计数等于0，代表正在被占用，加快判断
2、写指针不能超过所有的读指针
*/
bool avilable_write()
{
	if((g_buffer.buf_used_count[g_buffer.write_idx] > 0)
		|| (avilable_write_len() <= 1))//这里判断可写入的长度大于1，是为了让读写指针在启动之后，永不相遇
	{
		return false;
	}
	return true;
}

//阻塞等待，直到写指针位置可写入
void get_write_pos(MyData** data,int* write_idx)
{
    pthread_mutex_lock(&g_buffer.lock);
    // 等待缓冲区非满
    while (!avilable_write()) {
        pthread_cond_wait(&g_buffer.full, &g_buffer.lock);
    }
    *data = &g_buffer.buffer[g_buffer.write_idx];
    *write_idx = g_buffer.write_idx;
    pthread_mutex_unlock(&g_buffer.lock);
}

//将写指针前移
void write_one_data()
{
    pthread_mutex_lock(&g_buffer.lock);
    g_buffer.write_idx = (g_buffer.write_idx + 1) % g_buffer.size;
    pthread_cond_broadcast(&g_buffer.empty);// 唤醒所有消费者
    pthread_mutex_unlock(&g_buffer.lock);
}

//获取可读取的长度
int avilable_read_len(int read_idx)
{
	if(read_idx == g_buffer.write_idx)
	{
		return 0;
	}
    else if(read_idx < g_buffer.write_idx)
    {
        return g_buffer.write_idx - read_idx - 1;
    }
    return g_buffer.size + g_buffer.write_idx  - read_idx - 1;
}

/*
读指针此时不更新，让写指针不要越过读指针，读取完成后，再将读指针前移
也就是g_buffer.read_idx始终指向下一个即将要读的位置
判断是否可读的条件是,读指针与写指针不相同，且可读取的长度不为0，也就是写指针要超前读指针
*/
int read_data(int consumerId,bool bFirst,MyData** data)
{
    pthread_mutex_lock(&g_buffer.lock);
    assert(0 <= g_buffer.read_idx[consumerId] && g_buffer.read_idx[consumerId] < g_buffer.size);
    // 等待缓冲区非空
    while (avilable_read_len(g_buffer.read_idx[consumerId]) <= 1) {
        pthread_cond_wait(&g_buffer.empty, &g_buffer.lock);
    }
	int read_idx = 0;
	if(!bFirst)//如果不是第一次读取数据，则此次直接读取g_buffer.read_idx位置的数据，因为g_buffer.read_idx指针下一次读取的位置
	{
		read_idx = g_buffer.read_idx[consumerId];
	}
	else//如果第一次读取数据，则将读取指针指向写指针的前一个位置，上面的可读长度判断已经保证此时写指针一定超前读指针了
	{
		if(0 == g_buffer.write_idx)
		{
			read_idx = g_buffer.size - 1;
		}
		else
		{
			read_idx = g_buffer.write_idx - 1;
		}
		g_buffer.read_idx[consumerId] = read_idx;
	}
    g_buffer.buf_used_count[read_idx]++;//将使用计数加加
    assert(g_buffer.buf_used_count[read_idx] <= CONSUMER_NUM);
    *data = &(g_buffer.buffer[read_idx]);
    pthread_mutex_unlock(&g_buffer.lock);
    return 0;
}

//将读指针前移，并将使用计数减减，再唤醒生产者
void release_read_data(int consumerId)
{
    pthread_mutex_lock(&g_buffer.lock);
    assert(g_buffer.buf_used_count[g_buffer.read_idx[consumerId]] > 0);
    g_buffer.buf_used_count[g_buffer.read_idx[consumerId]]--;
	g_buffer.read_idx[consumerId] = (g_buffer.read_idx[consumerId] + 1) % g_buffer.size;
    pthread_cond_signal(&g_buffer.full); // 唤醒生产者
    pthread_mutex_unlock(&g_buffer.lock);
}

void simulateData(MyData*pData,int write_idx)
{
    pData->magic = MAGIC_NUMBER;
    pData->seqNo = g_seqNo;
    pData->write_idx = write_idx;
    g_seqNo++;
}

static void *producer(void *arg) {
    char producer_file_path[256];
    snprintf(producer_file_path,sizeof(producer_file_path),"%s/producer.bin",g_output_dir);
    if ((g_simulate_fp = fopen(producer_file_path, "w")) == NULL)
    {
        DEBUG_PN("\n Can't open simulate file[%s][%s]\n", g_output_dir,producer_file_path);
        g_run_flag = 0;
        return NULL;
    }
#if 1
	//等待消费者线程全部启动后，再开始生产
	for (int i = 0; i < CONSUMER_NUM; i++) {
		DEBUG_PN("sem_wait[%d]1\n",i);
		sem_wait(&g_consumerSema[i]);
		DEBUG_PN("sem_wait[%d]2\n",i);
	}
#endif		
	DEBUG_PN("start producer[%s]\n", producer_file_path);
    int ret = 0;
    MyData* pData = NULL;
    int write_idx = 0;
    while (g_run_flag) {
        get_write_pos(&pData,&write_idx);
        simulateData(pData,write_idx);
        ret = fwrite(pData, sizeof(MyData), 1, g_simulate_fp);
        if (ret < 0)
        {
            DEBUG_PN("fwrite write nread len error[%d][%d]\n", ret, errno);
        }
        write_one_data();
    }
    if (g_simulate_fp)
    {
        fclose(g_simulate_fp);
        g_simulate_fp = NULL;
    }
    return NULL;
}

static void *consumer(void *arg) {
    int consumerId = *((int*)arg);
	bool bFirst = true;
    char consumer_file_path[128];
    snprintf(consumer_file_path,sizeof(consumer_file_path),"%s/consumer_%d.bin",g_output_dir,consumerId);
    FILE* fp = NULL;

    if ((fp = fopen(consumer_file_path, "wb")) == NULL)
    {
        DEBUG_PW("Can't open buffer[%s][%s]\n",g_output_dir,consumer_file_path);
        g_run_flag = 0;
        return NULL;
    }
#if 1
	//让生产者开始生产
	sem_post(&g_consumerSema[consumerId]);
#endif
	DEBUG_PN("start consumer[%d] = [%s]\n",consumerId, consumer_file_path);
    int ret = 0;
    MyData* pData = NULL;
    while (g_run_flag) {
		read_data(consumerId,bFirst,&pData);
        if(!bFirst)
        {
            assert(g_lastSeqNo[consumerId] + 1 == pData->seqNo);
        }
		else
		{
			bFirst = false;
		}
        g_lastSeqNo[consumerId] = pData->seqNo;
        ret = fwrite(pData, sizeof(MyData), 1, fp);

        if (ret < 0)
        {
            DEBUG_PD("fwrite write nread len error[%d][%d]\n", ret, errno);
            break;
        }
        release_read_data(consumerId);
    }
    fclose(fp);
    return NULL;
}

int main(int argc, char** argv) {
	if(argc != 2)
	{
		printf("usage: %s output_dir\n",argv[0]);
		return -1;
	}
    snprintf(g_output_dir, sizeof(g_output_dir), "%s",argv[1]);
    // 检查目录是否存在
    if (access(g_output_dir, F_OK) == -1) {
        // 目录不存在，则创建目录
        if (mkdir(g_output_dir, 0777) == 0) {
            printf("mkdir %s success\n",g_output_dir);
        } else {
            printf("mkdir %s failed %s\n", g_output_dir,strerror(errno));
        }
    }

    g_seqNo = 0;//模拟数据序列号

    // 初始化缓冲区
    g_buffer.buffer = (MyData *)malloc(sizeof(MyData) * BUFFER_SIZE);
    memset(g_buffer.buffer,0,sizeof(MyData) * BUFFER_SIZE);
    g_buffer.buf_used_count  = (int *)malloc(sizeof(int) * BUFFER_SIZE);
    memset(g_buffer.buf_used_count,0,sizeof(int) * BUFFER_SIZE);
    g_buffer.size = BUFFER_SIZE;
    g_buffer.write_idx = 0;
    memset(g_buffer.read_idx,0,sizeof(g_buffer.read_idx));
    pthread_mutex_init(&g_buffer.lock, NULL);
    pthread_cond_init(&g_buffer.full, NULL);
    pthread_cond_init(&g_buffer.empty, NULL);

    sem_init(&g_produceSema, 0, 0);

	//设置线程实时优先级
    struct sched_param param;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_RR);
    param.sched_priority = 99;
    pthread_attr_setschedparam(&attr, &param);
	
    // 创建生产者线程
    pthread_t producerThreadId;
    pthread_create(&producerThreadId, &attr, producer, NULL);

    param.sched_priority = 99;
    pthread_attr_setschedparam(&attr, &param);
    // 创建多个消费者线程
    pthread_t consumerThreadIds[CONSUMER_NUM];
    int consumerId[CONSUMER_NUM] = {0};
    for (int i = 0; i < CONSUMER_NUM; i++) {
		sem_init(&g_consumerSema[i], 0, 0);
        consumerId[i] = i;
        pthread_create(&consumerThreadIds[i], &attr, consumer, &consumerId[i]);
    }
    
    // 等待生产者和消费者线程结束
    pthread_join(producerThreadId, NULL);
    for (int i = 0; i < CONSUMER_NUM; i++) {
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

