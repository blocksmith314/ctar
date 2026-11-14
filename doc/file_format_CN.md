## 整体结构

```text
<begin_of_file>
[header]
[pack file meta]
[count of data block meta]
[data block meta 1]
[data block meta 2]
...
[data block meta n]
[data block 1]
[data block 2]
...
[data block n]
[magic_number]
<end_of_file>
```

## 字节布局

本节中的表格是规范性的，定义了元数据区中的每一个字节；后面的章节解释字段的含义。所有整数均为**小端**；
`u8`、`u32`、`u64` 为无符号，`i64` 为补码。偏移相对于所属结构的起始位置，所有记录都是固定大小，
因此 reader 可以跳过不需要的记录。

### header（16 字节）

| 偏移 | 大小 | 字段 | 说明 |
|---|---|---|---|
| 0 | 4 | `magic_number` | `0x52415443`（小端字节序下即 `CTAR`） |
| 4 | 4 | `file_format_version` | `major \| minor << 8 \| patch << 16` |
| 8 | 8 | `file_meta_size` | 元数据区大小减去这 16 字节；元数据区按 4K 对齐 |

### 元数据区

```text
偏移 0                          header（16 字节）
偏移 16                         pack_file_meta
pack_file_meta 之后             u64 data_block_count
之后                            data_block_meta[data_block_count]
直到 16 + file_meta_size        零填充
```

`file_meta_size` 包含 header 之后的所有内容（包括零填充），因此第一个块从 `16 + file_meta_size` 开始，
该值是 4096 的倍数。

### pack_file_meta

| 顺序 | 大小 | 字段 |
|---|---|---|
| 1 | 8 | `total_file_count` |
| 2 | 8 | `total_original_size` |
| 3 | 8 | `total_compressed_size` |
| 4 | 8 | `padding_size` |
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
| 168 | 8 | `entry_meta.offset` | 文件 payload 在块内的偏移 |
| 176 | 8 | `entry_meta.file_orig_size` | 文件原始大小 |
| 184 | 8 | `entry_meta.file_compressed_size` | 存储（压缩后）大小 |

### data block meta

| 顺序 | 大小 | 字段 | 说明 |
|---|---|---|---|
| 1 | 8 | `block_id` | |
| 2 | 8 | `dir_id` | 该块只包含这个目录下的文件 |
| 3 | 8 | `block_file_offset` | 相对于块区起点的偏移 |
| 4 | 8 | `payload_size` | 文件 payload 占用的字节数 |
| 5 | 8 | `block_size` | 该块在文件中存储的字节数；当前版本等于 `payload_size` |
| 6 | 8 | `hash_value` | 保留，当前为 0 |
| 7 | 8 + 8n | `file_ids` | `u64 count` + `count × u64` |
| 8 | 8 + 8n | `file_orig_sizes` | `u64 count` + `count × u64` |
| 9 | 8 + 8n | `file_compressed_sizes` | `u64 count` + `count × u64` |
| 10 | 8 + n | `file_compressed_types` | `u64 count` + `count × u8`（0 = none，1 = lz4，2 = lz4hc） |
| 11 | 8 + 8n | `file_offsets` | `u64 count` + `count × u64`，相对于块 payload 起点 |
| 12 | 8 + 8n | `file_hashes` | `u64 count` + `count × u64`，原始数据的 XXH64 |

原始 `u64` 数组按连续的小端数值存储。一个 data block meta 至少为 12 × 8 = 96 字节。

### 块区、magic 与 padding

- 块区从 `16 + file_meta_size` 开始（4K 对齐）。
- 块与块连续存放：第 *i* 个块从 `block_region_start + block_file_offset` 开始，下一个块的
  `block_file_offset` 等于前一个块的偏移加上它的 `block_size`。
- `block_size` 是该块实际存储的 payload 字节数；当前版本没有逐块 padding，`padding_size == 0`。
- 最后一个块之后是 4 字节的 `magic_number`，位置为 `16 + file_meta_size + Σ block_size`。
- 旧归档（连续布局之前写出的）可能把每个 `block_size` 补齐到 4 KiB，且 `padding_size` 非 0。reader
  必须依据元数据中的 `block_file_offset` / `block_size` 读取，不能假设块连续或对齐。

### 定位文件 payload

1. 读取 `file_entry_records[id]`，取出 `entry_meta.block_id`、`entry_meta.offset`、`entry_meta.file_compressed_size`。
2. 读取 `data_block_metas[block_id]`，其 payload 起点为 `block_region_start + block_file_offset`。
3. 文件数据位于 `payload_start + entry_meta.offset`，长度为 `file_compressed_size` 字节。
4. 按该块的 `file_compressed_types` 解压，并用 `file_hashes` 校验。

### 版本编码与兼容性

`file_format_version` 是 `u32`：`major` 位于第 0..7 位，`minor` 位于 8..15 位，`patch` 位于 16..23 位，
`reserved` 位于 24..31 位。当前值为 `1.0.0`（`0x00000001`）。reader 拒绝 major 大于支持版本、或 major
相同但 minor 更大的归档；patch 被忽略。任何修改本节所描述布局的改动都必须提升 `kFileFormatVersion`。

