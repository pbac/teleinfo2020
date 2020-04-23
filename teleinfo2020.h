
#define VERSION "1.1.4"

#ifdef DEBUG
#define isDebug     1
#define debug(x)    fileLog.print(x)
#define debugln(x)  fileLog.println(x)
#else
#define isDebug     0
#define debug(x)    {}
#define debugln(x)  {}
#endif

#define LED_BUILTIN 16

#define TINFO_STX 0x02
#define TINFO_ETX 0x03 
#define TINFO_SGR 0x0A //'\n' // start of group  
#define TINFO_EGR 0x0D //'\r' // End of group    
#define TINFO_SEP 0x09          // Separator (tab)

#define PIN_LATCH  12
#define PIN_CLOCK  4
#define PIN_DATA   14
#define PIN_DRIVE1 5
#define PIN_DRIVE2 0

#define TMSG_SIZE_BUFFER     256
#define TMSG_SIZE_LABEL      10
#define TMSG_SIZE_TIMESTAMP  14
#define TMSG_SIZE_VALUE      150

#define TMSG_CACHE_SIZE 33
#define TMSG_CACHE_LABEL "ADSC,EASD01,EASD02,EASD03,EASD04,EASF01,EASF02,EASF03,EASF04,EASF05,EASF06,EASF07,EASF08,EASF09,EASF10,IRMS1,LTARF,MSG1,NGTF,NJOURF,NTARF,PCOUP,PREF,PRM,RELAIS,STGE,URMS1,VTIC,CCASN,CCASN-1,SMAXSN,SMAXSN-1,UMOY1,"   

class TMsg
{
  public:
    char    label[TMSG_SIZE_LABEL + 1];
    char    value[TMSG_SIZE_TIMESTAMP + TMSG_SIZE_VALUE + 1];
    short   cacheHit;
};
