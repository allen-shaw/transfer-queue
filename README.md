# Transfer-Queue 高性能分布式矩阵存储系统

## 项目简介

Transfer-Queue 是一个专为**后训练工作流**设计的高性能分布式矩阵存储系统，特别适用于大型语言模型(LLM)强化学习后训练场景。系统采用**基于index_id的分片存储**架构，高效存储和检索训练过程中的矩阵数据（prompt、resp、Ref Log Prob、advantage等），实现极简的系统层级和极致性能。

## 核心特性

- **高性能矩阵存储**：专为矩阵数据优化的存储引擎，支持大规模矩阵读写
- **智能分片**：基于index_id自动分片，实现数据均匀分布和高效查询
- **极简架构**：最小化系统层级，减少延迟和复杂度
- **分布式存储**：支持多节点部署，水平扩展
- **高效序列化**：采用列式存储格式（Parquet/Arrow），优化矩阵I/O性能
- **零拷贝访问**：支持内存映射和零拷贝数据访问

## 存储的数据类型

系统主要存储以下矩阵数据：

- **prompt**: 输入提示矩阵 `[batch_size, seq_len, hidden_dim]`
- **resp**: 响应矩阵 `[batch_size, seq_len, hidden_dim]`
- **Ref Log Prob**: 参考模型对数概率矩阵 `[batch_size, seq_len]`
- **advantage**: 优势值矩阵 `[batch_size, seq_len]`
- **其他训练中间数据**: 根据需求扩展

## 技术选型

### 开发语言
- **Rust** (推荐) - 极致性能、内存安全、零成本抽象
- **Go** (备选) - 高性能、并发友好、开发效率高

### 存储引擎
- **本地NVMe SSD** (主存储) - 极致性能，延迟<100μs，吞吐3GB/s+
- **Arrow IPC格式** - 零拷贝存储，原生支持Arrow数据
- **S3/OSS** (备份存储) - 异步备份，数据安全
- **混合存储策略** - 热数据本地SSD，冷数据S3归档

### 网络协议
- **RDMA** (生产环境) - 远程直接内存访问，极致性能，零拷贝
- **TCP/gRPC** (开发环境) - 传统TCP协议，易于开发和调试
- **自研轻量协议** - 基于RDMA/TCP的高性能协议

### 元数据存储
- **etcd/Consul** - 分片路由和元数据管理
- **Redis** - 热点数据缓存和索引
- **本地索引文件** - 轻量级元数据存储

## 系统架构（极简单层架构）

```
┌─────────────────────────────────────────────────────────┐
│              Client SDK (训练任务)                       │
│  ┌──────────────────────────────────────────────┐      │
│  │  - 分片路由 (Shard Router) - 集成在SDK中       │      │
│  │  - 连接池管理 (Connection Pool)               │      │
│  │  - 协议选择 (RDMA/TCP)                        │      │
│  └──────────────────────────────────────────────┘      │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐            │
│  │ Producer │  │ Consumer │  │  Query   │            │
│  └──────────┘  └──────────┘  └──────────┘            │
└─────────────────────────────────────────────────────────┘
                          │
                          │ 直接连接 (RDMA/TCP)
                          │ 根据 index_id 路由到对应节点
                          ▼
┌─────────────────────────────────────────────────────────┐
│          Storage Nodes (存储节点)                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐            │
│  │  Node 1  │  │  Node 2  │  │  Node N  │            │
│  │ Shard 0  │  │ Shard 1  │  │ Shard N  │            │
│  └──────────┘  └──────────┘  └──────────┘            │
│       │              │              │                  │
│       └──────────────┼──────────────┘                  │
│                      │                                 │
│                      ▼                                 │
│          ┌──────────────────────┐                    │
│          │   Matrix Storage      │                    │
│          │  (Parquet/Arrow/HDF5) │                    │
│          └──────────────────────┘                    │
└─────────────────────────────────────────────────────────┘
```

**架构优势**：
- **零转发延迟**：客户端直接连接存储节点，无中间层
- **零数据拷贝**：大矩阵数据直接传输，无额外拷贝
- **RDMA支持**：生产环境使用RDMA，极致性能
- **TCP兼容**：开发环境使用TCP，易于调试

## 分片策略

### 基于 index_id 分片

```
shard_id = hash(index_id) % num_shards
```

