## Overall structure (file format 1.1.0)

```text
<begin_of_file>
[header 4096 bytes]
[file data block 1]
[file data block 2]
...
[file data block n]
[metadata (compressed or raw)]
[magic_number 4 bytes]
<end_of_file>
```

## Byte layout

The tables in this section are normative: they define every byte of the header and the metadata region. All
integers are **little endian**; `u8`, `u32` and `u64` are unsigned, `i64` is two's complement. Offsets are
relative to the start of the enclosing structure.

### header (4096 bytes, first 136 bytes used)

The header is a fixed 4096-byte region at offset 0. The first 136 bytes hold the fields below; the remaining
3960 bytes are zero. Every `u64` field is 8-byte aligned.

| offset | size | field | type | notes |
|---|---|---|---|---|
| 0 | 4 | `magic_number` | u32 | `0x52415443` (`CTAR`) |
| 4 | 4 | `file_format_version` | u32 | `major \| minor << 8 \| patch << 16` |
| 8 | 4 | `ctar_version` | u32 | tool version that wrote the archive (`kToolVersion`) |
| 12 | 4 | `compressed_param` | u32 | shared lz4 acceleration / lz4hc level (0 for none) |
| 16 | 8 | `pack_begin_time` | u64 | pack start, UTC epoch milliseconds |
| 24 | 8 | `pack_end_time` | u64 | pack end, UTC epoch milliseconds |
| 32 | 8 | `pack_file_size` | u64 | final archive size; equals the file size |
| 40 | 8 | `file_count` | u64 | number of files |
| 48 | 8 | `directory_count` | u64 | number of directory entries, including the root |
| 56 | 8 | `block_count` | u64 | number of data blocks |
| 64 | 8 | `file_data_offset` | u64 | start of file data; 4096 in this version |
| 72 | 8 | `file_data_original_size` | u64 | original file bytes |
| 80 | 8 | `file_data_compressed_size` | u64 | compressed file payload bytes |
| 88 | 8 | `file_data_compressed_type` | u64 | default `CompressionType` of file data |
| 96 | 8 | `metadata_offset` | u64 | start of the stored metadata |
| 104 | 8 | `metadata_original_size` | u64 | serialized metadata size before compression |
| 112 | 8 | `metadata_compressed_size` | u64 | stored metadata size |
| 120 | 8 | `metadata_compressed_type` | u64 | `CompressionType` of metadata |
| 128 | 8 | `metadata_checksum` | u64 | XXH64 of the **uncompressed** metadata |
| 136 | 3960 | `reserved` | | must be 0 |

Invariants:

```text
file_data_offset        == 4096
metadata_offset         == file_data_offset + file_data_compressed_size
pack_file_size          == metadata_offset + metadata_compressed_size + 4
pack_file_size          == fs::file_size(archive)
```

`CompressionType` values: 0 = none, 1 = lz4, 2 = lz4hc.

### file data region

- Starts at `file_data_offset` (4096) and is not required to be 4K-aligned internally.
- Blocks are stored back to back: block `i` starts at `file_data_offset + block_file_offset`, and the next
  block's `block_file_offset` is the previous one plus its `block_size`.
- `block_size` is the number of stored bytes of the block payload. This version has no per-block padding.
- `file_data_compressed_size` is the total stored size of all block payloads.

### metadata region

- Located at `metadata_offset`, after the file data region.
- It is either stored raw or compressed:
  - `metadata_compressed_type == 0`: `metadata_compressed_size == metadata_original_size`.
  - otherwise: the stored bytes are the compressed form of the serialized metadata.
- Metadata is compressed only when `metadata_original_size >= 1 MiB` **and** file data compression is enabled.
  It follows the file data compression type and `compressed_param`.
- `metadata_checksum` is the XXH64 (using the file-hash seed) of the uncompressed serialized metadata.
- The serialized metadata is:

```text
pack_file_meta
u64 data_block_count
data_block_meta[data_block_count]
```

### pack_file_meta

| order | size | field |
|---|---|---|
| 1 | 8 | `file_count` |
| 2 | 8 | `file_data_original_size` |
| 3 | 8 | `file_data_compressed_size` |
| 4 | 8 | `block_padding_size` (0 in 1.1.0; may be non-zero in 1.0.0 archives) |
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
| 168 | 8 | `entry_meta.file_offset` | absolute offset of the file payload from the start of the archive (`file_data_offset + block_file_offset + DataBlockMeta.file_offsets[i]`) |
| 176 | 8 | `entry_meta.file_orig_size` | original file size |
| 184 | 8 | `entry_meta.file_compressed_size` | stored (compressed) size |

