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
[data block 1][padding]
[data block 2][padding]
...
[data block n][padding]
[magic_number]
<end_of_file>
```

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
- padding_size: 出于性能考虑，每个 data block 都是 4k 对齐的，这里是对齐带来的 padding 字节数
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
- payload_size: `data_block` 保存的有效数据的大小，即不包含 padding 字符数
- block_size: `data_block` 的大小，包含由于对齐产生的 padding 字符数
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
[padding]
<end_of_data_block>
```

- 使用者可以自行决定是否对文件进行压缩，默认情况下会用 `lz4` 进行压缩，但会排除 `jpeg`、`png`、`parquet` 等已经压缩过的文件，此时对应的压缩类型为 `none`
- 每个 `data block` 都有对应的 `data block meta`, `data block meta` 中记录了每个被打包文件在 `data block`
  中的偏移量，使用者可以根据使用场景进行单个文件的读取或者批量读取
- padding 保证每个 `data block` 是 4k 对齐的

## magic_number

- magic_number, 占用4字节，内容为 `CTAR`
