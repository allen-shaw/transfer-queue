# Transfer-Queue 系统架构设计

## 1. 系统概述

Transfer-Queue 是一个高性能分布式矩阵存储系统，专为后训练工作流设计。系统采用极简的两层架构，基于index_id进行智能分片，高效存储和检索训练过程中的矩阵数据。

### 1.1 设计目标

1. **极致性能**：支持大规模矩阵数据的高吞吐、低延迟读写
2. **智能分片**：基于index_id自动分片，实现数据均匀分布
3. **极简架构**：最小化系统层级，减少延迟和复杂度
4. **水平扩展**：支持动态添加存储节点，线性扩展性能
5. **高效存储**：采用列式存储格式，优化矩阵I/O性能

### 1.2 核心数据模型

系统存储的矩阵数据：

| 数据类型 | 维度 | 说明 |
|---------|------|------|
| prompt | `[batch_size, seq_len, hidden_dim]` | 输入提示矩阵 |
| resp | `[batch_size, seq_len, hidden_dim]` | 响应矩阵 |
| ref_log_prob | `[batch_size, seq_len]` | 参考模型对数概率 |
| advantage | `[batch_size, seq_len]` | 优势值矩阵 |
| value | `[batch_size, seq_len]` | 价值函数值（可选） |
| reward | `[batch_size]` | 奖励值（可选） |

## 2. 技术选型

### 2.1 开发语言选型

#### 方案A：Rust (推荐) ⭐

**优势**：
- **极致性能**：零成本抽象，接近C++性能
- **内存安全**：编译期保证内存安全，避免运行时错误
- **并发优势**：基于所有权系统的并发模型，无数据竞争
- **生态完善**：Tokio异步运行时，高性能网络库
- **科学计算**：支持ndarray、arrow等高性能库

**适用场景**：
- 对性能要求极高的场景
- 需要长期稳定运行的服务
- 大规模并发处理

#### 方案B：Go (备选)

**优势**：
- **高性能**：GC优化良好，性能接近C++
- **并发友好**：goroutine轻量级并发
- **开发效率**：语法简洁，开发速度快
- **生态成熟**：丰富的第三方库

**适用场景**：
- 需要快速迭代开发
- 团队更熟悉Go语言
- 性能要求可接受Go的GC开销

**推荐选择Rust**，因为矩阵存储对性能要求极高，Rust的零成本抽象和内存安全特性更适合。

### 2.2 存储引擎选型

#### 方案A：Parquet + Arrow (推荐) ⭐

**优势**：
- **列式存储**：矩阵按列存储，适合按列访问
- **高效压缩**：Snappy/Zstd压缩，压缩率高
- **零拷贝**：Arrow内存格式支持零拷贝访问
- **生态成熟**：广泛使用的列式存储格式
- **跨语言**：支持多种语言绑定

**存储结构**：
```
index_id/
  ├── prompt.parquet      # [batch, seq_len, hidden_dim]
  ├── resp.parquet        # [batch, seq_len, hidden_dim]
  ├── ref_log_prob.parquet # [batch, seq_len]
  └── advantage.parquet   # [batch, seq_len]
```

#### 方案B：HDF5

**优势**：
- **科学计算标准**：广泛用于科学计算
- **支持大矩阵**：支持超大矩阵存储
- **丰富的元数据**：支持复杂的元数据

**劣势**：
- 性能相对Parquet较低
- 压缩率较低
- 跨语言支持不如Parquet

#### 方案C：自定义二进制格式

**优势**：
- **极致优化**：针对特定场景优化
- **最小开销**：无格式解析开销

**劣势**：
- 开发维护成本高
- 兼容性差
- 调试困难

**推荐选择Parquet + Arrow**，平衡了性能、压缩率和生态成熟度。

### 2.3 RPC框架选型

#### 方案A：Apache Arrow Flight (强烈推荐) ⭐⭐⭐

**优势**：
- **统一框架**：同时支持多种传输后端（gRPC/TCP、可扩展RDMA）
- **零拷贝**：专为Arrow数据设计，支持零拷贝传输
- **流式传输**：支持并行流式传输，适合大矩阵数据
- **生态成熟**：Apache项目，多语言支持（C++、Java、Python、Rust）
- **屏蔽网络细节**：统一的API，自动选择最佳传输方式

