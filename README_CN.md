中文 | [English](./README.md)

# ctar - 非常快速的打包工具

ctar（Cloud-era Tar）是一款受传统 tar 启发的现代归档工具，主要面向海量小文件的使用场景，充分利用多线程进行打包，相比传统 tar 有更快的打包速度，并针对云对象存储的使用场景进行了优化，特别适用于 AI、自动驾驶等需要处理海量多模态数据的行业。

## 核心功能

- **多线程打包与解包** —— 默认使用全部 CPU 核心，可以指定线程数，文件越多收益越明显
- **按文件随机读取** —— 归档内每个文件的偏移都记录在索引中，可以只读取单个文件或一批文件，无需下载或解压整个归档
- **面向云对象存储的布局** —— 同一目录的文件集中存放，便于按块拉取与并发读取
- **可选压缩** —— `lz4`（默认，偏向速度）、`lz4hc`（偏向压缩率）、`none`（不压缩）；`jpeg`、`png`、`parquet` 等已压缩格式自动跳过
- **可调的压缩参数** —— 可以控制 lz4 加速因子或 lz4hc 压缩级别
- **完整保留文件元数据** —— 权限、属主、属组、修改时间
- **归档与本地目录皆可查看** —— `tree` 打印目录树，`ls` 列出元信息，`dump` 导出可过滤的 TSV 索引

更多功能可用 `--help` 查看。

## 安装

### 预编译二进制

