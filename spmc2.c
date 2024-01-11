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
#define DEBUG_PN(msg, args...)        do{printf("[%s][%d]:\t ", __FUNCTION__ , __LINE__ );printf(msg, ##args);}while(0)
#define DEBUG_PD(msg, args...)        do{DUMP_GREEN; printf("[%s][%d]:\t ", __FUNCTION__ , __LINE__ );printf(msg, ##args);DUMP_NONE;}while(0)
#define DEBUG_PW(msg, args...)        do{DUMP_RED; printf("[%s][%d]:\t ", __FUNCTION__ , __LINE__ );printf(msg, ##args);DUMP_NONE;}while(0)
#else
#define DEBUG_PN(msg, args...)        
#define DEBUG_PD(msg, args...)        
#define DEBUG_PW(msg, args...)        
#endif

#define BUFFER_SIZE (1024)
#define MAGIC_NUMBER (0xAACC9527)
#define CONSUMER_NUM (3)
#define DEBUG_MAX_SEQ_NO (100)

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
    int read_idx;      // 消费者读取位置
    pthread_mutex_t lock;  // 互斥锁
    pthread_cond_t full;   // 缓冲区满条件变量
    pthread_cond_t empty;  // 缓冲区空条件变量
    
    int *buf_used_count;  // 缓冲区每个MyData数据被使用的记数
} BoundedBuffer;

BoundedBuffer g_buffer;

int avilable_read_len(int last_read_idx)
{
    if(last_read_idx < g_buffer.write_idx)
    {
        return g_buffer.write_idx - last_read_idx - 1;
    }
    return g_buffer.size - last_read_idx + g_buffer.write_idx - 1;
}

void get_write_idx(int* write_idx)
{
    pthread_mutex_lock(&g_buffer.lock);

    if(g_buffer.read_idx != g_buffer.write_idx)
    {
        // 等待缓冲区非满
        while ((avilable_read_len(g_buffer.read_idx) == g_buffer.size - 1)
               || (g_buffer.buf_used_count[g_buffer.write_idx] > 0)) {
            pthread_cond_wait(&g_buffer.full, &g_buffer.lock);
        }
    }
    *write_idx = g_buffer.write_idx;
    pthread_mutex_unlock(&g_buffer.lock);
}

void get_write_pos(MyData** data,int* write_idx)
{
    pthread_mutex_lock(&g_buffer.lock);

    if(g_buffer.read_idx != g_buffer.write_idx)
    {
        // 等待缓冲区非满
        while ((avilable_read_len(g_buffer.read_idx) == g_buffer.size - 1)
               || (g_buffer.buf_used_count[g_buffer.write_idx] > 0)) {
            pthread_cond_wait(&g_buffer.full, &g_buffer.lock);
        }
    }
    *data = &g_buffer.buffer[g_buffer.write_idx];
    *write_idx = g_buffer.write_idx;
    DEBUG_PN("write_idx[%d],read_idx[%d],count[%d]\n",
             g_buffer.write_idx,g_buffer.read_idx,avilable_read_len(g_buffer.read_idx));
    pthread_mutex_unlock(&g_buffer.lock);
}

void write_data()
{
    pthread_mutex_lock(&g_buffer.lock);
    g_buffer.write_idx = (g_buffer.write_idx + 1) % g_buffer.size;
    DEBUG_PN("write_idx[%d],read_idx[%d],count[%d]\n",
             g_buffer.write_idx,g_buffer.read_idx,avilable_read_len(g_buffer.read_idx));
    // 唤醒一个消费者
    //pthread_cond_signal(&g_buffer.empty);
    pthread_cond_broadcast(&g_buffer.empty);
    pthread_mutex_unlock(&g_buffer.lock);
}

int write_data_block(int* data,int data_count)
{
    return 0;
}

int read_first_data(int consumerId,MyData** data,int* read_idx)
{
    pthread_mutex_lock(&g_buffer.lock);

    // 等待缓冲区非空
    while (g_buffer.read_idx == g_buffer.write_idx) {
        pthread_cond_wait(&g_buffer.empty, &g_buffer.lock);
    }
    DEBUG_PD("consumerId[%d],write_idx[%d],read_idx[%d],count[%d],buf_used_count[%d]=[%d]\n",
             consumerId,
             g_buffer.write_idx,
             g_buffer.read_idx,
             avilable_read_len(g_buffer.read_idx),
             g_buffer.read_idx,g_buffer.buf_used_count[g_buffer.read_idx]);
    if(0 == g_buffer.write_idx)
    {
        g_buffer.read_idx = g_buffer.size - 1;
    }
    else
    {
        g_buffer.read_idx = g_buffer.write_idx - 1;
    }
    g_buffer.buf_used_count[g_buffer.read_idx]++;//将使用计数加加
    assert(g_buffer.buf_used_count[g_buffer.read_idx] <= CONSUMER_NUM);
    *data = &(g_buffer.buffer[g_buffer.read_idx]);
    *read_idx = g_buffer.read_idx;
    DEBUG_PD("consumerId[%d],write_idx[%d],read_idx[%d],count[%d],buf_used_count[%d]=[%d]\n",
             consumerId,
             g_buffer.write_idx,
             g_buffer.read_idx,
             avilable_read_len(g_buffer.read_idx),
             *read_idx,g_buffer.buf_used_count[*read_idx]);
    pthread_mutex_unlock(&g_buffer.lock);
    return 0;
}

