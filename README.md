English | [中文](./README_CN.md)

# ctar - an extremely fast packing tool

ctar (Cloud-era Tar) is a modern archiving tool inspired by traditional tar.
It is designed for workloads with huge numbers of small files.
Packing is fully multithreaded, making it faster than traditional tar, and the resulting archive is optimized for cloud object storage.
It is a good fit for industries such as AI and autonomous driving that process large volumes of multimodal data.

## Core features

- **Multithreaded packing and unpacking** — uses all CPU cores by default; the thread count can be configured, and the more files there are the bigger the win
- **Per-file random access** — every file's offset is recorded in the index, so a single file or a batch of files can be read without downloading or extracting the whole archive
- **A layout built for cloud object storage** — the files of one directory are stored together, which makes block-level fetching and parallel reads cheap
- **Optional compression** — `lz4` (default, favours speed), `lz4hc` (favours ratio) or `none`; already-compressed formats such as `jpeg`, `png` and `parquet` are skipped
- **Tunable compression** — the lz4 acceleration factor or the lz4hc compression level can be set
- **Full metadata preservation** — permissions, owner, group and modification time
- **Inspect archives and local directories alike** — `tree` prints the directory tree, `ls` lists metadata, `dump` exports a filterable TSV index

Run `ctar --help` for the rest.

## Installation

### Prebuilt binaries

