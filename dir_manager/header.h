#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <syslog.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#define PROMPT_STRING "$ "
#define DAEMON_PID_FILE "../.daemonPid"							// daemon process의 pid 저장할 파일 경로

#define NAME_LEN 20												// 파일명 최대 길이
#define PATH_LEN 200											// 절대 경로 최대 길이
#define BUFFER_SIZE 256
#define ARGV_MAX 7												// 명령어 인자 최대 개수
#define CMD_BUFFER (ARGV_MAX * NAME_LEN + ARGV_MAX)				// 명령어 저장 문자열 최대 길이
#define TIME_LEN 21												// 시간 정보 저장 문자열 최대 길이
#define LOG_LEN (TIME_LEN + NAME_LEN + 10)						// LOG 저장 문자열 최대 길이
#define MAX_DUP_CNT 10											// 최대 중복 파일 개수

#define MAX_INFO_SIZE 2048										// ./trash/info 의 최대 size (2kb)
#define DEFAULT_INFO_FILE_SIZE 50								// 최소 info file size
#define MAX_INFO_CNT (MAX_INFO_SIZE / DEFAULT_INFO_FILE_SIZE) 	// info 파일 최대 개수

#define MAX_DEPTH 10											// tree에서 최대 깊이
#define TREE_NAME_LEN (NAME_LEN + 2)							// tree 출력 시 파일명 길이

typedef enum {false, true} bool;


//File 정보 저장하는 Tree Node
typedef struct FileNode{
	char name[NAME_LEN];
	int childCnt;
	struct FileNode * parent;
	struct FileNode ** child;
	time_t mtime;
	off_t size;
}FileNode;


//시간 정보 저장하는 구조체
typedef struct{
	int year;
	int month;
	int day;
	int hour;
	int min;
	int sec;
}customTime;

//delete 목록 linked list의 Node
typedef struct Node {
	char path[PATH_LEN];
	customTime ct;
	bool iOption;
	bool rOption;
	struct Node *prev;
	struct Node *next;
}Node;


//명령어 종류 판단하는 열거형
enum CMD_CASE {NONE, DELETE, SIZE, RECOVER, TREE, EXIT, HELP};


//파일 변경 상태 판단하는 열거형
enum FILE_STATUS {CREATED, DELETED, MODIFIED};