**架构**：
```
Arrow Flight RPC
├── Transport Abstraction Layer
│   ├── gRPC Transport (默认，TCP)
│   ├── RDMA Transport (可扩展)
│   └── 自动选择最佳传输
├── Arrow IPC Format
│   └── 零拷贝序列化
└── Flight Protocol
    ├── DoGet (下载数据流)
    ├── DoPut (上传数据流)
    └── DoAction (自定义操作)
```

**使用示例**：
```rust
// 客户端自动选择传输方式，无需关心网络细节
let client = FlightClient::builder()
    .location("grpc://node1:8080")  // TCP传输
    .location("rdma://node1:8081")  // RDMA传输（如果可用）
    .build()
    .await?;

// 统一的API，自动选择最佳传输
let stream = client.do_get(ticket).await?;
```

**RDMA扩展**：
- Arrow Flight支持自定义Transport实现
- 可以实现RDMA Transport，无缝集成
- 客户端自动fallback到TCP（如果RDMA不可用）

#### 方案B：自研统一Transport层

**优势**：
- 完全控制
- 可针对场景优化

**劣势**：
- 开发成本高
- 需要自己实现序列化、连接管理等
- 维护成本高

**推荐选择Apache Arrow Flight**，成熟稳定，支持扩展，完美匹配Arrow数据格式。

### 2.4 存储引擎选型

#### 方案A：本地NVMe SSD + 异步备份S3 (强烈推荐) ⭐⭐⭐

**架构**：
```
存储节点
├── 主存储：本地NVMe SSD
│   ├── 高性能读写（微秒级延迟）
│   ├── 高IOPS（>100K IOPS）
│   └── 低延迟（<100μs）
├── 备份存储：S3/OSS（异步）
│   ├── 定期备份（每小时/每天）
│   ├── 冷数据归档
│   └── 灾难恢复
└── 缓存层：内存（可选）
    └── 热点数据缓存
```

**优势**：
- **极致性能**：本地SSD延迟<100μs，远低于S3的毫秒级
- **高吞吐**：单盘可达3GB/s+，多盘RAID可达10GB/s+
- **成本可控**：SSD成本持续下降，容量足够
- **数据安全**：异步备份到S3，保证数据不丢失
- **灵活扩展**：可按需添加SSD容量

**存储策略**：
- **热数据**：本地NVMe SSD（最近7天）
- **温数据**：本地SSD（最近30天）
- **冷数据**：S3/OSS（归档）

#### 方案B：分布式文件系统（Ceph/GlusterFS）

**优势**：
- 统一存储池
- 自动副本
- 易于管理

**劣势**：
- 延迟高于本地SSD
- 网络开销
- 复杂度高

#### 方案C：Alluxio缓存层 + S3

**优势**：
- 自动缓存
- 透明访问

**劣势**：
- 额外组件
- 缓存命中率依赖访问模式

**推荐选择本地NVMe SSD + 异步备份S3**，性能最优，成本可控。

### 2.5 存储格式与引擎结合

**Arrow数据存储**：
```
本地NVMe SSD
├── index_12345/
│   ├── prompt.arrow      # Arrow IPC格式（零拷贝）
│   ├── resp.arrow
│   ├── ref_log_prob.arrow
│   └── advantage.arrow
└── index_67890/
    └── ...
```

**优势**：
- **Arrow IPC格式**：原生支持，零拷贝读写
- **列式存储**：适合矩阵数据
- **高效压缩**：支持Snappy/Zstd压缩
- **快速查询**：支持列式查询优化

### 2.4 元数据存储选型

#### 方案A：etcd (推荐) ⭐

**优势**：
- **分布式一致性**：基于Raft算法
- **高性能**：支持高并发读写
- **Watch机制**：支持实时监听变化
- **生态成熟**：Kubernetes等广泛使用

