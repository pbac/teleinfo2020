//#define DEBUG
#ifdef DEBUG
#define isDebug     1
#define debug(x)    fileLog.print(x)
#define debugln(x)  fileLog.println(x)
#else
#define isDebug     0
#define debug(x)    {}
#define debugln(x)  {}
#endif

