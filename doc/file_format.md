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
- padding_size: for performance reasons, every data block is 4K-aligned; this is the number of padding bytes that alignment introduces
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
- payload_size: the size of the valid data stored in the data block, i.e. excluding padding bytes
- block_size: the size of the data block, including the padding bytes introduced by alignment
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
[padding]
<end_of_data_block>
```

- Consumers may decide for themselves whether to compress files. By default `lz4` is used, but already-compressed formats such as `jpeg`, `png`, and `parquet` are excluded; in that case the corresponding compression type is `none`
- Every data block has a corresponding data block meta. The data block meta records the offset of each packed file within the data block, so consumers can read a single file or a batch of files depending on the use case
- padding ensures that every data block is 4K-aligned

## magic_number

- magic_number occupies 4 bytes and contains `CTAR`
