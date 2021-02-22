#include "header.h"

// time.c
extern void time_to_custom_time(time_t t_time, CustomTime *ct);
extern void custom_time_to_str(CustomTime *ct, char *str);
// tree.c
extern void print_tree(FileNode *root, bool pipe[MAX_DEPTH], int depth);
extern void make_tree(FileNode *root, struct dirent **fileList, int cnt, char *path);
extern void free_tree(FileNode *root);
extern bool compare_tree(FileNode *before, FileNode* now);

pid_t g_daemon_pid;
int g_log_fd;
FileNode *g_before_tree_root;
FileNode *g_now_tree_root;

// log.txt에 log 추가하는 함수
void add_log(FileStatus status, char *fname)
{
  char log[LOG_LEN];
  // 현재 시간 불러오기
  time_t now;
  now = time(NULL);
  CustomTime ct;
  time_to_custom_time(now, &ct);
  // 현재 시간 정보를 문자열에 추가
  log[0] = '[';
  custom_time_to_str(&ct, log+1);
  strcat(log, "][");
  // 파일 상태를 문자열에 추가
  switch (status) {
    case CREATED:
      strcat(log, "create");
      break;
    case DELETED:
      strcat(log, "delete");
      break;
    case MODIFIED:
      strcat(log, "modify");
      break;
    default:
      break;
  }
  strcat(log, "_");
  // 파일명을 문자열에 추가
  strcat(log, fname);
  strcat(log, "]\n");
  // log.txt에 write
  write(g_log_fd, log, strlen(log));
  return;
}

// daemon process가 매 초마다 실행하는 함수
void daemon_execute(void)
{
  g_now_tree_root = (FileNode *)malloc(sizeof(FileNode) * 1);
  make_tree(g_now_tree_root, NULL, -1, ".");
  bool pipe[MAX_DEPTH];
  compare_tree(g_before_tree_root, g_now_tree_root);
  if (g_before_tree_root != NULL) {
    free_tree(g_before_tree_root);
  }
  g_before_tree_root = g_now_tree_root;
  return;
}

// daemon process 생성 함수
int daemon_init(void)
{
  pid_t pid;
  // 자식 process 생성
  if ((pid = fork()) < 0) {
    fprintf(stderr, "fork error\n");
    exit(1);
  }
  // 1번 규칙: 부모 process는 종료
  else if (pid != 0) {
    g_daemon_pid = pid;
    exit(0);
  }
  // 2번 규칙: 자기자신을 leader로 하는 session 생성
  setsid();
  // 3번 규칙: signal 무시
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
  signal(SIGTSTP, SIG_IGN);
  g_daemon_pid = getpid();
  int dp;
  if ((dp = open("../.g_daemon_pid", O_RDWR | O_CREAT | O_APPEND, 0644)) < 0) {
    fprintf(stderr, "g_daemon_pid open error\n");
    exit(1);
  }
  char buf[10];          // g_daemon_pid 저장할 문자열
  memset(buf, (char)0, sizeof(buf));
  sprintf(buf, "%d\n", g_daemon_pid);
  write(dp, buf, sizeof(buf));  // .g_daemon_pid 파일에 pid 저장
  close(dp);
  // 6번 규칙: 모든 file descripter 닫기
  int maxfd = getdtablesize();
  for (int fd = 0; fd < maxfd; fd++) {
    close(fd);
  }
  // 4번 규칙: 파일 mode 생성 mask 해제
  umask(0);
  // 5번 규칙: 현재 directory를 root로 지정
  // 7번 규칙: 표준 입출력, error를 null로 지정
  int tmpFd = open("/dev/null", O_RDWR);
  dup(0);
  dup(0);
  return 0;
}

// daemon process 생성 및 관리하는 함수
int daemon_main(void)
{
  // daemon process 생성
  if (daemon_init() < 0) {
    fprintf(stderr, "daemon_init failed\n");
    exit(1);
  }
  // log.txt file open
  g_log_fd = open("../log.txt", O_RDWR | O_CREAT | O_APPEND, 0644);
  while (1) {
    daemon_execute();
    sleep(1);
  }
  close(g_log_fd);
  exit(0);
}
