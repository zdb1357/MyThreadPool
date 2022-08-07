#ifndef THREADPOLL_H
#define THREADPOLL_H

#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable> //条件变量
#include <functional>
#include <unordered_map>
#include <thread>

//模板的代码只能写在头文件当中，不能h cpp分离
// Any类型：可以接收任意数据的类型
class Any
{
public:
    Any() = default;
    ~Any() = default;
    Any(const Any &) = delete;
    Any &operator=(const Any &) = delete;
    Any(Any &&) = default;
    Any &operator=(Any &&) = default;

    //这个构造函数可以让Any类型接收任意其它的数据
    template <typename T>
    Any(T data) : base_(std::make_unique<Dervice<T>>(data)) {}

    //这个方法能把Any对象里面存储的data对象数据存储出来
    template <typename T>
    T cast_()
    {
        //我们怎么从base_找到它所指向的Dervice对象，从它里面取出data成员变量
        //基类指针 => 派生类指针
        Dervice<T> *pd = dynamic_cast<Dervice<T> *>(base_.get());
        if (pd == nullptr)
        {
            throw "type is unmatch!";
        }
        return pd->data_;
    }

private:
    //基类类型
    class Base
    {
    public:
        virtual ~Base() = default;
    };

    //派生类类型
    template <typename T>
    class Dervice : public Base
    {
    public:
        Dervice(T data) : data_(data) {}
        T data_; //保存了任意的其他类型
    };

private:
    //定义一个基类指针
    std::unique_ptr<Base> base_;
};

//实现一个信号量类
class Semaphore
{
public:
    Semaphore(int limit = 0) : resLimit_(limit), isExit_(false) {}
    ~Semaphore()
    {
        isExit_ = true;
    }

    //获取一个个信号量资源
    void wait()
    {
        if (isExit_)
            return;
        std::unique_lock<std::mutex> lock(mtx_);
        //等待信号量有资源，没有资源的话，会阻塞当前线程
        cond_.wait(lock, [&]() -> bool
                   { return resLimit_ > 0; });
        resLimit_--;
    }

    //增加一个信号量资源
    void post()
    {
        if (isExit_)
            return;
        std::unique_lock<std::mutex> lock(mtx_);
        resLimit_++;
        // linux下condition_variable的析构函数什么也没做
        // 导致这里状态已经失效，无故阻塞
        cond_.notify_all(); // 等待状态，释放mutex锁 通知条件变量wait的地方，可以起来干活了
    }

private:
    std::atomic_bool isExit_;
    int resLimit_;
    std::mutex mtx_;
    std::condition_variable cond_;
};

// Task类型的前置声明
class Task;
//实现接收提交到线程池的task任务执行完成后的返回值类型Result
class Result
{
public:
    Result(std::shared_ptr<Task> task, bool isValid = true);
    ~Result() = default;

    //问题一：setVal方法，获取执行完的返回值
    void setVal(Any any);

    //问题二：get方法，用户调用这个方法获取task的返回值
    Any get();

private:
    Any any_;                    //存储任务的返回值
    Semaphore sem_;              //线程通信信号量
    std::shared_ptr<Task> task_; //指向对应获取返回值的任务对象
    std::atomic_bool isValid_;   //返回值是否有效
};

//任务抽象基类
class Task
{
public:
    Task();
    ~Task() = default;

    void exec();
    void setResult(Result *res);

    //用户可以自定义任意任务类型，从Task继承，重写run方法，实现自定义类型处理
    virtual Any run() = 0;

private:
    Result *result_; // Result对象的生命周期  强于  Task
};

//线程池支持的模式
enum class PoolMode
{
    MODE_FIXED,  //固定数量的线程
    MODE_CACHED, //线程数量可动态增长
};

//线程类型
class Thread
{
public:
    //线程函数对象类型
    using ThreadFunc = std::function<void(int)>;

    //线程构造和析构
    Thread(ThreadFunc func);
    ~Thread();

    //启动线程
    void start();

    //获取线程id
    int getId() const;

private:
    ThreadFunc func_;
    static int generateId_;
    int threadId_; //保存线程id
};

/**
 * example:
 * ThreadPool pool;
 * pool.start(4);
 *
 * class MyTask : public Task
 * {
 * public:
 *      void run() {//线程代码...}
 * };
 *
 * pool.submitTask(std::make_shared<MyTask>());
 */

//线程池类型
class ThreadPool
{
public:
    //构造和析构
    ThreadPool();
    ~ThreadPool();

    //设置线程池工作模式
    void setMode(PoolMode mode);

    //设置task任务队列上限阈值
    void setTaskQueMaxThreadHold(int threadhold);

    //设置线程池cached模式下线程阈值
    void setThreadSizeThreshHold(int threadhold);

    //给线程池提交任务
    Result submitTask(std::shared_ptr<Task> sp);

    //开启线程池，并初始化线程数量
    void start(int initThreadSize = std::thread::hardware_concurrency()); //系统CPU核心数量
    // void start(int initThreadSize = 4);
    //禁止拷贝构造和赋值
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

private:
    //定义线程函数
    void threadFunc(int threadid);

    //检查pool的运行状态
    bool checkRunningState() const;

private:
    // std::vector<std::unique_ptr<Thread>> threads_; //线程列表
    std::unordered_map<int, std::unique_ptr<Thread>> threads_; //线程列表

    size_t initThreadSize_;          //初始线程数量
    int threadSizeThreshHold_;       //线程数量上限阈值
    std::atomic_int curThreadSize_;  //记录当前线程池里面线程的总数量
    std::atomic_int idleThreadSize_; //记录空闲线程的数量

    std::queue<std::shared_ptr<Task>> taskQue_; //任务队列
    std::atomic_uint taskSize_;                 //任务的数量
    int taskQueMaxThreshHold_;                  //任务队列数量上限阈值

    std::mutex taskQueMtx_;            //保证任务队列的线程安全
    std::condition_variable notFull_;  //表示任务队列不满
    std::condition_variable notEmpty_; //表示任务队列不空
    std::condition_variable exitCond_; //等待线程资源全部回收

    PoolMode poolMode_;              //当前线程池的工作模式
    std::atomic_bool isPoolRunning_; //表示当前线程池的启动状态
};

#endif