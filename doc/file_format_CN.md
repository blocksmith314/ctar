## 整体结构（文件格式 1.1.0）

```text
<begin_of_file>
[header 4096 字节]
[file data block 1]
[file data block 2]
...
[file data block n]
[metadata（压缩或原始）]
[magic_number 4 字节]
<end_of_file>
```

## 字节布局

本节表格是规范性的，定义 header 和 metadata 区的每一个字节。所有整数均为**小端**；`u8`、`u32`、`u64`
为无符号，`i64` 为补码。偏移相对于所属结构的起始位置。

### header（4096 字节，前 136 字节有效）

header 是位于偏移 0 的固定 4096 字节区域。前 136 字节存放字段，其余 3960 字节为 0。所有 `u64` 字段按
8 字节对齐。

| 偏移 | 大小 | 字段 | 类型 | 说明 |
|---|---|---|---|---|
| 0 | 4 | `magic_number` | u32 | `0x52415443`（`CTAR`） |
| 4 | 4 | `file_format_version` | u32 | `major \| minor << 8 \| patch << 16` |
| 8 | 4 | `ctar_version` | u32 | 写入归档的工具版本（`kToolVersion`） |
| 12 | 4 | `compressed_param` | u32 | 共享的 lz4 acceleration / lz4hc level（none 时为 0） |
| 16 | 8 | `pack_begin_time` | u64 | 打包开始时间，UTC epoch 毫秒 |
| 24 | 8 | `pack_end_time` | u64 | 打包结束时间，UTC epoch 毫秒 |
| 32 | 8 | `pack_file_size` | u64 | 最终归档大小，等于文件大小 |
| 40 | 8 | `file_count` | u64 | 文件数 |
| 48 | 8 | `directory_count` | u64 | 目录条目数（含根目录） |
| 56 | 8 | `block_count` | u64 | 数据块数 |
| 64 | 8 | `file_data_offset` | u64 | file data 起点；本版本为 4096 |
| 72 | 8 | `file_data_original_size` | u64 | 原始文件内容总字节 |
| 80 | 8 | `file_data_compressed_size` | u64 | 压缩后的 file payload 总字节 |
| 88 | 8 | `file_data_compressed_type` | u64 | file data 默认 `CompressionType` |
| 96 | 8 | `metadata_offset` | u64 | 存储的 metadata 起点 |
| 104 | 8 | `metadata_original_size` | u64 | 压缩前序列化 metadata 大小 |
| 112 | 8 | `metadata_compressed_size` | u64 | 存储的 metadata 大小 |
| 120 | 8 | `metadata_compressed_type` | u64 | metadata 的 `CompressionType` |
| 128 | 8 | `metadata_checksum` | u64 | **未压缩** metadata 的 XXH64 |
| 136 | 3960 | `reserved` | | 必须为 0 |

不变量：

```text
file_data_offset        == 4096
metadata_offset         == file_data_offset + file_data_compressed_size
pack_file_size          == metadata_offset + metadata_compressed_size + 4
pack_file_size          == fs::file_size(archive)
```

`CompressionType`：0 = none，1 = lz4，2 = lz4hc。

### file data 区

- 从 `file_data_offset`（4096）开始，内部不要求 4K 对齐。
- 块与块连续存放：第 `i` 个块从 `file_data_offset + block_file_offset` 开始，下一个块的
  `block_file_offset` 等于前一个块的偏移加上它的 `block_size`。
- `block_size` 是该块实际存储的 payload 字节数；本版本没有逐块 padding。
- `file_data_compressed_size` 是所有块 payload 的存储总大小。

### metadata 区

- 位于 `metadata_offset`，在 file data 区之后。
- 可能原样存储，也可能压缩：
  - `metadata_compressed_type == 0`：`metadata_compressed_size == metadata_original_size`。
  - 否则：存储的是序列化 metadata 的压缩形式。
- 仅当 `metadata_original_size >= 1 MiB` **且** file data 启用压缩时才压缩 metadata；压缩类型和
  `compressed_param` 跟随 file data。
