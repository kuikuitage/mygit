#进程池
from multiprocessing import Pool,Process
import time,random
def foo():
    print("孙子进程开始...")
    print("孙子进程结束...")
def func(name):
    #print("子进程%s启动..."%(name))
    print("%d" %name)
    #print("子进程%s结束...耗时%.2f"%(name,end - start))

if __name__ == '__main__':
    print("父进程开始...")
    # 创建进程池
    # 如果没有参数  默认大小为自己电脑的CPU核心数
    # 表示可以同时执行的进程数量
    pp = Pool(200)
    for i in range(100000):
        # 创建进程,放入进程池统一管理
        pp.apply_async(func,args=(i,))
        # 在调用join之前必须先关掉进程池
    # 进程池一旦关闭  就不能再添加新的进程了
    pp.close()
    # 进程池对象调用join,会等待进程池中所有的子进程结束之后再结束父进程
    pp.join()
    print("父进程结束...")