**存储内容**：
- 分片映射表：`index_id → shard_id → node_id`
- 节点信息：节点地址、状态、容量
- 配置信息：分片数量、副本数等

#### 方案B：Consul

**优势**：
- 服务发现集成
- 健康检查

**劣势**：
- 性能略低于etcd

#### 方案C：Redis

**优势**：
- 极高的读写性能
- 支持丰富的数据结构

**劣势**：
- 无强一致性保证
- 不适合存储关键元数据

**推荐选择etcd**，提供强一致性和高性能。

## 3. 系统架构

### 3.1 整体架构（极简单层，零转发）

```
┌─────────────────────────────────────────────────────────┐
│              Client SDK (客户端SDK)                      │
│  ┌──────────────────────────────────────────────┐      │
│  │  Shard Router (分片路由 - 集成在SDK)          │      │
│  │  - 根据 index_id 计算 shard_id               │      │
│  │  - 查询 etcd 获取 node_id (缓存)              │      │
│  │  - 直接连接对应存储节点                        │      │
│  └──────────────────────────────────────────────┘      │
│  ┌──────────────────────────────────────────────┐      │
│  │  Transport Layer (协议抽象层)                  │      │
│  │  - RDMA Transport (生产环境)                  │      │
│  │  - TCP/gRPC Transport (开发环境)               │      │
│  │  - 自动选择协议                                │      │
│  └──────────────────────────────────────────────┘      │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐            │
│  │ Producer │  │ Consumer │  │  Query  │            │
│  │ (写入)    │  │ (读取)    │  │ (查询)   │            │
│  └──────────┘  └──────────┘  └──────────┘            │
└─────────────────────────────────────────────────────────┘
                          │
                          │ 直接连接 (RDMA/TCP)
                          │ 零转发，零拷贝
                          ▼
┌─────────────────────────────────────────────────────────┐
│      Storage Node Layer (存储节点层)                     │
│  ┌──────────────────────────────────────────────┐      │
│  │  Network Layer                                │      │
│  │  - RDMA Server (生产环境)                     │      │
│  │  - TCP/gRPC Server (开发环境)                  │      │
│  └──────────────────────────────────────────────┘      │
│  ┌──────────────────────────────────────────────┐      │
│  │  Matrix Storage Engine                       │      │
│  │  - Parquet/Arrow 存储                         │      │
│  │  - 本地文件系统或对象存储                      │      │
│  │  - 索引管理                                   │      │
│  └──────────────────────────────────────────────┘      │
└─────────────────────────────────────────────────────────┘
```

**架构优势**：
- **零转发延迟**：客户端直接连接存储节点，无中间层
- **零数据拷贝**：大矩阵数据直接传输，无额外拷贝
- **RDMA支持**：生产环境使用RDMA，极致性能
- **TCP兼容**：开发环境使用TCP，易于调试

### 3.2 分片路由流程（SDK内完成）

```
Client SDK: WriteMatrix(index_id=12345, type="prompt", data)
    ↓
SDK: Shard Router
    ├─ 计算 shard_id = hash(12345) % num_shards
    ├─ 查询 etcd 获取 node_id (带缓存)
    └─ 选择连接池中的连接 (或创建新连接)
    ↓
SDK: Transport Layer
    ├─ 生产环境: RDMA Transport
    │   └─ 直接内存访问，零拷贝传输
    └─ 开发环境: TCP/gRPC Transport
        └─ 标准TCP传输
    ↓
直接连接到 Storage Node (node_id)
    ↓
Storage Node: 读取/写入矩阵数据
    ↓
直接返回结果给 Client SDK
```

**关键优化**：
- 路由逻辑在SDK中完成，无需中间服务
- 连接池复用连接，减少开销
- 路由信息缓存，减少etcd查询
- 支持连接预热和健康检查

### 3.3 存储节点架构

