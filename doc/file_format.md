## Overall Structure

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

## Byte layout

The tables in this section are normative: they define every byte of the metadata region. The sections that
follow explain what the fields mean. All integers are **little endian**; `u8`, `u32` and `u64` are unsigned,
`i64` is two's complement. Offsets are relative to the start of the enclosing structure, and every record has
a fixed size, so a reader can skip records it does not need.

### header (16 bytes)

| offset | size | field | notes |
|---|---|---|---|
| 0 | 4 | `magic_number` | `0x52415443` (`CTAR` in little-endian byte order) |
| 4 | 4 | `file_format_version` | `major \| minor << 8 \| patch << 16` |
| 8 | 8 | `file_meta_size` | metadata region size minus these 16 bytes; the region is 4K-aligned |

### metadata region

```text
offset 0                       header (16 bytes)
offset 16                      pack_file_meta
after pack_file_meta           u64 data_block_count
after that                     data_block_meta[data_block_count]
up to 16 + file_meta_size      zero padding
```

`file_meta_size` counts everything after the header, including the zero padding, so the first block starts at
`16 + file_meta_size`, a multiple of 4096.

### pack_file_meta

| order | size | field |
|---|---|---|
| 1 | 8 | `total_file_count` |
| 2 | 8 | `total_original_size` |
| 3 | 8 | `total_compressed_size` |
| 4 | 8 | `padding_size` |
| 5 | 8 + n | `dir_name_data`: `u64 length` + `length` raw bytes |
| 6 | 8 + n | `file_name_data`: `u64 length` + `length` raw bytes |
| 7 | map | `dir_entry_records` |
| 8 | map | `file_entry_records` |
| 9 | map | `dir_id_to_child_dir_ids` |
| 10 | map | `dir_id_to_child_file_ids` |
| 11 | map | `dir_id_to_child_file_sizes` |

`dir_name_data` and `file_name_data` are concatenated names without separators. A name is located through a
`Handle` (`offset`, `len`) stored in the matching `EntryRecord`; offset 0 is the first byte of the buffer.

| map | encoding |
|---|---|
| `dir_entry_records`, `file_entry_records` | `u64 count` + `count × (u64 key + EntryRecord)`; every entry is 8 + 192 = 200 bytes |
| `dir_id_to_child_dir_ids`, `dir_id_to_child_file_ids`, `dir_id_to_child_file_sizes` | `u64 count` + `count × (u64 key + u64 n + n × u64)` |

### EntryRecord (192 bytes)

`EntryMeta` is the same layout without the 16-byte handle; it occupies bytes 16..192 (176 bytes).

| offset | size | field | notes |
|---|---|---|---|
| 0 | 8 | `name_handle.offset` | byte offset inside `dir_name_data` / `file_name_data` |
| 8 | 8 | `name_handle.len` | name length in bytes |
| 16 | 4 | `entry_meta.entry_type` | 0 = regular file, 1 = directory |
| 20 | 4 | `entry_meta.permissions` | POSIX mode bits |
| 24 | 8 | `entry_meta.block_id` | block holding this file (0 for directories) |
| 32 | 64 | `entry_meta.owner_name` | NUL-padded |
| 96 | 64 | `entry_meta.group_name` | NUL-padded |
| 160 | 8 | `entry_meta.modify_time` | `i64`, unix epoch milliseconds |
| 168 | 8 | `entry_meta.offset` | file payload offset inside the block |
| 176 | 8 | `entry_meta.file_orig_size` | original file size |
| 184 | 8 | `entry_meta.file_compressed_size` | stored (compressed) size |

### data block meta

| order | size | field | notes |
|---|---|---|---|
| 1 | 8 | `block_id` | |
| 2 | 8 | `dir_id` | the block holds files of exactly this directory |
| 3 | 8 | `block_file_offset` | offset relative to the block region start |
| 4 | 8 | `payload_size` | bytes occupied by the file payloads |
| 5 | 8 | `block_size` | bytes stored for this block; equals `payload_size` in archives written by this version |
| 6 | 8 | `hash_value` | reserved, currently 0 |
| 7 | 8 + 8n | `file_ids` | `u64 count` + `count × u64` |
| 8 | 8 + 8n | `file_orig_sizes` | `u64 count` + `count × u64` |
| 9 | 8 + 8n | `file_compressed_sizes` | `u64 count` + `count × u64` |
| 10 | 8 + n | `file_compressed_types` | `u64 count` + `count × u8` (0 = none, 1 = lz4, 2 = lz4hc) |
| 11 | 8 + 8n | `file_offsets` | `u64 count` + `count × u64`, relative to the block payload |
| 12 | 8 + 8n | `file_hashes` | `u64 count` + `count × u64`, XXH64 of the original bytes |

The raw `u64` arrays are stored as consecutive little-endian values. A data block meta is at least
12 × 8 = 96 bytes.

### block region, magic and padding

