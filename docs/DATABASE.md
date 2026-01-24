# Transfer-Queue 元数据存储设计

## 1. 存储架构概述

Transfer-Queue 采用**分层存储架构**：
- **元数据存储**：etcd（分片路由、节点信息）
- **实际数据存储**：Parquet文件（矩阵数据）
- **缓存层**：Redis（可选，热点数据缓存）

## 2. etcd 元数据存储

### 2.1 存储结构

etcd 中存储的元数据：

```
/transfer-queue/
├── shards/
│   ├── {shard_id}          # 分片信息
│   └── ...
├── nodes/
│   ├── {node_id}           # 节点信息
│   └── ...
├── routing/
│   ├── {index_id}          # 路由信息（可选，用于缓存）
│   └── ...
└── config/                 # 配置信息
```

### 2.2 分片信息 (shards/{shard_id})

```json
{
  "shard_id": 0,
  "node_id": "node-1",
  "node_address": "192.168.1.100:8080",
  "status": "active",
  "capacity": 1000000,
  "used": 500000,
  "index_count": 50000,
  "created_at": "2024-01-24T10:00:00Z",
  "updated_at": "2024-01-24T10:05:00Z"
}
```

**字段说明**：
- `shard_id`: 分片ID
- `node_id`: 负责该分片的节点ID
- `node_address`: 节点地址
- `status`: 状态（active, draining, inactive）
- `capacity`: 容量（index_id数量）
- `used`: 已使用数量
- `index_count`: 当前存储的index_id数量

### 2.3 节点信息 (nodes/{node_id})

```json
{
  "node_id": "node-1",
  "address": "192.168.1.100:8080",
  "status": "active",
  "shards": [0, 1, 2],
  "capacity": {
    "storage_gb": 1000,
    "used_gb": 500,
    "index_capacity": 1000000,
    "index_used": 500000
  },
  "performance": {
    "qps": 10000,
    "latency_p50": 5,
    "latency_p99": 20
  },
  "last_heartbeat": "2024-01-24T10:05:00Z"
}
```

**字段说明**：
- `node_id`: 节点唯一标识
- `address`: 节点地址
- `status`: 节点状态
- `shards`: 该节点负责的分片列表
- `capacity`: 容量信息
- `performance`: 性能指标
- `last_heartbeat`: 最后心跳时间

### 2.4 路由信息 (routing/{index_id}) - 可选缓存

```json
{
  "index_id": "12345",
  "shard_id": 0,
  "node_id": "node-1",
  "node_address": "192.168.1.100:8080",
  "cached_at": "2024-01-24T10:00:00Z"
}
```

**说明**：
- 这是可选的缓存，用于加速路由查询
- TTL: 1小时
- 如果缓存不存在，通过计算shard_id再查询shards表

### 2.5 配置信息 (config/)

```json
{
  "num_shards": 32,
  "replication_factor": 3,
  "shard_algorithm": "hash",
  "compression": "snappy",
  "batch_size": 100
}
```

## 3. 本地索引文件

每个存储节点维护本地索引文件，用于快速查询。

### 3.1 索引文件结构

```
{storage_root}/.index/
├── shard_0.index
├── shard_1.index
└── ...
```

### 3.2 索引文件格式（二进制）

```
[Header]
  - magic: 4 bytes ("TQIX")
  - version: 4 bytes
  - num_entries: 8 bytes
  - created_at: 8 bytes
  - updated_at: 8 bytes

[Entries]
  - index_id: 8 bytes
  - offset: 8 bytes (在数据文件中的偏移)
  - size: 8 bytes (数据大小)
  - types: 4 bytes (位掩码，表示存储的数据类型)
  - timestamp: 8 bytes
```

### 3.3 索引操作

- **写入时**：更新索引文件
- **读取时**：查询索引文件定位数据
- **定期重建**：定期重建索引，保证一致性

## 4. 元数据操作

