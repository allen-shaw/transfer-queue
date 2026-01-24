# Transfer-Queue API 接口文档

## 基础信息

- **RPC框架**: Apache Arrow Flight
- **传输协议**: RDMA (生产环境) / TCP/gRPC (开发环境)
- **数据格式**: Arrow IPC Format
- **默认端口**: 
  - RDMA: 8081
  - TCP/gRPC: 8080
- **压缩**: 支持Snappy/Zstd压缩（可选）

## Arrow Flight 优势

- **统一框架**：同时支持RDMA和TCP，自动选择最佳传输
- **零拷贝**：Arrow数据零拷贝传输和存储
- **流式传输**：支持并行流式传输，适合大矩阵
- **屏蔽网络细节**：客户端无需关心底层传输协议

## Arrow Flight API 定义

### Flight Descriptor

Arrow Flight使用FlightDescriptor来标识数据流：

```protobuf
message FlightDescriptor {
  oneof descriptor {
    string path = 1;           // 路径标识
    Command cmd = 2;          // 命令
  }
}

// 自定义命令
message Command {
  string command = 1;          // "write_matrix", "read_matrix", etc.
  bytes body = 2;              // JSON序列化的参数
}
```

### 核心Flight方法

Arrow Flight提供以下核心方法：

1. **DoGet**: 下载数据流（读取矩阵）
2. **DoPut**: 上传数据流（写入矩阵）
3. **DoAction**: 执行自定义操作（删除、查询元数据等）
4. **ListFlights**: 列出可用数据流
5. **GetFlightInfo**: 获取数据流信息

### 自定义Action定义

```protobuf
// 写入矩阵Action
message WriteMatrixAction {
  uint64 index_id = 1;
  string matrix_type = 2;  // "prompt", "resp", "ref_log_prob", "advantage"
}

// 读取矩阵Action
message ReadMatrixAction {
  uint64 index_id = 1;
  string matrix_type = 2;
}

// 删除矩阵Action
message DeleteMatrixAction {
  uint64 index_id = 1;
  string matrix_type = 2;  // 如果为空，删除所有类型
}

// 查询元数据Action
message GetMetadataAction {
  uint64 index_id = 1;
}
```

## API 接口说明

### 1. DoPut - 写入矩阵（零拷贝）

使用Arrow Flight的DoPut方法上传矩阵数据流。

**流程**:
```
1. Client创建Arrow RecordBatch
2. 调用DoPut，传入FlightDescriptor和RecordBatch流
3. Server接收RecordBatch（零拷贝）
4. Server写入本地NVMe SSD（Arrow IPC格式，零拷贝）
5. 返回写入结果
```

**示例**:
```rust
// 创建Arrow RecordBatch
let schema = Schema::new(vec![
    Field::new("data", DataType::FixedSizeList(
        Box::new(Field::new("item", DataType::Float32, false)),
        batch_size * seq_len * hidden_dim
    ), false),
]);

let record_batch = RecordBatch::try_new(
    schema,
    vec![Arc::new(Float32Array::from(data))],
)?;

// 创建FlightDescriptor
let descriptor = FlightDescriptor::new_cmd(
    serde_json::to_string(&WriteMatrixAction {
        index_id: 12345,
        matrix_type: "prompt".to_string(),
    })?
);

// 调用DoPut（零拷贝）
let mut writer = client.do_put(descriptor, schema).await?;
writer.write(&record_batch).await?;
writer.close().await?;
```

**优势**:
- **零拷贝**：Arrow数据直接传输，无需序列化
- **流式传输**：支持大矩阵流式传输
- **自动选择传输**：自动选择RDMA或TCP

### 2. DoGet - 读取矩阵（零拷贝）

使用Arrow Flight的DoGet方法下载矩阵数据流。

**流程**:
```
1. Client调用DoGet，传入FlightDescriptor
2. Server从本地NVMe SSD读取Arrow IPC文件（零拷贝）
3. Server返回Arrow RecordBatch流（零拷贝）
4. Client接收RecordBatch（零拷贝）
```

**示例**:
```rust
// 创建FlightDescriptor
let descriptor = FlightDescriptor::new_cmd(
    serde_json::to_string(&ReadMatrixAction {
        index_id: 12345,
        matrix_type: "prompt".to_string(),
    })?
);

// 调用DoGet（零拷贝）
let mut stream = client.do_get(descriptor).await?;
while let Some(record_batch) = stream.next().await? {
    // 直接使用Arrow RecordBatch，零拷贝
    let data = record_batch.column(0)
        .as_any()
        .downcast_ref::<Float32Array>()
        .unwrap();
    // 处理数据...
}
```

