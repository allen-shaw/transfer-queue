#include <iostream>

#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/thread.hh>
#include <seastar/util/log.hh>

#include "transfer_queue/client/client.h"

static seastar::logger client_logger("tq-client");

int main(int argc, char** argv) {
    seastar::app_template app;

    // TODO: 添加命令行参数
    // app.add_options()
    //     ("host", ..., "Server host")
    //     ("port", ..., "Server RPC port")
    //     ("command", ..., "Command: write|read|status|subscribe")
    //     ...

    return app.run(argc, argv, [] {
        return seastar::async([] {
            client_logger.info("TransferQueue Client starting...");

            // TODO: 实现 CLI 逻辑
            // 1. 解析参数，获取 host, port, command
            // 2. 创建 TransferQueueClient
            // 3. 连接
            // 4. 根据 command 执行对应操作
            //    - write:     从 stdin 或文件读取数据，调用 batch_write
            //    - read:      调用 batch_read，打印结果
            //    - status:    调用 get_status，打印状态
            //    - subscribe: 调用 subscribe，持续打印推送数据
            // 5. 关闭连接

            client_logger.info("TransferQueue Client done");
        });
    });
}
