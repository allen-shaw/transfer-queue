# Transfer-Queue 存储设计文档

## 1. 存储概述

Transfer-Queue 采用**Arrow IPC格式**存储矩阵数据，基于**index_id进行分片**，使用**本地NVMe SSD作为主存储**，**异步备份到S3**，实现高性能的分布式矩阵存储。

### 1.1 存储架构

```
存储节点
├── 主存储：本地NVMe SSD
│   ├── 延迟：<100μs（vs S3的10-100ms）
│   ├── 吞吐：单盘3GB/s+，多盘RAID 10GB/s+
│   ├── IOPS：>100K IOPS
│   └── 格式：Arrow IPC（零拷贝）
├── 备份存储：S3/OSS（异步）
│   ├── 定期备份（每小时/每天）
│   ├── 冷数据归档
│   └── 灾难恢复
└── 缓存层：内存（可选）
    └── 热点数据缓存
```

## 2. 数据模型

### 2.1 存储的数据类型

| 数据类型 | 维度 | 数据类型 | 说明 |
|---------|------|---------|------|
| prompt | `[batch_size, seq_len, hidden_dim]` | float32 | 输入提示矩阵 |
| resp | `[batch_size, seq_len, hidden_dim]` | float32 | 响应矩阵 |
| ref_log_prob | `[batch_size, seq_len]` | float32 | 参考模型对数概率 |
| advantage | `[batch_size, seq_len]` | float32 | 优势值矩阵 |
| value | `[batch_size, seq_len]` | float32 | 价值函数值（可选） |
| reward | `[batch_size]` | float32 | 奖励值（可选） |

### 2.2 数据组织方式

```
本地NVMe SSD存储根目录/
├── shard_0/
│   ├── index_12345/
│   │   ├── prompt.arrow      # Arrow IPC格式
│   │   ├── resp.arrow
│   │   ├── ref_log_prob.arrow
│   │   ├── advantage.arrow
│   │   └── metadata.json
│   └── index_67890/
│       └── ...
├── shard_1/
│   └── ...
└── shard_N/
    └── ...

S3备份存储/
├── backups/
│   ├── 2024-01-24/
│   │   ├── shard_0/
│   │   └── ...
│   └── ...
└── archive/
    └── 冷数据归档
```

## 3. Arrow IPC 格式设计

### 3.1 Arrow IPC 优势

- **零拷贝**：内存格式与存储格式一致，无需序列化/反序列化
- **列式存储**：原生列式格式，适合矩阵数据
- **高效压缩**：支持Snappy/Zstd压缩
- **跨语言**：多语言支持（C++、Java、Python、Rust等）
- **流式传输**：支持流式读写，适合大矩阵

### 3.2 Arrow Schema 定义

```rust
// prompt/resp (3D矩阵)
let schema = Schema::new(vec![
    Field::new("batch_size", DataType::Int64, false),
    Field::new("seq_len", DataType::Int64, false),
    Field::new("hidden_dim", DataType::Int64, false),
    Field::new("data", DataType::FixedSizeList(
        Box::new(Field::new("item", DataType::Float32, false)),
        batch_size * seq_len * hidden_dim
    ), false),
]);

// ref_log_prob/advantage (2D矩阵)
let schema = Schema::new(vec![
    Field::new("batch_size", DataType::Int64, false),
    Field::new("seq_len", DataType::Int64, false),
    Field::new("data", DataType::FixedSizeList(
        Box::new(Field::new("item", DataType::Float32, false)),
        batch_size * seq_len
    ), false),
]);

// reward (1D向量)
let schema = Schema::new(vec![
    Field::new("length", DataType::Int64, false),
    Field::new("data", DataType::FixedSizeList(
        Box::new(Field::new("item", DataType::Float32, false)),
        length
    ), false),
]);
```

### 3.3 Arrow IPC 文件格式