### data block meta

| order | size | field | notes |
|---|---|---|---|
| 1 | 8 | `block_id` | |
| 2 | 8 | `dir_id` | the block holds files of exactly this directory |
| 3 | 8 | `block_file_offset` | offset relative to `file_data_offset` |
| 4 | 8 | `payload_size` | bytes occupied by the file payloads |
| 5 | 8 | `block_size` | bytes stored for this block; equals `payload_size` in this version |
| 6 | 8 | `hash_value` | reserved, currently 0 |
| 7 | 8 + 8n | `file_ids` | `u64 count` + `count × u64` |
| 8 | 8 + 8n | `file_orig_sizes` | `u64 count` + `count × u64` |
| 9 | 8 + 8n | `file_compressed_sizes` | `u64 count` + `count × u64` |
| 10 | 8 + n | `file_compressed_types` | `u64 count` + `count × u8` (0 = none, 1 = lz4, 2 = lz4hc) |
| 11 | 8 + 8n | `file_offsets` | `u64 count` + `count × u64`, relative to the block payload |
| 12 | 8 + 8n | `file_hashes` | `u64 count` + `count × u64`, XXH64 of the original bytes |

The raw `u64` arrays are stored as consecutive little-endian values. A data block meta is at least
12 × 8 = 96 bytes.

### magic_number

The 4-byte `magic_number` (`CTAR`) follows the metadata at `metadata_offset + metadata_compressed_size`.

### locating a file payload

1. Read `file_entry_records[id]`; take `entry_meta.block_id`, `entry_meta.file_offset`, `entry_meta.file_compressed_size`.
2. Read `data_block_metas[block_id]`; its payload starts at `file_data_offset + block_file_offset`.
3. The file bytes are at `entry_meta.file_offset`, `file_compressed_size` bytes long. Equivalently, the block-relative offset is `data_block_metas[block_id].file_offsets[index]`, where `index` is the position of `id` in `file_ids`; the absolute position is `payload_start + file_offsets[index]`.
4. Decompress according to `file_compressed_types`; verify `file_hashes`.

`dump`'s `offset` column reports this absolute `entry_meta.file_offset`; `ls` does not display it.

### reader validation contracts

The reader rejects archives that violate these cross-field invariants; a writer must satisfy them even though they are not separate fields:

- `file_ids` are strictly increasing within a block, so a block cannot reference the same file twice.
- File ranges (`file_offsets` + `file_compressed_sizes`) are ordered, non-overlapping, and fit inside `payload_size`.
- For `kLZ4`/`kLZ4HC`, `file_orig_sizes` and `file_compressed_sizes` fit the LZ4 `int` API, `file_orig_sizes <= 255 * file_compressed_sizes`, and a zero original size requires a zero stored size.
- Every `file_ids[i]` exists in `file_entry_records`; the entry's `block_id`, `file_orig_size`, `file_compressed_size` and absolute `file_offset` agree with the block, and every indexed file is referenced exactly once.
- Directory paths are unique, and a directory contains no duplicate file names, so every extracted file has a distinct output path.
- `unpack` enforces a per-file decompressed-size budget (`--max-decompressed-size`, default 1 GiB, `0` disables it). Archives with larger files need a larger budget or chunked files (#38).
- All archive readers enforce a per-archive metadata budget (`--max-metadata-size`, default 1 GiB, `0` disables it) before allocating the uncompressed metadata. Compressed metadata above the LZ4 `int` limit is always rejected, and empty data blocks are invalid.

### version encoding and compatibility

`file_format_version` is a `u32`: `major` in bits 0..7, `minor` in 8..15, `patch` in 16..23, `reserved` in
24..31. The current value is `1.1.0` (`0x00000101`). A reader rejects an archive whose major is greater than the
supported one, or whose major is equal and minor is greater; the patch is ignored. Any change to the layout in
this section must bump `kFileFormatVersion`.

### 1.0.0 layout (still readable)

1.0.0 archives use a 16-byte header and store the metadata at the front:

```text
[16-byte header]
[metadata region, 4K-aligned]
[file data blocks]
[magic_number]
```

| offset | size | field |
|---|---|---|
| 0 | 4 | `magic_number` |
| 4 | 4 | `file_format_version` |
| 8 | 8 | `file_meta_size` (metadata region size minus 16) |

Readers must follow `block_file_offset` and `block_size` from the metadata; old archives may have per-block
padding and a non-zero `padding_size`. 1.0.0 metadata is never compressed.