**优势**：
- 数据均匀分布
- 支持范围查询优化
- 易于扩展和迁移

### 分片元数据

- **分片映射表**：index_id → shard_id → node_id
- **存储位置**：etcd/Consul 或本地索引文件
- **一致性哈希**：支持动态扩缩容

## 快速开始

### 环境要求

- Rust 1.70+ (推荐) 或 Go 1.21+
- etcd 3.5+ (元数据存储)
- 对象存储 (S3/OSS) 或本地存储

### 安装

```bash
# 克隆项目
git clone <repository-url>
cd transfer-queue

# Rust 版本
cargo build --release

# Go 版本
go build -o transfer-queue ./cmd/server
```

### 使用示例

```rust
// Rust 示例
use transfer_queue::{Client, Transport};

// 创建客户端，自动选择协议（生产环境RDMA，开发环境TCP）
let client = Client::builder()
    .etcd_endpoints(vec!["http://etcd:2379"])
    .transport(Transport::Auto) // 自动选择
    .build()
    .await?;

// 写入矩阵数据（SDK自动路由到对应节点）
let prompt: Matrix<f32> = ...; // [batch, seq_len, hidden]
let resp: Matrix<f32> = ...;
let ref_log_prob: Matrix<f32> = ...;
let advantage: Matrix<f32> = ...;

client.write_matrix(index_id, MatrixType::Prompt, &prompt).await?;
client.write_matrix(index_id, MatrixType::Resp, &resp).await?;
client.write_matrix(index_id, MatrixType::RefLogProb, &ref_log_prob).await?;
client.write_matrix(index_id, MatrixType::Advantage, &advantage).await?;

// 读取矩阵数据（SDK自动路由到对应节点）
let prompt = client.read_matrix(index_id, MatrixType::Prompt).await?;
let resp = client.read_matrix(index_id, MatrixType::Resp).await?;
```

```go
// Go 示例
import "transfer-queue/client"

// 创建客户端，支持RDMA和TCP
config := client.Config{
    EtcdEndpoints: []string{"http://etcd:2379"},
    Transport:     client.TransportAuto, // 自动选择RDMA或TCP
}

client, err := client.New(config)
if err != nil {
    log.Fatal(err)
}

// 写入矩阵数据（SDK自动路由）
prompt := [][][]float32{...} // [batch, seq_len, hidden]
resp := [][][]float32{...}
refLogProb := [][]float32{...}
advantage := [][]float32{...}

err = client.WriteMatrix(indexID, client.Prompt, prompt)
err = client.WriteMatrix(indexID, client.Resp, resp)
err = client.WriteMatrix(indexID, client.RefLogProb, refLogProb)
err = client.WriteMatrix(indexID, client.Advantage, advantage)

// 读取矩阵数据（SDK自动路由）
prompt, err := client.ReadMatrix(indexID, client.Prompt)
resp, err := client.ReadMatrix(indexID, client.Resp)
```

## 目录结构

```
transfer-queue/
├── cmd/                 # 应用程序入口
│   └── server/         # 存储服务
├── src/                # Rust 源码 (或 internal/ for Go)
│   ├── storage/        # 存储引擎实现
│   │   ├── parquet.rs  # Parquet 存储
│   │   ├── arrow.rs    # Arrow 存储
│   │   └── hdf5.rs     # HDF5 存储
│   ├── shard/          # 分片管理
│   │   ├── router.rs   # 分片路由
│   │   └── manager.rs  # 分片管理器
│   ├── matrix/         # 矩阵操作
│   │   ├── serializer.rs # 序列化
│   │   └── accessor.rs   # 访问器
│   └── server/         # gRPC 服务
├── proto/              # gRPC 协议定义
├── docs/               # 文档
│   ├── ARCHITECTURE.md
│   ├── API.md
│   └── STORAGE.md
└── tests/              # 测试
```

## 性能目标

- **写入吞吐**：> 10GB/s (多节点)
- **读取延迟**：< 10ms (P99)
- **并发支持**：> 10K QPS
- **存储效率**：压缩率 > 50%

## 文档

- [系统架构设计](./docs/ARCHITECTURE.md) - 详细的架构设计和选型说明
- [API 接口文档](./docs/API.md) - gRPC API 文档
- [存储设计](./docs/STORAGE.md) - 存储格式和分片策略

## License

MIT License