从 [Releases](https://github.com/blocksmith314/ctar/releases) 页面下载对应平台的产物：

| 平台 | 独立可执行文件 |
|---|---|
| Linux x86_64 | `ctar-<版本>-linux-x86_64` |
| macOS arm64 | `ctar-<版本>-macos-arm64` |

> **macOS 要求**：需要 macOS **13.3 及以上**，且必须为 Apple Silicon。

```shell
VERSION=v1.0.0
BASE=https://github.com/blocksmith314/ctar/releases/download/$VERSION

curl -sSL -O "$BASE/ctar-$VERSION-linux-x86_64"
curl -sSL -O "$BASE/SHA256SUMS"

# 校验下载完整性
sha256sum -c --ignore-missing SHA256SUMS

chmod +x ctar-$VERSION-linux-x86_64
sudo mv ctar-$VERSION-linux-x86_64 /usr/local/bin/ctar
ctar --help
```

> **macOS 用户注意**：浏览器下载的二进制会被 Gatekeeper 拦截，提示「Apple 无法验证……是否包含恶意软件」。这不是病毒告警，而是该产物未做 Apple 开发者签名与公证。清除隔离标记即可：
>
> ```shell
> xattr -d com.apple.quarantine ./ctar-$VERSION-macos-arm64
> ```
>
> 或者改用 `curl` 下载 —— 它不会写入隔离标记。

## 常用命令

```shell
# 打包文件
ctar pack input_dir pack_file.ctar

# 解包文件
ctar unpack pack_file.ctar output_dir

# 以目录树的形式查看已打包的目录
ctar tree pack_file.ctar

# 显示文件的元信息
ctar ls pack_file.ctar

# 显示文件元信息和索引信息，索引信息可以用来快速读取单个文件或者一批文件
ctar dump pack_file.ctar
```

一般情况下，使用 `pack` 和 `unpack` 命令即可满足大部分的需求，文件越多收益越明显。如果有其他需求，如将打包后的文件放在云端，但只想获取打包文件中的部分数据时，可参考下面的简短教程。

## 简短教程

假设一个目录下有如下文件：

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

### 打包

```shell
> ctar pack input_dir pack_file.ctar
file count: 10, total bytes: 630, compressed bytes: 190, padding bytes: 20290, elapsed time 10 ms
compressed ratio: 30.16%, Throughput: 61.5K/s
```

- 利用多线程将指定文件夹下的指定文件进行打包
- `file count` 打包的文件总数
- `total bytes` 目录下原始文件的字节总数
- `compressed bytes` 目录下压缩文件的字节总数
- `elapsed time` 总耗时，单位总是毫秒
- `compressed ratio` 压缩率，计算公式 `compressed bytes`/`total bytes`
- `Throughput` 吞吐，计算公式 `total bytes`/`elapsed time`

### 解包

```shell
> ctar unpack pack_file.ctar output_dir
```

- 利用多线程进行解包，会将打包的整个目录解压到 `output_dir` 中

### 查看目录树

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

### 查看文件元信息

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

- 第 1 列：权限位
- 第 2 列：owner，文件属主
- 第 3 列：group，文件属组
- 第 4 列：文件大小
- 第 5 列：文件最后修改时间
- 第 6 列：文件路径

#### 查看指定目录的文件元信息

```shell
> ctar ls pack_file.ctar input_dir/log
-rw-r--r--  odyssey  staff  63  Oct 04 22:27  input_dir/log/file0.txt
-rw-r--r--  odyssey  staff  63  Oct 04 22:27  input_dir/log/file1.txt
```

### 查看文件的元信息和文件的索引信息

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

- 第 1 列：文件所属 block 的 id，打包时会根据文件夹中文件的分布来将文件打包到不同的 block 中，block 的 id 从 0 开始自增
- 第 2 列：文件所属目录的 id，对打包文件夹进行扫描时，会为每个文件夹分配一个 id, 从 1 开始自增
- 第 3 列：文件的 id，对打包文件夹进行扫描时，会为每个文件分配一个 id, 从 1 开始自增
- 第 4 列：文件的权限值，Unix 文件权限模型
- 第 5 列：owner，文件属主
- 第 6 列：group，文件属组
- 第 7 列：文件最后修改的时间戳
- 第 8 列：文件相对于整个打包文件的索引，可用于读取单个文件
- 第 9 列：原始文件大小
- 第 10 列：压缩后的文件大小
- 第 11 列：目录名
- 第 12 列：文件名

注意：输出按 `block_id` 排序，与各 block 在打包文件中的物理顺序不一定一致。

#### 注意

1. 可以根据 block_id 的所有文件计算出整个 block 的开始位置和大小，从而一次性的读取整个 block，如果将打包文件存储在云端，这种方法可以加快获取文件的速度
2. 默认压缩算法为 `lz4`，偏向于更快的处理速度。如果希望更高的压缩率，可以用 `ctar pack -c lz4hc` 指定 `lz4hc`，并通过 `-p` 调整压缩级别
3. 压缩文件时，对于常见的已压缩的文件不会进行压缩，如 `parquet`、`jpeg` 等

#### 查看指定目录的文件的元信息和文件的索引信息

```shell
> ctar dump -H pack_file.ctar input_dir/log
block_id	dir_id	file_id	permissions	owner	group	modify_time	offset	original_size	compressed_size	dir_name	file_name
2	5	4	420	odyssey	staff	1791124062503	12288	63	19	input_dir/log	file0.txt
2	5	5	420	odyssey	staff	1791124062503	12307	63	19	input_dir/log	file1.txt
```

### 将 dump 结果写到文件中

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

## 文件格式设计

如果你想了解打包文件的格式设计，请参考[文件格式设计](./doc/file_format_CN.md)。

## 从源码构建

| 依赖 | 要求 |
|---|---|
| CMake | **3.20** 及以上 |
| 编译器 | **GCC 14+** 或 **Clang 18+** |
| git | 任意较新版本 |

```shell
git clone -b main --single-branch --depth 1 https://github.com/blocksmith314/ctar.git
cd ctar
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
cmake --install . # --prefix ./user_defined_install_path
```

**暂不支持**：Windows、Intel Mac、arm64 Linux、glibc 低于 2.28 的系统（如 CentOS 7），以及使用 musl 的系统（如 Alpine）。

## License

`ctar` 遵循 Apache License (Version 2.0) 开源许可证。`ctar` 包含的部分第三方组件可能遵循其它开源许可证。相关详细信息可以查看 NOTICE 文件。