```
Storage Node
├── Network Layer
│   ├── RDMA Server (生产环境)
│   │   ├── InfiniBand/RoCE 支持
│   │   ├── 零拷贝接收
│   │   └── 异步处理
│   └── TCP/gRPC Server (开发环境)
│       ├── gRPC 服务
│       └── 标准TCP处理
├── Matrix Storage
│   ├── Primary Storage: Local NVMe SSD
│   │   ├── Arrow Files (按 index_id 组织)
│   │   │   ├── shard_0/
│   │   │   │   ├── index_12345/
│   │   │   │   │   ├── prompt.arrow      # Arrow IPC格式
│   │   │   │   │   ├── resp.arrow
│   │   │   │   │   ├── ref_log_prob.arrow
│   │   │   │   │   └── advantage.arrow
│   │   │   │   └── index_67890/
│   │   │   └── shard_1/
│   │   └── Index Files (元数据索引)
│   │       ├── index_12345.meta
│   │       └── index_67890.meta
│   ├── Backup Storage: S3/OSS (异步)
│   │   └── 定期备份的Arrow文件
│   └── Cache Layer (可选)
│       └── 内存缓存热点数据
├── Cache Layer (可选)
│   └── Redis (热点数据缓存)
└── Metadata
    └── Local Index (快速查询)
```

### 3.4 SDK架构设计

```
Client SDK
├── Router Module (分片路由)
│   ├── Shard Calculator (计算shard_id)
│   ├── Node Resolver (查询etcd获取node_id)
│   ├── Route Cache (路由缓存，TTL=1小时)
│   └── Load Balancer (负载均衡，多副本)
├── Transport Module (协议抽象)
│   ├── RDMA Transport
│   │   ├── InfiniBand/RoCE 客户端
│   │   ├── 零拷贝发送
│   │   └── 连接管理
│   ├── TCP Transport
│   │   ├── gRPC 客户端
│   │   ├── 连接池
│   │   └── 重试机制
│   └── Auto Selector (自动选择协议)
├── Connection Pool
│   ├── 连接复用
│   ├── 健康检查
│   └── 故障转移
└── Client API
    ├── write_matrix()
    ├── read_matrix()
    ├── batch_write()
    └── batch_read()
```

## 4. RDMA与TCP协议支持

### 4.1 协议选择策略

SDK支持自动选择协议，也可以手动指定：

```rust
// 自动选择（推荐）
let client = Client::builder()
    .etcd_endpoints(vec!["http://etcd:2379"])
    .transport(Transport::Auto) // 自动检测RDMA可用性
    .build()
    .await?;

// 强制使用RDMA
let client = Client::builder()
    .transport(Transport::RDMA)
    .build()
    .await?;

// 强制使用TCP（开发环境）
let client = Client::builder()
    .transport(Transport::TCP)
    .build()
    .await?;
```

### 4.2 RDMA实现细节

#### 4.2.1 RDMA优势

- **零拷贝**：数据直接从应用内存到网络，绕过内核
- **CPU零参与**：数据传输不占用CPU
- **低延迟**：微秒级延迟（vs TCP的毫秒级）
- **高带宽**：充分利用网络带宽

#### 4.2.2 RDMA实现

```rust
// RDMA Transport实现
struct RDMATransport {
    qp: QueuePair,  // RDMA队列对
    mr: MemoryRegion, // 内存区域
}

impl Transport for RDMATransport {
    async fn write_matrix(&self, req: WriteMatrixRequest) -> Result<WriteMatrixResponse> {
        // 1. 注册内存区域（如果未注册）
        let mr = self.register_memory(&req.data)?;
        
        // 2. 发送RDMA Write操作（零拷贝）
        self.qp.post_send(SendWr {
            opcode: Opcode::RDMA_WRITE,
            sge: ScatterGatherElement {
                addr: mr.addr,
                length: req.data.len(),
                lkey: mr.lkey,
            },
            // ...
        })?;
        
        // 3. 等待完成
        self.qp.poll_completion()?;
        
        Ok(WriteMatrixResponse { success: true })
    }
}
```

#### 4.2.3 RDMA部署要求

- **硬件**：支持RDMA的网卡（InfiniBand或RoCE）
- **驱动**：安装RDMA驱动（libibverbs, librdmacm）
- **网络**：配置RDMA网络（InfiniBand交换机或支持RoCE的以太网交换机）

### 4.3 TCP实现细节

#### 4.3.1 TCP实现