- The block region starts at `16 + file_meta_size` (4K-aligned).
- Blocks are stored back to back: block *i* starts at `block_region_start + block_file_offset`, and the next
  block's `block_file_offset` is the previous one plus its `block_size`.
- `block_size` is the number of stored payload bytes; this version has no per-block padding and reports
  `padding_size == 0`.
- The 4-byte `magic_number` follows the last block, at `16 + file_meta_size + Σ block_size`.
- Old archives (written before the contiguous layout) may have each `block_size` rounded up to 4 KiB and a
  non-zero `padding_size`. Readers must follow `block_file_offset` / `block_size` from the metadata and must
  not assume that blocks are contiguous or aligned.

### locating a file payload

1. Read `file_entry_records[id]`; take `entry_meta.block_id`, `entry_meta.offset`, `entry_meta.file_compressed_size`.
2. Read `data_block_metas[block_id]`; its payload starts at `block_region_start + block_file_offset`.
3. The file bytes are at `payload_start + entry_meta.offset`, `file_compressed_size` bytes long.
4. Decompress according to the block's `file_compressed_types`; verify `file_hashes`.

### version encoding and compatibility

`file_format_version` is a `u32`: `major` in bits 0..7, `minor` in 8..15, `patch` in 16..23, `reserved` in
24..31. The current value is `1.0.0` (`0x00000001`). A reader rejects an archive whose major is greater than
the supported one, or whose major is equal and minor is greater; the patch is ignored. Any change to the
layout in this section must bump `kFileFormatVersion`.

### header

```text
<begin_of_header>
[magic_number]
[version_number]
[file_meta_size]
<end_of_header>
```

- magic_number: occupies 4 bytes and contains `CTAR`
- version_number: the version number of this file, occupying 4 bytes
- file_meta_size: covers the metadata sections such as `pack file meta`, `count of data block meta`, and `data block meta`, **plus the padding added at the end of the metadata region to 4K-align it**, so that consumers can read all the metadata into memory in a single pass based on this value

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

- total_file_count: the number of files contained in this pack file
- total_original_size: the sum of the original sizes of all files in this pack file
- total_compressed_size: the sum of the compressed sizes of all files in this pack file
- padding_size: number of padding bytes in the data block region (block region size minus `total_compressed_size`). Archives written by this version store blocks back to back, so this is 0; older archives may report their per-block alignment padding
- dir_name_data: stores all directory names
- file_name_data: stores all file names
- dir_id_to_child_dir_ids: records all child directory ids under each directory id
- dir_id_to_child_file_ids: records all file ids under each directory id
- dir_id_to_child_file_sizes: records the sizes of all files under each directory id

### count of data block meta

```text
<begin_of_count_of_data_block_meta>
[data_block_count]
<end_of_count_of_data_block_meta>
```

- data_block_count: a `uint64_t` occupying 8 bytes, recording how many `data block meta` entries immediately follow it. A value of `n` means the next `n` `data block meta` entries are laid out back to back
- it sits between `pack file meta` and the first `data block meta`, and is counted as part of `file_meta_size`
- consumers must read this value first to know how many `data block meta` entries to decode, and consequently where the data block region begins

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

- block_id: the sequence number of the data block, starting from 1
- dir_id: the id of the directory that owns the files stored in this data block; a single data block holds the files of at most one directory, but the files of one directory may be spread across multiple data blocks
- block_file_offset: the offset of the data block relative to the end of the metadata region (that is, the start of the data block region); adding the length of the metadata region (`header` size + `file_meta_size`) yields the absolute offset of this data block within the pack file
- payload_size: the size of the valid data stored in the data block
- block_size: the size of the data block as stored in the file (compressed when compression is enabled); it equals `payload_size`. Blocks are stored back to back; only the start of the data block region is 4K-aligned
- hash_value: the hash of the data block; this value is not computed at present
- file_ids: the file ids of the files stored in this data block; file_id is the unique identifier of every file under the packed directory tree
- file_orig_sizes: the original sizes of the files in this data block
- file_compressed_sizes: the compressed sizes of the files in this data block
- file_compressed_types: identifies the compression type of the corresponding file; can be `none`, `lz4`, or `lz4hc`, and defaults to `lz4`
- file_offsets: the offset of each file's content in this data block relative to the start of the data block it belongs to
- file_hashes: the hash of each file's original content in this data block, computed with xxh64

## data block

```text
<begin_of_data_block>
[data_of_file_1(may compressed)]
[data_of_file_2(may compressed)]
...
[data_of_file_n(may compressed)]
<end_of_data_block>
```

- Consumers may decide for themselves whether to compress files. By default `lz4` is used, but already-compressed formats such as `jpeg`, `png`, and `parquet` are excluded; in that case the corresponding compression type is `none`
- Every data block has a corresponding data block meta. The data block meta records the offset of each packed file within the data block, so consumers can read a single file or a batch of files depending on the use case
- blocks are stored back to back; only the start of the data block region is 4K-aligned, so a block needs no trailing padding

## magic_number

- magic_number occupies 4 bytes and contains `CTAR`