- `metadata_checksum` 是对未压缩序列化 metadata 计算的 XXH64（使用文件哈希 seed）。
- 序列化 metadata 为：

```text
pack_file_meta
u64 data_block_count
data_block_meta[data_block_count]
```

### pack_file_meta

| 顺序 | 大小 | 字段 |
|---|---|---|
| 1 | 8 | `file_count` |
| 2 | 8 | `file_data_original_size` |
| 3 | 8 | `file_data_compressed_size` |
| 4 | 8 | `block_padding_size`（1.1.0 为 0；1.0.0 旧归档可能非 0） |
| 5 | 8 + n | `dir_name_data`：`u64 length` + `length` 个原始字节 |
| 6 | 8 + n | `file_name_data`：`u64 length` + `length` 个原始字节 |
| 7 | map | `dir_entry_records` |
| 8 | map | `file_entry_records` |
| 9 | map | `dir_id_to_child_dir_ids` |
| 10 | map | `dir_id_to_child_file_ids` |
| 11 | map | `dir_id_to_child_file_sizes` |

`dir_name_data` 和 `file_name_data` 是首尾相接、没有分隔符的名称数据。名称通过 `EntryRecord` 中的
`Handle`（`offset`、`len`）定位；`offset` 为 0 表示缓冲区第一个字节。

| map | 编码 |
|---|---|
| `dir_entry_records`、`file_entry_records` | `u64 count` + `count × (u64 key + EntryRecord)`；每条 8 + 192 = 200 字节 |
| `dir_id_to_child_dir_ids`、`dir_id_to_child_file_ids`、`dir_id_to_child_file_sizes` | `u64 count` + `count × (u64 key + u64 n + n × u64)` |

### EntryRecord（192 字节）

`EntryMeta` 是去掉 16 字节 `Handle` 后的同一布局，占据第 16..192 字节（176 字节）。

| 偏移 | 大小 | 字段 | 说明 |
|---|---|---|---|
| 0 | 8 | `name_handle.offset` | 在 `dir_name_data` / `file_name_data` 中的字节偏移 |
| 8 | 8 | `name_handle.len` | 名称字节数 |
| 16 | 4 | `entry_meta.entry_type` | 0 = 普通文件，1 = 目录 |
| 20 | 4 | `entry_meta.permissions` | POSIX 权限位 |
| 24 | 8 | `entry_meta.block_id` | 文件所在块（目录为 0） |
| 32 | 64 | `entry_meta.owner_name` | NUL 填充 |
| 96 | 64 | `entry_meta.group_name` | NUL 填充 |
| 160 | 8 | `entry_meta.modify_time` | `i64`，Unix epoch 毫秒 |
| 168 | 8 | `entry_meta.file_offset` | 文件 payload 从归档起点算起的绝对偏移（`file_data_offset + block_file_offset + DataBlockMeta.file_offsets[i]`） |
| 176 | 8 | `entry_meta.file_orig_size` | 文件原始大小 |
| 184 | 8 | `entry_meta.file_compressed_size` | 存储（压缩后）大小 |

### data block meta

| 顺序 | 大小 | 字段 | 说明 |
|---|---|---|---|
| 1 | 8 | `block_id` | |
| 2 | 8 | `dir_id` | 该块只包含这个目录下的文件 |
| 3 | 8 | `block_file_offset` | 相对于 `file_data_offset` 的偏移 |
| 4 | 8 | `payload_size` | 文件 payload 占用的字节数 |
| 5 | 8 | `block_size` | 该块在文件中存储的字节数；本版本等于 `payload_size` |
| 6 | 8 | `hash_value` | 保留，当前为 0 |
| 7 | 8 + 8n | `file_ids` | `u64 count` + `count × u64` |
| 8 | 8 + 8n | `file_orig_sizes` | `u64 count` + `count × u64` |
| 9 | 8 + 8n | `file_compressed_sizes` | `u64 count` + `count × u64` |
| 10 | 8 + n | `file_compressed_types` | `u64 count` + `count × u8`（0 = none，1 = lz4，2 = lz4hc） |
| 11 | 8 + 8n | `file_offsets` | `u64 count` + `count × u64`，相对于块 payload 起点 |
| 12 | 8 + 8n | `file_hashes` | `u64 count` + `count × u64`，原始数据的 XXH64 |

