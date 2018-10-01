#ifndef OKTUN_H
#define OKTUN_H

#define OKTUN_BEGIN_NAMESPACE namespace oktun {
#define OKTUN_END_NAMESPACE }

#ifndef __FILENAME__

#define __FILENAME__ \
    (__builtin_strrchr(__FILE__, '/') ? \
     __builtin_strrchr(__FILE__, '/') + 1 : __FILE__)

#endif

#ifdef NDEBUG //no debug

#define DLOG(fmt,...)

#else

#define DLOG(fmt,...) \
{ \
    printf("%s:%d | %s | " fmt "\n", \
           __FILENAME__, __LINE__, __func__, ##__VA_ARGS__); \
    fflush(stdout); \
}

#endif //EOF NDEBUG

#endif //EOF OKTUN_H