### 4.1 分片路由查询

```rust
// 1. 计算shard_id
let shard_id = hash(index_id) % num_shards;

// 2. 查询etcd获取节点信息
let shard_info = etcd.get(format!("/transfer-queue/shards/{}", shard_id)).await?;

// 3. 获取节点地址
let node_address = shard_info.node_address;
```

### 4.2 节点注册

```rust
// 节点启动时注册
let node_info = NodeInfo {
    node_id: "node-1",
    address: "192.168.1.100:8080",
    status: "active",
    shards: vec![0, 1, 2],
    // ...
};

etcd.put(
    format!("/transfer-queue/nodes/{}", node_id),
    serde_json::to_string(&node_info)?
).await?;
```

### 4.3 心跳更新

```rust
// 定期更新心跳
loop {
    etcd.put(
        format!("/transfer-queue/nodes/{}/last_heartbeat", node_id),
        current_timestamp()
    ).await?;
    
    tokio::time::sleep(Duration::from_secs(10)).await;
}
```

## 5. 数据一致性

### 5.1 etcd 一致性

- **强一致性**：etcd基于Raft算法，保证强一致性
- **读写一致性**：所有节点看到相同的数据
- **故障恢复**：自动故障恢复，保证可用性

### 5.2 元数据与数据一致性

- **最终一致性**：元数据更新后，数据最终一致
- **写入流程**：
  1. 写入数据文件
  2. 更新本地索引
  3. 更新etcd元数据（异步）

## 6. 性能优化

### 6.1 路由缓存

- **本地缓存**：在服务层缓存路由信息
- **TTL**：缓存TTL为1小时
- **失效策略**：节点变更时清除缓存

### 6.2 批量查询

- **批量获取**：使用etcd的批量查询API
- **Watch机制**：使用Watch监听变化，减少查询

### 6.3 索引优化

- **内存索引**：热点索引常驻内存
- **Bloom Filter**：快速判断index_id是否存在
- **范围索引**：支持范围查询优化

## 7. 故障处理

### 7.1 节点故障

1. **检测**：通过心跳检测节点故障
2. **标记**：将节点标记为inactive
3. **迁移**：将分片迁移到其他节点
4. **恢复**：节点恢复后重新分配分片

### 7.2 分片迁移

1. **计算迁移**：确定需要迁移的分片
2. **数据迁移**：后台异步迁移数据
3. **更新路由**：更新etcd中的分片映射
4. **切换流量**：逐步切换流量

## 8. 监控指标

### 8.1 etcd 指标

- **QPS**：etcd的读写QPS
- **延迟**：etcd操作的延迟
- **存储大小**：etcd存储的数据大小

### 8.2 元数据指标

- **路由缓存命中率**：路由缓存的命中率
- **索引查询延迟**：索引查询的延迟
- **分片分布**：各分片的数据分布

## 9. 备份与恢复

### 9.1 etcd 备份

- **定期备份**：定期备份etcd数据
- **快照备份**：使用etcd的快照功能
- **异地备份**：备份到异地存储

### 9.2 恢复流程

1. **恢复etcd**：从备份恢复etcd数据
2. **重建索引**：重建本地索引文件
3. **验证数据**：验证数据一致性

## 10. 容量规划

### 10.1 etcd 容量

假设：
- 每个分片信息：1 KB
- 每个节点信息：2 KB
- 每个路由缓存：200 bytes

**1000个分片，100个节点，1000万路由缓存**：
- 分片信息：1000 × 1 KB = 1 MB
- 节点信息：100 × 2 KB = 200 KB
- 路由缓存：10M × 200 bytes = 2 GB（可选）

**总计**：约2-3 GB（不含路由缓存）或 5 GB（含路由缓存）

### 10.2 性能建议

- **etcd集群**：3-5个节点
- **存储**：SSD，至少100 GB
- **网络**：低延迟网络（< 1ms）