```
Arrow IPC File Format
├── Magic Number (8 bytes)
├── Footer
│   ├── Schema
│   ├── Dictionaries
│   └── Record Batches
│       ├── Offset
│       ├── Metadata
│       └── Body
└── Footer Length (4 bytes)
```

**特点**：
- 自描述格式（包含Schema）
- 支持随机访问（通过Footer定位）
- 支持压缩（可选）
- 支持字典编码（可选）

## 4. 存储引擎实现

### 4.1 本地NVMe SSD存储

#### 4.1.1 存储路径

```
/mnt/nvme/transfer-queue/
├── shard_0/
│   └── index_12345/
│       ├── prompt.arrow
│       ├── resp.arrow
│       └── ...
└── shard_1/
    └── ...
```

#### 4.1.2 写入流程

```rust
async fn write_matrix(
    &self,
    index_id: u64,
    matrix_type: MatrixType,
    data: &Matrix<f32>,
) -> Result<()> {
    // 1. 创建Arrow RecordBatch
    let record_batch = self.create_record_batch(data)?;
    
    // 2. 写入Arrow IPC文件（零拷贝）
    let file_path = self.get_file_path(index_id, matrix_type);
    let mut writer = FileWriter::try_new(
        File::create(&file_path)?,
        &record_batch.schema()
    )?;
    
    writer.write(&record_batch)?;
    writer.finish()?;
    
    // 3. 异步触发备份到S3
    self.backup_to_s3(index_id, matrix_type, &file_path).await?;
    
    Ok(())
}
```

#### 4.1.3 读取流程

```rust
async fn read_matrix(
    &self,
    index_id: u64,
    matrix_type: MatrixType,
) -> Result<Matrix<f32>> {
    // 1. 读取Arrow IPC文件（零拷贝）
    let file_path = self.get_file_path(index_id, matrix_type);
    let file = File::open(&file_path)?;
    let reader = FileReader::try_new(file)?;
    
    // 2. 读取RecordBatch（零拷贝到内存）
    let record_batch = reader.next().await?.unwrap();
    
    // 3. 转换为Matrix（零拷贝）
    let matrix = self.record_batch_to_matrix(record_batch)?;
    
    Ok(matrix)
}
```

### 4.2 异步备份到S3

#### 4.2.1 备份策略

- **实时备份**：写入后异步备份（延迟<1分钟）
- **定期备份**：每小时全量备份
- **增量备份**：只备份变更的文件

#### 4.2.2 备份实现

```rust
async fn backup_to_s3(
    &self,
    index_id: u64,
    matrix_type: MatrixType,
    local_path: &Path,
) -> Result<()> {
    // 异步上传到S3
    let s3_path = format!("s3://bucket/backups/{}/{}", index_id, matrix_type);
    self.s3_client
        .put_object()
        .bucket("transfer-queue-backup")
        .key(&s3_path)
        .body(ByteStream::from_path(local_path).await?)
        .send()
        .await?;
    
    Ok(())
}
```

### 4.3 存储性能对比

| 存储方案 | 延迟 | 吞吐 | IOPS | 成本 |
|---------|------|------|------|------|
| 本地NVMe SSD | <100μs | 3GB/s+ | >100K | 中等 |
| S3/OSS | 10-100ms | 5GB/s | 低 | 低 |
| 分布式文件系统 | 1-10ms | 1-5GB/s | 中等 | 高 |

**推荐**：本地NVMe SSD作为主存储，S3作为备份存储。

## 5. 分片策略

### 4.1 分片算法

```rust
// 简单哈希分片（推荐）
fn get_shard_id(index_id: u64, num_shards: u32) -> u32 {
    (index_id % num_shards as u32) as u32
}

// 一致性哈希分片（支持动态扩缩容）
fn get_shard_id_consistent(index_id: u64, shard_ring: &[u32]) -> u32 {
    let hash = consistent_hash(index_id);
    // 在ring上查找第一个大于hash的shard
    shard_ring.binary_search(&hash).unwrap_or_else(|i| i)
}
```

