vscode中的GDB多线程调试注意事项： 
    调试控制台中的执行命令格式为：-exec + 空格 + 命令

多线程常用几个命令：
    info threads: 显示当前可调试的所有线程。每个线程会有GDB为其分配的ID，带'*'号的表示的是当前正在调试的线程
    thread ID, 切换当前调试的线程为指定ID的线程
    set scheduler-locking off|on|step 
        off: 不锁定任何线程，也就是所有线程都执行，这是默认值
        on: 只有当前被调试程序会执行
        step: 单步模式，只有当前线程会执行