```rust
// TCP Transport实现
struct TCPTransport {
    client: GrpcClient,
    connection_pool: ConnectionPool,
}

impl Transport for TCPTransport {
    async fn write_matrix(&self, req: WriteMatrixRequest) -> Result<WriteMatrixResponse> {
        // 使用gRPC客户端发送
        let mut client = self.connection_pool.get().await?;
        client.write_matrix(req).await
    }
}
```

#### 4.3.2 TCP优化

- **连接池**：复用连接，减少握手开销
- **压缩**：gRPC支持压缩传输
- **流式传输**：大矩阵使用流式传输

### 4.4 协议切换

SDK支持运行时协议切换：

```rust
// 检测RDMA可用性
fn detect_rdma_available() -> bool {
    // 检查RDMA设备
    // 检查驱动是否加载
    // 检查网络配置
}

// 自动选择协议
match transport {
    Transport::Auto => {
        if detect_rdma_available() {
            RDMATransport::new()
        } else {
            TCPTransport::new()
        }
    }
    Transport::RDMA => RDMATransport::new(),
    Transport::TCP => TCPTransport::new(),
}
```

## 5. SDK路由实现

### 5.1 路由缓存

SDK维护路由缓存，减少etcd查询：

```rust
struct RouteCache {
    cache: Arc<RwLock<HashMap<u64, CachedRoute>>>,
    etcd_client: EtcdClient,
}

struct CachedRoute {
    shard_id: u32,
    node_id: String,
    node_address: String,
    cached_at: Instant,
    ttl: Duration,
}

impl RouteCache {
    async fn get_route(&self, index_id: u64) -> Result<Route> {
        // 1. 检查缓存
        if let Some(cached) = self.cache.read().await.get(&index_id) {
            if cached.cached_at.elapsed() < cached.ttl {
                return Ok(Route {
                    shard_id: cached.shard_id,
                    node_id: cached.node_id.clone(),
                    node_address: cached.node_address.clone(),
                });
            }
        }
        
        // 2. 计算shard_id
        let shard_id = hash(index_id) % num_shards;
        
        // 3. 查询etcd
        let shard_info = self.etcd_client
            .get(format!("/transfer-queue/shards/{}", shard_id))
            .await?;
        
        // 4. 更新缓存
        let route = Route {
            shard_id,
            node_id: shard_info.node_id,
            node_address: shard_info.node_address,
        };
        
        self.cache.write().await.insert(index_id, CachedRoute {
            shard_id: route.shard_id,
            node_id: route.node_id.clone(),
            node_address: route.node_address.clone(),
            cached_at: Instant::now(),
            ttl: Duration::from_secs(3600), // 1小时
        });
        
        Ok(route)
    }
}
```

### 5.2 连接池管理

SDK维护到每个节点的连接池：

```rust
struct ConnectionPool {
    pools: Arc<RwLock<HashMap<String, Vec<Connection>>>>,
    max_connections_per_node: usize,
}

impl ConnectionPool {
    async fn get_connection(&self, node_address: &str) -> Result<Connection> {
        let mut pools = self.pools.write().await;
        let pool = pools.entry(node_address.to_string())
            .or_insert_with(Vec::new);
        
        // 查找可用连接
        if let Some(conn) = pool.iter().find(|c| c.is_idle()) {
            return Ok(conn.clone());
        }
        
        // 创建新连接
        if pool.len() < self.max_connections_per_node {
            let conn = Connection::new(node_address).await?;
            pool.push(conn.clone());
            return Ok(conn);
        }
        
        // 等待连接可用
        // ...
    }
}
```

### 5.3 故障转移

SDK支持自动故障转移：

```rust
async fn write_matrix_with_retry(
    &self,
    index_id: u64,
    req: WriteMatrixRequest,
) -> Result<WriteMatrixResponse> {
    let mut retries = 3;
    
    loop {
        match self.write_matrix(index_id, req.clone()).await {
            Ok(resp) => return Ok(resp),
            Err(e) if retries > 0 => {
                // 清除缓存，重新查询路由
                self.route_cache.invalidate(index_id).await;
                retries -= 1;
                tokio::time::sleep(Duration::from_millis(100)).await;
            }
            Err(e) => return Err(e),
        }
    }
}
```