### 4.2 分片元数据

存储在 etcd 中：

```
/transfer-queue/shard/{shard_id}
{
  "node_id": "node-1",
  "node_address": "192.168.1.100:8080",
  "status": "active",
  "capacity": 1000000,
  "used": 500000,
  "index_ids": ["12345", "67890", ...]
}
```

### 4.3 分片路由表

```
/transfer-queue/routing/{index_id}
{
  "shard_id": 0,
  "node_id": "node-1",
  "node_address": "192.168.1.100:8080"
}
```

## 5. 存储优化

### 5.1 列式存储优势

- **压缩率高**：列式存储压缩率通常比行式高2-10倍
- **按列访问**：适合按列访问的场景
- **向量化计算**：支持SIMD优化

### 5.2 压缩策略

- **Snappy**：快速压缩，适合实时写入
- **Zstd**：高压缩率，适合存储优化
- **LZ4**：平衡压缩率和速度

**推荐**：写入时使用Snappy，归档时使用Zstd。

### 5.3 索引设计

为每个index_id创建元数据索引：

```json
{
  "index_id": "12345",
  "shard_id": 0,
  "node_id": "node-1",
  "data_types": ["prompt", "resp", "ref_log_prob", "advantage"],
  "dimensions": {
    "prompt": [32, 512, 4096],
    "resp": [32, 512, 4096],
    "ref_log_prob": [32, 512],
    "advantage": [32, 512]
  },
  "file_sizes": {
    "prompt": 268435456,
    "resp": 268435456,
    "ref_log_prob": 65536,
    "advantage": 65536
  },
  "created_at": "2024-01-24T10:00:00Z",
  "updated_at": "2024-01-24T10:05:00Z"
}
```

## 6. 读写流程

### 6.1 写入流程（零拷贝）

```
1. Client SDK: WriteMatrix(index_id, type, matrix)
2. SDK: 计算 shard_id，查询路由，直接连接存储节点
3. Storage Node:
   a. 接收Arrow数据（通过Arrow Flight，零拷贝）
   b. 写入本地NVMe SSD（Arrow IPC格式，零拷贝）
   c. 更新元数据索引
   d. 异步触发备份到S3
   e. 返回成功
4. SDK: 返回结果给Client
```

**性能优化**：
- Arrow Flight零拷贝传输
- Arrow IPC零拷贝存储
- 异步备份，不阻塞写入

### 6.2 读取流程（零拷贝）

```
1. Client SDK: ReadMatrix(index_id, type)
2. SDK: 计算 shard_id，查询路由，直接连接存储节点
3. Storage Node:
   a. 从本地NVMe SSD读取Arrow IPC文件（零拷贝）
   b. 返回Arrow RecordBatch（通过Arrow Flight，零拷贝）
4. SDK: 返回Arrow数据给Client（零拷贝）
```

**性能优化**：
- 本地SSD低延迟（<100μs）
- Arrow IPC零拷贝
- Arrow Flight零拷贝传输

### 6.3 批量操作

支持批量读写，减少网络往返：

```rust
// 批量写入
client.batch_write(vec![
    (index_id1, "prompt", matrix1),
    (index_id2, "prompt", matrix2),
    ...
]).await?;

// 批量读取
let matrices = client.batch_read(vec![
    (index_id1, "prompt"),
    (index_id2, "prompt"),
    ...
]).await?;
```

## 7. 存储容量规划

### 7.1 单节点容量

假设：
- 单节点NVMe SSD：8TB
- 每个index_id存储4种数据类型
- 压缩率：50%

**单个index_id存储大小**（压缩后）：
- prompt: 268 MB
- resp: 268 MB
- ref_log_prob: 65 KB
- advantage: 65 KB
- **总计**: ~536 MB

**单节点容量**：
- 可存储约15,000个index_id
- 总数据量约8TB

