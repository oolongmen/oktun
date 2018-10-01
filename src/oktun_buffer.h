#ifndef OKTUN_BUFFER_H
#define OKTUN_BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <vector>

#include "oktun.h"

OKTUN_BEGIN_NAMESPACE

struct Buffer 
{
public:
    Buffer(size_t size = 8192);
          
    size_t Size() const;

    char* Data(size_t pos = 0);

    const char* Data(size_t pos = 0) const;

    //first pos of used buffer
    char* Head();
    const char* Head() const;

    //first pos of unused buffer
    char* Tail();
    const char* Tail() const;

    //is buffer full
    bool Full() const;

    //is buffer empty
    bool Empty() const;

    //num of unused data
    size_t Unused() const;

    //num of used data
    size_t Used() const;

    //mark unused data after Tail() used
    virtual size_t Commit(size_t n);

    //remove used data after Head() unused
    virtual size_t Remove(size_t n);

    //resize buffer
    virtual size_t Resize(size_t n);

    //grow
    virtual size_t Grow(size_t n);

    //clear
    virtual void Clear();

private:
    size_t m_written;
    std::vector<char> m_data;
};

OKTUN_END_NAMESPACE

#endif
