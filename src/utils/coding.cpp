#include "coding.h"
namespace ctar
{

    void PutFixed32(std::string* dst, uint32_t value)
    {
        char buf[sizeof(value)];
        EncodeFixed32(buf, value);
        dst->append(buf, sizeof(buf));
    }

    void PutFixed64(std::string* dst, uint64_t value)
    {
        char buf[sizeof(value)];
        EncodeFixed64(buf, value);
        dst->append(buf, sizeof(buf));
    }
} // namespace ctar