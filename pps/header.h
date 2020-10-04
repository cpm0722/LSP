#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <limits.h>
#include <ctype.h>
#include <dirent.h>
#include <pwd.h>
#include <utmp.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <ncurses.h>

#define BUFFER_SIZE 1024
#define PATH_LEN 1024
#define TOKEN_LEN 32
#define FNAME_LEN 128

#define MAX_TOKEN 22				// /proc/pid/stat에서 읽어들일 token 갯수

#define UNAME_LEN 32
#define TTY_LEN 32
#define STAT_LEN 8
#define TIME_LEN 16
#define CMD_LEN 1024

#define PID_MAX 32768				//pid 최대 갯수

#define PROCESS_MAX 4096

#define TICK_CNT 8					// cpu ticks 개수, us sy ni id wa hi si st로 총 8개

#define SYSTEMD "systemd-"			// system USER명의 앞부분
#define DEVNULL "/dev/null"			// 터미널 없을 시 가리키는 /dev/null 절대 경로
#define PTS "pts/"					// 터미널 문자열의 앞부분

#define PROC "/proc"				// /proc 절대 경로
#define LOADAVG "/proc/loadavg"		// /proc/loadavg 절대 경로
#define UPTIME "/proc/uptime"		// /proc/uptime 절대 경로
#define CPUSTAT "/proc/stat"		// /proc/stat 절대 경로
#define MEMINFO "/proc/meminfo"		// /proc/meminfo 절대 경로
#define DEV "/dev"					// /dev 절대 경로

#define FD__ZERO "/fd/0"			// /proc/pid 에서의 0번째 fd 경로
#define STAT "/stat"				// /proc/pid에서의 stat 경로
#define STATUS "/status"			// /proc/pid에서의 status 경로
#define CMDLINE "/cmdline"			// /proc/pid에서의 cmdline 경로
#define VMSIZE "VmSize"				// /proc/pid/status에서 VmSize있는지 확인하기 위한 문자열

// /proc/pid/stat에서의 idx
#define STAT_PID_IDX 0
#define STAT_CMD_IDX 1
#define STAT_STATE_IDX 2
#define STAT_SID_IDX 5
#define STAT_TTY_NR_IDX 6
#define STAT_TPGID_IDX 7
#define STAT_UTIME_IDX 13
#define STAT_STIME_IDX 14
#define STAT_PRIORITY_IDX 17
#define STAT_NICE_IDX 18
#define STAT_N_THREAD_IDX 19
#define STAT_START_TIME_IDX 21

// /proc/pid/status에서의 row
#define STATUS_VSZ_ROW 18
#define STATUS_VMLCK_ROW 19
#define STATUS_RSS_ROW 22
#define STATUS_SHR_ROW 24

// /proc/stat 에서의 idx
#define CPU_STAT_US_IDX 1
#define CPU_STAT_SY_IDX 2
#define CPU_STAT_NI_IDX 3
#define CPU_STAT_ID_IDX 4
#define CPU_STAT_WA_IDX 5
#define CPU_STAT_HI_IDX 6
#define CPU_STAT_SI_IDX 7
#define CPU_STAT_ST_IDX 8

// /proc/meminfo 에서의 row
#define MEMINFO_MEM_TOTAL_ROW 1
#define MEMINFO_MEM_FREE_ROW 2
#define MEMINFO_MEM_AVAILABLE_ROW 3
#define MEMINFO_BUFFERS_ROW 4
#define MEMINFO_CACHED_ROW 5
#define MEMINFO_SWAP_TOTAL_ROW 15
#define MEMINFO_SWAP_FREE_ROW 16
#define MEMINFO_S_RECLAIMABLE_ROW 24

// column에 출력할 문자열
#define PID_STR "PID"
#define USER_STR "USER"
#define PR_STR "PR"
#define NI_STR "NI"
#define VSZ_STR "VSZ"
#define VIRT_STR "VIRT"
#define RSS_STR "RSS"
#define RES_STR "RES"
#define SHR_STR "SHR"
#define S_STR "S"
#define STAT_STR "STAT"
#define START_STR "START"
#define TTY_STR "TTY"
#define CPU_STR "%CPU"
#define MEM_STR "MEM"
#define TIME_STR "TIME"
#define TIME_P_STR "TIME+"
#define CMD_STR "CMD"
#define COMMAND_STR "COMMAND"

#define TAB_WIDTH 8					//tab 길이

//process를 추상화 한 myProc 구조체
typedef struct{
	unsigned long pid;
	unsigned long uid;			//USER 구하기 위한 uid
	char user[UNAME_LEN];		//user명
	long double cpu;			//cpu 사용률
	long double mem;			//메모리 사용률
	unsigned long vsz;			//가상 메모리 사용량
	unsigned long rss;			//실제 메모리 사용량
	unsigned long shr;			//공유 메모리 사용량
	int priority;				//우선순위
	int nice;					//nice 값
	char tty[TTY_LEN];			//터미널
	char stat[STAT_LEN];		//상태
	char start[TIME_LEN];		//프로세스 시작 시각
	char time[TIME_LEN];		//총 cpu 사용 시간
	char cmd[CMD_LEN];			//option 없을 경우에만 출력되는 command (short)
	char command[CMD_LEN];		//option 있을 경우에 출력되는 command (long)
	
}myProc;

//src를 소숫점 아래 rdx+1자리에서 반올림하는 함수
long double round_double(long double src, int rdx);

//Kib 단위를 Kb로 변환시키는 함수
unsigned long kib_to_kb(unsigned long kib);

//path에 대한 tty 얻는 함수
void getTTY(char path[PATH_LEN], char tty[TTY_LEN]);

// /proc/uptime에서 OS 부팅 후 지난 시간 얻는 함수
unsigned long get_uptime(void);

// /proc/meminfo에서 전체 물리 메모리 크기 얻는 함수
unsigned long get_mem_total(void);

// pid 디렉터리 내의 파일들을 이용해 myProc 완성하는 함수
void add_proc_list(char path[PATH_LEN], bool isPPS, bool aOption, bool uOption, bool xOption, unsigned long cpuTimeTable[]);

//	/proc 디렉터리 탐색하는 함수
void search_proc(bool isPPS, bool aOption, bool uOption, bool xOption, unsigned long cpuTimeTable[]);

//proc의 내용을 지우는 함수
void erase_proc(myProc *proc);

// procList 내용 지우는 함수
void erase_proc_list(void);
