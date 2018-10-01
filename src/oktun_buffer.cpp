#include <stdio.h>
#include <string.h>

#include "oktun_buffer.h"

OKTUN_BEGIN_NAMESPACE

Buffer::Buffer(size_t size)
    : m_written(0),
      m_data(size)
{
}
      
size_t Buffer::Size() const
{
    return m_data.size();
}

char* Buffer::Data(size_t pos)
{
    if (pos >= Size())
        return NULL;
    return &m_data[pos];
}

const char* Buffer::Data(size_t pos) const 
{ 
    if (pos >= Size())
        return NULL; 
    return &m_data[pos];
}

char* Buffer::Head()
{
    return Data();
}

const char* Buffer::Head() const
{
    return Data();
}

char* Buffer::Tail()
{
    return Data(m_written);
}

const char* Buffer::Tail() const
{
    return Data(m_written);
} 

bool Buffer::Full() const
{
    return (m_written == Size());
}

bool Buffer::Empty() const
{
    return (m_written == 0);
}

size_t Buffer::Unused() const
{
    return (Size() - m_written);
}

size_t Buffer::Used() const
{
    return m_written;
};

size_t Buffer::Commit(size_t n)
{
    n = std::min(Unused(), n);

    if (!n)
        return 0;

    m_written += n;
    return n;
}

size_t Buffer::Remove(size_t n)
{
    n = std::min(m_written, n);

    if (!n)
        return 0;

    size_t remain = m_written - n;

    if (remain)
        memmove(Data(), Data() + n, remain);

    m_written -= n;
    return n;
}

size_t Buffer::Resize(size_t n)
{
    //used data
    size_t max = std::min(n, Used());

    Clear();
    m_data.resize(n);
    Commit(max);

    return n;
}

//grow
size_t Buffer::Grow(size_t n)
{
    return Resize(Size() + n);
}

//clear
void Buffer::Clear()
{
    m_written = 0;
}

OKTUN_END_NAMESPACE
