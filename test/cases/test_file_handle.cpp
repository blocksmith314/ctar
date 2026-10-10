
#include <filesystem>
#include <string>
#include <string_view>
#include <cstring>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "gtest/gtest.h"
#include "src/core/file_handle.h"
#include "src/utils/status.h"

namespace fs = std::filesystem;
namespace ctar::test
{

    static constexpr std::string_view kTestFile = "./tmp_file_handle_test.ctar";

    class PosixFileHandleTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            if (fs::exists(kTestFile))
            {
                fs::remove(kTestFile);
            }
        }
        void TearDown() override
        {
            if (fs::exists(kTestFile))
            {
                fs::remove(kTestFile);
            }
        }
    };

    TEST_F(PosixFileHandleTest, FileNotExist)
    {
        auto res = ctar::NewPosixReadFile(std::string(kTestFile));
        ASSERT_FALSE(res);
    }

    TEST_F(PosixFileHandleTest, WriteAndSequentialRead)
    {
        auto w_res = ctar::NewPosixWriteFile(std::string(kTestFile), true);
        ASSERT_TRUE(w_res);
        auto& wf = w_res.value();

        std::string src_data = "abcdef0123456789";
        Status st = wf->Write(src_data.size(), src_data.data());
        ASSERT_TRUE(st);

        wf.reset();

        auto r_res = ctar::NewPosixReadFile(std::string(kTestFile));
        ASSERT_TRUE(r_res);
        auto& rf = r_res.value();

        char buf[64]{};
        auto read_ret = rf->Read(src_data.size(), buf);
        ASSERT_TRUE(read_ret);
        ASSERT_EQ(read_ret.value(), src_data.size());
        ASSERT_STREQ(buf, src_data.c_str());

        auto eof_ret = rf->Read(10, buf);
        ASSERT_TRUE(eof_ret);
        ASSERT_EQ(eof_ret.value(), 0U);
    }

    TEST_F(PosixFileHandleTest, RandomReadNoOffsetAdvance)
    {
        auto w_res = ctar::NewPosixWriteFile(std::string(kTestFile), true);
        ASSERT_TRUE(w_res);
        constexpr std::string_view data = "0123456789ABCDEF";
        ASSERT_TRUE(w_res.value()->Write(data.size(), data.data()));
        w_res.value().reset();

        auto r_res = ctar::NewPosixReadFile(std::string(kTestFile));
        ASSERT_TRUE(r_res);
        auto& rf = r_res.value();

        char tmp[8]{};
        auto rnd = rf->RandomRead(10U, 5U, tmp);
        ASSERT_TRUE(rnd);
        ASSERT_EQ(rnd.value(), 5U);
        ASSERT_EQ(std::string_view(tmp, 5), "ABCDE");

        rnd = rf->RandomRead(5U, 5U, tmp);
        ASSERT_TRUE(rnd);
        ASSERT_EQ(rnd.value(), 5U);
        ASSERT_EQ(std::string_view(tmp, 5), "56789");

        char seq_buf[4]{};
        auto seq = rf->Read(4, seq_buf);
        ASSERT_TRUE(seq);
        ASSERT_EQ(seq.value(), 4U);
        ASSERT_EQ(std::string_view(seq_buf, 4), "0123");
    }


    TEST_F(PosixFileHandleTest, ReadWithOffsetAdvance)
    {
        auto w_res = ctar::NewPosixWriteFile(std::string(kTestFile), true);
        ASSERT_TRUE(w_res);
        constexpr std::string_view data = "0123456789ABCDEF";
        ASSERT_TRUE(w_res.value()->Write(data.size(), data.data()));
        w_res.value().reset();

        auto r_res = ctar::NewPosixReadFile(std::string(kTestFile));
        ASSERT_TRUE(r_res);
        auto& rf = r_res.value();

        char seq_buf[4]{};
        auto seq = rf->Read(4, seq_buf);
        ASSERT_TRUE(seq);
        ASSERT_EQ(seq.value(), 4U);
        ASSERT_EQ(std::string_view(seq_buf, 4), "0123");

        seq = rf->Read(4, seq_buf);
        ASSERT_TRUE(seq);
        ASSERT_EQ(seq.value(), 4U);
        ASSERT_EQ(std::string_view(seq_buf, 4), "4567");
    }

    TEST_F(PosixFileHandleTest, RandomWrite)
    {
        auto w_res = ctar::NewPosixWriteFile(std::string(kTestFile), true);
        auto& wf = w_res.value();

        std::string base(32, '-');
        ASSERT_TRUE(wf->Write(base.size(), base.data()));

        constexpr std::string_view s = "HELLO";
        ASSERT_TRUE(wf->RandomWrite(8U, s.size(), s.data()));
        wf.reset();

        auto r_res = ctar::NewPosixReadFile(std::string(kTestFile));
        char buf[32]{};
        auto rd = r_res.value()->Read(32, buf);
        ASSERT_TRUE(rd);
        ASSERT_EQ(std::string_view(buf, 8), "--------");
        ASSERT_EQ(std::string_view(buf + 8, s.size()), s);
    }

    TEST_F(PosixFileHandleTest, WriteFileWithTruncateTrue)
    {
        auto w_res = ctar::NewPosixWriteFile(std::string(kTestFile), true);
        auto& wf = w_res.value();

        std::string data = "1234567890ABCDEF";
        ASSERT_TRUE(wf->Write(data.size(), data.data()));
        ASSERT_TRUE(wf->TruncFile(6U));
        wf.reset();

        auto r_res = ctar::NewPosixReadFile(std::string(kTestFile));
        char buf[16]{};
        auto rd = r_res.value()->Read(16, buf);
        ASSERT_TRUE(rd);
        ASSERT_EQ(rd.value(), 6U);
        ASSERT_EQ(std::string_view(buf, 6), "123456");
    }

    TEST_F(PosixFileHandleTest, WriteFileWithTruncateFalse)
    {
        {
            auto w1 = ctar::NewPosixWriteFile(std::string(kTestFile), true);
            ASSERT_TRUE(w1);
            std::string old = "OLD_CONTENT";
            ASSERT_TRUE(w1.value()->Write(old.size(), old.data()));
        }
        {
            auto w2 = ctar::NewPosixWriteFile(std::string(kTestFile), false);
            ASSERT_TRUE(w2);
            std::string new_head = "NEW_";
            ASSERT_TRUE(w2.value()->Write(new_head.size(), new_head.data()));
        }

        auto r = ctar::NewPosixReadFile(std::string(kTestFile));
        ASSERT_TRUE(r);
        char buf[32]{};
        auto rd = r.value()->Read(32, buf);
        ASSERT_TRUE(rd);
        ASSERT_EQ(std::string_view(buf, rd.value()), "NEW_CONTENT");
    }

    TEST_F(PosixFileHandleTest, Sync)
    {
        auto w_res = ctar::NewPosixWriteFile(std::string(kTestFile), true);
        ASSERT_TRUE(w_res);
        auto st = w_res.value()->Sync();
        (void)st;
    }

    TEST_F(PosixFileHandleTest, ReadNullptrBuffer)
    {
        auto w = ctar::NewPosixWriteFile(std::string(kTestFile), true);
        constexpr std::string_view s = "abcd";
        ASSERT_TRUE(w.value()->Write(s.size(), s.data()));

        auto r_res = ctar::NewPosixReadFile(std::string(kTestFile));
        ASSERT_TRUE(r_res);
        char buf[4]{};
        auto rd = r_res.value()->Read(s.size(), buf);
        ASSERT_TRUE(rd);
        ASSERT_EQ(std::string_view(buf, rd.value()), s);
        auto ret = r_res.value()->Read(s.size(), nullptr);
        ASSERT_FALSE(ret);
        auto rnd_ret = r_res.value()->RandomRead(0, 4, nullptr);
        ASSERT_FALSE(rnd_ret);
    }


    TEST_F(PosixFileHandleTest, RandomWriteVReadVNormalWithoutOffset)
    {
        char bufA[64];
        char bufB[128];
        char bufC[32];
        std::memset(bufA, 'A', sizeof(bufA));
        std::memset(bufB, 'B', sizeof(bufB));
        std::memset(bufC, 'C', sizeof(bufC));

        // 打开写，truncate
        auto wRes = ctar::NewPosixWriteFile(std::string(kTestFile), true);
        ASSERT_TRUE(wRes);
        auto& wFile = wRes.value();

        struct iovec iovWrite[3]{};
        iovWrite[0].iov_base = bufA;
        iovWrite[0].iov_len = sizeof(bufA);
        iovWrite[1].iov_base = bufB;
        iovWrite[1].iov_len = sizeof(bufB);
        iovWrite[2].iov_base = bufC;
        iovWrite[2].iov_len = sizeof(bufC);

        auto st = wFile->RandomWriteV(iovWrite, 3, 0);
        ASSERT_TRUE(st);
        ASSERT_TRUE(wFile->Sync());

        auto rRes = ctar::NewPosixReadFile(std::string(kTestFile));
        ASSERT_TRUE(rRes);
        auto& rFile = rRes.value();

        char rA[64]{0};
        char rB[128]{0};
        char rC[32]{0};
        struct iovec iovRead[3]{};
        iovRead[0].iov_base = rA;
        iovRead[0].iov_len = sizeof(rA);
        iovRead[1].iov_base = rB;
        iovRead[1].iov_len = sizeof(rB);
        iovRead[2].iov_base = rC;
        iovRead[2].iov_len = sizeof(rC);

        auto readRet = rFile->RandomReadV(iovRead, 3, 0);
        ASSERT_TRUE(readRet);
        size_t totalExpect = sizeof(bufA) + sizeof(bufB) + sizeof(bufC);
        ASSERT_EQ(readRet.value(), totalExpect);

        ASSERT_EQ(std::memcmp(rA, bufA, sizeof(rA)), 0);
        ASSERT_EQ(std::memcmp(rB, bufB, sizeof(rB)), 0);
        ASSERT_EQ(std::memcmp(rC, bufC, sizeof(rC)), 0);
    }

    TEST_F(PosixFileHandleTest, RandomWriteVReadVWithOffset)
    {
        auto wRes = ctar::NewPosixWriteFile(std::string(kTestFile), true);
        ASSERT_TRUE(wRes);
        auto& wFile = wRes.value();

        char fill[100];
        std::memset(fill, 'X', sizeof(fill));
        ASSERT_TRUE(wFile->RandomWrite(0, sizeof(fill), fill));

        char buf1[32];
        char buf2[64];
        std::memset(buf1, '1', sizeof(buf1));
        std::memset(buf2, '2', sizeof(buf2));
        struct iovec iovW[2]{};
        iovW[0].iov_base = buf1;
        iovW[0].iov_len = sizeof(buf1);
        iovW[1].iov_base = buf2;
        iovW[1].iov_len = sizeof(buf2);
        ASSERT_TRUE(wFile->RandomWriteV(iovW, 2, 100));
        ASSERT_TRUE(wFile->Sync());

        auto rRes = ctar::NewPosixReadFile(std::string(kTestFile));
        ASSERT_TRUE(rRes);
        auto& rFile = rRes.value();

        char r1[32]{0};
        char r2[64]{0};
        struct iovec iovR[2]{};
        iovR[0].iov_base = r1;
        iovR[0].iov_len = sizeof(r1);
        iovR[1].iov_base = r2;
        iovR[1].iov_len = sizeof(r2);

        auto ret = rFile->RandomReadV(iovR, 2, 100);
        ASSERT_TRUE(ret);
        ASSERT_EQ(ret.value(), 32 + 64);
        ASSERT_EQ(std::memcmp(r1, buf1, 32), 0);
        ASSERT_EQ(std::memcmp(r2, buf2, 64), 0);
    }

    TEST_F(PosixFileHandleTest, RandomReadVEOF)
    {
        auto wRes = ctar::NewPosixWriteFile(std::string(kTestFile), true);
        ASSERT_TRUE(wRes);
        char tmp[50];
        std::memset(tmp, 'Z', sizeof(tmp));
        auto& wFile = wRes.value();
        ASSERT_TRUE(wFile->RandomWrite(0, sizeof(tmp), tmp));
        ASSERT_TRUE(wFile->Sync());

        auto rRes = ctar::NewPosixReadFile(std::string(kTestFile));
        ASSERT_TRUE(rRes);
        auto& rFile = rRes.value();

        char b1[40]{0};
        char b2[60]{0};
        struct iovec iov[2]{};
        iov[0].iov_base = b1;
        iov[0].iov_len = sizeof(b1);
        iov[1].iov_base = b2;
        iov[1].iov_len = sizeof(b2);

        auto res = rFile->RandomReadV(iov, 2, 0);
        ASSERT_TRUE(res);
        // 文件只有50字节，读到50返回，不是100
        ASSERT_EQ(res.value(), 50);
    }

    TEST_F(PosixFileHandleTest, RandomWriteVReadVBadArgument)
    {
        auto wRes = ctar::NewPosixWriteFile(std::string(kTestFile), true);
        ASSERT_TRUE(wRes);
        auto& wFile = wRes.value();

        // nullptr iov
        auto st1 = wFile->RandomWriteV(nullptr, 2, 0);
        ASSERT_FALSE(st1);

        // iov_cnt <= 0
        char dummy[16];
        struct iovec iov{};
        iov.iov_base = dummy;
        iov.iov_len = sizeof(dummy);
        auto st2 = wFile->RandomWriteV(&iov, 0, 0);
        ASSERT_FALSE(st2);

        // read side
        ASSERT_TRUE(wFile->Sync());
        auto rRes = ctar::NewPosixReadFile(std::string(kTestFile));
        ASSERT_TRUE(rRes);
        auto& rFile = rRes.value();
        auto r1 = rFile->RandomReadV(nullptr, 1, 0);
        ASSERT_FALSE(r1);
        auto r2 = rFile->RandomReadV(&iov, -1, 0);
        ASSERT_FALSE(r2);
    }

    // A block whose files are all empty reaches the writer with a single zero-length
    // iovec. That is a no-op for writev(), not an error.
    TEST_F(PosixFileHandleTest, RandomWriteVZeroLengthIsNoOp)
    {
        constexpr std::string_view seed = "keep-the-existing-content";
        {
            auto w_res = ctar::NewPosixWriteFile(std::string(kTestFile), true);
            ASSERT_TRUE(w_res);
            ASSERT_TRUE(w_res.value()->Write(seed.size(), seed.data()));
            ASSERT_TRUE(w_res.value()->Sync());
        }

        std::string empty;
        struct iovec iov[2]{};
        iov[0].iov_base = empty.data();
        iov[0].iov_len = 0;
        iov[1].iov_base = empty.data();
        iov[1].iov_len = 0;

        auto w_res = ctar::NewPosixWriteFile(std::string(kTestFile));
        ASSERT_TRUE(w_res);
        ASSERT_TRUE(w_res.value()->RandomWriteV(iov, 2, 0));
        ASSERT_TRUE(w_res.value()->Sync());

        auto r_res = ctar::NewPosixReadFile(std::string(kTestFile));
        ASSERT_TRUE(r_res);
        auto size = r_res.value()->GetFileSize();
        ASSERT_TRUE(size);
        ASSERT_EQ(size.value(), seed.size());
    }

    // IOV_MAX is 1024 on Linux and macOS; a longer list has to be sent in several calls.
    TEST_F(PosixFileHandleTest, RandomWriteVAndReadVAboveIovMax)
    {
        constexpr size_t kChunk = 4;
        constexpr size_t kCount = 1100; // above IOV_MAX
        std::string pattern(kChunk, 'Z');

        std::vector<struct iovec> iovs(kCount);
        for (auto& iv : iovs)
        {
            iv.iov_base = pattern.data();
            iv.iov_len = kChunk;
        }

        auto w_res = ctar::NewPosixWriteFile(std::string(kTestFile), true);
        ASSERT_TRUE(w_res);
        ASSERT_TRUE(w_res.value()->RandomWriteV(iovs.data(), static_cast<int>(iovs.size()), 0));
        ASSERT_TRUE(w_res.value()->Sync());

        auto r_res = ctar::NewPosixReadFile(std::string(kTestFile));
        ASSERT_TRUE(r_res);
        auto size = r_res.value()->GetFileSize();
        ASSERT_TRUE(size);
        ASSERT_EQ(size.value(), kChunk * kCount);

        std::string read_back(kChunk * kCount, '\0');
        for (size_t i = 0; i < kCount; ++i)
        {
            iovs[i].iov_base = read_back.data() + i * kChunk;
            iovs[i].iov_len = kChunk;
        }
        auto rd = r_res.value()->RandomReadV(iovs.data(), static_cast<int>(iovs.size()), 0);
        ASSERT_TRUE(rd);
        ASSERT_EQ(rd.value(), kChunk * kCount);
        ASSERT_EQ(read_back, std::string(kChunk * kCount, 'Z'));
    }

    // The syscall limits are parameters so the splitting loop can be driven with small
    // buffers instead of allocating gigabytes.
    TEST_F(PosixFileHandleTest, PwritevAllSplitsByBytesAndIovCount)
    {
        constexpr size_t kChunk = 512;
        constexpr size_t kCount = 8;
        std::string pattern(kChunk, '\0');
        for (size_t i = 0; i < kChunk; ++i)
        {
            pattern[i] = static_cast<char>('a' + i % 26);
        }

        std::vector<struct iovec> iovs(kCount);
        for (auto& iv : iovs)
        {
            iv.iov_base = pattern.data();
            iv.iov_len = kChunk;
        }

        const std::string file_name(kTestFile);
        const int fd = ::open(file_name.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(fd, 0);
        // At most 3 entries and 1024 bytes per call: 8 x 512 bytes needs 4 calls.
        Status st = ctar::detail::PwritevAll(fd, file_name, iovs, 0, 1024, 3);
        ASSERT_TRUE(st) << st.error().message();
        ASSERT_EQ(::close(fd), 0);

        std::string expected;
        for (size_t i = 0; i < kCount; ++i)
        {
            expected += pattern;
        }

        auto r_res = ctar::NewPosixReadFile(file_name);
        ASSERT_TRUE(r_res);
        std::string actual(expected.size(), '\0');
        auto rd = r_res.value()->RandomRead(0, actual.size(), actual.data());
        ASSERT_TRUE(rd);
        ASSERT_EQ(rd.value(), expected.size());
        ASSERT_EQ(actual, expected);
    }

    TEST_F(PosixFileHandleTest, PwritevAllSlicesSingleOversizedEntry)
    {
        constexpr size_t kTotal = 4096;
        std::string payload(kTotal, 'q');
        size_t offset = 0;
        for (char& c : payload)
        {
            c = static_cast<char>('0' + offset++ % 10);
        }

        std::vector<struct iovec> iovs(1);
        iovs[0].iov_base = payload.data();
        iovs[0].iov_len = payload.size();

        const std::string file_name(kTestFile);
        const int fd = ::open(file_name.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(fd, 0);
        // A single 4096-byte iovec with a 512-byte per-call budget must be sliced.
        Status st = ctar::detail::PwritevAll(fd, file_name, iovs, 0, 512, 1024);
        ASSERT_TRUE(st) << st.error().message();
        ASSERT_EQ(::close(fd), 0);

        auto r_res = ctar::NewPosixReadFile(file_name);
        ASSERT_TRUE(r_res);
        std::string actual(kTotal, '\0');
        auto rd = r_res.value()->RandomRead(0, actual.size(), actual.data());
        ASSERT_TRUE(rd);
        ASSERT_EQ(rd.value(), kTotal);
        ASSERT_EQ(actual, payload);
    }

    TEST_F(PosixFileHandleTest, PreadvAllSplitsByBytes)
    {
        constexpr size_t kTotal = 4096;
        std::string payload(kTotal, 'r');
        size_t offset = 0;
        for (char& c : payload)
        {
            c = static_cast<char>('A' + offset++ % 26);
        }

        const std::string file_name(kTestFile);
        {
            auto w_res = ctar::NewPosixWriteFile(file_name, true);
            ASSERT_TRUE(w_res);
            ASSERT_TRUE(w_res.value()->Write(payload.size(), payload.data()));
        }

        std::string actual(kTotal, '\0');
        std::vector<struct iovec> iovs(4);
        for (size_t i = 0; i < iovs.size(); ++i)
        {
            iovs[i].iov_base = actual.data() + i * 1024;
            iovs[i].iov_len = 1024;
        }

        const int fd = ::open(file_name.c_str(), O_RDONLY);
        ASSERT_GE(fd, 0);
        auto rd = ctar::detail::PreadvAll(fd, file_name, iovs, 0, 700, 2);
        ASSERT_TRUE(rd) << rd.error().message();
        ASSERT_EQ(rd.value(), kTotal);
        ASSERT_EQ(::close(fd), 0);
        ASSERT_EQ(actual, payload);
    }

    TEST_F(PosixFileHandleTest, ReadAllSpansMultipleSyscallsAndStopsAtEof)
    {
        constexpr size_t kTotal = 4096;
        std::string payload(kTotal, 'k');

        const std::string file_name(kTestFile);
        {
            auto w_res = ctar::NewPosixWriteFile(file_name, true);
            ASSERT_TRUE(w_res);
            ASSERT_TRUE(w_res.value()->Write(payload.size(), payload.data()));
        }

        const int fd = ::open(file_name.c_str(), O_RDONLY);
        ASSERT_GE(fd, 0);

        std::string actual(kTotal, '\0');
        auto positioned = ctar::detail::ReadAll(fd, file_name, 0, true, kTotal, actual.data(), 1000);
        ASSERT_TRUE(positioned) << positioned.error().message();
        ASSERT_EQ(positioned.value(), kTotal);
        ASSERT_EQ(actual, payload);

        std::string sequential(kTotal, '\0');
        auto seq = ctar::detail::ReadAll(fd, file_name, 0, false, kTotal, sequential.data(), 1000);
        ASSERT_TRUE(seq) << seq.error().message();
        ASSERT_EQ(seq.value(), kTotal);
        ASSERT_EQ(sequential, payload);

        // Past EOF the loop must return a short count rather than spin.
        char tail[16]{};
        auto eof = ctar::detail::ReadAll(fd, file_name, kTotal, true, sizeof(tail), tail, 1000);
        ASSERT_TRUE(eof);
        ASSERT_EQ(eof.value(), 0U);

        ASSERT_EQ(::close(fd), 0);
    }

} // namespace ctar::test

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}