# TransferQueue Client Examples

This directory contains example applications demonstrating how to use the TransferQueue client SDK.

## Examples

### 1. Write Example (`write_example`)
Demonstrates how to write trajectories to the TransferQueue server.

**Usage:**
```bash
./write_example
```

**What it does:**
- Connects to TransferQueue server at localhost:8890
- Creates a sample trajectory with chat messages
- Writes the trajectory using the client SDK
- Logs success/failure

### 2. Read Example (`read_example`)
Demonstrates how to read trajectory groups from the TransferQueue server.

**Usage:**
```bash
./read_example
```

**What it does:**
- Connects to TransferQueue server at localhost:8890
- Reads up to 5 trajectory groups (non-blocking)
- Prints detailed information about each group and trajectory

### 3. Status Example (`status_example`)
Demonstrates how to query buffer status from the TransferQueue server.

**Usage:**
```bash
./status_example
```

**What it does:**
- Connects to TransferQueue server at localhost:8890
- Queries buffer status
- Prints comprehensive buffer statistics

## Building the Examples

To build these examples, make sure you have compiled the TransferQueue library and have the dependencies installed:

```bash
# From the project root directory
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

Then run the examples:

```bash
./examples/write_example
./examples/read_example  
./examples/status_example
```

## Note

These examples use placeholder implementations in the client SDK. The actual RPC calls are marked as TODO and will return placeholder results. To use with a real server, you'll need to complete the RPC implementation in `src/client/client.cc`.