**优势**:
- **零拷贝**：Arrow数据直接传输，无需序列化
- **低延迟**：本地SSD延迟<100μs
- **流式传输**：支持大矩阵流式传输

### 3. DoAction - 删除矩阵

使用Arrow Flight的DoAction方法执行删除操作。

**示例**:
```rust
let action = Action {
    r#type: "DeleteMatrix".to_string(),
    body: serde_json::to_vec(&DeleteMatrixAction {
        index_id: 12345,
        matrix_type: "prompt".to_string(),
    })?,
};

let results = client.do_action(action).await?;
for result in results {
    // 处理结果...
}
```

### 4. DoAction - 查询元数据

**示例**:
```rust
let action = Action {
    r#type: "GetMetadata".to_string(),
    body: serde_json::to_vec(&GetMetadataAction {
        index_id: 12345,
    })?,
};

let results = client.do_action(action).await?;
// 解析元数据...
```

### 5. 批量操作

Arrow Flight支持并行流式传输，可以实现高效的批量操作：

```rust
// 并行读取多个矩阵
let futures: Vec<_> = index_ids.iter().map(|index_id| {
    let descriptor = FlightDescriptor::new_cmd(...);
    client.do_get(descriptor)
}).collect();

let results = futures::future::join_all(futures).await;
```

批量写入多个矩阵数据。

**请求**:
```protobuf
BatchWriteRequest {
  items: [
    {
      index_id: 12345
      type: PROMPT
      shape: [32, 512, 4096]
      data: <data1>
    },
    {
      index_id: 12345
      type: RESP
      shape: [32, 512, 4096]
      data: <data2>
    },
    {
      index_id: 67890
      type: PROMPT
      shape: [32, 512, 4096]
      data: <data3>
    }
  ]
}
```

**响应**:
```protobuf
BatchWriteResponse {
  results: [
    {
      index_id: 12345
      type: PROMPT
      success: true
      message: "success"
    },
    {
      index_id: 12345
      type: RESP
      success: true
      message: "success"
    },
    {
      index_id: 67890
      type: PROMPT
      success: true
      message: "success"
    }
  ]
}
```

**说明**:
- 批量写入可以提高吞吐量
- 每个item独立处理，部分失败不影响其他
- 建议批量大小：10-100个

### 4. BatchRead - 批量读取

批量读取多个矩阵数据。

**请求**:
```protobuf
BatchReadRequest {
  items: [
    {
      index_id: 12345
      type: PROMPT
    },
    {
      index_id: 12345
      type: RESP
    },
    {
      index_id: 67890
      type: PROMPT
    }
  ]
}
```

**响应**:
```protobuf
BatchReadResponse {
  results: [
    {
      index_id: 12345
      type: PROMPT
      success: true
      message: "success"
      matrix: { ... }
    },
    {
      index_id: 12345
      type: RESP
      success: true
      message: "success"
      matrix: { ... }
    },
    {
      index_id: 67890
      type: PROMPT
      success: false
      message: "not found"
    }
  ]
}
```

### 5. DeleteMatrix - 删除矩阵

删除指定的矩阵数据。

**请求**:
```protobuf
DeleteMatrixRequest {
  index_id: 12345
  type: PROMPT  // 如果为空，删除该index_id的所有数据
}
```

**响应**:
```protobuf
DeleteMatrixResponse {
  success: true
  message: "success"
}
```

### 6. GetMetadata - 查询元数据

查询指定index_id的元数据信息。

**请求**:
```protobuf
GetMetadataRequest {
  index_id: 12345
}
```

**响应**:
```protobuf
GetMetadataResponse {
  success: true
  message: "success"
  types: [PROMPT, RESP, REF_LOG_PROB, ADVANTAGE]
  dimensions: {
    "PROMPT": [32, 512, 4096]
    "RESP": [32, 512, 4096]
    "REF_LOG_PROB": [32, 512]
    "ADVANTAGE": [32, 512]
  }
  created_at: 1642406400000
  updated_at: 1642406500000
}
```

## 客户端使用示例

### Rust 示例（Arrow Flight）

