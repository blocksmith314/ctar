#include <gtest/gtest.h>
#include "src/utils/status.h"

namespace ctar
{

    TEST(ErrorStatusTest, DefaultConstructIsSuccess)
    {
        ErrorStatus st;
        EXPECT_TRUE(st.ok());
        EXPECT_EQ(st.type(), ErrorType::kSuccess);
        Status s;
        EXPECT_TRUE(s);
    }

    TEST(ErrorStatusTest, ConstructErrorFieldsCorrect)
    {
        ErrorStatus err(ErrorType::kIoError, "open failed", "main.cpp", 66);
        EXPECT_FALSE(err.ok());
        EXPECT_EQ(err.type(), ErrorType::kIoError);
        EXPECT_EQ(err.err_message, "open failed");
        EXPECT_EQ(err.source_file, "main.cpp");
        EXPECT_EQ(err.source_line, 66);
    }

    TEST(ErrorStatusTest, MessageFormatWork)
    {
        ErrorStatus err(ErrorType::kIoError, "open failed", "main.cpp", 66);
        std::string expect_str = "main.cpp:66 open failed";
        EXPECT_EQ(err.message(), expect_str);
        EXPECT_EQ(static_cast<std::string>(err), expect_str);
    }

    TEST(ErrorStatusTest, MakeErrorBuildUnexpected)
    {
        auto res = make_error(ErrorType::kInvalidArgument, "bad param", "test.cpp", 12);
        const auto& err = res.error();
        EXPECT_EQ(err.type(), ErrorType::kInvalidArgument);
        EXPECT_EQ(err.err_message, "bad param");
        EXPECT_EQ(err.source_file, "test.cpp");
        EXPECT_EQ(err.source_line, 12);
    }

    TEST(ErrorStatusTest, ErrorMacro_Io)
    {
        Status status = error_io("cannot open file {}", "data.bin");
        EXPECT_FALSE(status);
        EXPECT_EQ(status.error().type(), ErrorType::kIoError);
        EXPECT_NE(status.error().message().find("data.bin"), std::string::npos);
    }

    TEST(ErrorStatusTest, ErrorMacro_NotFound)
    {
        Status status = error_not_found("blob not found, id:{}", 5);
        EXPECT_FALSE(status);
        EXPECT_EQ(status.error().type(), ctar::ErrorType::kNotFound);
        EXPECT_NE(status.error().message().find('5'), std::string::npos);
    }

    TEST(ErrorStatusTest, ErrorMacro_Corruption)
    {
        Status status = error_corruption("file damaged");
        EXPECT_FALSE(status);
        EXPECT_EQ(status.error().type(), ctar::ErrorType::kCorruption);
        EXPECT_NE(status.error().message().find("file damaged"), std::string::npos);
    }

    TEST(ErrorStatusTest, ErrorMacro_NotSupported)
    {
        Status status = error_not_supported("this mode is unsupported");
        EXPECT_FALSE(status);
        EXPECT_EQ(status.error().type(), ErrorType::kNotSupported);
    }

    TEST(ErrorStatusTest, ErrorMacro_InvalidArg)
    {
        Status status = error_invalid_arg("size can not be zero");
        EXPECT_FALSE(status);
        EXPECT_EQ(status.error().type(), ErrorType::kInvalidArgument);
    }

    TEST(ErrorStatusTest, ErrorMacro_Runtime)
    {
        Status status = error_run_time("runtime exception");
        EXPECT_FALSE(status);
        EXPECT_EQ(status.error().type(), ErrorType::kRuntimeError);
    }

    TEST(ResultStatusTest, ReturnValueSuccess)
    {
        ResultStatus<std::uint64_t> res{1024U};
        EXPECT_TRUE(res);
        EXPECT_TRUE(res.has_value());
        EXPECT_EQ(res.value(), 1024U);
    }

    TEST(ResultStatusTest, ReturnErrorState)
    {
        ResultStatus<uint64_t> res = make_error(ErrorType::kIoError, "read failed", "x.cpp", 99);
        EXPECT_FALSE(res);
        EXPECT_FALSE(res.has_value());
        const auto& err = res.error();
        EXPECT_EQ(err.type(), ErrorType::kIoError);
        EXPECT_EQ(err.source_line, 99);

        EXPECT_THROW((void)res.value(), std::bad_expected_access<ErrorStatus>);
    }

    TEST(ErrorStatusTest, CopyConstructError)
    {
        ErrorStatus src(ErrorType::kRuntimeError, "test err", "a.cpp", 10);
        ErrorStatus dst = src;
        dst.err_message = "modified copy";
        EXPECT_EQ(dst.type(), src.type());
        EXPECT_NE(dst.err_message, src.err_message);
        EXPECT_EQ(dst.source_file, src.source_file);
        EXPECT_EQ(dst.source_line, src.source_line);
    }

    TEST(ErrorStatusTest, MoveConstructError)
    {
        ErrorStatus src(ErrorType::kIoError, "move-me", "b.cpp", 20);
        const ErrorStatus dst = std::move(src);
        EXPECT_EQ(dst.err_message, "move-me");
    }

    TEST(StatusTest, CopyAndMoveOk)
    {
        Status ok1;
        Status ok2 = ok1;
        EXPECT_TRUE(ok2);

        Status err1 = error_run_time("move test");
        Status err2 = std::move(err1);
        EXPECT_FALSE(err2);
    }

    TEST(ResultStatusTest, CopyMoveSemantic)
    {
        ResultStatus<int> val_ok{42};
        ResultStatus<int> val_copy = val_ok;
        EXPECT_EQ(val_copy.value(), 42);

        ResultStatus<int> val_move = std::move(val_ok);
        EXPECT_EQ(val_move.value(), 42);

        ResultStatus<int> err_src = make_error(ErrorType::kRuntimeError, "err", "x.cc", 1);
        ResultStatus<int> err_copy = err_src;
        EXPECT_FALSE(err_copy);

        ResultStatus<int> err_move = std::move(err_src);
        EXPECT_FALSE(err_move);
    }


    TEST(ErrorStatusTest, EmptyMessage)
    {
        ErrorStatus st(ErrorType::kInvalidArgument, "", "empty.cpp", 100);
        EXPECT_EQ(st.err_message, "");
        std::string out = st.message();
        EXPECT_NE(out.find("empty.cpp:100"), std::string::npos);
    }

} // namespace ctar

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