int read_data(int consumerId,MyData** data,int last_read_idx,int* read_idx)
{
    assert(0 <= last_read_idx && last_read_idx < g_buffer.size);
    pthread_mutex_lock(&g_buffer.lock);

    // 等待缓冲区非空
    while ((g_buffer.read_idx == g_buffer.write_idx)
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
    DEBUG_PD("consumerId[%d],write_idx[%d],read_idx[%d],count[%d],buf_used_count[%d]=[%d],last_read_idx[%d],avilable_read_len[%d]\n",
             consumerId,g_buffer.write_idx,g_buffer.read_idx,avilable_read_len(g_buffer.read_idx),*read_idx,
             g_buffer.buf_used_count[*read_idx],last_read_idx,avilable_read_len(last_read_idx));
    pthread_mutex_unlock(&g_buffer.lock);
    return 0;
}

void release_read_data(int consumerId,int read_idx)
{
    assert(g_buffer.buf_used_count[read_idx] > 0);
    pthread_mutex_lock(&g_buffer.lock);
	DEBUG_PW("consumerId[%d],seqNo[%d],write_idx[%d],read_idx[%d],count[%d],buf_used_count[%d] = [%d]\n",
			 consumerId,g_buffer.buffer[read_idx].seqNo,g_buffer.write_idx,g_buffer.read_idx,avilable_read_len(g_buffer.read_idx),
			 g_buffer.read_idx,g_buffer.buf_used_count[g_buffer.read_idx]);
    g_buffer.buf_used_count[read_idx]--;
    if(0 == g_buffer.buf_used_count[read_idx])
    {
        DEBUG_PW("consumerId[%d],seqNo[%d],write_idx[%d],read_idx[%d],count[%d],buf_used_count[%d]\n",
                 consumerId,g_buffer.buffer[g_buffer.read_idx].seqNo,g_buffer.write_idx,g_buffer.read_idx,
                 avilable_read_len(g_buffer.read_idx),g_buffer.buf_used_count[read_idx]);
        g_buffer.read_idx = (g_buffer.read_idx + 1) % g_buffer.size;
    }
    // 唤醒生产者
    pthread_cond_signal(&g_buffer.full);
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
	//等待消费者全部消费完成
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
        write_data();
        if(g_seqNo > DEBUG_MAX_SEQ_NO)
        {
            g_run_flag = 0;
        }
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
    int last_read_idx = -1;
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
	//让生产者退出
		sem_post(&g_consumerSema[consumerId]);
#endif
	DEBUG_PN("start consumer[%d] = [%s]\n",consumerId, consumer_file_path);
    int ret = 0;
    MyData* pData = NULL;
    while (g_run_flag) {
        if(last_read_idx > 0)
        {
            int cur_read_idx = -1;
            if(read_data(consumerId,&pData,last_read_idx,&cur_read_idx) == 0)
            {
                last_read_idx = cur_read_idx;
            }
//            if(g_lastSeqNo[consumerId] + 1 != pData->seqNo)
//            {
//                DEBUG_PW("g_lastSeqNo[%d] =[%d]  != seqNo[%d],write_idx[%d],read_idx[%d],count[%d],buf_used_count[%d] = [%d]\n",
//                         consumerId,g_lastSeqNo[consumerId],pData->seqNo,g_buffer.write_idx,g_buffer.read_idx,avilable_read_len(g_buffer.read_idx),g_buffer.read_idx,g_buffer.buf_used_count[g_buffer.read_idx]);
//                assert(g_lastSeqNo[consumerId] + 1 == pData->seqNo);
//            }
            g_lastSeqNo[consumerId] = pData->seqNo;
        }
        else
        {
            read_first_data(consumerId,&pData,&last_read_idx);
            g_lastSeqNo[consumerId] = pData->seqNo;
            assert(last_read_idx >= 0);
        }
        ret = fwrite(pData, sizeof(MyData), 1, fp);

        if (ret < 0)
        {
            printf("fwrite write nread len error[%d][%d]\n", ret, errno);
            break;
        }
        release_read_data(consumerId,last_read_idx);
    }
    fclose(fp);
    return NULL;
}

int main(int argc, char** argv) {
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
    g_buffer.buf_used_count  = (int *)malloc(sizeof(int) * BUFFER_SIZE);
    memset(g_buffer.buf_used_count,0,sizeof(int) * BUFFER_SIZE);
    g_buffer.size = BUFFER_SIZE;
    g_buffer.write_idx = 0;
    g_buffer.read_idx = 0;
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