## 6. 分片策略

### 4.1 分片算法

```rust
// 一致性哈希分片
fn get_shard_id(index_id: u64, num_shards: u32) -> u32 {
    // 使用一致性哈希，支持动态扩缩容
    consistent_hash(index_id, num_shards)
}

// 简单哈希分片（推荐，更简单高效）
fn get_shard_id(index_id: u64, num_shards: u32) -> u32 {
    (index_id % num_shards as u64) as u32
}
```

### 4.2 分片元数据

存储在 etcd 中：

```
/transfer-queue/shard/{shard_id}
  - node_id: "node-1"
  - node_address: "192.168.1.100:8080"
  - status: "active"
  - capacity: 1000000
  - used: 500000
```

### 4.3 分片迁移

当添加/删除节点时：

1. **添加节点**：
   - 计算需要迁移的分片
   - 后台异步迁移数据
   - 更新etcd中的分片映射

2. **删除节点**：
   - 标记节点为draining
   - 迁移数据到其他节点
   - 更新分片映射

## 7. 存储格式设计

### 5.1 Parquet Schema

```protobuf
// prompt.parquet
message PromptMatrix {
  required int64 batch_size;
  required int64 seq_len;
  required int64 hidden_dim;
  required binary data;  // 序列化的矩阵数据
  optional int64 timestamp;
}

// ref_log_prob.parquet (2D矩阵)
message RefLogProbMatrix {
  required int64 batch_size;
  required int64 seq_len;
  required binary data;  // float32数组
  optional int64 timestamp;
}
```

### 5.2 存储优化

1. **列式存储**：矩阵按列存储，支持按列访问
2. **压缩**：使用Snappy或Zstd压缩
3. **索引**：为每个index_id创建元数据索引
4. **批量写入**：支持批量写入，减少I/O次数

## 8. 性能优化

### 6.1 写入优化

- **批量写入**：收集多个请求批量写入
- **异步写入**：异步刷盘，不阻塞请求
- **预分配空间**：预分配文件空间，减少碎片
- **压缩**：写入时压缩，减少存储空间

### 6.2 读取优化

- **缓存**：热点数据缓存在内存
- **预取**：预取相邻数据
- **零拷贝**：使用Arrow格式，支持零拷贝
- **并行读取**：多线程并行读取不同分片

### 6.3 网络优化

- **连接池**：复用gRPC连接
- **压缩传输**：gRPC支持压缩传输
- **流式传输**：大矩阵使用流式传输

## 7. 高可用设计

### 7.1 数据副本

- **副本策略**：每个分片存储N个副本（N=3）
- **副本分布**：副本分布在不同节点
- **一致性**：使用etcd保证元数据一致性

### 7.2 故障恢复

- **节点故障**：自动切换到副本节点
- **数据恢复**：从副本恢复数据
- **服务降级**：部分节点故障时降级服务

## 8. 监控与运维

### 8.1 监控指标

- **性能指标**：QPS、延迟、吞吐量
- **存储指标**：容量、使用率、IOPS
- **系统指标**：CPU、内存、网络

### 8.2 告警规则

- 节点故障告警
- 存储容量告警
- 性能下降告警

## 9. 扩展性设计

### 9.1 水平扩展

- **动态添加节点**：支持在线添加存储节点
- **自动负载均衡**：新节点自动分担负载
- **分片重平衡**：自动重新分配分片

### 9.2 垂直扩展

- **单节点性能**：优化单节点性能
- **存储容量**：支持大容量存储

## 10. 开发计划

### Phase 1: 核心功能
- [ ] 基础存储引擎（Parquet）
- [ ] 分片路由
- [ ] gRPC服务
- [ ] 基本读写接口

### Phase 2: 性能优化
- [ ] 批量写入
- [ ] 缓存层
- [ ] 压缩优化

### Phase 3: 高可用
- [ ] 数据副本
- [ ] 故障恢复
- [ ] 监控告警
