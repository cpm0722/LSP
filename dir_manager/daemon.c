#include "header.h"

pid_t daemonPid;
int fd;

FileNode *beforeRoot;
FileNode *nowRoot;

extern void time_to_customTime(time_t t_time, customTime *ct);
extern void customTime_to_str(customTime *ct, char *str);

extern void print_tree(FileNode *root, bool pipe[MAX_DEPTH], int depth);
extern void make_tree(FileNode *root, struct dirent **fileList, int cnt, char *path);
extern void free_tree(FileNode *root);
extern bool compare_tree(FileNode *before, FileNode* now);

//log.txt에 log 추가하는 함수
void add_log(enum FILE_STATUS status, char *fname)
{
	char log[LOG_LEN];
	//현재 시간 불러오기
	time_t now;
	now = time(NULL);
	customTime ct;
	time_to_customTime(now, &ct);
	//현재 시간 정보를 문자열에 추가
	log[0] = '[';
	customTime_to_str(&ct, log+1);
	strcat(log, "][");
	//파일 상태를 문자열에 추가
	switch(status){
		case CREATED:
			strcat(log, "create");
			break;
		case DELETED:
			strcat(log, "delete");
			break;
		case MODIFIED:
			strcat(log, "modify");
			break;
	}
	strcat(log, "_");
	//파일명을 문자열에 추가
	strcat(log, fname);
	strcat(log, "]\n");
	//log.txt에 write
	write(fd, log, strlen(log));
	return;
}

//daemon process가 매 초마다 실행하는 함수
void daemon_do(void)
{
	nowRoot = (FileNode *)malloc(sizeof(FileNode) * 1);
	make_tree(nowRoot, NULL, -1, ".");
	bool pipe[MAX_DEPTH];
	compare_tree(beforeRoot, nowRoot);
	if(beforeRoot != NULL)
		free_tree(beforeRoot);
	beforeRoot = nowRoot;
	return;
}

//daemon process 생성 함수
int daemon_init(void)
{
	pid_t pid;

	if ((pid = fork()) < 0) {	//자식 process 생성
		fprintf(stderr, "fork error\n");
		exit(1);
	}
	else if (pid != 0){	//1번 규칙: 부모 process는 종료
		daemonPid = pid;
		exit(0);
	}

	//2번 규칙: 자기자신을 leader로 하는 session 생성
	setsid();
	//3번 규칙: signal 무시
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);

	daemonPid = getpid();
	int dp;
	if((dp = open("../.daemonPid", O_RDWR | O_CREAT | O_APPEND, 0644)) < 0){
		fprintf(stderr, "daemonPid open error\n");
		exit(1);
	}
	char buf[10];					//daemonPid 저장할 문자열
	memset(buf, (char)0, sizeof(buf));
	sprintf(buf, "%d\n", daemonPid);
	write(dp, buf, sizeof(buf));	// .daemonPid 파일에 pid 저장
	close(dp);

	int maxfd = getdtablesize();

	//6번 규칙: 모든 file descripter 닫기
	for (int fd = 0; fd < maxfd; fd++)
		close(fd);

	//4번 규칙: 파일 mode 생성 mask 해제
	umask(0);
	//5번 규칙: 현재 directory를 root로 지정
	//7번 규칙: 표준 입출력, error를 null로 지정
	int tmpFd = open("/dev/null", O_RDWR);
	dup(0);
	dup(0);

	return 0;
}

//daemon process 생성 및 관리하는 함수
int daemon_main(void)
{
	if (daemon_init() < 0) {	//daemon process 생성
		fprintf(stderr, "daemon_init failed\n");
		exit(1);
	}
	
	//log.txt file open
	fd = open("../log.txt", O_RDWR | O_CREAT | O_APPEND, 0644);

	while (1) {
		daemon_do();
		sleep(1);
	}

	close(fd);
	exit(0);
}