Download the artifact for your platform from the [releases page](https://github.com/blocksmith314/ctar/releases):

| Platform | Standalone executable |
|---|---|
| Linux x86_64 | `ctar-<version>-linux-x86_64` |
| macOS arm64 | `ctar-<version>-macos-arm64` |

> **macOS requirement**: macOS **13.3 or newer**, on Apple Silicon.

```shell
VERSION=v1.0.0
BASE=https://github.com/blocksmith314/ctar/releases/download/$VERSION

curl -sSL -O "$BASE/ctar-$VERSION-linux-x86_64"
curl -sSL -O "$BASE/SHA256SUMS"

# verify the download
sha256sum -c --ignore-missing SHA256SUMS

chmod +x ctar-$VERSION-linux-x86_64
sudo mv ctar-$VERSION-linux-x86_64 /usr/local/bin/ctar
ctar --help
```

> **macOS**: a binary downloaded through a browser is blocked by Gatekeeper with
> "Apple cannot check it for malicious software". That is not a malware warning —
> the artifact is simply not signed with an Apple Developer ID or notarized.
> Clear the quarantine attribute to run it:
>
> ```shell
> xattr -d com.apple.quarantine ./ctar-$VERSION-macos-arm64
> ```
>
> Downloading with `curl` instead avoids the attribute altogether.

## Common commands

```shell
# Pack files
ctar pack input_dir pack_file.ctar

# Unpack files
ctar unpack pack_file.ctar output_dir

# View the packed directory as a directory tree
ctar tree pack_file.ctar

# Show file metadata
ctar ls pack_file.ctar

# Show file metadata and index information; the index information can be used to
# quickly read a single file or a batch of files
ctar dump pack_file.ctar
```

In most cases the `pack` and `unpack` commands are all you need, and the more files there are, the more noticeable
the benefit. If you have other requirements — for example, storing the packed file in the cloud but wanting to
retrieve only part of the data inside it — refer to the short tutorial below.

## Short tutorial

Suppose a directory contains the following files:

```shell
input_dir/config/device/file2.txt
input_dir/config/device/file3.txt
input_dir/config/network/file4.txt
input_dir/log/file0.txt
input_dir/log/file1.txt
input_dir/media/sensor/1/file7.bin
input_dir/media/sensor/1/file8.bin
input_dir/media/sensor/1/file9.bin
input_dir/resource/bin/file5.bin
input_dir/resource/bin/file6.bin
```

### Packing

```shell
> ctar pack input_dir pack_file.ctar
file count: 10, total bytes: 630, compressed bytes: 190, padding bytes: 20290, elapsed time 10 ms
compressed ratio: 30.16%, Throughput: 61.5K/s
```

- Packs the specified files under the specified directory using multiple threads
- `file count`: the total number of files packed
- `total bytes`: the total number of bytes of the original files in the directory
- `compressed bytes`: the total number of bytes of the compressed files in the directory
- `elapsed time`: total elapsed time; the unit is always milliseconds
- `compressed ratio`: compression ratio, computed as `compressed bytes` / `total bytes`
- `Throughput`: throughput, computed as `total bytes` / `elapsed time`

### Unpacking

```shell
> ctar unpack pack_file.ctar output_dir
```

- Unpacks using multiple threads, restoring the entire packed directory into `output_dir`

### Viewing the directory tree

```shell
> ctar tree pack_file.ctar
input_dir
├── config
│   ├── device
│   │   ├── file2.txt
│   │   └── file3.txt
│   └── network
│       └── file4.txt
├── log
│   ├── file0.txt
│   └── file1.txt
├── media
│   └── sensor
│       └── 1
│           ├── file7.bin
│           ├── file8.bin
│           └── file9.bin
└── resource
    └── bin
        ├── file5.bin
        └── file6.bin
```

### Viewing file metadata

```shell
> ctar ls pack_file.ctar
-rw-r--r--  odyssey  staff  63  Oct 04 22:27  input_dir/config/device/file2.txt
-rw-r--r--  odyssey  staff  63  Oct 04 22:27  input_dir/config/device/file3.txt
-rw-r--r--  odyssey  staff  63  Oct 04 22:27  input_dir/config/network/file4.txt
-rw-r--r--  odyssey  staff  63  Oct 04 22:27  input_dir/log/file0.txt
-rw-r--r--  odyssey  staff  63  Oct 04 22:27  input_dir/log/file1.txt
-rw-r--r--  odyssey  staff  63  Oct 04 22:27  input_dir/media/sensor/1/file7.bin
-rw-r--r--  odyssey  staff  63  Oct 04 22:27  input_dir/media/sensor/1/file8.bin
-rw-r--r--  odyssey  staff  63  Oct 04 22:27  input_dir/media/sensor/1/file9.bin
-rw-r--r--  odyssey  staff  63  Oct 04 22:27  input_dir/resource/bin/file5.bin
-rw-r--r--  odyssey  staff  63  Oct 04 22:27  input_dir/resource/bin/file6.bin
```

- Column 1: permission bits
- Column 2: owner, the file's owning user
- Column 3: group, the file's owning group
- Column 4: file size
- Column 5: file last modification time
- Column 6: file path

#### Viewing file metadata for a specified directory

```shell
> ctar ls pack_file.ctar input_dir/log
-rw-r--r--  odyssey  staff  63  Oct 04 22:27  input_dir/log/file0.txt
-rw-r--r--  odyssey  staff  63  Oct 04 22:27  input_dir/log/file1.txt
```

### Viewing file metadata and file index information

```shell
> ctar dump -H pack_file.ctar
block_id	dir_id	file_id	permissions	owner	group	modify_time	offset	original_size	compressed_size	dir_name	file_name
0	3	1	420	odyssey	staff	1791124062503	20480	63	19	input_dir/config/device	file2.txt
0	3	2	420	odyssey	staff	1791124062504	20499	63	19	input_dir/config/device	file3.txt
1	4	3	420	odyssey	staff	1791124062504	16384	63	19	input_dir/config/network	file4.txt
2	5	4	420	odyssey	staff	1791124062503	12288	63	19	input_dir/log	file0.txt
2	5	5	420	odyssey	staff	1791124062503	12307	63	19	input_dir/log	file1.txt
3	8	6	420	odyssey	staff	1791124062504	24576	63	19	input_dir/media/sensor/1	file7.bin
3	8	7	420	odyssey	staff	1791124062504	24595	63	19	input_dir/media/sensor/1	file8.bin
3	8	8	420	odyssey	staff	1791124062504	24614	63	19	input_dir/media/sensor/1	file9.bin
4	10	9	420	odyssey	staff	1791124062504	8192	63	19	input_dir/resource/bin	file5.bin
4	10	10	420	odyssey	staff	1791124062504	8211	63	19	input_dir/resource/bin	file6.bin
```

- Column 1: the id of the block the file belongs to. During packing, files are distributed into different blocks according to how they are laid out across directories; block ids start at 0 and increase
- Column 2: the id of the directory the file belongs to. When scanning the packed directory, each directory is assigned an id, starting at 1 and increasing
- Column 3: the file id. When scanning the packed directory, each file is assigned an id, starting at 1 and increasing
- Column 4: the file's permission value, using the Unix file permission model
- Column 5: owner, the file's owning user
- Column 6: group, the file's owning group
- Column 7: the file's last modification timestamp
- Column 8: the file's index relative to the whole pack file, usable for reading a single file
- Column 9: original file size
- Column 10: compressed file size
- Column 11: directory name
- Column 12: file name

Note that rows are sorted by `block_id`, which is not necessarily the physical order of the blocks within the
pack file.

#### Notes

1. The start position and size of an entire block can be worked out from all the files sharing a `block_id`, so the
   whole block can be read in one shot. If the pack file is stored in the cloud, this approach speeds up file retrieval
2. The default compression algorithm is `lz4`, which favors faster processing speed. If a higher compression ratio is
   desired, pass `ctar pack -c lz4hc` to use `lz4hc` and tune the level with `-p`
3. When compressing files, commonly seen already-compressed files such as `parquet` and `jpeg` are left uncompressed

#### Viewing metadata and index information for files in a specified directory

```shell
> ctar dump -H pack_file.ctar input_dir/log
block_id	dir_id	file_id	permissions	owner	group	modify_time	offset	original_size	compressed_size	dir_name	file_name
2	5	4	420	odyssey	staff	1791124062503	12288	63	19	input_dir/log	file0.txt
2	5	5	420	odyssey	staff	1791124062503	12307	63	19	input_dir/log	file1.txt
```

### Writing dump output to a file

```shell
> ctar dump -o pack_file_meta.tsv pack_file.ctar
> cat pack_file_meta.tsv
0	3	1	420	odyssey	staff	1791124062503	20480	63	19	input_dir/config/device	file2.txt
0	3	2	420	odyssey	staff	1791124062504	20499	63	19	input_dir/config/device	file3.txt
1	4	3	420	odyssey	staff	1791124062504	16384	63	19	input_dir/config/network	file4.txt
2	5	4	420	odyssey	staff	1791124062503	12288	63	19	input_dir/log	file0.txt
2	5	5	420	odyssey	staff	1791124062503	12307	63	19	input_dir/log	file1.txt
3	8	6	420	odyssey	staff	1791124062504	24576	63	19	input_dir/media/sensor/1	file7.bin
3	8	7	420	odyssey	staff	1791124062504	24595	63	19	input_dir/media/sensor/1	file8.bin
3	8	8	420	odyssey	staff	1791124062504	24614	63	19	input_dir/media/sensor/1	file9.bin
4	10	9	420	odyssey	staff	1791124062504	8192	63	19	input_dir/resource/bin	file5.bin
4	10	10	420	odyssey	staff	1791124062504	8211	63	19	input_dir/resource/bin	file6.bin
```

## File format design

If you want to understand the design of the pack file format, see [File format design](./doc/file_format.md).

## Building from source

| Dependency | Requirement |
|---|---|
| CMake | **3.20** or newer |
| Compiler | **GCC 14+** or **Clang 18+** |
| git | any recent version |

```shell
git clone -b main --single-branch --depth 1 https://github.com/blocksmith314/ctar.git
cd ctar
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
cmake --install . # --prefix ./user_defined_install_path
```

**Not supported**: Windows, Intel Macs, arm64 Linux, systems with glibc older than 2.28 (such as CentOS 7), and
musl-based distributions such as Alpine.

## License

`ctar` is released under the Apache License (Version 2.0). Some of the third-party components included in `ctar` may
be covered by other open-source licenses. See the NOTICE file for details.