原始 `u64` 数组按连续的小端数值存储。一个 data block meta 至少为 12 × 8 = 96 字节。

### magic_number

4 字节的 `magic_number`（`CTAR`）位于 `metadata_offset + metadata_compressed_size`。

### 定位文件 payload

1. 读取 `file_entry_records[id]`，取出 `entry_meta.block_id`、`entry_meta.file_offset`、`entry_meta.file_compressed_size`。
2. 读取 `data_block_metas[block_id]`，其 payload 起点为 `file_data_offset + block_file_offset`。
3. 文件数据位于 `entry_meta.file_offset`，长度为 `file_compressed_size` 字节。等价地，块内相对偏移是 `data_block_metas[block_id].file_offsets[index]`（`index` 是 `id` 在 `file_ids` 中的位置），绝对位置为 `payload_start + file_offsets[index]`。
4. 按 `file_compressed_types` 解压，并用 `file_hashes` 校验。

`dump` 的 `offset` 列显示这个绝对值；`ls` 不显示 offset。

### 读取端校验约束

以下跨字段不变量不是独立的序列化字段，但 reader 会拒绝违反它们的归档，writer 必须满足：

- 块内 `file_ids` 严格递增，同一块不能引用同一个文件两次。
- 文件区间（`file_offsets` + `file_compressed_sizes`）有序、互不重叠，且都位于 `payload_size` 内。
- 对 `kLZ4`/`kLZ4HC`，`file_orig_sizes` 和 `file_compressed_sizes` 必须落在 LZ4 的 `int` API 范围内，`file_orig_sizes <= 255 * file_compressed_sizes`，且原始大小为 0 时存储大小也必须为 0。
- 每个 `file_ids[i]` 都必须存在于 `file_entry_records`，其 `block_id`、`file_orig_size`、`file_compressed_size` 和绝对 `file_offset` 与块元数据一致；所有索引文件都必须被且仅被引用一次。
- 目录路径唯一，同一目录内文件名唯一，保证每个解压文件有唯一的输出路径。
- `unpack` 对每个文件施加解压大小预算（`--max-decompressed-size`，默认 1 GiB，`0` 表示不限制）。更大的文件需要调大预算，或等待分块支持（#38）。
- 所有归档读取命令都对存储和未压缩 metadata 施加每归档预算（`--max-metadata-size`，默认 1 GiB，`0` 表示不限制），并在分配任一缓冲区之前检查。超过 LZ4 `int` 上限的压缩 metadata 始终被拒绝，压缩 metadata 必须严格小于原始大小，空 data block 也是非法的。

### 版本编码与兼容性

`file_format_version` 是 `u32`：`major` 位于第 0..7 位，`minor` 位于 8..15 位，`patch` 位于 16..23 位，
`reserved` 位于 24..31 位。当前值为 `1.1.0`（`0x00000101`）。reader 拒绝 major 大于支持版本、或 major
相同但 minor 更大的归档；patch 被忽略。任何修改本节布局的改动都必须提升 `kFileFormatVersion`。

### 1.0.0 布局（仍可读取）

1.0.0 使用 16 字节 header，并把 metadata 放在文件开头：

```text
[16 字节 header]
[metadata 区，4K 对齐]
[file data blocks]
[magic_number]
```

| 偏移 | 大小 | 字段 |
|---|---|---|
| 0 | 4 | `magic_number` |
| 4 | 4 | `file_format_version` |
| 8 | 8 | `file_meta_size`（metadata 区大小减去 16） |

reader 必须依据 metadata 中的 `block_file_offset` / `block_size` 读取；旧归档可能有逐块 padding，且
`padding_size` 非 0。1.0.0 的 metadata 从不压缩。