### 7.2 多节点扩展

- **10个节点**：150,000个index_id，80TB
- **100个节点**：1,500,000个index_id，800TB

## 8. 存储后端

### 8.1 主存储：本地NVMe SSD（推荐）⭐

**配置建议**：
- **容量**：每节点4-8TB NVMe SSD
- **RAID**：RAID 0（性能）或RAID 10（可靠性）
- **文件系统**：XFS或ext4（支持大文件）
- **挂载选项**：`noatime,nodiratime`（减少元数据更新）

**性能指标**：
- 延迟：<100μs
- 吞吐：单盘3GB/s+，RAID 10GB/s+
- IOPS：>100K

### 8.2 备份存储：S3/OSS

**配置**：
- **存储类**：标准存储（热数据）或低频存储（冷数据）
- **备份频率**：每小时全量备份 + 实时增量备份
- **保留策略**：30天标准存储，之后转为归档存储

**成本优化**：
- 使用S3生命周期策略自动转换存储类
- 压缩后上传，节省存储成本
- 只备份变更文件，减少传输成本

### 8.3 混合存储策略

**数据分层**：
- **热数据**（最近7天）：本地NVMe SSD
- **温数据**（7-30天）：本地NVMe SSD
- **冷数据**（>30天）：S3标准存储
- **归档数据**（>90天）：S3归档存储

**自动迁移**：
- 根据访问时间自动迁移
- 根据访问频率自动迁移
- 支持手动迁移

## 8. 性能优化

### 8.1 写入优化

1. **批量写入**：收集多个请求批量写入
2. **异步刷盘**：写入后异步刷盘，不阻塞
3. **预分配空间**：预分配文件空间
4. **压缩写入**：写入时压缩

### 8.2 读取优化

1. **缓存热点**：热点数据缓存在内存
2. **预取**：预取相邻数据
3. **并行读取**：多线程并行读取
4. **零拷贝**：使用Arrow格式支持零拷贝

### 8.3 索引优化

1. **内存索引**：元数据索引常驻内存
2. **Bloom Filter**：快速判断数据是否存在
3. **范围索引**：支持范围查询优化

## 9. 数据一致性

### 9.1 写入一致性

- **最终一致性**：数据最终一致
- **幂等性**：支持重复写入
- **原子性**：单个index_id的写入是原子的

### 9.2 读取一致性

- **读已提交**：读取已提交的数据
- **快照读**：支持快照读（可选）

## 10. 数据迁移

### 10.1 分片迁移

当添加/删除节点时：

1. **计算迁移分片**：确定需要迁移的分片
2. **后台迁移**：后台异步迁移数据
3. **更新路由**：更新etcd中的分片映射
4. **切换流量**：逐步切换流量到新节点

### 10.2 数据备份

- **定期备份**：定期备份到对象存储
- **增量备份**：支持增量备份
- **快速恢复**：支持快速恢复

## 11. 容量规划

### 11.1 存储容量估算

假设：
- batch_size = 32
- seq_len = 512
- hidden_dim = 4096
- 每个index_id存储4种数据类型

**单个index_id存储大小**：
- prompt: 32 × 512 × 4096 × 4 = 268 MB
- resp: 32 × 512 × 4096 × 4 = 268 MB
- ref_log_prob: 32 × 512 × 4 = 65 KB
- advantage: 32 × 512 × 4 = 65 KB
- **总计（压缩前）**: ~536 MB
- **总计（压缩后，50%压缩率）**: ~268 MB

**100万个index_id**：
- 压缩前: ~536 TB
- 压缩后: ~268 TB

### 11.2 分片数量建议

- **小规模**（< 100万index_id）: 4-8个分片
- **中规模**（100万-1000万）: 16-32个分片
- **大规模**（> 1000万）: 64-128个分片

每个分片建议存储：
- 10万-100万个index_id
- 压缩后约27-270 GB