```rust
use arrow_flight::FlightClient;
use arrow_flight::FlightDescriptor;
use arrow::record_batch::RecordBatch;
use arrow::array::Float32Array;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // 创建Flight客户端（自动选择RDMA或TCP）
    let client = FlightClient::builder()
        .location("grpc://localhost:8080")  // TCP
        .location("rdma://localhost:8081")  // RDMA（如果可用）
        .build()
        .await?;
    
    // 写入矩阵（零拷贝）
    let prompt_data: Vec<f32> = vec![...]; // [32 * 512 * 4096]
    let schema = create_schema(32, 512, 4096);
    let record_batch = RecordBatch::try_new(
        schema.clone(),
        vec![Arc::new(Float32Array::from(prompt_data))],
    )?;
    
    let descriptor = FlightDescriptor::new_cmd(
        serde_json::to_string(&WriteMatrixAction {
            index_id: 12345,
            matrix_type: "prompt".to_string(),
        })?
    );
    
    let mut writer = client.do_put(descriptor, schema).await?;
    writer.write(&record_batch).await?;
    writer.close().await?;
    
    // 读取矩阵（零拷贝）
    let descriptor = FlightDescriptor::new_cmd(
        serde_json::to_string(&ReadMatrixAction {
            index_id: 12345,
            matrix_type: "prompt".to_string(),
        })?
    );
    
    let mut stream = client.do_get(descriptor).await?;
    while let Some(record_batch) = stream.next().await? {
        // 直接使用Arrow数据，零拷贝
        let data = record_batch.column(0)
            .as_any()
            .downcast_ref::<Float32Array>()
            .unwrap();
        println!("Read matrix: {} elements", data.len());
    }
    
    Ok(())
}
```

### Python 示例（Arrow Flight）

```python
import pyarrow.flight as flight
import pyarrow as pa
import numpy as np

# 创建Flight客户端（自动选择传输）
client = flight.FlightClient("grpc://localhost:8080")

# 写入矩阵（零拷贝）
prompt = np.random.randn(32, 512, 4096).astype(np.float32)
schema = pa.schema([pa.field("data", pa.list_(pa.float32()))])
table = pa.Table.from_arrays([pa.array([prompt.flatten()])], schema=schema)

descriptor = flight.FlightDescriptor.for_command(
    json.dumps({"action": "WriteMatrix", "index_id": 12345, "matrix_type": "prompt"})
)

writer, _ = client.do_put(descriptor, schema)
writer.write_table(table)
writer.close()

# 读取矩阵（零拷贝）
descriptor = flight.FlightDescriptor.for_command(
    json.dumps({"action": "ReadMatrix", "index_id": 12345, "matrix_type": "prompt"})
)

flight_info = client.get_flight_info(descriptor)
reader = client.do_get(flight_info.endpoints[0].ticket)
table = reader.read_all()
data = table.to_pandas()  # 零拷贝转换为pandas
```

## 协议自动选择

Arrow Flight客户端支持自动选择最佳传输协议：

```rust
// 自动检测并选择
let client = FlightClient::builder()
    .location("grpc://node1:8080")   // TCP备选
    .location("rdma://node1:8081")    // RDMA首选
    .auto_select_transport(true)      // 自动选择
    .build()
    .await?;

// 客户端会自动：
// 1. 尝试连接RDMA端点
// 2. 如果失败，fallback到TCP
// 3. 对用户完全透明
```

**优势**：
- **开发环境**：自动使用TCP，易于调试
- **生产环境**：自动使用RDMA，极致性能
- **无需修改代码**：同一套代码，自动适配

## 错误处理

### 错误码

| 错误码 | 说明 |
|--------|------|
| OK | 成功 |
| NOT_FOUND | 数据不存在 |
| INVALID_ARGUMENT | 参数错误 |
| INTERNAL_ERROR | 内部错误 |
| UNAVAILABLE | 服务不可用 |

### 错误响应示例

```protobuf
ReadMatrixResponse {
  success: false
  message: "matrix not found: index_id=12345, type=PROMPT"
}
```

## 性能建议

1. **零拷贝优势**：Arrow Flight零拷贝传输，充分利用
2. **流式传输**：大矩阵使用流式传输，减少内存占用
3. **并行传输**：Arrow Flight支持并行流式传输
4. **连接复用**：复用Flight连接，避免频繁创建
5. **自动选择协议**：让系统自动选择RDMA或TCP
6. **批量操作**：使用并行DoGet/DoPut提高吞吐量

## 限流与配额

- **QPS限制**：每个客户端默认10000 QPS（RDMA）或 1000 QPS（TCP）
- **并发流限制**：单个客户端最多100个并发流
- **数据大小限制**：单个矩阵最大10GB
- **连接数限制**：每个节点最多1000个并发连接