### header

```text
<begin_of_header>
[magic_number]
[version_number]
[file_meta_size]
<end_of_header>
```

- magic_number: 占用 4 字节，内容为 `CTAR`
- version_number: 该文件的版本号，占用 4 字节
- file_meta_size: 包含 `pack file meta`、`count of data block meta`、`data block meta` 等元数据模块，**以及元数据区末尾为 4K 对齐补的 padding**，这样使用者可以根据该值一次性将元数据读入内存中

### pack_file_meta

```text
<begin_of_pack_file_meta>
[total_file_count]
[total_original_size]
[total_compressed_size]
[padding_size]
[dir_name_data]
[file_name_data]
[dir_entry_records]
[file_entry_records]
[dir_id_to_child_dir_ids]
[dir_id_to_child_file_ids]
[dir_id_to_child_file_sizes]
<end_of_pack_file_meta>
```

- total_file_count: 该打包文件包含的文件数
- total_original_size: 该打包文件所有文件的原始大小之和
- total_compressed_size: 该打包文件所有文件压缩后的大小之和
- padding_size: data block 区域中 padding 的字节数（块区域大小减去 `total_compressed_size`）。当前版本写出的归档中块与块连续存放，因此为 0；旧归档可能包含逐块对齐 padding
- dir_name_data: 保存了所有的文件夹名称
- file_name_data: 保存了所有的文件名称
- dir_id_to_child_dir_ids: 记录每个文件夹 id 下的所有文件夹 id
- dir_id_to_child_file_ids: 记录每个文件夹 id 下的所有文件 id
- dir_id_to_child_file_sizes: 记录每个文件夹 id 下的所有文件大小

### count of data block meta

```text
<begin_of_count_of_data_block_meta>
[data_block_count]
<end_of_count_of_data_block_meta>
```

- data_block_count: 占用 8 字节的 `uint64_t`，记录紧随其后的 `data block meta` 的数量。值为 `n` 表示后面依次紧挨着排列 `n` 个 `data block meta`
- 它位于 `pack file meta` 与第一个 `data block meta` 之间，计入 `file_meta_size`
- 使用者必须先读取该值，才能知道需要解码多少个 `data block meta`，进而确定数据块区的起始位置

### data block meta

```text
<begin_of_data_block_meta>
block_id
dir_id
block_file_offset
payload_size
block_size
hash_value
file_ids
file_orig_sizes
file_compressed_sizes
file_compressed_types
file_offsets
file_hashes
<end_of_data_block_meta>
```

- block_id: `data_block` 的序号，从 1 开始
- dir_id: `data_block` 保存的文件所属的文件夹id, 每个 `data_block` 至多保存一个文件夹的文件，但一个文件夹下的文件可能保存在多个
  `data_block` 中
- block_file_offset: `data_block` 相对于元数据区末尾（即数据块区起点）的偏移量；加上元数据区长度（`header` 长度 + `file_meta_size`）即为该 `data_block` 在打包文件中的绝对偏移量
- payload_size: `data_block` 保存的有效数据的大小
- block_size: `data_block` 在文件中实际存储的字节数（启用压缩时为压缩后大小），等于 `payload_size`。块与块连续存放，只有 data block 区域起点是 4K 对齐的
- hash_value: `data_block` 的 hash 值，当前该值未计算
- file_ids: `data_block` 保存的文件对应的文件 id, `file_id` 为整个被打包文件夹下所有文件的唯一标识
- file_orig_sizes: `data_block` 下的文件对应的原始文件大小
- file_compressed_sizes: `data_block` 下的文件对应的压缩后的文件大小
- file_compressed_types: 标识对应文件的压缩类型，可以是 `none`、`lz4`、`lz4hc`，默认是 `lz4`
- file_offsets: `data_block` 下的每个文件的文件内容相对于所属 `data_block` 起点的偏移量
- file_hashes: `data_block` 下的每个文件原始内容的 hash 值，采用 xxh64 计算

## data block

```text
<begin_of_data_block>
[data_of_file_1(may compressed)]
[data_of_file_2(may compressed)]
...
[data_of_file_n(may compressed)]
<end_of_data_block>
```

- 使用者可以自行决定是否对文件进行压缩，默认情况下会用 `lz4` 进行压缩，但会排除 `jpeg`、`png`、`parquet` 等已经压缩过的文件，此时对应的压缩类型为 `none`
- 每个 `data block` 都有对应的 `data block meta`, `data block meta` 中记录了每个被打包文件在 `data block`
  中的偏移量，使用者可以根据使用场景进行单个文件的读取或者批量读取
- 块与块连续存放，只有 data block 区域起点是 4K 对齐的，因此块本身不需要尾部 padding

## magic_number

- magic_number, 占用4字节，内容为 `CTAR`
